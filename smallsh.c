#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stddef.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>

char *replace_tilde_home(char *restrict *restrict haystack, char *restrict home_var);

int splitWords (char *line, char **words, char *delims);
char *str_gsub(char *restrict *restrict haystack, char const *restrict needle, char const *restrict sub);

void handle_SIGINT(int sig){
  //does nothing

}

int main(){

  // Creating all variables that we want to persist through each loop execution
  char *line = NULL;
  char *delims = NULL;
  size_t n = 0;
  // main array where we store each input word
  char *words[512];
  char *home = NULL;
  int temp_len = 0;
  char *temp_str = NULL;
  int num_words = 0;
  // expands "$?" 
  int fg_exit_status = 0;
  // expands "$!" 
  pid_t recent_bg_pid = 0;

  //the SIGTSTP signal shall be ignored by smallsh.
  // set its disposition to SIG_IGN
  struct sigaction SIGTSTP_action = {0};
  struct sigaction SIGTSTP_oldact = {0};
  sigfillset(&SIGTSTP_action.sa_mask);
  SIGTSTP_action.sa_handler = SIG_IGN;
  SIGTSTP_action.sa_flags = 0;
  sigaction(SIGTSTP, &SIGTSTP_action, &SIGTSTP_oldact);

  //SIGINT SHALL BE SET TO SIG_IGN AT ALL times except when reading a line of input
  struct sigaction SIGINT_action = {0};
  struct sigaction SIGINT_oldact = {0};
  sigfillset(&SIGINT_action.sa_mask);
  SIGINT_action.sa_handler = SIG_IGN;
  SIGINT_action.sa_flags = 0;
  sigaction(SIGINT, &SIGINT_action, &SIGINT_oldact);


  //initialize home variable for later use
  if (getenv("HOME") == NULL){
    home = realloc(home, (strlen("")+1)*sizeof(char));
    strcpy(home, ""); 

  }
  else{ 
    home = realloc(home, (strlen(getenv("HOME"))+1)*sizeof(char));
    strcpy(home, getenv("HOME"));

  }

  // main loop where our shell will take place
  for (;;){

    // resets error indicators after interrupt signal is handled
    if (errno == EINTR){
      fprintf(stderr, "\n");
    }
    errno = 0;
    clearerr(stdin);


    pid_t temp_pid;
    int temp_status; 
    //checking for all unwaited-for background processes. Processes process stopped, signalled and ended.
    while ((temp_pid = waitpid(0, &temp_status, WNOHANG | WUNTRACED)) > 0){
      if (WIFEXITED(temp_status)){
        fprintf(stderr, "Child process %jd done. Exit status %d. \n", (intmax_t) temp_pid, WEXITSTATUS(temp_status));
      } else if (WIFSIGNALED(temp_status)){
        fprintf(stderr, "Child process %jd done. Signaled %d. \n", (intmax_t) temp_pid, WTERMSIG(temp_status));

      } else if (WIFSTOPPED(temp_status)){
        fprintf(stderr, "Child process %d stopped. Continuing. \n", temp_pid);
        kill(temp_pid, SIGCONT);

      }
    }

    //get PS1 Parameter and print for prompt
    if (getenv("PS1") == NULL){
      fprintf(stderr, "");
    } 
    else {
      fprintf(stderr, "%s", getenv("PS1"));
    }
    if (getenv("IFS") == NULL){
      delims = realloc(delims, (strlen(" \t\n")+1)*sizeof(char));
      strcpy(delims, " \t\n");
    }
    else {
      delims = realloc(delims, (strlen(getenv("IFS"))+1)*sizeof(char));
      strcpy(delims, getenv("IFS"));
    }

    //sets SIGINT to be sent to a dummy handler
    SIGINT_action.sa_handler = handle_SIGINT;
    sigaction(SIGINT, &SIGINT_action, NULL);

    // taking user input and handling for EOF and SIGINT
    ssize_t line_length;
    if ((( line_length = getline(&line, &n, stdin))==-1) || errno==EINTR){
      if (feof(stdin)){
        fprintf(stderr, "\nexit\n");
        kill(0, SIGINT);
        exit(fg_exit_status);
      }
      continue;
    };
    
 
    // calls function to split input into the words array, returns number of words
    num_words = splitWords(line, words, delims);

    // resets SIGINT to ignore
    SIGINT_action.sa_handler = SIG_IGN;
    SIGINT_action.sa_flags=0;
    sigaction(SIGINT, &SIGINT_action, NULL);


    //----------------------------------------EXPANSION--------------------------------
    for (int i=0; i < num_words; i++){
      //expanding once for beginning tilde 
      if (words[i][0] == '~' && words[i][1] == '/'){
        if (getenv("HOME") == NULL){
          home = realloc(home, (strlen("")+1)*sizeof(char));
          strcpy(home, ""); 
          words[i] = replace_tilde_home(&words[i], home);
        }
        else{ 
          home = realloc(home, (strlen(getenv("HOME"))+1)*sizeof(char));
          strcpy(home, getenv("HOME"));
          words[i] = replace_tilde_home(&words[i], home);
        }
      }


      //Expand for any occurrence of "$$". Replace with pid of smallsh process (getpid)

      //gets length of pid as string, reallocates that amount of storage, then copies (int) pid to (str) pid_str
      //replacing pid_len and pid_str with temp variables
      temp_len = snprintf(NULL, 0, "%d", getpid());
      temp_str = realloc(temp_str, temp_len + 1);
      snprintf(temp_str, temp_len + 1, "%d", getpid());
      if (strstr(words[i], "$$") != NULL){
        words[i] = str_gsub(&words[i], "$$", temp_str);
      }


      //Expand for any occurence of "$?". Replace with exit status of last foreground command.
      temp_len = snprintf(NULL, 0, "%d", fg_exit_status);
      temp_str = realloc(temp_str, temp_len + 1);
      snprintf(temp_str, temp_len + 1, "%d", fg_exit_status);
      if (strstr(words[i], "$?") != NULL){
        words[i] = str_gsub(&words[i], "$?", temp_str);
      }

      //Replace "$!" with process ID of most recent background process 
      if (recent_bg_pid == 0){
        temp_str = realloc(temp_str, strlen("")+1);
        strcpy(temp_str, "");
      } else {
        temp_len = snprintf(NULL, 0, "%d", recent_bg_pid);
        temp_str = realloc(temp_str, temp_len + 1);
        snprintf(temp_str, temp_len + 1, "%d", recent_bg_pid);
      }
      if (strstr(words[i], "$!") != NULL){
        words[i] = str_gsub(&words[i], "$!", temp_str);
      }
      
    }
    //-----------------------------------------PARSING-----------------------------------
    //parse_idx holds the rolling index used to parse through the string from back to start
    int parse_idx = 0;
    int comment_end_idx = 0;
    bool comments = false;
    bool bg_process = false;
    int bg_id = 0;
    int input_redirect_idx = 0;
    bool input_redirect = false;
    int output_redirect_idx = 0;
    bool output_redirect = false;
    int end_args_idx = -1;


    //finds the index of either the first # or the end of the array(past the end)
    while ((parse_idx < num_words) && (strcmp(words[parse_idx], "#")!=0)){
      parse_idx += 1;
    }

    //parse_idx either equals idx of "#" or length of array

    //checking if there are comments
    //sets comment_end idx to either idx of comments or past the end of the array
    if (parse_idx != num_words){
      comments = true;
    }
    comment_end_idx = parse_idx;

    if (parse_idx == 0){
      continue;
    }

    //checks one word in for a background process token
    if (parse_idx-1 >= 0){
      if (strcmp(words[parse_idx-1], "&")==0){
        bg_process = true;
        bg_id = parse_idx-1;
        parse_idx -= 1;
      }     
    }
    //parse_idx either equals "&" idx or the previous value ("#" or end of array)
    //if "&" is first element, we need to continue
    if (parse_idx == 0){
      continue;
    }

    
    // if two elements before (either "&" or the end) is a "<", or ">" we know we have redirection
    if ((parse_idx-2 >= 0) && (strcmp(words[parse_idx - 2], "<")==0 || strcmp(words[parse_idx - 2], ">")==0)){
      if (strcmp(words[parse_idx - 2], "<")==0){
        input_redirect = true;
        input_redirect_idx = parse_idx - 2;  
        parse_idx -= 2;

        // checks if output is present
        if ((parse_idx-2 >= 0) && (strcmp(words[parse_idx - 2], ">")==0)){
          output_redirect = true;
          output_redirect_idx = parse_idx - 2;
          parse_idx -= 2;
        }

      }

      // checks through the same process but in the opposite order (output to input)
      else {
        output_redirect = true;
        output_redirect_idx = parse_idx - 2;
        parse_idx -= 2;
        if ((parse_idx-2 >= 0) && (strcmp(words[parse_idx - 2], "<") == 0)){
          input_redirect = true;
          input_redirect_idx = parse_idx - 2;
          parse_idx -= 2;
        }
      }
    } 
    // checks is final parsed index is at element 0, then continue
    if (parse_idx == 0){
      continue;
    }
    
    //parse_idx is the index after where arguments stop

    
    
    
    //-------------------------------------EXECUTION----------------------------------------
    //
     
    // just a double check that there is a command word
    if ((input_redirect && (input_redirect_idx == 0))  || (output_redirect && (output_redirect_idx == 0))){
    
      continue;
    }
    
    
  // words[0] is command word, words[1] up to and not including words[first_redirect] 
  // or words[1] up to the & symbol if there is no redirect at all
     
    //mallocing space for the new arguments array
    char **args = malloc((parse_idx + 1) * sizeof(char *));

  //creating space for hte argument index and copying the word to args (including the command)
    for (int i = 0; i < parse_idx; i++){
      args[i] = NULL;
      args[i] = strdup(words[i]);
    }
    
  // final element needs to be NULL for execvp
    args[parse_idx] = NULL;
    char *cmd = strdup(words[0]);  
  
 




//----------------------------BUILT-IN EXIT-------------------------------

    int isNumber = 1;
    if (strcmp(cmd, "exit")==0){
    //if there is one argument we pass that to the exit function
      if (args[1] != NULL){
      //if there are two or more arguments, print error message and continue
        if (args[2] != NULL){
          fprintf(stderr, "'exit' command takes at most one argument\n");
          continue;
        }
      //if the first argument isn't an int -> error message and continue
        for (int i = 0; i < strlen(args[1]); i++){
         isNumber = isNumber && isdigit(args[1][i]);
        }
        if (isNumber != 1){
          fprintf(stderr, "'exit' command argument must be of type int\n");
          continue;
        }
  
        //if only one numerical argument -> print error and exit
        fprintf(stderr, "\nexit\n");
        kill(0, SIGINT);
        exit(atoi(args[1]));
      } 
      //if args[1] is NULL -> print error and exit with fg_exit_status
      else 
      {
        fprintf(stderr, "\nexit\n");
        kill(0, SIGINT);
        exit(fg_exit_status);
      }
    } 
//-------------------------BUILT IN CD-------------------------------------
    else if (strcmp(cmd, "cd")==0){
      char cwd[256];


     // cd with no arguments
      if (args[1] == NULL){
        if (chdir(home) == -1){
          fprintf(stderr, "cd operation failed\n");
          continue;
        }
      }
     
    // cd with one argument
      else {
        //error check if there are two arguments
        if(args[2] != NULL){
          fprintf(stderr, "cd operation failed: too many arguments\n");
          continue;
        }
        if(chdir(args[1])==-1){
          fprintf(stderr, "cd operation failed\n");
          continue;
        } 
      }
    }
//------------------------------NON-BUILT-IN FUNCTIONS/FORKING-------------------------------
    else{
      // if not a built in function, need to fork
      // forking child process
      int childStatus;
      pid_t wait_res;
      int fd;
      pid_t spawnPid = fork();
      

      switch(spawnPid){
        case -1:
          //error on forking
          perror("fork() failed\n");
          exit(1);
          break;
        case 0: 
          //child process
          //
          //RESETTING SIGNAL HANDLERS FOR TSTP & INT
          sigaction(SIGTSTP, &SIGTSTP_oldact, NULL);
          sigaction(SIGINT, &SIGINT_oldact, NULL);
          //execv(words[0],
          //-------INPUT/OUTPUT REDIRECTION-------------------
          if (input_redirect){
            //error when file doesn't exist or cannot read
            fd = open(words[input_redirect_idx+1], O_RDONLY);
              if (fd == -1){
                fprintf(stderr, "open() failed on \"%s\"\n", words[input_redirect_idx+1]);
                exit(1);
              }
              dup2(fd, STDIN_FILENO);
              close(fd);
            }
          if (output_redirect){
            fd = open(words[output_redirect_idx+1], O_WRONLY | O_CREAT, 0777);
            if (fd == -1){
              fprintf(stderr, "open() failed on \"%s\"\n", words[output_redirect_idx+1]);
              exit(1);
            }
            dup2(fd, STDOUT_FILENO);
            close(fd);
          }
   
          // execute the process and error check
          if (execvp(cmd, args) == -1){
            fprintf(stderr, "Could not execute \"%s\" command\n", cmd);
            exit(1);
          };
        default:
          // PARENT PROCESS
          if (!bg_process){
            // FOREGROUND PROCESSES
            //  -perform a blocking wait on the process
            //  -update the "$?" variable with the exit status of the waited for command
            //    -childStatus contains the termination status of child process 
            
            wait_res = waitpid(spawnPid, &childStatus, WUNTRACED);
            //if process ended normally, update child status
            if (WIFEXITED(childStatus)){
              fg_exit_status = WEXITSTATUS(childStatus);
            } else if (WIFSIGNALED(childStatus)){
              // if process was signalled
              // send SIGCONT to continue process
              // print error & 
              // update $! recent_bg_pid 
              // update $? fg_exit_status to 128 + number of the signal
              fg_exit_status = 128 + WTERMSIG(childStatus);
              
              
            } else if (WIFSTOPPED(childStatus)){
              fprintf(stderr, "Child process %d stopped. Continuing\n", spawnPid);
              recent_bg_pid = spawnPid;
              kill(spawnPid, SIGCONT);
              wait_res = waitpid(spawnPid, &childStatus, WNOHANG | WUNTRACED);
            }

          } else {
            // BACKGROUND PROCESSES
            recent_bg_pid = spawnPid;
            wait_res = waitpid(spawnPid, &childStatus, WNOHANG | WUNTRACED);

          }
      }
    }



  // breaks out of main for loop
  }

  
  return 0;
}




int splitWords(char *line, char **words, char *delims){
//
// Splits main input from getline into words array
// Returns number of words saved in words array (int)
//
  int word_count = 0;
  char *split = strtok(line, delims);

  //strtoks through the input and duplicates the string into words[]
  if (split != NULL) {
    words[0] = strdup(split);
    word_count = 1;
  }

  while ((split = strtok(NULL, delims)) != NULL){
    words[word_count] = strdup(split);
    word_count += 1;
  }
  return word_count;
}





char *replace_tilde_home(char *restrict *restrict haystack, char *restrict home_var){
  // 
  // replaces ~/ to home variable value if found at the start of a word in word array
  //
  char *str = *haystack;
  size_t haystack_len = strlen(str);
  size_t const home_len = strlen(home_var);
  str = realloc(*haystack, sizeof **haystack * (haystack_len + home_len));
  if (!str){
    goto exit;
  } 
  memmove(str + home_len, str + 1, haystack_len - 1);
  memcpy(str, home_var, home_len);
 

  // need to shift string in case where home is empty string
  if (home_len < 1){
    str = realloc(*haystack, sizeof **haystack * (haystack_len));
    str[haystack_len-1] = '\0';
    if (!str){
      goto exit;
    }
    *haystack = str;
  }
   

exit:
  return str;
}



char *str_gsub(char *restrict *restrict haystack, char const *restrict needle, char const *restrict sub)
{
  //
  // Finds "needle" str in the "haystack" string and replaces "needle" with "sub" string
  // Happens in place
  //
  char *str = *haystack;
  size_t haystack_len = strlen(str);
  size_t const needle_len = strlen(needle),
                              sub_len = strlen(sub);

 
  for (;(str = strstr(str, needle));){
    ptrdiff_t off = str - *haystack;

    if (sub_len > needle_len){
      str = realloc(*haystack, sizeof **haystack * (haystack_len + sub_len - needle_len + 1));
      if (!str){
        goto exit;
      }
      *haystack = str;
      str = *haystack + off;
    }
    memmove(str + sub_len, str + needle_len, haystack_len + 1 - off - needle_len);
    memcpy(str, sub, sub_len);
    haystack_len = haystack_len + sub_len - needle_len;
    str += sub_len;
    

  }
  str = *haystack;
  if (sub_len < needle_len){
    str = realloc(*haystack, sizeof **haystack * (haystack_len +1));
    if (!str){
      goto exit;
    }
    *haystack = str;
  }
  
exit:
  return str;

}

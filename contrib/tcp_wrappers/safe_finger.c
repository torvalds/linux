 /*
  * safe_finger - finger client wrapper that protects against nasty stuff
  * from finger servers. Use this program for automatic reverse finger
  * probes, not the raw finger command.
  * 
  * Build with: cc -o safe_finger safe_finger.c
  * 
  * The problem: some programs may react to stuff in the first column. Other
  * programs may get upset by thrash anywhere on a line. File systems may
  * fill up as the finger server keeps sending data. Text editors may bomb
  * out on extremely long lines. The finger server may take forever because
  * it is somehow wedged. The code below takes care of all this badness.
  * 
  * Author: Wietse Venema, Eindhoven University of Technology, The Netherlands.
  */

#ifndef lint
static char sccsid[] = "@(#) safe_finger.c 1.4 94/12/28 17:42:41";
#endif

/* System libraries */

#include <sys/types.h>
#include <sys/stat.h>
#include <signal.h>
#include <stdio.h>
#include <ctype.h>
#include <pwd.h>

extern void exit();

/* Local stuff */

char    path[] = "PATH=/bin:/usr/bin:/usr/ucb:/usr/bsd:/etc:/usr/etc:/usr/sbin";

#define	TIME_LIMIT	60		/* Do not keep listinging forever */
#define	INPUT_LENGTH	100000		/* Do not keep listinging forever */
#define	LINE_LENGTH	128		/* Editors can choke on long lines */
#define	FINGER_PROGRAM	"finger"	/* Most, if not all, UNIX systems */
#define	UNPRIV_NAME	"nobody"	/* Preferred privilege level */
#define	UNPRIV_UGID	32767		/* Default uid and gid */

int     finger_pid;

void    cleanup(sig)
int     sig;
{
    kill(finger_pid, SIGKILL);
    exit(0);
}

main(argc, argv)
int     argc;
char  **argv;
{
    int     c;
    int     line_length = 0;
    int     finger_status;
    int     wait_pid;
    int     input_count = 0;
    struct passwd *pwd;

    /*
     * First of all, let's don't run with superuser privileges.
     */
    if (getuid() == 0 || geteuid() == 0) {
	if ((pwd = getpwnam(UNPRIV_NAME)) && pwd->pw_uid > 0) {
	    setgid(pwd->pw_gid);
	    setuid(pwd->pw_uid);
	} else {
	    setgid(UNPRIV_UGID);
	    setuid(UNPRIV_UGID);
	}
    }

    /*
     * Redirect our standard input through the raw finger command.
     */
    if (putenv(path)) {
	fprintf(stderr, "%s: putenv: out of memory", argv[0]);
	exit(1);
    }
    argv[0] = FINGER_PROGRAM;
    finger_pid = pipe_stdin(argv);

    /*
     * Don't wait forever (Peter Wemm <peter@gecko.DIALix.oz.au>).
     */
    signal(SIGALRM, cleanup);
    (void) alarm(TIME_LIMIT);

    /*
     * Main filter loop.
     */
    while ((c = getchar()) != EOF) {
	if (input_count++ >= INPUT_LENGTH) {	/* don't listen forever */
	    fclose(stdin);
	    printf("\n\n Input truncated to %d bytes...\n", input_count - 1);
	    break;
	}
	if (c == '\n') {			/* good: end of line */
	    putchar(c);
	    line_length = 0;
	} else {
	    if (line_length >= LINE_LENGTH) {	/* force end of line */
		printf("\\\n");
		line_length = 0;
	    }
	    if (line_length == 0) {		/* protect left margin */
		putchar(' ');
		line_length++;
	    }
	    if (isascii(c) && (isprint(c) || isspace(c))) {	/* text */
		if (c == '\\') {
		    putchar(c);
		    line_length++;
		}
		putchar(c);
		line_length++;
	    } else {				/* quote all other thash */
		printf("\\%03o", c & 0377);
		line_length += 4;
	    }
	}
    }

    /*
     * Wait until the finger child process has terminated and account for its
     * exit status. Which will always be zero on most systems.
     */
    while ((wait_pid = wait(&finger_status)) != -1 && wait_pid != finger_pid)
	 /* void */ ;
    return (wait_pid != finger_pid || finger_status != 0);
}

/* perror_exit - report system error text and terminate */

void    perror_exit(text)
char   *text;
{
    perror(text);
    exit(1);
}

/* pipe_stdin - pipe stdin through program (from my ANSI to OLD C converter) */

int     pipe_stdin(argv)
char  **argv;
{
    int     pipefds[2];
    int     pid;
    int     i;
    struct stat st;

    /*
     * The code that sets up the pipe requires that file descriptors 0,1,2
     * are already open. All kinds of mysterious things will happen if that
     * is not the case. The following loops makes sure that descriptors 0,1,2
     * are set up properly.
     */

    for (i = 0; i < 3; i++) {
	if (fstat(i, &st) == -1 && open("/dev/null", 2) != i)
	    perror_exit("open /dev/null");
    }

    /*
     * Set up the pipe that interposes the command into our standard input
     * stream.
     */

    if (pipe(pipefds))
	perror_exit("pipe");

    switch (pid = fork()) {
    case -1:					/* error */
	perror_exit("fork");
	/* NOTREACHED */
    case 0:					/* child */
	(void) close(pipefds[0]);		/* close reading end */
	(void) close(1);			/* connect stdout to pipe */
	if (dup(pipefds[1]) != 1)
	    perror_exit("dup");
	(void) close(pipefds[1]);		/* close redundant fd */
	(void) execvp(argv[0], argv);
	perror_exit(argv[0]);
	/* NOTREACHED */
    default:					/* parent */
	(void) close(pipefds[1]);		/* close writing end */
	(void) close(0);			/* connect stdin to pipe */
	if (dup(pipefds[0]) != 0)
	    perror_exit("dup");
	(void) close(pipefds[0]);		/* close redundant fd */
	return (pid);
    }
}

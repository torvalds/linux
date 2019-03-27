 /*
  * shell_cmd() takes a shell command after %<character> substitutions. The
  * command is executed by a /bin/sh child process, with standard input,
  * standard output and standard error connected to /dev/null.
  * 
  * Diagnostics are reported through syslog(3).
  * 
  * Author: Wietse Venema, Eindhoven University of Technology, The Netherlands.
  */

#ifndef lint
static char sccsid[] = "@(#) shell_cmd.c 1.5 94/12/28 17:42:44";
#endif

/* System libraries. */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/wait.h>
#include <signal.h>
#include <stdio.h>
#include <syslog.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

extern void exit();

/* Local stuff. */

#include "tcpd.h"

/* Forward declarations. */

static void do_child();

/* shell_cmd - execute shell command */

void    shell_cmd(command)
char   *command;
{
    int     child_pid;
    int     wait_pid;

    /*
     * Most of the work is done within the child process, to minimize the
     * risk of damage to the parent.
     */

    switch (child_pid = fork()) {
    case -1:					/* error */
	tcpd_warn("cannot fork: %m");
	break;
    case 00:					/* child */
	do_child(command);
	/* NOTREACHED */
    default:					/* parent */
	while ((wait_pid = wait((int *) 0)) != -1 && wait_pid != child_pid)
	     /* void */ ;
    }
}

/* do_child - exec command with { stdin, stdout, stderr } to /dev/null */

static void do_child(command)
char   *command;
{
    char   *error;
    int     tmp_fd;

    /*
     * Systems with POSIX sessions may send a SIGHUP to grandchildren if the
     * child exits first. This is sick, sessions were invented for terminals.
     */

    signal(SIGHUP, SIG_IGN);

    /* Set up new stdin, stdout, stderr, and exec the shell command. */

    for (tmp_fd = 0; tmp_fd < 3; tmp_fd++)
	(void) close(tmp_fd);
    if (open("/dev/null", 2) != 0) {
	error = "open /dev/null: %m";
    } else if (dup(0) != 1 || dup(0) != 2) {
	error = "dup: %m";
    } else {
	(void) execl("/bin/sh", "sh", "-c", command, (char *) 0);
	error = "execl /bin/sh: %m";
    }

    /* Something went wrong. We MUST terminate the child process. */

    tcpd_warn(error);
    _exit(0);
}

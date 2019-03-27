 /*
  * This program can be called via a remote shell command to find out if the
  * hostname and address are properly recognized, if username lookup works,
  * and (SysV only) if the TLI on top of IP heuristics work.
  * 
  * Example: "rsh host /some/where/try-from".
  * 
  * Diagnostics are reported through syslog(3) and redirected to stderr.
  * 
  * Author: Wietse Venema, Eindhoven University of Technology, The Netherlands.
  */

#ifndef lint
static char sccsid[] = "@(#) try-from.c 1.2 94/12/28 17:42:55";
#endif

/* System libraries. */

#include <sys/types.h>
#include <stdio.h>
#include <syslog.h>
#include <string.h>

#ifdef TLI
#include <sys/tiuser.h>
#include <stropts.h>
#endif

#ifndef STDIN_FILENO
#define	STDIN_FILENO	0
#endif

/* Local stuff. */

#include "tcpd.h"

int     allow_severity = SEVERITY;	/* run-time adjustable */
int     deny_severity = LOG_WARNING;	/* ditto */

main(argc, argv)
int     argc;
char  **argv;
{
    struct request_info request;
    char    buf[BUFSIZ];
    char   *cp;

    /*
     * Simplify the process name, just like tcpd would.
     */
    if ((cp = strrchr(argv[0], '/')) != 0)
	argv[0] = cp + 1;

    /*
     * Turn on the "IP-underneath-TLI" detection heuristics.
     */
#ifdef TLI
    if (ioctl(0, I_FIND, "timod") == 0)
	ioctl(0, I_PUSH, "timod");
#endif /* TLI */

    /*
     * Look up the endpoint information.
     */
    request_init(&request, RQ_DAEMON, argv[0], RQ_FILE, STDIN_FILENO, 0);
    (void) fromhost(&request);

    /*
     * Show some results. Name and address information is looked up when we
     * ask for it.
     */

#define EXPAND(str) percent_x(buf, sizeof(buf), str, &request)

    puts(EXPAND("client address  (%%a): %a"));
    puts(EXPAND("client hostname (%%n): %n"));
    puts(EXPAND("client username (%%u): %u"));
    puts(EXPAND("client info     (%%c): %c"));
    puts(EXPAND("server address  (%%A): %A"));
    puts(EXPAND("server hostname (%%N): %N"));
    puts(EXPAND("server process  (%%d): %d"));
    puts(EXPAND("server info     (%%s): %s"));

    return (0);
}

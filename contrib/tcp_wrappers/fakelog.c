 /*
  * This module intercepts syslog() library calls and redirects their output
  * to the standard output stream. For interactive testing.
  * 
  * Author: Wietse Venema, Eindhoven University of Technology, The Netherlands.
  */

#ifndef lint
static char sccsid[] = "@(#) fakelog.c 1.3 94/12/28 17:42:21";
#endif

#include <stdio.h>

#include "mystdarg.h"

/* openlog - dummy */

/* ARGSUSED */

void openlog(name, logopt, facility)
char   *name;
int     logopt;
int     facility;
{
    /* void */
}

/* vsyslog - format one record */

void vsyslog(severity, fmt, ap)
int     severity;
char   *fmt;
va_list ap;
{
    char    buf[BUFSIZ];

    vprintf(percent_m(buf, fmt), ap);
    printf("\n");
    fflush(stdout);
}

/* syslog - format one record */

/* VARARGS */

void VARARGS(syslog, int, severity)
{
    va_list ap;
    char   *fmt;

    VASTART(ap, int, severity);
    fmt = va_arg(ap, char *);
    vsyslog(severity, fmt, ap);
    VAEND(ap);
}

/* closelog - dummy */

void closelog()
{
    /* void */
}

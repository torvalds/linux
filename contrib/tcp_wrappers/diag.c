 /*
  * Routines to report various classes of problems. Each report is decorated
  * with the current context (file name and line number), if available.
  * 
  * tcpd_warn() reports a problem and proceeds.
  * 
  * tcpd_jump() reports a problem and jumps.
  * 
  * Author: Wietse Venema, Eindhoven University of Technology, The Netherlands.
  */

#ifndef lint
static char sccsid[] = "@(#) diag.c 1.1 94/12/28 17:42:20";
#endif

/* System libraries */

#include <syslog.h>
#include <stdio.h>
#include <setjmp.h>

/* Local stuff */

#include "tcpd.h"
#include "mystdarg.h"

struct tcpd_context tcpd_context;
jmp_buf tcpd_buf;

/* tcpd_diag - centralize error reporter */

static void tcpd_diag(severity, tag, format, ap)
int     severity;
char   *tag;
char   *format;
va_list ap;
{
    char    fmt[BUFSIZ];

    if (tcpd_context.file)
	sprintf(fmt, "%s: %s, line %d: %s",
		tag, tcpd_context.file, tcpd_context.line, format);
    else
	sprintf(fmt, "%s: %s", tag, format);
    vsyslog(severity, fmt, ap);
}

/* tcpd_warn - report problem of some sort and proceed */

void    VARARGS(tcpd_warn, char *, format)
{
    va_list ap;

    VASTART(ap, char *, format);
    tcpd_diag(LOG_WARNING, "warning", format, ap);
    VAEND(ap);
}

/* tcpd_jump - report serious problem and jump */

void    VARARGS(tcpd_jump, char *, format)
{
    va_list ap;

    VASTART(ap, char *, format);
    tcpd_diag(LOG_ERR, "error", format, ap);
    VAEND(ap);
    longjmp(tcpd_buf, AC_ERROR);
}

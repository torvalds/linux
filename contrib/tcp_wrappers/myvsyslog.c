 /*
  * vsyslog() for sites without. In order to enable this code, build with
  * -Dvsyslog=myvsyslog. We use a different name so that no accidents will
  * happen when vsyslog() exists. On systems with vsyslog(), syslog() is
  * typically implemented in terms of vsyslog().
  * 
  * Author: Wietse Venema, Eindhoven University of Technology, The Netherlands.
  */

#ifndef lint
static char sccsid[] = "@(#) myvsyslog.c 1.1 94/12/28 17:42:33";
#endif

#ifdef vsyslog

#include <stdio.h>

#include "tcpd.h"
#include "mystdarg.h"

myvsyslog(severity, format, ap)
int     severity;
char   *format;
va_list ap;
{
    char    fbuf[BUFSIZ];
    char    obuf[3 * STRING_LENGTH];

    vsprintf(obuf, percent_m(fbuf, format), ap);
    syslog(severity, "%s", obuf);
}

#endif

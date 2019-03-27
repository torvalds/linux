 /*
  * Misc routines that are used by tcpd and by tcpdchk.
  * 
  * Author: Wietse Venema, Eindhoven University of Technology, The Netherlands.
  *
  * $FreeBSD$
  */

#ifndef lint
static char sccsic[] = "@(#) misc.c 1.2 96/02/11 17:01:29";
#endif

#include <sys/types.h>
#include <sys/param.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>

#include "tcpd.h"

extern char *fgets();

#ifndef	INADDR_NONE
#define	INADDR_NONE	(-1)		/* XXX should be 0xffffffff */
#endif

/* xgets - fgets() with backslash-newline stripping */

char   *xgets(ptr, len, fp)
char   *ptr;
int     len;
FILE   *fp;
{
    int     got;
    char   *start = ptr;

    while (fgets(ptr, len, fp)) {
	got = strlen(ptr);
	if (got >= 1 && ptr[got - 1] == '\n') {
	    tcpd_context.line++;
	    if (got >= 2 && ptr[got - 2] == '\\') {
		got -= 2;
	    } else {
		return (start);
	    }
	}
	ptr += got;
	len -= got;
	ptr[0] = 0;
    }
    return (ptr > start ? start : 0);
}

/* split_at - break string at delimiter or return NULL */

char   *split_at(string, delimiter)
char   *string;
int     delimiter;
{
    char   *cp;

#ifdef INET6
    int bracket = 0;

    for (cp = string; cp && *cp; cp++) {
	switch (*cp) {
	case '[':
	    bracket++;
	    break;
	case ']':
	    bracket--;
	    break;
	default:
	    if (bracket == 0 && *cp == delimiter) {
		*cp++ = 0;
		return cp;
	    }
	    break;
	}
    }
    return (NULL);
#else
    if ((cp = strchr(string, delimiter)) != 0)
	*cp++ = 0;
    return (cp);
#endif
}

/* dot_quad_addr - convert dotted quad to internal form */

unsigned long dot_quad_addr(str)
char   *str;
{
    int     in_run = 0;
    int     runs = 0;
    char   *cp = str;

    /* Count the number of runs of non-dot characters. */

    while (*cp) {
	if (*cp == '.') {
	    in_run = 0;
	} else if (in_run == 0) {
	    in_run = 1;
	    runs++;
	}
	cp++;
    }
    return (runs == 4 ? inet_addr(str) : INADDR_NONE);
}

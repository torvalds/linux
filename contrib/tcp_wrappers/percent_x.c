 /*
  * percent_x() takes a string and performs %<char> expansions. It aborts the
  * program when the expansion would overflow the output buffer. The result
  * of %<char> expansion may be passed on to a shell process. For this
  * reason, characters with a special meaning to shells are replaced by
  * underscores.
  * 
  * Diagnostics are reported through syslog(3).
  * 
  * Author: Wietse Venema, Eindhoven University of Technology, The Netherlands.
  */

#ifndef lint
static char sccsid[] = "@(#) percent_x.c 1.4 94/12/28 17:42:37";
#endif

/* System libraries. */

#include <stdio.h>
#include <syslog.h>
#include <string.h>
#include <unistd.h>

extern void exit();

/* Local stuff. */

#include "tcpd.h"

/* percent_x - do %<char> expansion, abort if result buffer is too small */

char   *percent_x(result, result_len, string, request)
char   *result;
int     result_len;
char   *string;
struct request_info *request;
{
    char   *bp = result;
    char   *end = result + result_len - 1;	/* end of result buffer */
    char   *expansion;
    int     expansion_len;
    static char ok_chars[] = "1234567890!@%-_=+:,./\
abcdefghijklmnopqrstuvwxyz\
ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    char   *str = string;
    char   *cp;
    int     ch;

    /*
     * Warning: we may be called from a child process or after pattern
     * matching, so we cannot use clean_exit() or tcpd_jump().
     */

    while (*str) {
	if (*str == '%' && (ch = str[1]) != 0) {
	    str += 2;
	    expansion =
		ch == 'a' ? eval_hostaddr(request->client) :
		ch == 'A' ? eval_hostaddr(request->server) :
		ch == 'c' ? eval_client(request) :
		ch == 'd' ? eval_daemon(request) :
		ch == 'h' ? eval_hostinfo(request->client) :
		ch == 'H' ? eval_hostinfo(request->server) :
		ch == 'n' ? eval_hostname(request->client) :
		ch == 'N' ? eval_hostname(request->server) :
		ch == 'p' ? eval_pid(request) :
		ch == 's' ? eval_server(request) :
		ch == 'u' ? eval_user(request) :
		ch == '%' ? "%" : (tcpd_warn("unrecognized %%%c", ch), "");
	    for (cp = expansion; *(cp += strspn(cp, ok_chars)); /* */ )
		*cp = '_';
	    expansion_len = cp - expansion;
	} else {
	    expansion = str++;
	    expansion_len = 1;
	}
	if (bp + expansion_len >= end) {
	    tcpd_warn("percent_x: expansion too long: %.30s...", result);
	    sleep(5);
	    exit(0);
	}
	memcpy(bp, expansion, expansion_len);
	bp += expansion_len;
    }
    *bp = 0;
    return (result);
}

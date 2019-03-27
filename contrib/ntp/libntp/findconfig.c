
#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#ifdef NEED_HPUX_FINDCONFIG
#include <string.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <unistd.h>

const char *
FindConfig(
	const char *base
	)
{
	static char result[BUFSIZ];
	char hostname[BUFSIZ], *cp;
	struct stat sbuf; 
	struct utsname unamebuf; 

	/* All keyed by initial target being a directory */
	strlcpy(result, base, sizeof(result));
	if (stat(result, &sbuf) == 0) {
		if (S_ISDIR(sbuf.st_mode)) {

			/* First choice is my hostname */
			if (gethostname(hostname, BUFSIZ) >= 0) {
				snprintf(result, sizeof(result), "%s/%s", base, hostname);
				if (stat(result, &sbuf) == 0) {
					goto outahere;
				} else {

					/* Second choice is of form default.835 */
					(void) uname(&unamebuf);
					if (strncmp(unamebuf.machine, "9000/", 5) == 0)
					    cp = unamebuf.machine + 5;
					else
					    cp = unamebuf.machine;
					snprintf(result, sizeof(result), "%s/default.%s", base, cp);
					if (stat(result, &sbuf) == 0) {
						goto outahere;
					} else {

						/* Last choice is just default */
						snprintf(result, sizeof(result), "%s/default", base);
						if (stat(result, &sbuf) == 0) {
							goto outahere;
						} else {
							strlcpy(result,
								"/not/found",
								sizeof(result));
						}
					}
				}
			} 
		} 
	}
    outahere:
	return(result);
}
#else
#include "ntp_stdlib.h"

const char *
FindConfig(
	const char *base
	)
{
	return base;
}
#endif

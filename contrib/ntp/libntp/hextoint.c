/*
 * hextoint - convert an ascii string in hex to an unsigned
 *	      long, with error checking
 */
#include <config.h>
#include <ctype.h>

#include "ntp_stdlib.h"

int
hextoint(
	const char *str,
	u_long *pu
	)
{
	register u_long u;
	register const char *cp;

	cp = str;

	if (*cp == '\0')
		return 0;

	u = 0;
	while (*cp != '\0') {
		if (!isxdigit((unsigned char)*cp))
			return 0;
		if (u & 0xF0000000)
			return 0;	/* overflow */
		u <<= 4;
		if ('0' <= *cp && *cp <= '9')
			u += *cp++ - '0';
		else if ('a' <= *cp && *cp <= 'f')
			u += *cp++ - 'a' + 10;
		else if ('A' <= *cp && *cp <= 'F')
			u += *cp++ - 'A' + 10;
		else
			return 0;
	}
	*pu = u;
	return 1;
}

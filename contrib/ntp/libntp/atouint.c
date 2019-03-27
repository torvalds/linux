#include <config.h>
#include <sys/types.h>
#include <ctype.h>

#include "ntp_types.h"
#include "ntp_stdlib.h"

/*
 * atouint() - convert an ascii string representing a whole base 10
 *	       number to u_long *uval, returning TRUE if successful.
 *	       Does not modify *uval and returns FALSE if str is not
 *	       a positive base10 integer or is too large for a u_int32.
 *	       this function uses u_long but should use u_int32, and
 *	       probably be renamed.
 */
int
atouint(
	const char *str,
	u_long *uval
	)
{
	u_long u;
	const char *cp;

	cp = str;
	if ('\0' == *cp)
		return 0;

	u = 0;
	while ('\0' != *cp) {
		if (!isdigit((unsigned char)*cp))
			return 0;
		if (u > 429496729 || (u == 429496729 && *cp >= '6'))
			return 0;		/* overflow */
		/* hand-optimized u *= 10; */
		u = (u << 3) + (u << 1);
		u += *cp++ - '0';		/* not '\0' */
	}

	*uval = u;
	return 1;
}

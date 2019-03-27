/*
 * hextolfp - convert an ascii hex string to an l_fp number
 */
#include <config.h>
#include <stdio.h>
#include <ctype.h>

#include "ntp_fp.h"
#include "ntp_string.h"
#include "ntp_stdlib.h"

int
hextolfp(
	const char *str,
	l_fp *lfp
	)
{
	register const char *cp;
	register const char *cpstart;
	register u_long dec_i;
	register u_long dec_f;
	char *ind = NULL;
	static const char *digits = "0123456789abcdefABCDEF";

	dec_i = dec_f = 0;
	cp = str;

	/*
	 * We understand numbers of the form:
	 *
	 * [spaces]8_hex_digits[.]8_hex_digits[spaces|\n|\0]
	 */
	while (isspace((unsigned char)*cp))
	    cp++;
	
	cpstart = cp;
	while (*cp != '\0' && (cp - cpstart) < 8 &&
	       (ind = strchr(digits, *cp)) != NULL) {
		dec_i = dec_i << 4;	/* multiply by 16 */
		dec_i += ((ind - digits) > 15)
			? (u_long)(ind - digits - 6)
			: (u_long)(ind - digits);
		cp++;
	}

	if ((cp - cpstart) < 8 || ind == NULL)
	    return 0;
	if (*cp == '.')
	    cp++;

	cpstart = cp;
	while (*cp != '\0' && (cp - cpstart) < 8 &&
	       (ind = strchr(digits, *cp)) != NULL) {
		dec_f = dec_f << 4;	/* multiply by 16 */
		dec_f += ((ind - digits) > 15)
			? (u_long)(ind - digits - 6)
			: (u_long)(ind - digits);
		cp++;
	}

	if ((cp - cpstart) < 8 || ind == NULL)
	    return 0;
	
	if (*cp != '\0' && !isspace((unsigned char)*cp))
	    return 0;

	lfp->l_ui = dec_i;
	lfp->l_uf = dec_f;
	return 1;
}

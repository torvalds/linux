/*
 * atolfp - convert an ascii string to an l_fp number
 */
#include <config.h>
#include <stdio.h>
#include <ctype.h>

#include "ntp_fp.h"
#include "ntp_string.h"
#include "ntp_assert.h"

/*
 * Powers of 10
 */
static u_long ten_to_the_n[10] = {
	0,
	10,
	100,
	1000,
	10000,
	100000,
	1000000,
	10000000,
	100000000,
	1000000000,
};


int
atolfp(
	const char *str,
	l_fp *lfp
	)
{
	register const char *cp;
	register u_long dec_i;
	register u_long dec_f;
	char *ind;
	int ndec;
	int isneg;
	static const char *digits = "0123456789";

	REQUIRE(str != NULL);

	isneg = 0;
	dec_i = dec_f = 0;
	ndec = 0;
	cp = str;

	/*
	 * We understand numbers of the form:
	 *
	 * [spaces][-|+][digits][.][digits][spaces|\n|\0]
	 */
	while (isspace((unsigned char)*cp))
	    cp++;
	
	if (*cp == '-') {
		cp++;
		isneg = 1;
	}
	
	if (*cp == '+')
	    cp++;

	if (*cp != '.' && !isdigit((unsigned char)*cp))
	    return 0;

	while (*cp != '\0' && (ind = strchr(digits, *cp)) != NULL) {
		dec_i = (dec_i << 3) + (dec_i << 1);	/* multiply by 10 */
		dec_i += (u_long)(ind - digits);
		cp++;
	}

	if (*cp != '\0' && !isspace((unsigned char)*cp)) {
		if (*cp++ != '.')
		    return 0;
	
		while (ndec < 9 && *cp != '\0'
		       && (ind = strchr(digits, *cp)) != NULL) {
			ndec++;
			dec_f = (dec_f << 3) + (dec_f << 1);	/* *10 */
			dec_f += (u_long)(ind - digits);
			cp++;
		}

		while (isdigit((unsigned char)*cp))
		    cp++;
		
		if (*cp != '\0' && !isspace((unsigned char)*cp))
		    return 0;
	}

	if (ndec > 0) {
		register u_long tmp;
		register u_long bit;
		register u_long ten_fact;

		ten_fact = ten_to_the_n[ndec];

		tmp = 0;
		bit = 0x80000000;
		while (bit != 0) {
			dec_f <<= 1;
			if (dec_f >= ten_fact) {
				tmp |= bit;
				dec_f -= ten_fact;
			}
			bit >>= 1;
		}
		if ((dec_f << 1) > ten_fact)
		    tmp++;
		dec_f = tmp;
	}

	if (isneg)
	    M_NEG(dec_i, dec_f);
	
	lfp->l_ui = dec_i;
	lfp->l_uf = dec_f;
	return 1;
}

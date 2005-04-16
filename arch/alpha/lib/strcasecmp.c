/*
 *  linux/arch/alpha/lib/strcasecmp.c
 */

#include <linux/string.h>


/* We handle nothing here except the C locale.  Since this is used in
   only one place, on strings known to contain only 7 bit ASCII, this
   is ok.  */

int strcasecmp(const char *a, const char *b)
{
	int ca, cb;

	do {
		ca = *a++ & 0xff;
		cb = *b++ & 0xff;
		if (ca >= 'A' && ca <= 'Z')
			ca += 'a' - 'A';
		if (cb >= 'A' && cb <= 'Z')
			cb += 'a' - 'A';
	} while (ca == cb && ca != '\0');

	return ca - cb;
}

/*
 *  linux/arch/xtensa/lib/strcasecmp.c
 *
 *  This file is subject to the terms and conditions of the GNU General
 *  Public License.  See the file "COPYING" in the main directory of
 *  this archive for more details.
 *
 *  Copyright (C) 2002 Tensilica Inc.
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

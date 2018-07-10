/* vi: set sw=4 ts=4: */
/*
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
//kbuild:lib-y += percent_decode.o

#include "libbb.h"

static unsigned hex_to_bin(unsigned char c)
{
	unsigned v;

	v = c - '0';
	if (v <= 9)
		return v;
	/* c | 0x20: letters to lower case, non-letters
	 * to (potentially different) non-letters */
	v = (unsigned)(c | 0x20) - 'a';
	if (v <= 5)
		return v + 10;
	return ~0;
/* For testing:
void t(char c) { printf("'%c'(%u) %u\n", c, c, hex_to_bin(c)); }
int main() { t(0x10); t(0x20); t('0'); t('9'); t('A'); t('F'); t('a'); t('f');
t('0'-1); t('9'+1); t('A'-1); t('F'+1); t('a'-1); t('f'+1); return 0; }
*/
}

char* FAST_FUNC percent_decode_in_place(char *str, int strict)
{
	/* note that decoded string is always shorter than original */
	char *src = str;
	char *dst = str;
	char c;

	while ((c = *src++) != '\0') {
		unsigned v;

		if (!strict && c == '+') {
			*dst++ = ' ';
			continue;
		}
		if (c != '%') {
			*dst++ = c;
			continue;
		}
		v = hex_to_bin(src[0]);
		if (v > 15) {
 bad_hex:
			if (strict)
				return NULL;
			*dst++ = '%';
			continue;
		}
		v = (v * 16) | hex_to_bin(src[1]);
		if (v > 255)
			goto bad_hex;
		if (strict && (v == '/' || v == '\0')) {
			/* caller takes it as indication of invalid
			 * (dangerous wrt exploits) chars */
			return str + 1;
		}
		*dst++ = v;
		src += 2;
	}
	*dst = '\0';
	return str;
}

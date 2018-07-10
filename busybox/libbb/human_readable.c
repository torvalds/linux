/* vi: set sw=4 ts=4: */
/*
 * June 30, 2001                 Manuel Novoa III
 *
 * All-integer version (hey, not everyone has floating point) of
 * make_human_readable_str, modified from similar code I had written
 * for busybox several months ago.
 *
 * Notes:
 *   1) I'm using an unsigned long long to hold the product size * block_size,
 *      as df (which calls this routine) could request a representation of a
 *      partition size in bytes > max of unsigned long.  If long longs aren't
 *      available, it would be possible to do what's needed using polynomial
 *      representations (say, powers of 1024) and manipulating coefficients.
 *      The base ten "bytes" output could be handled similarly.
 *
 *   2) This routine outputs a decimal point and a tenths digit when
 *      display_unit == 0.  Hence, it isn't uncommon for the returned string
 *      to have a length of 5 or 6.
 *
 *      If block_size is also 0, no decimal digits are printed.
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */
#include "libbb.h"

const char* FAST_FUNC make_human_readable_str(unsigned long long val,
	unsigned long block_size, unsigned long display_unit)
{
	static const char unit_chars[] ALIGN1 = {
		'\0', 'K', 'M', 'G', 'T', 'P', 'E', 'Z', 'Y'
	};

	unsigned frac; /* 0..9 - the fractional digit */
	const char *u;
	const char *fmt;

	if (val == 0)
		return "0";

	fmt = "%llu";
	if (block_size > 1)
		val *= block_size;
	frac = 0;
	u = unit_chars;

	if (display_unit) {
		val += display_unit/2;  /* Deal with rounding */
		val /= display_unit;    /* Don't combine with the line above! */
		/* will just print it as ulonglong (below) */
	} else {
		while ((val >= 1024)
		 /* && (u < unit_chars + sizeof(unit_chars) - 1) - always true */
		) {
			fmt = "%llu.%u%c";
			u++;
			frac = (((unsigned)val % 1024) * 10 + 1024/2) / 1024;
			val /= 1024;
		}
		if (frac >= 10) { /* we need to round up here */
			++val;
			frac = 0;
		}
#if 1
		/* If block_size is 0, dont print fractional part */
		if (block_size == 0) {
			if (frac >= 5) {
				++val;
			}
			fmt = "%llu%*c";
			frac = 1;
		}
#endif
	}

	return auto_string(xasprintf(fmt, val, frac, *u));
}


/* vda's implementations of the similar idea */

/* Convert unsigned long long value into compact 5-char representation.
 * String is not terminated (buf[5] is untouched) */
char* FAST_FUNC smart_ulltoa5(unsigned long long ul, char buf[5], const char *scale)
{
	const char *fmt;
	char c;
	unsigned v, u, idx = 0;

	if (ul > 99999) { // do not scale if 99999 or less
		ul *= 10;
		do {
			ul /= 1024;
			idx++;
		} while (ul >= 100000);
	}
	v = ul; // ullong divisions are expensive, avoid them

	fmt = " 123456789";
	u = v / 10;
	v = v % 10;
	if (!idx) {
		// 99999 or less: use "12345" format
		// u is value/10, v is last digit
		c = buf[0] = " 123456789"[u/1000];
		if (c != ' ') fmt = "0123456789";
		c = buf[1] = fmt[u/100%10];
		if (c != ' ') fmt = "0123456789";
		c = buf[2] = fmt[u/10%10];
		if (c != ' ') fmt = "0123456789";
		buf[3] = fmt[u%10];
		buf[4] = "0123456789"[v];
	} else {
		// value has been scaled into 0..9999.9 range
		// u is value, v is 1/10ths (allows for 92.1M format)
		if (u >= 100) {
			// value is >= 100: use "1234M', " 123M" formats
			c = buf[0] = " 123456789"[u/1000];
			if (c != ' ') fmt = "0123456789";
			c = buf[1] = fmt[u/100%10];
			if (c != ' ') fmt = "0123456789";
			v = u % 10;
			u = u / 10;
			buf[2] = fmt[u%10];
		} else {
			// value is < 100: use "92.1M" format
			c = buf[0] = " 123456789"[u/10];
			if (c != ' ') fmt = "0123456789";
			buf[1] = fmt[u%10];
			buf[2] = '.';
		}
		buf[3] = "0123456789"[v];
		buf[4] = scale[idx]; /* typically scale = " kmgt..." */
	}
	return buf + 5;
}

/* Convert unsigned long long value into compact 4-char
 * representation. Examples: "1234", "1.2k", " 27M", "123T"
 * String is not terminated (buf[4] is untouched) */
char* FAST_FUNC smart_ulltoa4(unsigned long long ul, char buf[4], const char *scale)
{
	const char *fmt;
	char c;
	unsigned v, u, idx = 0;

	if (ul > 9999) { // do not scale if 9999 or less
		ul *= 10;
		do {
			ul /= 1024;
			idx++;
		} while (ul >= 10000);
	}
	v = ul; // ullong divisions are expensive, avoid them

	fmt = " 123456789";
	u = v / 10;
	v = v % 10;
	if (!idx) {
		// 9999 or less: use "1234" format
		// u is value/10, v is last digit
		c = buf[0] = " 123456789"[u/100];
		if (c != ' ') fmt = "0123456789";
		c = buf[1] = fmt[u/10%10];
		if (c != ' ') fmt = "0123456789";
		buf[2] = fmt[u%10];
		buf[3] = "0123456789"[v];
	} else {
		// u is value, v is 1/10ths (allows for 9.2M format)
		if (u >= 10) {
			// value is >= 10: use "123M', " 12M" formats
			c = buf[0] = " 123456789"[u/100];
			if (c != ' ') fmt = "0123456789";
			v = u % 10;
			u = u / 10;
			buf[1] = fmt[u%10];
		} else {
			// value is < 10: use "9.2M" format
			buf[0] = "0123456789"[u];
			buf[1] = '.';
		}
		buf[2] = "0123456789"[v];
		buf[3] = scale[idx]; /* typically scale = " kmgt..." */
	}
	return buf + 4;
}

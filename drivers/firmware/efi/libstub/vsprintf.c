// SPDX-License-Identifier: GPL-2.0-only
/* -*- linux-c -*- ------------------------------------------------------- *
 *
 *   Copyright (C) 1991, 1992 Linus Torvalds
 *   Copyright 2007 rPath, Inc. - All Rights Reserved
 *
 * ----------------------------------------------------------------------- */

/*
 * Oh, it's a waste of space, but oh-so-yummy for debugging.
 */

#include <stdarg.h>

#include <linux/compiler.h>
#include <linux/ctype.h>
#include <linux/string.h>

static int skip_atoi(const char **s)
{
	int i = 0;

	while (isdigit(**s))
		i = i * 10 + *((*s)++) - '0';
	return i;
}

/*
 * put_dec_full4 handles numbers in the range 0 <= r < 10000.
 * The multiplier 0xccd is round(2^15/10), and the approximation
 * r/10 == (r * 0xccd) >> 15 is exact for all r < 16389.
 */
static
void put_dec_full4(char *buf, unsigned int r)
{
	int i;

	for (i = 0; i < 3; i++) {
		unsigned int q = (r * 0xccd) >> 15;
		*buf++ = '0' + (r - q * 10);
		r = q;
	}
	*buf++ = '0' + r;
}

/* put_dec is copied from lib/vsprintf.c with small modifications */

/*
 * Call put_dec_full4 on x % 10000, return x / 10000.
 * The approximation x/10000 == (x * 0x346DC5D7) >> 43
 * holds for all x < 1,128,869,999.  The largest value this
 * helper will ever be asked to convert is 1,125,520,955.
 * (second call in the put_dec code, assuming n is all-ones).
 */
static
unsigned int put_dec_helper4(char *buf, unsigned int x)
{
	unsigned int q = (x * 0x346DC5D7ULL) >> 43;

	put_dec_full4(buf, x - q * 10000);
	return q;
}

/* Based on code by Douglas W. Jones found at
 * <http://www.cs.uiowa.edu/~jones/bcd/decimal.html#sixtyfour>
 * (with permission from the author).
 * Performs no 64-bit division and hence should be fast on 32-bit machines.
 */
static
int put_dec(char *buf, unsigned long long n)
{
	unsigned int d3, d2, d1, q, h;
	char *p = buf;

	d1  = ((unsigned int)n >> 16); /* implicit "& 0xffff" */
	h   = (n >> 32);
	d2  = (h      ) & 0xffff;
	d3  = (h >> 16); /* implicit "& 0xffff" */

	/* n = 2^48 d3 + 2^32 d2 + 2^16 d1 + d0
	     = 281_4749_7671_0656 d3 + 42_9496_7296 d2 + 6_5536 d1 + d0 */
	q = 656 * d3 + 7296 * d2 + 5536 * d1 + ((unsigned int)n & 0xffff);
	q = put_dec_helper4(p, q);
	p += 4;

	q += 7671 * d3 + 9496 * d2 + 6 * d1;
	q = put_dec_helper4(p, q);
	p += 4;

	q += 4749 * d3 + 42 * d2;
	q = put_dec_helper4(p, q);
	p += 4;

	q += 281 * d3;
	q = put_dec_helper4(p, q);
	p += 4;

	put_dec_full4(p, q);
	p += 4;

	/* strip off the extra 0's we printed */
	while (p > buf && p[-1] == '0')
		--p;

	return p - buf;
}

#define ZEROPAD	1		/* pad with zero */
#define SIGN	2		/* unsigned/signed long */
#define PLUS	4		/* show plus */
#define SPACE	8		/* space if plus */
#define LEFT	16		/* left justified */
#define SMALL	32		/* Must be 32 == 0x20 */
#define SPECIAL	64		/* 0x */

static char *number(char *str, long long num, int base, int size, int precision,
		    int type)
{
	/* we are called with base 8, 10 or 16, only, thus don't need "G..."  */
	static const char digits[16] = "0123456789ABCDEF"; /* "GHIJKLMNOPQRSTUVWXYZ"; */

	char tmp[66];
	char c, sign, locase;
	int i;

	/* locase = 0 or 0x20. ORing digits or letters with 'locase'
	 * produces same digits or (maybe lowercased) letters */
	locase = (type & SMALL);
	if (type & LEFT)
		type &= ~ZEROPAD;
	c = (type & ZEROPAD) ? '0' : ' ';
	sign = 0;
	if (type & SIGN) {
		if (num < 0) {
			sign = '-';
			num = -num;
			size--;
		} else if (type & PLUS) {
			sign = '+';
			size--;
		} else if (type & SPACE) {
			sign = ' ';
			size--;
		}
	}
	if (type & SPECIAL) {
		if (base == 16)
			size -= 2;
		else if (base == 8)
			size--;
	}
	i = 0;
	if (num == 0)
		tmp[i++] = '0';
	else {
		switch (base) {
		case 10:
			i += put_dec(&tmp[i], num);
			break;
		case 8:
			while (num != 0) {
				tmp[i++] = '0' + (num & 07);
				num = (unsigned long long)num >> 3;
			}
			break;
		case 16:
			while (num != 0) {
				tmp[i++] = digits[num & 0xf] | locase;
				num = (unsigned long long)num >> 4;
			}
			break;
		default:
			unreachable();
		}
	}

	if (i > precision)
		precision = i;
	size -= precision;
	if (!(type & (ZEROPAD + LEFT)))
		while (size-- > 0)
			*str++ = ' ';
	if (sign)
		*str++ = sign;
	if (type & SPECIAL) {
		if (base == 8) {
			*str++ = '0';
		} else if (base == 16) {
			*str++ = '0';
			*str++ = ('X' | locase);
		}
	}
	if (!(type & LEFT))
		while (size-- > 0)
			*str++ = c;
	while (i < precision--)
		*str++ = '0';
	while (i-- > 0)
		*str++ = tmp[i];
	while (size-- > 0)
		*str++ = ' ';
	return str;
}

static
int get_flags(const char **fmt)
{
	int flags = 0;

	do {
		switch (**fmt) {
		case '-':
			flags |= LEFT;
			break;
		case '+':
			flags |= PLUS;
			break;
		case ' ':
			flags |= SPACE;
			break;
		case '#':
			flags |= SPECIAL;
			break;
		case '0':
			flags |= ZEROPAD;
			break;
		default:
			return flags;
		}
		++(*fmt);
	} while (1);
}

static
int get_int(const char **fmt, va_list *ap)
{
	if (isdigit(**fmt))
		return skip_atoi(fmt);
	if (**fmt == '*') {
		++(*fmt);
		/* it's the next argument */
		return va_arg(*ap, int);
	}
	return 0;
}

int vsprintf(char *buf, const char *fmt, va_list ap)
{
	int len;
	unsigned long long num;
	int i, base;
	char *str;
	const char *s;

	int flags;		/* flags to number() */

	int field_width;	/* width of output field */
	int precision;		/* min. # of digits for integers; max
				   number of chars for from string */
	int qualifier;		/* 'h', 'hh', 'l' or 'll' for integer fields */

	va_list args;

	/*
	 * We want to pass our input va_list to helper functions by reference,
	 * but there's an annoying edge case. If va_list was originally passed
	 * to us by value, we could just pass &ap down to the helpers. This is
	 * the case on, for example, X86_32.
	 * However, on X86_64 (and possibly others), va_list is actually a
	 * size-1 array containing a structure. Our function parameter ap has
	 * decayed from T[1] to T*, and &ap has type T** rather than T(*)[1],
	 * which is what will be expected by a function taking a va_list *
	 * parameter.
	 * One standard way to solve this mess is by creating a copy in a local
	 * variable of type va_list and then passing a pointer to that local
	 * copy instead, which is what we do here.
	 */
	va_copy(args, ap);

	for (str = buf; *fmt; ++fmt) {
		if (*fmt != '%' || *++fmt == '%') {
			*str++ = *fmt;
			continue;
		}

		/* process flags */
		flags = get_flags(&fmt);

		/* get field width */
		field_width = get_int(&fmt, &args);
		if (field_width < 0) {
			field_width = -field_width;
			flags |= LEFT;
		}

		/* get the precision */
		precision = -1;
		if (*fmt == '.') {
			++fmt;
			precision = get_int(&fmt, &args);
			if (precision >= 0)
				flags &= ~ZEROPAD;
		}

		/* get the conversion qualifier */
		qualifier = -1;
		if (*fmt == 'h' || *fmt == 'l') {
			qualifier = *fmt;
			++fmt;
			if (qualifier == *fmt) {
				qualifier -= 'a'-'A';
				++fmt;
			}
		}

		switch (*fmt) {
		case 'c':
			if (!(flags & LEFT))
				while (--field_width > 0)
					*str++ = ' ';
			*str++ = (unsigned char)va_arg(args, int);
			while (--field_width > 0)
				*str++ = ' ';
			continue;

		case 's':
			s = va_arg(args, char *);
			len = strnlen(s, precision);

			if (!(flags & LEFT))
				while (len < field_width--)
					*str++ = ' ';
			for (i = 0; i < len; ++i)
				*str++ = *s++;
			while (len < field_width--)
				*str++ = ' ';
			continue;

			/* integer number formats - set up the flags and "break" */
		case 'o':
			base = 8;
			break;

		case 'p':
			if (precision < 0)
				precision = 2 * sizeof(void *);
			fallthrough;
		case 'x':
			flags |= SMALL;
			fallthrough;
		case 'X':
			base = 16;
			break;

		case 'd':
		case 'i':
			flags |= SIGN;
			fallthrough;
		case 'u':
			base = 10;
			break;

		default:
			*str++ = '%';
			if (*fmt)
				*str++ = *fmt;
			else
				--fmt;
			continue;
		}
		if (*fmt == 'p') {
			num = (unsigned long)va_arg(args, void *);
		} else if (flags & SIGN) {
			switch (qualifier) {
			case 'L':
				num = va_arg(args, long long);
				break;
			case 'l':
				num = va_arg(args, long);
				break;
			case 'h':
				num = (short)va_arg(args, int);
				break;
			case 'H':
				num = (signed char)va_arg(args, int);
				break;
			default:
				num = va_arg(args, int);
			}
		} else {
			switch (qualifier) {
			case 'L':
				num = va_arg(args, unsigned long long);
				break;
			case 'l':
				num = va_arg(args, unsigned long);
				break;
			case 'h':
				num = (unsigned short)va_arg(args, int);
				break;
			case 'H':
				num = (unsigned char)va_arg(args, int);
				break;
			default:
				num = va_arg(args, unsigned int);
			}
		}
		str = number(str, num, base, field_width, precision, flags);
	}
	*str = '\0';

	va_end(args);

	return str - buf;
}

int sprintf(char *buf, const char *fmt, ...)
{
	va_list args;
	int i;

	va_start(args, fmt);
	i = vsprintf(buf, fmt, args);
	va_end(args);
	return i;
}

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
#include <linux/kernel.h>
#include <linux/limits.h>
#include <linux/string.h>
#include <linux/types.h>

static
int skip_atoi(const char **s)
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
void put_dec_full4(char *end, unsigned int r)
{
	int i;

	for (i = 0; i < 3; i++) {
		unsigned int q = (r * 0xccd) >> 15;
		*--end = '0' + (r - q * 10);
		r = q;
	}
	*--end = '0' + r;
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
unsigned int put_dec_helper4(char *end, unsigned int x)
{
	unsigned int q = (x * 0x346DC5D7ULL) >> 43;

	put_dec_full4(end, x - q * 10000);
	return q;
}

/* Based on code by Douglas W. Jones found at
 * <http://www.cs.uiowa.edu/~jones/bcd/decimal.html#sixtyfour>
 * (with permission from the author).
 * Performs no 64-bit division and hence should be fast on 32-bit machines.
 */
static
char *put_dec(char *end, unsigned long long n)
{
	unsigned int d3, d2, d1, q, h;
	char *p = end;

	d1  = ((unsigned int)n >> 16); /* implicit "& 0xffff" */
	h   = (n >> 32);
	d2  = (h      ) & 0xffff;
	d3  = (h >> 16); /* implicit "& 0xffff" */

	/* n = 2^48 d3 + 2^32 d2 + 2^16 d1 + d0
	     = 281_4749_7671_0656 d3 + 42_9496_7296 d2 + 6_5536 d1 + d0 */
	q = 656 * d3 + 7296 * d2 + 5536 * d1 + ((unsigned int)n & 0xffff);
	q = put_dec_helper4(p, q);
	p -= 4;

	q += 7671 * d3 + 9496 * d2 + 6 * d1;
	q = put_dec_helper4(p, q);
	p -= 4;

	q += 4749 * d3 + 42 * d2;
	q = put_dec_helper4(p, q);
	p -= 4;

	q += 281 * d3;
	q = put_dec_helper4(p, q);
	p -= 4;

	put_dec_full4(p, q);
	p -= 4;

	/* strip off the extra 0's we printed */
	while (p < end && *p == '0')
		++p;

	return p;
}

static
char *number(char *end, unsigned long long num, int base, char locase)
{
	/*
	 * locase = 0 or 0x20. ORing digits or letters with 'locase'
	 * produces same digits or (maybe lowercased) letters
	 */

	/* we are called with base 8, 10 or 16, only, thus don't need "G..."  */
	static const char digits[16] = "0123456789ABCDEF"; /* "GHIJKLMNOPQRSTUVWXYZ"; */

	switch (base) {
	case 10:
		if (num != 0)
			end = put_dec(end, num);
		break;
	case 8:
		for (; num != 0; num >>= 3)
			*--end = '0' + (num & 07);
		break;
	case 16:
		for (; num != 0; num >>= 4)
			*--end = digits[num & 0xf] | locase;
		break;
	default:
		unreachable();
	};

	return end;
}

#define ZEROPAD	1		/* pad with zero */
#define SIGN	2		/* unsigned/signed long */
#define PLUS	4		/* show plus */
#define SPACE	8		/* space if plus */
#define LEFT	16		/* left justified */
#define SMALL	32		/* Must be 32 == 0x20 */
#define SPECIAL	64		/* 0x */
#define WIDE	128		/* UTF-16 string */

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

static
unsigned long long get_number(int sign, int qualifier, va_list *ap)
{
	if (sign) {
		switch (qualifier) {
		case 'L':
			return va_arg(*ap, long long);
		case 'l':
			return va_arg(*ap, long);
		case 'h':
			return (short)va_arg(*ap, int);
		case 'H':
			return (signed char)va_arg(*ap, int);
		default:
			return va_arg(*ap, int);
		};
	} else {
		switch (qualifier) {
		case 'L':
			return va_arg(*ap, unsigned long long);
		case 'l':
			return va_arg(*ap, unsigned long);
		case 'h':
			return (unsigned short)va_arg(*ap, int);
		case 'H':
			return (unsigned char)va_arg(*ap, int);
		default:
			return va_arg(*ap, unsigned int);
		}
	}
}

static
char get_sign(long long *num, int flags)
{
	if (!(flags & SIGN))
		return 0;
	if (*num < 0) {
		*num = -(*num);
		return '-';
	}
	if (flags & PLUS)
		return '+';
	if (flags & SPACE)
		return ' ';
	return 0;
}

static
size_t utf16s_utf8nlen(const u16 *s16, size_t maxlen)
{
	size_t len, clen;

	for (len = 0; len < maxlen && *s16; len += clen) {
		u16 c0 = *s16++;

		/* First, get the length for a BMP character */
		clen = 1 + (c0 >= 0x80) + (c0 >= 0x800);
		if (len + clen > maxlen)
			break;
		/*
		 * If this is a high surrogate, and we're already at maxlen, we
		 * can't include the character if it's a valid surrogate pair.
		 * Avoid accessing one extra word just to check if it's valid
		 * or not.
		 */
		if ((c0 & 0xfc00) == 0xd800) {
			if (len + clen == maxlen)
				break;
			if ((*s16 & 0xfc00) == 0xdc00) {
				++s16;
				++clen;
			}
		}
	}

	return len;
}

static
u32 utf16_to_utf32(const u16 **s16)
{
	u16 c0, c1;

	c0 = *(*s16)++;
	/* not a surrogate */
	if ((c0 & 0xf800) != 0xd800)
		return c0;
	/* invalid: low surrogate instead of high */
	if (c0 & 0x0400)
		return 0xfffd;
	c1 = **s16;
	/* invalid: missing low surrogate */
	if ((c1 & 0xfc00) != 0xdc00)
		return 0xfffd;
	/* valid surrogate pair */
	++(*s16);
	return (0x10000 - (0xd800 << 10) - 0xdc00) + (c0 << 10) + c1;
}

#define PUTC(c) \
do {				\
	if (pos < size)		\
		buf[pos] = (c);	\
	++pos;			\
} while (0);

int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap)
{
	/* The maximum space required is to print a 64-bit number in octal */
	char tmp[(sizeof(unsigned long long) * 8 + 2) / 3];
	char *tmp_end = &tmp[ARRAY_SIZE(tmp)];
	long long num;
	int base;
	const char *s;
	size_t len, pos;
	char sign;

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

	for (pos = 0; *fmt; ++fmt) {
		if (*fmt != '%' || *++fmt == '%') {
			PUTC(*fmt);
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

		if (flags & LEFT)
			flags &= ~ZEROPAD;

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

		sign = 0;

		switch (*fmt) {
		case 'c':
			flags &= LEFT;
			s = tmp;
			if (qualifier == 'l') {
				((u16 *)tmp)[0] = (u16)va_arg(args, unsigned int);
				((u16 *)tmp)[1] = L'\0';
				precision = INT_MAX;
				goto wstring;
			} else {
				tmp[0] = (unsigned char)va_arg(args, int);
				precision = len = 1;
			}
			goto output;

		case 's':
			flags &= LEFT;
			if (precision < 0)
				precision = INT_MAX;
			s = va_arg(args, void *);
			if (!s)
				s = precision < 6 ? "" : "(null)";
			else if (qualifier == 'l') {
		wstring:
				flags |= WIDE;
				precision = len = utf16s_utf8nlen((const u16 *)s, precision);
				goto output;
			}
			precision = len = strnlen(s, precision);
			goto output;

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
			flags &= ~SPECIAL;
			base = 10;
			break;

		default:
			/*
			 * Bail out if the conversion specifier is invalid.
			 * There's probably a typo in the format string and the
			 * remaining specifiers are unlikely to match up with
			 * the arguments.
			 */
			goto fail;
		}
		if (*fmt == 'p') {
			num = (unsigned long)va_arg(args, void *);
		} else {
			num = get_number(flags & SIGN, qualifier, &args);
		}

		sign = get_sign(&num, flags);
		if (sign)
			--field_width;

		s = number(tmp_end, num, base, flags & SMALL);
		len = tmp_end - s;
		/* default precision is 1 */
		if (precision < 0)
			precision = 1;
		/* precision is minimum number of digits to print */
		if (precision < len)
			precision = len;
		if (flags & SPECIAL) {
			/*
			 * For octal, a leading 0 is printed only if necessary,
			 * i.e. if it's not already there because of the
			 * precision.
			 */
			if (base == 8 && precision == len)
				++precision;
			/*
			 * For hexadecimal, the leading 0x is skipped if the
			 * output is empty, i.e. both the number and the
			 * precision are 0.
			 */
			if (base == 16 && precision > 0)
				field_width -= 2;
			else
				flags &= ~SPECIAL;
		}
		/*
		 * For zero padding, increase the precision to fill the field
		 * width.
		 */
		if ((flags & ZEROPAD) && field_width > precision)
			precision = field_width;

output:
		/* Calculate the padding necessary */
		field_width -= precision;
		/* Leading padding with ' ' */
		if (!(flags & LEFT))
			while (field_width-- > 0)
				PUTC(' ');
		/* sign */
		if (sign)
			PUTC(sign);
		/* 0x/0X for hexadecimal */
		if (flags & SPECIAL) {
			PUTC('0');
			PUTC( 'X' | (flags & SMALL));
		}
		/* Zero padding and excess precision */
		while (precision-- > len)
			PUTC('0');
		/* Actual output */
		if (flags & WIDE) {
			const u16 *ws = (const u16 *)s;

			while (len-- > 0) {
				u32 c32 = utf16_to_utf32(&ws);
				u8 *s8;
				size_t clen;

				if (c32 < 0x80) {
					PUTC(c32);
					continue;
				}

				/* Number of trailing octets */
				clen = 1 + (c32 >= 0x800) + (c32 >= 0x10000);

				len -= clen;
				s8 = (u8 *)&buf[pos];

				/* Avoid writing partial character */
				PUTC('\0');
				pos += clen;
				if (pos >= size)
					continue;

				/* Set high bits of leading octet */
				*s8 = (0xf00 >> 1) >> clen;
				/* Write trailing octets in reverse order */
				for (s8 += clen; clen; --clen, c32 >>= 6)
					*s8-- = 0x80 | (c32 & 0x3f);
				/* Set low bits of leading octet */
				*s8 |= c32;
			}
		} else {
			while (len-- > 0)
				PUTC(*s++);
		}
		/* Trailing padding with ' ' */
		while (field_width-- > 0)
			PUTC(' ');
	}
fail:
	va_end(args);

	if (size)
		buf[min(pos, size-1)] = '\0';

	return pos;
}

int snprintf(char *buf, size_t size, const char *fmt, ...)
{
	va_list args;
	int i;

	va_start(args, fmt);
	i = vsnprintf(buf, size, fmt, args);
	va_end(args);
	return i;
}

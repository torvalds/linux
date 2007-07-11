/* -*- linux-c -*- ------------------------------------------------------- *
 *
 *   Copyright (C) 1991, 1992 Linus Torvalds
 *   Copyright 2007 rPath, Inc. - All Rights Reserved
 *
 *   This file is part of the Linux kernel, and is made available under
 *   the terms of the GNU General Public License version 2.
 *
 * ----------------------------------------------------------------------- */

/*
 * arch/i386/boot/printf.c
 *
 * Oh, it's a waste of space, but oh-so-yummy for debugging.  This
 * version of printf() does not include 64-bit support.  "Live with
 * it."
 *
 */

#include "boot.h"

static int skip_atoi(const char **s)
{
	int i = 0;

	while (isdigit(**s))
		i = i * 10 + *((*s)++) - '0';
	return i;
}

#define ZEROPAD	1		/* pad with zero */
#define SIGN	2		/* unsigned/signed long */
#define PLUS	4		/* show plus */
#define SPACE	8		/* space if plus */
#define LEFT	16		/* left justified */
#define SPECIAL	32		/* 0x */
#define LARGE	64		/* use 'ABCDEF' instead of 'abcdef' */

#define do_div(n,base) ({ \
int __res; \
__res = ((unsigned long) n) % (unsigned) base; \
n = ((unsigned long) n) / (unsigned) base; \
__res; })

static char *number(char *str, long num, int base, int size, int precision,
		    int type)
{
	char c, sign, tmp[66];
	const char *digits = "0123456789abcdefghijklmnopqrstuvwxyz";
	int i;

	if (type & LARGE)
		digits = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
	if (type & LEFT)
		type &= ~ZEROPAD;
	if (base < 2 || base > 36)
		return 0;
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
	else
		while (num != 0)
			tmp[i++] = digits[do_div(num, base)];
	if (i > precision)
		precision = i;
	size -= precision;
	if (!(type & (ZEROPAD + LEFT)))
		while (size-- > 0)
			*str++ = ' ';
	if (sign)
		*str++ = sign;
	if (type & SPECIAL) {
		if (base == 8)
			*str++ = '0';
		else if (base == 16) {
			*str++ = '0';
			*str++ = digits[33];
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

int vsprintf(char *buf, const char *fmt, va_list args)
{
	int len;
	unsigned long num;
	int i, base;
	char *str;
	const char *s;

	int flags;		/* flags to number() */

	int field_width;	/* width of output field */
	int precision;		/* min. # of digits for integers; max
				   number of chars for from string */
	int qualifier;		/* 'h', 'l', or 'L' for integer fields */

	for (str = buf; *fmt; ++fmt) {
		if (*fmt != '%') {
			*str++ = *fmt;
			continue;
		}

		/* process flags */
		flags = 0;
	      repeat:
		++fmt;		/* this also skips first '%' */
		switch (*fmt) {
		case '-':
			flags |= LEFT;
			goto repeat;
		case '+':
			flags |= PLUS;
			goto repeat;
		case ' ':
			flags |= SPACE;
			goto repeat;
		case '#':
			flags |= SPECIAL;
			goto repeat;
		case '0':
			flags |= ZEROPAD;
			goto repeat;
		}

		/* get field width */
		field_width = -1;
		if (isdigit(*fmt))
			field_width = skip_atoi(&fmt);
		else if (*fmt == '*') {
			++fmt;
			/* it's the next argument */
			field_width = va_arg(args, int);
			if (field_width < 0) {
				field_width = -field_width;
				flags |= LEFT;
			}
		}

		/* get the precision */
		precision = -1;
		if (*fmt == '.') {
			++fmt;
			if (isdigit(*fmt))
				precision = skip_atoi(&fmt);
			else if (*fmt == '*') {
				++fmt;
				/* it's the next argument */
				precision = va_arg(args, int);
			}
			if (precision < 0)
				precision = 0;
		}

		/* get the conversion qualifier */
		qualifier = -1;
		if (*fmt == 'h' || *fmt == 'l' || *fmt == 'L') {
			qualifier = *fmt;
			++fmt;
		}

		/* default base */
		base = 10;

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

		case 'p':
			if (field_width == -1) {
				field_width = 2 * sizeof(void *);
				flags |= ZEROPAD;
			}
			str = number(str,
				     (unsigned long)va_arg(args, void *), 16,
				     field_width, precision, flags);
			continue;

		case 'n':
			if (qualifier == 'l') {
				long *ip = va_arg(args, long *);
				*ip = (str - buf);
			} else {
				int *ip = va_arg(args, int *);
				*ip = (str - buf);
			}
			continue;

		case '%':
			*str++ = '%';
			continue;

			/* integer number formats - set up the flags and "break" */
		case 'o':
			base = 8;
			break;

		case 'X':
			flags |= LARGE;
		case 'x':
			base = 16;
			break;

		case 'd':
		case 'i':
			flags |= SIGN;
		case 'u':
			break;

		default:
			*str++ = '%';
			if (*fmt)
				*str++ = *fmt;
			else
				--fmt;
			continue;
		}
		if (qualifier == 'l')
			num = va_arg(args, unsigned long);
		else if (qualifier == 'h') {
			num = (unsigned short)va_arg(args, int);
			if (flags & SIGN)
				num = (short)num;
		} else if (flags & SIGN)
			num = va_arg(args, int);
		else
			num = va_arg(args, unsigned int);
		str = number(str, num, base, field_width, precision, flags);
	}
	*str = '\0';
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

int printf(const char *fmt, ...)
{
	char printf_buf[1024];
	va_list args;
	int printed;

	va_start(args, fmt);
	printed = vsprintf(printf_buf, fmt, args);
	va_end(args);

	puts(printf_buf);

	return printed;
}

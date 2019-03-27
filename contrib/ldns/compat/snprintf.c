/* snprintf - compatibility implementation of snprintf, vsnprintf
 *
 * Copyright (c) 2013, NLnet Labs. All rights reserved.
 *
 * This software is open source.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 * 
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 * 
 * Neither the name of the NLNET LABS nor the names of its contributors may
 * be used to endorse or promote products derived from this software without
 * specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <ldns/config.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include <errno.h>
#ifdef HAVE_STDINT_H
#include <stdint.h>
#endif

/* for test */
/* #define SNPRINTF_TEST 1 */
#ifdef SNPRINTF_TEST
#define snprintf my_snprintf
#define vsnprintf my_vsnprintf
#endif /* SNPRINTF_TEST */

int snprintf(char* str, size_t size, const char* format, ...);
int vsnprintf(char* str, size_t size, const char* format, va_list arg);

/**
 * Very portable snprintf implementation, limited in functionality,
 * esp. for %[capital] %[nonportable] and so on.  Reduced float functionality,
 * mostly in formatting and range (e+-16), for %f and %g.
 *
 * %s, %d, %u, %i, %x, %c, %n and %% are fully supported.
 *   This includes width, precision, flags 0- +, and *(arg for wid,prec).
 * %f, %g, %m, %p have reduced support, support for wid,prec,flags,*, but
 *   less floating point range, no %e formatting for %g.
 */
int snprintf(char* str, size_t size, const char* format, ...)
{
	int r;
	va_list args;
	va_start(args, format);
	r = vsnprintf(str, size, format, args);
	va_end(args);
	return r;
}

/** add padding to string */
static void
print_pad(char** at, size_t* left, int* ret, char p, int num)
{
	while(num--) {
		if(*left > 1) {
			*(*at)++ = p;
			(*left)--;
		}
		(*ret)++;
	}
}

/** get negative symbol, 0 if none */
static char
get_negsign(int negative, int plus, int space)
{
	if(negative)
		return '-';
	if(plus)
		return '+';
	if(space)
		return ' ';
	return 0;
}

#define PRINT_DEC_BUFSZ 32 /* 20 is enough for 64 bit decimals */
/** print decimal into buffer, returns length */
static int
print_dec(char* buf, int max, unsigned int value)
{
	int i = 0;
	if(value == 0) {
		if(max > 0) {
			buf[0] = '0';
			i = 1;
		}
	} else while(value && i < max) {
		buf[i++] = '0' + value % 10;
		value /= 10;
	}
	return i;
}

/** print long decimal into buffer, returns length */
static int
print_dec_l(char* buf, int max, unsigned long value)
{
	int i = 0;
	if(value == 0) {
		if(max > 0) {
			buf[0] = '0';
			i = 1;
		}
	} else while(value && i < max) {
		buf[i++] = '0' + value % 10;
		value /= 10;
	}
	return i;
}

/** print long decimal into buffer, returns length */
static int
print_dec_ll(char* buf, int max, unsigned long long value)
{
	int i = 0;
	if(value == 0) {
		if(max > 0) {
			buf[0] = '0';
			i = 1;
		}
	} else while(value && i < max) {
		buf[i++] = '0' + value % 10;
		value /= 10;
	}
	return i;
}

/** print hex into buffer, returns length */
static int
print_hex(char* buf, int max, unsigned int value)
{
	const char* h = "0123456789abcdef";
	int i = 0;
	if(value == 0) {
		if(max > 0) {
			buf[0] = '0';
			i = 1;
		}
	} else while(value && i < max) {
		buf[i++] = h[value & 0x0f];
		value >>= 4;
	}
	return i;
}

/** print long hex into buffer, returns length */
static int
print_hex_l(char* buf, int max, unsigned long value)
{
	const char* h = "0123456789abcdef";
	int i = 0;
	if(value == 0) {
		if(max > 0) {
			buf[0] = '0';
			i = 1;
		}
	} else while(value && i < max) {
		buf[i++] = h[value & 0x0f];
		value >>= 4;
	}
	return i;
}

/** print long long hex into buffer, returns length */
static int
print_hex_ll(char* buf, int max, unsigned long long value)
{
	const char* h = "0123456789abcdef";
	int i = 0;
	if(value == 0) {
		if(max > 0) {
			buf[0] = '0';
			i = 1;
		}
	} else while(value && i < max) {
		buf[i++] = h[value & 0x0f];
		value >>= 4;
	}
	return i;
}

/** copy string into result, reversed */
static void
spool_str_rev(char** at, size_t* left, int* ret, const char* buf, int len)
{
	int i = len;
	while(i) {
		if(*left > 1) {
			*(*at)++ = buf[--i];
			(*left)--;
		} else --i;
		(*ret)++;
	}
}

/** copy string into result */
static void
spool_str(char** at, size_t* left, int* ret, const char* buf, int len)
{
	int i;
	for(i=0; i<len; i++) {
		if(*left > 1) {
			*(*at)++ = buf[i];
			(*left)--;
		}
		(*ret)++;
	}
}

/** print number formatted */
static void
print_num(char** at, size_t* left, int* ret, int minw, int precision,
	int prgiven, int zeropad, int minus, int plus, int space,
	int zero, int negative, char* buf, int len)
{
	int w = len; /* excludes minus sign */
	char s = get_negsign(negative, plus, space);
	if(minus) {
		/* left adjust the number into the field, space padding */
		/* calc numw = [sign][zeroes][number] */
		int numw = w;
		if(precision == 0 && zero) numw = 0;
		if(numw < precision) numw = precision;
		if(s) numw++;

		/* sign */
		if(s) print_pad(at, left, ret, s, 1);

		/* number */
		if(precision == 0 && zero) {
			/* "" for the number */
		} else {
			if(w < precision)
				print_pad(at, left, ret, '0', precision - w);
			spool_str_rev(at, left, ret, buf, len);
		}
		/* spaces */
		if(numw < minw)
			print_pad(at, left, ret, ' ', minw - numw);
	} else {
		/* pad on the left of the number */
		/* calculate numw has width of [sign][zeroes][number] */
		int numw = w;
		if(precision == 0 && zero) numw = 0;
		if(numw < precision) numw = precision;
		if(!prgiven && zeropad && numw < minw) numw = minw;
		else if(s) numw++;

		/* pad with spaces */
		if(numw < minw)
			print_pad(at, left, ret, ' ', minw - numw);
		/* print sign (and one less zeropad if so) */
		if(s) {
			print_pad(at, left, ret, s, 1);
			numw--;
		}
		/* pad with zeroes */
		if(w < numw)
			print_pad(at, left, ret, '0', numw - w);
		if(precision == 0 && zero)
			return;
		/* print the characters for the value */
		spool_str_rev(at, left, ret, buf, len);
	}
}

/** print %d and %i */
static void
print_num_d(char** at, size_t* left, int* ret, int value,
	int minw, int precision, int prgiven, int zeropad, int minus,
	int plus, int space)
{
	char buf[PRINT_DEC_BUFSZ];
	int negative = (value < 0);
	int zero = (value == 0);
	int len = print_dec(buf, (int)sizeof(buf),
		(unsigned int)(negative?-value:value));
	print_num(at, left, ret, minw, precision, prgiven, zeropad, minus,
		plus, space, zero, negative, buf, len);
}

/** print %ld and %li */
static void
print_num_ld(char** at, size_t* left, int* ret, long value,
	int minw, int precision, int prgiven, int zeropad, int minus,
	int plus, int space)
{
	char buf[PRINT_DEC_BUFSZ];
	int negative = (value < 0);
	int zero = (value == 0);
	int len = print_dec_l(buf, (int)sizeof(buf),
		(unsigned long)(negative?-value:value));
	print_num(at, left, ret, minw, precision, prgiven, zeropad, minus,
		plus, space, zero, negative, buf, len);
}

/** print %lld and %lli */
static void
print_num_lld(char** at, size_t* left, int* ret, long long value,
	int minw, int precision, int prgiven, int zeropad, int minus,
	int plus, int space)
{
	char buf[PRINT_DEC_BUFSZ];
	int negative = (value < 0);
	int zero = (value == 0);
	int len = print_dec_ll(buf, (int)sizeof(buf),
		(unsigned long long)(negative?-value:value));
	print_num(at, left, ret, minw, precision, prgiven, zeropad, minus,
		plus, space, zero, negative, buf, len);
}

/** print %u */
static void
print_num_u(char** at, size_t* left, int* ret, unsigned int value,
	int minw, int precision, int prgiven, int zeropad, int minus,
	int plus, int space)
{
	char buf[PRINT_DEC_BUFSZ];
	int negative = 0;
	int zero = (value == 0);
	int len = print_dec(buf, (int)sizeof(buf), value);
	print_num(at, left, ret, minw, precision, prgiven, zeropad, minus,
		plus, space, zero, negative, buf, len);
}

/** print %lu */
static void
print_num_lu(char** at, size_t* left, int* ret, unsigned long value,
	int minw, int precision, int prgiven, int zeropad, int minus,
	int plus, int space)
{
	char buf[PRINT_DEC_BUFSZ];
	int negative = 0;
	int zero = (value == 0);
	int len = print_dec_l(buf, (int)sizeof(buf), value);
	print_num(at, left, ret, minw, precision, prgiven, zeropad, minus,
		plus, space, zero, negative, buf, len);
}

/** print %llu */
static void
print_num_llu(char** at, size_t* left, int* ret, unsigned long long value,
	int minw, int precision, int prgiven, int zeropad, int minus,
	int plus, int space)
{
	char buf[PRINT_DEC_BUFSZ];
	int negative = 0;
	int zero = (value == 0);
	int len = print_dec_ll(buf, (int)sizeof(buf), value);
	print_num(at, left, ret, minw, precision, prgiven, zeropad, minus,
		plus, space, zero, negative, buf, len);
}

/** print %x */
static void
print_num_x(char** at, size_t* left, int* ret, unsigned int value,
	int minw, int precision, int prgiven, int zeropad, int minus,
	int plus, int space)
{
	char buf[PRINT_DEC_BUFSZ];
	int negative = 0;
	int zero = (value == 0);
	int len = print_hex(buf, (int)sizeof(buf), value);
	print_num(at, left, ret, minw, precision, prgiven, zeropad, minus,
		plus, space, zero, negative, buf, len);
}

/** print %lx */
static void
print_num_lx(char** at, size_t* left, int* ret, unsigned long value,
	int minw, int precision, int prgiven, int zeropad, int minus,
	int plus, int space)
{
	char buf[PRINT_DEC_BUFSZ];
	int negative = 0;
	int zero = (value == 0);
	int len = print_hex_l(buf, (int)sizeof(buf), value);
	print_num(at, left, ret, minw, precision, prgiven, zeropad, minus,
		plus, space, zero, negative, buf, len);
}

/** print %llx */
static void
print_num_llx(char** at, size_t* left, int* ret, unsigned long long value,
	int minw, int precision, int prgiven, int zeropad, int minus,
	int plus, int space)
{
	char buf[PRINT_DEC_BUFSZ];
	int negative = 0;
	int zero = (value == 0);
	int len = print_hex_ll(buf, (int)sizeof(buf), value);
	print_num(at, left, ret, minw, precision, prgiven, zeropad, minus,
		plus, space, zero, negative, buf, len);
}

/** print %llp */
static void
print_num_llp(char** at, size_t* left, int* ret, void* value,
	int minw, int precision, int prgiven, int zeropad, int minus,
	int plus, int space)
{
	char buf[PRINT_DEC_BUFSZ];
	int negative = 0;
	int zero = (value == 0);
#if defined(UINTPTR_MAX) && defined(UINT32_MAX) && (UINTPTR_MAX == UINT32_MAX)
	/* avoid warning about upcast on 32bit systems */
	unsigned long long llvalue = (unsigned long)value;
#else
	unsigned long long llvalue = (unsigned long long)value;
#endif
	int len = print_hex_ll(buf, (int)sizeof(buf), llvalue);
	if(zero) {
		buf[0]=')';
		buf[1]='l';
		buf[2]='i';
		buf[3]='n';
		buf[4]='(';
		len = 5;
	} else {
		/* put '0x' in front of the (reversed) buffer result */
		if(len < PRINT_DEC_BUFSZ)
			buf[len++] = 'x';
		if(len < PRINT_DEC_BUFSZ)
			buf[len++] = '0';
	}
	print_num(at, left, ret, minw, precision, prgiven, zeropad, minus,
		plus, space, zero, negative, buf, len);
}

#define PRINT_FLOAT_BUFSZ 64 /* xx.yy with 20.20 about the max */
/** spool remainder after the decimal point to buffer, in reverse */
static int
print_remainder(char* buf, int max, double r, int prec)
{
	unsigned long long cap = 1;
	unsigned long long value;
	int len, i;
	if(prec > 19) prec = 19; /* max we can do */
	if(max < prec) return 0;
	for(i=0; i<prec; i++) {
		cap *= 10;
	}
	r *= (double)cap;
	value = (unsigned long long)r;
	/* see if we need to round up */
	if(((unsigned long long)((r - (double)value)*10.0)) >= 5) {
		value++;
		/* that might carry to numbers before the comma, if so,
		 * just ignore that rounding. failure because 64bitprintout */
		if(value >= cap)
			value = cap-1;
	}
	len = print_dec_ll(buf, max, value);
	while(len < prec) { /* pad with zeroes, e.g. if 0.0012 */
		buf[len++] = '0';
	}
	if(len < max)
		buf[len++] = '.';
	return len;
}

/** spool floating point to buffer */
static int
print_float(char* buf, int max, double value, int prec)
{
	/* as xxx.xxx  if prec==0, no '.', with prec decimals after . */
	/* no conversion for NAN and INF, because we do not want to require
	   linking with -lm. */
	/* Thus, the conversions use 64bit integers to convert the numbers,
	 * which makes 19 digits before and after the decimal point the max */
	unsigned long long whole = (unsigned long long)value;
	double remain = value - (double)whole;
	int len = 0;
	if(prec != 0)
		len = print_remainder(buf, max, remain, prec);
	len += print_dec_ll(buf+len, max-len, whole);
	return len;
}

/** print %f */
static void
print_num_f(char** at, size_t* left, int* ret, double value,
	int minw, int precision, int prgiven, int zeropad, int minus,
	int plus, int space)
{
	char buf[PRINT_FLOAT_BUFSZ];
	int negative = (value < 0);
	int zero = 0;
	int len;
	if(!prgiven) precision = 6;
	len = print_float(buf, (int)sizeof(buf), negative?-value:value,
		precision);
	print_num(at, left, ret, minw, 1, 0, zeropad, minus,
		plus, space, zero, negative, buf, len);
}

/* rudimentary %g support */
static int
print_float_g(char* buf, int max, double value, int prec)
{
	unsigned long long whole = (unsigned long long)value;
	double remain = value - (double)whole;
	int before = 0;
	int len = 0;

	/* number of digits before the decimal point */
	while(whole > 0) {
		before++;
		whole /= 10;
	}
	whole = (unsigned long long)value;

	if(prec > before && remain != 0.0) {
		/* see if the last decimals are zero, if so, skip them */
		len = print_remainder(buf, max, remain, prec-before);
		while(len > 0 && buf[0]=='0') {
			memmove(buf, buf+1, --len);
		}
	}
	len += print_dec_ll(buf+len, max-len, whole);
	return len;
}


/** print %g */
static void
print_num_g(char** at, size_t* left, int* ret, double value,
	int minw, int precision, int prgiven, int zeropad, int minus,
	int plus, int space)
{
	char buf[PRINT_FLOAT_BUFSZ];
	int negative = (value < 0);
	int zero = 0;
	int len;
	if(!prgiven) precision = 6;
	if(precision == 0) precision = 1;
	len = print_float_g(buf, (int)sizeof(buf), negative?-value:value,
		precision);
	print_num(at, left, ret, minw, 1, 0, zeropad, minus,
		plus, space, zero, negative, buf, len);
}


/** strnlen (compat implementation) */
static int
my_strnlen(const char* s, int max)
{
	int i;
	for(i=0; i<max; i++)
		if(s[i]==0)
			return i;
	return max;
}

/** print %s */
static void
print_str(char** at, size_t* left, int* ret, char* s,
	int minw, int precision, int prgiven, int minus)
{
	int w;
	/* with prec: no more than x characters from this string, stop at 0 */
	if(prgiven)
		w = my_strnlen(s, precision);
	else	w = (int)strlen(s); /* up to the nul */
	if(w < minw && !minus)
		print_pad(at, left, ret, ' ', minw - w);
	spool_str(at, left, ret, s, w);
	if(w < minw && minus)
		print_pad(at, left, ret, ' ', minw - w);
}

/** print %c */
static void
print_char(char** at, size_t* left, int* ret, int c,
	int minw, int minus)
{
	if(1 < minw && !minus)
		print_pad(at, left, ret, ' ', minw - 1);
	print_pad(at, left, ret, c, 1);
	if(1 < minw && minus)
		print_pad(at, left, ret, ' ', minw - 1);
}


/** 
 * Print to string.
 * str: string buffer for result. result will be null terminated.
 * size: size of the buffer. null is put inside buffer.
 * format: printf format string.
 * arg: '...' arguments to print.
 * returns number of characters. a null is printed after this.
 * return number of bytes that would have been written
 *	   if the buffer had been large enough.
 * 
 * supported format specifiers:
 * 	%s, %u, %d, %x, %i, %f, %g, %c, %p, %n.
 * 	length: l, ll (for d, u, x).
 * 	precision: 6.6d (for d, u, x)
 * 		%f, %g precisions, 0.3f
 * 		%20s, '.*s'
 * 	and %%.
 */
int vsnprintf(char* str, size_t size, const char* format, va_list arg)
{
	char* at = str;
	size_t left = size;
	int ret = 0;
	const char* fmt = format;
	int conv, minw, precision, prgiven, zeropad, minus, plus, space, length;
	while(*fmt) {
		/* copy string before % */
		while(*fmt && *fmt!='%') {
			if(left > 1) {
				*at++ = *fmt++;
				left--;
			} else fmt++;
			ret++;
		}
		
		/* see if we are at end */
		if(!*fmt) break;

		/* fetch next argument % designation from format string */
		fmt++; /* skip the '%' */

		/********************************/
		/* get the argument designation */
		/********************************/
		/* we must do this vararg stuff inside this function for
		 * portability.  Hence, get_designation, and print_designation
		 * are not their own functions. */

		/* printout designation:
		 * conversion specifier: x, d, u, s, c, n, m, p
		 * flags: # not supported
		 *        0 zeropad (on the left)
		 *	  - left adjust (right by default)
		 *	  ' ' printspace for positive number (in - position).
		 *	  + alwayssign
		 * fieldwidth: [1-9][0-9]* minimum field width.
		 * 	if this is * then type int next argument specifies the minwidth.
		 * 	if this is negative, the - flag is set (with positive width).
		 * precision: period[digits]*, %.2x.
		 * 	if this is * then type int next argument specifies the precision.
		 *	just '.' or negative value means precision=0.
		 *		this is mindigits to print for d, i, u, x
		 *		this is aftercomma digits for f
		 *		this is max number significant digits for g
		 *		maxnumber characters to be printed for s
		 * length: 0-none (int), 1-l (long), 2-ll (long long)
		 * 	notsupported: hh (char), h (short), L (long double), q, j, z, t
		 * Does not support %m$ and *m$ argument designation as array indices.
		 * Does not support %#x
		 *
		 */
		minw = 0;
		precision = 1;
		prgiven = 0;
		zeropad = 0;
		minus = 0;
		plus = 0;
		space = 0;
		length = 0;

		/* get flags in any order */
		for(;;) {
			if(*fmt == '0')
				zeropad = 1;
			else if(*fmt == '-')
				minus = 1;
			else if(*fmt == '+')
				plus = 1;
			else if(*fmt == ' ')
				space = 1;
			else break;
			fmt++;
		}

		/* field width */
		if(*fmt == '*') {
			fmt++; /* skip char */
			minw = va_arg(arg, int);
			if(minw < 0) {
				minus = 1;
				minw = -minw;
			}
		} else while(*fmt >= '0' && *fmt <= '9') {
			minw = minw*10 + (*fmt++)-'0';
		}

		/* precision */
		if(*fmt == '.') {
			fmt++; /* skip period */
			prgiven = 1;
			precision = 0;
			if(*fmt == '*') {
				fmt++; /* skip char */
				precision = va_arg(arg, int);
				if(precision < 0)
					precision = 0;
			} else while(*fmt >= '0' && *fmt <= '9') {
				precision = precision*10 + (*fmt++)-'0';
			}
		}

		/* length */
		if(*fmt == 'l') {
			fmt++; /* skip char */
			length = 1;
			if(*fmt == 'l') {
				fmt++; /* skip char */
				length = 2;
			}
		}

		/* get the conversion */
		if(!*fmt) conv = 0;
		else	conv = *fmt++;

		/***********************************/
		/* print that argument designation */
		/***********************************/
		switch(conv) {
		case 'i':
		case 'd':
			if(length == 0)
			    print_num_d(&at, &left, &ret, va_arg(arg, int),
				minw, precision, prgiven, zeropad, minus, plus, space);
			else if(length == 1)
			    print_num_ld(&at, &left, &ret, va_arg(arg, long),
				minw, precision, prgiven, zeropad, minus, plus, space);
			else if(length == 2)
			    print_num_lld(&at, &left, &ret,
				va_arg(arg, long long),
				minw, precision, prgiven, zeropad, minus, plus, space);
			break;
		case 'u':
			if(length == 0)
			    print_num_u(&at, &left, &ret,
				va_arg(arg, unsigned int),
				minw, precision, prgiven, zeropad, minus, plus, space);
			else if(length == 1)
			    print_num_lu(&at, &left, &ret,
				va_arg(arg, unsigned long),
				minw, precision, prgiven, zeropad, minus, plus, space);
			else if(length == 2)
			    print_num_llu(&at, &left, &ret,
				va_arg(arg, unsigned long long),
				minw, precision, prgiven, zeropad, minus, plus, space);
			break;
		case 'x':
			if(length == 0)
			    print_num_x(&at, &left, &ret,
				va_arg(arg, unsigned int),
				minw, precision, prgiven, zeropad, minus, plus, space);
			else if(length == 1)
			    print_num_lx(&at, &left, &ret,
				va_arg(arg, unsigned long),
				minw, precision, prgiven, zeropad, minus, plus, space);
			else if(length == 2)
			    print_num_llx(&at, &left, &ret,
				va_arg(arg, unsigned long long),
				minw, precision, prgiven, zeropad, minus, plus, space);
			break;
		case 's':
			print_str(&at, &left, &ret, va_arg(arg, char*),
				minw, precision, prgiven, minus);
			break;
		case 'c':
			print_char(&at, &left, &ret, va_arg(arg, int),
				minw, minus);
			break;
		case 'n':
			*va_arg(arg, int*) = ret;
			break;
		case 'm':
			print_str(&at, &left, &ret, strerror(errno),
				minw, precision, prgiven, minus);
			break;
		case 'p':
			print_num_llp(&at, &left, &ret, va_arg(arg, void*),
				minw, precision, prgiven, zeropad, minus, plus, space);
			break;
		case '%':
			print_pad(&at, &left, &ret, '%', 1);
			break;
		case 'f':
			print_num_f(&at, &left, &ret, va_arg(arg, double),
				minw, precision, prgiven, zeropad, minus, plus, space);
			break;
		case 'g':
			print_num_g(&at, &left, &ret, va_arg(arg, double),
				minw, precision, prgiven, zeropad, minus, plus, space);
			break;
		/* unknown */
		default:
		case 0: break;
		}
	}

	/* zero terminate */
	if(left > 0)
		*at = 0;
	return ret;
}

#ifdef SNPRINTF_TEST

/** do tests */
#undef snprintf
#define DOTEST(bufsz, result, retval, ...) do { \
	char buf[bufsz]; \
	printf("now test %s\n", #__VA_ARGS__); \
	int r=my_snprintf(buf, sizeof(buf), __VA_ARGS__); \
	if(r != retval || strcmp(buf, result) != 0) { \
		printf("error test(%s) was \"%s\":%d\n", \
			""#bufsz", "#result", "#retval", "#__VA_ARGS__, \
			buf, r); \
		exit(1); \
		} \
	r=snprintf(buf, sizeof(buf), __VA_ARGS__); \
	if(r != retval || strcmp(buf, result) != 0) { \
		printf("error test(%s) differs with system, \"%s\":%d\n", \
			""#bufsz", "#result", "#retval", "#__VA_ARGS__, \
			buf, r); \
		exit(1); \
		} \
	printf("test(\"%s\":%d) passed\n", buf, r); \
	} while(0);

/** test program */
int main(void)
{
	int x = 0;

	/* bufsize, expectedstring, expectedretval, snprintf arguments */
	DOTEST(1024, "hello", 5, "hello");
	DOTEST(1024, "h", 1, "h");
	/* warning from gcc for format string, but it does work
	 * DOTEST(1024, "", 0, ""); */

	DOTEST(3, "he", 5, "hello");
	DOTEST(1, "", 7, "%d", 7823089);

	/* test positive numbers */
	DOTEST(1024, "0", 1, "%d", 0);
	DOTEST(1024, "1", 1, "%d", 1);
	DOTEST(1024, "9", 1, "%d", 9);
	DOTEST(1024, "15", 2, "%d", 15);
	DOTEST(1024, "ab15cd", 6, "ab%dcd", 15);
	DOTEST(1024, "167", 3, "%d", 167);
	DOTEST(1024, "7823089", 7, "%d", 7823089);
	DOTEST(1024, " 12", 3, "%3d", 12);
	DOTEST(1024, "012", 3, "%.3d", 12);
	DOTEST(1024, "012", 3, "%3.3d", 12);
	DOTEST(1024, "012", 3, "%03d", 12);
	DOTEST(1024, " 012", 4, "%4.3d", 12);
	DOTEST(1024, "", 0, "%.0d", 0);

	/* test negative numbers */
	DOTEST(1024, "-1", 2, "%d", -1);
	DOTEST(1024, "-12", 3, "%3d", -12);
	DOTEST(1024, " -2", 3, "%3d", -2);
	DOTEST(1024, "-012", 4, "%.3d", -12);
	DOTEST(1024, "-012", 4, "%3.3d", -12);
	DOTEST(1024, "-012", 4, "%4.3d", -12);
	DOTEST(1024, " -012", 5, "%5.3d", -12);
	DOTEST(1024, "-12", 3, "%03d", -12);
	DOTEST(1024, "-02", 3, "%03d", -2);
	DOTEST(1024, "-15", 3, "%d", -15);
	DOTEST(1024, "-7307", 5, "%d", -7307);
	DOTEST(1024, "-12  ", 5, "%-5d", -12);
	DOTEST(1024, "-00012", 6, "%-.5d", -12);

	/* test + and space flags */
	DOTEST(1024, "+12", 3, "%+d", 12);
	DOTEST(1024, " 12", 3, "% d", 12);

	/* test %u */
	DOTEST(1024, "12", 2, "%u", 12);
	DOTEST(1024, "0", 1, "%u", 0);
	DOTEST(1024, "4294967295", 10, "%u", 0xffffffff);

	/* test %x */
	DOTEST(1024, "0", 1, "%x", 0);
	DOTEST(1024, "c", 1, "%x", 12);
	DOTEST(1024, "12ab34cd", 8, "%x", 0x12ab34cd);

	/* test %llu, %lld */
	DOTEST(1024, "18446744073709551615", 20, "%llu",
		(long long)0xffffffffffffffff);
	DOTEST(1024, "-9223372036854775808", 20, "%lld",
		(long long)0x8000000000000000);
	DOTEST(1024, "9223372036854775808", 19, "%llu",
		(long long)0x8000000000000000);

	/* test %s */
	DOTEST(1024, "hello", 5, "%s", "hello");
	DOTEST(1024, "     hello", 10, "%10s", "hello");
	DOTEST(1024, "hello     ", 10, "%-10s", "hello");
	DOTEST(1024, "he", 2, "%.2s", "hello");
	DOTEST(1024, "  he", 4, "%4.2s", "hello");
	DOTEST(1024, "   h", 4, "%4.2s", "h");

	/* test %c */
	DOTEST(1024, "a", 1, "%c", 'a');
	/* warning from gcc for format string, but it does work
	   DOTEST(1024, "    a", 5, "%5c", 'a');
	   DOTEST(1024, "a", 1, "%.0c", 'a'); */

	/* test %n */
	DOTEST(1024, "hello", 5, "hello%n", &x);
	if(x != 5) { printf("the %%n failed\n"); exit(1); }

	/* test %m */
	errno = 0;
	DOTEST(1024, "Success", 7, "%m");

	/* test %p */
	DOTEST(1024, "0x10", 4, "%p", (void*)0x10);
	DOTEST(1024, "(nil)", 5, "%p", (void*)0x0);

	/* test %% */
	DOTEST(1024, "%", 1, "%%");

	/* test %f */
	DOTEST(1024, "0.000000", 8, "%f", 0.0);
	DOTEST(1024, "0.00", 4, "%.2f", 0.0);
	/* differs, "-0.00" DOTEST(1024, "0.00", 4, "%.2f", -0.0); */
	DOTEST(1024, "234.00", 6, "%.2f", 234.005);
	DOTEST(1024, "8973497.1246", 12, "%.4f", 8973497.12456);
	DOTEST(1024, "-12.000000", 10, "%f", -12.0);
	DOTEST(1024, "6", 1, "%.0f", 6.0);

	DOTEST(1024, "6", 1, "%g", 6.0);
	DOTEST(1024, "6.1", 3, "%g", 6.1);
	DOTEST(1024, "6.15", 4, "%g", 6.15);

	/* These format strings are from the code of NSD, Unbound, ldns */

	DOTEST(1024, "abcdef", 6, "%s", "abcdef");
	DOTEST(1024, "005", 3, "%03u", 5);
	DOTEST(1024, "12345", 5, "%03u", 12345);
	DOTEST(1024, "5", 1, "%d", 5);
	DOTEST(1024, "(nil)", 5, "%p", NULL);
	DOTEST(1024, "12345", 5, "%ld", (long)12345);
	DOTEST(1024, "12345", 5, "%lu", (long)12345);
	DOTEST(1024, "       12345", 12, "%12u", (unsigned)12345);
	DOTEST(1024, "12345", 5, "%u", (unsigned)12345);
	DOTEST(1024, "12345", 5, "%llu", (unsigned long long)12345);
	DOTEST(1024, "12345", 5, "%x", 0x12345);
	DOTEST(1024, "12345", 5, "%llx", (long long)0x12345);
	DOTEST(1024, "012345", 6, "%6.6d", 12345);
	DOTEST(1024, "012345", 6, "%6.6u", 12345);
	DOTEST(1024, "1234.54", 7, "%g", 1234.54);
	DOTEST(1024, "123456789.54", 12, "%.12g", 123456789.54);
	DOTEST(1024, "3456789123456.54", 16, "%.16g", 3456789123456.54);
	/* %24g does not work with 24 digits, not enough accuracy,
	 * the first 16 digits are correct */
	DOTEST(1024, "12345", 5, "%3.3d", 12345);
	DOTEST(1024, "000", 3, "%3.3d", 0);
	DOTEST(1024, "001", 3, "%3.3d", 1);
	DOTEST(1024, "012", 3, "%3.3d", 12);
	DOTEST(1024, "-012", 4, "%3.3d", -12);
	DOTEST(1024, "he", 2, "%.2s", "hello");
	DOTEST(1024, "helloworld", 10, "%s%s", "hello", "world");
	DOTEST(1024, "he", 2, "%.*s", 2, "hello");
	DOTEST(1024, "  hello", 7, "%*s", 7, "hello");
	DOTEST(1024, "hello  ", 7, "%*s", -7, "hello");
	DOTEST(1024, "0", 1, "%c", '0'); 
	DOTEST(1024, "A", 1, "%c", 'A'); 
	DOTEST(1024, "", 1, "%c", 0); 
	DOTEST(1024, "\010", 1, "%c", 8); 
	DOTEST(1024, "%", 1, "%%"); 
	DOTEST(1024, "0a", 2, "%02x", 0x0a); 
	DOTEST(1024, "bd", 2, "%02x", 0xbd); 
	DOTEST(1024, "12", 2, "%02ld", (long)12); 
	DOTEST(1024, "02", 2, "%02ld", (long)2); 
	DOTEST(1024, "02", 2, "%02u", (unsigned)2); 
	DOTEST(1024, "765432", 6, "%05u", (unsigned)765432); 
	DOTEST(1024, "10.234", 6, "%0.3f", 10.23421); 
	DOTEST(1024, "123456.234", 10, "%0.3f", 123456.23421); 
	DOTEST(1024, "123456789.234", 13, "%0.3f", 123456789.23421); 
	DOTEST(1024, "123456.23", 9, "%.2f", 123456.23421); 
	DOTEST(1024, "123456", 6, "%.0f", 123456.23421); 
	DOTEST(1024, "0123", 4, "%.4x", 0x0123); 
	DOTEST(1024, "00000123", 8, "%.8x", 0x0123); 
	DOTEST(1024, "ffeb0cde", 8, "%.8x", 0xffeb0cde); 
	DOTEST(1024, " 987654321", 10, "%10lu", (unsigned long)987654321); 
	DOTEST(1024, "   987654321", 12, "%12lu", (unsigned long)987654321); 
	DOTEST(1024, "987654321", 9, "%i", 987654321); 
	DOTEST(1024, "-87654321", 9, "%i", -87654321); 
	DOTEST(1024, "hello           ", 16, "%-16s", "hello"); 
	DOTEST(1024, "                ", 16, "%-16s", ""); 
	DOTEST(1024, "a               ", 16, "%-16s", "a"); 
	DOTEST(1024, "foobarfoobar    ", 16, "%-16s", "foobarfoobar"); 
	DOTEST(1024, "foobarfoobarfoobar", 18, "%-16s", "foobarfoobarfoobar"); 

	/* combined expressions */
	DOTEST(1024, "foo 1.0 size 512 edns", 21,
		"foo %s size %d %s%s", "1.0", 512, "", "edns");
	DOTEST(15, "foo 1.0 size 5", 21,
		"foo %s size %d %s%s", "1.0", 512, "", "edns");
	DOTEST(1024, "packet 1203ceff id", 18,
		"packet %2.2x%2.2x%2.2x%2.2x id", 0x12, 0x03, 0xce, 0xff);
	DOTEST(1024, "/tmp/testbound_123abcd.tmp", 26, "/tmp/testbound_%u%s%s.tmp", 123, "ab", "cd");
 
	return 0;
}
#endif /* SNPRINTF_TEST */

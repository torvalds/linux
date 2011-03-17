/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 */

#define __IN_STRING_C

#include <linux/module.h>
#include <linux/string.h>

char *strcpy(char *dest, const char *src)
{
	return __kernel_strcpy(dest, src);
}
EXPORT_SYMBOL(strcpy);

char *strcat(char *dest, const char *src)
{
	return __kernel_strcpy(dest + __kernel_strlen(dest), src);
}
EXPORT_SYMBOL(strcat);

void *memset(void *s, int c, size_t count)
{
	void *xs = s;
	size_t temp, temp1;

	if (!count)
		return xs;
	c &= 0xff;
	c |= c << 8;
	c |= c << 16;
	if ((long)s & 1) {
		char *cs = s;
		*cs++ = c;
		s = cs;
		count--;
	}
	if (count > 2 && (long)s & 2) {
		short *ss = s;
		*ss++ = c;
		s = ss;
		count -= 2;
	}
	temp = count >> 2;
	if (temp) {
		long *ls = s;

		asm volatile (
			"	movel %1,%2\n"
			"	andw  #7,%2\n"
			"	lsrl  #3,%1\n"
			"	negw  %2\n"
			"	jmp   %%pc@(2f,%2:w:2)\n"
			"1:	movel %3,%0@+\n"
			"	movel %3,%0@+\n"
			"	movel %3,%0@+\n"
			"	movel %3,%0@+\n"
			"	movel %3,%0@+\n"
			"	movel %3,%0@+\n"
			"	movel %3,%0@+\n"
			"	movel %3,%0@+\n"
			"2:	dbra  %1,1b\n"
			"	clrw  %1\n"
			"	subql #1,%1\n"
			"	jpl   1b"
			: "=a" (ls), "=d" (temp), "=&d" (temp1)
			: "d" (c), "0" (ls), "1" (temp));
		s = ls;
	}
	if (count & 2) {
		short *ss = s;
		*ss++ = c;
		s = ss;
	}
	if (count & 1) {
		char *cs = s;
		*cs = c;
	}
	return xs;
}
EXPORT_SYMBOL(memset);

void *memcpy(void *to, const void *from, size_t n)
{
	void *xto = to;
	size_t temp, temp1;

	if (!n)
		return xto;
	if ((long)to & 1) {
		char *cto = to;
		const char *cfrom = from;
		*cto++ = *cfrom++;
		to = cto;
		from = cfrom;
		n--;
	}
	if (n > 2 && (long)to & 2) {
		short *sto = to;
		const short *sfrom = from;
		*sto++ = *sfrom++;
		to = sto;
		from = sfrom;
		n -= 2;
	}
	temp = n >> 2;
	if (temp) {
		long *lto = to;
		const long *lfrom = from;

		asm volatile (
			"	movel %2,%3\n"
			"	andw  #7,%3\n"
			"	lsrl  #3,%2\n"
			"	negw  %3\n"
			"	jmp   %%pc@(1f,%3:w:2)\n"
			"4:	movel %0@+,%1@+\n"
			"	movel %0@+,%1@+\n"
			"	movel %0@+,%1@+\n"
			"	movel %0@+,%1@+\n"
			"	movel %0@+,%1@+\n"
			"	movel %0@+,%1@+\n"
			"	movel %0@+,%1@+\n"
			"	movel %0@+,%1@+\n"
			"1:	dbra  %2,4b\n"
			"	clrw  %2\n"
			"	subql #1,%2\n"
			"	jpl   4b"
			: "=a" (lfrom), "=a" (lto), "=d" (temp), "=&d" (temp1)
			: "0" (lfrom), "1" (lto), "2" (temp));
		to = lto;
		from = lfrom;
	}
	if (n & 2) {
		short *sto = to;
		const short *sfrom = from;
		*sto++ = *sfrom++;
		to = sto;
		from = sfrom;
	}
	if (n & 1) {
		char *cto = to;
		const char *cfrom = from;
		*cto = *cfrom;
	}
	return xto;
}
EXPORT_SYMBOL(memcpy);

void *memmove(void *dest, const void *src, size_t n)
{
	void *xdest = dest;
	size_t temp;

	if (!n)
		return xdest;

	if (dest < src) {
		if ((long)dest & 1) {
			char *cdest = dest;
			const char *csrc = src;
			*cdest++ = *csrc++;
			dest = cdest;
			src = csrc;
			n--;
		}
		if (n > 2 && (long)dest & 2) {
			short *sdest = dest;
			const short *ssrc = src;
			*sdest++ = *ssrc++;
			dest = sdest;
			src = ssrc;
			n -= 2;
		}
		temp = n >> 2;
		if (temp) {
			long *ldest = dest;
			const long *lsrc = src;
			temp--;
			do
				*ldest++ = *lsrc++;
			while (temp--);
			dest = ldest;
			src = lsrc;
		}
		if (n & 2) {
			short *sdest = dest;
			const short *ssrc = src;
			*sdest++ = *ssrc++;
			dest = sdest;
			src = ssrc;
		}
		if (n & 1) {
			char *cdest = dest;
			const char *csrc = src;
			*cdest = *csrc;
		}
	} else {
		dest = (char *)dest + n;
		src = (const char *)src + n;
		if ((long)dest & 1) {
			char *cdest = dest;
			const char *csrc = src;
			*--cdest = *--csrc;
			dest = cdest;
			src = csrc;
			n--;
		}
		if (n > 2 && (long)dest & 2) {
			short *sdest = dest;
			const short *ssrc = src;
			*--sdest = *--ssrc;
			dest = sdest;
			src = ssrc;
			n -= 2;
		}
		temp = n >> 2;
		if (temp) {
			long *ldest = dest;
			const long *lsrc = src;
			temp--;
			do
				*--ldest = *--lsrc;
			while (temp--);
			dest = ldest;
			src = lsrc;
		}
		if (n & 2) {
			short *sdest = dest;
			const short *ssrc = src;
			*--sdest = *--ssrc;
			dest = sdest;
			src = ssrc;
		}
		if (n & 1) {
			char *cdest = dest;
			const char *csrc = src;
			*--cdest = *--csrc;
		}
	}
	return xdest;
}
EXPORT_SYMBOL(memmove);

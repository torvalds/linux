/*
 * Copyright (C) 2017 Denys Vlasenko <vda.linux@googlemail.com>
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */

//kbuild:lib-y += isqrt.o

#ifndef ISQRT_TEST
# include "libbb.h"
#else
/* gcc -DISQRT_TEST -Wall -O2 isqrt.c -oisqrt && ./isqrt $((RANDOM*RANDOM)) */
# include <stdlib.h>
# include <stdio.h>
# include <time.h>
# define FAST_FUNC /* nothing */
#endif

/* Returns such x that x+1 > sqrt(N) */
unsigned long FAST_FUNC isqrt(unsigned long long N)
{
	unsigned long x;
	unsigned shift;
#define LL_WIDTH_BITS (unsigned)(sizeof(N)*8)

	shift = LL_WIDTH_BITS - 2;
	x = 0;
	do {
		x = (x << 1) + 1;
		if ((unsigned long long)x * x > (N >> shift))
			x--; /* whoops, that +1 was too much */
		shift -= 2;
	} while ((int)shift >= 0);
	return x;
}

#ifdef ISQRT_TEST
int main(int argc, char **argv)
{
	unsigned long long n = argv[1] ? strtoull(argv[1], NULL, 0) : time(NULL);
	for (;;) {
		unsigned long h;
		n--;
		h = isqrt(n);
		if (!(n & 0xffff))
			printf("isqrt(%llx)=%lx\n", n, h);
		if ((unsigned long long)h * h > n) {
			printf("BAD1: isqrt(%llx)=%lx\n", n, h);
			return 1;
		}
		h++;
		if ((unsigned long long)h * h != 0 /* this can overflow to 0 - not a bug */
		 && (unsigned long long)h * h <= n)
		{
			printf("BAD2: isqrt(%llx)=%lx\n", n, h);
			return 1;
		}
	}
}
#endif

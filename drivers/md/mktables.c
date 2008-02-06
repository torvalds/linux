#ident "$Id: mktables.c,v 1.2 2002/12/12 22:41:27 hpa Exp $"
/* ----------------------------------------------------------------------- *
 *
 *   Copyright 2002 H. Peter Anvin - All Rights Reserved
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, Inc., 53 Temple Place Ste 330,
 *   Bostom MA 02111-1307, USA; either version 2 of the License, or
 *   (at your option) any later version; incorporated herein by reference.
 *
 * ----------------------------------------------------------------------- */

/*
 * mktables.c
 *
 * Make RAID-6 tables.  This is a host user space program to be run at
 * compile time.
 */

#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <stdlib.h>
#include <time.h>

static uint8_t gfmul(uint8_t a, uint8_t b)
{
	uint8_t v = 0;

	while (b) {
		if (b & 1)
			v ^= a;
		a = (a << 1) ^ (a & 0x80 ? 0x1d : 0);
		b >>= 1;
	}

	return v;
}

static uint8_t gfpow(uint8_t a, int b)
{
	uint8_t v = 1;

	b %= 255;
	if (b < 0)
		b += 255;

	while (b) {
		if (b & 1)
			v = gfmul(v, a);
		a = gfmul(a, a);
		b >>= 1;
	}

	return v;
}

int main(int argc, char *argv[])
{
	int i, j, k;
	uint8_t v;
	uint8_t exptbl[256], invtbl[256];

	printf("#include \"raid6.h\"\n");

	/* Compute multiplication table */
	printf("\nconst u8  __attribute__((aligned(256)))\n"
		"raid6_gfmul[256][256] =\n"
		"{\n");
	for (i = 0; i < 256; i++) {
		printf("\t{\n");
		for (j = 0; j < 256; j += 8) {
			printf("\t\t");
			for (k = 0; k < 8; k++)
				printf("0x%02x, ", gfmul(i, j+k));
			printf("\n");
		}
		printf("\t},\n");
	}
	printf("};\n");

	/* Compute power-of-2 table (exponent) */
	v = 1;
	printf("\nconst u8 __attribute__((aligned(256)))\n"
		"raid6_gfexp[256] =\n"
		"{\n");
	for (i = 0; i < 256; i += 8) {
		printf("\t");
		for (j = 0; j < 8; j++) {
			exptbl[i+j] = v;
			printf("0x%02x, ", v);
			v = gfmul(v, 2);
			if (v == 1)
				v = 0;	/* For entry 255, not a real entry */
		}
		printf("\n");
	}
	printf("};\n");

	/* Compute inverse table x^-1 == x^254 */
	printf("\nconst u8 __attribute__((aligned(256)))\n"
		"raid6_gfinv[256] =\n"
		"{\n");
	for (i = 0; i < 256; i += 8) {
		printf("\t");
		for (j = 0; j < 8; j++) {
			v = gfpow(i+j, 254);
			invtbl[i+j] = v;
			printf("0x%02x, ", v);
		}
		printf("\n");
	}
	printf("};\n");

	/* Compute inv(2^x + 1) (exponent-xor-inverse) table */
	printf("\nconst u8 __attribute__((aligned(256)))\n"
		"raid6_gfexi[256] =\n"
		"{\n");
	for (i = 0; i < 256; i += 8) {
		printf("\t");
		for (j = 0; j < 8; j++)
			printf("0x%02x, ", invtbl[exptbl[i+j]^1]);
		printf("\n");
	}
	printf("};\n\n");

	return 0;
}

/*
 * Copyright (C) Cort Dougan 1999.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 * Generate a note section as per the CHRP specification.
 *
 */

#include <stdio.h>

#define PL(x) printf("%c%c%c%c", ((x)>>24)&0xff, ((x)>>16)&0xff, ((x)>>8)&0xff, (x)&0xff );

int main(void)
{
/* header */
	/* namesz */
	PL(strlen("PowerPC")+1);
	/* descrsz */
	PL(6*4);
	/* type */
	PL(0x1275);
	/* name */
	printf("PowerPC"); printf("%c", 0);
	
/* descriptor */
	/* real-mode */
	PL(0xffffffff);
	/* real-base */
	PL(0x00c00000);
	/* real-size */
	PL(0xffffffff);
	/* virt-base */
	PL(0xffffffff);
	/* virt-size */
	PL(0xffffffff);
	/* load-base */
	PL(0x4000);
	return 0;
}

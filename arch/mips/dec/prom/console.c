/*
 *	arch/mips/dec/prom/console.c
 *
 *	DECstation PROM-based early console support.
 *
 *	Copyright (C) 2004  Maciej W. Rozycki
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 */
#include <linux/console.h>
#include <linux/init.h>
#include <linux/kernel.h>

#include <asm/dec/prom.h>

void prom_putchar(char c)
{
	char s[2];

	s[0] = c;
	s[1] = '\0';

	prom_printf( s);
}

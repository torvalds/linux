/*
 *  linux/arch/arm/lib/io-shark.c
 *
 *  by Alexander Schulz
 *
 * derived from:
 * linux/arch/arm/lib/io-ebsa.S
 * Copyright (C) 1995, 1996 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/kernel.h>

#include <asm/io.h>

void print_warning(void)
{
	printk(KERN_WARNING "ins?/outs? not implemented on this architecture\n");
}

void insl(unsigned int port, void *to, int len)
{
	print_warning();
}

void insb(unsigned int port, void *to, int len)
{
	print_warning();
}

void outsl(unsigned int port, const void *from, int len)
{
	print_warning();
}

void outsb(unsigned int port, const void *from, int len)
{
	print_warning();
}

/* these should be in assembler again */

/*
 * Purpose: read a block of data from a hardware register to memory.
 * Proto  : insw(int from_port, void *to, int len_in_words);
 * Proto  : inswb(int from_port, void *to, int len_in_bytes);
 * Notes  : increment to
 */

void insw(unsigned int port, void *to, int len)
{
	int i;

	for (i = 0; i < len; i++)
		((unsigned short *) to)[i] = inw(port);
}

void inswb(unsigned int port, void *to, int len)
{
	insw(port, to, len >> 2);
}

/*
 * Purpose: write a block of data from memory to a hardware register.
 * Proto  : outsw(int to_reg, void *from, int len_in_words);
 * Proto  : outswb(int to_reg, void *from, int len_in_bytes);
 * Notes  : increments from
 */

void outsw(unsigned int port, const void *from, int len)
{
	int i;

	for (i = 0; i < len; i++)
		outw(((unsigned short *) from)[i], port);
}

void outswb(unsigned int port, const void *from, int len)
{
	outsw(port, from, len >> 2);
}

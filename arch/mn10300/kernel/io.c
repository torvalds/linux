/* MN10300 Misaligned multibyte-word I/O
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */
#include <linux/module.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <asm/io.h>

/*
 * output data from a potentially misaligned buffer
 */
void __outsl(unsigned long addr, const void *buffer, int count)
{
	const unsigned char *buf = buffer;
	unsigned long val;

	while (count--) {
		memcpy(&val, buf, 4);
		outl(val, addr);
		buf += 4;
	}
}
EXPORT_SYMBOL(__outsl);

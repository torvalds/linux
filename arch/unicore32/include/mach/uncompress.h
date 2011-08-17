/*
 * linux/arch/unicore32/include/mach/uncompress.h
 *
 * Code specific to PKUnity SoC and UniCore ISA
 *
 * Copyright (C) 2001-2010 GUAN Xue-tao
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __MACH_PUV3_UNCOMPRESS_H__
#define __MACH_PUV3_UNCOMPRESS_H__

#include "hardware.h"
#include "ocd.h"

extern char input_data[];
extern char input_data_end[];

static void arch_decomp_puts(const char *ptr)
{
	char c;

	while ((c = *ptr++) != '\0') {
		if (c == '\n')
			putc('\r');
		putc(c);
	}
}
#define ARCH_HAVE_DECOMP_PUTS

#endif /* __MACH_PUV3_UNCOMPRESS_H__ */

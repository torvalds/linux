/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * linux/arch/unicore32/include/mach/uncompress.h
 *
 * Code specific to PKUnity SoC and UniCore ISA
 *
 * Copyright (C) 2001-2010 GUAN Xue-tao
 */

#ifndef __MACH_PUV3_UNCOMPRESS_H__
#define __MACH_PUV3_UNCOMPRESS_H__

#include <mach/hardware.h>
#include <mach/ocd.h>

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

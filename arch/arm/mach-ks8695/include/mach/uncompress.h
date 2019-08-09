/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * arch/arm/mach-ks8695/include/mach/uncompress.h
 *
 * Copyright (C) 2006 Ben Dooks <ben@simtec.co.uk>
 * Copyright (C) 2006 Simtec Electronics
 *
 * KS8695 - Kernel uncompressor
 */

#ifndef __ASM_ARCH_UNCOMPRESS_H
#define __ASM_ARCH_UNCOMPRESS_H

#include <linux/io.h>
#include <mach/regs-uart.h>

static inline void putc(char c)
{
	while (!(__raw_readl((void __iomem*)KS8695_UART_PA + KS8695_URLS) & URLS_URTHRE))
		barrier();

	__raw_writel(c, (void __iomem*)KS8695_UART_PA + KS8695_URTH);
}

static inline void flush(void)
{
	while (!(__raw_readl((void __iomem*)KS8695_UART_PA + KS8695_URLS) & URLS_URTE))
		barrier();
}

#define arch_decomp_setup()

#endif

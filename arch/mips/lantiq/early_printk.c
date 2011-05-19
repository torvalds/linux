/*
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License version 2 as published
 *  by the Free Software Foundation.
 *
 *  Copyright (C) 2010 John Crispin <blogic@openwrt.org>
 */

#include <linux/init.h>
#include <linux/cpu.h>

#include <lantiq.h>
#include <lantiq_soc.h>

/* no ioremap possible at this early stage, lets use KSEG1 instead  */
#define LTQ_ASC_BASE	KSEG1ADDR(LTQ_ASC1_BASE_ADDR)
#define ASC_BUF		1024
#define LTQ_ASC_FSTAT	((u32 *)(LTQ_ASC_BASE + 0x0048))
#define LTQ_ASC_TBUF	((u32 *)(LTQ_ASC_BASE + 0x0020))
#define TXMASK		0x3F00
#define TXOFFSET	8

void prom_putchar(char c)
{
	unsigned long flags;

	local_irq_save(flags);
	do { } while ((ltq_r32(LTQ_ASC_FSTAT) & TXMASK) >> TXOFFSET);
	if (c == '\n')
		ltq_w32('\r', LTQ_ASC_TBUF);
	ltq_w32(c, LTQ_ASC_TBUF);
	local_irq_restore(flags);
}

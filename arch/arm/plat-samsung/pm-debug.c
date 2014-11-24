/*
 * Copyright (C) 2013 Samsung Electronics Co., Ltd.
 *	Tomasz Figa <t.figa@samsung.com>
 * Copyright (C) 2008 Openmoko, Inc.
 * Copyright (C) 2004-2008 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
 *	http://armlinux.simtec.co.uk/
 *
 * Samsung common power management (suspend to RAM) debug support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/serial_core.h>
#include <linux/serial_s3c.h>
#include <linux/io.h>

#include <asm/mach/map.h>

#include <plat/cpu.h>
#include <plat/pm-common.h>

#ifdef CONFIG_SAMSUNG_ATAGS
#include <mach/pm-core.h>
#else
static inline void s3c_pm_debug_init_uart(void) {}
static inline void s3c_pm_arch_update_uart(void __iomem *regs,
					   struct pm_uart_save *save) {}
#endif

static struct pm_uart_save uart_save;

extern void printascii(const char *);

void s3c_pm_dbg(const char *fmt, ...)
{
	va_list va;
	char buff[256];

	va_start(va, fmt);
	vsnprintf(buff, sizeof(buff), fmt, va);
	va_end(va);

	printascii(buff);
}

void s3c_pm_debug_init(void)
{
	/* restart uart clocks so we can use them to output */
	s3c_pm_debug_init_uart();
}

static inline void __iomem *s3c_pm_uart_base(void)
{
	unsigned long paddr;
	unsigned long vaddr;

	debug_ll_addr(&paddr, &vaddr);

	return (void __iomem *)vaddr;
}

void s3c_pm_save_uarts(void)
{
	void __iomem *regs = s3c_pm_uart_base();
	struct pm_uart_save *save = &uart_save;

	save->ulcon = __raw_readl(regs + S3C2410_ULCON);
	save->ucon = __raw_readl(regs + S3C2410_UCON);
	save->ufcon = __raw_readl(regs + S3C2410_UFCON);
	save->umcon = __raw_readl(regs + S3C2410_UMCON);
	save->ubrdiv = __raw_readl(regs + S3C2410_UBRDIV);

	if (!soc_is_s3c2410())
		save->udivslot = __raw_readl(regs + S3C2443_DIVSLOT);

	S3C_PMDBG("UART[%p]: ULCON=%04x, UCON=%04x, UFCON=%04x, UBRDIV=%04x\n",
		  regs, save->ulcon, save->ucon, save->ufcon, save->ubrdiv);
}

void s3c_pm_restore_uarts(void)
{
	void __iomem *regs = s3c_pm_uart_base();
	struct pm_uart_save *save = &uart_save;

	s3c_pm_arch_update_uart(regs, save);

	__raw_writel(save->ulcon, regs + S3C2410_ULCON);
	__raw_writel(save->ucon,  regs + S3C2410_UCON);
	__raw_writel(save->ufcon, regs + S3C2410_UFCON);
	__raw_writel(save->umcon, regs + S3C2410_UMCON);
	__raw_writel(save->ubrdiv, regs + S3C2410_UBRDIV);

	if (!soc_is_s3c2410())
		__raw_writel(save->udivslot, regs + S3C2443_DIVSLOT);
}

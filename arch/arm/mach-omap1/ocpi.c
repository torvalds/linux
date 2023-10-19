// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * linux/arch/arm/plat-omap/ocpi.c
 *
 * Minimal OCP bus support for omap16xx
 *
 * Copyright (C) 2003 - 2005 Nokia Corporation
 * Copyright (C) 2012 Texas Instruments, Inc.
 * Written by Tony Lindgren <tony@atomide.com>
 *
 * Modified for clock framework by Paul Mundt <paul.mundt@nokia.com>.
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/soc/ti/omap1-io.h>

#include "hardware.h"
#include "common.h"

#define OCPI_BASE		0xfffec320
#define OCPI_FAULT		(OCPI_BASE + 0x00)
#define OCPI_CMD_FAULT		(OCPI_BASE + 0x04)
#define OCPI_SINT0		(OCPI_BASE + 0x08)
#define OCPI_TABORT		(OCPI_BASE + 0x0c)
#define OCPI_SINT1		(OCPI_BASE + 0x10)
#define OCPI_PROT		(OCPI_BASE + 0x14)
#define OCPI_SEC		(OCPI_BASE + 0x18)

/* USB OHCI OCPI access error registers */
#define HOSTUEADDR	0xfffba0e0
#define HOSTUESTATUS	0xfffba0e4

static struct clk *ocpi_ck;

/*
 * Enables device access to OMAP buses via the OCPI bridge
 */
int ocpi_enable(void)
{
	unsigned int val;

	if (!cpu_is_omap16xx())
		return -ENODEV;

	/* Enable access for OHCI in OCPI */
	val = omap_readl(OCPI_PROT);
	val &= ~0xff;
	/* val &= (1 << 0);	 Allow access only to EMIFS */
	omap_writel(val, OCPI_PROT);

	val = omap_readl(OCPI_SEC);
	val &= ~0xff;
	omap_writel(val, OCPI_SEC);

	return 0;
}
EXPORT_SYMBOL(ocpi_enable);

static int __init omap_ocpi_init(void)
{
	if (!cpu_is_omap16xx())
		return -ENODEV;

	ocpi_ck = clk_get(NULL, "l3_ocpi_ck");
	if (IS_ERR(ocpi_ck))
		return PTR_ERR(ocpi_ck);

	clk_prepare_enable(ocpi_ck);
	ocpi_enable();
	pr_info("OMAP OCPI interconnect driver loaded\n");

	return 0;
}

static void __exit omap_ocpi_exit(void)
{
	/* REVISIT: Disable OCPI */

	if (!cpu_is_omap16xx())
		return;

	clk_disable_unprepare(ocpi_ck);
	clk_put(ocpi_ck);
}

MODULE_AUTHOR("Tony Lindgren <tony@atomide.com>");
MODULE_DESCRIPTION("OMAP OCPI bus controller module");
MODULE_LICENSE("GPL");
module_init(omap_ocpi_init);
module_exit(omap_ocpi_exit);

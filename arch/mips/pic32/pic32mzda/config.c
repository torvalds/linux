/*
 * Purna Chandra Mandal, purna.mandal@microchip.com
 * Copyright (C) 2015 Microchip Technology Inc.  All rights reserved.
 *
 *  This program is free software; you can distribute it and/or modify it
 *  under the terms of the GNU General Public License (Version 2) as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 */
#include <linux/init.h>
#include <linux/io.h>
#include <linux/of_platform.h>

#include <asm/mach-pic32/pic32.h>

#include "pic32mzda.h"

#define PIC32_CFGCON	0x0000
#define PIC32_DEVID	0x0020
#define PIC32_SYSKEY	0x0030
#define PIC32_CFGEBIA	0x00c0
#define PIC32_CFGEBIC	0x00d0
#define PIC32_CFGCON2	0x00f0
#define PIC32_RCON	0x1240

static void __iomem *pic32_conf_base;
static DEFINE_SPINLOCK(config_lock);
static u32 pic32_reset_status;

static u32 pic32_conf_get_reg_field(u32 offset, u32 rshift, u32 mask)
{
	u32 v;

	v = readl(pic32_conf_base + offset);
	v >>= rshift;
	v &= mask;

	return v;
}

static u32 pic32_conf_modify_atomic(u32 offset, u32 mask, u32 set)
{
	u32 v;
	unsigned long flags;

	spin_lock_irqsave(&config_lock, flags);
	v = readl(pic32_conf_base + offset);
	v &= ~mask;
	v |= (set & mask);
	writel(v, pic32_conf_base + offset);
	spin_unlock_irqrestore(&config_lock, flags);

	return 0;
}

int pic32_enable_lcd(void)
{
	return pic32_conf_modify_atomic(PIC32_CFGCON2, BIT(31), BIT(31));
}

int pic32_disable_lcd(void)
{
	return pic32_conf_modify_atomic(PIC32_CFGCON2, BIT(31), 0);
}

int pic32_set_lcd_mode(int mode)
{
	u32 mask = mode ? BIT(30) : 0;

	return pic32_conf_modify_atomic(PIC32_CFGCON2, BIT(30), mask);
}

int pic32_set_sdhci_adma_fifo_threshold(u32 rthrsh, u32 wthrsh)
{
	u32 clr, set;

	clr = (0x3ff << 4) | (0x3ff << 16);
	set = (rthrsh << 4) | (wthrsh << 16);
	return pic32_conf_modify_atomic(PIC32_CFGCON2, clr, set);
}

void pic32_syskey_unlock_debug(const char *func, const ulong line)
{
	void __iomem *syskey = pic32_conf_base + PIC32_SYSKEY;

	pr_debug("%s: called from %s:%lu\n", __func__, func, line);
	writel(0x00000000, syskey);
	writel(0xAA996655, syskey);
	writel(0x556699AA, syskey);
}

static u32 pic32_get_device_id(void)
{
	return pic32_conf_get_reg_field(PIC32_DEVID, 0, 0x0fffffff);
}

static u32 pic32_get_device_version(void)
{
	return pic32_conf_get_reg_field(PIC32_DEVID, 28, 0xf);
}

u32 pic32_get_boot_status(void)
{
	return pic32_reset_status;
}
EXPORT_SYMBOL(pic32_get_boot_status);

void __init pic32_config_init(void)
{
	pic32_conf_base = ioremap(PIC32_BASE_CONFIG, 0x110);
	if (!pic32_conf_base)
		panic("pic32: config base not mapped");

	/* Boot Status */
	pic32_reset_status = readl(pic32_conf_base + PIC32_RCON);
	writel(-1, PIC32_CLR(pic32_conf_base + PIC32_RCON));

	/* Device Inforation */
	pr_info("Device Id: 0x%08x, Device Ver: 0x%04x\n",
		pic32_get_device_id(),
		pic32_get_device_version());
}

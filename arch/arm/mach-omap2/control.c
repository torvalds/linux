/*
 * OMAP2/3 System Control Module register access
 *
 * Copyright (C) 2007 Texas Instruments, Inc.
 * Copyright (C) 2007 Nokia Corporation
 *
 * Written by Paul Walmsley
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#undef DEBUG

#include <linux/kernel.h>
#include <linux/io.h>

#include <mach/common.h>
#include <mach/control.h>

static void __iomem *omap2_ctrl_base;

#define OMAP_CTRL_REGADDR(reg)		(omap2_ctrl_base + (reg))

void __init omap2_set_globals_control(struct omap_globals *omap2_globals)
{
	omap2_ctrl_base = omap2_globals->ctrl;
}

void __iomem *omap_ctrl_base_get(void)
{
	return omap2_ctrl_base;
}

u8 omap_ctrl_readb(u16 offset)
{
	return __raw_readb(OMAP_CTRL_REGADDR(offset));
}

u16 omap_ctrl_readw(u16 offset)
{
	return __raw_readw(OMAP_CTRL_REGADDR(offset));
}

u32 omap_ctrl_readl(u16 offset)
{
	return __raw_readl(OMAP_CTRL_REGADDR(offset));
}

void omap_ctrl_writeb(u8 val, u16 offset)
{
	__raw_writeb(val, OMAP_CTRL_REGADDR(offset));
}

void omap_ctrl_writew(u16 val, u16 offset)
{
	__raw_writew(val, OMAP_CTRL_REGADDR(offset));
}

void omap_ctrl_writel(u32 val, u16 offset)
{
	__raw_writel(val, OMAP_CTRL_REGADDR(offset));
}


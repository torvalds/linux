/*
 * Copyright (C) 2013 ROCKCHIP, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/delay.h>
#include <linux/spinlock.h>
#include <linux/rockchip/iomap.h>
#include <linux/kobject.h>
#include "efuse.h"

#define efuse_readl(offset) readl_relaxed(RK_EFUSE_VIRT + offset)
#define efuse_writel(val, offset) writel_relaxed(val, RK_EFUSE_VIRT + offset)

static u8 efuse_buf[32 + 1] = {0, 0};

static int efuse_readregs(u32 addr, u32 length, u8 *buf)
{
#ifndef efuse_readl
	return 0;
#else
	unsigned long flags;
	static DEFINE_SPINLOCK(efuse_lock);
	int ret = length;

	if (!length)
		return 0;

	spin_lock_irqsave(&efuse_lock, flags);

	efuse_writel(EFUSE_CSB, REG_EFUSE_CTRL);
	efuse_writel(EFUSE_LOAD | EFUSE_PGENB, REG_EFUSE_CTRL);
	udelay(2);
	do {
		efuse_writel(efuse_readl(REG_EFUSE_CTRL) &
			(~(EFUSE_A_MASK << EFUSE_A_SHIFT)), REG_EFUSE_CTRL);
		efuse_writel(efuse_readl(REG_EFUSE_CTRL) |
			((addr & EFUSE_A_MASK) << EFUSE_A_SHIFT),
			REG_EFUSE_CTRL);
		udelay(2);
		efuse_writel(efuse_readl(REG_EFUSE_CTRL) |
				EFUSE_STROBE, REG_EFUSE_CTRL);
		udelay(2);
		*buf = efuse_readl(REG_EFUSE_DOUT);
		efuse_writel(efuse_readl(REG_EFUSE_CTRL) &
				(~EFUSE_STROBE), REG_EFUSE_CTRL);
		udelay(2);
		buf++;
		addr++;
	} while (--length);
	udelay(2);
	efuse_writel(efuse_readl(REG_EFUSE_CTRL) | EFUSE_CSB, REG_EFUSE_CTRL);
	udelay(1);

	spin_unlock_irqrestore(&efuse_lock, flags);
	return ret;
#endif
}

/*
static int efuse_writeregs(u32 addr, u32 length, u8 *buf)
{
	u32 j=0;
	unsigned long flags;
	static DEFINE_SPINLOCK(efuse_lock);
	spin_lock_irqsave(&efuse_lock, flags);

	efuse_writel(EFUSE_CSB|EFUSE_LOAD|EFUSE_PGENB,REG_EFUSE_CTRL);
	udelay(10);
	efuse_writel((~EFUSE_PGENB)&(efuse_readl(REG_EFUSE_CTRL)),
		REG_EFUSE_CTRL);
	udelay(10);
	efuse_writel((~(EFUSE_LOAD | EFUSE_CSB))&(efuse_readl(REG_EFUSE_CTRL)),
		REG_EFUSE_CTRL);
	udelay(1);

	do {
		for(j=0; j<8; j++){
			if(*buf & (1<<j)){
				efuse_writel(efuse_readl(REG_EFUSE_CTRL) &
					(~(EFUSE_A_MASK << EFUSE_A_SHIFT)),
					REG_EFUSE_CTRL);
				efuse_writel(efuse_readl(REG_EFUSE_CTRL) |
					(((addr + (j<<5))&EFUSE_A_MASK) << EFUSE_A_SHIFT),
					REG_EFUSE_CTRL);
				udelay(1);
				efuse_writel(efuse_readl(REG_EFUSE_CTRL) |
				EFUSE_STROBE, REG_EFUSE_CTRL);
				udelay(10);
				efuse_writel(efuse_readl(REG_EFUSE_CTRL) &
				(~EFUSE_STROBE), REG_EFUSE_CTRL);
				udelay(1);
			}
		}
		buf++;
		addr++;
	} while (--length);

	udelay(1);
	efuse_writel(efuse_readl(REG_EFUSE_CTRL) |
		EFUSE_CSB | EFUSE_LOAD, REG_EFUSE_CTRL);
	udelay(1);
	efuse_writel(efuse_readl(REG_EFUSE_CTRL)|EFUSE_PGENB, REG_EFUSE_CTRL);
	udelay(1);

	spin_unlock_irqrestore(&efuse_lock, flags);
	return 0;
}
*/

int rockchip_efuse_version(void)
{
	int ret = efuse_buf[4] & (~(0x1 << 3));
	return ret;
}

int rockchip_get_leakage(int ch)
{
	if ((ch < 0) || (ch > 2))
		return 0;

	return efuse_buf[23+ch];
}

static int efuse_init(void)
{
	efuse_readregs(0, 32, efuse_buf);

	return 0;
}

core_initcall(efuse_init);

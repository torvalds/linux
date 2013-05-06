/*
 * Copyright (C) 2013 ROCKCHIP, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/spinlock.h>
#include <plat/efuse.h>

/* eFuse controller register */
#define EFUSE_A_SHIFT		(6)
#define EFUSE_A_MASK		(0xFF)
//#define EFUSE_PD		(1 << 5)
//#define EFUSE_PS		(1 << 4)
#define EFUSE_PGENB		(1 << 3) //active low
#define EFUSE_LOAD		(1 << 2)
#define EFUSE_STROBE		(1 << 1)
#define EFUSE_CSB		(1 << 0) //active low

#define REG_EFUSE_CTRL		(0x0000)
#define REG_EFUSE_DOUT		(0x0004)

#if defined(CONFIG_ARCH_RK3188)
#define efuse_readl(offset)		readl_relaxed(RK30_EFUSE_BASE + offset)
#define efuse_writel(val, offset)	writel_relaxed(val, RK30_EFUSE_BASE + offset)
#endif

int efuse_readregs(u32 addr, u32 length, u8 *pData)
{
#ifdef efuse_readl
	unsigned long flags;
	static DEFINE_SPINLOCK(efuse_lock);

	if (!length)
		return 0;

	spin_lock_irqsave(&efuse_lock, flags);

	efuse_writel(EFUSE_CSB, REG_EFUSE_CTRL);
	efuse_writel(EFUSE_LOAD | EFUSE_PGENB, REG_EFUSE_CTRL);
	udelay(2);
	do {
		efuse_writel(efuse_readl(REG_EFUSE_CTRL) & (~(EFUSE_A_MASK << EFUSE_A_SHIFT)), REG_EFUSE_CTRL);
		efuse_writel(efuse_readl(REG_EFUSE_CTRL) | ((addr & EFUSE_A_MASK) << EFUSE_A_SHIFT), REG_EFUSE_CTRL);
		udelay(2);
		efuse_writel(efuse_readl(REG_EFUSE_CTRL) | EFUSE_STROBE, REG_EFUSE_CTRL);
		udelay(2);
		*pData = efuse_readl(REG_EFUSE_DOUT);
		efuse_writel(efuse_readl(REG_EFUSE_CTRL) & (~EFUSE_STROBE), REG_EFUSE_CTRL);
		udelay(2);
		pData++;
		addr++;
	} while(--length);
	udelay(2);
	efuse_writel(efuse_readl(REG_EFUSE_CTRL) | EFUSE_CSB, REG_EFUSE_CTRL);
	udelay(1);

	spin_unlock_irqrestore(&efuse_lock, flags);
#endif
	return 0;
}

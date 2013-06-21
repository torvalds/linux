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

#if defined(CONFIG_ARCH_RK3188)
#define efuse_readl(offset)		readl_relaxed(RK30_EFUSE_BASE + offset)
#define efuse_writel(val, offset)	writel_relaxed(val, RK30_EFUSE_BASE + offset)
#endif

u8 efuse_buf[32 + 1] = {0, 0};

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
		efuse_writel(efuse_readl(REG_EFUSE_CTRL) & (~(EFUSE_A_MASK << EFUSE_A_SHIFT)), REG_EFUSE_CTRL);
		efuse_writel(efuse_readl(REG_EFUSE_CTRL) | ((addr & EFUSE_A_MASK) << EFUSE_A_SHIFT), REG_EFUSE_CTRL);
		udelay(2);
		efuse_writel(efuse_readl(REG_EFUSE_CTRL) | EFUSE_STROBE, REG_EFUSE_CTRL);
		udelay(2);
		*buf = efuse_readl(REG_EFUSE_DOUT);
		efuse_writel(efuse_readl(REG_EFUSE_CTRL) & (~EFUSE_STROBE), REG_EFUSE_CTRL);
		udelay(2);
		buf++;
		addr++;
	} while(--length);
	udelay(2);
	efuse_writel(efuse_readl(REG_EFUSE_CTRL) | EFUSE_CSB, REG_EFUSE_CTRL);
	udelay(1);

	spin_unlock_irqrestore(&efuse_lock, flags);
	return ret;
#endif
}

void rk_efuse_init(void)
{
	efuse_readregs(0x0, 32, efuse_buf);
}

int rk_pll_flag(void)
{
	return efuse_buf[22] & 0x3;
}

int rk_leakage_val(void)
{
	/*
	 * efuse_buf[22]
	 * bit[3]:
	 * 	0:enable leakage level auto voltage scale
	 * 	1:disalbe leakage level avs
	 */
	if ((efuse_buf[22] >> 2) & 0x1)
		return 0;
	else
		return  (efuse_buf[22] >> 4) & 0x0f;
}

int rk3028_version_val(void)
{
	return efuse_buf[5];
}

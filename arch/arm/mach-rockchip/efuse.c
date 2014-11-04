/*
 * Copyright (C) 2013-2014 ROCKCHIP, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/crc32.h>
#include <linux/delay.h>
#include <linux/rockchip/cpu.h>
#include <linux/rockchip/iomap.h>
#include <asm/system_info.h>
#include "efuse.h"

#define efuse_readl(offset) readl_relaxed(RK_EFUSE_VIRT + offset)
#define efuse_writel(val, offset) writel_relaxed(val, RK_EFUSE_VIRT + offset)

static u8 efuse_buf[32] = {};

struct rockchip_efuse {
	int (*get_leakage)(int ch);
	int efuse_version;
	int process_version;
};

static struct rockchip_efuse efuse;

static int __init rk3288_efuse_readregs(u32 addr, u32 length, u8 *buf)
{
	int ret = length;

	if (!length)
		return 0;
	if (!buf)
		return 0;

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

	return ret;
}

static int __init rk3288_get_efuse_version(void)
{
	int ret = efuse_buf[4] & (~(0x1 << 3));
	return ret;
}

static int __init rk3288_get_process_version(void)
{
	int ret = efuse_buf[6]&0x0f;

	return ret;
}

static int rk3288_get_leakage(int ch)
{
	if ((ch < 0) || (ch > 2))
		return 0;

	return efuse_buf[23+ch];
}

static void __init rk3288_set_system_serial(void)
{
	int i;
	u8 buf[16];

	for (i = 0; i < 8; i++) {
		buf[i] = efuse_buf[8 + (i << 1)];
		buf[i + 8] = efuse_buf[7 + (i << 1)];
	}

	system_serial_low = crc32(0, buf, 8);
	system_serial_high = crc32(system_serial_low, buf + 8, 8);
}

int rockchip_efuse_version(void)
{
	return efuse.efuse_version;
}

int rockchip_process_version(void)
{
	return efuse.process_version;
}

int rockchip_get_leakage(int ch)
{
	if (efuse.get_leakage)
		return efuse.get_leakage(ch);
	return 0;
}

void __init rockchip_efuse_init(void)
{
	int ret;

	if (cpu_is_rk3288()) {
		ret = rk3288_efuse_readregs(0, 32, efuse_buf);
		if (ret == 32) {
			efuse.get_leakage = rk3288_get_leakage;
			efuse.efuse_version = rk3288_get_efuse_version();
			efuse.process_version = rk3288_get_process_version();
			rockchip_set_cpu_version((efuse_buf[6] >> 4) & 3);
			rk3288_set_system_serial();
		} else {
			pr_err("failed to read eFuse, return %d\n", ret);
		}
	}
}

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

#include <linux/cpuidle.h>
#include <linux/init.h>
#include <linux/of.h>
#include <linux/smp.h>
#include <linux/slab.h>
#include <linux/reboot.h>
#include <linux/pm.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <uapi/linux/psci.h>

#include <asm/compiler.h>
#include <asm/cpu_ops.h>
#include <asm/errno.h>
#include <asm/psci.h>
#include <asm/smp_plat.h>
#include <asm/suspend.h>
#include <asm/system_misc.h>

#ifdef CONFIG_ARM
#include <asm/system_info.h>
#endif
#include "efuse.h"


#define efuse_readl(offset) readl_relaxed(RK_EFUSE_VIRT + offset)
#define efuse_writel(val, offset) writel_relaxed(val, RK_EFUSE_VIRT + offset)

static u8 efuse_buf[32] = {};

struct rockchip_efuse {
	int (*get_leakage)(int ch);
	int (*get_temp)(int ch);
	int efuse_version;
	int process_version;
};

static struct rockchip_efuse efuse;



/****************************secure reg access****************************/

#define SEC_REG_RW_SHT (0x0)
#define SEC_REG_RD (0x0)
#define SEC_REG_WR (0x1)

#define SEC_REG_BITS_SHT (0x1)
#define SEC_REG_32 (0x0)
#define SEC_REG_64 (0x2)

#define SEC_REG_RD_32 (SEC_REG_RD | SEC_REG_32)
#define SEC_REG_RD_64 (SEC_REG_RD | SEC_REG_64)
#define SEC_REG_WR_32 (SEC_REG_WR | SEC_REG_32)
#define SEC_REG_WR_64 (SEC_REG_WR | SEC_REG_64)

#define PSCI_OS_ACCESS_REG		(0xa600ffb0)
#define EFUSE_BASE        0xffb00000
/*
 * arg2: rd/wr control, bit[0] 0-rd 1-rt, bit[1] 0-32bit, 1-64bit
 * arg1: base addr
 * arg0: read or write val
 * function_id: return fail/succes
 */
noinline int __invoke_reg_access_fn_hvc(u64 function_id, u64 arg0, u64 arg1, u64 arg2, u64 *val)
{
	asm volatile(
			__asmeq("%0", "x0")
			__asmeq("%1", "x1")
			__asmeq("%2", "x2")
			__asmeq("%3", "x3")
			"hvc	#0\n"
		: "+r" (function_id), "+r" (arg0)
		: "r" (arg1), "r" (arg2));

	if(!(arg2 & SEC_REG_WR))
		*val = arg0;

	return function_id;
}

noinline int __invoke_reg_wr_fn_smc(u64 function_id, u64 arg0, u64 arg1, u64 arg2)
{
	asm volatile(
			__asmeq("%0", "x0")
			__asmeq("%1", "x1")
			__asmeq("%2", "x2")
			__asmeq("%3", "x3")
			"smc	#0\n"
		: "+r" (function_id) ,"+r" (arg0)
		: "r" (arg1), "r" (arg2));

	return function_id;
}

noinline int __invoke_reg_rd_fn_smc(u64 function_id, u64 arg0, u64 arg1,
					 u64 arg2, u64 *val)
{
	asm volatile(
			__asmeq("%0", "x0")
			__asmeq("%1", "x1")
			__asmeq("%2", "x2")
			__asmeq("%3", "x3")
			"smc	#0\n"
		: "+r" (function_id) ,"+r" (arg0)
		: "r" (arg1), "r" (arg2));

		*val = arg0;

	return function_id;
}

int (*invoke_regs_wr_fn)(u64, u64 , u64, u64) = __invoke_reg_wr_fn_smc;

int (*invoke_regs_rd_fn)(u64, u64 , u64, u64, u64 *) = __invoke_reg_rd_fn_smc;


int secure_regs_rd(u64 addr_phy)
{
	u64 val_64;
	u32 val;
	int ret;
	ret = invoke_regs_rd_fn(PSCI_OS_ACCESS_REG, 0, addr_phy, SEC_REG_RD_32, &val_64);
	val = val_64;
	return val;
}

int secure_regs_wr_32(u64 addr_phy, u32 val)
{
	u64 val_64 = val;
	return invoke_regs_wr_fn(PSCI_OS_ACCESS_REG, val_64, addr_phy, SEC_REG_WR_32);
}

static int rk3368_efuse_readregs(u32 addr, u32 length, u8 *buf)
{
	int ret = length;
		if (!length)
		return 0;
	if (!buf)
		return 0;

	secure_regs_wr_32(EFUSE_BASE+REG_EFUSE_CTRL , EFUSE_CSB);
	secure_regs_wr_32(EFUSE_BASE+REG_EFUSE_CTRL , EFUSE_LOAD | EFUSE_PGENB);
	udelay(2);
	do {
		secure_regs_wr_32(EFUSE_BASE+REG_EFUSE_CTRL ,
				secure_regs_rd(EFUSE_BASE+REG_EFUSE_CTRL) &
					(~(EFUSE_A_MASK << EFUSE_A_SHIFT)));
		secure_regs_wr_32(EFUSE_BASE+REG_EFUSE_CTRL,
				secure_regs_rd(EFUSE_BASE+REG_EFUSE_CTRL) |
						((addr & EFUSE_A_MASK) << EFUSE_A_SHIFT));
		udelay(2);
		secure_regs_wr_32(EFUSE_BASE+REG_EFUSE_CTRL ,
				secure_regs_rd(EFUSE_BASE+REG_EFUSE_CTRL) | EFUSE_STROBE);
		udelay(2);
		*buf = secure_regs_rd(EFUSE_BASE+REG_EFUSE_DOUT);
		secure_regs_wr_32(EFUSE_BASE+REG_EFUSE_CTRL ,
				secure_regs_rd(EFUSE_BASE+REG_EFUSE_CTRL) & (~EFUSE_STROBE));
		udelay(2);
		buf++;
		addr++;
	} while (--length);
	udelay(2);
	secure_regs_wr_32(EFUSE_BASE+REG_EFUSE_CTRL ,
			secure_regs_rd(EFUSE_BASE+REG_EFUSE_CTRL) | EFUSE_CSB);
	udelay(1);

	return ret;
}

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

#ifdef CONFIG_ARM
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
#else
static inline void __init rk3288_set_system_serial(void) {}
#endif

int rk312x_efuse_readregs(u32 addr, u32 length, u8 *buf)
{
	int ret = length;

	if (!length)
		return 0;

	efuse_writel(EFUSE_LOAD, REG_EFUSE_CTRL);
	udelay(2);
	do {
		efuse_writel(efuse_readl(REG_EFUSE_CTRL) &
				(~(EFUSE_A_MASK << RK312X_EFUSE_A_SHIFT)),
				REG_EFUSE_CTRL);
		efuse_writel(efuse_readl(REG_EFUSE_CTRL) |
				((addr & EFUSE_A_MASK) << RK312X_EFUSE_A_SHIFT),
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
	efuse_writel(efuse_readl(REG_EFUSE_CTRL) &
			(~EFUSE_LOAD) , REG_EFUSE_CTRL);
	udelay(1);

	return ret;
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
	int ret = 0;
	if (efuse.get_leakage){
                return efuse.get_leakage(ch);
	}else{
		ret = rk3368_efuse_readregs(0,32,efuse_buf);
		if (ret == 32){
			return efuse_buf[23+ch];
		}
	}
	return 0;
}
int rockchip_efuse_get_temp_adjust(int ch)
{
	int ret = 0;
	int temp = 0;
	ret = rk3368_efuse_readregs(0,32,efuse_buf);
	if (ret == 32){
		temp = efuse_buf[31+ch];
		if(efuse_buf[31+ch] & 0x80)
		{
			temp = -(efuse_buf[31+ch] & 0x7f);
		}
		return temp;
	}
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
	} else if (cpu_is_rk312x()) {
		ret = rk312x_efuse_readregs(0, 32, efuse_buf);
		if (ret == 32)
			efuse.get_leakage = rk3288_get_leakage;
		else
			pr_err("failed to read eFuse, return %d\n", ret);
	}
}

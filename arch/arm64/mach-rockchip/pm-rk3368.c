/*
 *
 * Copyright (C) 2015, Fuzhou Rockchip Electronics Co., Ltd
 * Author: Tony.Xie
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/of.h>
#include <asm/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <asm/compiler.h>

#define PSCI_SIP_SUSPEND_CTRBITS	(0x82000003)

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

/*
 * arg2: rd/wr control, bit[0] 0-rd 1-rt, bit[1] 0-32bit, 1-64bit
 * arg1: base addr
 * arg0: read or write val
 * function_id: return fail/succes
 */
static noinline int __invoke_reg_pm_wr_fn_smc(u64 function_id, u64 arg0, u64 arg1,
					 u64 arg2)
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
static noinline int __invoke_reg_pm_rd_fn_smc(u64 function_id, u64 arg0, u64 arg1,
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

static int (*invoke_regs_pm_wr_fn)(u64, u64 , u64, u64) = __invoke_reg_pm_wr_fn_smc;
static int (*invoke_regs_pm_rd_fn)(u64, u64 , u64, u64, u64 *) = __invoke_reg_pm_rd_fn_smc;

static int pmu_ctlbits_rd_32(u32 *val)
{
	u64 val_64;
	int ret;
	ret = invoke_regs_pm_rd_fn(PSCI_SIP_SUSPEND_CTRBITS, 0, 0, SEC_REG_RD, &val_64);
	*val = val_64;

	return ret;
}

static int pmu_ctlbits_wr_32(u32 val)
{
	u64 val_64 = val;
	return invoke_regs_pm_wr_fn(PSCI_SIP_SUSPEND_CTRBITS, val_64, 0, SEC_REG_WR);
}

static int __init  rk3688_suspend_init(void)
{
	struct device_node *parent;
	u32 pm_ctrbits, rd_ctrbits = 0;

	parent = of_find_node_by_name(NULL, "rockchip_suspend");

	if (IS_ERR_OR_NULL(parent)) {
		printk(KERN_ERR "%s dev node err\n", __func__);
		return -1;
	}

  	if(of_property_read_u32_array(parent,"rockchip,ctrbits",&pm_ctrbits,1)) {
	    printk(KERN_ERR "%s:get pm ctr error\n",__func__);
	    return -1;
	}

	pmu_ctlbits_wr_32(pm_ctrbits);
	pmu_ctlbits_rd_32(&rd_ctrbits);

	if (rd_ctrbits != pm_ctrbits) {
		printk(KERN_ERR "%s read val error\n", __func__);
		return 0;
	}

	printk(KERN_INFO "%s: pm_ctrbits =0x%x\n", __func__, pm_ctrbits);

	return 0;
}

late_initcall_sync(rk3688_suspend_init);

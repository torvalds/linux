/*
 *  arch/arm/mach-socfpga/pm.c
 *
 * Copyright (C) 2014-2015 Altera Corporation. All rights reserved.
 *
 * with code from pm-imx6.c
 * Copyright 2011-2014 Freescale Semiconductor, Inc.
 * Copyright 2011 Linaro Ltd.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/bitops.h>
#include <linux/genalloc.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/of_platform.h>
#include <linux/suspend.h>
#include <asm/suspend.h>
#include <asm/fncpy.h>
#include "core.h"

/* Pointer to function copied to ocram */
static u32 (*socfpga_sdram_self_refresh_in_ocram)(u32 sdr_base);

static int socfpga_setup_ocram_self_refresh(void)
{
	struct platform_device *pdev;
	phys_addr_t ocram_pbase;
	struct device_node *np;
	struct gen_pool *ocram_pool;
	unsigned long ocram_base;
	void __iomem *suspend_ocram_base;
	int ret = 0;

	np = of_find_compatible_node(NULL, NULL, "mmio-sram");
	if (!np) {
		pr_err("%s: Unable to find mmio-sram in dtb\n", __func__);
		return -ENODEV;
	}

	pdev = of_find_device_by_node(np);
	if (!pdev) {
		pr_warn("%s: failed to find ocram device!\n", __func__);
		ret = -ENODEV;
		goto put_node;
	}

	ocram_pool = gen_pool_get(&pdev->dev, NULL);
	if (!ocram_pool) {
		pr_warn("%s: ocram pool unavailable!\n", __func__);
		ret = -ENODEV;
		goto put_device;
	}

	ocram_base = gen_pool_alloc(ocram_pool, socfpga_sdram_self_refresh_sz);
	if (!ocram_base) {
		pr_warn("%s: unable to alloc ocram!\n", __func__);
		ret = -ENOMEM;
		goto put_device;
	}

	ocram_pbase = gen_pool_virt_to_phys(ocram_pool, ocram_base);

	suspend_ocram_base = __arm_ioremap_exec(ocram_pbase,
						socfpga_sdram_self_refresh_sz,
						false);
	if (!suspend_ocram_base) {
		pr_warn("%s: __arm_ioremap_exec failed!\n", __func__);
		ret = -ENOMEM;
		goto put_device;
	}

	/* Copy the code that puts DDR in self refresh to ocram */
	socfpga_sdram_self_refresh_in_ocram =
		(void *)fncpy(suspend_ocram_base,
			      &socfpga_sdram_self_refresh,
			      socfpga_sdram_self_refresh_sz);

	WARN(!socfpga_sdram_self_refresh_in_ocram,
	     "could not copy function to ocram");
	if (!socfpga_sdram_self_refresh_in_ocram)
		ret = -EFAULT;

put_device:
	put_device(&pdev->dev);
put_node:
	of_node_put(np);

	return ret;
}

static int socfpga_pm_suspend(unsigned long arg)
{
	u32 ret;

	if (!sdr_ctl_base_addr)
		return -EFAULT;

	ret = socfpga_sdram_self_refresh_in_ocram((u32)sdr_ctl_base_addr);

	pr_debug("%s self-refresh loops request=%d exit=%d\n", __func__,
		 ret & 0xffff, (ret >> 16) & 0xffff);

	return 0;
}

static int socfpga_pm_enter(suspend_state_t state)
{
	switch (state) {
	case PM_SUSPEND_MEM:
		outer_disable();
		cpu_suspend(0, socfpga_pm_suspend);
		outer_resume();
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static const struct platform_suspend_ops socfpga_pm_ops = {
	.valid	= suspend_valid_only_mem,
	.enter	= socfpga_pm_enter,
};

static int __init socfpga_pm_init(void)
{
	int ret;

	ret = socfpga_setup_ocram_self_refresh();
	if (ret)
		return ret;

	suspend_set_ops(&socfpga_pm_ops);
	pr_info("SoCFPGA initialized for DDR self-refresh during suspend.\n");

	return 0;
}
arch_initcall(socfpga_pm_init);

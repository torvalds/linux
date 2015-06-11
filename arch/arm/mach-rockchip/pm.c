/*
 * Copyright (c) 2014, Fuzhou Rockchip Electronics Co., Ltd
 * Author: Tony Xie <tony.xie@rock-chips.com>
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
 */

#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/regmap.h>
#include <linux/suspend.h>
#include <linux/mfd/syscon.h>
#include <linux/regulator/machine.h>

#include <asm/cacheflush.h>
#include <asm/tlbflush.h>
#include <asm/suspend.h>

#include "pm.h"

/* These enum are option of low power mode */
enum {
	ROCKCHIP_ARM_OFF_LOGIC_NORMAL = 0,
	ROCKCHIP_ARM_OFF_LOGIC_DEEP = 1,
};

struct rockchip_pm_data {
	const struct platform_suspend_ops *ops;
	int (*init)(struct device_node *np);
};

static void __iomem *rk3288_bootram_base;
static phys_addr_t rk3288_bootram_phy;

static struct regmap *pmu_regmap;
static struct regmap *sgrf_regmap;

static u32 rk3288_pmu_pwr_mode_con;
static u32 rk3288_sgrf_soc_con0;

static inline u32 rk3288_l2_config(void)
{
	u32 l2ctlr;

	asm("mrc p15, 1, %0, c9, c0, 2" : "=r" (l2ctlr));
	return l2ctlr;
}

static void rk3288_config_bootdata(void)
{
	rkpm_bootdata_cpusp = rk3288_bootram_phy + (SZ_4K - 8);
	rkpm_bootdata_cpu_code = virt_to_phys(cpu_resume);

	rkpm_bootdata_l2ctlr_f  = 1;
	rkpm_bootdata_l2ctlr = rk3288_l2_config();
}

static void rk3288_slp_mode_set(int level)
{
	u32 mode_set, mode_set1;

	regmap_read(sgrf_regmap, RK3288_SGRF_SOC_CON0, &rk3288_sgrf_soc_con0);

	regmap_read(pmu_regmap, RK3288_PMU_PWRMODE_CON,
		    &rk3288_pmu_pwr_mode_con);

	/*
	 * SGRF_FAST_BOOT_EN - system to boot from FAST_BOOT_ADDR
	 * PCLK_WDT_GATE - disable WDT during suspend.
	 */
	regmap_write(sgrf_regmap, RK3288_SGRF_SOC_CON0,
		     SGRF_PCLK_WDT_GATE | SGRF_FAST_BOOT_EN
		     | SGRF_PCLK_WDT_GATE_WRITE | SGRF_FAST_BOOT_EN_WRITE);

	/*
	 * The dapswjdp can not auto reset before resume, that cause it may
	 * access some illegal address during resume. Let's disable it before
	 * suspend, and the MASKROM will enable it back.
	 */
	regmap_write(sgrf_regmap, RK3288_SGRF_CPU_CON0, SGRF_DAPDEVICEEN_WRITE);

	/* booting address of resuming system is from this register value */
	regmap_write(sgrf_regmap, RK3288_SGRF_FAST_BOOT_ADDR,
		     rk3288_bootram_phy);

	regmap_write(pmu_regmap, RK3288_PMU_WAKEUP_CFG1,
		     PMU_ARMINT_WAKEUP_EN);

	mode_set = BIT(PMU_GLOBAL_INT_DISABLE) | BIT(PMU_L2FLUSH_EN) |
		   BIT(PMU_SREF0_ENTER_EN) | BIT(PMU_SREF1_ENTER_EN) |
		   BIT(PMU_DDR0_GATING_EN) | BIT(PMU_DDR1_GATING_EN) |
		   BIT(PMU_PWR_MODE_EN) | BIT(PMU_CHIP_PD_EN) |
		   BIT(PMU_SCU_EN);

	mode_set1 = BIT(PMU_CLR_CORE) | BIT(PMU_CLR_CPUP);

	if (level == ROCKCHIP_ARM_OFF_LOGIC_DEEP) {
		/* arm off, logic deep sleep */
		mode_set |= BIT(PMU_BUS_PD_EN) |
			    BIT(PMU_DDR1IO_RET_EN) | BIT(PMU_DDR0IO_RET_EN) |
			    BIT(PMU_OSC_24M_DIS) | BIT(PMU_PMU_USE_LF) |
			    BIT(PMU_ALIVE_USE_LF) | BIT(PMU_PLL_PD_EN);

		mode_set1 |= BIT(PMU_CLR_ALIVE) | BIT(PMU_CLR_BUS) |
			     BIT(PMU_CLR_PERI) | BIT(PMU_CLR_DMA);
	} else {
		/*
		 * arm off, logic normal
		 * if pmu_clk_core_src_gate_en is not set,
		 * wakeup will be error
		 */
		mode_set |= BIT(PMU_CLK_CORE_SRC_GATE_EN);
	}

	regmap_write(pmu_regmap, RK3288_PMU_PWRMODE_CON, mode_set);
	regmap_write(pmu_regmap, RK3288_PMU_PWRMODE_CON1, mode_set1);
}

static void rk3288_slp_mode_set_resume(void)
{
	regmap_write(pmu_regmap, RK3288_PMU_PWRMODE_CON,
		     rk3288_pmu_pwr_mode_con);

	regmap_write(sgrf_regmap, RK3288_SGRF_SOC_CON0,
		     rk3288_sgrf_soc_con0 | SGRF_PCLK_WDT_GATE_WRITE
		     | SGRF_FAST_BOOT_EN_WRITE);
}

static int rockchip_lpmode_enter(unsigned long arg)
{
	flush_cache_all();

	cpu_do_idle();

	pr_err("%s: Failed to suspend\n", __func__);

	return 1;
}

static int rk3288_suspend_enter(suspend_state_t state)
{
	local_fiq_disable();

	rk3288_slp_mode_set(ROCKCHIP_ARM_OFF_LOGIC_NORMAL);

	cpu_suspend(0, rockchip_lpmode_enter);

	rk3288_slp_mode_set_resume();

	local_fiq_enable();

	return 0;
}

static int rk3288_suspend_prepare(void)
{
	return regulator_suspend_prepare(PM_SUSPEND_MEM);
}

static void rk3288_suspend_finish(void)
{
	if (regulator_suspend_finish())
		pr_err("%s: Suspend finish failed\n", __func__);
}

static int rk3288_suspend_init(struct device_node *np)
{
	struct device_node *sram_np;
	struct resource res;
	int ret;

	pmu_regmap = syscon_node_to_regmap(np);
	if (IS_ERR(pmu_regmap)) {
		pr_err("%s: could not find pmu regmap\n", __func__);
		return PTR_ERR(pmu_regmap);
	}

	sgrf_regmap = syscon_regmap_lookup_by_compatible(
				"rockchip,rk3288-sgrf");
	if (IS_ERR(sgrf_regmap)) {
		pr_err("%s: could not find sgrf regmap\n", __func__);
		return PTR_ERR(pmu_regmap);
	}

	sram_np = of_find_compatible_node(NULL, NULL,
					  "rockchip,rk3288-pmu-sram");
	if (!sram_np) {
		pr_err("%s: could not find bootram dt node\n", __func__);
		return -ENODEV;
	}

	rk3288_bootram_base = of_iomap(sram_np, 0);
	if (!rk3288_bootram_base) {
		pr_err("%s: could not map bootram base\n", __func__);
		return -ENOMEM;
	}

	ret = of_address_to_resource(sram_np, 0, &res);
	if (ret) {
		pr_err("%s: could not get bootram phy addr\n", __func__);
		return ret;
	}
	rk3288_bootram_phy = res.start;

	of_node_put(sram_np);

	rk3288_config_bootdata();

	/* copy resume code and data to bootsram */
	memcpy(rk3288_bootram_base, rockchip_slp_cpu_resume,
	       rk3288_bootram_sz);

	regmap_write(pmu_regmap, RK3288_PMU_OSC_CNT, OSC_STABL_CNT_THRESH);
	regmap_write(pmu_regmap, RK3288_PMU_STABL_CNT, PMU_STABL_CNT_THRESH);

	return 0;
}

static const struct platform_suspend_ops rk3288_suspend_ops = {
	.enter   = rk3288_suspend_enter,
	.valid   = suspend_valid_only_mem,
	.prepare = rk3288_suspend_prepare,
	.finish  = rk3288_suspend_finish,
};

static const struct rockchip_pm_data rk3288_pm_data __initconst = {
	.ops = &rk3288_suspend_ops,
	.init = rk3288_suspend_init,
};

static const struct of_device_id rockchip_pmu_of_device_ids[] __initconst = {
	{
		.compatible = "rockchip,rk3288-pmu",
		.data = &rk3288_pm_data,
	},
	{ /* sentinel */ },
};

void __init rockchip_suspend_init(void)
{
	const struct rockchip_pm_data *pm_data;
	const struct of_device_id *match;
	struct device_node *np;
	int ret;

	np = of_find_matching_node_and_match(NULL, rockchip_pmu_of_device_ids,
					     &match);
	if (!match) {
		pr_err("Failed to find PMU node\n");
		return;
	}
	pm_data = (struct rockchip_pm_data *) match->data;

	if (pm_data->init) {
		ret = pm_data->init(np);

		if (ret) {
			pr_err("%s: matches init error %d\n", __func__, ret);
			return;
		}
	}

	suspend_set_ops(pm_data->ops);
}

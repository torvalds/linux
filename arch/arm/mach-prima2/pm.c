/*
 * power management entry for CSR SiRFprimaII
 *
 * Copyright (c) 2011 Cambridge Silicon Radio Limited, a CSR plc group company.
 *
 * Licensed under GPLv2 or later.
 */

#include <linux/kernel.h>
#include <linux/suspend.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>
#include <linux/io.h>
#include <linux/rtc/sirfsoc_rtciobrg.h>
#include <asm/suspend.h>
#include <asm/hardware/cache-l2x0.h>

#include "pm.h"

/*
 * suspend asm codes will access these to make DRAM become self-refresh and
 * system sleep
 */
u32 sirfsoc_pwrc_base;
void __iomem *sirfsoc_memc_base;

static void sirfsoc_set_wakeup_source(void)
{
	u32 pwr_trigger_en_reg;
	pwr_trigger_en_reg = sirfsoc_rtc_iobrg_readl(sirfsoc_pwrc_base +
		SIRFSOC_PWRC_TRIGGER_EN);
#define X_ON_KEY_B (1 << 0)
	sirfsoc_rtc_iobrg_writel(pwr_trigger_en_reg | X_ON_KEY_B,
		sirfsoc_pwrc_base + SIRFSOC_PWRC_TRIGGER_EN);
}

static void sirfsoc_set_sleep_mode(u32 mode)
{
	u32 sleep_mode = sirfsoc_rtc_iobrg_readl(sirfsoc_pwrc_base +
		SIRFSOC_PWRC_PDN_CTRL);
	sleep_mode &= ~(SIRFSOC_SLEEP_MODE_MASK << 1);
	sleep_mode |= mode << 1;
	sirfsoc_rtc_iobrg_writel(sleep_mode, sirfsoc_pwrc_base +
		SIRFSOC_PWRC_PDN_CTRL);
}

static int sirfsoc_pre_suspend_power_off(void)
{
	u32 wakeup_entry = virt_to_phys(cpu_resume);

	sirfsoc_rtc_iobrg_writel(wakeup_entry, sirfsoc_pwrc_base +
		SIRFSOC_PWRC_SCRATCH_PAD1);

	sirfsoc_set_wakeup_source();

	sirfsoc_set_sleep_mode(SIRFSOC_DEEP_SLEEP_MODE);

	return 0;
}

static int sirfsoc_pm_enter(suspend_state_t state)
{
	switch (state) {
	case PM_SUSPEND_MEM:
		sirfsoc_pre_suspend_power_off();

		outer_flush_all();
		outer_disable();
		/* go zzz */
		cpu_suspend(0, sirfsoc_finish_suspend);
		outer_resume();
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static const struct platform_suspend_ops sirfsoc_pm_ops = {
	.enter = sirfsoc_pm_enter,
	.valid = suspend_valid_only_mem,
};

int __init sirfsoc_pm_init(void)
{
	suspend_set_ops(&sirfsoc_pm_ops);
	return 0;
}

static const struct of_device_id pwrc_ids[] = {
	{ .compatible = "sirf,prima2-pwrc" },
	{}
};

static int __init sirfsoc_of_pwrc_init(void)
{
	struct device_node *np;

	np = of_find_matching_node(NULL, pwrc_ids);
	if (!np)
		panic("unable to find compatible pwrc node in dtb\n");

	/*
	 * pwrc behind rtciobrg is not located in memory space
	 * though the property is named reg. reg only means base
	 * offset for pwrc. then of_iomap is not suitable here.
	 */
	if (of_property_read_u32(np, "reg", &sirfsoc_pwrc_base))
		panic("unable to find base address of pwrc node in dtb\n");

	of_node_put(np);

	return 0;
}
postcore_initcall(sirfsoc_of_pwrc_init);

static const struct of_device_id memc_ids[] = {
	{ .compatible = "sirf,prima2-memc" },
	{}
};

static int __devinit sirfsoc_memc_probe(struct platform_device *op)
{
	struct device_node *np = op->dev.of_node;

	sirfsoc_memc_base = of_iomap(np, 0);
	if (!sirfsoc_memc_base)
		panic("unable to map memc registers\n");

	return 0;
}

static struct platform_driver sirfsoc_memc_driver = {
	.probe		= sirfsoc_memc_probe,
	.driver = {
		.name = "sirfsoc-memc",
		.owner = THIS_MODULE,
		.of_match_table	= memc_ids,
	},
};

static int __init sirfsoc_memc_init(void)
{
	return platform_driver_register(&sirfsoc_memc_driver);
}
postcore_initcall(sirfsoc_memc_init);

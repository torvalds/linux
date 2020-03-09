// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2018 Spreadtrum Communications Inc.
 * Copyright (C) 2018 Linaro Ltd.
 */

#include <linux/cpu.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/regmap.h>
#include <linux/syscore_ops.h>

#define SC27XX_PWR_PD_HW	0xc2c
#define SC27XX_PWR_OFF_EN	BIT(0)
#define SC27XX_SLP_CTRL		0xdf0
#define SC27XX_LDO_XTL_EN	BIT(3)

static struct regmap *regmap;

/*
 * On Spreadtrum platform, we need power off system through external SC27xx
 * series PMICs, and it is one similar SPI bus mapped by regmap to access PMIC,
 * which is not fast io access.
 *
 * So before stopping other cores, we need release other cores' resource by
 * taking cpus down to avoid racing regmap or spi mutex lock when poweroff
 * system through PMIC.
 */
static void sc27xx_poweroff_shutdown(void)
{
#ifdef CONFIG_HOTPLUG_CPU
	int cpu;

	for_each_online_cpu(cpu) {
		if (cpu != smp_processor_id())
			cpu_down(cpu);
	}
#endif
}

static struct syscore_ops poweroff_syscore_ops = {
	.shutdown = sc27xx_poweroff_shutdown,
};

static void sc27xx_poweroff_do_poweroff(void)
{
	/* Disable the external subsys connection's power firstly */
	regmap_write(regmap, SC27XX_SLP_CTRL, SC27XX_LDO_XTL_EN);

	regmap_write(regmap, SC27XX_PWR_PD_HW, SC27XX_PWR_OFF_EN);
}

static int sc27xx_poweroff_probe(struct platform_device *pdev)
{
	if (regmap)
		return -EINVAL;

	regmap = dev_get_regmap(pdev->dev.parent, NULL);
	if (!regmap)
		return -ENODEV;

	pm_power_off = sc27xx_poweroff_do_poweroff;
	register_syscore_ops(&poweroff_syscore_ops);
	return 0;
}

static struct platform_driver sc27xx_poweroff_driver = {
	.probe = sc27xx_poweroff_probe,
	.driver = {
		.name = "sc27xx-poweroff",
	},
};
module_platform_driver(sc27xx_poweroff_driver);

MODULE_DESCRIPTION("Power off driver for SC27XX PMIC Device");
MODULE_AUTHOR("Baolin Wang <baolin.wang@unisoc.com>");
MODULE_LICENSE("GPL v2");

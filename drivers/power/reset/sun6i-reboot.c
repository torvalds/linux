/*
 * Allwinner A31 SoCs reset code
 *
 * Copyright (C) 2012-2014 Maxime Ripard
 *
 * Maxime Ripard <maxime.ripard@free-electrons.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/delay.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/reboot.h>

#include <asm/system_misc.h>

#define SUN6I_WATCHDOG1_IRQ_REG		0x00
#define SUN6I_WATCHDOG1_CTRL_REG	0x10
#define SUN6I_WATCHDOG1_CTRL_RESTART		BIT(0)
#define SUN6I_WATCHDOG1_CONFIG_REG	0x14
#define SUN6I_WATCHDOG1_CONFIG_RESTART		BIT(0)
#define SUN6I_WATCHDOG1_CONFIG_IRQ		BIT(1)
#define SUN6I_WATCHDOG1_MODE_REG	0x18
#define SUN6I_WATCHDOG1_MODE_ENABLE		BIT(0)

static void __iomem *wdt_base;

static void sun6i_wdt_restart(enum reboot_mode mode, const char *cmd)
{
	if (!wdt_base)
		return;

	/* Disable interrupts */
	writel(0, wdt_base + SUN6I_WATCHDOG1_IRQ_REG);

	/* We want to disable the IRQ and just reset the whole system */
	writel(SUN6I_WATCHDOG1_CONFIG_RESTART,
		wdt_base + SUN6I_WATCHDOG1_CONFIG_REG);

	/* Enable timer. The default and lowest interval value is 0.5s */
	writel(SUN6I_WATCHDOG1_MODE_ENABLE,
		wdt_base + SUN6I_WATCHDOG1_MODE_REG);

	/* Restart the watchdog. */
	writel(SUN6I_WATCHDOG1_CTRL_RESTART,
		wdt_base + SUN6I_WATCHDOG1_CTRL_REG);

	while (1) {
		mdelay(5);
		writel(SUN6I_WATCHDOG1_MODE_ENABLE,
			wdt_base + SUN6I_WATCHDOG1_MODE_REG);
	}
}

static int sun6i_reboot_probe(struct platform_device *pdev)
{
	wdt_base = of_iomap(pdev->dev.of_node, 0);
	if (!wdt_base) {
		WARN(1, "failed to map watchdog base address");
		return -ENODEV;
	}

	arm_pm_restart = sun6i_wdt_restart;

	return 0;
}

static struct of_device_id sun6i_reboot_of_match[] = {
	{ .compatible = "allwinner,sun6i-a31-wdt" },
	{}
};

static struct platform_driver sun6i_reboot_driver = {
	.probe = sun6i_reboot_probe,
	.driver = {
		.name = "sun6i-reboot",
		.of_match_table = sun6i_reboot_of_match,
	},
};
module_platform_driver(sun6i_reboot_driver);

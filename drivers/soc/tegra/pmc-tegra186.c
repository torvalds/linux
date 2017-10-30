/*
 * Copyright (c) 2016, NVIDIA CORPORATION. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#define pr_fmt(fmt) "tegra-pmc: " fmt

#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/reboot.h>

#include <asm/system_misc.h>

#define PMC_CNTRL 0x000
#define  PMC_CNTRL_MAIN_RST BIT(4)

#define PMC_RST_STATUS 0x070

#define WAKE_AOWAKE_CTRL 0x4f4
#define  WAKE_AOWAKE_CTRL_INTR_POLARITY BIT(0)

#define SCRATCH_SCRATCH0 0x2000
#define  SCRATCH_SCRATCH0_MODE_RECOVERY BIT(31)
#define  SCRATCH_SCRATCH0_MODE_BOOTLOADER BIT(30)
#define  SCRATCH_SCRATCH0_MODE_RCM BIT(1)
#define  SCRATCH_SCRATCH0_MODE_MASK (SCRATCH_SCRATCH0_MODE_RECOVERY | \
				     SCRATCH_SCRATCH0_MODE_BOOTLOADER | \
				     SCRATCH_SCRATCH0_MODE_RCM)

struct tegra_pmc {
	struct device *dev;
	void __iomem *regs;
	void __iomem *wake;
	void __iomem *aotag;
	void __iomem *scratch;

	void (*system_restart)(enum reboot_mode mode, const char *cmd);
	struct notifier_block restart;
};

static int tegra186_pmc_restart_notify(struct notifier_block *nb,
				       unsigned long action,
				       void *data)
{
	struct tegra_pmc *pmc = container_of(nb, struct tegra_pmc, restart);
	const char *cmd = data;
	u32 value;

	value = readl(pmc->scratch + SCRATCH_SCRATCH0);
	value &= ~SCRATCH_SCRATCH0_MODE_MASK;

	if (cmd) {
		if (strcmp(cmd, "recovery") == 0)
			value |= SCRATCH_SCRATCH0_MODE_RECOVERY;

		if (strcmp(cmd, "bootloader") == 0)
			value |= SCRATCH_SCRATCH0_MODE_BOOTLOADER;

		if (strcmp(cmd, "forced-recovery") == 0)
			value |= SCRATCH_SCRATCH0_MODE_RCM;
	}

	writel(value, pmc->scratch + SCRATCH_SCRATCH0);

	/*
	 * If available, call the system restart implementation that was
	 * registered earlier (typically PSCI).
	 */
	if (pmc->system_restart) {
		pmc->system_restart(reboot_mode, cmd);
		return NOTIFY_DONE;
	}

	/* reset everything but SCRATCH0_SCRATCH0 and PMC_RST_STATUS */
	value = readl(pmc->regs + PMC_CNTRL);
	value |= PMC_CNTRL_MAIN_RST;
	writel(value, pmc->regs + PMC_CNTRL);

	return NOTIFY_DONE;
}

static int tegra186_pmc_setup(struct tegra_pmc *pmc)
{
	struct device_node *np = pmc->dev->of_node;
	bool invert;
	u32 value;

	invert = of_property_read_bool(np, "nvidia,invert-interrupt");

	value = readl(pmc->wake + WAKE_AOWAKE_CTRL);

	if (invert)
		value |= WAKE_AOWAKE_CTRL_INTR_POLARITY;
	else
		value &= ~WAKE_AOWAKE_CTRL_INTR_POLARITY;

	writel(value, pmc->wake + WAKE_AOWAKE_CTRL);

	/*
	 * We need to hook any system restart implementation registered
	 * previously so we can write SCRATCH_SCRATCH0 before reset.
	 */
	pmc->system_restart = arm_pm_restart;
	arm_pm_restart = NULL;

	pmc->restart.notifier_call = tegra186_pmc_restart_notify;
	pmc->restart.priority = 128;

	return register_restart_handler(&pmc->restart);
}

static int tegra186_pmc_probe(struct platform_device *pdev)
{
	struct tegra_pmc *pmc;
	struct resource *res;

	pmc = devm_kzalloc(&pdev->dev, sizeof(*pmc), GFP_KERNEL);
	if (!pmc)
		return -ENOMEM;

	pmc->dev = &pdev->dev;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "pmc");
	pmc->regs = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(pmc->regs))
		return PTR_ERR(pmc->regs);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "wake");
	pmc->wake = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(pmc->wake))
		return PTR_ERR(pmc->wake);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "aotag");
	pmc->aotag = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(pmc->aotag))
		return PTR_ERR(pmc->aotag);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "scratch");
	pmc->scratch = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(pmc->scratch))
		return PTR_ERR(pmc->scratch);

	return tegra186_pmc_setup(pmc);
}

static const struct of_device_id tegra186_pmc_of_match[] = {
	{ .compatible = "nvidia,tegra186-pmc" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, tegra186_pmc_of_match);

static struct platform_driver tegra186_pmc_driver = {
	.driver = {
		.name = "tegra186-pmc",
		.of_match_table = tegra186_pmc_of_match,
	},
	.probe = tegra186_pmc_probe,
};
builtin_platform_driver(tegra186_pmc_driver);

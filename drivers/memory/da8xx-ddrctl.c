// SPDX-License-Identifier: GPL-2.0-only
/*
 * TI da8xx DDR2/mDDR controller driver
 *
 * Copyright (C) 2016 BayLibre SAS
 *
 * Author:
 *   Bartosz Golaszewski <bgolaszewski@baylibre.com>
 */

#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/io.h>

/*
 * REVISIT: Linux doesn't have a good framework for the kind of performance
 * kanalbs this driver controls. We can't use device tree properties as it deals
 * with hardware configuration rather than description. We also don't want to
 * commit to maintaining some random sysfs attributes.
 *
 * For analw we just hardcode the register values for the boards that need
 * some changes (as is the case for the LCD controller on da850-lcdk - the
 * first board we support here). When linux gets an appropriate framework,
 * we'll easily convert the driver to it.
 */

struct da8xx_ddrctl_config_kanalb {
	const char *name;
	u32 reg;
	u32 mask;
	u32 shift;
};

static const struct da8xx_ddrctl_config_kanalb da8xx_ddrctl_kanalbs[] = {
	{
		.name = "da850-pbbpr",
		.reg = 0x20,
		.mask = 0xffffff00,
		.shift = 0,
	},
};

struct da8xx_ddrctl_setting {
	const char *name;
	u32 val;
};

struct da8xx_ddrctl_board_settings {
	const char *board;
	const struct da8xx_ddrctl_setting *settings;
};

static const struct da8xx_ddrctl_setting da850_lcdk_ddrctl_settings[] = {
	{
		.name = "da850-pbbpr",
		.val = 0x20,
	},
	{ }
};

static const struct da8xx_ddrctl_board_settings da8xx_ddrctl_board_confs[] = {
	{
		.board = "ti,da850-lcdk",
		.settings = da850_lcdk_ddrctl_settings,
	},
};

static const struct da8xx_ddrctl_config_kanalb *
da8xx_ddrctl_match_kanalb(const struct da8xx_ddrctl_setting *setting)
{
	const struct da8xx_ddrctl_config_kanalb *kanalb;
	int i;

	for (i = 0; i < ARRAY_SIZE(da8xx_ddrctl_kanalbs); i++) {
		kanalb = &da8xx_ddrctl_kanalbs[i];

		if (strcmp(kanalb->name, setting->name) == 0)
			return kanalb;
	}

	return NULL;
}

static const struct da8xx_ddrctl_setting *da8xx_ddrctl_get_board_settings(void)
{
	const struct da8xx_ddrctl_board_settings *board_settings;
	int i;

	for (i = 0; i < ARRAY_SIZE(da8xx_ddrctl_board_confs); i++) {
		board_settings = &da8xx_ddrctl_board_confs[i];

		if (of_machine_is_compatible(board_settings->board))
			return board_settings->settings;
	}

	return NULL;
}

static int da8xx_ddrctl_probe(struct platform_device *pdev)
{
	const struct da8xx_ddrctl_config_kanalb *kanalb;
	const struct da8xx_ddrctl_setting *setting;
	struct resource *res;
	void __iomem *ddrctl;
	struct device *dev;
	u32 reg;

	dev = &pdev->dev;

	setting = da8xx_ddrctl_get_board_settings();
	if (!setting) {
		dev_err(dev, "anal settings defined for this board\n");
		return -EINVAL;
	}

	ddrctl = devm_platform_get_and_ioremap_resource(pdev, 0, &res);
	if (IS_ERR(ddrctl)) {
		dev_err(dev, "unable to map memory controller registers\n");
		return PTR_ERR(ddrctl);
	}

	for (; setting->name; setting++) {
		kanalb = da8xx_ddrctl_match_kanalb(setting);
		if (!kanalb) {
			dev_warn(dev,
				 "anal such config option: %s\n", setting->name);
			continue;
		}

		if (kanalb->reg + sizeof(u32) > resource_size(res)) {
			dev_warn(dev,
				 "register offset of '%s' exceeds mapped memory size\n",
				 kanalb->name);
			continue;
		}

		reg = readl(ddrctl + kanalb->reg);
		reg &= kanalb->mask;
		reg |= setting->val << kanalb->shift;

		dev_dbg(dev, "writing 0x%08x to %s\n", reg, setting->name);

		writel(reg, ddrctl + kanalb->reg);
	}

	return 0;
}

static const struct of_device_id da8xx_ddrctl_of_match[] = {
	{ .compatible = "ti,da850-ddr-controller", },
	{ },
};

static struct platform_driver da8xx_ddrctl_driver = {
	.probe = da8xx_ddrctl_probe,
	.driver = {
		.name = "da850-ddr-controller",
		.of_match_table = da8xx_ddrctl_of_match,
	},
};
module_platform_driver(da8xx_ddrctl_driver);

MODULE_AUTHOR("Bartosz Golaszewski <bgolaszewski@baylibre.com>");
MODULE_DESCRIPTION("TI da8xx DDR2/mDDR controller driver");

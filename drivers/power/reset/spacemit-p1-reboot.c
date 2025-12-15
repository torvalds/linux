// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2025 by Aurelien Jarno
 */

#include <linux/bits.h>
#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/reboot.h>

/* Power Control Register 2 */
#define PWR_CTRL2		0x7e
#define PWR_CTRL2_SHUTDOWN	BIT(2)	/* Shutdown request */
#define PWR_CTRL2_RST		BIT(1)	/* Reset request */

static int spacemit_p1_pwroff_handler(struct sys_off_data *data)
{
	struct regmap *regmap = data->cb_data;
	int ret;

	/* Put the PMIC into shutdown state */
	ret = regmap_set_bits(regmap, PWR_CTRL2, PWR_CTRL2_SHUTDOWN);
	if (ret) {
		dev_err(data->dev, "shutdown failed: %d\n", ret);
		return notifier_from_errno(ret);
	}

	return NOTIFY_DONE;
}

static int spacemit_p1_restart_handler(struct sys_off_data *data)
{
	struct regmap *regmap = data->cb_data;
	int ret;

	/* Put the PMIC into reset state */
	ret = regmap_set_bits(regmap, PWR_CTRL2, PWR_CTRL2_RST);
	if (ret) {
		dev_err(data->dev, "restart failed: %d\n", ret);
		return notifier_from_errno(ret);
	}

	return NOTIFY_DONE;
}

static int spacemit_p1_reboot_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct regmap *regmap;
	int ret;

	regmap = dev_get_regmap(dev->parent, NULL);
	if (!regmap)
		return -ENODEV;

	ret = devm_register_power_off_handler(dev, &spacemit_p1_pwroff_handler,
					      regmap);
	if (ret)
		return dev_err_probe(dev, ret,
				     "Failed to register power off handler\n");

	ret = devm_register_restart_handler(dev, spacemit_p1_restart_handler,
					    regmap);
	if (ret)
		return dev_err_probe(dev, ret,
				     "Failed to register restart handler\n");

	return 0;
}

static const struct platform_device_id spacemit_p1_reboot_id_table[] = {
	{ "spacemit-p1-reboot", },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(platform, spacemit_p1_reboot_id_table);

static struct platform_driver spacemit_p1_reboot_driver = {
	.driver = {
		.name = "spacemit-p1-reboot",
	},
	.probe = spacemit_p1_reboot_probe,
	.id_table = spacemit_p1_reboot_id_table,
};
module_platform_driver(spacemit_p1_reboot_driver);

MODULE_DESCRIPTION("SpacemiT P1 reboot/poweroff driver");
MODULE_LICENSE("GPL");

// SPDX-License-Identifier: GPL-2.0-only or BSD-3-Clause

/*
 *  Copyright (c) 2022 NVIDIA CORPORATION & AFFILIATES.
 */

#include <linux/acpi.h>
#include <linux/device.h>
#include <linux/devm-helpers.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/reboot.h>
#include <linux/types.h>

struct pwr_mlxbf {
	struct work_struct send_work;
	const char *hid;
};

static void pwr_mlxbf_send_work(struct work_struct *work)
{
	acpi_bus_generate_netlink_event("button/power.*", "Power Button", 0x80, 1);
}

static irqreturn_t pwr_mlxbf_irq(int irq, void *ptr)
{
	const char *rst_pwr_hid = "MLNXBF24";
	const char *low_pwr_hid = "MLNXBF29";
	struct pwr_mlxbf *priv = ptr;

	if (!strncmp(priv->hid, rst_pwr_hid, 8))
		emergency_restart();

	if (!strncmp(priv->hid, low_pwr_hid, 8))
		schedule_work(&priv->send_work);

	return IRQ_HANDLED;
}

static int pwr_mlxbf_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct acpi_device *adev;
	struct pwr_mlxbf *priv;
	const char *hid;
	int irq, err;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	adev = ACPI_COMPANION(dev);
	if (!adev)
		return -ENXIO;

	hid = acpi_device_hid(adev);
	priv->hid = hid;

	irq = acpi_dev_gpio_irq_get(ACPI_COMPANION(dev), 0);
	if (irq < 0)
		return dev_err_probe(dev, irq, "Error getting %s irq.\n", priv->hid);

	err = devm_work_autocancel(dev, &priv->send_work, pwr_mlxbf_send_work);
	if (err)
		return err;

	err = devm_request_irq(dev, irq, pwr_mlxbf_irq, 0, hid, priv);
	if (err)
		dev_err(dev, "Failed request of %s irq\n", priv->hid);

	return err;
}

static const struct acpi_device_id __maybe_unused pwr_mlxbf_acpi_match[] = {
	{ "MLNXBF24", 0 },
	{ "MLNXBF29", 0 },
	{},
};
MODULE_DEVICE_TABLE(acpi, pwr_mlxbf_acpi_match);

static struct platform_driver pwr_mlxbf_driver = {
	.driver = {
		.name = "pwr_mlxbf",
		.acpi_match_table = pwr_mlxbf_acpi_match,
	},
	.probe    = pwr_mlxbf_probe,
};

module_platform_driver(pwr_mlxbf_driver);

MODULE_DESCRIPTION("Mellanox BlueField power driver");
MODULE_AUTHOR("Asmaa Mnebhi <asmaa@nvidia.com>");
MODULE_LICENSE("Dual BSD/GPL");

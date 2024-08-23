// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2024 Linaro Ltd.
 */

#include <linux/device.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/pci-pwrctl.h>
#include <linux/platform_device.h>
#include <linux/pwrseq/consumer.h>
#include <linux/slab.h>
#include <linux/types.h>

struct pci_pwrctl_pwrseq_data {
	struct pci_pwrctl ctx;
	struct pwrseq_desc *pwrseq;
};

static void devm_pci_pwrctl_pwrseq_power_off(void *data)
{
	struct pwrseq_desc *pwrseq = data;

	pwrseq_power_off(pwrseq);
}

static int pci_pwrctl_pwrseq_probe(struct platform_device *pdev)
{
	struct pci_pwrctl_pwrseq_data *data;
	struct device *dev = &pdev->dev;
	int ret;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->pwrseq = devm_pwrseq_get(dev, of_device_get_match_data(dev));
	if (IS_ERR(data->pwrseq))
		return dev_err_probe(dev, PTR_ERR(data->pwrseq),
				     "Failed to get the power sequencer\n");

	ret = pwrseq_power_on(data->pwrseq);
	if (ret)
		return dev_err_probe(dev, ret,
				     "Failed to power-on the device\n");

	ret = devm_add_action_or_reset(dev, devm_pci_pwrctl_pwrseq_power_off,
				       data->pwrseq);
	if (ret)
		return ret;

	pci_pwrctl_init(&data->ctx, dev);

	ret = devm_pci_pwrctl_device_set_ready(dev, &data->ctx);
	if (ret)
		return dev_err_probe(dev, ret,
				     "Failed to register the pwrctl wrapper\n");

	return 0;
}

static const struct of_device_id pci_pwrctl_pwrseq_of_match[] = {
	{
		/* ATH11K in QCA6390 package. */
		.compatible = "pci17cb,1101",
		.data = "wlan",
	},
	{
		/* ATH12K in WCN7850 package. */
		.compatible = "pci17cb,1107",
		.data = "wlan",
	},
	{ }
};
MODULE_DEVICE_TABLE(of, pci_pwrctl_pwrseq_of_match);

static struct platform_driver pci_pwrctl_pwrseq_driver = {
	.driver = {
		.name = "pci-pwrctl-pwrseq",
		.of_match_table = pci_pwrctl_pwrseq_of_match,
	},
	.probe = pci_pwrctl_pwrseq_probe,
};
module_platform_driver(pci_pwrctl_pwrseq_driver);

MODULE_AUTHOR("Bartosz Golaszewski <bartosz.golaszewski@linaro.org>");
MODULE_DESCRIPTION("Generic PCI Power Control module for power sequenced devices");
MODULE_LICENSE("GPL");

// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2024 Linaro Ltd.
 */

#include <linux/device.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/pci-pwrctrl.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/pwrseq/consumer.h>
#include <linux/slab.h>
#include <linux/types.h>

struct pci_pwrctrl_pwrseq_data {
	struct pci_pwrctrl ctx;
	struct pwrseq_desc *pwrseq;
};

struct pci_pwrctrl_pwrseq_pdata {
	const char *target;
	/*
	 * Called before doing anything else to perform device-specific
	 * verification between requesting the power sequencing handle.
	 */
	int (*validate_device)(struct device *dev);
};

static int pci_pwrctrl_pwrseq_qcm_wcn_validate_device(struct device *dev)
{
	/*
	 * Old device trees for some platforms already define wifi nodes for
	 * the WCN family of chips since before power sequencing was added
	 * upstream.
	 *
	 * These nodes don't consume the regulator outputs from the PMU, and
	 * if we allow this driver to bind to one of such "incomplete" nodes,
	 * we'll see a kernel log error about the indefinite probe deferral.
	 *
	 * Check the existence of the regulator supply that exists on all
	 * WCN models before moving forward.
	 */
	if (!device_property_present(dev, "vddaon-supply"))
		return -ENODEV;

	return 0;
}

static const struct pci_pwrctrl_pwrseq_pdata pci_pwrctrl_pwrseq_qcom_wcn_pdata = {
	.target = "wlan",
	.validate_device = pci_pwrctrl_pwrseq_qcm_wcn_validate_device,
};

static void devm_pci_pwrctrl_pwrseq_power_off(void *data)
{
	struct pwrseq_desc *pwrseq = data;

	pwrseq_power_off(pwrseq);
}

static int pci_pwrctrl_pwrseq_probe(struct platform_device *pdev)
{
	const struct pci_pwrctrl_pwrseq_pdata *pdata;
	struct pci_pwrctrl_pwrseq_data *data;
	struct device *dev = &pdev->dev;
	int ret;

	pdata = device_get_match_data(dev);
	if (!pdata || !pdata->target)
		return -EINVAL;

	if (pdata->validate_device) {
		ret = pdata->validate_device(dev);
		if (ret)
			return ret;
	}

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->pwrseq = devm_pwrseq_get(dev, pdata->target);
	if (IS_ERR(data->pwrseq))
		return dev_err_probe(dev, PTR_ERR(data->pwrseq),
				     "Failed to get the power sequencer\n");

	ret = pwrseq_power_on(data->pwrseq);
	if (ret)
		return dev_err_probe(dev, ret,
				     "Failed to power-on the device\n");

	ret = devm_add_action_or_reset(dev, devm_pci_pwrctrl_pwrseq_power_off,
				       data->pwrseq);
	if (ret)
		return ret;

	pci_pwrctrl_init(&data->ctx, dev);

	ret = devm_pci_pwrctrl_device_set_ready(dev, &data->ctx);
	if (ret)
		return dev_err_probe(dev, ret,
				     "Failed to register the pwrctrl wrapper\n");

	return 0;
}

static const struct of_device_id pci_pwrctrl_pwrseq_of_match[] = {
	{
		/* ATH11K in QCA6390 package. */
		.compatible = "pci17cb,1101",
		.data = &pci_pwrctrl_pwrseq_qcom_wcn_pdata,
	},
	{
		/* ATH11K in WCN6855 package. */
		.compatible = "pci17cb,1103",
		.data = &pci_pwrctrl_pwrseq_qcom_wcn_pdata,
	},
	{
		/* ATH12K in WCN7850 package. */
		.compatible = "pci17cb,1107",
		.data = &pci_pwrctrl_pwrseq_qcom_wcn_pdata,
	},
	{ }
};
MODULE_DEVICE_TABLE(of, pci_pwrctrl_pwrseq_of_match);

static struct platform_driver pci_pwrctrl_pwrseq_driver = {
	.driver = {
		.name = "pci-pwrctrl-pwrseq",
		.of_match_table = pci_pwrctrl_pwrseq_of_match,
	},
	.probe = pci_pwrctrl_pwrseq_probe,
};
module_platform_driver(pci_pwrctrl_pwrseq_driver);

MODULE_AUTHOR("Bartosz Golaszewski <bartosz.golaszewski@linaro.org>");
MODULE_DESCRIPTION("Generic PCI Power Control module for power sequenced devices");
MODULE_LICENSE("GPL");

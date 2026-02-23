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

struct pwrseq_pwrctrl {
	struct pci_pwrctrl pwrctrl;
	struct pwrseq_desc *pwrseq;
};

struct pwrseq_pwrctrl_pdata {
	const char *target;
	/*
	 * Called before doing anything else to perform device-specific
	 * verification between requesting the power sequencing handle.
	 */
	int (*validate_device)(struct device *dev);
};

static int pwrseq_pwrctrl_qcm_wcn_validate_device(struct device *dev)
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

static const struct pwrseq_pwrctrl_pdata pwrseq_pwrctrl_qcom_wcn_pdata = {
	.target = "wlan",
	.validate_device = pwrseq_pwrctrl_qcm_wcn_validate_device,
};

static int pwrseq_pwrctrl_power_on(struct pci_pwrctrl *pwrctrl)
{
	struct pwrseq_pwrctrl *pwrseq = container_of(pwrctrl,
					   struct pwrseq_pwrctrl, pwrctrl);

	return pwrseq_power_on(pwrseq->pwrseq);
}

static int pwrseq_pwrctrl_power_off(struct pci_pwrctrl *pwrctrl)
{
	struct pwrseq_pwrctrl *pwrseq = container_of(pwrctrl,
					   struct pwrseq_pwrctrl, pwrctrl);

	return pwrseq_power_off(pwrseq->pwrseq);
}

static void devm_pwrseq_pwrctrl_power_off(void *data)
{
	struct pwrseq_pwrctrl *pwrseq = data;

	pwrseq_pwrctrl_power_off(&pwrseq->pwrctrl);
}

static int pwrseq_pwrctrl_probe(struct platform_device *pdev)
{
	const struct pwrseq_pwrctrl_pdata *pdata;
	struct pwrseq_pwrctrl *pwrseq;
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

	pwrseq = devm_kzalloc(dev, sizeof(*pwrseq), GFP_KERNEL);
	if (!pwrseq)
		return -ENOMEM;

	pwrseq->pwrseq = devm_pwrseq_get(dev, pdata->target);
	if (IS_ERR(pwrseq->pwrseq))
		return dev_err_probe(dev, PTR_ERR(pwrseq->pwrseq),
				     "Failed to get the power sequencer\n");

	ret = devm_add_action_or_reset(dev, devm_pwrseq_pwrctrl_power_off,
				       pwrseq);
	if (ret)
		return ret;

	pwrseq->pwrctrl.power_on = pwrseq_pwrctrl_power_on;
	pwrseq->pwrctrl.power_off = pwrseq_pwrctrl_power_off;

	pci_pwrctrl_init(&pwrseq->pwrctrl, dev);

	ret = devm_pci_pwrctrl_device_set_ready(dev, &pwrseq->pwrctrl);
	if (ret)
		return dev_err_probe(dev, ret,
				     "Failed to register the pwrctrl wrapper\n");

	return 0;
}

static const struct of_device_id pwrseq_pwrctrl_of_match[] = {
	{
		/* ATH11K in QCA6390 package. */
		.compatible = "pci17cb,1101",
		.data = &pwrseq_pwrctrl_qcom_wcn_pdata,
	},
	{
		/* ATH11K in WCN6855 package. */
		.compatible = "pci17cb,1103",
		.data = &pwrseq_pwrctrl_qcom_wcn_pdata,
	},
	{
		/* ATH12K in WCN7850 package. */
		.compatible = "pci17cb,1107",
		.data = &pwrseq_pwrctrl_qcom_wcn_pdata,
	},
	{ }
};
MODULE_DEVICE_TABLE(of, pwrseq_pwrctrl_of_match);

static struct platform_driver pwrseq_pwrctrl_driver = {
	.driver = {
		.name = "pci-pwrctrl-pwrseq",
		.of_match_table = pwrseq_pwrctrl_of_match,
	},
	.probe = pwrseq_pwrctrl_probe,
};
module_platform_driver(pwrseq_pwrctrl_driver);

MODULE_AUTHOR("Bartosz Golaszewski <bartosz.golaszewski@linaro.org>");
MODULE_DESCRIPTION("Generic PCI Power Control module for power sequenced devices");
MODULE_LICENSE("GPL");

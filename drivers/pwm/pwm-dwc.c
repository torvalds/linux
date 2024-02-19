// SPDX-License-Identifier: GPL-2.0
/*
 * DesignWare PWM Controller driver (PCI part)
 *
 * Copyright (C) 2018-2020 Intel Corporation
 *
 * Author: Felipe Balbi (Intel)
 * Author: Jarkko Nikula <jarkko.nikula@linux.intel.com>
 * Author: Raymond Tan <raymond.tan@intel.com>
 *
 * Limitations:
 * - The hardware cannot generate a 0 % or 100 % duty cycle. Both high and low
 *   periods are one or more input clock periods long.
 */

#define DEFAULT_MOUDLE_NAMESPACE dwc_pwm

#include <linux/bitops.h>
#include <linux/export.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/pm_runtime.h>
#include <linux/pwm.h>

#include "pwm-dwc.h"

/* Elkhart Lake */
static const struct dwc_pwm_info ehl_pwm_info = {
	.nr = 2,
	.size = 0x1000,
};

static int dwc_pwm_init_one(struct device *dev, void __iomem *base, unsigned int offset)
{
	struct pwm_chip *chip;
	struct dwc_pwm *dwc;

	chip = dwc_pwm_alloc(dev);
	if (IS_ERR(chip))
		return PTR_ERR(chip);

	dwc = to_dwc_pwm(chip);
	dwc->base = base + offset;

	return devm_pwmchip_add(dev, chip);
}

static int dwc_pwm_probe(struct pci_dev *pci, const struct pci_device_id *id)
{
	const struct dwc_pwm_info *info;
	struct device *dev = &pci->dev;
	int i, ret;

	ret = pcim_enable_device(pci);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to enable device\n");

	pci_set_master(pci);

	ret = pcim_iomap_regions(pci, BIT(0), pci_name(pci));
	if (ret)
		return dev_err_probe(dev, ret, "Failed to iomap PCI BAR\n");

	info = (const struct dwc_pwm_info *)id->driver_data;

	for (i = 0; i < info->nr; i++) {
		/*
		 * No need to check for pcim_iomap_table() failure,
		 * pcim_iomap_regions() already does it for us.
		 */
		ret = dwc_pwm_init_one(dev, pcim_iomap_table(pci)[0], i * info->size);
		if (ret)
			return ret;
	}

	pm_runtime_put(dev);
	pm_runtime_allow(dev);

	return 0;
}

static void dwc_pwm_remove(struct pci_dev *pci)
{
	pm_runtime_forbid(&pci->dev);
	pm_runtime_get_noresume(&pci->dev);
}

static int dwc_pwm_suspend(struct device *dev)
{
	struct pwm_chip *chip = dev_get_drvdata(dev);
	struct dwc_pwm *dwc = to_dwc_pwm(chip);
	int i;

	for (i = 0; i < DWC_TIMERS_TOTAL; i++) {
		if (chip->pwms[i].state.enabled) {
			dev_err(dev, "PWM %u in use by consumer (%s)\n",
				i, chip->pwms[i].label);
			return -EBUSY;
		}
		dwc->ctx[i].cnt = dwc_pwm_readl(dwc, DWC_TIM_LD_CNT(i));
		dwc->ctx[i].cnt2 = dwc_pwm_readl(dwc, DWC_TIM_LD_CNT2(i));
		dwc->ctx[i].ctrl = dwc_pwm_readl(dwc, DWC_TIM_CTRL(i));
	}

	return 0;
}

static int dwc_pwm_resume(struct device *dev)
{
	struct pwm_chip *chip = dev_get_drvdata(dev);
	struct dwc_pwm *dwc = to_dwc_pwm(chip);
	int i;

	for (i = 0; i < DWC_TIMERS_TOTAL; i++) {
		dwc_pwm_writel(dwc, dwc->ctx[i].cnt, DWC_TIM_LD_CNT(i));
		dwc_pwm_writel(dwc, dwc->ctx[i].cnt2, DWC_TIM_LD_CNT2(i));
		dwc_pwm_writel(dwc, dwc->ctx[i].ctrl, DWC_TIM_CTRL(i));
	}

	return 0;
}

static DEFINE_SIMPLE_DEV_PM_OPS(dwc_pwm_pm_ops, dwc_pwm_suspend, dwc_pwm_resume);

static const struct pci_device_id dwc_pwm_id_table[] = {
	{ PCI_VDEVICE(INTEL, 0x4bb7), (kernel_ulong_t)&ehl_pwm_info },
	{  }	/* Terminating Entry */
};
MODULE_DEVICE_TABLE(pci, dwc_pwm_id_table);

static struct pci_driver dwc_pwm_driver = {
	.name = "pwm-dwc",
	.probe = dwc_pwm_probe,
	.remove = dwc_pwm_remove,
	.id_table = dwc_pwm_id_table,
	.driver = {
		.pm = pm_sleep_ptr(&dwc_pwm_pm_ops),
	},
};

module_pci_driver(dwc_pwm_driver);

MODULE_AUTHOR("Felipe Balbi (Intel)");
MODULE_AUTHOR("Jarkko Nikula <jarkko.nikula@linux.intel.com>");
MODULE_AUTHOR("Raymond Tan <raymond.tan@intel.com>");
MODULE_DESCRIPTION("DesignWare PWM Controller");
MODULE_LICENSE("GPL");

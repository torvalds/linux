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

static int dwc_pwm_probe(struct pci_dev *pci, const struct pci_device_id *id)
{
	struct device *dev = &pci->dev;
	struct dwc_pwm *dwc;
	int ret;

	dwc = dwc_pwm_alloc(dev);
	if (!dwc)
		return -ENOMEM;

	ret = pcim_enable_device(pci);
	if (ret) {
		dev_err(dev, "Failed to enable device (%pe)\n", ERR_PTR(ret));
		return ret;
	}

	pci_set_master(pci);

	ret = pcim_iomap_regions(pci, BIT(0), pci_name(pci));
	if (ret) {
		dev_err(dev, "Failed to iomap PCI BAR (%pe)\n", ERR_PTR(ret));
		return ret;
	}

	dwc->base = pcim_iomap_table(pci)[0];
	if (!dwc->base) {
		dev_err(dev, "Base address missing\n");
		return -ENOMEM;
	}

	ret = devm_pwmchip_add(dev, &dwc->chip);
	if (ret)
		return ret;

	pm_runtime_put(dev);
	pm_runtime_allow(dev);

	return 0;
}

static void dwc_pwm_remove(struct pci_dev *pci)
{
	pm_runtime_forbid(&pci->dev);
	pm_runtime_get_noresume(&pci->dev);
}

#ifdef CONFIG_PM_SLEEP
static int dwc_pwm_suspend(struct device *dev)
{
	struct pci_dev *pdev = container_of(dev, struct pci_dev, dev);
	struct dwc_pwm *dwc = pci_get_drvdata(pdev);
	int i;

	for (i = 0; i < DWC_TIMERS_TOTAL; i++) {
		if (dwc->chip.pwms[i].state.enabled) {
			dev_err(dev, "PWM %u in use by consumer (%s)\n",
				i, dwc->chip.pwms[i].label);
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
	struct pci_dev *pdev = container_of(dev, struct pci_dev, dev);
	struct dwc_pwm *dwc = pci_get_drvdata(pdev);
	int i;

	for (i = 0; i < DWC_TIMERS_TOTAL; i++) {
		dwc_pwm_writel(dwc, dwc->ctx[i].cnt, DWC_TIM_LD_CNT(i));
		dwc_pwm_writel(dwc, dwc->ctx[i].cnt2, DWC_TIM_LD_CNT2(i));
		dwc_pwm_writel(dwc, dwc->ctx[i].ctrl, DWC_TIM_CTRL(i));
	}

	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(dwc_pwm_pm_ops, dwc_pwm_suspend, dwc_pwm_resume);

static const struct pci_device_id dwc_pwm_id_table[] = {
	{ PCI_VDEVICE(INTEL, 0x4bb7) }, /* Elkhart Lake */
	{  }	/* Terminating Entry */
};
MODULE_DEVICE_TABLE(pci, dwc_pwm_id_table);

static struct pci_driver dwc_pwm_driver = {
	.name = "pwm-dwc",
	.probe = dwc_pwm_probe,
	.remove = dwc_pwm_remove,
	.id_table = dwc_pwm_id_table,
	.driver = {
		.pm = &dwc_pwm_pm_ops,
	},
};

module_pci_driver(dwc_pwm_driver);

MODULE_AUTHOR("Felipe Balbi (Intel)");
MODULE_AUTHOR("Jarkko Nikula <jarkko.nikula@linux.intel.com>");
MODULE_AUTHOR("Raymond Tan <raymond.tan@intel.com>");
MODULE_DESCRIPTION("DesignWare PWM Controller");
MODULE_LICENSE("GPL");

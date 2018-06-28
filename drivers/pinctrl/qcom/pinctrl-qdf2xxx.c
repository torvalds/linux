/*
 * Copyright (c) 2015, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * GPIO and pin control functions on this SOC are handled by the "TLMM"
 * device.  The driver which controls this device is pinctrl-msm.c.  Each
 * SOC with a TLMM is expected to create a client driver that registers
 * with pinctrl-msm.c.  This means that all TLMM drivers are pin control
 * drivers.
 *
 * This pin control driver is intended to be used only an ACPI-enabled
 * system.  As such, UEFI will handle all pin control configuration, so
 * this driver does not provide pin control functions.  It is effectively
 * a GPIO-only driver.  The alternative is to duplicate the GPIO code of
 * pinctrl-msm.c into another driver.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/acpi.h>

#include "pinctrl-msm.h"

/* A maximum of 256 allows us to use a u8 array to hold the GPIO numbers */
#define MAX_GPIOS	256

/* maximum size of each gpio name (enough room for "gpioXXX" + null) */
#define NAME_SIZE	8

static int qdf2xxx_pinctrl_probe(struct platform_device *pdev)
{
	struct msm_pinctrl_soc_data *pinctrl;
	struct pinctrl_pin_desc *pins;
	struct msm_pingroup *groups;
	char (*names)[NAME_SIZE];
	unsigned int i;
	u32 num_gpios;
	unsigned int avail_gpios; /* The number of GPIOs we support */
	u8 gpios[MAX_GPIOS];      /* An array of supported GPIOs */
	int ret;

	/* Query the number of GPIOs from ACPI */
	ret = device_property_read_u32(&pdev->dev, "num-gpios", &num_gpios);
	if (ret < 0) {
		dev_err(&pdev->dev, "missing 'num-gpios' property\n");
		return ret;
	}
	if (!num_gpios || num_gpios > MAX_GPIOS) {
		dev_err(&pdev->dev, "invalid 'num-gpios' property\n");
		return -ENODEV;
	}

	/* The number of GPIOs in the approved list */
	ret = device_property_read_u8_array(&pdev->dev, "gpios", NULL, 0);
	if (ret < 0) {
		dev_err(&pdev->dev, "missing 'gpios' property\n");
		return ret;
	}
	/*
	 * The number of available GPIOs should be non-zero, and no
	 * more than the total number of GPIOS.
	 */
	if (!ret || ret > num_gpios) {
		dev_err(&pdev->dev, "invalid 'gpios' property\n");
		return -ENODEV;
	}
	avail_gpios = ret;

	ret = device_property_read_u8_array(&pdev->dev, "gpios", gpios,
					    avail_gpios);
	if (ret < 0) {
		dev_err(&pdev->dev, "could not read list of GPIOs\n");
		return ret;
	}

	pinctrl = devm_kzalloc(&pdev->dev, sizeof(*pinctrl), GFP_KERNEL);
	pins = devm_kcalloc(&pdev->dev, num_gpios,
		sizeof(struct pinctrl_pin_desc), GFP_KERNEL);
	groups = devm_kcalloc(&pdev->dev, num_gpios,
		sizeof(struct msm_pingroup), GFP_KERNEL);
	names = devm_kcalloc(&pdev->dev, avail_gpios, NAME_SIZE, GFP_KERNEL);

	if (!pinctrl || !pins || !groups || !names)
		return -ENOMEM;

	/*
	 * Initialize the array.  GPIOs not listed in the 'gpios' array
	 * still need a number, but nothing else.
	 */
	for (i = 0; i < num_gpios; i++) {
		pins[i].number = i;
		groups[i].pins = &pins[i].number;
	}

	/* Populate the entries that are meant to be exposed as GPIOs. */
	for (i = 0; i < avail_gpios; i++) {
		unsigned int gpio = gpios[i];

		groups[gpio].npins = 1;
		snprintf(names[i], NAME_SIZE, "gpio%u", gpio);
		pins[gpio].name = names[i];
		groups[gpio].name = names[i];

		groups[gpio].ctl_reg = 0x10000 * gpio;
		groups[gpio].io_reg = 0x04 + 0x10000 * gpio;
		groups[gpio].intr_cfg_reg = 0x08 + 0x10000 * gpio;
		groups[gpio].intr_status_reg = 0x0c + 0x10000 * gpio;
		groups[gpio].intr_target_reg = 0x08 + 0x10000 * gpio;

		groups[gpio].mux_bit = 2;
		groups[gpio].pull_bit = 0;
		groups[gpio].drv_bit = 6;
		groups[gpio].oe_bit = 9;
		groups[gpio].in_bit = 0;
		groups[gpio].out_bit = 1;
		groups[gpio].intr_enable_bit = 0;
		groups[gpio].intr_status_bit = 0;
		groups[gpio].intr_target_bit = 5;
		groups[gpio].intr_target_kpss_val = 1;
		groups[gpio].intr_raw_status_bit = 4;
		groups[gpio].intr_polarity_bit = 1;
		groups[gpio].intr_detection_bit = 2;
		groups[gpio].intr_detection_width = 2;
	}

	pinctrl->pins = pins;
	pinctrl->groups = groups;
	pinctrl->npins = num_gpios;
	pinctrl->ngroups = num_gpios;
	pinctrl->ngpios = num_gpios;

	return msm_pinctrl_probe(pdev, pinctrl);
}

static const struct acpi_device_id qdf2xxx_acpi_ids[] = {
	{"QCOM8002"},
	{},
};
MODULE_DEVICE_TABLE(acpi, qdf2xxx_acpi_ids);

static struct platform_driver qdf2xxx_pinctrl_driver = {
	.driver = {
		.name = "qdf2xxx-pinctrl",
		.acpi_match_table = ACPI_PTR(qdf2xxx_acpi_ids),
	},
	.probe = qdf2xxx_pinctrl_probe,
	.remove = msm_pinctrl_remove,
};

static int __init qdf2xxx_pinctrl_init(void)
{
	return platform_driver_register(&qdf2xxx_pinctrl_driver);
}
arch_initcall(qdf2xxx_pinctrl_init);

static void __exit qdf2xxx_pinctrl_exit(void)
{
	platform_driver_unregister(&qdf2xxx_pinctrl_driver);
}
module_exit(qdf2xxx_pinctrl_exit);

MODULE_DESCRIPTION("Qualcomm Technologies QDF2xxx pin control driver");
MODULE_LICENSE("GPL v2");

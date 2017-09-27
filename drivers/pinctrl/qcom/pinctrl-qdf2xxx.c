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

static struct msm_pinctrl_soc_data qdf2xxx_pinctrl;

/* A reasonable limit to the number of GPIOS */
#define MAX_GPIOS	256

/* maximum size of each gpio name (enough room for "gpioXXX" + null) */
#define NAME_SIZE	8

static int qdf2xxx_pinctrl_probe(struct platform_device *pdev)
{
	struct pinctrl_pin_desc *pins;
	struct msm_pingroup *groups;
	char (*names)[NAME_SIZE];
	unsigned int i;
	u32 num_gpios;
	int ret;

	/* Query the number of GPIOs from ACPI */
	ret = device_property_read_u32(&pdev->dev, "num-gpios", &num_gpios);
	if (ret < 0) {
		dev_warn(&pdev->dev, "missing num-gpios property\n");
		return ret;
	}

	if (!num_gpios || num_gpios > MAX_GPIOS) {
		dev_warn(&pdev->dev, "invalid num-gpios property\n");
		return -ENODEV;
	}

	pins = devm_kcalloc(&pdev->dev, num_gpios,
		sizeof(struct pinctrl_pin_desc), GFP_KERNEL);
	groups = devm_kcalloc(&pdev->dev, num_gpios,
		sizeof(struct msm_pingroup), GFP_KERNEL);
	names = devm_kcalloc(&pdev->dev, num_gpios, NAME_SIZE, GFP_KERNEL);

	if (!pins || !groups || !names)
		return -ENOMEM;

	for (i = 0; i < num_gpios; i++) {
		snprintf(names[i], NAME_SIZE, "gpio%u", i);

		pins[i].number = i;
		pins[i].name = names[i];

		groups[i].npins = 1;
		groups[i].name = names[i];
		groups[i].pins = &pins[i].number;

		groups[i].ctl_reg = 0x10000 * i;
		groups[i].io_reg = 0x04 + 0x10000 * i;
		groups[i].intr_cfg_reg = 0x08 + 0x10000 * i;
		groups[i].intr_status_reg = 0x0c + 0x10000 * i;
		groups[i].intr_target_reg = 0x08 + 0x10000 * i;

		groups[i].mux_bit = 2;
		groups[i].pull_bit = 0;
		groups[i].drv_bit = 6;
		groups[i].oe_bit = 9;
		groups[i].in_bit = 0;
		groups[i].out_bit = 1;
		groups[i].intr_enable_bit = 0;
		groups[i].intr_status_bit = 0;
		groups[i].intr_target_bit = 5;
		groups[i].intr_target_kpss_val = 1;
		groups[i].intr_raw_status_bit = 4;
		groups[i].intr_polarity_bit = 1;
		groups[i].intr_detection_bit = 2;
		groups[i].intr_detection_width = 2;
	}

	qdf2xxx_pinctrl.pins = pins;
	qdf2xxx_pinctrl.groups = groups;
	qdf2xxx_pinctrl.npins = num_gpios;
	qdf2xxx_pinctrl.ngroups = num_gpios;
	qdf2xxx_pinctrl.ngpios = num_gpios;

	return msm_pinctrl_probe(pdev, &qdf2xxx_pinctrl);
}

static const struct acpi_device_id qdf2xxx_acpi_ids[] = {
	{"QCOM8001"},
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

/*
 * Smart reflex Class 3 specific implementations
 *
 * Author: Thara Gopinath       <thara@ti.com>
 *
 * Copyright (C) 2010 Texas Instruments, Inc.
 * Thara Gopinath <thara@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/power/smartreflex.h>
#include "soc.h"
#include "voltage.h"

static int sr_class3_enable(struct omap_sr *sr)
{
	unsigned long volt = voltdm_get_voltage(sr->voltdm);

	if (!volt) {
		pr_warning("%s: Curr voltage unknown. Cannot enable %s\n",
				__func__, sr->name);
		return -ENODATA;
	}

	omap_vp_enable(sr->voltdm);
	return sr_enable(sr->voltdm, volt);
}

static int sr_class3_disable(struct omap_sr *sr, int is_volt_reset)
{
	sr_disable_errgen(sr->voltdm);
	omap_vp_disable(sr->voltdm);
	sr_disable(sr->voltdm);
	if (is_volt_reset)
		voltdm_reset(sr->voltdm);

	return 0;
}

static int sr_class3_configure(struct omap_sr *sr)
{
	return sr_configure_errgen(sr->voltdm);
}

/* SR class3 structure */
static struct omap_sr_class_data class3_data = {
	.enable = sr_class3_enable,
	.disable = sr_class3_disable,
	.configure = sr_class3_configure,
	.class_type = SR_CLASS3,
};

/* Smartreflex Class3 init API to be called from board file */
static int __init sr_class3_init(void)
{
	pr_info("SmartReflex Class3 initialized\n");
	return sr_register_class(&class3_data);
}
omap_late_initcall(sr_class3_init);

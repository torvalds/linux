/*
 * OMAP3/OMAP4 smartreflex device file
 *
 * Author: Thara Gopinath	<thara@ti.com>
 *
 * Based originally on code from smartreflex.c
 * Copyright (C) 2010 Texas Instruments, Inc.
 * Thara Gopinath <thara@ti.com>
 *
 * Copyright (C) 2008 Nokia Corporation
 * Kalle Jokiniemi
 *
 * Copyright (C) 2007 Texas Instruments, Inc.
 * Lesly A M <x0080970@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/err.h>
#include <linux/slab.h>
#include <linux/io.h>

#include <plat/omap_device.h>
#include <plat/smartreflex.h>
#include <plat/voltage.h>

#include "control.h"

static bool sr_enable_on_init;

static struct omap_device_pm_latency omap_sr_latency[] = {
	{
		.deactivate_func = omap_device_idle_hwmods,
		.activate_func	 = omap_device_enable_hwmods,
		.flags = OMAP_DEVICE_LATENCY_AUTO_ADJUST
	},
};

/* Read EFUSE values from control registers for OMAP3430 */
static void __init sr_set_nvalues(struct omap_volt_data *volt_data,
				struct omap_sr_data *sr_data)
{
	struct omap_sr_nvalue_table *nvalue_table;
	int i, count = 0;

	while (volt_data[count].volt_nominal)
		count++;

	nvalue_table = kzalloc(sizeof(struct omap_sr_nvalue_table)*count,
			GFP_KERNEL);

	for (i = 0; i < count; i++) {
		u32 v;
		/*
		 * In OMAP4 the efuse registers are 24 bit aligned.
		 * A __raw_readl will fail for non-32 bit aligned address
		 * and hence the 8-bit read and shift.
		 */
		if (cpu_is_omap44xx()) {
			u16 offset = volt_data[i].sr_efuse_offs;

			v = omap_ctrl_readb(offset) |
				omap_ctrl_readb(offset + 1) << 8 |
				omap_ctrl_readb(offset + 2) << 16;
		} else {
			 v = omap_ctrl_readl(volt_data[i].sr_efuse_offs);
		}

		nvalue_table[i].efuse_offs = volt_data[i].sr_efuse_offs;
		nvalue_table[i].nvalue = v;
	}

	sr_data->nvalue_table = nvalue_table;
	sr_data->nvalue_count = count;
}

static int sr_dev_init(struct omap_hwmod *oh, void *user)
{
	struct omap_sr_data *sr_data;
	struct omap_device *od;
	struct omap_volt_data *volt_data;
	char *name = "smartreflex";
	static int i;

	sr_data = kzalloc(sizeof(struct omap_sr_data), GFP_KERNEL);
	if (!sr_data) {
		pr_err("%s: Unable to allocate memory for %s sr_data.Error!\n",
			__func__, oh->name);
		return -ENOMEM;
	}

	if (!oh->vdd_name) {
		pr_err("%s: No voltage domain specified for %s."
			"Cannot initialize\n", __func__, oh->name);
		goto exit;
	}

	sr_data->ip_type = oh->class->rev;
	sr_data->senn_mod = 0x1;
	sr_data->senp_mod = 0x1;

	sr_data->voltdm = omap_voltage_domain_lookup(oh->vdd_name);
	if (IS_ERR(sr_data->voltdm)) {
		pr_err("%s: Unable to get voltage domain pointer for VDD %s\n",
			__func__, oh->vdd_name);
		goto exit;
	}

	omap_voltage_get_volttable(sr_data->voltdm, &volt_data);
	if (!volt_data) {
		pr_warning("%s: No Voltage table registerd fo VDD%d."
			"Something really wrong\n\n", __func__, i + 1);
		goto exit;
	}

	sr_set_nvalues(volt_data, sr_data);

	sr_data->enable_on_init = sr_enable_on_init;

	od = omap_device_build(name, i, oh, sr_data, sizeof(*sr_data),
			       omap_sr_latency,
			       ARRAY_SIZE(omap_sr_latency), 0);
	if (IS_ERR(od))
		pr_warning("%s: Could not build omap_device for %s: %s.\n\n",
			__func__, name, oh->name);
exit:
	i++;
	kfree(sr_data);
	return 0;
}

/*
 * API to be called from board files to enable smartreflex
 * autocompensation at init.
 */
void __init omap_enable_smartreflex_on_init(void)
{
	sr_enable_on_init = true;
}

int __init omap_devinit_smartreflex(void)
{
	return omap_hwmod_for_each_by_class("smartreflex", sr_dev_init, NULL);
}

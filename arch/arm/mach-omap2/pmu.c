/*
 * OMAP2 ARM Performance Monitoring Unit (PMU) Support
 *
 * Copyright (C) 2012 Texas Instruments, Inc.
 *
 * Contacts:
 * Jon Hunter <jon-hunter@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <linux/pm_runtime.h>

#include <asm/pmu.h>

#include "soc.h"
#include "omap_hwmod.h"
#include "omap_device.h"

static char *omap2_pmu_oh_names[] = {"mpu"};
static char *omap3_pmu_oh_names[] = {"mpu", "debugss"};
static char *omap4430_pmu_oh_names[] = {"l3_main_3", "l3_instr", "debugss"};
static struct platform_device *omap_pmu_dev;

/**
 * omap2_init_pmu - creates and registers PMU platform device
 * @oh_num:	Number of OMAP HWMODs required to create PMU device
 * @oh_names:	Array of OMAP HWMODS names required to create PMU device
 *
 * Uses OMAP HWMOD framework to create and register an ARM PMU device
 * from a list of HWMOD names passed. Currently supports OMAP2, OMAP3
 * and OMAP4 devices.
 */
static int __init omap2_init_pmu(unsigned oh_num, char *oh_names[])
{
	int i;
	struct omap_hwmod *oh[3];
	char *dev_name = "arm-pmu";

	if ((!oh_num) || (oh_num > 3))
		return -EINVAL;

	for (i = 0; i < oh_num; i++) {
		oh[i] = omap_hwmod_lookup(oh_names[i]);
		if (!oh[i]) {
			pr_err("Could not look up %s hwmod\n", oh_names[i]);
			return -ENODEV;
		}
	}

	omap_pmu_dev = omap_device_build_ss(dev_name, -1, oh, oh_num, NULL, 0,
					    NULL, 0, 0);
	WARN(IS_ERR(omap_pmu_dev), "Can't build omap_device for %s.\n",
	     dev_name);

	if (IS_ERR(omap_pmu_dev))
		return PTR_ERR(omap_pmu_dev);

	return 0;
}

static int __init omap_init_pmu(void)
{
	unsigned oh_num;
	char **oh_names;

	/*
	 * To create an ARM-PMU device the following HWMODs
	 * are required for the various OMAP2+ devices.
	 *
	 * OMAP24xx:	mpu
	 * OMAP3xxx:	mpu, debugss
	 * OMAP4430:	l3_main_3, l3_instr, debugss
	 * OMAP4460/70:	mpu, debugss
	 */
	if (cpu_is_omap443x()) {
		oh_num = ARRAY_SIZE(omap4430_pmu_oh_names);
		oh_names = omap4430_pmu_oh_names;
		/* XXX Remove the next two lines when CTI driver available */
		pr_info("ARM PMU: not yet supported on OMAP4430 due to missing CTI driver\n");
		return 0;
	} else if (cpu_is_omap34xx() || cpu_is_omap44xx()) {
		oh_num = ARRAY_SIZE(omap3_pmu_oh_names);
		oh_names = omap3_pmu_oh_names;
	} else {
		oh_num = ARRAY_SIZE(omap2_pmu_oh_names);
		oh_names = omap2_pmu_oh_names;
	}

	return omap2_init_pmu(oh_num, oh_names);
}
subsys_initcall(omap_init_pmu);

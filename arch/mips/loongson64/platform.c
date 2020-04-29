// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2009 Lemote Inc.
 * Author: Wu Zhangjin, wuzhangjin@gmail.com
 *         Xiang Yu, xiangy@lemote.com
 *         Chen Huacai, chenhc@lemote.com
 */

#include <linux/err.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <asm/bootinfo.h>
#include <boot_param.h>
#include <loongson_hwmon.h>
#include <workarounds.h>

static int __init loongson3_platform_init(void)
{
	int i;
	struct platform_device *pdev;

	if (loongson_sysconf.ecname[0] != '\0')
		platform_device_register_simple(loongson_sysconf.ecname, -1, NULL, 0);

	for (i = 0; i < loongson_sysconf.nr_sensors; i++) {
		if (loongson_sysconf.sensors[i].type > SENSOR_FAN)
			continue;

		pdev = kzalloc(sizeof(struct platform_device), GFP_KERNEL);
		if (!pdev)
			return -ENOMEM;

		pdev->name = loongson_sysconf.sensors[i].name;
		pdev->id = loongson_sysconf.sensors[i].id;
		pdev->dev.platform_data = &loongson_sysconf.sensors[i];
		platform_device_register(pdev);
	}

	return 0;
}

arch_initcall(loongson3_platform_init);

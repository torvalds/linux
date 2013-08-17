/*
 * Copyright (c) 2011-2012 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * EXYNOS - System MMU support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _ARM_MACH_EXYNOS_SYSMMU_H_
#define _ARM_MACH_EXYNOS_SYSMMU_H_

#include <linux/device.h>
#include <linux/list.h>

struct sysmmu_version {
	unsigned char major; /* major = 0 means that driver must use MMU_VERSION
				register instead of this structure */
	unsigned char minor;
};

enum sysmmu_property {
	SYSMMU_PROP_READ = 1,
	SYSMMU_PROP_WRITE = 2,
	SYSMMU_PROP_READWRITE = SYSMMU_PROP_READ | SYSMMU_PROP_WRITE,
};

struct sysmmu_platform_data {
	char *dbgname;
	/* comma(,) separated list of clock names for clock gating */
	char *clockname;
	struct sysmmu_version ver;
	short qos;
	enum sysmmu_property prop;
};

#define SYSMMU_DEVNAME_BASE "exynos-sysmmu"

#define SYSMMU_CLOCK_NAME "sysmmu"
#define SYSMMU_CLOCK_NAME2 "sysmmu_mc"
#define SYSMMU_CLOCK_NAME3 "sysmmu_mc2"

struct platform_device;

#define SYSMMU_PLATDEV(ipname) exynos_device_sysmmu_##ipname

#ifdef CONFIG_EXYNOS_DEV_SYSMMU

extern struct platform_device SYSMMU_PLATDEV(mfc_lr);
extern struct platform_device SYSMMU_PLATDEV(tv);
extern struct platform_device SYSMMU_PLATDEV(jpeg);
extern struct platform_device SYSMMU_PLATDEV(rot);
extern struct platform_device SYSMMU_PLATDEV(fimc0);
extern struct platform_device SYSMMU_PLATDEV(fimc1);
extern struct platform_device SYSMMU_PLATDEV(fimc2);
extern struct platform_device SYSMMU_PLATDEV(fimc3);
extern struct platform_device SYSMMU_PLATDEV(gsc0);
extern struct platform_device SYSMMU_PLATDEV(gsc1);
extern struct platform_device SYSMMU_PLATDEV(gsc2);
extern struct platform_device SYSMMU_PLATDEV(gsc3);
extern struct platform_device SYSMMU_PLATDEV(isp0);
extern struct platform_device SYSMMU_PLATDEV(isp1);
extern struct platform_device SYSMMU_PLATDEV(isp2);
extern struct platform_device SYSMMU_PLATDEV(isp3);
extern struct platform_device SYSMMU_PLATDEV(fimd0);
extern struct platform_device SYSMMU_PLATDEV(fimd1);
extern struct platform_device SYSMMU_PLATDEV(camif0);
extern struct platform_device SYSMMU_PLATDEV(camif1);
extern struct platform_device SYSMMU_PLATDEV(camif2);
extern struct platform_device SYSMMU_PLATDEV(2d);
extern struct platform_device SYSMMU_PLATDEV(scaler);
extern struct platform_device SYSMMU_PLATDEV(s3d);
extern struct platform_device SYSMMU_PLATDEV(mjpeg);

#endif /* CONFIG_EXYNOS_DEV_SYSMMU */

#define SYSMMU_CLOCK_DEVNAME(ipname, id) (SYSMMU_DEVNAME_BASE "." #id)

#endif /* _ARM_MACH_EXYNOS_SYSMMU_H_ */

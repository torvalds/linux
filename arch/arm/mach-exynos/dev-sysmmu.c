/* linux/arch/arm/mach-exynos/dev-sysmmu.c
 *
 * Copyright (c) 2010-2012 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * EXYNOS - System MMU support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/platform_device.h>
#include <linux/dma-mapping.h>

#include <plat/cpu.h>
#include <plat/pd.h>
#include <plat/devs.h>

#include <mach/map.h>
#include <mach/irqs.h>
#include <mach/sysmmu.h>

static u64 exynos_sysmmu_dma_mask = DMA_BIT_MASK(32);

/* DEFINE_RES_XXXX is defined in 3.3 kernel */
#ifndef DEFINE_RES_NAMED
#define DEFINE_RES_NAMED(_start, _size, _name, _flags)			\
	{								\
		.start = (_start),					\
		.end = (_start) + (_size) - 1,				\
		.name = (_name),					\
		.flags = (_flags),					\
	}

#define DEFINE_RES_MEM_NAMED(_start, _size, _name)			\
	DEFINE_RES_NAMED((_start), (_size), (_name), IORESOURCE_MEM)
#define DEFINE_RES_MEM(_start, _size)					\
	DEFINE_RES_MEM_NAMED((_start), (_size), NULL)

#define DEFINE_RES_IRQ_NAMED(_irq, _name)				\
	DEFINE_RES_NAMED((_irq), 1, (_name), IORESOURCE_IRQ)
#define DEFINE_RES_IRQ(_irq)						\
	DEFINE_RES_IRQ_NAMED((_irq), NULL)
#endif /* DEFINE_RES_NAMED */

#define SYSMMU_PLATFORM_DEVICE(ipname, devid)				\
static struct sysmmu_platform_data platdata_##ipname = {		\
	.dbgname = #ipname,						\
};									\
struct platform_device SYSMMU_PLATDEV(ipname) =				\
{									\
	.name		= SYSMMU_DEVNAME_BASE,				\
	.id		= devid,					\
	.dev		= {						\
		.dma_mask		= &exynos_sysmmu_dma_mask,	\
		.coherent_dma_mask	= DMA_BIT_MASK(32),		\
		.platform_data		= &platdata_##ipname,		\
	},								\
}

SYSMMU_PLATFORM_DEVICE(mfc_lr,	0);
SYSMMU_PLATFORM_DEVICE(tv,	2);
SYSMMU_PLATFORM_DEVICE(jpeg,	3);
SYSMMU_PLATFORM_DEVICE(rot,	4);
SYSMMU_PLATFORM_DEVICE(fimc0,	5); /* fimc* and gsc* exist exclusively */
SYSMMU_PLATFORM_DEVICE(fimc1,	6);
SYSMMU_PLATFORM_DEVICE(fimc2,	7);
SYSMMU_PLATFORM_DEVICE(fimc3,	8);
SYSMMU_PLATFORM_DEVICE(gsc0,	5);
SYSMMU_PLATFORM_DEVICE(gsc1,	6);
SYSMMU_PLATFORM_DEVICE(gsc2,	7);
SYSMMU_PLATFORM_DEVICE(gsc3,	8);
SYSMMU_PLATFORM_DEVICE(isp,	9);
SYSMMU_PLATFORM_DEVICE(fimd0,	10);
SYSMMU_PLATFORM_DEVICE(fimd1,	11);
SYSMMU_PLATFORM_DEVICE(camif0,	12);
SYSMMU_PLATFORM_DEVICE(camif1,	13);
SYSMMU_PLATFORM_DEVICE(camif2,	14);
SYSMMU_PLATFORM_DEVICE(2d,	15);

#define SYSMMU_RESOURCE_NAME(core, ipname) sysmmures_##core##_##ipname

#define SYSMMU_RESOURCE(core, ipname)					\
	static struct resource SYSMMU_RESOURCE_NAME(core, ipname)[] __initdata =

#define DEFINE_SYSMMU_RESOURCE(core, mem, irq)				\
	DEFINE_RES_MEM_NAMED(core##_PA_SYSMMU_##mem, SZ_4K, #mem),	\
	DEFINE_RES_IRQ_NAMED(core##_IRQ_SYSMMU_##irq##_0, #mem)

#define SYSMMU_RESOURCE_DEFINE(core, ipname, mem, irq)			\
	SYSMMU_RESOURCE(core, ipname) {					\
		DEFINE_SYSMMU_RESOURCE(core, mem, irq)			\
	}

struct sysmmu_resource_map {
	struct platform_device *pdev;
	struct resource *res;
	u32 rnum;
	struct device *pdd;
	char *clocknames;
};

#define SYSMMU_RESOURCE_MAPPING(core, ipname, resname) {		\
	.pdev = &SYSMMU_PLATDEV(ipname),				\
	.res = SYSMMU_RESOURCE_NAME(EXYNOS##core, resname),		\
	.rnum = ARRAY_SIZE(SYSMMU_RESOURCE_NAME(EXYNOS##core, resname)),\
	.clocknames = SYSMMU_CLOCK_NAME,				\
}

#define SYSMMU_RESOURCE_MAPPING_MC(core, ipname, resname, pdata) {	\
	.pdev = &SYSMMU_PLATDEV(ipname),				\
	.res = SYSMMU_RESOURCE_NAME(EXYNOS##core, resname),		\
	.rnum = ARRAY_SIZE(SYSMMU_RESOURCE_NAME(EXYNOS##core, resname)),\
	.clocknames = SYSMMU_CLOCK_NAME "," SYSMMU_CLOCK_NAME2,		\
}

#ifdef CONFIG_EXYNOS_DEV_PD
#define SYSMMU_RESOURCE_MAPPING_PD(core, ipname, resname, pd) {		\
	.pdev = &SYSMMU_PLATDEV(ipname),				\
	.res = SYSMMU_RESOURCE_NAME(EXYNOS##core, resname),		\
	.rnum = ARRAY_SIZE(SYSMMU_RESOURCE_NAME(EXYNOS##core, resname)),\
	.clocknames = SYSMMU_CLOCK_NAME,				\
	.pdd = &exynos##core##_device_pd[pd].dev,			\
}

#define SYSMMU_RESOURCE_MAPPING_MCPD(core, ipname, resname, pd, pdata) {\
	.pdev = &SYSMMU_PLATDEV(ipname),				\
	.res = SYSMMU_RESOURCE_NAME(EXYNOS##core, resname),		\
	.rnum = ARRAY_SIZE(SYSMMU_RESOURCE_NAME(EXYNOS##core, resname)),\
	.clocknames = SYSMMU_CLOCK_NAME "," SYSMMU_CLOCK_NAME2,		\
	.pdd = &exynos##core##_device_pd[pd].dev,			\
}
#else
#define SYSMMU_RESOURCE_MAPPING_PD(core, ipname, resname, pd)		\
		SYSMMU_RESOURCE_MAPPING(core, ipname, resname)
#define SYSMMU_RESOURCE_MAPPING_MCPD(core, ipname, resname, pd, pdata)	\
		SYSMMU_RESOURCE_MAPPING_MC(core, ipname, resname, pdata)

#endif /* CONFIG_EXYNOS_DEV_PD */

#ifdef CONFIG_ARCH_EXYNOS4
SYSMMU_RESOURCE_DEFINE(EXYNOS4, fimc0,	FIMC0,	FIMC0);
SYSMMU_RESOURCE_DEFINE(EXYNOS4, fimc1,	FIMC1,	FIMC1);
SYSMMU_RESOURCE_DEFINE(EXYNOS4, fimc2,	FIMC2,	FIMC2);
SYSMMU_RESOURCE_DEFINE(EXYNOS4, fimc3,	FIMC3,	FIMC3);
SYSMMU_RESOURCE_DEFINE(EXYNOS4, jpeg,	JPEG,	JPEG);
SYSMMU_RESOURCE_DEFINE(EXYNOS4, 2d,	G2D,	2D);
SYSMMU_RESOURCE_DEFINE(EXYNOS4, tv,	TV,	TV_M0);
SYSMMU_RESOURCE_DEFINE(EXYNOS4, 2d_acp,	G2D_ACP, 2D);
SYSMMU_RESOURCE_DEFINE(EXYNOS4, rot,	ROTATOR, ROTATOR);
SYSMMU_RESOURCE_DEFINE(EXYNOS4, fimd0,	FIMD0,	LCD0_M0);
SYSMMU_RESOURCE_DEFINE(EXYNOS4, fimd1,	FIMD1,	LCD1_M1);
SYSMMU_RESOURCE_DEFINE(EXYNOS4, flite0,	FIMC_LITE0, FIMC_LITE0);
SYSMMU_RESOURCE_DEFINE(EXYNOS4, flite1,	FIMC_LITE1, FIMC_LITE1);
SYSMMU_RESOURCE(EXYNOS4, mfc_lr) {
	DEFINE_SYSMMU_RESOURCE(EXYNOS4, MFC_R, MFC_M0),
	DEFINE_SYSMMU_RESOURCE(EXYNOS4, MFC_L, MFC_M1),
};
SYSMMU_RESOURCE(EXYNOS4, isp) {
	DEFINE_SYSMMU_RESOURCE(EXYNOS4, ISP, FIMC_ISP),
	DEFINE_SYSMMU_RESOURCE(EXYNOS4, DRC, FIMC_DRC),
	DEFINE_SYSMMU_RESOURCE(EXYNOS4, FD, FIMC_FD),
	DEFINE_SYSMMU_RESOURCE(EXYNOS4, ISPCPU, FIMC_CX),
};

static struct sysmmu_resource_map sysmmu_resmap4[] __initdata = {
	SYSMMU_RESOURCE_MAPPING_PD(4, fimc0,	fimc0,	PD_CAM),
	SYSMMU_RESOURCE_MAPPING_PD(4, fimc1,	fimc1,	PD_CAM),
	SYSMMU_RESOURCE_MAPPING_PD(4, fimc2,	fimc2,	PD_CAM),
	SYSMMU_RESOURCE_MAPPING_PD(4, fimc3,	fimc3,	PD_CAM),
	SYSMMU_RESOURCE_MAPPING_PD(4, tv,	tv,	PD_TV),
	SYSMMU_RESOURCE_MAPPING_PD(4, mfc_lr,	mfc_lr,	PD_MFC),
	SYSMMU_RESOURCE_MAPPING_PD(4, rot,	rot,	PD_LCD0),
	SYSMMU_RESOURCE_MAPPING_PD(4, jpeg,	jpeg,	PD_CAM),
	SYSMMU_RESOURCE_MAPPING_PD(4, fimd0,	fimd0,	PD_LCD0),
};

static struct sysmmu_resource_map sysmmu_resmap4210[] __initdata = {
	SYSMMU_RESOURCE_MAPPING_PD(4, 2d,	2d,	PD_LCD0),
	SYSMMU_RESOURCE_MAPPING_PD(4, fimd1,	fimd1,	PD_LCD1),
};

static struct sysmmu_resource_map sysmmu_resmap4212[] __initdata = {
	SYSMMU_RESOURCE_MAPPING(4,	2d,	2d_acp),
	SYSMMU_RESOURCE_MAPPING_PD(4,	camif0, flite0,	PD_ISP),
	SYSMMU_RESOURCE_MAPPING_PD(4,	camif1, flite1,	PD_ISP),
	SYSMMU_RESOURCE_MAPPING_PD(4,	isp,	isp,	PD_ISP),
};
#endif /* CONFIG_ARCH_EXYNOS4 */

#ifdef CONFIG_ARCH_EXYNOS5
SYSMMU_RESOURCE_DEFINE(EXYNOS5, jpeg,	JPEG,	JPEG);
SYSMMU_RESOURCE_DEFINE(EXYNOS5, fimd1,	FIMD1,	FIMD1);
SYSMMU_RESOURCE_DEFINE(EXYNOS5, 2d,	2D,	2D);
SYSMMU_RESOURCE_DEFINE(EXYNOS5, rot,	ROTATOR, ROTATOR);
SYSMMU_RESOURCE_DEFINE(EXYNOS5, tv,	TV,	TV);
SYSMMU_RESOURCE_DEFINE(EXYNOS5, flite0,	LITE0,	LITE0);
SYSMMU_RESOURCE_DEFINE(EXYNOS5, flite1,	LITE1,	LITE1);
SYSMMU_RESOURCE_DEFINE(EXYNOS5, flite2,	LITE2,	LITE2);
SYSMMU_RESOURCE_DEFINE(EXYNOS5, gsc0,	GSC0,	GSC0);
SYSMMU_RESOURCE_DEFINE(EXYNOS5, gsc1,	GSC1,	GSC1);
SYSMMU_RESOURCE_DEFINE(EXYNOS5, gsc2,	GSC2,	GSC2);
SYSMMU_RESOURCE_DEFINE(EXYNOS5, gsc3,	GSC3,	GSC3);
SYSMMU_RESOURCE(EXYNOS5, mfc_lr) {
	DEFINE_SYSMMU_RESOURCE(EXYNOS5, MFC_R, MFC_R),
	DEFINE_SYSMMU_RESOURCE(EXYNOS5, MFC_L, MFC_L),
};
SYSMMU_RESOURCE(EXYNOS5, isp) {
	DEFINE_SYSMMU_RESOURCE(EXYNOS5, ISP, ISP),
	DEFINE_SYSMMU_RESOURCE(EXYNOS5, DRC, DRC),
	DEFINE_SYSMMU_RESOURCE(EXYNOS5, FD, FD),
	DEFINE_SYSMMU_RESOURCE(EXYNOS5, ISPCPU, MCUISP),
	DEFINE_SYSMMU_RESOURCE(EXYNOS5, SCALERC, SCALERCISP),
	DEFINE_SYSMMU_RESOURCE(EXYNOS5, SCALERP, SCALERPISP),
	DEFINE_SYSMMU_RESOURCE(EXYNOS5,	ODC, ODC),
	DEFINE_SYSMMU_RESOURCE(EXYNOS5, DIS0, DIS0),
	DEFINE_SYSMMU_RESOURCE(EXYNOS5, DIS1, DIS1),
	DEFINE_SYSMMU_RESOURCE(EXYNOS5, 3DNR, 3DNR),
};

static struct sysmmu_resource_map sysmmu_resmap5[] __initdata = {
	SYSMMU_RESOURCE_MAPPING(5,	jpeg,	jpeg),
	SYSMMU_RESOURCE_MAPPING(5,	fimd1,	fimd1),
	SYSMMU_RESOURCE_MAPPING(5,	2d,	2d),
	SYSMMU_RESOURCE_MAPPING(5,	rot,	rot),
	SYSMMU_RESOURCE_MAPPING_PD(5,	tv,	tv,	PD_DISP1),
	SYSMMU_RESOURCE_MAPPING_PD(5,	camif0,	flite0,	PD_GSCL),
	SYSMMU_RESOURCE_MAPPING_PD(5,	camif1,	flite1,	PD_GSCL),
	SYSMMU_RESOURCE_MAPPING_PD(5,	camif2,	flite2,	PD_GSCL),
	SYSMMU_RESOURCE_MAPPING_PD(5,	gsc0,	gsc0,	PD_GSCL),
	SYSMMU_RESOURCE_MAPPING_PD(5,	gsc1,	gsc1,	PD_GSCL),
	SYSMMU_RESOURCE_MAPPING_PD(5,	gsc2,	gsc2,	PD_GSCL),
	SYSMMU_RESOURCE_MAPPING_PD(5,	gsc3,	gsc3,	PD_GSCL),
	SYSMMU_RESOURCE_MAPPING_PD(5,	mfc_lr,	mfc_lr,	PD_MFC),
	SYSMMU_RESOURCE_MAPPING_MCPD(5,	isp,	isp,	PD_ISP, mc_platdata),
};
#endif /* CONFIG_ARCH_EXYNOS5 */

static int __init init_sysmmu_platform_device(void)
{
	int i, j;
	struct sysmmu_resource_map *resmap[2] = {NULL, NULL};
	int nmap[2] = {0, 0};

#ifdef CONFIG_ARCH_EXYNOS5
	if (soc_is_exynos5250()) {
		resmap[0] = sysmmu_resmap5;
		nmap[0] = ARRAY_SIZE(sysmmu_resmap5);
		nmap[1] = 0;
	}
#endif

#ifdef CONFIG_ARCH_EXYNOS4
	if (resmap[0] == NULL) {
		resmap[0] = sysmmu_resmap4;
		nmap[0] = ARRAY_SIZE(sysmmu_resmap4);
	}

	if (soc_is_exynos4210()) {
		resmap[1] = sysmmu_resmap4210;
		nmap[1] = ARRAY_SIZE(sysmmu_resmap4210);
	}

	if (soc_is_exynos4412() || soc_is_exynos4212()) {
		resmap[1] = sysmmu_resmap4212;
		nmap[1] = ARRAY_SIZE(sysmmu_resmap4212);
	}
#endif

	for (j = 0; j < 2; j++) {
		for (i = 0; i < nmap[j]; i++) {
			struct sysmmu_resource_map *map;
			struct sysmmu_platform_data *platdata;

			map = &resmap[j][i];

			map->pdev->dev.parent = map->pdd;

			platdata = map->pdev->dev.platform_data;
			platdata->clockname = map->clocknames;

			if (platform_device_add_resources(map->pdev, map->res,
								map->rnum)) {
				pr_err("%s: Failed to add device resources for "
						"%s.%d\n", __func__,
						map->pdev->name, map->pdev->id);
				continue;
			}

			if (platform_device_register(map->pdev)) {
				pr_err("%s: Failed to register %s.%d\n",
					__func__, map->pdev->name,
						map->pdev->id);
			}
		}
	}

	return 0;
}
subsys_initcall(init_sysmmu_platform_device);

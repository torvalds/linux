/* linux/arch/arm/mach-exynos/dev-sysmmu.c
 *
 * Copyright (c) 2010-2011 Samsung Electronics Co., Ltd.
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

#include <mach/map.h>
#include <mach/irqs.h>
#include <mach/dev-sysmmu.h>
#include <plat/s5p-clock.h>

#if defined(CONFIG_ARCH_EXYNOS4)
#define EXYNOS_PA_SYSMMU(ipbase) EXYNOS4_PA_SYSMMU_##ipbase
#elif defined(CONFIG_ARCH_EXYNOS5)
#define EXYNOS_PA_SYSMMU(ipbase) EXYNOS5_PA_SYSMMU_##ipbase
#endif

#define SYSMMU_RESOURCE(ipname, base, irq) \
static struct resource sysmmu_resource_##ipname[] =\
{\
	{\
		.start	= EXYNOS_PA_SYSMMU(base),\
		.end	= EXYNOS_PA_SYSMMU(base) + SZ_4K - 1,\
		.flags	= IORESOURCE_MEM,\
	}, {\
		.start	= IRQ_SYSMMU_##irq##_0,\
		.end	= IRQ_SYSMMU_##irq##_0,\
		.flags	= IORESOURCE_IRQ,\
	},\
}

#define SYSMMU_PLATFORM_DEVICE(ipname, devid) \
struct platform_device SYSMMU_PLATDEV(ipname) =\
{\
	.name		= SYSMMU_DEVNAME_BASE,\
	.id		= devid,\
	.num_resources	= ARRAY_SIZE(sysmmu_resource_##ipname),\
	.resource	= sysmmu_resource_##ipname,\
	.dev		= {\
		.dma_mask		= &exynos_sysmmu_dma_mask,\
		.coherent_dma_mask	= DMA_BIT_MASK(32),\
	},\
}

static u64 exynos_sysmmu_dma_mask = DMA_BIT_MASK(32);

SYSMMU_RESOURCE(sss,	SSS,	SSS);
SYSMMU_RESOURCE(jpeg,	JPEG,	JPEG);
SYSMMU_RESOURCE(fimd1,	FIMD1,	FIMD1);
SYSMMU_RESOURCE(2d,	2D,	2D);
SYSMMU_RESOURCE(rot,	ROTATOR, ROTATOR);
SYSMMU_RESOURCE(mdma,	MDMA2,	MDMA1);
SYSMMU_RESOURCE(tv,	TV,	TV);
SYSMMU_RESOURCE(mfc_l,	MFC_L,	MFC_L);
SYSMMU_RESOURCE(mfc_r,	MFC_R,	MFC_R);
SYSMMU_RESOURCE(is_isp, ISP,	ISP);
SYSMMU_RESOURCE(is_drc, DRC,	DRC);
SYSMMU_RESOURCE(is_fd,	FD,	FD);
SYSMMU_RESOURCE(is_cpu, ISPCPU, MCUISP);
SYSMMU_RESOURCE(flite0,	LITE0,	LITE0);
SYSMMU_RESOURCE(flite1,	LITE1,	LITE1);

SYSMMU_PLATFORM_DEVICE(sss,	0);
SYSMMU_PLATFORM_DEVICE(jpeg,	5);
SYSMMU_PLATFORM_DEVICE(fimd1,	7);
SYSMMU_PLATFORM_DEVICE(2d,	9);
SYSMMU_PLATFORM_DEVICE(rot,	10);
SYSMMU_PLATFORM_DEVICE(mdma,	11);
SYSMMU_PLATFORM_DEVICE(tv,	12);
SYSMMU_PLATFORM_DEVICE(mfc_l,	13);
SYSMMU_PLATFORM_DEVICE(mfc_r,	14);
SYSMMU_PLATFORM_DEVICE(is_isp,	16);
SYSMMU_PLATFORM_DEVICE(is_drc,	17);
SYSMMU_PLATFORM_DEVICE(is_fd,	18);
SYSMMU_PLATFORM_DEVICE(is_cpu,	19);
SYSMMU_PLATFORM_DEVICE(flite0,	30);
SYSMMU_PLATFORM_DEVICE(flite1,	31);

#ifdef CONFIG_ARCH_EXYNOS4
SYSMMU_RESOURCE(fimc0,	FIMC0,	FIMC0);
SYSMMU_RESOURCE(fimc1,	FIMC1,	FIMC1);
SYSMMU_RESOURCE(fimc2,	FIMC2,	FIMC2);
SYSMMU_RESOURCE(fimc3,	FIMC3,	FIMC3);
SYSMMU_RESOURCE(fimd0,	FIMD0,	FIMD0);
SYSMMU_RESOURCE(pcie,	PCIe,	PCIE);
SYSMMU_RESOURCE(g2d_acp, G2D_ACP, 2D);

SYSMMU_PLATFORM_DEVICE(fimc0,	1);
SYSMMU_PLATFORM_DEVICE(fimc1,	2);
SYSMMU_PLATFORM_DEVICE(fimc2,	3);
SYSMMU_PLATFORM_DEVICE(fimc3,	4);
SYSMMU_PLATFORM_DEVICE(fimd0,	6);
SYSMMU_PLATFORM_DEVICE(pcie,	8);
SYSMMU_PLATFORM_DEVICE(g2d_acp,	15);
#endif

#ifdef CONFIG_ARCH_EXYNOS5
SYSMMU_RESOURCE(gsc0,	GSC0,	GSC0);
SYSMMU_RESOURCE(gsc1,	GSC1,	GSC1);
SYSMMU_RESOURCE(gsc2,	GSC2,	GSC2);
SYSMMU_RESOURCE(gsc3,	GSC3,	GSC3);
SYSMMU_RESOURCE(is_sclrc, SCALERC, SCALERCISP);
SYSMMU_RESOURCE(is_sclrp, SCALERP, SCALERPISP);
SYSMMU_RESOURCE(is_odc,	ODC,	ODC);
SYSMMU_RESOURCE(is_dis0, DIS0,	DIS0);
SYSMMU_RESOURCE(is_dis1, DIS1,	DIS1);
SYSMMU_RESOURCE(is_3dnr, 3DNR,	3DNR);
SYSMMU_RESOURCE(flite2,	LITE2,	LITE2);

SYSMMU_PLATFORM_DEVICE(gsc0,	20);
SYSMMU_PLATFORM_DEVICE(gsc1,	21);
SYSMMU_PLATFORM_DEVICE(gsc2,	22);
SYSMMU_PLATFORM_DEVICE(gsc3,	23);
SYSMMU_PLATFORM_DEVICE(is_sclrc, 24);
SYSMMU_PLATFORM_DEVICE(is_sclrp, 25);
SYSMMU_PLATFORM_DEVICE(is_odc,	26);
SYSMMU_PLATFORM_DEVICE(is_dis0,	27);
SYSMMU_PLATFORM_DEVICE(is_dis1,	28);
SYSMMU_PLATFORM_DEVICE(is_3dnr,	29);
SYSMMU_PLATFORM_DEVICE(flite2,	32);
#endif

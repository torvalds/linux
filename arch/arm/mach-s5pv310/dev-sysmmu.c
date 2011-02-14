/* linux/arch/arm/mach-s5pv310/dev-sysmmu.c
 *
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/platform_device.h>
#include <linux/dma-mapping.h>

#include <mach/map.h>
#include <mach/irqs.h>

static struct resource s5pv310_sysmmu_resource[] = {
	[0] = {
		.start	= S5PV310_PA_SYSMMU_MDMA,
		.end	= S5PV310_PA_SYSMMU_MDMA + SZ_64K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= IRQ_SYSMMU_MDMA0_0,
		.end	= IRQ_SYSMMU_MDMA0_0,
		.flags	= IORESOURCE_IRQ,
	},
	[2] = {
		.start	= S5PV310_PA_SYSMMU_SSS,
		.end	= S5PV310_PA_SYSMMU_SSS + SZ_64K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[3] = {
		.start	= IRQ_SYSMMU_SSS_0,
		.end	= IRQ_SYSMMU_SSS_0,
		.flags	= IORESOURCE_IRQ,
	},
	[4] = {
		.start	= S5PV310_PA_SYSMMU_FIMC0,
		.end	= S5PV310_PA_SYSMMU_FIMC0 + SZ_64K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[5] = {
		.start	= IRQ_SYSMMU_FIMC0_0,
		.end	= IRQ_SYSMMU_FIMC0_0,
		.flags	= IORESOURCE_IRQ,
	},
	[6] = {
		.start	= S5PV310_PA_SYSMMU_FIMC1,
		.end	= S5PV310_PA_SYSMMU_FIMC1 + SZ_64K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[7] = {
		.start	= IRQ_SYSMMU_FIMC1_0,
		.end	= IRQ_SYSMMU_FIMC1_0,
		.flags	= IORESOURCE_IRQ,
	},
	[8] = {
		.start	= S5PV310_PA_SYSMMU_FIMC2,
		.end	= S5PV310_PA_SYSMMU_FIMC2 + SZ_64K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[9] = {
		.start	= IRQ_SYSMMU_FIMC2_0,
		.end	= IRQ_SYSMMU_FIMC2_0,
		.flags	= IORESOURCE_IRQ,
	},
	[10] = {
		.start	= S5PV310_PA_SYSMMU_FIMC3,
		.end	= S5PV310_PA_SYSMMU_FIMC3 + SZ_64K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[11] = {
		.start	= IRQ_SYSMMU_FIMC3_0,
		.end	= IRQ_SYSMMU_FIMC3_0,
		.flags	= IORESOURCE_IRQ,
	},
	[12] = {
		.start	= S5PV310_PA_SYSMMU_JPEG,
		.end	= S5PV310_PA_SYSMMU_JPEG + SZ_64K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[13] = {
		.start	= IRQ_SYSMMU_JPEG_0,
		.end	= IRQ_SYSMMU_JPEG_0,
		.flags	= IORESOURCE_IRQ,
	},
	[14] = {
		.start	= S5PV310_PA_SYSMMU_FIMD0,
		.end	= S5PV310_PA_SYSMMU_FIMD0 + SZ_64K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[15] = {
		.start	= IRQ_SYSMMU_LCD0_M0_0,
		.end	= IRQ_SYSMMU_LCD0_M0_0,
		.flags	= IORESOURCE_IRQ,
	},
	[16] = {
		.start	= S5PV310_PA_SYSMMU_FIMD1,
		.end	= S5PV310_PA_SYSMMU_FIMD1 + SZ_64K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[17] = {
		.start	= IRQ_SYSMMU_LCD1_M1_0,
		.end	= IRQ_SYSMMU_LCD1_M1_0,
		.flags	= IORESOURCE_IRQ,
	},
	[18] = {
		.start	= S5PV310_PA_SYSMMU_PCIe,
		.end	= S5PV310_PA_SYSMMU_PCIe + SZ_64K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[19] = {
		.start	= IRQ_SYSMMU_PCIE_0,
		.end	= IRQ_SYSMMU_PCIE_0,
		.flags	= IORESOURCE_IRQ,
	},
	[20] = {
		.start	= S5PV310_PA_SYSMMU_G2D,
		.end	= S5PV310_PA_SYSMMU_G2D + SZ_64K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[21] = {
		.start	= IRQ_SYSMMU_2D_0,
		.end	= IRQ_SYSMMU_2D_0,
		.flags	= IORESOURCE_IRQ,
	},
	[22] = {
		.start	= S5PV310_PA_SYSMMU_ROTATOR,
		.end	= S5PV310_PA_SYSMMU_ROTATOR + SZ_64K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[23] = {
		.start	= IRQ_SYSMMU_ROTATOR_0,
		.end	= IRQ_SYSMMU_ROTATOR_0,
		.flags	= IORESOURCE_IRQ,
	},
	[24] = {
		.start	= S5PV310_PA_SYSMMU_MDMA2,
		.end	= S5PV310_PA_SYSMMU_MDMA2 + SZ_64K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[25] = {
		.start	= IRQ_SYSMMU_MDMA1_0,
		.end	= IRQ_SYSMMU_MDMA1_0,
		.flags	= IORESOURCE_IRQ,
	},
	[26] = {
		.start	= S5PV310_PA_SYSMMU_TV,
		.end	= S5PV310_PA_SYSMMU_TV + SZ_64K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[27] = {
		.start	= IRQ_SYSMMU_TV_M0_0,
		.end	= IRQ_SYSMMU_TV_M0_0,
		.flags	= IORESOURCE_IRQ,
	},
	[28] = {
		.start	= S5PV310_PA_SYSMMU_MFC_L,
		.end	= S5PV310_PA_SYSMMU_MFC_L + SZ_64K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[29] = {
		.start	= IRQ_SYSMMU_MFC_M0_0,
		.end	= IRQ_SYSMMU_MFC_M0_0,
		.flags	= IORESOURCE_IRQ,
	},
	[30] = {
		.start	= S5PV310_PA_SYSMMU_MFC_R,
		.end	= S5PV310_PA_SYSMMU_MFC_R + SZ_64K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[31] = {
		.start	= IRQ_SYSMMU_MFC_M1_0,
		.end	= IRQ_SYSMMU_MFC_M1_0,
		.flags	= IORESOURCE_IRQ,
	},
};

struct platform_device s5pv310_device_sysmmu = {
	.name		= "s5p-sysmmu",
	.id		= 32,
	.num_resources	= ARRAY_SIZE(s5pv310_sysmmu_resource),
	.resource	= s5pv310_sysmmu_resource,
};

EXPORT_SYMBOL(s5pv310_device_sysmmu);

/* linux/arch/arm/mach-exynos/bts.c
 *
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/platform_device.h>
#include <linux/dma-mapping.h>

#include <plat/devs.h>
#include <plat/irqs.h>
#ifdef CONFIG_EXYNOS_DEV_PD
#include <plat/pd.h>
#endif

#include <mach/map.h>
#include <mach/irqs.h>

#include <plat/cpu.h>
#include <plat/bts.h>
#include <mach/map-exynos5.h>


static struct resource exynos_bts_cpu_resource[] = {
	[0] = {
		.start  = EXYNOS5_PA_BTS_CPU,
		.end    = EXYNOS5_PA_BTS_CPU + SZ_1K,
		.flags = IORESOURCE_MEM,
	},
};

static struct resource exynos_bts_jpeg_resource[] = {
	[0] = {
		.start  = EXYNOS5_PA_BTS_JPEG,
		.end    = EXYNOS5_PA_BTS_JPEG + SZ_1K,
		.flags = IORESOURCE_MEM,
	},
};

static struct resource exynos_bts_mdma1_resource[] = {
	[0] = {
		.start  = EXYNOS5_PA_BTS_MDMA1,
		.end    = EXYNOS5_PA_BTS_MDMA1 + SZ_1K,
		.flags = IORESOURCE_MEM,
	},
};

static struct resource exynos_bts_rotator_resource[] = {
	[0] = {
		.start  = EXYNOS5_PA_BTS_ROTATOR,
		.end    = EXYNOS5_PA_BTS_ROTATOR + SZ_1K,
		.flags = IORESOURCE_MEM,
	},
};

static struct resource exynos_bts_gscl0_resource[] = {
	[0] = {
		.start  = EXYNOS5_PA_BTS_GSCL0,
		.end    = EXYNOS5_PA_BTS_GSCL0 + SZ_1K,
		.flags = IORESOURCE_MEM,
	},
};

static struct resource exynos_bts_gscl1_resource[] = {
	[0] = {
		.start  = EXYNOS5_PA_BTS_GSCL1,
		.end    = EXYNOS5_PA_BTS_GSCL1 + SZ_1K,
		.flags = IORESOURCE_MEM,
	},
};

static struct resource exynos_bts_gscl2_resource[] = {
	[0] = {
		.start  = EXYNOS5_PA_BTS_GSCL2,
		.end    = EXYNOS5_PA_BTS_GSCL2 + SZ_1K,
		.flags = IORESOURCE_MEM,
	},
};

static struct resource exynos_bts_gscl3_resource[] = {
	[0] = {
		.start  = EXYNOS5_PA_BTS_GSCL3,
		.end    = EXYNOS5_PA_BTS_GSCL3 + SZ_1K,
		.flags = IORESOURCE_MEM,
	},
};

static struct resource exynos_bts_mfc_resource[] = {
	[0] = {
		.start  = EXYNOS5_PA_BTS_MFC0,
		.end    = EXYNOS5_PA_BTS_MFC0 + SZ_1K,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start  = EXYNOS5_PA_BTS_MFC1,
		.end    = EXYNOS5_PA_BTS_MFC1 + SZ_1K,
		.flags = IORESOURCE_MEM,
	},
};

static struct resource exynos_bts_g3dacp_resource[] = {
	[0] = {
		.start  = EXYNOS5_PA_BTS_G3D_ACP,
		.end    = EXYNOS5_PA_BTS_G3D_ACP + SZ_1K,
		.flags = IORESOURCE_MEM,
	},
};

static struct resource exynos_bts_isp0_resource[] = {
	[0] = {
		.start  = EXYNOS5_PA_BTS_FIMC_ISP,
		.end    = EXYNOS5_PA_BTS_FIMC_ISP + SZ_1K,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start  = EXYNOS5_PA_BTS_FIMC_FD,
		.end    = EXYNOS5_PA_BTS_FIMC_FD + SZ_1K,
		.flags = IORESOURCE_MEM,
	},
	[2] = {
		.start  = EXYNOS5_PA_BTS_FIMC_SCALER_C,
		.end    = EXYNOS5_PA_BTS_FIMC_SCALER_C + SZ_1K,
		.flags = IORESOURCE_MEM,
	},
	[3] = {
		.start  = EXYNOS5_PA_BTS_FIMC_SCALER_P,
		.end    = EXYNOS5_PA_BTS_FIMC_SCALER_P + SZ_1K,
		.flags = IORESOURCE_MEM,
	},
};

static struct resource exynos_bts_isp1_resource[] = {
	[0] = {
		.start  = EXYNOS5_PA_BTS_FIMC_ODC,
		.end    = EXYNOS5_PA_BTS_FIMC_ODC + SZ_1K,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start  = EXYNOS5_PA_BTS_FIMC_DIS0,
		.end    = EXYNOS5_PA_BTS_FIMC_DIS0 + SZ_1K,
		.flags = IORESOURCE_MEM,
	},
	[2] = {
		.start  = EXYNOS5_PA_BTS_FIMC_DIS1,
		.end    = EXYNOS5_PA_BTS_FIMC_DIS1 + SZ_1K,
		.flags = IORESOURCE_MEM,
	},
	[3] = {
		.start  = EXYNOS5_PA_BTS_FIMC_3DNR,
		.end    = EXYNOS5_PA_BTS_FIMC_3DNR + SZ_1K,
		.flags = IORESOURCE_MEM,
	},
};

static struct resource exynos_bts_disp_resource[] = {
	[0] = {
		.start  = EXYNOS5_PA_BTS_DISP10,
		.end    = EXYNOS5_PA_BTS_DISP10 + SZ_1K,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start  = EXYNOS5_PA_BTS_DISP11,
		.end    = EXYNOS5_PA_BTS_DISP11 + SZ_1K,
		.flags = IORESOURCE_MEM,
	},
};

static struct resource exynos_bts_tv_resource[] = {
	[0] = {
		.start  = EXYNOS5_PA_BTS_TV0,
		.end    = EXYNOS5_PA_BTS_TV0 + SZ_1K,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start  = EXYNOS5_PA_BTS_TV1,
		.end    = EXYNOS5_PA_BTS_TV1 + SZ_1K,
		.flags = IORESOURCE_MEM,
	},
};

static struct resource exynos_bts_c2c_resource[] = {
	[0] = {
		.start  = EXYNOS5_PA_BTS_C2C,
		.end    = EXYNOS5_PA_BTS_C2C + SZ_1K,
		.flags = IORESOURCE_MEM,
	},
};

struct exynos_fbm_resource fbm_res[] = {
	{
		.fbm_group = BTS_FBM_G1_R,
		.priority = BTS_BE,
		.base = EXYNOS5_PA_FBM_DDR_R1,
	}, {
		.fbm_group = BTS_FBM_G1_L,
		.priority = BTS_HARDTIME,
		.base = EXYNOS5_PA_FBM_DDR_R0,
	}
};

struct exynos_fbm_pdata fbm_pdata = {
	.res = fbm_res,
	.res_num = ARRAY_SIZE(fbm_res),
};

struct exynos_bts_pdata bts_cpu_res = {
	.id = BTS_CPU,
	.def_priority = BTS_BE,
	.pd_block = PD_TOP,
	.clk_name = NULL,
	.fbm = &fbm_pdata,
	.res_num = ARRAY_SIZE(exynos_bts_cpu_resource),
};

struct exynos_bts_pdata bts_jpeg_res = {
	.id = BTS_JPEG,
	.def_priority = BTS_BE,
	.pd_block = PD_GSCL,
	.clk_name = "jpeg",
	.fbm = &fbm_pdata,
	.res_num = ARRAY_SIZE(exynos_bts_jpeg_resource),
};

struct exynos_bts_pdata bts_mdma1_res = {
	.id = BTS_MDMA1,
	.def_priority = BTS_BE,
	.clk_name = "pdma",
	.pd_block = PD_TOP,
	.fbm = &fbm_pdata,
	.res_num = ARRAY_SIZE(exynos_bts_mdma1_resource),
};

struct exynos_bts_pdata bts_rotator_res = {
	.id = BTS_ROTATOR,
	.def_priority = BTS_BE,
	.pd_block = PD_DISP1,
	.clk_name = "rotator",
	.fbm = &fbm_pdata,
	.res_num = ARRAY_SIZE(exynos_bts_rotator_resource),
};

struct exynos_bts_pdata bts_gscl0_res = {
	.id = BTS_GSCL,
	.def_priority = BTS_BE,
	.pd_block = PD_GSCL,
	.clk_name = "gscl",
	.fbm = &fbm_pdata,
	.res_num = ARRAY_SIZE(exynos_bts_gscl0_resource),
};

struct exynos_bts_pdata bts_gscl1_res = {
	.id = BTS_GSCL,
	.def_priority = BTS_BE,
	.pd_block = PD_GSCL,
	.clk_name = "gscl",
	.fbm = &fbm_pdata,
	.res_num = ARRAY_SIZE(exynos_bts_gscl1_resource),
};

struct exynos_bts_pdata bts_gscl2_res = {
	.id = BTS_GSCL,
	.def_priority = BTS_BE,
	.pd_block = PD_GSCL,
	.clk_name = "gscl",
	.fbm = &fbm_pdata,
	.res_num = ARRAY_SIZE(exynos_bts_gscl2_resource),
};

struct exynos_bts_pdata bts_gscl3_res = {
	.id = BTS_GSCL,
	.def_priority = BTS_BE,
	.pd_block = PD_GSCL,
	.clk_name = "gscl",
	.fbm = &fbm_pdata,
	.res_num = ARRAY_SIZE(exynos_bts_gscl3_resource),
};

struct exynos_bts_pdata bts_mfc_res = {
	.id = BTS_MFC,
	.def_priority = BTS_BE,
	.pd_block = PD_MFC,
	.clk_name = "mfc",
	.fbm = &fbm_pdata,
	.res_num = ARRAY_SIZE(exynos_bts_mfc_resource),
};

struct exynos_bts_pdata bts_g3dacp_res = {
	.id = BTS_G3D_ACP,
	.def_priority = BTS_BE,
	.pd_block = PD_G3D,
	.clk_name = "g3d",
	.fbm = &fbm_pdata,
	.res_num = ARRAY_SIZE(exynos_bts_g3dacp_resource),
};

struct exynos_bts_pdata bts_isp0_res = {
	.id = BTS_ISP0,
	.def_priority = BTS_BE,
	.pd_block = PD_MFC,
	.clk_name = "isp0",
	.fbm = &fbm_pdata,
	.res_num = ARRAY_SIZE(exynos_bts_isp0_resource),
};

struct exynos_bts_pdata bts_isp1_res = {
	.id = BTS_ISP1,
	.def_priority = BTS_BE,
	.pd_block = PD_MFC,
	.clk_name = "isp1",
	.fbm = &fbm_pdata,
	.res_num = ARRAY_SIZE(exynos_bts_isp1_resource),
};

struct exynos_bts_pdata bts_disp_res = {
	.id = BTS_DISP,
	.def_priority = BTS_HARDTIME,
	.pd_block = PD_DISP1,
	.clk_name = "lcd",
	.fbm = &fbm_pdata,
	.res_num = ARRAY_SIZE(exynos_bts_disp_resource),
};

struct exynos_bts_pdata bts_tv_res = {
	.id = BTS_TV,
	.def_priority = BTS_HARDTIME,
	.pd_block = PD_DISP1,
	.clk_name = "mixer",
	.fbm = &fbm_pdata,
	.res_num = ARRAY_SIZE(exynos_bts_tv_resource),
};

struct exynos_bts_pdata bts_c2c_res = {
	.id = BTS_C2C,
	.def_priority = BTS_HARDTIME,
	.clk_name = "c2c",
	.fbm = &fbm_pdata,
	.res_num = ARRAY_SIZE(exynos_bts_c2c_resource),
};

/* bts platform device lists */
struct platform_device exynos_device_bts_disp = {
	.name		= "exynos-bts",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(exynos_bts_disp_resource),
	.resource	= exynos_bts_disp_resource,
	.dev		= {
		.platform_data = &bts_disp_res,
		.parent = &s5p_device_fimd1.dev,
	},
};

struct platform_device exynos_device_bts_tv = {
	.name		= "exynos-bts",
	.id		= 1,
	.num_resources	= ARRAY_SIZE(exynos_bts_tv_resource),
	.resource	= exynos_bts_tv_resource,
	.dev		= {
		.platform_data = &bts_tv_res,
		.parent = &s5p_device_mixer.dev,
	},
};

#if defined(CONFIG_EXYNOS_C2C)
struct platform_device exynos_device_bts_c2c = {
	.name		= "exynos-bts",
	.id		= 2,
	.num_resources	= ARRAY_SIZE(exynos_bts_c2c_resource),
	.resource	= exynos_bts_c2c_resource,
	.dev		= {
		.platform_data = &bts_c2c_res,
		.parent = &exynos_device_c2c.dev,
	},
};
#endif

struct platform_device exynos_device_bts_g3dacp = {
	.name		= "exynos-bts",
	.id		= 3,
	.num_resources	= ARRAY_SIZE(exynos_bts_g3dacp_resource),
	.resource	= exynos_bts_g3dacp_resource,
	.dev		= {
		.platform_data = &bts_g3dacp_res,
	},
};

struct platform_device exynos_device_bts_rotator = {
	.name		= "exynos-bts",
	.id		= 4,
	.num_resources	= ARRAY_SIZE(exynos_bts_rotator_resource),
	.resource	= exynos_bts_rotator_resource,
	.dev		= {
		.platform_data = &bts_rotator_res,
		.parent = &exynos_device_rotator.dev,
	},
};

struct platform_device exynos_device_bts_jpeg = {
	.name		= "exynos-bts",
	.id		= 5,
	.num_resources	= ARRAY_SIZE(exynos_bts_jpeg_resource),
	.resource	= exynos_bts_jpeg_resource,
	.dev		= {
		.platform_data = &bts_jpeg_res,
	},
};

struct platform_device exynos_device_bts_mdma1 = {
	.name		= "exynos-bts",
	.id		= 6,
	.num_resources	= ARRAY_SIZE(exynos_bts_mdma1_resource),
	.resource	= exynos_bts_mdma1_resource,
	.dev		= {
		.platform_data = &bts_mdma1_res,
		.parent = &exynos_device_mdma.dev,
	},
};

struct platform_device exynos_device_bts_gscl0 = {
	.name		= "exynos-bts",
	.id		= 7,
	.num_resources	= ARRAY_SIZE(exynos_bts_gscl0_resource),
	.resource	= exynos_bts_gscl0_resource,
	.dev		= {
		.platform_data = &bts_gscl0_res,
		.parent = &exynos5_device_gsc0.dev,
	},
};

struct platform_device exynos_device_bts_gscl1 = {
	.name		= "exynos-bts",
	.id		= 8,
	.num_resources	= ARRAY_SIZE(exynos_bts_gscl1_resource),
	.resource	= exynos_bts_gscl1_resource,
	.dev		= {
		.platform_data = &bts_gscl1_res,
		.parent = &exynos5_device_gsc1.dev,
	},
};

struct platform_device exynos_device_bts_gscl2 = {
	.name		= "exynos-bts",
	.id		= 9,
	.num_resources	= ARRAY_SIZE(exynos_bts_gscl2_resource),
	.resource	= exynos_bts_gscl2_resource,
	.dev		= {
		.platform_data = &bts_gscl2_res,
		.parent = &exynos5_device_gsc2.dev,
	},
};

struct platform_device exynos_device_bts_gscl3 = {
	.name		= "exynos-bts",
	.id		= 10,
	.num_resources	= ARRAY_SIZE(exynos_bts_gscl3_resource),
	.resource	= exynos_bts_gscl3_resource,
	.dev		= {
		.platform_data = &bts_gscl3_res,
		.parent = &exynos5_device_gsc3.dev,
	},
};

struct platform_device exynos_device_bts_mfc = {
	.name		= "exynos-bts",
	.id		= 11,
	.num_resources	= ARRAY_SIZE(exynos_bts_mfc_resource),
	.resource	= exynos_bts_mfc_resource,
	.dev		= {
		.platform_data = &bts_mfc_res,
		.parent = &s5p_device_mfc.dev,
	},
};

#if defined(CONFIG_EXYNOS4_DEV_FIMC_IS)
struct platform_device exynos_device_bts_isp0 = {
	.name		= "exynos-bts",
	.id		= 12,
	.num_resources	= ARRAY_SIZE(exynos_bts_isp0_resource),
	.resource	= exynos_bts_isp0_resource,
	.dev		= {
		.platform_data = &bts_isp0_res,
		.parent = &exynos5_device_fimc_is.dev,
	},
};

struct platform_device exynos_device_bts_isp1 = {
	.name		= "exynos-bts",
	.id		= 13,
	.num_resources	= ARRAY_SIZE(exynos_bts_isp1_resource),
	.resource	= exynos_bts_isp1_resource,
	.dev		= {
		.platform_data = &bts_isp1_res,
		.parent = &exynos5_device_fimc_is.dev,
	},
};
#endif

struct platform_device exynos_device_bts_cpu = {
	.name		= "exynos-bts",
	.id		= 14,
	.num_resources	= ARRAY_SIZE(exynos_bts_cpu_resource),
	.resource	= exynos_bts_cpu_resource,
	.dev		= {
		.platform_data = &bts_cpu_res,
	},
};

static struct platform_device *exynos_bts[] __initdata = {
	&exynos_device_bts_disp,
	&exynos_device_bts_tv,
#if defined(CONFIG_EXYNOS_C2C)
	&exynos_device_bts_c2c,
#endif
	&exynos_device_bts_g3dacp,
	&exynos_device_bts_rotator,
	&exynos_device_bts_jpeg,
	&exynos_device_bts_mdma1,
	&exynos_device_bts_gscl0,
	&exynos_device_bts_gscl1,
	&exynos_device_bts_gscl2,
	&exynos_device_bts_gscl3,
	&exynos_device_bts_mfc,
	&exynos_device_bts_isp0,
	&exynos_device_bts_isp1,
	&exynos_device_bts_cpu,
};

static int __init exynos_bts_init(void)
{
	return platform_add_devices(exynos_bts, ARRAY_SIZE(exynos_bts));
}
arch_initcall(exynos_bts_init);

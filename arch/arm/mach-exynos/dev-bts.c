/* linux/arch/arm/mach-exynos/dev-bts.c
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
#include <plat/cpu.h>
#include <plat/bts.h>

#include <mach/map.h>
#include <mach/irqs.h>
#include <mach/map.h>

static struct resource exynos_bts_fbm_ddr_r1_resource[] = {
	[0] = {
		.start  = EXYNOS5_PA_BTS_FBM_DDR_R1,
		.end    = EXYNOS5_PA_BTS_FBM_DDR_R1 + SZ_1K,
		.flags = IORESOURCE_MEM,
	},
};

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

#if defined(CONFIG_EXYNOS4_DEV_FIMC_IS)
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
#endif

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

static struct resource exynos_bts_mixer_resource[] = {
	[0] = {
		.start  = EXYNOS5_PA_BTS_MIXER0,
		.end    = EXYNOS5_PA_BTS_MIXER0 + SZ_1K,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start  = EXYNOS5_PA_BTS_MIXER1,
		.end    = EXYNOS5_PA_BTS_MIXER1 + SZ_1K,
		.flags = IORESOURCE_MEM,
	},
};

struct exynos_fbm_resource fbm_res[] = {
	{
		.port_name = BTS_FBM_G1_R,
		.deblock_src_order = BTS_2ND_FBM_SRC,
	}, {
		.port_name = BTS_FBM_G1_L,
		.deblock_src_order = BTS_1ST_FBM_SRC,
	}
};

struct exynos_fbm_pdata fbm_pdata = {
	.res = fbm_res,
	.res_num = ARRAY_SIZE(fbm_res),
};

#define EXYNOS_BTS_PDATA(_name, _prio, _block, _clkname, _changable, _th_changable, _act)\
static struct exynos_bts_pdata bts_##_name##_res = {			\
		.def_priority = _prio,					\
		.pd_name = _block,					\
		.clk_name = _clkname,					\
		.fbm = &fbm_pdata,					\
		.res_num = ARRAY_SIZE(exynos_bts_##_name##_resource),	\
		.deblock_changable = _changable,	\
		.threshold_changable = _th_changable,	\
		.traffic_control_act = _act,	\
}

EXYNOS_BTS_PDATA(fbm_ddr_r1, BTS_FBM_DDR_R1, NULL, NULL, 0, 1, BTS_NO_ACTION);
EXYNOS_BTS_PDATA(cpu, BTS_PRIOR_BE, NULL, NULL, 1, 0, BTS_NO_ACTION);
EXYNOS_BTS_PDATA(jpeg, BTS_PRIOR_BE, "pd-gscl", "jpeg", 0, 0, BTS_NO_ACTION);
EXYNOS_BTS_PDATA(gscl0, BTS_PRIOR_BE, "pd-gscl", "gscl", 0, 0, BTS_ON_OFF);
EXYNOS_BTS_PDATA(gscl1, BTS_PRIOR_BE, "pd-gscl", "gscl", 0, 0, BTS_ON_OFF);
EXYNOS_BTS_PDATA(gscl2, BTS_PRIOR_BE, "pd-gscl", "gscl", 0, 0, BTS_ON_OFF);
EXYNOS_BTS_PDATA(gscl3, BTS_PRIOR_BE, "pd-gscl", "gscl", 0, 0, BTS_ON_OFF);
EXYNOS_BTS_PDATA(mfc, BTS_PRIOR_BE, "pd-mfc", "mfc", 0, 0, BTS_NO_ACTION);
EXYNOS_BTS_PDATA(g3dacp, BTS_PRIOR_BE, "pd-g3d", "g3d", 1, 0, BTS_NO_ACTION);
#if defined(CONFIG_EXYNOS4_DEV_FIMC_IS)
EXYNOS_BTS_PDATA(isp0, BTS_PRIOR_BE, "pd-isp", "isp0", 0, 0, BTS_CHANGE_OTHER_DEBLOCK);
EXYNOS_BTS_PDATA(isp1, BTS_PRIOR_BE, "pd-isp", "isp1", 0, 0, BTS_CHANGE_OTHER_DEBLOCK);
#endif
EXYNOS_BTS_PDATA(disp, BTS_PRIOR_HARDTIME, "pd-disp1", "fimd", 0, 0, BTS_NO_ACTION);
EXYNOS_BTS_PDATA(mixer, BTS_PRIOR_HARDTIME, "pd-disp1", "mixer", 0, 0, BTS_NO_ACTION);

/* bts platform device lists */
#define EXYNOS_BTS_DEVICE(_name, _parent, _bts_name)			\
static struct platform_device exynos_device_bts_##_name = {		\
		.name		= "exynos-bts",				\
		.num_resources	= ARRAY_SIZE(exynos_bts_##_name##_resource),\
		.resource	= exynos_bts_##_name##_resource,	\
		.dev		= {					\
			.platform_data = &bts_##_name##_res,		\
			.parent = _parent,				\
			.init_name = _bts_name,				\
		}							\
}

EXYNOS_BTS_DEVICE(disp, &s5p_device_fimd1.dev, "disp-bts");
EXYNOS_BTS_DEVICE(mixer, &s5p_device_mixer.dev, "mixer-bts");
EXYNOS_BTS_DEVICE(g3dacp, &exynos5_device_g3d.dev, "g3dacp-bts");
EXYNOS_BTS_DEVICE(jpeg, NULL, "jpeg-bts");
EXYNOS_BTS_DEVICE(gscl0, &exynos5_device_gsc0.dev, "gscl0-bts");
EXYNOS_BTS_DEVICE(gscl1, &exynos5_device_gsc1.dev, "gscl1-bts");
EXYNOS_BTS_DEVICE(gscl2, &exynos5_device_gsc2.dev, "gscl2-bts");
EXYNOS_BTS_DEVICE(gscl3, &exynos5_device_gsc3.dev, "gscl3-bts");
EXYNOS_BTS_DEVICE(mfc, &s5p_device_mfc.dev, "mfc-bts");
#if defined(CONFIG_EXYNOS4_DEV_FIMC_IS)
EXYNOS_BTS_DEVICE(isp0, &exynos5_device_fimc_is.dev, "isp0-bts");
EXYNOS_BTS_DEVICE(isp1, &exynos5_device_fimc_is.dev, "isp1-bts");
#endif
EXYNOS_BTS_DEVICE(cpu, NULL, "cpu-bts");
EXYNOS_BTS_DEVICE(fbm_ddr_r1, NULL, "fbm-ddr-r1-bts");

static struct platform_device *exynos_bts[] __initdata = {
	&exynos_device_bts_disp,
	&exynos_device_bts_mixer,
	&exynos_device_bts_cpu,
	&exynos_device_bts_fbm_ddr_r1,
	&exynos_device_bts_g3dacp,
	&exynos_device_bts_jpeg,
	&exynos_device_bts_gscl0,
	&exynos_device_bts_gscl1,
	&exynos_device_bts_gscl2,
	&exynos_device_bts_gscl3,
	&exynos_device_bts_mfc,
#if defined(CONFIG_EXYNOS4_DEV_FIMC_IS)
	&exynos_device_bts_isp0,
	&exynos_device_bts_isp1,
#endif
};

static int __init exynos_bts_init(void)
{
	int i;
	for (i = 0; i < ARRAY_SIZE(exynos_bts); i++)
		exynos_bts[i]->id = i;

	return platform_add_devices(exynos_bts, ARRAY_SIZE(exynos_bts));
}
arch_initcall(exynos_bts_init);

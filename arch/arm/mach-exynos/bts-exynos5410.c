/*
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/clk.h>
#include <linux/list.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/pm_runtime.h>

#include <plat/devs.h>

#include <mach/map.h>
#include <mach/devfreq.h>

enum bts_index {
	BTS_IDX_FIMD1M0 = 0,
	BTS_IDX_FIMD1M1 = 1,
	BTS_IDX_FIMD1M2 = 2,
	BTS_IDX_TV0 = 3,
	BTS_IDX_TV1 = 4,
	BTS_IDX_FIMCLITE0 = 5,
	BTS_IDX_FIMCLITE1 = 6,
	BTS_IDX_FIMCLITE2 = 7,
	BTS_IDX_G2D = 8,
	BTS_IDX_G3D0 = 9,
	BTS_IDX_G3D1 = 10,
	BTS_IDX_MFC0 = 11,
	BTS_IDX_MFC1 = 12,
	BTS_IDX_GSCL0 = 13,
	BTS_IDX_GSCL1 = 14,
	BTS_IDX_GSCL2 = 15,
	BTS_IDX_GSCL3 = 16,
	BTS_IDX_GSCL4 = 17,
	BTS_IDX_EAGLE = 18,
	BTS_IDX_KFC = 19,
};

enum bts_id {
	BTS_FIMD1M0 = (1 << BTS_IDX_FIMD1M0),
	BTS_FIMD1M1 = (1 << BTS_IDX_FIMD1M1),
	BTS_FIMD1M2 = (1 << BTS_IDX_FIMD1M2),
	BTS_TV0 = (1 << BTS_IDX_TV0),
	BTS_TV1 = (1 << BTS_IDX_TV1),
	BTS_FIMCLITE0 = (1 << BTS_IDX_FIMCLITE0),
	BTS_FIMCLITE1 = (1 << BTS_IDX_FIMCLITE1),
	BTS_FIMCLITE2 = (1 << BTS_IDX_FIMCLITE2),
	BTS_G2D = (1 << BTS_IDX_G2D),
	BTS_G3D0 = (1 << BTS_IDX_G3D0),
	BTS_G3D1 = (1 << BTS_IDX_G3D1),
	BTS_MFC0 = (1 << BTS_IDX_MFC0),
	BTS_MFC1 = (1 << BTS_IDX_MFC1),
	BTS_GSCL0 = (1 << BTS_IDX_GSCL0),
	BTS_GSCL1 = (1 << BTS_IDX_GSCL1),
	BTS_GSCL2 = (1 << BTS_IDX_GSCL2),
	BTS_GSCL3 = (1 << BTS_IDX_GSCL3),
	BTS_GSCL4 = (1 << BTS_IDX_GSCL4),
	BTS_EAGLE = (1 << BTS_IDX_EAGLE),
	BTS_KFC = (1 << BTS_IDX_KFC),
};

#define BTS_FIMD1 (BTS_FIMD1M0 | BTS_FIMD1M1 | BTS_FIMD1M2)
#define BTS_TV (BTS_TV0 | BTS_TV1)
#define BTS_FIMCLITE (BTS_FIMCLITE0 | BTS_FIMCLITE2)
#define BTS_G3D (BTS_G3D0 | BTS_G3D1)
#define BTS_MFC (BTS_MFC0 | BTS_MFC1)
#define BTS_GSCL (BTS_GSCL0 | BTS_GSCL1|BTS_GSCL2 | BTS_GSCL3)

enum exynos_bts_scenario {
	BS_DEFAULT,
	BS_MIF800_FIMCIS,
	BS_MIF400_MFC,
	BS_MIF400_MFC_G3D,
	BS_MIF400_MFC_GSCL,
	BS_MIF400_MFC_GSCL_G3D,
	BS_MIF400_GSCL_G3D,
	BS_MIF400_G2D,
	BS_MIF400_G3D,
	BS_MIF400_G2D_G3D,
	BS_MIF200_MFC,
	BS_MIF200_MFC_G3D,
	BS_MIF200_MFC_GSCL,
	BS_MIF200_MFC_GSCL_G3D,
	BS_MIF200_GSCL_G3D,
	BS_MIF200_G2D,
	BS_MIF200_G3D,
	BS_MIF200_G2D_G3D,
	BS_DISABLE,
	BS_MAX,
};

struct bts_table {
	struct bts_set_table *table_list;
	unsigned int table_num;
};

struct bts_info {
	enum bts_id id;
	const char *name;
	unsigned int pa_base;
	void __iomem *va_base;
	struct bts_table table[BS_MAX];
	struct device *parent;
	const char *pd_name;
	const char *clk_name;
	struct clk *clk;
	bool on;
	unsigned int bts_val;
	struct list_head list;
	struct list_head scen_list;
};

struct bts_set_table {
	unsigned int reg;
	unsigned int val;
};

static struct bts_set_table def_highest_table[] = {
	{0x0, 0x0},
	{0x8, 0x10fff},
	{0x0, 0x1}
};

static struct bts_set_table def_high_table[] = {
	{0x0, 0x0},
	{0x8, 0x10ccc},
	{0x0, 0x1}
};

static struct bts_set_table def_low_table[] = {
	{0x0, 0x0},
	{0x8, 0x10444},
	{0x0, 0x1}
};

static struct bts_set_table disable_table[] = {
	{0x0, 0x0}
};

#define BTS_TABLE(num)	\
static struct bts_set_table p##num##_table[] = {	\
	{0x0, 0x0},	\
	{0x4, 0x1},	\
	{0x8, 0x10888},	\
	{0xc, num},	\
	{0x44, 0x3ff},	\
	{0x0, 0x5},	\
}
BTS_TABLE(32);
BTS_TABLE(16);
BTS_TABLE(12);
BTS_TABLE(10);
BTS_TABLE(8);
BTS_TABLE(6);
BTS_TABLE(5);
BTS_TABLE(4);
BTS_TABLE(3);
BTS_TABLE(2);

static struct bts_info exynos5_bts[] = {
	[BTS_IDX_FIMD1M0] = {
		.id = BTS_FIMD1M0,
		.name = "fimd1m0",
		.pa_base = EXYNOS5_PA_BTS_FIMD1M0,
		.clk_name = "lcd",
		.parent = &s5p_device_fimd1.dev,
		.pd_name = "pd-disp1",
		.table[BS_MIF800_FIMCIS].table_list = def_high_table,
		.table[BS_MIF800_FIMCIS].table_num = ARRAY_SIZE(def_high_table),
		.table[BS_DEFAULT].table_list = def_highest_table,
		.table[BS_DEFAULT].table_num = ARRAY_SIZE(def_highest_table),
		.on = true,
	},
	[BTS_IDX_FIMD1M1] = {
		.id = BTS_FIMD1M1,
		.name = "fimd1m1",
		.pa_base = EXYNOS5_PA_BTS_FIMD1M1,
		.parent = &s5p_device_fimd1.dev,
		.clk_name = "lcd",
		.pd_name = "pd-disp1",
		.table[BS_MIF800_FIMCIS].table_list = def_high_table,
		.table[BS_MIF800_FIMCIS].table_num = ARRAY_SIZE(def_high_table),
		.table[BS_DEFAULT].table_list = def_highest_table,
		.table[BS_DEFAULT].table_num = ARRAY_SIZE(def_highest_table),
		.on = true,
	},
	[BTS_IDX_FIMD1M2] = {
		.id = BTS_FIMD1M2,
		.name = "fimd1m2",
		.pa_base = EXYNOS5_PA_BTS_FIMD1M2,
		.parent = &s5p_device_fimd1.dev,
		.clk_name = "lcd",
		.pd_name = "pd-disp1",
		.table[BS_MIF800_FIMCIS].table_list = def_high_table,
		.table[BS_MIF800_FIMCIS].table_num = ARRAY_SIZE(def_high_table),
		.table[BS_DEFAULT].table_list = def_highest_table,
		.table[BS_DEFAULT].table_num = ARRAY_SIZE(def_highest_table),
		.on = true,
	},
	[BTS_IDX_TV0] = {
		.id = BTS_TV0,
		.name = "tv0",
		.pa_base = EXYNOS5_PA_BTS_MIXER0,
		.parent = &s5p_device_mixer.dev,
		.clk_name = "mixer",
		.pd_name = "pd-disp1",
		.table[BS_MIF800_FIMCIS].table_list = def_high_table,
		.table[BS_MIF800_FIMCIS].table_num = ARRAY_SIZE(def_high_table),
		.table[BS_DEFAULT].table_list = def_highest_table,
		.table[BS_DEFAULT].table_num = ARRAY_SIZE(def_highest_table),
		.on = true,
	},
	[BTS_IDX_TV1] = {
		.id = BTS_TV1,
		.name = "tv1",
		.pa_base = EXYNOS5_PA_BTS_MIXER1,
		.parent = &s5p_device_mixer.dev,
		.clk_name = "mixer",
		.pd_name = "pd-disp1",
		.table[BS_MIF800_FIMCIS].table_list = def_high_table,
		.table[BS_MIF800_FIMCIS].table_num = ARRAY_SIZE(def_high_table),
		.table[BS_DEFAULT].table_list = def_highest_table,
		.table[BS_DEFAULT].table_num = ARRAY_SIZE(def_highest_table),
		.on = true,
	},
#ifdef CONFIG_VIDEO_EXYNOS5_FIMC_IS
	[BTS_IDX_FIMCLITE0] = {
		.id = BTS_FIMCLITE0,
		.name = "fimclite0",
		.pa_base = EXYNOS5_PA_BTS_FIMCLITE0,
		.clk_name = "gscl_flite0",
		.pd_name = "pd-isp",
		.parent = &exynos5_device_fimc_is.dev,
		.table[BS_MIF800_FIMCIS].table_list = def_highest_table,
		.table[BS_MIF800_FIMCIS].table_num = ARRAY_SIZE(def_highest_table),
		.on = true,
	},
	[BTS_IDX_FIMCLITE2] = {
		.id = BTS_FIMCLITE2,
		.name = "fimclite2",
		.pa_base = EXYNOS5_PA_BTS_FIMCLITE2,
		.clk_name = "gscl_flite2",
		.pd_name = "pd-isp",
		.parent = &exynos5_device_fimc_is.dev,
		.table[BS_MIF800_FIMCIS].table_list = def_highest_table,
		.table[BS_MIF800_FIMCIS].table_num = ARRAY_SIZE(def_highest_table),
		.on = true,
	},
#endif
#ifdef CONFIG_VIDEO_EXYNOS_MFC
	[BTS_IDX_MFC0] = {
		.id = BTS_MFC0,
		.name = "mfc0",
		.pa_base = EXYNOS5_PA_BTS_MFC0,
		.clk_name = "mfc",
		.pd_name = "pd-mfc",
		.parent = &s5p_device_mfc.dev,
		.table[BS_MIF800_FIMCIS].table_list = p4_table,
		.table[BS_MIF800_FIMCIS].table_num = ARRAY_SIZE(p4_table),
		.table[BS_MIF400_MFC].table_list = p8_table,
		.table[BS_MIF400_MFC].table_num = ARRAY_SIZE(p8_table),
		.table[BS_MIF400_MFC_G3D].table_list = p8_table,
		.table[BS_MIF400_MFC_G3D].table_num = ARRAY_SIZE(p8_table),
		.table[BS_MIF400_MFC_GSCL].table_list = p8_table,
		.table[BS_MIF400_MFC_GSCL].table_num = ARRAY_SIZE(p8_table),
		.table[BS_MIF400_MFC_GSCL_G3D].table_list = p8_table,
		.table[BS_MIF400_MFC_GSCL_G3D].table_num = ARRAY_SIZE(p8_table),
		.table[BS_MIF200_MFC].table_list = p8_table,
		.table[BS_MIF200_MFC].table_num = ARRAY_SIZE(p8_table),
		.table[BS_MIF200_MFC_G3D].table_list = p8_table,
		.table[BS_MIF200_MFC_G3D].table_num = ARRAY_SIZE(p8_table),
		.table[BS_MIF200_MFC_GSCL].table_list = p8_table,
		.table[BS_MIF200_MFC_GSCL].table_num = ARRAY_SIZE(p8_table),
		.table[BS_MIF200_MFC_GSCL_G3D].table_list = p8_table,
		.table[BS_MIF200_MFC_GSCL_G3D].table_num = ARRAY_SIZE(p8_table),
		.table[BS_DISABLE].table_list = disable_table,
		.table[BS_DISABLE].table_num = ARRAY_SIZE(disable_table),
		.on = true,
	},
	[BTS_IDX_MFC1] = {
		.id = BTS_MFC1,
		.name = "mfc1",
		.pa_base = EXYNOS5_PA_BTS_MFC1,
		.clk_name = "mfc",
		.pd_name = "pd-mfc",
		.parent = &s5p_device_mfc.dev,
		.table[BS_MIF800_FIMCIS].table_list = p4_table,
		.table[BS_MIF800_FIMCIS].table_num = ARRAY_SIZE(p4_table),
		.table[BS_MIF400_MFC].table_list = p8_table,
		.table[BS_MIF400_MFC].table_num = ARRAY_SIZE(p8_table),
		.table[BS_MIF400_MFC_G3D].table_list = p8_table,
		.table[BS_MIF400_MFC_G3D].table_num = ARRAY_SIZE(p8_table),
		.table[BS_MIF400_MFC_GSCL].table_list = p8_table,
		.table[BS_MIF400_MFC_GSCL].table_num = ARRAY_SIZE(p8_table),
		.table[BS_MIF400_MFC_GSCL_G3D].table_list = p8_table,
		.table[BS_MIF400_MFC_GSCL_G3D].table_num = ARRAY_SIZE(p8_table),
		.table[BS_MIF200_MFC].table_list = p8_table,
		.table[BS_MIF200_MFC].table_num = ARRAY_SIZE(p8_table),
		.table[BS_MIF200_MFC_G3D].table_list = p8_table,
		.table[BS_MIF200_MFC_G3D].table_num = ARRAY_SIZE(p8_table),
		.table[BS_MIF200_MFC_GSCL].table_list = p8_table,
		.table[BS_MIF200_MFC_GSCL].table_num = ARRAY_SIZE(p8_table),
		.table[BS_MIF200_MFC_GSCL_G3D].table_list = p8_table,
		.table[BS_MIF200_MFC_GSCL_G3D].table_num = ARRAY_SIZE(p8_table),
		.table[BS_DISABLE].table_list = disable_table,
		.table[BS_DISABLE].table_num = ARRAY_SIZE(disable_table),
		.on = true,
	},
#endif
	[BTS_IDX_G3D0] = {
		.id = BTS_G3D0,
		.name = "g3d0",
		.pa_base = EXYNOS5_PA_BTS_G3D0,
		.clk_name = "sgx_hyd",
		.pd_name = "pd-g3d",
		.parent = &exynos5_device_g3d.dev,
		.table[BS_MIF800_FIMCIS].table_list = p4_table,
		.table[BS_MIF800_FIMCIS].table_num = ARRAY_SIZE(p4_table),
		.table[BS_MIF400_MFC_G3D].table_list = p8_table,
		.table[BS_MIF400_MFC_G3D].table_num = ARRAY_SIZE(p8_table),
		.table[BS_MIF400_MFC_GSCL_G3D].table_list = p8_table,
		.table[BS_MIF400_MFC_GSCL_G3D].table_num = ARRAY_SIZE(p8_table),
		.table[BS_MIF400_GSCL_G3D].table_list = p8_table,
		.table[BS_MIF400_GSCL_G3D].table_num = ARRAY_SIZE(p8_table),
		.table[BS_MIF400_G3D].table_list = p2_table,
		.table[BS_MIF400_G3D].table_num = ARRAY_SIZE(p2_table),
		.table[BS_MIF400_G2D_G3D].table_list = p4_table,
		.table[BS_MIF400_G2D_G3D].table_num = ARRAY_SIZE(p4_table),
		.table[BS_MIF200_MFC_G3D].table_list = p32_table,
		.table[BS_MIF200_MFC_G3D].table_num = ARRAY_SIZE(p32_table),
		.table[BS_MIF200_MFC_GSCL_G3D].table_list = p32_table,
		.table[BS_MIF200_MFC_GSCL_G3D].table_num = ARRAY_SIZE(p32_table),
		.table[BS_MIF200_GSCL_G3D].table_list = p32_table,
		.table[BS_MIF200_GSCL_G3D].table_num = ARRAY_SIZE(p32_table),
		.table[BS_MIF200_G3D].table_list = p12_table,
		.table[BS_MIF200_G3D].table_num = ARRAY_SIZE(p12_table),
		.table[BS_MIF200_G2D_G3D].table_list = p16_table,
		.table[BS_MIF200_G2D_G3D].table_num = ARRAY_SIZE(p16_table),
		.table[BS_DISABLE].table_list = disable_table,
		.table[BS_DISABLE].table_num = ARRAY_SIZE(disable_table),
		.on = true,
	},
	[BTS_IDX_G3D1] = {
		.id = BTS_G3D1,
		.name = "g3d1",
		.pa_base = EXYNOS5_PA_BTS_G3D1,
		.clk_name = "sgx_hyd",
		.pd_name = "pd-g3d",
		.parent = &exynos5_device_g3d.dev,
		.table[BS_MIF800_FIMCIS].table_list = p4_table,
		.table[BS_MIF800_FIMCIS].table_num = ARRAY_SIZE(p4_table),
		.table[BS_MIF400_MFC_G3D].table_list = p8_table,
		.table[BS_MIF400_MFC_G3D].table_num = ARRAY_SIZE(p8_table),
		.table[BS_MIF400_MFC_GSCL_G3D].table_list = p8_table,
		.table[BS_MIF400_MFC_GSCL_G3D].table_num = ARRAY_SIZE(p8_table),
		.table[BS_MIF400_GSCL_G3D].table_list = p8_table,
		.table[BS_MIF400_GSCL_G3D].table_num = ARRAY_SIZE(p8_table),
		.table[BS_MIF400_G3D].table_list = p2_table,
		.table[BS_MIF400_G3D].table_num = ARRAY_SIZE(p2_table),
		.table[BS_MIF400_G2D_G3D].table_list = p4_table,
		.table[BS_MIF400_G2D_G3D].table_num = ARRAY_SIZE(p4_table),
		.table[BS_MIF200_MFC_G3D].table_list = p32_table,
		.table[BS_MIF200_MFC_G3D].table_num = ARRAY_SIZE(p32_table),
		.table[BS_MIF200_MFC_GSCL_G3D].table_list = p32_table,
		.table[BS_MIF200_MFC_GSCL_G3D].table_num = ARRAY_SIZE(p32_table),
		.table[BS_MIF200_GSCL_G3D].table_list = p32_table,
		.table[BS_MIF200_GSCL_G3D].table_num = ARRAY_SIZE(p32_table),
		.table[BS_MIF200_G3D].table_list = p12_table,
		.table[BS_MIF200_G3D].table_num = ARRAY_SIZE(p12_table),
		.table[BS_MIF200_G2D_G3D].table_list = p16_table,
		.table[BS_MIF200_G2D_G3D].table_num = ARRAY_SIZE(p16_table),
		.table[BS_DISABLE].table_list = disable_table,
		.table[BS_DISABLE].table_num = ARRAY_SIZE(disable_table),
		.on = true,
	},
#ifdef CONFIG_VIDEO_EXYNOS_FIMG2D
	[BTS_IDX_G2D] = {
		.id = BTS_G2D,
		.name = "g2d",
		.pa_base = EXYNOS5_PA_BTS_G2D,
		.clk_name = "fimg2d",
		.pd_name = "pd-g2d",
		.parent = &s5p_device_fimg2d.dev,
		.table[BS_MIF400_G2D].table_list = p2_table,
		.table[BS_MIF400_G2D].table_num = ARRAY_SIZE(p2_table),
		.table[BS_MIF400_G2D_G3D].table_list = p3_table,
		.table[BS_MIF400_G2D_G3D].table_num = ARRAY_SIZE(p3_table),
		.table[BS_MIF200_G2D].table_list = p8_table,
		.table[BS_MIF200_G2D].table_num = ARRAY_SIZE(p8_table),
		.table[BS_MIF200_G2D_G3D].table_list = p10_table,
		.table[BS_MIF200_G2D_G3D].table_num = ARRAY_SIZE(p10_table),
		.table[BS_DISABLE].table_list = disable_table,
		.table[BS_DISABLE].table_num = ARRAY_SIZE(disable_table),
		.on = true,
	},
#endif
#ifdef CONFIG_VIDEO_EXYNOS_GSCALER
	[BTS_IDX_GSCL0] = {
		.id = BTS_GSCL0,
		.name = "gscl0",
		.pa_base = EXYNOS5_PA_BTS_GSCL0,
		.clk_name = "gscl",
		.pd_name = "pd-gscl",
		.parent = &exynos5_device_gsc0.dev,
		.table[BS_MIF400_MFC_GSCL].table_list = p6_table,
		.table[BS_MIF400_MFC_GSCL].table_num = ARRAY_SIZE(p6_table),
		.table[BS_MIF400_MFC_GSCL_G3D].table_list = p6_table,
		.table[BS_MIF400_MFC_GSCL_G3D].table_num = ARRAY_SIZE(p6_table),
		.table[BS_MIF400_GSCL_G3D].table_list = p5_table,
		.table[BS_MIF400_GSCL_G3D].table_num = ARRAY_SIZE(p5_table),
		.table[BS_MIF200_MFC_GSCL].table_list = p5_table,
		.table[BS_MIF200_MFC_GSCL].table_num = ARRAY_SIZE(p5_table),
		.table[BS_MIF200_MFC_GSCL_G3D].table_list = p6_table,
		.table[BS_MIF200_MFC_GSCL_G3D].table_num = ARRAY_SIZE(p6_table),
		.table[BS_MIF200_GSCL_G3D].table_list = p6_table,
		.table[BS_MIF200_GSCL_G3D].table_num = ARRAY_SIZE(p6_table),
		.table[BS_DISABLE].table_list = disable_table,
		.table[BS_DISABLE].table_num = ARRAY_SIZE(disable_table),
		.on = true,
	},
	[BTS_IDX_GSCL1] = {
		.id = BTS_GSCL1,
		.name = "gsc1l",
		.pa_base = EXYNOS5_PA_BTS_GSCL1,
		.clk_name = "gscl",
		.pd_name = "pd-gscl",
		.parent = &exynos5_device_gsc1.dev,
		.table[BS_MIF400_MFC_GSCL].table_list = p6_table,
		.table[BS_MIF400_MFC_GSCL].table_num = ARRAY_SIZE(p6_table),
		.table[BS_MIF400_MFC_GSCL_G3D].table_list = p6_table,
		.table[BS_MIF400_MFC_GSCL_G3D].table_num = ARRAY_SIZE(p6_table),
		.table[BS_MIF400_GSCL_G3D].table_list = p5_table,
		.table[BS_MIF400_GSCL_G3D].table_num = ARRAY_SIZE(p5_table),
		.table[BS_MIF200_MFC_GSCL].table_list = p5_table,
		.table[BS_MIF200_MFC_GSCL].table_num = ARRAY_SIZE(p5_table),
		.table[BS_MIF200_MFC_GSCL_G3D].table_list = p6_table,
		.table[BS_MIF200_MFC_GSCL_G3D].table_num = ARRAY_SIZE(p6_table),
		.table[BS_MIF200_GSCL_G3D].table_list = p6_table,
		.table[BS_MIF200_GSCL_G3D].table_num = ARRAY_SIZE(p6_table),
		.table[BS_DISABLE].table_list = disable_table,
		.table[BS_DISABLE].table_num = ARRAY_SIZE(disable_table),
		.on = true,
	},
	[BTS_IDX_GSCL2] = {
		.id = BTS_GSCL2,
		.name = "gscl2",
		.pa_base = EXYNOS5_PA_BTS_GSCL2,
		.clk_name = "gscl",
		.pd_name = "pd-gscl",
		.parent = &exynos5_device_gsc2.dev,
		.table[BS_MIF400_MFC_GSCL].table_list = p6_table,
		.table[BS_MIF400_MFC_GSCL].table_num = ARRAY_SIZE(p6_table),
		.table[BS_MIF400_MFC_GSCL_G3D].table_list = p6_table,
		.table[BS_MIF400_MFC_GSCL_G3D].table_num = ARRAY_SIZE(p6_table),
		.table[BS_MIF400_GSCL_G3D].table_list = p5_table,
		.table[BS_MIF400_GSCL_G3D].table_num = ARRAY_SIZE(p5_table),
		.table[BS_MIF200_MFC_GSCL].table_list = p5_table,
		.table[BS_MIF200_MFC_GSCL].table_num = ARRAY_SIZE(p5_table),
		.table[BS_MIF200_MFC_GSCL_G3D].table_list = p6_table,
		.table[BS_MIF200_MFC_GSCL_G3D].table_num = ARRAY_SIZE(p6_table),
		.table[BS_MIF200_GSCL_G3D].table_list = p6_table,
		.table[BS_MIF200_GSCL_G3D].table_num = ARRAY_SIZE(p6_table),
		.table[BS_DISABLE].table_list = disable_table,
		.table[BS_DISABLE].table_num = ARRAY_SIZE(disable_table),
		.on = true,
	},
	[BTS_IDX_GSCL3] = {
		.id = BTS_GSCL3,
		.name = "gscl3",
		.pa_base = EXYNOS5_PA_BTS_GSCL3,
		.clk_name = "gscl",
		.pd_name = "pd-gscl",
		.parent = &exynos5_device_gsc3.dev,
		.table[BS_MIF400_MFC_GSCL].table_list = p6_table,
		.table[BS_MIF400_MFC_GSCL].table_num = ARRAY_SIZE(p6_table),
		.table[BS_MIF400_MFC_GSCL_G3D].table_list = p6_table,
		.table[BS_MIF400_MFC_GSCL_G3D].table_num = ARRAY_SIZE(p6_table),
		.table[BS_MIF400_GSCL_G3D].table_list = p5_table,
		.table[BS_MIF400_GSCL_G3D].table_num = ARRAY_SIZE(p5_table),
		.table[BS_MIF200_MFC_GSCL].table_list = p5_table,
		.table[BS_MIF200_MFC_GSCL].table_num = ARRAY_SIZE(p5_table),
		.table[BS_MIF200_MFC_GSCL_G3D].table_list = p6_table,
		.table[BS_MIF200_MFC_GSCL_G3D].table_num = ARRAY_SIZE(p6_table),
		.table[BS_MIF200_GSCL_G3D].table_list = p6_table,
		.table[BS_MIF200_GSCL_G3D].table_num = ARRAY_SIZE(p6_table),
		.table[BS_DISABLE].table_list = disable_table,
		.table[BS_DISABLE].table_num = ARRAY_SIZE(disable_table),
		.on = true,
	},
#endif
	[BTS_IDX_EAGLE] = {
		.id = BTS_EAGLE,
		.name = "eagle",
		.pa_base = EXYNOS5_PA_BTS_EAGLE,
		.pd_name = "pd-eagle",
		.table[BS_DEFAULT].table_list = def_low_table,
		.table[BS_DEFAULT].table_num = ARRAY_SIZE(def_low_table),
		.on = true,
	},
	[BTS_IDX_KFC] = {
		.id = BTS_KFC,
		.name = "kfc",
		.pa_base = EXYNOS5_PA_BTS_KFC,
		.pd_name = "pd-kfc",
		.table[BS_DEFAULT].table_list = def_low_table,
		.table[BS_DEFAULT].table_num = ARRAY_SIZE(def_low_table),
		.on = true,
	},
};

struct bts_scenario {
	const char *name;
	unsigned int ip;
	enum exynos_bts_scenario id;
};

static struct bts_scenario bts_scen[] = {
	[BS_DEFAULT] = {
		.name = "bts_default",
		.ip = BTS_FIMD1 | BTS_TV | BTS_FIMCLITE | BTS_KFC | BTS_EAGLE,
		.id = BS_DEFAULT,
	},
	[BS_MIF800_FIMCIS] = {
		.name = "bts_800_fimcis",
		.ip = BTS_FIMCLITE | BTS_G3D | BTS_MFC | BTS_FIMD1 | BTS_TV,
		.id = BS_MIF800_FIMCIS,
	},
	[BS_MIF400_MFC] = {
		.name = "bts_400_mfcl",
		.ip = BTS_MFC,
		.id = BS_MIF400_MFC,
	},
	[BS_MIF400_MFC_G3D] = {
		.name = "bts_400_mfc_g3d",
		.ip = BTS_MFC | BTS_G3D,
		.id = BS_MIF400_MFC_G3D,
	},
	[BS_MIF400_MFC_GSCL] = {
		.name = "bts_400_mfc_gscl",
		.ip = BTS_MFC | BTS_GSCL,
		.id = BS_MIF400_MFC_GSCL,
	},
	[BS_MIF400_MFC_GSCL_G3D] = {
		.name = "bts_400_mfc_gscl_g3d",
		.ip =  BTS_MFC | BTS_GSCL | BTS_G3D,
		.id = BS_MIF400_MFC_GSCL_G3D,
	},
	[BS_MIF400_GSCL_G3D] = {
		.name = "bts_400_gscl_g3d",
		.ip =  BTS_GSCL | BTS_G3D,
		.id = BS_MIF400_GSCL_G3D,
	},
	[BS_MIF400_G2D] = {
		.name = "bts_400_g2d",
		.ip = BTS_G2D,
		.id = BS_MIF400_G2D,
	},
	[BS_MIF400_G3D] = {
		.name = "bts_400_g3d",
		.ip = BTS_G3D,
		.id = BS_MIF400_G3D,
	},
	[BS_MIF400_G2D_G3D] = {
		.name = "bts_400_g2d_g3d",
		.ip = BTS_G2D | BTS_G3D,
		.id = BS_MIF400_G2D_G3D,
	},
	[BS_MIF200_MFC] = {
		.name = "bts_200_mfcl",
		.ip = BTS_MFC,
		.id = BS_MIF200_MFC,
	},
	[BS_MIF200_MFC_G3D] = {
		.name = "bts_200_mfc_g3d",
		.ip = BTS_MFC | BTS_G3D,
		.id = BS_MIF200_MFC_G3D,
	},
	[BS_MIF200_MFC_GSCL] = {
		.name = "bts_200_mfc_gscl",
		.ip = BTS_MFC | BTS_GSCL,
		.id = BS_MIF200_MFC_GSCL,
	},
	[BS_MIF200_MFC_GSCL_G3D] = {
		.name = "bts_200_mfc_gscl_g3d",
		.ip =  BTS_MFC | BTS_GSCL | BTS_G3D,
		.id = BS_MIF200_MFC_GSCL_G3D,
	},
	[BS_MIF200_GSCL_G3D] = {
		.name = "bts_200_gscl_g3d",
		.ip =  BTS_GSCL | BTS_G3D,
		.id = BS_MIF200_GSCL_G3D,
	},
	[BS_MIF200_G2D] = {
		.name = "bts_200_g2d",
		.ip = BTS_G2D,
		.id = BS_MIF200_G2D,
	},
	[BS_MIF200_G3D] = {
		.name = "bts_200_g3d",
		.ip = BTS_G3D,
		.id = BS_MIF200_G3D,
	},
	[BS_MIF200_G2D_G3D] = {
		.name = "bts_200_g2d_g3d",
		.ip = BTS_G2D | BTS_G3D,
		.id = BS_MIF200_G2D_G3D,
	},
	[BS_DISABLE] = {
		.name = "bts_disable",
		.ip = BTS_MFC | BTS_GSCL | BTS_G2D | BTS_G3D,
		.id = BS_DISABLE,
	},
	[BS_MAX] = {
		.name = "undefined"
	}
};

enum scen_act_status {
	SCEN_NO_ACTIVE,
	SCEN_ACTIVE,
	SCEN_DISABLE,
};

struct bts_scen_status {
	enum exynos_bts_scenario scen;
	unsigned int mif;
	enum scen_act_status active;
	bool active_bw; /* turn on if bw is bigger than FHD */
	bool bts_g2d_on;
};

static struct bts_scen_status pr_state = {
	.active = SCEN_DISABLE,
};
static DEFINE_SPINLOCK(bts_lock);
static LIST_HEAD(bts_list);
static LIST_HEAD(bts_scen_list);
static struct clk *sgx_core_clk;

#define MIF_800 (800000)
#define MIF_400 (400000)
#define MIF_200 (200000)
#define FHD_BW_PER_WINDOW (1920 * 1080 * 4 * 60)
#define is_bts_scen_ip(a) (a & (BTS_GSCL | BTS_MFC | BTS_G2D | BTS_G3D | BTS_FIMCLITE | BTS_FIMD1 | BTS_TV))
#define is_bts_scen_ip_with_long_table(a) (a & (BTS_GSCL | BTS_MFC | BTS_G2D | BTS_G3D))
#define update_scen(a) (pr_state.scen = a)
#define update_mif(a) (pr_state.mif = a)
#define update_active(a) (pr_state.active = a)
#define update_active_bw(a) (pr_state.active_bw = a)
#define get_active_bw(a) ((a > FHD_BW_PER_WINDOW) ? true : false)
#define LONG_TABLE_INDEX (3)
#define SHORT_TABLE_INDEX (1)

#ifdef BTS_DBGGEN
#define BTS_DBG(x...) printk(x)
#else
#define BTS_DBG(x...) do {} while (0)
#endif

static void set_bts_ip_table(enum exynos_bts_scenario scen,
		struct bts_info *bts)
{
	int i;
	unsigned int val;
	struct bts_set_table *table = bts->table[scen].table_list;

	if (!bts->on && (bts->id != BTS_G2D))
		return;

	if (scen == BS_DISABLE)
		bts->bts_val = 0;

	/* no set in case that BTS G2D already turns off */
	if (bts->id == BTS_G2D) {
		if (scen == BS_DISABLE) {
			if (pr_state.bts_g2d_on) {
				pr_state.bts_g2d_on = 0;
				goto set_bts;
			} else {
				return;
			}
		} else {
			pr_state.bts_g2d_on = 1;
		}
	}

	/* no set in case that previous setting is same with new setting */
	if (scen != BS_DISABLE) {
		if (is_bts_scen_ip(bts->id)) {
			if (is_bts_scen_ip_with_long_table(bts->id))
				val = table[LONG_TABLE_INDEX].val;
			else
				val = table[SHORT_TABLE_INDEX].val;
		} else {
			goto set_bts;
		}

		BTS_DBG("[BTS] check: %s: %x->%x\n", bts->name, bts->bts_val, val);
		if (val == bts->bts_val)
			return;
		else
			bts->bts_val = val;
	}

set_bts:
	BTS_DBG("[BTS] set: %s in %s\n", bts->name, bts_scen[scen].name);
	if (bts->id & BTS_G3D)
		if (sgx_core_clk)
			clk_enable(sgx_core_clk);

	if (bts->clk)
		clk_enable(bts->clk);

	for (i = 0; i < bts->table[scen].table_num; i++) {
		__raw_writel(table->val, bts->va_base + table->reg);
		BTS_DBG("[BTS] %x-%x\n", table->reg, table->val);
		table++;
	}

	if (bts->clk)
		clk_disable(bts->clk);

	if (bts->id & BTS_G3D)
		if (sgx_core_clk)
			clk_disable(sgx_core_clk);
}

static void set_bts_scen_table(enum exynos_bts_scenario scen)
{
	struct bts_info *bts;

	list_for_each_entry(bts, &bts_scen_list, scen_list)
		if (bts->id & bts_scen[scen].ip)
			set_bts_ip_table(scen, bts);
}

static enum scen_act_status get_active(unsigned int mif)
{
	enum scen_act_status new_active = SCEN_ACTIVE;
	bool fimcis = exynos5_bts[BTS_IDX_FIMCLITE0].on;

	/* bts is always active in case of fimc-is */
	if (fimcis)
		goto out;

	switch (mif) {
	case MIF_200:
		new_active = pr_state.active_bw ? SCEN_NO_ACTIVE : SCEN_ACTIVE;
		break;
	case MIF_400:
		new_active = pr_state.active_bw ? SCEN_ACTIVE : SCEN_NO_ACTIVE;
		break;
	case MIF_800:
		new_active = fimcis ? SCEN_ACTIVE : SCEN_NO_ACTIVE;
		break;
	default:
		break;
	}

out:
	return new_active;
}

static enum exynos_bts_scenario get_scen(unsigned int mif)
{
	enum exynos_bts_scenario scen = BS_DISABLE;
	bool gscl = exynos5_bts[BTS_IDX_GSCL0].on;
	bool mfc = exynos5_bts[BTS_IDX_MFC0].on;
	bool g2d = exynos5_bts[BTS_IDX_G2D].on;
	bool g3d = exynos5_bts[BTS_IDX_G3D0].on;
	bool fimcis = exynos5_bts[BTS_IDX_FIMCLITE0].on;

	if (fimcis) {
		scen = BS_MIF800_FIMCIS;
		goto out;
	}

	switch (mif) {
	case MIF_400:
		if (mfc && gscl && !g2d && !g3d)
			scen = BS_MIF400_MFC_GSCL;
		else if (mfc && gscl && !g2d && g3d)
			scen = BS_MIF400_MFC_GSCL_G3D;
		else if (!mfc && !gscl && g2d && !g3d)
			scen = BS_MIF400_G2D;
		else if (!mfc && !gscl && !g2d && g3d)
			scen = BS_MIF400_G3D;
		else if (!mfc && !gscl && g2d && g3d)
			scen = BS_MIF400_G2D_G3D;
		else if (mfc && !gscl && !g2d && !g3d)
			scen = BS_MIF400_MFC;
		else if (mfc && !gscl && !g2d && g3d)
			scen = BS_MIF400_MFC_G3D;
		else if (!mfc && gscl && !g2d && g3d)
			scen = BS_MIF400_GSCL_G3D;
		break;
	case MIF_200:
		if (mfc && gscl && !g2d && !g3d)
			scen = BS_MIF200_MFC_GSCL;
		else if (mfc && gscl && !g2d && g3d)
			scen = BS_MIF200_MFC_GSCL_G3D;
		else if (!mfc && !gscl && g2d && !g3d)
			scen = BS_MIF200_G2D;
		else if (!mfc && !gscl && !g2d && g3d)
			scen = BS_MIF200_G3D;
		else if (!mfc && !gscl && g2d && g3d)
			scen = BS_MIF200_G2D_G3D;
		else if (mfc && !gscl && !g2d && !g3d)
			scen = BS_MIF200_MFC;
		else if (mfc && !gscl && !g2d && g3d)
			scen = BS_MIF200_MFC_G3D;
		else if (!mfc && gscl && !g2d && g3d)
			scen = BS_MIF200_GSCL_G3D;
		break;
	default:
		break;
	}

out:
	BTS_DBG("[%s] scen: %s -> %s, mif:%d gscl:%x mfc:%x 2d:%x 3d:%x, fimc:%x\n",
		__func__, bts_scen[pr_state.scen].name, bts_scen[scen].name,
		mif, gscl, mfc, g2d, g3d, fimcis);

	return scen;
}

static bool need_to_set_new_scen(enum exynos_bts_scenario scen)
{
	bool need = true;

	switch (scen) {
	case BS_MIF400_MFC:
		if ((pr_state.scen == BS_MIF400_MFC_G3D) ||
			(pr_state.scen == BS_MIF400_MFC_GSCL))
			need = false;
		break;
	case BS_MIF400_MFC_G3D:
		if (pr_state.scen == BS_MIF400_MFC_GSCL_G3D)
			need = false;
		break;
	case BS_MIF400_MFC_GSCL:
		if (pr_state.scen == BS_MIF400_MFC_GSCL_G3D)
			need = false;
		break;
	case BS_MIF400_GSCL_G3D:
		if (pr_state.scen == BS_MIF400_MFC_GSCL_G3D)
			need = false;
		break;
	case BS_MIF200_MFC:
		if ((pr_state.scen == BS_MIF200_MFC_G3D) ||
			(pr_state.scen == BS_MIF200_MFC_GSCL))
			need = false;
		break;
	case BS_MIF200_MFC_G3D:
		if (pr_state.scen == BS_MIF200_MFC_GSCL_G3D)
			need = false;
		break;
	case BS_MIF200_MFC_GSCL:
		if (pr_state.scen == BS_MIF200_MFC_GSCL_G3D)
			need = false;
		break;
	case BS_MIF200_GSCL_G3D:
		if (pr_state.scen == BS_MIF200_MFC_GSCL_G3D)
			need = false;
		break;
	default:
		BTS_DBG("[%s] Unsupported BTS Scenario\n", __func__);
		break;
	}

	if ((scen == BS_DISABLE) && (pr_state.scen == scen))
		need = false;

	update_scen(scen);

	return need;
}

static void set_scen_bts_table(void)
{
	enum exynos_bts_scenario scen;
	enum scen_act_status new_active;

	new_active = get_active(pr_state.mif);
	BTS_DBG("[%s] act:%d->%d, bw:%d, mif:%d\n", __func__,
		pr_state.active, new_active, pr_state.active_bw, pr_state.mif);

	/* check active status */
	if (new_active == SCEN_NO_ACTIVE) {
		if (pr_state.active == SCEN_ACTIVE) {
			scen = BS_DISABLE;
		} else {
			update_active(new_active);
			return;
		}
	} else {
		scen = get_scen(pr_state.mif);
	}

	/* set new scenario */
	 if (need_to_set_new_scen(scen))
		set_bts_scen_table(scen);

	update_active(new_active);
}

void bts_change_bustraffic(struct devfreq_info *info, unsigned long event)
{
	unsigned int mif = 0;

	switch (event) {
	case MIF_DEVFREQ_PRECHANGE:
		if (info->old > info->new)
			mif = info->new;
		break;
	case MIF_DEVFREQ_POSTCHANGE:
		if (info->old < info->new)
			mif = info->new;
		break;
	default:
		break;
	}

	if (!mif)
		return;

	spin_lock(&bts_lock);
	BTS_DBG("[%s] mif:%d\n", __func__, mif);
	update_mif(mif);
	set_scen_bts_table();
	spin_unlock(&bts_lock);
}

void bts_set_bw(unsigned int bw)
{
	bool active_bw = get_active_bw(bw);

	if (active_bw == pr_state.active_bw)
		return;

	spin_lock(&bts_lock);
	BTS_DBG("[%s] active_bw:%d\n", __func__, active_bw);
	update_active_bw(active_bw);
	set_scen_bts_table();
	spin_unlock(&bts_lock);
}

void bts_initialize(char *pd_name, bool on)
{
	struct bts_info *bts;
	enum exynos_bts_scenario scen = BS_DEFAULT;
	bool scen_ip = false;
	bool fimcis_off = false;
	bool g2d_off = false;
	bool check_config = false;

	spin_lock(&bts_lock);

	BTS_DBG("[%s] %s, on:%d, act:%d, act_bw:%d, mif:%d\n", __func__,
		pd_name, on, pr_state.active, pr_state.active_bw, pr_state.mif);

	/* update on/off flags */
	list_for_each_entry(bts, &bts_list, list)
		if (pd_name && bts->pd_name &&
			!strcmp(bts->pd_name, pd_name)) {
			if (!check_config) {
				check_config = true;
				/* check scen ip */
				if (is_bts_scen_ip(bts->id)) {
					scen_ip = true;
					if ((bts->id & (BTS_FIMD1 | BTS_TV)) &&
						(!exynos5_bts[BTS_IDX_FIMCLITE0].on))
						scen_ip = false;
				}
				/* check fimc is */
				if (bts->id & BTS_FIMCLITE) {
					update_active(SCEN_ACTIVE);
					if (on == false)
						fimcis_off = true;
				}
				/* check g2d */
				if ((bts->id & BTS_G2D) && (on == false))
					g2d_off = true;
			}
			/* update on/off flags */
			bts->on = on;
			if (!bts->on)
				bts->bts_val = 0;
		}

	/* find BTS scenario */
	if (scen_ip) {
		if (pr_state.active == SCEN_NO_ACTIVE) {
			spin_unlock(&bts_lock);
			return;
		}
		scen = get_scen(pr_state.mif);
	}

	/* set BTS table */
	if ((scen == BS_DEFAULT) && pd_name) {
		list_for_each_entry(bts, &bts_list, list)
			if (bts->pd_name && !strcmp(bts->pd_name, pd_name))
				set_bts_ip_table(scen, bts);
	} else if (need_to_set_new_scen(scen)) {
		set_bts_scen_table(scen);
	}

	/* recovery FIMD1 & TV bts setting from FIMCIS */
	if (fimcis_off) {
		list_for_each_entry(bts, &bts_scen_list, scen_list)
			if (bts->id & (BTS_FIMD1|BTS_TV))
				set_bts_ip_table(BS_DEFAULT, bts);
		update_active(SCEN_NO_ACTIVE);
	}

	if (g2d_off)
		set_bts_ip_table(BS_DISABLE, &exynos5_bts[BTS_IDX_G2D]);

	spin_unlock(&bts_lock);
}

static int __init exynos5_bts_init(void)
{
	int i;
	struct clk *clk;

	for (i = 0; i < ARRAY_SIZE(exynos5_bts); i++) {
		exynos5_bts[i].va_base
			= ioremap(exynos5_bts[i].pa_base, SZ_512);

		if (exynos5_bts[i].clk_name) {
			clk = clk_get(exynos5_bts[i].parent,
				exynos5_bts[i].clk_name);
			if (IS_ERR(clk))
				pr_err("failed to get bts clk %s\n",
					exynos5_bts[i].clk_name);
			else
				exynos5_bts[i].clk = clk;
		}

		list_add(&exynos5_bts[i].list, &bts_list);
		if (is_bts_scen_ip(exynos5_bts[i].id))
			list_add(&exynos5_bts[i].scen_list, &bts_scen_list);
	}

	sgx_core_clk = clk_get(exynos5_bts[BTS_IDX_G3D0].parent, "sgx_core");
	if (IS_ERR(sgx_core_clk))
		pr_err("failed to get sgx_core clk for bts g3d\n");

	bts_initialize("pd-eagle", true);
	bts_initialize("pd-kfc", true);
	return 0;
}
arch_initcall(exynos5_bts_init);

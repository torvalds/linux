/*
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef	__EXYNOS_BTS_H_
#define	__EXYNOS_BTS_H_

#include <plat/pd.h>

enum exynos_bts_id {
	BTS_CPU,
	BTS_DISP,
	BTS_DISP10,
	BTS_DISP11,
	BTS_TV,
	BTS_TV0,
	BTS_TV1,
	BTS_C2C,
	BTS_JPEG,
	BTS_MDMA1,
	BTS_ROTATOR,
	BTS_GSCL,
	BTS_GSCL0,
	BTS_GSCL1,
	BTS_GSCL2,
	BTS_GSCL3,
	BTS_MFC,
	BTS_MFC0,
	BTS_MFC1,
	BTS_G3D_ACP,
	BTS_ISP0,
	BTS_ISP1,
	BTS_FIMC_ISP,
	BTS_FIMC_FD,
	BTS_FIMC_ODC,
	BTS_FIMC_DIS0,
	BTS_FIMC_DIS1,
	BTS_3DNR,
	BTS_SCALER_C,
	BTS_SCALER_P
};

enum bts_priority {
	BTS_LOW,
	BTS_BE,
	BTS_HARDTIME,
};

enum bts_fbm_group {
	BTS_FBM_G0_L = (1<<1),
	BTS_FBM_G0_R = (1<<2),
	BTS_FBM_G1_L = (1<<3),
	BTS_FBM_G1_R = (1<<4),
	BTS_FBM_G2_L = (1<<5),
	BTS_FBM_G2_R = (1<<6),
};

struct exynos_fbm_resource {
	enum bts_fbm_group fbm_group;
	enum bts_priority priority;
	u32 base;
};

struct exynos_fbm_pdata {
	struct exynos_fbm_resource *res;
	int res_num;
};

struct exynos_bts_pdata {
	enum exynos_bts_id id;
	enum bts_priority def_priority;
	enum exynos_pd_block pd_block;
	char *clk_name;
	struct exynos_fbm_pdata *fbm;
	int res_num;
};

/* BTS functions */
void exynos_bts_enable(enum exynos_pd_block pd_block);
void exynos_bts_set_priority(enum bts_priority prior);
#ifdef CONFIG_S5P_BTS
#define bts_enable(a) exynos_bts_enable(a);
#else
#define bts_enable(a) do {} while (0)
#endif
#endif	/* __EXYNOS_BTS_H_ */

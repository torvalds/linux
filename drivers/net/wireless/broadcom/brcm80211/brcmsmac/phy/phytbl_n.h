// SPDX-License-Identifier: ISC
/*
 * Copyright (c) 2010 Broadcom Corporation
 */

#define ANT_SWCTRL_TBL_REV3_IDX (0)

#include <types.h>
#include "phy_int.h"

extern const struct phytbl_info mimophytbl_info_rev0[],
				mimophytbl_info_rev0_volatile[];

extern const u32 mimophytbl_info_sz_rev0,
		 mimophytbl_info_sz_rev0_volatile;

extern const struct phytbl_info mimophytbl_info_rev3[],
				mimophytbl_info_rev3_volatile[],
				mimophytbl_info_rev3_volatile1[],
				mimophytbl_info_rev3_volatile2[],
				mimophytbl_info_rev3_volatile3[];

extern const u32 mimophytbl_info_sz_rev3,
		 mimophytbl_info_sz_rev3_volatile,
		 mimophytbl_info_sz_rev3_volatile1,
		 mimophytbl_info_sz_rev3_volatile2,
		 mimophytbl_info_sz_rev3_volatile3;

extern const u32 noise_var_tbl_rev3[];

extern const struct phytbl_info mimophytbl_info_rev7[];

extern const u32 mimophytbl_info_sz_rev7;

extern const u32 noise_var_tbl_rev7[];

extern const struct phytbl_info mimophytbl_info_rev16[];

extern const u32 mimophytbl_info_sz_rev16;

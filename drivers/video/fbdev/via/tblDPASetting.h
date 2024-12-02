/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright 1998-2008 VIA Technologies, Inc. All Rights Reserved.
 * Copyright 2001-2008 S3 Graphics, Inc. All Rights Reserved.

 */

#ifndef _TBLDPASETTING_H_
#define _TBLDPASETTING_H_
#include "global.h"

#define DPA_CLK_30M       30000000
#define DPA_CLK_50M       50000000
#define DPA_CLK_70M       70000000
#define DPA_CLK_100M      100000000
#define DPA_CLK_150M      150000000

enum DPA_RANGE {
	DPA_CLK_RANGE_30M,
	DPA_CLK_RANGE_30_50M,
	DPA_CLK_RANGE_50_70M,
	DPA_CLK_RANGE_70_100M,
	DPA_CLK_RANGE_100_150M,
	DPA_CLK_RANGE_150M
};

extern struct GFX_DPA_SETTING GFX_DPA_SETTING_TBL_VT3324[6];
extern struct GFX_DPA_SETTING GFX_DPA_SETTING_TBL_VT3327[];
extern struct GFX_DPA_SETTING GFX_DPA_SETTING_TBL_VT3364[6];

#endif

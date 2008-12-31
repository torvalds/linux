/*
 * Copyright 1998-2008 VIA Technologies, Inc. All Rights Reserved.
 * Copyright 2001-2008 S3 Graphics, Inc. All Rights Reserved.

 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation;
 * either version 2, or (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTIES OR REPRESENTATIONS; without even
 * the implied warranty of MERCHANTABILITY or FITNESS FOR
 * A PARTICULAR PURPOSE.See the GNU General Public License
 * for more details.

 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc.,
 * 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
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

extern struct VT1636_DPA_SETTING VT1636_DPA_SETTING_TBL_VT3324[7];
extern struct GFX_DPA_SETTING GFX_DPA_SETTING_TBL_VT3324[6];
extern struct VT1636_DPA_SETTING VT1636_DPA_SETTING_TBL_VT3327[7];
extern struct GFX_DPA_SETTING GFX_DPA_SETTING_TBL_VT3327[];
extern struct GFX_DPA_SETTING GFX_DPA_SETTING_TBL_VT3364[6];

#endif

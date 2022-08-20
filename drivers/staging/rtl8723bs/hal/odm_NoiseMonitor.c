// SPDX-License-Identifier: GPL-2.0
/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 *
 ******************************************************************************/

#include "odm_precomp.h"

/*  This function is for inband noise test utility only */
/*  To obtain the inband noise level(dbm), do the following. */
/*  1. disable DIG and Power Saving */
/*  2. Set initial gain = 0x1a */
/*  3. Stop updating idle time pwer report (for driver read) */
/* - 0x80c[25] */

#define Valid_Min				-35
#define Valid_Max			10
#define ValidCnt				5

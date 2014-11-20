/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 ******************************************************************************/

#ifndef	__ODM_PRECOMP_H__
#define __ODM_PRECOMP_H__

/* 2 Config Flags and Structs - defined by each ODM Type */

#include <osdep_service.h>
#include <drv_types.h>
#include <hal_intf.h>


/* 2 Hardware Parameter Files */
#include "Hal8723UHWImg_CE.h"


/* 2 OutSrc Header Files */

#include "odm.h"
#include "odm_HWConfig.h"
#include "odm_debug.h"
#include "odm_RegDefine11N.h"

#include "HalDMOutSrc8723A.h" /* for IQK,LCK,Power-tracking */
#include "rtl8723a_hal.h"

#include "odm_interface.h"
#include "odm_reg.h"

#include "HalHWImg8723A_MAC.h"
#include "HalHWImg8723A_RF.h"
#include "HalHWImg8723A_BB.h"
#include "HalHWImg8723A_FW.h"
#include "odm_RegConfig8723A.h"

#endif	/*  __ODM_PRECOMP_H__ */

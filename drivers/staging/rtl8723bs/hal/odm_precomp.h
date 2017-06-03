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

#include "odm_types.h"

#define		TEST_FALG___		1

/* 2 Config Flags and Structs - defined by each ODM Type */

	/* include <basic_types.h> */
	/* include <osdep_service.h> */
	/* include <drv_types.h> */
	/* include <rtw_byteorder.h> */
	/* include <hal_intf.h> */
#define BEAMFORMING_SUPPORT 0

/* 2 Hardware Parameter Files */

/* 2 OutSrc Header Files */

#include "odm.h"
#include "odm_HWConfig.h"
#include "odm_debug.h"
#include "odm_RegDefine11N.h"
#include "odm_AntDiv.h"
#include "odm_EdcaTurboCheck.h"
#include "odm_DIG.h"
#include "odm_PathDiv.h"
#include "odm_DynamicBBPowerSaving.h"
#include "odm_DynamicTxPower.h"
#include "odm_CfoTracking.h"
#include "odm_NoiseMonitor.h"
#include "HalPhyRf.h"
#include "HalPhyRf_8723B.h"/* for IQK, LCK, Power-tracking */
#include "rtl8723b_hal.h"
#include "odm_interface.h"
#include "odm_reg.h"
#include "HalHWImg8723B_MAC.h"
#include "HalHWImg8723B_RF.h"
#include "HalHWImg8723B_BB.h"
#include "Hal8723BReg.h"
#include "odm_RTL8723B.h"
#include "odm_RegConfig8723B.h"

#endif	/*  __ODM_PRECOMP_H__ */

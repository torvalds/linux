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
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 *
 ******************************************************************************/

#ifndef	__ODM_PRECOMP_H__
#define __ODM_PRECOMP_H__

#include "odm_types.h"

#if (DM_ODM_SUPPORT_TYPE == ODM_MP)
#include "Precomp.h"		// We need to include mp_precomp.h due to batch file setting.

#else

#define		TEST_FALG___		1

#endif

//2 Config Flags and Structs - defined by each ODM Type

#if (DM_ODM_SUPPORT_TYPE == ODM_AP)
	#include "../8192cd_cfg.h"
	#include "../odm_inc.h"

	#include "../8192cd.h"
	#include "../8192cd_util.h"
	#ifdef _BIG_ENDIAN_
	#define	ODM_ENDIAN_TYPE				ODM_ENDIAN_BIG
	#else
	#define	ODM_ENDIAN_TYPE				ODM_ENDIAN_LITTLE
	#endif

	#ifdef AP_BUILD_WORKAROUND
	#include "../8192cd_headers.h"
	#include "../8192cd_debug.h"		
	#endif
	
#elif (DM_ODM_SUPPORT_TYPE == ODM_ADSL)
	// Flags
	#include "../8192cd_cfg.h"		// OUTSRC needs ADSL config flags.
	#include "../odm_inc.h"			// OUTSRC needs some extra flags.
	// Data Structure
	#include "../common_types.h"	// OUTSRC and rtl8192cd both needs basic type such as UINT8 and BIT0.
	#include "../8192cd.h"			// OUTSRC needs basic ADSL struct definition.
	#include "../8192cd_util.h"		// OUTSRC needs basic I/O function.
	#ifdef _BIG_ENDIAN_
	#define	ODM_ENDIAN_TYPE				ODM_ENDIAN_BIG
	#else
	#define	ODM_ENDIAN_TYPE				ODM_ENDIAN_LITTLE
	#endif

	#ifdef ADSL_AP_BUILD_WORKAROUND
	// NESTED_INC: Functions defined outside should not be included!! Marked by Annie, 2011-10-14.
	#include "../8192cd_headers.h"
	#include "../8192cd_debug.h"	
	#endif	
	
#elif (DM_ODM_SUPPORT_TYPE ==ODM_CE)
	#include <drv_conf.h>
	#include <osdep_service.h>
	#include <drv_types.h>
	#include <rtw_byteorder.h>
	#include <hal_intf.h>	
		
#elif (DM_ODM_SUPPORT_TYPE == ODM_MP)
	#include "Mp_Precomp.h"
	#define	ODM_ENDIAN_TYPE				ODM_ENDIAN_LITTLE
#endif

 
//2 Hardware Parameter Files


#if (DM_ODM_SUPPORT_TYPE == ODM_AP)
#if (RTL8192C_SUPPORT==1)
	#include "rtl8192c/Hal8192CEFWImg_AP.h"
	#include "rtl8192c/Hal8192CEPHYImg_AP.h"
	#include "rtl8192c/Hal8192CEMACImg_AP.h"
#endif
#elif (DM_ODM_SUPPORT_TYPE == ODM_ADSL)
	#include "rtl8192c/Hal8192CEFWImg_ADSL.h"
	#include "rtl8192c/Hal8192CEPHYImg_ADSL.h"
	#include "rtl8192c/Hal8192CEMACImg_ADSL.h"

#elif (DM_ODM_SUPPORT_TYPE == ODM_CE)
	#if(RTL8192CE_SUPPORT ==1)
	#include "rtl8192c/Hal8192CEFWImg_CE.h"
	#include "rtl8192c/Hal8192CEPHYImg_CE.h"
	#include "rtl8192c/Hal8192CEMACImg_CE.h"	
	#elif(RTL8192CU_SUPPORT ==1)
	#include "rtl8192c/Hal8192CUFWImg_CE.h"
	#include "rtl8192c/Hal8192CUPHYImg_CE.h"
	#include "rtl8192c/Hal8192CUMACImg_CE.h"
	#elif(RTL8192DE_SUPPORT ==1)
	#include "rtl8192d/Hal8192DEFWImg_CE.h"
	#include "rtl8192d/Hal8192DEPHYImg_CE.h"
	#include "rtl8192d/Hal8192DEMACImg_CE.h"	
	#elif(RTL8192DU_SUPPORT ==1)
	#include "rtl8192d/Hal8192DUFWImg_CE.h"
	#include "rtl8192d/Hal8192DUPHYImg_CE.h"
	#include "rtl8192d/Hal8192DUMACImg_CE.h"	
	#elif(RTL8723AS_SUPPORT==1)
	#include "rtl8723a/Hal8723SHWImg_CE.h"
	#elif(RTL8723AU_SUPPORT==1)
	#include "rtl8723a/Hal8723UHWImg_CE.h"	 
	#elif(RTL8188E_SUPPORT==1)
	#include "rtl8188e/Hal8188EFWImg_CE.h"	
	#endif
#elif (DM_ODM_SUPPORT_TYPE == ODM_MP)

#endif


//2 OutSrc Header Files

#include "odm.h"
#include "odm_HWConfig.h"
#include "odm_debug.h"
#include "odm_RegDefine11AC.h"
#include "odm_RegDefine11N.h"

#if (DM_ODM_SUPPORT_TYPE == ODM_AP)
#if (RTL8192C_SUPPORT==1)
	#include "rtl8192c/HalDMOutSrc8192C_AP.h"
#endif
#if (RTL8188E_SUPPORT==1)
		#include "rtl8188e/Hal8188ERateAdaptive.h"//for  RA,Power training
#endif

#elif (DM_ODM_SUPPORT_TYPE == ODM_ADSL)
	#include "rtl8192c/HalDMOutSrc8192C_ADSL.h"

#elif (DM_ODM_SUPPORT_TYPE == ODM_CE)
	#include "HalPhyRf.h"
	#if (RTL8192C_SUPPORT==1) 
		#ifdef CONFIG_INTEL_PROXIM
		#include "../proxim/intel_proxim.h"	
		#endif	
		#include  "rtl8192c/HalDMOutSrc8192C_CE.h"
		#include <rtl8192c_hal.h>
	#elif (RTL8192D_SUPPORT==1)
		#include  "rtl8192d/HalDMOutSrc8192D_CE.h"
		#include  "rtl8192d_hal.h"
	#elif (RTL8723A_SUPPORT==1)
		#include "rtl8192c/HalDMOutSrc8192C_CE.h" //for IQK,LCK,Power-tracking
		#include "rtl8723a_hal.h"  
	#elif (RTL8188E_SUPPORT==1)		
		#include "rtl8188e/HalPhyRf_8188e.h"//for IQK,LCK,Power-tracking
		#include "rtl8188e/Hal8188ERateAdaptive.h"//for  RA,Power training
		#include "rtl8188e_hal.h"  	
	#endif

#endif

#include "odm_interface.h"
#include "odm_reg.h"

#if (RTL8192C_SUPPORT==1) 
#if (DM_ODM_SUPPORT_TYPE == ODM_AP)
#include "rtl8192c/Hal8192CHWImg_MAC.h"
#include "rtl8192c/Hal8192CHWImg_RF.h"
#include "rtl8192c/Hal8192CHWImg_BB.h"
#include "rtl8192c/Hal8192CHWImg_FW.h"
#endif
#include "rtl8192c/odm_RTL8192C.h"
#endif
#if (RTL8192D_SUPPORT==1) 
#include "rtl8192d/odm_RTL8192D.h"
#endif

#if (RTL8723A_SUPPORT==1) 
#include "rtl8723a/HalHWImg8723A_MAC.h"
#include "rtl8723a/HalHWImg8723A_RF.h"
#include "rtl8723a/HalHWImg8723A_BB.h"
#include "rtl8723a/HalHWImg8723A_FW.h"
#include "rtl8723a/odm_RegConfig8723A.h"
#endif

#if (RTL8188E_SUPPORT==1) 
#include "rtl8188e/HalHWImg8188E_MAC.h"
#include "rtl8188e/HalHWImg8188E_RF.h"
#include "rtl8188e/HalHWImg8188E_BB.h"
#include "rtl8188e/Hal8188EReg.h"

#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
#include "rtl8188e/HalPhyRf_8188e.h"
#endif

#if (RTL8188E_FOR_TEST_CHIP >= 1) 
#include "rtl8188e/HalHWImg8188E_TestChip_MAC.h"
#include "rtl8188e/HalHWImg8188E_TestChip_RF.h"
#include "rtl8188e/HalHWImg8188E_TestChip_BB.h"
#endif

#ifdef CONFIG_WOWLAN
#if (RTL8188E_SUPPORT==1)
#include "rtl8188e/HalHWImg8188E_FW.h"
#endif
#endif //CONFIG_WOWLAN

#include "rtl8188e/odm_RegConfig8188E.h"
#include "rtl8188e/odm_RTL8188E.h"
#endif

#endif	// __ODM_PRECOMP_H__


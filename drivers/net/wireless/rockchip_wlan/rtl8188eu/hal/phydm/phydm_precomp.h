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

#include "phydm_types.h"
#include "phydm_features.h"

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
#include "Precomp.h"		// We need to include mp_precomp.h due to batch file setting.
#else
#define		TEST_FALG___		1
#endif

#if (DM_ODM_SUPPORT_TYPE ==ODM_CE) 
#define 	RTL8192CE_SUPPORT 				0
#define 	RTL8192CU_SUPPORT 				0
#define 	RTL8192C_SUPPORT 				0	

#define 	RTL8192DE_SUPPORT 				0
#define 	RTL8192DU_SUPPORT 				0
#define 	RTL8192D_SUPPORT 				0	

#define 	RTL8723AU_SUPPORT				0
#define 	RTL8723AS_SUPPORT				0
#define 	RTL8723AE_SUPPORT				0
#define 	RTL8723A_SUPPORT				0
#define 	RTL8723_FPGA_VERIFICATION		0
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

#elif (DM_ODM_SUPPORT_TYPE ==ODM_CE)
	#define __PACK
	#define __WLAN_ATTRIB_PACK__
#elif (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	#include "mp_precomp.h"
	#define	ODM_ENDIAN_TYPE				ODM_ENDIAN_LITTLE
	#define __PACK
	#define __WLAN_ATTRIB_PACK__
#endif

//2 OutSrc Header Files
 
#include "phydm.h" 
#include "phydm_hwconfig.h"
#include "phydm_debug.h"
#include "phydm_regdefine11ac.h"
#include "phydm_regdefine11n.h"
#include "phydm_interface.h"
#include "phydm_reg.h"


#if (DM_ODM_SUPPORT_TYPE & ODM_CE)
#define RTL8821B_SUPPORT		0
#define RTL8822B_SUPPORT		0

VOID
PHY_SetTxPowerLimit(
	IN	PDM_ODM_T	pDM_Odm,
	IN	u8	*Regulation,
	IN	u8	*Band,
	IN	u8	*Bandwidth,
	IN	u8	*RateSection,
	IN	u8	*RfPath,
	IN	u8	*Channel,
	IN	u8	*PowerLimit
);

#endif

#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
#define RTL8821B_SUPPORT		0
#define RTL8822B_SUPPORT		0
#define RTL8703B_SUPPORT		0
#define RTL8188F_SUPPORT		0
#endif

#if RTL8188E_SUPPORT == 1
#define RTL8188E_T_SUPPORT 1
#ifdef CONFIG_SFW_SUPPORTED
#define RTL8188E_S_SUPPORT 1
#else
#define RTL8188E_S_SUPPORT 0
#endif
#endif

#if (RTL8188E_SUPPORT==1) 
#include "rtl8188e/hal8188erateadaptive.h"//for  RA,Power training
#include "rtl8188e/halhwimg8188e_mac.h"
#include "rtl8188e/halhwimg8188e_rf.h"
#include "rtl8188e/halhwimg8188e_bb.h"
#include "rtl8188e/halhwimg8188e_t_fw.h"
#include "rtl8188e/halhwimg8188e_s_fw.h"
#include "rtl8188e/phydm_regconfig8188e.h"
#include "rtl8188e/phydm_rtl8188e.h"
#include "rtl8188e/hal8188ereg.h"
#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
	#include "rtl8188e_hal.h" 
	#include "rtl8188e/halphyrf_8188e_ce.h"
#endif
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	#include "rtl8188e/halphyrf_8188e_win.h"
#endif
#if (DM_ODM_SUPPORT_TYPE == ODM_AP)
	#include "rtl8188e/halphyrf_8188e_ap.h"
#endif
#endif  //88E END

#if (RTL8192E_SUPPORT==1) 

	#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
		#include "rtl8192e/halphyrf_8192e_win.h" /*FOR_8192E_IQK*/
	#elif (DM_ODM_SUPPORT_TYPE == ODM_AP)
		#include "rtl8192e/halphyrf_8192e_ap.h" /*FOR_8192E_IQK*/
	#elif (DM_ODM_SUPPORT_TYPE == ODM_CE)
		#include "rtl8192e/halphyrf_8192e_ce.h" /*FOR_8192E_IQK*/
	#endif
	
#include "rtl8192e/phydm_rtl8192e.h" //FOR_8192E_IQK
#if (DM_ODM_SUPPORT_TYPE != ODM_AP)
	#include "rtl8192e/halhwimg8192e_bb.h"
	#include "rtl8192e/halhwimg8192e_mac.h"
	#include "rtl8192e/halhwimg8192e_rf.h"
	#include "rtl8192e/phydm_regconfig8192e.h"
	#include "rtl8192e/halhwimg8192e_fw.h"
	#include "rtl8192e/hal8192ereg.h"
#endif
#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
	#include "rtl8192e_hal.h"
#endif
#endif  //92E END

#if (RTL8812A_SUPPORT==1)

    #if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
        #include "rtl8812a/halphyrf_8812a_win.h"
    #elif (DM_ODM_SUPPORT_TYPE == ODM_AP)
        #include "rtl8812a/halphyrf_8812a_ap.h"
    #elif (DM_ODM_SUPPORT_TYPE == ODM_CE)
        #include "rtl8812a/halphyrf_8812a_ce.h"
    #endif

    //#include "rtl8812a/HalPhyRf_8812A.h" //FOR_8812_IQK
    #if (DM_ODM_SUPPORT_TYPE != ODM_AP)
        #include "rtl8812a/halhwimg8812a_bb.h"
        #include "rtl8812a/halhwimg8812a_mac.h"
        #include "rtl8812a/halhwimg8812a_rf.h"
        #include "rtl8812a/phydm_regconfig8812a.h"
        #include "rtl8812a/halhwimg8812a_fw.h"
        #include "rtl8812a/phydm_rtl8812a.h"
    #endif

    #if (DM_ODM_SUPPORT_TYPE == ODM_CE)
	    #include "rtl8812a_hal.h"
    #endif

#endif //8812 END

#if (RTL8814A_SUPPORT==1)

#include "rtl8814a/halhwimg8814a_mac.h"
#include "rtl8814a/halhwimg8814a_rf.h"
#include "rtl8814a/halhwimg8814a_bb.h"
#if (DM_ODM_SUPPORT_TYPE != ODM_AP)
	#include "rtl8814a/halhwimg8814a_fw.h"
	#include "rtl8814a/phydm_rtl8814a.h"
#endif
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	#include "rtl8814a/halphyrf_8814a_win.h"
#elif (DM_ODM_SUPPORT_TYPE == ODM_CE)
	#include "rtl8814a/halphyrf_8814a_ce.h"
#elif (DM_ODM_SUPPORT_TYPE == ODM_AP)
	#include "rtl8814a/halphyrf_8814a_ap.h"
#endif
	#include "rtl8814a/phydm_regconfig8814a.h"
#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
	#include "rtl8814a_hal.h"
	#include "rtl8814a/phydm_iqk_8814a.h"
#endif
#endif //8814 END

#if (RTL8881A_SUPPORT==1)//FOR_8881_IQK
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
#include "rtl8821a/phydm_iqk_8821a_win.h"
#elif (DM_ODM_SUPPORT_TYPE == ODM_CE)
#include "rtl8821a/phydm_iqk_8821a_ce.h"
#else
#include "rtl8821a/phydm_iqk_8821a_ap.h"
#endif
//#include "rtl8881a/HalHWImg8881A_BB.h"
//#include "rtl8881a/HalHWImg8881A_MAC.h"
//#include "rtl8881a/HalHWImg8881A_RF.h"
//#include "rtl8881a/odm_RegConfig8881A.h"
#endif

#if (RTL8723B_SUPPORT==1) 
#include "rtl8723b/halhwimg8723b_mac.h"
#include "rtl8723b/halhwimg8723b_rf.h"
#include "rtl8723b/halhwimg8723b_bb.h"
#include "rtl8723b/halhwimg8723b_fw.h"
#include "rtl8723b/phydm_regconfig8723b.h"
#include "rtl8723b/phydm_rtl8723b.h"
#include "rtl8723b/hal8723breg.h"
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
    #include "rtl8723b/halphyrf_8723b_win.h"
#elif (DM_ODM_SUPPORT_TYPE == ODM_CE)
    #include "rtl8723b/halphyrf_8723b_ce.h"
    #include "rtl8723b/halhwimg8723b_mp.h"
    #include "rtl8723b_hal.h"
#endif
#endif

#if (RTL8821A_SUPPORT==1) 
#include "rtl8821a/halhwimg8821a_mac.h"
#include "rtl8821a/halhwimg8821a_rf.h"
#include "rtl8821a/halhwimg8821a_bb.h"
#include "rtl8821a/halhwimg8821a_fw.h"
#include "rtl8821a/phydm_regconfig8821a.h"
#include "rtl8821a/phydm_rtl8821a.h"
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	#include "rtl8821a/halphyrf_8821a_win.h"
#elif (DM_ODM_SUPPORT_TYPE == ODM_CE)
	#include "rtl8821a/halphyrf_8821a_ce.h"
	#include "rtl8821a/phydm_iqk_8821a_ce.h"/*for IQK*/
	#include "rtl8812a/halphyrf_8812a_ce.h"/*for IQK,LCK,Power-tracking*/
	#include "rtl8812a_hal.h"
#else
#endif
#endif

#if (RTL8821B_SUPPORT==1) 
#include "rtl8821b/halhwimg8821b_mac.h"
#include "rtl8821b/halhwimg8821b_rf.h"
#include "rtl8821b/halhwimg8821b_bb.h"
#include "rtl8821b/halhwimg8821b_fw.h"
#include "rtl8821b/phydm_regconfig8821b.h"
#include "rtl8821b/halhwimg8821b_testchip_mac.h"
#include "rtl8821b/halhwimg8821b_testchip_rf.h"
#include "rtl8821b/halhwimg8821b_testchip_bb.h"
#include "rtl8821b/halhwimg8821b_testchip_fw.h"
#include "rtl8821b/halphyrf_8821b.h"
#endif

#if (RTL8822B_SUPPORT==1) 
#include "rtl8822b/halhwimg8822b_mac.h"
#include "rtl8822b/halhwimg8822b_rf.h"
#include "rtl8822b/halhwimg8822b_bb.h"
/*#include "rtl8822b/halhwimg8822b_fw.h"*/
#include "rtl8822b/phydm_regconfig8822b.h"
#include "rtl8822b/halphyrf_8822b.h"
#include "rtl8822b/phydm_rtl8822b.h"
#include "rtl8822b/phydm_hal_api8822b.h"
#include "rtl8822b/version_rtl8822b.h"
#endif

#if (RTL8703B_SUPPORT==1) 
#include "rtl8703b/phydm_regconfig8703b.h"
#include "rtl8703b/halhwimg8703b_mac.h"
#include "rtl8703b/halhwimg8703b_rf.h"
#include "rtl8703b/halhwimg8703b_bb.h"
#include "rtl8703b/halhwimg8703b_fw.h"
#include "rtl8703b/halphyrf_8703b.h"
#include "rtl8703b/version_rtl8703b.h"
#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
#include "rtl8703b_hal.h"
#endif
#endif

#if (RTL8188F_SUPPORT == 1) 
#include "rtl8188f/halhwimg8188f_mac.h"
#include "rtl8188f/halhwimg8188f_rf.h"
#include "rtl8188f/halhwimg8188f_bb.h"
#include "rtl8188f/halhwimg8188f_fw.h"
#include "rtl8188f/hal8188freg.h"
#include "rtl8188f/phydm_rtl8188f.h"
#include "rtl8188f/phydm_regconfig8188f.h"
#include "rtl8188f/halphyrf_8188f.h" /* for IQK,LCK,Power-tracking */
#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
#include "rtl8188f_hal.h"
#endif
#endif

#endif	// __ODM_PRECOMP_H__


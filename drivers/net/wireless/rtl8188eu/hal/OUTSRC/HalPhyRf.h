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
 
 #ifndef __HAL_PHY_RF_H__
 #define __HAL_PHY_RF_H__
 
 #if(DM_ODM_SUPPORT_TYPE & ODM_MP)
 #define MAX_TOLERANCE		5
 #define IQK_DELAY_TIME		1		//ms
 
 //
// BB/MAC/RF other monitor API
//

void	PHY_SetMonitorMode8192C(IN	PADAPTER	pAdapter,
										IN	BOOLEAN		bEnableMonitorMode	);
										
//
// IQ calibrate
//
void	
PHY_IQCalibrate_8192C(		IN	PADAPTER	pAdapter,	
							IN	BOOLEAN 	bReCovery);
							
//
// LC calibrate
//
void	
PHY_LCCalibrate_8192C(		IN	PADAPTER	pAdapter);

//
// AP calibrate
//
void	
PHY_APCalibrate_8192C(		IN	PADAPTER	pAdapter,
								IN 	s1Byte		delta);
#endif

#define ODM_TARGET_CHNL_NUM_2G_5G	59


VOID
ODM_ResetIQKResult(
	IN PDM_ODM_T	pDM_Odm 
);
u1Byte 
ODM_GetRightChnlPlaceforIQK(
    IN u1Byte chnl
);


#endif	// #ifndef __HAL_PHY_RF_H__


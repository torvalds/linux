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
#ifndef	__ODM_RTL8192C_H__
#define __ODM_RTL8192C_H__


VOID
odm_ResetFACounter_92C(
	IN		PDM_ODM_T		pDM_Odm
	);

#if (DM_ODM_SUPPORT_TYPE & ODM_MP)


//
// ==================================================
// Tx power tracking relative code.
// ==================================================
//

VOID
odm_TXPowerTrackingCallbackThermalMeter92C(
	IN PADAPTER	Adapter
	);


VOID
odm_TXPowerTrackingCallbackRXGainThermalMeter92D(
	IN PADAPTER 	Adapter
	);

VOID
odm_TXPowerTrackingDirectCall92C(
	IN	PADAPTER		Adapter
	);

VOID
odm_TXPowerTrackingDirectCall92C(
	IN	PADAPTER		Adapter
	);

VOID
odm_TXPowerTrackingCallback_ThermalMeter_92C(
	IN PADAPTER	Adapter
	);

VOID
odm_TXPowerTrackingCallback_ThermalMeter_8723A(
    IN PADAPTER	Adapter
    );

//
// ==================================================
// Tx power tracking relative code.
// ==================================================
//

void
ODM_RF_Saving_8188E(
	IN 	PDM_ODM_T	pDM_Odm,
	IN	u1Byte	bForceInNormal 
	);


#endif


#endif


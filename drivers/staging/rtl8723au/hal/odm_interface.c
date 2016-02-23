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

/*  */
/*  include files */
/*  */

#include "odm_precomp.h"
/*  */
/*  ODM IO Relative API. */
/*  */
#include <usb_ops_linux.h>

void ODM_SetRFReg(
	struct dm_odm_t *pDM_Odm,
	enum RF_RADIO_PATH	eRFPath,
	u32				RegAddr,
	u32				BitMask,
	u32				Data
	)
{
	struct rtw_adapter *Adapter = pDM_Odm->Adapter;

	PHY_SetRFReg(Adapter, eRFPath, RegAddr, BitMask, Data);
}

u32 ODM_GetRFReg(
	struct dm_odm_t *pDM_Odm,
	enum RF_RADIO_PATH	eRFPath,
	u32				RegAddr,
	u32				BitMask
	)
{
	struct rtw_adapter *Adapter = pDM_Odm->Adapter;

	return PHY_QueryRFReg(Adapter, eRFPath, RegAddr, BitMask);
}

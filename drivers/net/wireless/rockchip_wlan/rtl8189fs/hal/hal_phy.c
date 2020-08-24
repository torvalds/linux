/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2007 - 2017 Realtek Corporation.
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
 *****************************************************************************/
#define _HAL_PHY_C_

#include <drv_types.h>

/**
* Function:	PHY_CalculateBitShift
*
* OverView:	Get shifted position of the BitMask
*
* Input:
*			u32		BitMask,
*
* Output:	none
* Return:		u32		Return the shift bit bit position of the mask
*/
u32
PHY_CalculateBitShift(
	u32 BitMask
)
{
	u32 i;

	for (i = 0; i <= 31; i++) {
		if (((BitMask >> i) &  0x1) == 1)
			break;
	}

	return i;
}


#ifdef CONFIG_RF_SHADOW_RW
/* ********************************************************************************
 *	Constant.
 * ********************************************************************************
 * 2008/11/20 MH For Debug only, RF */
static RF_SHADOW_T RF_Shadow[RF6052_MAX_PATH][RF6052_MAX_REG];

/*
 * ==> RF shadow Operation API Code Section!!!
 *
 *-----------------------------------------------------------------------------
 * Function:	PHY_RFShadowRead
 *				PHY_RFShadowWrite
 *				PHY_RFShadowCompare
 *				PHY_RFShadowRecorver
 *				PHY_RFShadowCompareAll
 *				PHY_RFShadowRecorverAll
 *				PHY_RFShadowCompareFlagSet
 *				PHY_RFShadowRecorverFlagSet
 *
 * Overview:	When we set RF register, we must write shadow at first.
 *			When we are running, we must compare shadow abd locate error addr.
 *			Decide to recorver or not.
 *
 * Input:       NONE
 *
 * Output:      NONE
 *
 * Return:      NONE
 *
 * Revised History:
 * When			Who		Remark
 * 11/20/2008	MHC		Create Version 0.
 *
 *---------------------------------------------------------------------------*/
u32
PHY_RFShadowRead(
		PADAPTER		Adapter,
		enum rf_path		eRFPath,
		u32				Offset)
{
	return	RF_Shadow[eRFPath][Offset].Value;

}	/* PHY_RFShadowRead */


void
PHY_RFShadowWrite(
		PADAPTER		Adapter,
		enum rf_path		eRFPath,
		u32				Offset,
		u32				Data)
{
	RF_Shadow[eRFPath][Offset].Value = (Data & bRFRegOffsetMask);
	RF_Shadow[eRFPath][Offset].Driver_Write = _TRUE;

}	/* PHY_RFShadowWrite */


BOOLEAN
PHY_RFShadowCompare(
		PADAPTER		Adapter,
		enum rf_path		eRFPath,
		u32				Offset)
{
	u32	reg;
	/* Check if we need to check the register */
	if (RF_Shadow[eRFPath][Offset].Compare == _TRUE) {
		reg = rtw_hal_read_rfreg(Adapter, eRFPath, Offset, bRFRegOffsetMask);
		/* Compare shadow and real rf register for 20bits!! */
		if (RF_Shadow[eRFPath][Offset].Value != reg) {
			/* Locate error position. */
			RF_Shadow[eRFPath][Offset].ErrorOrNot = _TRUE;
		}
		return RF_Shadow[eRFPath][Offset].ErrorOrNot ;
	}
	return _FALSE;
}	/* PHY_RFShadowCompare */


void
PHY_RFShadowRecorver(
		PADAPTER		Adapter,
		enum rf_path		eRFPath,
		u32				Offset)
{
	/* Check if the address is error */
	if (RF_Shadow[eRFPath][Offset].ErrorOrNot == _TRUE) {
		/* Check if we need to recorver the register. */
		if (RF_Shadow[eRFPath][Offset].Recorver == _TRUE) {
			rtw_hal_write_rfreg(Adapter, eRFPath, Offset, bRFRegOffsetMask,
					    RF_Shadow[eRFPath][Offset].Value);
		}
	}

}	/* PHY_RFShadowRecorver */


void
PHY_RFShadowCompareAll(
		PADAPTER			Adapter)
{
	enum rf_path	eRFPath = RF_PATH_A;
	u32		Offset = 0, maxReg = GET_RF6052_REAL_MAX_REG(Adapter);

	for (eRFPath = 0; eRFPath < RF6052_MAX_PATH; eRFPath++) {
		for (Offset = 0; Offset < maxReg; Offset++)
			PHY_RFShadowCompare(Adapter, eRFPath, Offset);
	}

}	/* PHY_RFShadowCompareAll */


void
PHY_RFShadowRecorverAll(
		PADAPTER			Adapter)
{
	enum rf_path		eRFPath = RF_PATH_A;
	u32		Offset = 0, maxReg = GET_RF6052_REAL_MAX_REG(Adapter);

	for (eRFPath = 0; eRFPath < RF6052_MAX_PATH; eRFPath++) {
		for (Offset = 0; Offset < maxReg; Offset++)
			PHY_RFShadowRecorver(Adapter, eRFPath, Offset);
	}

}	/* PHY_RFShadowRecorverAll */


void
PHY_RFShadowCompareFlagSet(
		PADAPTER		Adapter,
		enum rf_path		eRFPath,
		u32				Offset,
		u8				Type)
{
	/* Set True or False!!! */
	RF_Shadow[eRFPath][Offset].Compare = Type;

}	/* PHY_RFShadowCompareFlagSet */


void
PHY_RFShadowRecorverFlagSet(
		PADAPTER		Adapter,
		enum rf_path		eRFPath,
		u32				Offset,
		u8				Type)
{
	/* Set True or False!!! */
	RF_Shadow[eRFPath][Offset].Recorver = Type;

}	/* PHY_RFShadowRecorverFlagSet */


void
PHY_RFShadowCompareFlagSetAll(
		PADAPTER			Adapter)
{
	enum rf_path	eRFPath = RF_PATH_A;
	u32		Offset = 0, maxReg = GET_RF6052_REAL_MAX_REG(Adapter);

	for (eRFPath = 0; eRFPath < RF6052_MAX_PATH; eRFPath++) {
		for (Offset = 0; Offset < maxReg; Offset++) {
			/* 2008/11/20 MH For S3S4 test, we only check reg 26/27 now!!!! */
			if (Offset != 0x26 && Offset != 0x27)
				PHY_RFShadowCompareFlagSet(Adapter, eRFPath, Offset, _FALSE);
			else
				PHY_RFShadowCompareFlagSet(Adapter, eRFPath, Offset, _TRUE);
		}
	}

}	/* PHY_RFShadowCompareFlagSetAll */


void
PHY_RFShadowRecorverFlagSetAll(
		PADAPTER			Adapter)
{
	enum rf_path		eRFPath = RF_PATH_A;
	u32		Offset = 0, maxReg = GET_RF6052_REAL_MAX_REG(Adapter);

	for (eRFPath = 0; eRFPath < RF6052_MAX_PATH; eRFPath++) {
		for (Offset = 0; Offset < maxReg; Offset++) {
			/* 2008/11/20 MH For S3S4 test, we only check reg 26/27 now!!!! */
			if (Offset != 0x26 && Offset != 0x27)
				PHY_RFShadowRecorverFlagSet(Adapter, eRFPath, Offset, _FALSE);
			else
				PHY_RFShadowRecorverFlagSet(Adapter, eRFPath, Offset, _TRUE);
		}
	}

}	/* PHY_RFShadowCompareFlagSetAll */

void
PHY_RFShadowRefresh(
		PADAPTER			Adapter)
{
	enum rf_path		eRFPath = RF_PATH_A;
	u32		Offset = 0, maxReg = GET_RF6052_REAL_MAX_REG(Adapter);

	for (eRFPath = 0; eRFPath < RF6052_MAX_PATH; eRFPath++) {
		for (Offset = 0; Offset < maxReg; Offset++) {
			RF_Shadow[eRFPath][Offset].Value = 0;
			RF_Shadow[eRFPath][Offset].Compare = _FALSE;
			RF_Shadow[eRFPath][Offset].Recorver  = _FALSE;
			RF_Shadow[eRFPath][Offset].ErrorOrNot = _FALSE;
			RF_Shadow[eRFPath][Offset].Driver_Write = _FALSE;
		}
	}

}	/* PHY_RFShadowRead */
#endif /*CONFIG_RF_SHADOW_RW*/

// SPDX-License-Identifier: GPL-2.0
/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 *
 ******************************************************************************/
#define _HAL_PHY_C_

#include <drv_types.h>

/**
* Function:	PHY_CalculateBitShift
*
* OverView:	Get shifted position of the BitMask
*
* Input:
*		u32 	BitMask,
*
* Output:	none
* Return:		u32 	Return the shift bit bit position of the mask
*/
u32 PHY_CalculateBitShift(u32 BitMask)
{
	u32 i;

	for (i = 0; i <= 31; i++) {
		if (((BitMask>>i) &  0x1) == 1)
			break;
	}

	return i;
}


/*  */
/*  ==> RF shadow Operation API Code Section!!! */
/*  */
/*-----------------------------------------------------------------------------
 * Function:	PHY_RFShadowRead
 *			PHY_RFShadowWrite
 *			PHY_RFShadowCompare
 *			PHY_RFShadowRecorver
 *			PHY_RFShadowCompareAll
 *			PHY_RFShadowRecorverAll
 *			PHY_RFShadowCompareFlagSet
 *			PHY_RFShadowRecorverFlagSet
 *
 * Overview:	When we set RF register, we must write shadow at first.
 *		When we are running, we must compare shadow abd locate error addr.
 *		Decide to recorver or not.
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
u32 PHY_RFShadowRead(IN PADAPTER Adapter, IN u8 eRFPath, IN u32 Offset)
{
	return	RF_Shadow[eRFPath][Offset].Value;

}	/* PHY_RFShadowRead */


void PHY_RFShadowWrite(
	IN PADAPTER Adapter, IN u8 eRFPath, IN u32 Offset, IN u32 Data
)
{
	RF_Shadow[eRFPath][Offset].Value = (Data & bRFRegOffsetMask);
	RF_Shadow[eRFPath][Offset].Driver_Write = true;

}	/* PHY_RFShadowWrite */


bool PHY_RFShadowCompare(IN PADAPTER Adapter, IN u8 eRFPath, IN u32 Offset)
{
	u32 reg;
	/*  Check if we need to check the register */
	if (RF_Shadow[eRFPath][Offset].Compare == true) {
		reg = rtw_hal_read_rfreg(Adapter, eRFPath, Offset, bRFRegOffsetMask);
		/*  Compare shadow and real rf register for 20bits!! */
		if (RF_Shadow[eRFPath][Offset].Value != reg) {
			/*  Locate error position. */
			RF_Shadow[eRFPath][Offset].ErrorOrNot = true;
			/* RT_TRACE(COMP_INIT, DBG_LOUD, */
			/* PHY_RFShadowCompare RF-%d Addr%02lx Err = %05lx\n", */
			/* eRFPath, Offset, reg)); */
		}
		return RF_Shadow[eRFPath][Offset].ErrorOrNot;
	}
	return false;
}	/* PHY_RFShadowCompare */


void PHY_RFShadowRecorver(IN PADAPTER Adapter, IN u8 eRFPath, IN u32 Offset)
{
	/*  Check if the address is error */
	if (RF_Shadow[eRFPath][Offset].ErrorOrNot == true) {
		/*  Check if we need to recorver the register. */
		if (RF_Shadow[eRFPath][Offset].Recorver == true) {
			rtw_hal_write_rfreg(Adapter, eRFPath, Offset, bRFRegOffsetMask,
							RF_Shadow[eRFPath][Offset].Value);
			/* RT_TRACE(COMP_INIT, DBG_LOUD, */
			/* PHY_RFShadowRecorver RF-%d Addr%02lx=%05lx", */
			/* eRFPath, Offset, RF_Shadow[eRFPath][Offset].Value)); */
		}
	}

}	/* PHY_RFShadowRecorver */


void PHY_RFShadowCompareAll(IN PADAPTER Adapter)
{
	u8 eRFPath = 0;
	u32 Offset = 0, maxReg = GET_RF6052_REAL_MAX_REG(Adapter);

	for (eRFPath = 0; eRFPath < RF6052_MAX_PATH; eRFPath++) {
		for (Offset = 0; Offset < maxReg; Offset++) {
			PHY_RFShadowCompare(Adapter, eRFPath, Offset);
		}
	}

}	/* PHY_RFShadowCompareAll */


void PHY_RFShadowRecorverAll(IN PADAPTER Adapter)
{
	u8 eRFPath = 0;
	u32 Offset = 0, maxReg = GET_RF6052_REAL_MAX_REG(Adapter);

	for (eRFPath = 0; eRFPath < RF6052_MAX_PATH; eRFPath++) {
		for (Offset = 0; Offset < maxReg; Offset++) {
			PHY_RFShadowRecorver(Adapter, eRFPath, Offset);
		}
	}

}	/* PHY_RFShadowRecorverAll */


void
PHY_RFShadowCompareFlagSet(
	IN PADAPTER Adapter, IN u8 eRFPath, IN u32 Offset, IN u8 Type
)
{
	/*  Set True or False!!! */
	RF_Shadow[eRFPath][Offset].Compare = Type;

}	/* PHY_RFShadowCompareFlagSet */


void PHY_RFShadowRecorverFlagSet(
	IN PADAPTER Adapter, IN u8 eRFPath, IN u32 Offset, IN u8 Type
)
{
	/*  Set True or False!!! */
	RF_Shadow[eRFPath][Offset].Recorver = Type;

}	/* PHY_RFShadowRecorverFlagSet */


void PHY_RFShadowCompareFlagSetAll(IN PADAPTER Adapter)
{
	u8 eRFPath = 0;
	u32 Offset = 0, maxReg = GET_RF6052_REAL_MAX_REG(Adapter);

	for (eRFPath = 0; eRFPath < RF6052_MAX_PATH; eRFPath++) {
		for (Offset = 0; Offset < maxReg; Offset++) {
			/*  2008/11/20 MH For S3S4 test, we only check reg 26/27 now!!!! */
			if (Offset != 0x26 && Offset != 0x27)
				PHY_RFShadowCompareFlagSet(Adapter, eRFPath, Offset, false);
			else
				PHY_RFShadowCompareFlagSet(Adapter, eRFPath, Offset, true);
		}
	}

}	/* PHY_RFShadowCompareFlagSetAll */

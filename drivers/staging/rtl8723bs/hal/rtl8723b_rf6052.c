// SPDX-License-Identifier: GPL-2.0
/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 *
 ******************************************************************************/
/******************************************************************************
 *
 *
 * Module:	rtl8192c_rf6052.c	(Source C File)
 *
 * Note:	Provide RF 6052 series relative API.
 *
 * Function:
 *
 * Export:
 *
 * Abbrev:
 *
 * History:
 * Data			Who		Remark
 *
 * 09/25/2008	MHC		Create initial version.
 * 11/05/2008	MHC		Add API for tw power setting.
 *
 *
******************************************************************************/

#include <rtl8723b_hal.h>

/*---------------------------Define Local Constant---------------------------*/
/*---------------------------Define Local Constant---------------------------*/


/*------------------------Define global variable-----------------------------*/
/*------------------------Define global variable-----------------------------*/


/*------------------------Define local variable------------------------------*/
/*  2008/11/20 MH For Debug only, RF */
/*------------------------Define local variable------------------------------*/

/*-----------------------------------------------------------------------------
 * Function:    PHY_RF6052SetBandwidth()
 *
 * Overview:    This function is called by SetBWModeCallback8190Pci() only
 *
 * Input:       struct adapter *			Adapter
 *		WIRELESS_BANDWIDTH_E	Bandwidth	20M or 40M
 *
 * Output:      NONE
 *
 * Return:      NONE
 *
 * Note:		For RF type 0222D
 *---------------------------------------------------------------------------*/
void PHY_RF6052SetBandwidth8723B(
	struct adapter *Adapter, enum CHANNEL_WIDTH Bandwidth
) /* 20M or 40M */
{
	struct hal_com_data *pHalData = GET_HAL_DATA(Adapter);

	switch (Bandwidth) {
	case CHANNEL_WIDTH_20:
		pHalData->RfRegChnlVal[0] = ((pHalData->RfRegChnlVal[0] & 0xfffff3ff) | BIT10 | BIT11);
		PHY_SetRFReg(Adapter, ODM_RF_PATH_A, RF_CHNLBW, bRFRegOffsetMask, pHalData->RfRegChnlVal[0]);
		PHY_SetRFReg(Adapter, ODM_RF_PATH_B, RF_CHNLBW, bRFRegOffsetMask, pHalData->RfRegChnlVal[0]);
		break;

	case CHANNEL_WIDTH_40:
		pHalData->RfRegChnlVal[0] = ((pHalData->RfRegChnlVal[0] & 0xfffff3ff) | BIT10);
		PHY_SetRFReg(Adapter, ODM_RF_PATH_A, RF_CHNLBW, bRFRegOffsetMask, pHalData->RfRegChnlVal[0]);
		PHY_SetRFReg(Adapter, ODM_RF_PATH_B, RF_CHNLBW, bRFRegOffsetMask, pHalData->RfRegChnlVal[0]);
		break;

	default:
		/* RT_TRACE(COMP_DBG, DBG_LOUD, ("PHY_SetRF8225Bandwidth(): unknown Bandwidth: %#X\n", Bandwidth)); */
		break;
	}

}

static int phy_RF6052_Config_ParaFile(struct adapter *Adapter)
{
	u32 u4RegValue = 0;
	u8 eRFPath;
	struct bb_register_def *pPhyReg;
	struct hal_com_data *pHalData = GET_HAL_DATA(Adapter);

	/* 3----------------------------------------------------------------- */
	/* 3 <2> Initialize RF */
	/* 3----------------------------------------------------------------- */
	/* for (eRFPath = RF_PATH_A; eRFPath <pHalData->NumTotalRFPath; eRFPath++) */
	for (eRFPath = 0; eRFPath < pHalData->NumTotalRFPath; eRFPath++) {

		pPhyReg = &pHalData->PHYRegDef[eRFPath];

		/*----Store original RFENV control type----*/
		switch (eRFPath) {
		case RF_PATH_A:
		case RF_PATH_C:
			u4RegValue = PHY_QueryBBReg(Adapter, pPhyReg->rfintfs, bRFSI_RFENV);
			break;
		case RF_PATH_B:
		case RF_PATH_D:
			u4RegValue = PHY_QueryBBReg(Adapter, pPhyReg->rfintfs, bRFSI_RFENV << 16);
			break;
		}

		/*----Set RF_ENV enable----*/
		PHY_SetBBReg(Adapter, pPhyReg->rfintfe, bRFSI_RFENV << 16, 0x1);
		udelay(1);/* PlatformStallExecution(1); */

		/*----Set RF_ENV output high----*/
		PHY_SetBBReg(Adapter, pPhyReg->rfintfo, bRFSI_RFENV, 0x1);
		udelay(1);/* PlatformStallExecution(1); */

		/* Set bit number of Address and Data for RF register */
		PHY_SetBBReg(Adapter, pPhyReg->rfHSSIPara2, b3WireAddressLength, 0x0);	/*  Set 1 to 4 bits for 8255 */
		udelay(1);/* PlatformStallExecution(1); */

		PHY_SetBBReg(Adapter, pPhyReg->rfHSSIPara2, b3WireDataLength, 0x0);	/*  Set 0 to 12  bits for 8255 */
		udelay(1);/* PlatformStallExecution(1); */

		/*----Initialize RF fom connfiguration file----*/
		switch (eRFPath) {
		case RF_PATH_A:
		case RF_PATH_B:
			ODM_ConfigRFWithHeaderFile(&pHalData->odmpriv,
						   CONFIG_RF_RADIO, eRFPath);
			break;
		case RF_PATH_C:
		case RF_PATH_D:
			break;
		}

		/*----Restore RFENV control type----*/
		switch (eRFPath) {
		case RF_PATH_A:
		case RF_PATH_C:
			PHY_SetBBReg(Adapter, pPhyReg->rfintfs, bRFSI_RFENV, u4RegValue);
			break;
		case RF_PATH_B:
		case RF_PATH_D:
			PHY_SetBBReg(Adapter, pPhyReg->rfintfs, bRFSI_RFENV << 16, u4RegValue);
			break;
		}
	}

	/* 3 ----------------------------------------------------------------- */
	/* 3 Configuration of Tx Power Tracking */
	/* 3 ----------------------------------------------------------------- */

	ODM_ConfigRFWithTxPwrTrackHeaderFile(&pHalData->odmpriv);

	/* RT_TRACE(COMP_INIT, DBG_LOUD, ("<---phy_RF6052_Config_ParaFile()\n")); */
	return _SUCCESS;
}


int PHY_RF6052_Config8723B(struct adapter *Adapter)
{
	struct hal_com_data *pHalData = GET_HAL_DATA(Adapter);

	/*  */
	/*  Initialize general global value */
	/*  */
	/*  TODO: Extend RF_PATH_C and RF_PATH_D in the future */
	if (pHalData->rf_type == RF_1T1R)
		pHalData->NumTotalRFPath = 1;
	else
		pHalData->NumTotalRFPath = 2;

	/*  */
	/*  Config BB and RF */
	/*  */
	return phy_RF6052_Config_ParaFile(Adapter);

}

/* End of HalRf6052.c */

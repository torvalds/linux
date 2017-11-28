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
/******************************************************************************
 *
 *
 * Module:	rtl8188e_rf6052.c	( Source C File)
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

#define _RTL8188E_RF6052_C_

#include <drv_types.h>
#include <rtl8188e_hal.h>

/*---------------------------Define Local Constant---------------------------*/

/*---------------------------Define Local Constant---------------------------*/


/*------------------------Define global variable-----------------------------*/
/*------------------------Define global variable-----------------------------*/


/*------------------------Define local variable------------------------------*/

/*------------------------Define local variable------------------------------*/


/*-----------------------------------------------------------------------------
 * Function:	RF_ChangeTxPath
 *
 * Overview:	For RL6052, we must change some RF settign for 1T or 2T.
 *
 * Input:		u2Byte DataRate		// 0x80-8f, 0x90-9f
 *
 * Output:      NONE
 *
 * Return:      NONE
 *
 * Revised History:
 * When			Who		Remark
 * 09/25/2008	MHC		Create Version 0.
 *						Firmwaer support the utility later.
 *
 *---------------------------------------------------------------------------*/
void rtl8188e_RF_ChangeTxPath(IN	PADAPTER	Adapter,
			      IN	u16		DataRate)
{
	/* We do not support gain table change inACUT now !!!! Delete later !!! */
#if 0/* (RTL92SE_FPGA_VERIFY == 0) */
	static	u1Byte	RF_Path_Type = 2;	/* 1 = 1T 2= 2T */
	static	u4Byte	tx_gain_tbl1[6]
		= {0x17f50, 0x11f40, 0x0cf30, 0x08720, 0x04310, 0x00100};
	static	u4Byte	tx_gain_tbl2[6]
		= {0x15ea0, 0x10e90, 0x0c680, 0x08250, 0x04040, 0x00030};
	u1Byte	i;

	if (RF_Path_Type == 2 && (DataRate & 0xF) <= 0x7) {
		/* Set TX SYNC power G2G3 loop filter */
		phy_set_rf_reg(Adapter, RF_PATH_A,
			     RF_TXPA_G2, bRFRegOffsetMask, 0x0f000);
		phy_set_rf_reg(Adapter, RF_PATH_A,
			     RF_TXPA_G3, bRFRegOffsetMask, 0xeacf1);

		/* Change TX AGC gain table */
		for (i = 0; i < 6; i++)
			phy_set_rf_reg(Adapter, RF_PATH_A,
				RF_TX_AGC, bRFRegOffsetMask, tx_gain_tbl1[i]);

		/* Set PA to high value */
		phy_set_rf_reg(Adapter, RF_PATH_A,
			     RF_TXPA_G2, bRFRegOffsetMask, 0x01e39);
	} else if (RF_Path_Type == 1 && (DataRate & 0xF) >= 0x8) {
		/* Set TX SYNC power G2G3 loop filter */
		phy_set_rf_reg(Adapter, RF_PATH_A,
			     RF_TXPA_G2, bRFRegOffsetMask, 0x04440);
		phy_set_rf_reg(Adapter, RF_PATH_A,
			     RF_TXPA_G3, bRFRegOffsetMask, 0xea4f1);

		/* Change TX AGC gain table */
		for (i = 0; i < 6; i++)
			phy_set_rf_reg(Adapter, RF_PATH_A,
				RF_TX_AGC, bRFRegOffsetMask, tx_gain_tbl2[i]);

		/* Set PA low gain */
		phy_set_rf_reg(Adapter, RF_PATH_A,
			     RF_TXPA_G2, bRFRegOffsetMask, 0x01e19);
	}
#endif

}	/* RF_ChangeTxPath */


/*-----------------------------------------------------------------------------
 * Function:    PHY_RF6052SetBandwidth()
 *
 * Overview:    This function is called by SetBWModeCallback8190Pci() only
 *
 * Input:       PADAPTER				Adapter
 *			WIRELESS_BANDWIDTH_E	Bandwidth	//20M or 40M
 *
 * Output:      NONE
 *
 * Return:      NONE
 *
 * Note:		For RF type 0222D
 *---------------------------------------------------------------------------*/
VOID
rtl8188e_PHY_RF6052SetBandwidth(
	IN	PADAPTER				Adapter,
	IN	CHANNEL_WIDTH		Bandwidth)	/* 20M or 40M */
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);

	switch (Bandwidth) {
	case CHANNEL_WIDTH_20:
		pHalData->RfRegChnlVal[0] = ((pHalData->RfRegChnlVal[0] & 0xfffff3ff) | BIT(10) | BIT(11));
		phy_set_rf_reg(Adapter, RF_PATH_A, RF_CHNLBW, bRFRegOffsetMask, pHalData->RfRegChnlVal[0]);
		break;

	case CHANNEL_WIDTH_40:
		pHalData->RfRegChnlVal[0] = ((pHalData->RfRegChnlVal[0] & 0xfffff3ff) | BIT(10));
		phy_set_rf_reg(Adapter, RF_PATH_A, RF_CHNLBW, bRFRegOffsetMask, pHalData->RfRegChnlVal[0]);
		break;

	default:
		break;
	}

}

static int
phy_RF6052_Config_ParaFile(
	IN	PADAPTER		Adapter
)
{
	u32					u4RegValue = 0;
	u8					eRFPath;
	BB_REGISTER_DEFINITION_T	*pPhyReg;

	int					rtStatus = _SUCCESS;
	HAL_DATA_TYPE		*pHalData = GET_HAL_DATA(Adapter);

	/* 3 */ /* ----------------------------------------------------------------- */
	/* 3 */ /* <2> Initialize RF */
	/* 3 */ /* ----------------------------------------------------------------- */
	/* for(eRFPath = RF_PATH_A; eRFPath <pHalData->NumTotalRFPath; eRFPath++) */
	for (eRFPath = 0; eRFPath < pHalData->NumTotalRFPath; eRFPath++) {

		pPhyReg = &pHalData->PHYRegDef[eRFPath];

		/*----Store original RFENV control type----*/
		switch (eRFPath) {
		case RF_PATH_A:
		case RF_PATH_C:
			u4RegValue = phy_query_bb_reg(Adapter, pPhyReg->rfintfs, bRFSI_RFENV);
			break;
		case RF_PATH_B:
		case RF_PATH_D:
			u4RegValue = phy_query_bb_reg(Adapter, pPhyReg->rfintfs, bRFSI_RFENV << 16);
			break;
		}

		/*----Set RF_ENV enable----*/
		phy_set_bb_reg(Adapter, pPhyReg->rfintfe, bRFSI_RFENV << 16, 0x1);
		rtw_udelay_os(1);/* PlatformStallExecution(1); */

		/*----Set RF_ENV output high----*/
		phy_set_bb_reg(Adapter, pPhyReg->rfintfo, bRFSI_RFENV, 0x1);
		rtw_udelay_os(1);/* PlatformStallExecution(1); */

		/* Set bit number of Address and Data for RF register */
		phy_set_bb_reg(Adapter, pPhyReg->rfHSSIPara2, b3WireAddressLength, 0x0);	/* Set 1 to 4 bits for 8255 */
		rtw_udelay_os(1);/* PlatformStallExecution(1); */

		phy_set_bb_reg(Adapter, pPhyReg->rfHSSIPara2, b3WireDataLength, 0x0);	/* Set 0 to 12  bits for 8255 */
		rtw_udelay_os(1);/* PlatformStallExecution(1); */

		/*----Initialize RF fom connfiguration file----*/
		switch (eRFPath) {
		case RF_PATH_A:
#ifdef CONFIG_LOAD_PHY_PARA_FROM_FILE
			if (PHY_ConfigRFWithParaFile(Adapter, PHY_FILE_RADIO_A, eRFPath) == _FAIL)
#endif
			{
#ifdef CONFIG_EMBEDDED_FWIMG
				if (HAL_STATUS_FAILURE == odm_config_rf_with_header_file(&pHalData->odmpriv, CONFIG_RF_RADIO, (enum odm_rf_radio_path_e)eRFPath))
					rtStatus = _FAIL;
#endif
			}
			break;
		case RF_PATH_B:
#ifdef CONFIG_LOAD_PHY_PARA_FROM_FILE
			if (PHY_ConfigRFWithParaFile(Adapter, PHY_FILE_RADIO_B, eRFPath) == _FAIL)
#endif
			{
#ifdef CONFIG_EMBEDDED_FWIMG
				if (HAL_STATUS_FAILURE == odm_config_rf_with_header_file(&pHalData->odmpriv, CONFIG_RF_RADIO, (enum odm_rf_radio_path_e)eRFPath))
					rtStatus = _FAIL;
#endif
			}
			break;
		case RF_PATH_C:
			break;
		case RF_PATH_D:
			break;
		}

		/*----Restore RFENV control type----*/;
		switch (eRFPath) {
		case RF_PATH_A:
		case RF_PATH_C:
			phy_set_bb_reg(Adapter, pPhyReg->rfintfs, bRFSI_RFENV, u4RegValue);
			break;
		case RF_PATH_B:
		case RF_PATH_D:
			phy_set_bb_reg(Adapter, pPhyReg->rfintfs, bRFSI_RFENV << 16, u4RegValue);
			break;
		}

		if (rtStatus != _SUCCESS) {
			goto phy_RF6052_Config_ParaFile_Fail;
		}

	}


	/* 3 ----------------------------------------------------------------- */
	/* 3 Configuration of Tx Power Tracking */
	/* 3 ----------------------------------------------------------------- */

#ifdef CONFIG_LOAD_PHY_PARA_FROM_FILE
	if (PHY_ConfigRFWithTxPwrTrackParaFile(Adapter, PHY_FILE_TXPWR_TRACK) == _FAIL)
#endif
	{
#ifdef CONFIG_EMBEDDED_FWIMG
		odm_config_rf_with_tx_pwr_track_header_file(&pHalData->odmpriv);
#endif
	}

	return rtStatus;

phy_RF6052_Config_ParaFile_Fail:
	return rtStatus;
}


int
PHY_RF6052_Config8188E(
	IN	PADAPTER		Adapter)
{
	HAL_DATA_TYPE				*pHalData = GET_HAL_DATA(Adapter);
	int					rtStatus = _SUCCESS;

	/*  */
	/* Initialize general global value */
	/*  */
	/* TODO: Extend RF_PATH_C and RF_PATH_D in the future */
	if (pHalData->rf_type == RF_1T1R)
		pHalData->NumTotalRFPath = 1;
	else
		pHalData->NumTotalRFPath = 2;

	/*  */
	/* Config BB and RF */
	/*  */
	rtStatus = phy_RF6052_Config_ParaFile(Adapter);
#if 0
	switch (Adapter->MgntInfo.bRegHwParaFile) {
	case 0:
		phy_RF6052_Config_HardCode(Adapter);
		break;

	case 1:
		rtStatus = phy_RF6052_Config_ParaFile(Adapter);
		break;

	case 2:
		/* Partial Modify. */
		phy_RF6052_Config_HardCode(Adapter);
		phy_RF6052_Config_ParaFile(Adapter);
		break;

	default:
		phy_RF6052_Config_HardCode(Adapter);
		break;
	}
#endif
	return rtStatus;

}

/* End of HalRf6052.c */

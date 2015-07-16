/******************************************************************************
 *
 * Copyright(c) 2007 - 2010 Realtek Corporation. All rights reserved.
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
 * Modifications for inclusion into the Linux staging tree are
 * Copyright(c) 2010 Larry Finger. All rights reserved.
 *
 * Contact information:
 * WLAN FAE <wlanfae@realtek.com>
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 ******************************************************************************/
#ifndef __RTL8712_CMD_H_
#define __RTL8712_CMD_H_

#define CMD_HDR_SZ      8

u8 r8712_fw_cmd(struct _adapter *pAdapter, u32 cmd);
void r8712_fw_cmd_data(struct _adapter *pAdapter, u32 *value, u8 flag);

struct cmd_hdr {
	u32 cmd_dw0;
	u32 cmd_dw1;
};

enum rtl8712_h2c_cmd {
	GEN_CMD_CODE(_Read_MACREG),	/*0*/
	GEN_CMD_CODE(_Write_MACREG),
	GEN_CMD_CODE(_Read_BBREG),
	GEN_CMD_CODE(_Write_BBREG),
	GEN_CMD_CODE(_Read_RFREG),
	GEN_CMD_CODE(_Write_RFREG), /*5*/
	GEN_CMD_CODE(_Read_EEPROM),
	GEN_CMD_CODE(_Write_EEPROM),
	GEN_CMD_CODE(_Read_EFUSE),
	GEN_CMD_CODE(_Write_EFUSE),

	GEN_CMD_CODE(_Read_CAM),	/*10*/
	GEN_CMD_CODE(_Write_CAM),
	GEN_CMD_CODE(_setBCNITV),
	GEN_CMD_CODE(_setMBIDCFG),
	GEN_CMD_CODE(_JoinBss),   /*14*/
	GEN_CMD_CODE(_DisConnect), /*15*/
	GEN_CMD_CODE(_CreateBss),
	GEN_CMD_CODE(_SetOpMode),
	GEN_CMD_CODE(_SiteSurvey),  /*18*/
	GEN_CMD_CODE(_SetAuth),

	GEN_CMD_CODE(_SetKey),	/*20*/
	GEN_CMD_CODE(_SetStaKey),
	GEN_CMD_CODE(_SetAssocSta),
	GEN_CMD_CODE(_DelAssocSta),
	GEN_CMD_CODE(_SetStaPwrState),
	GEN_CMD_CODE(_SetBasicRate), /*25*/
	GEN_CMD_CODE(_GetBasicRate),
	GEN_CMD_CODE(_SetDataRate),
	GEN_CMD_CODE(_GetDataRate),
	GEN_CMD_CODE(_SetPhyInfo),

	GEN_CMD_CODE(_GetPhyInfo),	/*30*/
	GEN_CMD_CODE(_SetPhy),
	GEN_CMD_CODE(_GetPhy),
	GEN_CMD_CODE(_readRssi),
	GEN_CMD_CODE(_readGain),
	GEN_CMD_CODE(_SetAtim), /*35*/
	GEN_CMD_CODE(_SetPwrMode),
	GEN_CMD_CODE(_JoinbssRpt),
	GEN_CMD_CODE(_SetRaTable),
	GEN_CMD_CODE(_GetRaTable),

	GEN_CMD_CODE(_GetCCXReport), /*40*/
	GEN_CMD_CODE(_GetDTMReport),
	GEN_CMD_CODE(_GetTXRateStatistics),
	GEN_CMD_CODE(_SetUsbSuspend),
	GEN_CMD_CODE(_SetH2cLbk),
	GEN_CMD_CODE(_AddBAReq), /*45*/

	GEN_CMD_CODE(_SetChannel), /*46*/
/* MP_OFFLOAD Start (47~54)*/
	GEN_CMD_CODE(_SetTxPower),
	GEN_CMD_CODE(_SwitchAntenna),
	GEN_CMD_CODE(_SetCrystalCap),
	GEN_CMD_CODE(_SetSingleCarrierTx), /*50*/
	GEN_CMD_CODE(_SetSingleToneTx),
	GEN_CMD_CODE(_SetCarrierSuppressionTx),
	GEN_CMD_CODE(_SetContinuousTx),
	GEN_CMD_CODE(_SwitchBandwidth), /*54*/
/* MP_OFFLOAD End*/
	GEN_CMD_CODE(_TX_Beacon), /*55*/
	GEN_CMD_CODE(_SetPowerTracking),
	GEN_CMD_CODE(_AMSDU_TO_AMPDU), /*57*/
	GEN_CMD_CODE(_SetMacAddress), /*58*/

	GEN_CMD_CODE(_DisconnectCtrl), /*59*/
	GEN_CMD_CODE(_SetChannelPlan), /*60*/
	GEN_CMD_CODE(_DisconnectCtrlEx), /*61*/

	/* To do, modify these h2c cmd, add or delete */
	GEN_CMD_CODE(_GetH2cLbk),

	/* WPS extra IE */
	GEN_CMD_CODE(_SetProbeReqExtraIE),
	GEN_CMD_CODE(_SetAssocReqExtraIE),
	GEN_CMD_CODE(_SetProbeRspExtraIE),
	GEN_CMD_CODE(_SetAssocRspExtraIE),

	/* the following is driver will do */
	GEN_CMD_CODE(_GetCurDataRate),

	GEN_CMD_CODE(_GetTxRetrycnt),  /* to record times that Tx retry to
					* transmit packet after association
					*/
	GEN_CMD_CODE(_GetRxRetrycnt),  /* to record total number of the
					* received frame with ReTry bit set in
					* the WLAN header
					*/

	GEN_CMD_CODE(_GetBCNOKcnt),
	GEN_CMD_CODE(_GetBCNERRcnt),
	GEN_CMD_CODE(_GetCurTxPwrLevel),

	GEN_CMD_CODE(_SetDIG),
	GEN_CMD_CODE(_SetRA),
	GEN_CMD_CODE(_SetPT),
	GEN_CMD_CODE(_ReadTSSI),

	MAX_H2CCMD
};


#define _GetBBReg_CMD_		_Read_BBREG_CMD_
#define _SetBBReg_CMD_		_Write_BBREG_CMD_
#define _GetRFReg_CMD_		_Read_RFREG_CMD_
#define _SetRFReg_CMD_		_Write_RFREG_CMD_
#define _DRV_INT_CMD_		(MAX_H2CCMD+1)
#define _SetRFIntFs_CMD_	(MAX_H2CCMD+2)

#ifdef _RTL8712_CMD_C_
static struct _cmd_callback	cmd_callback[] = {
	{GEN_CMD_CODE(_Read_MACREG), NULL}, /*0*/
	{GEN_CMD_CODE(_Write_MACREG), NULL},
	{GEN_CMD_CODE(_Read_BBREG), &r8712_getbbrfreg_cmdrsp_callback},
	{GEN_CMD_CODE(_Write_BBREG), NULL},
	{GEN_CMD_CODE(_Read_RFREG), &r8712_getbbrfreg_cmdrsp_callback},
	{GEN_CMD_CODE(_Write_RFREG), NULL}, /*5*/
	{GEN_CMD_CODE(_Read_EEPROM), NULL},
	{GEN_CMD_CODE(_Write_EEPROM), NULL},
	{GEN_CMD_CODE(_Read_EFUSE), NULL},
	{GEN_CMD_CODE(_Write_EFUSE), NULL},

	{GEN_CMD_CODE(_Read_CAM),	NULL},	/*10*/
	{GEN_CMD_CODE(_Write_CAM),	 NULL},
	{GEN_CMD_CODE(_setBCNITV), NULL},
	{GEN_CMD_CODE(_setMBIDCFG), NULL},
	{GEN_CMD_CODE(_JoinBss), &r8712_joinbss_cmd_callback},  /*14*/
	{GEN_CMD_CODE(_DisConnect), &r8712_disassoc_cmd_callback}, /*15*/
	{GEN_CMD_CODE(_CreateBss), &r8712_createbss_cmd_callback},
	{GEN_CMD_CODE(_SetOpMode), NULL},
	{GEN_CMD_CODE(_SiteSurvey), &r8712_survey_cmd_callback}, /*18*/
	{GEN_CMD_CODE(_SetAuth), NULL},

	{GEN_CMD_CODE(_SetKey), NULL},	/*20*/
	{GEN_CMD_CODE(_SetStaKey), &r8712_setstaKey_cmdrsp_callback},
	{GEN_CMD_CODE(_SetAssocSta), &r8712_setassocsta_cmdrsp_callback},
	{GEN_CMD_CODE(_DelAssocSta), NULL},
	{GEN_CMD_CODE(_SetStaPwrState), NULL},
	{GEN_CMD_CODE(_SetBasicRate), NULL}, /*25*/
	{GEN_CMD_CODE(_GetBasicRate), NULL},
	{GEN_CMD_CODE(_SetDataRate), NULL},
	{GEN_CMD_CODE(_GetDataRate), NULL},
	{GEN_CMD_CODE(_SetPhyInfo), NULL},

	{GEN_CMD_CODE(_GetPhyInfo), NULL}, /*30*/
	{GEN_CMD_CODE(_SetPhy), NULL},
	{GEN_CMD_CODE(_GetPhy), NULL},
	{GEN_CMD_CODE(_readRssi), NULL},
	{GEN_CMD_CODE(_readGain), NULL},
	{GEN_CMD_CODE(_SetAtim), NULL}, /*35*/
	{GEN_CMD_CODE(_SetPwrMode), NULL},
	{GEN_CMD_CODE(_JoinbssRpt), NULL},
	{GEN_CMD_CODE(_SetRaTable), NULL},
	{GEN_CMD_CODE(_GetRaTable), NULL},

	{GEN_CMD_CODE(_GetCCXReport), NULL}, /*40*/
	{GEN_CMD_CODE(_GetDTMReport),	NULL},
	{GEN_CMD_CODE(_GetTXRateStatistics), NULL},
	{GEN_CMD_CODE(_SetUsbSuspend), NULL},
	{GEN_CMD_CODE(_SetH2cLbk), NULL},
	{GEN_CMD_CODE(_AddBAReq), NULL}, /*45*/

	{GEN_CMD_CODE(_SetChannel), NULL},		/*46*/
/* MP_OFFLOAD Start (47~54)*/
	{GEN_CMD_CODE(_SetTxPower), NULL},
	{GEN_CMD_CODE(_SwitchAntenna), NULL},
	{GEN_CMD_CODE(_SetCrystalCap), NULL},
	{GEN_CMD_CODE(_SetSingleCarrierTx), NULL},	/*50*/
	{GEN_CMD_CODE(_SetSingleToneTx), NULL},
	{GEN_CMD_CODE(_SetCarrierSuppressionTx), NULL},
	{GEN_CMD_CODE(_SetContinuousTx), NULL},
	{GEN_CMD_CODE(_SwitchBandwidth), NULL},		/*54*/
/* MP_OFFLOAD End*/
	{GEN_CMD_CODE(_TX_Beacon), NULL}, /*55*/
	{GEN_CMD_CODE(_SetPowerTracking), NULL},
	{GEN_CMD_CODE(_AMSDU_TO_AMPDU), NULL}, /*57*/
	{GEN_CMD_CODE(_SetMacAddress), NULL}, /*58*/

	{GEN_CMD_CODE(_DisconnectCtrl), NULL}, /*59*/
	{GEN_CMD_CODE(_SetChannelPlan), NULL}, /*60*/
	{GEN_CMD_CODE(_DisconnectCtrlEx), NULL}, /*61*/

	/* To do, modify these h2c cmd, add or delete */
	{GEN_CMD_CODE(_GetH2cLbk), NULL},

	{_SetProbeReqExtraIE_CMD_, NULL},
	{_SetAssocReqExtraIE_CMD_, NULL},
	{_SetProbeRspExtraIE_CMD_, NULL},
	{_SetAssocRspExtraIE_CMD_, NULL},
	{_GetCurDataRate_CMD_, NULL},
	{_GetTxRetrycnt_CMD_, NULL},
	{_GetRxRetrycnt_CMD_, NULL},
	{_GetBCNOKcnt_CMD_, NULL},
	{_GetBCNERRcnt_CMD_, NULL},
	{_GetCurTxPwrLevel_CMD_, NULL},
	{_SetDIG_CMD_, NULL},
	{_SetRA_CMD_, NULL},
	{_SetPT_CMD_, NULL},
	{GEN_CMD_CODE(_ReadTSSI), &r8712_readtssi_cmdrsp_callback}
};
#endif

#endif

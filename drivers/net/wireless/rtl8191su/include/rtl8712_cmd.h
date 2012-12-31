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
 *
 ******************************************************************************/ 
#ifndef __RTL8712_CMD_H_
#define __RTL8712_CMD_H_

#define CMD_HDR_SZ	8

u8 fw_cmd(PADAPTER pAdapter, u32 cmd);
void fw_cmd_data(PADAPTER pAdapter, u32 *value, u8 flag);

struct cmd_hdr {
	u32 cmd_dw0;
	u32 cmd_dw1;
};


enum rtl8712_h2c_cmd
{
	GEN_CMD_CODE(_Read_MACREG) ,	/*0*/
 	GEN_CMD_CODE(_Write_MACREG) ,    
 	GEN_CMD_CODE(_Read_BBREG) ,  
 	GEN_CMD_CODE(_Write_BBREG) ,  
 	GEN_CMD_CODE(_Read_RFREG) ,  
 	GEN_CMD_CODE(_Write_RFREG) , /*5*/
 	GEN_CMD_CODE(_Read_EEPROM) ,  
 	GEN_CMD_CODE(_Write_EEPROM) ,  
 	GEN_CMD_CODE(_Read_EFUSE) ,  
 	GEN_CMD_CODE(_Write_EFUSE) , 
 	
 	GEN_CMD_CODE(_Read_CAM) ,	/*10*/
 	GEN_CMD_CODE(_Write_CAM) ,   
 	GEN_CMD_CODE(_setBCNITV),
 	GEN_CMD_CODE(_setMBIDCFG),
 	GEN_CMD_CODE(_JoinBss),   /*14*/
 	GEN_CMD_CODE(_DisConnect) , /*15*/
 	GEN_CMD_CODE(_CreateBss) ,
	GEN_CMD_CODE(_SetOpMode) , 
	GEN_CMD_CODE(_SiteSurvey),  /*18*/
 	GEN_CMD_CODE(_SetAuth) ,
 	
 	GEN_CMD_CODE(_SetKey) ,	/*20*/
 	GEN_CMD_CODE(_SetStaKey) ,
 	GEN_CMD_CODE(_SetAssocSta) ,
 	GEN_CMD_CODE(_DelAssocSta) ,
 	GEN_CMD_CODE(_SetStaPwrState) , 
 	GEN_CMD_CODE(_SetBasicRate) , /*25*/
 	GEN_CMD_CODE(_GetBasicRate) ,
 	GEN_CMD_CODE(_SetDataRate) ,
 	GEN_CMD_CODE(_GetDataRate) ,
	GEN_CMD_CODE(_SetPhyInfo) ,
	
 	GEN_CMD_CODE(_GetPhyInfo) ,	/*30*/
	GEN_CMD_CODE(_SetPhy) ,
 	GEN_CMD_CODE(_GetPhy) ,
 	GEN_CMD_CODE(_readRssi) ,
 	GEN_CMD_CODE(_readGain) ,
 	GEN_CMD_CODE(_SetAtim) , /*35*/
 	GEN_CMD_CODE(_SetPwrMode) , 
 	GEN_CMD_CODE(_JoinbssRpt),
 	GEN_CMD_CODE(_SetRaTable) ,
 	GEN_CMD_CODE(_GetRaTable) ,  	
 	
 	GEN_CMD_CODE(_GetCCXReport), /*40*/
 	GEN_CMD_CODE(_GetDTMReport),
 	GEN_CMD_CODE(_GetTXRateStatistics),
 	GEN_CMD_CODE(_SetUsbSuspend),
 	GEN_CMD_CODE(_SetH2cLbk),
 	GEN_CMD_CODE(_AddBAReq) , /*45*/

	GEN_CMD_CODE(_SetChannel), /*46*/

// MP_OFFLOAD Start (47~54)
	GEN_CMD_CODE(_SetTxPower), 
	GEN_CMD_CODE(_SwitchAntenna),
	GEN_CMD_CODE(_SetCrystalCap),
	GEN_CMD_CODE(_SetSingleCarrierTx), /*50*/
	GEN_CMD_CODE(_SetSingleToneTx),
	GEN_CMD_CODE(_SetCarrierSuppressionTx),
	GEN_CMD_CODE(_SetContinuousTx),
	GEN_CMD_CODE(_SwitchBandwidth), /*54*/
// MP_OFFLOAD End

	GEN_CMD_CODE(_TX_Beacon),        /*55*/
	GEN_CMD_CODE(_SetPowerTracking),
	GEN_CMD_CODE(_AMSDU_TO_AMPDU), /*57*/ 
	GEN_CMD_CODE(_SetMacAddress), /*58*/

	GEN_CMD_CODE(_DisconnectCtrl), /*59*/
	GEN_CMD_CODE(_SetChannelPlan), /*60*/
	GEN_CMD_CODE(_DisconnectCtrlEx), /*61*/
	GEN_CMD_CODE(_WWlanCtrl), /*62*/
	GEN_CMD_CODE(_SetPwrParam),	/*63*/
	
	
#if 1//To do, modify these h2c cmd, add or delete
	GEN_CMD_CODE(_GetH2cLbk) ,

	// WPS extra IE
	GEN_CMD_CODE(_SetProbeReqExtraIE) ,
	GEN_CMD_CODE(_SetAssocReqExtraIE) ,
	GEN_CMD_CODE(_SetProbeRspExtraIE) ,
	GEN_CMD_CODE(_SetAssocRspExtraIE) ,
	
	// the following is driver will do
	GEN_CMD_CODE(_GetCurDataRate) , 

	GEN_CMD_CODE(_GetTxRetrycnt),  // to record times that Tx retry to transmmit packet after association
	GEN_CMD_CODE(_GetRxRetrycnt), // to record total number of the received frame with ReTry bit set in the WLAN header

	GEN_CMD_CODE(_GetBCNOKcnt),
	GEN_CMD_CODE(_GetBCNERRcnt),
	GEN_CMD_CODE(_GetCurTxPwrLevel),

	GEN_CMD_CODE(_SetDIG),
	GEN_CMD_CODE(_SetRA),
	GEN_CMD_CODE(_SetPT),
	GEN_CMD_CODE(_ReadTSSI),	
 #endif
	MAX_H2CCMD
};


#define _GetBBReg_CMD_		_Read_BBREG_CMD_
#define _SetBBReg_CMD_ 		_Write_BBREG_CMD_
#define _GetRFReg_CMD_ 		_Read_RFREG_CMD_
#define _SetRFReg_CMD_ 		_Write_RFREG_CMD_
#define _DRV_INT_CMD_		(MAX_H2CCMD+1)
#define _SetRFIntFs_CMD_	(MAX_H2CCMD+2)

#ifdef _RTL8712_CMD_C_
struct _cmd_callback 	cmd_callback[] = 
{
	{GEN_CMD_CODE(_Read_MACREG), NULL}, /*0*/
	{GEN_CMD_CODE(_Write_MACREG), NULL}, 
	{GEN_CMD_CODE(_Read_BBREG), &getbbrfreg_cmdrsp_callback},
	{GEN_CMD_CODE(_Write_BBREG), NULL},
	{GEN_CMD_CODE(_Read_RFREG), &getbbrfreg_cmdrsp_callback},
	{GEN_CMD_CODE(_Write_RFREG), NULL}, /*5*/
	{GEN_CMD_CODE(_Read_EEPROM), NULL},
	{GEN_CMD_CODE(_Write_EEPROM), NULL},
	{GEN_CMD_CODE(_Read_EFUSE), NULL},
	{GEN_CMD_CODE(_Write_EFUSE), NULL},
	
	{GEN_CMD_CODE(_Read_CAM),	NULL},	/*10*/
	{GEN_CMD_CODE(_Write_CAM),	 NULL},	
	{GEN_CMD_CODE(_setBCNITV), NULL},
 	{GEN_CMD_CODE(_setMBIDCFG), NULL},
	{GEN_CMD_CODE(_JoinBss), &joinbss_cmd_callback},  /*14*/
	{GEN_CMD_CODE(_DisConnect), &disassoc_cmd_callback}, /*15*/
	{GEN_CMD_CODE(_CreateBss), &createbss_cmd_callback},
	{GEN_CMD_CODE(_SetOpMode), NULL},
	{GEN_CMD_CODE(_SiteSurvey), &survey_cmd_callback}, /*18*/
	{GEN_CMD_CODE(_SetAuth), NULL},
	
	{GEN_CMD_CODE(_SetKey), NULL},	/*20*/
	{GEN_CMD_CODE(_SetStaKey), &setstaKey_cmdrsp_callback},
	{GEN_CMD_CODE(_SetAssocSta), &setassocsta_cmdrsp_callback},
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
	{GEN_CMD_CODE(_GetRaTable) , NULL},
 	
	{GEN_CMD_CODE(_GetCCXReport), NULL}, /*40*/
 	{GEN_CMD_CODE(_GetDTMReport),	NULL},
 	{GEN_CMD_CODE(_GetTXRateStatistics), NULL}, 
 	{GEN_CMD_CODE(_SetUsbSuspend), NULL}, 
 	{GEN_CMD_CODE(_SetH2cLbk), NULL},
 	{GEN_CMD_CODE(_AddBAReq), NULL}, /*45*/

	{GEN_CMD_CODE(_SetChannel), NULL},		/*46*/

// MP_OFFLOAD Start (47~54)
	{GEN_CMD_CODE(_SetTxPower), NULL},
	{GEN_CMD_CODE(_SwitchAntenna), NULL},
	{GEN_CMD_CODE(_SetCrystalCap), NULL},
	{GEN_CMD_CODE(_SetSingleCarrierTx), NULL},	/*50*/
	{GEN_CMD_CODE(_SetSingleToneTx), NULL},
	{GEN_CMD_CODE(_SetCarrierSuppressionTx), NULL},
	{GEN_CMD_CODE(_SetContinuousTx), NULL},
	{GEN_CMD_CODE(_SwitchBandwidth), NULL},		/*54*/
// MP_OFFLOAD End

	{GEN_CMD_CODE(_TX_Beacon), NULL},        /*55*/
	{GEN_CMD_CODE(_SetPowerTracking), NULL},
	{GEN_CMD_CODE(_AMSDU_TO_AMPDU), NULL}, /*57*/ 
	{GEN_CMD_CODE(_SetMacAddress), NULL}, /*58*/	

	{GEN_CMD_CODE(_DisconnectCtrl),NULL}, /*59*/ 
	{GEN_CMD_CODE(_SetChannelPlan),NULL}, /*60*/
	{GEN_CMD_CODE(_DisconnectCtrlEx),NULL}, /*61*/
	{GEN_CMD_CODE(_WWlanCtrl), NULL}, /*62*/
	{GEN_CMD_CODE(_SetPwrParam), NULL}, /*63*/
	
#if 1//To do, modify these h2c cmd, add or delete
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
	{_SetPT_CMD_,NULL},
	{GEN_CMD_CODE(_ReadTSSI), &readtssi_cmdrsp_callback}
#endif
};
#endif


#ifdef CONFIG_MLME_EXT

struct cmd_hdl {
	uint	parmsize;
	u8 (*h2cfuns)(struct _ADAPTER *padapter, u8 *pbuf);	
};


u8 read_macreg_hdl(_adapter *padapter, u8 *pbuf);
u8 write_macreg_hdl(_adapter *padapter, u8 *pbuf);
u8 read_bbreg_hdl(_adapter *padapter, u8 *pbuf);
u8 write_bbreg_hdl(_adapter *padapter, u8 *pbuf);
u8 read_rfreg_hdl(_adapter *padapter, u8 *pbuf);
u8 write_rfreg_hdl(_adapter *padapter, u8 *pbuf);

#define GEN_DRV_CMD_HANDLER(size, cmd)	{size, &cmd ## _hdl},

extern u8 r871x_NULL_hdl(_adapter *padapter, u8 *pbuf);
extern u8 r871x_joinbss_hdl(_adapter *padapter, u8 *pbuf);	
extern u8 r871x_disconnect_hdl(_adapter *padapter, u8 *pbuf);
extern u8 r871x_createbss_hdl(_adapter *padapter, u8 *pbuf);
extern u8 r871x_setopmode_hdl(_adapter *padapter, u8 *pbuf);
extern u8 r871x_sitesurvey_hdl(_adapter *padapter, u8 *pbuf);	
extern u8 r871x_setauth_hdl(_adapter *padapter, u8 *pbuf);
extern u8 r871x_setkey_hdl(_adapter *padapter, u8 *pbuf);
extern u8 r871x_set_stakey_hdl(_adapter *padapter, u8 *pbuf);
extern u8 r871x_set_assocsta_hdl(_adapter *padapter, u8 *pbuf);
extern u8 r871x_del_assocsta_hdl(_adapter *padapter, u8 *pbuf);

#define GEN_MLME_CMD_HANDLER(size, cmd)	{size, &r871x_ ## cmd ## _hdl},

#ifdef _RTL8712_CMD_C_
struct cmd_hdl	wlancmds[] = 
{
	GEN_DRV_CMD_HANDLER(0, read_macreg) /*0*/
	GEN_DRV_CMD_HANDLER(0, write_macreg)
	GEN_MLME_CMD_HANDLER(0, NULL)
	GEN_MLME_CMD_HANDLER(0, NULL)
	GEN_MLME_CMD_HANDLER(0, NULL)
	GEN_MLME_CMD_HANDLER(0, NULL)
	GEN_MLME_CMD_HANDLER(0, NULL)
	GEN_MLME_CMD_HANDLER(0, NULL)
	GEN_MLME_CMD_HANDLER(0, NULL)
	GEN_MLME_CMD_HANDLER(0, NULL)
	
	GEN_MLME_CMD_HANDLER(0, NULL) /*10*/
	GEN_MLME_CMD_HANDLER(0, NULL)
	GEN_MLME_CMD_HANDLER(0, NULL)
	GEN_MLME_CMD_HANDLER(0, NULL)		
	GEN_MLME_CMD_HANDLER(sizeof (struct joinbss_parm), joinbss) /*14*/
	GEN_MLME_CMD_HANDLER(sizeof (struct disconnect_parm), NULL)
	GEN_MLME_CMD_HANDLER(sizeof (struct createbss_parm), createbss)
	GEN_MLME_CMD_HANDLER(sizeof (struct setopmode_parm), NULL)
	GEN_MLME_CMD_HANDLER(sizeof (struct sitesurvey_parm), sitesurvey) /*18*/
	GEN_MLME_CMD_HANDLER(sizeof (struct setauth_parm), setauth)
	
	GEN_MLME_CMD_HANDLER(sizeof (struct setkey_parm), setkey) /*20*/
	GEN_MLME_CMD_HANDLER(sizeof (struct set_stakey_parm), set_stakey)
	GEN_MLME_CMD_HANDLER(sizeof (struct set_assocsta_parm), NULL)
	GEN_MLME_CMD_HANDLER(sizeof (struct del_assocsta_parm), NULL)
	GEN_MLME_CMD_HANDLER(sizeof (struct setstapwrstate_parm), NULL)
	GEN_MLME_CMD_HANDLER(sizeof (struct setbasicrate_parm), NULL)
	GEN_MLME_CMD_HANDLER(sizeof (struct getbasicrate_parm), NULL)
	GEN_MLME_CMD_HANDLER(sizeof (struct setdatarate_parm), NULL)
	GEN_MLME_CMD_HANDLER(sizeof (struct getdatarate_parm), NULL)
	GEN_MLME_CMD_HANDLER(sizeof (struct setphyinfo_parm), NULL)
	
	GEN_MLME_CMD_HANDLER(sizeof (struct getphyinfo_parm), NULL)  /*30*/
	GEN_MLME_CMD_HANDLER(sizeof (struct setphy_parm), NULL)
	GEN_MLME_CMD_HANDLER(sizeof (struct getphy_parm), NULL)
	GEN_MLME_CMD_HANDLER(0, NULL)
	GEN_MLME_CMD_HANDLER(0, NULL)
	GEN_MLME_CMD_HANDLER(0, NULL)
	GEN_MLME_CMD_HANDLER(0, NULL)
	GEN_MLME_CMD_HANDLER(0, NULL)
	GEN_MLME_CMD_HANDLER(0, NULL)
	GEN_MLME_CMD_HANDLER(0, NULL)
	
	GEN_MLME_CMD_HANDLER(0, NULL)	/*40*/
	GEN_MLME_CMD_HANDLER(0, NULL)
	GEN_MLME_CMD_HANDLER(0, NULL)
	GEN_MLME_CMD_HANDLER(0, NULL)
	
};
#endif

#endif//end of CONFIG_MLME_EXT

u8 read_macreg_cmd(_adapter  *padapter, u32 offset, u8 *pval);

#endif


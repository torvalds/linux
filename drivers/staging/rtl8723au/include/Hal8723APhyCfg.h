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
 *
 ******************************************************************************/
#ifndef __INC_HAL8723PHYCFG_H__
#define __INC_HAL8723PHYCFG_H__

/*--------------------------Define Parameters-------------------------------*/
#define MAX_AGGR_NUM	0x0909

/*------------------------------Define structure----------------------------*/
enum RF_RADIO_PATH {
	RF_PATH_A = 0,			/* Radio Path A */
	RF_PATH_B = 1,			/* Radio Path B */
	RF_PATH_MAX			/* Max RF number 90 support */
};

#define CHANNEL_MAX_NUMBER		14	/*  14 is the max channel number */

enum WIRELESS_MODE {
	WIRELESS_MODE_UNKNOWN	= 0x00,
	WIRELESS_MODE_A		= BIT(2),
	WIRELESS_MODE_B		= BIT(0),
	WIRELESS_MODE_G		= BIT(1),
	WIRELESS_MODE_AUTO	= BIT(5),
	WIRELESS_MODE_N_24G	= BIT(3),
	WIRELESS_MODE_N_5G	= BIT(4),
	WIRELESS_MODE_AC	= BIT(6)
};

/* BB/RF related */
enum rf_type_8190p {
	RF_TYPE_MIN,		/*  0 */
	RF_8225 = 1,		/*  1 11b/g RF for verification only */
	RF_8256 = 2,		/*  2 11b/g/n */
	RF_8258 = 3,		/*  3 11a/b/g/n RF */
	RF_6052 = 4,		/*  4 11b/g/n RF */
};

struct bb_reg_define {
	u32 rfintfs;		/*  set software control: */
				/*		0x870~0x877[8 bytes] */
	u32 rfintfi;		/*  readback data: */
				/*		0x8e0~0x8e7[8 bytes] */
	u32 rfintfo;		/*  output data: */
				/*		0x860~0x86f [16 bytes] */
	u32 rfintfe;		/*  output enable: */
				/*		0x860~0x86f [16 bytes] */
	u32 rf3wireOffset;	/*  LSSI data: */
				/*		0x840~0x84f [16 bytes] */
	u32 rfLSSI_Select;	/*  BB Band Select: */
				/*		0x878~0x87f [8 bytes] */
	u32 rfTxGainStage;	/*  Tx gain stage: */
				/*		0x80c~0x80f [4 bytes] */
	u32 rfHSSIPara1;	/*  wire parameter control1 : */
				/*		0x820~0x823, 0x828~0x82b, 0x830~0x833, 0x838~0x83b [16 bytes] */
	u32 rfHSSIPara2;	/*  wire parameter control2 : */
				/*		0x824~0x827, 0x82c~0x82f, 0x834~0x837, 0x83c~0x83f [16 bytes] */
	u32 rfSwitchControl; /* Tx Rx antenna control : */
				/*		0x858~0x85f [16 bytes] */
	u32 rfAGCControl1;	/* AGC parameter control1 : */
				/*	0xc50~0xc53, 0xc58~0xc5b, 0xc60~0xc63, 0xc68~0xc6b [16 bytes] */
	u32 rfAGCControl2;	/* AGC parameter control2 : */
				/*		0xc54~0xc57, 0xc5c~0xc5f, 0xc64~0xc67, 0xc6c~0xc6f [16 bytes] */
	u32 rfRxIQImbalance; /* OFDM Rx IQ imbalance matrix : */
				/*		0xc14~0xc17, 0xc1c~0xc1f, 0xc24~0xc27, 0xc2c~0xc2f [16 bytes] */
	u32 rfRxAFE;		/* Rx IQ DC ofset and Rx digital filter, Rx DC notch filter : */
				/*	0xc10~0xc13, 0xc18~0xc1b, 0xc20~0xc23, 0xc28~0xc2b [16 bytes] */
	u32 rfTxIQImbalance; /* OFDM Tx IQ imbalance matrix */
				/*	0xc80~0xc83, 0xc88~0xc8b, 0xc90~0xc93, 0xc98~0xc9b [16 bytes] */
	u32 rfTxAFE;		/* Tx IQ DC Offset and Tx DFIR type */
				/*	0xc84~0xc87, 0xc8c~0xc8f, 0xc94~0xc97, 0xc9c~0xc9f [16 bytes] */
	u32 rfLSSIReadBack;	/* LSSI RF readback data SI mode */
				/*	0x8a0~0x8af [16 bytes] */
	u32 rfLSSIReadBackPi;	/* LSSI RF readback data PI mode 0x8b8-8bc for Path A and B */
};

struct r_antenna_sel_ofdm {
	u32			r_tx_antenna:4;
	u32			r_ant_l:4;
	u32			r_ant_non_ht:4;
	u32			r_ant_ht1:4;
	u32			r_ant_ht2:4;
	u32			r_ant_ht_s1:4;
	u32			r_ant_non_ht_s1:4;
	u32			OFDM_TXSC:2;
	u32			Reserved:2;
};

struct r_antenna_sel_cck {
	u8			r_cckrx_enable_2:2;
	u8			r_cckrx_enable:2;
	u8			r_ccktx_enable:4;
};

/*------------------------------Define structure----------------------------*/


/*------------------------Export global variable----------------------------*/
/*------------------------Export global variable----------------------------*/


/*------------------------Export Macro Definition---------------------------*/
/*------------------------Export Macro Definition---------------------------*/


/*--------------------------Exported Function prototype---------------------*/
/*  */
/*  BB and RF register read/write */
/*  */
u32	PHY_QueryBBReg(struct rtw_adapter *Adapter, u32 RegAddr,
		       u32 BitMask);
void	PHY_SetBBReg(struct rtw_adapter *Adapter, u32 RegAddr,
		     u32 BitMask, u32 Data);
u32	PHY_QueryRFReg(struct rtw_adapter *Adapter,
		       enum RF_RADIO_PATH	eRFPath, u32 RegAddr,
		       u32 BitMask);
void	PHY_SetRFReg(struct rtw_adapter *Adapter,
		     enum RF_RADIO_PATH eRFPath, u32 RegAddr,
		     u32 BitMask,  u32	Data);

/*  */
/*  BB TX Power R/W */
/*  */
void PHY_SetTxPowerLevel8723A(struct rtw_adapter *Adapter, u8 channel);

/*  */
/*  Switch bandwidth for 8723A */
/*  */
void	PHY_SetBWMode23a8723A(struct rtw_adapter *pAdapter,
			   enum ht_channel_width ChnlWidth,
			   unsigned char Offset);

/*  */
/*  channel switch related funciton */
/*  */
void	PHY_SwChnl8723A(struct rtw_adapter *pAdapter, u8 channel);
				/*  Call after initialization */
void ChkFwCmdIoDone(struct rtw_adapter *Adapter);

/*  */
/*  Modify the value of the hw register when beacon interval be changed. */
/*  */
void
rtl8192c_PHY_SetBeaconHwReg(struct rtw_adapter *Adapter, u16 BeaconInterval);


void PHY_SwitchEphyParameter(struct rtw_adapter *Adapter);

void PHY_EnableHostClkReq(struct rtw_adapter *Adapter);

bool
SetAntennaConfig92C(struct rtw_adapter *Adapter, u8 DefaultAnt);

/*--------------------------Exported Function prototype---------------------*/

#define PHY_SetMacReg	PHY_SetBBReg

/* MAC/BB/RF HAL config */
int PHY_BBConfig8723A(struct rtw_adapter *Adapter);
s32 PHY_MACConfig8723A(struct rtw_adapter *padapter);

#endif

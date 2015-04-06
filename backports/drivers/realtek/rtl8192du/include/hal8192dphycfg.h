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
/*****************************************************************************
 *
 * Module:	__INC_HAL8192DPHYCFG_H
 *
 *
 * Note:
 *
 *
 * Export:	Constants, macro, functions(API), global variables(None).
 *
 * Abbrev:
 *
 * History:
 *		Data		Who		Remark
 *      08/07/2007  MHC		1. Porting from 9x series PHYCFG.h.
 *							2. Reorganize code architecture.
 *
 *****************************************************************************/
 /* Check to see if the file has been included already.  */
#ifndef __INC_HAL8192DPHYCFG_H
#define __INC_HAL8192DPHYCFG_H


/*--------------------------Define Parameters-------------------------------*/
#define LOOP_LIMIT				5
#define MAX_STALL_TIME			50		/* us */
#define AntennaDiversityValue	0x80	/* adapter->bSoftwareAntennaDiversity ? 0x00:0x80) */
#define MAX_TXPWR_IDX_NMODE_92S	63
#define Reset_Cnt_Limit			3


#define IQK_MAC_REG_NUM		4
#define IQK_ADDA_REG_NUM		16
#define IQK_BB_REG_NUM			10
#define IQK_BB_REG_NUM_92C	9
#define IQK_BB_REG_NUM_92D	10
#define IQK_BB_REG_NUM_test	6
#define index_mapping_NUM		13
#define Rx_index_mapping_NUM	15
#define AVG_THERMAL_NUM		8
#define IQK_Matrix_REG_NUM	8
#define IQK_Matrix_Settings_NUM	1+24+21

/*--------------------------Define Parameters-------------------------------*/


/*------------------------------Define structure----------------------------*/
enum swchnl_cmdid {
	cmdid_end,
	cmdid_settxpowerlevel,
	cmdid_bbregwrite10,
	cmdid_writeportulong,
	cmdid_writeportushort,
	cmdid_writeportuchar,
	cmdid_rf_writereg,
};


/* 1. Switch channel related */
struct sw_chnl_cmd {
	enum swchnl_cmdid	cmdid;
	u32			Para1;
	u32			Para2;
	u32			msDelay;
};

enum HW90_BLOCK {
	HW90_BLOCK_MAC = 0,
	HW90_BLOCK_PHY0 = 1,
	HW90_BLOCK_PHY1 = 2,
	HW90_BLOCK_RF = 3,
	HW90_BLOCK_MAXIMUM = 4, /*  Never use this */
};

/* vivi added this for read parameter from header, 20100908 */
enum RF_CONTENT {
	radioa_txt = 0x1000,
	radiob_txt = 0x1001,
	radioc_txt = 0x1002,
	radiod_txt = 0x1003
};

#define	RF_PATH_MAX			2

enum WIRELESS_MODE {
	WIRELESS_MODE_UNKNOWN = 0x00,
	WIRELESS_MODE_A = 0x01,
	WIRELESS_MODE_B = 0x02,
	WIRELESS_MODE_G = 0x04,
	WIRELESS_MODE_AUTO = 0x08,
	WIRELESS_MODE_N_24G = 0x10,
	WIRELESS_MODE_N_5G = 0x20
};


#define CHANNEL_MAX_NUMBER		14+24+21	/*  14 is the max channel number */
#define CHANNEL_GROUP_MAX		3+9	/*  ch1~3, ch4~9, ch10~14 total three groups */
#define MAX_PG_GROUP 13

#define	CHANNEL_GROUP_MAX_2G		3
#define	CHANNEL_GROUP_IDX_5GL		3
#define	CHANNEL_GROUP_IDX_5GM		6
#define	CHANNEL_GROUP_IDX_5GH		9
#define	CHANNEL_GROUP_MAX_5G		9
#define	CHANNEL_MAX_NUMBER_2G		14

enum BaseBand_Config_Type {
	BaseBand_Config_PHY_REG = 0,			/* Radio Path A */
	BaseBand_Config_AGC_TAB = 1,			/* Radio Path B */
};

enum MACPHY_MODE_8192D {
	SINGLEMAC_SINGLEPHY,
	DUALMAC_DUALPHY,
	DUALMAC_SINGLEPHY,
};

enum MACPHY_MODE_CHANGE_ACTION {
	DMDP2DMSP = 0,
	DMSP2DMDP = 1,
	DMDP2SMSP = 2,
	SMSP2DMDP = 3,
	DMSP2SMSP = 4,
	SMSP2DMSP = 5,
	MAXACTION
};

enum BAND_TYPE {
	BAND_ON_2_4G = 0,
	BAND_ON_5G,
	BAND_ON_BOTH,
	BANDMAX
};

enum PHY_Rate_Tx_Power_Offset_Area {
	RA_OFFSET_LEGACY_OFDM1,
	RA_OFFSET_LEGACY_OFDM2,
	RA_OFFSET_HT_OFDM1,
	RA_OFFSET_HT_OFDM2,
	RA_OFFSET_HT_OFDM3,
	RA_OFFSET_HT_OFDM4,
	RA_OFFSET_HT_CCK,
};


/* BB/RF related */
enum RF_TYPE_8190P {
	RF_TYPE_MIN,	/*  0 */
	RF_8225=1,			/*  1 11b/g RF for verification only */
	RF_8256=2,			/*  2 11b/g/n */
	RF_8258=3,			/*  3 11a/b/g/n RF */
	RF_6052=4,		/*  4 11b/g/n RF */
	/*  TODO: We should remove this psudo PHY RF after we get new RF. */
	RF_PSEUDO_11N=5,	/*  5, It is a temporality RF. */
};

struct bb_register_def {
	u32 rfintfs;		/*  set software control: */
				/* 		0x870~0x877[8 bytes] */
	u32 rfintfi;		/*  readback data: */
				/* 		0x8e0~0x8e7[8 bytes] */
	u32 rfintfo;		/*  output data: */
				/* 		0x860~0x86f [16 bytes] */
	u32 rfintfe;		/*  output enable: */
				/* 		0x860~0x86f [16 bytes] */
	u32 rf3wireOffset;	/*  LSSI data: */
				/* 		0x840~0x84f [16 bytes] */
	u32 rfLSSI_Select;	/*  BB Band Select: */
				/* 		0x878~0x87f [8 bytes] */
	u32 rfTxGainStage;	/*  Tx gain stage: */
				/* 		0x80c~0x80f [4 bytes] */
	u32 rfHSSIPara1;	/*  wire parameter control1 : */
				/* 		0x820~0x823,0x828~0x82b, 0x830~0x833, 0x838~0x83b [16 bytes] */
	u32 rfHSSIPara2;	/*  wire parameter control2 : */
				/* 		0x824~0x827,0x82c~0x82f, 0x834~0x837, 0x83c~0x83f [16 bytes] */
	u32 rfSwitchControl;	/* Tx Rx antenna control : */
				/* 		0x858~0x85f [16 bytes] */
	u32 rfAGCControl1;	/* AGC parameter control1 : */
				/* 		0xc50~0xc53,0xc58~0xc5b, 0xc60~0xc63, 0xc68~0xc6b [16 bytes] */
	u32 rfAGCControl2;	/* AGC parameter control2 : */
				/* 		0xc54~0xc57,0xc5c~0xc5f, 0xc64~0xc67, 0xc6c~0xc6f [16 bytes] */
	u32 rfRxIQImbalance;	/* OFDM Rx IQ imbalance matrix : */
				/* 		0xc14~0xc17,0xc1c~0xc1f, 0xc24~0xc27, 0xc2c~0xc2f [16 bytes] */
	u32 rfRxAFE;		/* Rx IQ DC ofset and Rx digital filter, Rx DC notch filter : */
				/* 		0xc10~0xc13,0xc18~0xc1b, 0xc20~0xc23, 0xc28~0xc2b [16 bytes] */
	u32 rfTxIQImbalance;	/* OFDM Tx IQ imbalance matrix */
				/* 		0xc80~0xc83,0xc88~0xc8b, 0xc90~0xc93, 0xc98~0xc9b [16 bytes] */
	u32 rfTxAFE;		/* Tx IQ DC Offset and Tx DFIR type */
				/* 		0xc84~0xc87,0xc8c~0xc8f, 0xc94~0xc97, 0xc9c~0xc9f [16 bytes] */
	u32 rfLSSIReadBack;	/* LSSI RF readback data SI mode */
				/* 		0x8a0~0x8af [16 bytes] */
	u32 rfLSSIReadBackPi;	/* LSSI RF readback data PI mode 0x8b8-8bc for Path A and B */
};

struct antenna_select_ofdm {
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

struct antenna_select_cck {
	u8			r_cckrx_enable_2:2;
	u8			r_cckrx_enable:2;
	u8			r_ccktx_enable:4;
};

/*------------------------------Define structure----------------------------*/


/*------------------------Export global variable----------------------------*/
/*------------------------Export global variable----------------------------*/


/*------------------------Export Marco Definition---------------------------*/
/*------------------------Export Marco Definition---------------------------*/

/* Added for TX Power */
/* u8 GetRightChnlPlace(u8 chnl); */
u8 rtl8192d_GetRightChnlPlaceforIQK(u8 chnl);
u8 rtl8192d_getChnlGroupfromArray(u8 chnl);
/*--------------------------Exported Function prototype---------------------*/
/*  */
/*  BB and RF register read/write */
/*  */
void	rtl8192d_PHY_SetBBReg1Byte(	struct rtw_adapter *	adapter,
								u32		RegAddr,
								u32		BitMask,
								u32		Data	);
u32	rtl8192d_PHY_QueryBBReg(	struct rtw_adapter *	adapter,
								u32		RegAddr,
								u32		BitMask	);
void	rtl8192d_PHY_SetBBReg(	struct rtw_adapter *	adapter,
								u32		RegAddr,
								u32		BitMask,
								u32		Data	);
u32	rtl8192d_PHY_QueryRFReg(	struct rtw_adapter *			adapter,
								enum RF_RADIO_PATH_E	eRFPath,
								u32				RegAddr,
								u32				BitMask	);
void	rtl8192d_PHY_SetRFReg(	struct rtw_adapter *			adapter,
								enum RF_RADIO_PATH_E	eRFPath,
								u32				RegAddr,
								u32				BitMask,
								u32				Data	);

/*  */
/*  Initialization related function */
/*  */
/* MAC/BB/RF HAL config */
int	PHY_MACConfig8192D(	struct rtw_adapter *	adapter	);
int	PHY_BBConfig8192D(	struct rtw_adapter *	adapter	);
int	PHY_RFConfig8192D(	struct rtw_adapter *	adapter	);
/* RF config */
int	rtl8192d_PHY_ConfigRFWithHeaderFile(	struct rtw_adapter *			adapter,
						enum RF_CONTENT			Content,
						enum RF_RADIO_PATH_E	eRFPath);
/* BB/RF readback check for making sure init OK */
int	rtl8192d_PHY_CheckBBAndRFOK(	struct rtw_adapter *			adapter,
				enum HW90_BLOCK		CheckBlock,
				enum RF_RADIO_PATH_E	eRFPath	 );
/* Read initi reg value for tx power setting. */
void	rtl8192d_PHY_GetHWRegOriginalValue(	struct rtw_adapter *		adapter	);

/*  */
/*  RF Power setting */
/*  */

/*  */
/*  BB TX Power R/W */
/*  */
void	PHY_GetTxPowerLevel8192D(	struct rtw_adapter *		adapter,
											u32*		powerlevel	);
void	PHY_SetTxPowerLevel8192D(	struct rtw_adapter *		adapter,
											u8			channel	);
bool	PHY_UpdateTxPowerDbm8192D(	struct rtw_adapter *	adapter,
											int		powerInDbm	);

/*  */
/*  Switch bandwidth for 8192S */
/*  */
void	PHY_SetBWMode8192D(	struct rtw_adapter *			adapter,
			enum HT_CHANNEL_WIDTH	ChnlWidth,
									unsigned char	Offset	);

/*  */
/*  Set FW CMD IO for 8192S. */
/*  */

/*  */
/*  Set A2 entry to fw for 8192S */
/*  */
extern	void FillA2Entry8192C(		struct rtw_adapter *			adapter,
										u8				index,
										u8*				val);


/*  */
/*  channel switch related funciton */
/*  */
void	PHY_SwChnl8192D(	struct rtw_adapter *		adapter,
									u8			channel	);
				/*  Call after initialization */
void	PHY_SwChnlPhy8192D(	struct rtw_adapter *		adapter,
									u8			channel	);

extern void ChkFwCmdIoDone(	struct rtw_adapter *	adapter);

/*  */
/*  IQ calibrate */
/*  */
void	rtl8192d_PHY_IQCalibrate(	struct rtw_adapter *	adapter);


/*  */
/*  LC calibrate */
/*  */
void	rtl8192d_PHY_LCCalibrate(struct rtw_adapter *	adapter);

/*  */
/*  AP calibrate */
/*  */
void	rtl8192d_PHY_APCalibrate(struct rtw_adapter *	adapter, char		delta);

extern	void
PHY_SwitchEphyParameter(
	struct rtw_adapter *			adapter
	);

extern	void
PHY_EnableHostClkReq(
	struct rtw_adapter *			adapter
	);

bool
SetAntennaConfig92C(
	struct rtw_adapter *	adapter,
	u8		DefaultAnt
	);

void
PHY_StopTRXBeforeChangeBand8192D(
	  struct rtw_adapter *		adapter
);

void
PHY_UpdateBBRFConfiguration8192D(
	struct rtw_adapter * adapter,
	bool bisBandSwitch
);

void PHY_ReadMacPhyMode92D(
	struct rtw_adapter *	adapter,
	bool	AutoloadFail
);

void PHY_ConfigMacPhyMode92D(
	struct rtw_adapter *	adapter
);

void PHY_ConfigMacPhyModeInfo92D(
	struct rtw_adapter *	adapter
);

void PHY_ConfigMacCoexist_RFPage92D(
	struct rtw_adapter *	adapter
);

void
rtl8192d_PHY_InitRxSetting(
	struct rtw_adapter * adapter
);

void
rtl8192d_PHY_ResetIQKResult(
	struct rtw_adapter * adapter
);


void
rtl8192d_PHY_SetRFPathSwitch(struct rtw_adapter *	adapter, bool		bMain);

void
HalChangeCCKStatus8192D(
	struct rtw_adapter *	adapter,
	bool		bCCKDisable
);

void PHY_InitPABias92D(struct rtw_adapter * adapter);

/*--------------------------Exported Function prototype---------------------*/

#define PHY_SetBBReg1Byte(adapter, RegAddr, BitMask, Data) rtl8192d_PHY_SetBBReg1Byte((adapter), (RegAddr), (BitMask), (Data))
#define PHY_QueryBBReg(adapter, RegAddr, BitMask) rtl8192d_PHY_QueryBBReg((adapter), (RegAddr), (BitMask))
#define PHY_SetBBReg(adapter, RegAddr, BitMask, Data) rtl8192d_PHY_SetBBReg((adapter), (RegAddr), (BitMask), (Data))
#define PHY_QueryRFReg(adapter, eRFPath, RegAddr, BitMask) rtl8192d_PHY_QueryRFReg((adapter), (eRFPath), (RegAddr), (BitMask))
#define PHY_SetRFReg(adapter, eRFPath, RegAddr, BitMask, Data) rtl8192d_PHY_SetRFReg((adapter), (eRFPath), (RegAddr), (BitMask), (Data))

#define PHY_SetMacReg	PHY_SetBBReg

#endif	/*  __INC_HAL8192SPHYCFG_H */

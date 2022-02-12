/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2007 - 2011 Realtek Corporation. */

#ifndef __INC_HAL8188EPHYCFG_H__
#define __INC_HAL8188EPHYCFG_H__

#define MAX_AGGR_NUM			0x07

enum rf_radio_path {
	RF_PATH_A = 0,			/* Radio Path A */
	RF_PATH_B = 1,			/* Radio Path B */
};

#define MAX_PG_GROUP 13

#define	RF_PATH_MAX			3
#define		MAX_TX_COUNT		4 /* path numbers */

#define CHANNEL_MAX_NUMBER		14	/*  14 is the max chnl number */
#define MAX_CHNL_GROUP_24G		6	/*  ch1~2, ch3~5, ch6~8,
						 *ch9~11, ch12~13, CH 14
						 * total three groups */

struct bb_reg_def {
	u32 rfintfs;		/*  set software control: */
				/*	0x870~0x877[8 bytes] */
	u32 rfintfi;		/*  readback data: */
				/*	0x8e0~0x8e7[8 bytes] */
	u32 rfintfo;		/*  output data: */
				/*	0x860~0x86f [16 bytes] */
	u32 rfintfe;		/*  output enable: */
				/*	0x860~0x86f [16 bytes] */
	u32 rf3wireOffset;	/*  LSSI data: */
				/*	0x840~0x84f [16 bytes] */
	u32 rfLSSI_Select;	/*  BB Band Select: */
				/*	0x878~0x87f [8 bytes] */
	u32 rfTxGainStage;	/*  Tx gain stage: */
				/*	0x80c~0x80f [4 bytes] */
	u32 rfHSSIPara1;	/*  wire parameter control1 : */
				/*	0x820~0x823,0x828~0x82b,
				 *	0x830~0x833, 0x838~0x83b [16 bytes] */
	u32 rfHSSIPara2;	/*  wire parameter control2 : */
				/*	0x824~0x827,0x82c~0x82f, 0x834~0x837,
				 *	0x83c~0x83f [16 bytes] */
	u32 rfSwitchControl;	/* Tx Rx antenna control : */
				/*	0x858~0x85f [16 bytes] */
	u32 rfAGCControl1;	/* AGC parameter control1 : */
				/*	0xc50~0xc53,0xc58~0xc5b, 0xc60~0xc63,
				 * 0xc68~0xc6b [16 bytes] */
	u32 rfAGCControl2;	/* AGC parameter control2 : */
				/*	0xc54~0xc57,0xc5c~0xc5f, 0xc64~0xc67,
				 *	0xc6c~0xc6f [16 bytes] */
	u32 rfRxIQImbalance;	/* OFDM Rx IQ imbalance matrix : */
				/*	0xc14~0xc17,0xc1c~0xc1f, 0xc24~0xc27,
				 *	0xc2c~0xc2f [16 bytes] */
	u32 rfRxAFE;		/* Rx IQ DC ofset and Rx digital filter,
				 * Rx DC notch filter : */
				/*	0xc10~0xc13,0xc18~0xc1b, 0xc20~0xc23,
				 *	0xc28~0xc2b [16 bytes] */
	u32 rfTxIQImbalance;	/* OFDM Tx IQ imbalance matrix */
				/*	0xc80~0xc83,0xc88~0xc8b, 0xc90~0xc93,
				 *	 0xc98~0xc9b [16 bytes] */
	u32 rfTxAFE;		/* Tx IQ DC Offset and Tx DFIR type */
				/*	0xc84~0xc87,0xc8c~0xc8f, 0xc94~0xc97,
				 *	0xc9c~0xc9f [16 bytes] */
	u32 rfLSSIReadBack;	/* LSSI RF readback data SI mode */
				/*	0x8a0~0x8af [16 bytes] */
	u32 rfLSSIReadBackPi;	/* LSSI RF readback data PI mode 0x8b8-8bc for
				 * Path A and B */
};

/*  BB and RF register read/write */
u32 rtl8188e_PHY_QueryBBReg(struct adapter *adapter, u32 regaddr, u32 mask);
void rtl8188e_PHY_SetBBReg(struct adapter *Adapter, u32 RegAddr,
			   u32 mask, u32 data);
u32 rtl8188e_PHY_QueryRFReg(struct adapter *adapter, enum rf_radio_path rfpath,
			    u32 regaddr, u32 mask);
void rtl8188e_PHY_SetRFReg(struct adapter *adapter, u32 regaddr, u32 mask, u32 data);

/*  Initialization related function */
/* MAC/BB/RF HAL config */
int PHY_MACConfig8188E(struct adapter *adapter);
int PHY_BBConfig8188E(struct adapter *adapter);
int PHY_RFConfig8188E(struct adapter *adapter);

/*  BB TX Power R/W */
void PHY_SetTxPowerLevel8188E(struct adapter *adapter, u8 channel);

/*  Switch bandwidth for 8192S */
void PHY_SetBWMode8188E(struct adapter *adapter,
			enum ht_channel_width chnlwidth, unsigned char offset);

/*  channel switch related funciton */
void PHY_SwChnl8188E(struct adapter *adapter, u8 channel);

void storePwrIndexDiffRateOffset(struct adapter *adapter, u32 regaddr,
				 u32 mask, u32 data);

#endif

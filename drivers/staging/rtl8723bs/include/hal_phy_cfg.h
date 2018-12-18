/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 *
 ******************************************************************************/
#ifndef __INC_HAL8723BPHYCFG_H__
#define __INC_HAL8723BPHYCFG_H__

/*--------------------------Define Parameters-------------------------------*/
#define LOOP_LIMIT				5
#define MAX_STALL_TIME			50		/* us */
#define AntennaDiversityValue	0x80	/* Adapter->bSoftwareAntennaDiversity ? 0x00:0x80) */
#define MAX_TXPWR_IDX_NMODE_92S	63
#define Reset_Cnt_Limit			3

#define MAX_AGGR_NUM	0x07


/*--------------------------Define Parameters End-------------------------------*/


/*------------------------------Define structure----------------------------*/

/*------------------------------Define structure End----------------------------*/

/*--------------------------Exported Function prototype---------------------*/
u32
PHY_QueryBBReg_8723B(
struct adapter *Adapter,
u32 	RegAddr,
u32 	BitMask
	);

void
PHY_SetBBReg_8723B(
struct adapter *Adapter,
u32 	RegAddr,
u32 	BitMask,
u32 	Data
	);

u32
PHY_QueryRFReg_8723B(
struct adapter *		Adapter,
u8 		eRFPath,
u32 			RegAddr,
u32 			BitMask
	);

void
PHY_SetRFReg_8723B(
struct adapter *		Adapter,
u8 		eRFPath,
u32 			RegAddr,
u32 			BitMask,
u32 			Data
	);

/* MAC/BB/RF HAL config */
int PHY_BBConfig8723B(struct adapter *Adapter	);

int PHY_RFConfig8723B(struct adapter *Adapter	);

s32 PHY_MACConfig8723B(struct adapter *padapter);

void
PHY_SetTxPowerIndex_8723B(
struct adapter *		Adapter,
u32 				PowerIndex,
u8 			RFPath,
u8 			Rate
	);

u8
PHY_GetTxPowerIndex_8723B(
struct adapter *		padapter,
u8 			RFPath,
u8 			Rate,
enum CHANNEL_WIDTH		BandWidth,
u8 			Channel
	);

void
PHY_GetTxPowerLevel8723B(
struct adapter *	Adapter,
	s32*			powerlevel
	);

void
PHY_SetTxPowerLevel8723B(
struct adapter *	Adapter,
u8 	channel
	);

void
PHY_SetBWMode8723B(
struct adapter *			Adapter,
enum CHANNEL_WIDTH			Bandwidth,	/*  20M or 40M */
unsigned char 			Offset		/*  Upper, Lower, or Don't care */
);

void
PHY_SwChnl8723B(/*  Call after initialization */
struct adapter *Adapter,
u8 channel
	);

void
PHY_SetSwChnlBWMode8723B(
struct adapter *		Adapter,
u8 			channel,
enum CHANNEL_WIDTH		Bandwidth,
u8 			Offset40,
u8 			Offset80
);

/*--------------------------Exported Function prototype End---------------------*/

#endif

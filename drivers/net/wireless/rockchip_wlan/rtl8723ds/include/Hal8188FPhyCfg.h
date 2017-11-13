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
#ifndef __INC_HAL8188FPHYCFG_H__
#define __INC_HAL8188FPHYCFG_H__

/*--------------------------Define Parameters-------------------------------*/
#define LOOP_LIMIT				5
#define MAX_STALL_TIME			50		/* us */
#define AntennaDiversityValue	0x80	/* (Adapter->bSoftwareAntennaDiversity ? 0x00 : 0x80) */
#define MAX_TXPWR_IDX_NMODE_92S	63
#define Reset_Cnt_Limit			3

#ifdef CONFIG_PCI_HCI
	#define MAX_AGGR_NUM	0x0B
#else
	#define MAX_AGGR_NUM	0x07
#endif /* CONFIG_PCI_HCI */


/*--------------------------Define Parameters End-------------------------------*/


/*------------------------------Define structure----------------------------*/

/*------------------------------Define structure End----------------------------*/

/*--------------------------Exported Function prototype---------------------*/
u32
PHY_QueryBBReg_8188F(
	IN	PADAPTER	Adapter,
	IN	u32		RegAddr,
	IN	u32		BitMask
);

VOID
PHY_SetBBReg_8188F(
	IN	PADAPTER	Adapter,
	IN	u32		RegAddr,
	IN	u32		BitMask,
	IN	u32		Data
);

u32
PHY_QueryRFReg_8188F(
	IN	PADAPTER			Adapter,
	IN	enum rf_path			eRFPath,
	IN	u32				RegAddr,
	IN	u32				BitMask
);

VOID
PHY_SetRFReg_8188F(
	IN	PADAPTER			Adapter,
	IN	enum rf_path			eRFPath,
	IN	u32				RegAddr,
	IN	u32				BitMask,
	IN	u32				Data
);

/* MAC/BB/RF HAL config */
int PHY_BBConfig8188F(PADAPTER	Adapter);

int PHY_RFConfig8188F(PADAPTER	Adapter);

s32 PHY_MACConfig8188F(PADAPTER padapter);

int
PHY_ConfigRFWithParaFile_8188F(
	IN	PADAPTER			Adapter,
	IN	u8					*pFileName,
	enum rf_path				eRFPath
);

VOID
PHY_SetTxPowerIndex_8188F(
	IN	PADAPTER			Adapter,
	IN	u32					PowerIndex,
	IN	enum rf_path			RFPath,
	IN	u8					Rate
);

u8
PHY_GetTxPowerIndex_8188F(
	IN	PADAPTER			pAdapter,
	IN	enum rf_path			RFPath,
	IN	u8					Rate,
	IN	u8		BandWidth,
	IN	u8					Channel,
	struct txpwr_idx_comp *tic
);

VOID
PHY_GetTxPowerLevel8188F(
	IN	PADAPTER		Adapter,
	OUT s32				*powerlevel
);

VOID
PHY_SetTxPowerLevel8188F(
	IN	PADAPTER		Adapter,
	IN	u8			channel
);

VOID
PHY_SetSwChnlBWMode8188F(
	IN	PADAPTER			Adapter,
	IN	u8					channel,
	IN	enum channel_width	Bandwidth,
	IN	u8					Offset40,
	IN	u8					Offset80
);

VOID phy_set_rf_path_switch_8188f(
	IN	PADAPTER	pAdapter,
	IN	bool		bMain
);

void BBTurnOnBlock_8188F(_adapter *adapter);

/*--------------------------Exported Function prototype End---------------------*/

#endif

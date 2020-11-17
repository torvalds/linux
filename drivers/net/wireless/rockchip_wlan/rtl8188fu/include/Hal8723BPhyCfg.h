/* SPDX-License-Identifier: GPL-2.0 */
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
#ifndef __INC_HAL8723BPHYCFG_H__
#define __INC_HAL8723BPHYCFG_H__

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
PHY_QueryBBReg_8723B(
		PADAPTER	Adapter,
		u32		RegAddr,
		u32		BitMask
);

void
PHY_SetBBReg_8723B(
		PADAPTER	Adapter,
		u32		RegAddr,
		u32		BitMask,
		u32		Data
);

u32
PHY_QueryRFReg_8723B(
		PADAPTER			Adapter,
		enum rf_path			eRFPath,
		u32				RegAddr,
		u32				BitMask
);

void
PHY_SetRFReg_8723B(
		PADAPTER			Adapter,
		enum rf_path			eRFPath,
		u32				RegAddr,
		u32				BitMask,
		u32				Data
);

/* MAC/BB/RF HAL config */
int PHY_BBConfig8723B(PADAPTER	Adapter);

int PHY_RFConfig8723B(PADAPTER	Adapter);

s32 PHY_MACConfig8723B(PADAPTER padapter);

int
PHY_ConfigRFWithParaFile_8723B(
		PADAPTER			Adapter,
		u8					*pFileName,
	enum rf_path				eRFPath
);

void
PHY_SetTxPowerIndex_8723B(
		PADAPTER			Adapter,
		u32					PowerIndex,
		enum rf_path			RFPath,
		u8					Rate
);

u8
PHY_GetTxPowerIndex_8723B(
		PADAPTER			pAdapter,
		enum rf_path			RFPath,
		u8					Rate,
		u8					BandWidth,
		u8					Channel,
	struct txpwr_idx_comp *tic
);

void
PHY_SetTxPowerLevel8723B(
		PADAPTER		Adapter,
		u8			channel
);

void
PHY_SetSwChnlBWMode8723B(
		PADAPTER			Adapter,
		u8					channel,
		enum channel_width	Bandwidth,
		u8					Offset40,
		u8					Offset80
);

void phy_set_rf_path_switch_8723b(
		struct dm_struct		*phydm,
		bool		bMain
);

/*--------------------------Exported Function prototype End---------------------*/

#endif

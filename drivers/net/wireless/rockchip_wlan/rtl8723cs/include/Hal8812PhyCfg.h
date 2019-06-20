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
#ifndef __INC_HAL8812PHYCFG_H__
#define __INC_HAL8812PHYCFG_H__


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


/*--------------------------Define Parameters-------------------------------*/

/*------------------------------Define structure----------------------------*/


/* BB/RF related */

/*------------------------------Define structure----------------------------*/


/*------------------------Export global variable----------------------------*/
/*------------------------Export global variable----------------------------*/


/*------------------------Export Marco Definition---------------------------*/
/*------------------------Export Marco Definition---------------------------*/


/*--------------------------Exported Function prototype---------------------*/
/*
 * BB and RF register read/write
 *   */
u32	PHY_QueryBBReg8812(PADAPTER	Adapter,
				u32			RegAddr,
				u32			BitMask);
void	PHY_SetBBReg8812(PADAPTER		Adapter,
				u32			RegAddr,
				u32			BitMask,
				u32			Data);
u32	PHY_QueryRFReg8812(PADAPTER	Adapter,
				enum rf_path	eRFPath,
				u32			RegAddr,
				u32			BitMask);
void	PHY_SetRFReg8812(PADAPTER		Adapter,
				enum rf_path	eRFPath,
				u32			RegAddr,
				u32			BitMask,
				u32			Data);

/*
 * Initialization related function
 *
 * MAC/BB/RF HAL config */
int	PHY_MACConfig8812(PADAPTER	Adapter);
int	PHY_BBConfig8812(PADAPTER	Adapter);
void	PHY_BB8812_Config_1T(PADAPTER	Adapter);
int	PHY_RFConfig8812(PADAPTER	Adapter);

/* RF config */

s32
PHY_SwitchWirelessBand8812(
		PADAPTER		Adapter,
		u8			Band
);

/*
 * BB TX Power R/W
 *   */
void	PHY_SetTxPowerLevel8812(PADAPTER	Adapter, u8	Channel);

u8 PHY_GetTxPowerIndex_8812A(
		PADAPTER			pAdapter,
		enum rf_path			RFPath,
		u8					Rate,
		u8					BandWidth,
		u8					Channel,
	struct txpwr_idx_comp *tic
);

u32 phy_get_tx_bb_swing_8812a(
		PADAPTER	Adapter,
		BAND_TYPE	Band,
		enum rf_path	RFPath
);

void
PHY_SetTxPowerIndex_8812A(
		PADAPTER		Adapter,
		u32				PowerIndex,
		enum rf_path		RFPath,
		u8				Rate
);

/*
 * channel switch related funciton
 *   */
void
PHY_SetSwChnlBWMode8812(
		PADAPTER			Adapter,
		u8					channel,
		enum channel_width	Bandwidth,
		u8					Offset40,
		u8					Offset80
);

/*
 * BB/MAC/RF other monitor API
 *   */

void
phy_set_rf_path_switch_8812a(
		struct dm_struct		*phydm,
		bool		bMain
);

/*--------------------------Exported Function prototype---------------------*/
#endif /* __INC_HAL8192CPHYCFG_H */

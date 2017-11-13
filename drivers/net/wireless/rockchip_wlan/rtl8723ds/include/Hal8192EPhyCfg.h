/******************************************************************************
 *
 * Copyright(c) 2012 - 2017 Realtek Corporation.
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
#ifndef __INC_HAL8192EPHYCFG_H__
#define __INC_HAL8192EPHYCFG_H__


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
u32	PHY_QueryBBReg8192E(IN	PADAPTER	Adapter,
			    IN	u32			RegAddr,
			    IN	u32			BitMask);
void	PHY_SetBBReg8192E(IN	PADAPTER		Adapter,
			  IN	u32			RegAddr,
			  IN	u32			BitMask,
			  IN	u32			Data);
u32	PHY_QueryRFReg8192E(IN	PADAPTER	Adapter,
			    IN	enum rf_path	eRFPath,
			    IN	u32			RegAddr,
			    IN	u32			BitMask);
void	PHY_SetRFReg8192E(IN	PADAPTER		Adapter,
			  IN	enum rf_path	eRFPath,
			  IN	u32			RegAddr,
			  IN	u32			BitMask,
			  IN	u32			Data);

/*
 * Initialization related function
 *
 * MAC/BB/RF HAL config */
int	PHY_MACConfig8192E(IN PADAPTER	Adapter);
int	PHY_BBConfig8192E(IN PADAPTER	Adapter);
int	PHY_RFConfig8192E(IN PADAPTER	Adapter);

/* RF config */


/*
 * BB TX Power R/W
 *   */
void	PHY_GetTxPowerLevel8192E(IN PADAPTER	Adapter, OUT s32	*powerlevel);
void	PHY_SetTxPowerLevel8192E(IN PADAPTER	Adapter, IN u8	channel);
BOOLEAN	PHY_UpdateTxPowerDbm8192E(IN PADAPTER	Adapter, IN int	powerInDbm);

VOID
PHY_SetTxPowerIndex_8192E(
	IN	PADAPTER			Adapter,
	IN	u32					PowerIndex,
	IN	enum rf_path			RFPath,
	IN	u8					Rate
);

u8
PHY_GetTxPowerIndex_8192E(
	IN	PADAPTER			pAdapter,
	IN	enum rf_path			RFPath,
	IN	u8					Rate,
	IN	u8					BandWidth,
	IN	u8					Channel,
	struct txpwr_idx_comp *tic
);

/*
 * channel switch related funciton
 *   */
VOID
PHY_SetSwChnlBWMode8192E(
	IN	PADAPTER			Adapter,
	IN	u8					channel,
	IN	enum channel_width	Bandwidth,
	IN	u8					Offset40,
	IN	u8					Offset80
);

VOID
PHY_SetRFEReg_8192E(
	IN PADAPTER		Adapter
);

void
phy_SpurCalibration_8192E(
	IN	PADAPTER			Adapter,
	IN	enum spur_cal_method	method
);
void PHY_SpurCalibration_8192E(IN PADAPTER Adapter);

#ifdef CONFIG_SPUR_CAL_NBI
void
phy_SpurCalibration_8192E_NBI(
	IN	PADAPTER			Adapter
);
#endif
/*
 * BB/MAC/RF other monitor API
 *   */

VOID
phy_set_rf_path_switch_8192e(
	IN	PADAPTER	pAdapter,
	IN	bool		bMain
);

/*--------------------------Exported Function prototype---------------------*/
#endif /* __INC_HAL8192CPHYCFG_H */

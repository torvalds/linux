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
#ifndef __INC_HAL8814PHYCFG_H__
#define __INC_HAL8814PHYCFG_H__


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

#define	SIC_ENABLE				0

/*------------------------------Define structure----------------------------*/


/*------------------------Export global variable----------------------------*/
/*------------------------Export global variable----------------------------*/


/*------------------------Export Marco Definition---------------------------*/
/*------------------------Export Marco Definition---------------------------*/


/*--------------------------Exported Function prototype---------------------*/
/* 1. BB register R/W API */

extern	u32
PHY_QueryBBReg8814A(IN	PADAPTER	Adapter,
		    IN	u32		RegAddr,
		    IN	u32		BitMask);


VOID
PHY_SetBBReg8814A(IN	PADAPTER	Adapter,
		  IN	u32		RegAddr,
		  IN	u32		BitMask,
		  IN	u32		Data);


extern	u32
PHY_QueryRFReg8814A(IN	PADAPTER			Adapter,
		    IN	enum rf_path	eRFPath,
		    IN	u32			RegAddr,
		    IN	u32			BitMask);


void
PHY_SetRFReg8814A(IN	PADAPTER			Adapter,
		  IN	enum rf_path		eRFPath,
		  IN	u32				RegAddr,
		  IN	u32				BitMask,
		  IN	u32				Data);

/* 1 3. Initial BB/RF config by reading MAC/BB/RF txt. */
s32
phy_BB8814A_Config_ParaFile(
	IN	PADAPTER	Adapter
);

VOID
PHY_ConfigBB_8814A(
	IN	PADAPTER	Adapter
);


VOID
phy_ADC_CLK_8814A(
	IN	PADAPTER	Adapter
);

s32
PHY_RFConfig8814A(
	IN	PADAPTER	Adapter
);

/*
 * RF Power setting
 *
 * BOOLEAN	PHY_SetRFPowerState8814A(PADAPTER Adapter, rt_rf_power_state	eRFPowerState); */

/* 1 5. Tx  Power setting API */

VOID
PHY_GetTxPowerLevel8814(
	IN	PADAPTER		Adapter,
	OUT ps4Byte			powerlevel
);

VOID
PHY_SetTxPowerLevel8814(
	IN	PADAPTER		Adapter,
	IN	u8			Channel
);

u8
phy_get_tx_power_index_8814a(
	IN	PADAPTER		Adapter,
	IN	enum rf_path		RFPath,
	IN	u8				Rate,
	IN	enum channel_width BandWidth,
	IN	u8				Channel
);

u8
PHY_GetTxPowerIndex8814A(
	IN	PADAPTER		Adapter,
	IN	enum rf_path		RFPath,
	IN	u8				Rate,
	IN	u8				BandWidth,
	IN	u8				Channel,
	struct txpwr_idx_comp *tic
);

VOID
PHY_SetTxPowerIndex_8814A(
	IN	PADAPTER		Adapter,
	IN	u32				PowerIndex,
	IN	enum rf_path		RFPath,
	IN	u8				Rate
);


BOOLEAN
PHY_UpdateTxPowerDbm8814A(
	IN	PADAPTER	Adapter,
	IN	s4Byte		powerInDbm
);


u32
PHY_GetTxBBSwing_8814A(
	IN	PADAPTER	Adapter,
	IN	BAND_TYPE	Band,
	IN	enum rf_path	RFPath
);



/* 1 6. Channel setting API */

VOID
PHY_SwChnlTimerCallback8814A(
	IN	struct timer_list		*p_timer
);

VOID
PHY_SwChnlWorkItemCallback8814A(
	IN PVOID            pContext
);


VOID
HAL_HandleSwChnl8814A(
	IN	PADAPTER	pAdapter,
	IN	u8		channel
);

VOID
PHY_SwChnlSynchronously8814A(IN	PADAPTER		pAdapter,
			     IN	u8			channel);

VOID
PHY_SwChnlAndSetBWModeCallback8814A(IN PVOID            pContext);


VOID
PHY_HandleSwChnlAndSetBW8814A(
	IN	PADAPTER			Adapter,
	IN	BOOLEAN				bSwitchChannel,
	IN	BOOLEAN				bSetBandWidth,
	IN	u8					ChannelNum,
	IN	enum channel_width	ChnlWidth,
	IN	u8					ChnlOffsetOf40MHz,
	IN	u8					ChnlOffsetOf80MHz,
	IN	u8					CenterFrequencyIndex1
);


BOOLEAN
PHY_QueryRFPathSwitch_8814A(IN	PADAPTER	pAdapter);



#if (USE_WORKITEM)
VOID
RtCheckForHangWorkItemCallback8814A(
	IN PVOID   pContext
);
#endif

BOOLEAN
SetAntennaConfig8814A(
	IN	PADAPTER	Adapter,
	IN	u8		DefaultAnt
);

VOID
PHY_SetRFEReg8814A(
	IN PADAPTER		Adapter,
	IN BOOLEAN		bInit,
	IN u8		Band
);


s32
PHY_SwitchWirelessBand8814A(
	IN PADAPTER		 Adapter,
	IN u8		Band
);

VOID
PHY_SetIO_8814A(
	PADAPTER		pAdapter
);

VOID
PHY_SetSwChnlBWMode8814(
	IN	PADAPTER			Adapter,
	IN	u8					channel,
	IN	enum channel_width	Bandwidth,
	IN	u8					Offset40,
	IN	u8					Offset80
);

s32 PHY_MACConfig8814(PADAPTER Adapter);
int PHY_BBConfig8814(PADAPTER	Adapter);
VOID PHY_Set_SecCCATH_by_RXANT_8814A(PADAPTER	pAdapter, u4Byte ulAntennaRx);



/*--------------------------Exported Function prototype---------------------*/

/*--------------------------Exported Function prototype---------------------*/
#endif /* __INC_HAL8192CPHYCFG_H */

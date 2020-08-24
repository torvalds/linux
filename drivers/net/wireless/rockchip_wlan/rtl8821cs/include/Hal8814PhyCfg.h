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
PHY_QueryBBReg8814A(PADAPTER	Adapter,
			u32		RegAddr,
			u32		BitMask);


void
PHY_SetBBReg8814A(PADAPTER	Adapter,
			u32		RegAddr,
			u32		BitMask,
			u32		Data);


extern	u32
PHY_QueryRFReg8814A(PADAPTER			Adapter,
			enum rf_path	eRFPath,
			u32			RegAddr,
			u32			BitMask);


void
PHY_SetRFReg8814A(PADAPTER			Adapter,
			enum rf_path		eRFPath,
			u32				RegAddr,
			u32				BitMask,
			u32				Data);

/* 1 3. Initial BB/RF config by reading MAC/BB/RF txt. */
s32
phy_BB8814A_Config_ParaFile(
		PADAPTER	Adapter
);

void
PHY_ConfigBB_8814A(
		PADAPTER	Adapter
);


void
phy_ADC_CLK_8814A(
		PADAPTER	Adapter
);

s32
PHY_RFConfig8814A(
		PADAPTER	Adapter
);

/*
 * RF Power setting
 *
 * BOOLEAN	PHY_SetRFPowerState8814A(PADAPTER Adapter, rt_rf_power_state	eRFPowerState); */

/* 1 5. Tx  Power setting API */

void
PHY_SetTxPowerLevel8814(
		PADAPTER		Adapter,
		u8			Channel
);

u8
phy_get_tx_power_index_8814a(
		PADAPTER		Adapter,
		enum rf_path		RFPath,
		u8				Rate,
		enum channel_width BandWidth,
		u8				Channel
);

void
PHY_SetTxPowerIndex_8814A(
		PADAPTER		Adapter,
		u32				PowerIndex,
		enum rf_path		RFPath,
		u8				Rate
);

u32
PHY_GetTxBBSwing_8814A(
		PADAPTER	Adapter,
		BAND_TYPE	Band,
		enum rf_path	RFPath
);



/* 1 6. Channel setting API */
#if 0
void
PHY_SwChnlTimerCallback8814A(
		struct timer_list		*p_timer
);
#endif
void
PHY_SwChnlWorkItemCallback8814A(
		void *pContext
);


void
HAL_HandleSwChnl8814A(
		PADAPTER	pAdapter,
		u8		channel
);

void
PHY_SwChnlSynchronously8814A(PADAPTER		pAdapter,
				u8			channel);

void
PHY_HandleSwChnlAndSetBW8814A(
		PADAPTER			Adapter,
		BOOLEAN				bSwitchChannel,
		BOOLEAN				bSetBandWidth,
		u8					ChannelNum,
		enum channel_width	ChnlWidth,
		u8					ChnlOffsetOf40MHz,
		u8					ChnlOffsetOf80MHz,
		u8					CenterFrequencyIndex1
);


BOOLEAN
PHY_QueryRFPathSwitch_8814A(PADAPTER	pAdapter);



#if (USE_WORKITEM)
void
RtCheckForHangWorkItemCallback8814A(
		void *pContext
);
#endif

BOOLEAN
SetAntennaConfig8814A(
		PADAPTER	Adapter,
		u8		DefaultAnt
);

void
PHY_SetRFEReg8814A(
		PADAPTER		Adapter,
		BOOLEAN		bInit,
		u8		Band
);


s32
PHY_SwitchWirelessBand8814A(
		PADAPTER		 Adapter,
		u8		Band
);

void
PHY_SetIO_8814A(
	PADAPTER		pAdapter
);

void
PHY_SetSwChnlBWMode8814(
		PADAPTER			Adapter,
		u8					channel,
		enum channel_width	Bandwidth,
		u8					Offset40,
		u8					Offset80
);

s32 PHY_MACConfig8814(PADAPTER Adapter);
int PHY_BBConfig8814(PADAPTER	Adapter);
void PHY_Set_SecCCATH_by_RXANT_8814A(PADAPTER	pAdapter, u32 ulAntennaRx);



/*--------------------------Exported Function prototype---------------------*/

/*--------------------------Exported Function prototype---------------------*/
#endif /* __INC_HAL8192CPHYCFG_H */

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
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 *
 ******************************************************************************/
#ifndef __INC_HAL8723BPHYCFG_H__
#define __INC_HAL8723BPHYCFG_H__

/*--------------------------Define Parameters-------------------------------*/
#define LOOP_LIMIT				5
#define MAX_STALL_TIME			50		//us
#define AntennaDiversityValue	0x80	//(Adapter->bSoftwareAntennaDiversity ? 0x00:0x80)
#define MAX_TXPWR_IDX_NMODE_92S	63
#define Reset_Cnt_Limit			3

#ifdef CONFIG_PCI_HCI
#define MAX_AGGR_NUM	0x0B
#else
#define MAX_AGGR_NUM	0x07
#endif // CONFIG_PCI_HCI


/*--------------------------Define Parameters End-------------------------------*/


/*------------------------------Define structure----------------------------*/

/*------------------------------Define structure End----------------------------*/

/*--------------------------Exported Function prototype---------------------*/
u32
PHY_QueryBBReg_8723B(
	IN	PADAPTER	Adapter,
	IN	u32		RegAddr,
	IN	u32		BitMask
	);

VOID
PHY_SetBBReg_8723B(
	IN	PADAPTER	Adapter,
	IN	u32		RegAddr,
	IN	u32		BitMask,
	IN	u32		Data
	);

u32
PHY_QueryRFReg_8723B(
	IN	PADAPTER			Adapter,
	IN	u8				eRFPath,
	IN	u32				RegAddr,
	IN	u32				BitMask
	);

VOID
PHY_SetRFReg_8723B(
	IN	PADAPTER			Adapter,
	IN	u8				eRFPath,
	IN	u32				RegAddr,
	IN	u32				BitMask,
	IN	u32				Data
	);

/* MAC/BB/RF HAL config */
int PHY_BBConfig8723B(PADAPTER	Adapter	);

int PHY_RFConfig8723B(PADAPTER	Adapter	);

s32 PHY_MACConfig8723B(PADAPTER padapter);

int
PHY_ConfigRFWithParaFile_8723B(
	IN	PADAPTER			Adapter,
	IN	u8* 				pFileName,
	RF_PATH				eRFPath
);
int
PHY_ConfigRFWithHeaderFile_8723B(
	IN	PADAPTER			Adapter,
	RF_PATH				eRFPath
);

int
PHY_ConfigRFWithTxPwrTrackParaFile(
	IN	PADAPTER			Adapter,
	IN	s8 			* pFileName
);

VOID
storePwrIndexDiffRateOffset(
	IN	PADAPTER	Adapter,
	IN	u32		RegAddr,
	IN	u32		BitMask,
	IN	u32		Data
	);

void PHY_SetTxPowerLevel8723B(PADAPTER	 Adapter, u8 channel);

VOID
PHY_SetTxPowerLevel8723B(
	IN	PADAPTER		Adapter,
	IN	u8			channel
	);

VOID
PHY_SetBWMode8723B(
	IN	PADAPTER				Adapter,
	IN	CHANNEL_WIDTH			Bandwidth,	// 20M or 40M
	IN	unsigned char				Offset		// Upper, Lower, or Don't care
);

VOID
PHY_SwChnl8723B(	// Call after initialization
	IN	PADAPTER	Adapter,
	IN	u8		channel
	);

VOID
PHY_SetSwChnlBWMode8723B(
	IN	PADAPTER			Adapter,
	IN	u8					channel,
	IN	CHANNEL_WIDTH		Bandwidth,
	IN	u8					Offset40,
	IN	u8					Offset80
);
/*--------------------------Exported Function prototype End---------------------*/

#endif


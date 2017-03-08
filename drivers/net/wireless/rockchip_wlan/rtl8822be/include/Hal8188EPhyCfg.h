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
#ifndef __INC_HAL8188EPHYCFG_H__
#define __INC_HAL8188EPHYCFG_H__


/*--------------------------Define Parameters-------------------------------*/
#define LOOP_LIMIT				5
#define MAX_STALL_TIME			50		/* us */
#define AntennaDiversityValue		0x80	/* (Adapter->bSoftwareAntennaDiversity ? 0x00 : 0x80) */
#define MAX_TXPWR_IDX_NMODE_92S	63
#define Reset_Cnt_Limit			3

#ifdef CONFIG_PCI_HCI
	#define MAX_AGGR_NUM	0x0B
#else
	#define MAX_AGGR_NUM	0x07
#endif /* CONFIG_PCI_HCI */


/*--------------------------Define Parameters-------------------------------*/


/*------------------------------Define structure----------------------------*/

#define	MAX_TX_COUNT_8188E			1

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
u32	PHY_QueryBBReg8188E(IN	PADAPTER	Adapter,
			    IN	u32		RegAddr,
			    IN	u32		BitMask);
void	PHY_SetBBReg8188E(IN	PADAPTER	Adapter,
			  IN	u32		RegAddr,
			  IN	u32		BitMask,
			  IN	u32		Data);
u32	PHY_QueryRFReg8188E(IN	PADAPTER	Adapter,
			    IN	u8				eRFPath,
			    IN	u32				RegAddr,
			    IN	u32				BitMask);
void	PHY_SetRFReg8188E(IN	PADAPTER		Adapter,
			  IN	u8				eRFPath,
			  IN	u32				RegAddr,
			  IN	u32				BitMask,
			  IN	u32				Data);

/*
 * Initialization related function
 */
/* MAC/BB/RF HAL config */
int	PHY_MACConfig8188E(IN	PADAPTER	Adapter);
int	PHY_BBConfig8188E(IN	PADAPTER	Adapter);
int	PHY_RFConfig8188E(IN	PADAPTER	Adapter);

/* RF config */
int	rtl8188e_PHY_ConfigRFWithParaFile(IN PADAPTER Adapter, IN u8 *pFileName, u8 eRFPath);

/*
 * RF Power setting
 */
/* extern	BOOLEAN	PHY_SetRFPowerState(IN	PADAPTER			Adapter,
 *									IN	RT_RF_POWER_STATE	eRFPowerState); */

/*
 * BB TX Power R/W
 *   */
void	PHY_GetTxPowerLevel8188E(IN	PADAPTER		Adapter,
				 OUT s32		*powerlevel);
void	PHY_SetTxPowerLevel8188E(IN	PADAPTER		Adapter,
				 IN	u8			channel);
BOOLEAN	PHY_UpdateTxPowerDbm8188E(IN	PADAPTER	Adapter,
				  IN	int		powerInDbm);

VOID
PHY_SetTxPowerIndex_8188E(
	IN	PADAPTER			Adapter,
	IN	u32					PowerIndex,
	IN	u8					RFPath,
	IN	u8					Rate
);

u8
PHY_GetTxPowerIndex_8188E(
	IN	PADAPTER		pAdapter,
	IN	u8				RFPath,
	IN	u8				Rate,
	IN	CHANNEL_WIDTH	BandWidth,
	IN	u8				Channel
);

/*
 * Switch bandwidth for 8192S
 */
/* extern	void	PHY_SetBWModeCallback8192C(	IN	PRT_TIMER		pTimer	); */
void	PHY_SetBWMode8188E(IN	PADAPTER			pAdapter,
			   IN	CHANNEL_WIDTH	ChnlWidth,
			   IN	unsigned char	Offset);

/*
 * Set FW CMD IO for 8192S.
 */
/* extern	BOOLEAN HalSetIO8192C(	IN	PADAPTER			Adapter,
 *									IN	IO_TYPE				IOType); */

/*
 * Set A2 entry to fw for 8192S
 *   */
extern	void FillA2Entry8192C(IN	PADAPTER			Adapter,
			      IN	u8				index,
			      IN	u8				*val);


/*
 * channel switch related funciton
 */
/* extern	void	PHY_SwChnlCallback8192C(	IN	PRT_TIMER		pTimer	); */
void	PHY_SwChnl8188E(IN	PADAPTER		pAdapter,
			IN	u8			channel);

VOID
PHY_SetSwChnlBWMode8188E(
	IN	PADAPTER			Adapter,
	IN	u8					channel,
	IN	CHANNEL_WIDTH	Bandwidth,
	IN	u8					Offset40,
	IN	u8					Offset80
);

VOID
PHY_SetRFEReg_8188E(
	IN PADAPTER		Adapter
);
/*
 * BB/MAC/RF other monitor API
 *   */
VOID PHY_SetRFPathSwitch_8188E(IN	PADAPTER	pAdapter, IN	BOOLEAN		bMain);

extern	VOID
PHY_SwitchEphyParameter(
	IN	PADAPTER			Adapter
);

extern	VOID
PHY_EnableHostClkReq(
	IN	PADAPTER			Adapter
);

BOOLEAN
SetAntennaConfig92C(
	IN	PADAPTER	Adapter,
	IN	u8		DefaultAnt
);

VOID
storePwrIndexDiffRateOffset(
	IN	PADAPTER	Adapter,
	IN	u32		RegAddr,
	IN	u32		BitMask,
	IN	u32		Data
);
/*--------------------------Exported Function prototype---------------------*/

/*
 * Initialization related function
 *
 * MAC/BB/RF HAL config */
/* extern s32 PHY_MACConfig8723(PADAPTER padapter);
 * s32 PHY_BBConfig8723(PADAPTER padapter);
 * s32 PHY_RFConfig8723(PADAPTER padapter); */



/* ******************************************************************
 * Note: If SIC_ENABLE under PCIE, because of the slow operation
 *	you should
 * 	2) "#define RTL8723_FPGA_VERIFICATION	1"				in Precomp.h.WlanE.Windows
 * 	3) "#define RTL8190_Download_Firmware_From_Header	0"	in Precomp.h.WlanE.Windows if needed.
 *   */
#if (RTL8188E_SUPPORT == 1) && (RTL8188E_FPGA_TRUE_PHY_VERIFICATION == 1)
	#define	SIC_ENABLE				1
	#define	SIC_HW_SUPPORT		1
#else
	#define	SIC_ENABLE				0
	#define	SIC_HW_SUPPORT		0
#endif
/* ****************************************************************** */


#define	SIC_MAX_POLL_CNT		5

#if (SIC_HW_SUPPORT == 1)
	#define	SIC_CMD_READY			0
	#define	SIC_CMD_PREWRITE		0x1
	#if (RTL8188E_SUPPORT == 1)
		#define	SIC_CMD_WRITE			0x40
		#define	SIC_CMD_PREREAD		0x2
		#define	SIC_CMD_READ			0x80
		#define	SIC_CMD_INIT			0xf0
		#define	SIC_INIT_VAL			0xff

		#define	SIC_INIT_REG			0x1b7
		#define	SIC_CMD_REG			0x1EB		/* 1byte */
		#define	SIC_ADDR_REG			0x1E8		/* 1b4~1b5, 2 bytes */
		#define	SIC_DATA_REG			0x1EC		/* 1b0~1b3 */
	#else
		#define	SIC_CMD_WRITE			0x11
		#define	SIC_CMD_PREREAD		0x2
		#define	SIC_CMD_READ			0x12
		#define	SIC_CMD_INIT			0x1f
		#define	SIC_INIT_VAL			0xff

		#define	SIC_INIT_REG			0x1b7
		#define	SIC_CMD_REG			0x1b6		/* 1byte */
		#define	SIC_ADDR_REG			0x1b4		/* 1b4~1b5, 2 bytes */
		#define	SIC_DATA_REG			0x1b0		/* 1b0~1b3 */
	#endif
#else
	#define	SIC_CMD_READY			0
	#define	SIC_CMD_WRITE			1
	#define	SIC_CMD_READ			2

	#if (RTL8188E_SUPPORT == 1)
		#define	SIC_CMD_REG			0x1EB		/* 1byte */
		#define	SIC_ADDR_REG			0x1E8		/* 1b9~1ba, 2 bytes */
		#define	SIC_DATA_REG			0x1EC		/* 1bc~1bf */
	#else
		#define	SIC_CMD_REG			0x1b8		/* 1byte */
		#define	SIC_ADDR_REG			0x1b9		/* 1b9~1ba, 2 bytes */
		#define	SIC_DATA_REG			0x1bc		/* 1bc~1bf */
	#endif
#endif

#if (SIC_ENABLE == 1)
	VOID SIC_Init(IN PADAPTER Adapter);
#endif


#endif /* __INC_HAL8192CPHYCFG_H */

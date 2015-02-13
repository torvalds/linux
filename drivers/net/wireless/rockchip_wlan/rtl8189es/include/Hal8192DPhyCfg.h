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
/*****************************************************************************
 *
 * Module:	__INC_HAL8192DPHYCFG_H
 *
 *
 * Note:	
 *			
 *
 * Export:	Constants, macro, functions(API), global variables(None).
 *
 * Abbrev:	
 *
 * History:
 *		Data		Who		Remark 
 *      08/07/2007  MHC    	1. Porting from 9x series PHYCFG.h.
 *							2. Reorganize code architecture.
 * 
 *****************************************************************************/
 /* Check to see if the file has been included already.  */
#ifndef __INC_HAL8192DPHYCFG_H
#define __INC_HAL8192DPHYCFG_H


/*--------------------------Define Parameters-------------------------------*/
#define LOOP_LIMIT				5
#define MAX_STALL_TIME			50		//us
#define AntennaDiversityValue	0x80	//(Adapter->bSoftwareAntennaDiversity ? 0x00:0x80)
#define MAX_TXPWR_IDX_NMODE_92S	63
#define Reset_Cnt_Limit			3


#ifdef CONFIG_PCI_HCI
#define	SET_RTL8192SE_RF_SLEEP(_pAdapter)							\
{																	\
	u1Byte		u1bTmp;												\
	u1bTmp = PlatformEFIORead1Byte(_pAdapter, REG_LDOV12D_CTRL);		\
	u1bTmp |= BIT0;													\
	PlatformEFIOWrite1Byte(_pAdapter, REG_LDOV12D_CTRL, u1bTmp);		\
	PlatformEFIOWrite1Byte(_pAdapter, REG_SPS_OCP_CFG, 0x0);				\
	PlatformEFIOWrite1Byte(_pAdapter, TXPAUSE, 0xFF);				\
	PlatformEFIOWrite2Byte(_pAdapter, CMDR, 0x57FC);				\
	delay_us(100);													\
	PlatformEFIOWrite2Byte(_pAdapter, CMDR, 0x77FC);				\
	PlatformEFIOWrite1Byte(_pAdapter, PHY_CCA, 0x0);				\
	delay_us(10);													\
	PlatformEFIOWrite2Byte(_pAdapter, CMDR, 0x37FC);				\
	delay_us(10);													\
	PlatformEFIOWrite2Byte(_pAdapter, CMDR, 0x77FC);				\
	delay_us(10);													\
	PlatformEFIOWrite2Byte(_pAdapter, CMDR, 0x57FC);				\
}
#endif


/*--------------------------Define Parameters-------------------------------*/


/*------------------------------Define structure----------------------------*/ 

#define	CHANNEL_GROUP_MAX_2G		3
#define	CHANNEL_GROUP_IDX_5GL		3
#define	CHANNEL_GROUP_IDX_5GM		6
#define	CHANNEL_GROUP_IDX_5GH		9
#define	CHANNEL_GROUP_MAX_5G		9
#define	CHANNEL_MAX_NUMBER_2G		14

typedef enum _MACPHY_MODE_CHANGE_ACTION{
	DMDP2DMSP = 0,
	DMSP2DMDP = 1,
	DMDP2SMSP = 2,
	SMSP2DMDP = 3,
	DMSP2SMSP = 4,
	SMSP2DMSP = 5,
	MAXACTION
}MACPHY_MODE_CHANGE_ACTION,*PMACPHY_MODE_CHANGE_ACTION;



/* BB/RF related */

/*------------------------------Define structure----------------------------*/ 


/*------------------------Export global variable----------------------------*/
/*------------------------Export global variable----------------------------*/


/*------------------------Export Marco Definition---------------------------*/

/*--------------------------Exported Function prototype---------------------*/
//
// BB and RF register read/write
//
void	PHY_SetBBReg1Byte8192D(	IN	PADAPTER	Adapter,
								IN	u32		RegAddr,
								IN	u32		BitMask,
								IN	u32		Data	);
u32	PHY_QueryBBReg8192D(	IN	PADAPTER	Adapter,
								IN	u32		RegAddr,
								IN	u32		BitMask	);
void	PHY_SetBBReg8192D(	IN	PADAPTER	Adapter,
								IN	u32		RegAddr,
								IN	u32		BitMask,
								IN	u32		Data	);
u32	PHY_QueryRFReg8192D(	IN	PADAPTER	Adapter,
								IN	u8				eRFPath,
								IN	u32				RegAddr,
								IN	u32				BitMask	);
void	PHY_SetRFReg8192D(	IN	PADAPTER		Adapter,
								IN	u8				eRFPath,
								IN	u32				RegAddr,
								IN	u32				BitMask,
								IN	u32				Data	);

//
// Initialization related function
//
/* MAC/BB/RF HAL config */
extern	int	PHY_MACConfig8192D(	IN	PADAPTER	Adapter	);
extern	int	PHY_BBConfig8192D(	IN	PADAPTER	Adapter	);
extern	int	PHY_RFConfig8192D(	IN	PADAPTER	Adapter	);
/* RF config */
int	rtl8192d_PHY_ConfigRFWithParaFile(	IN	PADAPTER	Adapter,
												IN	u8* 			pFileName,
												IN	u8			eRFPath);
int	rtl8192d_PHY_ConfigRFWithHeaderFile(	IN	PADAPTER		Adapter,
												IN	RF_CONTENT		Content,
												IN	u8				eRFPath);
/* BB/RF readback check for making sure init OK */
int	rtl8192d_PHY_CheckBBAndRFOK(	IN	PADAPTER		Adapter,
											IN	HW_BLOCK_E		CheckBlock,
											IN	u8				eRFPath	  );
/* Read initi reg value for tx power setting. */
void	rtl8192d_PHY_GetHWRegOriginalValue(	IN	PADAPTER		Adapter	);

//
// RF Power setting
//
//extern	BOOLEAN	PHY_SetRFPowerState(IN	PADAPTER			Adapter, 
//									IN	RT_RF_POWER_STATE	eRFPowerState);

//
// BB TX Power R/W
//
void	PHY_GetTxPowerLevel8192D(	IN	PADAPTER		Adapter,
											OUT s32*    		powerlevel	);
void	PHY_SetTxPowerLevel8192D(	IN	PADAPTER		Adapter,
											IN	u8			channel	);
BOOLEAN	PHY_UpdateTxPowerDbm8192D(	IN	PADAPTER	Adapter,
											IN	int		powerInDbm	);

//
VOID 
PHY_ScanOperationBackup8192D(IN	PADAPTER	Adapter,
										IN	u8		Operation	);

//
// Switch bandwidth for 8192S
//
//void	PHY_SetBWModeCallback8192C(	IN	PRT_TIMER		pTimer	);
void	PHY_SetBWMode8192D(	IN	PADAPTER			pAdapter,
									IN	CHANNEL_WIDTH	ChnlWidth,
									IN	unsigned char	Offset	);

//
// Set FW CMD IO for 8192S.
//
//extern	BOOLEAN HalSetIO8192C(	IN	PADAPTER			Adapter,
//									IN	IO_TYPE				IOType);

//
// Set A2 entry to fw for 8192S
//
extern	void FillA2Entry8192C(		IN	PADAPTER			Adapter,
										IN	u8				index,
										IN	u8*				val);


//
// channel switch related funciton
//
//extern	void	PHY_SwChnlCallback8192C(	IN	PRT_TIMER		pTimer	);
void	PHY_SwChnl8192D(	IN	PADAPTER		pAdapter,
									IN	u8			channel	);

VOID
PHY_SetSwChnlBWMode8192D(
	IN	PADAPTER			Adapter,
	IN	u8					channel,
	IN	CHANNEL_WIDTH	Bandwidth,
	IN	u8					Offset40,
	IN	u8					Offset80
);
	
//
// BB/MAC/RF other monitor API
//
void	PHY_SetMonitorMode8192D(IN	PADAPTER	pAdapter,
										IN	BOOLEAN		bEnableMonitorMode	);

BOOLEAN	PHY_CheckIsLegalRfPath8192D(IN	PADAPTER	pAdapter,
											IN	u32		eRFPath	);


//
// Modify the value of the hw register when beacon interval be changed.
//
void	
rtl8192d_PHY_SetBeaconHwReg(	IN	PADAPTER		Adapter,
					IN	u16			BeaconInterval	);


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
PHY_UpdateBBRFConfiguration8192D(
	IN PADAPTER Adapter,
	IN BOOLEAN bisBandSwitch
);

VOID PHY_ReadMacPhyMode92D(
	IN PADAPTER	Adapter,
	IN BOOLEAN 	AutoloadFail	
);

VOID PHY_ConfigMacPhyMode92D(
	IN PADAPTER	Adapter
);

VOID PHY_ConfigMacPhyModeInfo92D(
	IN PADAPTER	Adapter
);

VOID PHY_ConfigMacCoexist_RFPage92D(
	IN PADAPTER	Adapter
);

VOID
rtl8192d_PHY_InitRxSetting(
	IN	PADAPTER Adapter
);


VOID 
rtl8192d_PHY_SetRFPathSwitch(IN	PADAPTER	pAdapter, IN	BOOLEAN		bMain);

VOID
HalChangeCCKStatus8192D(
	IN	PADAPTER	Adapter,
	IN	BOOLEAN		bCCKDisable
);

VOID 
PHY_InitPABias92D(IN	PADAPTER Adapter);

/*--------------------------Exported Function prototype---------------------*/

#define PHY_SetBBReg1Byte(Adapter, RegAddr, BitMask, Data) PHY_SetBBReg1Byte8192D((Adapter), (RegAddr), (BitMask), (Data))
#endif	// __INC_HAL8192SPHYCFG_H


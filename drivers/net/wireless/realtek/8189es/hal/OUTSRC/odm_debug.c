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

//============================================================
// include files
//============================================================

#include "odm_precomp.h"


VOID 
ODM_InitDebugSetting(
	IN		PDM_ODM_T		pDM_Odm
	)
{
pDM_Odm->DebugLevel				= 	ODM_DBG_LOUD;

pDM_Odm->DebugComponents			= 
\
#if DBG
//BB Functions
//									ODM_COMP_DIG					|
//									ODM_COMP_RA_MASK				|
//									ODM_COMP_DYNAMIC_TXPWR		|
//									ODM_COMP_FA_CNT				|
//									ODM_COMP_RSSI_MONITOR			|
//									ODM_COMP_CCK_PD				|
//									ODM_COMP_ANT_DIV				|
//									ODM_COMP_PWR_SAVE				|
//									ODM_COMP_PWR_TRAIN			|
//									ODM_COMP_RATE_ADAPTIVE		|
//									ODM_COMP_PATH_DIV				|
//									ODM_COMP_DYNAMIC_PRICCA		|
//									ODM_COMP_RXHP					|
//									ODM_COMP_MP 					|
//									ODM_COMP_DYNAMIC_ATC		|

//MAC Functions
//									ODM_COMP_EDCA_TURBO			|
//									ODM_COMP_EARLY_MODE			|
//RF Functions
//									ODM_COMP_TX_PWR_TRACK		|
//									ODM_COMP_RX_GAIN_TRACK		|
//									ODM_COMP_CALIBRATION			|
//Common
//									ODM_COMP_COMMON				|
//									ODM_COMP_INIT					|
//									ODM_COMP_PSD					|
#endif
									0;
}

#if 0
/*------------------Declare variable-----------------------
// Define debug flag array for common debug print macro. */
u4Byte ODM_DBGP_Type[ODM_DBGP_TYPE_MAX];

/* Define debug print header for every service module. */
ODM_DBGP_HEAD_T	ODM_DBGP_Head;


/*-----------------------------------------------------------------------------
 * Function:    DBGP_Flag_Init
 *
 * Overview:    Refresh all debug print control flag content to zero.
 *
 * Input:       NONE
 *
 * Output:      NONE
 *
 * Return:      NONE
 *
 * Revised History:
 *  When		Who		Remark
 *  10/20/2006	MHC		Create Version 0.
 *
 *---------------------------------------------------------------------------*/
extern	void	ODM_DBGP_Flag_Init(void)
{    
    u1Byte i;
    
	for (i = 0; i < ODM_DBGP_TYPE_MAX; i++)
	{
 		ODM_DBGP_Type[i] = 0;
	}
    
#ifndef ADSL_AP_BUILD_WORKAROUND
#if DBG		
	// 2010/06/02 MH Free build driver can not out any debug message!!!
	// Init Debug flag enable condition

	ODM_DBGP_Type[FINIT]			=	\
//								INIT_EEPROM						|
//								INIT_TxPower						|
//								INIT_IQK							|
//								INIT_RF							|
								0;

	ODM_DBGP_Type[FDM]			=	\
//								WA_IOT							|
//								DM_PWDB						|
//								DM_Monitor						|
//								DM_DIG				|
//								DM_EDCA_Turbo		|
//								DM_BT30				|
								0;

	ODM_DBGP_Type[FIOCTL]		= 	\
//								IOCTL_IRP						|
//								IOCTL_IRP_DETAIL					|
//								IOCTL_IRP_STATISTICS				|
//								IOCTL_IRP_HANDLE				|
//								IOCTL_BT_HCICMD					|
//								IOCTL_BT_HCICMD_DETAIL			|
//								IOCTL_BT_HCICMD_EXT				|
//								IOCTL_BT_EVENT					|
//								IOCTL_BT_EVENT_DETAIL			|
//								IOCTL_BT_EVENT_PERIODICAL		|
//								IOCTL_BT_TX_ACLDATA			|
//								IOCTL_BT_TX_ACLDATA_DETAIL		|
//								IOCTL_BT_RX_ACLDATA				|
//								IOCTL_BT_RX_ACLDATA_DETAIL		|
//								IOCTL_BT_TP						|
//								IOCTL_STATE						|
//								IOCTL_BT_LOGO					|
//								IOCTL_CALLBACK_FUN				|
//								IOCTL_PARSE_BT_PKT				|
								0;

	ODM_DBGP_Type[FBT]			= 	\
//								BT_TRACE						|
								0;

	ODM_DBGP_Type[FEEPROM]		=	\
//								EEPROM_W						|
//								EFUSE_PG						|
//								EFUSE_READ_ALL					|
//								EFUSE_ANALYSIS					|
//								EFUSE_PG_DETAIL					|
								0;

	ODM_DBGP_Type[FDBG_CTRL]	= 	\
//								DBG_CTRL_TRACE					|
//								DBG_CTRL_INBAND_NOISE			|
								0;
	
	// 2011/07/20 MH Add for short cut 
	ODM_DBGP_Type[FSHORT_CUT] = 	\
//								SHCUT_TX 						| 
//								SHCUT_RX						|
								0;
	
#endif	
#endif
	/* Define debug header of every service module. */
	//ODM_DBGP_Head.pMANS	= "\n\r[MANS] ";
	//ODM_DBGP_Head.pRTOS	= "\n\r[RTOS] ";
	//ODM_DBGP_Head.pALM	= "\n\r[ALM]  ";
	//ODM_DBGP_Head.pPEM	= "\n\r[PEM]  ";
	//ODM_DBGP_Head.pCMPK	= "\n\r[CMPK] ";
	//ODM_DBGP_Head.pRAPD	= "\n\r[RAPD] ";
	//ODM_DBGP_Head.pTXPB	= "\n\r[TXPB] ";
	//ODM_DBGP_Head.pQUMG	= "\n\r[QUMG] ";
    	
}	/* DBGP_Flag_Init */

#endif


#if 0
u4Byte GlobalDebugLevel			= 	DBG_LOUD;
//
// 2009/06/22 MH Allow Fre build to print none debug info at init time.
//
#if DBG
u8Byte GlobalDebugComponents	= 	\
//									COMP_TRACE				|
//									COMP_DBG				|
//									COMP_INIT				|
//									COMP_OID_QUERY			|
//									COMP_OID_SET			|
//									COMP_RECV				|
//									COMP_SEND				|
//									COMP_IO					|
//									COMP_POWER				|
//									COMP_MLME				|
//									COMP_SCAN				|
//									COMP_SYSTEM			|
//									COMP_SEC				|
//									COMP_AP				|
//									COMP_TURBO				|
//									COMP_QOS				|
//									COMP_AUTHENTICATOR	|
//									COMP_BEACON			|
//									COMP_ANTENNA			|
//									COMP_RATE				|
//									COMP_EVENTS			|
//									COMP_FPGA				|
//									COMP_RM				|
//									COMP_MP				|
//									COMP_RXDESC			|
//									COMP_CKIP				|
//									COMP_DIG				|
//									COMP_TXAGC				|
//									COMP_HIPWR				|
//									COMP_HALDM				|
//									COMP_RSNA				|
//									COMP_INDIC				|
//									COMP_LED				|
//									COMP_RF					|
//									COMP_DUALMACSWITCH	|
//									COMP_EASY_CONCURRENT	|

//1!!!!!!!!!!!!!!!!!!!!!!!!!!!
//1//1Attention Please!!!<11n or 8190 specific code should be put below this line>
//1!!!!!!!!!!!!!!!!!!!!!!!!!!!

//									COMP_HT				|
//									COMP_POWER_TRACKING 	|
//									COMP_RX_REORDER		|
//									COMP_AMSDU 			|
//									COMP_WPS				|
//									COMP_RATR				|
//									COMP_RESET 				|
//									COMP_CMD				|
//									COMP_EFUSE				|
//									COMP_MESH_INTERWORKING |
//									COMP_CCX				|	
//									COMP_IOCTL				|
//									COMP_GP 				|
//									COMP_TXAGG				|
//									COMP_BB_POWERSAVING	|
//									COMP_SWAS				|
//									COMP_P2P				|
//									COMP_MUX				|
//									COMP_FUNC				|
//									COMP_TDLS				|
//									COMP_OMNIPEEK			|
//									COMP_PSD				|
									0;


#else
u8Byte GlobalDebugComponents	= 0;
#endif

#if (RT_PLATFORM==PLATFORM_LINUX) 
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0))
EXPORT_SYMBOL(GlobalDebugComponents);
EXPORT_SYMBOL(GlobalDebugLevel);
#endif
#endif

/*------------------Declare variable-----------------------
// Define debug flag array for common debug print macro. */
u4Byte			DBGP_Type[DBGP_TYPE_MAX];

/* Define debug print header for every service module. */
DBGP_HEAD_T		DBGP_Head;


/*-----------------------------------------------------------------------------
 * Function:    DBGP_Flag_Init
 *
 * Overview:    Refresh all debug print control flag content to zero.
 *
 * Input:       NONE
 *
 * Output:      NONE
 *
 * Return:      NONE
 *
 * Revised History:
 *  When		Who		Remark
 *  10/20/2006	MHC		Create Version 0.
 *
 *---------------------------------------------------------------------------*/
extern	void	DBGP_Flag_Init(void)
{    
    u1Byte	i;
    
	for (i = 0; i < DBGP_TYPE_MAX; i++)
	{
 		DBGP_Type[i] = 0;
	}
    
#if DBG		
	// 2010/06/02 MH Free build driver can not out any debug message!!!
	// Init Debug flag enable condition

	DBGP_Type[FINIT]			=	\
//								INIT_EEPROM						|
//								INIT_TxPower						|
//								INIT_IQK							|
//								INIT_RF							|
								0;

	DBGP_Type[FDM]			=	\
//								WA_IOT							|
//								DM_PWDB						|
//								DM_Monitor						|
//								DM_DIG				|
//								DM_EDCA_Turbo		|
//								DM_BT30				|
								0;

	DBGP_Type[FIOCTL]		= 	\
//								IOCTL_IRP						|
//								IOCTL_IRP_DETAIL					|
//								IOCTL_IRP_STATISTICS				|
//								IOCTL_IRP_HANDLE				|
//								IOCTL_BT_HCICMD					|
//								IOCTL_BT_HCICMD_DETAIL			|
//								IOCTL_BT_HCICMD_EXT				|
//								IOCTL_BT_EVENT					|
//								IOCTL_BT_EVENT_DETAIL			|
//								IOCTL_BT_EVENT_PERIODICAL		|
//								IOCTL_BT_TX_ACLDATA			|
//								IOCTL_BT_TX_ACLDATA_DETAIL		|
//								IOCTL_BT_RX_ACLDATA				|
//								IOCTL_BT_RX_ACLDATA_DETAIL		|
//								IOCTL_BT_TP						|
//								IOCTL_STATE						|
//								IOCTL_BT_LOGO					|
//								IOCTL_CALLBACK_FUN				|
//								IOCTL_PARSE_BT_PKT				|
								0;

	DBGP_Type[FBT]			= 	\
//								BT_TRACE						|
								0;

	DBGP_Type[FEEPROM]		=	\
//								EEPROM_W						|
//								EFUSE_PG						|
//								EFUSE_READ_ALL					|
//								EFUSE_ANALYSIS					|
//								EFUSE_PG_DETAIL					|
								0;

	DBGP_Type[FDBG_CTRL]	= 	\
//								DBG_CTRL_TRACE					|
//								DBG_CTRL_INBAND_NOISE			|
								0;
	
	// 2011/07/20 MH Add for short cut 
	DBGP_Type[FSHORT_CUT] = 	\
//								SHCUT_TX 						| 
//								SHCUT_RX						|
								0;
	
#endif	
	/* Define debug header of every service module. */
	DBGP_Head.pMANS	= "\n\r[MANS] ";
	DBGP_Head.pRTOS	= "\n\r[RTOS] ";
	DBGP_Head.pALM	= "\n\r[ALM]  ";
	DBGP_Head.pPEM	= "\n\r[PEM]  ";
	DBGP_Head.pCMPK	= "\n\r[CMPK] ";
	DBGP_Head.pRAPD	= "\n\r[RAPD] ";
	DBGP_Head.pTXPB	= "\n\r[TXPB] ";
	DBGP_Head.pQUMG	= "\n\r[QUMG] ";
    	
}	/* DBGP_Flag_Init */


/*-----------------------------------------------------------------------------
 * Function:    DBG_PrintAllFlag
 *
 * Overview:    Print All debug flag
 *
 * Input:       NONE
 *
 * Output:      NONE
 *
 * Return:      NONE
 *
 * Revised History:
 *  When		Who		Remark
 *  12/10/2008	MHC		Create Version 0.
 *
 *---------------------------------------------------------------------------*/
extern	void	DBG_PrintAllFlag(void)
{
	ODM_RT_TRACE(pDM_Odm,COMP_CMD, DBG_LOUD, 	("DBGFLAG 0    FQoS\n"));
	ODM_RT_TRACE(pDM_Odm,COMP_CMD, DBG_LOUD, 	("DBGFLAG 1    FTX\n"));
	ODM_RT_TRACE(pDM_Odm,COMP_CMD, DBG_LOUD, 	("DBGFLAG 2    FRX\n"));
	ODM_RT_TRACE(pDM_Odm,COMP_CMD, DBG_LOUD, 	("DBGFLAG 3    FSEC\n"));
	ODM_RT_TRACE(pDM_Odm,COMP_CMD, DBG_LOUD, 	("DBGFLAG 4    FMGNT\n"));
	ODM_RT_TRACE(pDM_Odm,COMP_CMD, DBG_LOUD, 	("DBGFLAG 5    FMLME\n"));
	ODM_RT_TRACE(pDM_Odm,COMP_CMD, DBG_LOUD, 	("DBGFLAG 6    FRESOURCE\n"));
	ODM_RT_TRACE(pDM_Odm,COMP_CMD, DBG_LOUD, 	("DBGFLAG 7    FBEACON\n"));
	ODM_RT_TRACE(pDM_Odm,COMP_CMD, DBG_LOUD, 	("DBGFLAG 8    FISR\n"));
	ODM_RT_TRACE(pDM_Odm,COMP_CMD, DBG_LOUD, 	("DBGFLAG 9    FPHY\n"));
	ODM_RT_TRACE(pDM_Odm,COMP_CMD, DBG_LOUD, 	("DBGFLAG 11   FMP\n"));
	ODM_RT_TRACE(pDM_Odm,COMP_CMD, DBG_LOUD, 	("DBGFLAG 12   FPWR\n"));
	ODM_RT_TRACE(pDM_Odm,COMP_CMD, DBG_LOUD, 	("DBGFLAG 13   FDM\n"));
	ODM_RT_TRACE(pDM_Odm,COMP_CMD, DBG_LOUD, 	("DBGFLAG 14   FDBG_CTRL\n"));
	ODM_RT_TRACE(pDM_Odm,COMP_CMD, DBG_LOUD, 	("DBGFLAG 15   FC2H\n"));
	ODM_RT_TRACE(pDM_Odm,COMP_CMD, DBG_LOUD, 	("DBGFLAG 16   FBT\n"));			
}	// DBG_PrintAllFlag


extern	void	DBG_PrintAllComp(void)
{
	u1Byte	i;
	
	ODM_RT_TRACE(pDM_Odm,COMP_CMD, DBG_LOUD, 	("GlobalDebugComponents Definition\n"));
	ODM_RT_TRACE(pDM_Odm,COMP_CMD, DBG_LOUD, 	("BIT0    COMP_TRACE\n"));
	ODM_RT_TRACE(pDM_Odm,COMP_CMD, DBG_LOUD, 	("BIT1    COMP_DBG\n"));
	ODM_RT_TRACE(pDM_Odm,COMP_CMD, DBG_LOUD, 	("BIT2    COMP_INIT\n"));
	ODM_RT_TRACE(pDM_Odm,COMP_CMD, DBG_LOUD, 	("BIT3    COMP_OID_QUERY\n"));
	ODM_RT_TRACE(pDM_Odm,COMP_CMD, DBG_LOUD, 	("BIT4    COMP_OID_SET\n"));
	ODM_RT_TRACE(pDM_Odm,COMP_CMD, DBG_LOUD, 	("BIT5    COMP_RECV\n"));
	ODM_RT_TRACE(pDM_Odm,COMP_CMD, DBG_LOUD, 	("BIT6    COMP_SEND\n"));
	ODM_RT_TRACE(pDM_Odm,COMP_CMD, DBG_LOUD, 	("BIT7    COMP_IO\n"));
	ODM_RT_TRACE(pDM_Odm,COMP_CMD, DBG_LOUD, 	("BIT8    COMP_POWER\n"));
	ODM_RT_TRACE(pDM_Odm,COMP_CMD, DBG_LOUD, 	("BIT9   COMP_MLME\n"));
	ODM_RT_TRACE(pDM_Odm,COMP_CMD, DBG_LOUD, 	("BIT10   COMP_SCAN\n"));
	ODM_RT_TRACE(pDM_Odm,COMP_CMD, DBG_LOUD, 	("BIT11   COMP_SYSTEM\n"));
	ODM_RT_TRACE(pDM_Odm,COMP_CMD, DBG_LOUD, 	("BIT12   COMP_SEC\n"));
	ODM_RT_TRACE(pDM_Odm,COMP_CMD, DBG_LOUD, 	("BIT13   COMP_AP\n"));
	ODM_RT_TRACE(pDM_Odm,COMP_CMD, DBG_LOUD, 	("BIT14   COMP_TURBO\n"));
	ODM_RT_TRACE(pDM_Odm,COMP_CMD, DBG_LOUD, 	("BIT15   COMP_QOS\n"));
	ODM_RT_TRACE(pDM_Odm,COMP_CMD, DBG_LOUD, 	("BIT16   COMP_AUTHENTICATOR\n"));
	ODM_RT_TRACE(pDM_Odm,COMP_CMD, DBG_LOUD, 	("BIT17   COMP_BEACON\n"));
	ODM_RT_TRACE(pDM_Odm,COMP_CMD, DBG_LOUD, 	("BIT18   COMP_BEACON\n"));
	ODM_RT_TRACE(pDM_Odm,COMP_CMD, DBG_LOUD, 	("BIT19   COMP_RATE\n"));
	ODM_RT_TRACE(pDM_Odm,COMP_CMD, DBG_LOUD, 	("BIT20   COMP_EVENTS\n"));
	ODM_RT_TRACE(pDM_Odm,COMP_CMD, DBG_LOUD, 	("BIT21   COMP_FPGA\n"));
	ODM_RT_TRACE(pDM_Odm,COMP_CMD, DBG_LOUD, 	("BIT22   COMP_RM\n"));
	ODM_RT_TRACE(pDM_Odm,COMP_CMD, DBG_LOUD, 	("BIT23   COMP_MP\n"));
	ODM_RT_TRACE(pDM_Odm,COMP_CMD, DBG_LOUD, 	("BIT24   COMP_RXDESC\n"));
	ODM_RT_TRACE(pDM_Odm,COMP_CMD, DBG_LOUD, 	("BIT25   COMP_CKIP\n"));
	ODM_RT_TRACE(pDM_Odm,COMP_CMD, DBG_LOUD, 	("BIT26   COMP_DIG\n"));
	ODM_RT_TRACE(pDM_Odm,COMP_CMD, DBG_LOUD, 	("BIT27   COMP_TXAGC\n"));
	ODM_RT_TRACE(pDM_Odm,COMP_CMD, DBG_LOUD, 	("BIT28   COMP_HIPWR\n"));
	ODM_RT_TRACE(pDM_Odm,COMP_CMD, DBG_LOUD, 	("BIT29   COMP_HALDM\n"));
	ODM_RT_TRACE(pDM_Odm,COMP_CMD, DBG_LOUD, 	("BIT30   COMP_RSNA\n"));
	ODM_RT_TRACE(pDM_Odm,COMP_CMD, DBG_LOUD, 	("BIT31   COMP_INDIC\n"));
	ODM_RT_TRACE(pDM_Odm,COMP_CMD, DBG_LOUD, 	("BIT32   COMP_LED\n"));
	ODM_RT_TRACE(pDM_Odm,COMP_CMD, DBG_LOUD, 	("BIT33   COMP_RF\n"));
	ODM_RT_TRACE(pDM_Odm,COMP_CMD, DBG_LOUD, 	("BIT34   COMP_HT\n"));
	ODM_RT_TRACE(pDM_Odm,COMP_CMD, DBG_LOUD, 	("BIT35   COMP_POWER_TRACKING\n"));
	ODM_RT_TRACE(pDM_Odm,COMP_CMD, DBG_LOUD, 	("BIT36   COMP_POWER_TRACKING\n"));
	ODM_RT_TRACE(pDM_Odm,COMP_CMD, DBG_LOUD, 	("BIT37   COMP_AMSDU\n"));
	ODM_RT_TRACE(pDM_Odm,COMP_CMD, DBG_LOUD, 	("BIT38   COMP_WPS\n"));
	ODM_RT_TRACE(pDM_Odm,COMP_CMD, DBG_LOUD, 	("BIT39   COMP_RATR\n"));
	ODM_RT_TRACE(pDM_Odm,COMP_CMD, DBG_LOUD, 	("BIT40   COMP_RESET\n"));
	ODM_RT_TRACE(pDM_Odm,COMP_CMD, DBG_LOUD, 	("BIT41   COMP_CMD\n"));
	ODM_RT_TRACE(pDM_Odm,COMP_CMD, DBG_LOUD, 	("BIT42   COMP_EFUSE\n"));
	ODM_RT_TRACE(pDM_Odm,COMP_CMD, DBG_LOUD, 	("BIT43   COMP_MESH_INTERWORKING\n"));
	ODM_RT_TRACE(pDM_Odm,COMP_CMD, DBG_LOUD, 	("BIT43   COMP_CCX\n"));		
	
	ODM_RT_TRACE(pDM_Odm,COMP_CMD, DBG_LOUD, 	("GlobalDebugComponents = %"i64fmt"x\n", GlobalDebugComponents));
	ODM_RT_TRACE(pDM_Odm,COMP_CMD, DBG_LOUD, 	("Enable DBG COMP ="));
	for (i = 0; i < 64; i++)
	{
		if (GlobalDebugComponents & ((u8Byte)0x1 << i) )
		{
			ODM_RT_TRACE(pDM_Odm,COMP_CMD, DBG_LOUD, 	("BIT%02d |\n", i));
		}
	}
	ODM_RT_TRACE(pDM_Odm,COMP_CMD, DBG_LOUD, 	("\n"));
	
}	// DBG_PrintAllComp


/*-----------------------------------------------------------------------------
 * Function:    DBG_PrintFlagEvent
 *
 * Overview:    Print dedicated debug flag event
 *
 * Input:       NONE
 *
 * Output:      NONE
 *
 * Return:      NONE
 *
 * Revised History:
 *  When		Who		Remark
 *  12/10/2008	MHC		Create Version 0.
 *
 *---------------------------------------------------------------------------*/
extern	void	DBG_PrintFlagEvent(u1Byte	DbgFlag)
{
	switch(DbgFlag)
	{
		case	FQoS:
		ODM_RT_TRACE(pDM_Odm,COMP_CMD, DBG_LOUD, 	("BIT 0    QoS_INIT\n"));
		ODM_RT_TRACE(pDM_Odm,COMP_CMD, DBG_LOUD, 	("BIT 1    QoS_VISTA\n"));
		break;

		case	FTX:
		ODM_RT_TRACE(pDM_Odm,COMP_CMD, DBG_LOUD, 	("BIT 0    TX_DESC\n"));
		ODM_RT_TRACE(pDM_Odm,COMP_CMD, DBG_LOUD, 	("BIT 1    TX_DESC_TID\n"));
		break;

		case	FRX:
		ODM_RT_TRACE(pDM_Odm,COMP_CMD, DBG_LOUD, 	("BIT 0    RX_DATA\n"));
		ODM_RT_TRACE(pDM_Odm,COMP_CMD, DBG_LOUD, 	("BIT 1    RX_PHY_STS\n"));
		ODM_RT_TRACE(pDM_Odm,COMP_CMD, DBG_LOUD, 	("BIT 2    RX_PHY_SS\n"));
		ODM_RT_TRACE(pDM_Odm,COMP_CMD, DBG_LOUD, 	("BIT 3    RX_PHY_SQ\n"));
		ODM_RT_TRACE(pDM_Odm,COMP_CMD, DBG_LOUD, 	("BIT 4    RX_PHY_ASTS\n"));
		ODM_RT_TRACE(pDM_Odm,COMP_CMD, DBG_LOUD, 	("BIT 5    RX_ERR_LEN\n"));
		ODM_RT_TRACE(pDM_Odm,COMP_CMD, DBG_LOUD, 	("BIT 6    RX_DEFRAG\n"));
		ODM_RT_TRACE(pDM_Odm,COMP_CMD, DBG_LOUD, 	("BIT 7    RX_ERR_RATE\n"));
		break;

		case	FSEC:
		ODM_RT_TRACE(pDM_Odm,COMP_CMD, DBG_LOUD, 	("NA\n"));
		break;

		case	FMGNT:
		ODM_RT_TRACE(pDM_Odm,COMP_CMD, DBG_LOUD, 	("NA\n"));
		break;

		case	FMLME:
		ODM_RT_TRACE(pDM_Odm,COMP_CMD, DBG_LOUD, 	("BIT 0    MEDIA_STS\n"));
		ODM_RT_TRACE(pDM_Odm,COMP_CMD, DBG_LOUD, 	("BIT 1    LINK_STS\n"));		
		break;

		case	FRESOURCE:
		ODM_RT_TRACE(pDM_Odm,COMP_CMD, DBG_LOUD, 	("BIT 0    OS_CHK\n"));
		break;

		case	FBEACON:
		ODM_RT_TRACE(pDM_Odm,COMP_CMD, DBG_LOUD, 	("BIT 0    BCN_SHOW\n"));
		ODM_RT_TRACE(pDM_Odm,COMP_CMD, DBG_LOUD, 	("BIT 1    BCN_PEER\n"));		
		break;

		case	FISR:
		ODM_RT_TRACE(pDM_Odm,COMP_CMD, DBG_LOUD, 	("BIT 0    ISR_CHK\n"));
		break;

		case	FPHY:
		ODM_RT_TRACE(pDM_Odm,COMP_CMD, DBG_LOUD, 	("BIT 0    PHY_BBR\n"));
		ODM_RT_TRACE(pDM_Odm,COMP_CMD, DBG_LOUD, 	("BIT 1    PHY_BBW\n"));
		ODM_RT_TRACE(pDM_Odm,COMP_CMD, DBG_LOUD, 	("BIT 2    PHY_RFR\n"));
		ODM_RT_TRACE(pDM_Odm,COMP_CMD, DBG_LOUD, 	("BIT 3    PHY_RFW\n"));
		ODM_RT_TRACE(pDM_Odm,COMP_CMD, DBG_LOUD, 	("BIT 4    PHY_MACR\n"));
		ODM_RT_TRACE(pDM_Odm,COMP_CMD, DBG_LOUD, 	("BIT 5    PHY_MACW\n"));
		ODM_RT_TRACE(pDM_Odm,COMP_CMD, DBG_LOUD, 	("BIT 6    PHY_ALLR\n"));
		ODM_RT_TRACE(pDM_Odm,COMP_CMD, DBG_LOUD, 	("BIT 7    PHY_ALLW\n"));
		ODM_RT_TRACE(pDM_Odm,COMP_CMD, DBG_LOUD, 	("BIT 8    PHY_TXPWR\n"));
		break;

		case	FMP:
		ODM_RT_TRACE(pDM_Odm,COMP_CMD, DBG_LOUD, 	("BIT 0    MP_RX\n"));
		break;
		
		case	FEEPROM:
		ODM_RT_TRACE(pDM_Odm,COMP_CMD, DBG_LOUD, 	("BIT 0    EEPROM_W\n"));
		ODM_RT_TRACE(pDM_Odm,COMP_CMD, DBG_LOUD, 	("BIT 1    EFUSE_PG\n"));		
		break;
		
		case	FPWR:
		ODM_RT_TRACE(pDM_Odm,COMP_CMD, DBG_LOUD, 	("BIT 0    LPS\n"));
		ODM_RT_TRACE(pDM_Odm,COMP_CMD, DBG_LOUD, 	("BIT 1    IPS\n"));		
		ODM_RT_TRACE(pDM_Odm,COMP_CMD, DBG_LOUD, 	("BIT 2    PWRSW\n"));
		ODM_RT_TRACE(pDM_Odm,COMP_CMD, DBG_LOUD, 	("BIT 3    PWRHW\n"));
		ODM_RT_TRACE(pDM_Odm,COMP_CMD, DBG_LOUD, 	("BIT 4    PWRHAL\n"));
		break;

		case	FDM:
		ODM_RT_TRACE(pDM_Odm,COMP_CMD, DBG_LOUD, 	("BIT 0    WA_IOT\n"));
		ODM_RT_TRACE(pDM_Odm,COMP_CMD, DBG_LOUD, 	("BIT 1    DM_PWDB\n"));		
		ODM_RT_TRACE(pDM_Odm,COMP_CMD, DBG_LOUD, 	("BIT 2    DM_Monitor\n"));		
		ODM_RT_TRACE(pDM_Odm,COMP_CMD, DBG_LOUD, 	("BIT 3    DM_DIG\n"));		
		break;

		case	FDBG_CTRL:
		ODM_RT_TRACE(pDM_Odm,COMP_CMD, DBG_LOUD, 	("BIT 0    DBG_CTRL_TRACE\n"));
		ODM_RT_TRACE(pDM_Odm,COMP_CMD, DBG_LOUD, 	("BIT 1    DBG_CTRL_INBAND_NOISE\n"));		
		break;
		
		case	FC2H:
		ODM_RT_TRACE(pDM_Odm,COMP_CMD, DBG_LOUD, 	("BIT 0    C2H_Summary\n"));
		ODM_RT_TRACE(pDM_Odm,COMP_CMD, DBG_LOUD, 	("BIT 1    C2H_PacketData\n"));		
		ODM_RT_TRACE(pDM_Odm,COMP_CMD, DBG_LOUD, 	("BIT 2    C2H_ContentData\n"));		
		break;
		
		case	FBT:
		ODM_RT_TRACE(pDM_Odm,COMP_CMD, DBG_LOUD, 	("BIT 0    BT_TRACE\n"));
		ODM_RT_TRACE(pDM_Odm,COMP_CMD, DBG_LOUD, 	("BIT 1    BT_RFPoll\n"));		
		break;		
		
		default:
			break;
	}

}	// DBG_PrintFlagEvent


extern	void	DBG_DumpMem(const u1Byte DbgComp, 
							const u1Byte DbgLevel, 
							pu1Byte pMem, 
							u2Byte Len)
{
	u2Byte i;

	for (i=0;i<((Len>>3) + 1);i++)
	{
		ODM_RT_TRACE(pDM_Odm,DbgComp, DbgLevel, ("%02X %02X %02X %02X %02X %02X %02X %02X\n",
					*(pMem+(i*8)), *(pMem+(i*8+1)), *(pMem+(i*8+2)), *(pMem+(i*8+3)),
					*(pMem+(i*8+4)), *(pMem+(i*8+5)), *(pMem+(i*8+6)), *(pMem+(i*8+7))));
		
	}
}


#endif


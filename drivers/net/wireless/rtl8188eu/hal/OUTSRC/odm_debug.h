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


#ifndef	__ODM_DBG_H__
#define __ODM_DBG_H__


//-----------------------------------------------------------------------------
//	Define the debug levels
//
//	1.	DBG_TRACE and DBG_LOUD are used for normal cases.
//	So that, they can help SW engineer to develope or trace states changed 
//	and also help HW enginner to trace every operation to and from HW, 
//	e.g IO, Tx, Rx. 
//
//	2.	DBG_WARNNING and DBG_SERIOUS are used for unusual or error cases, 
//	which help us to debug SW or HW.
//
//-----------------------------------------------------------------------------
//
//	Never used in a call to ODM_RT_TRACE()!
//
#define ODM_DBG_OFF					1

//
//	Fatal bug. 
//	For example, Tx/Rx/IO locked up, OS hangs, memory access violation, 
//	resource allocation failed, unexpected HW behavior, HW BUG and so on.
//
#define ODM_DBG_SERIOUS				2

//
//	Abnormal, rare, or unexpeted cases.
//	For example, IRP/Packet/OID canceled, device suprisely unremoved and so on.
//
#define ODM_DBG_WARNING				3

//
//	Normal case with useful information about current SW or HW state. 
//	For example, Tx/Rx descriptor to fill, Tx/Rx descriptor completed status, 
//	SW protocol state change, dynamic mechanism state change and so on.
//
#define ODM_DBG_LOUD					4

//
//	Normal case with detail execution flow or information.
//
#define ODM_DBG_TRACE					5

//-----------------------------------------------------------------------------
// Define the tracing components
//
//-----------------------------------------------------------------------------
//BB Functions
#define ODM_COMP_DIG					BIT0	
#define ODM_COMP_RA_MASK				BIT1	
#define ODM_COMP_DYNAMIC_TXPWR		BIT2
#define ODM_COMP_FA_CNT				BIT3
#define ODM_COMP_RSSI_MONITOR		BIT4
#define ODM_COMP_CCK_PD				BIT5
#define ODM_COMP_ANT_DIV				BIT6
#define ODM_COMP_PWR_SAVE			BIT7
#define ODM_COMP_PWR_TRAIN			BIT8
#define ODM_COMP_RATE_ADAPTIVE		BIT9
#define ODM_COMP_PATH_DIV				BIT10
#define ODM_COMP_PSD					BIT11
#define ODM_COMP_DYNAMIC_PRICCA		BIT12
#define ODM_COMP_RXHP				BIT13			
//MAC Functions
#define ODM_COMP_EDCA_TURBO			BIT16
#define ODM_COMP_EARLY_MODE			BIT17
//RF Functions
#define ODM_COMP_TX_PWR_TRACK		BIT24
#define ODM_COMP_RX_GAIN_TRACK		BIT25
#define ODM_COMP_CALIBRATION			BIT26
//Common Functions
#define ODM_COMP_COMMON				BIT30
#define ODM_COMP_INIT					BIT31

/*------------------------Export Marco Definition---------------------------*/
#if (DM_ODM_SUPPORT_TYPE == ODM_MP)
#define RT_PRINTK				DbgPrint
#elif (DM_ODM_SUPPORT_TYPE == ODM_CE)
	#define DbgPrint	printk
	#define RT_PRINTK(fmt, args...)	DbgPrint( "%s(): " fmt, __FUNCTION__, ## args);
#else
	#define DbgPrint	panic_printk
#define RT_PRINTK(fmt, args...)	DbgPrint( "%s(): " fmt, __FUNCTION__, ## args);
#endif

#ifndef ASSERT
	#define ASSERT(expr)
#endif

#if DBG
#define ODM_RT_TRACE(pDM_Odm, comp, level, fmt)									\
		if(((comp) & pDM_Odm->DebugComponents) && (level <= pDM_Odm->DebugLevel))	\
		{																			\
			if(pDM_Odm->SupportICType == ODM_RTL8192C)								\
				DbgPrint("[ODM-92C] ");												\
			else if(pDM_Odm->SupportICType == ODM_RTL8192D)							\
				DbgPrint("[ODM-92D] ");												\
			else if(pDM_Odm->SupportICType == ODM_RTL8723A)							\
				DbgPrint("[ODM-8723A] ");												\
			else if(pDM_Odm->SupportICType == ODM_RTL8188E)							\
				DbgPrint("[ODM-8188E] ");												\
			else if(pDM_Odm->SupportICType == ODM_RTL8812)							\
				DbgPrint("[ODM-8812] ");												\
			else if(pDM_Odm->SupportICType == ODM_RTL8821)							\
				DbgPrint("[ODM-8821] ");												\
			RT_PRINTK fmt;															\
		}

#define ODM_RT_TRACE_F(pDM_Odm, comp, level, fmt)									\
		if(((comp) & pDM_Odm->DebugComponents) && (level <= pDM_Odm->DebugLevel))	\
		{																			\
			RT_PRINTK fmt;															\
		}

#define ODM_RT_ASSERT(pDM_Odm, expr, fmt)											\
		if(!(expr)) {																	\
			DbgPrint( "Assertion failed! %s at ......\n", #expr);								\
			DbgPrint( "      ......%s,%s,line=%d\n",__FILE__,__FUNCTION__,__LINE__);			\
			RT_PRINTK fmt;															\
			ASSERT(FALSE);															\
		}
#define ODM_dbg_enter() { DbgPrint("==> %s\n", __FUNCTION__); }
#define ODM_dbg_exit() { DbgPrint("<== %s\n", __FUNCTION__); }
#define ODM_dbg_trace(str) { DbgPrint("%s:%s\n", __FUNCTION__, str); }

#define ODM_PRINT_ADDR(pDM_Odm, comp, level, title_str, ptr)						\
			if(((comp) & pDM_Odm->DebugComponents) && (level <= pDM_Odm->DebugLevel))	\
			{																		\
				int __i;																\
				pu1Byte	__ptr = (pu1Byte)ptr;											\
				DbgPrint("[ODM] ");													\
				DbgPrint(title_str);												\
				DbgPrint(" ");														\
				for( __i=0; __i<6; __i++ )												\
					DbgPrint("%02X%s", __ptr[__i], (__i==5)?"":"-");						\
				DbgPrint("\n");														\
			}
#else
#define ODM_RT_TRACE(pDM_Odm, comp, level, fmt)
#define ODM_RT_TRACE_F(pDM_Odm, comp, level, fmt)
#define ODM_RT_ASSERT(pDM_Odm, expr, fmt)
#define ODM_dbg_enter()
#define ODM_dbg_exit()
#define ODM_dbg_trace(str)
#define ODM_PRINT_ADDR(pDM_Odm, comp, level, title_str, ptr)
#endif


VOID 
ODM_InitDebugSetting(
	IN		PDM_ODM_T		pDM_Odm
	);



#if 0
#if DBG
#define DbgPrint printk

#define PRINT_DATA(_TitleString, _HexData, _HexDataLen)								\
			{																		\
				char			*szTitle = _TitleString;									\
				pu1Byte		pbtHexData = _HexData;									\
				u4Byte		u4bHexDataLen = _HexDataLen;							\
				u4Byte		__i;														\
				DbgPrint("%s", szTitle);													\
				for (__i=0;__i<u4bHexDataLen;__i++)									\
				{																	\
					if ((__i & 15) == 0) 												\
					{																\
						DbgPrint("\n");												\
					}																\
					DbgPrint("%02X%s", pbtHexData[__i], ( ((__i&3)==3) ? "  " : " ") );		\
				}																	\
				DbgPrint("\n");														\
			}

// RT_PRINT_XXX macros: implemented for debugging purpose.
// Added by Annie, 2005-11-21.
#define RT_PRINT_DATA(_Comp, _Level, _TitleString, _HexData, _HexDataLen)			\
			if(((_Comp) & ODM_GlobalDebugComponents) && (_Level <= ODM_GlobalDebugLevel))	\
			{																		\
				int __i;																\
				pu1Byte	ptr = (pu1Byte)_HexData;										\
				DbgPrint("Rtl819x: ");													\
				DbgPrint(_TitleString);												\
				for( __i=0; __i<(int)_HexDataLen; __i++ )								\
				{																	\
					DbgPrint("%02X%s", ptr[__i], (((__i + 1) % 4) == 0)?"  ":" ");			\
					if (((__i + 1) % 16) == 0)	DbgPrint("\n");							\
				}																	\
				DbgPrint("\n");														\
			}

#define RT_PRINT_ADDR(_Comp, _Level, _TitleString, _Ptr)								\
			if(((_Comp) & ODM_GlobalDebugComponents) && (_Level <= ODM_GlobalDebugLevel))	\
			{																		\
				int __i;																\
				pu1Byte	ptr = (pu1Byte)_Ptr;											\
				DbgPrint("Rtl819x: ");													\
				DbgPrint(_TitleString);												\
				DbgPrint(" ");															\
				for( __i=0; __i<6; __i++ )												\
					DbgPrint("%02X%s", ptr[__i], (__i==5)?"":"-");							\
				DbgPrint("\n");														\
			}

#define RT_PRINT_ADDRS(_Comp, _Level, _TitleString, _Ptr, _AddNum)					\
			if(((_Comp) & ODM_GlobalDebugComponents) && (_Level <= ODM_GlobalDebugLevel))	\
			{																		\
				int __i, __j;															\
				pu1Byte	ptr = (pu1Byte)_Ptr;											\
				DbgPrint("Rtl819x: ");													\
				DbgPrint(_TitleString);												\
				DbgPrint("\n");														\
				for( __i=0; __i<(int)_AddNum; __i++ )									\
				{																	\
					for( __j=0; __j<6; __j++ )											\
						DbgPrint("%02X%s", ptr[__i*6+__j], (__j==5)?"":"-");				\
					DbgPrint("\n");													\
				}																	\
			}

// Added by Annie, 2005-11-22.
#define	MAX_STR_LEN	64
#define	PRINTABLE(_ch)	(_ch>=' ' &&_ch<='~' )	// I want to see ASCII 33 to 126 only. Otherwise, I print '?'. Annie, 2005-11-22.

#define RT_PRINT_STR(_Comp, _Level, _TitleString, _Ptr, _Len)							\
			if(((_Comp) & ODM_GlobalDebugComponents) && (_Level <= ODM_GlobalDebugLevel))	\
			{																		\
				int		__i;															\
				u1Byte	buffer[MAX_STR_LEN];											\
				int	length = (_Len<MAX_STR_LEN)? _Len : (MAX_STR_LEN-1) ;				\
				PlatformZeroMemory( buffer, MAX_STR_LEN );							\
				PlatformMoveMemory( buffer, (pu1Byte)_Ptr, length );						\
				for( __i=0; __i<MAX_STR_LEN; __i++ )									\
				{																	\
					if( !PRINTABLE(buffer[__i]) )	buffer[__i] = '?';						\
				}																	\
				buffer[length] = '\0';													\
				DbgPrint("Rtl819x: ");													\
				DbgPrint(_TitleString);												\
				DbgPrint(": %d, <%s>\n", _Len, buffer);									\
			}
			
#else	// of #if DBG
#define DbgPrint(...)	
#define PRINT_DATA(_TitleString, _HexData, _HexDataLen)
#define RT_PRINT_DATA(_Comp, _Level, _TitleString, _HexData, _HexDataLen)
#define RT_PRINT_ADDR(_Comp, _Level, _TitleString, _Ptr)
#define RT_PRINT_ADDRS(_Comp, _Level, _TitleString, _Ptr, _AddNum)
#define RT_PRINT_STR(_Comp, _Level, _TitleString, _Ptr, _Len)
#endif	// of #if DBG

#endif	


#if 0
/* Define debug print header for every service module.*/
typedef struct tag_ODM_DBGP_Service_Module_Header_Name_Structure
{
	const char 	*pMANS;
	const char 	*pRTOS;
	const char 	*pALM;
	const char 	*pPEM;
	const char 	*pCMPK;
	const char 	*pRAPD;
	const char 	*pTXPB;
	const char 	*pQUMG;
}ODM_DBGP_HEAD_T;


/* Define different debug flag for dedicated service modules in debug flag array. */
// Each module has independt 32 bit debug flag you cnn define the flag as yout require.
typedef enum tag_ODM_DBGP_Flag_Type_Definition
{
	ODM_FTX				= 0,
	ODM_FRX				,	
	ODM_FPHY				,
	ODM_FPWR				,
	ODM_FDM				,
	ODM_FC2H				,
	ODM_FBT				,
	ODM_DBGP_TYPE_MAX
}ODM_DBGP_FLAG_E;


// Define TX relative debug bit				--> FTX
#define		ODM_TX_DESC			BIT0
#define		ODM_TX_DESC_TID		BIT1
#define		ODM_TX_PATH			BIT2

// Define RX relative debug  bit				--> FRX
#define		ODM_RX_DATA				BIT0	
#define		ODM_RX_PHY_STS			BIT1
#define		ODM_RX_PHY_SS				BIT2
#define		ODM_RX_PHY_SQ				BIT3
#define		ODM_RX_PHY_ASTS			BIT4
#define		ODM_RX_ERR_LEN			BIT5
#define		ODM_RX_DEFRAG				BIT6
#define		ODM_RX_ERR_RATE			BIT7
#define		ODM_RX_PATH				BIT8
#define		ODM_RX_BEACON				BIT9

// Define PHY-BB/RF/MAC check module bit		--> FPHY
#define		ODM_PHY_BBR				BIT0
#define		ODM_PHY_BBW				BIT1
#define		ODM_PHY_RFR				BIT2
#define		ODM_PHY_RFW				BIT3
#define		ODM_PHY_MACR				BIT4
#define		ODM_PHY_MACW				BIT5
#define		ODM_PHY_ALLR				BIT6
#define		ODM_PHY_ALLW				BIT7
#define		ODM_PHY_TXPWR			BIT8
#define		ODM_PHY_PWRDIFF			BIT9
#define		ODM_PHY_SICR				BIT10
#define		ODM_PHY_SICW				BIT11




extern	u4Byte ODM_GlobalDebugLevel;


#if DBG
extern	u8Byte ODM_GlobalDebugComponents;
#endif
#endif
#if 0

//-----------------------------------------------------------------------------
//	Define the debug levels
//
//	1.	DBG_TRACE and DBG_LOUD are used for normal cases.
//	So that, they can help SW engineer to develope or trace states changed 
//	and also help HW enginner to trace every operation to and from HW, 
//	e.g IO, Tx, Rx. 
//
//	2.	DBG_WARNNING and DBG_SERIOUS are used for unusual or error cases, 
//	which help us to debug SW or HW.
//
//-----------------------------------------------------------------------------
//
//	Never used in a call to ODM_RT_TRACE(pDM_Odm,)!
//
#define DBG_OFF					0

//
//	Deprecated! Don't use it! 
//	TODO: fix related debug message!
//
//#define DBG_SEC					1

//
//	Fatal bug. 
//	For example, Tx/Rx/IO locked up, OS hangs, memory access violation, 
//	resource allocation failed, unexpected HW behavior, HW BUG and so on.
//
#define DBG_SERIOUS				2

//
//	Abnormal, rare, or unexpeted cases.
//	For example, IRP/Packet/OID canceled, device suprisely unremoved and so on.
//
#define DBG_WARNING				3

//
//	Normal case with useful information about current SW or HW state. 
//	For example, Tx/Rx descriptor to fill, Tx/Rx descriptor completed status, 
//	SW protocol state change, dynamic mechanism state change and so on.
//
#define DBG_LOUD				4

//
//	Normal case with detail execution flow or information.
//
#define DBG_TRACE				5



//-----------------------------------------------------------------------------
// Define the tracing components
//
//-----------------------------------------------------------------------------
#define COMP_TRACE				BIT0		// For function call tracing.
#define COMP_DBG				BIT1		// Only for temporary debug message.
#define COMP_INIT				BIT2		// during driver initialization / halt / reset.
#define COMP_OID_QUERY				BIT3		// Query OID.
#define COMP_OID_SET				BIT4		// Set OID.
#define COMP_RECV				BIT5		// Reveive part data path.
#define COMP_SEND				BIT6		// Send part path.
#define COMP_IO					BIT7		// I/O Related. Added by Annie, 2006-03-02.
#define COMP_POWER				BIT8		// 802.11 Power Save mode or System/Device Power state related.
#define COMP_MLME				BIT9		// 802.11 link related: join/start BSS, leave BSS.
#define COMP_SCAN				BIT10	// For site survey.
#define COMP_SYSTEM				BIT11	// For general platform function.
#define COMP_SEC				BIT12	// For Security.
#define COMP_AP					BIT13	// For AP mode related.
#define COMP_TURBO				BIT14	// For Turbo Mode related. By Annie, 2005-10-21.
#define COMP_QOS				BIT15	// For QoS.
#define COMP_AUTHENTICATOR			BIT16	// For AP mode Authenticator. Added by Annie, 2006-01-30.
#define COMP_BEACON				BIT17	// For Beacon related, by rcnjko.
#define COMP_ANTENNA				BIT18	// For Antenna diversity related, by rcnjko. 
#define COMP_RATE				BIT19	// For Rate Adaptive mechanism, 2006.07.02, by rcnjko. #define COMP_EVENTS				0x00000080	// Event handling
#define COMP_EVENTS				BIT20	// Event handling
#define COMP_FPGA				BIT21	// For FPGA verfication 
#define COMP_RM					BIT22	// For Radio Measurement. 
#define COMP_MP					BIT23	// For mass production test, by shien chang, 2006.07.13
#define COMP_RXDESC				BIT24	// Show Rx desc information for SD3 debug. Added by Annie, 2006-07-15.
#define COMP_CKIP				BIT25	// For CCX 1 S13: CKIP. Added by Annie, 2006-08-14.
#define COMP_DIG				BIT26	// For DIG, 2006.09.25, by rcnjko.
#define COMP_TXAGC				BIT27	// For Tx power, 060928, by rcnjko. 
#define COMP_HIPWR				BIT28	// For High Power Mechanism, 060928, by rcnjko. 
#define COMP_HALDM				BIT29	// For HW Dynamic Mechanism, 061010, by rcnjko. 
#define COMP_RSNA				BIT30	// For RSNA IBSS , 061201, by CCW. 
#define COMP_INDIC				BIT31	// For link indication
#define COMP_LED				BIT32	// For LED.
#define COMP_RF					BIT33	// For RF.
//1!!!!!!!!!!!!!!!!!!!!!!!!!!!
//1//1Attention Please!!!<11n or 8190 specific code should be put below this line>
//1!!!!!!!!!!!!!!!!!!!!!!!!!!!

#define COMP_HT					BIT34	// For 802.11n HT related information. by Emily 2006-8-11
#define COMP_POWER_TRACKING			BIT35	//FOR 8190 TX POWER TRACKING
#define COMP_RX_REORDER				BIT36	// 8190 Rx Reorder
#define COMP_AMSDU				BIT37	// For A-MSDU Debugging
#define COMP_WPS				BIT38   //WPS Debug Message 
#define COMP_RATR				BIT39	
#define COMP_RESET				BIT40
// For debug command to print on dbgview!!
#define COMP_CMD				BIT41
#define COMP_EFUSE				BIT42
#define COMP_MESH_INTERWORKING 			BIT43
#define COMP_CCX				BIT44	//CCX Debug Flag
#define	COMP_IOCTL				BIT45	// IO Control
#define COMP_GP					BIT46	// For generic parser.
#define COMP_TXAGG				BIT47
#define COMP_HVL                            	BIT48     // For Ndis 6.2 Context Swirch and Hardware Virtualiztion Layer
#define COMP_TEST				BIT49
#define COMP_BB_POWERSAVING	BIT50
#define COMP_SWAS				BIT51	// For SW Antenna Switch
#define COMP_P2P				BIT52
#define COMP_MUX				BIT53
#define COMP_FUNC				BIT54
#define COMP_TDLS				BIT55
#define COMP_OMNIPEEK			BIT56
#define COMP_DUALMACSWITCH	BIT60	// 2010/12/27 Add for Dual mac mode debug
#define COMP_EASY_CONCURRENT	BIT61	// 2010/12/27 Add for easy cncurrent mode debug
#define COMP_PSD				BIT63	//2011/3/9 Add for WLAN PSD for BT AFH

#define COMP_DFS                                BIT62

#define	COMP_ALL				UINT64_C(0xFFFFFFFFFFFFFFFF) // All components
// For debug print flag to use
/*------------------------------Define structure----------------------------*/
/* 2007/07/13 MH  *//*------For DeBuG Print modeue------*/

/* Defnie structure to store different debug flag variable. Every debug flag 
     is a UINT32 integer and you can assign 32 different events. */
typedef struct tag_DBGP_Debug_Flag_Structure
{
	u4Byte	Mans;			/* Main Scheduler module. */
	u4Byte	Rtos;			/* RTOS module. */
	u4Byte	Alarm;		/* Alarm module. */	
	u4Byte	Pm;			/* Performance monitor module. */	
}DBGP_FLAG_T;

/* Define debug print header for every service module.*/
typedef struct tag_DBGP_Service_Module_Header_Name_Structure
{
	const char 	*pMANS;
	const char 	*pRTOS;
	const char 	*pALM;
	const char 	*pPEM;
	const char 	*pCMPK;
	const char 	*pRAPD;
	const char 	*pTXPB;
	const char 	*pQUMG;
}DBGP_HEAD_T;


/* Define different debug flag for dedicated service modules in debug flag array. */
// Each module has independt 32 bit debug flag you cnn define the flag as yout require.
typedef enum tag_DBGP_Flag_Type_Definition
{
	FQoS				= 0,	
	FTX					= 1,
	FRX					= 2,	
	FSEC				= 3,
	FMGNT				= 4,
	FMLME				= 5,
	FRESOURCE			= 6,
	FBEACON				= 7,
	FISR				= 8,
	FPHY				= 9,
	FMP					= 10,
	FEEPROM			= 11,
	FPWR				= 12,
	FDM					= 13,
	FDBG_CTRL			= 14,
	FC2H				= 15,
	FBT					= 16,
	FINIT				= 17,
	FIOCTL				= 18,
	FSHORT_CUT			= 19,
	DBGP_TYPE_MAX
}DBGP_FLAG_E;


// Define Qos Relative debug flag bit		--> FQoS
#define		QoS_INIT			BIT0
#define		QoS_VISTA			BIT1

// Define TX relative debug bit				--> FTX
#define		TX_DESC			BIT0
#define		TX_DESC_TID		BIT1
#define		TX_PATH			BIT2

// Define RX relative debug  bit				--> FRX
#define		RX_DATA				BIT0	
#define		RX_PHY_STS				BIT1
#define		RX_PHY_SS				BIT2
#define		RX_PHY_SQ				BIT3
#define		RX_PHY_ASTS			BIT4
#define		RX_ERR_LEN			BIT5
#define		RX_DEFRAG			BIT6
#define		RX_ERR_RATE			BIT7
#define		RX_PATH				BIT8
#define		RX_BEACON				BIT9

// Define Security relative debug  bit			--> FSEC

// Define MGNT relative debug bit			--> FMGNT

// Define MLME relative debug bit				--> FMLME
#define		MEDIA_STS			BIT0
#define		LINK_STS			BIT1

// Define OS resource check module bit		--> FRESOURCE
#define		OS_CHK				BIT0

// Define beacon content check module bit		--> FBEACON
#define		BCN_SHOW			BIT0
#define		BCN_PEER			BIT1

// Define ISR/IMR check module bit		--> FISR
#define		ISR_CHK				BIT0

// Define PHY-BB/RF/MAC check module bit		--> FPHY
#define		PHY_BBR				BIT0
#define		PHY_BBW				BIT1
#define		PHY_RFR				BIT2
#define		PHY_RFW				BIT3
#define		PHY_MACR				BIT4
#define		PHY_MACW				BIT5
#define		PHY_ALLR				BIT6
#define		PHY_ALLW				BIT7
#define		PHY_TXPWR				BIT8
#define		PHY_PWRDIFF			BIT9
#define		PHY_SICR				BIT10
#define		PHY_SICW				BIT11

// Define MPT driver check module bit		--> FMP
#define		MP_RX					BIT0
#define		MP_SWICH_CH			BIT1

// Define EEPROM and EFUSE  check module bit		--> FEEPROM
#define		EEPROM_W					BIT0
#define		EFUSE_PG					BIT1
#define		EFUSE_READ_ALL			BIT2
#define		EFUSE_ANALYSIS				BIT3
#define		EFUSE_PG_DETAIL			BIT4

// Define power save  check module bit		--> FPWR
#define		LPS					BIT0
#define		IPS					BIT1
#define		PWRSW				BIT2
#define		PWRHW				BIT3
#define		PWRHAL				BIT4

// Define Dynamic Mechanism check module bit		--> FDM
#define		WA_IOT				BIT0
#define		DM_PWDB			BIT1
#define		DM_Monitor			BIT2
#define		DM_DIG				BIT3
#define		DM_EDCA_Turbo		BIT4
#define		DM_BT30			BIT5

// Define Dbg Control module bit		--> FDBG_CTRL
#define		DBG_CTRL_TRACE			BIT0
#define		DBG_CTRL_INBAND_NOISE	BIT1

// Define FW C2H Cmd check module bit		--> FC2H
#define		C2H_Summary				BIT0
#define		C2H_PacketData				BIT1
#define		C2H_ContentData			BIT2
// Define BT Cmd check module bit		--> FBT
#define		BT_TRACE					BIT0
#define		BT_RFPoll					BIT1

// Define init check for module bit		--> FINIT
#define		INIT_EEPROM				BIT0
#define		INIT_TxPower				BIT1
#define		INIT_IQK					BIT2
#define		INIT_RF						BIT3

// Define IOCTL Cmd check module bit		--> FIOCTL
// section 1 : IRP related
#define		IOCTL_IRP						BIT0
#define		IOCTL_IRP_DETAIL				BIT1
#define		IOCTL_IRP_STATISTICS			BIT2
#define		IOCTL_IRP_HANDLE				BIT3
// section 2 : HCI command/event
#define		IOCTL_BT_HCICMD				BIT8
#define		IOCTL_BT_HCICMD_DETAIL		BIT9
#define		IOCTL_BT_HCICMD_EXT			BIT10
#define		IOCTL_BT_EVENT					BIT11
#define		IOCTL_BT_EVENT_DETAIL			BIT12
#define		IOCTL_BT_EVENT_PERIODICAL		BIT13
// section 3 : BT tx/rx data and throughput
#define		IOCTL_BT_TX_ACLDATA			BIT16
#define		IOCTL_BT_TX_ACLDATA_DETAIL	BIT17
#define		IOCTL_BT_RX_ACLDATA			BIT18
#define		IOCTL_BT_RX_ACLDATA_DETAIL	BIT19
#define		IOCTL_BT_TP					BIT20
// section 4 : BT connection state machine.
#define 		IOCTL_STATE					BIT21	
#define		IOCTL_BT_LOGO					BIT22
// section 5 : BT function trace
#define		IOCTL_CALLBACK_FUN			BIT24
#define		IOCTL_PARSE_BT_PKT			BIT25
#define		IOCTL_BT_TX_PKT				BIT26
#define		IOCTL_BT_FLAG_MON				BIT27

//
// Define init check for module bit		--> FSHORT_CUT
// 2011/07/20 MH Add for short but definition.
//
#define		SHCUT_TX				BIT0
#define		SHCUT_RX				BIT1


/* 2007/07/13 MH  *//*------For DeBuG Print modeue------*/
/*------------------------------Define structure----------------------------*/


/*------------------------Export Marco Definition---------------------------*/
#if (DM_ODM_SUPPORT_TYPE != ODM_MP)
#define RT_PRINTK(fmt, args...)    printk( "%s(): " fmt, __FUNCTION__, ## args);

#if DBG
#define ODM_RT_TRACE(pDM_Odm,comp, level, fmt)											\
		if(((comp) & GlobalDebugComponents) && (level <= GlobalDebugLevel))	\
		{																	\
			RT_PRINTK fmt;													\
		}

#define RT_TRACE_F(comp, level, fmt)											\
		if(((comp) & GlobalDebugComponents) && (level <= GlobalDebugLevel))	\
		{																	\
			RT_PRINTK fmt;													\
		}

#define RT_ASSERT(expr,fmt)													\
		if(!(expr)) {															\
			printk( "Assertion failed! %s at ......\n", #expr);							\
			printk( "      ......%s,%s,line=%d\n",__FILE__,__FUNCTION__,__LINE__);	\
		}
#define dbg_enter() { printk("==> %s\n", __FUNCTION__); }
#define dbg_exit() { printk("<== %s\n", __FUNCTION__); }
#define dbg_trace(str) { printk("%s:%s\n", __FUNCTION__, str); }
#else
#define ODM_RT_TRACE(pDM_Odm,comp, level, fmt)
#define RT_TRACE_F(comp, level, fmt)
#define RT_ASSERT(expr, fmt)
#define dbg_enter()
#define dbg_exit()
#define dbg_trace(str)
#endif

#if DBG
#define DbgPrint printk

#define PRINT_DATA(_TitleString, _HexData, _HexDataLen)								\
			{																		\
				char			*szTitle = _TitleString;									\
				pu1Byte		pbtHexData = _HexData;									\
				u4Byte		u4bHexDataLen = _HexDataLen;							\
				u4Byte		__i;														\
				DbgPrint("%s", szTitle);													\
				for (__i=0;__i<u4bHexDataLen;__i++)									\
				{																	\
					if ((__i & 15) == 0) 												\
					{																\
						DbgPrint("\n");												\
					}																\
					DbgPrint("%02X%s", pbtHexData[__i], ( ((__i&3)==3) ? "  " : " ") );		\
				}																	\
				DbgPrint("\n");														\
			}

// RT_PRINT_XXX macros: implemented for debugging purpose.
// Added by Annie, 2005-11-21.
#define RT_PRINT_DATA(_Comp, _Level, _TitleString, _HexData, _HexDataLen)			\
			if(((_Comp) & GlobalDebugComponents) && (_Level <= GlobalDebugLevel))	\
			{																		\
				int __i;																\
				pu1Byte	ptr = (pu1Byte)_HexData;										\
				DbgPrint("Rtl819x: ");													\
				DbgPrint(_TitleString);												\
				for( __i=0; __i<(int)_HexDataLen; __i++ )								\
				{																	\
					DbgPrint("%02X%s", ptr[__i], (((__i + 1) % 4) == 0)?"  ":" ");			\
					if (((__i + 1) % 16) == 0)	DbgPrint("\n");							\
				}																	\
				DbgPrint("\n");														\
			}

#define RT_PRINT_ADDR(_Comp, _Level, _TitleString, _Ptr)								\
			if(((_Comp) & GlobalDebugComponents) && (_Level <= GlobalDebugLevel))	\
			{																		\
				int __i;																\
				pu1Byte	ptr = (pu1Byte)_Ptr;											\
				DbgPrint("Rtl819x: ");													\
				DbgPrint(_TitleString);												\
				DbgPrint(" ");															\
				for( __i=0; __i<6; __i++ )												\
					DbgPrint("%02X%s", ptr[__i], (__i==5)?"":"-");							\
				DbgPrint("\n");														\
			}

#define RT_PRINT_ADDRS(_Comp, _Level, _TitleString, _Ptr, _AddNum)					\
			if(((_Comp) & GlobalDebugComponents) && (_Level <= GlobalDebugLevel))	\
			{																		\
				int __i, __j;															\
				pu1Byte	ptr = (pu1Byte)_Ptr;											\
				DbgPrint("Rtl819x: ");													\
				DbgPrint(_TitleString);												\
				DbgPrint("\n");														\
				for( __i=0; __i<(int)_AddNum; __i++ )									\
				{																	\
					for( __j=0; __j<6; __j++ )											\
						DbgPrint("%02X%s", ptr[__i*6+__j], (__j==5)?"":"-");				\
					DbgPrint("\n");													\
				}																	\
			}

// Added by Annie, 2005-11-22.
#define	MAX_STR_LEN	64
#define	PRINTABLE(_ch)	(_ch>=' ' &&_ch<='~' )	// I want to see ASCII 33 to 126 only. Otherwise, I print '?'. Annie, 2005-11-22.

#define RT_PRINT_STR(_Comp, _Level, _TitleString, _Ptr, _Len)							\
			if(((_Comp) & GlobalDebugComponents) && (_Level <= GlobalDebugLevel))	\
			{																		\
				int		__i;															\
				u1Byte	buffer[MAX_STR_LEN];											\
				int	length = (_Len<MAX_STR_LEN)? _Len : (MAX_STR_LEN-1) ;				\
				PlatformZeroMemory( buffer, MAX_STR_LEN );							\
				PlatformMoveMemory( buffer, (pu1Byte)_Ptr, length );						\
				for( __i=0; __i<MAX_STR_LEN; __i++ )									\
				{																	\
					if( !PRINTABLE(buffer[__i]) )	buffer[__i] = '?';						\
				}																	\
				buffer[length] = '\0';													\
				DbgPrint("Rtl819x: ");													\
				DbgPrint(_TitleString);												\
				DbgPrint(": %d, <%s>\n", _Len, buffer);									\
			}
			
#else	// of #if DBG
#define DbgPrint(...)	
#define PRINT_DATA(_TitleString, _HexData, _HexDataLen)
#define RT_PRINT_DATA(_Comp, _Level, _TitleString, _HexData, _HexDataLen)
#define RT_PRINT_ADDR(_Comp, _Level, _TitleString, _Ptr)
#define RT_PRINT_ADDRS(_Comp, _Level, _TitleString, _Ptr, _AddNum)
#define RT_PRINT_STR(_Comp, _Level, _TitleString, _Ptr, _Len)
#endif	// of #if DBG



#endif	// #if (DM_ODM_SUPPORT_TYPE != ODM_MP)

#define		DEBUG_PRINT				1

// Please add new OS's print API by yourself

//#if (RT_PLATFORM==PLATFORM_WINDOWS) 
#if (DEBUG_PRINT == 1) && DBG
#define	RTPRINT(dbgtype, dbgflag, printstr)\
{\
	if (DBGP_Type[dbgtype] & dbgflag)\
	{\
		DbgPrint printstr;\
	}\
}

#define	RTPRINT_ADDR(dbgtype, dbgflag, printstr, _Ptr)\
{\
	if (DBGP_Type[dbgtype] & dbgflag)\
	{\
				int __i;						\
				pu1Byte	ptr = (pu1Byte)_Ptr;	\
				DbgPrint printstr;				\
				DbgPrint(" ");					\
				for( __i=0; __i<6; __i++ )		\
					DbgPrint("%02X%s", ptr[__i], (__i==5)?"":"-");		\
				DbgPrint("\n");							\
	}\
}

#define RTPRINT_DATA(dbgtype, dbgflag, _TitleString, _HexData, _HexDataLen)\
{\
	if (DBGP_Type[dbgtype] & dbgflag)\
	{\
		int __i;									\
		pu1Byte	ptr = (pu1Byte)_HexData;			\
		DbgPrint(_TitleString);					\
		for( __i=0; __i<(int)_HexDataLen; __i++ )	\
		{										\
			DbgPrint("%02X%s", ptr[__i], (((__i + 1) % 4) == 0)?"  ":" ");\
			if (((__i + 1) % 16) == 0)	DbgPrint("\n");\
		}										\
		DbgPrint("\n");							\
	}\
}
#define FuncEntry        FunctionIn(COMP_FUNC)
#define FuncExit          FunctionOut(COMP_FUNC)

#define FunctionIn(_comp)		ODM_RT_TRACE(pDM_Odm,(_comp), DBG_LOUD, ("==========> %s\n",  __FUNCTION__))
#define FunctionOut(_comp)		ODM_RT_TRACE(pDM_Odm,(_comp), DBG_LOUD, ("<========== %s\n",  __FUNCTION__))


#else

#define	DBGP(dbgtype, dbgflag, printstr)
#define	RTPRINT(dbgtype, dbgflag, printstr)
#define	RTPRINT_ADDR(dbgtype, dbgflag, printstr, _Ptr)
#define RTPRINT_DATA(dbgtype, dbgflag, _TitleString, _HexData, _HexDataLen)
#define FuncEntry       
#define FuncExit          
#define FunctionIn(_comp)
#define FunctionOut(_comp)
#endif
/*------------------------Export Marco Definition---------------------------*/


/*------------------------Export global variable----------------------------*/
extern	u4Byte			DBGP_Type[DBGP_TYPE_MAX];
extern	DBGP_HEAD_T	DBGP_Head;

/*------------------------Export global variable----------------------------*/


/*--------------------------Exported Function prototype---------------------*/
extern	void	DBGP_Flag_Init(void);
extern	void	DBG_PrintAllFlag(void);
extern	void	DBG_PrintAllComp(void);
extern	void	DBG_PrintFlagEvent(u1Byte	DbgFlag);
extern	void	DBG_DumpMem(const u1Byte DbgComp, 
							const u1Byte DbgLevel, 
							pu1Byte pMem, 
							u2Byte Len);

/*--------------------------Exported Function prototype---------------------*/









extern u4Byte GlobalDebugLevel;
extern u8Byte GlobalDebugComponents;


#endif


#endif	// __ODM_DBG_H__


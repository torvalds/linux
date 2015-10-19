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


#ifndef	__PHYDMPREDEFINE_H__
#define    __PHYDMPREDEFINE_H__

//1 ============================================================
//1  Definition 
//1 ============================================================

//Max path of IC
#define MAX_PATH_NUM_92CS		2
#define MAX_PATH_NUM_8188E		1
#define MAX_PATH_NUM_8192E		2
#define MAX_PATH_NUM_8723B		1
#define MAX_PATH_NUM_8812A		2
#define MAX_PATH_NUM_8821A		1
#define MAX_PATH_NUM_8814A		4
#define MAX_PATH_NUM_8822B		2
#define MAX_PATH_NUM_8821B		2

//Max RF path
#define ODM_RF_PATH_MAX 2
#define ODM_RF_PATH_MAX_JAGUAR 4

//number of entry
#if(DM_ODM_SUPPORT_TYPE & (ODM_CE))
	#define	ASSOCIATE_ENTRY_NUM					MACID_NUM_SW_LIMIT  /* Max size of AsocEntry[].*/
	#define	ODM_ASSOCIATE_ENTRY_NUM				ASSOCIATE_ENTRY_NUM
#elif(DM_ODM_SUPPORT_TYPE & (ODM_AP|ODM_ADSL))
	#define ASSOCIATE_ENTRY_NUM					NUM_STAT
	#define	ODM_ASSOCIATE_ENTRY_NUM				ASSOCIATE_ENTRY_NUM+1
#else
	#define ODM_ASSOCIATE_ENTRY_NUM				(ASSOCIATE_ENTRY_NUM*3)+1// Default port only one // 0 is for STA 1-n is for AP clients.
#endif

/* -----MGN rate--------------------------------- */

#define	ODM_MGN_1M			0x02
#define	ODM_MGN_2M			0x04
#define	ODM_MGN_5_5M			0x0b
#define	ODM_MGN_11M			0x16

#define	ODM_MGN_6M			0x0c
#define	ODM_MGN_9M			0x12
#define	ODM_MGN_12M			0x18
#define	ODM_MGN_18M			0x24
#define	ODM_MGN_24M			0x30
#define	ODM_MGN_36M			0x48
#define	ODM_MGN_48M			0x60
#define	ODM_MGN_54M			0x6c

/*TxHT = 1*/
#define	ODM_MGN_MCS0			0x80
#define	ODM_MGN_MCS1			0x81
#define	ODM_MGN_MCS2			0x82
#define	ODM_MGN_MCS3			0x83
#define	ODM_MGN_MCS4			0x84
#define	ODM_MGN_MCS5			0x85
#define	ODM_MGN_MCS6			0x86
#define	ODM_MGN_MCS7			0x87
#define	ODM_MGN_MCS8			0x88
#define	ODM_MGN_MCS9			0x89
#define	ODM_MGN_MCS10		0x8a
#define	ODM_MGN_MCS11		0x8b
#define	ODM_MGN_MCS12		0x8c
#define	ODM_MGN_MCS13		0x8d
#define	ODM_MGN_MCS14		0x8e
#define	ODM_MGN_MCS15		0x8f
#define	ODM_MGN_VHT1SS_MCS0	0x90
#define	ODM_MGN_VHT1SS_MCS1	0x91
#define	ODM_MGN_VHT1SS_MCS2	0x92
#define	ODM_MGN_VHT1SS_MCS3	0x93
#define	ODM_MGN_VHT1SS_MCS4	0x94
#define	ODM_MGN_VHT1SS_MCS5	0x95
#define	ODM_MGN_VHT1SS_MCS6	0x96
#define	ODM_MGN_VHT1SS_MCS7	0x97
#define	ODM_MGN_VHT1SS_MCS8	0x98
#define	ODM_MGN_VHT1SS_MCS9	0x99
#define	ODM_MGN_VHT2SS_MCS0	0x9a
#define	ODM_MGN_VHT2SS_MCS1	0x9b
#define	ODM_MGN_VHT2SS_MCS2	0x9c
#define	ODM_MGN_VHT2SS_MCS3	0x9d
#define	ODM_MGN_VHT2SS_MCS4	0x9e
#define	ODM_MGN_VHT2SS_MCS5	0x9f
#define	ODM_MGN_VHT2SS_MCS6	0xa0
#define	ODM_MGN_VHT2SS_MCS7	0xa1
#define	ODM_MGN_VHT2SS_MCS8	0xa2
#define	ODM_MGN_VHT2SS_MCS9	0xa3

#define	ODM_MGN_MCS0_SG		0xc0
#define	ODM_MGN_MCS1_SG		0xc1
#define	ODM_MGN_MCS2_SG		0xc2
#define	ODM_MGN_MCS3_SG		0xc3
#define	ODM_MGN_MCS4_SG		0xc4
#define	ODM_MGN_MCS5_SG		0xc5
#define	ODM_MGN_MCS6_SG		0xc6
#define	ODM_MGN_MCS7_SG		0xc7
#define	ODM_MGN_MCS8_SG		0xc8
#define	ODM_MGN_MCS9_SG		0xc9
#define	ODM_MGN_MCS10_SG		0xca
#define	ODM_MGN_MCS11_SG		0xcb
#define	ODM_MGN_MCS12_SG		0xcc
#define	ODM_MGN_MCS13_SG		0xcd
#define	ODM_MGN_MCS14_SG		0xce
#define	ODM_MGN_MCS15_SG		0xcf

/* -----DESC rate--------------------------------- */

#define ODM_RATEMCS15_SG		0x1c
#define ODM_RATEMCS32			0x20


// CCK Rates, TxHT = 0
#define ODM_RATE1M				0x00
#define ODM_RATE2M				0x01
#define ODM_RATE5_5M			0x02
#define ODM_RATE11M				0x03
// OFDM Rates, TxHT = 0
#define ODM_RATE6M				0x04
#define ODM_RATE9M				0x05
#define ODM_RATE12M				0x06
#define ODM_RATE18M				0x07
#define ODM_RATE24M				0x08
#define ODM_RATE36M				0x09
#define ODM_RATE48M				0x0A
#define ODM_RATE54M				0x0B
// MCS Rates, TxHT = 1
#define ODM_RATEMCS0			0x0C
#define ODM_RATEMCS1			0x0D
#define ODM_RATEMCS2			0x0E
#define ODM_RATEMCS3			0x0F
#define ODM_RATEMCS4			0x10
#define ODM_RATEMCS5			0x11
#define ODM_RATEMCS6			0x12
#define ODM_RATEMCS7			0x13
#define ODM_RATEMCS8			0x14
#define ODM_RATEMCS9			0x15
#define ODM_RATEMCS10			0x16
#define ODM_RATEMCS11			0x17
#define ODM_RATEMCS12			0x18
#define ODM_RATEMCS13			0x19
#define ODM_RATEMCS14			0x1A
#define ODM_RATEMCS15			0x1B
#define ODM_RATEMCS16			0x1C
#define ODM_RATEMCS17			0x1D
#define ODM_RATEMCS18			0x1E
#define ODM_RATEMCS19			0x1F
#define ODM_RATEMCS20			0x20
#define ODM_RATEMCS21			0x21
#define ODM_RATEMCS22			0x22
#define ODM_RATEMCS23			0x23
#define ODM_RATEMCS24			0x24
#define ODM_RATEMCS25			0x25
#define ODM_RATEMCS26			0x26
#define ODM_RATEMCS27			0x27
#define ODM_RATEMCS28			0x28
#define ODM_RATEMCS29			0x29
#define ODM_RATEMCS30			0x2A
#define ODM_RATEMCS31			0x2B
#define ODM_RATEVHTSS1MCS0		0x2C
#define ODM_RATEVHTSS1MCS1		0x2D
#define ODM_RATEVHTSS1MCS2		0x2E
#define ODM_RATEVHTSS1MCS3		0x2F
#define ODM_RATEVHTSS1MCS4		0x30
#define ODM_RATEVHTSS1MCS5		0x31
#define ODM_RATEVHTSS1MCS6		0x32
#define ODM_RATEVHTSS1MCS7		0x33
#define ODM_RATEVHTSS1MCS8		0x34
#define ODM_RATEVHTSS1MCS9		0x35
#define ODM_RATEVHTSS2MCS0		0x36
#define ODM_RATEVHTSS2MCS1		0x37
#define ODM_RATEVHTSS2MCS2		0x38
#define ODM_RATEVHTSS2MCS3		0x39
#define ODM_RATEVHTSS2MCS4		0x3A
#define ODM_RATEVHTSS2MCS5		0x3B
#define ODM_RATEVHTSS2MCS6		0x3C
#define ODM_RATEVHTSS2MCS7		0x3D
#define ODM_RATEVHTSS2MCS8		0x3E
#define ODM_RATEVHTSS2MCS9		0x3F
#define ODM_RATEVHTSS3MCS0		0x40
#define ODM_RATEVHTSS3MCS1		0x41
#define ODM_RATEVHTSS3MCS2		0x42
#define ODM_RATEVHTSS3MCS3		0x43
#define ODM_RATEVHTSS3MCS4		0x44
#define ODM_RATEVHTSS3MCS5		0x45
#define ODM_RATEVHTSS3MCS6		0x46
#define ODM_RATEVHTSS3MCS7		0x47
#define ODM_RATEVHTSS3MCS8		0x48
#define ODM_RATEVHTSS3MCS9		0x49
#define ODM_RATEVHTSS4MCS0		0x4A
#define ODM_RATEVHTSS4MCS1		0x4B
#define ODM_RATEVHTSS4MCS2		0x4C
#define ODM_RATEVHTSS4MCS3		0x4D
#define ODM_RATEVHTSS4MCS4		0x4E
#define ODM_RATEVHTSS4MCS5		0x4F
#define ODM_RATEVHTSS4MCS6		0x50
#define ODM_RATEVHTSS4MCS7		0x51
#define ODM_RATEVHTSS4MCS8		0x52
#define ODM_RATEVHTSS4MCS9		0x53

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	#define ODM_NUM_RATE_IDX (ODM_RATEVHTSS4MCS9+1)
#else
	#if (RTL8192E_SUPPORT == 1)
		#define ODM_NUM_RATE_IDX (ODM_RATEMCS15+1)
	#elif (RTL8723B_SUPPORT == 1)|| (RTL8188E_SUPPORT == 1) 
		#define ODM_NUM_RATE_IDX (ODM_RATEMCS7+1)
	#elif (RTL8821A_SUPPORT == 1) || (RTL8881A_SUPPORT == 1) 
		#define ODM_NUM_RATE_IDX (ODM_RATEVHTSS1MCS9+1)
	#elif (RTL8812A_SUPPORT == 1)
		#define ODM_NUM_RATE_IDX (ODM_RATEVHTSS2MCS9+1)
	#elif(RTL8814A_SUPPORT == 1)
		#define ODM_NUM_RATE_IDX (ODM_RATEVHTSS3MCS9+1)
	#else
		#define ODM_NUM_RATE_IDX (ODM_RATEVHTSS4MCS9+1)
	#endif
#endif

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
#define CONFIG_SFW_SUPPORTED
#endif

//1 ============================================================
//1  enumeration
//1 ============================================================


//	ODM_CMNINFO_INTERFACE
typedef enum tag_ODM_Support_Interface_Definition
{
	ODM_ITRF_PCIE 	=	0x1,
	ODM_ITRF_USB 	=	0x2,
	ODM_ITRF_SDIO 	=	0x4,
	ODM_ITRF_ALL 	=	0x7,
}ODM_INTERFACE_E;

// ODM_CMNINFO_IC_TYPE
typedef enum tag_ODM_Support_IC_Type_Definition
{
	ODM_RTL8192S 	=	BIT0,
	ODM_RTL8192C 	=	BIT1,
	ODM_RTL8192D 	=	BIT2,
	ODM_RTL8723A 	=	BIT3,
	ODM_RTL8188E 	=	BIT4,
	ODM_RTL8812 	=	BIT5,
	ODM_RTL8821 	=	BIT6,
	ODM_RTL8192E 	=	BIT7,	
	ODM_RTL8723B	=	BIT8,
	ODM_RTL8814A	=	BIT9,	
	ODM_RTL8881A 	=	BIT10,
	ODM_RTL8821B 	=	BIT11,
	ODM_RTL8822B 	=	BIT12,
	ODM_RTL8703B 	=	BIT13,
	ODM_RTL8195A	=	BIT14,
	ODM_RTL8188F 	=	BIT15
}ODM_IC_TYPE_E;




#define ODM_IC_11N_SERIES		(ODM_RTL8192S|ODM_RTL8192C|ODM_RTL8192D|ODM_RTL8723A|ODM_RTL8188E|ODM_RTL8192E|ODM_RTL8723B|ODM_RTL8703B|ODM_RTL8188F)
#define ODM_IC_11AC_SERIES		(ODM_RTL8812|ODM_RTL8821|ODM_RTL8814A|ODM_RTL8881A|ODM_RTL8821B|ODM_RTL8822B)
#define ODM_IC_TXBF_SUPPORT		(ODM_RTL8192E|ODM_RTL8812|ODM_RTL8821|ODM_RTL8814A|ODM_RTL8881A|ODM_RTL8822B)

#if (DM_ODM_SUPPORT_TYPE == ODM_AP)

#ifdef RTK_AC_SUPPORT
#define ODM_IC_11AC_SERIES_SUPPORT		1
#else
#define ODM_IC_11AC_SERIES_SUPPORT		0
#endif

#define ODM_IC_11N_SERIES_SUPPORT			1
#define ODM_CONFIG_BT_COEXIST				0

#elif (DM_ODM_SUPPORT_TYPE == ODM_WIN)

#define ODM_IC_11AC_SERIES_SUPPORT		1
#define ODM_IC_11N_SERIES_SUPPORT			1
#define ODM_CONFIG_BT_COEXIST				1

#else 

#if((RTL8192C_SUPPORT == 1) || (RTL8192D_SUPPORT == 1) || (RTL8723A_SUPPORT == 1) || (RTL8188E_SUPPORT == 1) ||\
(RTL8723B_SUPPORT == 1) || (RTL8192E_SUPPORT == 1) || (RTL8195A_SUPPORT == 1))
#define ODM_IC_11N_SERIES_SUPPORT			1
#define ODM_IC_11AC_SERIES_SUPPORT		0
#else
#define ODM_IC_11N_SERIES_SUPPORT			0
#define ODM_IC_11AC_SERIES_SUPPORT		1
#endif

#ifdef CONFIG_BT_COEXIST
#define ODM_CONFIG_BT_COEXIST				1
#else
#define ODM_CONFIG_BT_COEXIST				0
#endif

#endif


//ODM_CMNINFO_CUT_VER
typedef enum tag_ODM_Cut_Version_Definition
{
	ODM_CUT_A 		=	0,
	ODM_CUT_B 		=	1,
	ODM_CUT_C 		=	2,
	ODM_CUT_D 		=	3,
	ODM_CUT_E 		=	4,
	ODM_CUT_F 		=	5,

	ODM_CUT_I 		=	8,
	ODM_CUT_J 		=	9,
	ODM_CUT_K 		=	10,	
	ODM_CUT_TEST 	=	15,
}ODM_CUT_VERSION_E;

// ODM_CMNINFO_FAB_VER
typedef enum tag_ODM_Fab_Version_Definition
{
	ODM_TSMC 	=	0,
	ODM_UMC 	=	1,
}ODM_FAB_E;

// ODM_CMNINFO_RF_TYPE
//
// For example 1T2R (A+AB = BIT0|BIT4|BIT5)
//
typedef enum tag_ODM_RF_Path_Bit_Definition
{
	ODM_RF_TX_A 	=	BIT0,
	ODM_RF_TX_B 	=	BIT1,
	ODM_RF_TX_C	=	BIT2,
	ODM_RF_TX_D	=	BIT3,
	ODM_RF_RX_A	=	BIT4,
	ODM_RF_RX_B	=	BIT5,
	ODM_RF_RX_C	=	BIT6,
	ODM_RF_RX_D	=	BIT7,
}ODM_RF_PATH_E;

typedef enum tag_PHYDM_RF_TX_NUM {
	ODM_1T	=	1,
	ODM_2T	=	2,
	ODM_3T	=	3,
	ODM_4T	=	4,
} ODM_RF_TX_NUM_E;

typedef enum tag_ODM_RF_Type_Definition {
	ODM_1T1R,
	ODM_1T2R,
	ODM_2T2R,
	ODM_2T2R_GREEN,
	ODM_2T3R,
	ODM_2T4R,
	ODM_3T3R,
	ODM_3T4R,
	ODM_4T4R,
	ODM_XTXR
}ODM_RF_TYPE_E;


typedef enum tag_ODM_MAC_PHY_Mode_Definition
{
	ODM_SMSP	= 0,
	ODM_DMSP	= 1,
	ODM_DMDP	= 2,
}ODM_MAC_PHY_MODE_E;


typedef enum tag_BT_Coexist_Definition
{	
	ODM_BT_BUSY 		= 1,
	ODM_BT_ON 			= 2,
	ODM_BT_OFF 		= 3,
	ODM_BT_NONE 		= 4,
}ODM_BT_COEXIST_E;

// ODM_CMNINFO_OP_MODE
typedef enum tag_Operation_Mode_Definition
{
	ODM_NO_LINK 		= BIT0,
	ODM_LINK 			= BIT1,
	ODM_SCAN 			= BIT2,
	ODM_POWERSAVE 	= BIT3,
	ODM_AP_MODE 		= BIT4,
	ODM_CLIENT_MODE	= BIT5,
	ODM_AD_HOC 		= BIT6,
	ODM_WIFI_DIRECT	= BIT7,
	ODM_WIFI_DISPLAY	= BIT8,
}ODM_OPERATION_MODE_E;

// ODM_CMNINFO_WM_MODE
#if (DM_ODM_SUPPORT_TYPE & (ODM_CE))
typedef enum tag_Wireless_Mode_Definition
{
	ODM_WM_UNKNOW	= 0x0,
	ODM_WM_B			= BIT0,
	ODM_WM_G			= BIT1,
	ODM_WM_A			= BIT2,
	ODM_WM_N24G		= BIT3,
	ODM_WM_N5G		= BIT4,
	ODM_WM_AUTO		= BIT5,
	ODM_WM_AC		= BIT6,
}ODM_WIRELESS_MODE_E;
#else
typedef enum tag_Wireless_Mode_Definition
{
	ODM_WM_UNKNOWN	= 0x00,/*0x0*/
	ODM_WM_A			= BIT0, /* 0x1*/
	ODM_WM_B			= BIT1, /* 0x2*/
	ODM_WM_G			= BIT2,/* 0x4*/
	ODM_WM_AUTO		= BIT3,/* 0x8*/
	ODM_WM_N24G		= BIT4,/* 0x10*/
	ODM_WM_N5G		= BIT5,/* 0x20*/
	ODM_WM_AC_5G		= BIT6,/* 0x40*/
	ODM_WM_AC_24G	= BIT7,/* 0x80*/
	ODM_WM_AC_ONLY	= BIT8,/* 0x100*/
	ODM_WM_MAX		= BIT11/* 0x800*/

}ODM_WIRELESS_MODE_E;
#endif

// ODM_CMNINFO_BAND
typedef enum tag_Band_Type_Definition
{
#if (DM_ODM_SUPPORT_TYPE & (ODM_AP))
	ODM_BAND_2_4G 	= BIT0,
	ODM_BAND_5G 		= BIT1,
#else
	ODM_BAND_2_4G = 0,
	ODM_BAND_5G,
	ODM_BAND_ON_BOTH,
	ODM_BANDMAX
#endif
}ODM_BAND_TYPE_E;


// ODM_CMNINFO_SEC_CHNL_OFFSET
typedef enum tag_Secondary_Channel_Offset_Definition
{
	ODM_DONT_CARE 	= 0,
	ODM_BELOW 		= 1,
	ODM_ABOVE 			= 2
}ODM_SEC_CHNL_OFFSET_E;

// ODM_CMNINFO_SEC_MODE
typedef enum tag_Security_Definition
{
	ODM_SEC_OPEN 			= 0,
	ODM_SEC_WEP40 		= 1,
	ODM_SEC_TKIP 			= 2,
	ODM_SEC_RESERVE 		= 3,
	ODM_SEC_AESCCMP 		= 4,
	ODM_SEC_WEP104 		= 5,
	ODM_WEP_WPA_MIXED    = 6, // WEP + WPA
	ODM_SEC_SMS4 			= 7,
}ODM_SECURITY_E;

// ODM_CMNINFO_BW
typedef enum tag_Bandwidth_Definition
{	
	ODM_BW20M 		= 0,
	ODM_BW40M 		= 1,
	ODM_BW80M 		= 2,
	ODM_BW160M 		= 3,
	ODM_BW10M 		= 4,
}ODM_BW_E;

// ODM_CMNINFO_CHNL

// ODM_CMNINFO_BOARD_TYPE
typedef enum tag_Board_Definition
{
    ODM_BOARD_DEFAULT  	= 0, 	  // The DEFAULT case.
    ODM_BOARD_MINICARD  = BIT(0), // 0 = non-mini card, 1= mini card.
    ODM_BOARD_SLIM      = BIT(1), // 0 = non-slim card, 1 = slim card
    ODM_BOARD_BT        = BIT(2), // 0 = without BT card, 1 = with BT
    ODM_BOARD_EXT_PA    = BIT(3), // 0 = no 2G ext-PA, 1 = existing 2G ext-PA
    ODM_BOARD_EXT_LNA   = BIT(4), // 0 = no 2G ext-LNA, 1 = existing 2G ext-LNA
    ODM_BOARD_EXT_TRSW  = BIT(5), // 0 = no ext-TRSW, 1 = existing ext-TRSW
    ODM_BOARD_EXT_PA_5G	= BIT(6), // 0 = no 5G ext-PA, 1 = existing 5G ext-PA
    ODM_BOARD_EXT_LNA_5G= BIT(7), // 0 = no 5G ext-LNA, 1 = existing 5G ext-LNA
}ODM_BOARD_TYPE_E;

typedef enum tag_ODM_Package_Definition
{
    ODM_PACKAGE_DEFAULT  	 = 0, 	  
    ODM_PACKAGE_QFN68        = BIT(0), 
    ODM_PACKAGE_TFBGA90      = BIT(1), 
    ODM_PACKAGE_TFBGA79      = BIT(2),	
}ODM_Package_TYPE_E;

typedef enum tag_ODM_TYPE_GPA_Definition
{
    TYPE_GPA0 = 0, 	  
    TYPE_GPA1 = BIT(1)|BIT(0)
}ODM_TYPE_GPA_E;

typedef enum tag_ODM_TYPE_APA_Definition
{
    TYPE_APA0 = 0, 	  
    TYPE_APA1 = BIT(1)|BIT(0)
}ODM_TYPE_APA_E;

typedef enum tag_ODM_TYPE_GLNA_Definition
{
    TYPE_GLNA0 = 0, 	  
    TYPE_GLNA1 = BIT(2)|BIT(0),
    TYPE_GLNA2 = BIT(3)|BIT(1),
    TYPE_GLNA3 = BIT(3)|BIT(2)|BIT(1)|BIT(0)
}ODM_TYPE_GLNA_E;

typedef enum tag_ODM_TYPE_ALNA_Definition
{
    TYPE_ALNA0 = 0, 	  
    TYPE_ALNA1 = BIT(2)|BIT(0),
    TYPE_ALNA2 = BIT(3)|BIT(1),
    TYPE_ALNA3 = BIT(3)|BIT(2)|BIT(1)|BIT(0)
}ODM_TYPE_ALNA_E;


typedef enum _ODM_RF_RADIO_PATH {
    ODM_RF_PATH_A = 0,   //Radio Path A
    ODM_RF_PATH_B = 1,   //Radio Path B
    ODM_RF_PATH_C = 2,   //Radio Path C
    ODM_RF_PATH_D = 3,   //Radio Path D
    ODM_RF_PATH_AB,
    ODM_RF_PATH_AC,
    ODM_RF_PATH_AD,
    ODM_RF_PATH_BC,
    ODM_RF_PATH_BD,
    ODM_RF_PATH_CD,
    ODM_RF_PATH_ABC,
    ODM_RF_PATH_ACD,
    ODM_RF_PATH_BCD,
    ODM_RF_PATH_ABCD,
  //  ODM_RF_PATH_MAX,    //Max RF number 90 support
} ODM_RF_RADIO_PATH_E, *PODM_RF_RADIO_PATH_E;






#endif

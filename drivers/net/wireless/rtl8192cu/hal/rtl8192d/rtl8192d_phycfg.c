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
/******************************************************************************


 Module:	rtl8192d_phycfg.c	

 Note:		Merge 92DE/SDU PHY config as below
			1. BB register R/W API
 			2. RF register R/W API
 			3. Initial BB/RF/MAC config by reading BB/MAC/RF txt.
 			3. Power setting API
 			4. Channel switch API
 			5. Initial gain switch API.
 			6. Other BB/MAC/RF API.
 			
 Function:	PHY: Extern function, phy: local function
 		 
 Export:	PHY_FunctionName

 Abbrev:	NONE

 History:
	Data		Who		Remark	
	08/08/2008  MHC    	1. Port from 9x series phycfg.c
						2. Reorganize code arch and ad description.
						3. Collect similar function.
						4. Seperate extern/local API.
	08/12/2008	MHC		We must merge or move USB PHY relative function later.
	10/07/2008	MHC		Add IQ calibration for PHY.(Only 1T2R mode now!!!)
	11/06/2008	MHC		Add TX Power index PG file to config in 0xExx register
						area to map with EEPROM/EFUSE tx pwr index.
	
******************************************************************************/
#define _HAL_8192D_PHYCFG_C_

#include <drv_conf.h>
#include <osdep_service.h>
#include <drv_types.h>
#include <rtw_byteorder.h>

#include <hal_init.h>
#include <rtl8192d_hal.h>

/*---------------------------Define Local Constant---------------------------*/
/* Channel switch:The size of command tables for switch channel*/
#define MAX_PRECMD_CNT 16
#define MAX_RFDEPENDCMD_CNT 16
#define MAX_POSTCMD_CNT 16

#define MAX_DOZE_WAITING_TIMES_9x 64

#define MAX_RF_IMR_INDEX 12
#define MAX_RF_IMR_INDEX_NORMAL 13
#define RF_REG_NUM_for_C_CUT_5G 	6
#define RF_REG_NUM_for_C_CUT_5G_internalPA 	7
#define RF_REG_NUM_for_C_CUT_2G 	5
#define RF_CHNL_NUM_5G			19	
#define RF_CHNL_NUM_5G_40M		17
#define TARGET_CHNL_NUM_5G	221
#define TARGET_CHNL_NUM_2G	14
#define TARGET_CHNL_NUM_2G_5G	59
#define CV_CURVE_CNT			64

//static u32     RF_REG_FOR_5G_SWCHNL[MAX_RF_IMR_INDEX]={0,0x2f,0x30,0x31,0x32,0x33,0x34,0x35,0x36,0x38,0x39,0x0};
static u32     RF_REG_FOR_5G_SWCHNL_NORMAL[MAX_RF_IMR_INDEX_NORMAL]={0,0x2f,0x30,0x31,0x32,0x33,0x34,0x35,0x36,0x37,0x38,0x39,0x0};

static u8	RF_REG_for_C_CUT_5G[RF_REG_NUM_for_C_CUT_5G] = 
			{RF_SYN_G1,	RF_SYN_G2,	RF_SYN_G3,	RF_SYN_G4,	RF_SYN_G5,	RF_SYN_G6};

static u8	RF_REG_for_C_CUT_5G_internalPA[RF_REG_NUM_for_C_CUT_5G_internalPA] = 
			{0x0B,	0x48,	0x49,	0x4B,	0x03,	0x04, 	0x0E};
static u8	RF_REG_for_C_CUT_2G[RF_REG_NUM_for_C_CUT_2G] = 
			{RF_SYN_G1, RF_SYN_G2,	RF_SYN_G3,	RF_SYN_G7,	RF_SYN_G8};

#if DBG
static u32	RF_REG_MASK_for_C_CUT_2G[RF_REG_NUM_for_C_CUT_2G] = 
			{BIT19|BIT18|BIT17|BIT14|BIT1, 	BIT10|BIT9,	
			BIT18|BIT17|BIT16|BIT1,		BIT2|BIT1,	
			BIT15|BIT14|BIT13|BIT12|BIT11};
#endif  //amy, temp remove
static u8	RF_CHNL_5G[RF_CHNL_NUM_5G] = 
			{36,40,44,48,52,56,60,64,100,104,108,112,116,120,124,128,132,136,140};
static u8	RF_CHNL_5G_40M[RF_CHNL_NUM_5G_40M] = 
			{38,42,46,50,54,58,62,102,106,110,114,118,122,126,130,134,138};

static u32	RF_REG_Param_for_C_CUT_5G[5][RF_REG_NUM_for_C_CUT_5G] = {
			{0xE43BE,	0xFC638,	0x77C0A,	0xDE471,	0xd7110,	0x8EB04},
			{0xE43BE,	0xFC078,	0xF7C1A,	0xE0C71,	0xD7550,	0xAEB04},	
			{0xE43BF,	0xFF038,	0xF7C0A,	0xDE471,	0xE5550,	0xAEB04},
			{0xE43BF,	0xFF079,	0xF7C1A,	0xDE471,	0xE5550,	0xAEB04},
			{0xE43BF,	0xFF038,	0xF7C1A,	0xDE471,	0xd7550,	0xAEB04}};

static u32	RF_REG_Param_for_C_CUT_2G[3][RF_REG_NUM_for_C_CUT_2G] = {
			{0x643BC,	0xFC038,	0x77C1A,	0x41289,	0x01840},
			{0x643BC,	0xFC038,	0x07C1A,	0x41289,	0x01840},
			{0x243BC,	0xFC438,	0x07C1A,	0x4128B,	0x0FC41}};

#if SWLCK == 1
static u32 RF_REG_SYN_G4_for_C_CUT_2G = 0xD1C31&0x7FF;
#endif

static u32	RF_REG_Param_for_C_CUT_5G_internalPA[3][RF_REG_NUM_for_C_CUT_5G_internalPA] = {
			{0x01a00,	0x40443,	0x00eb5,	0x89bec,	0x94a12,	0x94a12,	0x94a12},
			{0x01800,	0xc0443,	0x00730,	0x896ee,	0x94a52,	0x94a52,	0x94a52},	
			{0x01800,	0xc0443,	0x00730,	0x896ee,	0x94a12,	0x94a12,	0x94a12}};



//[mode][patha+b][reg]
static u32 RF_IMR_Param_Normal[1][3][MAX_RF_IMR_INDEX_NORMAL]={{
	{0x70000,0x00ff0,0x4400f,0x00ff0,0x0,0x0,0x0,0x0,0x0,0x64888,0xe266c,0x00090,0x22fff},// channel 1-14.
	{0x70000,0x22880,0x4470f,0x55880,0x00070, 0x88000, 0x0,0x88080,0x70000,0x64a82,0xe466c,0x00090,0x32c9a}, //path 36-64
	{0x70000,0x44880,0x4477f,0x77880,0x00070, 0x88000, 0x0,0x880b0,0x0,0x64b82,0xe466c,0x00090,0x32c9a} //100 -165
}
};

//static u32 CurveIndex_5G[TARGET_CHNL_NUM_5G]={0};
//static u32 CurveIndex_2G[TARGET_CHNL_NUM_2G]={0};
static u32 CurveIndex[TARGET_CHNL_NUM_2G_5G]={0};

static u32 TargetChnl_5G[TARGET_CHNL_NUM_5G] = {
25141,	25116,	25091,	25066,	25041,
25016,	24991,	24966,	24941,	24917,
24892,	24867,	24843,	24818,	24794,
24770,	24765,	24721,	24697,	24672,
24648,	24624,	24600,	24576,	24552,
24528,	24504,	24480,	24457,	24433,
24409,	24385,	24362,	24338,	24315,
24291,	24268,	24245,	24221,	24198,
24175,	24151,	24128,	24105,	24082,
24059,	24036,	24013,	23990,	23967,
23945,	23922,	23899,	23876,	23854,
23831,	23809,	23786,	23764,	23741,
23719,	23697,	23674,	23652,	23630,
23608,	23586,	23564,	23541,	23519,
23498,	23476,	23454,	23432,	23410,
23388,	23367,	23345,	23323,	23302,
23280,	23259,	23237,	23216,	23194,
23173,	23152,	23130,	23109,	23088,
23067,	23046,	23025,	23003,	22982,
22962,	22941,	22920,	22899,	22878,
22857,	22837,	22816,	22795,	22775,
22754,	22733,	22713,	22692,	22672,
22652,	22631,	22611,	22591,	22570,
22550,	22530,	22510,	22490,	22469,
22449,	22429,	22409,	22390,	22370,
22350,	22336,	22310,	22290,	22271,
22251,	22231,	22212,	22192,	22173,
22153,	22134,	22114,	22095,	22075,
22056,	22037,	22017,	21998,	21979,
21960,	21941,	21921,	21902,	21883,
21864,	21845,	21826,	21807,	21789,
21770,	21751,	21732,	21713,	21695,
21676,	21657,	21639,	21620,	21602,
21583,	21565,	21546,	21528,	21509,
21491,	21473,	21454,	21436,	21418,
21400,	21381,	21363,	21345,	21327,
21309,	21291,	21273,	21255,	21237,
21219,	21201,	21183,	21166,	21148,
21130,	21112,	21095,	21077,	21059,
21042,	21024,	21007,	20989,	20972,
25679,	25653,	25627,	25601,	25575,
25549,	25523,	25497,	25471,	25446,
25420,	25394,	25369,	25343,	25318,
25292,	25267,	25242,	25216,	25191,
25166	};

static u32 TargetChnl_2G[TARGET_CHNL_NUM_2G] = {	// channel 1~14
26084, 26030, 25976, 25923, 25869, 25816, 25764,
25711, 25658, 25606, 25554, 25502, 25451, 25328
};

/*---------------------------Define Local Constant---------------------------*/


/*------------------------Define global variable-----------------------------*/
#ifdef CONFIG_DUALMAC_CONCURRENT
extern atomic_t GlobalCounterForMutex;
#endif
/*------------------------Define local variable------------------------------*/


/*--------------------Define export function prototype-----------------------*/
// Please refer to header file
/*--------------------Define export function prototype-----------------------*/

/*---------------------Define local function prototype-----------------------*/
static VOID
phy_PathAFillIQKMatrix(
	IN PADAPTER	pAdapter,
	IN BOOLEAN	bIQKOK,
	IN int		result[][8],
	IN u8		final_candidate,
	IN BOOLEAN	bTxOnly
	);

static VOID
phy_PathAFillIQKMatrix_5G_Normal(
	IN PADAPTER	pAdapter,
	IN BOOLEAN	bIQKOK,
	IN int		result[][8],
	IN u8		final_candidate,
	IN BOOLEAN	bTxOnly
	);

static VOID
phy_PathBFillIQKMatrix(
	IN PADAPTER	pAdapter,
	IN BOOLEAN	bIQKOK,
	IN int		result[][8],
	IN u8		final_candidate,
	IN BOOLEAN	bTxOnly
	);

static VOID
phy_PathBFillIQKMatrix_5G_Normal(
	IN PADAPTER	pAdapter,
	IN BOOLEAN	bIQKOK,
	IN int		result[][8],
	IN u8		final_candidate,
	IN BOOLEAN	bTxOnly
	);
/*----------------------------Function Body----------------------------------*/

static u8 GetRightChnlPlace(u8 chnl)
{
	u8	channel_5G[TARGET_CHNL_NUM_2G_5G] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,36,38,40,42,44,46,48,50,52,54,56,58,60,62,64,100,102,104,106,108,110,112,114,116,118,120,122,124,126,128,130,132,134,136,138,140,149,151,153,155,157,159,161,163,165};
	u8	place = chnl;

	if(chnl > 14)
	{
		for(place = 14; place<sizeof(channel_5G); place++)
		{
			if(channel_5G[place] == chnl)
			{
				place++;
				break;
			}
		}
	}

	return place;
}

static u8 GetChnlFromPlace(u8 place)
{
	u8	channel_5G[TARGET_CHNL_NUM_2G_5G] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,36,38,40,42,44,46,48,50,52,54,56,58,60,62,64,100,102,104,106,108,110,112,114,116,118,120,122,124,126,128,130,132,134,136,138,140,149,151,153,155,157,159,161,163,165};

	return channel_5G[place];
}

u8 rtl8192d_GetRightChnlPlaceforIQK(u8 chnl)
{
	u8	channel_all[TARGET_CHNL_NUM_2G_5G] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,36,38,40,42,44,46,48,50,52,54,56,58,60,62,64,100,102,104,106,108,110,112,114,116,118,120,122,124,126,128,130,132,134,136,138,140,149,151,153,155,157,159,161,163,165};
	u8	place = chnl;

	
	if(chnl > 14)
	{
		for(place = 14; place<sizeof(channel_all); place++)
		{
			if(channel_all[place] == chnl)
			{
				return place-13;
			}
		}
	}

	return 0;
}


//
// 1. BB register R/W API
//
/**
* Function:	phy_CalculateBitShift
*
* OverView:	Get shifted position of the BitMask
*
* Input:
*			u4Byte		BitMask,	
*
* Output:	none
* Return:		u4Byte		Return the shift bit bit position of the mask
*/
static	u32
phy_CalculateBitShift(
	u32 BitMask
	)
{
	u32 i;

	for(i=0; i<=31; i++)
	{
		if ( ((BitMask>>i) &  0x1 ) == 1)
			break;
	}

	return (i);
}

//
//To avoid miswrite Reg0x800 for 92D
//
VOID
rtl8192d_PHY_SetBBReg1Byte(
	IN	PADAPTER	Adapter,
	IN	u32		RegAddr,
	IN	u32		BitMask,
	IN	u32		Data
	)
{
	u32			OriginalValue, BitShift,offset = 0;
       u8   			value=0;
	   
#if (DISABLE_BB_RF == 1)
	return;
#endif
	// BitMask only support bit0~bit7 or bit8~bit15,bit16~bit23,bit24~bit31,should in 1 byte scale;
	BitShift = phy_CalculateBitShift(BitMask);
	offset = BitShift /8;

	OriginalValue = rtw_read32(Adapter, RegAddr);
	Data = ((OriginalValue & (~BitMask)) | ((Data << BitShift) & BitMask));

	value =(u8)(Data>>(8*offset));

	rtw_write8(Adapter, RegAddr+offset, value);
	//RT_TRACE(COMP_INIT,DBG_TRACE,("Write Reg0x800 originalvalue %x  to set 1byte value %x Data %x offset %x \n",OriginalValue,value,Data,offset));

}

/**
* Function:	PHY_QueryBBReg
*
* OverView:	Read "sepcific bits" from BB register
*
* Input:
*			PADAPTER		Adapter,
*			u4Byte			RegAddr,		//The target address to be readback
*			u4Byte			BitMask		//The target bit position in the target address
*										//to be readback	
* Output:	None
* Return:		u4Byte			Data			//The readback register value
* Note:		This function is equal to "GetRegSetting" in PHY programming guide
*/
u32
rtl8192d_PHY_QueryBBReg(
	IN	PADAPTER	Adapter,
	IN	u32		RegAddr,
	IN	u32		BitMask
	)
{
	#ifdef CONFIG_PCI_HCI
	u8	DBIdirect = 0;
	#endif //CONFIG_PCI_HCI
  	u32	ReturnValue = 0, OriginalValue, BitShift;


#if (DISABLE_BB_RF == 1)
	return 0;
#endif

	//RT_TRACE(COMP_RF, DBG_TRACE, ("--->PHY_QueryBBReg(): RegAddr(%#lx), BitMask(%#lx)\n", RegAddr, BitMask));

#ifdef CONFIG_PCI_HCI
	if(RegAddr&MAC1_ACCESS_PHY0) //MAC1 use PHY0 wirte radio_A.
		DBIdirect = BIT3;
	else if(RegAddr&MAC0_ACCESS_PHY1) //MAC0 use PHY1 wirte radio_B.
		DBIdirect = BIT3|BIT2;

	if (DBIdirect)
		OriginalValue = MpReadPCIDwordDBI8192D(Adapter, (u16)RegAddr&0xFFF, DBIdirect);  
	else
#endif
	{
		OriginalValue = rtw_read32(Adapter, RegAddr);
	}
	BitShift = phy_CalculateBitShift(BitMask);
	ReturnValue = (OriginalValue & BitMask) >> BitShift;

	//RTPRINT(FPHY, PHY_BBR, ("BBR MASK=0x%lx Addr[0x%lx]=0x%lx\n", BitMask, RegAddr, OriginalValue));
	//RT_TRACE(COMP_RF, DBG_TRACE, ("<---PHY_QueryBBReg(): RegAddr(%#lx), BitMask(%#lx), OriginalValue(%#lx)\n", RegAddr, BitMask, OriginalValue));

	return (ReturnValue);

}


/**
* Function:	PHY_SetBBReg
*
* OverView:	Write "Specific bits" to BB register (page 8~) 
*
* Input:
*			PADAPTER		Adapter,
*			u4Byte			RegAddr,		//The target address to be modified
*			u4Byte			BitMask		//The target bit position in the target address
*										//to be modified	
*			u4Byte			Data			//The new register value in the target bit position
*										//of the target address			
*
* Output:	None
* Return:		None
* Note:		This function is equal to "PutRegSetting" in PHY programming guide
*/

VOID
rtl8192d_PHY_SetBBReg(
	IN	PADAPTER	Adapter,
	IN	u32		RegAddr,
	IN	u32		BitMask,
	IN	u32		Data
	)
{
#ifdef CONFIG_PCI_HCI
	u8	DBIdirect=0;
#endif //CONFIG_PCI_HCI
	u32	OriginalValue, BitShift;

#if (DISABLE_BB_RF == 1)
	return;
#endif

#ifdef CONFIG_PCI_HCI
	if(RegAddr&MAC1_ACCESS_PHY0) //MAC1 use PHY0 wirte radio_A.
		DBIdirect = BIT3;
	else if(RegAddr&MAC0_ACCESS_PHY1) //MAC0 use PHY1 wirte radio_B.
		DBIdirect = BIT3|BIT2;
#endif

	//RT_TRACE(COMP_RF, DBG_TRACE, ("--->PHY_SetBBReg(): RegAddr(%#lx), BitMask(%#lx), Data(%#lx)\n", RegAddr, BitMask, Data));

	if(BitMask!= bMaskDWord)
	{//if not "double word" write
#ifdef CONFIG_PCI_HCI
		if (DBIdirect)
		{
			OriginalValue = MpReadPCIDwordDBI8192D(Adapter, (u16)RegAddr&0xFFF, DBIdirect);  
		}
		else
#endif
		{
			OriginalValue = rtw_read32(Adapter, RegAddr);
		}
		BitShift = phy_CalculateBitShift(BitMask);
		Data = ((OriginalValue & (~BitMask)) | ((Data << BitShift) & BitMask));
	}

#ifdef CONFIG_PCI_HCI
	if (DBIdirect)
	{
		MpWritePCIDwordDBI8192D(Adapter, 
					(u16)RegAddr&0xFFF, 
					Data,
					DBIdirect);
	}
	else
#endif
	{
		rtw_write32(Adapter, RegAddr, Data);
	}

	//RTPRINT(FPHY, PHY_BBW, ("BBW MASK=0x%lx Addr[0x%lx]=0x%lx\n", BitMask, RegAddr, Data));
	//RT_TRACE(COMP_RF, DBG_TRACE, ("<---PHY_SetBBReg(): RegAddr(%#lx), BitMask(%#lx), Data(%#lx)\n", RegAddr, BitMask, Data));
	
}


//
// 2. RF register R/W API
//
/*-----------------------------------------------------------------------------
 * Function:	phy_FwRFSerialRead()
 *
 * Overview:	We support firmware to execute RF-R/W.
 *
 * Input:		NONE
 *
 * Output:		NONE
 *
 * Return:		NONE
 *
 * Revised History:
 *	When		Who		Remark
 *	01/21/2008	MHC		Create Version 0.  
 *
 *---------------------------------------------------------------------------*/
#ifndef PLATFORM_FREEBSD   //amy, temp remove
static	u32
phy_FwRFSerialRead(
	IN	PADAPTER			Adapter,
	IN	RF_RADIO_PATH_E	eRFPath,
	IN	u32				Offset	)
{
	u32		retValue = 0;		
	//RT_ASSERT(FALSE,("deprecate!\n"));
	return	(retValue);

}	/* phy_FwRFSerialRead */


/*-----------------------------------------------------------------------------
 * Function:	phy_FwRFSerialWrite()
 *
 * Overview:	We support firmware to execute RF-R/W.
 *
 * Input:		NONE
 *
 * Output:		NONE
 *
 * Return:		NONE
 *
 * Revised History:
 *	When		Who		Remark
 *	01/21/2008	MHC		Create Version 0.  
 *
 *---------------------------------------------------------------------------*/
static	VOID
phy_FwRFSerialWrite(
	IN	PADAPTER			Adapter,
	IN	RF_RADIO_PATH_E	eRFPath,
	IN	u32				Offset,
	IN	u32				Data	)
{
	//RT_ASSERT(FALSE,("deprecate!\n"));
}
#endif //PLATFORM_FREEBSD amy, temp remove

/**
* Function:	phy_RFSerialRead
*
* OverView:	Read regster from RF chips 
*
* Input:
*			PADAPTER		Adapter,
*			RF_RADIO_PATH_E	eRFPath,	//Radio path of A/B/C/D
*			u4Byte			Offset,		//The target address to be read			
*
* Output:	None
* Return:		u4Byte			reback value
* Note:		Threre are three types of serial operations: 
*			1. Software serial write
*			2. Hardware LSSI-Low Speed Serial Interface 
*			3. Hardware HSSI-High speed
*			serial write. Driver need to implement (1) and (2).
*			This function is equal to the combination of RF_ReadReg() and  RFLSSIRead()
*/
static	u32
phy_RFSerialRead(
	IN	PADAPTER			Adapter,
	IN	RF_RADIO_PATH_E	eRFPath,
	IN	u32				Offset
	)
{
	u32	retValue = 0;
	HAL_DATA_TYPE				*pHalData = GET_HAL_DATA(Adapter);
	BB_REGISTER_DEFINITION_T	*pPhyReg = &pHalData->PHYRegDef[eRFPath];
	u32	NewOffset;
	u32 	tmplong,tmplong2;
	u8	RfPiEnable=0;
	u8	i;
	u32	MaskforPhyAccess=0;
#if 0
	if(pHalData->RFChipID == RF_8225 && Offset > 0x24) //36 valid regs
		return	retValue;
	if(pHalData->RFChipID == RF_8256 && Offset > 0x2D) //45 valid regs
		return	retValue;
#endif
	//
	// Make sure RF register offset is correct 
	//
	if(Offset & MAC1_ACCESS_PHY0)
		MaskforPhyAccess = MAC1_ACCESS_PHY0;
	else if(Offset & MAC0_ACCESS_PHY1)
		MaskforPhyAccess = MAC0_ACCESS_PHY1;

	Offset &=0xFF;
	//92D RF offset >0x3f

	//
	// Switch page for 8256 RF IC
	//
	NewOffset = Offset;

	// 2009/06/17 MH We can not execute IO for power save or other accident mode.
	//if(RT_CANNOT_IO(Adapter))
	//{
	//	RTPRINT(FPHY, PHY_RFR, ("phy_RFSerialRead return all one\n"));
	//	return	0xFFFFFFFF;
	//}

	// For 92S LSSI Read RFLSSIRead
	// For RF A/B write 0x824/82c(does not work in the future) 
	// We must use 0x824 for RF A and B to execute read trigger
	tmplong = PHY_QueryBBReg(Adapter, rFPGA0_XA_HSSIParameter2|MaskforPhyAccess, bMaskDWord);
	tmplong |= 0x390004; //by yn.
	if(eRFPath == RF_PATH_A)
		tmplong2 = tmplong;
	else
		tmplong2 = PHY_QueryBBReg(Adapter, pPhyReg->rfHSSIPara2|MaskforPhyAccess, bMaskDWord);
	tmplong2 = (tmplong2 & (~bLSSIReadAddress)) | (NewOffset<<23) | bLSSIReadEdge;	//T65 RF
	tmplong2 |= 0x390004;
	PHY_SetBBReg(Adapter, rFPGA0_XA_HSSIParameter2|MaskforPhyAccess, bMaskDWord, tmplong&(~bLSSIReadEdge));
	//rtw_udelay_os(1000);
	rtw_udelay_os(10);
	
	PHY_SetBBReg(Adapter, pPhyReg->rfHSSIPara2|MaskforPhyAccess, bMaskDWord, tmplong2);	
	//rtw_udelay_os(1000);
	for(i=0;i<2;i++)
		rtw_udelay_os(MAX_STALL_TIME);
	
	PHY_SetBBReg(Adapter, rFPGA0_XA_HSSIParameter2|MaskforPhyAccess, bMaskDWord, tmplong|bLSSIReadEdge);
	//rtw_udelay_os(1000);
	rtw_udelay_os(10);

	if(eRFPath == RF_PATH_A)
		RfPiEnable = (u8)PHY_QueryBBReg(Adapter, rFPGA0_XA_HSSIParameter1|MaskforPhyAccess, BIT8);
	else if(eRFPath == RF_PATH_B)
		RfPiEnable = (u8)PHY_QueryBBReg(Adapter, rFPGA0_XB_HSSIParameter1|MaskforPhyAccess, BIT8);
	
	if(RfPiEnable)
	{	// Read from BBreg8b8, 12 bits for 8190, 20bits for T65 RF
		retValue = PHY_QueryBBReg(Adapter, pPhyReg->rfLSSIReadBackPi|MaskforPhyAccess, bLSSIReadBackData);
		//RTPRINT(FINIT, INIT_RF, ("Readback from RF-PI : 0x%x\n", retValue));
	}
	else
	{	//Read from BBreg8a0, 12 bits for 8190, 20 bits for T65 RF
		retValue = PHY_QueryBBReg(Adapter, pPhyReg->rfLSSIReadBack|MaskforPhyAccess, bLSSIReadBackData);
		//RTPRINT(FINIT, INIT_RF,("Readback from RF-SI : 0x%x\n", retValue));
	}
	//RTPRINT(FPHY, PHY_RFR, ("RFR-%d Addr[0x%lx]=0x%lx\n", eRFPath, pPhyReg->rfLSSIReadBack, retValue));
	
	return retValue;	
		
}



/**
* Function:	phy_RFSerialWrite
*
* OverView:	Write data to RF register (page 8~) 
*
* Input:
*			PADAPTER		Adapter,
*			RF_RADIO_PATH_E	eRFPath,	//Radio path of A/B/C/D
*			u4Byte			Offset,		//The target address to be read			
*			u4Byte			Data			//The new register Data in the target bit position
*										//of the target to be read			
*
* Output:	None
* Return:		None
* Note:		Threre are three types of serial operations: 
*			1. Software serial write
*			2. Hardware LSSI-Low Speed Serial Interface 
*			3. Hardware HSSI-High speed
*			serial write. Driver need to implement (1) and (2).
*			This function is equal to the combination of RF_ReadReg() and  RFLSSIRead()
 *
 * Note: 		  For RF8256 only
 *			 The total count of RTL8256(Zebra4) register is around 36 bit it only employs 
 *			 4-bit RF address. RTL8256 uses "register mode control bit" (Reg00[12], Reg00[10]) 
 *			 to access register address bigger than 0xf. See "Appendix-4 in PHY Configuration
 *			 programming guide" for more details. 
 *			 Thus, we define a sub-finction for RTL8526 register address conversion
 *		       ===========================================================
 *			 Register Mode		RegCTL[1]		RegCTL[0]		Note
 *								(Reg00[12])		(Reg00[10])
 *		       ===========================================================
 *			 Reg_Mode0				0				x			Reg 0 ~15(0x0 ~ 0xf)
 *		       ------------------------------------------------------------------
 *			 Reg_Mode1				1				0			Reg 16 ~30(0x1 ~ 0xf)
 *		       ------------------------------------------------------------------
 *			 Reg_Mode2				1				1			Reg 31 ~ 45(0x1 ~ 0xf)
 *		       ------------------------------------------------------------------
 *
 *	2008/09/02	MH	Add 92S RF definition
 *	
 *
 *
*/
static	VOID
phy_RFSerialWrite(
	IN	PADAPTER			Adapter,
	IN	RF_RADIO_PATH_E	eRFPath,
	IN	u32				Offset,
	IN	u32				Data
	)
{
	u32	DataAndAddr = 0;
	HAL_DATA_TYPE				*pHalData = GET_HAL_DATA(Adapter);
	BB_REGISTER_DEFINITION_T	*pPhyReg = &pHalData->PHYRegDef[eRFPath];
	u32	NewOffset,MaskforPhyAccess=0;
	
#if 0
	//<Roger_TODO> We should check valid regs for RF_6052 case.
	if(pHalData->RFChipID == RF_8225 && Offset > 0x24) //36 valid regs
		return;
	if(pHalData->RFChipID == RF_8256 && Offset > 0x2D) //45 valid regs
		return;
#endif

	// 2009/06/17 MH We can not execute IO for power save or other accident mode.
	//if(RT_CANNOT_IO(Adapter))
	//{
	//	RTPRINT(FPHY, PHY_RFW, ("phy_RFSerialWrite stop\n"));
	//	return;
	//}

	if(Offset & MAC1_ACCESS_PHY0)
		MaskforPhyAccess = MAC1_ACCESS_PHY0;
	else if(Offset & MAC0_ACCESS_PHY1)
		MaskforPhyAccess = MAC0_ACCESS_PHY1;

	Offset &=0xFF;

	//
	//92D RF offset >0x3f

	//
	// Shadow Update
	//
	//PHY_RFShadowWrite(Adapter, eRFPath, Offset, Data);	

	//
	// Switch page for 8256 RF IC
	//
	NewOffset = Offset;

	//
	// Put write addr in [5:0]  and write data in [31:16]
	//
	//DataAndAddr = (Data<<16) | (NewOffset&0x3f);
	DataAndAddr = ((NewOffset<<20) | (Data&0x000fffff)) & 0x0fffffff;	// T65 RF

	//
	// Write Operation
	//
	PHY_SetBBReg(Adapter, pPhyReg->rf3wireOffset|MaskforPhyAccess, bMaskDWord, DataAndAddr);
	//RTPRINT(FPHY, PHY_RFW, ("RFW-%d Addr[0x%lx]=0x%lx\n", eRFPath, pPhyReg->rf3wireOffset, DataAndAddr));

}


/**
* Function:	PHY_QueryRFReg
*
* OverView:	Query "Specific bits" to RF register (page 8~) 
*
* Input:
*			PADAPTER		Adapter,
*			RF_RADIO_PATH_E	eRFPath,	//Radio path of A/B/C/D
*			u4Byte			RegAddr,		//The target address to be read
*			u4Byte			BitMask		//The target bit position in the target address
*										//to be read	
*
* Output:	None
* Return:		u4Byte			Readback value
* Note:		This function is equal to "GetRFRegSetting" in PHY programming guide
*/
u32
rtl8192d_PHY_QueryRFReg(
	IN	PADAPTER			Adapter,
	IN	RF_RADIO_PATH_E	eRFPath,
	IN	u32				RegAddr,
	IN	u32				BitMask
	)
{
	u32 Original_Value, Readback_Value, BitShift;	
	HAL_DATA_TYPE		*pHalData = GET_HAL_DATA(Adapter);
	//u8	RFWaitCounter = 0;

#if (DISABLE_BB_RF == 1)
	return 0;
#endif

	if(!pHalData->bPhyValueInitReady)
		return 0;

	//RT_TRACE(COMP_RF, DBG_TRACE, ("--->PHY_QueryRFReg(): RegAddr(%#lx), eRFPath(%#x), BitMask(%#lx)\n", RegAddr, eRFPath,BitMask));
	
#ifdef CONFIG_USB_HCI
	//PlatformAcquireMutex(&pHalData->mxRFOperate);
#else
	//PlatformAcquireSpinLock(Adapter, RT_RF_OPERATE_SPINLOCK);
#endif

#ifdef CONFIG_USB_HCI
	if(pHalData->bReadRFbyFW)
	{
		Original_Value = rtw_read32(Adapter,(0x66<<24|eRFPath<<16)|RegAddr ); //0x66 Just a identifier.by wl
	}
	else
#endif
	{
		Original_Value = phy_RFSerialRead(Adapter, eRFPath, RegAddr);
	}
	
	BitShift =  phy_CalculateBitShift(BitMask);
	Readback_Value = (Original_Value & BitMask) >> BitShift;	

#ifdef CONFIG_USB_HCI
	//PlatformReleaseMutex(&pHalData->mxRFOperate);
#else
	//PlatformReleaseSpinLock(Adapter, RT_RF_OPERATE_SPINLOCK);
#endif


	//RTPRINT(FPHY, PHY_RFR, ("RFR-%d MASK=0x%lx Addr[0x%lx]=0x%lx\n", eRFPath, BitMask, RegAddr, Original_Value));//BitMask(%#lx),BitMask,
	//RT_TRACE(COMP_RF, DBG_TRACE, ("<---PHY_QueryRFReg(): RegAddr(%#lx), eRFPath(%#x),  Original_Value(%#lx)\n", 
	//				RegAddr, eRFPath, Original_Value));
	
	return (Readback_Value);
}

/**
* Function:	PHY_SetRFReg
*
* OverView:	Write "Specific bits" to RF register (page 8~) 
*
* Input:
*			PADAPTER		Adapter,
*			RF_RADIO_PATH_E	eRFPath,	//Radio path of A/B/C/D
*			u4Byte			RegAddr,		//The target address to be modified
*			u4Byte			BitMask		//The target bit position in the target address
*										//to be modified	
*			u4Byte			Data			//The new register Data in the target bit position
*										//of the target address			
*
* Output:	None
* Return:		None
* Note:		This function is equal to "PutRFRegSetting" in PHY programming guide
*/
VOID
rtl8192d_PHY_SetRFReg(
	IN	PADAPTER			Adapter,
	IN	RF_RADIO_PATH_E	eRFPath,
	IN	u32				RegAddr,
	IN	u32				BitMask,
	IN	u32				Data
	)
{

	HAL_DATA_TYPE	*pHalData		= GET_HAL_DATA(Adapter);
	//u1Byte			RFWaitCounter	= 0;
	u32 			Original_Value, BitShift;

#if (DISABLE_BB_RF == 1)
	return;
#endif

	if(!pHalData->bPhyValueInitReady)
		return;

	if(BitMask == 0)
		return;

	//RT_TRACE(COMP_RF, DBG_TRACE, ("--->PHY_SetRFReg(): RegAddr(%#lx), BitMask(%#lx), Data(%#lx), eRFPath(%#x)\n", 
	//	RegAddr, BitMask, Data, eRFPath));
	//RTPRINT(FINIT, INIT_RF, ("PHY_SetRFReg(): RegAddr(%#lx), BitMask(%#lx), Data(%#lx), eRFPath(%#x)\n", 
	//	RegAddr, BitMask, Data, eRFPath));


#ifdef CONFIG_USB_HCI
	//PlatformAcquireMutex(&pHalData->mxRFOperate);
#else
	//PlatformAcquireSpinLock(Adapter, RT_RF_OPERATE_SPINLOCK);
#endif

	
	// RF data is 12 bits only
	if (BitMask != bRFRegOffsetMask) 
	{
		Original_Value = phy_RFSerialRead(Adapter, eRFPath, RegAddr);
		BitShift =  phy_CalculateBitShift(BitMask);
		Data = (((Original_Value) & (~BitMask)) | (Data<< BitShift));
	}
		
	phy_RFSerialWrite(Adapter, eRFPath, RegAddr, Data);
	


#ifdef CONFIG_USB_HCI
	//PlatformReleaseMutex(&pHalData->mxRFOperate);
#else
	//PlatformReleaseSpinLock(Adapter, RT_RF_OPERATE_SPINLOCK);
#endif
	
	//PHY_QueryRFReg(Adapter,eRFPath,RegAddr,BitMask);
	//RT_TRACE(COMP_RF, DBG_TRACE, ("<---PHY_SetRFReg(): RegAddr(%#lx), BitMask(%#lx), Data(%#lx), eRFPath(%#x)\n", 
	//		RegAddr, BitMask, Data, eRFPath));

}


//
// 3. Initial MAC/BB/RF config by reading MAC/BB/RF txt.
//

/*-----------------------------------------------------------------------------
 * Function:    phy_ConfigMACWithParaFile()
 *
 * Overview:    This function read BB parameters from general file format, and do register
 *			  Read/Write 
 *
 * Input:      	PADAPTER		Adapter
 *			ps1Byte 			pFileName			
 *
 * Output:      NONE
 *
 * Return:      RT_STATUS_SUCCESS: configuration file exist
 *			
 * Note: 		The format of MACPHY_REG.txt is different from PHY and RF. 
 *			[Register][Mask][Value]
 *---------------------------------------------------------------------------*/
#ifndef CONFIG_EMBEDDED_FWIMG
static	int
phy_ConfigMACWithParaFile(
	IN	PADAPTER		Adapter,
	IN	u8* 			pFileName
)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	
	int		rtStatus = _SUCCESS;

	return rtStatus;
}
#endif //CONFIG_EMBEDDED_FWIMG
/*-----------------------------------------------------------------------------
 * Function:    phy_ConfigMACWithHeaderFile()
 *
 * Overview:    This function read BB parameters from Header file we gen, and do register
 *			  Read/Write 
 *
 * Input:      	PADAPTER		Adapter
 *			ps1Byte 			pFileName			
 *
 * Output:      NONE
 *
 * Return:      RT_STATUS_SUCCESS: configuration file exist
 *			
 * Note: 		The format of MACPHY_REG.txt is different from PHY and RF. 
 *			[Register][Mask][Value]
 *---------------------------------------------------------------------------*/
static	int
phy_ConfigMACWithHeaderFile(
	IN	PADAPTER		Adapter
)
{
	u32					i = 0;
	u32					ArrayLength = 0;
	u32*				ptrArray;	
	HAL_DATA_TYPE		*pHalData = GET_HAL_DATA(Adapter);

	//2008.11.06 Modified by tynli.
	//RT_TRACE(COMP_INIT, DBG_LOUD, ("Read Rtl819XMACPHY_Array\n"));

	ArrayLength = Rtl8192D_MAC_ArrayLength;
	ptrArray = (u32 *)Rtl8192D_MAC_Array;
	//RT_TRACE(COMP_INIT, DBG_LOUD, (" ===> phy_ConfigMACWithHeaderFile() Img:Rtl819XMAC_Array\n"));

	for(i = 0 ;i < ArrayLength;i=i+2){ // Add by tynli for 2 column
		rtw_write8(Adapter, ptrArray[i], (u8)ptrArray[i+1]);
	}
	
	return _SUCCESS;
	
}

/*-----------------------------------------------------------------------------
 * Function:    PHY_MACConfig8192C
 *
 * Overview:	Condig MAC by header file or parameter file.
 *
 * Input:       NONE
 *
 * Output:      NONE
 *
 * Return:      NONE
 *
 * Revised History:
 *  When		Who		Remark
 *  08/12/2008	MHC		Create Version 0.
 *
 *---------------------------------------------------------------------------*/
extern	int
PHY_MACConfig8192D(
	IN	PADAPTER	Adapter
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	char		*pszMACRegFile;
	char		sz92DMACRegFile[] = RTL8192D_PHY_MACREG;
	int		rtStatus = _SUCCESS;

	if(Adapter->bSurpriseRemoved){
		rtStatus = _FAIL;
		return rtStatus;
	}

	pszMACRegFile = sz92DMACRegFile;

	//
	// Config MAC
	//
#ifdef CONFIG_EMBEDDED_FWIMG
	rtStatus = phy_ConfigMACWithHeaderFile(Adapter);
#else
	
	// Not make sure EEPROM, add later
	//RT_TRACE(COMP_INIT, DBG_LOUD, ("Read MACREG.txt\n"));
	rtStatus = phy_ConfigMACWithParaFile(Adapter, pszMACRegFile);
#endif

	if(pHalData->MacPhyMode92D == SINGLEMAC_SINGLEPHY)
	{
		//improve 2-stream TX EVM by Jenyu
		//rtw_write8(Adapter, 0x14,0x71);
		// 2010.07.13 AMPDU aggregation number 9
		//rtw_write16(Adapter, REG_MAX_AGGR_NUM, MAX_AGGR_NUM);
		rtw_write8(Adapter, REG_MAX_AGGR_NUM, 0x0B); //By tynli. 2010.11.18.
	}
	else
		rtw_write8(Adapter, REG_MAX_AGGR_NUM, 0x07); //92D need to test to decide the num.

	return rtStatus;

}


/**
* Function:	phy_InitBBRFRegisterDefinition
*
* OverView:	Initialize Register definition offset for Radio Path A/B/C/D
*
* Input:
*			PADAPTER		Adapter,
*
* Output:	None
* Return:		None
* Note:		The initialization value is constant and it should never be changes
*/
static	VOID
phy_InitBBRFRegisterDefinition(
	IN	PADAPTER		Adapter
)
{
	HAL_DATA_TYPE		*pHalData = GET_HAL_DATA(Adapter);	

	// RF Interface Sowrtware Control
	pHalData->PHYRegDef[RF_PATH_A].rfintfs = rFPGA0_XAB_RFInterfaceSW; // 16 LSBs if read 32-bit from 0x870
	pHalData->PHYRegDef[RF_PATH_B].rfintfs = rFPGA0_XAB_RFInterfaceSW; // 16 MSBs if read 32-bit from 0x870 (16-bit for 0x872)
	pHalData->PHYRegDef[RF_PATH_C].rfintfs = rFPGA0_XCD_RFInterfaceSW;// 16 LSBs if read 32-bit from 0x874
	pHalData->PHYRegDef[RF_PATH_D].rfintfs = rFPGA0_XCD_RFInterfaceSW;// 16 MSBs if read 32-bit from 0x874 (16-bit for 0x876)

	// RF Interface Readback Value
	pHalData->PHYRegDef[RF_PATH_A].rfintfi = rFPGA0_XAB_RFInterfaceRB; // 16 LSBs if read 32-bit from 0x8E0	
	pHalData->PHYRegDef[RF_PATH_B].rfintfi = rFPGA0_XAB_RFInterfaceRB;// 16 MSBs if read 32-bit from 0x8E0 (16-bit for 0x8E2)
	pHalData->PHYRegDef[RF_PATH_C].rfintfi = rFPGA0_XCD_RFInterfaceRB;// 16 LSBs if read 32-bit from 0x8E4
	pHalData->PHYRegDef[RF_PATH_D].rfintfi = rFPGA0_XCD_RFInterfaceRB;// 16 MSBs if read 32-bit from 0x8E4 (16-bit for 0x8E6)

	// RF Interface Output (and Enable)
	pHalData->PHYRegDef[RF_PATH_A].rfintfo = rFPGA0_XA_RFInterfaceOE; // 16 LSBs if read 32-bit from 0x860
	pHalData->PHYRegDef[RF_PATH_B].rfintfo = rFPGA0_XB_RFInterfaceOE; // 16 LSBs if read 32-bit from 0x864

	// RF Interface (Output and)  Enable
	pHalData->PHYRegDef[RF_PATH_A].rfintfe = rFPGA0_XA_RFInterfaceOE; // 16 MSBs if read 32-bit from 0x860 (16-bit for 0x862)
	pHalData->PHYRegDef[RF_PATH_B].rfintfe = rFPGA0_XB_RFInterfaceOE; // 16 MSBs if read 32-bit from 0x864 (16-bit for 0x866)

	//Addr of LSSI. Wirte RF register by driver
	pHalData->PHYRegDef[RF_PATH_A].rf3wireOffset = rFPGA0_XA_LSSIParameter; //LSSI Parameter
	pHalData->PHYRegDef[RF_PATH_B].rf3wireOffset = rFPGA0_XB_LSSIParameter;

	// RF parameter
	pHalData->PHYRegDef[RF_PATH_A].rfLSSI_Select = rFPGA0_XAB_RFParameter;  //BB Band Select
	pHalData->PHYRegDef[RF_PATH_B].rfLSSI_Select = rFPGA0_XAB_RFParameter;
	pHalData->PHYRegDef[RF_PATH_C].rfLSSI_Select = rFPGA0_XCD_RFParameter;
	pHalData->PHYRegDef[RF_PATH_D].rfLSSI_Select = rFPGA0_XCD_RFParameter;

	// Tx AGC Gain Stage (same for all path. Should we remove this?)
	pHalData->PHYRegDef[RF_PATH_A].rfTxGainStage = rFPGA0_TxGainStage; //Tx gain stage
	pHalData->PHYRegDef[RF_PATH_B].rfTxGainStage = rFPGA0_TxGainStage; //Tx gain stage
	pHalData->PHYRegDef[RF_PATH_C].rfTxGainStage = rFPGA0_TxGainStage; //Tx gain stage
	pHalData->PHYRegDef[RF_PATH_D].rfTxGainStage = rFPGA0_TxGainStage; //Tx gain stage

	// Tranceiver A~D HSSI Parameter-1
	pHalData->PHYRegDef[RF_PATH_A].rfHSSIPara1 = rFPGA0_XA_HSSIParameter1;  //wire control parameter1
	pHalData->PHYRegDef[RF_PATH_B].rfHSSIPara1 = rFPGA0_XB_HSSIParameter1;  //wire control parameter1

	// Tranceiver A~D HSSI Parameter-2
	pHalData->PHYRegDef[RF_PATH_A].rfHSSIPara2 = rFPGA0_XA_HSSIParameter2;  //wire control parameter2
	pHalData->PHYRegDef[RF_PATH_B].rfHSSIPara2 = rFPGA0_XB_HSSIParameter2;  //wire control parameter2

	// RF switch Control
	pHalData->PHYRegDef[RF_PATH_A].rfSwitchControl = rFPGA0_XAB_SwitchControl; //TR/Ant switch control
	pHalData->PHYRegDef[RF_PATH_B].rfSwitchControl = rFPGA0_XAB_SwitchControl;
	pHalData->PHYRegDef[RF_PATH_C].rfSwitchControl = rFPGA0_XCD_SwitchControl;
	pHalData->PHYRegDef[RF_PATH_D].rfSwitchControl = rFPGA0_XCD_SwitchControl;

	// AGC control 1 
	pHalData->PHYRegDef[RF_PATH_A].rfAGCControl1 = rOFDM0_XAAGCCore1;
	pHalData->PHYRegDef[RF_PATH_B].rfAGCControl1 = rOFDM0_XBAGCCore1;
	pHalData->PHYRegDef[RF_PATH_C].rfAGCControl1 = rOFDM0_XCAGCCore1;
	pHalData->PHYRegDef[RF_PATH_D].rfAGCControl1 = rOFDM0_XDAGCCore1;

	// AGC control 2 
	pHalData->PHYRegDef[RF_PATH_A].rfAGCControl2 = rOFDM0_XAAGCCore2;
	pHalData->PHYRegDef[RF_PATH_B].rfAGCControl2 = rOFDM0_XBAGCCore2;
	pHalData->PHYRegDef[RF_PATH_C].rfAGCControl2 = rOFDM0_XCAGCCore2;
	pHalData->PHYRegDef[RF_PATH_D].rfAGCControl2 = rOFDM0_XDAGCCore2;

	// RX AFE control 1 
	pHalData->PHYRegDef[RF_PATH_A].rfRxIQImbalance = rOFDM0_XARxIQImbalance;
	pHalData->PHYRegDef[RF_PATH_B].rfRxIQImbalance = rOFDM0_XBRxIQImbalance;
	pHalData->PHYRegDef[RF_PATH_C].rfRxIQImbalance = rOFDM0_XCRxIQImbalance;
	pHalData->PHYRegDef[RF_PATH_D].rfRxIQImbalance = rOFDM0_XDRxIQImbalance;	

	// RX AFE control 1  
	pHalData->PHYRegDef[RF_PATH_A].rfRxAFE = rOFDM0_XARxAFE;
	pHalData->PHYRegDef[RF_PATH_B].rfRxAFE = rOFDM0_XBRxAFE;
	pHalData->PHYRegDef[RF_PATH_C].rfRxAFE = rOFDM0_XCRxAFE;
	pHalData->PHYRegDef[RF_PATH_D].rfRxAFE = rOFDM0_XDRxAFE;	

	// Tx AFE control 1 
	pHalData->PHYRegDef[RF_PATH_A].rfTxIQImbalance = rOFDM0_XATxIQImbalance;
	pHalData->PHYRegDef[RF_PATH_B].rfTxIQImbalance = rOFDM0_XBTxIQImbalance;
	pHalData->PHYRegDef[RF_PATH_C].rfTxIQImbalance = rOFDM0_XCTxIQImbalance;
	pHalData->PHYRegDef[RF_PATH_D].rfTxIQImbalance = rOFDM0_XDTxIQImbalance;	

	// Tx AFE control 2 
	pHalData->PHYRegDef[RF_PATH_A].rfTxAFE = rOFDM0_XATxAFE;
	pHalData->PHYRegDef[RF_PATH_B].rfTxAFE = rOFDM0_XBTxAFE;
	pHalData->PHYRegDef[RF_PATH_C].rfTxAFE = rOFDM0_XCTxAFE;
	pHalData->PHYRegDef[RF_PATH_D].rfTxAFE = rOFDM0_XDTxAFE;	

	// Tranceiver LSSI Readback SI mode 
	pHalData->PHYRegDef[RF_PATH_A].rfLSSIReadBack = rFPGA0_XA_LSSIReadBack;
	pHalData->PHYRegDef[RF_PATH_B].rfLSSIReadBack = rFPGA0_XB_LSSIReadBack;
	pHalData->PHYRegDef[RF_PATH_C].rfLSSIReadBack = rFPGA0_XC_LSSIReadBack;
	pHalData->PHYRegDef[RF_PATH_D].rfLSSIReadBack = rFPGA0_XD_LSSIReadBack;	

	// Tranceiver LSSI Readback PI mode 
	pHalData->PHYRegDef[RF_PATH_A].rfLSSIReadBackPi = TransceiverA_HSPI_Readback;
	pHalData->PHYRegDef[RF_PATH_B].rfLSSIReadBackPi = TransceiverB_HSPI_Readback;
	//pHalData->PHYRegDef[RF_PATH_C].rfLSSIReadBackPi = rFPGA0_XC_LSSIReadBack;
	//pHalData->PHYRegDef[RF_PATH_D].rfLSSIReadBackPi = rFPGA0_XD_LSSIReadBack;	
	pHalData->bPhyValueInitReady = _TRUE;
}

/*-----------------------------------------------------------------------------
 * Function:    phy_ConfigBBWithHeaderFile()
 *
 * Overview:    This function read BB parameters from general file format, and do register
 *			  Read/Write 
 *
 * Input:      	PADAPTER		Adapter
 *			u1Byte 			ConfigType     0 => PHY_CONFIG
 *										 1 =>AGC_TAB
 *
 * Output:      NONE
 *
 * Return:      RT_STATUS_SUCCESS: configuration file exist
 *			
 *---------------------------------------------------------------------------*/
static	int
phy_ConfigBBWithHeaderFile(
	IN	PADAPTER		Adapter,
	IN	u8 			ConfigType
)
{
	int i;
	u32*	Rtl819XPHY_REGArray_Table;
	u32*	Rtl819XAGCTAB_Array_Table=NULL;
	u32*	Rtl819XAGCTAB_5GArray_Table=NULL;
	u16	PHY_REGArrayLen=0, AGCTAB_ArrayLen=0, AGCTAB_5GArrayLen=0;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;


	//Normal chip,Mac0 use AGC_TAB.txt for 2G and 5G band.
	if(pHalData->interfaceIndex == 0)
	{
		AGCTAB_ArrayLen = Rtl8192D_AGCTAB_ArrayLength;
		Rtl819XAGCTAB_Array_Table = (u32 *)Rtl8192D_AGCTAB_Array;
		//RT_TRACE(COMP_INIT, DBG_LOUD, (" ===> phy_ConfigBBWithHeaderFile() phy:MAC0, Rtl819XAGCTAB_Array\n"));
	}
	else
	{
		if(pHalData->CurrentBandType92D == BAND_ON_2_4G)
		{
			AGCTAB_ArrayLen = Rtl8192D_AGCTAB_2GArrayLength;
			Rtl819XAGCTAB_Array_Table = (u32 *)Rtl8192D_AGCTAB_2GArray;
			//RT_TRACE(COMP_INIT, DBG_LOUD, (" ===> phy_ConfigBBWithHeaderFile() phy:MAC1, Rtl819XAGCTAB_2GArray\n"));
		}
		else
		{
			AGCTAB_5GArrayLen = Rtl8192D_AGCTAB_5GArrayLength;
			Rtl819XAGCTAB_5GArray_Table = (u32 *)Rtl8192D_AGCTAB_5GArray;
			//RT_TRACE(COMP_INIT, DBG_LOUD, (" ===> phy_ConfigBBWithHeaderFile() phy:MAC1, Rtl819XAGCTAB_5GArray\n"));
		}
	}

	PHY_REGArrayLen = Rtl8192D_PHY_REG_2TArrayLength;
	Rtl819XPHY_REGArray_Table = (u32 *)Rtl8192D_PHY_REG_2TArray;
	//RT_TRACE(COMP_INIT, DBG_LOUD, (" ===> phy_ConfigBBWithHeaderFile() phy:Rtl819XPHY_REG_Array_PG\n"));

	if(ConfigType == BaseBand_Config_PHY_REG)
	{
		for(i=0;i<PHY_REGArrayLen;i=i+2)
		{
			if (Rtl819XPHY_REGArray_Table[i] == 0xfe) {
				#ifdef CONFIG_LONG_DELAY_ISSUE
				rtw_msleep_os(50);
				#else
				rtw_mdelay_os(50);
				#endif
			}
			else if (Rtl819XPHY_REGArray_Table[i] == 0xfd)
				rtw_mdelay_os(5);
			else if (Rtl819XPHY_REGArray_Table[i] == 0xfc)
				rtw_mdelay_os(1);
			else if (Rtl819XPHY_REGArray_Table[i] == 0xfb)
				rtw_udelay_os(50);
			else if (Rtl819XPHY_REGArray_Table[i] == 0xfa)
				rtw_udelay_os(5);
			else if (Rtl819XPHY_REGArray_Table[i] == 0xf9)
				rtw_udelay_os(1);
			else if (Rtl819XPHY_REGArray_Table[i] == 0xa24)
				pdmpriv->RegA24 = Rtl819XPHY_REGArray_Table[i+1];			
			PHY_SetBBReg(Adapter, Rtl819XPHY_REGArray_Table[i], bMaskDWord, Rtl819XPHY_REGArray_Table[i+1]);		

			// Add 1us delay between BB/RF register setting.
			rtw_udelay_os(1);

			//RT_TRACE(COMP_INIT, DBG_TRACE, ("The Rtl819XPHY_REGArray_Table[0] is %lx Rtl819XPHY_REGArray[1] is %lx \n",Rtl819XPHY_REGArray_Table[i], Rtl819XPHY_REGArray_Table[i+1]));
		}
	}
	else if(ConfigType == BaseBand_Config_AGC_TAB)
	{
		//especial for 5G, vivi, 20100528
		if(pHalData->interfaceIndex == 0)
		{
			for(i=0;i<AGCTAB_ArrayLen;i=i+2)
			{
				PHY_SetBBReg(Adapter, Rtl819XAGCTAB_Array_Table[i], bMaskDWord, Rtl819XAGCTAB_Array_Table[i+1]);		

				// Add 1us delay between BB/RF register setting.
				rtw_udelay_os(1);
					
				//RT_TRACE(COMP_INIT, DBG_TRACE, ("The Rtl819XAGCTAB_Array_Table[0] is %lx Rtl819XPHY_REGArray[1] is %lx \n",Rtl819XAGCTAB_Array_Table[i], Rtl819XAGCTAB_Array_Table[i+1]));
			}
			//RT_TRACE(COMP_INIT, DBG_LOUD, ("Normal Chip, MAC0, load Rtl819XAGCTAB_Array\n"));
		}
		else
		{
			if(pHalData->CurrentBandType92D == BAND_ON_2_4G)
			{
				for(i=0;i<AGCTAB_ArrayLen;i=i+2)
				{
					PHY_SetBBReg(Adapter, Rtl819XAGCTAB_Array_Table[i], bMaskDWord, Rtl819XAGCTAB_Array_Table[i+1]);		

					// Add 1us delay between BB/RF register setting.
					rtw_udelay_os(1);
					
					//RT_TRACE(COMP_INIT, DBG_TRACE, ("The Rtl819XAGCTAB_Array_Table[0] is %lx Rtl819XPHY_REGArray[1] is %lx \n",Rtl819XAGCTAB_Array_Table[i], Rtl819XAGCTAB_Array_Table[i+1]));
				}
				//RT_TRACE(COMP_INIT, DBG_LOUD, ("Load Rtl819XAGCTAB_2GArray\n"));
			}
			else
			{
				for(i=0;i<AGCTAB_5GArrayLen;i=i+2)
				{
					PHY_SetBBReg(Adapter, Rtl819XAGCTAB_5GArray_Table[i], bMaskDWord, Rtl819XAGCTAB_5GArray_Table[i+1]);		

					// Add 1us delay between BB/RF register setting.
					rtw_udelay_os(1);
					
					//RT_TRACE(COMP_INIT, DBG_TRACE, ("The Rtl819XAGCTAB_5GArray_Table[0] is %lx Rtl819XPHY_REGArray[1] is %lx \n",Rtl819XAGCTAB_5GArray_Table[i], Rtl819XAGCTAB_5GArray_Table[i+1]));
				}
				//RT_TRACE(COMP_INIT, DBG_LOUD, ("Load Rtl819XAGCTAB_5GArray\n"));
			}
		}
	}
	
	return _SUCCESS;
	
}

/*-----------------------------------------------------------------------------
 * Function:    phy_ConfigBBWithParaFile()
 *
 * Overview:    This function read BB parameters from general file format, and do register
 *			  Read/Write 
 *
 * Input:      	PADAPTER		Adapter
 *			ps1Byte 			pFileName			
 *
 * Output:      NONE
 *
 * Return:      RT_STATUS_SUCCESS: configuration file exist
 *	2008/11/06	MH	For 92S we do not support silent reset now. Disable 
 *					parameter file compare!!!!!!??
 *			
 *---------------------------------------------------------------------------*/
#ifndef CONFIG_EMBEDDED_FWIMG
static	int
phy_ConfigBBWithParaFile(
	IN	PADAPTER		Adapter,
	IN	u8* 			pFileName
)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	
	int		rtStatus = _SUCCESS;

	return rtStatus;	
}
#endif //CONFIG_EMBEDDED_FWIMG
#if MP_DRIVER != 1
static VOID
storePwrIndexDiffRateOffset(
	IN	PADAPTER	Adapter,
	IN	u32		RegAddr,
	IN	u32		BitMask,
	IN	u32		Data
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	
	if(RegAddr == rTxAGC_A_Rate18_06)
	{
		pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][0] = Data;
		//RT_TRACE(COMP_INIT, DBG_TRACE, ("MCSTxPowerLevelOriginalOffset[%d][0] = 0x%lx\n", pHalData->pwrGroupCnt,
		//	pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][0]));
	}
	if(RegAddr == rTxAGC_A_Rate54_24)
	{
		pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][1] = Data;
		//RT_TRACE(COMP_INIT, DBG_TRACE, ("MCSTxPowerLevelOriginalOffset[%d][1] = 0x%lx\n", pHalData->pwrGroupCnt,
		//	pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][1]));
	}
	if(RegAddr == rTxAGC_A_CCK1_Mcs32)
	{
		pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][6] = Data;
		//RT_TRACE(COMP_INIT, DBG_TRACE, ("MCSTxPowerLevelOriginalOffset[%d][6] = 0x%lx\n", pHalData->pwrGroupCnt,
		//	pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][6]));
	}
	if(RegAddr == rTxAGC_B_CCK11_A_CCK2_11 && BitMask == 0xffffff00)
	{
		pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][7] = Data;
		//RT_TRACE(COMP_INIT, DBG_TRACE, ("MCSTxPowerLevelOriginalOffset[%d][7] = 0x%lx\n", pHalData->pwrGroupCnt,
		//	pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][7]));
	}	
	if(RegAddr == rTxAGC_A_Mcs03_Mcs00)
	{
		pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][2] = Data;
		//RT_TRACE(COMP_INIT, DBG_TRACE, ("MCSTxPowerLevelOriginalOffset[%d][2] = 0x%lx\n", pHalData->pwrGroupCnt,
		//	pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][2]));
	}
	if(RegAddr == rTxAGC_A_Mcs07_Mcs04)
	{
		pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][3] = Data;
		//RT_TRACE(COMP_INIT, DBG_TRACE, ("MCSTxPowerLevelOriginalOffset[%d][3] = 0x%lx\n", pHalData->pwrGroupCnt,
		//	pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][3]));
	}
	if(RegAddr == rTxAGC_A_Mcs11_Mcs08)
	{
		pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][4] = Data;
		//RT_TRACE(COMP_INIT, DBG_TRACE, ("MCSTxPowerLevelOriginalOffset[%d][4] = 0x%lx\n", pHalData->pwrGroupCnt,
		//	pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][4]));
	}
	if(RegAddr == rTxAGC_A_Mcs15_Mcs12)
	{
		pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][5] = Data;
		//RT_TRACE(COMP_INIT, DBG_TRACE, ("MCSTxPowerLevelOriginalOffset[%d][5] = 0x%lx\n", pHalData->pwrGroupCnt,
		//	pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][5]));
	}
	if(RegAddr == rTxAGC_B_Rate18_06)
	{
		pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][8] = Data;
		//RT_TRACE(COMP_INIT, DBG_TRACE, ("MCSTxPowerLevelOriginalOffset[%d][8] = 0x%lx\n", pHalData->pwrGroupCnt,
		//	pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][8]));
	}
	if(RegAddr == rTxAGC_B_Rate54_24)
	{
		pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][9] = Data;
		//RT_TRACE(COMP_INIT, DBG_TRACE, ("MCSTxPowerLevelOriginalOffset[%d][9] = 0x%lx\n", pHalData->pwrGroupCnt,
		//	pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][9]));
	}
	if(RegAddr == rTxAGC_B_CCK1_55_Mcs32)
	{
		pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][14] = Data;
		//RT_TRACE(COMP_INIT, DBG_TRACE, ("MCSTxPowerLevelOriginalOffset[%d][14] = 0x%lx\n", pHalData->pwrGroupCnt,
		//	pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][14]));
	}
	if(RegAddr == rTxAGC_B_CCK11_A_CCK2_11 && BitMask == 0x000000ff)
	{
		pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][15] = Data;
		//RT_TRACE(COMP_INIT, DBG_TRACE, ("MCSTxPowerLevelOriginalOffset[%d][15] = 0x%lx\n", pHalData->pwrGroupCnt,
		//	pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][15]));
	}	
	if(RegAddr == rTxAGC_B_Mcs03_Mcs00)
	{
		pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][10] = Data;
		//RT_TRACE(COMP_INIT, DBG_TRACE, ("MCSTxPowerLevelOriginalOffset[%d][10] = 0x%lx\n", pHalData->pwrGroupCnt,
		//	pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][10]));
	}
	if(RegAddr == rTxAGC_B_Mcs07_Mcs04)
	{
		pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][11] = Data;
		//RT_TRACE(COMP_INIT, DBG_TRACE, ("MCSTxPowerLevelOriginalOffset[%d][11] = 0x%lx\n", pHalData->pwrGroupCnt,
		//	pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][11]));
	}
	if(RegAddr == rTxAGC_B_Mcs11_Mcs08)
	{
		pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][12] = Data;
		//RT_TRACE(COMP_INIT, DBG_TRACE, ("MCSTxPowerLevelOriginalOffset[%d][12] = 0x%lx\n", pHalData->pwrGroupCnt,
		//	pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][12]));
	}
	if(RegAddr == rTxAGC_B_Mcs15_Mcs12)
	{
		pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][13] = Data;
		//RT_TRACE(COMP_INIT, DBG_TRACE, ("MCSTxPowerLevelOriginalOffset[%d][13] = 0x%lx\n", pHalData->pwrGroupCnt,
		//	pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][13]));
		pHalData->pwrGroupCnt++;
	}
}

/*-----------------------------------------------------------------------------
 * Function:	phy_ConfigBBWithPgHeaderFile
 *
 * Overview:	Config PHY_REG_PG array 
 *
 * Input:       NONE
 *
 * Output:      NONE
 *
 * Return:      NONE
 *
 * Revised History:
 * When			Who		Remark
 * 11/06/2008 	MHC		Add later!!!!!!.. Please modify for new files!!!!
 * 11/10/2008	tynli		Modify to mew files.
 *---------------------------------------------------------------------------*/
static	int
phy_ConfigBBWithPgHeaderFile(
	IN	PADAPTER		Adapter,
	IN	u8 			ConfigType)
{
	int i;
	u32*	Rtl819XPHY_REGArray_Table_PG;
	u16	PHY_REGArrayPGLen;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);

	PHY_REGArrayPGLen = Rtl8192D_PHY_REG_Array_PGLength;
	Rtl819XPHY_REGArray_Table_PG = (u32 *)Rtl8192D_PHY_REG_Array_PG;

	if(ConfigType == BaseBand_Config_PHY_REG)
	{
		for(i=0;i<PHY_REGArrayPGLen;i=i+3)
		{
			//if (Rtl819XPHY_REGArray_Table_PG[i] == 0xfe) {
			//	#ifdef CONFIG_LONG_DELAY_ISSUE
			//	rtw_msleep_os(50);
			//	#else
			//	rtw_mdelay_os(50);
			//	#endif
			//}
			//else if (Rtl819XPHY_REGArray_Table_PG[i] == 0xfd)
			//	rtw_mdelay_os(5);
			//else if (Rtl819XPHY_REGArray_Table_PG[i] == 0xfc)
			//	rtw_mdelay_os(1);
			//else if (Rtl819XPHY_REGArray_Table_PG[i] == 0xfb)
			//	rtw_udelay_os(50);
			//else if (Rtl819XPHY_REGArray_Table_PG[i] == 0xfa)
			//	rtw_udelay_os(5);
			//else if (Rtl819XPHY_REGArray_Table_PG[i] == 0xf9)
			//	rtw_udelay_os(1);
			storePwrIndexDiffRateOffset(Adapter, Rtl819XPHY_REGArray_Table_PG[i], 
				Rtl819XPHY_REGArray_Table_PG[i+1], 
				Rtl819XPHY_REGArray_Table_PG[i+2]);
			//PHY_SetBBReg(Adapter, Rtl819XPHY_REGArray_Table_PG[i], Rtl819XPHY_REGArray_Table_PG[i+1], Rtl819XPHY_REGArray_Table_PG[i+2]);		
			//RT_TRACE(COMP_SEND, DBG_TRACE, ("The Rtl819XPHY_REGArray_Table_PG[0] is %lx Rtl819XPHY_REGArray_Table_PG[1] is %lx \n",Rtl819XPHY_REGArray_Table_PG[i], Rtl819XPHY_REGArray_Table_PG[i+1]));
		}
	}
	else
	{

		//RT_TRACE(COMP_SEND, DBG_LOUD, ("phy_ConfigBBWithPgHeaderFile(): ConfigType != BaseBand_Config_PHY_REG\n"));
	}
	
	return _SUCCESS;
	
}	/* phy_ConfigBBWithPgHeaderFile */
#endif

/*-----------------------------------------------------------------------------
 * Function:	phy_ConfigBBWithPgParaFile
 *
 * Overview:	
 *
 * Input:       NONE
 *
 * Output:      NONE
 *
 * Return:      NONE
 *
 * Revised History:
 * When			Who		Remark
 * 11/06/2008 	MHC		Create Version 0.
 * 2009/07/29	tynli		(porting from 92SE branch)2009/03/11 Add copy parameter file to buffer for silent reset
 *---------------------------------------------------------------------------*/
#ifndef CONFIG_EMBEDDED_FWIMG
static	int
phy_ConfigBBWithPgParaFile(
	IN	PADAPTER		Adapter,
	IN	u8* 			pFileName)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	
	int		rtStatus = _SUCCESS;


	return rtStatus;
	
}	/* phy_ConfigBBWithPgParaFile */
#endif //CONFIG_EMBEDDED_FWIMG
#if MP_DRIVER == 1 
#ifndef CONFIG_EMBEDDED_FWIMG
/*-----------------------------------------------------------------------------
 * Function:    phy_ConfigBBWithMpParaFile()
 *
 * Overview:    This function read BB parameters from general file format, and do register
 *			  Read/Write 
 *
 * Input:      	PADAPTER		Adapter
 *			ps1Byte 			pFileName			
 *
 * Output:      NONE
 *
 * Return:      RT_STATUS_SUCCESS: configuration file exist
 *	2008/11/06	MH	For 92S we do not support silent reset now. Disable 
 *					parameter file compare!!!!!!??
 *			
 *---------------------------------------------------------------------------*/
static	int
phy_ConfigBBWithMpParaFile(
	IN	PADAPTER	Adapter,
	IN	s8 			*pFileName
)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	int	rtStatus = _SUCCESS;

	return rtStatus;
	
}
#else
/*-----------------------------------------------------------------------------
 * Function:	phy_ConfigBBWithMpHeaderFile
 *
 * Overview:	Config PHY_REG_MP array 
 *
 * Input:       NONE
 *
 * Output:      NONE
 *
 * Return:      NONE
 *
 * Revised History:
 * When			Who		Remark
 * 02/04/2010	chiyokolin		Modify to new files.
 *---------------------------------------------------------------------------*/
static	int
phy_ConfigBBWithMpHeaderFile(
	IN	PADAPTER		Adapter,
	IN	u1Byte 			ConfigType)
{
	int	i;
	u32	*Rtl8192CPHY_REGArray_Table_MP;
	u16	PHY_REGArrayMPLen;

	PHY_REGArrayMPLen = Rtl8192D_PHY_REG_Array_MPLength;
	Rtl8192CPHY_REGArray_Table_MP = (u32 *)Rtl8192D_PHY_REG_Array_MP;

	if(ConfigType == BaseBand_Config_PHY_REG)
	{
		for(i=0;i<PHY_REGArrayMPLen;i=i+2)
		{
			if (Rtl8192CPHY_REGArray_Table_MP[i] == 0xfe) {
				#ifdef CONFIG_LONG_DELAY_ISSUE
				rtw_msleep_os(50);
				#else
				rtw_mdelay_os(50);
				#endif
			}
			else if (Rtl8192CPHY_REGArray_Table_MP[i] == 0xfd)
				rtw_mdelay_os(5);
			else if (Rtl8192CPHY_REGArray_Table_MP[i] == 0xfc)
				rtw_mdelay_os(1);
			else if (Rtl8192CPHY_REGArray_Table_MP[i] == 0xfb)
				rtw_udelay_os(50);
			else if (Rtl8192CPHY_REGArray_Table_MP[i] == 0xfa)
				rtw_udelay_os(5);
			else if (Rtl8192CPHY_REGArray_Table_MP[i] == 0xf9)
				rtw_udelay_os(1);
			PHY_SetBBReg(Adapter, Rtl8192CPHY_REGArray_Table_MP[i], bMaskDWord, Rtl8192CPHY_REGArray_Table_MP[i+1]);		

			// Add 1us delay between BB/RF register setting.

			rtw_udelay_os(1);

			//RT_TRACE(COMP_INIT, DBG_TRACE, ("The Rtl8192CPHY_REGArray_Table_MP[%d] is %lx Rtl8192CPHY_REGArray_Table_MP[%d] is %lx \n", i, i+1, Rtl8192CPHY_REGArray_Table_MP[i], Rtl8192CPHY_REGArray_Table_MP[i+1]));
		}
	}
	else
	{
		//RT_TRACE(COMP_SEND, DBG_LOUD, ("phy_ConfigBBWithMpHeaderFile(): ConfigType != BaseBand_Config_PHY_REG\n"));
	}
	return _SUCCESS;
	
}	/* phy_ConfigBBWithPgHeaderFile */

#endif
#endif

static	int
phy_BB8192D_Config_ParaFile(
	IN	PADAPTER	Adapter
	)
{
#if MP_DRIVER != 1
	EEPROM_EFUSE_PRIV	*pEEPROM = GET_EEPROM_EFUSE_PRIV(Adapter);
#endif
	HAL_DATA_TYPE		*pHalData = GET_HAL_DATA(Adapter);
	int		rtStatus = _SUCCESS;
	s8		sz92DBBRegFile[] = RTL8192D_PHY_REG;
	s8		sz92DBBRegPgFile[] = RTL8192D_PHY_REG_PG;
	s8		sz92DBBRegMpFile[] = RTL8192D_PHY_REG_MP;	
	s8		sz92DAGCTableFile[] = RTL8192D_AGC_TAB;
	s8		sz92D2GAGCTableFile[] = RTL8192D_AGC_TAB_2G;
	s8		sz92D5GAGCTableFile[] = RTL8192D_AGC_TAB_5G;
	char		*pszBBRegFile, *pszAGCTableFile, *pszBBRegPgFile, *pszBBRegMpFile;
	
	//RT_TRACE(COMP_INIT, DBG_TRACE, ("==>phy_BB8192S_Config_ParaFile\n"));

	pszBBRegFile = sz92DBBRegFile;
	pszBBRegPgFile = sz92DBBRegPgFile;
	
	//Normal chip,Mac0 use AGC_TAB.txt for 2G and 5G band.
	if(pHalData->interfaceIndex == 0)
		pszAGCTableFile = sz92DAGCTableFile;
	else
	{
		if(pHalData->CurrentBandType92D == BAND_ON_2_4G)
			pszAGCTableFile = sz92D2GAGCTableFile;
		else
			pszAGCTableFile = sz92D5GAGCTableFile;
	}
	pszBBRegMpFile = sz92DBBRegMpFile;

	//RT_TRACE(COMP_INIT, DBG_LOUD, (" ===> phy_BB8192C_Config_ParaFile() phy_reg:%s\n",pszBBRegFile));
	//RT_TRACE(COMP_INIT, DBG_LOUD, (" ===> phy_BB8192C_Config_ParaFile() phy_reg_pg:%s\n",pszBBRegPgFile));
	//RT_TRACE(COMP_INIT, DBG_LOUD, (" ===> phy_BB8192C_Config_ParaFile() agc_table:%s\n",pszAGCTableFile));

	//
	// 1. Read PHY_REG.TXT BB INIT!!
	// We will seperate as 88C / 92C according to chip version
	//
#ifdef CONFIG_EMBEDDED_FWIMG
	rtStatus = phy_ConfigBBWithHeaderFile(Adapter, BaseBand_Config_PHY_REG);
#else	
	// No matter what kind of CHIP we always read PHY_REG.txt. We must copy different 
	// type of parameter files to phy_reg.txt at first.	
	rtStatus = phy_ConfigBBWithParaFile(Adapter,pszBBRegFile);
#endif

	if(rtStatus != _SUCCESS){
		//RT_TRACE(COMP_INIT, DBG_SERIOUS, ("phy_BB8192S_Config_ParaFile():Write BB Reg Fail!!"));
		goto phy_BB8190_Config_ParaFile_Fail;
	}

#if MP_DRIVER == 1 
	//
	// 1.1 Read PHY_REG_MP.TXT BB INIT!!
	// We will seperate as 88C / 92C according to chip version
	//
#ifdef CONFIG_EMBEDDED_FWIMG
	rtStatus = phy_ConfigBBWithMpHeaderFile(Adapter, BaseBand_Config_PHY_REG);
#else	
	// No matter what kind of CHIP we always read PHY_REG.txt. We must copy different 
	// type of parameter files to phy_reg.txt at first.	
	rtStatus = phy_ConfigBBWithMpParaFile(Adapter,pszBBRegMpFile);
#endif

	if(rtStatus != _SUCCESS){
		//RT_TRACE(COMP_INIT, DBG_SERIOUS, ("phy_BB8192S_Config_ParaFile():Write BB Reg MP Fail!!"));
		goto phy_BB8190_Config_ParaFile_Fail;
	}
#endif

#if MP_DRIVER != 1
	//
	// 2. If EEPROM or EFUSE autoload OK, We must config by PHY_REG_PG.txt
	//
	if (pEEPROM->bautoload_fail_flag == _FALSE)
	{
		pHalData->pwrGroupCnt = 0;

#ifdef CONFIG_EMBEDDED_FWIMG
		rtStatus = phy_ConfigBBWithPgHeaderFile(Adapter, BaseBand_Config_PHY_REG);
#else
		rtStatus = phy_ConfigBBWithPgParaFile(Adapter, pszBBRegPgFile);
#endif
	}
	
	if(rtStatus != _SUCCESS){
		//RT_TRACE(COMP_INIT, DBG_SERIOUS, ("phy_BB8192S_Config_ParaFile():BB_PG Reg Fail!!"));
		goto phy_BB8190_Config_ParaFile_Fail;
	}
#endif

	//
	// 3. BB AGC table Initialization
	//
#ifdef CONFIG_EMBEDDED_FWIMG
#ifdef CONFIG_DUALMAC_CONCURRENT
	if(pHalData->bSlaveOfDMSP)
	{
		DBG_871X("BB config slave skip  2222 \n");
	}
	else
#endif
	{
		rtStatus = phy_ConfigBBWithHeaderFile(Adapter, BaseBand_Config_AGC_TAB);
	}
#else
	//RT_TRACE(COMP_INIT, DBG_LOUD, ("phy_BB8192S_Config_ParaFile AGC_TAB.txt\n"));
	rtStatus = phy_ConfigBBWithParaFile(Adapter, pszAGCTableFile);
#endif

	if(rtStatus != _SUCCESS){
		//RT_TRACE(COMP_FPGA, DBG_SERIOUS, ("phy_BB8192S_Config_ParaFile():AGC Table Fail\n"));
		goto phy_BB8190_Config_ParaFile_Fail;
	}

	// Check if the CCK HighPower is turned ON.
	// This is used to calculate PWDB.
	pHalData->bCckHighPower = (BOOLEAN)(PHY_QueryBBReg(Adapter, rFPGA0_XA_HSSIParameter2, 0x200));
	
phy_BB8190_Config_ParaFile_Fail:

	return rtStatus;
}

int
PHY_BBConfig8192D(
	IN	PADAPTER	Adapter
	)
{
	int	rtStatus = _SUCCESS;
	//u8		PathMap = 0, index = 0, rf_num = 0;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	u32	RegVal;
	u8	value;

	if(Adapter->bSurpriseRemoved){
		rtStatus = _FAIL;
		return rtStatus;
	}

	phy_InitBBRFRegisterDefinition(Adapter);

	// Enable BB and RF
	RegVal = rtw_read16(Adapter, REG_SYS_FUNC_EN);
	rtw_write16(Adapter, REG_SYS_FUNC_EN, RegVal|BIT13|BIT0|BIT1);

	// 20090923 Joseph: Advised by Steven and Jenyu. Power sequence before init RF.
	rtw_write8(Adapter, REG_AFE_PLL_CTRL, 0x83);
	rtw_write8(Adapter, REG_AFE_PLL_CTRL+1, 0xdb);
	value=rtw_read8(Adapter, REG_RF_CTRL);     //  0x1f bit7 bit6 represent for mac0/mac1 driver ready
	rtw_write8(Adapter, REG_RF_CTRL, value|RF_EN|RF_RSTB|RF_SDMRSTB);

#ifdef CONFIG_USB_HCI
	rtw_write8(Adapter, REG_SYS_FUNC_EN, FEN_USBA | FEN_USBD | FEN_BB_GLB_RSTn | FEN_BBRSTB);
#else
	rtw_write8(Adapter, REG_SYS_FUNC_EN, FEN_PPLL|FEN_PCIEA|FEN_DIO_PCIE|FEN_BB_GLB_RSTn|FEN_BBRSTB);
#endif

#ifdef CONFIG_USB_HCI
	//To Fix MAC loopback mode fail. Suggested by SD4 Johnny. 2010.03.23.
	rtw_write8(Adapter, REG_LDOHCI12_CTRL, 0x0f);	
	rtw_write8(Adapter, 0x15, 0xe9);
#endif

	rtw_write8(Adapter, REG_AFE_XTAL_CTRL+1, 0x80);

#ifdef CONFIG_PCI_HCI
	// Force use left antenna by default for 88C.
	if(Adapter->ledpriv.LedStrategy != SW_LED_MODE10)
	{
		RegVal = rtw_read32(Adapter, REG_LEDCFG0);
		rtw_write32(Adapter, REG_LEDCFG0, RegVal|BIT23);
	}
#endif

	//
	// Config BB and AGC
	//
	rtStatus = phy_BB8192D_Config_ParaFile(Adapter);

#if MP_DRIVER == 1
	PHY_SetBBReg(Adapter, 0x24, 0xF0, pHalData->CrystalCap & 0x0F);
	PHY_SetBBReg(Adapter, 0x28, 0xF0000000, ((pHalData->CrystalCap & 0xF0) >> 4));
#endif

	return rtStatus;
}


extern	int
PHY_RFConfig8192D(
	IN	PADAPTER	Adapter
	)
{
	//HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	int		rtStatus = _SUCCESS;

	if(Adapter->bSurpriseRemoved){
		rtStatus = _FAIL;
		return rtStatus;
	}

	//
	// RF config
	//
	rtStatus = PHY_RF6052_Config8192D(Adapter);
#if 0	
	switch(pHalData->rf_chip)
	{
		case RF_6052:
			rtStatus = PHY_RF6052_Config(Adapter);
			break;
		case RF_8225:
			rtStatus = PHY_RF8225_Config(Adapter);
			break;
		case RF_8256:			
			rtStatus = PHY_RF8256_Config(Adapter);
			break;
		case RF_8258:
			break;
		case RF_PSEUDO_11N:
			rtStatus = PHY_RF8225_Config(Adapter);
			break;
		default: //for MacOs Warning: "RF_TYPE_MIN" not handled in switch
			break;
	}
#endif
	return rtStatus;
}


/*-----------------------------------------------------------------------------
 * Function:    PHY_ConfigRFWithParaFile()
 *
 * Overview:    This function read RF parameters from general file format, and do RF 3-wire
 *
 * Input:      	PADAPTER			Adapter
 *			ps1Byte 				pFileName			
 *			RF_RADIO_PATH_E	eRFPath
 *
 * Output:      NONE
 *
 * Return:      RT_STATUS_SUCCESS: configuration file exist
 *			
 * Note:		Delay may be required for RF configuration
 *---------------------------------------------------------------------------*/
int
rtl8192d_PHY_ConfigRFWithParaFile(
	IN	PADAPTER			Adapter,
	IN	u8* 				pFileName,
	RF_RADIO_PATH_E		eRFPath
)
{
	int	rtStatus = _SUCCESS;


	return rtStatus;
	
}

//****************************************
/*-----------------------------------------------------------------------------
 * Function:    PHY_ConfigRFWithHeaderFile()
 *
 * Overview:    This function read RF parameters from general file format, and do RF 3-wire
 *
 * Input:      	PADAPTER			Adapter
 *			ps1Byte 				pFileName			
 *			RF_RADIO_PATH_E	eRFPath
 *
 * Output:      NONE
 *
 * Return:      RT_STATUS_SUCCESS: configuration file exist
 *			
 * Note:		Delay may be required for RF configuration
 *---------------------------------------------------------------------------*/
int
rtl8192d_PHY_ConfigRFWithHeaderFile(
	IN	PADAPTER			Adapter,
	RF_CONTENT				Content,
	RF_RADIO_PATH_E		eRFPath
)
{
	int	i, j;
	int	rtStatus = _SUCCESS;
	u32*	Rtl819XRadioA_Array_Table;
	u32*	Rtl819XRadioB_Array_Table;
	u16		RadioA_ArrayLen,RadioB_ArrayLen;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	u32	MaskforPhySet= (u32)(Content&0xE000);

	Content &= 0x1FFF;

	DBG_871X(" ===> PHY_ConfigRFWithHeaderFile() intferace = %d, Radio_txt = 0x%x, eRFPath = %d,MaskforPhyAccess:0x%x.\n", pHalData->interfaceIndex, Content,eRFPath,MaskforPhySet);

	RadioA_ArrayLen = Rtl8192D_RadioA_2TArrayLength;
	Rtl819XRadioA_Array_Table = (u32 *)Rtl8192D_RadioA_2TArray;
	RadioB_ArrayLen = Rtl8192D_RadioB_2TArrayLength;	
	Rtl819XRadioB_Array_Table = (u32 *)Rtl8192D_RadioB_2TArray;

	if(pHalData->InternalPA5G[0])
	{
		RadioA_ArrayLen = Rtl8192D_RadioA_2T_intPAArrayLength;
		Rtl819XRadioA_Array_Table = (u32 *)Rtl8192D_RadioA_2T_intPAArray;
	}

	if(pHalData->InternalPA5G[1])
	{
		RadioB_ArrayLen = Rtl8192D_RadioB_2T_intPAArrayLength;
		Rtl819XRadioB_Array_Table = (u32 *)Rtl8192D_RadioB_2T_intPAArray;
	}				
	
	//RT_TRACE(COMP_INIT, DBG_LOUD, (" ===> PHY_ConfigRFWithHeaderFile() Radio_A:Rtl819XRadioA_1TArray\n"));
	//RT_TRACE(COMP_INIT, DBG_LOUD, (" ===> PHY_ConfigRFWithHeaderFile() Radio_B:Rtl819XRadioB_1TArray\n"));

	//RT_TRACE(COMP_INIT, DBG_TRACE, ("PHY_ConfigRFWithHeaderFile: Radio No %x\n", eRFPath));
	rtStatus = _SUCCESS;

	//vivi added this for read parameter from header, 20100908
	//1this only happens when DMDP, mac0 start on 2.4G, mac1 start on 5G, 
	//1mac 0 has to set phy0&phy1 pathA or mac1 has to set phy0&phy1 pathA
	if((Content == radiob_txt)&&(eRFPath == RF_PATH_A))
	{
		//RT_TRACE(COMP_INIT, DBG_LOUD, (" ===> PHY_ConfigRFWithHeaderFile(), althougth Path A, we load radiob.txt\n"));
		RadioA_ArrayLen = RadioB_ArrayLen;
		Rtl819XRadioA_Array_Table = Rtl819XRadioB_Array_Table;
	}

	switch(eRFPath){
		case RF_PATH_A:
			for(i = 0;i<RadioA_ArrayLen; i=i+2)
			{
				if(Rtl819XRadioA_Array_Table[i] == 0xfe)
				{
					#ifdef CONFIG_LONG_DELAY_ISSUE
					rtw_msleep_os(50);
					#else
					rtw_mdelay_os(50);
					#endif
				}
				else if (Rtl819XRadioA_Array_Table[i] == 0xfd)
				{
					//rtw_mdelay_os(5);
					for(j=0;j<100;j++)
						rtw_udelay_os(MAX_STALL_TIME);
				}
				else if (Rtl819XRadioA_Array_Table[i] == 0xfc)
				{
					//rtw_mdelay_os(1);
					for(j=0;j<20;j++)
						rtw_udelay_os(MAX_STALL_TIME);
				}
				else if (Rtl819XRadioA_Array_Table[i] == 0xfb)
				{
					rtw_udelay_os(50);
				}
				else if (Rtl819XRadioA_Array_Table[i] == 0xfa)
				{
					rtw_udelay_os(5);
				}
				else if (Rtl819XRadioA_Array_Table[i] == 0xf9)
				{
					rtw_udelay_os(1);
				}
				else
				{
					PHY_SetRFReg(Adapter, eRFPath, Rtl819XRadioA_Array_Table[i]|MaskforPhySet, bRFRegOffsetMask, Rtl819XRadioA_Array_Table[i+1]);
					// Add 1us delay between BB/RF register setting.
					rtw_udelay_os(1);
				}
			}
			break;
		case RF_PATH_B:
			for(i = 0;i<RadioB_ArrayLen; i=i+2)
			{
				if(Rtl819XRadioB_Array_Table[i] == 0xfe)
				{ // Deay specific ms. Only RF configuration require delay.												
					#ifdef CONFIG_LONG_DELAY_ISSUE
					rtw_msleep_os(50);
					#else
					rtw_mdelay_os(50);
					#endif
				}
				else if (Rtl819XRadioB_Array_Table[i] == 0xfd)
				{
					//rtw_mdelay_os(5);
					for(j=0;j<100;j++)
						rtw_udelay_os(MAX_STALL_TIME);
				}
				else if (Rtl819XRadioB_Array_Table[i] == 0xfc)
				{
					//rtw_mdelay_os(1);
					for(j=0;j<20;j++)
						rtw_udelay_os(MAX_STALL_TIME);
				}
				else if (Rtl819XRadioB_Array_Table[i] == 0xfb)
				{
					rtw_udelay_os(50);
				}
				else if (Rtl819XRadioB_Array_Table[i] == 0xfa)
				{
					rtw_udelay_os(5);
				}
				else if (Rtl819XRadioB_Array_Table[i] == 0xf9)
				{
					rtw_udelay_os(1);
				}
				else
				{
					PHY_SetRFReg(Adapter, eRFPath, Rtl819XRadioB_Array_Table[i]|MaskforPhySet, bRFRegOffsetMask, Rtl819XRadioB_Array_Table[i+1]);
					// Add 1us delay between BB/RF register setting.
					rtw_udelay_os(1);
				}
			}			
			break;
		case RF_PATH_C:
			break;
		case RF_PATH_D:
			break;
	}
	
	return _SUCCESS;

}


/*-----------------------------------------------------------------------------
 * Function:    PHY_CheckBBAndRFOK()
 *
 * Overview:    This function is write register and then readback to make sure whether
 *			  BB[PHY0, PHY1], RF[Patha, path b, path c, path d] is Ok
 *
 * Input:      	PADAPTER			Adapter
 *			HW90_BLOCK_E		CheckBlock
 *			RF_RADIO_PATH_E	eRFPath		// it is used only when CheckBlock is HW90_BLOCK_RF
 *
 * Output:      NONE
 *
 * Return:      RT_STATUS_SUCCESS: PHY is OK
 *			
 * Note:		This function may be removed in the ASIC
 *---------------------------------------------------------------------------*/
int
rtl8192d_PHY_CheckBBAndRFOK(
	IN	PADAPTER			Adapter,
	IN	HW90_BLOCK_E		CheckBlock,
	IN	RF_RADIO_PATH_E	eRFPath
	)
{
	int			rtStatus = _SUCCESS;

	u32				i, CheckTimes = 4,ulRegRead=0;

	u32				WriteAddr[4];
	u32				WriteData[] = {0xfffff027, 0xaa55a02f, 0x00000027, 0x55aa502f};

	// Initialize register address offset to be checked
	WriteAddr[HW90_BLOCK_MAC] = 0x100;
	WriteAddr[HW90_BLOCK_PHY0] = 0x900;
	WriteAddr[HW90_BLOCK_PHY1] = 0x800;
	WriteAddr[HW90_BLOCK_RF] = 0x3;
	
	for(i=0 ; i < CheckTimes ; i++)
	{

		//
		// Write Data to register and readback
		//
		switch(CheckBlock)
		{
		case HW90_BLOCK_MAC:
			//RT_ASSERT(FALSE, ("PHY_CheckBBRFOK(): Never Write 0x100 here!"));
			//RT_TRACE(COMP_INIT, DBG_LOUD, ("PHY_CheckBBRFOK(): Never Write 0x100 here!\n"));
			break;
			
		case HW90_BLOCK_PHY0:
		case HW90_BLOCK_PHY1:
			rtw_write32(Adapter, WriteAddr[CheckBlock], WriteData[i]);
			ulRegRead = rtw_read32(Adapter, WriteAddr[CheckBlock]);
			break;

		case HW90_BLOCK_RF:
			// When initialization, we want the delay function(delay_ms(), delay_us() 
			// ==> actually we call PlatformStallExecution()) to do NdisStallExecution()
			// [busy wait] instead of NdisMSleep(). So we acquire RT_INITIAL_SPINLOCK 
			// to run at Dispatch level to achive it.	
			//cosa PlatformAcquireSpinLock(Adapter, RT_INITIAL_SPINLOCK);
			WriteData[i] &= 0xfff;
			PHY_SetRFReg(Adapter, eRFPath, WriteAddr[HW90_BLOCK_RF], bRFRegOffsetMask, WriteData[i]);
			// TODO: we should not delay for such a long time. Ask SD3
			rtw_mdelay_os(10);
			ulRegRead = PHY_QueryRFReg(Adapter, eRFPath, WriteAddr[HW90_BLOCK_RF], bRFRegOffsetMask);				
			rtw_mdelay_os(10);
			//cosa PlatformReleaseSpinLock(Adapter, RT_INITIAL_SPINLOCK);
			break;
			
		default:
			rtStatus = _FAIL;
			break;
		}


		//
		// Check whether readback data is correct
		//
		if(ulRegRead != WriteData[i])
		{
			//RT_TRACE(COMP_FPGA, DBG_LOUD, ("ulRegRead: %lx, WriteData: %lx \n", ulRegRead, WriteData[i]));
			rtStatus = _FAIL;			
			break;
		}
	}

	return rtStatus;
}


VOID
rtl8192d_PHY_GetHWRegOriginalValue(
	IN	PADAPTER		Adapter
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	
	// read rx initial gain
	pHalData->DefaultInitialGain[0] = (u8)PHY_QueryBBReg(Adapter, rOFDM0_XAAGCCore1, bMaskByte0);
	pHalData->DefaultInitialGain[1] = (u8)PHY_QueryBBReg(Adapter, rOFDM0_XBAGCCore1, bMaskByte0);
	pHalData->DefaultInitialGain[2] = (u8)PHY_QueryBBReg(Adapter, rOFDM0_XCAGCCore1, bMaskByte0);
	pHalData->DefaultInitialGain[3] = (u8)PHY_QueryBBReg(Adapter, rOFDM0_XDAGCCore1, bMaskByte0);
	//RT_TRACE(COMP_INIT, DBG_LOUD,
	//("Default initial gain (c50=0x%x, c58=0x%x, c60=0x%x, c68=0x%x) \n", 
	//pHalData->DefaultInitialGain[0], pHalData->DefaultInitialGain[1], 
	//pHalData->DefaultInitialGain[2], pHalData->DefaultInitialGain[3]));

	// read framesync
	pHalData->framesync = (u8)PHY_QueryBBReg(Adapter, rOFDM0_RxDetector3, bMaskByte0);
	pHalData->framesyncC34 = PHY_QueryBBReg(Adapter, rOFDM0_RxDetector2, bMaskDWord);
	//RT_TRACE(COMP_INIT, DBG_LOUD, ("Default framesync (0x%x) = 0x%x \n", 
	//	rOFDM0_RxDetector3, pHalData->framesync));
}


//
//	Description:
//		Map dBm into Tx power index according to 
//		current HW model, for example, RF and PA, and
//		current wireless mode.
//	By Bruce, 2008-01-29.
//
static	u8
phy_DbmToTxPwrIdx(
	IN	PADAPTER		Adapter,
	IN	WIRELESS_MODE	WirelessMode,
	IN	int			PowerInDbm
	)
{
	u8				TxPwrIdx = 0;
	int				Offset = 0;
	

	//
	// Tested by MP, we found that CCK Index 0 equals to 8dbm, OFDM legacy equals to 
	// 3dbm, and OFDM HT equals to 0dbm repectively.
	// Note:
	//	The mapping may be different by different NICs. Do not use this formula for what needs accurate result.  
	// By Bruce, 2008-01-29.
	// 
	switch(WirelessMode)
	{
	case WIRELESS_MODE_B:
		Offset = -7;
		break;

	case WIRELESS_MODE_G:
	case WIRELESS_MODE_N_24G:
		Offset = -8;
		break;

	default:
		break;
	}

	if((PowerInDbm - Offset) > 0)
	{
		TxPwrIdx = (u8)((PowerInDbm - Offset) * 2);
	}
	else
	{
		TxPwrIdx = 0;
	}

	// Tx Power Index is too large.
	if(TxPwrIdx > MAX_TXPWR_IDX_NMODE_92S)
		TxPwrIdx = MAX_TXPWR_IDX_NMODE_92S;

	return TxPwrIdx;
}


//
//	Description:
//		Map Tx power index into dBm according to 
//		current HW model, for example, RF and PA, and
//		current wireless mode.
//	By Bruce, 2008-01-29.
//
static int
phy_TxPwrIdxToDbm(
	IN	PADAPTER		Adapter,
	IN	WIRELESS_MODE	WirelessMode,
	IN	u8			TxPwrIdx
	)
{
	int				Offset = 0;
	int				PwrOutDbm = 0;
	
	//
	// Tested by MP, we found that CCK Index 0 equals to -7dbm, OFDM legacy equals to -8dbm.
	// Note:
	//	The mapping may be different by different NICs. Do not use this formula for what needs accurate result.  
	// By Bruce, 2008-01-29.
	// 
	switch(WirelessMode)
	{
	case WIRELESS_MODE_B:
		Offset = -7;
		break;

	case WIRELESS_MODE_G:
	case WIRELESS_MODE_N_24G:
		Offset = -8;
		break;

	default:
		break;
	}

	PwrOutDbm = TxPwrIdx / 2 + Offset; // Discard the decimal part.

	return PwrOutDbm;
}


/*-----------------------------------------------------------------------------
 * Function:    GetTxPowerLevel8190()
 *
 * Overview:    This function is export to "common" moudule
 *
 * Input:       PADAPTER		Adapter
 *			psByte			Power Level
 *
 * Output:      NONE
 *
 * Return:      NONE
 *
 *---------------------------------------------------------------------------*/
VOID
PHY_GetTxPowerLevel8192D(
	IN	PADAPTER		Adapter,
	OUT u32*    		powerlevel
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	u8			TxPwrLevel = 0;
	int			TxPwrDbm;
	
	//
	// Because the Tx power indexes are different, we report the maximum of them to 
	// meet the CCX TPC request. By Bruce, 2008-01-31.
	//

	// CCK
	TxPwrLevel = pHalData->CurrentCckTxPwrIdx;
	TxPwrDbm = phy_TxPwrIdxToDbm(Adapter, WIRELESS_MODE_B, TxPwrLevel);

	// Legacy OFDM
	TxPwrLevel = pHalData->CurrentOfdm24GTxPwrIdx + pHalData->LegacyHTTxPowerDiff;

	// Compare with Legacy OFDM Tx power.
	if(phy_TxPwrIdxToDbm(Adapter, WIRELESS_MODE_G, TxPwrLevel) > TxPwrDbm)
		TxPwrDbm = phy_TxPwrIdxToDbm(Adapter, WIRELESS_MODE_G, TxPwrLevel);

	// HT OFDM
	TxPwrLevel = pHalData->CurrentOfdm24GTxPwrIdx;
	
	// Compare with HT OFDM Tx power.
	if(phy_TxPwrIdxToDbm(Adapter, WIRELESS_MODE_N_24G, TxPwrLevel) > TxPwrDbm)
		TxPwrDbm = phy_TxPwrIdxToDbm(Adapter, WIRELESS_MODE_N_24G, TxPwrLevel);

	*powerlevel = TxPwrDbm;
}


static void getTxPowerIndex(
	IN	PADAPTER		Adapter,
	IN	u8			channel,
	IN OUT u8*		cckPowerLevel,
	IN OUT u8*		ofdmPowerLevel
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	u8	index = (channel -1);

	// 1. CCK
	if(pHalData->CurrentBandType92D == BAND_ON_2_4G)
	{
		cckPowerLevel[RF_PATH_A] = pHalData->TxPwrLevelCck[RF_PATH_A][index];	//RF-A
		cckPowerLevel[RF_PATH_B] = pHalData->TxPwrLevelCck[RF_PATH_B][index];	//RF-B
	}
	else
		cckPowerLevel[RF_PATH_A] = cckPowerLevel[RF_PATH_B] = 0;

	// 2. OFDM for 1S or 2S
	if (GET_RF_TYPE(Adapter) == RF_1T2R || GET_RF_TYPE(Adapter) == RF_1T1R)
	{
		// Read HT 40 OFDM TX power
		ofdmPowerLevel[RF_PATH_A] = pHalData->TxPwrLevelHT40_1S[RF_PATH_A][index];
		ofdmPowerLevel[RF_PATH_B] = pHalData->TxPwrLevelHT40_1S[RF_PATH_B][index];
	}
	else if (GET_RF_TYPE(Adapter) == RF_2T2R)
	{
		// Read HT 40 OFDM TX power
		ofdmPowerLevel[RF_PATH_A] = pHalData->TxPwrLevelHT40_2S[RF_PATH_A][index];
		ofdmPowerLevel[RF_PATH_B] = pHalData->TxPwrLevelHT40_2S[RF_PATH_B][index];
	}
	//RTPRINT(FPHY, PHY_TXPWR, ("Channel-%d, set tx power index !!\n", channel));
}

static void ccxPowerIndexCheck(
	IN	PADAPTER		Adapter,
	IN	u8			channel,
	IN OUT u8*		cckPowerLevel,
	IN OUT u8*		ofdmPowerLevel
	)
{
#if 0
	PMGNT_INFO			pMgntInfo = &(Adapter->MgntInfo);
	HAL_DATA_TYPE		*pHalData = GET_HAL_DATA(Adapter);
	PRT_CCX_INFO		pCcxInfo = GET_CCX_INFO(pMgntInfo);

	//
	// CCX 2 S31, AP control of client transmit power:
	// 1. We shall not exceed Cell Power Limit as possible as we can.
	// 2. Tolerance is +/- 5dB.
	// 3. 802.11h Power Contraint takes higher precedence over CCX Cell Power Limit.
	// 
	// TODO: 
	// 1. 802.11h power contraint 
	//
	// 071011, by rcnjko.
	//
	if(	pMgntInfo->OpMode == RT_OP_MODE_INFRASTRUCTURE && 
		pMgntInfo->mAssoc &&
		pCcxInfo->bUpdateCcxPwr &&
		pCcxInfo->bWithCcxCellPwr &&
		channel == pMgntInfo->dot11CurrentChannelNumber)
	{
		u1Byte	CckCellPwrIdx = phy_DbmToTxPwrIdx(Adapter, WIRELESS_MODE_B, pCcxInfo->CcxCellPwr);
		u1Byte	LegacyOfdmCellPwrIdx = phy_DbmToTxPwrIdx(Adapter, WIRELESS_MODE_G, pCcxInfo->CcxCellPwr);
		u1Byte	OfdmCellPwrIdx = phy_DbmToTxPwrIdx(Adapter, WIRELESS_MODE_N_24G, pCcxInfo->CcxCellPwr);

		RT_TRACE(COMP_TXAGC, DBG_LOUD, 
		("CCX Cell Limit: %d dbm => CCK Tx power index : %d, Legacy OFDM Tx power index : %d, OFDM Tx power index: %d\n", 
		pCcxInfo->CcxCellPwr, CckCellPwrIdx, LegacyOfdmCellPwrIdx, OfdmCellPwrIdx));
		RT_TRACE(COMP_TXAGC, DBG_LOUD, 
		("EEPROM channel(%d) => CCK Tx power index: %d, Legacy OFDM Tx power index : %d, OFDM Tx power index: %d\n",
		channel, cckPowerLevel[0], ofdmPowerLevel[0] + pHalData->LegacyHTTxPowerDiff, ofdmPowerLevel[0])); 

		// CCK
		if(cckPowerLevel[0] > CckCellPwrIdx)
			cckPowerLevel[0] = CckCellPwrIdx;
		// Legacy OFDM, HT OFDM
		if(ofdmPowerLevel[0] + pHalData->LegacyHTTxPowerDiff > LegacyOfdmCellPwrIdx)
		{
			if((OfdmCellPwrIdx - pHalData->LegacyHTTxPowerDiff) > 0)
			{
				ofdmPowerLevel[0] = OfdmCellPwrIdx - pHalData->LegacyHTTxPowerDiff;
			}
			else
			{
				ofdmPowerLevel[0] = 0;
			}
		}

		RT_TRACE(COMP_TXAGC, DBG_LOUD, 
		("Altered CCK Tx power index : %d, Legacy OFDM Tx power index: %d, OFDM Tx power index: %d\n", 
		cckPowerLevel[0], ofdmPowerLevel[0] + pHalData->LegacyHTTxPowerDiff, ofdmPowerLevel[0]));
	}

	pHalData->CurrentCckTxPwrIdx = cckPowerLevel[0];
	pHalData->CurrentOfdm24GTxPwrIdx = ofdmPowerLevel[0];

	RT_TRACE(COMP_TXAGC, DBG_LOUD, 
		("PHY_SetTxPowerLevel8192S(): CCK Tx power index : %d, Legacy OFDM Tx power index: %d, OFDM Tx power index: %d\n", 
		cckPowerLevel[0], ofdmPowerLevel[0] + pHalData->LegacyHTTxPowerDiff, ofdmPowerLevel[0]));
#endif	
}
/*-----------------------------------------------------------------------------
 * Function:    SetTxPowerLevel8190()
 *
 * Overview:    This function is export to "HalCommon" moudule
 *			We must consider RF path later!!!!!!!
 *
 * Input:       PADAPTER		Adapter
 *			u1Byte		channel
 *
 * Output:      NONE
 *
 * Return:      NONE
 *	2008/11/04	MHC		We remove EEPROM_93C56.
 *						We need to move CCX relative code to independet file.
 *	2009/01/21	MHC		Support new EEPROM format from SD3 requirement.
 *
 *---------------------------------------------------------------------------*/
VOID
PHY_SetTxPowerLevel8192D(
	IN	PADAPTER		Adapter,
	IN	u8			channel
	)
{
	HAL_DATA_TYPE		*pHalData = GET_HAL_DATA(Adapter);
	u8	cckPowerLevel[2], ofdmPowerLevel[2];	// [0]:RF-A, [1]:RF-B

#if(MP_DRIVER == 1)
	return;
#endif

#ifdef CONFIG_USB_HCI
	if((Adapter->mlmeextpriv.sitesurvey_res.state == SCAN_PROCESS)&&(Adapter->dvobjpriv.ishighspeed == _FALSE))
		return;
#endif

	if(pHalData->bTXPowerDataReadFromEEPORM == _FALSE)
		return;

	channel = GetRightChnlPlace(channel);

	getTxPowerIndex(Adapter, channel, &cckPowerLevel[0], &ofdmPowerLevel[0]);
	//DBG_8192C("Channel-%d, cckPowerLevel (A / B) = 0x%x / 0x%x,   ofdmPowerLevel (A / B) = 0x%x / 0x%x\n", 
	//	channel, cckPowerLevel[0], cckPowerLevel[1], ofdmPowerLevel[0], ofdmPowerLevel[1]);

	if(pHalData->CurrentBandType92D == BAND_ON_2_4G)
		ccxPowerIndexCheck(Adapter, channel, &cckPowerLevel[0], &ofdmPowerLevel[0]);

	if(pHalData->CurrentBandType92D == BAND_ON_2_4G)
		rtl8192d_PHY_RF6052SetCckTxPower(Adapter, &cckPowerLevel[0]);
	rtl8192d_PHY_RF6052SetOFDMTxPower(Adapter, &ofdmPowerLevel[0], channel);

#if 0
	switch(pHalData->rf_chip)
	{
		case RF_8225:
			PHY_SetRF8225CckTxPower(Adapter, cckPowerLevel[0]);
			PHY_SetRF8225OfdmTxPower(Adapter, ofdmPowerLevel[0]);
		break;

		case RF_8256:
			PHY_SetRF8256CCKTxPower(Adapter, cckPowerLevel[0]);
			PHY_SetRF8256OFDMTxPower(Adapter, ofdmPowerLevel[0]);
			break;

		case RF_6052:
			PHY_RF6052SetCckTxPower(Adapter, &cckPowerLevel[0]);
			PHY_RF6052SetOFDMTxPower(Adapter, &ofdmPowerLevel[0], channel);
			break;

		case RF_8258:
			break;
	}
#endif

}


//
//	Description:
//		Update transmit power level of all channel supported.
//
//	TODO: 
//		A mode.
//	By Bruce, 2008-02-04.
//
BOOLEAN
PHY_UpdateTxPowerDbm8192D(
	IN	PADAPTER	Adapter,
	IN	int		powerInDbm
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	u8	idx;
	u8	rf_path;

	// TODO: A mode Tx power.
	u8	CckTxPwrIdx = phy_DbmToTxPwrIdx(Adapter, WIRELESS_MODE_B, powerInDbm);
	u8	OfdmTxPwrIdx = phy_DbmToTxPwrIdx(Adapter, WIRELESS_MODE_N_24G, powerInDbm);

	if(OfdmTxPwrIdx - pHalData->LegacyHTTxPowerDiff > 0)
		OfdmTxPwrIdx -= pHalData->LegacyHTTxPowerDiff;
	else
		OfdmTxPwrIdx = 0;

	//RT_TRACE(COMP_TXAGC, DBG_LOUD, ("PHY_UpdateTxPowerDbm8192S(): %ld dBm , CckTxPwrIdx = %d, OfdmTxPwrIdx = %d\n", powerInDbm, CckTxPwrIdx, OfdmTxPwrIdx));

	for(idx = 0; idx < CHANNEL_MAX_NUMBER; idx++)
	{
		for (rf_path = 0; rf_path < 2; rf_path++)
		{
			if(idx < CHANNEL_MAX_NUMBER_2G)
				pHalData->TxPwrLevelCck[rf_path][idx] = CckTxPwrIdx;
			pHalData->TxPwrLevelHT40_1S[rf_path][idx] = 
			pHalData->TxPwrLevelHT40_2S[rf_path][idx] = OfdmTxPwrIdx;
		}
	}

	//Adapter->HalFunc.SetTxPowerLevelHandler(Adapter, pHalData->CurrentChannel);//gtest:todo

	return _TRUE;	
}


/*
	Description:
		When beacon interval is changed, the values of the 
		hw registers should be modified.
	By tynli, 2008.10.24.

*/


void	
rtl8192d_PHY_SetBeaconHwReg(	
	IN	PADAPTER		Adapter,
	IN	u16			BeaconInterval	
	)
{

}


VOID 
PHY_ScanOperationBackup8192D(
	IN	PADAPTER	Adapter,
	IN	u8		Operation
	)
{
#if 0
	IO_TYPE	IoType;
	
	if(!Adapter->bDriverStopped)
	{
		switch(Operation)
		{
			case SCAN_OPT_BACKUP:
				IoType = IO_CMD_PAUSE_DM_BY_SCAN;
				Adapter->HalFunc.SetHwRegHandler(Adapter,HW_VAR_IO_CMD,  (pu1Byte)&IoType);

				break;

			case SCAN_OPT_RESTORE:
				IoType = IO_CMD_RESUME_DM_BY_SCAN;
				Adapter->HalFunc.SetHwRegHandler(Adapter,HW_VAR_IO_CMD,  (pu1Byte)&IoType);
				break;

			default:
				RT_TRACE(COMP_SCAN, DBG_LOUD, ("Unknown Scan Backup Operation. \n"));
				break;
		}
	}
#endif	
}

/*-----------------------------------------------------------------------------
 * Function:    PHY_SetBWModeCallback8192C()
 *
 * Overview:    Timer callback function for SetSetBWMode
 *
 * Input:       	PRT_TIMER		pTimer
 *
 * Output:      NONE
 *
 * Return:      NONE
 *
 * Note:		(1) We do not take j mode into consideration now
 *			(2) Will two workitem of "switch channel" and "switch channel bandwidth" run
 *			     concurrently?
 *---------------------------------------------------------------------------*/
static VOID
_PHY_SetBWMode92D(
	IN	PADAPTER	Adapter
)
{
	HAL_DATA_TYPE		*pHalData = GET_HAL_DATA(Adapter);
	u8	regBwOpMode;
	u8	regRRSR_RSC;

#ifdef CONFIG_DUALMAC_CONCURRENT
	// FOr 92D dual mac config.
	PADAPTER	BuddyAdapter = Adapter->pbuddy_adapter;
	PHAL_DATA_TYPE	pHalDataBuddyAdapter;
#endif

	//DBG_8192C("==>[%d]: _PHY_SetBWMode92D()  Switch to %s bandwidth\n", pHalData->interfaceIndex, pHalData->CurrentChannelBW == HT_CHANNEL_WIDTH_20?"20MHz":"40MHz");

	if(pHalData->rf_chip == RF_PSEUDO_11N)
	{
		//pHalData->SetBWModeInProgress= _FALSE;
		return;
	}

	// There is no 40MHz mode in RF_8225.
	if(pHalData->rf_chip==RF_8225)
		return;

	if(Adapter->bDriverStopped)
		return;

		
	//3//
	//3//<1>Set MAC register
	//3//
	//Adapter->HalFunc.SetBWModeHandler();
	
	regBwOpMode = rtw_read8(Adapter, REG_BWOPMODE);
	regRRSR_RSC = rtw_read8(Adapter, REG_RRSR+2);
	//regBwOpMode = Adapter->HalFunc.GetHwRegHandler(Adapter,HW_VAR_BWMODE,(pu1Byte)&regBwOpMode);
	
	switch(pHalData->CurrentChannelBW)
	{
		case HT_CHANNEL_WIDTH_20:
			regBwOpMode |= BW_OPMODE_20MHZ;
			   // 2007/02/07 Mark by Emily becasue we have not verify whether this register works
			rtw_write8(Adapter, REG_BWOPMODE, regBwOpMode);
			break;
			   
		case HT_CHANNEL_WIDTH_40:
			regBwOpMode &= ~BW_OPMODE_20MHZ;
				// 2007/02/07 Mark by Emily becasue we have not verify whether this register works
			rtw_write8(Adapter, REG_BWOPMODE, regBwOpMode);

			regRRSR_RSC = (regRRSR_RSC&0x90) |(pHalData->nCur40MhzPrimeSC<<5);
			rtw_write8(Adapter, REG_RRSR+2, regRRSR_RSC);
			break;

		default:
			/*RT_TRACE(COMP_DBG, DBG_LOUD, ("PHY_SetBWModeCallback8192C():
						unknown Bandwidth: %#X\n",pHalData->CurrentChannelBW));*/
			break;
	}
	
	//3//
	//3//<2>Set PHY related register
	//3//
	switch(pHalData->CurrentChannelBW)
	{
		/* 20 MHz channel*/
		case HT_CHANNEL_WIDTH_20:
			PHY_SetBBReg1Byte(Adapter, rFPGA0_RFMOD, bRFMOD, 0x0);

			PHY_SetBBReg(Adapter, rFPGA1_RFMOD, bRFMOD, 0x0);

			PHY_SetBBReg(Adapter, rFPGA0_AnalogParameter2, BIT10|BIT11, 3);// SET BIT10 BIT11  for receive cck

			break;

		/* 40 MHz channel*/
		case HT_CHANNEL_WIDTH_40:
			PHY_SetBBReg1Byte(Adapter, rFPGA0_RFMOD, bRFMOD, 0x1);

			PHY_SetBBReg(Adapter, rFPGA1_RFMOD, bRFMOD, 0x1);

			// Set Control channel to upper or lower. These settings are required only for 40MHz
			if(pHalData->CurrentBandType92D == BAND_ON_2_4G)
			{
				//AcquireCCKAndRWPageAControl(Adapter);
				PHY_SetBBReg(Adapter, rCCK0_System, bCCKSideBand, (pHalData->nCur40MhzPrimeSC>>1));
				//ReleaseCCKAndRWPageAControl(Adapter);
			}
			PHY_SetBBReg(Adapter, rOFDM1_LSTF, 0xC00, pHalData->nCur40MhzPrimeSC);

			PHY_SetBBReg(Adapter, rFPGA0_AnalogParameter2, BIT10|BIT11, 0);// SET BIT10 BIT11  for receive cck

			PHY_SetBBReg(Adapter, 0x818, (BIT26|BIT27), (pHalData->nCur40MhzPrimeSC==HAL_PRIME_CHNL_OFFSET_LOWER)?2:1);

			break;

		default:
			/*RT_TRACE(COMP_DBG, DBG_LOUD, ("PHY_SetBWModeCallback8192C(): unknown Bandwidth: %#X\n"\
						,pHalData->CurrentChannelBW));*/
			break;
			
	}

	//3<3>Set RF related register
	switch(pHalData->rf_chip)
	{
		case RF_8225:		
			//PHY_SetRF8225Bandwidth(Adapter, pHalData->CurrentChannelBW);
			break;	
			
		case RF_8256:
			// Please implement this function in Hal8190PciPhy8256.c
			//PHY_SetRF8256Bandwidth(Adapter, pHalData->CurrentChannelBW);
			break;
			
		case RF_8258:
			// Please implement this function in Hal8190PciPhy8258.c
			// PHY_SetRF8258Bandwidth();
			break;

		case RF_PSEUDO_11N:
			// Do Nothing
			break;
			
		case RF_6052:
			rtl8192d_PHY_RF6052SetBandwidth(Adapter, pHalData->CurrentChannelBW);
			break;	
			
		default:
			//RT_ASSERT(FALSE, ("Unknown RFChipID: %d\n", pHalData->RFChipID));
			break;
	}

	//pHalData->SetBWModeInProgress= FALSE;

#ifdef CONFIG_DUALMAC_CONCURRENT
	if(Adapter->DualMacConcurrent == _TRUE && BuddyAdapter != NULL)
	{
		if(pHalData->bMasterOfDMSP)
		{
			pHalDataBuddyAdapter = GET_HAL_DATA(BuddyAdapter);
			pHalDataBuddyAdapter->CurrentChannelBW=pHalData->CurrentChannelBW;
			pHalDataBuddyAdapter->nCur40MhzPrimeSC = pHalData->nCur40MhzPrimeSC;
		}
	}
#endif

	//RT_TRACE(COMP_SCAN, DBG_LOUD, ("<==PHY_SetBWModeCallback8192C() \n" ));
}

 /*-----------------------------------------------------------------------------
 * Function:   SetBWMode8190Pci()
 *
 * Overview:  This function is export to "HalCommon" moudule
 *
 * Input:       	PADAPTER			Adapter
 *			HT_CHANNEL_WIDTH	Bandwidth	//20M or 40M
 *
 * Output:      NONE
 *
 * Return:      NONE
 *
 * Note:		We do not take j mode into consideration now
 *---------------------------------------------------------------------------*/
VOID
PHY_SetBWMode8192D(
	IN	PADAPTER					Adapter,
	IN	HT_CHANNEL_WIDTH	Bandwidth,	// 20M or 40M
	IN	unsigned char	Offset		// Upper, Lower, or Don't care
)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	HT_CHANNEL_WIDTH 	tmpBW= pHalData->CurrentChannelBW;
#ifdef CONFIG_DUALMAC_CONCURRENT
	PADAPTER	BuddyAdapter = Adapter->pbuddy_adapter;
#endif

	//if(pHalData->SetBWModeInProgress)
	//	return;

#ifdef CONFIG_DUALMAC_CONCURRENT
	if(pHalData->bInModeSwitchProcess)
	{
		DBG_871X("PHY_SwChnl8192D(): During mode switch \n");
		//pHalData->SetBWModeInProgress=_FALSE;
		return;
	}
#endif

	//pHalData->SetBWModeInProgress= _TRUE;

	pHalData->CurrentChannelBW = Bandwidth;

#if 0
	if(Offset==HT_EXTCHNL_OFFSET_LOWER)
		pHalData->nCur40MhzPrimeSC = HAL_PRIME_CHNL_OFFSET_UPPER;
	else if(Offset==HT_EXTCHNL_OFFSET_UPPER)
		pHalData->nCur40MhzPrimeSC = HAL_PRIME_CHNL_OFFSET_LOWER;
	else
		pHalData->nCur40MhzPrimeSC = HAL_PRIME_CHNL_OFFSET_DONT_CARE;
#else
	pHalData->nCur40MhzPrimeSC = Offset;
#endif

#ifdef CONFIG_DUALMAC_CONCURRENT
	if((BuddyAdapter !=NULL) && (pHalData->bSlaveOfDMSP))
	{
		//if((BuddyAdapter->MgntInfo.bJoinInProgress) ||(BuddyAdapter->MgntInfo.bScanInProgress))
		{
			DBG_871X("PHY_SetBWMode92D():slave return when slave \n");
			//pHalData->SetBWModeInProgress=_FALSE;
			return;
		}
	}
#endif

	if((!Adapter->bDriverStopped) && (!Adapter->bSurpriseRemoved))
	{
#ifdef USE_WORKITEM	
		//PlatformScheduleWorkItem(&(pHalData->SetBWModeWorkItem));
#else
	#if 0
		//PlatformSetTimer(Adapter, &(pHalData->SetBWModeTimer), 0);
	#else
		_PHY_SetBWMode92D(Adapter);
	#endif
#endif		
	}
	else
	{
		//RT_TRACE(COMP_SCAN, DBG_LOUD, ("PHY_SetBWMode8192C() SetBWModeInProgress FALSE driver sleep or unload\n"));	
		//pHalData->SetBWModeInProgress= FALSE;	
		pHalData->CurrentChannelBW = tmpBW;
	}
	
}


/*******************************************************************
Descriptor:
			stop TRX Before change bandType dynamically	

********************************************************************/
VOID 
PHY_StopTRXBeforeChangeBand8192D(
	  PADAPTER		Adapter
)
{
#if MP_DRIVER == 1
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;

	pdmpriv->RegC04_MP = (u8)PHY_QueryBBReg(Adapter, rOFDM0_TRxPathEnable, bMaskByte0);
	pdmpriv->RegD04_MP = PHY_QueryBBReg(Adapter, rOFDM1_TRxPathEnable, bDWord);
#endif

	PHY_SetBBReg1Byte(Adapter, rFPGA0_RFMOD, bCCKEn|bOFDMEn, 0x00);
	
	PHY_SetBBReg(Adapter, rOFDM0_TRxPathEnable, bMaskByte0, 0x00);
	PHY_SetBBReg(Adapter, rOFDM1_TRxPathEnable, bDWord, 0x0);
}

/*

*/
void
PHY_SwitchWirelessBand(
	IN PADAPTER		 Adapter,
	IN u8		Band);
void
PHY_SwitchWirelessBand(
	IN PADAPTER		 Adapter,
	IN u8		Band)
{		
	HAL_DATA_TYPE	*pHalData	= GET_HAL_DATA(Adapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	u8	i, value8;//, RegValue

#ifdef CONFIG_DUALMAC_CONCURRENT
	if(pHalData->bInModeSwitchProcess || pHalData->bSlaveOfDMSP)
	{
		DBG_871X("PHY_SwitchWirelessBand(): skip for mode switch or slave \n");
		return;
	}
#endif
	//DBG_8192C("PHY_SwitchWirelessBand():Before Switch Band \n");

	pHalData->BandSet92D = pHalData->CurrentBandType92D = (BAND_TYPE)Band;	
	if(IS_92D_SINGLEPHY(pHalData->VersionID))
         	pHalData->BandSet92D = BAND_ON_BOTH;

	if(pHalData->CurrentBandType92D == BAND_ON_5G)
	{
		pHalData->CurrentWirelessMode = WIRELESS_MODE_N_5G;
	}
	else
	{
		pHalData->CurrentWirelessMode = WIRELESS_MODE_N_24G;
	}

#ifdef CONFIG_DUALMAC_CONCURRENT
	if(pHalData->bMasterOfDMSP)
	{
		PADAPTER	BuddyAdapter = Adapter->pbuddy_adapter;
		if(BuddyAdapter!=NULL)
		{
			if(BuddyAdapter->hw_init_completed)
			{
				GET_HAL_DATA(BuddyAdapter)->BandSet92D = pHalData->BandSet92D;
				GET_HAL_DATA(BuddyAdapter)->CurrentBandType92D = pHalData->CurrentBandType92D;
				GET_HAL_DATA(BuddyAdapter)->CurrentWirelessMode = pHalData->CurrentWirelessMode;
			}
		}
	}
#endif

//#ifdef CONFIG_USB_HCI
	//RT_ASSERT((KeGetCurrentIrql() == PASSIVE_LEVEL),
	//	("MPT_ActSetWirelessMode819x(): not in PASSIVE_LEVEL!\n"));
//#endif

	//stop RX/Tx
	PHY_StopTRXBeforeChangeBand8192D(Adapter);

	//reconfig BB/RF according to wireless mode
	if(pHalData->CurrentBandType92D == BAND_ON_2_4G)
	{
		//BB & RF Config
		if(pHalData->interfaceIndex == 1)
		{
#ifdef CONFIG_EMBEDDED_FWIMG
			phy_ConfigBBWithHeaderFile(Adapter, BaseBand_Config_AGC_TAB);
#else
			PHY_SetAGCTab8192D(Adapter);
#endif
		}
		//if( (check_fwstate(&Adapter->mlmepriv, WIFI_ADHOC_STATE|WIFI_ADHOC_MASTER_STATE) == _TRUE) &&
		//	Adapter->mlmeextpriv.cur_channel <= 14)
		//{
		//	ResumeTxBeacon(Adapter);
		//	DBG_8192C("==>PHY_SwitchWirelessBand():Resume send beacon! \n");
		//}
	}
	else	//5G band
	{
		//if( (check_fwstate(&Adapter->mlmepriv, WIFI_ADHOC_STATE|WIFI_ADHOC_MASTER_STATE) == _TRUE) &&
		//	Adapter->mlmeextpriv.cur_channel <= 14)
		//{
		//	StopTxBeacon(Adapter);
		//	DBG_8192C("==>PHY_SwitchWirelessBand():Stop send beacon! \n");
		//}

		if(pHalData->interfaceIndex == 1)
		{
#ifdef CONFIG_EMBEDDED_FWIMG
			phy_ConfigBBWithHeaderFile(Adapter, BaseBand_Config_AGC_TAB);
#else
			PHY_SetAGCTab8192D(Adapter);
#endif
		}
	}
		
	PHY_UpdateBBRFConfiguration8192D(Adapter, _TRUE);

	if(pHalData->CurrentBandType92D == BAND_ON_2_4G)
	{
		rtw_write16(Adapter, REG_RRSR, 0x15d);
	
		PHY_SetBBReg1Byte(Adapter, rFPGA0_RFMOD, bCCKEn|bOFDMEn, 0x3);
	}
	else
	{
		//avoid using cck rate in 5G band
		// Set RRSR rate table.
		rtw_write16(Adapter, REG_RRSR, 0x150);
	
		PHY_SetBBReg1Byte(Adapter, rFPGA0_RFMOD, bCCKEn|bOFDMEn, 0x2);
	}

#ifdef CONFIG_DUALMAC_CONCURRENT
	if(pHalData->bMasterOfDMSP)
	{
		PADAPTER	BuddyAdapter = Adapter->pbuddy_adapter;
		if(BuddyAdapter!=NULL)
		{
			if(pHalData->CurrentBandType92D == BAND_ON_2_4G)
				rtw_write16(BuddyAdapter, REG_RRSR, 0x15d);
			else
				rtw_write16(BuddyAdapter, REG_RRSR, 0x150);
		}
	}
#endif


	pdmpriv->bReloadtxpowerindex = _TRUE;

	// notice fw know band status  0x81[1]/0x53[1] = 0: 5G, 1: 2G
	if(pHalData->CurrentBandType92D==BAND_ON_2_4G)
	{
		value8 = rtw_read8(Adapter, (pHalData->interfaceIndex==0?REG_MAC0:REG_MAC1));
		value8 |= BIT1;
		rtw_write8(Adapter, (pHalData->interfaceIndex==0?REG_MAC0:REG_MAC1),value8);
	}
	else
	{
		value8 = rtw_read8(Adapter, (pHalData->interfaceIndex==0?REG_MAC0:REG_MAC1));
		value8 &= (~BIT1);
		rtw_write8(Adapter, (pHalData->interfaceIndex==0?REG_MAC0:REG_MAC1),value8);	
	}


	for(i=0;i<20;i++)
			rtw_udelay_os(MAX_STALL_TIME);

	//DBG_8192C("PHY_SwitchWirelessBand():Switch Band OK.\n");
}


static VOID
PHY_EnableRFENV(
	IN	PADAPTER		Adapter,	
	IN	u8				eRFPath	,
	IN	u32				MaskforPhySet,
	OUT	u32*			pu4RegValue
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	BB_REGISTER_DEFINITION_T	*pPhyReg = &pHalData->PHYRegDef[eRFPath];

	//RT_TRACE(COMP_RF, DBG_LOUD, ("====>PHY_EnableRFENV\n"));

	/*----Store original RFENV control type----*/		
	switch(eRFPath)
	{
		case RF_PATH_A:
		case RF_PATH_C:
			*pu4RegValue = PHY_QueryBBReg(Adapter, pPhyReg->rfintfs|MaskforPhySet, bRFSI_RFENV);
			break;
		case RF_PATH_B:
		case RF_PATH_D:
			*pu4RegValue = PHY_QueryBBReg(Adapter, pPhyReg->rfintfs|MaskforPhySet, bRFSI_RFENV<<16);
			break;
	}	

	/*----Set RF_ENV enable----*/		
	PHY_SetBBReg(Adapter, pPhyReg->rfintfe|MaskforPhySet, bRFSI_RFENV<<16, 0x1);
	rtw_udelay_os(1);
	
	/*----Set RF_ENV output high----*/
	PHY_SetBBReg(Adapter, pPhyReg->rfintfo|MaskforPhySet, bRFSI_RFENV, 0x1);
	rtw_udelay_os(1);
	
	/* Set bit number of Address and Data for RF register */
	PHY_SetBBReg(Adapter, pPhyReg->rfHSSIPara2|MaskforPhySet, b3WireAddressLength, 0x0);	// Set 1 to 4 bits for 8255
	rtw_udelay_os(1);
	
	PHY_SetBBReg(Adapter, pPhyReg->rfHSSIPara2|MaskforPhySet, b3WireDataLength, 0x0); // Set 0 to 12	bits for 8255
	rtw_udelay_os(1);

	//RT_TRACE(COMP_RF, DBG_LOUD, ("<====PHY_EnableRFENV\n"));	

}

static VOID
PHY_RestoreRFENV(
	IN	PADAPTER		Adapter,	
	IN	u8				eRFPath,
	IN	u32				MaskforPhySet,
	IN	u32*			pu4RegValue
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	BB_REGISTER_DEFINITION_T	*pPhyReg = &pHalData->PHYRegDef[eRFPath];

	//RT_TRACE(COMP_RF, DBG_LOUD, ("=====>PHY_RestoreRFENV\n"));
	//If another MAC is ON,need do this?
	/*----Restore RFENV control type----*/;
	switch(eRFPath)
	{
		case RF_PATH_A:
		case RF_PATH_C:
			PHY_SetBBReg(Adapter, pPhyReg->rfintfs|MaskforPhySet, bRFSI_RFENV, *pu4RegValue);
			break;
		case RF_PATH_B :
		case RF_PATH_D:
			PHY_SetBBReg(Adapter, pPhyReg->rfintfs|MaskforPhySet, bRFSI_RFENV<<16, *pu4RegValue);
			break;
	}
	//RT_TRACE(COMP_RF, DBG_LOUD, ("<=====PHY_RestoreRFENV\n"));	

}


/*-----------------------------------------------------------------------------
 * Function:	phy_SwitchRfSetting
 *
 * Overview:	Change RF Setting when we siwthc channel for 92D C-cut.
 *
 * Input:       IN	PADAPTER				pAdapter
 *
 * Output:      NONE
 *
 * Return:      NONE
 *
 * Revised History:
 * When			Who		Remark
 * 01/08/2009	MHC		Suggestion from SD3 Willis for 92S series.
 * 01/09/2009	MHC		Add CCK modification for 40MHZ. Suggestion from SD3.
 *
 *---------------------------------------------------------------------------*/
 static	VOID	
 phy_SwitchRfSetting(
 	IN	PADAPTER			Adapter,
	IN	u8					channel 	
 	)
{
	HAL_DATA_TYPE		*pHalData = GET_HAL_DATA(Adapter);
	u8			path = pHalData->CurrentBandType92D==BAND_ON_5G?RF_PATH_A:RF_PATH_B;
	u8			index = 0,	i = 0, eRFPath = RF_PATH_A;
	BOOLEAN		bNeedPowerDownRadio = _FALSE, bInteralPA = _FALSE;
	u32			u4RegValue, mask = 0x1C000, value = 0, u4tmp, u4tmp2,MaskforPhySet=0;

	//RT_TRACE(COMP_CMD, DBG_LOUD, ("====>phy_SwitchRfSetting interface %d\n", pHalData->interfaceIndex));

	//only for 92D C-cut SMSP

#ifdef CONFIG_USB_HCI
	if(Adapter->dvobjpriv.ishighspeed == _FALSE)
		return;
#endif

	//config path A for 5G
	if(pHalData->CurrentBandType92D==BAND_ON_5G)
	{
		//RT_TRACE(COMP_CMD, DBG_LOUD, ("====>phy_SwitchRfSetting interface %d 5G\n", Adapter->interfaceIndex));

		u4tmp = CurveIndex[GetRightChnlPlace(channel)-1];
		//RTPRINT(FINIT, INIT_IQK, ("cosa ver 1 set RF-A, 5G, 0x28 = 0x%x !!\n", u4tmp));
	
		for(i = 0; i < RF_CHNL_NUM_5G; i++)
		{
			if(channel == RF_CHNL_5G[i] && channel <= 140)
				index = 0;
		}

		for(i = 0; i < RF_CHNL_NUM_5G_40M; i++)
		{
			if(channel == RF_CHNL_5G_40M[i] && channel <= 140)
				index = 1;
		}

		if(channel ==149 || channel == 155 || channel ==161)
			index = 2;
		else if(channel == 151 || channel == 153 || channel == 163 || channel == 165)
			index = 3;
		else if(channel == 157 || channel == 159 )
			index = 4;

		if(pHalData->MacPhyMode92D == DUALMAC_DUALPHY && pHalData->interfaceIndex == 1)
		{
			bNeedPowerDownRadio = rtl8192d_PHY_EnableAnotherPHY(Adapter, _FALSE);
			MaskforPhySet = MAC1_ACCESS_PHY0;
			//asume no this case
			if(bNeedPowerDownRadio)
			 	PHY_EnableRFENV(Adapter, path, MaskforPhySet, &u4RegValue);
		}

		for(i = 0; i < RF_REG_NUM_for_C_CUT_5G; i++)
		{
#if 1
			if(i == 0 && (pHalData->MacPhyMode92D == DUALMAC_DUALPHY))
			{
				PHY_SetRFReg(Adapter, (RF_RADIO_PATH_E)path, RF_REG_for_C_CUT_5G[i]|MaskforPhySet, bRFRegOffsetMask, 0xE439D);
			}
			else if (RF_REG_for_C_CUT_5G[i] == RF_SYN_G4)
			{
#if SWLCK == 1
				u4tmp2= (RF_REG_Param_for_C_CUT_5G[index][i]&0x7FF)|(u4tmp << 11);

				if(channel == 36)
					u4tmp2 &= ~(BIT7|BIT6);

				PHY_SetRFReg(Adapter, (RF_RADIO_PATH_E)path, RF_REG_for_C_CUT_5G[i]|MaskforPhySet, bRFRegOffsetMask, u4tmp2);
#else
				u4tmp2= RF_REG_Param_for_C_CUT_5G[index][i];
				PHY_SetRFReg(Adapter, (RF_RADIO_PATH_E)path, RF_REG_for_C_CUT_5G[i]|MaskforPhySet, 0xFF8FF, u4tmp2);
#endif
			}
			else
			{
				PHY_SetRFReg(Adapter, (RF_RADIO_PATH_E)path, RF_REG_for_C_CUT_5G[i]|MaskforPhySet, bRFRegOffsetMask, RF_REG_Param_for_C_CUT_5G[index][i]);
			}
#else
			if(i == 0 && (pHalData->MacPhyMode92D == DUALMAC_DUALPHY))
				PHY_SetRFReg(Adapter, (RF_RADIO_PATH_E)path, RF_REG_for_C_CUT_5G[i], RF_REG_MASK_for_C_CUT_5G[i], 0xE439D);
			else
				PHY_SetRFReg(Adapter, (RF_RADIO_PATH_E)path, RF_REG_for_C_CUT_5G[i], RF_REG_MASK_for_C_CUT_5G[i], RF_REG_Param_for_C_CUT_5G[index][i]);
#endif			
			//RT_TRACE(COMP_RF, DBG_TRACE, ("phy_SwitchRfSetting offset 0x%x value 0x%x path %d index %d readback 0x%x\n", 
			//	RF_REG_for_C_CUT_5G[i], RF_REG_Param_for_C_CUT_5G[index][i], path,  index,
			//	PHY_QueryRFReg(Adapter, (RF_RADIO_PATH_E) path, RF_REG_for_C_CUT_5G[i]|MaskforPhyAccess, bRFRegOffsetMask)));
		}
		if(pHalData->MacPhyMode92D == DUALMAC_DUALPHY && pHalData->interfaceIndex == 1)
		{
			if(bNeedPowerDownRadio)
			{
				PHY_RestoreRFENV(Adapter, path,MaskforPhySet, &u4RegValue);	
			}
			rtl8192d_PHY_PowerDownAnotherPHY(Adapter, _FALSE);
		}
		if(channel < 149)
			value = 0x07;
		else if(channel >= 149)
			value = 0x02;

		if(channel >= 36 && channel <= 64)
			index = 0;
		else if(channel >=100 && channel <= 140)
			index = 1;
		else	
			index = 2;

		for(eRFPath = RF_PATH_A; eRFPath < pHalData->NumTotalRFPath; eRFPath++)
		{
			if(pHalData->MacPhyMode92D == DUALMAC_DUALPHY &&
				pHalData->interfaceIndex == 1)		//MAC 1 5G
				bInteralPA = pHalData->InternalPA5G[1];
			else
				bInteralPA = pHalData->InternalPA5G[eRFPath];
			
			if(bInteralPA)
			{
				for(i = 0; i < RF_REG_NUM_for_C_CUT_5G_internalPA; i++)
				{
					if(RF_REG_for_C_CUT_5G_internalPA[i] == 0x03 &&
						channel >=36 && channel <=64)
						PHY_SetRFReg(Adapter, eRFPath, RF_REG_for_C_CUT_5G_internalPA[i], bRFRegOffsetMask, 0x7bdef);
					else
						PHY_SetRFReg(Adapter, eRFPath, RF_REG_for_C_CUT_5G_internalPA[i], bRFRegOffsetMask, RF_REG_Param_for_C_CUT_5G_internalPA[index][i]);
					//RT_TRACE(COMP_RF, DBG_LOUD, ("phy_SwitchRfSetting offset 0x%x value 0x%x path %d index %d \n", 
					//	RF_REG_for_C_CUT_5G_internalPA[i], RF_REG_Param_for_C_CUT_5G_internalPA[index][i], eRFPath,	index));					
				}
			}		
			else
				PHY_SetRFReg(Adapter, (RF_RADIO_PATH_E)eRFPath, 0x0B, mask, value);
		}
	}
	else if(pHalData->CurrentBandType92D==BAND_ON_2_4G)
	{
		//DBG_8192C("====>phy_SwitchRfSetting interface %d 2.4G\n", pHalData->interfaceIndex);
		u4tmp = CurveIndex[channel-1];
		//RTPRINT(FINIT, INIT_IQK, ("cosa ver 3 set RF-B, 2G, 0x28 = 0x%x !!\n", u4tmp));
	
		if(channel == 1 || channel == 2 || channel ==4 || channel == 9 || channel == 10 || 
			channel == 11 || channel ==12)
			index = 0;
		else if(channel ==3 || channel == 13 || channel == 14)
			index = 1;
		else if(channel >= 5 && channel <= 8)
			index = 2;

		if(pHalData->MacPhyMode92D == DUALMAC_DUALPHY)
		{
			path = RF_PATH_A;		
			if(pHalData->interfaceIndex == 0)
			{
				bNeedPowerDownRadio = rtl8192d_PHY_EnableAnotherPHY(Adapter, _TRUE);
				MaskforPhySet = MAC0_ACCESS_PHY1;
				if(bNeedPowerDownRadio)
					PHY_EnableRFENV(Adapter, path,MaskforPhySet,&u4RegValue);
			}
		}


		for(i = 0; i < RF_REG_NUM_for_C_CUT_2G; i++)
		{
#if 1
#if SWLCK == 1
			if (RF_REG_for_C_CUT_2G[i] == RF_SYN_G7)
				PHY_SetRFReg(Adapter, (RF_RADIO_PATH_E)path, RF_REG_for_C_CUT_2G[i]|MaskforPhySet, bRFRegOffsetMask, (RF_REG_Param_for_C_CUT_2G[index][i] | BIT17));
			else
#endif
			PHY_SetRFReg(Adapter, (RF_RADIO_PATH_E)path, RF_REG_for_C_CUT_2G[i]|MaskforPhySet, bRFRegOffsetMask, RF_REG_Param_for_C_CUT_2G[index][i]);
#else
			PHY_SetRFReg(Adapter, (RF_RADIO_PATH_E)path, RF_REG_for_C_CUT_2G[i], RF_REG_MASK_for_C_CUT_2G[i], RF_REG_Param_for_C_CUT_2G[index][i]);
#endif
			//RT_TRACE(COMP_RF, DBG_TRACE, ("phy_SwitchRfSetting offset 0x%x value 0x%x mak 0x%x path %d index %d readback 0x%x\n", 
			//	RF_REG_for_C_CUT_2G[i], RF_REG_Param_for_C_CUT_2G[index][i], RF_REG_MASK_for_C_CUT_2G[i], path,  index, 
			//	PHY_QueryRFReg(Adapter, (RF_RADIO_PATH_E) path, RF_REG_for_C_CUT_2G[i]|MaskforPhySet, bRFRegOffsetMask)));
		}

#if SWLCK == 1
		//for SWLCK
		//RTPRINT(FINIT, INIT_IQK, ("cosa ver 3 set RF-B, 2G, 0x28 = 0x%x !!\n", RF_REG_SYN_G4_for_C_CUT_2G | (u4tmp << 11)));

		PHY_SetRFReg(Adapter, (RF_RADIO_PATH_E)path, RF_SYN_G4|MaskforPhySet, bRFRegOffsetMask, RF_REG_SYN_G4_for_C_CUT_2G | (u4tmp << 11));
#endif
		if(pHalData->MacPhyMode92D == DUALMAC_DUALPHY && pHalData->interfaceIndex == 0)
		{
			if(bNeedPowerDownRadio){
				PHY_RestoreRFENV(Adapter, path,MaskforPhySet, &u4RegValue);
			}
			rtl8192d_PHY_PowerDownAnotherPHY(Adapter, _TRUE);
		}
	}

	//RT_TRACE(COMP_CMD, DBG_LOUD, ("<====phy_SwitchRfSetting interface %d\n", pHalData->interfaceIndex));

}


/*-----------------------------------------------------------------------------
 * Function:	phy_ReloadLCKSetting
 *
 * Overview:	Change RF Setting when we siwthc channel for 92D C-cut.
 *
 * Input:       IN	PADAPTER				pAdapter
 *
 * Output:      NONE
 *
 * Return:      NONE
 *
 * Revised History:
 * When			Who		Remark
 * 01/08/2009	MHC		Suggestion from SD3 Willis for 92S series.
 * 01/09/2009	MHC		Add CCK modification for 40MHZ. Suggestion from SD3.
 *
 *---------------------------------------------------------------------------*/
static  VOID	
 phy_ReloadLCKSetting(
 	IN	PADAPTER				Adapter,
	IN	u8					channel 	
 	)
{
	HAL_DATA_TYPE		*pHalData = GET_HAL_DATA(Adapter);
	u8		eRFPath = pHalData->CurrentBandType92D == BAND_ON_5G?RF_PATH_A:IS_92D_SINGLEPHY(pHalData->VersionID)?RF_PATH_B:RF_PATH_A;
	u32 		u4tmp = 0, u4RegValue = 0;
	BOOLEAN		bNeedPowerDownRadio = _FALSE;
	u32		MaskforPhySet = 0;

	//RT_TRACE(COMP_CMD, DBG_LOUD, ("====>phy_ReloadLCKSetting interface %d path %d\n", Adapter->interfaceIndex, eRFPath));

	//only for 92D C-cut SMSP

	//RTPRINT(FINIT, INIT_IQK, ("cosa pHalData->CurrentBandType92D = %d\n", pHalData->CurrentBandType92D));
	//RTPRINT(FINIT, INIT_IQK, ("cosa channel = %d\n", channel));
	if(pHalData->CurrentBandType92D == BAND_ON_5G)
	{
		//Path-A for 5G
		{
			u4tmp = CurveIndex[GetRightChnlPlace(channel)-1];
			//RTPRINT(FINIT, INIT_IQK, ("cosa ver 1 set RF-A, 5G,	0x28 = 0x%x !!\n", u4tmp));

			if(pHalData->MacPhyMode92D == DUALMAC_DUALPHY && pHalData->interfaceIndex == 1)
			{
				bNeedPowerDownRadio = rtl8192d_PHY_EnableAnotherPHY(Adapter, _FALSE);
				MaskforPhySet = MAC1_ACCESS_PHY0;
				//asume no this case
				if(bNeedPowerDownRadio)
				 	PHY_EnableRFENV(Adapter, eRFPath, MaskforPhySet,&u4RegValue);
			}		
			
			PHY_SetRFReg(Adapter, (RF_RADIO_PATH_E)eRFPath, RF_SYN_G4|MaskforPhySet, 0x3f800, u4tmp);

			if(bNeedPowerDownRadio){
				PHY_RestoreRFENV(Adapter, eRFPath,MaskforPhySet, &u4RegValue);
				rtl8192d_PHY_PowerDownAnotherPHY(Adapter, _FALSE);
			}
		}
	}
	else if(pHalData->CurrentBandType92D == BAND_ON_2_4G)
	{
		{
			u32 u4tmp=0;
			u4tmp = CurveIndex[channel-1];
			//RTPRINT(FINIT, INIT_IQK, ("cosa ver 3 set RF-B, 2G, 0x28 = 0x%x !!\n", u4tmp));

			if(pHalData->MacPhyMode92D == DUALMAC_DUALPHY && pHalData->interfaceIndex == 0)
			{
				bNeedPowerDownRadio = rtl8192d_PHY_EnableAnotherPHY(Adapter, _TRUE);
				MaskforPhySet = MAC0_ACCESS_PHY1;
				if(bNeedPowerDownRadio)
					PHY_EnableRFENV(Adapter, eRFPath,MaskforPhySet, &u4RegValue);
			}			
			PHY_SetRFReg(Adapter, (RF_RADIO_PATH_E)eRFPath, RF_SYN_G4|MaskforPhySet, 0x3f800, u4tmp);
			//RTPRINT(FINIT, INIT_IQK, ("cosa ver 3 set RF-B, 2G, 0x28 = 0x%lx !!\n", PHY_QueryRFReg(Adapter, (RF_RADIO_PATH_E) eRFPath, RF_SYN_G4|MaskforPhyAccess, 0x3f800)));

			if(bNeedPowerDownRadio){
				PHY_RestoreRFENV(Adapter, eRFPath,MaskforPhySet, &u4RegValue);
				rtl8192d_PHY_PowerDownAnotherPHY(Adapter, _TRUE);
			}
		}
	}



	//RT_TRACE(COMP_CMD, DBG_LOUD, ("<====phy_ReloadLCKSetting\n"));

}


/*-----------------------------------------------------------------------------
 * Function:	phy_ReloadIMRSetting
 *
 * Overview:	Change RF Setting when we siwthc channel for 92D C-cut.
 *
 * Input:       IN	PADAPTER				pAdapter
 *
 * Output:      NONE
 *
 * Return:      NONE
 *
 * Revised History:
 * When			Who		Remark
 * 01/08/2009	MHC		Suggestion from SD3 Willis for 92S series.
 * 01/09/2009	MHC		Add CCK modification for 40MHZ. Suggestion from SD3.
 *
 *---------------------------------------------------------------------------*/
 static VOID	
 phy_ReloadIMRSetting(
 	IN	PADAPTER				Adapter,
	IN	u8					channel,
	IN  	u8					eRFPath
 	)
{
	HAL_DATA_TYPE		*pHalData = GET_HAL_DATA(Adapter);
	u32		IMR_NUM = MAX_RF_IMR_INDEX;
	u32	   	RFMask=bRFRegOffsetMask;
	u8	   	group=0, i;

#ifdef CONFIG_USB_HCI
	if(Adapter->dvobjpriv.ishighspeed == _FALSE)
		return;
#endif	

	//only for 92D C-cut SMSP

	//RT_TRACE(COMP_CMD, DBG_LOUD, ("====>phy_ReloadIMRSetting interface %d path %d\n", pHalData->interfaceIndex, eRFPath));

	if(pHalData->CurrentBandType92D == BAND_ON_5G)
	{
		PHY_SetBBReg1Byte(Adapter, rFPGA0_RFMOD, BIT25|BIT24, 0);
		PHY_SetBBReg(Adapter, rFPGA0_AnalogParameter4, 0x00f00000,	0xf);
	
		// fc area 0xd2c
		if(channel>=149)
			PHY_SetBBReg(Adapter, rOFDM1_CFOTracking, BIT13|BIT14,2);	
		else
			PHY_SetBBReg(Adapter, rOFDM1_CFOTracking, BIT13|BIT14,1);
		
		group = channel<=64?1:2; //leave 0 for channel1-14.
		IMR_NUM = MAX_RF_IMR_INDEX_NORMAL;
		
		for(i=0; i<IMR_NUM; i++){
			PHY_SetRFReg(Adapter, (RF_RADIO_PATH_E)eRFPath, RF_REG_FOR_5G_SWCHNL_NORMAL[i], RFMask,RF_IMR_Param_Normal[0][group][i]);
		}				
		PHY_SetBBReg(Adapter, rFPGA0_AnalogParameter4, 0x00f00000,0);
		PHY_SetBBReg1Byte(Adapter, rFPGA0_RFMOD, bOFDMEn|bCCKEn, 2);
	}
	else{ //G band.
		//RT_TRACE(COMP_SCAN,DBG_LOUD,("Load RF IMR parameters for G band. IMR already setting %d \n",pMgntInfo->bLoadIMRandIQKSettingFor2G));

		if(!pHalData->bLoadIMRandIQKSettingFor2G){
			//RT_TRACE(COMP_SCAN,DBG_LOUD,("Load RF IMR parameters for G band. %d \n",eRFPath));
			//AcquireCCKAndRWPageAControl(Adapter);
			PHY_SetBBReg1Byte(Adapter, rFPGA0_RFMOD, BIT25|BIT24, 0);
			PHY_SetBBReg(Adapter, rFPGA0_AnalogParameter4, 0x00f00000,	0xf);

			IMR_NUM = MAX_RF_IMR_INDEX_NORMAL;
			for(i=0; i<IMR_NUM; i++){
				PHY_SetRFReg(Adapter, (RF_RADIO_PATH_E)eRFPath, RF_REG_FOR_5G_SWCHNL_NORMAL[i], bRFRegOffsetMask,RF_IMR_Param_Normal[0][0][i]);
			}
			PHY_SetBBReg(Adapter, rFPGA0_AnalogParameter4, 0x00f00000,0);
			PHY_SetBBReg1Byte(Adapter, rFPGA0_RFMOD, bOFDMEn|bCCKEn, 3);
			//ReleaseCCKAndRWPageAControl(Adapter);
		}
	}
	
	//RT_TRACE(COMP_CMD, DBG_LOUD, ("<====phy_ReloadIMRSetting\n"));

}	


/*-----------------------------------------------------------------------------
 * Function:	phy_ReloadIQKSetting
 *
 * Overview:	Change RF Setting when we siwthc channel for 92D C-cut.
 *
 * Input:       IN	PADAPTER				pAdapter
 *
 * Output:      NONE
 *
 * Return:      NONE
 *
 * Revised History:
 * When			Who		Remark
 * 01/08/2009	MHC		Suggestion from SD3 Willis for 92S series.
 * 01/09/2009	MHC		Add CCK modification for 40MHZ. Suggestion from SD3.
 *
 *---------------------------------------------------------------------------*/
 static VOID	
 phy_ReloadIQKSetting(
 	IN	PADAPTER				Adapter,
	IN	u8					channel
 	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	u8	   	Indexforchannel;//index, 

	//only for 92D C-cut SMSP

#ifdef CONFIG_USB_HCI
	if(Adapter->dvobjpriv.ishighspeed == _FALSE)
		return;
#endif

	//RT_TRACE(COMP_CMD, DBG_LOUD, ("====>phy_ReloadIQKSetting interface %d channel %d \n", Adapter->interfaceIndex, channel));

	//---------Do IQK for normal chip and test chip 5G band----------------
	Indexforchannel = rtl8192d_GetRightChnlPlaceforIQK(channel);

	//RT_TRACE(COMP_CMD, DBG_LOUD, ("====>Indexforchannel %d done %d\n", Indexforchannel, pHalData->IQKMatrixRegSetting[Indexforchannel].bIQKDone));

	
#if MP_DRIVER == 1
	pHalData->bNeedIQK = _TRUE;
	pHalData->bLoadIMRandIQKSettingFor2G = _FALSE;
#endif
	
	if(pHalData->bNeedIQK && !pHalData->IQKMatrixRegSetting[Indexforchannel].bIQKDone)
	{ //Re Do IQK.
		DBG_8192C("Do IQK Matrix reg for channel:%d....\n", channel);
		rtl8192d_PHY_IQCalibrate(Adapter);
	}
	else //Just load the value.
	{
		// 2G band just load once.
		if(((!pHalData->bLoadIMRandIQKSettingFor2G) && Indexforchannel==0) ||Indexforchannel>0)
		{
			//DBG_8192C("Just Read IQK Matrix reg for channel:%d....\n", channel);

			if((pHalData->IQKMatrixRegSetting[Indexforchannel].Value[0][0] != 0)/*&&(RegEA4 != 0)*/)
			{
				if(pHalData->CurrentBandType92D == BAND_ON_5G)
					phy_PathAFillIQKMatrix_5G_Normal(Adapter, _TRUE, pHalData->IQKMatrixRegSetting[Indexforchannel].Value, 0, (pHalData->IQKMatrixRegSetting[Indexforchannel].Value[0][2] == 0));
				else
					phy_PathAFillIQKMatrix(Adapter, _TRUE, pHalData->IQKMatrixRegSetting[Indexforchannel].Value, 0, (pHalData->IQKMatrixRegSetting[Indexforchannel].Value[0][2] == 0));
			}

			if (IS_92D_SINGLEPHY(pHalData->VersionID))
			{
				if((pHalData->IQKMatrixRegSetting[Indexforchannel].Value[0][4] != 0)/*&&(RegEC4 != 0)*/)
				{
					if(pHalData->CurrentBandType92D == BAND_ON_5G)
						phy_PathBFillIQKMatrix_5G_Normal(Adapter, _TRUE, pHalData->IQKMatrixRegSetting[Indexforchannel].Value, 0, (pHalData->IQKMatrixRegSetting[Indexforchannel].Value[0][6] == 0));
					else
						phy_PathBFillIQKMatrix(Adapter, _TRUE, pHalData->IQKMatrixRegSetting[Indexforchannel].Value, 0, (pHalData->IQKMatrixRegSetting[Indexforchannel].Value[0][6] == 0));
				}
			}				

			if((Adapter->mlmeextpriv.sitesurvey_res.state == SCAN_PROCESS)&&(Indexforchannel==0))
				pHalData->bLoadIMRandIQKSettingFor2G=_TRUE;
		}				
	}					
	pHalData->bNeedIQK = _FALSE;
	ATOMIC_SET(&pHalData->IQKRdyForXmit, 1);
	
	//RT_TRACE(COMP_CMD, DBG_LOUD, ("<====phy_ReloadIQKSetting\n"));

}


static void _PHY_SwChnl8192D(PADAPTER Adapter, u8 channel)
{
	u8	eRFPath;
	u32	param1, param2;
	u32	ret_value;
	BAND_TYPE	bandtype, target_bandtype;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
#ifdef CONFIG_DUALMAC_CONCURRENT
	// FOr 92D dual mac config.
	PADAPTER	BuddyAdapter = Adapter->pbuddy_adapter;
#endif

	if(pHalData->BandSet92D == BAND_ON_BOTH){
		// Need change band?
		// BB {Reg878[0],[16]} bit0= 1 is 5G, bit0=0 is 2G.
		ret_value = PHY_QueryBBReg(Adapter, rFPGA0_XAB_RFParameter, bMaskDWord);

		if(ret_value & BIT0)
			bandtype = BAND_ON_5G;
		else
			bandtype = BAND_ON_2_4G;

		// Use current channel to judge Band Type and switch Band if need.
		if(channel > 14)
		{
			target_bandtype = BAND_ON_5G;
		}
		else
		{
			target_bandtype = BAND_ON_2_4G;
		}

		if(target_bandtype != bandtype)
			PHY_SwitchWirelessBand(Adapter,target_bandtype);
	}

	do{
		//s1. pre common command - CmdID_SetTxPowerLevel
		PHY_SetTxPowerLevel8192D(Adapter, channel);

		//s2. RF dependent command - CmdID_RF_WriteReg, param1=RF_CHNLBW, param2=channel
		param1 = RF_CHNLBW;
		param2 = channel;
		for(eRFPath = 0; eRFPath <pHalData->NumTotalRFPath; eRFPath++)
		{
#if 1
			//pHalData->RfRegChnlVal[eRFPath] = PHY_QueryRFReg(Adapter, (RF_RADIO_PATH_E)eRFPath, RF_CHNLBW, bRFRegOffsetMask);
			// & 0xFFFFFC00 just for 2.4G. So for 5G band,bit[9:8]= 1. we change this setting for 5G. by wl
			pHalData->RfRegChnlVal[eRFPath] = ((pHalData->RfRegChnlVal[eRFPath] & 0xffffff00) | param2);
			if(pHalData->CurrentBandType92D == BAND_ON_5G)
			{
				if(param2>99)
				{
					pHalData->RfRegChnlVal[eRFPath]=pHalData->RfRegChnlVal[eRFPath]|(BIT18);
				}
				else
				{
					pHalData->RfRegChnlVal[eRFPath]=pHalData->RfRegChnlVal[eRFPath]&(~BIT18);
				}
				pHalData->RfRegChnlVal[eRFPath] |= (BIT16|BIT8);
			}
			else
			{
				pHalData->RfRegChnlVal[eRFPath] &= ~(BIT8|BIT16|BIT18);
			}
			PHY_SetRFReg(Adapter, (RF_RADIO_PATH_E)eRFPath, param1, bRFRegOffsetMask, pHalData->RfRegChnlVal[eRFPath]);
#else
			PHY_SetRFReg(Adapter, (RF_RADIO_PATH_E)eRFPath, param1, bRFRegOffsetMask, param2);
#endif

			phy_ReloadIMRSetting(Adapter, channel, eRFPath);
		}

		phy_SwitchRfSetting(Adapter, channel);

#if 0
		phy_ReloadLCKSetting(Adapter, channel);		
#endif	

		//do IQK when all parameters are ready			
		phy_ReloadIQKSetting(Adapter, channel);
		break;
	}while(_TRUE);

	//s3. post common command - CmdID_End, None

#ifdef CONFIG_DUALMAC_CONCURRENT
	if(Adapter->DualMacConcurrent == _TRUE && BuddyAdapter != NULL)
	{
		if(pHalData->bMasterOfDMSP)
		{
			GET_HAL_DATA(BuddyAdapter)->CurrentChannel=channel;
			ATOMIC_SET(&(GET_HAL_DATA(BuddyAdapter)->IQKRdyForXmit), 1);
		}
	}
#endif
}


VOID
PHY_SwChnl8192D(	// Call after initialization
	IN	PADAPTER	Adapter,
	IN	u8		channel
	)
{
	//PADAPTER Adapter =  ADJUST_TO_ADAPTIVE_ADAPTER(pAdapter, _TRUE);
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	u8	tmpchannel = pHalData->CurrentChannel;
	BOOLEAN  bResult = _TRUE;
	u32	timeout = 1000, timecount = 0;

#ifdef CONFIG_DUALMAC_CONCURRENT
	PADAPTER	BuddyAdapter = Adapter->pbuddy_adapter;
	HAL_DATA_TYPE	*pHalDataBuddyAdapter;
#endif

	if(pHalData->rf_chip == RF_PSEUDO_11N)
	{
		//pHalData->SwChnlInProgress=FALSE;
		return; 								//return immediately if it is peudo-phy	
	}

	if(Adapter->mlmeextpriv.sitesurvey_res.state == SCAN_COMPLETE)
		pHalData->bLoadIMRandIQKSettingFor2G = _FALSE;

	//if(pHalData->SwChnlInProgress)
	//	return;

	//if(pHalData->SetBWModeInProgress)
	//	return;

#ifdef CONFIG_DUALMAC_CONCURRENT
	if(pHalData->bInModeSwitchProcess)
	{
		DBG_871X("PHY_SwChnl8192D(): During mode switch \n");
		//pHalData->SwChnlInProgress=_FALSE;
		return;
	}

	if(BuddyAdapter != NULL && 
		((pHalData->interfaceIndex == 0 && pHalData->CurrentBandType92D == BAND_ON_2_4G) ||
		(pHalData->interfaceIndex == 1 && pHalData->CurrentBandType92D == BAND_ON_5G)))
	{
		pHalDataBuddyAdapter=GET_HAL_DATA(BuddyAdapter);
		while(pHalDataBuddyAdapter->bLCKInProgress && timecount < timeout)
		{
			#ifdef CONFIG_LONG_DELAY_ISSUE
			rtw_msleep_os(50);
			#else
			rtw_mdelay_os(50);
			#endif
			timecount += 50;
		}
	}
#endif

	while(pHalData->bLCKInProgress && timecount < timeout)
	{
		#ifdef CONFIG_LONG_DELAY_ISSUE
		rtw_msleep_os(50);
		#else
		rtw_mdelay_os(50);
		#endif
		timecount += 50;
	}

	//--------------------------------------------
	switch(pHalData->CurrentWirelessMode)
	{
		case WIRELESS_MODE_A:
		case WIRELESS_MODE_N_5G:
			//Get first channel error when change between 5G and 2.4G band.
			//FIX ME!!!
			//if(channel <=14)
			//	return;
			//RT_ASSERT((channel>14), ("WIRELESS_MODE_A but channel<=14"));
			break;
		
		case WIRELESS_MODE_B:
			//if(channel>14)
			//	return;
			//RT_ASSERT((channel<=14), ("WIRELESS_MODE_B but channel>14"));
			break;
		
		case WIRELESS_MODE_G:
		case WIRELESS_MODE_N_24G:
			//Get first channel error when change between 5G and 2.4G band.
			//FIX ME!!!
			//if(channel > 14)
			//	return;
			//RT_ASSERT((channel<=14), ("WIRELESS_MODE_G but channel>14"));
			break;

		default:
			//RT_ASSERT(FALSE, ("Invalid WirelessMode(%#x)!!\n", pHalData->CurrentWirelessMode));
			break;
	}
	//--------------------------------------------

	//pHalData->SwChnlInProgress = TRUE;
	if( channel == 0){//FIXME!!!A band?
		channel = 1;
	}

	pHalData->CurrentChannel=channel;

	//pHalData->SwChnlStage=0;
	//pHalData->SwChnlStep=0;

#ifdef CONFIG_DUALMAC_CONCURRENT
	if((BuddyAdapter !=NULL) && (pHalData->bSlaveOfDMSP))
	{
		DBG_871X("PHY_SwChnl8192D():slave return when slave  \n");
		//pHalData->SwChnlInProgress=_FALSE;
		return;
	}
#endif

	if((!Adapter->bDriverStopped) && (!Adapter->bSurpriseRemoved))
	{
#ifdef USE_WORKITEM	
		//bResult = PlatformScheduleWorkItem(&(pHalData->SwChnlWorkItem));
#else
		#if 0		
		//PlatformSetTimer(Adapter, &(pHalData->SwChnlTimer), 0);
		#else
		_PHY_SwChnl8192D(Adapter, channel);
		#endif
#endif
		if(bResult)
		{
			//RT_TRACE(COMP_SCAN, DBG_LOUD, ("PHY_SwChnl8192C SwChnlInProgress TRUE schdule workitem done\n"));
		}
		else
		{
			//RT_TRACE(COMP_SCAN, DBG_LOUD, ("PHY_SwChnl8192C SwChnlInProgress FALSE schdule workitem error\n"));		
			//if(IS_HARDWARE_TYPE_8192SU(Adapter))
			//{
			//	pHalData->SwChnlInProgress = FALSE; 	
				pHalData->CurrentChannel = tmpchannel;			
			//}
		}

	}
	else
	{
		//RT_TRACE(COMP_SCAN, DBG_LOUD, ("PHY_SwChnl8192C SwChnlInProgress FALSE driver sleep or unload\n"));	
		//if(IS_HARDWARE_TYPE_8192SU(Adapter))
		//{
		//	pHalData->SwChnlInProgress = FALSE;		
			pHalData->CurrentChannel = tmpchannel;
		//}
	}
}

#ifndef PLATFORM_FREEBSD //amy, temp remove
static	BOOLEAN
phy_SwChnlStepByStep(
	IN	PADAPTER	Adapter,
	IN	u8		channel,
	IN	u8		*stage,
	IN	u8		*step,
	OUT u32		*delay
	)
{
#if 0
	HAL_DATA_TYPE			*pHalData = GET_HAL_DATA(Adapter);
	PCHANNEL_ACCESS_SETTING	pChnlAccessSetting;
	SwChnlCmd				PreCommonCmd[MAX_PRECMD_CNT];
	u4Byte					PreCommonCmdCnt;
	SwChnlCmd				PostCommonCmd[MAX_POSTCMD_CNT];
	u4Byte					PostCommonCmdCnt;
	SwChnlCmd				RfDependCmd[MAX_RFDEPENDCMD_CNT];
	u4Byte					RfDependCmdCnt;
	SwChnlCmd				*CurrentCmd;	
	u1Byte					eRFPath;	
	u4Byte					RfTXPowerCtrl;
	BOOLEAN					bAdjRfTXPowerCtrl = _FALSE;
	
	
	RT_ASSERT((Adapter != NULL), ("Adapter should not be NULL\n"));
#if(MP_DRIVER != 1)
	RT_ASSERT(IsLegalChannel(Adapter, channel), ("illegal channel: %d\n", channel));
#endif
	RT_ASSERT((pHalData != NULL), ("pHalData should not be NULL\n"));
	
	pChnlAccessSetting = &Adapter->MgntInfo.Info8185.ChannelAccessSetting;
	RT_ASSERT((pChnlAccessSetting != NULL), ("pChnlAccessSetting should not be NULL\n"));
	
	//for(eRFPath = RF_PATH_A; eRFPath <pHalData->NumTotalRFPath; eRFPath++)
	//for(eRFPath = 0; eRFPath <pHalData->NumTotalRFPath; eRFPath++)
	//{
		// <1> Fill up pre common command.
	PreCommonCmdCnt = 0;
	phy_SetSwChnlCmdArray(PreCommonCmd, PreCommonCmdCnt++, MAX_PRECMD_CNT, 
				CmdID_SetTxPowerLevel, 0, 0, 0);
	phy_SetSwChnlCmdArray(PreCommonCmd, PreCommonCmdCnt++, MAX_PRECMD_CNT, 
				CmdID_End, 0, 0, 0);
	
		// <2> Fill up post common command.
	PostCommonCmdCnt = 0;

	phy_SetSwChnlCmdArray(PostCommonCmd, PostCommonCmdCnt++, MAX_POSTCMD_CNT, 
				CmdID_End, 0, 0, 0);
	
		// <3> Fill up RF dependent command.
	RfDependCmdCnt = 0;
	switch( pHalData->RFChipID )
	{
		case RF_8225:		
		RT_ASSERT((channel >= 1 && channel <= 14), ("illegal channel for Zebra: %d\n", channel));
		// 2008/09/04 MH Change channel. 
		if(channel==14) channel++;
		phy_SetSwChnlCmdArray(RfDependCmd, RfDependCmdCnt++, MAX_RFDEPENDCMD_CNT, 
			CmdID_RF_WriteReg, rZebra1_Channel, (0x10+channel-1), 10);
		phy_SetSwChnlCmdArray(RfDependCmd, RfDependCmdCnt++, MAX_RFDEPENDCMD_CNT, 
		CmdID_End, 0, 0, 0);
		break;	
		
	case RF_8256:
		// TEST!! This is not the table for 8256!!
		RT_ASSERT((channel >= 1 && channel <= 14), ("illegal channel for Zebra: %d\n", channel));
		phy_SetSwChnlCmdArray(RfDependCmd, RfDependCmdCnt++, MAX_RFDEPENDCMD_CNT, 
			CmdID_RF_WriteReg, rRfChannel, channel, 10);
		phy_SetSwChnlCmdArray(RfDependCmd, RfDependCmdCnt++, MAX_RFDEPENDCMD_CNT, 
		CmdID_End, 0, 0, 0);
		break;
		
	case RF_6052:
		RT_ASSERT((channel >= 1 && channel <= 14), ("illegal channel for Zebra: %d\n", channel));
		phy_SetSwChnlCmdArray(RfDependCmd, RfDependCmdCnt++, MAX_RFDEPENDCMD_CNT, 
			CmdID_RF_WriteReg, RF_CHNLBW, channel, 10);		
		phy_SetSwChnlCmdArray(RfDependCmd, RfDependCmdCnt++, MAX_RFDEPENDCMD_CNT, 
		CmdID_End, 0, 0, 0);		
		
		break;

	case RF_8258:
		break;

	// For FPGA two MAC verification
	case RF_PSEUDO_11N:
		return TRUE;
	default:
		RT_ASSERT(FALSE, ("Unknown RFChipID: %d\n", pHalData->RFChipID));
		return FALSE;
		break;
	}

	
	do{
		switch(*stage)
		{
		case 0:
			CurrentCmd=&PreCommonCmd[*step];
			break;
		case 1:
			CurrentCmd=&RfDependCmd[*step];
			break;
		case 2:
			CurrentCmd=&PostCommonCmd[*step];
			break;
		}
		
		if(CurrentCmd->CmdID==CmdID_End)
		{
			if((*stage)==2)
			{
				return TRUE;
			}
			else
			{
				(*stage)++;
				(*step)=0;
				continue;
			}
		}
		
		switch(CurrentCmd->CmdID)
		{
		case CmdID_SetTxPowerLevel:
			PHY_SetTxPowerLevel8192C(Adapter,channel);
			break;
		case CmdID_WritePortUlong:
			PlatformEFIOWrite4Byte(Adapter, CurrentCmd->Para1, CurrentCmd->Para2);
			break;
		case CmdID_WritePortUshort:
			PlatformEFIOWrite2Byte(Adapter, CurrentCmd->Para1, (u2Byte)CurrentCmd->Para2);
			break;
		case CmdID_WritePortUchar:
			PlatformEFIOWrite1Byte(Adapter, CurrentCmd->Para1, (u1Byte)CurrentCmd->Para2);
			break;
		case CmdID_RF_WriteReg:	// Only modify channel for the register now !!!!!
			for(eRFPath = 0; eRFPath <pHalData->NumTotalRFPath; eRFPath++)
			{
#if 1
				pHalData->RfRegChnlVal[eRFPath] = ((pHalData->RfRegChnlVal[eRFPath] & 0xfffffc00) | CurrentCmd->Para2);
				PHY_SetRFReg(Adapter, (RF_RADIO_PATH_E)eRFPath, CurrentCmd->Para1, bRFRegOffsetMask, pHalData->RfRegChnlVal[eRFPath]);
#else
				PHY_SetRFReg(Adapter, (RF_RADIO_PATH_E)eRFPath, CurrentCmd->Para1, bRFRegOffsetMask, (CurrentCmd->Para2));
#endif
			}
			break;
		}
		
		break;
	}while(TRUE);
	//cosa }/*for(Number of RF paths)*/

	(*delay)=CurrentCmd->msDelay;
	(*step)++;
	return FALSE;
#endif	
	return _TRUE;
}


static	BOOLEAN
phy_SetSwChnlCmdArray(
	SwChnlCmd*		CmdTable,
	u32			CmdTableIdx,
	u32			CmdTableSz,
	SwChnlCmdID		CmdID,
	u32			Para1,
	u32			Para2,
	u32			msDelay
	)
{
	SwChnlCmd* pCmd;

	if(CmdTable == NULL)
	{
		//RT_ASSERT(FALSE, ("phy_SetSwChnlCmdArray(): CmdTable cannot be NULL.\n"));
		return _FALSE;
	}
	if(CmdTableIdx >= CmdTableSz)
	{
		//RT_ASSERT(FALSE, 
		//		("phy_SetSwChnlCmdArray(): Access invalid index, please check size of the table, CmdTableIdx:%ld, CmdTableSz:%ld\n",
		//		CmdTableIdx, CmdTableSz));
		return _FALSE;
	}

	pCmd = CmdTable + CmdTableIdx;
	pCmd->CmdID = CmdID;
	pCmd->Para1 = Para1;
	pCmd->Para2 = Para2;
	pCmd->msDelay = msDelay;

	return _TRUE;
}
#endif  //amy, temp remove

static	void
phy_FinishSwChnlNow(	// We should not call this function directly
		IN	PADAPTER	Adapter,
		IN	u8		channel
		)
{
#if 0
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	u32			delay;
  
	while(!phy_SwChnlStepByStep(Adapter,channel,&pHalData->SwChnlStage,&pHalData->SwChnlStep,&delay))
	{
		if(delay>0)
			rtw_mdelay_os(delay);
	}
#endif	
}


//
// Description:
//	Switch channel synchronously. Called by SwChnlByDelayHandler.
//
// Implemented by Bruce, 2008-02-14.
// The following procedure is operted according to SwChanlCallback8190Pci().
// However, this procedure is performed synchronously  which should be running under
// passive level.
// 
VOID
PHY_SwChnlPhy8192D(	// Only called during initialize
	IN	PADAPTER	Adapter,
	IN	u8		channel
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);

	//RT_TRACE(COMP_SCAN | COMP_RM, DBG_LOUD, ("==>PHY_SwChnlPhy8192S(), switch from channel %d to channel %d.\n", pHalData->CurrentChannel, channel));

	// Cannot IO.
	//if(RT_CANNOT_IO(Adapter))
	//	return;

	// Channel Switching is in progress.
	//if(pHalData->SwChnlInProgress)
	//	return;
	
	//return immediately if it is peudo-phy
	if(pHalData->rf_chip == RF_PSEUDO_11N)
	{
		//pHalData->SwChnlInProgress=FALSE;
		return;
	}
	
	//pHalData->SwChnlInProgress = TRUE;
	if( channel == 0)
		channel = 1;
	
	pHalData->CurrentChannel=channel;
	
	//pHalData->SwChnlStage = 0;
	//pHalData->SwChnlStep = 0;
	
	phy_FinishSwChnlNow(Adapter,channel);
	
	//pHalData->SwChnlInProgress = FALSE;
}


//
//	Description:
//		Configure H/W functionality to enable/disable Monitor mode.
//		Note, because we possibly need to configure BB and RF in this function, 
//		so caller should in PASSIVE_LEVEL. 080118, by rcnjko.
//
VOID
PHY_SetMonitorMode8192D(
	IN	PADAPTER			pAdapter,
	IN	BOOLEAN				bEnableMonitorMode
	)
{
#if 0
	HAL_DATA_TYPE		*pHalData	= GET_HAL_DATA(pAdapter);
	BOOLEAN				bFilterOutNonAssociatedBSSID = FALSE;

	//2 Note: we may need to stop antenna diversity.
	if(bEnableMonitorMode)
	{
		bFilterOutNonAssociatedBSSID = FALSE;
		RT_TRACE(COMP_RM, DBG_LOUD, ("PHY_SetMonitorMode8192S(): enable monitor mode\n"));

		pHalData->bInMonitorMode = TRUE;
		pAdapter->HalFunc.AllowAllDestAddrHandler(pAdapter, TRUE, TRUE);
		pAdapter->HalFunc.SetHwRegHandler(pAdapter, HW_VAR_CHECK_BSSID, (pu1Byte)&bFilterOutNonAssociatedBSSID);
	}
	else
	{
		bFilterOutNonAssociatedBSSID = TRUE;
		RT_TRACE(COMP_RM, DBG_LOUD, ("PHY_SetMonitorMode8192S(): disable monitor mode\n"));

		pAdapter->HalFunc.AllowAllDestAddrHandler(pAdapter, FALSE, TRUE);
		pHalData->bInMonitorMode = FALSE;
		pAdapter->HalFunc.SetHwRegHandler(pAdapter, HW_VAR_CHECK_BSSID, (pu1Byte)&bFilterOutNonAssociatedBSSID);
	}
#endif	
}


/*-----------------------------------------------------------------------------
 * Function:	PHYCheckIsLegalRfPath8190Pci()
 *
 * Overview:	Check different RF type to execute legal judgement. If RF Path is illegal
 *			We will return false.
 *
 * Input:		NONE
 *
 * Output:		NONE
 *
 * Return:		NONE
 *
 * Revised History:
 *	When		Who		Remark
 *	11/15/2007	MHC		Create Version 0.  
 *
 *---------------------------------------------------------------------------*/
BOOLEAN	
PHY_CheckIsLegalRfPath8192D(	
	IN	PADAPTER	pAdapter,
	IN	u32	eRFPath)
{
//	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	BOOLEAN				rtValue = _TRUE;

	// NOt check RF Path now.!
#if 0	
	if (pHalData->RF_Type == RF_1T2R && eRFPath != RF_PATH_A)
	{		
		rtValue = FALSE;
	}
	if (pHalData->RF_Type == RF_1T2R && eRFPath != RF_PATH_A)
	{

	}
#endif
	return	rtValue;

}	/* PHY_CheckIsLegalRfPath8192D */

//-------------------------------------------------------------------------
//
//	IQK
//
//-------------------------------------------------------------------------
#define MAX_TOLERANCE		5
#define MAX_TOLERANCE_92D	3
#define IQK_DELAY_TIME		1 	//ms

static u8			//bit0 = 1 => Tx OK, bit1 = 1 => Rx OK
phy_PathA_IQK(
	IN	PADAPTER	pAdapter,
	IN	BOOLEAN		configPathB
	)
{
	u32	regEAC, regE94, regE9C, regEA4;
	u8	result = 0x00;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);

	//RTPRINT(FINIT, INIT_IQK, ("Path A IQK!\n"));

	//path-A IQK setting
	//RTPRINT(FINIT, INIT_IQK, ("Path-A IQK setting!\n"));
	if(pHalData->interfaceIndex == 0)
	{
		PHY_SetBBReg(pAdapter, rTx_IQK_Tone_A, bMaskDWord, 0x10008c1f);
		PHY_SetBBReg(pAdapter, rRx_IQK_Tone_A, bMaskDWord, 0x10008c1f);
	}
	else
	{
		PHY_SetBBReg(pAdapter, rTx_IQK_Tone_A, bMaskDWord, 0x10008c22);
		PHY_SetBBReg(pAdapter, rRx_IQK_Tone_A, bMaskDWord, 0x10008c22);	
	}

	PHY_SetBBReg(pAdapter, rTx_IQK_PI_A, bMaskDWord, 0x82140102);

	PHY_SetBBReg(pAdapter, rRx_IQK_PI_A, bMaskDWord, configPathB ? 0x28160202 : 
		IS_81xxC_VENDOR_UMC_B_CUT(pHalData->VersionID)?0x28160202:0x28160502);

	//path-B IQK setting
	if(configPathB)
	{
		PHY_SetBBReg(pAdapter, rTx_IQK_Tone_B, bMaskDWord, 0x10008c22);
		PHY_SetBBReg(pAdapter, rRx_IQK_Tone_B, bMaskDWord, 0x10008c22);
		PHY_SetBBReg(pAdapter, rTx_IQK_PI_B, bMaskDWord, 0x82140102);
		if(IS_HARDWARE_TYPE_8192D(pAdapter))
			PHY_SetBBReg(pAdapter, rRx_IQK_PI_B, bMaskDWord, 0x28160206);
		else
			PHY_SetBBReg(pAdapter, rRx_IQK_PI_B, bMaskDWord, 0x28160202);
	}

	//LO calibration setting
	//RTPRINT(FINIT, INIT_IQK, ("LO calibration setting!\n"));
	if(IS_HARDWARE_TYPE_8192D(pAdapter))
		PHY_SetBBReg(pAdapter, rIQK_AGC_Rsp, bMaskDWord, 0x00462911);
	else
		PHY_SetBBReg(pAdapter, rIQK_AGC_Rsp, bMaskDWord, 0x001028d1);

	//One shot, path A LOK & IQK
	//RTPRINT(FINIT, INIT_IQK, ("One shot, path A LOK & IQK!\n"));
	PHY_SetBBReg(pAdapter, rIQK_AGC_Pts, bMaskDWord, 0xf9000000);
	PHY_SetBBReg(pAdapter, rIQK_AGC_Pts, bMaskDWord, 0xf8000000);
	
	// delay x ms
	//RTPRINT(FINIT, INIT_IQK, ("Delay %d ms for One shot, path A LOK & IQK.\n", IQK_DELAY_TIME));
	rtw_udelay_os(IQK_DELAY_TIME*1000);

	// Check failed
	regEAC = PHY_QueryBBReg(pAdapter, rRx_Power_After_IQK_A_2, bMaskDWord);
	//RTPRINT(FINIT, INIT_IQK, ("0xeac = 0x%x\n", regEAC));
	regE94 = PHY_QueryBBReg(pAdapter, rTx_Power_Before_IQK_A, bMaskDWord);
	//RTPRINT(FINIT, INIT_IQK, ("0xe94 = 0x%x\n", regE94));
	regE9C= PHY_QueryBBReg(pAdapter, rTx_Power_After_IQK_A, bMaskDWord);
	//RTPRINT(FINIT, INIT_IQK, ("0xe9c = 0x%x\n", regE9C));
	regEA4= PHY_QueryBBReg(pAdapter, rRx_Power_Before_IQK_A_2, bMaskDWord);
	//RTPRINT(FINIT, INIT_IQK, ("0xea4 = 0x%x\n", regEA4));

	if(!(regEAC & BIT28) &&		
		(((regE94 & 0x03FF0000)>>16) != 0x142) &&
		(((regE9C & 0x03FF0000)>>16) != 0x42) )
		result |= 0x01;
	else							//if Tx not OK, ignore Rx
		return result;

	if(!(regEAC & BIT27) &&		//if Tx is OK, check whether Rx is OK
		(((regEA4 & 0x03FF0000)>>16) != 0x132) &&
		(((regEAC & 0x03FF0000)>>16) != 0x36))
		result |= 0x02;
	else
		DBG_8192C("Path A Rx IQK fail!!\n");
	
	return result;


}

static u8			//bit0 = 1 => Tx OK, bit1 = 1 => Rx OK
phy_PathA_IQK_5G_Normal(
	IN	PADAPTER	pAdapter,
	IN	BOOLEAN		configPathB
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	u32	regEAC, regE94, regE9C, regEA4;
	u8	result = 0x00;
	u8	i = 0;
#if MP_DRIVER == 1
	u8	retryCount = 9;
#else
	u8	retryCount = 2;
#endif
	u8	timeout = 20, timecount = 0;

	u32	TxOKBit = BIT28, RxOKBit = BIT27;

	if(pHalData->interfaceIndex == 1)	//PHY1
	{
		TxOKBit = BIT31;
		RxOKBit = BIT30;
	}

	//RTPRINT(FINIT, INIT_IQK, ("Path A IQK!\n"));

	//path-A IQK setting
	//RTPRINT(FINIT, INIT_IQK, ("Path-A IQK setting!\n"));

	PHY_SetBBReg(pAdapter, rTx_IQK_Tone_A, bMaskDWord, 0x18008c1f);
	PHY_SetBBReg(pAdapter, rRx_IQK_Tone_A, bMaskDWord, 0x18008c1f);
	PHY_SetBBReg(pAdapter, rTx_IQK_PI_A, bMaskDWord, 0x82140307);
	PHY_SetBBReg(pAdapter, rRx_IQK_PI_A, bMaskDWord, 0x68160960);

	//path-B IQK setting
	if(configPathB)
	{
		PHY_SetBBReg(pAdapter, rTx_IQK_Tone_B, bMaskDWord, 0x18008c2f );
		PHY_SetBBReg(pAdapter, rRx_IQK_Tone_B, bMaskDWord, 0x18008c2f );
		PHY_SetBBReg(pAdapter, rTx_IQK_PI_B, bMaskDWord, 0x82110000);
		PHY_SetBBReg(pAdapter, rRx_IQK_PI_B, bMaskDWord, 0x68110000);
	}
	
	//LO calibration setting
	//RTPRINT(FINIT, INIT_IQK, ("LO calibration setting!\n"));
	PHY_SetBBReg(pAdapter, rIQK_AGC_Rsp, bMaskDWord, 0x00462911);

	//path-A PA on
#if 0	
	PHY_SetBBReg(pAdapter, rFPGA0_XAB_RFInterfaceSW, BIT10|BIT11, 0x03);
	PHY_SetBBReg(pAdapter, rFPGA0_XAB_RFInterfaceSW, BIT6|BIT5, 0x03);	
	PHY_SetBBReg(pAdapter, rFPGA0_XA_RFInterfaceOE, BIT10|BIT11, 0x03);
#else
	PHY_SetBBReg(pAdapter, rFPGA0_XAB_RFInterfaceSW, bMaskDWord, 0x07000f60);
	PHY_SetBBReg(pAdapter, rFPGA0_XA_RFInterfaceOE, bMaskDWord, 0x66e60e30);
#endif

	for(i = 0 ; i < retryCount ; i++)
	{

		//One shot, path A LOK & IQK
		//RTPRINT(FINIT, INIT_IQK, ("One shot, path A LOK & IQK!\n"));
		PHY_SetBBReg(pAdapter, rIQK_AGC_Pts, bMaskDWord, 0xf9000000);
		PHY_SetBBReg(pAdapter, rIQK_AGC_Pts, bMaskDWord, 0xf8000000);

		// delay x ms
		//RTPRINT(FINIT, INIT_IQK, ("Delay %d ms for One shot, path A LOK & IQK.\n", IQK_DELAY_TIME));
		//rtw_udelay_os(IQK_DELAY_TIME*1000*10);
		rtw_mdelay_os(IQK_DELAY_TIME*10);

		while(timecount < timeout && PHY_QueryBBReg(pAdapter, rRx_Power_After_IQK_A_2, BIT26) == 0x00)
		{
			rtw_udelay_os(IQK_DELAY_TIME*1000*2);
			timecount++;
			//RTPRINT(FINIT, INIT_IQK, ("Delay %d ms for polling 0xeac bit26\n", timecount*2));
		}

		timecount = 0;
		while(timecount < timeout && PHY_QueryBBReg(pAdapter, rRx_Power_Before_IQK_A_2, 0x3FF0000) == 0x00)
		{
			rtw_udelay_os(IQK_DELAY_TIME*1000*2);
			timecount++;
			//RTPRINT(FINIT, INIT_IQK, ("Delay %d ms for polling 0xea4[25:16]\n", timecount*2));
		}

		//RTPRINT(FINIT, INIT_IQK, ("0xea0 = 0x%lx\n", PHY_QueryBBReg(pAdapter, 0xea0, bMaskDWord)));
		//RTPRINT(FINIT, INIT_IQK, ("0xea8 = 0x%lx\n", PHY_QueryBBReg(pAdapter, 0xea8, bMaskDWord)));

		// Check failed
		regEAC = PHY_QueryBBReg(pAdapter, rRx_Power_After_IQK_A_2, bMaskDWord);
		//RTPRINT(FINIT, INIT_IQK, ("0xeac = 0x%lx\n", regEAC));
		regE94 = PHY_QueryBBReg(pAdapter, rTx_Power_Before_IQK_A, bMaskDWord);
		//RTPRINT(FINIT, INIT_IQK, ("0xe94 = 0x%lx\n", regE94));
		regE9C= PHY_QueryBBReg(pAdapter, rTx_Power_After_IQK_A, bMaskDWord);
		//RTPRINT(FINIT, INIT_IQK, ("0xe9c = 0x%lx\n", regE9C));
		regEA4= PHY_QueryBBReg(pAdapter, rRx_Power_Before_IQK_A_2, bMaskDWord);
		//RTPRINT(FINIT, INIT_IQK, ("0xea4 = 0x%lx\n", regEA4));

		if(!(regEAC & TxOKBit) &&		
			(((regE94 & 0x03FF0000)>>16) != 0x142)  )
		{
			result |= 0x01;
		}
		else			//if Tx not OK, ignore Rx
		{
			//RTPRINT(FINIT, INIT_IQK, ("Path A Tx IQK fail!!\n"));		
			continue;
		}		

		if(!(regEAC & RxOKBit) &&			//if Tx is OK, check whether Rx is OK
			(((regEA4 & 0x03FF0000)>>16) != 0x132))
		{
			result |= 0x02;
			break;
		}	
		else
		{
			//RTPRINT(FINIT, INIT_IQK, ("Path A Rx IQK fail!!\n"));
		}
	}

	//path A PA off
	PHY_SetBBReg(pAdapter, rFPGA0_XAB_RFInterfaceSW, bMaskDWord, pdmpriv->IQK_BB_backup[0]);
	PHY_SetBBReg(pAdapter, rFPGA0_XA_RFInterfaceOE, bMaskDWord, pdmpriv->IQK_BB_backup[1]);

	if(!(result & 0x01))	//Tx IQK fail
	{
		PHY_SetBBReg(pAdapter, rTx_IQK_Tone_A, bMaskDWord, 0x19008c00);
	}

	if(!(result & 0x02))	//Rx IQK fail
	{
		PHY_SetBBReg(pAdapter, rOFDM0_XARxIQImbalance , bMaskDWord, 0x40000100);	
		PHY_SetBBReg(pAdapter, rRx_IQK_Tone_A , bMaskDWord, 0x19008c00);	

		DBG_871X("Path A Rx IQK fail!!0xe34 = 0x%x\n", PHY_QueryBBReg(pAdapter, rRx_IQK_Tone_A, bMaskDWord));
	}

	return result;
}

static u8				//bit0 = 1 => Tx OK, bit1 = 1 => Rx OK
phy_PathB_IQK(
	IN	PADAPTER	pAdapter
	)
{
	u32 regEAC, regEB4, regEBC, regEC4, regECC;
	u8	result = 0x00;
	//RTPRINT(FINIT, INIT_IQK, ("Path B IQK!\n"));

	//One shot, path B LOK & IQK
	//RTPRINT(FINIT, INIT_IQK, ("One shot, path A LOK & IQK!\n"));
	PHY_SetBBReg(pAdapter, rIQK_AGC_Cont, bMaskDWord, 0x00000002);
	PHY_SetBBReg(pAdapter, rIQK_AGC_Cont, bMaskDWord, 0x00000000);

	// delay x ms
	//RTPRINT(FINIT, INIT_IQK, ("Delay %d ms for One shot, path B LOK & IQK.\n", IQK_DELAY_TIME));
	rtw_udelay_os(IQK_DELAY_TIME*1000);//PlatformStallExecution(IQK_DELAY_TIME*1000);

	// Check failed
	regEAC = PHY_QueryBBReg(pAdapter, rRx_Power_After_IQK_A_2, bMaskDWord);
	//RTPRINT(FINIT, INIT_IQK, ("0xeac = 0x%x\n", regEAC));
	regEB4 = PHY_QueryBBReg(pAdapter, rTx_Power_Before_IQK_B, bMaskDWord);
	//RTPRINT(FINIT, INIT_IQK, ("0xeb4 = 0x%x\n", regEB4));
	regEBC= PHY_QueryBBReg(pAdapter, rTx_Power_After_IQK_B, bMaskDWord);
	//RTPRINT(FINIT, INIT_IQK, ("0xebc = 0x%x\n", regEBC));
	regEC4= PHY_QueryBBReg(pAdapter, rRx_Power_Before_IQK_B_2, bMaskDWord);
	//RTPRINT(FINIT, INIT_IQK, ("0xec4 = 0x%x\n", regEC4));
	regECC= PHY_QueryBBReg(pAdapter, rRx_Power_After_IQK_B_2, bMaskDWord);
	//RTPRINT(FINIT, INIT_IQK, ("0xecc = 0x%x\n", regECC));

	if(!(regEAC & BIT31) &&
		(((regEB4 & 0x03FF0000)>>16) != 0x142) &&
		(((regEBC & 0x03FF0000)>>16) != 0x42))
		result |= 0x01;
	else
		return result;

	if(!(regEAC & BIT30) &&
		(((regEC4 & 0x03FF0000)>>16) != 0x132) &&
		(((regECC & 0x03FF0000)>>16) != 0x36))
		result |= 0x02;
	else
		DBG_8192C("Path B Rx IQK fail!!\n");
	

	return result;

}

static u8				//bit0 = 1 => Tx OK, bit1 = 1 => Rx OK
phy_PathB_IQK_5G_Normal(
	IN	PADAPTER	pAdapter
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	u32	regEAC, regEB4, regEBC, regEC4, regECC;
	u8	result = 0x00;
	u8	i = 0;
#if MP_DRIVER == 1
	u8	retryCount = 9;
#else
	u8	retryCount = 2;
#endif
	u8	timeout = 20, timecount = 0;

	//RTPRINT(FINIT, INIT_IQK, ("Path B IQK!\n"));

	//path-A IQK setting
	//RTPRINT(FINIT, INIT_IQK, ("Path-A IQK setting!\n"));
	PHY_SetBBReg(pAdapter, rTx_IQK_Tone_A, bMaskDWord, 0x18008c1f);
	PHY_SetBBReg(pAdapter, rRx_IQK_Tone_A, bMaskDWord, 0x18008c1f);

	PHY_SetBBReg(pAdapter, rTx_IQK_PI_A, bMaskDWord, 0x82110000);
	PHY_SetBBReg(pAdapter, rRx_IQK_PI_A, bMaskDWord, 0x68110000);

	//path-B IQK setting
	PHY_SetBBReg(pAdapter, rTx_IQK_Tone_B, bMaskDWord, 0x18008c2f );
	PHY_SetBBReg(pAdapter, rRx_IQK_Tone_B, bMaskDWord, 0x18008c2f );
	PHY_SetBBReg(pAdapter, rTx_IQK_PI_B, bMaskDWord, 0x82140307);
	PHY_SetBBReg(pAdapter, rRx_IQK_PI_B, bMaskDWord, 0x68160960);

	//LO calibration setting
	//RTPRINT(FINIT, INIT_IQK, ("LO calibration setting!\n"));
	PHY_SetBBReg(pAdapter, rIQK_AGC_Rsp, bMaskDWord, 0x00462911);

	//path-B PA on
#if 0	
	PHY_SetBBReg(pAdapter, rFPGA0_XAB_RFInterfaceSW, BIT27|BIT26, 0x03);
	PHY_SetBBReg(pAdapter, rFPGA0_XAB_RFInterfaceSW, BIT22|BIT21, 0x03);
	PHY_SetBBReg(pAdapter, rFPGA0_XB_RFInterfaceOE, BIT10|BIT11, 0x03);
#else
	PHY_SetBBReg(pAdapter, rFPGA0_XAB_RFInterfaceSW, bMaskDWord, 0x0f600700);	
	PHY_SetBBReg(pAdapter, rFPGA0_XB_RFInterfaceOE, bMaskDWord, 0x061f0d30);
#endif

	for(i = 0 ; i < retryCount ; i++)
	{
		//One shot, path B LOK & IQK
		//RTPRINT(FINIT, INIT_IQK, ("One shot, path A LOK & IQK!\n"));
		//PHY_SetBBReg(pAdapter, 0xe60, bMaskDWord, 0x00000002);
		//PHY_SetBBReg(pAdapter, 0xe60, bMaskDWord, 0x00000000);
		PHY_SetBBReg(pAdapter, rIQK_AGC_Pts, bMaskDWord, 0xfa000000);
		PHY_SetBBReg(pAdapter, rIQK_AGC_Pts, bMaskDWord, 0xf8000000);


		// delay x ms
		//RTPRINT(FINIT, INIT_IQK, ("Delay %d ms for One shot, path B LOK & IQK.\n", 10));
		//rtw_udelay_os(IQK_DELAY_TIME*1000*10);
		rtw_mdelay_os(IQK_DELAY_TIME*10);

		while(timecount < timeout && PHY_QueryBBReg(pAdapter, rRx_Power_After_IQK_A_2, BIT29) == 0x00)
		{
			rtw_udelay_os(IQK_DELAY_TIME*1000*2);
			timecount++;
			//RTPRINT(FINIT, INIT_IQK, ("Delay %d ms for polling 0xeac bit29\n", timecount*2));
		}

		timecount = 0;
		while(timecount < timeout && PHY_QueryBBReg(pAdapter, rRx_Power_Before_IQK_B_2, 0x3FF0000) == 0x00)
		{
			rtw_udelay_os(IQK_DELAY_TIME*1000*2);
			timecount++;
			//RTPRINT(FINIT, INIT_IQK, ("Delay %d ms for polling 0xec4[25:16]\n", timecount*2));
		}

		//RTPRINT(FINIT, INIT_IQK, ("0xec0 = 0x%lx\n", PHY_QueryBBReg(pAdapter, 0xec0, bMaskDWord)));
		//RTPRINT(FINIT, INIT_IQK, ("0xec8 = 0x%lx\n", PHY_QueryBBReg(pAdapter, 0xec8, bMaskDWord)));

		// Check failed
		regEAC = PHY_QueryBBReg(pAdapter, rRx_Power_After_IQK_A_2, bMaskDWord);
		//RTPRINT(FINIT, INIT_IQK, ("0xeac = 0x%lx\n", regEAC));
		regEB4 = PHY_QueryBBReg(pAdapter, rTx_Power_Before_IQK_B, bMaskDWord);
		//RTPRINT(FINIT, INIT_IQK, ("0xeb4 = 0x%lx\n", regEB4));
		regEBC= PHY_QueryBBReg(pAdapter, rTx_Power_After_IQK_B, bMaskDWord);
		//RTPRINT(FINIT, INIT_IQK, ("0xebc = 0x%lx\n", regEBC));
		regEC4= PHY_QueryBBReg(pAdapter, rRx_Power_Before_IQK_B_2, bMaskDWord);
		//RTPRINT(FINIT, INIT_IQK, ("0xec4 = 0x%lx\n", regEC4));
		regECC= PHY_QueryBBReg(pAdapter, rRx_Power_After_IQK_B_2, bMaskDWord);
		//RTPRINT(FINIT, INIT_IQK, ("0xecc = 0x%lx\n", regECC));

		if(!(regEAC & BIT31) &&
			(((regEB4 & 0x03FF0000)>>16) != 0x142))
			result |= 0x01;
		else
			continue;

		if(!(regEAC & BIT30) &&
			(((regEC4 & 0x03FF0000)>>16) != 0x132))
		{
			result |= 0x02;
			break;
		}
		else
		{
			//RTPRINT(FINIT, INIT_IQK, ("Path B Rx IQK fail!!\n"));		
		}
	}

	//path B PA off
	PHY_SetBBReg(pAdapter, rFPGA0_XAB_RFInterfaceSW, bMaskDWord, pdmpriv->IQK_BB_backup[0]);
	PHY_SetBBReg(pAdapter, rFPGA0_XB_RFInterfaceOE, bMaskDWord, pdmpriv->IQK_BB_backup[2]);

	if(!(result & 0x01))	//Tx IQK fail
	{
		PHY_SetBBReg(pAdapter, rTx_IQK_Tone_B, bMaskDWord, 0x19008c00);
	}

	if(!(result & 0x02))	//Rx IQK fail
	{
		PHY_SetBBReg(pAdapter, rOFDM0_XBRxIQImbalance , bMaskDWord, 0x40000100);
		PHY_SetBBReg(pAdapter, rRx_IQK_Tone_B , bMaskDWord, 0x19008c00);
		DBG_871X("Path B Rx IQK fail!!0xe54 = 0x%x\n", PHY_QueryBBReg(pAdapter, rRx_IQK_Tone_B, bMaskDWord));
	}

	return result;
}

static VOID
phy_PathAFillIQKMatrix(
	IN PADAPTER	pAdapter,
	IN BOOLEAN	bIQKOK,
	IN int		result[][8],
	IN u8		final_candidate,
	IN BOOLEAN	bTxOnly
	)
{
	u32	Oldval_0, X, TX0_A, reg;
	int	Y, TX0_C;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);	
	
	//DBG_8192C("Path A IQ Calibration %s !\n",(bIQKOK)?"Success":"Failed");

	if(final_candidate == 0xFF)
		return;

	else if(bIQKOK)
	{
		Oldval_0 = (PHY_QueryBBReg(pAdapter, rOFDM0_XATxIQImbalance, bMaskDWord) >> 22) & 0x3FF;//OFDM0_D

		X = result[final_candidate][0];
		if ((X & 0x00000200) != 0)
			X = X | 0xFFFFFC00;				
		TX0_A = (X * Oldval_0) >> 8;
		//RTPRINT(FINIT, INIT_IQK, ("X = 0x%lx, TX0_A = 0x%lx, Oldval_0 0x%lx\n", X, TX0_A, Oldval_0));
		PHY_SetBBReg(pAdapter, rOFDM0_XATxIQImbalance, 0x3FF, TX0_A);
		if(IS_HARDWARE_TYPE_8192D(pAdapter))
			PHY_SetBBReg(pAdapter, rOFDM0_ECCAThreshold, BIT24, ((X* Oldval_0>>7) & 0x1));
		else
			PHY_SetBBReg(pAdapter, rOFDM0_ECCAThreshold, BIT(31), ((X* Oldval_0>>7) & 0x1));

		Y = result[final_candidate][1];
		if ((Y & 0x00000200) != 0)
			Y = Y | 0xFFFFFC00;

		//path B IQK result + 3
		if(pHalData->interfaceIndex == 1 && pHalData->CurrentBandType92D == BAND_ON_5G)
			Y += 3;
		
		TX0_C = (Y * Oldval_0) >> 8;
		//RTPRINT(FINIT, INIT_IQK, ("Y = 0x%lx, TX0_C = 0x%lx\n", Y, TX0_C));
		PHY_SetBBReg(pAdapter, rOFDM0_XCTxAFE, 0xF0000000, ((TX0_C&0x3C0)>>6));
		PHY_SetBBReg(pAdapter, rOFDM0_XATxIQImbalance, 0x003F0000, (TX0_C&0x3F));
		if(IS_HARDWARE_TYPE_8192D(pAdapter)/*&&is2T*/)
			PHY_SetBBReg(pAdapter, rOFDM0_ECCAThreshold, BIT26, ((Y* Oldval_0>>7) & 0x1));
		else
			PHY_SetBBReg(pAdapter, rOFDM0_ECCAThreshold, BIT(29), ((Y* Oldval_0>>7) & 0x1));

	        if(bTxOnly)
		{
			//DBG_8192C("_PHY_PathAFillIQKMatrix only Tx OK\n");
			return;
		}

		reg = result[final_candidate][2];
		PHY_SetBBReg(pAdapter, rOFDM0_XARxIQImbalance, 0x3FF, reg);

		reg = result[final_candidate][3] & 0x3F;
		PHY_SetBBReg(pAdapter, rOFDM0_XARxIQImbalance, 0xFC00, reg);

		reg = (result[final_candidate][3] >> 6) & 0xF;
		PHY_SetBBReg(pAdapter, rOFDM0_RxIQExtAnta, 0xF0000000, reg);
	}
}

static VOID
phy_PathAFillIQKMatrix_5G_Normal(
	IN PADAPTER	pAdapter,
	IN BOOLEAN	bIQKOK,
	IN int		result[][8],
	IN u8		final_candidate,
	IN BOOLEAN	bTxOnly
	)
{
	u32	X, reg;
	int	Y;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	//BOOLEAN		is2T =  IS_92D_SINGLEPHY(pHalData->VersionID) || 
	//				pHalData->MacPhyMode92D == DUALMAC_DUALPHY;

	//DBG_8192C("Path A IQ Calibration %s !\n",(bIQKOK)?"Success":"Failed");

	if(bIQKOK && final_candidate != 0xFF)
	{
		X = result[final_candidate][0];
		if ((X & 0x00000200) != 0)
			X = X | 0xFFFFFC00;

		//RTPRINT(FINIT, INIT_IQK, ("X = 0x%lx\n", X));
		PHY_SetBBReg(pAdapter, rTx_IQK_Tone_A, 0x3FF0000, X);
		PHY_SetBBReg(pAdapter, rOFDM0_ECCAThreshold, BIT24, 0);

		//RTPRINT(FINIT, INIT_IQK, ("0xe30 = 0x%lx\n", PHY_QueryBBReg(pAdapter, 0xe30, bMaskDWord)));

		Y = result[final_candidate][1];
		if ((Y & 0x00000200) != 0)
			Y = Y | 0xFFFFFC00;	

		//path A/B IQK result + 3, suggest by Jenyu
		if(pHalData->CurrentBandType92D == BAND_ON_5G)
			Y += 3;

		//RTPRINT(FINIT, INIT_IQK, ("Y = 0x%lx\n", Y));
		PHY_SetBBReg(pAdapter, rTx_IQK_Tone_A, 0x003FF, Y);
		//if(is2T)
			PHY_SetBBReg(pAdapter, rOFDM0_ECCAThreshold, BIT26, 0);

		//RTPRINT(FINIT, INIT_IQK, ("0xe30 = 0x%lx\n", PHY_QueryBBReg(pAdapter, 0xe30, bMaskDWord)));

		if(bTxOnly)
		{
			//DBG_8192C("_PHY_PathAFillIQKMatrix only Tx OK\n");
			return;
		}

		reg = result[final_candidate][2];
		PHY_SetBBReg(pAdapter, rOFDM0_XARxIQImbalance, 0x3FF, reg);

		reg = result[final_candidate][3] & 0x3F;
		PHY_SetBBReg(pAdapter, rOFDM0_XARxIQImbalance, 0xFC00, reg);

		reg = (result[final_candidate][3] >> 6) & 0xF;
		PHY_SetBBReg(pAdapter, rOFDM0_RxIQExtAnta, 0xF0000000, reg);
	}
	else
	{
		DBG_871X("phy_PathAFillIQKMatrix Tx/Rx FAIL restore default value\n");
		PHY_SetBBReg(pAdapter, rTx_IQK_Tone_A, bMaskDWord, 0x19008c00);
		PHY_SetBBReg(pAdapter, rOFDM0_XARxIQImbalance , bMaskDWord, 0x40000100);
		PHY_SetBBReg(pAdapter, rRx_IQK_Tone_A , bMaskDWord, 0x19008c00);
	}
}

static VOID
phy_PathBFillIQKMatrix(
	IN PADAPTER	pAdapter,
	IN BOOLEAN	bIQKOK,
	IN int		result[][8],
	IN u8		final_candidate,
	IN BOOLEAN	bTxOnly			//do Tx only
	)
{
	u32	Oldval_1, X, TX1_A, reg;
	int	Y, TX1_C;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	
	//DBG_8192C("Path B IQ Calibration %s !\n",(bIQKOK)?"Success":"Failed");

        if(final_candidate == 0xFF)
		return;

	else if(bIQKOK)
	{
		Oldval_1 = (PHY_QueryBBReg(pAdapter, rOFDM0_XBTxIQImbalance, bMaskDWord) >> 22) & 0x3FF;

		X = result[final_candidate][4];
		if ((X & 0x00000200) != 0)
			X = X | 0xFFFFFC00;		
		TX1_A = (X * Oldval_1) >> 8;
		//RTPRINT(FINIT, INIT_IQK, ("X = 0x%lx, TX1_A = 0x%lx\n", X, TX1_A));
		PHY_SetBBReg(pAdapter, rOFDM0_XBTxIQImbalance, 0x3FF, TX1_A);
		if(IS_HARDWARE_TYPE_8192D(pAdapter))
           		PHY_SetBBReg(pAdapter, rOFDM0_ECCAThreshold, BIT28, ((X* Oldval_1>>7) & 0x1));
		else
			PHY_SetBBReg(pAdapter, rOFDM0_ECCAThreshold, BIT(27), ((X* Oldval_1>>7) & 0x1));

		Y = result[final_candidate][5];
		if ((Y & 0x00000200) != 0)
			Y = Y | 0xFFFFFC00;
		if(pHalData->CurrentBandType92D == BAND_ON_5G)		
			Y += 3;		//temp modify for preformance
		TX1_C = (Y * Oldval_1) >> 8;
		//RTPRINT(FINIT, INIT_IQK, ("Y = 0x%lx, TX1_C = 0x%lx\n", Y, TX1_C));
		PHY_SetBBReg(pAdapter, rOFDM0_XDTxAFE, 0xF0000000, ((TX1_C&0x3C0)>>6));
		PHY_SetBBReg(pAdapter, rOFDM0_XBTxIQImbalance, 0x003F0000, (TX1_C&0x3F));
		if(IS_HARDWARE_TYPE_8192D(pAdapter))
			PHY_SetBBReg(pAdapter, rOFDM0_ECCAThreshold, BIT30, ((Y* Oldval_1>>7) & 0x1));
		else
			PHY_SetBBReg(pAdapter, rOFDM0_ECCAThreshold, BIT(25), ((Y* Oldval_1>>7) & 0x1));

		if(bTxOnly)
			return;

		reg = result[final_candidate][6];
		PHY_SetBBReg(pAdapter, rOFDM0_XBRxIQImbalance, 0x3FF, reg);

		reg = result[final_candidate][7] & 0x3F;
		PHY_SetBBReg(pAdapter, rOFDM0_XBRxIQImbalance, 0xFC00, reg);

		reg = (result[final_candidate][7] >> 6) & 0xF;
		PHY_SetBBReg(pAdapter, rOFDM0_AGCRSSITable, 0x0000F000, reg);
	}
}

static VOID
phy_PathBFillIQKMatrix_5G_Normal(
	IN PADAPTER	pAdapter,
	IN BOOLEAN	bIQKOK,
	IN int		result[][8],
	IN u8		final_candidate,
	IN BOOLEAN	bTxOnly			//do Tx only
	)
{
	u32	X, reg;
	int	Y;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	
	//DBG_8192C("Path B IQ Calibration %s !\n",(bIQKOK)?"Success":"Failed");

	if(bIQKOK && final_candidate != 0xFF)
	{
		X = result[final_candidate][4];
		if ((X & 0x00000200) != 0)
			X = X | 0xFFFFFC00;

		//RTPRINT(FINIT, INIT_IQK, ("X = 0x%lx\n", X));
		PHY_SetBBReg(pAdapter, 0xe50, 0x3FF0000, X);
		PHY_SetBBReg(pAdapter, rOFDM0_ECCAThreshold, BIT28, 0);

		Y = result[final_candidate][5];
		if ((Y & 0x00000200) != 0)
			Y = Y | 0xFFFFFC00;
		if(pHalData->CurrentBandType92D == BAND_ON_5G)
			Y += 3;		//temp modify for preformance, suggest by Jenyu

		//RTPRINT(FINIT, INIT_IQK, ("Y = 0x%lx\n", Y));
		PHY_SetBBReg(pAdapter, rTx_IQK_Tone_B, 0x003FF, Y);
		PHY_SetBBReg(pAdapter, rOFDM0_ECCAThreshold, BIT30, 0);

		if(bTxOnly)
			return;

		reg = result[final_candidate][6];
		PHY_SetBBReg(pAdapter, rOFDM0_XBRxIQImbalance, 0x3FF, reg);

		reg = result[final_candidate][7] & 0x3F;
		PHY_SetBBReg(pAdapter, rOFDM0_XBRxIQImbalance, 0xFC00, reg);

		reg = (result[final_candidate][7] >> 6) & 0xF;
		PHY_SetBBReg(pAdapter, rOFDM0_AGCRSSITable, 0x0000F000, reg);
	}
	else
	{
		DBG_871X("phy_PathBFillIQKMatrix Tx/Rx FAIL\n");
		PHY_SetBBReg(pAdapter, rTx_IQK_Tone_B, bMaskDWord, 0x19008c00);			
		PHY_SetBBReg(pAdapter, rOFDM0_XBRxIQImbalance , bMaskDWord, 0x40000100);	
		PHY_SetBBReg(pAdapter, rRx_IQK_Tone_B , bMaskDWord, 0x19008c00); 		
	}
}

static VOID
phy_SaveADDARegisters(
	IN	PADAPTER	pAdapter,
	IN	u32*		ADDAReg,
	IN	u32*		ADDABackup,
	IN	u32			RegisterNum
	)
{
	u32	i;

	//if (ODM_CheckPowerStatus(pAdapter) == _FALSE)
	//	return;

	//RTPRINT(FINIT, INIT_IQK, ("Save ADDA parameters.\n"));
	for( i = 0 ; i < RegisterNum ; i++){
		ADDABackup[i] = PHY_QueryBBReg(pAdapter, ADDAReg[i], bMaskDWord);
	}
}

static VOID
phy_SaveMACRegisters(
	IN	PADAPTER	pAdapter,
	IN	u32*		MACReg,
	IN	u32*		MACBackup
	)
{
	u32	i;
	
	//RTPRINT(FINIT, INIT_IQK, ("Save MAC parameters.\n"));
	for( i = 0 ; i < (IQK_MAC_REG_NUM - 1); i++){
		MACBackup[i] = rtw_read8(pAdapter, MACReg[i]);		
	}
	MACBackup[i] = rtw_read32(pAdapter, MACReg[i]);		

}

static VOID
phy_ReloadADDARegisters(
	IN	PADAPTER	pAdapter,
	IN	u32*		ADDAReg,
	IN	u32*		ADDABackup,
	IN	u32			RegiesterNum
	)
{
	u32	i;

	//RTPRINT(FINIT, INIT_IQK, ("Reload ADDA power saving parameters !\n"));
	for(i = 0 ; i < RegiesterNum ; i++){
		//path-A/B BB to initial gain		
		if(ADDAReg[i] == rOFDM0_XAAGCCore1 || ADDAReg[i] == rOFDM0_XBAGCCore1)
			PHY_SetBBReg(pAdapter, ADDAReg[i], bMaskDWord, 0x50);			
		PHY_SetBBReg(pAdapter, ADDAReg[i], bMaskDWord, ADDABackup[i]);
	}
}

static VOID
phy_ReloadMACRegisters(
	IN	PADAPTER	pAdapter,
	IN	u32*		MACReg,
	IN	u32*		MACBackup
	)
{
	u32	i;

	//RTPRINT(FINIT, INIT_IQK, ("Reload MAC parameters !\n"));
	for(i = 0 ; i < (IQK_MAC_REG_NUM - 1); i++){
		rtw_write8(pAdapter, MACReg[i], (u8)MACBackup[i]);
	}
	rtw_write32(pAdapter, MACReg[i], MACBackup[i]);	
}

static VOID
phy_PathADDAOn(
	IN	PADAPTER	pAdapter,
	IN	u32*		ADDAReg,
	IN	BOOLEAN		isPathAOn,
	IN	BOOLEAN		is2T
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	u32	pathOn;
	u32	i;

	//RTPRINT(FINIT, INIT_IQK, ("ADDA ON.\n"));

	pathOn = isPathAOn ? 0x04db25a4 : 0x0b1b25a4;
	if(isPathAOn)
		pathOn = pHalData->interfaceIndex == 0? 0x04db25a4 : 0x0b1b25a4;
	for( i = 0 ; i < IQK_ADDA_REG_NUM ; i++){
		PHY_SetBBReg(pAdapter, ADDAReg[i], bMaskDWord, pathOn);
	}
}

static VOID
phy_MACSettingCalibration(
	IN	PADAPTER	pAdapter,
	IN	u32*		MACReg,
	IN	u32*		MACBackup	
	)
{
	u32	i = 0;

	//RTPRINT(FINIT, INIT_IQK, ("MAC settings for Calibration.\n"));

	rtw_write8(pAdapter, MACReg[i], 0x3F);

	for(i = 1 ; i < (IQK_MAC_REG_NUM - 1); i++){
		rtw_write8(pAdapter, MACReg[i], (u8)(MACBackup[i]&(~BIT3)));
	}
	rtw_write8(pAdapter, MACReg[i], (u8)(MACBackup[i]&(~BIT5)));

}

static VOID
phy_PathAStandBy(
	IN	PADAPTER	pAdapter
	)
{
	//RTPRINT(FINIT, INIT_IQK, ("Path-A standby mode!\n"));

	PHY_SetBBReg(pAdapter, rFPGA0_IQK, bMaskDWord, 0x0);
	PHY_SetBBReg(pAdapter, 0x840, bMaskDWord, 0x00010000);
	PHY_SetBBReg(pAdapter, rFPGA0_IQK, bMaskDWord, 0x80800000);
}

static VOID
phy_PIModeSwitch(
	IN	PADAPTER	pAdapter,
	IN	BOOLEAN		PIMode
	)
{
	u32	mode;

	//RTPRINT(FINIT, INIT_IQK, ("BB Switch to %s mode!\n", (PIMode ? "PI" : "SI")));

	mode = PIMode ? 0x01000100 : 0x01000000;
	PHY_SetBBReg(pAdapter, 0x820, bMaskDWord, mode);
	PHY_SetBBReg(pAdapter, 0x828, bMaskDWord, mode);
}

static BOOLEAN							
phy_SimularityCompare_92D(
	IN	PADAPTER	pAdapter,
	IN	int 		result[][8],
	IN	u8		 c1,
	IN	u8		 c2
	)
{
	u32	i, j, diff, SimularityBitMap, bound = 0, u4temp = 0;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);	
	u8	final_candidate[2] = {0xFF, 0xFF};	//for path A and path B
	BOOLEAN		bResult = _TRUE;
	BOOLEAN		is2T = IS_92D_SINGLEPHY(pHalData->VersionID);
	
	if(is2T)
		bound = 8;
	else
		bound = 4;

	SimularityBitMap = 0;

	//check Tx
	for( i = 0; i < bound; i++ )
	{
		diff = (result[c1][i] > result[c2][i]) ? (result[c1][i] - result[c2][i]) : (result[c2][i] - result[c1][i]);
		if (diff > MAX_TOLERANCE_92D)
		{
			if((i == 2 || i == 6) && !SimularityBitMap)
			{
				if(result[c1][i]+result[c1][i+1] == 0)
					final_candidate[(i/4)] = c2;
				else if (result[c2][i]+result[c2][i+1] == 0)
					final_candidate[(i/4)] = c1;
				else
					SimularityBitMap = SimularityBitMap|(1<<i);
			}
			else
				SimularityBitMap = SimularityBitMap|(1<<i);
		}
	}
	
	if ( SimularityBitMap == 0)
	{
		for( i = 0; i < (bound/4); i++ )
		{
			if(final_candidate[i] != 0xFF)
			{
				for( j = i*4; j < (i+1)*4-2; j++)
					result[3][j] = result[final_candidate[i]][j];
				bResult = _FALSE;
			}
		}

		for( i = 0; i < bound; i++ )
		{
			u4temp += (result[c1][i]+ 	result[c2][i]);
		}
		if(u4temp == 0)	//IQK fail for c1 & c2
			bResult = _FALSE;
		
		return bResult;
	}

	if (!(SimularityBitMap & 0x0F))			//path A OK
	{
		for(i = 0; i < 4; i++)
			result[3][i] = result[c1][i];
	}
	else if (!(SimularityBitMap & 0x03)) 		//path A, Tx OK
	{
		for(i = 0; i < 2; i++)
			result[3][i] = result[c1][i];
	}

	if (!(SimularityBitMap & 0xF0) && is2T)		//path B OK
	{
		for(i = 4; i < 8; i++)
			result[3][i] = result[c1][i];
	}
	else if (!(SimularityBitMap & 0x30)) 		//path B, Tx OK
	{
		for(i = 4; i < 6; i++)
			result[3][i] = result[c1][i];
	}

	return _FALSE;
	
}

/*
return _FALSE => do IQK again
*/
static BOOLEAN							
phy_SimularityCompare(
	IN	PADAPTER	pAdapter,
	IN	int 		result[][8],
	IN	u8		 c1,
	IN	u8		 c2
	)
{
	return phy_SimularityCompare_92D(pAdapter, result, c1, c2);
}

static VOID	
phy_IQCalibrate(
	IN	PADAPTER	pAdapter,
	IN	int 		result[][8],
	IN	u8		t,
	IN	BOOLEAN		is2T
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	u32			i;
	u8			PathAOK, PathBOK;
	u32			ADDA_REG[IQK_ADDA_REG_NUM] = {	
						rFPGA0_XCD_SwitchControl, 	rBlue_Tooth, 	
						rRx_Wait_CCA, 		rTx_CCK_RFON,
						rTx_CCK_BBON, 	rTx_OFDM_RFON, 	
						rTx_OFDM_BBON, 	rTx_To_Rx,
						rTx_To_Tx, 		rRx_CCK, 	
						rRx_OFDM, 		rRx_Wait_RIFS,
						rRx_TO_Rx, 		rStandby, 	
						rSleep, 			rPMPD_ANAEN };
	u32			IQK_MAC_REG[IQK_MAC_REG_NUM] = {
						REG_TXPAUSE, 		REG_BCN_CTRL,	
						REG_BCN_CTRL_1,	REG_GPIO_MUXCFG};

	//since 92C & 92D have the different define in IQK_BB_REG	
	u32	IQK_BB_REG_92C[IQK_BB_REG_NUM_92C] = {
							rOFDM0_TRxPathEnable, 		rOFDM0_TRMuxPar,	
							rFPGA0_XCD_RFInterfaceSW,	rConfig_AntA,	rConfig_AntB,
							rFPGA0_XAB_RFInterfaceSW,	rFPGA0_XA_RFInterfaceOE,	
							rFPGA0_XB_RFInterfaceOE,	rFPGA0_RFMOD	
							};	

	u32	IQK_BB_REG_92D[IQK_BB_REG_NUM_92D] = {	//for normal
							rFPGA0_XAB_RFInterfaceSW,	rFPGA0_XA_RFInterfaceOE,	
							rFPGA0_XB_RFInterfaceOE,	rOFDM0_TRMuxPar,
							rFPGA0_XCD_RFInterfaceSW,	rOFDM0_TRxPathEnable,	
							rFPGA0_RFMOD,			rFPGA0_AnalogParameter4,
							rOFDM0_XAAGCCore1,		rOFDM0_XBAGCCore1						
						};

#if MP_DRIVER
	const u32	retryCount = 9;
#else
	const u32	retryCount = 2;
#endif

	// Note: IQ calibration must be performed after loading 
	// 		PHY_REG.txt , and radio_a, radio_b.txt	
	
	u32 bbvalue;

	if(t==0)
	{
	 	bbvalue = PHY_QueryBBReg(pAdapter, rFPGA0_RFMOD, bMaskDWord);
		//RTPRINT(FINIT, INIT_IQK, ("PHY_IQCalibrate()==>0x%08lx\n",bbvalue));
		//RTPRINT(FINIT, INIT_IQK, ("IQ Calibration for %s\n", (is2T ? "2T2R" : "1T1R")));

	 	// Save ADDA parameters, turn Path A ADDA on
	 	phy_SaveADDARegisters(pAdapter, ADDA_REG, pdmpriv->ADDA_backup, IQK_ADDA_REG_NUM);
		phy_SaveMACRegisters(pAdapter, IQK_MAC_REG, pdmpriv->IQK_MAC_backup);
		if(IS_HARDWARE_TYPE_8192D(pAdapter))
		 	phy_SaveADDARegisters(pAdapter, IQK_BB_REG_92D, pdmpriv->IQK_BB_backup, IQK_BB_REG_NUM_92D);
		else
			phy_SaveADDARegisters(pAdapter, IQK_BB_REG_92C, pdmpriv->IQK_BB_backup, IQK_BB_REG_NUM_92C);
	}

 	phy_PathADDAOn(pAdapter, ADDA_REG, _TRUE, is2T);

	if(IS_HARDWARE_TYPE_8192D(pAdapter))
		PHY_SetBBReg(pAdapter, rPdp_AntA, bMaskDWord, 0x01017038);

	if(t==0)
	{
		pdmpriv->bRfPiEnable = (u8)PHY_QueryBBReg(pAdapter, rFPGA0_XA_HSSIParameter1, BIT(8));
	}

	if(!pdmpriv->bRfPiEnable){
		// Switch BB to PI mode to do IQ Calibration.
		phy_PIModeSwitch(pAdapter, _TRUE);
	}

	PHY_SetBBReg1Byte(pAdapter, rFPGA0_RFMOD, BIT24, 0x00);
	PHY_SetBBReg(pAdapter, rOFDM0_TRxPathEnable, bMaskDWord, 0x03a05600);
	PHY_SetBBReg(pAdapter, rOFDM0_TRMuxPar, bMaskDWord, 0x000800e4);
	PHY_SetBBReg(pAdapter, rFPGA0_XCD_RFInterfaceSW, bMaskDWord, 0x22204000);
	if(IS_HARDWARE_TYPE_8192D(pAdapter))
		PHY_SetBBReg(pAdapter, rFPGA0_AnalogParameter4, 0xf00000, 0x0f); 
	else
	{
		PHY_SetBBReg(pAdapter, rFPGA0_XAB_RFInterfaceSW, BIT10, 0x01);
		PHY_SetBBReg(pAdapter, rFPGA0_XAB_RFInterfaceSW, BIT26, 0x01);	
		PHY_SetBBReg(pAdapter, rFPGA0_XA_RFInterfaceOE, BIT10, 0x00);
		PHY_SetBBReg(pAdapter, rFPGA0_XB_RFInterfaceOE, BIT10, 0x00);	
	}

	if(is2T)
	{
		PHY_SetBBReg(pAdapter, rFPGA0_XA_LSSIParameter, bMaskDWord, 0x00010000);
		PHY_SetBBReg(pAdapter, rFPGA0_XB_LSSIParameter, bMaskDWord, 0x00010000);
	}

	//MAC settings
	phy_MACSettingCalibration(pAdapter, IQK_MAC_REG, pdmpriv->IQK_MAC_backup);

	if(IS_HARDWARE_TYPE_8192D(pAdapter))
	{
		PHY_SetBBReg(pAdapter, rConfig_AntA, bMaskDWord, 0x0f600000);
		
		if(is2T)
		{
			PHY_SetBBReg(pAdapter, rConfig_AntB, bMaskDWord, 0x0f600000);
		}
	}
	else
	{
		//Page B init
		PHY_SetBBReg(pAdapter, rConfig_AntA, bMaskDWord, 0x00080000);
		
		if(is2T)
		{
			PHY_SetBBReg(pAdapter, rConfig_AntB, bMaskDWord, 0x00080000);
		}
	}

	// IQ calibration setting
	//RTPRINT(FINIT, INIT_IQK, ("IQK setting!\n"));		
	PHY_SetBBReg(pAdapter, rFPGA0_IQK, bMaskDWord, 0x80800000);
	PHY_SetBBReg(pAdapter, rTx_IQK, bMaskDWord, 0x01007c00);
	PHY_SetBBReg(pAdapter, rRx_IQK, bMaskDWord, 0x01004800);

	for(i = 0 ; i < retryCount ; i++){
		PathAOK = phy_PathA_IQK(pAdapter, is2T);
		if(PathAOK == 0x03){
			//RTPRINT(FINIT, INIT_IQK, ("Path A IQK Success!!\n"));
			result[t][0] = (PHY_QueryBBReg(pAdapter, rTx_Power_Before_IQK_A, bMaskDWord)&0x3FF0000)>>16;
			result[t][1] = (PHY_QueryBBReg(pAdapter, rTx_Power_After_IQK_A, bMaskDWord)&0x3FF0000)>>16;
			result[t][2] = (PHY_QueryBBReg(pAdapter, rRx_Power_Before_IQK_A_2, bMaskDWord)&0x3FF0000)>>16;
			result[t][3] = (PHY_QueryBBReg(pAdapter, rRx_Power_After_IQK_A_2, bMaskDWord)&0x3FF0000)>>16;
			break;
		}
		else if (i == (retryCount-1) && PathAOK == 0x01)	//Tx IQK OK
		{
			//RTPRINT(FINIT, INIT_IQK, ("Path A IQK Only  Tx Success!!\n"));
			
			result[t][0] = (PHY_QueryBBReg(pAdapter, rTx_Power_Before_IQK_A, bMaskDWord)&0x3FF0000)>>16;
			result[t][1] = (PHY_QueryBBReg(pAdapter, rTx_Power_After_IQK_A, bMaskDWord)&0x3FF0000)>>16;
		}
	}

	if(0x00 == PathAOK){		
		DBG_871X("Path A IQK failed!!\n");
	}

	if(is2T){
		phy_PathAStandBy(pAdapter);

		// Turn Path B ADDA on
		phy_PathADDAOn(pAdapter, ADDA_REG, _FALSE, is2T);

		for(i = 0 ; i < retryCount ; i++){
			PathBOK = phy_PathB_IQK(pAdapter);
			if(PathBOK == 0x03){
				//RTPRINT(FINIT, INIT_IQK, ("Path B IQK Success!!\n"));
				result[t][4] = (PHY_QueryBBReg(pAdapter, rTx_Power_Before_IQK_B, bMaskDWord)&0x3FF0000)>>16;
				result[t][5] = (PHY_QueryBBReg(pAdapter, rTx_Power_After_IQK_B, bMaskDWord)&0x3FF0000)>>16;
				result[t][6] = (PHY_QueryBBReg(pAdapter, rRx_Power_Before_IQK_B_2, bMaskDWord)&0x3FF0000)>>16;
				result[t][7] = (PHY_QueryBBReg(pAdapter, rRx_Power_After_IQK_B_2, bMaskDWord)&0x3FF0000)>>16;
				break;
			}
			else if (i == (retryCount - 1) && PathBOK == 0x01)	//Tx IQK OK
			{
				//RTPRINT(FINIT, INIT_IQK, ("Path B Only Tx IQK Success!!\n"));
				result[t][4] = (PHY_QueryBBReg(pAdapter, rTx_Power_Before_IQK_B, bMaskDWord)&0x3FF0000)>>16;
				result[t][5] = (PHY_QueryBBReg(pAdapter, rTx_Power_After_IQK_B, bMaskDWord)&0x3FF0000)>>16;
			}
		}

		if(0x00 == PathBOK){		
			DBG_871X("Path B IQK failed!!\n");
		}
	}

	//Back to BB mode, load original value
	//RTPRINT(FINIT, INIT_IQK, ("IQK:Back to BB mode, load original value!\n"));
	PHY_SetBBReg(pAdapter, rFPGA0_IQK, bMaskDWord, 0);

	if(t!=0)
	{
		if(!pdmpriv->bRfPiEnable){
			// Switch back BB to SI mode after finish IQ Calibration.
			phy_PIModeSwitch(pAdapter, _FALSE);
		}

	 	// Reload ADDA power saving parameters
	 	phy_ReloadADDARegisters(pAdapter, ADDA_REG, pdmpriv->ADDA_backup, IQK_ADDA_REG_NUM);

		// Reload MAC parameters
		phy_ReloadMACRegisters(pAdapter, IQK_MAC_REG, pdmpriv->IQK_MAC_backup);		

	 	// Reload BB parameters
	 	if(IS_HARDWARE_TYPE_8192D(pAdapter))
	 	{
			if(is2T)
		 		phy_ReloadADDARegisters(pAdapter, IQK_BB_REG_92D, pdmpriv->IQK_BB_backup, IQK_BB_REG_NUM_92D);
			else
		 		phy_ReloadADDARegisters(pAdapter, IQK_BB_REG_92D, pdmpriv->IQK_BB_backup, IQK_BB_REG_NUM_92D -1);			
	 	}
		else
		 	phy_ReloadADDARegisters(pAdapter, IQK_BB_REG_92C, pdmpriv->IQK_BB_backup, IQK_BB_REG_NUM_92C);

		if(!IS_HARDWARE_TYPE_8192D(pAdapter))
		{
			// Restore RX initial gain
			PHY_SetBBReg(pAdapter, rFPGA0_XA_LSSIParameter, bMaskDWord, 0x00032ed3);
			if(is2T){
				PHY_SetBBReg(pAdapter, rFPGA0_XB_LSSIParameter, bMaskDWord, 0x00032ed3);
			}
		}

		//load 0xe30 IQC default value
		PHY_SetBBReg(pAdapter, rTx_IQK_Tone_A, bMaskDWord, 0x01008c00);
		PHY_SetBBReg(pAdapter, rRx_IQK_Tone_A, bMaskDWord, 0x01008c00);

	}
	//RTPRINT(FINIT, INIT_IQK, ("_PHY_IQCalibrate() <==\n"));

}


static VOID	
phy_IQCalibrate_5G(
	IN	PADAPTER	pAdapter,
	IN	int 		result[][8]
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	u32			extPAon, REG0xe5c, RX0REG0xe40, REG0xe40, REG0xe94, REG0xe9c;
	u32			REG0xeac, RX1REG0xe40, REG0xeb4, REG0xea4,REG0xec4;
	u8			TX0IQKOK = _FALSE, TX1IQKOK = _FALSE ;
	u32			TX_X0, TX_Y0, TX_X1, TX_Y1, RX_X0, RX_Y0, RX_X1, RX_Y1;
	u32			ADDA_REG[IQK_ADDA_REG_NUM] = {	
						rFPGA0_XCD_SwitchControl, 	rBlue_Tooth, 	
						rRx_Wait_CCA, 		rTx_CCK_RFON,
						rTx_CCK_BBON, 	rTx_OFDM_RFON, 	
						rTx_OFDM_BBON, 	rTx_To_Rx,
						rTx_To_Tx, 		rRx_CCK, 	
						rRx_OFDM, 		rRx_Wait_RIFS,
						rRx_TO_Rx, 		rStandby, 	
						rSleep, 			rPMPD_ANAEN };

	u32			IQK_MAC_REG[IQK_MAC_REG_NUM] = {
						REG_TXPAUSE, 		REG_BCN_CTRL,	
						REG_BCN_CTRL_1,	REG_GPIO_MUXCFG};			

	u32			IQK_BB_REG[IQK_BB_REG_NUM_test] = {	//for normal
						rFPGA0_XAB_RFInterfaceSW,	rOFDM0_TRMuxPar,	
						rFPGA0_XCD_RFInterfaceSW,	rOFDM0_TRxPathEnable,
						rFPGA0_RFMOD,			rFPGA0_AnalogParameter4					
					};

	BOOLEAN       		is2T =  IS_92D_SINGLEPHY(pHalData->VersionID);

	DBG_8192C("IQK for 5G:Start!!!interface %d\n", pHalData->interfaceIndex);

	DBG_8192C("IQ Calibration for %s\n", (is2T ? "2T2R" : "1T1R"));

	//Save MAC default value
	phy_SaveMACRegisters(pAdapter, IQK_MAC_REG, pdmpriv->IQK_MAC_backup);

 	//Save BB Parameter
	phy_SaveADDARegisters(pAdapter, IQK_BB_REG, pdmpriv->IQK_BB_backup, IQK_BB_REG_NUM_test);		

	//Save AFE Parameters
	phy_SaveADDARegisters(pAdapter, ADDA_REG, pdmpriv->ADDA_backup, IQK_ADDA_REG_NUM);

	//1 Path-A TX IQK
	//Path-A AFE all on
 	phy_PathADDAOn(pAdapter, ADDA_REG, _TRUE, _TRUE);

	//MAC register setting
	phy_MACSettingCalibration(pAdapter, IQK_MAC_REG, pdmpriv->IQK_MAC_backup);

	//IQK must be done in PI mode	
	pdmpriv->bRfPiEnable = (u8)PHY_QueryBBReg(pAdapter, rFPGA0_XA_HSSIParameter1, BIT(8));
	if(!pdmpriv->bRfPiEnable)
		phy_PIModeSwitch(pAdapter, _TRUE);

	//TXIQK RF setting
	PHY_SetBBReg(pAdapter, rFPGA0_XA_LSSIParameter, bMaskDWord, 0x01940000);
	PHY_SetBBReg(pAdapter, rFPGA0_XB_LSSIParameter, bMaskDWord, 0x01940000);

	//BB setting
	PHY_SetBBReg(pAdapter, rOFDM0_TRxPathEnable, bMaskDWord, 0x03a05600);
	PHY_SetBBReg(pAdapter, rOFDM0_TRMuxPar, bMaskDWord, 0x000800e4);
	PHY_SetBBReg(pAdapter, rFPGA0_XCD_RFInterfaceSW, bMaskDWord, 0x22208000);
	PHY_SetBBReg(pAdapter, rFPGA0_XAB_RFInterfaceSW, BIT6|BIT5,  0x03);
	PHY_SetBBReg(pAdapter, rFPGA0_XAB_RFInterfaceSW, BIT22|BIT21,  0x03);
	PHY_SetBBReg(pAdapter, rFPGA0_AnalogParameter4, 0xf00000,  0x0f);

	//AP or IQK
	PHY_SetBBReg(pAdapter, rConfig_AntA, bMaskDWord, 0x0f600000);
	PHY_SetBBReg(pAdapter, rConfig_AntB, bMaskDWord, 0x0f600000);

	//IQK global setting
	PHY_SetBBReg(pAdapter, rFPGA0_IQK, bMaskDWord, 0x80800000);
	PHY_SetBBReg(pAdapter, rTx_IQK, bMaskDWord, 0x10007c00);
	PHY_SetBBReg(pAdapter, rRx_IQK, bMaskDWord, 0x01004800);

	//path-A IQK setting
	if(pHalData->interfaceIndex == 0)
	{
		PHY_SetBBReg(pAdapter, rTx_IQK_Tone_A, bMaskDWord, 0x14008c1f);
		PHY_SetBBReg(pAdapter, rRx_IQK_Tone_A, bMaskDWord, 0x30008c1f);
	}
	else
	{
		PHY_SetBBReg(pAdapter, rTx_IQK_Tone_A, bMaskDWord, 0x14008c22);
		PHY_SetBBReg(pAdapter, rRx_IQK_Tone_A, bMaskDWord, 0x30008c22);	
	}

	if(is2T)
		PHY_SetBBReg(pAdapter, rTx_IQK_PI_A, bMaskDWord, 0x821402e2);
	else
		PHY_SetBBReg(pAdapter, rTx_IQK_PI_A, bMaskDWord, 0x821402e6);	
	PHY_SetBBReg(pAdapter, rRx_IQK_PI_A, bMaskDWord, 0x68110000);

	//path-B IQK setting
	if(is2T)
	{
		PHY_SetBBReg(pAdapter, rTx_IQK_Tone_B, bMaskDWord, 0x14008c22);
		PHY_SetBBReg(pAdapter, rRx_IQK_Tone_B, bMaskDWord, 0x30008c22);
		PHY_SetBBReg(pAdapter, rTx_IQK_PI_B, bMaskDWord, 0x82110000);
		PHY_SetBBReg(pAdapter, rRx_IQK_PI_B, bMaskDWord, 0x68110000);
	}

	//LO calibration setting
	PHY_SetBBReg(pAdapter, rIQK_AGC_Rsp, bMaskDWord, 0x00462911);

	//RTPRINT(FINIT, INIT_IQK, ("0x522 %x\n", rtw_read8(pAdapter, 0x522)));

	//One shot, path A LOK & IQK
	PHY_SetBBReg(pAdapter, rIQK_AGC_Pts, bMaskDWord, 0xf9000000);
	PHY_SetBBReg(pAdapter, rIQK_AGC_Pts, bMaskDWord, 0xf8000000);

	//Delay 1 ms
	rtw_udelay_os(IQK_DELAY_TIME*1000);

	//Exit IQK mode
	PHY_SetBBReg(pAdapter, rFPGA0_IQK, bMaskDWord, 0x00000000);

	//Check_TX_IQK_A_result()
	REG0xe40 = PHY_QueryBBReg(pAdapter, rTx_IQK, bMaskDWord);
	REG0xeac = PHY_QueryBBReg(pAdapter, rRx_Power_After_IQK_A_2, bMaskDWord);
	REG0xe94 = PHY_QueryBBReg(pAdapter, rTx_Power_Before_IQK_A, bMaskDWord);

	if(((REG0xeac&BIT(28)) == 0) && (((REG0xe94&0x3FF0000)>>16)!=0x142))
	{		
		REG0xe9c = PHY_QueryBBReg(pAdapter, rTx_Power_After_IQK_A, bMaskDWord);
		TX_X0 = (PHY_QueryBBReg(pAdapter, rTx_Power_Before_IQK_A, bMaskDWord)&0x3FF0000)>>16;
		TX_Y0 = (PHY_QueryBBReg(pAdapter, rTx_Power_After_IQK_A, bMaskDWord)&0x3FF0000)>>16;
		RX0REG0xe40 =  0x80000000 | (REG0xe40 & 0xfc00fc00) | (TX_X0<<16) | TX_Y0;
		result[0][0] = TX_X0;
		result[0][1] = TX_Y0;
		TX0IQKOK = _TRUE;
		DBG_8192C("IQK for 5G: Path A TxOK interface %u\n", pHalData->interfaceIndex);
	}
	else
	{
		DBG_8192C("IQK for 5G: Path A Tx Fail interface %u\n", pHalData->interfaceIndex);
	}

	//1 path A RX IQK
	if(TX0IQKOK == _TRUE)
	{

		DBG_8192C("IQK for 5G: Path A Rx  START interface %u\n", pHalData->interfaceIndex);

		//TXIQK RF setting
		PHY_SetBBReg(pAdapter, rFPGA0_XA_LSSIParameter, bMaskDWord, 0x01900000);
		PHY_SetBBReg(pAdapter, rFPGA0_XB_LSSIParameter, bMaskDWord, 0x01900000);

		//turn on external PA
		if(pHalData->interfaceIndex == 1)
			PHY_SetBBReg(pAdapter, rFPGA0_XAB_RFParameter, BIT(30), 0x01);

		//IQK global setting
		PHY_SetBBReg(pAdapter, rFPGA0_IQK, bMaskDWord, 0x80800000);

		//path-A IQK setting
		if(pHalData->interfaceIndex == 0)
		{
			PHY_SetBBReg(pAdapter, rTx_IQK_Tone_A, bMaskDWord, 0x14008c1f);
			PHY_SetBBReg(pAdapter, rRx_IQK_Tone_A, bMaskDWord, 0x14008c1f);
		}
		else
		{
			PHY_SetBBReg(pAdapter, rTx_IQK_Tone_A, bMaskDWord, 0x14008c22);
			PHY_SetBBReg(pAdapter, rRx_IQK_Tone_A, bMaskDWord, 0x14008c22);	
		}
		PHY_SetBBReg(pAdapter, rTx_IQK_PI_A, bMaskDWord, 0x82110000);
		if(pHalData->interfaceIndex == 0)
			PHY_SetBBReg(pAdapter, rRx_IQK_PI_A, bMaskDWord, (pHalData->CurrentChannel<=140)?0x68160c62:0x68160c66);
		else
			PHY_SetBBReg(pAdapter, rRx_IQK_PI_A, bMaskDWord, 0x68160962);

		//path-B IQK setting
		if(is2T)
		{
			PHY_SetBBReg(pAdapter, rTx_IQK_Tone_B, bMaskDWord, 0x14008c22);
			PHY_SetBBReg(pAdapter, rRx_IQK_Tone_B, bMaskDWord, 0x14008c22);
			PHY_SetBBReg(pAdapter, rTx_IQK_PI_B, bMaskDWord, 0x82110000);
			PHY_SetBBReg(pAdapter, rRx_IQK_PI_B, bMaskDWord, 0x68110000);
		}

		//load TX0 IMR setting
		PHY_SetBBReg(pAdapter, rTx_IQK, bMaskDWord, RX0REG0xe40);
		//Sleep(5) -> delay 1ms
		rtw_udelay_os(IQK_DELAY_TIME*1000);

		//LO calibration setting
		PHY_SetBBReg(pAdapter, rIQK_AGC_Rsp, bMaskDWord, 0x0046a911);

		//One shot, path A LOK & IQK
		PHY_SetBBReg(pAdapter, rIQK_AGC_Pts, bMaskDWord, 0xf9000000);
		PHY_SetBBReg(pAdapter, rIQK_AGC_Pts, bMaskDWord, 0xf8000000);

		//Delay 3 ms
		rtw_udelay_os(3*IQK_DELAY_TIME*1000);

		//Exit IQK mode
		PHY_SetBBReg(pAdapter, rFPGA0_IQK, bMaskDWord, 0x00000000);

		//Check_RX_IQK_A_result()
		REG0xeac = PHY_QueryBBReg(pAdapter, rRx_Power_After_IQK_A_2, bMaskDWord);
		REG0xea4 = PHY_QueryBBReg(pAdapter, rRx_Power_Before_IQK_A_2, bMaskDWord);
		if(pHalData->interfaceIndex == 0)
		{
			if(((REG0xeac&BIT(27)) == 0) && (((REG0xea4&0x3FF0000)>>16)!=0x132))
			{
				RX_X0 =  (PHY_QueryBBReg(pAdapter, rRx_Power_Before_IQK_A_2, bMaskDWord)&0x3FF0000)>>16;
				RX_Y0 =  (PHY_QueryBBReg(pAdapter, rRx_Power_After_IQK_A_2, bMaskDWord)&0x3FF0000)>>16;
				result[0][2] = RX_X0;
				result[0][3] = RX_Y0;
			}
		}
		else
		{
			if(((REG0xeac&BIT(30)) == 0) && (((REG0xea4&0x3FF0000)>>16)!=0x132))
			{
				RX_X0 =  (PHY_QueryBBReg(pAdapter, rRx_Power_Before_IQK_A_2, bMaskDWord)&0x3FF0000)>>16;
				RX_Y0 =  (PHY_QueryBBReg(pAdapter, rRx_Power_After_IQK_A_2, bMaskDWord)&0x3FF0000)>>16;
				result[0][2] = RX_X0;
				result[0][3] = RX_Y0;
			}		
		}
	}

	if(!is2T)
		goto Exit_IQK;

	//1 path B TX IQK
	//Path-B AFE all on

	DBG_8192C("IQK for 5G: Path B Tx  START interface %u\n", pHalData->interfaceIndex);
	
 	phy_PathADDAOn(pAdapter, ADDA_REG, _FALSE, _TRUE);

	//TXIQK RF setting
	PHY_SetBBReg(pAdapter, rFPGA0_XA_LSSIParameter, bMaskDWord, 0x01940000);
	PHY_SetBBReg(pAdapter, rFPGA0_XB_LSSIParameter, bMaskDWord, 0x01940000);

	//IQK global setting
	PHY_SetBBReg(pAdapter, rFPGA0_IQK, bMaskDWord, 0x80800000);
	PHY_SetBBReg(pAdapter, rTx_IQK, bMaskDWord, 0x10007c00);	
	
	//path-A IQK setting
	PHY_SetBBReg(pAdapter, rTx_IQK_Tone_A, bMaskDWord, 0x14008c1f);
	PHY_SetBBReg(pAdapter, rRx_IQK_Tone_A, bMaskDWord, 0x30008c1f);
	PHY_SetBBReg(pAdapter, rTx_IQK_PI_A, bMaskDWord, 0x82110000);
	PHY_SetBBReg(pAdapter, rRx_IQK_PI_A, bMaskDWord, 0x68110000);
	
	//path-B IQK setting
	PHY_SetBBReg(pAdapter, rTx_IQK_Tone_B, bMaskDWord, 0x14008c22);
	PHY_SetBBReg(pAdapter, rRx_IQK_Tone_B, bMaskDWord, 0x30008c22);
	PHY_SetBBReg(pAdapter, rTx_IQK_PI_B, bMaskDWord, 0x82140386);
	PHY_SetBBReg(pAdapter, rRx_IQK_PI_B, bMaskDWord, 0x68110000);
	
	//LO calibration setting
	PHY_SetBBReg(pAdapter, rIQK_AGC_Rsp, bMaskDWord, 0x00462911);
	
	//One shot, path A LOK & IQK
	PHY_SetBBReg(pAdapter, rIQK_AGC_Cont, bMaskDWord, 0x00000002);
	PHY_SetBBReg(pAdapter, rIQK_AGC_Cont, bMaskDWord, 0x00000000);

	//Delay 1 ms
	rtw_udelay_os(IQK_DELAY_TIME*1000);

	//Exit IQK mode
	PHY_SetBBReg(pAdapter, rFPGA0_IQK, bMaskDWord, 0x00000000);

	// Check_TX_IQK_B_result()
	REG0xe40 = PHY_QueryBBReg(pAdapter, rTx_IQK, bMaskDWord);
	REG0xeac = PHY_QueryBBReg(pAdapter, rRx_Power_After_IQK_A_2, bMaskDWord);
	REG0xeb4 = PHY_QueryBBReg(pAdapter, rTx_Power_Before_IQK_B, bMaskDWord);
	if(((REG0xeac&BIT(31)) == 0) && ((REG0xeb4&0x3FF0000)!=0x142))
	{
		TX_X1 = (PHY_QueryBBReg(pAdapter, rTx_Power_Before_IQK_B, bMaskDWord)&0x3FF0000)>>16;
		TX_Y1 = (PHY_QueryBBReg(pAdapter, rTx_Power_After_IQK_B, bMaskDWord)&0x3FF0000)>>16;
		RX1REG0xe40 = 0x80000000 | (REG0xe40 & 0xfc00fc00) | (TX_X1<<16) | TX_Y1;
		result[0][4] = TX_X1;
		result[0][5] = TX_Y1;
		TX1IQKOK = _TRUE;
	}

	//1 path B RX IQK
	if(TX1IQKOK == _TRUE)
	{

		DBG_8192C("IQK for 5G: Path B Rx  START interface %u\n", pHalData->interfaceIndex);

		if(pHalData->CurrentChannel<=140)
		{
			REG0xe5c = 0x68160960;
			extPAon = 0x1;
		}	
		else
		{
			REG0xe5c = 0x68150c66;
	   		extPAon = 0x0;
		}

		//TXIQK RF setting
		PHY_SetBBReg(pAdapter, rFPGA0_XA_LSSIParameter, bMaskDWord, 0x01900000);
		PHY_SetBBReg(pAdapter, rFPGA0_XB_LSSIParameter, bMaskDWord, 0x01900000);

		//turn on external PA
		PHY_SetBBReg(pAdapter, rFPGA0_XAB_RFParameter, BIT(30), extPAon);

		//BB setting
		PHY_SetBBReg(pAdapter, rFPGA0_AnalogParameter4, bMaskDWord, 0xcc300080);

		//IQK global setting
		PHY_SetBBReg(pAdapter, rFPGA0_IQK, bMaskDWord, 0x80800000);
		
		//path-A IQK setting
		PHY_SetBBReg(pAdapter, rTx_IQK_Tone_A, bMaskDWord, 0x14008c1f);
		PHY_SetBBReg(pAdapter, rRx_IQK_Tone_A, bMaskDWord, 0x34008c1f);
		PHY_SetBBReg(pAdapter, rTx_IQK_PI_A, bMaskDWord, 0x82110000);
		PHY_SetBBReg(pAdapter, rRx_IQK_PI_A, bMaskDWord, 0x68110000 );
		
		//path-B IQK setting
		PHY_SetBBReg(pAdapter, rTx_IQK_Tone_B, bMaskDWord, 0x14008c22);
		PHY_SetBBReg(pAdapter, rRx_IQK_Tone_B, bMaskDWord, 0x14008c22);
		PHY_SetBBReg(pAdapter, rTx_IQK_PI_B, bMaskDWord, 0x82110000);
		PHY_SetBBReg(pAdapter, rRx_IQK_PI_B, bMaskDWord, REG0xe5c);
		
		//load TX0 IMR setting
		PHY_SetBBReg(pAdapter, rTx_IQK, bMaskDWord, RX1REG0xe40);

		//Sleep(5) -> delay 1ms
		rtw_udelay_os(IQK_DELAY_TIME*1000);

		//LO calibration setting
		PHY_SetBBReg(pAdapter, rIQK_AGC_Rsp, bMaskDWord, 0x0046a911);

		//One shot, path A LOK & IQK
		PHY_SetBBReg(pAdapter, rIQK_AGC_Cont, bMaskDWord, 0x00000002);
		PHY_SetBBReg(pAdapter, rIQK_AGC_Cont, bMaskDWord, 0x00000000);

		//Delay 1 ms
		rtw_udelay_os(3*IQK_DELAY_TIME*1000);

		//Check_RX_IQK_B_result()
		REG0xeac = PHY_QueryBBReg(pAdapter, rRx_Power_After_IQK_A_2, bMaskDWord);
		REG0xec4 = PHY_QueryBBReg(pAdapter, rRx_Power_Before_IQK_B_2, bMaskDWord);
		if(((REG0xeac&BIT(30)) == 0) && (((REG0xec4&0x3FF0000)>>16)!=0x132))
		{
			RX_X1 =  (PHY_QueryBBReg(pAdapter, rRx_Power_Before_IQK_B_2, bMaskDWord)&0x3FF0000)>>16;
			RX_Y1 =  (PHY_QueryBBReg(pAdapter, rRx_Power_After_IQK_B_2, bMaskDWord)&0x3FF0000)>>16;
			result[0][6] = RX_X1;
			result[0][7] = RX_Y1;
		}
	}

Exit_IQK:
	//turn off external PA
	if(pHalData->interfaceIndex == 1 || is2T)
		PHY_SetBBReg(pAdapter, rFPGA0_XAB_RFParameter, BIT(30), 0);

	//Exit IQK mode
	PHY_SetBBReg(pAdapter, rFPGA0_IQK, bMaskDWord, 0x00000000);
	phy_ReloadADDARegisters(pAdapter, IQK_BB_REG, pdmpriv->IQK_BB_backup, IQK_BB_REG_NUM_test);
	
	PHY_SetBBReg(pAdapter, rFPGA0_XA_LSSIParameter, bMaskDWord, 0x01900000);
	PHY_SetBBReg(pAdapter, rFPGA0_XB_LSSIParameter, bMaskDWord, 0x01900000);
	PHY_SetBBReg(pAdapter, rFPGA0_XA_LSSIParameter, bMaskDWord, 0x00032fff);
	PHY_SetBBReg(pAdapter, rFPGA0_XB_LSSIParameter, bMaskDWord, 0x00032fff);


	//reload MAC default value	
	phy_ReloadMACRegisters(pAdapter, IQK_MAC_REG, pdmpriv->IQK_MAC_backup);
	
	if(!pdmpriv->bRfPiEnable)
		phy_PIModeSwitch(pAdapter, _FALSE);
	//Reload ADDA power saving parameters
	phy_ReloadADDARegisters(pAdapter, ADDA_REG, pdmpriv->ADDA_backup, IQK_ADDA_REG_NUM);
	
}

static VOID	
phy_IQCalibrate_5G_Normal(
	IN	PADAPTER	pAdapter,
	IN	int 		result[][8],
	IN	u8		t
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	u32			PathAOK, PathBOK;
	u32			ADDA_REG[IQK_ADDA_REG_NUM] = {	
						rFPGA0_XCD_SwitchControl, 	rBlue_Tooth, 	
						rRx_Wait_CCA, 		rTx_CCK_RFON,
						rTx_CCK_BBON, 	rTx_OFDM_RFON, 	
						rTx_OFDM_BBON, 	rTx_To_Rx,
						rTx_To_Tx, 		rRx_CCK, 	
						rRx_OFDM, 		rRx_Wait_RIFS,
						rRx_TO_Rx, 		rStandby, 	
						rSleep, 			rPMPD_ANAEN };
	u32			IQK_MAC_REG[IQK_MAC_REG_NUM] = {
						REG_TXPAUSE, 		REG_BCN_CTRL,	
						REG_BCN_CTRL_1,	REG_GPIO_MUXCFG};

	u32			IQK_BB_REG[IQK_BB_REG_NUM] = {	//for normal
						rFPGA0_XAB_RFInterfaceSW,	rFPGA0_XA_RFInterfaceOE,	
						rFPGA0_XB_RFInterfaceOE,	rOFDM0_TRMuxPar,
						rFPGA0_XCD_RFInterfaceSW,	rOFDM0_TRxPathEnable,	
						rFPGA0_RFMOD,			rFPGA0_AnalogParameter4,
						rOFDM0_XAAGCCore1,		rOFDM0_XBAGCCore1
					};

	// Note: IQ calibration must be performed after loading 
	// 		PHY_REG.txt , and radio_a, radio_b.txt	

	u32	bbvalue;
	BOOLEAN		is2T =  IS_92D_SINGLEPHY(pHalData->VersionID);

	//RTPRINT(FINIT, INIT_IQK, ("IQK for 5G NORMAL:Start!!! interface %d\n", pAdapter->interfaceIndex));

	//rtw_udelay_os(IQK_DELAY_TIME*1000*100);	//delay after set IMR
	
	//rtw_udelay_os(IQK_DELAY_TIME*1000*20);
	rtw_mdelay_os(IQK_DELAY_TIME*20);

	if(t==0)
	{
		bbvalue = PHY_QueryBBReg(pAdapter, rFPGA0_RFMOD, bMaskDWord);
		//RTPRINT(FINIT, INIT_IQK, ("PHY_IQCalibrate()==>0x%08lx\n",bbvalue));
		//RTPRINT(FINIT, INIT_IQK, ("IQ Calibration for %s\n", (is2T ? "2T2R" : "1T1R")));

	 	// Save ADDA parameters, turn Path A ADDA on
		phy_SaveADDARegisters(pAdapter, ADDA_REG, pdmpriv->ADDA_backup, IQK_ADDA_REG_NUM);
		phy_SaveMACRegisters(pAdapter, IQK_MAC_REG, pdmpriv->IQK_MAC_backup);
		if(is2T)
			phy_SaveADDARegisters(pAdapter, IQK_BB_REG, pdmpriv->IQK_BB_backup, IQK_BB_REG_NUM_92D);		
		else
			phy_SaveADDARegisters(pAdapter, IQK_BB_REG, pdmpriv->IQK_BB_backup, IQK_BB_REG_NUM_92D-1);
	}

	//Path-A AFE all on
 	phy_PathADDAOn(pAdapter, ADDA_REG, _TRUE, is2T);

	//MAC settings
	phy_MACSettingCalibration(pAdapter, IQK_MAC_REG, pdmpriv->IQK_MAC_backup);
		
	if(t==0)
	{
		pdmpriv->bRfPiEnable = (u8)PHY_QueryBBReg(pAdapter, rFPGA0_XA_HSSIParameter1, BIT(8));
	}
	
	if(!pdmpriv->bRfPiEnable){
		// Switch BB to PI mode to do IQ Calibration.
		phy_PIModeSwitch(pAdapter, _TRUE);
	}

	PHY_SetBBReg1Byte(pAdapter, rFPGA0_RFMOD, BIT24, 0x00);
	PHY_SetBBReg(pAdapter, rOFDM0_TRxPathEnable, bMaskDWord, 0x03a05600);
	PHY_SetBBReg(pAdapter, rOFDM0_TRMuxPar, bMaskDWord, 0x000800e4);
	PHY_SetBBReg(pAdapter, rFPGA0_XCD_RFInterfaceSW, bMaskDWord, 0x22208000);
	PHY_SetBBReg(pAdapter, rFPGA0_AnalogParameter4, 0xf00000, 0x0f);

#if 0
	//Page B init
	PHY_SetBBReg(pAdapter, 0xb68, bMaskDWord, 0x0f600000);
	
	if(is2T)
	{
		PHY_SetBBReg(pAdapter, 0xb6c, bMaskDWord, 0x0f600000);
	}
#else	

	//Page A AP setting for IQK
	PHY_SetBBReg(pAdapter, rPdp_AntA, bMaskDWord, 0x00000000);
	PHY_SetBBReg(pAdapter, rConfig_AntA, bMaskDWord, 0x20000000);

	//Page B AP setting for IQK	
	if(is2T)
	{
		PHY_SetBBReg(pAdapter, rPdp_AntB, bMaskDWord, 0x00000000);
		PHY_SetBBReg(pAdapter, rConfig_AntB, bMaskDWord, 0x20000000);
	}
#endif
	// IQ calibration setting
	//RTPRINT(FINIT, INIT_IQK, ("IQK setting!\n"));		
	PHY_SetBBReg(pAdapter, rFPGA0_IQK, bMaskDWord, 0x80800000);
	PHY_SetBBReg(pAdapter, rTx_IQK, bMaskDWord, 0x10007c00);
	PHY_SetBBReg(pAdapter, rRx_IQK, bMaskDWord, 0x01004800);

	{
		PathAOK = phy_PathA_IQK_5G_Normal(pAdapter, is2T);
		if(PathAOK == 0x03){
			DBG_8192C("Path A IQK Success!!\n");
			result[t][0] = (PHY_QueryBBReg(pAdapter, rTx_Power_Before_IQK_A, bMaskDWord)&0x3FF0000)>>16;
			result[t][1] = (PHY_QueryBBReg(pAdapter, rTx_Power_After_IQK_A, bMaskDWord)&0x3FF0000)>>16;
			result[t][2] = (PHY_QueryBBReg(pAdapter, rRx_Power_Before_IQK_A_2, bMaskDWord)&0x3FF0000)>>16;
			result[t][3] = (PHY_QueryBBReg(pAdapter, rRx_Power_After_IQK_A_2, bMaskDWord)&0x3FF0000)>>16;
		}
		else if (PathAOK == 0x01)	//Tx IQK OK
		{
			DBG_8192C("Path A IQK Only  Tx Success!!\n");
			
			result[t][0] = (PHY_QueryBBReg(pAdapter, rTx_Power_Before_IQK_A, bMaskDWord)&0x3FF0000)>>16;
			result[t][1] = (PHY_QueryBBReg(pAdapter, rTx_Power_After_IQK_A, bMaskDWord)&0x3FF0000)>>16;
		}
		else
		{
			PHY_SetBBReg(pAdapter, rFPGA0_IQK, bMaskDWord, 0);
			DBG_871X("0xe70 = 0x%x\n", PHY_QueryBBReg(pAdapter, rRx_Wait_CCA, bMaskDWord));
			DBG_871X("RF path A 0x0 = 0x%x\n", PHY_QueryRFReg(pAdapter, RF_PATH_A, RF_AC, bRFRegOffsetMask));
			PHY_SetBBReg(pAdapter, rFPGA0_IQK, bMaskDWord, 0x80800000);					
			DBG_871X("Path A IQK Fail!!\n");
		}
	}

	if(is2T){
		//_PHY_PathAStandBy(pAdapter);

		// Turn Path B ADDA on
		phy_PathADDAOn(pAdapter, ADDA_REG, _FALSE, is2T);

		{
			PathBOK = phy_PathB_IQK_5G_Normal(pAdapter);
			if(PathBOK == 0x03){
				DBG_8192C("Path B IQK Success!!\n");
				result[t][4] = (PHY_QueryBBReg(pAdapter, rTx_Power_Before_IQK_B, bMaskDWord)&0x3FF0000)>>16;
				result[t][5] = (PHY_QueryBBReg(pAdapter, rTx_Power_After_IQK_B, bMaskDWord)&0x3FF0000)>>16;
				result[t][6] = (PHY_QueryBBReg(pAdapter, rRx_Power_Before_IQK_B_2, bMaskDWord)&0x3FF0000)>>16;
				result[t][7] = (PHY_QueryBBReg(pAdapter, rRx_Power_After_IQK_B_2, bMaskDWord)&0x3FF0000)>>16;
			}
			else if (PathBOK == 0x01)	//Tx IQK OK
			{
				DBG_8192C("Path B Only Tx IQK Success!!\n");
				result[t][4] = (PHY_QueryBBReg(pAdapter, rTx_Power_Before_IQK_B, bMaskDWord)&0x3FF0000)>>16;
				result[t][5] = (PHY_QueryBBReg(pAdapter, rTx_Power_After_IQK_B, bMaskDWord)&0x3FF0000)>>16;
			}
			else{		
				DBG_8192C("Path B IQK failed!!\n");
			}			
		}
	}
	
	//Back to BB mode, load original value
	//RTPRINT(FINIT, INIT_IQK, ("IQK:Back to BB mode, load original value!\n"));
	PHY_SetBBReg(pAdapter, rFPGA0_IQK, bMaskDWord, 0);

	if(t!=0)
	{
		if(is2T)			
			phy_ReloadADDARegisters(pAdapter, IQK_BB_REG, pdmpriv->IQK_BB_backup, IQK_BB_REG_NUM_92D);
		else
			phy_ReloadADDARegisters(pAdapter, IQK_BB_REG, pdmpriv->IQK_BB_backup, IQK_BB_REG_NUM_92D-1);

#if 1
		//path A IQ path to DP block
		PHY_SetBBReg(pAdapter, rPdp_AntA, bMaskDWord, 0x010170b8);

		//path B IQ path to DP block
		if(is2T)
			PHY_SetBBReg(pAdapter, rPdp_AntB, bMaskDWord, 0x010170b8);
#endif

		// Reload MAC parameters
		phy_ReloadMACRegisters(pAdapter, IQK_MAC_REG, pdmpriv->IQK_MAC_backup);		
		
		if(!pdmpriv->bRfPiEnable){
			// Switch back BB to SI mode after finish IQ Calibration.
			phy_PIModeSwitch(pAdapter, _FALSE);
		}

	 	// Reload ADDA power saving parameters
	 	phy_ReloadADDARegisters(pAdapter, ADDA_REG, pdmpriv->ADDA_backup, IQK_ADDA_REG_NUM);

		//load 0xe30 IQC default value
		//PHY_SetBBReg(pAdapter, 0xe30, bMaskDWord, 0x01008c00);
		//PHY_SetBBReg(pAdapter, 0xe34, bMaskDWord, 0x01008c00);

	}
	//RTPRINT(FINIT, INIT_IQK, ("_PHY_IQCalibrate_5G_Normal() <==\n"));
	
}

#if SWLCK != 1
static VOID	
phy_LCCalibrate92D(
	IN	PADAPTER	pAdapter,
	IN	BOOLEAN		is2T
	)
{
	u8	tmpReg, index = 0;
	u32 	RF_mode[2], tmpu4Byte[2];
	u8	path = is2T?2:1;
#if SWLCK == 1
	u16	timeout = 800, timecount = 0;
#endif

	//Check continuous TX and Packet TX
	tmpReg = rtw_read8(pAdapter, 0xd03);

	if((tmpReg&0x70) != 0)			//Deal with contisuous TX case
		rtw_write8(pAdapter, 0xd03, tmpReg&0x8F);	//disable all continuous TX
	else 							// Deal with Packet TX case
		rtw_write8(pAdapter, REG_TXPAUSE, 0xFF);			// block all queues

	PHY_SetBBReg(pAdapter, rFPGA0_AnalogParameter4, 0xF00000, 0x0F);

	for(index = 0; index <path; index ++)
	{
		//1. Read original RF mode
		RF_mode[index] = PHY_QueryRFReg(pAdapter, (RF_RADIO_PATH_E)index, RF_AC, bRFRegOffsetMask);
		
		//2. Set RF mode = standby mode
		PHY_SetRFReg(pAdapter, (RF_RADIO_PATH_E)index, RF_AC, 0x70000, 0x01);

		tmpu4Byte[index] = PHY_QueryRFReg(pAdapter, (RF_RADIO_PATH_E)index, RF_SYN_G4, bRFRegOffsetMask);
		PHY_SetRFReg(pAdapter, (RF_RADIO_PATH_E)index, RF_SYN_G4, 0x700, 0x07);

		//4. Set LC calibration begin
		PHY_SetRFReg(pAdapter, (RF_RADIO_PATH_E)index, RF_CHNLBW, 0x08000, 0x01);
		
	}
		
#if SWLCK == 1
	for(index = 0; index <path; index ++)
	{
		while(!(PHY_QueryRFReg(pAdapter, (RF_RADIO_PATH_E)index, RF_SYN_G6, BIT11)) &&
			timecount <= timeout)
		{
			
			//RTPRINT(FINIT, INIT_IQK,("PHY_LCK delay for %d ms=2\n", timecount));
			#ifdef CONFIG_LONG_DELAY_ISSUE
			rtw_msleep_os(50);
			#else
			rtw_mdelay_os(50);		
			#endif
			timecount += 50;
		}
	}
#else
	#ifdef CONFIG_LONG_DELAY_ISSUE
	rtw_msleep_os(100);
	#else
	rtw_mdelay_os(100);		
	#endif
#endif

	//Restore original situation	
	for(index = 0; index <path; index ++)
	{
		PHY_SetRFReg(pAdapter, (RF_RADIO_PATH_E)index, RF_SYN_G4, bRFRegOffsetMask, tmpu4Byte[index]);
		PHY_SetRFReg(pAdapter, (RF_RADIO_PATH_E)index, RF_AC, bRFRegOffsetMask, RF_mode[index]);
	}

	if((tmpReg&0x70) != 0)	
	{  
		//Path-A
		rtw_write8(pAdapter, 0xd03, tmpReg);
	}
	else // Deal with Packet TX case
	{
		rtw_write8(pAdapter, REG_TXPAUSE, 0x00);	
	}

	PHY_SetBBReg(pAdapter, rFPGA0_AnalogParameter4, 0xF00000, 0x00);
	
}
#endif  //SWLCK != 1, amy, temp remove
static u32
get_abs(
	IN	u32	val1,
	IN	u32	val2
	)
{
	u32 ret=0;
	
	if(val1 >= val2)
	{
		ret = val1 - val2;
	}
	else
	{
		ret = val2 - val1;
	}
	return ret;
}

#define	TESTFLAG			0
#define	BASE_CHNL_NUM		6
#define	BASE_CHNL_NUM_2G	2

static VOID
phy_CalcCurvIndex(
	IN	PADAPTER	pAdapter,
	IN	u32*		TargetChnl,
	IN	u32*		CurveCountVal,	
	IN	BOOLEAN		is5G,				
	OUT	u32*		CurveIndex
	)
{
	u32	smallestABSVal = 0xffffffff, u4tmp;
	u8	i, channel, pre_channel, start = is5G?TARGET_CHNL_NUM_2G:0, 
		start_base = is5G?BASE_CHNL_NUM_2G:0, 
		end_base = is5G?BASE_CHNL_NUM:BASE_CHNL_NUM_2G;
	u8	chnl_num = is5G?TARGET_CHNL_NUM_2G_5G:TARGET_CHNL_NUM_2G;
	u8 	Base_chnl[BASE_CHNL_NUM] = {1, 14, 36, 100, 149};
	u32	j, base_index = 0, search_bound;
	BOOLEAN	bBase = _FALSE;
	
	for(i=start; i<chnl_num; i++)
	{
		if(is5G)
		{
			if(i != start)
				pre_channel = channel;
			channel = GetChnlFromPlace(i);	//actual channel number

			if(i == start)
				pre_channel = channel;
		}
		else
		{
			if(i != start)		
				pre_channel = channel;		
			channel = i+1;

			if(i == start)
				pre_channel = channel;			
		}

#if 1
		bBase = _FALSE;

		for(j = start_base; j < end_base; j++)
		{
			if(channel == Base_chnl[j])
			{
				bBase = _TRUE;
				base_index = 0;
				search_bound = (CV_CURVE_CNT*2);	//search every 128
				break;
			}
			else if(channel < Base_chnl[j] || j == end_base-1)
			{
#if 1
				base_index = CurveIndex[GetRightChnlPlace(pre_channel)-1];
#else
				if(j > start_base && channel < Base_chnl[j])
					base_index = CurveIndex[GetRightChnlPlace(Base_chnl[j-1])-1];
				else
					base_index = CurveIndex[GetRightChnlPlace(Base_chnl[j])-1];
#endif

				if(base_index > 5)
					base_index -= 5;	//search -5~5, not every 128
				else
					base_index = 0;
				search_bound = base_index+10;
				break;
			}
		}
#endif

		CurveIndex[i] = 0;
#if 1
		for(j=base_index; j<base_index+search_bound; j++)
		{
			u4tmp = get_abs(TargetChnl[channel-1], CurveCountVal[j]);

			if(u4tmp < smallestABSVal)
			{
				CurveIndex[i] = j;
				smallestABSVal = u4tmp;
			}
		}
#endif

		smallestABSVal = 0xffffffff;
		//if(i == 0)
		//	RTPRINT(FINIT, INIT_IQK,("cosa, CurveIndex[%d] = %d channel %d\n", i, CurveIndex[i], index));
		//RTPRINT(FINIT, INIT_IQK,("CurveIndex[%d] = 0x%x channel %d base_index 0x%x\n", i, CurveIndex[i], channel, base_index));
	}
}

static VOID	
phy_LCCalibrate92DSW(
	IN	PADAPTER	pAdapter,
	IN	BOOLEAN		is2T
	)
{
	u8	RF_mode[2], tmpReg, index = 0;
#if (TESTFLAG == 0)
	u32	tmpu4Byte[2];
#endif //(TESTFLAG == 0)
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	u8	u1bTmp=0,path = is2T?2:1;
	u32	i, u4tmp, offset;
	u32	curveCountVal[CV_CURVE_CNT*2]={0};
	u16	timeout = 800, timecount = 0;

	//Check continuous TX and Packet TX
	tmpReg = rtw_read8(pAdapter, 0xd03);

	if((tmpReg&0x70) != 0)			//Deal with contisuous TX case
		rtw_write8(pAdapter, 0xd03, tmpReg&0x8F);	//disable all continuous TX
	else 							// Deal with Packet TX case
		rtw_write8(pAdapter, REG_TXPAUSE, 0xFF);			// block all queues

	PHY_SetBBReg(pAdapter, rFPGA0_AnalogParameter4, 0xF00000, 0x0F);

	for(index = 0; index <path; index ++)
	{
		//RTPRINT(FINIT, INIT_IQK,("PHY_LCK enter for loop for index %d \n", index));
	
		//1. Read original RF mode
		offset = index == 0?rOFDM0_XAAGCCore1:rOFDM0_XBAGCCore1;
		RF_mode[index] = rtw_read8(pAdapter, offset);
		//RTPRINT(FINIT, INIT_IQK,("PHY_LCK offset 0x%x tmpreg = 0x%x\n", offset, RF_mode[index]));
		
		//2. Set RF mode = standby mode
		PHY_SetRFReg(pAdapter, (RF_RADIO_PATH_E)index, RF_AC, bRFRegOffsetMask, 0x010000);
#if (TESTFLAG == 0)
		tmpu4Byte[index] = PHY_QueryRFReg(pAdapter, (RF_RADIO_PATH_E)index, RF_SYN_G4, bRFRegOffsetMask);
		PHY_SetRFReg(pAdapter, (RF_RADIO_PATH_E)index, RF_SYN_G4, 0x700, 0x07);
#endif

		if(pAdapter->hw_init_completed)
		{
			// switch CV-curve control by LC-calibration
			PHY_SetRFReg(pAdapter, (RF_RADIO_PATH_E)index, RF_SYN_G7, BIT17, 0x0);

			//4. Set LC calibration begin
			PHY_SetRFReg(pAdapter, (RF_RADIO_PATH_E)index, RF_CHNLBW, 0x08000, 0x01);

			//RTPRINT(FINIT, INIT_IQK,("PHY_LCK finish HWLCK \n"));
		}
	}

	for(index = 0; index <path; index ++)
	{
		u4tmp = PHY_QueryRFReg(pAdapter, (RF_RADIO_PATH_E)index, RF_SYN_G6, bRFRegOffsetMask);

		while((!(u4tmp & BIT11)) &&
			timecount <= timeout)
		{
				#ifdef CONFIG_LONG_DELAY_ISSUE
				rtw_msleep_os(50);
				#else
				rtw_mdelay_os(50);
				#endif
			timecount += 50;
			u4tmp = PHY_QueryRFReg(pAdapter, (RF_RADIO_PATH_E)index, RF_SYN_G6, bRFRegOffsetMask);
		}	
		//RTPRINT(FINIT, INIT_IQK,("PHY_LCK finish delay for %d ms=2\n", timecount));
	}
	//Disable TX only need during phy lck, To reduce LCK affect on chariot, 
	// move enable tx here after PHY LCK finish, it will not affect sw lck result.  
	// zhiyuan 2011/06/03
	if((tmpReg&0x70) != 0)	
	{  
		//Path-A
		rtw_write8(pAdapter, 0xd03, tmpReg);
	}
	else // Deal with Packet TX case
	{
		rtw_write8(pAdapter, REG_TXPAUSE, 0x00);	
	}
	PHY_SetBBReg(pAdapter, rFPGA0_AnalogParameter4, 0xF00000, 0x00);

	for(index = 0; index <path; index ++)
	{
		u4tmp = PHY_QueryRFReg(pAdapter, (RF_RADIO_PATH_E)index, RF_SYN_G4, bRFRegOffsetMask);

		{

			//if(index == 0 && pHalData->interfaceIndex == 0)
			//{
			//	RTPRINT(FINIT, INIT_IQK,("cosa, path-A / 5G LCK\n"));
			//}
			//else
			//{
			//	RTPRINT(FINIT, INIT_IQK,("cosa, path-B / 2.4G LCK\n"));				
			//}
			_rtw_memset(&curveCountVal[0], 0, CV_CURVE_CNT*2);

			//Set LC calibration off
			PHY_SetRFReg(pAdapter, (RF_RADIO_PATH_E)index, RF_CHNLBW, 0x08000, 0x0);
			//RTPRINT(FINIT, INIT_IQK,("cosa, set RF 0x18[15] = 0\n"));

			//save Curve-counting number
			for(i=0; i<CV_CURVE_CNT; i++)
			{
				u32	readVal=0, readVal2=0;
				PHY_SetRFReg(pAdapter, (RF_RADIO_PATH_E)index, RF_TRSW, 0x7f, i);

				PHY_SetRFReg(pAdapter, (RF_RADIO_PATH_E)index, 0x4D, bRFRegOffsetMask, 0x0);
				readVal = PHY_QueryRFReg(pAdapter, (RF_RADIO_PATH_E)index, 0x4F, bRFRegOffsetMask);

				curveCountVal[2*i+1] = (readVal & 0xfffe0) >> 5;
				// reg 0x4f [4:0]
				// reg 0x50 [19:10]
				readVal2 = PHY_QueryRFReg(pAdapter, (RF_RADIO_PATH_E)index, 0x50, 0xffc00);
				curveCountVal[2*i] = (((readVal & 0x1F) << 10) | readVal2);

			}

			if(index == 0 && pHalData->interfaceIndex == 0)
				phy_CalcCurvIndex(pAdapter, TargetChnl_5G, curveCountVal, _TRUE, CurveIndex);
			else 
				phy_CalcCurvIndex(pAdapter, TargetChnl_2G, curveCountVal, _FALSE, CurveIndex);

			// switch CV-curve control mode
			PHY_SetRFReg(pAdapter, (RF_RADIO_PATH_E)index, RF_SYN_G7, BIT17, 0x1);
		}

	}

	//Restore original situation	
	for(index = 0; index <path; index ++)
	{
#if (TESTFLAG == 0)
		PHY_SetRFReg(pAdapter, (RF_RADIO_PATH_E)index, RF_SYN_G4, bRFRegOffsetMask, tmpu4Byte[index]);
#endif
		offset = index == 0?rOFDM0_XAAGCCore1:rOFDM0_XBAGCCore1;
		//RTPRINT(FINIT, INIT_IQK,("offset 0x%x tmpreg = 0x%x index %d\n", offset, RF_mode[index], index));
		rtw_write8(pAdapter, offset, 0x50);
		rtw_write8(pAdapter, offset, RF_mode[index]);
	}

	phy_ReloadLCKSetting(pAdapter, pHalData->CurrentChannel);
	
}


static VOID	
phy_LCCalibrate(
	IN	PADAPTER	pAdapter,
	IN	BOOLEAN		is2T
	)
{
#if SWLCK == 1
	//DBG_8192C("cosa PHY_LCK ver=2\n");
	phy_LCCalibrate92DSW(pAdapter, is2T);
#else
	phy_LCCalibrate92D(pAdapter, is2T);
#endif
}


//Analog Pre-distortion calibration
#define		APK_BB_REG_NUM	8
#define		APK_CURVE_REG_NUM 4
#define		PATH_NUM		2

static VOID
phy_APCalibrate(
	IN	PADAPTER	pAdapter,
	IN	char 		delta,
	IN	BOOLEAN		is2T
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	u32 			regD[PATH_NUM];
	u32			tmpReg, index, offset, path, i, pathbound = PATH_NUM, apkbound;

	u32			BB_backup[APK_BB_REG_NUM];
	u32			BB_REG[APK_BB_REG_NUM] = {	
						rFPGA1_TxBlock, 	rOFDM0_TRxPathEnable, 
						rFPGA0_RFMOD, 	rOFDM0_TRMuxPar, 
						rFPGA0_XCD_RFInterfaceSW,	rFPGA0_XAB_RFInterfaceSW, 
						rFPGA0_XA_RFInterfaceOE, 	rFPGA0_XB_RFInterfaceOE	};
	u32			BB_AP_MODE[APK_BB_REG_NUM] = {	
						0x00000020, 0x00a05430, 0x02040000, 
						0x000800e4, 0x00204000 };
	u32			BB_normal_AP_MODE[APK_BB_REG_NUM] = {	
						0x00000020, 0x00a05430, 0x02040000, 
						0x000800e4, 0x22204000 };

	u32			AFE_backup[IQK_ADDA_REG_NUM];
	u32			AFE_REG[IQK_ADDA_REG_NUM] = {	
						rFPGA0_XCD_SwitchControl, 	rBlue_Tooth, 	
						rRx_Wait_CCA, 		rTx_CCK_RFON,
						rTx_CCK_BBON, 	rTx_OFDM_RFON, 	
						rTx_OFDM_BBON, 	rTx_To_Rx,
						rTx_To_Tx, 		rRx_CCK, 	
						rRx_OFDM, 		rRx_Wait_RIFS,
						rRx_TO_Rx, 		rStandby, 	
						rSleep, 			rPMPD_ANAEN };

	u32			MAC_backup[IQK_MAC_REG_NUM];
	u32			MAC_REG[IQK_MAC_REG_NUM] = {
						REG_TXPAUSE, 		REG_BCN_CTRL,	
						REG_BCN_CTRL_1,	REG_GPIO_MUXCFG};

	u32			APK_RF_init_value[PATH_NUM][APK_BB_REG_NUM] = {
					{0x0852c, 0x1852c, 0x5852c, 0x1852c, 0x5852c},
					{0x2852e, 0x0852e, 0x3852e, 0x0852e, 0x0852e}
					};

	u32			APK_normal_RF_init_value[PATH_NUM][APK_BB_REG_NUM] = {
					{0x0852c, 0x0a52c, 0x3a52c, 0x5a52c, 0x5a52c},	//path settings equal to path b settings
					{0x0852c, 0x0a52c, 0x5a52c, 0x5a52c, 0x5a52c}
					};

	u32			APK_RF_value_0[PATH_NUM][APK_BB_REG_NUM] = {
					{0x52019, 0x52014, 0x52013, 0x5200f, 0x5208d},
					{0x5201a, 0x52019, 0x52016, 0x52033, 0x52050}
					};

	u32			APK_normal_RF_value_0[PATH_NUM][APK_BB_REG_NUM] = {
					{0x52019, 0x52017, 0x52010, 0x5200d, 0x5206a},	//path settings equal to path b settings
					{0x52019, 0x52017, 0x52010, 0x5200d, 0x5206a}
					};
#if 0
	u32			APK_RF_value_A[PATH_NUM][APK_BB_REG_NUM] = {
					{0x1adb0, 0x1adb0, 0x1ada0, 0x1ad90, 0x1ad80},
					{0x00fb0, 0x00fb0, 0x00fa0, 0x00f90, 0x00f80}
					};
#endif
	u32			AFE_on_off[PATH_NUM] = {
					0x04db25a4, 0x0b1b25a4};	//path A on path B off / path A off path B on

	u32			APK_offset[PATH_NUM] = {
					rConfig_AntA, rConfig_AntB};

	u32			APK_normal_offset[PATH_NUM] = {
					rConfig_Pmpd_AntA, rConfig_Pmpd_AntB};
					
	u32			APK_value[PATH_NUM] = {
					0x92fc0000, 0x12fc0000};

	u32			APK_normal_value[PATH_NUM] = {
					0x92680000, 0x12680000};

	char			APK_delta_mapping[APK_BB_REG_NUM][13] = {
					{-4, -3, -2, -2, -1, -1, 0, 1, 2, 3, 4, 5, 6},
					{-4, -3, -2, -2, -1, -1, 0, 1, 2, 3, 4, 5, 6},											
					{-6, -4, -2, -2, -1, -1, 0, 1, 2, 3, 4, 5, 6},
					{-1, -1, -1, -1, -1, -1, 0, 1, 2, 3, 4, 5, 6},
					{-11, -9, -7, -5, -3, -1, 0, 0, 0, 0, 0, 0, 0}
					};
	
	u32			APK_normal_setting_value_1[13] = {
					0x01017018, 0xf7ed8f84, 0x1b1a1816, 0x2522201e, 0x322e2b28,
					0x433f3a36, 0x5b544e49, 0x7b726a62, 0xa69a8f84, 0xdfcfc0b3,
					0x12680000, 0x00880000, 0x00880000
					};

	u32			APK_normal_setting_value_2[16] = {
					0x01c7021d, 0x01670183, 0x01000123, 0x00bf00e2, 0x008d00a3,
					0x0068007b, 0x004d0059, 0x003a0042, 0x002b0031, 0x001f0025,
					0x0017001b, 0x00110014, 0x000c000f, 0x0009000b, 0x00070008,
					0x00050006
					};

	u32			APK_result[PATH_NUM][APK_BB_REG_NUM];	//val_1_1a, val_1_2a, val_2a, val_3a, val_4a
	//u32			AP_curve[PATH_NUM][APK_CURVE_REG_NUM];

	int			BB_offset, delta_V, delta_offset;

#if (MP_DRIVER == 1)
	PMPT_CONTEXT	pMptCtx = &pAdapter->mppriv.MptCtx;

	pMptCtx->APK_bound[0] = 45;
	pMptCtx->APK_bound[1] = 52;		
#endif

	//RTPRINT(FINIT, INIT_IQK, ("==>PHY_APCalibrate() delta %d\n", delta));
	//RTPRINT(FINIT, INIT_IQK, ("AP Calibration for %s\n", (is2T ? "2T2R" : "1T1R")));

	if(!is2T)
		pathbound = 1;

	//2 FOR NORMAL CHIP SETTINGS

// Temporarily do not allow normal driver to do the following settings because these offset
// and value will cause RF internal PA to be unpredictably disabled by HW, such that RF Tx signal
// will disappear after disable/enable card many times on 88CU. RF SD and DD have not find the
// root cause, so we remove these actions temporarily. Added by tynli and SD3 Allen. 2010.05.31.
#if MP_DRIVER != 1
	return;
#endif

	//settings adjust for normal chip
	for(index = 0; index < PATH_NUM; index ++)
	{
		APK_offset[index] = APK_normal_offset[index];
		APK_value[index] = APK_normal_value[index];
		AFE_on_off[index] = 0x6fdb25a4;
	}

	for(index = 0; index < APK_BB_REG_NUM; index ++)
	{
		for(path = 0; path < pathbound; path++)
		{
			APK_RF_init_value[path][index] = APK_normal_RF_init_value[path][index];
			APK_RF_value_0[path][index] = APK_normal_RF_value_0[path][index];
		}
		BB_AP_MODE[index] = BB_normal_AP_MODE[index];
	}

	apkbound = 6;
	
	//save BB default value
	for(index = 0; index < APK_BB_REG_NUM ; index++)
	{
		if(index == 0)		//skip 
			continue;				
		BB_backup[index] = PHY_QueryBBReg(pAdapter, BB_REG[index], bMaskDWord);
	}
	
	//save MAC default value													
	phy_SaveMACRegisters(pAdapter, MAC_REG, MAC_backup);
	
	//save AFE default value
	phy_SaveADDARegisters(pAdapter, AFE_REG, AFE_backup, IQK_ADDA_REG_NUM);

	for(path = 0; path < pathbound; path++)
	{


		if(path == RF_PATH_A)
		{
			//path A APK
			//load APK setting
			//path-A		
			offset = rPdp_AntA;
			for(index = 0; index < 11; index ++)			
			{
				PHY_SetBBReg(pAdapter, offset, bMaskDWord, APK_normal_setting_value_1[index]);
				//RTPRINT(FINIT, INIT_IQK, ("PHY_APCalibrate() offset 0x%x value 0x%x\n", offset, PHY_QueryBBReg(pAdapter, offset, bMaskDWord))); 	
				
				offset += 0x04;
			}
			
			PHY_SetBBReg(pAdapter, rConfig_Pmpd_AntB, bMaskDWord, 0x12680000);
			
			offset = rConfig_AntA;
			for(; index < 13; index ++) 		
			{
				PHY_SetBBReg(pAdapter, offset, bMaskDWord, APK_normal_setting_value_1[index]);
				//RTPRINT(FINIT, INIT_IQK, ("PHY_APCalibrate() offset 0x%x value 0x%x\n", offset, PHY_QueryBBReg(pAdapter, offset, bMaskDWord))); 	
				
				offset += 0x04;
			}	
			
			//page-B1
			PHY_SetBBReg(pAdapter, rFPGA0_IQK, bMaskDWord, 0x40000000);
		
			//path A
			offset = rPdp_AntA;
			for(index = 0; index < 16; index++)
			{
				PHY_SetBBReg(pAdapter, offset, bMaskDWord, APK_normal_setting_value_2[index]);		
				//RTPRINT(FINIT, INIT_IQK, ("PHY_APCalibrate() offset 0x%x value 0x%x\n", offset, PHY_QueryBBReg(pAdapter, offset, bMaskDWord))); 	
				
				offset += 0x04;
			}				
			PHY_SetBBReg(pAdapter, rFPGA0_IQK, bMaskDWord, 0x00000000);							
		}
		else if(path == RF_PATH_B)
		{
			//path B APK
			//load APK setting
			//path-B		
			offset = rPdp_AntB;
			for(index = 0; index < 10; index ++)			
			{
				PHY_SetBBReg(pAdapter, offset, bMaskDWord, APK_normal_setting_value_1[index]);
				//RTPRINT(FINIT, INIT_IQK, ("PHY_APCalibrate() offset 0x%x value 0x%x\n", offset, PHY_QueryBBReg(pAdapter, offset, bMaskDWord))); 	
				
				offset += 0x04;
			}
			PHY_SetBBReg(pAdapter, rConfig_Pmpd_AntA, bMaskDWord, 0x12680000);
			
			PHY_SetBBReg(pAdapter, rConfig_Pmpd_AntB, bMaskDWord, 0x12680000);
			
			offset = rConfig_AntA;
			index = 11;
			for(; index < 13; index ++) //offset 0xb68, 0xb6c		
			{
				PHY_SetBBReg(pAdapter, offset, bMaskDWord, APK_normal_setting_value_1[index]);
				//RTPRINT(FINIT, INIT_IQK, ("PHY_APCalibrate() offset 0x%x value 0x%x\n", offset, PHY_QueryBBReg(pAdapter, offset, bMaskDWord))); 	
				
				offset += 0x04;
			}	
			
			//page-B1
			PHY_SetBBReg(pAdapter, rFPGA0_IQK, bMaskDWord, 0x40000000);
			
			//path B
			offset = 0xb60;
			for(index = 0; index < 16; index++)
			{
				PHY_SetBBReg(pAdapter, offset, bMaskDWord, APK_normal_setting_value_2[index]);		
				//RTPRINT(FINIT, INIT_IQK, ("PHY_APCalibrate() offset 0x%x value 0x%x\n", offset, PHY_QueryBBReg(pAdapter, offset, bMaskDWord))); 	
				
				offset += 0x04;
			}				
			PHY_SetBBReg(pAdapter, rFPGA0_IQK, bMaskDWord, 0x00000000);							
		}
	
		//save RF default value
		regD[path] = PHY_QueryRFReg(pAdapter, (RF_RADIO_PATH_E)path, RF_TXBIAS_A, bRFRegOffsetMask);
		
		//Path A AFE all on, path B AFE All off or vise versa
		for(index = 0; index < IQK_ADDA_REG_NUM ; index++)
			PHY_SetBBReg(pAdapter, AFE_REG[index], bMaskDWord, AFE_on_off[path]);
		//RTPRINT(FINIT, INIT_IQK, ("PHY_APCalibrate() offset 0xe70 %x\n", PHY_QueryBBReg(pAdapter, rRx_Wait_CCA, bMaskDWord)));		

		//BB to AP mode
		if(path == 0)
		{				
			for(index = 0; index < APK_BB_REG_NUM ; index++)
			{

				if(index == 0)		//skip 
					continue;			
				else if (index < 5)
				PHY_SetBBReg(pAdapter, BB_REG[index], bMaskDWord, BB_AP_MODE[index]);
				else if (BB_REG[index] == 0x870)
					PHY_SetBBReg(pAdapter, BB_REG[index], bMaskDWord, BB_backup[index]|BIT10|BIT26);
				else
					PHY_SetBBReg(pAdapter, BB_REG[index], BIT10, 0x0);					
			}

			PHY_SetBBReg(pAdapter, rTx_IQK_Tone_A, bMaskDWord, 0x01008c00);			
			PHY_SetBBReg(pAdapter, rRx_IQK_Tone_A, bMaskDWord, 0x01008c00);					
		}
		else		//path B
		{
			PHY_SetBBReg(pAdapter, rTx_IQK_Tone_B, bMaskDWord, 0x01008c00);			
			PHY_SetBBReg(pAdapter, rRx_IQK_Tone_B, bMaskDWord, 0x01008c00);					
		
		}

		//RTPRINT(FINIT, INIT_IQK, ("PHY_APCalibrate() offset 0x800 %x\n", PHY_QueryBBReg(pAdapter, 0x800, bMaskDWord)));				

		//MAC settings
		phy_MACSettingCalibration(pAdapter, MAC_REG, MAC_backup);
		
		if(path == RF_PATH_A)	//Path B to standby mode
		{
			PHY_SetRFReg(pAdapter, RF_PATH_B, RF_AC, bRFRegOffsetMask, 0x10000);			
		}
		else			//Path A to standby mode
		{
			PHY_SetRFReg(pAdapter, RF_PATH_A, RF_AC, bRFRegOffsetMask, 0x10000);			
			PHY_SetRFReg(pAdapter, RF_PATH_A, RF_MODE1, bRFRegOffsetMask, 0x1000f);			
			PHY_SetRFReg(pAdapter, RF_PATH_A, RF_MODE2, bRFRegOffsetMask, 0x20103);						
		}

		delta_offset = ((delta+14)/2);
		if(delta_offset < 0)
			delta_offset = 0;
		else if (delta_offset > 12)
			delta_offset = 12;
			
		//AP calibration
		for(index = 0; index < APK_BB_REG_NUM; index++)
		{
			if(index != 1)	//only DO PA11+PAD01001, AP RF setting
				continue;
					
			tmpReg = APK_RF_init_value[path][index];
#if 1			
			if(!pdmpriv->bAPKThermalMeterIgnore)
			{
				BB_offset = (tmpReg & 0xF0000) >> 16;

				if(!(tmpReg & BIT15)) //sign bit 0
				{
					BB_offset = -BB_offset;
				}

				delta_V = APK_delta_mapping[index][delta_offset];
				
				BB_offset += delta_V;

				//RTPRINT(FINIT, INIT_IQK, ("PHY_APCalibrate() APK index %d tmpReg 0x%x delta_V %d delta_offset %d\n", index, tmpReg, delta_V, delta_offset));		
				
				if(BB_offset < 0)
				{
					tmpReg = tmpReg & (~BIT15);
					BB_offset = -BB_offset;
				}
				else
				{
					tmpReg = tmpReg | BIT15;
				}
				tmpReg = (tmpReg & 0xFFF0FFFF) | (BB_offset << 16);
			}
#endif

#ifdef CONFIG_PCI_HCI
			if(IS_81xxC_VENDOR_UMC_B_CUT(pHalData->VersionID))
				PHY_SetRFReg(pAdapter, (RF_RADIO_PATH_E)path, RF_IPA_A, bRFRegOffsetMask, 0x894ae);
			else
#endif	
				PHY_SetRFReg(pAdapter, (RF_RADIO_PATH_E)path, RF_IPA_A, bRFRegOffsetMask, 0x8992e);
			//RTPRINT(FINIT, INIT_IQK, ("PHY_APCalibrate() offset 0xc %x\n", PHY_QueryRFReg(pAdapter, (RF_RADIO_PATH_E)path, RF_IPA_A, bRFRegOffsetMask)));		
			PHY_SetRFReg(pAdapter, (RF_RADIO_PATH_E)path, RF_AC, bRFRegOffsetMask, APK_RF_value_0[path][index]);
			//RTPRINT(FINIT, INIT_IQK, ("PHY_APCalibrate() offset 0x0 %x\n", PHY_QueryRFReg(pAdapter, (RF_RADIO_PATH_E)path, RF_AC, bRFRegOffsetMask)));		
			PHY_SetRFReg(pAdapter, (RF_RADIO_PATH_E)path, RF_TXBIAS_A, bRFRegOffsetMask, tmpReg);
			//RTPRINT(FINIT, INIT_IQK, ("PHY_APCalibrate() offset 0xd %x\n", PHY_QueryRFReg(pAdapter, (RF_RADIO_PATH_E)path, RF_TXBIAS_A, bRFRegOffsetMask)));					
			
			// PA11+PAD01111, one shot	
			i = 0;
			do
			{
				PHY_SetBBReg(pAdapter, rFPGA0_IQK, bMaskDWord, 0x80000000);
				{
					PHY_SetBBReg(pAdapter, APK_offset[path], bMaskDWord, APK_value[0]);		
					//RTPRINT(FINIT, INIT_IQK, ("PHY_APCalibrate() offset 0x%x value 0x%x\n", APK_offset[path], PHY_QueryBBReg(pAdapter, APK_offset[path], bMaskDWord)));
					rtw_mdelay_os(3);				
					PHY_SetBBReg(pAdapter, APK_offset[path], bMaskDWord, APK_value[1]);
					//RTPRINT(FINIT, INIT_IQK, ("PHY_APCalibrate() offset 0x%x value 0x%x\n", APK_offset[path], PHY_QueryBBReg(pAdapter, APK_offset[path], bMaskDWord)));

					rtw_mdelay_os(20);
				}
				PHY_SetBBReg(pAdapter, rFPGA0_IQK, bMaskDWord, 0x00000000);

				if(path == RF_PATH_A)
					tmpReg = PHY_QueryBBReg(pAdapter, rAPK, 0x03E00000);
				else
					tmpReg = PHY_QueryBBReg(pAdapter, rAPK, 0xF8000000);
				//RTPRINT(FINIT, INIT_IQK, ("PHY_APCalibrate() offset 0xbd8[25:21] %x\n", tmpReg));		
				

				i++;
			}
			while(tmpReg > apkbound && i < 4);

			APK_result[path][index] = tmpReg;
		}
	}

	//reload MAC default value	
	phy_ReloadMACRegisters(pAdapter, MAC_REG, MAC_backup);

	//reload BB default value	
	for(index = 0; index < APK_BB_REG_NUM ; index++)
	{

		if(index == 0)		//skip 
			continue;					
		PHY_SetBBReg(pAdapter, BB_REG[index], bMaskDWord, BB_backup[index]);
	}
	
	//reload AFE default value
	phy_ReloadADDARegisters(pAdapter, AFE_REG, AFE_backup, IQK_ADDA_REG_NUM);

	//reload RF path default value
	for(path = 0; path < pathbound; path++)
	{
		PHY_SetRFReg(pAdapter, (RF_RADIO_PATH_E)path, RF_TXBIAS_A, bRFRegOffsetMask, regD[path]);
		if(path == RF_PATH_B)
		{
			PHY_SetRFReg(pAdapter, RF_PATH_A, RF_MODE1, bRFRegOffsetMask, 0x1000f);			
			PHY_SetRFReg(pAdapter, RF_PATH_A, RF_MODE2, bRFRegOffsetMask, 0x20101);						
		}

		//note no index == 0
		if (APK_result[path][1] > 6)
			APK_result[path][1] = 6;
		//RTPRINT(FINIT, INIT_IQK, ("apk path %d result %d 0x%x \t", path, 1, APK_result[path][1]));					
	}

	//RTPRINT(FINIT, INIT_IQK, ("\n"));

	for(path = 0; path < pathbound; path++)
	{
		PHY_SetRFReg(pAdapter, (RF_RADIO_PATH_E)path, RF_BS_PA_APSET_G1_G4, bRFRegOffsetMask, 
		((APK_result[path][1] << 15) | (APK_result[path][1] << 10) | (APK_result[path][1] << 5) | APK_result[path][1]));
		if(path == RF_PATH_A)
			PHY_SetRFReg(pAdapter, (RF_RADIO_PATH_E)path, RF_BS_PA_APSET_G5_G8, bRFRegOffsetMask, 
			((APK_result[path][1] << 15) | (APK_result[path][1] << 10) | (0x00 << 5) | 0x05));		
		else
		PHY_SetRFReg(pAdapter, (RF_RADIO_PATH_E)path, RF_BS_PA_APSET_G5_G8, bRFRegOffsetMask, 
			((APK_result[path][1] << 15) | (APK_result[path][1] << 10) | (0x02 << 5) | 0x05));						

		//if(!IS_HARDWARE_TYPE_8723A(pAdapter))		
		//	PHY_SetRFReg(pAdapter, (RF_RADIO_PATH_E)path, RF_BS_PA_APSET_G9_G11, bRFRegOffsetMask, 
		//	((0x08 << 15) | (0x08 << 10) | (0x08 << 5) | 0x08));
	}

	pdmpriv->bAPKdone = _TRUE;

	//RTPRINT(FINIT, INIT_IQK, ("<==PHY_APCalibrate()\n"));
}

static VOID phy_SetRFPathSwitch(
	IN	PADAPTER	pAdapter,
	IN	BOOLEAN		bMain,
	IN	BOOLEAN		is2T
	)
{
	
	if(!pAdapter->hw_init_completed)
	{
		PHY_SetBBReg(pAdapter, 0x4C, BIT23, 0x01);
		PHY_SetBBReg(pAdapter, rFPGA0_XAB_RFParameter, BIT13, 0x01);
	}
	
	if(bMain)
		PHY_SetBBReg(pAdapter, rFPGA0_XA_RFInterfaceOE, 0x300, 0x2);	
	else
		PHY_SetBBReg(pAdapter, rFPGA0_XA_RFInterfaceOE, 0x300, 0x1);		

	//RT_TRACE(COMP_OID_SET, DBG_LOUD, ("_PHY_SetRFPathSwitch 0x4C %lx, 0x878 %lx, 0x860 %lx \n", PHY_QueryBBReg(pAdapter, 0x4C, BIT23), PHY_QueryBBReg(pAdapter, 0x878, BIT13), PHY_QueryBBReg(pAdapter, 0x860, 0x300)));

}

VOID
rtl8192d_PHY_IQCalibrate(
	IN	PADAPTER	pAdapter
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	int			result[4][8];	//last is final result
	u8			i, final_candidate, Indexforchannel;
	BOOLEAN		bPathAOK, bPathBOK;
	int			RegE94, RegE9C, RegEA4, RegEAC, RegEB4, RegEBC, RegEC4, RegECC, RegTmp = 0;
	BOOLEAN		is12simular, is13simular, is23simular;
	BOOLEAN 	bStartContTx = _FALSE, bSingleTone = _FALSE, bCarrierSuppression = _FALSE;

	//if (ODM_CheckPowerStatus(pAdapter) == _FALSE)
	//	return;

#if (MP_DRIVER == 1)
	bStartContTx = pAdapter->mppriv.MptCtx.bStartContTx;
	bSingleTone = pAdapter->mppriv.MptCtx.bSingleTone;
	bCarrierSuppression = pAdapter->mppriv.MptCtx.bCarrierSuppression;
#endif

	//ignore IQK when continuous Tx
	if(bStartContTx || bSingleTone || bCarrierSuppression)
		return;

#if DISABLE_BB_RF
	return;
#endif

#ifdef CONFIG_DUALMAC_CONCURRENT
	if(pHalData->bSlaveOfDMSP)
		return;
#endif

	//RTPRINT(FINIT, INIT_IQK, ("IQK:Start!!!interface %d channel %d\n", pHalData->interfaceIndex, pHalData->CurrentChannel));

	for(i = 0; i < 8; i++)
	{
		result[0][i] = 0;
		result[1][i] = 0;
		result[2][i] = 0;
		result[3][i] = 0;
	}
	final_candidate = 0xff;
	bPathAOK = _FALSE;
	bPathBOK = _FALSE;
	is12simular = _FALSE;
	is23simular = _FALSE;
	is13simular = _FALSE;

	//RTPRINT(FINIT, INIT_IQK, ("IQK !!!interface %d currentband %d ishardwareD %d \n", pAdapter->interfaceIndex, pHalData->CurrentBandType92D, IS_HARDWARE_TYPE_8192D(pAdapter)));
	//AcquireCCKAndRWPageAControl(pAdapter);
	//RT_TRACE(COMP_INIT,DBG_LOUD,("Acquire Mutex in IQCalibrate \n"));
	for (i=0; i<3; i++)
	{
		if(pHalData->CurrentBandType92D == BAND_ON_5G)
		{
			phy_IQCalibrate_5G_Normal(pAdapter, result, i);
		}
		else if(pHalData->CurrentBandType92D == BAND_ON_2_4G)
		{
			if(IS_92D_SINGLEPHY(pHalData->VersionID))
				phy_IQCalibrate(pAdapter, result, i, _TRUE);
			else
				phy_IQCalibrate(pAdapter, result, i, _FALSE);
		}
		
		if(i == 1)
		{
			is12simular = phy_SimularityCompare(pAdapter, result, 0, 1);
			if(is12simular)
			{
				final_candidate = 0;
				break;
			}
		}
		
		if(i == 2)
		{
			is13simular = phy_SimularityCompare(pAdapter, result, 0, 2);
			if(is13simular)
			{
				final_candidate = 0;			
				break;
			}
			
			is23simular = phy_SimularityCompare(pAdapter, result, 1, 2);
			if(is23simular)
				final_candidate = 1;
			else
			{
				for(i = 0; i < 8; i++)
					RegTmp += result[3][i];

				if(RegTmp != 0)
					final_candidate = 3;			
				else
					final_candidate = 0xFF;
			}
		}
	}
	//RT_TRACE(COMP_INIT,DBG_LOUD,("Release Mutex in IQCalibrate \n"));
	//ReleaseCCKAndRWPageAControl(pAdapter);

        for (i=0; i<4; i++)
	{
		RegE94 = result[i][0];
		RegE9C = result[i][1];
		RegEA4 = result[i][2];
		RegEAC = result[i][3];
		RegEB4 = result[i][4];
		RegEBC = result[i][5];
		RegEC4 = result[i][6];
		RegECC = result[i][7];
		//RTPRINT(FINIT, INIT_IQK, ("IQK: RegE94=%lx RegE9C=%lx RegEA4=%lx RegEAC=%lx RegEB4=%lx RegEBC=%lx RegEC4=%lx RegECC=%lx\n ", RegE94, RegE9C, RegEA4, RegEAC, RegEB4, RegEBC, RegEC4, RegECC));
	}

	if(final_candidate != 0xff)
	{
		pdmpriv->RegE94 = RegE94 = result[final_candidate][0];
		pdmpriv->RegE9C = RegE9C = result[final_candidate][1];
		RegEA4 = result[final_candidate][2];
		RegEAC = result[final_candidate][3];
		pdmpriv->RegEB4 = RegEB4 = result[final_candidate][4];
		pdmpriv->RegEBC = RegEBC = result[final_candidate][5];
		RegEC4 = result[final_candidate][6];
		RegECC = result[final_candidate][7];
		DBG_8192C("IQK: final_candidate is %x\n", final_candidate);
		DBG_8192C("IQK: RegE94=%x RegE9C=%x RegEA4=%x RegEAC=%x RegEB4=%x RegEBC=%x RegEC4=%x RegECC=%x\n", RegE94, RegE9C, RegEA4, RegEAC, RegEB4, RegEBC, RegEC4, RegECC);
		bPathAOK = bPathBOK = _TRUE;
	}
	else
	{
		pdmpriv->RegE94 = pdmpriv->RegEB4 = 0x100;	//X default value
		pdmpriv->RegE9C = pdmpriv->RegEBC = 0x0;		//Y default value
	}

	if((RegE94 != 0)/*&&(RegEA4 != 0)*/)
	{
		if(pHalData->CurrentBandType92D == BAND_ON_5G)
			phy_PathAFillIQKMatrix_5G_Normal(pAdapter, bPathAOK, result, final_candidate, (RegEA4 == 0));	
		else	
			phy_PathAFillIQKMatrix(pAdapter, bPathAOK, result, final_candidate, (RegEA4 == 0));
	}

	if (IS_92C_SERIAL(pHalData->VersionID) || IS_92D_SINGLEPHY(pHalData->VersionID))
	{
		if((RegEB4 != 0)/*&&(RegEC4 != 0)*/)
		{
			if(pHalData->CurrentBandType92D == BAND_ON_5G)
				phy_PathBFillIQKMatrix_5G_Normal(pAdapter, bPathBOK, result, final_candidate, (RegEC4 == 0));
			else
				phy_PathBFillIQKMatrix(pAdapter, bPathBOK, result, final_candidate, (RegEC4 == 0));
		}
	}

	if(IS_HARDWARE_TYPE_8192D(pAdapter) && final_candidate != 0xFF)
	{
		Indexforchannel = rtl8192d_GetRightChnlPlaceforIQK(pHalData->CurrentChannel);

		for(i = 0; i < IQK_Matrix_REG_NUM; i++)
		{
			pHalData->IQKMatrixRegSetting[Indexforchannel].Value[0][i] = 
				result[final_candidate][i];
		}

		pHalData->IQKMatrixRegSetting[Indexforchannel].bIQKDone = _TRUE;

		//RT_TRACE(COMP_SCAN|COMP_MLME,DBG_LOUD,("\nIQK OK Indexforchannel %d.\n", Indexforchannel));
	}

}


VOID
rtl8192d_PHY_LCCalibrate(
	IN	PADAPTER	pAdapter
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	struct mlme_ext_priv	*pmlmeext = &pAdapter->mlmeextpriv;
	BOOLEAN 	bStartContTx = _FALSE, bSingleTone = _FALSE, bCarrierSuppression = _FALSE;
	u32			timeout = 2000, timecount = 0;
#ifdef CONFIG_DUALMAC_CONCURRENT
	PADAPTER	BuddyAdapter = pAdapter->pbuddy_adapter;
	struct mlme_priv	*pmlmeprivBuddyAdapter;
#endif

#if MP_DRIVER == 1
	bStartContTx = pAdapter->mppriv.MptCtx.bStartContTx;
	bSingleTone = pAdapter->mppriv.MptCtx.bSingleTone;
	bCarrierSuppression = pAdapter->mppriv.MptCtx.bCarrierSuppression;
#endif

#if DISABLE_BB_RF
	return;
#endif

	//ignore IQK when continuous Tx
	if(bStartContTx || bSingleTone || bCarrierSuppression)
		return;

#ifdef CONFIG_DUALMAC_CONCURRENT
	if(BuddyAdapter != NULL && 
		((pHalData->interfaceIndex == 0 && pHalData->CurrentBandType92D == BAND_ON_2_4G) ||
		(pHalData->interfaceIndex == 1 && pHalData->CurrentBandType92D == BAND_ON_5G)))
	{
		pmlmeprivBuddyAdapter = &BuddyAdapter->mlmepriv;
		while((check_fwstate(pmlmeprivBuddyAdapter, _FW_UNDER_LINKING|_FW_UNDER_SURVEY)==_TRUE) && timecount < timeout)
		{
			#ifdef CONFIG_LONG_DELAY_ISSUE
			rtw_msleep_os(50);
			#else
			rtw_mdelay_os(50);
			#endif
			timecount += 50;
		}
	}
#endif

	while((pmlmeext->sitesurvey_res.state == SCAN_PROCESS) && timecount < timeout)
	{
		#ifdef CONFIG_LONG_DELAY_ISSUE
		rtw_msleep_os(50);
		#else
		rtw_mdelay_os(50);
		#endif
		timecount += 50;
	}

	pHalData->bLCKInProgress = _TRUE;

	//DBG_8192C("LCK:Start!!!interface %d currentband %x delay %d ms\n", pHalData->interfaceIndex, pHalData->CurrentBandType92D, timecount);

	if(IS_92D_SINGLEPHY(pHalData->VersionID))
	{
		phy_LCCalibrate(pAdapter, _TRUE);
	}
	else{
		// For 88C 1T1R
		phy_LCCalibrate(pAdapter, _FALSE);
	}

	pHalData->bLCKInProgress = _FALSE;

	//RTPRINT(FINIT, INIT_IQK, ("LCK:Finish!!!interface %d\n", pHalData->interfaceIndex));
}

VOID
rtl8192d_PHY_APCalibrate(
	IN	PADAPTER	pAdapter,
	IN	char 		delta	
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;

#if DISABLE_BB_RF
	return;
#endif

	//if(IS_HARDWARE_TYPE_8192D(pAdapter))
		return;

	if(pdmpriv->bAPKdone)
		return;

//	if(IS_NORMAL_CHIP(pHalData->VersionID))
//		return;

	if(IS_92D_SINGLEPHY(pHalData->VersionID)){
		phy_APCalibrate(pAdapter, delta, _TRUE);
	}
	else{
		// For 88C 1T1R
		phy_APCalibrate(pAdapter, delta, _FALSE);
	}
}

/*
VOID PHY_SetRFPathSwitch(
	IN	PADAPTER	pAdapter,
	IN	BOOLEAN		bMain
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);

#if DISABLE_BB_RF
	return;
#endif

	if (IS_92D_SINGLEPHY(pHalData->VersionID))
	{
		_PHY_SetRFPathSwitch(pAdapter, bMain, _TRUE);
	}
	else{
		// For 88C 1T1R
		_PHY_SetRFPathSwitch(pAdapter, bMain, _FALSE);
	}
}


//return value TRUE => Main; FALSE => Aux
BOOLEAN PHY_QueryRFPathSwitch(	
	IN	PADAPTER	pAdapter
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);

#if DISABLE_BB_RF
	return _TRUE;
#endif

#ifdef CONFIG_USB_HCI
	return _TRUE;
#endif

	if(IS_92D_SINGLEPHY(pHalData->VersionID))
	{
		return _PHY_QueryRFPathSwitch(pAdapter, _TRUE);
	}
	else{
		// For 88C 1T1R
		return _PHY_QueryRFPathSwitch(pAdapter, _FALSE);
	}
}
*/

VOID
PHY_UpdateBBRFConfiguration8192D(
	IN PADAPTER Adapter,
	IN BOOLEAN bisBandSwitch
)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	u8	eRFPath = 0;
	BOOLEAN			bInternalPA;

	//DBG_8192C("PHY_UpdateBBRFConfiguration8192D()=====>\n");

	//Update BB
	//r_select_5G for path_A/B.0 for 2.4G,1 for 5G
	if(pHalData->CurrentBandType92D == BAND_ON_2_4G)
	{// 2.4G band
		//r_select_5G for path_A/B,0x878

		DBG_871X("==>PHY_UpdateBBRFConfiguration8192D() interface %d BAND_ON_2_4G settings\n", pHalData->interfaceIndex);
		
		PHY_SetBBReg(Adapter, rFPGA0_XAB_RFParameter, BIT0, 0x0);
		PHY_SetBBReg(Adapter, rFPGA0_XAB_RFParameter, BIT15, 0x0);
		if(pHalData->MacPhyMode92D != DUALMAC_DUALPHY)
		{
			PHY_SetBBReg(Adapter, rFPGA0_XAB_RFParameter, BIT16, 0x0);
			PHY_SetBBReg(Adapter, rFPGA0_XAB_RFParameter, BIT31, 0x0);
		}
		
		//rssi_table_select:index 0 for 2.4G.1~3 for 5G,0xc78
		PHY_SetBBReg(Adapter, rOFDM0_AGCRSSITable, BIT6|BIT7, 0x0);
		
		//fc_area//0xd2c
		PHY_SetBBReg(Adapter, rOFDM1_CFOTracking, BIT14|BIT13, 0x0);
		// 5G LAN ON
		PHY_SetBBReg(Adapter, 0xB30, 0x00F00000, 0xa);

		//TX BB gain shift*1,Just for testchip,0xc80,0xc88
		PHY_SetBBReg(Adapter, rOFDM0_XATxIQImbalance, bMaskDWord, 0x40000100);
		PHY_SetBBReg(Adapter, rOFDM0_XBTxIQImbalance, bMaskDWord, 0x40000100);

		if(pHalData->MacPhyMode92D == DUALMAC_DUALPHY)
		{
			PHY_SetBBReg(Adapter, rOFDM0_XATxIQImbalance, bMaskDWord, 0x40000100);
			pdmpriv->OFDM_index[RF_PATH_A] = 0x0c;		
			PHY_SetBBReg(Adapter, rFPGA0_XAB_RFInterfaceSW, BIT10|BIT6|BIT5, 
				((pHalData->EEPROMC9&BIT3) >> 3)|(pHalData->EEPROMC9&BIT1)|((pHalData->EEPROMCC&BIT1) << 4));	
			PHY_SetBBReg(Adapter, rFPGA0_XA_RFInterfaceOE, BIT10|BIT6|BIT5, 
				((pHalData->EEPROMC9&BIT2) >> 2)|((pHalData->EEPROMC9&BIT0) << 1)|((pHalData->EEPROMCC&BIT0) << 5));						
			PHY_SetBBReg(Adapter, rFPGA0_XAB_RFParameter, BIT15, 0);	
			PHY_SetBBReg(Adapter, rPdp_AntA, bMaskDWord, 0x01017038);
			PHY_SetBBReg(Adapter, rConfig_AntA, bMaskDWord, 0x0f600000);
		}
		else
		{
			PHY_SetBBReg(Adapter, rOFDM0_XATxIQImbalance, bMaskDWord, 0x40000100);
			PHY_SetBBReg(Adapter, rOFDM0_XBTxIQImbalance, bMaskDWord, 0x40000100);
			pdmpriv->OFDM_index[RF_PATH_A] = 0x0c;		
			pdmpriv->OFDM_index[RF_PATH_B] = 0x0c;		
		
			PHY_SetBBReg(Adapter, rFPGA0_XAB_RFInterfaceSW, BIT26|BIT22|BIT21|BIT10|BIT6|BIT5, 
				((pHalData->EEPROMC9&BIT3) >> 3)|(pHalData->EEPROMC9&BIT1)|((pHalData->EEPROMCC&BIT1) << 4)|((pHalData->EEPROMC9&BIT7) << 9)|((pHalData->EEPROMC9&BIT5) << 12)|((pHalData->EEPROMCC&BIT3) << 18));	
			PHY_SetBBReg(Adapter, rFPGA0_XA_RFInterfaceOE, BIT10|BIT6|BIT5, 
				((pHalData->EEPROMC9&BIT2) >> 2)|((pHalData->EEPROMC9&BIT0) << 1)|((pHalData->EEPROMCC&BIT0) << 5));			
			PHY_SetBBReg(Adapter, rFPGA0_XB_RFInterfaceOE, BIT10|BIT6|BIT5, 
				((pHalData->EEPROMC9&BIT6) >> 6)|((pHalData->EEPROMC9&BIT4) >> 3)|((pHalData->EEPROMCC&BIT2) << 3));						
			PHY_SetBBReg(Adapter, rFPGA0_XAB_RFParameter, BIT31|BIT15, 0);	
			PHY_SetBBReg(Adapter, rPdp_AntA, bMaskDWord, 0x01017038);
			PHY_SetBBReg(Adapter, rPdp_AntB, bMaskDWord, 0x01017038);	

			PHY_SetBBReg(Adapter, rConfig_AntA, bMaskDWord, 0x0f600000);
			PHY_SetBBReg(Adapter, rConfig_AntB, bMaskDWord, 0x0f600000);
		}
		pdmpriv->CCK_index = 0x0c;

	}
	else	//5G band
	{
		DBG_871X("==>PHY_UpdateBBRFConfiguration8192D() interface %d BAND_ON_5G settings\n", pHalData->interfaceIndex);
	
		//r_select_5G for path_A/B
		PHY_SetBBReg(Adapter, rFPGA0_XAB_RFParameter, BIT0, 0x1);
		PHY_SetBBReg(Adapter, rFPGA0_XAB_RFParameter, BIT15, 0x1);
		if(pHalData->MacPhyMode92D != DUALMAC_DUALPHY)
		{
			PHY_SetBBReg(Adapter, rFPGA0_XAB_RFParameter, BIT16, 0x1);
			PHY_SetBBReg(Adapter, rFPGA0_XAB_RFParameter, BIT31, 0x1);
		}
		
		//rssi_table_select:index 0 for 2.4G.1~3 for 5G
		PHY_SetBBReg(Adapter, rOFDM0_AGCRSSITable, BIT6|BIT7, 0x1);
		
		//fc_area
		PHY_SetBBReg(Adapter, rOFDM1_CFOTracking, BIT14|BIT13, 0x1);
		// 5G LAN ON
		PHY_SetBBReg(Adapter, 0xB30, 0x00F00000, 0x0);

		//TX BB gain shift,Just for testchip,0xc80,0xc88
		if(pHalData->MacPhyMode92D == DUALMAC_DUALPHY)
		{
			if(pHalData->interfaceIndex == 0)
				bInternalPA = pHalData->InternalPA5G[0];
			else
				bInternalPA = pHalData->InternalPA5G[1];							

			if(bInternalPA)
			{
				PHY_SetBBReg(Adapter, rOFDM0_XATxIQImbalance, bMaskDWord, 0x2d4000b5);
				pdmpriv->OFDM_index[RF_PATH_A] = 0x12;
			}
			else 
			{
				PHY_SetBBReg(Adapter, rOFDM0_XATxIQImbalance, bMaskDWord, 0x20000080);
				pdmpriv->OFDM_index[RF_PATH_A] = 0x18;
			}
		}
		else
		{
			if(pHalData->InternalPA5G[0])
			{
				PHY_SetBBReg(Adapter, rOFDM0_XATxIQImbalance, bMaskDWord, 0x2d4000b5);
				pdmpriv->OFDM_index[RF_PATH_A] = 0x12;
			}
			else 
			{
				PHY_SetBBReg(Adapter, rOFDM0_XATxIQImbalance, bMaskDWord, 0x20000080);
				pdmpriv->OFDM_index[RF_PATH_A] = 0x18;
			}

			if(pHalData->InternalPA5G[1])
			{
				PHY_SetBBReg(Adapter, rOFDM0_XBTxIQImbalance, bMaskDWord, 0x2d4000b5);			
				pdmpriv->OFDM_index[RF_PATH_B] = 0x12;
			}
			else
			{
				PHY_SetBBReg(Adapter, rOFDM0_XBTxIQImbalance, bMaskDWord, 0x20000080);
				pdmpriv->OFDM_index[RF_PATH_A] = 0x18;
			}
		}

		DBG_871X("==>PHY_UpdateBBRFConfiguration8192D() interface %d BAND_ON_5G settings OFDM index 0x%x\n", pHalData->interfaceIndex, pdmpriv->OFDM_index[RF_PATH_A]);

		if(pHalData->MacPhyMode92D == DUALMAC_DUALPHY)
		{
			PHY_SetBBReg(Adapter, rFPGA0_XAB_RFInterfaceSW, BIT10|BIT6|BIT5, 
				(pHalData->EEPROMCC&BIT5));	
			PHY_SetBBReg(Adapter, rFPGA0_XA_RFInterfaceOE, BIT10, 
				((pHalData->EEPROMCC&BIT4) >> 4));				
			PHY_SetBBReg(Adapter, rFPGA0_XAB_RFParameter, BIT15, 
				(pHalData->EEPROMCC&BIT4) >> 4);
			PHY_SetBBReg(Adapter, rPdp_AntA, bMaskDWord, 0x01017098);	
			if(pdmpriv->bDPKdone[RF_PATH_A])			
				PHY_SetBBReg(Adapter, 0xb68, bMaskDWord, 0x08080000);
			else
				PHY_SetBBReg(Adapter, 0xb68, bMaskDWord, 0x20000000);
		}
		else	
		{
			PHY_SetBBReg(Adapter, rFPGA0_XAB_RFInterfaceSW, BIT26|BIT22|BIT21|BIT10|BIT6|BIT5, 
				(pHalData->EEPROMCC&BIT5)|((pHalData->EEPROMCC&BIT7) << 14));
			PHY_SetBBReg(Adapter, rFPGA0_XA_RFInterfaceOE, BIT10, 
				((pHalData->EEPROMCC&BIT4) >> 4));
			PHY_SetBBReg(Adapter, rFPGA0_XB_RFInterfaceOE, BIT10, 
				((pHalData->EEPROMCC&BIT6) >> 6));
			PHY_SetBBReg(Adapter, rFPGA0_XAB_RFParameter, BIT31|BIT15, 
				((pHalData->EEPROMCC&BIT4) >> 4)|((pHalData->EEPROMCC&BIT6) << 10));
			PHY_SetBBReg(Adapter, rPdp_AntA, bMaskDWord, 0x01017098);
			PHY_SetBBReg(Adapter, rPdp_AntB, bMaskDWord, 0x01017098);
			if(pdmpriv->bDPKdone[RF_PATH_A])	
				PHY_SetBBReg(Adapter, 0xb68, bMaskDWord, 0x08080000);
			else
				PHY_SetBBReg(Adapter, 0xb68, bMaskDWord, 0x20000000);
			if(pdmpriv->bDPKdone[RF_PATH_B])	
				PHY_SetBBReg(Adapter, 0xb6c, bMaskDWord, 0x08080000);
			else	
				PHY_SetBBReg(Adapter, 0xb6c, bMaskDWord, 0x20000000);
		}

	}

	//update IQK related settings
	{
		PHY_SetBBReg(Adapter, rOFDM0_XARxIQImbalance, bMaskDWord, 0x40000100);
		PHY_SetBBReg(Adapter, rOFDM0_XBRxIQImbalance, bMaskDWord, 0x40000100);
		PHY_SetBBReg(Adapter, rOFDM0_XCTxAFE, 0xF0000000, 0x00);
		PHY_SetBBReg(Adapter, rOFDM0_ECCAThreshold,  BIT30|BIT28|BIT26|BIT24,  0x00);
		PHY_SetBBReg(Adapter, rOFDM0_XDTxAFE, 0xF0000000, 0x00);
		PHY_SetBBReg(Adapter, rOFDM0_RxIQExtAnta, 0xF0000000, 0x00);
		PHY_SetBBReg(Adapter, rOFDM0_AGCRSSITable, 0x0000F000, 0x00);
	}
		
	//Update RF	
	for(eRFPath = RF_PATH_A; eRFPath <pHalData->NumTotalRFPath; eRFPath++)
	{
		if(pHalData->CurrentBandType92D == BAND_ON_2_4G){
			//MOD_AG for RF paht_A 0x18 BIT8,BIT16
			PHY_SetRFReg(Adapter, (RF_RADIO_PATH_E)eRFPath, RF_CHNLBW, BIT8|BIT16|BIT18|0xFF, 1);
			
			//RF0x0b[16:14] =3b'111  
			PHY_SetRFReg(Adapter, (RF_RADIO_PATH_E)eRFPath, RF_TXPA_AG, 0x1c000, 0x07);
		}
		else{ //5G band
			//MOD_AG for RF paht_A 0x18 BIT8,BIT16
			PHY_SetRFReg(Adapter, RF_PATH_A, RF_CHNLBW, bRFRegOffsetMask, 0x97524); //set channel 36

		}

#if 1
		if((pHalData->interfaceIndex == 0 && pHalData->BandSet92D == BAND_ON_2_4G)
			|| (pHalData->interfaceIndex == 1 && pHalData->BandSet92D == BAND_ON_5G))
		{
			//Set right channel on RF reg0x18 for another mac. 
			if(pHalData->interfaceIndex == 0) //set MAC1 default channel if MAC1 not up.
 			{
				if(!(rtw_read8(Adapter, REG_MAC1)&MAC1_ON))
				{
					rtl8192d_PHY_EnableAnotherPHY(Adapter, _TRUE);
					PHY_SetRFReg(Adapter, RF_PATH_A, RF_CHNLBW|MAC0_ACCESS_PHY1, bRFRegOffsetMask, 0x97524); //set channel 36
					DBG_871X("PHY_UpdateBBRFConfiguration8192D(),MAC0 set MAC1 RF0x18:%x.\n",PHY_QueryRFReg(Adapter,RF_PATH_A,RF_CHNLBW|MAC0_ACCESS_PHY1,bRFRegOffsetMask));
					rtl8192d_PHY_PowerDownAnotherPHY(Adapter, _TRUE);
				}
			}
			else
			if(pHalData->interfaceIndex == 1)//set MAC0 default channel
			{
				if(!(rtw_read8(Adapter, REG_MAC0)&MAC0_ON))
				{
					rtl8192d_PHY_EnableAnotherPHY(Adapter, _FALSE);
					PHY_SetRFReg(Adapter, RF_PATH_A, RF_CHNLBW|MAC1_ACCESS_PHY0, bRFRegOffsetMask, 0x87401); // set channel 1
					DBG_871X("PHY_UpdateBBRFConfiguration8192D(),MAC1 set MAC0 RF0x18:%x.\n",PHY_QueryRFReg(Adapter,RF_PATH_A,RF_CHNLBW|MAC1_ACCESS_PHY0,bRFRegOffsetMask));
					rtl8192d_PHY_PowerDownAnotherPHY(Adapter, _FALSE);
				}
			}
		}
#endif

	}

	//Update for all band.
	if(pHalData->rf_type == RF_1T1R)
	{ //DMDP
		//Use antenna 0,0xc04,0xd04
#if MP_DRIVER == 1
		if(!bisBandSwitch)
#endif
		{
		PHY_SetBBReg(Adapter, rOFDM0_TRxPathEnable, bMaskByte0, 0x11);
		PHY_SetBBReg(Adapter, rOFDM1_TRxPathEnable, bDWord, 0x1);
		}

		//enable ad/da clock1 for dual-phy reg0x888
		if(pHalData->interfaceIndex == 0)
			PHY_SetBBReg(Adapter, rFPGA0_AdDaClockEn, BIT12|BIT13, 0x3);
		else
		{
			BOOLEAN bMAC0NotUp =_FALSE;

			bMAC0NotUp = rtl8192d_PHY_EnableAnotherPHY(Adapter, _FALSE);
			if(bMAC0NotUp)
			{
#ifdef CONFIG_PCI_HCI
				//RT_TRACE(COMP_INIT,DBG_LOUD,("MAC1 use DBI to update 0x888"));
				//0x888
				MpWritePCIDwordDBI8192D(Adapter, 
									rFPGA0_AdDaClockEn, 
									MpReadPCIDwordDBI8192D(Adapter, rFPGA0_AdDaClockEn, BIT3)|BIT12|BIT13,
									BIT3);
#else	//USB interface
				//RT_TRACE(COMP_INIT,DBG_LOUD,("MAC1 update MAC0's 0x888"));
				PHY_SetBBReg(Adapter, rFPGA0_AdDaClockEn|MAC1_ACCESS_PHY0, BIT12|BIT13, 0x3);
#endif
				rtl8192d_PHY_PowerDownAnotherPHY(Adapter, _FALSE);
			}
		}
	}
	else // 2T2R //Single PHY
	{
		//Use antenna 0 & 1,0xc04,0xd04
		PHY_SetBBReg(Adapter, rOFDM0_TRxPathEnable, bMaskByte0, 0x33);
		PHY_SetBBReg(Adapter, rOFDM1_TRxPathEnable, bDWord, 0x3);

		//disable ad/da clock1,0x888
		PHY_SetBBReg(Adapter, rFPGA0_AdDaClockEn, BIT12|BIT13, 0);
	}

#if MP_DRIVER == 1
	if(bisBandSwitch)
	{
		PHY_SetBBReg(Adapter, rOFDM0_TRxPathEnable, bMaskByte0, pdmpriv->RegC04_MP);
		PHY_SetBBReg(Adapter, rOFDM1_TRxPathEnable, bDWord, pdmpriv->RegD04_MP);
	}
#endif

	for(eRFPath = RF_PATH_A; eRFPath <pHalData->NumTotalRFPath; eRFPath++)
	{
		pHalData->RfRegChnlVal[eRFPath] = PHY_QueryRFReg(Adapter, (RF_RADIO_PATH_E)eRFPath, RF_CHNLBW, bRFRegOffsetMask);
		pdmpriv->RegRF3C[eRFPath] = PHY_QueryRFReg(Adapter, (RF_RADIO_PATH_E)eRFPath, RF_RXRF_A3, bRFRegOffsetMask);
	}

	//for(i = 0; i < 2; i++)
	//	DBG_8192C("PHY_UpdateBBRFConfiguration8192D RF 0x18 = 0x%x interface index %d\n",pHalData->RfRegChnlVal[i],	pHalData->interfaceIndex);

	//RT_TRACE(COMP_INIT,DBG_LOUD,("<==PHY_UpdateBBRFConfiguration8192D()\n"));

}

//
//	Description:
//		Read HW adapter information through EEPROM 93C46.
//		Or For EFUSE 92S .And Get and Set 92D MACPHY mode and Band Type.
//		MacPhyMode:DMDP,SMSP.
//		BandType:2.4G,5G.
//
//	Assumption:
//		1. Boot from EEPROM and CR9346 regiser has verified.
//		2. PASSIVE_LEVEL (USB interface)
//
VOID PHY_ReadMacPhyMode92D(
		IN PADAPTER			Adapter,
		IN	BOOLEAN		AutoloadFail		
)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	u8	MacPhyCrValue = 0;

	if(AutoloadFail)
	{
		pHalData->MacPhyMode92D = SINGLEMAC_SINGLEPHY;
		return;	
	}

	MacPhyCrValue = rtw_read8(Adapter, REG_MAC_PHY_CTRL_NORMAL);

	DBG_8192C("PHY_ReadMacPhyMode92D():   MAC_PHY_CTRL Value %x \n",MacPhyCrValue);
	
	if((MacPhyCrValue&0x03) == 0x03)
	{
		pHalData->MacPhyMode92D = DUALMAC_DUALPHY;
	}
	else if((MacPhyCrValue&0x03) == 0x01)
	{
		pHalData->MacPhyMode92D = DUALMAC_SINGLEPHY;
	}
	else
	{
		pHalData->MacPhyMode92D = SINGLEMAC_SINGLEPHY;
	}		
}

//
//	Description:
//		Read HW adapter information through EEPROM 93C46.
//		Or For EFUSE 92S .And Get and Set 92D MACPHY mode and Band Type.
//		MacPhyMode:DMDP,SMSP.
//		BandType:2.4G,5G.
//
//	Assumption:
//		1. Boot from EEPROM and CR9346 regiser has verified.
//		2. PASSIVE_LEVEL (USB interface)
//
VOID PHY_ConfigMacPhyMode92D(
		IN PADAPTER			Adapter
)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	u8	offset = REG_MAC_PHY_CTRL_NORMAL;
			
	switch(pHalData->MacPhyMode92D){
		case DUALMAC_DUALPHY:
			DBG_8192C("MacPhyMode: DUALMAC_DUALPHY \n");
			rtw_write8(Adapter, offset, 0xF3);
			break;
		case SINGLEMAC_SINGLEPHY:
			DBG_8192C("MacPhyMode: SINGLEMAC_SINGLEPHY \n");
			rtw_write8(Adapter, offset, 0xF4);
			break;
		case DUALMAC_SINGLEPHY:
			DBG_8192C("MacPhyMode: DUALMAC_SINGLEPHY \n");
			rtw_write8(Adapter, offset, 0xF1);
			break;
	}		
}

//
//	Description:
//		Read HW adapter information through EEPROM 93C46.
//		Or For EFUSE 92S .And Get and Set 92D MACPHY mode and Band Type.
//		MacPhyMode:DMDP,SMSP.
//		BandType:2.4G,5G.
//
//	Assumption:
//		1. Boot from EEPROM and CR9346 regiser has verified.
//		2. PASSIVE_LEVEL (USB interface)
//
VOID PHY_ConfigMacPhyModeInfo92D(
		IN PADAPTER			Adapter
)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
#ifdef CONFIG_DUALMAC_CONCURRENT
	PADAPTER		BuddyAdapter = Adapter->pbuddy_adapter;
	HAL_DATA_TYPE	*pHalDataBuddyAdapter;
#endif

	switch(pHalData->MacPhyMode92D){
		case DUALMAC_SINGLEPHY:
			pHalData->rf_type = RF_2T2R;
			pHalData->VersionID = (VERSION_8192D)(pHalData->VersionID | RF_TYPE_2T2R);
			pHalData->BandSet92D = BAND_ON_BOTH;
			pHalData->CurrentBandType92D = BAND_ON_2_4G;
#ifdef CONFIG_DUALMAC_CONCURRENT
//get bMasetOfDMSP and bSlaveOfDMSP sync with buddy adapter
			ACQUIRE_GLOBAL_MUTEX(GlobalCounterForMutex);
			if(BuddyAdapter != NULL)
			{
				pHalDataBuddyAdapter = GET_HAL_DATA(BuddyAdapter);
				pHalData->bMasterOfDMSP = !pHalDataBuddyAdapter->bMasterOfDMSP;
				pHalData->bSlaveOfDMSP = !pHalDataBuddyAdapter->bSlaveOfDMSP;
				pHalData->CurrentBandType92D = pHalDataBuddyAdapter->CurrentBandType92D;				
			}
			else
			{
				if(pHalData->interfaceIndex == 0)
				{
					pHalData->bMasterOfDMSP = _TRUE;
					pHalData->bSlaveOfDMSP = _FALSE;
				}
				else if(pHalData->interfaceIndex == 1)
				{
					pHalData->bMasterOfDMSP = _FALSE;
					pHalData->bSlaveOfDMSP = _TRUE;
				}
			}
			RELEASE_GLOBAL_MUTEX(GlobalCounterForMutex);
#endif
			break;

		case SINGLEMAC_SINGLEPHY:
			pHalData->rf_type = RF_2T2R;
			pHalData->VersionID = (VERSION_8192D)(pHalData->VersionID | RF_TYPE_2T2R);
			pHalData->BandSet92D = BAND_ON_BOTH;
			pHalData->CurrentBandType92D = BAND_ON_2_4G;
			pHalData->bMasterOfDMSP = _FALSE;
			pHalData->bSlaveOfDMSP = _FALSE;
			break;

		case DUALMAC_DUALPHY:
			pHalData->rf_type = RF_1T1R;
			pHalData->VersionID = (VERSION_8192D)(pHalData->VersionID & RF_TYPE_1T1R);
			if(pHalData->interfaceIndex == 1){
				pHalData->BandSet92D = BAND_ON_5G;
				pHalData->CurrentBandType92D = BAND_ON_5G;//Now we let MAC1 run on 5G band.
			}
			else{
				pHalData->BandSet92D = BAND_ON_2_4G;
				pHalData->CurrentBandType92D = BAND_ON_2_4G;//
			}
			pHalData->bMasterOfDMSP = _FALSE;
			pHalData->bSlaveOfDMSP = _FALSE;
			break;

		default:
			break;	
	}

	/*if(Adapter->bInHctTest&&(pHalData->MacPhyMode92D == SINGLEMAC_SINGLEPHY))
	{
		pHalData->CurrentBandType92D=BAND_ON_2_4G;
		pHalData->BandSet92D = BAND_ON_2_4G;
	}*/

	if(pHalData->CurrentBandType92D == BAND_ON_2_4G)
		pHalData->CurrentChannel = 1;
	else
		pHalData->CurrentChannel = 36;

	Adapter->registrypriv.channel = pHalData->CurrentChannel;

#if DBG
	switch(pHalData->VersionID)
	{
		case VERSION_NORMAL_CHIP_92D_D_CUT_SINGLEPHY:
			MSG_8192C("Chip Version ID: VERSION_NORMAL_CHIP_92D_D_CUT_SINGLEPHY.\n");
			break;
		case VERSION_NORMAL_CHIP_92D_D_CUT_DUALPHY:
			MSG_8192C("Chip Version ID: VERSION_NORMAL_CHIP_92D_D_CUT_DUALPHY.\n");
			break;
		case VERSION_NORMAL_CHIP_92D_E_CUT_SINGLEPHY:
			MSG_8192C("Chip Version ID: VERSION_NORMAL_CHIP_92D_E_CUT_SINGLEPHY.\n");
			break;
		case VERSION_NORMAL_CHIP_92D_E_CUT_DUALPHY:
			MSG_8192C("Chip Version ID: VERSION_NORMAL_CHIP_92D_E_CUT_DUALPHY.\n");
			break;
		case VERSION_NORMAL_CHIP_92D_C_CUT_SINGLEPHY:
			MSG_8192C("Chip Version ID: VERSION_NORMAL_CHIP_92D_C_CUT_SINGLEPHY.\n");
			break;
		case VERSION_NORMAL_CHIP_92D_C_CUT_DUALPHY:
			MSG_8192C("Chip Version ID: VERSION_NORMAL_CHIP_92D_C_CUT_DUALPHY.\n");
			break;
		case VERSION_TEST_CHIP_92D_SINGLEPHY:
			MSG_8192C("Chip Version ID: VERSION_TEST_CHIP_92D_SINGLEPHY.\n");
			break;
		case VERSION_TEST_CHIP_92D_DUALPHY:
			MSG_8192C("Chip Version ID: VERSION_TEST_CHIP_92D_DUALPHY.\n");
			break;	
		default:
			MSG_8192C("Chip Version ID: ???????????????.0x%04X\n",pHalData->VersionID);
			break;
	}
#endif

	switch(pHalData->BandSet92D)
	{
		case BAND_ON_2_4G:
			Adapter->registrypriv.wireless_mode = WIRELESS_11BG_24N;
			break;

		case BAND_ON_5G:
			Adapter->registrypriv.wireless_mode = WIRELESS_11A_5N;
			break;

		case BAND_ON_BOTH:
			Adapter->registrypriv.wireless_mode = WIRELESS_11ABGN;
			break;

		default:
			Adapter->registrypriv.wireless_mode = WIRELESS_11ABGN;
			break;
	}
	DBG_8192C("%s(): wireless_mode = %x\n",__FUNCTION__,Adapter->registrypriv.wireless_mode);
}

//
//	Description:
//	set RX packet buffer and other setting acording to dual mac mode
//
//	Assumption:
//		1. Boot from EEPROM and CR9346 regiser has verified.
//		2. PASSIVE_LEVEL (USB interface)
//
VOID PHY_ConfigMacCoexist_RFPage92D(
		IN PADAPTER			Adapter
)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);

	switch(pHalData->MacPhyMode92D)
	{
		case DUALMAC_DUALPHY:
			rtw_write8(Adapter,REG_DMC, 0x0);
			rtw_write8(Adapter,REG_RX_PKT_LIMIT,0x08);
			rtw_write16(Adapter,(REG_TRXFF_BNDY+2), 0x13ff);
			break;
		case DUALMAC_SINGLEPHY:
			rtw_write8(Adapter,REG_DMC, 0xf8);
			rtw_write8(Adapter,REG_RX_PKT_LIMIT,0x08);
			rtw_write16(Adapter,(REG_TRXFF_BNDY+2), 0x13ff);
			break;
		case SINGLEMAC_SINGLEPHY:
			rtw_write8(Adapter,REG_DMC, 0x0);
			rtw_write8(Adapter,REG_RX_PKT_LIMIT,0x10);
			rtw_write16(Adapter, (REG_TRXFF_BNDY + 2), 0x27FF);
			break;
		default:
			break;
	}		
}

VOID
rtl8192d_PHY_InitRxSetting(
	IN	PADAPTER Adapter
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);

#if MP_DRIVER != 1
	return;
#endif

	if(pHalData->interfaceIndex == 0)
	{
		rtw_write32(Adapter, REG_MACID, 0x87654321);
		rtw_write32(Adapter, 0x0700, 0x87654321);
	}
	else
	{
		rtw_write32(Adapter, REG_MACID, 0x12345678);
		rtw_write32(Adapter, 0x0700, 0x12345678); 	
	}	
}


VOID
rtl8192d_PHY_ResetIQKResult(
	IN	PADAPTER Adapter
)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	u8			i;

	//RT_TRACE(COMP_INIT, DBG_LOUD, ("PHY_ResetIQKResult interface %d settings regs %d default regs %d\n", Adapter->interfaceIndex, sizeof(pHalData->IQKMatrixRegSetting)/sizeof(IQK_MATRIX_REGS_SETTING), IQK_Matrix_Settings_NUM));
	//0xe94, 0xe9c, 0xea4, 0xeac, 0xeb4, 0xebc, 0xec4, 0xecc

	for(i = 0; i < IQK_Matrix_Settings_NUM; i++)
	{
		{
			pHalData->IQKMatrixRegSetting[i].Value[0][0] = 
				pHalData->IQKMatrixRegSetting[i].Value[0][2] = 
				pHalData->IQKMatrixRegSetting[i].Value[0][4] = 
				pHalData->IQKMatrixRegSetting[i].Value[0][6] = 0x100;

			pHalData->IQKMatrixRegSetting[i].Value[0][1] = 
				pHalData->IQKMatrixRegSetting[i].Value[0][3] = 
				pHalData->IQKMatrixRegSetting[i].Value[0][5] = 
				pHalData->IQKMatrixRegSetting[i].Value[0][7] = 0x0;

			pHalData->IQKMatrixRegSetting[i].bIQKDone = _FALSE;
			
		}
	}
}

VOID rtl8192d_PHY_SetRFPathSwitch(
	IN	PADAPTER	pAdapter,
	IN	BOOLEAN		bMain
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);

#if DISABLE_BB_RF
	return ;
#endif

	if (IS_92D_SINGLEPHY(pHalData->VersionID))
	{
		phy_SetRFPathSwitch(pAdapter, bMain, _TRUE);
	}
	else{
		// For 88C 1T1R
		phy_SetRFPathSwitch(pAdapter, bMain, _FALSE);
	}
}

VOID
HalChangeCCKStatus8192D(
	IN	PADAPTER	Adapter,
	IN	BOOLEAN		bCCKDisable
)
{
	//PADAPTER	BuddyAdapter = Adapter->BuddyAdapter;
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(Adapter);
	u8	i;

	//DBG_871X("MAC %d: =====> ChangeCCKStatus8192D \n",pHalData->interfaceIndex);

	if(pHalData->BandSet92D != BAND_ON_BOTH)
	{
		//DBG_871X("ChangeCCKStatus8192D():  Skip \n");
		return;
	}
	
	if(bCCKDisable)
	{
		//if(ACTING_AS_AP(Adapter) ||ACTING_AS_AP(ADJUST_TO_ADAPTIVE_ADAPTER(Adapter, FALSE)) || (Adapter->MgntInfo.mIbss))
		//	StopTxBeacon(Adapter);
		rtw_write16(Adapter, REG_RL,0x0101);
		for(i=0;i<30;i++)
		{
			if(rtw_read32(Adapter, 0x200) != rtw_read32(Adapter, 0x204))
			{
				DBG_871X("packet in tx packet buffer aaaaaaaaa 0x204 %x  \n", rtw_read32(Adapter, 0x204));
				DBG_871X("packet in tx packet buffer aaaaaaa 0x200 %x  \n", rtw_read32(Adapter, 0x200));
				rtw_udelay_os(1000);
			}
			else
			{
				//DBG_871X("no packet in tx packet buffer \n");
				break;
			}
		}

		/*if((BuddyAdapter != NULL) && BuddyAdapter->bHWInitReady && (pHalData->MacPhyMode92D == DUALMAC_SINGLEPHY))
		{
			if(ACTING_AS_AP(BuddyAdapter) ||ACTING_AS_AP(ADJUST_TO_ADAPTIVE_ADAPTER(BuddyAdapter, FALSE)) || BuddyAdapter->MgntInfo.mIbss)
				StopTxBeacon(BuddyAdapter);
			PlatformEFIOWrite2Byte(BuddyAdapter, REG_RL,0x0101);
			for(i=0;i<30;i++)
			{
				if(PlatformEFIORead4Byte(BuddyAdapter, 0x200) != PlatformEFIORead4Byte(BuddyAdapter, 0x204))
				{
					RT_TRACE(COMP_EASY_CONCURRENT,DBG_LOUD,("packet in tx packet buffer aaaaaaaaa 0x204 %x  \n", PlatformEFIORead4Byte(BuddyAdapter, 0x204)));
					RT_TRACE(COMP_EASY_CONCURRENT,DBG_LOUD,("packet in tx packet buffer aaaaaaa 0x200 %x  \n", PlatformEFIORead4Byte(BuddyAdapter, 0x200)));
					PlatformStallExecution(1000);
				}
				else
				{
					RT_TRACE(COMP_EASY_CONCURRENT,DBG_LOUD,("no packet in tx packet buffer \n"));
					break;
				}
			}

		}*/

		PHY_SetBBReg1Byte(Adapter, rFPGA0_RFMOD, bOFDMEn|bCCKEn, 3);
	}
	else
	{
		u8	RetryLimit = 0x30;
		//if(ACTING_AS_AP(Adapter) ||ACTING_AS_AP(ADJUST_TO_ADAPTIVE_ADAPTER(Adapter, FALSE)) || Adapter->MgntInfo.mIbss)
		//	ResumeTxBeacon(Adapter);

		rtw_write16(Adapter, REG_RL, RetryLimit << RETRY_LIMIT_SHORT_SHIFT | RetryLimit << RETRY_LIMIT_LONG_SHIFT);

		/*if((BuddyAdapter != NULL) && (pHalData->MacPhyMode92D == DUALMAC_SINGLEPHY))
		{
			if(ACTING_AS_AP(BuddyAdapter) ||ACTING_AS_AP(ADJUST_TO_ADAPTIVE_ADAPTER(BuddyAdapter, FALSE)) || BuddyAdapter->MgntInfo.mIbss)
				ResumeTxBeacon(BuddyAdapter);
			
			PlatformEFIOWrite2Byte(BuddyAdapter, REG_RL,
					pHalData->ShortRetryLimit << RETRY_LIMIT_SHORT_SHIFT | \
					pHalData->LongRetryLimit << RETRY_LIMIT_LONG_SHIFT);
		}*/
	}
}


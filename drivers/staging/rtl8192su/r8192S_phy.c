/******************************************************************************

     (c) Copyright 2008, RealTEK Technologies Inc. All Rights Reserved.

 Module:	hal8192sphy.c

 Note:		Merge 92SE/SU PHY config as below
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
#include "r8192U.h"
#include "r8192U_dm.h"
#include "r8192S_rtl6052.h"

#include "r8192S_hw.h"
#include "r8192S_phy.h"
#include "r8192S_phyreg.h"
#include "r8192SU_HWImg.h"

#include "ieee80211/dot11d.h"

/*---------------------------Define Local Constant---------------------------*/
/* Channel switch:The size of command tables for switch channel*/
#define MAX_PRECMD_CNT 16
#define MAX_RFDEPENDCMD_CNT 16
#define MAX_POSTCMD_CNT 16
#define MAX_DOZE_WAITING_TIMES_9x 64

/*------------------------Define local variable------------------------------*/
// 2004-05-11

static	u32
phy_CalculateBitShift(u32 BitMask);
static	RT_STATUS
phy_ConfigMACWithHeaderFile(struct net_device* dev);
static void
phy_InitBBRFRegisterDefinition(struct net_device* dev);
static	RT_STATUS
phy_BB8192S_Config_ParaFile(struct net_device* dev);
static	RT_STATUS
phy_ConfigBBWithHeaderFile(struct net_device* dev,u8 ConfigType);
static bool
phy_SetRFPowerState8192SU(struct net_device* dev,RT_RF_POWER_STATE eRFPowerState);
void
SetBWModeCallback8192SUsbWorkItem(struct net_device *dev);
void
SetBWModeCallback8192SUsbWorkItem(struct net_device *dev);
void
SwChnlCallback8192SUsbWorkItem(struct net_device *dev );
static void
phy_FinishSwChnlNow(struct net_device* dev,u8 channel);
static bool
phy_SwChnlStepByStep(
	struct net_device* dev,
	u8		channel,
	u8		*stage,
	u8		*step,
	u32		*delay
	);
static RT_STATUS
phy_ConfigBBWithPgHeaderFile(struct net_device* dev,u8 ConfigType);
static long phy_TxPwrIdxToDbm( struct net_device* dev, WIRELESS_MODE   WirelessMode, u8 TxPwrIdx);
static u8 phy_DbmToTxPwrIdx( struct net_device* dev, WIRELESS_MODE WirelessMode, long PowerInDbm);
void phy_SetFwCmdIOCallback(struct net_device* dev);

//#if ((HAL_CODE_BASE == RTL8192_S) && (DEV_BUS_TYPE==USB_INTERFACE))
//
// Description:
//	Base Band read by 4181 to make sure that operation could be done in unlimited cycle.
//
// Assumption:
//		-	Only use on RTL8192S USB interface.
//		-	PASSIVE LEVEL
//
// Created by Roger, 2008.09.06.
//
//use in phy only
u32 phy_QueryUsbBBReg(struct net_device* dev, u32	RegAddr)
{
	struct r8192_priv *priv = ieee80211_priv(dev);
	u32	ReturnValue = 0xffffffff;
	u8	PollingCnt = 50;
	u8	BBWaitCounter = 0;


	//
	// <Roger_Notes> Due to PASSIVE_LEVEL, so we ONLY simply busy waiting for a while here.
	// We have to make sure that previous BB I/O has been done.
	// 2008.08.20.
	//
	while(priv->bChangeBBInProgress)
	{
		BBWaitCounter ++;
		RT_TRACE(COMP_RF, "phy_QueryUsbBBReg(): Wait 1 ms (%d times)...\n", BBWaitCounter);
		msleep(1); // 1 ms

		// Wait too long, return FALSE to avoid to be stuck here.
		if((BBWaitCounter > 100) )//||RT_USB_CANNOT_IO(Adapter))
		{
			RT_TRACE(COMP_RF, "phy_QueryUsbBBReg(): (%d) Wait too logn to query BB!!\n", BBWaitCounter);
			return ReturnValue;
		}
	}

	priv->bChangeBBInProgress = true;

	read_nic_dword(dev, RegAddr);

	do
	{// Make sure that access could be done.
		if((read_nic_byte(dev, PHY_REG)&HST_RDBUSY) == 0)
			break;
	}while( --PollingCnt );

	if(PollingCnt == 0)
	{
		RT_TRACE(COMP_RF, "Fail!!!phy_QueryUsbBBReg(): RegAddr(%#x) = %#x\n", RegAddr, ReturnValue);
	}
	else
	{
		// Data FW read back.
		ReturnValue = read_nic_dword(dev, PHY_REG_DATA);
		RT_TRACE(COMP_RF, "phy_QueryUsbBBReg(): RegAddr(%#x) = %#x, PollingCnt(%d)\n", RegAddr, ReturnValue, PollingCnt);
	}

	priv->bChangeBBInProgress = false;

	return ReturnValue;
}



//
// Description:
//	Base Band wrote by 4181 to make sure that operation could be done in unlimited cycle.
//
// Assumption:
//		-	Only use on RTL8192S USB interface.
//		-	PASSIVE LEVEL
//
// Created by Roger, 2008.09.06.
//
//use in phy only
void
phy_SetUsbBBReg(struct net_device* dev,u32	RegAddr,u32 Data)
{
	struct r8192_priv *priv = ieee80211_priv(dev);
	u8	BBWaitCounter = 0;

	RT_TRACE(COMP_RF, "phy_SetUsbBBReg(): RegAddr(%#x) <= %#x\n", RegAddr, Data);

	//
	// <Roger_Notes> Due to PASSIVE_LEVEL, so we ONLY simply busy waiting for a while here.
	// We have to make sure that previous BB I/O has been done.
	// 2008.08.20.
	//
	while(priv->bChangeBBInProgress)
	{
		BBWaitCounter ++;
		RT_TRACE(COMP_RF, "phy_SetUsbBBReg(): Wait 1 ms (%d times)...\n", BBWaitCounter);
		msleep(1); // 1 ms

		if((BBWaitCounter > 100))// || RT_USB_CANNOT_IO(Adapter))
		{
			RT_TRACE(COMP_RF, "phy_SetUsbBBReg(): (%d) Wait too logn to query BB!!\n", BBWaitCounter);
			return;
		}
	}

	priv->bChangeBBInProgress = true;
	//printk("**************%s: RegAddr:%x Data:%x\n", __FUNCTION__,RegAddr, Data);
	write_nic_dword(dev, RegAddr, Data);

	priv->bChangeBBInProgress = false;
}



//
// Description:
//	RF read by 4181 to make sure that operation could be done in unlimited cycle.
//
// Assumption:
//		-	Only use on RTL8192S USB interface.
//		-	PASSIVE LEVEL
//		- 	RT_RF_OPERATE_SPINLOCK is acquired and keep on holding to the end.FIXLZM
//
// Created by Roger, 2008.09.06.
//
//use in phy only
u32 phy_QueryUsbRFReg(	struct net_device* dev, RF90_RADIO_PATH_E eRFPath,	u32	Offset)
{

	struct r8192_priv *priv = ieee80211_priv(dev);
	//u32	value  = 0, ReturnValue = 0;
	u32	ReturnValue = 0;
	//u32 	tmplong,tmplong2;
	u8	PollingCnt = 50;
	u8	RFWaitCounter = 0;


	//
	// <Roger_Notes> Due to PASSIVE_LEVEL, so we ONLY simply busy waiting for a while here.
	// We have to make sure that previous RF I/O has been done.
	// 2008.08.20.
	//
	while(priv->bChangeRFInProgress)
	{
		//PlatformReleaseSpinLock(Adapter, RT_RF_OPERATE_SPINLOCK);
		//spin_lock_irqsave(&priv->rf_lock, flags);	//LZM,090318
		down(&priv->rf_sem);

		RFWaitCounter ++;
		RT_TRACE(COMP_RF, "phy_QueryUsbRFReg(): Wait 1 ms (%d times)...\n", RFWaitCounter);
		msleep(1); // 1 ms

		if((RFWaitCounter > 100)) //|| RT_USB_CANNOT_IO(Adapter))
		{
			RT_TRACE(COMP_RF, "phy_QueryUsbRFReg(): (%d) Wait too logn to query BB!!\n", RFWaitCounter);
			return 0xffffffff;
		}
		else
		{
			//PlatformAcquireSpinLock(Adapter, RT_RF_OPERATE_SPINLOCK);
		}
	}

	priv->bChangeRFInProgress = true;
	//PlatformReleaseSpinLock(Adapter, RT_RF_OPERATE_SPINLOCK);


	Offset &= 0x3f; //RF_Offset= 0x00~0x3F

	write_nic_dword(dev, RF_BB_CMD_ADDR, 0xF0000002|
						(Offset<<8)|	//RF_Offset= 0x00~0x3F
						(eRFPath<<16)); 	//RF_Path = 0(A) or 1(B)

	do
	{// Make sure that access could be done.
		if(read_nic_dword(dev, RF_BB_CMD_ADDR) == 0)
			break;
	}while( --PollingCnt );

	// Data FW read back.
	ReturnValue = read_nic_dword(dev, RF_BB_CMD_DATA);

	//PlatformAcquireSpinLock(Adapter, RT_RF_OPERATE_SPINLOCK);
	//spin_unlock_irqrestore(&priv->rf_lock, flags);   //LZM,090318
	up(&priv->rf_sem);
	priv->bChangeRFInProgress = false;

	RT_TRACE(COMP_RF, "phy_QueryUsbRFReg(): eRFPath(%d), Offset(%#x) = %#x\n", eRFPath, Offset, ReturnValue);

	return ReturnValue;

}


//
// Description:
//	RF wrote by 4181 to make sure that operation could be done in unlimited cycle.
//
// Assumption:
//		-	Only use on RTL8192S USB interface.
//		-	PASSIVE LEVEL
//		- 	RT_RF_OPERATE_SPINLOCK is acquired and keep on holding to the end.FIXLZM
//
// Created by Roger, 2008.09.06.
//
//use in phy only
void phy_SetUsbRFReg(struct net_device* dev,RF90_RADIO_PATH_E eRFPath,u32	RegAddr,u32 Data)
{

	struct r8192_priv *priv = ieee80211_priv(dev);
	u8	PollingCnt = 50;
	u8	RFWaitCounter = 0;


	//
	// <Roger_Notes> Due to PASSIVE_LEVEL, so we ONLY simply busy waiting for a while here.
	// We have to make sure that previous BB I/O has been done.
	// 2008.08.20.
	//
	while(priv->bChangeRFInProgress)
	{
		//PlatformReleaseSpinLock(Adapter, RT_RF_OPERATE_SPINLOCK);
		//spin_lock_irqsave(&priv->rf_lock, flags);	//LZM,090318
		down(&priv->rf_sem);

		RFWaitCounter ++;
		RT_TRACE(COMP_RF, "phy_SetUsbRFReg(): Wait 1 ms (%d times)...\n", RFWaitCounter);
		msleep(1); // 1 ms

		if((RFWaitCounter > 100))// || RT_USB_CANNOT_IO(Adapter))
		{
			RT_TRACE(COMP_RF, "phy_SetUsbRFReg(): (%d) Wait too logn to query BB!!\n", RFWaitCounter);
			return;
		}
		else
		{
			//PlatformAcquireSpinLock(Adapter, RT_RF_OPERATE_SPINLOCK);
		}
	}

	priv->bChangeRFInProgress = true;
	//PlatformReleaseSpinLock(Adapter, RT_RF_OPERATE_SPINLOCK);


	RegAddr &= 0x3f; //RF_Offset= 0x00~0x3F

	write_nic_dword(dev, RF_BB_CMD_DATA, Data);
	write_nic_dword(dev, RF_BB_CMD_ADDR, 0xF0000003|
					(RegAddr<<8)| //RF_Offset= 0x00~0x3F
					(eRFPath<<16));  //RF_Path = 0(A) or 1(B)

	do
	{// Make sure that access could be done.
		if(read_nic_dword(dev, RF_BB_CMD_ADDR) == 0)
				break;
	}while( --PollingCnt );

	if(PollingCnt == 0)
	{
		RT_TRACE(COMP_RF, "phy_SetUsbRFReg(): Set RegAddr(%#x) = %#x Fail!!!\n", RegAddr, Data);
	}

	//PlatformAcquireSpinLock(Adapter, RT_RF_OPERATE_SPINLOCK);
	//spin_unlock_irqrestore(&priv->rf_lock, flags);   //LZM,090318
	up(&priv->rf_sem);
	priv->bChangeRFInProgress = false;

}


/*---------------------Define local function prototype-----------------------*/


/*----------------------------Function Body----------------------------------*/
//
// 1. BB register R/W API
//
/**
* Function:	PHY_QueryBBReg
*
* OverView:	Read "sepcific bits" from BB register
*
* Input:
*			PADAPTER		Adapter,
*			u32			RegAddr,		//The target address to be readback
*			u32			BitMask		//The target bit position in the target address
*										//to be readback
* Output:	None
* Return:		u32			Data			//The readback register value
* Note:		This function is equal to "GetRegSetting" in PHY programming guide
*/
//use phy dm core 8225 8256 6052
//u32 PHY_QueryBBReg(struct net_device* dev,u32		RegAddr,	u32		BitMask)
u32 rtl8192_QueryBBReg(struct net_device* dev, u32 RegAddr, u32 BitMask)
{

  	u32	ReturnValue = 0, OriginalValue, BitShift;


	RT_TRACE(COMP_RF, "--->PHY_QueryBBReg(): RegAddr(%#x), BitMask(%#x)\n", RegAddr, BitMask);

	//
	// <Roger_Notes> Due to 8051 operation cycle (limitation cycle: 6us) and 1-Byte access issue, we should use
	// 4181 to access Base Band instead of 8051 on USB interface to make sure that access could be done in
	// infinite cycle.
	// 2008.09.06.
	//
//#if ((HAL_CODE_BASE == RTL8192_S) && (DEV_BUS_TYPE==USB_INTERFACE))
	if(IS_BB_REG_OFFSET_92S(RegAddr))
	{
		//if(RT_USB_CANNOT_IO(Adapter))	return	FALSE;

		if((RegAddr & 0x03) != 0)
		{
			printk("%s: Not DWORD alignment!!\n", __FUNCTION__);
			return 0;
		}

	OriginalValue = phy_QueryUsbBBReg(dev, RegAddr);
	}
	else
	{
	OriginalValue = read_nic_dword(dev, RegAddr);
	}

	BitShift = phy_CalculateBitShift(BitMask);
	ReturnValue = (OriginalValue & BitMask) >> BitShift;

	//RTPRINT(FPHY, PHY_BBR, ("BBR MASK=0x%x Addr[0x%x]=0x%x\n", BitMask, RegAddr, OriginalValue));
	RT_TRACE(COMP_RF, "<---PHY_QueryBBReg(): RegAddr(%#x), BitMask(%#x), OriginalValue(%#x)\n", RegAddr, BitMask, OriginalValue);
	return (ReturnValue);
}

/**
* Function:	PHY_SetBBReg
*
* OverView:	Write "Specific bits" to BB register (page 8~)
*
* Input:
*			PADAPTER		Adapter,
*			u32			RegAddr,		//The target address to be modified
*			u32			BitMask		//The target bit position in the target address
*										//to be modified
*			u32			Data			//The new register value in the target bit position
*										//of the target address
*
* Output:	None
* Return:		None
* Note:		This function is equal to "PutRegSetting" in PHY programming guide
*/
//use phy dm core 8225 8256
//void PHY_SetBBReg(struct net_device* dev,u32		RegAddr,	u32		BitMask,	u32		Data	)
void rtl8192_setBBreg(struct net_device* dev, u32 RegAddr, u32 BitMask, u32 Data)
{
	u32	OriginalValue, BitShift, NewValue;


	RT_TRACE(COMP_RF, "--->PHY_SetBBReg(): RegAddr(%#x), BitMask(%#x), Data(%#x)\n", RegAddr, BitMask, Data);

	//
	// <Roger_Notes> Due to 8051 operation cycle (limitation cycle: 6us) and 1-Byte access issue, we should use
	// 4181 to access Base Band instead of 8051 on USB interface to make sure that access could be done in
	// infinite cycle.
	// 2008.09.06.
	//
//#if ((HAL_CODE_BASE == RTL8192_S) && (DEV_BUS_TYPE==USB_INTERFACE))
	if(IS_BB_REG_OFFSET_92S(RegAddr))
	{
		if((RegAddr & 0x03) != 0)
		{
			printk("%s: Not DWORD alignment!!\n", __FUNCTION__);
			return;
		}

		if(BitMask!= bMaskDWord)
		{//if not "double word" write
			OriginalValue = phy_QueryUsbBBReg(dev, RegAddr);
			BitShift = phy_CalculateBitShift(BitMask);
            NewValue = (((OriginalValue) & (~BitMask))|(Data << BitShift));
			phy_SetUsbBBReg(dev, RegAddr, NewValue);
		}else
			phy_SetUsbBBReg(dev, RegAddr, Data);
	}
	else
	{
		if(BitMask!= bMaskDWord)
		{//if not "double word" write
			OriginalValue = read_nic_dword(dev, RegAddr);
			BitShift = phy_CalculateBitShift(BitMask);
			NewValue = (((OriginalValue) & (~BitMask)) | (Data << BitShift));
			write_nic_dword(dev, RegAddr, NewValue);
		}else
			write_nic_dword(dev, RegAddr, Data);
	}

	//RT_TRACE(COMP_RF, "<---PHY_SetBBReg(): RegAddr(%#x), BitMask(%#x), Data(%#x)\n", RegAddr, BitMask, Data);

	return;
}


//
// 2. RF register R/W API
//
/**
* Function:	PHY_QueryRFReg
*
* OverView:	Query "Specific bits" to RF register (page 8~)
*
* Input:
*			PADAPTER		Adapter,
*			RF90_RADIO_PATH_E	eRFPath,	//Radio path of A/B/C/D
*			u32			RegAddr,		//The target address to be read
*			u32			BitMask		//The target bit position in the target address
*										//to be read
*
* Output:	None
* Return:		u32			Readback value
* Note:		This function is equal to "GetRFRegSetting" in PHY programming guide
*/
//in dm 8256 and phy
//u32 PHY_QueryRFReg(struct net_device* dev,	RF90_RADIO_PATH_E eRFPath, u32 RegAddr, u32 BitMask)
u32 rtl8192_phy_QueryRFReg(struct net_device* dev, RF90_RADIO_PATH_E eRFPath, u32 RegAddr, u32 BitMask)
{
	u32 Original_Value, Readback_Value, BitShift;//, flags;
	struct r8192_priv *priv = ieee80211_priv(dev);


	RT_TRACE(COMP_RF, "--->PHY_QueryRFReg(): RegAddr(%#x), eRFPath(%#x), BitMask(%#x)\n", RegAddr, eRFPath,BitMask);

	if (!((priv->rf_pathmap >> eRFPath) & 0x1))
	{
		printk("EEEEEError: rfpath off! rf_pathmap=%x eRFPath=%x\n", priv->rf_pathmap, eRFPath);
		return 0;
	}

	if (!rtl8192_phy_CheckIsLegalRFPath(dev, eRFPath))
	{
		printk("EEEEEError: not legal rfpath! eRFPath=%x\n", eRFPath);
		return 0;
	}

	/* 2008/01/17 MH We get and release spin lock when reading RF register. */
	//PlatformAcquireSpinLock(dev, RT_RF_OPERATE_SPINLOCK);FIXLZM
	//spin_lock_irqsave(&priv->rf_lock, flags);	//YJ,test,090113
	down(&priv->rf_sem);
	//
	// <Roger_Notes> Due to 8051 operation cycle (limitation cycle: 6us) and 1-Byte access issue, we should use
	// 4181 to access Base Band instead of 8051 on USB interface to make sure that access could be done in
	// infinite cycle.
	// 2008.09.06.
	//
//#if (HAL_CODE_BASE == RTL8192_S && DEV_BUS_TYPE==USB_INTERFACE)
	//if(RT_USB_CANNOT_IO(Adapter))	return FALSE;
	Original_Value = phy_QueryUsbRFReg(dev, eRFPath, RegAddr);

	BitShift =  phy_CalculateBitShift(BitMask);
	Readback_Value = (Original_Value & BitMask) >> BitShift;
	//spin_unlock_irqrestore(&priv->rf_lock, flags);   //YJ,test,090113
	up(&priv->rf_sem);
	//PlatformReleaseSpinLock(dev, RT_RF_OPERATE_SPINLOCK);

	//RTPRINT(FPHY, PHY_RFR, ("RFR-%d MASK=0x%x Addr[0x%x]=0x%x\n", eRFPath, BitMask, RegAddr, Original_Value));

	return (Readback_Value);
}

/**
* Function:	PHY_SetRFReg
*
* OverView:	Write "Specific bits" to RF register (page 8~)
*
* Input:
*			PADAPTER		Adapter,
*			RF90_RADIO_PATH_E	eRFPath,	//Radio path of A/B/C/D
*			u32			RegAddr,		//The target address to be modified
*			u32			BitMask		//The target bit position in the target address
*										//to be modified
*			u32			Data			//The new register Data in the target bit position
*										//of the target address
*
* Output:	None
* Return:		None
* Note:		This function is equal to "PutRFRegSetting" in PHY programming guide
*/
//use phy  8225 8256
//void PHY_SetRFReg(struct net_device* dev,RF90_RADIO_PATH_E eRFPath, u32	RegAddr,	u32 BitMask,u32	Data	)
void rtl8192_phy_SetRFReg(struct net_device* dev, RF90_RADIO_PATH_E eRFPath, u32 RegAddr, u32 BitMask, u32 Data)
{

	struct r8192_priv *priv = ieee80211_priv(dev);
	u32 Original_Value, BitShift, New_Value;//, flags;

	RT_TRACE(COMP_RF, "--->PHY_SetRFReg(): RegAddr(%#x), BitMask(%#x), Data(%#x), eRFPath(%#x)\n",
		RegAddr, BitMask, Data, eRFPath);

	if (!((priv->rf_pathmap >> eRFPath) & 0x1))
	{
		printk("EEEEEError: rfpath off! rf_pathmap=%x eRFPath=%x\n", priv->rf_pathmap, eRFPath);
		return ;
	}
	if (!rtl8192_phy_CheckIsLegalRFPath(dev, eRFPath))
	{
		printk("EEEEEError: not legal rfpath! eRFPath=%x\n", eRFPath);
		return;
	}

	/* 2008/01/17 MH We get and release spin lock when writing RF register. */
	//PlatformAcquireSpinLock(dev, RT_RF_OPERATE_SPINLOCK);
	//spin_lock_irqsave(&priv->rf_lock, flags);	//YJ,test,090113
	down(&priv->rf_sem);
	//
	// <Roger_Notes> Due to 8051 operation cycle (limitation cycle: 6us) and 1-Byte access issue, we should use
	// 4181 to access Base Band instead of 8051 on USB interface to make sure that access could be done in
	// infinite cycle.
	// 2008.09.06.
	//
//#if (HAL_CODE_BASE == RTL8192_S && DEV_BUS_TYPE==USB_INTERFACE)
		//if(RT_USB_CANNOT_IO(Adapter))	return;

		if (BitMask != bRFRegOffsetMask) // RF data is 12 bits only
		{
			Original_Value = phy_QueryUsbRFReg(dev, eRFPath, RegAddr);
			BitShift =  phy_CalculateBitShift(BitMask);
			New_Value = (((Original_Value)&(~BitMask))|(Data<< BitShift));
			phy_SetUsbRFReg(dev, eRFPath, RegAddr, New_Value);
		}
		else
			phy_SetUsbRFReg(dev, eRFPath, RegAddr, Data);
	//PlatformReleaseSpinLock(dev, RT_RF_OPERATE_SPINLOCK);
	//spin_unlock_irqrestore(&priv->rf_lock, flags);   //YJ,test,090113
	up(&priv->rf_sem);
	//RTPRINT(FPHY, PHY_RFW, ("RFW-%d MASK=0x%x Addr[0x%x]=0x%x\n", eRFPath, BitMask, RegAddr, Data));
	RT_TRACE(COMP_RF, "<---PHY_SetRFReg(): RegAddr(%#x), BitMask(%#x), Data(%#x), eRFPath(%#x)\n",
			RegAddr, BitMask, Data, eRFPath);

}

/**
* Function:	phy_CalculateBitShift
*
* OverView:	Get shifted position of the BitMask
*
* Input:
*			u32		BitMask,
*
* Output:	none
* Return:		u32		Return the shift bit bit position of the mask
*/
//use in phy only
static u32 phy_CalculateBitShift(u32 BitMask)
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
// 3. Initial MAC/BB/RF config by reading MAC/BB/RF txt.
//
/*-----------------------------------------------------------------------------
 * Function:    PHY_MACConfig8192S
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
//adapter_start
extern bool PHY_MACConfig8192S(struct net_device* dev)
{
	RT_STATUS		rtStatus = RT_STATUS_SUCCESS;

	//
	// Config MAC
	//
	rtStatus = phy_ConfigMACWithHeaderFile(dev);
	return (rtStatus == RT_STATUS_SUCCESS) ? true:false;

}

//adapter_start
extern	bool
PHY_BBConfig8192S(struct net_device* dev)
{
	RT_STATUS	rtStatus = RT_STATUS_SUCCESS;

	u8 PathMap = 0, index = 0, rf_num = 0;
	struct r8192_priv 	*priv = ieee80211_priv(dev);
	phy_InitBBRFRegisterDefinition(dev);

	//
	// Config BB and AGC
	//
	//switch( Adapter->MgntInfo.bRegHwParaFile )
	//{
	//	case 0:
	//		phy_BB8190_Config_HardCode(dev);
	//		break;

	//	case 1:
			rtStatus = phy_BB8192S_Config_ParaFile(dev);
	//		break;

	//	case 2:
			// Partial Modify.
	//		phy_BB8190_Config_HardCode(dev);
	//		phy_BB8192S_Config_ParaFile(dev);
	//		break;

	//	default:
	//		phy_BB8190_Config_HardCode(dev);
	//		break;
	//}
	PathMap = (u8)(rtl8192_QueryBBReg(dev, rFPGA0_TxInfo, 0xf) |
				rtl8192_QueryBBReg(dev, rOFDM0_TRxPathEnable, 0xf));
	priv->rf_pathmap = PathMap;
	for(index = 0; index<4; index++)
	{
		if((PathMap>>index)&0x1)
			rf_num++;
	}

	if((priv->rf_type==RF_1T1R && rf_num!=1) ||
		(priv->rf_type==RF_1T2R && rf_num!=2) ||
		(priv->rf_type==RF_2T2R && rf_num!=2) ||
		(priv->rf_type==RF_2T2R_GREEN && rf_num!=2) ||
		(priv->rf_type==RF_2T4R && rf_num!=4))
	{
		RT_TRACE( COMP_INIT, "PHY_BBConfig8192S: RF_Type(%x) does not match RF_Num(%x)!!\n", priv->rf_type, rf_num);
	}
	return (rtStatus == RT_STATUS_SUCCESS) ? 1:0;
}

//adapter_start
extern	bool
PHY_RFConfig8192S(struct net_device* dev)
{
	struct r8192_priv 	*priv = ieee80211_priv(dev);
	RT_STATUS    		rtStatus = RT_STATUS_SUCCESS;

	//Set priv->rf_chip = RF_8225 to do real PHY FPGA initilization

	//<Roger_EXP> We assign RF type here temporally. 2008.09.12.
	priv->rf_chip = RF_6052;

	//
	// RF config
	//
	switch(priv->rf_chip)
	{
	case RF_8225:
	case RF_6052:
		rtStatus = PHY_RF6052_Config(dev);
		break;

	case RF_8256:
		//rtStatus = PHY_RF8256_Config(dev);
		break;

	case RF_8258:
		break;

	case RF_PSEUDO_11N:
		//rtStatus = PHY_RF8225_Config(dev);
		break;
        default:
            break;
	}

	return (rtStatus == RT_STATUS_SUCCESS) ? 1:0;
}


// Joseph test: new initialize order!!
// Test only!! This part need to be re-organized.
// Now it is just for 8256.
//use in phy only
#ifdef TO_DO_LIST
static RT_STATUS
phy_BB8190_Config_HardCode(struct net_device* dev)
{
	//RT_ASSERT(FALSE, ("This function is not implement yet!! \n"));
	return RT_STATUS_SUCCESS;
}
#endif

/*-----------------------------------------------------------------------------
 * Function:    phy_SetBBtoDiffRFWithHeaderFile()
 *
 * Overview:    This function
 *
 *
 * Input:      	PADAPTER		Adapter
 *			u1Byte 			ConfigType     0 => PHY_CONFIG
 *
 * Output:      NONE
 *
 * Return:      RT_STATUS_SUCCESS: configuration file exist
 * When			Who		Remark
 * 2008/11/10	tynli
 * use in phy only
 *---------------------------------------------------------------------------*/
static RT_STATUS
phy_SetBBtoDiffRFWithHeaderFile(struct net_device* dev, u8 ConfigType)
{
	int i;
	struct r8192_priv 	*priv = ieee80211_priv(dev);
	u32* 			Rtl819XPHY_REGArraytoXTXR_Table;
	u16				PHY_REGArraytoXTXRLen;

//#if (HAL_CODE_BASE != RTL8192_S)

	if(priv->rf_type == RF_1T1R)
	{
		Rtl819XPHY_REGArraytoXTXR_Table = Rtl819XPHY_REG_to1T1R_Array;
		PHY_REGArraytoXTXRLen = PHY_ChangeTo_1T1RArrayLength;
	}
	else if(priv->rf_type == RF_1T2R)
	{
		Rtl819XPHY_REGArraytoXTXR_Table = Rtl819XPHY_REG_to1T2R_Array;
		PHY_REGArraytoXTXRLen = PHY_ChangeTo_1T2RArrayLength;
	}
	//else if(priv->rf_type == RF_2T2R || priv->rf_type == RF_2T2R_GREEN)
	//{
	//	Rtl819XPHY_REGArraytoXTXR_Table = Rtl819XPHY_REG_to2T2R_Array;
	//	PHY_REGArraytoXTXRLen = PHY_ChangeTo_2T2RArrayLength;
	//}
	else
	{
		return RT_STATUS_FAILURE;
	}

	if(ConfigType == BaseBand_Config_PHY_REG)
	{
		for(i=0;i<PHY_REGArraytoXTXRLen;i=i+3)
		{
			if (Rtl819XPHY_REGArraytoXTXR_Table[i] == 0xfe)
				mdelay(50);
			else if (Rtl819XPHY_REGArraytoXTXR_Table[i] == 0xfd)
				mdelay(5);
			else if (Rtl819XPHY_REGArraytoXTXR_Table[i] == 0xfc)
				mdelay(1);
			else if (Rtl819XPHY_REGArraytoXTXR_Table[i] == 0xfb)
				udelay(50);
			else if (Rtl819XPHY_REGArraytoXTXR_Table[i] == 0xfa)
				udelay(5);
			else if (Rtl819XPHY_REGArraytoXTXR_Table[i] == 0xf9)
				udelay(1);
			rtl8192_setBBreg(dev, Rtl819XPHY_REGArraytoXTXR_Table[i], Rtl819XPHY_REGArraytoXTXR_Table[i+1], Rtl819XPHY_REGArraytoXTXR_Table[i+2]);
			//RT_TRACE(COMP_SEND,
			//"The Rtl819XPHY_REGArraytoXTXR_Table[0] is %lx Rtl819XPHY_REGArraytoXTXR_Table[1] is %lx Rtl819XPHY_REGArraytoXTXR_Table[2] is %lx \n",
			//Rtl819XPHY_REGArraytoXTXR_Table[i],Rtl819XPHY_REGArraytoXTXR_Table[i+1], Rtl819XPHY_REGArraytoXTXR_Table[i+2]);
		}
	}
	else {
		RT_TRACE(COMP_SEND, "phy_SetBBtoDiffRFWithHeaderFile(): ConfigType != BaseBand_Config_PHY_REG\n");
	}
//#endif	// #if (HAL_CODE_BASE != RTL8192_S)
	return RT_STATUS_SUCCESS;
}


//use in phy only
static	RT_STATUS
phy_BB8192S_Config_ParaFile(struct net_device* dev)
{
	struct r8192_priv 	*priv = ieee80211_priv(dev);
	RT_STATUS			rtStatus = RT_STATUS_SUCCESS;
	//u8				u2RegValue;
	//u16				u4RegValue;
	//char				szBBRegFile[] = RTL819X_PHY_REG;
	//char				szBBRegFile1T2R[] = RTL819X_PHY_REG_1T2R;
	//char				szBBRegPgFile[] = RTL819X_PHY_REG_PG;
	//char				szAGCTableFile[] = RTL819X_AGC_TAB;
	//char				szBBRegto1T1RFile[] = RTL819X_PHY_REG_to1T1R;
	//char				szBBRegto1T2RFile[] = RTL819X_PHY_REG_to1T2R;

	RT_TRACE(COMP_INIT, "==>phy_BB8192S_Config_ParaFile\n");

	//
	// 1. Read PHY_REG.TXT BB INIT!!
	// We will seperate as 1T1R/1T2R/1T2R_GREEN/2T2R
	//
	if (priv->rf_type == RF_1T2R || priv->rf_type == RF_2T2R ||
	    priv->rf_type == RF_1T1R ||priv->rf_type == RF_2T2R_GREEN)
	{
		rtStatus = phy_ConfigBBWithHeaderFile(dev,BaseBand_Config_PHY_REG);
		if(priv->rf_type != RF_2T2R && priv->rf_type != RF_2T2R_GREEN)
		{//2008.11.10 Added by tynli. The default PHY_REG.txt we read is for 2T2R,
		  //so we should reconfig BB reg with the right PHY parameters.
			rtStatus = phy_SetBBtoDiffRFWithHeaderFile(dev,BaseBand_Config_PHY_REG);
		}
	}else
		rtStatus = RT_STATUS_FAILURE;

	if(rtStatus != RT_STATUS_SUCCESS){
		RT_TRACE(COMP_INIT, "phy_BB8192S_Config_ParaFile():Write BB Reg Fail!!");
		goto phy_BB8190_Config_ParaFile_Fail;
	}

	//
	// 2. If EEPROM or EFUSE autoload OK, We must config by PHY_REG_PG.txt
	//
	if (priv->AutoloadFailFlag == false)
	{
		rtStatus = phy_ConfigBBWithPgHeaderFile(dev,BaseBand_Config_PHY_REG);
	}
	if(rtStatus != RT_STATUS_SUCCESS){
		RT_TRACE(COMP_INIT, "phy_BB8192S_Config_ParaFile():BB_PG Reg Fail!!");
		goto phy_BB8190_Config_ParaFile_Fail;
	}

	//
	// 3. BB AGC table Initialization
	//
	rtStatus = phy_ConfigBBWithHeaderFile(dev,BaseBand_Config_AGC_TAB);

	if(rtStatus != RT_STATUS_SUCCESS){
		printk( "phy_BB8192S_Config_ParaFile():AGC Table Fail\n");
		goto phy_BB8190_Config_ParaFile_Fail;
	}


	// Check if the CCK HighPower is turned ON.
	// This is used to calculate PWDB.
	priv->bCckHighPower = (bool)(rtl8192_QueryBBReg(dev, rFPGA0_XA_HSSIParameter2, 0x200));


phy_BB8190_Config_ParaFile_Fail:
	return rtStatus;
}

/*-----------------------------------------------------------------------------
 * Function:    phy_ConfigMACWithHeaderFile()
 *
 * Overview:    This function read BB parameters from Header file we gen, and do register
 *			  Read/Write
 *
 * Input:      	PADAPTER		Adapter
 *			char* 			pFileName
 *
 * Output:      NONE
 *
 * Return:      RT_STATUS_SUCCESS: configuration file exist
 *
 * Note: 		The format of MACPHY_REG.txt is different from PHY and RF.
 *			[Register][Mask][Value]
 *---------------------------------------------------------------------------*/
//use in phy only
static	RT_STATUS
phy_ConfigMACWithHeaderFile(struct net_device* dev)
{
	u32					i = 0;
	u32					ArrayLength = 0;
	u32*					ptrArray;
	//struct r8192_priv 	*priv = ieee80211_priv(dev);

//#if (HAL_CODE_BASE != RTL8192_S)
	/*if(Adapter->bInHctTest)
	{
		RT_TRACE(COMP_INIT, DBG_LOUD, ("Rtl819XMACPHY_ArrayDTM\n"));
		ArrayLength = MACPHY_ArrayLengthDTM;
		ptrArray = Rtl819XMACPHY_ArrayDTM;
	}
	else if(pHalData->bTXPowerDataReadFromEEPORM)
	{
//		RT_TRACE(COMP_INIT, DBG_LOUD, ("Rtl819XMACPHY_Array_PG\n"));
//		ArrayLength = MACPHY_Array_PGLength;
//		ptrArray = Rtl819XMACPHY_Array_PG;

	}else*/
	{ //2008.11.06 Modified by tynli.
		RT_TRACE(COMP_INIT, "Read Rtl819XMACPHY_Array\n");
		ArrayLength = MAC_2T_ArrayLength;
		ptrArray = Rtl819XMAC_Array;
	}

	/*for(i = 0 ;i < ArrayLength;i=i+3){
		RT_TRACE(COMP_SEND, DBG_LOUD, ("The Rtl819XMACPHY_Array[0] is %lx Rtl819XMACPHY_Array[1] is %lx Rtl819XMACPHY_Array[2] is %lx\n",ptrArray[i], ptrArray[i+1], ptrArray[i+2]));
		if(ptrArray[i] == 0x318)
		{
			ptrArray[i+2] = 0x00000800;
			//DbgPrint("ptrArray[i], ptrArray[i+1], ptrArray[i+2] = %x, %x, %x\n",
			//	ptrArray[i], ptrArray[i+1], ptrArray[i+2]);
		}
		PHY_SetBBReg(Adapter, ptrArray[i], ptrArray[i+1], ptrArray[i+2]);
	}*/
	for(i = 0 ;i < ArrayLength;i=i+2){ // Add by tynli for 2 column
		write_nic_byte(dev, ptrArray[i], (u8)ptrArray[i+1]);
	}
//#endif
	return RT_STATUS_SUCCESS;
}

/*-----------------------------------------------------------------------------
 * Function:    phy_ConfigBBWithHeaderFile()
 *
 * Overview:    This function read BB parameters from general file format, and do register
 *			  Read/Write
 *
 * Input:      	PADAPTER		Adapter
 *			u8 			ConfigType     0 => PHY_CONFIG
 *										 1 =>AGC_TAB
 *
 * Output:      NONE
 *
 * Return:      RT_STATUS_SUCCESS: configuration file exist
 *
 *---------------------------------------------------------------------------*/
//use in phy only
static	RT_STATUS
phy_ConfigBBWithHeaderFile(struct net_device* dev,u8 ConfigType)
{
	int 		i;
	//u8 		ArrayLength;
	u32*	Rtl819XPHY_REGArray_Table;
	u32*	Rtl819XAGCTAB_Array_Table;
	u16		PHY_REGArrayLen, AGCTAB_ArrayLen;
	//struct r8192_priv *priv = ieee80211_priv(dev);
//#if (HAL_CODE_BASE != RTL8192_S)
	/*if(Adapter->bInHctTest)
	{

		AGCTAB_ArrayLen = AGCTAB_ArrayLengthDTM;
		Rtl819XAGCTAB_Array_Table = Rtl819XAGCTAB_ArrayDTM;

		if(pHalData->RF_Type == RF_2T4R)
		{
			PHY_REGArrayLen = PHY_REGArrayLengthDTM;
			Rtl819XPHY_REGArray_Table = Rtl819XPHY_REGArrayDTM;
		}
		else if (pHalData->RF_Type == RF_1T2R)
		{
			PHY_REGArrayLen = PHY_REG_1T2RArrayLengthDTM;
			Rtl819XPHY_REGArray_Table = Rtl819XPHY_REG_1T2RArrayDTM;
		}

	}
	else
	*/
	//{
	//
	// 2008.11.06 Modified by tynli.
	//
	AGCTAB_ArrayLen = AGCTAB_ArrayLength;
	Rtl819XAGCTAB_Array_Table = Rtl819XAGCTAB_Array;
	PHY_REGArrayLen = PHY_REG_2T2RArrayLength; // Default RF_type: 2T2R
	Rtl819XPHY_REGArray_Table = Rtl819XPHY_REG_Array;
	//}

	if(ConfigType == BaseBand_Config_PHY_REG)
	{
		for(i=0;i<PHY_REGArrayLen;i=i+2)
		{
			if (Rtl819XPHY_REGArray_Table[i] == 0xfe)
				mdelay(50);
			else if (Rtl819XPHY_REGArray_Table[i] == 0xfd)
				mdelay(5);
			else if (Rtl819XPHY_REGArray_Table[i] == 0xfc)
				mdelay(1);
			else if (Rtl819XPHY_REGArray_Table[i] == 0xfb)
				udelay(50);
			else if (Rtl819XPHY_REGArray_Table[i] == 0xfa)
				udelay(5);
			else if (Rtl819XPHY_REGArray_Table[i] == 0xf9)
				udelay(1);
			rtl8192_setBBreg(dev, Rtl819XPHY_REGArray_Table[i], bMaskDWord, Rtl819XPHY_REGArray_Table[i+1]);
			//RT_TRACE(COMP_SEND, "The Rtl819XPHY_REGArray_Table[0] is %lx Rtl819XPHY_REGArray[1] is %lx \n",Rtl819XPHY_REGArray_Table[i], Rtl819XPHY_REGArray_Table[i+1]);

		}
	}
	else if(ConfigType == BaseBand_Config_AGC_TAB){
		for(i=0;i<AGCTAB_ArrayLen;i=i+2)
		{
			rtl8192_setBBreg(dev, Rtl819XAGCTAB_Array_Table[i], bMaskDWord, Rtl819XAGCTAB_Array_Table[i+1]);
		}
	}
//#endif	// #if (HAL_CODE_BASE != RTL8192_S)
	return RT_STATUS_SUCCESS;
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
 //use in phy only
 *---------------------------------------------------------------------------*/
static RT_STATUS
phy_ConfigBBWithPgHeaderFile(struct net_device* dev,u8 ConfigType)
{
	int i;
	//u8 ArrayLength;
	u32*	Rtl819XPHY_REGArray_Table_PG;
	u16	PHY_REGArrayPGLen;
	//struct r8192_priv *priv = ieee80211_priv(dev);
//#if (HAL_CODE_BASE != RTL8192_S)
	// Default: pHalData->RF_Type = RF_2T2R.

	PHY_REGArrayPGLen = PHY_REG_Array_PGLength;
	Rtl819XPHY_REGArray_Table_PG = Rtl819XPHY_REG_Array_PG;

	if(ConfigType == BaseBand_Config_PHY_REG)
	{
		for(i=0;i<PHY_REGArrayPGLen;i=i+3)
		{
			if (Rtl819XPHY_REGArray_Table_PG[i] == 0xfe)
				mdelay(50);
			else if (Rtl819XPHY_REGArray_Table_PG[i] == 0xfd)
				mdelay(5);
			else if (Rtl819XPHY_REGArray_Table_PG[i] == 0xfc)
				mdelay(1);
			else if (Rtl819XPHY_REGArray_Table_PG[i] == 0xfb)
				udelay(50);
			else if (Rtl819XPHY_REGArray_Table_PG[i] == 0xfa)
				udelay(5);
			else if (Rtl819XPHY_REGArray_Table_PG[i] == 0xf9)
				udelay(1);
			rtl8192_setBBreg(dev, Rtl819XPHY_REGArray_Table_PG[i], Rtl819XPHY_REGArray_Table_PG[i+1], Rtl819XPHY_REGArray_Table_PG[i+2]);
			//RT_TRACE(COMP_SEND, "The Rtl819XPHY_REGArray_Table_PG[0] is %lx Rtl819XPHY_REGArray_Table_PG[1] is %lx \n",
			//		Rtl819XPHY_REGArray_Table_PG[i], Rtl819XPHY_REGArray_Table_PG[i+1]);
		}
	}else{
		RT_TRACE(COMP_SEND, "phy_ConfigBBWithPgHeaderFile(): ConfigType != BaseBand_Config_PHY_REG\n");
	}
	return RT_STATUS_SUCCESS;

}	/* phy_ConfigBBWithPgHeaderFile */

/*-----------------------------------------------------------------------------
 * Function:    PHY_ConfigRFWithHeaderFile()
 *
 * Overview:    This function read RF parameters from general file format, and do RF 3-wire
 *
 * Input:      	PADAPTER			Adapter
 *			char* 				pFileName
 *			RF90_RADIO_PATH_E	eRFPath
 *
 * Output:      NONE
 *
 * Return:      RT_STATUS_SUCCESS: configuration file exist
 *
 * Note:		Delay may be required for RF configuration
 *---------------------------------------------------------------------------*/
//in 8256 phy_RF8256_Config_ParaFile only
//RT_STATUS PHY_ConfigRFWithHeaderFile(struct net_device* dev,RF90_RADIO_PATH_E eRFPath)
u8 rtl8192_phy_ConfigRFWithHeaderFile(struct net_device* dev, RF90_RADIO_PATH_E	eRFPath)
{

	struct r8192_priv *priv = ieee80211_priv(dev);
	int			i;
	//u32*	pRFArray;
	RT_STATUS	rtStatus = RT_STATUS_SUCCESS;
	u32			*Rtl819XRadioA_Array_Table;
	u32			*Rtl819XRadioB_Array_Table;
	//u32*	Rtl819XRadioC_Array_Table;
	//u32*	Rtl819XRadioD_Array_Table;
	u16			RadioA_ArrayLen,RadioB_ArrayLen;

	{	//2008.11.06 Modified by tynli
		RadioA_ArrayLen = RadioA_1T_ArrayLength;
		Rtl819XRadioA_Array_Table=Rtl819XRadioA_Array;
		Rtl819XRadioB_Array_Table=Rtl819XRadioB_Array;
		RadioB_ArrayLen = RadioB_ArrayLength;
	}

	if( priv->rf_type == RF_2T2R_GREEN )
	{
		Rtl819XRadioB_Array_Table = Rtl819XRadioB_GM_Array;
		RadioB_ArrayLen = RadioB_GM_ArrayLength;
	}
	else
	{
		Rtl819XRadioB_Array_Table = Rtl819XRadioB_Array;
		RadioB_ArrayLen = RadioB_ArrayLength;
	}

	rtStatus = RT_STATUS_SUCCESS;

	// When initialization, we want the delay function(mdelay(), delay_us()
	// ==> actually we call PlatformStallExecution()) to do NdisStallExecution()
	// [busy wait] instead of NdisMSleep(). So we acquire RT_INITIAL_SPINLOCK
	// to run at Dispatch level to achive it.
	//cosa PlatformAcquireSpinLock(Adapter, RT_INITIAL_SPINLOCK);

	switch(eRFPath){
		case RF90_PATH_A:
			for(i = 0;i<RadioA_ArrayLen; i=i+2){
				if(Rtl819XRadioA_Array_Table[i] == 0xfe)
					{ // Deay specific ms. Only RF configuration require delay.
//#if (DEV_BUS_TYPE == USB_INTERFACE)
						mdelay(1000);
				}
					else if (Rtl819XRadioA_Array_Table[i] == 0xfd)
						mdelay(5);
					else if (Rtl819XRadioA_Array_Table[i] == 0xfc)
						mdelay(1);
					else if (Rtl819XRadioA_Array_Table[i] == 0xfb)
						udelay(50);
						//PlatformStallExecution(50);
					else if (Rtl819XRadioA_Array_Table[i] == 0xfa)
						udelay(5);
					else if (Rtl819XRadioA_Array_Table[i] == 0xf9)
						udelay(1);
					else
					{
					rtl8192_phy_SetRFReg(dev, eRFPath, Rtl819XRadioA_Array_Table[i], bRFRegOffsetMask, Rtl819XRadioA_Array_Table[i+1]);
					}
			}
			break;
		case RF90_PATH_B:
			for(i = 0;i<RadioB_ArrayLen; i=i+2){
				if(Rtl819XRadioB_Array_Table[i] == 0xfe)
					{ // Deay specific ms. Only RF configuration require delay.
//#if (DEV_BUS_TYPE == USB_INTERFACE)
						mdelay(1000);
				}
					else if (Rtl819XRadioB_Array_Table[i] == 0xfd)
						mdelay(5);
					else if (Rtl819XRadioB_Array_Table[i] == 0xfc)
						mdelay(1);
					else if (Rtl819XRadioB_Array_Table[i] == 0xfb)
						udelay(50);
					else if (Rtl819XRadioB_Array_Table[i] == 0xfa)
						udelay(5);
					else if (Rtl819XRadioB_Array_Table[i] == 0xf9)
						udelay(1);
					else
					{
					rtl8192_phy_SetRFReg(dev, eRFPath, Rtl819XRadioB_Array_Table[i], bRFRegOffsetMask, Rtl819XRadioB_Array_Table[i+1]);
					}
			}
			break;
		case RF90_PATH_C:
			break;
		case RF90_PATH_D:
			break;
		default:
			break;
	}

	return rtStatus;

}

/*-----------------------------------------------------------------------------
 * Function:    PHY_CheckBBAndRFOK()
 *
 * Overview:    This function is write register and then readback to make sure whether
 *			  BB[PHY0, PHY1], RF[Patha, path b, path c, path d] is Ok
 *
 * Input:      	PADAPTER			Adapter
 *			HW90_BLOCK_E		CheckBlock
 *			RF90_RADIO_PATH_E	eRFPath		// it is used only when CheckBlock is HW90_BLOCK_RF
 *
 * Output:      NONE
 *
 * Return:      RT_STATUS_SUCCESS: PHY is OK
 *
 * Note:		This function may be removed in the ASIC
 *---------------------------------------------------------------------------*/
//in 8256 phy_RF8256_Config_HardCode
//but we don't use it temp
RT_STATUS
PHY_CheckBBAndRFOK(
	struct net_device* dev,
	HW90_BLOCK_E		CheckBlock,
	RF90_RADIO_PATH_E	eRFPath
	)
{
	//struct r8192_priv *priv = ieee80211_priv(dev);
	RT_STATUS			rtStatus = RT_STATUS_SUCCESS;
	u32				i, CheckTimes = 4,ulRegRead = 0;
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
			RT_TRACE(COMP_INIT, "PHY_CheckBBRFOK(): Never Write 0x100 here!\n");
			break;

		case HW90_BLOCK_PHY0:
		case HW90_BLOCK_PHY1:
			write_nic_dword(dev, WriteAddr[CheckBlock], WriteData[i]);
			ulRegRead = read_nic_dword(dev, WriteAddr[CheckBlock]);
			break;

		case HW90_BLOCK_RF:
			// When initialization, we want the delay function(mdelay(), delay_us()
			// ==> actually we call PlatformStallExecution()) to do NdisStallExecution()
			// [busy wait] instead of NdisMSleep(). So we acquire RT_INITIAL_SPINLOCK
			// to run at Dispatch level to achive it.
			//cosa PlatformAcquireSpinLock(dev, RT_INITIAL_SPINLOCK);
			WriteData[i] &= 0xfff;
			rtl8192_phy_SetRFReg(dev, eRFPath, WriteAddr[HW90_BLOCK_RF], bRFRegOffsetMask, WriteData[i]);
			// TODO: we should not delay for such a long time. Ask SD3
			mdelay(10);
			ulRegRead = rtl8192_phy_QueryRFReg(dev, eRFPath, WriteAddr[HW90_BLOCK_RF], bMaskDWord);
			mdelay(10);
			//cosa PlatformReleaseSpinLock(dev, RT_INITIAL_SPINLOCK);
			break;

		default:
			rtStatus = RT_STATUS_FAILURE;
			break;
		}


		//
		// Check whether readback data is correct
		//
		if(ulRegRead != WriteData[i])
		{
			//RT_TRACE(COMP_FPGA,  ("ulRegRead: %x, WriteData: %x \n", ulRegRead, WriteData[i]));
			RT_TRACE(COMP_ERR, "read back error(read:%x, write:%x)\n", ulRegRead, WriteData[i]);
			rtStatus = RT_STATUS_FAILURE;
			break;
		}
	}

	return rtStatus;
}

//no use temp in windows driver
#ifdef TO_DO_LIST
void
PHY_SetRFPowerState8192SUsb(
	struct net_device* dev,
	RF_POWER_STATE	RFPowerState
	)
{
	struct r8192_priv *priv = ieee80211_priv(dev);
	bool			WaitShutDown = FALSE;
	u32			DWordContent;
	//RF90_RADIO_PATH_E	eRFPath;
	u8				eRFPath;
	BB_REGISTER_DEFINITION_T	*pPhyReg;

	if(priv->SetRFPowerStateInProgress == TRUE)
		return;

	priv->SetRFPowerStateInProgress = TRUE;

	// TODO: Emily, 2006.11.21, we should rewrite this function

	if(RFPowerState==RF_SHUT_DOWN)
	{
		RFPowerState=RF_OFF;
		WaitShutDown=TRUE;
	}


	priv->RFPowerState = RFPowerState;
	switch( priv->rf_chip )
	{
	case RF_8225:
	case RF_6052:
		switch( RFPowerState )
		{
		case RF_ON:
			break;

		case RF_SLEEP:
			break;

		case RF_OFF:
			break;
		}
		break;

	case RF_8256:
		switch( RFPowerState )
		{
		case RF_ON:
			break;

		case RF_SLEEP:
			break;

		case RF_OFF:
			for(eRFPath=(RF90_RADIO_PATH_E)RF90_PATH_A; eRFPath < RF90_PATH_MAX; eRFPath++)
			{
				if (!rtl8192_phy_CheckIsLegalRFPath(dev, eRFPath))
					continue;

				pPhyReg = &priv->PHYRegDef[eRFPath];
				rtl8192_setBBreg(dev, pPhyReg->rfintfs, bRFSI_RFENV, bRFSI_RFENV);
				rtl8192_setBBreg(dev, pPhyReg->rfintfo, bRFSI_RFENV, 0);
			}
			break;
		}
		break;

	case RF_8258:
		break;
	}// switch( priv->rf_chip )

	priv->SetRFPowerStateInProgress = FALSE;
}
#endif

#ifdef RTL8192U
//no use temp in windows driver
void
PHY_UpdateInitialGain(
	struct net_device* dev
	)
{
	struct r8192_priv 	*priv = ieee80211_priv(dev);
	//unsigned char			*IGTable;
	//u8			DIG_CurrentInitialGain = 4;

	switch(priv->rf_chip)
	{
	case RF_8225:
		break;
	case RF_8256:
		break;
	case RF_8258:
		break;
	case RF_PSEUDO_11N:
		break;
	case RF_6052:
		break;
	default:
		RT_TRACE(COMP_DBG, "PHY_UpdateInitialGain(): unknown rf_chip: %#X\n", priv->rf_chip);
		break;
	}
}
#endif

//YJ,modified,090107
void PHY_GetHWRegOriginalValue(struct net_device* dev)
{
	struct r8192_priv *priv = ieee80211_priv(dev);

	// read tx power offset
	// Simulate 8192
	priv->MCSTxPowerLevelOriginalOffset[0] =
		rtl8192_QueryBBReg(dev, rTxAGC_Rate18_06, bMaskDWord);
	priv->MCSTxPowerLevelOriginalOffset[1] =
		rtl8192_QueryBBReg(dev, rTxAGC_Rate54_24, bMaskDWord);
	priv->MCSTxPowerLevelOriginalOffset[2] =
		rtl8192_QueryBBReg(dev, rTxAGC_Mcs03_Mcs00, bMaskDWord);
	priv->MCSTxPowerLevelOriginalOffset[3] =
		rtl8192_QueryBBReg(dev, rTxAGC_Mcs07_Mcs04, bMaskDWord);
	priv->MCSTxPowerLevelOriginalOffset[4] =
		rtl8192_QueryBBReg(dev, rTxAGC_Mcs11_Mcs08, bMaskDWord);
	priv->MCSTxPowerLevelOriginalOffset[5] =
		rtl8192_QueryBBReg(dev, rTxAGC_Mcs15_Mcs12, bMaskDWord);

	// Read CCK offset
	priv->MCSTxPowerLevelOriginalOffset[6] =
		rtl8192_QueryBBReg(dev, rTxAGC_CCK_Mcs32, bMaskDWord);
	RT_TRACE(COMP_INIT, "Legacy OFDM =%08x/%08x HT_OFDM=%08x/%08x/%08x/%08x\n",
	priv->MCSTxPowerLevelOriginalOffset[0], priv->MCSTxPowerLevelOriginalOffset[1] ,
	priv->MCSTxPowerLevelOriginalOffset[2], priv->MCSTxPowerLevelOriginalOffset[3] ,
	priv->MCSTxPowerLevelOriginalOffset[4], priv->MCSTxPowerLevelOriginalOffset[5] );

	// read rx initial gain
	priv->DefaultInitialGain[0] = rtl8192_QueryBBReg(dev, rOFDM0_XAAGCCore1, bMaskByte0);
	priv->DefaultInitialGain[1] = rtl8192_QueryBBReg(dev, rOFDM0_XBAGCCore1, bMaskByte0);
	priv->DefaultInitialGain[2] = rtl8192_QueryBBReg(dev, rOFDM0_XCAGCCore1, bMaskByte0);
	priv->DefaultInitialGain[3] = rtl8192_QueryBBReg(dev, rOFDM0_XDAGCCore1, bMaskByte0);
	RT_TRACE(COMP_INIT, "Default initial gain (c50=0x%x, c58=0x%x, c60=0x%x, c68=0x%x) \n",
			priv->DefaultInitialGain[0], priv->DefaultInitialGain[1],
			priv->DefaultInitialGain[2], priv->DefaultInitialGain[3]);

	// read framesync
	priv->framesync = rtl8192_QueryBBReg(dev, rOFDM0_RxDetector3, bMaskByte0);
	priv->framesyncC34 = rtl8192_QueryBBReg(dev, rOFDM0_RxDetector2, bMaskDWord);
	RT_TRACE(COMP_INIT, "Default framesync (0x%x) = 0x%x \n",
				rOFDM0_RxDetector3, priv->framesync);
}
//YJ,modified,090107,end



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
//use in phy only
static void phy_InitBBRFRegisterDefinition(	struct net_device* dev)
{
	struct r8192_priv *priv = ieee80211_priv(dev);

	// RF Interface Sowrtware Control
	priv->PHYRegDef[RF90_PATH_A].rfintfs = rFPGA0_XAB_RFInterfaceSW; // 16 LSBs if read 32-bit from 0x870
	priv->PHYRegDef[RF90_PATH_B].rfintfs = rFPGA0_XAB_RFInterfaceSW; // 16 MSBs if read 32-bit from 0x870 (16-bit for 0x872)
	priv->PHYRegDef[RF90_PATH_C].rfintfs = rFPGA0_XCD_RFInterfaceSW;// 16 LSBs if read 32-bit from 0x874
	priv->PHYRegDef[RF90_PATH_D].rfintfs = rFPGA0_XCD_RFInterfaceSW;// 16 MSBs if read 32-bit from 0x874 (16-bit for 0x876)

	// RF Interface Readback Value
	priv->PHYRegDef[RF90_PATH_A].rfintfi = rFPGA0_XAB_RFInterfaceRB; // 16 LSBs if read 32-bit from 0x8E0
	priv->PHYRegDef[RF90_PATH_B].rfintfi = rFPGA0_XAB_RFInterfaceRB;// 16 MSBs if read 32-bit from 0x8E0 (16-bit for 0x8E2)
	priv->PHYRegDef[RF90_PATH_C].rfintfi = rFPGA0_XCD_RFInterfaceRB;// 16 LSBs if read 32-bit from 0x8E4
	priv->PHYRegDef[RF90_PATH_D].rfintfi = rFPGA0_XCD_RFInterfaceRB;// 16 MSBs if read 32-bit from 0x8E4 (16-bit for 0x8E6)

	// RF Interface Output (and Enable)
	priv->PHYRegDef[RF90_PATH_A].rfintfo = rFPGA0_XA_RFInterfaceOE; // 16 LSBs if read 32-bit from 0x860
	priv->PHYRegDef[RF90_PATH_B].rfintfo = rFPGA0_XB_RFInterfaceOE; // 16 LSBs if read 32-bit from 0x864
	priv->PHYRegDef[RF90_PATH_C].rfintfo = rFPGA0_XC_RFInterfaceOE;// 16 LSBs if read 32-bit from 0x868
	priv->PHYRegDef[RF90_PATH_D].rfintfo = rFPGA0_XD_RFInterfaceOE;// 16 LSBs if read 32-bit from 0x86C

	// RF Interface (Output and)  Enable
	priv->PHYRegDef[RF90_PATH_A].rfintfe = rFPGA0_XA_RFInterfaceOE; // 16 MSBs if read 32-bit from 0x860 (16-bit for 0x862)
	priv->PHYRegDef[RF90_PATH_B].rfintfe = rFPGA0_XB_RFInterfaceOE; // 16 MSBs if read 32-bit from 0x864 (16-bit for 0x866)
	priv->PHYRegDef[RF90_PATH_C].rfintfe = rFPGA0_XC_RFInterfaceOE;// 16 MSBs if read 32-bit from 0x86A (16-bit for 0x86A)
	priv->PHYRegDef[RF90_PATH_D].rfintfe = rFPGA0_XD_RFInterfaceOE;// 16 MSBs if read 32-bit from 0x86C (16-bit for 0x86E)

	//Addr of LSSI. Wirte RF register by driver
	priv->PHYRegDef[RF90_PATH_A].rf3wireOffset = rFPGA0_XA_LSSIParameter; //LSSI Parameter
	priv->PHYRegDef[RF90_PATH_B].rf3wireOffset = rFPGA0_XB_LSSIParameter;
	priv->PHYRegDef[RF90_PATH_C].rf3wireOffset = rFPGA0_XC_LSSIParameter;
	priv->PHYRegDef[RF90_PATH_D].rf3wireOffset = rFPGA0_XD_LSSIParameter;

	// RF parameter
	priv->PHYRegDef[RF90_PATH_A].rfLSSI_Select = rFPGA0_XAB_RFParameter;  //BB Band Select
	priv->PHYRegDef[RF90_PATH_B].rfLSSI_Select = rFPGA0_XAB_RFParameter;
	priv->PHYRegDef[RF90_PATH_C].rfLSSI_Select = rFPGA0_XCD_RFParameter;
	priv->PHYRegDef[RF90_PATH_D].rfLSSI_Select = rFPGA0_XCD_RFParameter;

	// Tx AGC Gain Stage (same for all path. Should we remove this?)
	priv->PHYRegDef[RF90_PATH_A].rfTxGainStage = rFPGA0_TxGainStage; //Tx gain stage
	priv->PHYRegDef[RF90_PATH_B].rfTxGainStage = rFPGA0_TxGainStage; //Tx gain stage
	priv->PHYRegDef[RF90_PATH_C].rfTxGainStage = rFPGA0_TxGainStage; //Tx gain stage
	priv->PHYRegDef[RF90_PATH_D].rfTxGainStage = rFPGA0_TxGainStage; //Tx gain stage

	// Tranceiver A~D HSSI Parameter-1
	priv->PHYRegDef[RF90_PATH_A].rfHSSIPara1 = rFPGA0_XA_HSSIParameter1;  //wire control parameter1
	priv->PHYRegDef[RF90_PATH_B].rfHSSIPara1 = rFPGA0_XB_HSSIParameter1;  //wire control parameter1
	priv->PHYRegDef[RF90_PATH_C].rfHSSIPara1 = rFPGA0_XC_HSSIParameter1;  //wire control parameter1
	priv->PHYRegDef[RF90_PATH_D].rfHSSIPara1 = rFPGA0_XD_HSSIParameter1;  //wire control parameter1

	// Tranceiver A~D HSSI Parameter-2
	priv->PHYRegDef[RF90_PATH_A].rfHSSIPara2 = rFPGA0_XA_HSSIParameter2;  //wire control parameter2
	priv->PHYRegDef[RF90_PATH_B].rfHSSIPara2 = rFPGA0_XB_HSSIParameter2;  //wire control parameter2
	priv->PHYRegDef[RF90_PATH_C].rfHSSIPara2 = rFPGA0_XC_HSSIParameter2;  //wire control parameter2
	priv->PHYRegDef[RF90_PATH_D].rfHSSIPara2 = rFPGA0_XD_HSSIParameter2;  //wire control parameter1

	// RF switch Control
	priv->PHYRegDef[RF90_PATH_A].rfSwitchControl = rFPGA0_XAB_SwitchControl; //TR/Ant switch control
	priv->PHYRegDef[RF90_PATH_B].rfSwitchControl = rFPGA0_XAB_SwitchControl;
	priv->PHYRegDef[RF90_PATH_C].rfSwitchControl = rFPGA0_XCD_SwitchControl;
	priv->PHYRegDef[RF90_PATH_D].rfSwitchControl = rFPGA0_XCD_SwitchControl;

	// AGC control 1
	priv->PHYRegDef[RF90_PATH_A].rfAGCControl1 = rOFDM0_XAAGCCore1;
	priv->PHYRegDef[RF90_PATH_B].rfAGCControl1 = rOFDM0_XBAGCCore1;
	priv->PHYRegDef[RF90_PATH_C].rfAGCControl1 = rOFDM0_XCAGCCore1;
	priv->PHYRegDef[RF90_PATH_D].rfAGCControl1 = rOFDM0_XDAGCCore1;

	// AGC control 2
	priv->PHYRegDef[RF90_PATH_A].rfAGCControl2 = rOFDM0_XAAGCCore2;
	priv->PHYRegDef[RF90_PATH_B].rfAGCControl2 = rOFDM0_XBAGCCore2;
	priv->PHYRegDef[RF90_PATH_C].rfAGCControl2 = rOFDM0_XCAGCCore2;
	priv->PHYRegDef[RF90_PATH_D].rfAGCControl2 = rOFDM0_XDAGCCore2;

	// RX AFE control 1
	priv->PHYRegDef[RF90_PATH_A].rfRxIQImbalance = rOFDM0_XARxIQImbalance;
	priv->PHYRegDef[RF90_PATH_B].rfRxIQImbalance = rOFDM0_XBRxIQImbalance;
	priv->PHYRegDef[RF90_PATH_C].rfRxIQImbalance = rOFDM0_XCRxIQImbalance;
	priv->PHYRegDef[RF90_PATH_D].rfRxIQImbalance = rOFDM0_XDRxIQImbalance;

	// RX AFE control 1
	priv->PHYRegDef[RF90_PATH_A].rfRxAFE = rOFDM0_XARxAFE;
	priv->PHYRegDef[RF90_PATH_B].rfRxAFE = rOFDM0_XBRxAFE;
	priv->PHYRegDef[RF90_PATH_C].rfRxAFE = rOFDM0_XCRxAFE;
	priv->PHYRegDef[RF90_PATH_D].rfRxAFE = rOFDM0_XDRxAFE;

	// Tx AFE control 1
	priv->PHYRegDef[RF90_PATH_A].rfTxIQImbalance = rOFDM0_XATxIQImbalance;
	priv->PHYRegDef[RF90_PATH_B].rfTxIQImbalance = rOFDM0_XBTxIQImbalance;
	priv->PHYRegDef[RF90_PATH_C].rfTxIQImbalance = rOFDM0_XCTxIQImbalance;
	priv->PHYRegDef[RF90_PATH_D].rfTxIQImbalance = rOFDM0_XDTxIQImbalance;

	// Tx AFE control 2
	priv->PHYRegDef[RF90_PATH_A].rfTxAFE = rOFDM0_XATxAFE;
	priv->PHYRegDef[RF90_PATH_B].rfTxAFE = rOFDM0_XBTxAFE;
	priv->PHYRegDef[RF90_PATH_C].rfTxAFE = rOFDM0_XCTxAFE;
	priv->PHYRegDef[RF90_PATH_D].rfTxAFE = rOFDM0_XDTxAFE;

	// Tranceiver LSSI Readback  SI mode
	priv->PHYRegDef[RF90_PATH_A].rfLSSIReadBack = rFPGA0_XA_LSSIReadBack;
	priv->PHYRegDef[RF90_PATH_B].rfLSSIReadBack = rFPGA0_XB_LSSIReadBack;
	priv->PHYRegDef[RF90_PATH_C].rfLSSIReadBack = rFPGA0_XC_LSSIReadBack;
	priv->PHYRegDef[RF90_PATH_D].rfLSSIReadBack = rFPGA0_XD_LSSIReadBack;

	// Tranceiver LSSI Readback PI mode
	priv->PHYRegDef[RF90_PATH_A].rfLSSIReadBackPi = TransceiverA_HSPI_Readback;
	priv->PHYRegDef[RF90_PATH_B].rfLSSIReadBackPi = TransceiverB_HSPI_Readback;
	//pHalData->PHYRegDef[RF90_PATH_C].rfLSSIReadBackPi = rFPGA0_XC_LSSIReadBack;
	//pHalData->PHYRegDef[RF90_PATH_D].rfLSSIReadBackPi = rFPGA0_XD_LSSIReadBack;

}


//
//	Description:  Change RF power state.
//
//	Assumption: This function must be executed in re-schdulable context,
//		ie. PASSIVE_LEVEL.
//
//	050823, by rcnjko.
//not understand it seem's use in init
//SetHwReg8192SUsb--->HalFunc.SetHwRegHandler
bool PHY_SetRFPowerState(struct net_device* dev, RT_RF_POWER_STATE eRFPowerState)
{
	struct r8192_priv *priv = ieee80211_priv(dev);
	bool			bResult = FALSE;

	RT_TRACE(COMP_RF, "---------> PHY_SetRFPowerState(): eRFPowerState(%d)\n", eRFPowerState);

	if(eRFPowerState == priv->ieee80211->eRFPowerState)
	{
		RT_TRACE(COMP_RF, "<--------- PHY_SetRFPowerState(): discard the request for eRFPowerState(%d) is the same.\n", eRFPowerState);
		return bResult;
	}

	bResult = phy_SetRFPowerState8192SU(dev, eRFPowerState);

	RT_TRACE(COMP_RF, "<--------- PHY_SetRFPowerState(): bResult(%d)\n", bResult);

	return bResult;
}

//use in phy only
static bool phy_SetRFPowerState8192SU(struct net_device* dev,RT_RF_POWER_STATE eRFPowerState)
{
	struct r8192_priv *priv = ieee80211_priv(dev);
	bool			bResult = TRUE;
	//u8		eRFPath;
	//u8		i, QueueID;
	u8 		u1bTmp;

	if(priv->SetRFPowerStateInProgress == TRUE)
		return FALSE;

	priv->SetRFPowerStateInProgress = TRUE;

	switch(priv->rf_chip )
	{
		default:
		switch( eRFPowerState )
		{
			case eRfOn:
				write_nic_dword(dev, WFM5, FW_BB_RESET_ENABLE);
				write_nic_word(dev, CMDR, 0x37FC);
				write_nic_byte(dev, PHY_CCA, 0x3);
				write_nic_byte(dev, TXPAUSE, 0x00);
				write_nic_byte(dev, SPS1_CTRL, 0x64);
				break;

			//
			// In current solution, RFSleep=RFOff in order to save power under 802.11 power save.
			// By Bruce, 2008-01-16.
			//
			case eRfSleep:
			case eRfOff:
			  	if (priv->ieee80211->eRFPowerState == eRfSleep || priv->ieee80211->eRFPowerState == eRfOff)
						break;
				//
				//RF Off/Sleep sequence. Designed/tested from SD4 Scott, SD1 Grent and Jonbon.
				// Added by Bruce, 2008-11-22.
				//
				//==================================================================
				// (0) Disable FW BB reset checking
				write_nic_dword(dev, WFM5, FW_BB_RESET_DISABLE);

				// (1) Switching Power Supply Register : Disable LD12 & SW12 (for IT)
				u1bTmp = read_nic_byte(dev, LDOV12D_CTRL);
				u1bTmp |= BIT0;
				write_nic_byte(dev, LDOV12D_CTRL, u1bTmp);

				write_nic_byte(dev, SPS1_CTRL, 0x0);
				write_nic_byte(dev, TXPAUSE, 0xFF);

				// (2) MAC Tx/Rx enable, BB enable, CCK/OFDM enable
				write_nic_word(dev, CMDR, 0x77FC);
				write_nic_byte(dev, PHY_CCA, 0x0);
				udelay(100);

				write_nic_word(dev, CMDR, 0x37FC);
				udelay(10);

				write_nic_word(dev, CMDR, 0x77FC);
				udelay(10);

				// (3) Reset BB TRX blocks
				write_nic_word(dev, CMDR, 0x57FC);
				break;

			default:
				bResult = FALSE;
				//RT_ASSERT(FALSE, ("phy_SetRFPowerState8192SU(): unknown state to set: 0x%X!!!\n", eRFPowerState));
				break;
		}
		break;

	}
	priv->ieee80211->eRFPowerState = eRFPowerState;
#ifdef TO_DO_LIST
	if(bResult)
	{
		// Update current RF state variable.
		priv->ieee80211->eRFPowerState = eRFPowerState;

		switch(priv->rf_chip )
		{
			case RF_8256:
			switch(priv->ieee80211->eRFPowerState)
			{
				case eRfOff:
					//
					//If Rf off reason is from IPS, Led should blink with no link, by Maddest 071015
					//
					if(pMgntInfo->RfOffReason==RF_CHANGE_BY_IPS )
					{
						dev->HalFunc.LedControlHandler(dev,LED_CTL_NO_LINK);
					}
					else
					{
						// Turn off LED if RF is not ON.
						dev->HalFunc.LedControlHandler(dev, LED_CTL_POWER_OFF);
					}
					break;

				case eRfOn:
					// Turn on RF we are still linked, which might happen when
					// we quickly turn off and on HW RF. 2006.05.12, by rcnjko.
					if( pMgntInfo->bMediaConnect == TRUE )
					{
						dev->HalFunc.LedControlHandler(dev, LED_CTL_LINK);
					}
					else
					{
						// Turn off LED if RF is not ON.
						dev->HalFunc.LedControlHandler(dev, LED_CTL_NO_LINK);
					}
					break;

				default:
					// do nothing.
					break;
			}// Switch RF state

				break;

			default:
				RT_TRACE(COMP_RF, "phy_SetRFPowerState8192SU(): Unknown RF type\n");
				break;
		}// Switch rf_chip
	}
#endif
	priv->SetRFPowerStateInProgress = FALSE;

	return bResult;
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
 // no use temp
 void
PHY_GetTxPowerLevel8192S(
	struct net_device* dev,
	 long*    		powerlevel
	)
{
	struct r8192_priv *priv = ieee80211_priv(dev);
	u8			TxPwrLevel = 0;
	long			TxPwrDbm;
	//
	// Because the Tx power indexes are different, we report the maximum of them to
	// meet the CCX TPC request. By Bruce, 2008-01-31.
	//

	// CCK
	TxPwrLevel = priv->CurrentCckTxPwrIdx;
	TxPwrDbm = phy_TxPwrIdxToDbm(dev, WIRELESS_MODE_B, TxPwrLevel);

	// Legacy OFDM
	TxPwrLevel = priv->CurrentOfdm24GTxPwrIdx + priv->LegacyHTTxPowerDiff;

	// Compare with Legacy OFDM Tx power.
	if(phy_TxPwrIdxToDbm(dev, WIRELESS_MODE_G, TxPwrLevel) > TxPwrDbm)
		TxPwrDbm = phy_TxPwrIdxToDbm(dev, WIRELESS_MODE_G, TxPwrLevel);

	// HT OFDM
	TxPwrLevel = priv->CurrentOfdm24GTxPwrIdx;

	// Compare with HT OFDM Tx power.
	if(phy_TxPwrIdxToDbm(dev, WIRELESS_MODE_N_24G, TxPwrLevel) > TxPwrDbm)
		TxPwrDbm = phy_TxPwrIdxToDbm(dev, WIRELESS_MODE_N_24G, TxPwrLevel);

	*powerlevel = TxPwrDbm;
}

/*-----------------------------------------------------------------------------
 * Function:    SetTxPowerLevel8190()
 *
 * Overview:    This function is export to "HalCommon" moudule
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
  *---------------------------------------------------------------------------*/
 void PHY_SetTxPowerLevel8192S(struct net_device* dev, u8	channel)
{
	struct r8192_priv *priv = ieee80211_priv(dev);
	//HAL_DATA_TYPE		*pHalData = GET_HAL_DATA(dev);
	u8	powerlevel = (u8)EEPROM_Default_TxPower, powerlevelOFDM24G = 0x10;
	s8 	ant_pwr_diff = 0;
	u32	u4RegValue;
	u8	index = (channel -1);
	// 2009/01/22 MH Add for new EEPROM format from SD3
	u8	pwrdiff[2] = {0};
	u8	ht20pwr[2] = {0}, ht40pwr[2] = {0};
	u8	rfpath = 0, rfpathnum = 2;

	if(priv->bTXPowerDataReadFromEEPORM == FALSE)
		return;

	//
	// Read predefined TX power index in EEPROM
	//
//	if(priv->epromtype == EPROM_93c46)
	{
		//
		// Mainly we use RF-A Tx Power to write the Tx Power registers, but the RF-B Tx
		// Power must be calculated by the antenna diff.
		// So we have to rewrite Antenna gain offset register here.
		// Please refer to BB register 0x80c
		// 1. For CCK.
		// 2. For OFDM 1T or 2T
		//

		// 1. CCK
		powerlevel = priv->RfTxPwrLevelCck[0][index];

		if (priv->rf_type == RF_1T2R || priv->rf_type == RF_1T1R)
		{
		// Read HT 40 OFDM TX power
		powerlevelOFDM24G = priv->RfTxPwrLevelOfdm1T[0][index];
		// RF B HT OFDM pwr-RFA HT OFDM pwr
		// Only one RF we need not to decide B <-> A pwr diff

		// Legacy<->HT pwr diff, we only care about path A.

		// We only assume 1T as RF path A
		rfpathnum = 1;
		ht20pwr[0] = ht40pwr[0] = priv->RfTxPwrLevelOfdm1T[0][index];
		}
		else if (priv->rf_type == RF_2T2R)
		{
		// Read HT 40 OFDM TX power
		powerlevelOFDM24G = priv->RfTxPwrLevelOfdm2T[0][index];
			// RF B HT OFDM pwr-RFA HT OFDM pwr
		ant_pwr_diff = 	priv->RfTxPwrLevelOfdm2T[1][index] -
						priv->RfTxPwrLevelOfdm2T[0][index];
			// RF B (HT OFDM pwr+legacy-ht-diff) -(RFA HT OFDM pwr+legacy-ht-diff)
		// We can not handle Path B&A HT/Legacy pwr diff for 92S now.

		//RTPRINT(FPHY, PHY_TXPWR, ("CH-%d HT40 A/B Pwr index = %x/%x(%d/%d)\n",
		//channel, priv->RfTxPwrLevelOfdm2T[0][index],
		//priv->RfTxPwrLevelOfdm2T[1][index],
		//priv->RfTxPwrLevelOfdm2T[0][index],
		//priv->RfTxPwrLevelOfdm2T[1][index]));

		ht20pwr[0] = ht40pwr[0] = priv->RfTxPwrLevelOfdm2T[0][index];
		ht20pwr[1] = ht40pwr[1] = priv->RfTxPwrLevelOfdm2T[1][index];
	}

	//
	// 2009/01/21 MH Support new EEPROM format from SD3 requirement
	// 2009/02/10 Cosa, Here is only for reg B/C/D to A gain diff.
	//
	if (priv->EEPROMVersion == 2)	// Defined by SD1 Jong
	{
		if (priv->CurrentChannelBW == HT_CHANNEL_WIDTH_20)
		{
			for (rfpath = 0; rfpath < rfpathnum; rfpath++)
			{
				// HT 20<->40 pwr diff
				pwrdiff[rfpath] = priv->TxPwrHt20Diff[rfpath][index];

				// Calculate Antenna pwr diff
				if (pwrdiff[rfpath] < 8)	// 0~+7
				{
					ht20pwr[rfpath] += pwrdiff[rfpath];
				}
				else				// index8-15=-8~-1
				{
					ht20pwr[rfpath] -= (15-pwrdiff[rfpath]);
				}
			}

			// RF B HT OFDM pwr-RFA HT OFDM pwr
			if (priv->rf_type == RF_2T2R)
				ant_pwr_diff = ht20pwr[1] - ht20pwr[0];

			//RTPRINT(FPHY, PHY_TXPWR,
			//("HT20 to HT40 pwrdiff[A/B]=%d/%d, ant_pwr_diff=%d(B-A=%d-%d)\n",
			//pwrdiff[0], pwrdiff[1], ant_pwr_diff, ht20pwr[1], ht20pwr[0]));
		}

		// Band Edge scheme is enabled for FCC mode
		if (priv->TxPwrbandEdgeFlag == 1/* && pHalData->ChannelPlan == 0*/)
		{
			for (rfpath = 0; rfpath < rfpathnum; rfpath++)
			{
				pwrdiff[rfpath] = 0;
				if (priv->CurrentChannelBW == HT_CHANNEL_WIDTH_20_40)
				{
					if (channel <= 3)
						pwrdiff[rfpath] = priv->TxPwrbandEdgeHt40[rfpath][0];
					else if (channel >= 9)
						pwrdiff[rfpath] = priv->TxPwrbandEdgeHt40[rfpath][1];
					else
						pwrdiff[rfpath] = 0;

					ht40pwr[rfpath] -= pwrdiff[rfpath];
				}
				else if (priv->CurrentChannelBW == HT_CHANNEL_WIDTH_20)
				{
					if (channel == 1)
						pwrdiff[rfpath] = priv->TxPwrbandEdgeHt20[rfpath][0];
					else if (channel >= 11)
						pwrdiff[rfpath] = priv->TxPwrbandEdgeHt20[rfpath][1];
					else
						pwrdiff[rfpath] = 0;

					ht20pwr[rfpath] -= pwrdiff[rfpath];
				}
			}

			if (priv->rf_type == RF_2T2R)
			{
				// HT 20/40 must decide if they need to minus  BD pwr offset
				if (priv->CurrentChannelBW == HT_CHANNEL_WIDTH_20_40)
					ant_pwr_diff = ht40pwr[1] - ht40pwr[0];
				else
					ant_pwr_diff = ht20pwr[1] - ht20pwr[0];
			}
			if (priv->CurrentChannelBW == HT_CHANNEL_WIDTH_20)
			{
				if (channel <= 1 || channel >= 11)
				{
					//RTPRINT(FPHY, PHY_TXPWR,
					//("HT20 Band-edge pwrdiff[A/B]=%d/%d, ant_pwr_diff=%d(B-A=%d-%d)\n",
					//pwrdiff[0], pwrdiff[1], ant_pwr_diff, ht20pwr[1], ht20pwr[0]));
				}
			}
			else
			{
				if (channel <= 3 || channel >= 9)
				{
					//RTPRINT(FPHY, PHY_TXPWR,
					//("HT40 Band-edge pwrdiff[A/B]=%d/%d, ant_pwr_diff=%d(B-A=%d-%d)\n",
					//pwrdiff[0], pwrdiff[1], ant_pwr_diff, ht40pwr[1], ht40pwr[0]));
				}
			}
		}
		}

	//Cosa added for protection, the reg rFPGA0_TxGainStage
	// range is from 7~-8, index = 0x0~0xf
	if(ant_pwr_diff > 7)
		ant_pwr_diff = 7;
	if(ant_pwr_diff < -8)
		ant_pwr_diff = -8;

		//RTPRINT(FPHY, PHY_TXPWR,
		//("CCK/HT Power index = %x/%x(%d/%d), ant_pwr_diff=%d\n",
		//powerlevel, powerlevelOFDM24G, powerlevel, powerlevelOFDM24G, ant_pwr_diff));

		ant_pwr_diff &= 0xf;

		// Antenna TX power difference
		priv->AntennaTxPwDiff[2] = 0;// RF-D, don't care
		priv->AntennaTxPwDiff[1] = 0;// RF-C, don't care
		priv->AntennaTxPwDiff[0] = (u8)(ant_pwr_diff);		// RF-B

		// Antenna gain offset from B/C/D to A
		u4RegValue = (	priv->AntennaTxPwDiff[2]<<8 |
						priv->AntennaTxPwDiff[1]<<4 |
						priv->AntennaTxPwDiff[0]	);

		// Notify Tx power difference for B/C/D to A!!!
		rtl8192_setBBreg(dev, rFPGA0_TxGainStage, (bXBTxAGC|bXCTxAGC|bXDTxAGC), u4RegValue);
	}

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
#ifdef TODO //WB, 11h has not implemented now.
	if(	priv->ieee80211->iw_mode != IW_MODE_INFRA && priv->bWithCcxCellPwr &&
		channel == priv->ieee80211->current_network.channel)// & priv->ieee80211->mAssoc )
	{
		u8	CckCellPwrIdx = phy_DbmToTxPwrIdx(dev, WIRELESS_MODE_B, priv->CcxCellPwr);
		u8	LegacyOfdmCellPwrIdx = phy_DbmToTxPwrIdx(dev, WIRELESS_MODE_G, priv->CcxCellPwr);
		u8	OfdmCellPwrIdx = phy_DbmToTxPwrIdx(dev, WIRELESS_MODE_N_24G, priv->CcxCellPwr);

		RT_TRACE(COMP_TXAGC,
		("CCX Cell Limit: %d dbm => CCK Tx power index : %d, Legacy OFDM Tx power index : %d, OFDM Tx power index: %d\n",
		priv->CcxCellPwr, CckCellPwrIdx, LegacyOfdmCellPwrIdx, OfdmCellPwrIdx));
		RT_TRACE(COMP_TXAGC,
		("EEPROM channel(%d) => CCK Tx power index: %d, Legacy OFDM Tx power index : %d, OFDM Tx power index: %d\n",
		channel, powerlevel, powerlevelOFDM24G + priv->LegacyHTTxPowerDiff, powerlevelOFDM24G));

		// CCK
		if(powerlevel > CckCellPwrIdx)
			powerlevel = CckCellPwrIdx;
		// Legacy OFDM, HT OFDM
		if(powerlevelOFDM24G + priv->LegacyHTTxPowerDiff > LegacyOfdmCellPwrIdx)
		{
			if((OfdmCellPwrIdx - priv->LegacyHTTxPowerDiff) > 0)
			{
				powerlevelOFDM24G = OfdmCellPwrIdx - priv->LegacyHTTxPowerDiff;
			}
			else
			{
				powerlevelOFDM24G = 0;
			}
		}

		RT_TRACE(COMP_TXAGC,
		("Altered CCK Tx power index : %d, Legacy OFDM Tx power index: %d, OFDM Tx power index: %d\n",
		powerlevel, powerlevelOFDM24G + priv->LegacyHTTxPowerDiff, powerlevelOFDM24G));
	}
#endif

	priv->CurrentCckTxPwrIdx = powerlevel;
	priv->CurrentOfdm24GTxPwrIdx = powerlevelOFDM24G;

	switch(priv->rf_chip)
	{
		case RF_8225:
			//PHY_SetRF8225CckTxPower(dev, powerlevel);
			//PHY_SetRF8225OfdmTxPower(dev, powerlevelOFDM24G);
		break;

		case RF_8256:
			break;

		case RF_6052:
			PHY_RF6052SetCckTxPower(dev, powerlevel);
			PHY_RF6052SetOFDMTxPower(dev, powerlevelOFDM24G);
			break;

		case RF_8258:
			break;
		default:
			break;
	}

}

//
//	Description:
//		Update transmit power level of all channel supported.
//
//	TODO:
//		A mode.
//	By Bruce, 2008-02-04.
//    no use temp
bool PHY_UpdateTxPowerDbm8192S(struct net_device* dev, long powerInDbm)
{
	struct r8192_priv 	*priv = ieee80211_priv(dev);
	u8				idx;
	u8				rf_path;

	// TODO: A mode Tx power.
	u8	CckTxPwrIdx = phy_DbmToTxPwrIdx(dev, WIRELESS_MODE_B, powerInDbm);
	u8	OfdmTxPwrIdx = phy_DbmToTxPwrIdx(dev, WIRELESS_MODE_N_24G, powerInDbm);

	if(OfdmTxPwrIdx - priv->LegacyHTTxPowerDiff > 0)
		OfdmTxPwrIdx -= priv->LegacyHTTxPowerDiff;
	else
		OfdmTxPwrIdx = 0;

	for(idx = 0; idx < 14; idx++)
	{
		priv->TxPowerLevelCCK[idx] = CckTxPwrIdx;
		priv->TxPowerLevelCCK_A[idx] = CckTxPwrIdx;
		priv->TxPowerLevelCCK_C[idx] = CckTxPwrIdx;
		priv->TxPowerLevelOFDM24G[idx] = OfdmTxPwrIdx;
		priv->TxPowerLevelOFDM24G_A[idx] = OfdmTxPwrIdx;
		priv->TxPowerLevelOFDM24G_C[idx] = OfdmTxPwrIdx;

		for (rf_path = 0; rf_path < 2; rf_path++)
		{
			priv->RfTxPwrLevelCck[rf_path][idx] = CckTxPwrIdx;
			priv->RfTxPwrLevelOfdm1T[rf_path][idx] =  \
			priv->RfTxPwrLevelOfdm2T[rf_path][idx] = OfdmTxPwrIdx;
		}
	}

	PHY_SetTxPowerLevel8192S(dev, priv->chan);

	return TRUE;
}

/*
	Description:
		When beacon interval is changed, the values of the
		hw registers should be modified.
	By tynli, 2008.10.24.

*/

extern void PHY_SetBeaconHwReg(	struct net_device* dev, u16 BeaconInterval)
{
	u32 NewBeaconNum;

	NewBeaconNum = BeaconInterval *32 - 64;
	//PlatformEFIOWrite4Byte(Adapter, WFM3+4, NewBeaconNum);
	//PlatformEFIOWrite4Byte(Adapter, WFM3, 0xB026007C);
	write_nic_dword(dev, WFM3+4, NewBeaconNum);
	write_nic_dword(dev, WFM3, 0xB026007C);
}

//
//	Description:
//		Map dBm into Tx power index according to
//		current HW model, for example, RF and PA, and
//		current wireless mode.
//	By Bruce, 2008-01-29.
//    use in phy only
static u8 phy_DbmToTxPwrIdx(
	struct net_device* dev,
	WIRELESS_MODE	WirelessMode,
	long			PowerInDbm
	)
{
	//struct r8192_priv *priv = ieee80211_priv(dev);
	u8				TxPwrIdx = 0;
	long				Offset = 0;


	//
	// Tested by MP, we found that CCK Index 0 equals to -7dbm, OFDM legacy equals to
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
//    use in phy only
static long phy_TxPwrIdxToDbm(
	struct net_device* dev,
	WIRELESS_MODE	WirelessMode,
	u8			TxPwrIdx
	)
{
	//struct r8192_priv *priv = ieee80211_priv(dev);
	long				Offset = 0;
	long				PwrOutDbm = 0;

	//
	// Tested by MP, we found that CCK Index 0 equals to -7dbm, OFDM legacy equals to
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

	PwrOutDbm = TxPwrIdx / 2 + Offset; // Discard the decimal part.

	return PwrOutDbm;
}

#ifdef TO_DO_LIST
extern	VOID
PHY_ScanOperationBackup8192S(
	IN	PADAPTER	Adapter,
	IN	u1Byte		Operation
	)
{

	HAL_DATA_TYPE			*pHalData = GET_HAL_DATA(Adapter);
	PMGNT_INFO			pMgntInfo = &(Adapter->MgntInfo);
	u4Byte				BitMask;
	u1Byte				initial_gain;





	if(!Adapter->bDriverStopped)
	{
		switch(Operation)
		{
			case SCAN_OPT_BACKUP:
				//
				// <Roger_Notes> We halt FW DIG and disable high ppower both two DMs here
				// and resume both two DMs while scan complete.
				// 2008.11.27.
				//
				Adapter->HalFunc.SetFwCmdHandler(Adapter, FW_CMD_PAUSE_DM_BY_SCAN);
				break;

			case SCAN_OPT_RESTORE:
				//
				// <Roger_Notes> We resume DIG and enable high power both two DMs here and
				// recover earlier DIG settings.
				// 2008.11.27.
				//
				Adapter->HalFunc.SetFwCmdHandler(Adapter, FW_CMD_RESUME_DM_BY_SCAN);
				break;

			default:
				RT_TRACE(COMP_SCAN, DBG_LOUD, ("Unknown Scan Backup Operation. \n"));
				break;
		}
	}
}
#endif

//nouse temp
void PHY_InitialGain8192S(struct net_device* dev,u8 Operation	)
{

	//struct r8192_priv *priv = ieee80211_priv(dev);
	//u32					BitMask;
	//u8					initial_gain;
}

/*-----------------------------------------------------------------------------
 * Function:    SetBWModeCallback8190Pci()
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
//    use in phy only (in win it's timer)
void PHY_SetBWModeCallback8192S(struct net_device *dev)
{
	struct r8192_priv *priv = ieee80211_priv(dev);
	u8	 			regBwOpMode;

	//return;

	// Added it for 20/40 mhz switch time evaluation by guangan 070531
	//u32				NowL, NowH;
	//u8Byte				BeginTime, EndTime;
	u8				regRRSR_RSC;

	RT_TRACE(COMP_SWBW, "==>SetBWModeCallback8190Pci()  Switch to %s bandwidth\n", \
					priv->CurrentChannelBW == HT_CHANNEL_WIDTH_20?"20MHz":"40MHz");

	if(priv->rf_chip == RF_PSEUDO_11N)
	{
		priv->SetBWModeInProgress= FALSE;
		return;
	}

	if(!priv->up)
		return;

	// Added it for 20/40 mhz switch time evaluation by guangan 070531
	//NowL = read_nic_dword(dev, TSFR);
	//NowH = read_nic_dword(dev, TSFR+4);
	//BeginTime = ((u8Byte)NowH << 32) + NowL;

	//3//
	//3//<1>Set MAC register
	//3//
	regBwOpMode = read_nic_byte(dev, BW_OPMODE);
	regRRSR_RSC = read_nic_byte(dev, RRSR+2);

	switch(priv->CurrentChannelBW)
	{
		case HT_CHANNEL_WIDTH_20:
			//if(priv->card_8192_version >= VERSION_8192S_BCUT)
			//	write_nic_byte(dev, rFPGA0_AnalogParameter2, 0x58);

			regBwOpMode |= BW_OPMODE_20MHZ;
		       	// 2007/02/07 Mark by Emily becasue we have not verify whether this register works
			write_nic_byte(dev, BW_OPMODE, regBwOpMode);
			break;

		case HT_CHANNEL_WIDTH_20_40:
			//if(priv->card_8192_version >= VERSION_8192S_BCUT)
			//	write_nic_byte(dev, rFPGA0_AnalogParameter2, 0x18);

			regBwOpMode &= ~BW_OPMODE_20MHZ;
        		// 2007/02/07 Mark by Emily becasue we have not verify whether this register works
			write_nic_byte(dev, BW_OPMODE, regBwOpMode);
			regRRSR_RSC = (regRRSR_RSC&0x90) |(priv->nCur40MhzPrimeSC<<5);
			write_nic_byte(dev, RRSR+2, regRRSR_RSC);
			break;

		default:
			RT_TRACE(COMP_DBG, "SetBWModeCallback8190Pci():\
						unknown Bandwidth: %#X\n",priv->CurrentChannelBW);
			break;
	}

	//3//
	//3//<2>Set PHY related register
	//3//
	switch(priv->CurrentChannelBW)
	{
		/* 20 MHz channel*/
		case HT_CHANNEL_WIDTH_20:
			rtl8192_setBBreg(dev, rFPGA0_RFMOD, bRFMOD, 0x0);
			rtl8192_setBBreg(dev, rFPGA1_RFMOD, bRFMOD, 0x0);

			// Correct the tx power for CCK rate in 40M. Suggest by YN, 20071207
			// It is set in Tx descriptor for 8192x series
			//write_nic_dword(dev, rCCK0_TxFilter1, 0x1a1b0000);
			//write_nic_dword(dev, rCCK0_TxFilter2, 0x090e1317);
			//write_nic_dword(dev, rCCK0_DebugPort, 0x00000204);

			if (priv->card_8192_version >= VERSION_8192S_BCUT)
				write_nic_byte(dev, rFPGA0_AnalogParameter2, 0x58);


			break;

		/* 40 MHz channel*/
		case HT_CHANNEL_WIDTH_20_40:
			rtl8192_setBBreg(dev, rFPGA0_RFMOD, bRFMOD, 0x1);
			rtl8192_setBBreg(dev, rFPGA1_RFMOD, bRFMOD, 0x1);

			// Correct the tx power for CCK rate in 40M. Suggest by YN, 20071207
			//write_nic_dword(dev, rCCK0_TxFilter1, 0x35360000);
			//write_nic_dword(dev, rCCK0_TxFilter2, 0x121c252e);
			//write_nic_dword(dev, rCCK0_DebugPort, 0x00000409);

			// Set Control channel to upper or lower. These settings are required only for 40MHz
			rtl8192_setBBreg(dev, rCCK0_System, bCCKSideBand, (priv->nCur40MhzPrimeSC>>1));
			rtl8192_setBBreg(dev, rOFDM1_LSTF, 0xC00, priv->nCur40MhzPrimeSC);

			//rtl8192_setBBreg(dev, rFPGA0_AnalogParameter1, 0x00300000, 3);
			if (priv->card_8192_version >= VERSION_8192S_BCUT)
				write_nic_byte(dev, rFPGA0_AnalogParameter2, 0x18);

			break;

		default:
			RT_TRACE(COMP_DBG, "SetBWModeCallback8190Pci(): unknown Bandwidth: %#X\n"\
						,priv->CurrentChannelBW);
			break;

	}
	//Skip over setting of J-mode in BB register here. Default value is "None J mode". Emily 20070315

	// Added it for 20/40 mhz switch time evaluation by guangan 070531
	//NowL = read_nic_dword(dev, TSFR);
	//NowH = read_nic_dword(dev, TSFR+4);
	//EndTime = ((u8Byte)NowH << 32) + NowL;
	//RT_TRACE(COMP_SCAN, DBG_LOUD, ("SetBWModeCallback8190Pci: time of SetBWMode = %I64d us!\n", (EndTime - BeginTime)));

	//3<3>Set RF related register
	switch( priv->rf_chip )
	{
		case RF_8225:
			//PHY_SetRF8225Bandwidth(dev, priv->CurrentChannelBW);
			break;

		case RF_8256:
			// Please implement this function in Hal8190PciPhy8256.c
			//PHY_SetRF8256Bandwidth(dev, priv->CurrentChannelBW);
			break;

		case RF_8258:
			// Please implement this function in Hal8190PciPhy8258.c
			// PHY_SetRF8258Bandwidth();
			break;

		case RF_PSEUDO_11N:
			// Do Nothing
			break;

		case RF_6052:
			PHY_RF6052SetBandwidth(dev, priv->CurrentChannelBW);
			break;
		default:
			printk("Unknown rf_chip: %d\n", priv->rf_chip);
			break;
	}

	priv->SetBWModeInProgress= FALSE;

	RT_TRACE(COMP_SWBW, "<==SetBWModeCallback8190Pci() \n" );
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
//extern void PHY_SetBWMode8192S(	struct net_device* dev,
//	HT_CHANNEL_WIDTH	Bandwidth,	// 20M or 40M
//	HT_EXTCHNL_OFFSET	Offset		// Upper, Lower, or Don't care
void rtl8192_SetBWMode(struct net_device *dev, HT_CHANNEL_WIDTH	Bandwidth, HT_EXTCHNL_OFFSET Offset)
{
	struct r8192_priv *priv = ieee80211_priv(dev);
	HT_CHANNEL_WIDTH tmpBW = priv->CurrentChannelBW;


	// Modified it for 20/40 mhz switch by guangan 070531

	//return;

	//if(priv->SwChnlInProgress)
//	if(pMgntInfo->bScanInProgress)
//	{
//		RT_TRACE(COMP_SCAN, DBG_LOUD, ("SetBWMode8190Pci() %s Exit because bScanInProgress!\n",
//					Bandwidth == HT_CHANNEL_WIDTH_20?"20MHz":"40MHz"));
//		return;
//	}

//	if(priv->SetBWModeInProgress)
//	{
//		// Modified it for 20/40 mhz switch by guangan 070531
//		RT_TRACE(COMP_SCAN, DBG_LOUD, ("SetBWMode8190Pci() %s cancel last timer because SetBWModeInProgress!\n",
//					Bandwidth == HT_CHANNEL_WIDTH_20?"20MHz":"40MHz"));
//		PlatformCancelTimer(dev, &priv->SetBWModeTimer);
//		//return;
//	}

	if(priv->SetBWModeInProgress)
		return;

	priv->SetBWModeInProgress= TRUE;

	priv->CurrentChannelBW = Bandwidth;

	if(Offset==HT_EXTCHNL_OFFSET_LOWER)
		priv->nCur40MhzPrimeSC = HAL_PRIME_CHNL_OFFSET_UPPER;
	else if(Offset==HT_EXTCHNL_OFFSET_UPPER)
		priv->nCur40MhzPrimeSC = HAL_PRIME_CHNL_OFFSET_LOWER;
	else
		priv->nCur40MhzPrimeSC = HAL_PRIME_CHNL_OFFSET_DONT_CARE;

	if((priv->up) )// && !(RT_CANNOT_IO(Adapter) && Adapter->bInSetPower) )
	{
	SetBWModeCallback8192SUsbWorkItem(dev);
	}
	else
	{
		RT_TRACE(COMP_SCAN, "PHY_SetBWMode8192S() SetBWModeInProgress FALSE driver sleep or unload\n");
		priv->SetBWModeInProgress= FALSE;
		priv->CurrentChannelBW = tmpBW;
	}
}

//    use in phy only (in win it's timer)
void PHY_SwChnlCallback8192S(struct net_device *dev)
{

	struct r8192_priv *priv = ieee80211_priv(dev);
	u32		delay;
	//bool			ret;

	RT_TRACE(COMP_CH, "==>SwChnlCallback8190Pci(), switch to channel %d\n", priv->chan);

	if(!priv->up)
		return;

	if(priv->rf_chip == RF_PSEUDO_11N)
	{
		priv->SwChnlInProgress=FALSE;
		return; 								//return immediately if it is peudo-phy
	}

	do{
		if(!priv->SwChnlInProgress)
			break;

		//if(!phy_SwChnlStepByStep(dev, priv->CurrentChannel, &priv->SwChnlStage, &priv->SwChnlStep, &delay))
		if(!phy_SwChnlStepByStep(dev, priv->chan, &priv->SwChnlStage, &priv->SwChnlStep, &delay))
		{
			if(delay>0)
			{
				mdelay(delay);
				//PlatformSetTimer(dev, &priv->SwChnlTimer, delay);
				//mod_timer(&priv->SwChnlTimer,  jiffies + MSECS(delay));
				//==>PHY_SwChnlCallback8192S(dev); for 92se
				//==>SwChnlCallback8192SUsb(dev) for 92su
			}
			else
			continue;
		}
		else
		{
			priv->SwChnlInProgress=FALSE;
			break;
		}
	}while(true);
}

// Call after initialization
//extern void PHY_SwChnl8192S(struct net_device* dev,	u8 channel)
u8 rtl8192_phy_SwChnl(struct net_device* dev, u8 channel)
{
	struct r8192_priv *priv = ieee80211_priv(dev);
	//u8 			tmpchannel =channel;
	//bool			bResult = false;

        if(!priv->up)
		return false;

	if(priv->SwChnlInProgress)
		return false;

	if(priv->SetBWModeInProgress)
		return false;

	//--------------------------------------------
	switch(priv->ieee80211->mode)
	{
	case WIRELESS_MODE_A:
	case WIRELESS_MODE_N_5G:
		if (channel<=14){
			RT_TRACE(COMP_ERR, "WIRELESS_MODE_A but channel<=14");
			return false;
		}
		break;

	case WIRELESS_MODE_B:
		if (channel>14){
			RT_TRACE(COMP_ERR, "WIRELESS_MODE_B but channel>14");
			return false;
		}
		break;

	case WIRELESS_MODE_G:
	case WIRELESS_MODE_N_24G:
		if (channel>14){
			RT_TRACE(COMP_ERR, "WIRELESS_MODE_G but channel>14");
			return false;
		}
		break;

	default:
			;//RT_TRACE(COMP_ERR, "Invalid WirelessMode(%#x)!!\n", priv->ieee80211->mode);
		break;
	}
	//--------------------------------------------

	priv->SwChnlInProgress = TRUE;
	if( channel == 0)
		channel = 1;

	priv->chan=channel;

	priv->SwChnlStage=0;
	priv->SwChnlStep=0;

	if((priv->up))// && !(RT_CANNOT_IO(Adapter) && Adapter->bInSetPower))
	{
	SwChnlCallback8192SUsbWorkItem(dev);
#ifdef TO_DO_LIST
	if(bResult)
		{
			RT_TRACE(COMP_SCAN, "PHY_SwChnl8192S SwChnlInProgress TRUE schdule workitem done\n");
		}
		else
		{
			RT_TRACE(COMP_SCAN, "PHY_SwChnl8192S SwChnlInProgress FALSE schdule workitem error\n");
			priv->SwChnlInProgress = false;
			priv->CurrentChannel = tmpchannel;
		}
#endif
	}
	else
	{
		RT_TRACE(COMP_SCAN, "PHY_SwChnl8192S SwChnlInProgress FALSE driver sleep or unload\n");
		priv->SwChnlInProgress = false;
		//priv->CurrentChannel = tmpchannel;
	}
        return true;
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
//not understand it
void PHY_SwChnlPhy8192S(	// Only called during initialize
	struct net_device* dev,
	u8		channel
	)
{
	struct r8192_priv *priv = ieee80211_priv(dev);

	RT_TRACE(COMP_SCAN, "==>PHY_SwChnlPhy8192S(), switch to channel %d.\n", priv->chan);

#ifdef TO_DO_LIST
	// Cannot IO.
	if(RT_CANNOT_IO(dev))
		return;
#endif

	// Channel Switching is in progress.
	if(priv->SwChnlInProgress)
		return;

	//return immediately if it is peudo-phy
	if(priv->rf_chip == RF_PSEUDO_11N)
	{
		priv->SwChnlInProgress=FALSE;
		return;
	}

	priv->SwChnlInProgress = TRUE;
	if( channel == 0)
		channel = 1;

	priv->chan=channel;

	priv->SwChnlStage = 0;
	priv->SwChnlStep = 0;

	phy_FinishSwChnlNow(dev,channel);

	priv->SwChnlInProgress = FALSE;
}

//    use in phy only
static bool
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
		return FALSE;
	}
	if(CmdTableIdx >= CmdTableSz)
	{
		//RT_ASSERT(FALSE,
			//	("phy_SetSwChnlCmdArray(): Access invalid index, please check size of the table, CmdTableIdx:%d, CmdTableSz:%d\n",
				//CmdTableIdx, CmdTableSz));
		return FALSE;
	}

	pCmd = CmdTable + CmdTableIdx;
	pCmd->CmdID = CmdID;
	pCmd->Para1 = Para1;
	pCmd->Para2 = Para2;
	pCmd->msDelay = msDelay;

	return TRUE;
}

//    use in phy only
static bool
phy_SwChnlStepByStep(
	struct net_device* dev,
	u8		channel,
	u8		*stage,
	u8		*step,
	u32		*delay
	)
{
	struct r8192_priv *priv = ieee80211_priv(dev);
	//PCHANNEL_ACCESS_SETTING	pChnlAccessSetting;
	SwChnlCmd				PreCommonCmd[MAX_PRECMD_CNT];
	u32					PreCommonCmdCnt;
	SwChnlCmd				PostCommonCmd[MAX_POSTCMD_CNT];
	u32					PostCommonCmdCnt;
	SwChnlCmd				RfDependCmd[MAX_RFDEPENDCMD_CNT];
	u32					RfDependCmdCnt;
	SwChnlCmd				*CurrentCmd = NULL;
	u8					eRFPath;

	//RT_ASSERT((dev != NULL), ("Adapter should not be NULL\n"));
	//RT_ASSERT(IsLegalChannel(dev, channel), ("illegal channel: %d\n", channel));
	RT_TRACE(COMP_CH, "===========>%s(), channel:%d, stage:%d, step:%d\n", __FUNCTION__, channel, *stage, *step);
	//RT_ASSERT((pHalData != NULL), ("pHalData should not be NULL\n"));
	if (!IsLegalChannel(priv->ieee80211, channel))
	{
		RT_TRACE(COMP_ERR, "=============>set to illegal channel:%d\n", channel);
		return true; //return true to tell upper caller function this channel setting is finished! Or it will in while loop.
	}

	//pChnlAccessSetting = &Adapter->MgntInfo.Info8185.ChannelAccessSetting;
	//RT_ASSERT((pChnlAccessSetting != NULL), ("pChnlAccessSetting should not be NULL\n"));

	//for(eRFPath = RF90_PATH_A; eRFPath <priv->NumTotalRFPath; eRFPath++)
	//for(eRFPath = 0; eRFPath <priv->NumTotalRFPath; eRFPath++)
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
	switch( priv->rf_chip )
	{
		case RF_8225:
		if (channel < 1 || channel > 14)
			RT_TRACE(COMP_ERR, "illegal channel for zebra:%d\n", channel);
		//RT_ASSERT((channel >= 1 && channel <= 14), ("illegal channel for Zebra: %d\n", channel));
		// 2008/09/04 MH Change channel.
		phy_SetSwChnlCmdArray(RfDependCmd, RfDependCmdCnt++, MAX_RFDEPENDCMD_CNT,
			CmdID_RF_WriteReg, rRfChannel, channel, 10);
		phy_SetSwChnlCmdArray(RfDependCmd, RfDependCmdCnt++, MAX_RFDEPENDCMD_CNT,
		CmdID_End, 0, 0, 0);
		break;

	case RF_8256:
		if (channel < 1 || channel > 14)
			RT_TRACE(COMP_ERR, "illegal channel for zebra:%d\n", channel);
		// TEST!! This is not the table for 8256!!
		//RT_ASSERT((channel >= 1 && channel <= 14), ("illegal channel for Zebra: %d\n", channel));
		phy_SetSwChnlCmdArray(RfDependCmd, RfDependCmdCnt++, MAX_RFDEPENDCMD_CNT,
			CmdID_RF_WriteReg, rRfChannel, channel, 10);
		phy_SetSwChnlCmdArray(RfDependCmd, RfDependCmdCnt++, MAX_RFDEPENDCMD_CNT,
		CmdID_End, 0, 0, 0);
		break;

	case RF_6052:
		if (channel < 1 || channel > 14)
			RT_TRACE(COMP_ERR, "illegal channel for zebra:%d\n", channel);
		phy_SetSwChnlCmdArray(RfDependCmd, RfDependCmdCnt++, MAX_RFDEPENDCMD_CNT,
			CmdID_RF_WriteReg, RF_CHNLBW, channel, 10);
		phy_SetSwChnlCmdArray(RfDependCmd, RfDependCmdCnt++, MAX_RFDEPENDCMD_CNT,
			CmdID_End, 0, 0, 0);
		break;

	case RF_8258:
		break;

	default:
		//RT_ASSERT(FALSE, ("Unknown rf_chip: %d\n", priv->rf_chip));
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
			//if(priv->card_8192_version > VERSION_8190_BD)
				PHY_SetTxPowerLevel8192S(dev,channel);
			break;
		case CmdID_WritePortUlong:
			write_nic_dword(dev, CurrentCmd->Para1, CurrentCmd->Para2);
			break;
		case CmdID_WritePortUshort:
			write_nic_word(dev, CurrentCmd->Para1, (u16)CurrentCmd->Para2);
			break;
		case CmdID_WritePortUchar:
			write_nic_byte(dev, CurrentCmd->Para1, (u8)CurrentCmd->Para2);
			break;
		case CmdID_RF_WriteReg:	// Only modify channel for the register now !!!!!
			for(eRFPath = 0; eRFPath <priv->NumTotalRFPath; eRFPath++)
			{
			// For new T65 RF 0222d register 0x18 bit 0-9 = channel number.
				rtl8192_phy_SetRFReg(dev, (RF90_RADIO_PATH_E)eRFPath, CurrentCmd->Para1, 0x1f, (CurrentCmd->Para2));
				//printk("====>%x, %x, read_back:%x\n", CurrentCmd->Para2,CurrentCmd->Para1, rtl8192_phy_QueryRFReg(dev, (RF90_RADIO_PATH_E)eRFPath, CurrentCmd->Para1, 0x1f));
			}
			break;
                default:
                        break;
		}

		break;
	}while(TRUE);
	//cosa }/*for(Number of RF paths)*/

	(*delay)=CurrentCmd->msDelay;
	(*step)++;
	RT_TRACE(COMP_CH, "<===========%s(), channel:%d, stage:%d, step:%d\n", __FUNCTION__, channel, *stage, *step);
	return FALSE;
}

//called PHY_SwChnlPhy8192S, SwChnlCallback8192SUsbWorkItem
//    use in phy only
static void
phy_FinishSwChnlNow(	// We should not call this function directly
	struct net_device* dev,
	u8		channel
		)
{
	struct r8192_priv 	*priv = ieee80211_priv(dev);
	u32			delay;

	while(!phy_SwChnlStepByStep(dev,channel,&priv->SwChnlStage,&priv->SwChnlStep,&delay))
	{
		if(delay>0)
			mdelay(delay);
		if(!priv->up)
			break;
	}
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
 //called by rtl8192_phy_QueryRFReg, rtl8192_phy_SetRFReg, PHY_SetRFPowerState8192SUsb
//extern	bool
//PHY_CheckIsLegalRfPath8192S(
//	struct net_device* dev,
//	u32	eRFPath)
u8 rtl8192_phy_CheckIsLegalRFPath(struct net_device* dev, u32 eRFPath)
{
//	struct r8192_priv *priv = ieee80211_priv(dev);
	bool				rtValue = TRUE;

	// NOt check RF Path now.!
	return	rtValue;

}	/* PHY_CheckIsLegalRfPath8192S */



/*-----------------------------------------------------------------------------
 * Function:	PHY_IQCalibrate8192S()
 *
 * Overview:	After all MAC/PHY/RF is configued. We must execute IQ calibration
 *			to improve RF EVM!!?
 *
 * Input:		IN	PADAPTER	pAdapter
 *
 * Output:		NONE
 *
 * Return:		NONE
 *
 * Revised History:
 *	When		Who		Remark
 *	10/07/2008	MHC		Create. Document from SD3 RFSI Jenyu.
 *
 *---------------------------------------------------------------------------*/
 //called by InitializeAdapter8192SE
void
PHY_IQCalibrate(	struct net_device* dev)
{
	//struct r8192_priv 	*priv = ieee80211_priv(dev);
	u32				i, reg;
	u32				old_value;
	long				X, Y, TX0[4];
	u32				TXA[4];

	// 1. Check QFN68 or 64 92S (Read from EEPROM)

	//
	// 2. QFN 68
	//
	// For 1T2R IQK only now !!!
	for (i = 0; i < 10; i++)
	{
		// IQK
		rtl8192_setBBreg(dev, 0xc04, bMaskDWord, 0x00a05430);
		//PlatformStallExecution(5);
		udelay(5);
		rtl8192_setBBreg(dev, 0xc08, bMaskDWord, 0x000800e4);
		udelay(5);
		rtl8192_setBBreg(dev, 0xe28, bMaskDWord, 0x80800000);
		udelay(5);
		rtl8192_setBBreg(dev, 0xe40, bMaskDWord, 0x02140148);
		udelay(5);
		rtl8192_setBBreg(dev, 0xe44, bMaskDWord, 0x681604a2);
		udelay(5);
		rtl8192_setBBreg(dev, 0xe4c, bMaskDWord, 0x000028d1);
		udelay(5);
		rtl8192_setBBreg(dev, 0xe60, bMaskDWord, 0x0214014d);
		udelay(5);
		rtl8192_setBBreg(dev, 0xe64, bMaskDWord, 0x281608ba);
		udelay(5);
		rtl8192_setBBreg(dev, 0xe6c, bMaskDWord, 0x000028d1);
		udelay(5);
		rtl8192_setBBreg(dev, 0xe48, bMaskDWord, 0xfb000001);
		udelay(5);
		rtl8192_setBBreg(dev, 0xe48, bMaskDWord, 0xf8000001);
		udelay(2000);
		rtl8192_setBBreg(dev, 0xc04, bMaskDWord, 0x00a05433);
		udelay(5);
		rtl8192_setBBreg(dev, 0xc08, bMaskDWord, 0x000000e4);
		udelay(5);
		rtl8192_setBBreg(dev, 0xe28, bMaskDWord, 0x0);


		reg = rtl8192_QueryBBReg(dev, 0xeac, bMaskDWord);

		// Readback IQK value and rewrite
		if (!(reg&(BIT27|BIT28|BIT30|BIT31)))
		{
			old_value = (rtl8192_QueryBBReg(dev, 0xc80, bMaskDWord) & 0x3FF);

			// Calibrate init gain for A path for TX0
			X = (rtl8192_QueryBBReg(dev, 0xe94, bMaskDWord) & 0x03FF0000)>>16;
			TXA[RF90_PATH_A] = (X * old_value)/0x100;
			reg = rtl8192_QueryBBReg(dev, 0xc80, bMaskDWord);
			reg = (reg & 0xFFFFFC00) | (u32)TXA[RF90_PATH_A];
			rtl8192_setBBreg(dev, 0xc80, bMaskDWord, reg);
			udelay(5);

			// Calibrate init gain for C path for TX0
			Y = ( rtl8192_QueryBBReg(dev, 0xe9C, bMaskDWord) & 0x03FF0000)>>16;
			TX0[RF90_PATH_C] = ((Y * old_value)/0x100);
			reg = rtl8192_QueryBBReg(dev, 0xc80, bMaskDWord);
			reg = (reg & 0xffc0ffff) |((u32) (TX0[RF90_PATH_C]&0x3F)<<16);
			rtl8192_setBBreg(dev, 0xc80, bMaskDWord, reg);
			reg = rtl8192_QueryBBReg(dev, 0xc94, bMaskDWord);
			reg = (reg & 0x0fffffff) |(((Y&0x3c0)>>6)<<28);
			rtl8192_setBBreg(dev, 0xc94, bMaskDWord, reg);
			udelay(5);

			// Calibrate RX A and B for RX0
			reg = rtl8192_QueryBBReg(dev, 0xc14, bMaskDWord);
			X = (rtl8192_QueryBBReg(dev, 0xea4, bMaskDWord) & 0x03FF0000)>>16;
			reg = (reg & 0xFFFFFC00) |X;
			rtl8192_setBBreg(dev, 0xc14, bMaskDWord, reg);
			Y = (rtl8192_QueryBBReg(dev, 0xeac, bMaskDWord) & 0x003F0000)>>16;
			reg = (reg & 0xFFFF03FF) |Y<<10;
			rtl8192_setBBreg(dev, 0xc14, bMaskDWord, reg);
			udelay(5);
			old_value = (rtl8192_QueryBBReg(dev, 0xc88, bMaskDWord) & 0x3FF);

			// Calibrate init gain for A path for TX1 !!!!!!
			X = (rtl8192_QueryBBReg(dev, 0xeb4, bMaskDWord) & 0x03FF0000)>>16;
			reg = rtl8192_QueryBBReg(dev, 0xc88, bMaskDWord);
			TXA[RF90_PATH_A] = (X * old_value) / 0x100;
			reg = (reg & 0xFFFFFC00) | TXA[RF90_PATH_A];
			rtl8192_setBBreg(dev, 0xc88, bMaskDWord, reg);
			udelay(5);

			// Calibrate init gain for C path for TX1
			Y = (rtl8192_QueryBBReg(dev, 0xebc, bMaskDWord)& 0x03FF0000)>>16;
			TX0[RF90_PATH_C] = ((Y * old_value)/0x100);
			reg = rtl8192_QueryBBReg(dev, 0xc88, bMaskDWord);
			reg = (reg & 0xffc0ffff) |( (TX0[RF90_PATH_C]&0x3F)<<16);
			rtl8192_setBBreg(dev, 0xc88, bMaskDWord, reg);
			reg = rtl8192_QueryBBReg(dev, 0xc9c, bMaskDWord);
			reg = (reg & 0x0fffffff) |(((Y&0x3c0)>>6)<<28);
			rtl8192_setBBreg(dev, 0xc9c, bMaskDWord, reg);
			udelay(5);

			// Calibrate RX A and B for RX1
			reg = rtl8192_QueryBBReg(dev, 0xc1c, bMaskDWord);
			X = (rtl8192_QueryBBReg(dev, 0xec4, bMaskDWord) & 0x03FF0000)>>16;
			reg = (reg & 0xFFFFFC00) |X;
			rtl8192_setBBreg(dev, 0xc1c, bMaskDWord, reg);

			Y = (rtl8192_QueryBBReg(dev, 0xecc, bMaskDWord) & 0x003F0000)>>16;
			reg = (reg & 0xFFFF03FF) |Y<<10;
			rtl8192_setBBreg(dev, 0xc1c, bMaskDWord, reg);
			udelay(5);

			RT_TRACE(COMP_INIT, "PHY_IQCalibrate OK\n");
			break;
		}

	}


	//
	// 3. QFN64. Not enabled now !!! We must use different gain table for 1T2R.
	//


}

/*-----------------------------------------------------------------------------
 * Function:	PHY_IQCalibrateBcut()
 *
 * Overview:	After all MAC/PHY/RF is configued. We must execute IQ calibration
 *			to improve RF EVM!!?
 *
 * Input:		IN	PADAPTER	pAdapter
 *
 * Output:		NONE
 *
 * Return:		NONE
 *
 * Revised History:
 *	When		Who		Remark
 *	11/18/2008	MHC		Create. Document from SD3 RFSI Jenyu.
 *						92S B-cut QFN 68 pin IQ calibration procedure.doc
 *
 *---------------------------------------------------------------------------*/
extern void PHY_IQCalibrateBcut(struct net_device* dev)
{
	//struct r8192_priv 	*priv = ieee80211_priv(dev);
	//PMGNT_INFO		pMgntInfo = &pAdapter->MgntInfo;
	u32				i, reg;
	u32				old_value;
	long				X, Y, TX0[4];
	u32				TXA[4];
	u32				calibrate_set[13] = {0};
	u32				load_value[13];
	u8				RfPiEnable=0;

	// 0. Check QFN68 or 64 92S (Read from EEPROM/EFUSE)

	//
	// 1. Save e70~ee0 register setting, and load calibration setting
	//
	/*
	0xee0[31:0]=0x3fed92fb;
	0xedc[31:0] =0x3fed92fb;
	0xe70[31:0] =0x3fed92fb;
	0xe74[31:0] =0x3fed92fb;
	0xe78[31:0] =0x3fed92fb;
	0xe7c[31:0]= 0x3fed92fb;
	0xe80[31:0]= 0x3fed92fb;
	0xe84[31:0]= 0x3fed92fb;
	0xe88[31:0]= 0x3fed92fb;
	0xe8c[31:0]= 0x3fed92fb;
	0xed0[31:0]= 0x3fed92fb;
	0xed4[31:0]= 0x3fed92fb;
	0xed8[31:0]= 0x3fed92fb;
	*/
	calibrate_set [0] = 0xee0;
	calibrate_set [1] = 0xedc;
	calibrate_set [2] = 0xe70;
	calibrate_set [3] = 0xe74;
	calibrate_set [4] = 0xe78;
	calibrate_set [5] = 0xe7c;
	calibrate_set [6] = 0xe80;
	calibrate_set [7] = 0xe84;
	calibrate_set [8] = 0xe88;
	calibrate_set [9] = 0xe8c;
	calibrate_set [10] = 0xed0;
	calibrate_set [11] = 0xed4;
	calibrate_set [12] = 0xed8;
	//RT_TRACE(COMP_INIT, DBG_LOUD, ("Save e70~ee0 register setting\n"));
	for (i = 0; i < 13; i++)
	{
		load_value[i] = rtl8192_QueryBBReg(dev, calibrate_set[i], bMaskDWord);
		rtl8192_setBBreg(dev, calibrate_set[i], bMaskDWord, 0x3fed92fb);

	}

	RfPiEnable = (u8)rtl8192_QueryBBReg(dev, rFPGA0_XA_HSSIParameter1, BIT8);

	//
	// 2. QFN 68
	//
	// For 1T2R IQK only now !!!
	for (i = 0; i < 10; i++)
	{
		RT_TRACE(COMP_INIT, "IQK -%d\n", i);
		//BB switch to PI mode. If default is PI mode, ignoring 2 commands below.
		if (!RfPiEnable)	//if original is SI mode, then switch to PI mode.
		{
			//DbgPrint("IQK Switch to PI mode\n");
			rtl8192_setBBreg(dev, 0x820, bMaskDWord, 0x01000100);
			rtl8192_setBBreg(dev, 0x828, bMaskDWord, 0x01000100);
		}

		// IQK
		// 2. IQ calibration & LO leakage calibration
		rtl8192_setBBreg(dev, 0xc04, bMaskDWord, 0x00a05430);
		udelay(5);
		rtl8192_setBBreg(dev, 0xc08, bMaskDWord, 0x000800e4);
		udelay(5);
		rtl8192_setBBreg(dev, 0xe28, bMaskDWord, 0x80800000);
		udelay(5);
		//path-A IQ K and LO K gain setting
		rtl8192_setBBreg(dev, 0xe40, bMaskDWord, 0x02140102);
		udelay(5);
		rtl8192_setBBreg(dev, 0xe44, bMaskDWord, 0x681604c2);
		udelay(5);
		//set LO calibration
		rtl8192_setBBreg(dev, 0xe4c, bMaskDWord, 0x000028d1);
		udelay(5);
		//path-B IQ K and LO K gain setting
		rtl8192_setBBreg(dev, 0xe60, bMaskDWord, 0x02140102);
		udelay(5);
		rtl8192_setBBreg(dev, 0xe64, bMaskDWord, 0x28160d05);
		udelay(5);
		//K idac_I & IQ
		rtl8192_setBBreg(dev, 0xe48, bMaskDWord, 0xfb000000);
		udelay(5);
		rtl8192_setBBreg(dev, 0xe48, bMaskDWord, 0xf8000000);
		udelay(5);

		// delay 2ms
		udelay(2000);

		//idac_Q setting
		rtl8192_setBBreg(dev, 0xe6c, bMaskDWord, 0x020028d1);
		udelay(5);
		//K idac_Q & IQ
		rtl8192_setBBreg(dev, 0xe48, bMaskDWord, 0xfb000000);
		udelay(5);
		rtl8192_setBBreg(dev, 0xe48, bMaskDWord, 0xf8000000);

		// delay 2ms
		udelay(2000);

		rtl8192_setBBreg(dev, 0xc04, bMaskDWord, 0x00a05433);
		udelay(5);
		rtl8192_setBBreg(dev, 0xc08, bMaskDWord, 0x000000e4);
		udelay(5);
		rtl8192_setBBreg(dev, 0xe28, bMaskDWord, 0x0);

		if (!RfPiEnable)	//if original is SI mode, then switch to PI mode.
		{
			//DbgPrint("IQK Switch back to SI mode\n");
			rtl8192_setBBreg(dev, 0x820, bMaskDWord, 0x01000000);
			rtl8192_setBBreg(dev, 0x828, bMaskDWord, 0x01000000);
		}


		reg = rtl8192_QueryBBReg(dev, 0xeac, bMaskDWord);

		// 3.	check fail bit, and fill BB IQ matrix
		// Readback IQK value and rewrite
		if (!(reg&(BIT27|BIT28|BIT30|BIT31)))
		{
			old_value = (rtl8192_QueryBBReg(dev, 0xc80, bMaskDWord) & 0x3FF);

			// Calibrate init gain for A path for TX0
			X = (rtl8192_QueryBBReg(dev, 0xe94, bMaskDWord) & 0x03FF0000)>>16;
			TXA[RF90_PATH_A] = (X * old_value)/0x100;
			reg = rtl8192_QueryBBReg(dev, 0xc80, bMaskDWord);
			reg = (reg & 0xFFFFFC00) | (u32)TXA[RF90_PATH_A];
			rtl8192_setBBreg(dev, 0xc80, bMaskDWord, reg);
			udelay(5);

			// Calibrate init gain for C path for TX0
			Y = ( rtl8192_QueryBBReg(dev, 0xe9C, bMaskDWord) & 0x03FF0000)>>16;
			TX0[RF90_PATH_C] = ((Y * old_value)/0x100);
			reg = rtl8192_QueryBBReg(dev, 0xc80, bMaskDWord);
			reg = (reg & 0xffc0ffff) |((u32) (TX0[RF90_PATH_C]&0x3F)<<16);
			rtl8192_setBBreg(dev, 0xc80, bMaskDWord, reg);
			reg = rtl8192_QueryBBReg(dev, 0xc94, bMaskDWord);
			reg = (reg & 0x0fffffff) |(((Y&0x3c0)>>6)<<28);
			rtl8192_setBBreg(dev, 0xc94, bMaskDWord, reg);
			udelay(5);

			// Calibrate RX A and B for RX0
			reg = rtl8192_QueryBBReg(dev, 0xc14, bMaskDWord);
			X = (rtl8192_QueryBBReg(dev, 0xea4, bMaskDWord) & 0x03FF0000)>>16;
			reg = (reg & 0xFFFFFC00) |X;
			rtl8192_setBBreg(dev, 0xc14, bMaskDWord, reg);
			Y = (rtl8192_QueryBBReg(dev, 0xeac, bMaskDWord) & 0x003F0000)>>16;
			reg = (reg & 0xFFFF03FF) |Y<<10;
			rtl8192_setBBreg(dev, 0xc14, bMaskDWord, reg);
			udelay(5);
			old_value = (rtl8192_QueryBBReg(dev, 0xc88, bMaskDWord) & 0x3FF);

			// Calibrate init gain for A path for TX1 !!!!!!
			X = (rtl8192_QueryBBReg(dev, 0xeb4, bMaskDWord) & 0x03FF0000)>>16;
			reg = rtl8192_QueryBBReg(dev, 0xc88, bMaskDWord);
			TXA[RF90_PATH_A] = (X * old_value) / 0x100;
			reg = (reg & 0xFFFFFC00) | TXA[RF90_PATH_A];
			rtl8192_setBBreg(dev, 0xc88, bMaskDWord, reg);
			udelay(5);

			// Calibrate init gain for C path for TX1
			Y = (rtl8192_QueryBBReg(dev, 0xebc, bMaskDWord)& 0x03FF0000)>>16;
			TX0[RF90_PATH_C] = ((Y * old_value)/0x100);
			reg = rtl8192_QueryBBReg(dev, 0xc88, bMaskDWord);
			reg = (reg & 0xffc0ffff) |( (TX0[RF90_PATH_C]&0x3F)<<16);
			rtl8192_setBBreg(dev, 0xc88, bMaskDWord, reg);
			reg = rtl8192_QueryBBReg(dev, 0xc9c, bMaskDWord);
			reg = (reg & 0x0fffffff) |(((Y&0x3c0)>>6)<<28);
			rtl8192_setBBreg(dev, 0xc9c, bMaskDWord, reg);
			udelay(5);

			// Calibrate RX A and B for RX1
			reg = rtl8192_QueryBBReg(dev, 0xc1c, bMaskDWord);
			X = (rtl8192_QueryBBReg(dev, 0xec4, bMaskDWord) & 0x03FF0000)>>16;
			reg = (reg & 0xFFFFFC00) |X;
			rtl8192_setBBreg(dev, 0xc1c, bMaskDWord, reg);

			Y = (rtl8192_QueryBBReg(dev, 0xecc, bMaskDWord) & 0x003F0000)>>16;
			reg = (reg & 0xFFFF03FF) |Y<<10;
			rtl8192_setBBreg(dev, 0xc1c, bMaskDWord, reg);
			udelay(5);

			RT_TRACE(COMP_INIT, "PHY_IQCalibrate OK\n");
			break;
		}

	}

	//
	// 4. Reload e70~ee0 register setting.
	//
	//RT_TRACE(COMP_INIT, DBG_LOUD, ("Reload e70~ee0 register setting.\n"));
	for (i = 0; i < 13; i++)
		rtl8192_setBBreg(dev, calibrate_set[i], bMaskDWord, load_value[i]);


	//
	// 3. QFN64. Not enabled now !!! We must use different gain table for 1T2R.
	//



}	// PHY_IQCalibrateBcut


//
// Move from phycfg.c to gen.c to be code independent later
//
//-------------------------Move to other DIR later----------------------------*/
//#if (DEV_BUS_TYPE == USB_INTERFACE)

//    use in phy only (in win it's timer)
void SwChnlCallback8192SUsb(struct net_device *dev)
{

	struct r8192_priv *priv = ieee80211_priv(dev);
	u32			delay;
//	bool			ret;

	RT_TRACE(COMP_SCAN, "==>SwChnlCallback8190Pci(), switch to channel\
				%d\n", priv->chan);


	if(!priv->up)
		return;

	if(priv->rf_chip == RF_PSEUDO_11N)
	{
		priv->SwChnlInProgress=FALSE;
		return; 								//return immediately if it is peudo-phy
	}

	do{
		if(!priv->SwChnlInProgress)
			break;

		if(!phy_SwChnlStepByStep(dev, priv->chan, &priv->SwChnlStage, &priv->SwChnlStep, &delay))
		{
			if(delay>0)
			{
				//PlatformSetTimer(dev, &priv->SwChnlTimer, delay);

			}
			else
			continue;
		}
		else
		{
			priv->SwChnlInProgress=FALSE;
		}
		break;
	}while(TRUE);
}


//
// Callback routine of the work item for switch channel.
//
//    use in phy only (in win it's work)
void SwChnlCallback8192SUsbWorkItem(struct net_device *dev )
{
	struct r8192_priv *priv = ieee80211_priv(dev);

	RT_TRACE(COMP_TRACE, "==> SwChnlCallback8192SUsbWorkItem()\n");
#ifdef TO_DO_LIST
	if(pAdapter->bInSetPower && RT_USB_CANNOT_IO(pAdapter))
	{
		RT_TRACE(COMP_SCAN, DBG_LOUD, ("<== SwChnlCallback8192SUsbWorkItem() SwChnlInProgress FALSE driver sleep or unload\n"));

		pHalData->SwChnlInProgress = FALSE;
		return;
	}
#endif
	phy_FinishSwChnlNow(dev, priv->chan);
	priv->SwChnlInProgress = FALSE;

	RT_TRACE(COMP_TRACE, "<== SwChnlCallback8192SUsbWorkItem()\n");
}


/*-----------------------------------------------------------------------------
 * Function:    SetBWModeCallback8192SUsb()
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
//====>//rtl8192_SetBWMode
//    use in phy only (in win it's timer)
void SetBWModeCallback8192SUsb(struct net_device *dev)
{
	struct r8192_priv *priv = ieee80211_priv(dev);
	u8	 			regBwOpMode;

	// Added it for 20/40 mhz switch time evaluation by guangan 070531
	//u32				NowL, NowH;
	//u8Byte				BeginTime, EndTime;
	u8				regRRSR_RSC;

	RT_TRACE(COMP_SCAN, "==>SetBWModeCallback8190Pci()  Switch to %s bandwidth\n", \
					priv->CurrentChannelBW == HT_CHANNEL_WIDTH_20?"20MHz":"40MHz");

	if(priv->rf_chip == RF_PSEUDO_11N)
	{
		priv->SetBWModeInProgress= FALSE;
		return;
	}

	if(!priv->up)
		return;

	// Added it for 20/40 mhz switch time evaluation by guangan 070531
	//NowL = read_nic_dword(dev, TSFR);
	//NowH = read_nic_dword(dev, TSFR+4);
	//BeginTime = ((u8Byte)NowH << 32) + NowL;

	//3<1>Set MAC register
	regBwOpMode = read_nic_byte(dev, BW_OPMODE);
	regRRSR_RSC = read_nic_byte(dev, RRSR+2);

	switch(priv->CurrentChannelBW)
	{
		case HT_CHANNEL_WIDTH_20:
			regBwOpMode |= BW_OPMODE_20MHZ;
		       // 2007/02/07 Mark by Emily becasue we have not verify whether this register works
			write_nic_byte(dev, BW_OPMODE, regBwOpMode);
			break;

		case HT_CHANNEL_WIDTH_20_40:
			regBwOpMode &= ~BW_OPMODE_20MHZ;
        		// 2007/02/07 Mark by Emily becasue we have not verify whether this register works
			write_nic_byte(dev, BW_OPMODE, regBwOpMode);

			regRRSR_RSC = (regRRSR_RSC&0x90) |(priv->nCur40MhzPrimeSC<<5);
			write_nic_byte(dev, RRSR+2, regRRSR_RSC);
			break;

		default:
			RT_TRACE(COMP_DBG, "SetChannelBandwidth8190Pci():\
						unknown Bandwidth: %#X\n",priv->CurrentChannelBW);
			break;
	}

	//3 <2>Set PHY related register
	switch(priv->CurrentChannelBW)
	{
		case HT_CHANNEL_WIDTH_20:
			rtl8192_setBBreg(dev, rFPGA0_RFMOD, bRFMOD, 0x0);
			rtl8192_setBBreg(dev, rFPGA1_RFMOD, bRFMOD, 0x0);

			if (priv->card_8192_version >= VERSION_8192S_BCUT)
				rtl8192_setBBreg(dev, rFPGA0_AnalogParameter2, 0xff, 0x58);

			break;
		case HT_CHANNEL_WIDTH_20_40:
			rtl8192_setBBreg(dev, rFPGA0_RFMOD, bRFMOD, 0x1);
			rtl8192_setBBreg(dev, rFPGA1_RFMOD, bRFMOD, 0x1);
			rtl8192_setBBreg(dev, rCCK0_System, bCCKSideBand, (priv->nCur40MhzPrimeSC>>1));
			rtl8192_setBBreg(dev, rOFDM1_LSTF, 0xC00, priv->nCur40MhzPrimeSC);

			// Correct the tx power for CCK rate in 40M. Suggest by YN, 20071207
			//PHY_SetBBReg(Adapter, rCCK0_TxFilter1, bMaskDWord, 0x35360000);
			//PHY_SetBBReg(Adapter, rCCK0_TxFilter2, bMaskDWord, 0x121c252e);
			//PHY_SetBBReg(Adapter, rCCK0_DebugPort, bMaskDWord, 0x00000409);
			//PHY_SetBBReg(Adapter, rFPGA0_AnalogParameter1, bADClkPhase, 0);

			if (priv->card_8192_version >= VERSION_8192S_BCUT)
				rtl8192_setBBreg(dev, rFPGA0_AnalogParameter2, 0xff, 0x18);

			break;
		default:
			RT_TRACE(COMP_DBG, "SetChannelBandwidth8190Pci(): unknown Bandwidth: %#X\n"\
						,priv->CurrentChannelBW);
			break;

	}
	//Skip over setting of J-mode in BB register here. Default value is "None J mode". Emily 20070315

	// Added it for 20/40 mhz switch time evaluation by guangan 070531
	//NowL = read_nic_dword(dev, TSFR);
	//NowH = read_nic_dword(dev, TSFR+4);
	//EndTime = ((u8Byte)NowH << 32) + NowL;
	//RT_TRACE(COMP_SCAN, DBG_LOUD, ("SetBWModeCallback8190Pci: time of SetBWMode = %I64d us!\n", (EndTime - BeginTime)));

#if 1
	//3<3>Set RF related register
	switch( priv->rf_chip )
	{
		case RF_8225:
			PHY_SetRF8225Bandwidth(dev, priv->CurrentChannelBW);
			break;

		case RF_8256:
			// Please implement this function in Hal8190PciPhy8256.c
			//PHY_SetRF8256Bandwidth(dev, priv->CurrentChannelBW);
			break;

		case RF_6052:
			PHY_RF6052SetBandwidth(dev, priv->CurrentChannelBW);
			break;

		case RF_8258:
			// Please implement this function in Hal8190PciPhy8258.c
			// PHY_SetRF8258Bandwidth();
			break;

		case RF_PSEUDO_11N:
			// Do Nothing
			break;

		default:
			//RT_ASSERT(FALSE, ("Unknown rf_chip: %d\n", priv->rf_chip));
			break;
	}
#endif
	priv->SetBWModeInProgress= FALSE;

	RT_TRACE(COMP_SCAN, "<==SetBWMode8190Pci()" );
}

//
// Callback routine of the work item for set bandwidth mode.
//
//    use in phy only (in win it's work)
void SetBWModeCallback8192SUsbWorkItem(struct net_device *dev)
{
	struct r8192_priv 		*priv = ieee80211_priv(dev);
	u8	 			regBwOpMode;

	// Added it for 20/40 mhz switch time evaluation by guangan 070531
	//u32				NowL, NowH;
	//u8Byte				BeginTime, EndTime;
	u8			regRRSR_RSC;

	RT_TRACE(COMP_SCAN, "==>SetBWModeCallback8192SUsbWorkItem()  Switch to %s bandwidth\n", \
					priv->CurrentChannelBW == HT_CHANNEL_WIDTH_20?"20MHz":"40MHz");

	if(priv->rf_chip == RF_PSEUDO_11N)
	{
		priv->SetBWModeInProgress= FALSE;
		return;
	}

	if(!priv->up)
		return;

	// Added it for 20/40 mhz switch time evaluation by guangan 070531
	//NowL = read_nic_dword(dev, TSFR);
	//NowH = read_nic_dword(dev, TSFR+4);
	//BeginTime = ((u8Byte)NowH << 32) + NowL;

	//3<1>Set MAC register
	regBwOpMode = read_nic_byte(dev, BW_OPMODE);
	regRRSR_RSC = read_nic_byte(dev, RRSR+2);

	switch(priv->CurrentChannelBW)
	{
		case HT_CHANNEL_WIDTH_20:
			regBwOpMode |= BW_OPMODE_20MHZ;
		       // 2007/02/07 Mark by Emily becasue we have not verify whether this register works
			write_nic_byte(dev, BW_OPMODE, regBwOpMode);
			break;

		case HT_CHANNEL_WIDTH_20_40:
			regBwOpMode &= ~BW_OPMODE_20MHZ;
        		// 2007/02/07 Mark by Emily becasue we have not verify whether this register works
			write_nic_byte(dev, BW_OPMODE, regBwOpMode);
			regRRSR_RSC = (regRRSR_RSC&0x90) |(priv->nCur40MhzPrimeSC<<5);
			write_nic_byte(dev, RRSR+2, regRRSR_RSC);

			break;

		default:
			RT_TRACE(COMP_DBG, "SetBWModeCallback8192SUsbWorkItem():\
						unknown Bandwidth: %#X\n",priv->CurrentChannelBW);
			break;
	}

	//3 <2>Set PHY related register
	switch(priv->CurrentChannelBW)
	{
		case HT_CHANNEL_WIDTH_20:
			rtl8192_setBBreg(dev, rFPGA0_RFMOD, bRFMOD, 0x0);
			rtl8192_setBBreg(dev, rFPGA1_RFMOD, bRFMOD, 0x0);

			rtl8192_setBBreg(dev, rFPGA0_AnalogParameter2, 0xff, 0x58);

			break;
		case HT_CHANNEL_WIDTH_20_40:
			rtl8192_setBBreg(dev, rFPGA0_RFMOD, bRFMOD, 0x1);
			rtl8192_setBBreg(dev, rFPGA1_RFMOD, bRFMOD, 0x1);

			// Set Control channel to upper or lower. These settings are required only for 40MHz
			rtl8192_setBBreg(dev, rCCK0_System, bCCKSideBand, (priv->nCur40MhzPrimeSC>>1));
			rtl8192_setBBreg(dev, rOFDM1_LSTF, 0xC00, priv->nCur40MhzPrimeSC);

			rtl8192_setBBreg(dev, rFPGA0_AnalogParameter2, 0xff, 0x18);

			break;


		default:
			RT_TRACE(COMP_DBG, "SetBWModeCallback8192SUsbWorkItem(): unknown Bandwidth: %#X\n"\
						,priv->CurrentChannelBW);
			break;

	}
	//Skip over setting of J-mode in BB register here. Default value is "None J mode". Emily 20070315

	//3<3>Set RF related register
	switch( priv->rf_chip )
	{
		case RF_8225:
			PHY_SetRF8225Bandwidth(dev, priv->CurrentChannelBW);
			break;

		case RF_8256:
			// Please implement this function in Hal8190PciPhy8256.c
			//PHY_SetRF8256Bandwidth(dev, priv->CurrentChannelBW);
			break;

		case RF_6052:
			PHY_RF6052SetBandwidth(dev, priv->CurrentChannelBW);
			break;

		case RF_8258:
			// Please implement this function in Hal8190PciPhy8258.c
			// PHY_SetRF8258Bandwidth();
			break;

		case RF_PSEUDO_11N:
			// Do Nothing
			break;

		default:
			//RT_ASSERT(FALSE, ("Unknown rf_chip: %d\n", priv->rf_chip));
			break;
	}

	priv->SetBWModeInProgress= FALSE;

	RT_TRACE(COMP_SCAN, "<==SetBWModeCallback8192SUsbWorkItem()" );
}

//--------------------------Move to oter DIR later-------------------------------*/
void InitialGain8192S(struct net_device *dev,	u8 Operation)
{
#ifdef TO_DO_LIST
	struct r8192_priv *priv = ieee80211_priv(dev);
#endif

}

void InitialGain819xUsb(struct net_device *dev,	u8 Operation)
{
	struct r8192_priv *priv = ieee80211_priv(dev);

	priv->InitialGainOperateType = Operation;

	if(priv->up)
	{
		queue_delayed_work(priv->priv_wq,&priv->initialgain_operate_wq,0);
	}
}

extern void InitialGainOperateWorkItemCallBack(struct work_struct *work)
{
	struct delayed_work *dwork = container_of(work,struct delayed_work,work);
       struct r8192_priv *priv = container_of(dwork,struct r8192_priv,initialgain_operate_wq);
       struct net_device *dev = priv->ieee80211->dev;
#define SCAN_RX_INITIAL_GAIN	0x17
#define POWER_DETECTION_TH	0x08
	u32	BitMask;
	u8	initial_gain;
	u8	Operation;

	Operation = priv->InitialGainOperateType;

	switch(Operation)
	{
		case IG_Backup:
			RT_TRACE(COMP_SCAN, "IG_Backup, backup the initial gain.\n");
			initial_gain = SCAN_RX_INITIAL_GAIN;//priv->DefaultInitialGain[0];//
			BitMask = bMaskByte0;
			if(dm_digtable.dig_algorithm == DIG_ALGO_BY_FALSE_ALARM)
				rtl8192_setBBreg(dev, UFWP, bMaskByte1, 0x8);	// FW DIG OFF
			priv->initgain_backup.xaagccore1 = (u8)rtl8192_QueryBBReg(dev, rOFDM0_XAAGCCore1, BitMask);
			priv->initgain_backup.xbagccore1 = (u8)rtl8192_QueryBBReg(dev, rOFDM0_XBAGCCore1, BitMask);
			priv->initgain_backup.xcagccore1 = (u8)rtl8192_QueryBBReg(dev, rOFDM0_XCAGCCore1, BitMask);
			priv->initgain_backup.xdagccore1 = (u8)rtl8192_QueryBBReg(dev, rOFDM0_XDAGCCore1, BitMask);
			BitMask  = bMaskByte2;
			priv->initgain_backup.cca		= (u8)rtl8192_QueryBBReg(dev, rCCK0_CCA, BitMask);

			RT_TRACE(COMP_SCAN, "Scan InitialGainBackup 0xc50 is %x\n",priv->initgain_backup.xaagccore1);
			RT_TRACE(COMP_SCAN, "Scan InitialGainBackup 0xc58 is %x\n",priv->initgain_backup.xbagccore1);
			RT_TRACE(COMP_SCAN, "Scan InitialGainBackup 0xc60 is %x\n",priv->initgain_backup.xcagccore1);
			RT_TRACE(COMP_SCAN, "Scan InitialGainBackup 0xc68 is %x\n",priv->initgain_backup.xdagccore1);
			RT_TRACE(COMP_SCAN, "Scan InitialGainBackup 0xa0a is %x\n",priv->initgain_backup.cca);

			RT_TRACE(COMP_SCAN, "Write scan initial gain = 0x%x \n", initial_gain);
			write_nic_byte(dev, rOFDM0_XAAGCCore1, initial_gain);
			write_nic_byte(dev, rOFDM0_XBAGCCore1, initial_gain);
			write_nic_byte(dev, rOFDM0_XCAGCCore1, initial_gain);
			write_nic_byte(dev, rOFDM0_XDAGCCore1, initial_gain);
			RT_TRACE(COMP_SCAN, "Write scan 0xa0a = 0x%x \n", POWER_DETECTION_TH);
			write_nic_byte(dev, 0xa0a, POWER_DETECTION_TH);
			break;
		case IG_Restore:
			RT_TRACE(COMP_SCAN, "IG_Restore, restore the initial gain.\n");
			BitMask = 0x7f; //Bit0~ Bit6
			if(dm_digtable.dig_algorithm == DIG_ALGO_BY_FALSE_ALARM)
				rtl8192_setBBreg(dev, UFWP, bMaskByte1, 0x8);	// FW DIG OFF

			rtl8192_setBBreg(dev, rOFDM0_XAAGCCore1, BitMask, (u32)priv->initgain_backup.xaagccore1);
			rtl8192_setBBreg(dev, rOFDM0_XBAGCCore1, BitMask, (u32)priv->initgain_backup.xbagccore1);
			rtl8192_setBBreg(dev, rOFDM0_XCAGCCore1, BitMask, (u32)priv->initgain_backup.xcagccore1);
			rtl8192_setBBreg(dev, rOFDM0_XDAGCCore1, BitMask, (u32)priv->initgain_backup.xdagccore1);
			BitMask  = bMaskByte2;
			rtl8192_setBBreg(dev, rCCK0_CCA, BitMask, (u32)priv->initgain_backup.cca);

			RT_TRACE(COMP_SCAN, "Scan BBInitialGainRestore 0xc50 is %x\n",priv->initgain_backup.xaagccore1);
			RT_TRACE(COMP_SCAN, "Scan BBInitialGainRestore 0xc58 is %x\n",priv->initgain_backup.xbagccore1);
			RT_TRACE(COMP_SCAN, "Scan BBInitialGainRestore 0xc60 is %x\n",priv->initgain_backup.xcagccore1);
			RT_TRACE(COMP_SCAN, "Scan BBInitialGainRestore 0xc68 is %x\n",priv->initgain_backup.xdagccore1);
			RT_TRACE(COMP_SCAN, "Scan BBInitialGainRestore 0xa0a is %x\n",priv->initgain_backup.cca);

			PHY_SetTxPowerLevel8192S(dev,priv->ieee80211->current_network.channel);

			if(dm_digtable.dig_algorithm == DIG_ALGO_BY_FALSE_ALARM)
				rtl8192_setBBreg(dev, UFWP, bMaskByte1, 0x1);	// FW DIG ON
			break;
		default:
			RT_TRACE(COMP_SCAN, "Unknown IG Operation. \n");
			break;
	}
}


//-----------------------------------------------------------------------------
//	Description:
//		Schedule workitem to send specific CMD IO to FW.
//	Added by Roger, 2008.12.03.
//
//-----------------------------------------------------------------------------
bool HalSetFwCmd8192S(struct net_device* dev, FW_CMD_IO_TYPE	FwCmdIO)
{
	struct r8192_priv *priv = ieee80211_priv(dev);
	u16	FwCmdWaitCounter = 0;

	u16	FwCmdWaitLimit = 1000;

	//if(IS_HARDWARE_TYPE_8192SU(Adapter) && Adapter->bInHctTest)
	if(priv->bInHctTest)
		return true;

	RT_TRACE(COMP_CMD, "-->HalSetFwCmd8192S(): Set FW Cmd(%x), SetFwCmdInProgress(%d)\n", (u32)FwCmdIO, priv->SetFwCmdInProgress);

	// Will be done by high power respectively.
	if(FwCmdIO==FW_CMD_DIG_HALT || FwCmdIO==FW_CMD_DIG_RESUME)
	{
		RT_TRACE(COMP_CMD, "<--HalSetFwCmd8192S(): Set FW Cmd(%x)\n", (u32)FwCmdIO);
		return false;
	}

#if 1
	while(priv->SetFwCmdInProgress && FwCmdWaitCounter<FwCmdWaitLimit)
	{
		//if(RT_USB_CANNOT_IO(Adapter))
		//{
		//	RT_TRACE(COMP_CMD, DBG_WARNING, ("HalSetFwCmd8192S(): USB can NOT IO!!\n"));
		//	return FALSE;
		//}

		RT_TRACE(COMP_CMD, "HalSetFwCmd8192S(): previous workitem not finish!!\n");
		return false;
		FwCmdWaitCounter ++;
		RT_TRACE(COMP_CMD, "HalSetFwCmd8192S(): Wait 10 ms (%d times)...\n", FwCmdWaitCounter);
		udelay(100);
	}

	if(FwCmdWaitCounter == FwCmdWaitLimit)
	{
		//RT_ASSERT(FALSE, ("SetFwCmdIOWorkItemCallback(): Wait too logn to set FW CMD\n"));
		RT_TRACE(COMP_CMD, "HalSetFwCmd8192S(): Wait too logn to set FW CMD\n");
		//return false;
	}
#endif
	if (priv->SetFwCmdInProgress)
	{
		RT_TRACE(COMP_ERR, "<--HalSetFwCmd8192S(): Set FW Cmd(%#x)\n", FwCmdIO);
		return false;
	}
	priv->SetFwCmdInProgress = TRUE;
	priv->CurrentFwCmdIO = FwCmdIO; // Update current FW Cmd for callback use.

	phy_SetFwCmdIOCallback(dev);
	return true;
}
void ChkFwCmdIoDone(struct net_device* dev)
{
	u16 PollingCnt = 1000;
	u32 tmpValue;

	do
	{// Make sure that CMD IO has be accepted by FW.
#ifdef TO_DO_LIST
		if(RT_USB_CANNOT_IO(Adapter))
		{
			RT_TRACE(COMP_CMD, "ChkFwCmdIoDone(): USB can NOT IO!!\n");
			return;
		}
#endif
		udelay(10); // sleep 20us
		tmpValue = read_nic_dword(dev, WFM5);
		if(tmpValue == 0)
		{
			RT_TRACE(COMP_CMD, "[FW CMD] Set FW Cmd success!!\n");
			break;
		}
		else
		{
			RT_TRACE(COMP_CMD, "[FW CMD] Polling FW Cmd PollingCnt(%d)!!\n", PollingCnt);
		}
	}while( --PollingCnt );

	if(PollingCnt == 0)
	{
		RT_TRACE(COMP_ERR, "[FW CMD] Set FW Cmd fail!!\n");
	}
}
// 	Callback routine of the timer callback for FW Cmd IO.
//
//	Description:
//		This routine will send specific CMD IO to FW and check whether it is done.
//
void phy_SetFwCmdIOCallback(struct net_device* dev)
{
	//struct net_device* dev = (struct net_device*) data;
	u32 	 	input;
	static u32 ScanRegister;
	struct r8192_priv *priv = ieee80211_priv(dev);
	if(!priv->up)
	{
		RT_TRACE(COMP_CMD, "SetFwCmdIOTimerCallback(): driver is going to unload\n");
		return;
	}

	RT_TRACE(COMP_CMD, "--->SetFwCmdIOTimerCallback(): Cmd(%#x), SetFwCmdInProgress(%d)\n", priv->CurrentFwCmdIO, priv->SetFwCmdInProgress);

	switch(priv->CurrentFwCmdIO)
	{
		case FW_CMD_HIGH_PWR_ENABLE:
			if((priv->ieee80211->pHTInfo->IOTAction & HT_IOT_ACT_DISABLE_HIGH_POWER)==0)
				write_nic_dword(dev, WFM5, FW_HIGH_PWR_ENABLE);
			break;

		case FW_CMD_HIGH_PWR_DISABLE:
			write_nic_dword(dev, WFM5, FW_HIGH_PWR_DISABLE);
			break;

		case FW_CMD_DIG_RESUME:
			write_nic_dword(dev, WFM5, FW_DIG_RESUME);
			break;

		case FW_CMD_DIG_HALT:
			write_nic_dword(dev, WFM5, FW_DIG_HALT);
			break;

		//
		// <Roger_Notes> The following FW CMD IO was combined into single operation
		// (i.e., to prevent number of system workitem out of resource!!).
		// 2008.12.04.
		//
		case FW_CMD_RESUME_DM_BY_SCAN:
			RT_TRACE(COMP_CMD, "[FW CMD] Set HIGHPWR enable and DIG resume!!\n");
			if((priv->ieee80211->pHTInfo->IOTAction & HT_IOT_ACT_DISABLE_HIGH_POWER)==0)
			{
				write_nic_dword(dev, WFM5, FW_HIGH_PWR_ENABLE); //break;
				ChkFwCmdIoDone(dev);
			}
			write_nic_dword(dev, WFM5, FW_DIG_RESUME);
			break;

		case FW_CMD_PAUSE_DM_BY_SCAN:
			RT_TRACE(COMP_CMD, "[FW CMD] Set HIGHPWR disable and DIG halt!!\n");
			write_nic_dword(dev, WFM5, FW_HIGH_PWR_DISABLE); //break;
			ChkFwCmdIoDone(dev);
			write_nic_dword(dev, WFM5, FW_DIG_HALT);
			break;

		//
		// <Roger_Notes> The following FW CMD IO should be checked
		// (i.e., workitem schedule timing issue!!).
		// 2008.12.04.
		//
		case FW_CMD_DIG_DISABLE:
			RT_TRACE(COMP_CMD, "[FW CMD] Set DIG disable!!\n");
			write_nic_dword(dev, WFM5, FW_DIG_DISABLE);
			break;

		case FW_CMD_DIG_ENABLE:
			RT_TRACE(COMP_CMD, "[FW CMD] Set DIG enable!!\n");
			write_nic_dword(dev, WFM5, FW_DIG_ENABLE);
			break;

		case FW_CMD_RA_RESET:
			write_nic_dword(dev, WFM5, FW_RA_RESET);
			break;

		case FW_CMD_RA_ACTIVE:
			write_nic_dword(dev, WFM5, FW_RA_ACTIVE);
			break;

		case FW_CMD_RA_REFRESH_N:
			RT_TRACE(COMP_CMD, "[FW CMD] Set RA refresh!! N\n");
			if(priv->ieee80211->pHTInfo->IOTRaFunc & HT_IOT_RAFUNC_DISABLE_ALL)
				input = FW_RA_REFRESH;
			else
				input = FW_RA_REFRESH | (priv->ieee80211->pHTInfo->IOTRaFunc << 8);
			write_nic_dword(dev, WFM5, input);
			break;
		case FW_CMD_RA_REFRESH_BG:
			RT_TRACE(COMP_CMD, "[FW CMD] Set RA refresh!! B/G\n");
			write_nic_dword(dev, WFM5, FW_RA_REFRESH);
			ChkFwCmdIoDone(dev);
			write_nic_dword(dev, WFM5, FW_RA_ENABLE_BG);
			break;

		case FW_CMD_IQK_ENABLE:
			write_nic_dword(dev, WFM5, FW_IQK_ENABLE);
			break;

		case FW_CMD_TXPWR_TRACK_ENABLE:
			write_nic_dword(dev, WFM5, FW_TXPWR_TRACK_ENABLE);
			break;

		case FW_CMD_TXPWR_TRACK_DISABLE:
			write_nic_dword(dev, WFM5, FW_TXPWR_TRACK_DISABLE);
			break;

		default:
			RT_TRACE(COMP_CMD,"Unknown FW Cmd IO(%#x)\n", priv->CurrentFwCmdIO);
			break;
	}

	ChkFwCmdIoDone(dev);

	switch(priv->CurrentFwCmdIO)
	{
		case FW_CMD_HIGH_PWR_DISABLE:
			//if(pMgntInfo->bTurboScan)
			{
				//Lower initial gain
				rtl8192_setBBreg(dev, rOFDM0_XAAGCCore1, bMaskByte0, 0x17);
				rtl8192_setBBreg(dev, rOFDM0_XBAGCCore1, bMaskByte0, 0x17);
				// CCA threshold
				rtl8192_setBBreg(dev, rCCK0_CCA, bMaskByte2, 0x40);
				// Disable OFDM Part
				rtl8192_setBBreg(dev, rOFDM0_TRMuxPar, bMaskByte2, 0x1);
				ScanRegister = rtl8192_QueryBBReg(dev, rOFDM0_RxDetector1,bMaskDWord);
				rtl8192_setBBreg(dev, rOFDM0_RxDetector1, 0xf, 0xf);
				rtl8192_setBBreg(dev, rOFDM1_TRxPathEnable, 0xf, 0x0);
			}
			break;

		case FW_CMD_HIGH_PWR_ENABLE:
			//if(pMgntInfo->bTurboScan)
			{
				rtl8192_setBBreg(dev, rOFDM0_XAAGCCore1, bMaskByte0, 0x36);
				rtl8192_setBBreg(dev, rOFDM0_XBAGCCore1, bMaskByte0, 0x36);

				// CCA threshold
				rtl8192_setBBreg(dev, rCCK0_CCA, bMaskByte2, 0x83);
				// Enable OFDM Part
				rtl8192_setBBreg(dev, rOFDM0_TRMuxPar, bMaskByte2, 0x0);

				//LZM ADD because sometimes there is no FW_CMD_HIGH_PWR_DISABLE, this value will be 0.
				if(ScanRegister != 0){
				rtl8192_setBBreg(dev, rOFDM0_RxDetector1, bMaskDWord, ScanRegister);
				}

				if(priv->rf_type == RF_1T2R || priv->rf_type == RF_2T2R)
					rtl8192_setBBreg(dev, rOFDM1_TRxPathEnable, 0xf, 0x3);
				else
					rtl8192_setBBreg(dev, rOFDM1_TRxPathEnable, 0xf, 0x1);
			}
			break;
	}

	priv->SetFwCmdInProgress = false;// Clear FW CMD operation flag.
	RT_TRACE(COMP_CMD, "<---SetFwCmdIOWorkItemCallback()\n");

}


#ifndef _R819XU_PHY_H
#define _R819XU_PHY_H

/* Channel switch:The size of command tables for switch channel*/
#define MAX_PRECMD_CNT 16
#define MAX_RFDEPENDCMD_CNT 16
#define MAX_POSTCMD_CNT 16

typedef enum _SwChnlCmdID{
	CmdID_End,
	CmdID_SetTxPowerLevel,
	CmdID_BBRegWrite10,
	CmdID_WritePortUlong,
	CmdID_WritePortUshort,
	CmdID_WritePortUchar,
	CmdID_RF_WriteReg,
}SwChnlCmdID;

/*--------------------------------Define structure--------------------------------*/
/* 1. Switch channel related */
typedef struct _SwChnlCmd{
	SwChnlCmdID	CmdID;
	u32			Para1;
	u32			Para2;
	u32			msDelay;
}__attribute__ ((packed)) SwChnlCmd;

extern u32 rtl819XMACPHY_Array_PG[];
extern u32 rtl819XPHY_REG_1T2RArray[];
extern u32 rtl819XAGCTAB_Array[];
extern u32 rtl819XRadioA_Array[];
extern u32 rtl819XRadioB_Array[];
extern u32 rtl819XRadioC_Array[];
extern u32 rtl819XRadioD_Array[];

typedef enum _HW90_BLOCK{
	HW90_BLOCK_MAC = 0,
	HW90_BLOCK_PHY0 = 1,
	HW90_BLOCK_PHY1 = 2,
	HW90_BLOCK_RF = 3,
	HW90_BLOCK_MAXIMUM = 4, // Never use this
}HW90_BLOCK_E, *PHW90_BLOCK_E;

typedef enum _RF90_RADIO_PATH{
	RF90_PATH_A = 0,			//Radio Path A
	RF90_PATH_B = 1,			//Radio Path B
	RF90_PATH_C = 2,			//Radio Path C
	RF90_PATH_D = 3,			//Radio Path D
	RF90_PATH_MAX				//Max RF number 92 support
}RF90_RADIO_PATH_E, *PRF90_RADIO_PATH_E;

#define bMaskByte0                0xff
#define bMaskByte1                0xff00
#define bMaskByte2                0xff0000
#define bMaskByte3                0xff000000
#define bMaskHWord                0xffff0000
#define bMaskLWord                0x0000ffff
#define bMaskDWord                0xffffffff

//extern u32 rtl8192_CalculateBitShift(u32 dwBitMask);
extern u8 rtl8192_phy_CheckIsLegalRFPath(struct net_device* dev, u32 eRFPath);
extern void rtl8192_setBBreg(struct net_device* dev, u32 dwRegAddr, u32 dwBitMask, u32 dwData);
extern u32 rtl8192_QueryBBReg(struct net_device* dev, u32 dwRegAddr, u32 dwBitMask);
//extern u32 rtl8192_phy_RFSerialRead(struct net_device* dev, RF90_RADIO_PATH_E eRFPath, u32 Offset);
//extern void rtl8192_phy_RFSerialWrite(struct net_device* dev, RF90_RADIO_PATH_E eRFPath, u32 Offset, u32 Data);
extern void rtl8192_phy_SetRFReg(struct net_device* dev, RF90_RADIO_PATH_E eRFPath, u32 RegAddr, u32 BitMask, u32 Data);
extern u32 rtl8192_phy_QueryRFReg(struct net_device* dev, RF90_RADIO_PATH_E eRFPath, u32 RegAddr, u32 BitMask);
extern void rtl8192_phy_configmac(struct net_device* dev);
extern void rtl8192_phyConfigBB(struct net_device* dev, u8 ConfigType);
//extern void rtl8192_InitBBRFRegDef(struct net_device* dev);
extern u8 rtl8192_phy_checkBBAndRF(struct net_device* dev, HW90_BLOCK_E CheckBlock, RF90_RADIO_PATH_E eRFPath);
//extern void rtl8192_BB_Config_ParaFile(struct net_device* dev);
extern void rtl8192_BBConfig(struct net_device* dev);
extern void rtl8192_phy_getTxPower(struct net_device* dev);
extern void rtl8192_phy_setTxPower(struct net_device* dev, u8 channel);
extern void rtl8192_phy_RFConfig(struct net_device* dev);
extern void rtl8192_phy_updateInitGain(struct net_device* dev);
extern u8 rtl8192_phy_ConfigRFWithHeaderFile(struct net_device* dev, RF90_RADIO_PATH_E	eRFPath);

extern u8 rtl8192_phy_SwChnl(struct net_device* dev, u8 channel);
extern void rtl8192_SetBWMode(struct net_device *dev, HT_CHANNEL_WIDTH	Bandwidth, HT_EXTCHNL_OFFSET Offset);
extern void rtl8192_SwChnl_WorkItem(struct net_device *dev);
void rtl8192_SetBWModeWorkItem(struct net_device *dev);
extern bool rtl8192_SetRFPowerState(struct net_device *dev, RT_RF_POWER_STATE eRFPowerState);
//added by amy
extern void InitialGain819xUsb(struct net_device *dev,	u8 Operation);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,20))
extern void InitialGainOperateWorkItemCallBack(struct work_struct *work);
#else
extern void InitialGainOperateWorkItemCallBack(struct net_device *dev);
#endif

#endif

#ifndef _R819XU_PHY_H
#define _R819XU_PHY_H

/* Channel switch: the size of command tables for switch channel */
#define MAX_PRECMD_CNT 16
#define MAX_RFDEPENDCMD_CNT 16
#define MAX_POSTCMD_CNT 16

#define MACPHY_Array_PGLength 30
#define Rtl819XMACPHY_Array_PG Rtl8192PciEMACPHY_Array_PG
#define Rtl819XMACPHY_Array Rtl8192PciEMACPHY_Array
#define RadioC_ArrayLength 1
#define RadioD_ArrayLength 1
#define Rtl819XRadioA_Array Rtl8192PciERadioA_Array
#define Rtl819XRadioB_Array Rtl8192PciERadioB_Array
#define Rtl819XRadioC_Array Rtl8192PciERadioC_Array
#define Rtl819XRadioD_Array Rtl8192PciERadioD_Array
#define Rtl819XAGCTAB_Array Rtl8192PciEAGCTAB_Array
#define PHY_REGArrayLength 1
#define Rtl819XPHY_REGArray Rtl8192PciEPHY_REGArray
#define PHY_REG_1T2RArrayLength 296
#define Rtl819XPHY_REG_1T2RArray Rtl8192PciEPHY_REG_1T2RArray

#define AGCTAB_ArrayLength 384
#define MACPHY_ArrayLength 18

#define RadioA_ArrayLength 246
#define RadioB_ArrayLength 78


typedef enum _SwChnlCmdID {
	CmdID_End,
	CmdID_SetTxPowerLevel,
	CmdID_BBRegWrite10,
	CmdID_WritePortUlong,
	CmdID_WritePortUshort,
	CmdID_WritePortUchar,
	CmdID_RF_WriteReg,
} SwChnlCmdID;

/* switch channel data structure */
typedef struct _SwChnlCmd {
	SwChnlCmdID CmdID;
	u32 Para1;
	u32 Para2;
	u32 msDelay;
} __attribute__ ((packed)) SwChnlCmd;

extern u32 rtl819XMACPHY_Array_PG[];
extern u32 rtl819XPHY_REG_1T2RArray[];
extern u32 rtl819XAGCTAB_Array[];
extern u32 rtl819XRadioA_Array[];
extern u32 rtl819XRadioB_Array[];
extern u32 rtl819XRadioC_Array[];
extern u32 rtl819XRadioD_Array[];

typedef enum _HW90_BLOCK {
	HW90_BLOCK_MAC = 0,
	HW90_BLOCK_PHY0 = 1,
	HW90_BLOCK_PHY1 = 2,
	HW90_BLOCK_RF = 3,
	/* Don't ever use this. */
	HW90_BLOCK_MAXIMUM = 4,
} HW90_BLOCK_E, *PHW90_BLOCK_E;

typedef enum _RF90_RADIO_PATH {
	/* Radio paths */
	RF90_PATH_A = 0,
	RF90_PATH_B = 1,
	RF90_PATH_C = 2,
	RF90_PATH_D = 3,

	/* Max RF number 92 support */
	RF90_PATH_MAX
} RF90_RADIO_PATH_E, *PRF90_RADIO_PATH_E;

#define bMaskByte0 0xff
#define bMaskByte1 0xff00
#define bMaskByte2 0xff0000
#define bMaskByte3 0xff000000
#define bMaskHWord 0xffff0000
#define bMaskLWord 0x0000ffff
#define bMaskDWord 0xffffffff

u8 rtl8192_phy_CheckIsLegalRFPath(struct r8192_priv *priv, u32 eRFPath);

void rtl8192_setBBreg(struct r8192_priv *priv, u32 dwRegAddr,
			     u32 dwBitMask, u32 dwData);

u32 rtl8192_QueryBBReg(struct r8192_priv *priv, u32 dwRegAddr,
			     u32 dwBitMask);

void rtl8192_phy_SetRFReg(struct r8192_priv *priv,
		RF90_RADIO_PATH_E eRFPath, u32 RegAddr,
		u32 BitMask, u32 Data);

u32 rtl8192_phy_QueryRFReg(struct r8192_priv *priv,
		RF90_RADIO_PATH_E eRFPath, u32 RegAddr, u32 BitMask);

void rtl8192_phy_configmac(struct r8192_priv *priv);

void rtl8192_phyConfigBB(struct r8192_priv *priv, u8 ConfigType);

RT_STATUS rtl8192_phy_checkBBAndRF(struct r8192_priv *priv,
		HW90_BLOCK_E CheckBlock, RF90_RADIO_PATH_E eRFPath);

RT_STATUS rtl8192_BBConfig(struct r8192_priv *priv);

void rtl8192_phy_getTxPower(struct r8192_priv *priv);

void rtl8192_phy_setTxPower(struct r8192_priv *priv, u8 channel);

RT_STATUS rtl8192_phy_RFConfig(struct r8192_priv *priv);

void rtl8192_phy_updateInitGain(struct r8192_priv *priv);

u8 rtl8192_phy_ConfigRFWithHeaderFile(struct r8192_priv *priv,
					RF90_RADIO_PATH_E eRFPath);

u8 rtl8192_phy_SwChnl(struct ieee80211_device *ieee80211, u8 channel);

void rtl8192_SetBWMode(struct ieee80211_device *ieee80211,
		HT_CHANNEL_WIDTH Bandwidth, HT_EXTCHNL_OFFSET Offset);

void rtl8192_SwChnl_WorkItem(struct r8192_priv *priv);

void rtl8192_SetBWModeWorkItem(struct r8192_priv *priv);

void InitialGain819xPci(struct ieee80211_device *ieee, u8 Operation);

#endif /* _R819XU_PHY_H */

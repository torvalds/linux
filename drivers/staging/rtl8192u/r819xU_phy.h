/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _R819XU_PHY_H
#define _R819XU_PHY_H

/* Channel switch: The size of command tables for switch channel */
#define MAX_PRECMD_CNT 16
#define MAX_RFDEPENDCMD_CNT 16
#define MAX_POSTCMD_CNT 16

enum baseband_config_type {
	BASEBAND_CONFIG_PHY_REG = 0,			//Radio Path A
	BASEBAND_CONFIG_AGC_TAB = 1,			//Radio Path B
};

enum switch_chan_cmd_id {
	CMD_ID_END,
	CMD_ID_SET_TX_PWR_LEVEL,
	CMD_ID_WRITE_PORT_ULONG,
	CMD_ID_WRITE_PORT_USHORT,
	CMD_ID_WRITE_PORT_UCHAR,
	CMD_ID_RF_WRITE_REG,
};

/* -----------------------Define structure---------------------- */
/* 1. Switch channel related */
struct sw_chnl_cmd {
	enum switch_chan_cmd_id	cmd_id;
	u32		        para_1;
	u32		        para_2;
	u32		        ms_delay;
} __packed;

enum hw90_block_e {
	HW90_BLOCK_MAC = 0,
	HW90_BLOCK_PHY0 = 1,
	HW90_BLOCK_PHY1 = 2,
	HW90_BLOCK_RF = 3,
	HW90_BLOCK_MAXIMUM = 4, /* Never use this */
};

enum rf90_radio_path_e {
	RF90_PATH_A = 0,			/* Radio Path A */
	RF90_PATH_B = 1,			/* Radio Path B */
	RF90_PATH_C = 2,			/* Radio Path C */
	RF90_PATH_D = 3,			/* Radio Path D */
	RF90_PATH_MAX				/* Max RF number 92 support */
};

u8 rtl8192_phy_CheckIsLegalRFPath(struct net_device *dev, u32 e_rfpath);
void rtl8192_setBBreg(struct net_device *dev, u32 reg_addr,
		      u32 bitmask, u32 data);
u32 rtl8192_QueryBBReg(struct net_device *dev, u32 reg_addr, u32 bitmask);
void rtl8192_phy_SetRFReg(struct net_device *dev,
			  enum rf90_radio_path_e e_rfpath,
			  u32 reg_addr, u32 bitmask, u32 data);
u32 rtl8192_phy_QueryRFReg(struct net_device *dev,
			   enum rf90_radio_path_e e_rfpath,
			   u32 reg_addr, u32 bitmask);
void rtl8192_phy_configmac(struct net_device *dev);
u8 rtl8192_phy_checkBBAndRF(struct net_device *dev,
			    enum hw90_block_e CheckBlock,
			    enum rf90_radio_path_e e_rfpath);
void rtl8192_BBConfig(struct net_device *dev);
void rtl8192_phy_getTxPower(struct net_device *dev);
void rtl8192_phy_setTxPower(struct net_device *dev, u8 channel);
void rtl8192_phy_RFConfig(struct net_device *dev);
void rtl8192_phy_updateInitGain(struct net_device *dev);
u8 rtl8192_phy_ConfigRFWithHeaderFile(struct net_device *dev,
				      enum rf90_radio_path_e e_rfpath);

u8 rtl8192_phy_SwChnl(struct net_device *dev, u8 channel);
void rtl8192_SetBWMode(struct net_device *dev,
		       enum ht_channel_width bandwidth,
		       enum ht_extension_chan_offset offset);
void rtl8192_SwChnl_WorkItem(struct net_device *dev);
void rtl8192_SetBWModeWorkItem(struct net_device *dev);
bool rtl8192_SetRFPowerState(struct net_device *dev,
			     RT_RF_POWER_STATE eRFPowerState);
void InitialGain819xUsb(struct net_device *dev, u8 Operation);

void InitialGainOperateWorkItemCallBack(struct work_struct *work);

#endif

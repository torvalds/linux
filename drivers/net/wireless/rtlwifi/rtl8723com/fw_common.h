/******************************************************************************
 *
 * Copyright(c) 2009-2014  Realtek Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * The full GNU General Public License is included in this distribution in the
 * file called LICENSE.
 *
 * Contact Information:
 * wlanfae <wlanfae@realtek.com>
 * Realtek Corporation, No. 2, Innovation Road II, Hsinchu Science Park,
 * Hsinchu 300, Taiwan.
 *
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 *****************************************************************************/

#ifndef __FW_COMMON_H__
#define __FW_COMMON_H__

#define REG_SYS_FUNC_EN				0x0002
#define REG_MCUFWDL				0x0080
#define FW_8192C_START_ADDRESS			0x1000
#define FW_8192C_PAGE_SIZE			4096
#define FW_8192C_POLLING_TIMEOUT_COUNT		6000
#define FW_8192C_POLLING_DELAY			5

#define MCUFWDL_RDY				BIT(1)
#define FWDL_CHKSUM_RPT				BIT(2)
#define WINTINI_RDY				BIT(6)

#define REG_RSV_CTRL				0x001C
#define REG_HMETFR				0x01CC

enum version_8723e {
	VERSION_TEST_UMC_CHIP_8723 = 0x0081,
	VERSION_NORMAL_UMC_CHIP_8723_1T1R_A_CUT = 0x0089,
	VERSION_NORMAL_UMC_CHIP_8723_1T1R_B_CUT = 0x1089,
	VERSION_TEST_CHIP_1T1R_8723B = 0x0106,
	VERSION_NORMAL_SMIC_CHIP_1T1R_8723B = 0x010E,
	VERSION_UNKNOWN = 0xFF,
};

enum rtl8723ae_h2c_cmd {
	H2C_AP_OFFLOAD = 0,
	H2C_SETPWRMODE = 1,
	H2C_JOINBSSRPT = 2,
	H2C_RSVDPAGE = 3,
	H2C_RSSI_REPORT = 4,
	H2C_P2P_PS_CTW_CMD = 5,
	H2C_P2P_PS_OFFLOAD = 6,
	H2C_RA_MASK = 7,
	MAX_H2CCMD
};

enum rtl8723be_cmd {
	H2C_8723BE_RSVDPAGE = 0,
	H2C_8723BE_JOINBSSRPT = 1,
	H2C_8723BE_SCAN = 2,
	H2C_8723BE_KEEP_ALIVE_CTRL = 3,
	H2C_8723BE_DISCONNECT_DECISION = 4,
	H2C_8723BE_INIT_OFFLOAD = 6,
	H2C_8723BE_AP_OFFLOAD = 8,
	H2C_8723BE_BCN_RSVDPAGE = 9,
	H2C_8723BE_PROBERSP_RSVDPAGE = 10,

	H2C_8723BE_SETPWRMODE = 0x20,
	H2C_8723BE_PS_TUNING_PARA = 0x21,
	H2C_8723BE_PS_TUNING_PARA2 = 0x22,
	H2C_8723BE_PS_LPS_PARA = 0x23,
	H2C_8723BE_P2P_PS_OFFLOAD = 0x24,

	H2C_8723BE_WO_WLAN = 0x80,
	H2C_8723BE_REMOTE_WAKE_CTRL = 0x81,
	H2C_8723BE_AOAC_GLOBAL_INFO = 0x82,
	H2C_8723BE_AOAC_RSVDPAGE = 0x83,
	H2C_8723BE_RSSI_REPORT = 0x42,
	H2C_8723BE_RA_MASK = 0x40,
	H2C_8723BE_SELECTIVE_SUSPEND_ROF_CMD,
	H2C_8723BE_P2P_PS_MODE,
	H2C_8723BE_PSD_RESULT,
	/*Not defined CTW CMD for P2P yet*/
	H2C_8723BE_P2P_PS_CTW_CMD,
	MAX_8723BE_H2CCMD
};

struct rtl92c_firmware_header {
	u16 signature;
	u8 category;
	u8 function;
	u16 version;
	u8 subversion;
	u8 rsvd1;
	u8 month;
	u8 date;
	u8 hour;
	u8 minute;
	u16 ramcodesize;
	u16 rsvd2;
	u32 svnindex;
	u32 rsvd3;
	u32 rsvd4;
	u32 rsvd5;
};

void rtl8723ae_firmware_selfreset(struct ieee80211_hw *hw);
void rtl8723be_firmware_selfreset(struct ieee80211_hw *hw);
void rtl8723_enable_fw_download(struct ieee80211_hw *hw, bool enable);
void rtl8723_fw_block_write(struct ieee80211_hw *hw,
			    const u8 *buffer, u32 size);
void rtl8723_fw_page_write(struct ieee80211_hw *hw,
			   u32 page, const u8 *buffer, u32 size);
void rtl8723_write_fw(struct ieee80211_hw *hw,
		      enum version_8723e version,
		      u8 *buffer, u32 size);
int rtl8723_fw_free_to_go(struct ieee80211_hw *hw, bool is_8723be);
int rtl8723_download_fw(struct ieee80211_hw *hw, bool is_8723be);
#endif

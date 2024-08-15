/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2009-2012  Realtek Corporation.*/

#ifndef __RTL92CU_HW_H__
#define __RTL92CU_HW_H__

#define H2C_RA_MASK	6

#define LLT_LAST_ENTRY_OF_TX_PKT_BUFFER		255

#define RX_PAGE_SIZE_REG_VALUE			PBP_128
/* Note: We will divide number of page equally for each queue
 * other than public queue! */
#define TX_TOTAL_PAGE_NUMBER			0xF8
#define TX_PAGE_BOUNDARY			(TX_TOTAL_PAGE_NUMBER + 1)

#define CHIP_B_PAGE_NUM_PUBQ			0xE7

/* For Test Chip Setting
 * (HPQ + LPQ + PUBQ) shall be TX_TOTAL_PAGE_NUMBER */
#define CHIP_A_PAGE_NUM_PUBQ			0x7E

/* For Chip A Setting */
#define WMM_CHIP_A_TX_TOTAL_PAGE_NUMBER		0xF5
#define WMM_CHIP_A_TX_PAGE_BOUNDARY		\
	(WMM_CHIP_A_TX_TOTAL_PAGE_NUMBER + 1) /* F6 */

#define WMM_CHIP_A_PAGE_NUM_PUBQ		0xA3
#define WMM_CHIP_A_PAGE_NUM_HPQ			0x29
#define WMM_CHIP_A_PAGE_NUM_LPQ			0x29

/* Note: For Chip B Setting ,modify later */
#define WMM_CHIP_B_TX_TOTAL_PAGE_NUMBER		0xF5
#define WMM_CHIP_B_TX_PAGE_BOUNDARY		\
	(WMM_CHIP_B_TX_TOTAL_PAGE_NUMBER + 1) /* F6 */

#define WMM_CHIP_B_PAGE_NUM_PUBQ		0xB0
#define WMM_CHIP_B_PAGE_NUM_HPQ			0x29
#define WMM_CHIP_B_PAGE_NUM_LPQ			0x1C
#define WMM_CHIP_B_PAGE_NUM_NPQ			0x1C

#define BOARD_TYPE_NORMAL_MASK			0xE0
#define BOARD_TYPE_TEST_MASK			0x0F

/* should be renamed and moved to another file */
enum _BOARD_TYPE_8192CUSB {
	BOARD_USB_DONGLE		= 0,	/* USB dongle */
	BOARD_USB_HIGH_PA		= 1,	/* USB dongle - high power PA */
	BOARD_MINICARD			= 2,	/* Minicard */
	BOARD_USB_SOLO			= 3,	/* USB solo-Slim module */
	BOARD_USB_COMBO			= 4,	/* USB Combo-Slim module */
};

#define IS_HIGHT_PA(boardtype)		\
	((boardtype == BOARD_USB_HIGH_PA) ? true : false)

#define RTL92C_DRIVER_INFO_SIZE				4
void rtl92cu_read_eeprom_info(struct ieee80211_hw *hw);
void rtl92cu_enable_hw_security_config(struct ieee80211_hw *hw);
int rtl92cu_hw_init(struct ieee80211_hw *hw);
void rtl92cu_card_disable(struct ieee80211_hw *hw);
int rtl92cu_set_network_type(struct ieee80211_hw *hw, enum nl80211_iftype type);
void rtl92cu_set_beacon_related_registers(struct ieee80211_hw *hw);
void rtl92cu_set_beacon_interval(struct ieee80211_hw *hw);
void rtl92cu_update_interrupt_mask(struct ieee80211_hw *hw,
				   u32 add_msr, u32 rm_msr);
void rtl92cu_get_hw_reg(struct ieee80211_hw *hw, u8 variable, u8 *val);
void rtl92cu_set_hw_reg(struct ieee80211_hw *hw, u8 variable, u8 *val);

void rtl92cu_update_channel_access_setting(struct ieee80211_hw *hw);
bool rtl92cu_gpio_radio_on_off_checking(struct ieee80211_hw *hw, u8 * valid);
void rtl92cu_set_check_bssid(struct ieee80211_hw *hw, bool check_bssid);
int rtl92c_download_fw(struct ieee80211_hw *hw);
void rtl92c_set_fw_pwrmode_cmd(struct ieee80211_hw *hw, u8 mode);
void rtl92c_set_fw_joinbss_report_cmd(struct ieee80211_hw *hw, u8 mstatus);
void rtl92c_fill_h2c_cmd(struct ieee80211_hw *hw,
			 u8 element_id, u32 cmd_len, u8 *p_cmdbuffer);
bool rtl92cu_phy_mac_config(struct ieee80211_hw *hw);
void rtl92cu_update_hal_rate_tbl(struct ieee80211_hw *hw,
				 struct ieee80211_sta *sta,
				 u8 rssi_level, bool update_bw);

#endif

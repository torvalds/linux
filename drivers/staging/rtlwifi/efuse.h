/******************************************************************************
 *
 * Copyright(c) 2009-2012  Realtek Corporation.
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

#ifndef __RTL_EFUSE_H_
#define __RTL_EFUSE_H_

#define EFUSE_IC_ID_OFFSET		506

#define EFUSE_MAX_WORD_UNIT		4

#define EFUSE_INIT_MAP			0
#define EFUSE_MODIFY_MAP		1

#define PG_STATE_HEADER			0x01
#define PG_STATE_WORD_0			0x02
#define PG_STATE_WORD_1			0x04
#define PG_STATE_WORD_2			0x08
#define PG_STATE_WORD_3			0x10
#define PG_STATE_DATA			0x20

#define EFUSE_REPEAT_THRESHOLD_		3
#define EFUSE_ERROE_HANDLE		1

struct efuse_map {
	u8 offset;
	u8 word_start;
	u8 byte_start;
	u8 byte_cnts;
};

struct pgpkt_struct {
	u8 offset;
	u8 word_en;
	u8 data[8];
};

enum efuse_data_item {
	EFUSE_CHIP_ID = 0,
	EFUSE_LDO_SETTING,
	EFUSE_CLK_SETTING,
	EFUSE_SDIO_SETTING,
	EFUSE_CCCR,
	EFUSE_SDIO_MODE,
	EFUSE_OCR,
	EFUSE_F0CIS,
	EFUSE_F1CIS,
	EFUSE_MAC_ADDR,
	EFUSE_EEPROM_VER,
	EFUSE_CHAN_PLAN,
	EFUSE_TXPW_TAB
};

enum {
	VOLTAGE_V25 = 0x03,
	LDOE25_SHIFT = 28,
};

struct efuse_priv {
	u8 id[2];
	u8 ldo_setting[2];
	u8 clk_setting[2];
	u8 cccr;
	u8 sdio_mode;
	u8 ocr[3];
	u8 cis0[17];
	u8 cis1[48];
	u8 mac_addr[6];
	u8 eeprom_verno;
	u8 channel_plan;
	u8 tx_power_b[14];
	u8 tx_power_g[14];
};

void read_efuse_byte(struct ieee80211_hw *hw, u16 _offset, u8 *pbuf);
void efuse_initialize(struct ieee80211_hw *hw);
u8 efuse_read_1byte(struct ieee80211_hw *hw, u16 address);
int efuse_one_byte_read(struct ieee80211_hw *hw, u16 addr, u8 *data);
void efuse_write_1byte(struct ieee80211_hw *hw, u16 address, u8 value);
void read_efuse(struct ieee80211_hw *hw, u16 _offset,
		u16 _size_byte, u8 *pbuf);
void efuse_shadow_read(struct ieee80211_hw *hw, u8 type,
		       u16 offset, u32 *value);
void efuse_shadow_write(struct ieee80211_hw *hw, u8 type,
			u16 offset, u32 value);
bool efuse_shadow_update(struct ieee80211_hw *hw);
bool efuse_shadow_update_chk(struct ieee80211_hw *hw);
void rtl_efuse_shadow_map_update(struct ieee80211_hw *hw);
void efuse_force_write_vendor_id(struct ieee80211_hw *hw);
void efuse_re_pg_section(struct ieee80211_hw *hw, u8 section_idx);
void efuse_power_switch(struct ieee80211_hw *hw, u8 write, u8 pwrstate);
int rtl_get_hwinfo(struct ieee80211_hw *hw, struct rtl_priv *rtlpriv,
		   int max_size, u8 *hwinfo, int *params);
void rtl_fill_dummy(u8 *pfwbuf, u32 *pfwlen);
void rtl_fw_page_write(struct ieee80211_hw *hw, u32 page, const u8 *buffer,
		       u32 size);
void rtl_fw_block_write(struct ieee80211_hw *hw, const u8 *buffer, u32 size);

#endif

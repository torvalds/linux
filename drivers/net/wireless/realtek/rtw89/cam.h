/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2019-2020  Realtek Corporation
 */

#ifndef __RTW89_CAM_H__
#define __RTW89_CAM_H__

#include "core.h"

#define RTW89_SEC_CAM_LEN	20

static inline void FWCMD_SET_ADDR_IDX(void *cmd, u32 value)
{
	le32p_replace_bits((__le32 *)(cmd) + 1, value, GENMASK(7, 0));
}

static inline void FWCMD_SET_ADDR_OFFSET(void *cmd, u32 value)
{
	le32p_replace_bits((__le32 *)(cmd) + 1, value, GENMASK(15, 8));
}

static inline void FWCMD_SET_ADDR_LEN(void *cmd, u32 value)
{
	le32p_replace_bits((__le32 *)(cmd) + 1, value, GENMASK(23, 16));
}

static inline void FWCMD_SET_ADDR_VALID(void *cmd, u32 value)
{
	le32p_replace_bits((__le32 *)(cmd) + 2, value, BIT(0));
}

static inline void FWCMD_SET_ADDR_NET_TYPE(void *cmd, u32 value)
{
	le32p_replace_bits((__le32 *)(cmd) + 2, value, GENMASK(2, 1));
}

static inline void FWCMD_SET_ADDR_BCN_HIT_COND(void *cmd, u32 value)
{
	le32p_replace_bits((__le32 *)(cmd) + 2, value, GENMASK(4, 3));
}

static inline void FWCMD_SET_ADDR_HIT_RULE(void *cmd, u32 value)
{
	le32p_replace_bits((__le32 *)(cmd) + 2, value, GENMASK(6, 5));
}

static inline void FWCMD_SET_ADDR_BB_SEL(void *cmd, u32 value)
{
	le32p_replace_bits((__le32 *)(cmd) + 2, value, BIT(7));
}

static inline void FWCMD_SET_ADDR_ADDR_MASK(void *cmd, u32 value)
{
	le32p_replace_bits((__le32 *)(cmd) + 2, value, GENMASK(13, 8));
}

static inline void FWCMD_SET_ADDR_MASK_SEL(void *cmd, u32 value)
{
	le32p_replace_bits((__le32 *)(cmd) + 2, value, GENMASK(15, 14));
}

static inline void FWCMD_SET_ADDR_SMA_HASH(void *cmd, u32 value)
{
	le32p_replace_bits((__le32 *)(cmd) + 2, value, GENMASK(23, 16));
}

static inline void FWCMD_SET_ADDR_TMA_HASH(void *cmd, u32 value)
{
	le32p_replace_bits((__le32 *)(cmd) + 2, value, GENMASK(31, 24));
}

static inline void FWCMD_SET_ADDR_BSSID_CAM_IDX(void *cmd, u32 value)
{
	le32p_replace_bits((__le32 *)(cmd) + 3, value, GENMASK(5, 0));
}

static inline void FWCMD_SET_ADDR_SMA0(void *cmd, u32 value)
{
	le32p_replace_bits((__le32 *)(cmd) + 4, value, GENMASK(7, 0));
}

static inline void FWCMD_SET_ADDR_SMA1(void *cmd, u32 value)
{
	le32p_replace_bits((__le32 *)(cmd) + 4, value, GENMASK(15, 8));
}

static inline void FWCMD_SET_ADDR_SMA2(void *cmd, u32 value)
{
	le32p_replace_bits((__le32 *)(cmd) + 4, value, GENMASK(23, 16));
}

static inline void FWCMD_SET_ADDR_SMA3(void *cmd, u32 value)
{
	le32p_replace_bits((__le32 *)(cmd) + 4, value, GENMASK(31, 24));
}

static inline void FWCMD_SET_ADDR_SMA4(void *cmd, u32 value)
{
	le32p_replace_bits((__le32 *)(cmd) + 5, value, GENMASK(7, 0));
}

static inline void FWCMD_SET_ADDR_SMA5(void *cmd, u32 value)
{
	le32p_replace_bits((__le32 *)(cmd) + 5, value, GENMASK(15, 8));
}

static inline void FWCMD_SET_ADDR_TMA0(void *cmd, u32 value)
{
	le32p_replace_bits((__le32 *)(cmd) + 5, value, GENMASK(23, 16));
}

static inline void FWCMD_SET_ADDR_TMA1(void *cmd, u32 value)
{
	le32p_replace_bits((__le32 *)(cmd) + 5, value, GENMASK(31, 24));
}

static inline void FWCMD_SET_ADDR_TMA2(void *cmd, u32 value)
{
	le32p_replace_bits((__le32 *)(cmd) + 6, value, GENMASK(7, 0));
}

static inline void FWCMD_SET_ADDR_TMA3(void *cmd, u32 value)
{
	le32p_replace_bits((__le32 *)(cmd) + 6, value, GENMASK(15, 8));
}

static inline void FWCMD_SET_ADDR_TMA4(void *cmd, u32 value)
{
	le32p_replace_bits((__le32 *)(cmd) + 6, value, GENMASK(23, 16));
}

static inline void FWCMD_SET_ADDR_TMA5(void *cmd, u32 value)
{
	le32p_replace_bits((__le32 *)(cmd) + 6, value, GENMASK(31, 24));
}

static inline void FWCMD_SET_ADDR_MACID(void *cmd, u32 value)
{
	le32p_replace_bits((__le32 *)(cmd) + 8, value, GENMASK(7, 0));
}

static inline void FWCMD_SET_ADDR_PORT_INT(void *cmd, u32 value)
{
	le32p_replace_bits((__le32 *)(cmd) + 8, value, GENMASK(10, 8));
}

static inline void FWCMD_SET_ADDR_TSF_SYNC(void *cmd, u32 value)
{
	le32p_replace_bits((__le32 *)(cmd) + 8, value, GENMASK(13, 11));
}

static inline void FWCMD_SET_ADDR_TF_TRS(void *cmd, u32 value)
{
	le32p_replace_bits((__le32 *)(cmd) + 8, value, BIT(14));
}

static inline void FWCMD_SET_ADDR_LSIG_TXOP(void *cmd, u32 value)
{
	le32p_replace_bits((__le32 *)(cmd) + 8, value, BIT(15));
}

static inline void FWCMD_SET_ADDR_TGT_IND(void *cmd, u32 value)
{
	le32p_replace_bits((__le32 *)(cmd) + 8, value, GENMASK(26, 24));
}

static inline void FWCMD_SET_ADDR_FRM_TGT_IND(void *cmd, u32 value)
{
	le32p_replace_bits((__le32 *)(cmd) + 8, value, GENMASK(29, 27));
}

static inline void FWCMD_SET_ADDR_AID12(void *cmd, u32 value)
{
	le32p_replace_bits((__le32 *)(cmd) + 9, value, GENMASK(11, 0));
}

static inline void FWCMD_SET_ADDR_AID12_0(void *cmd, u32 value)
{
	le32p_replace_bits((__le32 *)(cmd) + 9, value, GENMASK(7, 0));
}

static inline void FWCMD_SET_ADDR_AID12_1(void *cmd, u32 value)
{
	le32p_replace_bits((__le32 *)(cmd) + 9, value, GENMASK(11, 8));
}

static inline void FWCMD_SET_ADDR_WOL_PATTERN(void *cmd, u32 value)
{
	le32p_replace_bits((__le32 *)(cmd) + 9, value, BIT(12));
}

static inline void FWCMD_SET_ADDR_WOL_UC(void *cmd, u32 value)
{
	le32p_replace_bits((__le32 *)(cmd) + 9, value, BIT(13));
}

static inline void FWCMD_SET_ADDR_WOL_MAGIC(void *cmd, u32 value)
{
	le32p_replace_bits((__le32 *)(cmd) + 9, value, BIT(14));
}

static inline void FWCMD_SET_ADDR_WAPI(void *cmd, u32 value)
{
	le32p_replace_bits((__le32 *)(cmd) + 9, value, BIT(15));
}

static inline void FWCMD_SET_ADDR_SEC_ENT_MODE(void *cmd, u32 value)
{
	le32p_replace_bits((__le32 *)(cmd) + 9, value, GENMASK(17, 16));
}

static inline void FWCMD_SET_ADDR_SEC_ENT0_KEYID(void *cmd, u32 value)
{
	le32p_replace_bits((__le32 *)(cmd) + 9, value, GENMASK(19, 18));
}

static inline void FWCMD_SET_ADDR_SEC_ENT1_KEYID(void *cmd, u32 value)
{
	le32p_replace_bits((__le32 *)(cmd) + 9, value, GENMASK(21, 20));
}

static inline void FWCMD_SET_ADDR_SEC_ENT2_KEYID(void *cmd, u32 value)
{
	le32p_replace_bits((__le32 *)(cmd) + 9, value, GENMASK(23, 22));
}

static inline void FWCMD_SET_ADDR_SEC_ENT3_KEYID(void *cmd, u32 value)
{
	le32p_replace_bits((__le32 *)(cmd) + 9, value, GENMASK(25, 24));
}

static inline void FWCMD_SET_ADDR_SEC_ENT4_KEYID(void *cmd, u32 value)
{
	le32p_replace_bits((__le32 *)(cmd) + 9, value, GENMASK(27, 26));
}

static inline void FWCMD_SET_ADDR_SEC_ENT5_KEYID(void *cmd, u32 value)
{
	le32p_replace_bits((__le32 *)(cmd) + 9, value, GENMASK(29, 28));
}

static inline void FWCMD_SET_ADDR_SEC_ENT6_KEYID(void *cmd, u32 value)
{
	le32p_replace_bits((__le32 *)(cmd) + 9, value, GENMASK(31, 30));
}

static inline void FWCMD_SET_ADDR_SEC_ENT_VALID(void *cmd, u32 value)
{
	le32p_replace_bits((__le32 *)(cmd) + 10, value, GENMASK(7, 0));
}

static inline void FWCMD_SET_ADDR_SEC_ENT0(void *cmd, u32 value)
{
	le32p_replace_bits((__le32 *)(cmd) + 10, value, GENMASK(15, 8));
}

static inline void FWCMD_SET_ADDR_SEC_ENT1(void *cmd, u32 value)
{
	le32p_replace_bits((__le32 *)(cmd) + 10, value, GENMASK(23, 16));
}

static inline void FWCMD_SET_ADDR_SEC_ENT2(void *cmd, u32 value)
{
	le32p_replace_bits((__le32 *)(cmd) + 10, value, GENMASK(31, 24));
}

static inline void FWCMD_SET_ADDR_SEC_ENT3(void *cmd, u32 value)
{
	le32p_replace_bits((__le32 *)(cmd) + 11, value, GENMASK(7, 0));
}

static inline void FWCMD_SET_ADDR_SEC_ENT4(void *cmd, u32 value)
{
	le32p_replace_bits((__le32 *)(cmd) + 11, value, GENMASK(15, 8));
}

static inline void FWCMD_SET_ADDR_SEC_ENT5(void *cmd, u32 value)
{
	le32p_replace_bits((__le32 *)(cmd) + 11, value, GENMASK(23, 16));
}

static inline void FWCMD_SET_ADDR_SEC_ENT6(void *cmd, u32 value)
{
	le32p_replace_bits((__le32 *)(cmd) + 11, value, GENMASK(31, 24));
}

static inline void FWCMD_SET_ADDR_BSSID_IDX(void *cmd, u32 value)
{
	le32p_replace_bits((__le32 *)(cmd) + 12, value, GENMASK(7, 0));
}

static inline void FWCMD_SET_ADDR_BSSID_OFFSET(void *cmd, u32 value)
{
	le32p_replace_bits((__le32 *)(cmd) + 12, value, GENMASK(15, 8));
}

static inline void FWCMD_SET_ADDR_BSSID_LEN(void *cmd, u32 value)
{
	le32p_replace_bits((__le32 *)(cmd) + 12, value, GENMASK(23, 16));
}

static inline void FWCMD_SET_ADDR_BSSID_VALID(void *cmd, u32 value)
{
	le32p_replace_bits((__le32 *)(cmd) + 13, value, BIT(0));
}

static inline void FWCMD_SET_ADDR_BSSID_BB_SEL(void *cmd, u32 value)
{
	le32p_replace_bits((__le32 *)(cmd) + 13, value, BIT(1));
}

static inline void FWCMD_SET_ADDR_BSSID_BSS_COLOR(void *cmd, u32 value)
{
	le32p_replace_bits((__le32 *)(cmd) + 13, value, GENMASK(13, 8));
}

static inline void FWCMD_SET_ADDR_BSSID_BSSID0(void *cmd, u32 value)
{
	le32p_replace_bits((__le32 *)(cmd) + 13, value, GENMASK(23, 16));
}

static inline void FWCMD_SET_ADDR_BSSID_BSSID1(void *cmd, u32 value)
{
	le32p_replace_bits((__le32 *)(cmd) + 13, value, GENMASK(31, 24));
}

static inline void FWCMD_SET_ADDR_BSSID_BSSID2(void *cmd, u32 value)
{
	le32p_replace_bits((__le32 *)(cmd) + 14, value, GENMASK(7, 0));
}

static inline void FWCMD_SET_ADDR_BSSID_BSSID3(void *cmd, u32 value)
{
	le32p_replace_bits((__le32 *)(cmd) + 14, value, GENMASK(15, 8));
}

static inline void FWCMD_SET_ADDR_BSSID_BSSID4(void *cmd, u32 value)
{
	le32p_replace_bits((__le32 *)(cmd) + 14, value, GENMASK(23, 16));
}

static inline void FWCMD_SET_ADDR_BSSID_BSSID5(void *cmd, u32 value)
{
	le32p_replace_bits((__le32 *)(cmd) + 14, value, GENMASK(31, 24));
}

int rtw89_cam_init(struct rtw89_dev *rtwdev, struct rtw89_vif *vif);
void rtw89_cam_deinit(struct rtw89_dev *rtwdev, struct rtw89_vif *vif);
void rtw89_cam_fill_addr_cam_info(struct rtw89_dev *rtwdev,
				  struct rtw89_vif *vif,
				  struct rtw89_sta *rtwsta,
				  const u8 *scan_mac_addr, u8 *cmd);
int rtw89_cam_fill_bssid_cam_info(struct rtw89_dev *rtwdev,
				  struct rtw89_vif *vif, u8 *cmd);
int rtw89_cam_sec_key_add(struct rtw89_dev *rtwdev,
			  struct ieee80211_vif *vif,
			  struct ieee80211_sta *sta,
			  struct ieee80211_key_conf *key);
int rtw89_cam_sec_key_del(struct rtw89_dev *rtwdev,
			  struct ieee80211_vif *vif,
			  struct ieee80211_sta *sta,
			  struct ieee80211_key_conf *key,
			  bool inform_fw);
void rtw89_cam_bssid_changed(struct rtw89_dev *rtwdev,
			     struct rtw89_vif *rtwvif);
void rtw89_cam_reset_keys(struct rtw89_dev *rtwdev);
#endif

/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2019-2020  Realtek Corporation
 */

#ifndef __RTW89_CAM_H__
#define __RTW89_CAM_H__

#include "core.h"

#define RTW89_SEC_CAM_LEN	20

#define FWCMD_SET_ADDR_IDX(cmd, value)					\
	le32p_replace_bits((__le32 *)(cmd) + 1, value, GENMASK(7, 0))
#define FWCMD_SET_ADDR_OFFSET(cmd, value)				\
	le32p_replace_bits((__le32 *)(cmd) + 1, value, GENMASK(15, 8))
#define FWCMD_SET_ADDR_LEN(cmd, value)					\
	le32p_replace_bits((__le32 *)(cmd) + 1, value, GENMASK(23, 16))
#define FWCMD_SET_ADDR_VALID(cmd, value)				\
	le32p_replace_bits((__le32 *)(cmd) + 2, value, BIT(0))
#define FWCMD_SET_ADDR_NET_TYPE(cmd, value)				\
	le32p_replace_bits((__le32 *)(cmd) + 2, value, GENMASK(2, 1))
#define FWCMD_SET_ADDR_BCN_HIT_COND(cmd, value)				\
	le32p_replace_bits((__le32 *)(cmd) + 2, value, GENMASK(4, 3))
#define FWCMD_SET_ADDR_HIT_RULE(cmd, value)				\
	le32p_replace_bits((__le32 *)(cmd) + 2, value, GENMASK(6, 5))
#define FWCMD_SET_ADDR_BB_SEL(cmd, value)				\
	le32p_replace_bits((__le32 *)(cmd) + 2, value, BIT(7))
#define FWCMD_SET_ADDR_ADDR_MASK(cmd, value)				\
	le32p_replace_bits((__le32 *)(cmd) + 2, value, GENMASK(13, 8))
#define FWCMD_SET_ADDR_MASK_SEL(cmd, value)				\
	le32p_replace_bits((__le32 *)(cmd) + 2, value, GENMASK(15, 14))
#define FWCMD_SET_ADDR_SMA_HASH(cmd, value)				\
	le32p_replace_bits((__le32 *)(cmd) + 2, value, GENMASK(23, 16))
#define FWCMD_SET_ADDR_TMA_HASH(cmd, value)				\
	le32p_replace_bits((__le32 *)(cmd) + 2, value, GENMASK(31, 24))
#define FWCMD_SET_ADDR_BSSID_CAM_IDX(cmd, value)			\
	le32p_replace_bits((__le32 *)(cmd) + 3, value, GENMASK(5, 0))
#define FWCMD_SET_ADDR_SMA0(cmd, value)					\
	le32p_replace_bits((__le32 *)(cmd) + 4, value, GENMASK(7, 0))
#define FWCMD_SET_ADDR_SMA1(cmd, value)					\
	le32p_replace_bits((__le32 *)(cmd) + 4, value, GENMASK(15, 8))
#define FWCMD_SET_ADDR_SMA2(cmd, value)					\
	le32p_replace_bits((__le32 *)(cmd) + 4, value, GENMASK(23, 16))
#define FWCMD_SET_ADDR_SMA3(cmd, value)					\
	le32p_replace_bits((__le32 *)(cmd) + 4, value, GENMASK(31, 24))
#define FWCMD_SET_ADDR_SMA4(cmd, value)					\
	le32p_replace_bits((__le32 *)(cmd) + 5, value, GENMASK(7, 0))
#define FWCMD_SET_ADDR_SMA5(cmd, value)					\
	le32p_replace_bits((__le32 *)(cmd) + 5, value, GENMASK(15, 8))
#define FWCMD_SET_ADDR_TMA0(cmd, value)					\
	le32p_replace_bits((__le32 *)(cmd) + 5, value, GENMASK(23, 16))
#define FWCMD_SET_ADDR_TMA1(cmd, value)					\
	le32p_replace_bits((__le32 *)(cmd) + 5, value, GENMASK(31, 24))
#define FWCMD_SET_ADDR_TMA2(cmd, value)					\
	le32p_replace_bits((__le32 *)(cmd) + 6, value, GENMASK(7, 0))
#define FWCMD_SET_ADDR_TMA3(cmd, value)					\
	le32p_replace_bits((__le32 *)(cmd) + 6, value, GENMASK(15, 8))
#define FWCMD_SET_ADDR_TMA4(cmd, value)					\
	le32p_replace_bits((__le32 *)(cmd) + 6, value, GENMASK(23, 16))
#define FWCMD_SET_ADDR_TMA5(cmd, value)					\
	le32p_replace_bits((__le32 *)(cmd) + 6, value, GENMASK(31, 24))
#define FWCMD_SET_ADDR_MACID(cmd, value)				\
	le32p_replace_bits((__le32 *)(cmd) + 8, value, GENMASK(7, 0))
#define FWCMD_SET_ADDR_PORT_INT(cmd, value)				\
	le32p_replace_bits((__le32 *)(cmd) + 8, value, GENMASK(10, 8))
#define FWCMD_SET_ADDR_TSF_SYNC(cmd, value)				\
	le32p_replace_bits((__le32 *)(cmd) + 8, value, GENMASK(13, 11))
#define FWCMD_SET_ADDR_TF_TRS(cmd, value)				\
	le32p_replace_bits((__le32 *)(cmd) + 8, value, BIT(14))
#define FWCMD_SET_ADDR_LSIG_TXOP(cmd, value)				\
	le32p_replace_bits((__le32 *)(cmd) + 8, value, BIT(15))
#define FWCMD_SET_ADDR_TGT_IND(cmd, value)				\
	le32p_replace_bits((__le32 *)(cmd) + 8, value, GENMASK(26, 24))
#define FWCMD_SET_ADDR_FRM_TGT_IND(cmd, value)				\
	le32p_replace_bits((__le32 *)(cmd) + 8, value, GENMASK(29, 27))
#define FWCMD_SET_ADDR_AID12(cmd, value)				\
	le32p_replace_bits((__le32 *)(cmd) + 9, value, GENMASK(11, 0))
#define FWCMD_SET_ADDR_AID12_0(cmd, value)				\
	le32p_replace_bits((__le32 *)(cmd) + 9, value, GENMASK(7, 0))
#define FWCMD_SET_ADDR_AID12_1(cmd, value)				\
	le32p_replace_bits((__le32 *)(cmd) + 9, value, GENMASK(11, 8))
#define FWCMD_SET_ADDR_WOL_PATTERN(cmd, value)				\
	le32p_replace_bits((__le32 *)(cmd) + 9, value, BIT(12))
#define FWCMD_SET_ADDR_WOL_UC(cmd, value)				\
	le32p_replace_bits((__le32 *)(cmd) + 9, value, BIT(13))
#define FWCMD_SET_ADDR_WOL_MAGIC(cmd, value)				\
	le32p_replace_bits((__le32 *)(cmd) + 9, value, BIT(14))
#define FWCMD_SET_ADDR_WAPI(cmd, value)					\
	le32p_replace_bits((__le32 *)(cmd) + 9, value, BIT(15))
#define FWCMD_SET_ADDR_SEC_ENT_MODE(cmd, value)				\
	le32p_replace_bits((__le32 *)(cmd) + 9, value, GENMASK(17, 16))
#define FWCMD_SET_ADDR_SEC_ENT0_KEYID(cmd, value)			\
	le32p_replace_bits((__le32 *)(cmd) + 9, value, GENMASK(19, 18))
#define FWCMD_SET_ADDR_SEC_ENT1_KEYID(cmd, value)			\
	le32p_replace_bits((__le32 *)(cmd) + 9, value, GENMASK(21, 20))
#define FWCMD_SET_ADDR_SEC_ENT2_KEYID(cmd, value)			\
	le32p_replace_bits((__le32 *)(cmd) + 9, value, GENMASK(23, 22))
#define FWCMD_SET_ADDR_SEC_ENT3_KEYID(cmd, value)			\
	le32p_replace_bits((__le32 *)(cmd) + 9, value, GENMASK(25, 24))
#define FWCMD_SET_ADDR_SEC_ENT4_KEYID(cmd, value)			\
	le32p_replace_bits((__le32 *)(cmd) + 9, value, GENMASK(27, 26))
#define FWCMD_SET_ADDR_SEC_ENT5_KEYID(cmd, value)			\
	le32p_replace_bits((__le32 *)(cmd) + 9, value, GENMASK(29, 28))
#define FWCMD_SET_ADDR_SEC_ENT6_KEYID(cmd, value)			\
	le32p_replace_bits((__le32 *)(cmd) + 9, value, GENMASK(31, 30))
#define FWCMD_SET_ADDR_SEC_ENT_VALID(cmd, value)			\
	le32p_replace_bits((__le32 *)(cmd) + 10, value, GENMASK(7, 0))
#define FWCMD_SET_ADDR_SEC_ENT0(cmd, value)				\
	le32p_replace_bits((__le32 *)(cmd) + 10, value, GENMASK(15, 8))
#define FWCMD_SET_ADDR_SEC_ENT1(cmd, value)				\
	le32p_replace_bits((__le32 *)(cmd) + 10, value, GENMASK(23, 16))
#define FWCMD_SET_ADDR_SEC_ENT2(cmd, value)				\
	le32p_replace_bits((__le32 *)(cmd) + 10, value, GENMASK(31, 24))
#define FWCMD_SET_ADDR_SEC_ENT3(cmd, value)				\
	le32p_replace_bits((__le32 *)(cmd) + 11, value, GENMASK(7, 0))
#define FWCMD_SET_ADDR_SEC_ENT4(cmd, value)				\
	le32p_replace_bits((__le32 *)(cmd) + 11, value, GENMASK(15, 8))
#define FWCMD_SET_ADDR_SEC_ENT5(cmd, value)				\
	le32p_replace_bits((__le32 *)(cmd) + 11, value, GENMASK(23, 16))
#define FWCMD_SET_ADDR_SEC_ENT6(cmd, value)				\
	le32p_replace_bits((__le32 *)(cmd) + 11, value, GENMASK(31, 24))
#define FWCMD_SET_ADDR_BSSID_IDX(cmd, value)				\
	le32p_replace_bits((__le32 *)(cmd) + 12, value, GENMASK(7, 0))
#define FWCMD_SET_ADDR_BSSID_OFFSET(cmd, value)				\
	le32p_replace_bits((__le32 *)(cmd) + 12, value, GENMASK(15, 8))
#define FWCMD_SET_ADDR_BSSID_LEN(cmd, value)				\
	le32p_replace_bits((__le32 *)(cmd) + 12, value, GENMASK(23, 16))
#define FWCMD_SET_ADDR_BSSID_VALID(cmd, value)				\
	le32p_replace_bits((__le32 *)(cmd) + 13, value, BIT(0))
#define FWCMD_SET_ADDR_BSSID_BB_SEL(cmd, value)				\
	le32p_replace_bits((__le32 *)(cmd) + 13, value, BIT(1))
#define FWCMD_SET_ADDR_BSSID_BSS_COLOR(cmd, value)			\
	le32p_replace_bits((__le32 *)(cmd) + 13, value, GENMASK(13, 8))
#define FWCMD_SET_ADDR_BSSID_BSSID0(cmd, value)				\
	le32p_replace_bits((__le32 *)(cmd) + 13, value, GENMASK(23, 16))
#define FWCMD_SET_ADDR_BSSID_BSSID1(cmd, value)				\
	le32p_replace_bits((__le32 *)(cmd) + 13, value, GENMASK(31, 24))
#define FWCMD_SET_ADDR_BSSID_BSSID2(cmd, value)				\
	le32p_replace_bits((__le32 *)(cmd) + 14, value, GENMASK(7, 0))
#define FWCMD_SET_ADDR_BSSID_BSSID3(cmd, value)				\
	le32p_replace_bits((__le32 *)(cmd) + 14, value, GENMASK(15, 8))
#define FWCMD_SET_ADDR_BSSID_BSSID4(cmd, value)				\
	le32p_replace_bits((__le32 *)(cmd) + 14, value, GENMASK(23, 16))
#define FWCMD_SET_ADDR_BSSID_BSSID5(cmd, value)				\
	le32p_replace_bits((__le32 *)(cmd) + 14, value, GENMASK(31, 24))

int rtw89_cam_init(struct rtw89_dev *rtwdev, struct rtw89_vif *vif);
void rtw89_cam_deinit(struct rtw89_dev *rtwdev, struct rtw89_vif *vif);
void rtw89_cam_fill_addr_cam_info(struct rtw89_dev *rtwdev,
				  struct rtw89_vif *vif, u8 *cmd);
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

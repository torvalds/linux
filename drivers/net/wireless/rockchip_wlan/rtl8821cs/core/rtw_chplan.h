/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2007 - 2018 Realtek Corporation.
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
 *****************************************************************************/
#ifndef __RTW_CHPLAN_H__
#define __RTW_CHPLAN_H__

#define RTW_CHPLAN_UNSPECIFIED 0xFF

u8 rtw_chplan_get_default_regd(u8 id);
bool rtw_chplan_is_empty(u8 id);
bool rtw_is_channel_plan_valid(u8 id);
bool rtw_regsty_is_excl_chs(struct registry_priv *regsty, u8 ch);

enum regd_src_t {
	REGD_SRC_RTK_PRIV = 0, /* Regulatory settings from Realtek framework (Realtek defined or customized) */
	REGD_SRC_OS = 1, /* Regulatory settings from OS */
	REGD_SRC_NUM,
};

#define regd_src_is_valid(src) ((src) < REGD_SRC_NUM)

extern const char *_regd_src_str[];
#define regd_src_str(src) ((src) >= REGD_SRC_NUM ? _regd_src_str[REGD_SRC_NUM] : _regd_src_str[src])

struct _RT_CHANNEL_INFO;
u8 init_channel_set(_adapter *adapter);
bool rtw_chset_is_dfs_range(struct _RT_CHANNEL_INFO *chset, u32 hi, u32 lo);
bool rtw_chset_is_dfs_ch(struct _RT_CHANNEL_INFO *chset, u8 ch);
bool rtw_chset_is_dfs_chbw(struct _RT_CHANNEL_INFO *chset, u8 ch, u8 bw, u8 offset);
u8 rtw_process_beacon_hint(_adapter *adapter, WLAN_BSSID_EX *bss);

#define IS_ALPHA2_NO_SPECIFIED(_alpha2) ((*((u16 *)(_alpha2))) == 0xFFFF)
#define IS_ALPHA2_WORLDWIDE(_alpha2) (strncmp(_alpha2, "00", 2) == 0)

#define RTW_MODULE_RTL8821AE_HMC_M2		BIT0	/* RTL8821AE(HMC + M.2) */
#define RTW_MODULE_RTL8821AU			BIT1	/* RTL8821AU */
#define RTW_MODULE_RTL8812AENF_NGFF		BIT2	/* RTL8812AENF(8812AE+8761)_NGFF */
#define RTW_MODULE_RTL8812AEBT_HMC		BIT3	/* RTL8812AEBT(8812AE+8761)_HMC */
#define RTW_MODULE_RTL8188EE_HMC_M2		BIT4	/* RTL8188EE(HMC + M.2) */
#define RTW_MODULE_RTL8723BE_HMC_M2		BIT5	/* RTL8723BE(HMC + M.2) */
#define RTW_MODULE_RTL8723BS_NGFF1216	BIT6	/* RTL8723BS(NGFF1216) */
#define RTW_MODULE_RTL8192EEBT_HMC_M2	BIT7	/* RTL8192EEBT(8192EE+8761AU)_(HMC + M.2) */
#define RTW_MODULE_RTL8723DE_NGFF1630	BIT8	/* RTL8723DE(NGFF1630) */
#define RTW_MODULE_RTL8822BE			BIT9	/* RTL8822BE */
#define RTW_MODULE_RTL8821CE			BIT10	/* RTL8821CE */
#define RTW_MODULE_RTL8822CE			BIT11	/* RTL8822CE */

enum rtw_dfs_regd {
	RTW_DFS_REGD_NONE	= 0,
	RTW_DFS_REGD_FCC	= 1,
	RTW_DFS_REGD_MKK	= 2,
	RTW_DFS_REGD_ETSI	= 3,
	RTW_DFS_REGD_NUM,
	RTW_DFS_REGD_AUTO	= 0xFF, /* follow channel plan */
};

extern const char *_rtw_dfs_regd_str[];
#define rtw_dfs_regd_str(region) (((region) >= RTW_DFS_REGD_NUM) ? _rtw_dfs_regd_str[RTW_DFS_REGD_NONE] : _rtw_dfs_regd_str[(region)])

struct country_chplan {
	char alpha2[2]; /* "00" means worldwide */
	u8 chplan;
#ifdef CONFIG_80211AC_VHT
	u8 en_11ac;
#endif
};

#ifdef CONFIG_80211AC_VHT
#define COUNTRY_CHPLAN_EN_11AC(_ent) ((_ent)->en_11ac)
#else
#define COUNTRY_CHPLAN_EN_11AC(_ent) 0
#endif

const struct country_chplan *rtw_get_chplan_from_country(const char *country_code);

void dump_country_chplan(void *sel, const struct country_chplan *ent);
void dump_country_chplan_map(void *sel);
void dump_chplan_id_list(void *sel);
#ifdef CONFIG_RTW_DEBUG
void dump_chplan_test(void *sel);
#endif
void dump_chplan_ver(void *sel);

#endif /* __RTW_CHPLAN_H__ */

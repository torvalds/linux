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

struct _RT_CHANNEL_INFO;
u8 init_channel_set(_adapter *padapter, u8 ChannelPlan, struct _RT_CHANNEL_INFO *channel_set);
bool rtw_chset_is_dfs_range(struct _RT_CHANNEL_INFO *chset, u32 hi, u32 lo);
bool rtw_chset_is_dfs_ch(struct _RT_CHANNEL_INFO *chset, u8 ch);
bool rtw_chset_is_dfs_chbw(struct _RT_CHANNEL_INFO *chset, u8 ch, u8 bw, u8 offset);
void rtw_process_beacon_hint(_adapter *adapter, WLAN_BSSID_EX *bss);

#define IS_ALPHA2_NO_SPECIFIED(_alpha2) ((*((u16 *)(_alpha2))) == 0xFFFF)

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

struct country_chplan {
	char alpha2[2];
	u8 chplan;
#ifdef CONFIG_80211AC_VHT
	u8 en_11ac;
#endif
#if RTW_DEF_MODULE_REGULATORY_CERT
	u16 def_module_flags; /* RTW_MODULE_RTLXXX */
#endif
};

#ifdef CONFIG_80211AC_VHT
#define COUNTRY_CHPLAN_EN_11AC(_ent) ((_ent)->en_11ac)
#else
#define COUNTRY_CHPLAN_EN_11AC(_ent) 0
#endif

#if RTW_DEF_MODULE_REGULATORY_CERT
#define COUNTRY_CHPLAN_DEF_MODULE_FALGS(_ent) ((_ent)->def_module_flags)
#else
#define COUNTRY_CHPLAN_DEF_MODULE_FALGS(_ent) 0
#endif

const struct country_chplan *rtw_get_chplan_from_country(const char *country_code);

void dump_country_chplan(void *sel, const struct country_chplan *ent);
void dump_country_chplan_map(void *sel);
void dump_chplan_id_list(void *sel);
void dump_chplan_test(void *sel);
void dump_chplan_ver(void *sel);

#endif /* __RTW_CHPLAN_H__ */

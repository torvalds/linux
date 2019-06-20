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

enum rtw_chplan_id {
	/* ===== 0x00 ~ 0x1F, legacy channel plan ===== */
	RTW_CHPLAN_FCC = 0x00,
	RTW_CHPLAN_IC = 0x01,
	RTW_CHPLAN_ETSI = 0x02,
	RTW_CHPLAN_SPAIN = 0x03,
	RTW_CHPLAN_FRANCE = 0x04,
	RTW_CHPLAN_MKK = 0x05,
	RTW_CHPLAN_MKK1 = 0x06,
	RTW_CHPLAN_ISRAEL = 0x07,
	RTW_CHPLAN_TELEC = 0x08,
	RTW_CHPLAN_GLOBAL_DOAMIN = 0x09,
	RTW_CHPLAN_WORLD_WIDE_13 = 0x0A,
	RTW_CHPLAN_TAIWAN = 0x0B,
	RTW_CHPLAN_CHINA = 0x0C,
	RTW_CHPLAN_SINGAPORE_INDIA_MEXICO = 0x0D,
	RTW_CHPLAN_KOREA = 0x0E,
	RTW_CHPLAN_TURKEY = 0x0F,
	RTW_CHPLAN_JAPAN = 0x10,
	RTW_CHPLAN_FCC_NO_DFS = 0x11,
	RTW_CHPLAN_JAPAN_NO_DFS = 0x12,
	RTW_CHPLAN_WORLD_WIDE_5G = 0x13,
	RTW_CHPLAN_TAIWAN_NO_DFS = 0x14,

	/* ===== 0x20 ~ 0x7F, new channel plan ===== */
	RTW_CHPLAN_WORLD_NULL = 0x20,
	RTW_CHPLAN_ETSI1_NULL = 0x21,
	RTW_CHPLAN_FCC1_NULL = 0x22,
	RTW_CHPLAN_MKK1_NULL = 0x23,
	RTW_CHPLAN_ETSI2_NULL = 0x24,
	RTW_CHPLAN_FCC1_FCC1 = 0x25,
	RTW_CHPLAN_WORLD_ETSI1 = 0x26,
	RTW_CHPLAN_MKK1_MKK1 = 0x27,
	RTW_CHPLAN_WORLD_KCC1 = 0x28,
	RTW_CHPLAN_WORLD_FCC2 = 0x29,
	RTW_CHPLAN_FCC2_NULL = 0x2A,
	RTW_CHPLAN_IC1_IC2 = 0x2B,
	RTW_CHPLAN_MKK2_NULL = 0x2C,
	RTW_CHPLAN_WORLD_CHILE1= 0x2D,
	RTW_CHPLAN_WORLD1_WORLD1 = 0x2E,
	RTW_CHPLAN_WORLD_CHILE2 = 0x2F,
	RTW_CHPLAN_WORLD_FCC3 = 0x30,
	RTW_CHPLAN_WORLD_FCC4 = 0x31,
	RTW_CHPLAN_WORLD_FCC5 = 0x32,
	RTW_CHPLAN_WORLD_FCC6 = 0x33,
	RTW_CHPLAN_FCC1_FCC7 = 0x34,
	RTW_CHPLAN_WORLD_ETSI2 = 0x35,
	RTW_CHPLAN_WORLD_ETSI3 = 0x36,
	RTW_CHPLAN_MKK1_MKK2 = 0x37,
	RTW_CHPLAN_MKK1_MKK3 = 0x38,
	RTW_CHPLAN_FCC1_NCC1 = 0x39,
	RTW_CHPLAN_ETSI1_ETSI1 = 0x3A,
	RTW_CHPLAN_ETSI1_ACMA1 = 0x3B,
	RTW_CHPLAN_ETSI1_ETSI6 = 0x3C,
	RTW_CHPLAN_ETSI1_ETSI12 = 0x3D,
	RTW_CHPLAN_KCC1_KCC2 = 0x3E,
	RTW_CHPLAN_FCC1_FCC11 = 0x3F,
	RTW_CHPLAN_FCC1_NCC2 = 0x40,
	RTW_CHPLAN_GLOBAL_NULL = 0x41,
	RTW_CHPLAN_ETSI1_ETSI4 = 0x42,
	RTW_CHPLAN_FCC1_FCC2 = 0x43,
	RTW_CHPLAN_FCC1_NCC3 = 0x44,
	RTW_CHPLAN_WORLD_ACMA1 = 0x45,
	RTW_CHPLAN_FCC1_FCC8 = 0x46,
	RTW_CHPLAN_WORLD_ETSI6 = 0x47,
	RTW_CHPLAN_WORLD_ETSI7 = 0x48,
	RTW_CHPLAN_WORLD_ETSI8 = 0x49,
	RTW_CHPLAN_IC2_IC2 = 0x4A,
	RTW_CHPLAN_WORLD_ETSI9 = 0x50,
	RTW_CHPLAN_WORLD_ETSI10 = 0x51,
	RTW_CHPLAN_WORLD_ETSI11 = 0x52,
	RTW_CHPLAN_FCC1_NCC4 = 0x53,
	RTW_CHPLAN_WORLD_ETSI12 = 0x54,
	RTW_CHPLAN_FCC1_FCC9 = 0x55,
	RTW_CHPLAN_WORLD_ETSI13 = 0x56,
	RTW_CHPLAN_FCC1_FCC10 = 0x57,
	RTW_CHPLAN_MKK2_MKK4 = 0x58,
	RTW_CHPLAN_WORLD_ETSI14 = 0x59,
	RTW_CHPLAN_FCC1_FCC5 = 0x60,
	RTW_CHPLAN_FCC2_FCC7 = 0x61,
	RTW_CHPLAN_FCC2_FCC1 = 0x62,
	RTW_CHPLAN_WORLD_ETSI15 = 0x63,
	RTW_CHPLAN_MKK2_MKK5 = 0x64,
	RTW_CHPLAN_ETSI1_ETSI16 = 0x65,
	RTW_CHPLAN_FCC1_FCC14 = 0x66,
	RTW_CHPLAN_FCC1_FCC12 = 0x67,
	RTW_CHPLAN_FCC2_FCC14 = 0x68,
	RTW_CHPLAN_FCC2_FCC12 = 0x69,
	RTW_CHPLAN_ETSI1_ETSI17 = 0x6A,
	RTW_CHPLAN_WORLD_FCC16 = 0x6B,
	RTW_CHPLAN_WORLD_FCC13 = 0x6C,
	RTW_CHPLAN_FCC2_FCC15 = 0x6D,
	RTW_CHPLAN_WORLD_FCC12 = 0x6E,
	RTW_CHPLAN_NULL_ETSI8 = 0x6F,
	RTW_CHPLAN_NULL_ETSI18 = 0x70,
	RTW_CHPLAN_NULL_ETSI17 = 0x71,
	RTW_CHPLAN_NULL_ETSI19 = 0x72,
	RTW_CHPLAN_WORLD_FCC7 = 0x73,
	RTW_CHPLAN_FCC2_FCC17 = 0x74,
	RTW_CHPLAN_WORLD_ETSI20 = 0x75,
	RTW_CHPLAN_FCC2_FCC11 = 0x76,
	RTW_CHPLAN_WORLD_ETSI21 = 0x77,
	RTW_CHPLAN_FCC1_FCC18 = 0x78,
	RTW_CHPLAN_MKK2_MKK1 = 0x79,

	RTW_CHPLAN_MAX,
	RTW_CHPLAN_REALTEK_DEFINE = 0x7F,
	RTW_CHPLAN_UNSPECIFIED = 0xFF,
};

u8 rtw_chplan_get_default_regd(u8 id);
bool rtw_chplan_is_empty(u8 id);
#define rtw_is_channel_plan_valid(chplan) (((chplan) < RTW_CHPLAN_MAX || (chplan) == RTW_CHPLAN_REALTEK_DEFINE) && !rtw_chplan_is_empty(chplan))
#define rtw_is_legacy_channel_plan(chplan) ((chplan) < 0x20)

struct _RT_CHANNEL_INFO;
u8 init_channel_set(_adapter *padapter, u8 ChannelPlan, struct _RT_CHANNEL_INFO *channel_set);

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

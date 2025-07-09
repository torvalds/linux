/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2021-2023  Realtek Corporation
 */

#ifndef __RTW89_ACPI_H__
#define __RTW89_ACPI_H__

#include "core.h"

struct rtw89_acpi_data {
	u32 len;
	u8 buf[] __counted_by(len);
};

enum rtw89_acpi_dsm_func {
	RTW89_ACPI_DSM_FUNC_IDN_BAND_SUP = 2,
	RTW89_ACPI_DSM_FUNC_6G_DIS = 3,
	RTW89_ACPI_DSM_FUNC_6G_BP = 4,
	RTW89_ACPI_DSM_FUNC_TAS_EN = 5,
	RTW89_ACPI_DSM_FUNC_UNII4_SUP = 6,
	RTW89_ACPI_DSM_FUNC_6GHZ_SP_SUP = 7,
};

enum rtw89_acpi_conf_unii4 {
	RTW89_ACPI_CONF_UNII4_US = BIT(0),
	RTW89_ACPI_CONF_UNII4_CA = BIT(1),
};

enum rtw89_acpi_policy_mode {
	RTW89_ACPI_POLICY_BLOCK = 0,
	RTW89_ACPI_POLICY_ALLOW = 1,
};

enum rtw89_acpi_conf_tas {
	RTW89_ACPI_CONF_TAS_US = BIT(0),
	RTW89_ACPI_CONF_TAS_CA = BIT(1),
	RTW89_ACPI_CONF_TAS_KR = BIT(2),
	RTW89_ACPI_CONF_TAS_OTHERS = BIT(7),
};

struct rtw89_acpi_country_code {
	/* below are allowed:
	 * * ISO alpha2 country code
	 * * EU for countries in Europe
	 */
	char alpha2[2];
} __packed;

struct rtw89_acpi_policy_6ghz {
	u8 signature[3];
	u8 rsvd;
	u8 policy_mode;
	u8 country_count;
	struct rtw89_acpi_country_code country_list[] __counted_by(country_count);
} __packed;

enum rtw89_acpi_conf_6ghz_sp {
	RTW89_ACPI_CONF_6GHZ_SP_US = BIT(0),
	RTW89_ACPI_CONF_6GHZ_SP_CA = BIT(1),
};

struct rtw89_acpi_policy_6ghz_sp {
	u8 signature[4];
	u8 revision;
	u8 override;
	u8 conf;
	u8 rsvd;
} __packed;

struct rtw89_acpi_policy_tas {
	u8 signature[4];
	u8 revision;
	u8 enable;
	u8 enabled_countries;
	u8 rsvd[3];
} __packed;

struct rtw89_acpi_dsm_result {
	union {
		u8 value;
		/* caller needs to free it after using */
		struct rtw89_acpi_policy_6ghz *policy_6ghz;
		struct rtw89_acpi_policy_6ghz_sp *policy_6ghz_sp;
		struct rtw89_acpi_policy_tas *policy_tas;
	} u;
};

struct rtw89_acpi_rtag_result {
	u8 tag[4];
	u8 revision;
	__le32 domain;
	u8 ant_gain_table[RTW89_ANT_GAIN_CHAIN_NUM][RTW89_ANT_GAIN_SUBBAND_NR];
} __packed;

enum rtw89_acpi_sar_cid {
	RTW89_ACPI_SAR_CID_HP = 0x5048,
	RTW89_ACPI_SAR_CID_RT = 0x5452,
};

enum rtw89_acpi_sar_rev {
	RTW89_ACPI_SAR_REV_LEGACY = 1,
	RTW89_ACPI_SAR_REV_HAS_6GHZ = 2,
};

#define RTW89_ACPI_SAR_ANT_NR_STD 4
#define RTW89_ACPI_SAR_ANT_NR_SML 2

#define RTW89_ACPI_METHOD_STATIC_SAR "WRDS"
#define RTW89_ACPI_METHOD_DYNAMIC_SAR "RWRD"
#define RTW89_ACPI_METHOD_DYNAMIC_SAR_INDICATOR "RWSI"
#define RTW89_ACPI_METHOD_GEO_SAR "RWGS"

struct rtw89_acpi_sar_std_legacy {
	u8 v[RTW89_ACPI_SAR_ANT_NR_STD][RTW89_ACPI_SAR_SUBBAND_NR_LEGACY];
} __packed;

struct rtw89_acpi_sar_std_has_6ghz {
	u8 v[RTW89_ACPI_SAR_ANT_NR_STD][RTW89_ACPI_SAR_SUBBAND_NR_HAS_6GHZ];
} __packed;

struct rtw89_acpi_sar_sml_legacy {
	u8 v[RTW89_ACPI_SAR_ANT_NR_SML][RTW89_ACPI_SAR_SUBBAND_NR_LEGACY];
} __packed;

struct rtw89_acpi_sar_sml_has_6ghz {
	u8 v[RTW89_ACPI_SAR_ANT_NR_SML][RTW89_ACPI_SAR_SUBBAND_NR_HAS_6GHZ];
} __packed;

struct rtw89_acpi_static_sar_hdr {
	__le16 cid;
	u8 rev;
	u8 content[];
} __packed;

struct rtw89_acpi_dynamic_sar_hdr {
	__le16 cid;
	u8 rev;
	u8 cnt;
	u8 content[];
} __packed;

struct rtw89_acpi_sar_identifier {
	enum rtw89_acpi_sar_cid cid;
	enum rtw89_acpi_sar_rev rev;
	u8 size;
};

/* for rtw89_acpi_sar_identifier::size */
#define RTW89_ACPI_SAR_SIZE_MAX U8_MAX
#define RTW89_ACPI_SAR_SIZE_OF(type) \
	(BUILD_BUG_ON_ZERO(sizeof(struct rtw89_acpi_sar_ ## type) > \
			   RTW89_ACPI_SAR_SIZE_MAX) + \
	 sizeof(struct rtw89_acpi_sar_ ## type))

struct rtw89_acpi_sar_recognition {
	struct rtw89_acpi_sar_identifier id;
	const struct rtw89_acpi_geo_sar_handler *geo;

	u8 (*rfpath_to_antidx)(enum rtw89_rf_path rfpath);
	s16 (*normalize)(u8 v);
	void (*load)(struct rtw89_dev *rtwdev,
		     const struct rtw89_acpi_sar_recognition *rec,
		     const void *content,
		     struct rtw89_sar_entry_from_acpi *ent);
};

struct rtw89_acpi_geo_sar_hp_val {
	u8 max;
	s8 delta[RTW89_ACPI_SAR_ANT_NR_STD];
} __packed;

struct rtw89_acpi_geo_sar_hp_legacy_entry {
	struct rtw89_acpi_geo_sar_hp_val val_2ghz;
	struct rtw89_acpi_geo_sar_hp_val val_5ghz;
} __packed;

struct rtw89_acpi_geo_sar_hp_has_6ghz_entry {
	struct rtw89_acpi_geo_sar_hp_val val_2ghz;
	struct rtw89_acpi_geo_sar_hp_val val_5ghz;
	struct rtw89_acpi_geo_sar_hp_val val_6ghz;
} __packed;

enum rtw89_acpi_geo_sar_regd_hp {
	RTW89_ACPI_GEO_SAR_REGD_HP_FCC = 0,
	RTW89_ACPI_GEO_SAR_REGD_HP_ETSI = 1,
	RTW89_ACPI_GEO_SAR_REGD_HP_WW = 2,

	RTW89_ACPI_GEO_SAR_REGD_NR_HP,
};

struct rtw89_acpi_geo_sar_hp_legacy {
	struct rtw89_acpi_geo_sar_hp_legacy_entry
		entries[RTW89_ACPI_GEO_SAR_REGD_NR_HP];
} __packed;

struct rtw89_acpi_geo_sar_hp_has_6ghz {
	struct rtw89_acpi_geo_sar_hp_has_6ghz_entry
		entries[RTW89_ACPI_GEO_SAR_REGD_NR_HP];
} __packed;

struct rtw89_acpi_geo_sar_rt_val {
	u8 max;
	s8 delta;
} __packed;

struct rtw89_acpi_geo_sar_rt_legacy_entry {
	struct rtw89_acpi_geo_sar_rt_val val_2ghz;
	struct rtw89_acpi_geo_sar_rt_val val_5ghz;
} __packed;

struct rtw89_acpi_geo_sar_rt_has_6ghz_entry {
	struct rtw89_acpi_geo_sar_rt_val val_2ghz;
	struct rtw89_acpi_geo_sar_rt_val val_5ghz;
	struct rtw89_acpi_geo_sar_rt_val val_6ghz;
} __packed;

enum rtw89_acpi_geo_sar_regd_rt {
	RTW89_ACPI_GEO_SAR_REGD_RT_FCC = 0,
	RTW89_ACPI_GEO_SAR_REGD_RT_ETSI = 1,
	RTW89_ACPI_GEO_SAR_REGD_RT_MKK = 2,
	RTW89_ACPI_GEO_SAR_REGD_RT_IC = 3,
	RTW89_ACPI_GEO_SAR_REGD_RT_KCC = 4,
	RTW89_ACPI_GEO_SAR_REGD_RT_WW = 5,

	RTW89_ACPI_GEO_SAR_REGD_NR_RT,
};

struct rtw89_acpi_geo_sar_rt_legacy {
	struct rtw89_acpi_geo_sar_rt_legacy_entry
		entries[RTW89_ACPI_GEO_SAR_REGD_NR_RT];
} __packed;

struct rtw89_acpi_geo_sar_rt_has_6ghz {
	struct rtw89_acpi_geo_sar_rt_has_6ghz_entry
		entries[RTW89_ACPI_GEO_SAR_REGD_NR_RT];
} __packed;

struct rtw89_acpi_geo_sar_handler {
	u8 data_size;

	void (*load)(struct rtw89_dev *rtwdev,
		     const void *content,
		     enum rtw89_regulation_type regd,
		     struct rtw89_sar_entry_from_acpi *ent);
};

/* for rtw89_acpi_geo_sar_handler::data_size */
#define RTW89_ACPI_GEO_SAR_SIZE_MAX U8_MAX
#define RTW89_ACPI_GEO_SAR_SIZE_OF(type) \
	(BUILD_BUG_ON_ZERO(sizeof(struct rtw89_acpi_geo_sar_ ## type) > \
			   RTW89_ACPI_GEO_SAR_SIZE_MAX) + \
	 sizeof(struct rtw89_acpi_geo_sar_ ## type))

enum rtw89_acpi_sar_subband rtw89_acpi_sar_get_subband(struct rtw89_dev *rtwdev,
						       u32 center_freq);
enum rtw89_band rtw89_acpi_sar_subband_to_band(struct rtw89_dev *rtwdev,
					       enum rtw89_acpi_sar_subband subband);

int rtw89_acpi_evaluate_dsm(struct rtw89_dev *rtwdev,
			    enum rtw89_acpi_dsm_func func,
			    struct rtw89_acpi_dsm_result *res);
int rtw89_acpi_evaluate_rtag(struct rtw89_dev *rtwdev,
			     struct rtw89_acpi_rtag_result *res);
int rtw89_acpi_evaluate_sar(struct rtw89_dev *rtwdev,
			    struct rtw89_sar_cfg_acpi *cfg);
int rtw89_acpi_evaluate_dynamic_sar_indicator(struct rtw89_dev *rtwdev,
					      struct rtw89_sar_cfg_acpi *cfg,
					      bool *changed);

#endif

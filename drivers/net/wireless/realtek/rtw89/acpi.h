/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2021-2023  Realtek Corporation
 */

#ifndef __RTW89_ACPI_H__
#define __RTW89_ACPI_H__

#include "core.h"

enum rtw89_acpi_dsm_func {
	RTW89_ACPI_DSM_FUNC_IDN_BAND_SUP = 2,
	RTW89_ACPI_DSM_FUNC_6G_DIS = 3,
	RTW89_ACPI_DSM_FUNC_6G_BP = 4,
	RTW89_ACPI_DSM_FUNC_TAS_EN = 5,
	RTW89_ACPI_DSM_FUNC_UNII4_SUP = 6,
	RTW89_ACPI_DSM_FUNC_6GHZ_SP_SUP = 7,
};

enum rtw89_acpi_conf_unii4 {
	RTW89_ACPI_CONF_UNII4_FCC = BIT(0),
	RTW89_ACPI_CONF_UNII4_IC = BIT(1),
};

enum rtw89_acpi_policy_mode {
	RTW89_ACPI_POLICY_BLOCK = 0,
	RTW89_ACPI_POLICY_ALLOW = 1,
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
};

struct rtw89_acpi_policy_6ghz_sp {
	u8 signature[4];
	u8 revision;
	u8 override;
	u8 conf;
	u8 rsvd;
} __packed;

struct rtw89_acpi_dsm_result {
	union {
		u8 value;
		/* caller needs to free it after using */
		struct rtw89_acpi_policy_6ghz *policy_6ghz;
		struct rtw89_acpi_policy_6ghz_sp *policy_6ghz_sp;
	} u;
};

struct rtw89_acpi_rtag_result {
	u8 tag[4];
	u8 revision;
	__le32 domain;
	u8 ant_gain_table[RTW89_ANT_GAIN_CHAIN_NUM][RTW89_ANT_GAIN_SUBBAND_NR];
} __packed;

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

#endif

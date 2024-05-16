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
	RTW89_ACPI_DSM_FUNC_59G_EN = 6,
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

struct rtw89_acpi_dsm_result {
	union {
		u8 value;
		/* caller needs to free it after using */
		struct rtw89_acpi_policy_6ghz *policy_6ghz;
	} u;
};

int rtw89_acpi_evaluate_dsm(struct rtw89_dev *rtwdev,
			    enum rtw89_acpi_dsm_func func,
			    struct rtw89_acpi_dsm_result *res);

#endif

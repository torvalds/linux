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

int rtw89_acpi_evaluate_dsm(struct rtw89_dev *rtwdev,
			    enum rtw89_acpi_dsm_func func, u8 *value);

#endif

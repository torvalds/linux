/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2019-2020  Realtek Corporation
 */

#ifndef __RTW89_EFUSE_H__
#define __RTW89_EFUSE_H__

#include "core.h"

int rtw89_parse_efuse_map(struct rtw89_dev *rtwdev);
int rtw89_parse_phycap_map(struct rtw89_dev *rtwdev);
int rtw89_read_efuse_ver(struct rtw89_dev *rtwdev, u8 *efv);

#endif

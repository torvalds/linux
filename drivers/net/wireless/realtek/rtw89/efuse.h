/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2019-2020  Realtek Corporation
 */

#ifndef __RTW89_EFUSE_H__
#define __RTW89_EFUSE_H__

#include "core.h"

#define RTW89_EFUSE_BLOCK_ID_MASK GENMASK(31, 16)
#define RTW89_EFUSE_BLOCK_SIZE_MASK GENMASK(15, 0)
#define RTW89_EFUSE_MAX_BLOCK_SIZE 0x10000

struct rtw89_efuse_block_cfg {
	u32 offset;
	u32 size;
};

int rtw89_parse_efuse_map_ax(struct rtw89_dev *rtwdev);
int rtw89_parse_phycap_map_ax(struct rtw89_dev *rtwdev);
int rtw89_cnv_efuse_state_ax(struct rtw89_dev *rtwdev, bool idle);
int rtw89_parse_efuse_map_be(struct rtw89_dev *rtwdev);
int rtw89_parse_phycap_map_be(struct rtw89_dev *rtwdev);
int rtw89_cnv_efuse_state_be(struct rtw89_dev *rtwdev, bool idle);
int rtw89_read_efuse_ver(struct rtw89_dev *rtwdev, u8 *efv);

#endif

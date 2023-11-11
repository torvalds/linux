/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2018-2019  Realtek Corporation
 */

#ifndef __RTW_EFUSE_H__
#define __RTW_EFUSE_H__

#define EFUSE_HW_CAP_IGNORE		0
#define EFUSE_HW_CAP_PTCL_VHT		3
#define EFUSE_HW_CAP_SUPP_BW80		7
#define EFUSE_HW_CAP_SUPP_BW40		6

#define EFUSE_READ_FAIL			0xff

#define GET_EFUSE_HW_CAP_HCI(hw_cap)					       \
	le32_get_bits(*((__le32 *)(hw_cap) + 0x01), GENMASK(3, 0))
#define GET_EFUSE_HW_CAP_BW(hw_cap)					       \
	le32_get_bits(*((__le32 *)(hw_cap) + 0x01), GENMASK(18, 16))
#define GET_EFUSE_HW_CAP_NSS(hw_cap)					       \
	le32_get_bits(*((__le32 *)(hw_cap) + 0x01), GENMASK(20, 19))
#define GET_EFUSE_HW_CAP_ANT_NUM(hw_cap)				       \
	le32_get_bits(*((__le32 *)(hw_cap) + 0x01), GENMASK(23, 21))
#define GET_EFUSE_HW_CAP_PTCL(hw_cap)					       \
	le32_get_bits(*((__le32 *)(hw_cap) + 0x01), GENMASK(27, 26))

int rtw_parse_efuse_map(struct rtw_dev *rtwdev);
int rtw_read8_physical_efuse(struct rtw_dev *rtwdev, u16 addr, u8 *data);

#endif

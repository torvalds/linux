// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright(c) 2019-2022  Realtek Corporation
 */

#include "rtw8852c.h"

static const struct rtw89_chip_ops rtw8852c_chip_ops = {
};

const struct rtw89_chip_info rtw8852c_chip_info = {
	.chip_id		= RTL8852C,
	.ops			= &rtw8852c_chip_ops,
	.fw_name		= "rtw89/rtw8852c_fw.bin",
};
EXPORT_SYMBOL(rtw8852c_chip_info);

MODULE_FIRMWARE("rtw89/rtw8852c_fw.bin");
MODULE_AUTHOR("Realtek Corporation");
MODULE_DESCRIPTION("Realtek 802.11ax wireless 8852C driver");
MODULE_LICENSE("Dual BSD/GPL");

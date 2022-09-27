// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright(c) 2019-2022  Realtek Corporation
 */

#include "core.h"

const struct rtw89_chip_info rtw8852b_chip_info = {
	.chip_id		= RTL8852B,
	.dma_ch_mask		= BIT(RTW89_DMA_ACH4) | BIT(RTW89_DMA_ACH5) |
				  BIT(RTW89_DMA_ACH6) | BIT(RTW89_DMA_ACH7) |
				  BIT(RTW89_DMA_B1MG) | BIT(RTW89_DMA_B1HI),
};
EXPORT_SYMBOL(rtw8852b_chip_info);

MODULE_FIRMWARE("rtw89/rtw8852b_fw.bin");
MODULE_AUTHOR("Realtek Corporation");
MODULE_DESCRIPTION("Realtek 802.11ax wireless 8852B driver");
MODULE_LICENSE("Dual BSD/GPL");

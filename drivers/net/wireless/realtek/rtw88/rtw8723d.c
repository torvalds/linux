// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright(c) 2018-2019  Realtek Corporation
 */

#include "main.h"
#include "coex.h"
#include "fw.h"
#include "tx.h"
#include "rx.h"
#include "phy.h"
#include "rtw8723d.h"
#include "rtw8723d_table.h"
#include "mac.h"
#include "reg.h"
#include "debug.h"

static struct rtw_chip_ops rtw8723d_ops = {
	.set_antenna		= NULL,
};

struct rtw_chip_info rtw8723d_hw_spec = {
	.ops = &rtw8723d_ops,
	.id = RTW_CHIP_TYPE_8723D,
	.fw_name = "rtw88/rtw8723d_fw.bin",
	.tx_pkt_desc_sz = 40,
	.tx_buf_desc_sz = 16,
	.rx_pkt_desc_sz = 24,
	.rx_buf_desc_sz = 8,
	.phy_efuse_size = 512,
	.log_efuse_size = 512,
	.ptct_efuse_size = 96 + 1,
	.txgi_factor = 1,
	.is_pwr_by_rate_dec = true,
	.max_power_index = 0x3f,
	.csi_buf_pg_num = 0,
	.band = RTW_BAND_2G,
	.ht_supported = true,
	.vht_supported = false,
	.lps_deep_mode_supported = 0,
	.sys_func_en = 0xFD,
};
EXPORT_SYMBOL(rtw8723d_hw_spec);

MODULE_FIRMWARE("rtw88/rtw8723d_fw.bin");

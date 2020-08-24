/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2016 - 2019 Realtek Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 ******************************************************************************/

#ifndef _HALMAC_SDIO_88XX_H_
#define _HALMAC_SDIO_88XX_H_

#include "../halmac_api.h"

#if (HALMAC_88XX_SUPPORT && HALMAC_SDIO_SUPPORT)

enum halmac_ret_status
init_sdio_cfg_88xx(struct halmac_adapter *adapter);

enum halmac_ret_status
deinit_sdio_cfg_88xx(struct halmac_adapter *adapter);

enum halmac_ret_status
cfg_sdio_rx_agg_88xx(struct halmac_adapter *adapter,
		     struct halmac_rxagg_cfg *cfg);

enum halmac_ret_status
cfg_txagg_sdio_align_88xx(struct halmac_adapter *adapter, u8 enable,
			  u16 align_size);

u32
sdio_indirect_reg_r32_88xx(struct halmac_adapter *adapter, u32 offset);

enum halmac_ret_status
sdio_reg_rn_88xx(struct halmac_adapter *adapter, u32 offset, u32 size,
		 u8 *value);

enum halmac_ret_status
set_sdio_bulkout_num_88xx(struct halmac_adapter *adapter, u8 num);

enum halmac_ret_status
get_sdio_bulkout_id_88xx(struct halmac_adapter *adapter, u8 *buf, u32 size,
			 u8 *id);

enum halmac_ret_status
sdio_cmd53_4byte_88xx(struct halmac_adapter *adapter,
		      enum halmac_sdio_cmd53_4byte_mode mode);

enum halmac_ret_status
sdio_hw_info_88xx(struct halmac_adapter *adapter,
		  struct halmac_sdio_hw_info *info);

void
cfg_sdio_tx_page_threshold_88xx(struct halmac_adapter *adapter,
				struct halmac_tx_page_threshold_info *info);

enum halmac_ret_status
cnv_to_sdio_bus_offset_88xx(struct halmac_adapter *adapter, u32 *offset);

enum halmac_ret_status
leave_sdio_suspend_88xx(struct halmac_adapter *adapter);

u32
r_indir_sdio_88xx(struct halmac_adapter *adapter, u32 adr,
		  enum halmac_io_size size);

enum halmac_ret_status
w_indir_sdio_88xx(struct halmac_adapter *adapter, u32 adr, u32 val,
		  enum halmac_io_size size);

enum halmac_ret_status
en_ref_autok_sdio_88xx(struct halmac_adapter *adapter, u8 en);

#endif /* HALMAC_88XX_SUPPORT */

#endif/* _HALMAC_SDIO_88XX_H_ */

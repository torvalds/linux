/******************************************************************************
 *
 * Copyright(c) 2016  Realtek Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * The full GNU General Public License is included in this distribution in the
 * file called LICENSE.
 *
 * Contact Information:
 * wlanfae <wlanfae@realtek.com>
 * Realtek Corporation, No. 2, Innovation Road II, Hsinchu Science Park,
 * Hsinchu 300, Taiwan.
 *
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 *****************************************************************************/
#ifndef _HALMAC_API_88XX_SDIO_H_
#define _HALMAC_API_88XX_SDIO_H_

#include "../halmac_2_platform.h"
#include "../halmac_type.h"

enum halmac_ret_status
halmac_init_sdio_cfg_88xx(struct halmac_adapter *halmac_adapter);

enum halmac_ret_status
halmac_deinit_sdio_cfg_88xx(struct halmac_adapter *halmac_adapter);

enum halmac_ret_status
halmac_cfg_rx_aggregation_88xx_sdio(struct halmac_adapter *halmac_adapter,
				    struct halmac_rxagg_cfg *phalmac_rxagg_cfg);

u8 halmac_reg_read_8_sdio_88xx(struct halmac_adapter *halmac_adapter,
			       u32 halmac_offset);

enum halmac_ret_status
halmac_reg_write_8_sdio_88xx(struct halmac_adapter *halmac_adapter,
			     u32 halmac_offset, u8 halmac_data);

u16 halmac_reg_read_16_sdio_88xx(struct halmac_adapter *halmac_adapter,
				 u32 halmac_offset);

enum halmac_ret_status
halmac_reg_write_16_sdio_88xx(struct halmac_adapter *halmac_adapter,
			      u32 halmac_offset, u16 halmac_data);

u32 halmac_reg_read_32_sdio_88xx(struct halmac_adapter *halmac_adapter,
				 u32 halmac_offset);

enum halmac_ret_status
halmac_reg_write_32_sdio_88xx(struct halmac_adapter *halmac_adapter,
			      u32 halmac_offset, u32 halmac_data);

enum halmac_ret_status
halmac_get_sdio_tx_addr_88xx(struct halmac_adapter *halmac_adapter,
			     u8 *halmac_buf, u32 halmac_size, u32 *pcmd53_addr);

enum halmac_ret_status
halmac_cfg_tx_agg_align_sdio_88xx(struct halmac_adapter *halmac_adapter,
				  u8 enable, u16 align_size);

enum halmac_ret_status halmac_cfg_tx_agg_align_sdio_not_support_88xx(
	struct halmac_adapter *halmac_adapter, u8 enable, u16 align_size);

enum halmac_ret_status
halmac_tx_allowed_sdio_88xx(struct halmac_adapter *halmac_adapter,
			    u8 *halmac_buf, u32 halmac_size);

u32 halmac_reg_read_indirect_32_sdio_88xx(struct halmac_adapter *halmac_adapter,
					  u32 halmac_offset);

u8 halmac_reg_read_nbyte_sdio_88xx(struct halmac_adapter *halmac_adapter,
				   u32 halmac_offset, u32 halmac_size,
				   u8 *halmac_data);

#endif /* _HALMAC_API_88XX_SDIO_H_ */

/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2016  Realtek Corporation.
 *
 * Contact Information:
 * wlanfae <wlanfae@realtek.com>
 * Realtek Corporation, No. 2, Innovation Road II, Hsinchu Science Park,
 * Hsinchu 300, Taiwan.
 *
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 *****************************************************************************/
#ifndef _HALMAC_API_88XX_USB_H_
#define _HALMAC_API_88XX_USB_H_

#include "../halmac_2_platform.h"
#include "../halmac_type.h"

enum halmac_ret_status
halmac_init_usb_cfg_88xx(struct halmac_adapter *halmac_adapter);

enum halmac_ret_status
halmac_deinit_usb_cfg_88xx(struct halmac_adapter *halmac_adapter);

enum halmac_ret_status
halmac_cfg_rx_aggregation_88xx_usb(struct halmac_adapter *halmac_adapter,
				   struct halmac_rxagg_cfg *phalmac_rxagg_cfg);

u8 halmac_reg_read_8_usb_88xx(struct halmac_adapter *halmac_adapter,
			      u32 halmac_offset);

enum halmac_ret_status
halmac_reg_write_8_usb_88xx(struct halmac_adapter *halmac_adapter,
			    u32 halmac_offset, u8 halmac_data);

u16 halmac_reg_read_16_usb_88xx(struct halmac_adapter *halmac_adapter,
				u32 halmac_offset);

enum halmac_ret_status
halmac_reg_write_16_usb_88xx(struct halmac_adapter *halmac_adapter,
			     u32 halmac_offset, u16 halmac_data);

u32 halmac_reg_read_32_usb_88xx(struct halmac_adapter *halmac_adapter,
				u32 halmac_offset);

enum halmac_ret_status
halmac_reg_write_32_usb_88xx(struct halmac_adapter *halmac_adapter,
			     u32 halmac_offset, u32 halmac_data);

enum halmac_ret_status
halmac_set_bulkout_num_88xx(struct halmac_adapter *halmac_adapter,
			    u8 bulkout_num);

enum halmac_ret_status
halmac_get_usb_bulkout_id_88xx(struct halmac_adapter *halmac_adapter,
			       u8 *halmac_buf, u32 halmac_size, u8 *bulkout_id);

enum halmac_ret_status halmac_cfg_tx_agg_align_usb_not_support_88xx(
	struct halmac_adapter *halmac_adapter, u8 enable, u16 align_size);

#endif /* _HALMAC_API_88XX_USB_H_ */

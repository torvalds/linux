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
#ifndef _HALMAC_API_88XX_PCIE_H_
#define _HALMAC_API_88XX_PCIE_H_

#include "../halmac_2_platform.h"
#include "../halmac_type.h"

#define LINK_CTRL2_REG_OFFSET 0xA0
#define GEN2_CTRL_OFFSET 0x80C
#define LINK_STATUS_REG_OFFSET 0x82
#define GEN1_SPEED 0x01
#define GEN2_SPEED 0x02

enum halmac_ret_status
halmac_init_pcie_cfg_88xx(struct halmac_adapter *halmac_adapter);

enum halmac_ret_status
halmac_deinit_pcie_cfg_88xx(struct halmac_adapter *halmac_adapter);

enum halmac_ret_status
halmac_cfg_rx_aggregation_88xx_pcie(struct halmac_adapter *halmac_adapter,
				    struct halmac_rxagg_cfg *phalmac_rxagg_cfg);

u8 halmac_reg_read_8_pcie_88xx(struct halmac_adapter *halmac_adapter,
			       u32 halmac_offset);

enum halmac_ret_status
halmac_reg_write_8_pcie_88xx(struct halmac_adapter *halmac_adapter,
			     u32 halmac_offset, u8 halmac_data);

u16 halmac_reg_read_16_pcie_88xx(struct halmac_adapter *halmac_adapter,
				 u32 halmac_offset);

enum halmac_ret_status
halmac_reg_write_16_pcie_88xx(struct halmac_adapter *halmac_adapter,
			      u32 halmac_offset, u16 halmac_data);

u32 halmac_reg_read_32_pcie_88xx(struct halmac_adapter *halmac_adapter,
				 u32 halmac_offset);

enum halmac_ret_status
halmac_reg_write_32_pcie_88xx(struct halmac_adapter *halmac_adapter,
			      u32 halmac_offset, u32 halmac_data);

enum halmac_ret_status halmac_cfg_tx_agg_align_pcie_not_support_88xx(
	struct halmac_adapter *halmac_adapter, u8 enable, u16 align_size);

#endif /* _HALMAC_API_88XX_PCIE_H_ */

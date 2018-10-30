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
#ifndef _HALMAC_API_8822B_SDIO_H_
#define _HALMAC_API_8822B_SDIO_H_

#include "../../halmac_2_platform.h"
#include "../../halmac_type.h"

enum halmac_ret_status
halmac_mac_power_switch_8822b_sdio(struct halmac_adapter *halmac_adapter,
				   enum halmac_mac_power halmac_power);

enum halmac_ret_status
halmac_phy_cfg_8822b_sdio(struct halmac_adapter *halmac_adapter,
			  enum halmac_intf_phy_platform platform);

enum halmac_ret_status halmac_interface_integration_tuning_8822b_sdio(
	struct halmac_adapter *halmac_adapter);

#endif /* _HALMAC_API_8822B_SDIO_H_ */

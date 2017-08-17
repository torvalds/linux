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
#ifndef _HALMAC_API_8822B_PCIE_H_
#define _HALMAC_API_8822B_PCIE_H_

#include "../../halmac_2_platform.h"
#include "../../halmac_type.h"

extern struct halmac_intf_phy_para_ HALMAC_RTL8822B_PCIE_PHY_GEN1[];
extern struct halmac_intf_phy_para_ HALMAC_RTL8822B_PCIE_PHY_GEN2[];

enum halmac_ret_status
halmac_mac_power_switch_8822b_pcie(struct halmac_adapter *halmac_adapter,
				   enum halmac_mac_power halmac_power);

enum halmac_ret_status
halmac_pcie_switch_8822b(struct halmac_adapter *halmac_adapter,
			 enum halmac_pcie_cfg pcie_cfg);

enum halmac_ret_status
halmac_pcie_switch_8822b_nc(struct halmac_adapter *halmac_adapter,
			    enum halmac_pcie_cfg pcie_cfg);

enum halmac_ret_status
halmac_phy_cfg_8822b_pcie(struct halmac_adapter *halmac_adapter,
			  enum halmac_intf_phy_platform platform);

enum halmac_ret_status halmac_interface_integration_tuning_8822b_pcie(
	struct halmac_adapter *halmac_adapter);

#endif /* _HALMAC_API_8822B_PCIE_H_ */

/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (c) 2015-2017, The Linux Foundation. All rights reserved.
 *
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __EMAC_PHY_H__
#define __EMAC_PHY_H__

#include <linux/platform_device.h>
#include <linux/phy.h>

struct emac_adapter;

struct emac_phy_ops {
	int  (*config)(struct platform_device *pdev, struct emac_adapter *adpt);
	void (*reset)(struct emac_adapter *adpt);
	int  (*up)(struct emac_adapter *adpt);
	void (*down)(struct emac_adapter *adpt);
	int  (*link_setup_no_ephy)(struct emac_adapter *adpt);
	int  (*link_check_no_ephy)(struct emac_adapter *adpt,
				   struct phy_device *phydev);
	void (*tx_clk_set_rate)(struct emac_adapter *adpt);
	void (*periodic_task)(struct emac_adapter *adpt);
};

enum emac_flow_ctrl {
	EMAC_FC_NONE,
	EMAC_FC_RX_PAUSE,
	EMAC_FC_TX_PAUSE,
	EMAC_FC_FULL,
	EMAC_FC_DEFAULT
};

enum emac_phy_map_type {
	EMAC_PHY_MAP_DEFAULT = 0,
	EMAC_PHY_MAP_MDM9607,
	EMAC_PHY_MAP_V2,
	EMAC_PHY_MAP_NUM,
};

/*  emac_phy - internal emac phy
 * @addr mii address
 * @id vendor id
 * @cur_fc_mode flow control mode in effect
 * @req_fc_mode flow control mode requested by caller
 * @disable_fc_autoneg Do not auto-negotiate flow control
 */
struct emac_phy {
	phy_interface_t	phy_interface;
	u32				phy_version;
	bool				external;
	struct emac_phy_ops		ops;

	void				*private;

	/* flow control configuration */
	enum emac_flow_ctrl		cur_fc_mode;
	enum emac_flow_ctrl		req_fc_mode;
	bool				disable_fc_autoneg;
	enum emac_phy_map_type		board_id;

	int link_up;
	int link_speed;
	int link_duplex;
	int link_pause;

	bool	is_wol_irq_reg;
	bool	is_wol_enabled;
	spinlock_t	wol_irq_lock; /* lock for wol irq gpio enablement */
	bool	is_ext_phy_connect;
};

int emac_phy_config_internal(struct platform_device *pdev,
			     struct emac_adapter *adpt);
int emac_phy_config_external(struct platform_device *pdev,
			     struct emac_adapter *adpt);
int emac_phy_setup_link(struct emac_adapter *adpt, u32 speed, bool autoneg,
			bool fc);
int emac_phy_setup_link_speed(struct emac_adapter *adpt, u32 speed,
			      bool autoneg, bool fc);
int emac_phy_check_link(struct emac_adapter *adpt, u32 *speed, bool *link_up);
int emac_phy_get_lpa_speed(struct emac_adapter *adpt, u32 *speed);
int emac_phy_config_fc(struct emac_adapter *adpt);
void emac_phy_reset_external(struct emac_adapter *adpt);
#endif /* __EMAC_PHY_H__ */

/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __DRIVERS_NET_PHY_XPOWERS_ACX00_H
#define __DRIVERS_NET_PHY_XPOWERS_ACX00_H

#include <linux/err.h>
#include <linux/phy.h>

#define AC300_EPHY_CONTROL_ADDR_OFFSET	16

struct device_node;

struct acx00_ephy_control {
	int (*power_on)(struct acx00_ephy_control *control,
			unsigned int phy_addr);
	int (*power_off)(struct acx00_ephy_control *control);
	int (*set_interface)(struct acx00_ephy_control *control,
			     phy_interface_t interface);
};

struct acx00_ephy_control *
ac200_ephy_ctl_create(struct phy_device *phydev,
		      struct device_node *package_node,
		      bool has_calibration, u8 calibration);
struct acx00_ephy_control *
ac300_ephy_ctl_create(struct phy_device *phydev,
		      struct device_node *package_node, u8 calibration);

#endif

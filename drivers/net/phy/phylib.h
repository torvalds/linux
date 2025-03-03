/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * phylib header
 */

#ifndef __PHYLIB_H
#define __PHYLIB_H

struct device_node;
struct phy_device;

struct device_node *phy_package_get_node(struct phy_device *phydev);
void *phy_package_get_priv(struct phy_device *phydev);

#endif /* __PHYLIB_H */

/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * phylib-internal header
 */

#ifndef __PHYLIB_INTERNAL_H
#define __PHYLIB_INTERNAL_H

struct phy_device;

/*
 * phy_supported_speeds - return all speeds currently supported by a PHY device
 */
unsigned int phy_supported_speeds(struct phy_device *phy,
				  unsigned int *speeds,
				  unsigned int size);
void of_set_phy_supported(struct phy_device *phydev);
void of_set_phy_eee_broken(struct phy_device *phydev);
void of_set_phy_timing_role(struct phy_device *phydev);
int phy_speed_down_core(struct phy_device *phydev);
void phy_check_downshift(struct phy_device *phydev);

int phy_package_address(struct phy_device *phydev, unsigned int addr_offset);

int genphy_c45_read_eee_adv(struct phy_device *phydev, unsigned long *adv);

#endif /* __PHYLIB_INTERNAL_H */

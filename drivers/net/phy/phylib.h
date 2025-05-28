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
int __phy_package_read(struct phy_device *phydev, unsigned int addr_offset,
		       u32 regnum);
int __phy_package_write(struct phy_device *phydev, unsigned int addr_offset,
			u32 regnum, u16 val);
int __phy_package_read_mmd(struct phy_device *phydev,
			   unsigned int addr_offset, int devad,
			   u32 regnum);
int __phy_package_write_mmd(struct phy_device *phydev,
			    unsigned int addr_offset, int devad,
			    u32 regnum, u16 val);
bool phy_package_init_once(struct phy_device *phydev);
bool phy_package_probe_once(struct phy_device *phydev);
int phy_package_join(struct phy_device *phydev, int base_addr, size_t priv_size);
int of_phy_package_join(struct phy_device *phydev, size_t priv_size);
void phy_package_leave(struct phy_device *phydev);
int devm_phy_package_join(struct device *dev, struct phy_device *phydev,
			  int base_addr, size_t priv_size);
int devm_of_phy_package_join(struct device *dev, struct phy_device *phydev,
			     size_t priv_size);

#endif /* __PHYLIB_H */

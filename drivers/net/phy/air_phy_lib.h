/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (C) 2026 Airoha Technology Corp.
 * Copyright (C) 2026 Collabora Ltd.
 *                    Louis-Alexis Eyraud <louisalexis.eyraud@collabora.com>
 */

#ifndef __AIR_PHY_LIB_H
#define __AIR_PHY_LIB_H

#include <linux/phy.h>

#define AIR_EXT_PAGE_ACCESS		0x1f

#define AIR_PHY_PAGE_STANDARD		0x0000
#define AIR_PHY_PAGE_EXTENDED_1		0x0001
#define AIR_PHY_PAGE_EXTENDED_4		0x0004

/* MII Registers Page 4*/
#define AIR_BPBUS_MODE			0x10
#define   AIR_BPBUS_MODE_ADDR_FIXED		0x0000
#define   AIR_BPBUS_MODE_ADDR_INCR		BIT(15)
#define AIR_BPBUS_WR_ADDR_HIGH		0x11
#define AIR_BPBUS_WR_ADDR_LOW		0x12
#define AIR_BPBUS_WR_DATA_HIGH		0x13
#define AIR_BPBUS_WR_DATA_LOW		0x14
#define AIR_BPBUS_RD_ADDR_HIGH		0x15
#define AIR_BPBUS_RD_ADDR_LOW		0x16
#define AIR_BPBUS_RD_DATA_HIGH		0x17
#define AIR_BPBUS_RD_DATA_LOW		0x18

int air_phy_buckpbus_reg_modify(struct phy_device *phydev, u32 pbus_address,
				u32 mask, u32 set);
int air_phy_buckpbus_reg_read(struct phy_device *phydev, u32 pbus_address,
			      u32 *pbus_data);
int air_phy_buckpbus_reg_write(struct phy_device *phydev, u32 pbus_address,
			       u32 pbus_data);
int air_phy_read_page(struct phy_device *phydev);
int air_phy_write_page(struct phy_device *phydev, int page);

#endif /* __AIR_PHY_LIB_H */

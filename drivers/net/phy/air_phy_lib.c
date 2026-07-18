// SPDX-License-Identifier: GPL-2.0+
/*
 * Airoha Ethernet PHY common library
 *
 * Copyright (C) 2026 Airoha Technology Corp.
 * Copyright (C) 2026 Collabora Ltd.
 *                    Louis-Alexis Eyraud <louisalexis.eyraud@collabora.com>
 */

#include <linux/export.h>
#include <linux/module.h>
#include <linux/phy.h>
#include <linux/wordpart.h>

#include "air_phy_lib.h"

static int __air_buckpbus_reg_read(struct phy_device *phydev,
				   u32 pbus_address, u32 *pbus_data)
{
	int pbus_data_low, pbus_data_high;
	int ret;

	ret = __phy_write(phydev, AIR_BPBUS_MODE, AIR_BPBUS_MODE_ADDR_FIXED);
	if (ret < 0)
		return ret;

	ret = __phy_write(phydev, AIR_BPBUS_RD_ADDR_HIGH,
			  upper_16_bits(pbus_address));
	if (ret < 0)
		return ret;

	ret = __phy_write(phydev, AIR_BPBUS_RD_ADDR_LOW,
			  lower_16_bits(pbus_address));
	if (ret < 0)
		return ret;

	pbus_data_high = __phy_read(phydev, AIR_BPBUS_RD_DATA_HIGH);
	if (pbus_data_high < 0)
		return pbus_data_high;

	pbus_data_low = __phy_read(phydev, AIR_BPBUS_RD_DATA_LOW);
	if (pbus_data_low < 0)
		return pbus_data_low;

	*pbus_data = pbus_data_low | (pbus_data_high << 16);
	return 0;
}

static int __air_buckpbus_reg_write(struct phy_device *phydev,
				    u32 pbus_address, u32 pbus_data)
{
	int ret;

	ret = __phy_write(phydev, AIR_BPBUS_MODE, AIR_BPBUS_MODE_ADDR_FIXED);
	if (ret < 0)
		return ret;

	ret = __phy_write(phydev, AIR_BPBUS_WR_ADDR_HIGH,
			  upper_16_bits(pbus_address));
	if (ret < 0)
		return ret;

	ret = __phy_write(phydev, AIR_BPBUS_WR_ADDR_LOW,
			  lower_16_bits(pbus_address));
	if (ret < 0)
		return ret;

	ret = __phy_write(phydev, AIR_BPBUS_WR_DATA_HIGH,
			  upper_16_bits(pbus_data));
	if (ret < 0)
		return ret;

	ret = __phy_write(phydev, AIR_BPBUS_WR_DATA_LOW,
			  lower_16_bits(pbus_data));
	if (ret < 0)
		return ret;

	return 0;
}

static int __air_buckpbus_reg_modify(struct phy_device *phydev,
				     u32 pbus_address, u32 mask, u32 set)
{
	int pbus_data_low, pbus_data_high;
	u32 pbus_data_old, pbus_data_new;
	int ret;

	ret = __phy_write(phydev, AIR_BPBUS_MODE, AIR_BPBUS_MODE_ADDR_FIXED);
	if (ret < 0)
		return ret;

	ret = __phy_write(phydev, AIR_BPBUS_RD_ADDR_HIGH,
			  upper_16_bits(pbus_address));
	if (ret < 0)
		return ret;

	ret = __phy_write(phydev, AIR_BPBUS_RD_ADDR_LOW,
			  lower_16_bits(pbus_address));
	if (ret < 0)
		return ret;

	pbus_data_high = __phy_read(phydev, AIR_BPBUS_RD_DATA_HIGH);
	if (pbus_data_high < 0)
		return pbus_data_high;

	pbus_data_low = __phy_read(phydev, AIR_BPBUS_RD_DATA_LOW);
	if (pbus_data_low < 0)
		return pbus_data_low;

	pbus_data_old = pbus_data_low | (pbus_data_high << 16);
	pbus_data_new = (pbus_data_old & ~mask) | set;
	if (pbus_data_new == pbus_data_old)
		return 0;

	ret = __phy_write(phydev, AIR_BPBUS_WR_ADDR_HIGH,
			  upper_16_bits(pbus_address));
	if (ret < 0)
		return ret;

	ret = __phy_write(phydev, AIR_BPBUS_WR_ADDR_LOW,
			  lower_16_bits(pbus_address));
	if (ret < 0)
		return ret;

	ret = __phy_write(phydev, AIR_BPBUS_WR_DATA_HIGH,
			  upper_16_bits(pbus_data_new));
	if (ret < 0)
		return ret;

	ret = __phy_write(phydev, AIR_BPBUS_WR_DATA_LOW,
			  lower_16_bits(pbus_data_new));
	if (ret < 0)
		return ret;

	return 0;
}

int air_phy_buckpbus_reg_read(struct phy_device *phydev, u32 pbus_address,
			      u32 *pbus_data)
{
	int saved_page;
	int ret = 0;

	saved_page = phy_select_page(phydev, AIR_PHY_PAGE_EXTENDED_4);

	if (saved_page >= 0) {
		ret = __air_buckpbus_reg_read(phydev, pbus_address, pbus_data);
		if (ret < 0)
			phydev_err(phydev, "%s 0x%08x failed: %d\n", __func__,
				   pbus_address, ret);
	}

	return phy_restore_page(phydev, saved_page, ret);
}
EXPORT_SYMBOL_GPL(air_phy_buckpbus_reg_read);

int air_phy_buckpbus_reg_write(struct phy_device *phydev, u32 pbus_address,
			       u32 pbus_data)
{
	int saved_page;
	int ret = 0;

	saved_page = phy_select_page(phydev, AIR_PHY_PAGE_EXTENDED_4);

	if (saved_page >= 0) {
		ret = __air_buckpbus_reg_write(phydev, pbus_address,
					       pbus_data);
		if (ret < 0)
			phydev_err(phydev, "%s 0x%08x failed: %d\n", __func__,
				   pbus_address, ret);
	}

	return phy_restore_page(phydev, saved_page, ret);
}
EXPORT_SYMBOL_GPL(air_phy_buckpbus_reg_write);

int air_phy_buckpbus_reg_modify(struct phy_device *phydev, u32 pbus_address,
				u32 mask, u32 set)
{
	int saved_page;
	int ret = 0;

	saved_page = phy_select_page(phydev, AIR_PHY_PAGE_EXTENDED_4);

	if (saved_page >= 0) {
		ret = __air_buckpbus_reg_modify(phydev, pbus_address, mask,
						set);
		if (ret < 0)
			phydev_err(phydev, "%s 0x%08x failed: %d\n", __func__,
				   pbus_address, ret);
	}

	return phy_restore_page(phydev, saved_page, ret);
}
EXPORT_SYMBOL_GPL(air_phy_buckpbus_reg_modify);

int air_phy_read_page(struct phy_device *phydev)
{
	return __phy_read(phydev, AIR_EXT_PAGE_ACCESS);
}
EXPORT_SYMBOL_GPL(air_phy_read_page);

int air_phy_write_page(struct phy_device *phydev, int page)
{
	return __phy_write(phydev, AIR_EXT_PAGE_ACCESS, page);
}
EXPORT_SYMBOL_GPL(air_phy_write_page);

MODULE_DESCRIPTION("Airoha PHY Library");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Louis-Alexis Eyraud");

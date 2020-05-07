/******************************************************************************
 *
 * Copyright(c) 2016 - 2019 Realtek Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 ******************************************************************************/

#include "../../halmac_type.h"
#if HALMAC_USB_SUPPORT
#include "halmac_usb_8821c.h"
#endif
#if HALMAC_PCIE_SUPPORT
#include "halmac_pcie_8821c.h"
#endif

/**
 * ============ip sel item list============
 * HALMAC_IP_INTF_PHY
 *	USB2 : usb2 phy, 1byte value
 *	USB3 : usb3 phy, 2byte value
 *	PCIE1 : pcie gen1 mdio, 2byte value
 *	PCIE2 : pcie gen2 mdio, 2byte value
 * HALMAC_IP_SEL_MAC
 *	USB2, USB3, PCIE1, PCIE2 : mac ip, 1byte value
 * HALMAC_IP_PCIE_DBI
 *	USB2 USB3 : none
 *	PCIE1, PCIE2 : pcie dbi, 1byte value
 */

#if HALMAC_8821C_SUPPORT

struct halmac_intf_phy_para usb2_phy_param_8821c[] = {
	/* {offset, value, ip sel, cut mask, platform mask} */
	{0xFFFF, 0x00,
	 HALMAC_IP_INTF_PHY,
	 HALMAC_INTF_PHY_CUT_ALL,
	 HALMAC_INTF_PHY_PLATFORM_FOR_ALL},
};

struct halmac_intf_phy_para usb3_phy_param_8821c[] = {
	/* {offset, value, cut mask, platform mask} */
	{0xFFFF, 0x0000,
	 HALMAC_IP_INTF_PHY,
	 HALMAC_INTF_PHY_CUT_ALL,
	 HALMAC_INTF_PHY_PLATFORM_FOR_ALL},
};

struct halmac_intf_phy_para pcie_gen1_phy_param_8821c[] = {
	/* {offset, value, ip sel, cut mask, platform mask} */
	{0x0009, 0x6380,
	 HALMAC_IP_INTF_PHY,
	 HALMAC_INTF_PHY_CUT_ALL,
	 HALMAC_INTF_PHY_PLATFORM_FOR_ALL},
	{0xFFFF, 0x0000,
	 HALMAC_IP_INTF_PHY,
	 HALMAC_INTF_PHY_CUT_ALL,
	 HALMAC_INTF_PHY_PLATFORM_FOR_ALL},
};

struct halmac_intf_phy_para pcie_gen2_phy_param_8821c[] = {
	/* {offset, value, ip sel, cut mask, platform mask} */
	{0xFFFF, 0x0000,
	 HALMAC_IP_INTF_PHY,
	 HALMAC_INTF_PHY_CUT_ALL,
	 HALMAC_INTF_PHY_PLATFORM_FOR_ALL},
};

#endif /* HALMAC_8821C_SUPPORT */

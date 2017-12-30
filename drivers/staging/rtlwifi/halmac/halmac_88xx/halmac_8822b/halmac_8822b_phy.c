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
#include "../halmac_88xx_cfg.h"
#include "halmac_8822b_cfg.h"

/**
 * ============ip sel item list============
 * HALMAC_IP_SEL_INTF_PHY
 *	USB2 : usb2 phy, 1byte value
 *	USB3 : usb3 phy, 2byte value
 *	PCIE1 : pcie gen1 mdio, 2byte value
 *	PCIE2 : pcie gen2 mdio, 2byte value
 * HALMAC_IP_SEL_MAC
 *	USB2, USB3, PCIE1, PCIE2 : mac ip, 1byte value
 * HALMAC_IP_SEL_PCIE_DBI
 *	USB2 USB3 : none
 *	PCIE1, PCIE2 : pcie dbi, 1byte value
 */

struct halmac_intf_phy_para_ HALMAC_RTL8822B_USB2_PHY[] = {
	/* {offset, value, ip sel, cut mask, platform mask} */
	{0xFFFF, 0x00, HALMAC_IP_SEL_INTF_PHY, HALMAC_INTF_PHY_CUT_ALL,
	 HALMAC_INTF_PHY_PLATFORM_ALL},
};

struct halmac_intf_phy_para_ HALMAC_RTL8822B_USB3_PHY[] = {
	/* {offset, value, ip sel, cut mask, platform mask} */
	{0x0001, 0xA841, HALMAC_IP_SEL_INTF_PHY, HALMAC_INTF_PHY_CUT_D,
	 HALMAC_INTF_PHY_PLATFORM_ALL},
	{0xFFFF, 0x0000, HALMAC_IP_SEL_INTF_PHY, HALMAC_INTF_PHY_CUT_ALL,
	 HALMAC_INTF_PHY_PLATFORM_ALL},
};

struct halmac_intf_phy_para_ HALMAC_RTL8822B_PCIE_PHY_GEN1[] = {
	/* {offset, value, ip sel, cut mask, platform mask} */
	{0x0001, 0xA841, HALMAC_IP_SEL_INTF_PHY, HALMAC_INTF_PHY_CUT_C,
	 HALMAC_INTF_PHY_PLATFORM_ALL},
	{0x0002, 0x60C6, HALMAC_IP_SEL_INTF_PHY, HALMAC_INTF_PHY_CUT_C,
	 HALMAC_INTF_PHY_PLATFORM_ALL},
	{0x0008, 0x3596, HALMAC_IP_SEL_INTF_PHY, HALMAC_INTF_PHY_CUT_C,
	 HALMAC_INTF_PHY_PLATFORM_ALL},
	{0x0009, 0x321C, HALMAC_IP_SEL_INTF_PHY, HALMAC_INTF_PHY_CUT_C,
	 HALMAC_INTF_PHY_PLATFORM_ALL},
	{0x000A, 0x9623, HALMAC_IP_SEL_INTF_PHY, HALMAC_INTF_PHY_CUT_C,
	 HALMAC_INTF_PHY_PLATFORM_ALL},
	{0x0020, 0x94FF, HALMAC_IP_SEL_INTF_PHY, HALMAC_INTF_PHY_CUT_C,
	 HALMAC_INTF_PHY_PLATFORM_ALL},
	{0x0021, 0xFFCF, HALMAC_IP_SEL_INTF_PHY, HALMAC_INTF_PHY_CUT_C,
	 HALMAC_INTF_PHY_PLATFORM_ALL},
	{0x0026, 0xC006, HALMAC_IP_SEL_INTF_PHY, HALMAC_INTF_PHY_CUT_C,
	 HALMAC_INTF_PHY_PLATFORM_ALL},
	{0x0029, 0xFF0E, HALMAC_IP_SEL_INTF_PHY, HALMAC_INTF_PHY_CUT_C,
	 HALMAC_INTF_PHY_PLATFORM_ALL},
	{0x002A, 0x1840, HALMAC_IP_SEL_INTF_PHY, HALMAC_INTF_PHY_CUT_C,
	 HALMAC_INTF_PHY_PLATFORM_ALL},
	{0xFFFF, 0x0000, HALMAC_IP_SEL_INTF_PHY, HALMAC_INTF_PHY_CUT_ALL,
	 HALMAC_INTF_PHY_PLATFORM_ALL},
};

struct halmac_intf_phy_para_ HALMAC_RTL8822B_PCIE_PHY_GEN2[] = {
	/* {offset, value, ip sel, cut mask, platform mask} */
	{0x0001, 0xA841, HALMAC_IP_SEL_INTF_PHY, HALMAC_INTF_PHY_CUT_C,
	 HALMAC_INTF_PHY_PLATFORM_ALL},
	{0x0002, 0x60C6, HALMAC_IP_SEL_INTF_PHY, HALMAC_INTF_PHY_CUT_C,
	 HALMAC_INTF_PHY_PLATFORM_ALL},
	{0x0008, 0x3597, HALMAC_IP_SEL_INTF_PHY, HALMAC_INTF_PHY_CUT_C,
	 HALMAC_INTF_PHY_PLATFORM_ALL},
	{0x0009, 0x321C, HALMAC_IP_SEL_INTF_PHY, HALMAC_INTF_PHY_CUT_C,
	 HALMAC_INTF_PHY_PLATFORM_ALL},
	{0x000A, 0x9623, HALMAC_IP_SEL_INTF_PHY, HALMAC_INTF_PHY_CUT_C,
	 HALMAC_INTF_PHY_PLATFORM_ALL},
	{0x0020, 0x94FF, HALMAC_IP_SEL_INTF_PHY, HALMAC_INTF_PHY_CUT_C,
	 HALMAC_INTF_PHY_PLATFORM_ALL},
	{0x0021, 0xFFCF, HALMAC_IP_SEL_INTF_PHY, HALMAC_INTF_PHY_CUT_C,
	 HALMAC_INTF_PHY_PLATFORM_ALL},
	{0x0026, 0xC006, HALMAC_IP_SEL_INTF_PHY, HALMAC_INTF_PHY_CUT_C,
	 HALMAC_INTF_PHY_PLATFORM_ALL},
	{0x0029, 0xFF0E, HALMAC_IP_SEL_INTF_PHY, HALMAC_INTF_PHY_CUT_C,
	 HALMAC_INTF_PHY_PLATFORM_ALL},
	{0x002A, 0x3040, HALMAC_IP_SEL_INTF_PHY, HALMAC_INTF_PHY_CUT_C,
	 HALMAC_INTF_PHY_PLATFORM_ALL},
	{0xFFFF, 0x0000, HALMAC_IP_SEL_INTF_PHY, HALMAC_INTF_PHY_CUT_ALL,
	 HALMAC_INTF_PHY_PLATFORM_ALL},
};

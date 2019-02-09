/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2016  Realtek Corporation.
 *
 * Contact Information:
 * wlanfae <wlanfae@realtek.com>
 * Realtek Corporation, No. 2, Innovation Road II, Hsinchu Science Park,
 * Hsinchu 300, Taiwan.
 *
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 *****************************************************************************/
#ifndef HALMAC_INTF_PHY_CMD
#define HALMAC_INTF_PHY_CMD

/* Cut mask */
enum halmac_intf_phy_cut {
	HALMAC_INTF_PHY_CUT_TESTCHIP = BIT(0),
	HALMAC_INTF_PHY_CUT_A = BIT(1),
	HALMAC_INTF_PHY_CUT_B = BIT(2),
	HALMAC_INTF_PHY_CUT_C = BIT(3),
	HALMAC_INTF_PHY_CUT_D = BIT(4),
	HALMAC_INTF_PHY_CUT_E = BIT(5),
	HALMAC_INTF_PHY_CUT_F = BIT(6),
	HALMAC_INTF_PHY_CUT_G = BIT(7),
	HALMAC_INTF_PHY_CUT_ALL = 0x7FFF,
};

/* IP selection */
enum halmac_ip_sel {
	HALMAC_IP_SEL_INTF_PHY = 0,
	HALMAC_IP_SEL_MAC = 1,
	HALMAC_IP_SEL_PCIE_DBI = 2,
	HALMAC_IP_SEL_UNDEFINE = 0x7FFF,
};

/* Platform mask */
enum halmac_intf_phy_platform {
	HALMAC_INTF_PHY_PLATFORM_ALL = 0x7FFF,
};

#endif

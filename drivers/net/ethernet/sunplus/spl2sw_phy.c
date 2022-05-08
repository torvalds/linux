// SPDX-License-Identifier: GPL-2.0
/* Copyright Sunplus Technology Co., Ltd.
 *       All rights reserved.
 */

#include <linux/netdevice.h>
#include <linux/bitfield.h>
#include <linux/of_mdio.h>

#include "spl2sw_register.h"
#include "spl2sw_define.h"
#include "spl2sw_phy.h"

static void spl2sw_mii_link_change(struct net_device *ndev)
{
	struct spl2sw_mac *mac = netdev_priv(ndev);
	struct phy_device *phydev = ndev->phydev;
	struct spl2sw_common *comm = mac->comm;
	u32 reg;

	reg = readl(comm->l2sw_reg_base + L2SW_MAC_FORCE_MODE);

	if (phydev->link) {
		reg |= FIELD_PREP(MAC_FORCE_RMII_LINK, mac->lan_port);

		if (phydev->speed == 100) {
			reg |= FIELD_PREP(MAC_FORCE_RMII_SPD, mac->lan_port);
		} else {
			reg &= FIELD_PREP(MAC_FORCE_RMII_SPD, ~mac->lan_port) |
			       ~MAC_FORCE_RMII_SPD;
		}

		if (phydev->duplex) {
			reg |= FIELD_PREP(MAC_FORCE_RMII_DPX, mac->lan_port);
		} else {
			reg &= FIELD_PREP(MAC_FORCE_RMII_DPX, ~mac->lan_port) |
			       ~MAC_FORCE_RMII_DPX;
		}

		if (phydev->pause) {
			reg |= FIELD_PREP(MAC_FORCE_RMII_FC, mac->lan_port);
		} else {
			reg &= FIELD_PREP(MAC_FORCE_RMII_FC, ~mac->lan_port) |
			       ~MAC_FORCE_RMII_FC;
		}
	} else {
		reg &= FIELD_PREP(MAC_FORCE_RMII_LINK, ~mac->lan_port) |
		       ~MAC_FORCE_RMII_LINK;
	}

	writel(reg, comm->l2sw_reg_base + L2SW_MAC_FORCE_MODE);

	phy_print_status(phydev);
}

int spl2sw_phy_connect(struct spl2sw_common *comm)
{
	struct phy_device *phydev;
	struct net_device *ndev;
	struct spl2sw_mac *mac;
	int i;

	for (i = 0; i < MAX_NETDEV_NUM; i++)
		if (comm->ndev[i]) {
			ndev = comm->ndev[i];
			mac = netdev_priv(ndev);
			phydev = of_phy_connect(ndev, mac->phy_node, spl2sw_mii_link_change,
						0, mac->phy_mode);
			if (!phydev)
				return -ENODEV;

			phy_support_asym_pause(phydev);
			phy_attached_info(phydev);
		}

	return 0;
}

void spl2sw_phy_remove(struct spl2sw_common *comm)
{
	struct net_device *ndev;
	int i;

	for (i = 0; i < MAX_NETDEV_NUM; i++)
		if (comm->ndev[i]) {
			ndev = comm->ndev[i];
			if (ndev) {
				phy_disconnect(ndev->phydev);
				ndev->phydev = NULL;
			}
		}
}

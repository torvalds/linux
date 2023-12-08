// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2010 ASIX Electronics Corporation
 * Copyright (c) 2020 Samsung Electronics Co., Ltd.
 *
 * ASIX AX88796C SPI Fast Ethernet Linux driver
 */

#define pr_fmt(fmt)	"ax88796c: " fmt

#include <linux/bitmap.h>
#include <linux/iopoll.h>
#include <linux/phy.h>
#include <linux/netdevice.h>

#include "ax88796c_main.h"
#include "ax88796c_ioctl.h"

static const char ax88796c_priv_flag_names[][ETH_GSTRING_LEN] = {
	"SPICompression",
};

static void
ax88796c_get_drvinfo(struct net_device *ndev, struct ethtool_drvinfo *info)
{
	/* Inherit standard device info */
	strncpy(info->driver, DRV_NAME, sizeof(info->driver));
}

static u32 ax88796c_get_msglevel(struct net_device *ndev)
{
	struct ax88796c_device *ax_local = to_ax88796c_device(ndev);

	return ax_local->msg_enable;
}

static void ax88796c_set_msglevel(struct net_device *ndev, u32 level)
{
	struct ax88796c_device *ax_local = to_ax88796c_device(ndev);

	ax_local->msg_enable = level;
}

static void
ax88796c_get_pauseparam(struct net_device *ndev, struct ethtool_pauseparam *pause)
{
	struct ax88796c_device *ax_local = to_ax88796c_device(ndev);

	pause->tx_pause = !!(ax_local->flowctrl & AX_FC_TX);
	pause->rx_pause = !!(ax_local->flowctrl & AX_FC_RX);
	pause->autoneg = (ax_local->flowctrl & AX_FC_ANEG) ?
		AUTONEG_ENABLE :
		AUTONEG_DISABLE;
}

static int
ax88796c_set_pauseparam(struct net_device *ndev, struct ethtool_pauseparam *pause)
{
	struct ax88796c_device *ax_local = to_ax88796c_device(ndev);
	int fc;

	/* The following logic comes from phylink_ethtool_set_pauseparam() */
	fc = pause->tx_pause ? AX_FC_TX : 0;
	fc |= pause->rx_pause ? AX_FC_RX : 0;
	fc |= pause->autoneg ? AX_FC_ANEG : 0;

	ax_local->flowctrl = fc;

	if (pause->autoneg) {
		phy_set_asym_pause(ax_local->phydev, pause->tx_pause,
				   pause->rx_pause);
	} else {
		int maccr = 0;

		phy_set_asym_pause(ax_local->phydev, 0, 0);
		maccr |= (ax_local->flowctrl & AX_FC_RX) ? MACCR_RXFC_ENABLE : 0;
		maccr |= (ax_local->flowctrl & AX_FC_TX) ? MACCR_TXFC_ENABLE : 0;

		mutex_lock(&ax_local->spi_lock);

		maccr |= AX_READ(&ax_local->ax_spi, P0_MACCR) &
			~(MACCR_TXFC_ENABLE | MACCR_RXFC_ENABLE);
		AX_WRITE(&ax_local->ax_spi, maccr, P0_MACCR);

		mutex_unlock(&ax_local->spi_lock);
	}

	return 0;
}

static int ax88796c_get_regs_len(struct net_device *ndev)
{
	return AX88796C_REGDUMP_LEN + AX88796C_PHY_REGDUMP_LEN;
}

static void
ax88796c_get_regs(struct net_device *ndev, struct ethtool_regs *regs, void *_p)
{
	struct ax88796c_device *ax_local = to_ax88796c_device(ndev);
	int offset, i;
	u16 *p = _p;

	memset(p, 0, ax88796c_get_regs_len(ndev));

	mutex_lock(&ax_local->spi_lock);

	for (offset = 0; offset < AX88796C_REGDUMP_LEN; offset += 2) {
		if (!test_bit(offset / 2, ax88796c_no_regs_mask))
			*p = AX_READ(&ax_local->ax_spi, offset);
		p++;
	}

	mutex_unlock(&ax_local->spi_lock);

	for (i = 0; i < AX88796C_PHY_REGDUMP_LEN / 2; i++) {
		*p = phy_read(ax_local->phydev, i);
		p++;
	}
}

static void
ax88796c_get_strings(struct net_device *ndev, u32 stringset, u8 *data)
{
	switch (stringset) {
	case ETH_SS_PRIV_FLAGS:
		memcpy(data, ax88796c_priv_flag_names,
		       sizeof(ax88796c_priv_flag_names));
		break;
	}
}

static int
ax88796c_get_sset_count(struct net_device *ndev, int stringset)
{
	int ret = 0;

	switch (stringset) {
	case ETH_SS_PRIV_FLAGS:
		ret = ARRAY_SIZE(ax88796c_priv_flag_names);
		break;
	default:
		ret = -EOPNOTSUPP;
	}

	return ret;
}

static int ax88796c_set_priv_flags(struct net_device *ndev, u32 flags)
{
	struct ax88796c_device *ax_local = to_ax88796c_device(ndev);

	if (flags & ~AX_PRIV_FLAGS_MASK)
		return -EOPNOTSUPP;

	if ((ax_local->priv_flags ^ flags) & AX_CAP_COMP)
		if (netif_running(ndev))
			return -EBUSY;

	ax_local->priv_flags = flags;

	return 0;
}

static u32 ax88796c_get_priv_flags(struct net_device *ndev)
{
	struct ax88796c_device *ax_local = to_ax88796c_device(ndev);

	return ax_local->priv_flags;
}

int ax88796c_mdio_read(struct mii_bus *mdiobus, int phy_id, int loc)
{
	struct ax88796c_device *ax_local = mdiobus->priv;
	int ret;

	mutex_lock(&ax_local->spi_lock);
	AX_WRITE(&ax_local->ax_spi, MDIOCR_RADDR(loc)
			| MDIOCR_FADDR(phy_id) | MDIOCR_READ, P2_MDIOCR);

	ret = read_poll_timeout(AX_READ, ret,
				(ret != 0),
				0, jiffies_to_usecs(HZ / 100), false,
				&ax_local->ax_spi, P2_MDIOCR);
	if (!ret)
		ret = AX_READ(&ax_local->ax_spi, P2_MDIODR);

	mutex_unlock(&ax_local->spi_lock);

	return ret;
}

int
ax88796c_mdio_write(struct mii_bus *mdiobus, int phy_id, int loc, u16 val)
{
	struct ax88796c_device *ax_local = mdiobus->priv;
	int ret;

	mutex_lock(&ax_local->spi_lock);
	AX_WRITE(&ax_local->ax_spi, val, P2_MDIODR);

	AX_WRITE(&ax_local->ax_spi,
		 MDIOCR_RADDR(loc) | MDIOCR_FADDR(phy_id)
		 | MDIOCR_WRITE, P2_MDIOCR);

	ret = read_poll_timeout(AX_READ, ret,
				((ret & MDIOCR_VALID) != 0), 0,
				jiffies_to_usecs(HZ / 100), false,
				&ax_local->ax_spi, P2_MDIOCR);
	mutex_unlock(&ax_local->spi_lock);

	return ret;
}

const struct ethtool_ops ax88796c_ethtool_ops = {
	.get_drvinfo		= ax88796c_get_drvinfo,
	.get_link		= ethtool_op_get_link,
	.get_msglevel		= ax88796c_get_msglevel,
	.set_msglevel		= ax88796c_set_msglevel,
	.get_link_ksettings	= phy_ethtool_get_link_ksettings,
	.set_link_ksettings	= phy_ethtool_set_link_ksettings,
	.nway_reset		= phy_ethtool_nway_reset,
	.get_pauseparam		= ax88796c_get_pauseparam,
	.set_pauseparam		= ax88796c_set_pauseparam,
	.get_regs_len		= ax88796c_get_regs_len,
	.get_regs		= ax88796c_get_regs,
	.get_strings		= ax88796c_get_strings,
	.get_sset_count		= ax88796c_get_sset_count,
	.get_priv_flags		= ax88796c_get_priv_flags,
	.set_priv_flags		= ax88796c_set_priv_flags,
};

int ax88796c_ioctl(struct net_device *ndev, struct ifreq *ifr, int cmd)
{
	int ret;

	ret = phy_mii_ioctl(ndev->phydev, ifr, cmd);

	return ret;
}

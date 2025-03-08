// SPDX-License-Identifier: GPL-2.0-only
/*
 * Broadcom GENET (Gigabit Ethernet) Wake-on-LAN support
 *
 * Copyright (c) 2014-2025 Broadcom
 */

#define pr_fmt(fmt)				"bcmgenet_wol: " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/clk.h>
#include <linux/platform_device.h>
#include <net/arp.h>

#include <linux/mii.h>
#include <linux/ethtool.h>
#include <linux/netdevice.h>
#include <linux/inetdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/in.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/phy.h>

#include "bcmgenet.h"

/* ethtool function - get WOL (Wake on LAN) settings, Only Magic Packet
 * Detection is supported through ethtool
 */
void bcmgenet_get_wol(struct net_device *dev, struct ethtool_wolinfo *wol)
{
	struct bcmgenet_priv *priv = netdev_priv(dev);
	struct device *kdev = &priv->pdev->dev;
	u32 phy_wolopts = 0;

	if (dev->phydev) {
		phy_ethtool_get_wol(dev->phydev, wol);
		phy_wolopts = wol->wolopts;
	}

	/* MAC is not wake-up capable, return what the PHY does */
	if (!device_can_wakeup(kdev))
		return;

	/* Overlay MAC capabilities with that of the PHY queried before */
	wol->supported |= WAKE_MAGIC | WAKE_MAGICSECURE | WAKE_FILTER;
	wol->wolopts |= priv->wolopts;

	/* Return the PHY configured magic password */
	if (phy_wolopts & WAKE_MAGICSECURE)
		return;

	/* Otherwise the MAC one */
	memset(wol->sopass, 0, sizeof(wol->sopass));
	if (wol->wolopts & WAKE_MAGICSECURE)
		memcpy(wol->sopass, priv->sopass, sizeof(priv->sopass));
}

/* ethtool function - set WOL (Wake on LAN) settings.
 * Only for magic packet detection mode.
 */
int bcmgenet_set_wol(struct net_device *dev, struct ethtool_wolinfo *wol)
{
	struct bcmgenet_priv *priv = netdev_priv(dev);
	struct device *kdev = &priv->pdev->dev;
	int ret;

	/* Try Wake-on-LAN from the PHY first */
	if (dev->phydev) {
		ret = phy_ethtool_set_wol(dev->phydev, wol);
		if (ret != -EOPNOTSUPP && wol->wolopts)
			return ret;
	}

	if (!device_can_wakeup(kdev))
		return -ENOTSUPP;

	if (wol->wolopts & ~(WAKE_MAGIC | WAKE_MAGICSECURE | WAKE_FILTER))
		return -EINVAL;

	if (wol->wolopts & WAKE_MAGICSECURE)
		memcpy(priv->sopass, wol->sopass, sizeof(priv->sopass));

	/* Flag the device and relevant IRQ as wakeup capable */
	if (wol->wolopts) {
		device_set_wakeup_enable(kdev, 1);
		/* Avoid unbalanced enable_irq_wake calls */
		if (priv->wol_irq_disabled) {
			enable_irq_wake(priv->wol_irq);
			enable_irq_wake(priv->irq0);
		}
		priv->wol_irq_disabled = false;
	} else {
		device_set_wakeup_enable(kdev, 0);
		/* Avoid unbalanced disable_irq_wake calls */
		if (!priv->wol_irq_disabled) {
			disable_irq_wake(priv->wol_irq);
			disable_irq_wake(priv->irq0);
		}
		priv->wol_irq_disabled = true;
	}

	priv->wolopts = wol->wolopts;

	return 0;
}

static int bcmgenet_poll_wol_status(struct bcmgenet_priv *priv)
{
	struct net_device *dev = priv->dev;
	int retries = 0;

	while (!(bcmgenet_rbuf_readl(priv, RBUF_STATUS)
		& RBUF_STATUS_WOL)) {
		retries++;
		if (retries > 5) {
			netdev_crit(dev, "polling wol mode timeout\n");
			return -ETIMEDOUT;
		}
		mdelay(1);
	}

	return retries;
}

static void bcmgenet_set_mpd_password(struct bcmgenet_priv *priv)
{
	bcmgenet_umac_writel(priv, get_unaligned_be16(&priv->sopass[0]),
			     UMAC_MPD_PW_MS);
	bcmgenet_umac_writel(priv, get_unaligned_be32(&priv->sopass[2]),
			     UMAC_MPD_PW_LS);
}

int bcmgenet_wol_power_down_cfg(struct bcmgenet_priv *priv,
				enum bcmgenet_power_mode mode)
{
	struct net_device *dev = priv->dev;
	u32 reg, hfb_ctrl_reg;
	int retries = 0;

	if (mode != GENET_POWER_WOL_MAGIC) {
		netif_err(priv, wol, dev, "unsupported mode: %d\n", mode);
		return -EINVAL;
	}

	if (priv->wolopts & (WAKE_MAGIC | WAKE_MAGICSECURE)) {
		reg = bcmgenet_umac_readl(priv, UMAC_MPD_CTRL);
		reg |= MPD_EN;
		if (priv->wolopts & WAKE_MAGICSECURE) {
			bcmgenet_set_mpd_password(priv);
			reg |= MPD_PW_EN;
		}
		bcmgenet_umac_writel(priv, reg, UMAC_MPD_CTRL);
	}

	hfb_ctrl_reg = bcmgenet_hfb_reg_readl(priv, HFB_CTRL);
	reg = hfb_ctrl_reg | RBUF_ACPI_EN;
	bcmgenet_hfb_reg_writel(priv, reg, HFB_CTRL);

	/* Do not leave UniMAC in MPD mode only */
	retries = bcmgenet_poll_wol_status(priv);
	if (retries < 0) {
		reg = bcmgenet_umac_readl(priv, UMAC_MPD_CTRL);
		reg &= ~(MPD_EN | MPD_PW_EN);
		bcmgenet_umac_writel(priv, reg, UMAC_MPD_CTRL);
		bcmgenet_hfb_reg_writel(priv, hfb_ctrl_reg, HFB_CTRL);
		return retries;
	}

	netif_dbg(priv, wol, dev, "MPD WOL-ready status set after %d msec\n",
		  retries);

	/* Disable phy status updates while suspending */
	mutex_lock(&dev->phydev->lock);
	dev->phydev->state = PHY_READY;
	mutex_unlock(&dev->phydev->lock);

	clk_prepare_enable(priv->clk_wol);

	/* Enable CRC forward */
	spin_lock_bh(&priv->reg_lock);
	reg = bcmgenet_umac_readl(priv, UMAC_CMD);
	priv->crc_fwd_en = 1;
	reg |= CMD_CRC_FWD;

	/* Can't suspend with WoL if MAC is still in reset */
	if (reg & CMD_SW_RESET)
		reg &= ~CMD_SW_RESET;

	/* Receiver must be enabled for WOL MP detection */
	reg |= CMD_RX_EN;
	bcmgenet_umac_writel(priv, reg, UMAC_CMD);
	spin_unlock_bh(&priv->reg_lock);

	reg = UMAC_IRQ_MPD_R;
	if (hfb_ctrl_reg & RBUF_HFB_EN)
		reg |=  UMAC_IRQ_HFB_SM | UMAC_IRQ_HFB_MM;

	bcmgenet_intrl2_0_writel(priv, reg, INTRL2_CPU_MASK_CLEAR);

	return 0;
}

int bcmgenet_wol_power_up_cfg(struct bcmgenet_priv *priv,
			      enum bcmgenet_power_mode mode)
{
	struct net_device *dev = priv->dev;
	u32 reg;

	if (mode != GENET_POWER_WOL_MAGIC) {
		netif_err(priv, wol, priv->dev, "invalid mode: %d\n", mode);
		return -EINVAL;
	}

	clk_disable_unprepare(priv->clk_wol);
	priv->crc_fwd_en = 0;

	bcmgenet_intrl2_0_writel(priv, UMAC_IRQ_WAKE_EVENT,
				 INTRL2_CPU_MASK_SET);
	if (bcmgenet_has_mdio_intr(priv))
		bcmgenet_intrl2_0_writel(priv,
					 UMAC_IRQ_MDIO_EVENT,
					 INTRL2_CPU_MASK_CLEAR);

	/* Disable Magic Packet Detection */
	if (priv->wolopts & (WAKE_MAGIC | WAKE_MAGICSECURE)) {
		reg = bcmgenet_umac_readl(priv, UMAC_MPD_CTRL);
		if (!(reg & MPD_EN))
			return -EPERM;	/* already reset so skip the rest */
		reg &= ~(MPD_EN | MPD_PW_EN);
		bcmgenet_umac_writel(priv, reg, UMAC_MPD_CTRL);
	}

	/* Disable ACPI mode */
	reg = bcmgenet_hfb_reg_readl(priv, HFB_CTRL);
	if (!(reg & RBUF_ACPI_EN))
		return -EPERM;	/* already reset so skip the rest */
	reg &= ~RBUF_ACPI_EN;
	bcmgenet_hfb_reg_writel(priv, reg, HFB_CTRL);

	/* Disable CRC Forward */
	spin_lock_bh(&priv->reg_lock);
	reg = bcmgenet_umac_readl(priv, UMAC_CMD);
	reg &= ~CMD_CRC_FWD;
	bcmgenet_umac_writel(priv, reg, UMAC_CMD);
	spin_unlock_bh(&priv->reg_lock);

	/* Resume link status tracking */
	mutex_lock(&dev->phydev->lock);
	if (dev->phydev->link)
		dev->phydev->state = PHY_RUNNING;
	else
		dev->phydev->state = PHY_NOLINK;
	mutex_unlock(&dev->phydev->lock);

	return 0;
}

// SPDX-License-Identifier: (GPL-2.0+ OR BSD-3-Clause)
/* Copyright 2017-2019 NXP */

#include <linux/net_tstamp.h>
#include <linux/module.h>
#include "enetc.h"

static const u32 enetc_si_regs[] = {
	ENETC_SIMR, ENETC_SIPMAR0, ENETC_SIPMAR1, ENETC_SICBDRMR,
	ENETC_SICBDRSR,	ENETC_SICBDRBAR0, ENETC_SICBDRBAR1, ENETC_SICBDRPIR,
	ENETC_SICBDRCIR, ENETC_SICBDRLENR, ENETC_SICAPR0, ENETC_SICAPR1,
	ENETC_SIUEFDCR
};

static const u32 enetc_txbdr_regs[] = {
	ENETC_TBMR, ENETC_TBSR, ENETC_TBBAR0, ENETC_TBBAR1,
	ENETC_TBPIR, ENETC_TBCIR, ENETC_TBLENR, ENETC_TBIER
};

static const u32 enetc_rxbdr_regs[] = {
	ENETC_RBMR, ENETC_RBSR, ENETC_RBBSR, ENETC_RBCIR, ENETC_RBBAR0,
	ENETC_RBBAR1, ENETC_RBPIR, ENETC_RBLENR, ENETC_RBICIR0, ENETC_RBIER
};

static const u32 enetc_port_regs[] = {
	ENETC_PMR, ENETC_PSR, ENETC_PSIPMR, ENETC_PSIPMAR0(0),
	ENETC_PSIPMAR1(0), ENETC_PTXMBAR, ENETC_PCAPR0, ENETC_PCAPR1,
	ENETC_PSICFGR0(0), ENETC_PTCMSDUR(0), ENETC_PM0_CMD_CFG,
	ENETC_PM0_MAXFRM, ENETC_PM0_IF_MODE
};

static int enetc_get_reglen(struct net_device *ndev)
{
	struct enetc_ndev_priv *priv = netdev_priv(ndev);
	struct enetc_hw *hw = &priv->si->hw;
	int len;

	len = ARRAY_SIZE(enetc_si_regs);
	len += ARRAY_SIZE(enetc_txbdr_regs) * priv->num_tx_rings;
	len += ARRAY_SIZE(enetc_rxbdr_regs) * priv->num_rx_rings;

	if (hw->port)
		len += ARRAY_SIZE(enetc_port_regs);

	len *= sizeof(u32) * 2; /* store 2 entries per reg: addr and value */

	return len;
}

static void enetc_get_regs(struct net_device *ndev, struct ethtool_regs *regs,
			   void *regbuf)
{
	struct enetc_ndev_priv *priv = netdev_priv(ndev);
	struct enetc_hw *hw = &priv->si->hw;
	u32 *buf = (u32 *)regbuf;
	int i, j;
	u32 addr;

	for (i = 0; i < ARRAY_SIZE(enetc_si_regs); i++) {
		*buf++ = enetc_si_regs[i];
		*buf++ = enetc_rd(hw, enetc_si_regs[i]);
	}

	for (i = 0; i < priv->num_tx_rings; i++) {
		for (j = 0; j < ARRAY_SIZE(enetc_txbdr_regs); j++) {
			addr = ENETC_BDR(TX, i, enetc_txbdr_regs[j]);

			*buf++ = addr;
			*buf++ = enetc_rd(hw, addr);
		}
	}

	for (i = 0; i < priv->num_rx_rings; i++) {
		for (j = 0; j < ARRAY_SIZE(enetc_rxbdr_regs); j++) {
			addr = ENETC_BDR(RX, i, enetc_rxbdr_regs[j]);

			*buf++ = addr;
			*buf++ = enetc_rd(hw, addr);
		}
	}

	if (!hw->port)
		return;

	for (i = 0; i < ARRAY_SIZE(enetc_port_regs); i++) {
		addr = ENETC_PORT_BASE + enetc_port_regs[i];
		*buf++ = addr;
		*buf++ = enetc_rd(hw, addr);
	}
}

static void enetc_get_ringparam(struct net_device *ndev,
				struct ethtool_ringparam *ring)
{
	struct enetc_ndev_priv *priv = netdev_priv(ndev);

	ring->rx_pending = priv->rx_bd_count;
	ring->tx_pending = priv->tx_bd_count;

	/* do some h/w sanity checks for BDR length */
	if (netif_running(ndev)) {
		struct enetc_hw *hw = &priv->si->hw;
		u32 val = enetc_rxbdr_rd(hw, 0, ENETC_RBLENR);

		if (val != priv->rx_bd_count)
			netif_err(priv, hw, ndev, "RxBDR[RBLENR] = %d!\n", val);

		val = enetc_txbdr_rd(hw, 0, ENETC_TBLENR);

		if (val != priv->tx_bd_count)
			netif_err(priv, hw, ndev, "TxBDR[TBLENR] = %d!\n", val);
	}
}

static const struct ethtool_ops enetc_pf_ethtool_ops = {
	.get_regs_len = enetc_get_reglen,
	.get_regs = enetc_get_regs,
	.get_ringparam = enetc_get_ringparam,
	.get_link_ksettings = phy_ethtool_get_link_ksettings,
	.set_link_ksettings = phy_ethtool_set_link_ksettings,
};

static const struct ethtool_ops enetc_vf_ethtool_ops = {
	.get_regs_len = enetc_get_reglen,
	.get_regs = enetc_get_regs,
	.get_ringparam = enetc_get_ringparam,
};

void enetc_set_ethtool_ops(struct net_device *ndev)
{
	struct enetc_ndev_priv *priv = netdev_priv(ndev);

	if (enetc_si_is_pf(priv->si))
		ndev->ethtool_ops = &enetc_pf_ethtool_ops;
	else
		ndev->ethtool_ops = &enetc_vf_ethtool_ops;
}

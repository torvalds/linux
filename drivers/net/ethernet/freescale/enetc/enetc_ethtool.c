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

static const struct {
	int reg;
	char name[ETH_GSTRING_LEN];
} enetc_si_counters[] =  {
	{ ENETC_SIROCT, "SI rx octets" },
	{ ENETC_SIRFRM, "SI rx frames" },
	{ ENETC_SIRUCA, "SI rx u-cast frames" },
	{ ENETC_SIRMCA, "SI rx m-cast frames" },
	{ ENETC_SITOCT, "SI tx octets" },
	{ ENETC_SITFRM, "SI tx frames" },
	{ ENETC_SITUCA, "SI tx u-cast frames" },
	{ ENETC_SITMCA, "SI tx m-cast frames" },
	{ ENETC_RBDCR(0), "Rx ring  0 discarded frames" },
	{ ENETC_RBDCR(1), "Rx ring  1 discarded frames" },
	{ ENETC_RBDCR(2), "Rx ring  2 discarded frames" },
	{ ENETC_RBDCR(3), "Rx ring  3 discarded frames" },
	{ ENETC_RBDCR(4), "Rx ring  4 discarded frames" },
	{ ENETC_RBDCR(5), "Rx ring  5 discarded frames" },
	{ ENETC_RBDCR(6), "Rx ring  6 discarded frames" },
	{ ENETC_RBDCR(7), "Rx ring  7 discarded frames" },
	{ ENETC_RBDCR(8), "Rx ring  8 discarded frames" },
	{ ENETC_RBDCR(9), "Rx ring  9 discarded frames" },
	{ ENETC_RBDCR(10), "Rx ring 10 discarded frames" },
	{ ENETC_RBDCR(11), "Rx ring 11 discarded frames" },
	{ ENETC_RBDCR(12), "Rx ring 12 discarded frames" },
	{ ENETC_RBDCR(13), "Rx ring 13 discarded frames" },
	{ ENETC_RBDCR(14), "Rx ring 14 discarded frames" },
	{ ENETC_RBDCR(15), "Rx ring 15 discarded frames" },
};

static const struct {
	int reg;
	char name[ETH_GSTRING_LEN];
} enetc_port_counters[] = {
	{ ENETC_PM0_REOCT,  "MAC rx ethernet octets" },
	{ ENETC_PM0_RALN,   "MAC rx alignment errors" },
	{ ENETC_PM0_RXPF,   "MAC rx valid pause frames" },
	{ ENETC_PM0_RFRM,   "MAC rx valid frames" },
	{ ENETC_PM0_RFCS,   "MAC rx fcs errors" },
	{ ENETC_PM0_RVLAN,  "MAC rx VLAN frames" },
	{ ENETC_PM0_RERR,   "MAC rx frame errors" },
	{ ENETC_PM0_RUCA,   "MAC rx unicast frames" },
	{ ENETC_PM0_RMCA,   "MAC rx multicast frames" },
	{ ENETC_PM0_RBCA,   "MAC rx broadcast frames" },
	{ ENETC_PM0_RDRP,   "MAC rx dropped packets" },
	{ ENETC_PM0_RPKT,   "MAC rx packets" },
	{ ENETC_PM0_RUND,   "MAC rx undersized packets" },
	{ ENETC_PM0_R64,    "MAC rx 64 byte packets" },
	{ ENETC_PM0_R127,   "MAC rx 65-127 byte packets" },
	{ ENETC_PM0_R255,   "MAC rx 128-255 byte packets" },
	{ ENETC_PM0_R511,   "MAC rx 256-511 byte packets" },
	{ ENETC_PM0_R1023,  "MAC rx 512-1023 byte packets" },
	{ ENETC_PM0_R1518,  "MAC rx 1024-1518 byte packets" },
	{ ENETC_PM0_R1519X, "MAC rx 1519 to max-octet packets" },
	{ ENETC_PM0_ROVR,   "MAC rx oversized packets" },
	{ ENETC_PM0_RJBR,   "MAC rx jabber packets" },
	{ ENETC_PM0_RFRG,   "MAC rx fragment packets" },
	{ ENETC_PM0_RCNP,   "MAC rx control packets" },
	{ ENETC_PM0_RDRNTP, "MAC rx fifo drop" },
	{ ENETC_PM0_TEOCT,  "MAC tx ethernet octets" },
	{ ENETC_PM0_TOCT,   "MAC tx octets" },
	{ ENETC_PM0_TCRSE,  "MAC tx carrier sense errors" },
	{ ENETC_PM0_TXPF,   "MAC tx valid pause frames" },
	{ ENETC_PM0_TFRM,   "MAC tx frames" },
	{ ENETC_PM0_TFCS,   "MAC tx fcs errors" },
	{ ENETC_PM0_TVLAN,  "MAC tx VLAN frames" },
	{ ENETC_PM0_TERR,   "MAC tx frames" },
	{ ENETC_PM0_TUCA,   "MAC tx unicast frames" },
	{ ENETC_PM0_TMCA,   "MAC tx multicast frames" },
	{ ENETC_PM0_TBCA,   "MAC tx broadcast frames" },
	{ ENETC_PM0_TPKT,   "MAC tx packets" },
	{ ENETC_PM0_TUND,   "MAC tx undersized packets" },
	{ ENETC_PM0_T127,   "MAC tx 65-127 byte packets" },
	{ ENETC_PM0_T1023,  "MAC tx 512-1023 byte packets" },
	{ ENETC_PM0_T1518,  "MAC tx 1024-1518 byte packets" },
	{ ENETC_PM0_TCNP,   "MAC tx control packets" },
	{ ENETC_PM0_TDFR,   "MAC tx deferred packets" },
	{ ENETC_PM0_TMCOL,  "MAC tx multiple collisions" },
	{ ENETC_PM0_TSCOL,  "MAC tx single collisions" },
	{ ENETC_PM0_TLCOL,  "MAC tx late collisions" },
	{ ENETC_PM0_TECOL,  "MAC tx excessive collisions" },
	{ ENETC_UFDMF,      "SI MAC nomatch u-cast discards" },
	{ ENETC_MFDMF,      "SI MAC nomatch m-cast discards" },
	{ ENETC_PBFDSIR,    "SI MAC nomatch b-cast discards" },
	{ ENETC_PUFDVFR,    "SI VLAN nomatch u-cast discards" },
	{ ENETC_PMFDVFR,    "SI VLAN nomatch m-cast discards" },
	{ ENETC_PBFDVFR,    "SI VLAN nomatch b-cast discards" },
	{ ENETC_PFDMSAPR,   "SI pruning discarded frames" },
	{ ENETC_PICDR(0),   "ICM DR0 discarded frames" },
	{ ENETC_PICDR(1),   "ICM DR1 discarded frames" },
	{ ENETC_PICDR(2),   "ICM DR2 discarded frames" },
	{ ENETC_PICDR(3),   "ICM DR3 discarded frames" },
};

static const char rx_ring_stats[][ETH_GSTRING_LEN] = {
	"Rx ring %2d frames",
	"Rx ring %2d alloc errors",
};

static const char tx_ring_stats[][ETH_GSTRING_LEN] = {
	"Tx ring %2d frames",
};

static int enetc_get_sset_count(struct net_device *ndev, int sset)
{
	struct enetc_ndev_priv *priv = netdev_priv(ndev);

	if (sset == ETH_SS_STATS)
		return ARRAY_SIZE(enetc_si_counters) +
			ARRAY_SIZE(tx_ring_stats) * priv->num_tx_rings +
			ARRAY_SIZE(rx_ring_stats) * priv->num_rx_rings +
			(enetc_si_is_pf(priv->si) ?
			ARRAY_SIZE(enetc_port_counters) : 0);

	return -EOPNOTSUPP;
}

static void enetc_get_strings(struct net_device *ndev, u32 stringset, u8 *data)
{
	struct enetc_ndev_priv *priv = netdev_priv(ndev);
	u8 *p = data;
	int i, j;

	switch (stringset) {
	case ETH_SS_STATS:
		for (i = 0; i < ARRAY_SIZE(enetc_si_counters); i++) {
			strlcpy(p, enetc_si_counters[i].name, ETH_GSTRING_LEN);
			p += ETH_GSTRING_LEN;
		}
		for (i = 0; i < priv->num_tx_rings; i++) {
			for (j = 0; j < ARRAY_SIZE(tx_ring_stats); j++) {
				snprintf(p, ETH_GSTRING_LEN, tx_ring_stats[j],
					 i);
				p += ETH_GSTRING_LEN;
			}
		}
		for (i = 0; i < priv->num_rx_rings; i++) {
			for (j = 0; j < ARRAY_SIZE(rx_ring_stats); j++) {
				snprintf(p, ETH_GSTRING_LEN, rx_ring_stats[j],
					 i);
				p += ETH_GSTRING_LEN;
			}
		}

		if (!enetc_si_is_pf(priv->si))
			break;

		for (i = 0; i < ARRAY_SIZE(enetc_port_counters); i++) {
			strlcpy(p, enetc_port_counters[i].name,
				ETH_GSTRING_LEN);
			p += ETH_GSTRING_LEN;
		}
		break;
	}
}

static void enetc_get_ethtool_stats(struct net_device *ndev,
				    struct ethtool_stats *stats, u64 *data)
{
	struct enetc_ndev_priv *priv = netdev_priv(ndev);
	struct enetc_hw *hw = &priv->si->hw;
	int i, o = 0;

	for (i = 0; i < ARRAY_SIZE(enetc_si_counters); i++)
		data[o++] = enetc_rd64(hw, enetc_si_counters[i].reg);

	for (i = 0; i < priv->num_tx_rings; i++)
		data[o++] = priv->tx_ring[i]->stats.packets;

	for (i = 0; i < priv->num_rx_rings; i++) {
		data[o++] = priv->rx_ring[i]->stats.packets;
		data[o++] = priv->rx_ring[i]->stats.rx_alloc_errs;
	}

	if (!enetc_si_is_pf(priv->si))
		return;

	for (i = 0; i < ARRAY_SIZE(enetc_port_counters); i++)
		data[o++] = enetc_port_rd(hw, enetc_port_counters[i].reg);
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
	.get_sset_count = enetc_get_sset_count,
	.get_strings = enetc_get_strings,
	.get_ethtool_stats = enetc_get_ethtool_stats,
	.get_ringparam = enetc_get_ringparam,
	.get_link_ksettings = phy_ethtool_get_link_ksettings,
	.set_link_ksettings = phy_ethtool_set_link_ksettings,
};

static const struct ethtool_ops enetc_vf_ethtool_ops = {
	.get_regs_len = enetc_get_reglen,
	.get_regs = enetc_get_regs,
	.get_sset_count = enetc_get_sset_count,
	.get_strings = enetc_get_strings,
	.get_ethtool_stats = enetc_get_ethtool_stats,
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

// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2024 Furong Xu <0x1207@gmail.com>
 * stmmac FPE(802.3 Qbu) handling
 */
#include "stmmac.h"
#include "stmmac_fpe.h"
#include "dwmac4.h"
#include "dwmac5.h"
#include "dwxgmac2.h"

#define GMAC5_MAC_FPE_CTRL_STS		0x00000234
#define XGMAC_MAC_FPE_CTRL_STS		0x00000280

#define GMAC5_MTL_FPE_CTRL_STS		0x00000c90
#define XGMAC_MTL_FPE_CTRL_STS		0x00001090
/* Preemption Classification */
#define FPE_MTL_PREEMPTION_CLASS	GENMASK(15, 8)
/* Additional Fragment Size of preempted frames */
#define FPE_MTL_ADD_FRAG_SZ		GENMASK(1, 0)

#define STMMAC_MAC_FPE_CTRL_STS_TRSP	BIT(19)
#define STMMAC_MAC_FPE_CTRL_STS_TVER	BIT(18)
#define STMMAC_MAC_FPE_CTRL_STS_RRSP	BIT(17)
#define STMMAC_MAC_FPE_CTRL_STS_RVER	BIT(16)
#define STMMAC_MAC_FPE_CTRL_STS_SRSP	BIT(2)
#define STMMAC_MAC_FPE_CTRL_STS_SVER	BIT(1)
#define STMMAC_MAC_FPE_CTRL_STS_EFPE	BIT(0)

struct stmmac_fpe_reg {
	const u32 mac_fpe_reg;		/* offset of MAC_FPE_CTRL_STS */
	const u32 mtl_fpe_reg;		/* offset of MTL_FPE_CTRL_STS */
	const u32 rxq_ctrl1_reg;	/* offset of MAC_RxQ_Ctrl1 */
	const u32 fprq_mask;		/* Frame Preemption Residue Queue */
	const u32 int_en_reg;		/* offset of MAC_Interrupt_Enable */
	const u32 int_en_bit;		/* Frame Preemption Interrupt Enable */
};

bool stmmac_fpe_supported(struct stmmac_priv *priv)
{
	return priv->dma_cap.fpesel && priv->fpe_cfg.reg &&
		priv->hw->mac->fpe_map_preemption_class;
}

static void stmmac_fpe_configure_tx(struct ethtool_mmsv *mmsv, bool tx_enable)
{
	struct stmmac_fpe_cfg *cfg = container_of(mmsv, struct stmmac_fpe_cfg, mmsv);
	struct stmmac_priv *priv = container_of(cfg, struct stmmac_priv, fpe_cfg);
	const struct stmmac_fpe_reg *reg = cfg->reg;
	u32 num_rxq = priv->plat->rx_queues_to_use;
	void __iomem *ioaddr = priv->ioaddr;
	u32 value;

	if (tx_enable) {
		cfg->fpe_csr = STMMAC_MAC_FPE_CTRL_STS_EFPE;
		value = readl(ioaddr + reg->rxq_ctrl1_reg);
		value &= ~reg->fprq_mask;
		/* Keep this SHIFT, FIELD_PREP() expects a constant mask :-/ */
		value |= (num_rxq - 1) << __ffs(reg->fprq_mask);
		writel(value, ioaddr + reg->rxq_ctrl1_reg);
	} else {
		cfg->fpe_csr = 0;
	}
	writel(cfg->fpe_csr, ioaddr + reg->mac_fpe_reg);
}

static void stmmac_fpe_configure_pmac(struct ethtool_mmsv *mmsv, bool pmac_enable)
{
	struct stmmac_fpe_cfg *cfg = container_of(mmsv, struct stmmac_fpe_cfg, mmsv);
	struct stmmac_priv *priv = container_of(cfg, struct stmmac_priv, fpe_cfg);
	const struct stmmac_fpe_reg *reg = cfg->reg;
	void __iomem *ioaddr = priv->ioaddr;
	u32 value;

	value = readl(ioaddr + reg->int_en_reg);

	if (pmac_enable) {
		if (!(value & reg->int_en_bit)) {
			/* Dummy read to clear any pending masked interrupts */
			readl(ioaddr + reg->mac_fpe_reg);

			value |= reg->int_en_bit;
		}
	} else {
		value &= ~reg->int_en_bit;
	}

	writel(value, ioaddr + reg->int_en_reg);
}

static void stmmac_fpe_send_mpacket(struct ethtool_mmsv *mmsv,
				    enum ethtool_mpacket type)
{
	struct stmmac_fpe_cfg *cfg = container_of(mmsv, struct stmmac_fpe_cfg, mmsv);
	struct stmmac_priv *priv = container_of(cfg, struct stmmac_priv, fpe_cfg);
	const struct stmmac_fpe_reg *reg = cfg->reg;
	void __iomem *ioaddr = priv->ioaddr;
	u32 value = cfg->fpe_csr;

	if (type == ETHTOOL_MPACKET_VERIFY)
		value |= STMMAC_MAC_FPE_CTRL_STS_SVER;
	else if (type == ETHTOOL_MPACKET_RESPONSE)
		value |= STMMAC_MAC_FPE_CTRL_STS_SRSP;

	writel(value, ioaddr + reg->mac_fpe_reg);
}

static const struct ethtool_mmsv_ops stmmac_mmsv_ops = {
	.configure_tx = stmmac_fpe_configure_tx,
	.configure_pmac = stmmac_fpe_configure_pmac,
	.send_mpacket = stmmac_fpe_send_mpacket,
};

static void stmmac_fpe_event_status(struct stmmac_priv *priv, int status)
{
	struct stmmac_fpe_cfg *fpe_cfg = &priv->fpe_cfg;
	struct ethtool_mmsv *mmsv = &fpe_cfg->mmsv;

	if (status == FPE_EVENT_UNKNOWN)
		return;

	if ((status & FPE_EVENT_RVER) == FPE_EVENT_RVER)
		ethtool_mmsv_event_handle(mmsv, ETHTOOL_MMSV_LP_SENT_VERIFY_MPACKET);

	if ((status & FPE_EVENT_TVER) == FPE_EVENT_TVER)
		ethtool_mmsv_event_handle(mmsv, ETHTOOL_MMSV_LD_SENT_VERIFY_MPACKET);

	if ((status & FPE_EVENT_RRSP) == FPE_EVENT_RRSP)
		ethtool_mmsv_event_handle(mmsv, ETHTOOL_MMSV_LP_SENT_RESPONSE_MPACKET);
}

void stmmac_fpe_irq_status(struct stmmac_priv *priv)
{
	const struct stmmac_fpe_reg *reg = priv->fpe_cfg.reg;
	void __iomem *ioaddr = priv->ioaddr;
	struct net_device *dev = priv->dev;
	int status = FPE_EVENT_UNKNOWN;
	u32 value;

	/* Reads from the MAC_FPE_CTRL_STS register should only be performed
	 * here, since the status flags of MAC_FPE_CTRL_STS are "clear on read"
	 */
	value = readl(ioaddr + reg->mac_fpe_reg);

	if (value & STMMAC_MAC_FPE_CTRL_STS_TRSP) {
		status |= FPE_EVENT_TRSP;
		netdev_dbg(dev, "FPE: Respond mPacket is transmitted\n");
	}

	if (value & STMMAC_MAC_FPE_CTRL_STS_TVER) {
		status |= FPE_EVENT_TVER;
		netdev_dbg(dev, "FPE: Verify mPacket is transmitted\n");
	}

	if (value & STMMAC_MAC_FPE_CTRL_STS_RRSP) {
		status |= FPE_EVENT_RRSP;
		netdev_dbg(dev, "FPE: Respond mPacket is received\n");
	}

	if (value & STMMAC_MAC_FPE_CTRL_STS_RVER) {
		status |= FPE_EVENT_RVER;
		netdev_dbg(dev, "FPE: Verify mPacket is received\n");
	}

	stmmac_fpe_event_status(priv, status);
}

void stmmac_fpe_init(struct stmmac_priv *priv)
{
	ethtool_mmsv_init(&priv->fpe_cfg.mmsv, priv->dev,
			  &stmmac_mmsv_ops);

	if ((!priv->fpe_cfg.reg || !priv->hw->mac->fpe_map_preemption_class) &&
	    priv->dma_cap.fpesel)
		dev_info(priv->device, "FPE is not supported by driver.\n");
}

int stmmac_fpe_get_add_frag_size(struct stmmac_priv *priv)
{
	const struct stmmac_fpe_reg *reg = priv->fpe_cfg.reg;
	void __iomem *ioaddr = priv->ioaddr;

	return FIELD_GET(FPE_MTL_ADD_FRAG_SZ, readl(ioaddr + reg->mtl_fpe_reg));
}

void stmmac_fpe_set_add_frag_size(struct stmmac_priv *priv, u32 add_frag_size)
{
	const struct stmmac_fpe_reg *reg = priv->fpe_cfg.reg;
	void __iomem *ioaddr = priv->ioaddr;
	u32 value;

	value = readl(ioaddr + reg->mtl_fpe_reg);
	writel(u32_replace_bits(value, add_frag_size, FPE_MTL_ADD_FRAG_SZ),
	       ioaddr + reg->mtl_fpe_reg);
}

#define ALG_ERR_MSG "TX algorithm SP is not suitable for one-to-many mapping"
#define WEIGHT_ERR_MSG "TXQ weight %u differs across other TXQs in TC: [%u]"

int dwmac5_fpe_map_preemption_class(struct net_device *ndev,
				    struct netlink_ext_ack *extack, u32 pclass)
{
	u32 val, offset, count, queue_weight, preemptible_txqs = 0;
	struct stmmac_priv *priv = netdev_priv(ndev);
	int num_tc = netdev_get_num_tc(ndev);

	if (!pclass)
		goto update_mapping;

	/* DWMAC CORE4+ can not program TC:TXQ mapping to hardware.
	 *
	 * Synopsys Databook:
	 * "The number of Tx DMA channels is equal to the number of Tx queues,
	 * and is direct one-to-one mapping."
	 */
	for (u32 tc = 0; tc < num_tc; tc++) {
		count = ndev->tc_to_txq[tc].count;
		offset = ndev->tc_to_txq[tc].offset;

		if (pclass & BIT(tc))
			preemptible_txqs |= GENMASK(offset + count - 1, offset);

		/* This is 1:1 mapping, go to next TC */
		if (count == 1)
			continue;

		if (priv->plat->tx_sched_algorithm == MTL_TX_ALGORITHM_SP) {
			NL_SET_ERR_MSG_MOD(extack, ALG_ERR_MSG);
			return -EINVAL;
		}

		queue_weight = priv->plat->tx_queues_cfg[offset].weight;

		for (u32 i = 1; i < count; i++) {
			if (priv->plat->tx_queues_cfg[offset + i].weight !=
			    queue_weight) {
				NL_SET_ERR_MSG_FMT_MOD(extack, WEIGHT_ERR_MSG,
						       queue_weight, tc);
				return -EINVAL;
			}
		}
	}

update_mapping:
	val = readl(priv->ioaddr + GMAC5_MTL_FPE_CTRL_STS);
	writel(u32_replace_bits(val, preemptible_txqs, FPE_MTL_PREEMPTION_CLASS),
	       priv->ioaddr + GMAC5_MTL_FPE_CTRL_STS);

	return 0;
}

int dwxgmac3_fpe_map_preemption_class(struct net_device *ndev,
				      struct netlink_ext_ack *extack, u32 pclass)
{
	u32 val, offset, count, preemptible_txqs = 0;
	struct stmmac_priv *priv = netdev_priv(ndev);
	int num_tc = netdev_get_num_tc(ndev);

	if (!num_tc) {
		/* Restore default TC:Queue mapping */
		for (u32 i = 0; i < priv->plat->tx_queues_to_use; i++) {
			val = readl(priv->ioaddr + XGMAC_MTL_TXQ_OPMODE(i));
			writel(u32_replace_bits(val, i, XGMAC_Q2TCMAP),
			       priv->ioaddr + XGMAC_MTL_TXQ_OPMODE(i));
		}
	}

	/* Synopsys Databook:
	 * "All Queues within a traffic class are selected in a round robin
	 * fashion (when packets are available) when the traffic class is
	 * selected by the scheduler for packet transmission. This is true for
	 * any of the scheduling algorithms."
	 */
	for (u32 tc = 0; tc < num_tc; tc++) {
		count = ndev->tc_to_txq[tc].count;
		offset = ndev->tc_to_txq[tc].offset;

		if (pclass & BIT(tc))
			preemptible_txqs |= GENMASK(offset + count - 1, offset);

		for (u32 i = 0; i < count; i++) {
			val = readl(priv->ioaddr + XGMAC_MTL_TXQ_OPMODE(offset + i));
			writel(u32_replace_bits(val, tc, XGMAC_Q2TCMAP),
			       priv->ioaddr + XGMAC_MTL_TXQ_OPMODE(offset + i));
		}
	}

	val = readl(priv->ioaddr + XGMAC_MTL_FPE_CTRL_STS);
	writel(u32_replace_bits(val, preemptible_txqs, FPE_MTL_PREEMPTION_CLASS),
	       priv->ioaddr + XGMAC_MTL_FPE_CTRL_STS);

	return 0;
}

const struct stmmac_fpe_reg dwmac5_fpe_reg = {
	.mac_fpe_reg = GMAC5_MAC_FPE_CTRL_STS,
	.mtl_fpe_reg = GMAC5_MTL_FPE_CTRL_STS,
	.rxq_ctrl1_reg = GMAC_RXQ_CTRL1,
	.fprq_mask = GMAC_RXQCTRL_FPRQ,
	.int_en_reg = GMAC_INT_EN,
	.int_en_bit = GMAC_INT_FPE_EN,
};

const struct stmmac_fpe_reg dwxgmac3_fpe_reg = {
	.mac_fpe_reg = XGMAC_MAC_FPE_CTRL_STS,
	.mtl_fpe_reg = XGMAC_MTL_FPE_CTRL_STS,
	.rxq_ctrl1_reg = XGMAC_RXQ_CTRL1,
	.fprq_mask = XGMAC_FPRQ,
	.int_en_reg = XGMAC_INT_EN,
	.int_en_bit = XGMAC_FPEIE,
};

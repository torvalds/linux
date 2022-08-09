// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2016, The Linux Foundation. All rights reserved.
 */

#include <linux/ethtool.h>
#include <linux/phy.h>

#include "emac.h"

static const char * const emac_ethtool_stat_strings[] = {
	"rx_ok",
	"rx_bcast",
	"rx_mcast",
	"rx_pause",
	"rx_ctrl",
	"rx_fcs_err",
	"rx_len_err",
	"rx_byte_cnt",
	"rx_runt",
	"rx_frag",
	"rx_sz_64",
	"rx_sz_65_127",
	"rx_sz_128_255",
	"rx_sz_256_511",
	"rx_sz_512_1023",
	"rx_sz_1024_1518",
	"rx_sz_1519_max",
	"rx_sz_ov",
	"rx_rxf_ov",
	"rx_align_err",
	"rx_bcast_byte_cnt",
	"rx_mcast_byte_cnt",
	"rx_err_addr",
	"rx_crc_align",
	"rx_jabbers",
	"tx_ok",
	"tx_bcast",
	"tx_mcast",
	"tx_pause",
	"tx_exc_defer",
	"tx_ctrl",
	"tx_defer",
	"tx_byte_cnt",
	"tx_sz_64",
	"tx_sz_65_127",
	"tx_sz_128_255",
	"tx_sz_256_511",
	"tx_sz_512_1023",
	"tx_sz_1024_1518",
	"tx_sz_1519_max",
	"tx_1_col",
	"tx_2_col",
	"tx_late_col",
	"tx_abort_col",
	"tx_underrun",
	"tx_rd_eop",
	"tx_len_err",
	"tx_trunc",
	"tx_bcast_byte",
	"tx_mcast_byte",
	"tx_col",
};

#define EMAC_STATS_LEN	ARRAY_SIZE(emac_ethtool_stat_strings)

static u32 emac_get_msglevel(struct net_device *netdev)
{
	struct emac_adapter *adpt = netdev_priv(netdev);

	return adpt->msg_enable;
}

static void emac_set_msglevel(struct net_device *netdev, u32 data)
{
	struct emac_adapter *adpt = netdev_priv(netdev);

	adpt->msg_enable = data;
}

static int emac_get_sset_count(struct net_device *netdev, int sset)
{
	switch (sset) {
	case ETH_SS_PRIV_FLAGS:
		return 1;
	case ETH_SS_STATS:
		return EMAC_STATS_LEN;
	default:
		return -EOPNOTSUPP;
	}
}

static void emac_get_strings(struct net_device *netdev, u32 stringset, u8 *data)
{
	unsigned int i;

	switch (stringset) {
	case ETH_SS_PRIV_FLAGS:
		strcpy(data, "single-pause-mode");
		break;

	case ETH_SS_STATS:
		for (i = 0; i < EMAC_STATS_LEN; i++) {
			strscpy(data, emac_ethtool_stat_strings[i],
				ETH_GSTRING_LEN);
			data += ETH_GSTRING_LEN;
		}
		break;
	}
}

static void emac_get_ethtool_stats(struct net_device *netdev,
				   struct ethtool_stats *stats,
				   u64 *data)
{
	struct emac_adapter *adpt = netdev_priv(netdev);

	spin_lock(&adpt->stats.lock);

	emac_update_hw_stats(adpt);
	memcpy(data, &adpt->stats, EMAC_STATS_LEN * sizeof(u64));

	spin_unlock(&adpt->stats.lock);
}

static int emac_nway_reset(struct net_device *netdev)
{
	struct phy_device *phydev = netdev->phydev;

	if (!phydev)
		return -ENODEV;

	return genphy_restart_aneg(phydev);
}

static void emac_get_ringparam(struct net_device *netdev,
			       struct ethtool_ringparam *ring)
{
	struct emac_adapter *adpt = netdev_priv(netdev);

	ring->rx_max_pending = EMAC_MAX_RX_DESCS;
	ring->tx_max_pending = EMAC_MAX_TX_DESCS;
	ring->rx_pending = adpt->rx_desc_cnt;
	ring->tx_pending = adpt->tx_desc_cnt;
}

static int emac_set_ringparam(struct net_device *netdev,
			      struct ethtool_ringparam *ring)
{
	struct emac_adapter *adpt = netdev_priv(netdev);

	/* We don't have separate queues/rings for small/large frames, so
	 * reject any attempt to specify those values separately.
	 */
	if (ring->rx_mini_pending || ring->rx_jumbo_pending)
		return -EINVAL;

	adpt->tx_desc_cnt =
		clamp_val(ring->tx_pending, EMAC_MIN_TX_DESCS, EMAC_MAX_TX_DESCS);

	adpt->rx_desc_cnt =
		clamp_val(ring->rx_pending, EMAC_MIN_RX_DESCS, EMAC_MAX_RX_DESCS);

	if (netif_running(netdev))
		return emac_reinit_locked(adpt);

	return 0;
}

static void emac_get_pauseparam(struct net_device *netdev,
				struct ethtool_pauseparam *pause)
{
	struct emac_adapter *adpt = netdev_priv(netdev);

	pause->autoneg = adpt->automatic ? AUTONEG_ENABLE : AUTONEG_DISABLE;
	pause->rx_pause = adpt->rx_flow_control ? 1 : 0;
	pause->tx_pause = adpt->tx_flow_control ? 1 : 0;
}

static int emac_set_pauseparam(struct net_device *netdev,
			       struct ethtool_pauseparam *pause)
{
	struct emac_adapter *adpt = netdev_priv(netdev);

	adpt->automatic = pause->autoneg == AUTONEG_ENABLE;
	adpt->rx_flow_control = pause->rx_pause != 0;
	adpt->tx_flow_control = pause->tx_pause != 0;

	if (netif_running(netdev))
		return emac_reinit_locked(adpt);

	return 0;
}

/* Selected registers that might want to track during runtime. */
static const u16 emac_regs[] = {
	EMAC_DMA_MAS_CTRL,
	EMAC_MAC_CTRL,
	EMAC_TXQ_CTRL_0,
	EMAC_RXQ_CTRL_0,
	EMAC_DMA_CTRL,
	EMAC_INT_MASK,
	EMAC_AXI_MAST_CTRL,
	EMAC_CORE_HW_VERSION,
	EMAC_MISC_CTRL,
};

/* Every time emac_regs[] above is changed, increase this version number. */
#define EMAC_REGS_VERSION	0

#define EMAC_MAX_REG_SIZE	ARRAY_SIZE(emac_regs)

static void emac_get_regs(struct net_device *netdev,
			  struct ethtool_regs *regs, void *buff)
{
	struct emac_adapter *adpt = netdev_priv(netdev);
	u32 *val = buff;
	unsigned int i;

	regs->version = EMAC_REGS_VERSION;
	regs->len = EMAC_MAX_REG_SIZE * sizeof(u32);

	for (i = 0; i < EMAC_MAX_REG_SIZE; i++)
		val[i] = readl(adpt->base + emac_regs[i]);
}

static int emac_get_regs_len(struct net_device *netdev)
{
	return EMAC_MAX_REG_SIZE * sizeof(u32);
}

#define EMAC_PRIV_ENABLE_SINGLE_PAUSE	BIT(0)

static int emac_set_priv_flags(struct net_device *netdev, u32 flags)
{
	struct emac_adapter *adpt = netdev_priv(netdev);

	adpt->single_pause_mode = !!(flags & EMAC_PRIV_ENABLE_SINGLE_PAUSE);

	if (netif_running(netdev))
		return emac_reinit_locked(adpt);

	return 0;
}

static u32 emac_get_priv_flags(struct net_device *netdev)
{
	struct emac_adapter *adpt = netdev_priv(netdev);

	return adpt->single_pause_mode ? EMAC_PRIV_ENABLE_SINGLE_PAUSE : 0;
}

static const struct ethtool_ops emac_ethtool_ops = {
	.get_link_ksettings = phy_ethtool_get_link_ksettings,
	.set_link_ksettings = phy_ethtool_set_link_ksettings,

	.get_msglevel    = emac_get_msglevel,
	.set_msglevel    = emac_set_msglevel,

	.get_sset_count  = emac_get_sset_count,
	.get_strings = emac_get_strings,
	.get_ethtool_stats = emac_get_ethtool_stats,

	.get_ringparam = emac_get_ringparam,
	.set_ringparam = emac_set_ringparam,

	.get_pauseparam = emac_get_pauseparam,
	.set_pauseparam = emac_set_pauseparam,

	.nway_reset = emac_nway_reset,

	.get_link = ethtool_op_get_link,

	.get_regs_len    = emac_get_regs_len,
	.get_regs        = emac_get_regs,

	.set_priv_flags = emac_set_priv_flags,
	.get_priv_flags = emac_get_priv_flags,
};

void emac_set_ethtool_ops(struct net_device *netdev)
{
	netdev->ethtool_ops = &emac_ethtool_ops;
}

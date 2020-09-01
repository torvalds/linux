// SPDX-License-Identifier: GPL-2.0-only
/* 10G controller driver for Samsung SoCs
 *
 * Copyright (C) 2013 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Author: Siva Reddy Kallam <siva.kallam@samsung.com>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/clk.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/net_tstamp.h>
#include <linux/phy.h>
#include <linux/ptp_clock_kernel.h>

#include "sxgbe_common.h"
#include "sxgbe_reg.h"
#include "sxgbe_dma.h"

struct sxgbe_stats {
	char stat_string[ETH_GSTRING_LEN];
	int sizeof_stat;
	int stat_offset;
};

#define SXGBE_STAT(m)						\
{								\
	#m,							\
	sizeof_field(struct sxgbe_extra_stats, m),		\
	offsetof(struct sxgbe_priv_data, xstats.m)		\
}

static const struct sxgbe_stats sxgbe_gstrings_stats[] = {
	/* TX/RX IRQ events */
	SXGBE_STAT(tx_process_stopped_irq),
	SXGBE_STAT(tx_ctxt_desc_err),
	SXGBE_STAT(tx_threshold),
	SXGBE_STAT(rx_threshold),
	SXGBE_STAT(tx_pkt_n),
	SXGBE_STAT(rx_pkt_n),
	SXGBE_STAT(normal_irq_n),
	SXGBE_STAT(tx_normal_irq_n),
	SXGBE_STAT(rx_normal_irq_n),
	SXGBE_STAT(napi_poll),
	SXGBE_STAT(tx_clean),
	SXGBE_STAT(tx_reset_ic_bit),
	SXGBE_STAT(rx_process_stopped_irq),
	SXGBE_STAT(rx_underflow_irq),

	/* Bus access errors */
	SXGBE_STAT(fatal_bus_error_irq),
	SXGBE_STAT(tx_read_transfer_err),
	SXGBE_STAT(tx_write_transfer_err),
	SXGBE_STAT(tx_desc_access_err),
	SXGBE_STAT(tx_buffer_access_err),
	SXGBE_STAT(tx_data_transfer_err),
	SXGBE_STAT(rx_read_transfer_err),
	SXGBE_STAT(rx_write_transfer_err),
	SXGBE_STAT(rx_desc_access_err),
	SXGBE_STAT(rx_buffer_access_err),
	SXGBE_STAT(rx_data_transfer_err),

	/* EEE-LPI stats */
	SXGBE_STAT(tx_lpi_entry_n),
	SXGBE_STAT(tx_lpi_exit_n),
	SXGBE_STAT(rx_lpi_entry_n),
	SXGBE_STAT(rx_lpi_exit_n),
	SXGBE_STAT(eee_wakeup_error_n),

	/* RX specific */
	/* L2 error */
	SXGBE_STAT(rx_code_gmii_err),
	SXGBE_STAT(rx_watchdog_err),
	SXGBE_STAT(rx_crc_err),
	SXGBE_STAT(rx_gaint_pkt_err),
	SXGBE_STAT(ip_hdr_err),
	SXGBE_STAT(ip_payload_err),
	SXGBE_STAT(overflow_error),

	/* L2 Pkt type */
	SXGBE_STAT(len_pkt),
	SXGBE_STAT(mac_ctl_pkt),
	SXGBE_STAT(dcb_ctl_pkt),
	SXGBE_STAT(arp_pkt),
	SXGBE_STAT(oam_pkt),
	SXGBE_STAT(untag_okt),
	SXGBE_STAT(other_pkt),
	SXGBE_STAT(svlan_tag_pkt),
	SXGBE_STAT(cvlan_tag_pkt),
	SXGBE_STAT(dvlan_ocvlan_icvlan_pkt),
	SXGBE_STAT(dvlan_osvlan_isvlan_pkt),
	SXGBE_STAT(dvlan_osvlan_icvlan_pkt),
	SXGBE_STAT(dvan_ocvlan_icvlan_pkt),

	/* L3/L4 Pkt type */
	SXGBE_STAT(not_ip_pkt),
	SXGBE_STAT(ip4_tcp_pkt),
	SXGBE_STAT(ip4_udp_pkt),
	SXGBE_STAT(ip4_icmp_pkt),
	SXGBE_STAT(ip4_unknown_pkt),
	SXGBE_STAT(ip6_tcp_pkt),
	SXGBE_STAT(ip6_udp_pkt),
	SXGBE_STAT(ip6_icmp_pkt),
	SXGBE_STAT(ip6_unknown_pkt),

	/* Filter specific */
	SXGBE_STAT(vlan_filter_match),
	SXGBE_STAT(sa_filter_fail),
	SXGBE_STAT(da_filter_fail),
	SXGBE_STAT(hash_filter_pass),
	SXGBE_STAT(l3_filter_match),
	SXGBE_STAT(l4_filter_match),

	/* RX context specific */
	SXGBE_STAT(timestamp_dropped),
	SXGBE_STAT(rx_msg_type_no_ptp),
	SXGBE_STAT(rx_ptp_type_sync),
	SXGBE_STAT(rx_ptp_type_follow_up),
	SXGBE_STAT(rx_ptp_type_delay_req),
	SXGBE_STAT(rx_ptp_type_delay_resp),
	SXGBE_STAT(rx_ptp_type_pdelay_req),
	SXGBE_STAT(rx_ptp_type_pdelay_resp),
	SXGBE_STAT(rx_ptp_type_pdelay_follow_up),
	SXGBE_STAT(rx_ptp_announce),
	SXGBE_STAT(rx_ptp_mgmt),
	SXGBE_STAT(rx_ptp_signal),
	SXGBE_STAT(rx_ptp_resv_msg_type),
};
#define SXGBE_STATS_LEN ARRAY_SIZE(sxgbe_gstrings_stats)

static int sxgbe_get_eee(struct net_device *dev,
			 struct ethtool_eee *edata)
{
	struct sxgbe_priv_data *priv = netdev_priv(dev);

	if (!priv->hw_cap.eee)
		return -EOPNOTSUPP;

	edata->eee_enabled = priv->eee_enabled;
	edata->eee_active = priv->eee_active;
	edata->tx_lpi_timer = priv->tx_lpi_timer;

	return phy_ethtool_get_eee(dev->phydev, edata);
}

static int sxgbe_set_eee(struct net_device *dev,
			 struct ethtool_eee *edata)
{
	struct sxgbe_priv_data *priv = netdev_priv(dev);

	priv->eee_enabled = edata->eee_enabled;

	if (!priv->eee_enabled) {
		sxgbe_disable_eee_mode(priv);
	} else {
		/* We are asking for enabling the EEE but it is safe
		 * to verify all by invoking the eee_init function.
		 * In case of failure it will return an error.
		 */
		priv->eee_enabled = sxgbe_eee_init(priv);
		if (!priv->eee_enabled)
			return -EOPNOTSUPP;

		/* Do not change tx_lpi_timer in case of failure */
		priv->tx_lpi_timer = edata->tx_lpi_timer;
	}

	return phy_ethtool_set_eee(dev->phydev, edata);
}

static void sxgbe_getdrvinfo(struct net_device *dev,
			     struct ethtool_drvinfo *info)
{
	strlcpy(info->driver, KBUILD_MODNAME, sizeof(info->driver));
	strlcpy(info->version, DRV_VERSION, sizeof(info->version));
}

static u32 sxgbe_getmsglevel(struct net_device *dev)
{
	struct sxgbe_priv_data *priv = netdev_priv(dev);
	return priv->msg_enable;
}

static void sxgbe_setmsglevel(struct net_device *dev, u32 level)
{
	struct sxgbe_priv_data *priv = netdev_priv(dev);
	priv->msg_enable = level;
}

static void sxgbe_get_strings(struct net_device *dev, u32 stringset, u8 *data)
{
	int i;
	u8 *p = data;

	switch (stringset) {
	case ETH_SS_STATS:
		for (i = 0; i < SXGBE_STATS_LEN; i++) {
			memcpy(p, sxgbe_gstrings_stats[i].stat_string,
			       ETH_GSTRING_LEN);
			p += ETH_GSTRING_LEN;
		}
		break;
	default:
		WARN_ON(1);
		break;
	}
}

static int sxgbe_get_sset_count(struct net_device *netdev, int sset)
{
	int len;

	switch (sset) {
	case ETH_SS_STATS:
		len = SXGBE_STATS_LEN;
		return len;
	default:
		return -EINVAL;
	}
}

static void sxgbe_get_ethtool_stats(struct net_device *dev,
				    struct ethtool_stats *dummy, u64 *data)
{
	struct sxgbe_priv_data *priv = netdev_priv(dev);
	int i;
	char *p;

	if (priv->eee_enabled) {
		int val = phy_get_eee_err(dev->phydev);

		if (val)
			priv->xstats.eee_wakeup_error_n = val;
	}

	for (i = 0; i < SXGBE_STATS_LEN; i++) {
		p = (char *)priv + sxgbe_gstrings_stats[i].stat_offset;
		data[i] = (sxgbe_gstrings_stats[i].sizeof_stat == sizeof(u64))
			? (*(u64 *)p) : (*(u32 *)p);
	}
}

static void sxgbe_get_channels(struct net_device *dev,
			       struct ethtool_channels *channel)
{
	channel->max_rx = SXGBE_MAX_RX_CHANNELS;
	channel->max_tx = SXGBE_MAX_TX_CHANNELS;
	channel->rx_count = SXGBE_RX_QUEUES;
	channel->tx_count = SXGBE_TX_QUEUES;
}

static u32 sxgbe_riwt2usec(u32 riwt, struct sxgbe_priv_data *priv)
{
	unsigned long clk = clk_get_rate(priv->sxgbe_clk);

	if (!clk)
		return 0;

	return (riwt * 256) / (clk / 1000000);
}

static u32 sxgbe_usec2riwt(u32 usec, struct sxgbe_priv_data *priv)
{
	unsigned long clk = clk_get_rate(priv->sxgbe_clk);

	if (!clk)
		return 0;

	return (usec * (clk / 1000000)) / 256;
}

static int sxgbe_get_coalesce(struct net_device *dev,
			      struct ethtool_coalesce *ec)
{
	struct sxgbe_priv_data *priv = netdev_priv(dev);

	if (priv->use_riwt)
		ec->rx_coalesce_usecs = sxgbe_riwt2usec(priv->rx_riwt, priv);

	return 0;
}

static int sxgbe_set_coalesce(struct net_device *dev,
			      struct ethtool_coalesce *ec)
{
	struct sxgbe_priv_data *priv = netdev_priv(dev);
	unsigned int rx_riwt;

	if (!ec->rx_coalesce_usecs)
		return -EINVAL;

	rx_riwt = sxgbe_usec2riwt(ec->rx_coalesce_usecs, priv);

	if ((rx_riwt > SXGBE_MAX_DMA_RIWT) || (rx_riwt < SXGBE_MIN_DMA_RIWT))
		return -EINVAL;
	else if (!priv->use_riwt)
		return -EOPNOTSUPP;

	priv->rx_riwt = rx_riwt;
	priv->hw->dma->rx_watchdog(priv->ioaddr, priv->rx_riwt);

	return 0;
}

static int sxgbe_get_rss_hash_opts(struct sxgbe_priv_data *priv,
				   struct ethtool_rxnfc *cmd)
{
	cmd->data = 0;

	/* Report default options for RSS on sxgbe */
	switch (cmd->flow_type) {
	case TCP_V4_FLOW:
	case UDP_V4_FLOW:
		cmd->data |= RXH_L4_B_0_1 | RXH_L4_B_2_3;
		fallthrough;
	case SCTP_V4_FLOW:
	case AH_ESP_V4_FLOW:
	case AH_V4_FLOW:
	case ESP_V4_FLOW:
	case IPV4_FLOW:
		cmd->data |= RXH_IP_SRC | RXH_IP_DST;
		break;
	case TCP_V6_FLOW:
	case UDP_V6_FLOW:
		cmd->data |= RXH_L4_B_0_1 | RXH_L4_B_2_3;
		fallthrough;
	case SCTP_V6_FLOW:
	case AH_ESP_V6_FLOW:
	case AH_V6_FLOW:
	case ESP_V6_FLOW:
	case IPV6_FLOW:
		cmd->data |= RXH_IP_SRC | RXH_IP_DST;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int sxgbe_get_rxnfc(struct net_device *dev, struct ethtool_rxnfc *cmd,
			   u32 *rule_locs)
{
	struct sxgbe_priv_data *priv = netdev_priv(dev);
	int ret = -EOPNOTSUPP;

	switch (cmd->cmd) {
	case ETHTOOL_GRXFH:
		ret = sxgbe_get_rss_hash_opts(priv, cmd);
		break;
	default:
		break;
	}

	return ret;
}

static int sxgbe_set_rss_hash_opt(struct sxgbe_priv_data *priv,
				  struct ethtool_rxnfc *cmd)
{
	u32 reg_val = 0;

	/* RSS does not support anything other than hashing
	 * to queues on src and dst IPs and ports
	 */
	if (cmd->data & ~(RXH_IP_SRC | RXH_IP_DST |
			  RXH_L4_B_0_1 | RXH_L4_B_2_3))
		return -EINVAL;

	switch (cmd->flow_type) {
	case TCP_V4_FLOW:
	case TCP_V6_FLOW:
		if (!(cmd->data & RXH_IP_SRC) ||
		    !(cmd->data & RXH_IP_DST) ||
		    !(cmd->data & RXH_L4_B_0_1) ||
		    !(cmd->data & RXH_L4_B_2_3))
			return -EINVAL;
		reg_val = SXGBE_CORE_RSS_CTL_TCP4TE;
		break;
	case UDP_V4_FLOW:
	case UDP_V6_FLOW:
		if (!(cmd->data & RXH_IP_SRC) ||
		    !(cmd->data & RXH_IP_DST) ||
		    !(cmd->data & RXH_L4_B_0_1) ||
		    !(cmd->data & RXH_L4_B_2_3))
			return -EINVAL;
		reg_val = SXGBE_CORE_RSS_CTL_UDP4TE;
		break;
	case SCTP_V4_FLOW:
	case AH_ESP_V4_FLOW:
	case AH_V4_FLOW:
	case ESP_V4_FLOW:
	case AH_ESP_V6_FLOW:
	case AH_V6_FLOW:
	case ESP_V6_FLOW:
	case SCTP_V6_FLOW:
	case IPV4_FLOW:
	case IPV6_FLOW:
		if (!(cmd->data & RXH_IP_SRC) ||
		    !(cmd->data & RXH_IP_DST) ||
		    (cmd->data & RXH_L4_B_0_1) ||
		    (cmd->data & RXH_L4_B_2_3))
			return -EINVAL;
		reg_val = SXGBE_CORE_RSS_CTL_IP2TE;
		break;
	default:
		return -EINVAL;
	}

	/* Read SXGBE RSS control register and update */
	reg_val |= readl(priv->ioaddr + SXGBE_CORE_RSS_CTL_REG);
	writel(reg_val, priv->ioaddr + SXGBE_CORE_RSS_CTL_REG);
	readl(priv->ioaddr + SXGBE_CORE_RSS_CTL_REG);

	return 0;
}

static int sxgbe_set_rxnfc(struct net_device *dev, struct ethtool_rxnfc *cmd)
{
	struct sxgbe_priv_data *priv = netdev_priv(dev);
	int ret = -EOPNOTSUPP;

	switch (cmd->cmd) {
	case ETHTOOL_SRXFH:
		ret = sxgbe_set_rss_hash_opt(priv, cmd);
		break;
	default:
		break;
	}

	return ret;
}

static void sxgbe_get_regs(struct net_device *dev,
			   struct ethtool_regs *regs, void *space)
{
	struct sxgbe_priv_data *priv = netdev_priv(dev);
	u32 *reg_space = (u32 *)space;
	int reg_offset;
	int reg_ix = 0;
	void __iomem *ioaddr = priv->ioaddr;

	memset(reg_space, 0x0, REG_SPACE_SIZE);

	/* MAC registers */
	for (reg_offset = START_MAC_REG_OFFSET;
	     reg_offset <= MAX_MAC_REG_OFFSET; reg_offset += 4) {
		reg_space[reg_ix] = readl(ioaddr + reg_offset);
		reg_ix++;
	}

	/* MTL registers */
	for (reg_offset = START_MTL_REG_OFFSET;
	     reg_offset <= MAX_MTL_REG_OFFSET; reg_offset += 4) {
		reg_space[reg_ix] = readl(ioaddr + reg_offset);
		reg_ix++;
	}

	/* DMA registers */
	for (reg_offset = START_DMA_REG_OFFSET;
	     reg_offset <= MAX_DMA_REG_OFFSET; reg_offset += 4) {
		reg_space[reg_ix] = readl(ioaddr + reg_offset);
		reg_ix++;
	}

	BUG_ON(reg_ix * 4 > REG_SPACE_SIZE);
}

static int sxgbe_get_regs_len(struct net_device *dev)
{
	return REG_SPACE_SIZE;
}

static const struct ethtool_ops sxgbe_ethtool_ops = {
	.supported_coalesce_params = ETHTOOL_COALESCE_RX_USECS,
	.get_drvinfo = sxgbe_getdrvinfo,
	.get_msglevel = sxgbe_getmsglevel,
	.set_msglevel = sxgbe_setmsglevel,
	.get_link = ethtool_op_get_link,
	.get_strings = sxgbe_get_strings,
	.get_ethtool_stats = sxgbe_get_ethtool_stats,
	.get_sset_count = sxgbe_get_sset_count,
	.get_channels = sxgbe_get_channels,
	.get_coalesce = sxgbe_get_coalesce,
	.set_coalesce = sxgbe_set_coalesce,
	.get_rxnfc = sxgbe_get_rxnfc,
	.set_rxnfc = sxgbe_set_rxnfc,
	.get_regs = sxgbe_get_regs,
	.get_regs_len = sxgbe_get_regs_len,
	.get_eee = sxgbe_get_eee,
	.set_eee = sxgbe_set_eee,
	.get_link_ksettings = phy_ethtool_get_link_ksettings,
	.set_link_ksettings = phy_ethtool_set_link_ksettings,
};

void sxgbe_set_ethtool_ops(struct net_device *netdev)
{
	netdev->ethtool_ops = &sxgbe_ethtool_ops;
}

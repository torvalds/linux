// SPDX-License-Identifier: GPL-2.0+
// Copyright (c) 2024 Hisilicon Limited.

#include <linux/ethtool.h>
#include <linux/phy.h>
#include <linux/rtnetlink.h>
#include "hbg_common.h"
#include "hbg_err.h"
#include "hbg_ethtool.h"
#include "hbg_hw.h"

struct hbg_ethtool_stats {
	char name[ETH_GSTRING_LEN];
	unsigned long offset;
	u32 reg; /* set to 0 if stats is not updated via dump reg */
};

#define HBG_STATS_I(stats) { #stats, HBG_STATS_FIELD_OFF(stats), 0}
#define HBG_STATS_REG_I(stats, reg) { #stats, HBG_STATS_FIELD_OFF(stats), reg}

static const struct hbg_ethtool_stats hbg_ethtool_stats_info[] = {
	HBG_STATS_I(rx_desc_l2_err_cnt),
	HBG_STATS_I(rx_desc_pkt_len_err_cnt),
	HBG_STATS_I(rx_desc_l3_wrong_head_cnt),
	HBG_STATS_I(rx_desc_l3_csum_err_cnt),
	HBG_STATS_I(rx_desc_l3_len_err_cnt),
	HBG_STATS_I(rx_desc_l3_zero_ttl_cnt),
	HBG_STATS_I(rx_desc_l3_other_cnt),
	HBG_STATS_I(rx_desc_l4_wrong_head_cnt),
	HBG_STATS_I(rx_desc_l4_len_err_cnt),
	HBG_STATS_I(rx_desc_l4_csum_err_cnt),
	HBG_STATS_I(rx_desc_l4_zero_port_num_cnt),
	HBG_STATS_I(rx_desc_l4_other_cnt),
	HBG_STATS_I(rx_desc_ip_ver_err_cnt),
	HBG_STATS_I(rx_desc_ipv4_pkt_cnt),
	HBG_STATS_I(rx_desc_ipv6_pkt_cnt),
	HBG_STATS_I(rx_desc_no_ip_pkt_cnt),
	HBG_STATS_I(rx_desc_ip_pkt_cnt),
	HBG_STATS_I(rx_desc_tcp_pkt_cnt),
	HBG_STATS_I(rx_desc_udp_pkt_cnt),
	HBG_STATS_I(rx_desc_vlan_pkt_cnt),
	HBG_STATS_I(rx_desc_icmp_pkt_cnt),
	HBG_STATS_I(rx_desc_arp_pkt_cnt),
	HBG_STATS_I(rx_desc_rarp_pkt_cnt),
	HBG_STATS_I(rx_desc_multicast_pkt_cnt),
	HBG_STATS_I(rx_desc_broadcast_pkt_cnt),
	HBG_STATS_I(rx_desc_ipsec_pkt_cnt),
	HBG_STATS_I(rx_desc_ip_opt_pkt_cnt),
	HBG_STATS_I(rx_desc_key_not_match_cnt),

	HBG_STATS_REG_I(rx_octets_bad_cnt, HBG_REG_RX_OCTETS_BAD_ADDR),
	HBG_STATS_REG_I(rx_octets_total_filt_cnt,
			HBG_REG_RX_OCTETS_TOTAL_FILT_ADDR),
	HBG_STATS_REG_I(rx_uc_pkt_cnt, HBG_REG_RX_UC_PKTS_ADDR),
	HBG_STATS_REG_I(rx_vlan_pkt_cnt, HBG_REG_RX_TAGGED_ADDR),
	HBG_STATS_REG_I(rx_filt_pkt_cnt, HBG_REG_RX_FILT_PKT_CNT_ADDR),
	HBG_STATS_REG_I(rx_data_error_cnt, HBG_REG_RX_DATA_ERR_ADDR),
	HBG_STATS_REG_I(rx_frame_long_err_cnt, HBG_REG_RX_LONG_ERRORS_ADDR),
	HBG_STATS_REG_I(rx_jabber_err_cnt, HBG_REG_RX_JABBER_ERRORS_ADDR),
	HBG_STATS_REG_I(rx_frame_very_long_err_cnt,
			HBG_REG_RX_VERY_LONG_ERR_CNT_ADDR),
	HBG_STATS_REG_I(rx_frame_runt_err_cnt, HBG_REG_RX_RUNT_ERR_CNT_ADDR),
	HBG_STATS_REG_I(rx_frame_short_err_cnt, HBG_REG_RX_SHORT_ERR_CNT_ADDR),
	HBG_STATS_REG_I(rx_overflow_cnt, HBG_REG_RX_OVER_FLOW_CNT_ADDR),
	HBG_STATS_REG_I(rx_bufrq_err_cnt, HBG_REG_RX_BUFRQ_ERR_CNT_ADDR),
	HBG_STATS_REG_I(rx_we_err_cnt, HBG_REG_RX_WE_ERR_CNT_ADDR),
	HBG_STATS_REG_I(rx_overrun_cnt, HBG_REG_RX_OVERRUN_CNT_ADDR),
	HBG_STATS_REG_I(rx_lengthfield_err_cnt,
			HBG_REG_RX_LENGTHFIELD_ERR_CNT_ADDR),
	HBG_STATS_REG_I(rx_fail_comma_cnt, HBG_REG_RX_FAIL_COMMA_CNT_ADDR),
	HBG_STATS_I(rx_dma_err_cnt),
	HBG_STATS_I(rx_fifo_less_empty_thrsld_cnt),

	HBG_STATS_REG_I(tx_uc_pkt_cnt, HBG_REG_TX_UC_PKTS_ADDR),
	HBG_STATS_REG_I(tx_vlan_pkt_cnt, HBG_REG_TX_TAGGED_ADDR),
	HBG_STATS_REG_I(tx_octets_bad_cnt, HBG_REG_OCTETS_TRANSMITTED_BAD_ADDR),

	HBG_STATS_REG_I(tx_underrun_err_cnt, HBG_REG_TX_UNDERRUN_ADDR),
	HBG_STATS_REG_I(tx_add_cs_fail_cnt, HBG_REG_TX_CS_FAIL_CNT_ADDR),
	HBG_STATS_REG_I(tx_bufrl_err_cnt, HBG_REG_TX_BUFRL_ERR_CNT_ADDR),
	HBG_STATS_REG_I(tx_crc_err_cnt, HBG_REG_TX_CRC_ERROR_ADDR),
	HBG_STATS_REG_I(tx_drop_cnt, HBG_REG_TX_DROP_CNT_ADDR),
	HBG_STATS_REG_I(tx_excessive_length_drop_cnt,
			HBG_REG_TX_EXCESSIVE_LENGTH_DROP_ADDR),
	HBG_STATS_I(tx_dma_err_cnt),
	HBG_STATS_I(tx_timeout_cnt),
	HBG_STATS_I(reset_fail_cnt),
};

static const struct hbg_ethtool_stats hbg_ethtool_rmon_stats_info[] = {
	HBG_STATS_I(rx_desc_frag_cnt),
	HBG_STATS_REG_I(rx_framesize_64, HBG_REG_RX_PKTS_64OCTETS_ADDR),
	HBG_STATS_REG_I(rx_framesize_65_127,
			HBG_REG_RX_PKTS_65TO127OCTETS_ADDR),
	HBG_STATS_REG_I(rx_framesize_128_255,
			HBG_REG_RX_PKTS_128TO255OCTETS_ADDR),
	HBG_STATS_REG_I(rx_framesize_256_511,
			HBG_REG_RX_PKTS_256TO511OCTETS_ADDR),
	HBG_STATS_REG_I(rx_framesize_512_1023,
			HBG_REG_RX_PKTS_512TO1023OCTETS_ADDR),
	HBG_STATS_REG_I(rx_framesize_1024_1518,
			HBG_REG_RX_PKTS_1024TO1518OCTETS_ADDR),
	HBG_STATS_REG_I(rx_framesize_bt_1518,
			HBG_REG_RX_PKTS_1519TOMAXOCTETS_ADDR),
	HBG_STATS_REG_I(tx_framesize_64, HBG_REG_TX_PKTS_64OCTETS_ADDR),
	HBG_STATS_REG_I(tx_framesize_65_127,
			HBG_REG_TX_PKTS_65TO127OCTETS_ADDR),
	HBG_STATS_REG_I(tx_framesize_128_255,
			HBG_REG_TX_PKTS_128TO255OCTETS_ADDR),
	HBG_STATS_REG_I(tx_framesize_256_511,
			HBG_REG_TX_PKTS_256TO511OCTETS_ADDR),
	HBG_STATS_REG_I(tx_framesize_512_1023,
			HBG_REG_TX_PKTS_512TO1023OCTETS_ADDR),
	HBG_STATS_REG_I(tx_framesize_1024_1518,
			HBG_REG_TX_PKTS_1024TO1518OCTETS_ADDR),
	HBG_STATS_REG_I(tx_framesize_bt_1518,
			HBG_REG_TX_PKTS_1519TOMAXOCTETS_ADDR),
};

static const struct hbg_ethtool_stats hbg_ethtool_mac_stats_info[] = {
	HBG_STATS_REG_I(rx_mc_pkt_cnt, HBG_REG_RX_MC_PKTS_ADDR),
	HBG_STATS_REG_I(rx_bc_pkt_cnt, HBG_REG_RX_BC_PKTS_ADDR),
	HBG_STATS_REG_I(rx_align_error_cnt, HBG_REG_RX_ALIGN_ERRORS_ADDR),
	HBG_STATS_REG_I(rx_octets_total_ok_cnt,
			HBG_REG_RX_OCTETS_TOTAL_OK_ADDR),
	HBG_STATS_REG_I(rx_trans_pkt_cnt, HBG_REG_RX_TRANS_PKG_CNT_ADDR),
	HBG_STATS_REG_I(rx_fcs_error_cnt, HBG_REG_RX_FCS_ERRORS_ADDR),
	HBG_STATS_REG_I(tx_mc_pkt_cnt, HBG_REG_TX_MC_PKTS_ADDR),
	HBG_STATS_REG_I(tx_bc_pkt_cnt, HBG_REG_TX_BC_PKTS_ADDR),
	HBG_STATS_REG_I(tx_octets_total_ok_cnt,
			HBG_REG_OCTETS_TRANSMITTED_OK_ADDR),
	HBG_STATS_REG_I(tx_trans_pkt_cnt, HBG_REG_TX_TRANS_PKG_CNT_ADDR),
};

static const struct hbg_ethtool_stats hbg_ethtool_ctrl_stats_info[] = {
	HBG_STATS_REG_I(rx_pause_macctl_frame_cnt,
			HBG_REG_RX_PAUSE_MACCTL_FRAMCOUNTER_ADDR),
	HBG_STATS_REG_I(tx_pause_frame_cnt, HBG_REG_TX_PAUSE_FRAMES_ADDR),
	HBG_STATS_REG_I(rx_unknown_macctl_frame_cnt,
			HBG_REG_RX_UNKNOWN_MACCTL_FRAMCOUNTER_ADDR),
};

enum hbg_reg_dump_type {
	HBG_DUMP_REG_TYPE_SPEC = 0,
	HBG_DUMP_REG_TYPE_MDIO,
	HBG_DUMP_REG_TYPE_GMAC,
	HBG_DUMP_REG_TYPE_PCU,
};

struct hbg_reg_info {
	u32 type;
	u32 offset;
	u32 val;
};

#define HBG_DUMP_SPEC_I(offset) {HBG_DUMP_REG_TYPE_SPEC, offset, 0}
#define HBG_DUMP_MDIO_I(offset) {HBG_DUMP_REG_TYPE_MDIO, offset, 0}
#define HBG_DUMP_GMAC_I(offset) {HBG_DUMP_REG_TYPE_GMAC, offset, 0}
#define HBG_DUMP_PCU_I(offset) {HBG_DUMP_REG_TYPE_PCU, offset, 0}

static const struct hbg_reg_info hbg_dump_reg_infos[] = {
	/* dev specs */
	HBG_DUMP_SPEC_I(HBG_REG_SPEC_VALID_ADDR),
	HBG_DUMP_SPEC_I(HBG_REG_EVENT_REQ_ADDR),
	HBG_DUMP_SPEC_I(HBG_REG_MAC_ID_ADDR),
	HBG_DUMP_SPEC_I(HBG_REG_PHY_ID_ADDR),
	HBG_DUMP_SPEC_I(HBG_REG_MAC_ADDR_ADDR),
	HBG_DUMP_SPEC_I(HBG_REG_MAC_ADDR_HIGH_ADDR),
	HBG_DUMP_SPEC_I(HBG_REG_UC_MAC_NUM_ADDR),
	HBG_DUMP_SPEC_I(HBG_REG_MDIO_FREQ_ADDR),
	HBG_DUMP_SPEC_I(HBG_REG_MAX_MTU_ADDR),
	HBG_DUMP_SPEC_I(HBG_REG_MIN_MTU_ADDR),
	HBG_DUMP_SPEC_I(HBG_REG_TX_FIFO_NUM_ADDR),
	HBG_DUMP_SPEC_I(HBG_REG_RX_FIFO_NUM_ADDR),
	HBG_DUMP_SPEC_I(HBG_REG_VLAN_LAYERS_ADDR),

	/* mdio */
	HBG_DUMP_MDIO_I(HBG_REG_MDIO_COMMAND_ADDR),
	HBG_DUMP_MDIO_I(HBG_REG_MDIO_ADDR_ADDR),
	HBG_DUMP_MDIO_I(HBG_REG_MDIO_WDATA_ADDR),
	HBG_DUMP_MDIO_I(HBG_REG_MDIO_RDATA_ADDR),
	HBG_DUMP_MDIO_I(HBG_REG_MDIO_STA_ADDR),

	/* gmac */
	HBG_DUMP_GMAC_I(HBG_REG_DUPLEX_TYPE_ADDR),
	HBG_DUMP_GMAC_I(HBG_REG_FD_FC_TYPE_ADDR),
	HBG_DUMP_GMAC_I(HBG_REG_FC_TX_TIMER_ADDR),
	HBG_DUMP_GMAC_I(HBG_REG_FD_FC_ADDR_LOW_ADDR),
	HBG_DUMP_GMAC_I(HBG_REG_FD_FC_ADDR_HIGH_ADDR),
	HBG_DUMP_GMAC_I(HBG_REG_MAX_FRAME_SIZE_ADDR),
	HBG_DUMP_GMAC_I(HBG_REG_PORT_MODE_ADDR),
	HBG_DUMP_GMAC_I(HBG_REG_PORT_ENABLE_ADDR),
	HBG_DUMP_GMAC_I(HBG_REG_PAUSE_ENABLE_ADDR),
	HBG_DUMP_GMAC_I(HBG_REG_AN_NEG_STATE_ADDR),
	HBG_DUMP_GMAC_I(HBG_REG_TRANSMIT_CTRL_ADDR),
	HBG_DUMP_GMAC_I(HBG_REG_REC_FILT_CTRL_ADDR),
	HBG_DUMP_GMAC_I(HBG_REG_LINE_LOOP_BACK_ADDR),
	HBG_DUMP_GMAC_I(HBG_REG_CF_CRC_STRIP_ADDR),
	HBG_DUMP_GMAC_I(HBG_REG_MODE_CHANGE_EN_ADDR),
	HBG_DUMP_GMAC_I(HBG_REG_LOOP_REG_ADDR),
	HBG_DUMP_GMAC_I(HBG_REG_RECV_CTRL_ADDR),
	HBG_DUMP_GMAC_I(HBG_REG_VLAN_CODE_ADDR),
	HBG_DUMP_GMAC_I(HBG_REG_STATION_ADDR_LOW_0_ADDR),
	HBG_DUMP_GMAC_I(HBG_REG_STATION_ADDR_HIGH_0_ADDR),
	HBG_DUMP_GMAC_I(HBG_REG_STATION_ADDR_LOW_1_ADDR),
	HBG_DUMP_GMAC_I(HBG_REG_STATION_ADDR_HIGH_1_ADDR),
	HBG_DUMP_GMAC_I(HBG_REG_STATION_ADDR_LOW_2_ADDR),
	HBG_DUMP_GMAC_I(HBG_REG_STATION_ADDR_HIGH_2_ADDR),
	HBG_DUMP_GMAC_I(HBG_REG_STATION_ADDR_LOW_3_ADDR),
	HBG_DUMP_GMAC_I(HBG_REG_STATION_ADDR_HIGH_3_ADDR),
	HBG_DUMP_GMAC_I(HBG_REG_STATION_ADDR_LOW_4_ADDR),
	HBG_DUMP_GMAC_I(HBG_REG_STATION_ADDR_HIGH_4_ADDR),
	HBG_DUMP_GMAC_I(HBG_REG_STATION_ADDR_LOW_5_ADDR),
	HBG_DUMP_GMAC_I(HBG_REG_STATION_ADDR_HIGH_5_ADDR),

	/* pcu */
	HBG_DUMP_PCU_I(HBG_REG_TX_FIFO_THRSLD_ADDR),
	HBG_DUMP_PCU_I(HBG_REG_RX_FIFO_THRSLD_ADDR),
	HBG_DUMP_PCU_I(HBG_REG_CFG_FIFO_THRSLD_ADDR),
	HBG_DUMP_PCU_I(HBG_REG_CF_INTRPT_MSK_ADDR),
	HBG_DUMP_PCU_I(HBG_REG_CF_INTRPT_STAT_ADDR),
	HBG_DUMP_PCU_I(HBG_REG_CF_INTRPT_CLR_ADDR),
	HBG_DUMP_PCU_I(HBG_REG_TX_BUS_ERR_ADDR_ADDR),
	HBG_DUMP_PCU_I(HBG_REG_RX_BUS_ERR_ADDR_ADDR),
	HBG_DUMP_PCU_I(HBG_REG_MAX_FRAME_LEN_ADDR),
	HBG_DUMP_PCU_I(HBG_REG_DEBUG_ST_MCH_ADDR),
	HBG_DUMP_PCU_I(HBG_REG_FIFO_CURR_STATUS_ADDR),
	HBG_DUMP_PCU_I(HBG_REG_FIFO_HIST_STATUS_ADDR),
	HBG_DUMP_PCU_I(HBG_REG_CF_CFF_DATA_NUM_ADDR),
	HBG_DUMP_PCU_I(HBG_REG_CF_TX_PAUSE_ADDR),
	HBG_DUMP_PCU_I(HBG_REG_RX_CFF_ADDR_ADDR),
	HBG_DUMP_PCU_I(HBG_REG_RX_BUF_SIZE_ADDR),
	HBG_DUMP_PCU_I(HBG_REG_BUS_CTRL_ADDR),
	HBG_DUMP_PCU_I(HBG_REG_RX_CTRL_ADDR),
	HBG_DUMP_PCU_I(HBG_REG_RX_PKT_MODE_ADDR),
	HBG_DUMP_PCU_I(HBG_REG_DBG_ST0_ADDR),
	HBG_DUMP_PCU_I(HBG_REG_DBG_ST1_ADDR),
	HBG_DUMP_PCU_I(HBG_REG_DBG_ST2_ADDR),
	HBG_DUMP_PCU_I(HBG_REG_BUS_RST_EN_ADDR),
	HBG_DUMP_PCU_I(HBG_REG_CF_IND_TXINT_MSK_ADDR),
	HBG_DUMP_PCU_I(HBG_REG_CF_IND_TXINT_STAT_ADDR),
	HBG_DUMP_PCU_I(HBG_REG_CF_IND_TXINT_CLR_ADDR),
	HBG_DUMP_PCU_I(HBG_REG_CF_IND_RXINT_MSK_ADDR),
	HBG_DUMP_PCU_I(HBG_REG_CF_IND_RXINT_STAT_ADDR),
	HBG_DUMP_PCU_I(HBG_REG_CF_IND_RXINT_CLR_ADDR),
};

static const u32 hbg_dump_type_base_array[] = {
	[HBG_DUMP_REG_TYPE_SPEC] = 0,
	[HBG_DUMP_REG_TYPE_MDIO] = HBG_REG_MDIO_BASE,
	[HBG_DUMP_REG_TYPE_GMAC] = HBG_REG_SGMII_BASE,
	[HBG_DUMP_REG_TYPE_PCU] = HBG_REG_SGMII_BASE,
};

static int hbg_ethtool_get_regs_len(struct net_device *netdev)
{
	return ARRAY_SIZE(hbg_dump_reg_infos) * sizeof(struct hbg_reg_info);
}

static void hbg_ethtool_get_regs(struct net_device *netdev,
				 struct ethtool_regs *regs, void *data)
{
	struct hbg_priv *priv = netdev_priv(netdev);
	struct hbg_reg_info *info;
	u32 i, offset = 0;

	regs->version = 0;
	for (i = 0; i < ARRAY_SIZE(hbg_dump_reg_infos); i++) {
		info = data + offset;

		*info = hbg_dump_reg_infos[i];
		info->val = hbg_reg_read(priv, info->offset);
		info->offset -= hbg_dump_type_base_array[info->type];

		offset += sizeof(*info);
	}
}

static void hbg_ethtool_get_pauseparam(struct net_device *net_dev,
				       struct ethtool_pauseparam *param)
{
	struct hbg_priv *priv = netdev_priv(net_dev);

	param->autoneg = priv->mac.pause_autoneg;
	hbg_hw_get_pause_enable(priv, &param->tx_pause, &param->rx_pause);
}

static int hbg_ethtool_set_pauseparam(struct net_device *net_dev,
				      struct ethtool_pauseparam *param)
{
	struct hbg_priv *priv = netdev_priv(net_dev);

	priv->mac.pause_autoneg = param->autoneg;
	phy_set_asym_pause(priv->mac.phydev, param->rx_pause, param->tx_pause);

	if (!param->autoneg)
		hbg_hw_set_pause_enable(priv, param->tx_pause, param->rx_pause);

	priv->user_def.pause_param = *param;
	return 0;
}

static int hbg_ethtool_reset(struct net_device *netdev, u32 *flags)
{
	struct hbg_priv *priv = netdev_priv(netdev);

	if (*flags != ETH_RESET_DEDICATED)
		return -EOPNOTSUPP;

	*flags = 0;
	return hbg_reset(priv);
}

static void hbg_update_stats_by_info(struct hbg_priv *priv,
				     const struct hbg_ethtool_stats *info,
				     u32 info_len)
{
	const struct hbg_ethtool_stats *stats;
	u32 i;

	if (test_bit(HBG_NIC_STATE_RESETTING, &priv->state))
		return;

	for (i = 0; i < info_len; i++) {
		stats = &info[i];
		if (!stats->reg)
			continue;

		HBG_STATS_U(&priv->stats, stats->offset,
			    hbg_reg_read(priv, stats->reg));
	}
}

void hbg_update_stats(struct hbg_priv *priv)
{
	hbg_update_stats_by_info(priv, hbg_ethtool_stats_info,
				 ARRAY_SIZE(hbg_ethtool_stats_info));
	hbg_update_stats_by_info(priv, hbg_ethtool_rmon_stats_info,
				 ARRAY_SIZE(hbg_ethtool_rmon_stats_info));
	hbg_update_stats_by_info(priv, hbg_ethtool_mac_stats_info,
				 ARRAY_SIZE(hbg_ethtool_mac_stats_info));
	hbg_update_stats_by_info(priv, hbg_ethtool_ctrl_stats_info,
				 ARRAY_SIZE(hbg_ethtool_ctrl_stats_info));
}

static int hbg_ethtool_get_sset_count(struct net_device *netdev, int stringset)
{
	if (stringset != ETH_SS_STATS)
		return -EOPNOTSUPP;

	return ARRAY_SIZE(hbg_ethtool_stats_info);
}

static void hbg_ethtool_get_strings(struct net_device *netdev,
				    u32 stringset, u8 *data)
{
	u32 i;

	if (stringset != ETH_SS_STATS)
		return;

	for (i = 0; i < ARRAY_SIZE(hbg_ethtool_stats_info); i++)
		ethtool_puts(&data, hbg_ethtool_stats_info[i].name);
}

static void hbg_ethtool_get_stats(struct net_device *netdev,
				  struct ethtool_stats *stats, u64 *data)
{
	struct hbg_priv *priv = netdev_priv(netdev);
	u32 i;

	hbg_update_stats(priv);
	for (i = 0; i < ARRAY_SIZE(hbg_ethtool_stats_info); i++)
		*data++ = HBG_STATS_R(&priv->stats,
				      hbg_ethtool_stats_info[i].offset);
}

static void hbg_ethtool_get_pause_stats(struct net_device *netdev,
					struct ethtool_pause_stats *epstats)
{
	struct hbg_priv *priv = netdev_priv(netdev);
	struct hbg_stats *stats = &priv->stats;

	hbg_update_stats(priv);
	epstats->rx_pause_frames = stats->rx_pause_macctl_frame_cnt;
	epstats->tx_pause_frames = stats->tx_pause_frame_cnt;
}

static void hbg_ethtool_get_eth_mac_stats(struct net_device *netdev,
					  struct ethtool_eth_mac_stats *emstats)
{
	struct hbg_priv *priv = netdev_priv(netdev);
	struct hbg_stats *stats = &priv->stats;

	hbg_update_stats(priv);
	emstats->FramesTransmittedOK = stats->tx_trans_pkt_cnt;
	emstats->FramesReceivedOK = stats->rx_trans_pkt_cnt;
	emstats->FrameCheckSequenceErrors = stats->rx_fcs_error_cnt;
	emstats->AlignmentErrors = stats->rx_align_error_cnt;
	emstats->OctetsTransmittedOK = stats->tx_octets_total_ok_cnt;
	emstats->OctetsReceivedOK = stats->rx_octets_total_ok_cnt;

	emstats->MulticastFramesXmittedOK = stats->tx_mc_pkt_cnt;
	emstats->BroadcastFramesXmittedOK = stats->tx_bc_pkt_cnt;
	emstats->MulticastFramesReceivedOK = stats->rx_mc_pkt_cnt;
	emstats->BroadcastFramesReceivedOK = stats->rx_bc_pkt_cnt;
	emstats->InRangeLengthErrors = stats->rx_fcs_error_cnt +
				       stats->rx_jabber_err_cnt +
				       stats->rx_unknown_macctl_frame_cnt +
				       stats->rx_bufrq_err_cnt +
				       stats->rx_we_err_cnt;
	emstats->OutOfRangeLengthField = stats->rx_frame_short_err_cnt +
					 stats->rx_frame_runt_err_cnt +
					 stats->rx_lengthfield_err_cnt +
					 stats->rx_frame_long_err_cnt +
					 stats->rx_frame_very_long_err_cnt;
	emstats->FrameTooLongErrors = stats->rx_frame_long_err_cnt +
				      stats->rx_frame_very_long_err_cnt;
}

static void
hbg_ethtool_get_eth_ctrl_stats(struct net_device *netdev,
			       struct ethtool_eth_ctrl_stats *ecstats)
{
	struct hbg_priv *priv = netdev_priv(netdev);
	struct hbg_stats *s = &priv->stats;

	hbg_update_stats(priv);
	ecstats->MACControlFramesTransmitted = s->tx_pause_frame_cnt;
	ecstats->MACControlFramesReceived = s->rx_pause_macctl_frame_cnt;
	ecstats->UnsupportedOpcodesReceived = s->rx_unknown_macctl_frame_cnt;
}

static const struct ethtool_rmon_hist_range hbg_rmon_ranges[] = {
	{    0,    64 },
	{   65,   127 },
	{  128,   255 },
	{  256,   511 },
	{  512,  1023 },
	{ 1024,  1518 },
	{ 1519,  4095 },
};

static void
hbg_ethtool_get_rmon_stats(struct net_device *netdev,
			   struct ethtool_rmon_stats *rmon_stats,
			   const struct ethtool_rmon_hist_range **ranges)
{
	struct hbg_priv *priv = netdev_priv(netdev);
	struct hbg_stats *stats = &priv->stats;

	hbg_update_stats(priv);
	rmon_stats->undersize_pkts = stats->rx_frame_short_err_cnt +
				     stats->rx_frame_runt_err_cnt +
				     stats->rx_lengthfield_err_cnt;
	rmon_stats->oversize_pkts = stats->rx_frame_long_err_cnt +
				    stats->rx_frame_very_long_err_cnt;
	rmon_stats->fragments = stats->rx_desc_frag_cnt;
	rmon_stats->hist[0] = stats->rx_framesize_64;
	rmon_stats->hist[1] = stats->rx_framesize_65_127;
	rmon_stats->hist[2] = stats->rx_framesize_128_255;
	rmon_stats->hist[3] = stats->rx_framesize_256_511;
	rmon_stats->hist[4] = stats->rx_framesize_512_1023;
	rmon_stats->hist[5] = stats->rx_framesize_1024_1518;
	rmon_stats->hist[6] = stats->rx_framesize_bt_1518;

	rmon_stats->hist_tx[0] = stats->tx_framesize_64;
	rmon_stats->hist_tx[1] = stats->tx_framesize_65_127;
	rmon_stats->hist_tx[2] = stats->tx_framesize_128_255;
	rmon_stats->hist_tx[3] = stats->tx_framesize_256_511;
	rmon_stats->hist_tx[4] = stats->tx_framesize_512_1023;
	rmon_stats->hist_tx[5] = stats->tx_framesize_1024_1518;
	rmon_stats->hist_tx[6] = stats->tx_framesize_bt_1518;

	*ranges = hbg_rmon_ranges;
}

static const struct ethtool_ops hbg_ethtool_ops = {
	.get_link		= ethtool_op_get_link,
	.get_link_ksettings	= phy_ethtool_get_link_ksettings,
	.set_link_ksettings	= phy_ethtool_set_link_ksettings,
	.get_regs_len		= hbg_ethtool_get_regs_len,
	.get_regs		= hbg_ethtool_get_regs,
	.get_pauseparam         = hbg_ethtool_get_pauseparam,
	.set_pauseparam         = hbg_ethtool_set_pauseparam,
	.reset			= hbg_ethtool_reset,
	.nway_reset		= phy_ethtool_nway_reset,
	.get_sset_count		= hbg_ethtool_get_sset_count,
	.get_strings		= hbg_ethtool_get_strings,
	.get_ethtool_stats	= hbg_ethtool_get_stats,
	.get_pause_stats	= hbg_ethtool_get_pause_stats,
	.get_eth_mac_stats	= hbg_ethtool_get_eth_mac_stats,
	.get_eth_ctrl_stats	= hbg_ethtool_get_eth_ctrl_stats,
	.get_rmon_stats		= hbg_ethtool_get_rmon_stats,
};

void hbg_ethtool_set_ops(struct net_device *netdev)
{
	netdev->ethtool_ops = &hbg_ethtool_ops;
}

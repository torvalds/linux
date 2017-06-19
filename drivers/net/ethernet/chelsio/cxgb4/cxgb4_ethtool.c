/*
 *  Copyright (C) 2013-2015 Chelsio Communications.  All rights reserved.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms and conditions of the GNU General Public License,
 *  version 2, as published by the Free Software Foundation.
 *
 *  This program is distributed in the hope it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 *  more details.
 *
 *  The full GNU General Public License is included in this distribution in
 *  the file called "COPYING".
 *
 */

#include <linux/firmware.h>
#include <linux/mdio.h>

#include "cxgb4.h"
#include "t4_regs.h"
#include "t4fw_api.h"

#define EEPROM_MAGIC 0x38E2F10C

static u32 get_msglevel(struct net_device *dev)
{
	return netdev2adap(dev)->msg_enable;
}

static void set_msglevel(struct net_device *dev, u32 val)
{
	netdev2adap(dev)->msg_enable = val;
}

static const char stats_strings[][ETH_GSTRING_LEN] = {
	"tx_octets_ok           ",
	"tx_frames_ok           ",
	"tx_broadcast_frames    ",
	"tx_multicast_frames    ",
	"tx_unicast_frames      ",
	"tx_error_frames        ",

	"tx_frames_64           ",
	"tx_frames_65_to_127    ",
	"tx_frames_128_to_255   ",
	"tx_frames_256_to_511   ",
	"tx_frames_512_to_1023  ",
	"tx_frames_1024_to_1518 ",
	"tx_frames_1519_to_max  ",

	"tx_frames_dropped      ",
	"tx_pause_frames        ",
	"tx_ppp0_frames         ",
	"tx_ppp1_frames         ",
	"tx_ppp2_frames         ",
	"tx_ppp3_frames         ",
	"tx_ppp4_frames         ",
	"tx_ppp5_frames         ",
	"tx_ppp6_frames         ",
	"tx_ppp7_frames         ",

	"rx_octets_ok           ",
	"rx_frames_ok           ",
	"rx_broadcast_frames    ",
	"rx_multicast_frames    ",
	"rx_unicast_frames      ",

	"rx_frames_too_long     ",
	"rx_jabber_errors       ",
	"rx_fcs_errors          ",
	"rx_length_errors       ",
	"rx_symbol_errors       ",
	"rx_runt_frames         ",

	"rx_frames_64           ",
	"rx_frames_65_to_127    ",
	"rx_frames_128_to_255   ",
	"rx_frames_256_to_511   ",
	"rx_frames_512_to_1023  ",
	"rx_frames_1024_to_1518 ",
	"rx_frames_1519_to_max  ",

	"rx_pause_frames        ",
	"rx_ppp0_frames         ",
	"rx_ppp1_frames         ",
	"rx_ppp2_frames         ",
	"rx_ppp3_frames         ",
	"rx_ppp4_frames         ",
	"rx_ppp5_frames         ",
	"rx_ppp6_frames         ",
	"rx_ppp7_frames         ",

	"rx_bg0_frames_dropped  ",
	"rx_bg1_frames_dropped  ",
	"rx_bg2_frames_dropped  ",
	"rx_bg3_frames_dropped  ",
	"rx_bg0_frames_trunc    ",
	"rx_bg1_frames_trunc    ",
	"rx_bg2_frames_trunc    ",
	"rx_bg3_frames_trunc    ",

	"tso                    ",
	"tx_csum_offload        ",
	"rx_csum_good           ",
	"vlan_extractions       ",
	"vlan_insertions        ",
	"gro_packets            ",
	"gro_merged             ",
};

static char adapter_stats_strings[][ETH_GSTRING_LEN] = {
	"db_drop                ",
	"db_full                ",
	"db_empty               ",
	"tcp_ipv4_out_rsts      ",
	"tcp_ipv4_in_segs       ",
	"tcp_ipv4_out_segs      ",
	"tcp_ipv4_retrans_segs  ",
	"tcp_ipv6_out_rsts      ",
	"tcp_ipv6_in_segs       ",
	"tcp_ipv6_out_segs      ",
	"tcp_ipv6_retrans_segs  ",
	"usm_ddp_frames         ",
	"usm_ddp_octets         ",
	"usm_ddp_drops          ",
	"rdma_no_rqe_mod_defer  ",
	"rdma_no_rqe_pkt_defer  ",
	"tp_err_ofld_no_neigh   ",
	"tp_err_ofld_cong_defer ",
	"write_coal_success     ",
	"write_coal_fail        ",
};

static char channel_stats_strings[][ETH_GSTRING_LEN] = {
	"--------Channel--------- ",
	"tp_cpl_requests        ",
	"tp_cpl_responses       ",
	"tp_mac_in_errs         ",
	"tp_hdr_in_errs         ",
	"tp_tcp_in_errs         ",
	"tp_tcp6_in_errs        ",
	"tp_tnl_cong_drops      ",
	"tp_tnl_tx_drops        ",
	"tp_ofld_vlan_drops     ",
	"tp_ofld_chan_drops     ",
	"fcoe_octets_ddp        ",
	"fcoe_frames_ddp        ",
	"fcoe_frames_drop       ",
};

static char loopback_stats_strings[][ETH_GSTRING_LEN] = {
	"-------Loopback----------- ",
	"octets_ok              ",
	"frames_ok              ",
	"bcast_frames           ",
	"mcast_frames           ",
	"ucast_frames           ",
	"error_frames           ",
	"frames_64              ",
	"frames_65_to_127       ",
	"frames_128_to_255      ",
	"frames_256_to_511      ",
	"frames_512_to_1023     ",
	"frames_1024_to_1518    ",
	"frames_1519_to_max     ",
	"frames_dropped         ",
	"bg0_frames_dropped     ",
	"bg1_frames_dropped     ",
	"bg2_frames_dropped     ",
	"bg3_frames_dropped     ",
	"bg0_frames_trunc       ",
	"bg1_frames_trunc       ",
	"bg2_frames_trunc       ",
	"bg3_frames_trunc       ",
};

static int get_sset_count(struct net_device *dev, int sset)
{
	switch (sset) {
	case ETH_SS_STATS:
		return ARRAY_SIZE(stats_strings) +
		       ARRAY_SIZE(adapter_stats_strings) +
		       ARRAY_SIZE(channel_stats_strings) +
		       ARRAY_SIZE(loopback_stats_strings);
	default:
		return -EOPNOTSUPP;
	}
}

static int get_regs_len(struct net_device *dev)
{
	struct adapter *adap = netdev2adap(dev);

	return t4_get_regs_len(adap);
}

static int get_eeprom_len(struct net_device *dev)
{
	return EEPROMSIZE;
}

static void get_drvinfo(struct net_device *dev, struct ethtool_drvinfo *info)
{
	struct adapter *adapter = netdev2adap(dev);
	u32 exprom_vers;

	strlcpy(info->driver, cxgb4_driver_name, sizeof(info->driver));
	strlcpy(info->version, cxgb4_driver_version,
		sizeof(info->version));
	strlcpy(info->bus_info, pci_name(adapter->pdev),
		sizeof(info->bus_info));
	info->regdump_len = get_regs_len(dev);

	if (!adapter->params.fw_vers)
		strcpy(info->fw_version, "N/A");
	else
		snprintf(info->fw_version, sizeof(info->fw_version),
			 "%u.%u.%u.%u, TP %u.%u.%u.%u",
			 FW_HDR_FW_VER_MAJOR_G(adapter->params.fw_vers),
			 FW_HDR_FW_VER_MINOR_G(adapter->params.fw_vers),
			 FW_HDR_FW_VER_MICRO_G(adapter->params.fw_vers),
			 FW_HDR_FW_VER_BUILD_G(adapter->params.fw_vers),
			 FW_HDR_FW_VER_MAJOR_G(adapter->params.tp_vers),
			 FW_HDR_FW_VER_MINOR_G(adapter->params.tp_vers),
			 FW_HDR_FW_VER_MICRO_G(adapter->params.tp_vers),
			 FW_HDR_FW_VER_BUILD_G(adapter->params.tp_vers));

	if (!t4_get_exprom_version(adapter, &exprom_vers))
		snprintf(info->erom_version, sizeof(info->erom_version),
			 "%u.%u.%u.%u",
			 FW_HDR_FW_VER_MAJOR_G(exprom_vers),
			 FW_HDR_FW_VER_MINOR_G(exprom_vers),
			 FW_HDR_FW_VER_MICRO_G(exprom_vers),
			 FW_HDR_FW_VER_BUILD_G(exprom_vers));
}

static void get_strings(struct net_device *dev, u32 stringset, u8 *data)
{
	if (stringset == ETH_SS_STATS) {
		memcpy(data, stats_strings, sizeof(stats_strings));
		data += sizeof(stats_strings);
		memcpy(data, adapter_stats_strings,
		       sizeof(adapter_stats_strings));
		data += sizeof(adapter_stats_strings);
		memcpy(data, channel_stats_strings,
		       sizeof(channel_stats_strings));
		data += sizeof(channel_stats_strings);
		memcpy(data, loopback_stats_strings,
		       sizeof(loopback_stats_strings));
	}
}

/* port stats maintained per queue of the port. They should be in the same
 * order as in stats_strings above.
 */
struct queue_port_stats {
	u64 tso;
	u64 tx_csum;
	u64 rx_csum;
	u64 vlan_ex;
	u64 vlan_ins;
	u64 gro_pkts;
	u64 gro_merged;
};

struct adapter_stats {
	u64 db_drop;
	u64 db_full;
	u64 db_empty;
	u64 tcp_v4_out_rsts;
	u64 tcp_v4_in_segs;
	u64 tcp_v4_out_segs;
	u64 tcp_v4_retrans_segs;
	u64 tcp_v6_out_rsts;
	u64 tcp_v6_in_segs;
	u64 tcp_v6_out_segs;
	u64 tcp_v6_retrans_segs;
	u64 frames;
	u64 octets;
	u64 drops;
	u64 rqe_dfr_mod;
	u64 rqe_dfr_pkt;
	u64 ofld_no_neigh;
	u64 ofld_cong_defer;
	u64 wc_success;
	u64 wc_fail;
};

struct channel_stats {
	u64 cpl_req;
	u64 cpl_rsp;
	u64 mac_in_errs;
	u64 hdr_in_errs;
	u64 tcp_in_errs;
	u64 tcp6_in_errs;
	u64 tnl_cong_drops;
	u64 tnl_tx_drops;
	u64 ofld_vlan_drops;
	u64 ofld_chan_drops;
	u64 octets_ddp;
	u64 frames_ddp;
	u64 frames_drop;
};

static void collect_sge_port_stats(const struct adapter *adap,
				   const struct port_info *p,
				   struct queue_port_stats *s)
{
	int i;
	const struct sge_eth_txq *tx = &adap->sge.ethtxq[p->first_qset];
	const struct sge_eth_rxq *rx = &adap->sge.ethrxq[p->first_qset];

	memset(s, 0, sizeof(*s));
	for (i = 0; i < p->nqsets; i++, rx++, tx++) {
		s->tso += tx->tso;
		s->tx_csum += tx->tx_cso;
		s->rx_csum += rx->stats.rx_cso;
		s->vlan_ex += rx->stats.vlan_ex;
		s->vlan_ins += tx->vlan_ins;
		s->gro_pkts += rx->stats.lro_pkts;
		s->gro_merged += rx->stats.lro_merged;
	}
}

static void collect_adapter_stats(struct adapter *adap, struct adapter_stats *s)
{
	struct tp_tcp_stats v4, v6;
	struct tp_rdma_stats rdma_stats;
	struct tp_err_stats err_stats;
	struct tp_usm_stats usm_stats;
	u64 val1, val2;

	memset(s, 0, sizeof(*s));

	spin_lock(&adap->stats_lock);
	t4_tp_get_tcp_stats(adap, &v4, &v6);
	t4_tp_get_rdma_stats(adap, &rdma_stats);
	t4_get_usm_stats(adap, &usm_stats);
	t4_tp_get_err_stats(adap, &err_stats);
	spin_unlock(&adap->stats_lock);

	s->db_drop = adap->db_stats.db_drop;
	s->db_full = adap->db_stats.db_full;
	s->db_empty = adap->db_stats.db_empty;

	s->tcp_v4_out_rsts = v4.tcp_out_rsts;
	s->tcp_v4_in_segs = v4.tcp_in_segs;
	s->tcp_v4_out_segs = v4.tcp_out_segs;
	s->tcp_v4_retrans_segs = v4.tcp_retrans_segs;
	s->tcp_v6_out_rsts = v6.tcp_out_rsts;
	s->tcp_v6_in_segs = v6.tcp_in_segs;
	s->tcp_v6_out_segs = v6.tcp_out_segs;
	s->tcp_v6_retrans_segs = v6.tcp_retrans_segs;

	if (is_offload(adap)) {
		s->frames = usm_stats.frames;
		s->octets = usm_stats.octets;
		s->drops = usm_stats.drops;
		s->rqe_dfr_mod = rdma_stats.rqe_dfr_mod;
		s->rqe_dfr_pkt = rdma_stats.rqe_dfr_pkt;
	}

	s->ofld_no_neigh = err_stats.ofld_no_neigh;
	s->ofld_cong_defer = err_stats.ofld_cong_defer;

	if (!is_t4(adap->params.chip)) {
		int v;

		v = t4_read_reg(adap, SGE_STAT_CFG_A);
		if (STATSOURCE_T5_G(v) == 7) {
			val2 = t4_read_reg(adap, SGE_STAT_MATCH_A);
			val1 = t4_read_reg(adap, SGE_STAT_TOTAL_A);
			s->wc_success = val1 - val2;
			s->wc_fail = val2;
		}
	}
}

static void collect_channel_stats(struct adapter *adap, struct channel_stats *s,
				  u8 i)
{
	struct tp_cpl_stats cpl_stats;
	struct tp_err_stats err_stats;
	struct tp_fcoe_stats fcoe_stats;

	memset(s, 0, sizeof(*s));

	spin_lock(&adap->stats_lock);
	t4_tp_get_cpl_stats(adap, &cpl_stats);
	t4_tp_get_err_stats(adap, &err_stats);
	t4_get_fcoe_stats(adap, i, &fcoe_stats);
	spin_unlock(&adap->stats_lock);

	s->cpl_req = cpl_stats.req[i];
	s->cpl_rsp = cpl_stats.rsp[i];
	s->mac_in_errs = err_stats.mac_in_errs[i];
	s->hdr_in_errs = err_stats.hdr_in_errs[i];
	s->tcp_in_errs = err_stats.tcp_in_errs[i];
	s->tcp6_in_errs = err_stats.tcp6_in_errs[i];
	s->tnl_cong_drops = err_stats.tnl_cong_drops[i];
	s->tnl_tx_drops = err_stats.tnl_tx_drops[i];
	s->ofld_vlan_drops = err_stats.ofld_vlan_drops[i];
	s->ofld_chan_drops = err_stats.ofld_chan_drops[i];
	s->octets_ddp = fcoe_stats.octets_ddp;
	s->frames_ddp = fcoe_stats.frames_ddp;
	s->frames_drop = fcoe_stats.frames_drop;
}

static void get_stats(struct net_device *dev, struct ethtool_stats *stats,
		      u64 *data)
{
	struct port_info *pi = netdev_priv(dev);
	struct adapter *adapter = pi->adapter;
	struct lb_port_stats s;
	int i;
	u64 *p0;

	t4_get_port_stats_offset(adapter, pi->tx_chan,
				 (struct port_stats *)data,
				 &pi->stats_base);

	data += sizeof(struct port_stats) / sizeof(u64);
	collect_sge_port_stats(adapter, pi, (struct queue_port_stats *)data);
	data += sizeof(struct queue_port_stats) / sizeof(u64);
	collect_adapter_stats(adapter, (struct adapter_stats *)data);
	data += sizeof(struct adapter_stats) / sizeof(u64);

	*data++ = (u64)pi->port_id;
	collect_channel_stats(adapter, (struct channel_stats *)data,
			      pi->port_id);
	data += sizeof(struct channel_stats) / sizeof(u64);

	*data++ = (u64)pi->port_id;
	memset(&s, 0, sizeof(s));
	t4_get_lb_stats(adapter, pi->port_id, &s);

	p0 = &s.octets;
	for (i = 0; i < ARRAY_SIZE(loopback_stats_strings) - 1; i++)
		*data++ = (unsigned long long)*p0++;
}

static void get_regs(struct net_device *dev, struct ethtool_regs *regs,
		     void *buf)
{
	struct adapter *adap = netdev2adap(dev);
	size_t buf_size;

	buf_size = t4_get_regs_len(adap);
	regs->version = mk_adap_vers(adap);
	t4_get_regs(adap, buf, buf_size);
}

static int restart_autoneg(struct net_device *dev)
{
	struct port_info *p = netdev_priv(dev);

	if (!netif_running(dev))
		return -EAGAIN;
	if (p->link_cfg.autoneg != AUTONEG_ENABLE)
		return -EINVAL;
	t4_restart_aneg(p->adapter, p->adapter->pf, p->tx_chan);
	return 0;
}

static int identify_port(struct net_device *dev,
			 enum ethtool_phys_id_state state)
{
	unsigned int val;
	struct adapter *adap = netdev2adap(dev);

	if (state == ETHTOOL_ID_ACTIVE)
		val = 0xffff;
	else if (state == ETHTOOL_ID_INACTIVE)
		val = 0;
	else
		return -EINVAL;

	return t4_identify_port(adap, adap->pf, netdev2pinfo(dev)->viid, val);
}

/**
 *	from_fw_port_mod_type - translate Firmware Port/Module type to Ethtool
 *	@port_type: Firmware Port Type
 *	@mod_type: Firmware Module Type
 *
 *	Translate Firmware Port/Module type to Ethtool Port Type.
 */
static int from_fw_port_mod_type(enum fw_port_type port_type,
				 enum fw_port_module_type mod_type)
{
	if (port_type == FW_PORT_TYPE_BT_SGMII ||
	    port_type == FW_PORT_TYPE_BT_XFI ||
	    port_type == FW_PORT_TYPE_BT_XAUI) {
		return PORT_TP;
	} else if (port_type == FW_PORT_TYPE_FIBER_XFI ||
		   port_type == FW_PORT_TYPE_FIBER_XAUI) {
		return PORT_FIBRE;
	} else if (port_type == FW_PORT_TYPE_SFP ||
		   port_type == FW_PORT_TYPE_QSFP_10G ||
		   port_type == FW_PORT_TYPE_QSA ||
		   port_type == FW_PORT_TYPE_QSFP) {
		if (mod_type == FW_PORT_MOD_TYPE_LR ||
		    mod_type == FW_PORT_MOD_TYPE_SR ||
		    mod_type == FW_PORT_MOD_TYPE_ER ||
		    mod_type == FW_PORT_MOD_TYPE_LRM)
			return PORT_FIBRE;
		else if (mod_type == FW_PORT_MOD_TYPE_TWINAX_PASSIVE ||
			 mod_type == FW_PORT_MOD_TYPE_TWINAX_ACTIVE)
			return PORT_DA;
		else
			return PORT_OTHER;
	}

	return PORT_OTHER;
}

/**
 *	speed_to_fw_caps - translate Port Speed to Firmware Port Capabilities
 *	@speed: speed in Kb/s
 *
 *	Translates a specific Port Speed into a Firmware Port Capabilities
 *	value.
 */
static unsigned int speed_to_fw_caps(int speed)
{
	if (speed == 100)
		return FW_PORT_CAP_SPEED_100M;
	if (speed == 1000)
		return FW_PORT_CAP_SPEED_1G;
	if (speed == 10000)
		return FW_PORT_CAP_SPEED_10G;
	if (speed == 25000)
		return FW_PORT_CAP_SPEED_25G;
	if (speed == 40000)
		return FW_PORT_CAP_SPEED_40G;
	if (speed == 100000)
		return FW_PORT_CAP_SPEED_100G;
	return 0;
}

/**
 *	fw_caps_to_lmm - translate Firmware to ethtool Link Mode Mask
 *	@port_type: Firmware Port Type
 *	@fw_caps: Firmware Port Capabilities
 *	@link_mode_mask: ethtool Link Mode Mask
 *
 *	Translate a Firmware Port Capabilities specification to an ethtool
 *	Link Mode Mask.
 */
static void fw_caps_to_lmm(enum fw_port_type port_type,
			   unsigned int fw_caps,
			   unsigned long *link_mode_mask)
{
	#define SET_LMM(__lmm_name) __set_bit(ETHTOOL_LINK_MODE_ ## __lmm_name \
					## _BIT, link_mode_mask)

	#define FW_CAPS_TO_LMM(__fw_name, __lmm_name) \
		do { \
			if (fw_caps & FW_PORT_CAP_ ## __fw_name) \
				SET_LMM(__lmm_name); \
		} while (0)

	switch (port_type) {
	case FW_PORT_TYPE_BT_SGMII:
	case FW_PORT_TYPE_BT_XFI:
	case FW_PORT_TYPE_BT_XAUI:
		SET_LMM(TP);
		FW_CAPS_TO_LMM(SPEED_100M, 100baseT_Full);
		FW_CAPS_TO_LMM(SPEED_1G, 1000baseT_Full);
		FW_CAPS_TO_LMM(SPEED_10G, 10000baseT_Full);
		break;

	case FW_PORT_TYPE_KX4:
	case FW_PORT_TYPE_KX:
		SET_LMM(Backplane);
		FW_CAPS_TO_LMM(SPEED_1G, 1000baseKX_Full);
		FW_CAPS_TO_LMM(SPEED_10G, 10000baseKX4_Full);
		break;

	case FW_PORT_TYPE_KR:
		SET_LMM(Backplane);
		SET_LMM(10000baseKR_Full);
		break;

	case FW_PORT_TYPE_BP_AP:
		SET_LMM(Backplane);
		SET_LMM(10000baseR_FEC);
		SET_LMM(10000baseKR_Full);
		SET_LMM(1000baseKX_Full);
		break;

	case FW_PORT_TYPE_BP4_AP:
		SET_LMM(Backplane);
		SET_LMM(10000baseR_FEC);
		SET_LMM(10000baseKR_Full);
		SET_LMM(1000baseKX_Full);
		SET_LMM(10000baseKX4_Full);
		break;

	case FW_PORT_TYPE_FIBER_XFI:
	case FW_PORT_TYPE_FIBER_XAUI:
	case FW_PORT_TYPE_SFP:
	case FW_PORT_TYPE_QSFP_10G:
	case FW_PORT_TYPE_QSA:
		SET_LMM(FIBRE);
		FW_CAPS_TO_LMM(SPEED_1G, 1000baseT_Full);
		FW_CAPS_TO_LMM(SPEED_10G, 10000baseT_Full);
		break;

	case FW_PORT_TYPE_BP40_BA:
	case FW_PORT_TYPE_QSFP:
		SET_LMM(FIBRE);
		SET_LMM(40000baseSR4_Full);
		break;

	case FW_PORT_TYPE_CR_QSFP:
	case FW_PORT_TYPE_SFP28:
		SET_LMM(FIBRE);
		SET_LMM(25000baseCR_Full);
		break;

	case FW_PORT_TYPE_KR4_100G:
	case FW_PORT_TYPE_CR4_QSFP:
		SET_LMM(FIBRE);
		SET_LMM(100000baseCR4_Full);
		break;

	default:
		break;
	}

	FW_CAPS_TO_LMM(ANEG, Autoneg);
	FW_CAPS_TO_LMM(802_3_PAUSE, Pause);
	FW_CAPS_TO_LMM(802_3_ASM_DIR, Asym_Pause);

	#undef FW_CAPS_TO_LMM
	#undef SET_LMM
}

/**
 *	lmm_to_fw_caps - translate ethtool Link Mode Mask to Firmware
 *	capabilities
 *
 *	@link_mode_mask: ethtool Link Mode Mask
 *
 *	Translate ethtool Link Mode Mask into a Firmware Port capabilities
 *	value.
 */
static unsigned int lmm_to_fw_caps(const unsigned long *link_mode_mask)
{
	unsigned int fw_caps = 0;

	#define LMM_TO_FW_CAPS(__lmm_name, __fw_name) \
		do { \
			if (test_bit(ETHTOOL_LINK_MODE_ ## __lmm_name ## _BIT, \
				     link_mode_mask)) \
				fw_caps |= FW_PORT_CAP_ ## __fw_name; \
		} while (0)

	LMM_TO_FW_CAPS(100baseT_Full, SPEED_100M);
	LMM_TO_FW_CAPS(1000baseT_Full, SPEED_1G);
	LMM_TO_FW_CAPS(10000baseT_Full, SPEED_10G);
	LMM_TO_FW_CAPS(40000baseSR4_Full, SPEED_40G);
	LMM_TO_FW_CAPS(25000baseCR_Full, SPEED_25G);
	LMM_TO_FW_CAPS(100000baseCR4_Full, SPEED_100G);

	#undef LMM_TO_FW_CAPS

	return fw_caps;
}

static int get_link_ksettings(struct net_device *dev,
			      struct ethtool_link_ksettings *link_ksettings)
{
	const struct port_info *pi = netdev_priv(dev);
	struct ethtool_link_settings *base = &link_ksettings->base;

	ethtool_link_ksettings_zero_link_mode(link_ksettings, supported);
	ethtool_link_ksettings_zero_link_mode(link_ksettings, advertising);
	ethtool_link_ksettings_zero_link_mode(link_ksettings, lp_advertising);

	base->port = from_fw_port_mod_type(pi->port_type, pi->mod_type);

	if (pi->mdio_addr >= 0) {
		base->phy_address = pi->mdio_addr;
		base->mdio_support = (pi->port_type == FW_PORT_TYPE_BT_SGMII
				      ? ETH_MDIO_SUPPORTS_C22
				      : ETH_MDIO_SUPPORTS_C45);
	} else {
		base->phy_address = 255;
		base->mdio_support = 0;
	}

	fw_caps_to_lmm(pi->port_type, pi->link_cfg.supported,
		       link_ksettings->link_modes.supported);
	fw_caps_to_lmm(pi->port_type, pi->link_cfg.advertising,
		       link_ksettings->link_modes.advertising);
	fw_caps_to_lmm(pi->port_type, pi->link_cfg.lp_advertising,
		       link_ksettings->link_modes.lp_advertising);

	if (netif_carrier_ok(dev)) {
		base->speed = pi->link_cfg.speed;
		base->duplex = DUPLEX_FULL;
	} else {
		base->speed = SPEED_UNKNOWN;
		base->duplex = DUPLEX_UNKNOWN;
	}

	base->autoneg = pi->link_cfg.autoneg;
	if (pi->link_cfg.supported & FW_PORT_CAP_ANEG)
		ethtool_link_ksettings_add_link_mode(link_ksettings,
						     supported, Autoneg);
	if (pi->link_cfg.autoneg)
		ethtool_link_ksettings_add_link_mode(link_ksettings,
						     advertising, Autoneg);

	return 0;
}

static int set_link_ksettings(struct net_device *dev,
			      const struct ethtool_link_ksettings
						*link_ksettings)
{
	struct port_info *pi = netdev_priv(dev);
	struct link_config *lc = &pi->link_cfg;
	const struct ethtool_link_settings *base = &link_ksettings->base;
	struct link_config old_lc;
	unsigned int fw_caps;
	int ret = 0;

	/* only full-duplex supported */
	if (base->duplex != DUPLEX_FULL)
		return -EINVAL;

	if (!(lc->supported & FW_PORT_CAP_ANEG)) {
		/* PHY offers a single speed.  See if that's what's
		 * being requested.
		 */
		if (base->autoneg == AUTONEG_DISABLE &&
		    (lc->supported & speed_to_fw_caps(base->speed)))
			return 0;
		return -EINVAL;
	}

	old_lc = *lc;
	if (base->autoneg == AUTONEG_DISABLE) {
		fw_caps = speed_to_fw_caps(base->speed);

		if (!(lc->supported & fw_caps))
			return -EINVAL;
		lc->requested_speed = fw_caps;
		lc->advertising = 0;
	} else {
		fw_caps =
			lmm_to_fw_caps(link_ksettings->link_modes.advertising);

		if (!(lc->supported & fw_caps))
			return -EINVAL;
		lc->requested_speed = 0;
		lc->advertising = fw_caps | FW_PORT_CAP_ANEG;
	}
	lc->autoneg = base->autoneg;

	/* If the firmware rejects the Link Configuration request, back out
	 * the changes and report the error.
	 */
	ret = t4_link_l1cfg(pi->adapter, pi->adapter->mbox, pi->tx_chan, lc);
	if (ret)
		*lc = old_lc;

	return ret;
}

static void get_pauseparam(struct net_device *dev,
			   struct ethtool_pauseparam *epause)
{
	struct port_info *p = netdev_priv(dev);

	epause->autoneg = (p->link_cfg.requested_fc & PAUSE_AUTONEG) != 0;
	epause->rx_pause = (p->link_cfg.fc & PAUSE_RX) != 0;
	epause->tx_pause = (p->link_cfg.fc & PAUSE_TX) != 0;
}

static int set_pauseparam(struct net_device *dev,
			  struct ethtool_pauseparam *epause)
{
	struct port_info *p = netdev_priv(dev);
	struct link_config *lc = &p->link_cfg;

	if (epause->autoneg == AUTONEG_DISABLE)
		lc->requested_fc = 0;
	else if (lc->supported & FW_PORT_CAP_ANEG)
		lc->requested_fc = PAUSE_AUTONEG;
	else
		return -EINVAL;

	if (epause->rx_pause)
		lc->requested_fc |= PAUSE_RX;
	if (epause->tx_pause)
		lc->requested_fc |= PAUSE_TX;
	if (netif_running(dev))
		return t4_link_l1cfg(p->adapter, p->adapter->mbox, p->tx_chan,
				     lc);
	return 0;
}

static void get_sge_param(struct net_device *dev, struct ethtool_ringparam *e)
{
	const struct port_info *pi = netdev_priv(dev);
	const struct sge *s = &pi->adapter->sge;

	e->rx_max_pending = MAX_RX_BUFFERS;
	e->rx_mini_max_pending = MAX_RSPQ_ENTRIES;
	e->rx_jumbo_max_pending = 0;
	e->tx_max_pending = MAX_TXQ_ENTRIES;

	e->rx_pending = s->ethrxq[pi->first_qset].fl.size - 8;
	e->rx_mini_pending = s->ethrxq[pi->first_qset].rspq.size;
	e->rx_jumbo_pending = 0;
	e->tx_pending = s->ethtxq[pi->first_qset].q.size;
}

static int set_sge_param(struct net_device *dev, struct ethtool_ringparam *e)
{
	int i;
	const struct port_info *pi = netdev_priv(dev);
	struct adapter *adapter = pi->adapter;
	struct sge *s = &adapter->sge;

	if (e->rx_pending > MAX_RX_BUFFERS || e->rx_jumbo_pending ||
	    e->tx_pending > MAX_TXQ_ENTRIES ||
	    e->rx_mini_pending > MAX_RSPQ_ENTRIES ||
	    e->rx_mini_pending < MIN_RSPQ_ENTRIES ||
	    e->rx_pending < MIN_FL_ENTRIES || e->tx_pending < MIN_TXQ_ENTRIES)
		return -EINVAL;

	if (adapter->flags & FULL_INIT_DONE)
		return -EBUSY;

	for (i = 0; i < pi->nqsets; ++i) {
		s->ethtxq[pi->first_qset + i].q.size = e->tx_pending;
		s->ethrxq[pi->first_qset + i].fl.size = e->rx_pending + 8;
		s->ethrxq[pi->first_qset + i].rspq.size = e->rx_mini_pending;
	}
	return 0;
}

/**
 * set_rx_intr_params - set a net devices's RX interrupt holdoff paramete!
 * @dev: the network device
 * @us: the hold-off time in us, or 0 to disable timer
 * @cnt: the hold-off packet count, or 0 to disable counter
 *
 * Set the RX interrupt hold-off parameters for a network device.
 */
static int set_rx_intr_params(struct net_device *dev,
			      unsigned int us, unsigned int cnt)
{
	int i, err;
	struct port_info *pi = netdev_priv(dev);
	struct adapter *adap = pi->adapter;
	struct sge_eth_rxq *q = &adap->sge.ethrxq[pi->first_qset];

	for (i = 0; i < pi->nqsets; i++, q++) {
		err = cxgb4_set_rspq_intr_params(&q->rspq, us, cnt);
		if (err)
			return err;
	}
	return 0;
}

static int set_adaptive_rx_setting(struct net_device *dev, int adaptive_rx)
{
	int i;
	struct port_info *pi = netdev_priv(dev);
	struct adapter *adap = pi->adapter;
	struct sge_eth_rxq *q = &adap->sge.ethrxq[pi->first_qset];

	for (i = 0; i < pi->nqsets; i++, q++)
		q->rspq.adaptive_rx = adaptive_rx;

	return 0;
}

static int get_adaptive_rx_setting(struct net_device *dev)
{
	struct port_info *pi = netdev_priv(dev);
	struct adapter *adap = pi->adapter;
	struct sge_eth_rxq *q = &adap->sge.ethrxq[pi->first_qset];

	return q->rspq.adaptive_rx;
}

static int set_coalesce(struct net_device *dev, struct ethtool_coalesce *c)
{
	set_adaptive_rx_setting(dev, c->use_adaptive_rx_coalesce);
	return set_rx_intr_params(dev, c->rx_coalesce_usecs,
				  c->rx_max_coalesced_frames);
}

static int get_coalesce(struct net_device *dev, struct ethtool_coalesce *c)
{
	const struct port_info *pi = netdev_priv(dev);
	const struct adapter *adap = pi->adapter;
	const struct sge_rspq *rq = &adap->sge.ethrxq[pi->first_qset].rspq;

	c->rx_coalesce_usecs = qtimer_val(adap, rq);
	c->rx_max_coalesced_frames = (rq->intr_params & QINTR_CNT_EN_F) ?
		adap->sge.counter_val[rq->pktcnt_idx] : 0;
	c->use_adaptive_rx_coalesce = get_adaptive_rx_setting(dev);
	return 0;
}

/**
 *	eeprom_ptov - translate a physical EEPROM address to virtual
 *	@phys_addr: the physical EEPROM address
 *	@fn: the PCI function number
 *	@sz: size of function-specific area
 *
 *	Translate a physical EEPROM address to virtual.  The first 1K is
 *	accessed through virtual addresses starting at 31K, the rest is
 *	accessed through virtual addresses starting at 0.
 *
 *	The mapping is as follows:
 *	[0..1K) -> [31K..32K)
 *	[1K..1K+A) -> [31K-A..31K)
 *	[1K+A..ES) -> [0..ES-A-1K)
 *
 *	where A = @fn * @sz, and ES = EEPROM size.
 */
static int eeprom_ptov(unsigned int phys_addr, unsigned int fn, unsigned int sz)
{
	fn *= sz;
	if (phys_addr < 1024)
		return phys_addr + (31 << 10);
	if (phys_addr < 1024 + fn)
		return 31744 - fn + phys_addr - 1024;
	if (phys_addr < EEPROMSIZE)
		return phys_addr - 1024 - fn;
	return -EINVAL;
}

/* The next two routines implement eeprom read/write from physical addresses.
 */
static int eeprom_rd_phys(struct adapter *adap, unsigned int phys_addr, u32 *v)
{
	int vaddr = eeprom_ptov(phys_addr, adap->pf, EEPROMPFSIZE);

	if (vaddr >= 0)
		vaddr = pci_read_vpd(adap->pdev, vaddr, sizeof(u32), v);
	return vaddr < 0 ? vaddr : 0;
}

static int eeprom_wr_phys(struct adapter *adap, unsigned int phys_addr, u32 v)
{
	int vaddr = eeprom_ptov(phys_addr, adap->pf, EEPROMPFSIZE);

	if (vaddr >= 0)
		vaddr = pci_write_vpd(adap->pdev, vaddr, sizeof(u32), &v);
	return vaddr < 0 ? vaddr : 0;
}

#define EEPROM_MAGIC 0x38E2F10C

static int get_eeprom(struct net_device *dev, struct ethtool_eeprom *e,
		      u8 *data)
{
	int i, err = 0;
	struct adapter *adapter = netdev2adap(dev);
	u8 *buf = kvzalloc(EEPROMSIZE, GFP_KERNEL);

	if (!buf)
		return -ENOMEM;

	e->magic = EEPROM_MAGIC;
	for (i = e->offset & ~3; !err && i < e->offset + e->len; i += 4)
		err = eeprom_rd_phys(adapter, i, (u32 *)&buf[i]);

	if (!err)
		memcpy(data, buf + e->offset, e->len);
	kvfree(buf);
	return err;
}

static int set_eeprom(struct net_device *dev, struct ethtool_eeprom *eeprom,
		      u8 *data)
{
	u8 *buf;
	int err = 0;
	u32 aligned_offset, aligned_len, *p;
	struct adapter *adapter = netdev2adap(dev);

	if (eeprom->magic != EEPROM_MAGIC)
		return -EINVAL;

	aligned_offset = eeprom->offset & ~3;
	aligned_len = (eeprom->len + (eeprom->offset & 3) + 3) & ~3;

	if (adapter->pf > 0) {
		u32 start = 1024 + adapter->pf * EEPROMPFSIZE;

		if (aligned_offset < start ||
		    aligned_offset + aligned_len > start + EEPROMPFSIZE)
			return -EPERM;
	}

	if (aligned_offset != eeprom->offset || aligned_len != eeprom->len) {
		/* RMW possibly needed for first or last words.
		 */
		buf = kvzalloc(aligned_len, GFP_KERNEL);
		if (!buf)
			return -ENOMEM;
		err = eeprom_rd_phys(adapter, aligned_offset, (u32 *)buf);
		if (!err && aligned_len > 4)
			err = eeprom_rd_phys(adapter,
					     aligned_offset + aligned_len - 4,
					     (u32 *)&buf[aligned_len - 4]);
		if (err)
			goto out;
		memcpy(buf + (eeprom->offset & 3), data, eeprom->len);
	} else {
		buf = data;
	}

	err = t4_seeprom_wp(adapter, false);
	if (err)
		goto out;

	for (p = (u32 *)buf; !err && aligned_len; aligned_len -= 4, p++) {
		err = eeprom_wr_phys(adapter, aligned_offset, *p);
		aligned_offset += 4;
	}

	if (!err)
		err = t4_seeprom_wp(adapter, true);
out:
	if (buf != data)
		kvfree(buf);
	return err;
}

static int set_flash(struct net_device *netdev, struct ethtool_flash *ef)
{
	int ret;
	const struct firmware *fw;
	struct adapter *adap = netdev2adap(netdev);
	unsigned int mbox = PCIE_FW_MASTER_M + 1;
	u32 pcie_fw;
	unsigned int master;
	u8 master_vld = 0;

	pcie_fw = t4_read_reg(adap, PCIE_FW_A);
	master = PCIE_FW_MASTER_G(pcie_fw);
	if (pcie_fw & PCIE_FW_MASTER_VLD_F)
		master_vld = 1;
	/* if csiostor is the master return */
	if (master_vld && (master != adap->pf)) {
		dev_warn(adap->pdev_dev,
			 "cxgb4 driver needs to be loaded as MASTER to support FW flash\n");
		return -EOPNOTSUPP;
	}

	ef->data[sizeof(ef->data) - 1] = '\0';
	ret = request_firmware(&fw, ef->data, adap->pdev_dev);
	if (ret < 0)
		return ret;

	/* If the adapter has been fully initialized then we'll go ahead and
	 * try to get the firmware's cooperation in upgrading to the new
	 * firmware image otherwise we'll try to do the entire job from the
	 * host ... and we always "force" the operation in this path.
	 */
	if (adap->flags & FULL_INIT_DONE)
		mbox = adap->mbox;

	ret = t4_fw_upgrade(adap, mbox, fw->data, fw->size, 1);
	release_firmware(fw);
	if (!ret)
		dev_info(adap->pdev_dev,
			 "loaded firmware %s, reload cxgb4 driver\n", ef->data);
	return ret;
}

static int get_ts_info(struct net_device *dev, struct ethtool_ts_info *ts_info)
{
	ts_info->so_timestamping = SOF_TIMESTAMPING_TX_SOFTWARE |
				   SOF_TIMESTAMPING_RX_SOFTWARE |
				   SOF_TIMESTAMPING_SOFTWARE;

	ts_info->so_timestamping |= SOF_TIMESTAMPING_RX_HARDWARE |
				    SOF_TIMESTAMPING_RAW_HARDWARE;

	ts_info->phc_index = -1;

	return 0;
}

static u32 get_rss_table_size(struct net_device *dev)
{
	const struct port_info *pi = netdev_priv(dev);

	return pi->rss_size;
}

static int get_rss_table(struct net_device *dev, u32 *p, u8 *key, u8 *hfunc)
{
	const struct port_info *pi = netdev_priv(dev);
	unsigned int n = pi->rss_size;

	if (hfunc)
		*hfunc = ETH_RSS_HASH_TOP;
	if (!p)
		return 0;
	while (n--)
		p[n] = pi->rss[n];
	return 0;
}

static int set_rss_table(struct net_device *dev, const u32 *p, const u8 *key,
			 const u8 hfunc)
{
	unsigned int i;
	struct port_info *pi = netdev_priv(dev);

	/* We require at least one supported parameter to be changed and no
	 * change in any of the unsupported parameters
	 */
	if (key ||
	    (hfunc != ETH_RSS_HASH_NO_CHANGE && hfunc != ETH_RSS_HASH_TOP))
		return -EOPNOTSUPP;
	if (!p)
		return 0;

	/* Interface must be brought up atleast once */
	if (pi->adapter->flags & FULL_INIT_DONE) {
		for (i = 0; i < pi->rss_size; i++)
			pi->rss[i] = p[i];

		return cxgb4_write_rss(pi, pi->rss);
	}

	return -EPERM;
}

static int get_rxnfc(struct net_device *dev, struct ethtool_rxnfc *info,
		     u32 *rules)
{
	const struct port_info *pi = netdev_priv(dev);

	switch (info->cmd) {
	case ETHTOOL_GRXFH: {
		unsigned int v = pi->rss_mode;

		info->data = 0;
		switch (info->flow_type) {
		case TCP_V4_FLOW:
			if (v & FW_RSS_VI_CONFIG_CMD_IP4FOURTUPEN_F)
				info->data = RXH_IP_SRC | RXH_IP_DST |
					     RXH_L4_B_0_1 | RXH_L4_B_2_3;
			else if (v & FW_RSS_VI_CONFIG_CMD_IP4TWOTUPEN_F)
				info->data = RXH_IP_SRC | RXH_IP_DST;
			break;
		case UDP_V4_FLOW:
			if ((v & FW_RSS_VI_CONFIG_CMD_IP4FOURTUPEN_F) &&
			    (v & FW_RSS_VI_CONFIG_CMD_UDPEN_F))
				info->data = RXH_IP_SRC | RXH_IP_DST |
					     RXH_L4_B_0_1 | RXH_L4_B_2_3;
			else if (v & FW_RSS_VI_CONFIG_CMD_IP4TWOTUPEN_F)
				info->data = RXH_IP_SRC | RXH_IP_DST;
			break;
		case SCTP_V4_FLOW:
		case AH_ESP_V4_FLOW:
		case IPV4_FLOW:
			if (v & FW_RSS_VI_CONFIG_CMD_IP4TWOTUPEN_F)
				info->data = RXH_IP_SRC | RXH_IP_DST;
			break;
		case TCP_V6_FLOW:
			if (v & FW_RSS_VI_CONFIG_CMD_IP6FOURTUPEN_F)
				info->data = RXH_IP_SRC | RXH_IP_DST |
					     RXH_L4_B_0_1 | RXH_L4_B_2_3;
			else if (v & FW_RSS_VI_CONFIG_CMD_IP6TWOTUPEN_F)
				info->data = RXH_IP_SRC | RXH_IP_DST;
			break;
		case UDP_V6_FLOW:
			if ((v & FW_RSS_VI_CONFIG_CMD_IP6FOURTUPEN_F) &&
			    (v & FW_RSS_VI_CONFIG_CMD_UDPEN_F))
				info->data = RXH_IP_SRC | RXH_IP_DST |
					     RXH_L4_B_0_1 | RXH_L4_B_2_3;
			else if (v & FW_RSS_VI_CONFIG_CMD_IP6TWOTUPEN_F)
				info->data = RXH_IP_SRC | RXH_IP_DST;
			break;
		case SCTP_V6_FLOW:
		case AH_ESP_V6_FLOW:
		case IPV6_FLOW:
			if (v & FW_RSS_VI_CONFIG_CMD_IP6TWOTUPEN_F)
				info->data = RXH_IP_SRC | RXH_IP_DST;
			break;
		}
		return 0;
	}
	case ETHTOOL_GRXRINGS:
		info->data = pi->nqsets;
		return 0;
	}
	return -EOPNOTSUPP;
}

static const struct ethtool_ops cxgb_ethtool_ops = {
	.get_link_ksettings = get_link_ksettings,
	.set_link_ksettings = set_link_ksettings,
	.get_drvinfo       = get_drvinfo,
	.get_msglevel      = get_msglevel,
	.set_msglevel      = set_msglevel,
	.get_ringparam     = get_sge_param,
	.set_ringparam     = set_sge_param,
	.get_coalesce      = get_coalesce,
	.set_coalesce      = set_coalesce,
	.get_eeprom_len    = get_eeprom_len,
	.get_eeprom        = get_eeprom,
	.set_eeprom        = set_eeprom,
	.get_pauseparam    = get_pauseparam,
	.set_pauseparam    = set_pauseparam,
	.get_link          = ethtool_op_get_link,
	.get_strings       = get_strings,
	.set_phys_id       = identify_port,
	.nway_reset        = restart_autoneg,
	.get_sset_count    = get_sset_count,
	.get_ethtool_stats = get_stats,
	.get_regs_len      = get_regs_len,
	.get_regs          = get_regs,
	.get_rxnfc         = get_rxnfc,
	.get_rxfh_indir_size = get_rss_table_size,
	.get_rxfh	   = get_rss_table,
	.set_rxfh	   = set_rss_table,
	.flash_device      = set_flash,
	.get_ts_info       = get_ts_info
};

void cxgb4_set_ethtool_ops(struct net_device *netdev)
{
	netdev->ethtool_ops = &cxgb_ethtool_ops;
}

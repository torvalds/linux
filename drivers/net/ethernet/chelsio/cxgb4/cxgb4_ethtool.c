// SPDX-License-Identifier: GPL-2.0-only
/*
 *  Copyright (C) 2013-2015 Chelsio Communications.  All rights reserved.
 */

#include <linux/firmware.h>
#include <linux/mdio.h>

#include "cxgb4.h"
#include "t4_regs.h"
#include "t4fw_api.h"
#include "cxgb4_cudbg.h"
#include "cxgb4_filter.h"
#include "cxgb4_tc_flower.h"

#define EEPROM_MAGIC 0x38E2F10C

static u32 get_msglevel(struct net_device *dev)
{
	return netdev2adap(dev)->msg_enable;
}

static void set_msglevel(struct net_device *dev, u32 val)
{
	netdev2adap(dev)->msg_enable = val;
}

enum cxgb4_ethtool_tests {
	CXGB4_ETHTOOL_LB_TEST,
	CXGB4_ETHTOOL_MAX_TEST,
};

static const char cxgb4_selftest_strings[CXGB4_ETHTOOL_MAX_TEST][ETH_GSTRING_LEN] = {
	"Loop back test (offline)",
};

static const char * const flash_region_strings[] = {
	"All",
	"Firmware",
	"PHY Firmware",
	"Boot",
	"Boot CFG",
};

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
	"uso                    ",
	"tx_csum_offload        ",
	"rx_csum_good           ",
	"vlan_extractions       ",
	"vlan_insertions        ",
	"gro_packets            ",
	"gro_merged             ",
#if  IS_ENABLED(CONFIG_CHELSIO_TLS_DEVICE)
	"tx_tls_encrypted_packets",
	"tx_tls_encrypted_bytes  ",
	"tx_tls_ctx              ",
	"tx_tls_ooo              ",
	"tx_tls_skip_no_sync_data",
	"tx_tls_drop_no_sync_data",
	"tx_tls_drop_bypass_req  ",
#endif
};

static char adapter_stats_strings[][ETH_GSTRING_LEN] = {
	"db_drop                ",
	"db_full                ",
	"db_empty               ",
	"write_coal_success     ",
	"write_coal_fail        ",
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

static const char cxgb4_priv_flags_strings[][ETH_GSTRING_LEN] = {
	[PRIV_FLAG_PORT_TX_VM_BIT] = "port_tx_vm_wr",
};

static int get_sset_count(struct net_device *dev, int sset)
{
	switch (sset) {
	case ETH_SS_STATS:
		return ARRAY_SIZE(stats_strings) +
		       ARRAY_SIZE(adapter_stats_strings) +
		       ARRAY_SIZE(loopback_stats_strings);
	case ETH_SS_PRIV_FLAGS:
		return ARRAY_SIZE(cxgb4_priv_flags_strings);
	case ETH_SS_TEST:
		return ARRAY_SIZE(cxgb4_selftest_strings);
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
	strlcpy(info->bus_info, pci_name(adapter->pdev),
		sizeof(info->bus_info));
	info->regdump_len = get_regs_len(dev);

	if (adapter->params.fw_vers)
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
	info->n_priv_flags = ARRAY_SIZE(cxgb4_priv_flags_strings);
}

static void get_strings(struct net_device *dev, u32 stringset, u8 *data)
{
	if (stringset == ETH_SS_STATS) {
		memcpy(data, stats_strings, sizeof(stats_strings));
		data += sizeof(stats_strings);
		memcpy(data, adapter_stats_strings,
		       sizeof(adapter_stats_strings));
		data += sizeof(adapter_stats_strings);
		memcpy(data, loopback_stats_strings,
		       sizeof(loopback_stats_strings));
	} else if (stringset == ETH_SS_PRIV_FLAGS) {
		memcpy(data, cxgb4_priv_flags_strings,
		       sizeof(cxgb4_priv_flags_strings));
	} else if (stringset == ETH_SS_TEST) {
		memcpy(data, cxgb4_selftest_strings,
		       sizeof(cxgb4_selftest_strings));
	}
}

/* port stats maintained per queue of the port. They should be in the same
 * order as in stats_strings above.
 */
struct queue_port_stats {
	u64 tso;
	u64 uso;
	u64 tx_csum;
	u64 rx_csum;
	u64 vlan_ex;
	u64 vlan_ins;
	u64 gro_pkts;
	u64 gro_merged;
#if IS_ENABLED(CONFIG_CHELSIO_TLS_DEVICE)
	u64 tx_tls_encrypted_packets;
	u64 tx_tls_encrypted_bytes;
	u64 tx_tls_ctx;
	u64 tx_tls_ooo;
	u64 tx_tls_skip_no_sync_data;
	u64 tx_tls_drop_no_sync_data;
	u64 tx_tls_drop_bypass_req;
#endif
};

struct adapter_stats {
	u64 db_drop;
	u64 db_full;
	u64 db_empty;
	u64 wc_success;
	u64 wc_fail;
};

static void collect_sge_port_stats(const struct adapter *adap,
				   const struct port_info *p,
				   struct queue_port_stats *s)
{
	const struct sge_eth_txq *tx = &adap->sge.ethtxq[p->first_qset];
	const struct sge_eth_rxq *rx = &adap->sge.ethrxq[p->first_qset];
#if IS_ENABLED(CONFIG_CHELSIO_TLS_DEVICE)
	const struct ch_ktls_port_stats_debug *ktls_stats;
#endif
	struct sge_eohw_txq *eohw_tx;
	unsigned int i;

	memset(s, 0, sizeof(*s));
	for (i = 0; i < p->nqsets; i++, rx++, tx++) {
		s->tso += tx->tso;
		s->uso += tx->uso;
		s->tx_csum += tx->tx_cso;
		s->rx_csum += rx->stats.rx_cso;
		s->vlan_ex += rx->stats.vlan_ex;
		s->vlan_ins += tx->vlan_ins;
		s->gro_pkts += rx->stats.lro_pkts;
		s->gro_merged += rx->stats.lro_merged;
	}

	if (adap->sge.eohw_txq) {
		eohw_tx = &adap->sge.eohw_txq[p->first_qset];
		for (i = 0; i < p->nqsets; i++, eohw_tx++) {
			s->tso += eohw_tx->tso;
			s->uso += eohw_tx->uso;
			s->tx_csum += eohw_tx->tx_cso;
			s->vlan_ins += eohw_tx->vlan_ins;
		}
	}
#if IS_ENABLED(CONFIG_CHELSIO_TLS_DEVICE)
	ktls_stats = &adap->ch_ktls_stats.ktls_port[p->port_id];
	s->tx_tls_encrypted_packets =
		atomic64_read(&ktls_stats->ktls_tx_encrypted_packets);
	s->tx_tls_encrypted_bytes =
		atomic64_read(&ktls_stats->ktls_tx_encrypted_bytes);
	s->tx_tls_ctx = atomic64_read(&ktls_stats->ktls_tx_ctx);
	s->tx_tls_ooo = atomic64_read(&ktls_stats->ktls_tx_ooo);
	s->tx_tls_skip_no_sync_data =
		atomic64_read(&ktls_stats->ktls_tx_skip_no_sync_data);
	s->tx_tls_drop_no_sync_data =
		atomic64_read(&ktls_stats->ktls_tx_drop_no_sync_data);
	s->tx_tls_drop_bypass_req =
		atomic64_read(&ktls_stats->ktls_tx_drop_bypass_req);
#endif
}

static void collect_adapter_stats(struct adapter *adap, struct adapter_stats *s)
{
	u64 val1, val2;

	memset(s, 0, sizeof(*s));

	s->db_drop = adap->db_stats.db_drop;
	s->db_full = adap->db_stats.db_full;
	s->db_empty = adap->db_stats.db_empty;

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
		   port_type == FW_PORT_TYPE_QSFP ||
		   port_type == FW_PORT_TYPE_CR4_QSFP ||
		   port_type == FW_PORT_TYPE_CR_QSFP ||
		   port_type == FW_PORT_TYPE_CR2_QSFP ||
		   port_type == FW_PORT_TYPE_SFP28) {
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
	} else if (port_type == FW_PORT_TYPE_KR4_100G ||
		   port_type == FW_PORT_TYPE_KR_SFP28 ||
		   port_type == FW_PORT_TYPE_KR_XLAUI) {
		return PORT_NONE;
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
		return FW_PORT_CAP32_SPEED_100M;
	if (speed == 1000)
		return FW_PORT_CAP32_SPEED_1G;
	if (speed == 10000)
		return FW_PORT_CAP32_SPEED_10G;
	if (speed == 25000)
		return FW_PORT_CAP32_SPEED_25G;
	if (speed == 40000)
		return FW_PORT_CAP32_SPEED_40G;
	if (speed == 50000)
		return FW_PORT_CAP32_SPEED_50G;
	if (speed == 100000)
		return FW_PORT_CAP32_SPEED_100G;
	if (speed == 200000)
		return FW_PORT_CAP32_SPEED_200G;
	if (speed == 400000)
		return FW_PORT_CAP32_SPEED_400G;
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
			   fw_port_cap32_t fw_caps,
			   unsigned long *link_mode_mask)
{
	#define SET_LMM(__lmm_name) \
		do { \
			__set_bit(ETHTOOL_LINK_MODE_ ## __lmm_name ## _BIT, \
				  link_mode_mask); \
		} while (0)

	#define FW_CAPS_TO_LMM(__fw_name, __lmm_name) \
		do { \
			if (fw_caps & FW_PORT_CAP32_ ## __fw_name) \
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
		FW_CAPS_TO_LMM(SPEED_10G, 10000baseKR_Full);
		break;

	case FW_PORT_TYPE_BP_AP:
		SET_LMM(Backplane);
		FW_CAPS_TO_LMM(SPEED_1G, 1000baseKX_Full);
		FW_CAPS_TO_LMM(SPEED_10G, 10000baseR_FEC);
		FW_CAPS_TO_LMM(SPEED_10G, 10000baseKR_Full);
		break;

	case FW_PORT_TYPE_BP4_AP:
		SET_LMM(Backplane);
		FW_CAPS_TO_LMM(SPEED_1G, 1000baseKX_Full);
		FW_CAPS_TO_LMM(SPEED_10G, 10000baseR_FEC);
		FW_CAPS_TO_LMM(SPEED_10G, 10000baseKR_Full);
		FW_CAPS_TO_LMM(SPEED_10G, 10000baseKX4_Full);
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
		FW_CAPS_TO_LMM(SPEED_1G, 1000baseT_Full);
		FW_CAPS_TO_LMM(SPEED_10G, 10000baseT_Full);
		FW_CAPS_TO_LMM(SPEED_40G, 40000baseSR4_Full);
		break;

	case FW_PORT_TYPE_CR_QSFP:
	case FW_PORT_TYPE_SFP28:
		SET_LMM(FIBRE);
		FW_CAPS_TO_LMM(SPEED_1G, 1000baseT_Full);
		FW_CAPS_TO_LMM(SPEED_10G, 10000baseT_Full);
		FW_CAPS_TO_LMM(SPEED_25G, 25000baseCR_Full);
		break;

	case FW_PORT_TYPE_KR_SFP28:
		SET_LMM(Backplane);
		FW_CAPS_TO_LMM(SPEED_1G, 1000baseT_Full);
		FW_CAPS_TO_LMM(SPEED_10G, 10000baseKR_Full);
		FW_CAPS_TO_LMM(SPEED_25G, 25000baseKR_Full);
		break;

	case FW_PORT_TYPE_KR_XLAUI:
		SET_LMM(Backplane);
		FW_CAPS_TO_LMM(SPEED_1G, 1000baseKX_Full);
		FW_CAPS_TO_LMM(SPEED_10G, 10000baseKR_Full);
		FW_CAPS_TO_LMM(SPEED_40G, 40000baseKR4_Full);
		break;

	case FW_PORT_TYPE_CR2_QSFP:
		SET_LMM(FIBRE);
		FW_CAPS_TO_LMM(SPEED_50G, 50000baseSR2_Full);
		break;

	case FW_PORT_TYPE_KR4_100G:
	case FW_PORT_TYPE_CR4_QSFP:
		SET_LMM(FIBRE);
		FW_CAPS_TO_LMM(SPEED_1G,  1000baseT_Full);
		FW_CAPS_TO_LMM(SPEED_10G, 10000baseKR_Full);
		FW_CAPS_TO_LMM(SPEED_40G, 40000baseSR4_Full);
		FW_CAPS_TO_LMM(SPEED_25G, 25000baseCR_Full);
		FW_CAPS_TO_LMM(SPEED_50G, 50000baseCR2_Full);
		FW_CAPS_TO_LMM(SPEED_100G, 100000baseCR4_Full);
		break;

	default:
		break;
	}

	if (fw_caps & FW_PORT_CAP32_FEC_V(FW_PORT_CAP32_FEC_M)) {
		FW_CAPS_TO_LMM(FEC_RS, FEC_RS);
		FW_CAPS_TO_LMM(FEC_BASER_RS, FEC_BASER);
	} else {
		SET_LMM(FEC_NONE);
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
				fw_caps |= FW_PORT_CAP32_ ## __fw_name; \
		} while (0)

	LMM_TO_FW_CAPS(100baseT_Full, SPEED_100M);
	LMM_TO_FW_CAPS(1000baseT_Full, SPEED_1G);
	LMM_TO_FW_CAPS(10000baseT_Full, SPEED_10G);
	LMM_TO_FW_CAPS(40000baseSR4_Full, SPEED_40G);
	LMM_TO_FW_CAPS(25000baseCR_Full, SPEED_25G);
	LMM_TO_FW_CAPS(50000baseCR2_Full, SPEED_50G);
	LMM_TO_FW_CAPS(100000baseCR4_Full, SPEED_100G);

	#undef LMM_TO_FW_CAPS

	return fw_caps;
}

static int get_link_ksettings(struct net_device *dev,
			      struct ethtool_link_ksettings *link_ksettings)
{
	struct port_info *pi = netdev_priv(dev);
	struct ethtool_link_settings *base = &link_ksettings->base;

	/* For the nonce, the Firmware doesn't send up Port State changes
	 * when the Virtual Interface attached to the Port is down.  So
	 * if it's down, let's grab any changes.
	 */
	if (!netif_running(dev))
		(void)t4_update_port_info(pi);

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

	fw_caps_to_lmm(pi->port_type, pi->link_cfg.pcaps,
		       link_ksettings->link_modes.supported);
	fw_caps_to_lmm(pi->port_type,
		       t4_link_acaps(pi->adapter,
				     pi->lport,
				     &pi->link_cfg),
		       link_ksettings->link_modes.advertising);
	fw_caps_to_lmm(pi->port_type, pi->link_cfg.lpacaps,
		       link_ksettings->link_modes.lp_advertising);

	base->speed = (netif_carrier_ok(dev)
		       ? pi->link_cfg.speed
		       : SPEED_UNKNOWN);
	base->duplex = DUPLEX_FULL;

	base->autoneg = pi->link_cfg.autoneg;
	if (pi->link_cfg.pcaps & FW_PORT_CAP32_ANEG)
		ethtool_link_ksettings_add_link_mode(link_ksettings,
						     supported, Autoneg);
	if (pi->link_cfg.autoneg)
		ethtool_link_ksettings_add_link_mode(link_ksettings,
						     advertising, Autoneg);

	return 0;
}

static int set_link_ksettings(struct net_device *dev,
			    const struct ethtool_link_ksettings *link_ksettings)
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

	old_lc = *lc;
	if (!(lc->pcaps & FW_PORT_CAP32_ANEG) ||
	    base->autoneg == AUTONEG_DISABLE) {
		fw_caps = speed_to_fw_caps(base->speed);

		/* Speed must be supported by Physical Port Capabilities. */
		if (!(lc->pcaps & fw_caps))
			return -EINVAL;

		lc->speed_caps = fw_caps;
		lc->acaps = fw_caps;
	} else {
		fw_caps =
			lmm_to_fw_caps(link_ksettings->link_modes.advertising);
		if (!(lc->pcaps & fw_caps))
			return -EINVAL;
		lc->speed_caps = 0;
		lc->acaps = fw_caps | FW_PORT_CAP32_ANEG;
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

/* Translate the Firmware FEC value into the ethtool value. */
static inline unsigned int fwcap_to_eth_fec(unsigned int fw_fec)
{
	unsigned int eth_fec = 0;

	if (fw_fec & FW_PORT_CAP32_FEC_RS)
		eth_fec |= ETHTOOL_FEC_RS;
	if (fw_fec & FW_PORT_CAP32_FEC_BASER_RS)
		eth_fec |= ETHTOOL_FEC_BASER;

	/* if nothing is set, then FEC is off */
	if (!eth_fec)
		eth_fec = ETHTOOL_FEC_OFF;

	return eth_fec;
}

/* Translate Common Code FEC value into ethtool value. */
static inline unsigned int cc_to_eth_fec(unsigned int cc_fec)
{
	unsigned int eth_fec = 0;

	if (cc_fec & FEC_AUTO)
		eth_fec |= ETHTOOL_FEC_AUTO;
	if (cc_fec & FEC_RS)
		eth_fec |= ETHTOOL_FEC_RS;
	if (cc_fec & FEC_BASER_RS)
		eth_fec |= ETHTOOL_FEC_BASER;

	/* if nothing is set, then FEC is off */
	if (!eth_fec)
		eth_fec = ETHTOOL_FEC_OFF;

	return eth_fec;
}

/* Translate ethtool FEC value into Common Code value. */
static inline unsigned int eth_to_cc_fec(unsigned int eth_fec)
{
	unsigned int cc_fec = 0;

	if (eth_fec & ETHTOOL_FEC_OFF)
		return cc_fec;

	if (eth_fec & ETHTOOL_FEC_AUTO)
		cc_fec |= FEC_AUTO;
	if (eth_fec & ETHTOOL_FEC_RS)
		cc_fec |= FEC_RS;
	if (eth_fec & ETHTOOL_FEC_BASER)
		cc_fec |= FEC_BASER_RS;

	return cc_fec;
}

static int get_fecparam(struct net_device *dev, struct ethtool_fecparam *fec)
{
	const struct port_info *pi = netdev_priv(dev);
	const struct link_config *lc = &pi->link_cfg;

	/* Translate the Firmware FEC Support into the ethtool value.  We
	 * always support IEEE 802.3 "automatic" selection of Link FEC type if
	 * any FEC is supported.
	 */
	fec->fec = fwcap_to_eth_fec(lc->pcaps);
	if (fec->fec != ETHTOOL_FEC_OFF)
		fec->fec |= ETHTOOL_FEC_AUTO;

	/* Translate the current internal FEC parameters into the
	 * ethtool values.
	 */
	fec->active_fec = cc_to_eth_fec(lc->fec);

	return 0;
}

static int set_fecparam(struct net_device *dev, struct ethtool_fecparam *fec)
{
	struct port_info *pi = netdev_priv(dev);
	struct link_config *lc = &pi->link_cfg;
	struct link_config old_lc;
	int ret;

	/* Save old Link Configuration in case the L1 Configure below
	 * fails.
	 */
	old_lc = *lc;

	/* Try to perform the L1 Configure and return the result of that
	 * effort.  If it fails, revert the attempted change.
	 */
	lc->requested_fec = eth_to_cc_fec(fec->fec);
	ret = t4_link_l1cfg(pi->adapter, pi->adapter->mbox,
			    pi->tx_chan, lc);
	if (ret)
		*lc = old_lc;
	return ret;
}

static void get_pauseparam(struct net_device *dev,
			   struct ethtool_pauseparam *epause)
{
	struct port_info *p = netdev_priv(dev);

	epause->autoneg = (p->link_cfg.requested_fc & PAUSE_AUTONEG) != 0;
	epause->rx_pause = (p->link_cfg.advertised_fc & PAUSE_RX) != 0;
	epause->tx_pause = (p->link_cfg.advertised_fc & PAUSE_TX) != 0;
}

static int set_pauseparam(struct net_device *dev,
			  struct ethtool_pauseparam *epause)
{
	struct port_info *p = netdev_priv(dev);
	struct link_config *lc = &p->link_cfg;

	if (epause->autoneg == AUTONEG_DISABLE)
		lc->requested_fc = 0;
	else if (lc->pcaps & FW_PORT_CAP32_ANEG)
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

static void get_sge_param(struct net_device *dev, struct ethtool_ringparam *e,
			  struct kernel_ethtool_ringparam *kernel_e,
			  struct netlink_ext_ack *extack)
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

static int set_sge_param(struct net_device *dev, struct ethtool_ringparam *e,
			 struct kernel_ethtool_ringparam *kernel_e,
			 struct netlink_ext_ack *extack)
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

	if (adapter->flags & CXGB4_FULL_INIT_DONE)
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

/* Return the current global Adapter SGE Doorbell Queue Timer Tick for all
 * Ethernet TX Queues.
 */
static int get_dbqtimer_tick(struct net_device *dev)
{
	struct port_info *pi = netdev_priv(dev);
	struct adapter *adap = pi->adapter;

	if (!(adap->flags & CXGB4_SGE_DBQ_TIMER))
		return 0;

	return adap->sge.dbqtimer_tick;
}

/* Return the SGE Doorbell Queue Timer Value for the Ethernet TX Queues
 * associated with a Network Device.
 */
static int get_dbqtimer(struct net_device *dev)
{
	struct port_info *pi = netdev_priv(dev);
	struct adapter *adap = pi->adapter;
	struct sge_eth_txq *txq;

	txq = &adap->sge.ethtxq[pi->first_qset];

	if (!(adap->flags & CXGB4_SGE_DBQ_TIMER))
		return 0;

	/* all of the TX Queues use the same Timer Index */
	return adap->sge.dbqtimer_val[txq->dbqtimerix];
}

/* Set the global Adapter SGE Doorbell Queue Timer Tick for all Ethernet TX
 * Queues.  This is the fundamental "Tick" that sets the scale of values which
 * can be used.  Individual Ethernet TX Queues index into a relatively small
 * array of Tick Multipliers.  Changing the base Tick will thus change all of
 * the resulting Timer Values associated with those multipliers for all
 * Ethernet TX Queues.
 */
static int set_dbqtimer_tick(struct net_device *dev, int usecs)
{
	struct port_info *pi = netdev_priv(dev);
	struct adapter *adap = pi->adapter;
	struct sge *s = &adap->sge;
	u32 param, val;
	int ret;

	if (!(adap->flags & CXGB4_SGE_DBQ_TIMER))
		return 0;

	/* return early if it's the same Timer Tick we're already using */
	if (s->dbqtimer_tick == usecs)
		return 0;

	/* attempt to set the new Timer Tick value */
	param = (FW_PARAMS_MNEM_V(FW_PARAMS_MNEM_DEV) |
		 FW_PARAMS_PARAM_X_V(FW_PARAMS_PARAM_DEV_DBQ_TIMERTICK));
	val = usecs;
	ret = t4_set_params(adap, adap->mbox, adap->pf, 0, 1, &param, &val);
	if (ret)
		return ret;
	s->dbqtimer_tick = usecs;

	/* if successful, reread resulting dependent Timer values */
	ret = t4_read_sge_dbqtimers(adap, ARRAY_SIZE(s->dbqtimer_val),
				    s->dbqtimer_val);
	return ret;
}

/* Set the SGE Doorbell Queue Timer Value for the Ethernet TX Queues
 * associated with a Network Device.  There is a relatively small array of
 * possible Timer Values so we need to pick the closest value available.
 */
static int set_dbqtimer(struct net_device *dev, int usecs)
{
	int qix, timerix, min_timerix, delta, min_delta;
	struct port_info *pi = netdev_priv(dev);
	struct adapter *adap = pi->adapter;
	struct sge *s = &adap->sge;
	struct sge_eth_txq *txq;
	u32 param, val;
	int ret;

	if (!(adap->flags & CXGB4_SGE_DBQ_TIMER))
		return 0;

	/* Find the SGE Doorbell Timer Value that's closest to the requested
	 * value.
	 */
	min_delta = INT_MAX;
	min_timerix = 0;
	for (timerix = 0; timerix < ARRAY_SIZE(s->dbqtimer_val); timerix++) {
		delta = s->dbqtimer_val[timerix] - usecs;
		if (delta < 0)
			delta = -delta;
		if (delta < min_delta) {
			min_delta = delta;
			min_timerix = timerix;
		}
	}

	/* Return early if it's the same Timer Index we're already using.
	 * We use the same Timer Index for all of the TX Queues for an
	 * interface so it's only necessary to check the first one.
	 */
	txq = &s->ethtxq[pi->first_qset];
	if (txq->dbqtimerix == min_timerix)
		return 0;

	for (qix = 0; qix < pi->nqsets; qix++, txq++) {
		if (adap->flags & CXGB4_FULL_INIT_DONE) {
			param =
			 (FW_PARAMS_MNEM_V(FW_PARAMS_MNEM_DMAQ) |
			  FW_PARAMS_PARAM_X_V(FW_PARAMS_PARAM_DMAQ_EQ_TIMERIX) |
			  FW_PARAMS_PARAM_YZ_V(txq->q.cntxt_id));
			val = min_timerix;
			ret = t4_set_params(adap, adap->mbox, adap->pf, 0,
					    1, &param, &val);
			if (ret)
				return ret;
		}
		txq->dbqtimerix = min_timerix;
	}
	return 0;
}

/* Set the global Adapter SGE Doorbell Queue Timer Tick for all Ethernet TX
 * Queues and the Timer Value for the Ethernet TX Queues associated with a
 * Network Device.  Since changing the global Tick changes all of the
 * available Timer Values, we need to do this first before selecting the
 * resulting closest Timer Value.  Moreover, since the Tick is global,
 * changing it affects the Timer Values for all Network Devices on the
 * adapter.  So, before changing the Tick, we grab all of the current Timer
 * Values for other Network Devices on this Adapter and then attempt to select
 * new Timer Values which are close to the old values ...
 */
static int set_dbqtimer_tickval(struct net_device *dev,
				int tick_usecs, int timer_usecs)
{
	struct port_info *pi = netdev_priv(dev);
	struct adapter *adap = pi->adapter;
	int timer[MAX_NPORTS];
	unsigned int port;
	int ret;

	/* Grab the other adapter Network Interface current timers and fill in
	 * the new one for this Network Interface.
	 */
	for_each_port(adap, port)
		if (port == pi->port_id)
			timer[port] = timer_usecs;
		else
			timer[port] = get_dbqtimer(adap->port[port]);

	/* Change the global Tick first ... */
	ret = set_dbqtimer_tick(dev, tick_usecs);
	if (ret)
		return ret;

	/* ... and then set all of the Network Interface Timer Values ... */
	for_each_port(adap, port) {
		ret = set_dbqtimer(adap->port[port], timer[port]);
		if (ret)
			return ret;
	}

	return 0;
}

static int set_coalesce(struct net_device *dev,
			struct ethtool_coalesce *coalesce,
			struct kernel_ethtool_coalesce *kernel_coal,
			struct netlink_ext_ack *extack)
{
	int ret;

	set_adaptive_rx_setting(dev, coalesce->use_adaptive_rx_coalesce);

	ret = set_rx_intr_params(dev, coalesce->rx_coalesce_usecs,
				 coalesce->rx_max_coalesced_frames);
	if (ret)
		return ret;

	return set_dbqtimer_tickval(dev,
				    coalesce->tx_coalesce_usecs_irq,
				    coalesce->tx_coalesce_usecs);
}

static int get_coalesce(struct net_device *dev, struct ethtool_coalesce *c,
			struct kernel_ethtool_coalesce *kernel_coal,
			struct netlink_ext_ack *extack)
{
	const struct port_info *pi = netdev_priv(dev);
	const struct adapter *adap = pi->adapter;
	const struct sge_rspq *rq = &adap->sge.ethrxq[pi->first_qset].rspq;

	c->rx_coalesce_usecs = qtimer_val(adap, rq);
	c->rx_max_coalesced_frames = (rq->intr_params & QINTR_CNT_EN_F) ?
		adap->sge.counter_val[rq->pktcnt_idx] : 0;
	c->use_adaptive_rx_coalesce = get_adaptive_rx_setting(dev);
	c->tx_coalesce_usecs_irq = get_dbqtimer_tick(dev);
	c->tx_coalesce_usecs = get_dbqtimer(dev);
	return 0;
}

/* The next two routines implement eeprom read/write from physical addresses.
 */
static int eeprom_rd_phys(struct adapter *adap, unsigned int phys_addr, u32 *v)
{
	int vaddr = t4_eeprom_ptov(phys_addr, adap->pf, EEPROMPFSIZE);

	if (vaddr >= 0)
		vaddr = pci_read_vpd(adap->pdev, vaddr, sizeof(u32), v);
	return vaddr < 0 ? vaddr : 0;
}

static int eeprom_wr_phys(struct adapter *adap, unsigned int phys_addr, u32 v)
{
	int vaddr = t4_eeprom_ptov(phys_addr, adap->pf, EEPROMPFSIZE);

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

static int cxgb4_ethtool_flash_bootcfg(struct net_device *netdev,
				       const u8 *data, u32 size)
{
	struct adapter *adap = netdev2adap(netdev);
	int ret;

	ret = t4_load_bootcfg(adap, data, size);
	if (ret)
		dev_err(adap->pdev_dev, "Failed to load boot cfg image\n");

	return ret;
}

static int cxgb4_ethtool_flash_boot(struct net_device *netdev,
				    const u8 *bdata, u32 size)
{
	struct adapter *adap = netdev2adap(netdev);
	unsigned int offset;
	u8 *data;
	int ret;

	data = kmemdup(bdata, size, GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	offset = OFFSET_G(t4_read_reg(adap, PF_REG(0, PCIE_PF_EXPROM_OFST_A)));

	ret = t4_load_boot(adap, data, offset, size);
	if (ret)
		dev_err(adap->pdev_dev, "Failed to load boot image\n");

	kfree(data);
	return ret;
}

#define CXGB4_PHY_SIG 0x130000ea

static int cxgb4_validate_phy_image(const u8 *data, u32 *size)
{
	struct cxgb4_fw_data *header;

	header = (struct cxgb4_fw_data *)data;
	if (be32_to_cpu(header->signature) != CXGB4_PHY_SIG)
		return -EINVAL;

	return 0;
}

static int cxgb4_ethtool_flash_phy(struct net_device *netdev,
				   const u8 *data, u32 size)
{
	struct adapter *adap = netdev2adap(netdev);
	int ret;

	ret = cxgb4_validate_phy_image(data, NULL);
	if (ret) {
		dev_err(adap->pdev_dev, "PHY signature mismatch\n");
		return ret;
	}

	/* We have to RESET the chip/firmware because we need the
	 * chip in uninitialized state for loading new PHY image.
	 * Otherwise, the running firmware will only store the PHY
	 * image in local RAM which will be lost after next reset.
	 */
	ret = t4_fw_reset(adap, adap->mbox, PIORSTMODE_F | PIORST_F);
	if (ret < 0) {
		dev_err(adap->pdev_dev,
			"Set FW to RESET for flashing PHY FW failed. ret: %d\n",
			ret);
		return ret;
	}

	ret = t4_load_phy_fw(adap, MEMWIN_NIC, NULL, data, size);
	if (ret < 0) {
		dev_err(adap->pdev_dev, "Failed to load PHY FW. ret: %d\n",
			ret);
		return ret;
	}

	return 0;
}

static int cxgb4_ethtool_flash_fw(struct net_device *netdev,
				  const u8 *data, u32 size)
{
	struct adapter *adap = netdev2adap(netdev);
	unsigned int mbox = PCIE_FW_MASTER_M + 1;
	int ret;

	/* If the adapter has been fully initialized then we'll go ahead and
	 * try to get the firmware's cooperation in upgrading to the new
	 * firmware image otherwise we'll try to do the entire job from the
	 * host ... and we always "force" the operation in this path.
	 */
	if (adap->flags & CXGB4_FULL_INIT_DONE)
		mbox = adap->mbox;

	ret = t4_fw_upgrade(adap, mbox, data, size, 1);
	if (ret)
		dev_err(adap->pdev_dev,
			"Failed to flash firmware\n");

	return ret;
}

static int cxgb4_ethtool_flash_region(struct net_device *netdev,
				      const u8 *data, u32 size, u32 region)
{
	struct adapter *adap = netdev2adap(netdev);
	int ret;

	switch (region) {
	case CXGB4_ETHTOOL_FLASH_FW:
		ret = cxgb4_ethtool_flash_fw(netdev, data, size);
		break;
	case CXGB4_ETHTOOL_FLASH_PHY:
		ret = cxgb4_ethtool_flash_phy(netdev, data, size);
		break;
	case CXGB4_ETHTOOL_FLASH_BOOT:
		ret = cxgb4_ethtool_flash_boot(netdev, data, size);
		break;
	case CXGB4_ETHTOOL_FLASH_BOOTCFG:
		ret = cxgb4_ethtool_flash_bootcfg(netdev, data, size);
		break;
	default:
		ret = -EOPNOTSUPP;
		break;
	}

	if (!ret)
		dev_info(adap->pdev_dev,
			 "loading %s successful, reload cxgb4 driver\n",
			 flash_region_strings[region]);
	return ret;
}

#define CXGB4_FW_SIG 0x4368656c
#define CXGB4_FW_SIG_OFFSET 0x160

static int cxgb4_validate_fw_image(const u8 *data, u32 *size)
{
	struct cxgb4_fw_data *header;

	header = (struct cxgb4_fw_data *)&data[CXGB4_FW_SIG_OFFSET];
	if (be32_to_cpu(header->signature) != CXGB4_FW_SIG)
		return -EINVAL;

	if (size)
		*size = be16_to_cpu(((struct fw_hdr *)data)->len512) * 512;

	return 0;
}

static int cxgb4_validate_bootcfg_image(const u8 *data, u32 *size)
{
	struct cxgb4_bootcfg_data *header;

	header = (struct cxgb4_bootcfg_data *)data;
	if (le16_to_cpu(header->signature) != BOOT_CFG_SIG)
		return -EINVAL;

	return 0;
}

static int cxgb4_validate_boot_image(const u8 *data, u32 *size)
{
	struct cxgb4_pci_exp_rom_header *exp_header;
	struct cxgb4_pcir_data *pcir_header;
	struct legacy_pci_rom_hdr *header;
	const u8 *cur_header = data;
	u16 pcir_offset;

	exp_header = (struct cxgb4_pci_exp_rom_header *)data;

	if (le16_to_cpu(exp_header->signature) != BOOT_SIGNATURE)
		return -EINVAL;

	if (size) {
		do {
			header = (struct legacy_pci_rom_hdr *)cur_header;
			pcir_offset = le16_to_cpu(header->pcir_offset);
			pcir_header = (struct cxgb4_pcir_data *)(cur_header +
				      pcir_offset);

			*size += header->size512 * 512;
			cur_header += header->size512 * 512;
		} while (!(pcir_header->indicator & CXGB4_HDR_INDI));
	}

	return 0;
}

static int cxgb4_ethtool_get_flash_region(const u8 *data, u32 *size)
{
	if (!cxgb4_validate_fw_image(data, size))
		return CXGB4_ETHTOOL_FLASH_FW;
	if (!cxgb4_validate_boot_image(data, size))
		return CXGB4_ETHTOOL_FLASH_BOOT;
	if (!cxgb4_validate_phy_image(data, size))
		return CXGB4_ETHTOOL_FLASH_PHY;
	if (!cxgb4_validate_bootcfg_image(data, size))
		return CXGB4_ETHTOOL_FLASH_BOOTCFG;

	return -EOPNOTSUPP;
}

static int set_flash(struct net_device *netdev, struct ethtool_flash *ef)
{
	struct adapter *adap = netdev2adap(netdev);
	const struct firmware *fw;
	unsigned int master;
	u8 master_vld = 0;
	const u8 *fw_data;
	size_t fw_size;
	u32 size = 0;
	u32 pcie_fw;
	int region;
	int ret;

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

	fw_data = fw->data;
	fw_size = fw->size;
	if (ef->region == ETHTOOL_FLASH_ALL_REGIONS) {
		while (fw_size > 0) {
			size = 0;
			region = cxgb4_ethtool_get_flash_region(fw_data, &size);
			if (region < 0 || !size) {
				ret = region;
				goto out_free_fw;
			}

			ret = cxgb4_ethtool_flash_region(netdev, fw_data, size,
							 region);
			if (ret)
				goto out_free_fw;

			fw_data += size;
			fw_size -= size;
		}
	} else {
		ret = cxgb4_ethtool_flash_region(netdev, fw_data, fw_size,
						 ef->region);
	}

out_free_fw:
	release_firmware(fw);
	return ret;
}

static int get_ts_info(struct net_device *dev, struct ethtool_ts_info *ts_info)
{
	struct port_info *pi = netdev_priv(dev);
	struct  adapter *adapter = pi->adapter;

	ts_info->so_timestamping = SOF_TIMESTAMPING_TX_SOFTWARE |
				   SOF_TIMESTAMPING_RX_SOFTWARE |
				   SOF_TIMESTAMPING_SOFTWARE;

	ts_info->so_timestamping |= SOF_TIMESTAMPING_RX_HARDWARE |
				    SOF_TIMESTAMPING_TX_HARDWARE |
				    SOF_TIMESTAMPING_RAW_HARDWARE;

	ts_info->tx_types = (1 << HWTSTAMP_TX_OFF) |
			    (1 << HWTSTAMP_TX_ON);

	ts_info->rx_filters = (1 << HWTSTAMP_FILTER_NONE) |
			      (1 << HWTSTAMP_FILTER_PTP_V2_L4_EVENT) |
			      (1 << HWTSTAMP_FILTER_PTP_V1_L4_SYNC) |
			      (1 << HWTSTAMP_FILTER_PTP_V1_L4_DELAY_REQ) |
			      (1 << HWTSTAMP_FILTER_PTP_V2_L4_SYNC) |
			      (1 << HWTSTAMP_FILTER_PTP_V2_L4_DELAY_REQ);

	if (adapter->ptp_clock)
		ts_info->phc_index = ptp_clock_index(adapter->ptp_clock);
	else
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
	if (pi->adapter->flags & CXGB4_FULL_INIT_DONE) {
		for (i = 0; i < pi->rss_size; i++)
			pi->rss[i] = p[i];

		return cxgb4_write_rss(pi, pi->rss);
	}

	return -EPERM;
}

static struct filter_entry *cxgb4_get_filter_entry(struct adapter *adap,
						   u32 ftid)
{
	struct tid_info *t = &adap->tids;

	if (ftid >= t->hpftid_base && ftid < t->hpftid_base + t->nhpftids)
		return &t->hpftid_tab[ftid - t->hpftid_base];

	if (ftid >= t->ftid_base && ftid < t->ftid_base + t->nftids)
		return &t->ftid_tab[ftid - t->ftid_base];

	return lookup_tid(t, ftid);
}

static void cxgb4_fill_filter_rule(struct ethtool_rx_flow_spec *fs,
				   struct ch_filter_specification *dfs)
{
	switch (dfs->val.proto) {
	case IPPROTO_TCP:
		if (dfs->type)
			fs->flow_type = TCP_V6_FLOW;
		else
			fs->flow_type = TCP_V4_FLOW;
		break;
	case IPPROTO_UDP:
		if (dfs->type)
			fs->flow_type = UDP_V6_FLOW;
		else
			fs->flow_type = UDP_V4_FLOW;
		break;
	}

	if (dfs->type) {
		fs->h_u.tcp_ip6_spec.psrc = cpu_to_be16(dfs->val.fport);
		fs->m_u.tcp_ip6_spec.psrc = cpu_to_be16(dfs->mask.fport);
		fs->h_u.tcp_ip6_spec.pdst = cpu_to_be16(dfs->val.lport);
		fs->m_u.tcp_ip6_spec.pdst = cpu_to_be16(dfs->mask.lport);
		memcpy(&fs->h_u.tcp_ip6_spec.ip6src, &dfs->val.fip[0],
		       sizeof(fs->h_u.tcp_ip6_spec.ip6src));
		memcpy(&fs->m_u.tcp_ip6_spec.ip6src, &dfs->mask.fip[0],
		       sizeof(fs->m_u.tcp_ip6_spec.ip6src));
		memcpy(&fs->h_u.tcp_ip6_spec.ip6dst, &dfs->val.lip[0],
		       sizeof(fs->h_u.tcp_ip6_spec.ip6dst));
		memcpy(&fs->m_u.tcp_ip6_spec.ip6dst, &dfs->mask.lip[0],
		       sizeof(fs->m_u.tcp_ip6_spec.ip6dst));
		fs->h_u.tcp_ip6_spec.tclass = dfs->val.tos;
		fs->m_u.tcp_ip6_spec.tclass = dfs->mask.tos;
	} else {
		fs->h_u.tcp_ip4_spec.psrc = cpu_to_be16(dfs->val.fport);
		fs->m_u.tcp_ip4_spec.psrc = cpu_to_be16(dfs->mask.fport);
		fs->h_u.tcp_ip4_spec.pdst = cpu_to_be16(dfs->val.lport);
		fs->m_u.tcp_ip4_spec.pdst = cpu_to_be16(dfs->mask.lport);
		memcpy(&fs->h_u.tcp_ip4_spec.ip4src, &dfs->val.fip[0],
		       sizeof(fs->h_u.tcp_ip4_spec.ip4src));
		memcpy(&fs->m_u.tcp_ip4_spec.ip4src, &dfs->mask.fip[0],
		       sizeof(fs->m_u.tcp_ip4_spec.ip4src));
		memcpy(&fs->h_u.tcp_ip4_spec.ip4dst, &dfs->val.lip[0],
		       sizeof(fs->h_u.tcp_ip4_spec.ip4dst));
		memcpy(&fs->m_u.tcp_ip4_spec.ip4dst, &dfs->mask.lip[0],
		       sizeof(fs->m_u.tcp_ip4_spec.ip4dst));
		fs->h_u.tcp_ip4_spec.tos = dfs->val.tos;
		fs->m_u.tcp_ip4_spec.tos = dfs->mask.tos;
	}
	fs->h_ext.vlan_tci = cpu_to_be16(dfs->val.ivlan);
	fs->m_ext.vlan_tci = cpu_to_be16(dfs->mask.ivlan);
	fs->flow_type |= FLOW_EXT;

	if (dfs->action == FILTER_DROP)
		fs->ring_cookie = RX_CLS_FLOW_DISC;
	else
		fs->ring_cookie = dfs->iq;
}

static int cxgb4_ntuple_get_filter(struct net_device *dev,
				   struct ethtool_rxnfc *cmd,
				   unsigned int loc)
{
	const struct port_info *pi = netdev_priv(dev);
	struct adapter *adap = netdev2adap(dev);
	struct filter_entry *f;
	int ftid;

	if (!(adap->flags & CXGB4_FULL_INIT_DONE))
		return -EAGAIN;

	/* Check for maximum filter range */
	if (!adap->ethtool_filters)
		return -EOPNOTSUPP;

	if (loc >= adap->ethtool_filters->nentries)
		return -ERANGE;

	if (!test_bit(loc, adap->ethtool_filters->port[pi->port_id].bmap))
		return -ENOENT;

	ftid = adap->ethtool_filters->port[pi->port_id].loc_array[loc];

	/* Fetch filter_entry */
	f = cxgb4_get_filter_entry(adap, ftid);

	cxgb4_fill_filter_rule(&cmd->fs, &f->fs);

	return 0;
}

static int get_rxnfc(struct net_device *dev, struct ethtool_rxnfc *info,
		     u32 *rules)
{
	const struct port_info *pi = netdev_priv(dev);
	struct adapter *adap = netdev2adap(dev);
	unsigned int count = 0, index = 0;
	int ret = 0;

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
	case ETHTOOL_GRXCLSRLCNT:
		info->rule_cnt =
		       adap->ethtool_filters->port[pi->port_id].in_use;
		return 0;
	case ETHTOOL_GRXCLSRULE:
		return cxgb4_ntuple_get_filter(dev, info, info->fs.location);
	case ETHTOOL_GRXCLSRLALL:
		info->data = adap->ethtool_filters->nentries;
		while (count < info->rule_cnt) {
			ret = cxgb4_ntuple_get_filter(dev, info, index);
			if (!ret)
				rules[count++] = index;
			index++;
		}
		return 0;
	}

	return -EOPNOTSUPP;
}

static int cxgb4_ntuple_del_filter(struct net_device *dev,
				   struct ethtool_rxnfc *cmd)
{
	struct cxgb4_ethtool_filter_info *filter_info;
	struct adapter *adapter = netdev2adap(dev);
	struct port_info *pi = netdev_priv(dev);
	struct filter_entry *f;
	u32 filter_id;
	int ret;

	if (!(adapter->flags & CXGB4_FULL_INIT_DONE))
		return -EAGAIN;  /* can still change nfilters */

	if (!adapter->ethtool_filters)
		return -EOPNOTSUPP;

	if (cmd->fs.location >= adapter->ethtool_filters->nentries) {
		dev_err(adapter->pdev_dev,
			"Location must be < %u",
			adapter->ethtool_filters->nentries);
		return -ERANGE;
	}

	filter_info = &adapter->ethtool_filters->port[pi->port_id];

	if (!test_bit(cmd->fs.location, filter_info->bmap))
		return -ENOENT;

	filter_id = filter_info->loc_array[cmd->fs.location];
	f = cxgb4_get_filter_entry(adapter, filter_id);

	if (f->fs.prio)
		filter_id -= adapter->tids.hpftid_base;
	else if (!f->fs.hash)
		filter_id -= (adapter->tids.ftid_base - adapter->tids.nhpftids);

	ret = cxgb4_flow_rule_destroy(dev, f->fs.tc_prio, &f->fs, filter_id);
	if (ret)
		goto err;

	clear_bit(cmd->fs.location, filter_info->bmap);
	filter_info->in_use--;

err:
	return ret;
}

/* Add Ethtool n-tuple filters. */
static int cxgb4_ntuple_set_filter(struct net_device *netdev,
				   struct ethtool_rxnfc *cmd)
{
	struct ethtool_rx_flow_spec_input input = {};
	struct cxgb4_ethtool_filter_info *filter_info;
	struct adapter *adapter = netdev2adap(netdev);
	struct port_info *pi = netdev_priv(netdev);
	struct ch_filter_specification fs;
	struct ethtool_rx_flow_rule *flow;
	u32 tid;
	int ret;

	if (!(adapter->flags & CXGB4_FULL_INIT_DONE))
		return -EAGAIN;  /* can still change nfilters */

	if (!adapter->ethtool_filters)
		return -EOPNOTSUPP;

	if (cmd->fs.location >= adapter->ethtool_filters->nentries) {
		dev_err(adapter->pdev_dev,
			"Location must be < %u",
			adapter->ethtool_filters->nentries);
		return -ERANGE;
	}

	if (test_bit(cmd->fs.location,
		     adapter->ethtool_filters->port[pi->port_id].bmap))
		return -EEXIST;

	memset(&fs, 0, sizeof(fs));

	input.fs = &cmd->fs;
	flow = ethtool_rx_flow_rule_create(&input);
	if (IS_ERR(flow)) {
		ret = PTR_ERR(flow);
		goto exit;
	}

	fs.hitcnts = 1;

	ret = cxgb4_flow_rule_replace(netdev, flow->rule, cmd->fs.location,
				      NULL, &fs, &tid);
	if (ret)
		goto free;

	filter_info = &adapter->ethtool_filters->port[pi->port_id];

	if (fs.prio)
		tid += adapter->tids.hpftid_base;
	else if (!fs.hash)
		tid += (adapter->tids.ftid_base - adapter->tids.nhpftids);

	filter_info->loc_array[cmd->fs.location] = tid;
	set_bit(cmd->fs.location, filter_info->bmap);
	filter_info->in_use++;

free:
	ethtool_rx_flow_rule_destroy(flow);
exit:
	return ret;
}

static int set_rxnfc(struct net_device *dev, struct ethtool_rxnfc *cmd)
{
	int ret = -EOPNOTSUPP;

	switch (cmd->cmd) {
	case ETHTOOL_SRXCLSRLINS:
		ret = cxgb4_ntuple_set_filter(dev, cmd);
		break;
	case ETHTOOL_SRXCLSRLDEL:
		ret = cxgb4_ntuple_del_filter(dev, cmd);
		break;
	default:
		break;
	}

	return ret;
}

static int set_dump(struct net_device *dev, struct ethtool_dump *eth_dump)
{
	struct adapter *adapter = netdev2adap(dev);
	u32 len = 0;

	len = sizeof(struct cudbg_hdr) +
	      sizeof(struct cudbg_entity_hdr) * CUDBG_MAX_ENTITY;
	len += cxgb4_get_dump_length(adapter, eth_dump->flag);

	adapter->eth_dump.flag = eth_dump->flag;
	adapter->eth_dump.len = len;
	return 0;
}

static int get_dump_flag(struct net_device *dev, struct ethtool_dump *eth_dump)
{
	struct adapter *adapter = netdev2adap(dev);

	eth_dump->flag = adapter->eth_dump.flag;
	eth_dump->len = adapter->eth_dump.len;
	eth_dump->version = adapter->eth_dump.version;
	return 0;
}

static int get_dump_data(struct net_device *dev, struct ethtool_dump *eth_dump,
			 void *buf)
{
	struct adapter *adapter = netdev2adap(dev);
	u32 len = 0;
	int ret = 0;

	if (adapter->eth_dump.flag == CXGB4_ETH_DUMP_NONE)
		return -ENOENT;

	len = sizeof(struct cudbg_hdr) +
	      sizeof(struct cudbg_entity_hdr) * CUDBG_MAX_ENTITY;
	len += cxgb4_get_dump_length(adapter, adapter->eth_dump.flag);
	if (eth_dump->len < len)
		return -ENOMEM;

	ret = cxgb4_cudbg_collect(adapter, buf, &len, adapter->eth_dump.flag);
	if (ret)
		return ret;

	eth_dump->flag = adapter->eth_dump.flag;
	eth_dump->len = len;
	eth_dump->version = adapter->eth_dump.version;
	return 0;
}

static bool cxgb4_fw_mod_type_info_available(unsigned int fw_mod_type)
{
	/* Read port module EEPROM as long as it is plugged-in and
	 * safe to read.
	 */
	return (fw_mod_type != FW_PORT_MOD_TYPE_NONE &&
		fw_mod_type != FW_PORT_MOD_TYPE_ERROR);
}

static int cxgb4_get_module_info(struct net_device *dev,
				 struct ethtool_modinfo *modinfo)
{
	struct port_info *pi = netdev_priv(dev);
	u8 sff8472_comp, sff_diag_type, sff_rev;
	struct adapter *adapter = pi->adapter;
	int ret;

	if (!cxgb4_fw_mod_type_info_available(pi->mod_type))
		return -EINVAL;

	switch (pi->port_type) {
	case FW_PORT_TYPE_SFP:
	case FW_PORT_TYPE_QSA:
	case FW_PORT_TYPE_SFP28:
		ret = t4_i2c_rd(adapter, adapter->mbox, pi->tx_chan,
				I2C_DEV_ADDR_A0, SFF_8472_COMP_ADDR,
				SFF_8472_COMP_LEN, &sff8472_comp);
		if (ret)
			return ret;
		ret = t4_i2c_rd(adapter, adapter->mbox, pi->tx_chan,
				I2C_DEV_ADDR_A0, SFP_DIAG_TYPE_ADDR,
				SFP_DIAG_TYPE_LEN, &sff_diag_type);
		if (ret)
			return ret;

		if (!sff8472_comp || (sff_diag_type & SFP_DIAG_ADDRMODE)) {
			modinfo->type = ETH_MODULE_SFF_8079;
			modinfo->eeprom_len = ETH_MODULE_SFF_8079_LEN;
		} else {
			modinfo->type = ETH_MODULE_SFF_8472;
			if (sff_diag_type & SFP_DIAG_IMPLEMENTED)
				modinfo->eeprom_len = ETH_MODULE_SFF_8472_LEN;
			else
				modinfo->eeprom_len = ETH_MODULE_SFF_8472_LEN / 2;
		}
		break;

	case FW_PORT_TYPE_QSFP:
	case FW_PORT_TYPE_QSFP_10G:
	case FW_PORT_TYPE_CR_QSFP:
	case FW_PORT_TYPE_CR2_QSFP:
	case FW_PORT_TYPE_CR4_QSFP:
		ret = t4_i2c_rd(adapter, adapter->mbox, pi->tx_chan,
				I2C_DEV_ADDR_A0, SFF_REV_ADDR,
				SFF_REV_LEN, &sff_rev);
		/* For QSFP type ports, revision value >= 3
		 * means the SFP is 8636 compliant.
		 */
		if (ret)
			return ret;
		if (sff_rev >= 0x3) {
			modinfo->type = ETH_MODULE_SFF_8636;
			modinfo->eeprom_len = ETH_MODULE_SFF_8636_LEN;
		} else {
			modinfo->type = ETH_MODULE_SFF_8436;
			modinfo->eeprom_len = ETH_MODULE_SFF_8436_LEN;
		}
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

static int cxgb4_get_module_eeprom(struct net_device *dev,
				   struct ethtool_eeprom *eprom, u8 *data)
{
	int ret = 0, offset = eprom->offset, len = eprom->len;
	struct port_info *pi = netdev_priv(dev);
	struct adapter *adapter = pi->adapter;

	memset(data, 0, eprom->len);
	if (offset + len <= I2C_PAGE_SIZE)
		return t4_i2c_rd(adapter, adapter->mbox, pi->tx_chan,
				 I2C_DEV_ADDR_A0, offset, len, data);

	/* offset + len spans 0xa0 and 0xa1 pages */
	if (offset <= I2C_PAGE_SIZE) {
		/* read 0xa0 page */
		len = I2C_PAGE_SIZE - offset;
		ret =  t4_i2c_rd(adapter, adapter->mbox, pi->tx_chan,
				 I2C_DEV_ADDR_A0, offset, len, data);
		if (ret)
			return ret;
		offset = I2C_PAGE_SIZE;
		/* Remaining bytes to be read from second page =
		 * Total length - bytes read from first page
		 */
		len = eprom->len - len;
	}
	/* Read additional optical diagnostics from page 0xa2 if supported */
	return t4_i2c_rd(adapter, adapter->mbox, pi->tx_chan, I2C_DEV_ADDR_A2,
			 offset, len, &data[eprom->len - len]);
}

static u32 cxgb4_get_priv_flags(struct net_device *netdev)
{
	struct port_info *pi = netdev_priv(netdev);
	struct adapter *adapter = pi->adapter;

	return (adapter->eth_flags | pi->eth_flags);
}

/**
 *	set_flags - set/unset specified flags if passed in new_flags
 *	@cur_flags: pointer to current flags
 *	@new_flags: new incoming flags
 *	@flags: set of flags to set/unset
 */
static inline void set_flags(u32 *cur_flags, u32 new_flags, u32 flags)
{
	*cur_flags = (*cur_flags & ~flags) | (new_flags & flags);
}

static int cxgb4_set_priv_flags(struct net_device *netdev, u32 flags)
{
	struct port_info *pi = netdev_priv(netdev);
	struct adapter *adapter = pi->adapter;

	set_flags(&adapter->eth_flags, flags, PRIV_FLAGS_ADAP);
	set_flags(&pi->eth_flags, flags, PRIV_FLAGS_PORT);

	return 0;
}

static void cxgb4_lb_test(struct net_device *netdev, u64 *lb_status)
{
	int dev_state = netif_running(netdev);

	if (dev_state) {
		netif_tx_stop_all_queues(netdev);
		netif_carrier_off(netdev);
	}

	*lb_status = cxgb4_selftest_lb_pkt(netdev);

	if (dev_state) {
		netif_tx_start_all_queues(netdev);
		netif_carrier_on(netdev);
	}
}

static void cxgb4_self_test(struct net_device *netdev,
			    struct ethtool_test *eth_test, u64 *data)
{
	struct port_info *pi = netdev_priv(netdev);
	struct adapter *adap = pi->adapter;

	memset(data, 0, sizeof(u64) * CXGB4_ETHTOOL_MAX_TEST);

	if (!(adap->flags & CXGB4_FULL_INIT_DONE) ||
	    !(adap->flags & CXGB4_FW_OK)) {
		eth_test->flags |= ETH_TEST_FL_FAILED;
		return;
	}

	if (eth_test->flags & ETH_TEST_FL_OFFLINE)
		cxgb4_lb_test(netdev, &data[CXGB4_ETHTOOL_LB_TEST]);

	if (data[CXGB4_ETHTOOL_LB_TEST])
		eth_test->flags |= ETH_TEST_FL_FAILED;
}

static const struct ethtool_ops cxgb_ethtool_ops = {
	.supported_coalesce_params = ETHTOOL_COALESCE_USECS |
				     ETHTOOL_COALESCE_RX_MAX_FRAMES |
				     ETHTOOL_COALESCE_TX_USECS_IRQ |
				     ETHTOOL_COALESCE_USE_ADAPTIVE_RX,
	.get_link_ksettings = get_link_ksettings,
	.set_link_ksettings = set_link_ksettings,
	.get_fecparam      = get_fecparam,
	.set_fecparam      = set_fecparam,
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
	.set_rxnfc         = set_rxnfc,
	.get_rxfh_indir_size = get_rss_table_size,
	.get_rxfh	   = get_rss_table,
	.set_rxfh	   = set_rss_table,
	.self_test	   = cxgb4_self_test,
	.flash_device      = set_flash,
	.get_ts_info       = get_ts_info,
	.set_dump          = set_dump,
	.get_dump_flag     = get_dump_flag,
	.get_dump_data     = get_dump_data,
	.get_module_info   = cxgb4_get_module_info,
	.get_module_eeprom = cxgb4_get_module_eeprom,
	.get_priv_flags    = cxgb4_get_priv_flags,
	.set_priv_flags    = cxgb4_set_priv_flags,
};

void cxgb4_cleanup_ethtool_filters(struct adapter *adap)
{
	struct cxgb4_ethtool_filter_info *eth_filter_info;
	u8 i;

	if (!adap->ethtool_filters)
		return;

	eth_filter_info = adap->ethtool_filters->port;

	if (eth_filter_info) {
		for (i = 0; i < adap->params.nports; i++) {
			kvfree(eth_filter_info[i].loc_array);
			kfree(eth_filter_info[i].bmap);
		}
		kfree(eth_filter_info);
	}

	kfree(adap->ethtool_filters);
}

int cxgb4_init_ethtool_filters(struct adapter *adap)
{
	struct cxgb4_ethtool_filter_info *eth_filter_info;
	struct cxgb4_ethtool_filter *eth_filter;
	struct tid_info *tids = &adap->tids;
	u32 nentries, i;
	int ret;

	eth_filter = kzalloc(sizeof(*eth_filter), GFP_KERNEL);
	if (!eth_filter)
		return -ENOMEM;

	eth_filter_info = kcalloc(adap->params.nports,
				  sizeof(*eth_filter_info),
				  GFP_KERNEL);
	if (!eth_filter_info) {
		ret = -ENOMEM;
		goto free_eth_filter;
	}

	eth_filter->port = eth_filter_info;

	nentries = tids->nhpftids + tids->nftids;
	if (is_hashfilter(adap))
		nentries += tids->nhash +
			    (adap->tids.stid_base - adap->tids.tid_base);
	eth_filter->nentries = nentries;

	for (i = 0; i < adap->params.nports; i++) {
		eth_filter->port[i].loc_array = kvzalloc(nentries, GFP_KERNEL);
		if (!eth_filter->port[i].loc_array) {
			ret = -ENOMEM;
			goto free_eth_finfo;
		}

		eth_filter->port[i].bmap = kcalloc(BITS_TO_LONGS(nentries),
						   sizeof(unsigned long),
						   GFP_KERNEL);
		if (!eth_filter->port[i].bmap) {
			ret = -ENOMEM;
			goto free_eth_finfo;
		}
	}

	adap->ethtool_filters = eth_filter;
	return 0;

free_eth_finfo:
	while (i-- > 0) {
		kfree(eth_filter->port[i].bmap);
		kvfree(eth_filter->port[i].loc_array);
	}
	kfree(eth_filter_info);

free_eth_filter:
	kfree(eth_filter);

	return ret;
}

void cxgb4_set_ethtool_ops(struct net_device *netdev)
{
	netdev->ethtool_ops = &cxgb_ethtool_ops;
}

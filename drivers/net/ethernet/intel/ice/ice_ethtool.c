// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2018, Intel Corporation. */

/* ethtool support for ice */

#include "ice.h"

struct ice_stats {
	char stat_string[ETH_GSTRING_LEN];
	int sizeof_stat;
	int stat_offset;
};

#define ICE_STAT(_type, _name, _stat) { \
	.stat_string = _name, \
	.sizeof_stat = FIELD_SIZEOF(_type, _stat), \
	.stat_offset = offsetof(_type, _stat) \
}

#define ICE_VSI_STAT(_name, _stat) \
		ICE_STAT(struct ice_vsi, _name, _stat)
#define ICE_PF_STAT(_name, _stat) \
		ICE_STAT(struct ice_pf, _name, _stat)

static int ice_q_stats_len(struct net_device *netdev)
{
	struct ice_netdev_priv *np = netdev_priv(netdev);

	return ((np->vsi->num_txq + np->vsi->num_rxq) *
		(sizeof(struct ice_q_stats) / sizeof(u64)));
}

#define ICE_PF_STATS_LEN	ARRAY_SIZE(ice_gstrings_pf_stats)
#define ICE_VSI_STATS_LEN	ARRAY_SIZE(ice_gstrings_vsi_stats)

#define ICE_ALL_STATS_LEN(n)	(ICE_PF_STATS_LEN + ICE_VSI_STATS_LEN + \
				 ice_q_stats_len(n))

static const struct ice_stats ice_gstrings_vsi_stats[] = {
	ICE_VSI_STAT("tx_unicast", eth_stats.tx_unicast),
	ICE_VSI_STAT("rx_unicast", eth_stats.rx_unicast),
	ICE_VSI_STAT("tx_multicast", eth_stats.tx_multicast),
	ICE_VSI_STAT("rx_multicast", eth_stats.rx_multicast),
	ICE_VSI_STAT("tx_broadcast", eth_stats.tx_broadcast),
	ICE_VSI_STAT("rx_broadcast", eth_stats.rx_broadcast),
	ICE_VSI_STAT("tx_bytes", eth_stats.tx_bytes),
	ICE_VSI_STAT("rx_bytes", eth_stats.rx_bytes),
	ICE_VSI_STAT("rx_discards", eth_stats.rx_discards),
	ICE_VSI_STAT("tx_errors", eth_stats.tx_errors),
	ICE_VSI_STAT("tx_linearize", tx_linearize),
	ICE_VSI_STAT("rx_unknown_protocol", eth_stats.rx_unknown_protocol),
	ICE_VSI_STAT("rx_alloc_fail", rx_buf_failed),
	ICE_VSI_STAT("rx_pg_alloc_fail", rx_page_failed),
};

/* These PF_STATs might look like duplicates of some NETDEV_STATs,
 * but they aren't. This device is capable of supporting multiple
 * VSIs/netdevs on a single PF. The NETDEV_STATs are for individual
 * netdevs whereas the PF_STATs are for the physical function that's
 * hosting these netdevs.
 *
 * The PF_STATs are appended to the netdev stats only when ethtool -S
 * is queried on the base PF netdev.
 */
static struct ice_stats ice_gstrings_pf_stats[] = {
	ICE_PF_STAT("tx_bytes", stats.eth.tx_bytes),
	ICE_PF_STAT("rx_bytes", stats.eth.rx_bytes),
	ICE_PF_STAT("tx_unicast", stats.eth.tx_unicast),
	ICE_PF_STAT("rx_unicast", stats.eth.rx_unicast),
	ICE_PF_STAT("tx_multicast", stats.eth.tx_multicast),
	ICE_PF_STAT("rx_multicast", stats.eth.rx_multicast),
	ICE_PF_STAT("tx_broadcast", stats.eth.tx_broadcast),
	ICE_PF_STAT("rx_broadcast", stats.eth.rx_broadcast),
	ICE_PF_STAT("tx_errors", stats.eth.tx_errors),
	ICE_PF_STAT("tx_size_64", stats.tx_size_64),
	ICE_PF_STAT("rx_size_64", stats.rx_size_64),
	ICE_PF_STAT("tx_size_127", stats.tx_size_127),
	ICE_PF_STAT("rx_size_127", stats.rx_size_127),
	ICE_PF_STAT("tx_size_255", stats.tx_size_255),
	ICE_PF_STAT("rx_size_255", stats.rx_size_255),
	ICE_PF_STAT("tx_size_511", stats.tx_size_511),
	ICE_PF_STAT("rx_size_511", stats.rx_size_511),
	ICE_PF_STAT("tx_size_1023", stats.tx_size_1023),
	ICE_PF_STAT("rx_size_1023", stats.rx_size_1023),
	ICE_PF_STAT("tx_size_1522", stats.tx_size_1522),
	ICE_PF_STAT("rx_size_1522", stats.rx_size_1522),
	ICE_PF_STAT("tx_size_big", stats.tx_size_big),
	ICE_PF_STAT("rx_size_big", stats.rx_size_big),
	ICE_PF_STAT("link_xon_tx", stats.link_xon_tx),
	ICE_PF_STAT("link_xon_rx", stats.link_xon_rx),
	ICE_PF_STAT("link_xoff_tx", stats.link_xoff_tx),
	ICE_PF_STAT("link_xoff_rx", stats.link_xoff_rx),
	ICE_PF_STAT("tx_dropped_link_down", stats.tx_dropped_link_down),
	ICE_PF_STAT("rx_undersize", stats.rx_undersize),
	ICE_PF_STAT("rx_fragments", stats.rx_fragments),
	ICE_PF_STAT("rx_oversize", stats.rx_oversize),
	ICE_PF_STAT("rx_jabber", stats.rx_jabber),
	ICE_PF_STAT("rx_csum_bad", hw_csum_rx_error),
	ICE_PF_STAT("rx_length_errors", stats.rx_len_errors),
	ICE_PF_STAT("rx_dropped", stats.eth.rx_discards),
	ICE_PF_STAT("rx_crc_errors", stats.crc_errors),
	ICE_PF_STAT("illegal_bytes", stats.illegal_bytes),
	ICE_PF_STAT("mac_local_faults", stats.mac_local_faults),
	ICE_PF_STAT("mac_remote_faults", stats.mac_remote_faults),
};

static u32 ice_regs_dump_list[] = {
	PFGEN_STATE,
	PRTGEN_STATUS,
	QRX_CTRL(0),
	QINT_TQCTL(0),
	QINT_RQCTL(0),
	PFINT_OICR_ENA,
	QRX_ITR(0),
};

/**
 * ice_nvm_version_str - format the NVM version strings
 * @hw: ptr to the hardware info
 */
static char *ice_nvm_version_str(struct ice_hw *hw)
{
	static char buf[ICE_ETHTOOL_FWVER_LEN];
	u8 ver, patch;
	u32 full_ver;
	u16 build;

	full_ver = hw->nvm.oem_ver;
	ver = (u8)((full_ver & ICE_OEM_VER_MASK) >> ICE_OEM_VER_SHIFT);
	build = (u16)((full_ver & ICE_OEM_VER_BUILD_MASK) >>
		      ICE_OEM_VER_BUILD_SHIFT);
	patch = (u8)(full_ver & ICE_OEM_VER_PATCH_MASK);

	snprintf(buf, sizeof(buf), "%x.%02x 0x%x %d.%d.%d",
		 (hw->nvm.ver & ICE_NVM_VER_HI_MASK) >> ICE_NVM_VER_HI_SHIFT,
		 (hw->nvm.ver & ICE_NVM_VER_LO_MASK) >> ICE_NVM_VER_LO_SHIFT,
		 hw->nvm.eetrack, ver, build, patch);

	return buf;
}

static void
ice_get_drvinfo(struct net_device *netdev, struct ethtool_drvinfo *drvinfo)
{
	struct ice_netdev_priv *np = netdev_priv(netdev);
	struct ice_vsi *vsi = np->vsi;
	struct ice_pf *pf = vsi->back;

	strlcpy(drvinfo->driver, KBUILD_MODNAME, sizeof(drvinfo->driver));
	strlcpy(drvinfo->version, ice_drv_ver, sizeof(drvinfo->version));
	strlcpy(drvinfo->fw_version, ice_nvm_version_str(&pf->hw),
		sizeof(drvinfo->fw_version));
	strlcpy(drvinfo->bus_info, pci_name(pf->pdev),
		sizeof(drvinfo->bus_info));
}

static int ice_get_regs_len(struct net_device __always_unused *netdev)
{
	return sizeof(ice_regs_dump_list);
}

static void
ice_get_regs(struct net_device *netdev, struct ethtool_regs *regs, void *p)
{
	struct ice_netdev_priv *np = netdev_priv(netdev);
	struct ice_pf *pf = np->vsi->back;
	struct ice_hw *hw = &pf->hw;
	u32 *regs_buf = (u32 *)p;
	int i;

	regs->version = 1;

	for (i = 0; i < ARRAY_SIZE(ice_regs_dump_list); ++i)
		regs_buf[i] = rd32(hw, ice_regs_dump_list[i]);
}

static u32 ice_get_msglevel(struct net_device *netdev)
{
	struct ice_netdev_priv *np = netdev_priv(netdev);
	struct ice_pf *pf = np->vsi->back;

#ifndef CONFIG_DYNAMIC_DEBUG
	if (pf->hw.debug_mask)
		netdev_info(netdev, "hw debug_mask: 0x%llX\n",
			    pf->hw.debug_mask);
#endif /* !CONFIG_DYNAMIC_DEBUG */

	return pf->msg_enable;
}

static void ice_set_msglevel(struct net_device *netdev, u32 data)
{
	struct ice_netdev_priv *np = netdev_priv(netdev);
	struct ice_pf *pf = np->vsi->back;

#ifndef CONFIG_DYNAMIC_DEBUG
	if (ICE_DBG_USER & data)
		pf->hw.debug_mask = data;
	else
		pf->msg_enable = data;
#else
	pf->msg_enable = data;
#endif /* !CONFIG_DYNAMIC_DEBUG */
}

static void ice_get_strings(struct net_device *netdev, u32 stringset, u8 *data)
{
	struct ice_netdev_priv *np = netdev_priv(netdev);
	struct ice_vsi *vsi = np->vsi;
	char *p = (char *)data;
	unsigned int i;

	switch (stringset) {
	case ETH_SS_STATS:
		for (i = 0; i < ICE_VSI_STATS_LEN; i++) {
			snprintf(p, ETH_GSTRING_LEN, "%s",
				 ice_gstrings_vsi_stats[i].stat_string);
			p += ETH_GSTRING_LEN;
		}

		ice_for_each_txq(vsi, i) {
			snprintf(p, ETH_GSTRING_LEN,
				 "tx-queue-%u.tx_packets", i);
			p += ETH_GSTRING_LEN;
			snprintf(p, ETH_GSTRING_LEN, "tx-queue-%u.tx_bytes", i);
			p += ETH_GSTRING_LEN;
		}

		ice_for_each_rxq(vsi, i) {
			snprintf(p, ETH_GSTRING_LEN,
				 "rx-queue-%u.rx_packets", i);
			p += ETH_GSTRING_LEN;
			snprintf(p, ETH_GSTRING_LEN, "rx-queue-%u.rx_bytes", i);
			p += ETH_GSTRING_LEN;
		}

		if (vsi->type != ICE_VSI_PF)
			return;

		for (i = 0; i < ICE_PF_STATS_LEN; i++) {
			snprintf(p, ETH_GSTRING_LEN, "port.%s",
				 ice_gstrings_pf_stats[i].stat_string);
			p += ETH_GSTRING_LEN;
		}

		break;
	default:
		break;
	}
}

static int ice_get_sset_count(struct net_device *netdev, int sset)
{
	switch (sset) {
	case ETH_SS_STATS:
		return ICE_ALL_STATS_LEN(netdev);
	default:
		return -EOPNOTSUPP;
	}
}

static void
ice_get_ethtool_stats(struct net_device *netdev,
		      struct ethtool_stats __always_unused *stats, u64 *data)
{
	struct ice_netdev_priv *np = netdev_priv(netdev);
	struct ice_vsi *vsi = np->vsi;
	struct ice_pf *pf = vsi->back;
	struct ice_ring *ring;
	unsigned int j = 0;
	int i = 0;
	char *p;

	for (j = 0; j < ICE_VSI_STATS_LEN; j++) {
		p = (char *)vsi + ice_gstrings_vsi_stats[j].stat_offset;
		data[i++] = (ice_gstrings_vsi_stats[j].sizeof_stat ==
			    sizeof(u64)) ? *(u64 *)p : *(u32 *)p;
	}

	/* populate per queue stats */
	rcu_read_lock();

	ice_for_each_txq(vsi, j) {
		ring = READ_ONCE(vsi->tx_rings[j]);
		if (!ring)
			continue;
		data[i++] = ring->stats.pkts;
		data[i++] = ring->stats.bytes;
	}

	ice_for_each_rxq(vsi, j) {
		ring = READ_ONCE(vsi->rx_rings[j]);
		data[i++] = ring->stats.pkts;
		data[i++] = ring->stats.bytes;
	}

	rcu_read_unlock();

	if (vsi->type != ICE_VSI_PF)
		return;

	for (j = 0; j < ICE_PF_STATS_LEN; j++) {
		p = (char *)pf + ice_gstrings_pf_stats[j].stat_offset;
		data[i++] = (ice_gstrings_pf_stats[j].sizeof_stat ==
			     sizeof(u64)) ? *(u64 *)p : *(u32 *)p;
	}
}

static int
ice_get_link_ksettings(struct net_device *netdev,
		       struct ethtool_link_ksettings *ks)
{
	struct ice_netdev_priv *np = netdev_priv(netdev);
	struct ice_link_status *hw_link_info;
	struct ice_vsi *vsi = np->vsi;
	bool link_up;

	hw_link_info = &vsi->port_info->phy.link_info;
	link_up = hw_link_info->link_info & ICE_AQ_LINK_UP;

	ethtool_link_ksettings_add_link_mode(ks, supported,
					     10000baseT_Full);
	ethtool_link_ksettings_add_link_mode(ks, advertising,
					     10000baseT_Full);

	/* set speed and duplex */
	if (link_up) {
		switch (hw_link_info->link_speed) {
		case ICE_AQ_LINK_SPEED_100MB:
			ks->base.speed = SPEED_100;
			break;
		case ICE_AQ_LINK_SPEED_2500MB:
			ks->base.speed = SPEED_2500;
			break;
		case ICE_AQ_LINK_SPEED_5GB:
			ks->base.speed = SPEED_5000;
			break;
		case ICE_AQ_LINK_SPEED_10GB:
			ks->base.speed = SPEED_10000;
			break;
		case ICE_AQ_LINK_SPEED_25GB:
			ks->base.speed = SPEED_25000;
			break;
		case ICE_AQ_LINK_SPEED_40GB:
			ks->base.speed = SPEED_40000;
			break;
		default:
			ks->base.speed = SPEED_UNKNOWN;
			break;
		}

		ks->base.duplex = DUPLEX_FULL;
	} else {
		ks->base.speed = SPEED_UNKNOWN;
		ks->base.duplex = DUPLEX_UNKNOWN;
	}

	/* set autoneg settings */
	ks->base.autoneg = ((hw_link_info->an_info & ICE_AQ_AN_COMPLETED) ?
			    AUTONEG_ENABLE : AUTONEG_DISABLE);

	/* set media type settings */
	switch (vsi->port_info->phy.media_type) {
	case ICE_MEDIA_FIBER:
		ethtool_link_ksettings_add_link_mode(ks, supported, FIBRE);
		ks->base.port = PORT_FIBRE;
		break;
	case ICE_MEDIA_BASET:
		ethtool_link_ksettings_add_link_mode(ks, supported, TP);
		ethtool_link_ksettings_add_link_mode(ks, advertising, TP);
		ks->base.port = PORT_TP;
		break;
	case ICE_MEDIA_BACKPLANE:
		ethtool_link_ksettings_add_link_mode(ks, supported, Autoneg);
		ethtool_link_ksettings_add_link_mode(ks, supported, Backplane);
		ethtool_link_ksettings_add_link_mode(ks, advertising, Autoneg);
		ethtool_link_ksettings_add_link_mode(ks, advertising,
						     Backplane);
		ks->base.port = PORT_NONE;
		break;
	case ICE_MEDIA_DA:
		ethtool_link_ksettings_add_link_mode(ks, supported, FIBRE);
		ethtool_link_ksettings_add_link_mode(ks, advertising, FIBRE);
		ks->base.port = PORT_DA;
		break;
	default:
		ks->base.port = PORT_OTHER;
		break;
	}

	/* flow control is symmetric and always supported */
	ethtool_link_ksettings_add_link_mode(ks, supported, Pause);

	switch (vsi->port_info->fc.req_mode) {
	case ICE_FC_FULL:
		ethtool_link_ksettings_add_link_mode(ks, advertising, Pause);
		break;
	case ICE_FC_TX_PAUSE:
		ethtool_link_ksettings_add_link_mode(ks, advertising,
						     Asym_Pause);
		break;
	case ICE_FC_RX_PAUSE:
		ethtool_link_ksettings_add_link_mode(ks, advertising, Pause);
		ethtool_link_ksettings_add_link_mode(ks, advertising,
						     Asym_Pause);
		break;
	case ICE_FC_PFC:
	default:
		ethtool_link_ksettings_del_link_mode(ks, advertising, Pause);
		ethtool_link_ksettings_del_link_mode(ks, advertising,
						     Asym_Pause);
		break;
	}

	return 0;
}

/**
 * ice_get_rxnfc - command to get RX flow classification rules
 * @netdev: network interface device structure
 * @cmd: ethtool rxnfc command
 * @rule_locs: buffer to rturn Rx flow classification rules
 *
 * Returns Success if the command is supported.
 */
static int ice_get_rxnfc(struct net_device *netdev, struct ethtool_rxnfc *cmd,
			 u32 __always_unused *rule_locs)
{
	struct ice_netdev_priv *np = netdev_priv(netdev);
	struct ice_vsi *vsi = np->vsi;
	int ret = -EOPNOTSUPP;

	switch (cmd->cmd) {
	case ETHTOOL_GRXRINGS:
		cmd->data = vsi->rss_size;
		ret = 0;
		break;
	default:
		break;
	}

	return ret;
}

static void
ice_get_ringparam(struct net_device *netdev, struct ethtool_ringparam *ring)
{
	struct ice_netdev_priv *np = netdev_priv(netdev);
	struct ice_vsi *vsi = np->vsi;

	ring->rx_max_pending = ICE_MAX_NUM_DESC;
	ring->tx_max_pending = ICE_MAX_NUM_DESC;
	ring->rx_pending = vsi->rx_rings[0]->count;
	ring->tx_pending = vsi->tx_rings[0]->count;
	ring->rx_mini_pending = ICE_MIN_NUM_DESC;
	ring->rx_mini_max_pending = 0;
	ring->rx_jumbo_max_pending = 0;
	ring->rx_jumbo_pending = 0;
}

static int
ice_set_ringparam(struct net_device *netdev, struct ethtool_ringparam *ring)
{
	struct ice_ring *tx_rings = NULL, *rx_rings = NULL;
	struct ice_netdev_priv *np = netdev_priv(netdev);
	struct ice_vsi *vsi = np->vsi;
	struct ice_pf *pf = vsi->back;
	int i, timeout = 50, err = 0;
	u32 new_rx_cnt, new_tx_cnt;

	if (ring->tx_pending > ICE_MAX_NUM_DESC ||
	    ring->tx_pending < ICE_MIN_NUM_DESC ||
	    ring->rx_pending > ICE_MAX_NUM_DESC ||
	    ring->rx_pending < ICE_MIN_NUM_DESC) {
		netdev_err(netdev, "Descriptors requested (Tx: %d / Rx: %d) out of range [%d-%d]\n",
			   ring->tx_pending, ring->rx_pending,
			   ICE_MIN_NUM_DESC, ICE_MAX_NUM_DESC);
		return -EINVAL;
	}

	new_tx_cnt = ALIGN(ring->tx_pending, ICE_REQ_DESC_MULTIPLE);
	new_rx_cnt = ALIGN(ring->rx_pending, ICE_REQ_DESC_MULTIPLE);

	/* if nothing to do return success */
	if (new_tx_cnt == vsi->tx_rings[0]->count &&
	    new_rx_cnt == vsi->rx_rings[0]->count) {
		netdev_dbg(netdev, "Nothing to change, descriptor count is same as requested\n");
		return 0;
	}

	while (test_and_set_bit(__ICE_CFG_BUSY, pf->state)) {
		timeout--;
		if (!timeout)
			return -EBUSY;
		usleep_range(1000, 2000);
	}

	/* set for the next time the netdev is started */
	if (!netif_running(vsi->netdev)) {
		for (i = 0; i < vsi->alloc_txq; i++)
			vsi->tx_rings[i]->count = new_tx_cnt;
		for (i = 0; i < vsi->alloc_rxq; i++)
			vsi->rx_rings[i]->count = new_rx_cnt;
		netdev_dbg(netdev, "Link is down, descriptor count change happens when link is brought up\n");
		goto done;
	}

	if (new_tx_cnt == vsi->tx_rings[0]->count)
		goto process_rx;

	/* alloc updated Tx resources */
	netdev_info(netdev, "Changing Tx descriptor count from %d to %d\n",
		    vsi->tx_rings[0]->count, new_tx_cnt);

	tx_rings = devm_kcalloc(&pf->pdev->dev, vsi->alloc_txq,
				sizeof(struct ice_ring), GFP_KERNEL);
	if (!tx_rings) {
		err = -ENOMEM;
		goto done;
	}

	for (i = 0; i < vsi->num_txq; i++) {
		/* clone ring and setup updated count */
		tx_rings[i] = *vsi->tx_rings[i];
		tx_rings[i].count = new_tx_cnt;
		tx_rings[i].desc = NULL;
		tx_rings[i].tx_buf = NULL;
		err = ice_setup_tx_ring(&tx_rings[i]);
		if (err) {
			while (i) {
				i--;
				ice_clean_tx_ring(&tx_rings[i]);
			}
			devm_kfree(&pf->pdev->dev, tx_rings);
			goto done;
		}
	}

process_rx:
	if (new_rx_cnt == vsi->rx_rings[0]->count)
		goto process_link;

	/* alloc updated Rx resources */
	netdev_info(netdev, "Changing Rx descriptor count from %d to %d\n",
		    vsi->rx_rings[0]->count, new_rx_cnt);

	rx_rings = devm_kcalloc(&pf->pdev->dev, vsi->alloc_rxq,
				sizeof(struct ice_ring), GFP_KERNEL);
	if (!rx_rings) {
		err = -ENOMEM;
		goto done;
	}

	for (i = 0; i < vsi->num_rxq; i++) {
		/* clone ring and setup updated count */
		rx_rings[i] = *vsi->rx_rings[i];
		rx_rings[i].count = new_rx_cnt;
		rx_rings[i].desc = NULL;
		rx_rings[i].rx_buf = NULL;
		/* this is to allow wr32 to have something to write to
		 * during early allocation of Rx buffers
		 */
		rx_rings[i].tail = vsi->back->hw.hw_addr + PRTGEN_STATUS;

		err = ice_setup_rx_ring(&rx_rings[i]);
		if (err)
			goto rx_unwind;

		/* allocate Rx buffers */
		err = ice_alloc_rx_bufs(&rx_rings[i],
					ICE_DESC_UNUSED(&rx_rings[i]));
rx_unwind:
		if (err) {
			while (i) {
				i--;
				ice_free_rx_ring(&rx_rings[i]);
			}
			devm_kfree(&pf->pdev->dev, rx_rings);
			err = -ENOMEM;
			goto free_tx;
		}
	}

process_link:
	/* Bring interface down, copy in the new ring info, then restore the
	 * interface. if VSI is up, bring it down and then back up
	 */
	if (!test_and_set_bit(__ICE_DOWN, vsi->state)) {
		ice_down(vsi);

		if (tx_rings) {
			for (i = 0; i < vsi->alloc_txq; i++) {
				ice_free_tx_ring(vsi->tx_rings[i]);
				*vsi->tx_rings[i] = tx_rings[i];
			}
			devm_kfree(&pf->pdev->dev, tx_rings);
		}

		if (rx_rings) {
			for (i = 0; i < vsi->alloc_rxq; i++) {
				ice_free_rx_ring(vsi->rx_rings[i]);
				/* copy the real tail offset */
				rx_rings[i].tail = vsi->rx_rings[i]->tail;
				/* this is to fake out the allocation routine
				 * into thinking it has to realloc everything
				 * but the recycling logic will let us re-use
				 * the buffers allocated above
				 */
				rx_rings[i].next_to_use = 0;
				rx_rings[i].next_to_clean = 0;
				rx_rings[i].next_to_alloc = 0;
				*vsi->rx_rings[i] = rx_rings[i];
			}
			devm_kfree(&pf->pdev->dev, rx_rings);
		}

		ice_up(vsi);
	}
	goto done;

free_tx:
	/* error cleanup if the Rx allocations failed after getting Tx */
	if (tx_rings) {
		for (i = 0; i < vsi->alloc_txq; i++)
			ice_free_tx_ring(&tx_rings[i]);
		devm_kfree(&pf->pdev->dev, tx_rings);
	}

done:
	clear_bit(__ICE_CFG_BUSY, pf->state);
	return err;
}

static int ice_nway_reset(struct net_device *netdev)
{
	/* restart autonegotiation */
	struct ice_netdev_priv *np = netdev_priv(netdev);
	struct ice_link_status *hw_link_info;
	struct ice_vsi *vsi = np->vsi;
	struct ice_port_info *pi;
	enum ice_status status;
	bool link_up;

	pi = vsi->port_info;
	hw_link_info = &pi->phy.link_info;
	link_up = hw_link_info->link_info & ICE_AQ_LINK_UP;

	status = ice_aq_set_link_restart_an(pi, link_up, NULL);
	if (status) {
		netdev_info(netdev, "link restart failed, err %d aq_err %d\n",
			    status, pi->hw->adminq.sq_last_status);
		return -EIO;
	}

	return 0;
}

/**
 * ice_get_pauseparam - Get Flow Control status
 * @netdev: network interface device structure
 * @pause: ethernet pause (flow control) parameters
 */
static void
ice_get_pauseparam(struct net_device *netdev, struct ethtool_pauseparam *pause)
{
	struct ice_netdev_priv *np = netdev_priv(netdev);
	struct ice_port_info *pi;

	pi = np->vsi->port_info;
	pause->autoneg =
		((pi->phy.link_info.an_info & ICE_AQ_AN_COMPLETED) ?
		 AUTONEG_ENABLE : AUTONEG_DISABLE);

	if (pi->fc.current_mode == ICE_FC_RX_PAUSE) {
		pause->rx_pause = 1;
	} else if (pi->fc.current_mode == ICE_FC_TX_PAUSE) {
		pause->tx_pause = 1;
	} else if (pi->fc.current_mode == ICE_FC_FULL) {
		pause->rx_pause = 1;
		pause->tx_pause = 1;
	}
}

/**
 * ice_set_pauseparam - Set Flow Control parameter
 * @netdev: network interface device structure
 * @pause: return tx/rx flow control status
 */
static int
ice_set_pauseparam(struct net_device *netdev, struct ethtool_pauseparam *pause)
{
	struct ice_netdev_priv *np = netdev_priv(netdev);
	struct ice_link_status *hw_link_info;
	struct ice_pf *pf = np->vsi->back;
	struct ice_vsi *vsi = np->vsi;
	struct ice_hw *hw = &pf->hw;
	struct ice_port_info *pi;
	enum ice_status status;
	u8 aq_failures;
	bool link_up;
	int err = 0;

	pi = vsi->port_info;
	hw_link_info = &pi->phy.link_info;
	link_up = hw_link_info->link_info & ICE_AQ_LINK_UP;

	/* Changing the port's flow control is not supported if this isn't the
	 * PF VSI
	 */
	if (vsi->type != ICE_VSI_PF) {
		netdev_info(netdev, "Changing flow control parameters only supported for PF VSI\n");
		return -EOPNOTSUPP;
	}

	if (pause->autoneg != (hw_link_info->an_info & ICE_AQ_AN_COMPLETED)) {
		netdev_info(netdev, "To change autoneg please use: ethtool -s <dev> autoneg <on|off>\n");
		return -EOPNOTSUPP;
	}

	/* If we have link and don't have autoneg */
	if (!test_bit(__ICE_DOWN, pf->state) &&
	    !(hw_link_info->an_info & ICE_AQ_AN_COMPLETED)) {
		/* Send message that it might not necessarily work*/
		netdev_info(netdev, "Autoneg did not complete so changing settings may not result in an actual change.\n");
	}

	if (pause->rx_pause && pause->tx_pause)
		pi->fc.req_mode = ICE_FC_FULL;
	else if (pause->rx_pause && !pause->tx_pause)
		pi->fc.req_mode = ICE_FC_RX_PAUSE;
	else if (!pause->rx_pause && pause->tx_pause)
		pi->fc.req_mode = ICE_FC_TX_PAUSE;
	else if (!pause->rx_pause && !pause->tx_pause)
		pi->fc.req_mode = ICE_FC_NONE;
	else
		return -EINVAL;

	/* Tell the OS link is going down, the link will go back up when fw
	 * says it is ready asynchronously
	 */
	ice_print_link_msg(vsi, false);
	netif_carrier_off(netdev);
	netif_tx_stop_all_queues(netdev);

	/* Set the FC mode and only restart AN if link is up */
	status = ice_set_fc(pi, &aq_failures, link_up);

	if (aq_failures & ICE_SET_FC_AQ_FAIL_GET) {
		netdev_info(netdev, "Set fc failed on the get_phy_capabilities call with err %d aq_err %d\n",
			    status, hw->adminq.sq_last_status);
		err = -EAGAIN;
	} else if (aq_failures & ICE_SET_FC_AQ_FAIL_SET) {
		netdev_info(netdev, "Set fc failed on the set_phy_config call with err %d aq_err %d\n",
			    status, hw->adminq.sq_last_status);
		err = -EAGAIN;
	} else if (aq_failures & ICE_SET_FC_AQ_FAIL_UPDATE) {
		netdev_info(netdev, "Set fc failed on the get_link_info call with err %d aq_err %d\n",
			    status, hw->adminq.sq_last_status);
		err = -EAGAIN;
	}

	if (!test_bit(__ICE_DOWN, pf->state)) {
		/* Give it a little more time to try to come back */
		msleep(75);
		if (!test_bit(__ICE_DOWN, pf->state))
			return ice_nway_reset(netdev);
	}

	return err;
}

/**
 * ice_get_rxfh_key_size - get the RSS hash key size
 * @netdev: network interface device structure
 *
 * Returns the table size.
 */
static u32 ice_get_rxfh_key_size(struct net_device __always_unused *netdev)
{
	return ICE_VSIQF_HKEY_ARRAY_SIZE;
}

/**
 * ice_get_rxfh_indir_size - get the rx flow hash indirection table size
 * @netdev: network interface device structure
 *
 * Returns the table size.
 */
static u32 ice_get_rxfh_indir_size(struct net_device *netdev)
{
	struct ice_netdev_priv *np = netdev_priv(netdev);

	return np->vsi->rss_table_size;
}

/**
 * ice_get_rxfh - get the rx flow hash indirection table
 * @netdev: network interface device structure
 * @indir: indirection table
 * @key: hash key
 * @hfunc: hash function
 *
 * Reads the indirection table directly from the hardware.
 */
static int
ice_get_rxfh(struct net_device *netdev, u32 *indir, u8 *key, u8 *hfunc)
{
	struct ice_netdev_priv *np = netdev_priv(netdev);
	struct ice_vsi *vsi = np->vsi;
	struct ice_pf *pf = vsi->back;
	int ret = 0, i;
	u8 *lut;

	if (hfunc)
		*hfunc = ETH_RSS_HASH_TOP;

	if (!indir)
		return 0;

	if (!test_bit(ICE_FLAG_RSS_ENA, pf->flags)) {
		/* RSS not supported return error here */
		netdev_warn(netdev, "RSS is not configured on this VSI!\n");
		return -EIO;
	}

	lut = devm_kzalloc(&pf->pdev->dev, vsi->rss_table_size, GFP_KERNEL);
	if (!lut)
		return -ENOMEM;

	if (ice_get_rss(vsi, key, lut, vsi->rss_table_size)) {
		ret = -EIO;
		goto out;
	}

	for (i = 0; i < vsi->rss_table_size; i++)
		indir[i] = (u32)(lut[i]);

out:
	devm_kfree(&pf->pdev->dev, lut);
	return ret;
}

/**
 * ice_set_rxfh - set the rx flow hash indirection table
 * @netdev: network interface device structure
 * @indir: indirection table
 * @key: hash key
 * @hfunc: hash function
 *
 * Returns -EINVAL if the table specifies an invalid queue id, otherwise
 * returns 0 after programming the table.
 */
static int ice_set_rxfh(struct net_device *netdev, const u32 *indir,
			const u8 *key, const u8 hfunc)
{
	struct ice_netdev_priv *np = netdev_priv(netdev);
	struct ice_vsi *vsi = np->vsi;
	struct ice_pf *pf = vsi->back;
	u8 *seed = NULL;

	if (hfunc != ETH_RSS_HASH_NO_CHANGE && hfunc != ETH_RSS_HASH_TOP)
		return -EOPNOTSUPP;

	if (!test_bit(ICE_FLAG_RSS_ENA, pf->flags)) {
		/* RSS not supported return error here */
		netdev_warn(netdev, "RSS is not configured on this VSI!\n");
		return -EIO;
	}

	if (key) {
		if (!vsi->rss_hkey_user) {
			vsi->rss_hkey_user =
				devm_kzalloc(&pf->pdev->dev,
					     ICE_VSIQF_HKEY_ARRAY_SIZE,
					     GFP_KERNEL);
			if (!vsi->rss_hkey_user)
				return -ENOMEM;
		}
		memcpy(vsi->rss_hkey_user, key, ICE_VSIQF_HKEY_ARRAY_SIZE);
		seed = vsi->rss_hkey_user;
	}

	if (!vsi->rss_lut_user) {
		vsi->rss_lut_user = devm_kzalloc(&pf->pdev->dev,
						 vsi->rss_table_size,
						 GFP_KERNEL);
		if (!vsi->rss_lut_user)
			return -ENOMEM;
	}

	/* Each 32 bits pointed by 'indir' is stored with a lut entry */
	if (indir) {
		int i;

		for (i = 0; i < vsi->rss_table_size; i++)
			vsi->rss_lut_user[i] = (u8)(indir[i]);
	} else {
		ice_fill_rss_lut(vsi->rss_lut_user, vsi->rss_table_size,
				 vsi->rss_size);
	}

	if (ice_set_rss(vsi, seed, vsi->rss_lut_user, vsi->rss_table_size))
		return -EIO;

	return 0;
}

static const struct ethtool_ops ice_ethtool_ops = {
	.get_link_ksettings	= ice_get_link_ksettings,
	.get_drvinfo            = ice_get_drvinfo,
	.get_regs_len           = ice_get_regs_len,
	.get_regs               = ice_get_regs,
	.get_msglevel           = ice_get_msglevel,
	.set_msglevel           = ice_set_msglevel,
	.get_link		= ethtool_op_get_link,
	.get_strings		= ice_get_strings,
	.get_ethtool_stats      = ice_get_ethtool_stats,
	.get_sset_count		= ice_get_sset_count,
	.get_rxnfc		= ice_get_rxnfc,
	.get_ringparam		= ice_get_ringparam,
	.set_ringparam		= ice_set_ringparam,
	.nway_reset		= ice_nway_reset,
	.get_pauseparam		= ice_get_pauseparam,
	.set_pauseparam		= ice_set_pauseparam,
	.get_rxfh_key_size	= ice_get_rxfh_key_size,
	.get_rxfh_indir_size	= ice_get_rxfh_indir_size,
	.get_rxfh		= ice_get_rxfh,
	.set_rxfh		= ice_set_rxfh,
};

/**
 * ice_set_ethtool_ops - setup netdev ethtool ops
 * @netdev: network interface device structure
 *
 * setup netdev ethtool ops with ice specific ops
 */
void ice_set_ethtool_ops(struct net_device *netdev)
{
	netdev->ethtool_ops = &ice_ethtool_ops;
}

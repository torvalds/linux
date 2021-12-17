// SPDX-License-Identifier: GPL-2.0
/*
 * DPAA2 Ethernet Switch driver
 *
 * Copyright 2014-2016 Freescale Semiconductor Inc.
 * Copyright 2017-2021 NXP
 *
 */

#include <linux/module.h>

#include <linux/interrupt.h>
#include <linux/msi.h>
#include <linux/kthread.h>
#include <linux/workqueue.h>
#include <linux/iommu.h>
#include <net/pkt_cls.h>

#include <linux/fsl/mc.h>

#include "dpaa2-switch.h"

/* Minimal supported DPSW version */
#define DPSW_MIN_VER_MAJOR		8
#define DPSW_MIN_VER_MINOR		9

#define DEFAULT_VLAN_ID			1

static u16 dpaa2_switch_port_get_fdb_id(struct ethsw_port_priv *port_priv)
{
	return port_priv->fdb->fdb_id;
}

static struct dpaa2_switch_fdb *dpaa2_switch_fdb_get_unused(struct ethsw_core *ethsw)
{
	int i;

	for (i = 0; i < ethsw->sw_attr.num_ifs; i++)
		if (!ethsw->fdbs[i].in_use)
			return &ethsw->fdbs[i];
	return NULL;
}

static struct dpaa2_switch_filter_block *
dpaa2_switch_filter_block_get_unused(struct ethsw_core *ethsw)
{
	int i;

	for (i = 0; i < ethsw->sw_attr.num_ifs; i++)
		if (!ethsw->filter_blocks[i].in_use)
			return &ethsw->filter_blocks[i];
	return NULL;
}

static u16 dpaa2_switch_port_set_fdb(struct ethsw_port_priv *port_priv,
				     struct net_device *bridge_dev)
{
	struct ethsw_port_priv *other_port_priv = NULL;
	struct dpaa2_switch_fdb *fdb;
	struct net_device *other_dev;
	struct list_head *iter;

	/* If we leave a bridge (bridge_dev is NULL), find an unused
	 * FDB and use that.
	 */
	if (!bridge_dev) {
		fdb = dpaa2_switch_fdb_get_unused(port_priv->ethsw_data);

		/* If there is no unused FDB, we must be the last port that
		 * leaves the last bridge, all the others are standalone. We
		 * can just keep the FDB that we already have.
		 */

		if (!fdb) {
			port_priv->fdb->bridge_dev = NULL;
			return 0;
		}

		port_priv->fdb = fdb;
		port_priv->fdb->in_use = true;
		port_priv->fdb->bridge_dev = NULL;
		return 0;
	}

	/* The below call to netdev_for_each_lower_dev() demands the RTNL lock
	 * being held. Assert on it so that it's easier to catch new code
	 * paths that reach this point without the RTNL lock.
	 */
	ASSERT_RTNL();

	/* If part of a bridge, use the FDB of the first dpaa2 switch interface
	 * to be present in that bridge
	 */
	netdev_for_each_lower_dev(bridge_dev, other_dev, iter) {
		if (!dpaa2_switch_port_dev_check(other_dev))
			continue;

		if (other_dev == port_priv->netdev)
			continue;

		other_port_priv = netdev_priv(other_dev);
		break;
	}

	/* The current port is about to change its FDB to the one used by the
	 * first port that joined the bridge.
	 */
	if (other_port_priv) {
		/* The previous FDB is about to become unused, since the
		 * interface is no longer standalone.
		 */
		port_priv->fdb->in_use = false;
		port_priv->fdb->bridge_dev = NULL;

		/* Get a reference to the new FDB */
		port_priv->fdb = other_port_priv->fdb;
	}

	/* Keep track of the new upper bridge device */
	port_priv->fdb->bridge_dev = bridge_dev;

	return 0;
}

static void dpaa2_switch_fdb_get_flood_cfg(struct ethsw_core *ethsw, u16 fdb_id,
					   enum dpsw_flood_type type,
					   struct dpsw_egress_flood_cfg *cfg)
{
	int i = 0, j;

	memset(cfg, 0, sizeof(*cfg));

	/* Add all the DPAA2 switch ports found in the same bridging domain to
	 * the egress flooding domain
	 */
	for (j = 0; j < ethsw->sw_attr.num_ifs; j++) {
		if (!ethsw->ports[j])
			continue;
		if (ethsw->ports[j]->fdb->fdb_id != fdb_id)
			continue;

		if (type == DPSW_BROADCAST && ethsw->ports[j]->bcast_flood)
			cfg->if_id[i++] = ethsw->ports[j]->idx;
		else if (type == DPSW_FLOODING && ethsw->ports[j]->ucast_flood)
			cfg->if_id[i++] = ethsw->ports[j]->idx;
	}

	/* Add the CTRL interface to the egress flooding domain */
	cfg->if_id[i++] = ethsw->sw_attr.num_ifs;

	cfg->fdb_id = fdb_id;
	cfg->flood_type = type;
	cfg->num_ifs = i;
}

static int dpaa2_switch_fdb_set_egress_flood(struct ethsw_core *ethsw, u16 fdb_id)
{
	struct dpsw_egress_flood_cfg flood_cfg;
	int err;

	/* Setup broadcast flooding domain */
	dpaa2_switch_fdb_get_flood_cfg(ethsw, fdb_id, DPSW_BROADCAST, &flood_cfg);
	err = dpsw_set_egress_flood(ethsw->mc_io, 0, ethsw->dpsw_handle,
				    &flood_cfg);
	if (err) {
		dev_err(ethsw->dev, "dpsw_set_egress_flood() = %d\n", err);
		return err;
	}

	/* Setup unknown flooding domain */
	dpaa2_switch_fdb_get_flood_cfg(ethsw, fdb_id, DPSW_FLOODING, &flood_cfg);
	err = dpsw_set_egress_flood(ethsw->mc_io, 0, ethsw->dpsw_handle,
				    &flood_cfg);
	if (err) {
		dev_err(ethsw->dev, "dpsw_set_egress_flood() = %d\n", err);
		return err;
	}

	return 0;
}

static void *dpaa2_iova_to_virt(struct iommu_domain *domain,
				dma_addr_t iova_addr)
{
	phys_addr_t phys_addr;

	phys_addr = domain ? iommu_iova_to_phys(domain, iova_addr) : iova_addr;

	return phys_to_virt(phys_addr);
}

static int dpaa2_switch_add_vlan(struct ethsw_port_priv *port_priv, u16 vid)
{
	struct ethsw_core *ethsw = port_priv->ethsw_data;
	struct dpsw_vlan_cfg vcfg = {0};
	int err;

	vcfg.fdb_id = dpaa2_switch_port_get_fdb_id(port_priv);
	err = dpsw_vlan_add(ethsw->mc_io, 0,
			    ethsw->dpsw_handle, vid, &vcfg);
	if (err) {
		dev_err(ethsw->dev, "dpsw_vlan_add err %d\n", err);
		return err;
	}
	ethsw->vlans[vid] = ETHSW_VLAN_MEMBER;

	return 0;
}

static bool dpaa2_switch_port_is_up(struct ethsw_port_priv *port_priv)
{
	struct net_device *netdev = port_priv->netdev;
	struct dpsw_link_state state;
	int err;

	err = dpsw_if_get_link_state(port_priv->ethsw_data->mc_io, 0,
				     port_priv->ethsw_data->dpsw_handle,
				     port_priv->idx, &state);
	if (err) {
		netdev_err(netdev, "dpsw_if_get_link_state() err %d\n", err);
		return true;
	}

	WARN_ONCE(state.up > 1, "Garbage read into link_state");

	return state.up ? true : false;
}

static int dpaa2_switch_port_set_pvid(struct ethsw_port_priv *port_priv, u16 pvid)
{
	struct ethsw_core *ethsw = port_priv->ethsw_data;
	struct net_device *netdev = port_priv->netdev;
	struct dpsw_tci_cfg tci_cfg = { 0 };
	bool up;
	int err, ret;

	err = dpsw_if_get_tci(ethsw->mc_io, 0, ethsw->dpsw_handle,
			      port_priv->idx, &tci_cfg);
	if (err) {
		netdev_err(netdev, "dpsw_if_get_tci err %d\n", err);
		return err;
	}

	tci_cfg.vlan_id = pvid;

	/* Interface needs to be down to change PVID */
	up = dpaa2_switch_port_is_up(port_priv);
	if (up) {
		err = dpsw_if_disable(ethsw->mc_io, 0,
				      ethsw->dpsw_handle,
				      port_priv->idx);
		if (err) {
			netdev_err(netdev, "dpsw_if_disable err %d\n", err);
			return err;
		}
	}

	err = dpsw_if_set_tci(ethsw->mc_io, 0, ethsw->dpsw_handle,
			      port_priv->idx, &tci_cfg);
	if (err) {
		netdev_err(netdev, "dpsw_if_set_tci err %d\n", err);
		goto set_tci_error;
	}

	/* Delete previous PVID info and mark the new one */
	port_priv->vlans[port_priv->pvid] &= ~ETHSW_VLAN_PVID;
	port_priv->vlans[pvid] |= ETHSW_VLAN_PVID;
	port_priv->pvid = pvid;

set_tci_error:
	if (up) {
		ret = dpsw_if_enable(ethsw->mc_io, 0,
				     ethsw->dpsw_handle,
				     port_priv->idx);
		if (ret) {
			netdev_err(netdev, "dpsw_if_enable err %d\n", ret);
			return ret;
		}
	}

	return err;
}

static int dpaa2_switch_port_add_vlan(struct ethsw_port_priv *port_priv,
				      u16 vid, u16 flags)
{
	struct ethsw_core *ethsw = port_priv->ethsw_data;
	struct net_device *netdev = port_priv->netdev;
	struct dpsw_vlan_if_cfg vcfg = {0};
	int err;

	if (port_priv->vlans[vid]) {
		netdev_warn(netdev, "VLAN %d already configured\n", vid);
		return -EEXIST;
	}

	/* If hit, this VLAN rule will lead the packet into the FDB table
	 * specified in the vlan configuration below
	 */
	vcfg.num_ifs = 1;
	vcfg.if_id[0] = port_priv->idx;
	vcfg.fdb_id = dpaa2_switch_port_get_fdb_id(port_priv);
	vcfg.options |= DPSW_VLAN_ADD_IF_OPT_FDB_ID;
	err = dpsw_vlan_add_if(ethsw->mc_io, 0, ethsw->dpsw_handle, vid, &vcfg);
	if (err) {
		netdev_err(netdev, "dpsw_vlan_add_if err %d\n", err);
		return err;
	}

	port_priv->vlans[vid] = ETHSW_VLAN_MEMBER;

	if (flags & BRIDGE_VLAN_INFO_UNTAGGED) {
		err = dpsw_vlan_add_if_untagged(ethsw->mc_io, 0,
						ethsw->dpsw_handle,
						vid, &vcfg);
		if (err) {
			netdev_err(netdev,
				   "dpsw_vlan_add_if_untagged err %d\n", err);
			return err;
		}
		port_priv->vlans[vid] |= ETHSW_VLAN_UNTAGGED;
	}

	if (flags & BRIDGE_VLAN_INFO_PVID) {
		err = dpaa2_switch_port_set_pvid(port_priv, vid);
		if (err)
			return err;
	}

	return 0;
}

static enum dpsw_stp_state br_stp_state_to_dpsw(u8 state)
{
	switch (state) {
	case BR_STATE_DISABLED:
		return DPSW_STP_STATE_DISABLED;
	case BR_STATE_LISTENING:
		return DPSW_STP_STATE_LISTENING;
	case BR_STATE_LEARNING:
		return DPSW_STP_STATE_LEARNING;
	case BR_STATE_FORWARDING:
		return DPSW_STP_STATE_FORWARDING;
	case BR_STATE_BLOCKING:
		return DPSW_STP_STATE_BLOCKING;
	default:
		return DPSW_STP_STATE_DISABLED;
	}
}

static int dpaa2_switch_port_set_stp_state(struct ethsw_port_priv *port_priv, u8 state)
{
	struct dpsw_stp_cfg stp_cfg = {0};
	int err;
	u16 vid;

	if (!netif_running(port_priv->netdev) || state == port_priv->stp_state)
		return 0;	/* Nothing to do */

	stp_cfg.state = br_stp_state_to_dpsw(state);
	for (vid = 0; vid <= VLAN_VID_MASK; vid++) {
		if (port_priv->vlans[vid] & ETHSW_VLAN_MEMBER) {
			stp_cfg.vlan_id = vid;
			err = dpsw_if_set_stp(port_priv->ethsw_data->mc_io, 0,
					      port_priv->ethsw_data->dpsw_handle,
					      port_priv->idx, &stp_cfg);
			if (err) {
				netdev_err(port_priv->netdev,
					   "dpsw_if_set_stp err %d\n", err);
				return err;
			}
		}
	}

	port_priv->stp_state = state;

	return 0;
}

static int dpaa2_switch_dellink(struct ethsw_core *ethsw, u16 vid)
{
	struct ethsw_port_priv *ppriv_local = NULL;
	int i, err;

	if (!ethsw->vlans[vid])
		return -ENOENT;

	err = dpsw_vlan_remove(ethsw->mc_io, 0, ethsw->dpsw_handle, vid);
	if (err) {
		dev_err(ethsw->dev, "dpsw_vlan_remove err %d\n", err);
		return err;
	}
	ethsw->vlans[vid] = 0;

	for (i = 0; i < ethsw->sw_attr.num_ifs; i++) {
		ppriv_local = ethsw->ports[i];
		ppriv_local->vlans[vid] = 0;
	}

	return 0;
}

static int dpaa2_switch_port_fdb_add_uc(struct ethsw_port_priv *port_priv,
					const unsigned char *addr)
{
	struct dpsw_fdb_unicast_cfg entry = {0};
	u16 fdb_id;
	int err;

	entry.if_egress = port_priv->idx;
	entry.type = DPSW_FDB_ENTRY_STATIC;
	ether_addr_copy(entry.mac_addr, addr);

	fdb_id = dpaa2_switch_port_get_fdb_id(port_priv);
	err = dpsw_fdb_add_unicast(port_priv->ethsw_data->mc_io, 0,
				   port_priv->ethsw_data->dpsw_handle,
				   fdb_id, &entry);
	if (err)
		netdev_err(port_priv->netdev,
			   "dpsw_fdb_add_unicast err %d\n", err);
	return err;
}

static int dpaa2_switch_port_fdb_del_uc(struct ethsw_port_priv *port_priv,
					const unsigned char *addr)
{
	struct dpsw_fdb_unicast_cfg entry = {0};
	u16 fdb_id;
	int err;

	entry.if_egress = port_priv->idx;
	entry.type = DPSW_FDB_ENTRY_STATIC;
	ether_addr_copy(entry.mac_addr, addr);

	fdb_id = dpaa2_switch_port_get_fdb_id(port_priv);
	err = dpsw_fdb_remove_unicast(port_priv->ethsw_data->mc_io, 0,
				      port_priv->ethsw_data->dpsw_handle,
				      fdb_id, &entry);
	/* Silently discard error for calling multiple times the del command */
	if (err && err != -ENXIO)
		netdev_err(port_priv->netdev,
			   "dpsw_fdb_remove_unicast err %d\n", err);
	return err;
}

static int dpaa2_switch_port_fdb_add_mc(struct ethsw_port_priv *port_priv,
					const unsigned char *addr)
{
	struct dpsw_fdb_multicast_cfg entry = {0};
	u16 fdb_id;
	int err;

	ether_addr_copy(entry.mac_addr, addr);
	entry.type = DPSW_FDB_ENTRY_STATIC;
	entry.num_ifs = 1;
	entry.if_id[0] = port_priv->idx;

	fdb_id = dpaa2_switch_port_get_fdb_id(port_priv);
	err = dpsw_fdb_add_multicast(port_priv->ethsw_data->mc_io, 0,
				     port_priv->ethsw_data->dpsw_handle,
				     fdb_id, &entry);
	/* Silently discard error for calling multiple times the add command */
	if (err && err != -ENXIO)
		netdev_err(port_priv->netdev, "dpsw_fdb_add_multicast err %d\n",
			   err);
	return err;
}

static int dpaa2_switch_port_fdb_del_mc(struct ethsw_port_priv *port_priv,
					const unsigned char *addr)
{
	struct dpsw_fdb_multicast_cfg entry = {0};
	u16 fdb_id;
	int err;

	ether_addr_copy(entry.mac_addr, addr);
	entry.type = DPSW_FDB_ENTRY_STATIC;
	entry.num_ifs = 1;
	entry.if_id[0] = port_priv->idx;

	fdb_id = dpaa2_switch_port_get_fdb_id(port_priv);
	err = dpsw_fdb_remove_multicast(port_priv->ethsw_data->mc_io, 0,
					port_priv->ethsw_data->dpsw_handle,
					fdb_id, &entry);
	/* Silently discard error for calling multiple times the del command */
	if (err && err != -ENAVAIL)
		netdev_err(port_priv->netdev,
			   "dpsw_fdb_remove_multicast err %d\n", err);
	return err;
}

static void dpaa2_switch_port_get_stats(struct net_device *netdev,
					struct rtnl_link_stats64 *stats)
{
	struct ethsw_port_priv *port_priv = netdev_priv(netdev);
	u64 tmp;
	int err;

	err = dpsw_if_get_counter(port_priv->ethsw_data->mc_io, 0,
				  port_priv->ethsw_data->dpsw_handle,
				  port_priv->idx,
				  DPSW_CNT_ING_FRAME, &stats->rx_packets);
	if (err)
		goto error;

	err = dpsw_if_get_counter(port_priv->ethsw_data->mc_io, 0,
				  port_priv->ethsw_data->dpsw_handle,
				  port_priv->idx,
				  DPSW_CNT_EGR_FRAME, &stats->tx_packets);
	if (err)
		goto error;

	err = dpsw_if_get_counter(port_priv->ethsw_data->mc_io, 0,
				  port_priv->ethsw_data->dpsw_handle,
				  port_priv->idx,
				  DPSW_CNT_ING_BYTE, &stats->rx_bytes);
	if (err)
		goto error;

	err = dpsw_if_get_counter(port_priv->ethsw_data->mc_io, 0,
				  port_priv->ethsw_data->dpsw_handle,
				  port_priv->idx,
				  DPSW_CNT_EGR_BYTE, &stats->tx_bytes);
	if (err)
		goto error;

	err = dpsw_if_get_counter(port_priv->ethsw_data->mc_io, 0,
				  port_priv->ethsw_data->dpsw_handle,
				  port_priv->idx,
				  DPSW_CNT_ING_FRAME_DISCARD,
				  &stats->rx_dropped);
	if (err)
		goto error;

	err = dpsw_if_get_counter(port_priv->ethsw_data->mc_io, 0,
				  port_priv->ethsw_data->dpsw_handle,
				  port_priv->idx,
				  DPSW_CNT_ING_FLTR_FRAME,
				  &tmp);
	if (err)
		goto error;
	stats->rx_dropped += tmp;

	err = dpsw_if_get_counter(port_priv->ethsw_data->mc_io, 0,
				  port_priv->ethsw_data->dpsw_handle,
				  port_priv->idx,
				  DPSW_CNT_EGR_FRAME_DISCARD,
				  &stats->tx_dropped);
	if (err)
		goto error;

	return;

error:
	netdev_err(netdev, "dpsw_if_get_counter err %d\n", err);
}

static bool dpaa2_switch_port_has_offload_stats(const struct net_device *netdev,
						int attr_id)
{
	return (attr_id == IFLA_OFFLOAD_XSTATS_CPU_HIT);
}

static int dpaa2_switch_port_get_offload_stats(int attr_id,
					       const struct net_device *netdev,
					       void *sp)
{
	switch (attr_id) {
	case IFLA_OFFLOAD_XSTATS_CPU_HIT:
		dpaa2_switch_port_get_stats((struct net_device *)netdev, sp);
		return 0;
	}

	return -EINVAL;
}

static int dpaa2_switch_port_change_mtu(struct net_device *netdev, int mtu)
{
	struct ethsw_port_priv *port_priv = netdev_priv(netdev);
	int err;

	err = dpsw_if_set_max_frame_length(port_priv->ethsw_data->mc_io,
					   0,
					   port_priv->ethsw_data->dpsw_handle,
					   port_priv->idx,
					   (u16)ETHSW_L2_MAX_FRM(mtu));
	if (err) {
		netdev_err(netdev,
			   "dpsw_if_set_max_frame_length() err %d\n", err);
		return err;
	}

	netdev->mtu = mtu;
	return 0;
}

static int dpaa2_switch_port_link_state_update(struct net_device *netdev)
{
	struct ethsw_port_priv *port_priv = netdev_priv(netdev);
	struct dpsw_link_state state;
	int err;

	/* When we manage the MAC/PHY using phylink there is no need
	 * to manually update the netif_carrier.
	 */
	if (dpaa2_switch_port_is_type_phy(port_priv))
		return 0;

	/* Interrupts are received even though no one issued an 'ifconfig up'
	 * on the switch interface. Ignore these link state update interrupts
	 */
	if (!netif_running(netdev))
		return 0;

	err = dpsw_if_get_link_state(port_priv->ethsw_data->mc_io, 0,
				     port_priv->ethsw_data->dpsw_handle,
				     port_priv->idx, &state);
	if (err) {
		netdev_err(netdev, "dpsw_if_get_link_state() err %d\n", err);
		return err;
	}

	WARN_ONCE(state.up > 1, "Garbage read into link_state");

	if (state.up != port_priv->link_state) {
		if (state.up) {
			netif_carrier_on(netdev);
			netif_tx_start_all_queues(netdev);
		} else {
			netif_carrier_off(netdev);
			netif_tx_stop_all_queues(netdev);
		}
		port_priv->link_state = state.up;
	}

	return 0;
}

/* Manage all NAPI instances for the control interface.
 *
 * We only have one RX queue and one Tx Conf queue for all
 * switch ports. Therefore, we only need to enable the NAPI instance once, the
 * first time one of the switch ports runs .dev_open().
 */

static void dpaa2_switch_enable_ctrl_if_napi(struct ethsw_core *ethsw)
{
	int i;

	/* Access to the ethsw->napi_users relies on the RTNL lock */
	ASSERT_RTNL();

	/* a new interface is using the NAPI instance */
	ethsw->napi_users++;

	/* if there is already a user of the instance, return */
	if (ethsw->napi_users > 1)
		return;

	for (i = 0; i < DPAA2_SWITCH_RX_NUM_FQS; i++)
		napi_enable(&ethsw->fq[i].napi);
}

static void dpaa2_switch_disable_ctrl_if_napi(struct ethsw_core *ethsw)
{
	int i;

	/* Access to the ethsw->napi_users relies on the RTNL lock */
	ASSERT_RTNL();

	/* If we are not the last interface using the NAPI, return */
	ethsw->napi_users--;
	if (ethsw->napi_users)
		return;

	for (i = 0; i < DPAA2_SWITCH_RX_NUM_FQS; i++)
		napi_disable(&ethsw->fq[i].napi);
}

static int dpaa2_switch_port_open(struct net_device *netdev)
{
	struct ethsw_port_priv *port_priv = netdev_priv(netdev);
	struct ethsw_core *ethsw = port_priv->ethsw_data;
	int err;

	if (!dpaa2_switch_port_is_type_phy(port_priv)) {
		/* Explicitly set carrier off, otherwise
		 * netif_carrier_ok() will return true and cause 'ip link show'
		 * to report the LOWER_UP flag, even though the link
		 * notification wasn't even received.
		 */
		netif_carrier_off(netdev);
	}

	err = dpsw_if_enable(port_priv->ethsw_data->mc_io, 0,
			     port_priv->ethsw_data->dpsw_handle,
			     port_priv->idx);
	if (err) {
		netdev_err(netdev, "dpsw_if_enable err %d\n", err);
		return err;
	}

	dpaa2_switch_enable_ctrl_if_napi(ethsw);

	if (dpaa2_switch_port_is_type_phy(port_priv))
		phylink_start(port_priv->mac->phylink);

	return 0;
}

static int dpaa2_switch_port_stop(struct net_device *netdev)
{
	struct ethsw_port_priv *port_priv = netdev_priv(netdev);
	struct ethsw_core *ethsw = port_priv->ethsw_data;
	int err;

	if (dpaa2_switch_port_is_type_phy(port_priv)) {
		phylink_stop(port_priv->mac->phylink);
	} else {
		netif_tx_stop_all_queues(netdev);
		netif_carrier_off(netdev);
	}

	err = dpsw_if_disable(port_priv->ethsw_data->mc_io, 0,
			      port_priv->ethsw_data->dpsw_handle,
			      port_priv->idx);
	if (err) {
		netdev_err(netdev, "dpsw_if_disable err %d\n", err);
		return err;
	}

	dpaa2_switch_disable_ctrl_if_napi(ethsw);

	return 0;
}

static int dpaa2_switch_port_parent_id(struct net_device *dev,
				       struct netdev_phys_item_id *ppid)
{
	struct ethsw_port_priv *port_priv = netdev_priv(dev);

	ppid->id_len = 1;
	ppid->id[0] = port_priv->ethsw_data->dev_id;

	return 0;
}

static int dpaa2_switch_port_get_phys_name(struct net_device *netdev, char *name,
					   size_t len)
{
	struct ethsw_port_priv *port_priv = netdev_priv(netdev);
	int err;

	err = snprintf(name, len, "p%d", port_priv->idx);
	if (err >= len)
		return -EINVAL;

	return 0;
}

struct ethsw_dump_ctx {
	struct net_device *dev;
	struct sk_buff *skb;
	struct netlink_callback *cb;
	int idx;
};

static int dpaa2_switch_fdb_dump_nl(struct fdb_dump_entry *entry,
				    struct ethsw_dump_ctx *dump)
{
	int is_dynamic = entry->type & DPSW_FDB_ENTRY_DINAMIC;
	u32 portid = NETLINK_CB(dump->cb->skb).portid;
	u32 seq = dump->cb->nlh->nlmsg_seq;
	struct nlmsghdr *nlh;
	struct ndmsg *ndm;

	if (dump->idx < dump->cb->args[2])
		goto skip;

	nlh = nlmsg_put(dump->skb, portid, seq, RTM_NEWNEIGH,
			sizeof(*ndm), NLM_F_MULTI);
	if (!nlh)
		return -EMSGSIZE;

	ndm = nlmsg_data(nlh);
	ndm->ndm_family  = AF_BRIDGE;
	ndm->ndm_pad1    = 0;
	ndm->ndm_pad2    = 0;
	ndm->ndm_flags   = NTF_SELF;
	ndm->ndm_type    = 0;
	ndm->ndm_ifindex = dump->dev->ifindex;
	ndm->ndm_state   = is_dynamic ? NUD_REACHABLE : NUD_NOARP;

	if (nla_put(dump->skb, NDA_LLADDR, ETH_ALEN, entry->mac_addr))
		goto nla_put_failure;

	nlmsg_end(dump->skb, nlh);

skip:
	dump->idx++;
	return 0;

nla_put_failure:
	nlmsg_cancel(dump->skb, nlh);
	return -EMSGSIZE;
}

static int dpaa2_switch_port_fdb_valid_entry(struct fdb_dump_entry *entry,
					     struct ethsw_port_priv *port_priv)
{
	int idx = port_priv->idx;
	int valid;

	if (entry->type & DPSW_FDB_ENTRY_TYPE_UNICAST)
		valid = entry->if_info == port_priv->idx;
	else
		valid = entry->if_mask[idx / 8] & BIT(idx % 8);

	return valid;
}

static int dpaa2_switch_fdb_iterate(struct ethsw_port_priv *port_priv,
				    dpaa2_switch_fdb_cb_t cb, void *data)
{
	struct net_device *net_dev = port_priv->netdev;
	struct ethsw_core *ethsw = port_priv->ethsw_data;
	struct device *dev = net_dev->dev.parent;
	struct fdb_dump_entry *fdb_entries;
	struct fdb_dump_entry fdb_entry;
	dma_addr_t fdb_dump_iova;
	u16 num_fdb_entries;
	u32 fdb_dump_size;
	int err = 0, i;
	u8 *dma_mem;
	u16 fdb_id;

	fdb_dump_size = ethsw->sw_attr.max_fdb_entries * sizeof(fdb_entry);
	dma_mem = kzalloc(fdb_dump_size, GFP_KERNEL);
	if (!dma_mem)
		return -ENOMEM;

	fdb_dump_iova = dma_map_single(dev, dma_mem, fdb_dump_size,
				       DMA_FROM_DEVICE);
	if (dma_mapping_error(dev, fdb_dump_iova)) {
		netdev_err(net_dev, "dma_map_single() failed\n");
		err = -ENOMEM;
		goto err_map;
	}

	fdb_id = dpaa2_switch_port_get_fdb_id(port_priv);
	err = dpsw_fdb_dump(ethsw->mc_io, 0, ethsw->dpsw_handle, fdb_id,
			    fdb_dump_iova, fdb_dump_size, &num_fdb_entries);
	if (err) {
		netdev_err(net_dev, "dpsw_fdb_dump() = %d\n", err);
		goto err_dump;
	}

	dma_unmap_single(dev, fdb_dump_iova, fdb_dump_size, DMA_FROM_DEVICE);

	fdb_entries = (struct fdb_dump_entry *)dma_mem;
	for (i = 0; i < num_fdb_entries; i++) {
		fdb_entry = fdb_entries[i];

		err = cb(port_priv, &fdb_entry, data);
		if (err)
			goto end;
	}

end:
	kfree(dma_mem);

	return 0;

err_dump:
	dma_unmap_single(dev, fdb_dump_iova, fdb_dump_size, DMA_TO_DEVICE);
err_map:
	kfree(dma_mem);
	return err;
}

static int dpaa2_switch_fdb_entry_dump(struct ethsw_port_priv *port_priv,
				       struct fdb_dump_entry *fdb_entry,
				       void *data)
{
	if (!dpaa2_switch_port_fdb_valid_entry(fdb_entry, port_priv))
		return 0;

	return dpaa2_switch_fdb_dump_nl(fdb_entry, data);
}

static int dpaa2_switch_port_fdb_dump(struct sk_buff *skb, struct netlink_callback *cb,
				      struct net_device *net_dev,
				      struct net_device *filter_dev, int *idx)
{
	struct ethsw_port_priv *port_priv = netdev_priv(net_dev);
	struct ethsw_dump_ctx dump = {
		.dev = net_dev,
		.skb = skb,
		.cb = cb,
		.idx = *idx,
	};
	int err;

	err = dpaa2_switch_fdb_iterate(port_priv, dpaa2_switch_fdb_entry_dump, &dump);
	*idx = dump.idx;

	return err;
}

static int dpaa2_switch_fdb_entry_fast_age(struct ethsw_port_priv *port_priv,
					   struct fdb_dump_entry *fdb_entry,
					   void *data __always_unused)
{
	if (!dpaa2_switch_port_fdb_valid_entry(fdb_entry, port_priv))
		return 0;

	if (!(fdb_entry->type & DPSW_FDB_ENTRY_TYPE_DYNAMIC))
		return 0;

	if (fdb_entry->type & DPSW_FDB_ENTRY_TYPE_UNICAST)
		dpaa2_switch_port_fdb_del_uc(port_priv, fdb_entry->mac_addr);
	else
		dpaa2_switch_port_fdb_del_mc(port_priv, fdb_entry->mac_addr);

	return 0;
}

static void dpaa2_switch_port_fast_age(struct ethsw_port_priv *port_priv)
{
	dpaa2_switch_fdb_iterate(port_priv,
				 dpaa2_switch_fdb_entry_fast_age, NULL);
}

static int dpaa2_switch_port_vlan_add(struct net_device *netdev, __be16 proto,
				      u16 vid)
{
	struct switchdev_obj_port_vlan vlan = {
		.obj.id = SWITCHDEV_OBJ_ID_PORT_VLAN,
		.vid = vid,
		.obj.orig_dev = netdev,
		/* This API only allows programming tagged, non-PVID VIDs */
		.flags = 0,
	};

	return dpaa2_switch_port_vlans_add(netdev, &vlan);
}

static int dpaa2_switch_port_vlan_kill(struct net_device *netdev, __be16 proto,
				       u16 vid)
{
	struct switchdev_obj_port_vlan vlan = {
		.obj.id = SWITCHDEV_OBJ_ID_PORT_VLAN,
		.vid = vid,
		.obj.orig_dev = netdev,
		/* This API only allows programming tagged, non-PVID VIDs */
		.flags = 0,
	};

	return dpaa2_switch_port_vlans_del(netdev, &vlan);
}

static int dpaa2_switch_port_set_mac_addr(struct ethsw_port_priv *port_priv)
{
	struct ethsw_core *ethsw = port_priv->ethsw_data;
	struct net_device *net_dev = port_priv->netdev;
	struct device *dev = net_dev->dev.parent;
	u8 mac_addr[ETH_ALEN];
	int err;

	if (!(ethsw->features & ETHSW_FEATURE_MAC_ADDR))
		return 0;

	/* Get firmware address, if any */
	err = dpsw_if_get_port_mac_addr(ethsw->mc_io, 0, ethsw->dpsw_handle,
					port_priv->idx, mac_addr);
	if (err) {
		dev_err(dev, "dpsw_if_get_port_mac_addr() failed\n");
		return err;
	}

	/* First check if firmware has any address configured by bootloader */
	if (!is_zero_ether_addr(mac_addr)) {
		eth_hw_addr_set(net_dev, mac_addr);
	} else {
		/* No MAC address configured, fill in net_dev->dev_addr
		 * with a random one
		 */
		eth_hw_addr_random(net_dev);
		dev_dbg_once(dev, "device(s) have all-zero hwaddr, replaced with random\n");

		/* Override NET_ADDR_RANDOM set by eth_hw_addr_random(); for all
		 * practical purposes, this will be our "permanent" mac address,
		 * at least until the next reboot. This move will also permit
		 * register_netdevice() to properly fill up net_dev->perm_addr.
		 */
		net_dev->addr_assign_type = NET_ADDR_PERM;
	}

	return 0;
}

static void dpaa2_switch_free_fd(const struct ethsw_core *ethsw,
				 const struct dpaa2_fd *fd)
{
	struct device *dev = ethsw->dev;
	unsigned char *buffer_start;
	struct sk_buff **skbh, *skb;
	dma_addr_t fd_addr;

	fd_addr = dpaa2_fd_get_addr(fd);
	skbh = dpaa2_iova_to_virt(ethsw->iommu_domain, fd_addr);

	skb = *skbh;
	buffer_start = (unsigned char *)skbh;

	dma_unmap_single(dev, fd_addr,
			 skb_tail_pointer(skb) - buffer_start,
			 DMA_TO_DEVICE);

	/* Move on with skb release */
	dev_kfree_skb(skb);
}

static int dpaa2_switch_build_single_fd(struct ethsw_core *ethsw,
					struct sk_buff *skb,
					struct dpaa2_fd *fd)
{
	struct device *dev = ethsw->dev;
	struct sk_buff **skbh;
	dma_addr_t addr;
	u8 *buff_start;
	void *hwa;

	buff_start = PTR_ALIGN(skb->data - DPAA2_SWITCH_TX_DATA_OFFSET -
			       DPAA2_SWITCH_TX_BUF_ALIGN,
			       DPAA2_SWITCH_TX_BUF_ALIGN);

	/* Clear FAS to have consistent values for TX confirmation. It is
	 * located in the first 8 bytes of the buffer's hardware annotation
	 * area
	 */
	hwa = buff_start + DPAA2_SWITCH_SWA_SIZE;
	memset(hwa, 0, 8);

	/* Store a backpointer to the skb at the beginning of the buffer
	 * (in the private data area) such that we can release it
	 * on Tx confirm
	 */
	skbh = (struct sk_buff **)buff_start;
	*skbh = skb;

	addr = dma_map_single(dev, buff_start,
			      skb_tail_pointer(skb) - buff_start,
			      DMA_TO_DEVICE);
	if (unlikely(dma_mapping_error(dev, addr)))
		return -ENOMEM;

	/* Setup the FD fields */
	memset(fd, 0, sizeof(*fd));

	dpaa2_fd_set_addr(fd, addr);
	dpaa2_fd_set_offset(fd, (u16)(skb->data - buff_start));
	dpaa2_fd_set_len(fd, skb->len);
	dpaa2_fd_set_format(fd, dpaa2_fd_single);

	return 0;
}

static netdev_tx_t dpaa2_switch_port_tx(struct sk_buff *skb,
					struct net_device *net_dev)
{
	struct ethsw_port_priv *port_priv = netdev_priv(net_dev);
	struct ethsw_core *ethsw = port_priv->ethsw_data;
	int retries = DPAA2_SWITCH_SWP_BUSY_RETRIES;
	struct dpaa2_fd fd;
	int err;

	if (unlikely(skb_headroom(skb) < DPAA2_SWITCH_NEEDED_HEADROOM)) {
		struct sk_buff *ns;

		ns = skb_realloc_headroom(skb, DPAA2_SWITCH_NEEDED_HEADROOM);
		if (unlikely(!ns)) {
			net_err_ratelimited("%s: Error reallocating skb headroom\n", net_dev->name);
			goto err_free_skb;
		}
		dev_consume_skb_any(skb);
		skb = ns;
	}

	/* We'll be holding a back-reference to the skb until Tx confirmation */
	skb = skb_unshare(skb, GFP_ATOMIC);
	if (unlikely(!skb)) {
		/* skb_unshare() has already freed the skb */
		net_err_ratelimited("%s: Error copying the socket buffer\n", net_dev->name);
		goto err_exit;
	}

	/* At this stage, we do not support non-linear skbs so just try to
	 * linearize the skb and if that's not working, just drop the packet.
	 */
	err = skb_linearize(skb);
	if (err) {
		net_err_ratelimited("%s: skb_linearize error (%d)!\n", net_dev->name, err);
		goto err_free_skb;
	}

	err = dpaa2_switch_build_single_fd(ethsw, skb, &fd);
	if (unlikely(err)) {
		net_err_ratelimited("%s: ethsw_build_*_fd() %d\n", net_dev->name, err);
		goto err_free_skb;
	}

	do {
		err = dpaa2_io_service_enqueue_qd(NULL,
						  port_priv->tx_qdid,
						  8, 0, &fd);
		retries--;
	} while (err == -EBUSY && retries);

	if (unlikely(err < 0)) {
		dpaa2_switch_free_fd(ethsw, &fd);
		goto err_exit;
	}

	return NETDEV_TX_OK;

err_free_skb:
	dev_kfree_skb(skb);
err_exit:
	return NETDEV_TX_OK;
}

static int
dpaa2_switch_setup_tc_cls_flower(struct dpaa2_switch_filter_block *filter_block,
				 struct flow_cls_offload *f)
{
	switch (f->command) {
	case FLOW_CLS_REPLACE:
		return dpaa2_switch_cls_flower_replace(filter_block, f);
	case FLOW_CLS_DESTROY:
		return dpaa2_switch_cls_flower_destroy(filter_block, f);
	default:
		return -EOPNOTSUPP;
	}
}

static int
dpaa2_switch_setup_tc_cls_matchall(struct dpaa2_switch_filter_block *block,
				   struct tc_cls_matchall_offload *f)
{
	switch (f->command) {
	case TC_CLSMATCHALL_REPLACE:
		return dpaa2_switch_cls_matchall_replace(block, f);
	case TC_CLSMATCHALL_DESTROY:
		return dpaa2_switch_cls_matchall_destroy(block, f);
	default:
		return -EOPNOTSUPP;
	}
}

static int dpaa2_switch_port_setup_tc_block_cb_ig(enum tc_setup_type type,
						  void *type_data,
						  void *cb_priv)
{
	switch (type) {
	case TC_SETUP_CLSFLOWER:
		return dpaa2_switch_setup_tc_cls_flower(cb_priv, type_data);
	case TC_SETUP_CLSMATCHALL:
		return dpaa2_switch_setup_tc_cls_matchall(cb_priv, type_data);
	default:
		return -EOPNOTSUPP;
	}
}

static LIST_HEAD(dpaa2_switch_block_cb_list);

static int
dpaa2_switch_port_acl_tbl_bind(struct ethsw_port_priv *port_priv,
			       struct dpaa2_switch_filter_block *block)
{
	struct ethsw_core *ethsw = port_priv->ethsw_data;
	struct net_device *netdev = port_priv->netdev;
	struct dpsw_acl_if_cfg acl_if_cfg;
	int err;

	if (port_priv->filter_block)
		return -EINVAL;

	acl_if_cfg.if_id[0] = port_priv->idx;
	acl_if_cfg.num_ifs = 1;
	err = dpsw_acl_add_if(ethsw->mc_io, 0, ethsw->dpsw_handle,
			      block->acl_id, &acl_if_cfg);
	if (err) {
		netdev_err(netdev, "dpsw_acl_add_if err %d\n", err);
		return err;
	}

	block->ports |= BIT(port_priv->idx);
	port_priv->filter_block = block;

	return 0;
}

static int
dpaa2_switch_port_acl_tbl_unbind(struct ethsw_port_priv *port_priv,
				 struct dpaa2_switch_filter_block *block)
{
	struct ethsw_core *ethsw = port_priv->ethsw_data;
	struct net_device *netdev = port_priv->netdev;
	struct dpsw_acl_if_cfg acl_if_cfg;
	int err;

	if (port_priv->filter_block != block)
		return -EINVAL;

	acl_if_cfg.if_id[0] = port_priv->idx;
	acl_if_cfg.num_ifs = 1;
	err = dpsw_acl_remove_if(ethsw->mc_io, 0, ethsw->dpsw_handle,
				 block->acl_id, &acl_if_cfg);
	if (err) {
		netdev_err(netdev, "dpsw_acl_add_if err %d\n", err);
		return err;
	}

	block->ports &= ~BIT(port_priv->idx);
	port_priv->filter_block = NULL;
	return 0;
}

static int dpaa2_switch_port_block_bind(struct ethsw_port_priv *port_priv,
					struct dpaa2_switch_filter_block *block)
{
	struct dpaa2_switch_filter_block *old_block = port_priv->filter_block;
	int err;

	/* Offload all the mirror entries found in the block on this new port
	 * joining it.
	 */
	err = dpaa2_switch_block_offload_mirror(block, port_priv);
	if (err)
		return err;

	/* If the port is already bound to this ACL table then do nothing. This
	 * can happen when this port is the first one to join a tc block
	 */
	if (port_priv->filter_block == block)
		return 0;

	err = dpaa2_switch_port_acl_tbl_unbind(port_priv, old_block);
	if (err)
		return err;

	/* Mark the previous ACL table as being unused if this was the last
	 * port that was using it.
	 */
	if (old_block->ports == 0)
		old_block->in_use = false;

	return dpaa2_switch_port_acl_tbl_bind(port_priv, block);
}

static int
dpaa2_switch_port_block_unbind(struct ethsw_port_priv *port_priv,
			       struct dpaa2_switch_filter_block *block)
{
	struct ethsw_core *ethsw = port_priv->ethsw_data;
	struct dpaa2_switch_filter_block *new_block;
	int err;

	/* Unoffload all the mirror entries found in the block from the
	 * port leaving it.
	 */
	err = dpaa2_switch_block_unoffload_mirror(block, port_priv);
	if (err)
		return err;

	/* We are the last port that leaves a block (an ACL table).
	 * We'll continue to use this table.
	 */
	if (block->ports == BIT(port_priv->idx))
		return 0;

	err = dpaa2_switch_port_acl_tbl_unbind(port_priv, block);
	if (err)
		return err;

	if (block->ports == 0)
		block->in_use = false;

	new_block = dpaa2_switch_filter_block_get_unused(ethsw);
	new_block->in_use = true;
	return dpaa2_switch_port_acl_tbl_bind(port_priv, new_block);
}

static int dpaa2_switch_setup_tc_block_bind(struct net_device *netdev,
					    struct flow_block_offload *f)
{
	struct ethsw_port_priv *port_priv = netdev_priv(netdev);
	struct ethsw_core *ethsw = port_priv->ethsw_data;
	struct dpaa2_switch_filter_block *filter_block;
	struct flow_block_cb *block_cb;
	bool register_block = false;
	int err;

	block_cb = flow_block_cb_lookup(f->block,
					dpaa2_switch_port_setup_tc_block_cb_ig,
					ethsw);

	if (!block_cb) {
		/* If the filter block is not already known, then this port
		 * must be the first to join it. In this case, we can just
		 * continue to use our private table
		 */
		filter_block = port_priv->filter_block;

		block_cb = flow_block_cb_alloc(dpaa2_switch_port_setup_tc_block_cb_ig,
					       ethsw, filter_block, NULL);
		if (IS_ERR(block_cb))
			return PTR_ERR(block_cb);

		register_block = true;
	} else {
		filter_block = flow_block_cb_priv(block_cb);
	}

	flow_block_cb_incref(block_cb);
	err = dpaa2_switch_port_block_bind(port_priv, filter_block);
	if (err)
		goto err_block_bind;

	if (register_block) {
		flow_block_cb_add(block_cb, f);
		list_add_tail(&block_cb->driver_list,
			      &dpaa2_switch_block_cb_list);
	}

	return 0;

err_block_bind:
	if (!flow_block_cb_decref(block_cb))
		flow_block_cb_free(block_cb);
	return err;
}

static void dpaa2_switch_setup_tc_block_unbind(struct net_device *netdev,
					       struct flow_block_offload *f)
{
	struct ethsw_port_priv *port_priv = netdev_priv(netdev);
	struct ethsw_core *ethsw = port_priv->ethsw_data;
	struct dpaa2_switch_filter_block *filter_block;
	struct flow_block_cb *block_cb;
	int err;

	block_cb = flow_block_cb_lookup(f->block,
					dpaa2_switch_port_setup_tc_block_cb_ig,
					ethsw);
	if (!block_cb)
		return;

	filter_block = flow_block_cb_priv(block_cb);
	err = dpaa2_switch_port_block_unbind(port_priv, filter_block);
	if (!err && !flow_block_cb_decref(block_cb)) {
		flow_block_cb_remove(block_cb, f);
		list_del(&block_cb->driver_list);
	}
}

static int dpaa2_switch_setup_tc_block(struct net_device *netdev,
				       struct flow_block_offload *f)
{
	if (f->binder_type != FLOW_BLOCK_BINDER_TYPE_CLSACT_INGRESS)
		return -EOPNOTSUPP;

	f->driver_block_list = &dpaa2_switch_block_cb_list;

	switch (f->command) {
	case FLOW_BLOCK_BIND:
		return dpaa2_switch_setup_tc_block_bind(netdev, f);
	case FLOW_BLOCK_UNBIND:
		dpaa2_switch_setup_tc_block_unbind(netdev, f);
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

static int dpaa2_switch_port_setup_tc(struct net_device *netdev,
				      enum tc_setup_type type,
				      void *type_data)
{
	switch (type) {
	case TC_SETUP_BLOCK: {
		return dpaa2_switch_setup_tc_block(netdev, type_data);
	}
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static const struct net_device_ops dpaa2_switch_port_ops = {
	.ndo_open		= dpaa2_switch_port_open,
	.ndo_stop		= dpaa2_switch_port_stop,

	.ndo_set_mac_address	= eth_mac_addr,
	.ndo_get_stats64	= dpaa2_switch_port_get_stats,
	.ndo_change_mtu		= dpaa2_switch_port_change_mtu,
	.ndo_has_offload_stats	= dpaa2_switch_port_has_offload_stats,
	.ndo_get_offload_stats	= dpaa2_switch_port_get_offload_stats,
	.ndo_fdb_dump		= dpaa2_switch_port_fdb_dump,
	.ndo_vlan_rx_add_vid	= dpaa2_switch_port_vlan_add,
	.ndo_vlan_rx_kill_vid	= dpaa2_switch_port_vlan_kill,

	.ndo_start_xmit		= dpaa2_switch_port_tx,
	.ndo_get_port_parent_id	= dpaa2_switch_port_parent_id,
	.ndo_get_phys_port_name = dpaa2_switch_port_get_phys_name,
	.ndo_setup_tc		= dpaa2_switch_port_setup_tc,
};

bool dpaa2_switch_port_dev_check(const struct net_device *netdev)
{
	return netdev->netdev_ops == &dpaa2_switch_port_ops;
}

static int dpaa2_switch_port_connect_mac(struct ethsw_port_priv *port_priv)
{
	struct fsl_mc_device *dpsw_port_dev, *dpmac_dev;
	struct dpaa2_mac *mac;
	int err;

	dpsw_port_dev = to_fsl_mc_device(port_priv->netdev->dev.parent);
	dpmac_dev = fsl_mc_get_endpoint(dpsw_port_dev, port_priv->idx);

	if (PTR_ERR(dpmac_dev) == -EPROBE_DEFER)
		return PTR_ERR(dpmac_dev);

	if (IS_ERR(dpmac_dev) || dpmac_dev->dev.type != &fsl_mc_bus_dpmac_type)
		return 0;

	mac = kzalloc(sizeof(*mac), GFP_KERNEL);
	if (!mac)
		return -ENOMEM;

	mac->mc_dev = dpmac_dev;
	mac->mc_io = port_priv->ethsw_data->mc_io;
	mac->net_dev = port_priv->netdev;

	err = dpaa2_mac_open(mac);
	if (err)
		goto err_free_mac;
	port_priv->mac = mac;

	if (dpaa2_switch_port_is_type_phy(port_priv)) {
		err = dpaa2_mac_connect(mac);
		if (err) {
			netdev_err(port_priv->netdev,
				   "Error connecting to the MAC endpoint %pe\n",
				   ERR_PTR(err));
			goto err_close_mac;
		}
	}

	return 0;

err_close_mac:
	dpaa2_mac_close(mac);
	port_priv->mac = NULL;
err_free_mac:
	kfree(mac);
	return err;
}

static void dpaa2_switch_port_disconnect_mac(struct ethsw_port_priv *port_priv)
{
	if (dpaa2_switch_port_is_type_phy(port_priv))
		dpaa2_mac_disconnect(port_priv->mac);

	if (!dpaa2_switch_port_has_mac(port_priv))
		return;

	dpaa2_mac_close(port_priv->mac);
	kfree(port_priv->mac);
	port_priv->mac = NULL;
}

static irqreturn_t dpaa2_switch_irq0_handler_thread(int irq_num, void *arg)
{
	struct device *dev = (struct device *)arg;
	struct ethsw_core *ethsw = dev_get_drvdata(dev);
	struct ethsw_port_priv *port_priv;
	u32 status = ~0;
	int err, if_id;

	err = dpsw_get_irq_status(ethsw->mc_io, 0, ethsw->dpsw_handle,
				  DPSW_IRQ_INDEX_IF, &status);
	if (err) {
		dev_err(dev, "Can't get irq status (err %d)\n", err);
		goto out;
	}

	if_id = (status & 0xFFFF0000) >> 16;
	port_priv = ethsw->ports[if_id];

	if (status & DPSW_IRQ_EVENT_LINK_CHANGED) {
		dpaa2_switch_port_link_state_update(port_priv->netdev);
		dpaa2_switch_port_set_mac_addr(port_priv);
	}

	if (status & DPSW_IRQ_EVENT_ENDPOINT_CHANGED) {
		rtnl_lock();
		if (dpaa2_switch_port_has_mac(port_priv))
			dpaa2_switch_port_disconnect_mac(port_priv);
		else
			dpaa2_switch_port_connect_mac(port_priv);
		rtnl_unlock();
	}

out:
	err = dpsw_clear_irq_status(ethsw->mc_io, 0, ethsw->dpsw_handle,
				    DPSW_IRQ_INDEX_IF, status);
	if (err)
		dev_err(dev, "Can't clear irq status (err %d)\n", err);

	return IRQ_HANDLED;
}

static int dpaa2_switch_setup_irqs(struct fsl_mc_device *sw_dev)
{
	struct device *dev = &sw_dev->dev;
	struct ethsw_core *ethsw = dev_get_drvdata(dev);
	u32 mask = DPSW_IRQ_EVENT_LINK_CHANGED;
	struct fsl_mc_device_irq *irq;
	int err;

	err = fsl_mc_allocate_irqs(sw_dev);
	if (err) {
		dev_err(dev, "MC irqs allocation failed\n");
		return err;
	}

	if (WARN_ON(sw_dev->obj_desc.irq_count != DPSW_IRQ_NUM)) {
		err = -EINVAL;
		goto free_irq;
	}

	err = dpsw_set_irq_enable(ethsw->mc_io, 0, ethsw->dpsw_handle,
				  DPSW_IRQ_INDEX_IF, 0);
	if (err) {
		dev_err(dev, "dpsw_set_irq_enable err %d\n", err);
		goto free_irq;
	}

	irq = sw_dev->irqs[DPSW_IRQ_INDEX_IF];

	err = devm_request_threaded_irq(dev, irq->msi_desc->irq,
					NULL,
					dpaa2_switch_irq0_handler_thread,
					IRQF_NO_SUSPEND | IRQF_ONESHOT,
					dev_name(dev), dev);
	if (err) {
		dev_err(dev, "devm_request_threaded_irq(): %d\n", err);
		goto free_irq;
	}

	err = dpsw_set_irq_mask(ethsw->mc_io, 0, ethsw->dpsw_handle,
				DPSW_IRQ_INDEX_IF, mask);
	if (err) {
		dev_err(dev, "dpsw_set_irq_mask(): %d\n", err);
		goto free_devm_irq;
	}

	err = dpsw_set_irq_enable(ethsw->mc_io, 0, ethsw->dpsw_handle,
				  DPSW_IRQ_INDEX_IF, 1);
	if (err) {
		dev_err(dev, "dpsw_set_irq_enable(): %d\n", err);
		goto free_devm_irq;
	}

	return 0;

free_devm_irq:
	devm_free_irq(dev, irq->msi_desc->irq, dev);
free_irq:
	fsl_mc_free_irqs(sw_dev);
	return err;
}

static void dpaa2_switch_teardown_irqs(struct fsl_mc_device *sw_dev)
{
	struct device *dev = &sw_dev->dev;
	struct ethsw_core *ethsw = dev_get_drvdata(dev);
	int err;

	err = dpsw_set_irq_enable(ethsw->mc_io, 0, ethsw->dpsw_handle,
				  DPSW_IRQ_INDEX_IF, 0);
	if (err)
		dev_err(dev, "dpsw_set_irq_enable err %d\n", err);

	fsl_mc_free_irqs(sw_dev);
}

static int dpaa2_switch_port_set_learning(struct ethsw_port_priv *port_priv, bool enable)
{
	struct ethsw_core *ethsw = port_priv->ethsw_data;
	enum dpsw_learning_mode learn_mode;
	int err;

	if (enable)
		learn_mode = DPSW_LEARNING_MODE_HW;
	else
		learn_mode = DPSW_LEARNING_MODE_DIS;

	err = dpsw_if_set_learning_mode(ethsw->mc_io, 0, ethsw->dpsw_handle,
					port_priv->idx, learn_mode);
	if (err)
		netdev_err(port_priv->netdev, "dpsw_if_set_learning_mode err %d\n", err);

	if (!enable)
		dpaa2_switch_port_fast_age(port_priv);

	return err;
}

static int dpaa2_switch_port_attr_stp_state_set(struct net_device *netdev,
						u8 state)
{
	struct ethsw_port_priv *port_priv = netdev_priv(netdev);
	int err;

	err = dpaa2_switch_port_set_stp_state(port_priv, state);
	if (err)
		return err;

	switch (state) {
	case BR_STATE_DISABLED:
	case BR_STATE_BLOCKING:
	case BR_STATE_LISTENING:
		err = dpaa2_switch_port_set_learning(port_priv, false);
		break;
	case BR_STATE_LEARNING:
	case BR_STATE_FORWARDING:
		err = dpaa2_switch_port_set_learning(port_priv,
						     port_priv->learn_ena);
		break;
	}

	return err;
}

static int dpaa2_switch_port_flood(struct ethsw_port_priv *port_priv,
				   struct switchdev_brport_flags flags)
{
	struct ethsw_core *ethsw = port_priv->ethsw_data;

	if (flags.mask & BR_BCAST_FLOOD)
		port_priv->bcast_flood = !!(flags.val & BR_BCAST_FLOOD);

	if (flags.mask & BR_FLOOD)
		port_priv->ucast_flood = !!(flags.val & BR_FLOOD);

	return dpaa2_switch_fdb_set_egress_flood(ethsw, port_priv->fdb->fdb_id);
}

static int dpaa2_switch_port_pre_bridge_flags(struct net_device *netdev,
					      struct switchdev_brport_flags flags,
					      struct netlink_ext_ack *extack)
{
	if (flags.mask & ~(BR_LEARNING | BR_BCAST_FLOOD | BR_FLOOD |
			   BR_MCAST_FLOOD))
		return -EINVAL;

	if (flags.mask & (BR_FLOOD | BR_MCAST_FLOOD)) {
		bool multicast = !!(flags.val & BR_MCAST_FLOOD);
		bool unicast = !!(flags.val & BR_FLOOD);

		if (unicast != multicast) {
			NL_SET_ERR_MSG_MOD(extack,
					   "Cannot configure multicast flooding independently of unicast");
			return -EINVAL;
		}
	}

	return 0;
}

static int dpaa2_switch_port_bridge_flags(struct net_device *netdev,
					  struct switchdev_brport_flags flags,
					  struct netlink_ext_ack *extack)
{
	struct ethsw_port_priv *port_priv = netdev_priv(netdev);
	int err;

	if (flags.mask & BR_LEARNING) {
		bool learn_ena = !!(flags.val & BR_LEARNING);

		err = dpaa2_switch_port_set_learning(port_priv, learn_ena);
		if (err)
			return err;
		port_priv->learn_ena = learn_ena;
	}

	if (flags.mask & (BR_BCAST_FLOOD | BR_FLOOD | BR_MCAST_FLOOD)) {
		err = dpaa2_switch_port_flood(port_priv, flags);
		if (err)
			return err;
	}

	return 0;
}

static int dpaa2_switch_port_attr_set(struct net_device *netdev, const void *ctx,
				      const struct switchdev_attr *attr,
				      struct netlink_ext_ack *extack)
{
	int err = 0;

	switch (attr->id) {
	case SWITCHDEV_ATTR_ID_PORT_STP_STATE:
		err = dpaa2_switch_port_attr_stp_state_set(netdev,
							   attr->u.stp_state);
		break;
	case SWITCHDEV_ATTR_ID_BRIDGE_VLAN_FILTERING:
		if (!attr->u.vlan_filtering) {
			NL_SET_ERR_MSG_MOD(extack,
					   "The DPAA2 switch does not support VLAN-unaware operation");
			return -EOPNOTSUPP;
		}
		break;
	case SWITCHDEV_ATTR_ID_PORT_PRE_BRIDGE_FLAGS:
		err = dpaa2_switch_port_pre_bridge_flags(netdev, attr->u.brport_flags, extack);
		break;
	case SWITCHDEV_ATTR_ID_PORT_BRIDGE_FLAGS:
		err = dpaa2_switch_port_bridge_flags(netdev, attr->u.brport_flags, extack);
		break;
	default:
		err = -EOPNOTSUPP;
		break;
	}

	return err;
}

int dpaa2_switch_port_vlans_add(struct net_device *netdev,
				const struct switchdev_obj_port_vlan *vlan)
{
	struct ethsw_port_priv *port_priv = netdev_priv(netdev);
	struct ethsw_core *ethsw = port_priv->ethsw_data;
	struct dpsw_attr *attr = &ethsw->sw_attr;
	int err = 0;

	/* Make sure that the VLAN is not already configured
	 * on the switch port
	 */
	if (port_priv->vlans[vlan->vid] & ETHSW_VLAN_MEMBER)
		return -EEXIST;

	/* Check if there is space for a new VLAN */
	err = dpsw_get_attributes(ethsw->mc_io, 0, ethsw->dpsw_handle,
				  &ethsw->sw_attr);
	if (err) {
		netdev_err(netdev, "dpsw_get_attributes err %d\n", err);
		return err;
	}
	if (attr->max_vlans - attr->num_vlans < 1)
		return -ENOSPC;

	/* Check if there is space for a new VLAN */
	err = dpsw_get_attributes(ethsw->mc_io, 0, ethsw->dpsw_handle,
				  &ethsw->sw_attr);
	if (err) {
		netdev_err(netdev, "dpsw_get_attributes err %d\n", err);
		return err;
	}
	if (attr->max_vlans - attr->num_vlans < 1)
		return -ENOSPC;

	if (!port_priv->ethsw_data->vlans[vlan->vid]) {
		/* this is a new VLAN */
		err = dpaa2_switch_add_vlan(port_priv, vlan->vid);
		if (err)
			return err;

		port_priv->ethsw_data->vlans[vlan->vid] |= ETHSW_VLAN_GLOBAL;
	}

	return dpaa2_switch_port_add_vlan(port_priv, vlan->vid, vlan->flags);
}

static int dpaa2_switch_port_lookup_address(struct net_device *netdev, int is_uc,
					    const unsigned char *addr)
{
	struct netdev_hw_addr_list *list = (is_uc) ? &netdev->uc : &netdev->mc;
	struct netdev_hw_addr *ha;

	netif_addr_lock_bh(netdev);
	list_for_each_entry(ha, &list->list, list) {
		if (ether_addr_equal(ha->addr, addr)) {
			netif_addr_unlock_bh(netdev);
			return 1;
		}
	}
	netif_addr_unlock_bh(netdev);
	return 0;
}

static int dpaa2_switch_port_mdb_add(struct net_device *netdev,
				     const struct switchdev_obj_port_mdb *mdb)
{
	struct ethsw_port_priv *port_priv = netdev_priv(netdev);
	int err;

	/* Check if address is already set on this port */
	if (dpaa2_switch_port_lookup_address(netdev, 0, mdb->addr))
		return -EEXIST;

	err = dpaa2_switch_port_fdb_add_mc(port_priv, mdb->addr);
	if (err)
		return err;

	err = dev_mc_add(netdev, mdb->addr);
	if (err) {
		netdev_err(netdev, "dev_mc_add err %d\n", err);
		dpaa2_switch_port_fdb_del_mc(port_priv, mdb->addr);
	}

	return err;
}

static int dpaa2_switch_port_obj_add(struct net_device *netdev,
				     const struct switchdev_obj *obj)
{
	int err;

	switch (obj->id) {
	case SWITCHDEV_OBJ_ID_PORT_VLAN:
		err = dpaa2_switch_port_vlans_add(netdev,
						  SWITCHDEV_OBJ_PORT_VLAN(obj));
		break;
	case SWITCHDEV_OBJ_ID_PORT_MDB:
		err = dpaa2_switch_port_mdb_add(netdev,
						SWITCHDEV_OBJ_PORT_MDB(obj));
		break;
	default:
		err = -EOPNOTSUPP;
		break;
	}

	return err;
}

static int dpaa2_switch_port_del_vlan(struct ethsw_port_priv *port_priv, u16 vid)
{
	struct ethsw_core *ethsw = port_priv->ethsw_data;
	struct net_device *netdev = port_priv->netdev;
	struct dpsw_vlan_if_cfg vcfg;
	int i, err;

	if (!port_priv->vlans[vid])
		return -ENOENT;

	if (port_priv->vlans[vid] & ETHSW_VLAN_PVID) {
		/* If we are deleting the PVID of a port, use VLAN 4095 instead
		 * as we are sure that neither the bridge nor the 8021q module
		 * will use it
		 */
		err = dpaa2_switch_port_set_pvid(port_priv, 4095);
		if (err)
			return err;
	}

	vcfg.num_ifs = 1;
	vcfg.if_id[0] = port_priv->idx;
	if (port_priv->vlans[vid] & ETHSW_VLAN_UNTAGGED) {
		err = dpsw_vlan_remove_if_untagged(ethsw->mc_io, 0,
						   ethsw->dpsw_handle,
						   vid, &vcfg);
		if (err) {
			netdev_err(netdev,
				   "dpsw_vlan_remove_if_untagged err %d\n",
				   err);
		}
		port_priv->vlans[vid] &= ~ETHSW_VLAN_UNTAGGED;
	}

	if (port_priv->vlans[vid] & ETHSW_VLAN_MEMBER) {
		err = dpsw_vlan_remove_if(ethsw->mc_io, 0, ethsw->dpsw_handle,
					  vid, &vcfg);
		if (err) {
			netdev_err(netdev,
				   "dpsw_vlan_remove_if err %d\n", err);
			return err;
		}
		port_priv->vlans[vid] &= ~ETHSW_VLAN_MEMBER;

		/* Delete VLAN from switch if it is no longer configured on
		 * any port
		 */
		for (i = 0; i < ethsw->sw_attr.num_ifs; i++)
			if (ethsw->ports[i]->vlans[vid] & ETHSW_VLAN_MEMBER)
				return 0; /* Found a port member in VID */

		ethsw->vlans[vid] &= ~ETHSW_VLAN_GLOBAL;

		err = dpaa2_switch_dellink(ethsw, vid);
		if (err)
			return err;
	}

	return 0;
}

int dpaa2_switch_port_vlans_del(struct net_device *netdev,
				const struct switchdev_obj_port_vlan *vlan)
{
	struct ethsw_port_priv *port_priv = netdev_priv(netdev);

	if (netif_is_bridge_master(vlan->obj.orig_dev))
		return -EOPNOTSUPP;

	return dpaa2_switch_port_del_vlan(port_priv, vlan->vid);
}

static int dpaa2_switch_port_mdb_del(struct net_device *netdev,
				     const struct switchdev_obj_port_mdb *mdb)
{
	struct ethsw_port_priv *port_priv = netdev_priv(netdev);
	int err;

	if (!dpaa2_switch_port_lookup_address(netdev, 0, mdb->addr))
		return -ENOENT;

	err = dpaa2_switch_port_fdb_del_mc(port_priv, mdb->addr);
	if (err)
		return err;

	err = dev_mc_del(netdev, mdb->addr);
	if (err) {
		netdev_err(netdev, "dev_mc_del err %d\n", err);
		return err;
	}

	return err;
}

static int dpaa2_switch_port_obj_del(struct net_device *netdev,
				     const struct switchdev_obj *obj)
{
	int err;

	switch (obj->id) {
	case SWITCHDEV_OBJ_ID_PORT_VLAN:
		err = dpaa2_switch_port_vlans_del(netdev, SWITCHDEV_OBJ_PORT_VLAN(obj));
		break;
	case SWITCHDEV_OBJ_ID_PORT_MDB:
		err = dpaa2_switch_port_mdb_del(netdev, SWITCHDEV_OBJ_PORT_MDB(obj));
		break;
	default:
		err = -EOPNOTSUPP;
		break;
	}
	return err;
}

static int dpaa2_switch_port_attr_set_event(struct net_device *netdev,
					    struct switchdev_notifier_port_attr_info *ptr)
{
	int err;

	err = switchdev_handle_port_attr_set(netdev, ptr,
					     dpaa2_switch_port_dev_check,
					     dpaa2_switch_port_attr_set);
	return notifier_from_errno(err);
}

static struct notifier_block dpaa2_switch_port_switchdev_nb;
static struct notifier_block dpaa2_switch_port_switchdev_blocking_nb;

static int dpaa2_switch_port_bridge_join(struct net_device *netdev,
					 struct net_device *upper_dev,
					 struct netlink_ext_ack *extack)
{
	struct ethsw_port_priv *port_priv = netdev_priv(netdev);
	struct ethsw_core *ethsw = port_priv->ethsw_data;
	struct ethsw_port_priv *other_port_priv;
	struct net_device *other_dev;
	struct list_head *iter;
	bool learn_ena;
	int err;

	netdev_for_each_lower_dev(upper_dev, other_dev, iter) {
		if (!dpaa2_switch_port_dev_check(other_dev))
			continue;

		other_port_priv = netdev_priv(other_dev);
		if (other_port_priv->ethsw_data != port_priv->ethsw_data) {
			NL_SET_ERR_MSG_MOD(extack,
					   "Interface from a different DPSW is in the bridge already");
			return -EINVAL;
		}
	}

	/* Delete the previously manually installed VLAN 1 */
	err = dpaa2_switch_port_del_vlan(port_priv, 1);
	if (err)
		return err;

	dpaa2_switch_port_set_fdb(port_priv, upper_dev);

	/* Inherit the initial bridge port learning state */
	learn_ena = br_port_flag_is_set(netdev, BR_LEARNING);
	err = dpaa2_switch_port_set_learning(port_priv, learn_ena);
	port_priv->learn_ena = learn_ena;

	/* Setup the egress flood policy (broadcast, unknown unicast) */
	err = dpaa2_switch_fdb_set_egress_flood(ethsw, port_priv->fdb->fdb_id);
	if (err)
		goto err_egress_flood;

	err = switchdev_bridge_port_offload(netdev, netdev, NULL,
					    &dpaa2_switch_port_switchdev_nb,
					    &dpaa2_switch_port_switchdev_blocking_nb,
					    false, extack);
	if (err)
		goto err_switchdev_offload;

	return 0;

err_switchdev_offload:
err_egress_flood:
	dpaa2_switch_port_set_fdb(port_priv, NULL);
	return err;
}

static int dpaa2_switch_port_clear_rxvlan(struct net_device *vdev, int vid, void *arg)
{
	__be16 vlan_proto = htons(ETH_P_8021Q);

	if (vdev)
		vlan_proto = vlan_dev_vlan_proto(vdev);

	return dpaa2_switch_port_vlan_kill(arg, vlan_proto, vid);
}

static int dpaa2_switch_port_restore_rxvlan(struct net_device *vdev, int vid, void *arg)
{
	__be16 vlan_proto = htons(ETH_P_8021Q);

	if (vdev)
		vlan_proto = vlan_dev_vlan_proto(vdev);

	return dpaa2_switch_port_vlan_add(arg, vlan_proto, vid);
}

static void dpaa2_switch_port_pre_bridge_leave(struct net_device *netdev)
{
	switchdev_bridge_port_unoffload(netdev, NULL,
					&dpaa2_switch_port_switchdev_nb,
					&dpaa2_switch_port_switchdev_blocking_nb);
}

static int dpaa2_switch_port_bridge_leave(struct net_device *netdev)
{
	struct ethsw_port_priv *port_priv = netdev_priv(netdev);
	struct dpaa2_switch_fdb *old_fdb = port_priv->fdb;
	struct ethsw_core *ethsw = port_priv->ethsw_data;
	int err;

	/* First of all, fast age any learn FDB addresses on this switch port */
	dpaa2_switch_port_fast_age(port_priv);

	/* Clear all RX VLANs installed through vlan_vid_add() either as VLAN
	 * upper devices or otherwise from the FDB table that we are about to
	 * leave
	 */
	err = vlan_for_each(netdev, dpaa2_switch_port_clear_rxvlan, netdev);
	if (err)
		netdev_err(netdev, "Unable to clear RX VLANs from old FDB table, err (%d)\n", err);

	dpaa2_switch_port_set_fdb(port_priv, NULL);

	/* Restore all RX VLANs into the new FDB table that we just joined */
	err = vlan_for_each(netdev, dpaa2_switch_port_restore_rxvlan, netdev);
	if (err)
		netdev_err(netdev, "Unable to restore RX VLANs to the new FDB, err (%d)\n", err);

	/* Reset the flooding state to denote that this port can send any
	 * packet in standalone mode. With this, we are also ensuring that any
	 * later bridge join will have the flooding flag on.
	 */
	port_priv->bcast_flood = true;
	port_priv->ucast_flood = true;

	/* Setup the egress flood policy (broadcast, unknown unicast).
	 * When the port is not under a bridge, only the CTRL interface is part
	 * of the flooding domain besides the actual port
	 */
	err = dpaa2_switch_fdb_set_egress_flood(ethsw, port_priv->fdb->fdb_id);
	if (err)
		return err;

	/* Recreate the egress flood domain of the FDB that we just left */
	err = dpaa2_switch_fdb_set_egress_flood(ethsw, old_fdb->fdb_id);
	if (err)
		return err;

	/* No HW learning when not under a bridge */
	err = dpaa2_switch_port_set_learning(port_priv, false);
	if (err)
		return err;
	port_priv->learn_ena = false;

	/* Add the VLAN 1 as PVID when not under a bridge. We need this since
	 * the dpaa2 switch interfaces are not capable to be VLAN unaware
	 */
	return dpaa2_switch_port_add_vlan(port_priv, DEFAULT_VLAN_ID,
					  BRIDGE_VLAN_INFO_UNTAGGED | BRIDGE_VLAN_INFO_PVID);
}

static int dpaa2_switch_prevent_bridging_with_8021q_upper(struct net_device *netdev)
{
	struct net_device *upper_dev;
	struct list_head *iter;

	/* RCU read lock not necessary because we have write-side protection
	 * (rtnl_mutex), however a non-rcu iterator does not exist.
	 */
	netdev_for_each_upper_dev_rcu(netdev, upper_dev, iter)
		if (is_vlan_dev(upper_dev))
			return -EOPNOTSUPP;

	return 0;
}

static int
dpaa2_switch_prechangeupper_sanity_checks(struct net_device *netdev,
					  struct net_device *upper_dev,
					  struct netlink_ext_ack *extack)
{
	int err;

	if (!br_vlan_enabled(upper_dev)) {
		NL_SET_ERR_MSG_MOD(extack, "Cannot join a VLAN-unaware bridge");
		return -EOPNOTSUPP;
	}

	err = dpaa2_switch_prevent_bridging_with_8021q_upper(netdev);
	if (err) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Cannot join a bridge while VLAN uppers are present");
		return 0;
	}

	return 0;
}

static int dpaa2_switch_port_netdevice_event(struct notifier_block *nb,
					     unsigned long event, void *ptr)
{
	struct net_device *netdev = netdev_notifier_info_to_dev(ptr);
	struct netdev_notifier_changeupper_info *info = ptr;
	struct netlink_ext_ack *extack;
	struct net_device *upper_dev;
	int err = 0;

	if (!dpaa2_switch_port_dev_check(netdev))
		return NOTIFY_DONE;

	extack = netdev_notifier_info_to_extack(&info->info);

	switch (event) {
	case NETDEV_PRECHANGEUPPER:
		upper_dev = info->upper_dev;
		if (!netif_is_bridge_master(upper_dev))
			break;

		err = dpaa2_switch_prechangeupper_sanity_checks(netdev,
								upper_dev,
								extack);
		if (err)
			goto out;

		if (!info->linking)
			dpaa2_switch_port_pre_bridge_leave(netdev);

		break;
	case NETDEV_CHANGEUPPER:
		upper_dev = info->upper_dev;
		if (netif_is_bridge_master(upper_dev)) {
			if (info->linking)
				err = dpaa2_switch_port_bridge_join(netdev,
								    upper_dev,
								    extack);
			else
				err = dpaa2_switch_port_bridge_leave(netdev);
		}
		break;
	}

out:
	return notifier_from_errno(err);
}

struct ethsw_switchdev_event_work {
	struct work_struct work;
	struct switchdev_notifier_fdb_info fdb_info;
	struct net_device *dev;
	unsigned long event;
};

static void dpaa2_switch_event_work(struct work_struct *work)
{
	struct ethsw_switchdev_event_work *switchdev_work =
		container_of(work, struct ethsw_switchdev_event_work, work);
	struct net_device *dev = switchdev_work->dev;
	struct switchdev_notifier_fdb_info *fdb_info;
	int err;

	rtnl_lock();
	fdb_info = &switchdev_work->fdb_info;

	switch (switchdev_work->event) {
	case SWITCHDEV_FDB_ADD_TO_DEVICE:
		if (!fdb_info->added_by_user || fdb_info->is_local)
			break;
		if (is_unicast_ether_addr(fdb_info->addr))
			err = dpaa2_switch_port_fdb_add_uc(netdev_priv(dev),
							   fdb_info->addr);
		else
			err = dpaa2_switch_port_fdb_add_mc(netdev_priv(dev),
							   fdb_info->addr);
		if (err)
			break;
		fdb_info->offloaded = true;
		call_switchdev_notifiers(SWITCHDEV_FDB_OFFLOADED, dev,
					 &fdb_info->info, NULL);
		break;
	case SWITCHDEV_FDB_DEL_TO_DEVICE:
		if (!fdb_info->added_by_user || fdb_info->is_local)
			break;
		if (is_unicast_ether_addr(fdb_info->addr))
			dpaa2_switch_port_fdb_del_uc(netdev_priv(dev), fdb_info->addr);
		else
			dpaa2_switch_port_fdb_del_mc(netdev_priv(dev), fdb_info->addr);
		break;
	}

	rtnl_unlock();
	kfree(switchdev_work->fdb_info.addr);
	kfree(switchdev_work);
	dev_put(dev);
}

/* Called under rcu_read_lock() */
static int dpaa2_switch_port_event(struct notifier_block *nb,
				   unsigned long event, void *ptr)
{
	struct net_device *dev = switchdev_notifier_info_to_dev(ptr);
	struct ethsw_port_priv *port_priv = netdev_priv(dev);
	struct ethsw_switchdev_event_work *switchdev_work;
	struct switchdev_notifier_fdb_info *fdb_info = ptr;
	struct ethsw_core *ethsw = port_priv->ethsw_data;

	if (event == SWITCHDEV_PORT_ATTR_SET)
		return dpaa2_switch_port_attr_set_event(dev, ptr);

	if (!dpaa2_switch_port_dev_check(dev))
		return NOTIFY_DONE;

	switchdev_work = kzalloc(sizeof(*switchdev_work), GFP_ATOMIC);
	if (!switchdev_work)
		return NOTIFY_BAD;

	INIT_WORK(&switchdev_work->work, dpaa2_switch_event_work);
	switchdev_work->dev = dev;
	switchdev_work->event = event;

	switch (event) {
	case SWITCHDEV_FDB_ADD_TO_DEVICE:
	case SWITCHDEV_FDB_DEL_TO_DEVICE:
		memcpy(&switchdev_work->fdb_info, ptr,
		       sizeof(switchdev_work->fdb_info));
		switchdev_work->fdb_info.addr = kzalloc(ETH_ALEN, GFP_ATOMIC);
		if (!switchdev_work->fdb_info.addr)
			goto err_addr_alloc;

		ether_addr_copy((u8 *)switchdev_work->fdb_info.addr,
				fdb_info->addr);

		/* Take a reference on the device to avoid being freed. */
		dev_hold(dev);
		break;
	default:
		kfree(switchdev_work);
		return NOTIFY_DONE;
	}

	queue_work(ethsw->workqueue, &switchdev_work->work);

	return NOTIFY_DONE;

err_addr_alloc:
	kfree(switchdev_work);
	return NOTIFY_BAD;
}

static int dpaa2_switch_port_obj_event(unsigned long event,
				       struct net_device *netdev,
				       struct switchdev_notifier_port_obj_info *port_obj_info)
{
	int err = -EOPNOTSUPP;

	if (!dpaa2_switch_port_dev_check(netdev))
		return NOTIFY_DONE;

	switch (event) {
	case SWITCHDEV_PORT_OBJ_ADD:
		err = dpaa2_switch_port_obj_add(netdev, port_obj_info->obj);
		break;
	case SWITCHDEV_PORT_OBJ_DEL:
		err = dpaa2_switch_port_obj_del(netdev, port_obj_info->obj);
		break;
	}

	port_obj_info->handled = true;
	return notifier_from_errno(err);
}

static int dpaa2_switch_port_blocking_event(struct notifier_block *nb,
					    unsigned long event, void *ptr)
{
	struct net_device *dev = switchdev_notifier_info_to_dev(ptr);

	switch (event) {
	case SWITCHDEV_PORT_OBJ_ADD:
	case SWITCHDEV_PORT_OBJ_DEL:
		return dpaa2_switch_port_obj_event(event, dev, ptr);
	case SWITCHDEV_PORT_ATTR_SET:
		return dpaa2_switch_port_attr_set_event(dev, ptr);
	}

	return NOTIFY_DONE;
}

/* Build a linear skb based on a single-buffer frame descriptor */
static struct sk_buff *dpaa2_switch_build_linear_skb(struct ethsw_core *ethsw,
						     const struct dpaa2_fd *fd)
{
	u16 fd_offset = dpaa2_fd_get_offset(fd);
	dma_addr_t addr = dpaa2_fd_get_addr(fd);
	u32 fd_length = dpaa2_fd_get_len(fd);
	struct device *dev = ethsw->dev;
	struct sk_buff *skb = NULL;
	void *fd_vaddr;

	fd_vaddr = dpaa2_iova_to_virt(ethsw->iommu_domain, addr);
	dma_unmap_page(dev, addr, DPAA2_SWITCH_RX_BUF_SIZE,
		       DMA_FROM_DEVICE);

	skb = build_skb(fd_vaddr, DPAA2_SWITCH_RX_BUF_SIZE +
			SKB_DATA_ALIGN(sizeof(struct skb_shared_info)));
	if (unlikely(!skb)) {
		dev_err(dev, "build_skb() failed\n");
		return NULL;
	}

	skb_reserve(skb, fd_offset);
	skb_put(skb, fd_length);

	ethsw->buf_count--;

	return skb;
}

static void dpaa2_switch_tx_conf(struct dpaa2_switch_fq *fq,
				 const struct dpaa2_fd *fd)
{
	dpaa2_switch_free_fd(fq->ethsw, fd);
}

static void dpaa2_switch_rx(struct dpaa2_switch_fq *fq,
			    const struct dpaa2_fd *fd)
{
	struct ethsw_core *ethsw = fq->ethsw;
	struct ethsw_port_priv *port_priv;
	struct net_device *netdev;
	struct vlan_ethhdr *hdr;
	struct sk_buff *skb;
	u16 vlan_tci, vid;
	int if_id, err;

	/* get switch ingress interface ID */
	if_id = upper_32_bits(dpaa2_fd_get_flc(fd)) & 0x0000FFFF;

	if (if_id >= ethsw->sw_attr.num_ifs) {
		dev_err(ethsw->dev, "Frame received from unknown interface!\n");
		goto err_free_fd;
	}
	port_priv = ethsw->ports[if_id];
	netdev = port_priv->netdev;

	/* build the SKB based on the FD received */
	if (dpaa2_fd_get_format(fd) != dpaa2_fd_single) {
		if (net_ratelimit()) {
			netdev_err(netdev, "Received invalid frame format\n");
			goto err_free_fd;
		}
	}

	skb = dpaa2_switch_build_linear_skb(ethsw, fd);
	if (unlikely(!skb))
		goto err_free_fd;

	skb_reset_mac_header(skb);

	/* Remove the VLAN header if the packet that we just received has a vid
	 * equal to the port PVIDs. Since the dpaa2-switch can operate only in
	 * VLAN-aware mode and no alterations are made on the packet when it's
	 * redirected/mirrored to the control interface, we are sure that there
	 * will always be a VLAN header present.
	 */
	hdr = vlan_eth_hdr(skb);
	vid = ntohs(hdr->h_vlan_TCI) & VLAN_VID_MASK;
	if (vid == port_priv->pvid) {
		err = __skb_vlan_pop(skb, &vlan_tci);
		if (err) {
			dev_info(ethsw->dev, "__skb_vlan_pop() returned %d", err);
			goto err_free_fd;
		}
	}

	skb->dev = netdev;
	skb->protocol = eth_type_trans(skb, skb->dev);

	/* Setup the offload_fwd_mark only if the port is under a bridge */
	skb->offload_fwd_mark = !!(port_priv->fdb->bridge_dev);

	netif_receive_skb(skb);

	return;

err_free_fd:
	dpaa2_switch_free_fd(ethsw, fd);
}

static void dpaa2_switch_detect_features(struct ethsw_core *ethsw)
{
	ethsw->features = 0;

	if (ethsw->major > 8 || (ethsw->major == 8 && ethsw->minor >= 6))
		ethsw->features |= ETHSW_FEATURE_MAC_ADDR;
}

static int dpaa2_switch_setup_fqs(struct ethsw_core *ethsw)
{
	struct dpsw_ctrl_if_attr ctrl_if_attr;
	struct device *dev = ethsw->dev;
	int i = 0;
	int err;

	err = dpsw_ctrl_if_get_attributes(ethsw->mc_io, 0, ethsw->dpsw_handle,
					  &ctrl_if_attr);
	if (err) {
		dev_err(dev, "dpsw_ctrl_if_get_attributes() = %d\n", err);
		return err;
	}

	ethsw->fq[i].fqid = ctrl_if_attr.rx_fqid;
	ethsw->fq[i].ethsw = ethsw;
	ethsw->fq[i++].type = DPSW_QUEUE_RX;

	ethsw->fq[i].fqid = ctrl_if_attr.tx_err_conf_fqid;
	ethsw->fq[i].ethsw = ethsw;
	ethsw->fq[i++].type = DPSW_QUEUE_TX_ERR_CONF;

	return 0;
}

/* Free buffers acquired from the buffer pool or which were meant to
 * be released in the pool
 */
static void dpaa2_switch_free_bufs(struct ethsw_core *ethsw, u64 *buf_array, int count)
{
	struct device *dev = ethsw->dev;
	void *vaddr;
	int i;

	for (i = 0; i < count; i++) {
		vaddr = dpaa2_iova_to_virt(ethsw->iommu_domain, buf_array[i]);
		dma_unmap_page(dev, buf_array[i], DPAA2_SWITCH_RX_BUF_SIZE,
			       DMA_FROM_DEVICE);
		free_pages((unsigned long)vaddr, 0);
	}
}

/* Perform a single release command to add buffers
 * to the specified buffer pool
 */
static int dpaa2_switch_add_bufs(struct ethsw_core *ethsw, u16 bpid)
{
	struct device *dev = ethsw->dev;
	u64 buf_array[BUFS_PER_CMD];
	struct page *page;
	int retries = 0;
	dma_addr_t addr;
	int err;
	int i;

	for (i = 0; i < BUFS_PER_CMD; i++) {
		/* Allocate one page for each Rx buffer. WRIOP sees
		 * the entire page except for a tailroom reserved for
		 * skb shared info
		 */
		page = dev_alloc_pages(0);
		if (!page) {
			dev_err(dev, "buffer allocation failed\n");
			goto err_alloc;
		}

		addr = dma_map_page(dev, page, 0, DPAA2_SWITCH_RX_BUF_SIZE,
				    DMA_FROM_DEVICE);
		if (dma_mapping_error(dev, addr)) {
			dev_err(dev, "dma_map_single() failed\n");
			goto err_map;
		}
		buf_array[i] = addr;
	}

release_bufs:
	/* In case the portal is busy, retry until successful or
	 * max retries hit.
	 */
	while ((err = dpaa2_io_service_release(NULL, bpid,
					       buf_array, i)) == -EBUSY) {
		if (retries++ >= DPAA2_SWITCH_SWP_BUSY_RETRIES)
			break;

		cpu_relax();
	}

	/* If release command failed, clean up and bail out. */
	if (err) {
		dpaa2_switch_free_bufs(ethsw, buf_array, i);
		return 0;
	}

	return i;

err_map:
	__free_pages(page, 0);
err_alloc:
	/* If we managed to allocate at least some buffers,
	 * release them to hardware
	 */
	if (i)
		goto release_bufs;

	return 0;
}

static int dpaa2_switch_refill_bp(struct ethsw_core *ethsw)
{
	int *count = &ethsw->buf_count;
	int new_count;
	int err = 0;

	if (unlikely(*count < DPAA2_ETHSW_REFILL_THRESH)) {
		do {
			new_count = dpaa2_switch_add_bufs(ethsw, ethsw->bpid);
			if (unlikely(!new_count)) {
				/* Out of memory; abort for now, we'll
				 * try later on
				 */
				break;
			}
			*count += new_count;
		} while (*count < DPAA2_ETHSW_NUM_BUFS);

		if (unlikely(*count < DPAA2_ETHSW_NUM_BUFS))
			err = -ENOMEM;
	}

	return err;
}

static int dpaa2_switch_seed_bp(struct ethsw_core *ethsw)
{
	int *count, i;

	for (i = 0; i < DPAA2_ETHSW_NUM_BUFS; i += BUFS_PER_CMD) {
		count = &ethsw->buf_count;
		*count += dpaa2_switch_add_bufs(ethsw, ethsw->bpid);

		if (unlikely(*count < BUFS_PER_CMD))
			return -ENOMEM;
	}

	return 0;
}

static void dpaa2_switch_drain_bp(struct ethsw_core *ethsw)
{
	u64 buf_array[BUFS_PER_CMD];
	int ret;

	do {
		ret = dpaa2_io_service_acquire(NULL, ethsw->bpid,
					       buf_array, BUFS_PER_CMD);
		if (ret < 0) {
			dev_err(ethsw->dev,
				"dpaa2_io_service_acquire() = %d\n", ret);
			return;
		}
		dpaa2_switch_free_bufs(ethsw, buf_array, ret);

	} while (ret);
}

static int dpaa2_switch_setup_dpbp(struct ethsw_core *ethsw)
{
	struct dpsw_ctrl_if_pools_cfg dpsw_ctrl_if_pools_cfg = { 0 };
	struct device *dev = ethsw->dev;
	struct fsl_mc_device *dpbp_dev;
	struct dpbp_attr dpbp_attrs;
	int err;

	err = fsl_mc_object_allocate(to_fsl_mc_device(dev), FSL_MC_POOL_DPBP,
				     &dpbp_dev);
	if (err) {
		if (err == -ENXIO)
			err = -EPROBE_DEFER;
		else
			dev_err(dev, "DPBP device allocation failed\n");
		return err;
	}
	ethsw->dpbp_dev = dpbp_dev;

	err = dpbp_open(ethsw->mc_io, 0, dpbp_dev->obj_desc.id,
			&dpbp_dev->mc_handle);
	if (err) {
		dev_err(dev, "dpbp_open() failed\n");
		goto err_open;
	}

	err = dpbp_reset(ethsw->mc_io, 0, dpbp_dev->mc_handle);
	if (err) {
		dev_err(dev, "dpbp_reset() failed\n");
		goto err_reset;
	}

	err = dpbp_enable(ethsw->mc_io, 0, dpbp_dev->mc_handle);
	if (err) {
		dev_err(dev, "dpbp_enable() failed\n");
		goto err_enable;
	}

	err = dpbp_get_attributes(ethsw->mc_io, 0, dpbp_dev->mc_handle,
				  &dpbp_attrs);
	if (err) {
		dev_err(dev, "dpbp_get_attributes() failed\n");
		goto err_get_attr;
	}

	dpsw_ctrl_if_pools_cfg.num_dpbp = 1;
	dpsw_ctrl_if_pools_cfg.pools[0].dpbp_id = dpbp_attrs.id;
	dpsw_ctrl_if_pools_cfg.pools[0].buffer_size = DPAA2_SWITCH_RX_BUF_SIZE;
	dpsw_ctrl_if_pools_cfg.pools[0].backup_pool = 0;

	err = dpsw_ctrl_if_set_pools(ethsw->mc_io, 0, ethsw->dpsw_handle,
				     &dpsw_ctrl_if_pools_cfg);
	if (err) {
		dev_err(dev, "dpsw_ctrl_if_set_pools() failed\n");
		goto err_get_attr;
	}
	ethsw->bpid = dpbp_attrs.id;

	return 0;

err_get_attr:
	dpbp_disable(ethsw->mc_io, 0, dpbp_dev->mc_handle);
err_enable:
err_reset:
	dpbp_close(ethsw->mc_io, 0, dpbp_dev->mc_handle);
err_open:
	fsl_mc_object_free(dpbp_dev);
	return err;
}

static void dpaa2_switch_free_dpbp(struct ethsw_core *ethsw)
{
	dpbp_disable(ethsw->mc_io, 0, ethsw->dpbp_dev->mc_handle);
	dpbp_close(ethsw->mc_io, 0, ethsw->dpbp_dev->mc_handle);
	fsl_mc_object_free(ethsw->dpbp_dev);
}

static int dpaa2_switch_alloc_rings(struct ethsw_core *ethsw)
{
	int i;

	for (i = 0; i < DPAA2_SWITCH_RX_NUM_FQS; i++) {
		ethsw->fq[i].store =
			dpaa2_io_store_create(DPAA2_SWITCH_STORE_SIZE,
					      ethsw->dev);
		if (!ethsw->fq[i].store) {
			dev_err(ethsw->dev, "dpaa2_io_store_create failed\n");
			while (--i >= 0)
				dpaa2_io_store_destroy(ethsw->fq[i].store);
			return -ENOMEM;
		}
	}

	return 0;
}

static void dpaa2_switch_destroy_rings(struct ethsw_core *ethsw)
{
	int i;

	for (i = 0; i < DPAA2_SWITCH_RX_NUM_FQS; i++)
		dpaa2_io_store_destroy(ethsw->fq[i].store);
}

static int dpaa2_switch_pull_fq(struct dpaa2_switch_fq *fq)
{
	int err, retries = 0;

	/* Try to pull from the FQ while the portal is busy and we didn't hit
	 * the maximum number fo retries
	 */
	do {
		err = dpaa2_io_service_pull_fq(NULL, fq->fqid, fq->store);
		cpu_relax();
	} while (err == -EBUSY && retries++ < DPAA2_SWITCH_SWP_BUSY_RETRIES);

	if (unlikely(err))
		dev_err(fq->ethsw->dev, "dpaa2_io_service_pull err %d", err);

	return err;
}

/* Consume all frames pull-dequeued into the store */
static int dpaa2_switch_store_consume(struct dpaa2_switch_fq *fq)
{
	struct ethsw_core *ethsw = fq->ethsw;
	int cleaned = 0, is_last;
	struct dpaa2_dq *dq;
	int retries = 0;

	do {
		/* Get the next available FD from the store */
		dq = dpaa2_io_store_next(fq->store, &is_last);
		if (unlikely(!dq)) {
			if (retries++ >= DPAA2_SWITCH_SWP_BUSY_RETRIES) {
				dev_err_once(ethsw->dev,
					     "No valid dequeue response\n");
				return -ETIMEDOUT;
			}
			continue;
		}

		if (fq->type == DPSW_QUEUE_RX)
			dpaa2_switch_rx(fq, dpaa2_dq_fd(dq));
		else
			dpaa2_switch_tx_conf(fq, dpaa2_dq_fd(dq));
		cleaned++;

	} while (!is_last);

	return cleaned;
}

/* NAPI poll routine */
static int dpaa2_switch_poll(struct napi_struct *napi, int budget)
{
	int err, cleaned = 0, store_cleaned, work_done;
	struct dpaa2_switch_fq *fq;
	int retries = 0;

	fq = container_of(napi, struct dpaa2_switch_fq, napi);

	do {
		err = dpaa2_switch_pull_fq(fq);
		if (unlikely(err))
			break;

		/* Refill pool if appropriate */
		dpaa2_switch_refill_bp(fq->ethsw);

		store_cleaned = dpaa2_switch_store_consume(fq);
		cleaned += store_cleaned;

		if (cleaned >= budget) {
			work_done = budget;
			goto out;
		}

	} while (store_cleaned);

	/* We didn't consume the entire budget, so finish napi and re-enable
	 * data availability notifications
	 */
	napi_complete_done(napi, cleaned);
	do {
		err = dpaa2_io_service_rearm(NULL, &fq->nctx);
		cpu_relax();
	} while (err == -EBUSY && retries++ < DPAA2_SWITCH_SWP_BUSY_RETRIES);

	work_done = max(cleaned, 1);
out:

	return work_done;
}

static void dpaa2_switch_fqdan_cb(struct dpaa2_io_notification_ctx *nctx)
{
	struct dpaa2_switch_fq *fq;

	fq = container_of(nctx, struct dpaa2_switch_fq, nctx);

	napi_schedule(&fq->napi);
}

static int dpaa2_switch_setup_dpio(struct ethsw_core *ethsw)
{
	struct dpsw_ctrl_if_queue_cfg queue_cfg;
	struct dpaa2_io_notification_ctx *nctx;
	int err, i, j;

	for (i = 0; i < DPAA2_SWITCH_RX_NUM_FQS; i++) {
		nctx = &ethsw->fq[i].nctx;

		/* Register a new software context for the FQID.
		 * By using NULL as the first parameter, we specify that we do
		 * not care on which cpu are interrupts received for this queue
		 */
		nctx->is_cdan = 0;
		nctx->id = ethsw->fq[i].fqid;
		nctx->desired_cpu = DPAA2_IO_ANY_CPU;
		nctx->cb = dpaa2_switch_fqdan_cb;
		err = dpaa2_io_service_register(NULL, nctx, ethsw->dev);
		if (err) {
			err = -EPROBE_DEFER;
			goto err_register;
		}

		queue_cfg.options = DPSW_CTRL_IF_QUEUE_OPT_DEST |
				    DPSW_CTRL_IF_QUEUE_OPT_USER_CTX;
		queue_cfg.dest_cfg.dest_type = DPSW_CTRL_IF_DEST_DPIO;
		queue_cfg.dest_cfg.dest_id = nctx->dpio_id;
		queue_cfg.dest_cfg.priority = 0;
		queue_cfg.user_ctx = nctx->qman64;

		err = dpsw_ctrl_if_set_queue(ethsw->mc_io, 0,
					     ethsw->dpsw_handle,
					     ethsw->fq[i].type,
					     &queue_cfg);
		if (err)
			goto err_set_queue;
	}

	return 0;

err_set_queue:
	dpaa2_io_service_deregister(NULL, nctx, ethsw->dev);
err_register:
	for (j = 0; j < i; j++)
		dpaa2_io_service_deregister(NULL, &ethsw->fq[j].nctx,
					    ethsw->dev);

	return err;
}

static void dpaa2_switch_free_dpio(struct ethsw_core *ethsw)
{
	int i;

	for (i = 0; i < DPAA2_SWITCH_RX_NUM_FQS; i++)
		dpaa2_io_service_deregister(NULL, &ethsw->fq[i].nctx,
					    ethsw->dev);
}

static int dpaa2_switch_ctrl_if_setup(struct ethsw_core *ethsw)
{
	int err;

	/* setup FQs for Rx and Tx Conf */
	err = dpaa2_switch_setup_fqs(ethsw);
	if (err)
		return err;

	/* setup the buffer pool needed on the Rx path */
	err = dpaa2_switch_setup_dpbp(ethsw);
	if (err)
		return err;

	err = dpaa2_switch_alloc_rings(ethsw);
	if (err)
		goto err_free_dpbp;

	err = dpaa2_switch_setup_dpio(ethsw);
	if (err)
		goto err_destroy_rings;

	err = dpaa2_switch_seed_bp(ethsw);
	if (err)
		goto err_deregister_dpio;

	err = dpsw_ctrl_if_enable(ethsw->mc_io, 0, ethsw->dpsw_handle);
	if (err) {
		dev_err(ethsw->dev, "dpsw_ctrl_if_enable err %d\n", err);
		goto err_drain_dpbp;
	}

	return 0;

err_drain_dpbp:
	dpaa2_switch_drain_bp(ethsw);
err_deregister_dpio:
	dpaa2_switch_free_dpio(ethsw);
err_destroy_rings:
	dpaa2_switch_destroy_rings(ethsw);
err_free_dpbp:
	dpaa2_switch_free_dpbp(ethsw);

	return err;
}

static void dpaa2_switch_remove_port(struct ethsw_core *ethsw,
				     u16 port_idx)
{
	struct ethsw_port_priv *port_priv = ethsw->ports[port_idx];

	rtnl_lock();
	dpaa2_switch_port_disconnect_mac(port_priv);
	rtnl_unlock();
	free_netdev(port_priv->netdev);
	ethsw->ports[port_idx] = NULL;
}

static int dpaa2_switch_init(struct fsl_mc_device *sw_dev)
{
	struct device *dev = &sw_dev->dev;
	struct ethsw_core *ethsw = dev_get_drvdata(dev);
	struct dpsw_vlan_if_cfg vcfg = {0};
	struct dpsw_tci_cfg tci_cfg = {0};
	struct dpsw_stp_cfg stp_cfg;
	int err;
	u16 i;

	ethsw->dev_id = sw_dev->obj_desc.id;

	err = dpsw_open(ethsw->mc_io, 0, ethsw->dev_id, &ethsw->dpsw_handle);
	if (err) {
		dev_err(dev, "dpsw_open err %d\n", err);
		return err;
	}

	err = dpsw_get_attributes(ethsw->mc_io, 0, ethsw->dpsw_handle,
				  &ethsw->sw_attr);
	if (err) {
		dev_err(dev, "dpsw_get_attributes err %d\n", err);
		goto err_close;
	}

	err = dpsw_get_api_version(ethsw->mc_io, 0,
				   &ethsw->major,
				   &ethsw->minor);
	if (err) {
		dev_err(dev, "dpsw_get_api_version err %d\n", err);
		goto err_close;
	}

	/* Minimum supported DPSW version check */
	if (ethsw->major < DPSW_MIN_VER_MAJOR ||
	    (ethsw->major == DPSW_MIN_VER_MAJOR &&
	     ethsw->minor < DPSW_MIN_VER_MINOR)) {
		dev_err(dev, "DPSW version %d:%d not supported. Use firmware 10.28.0 or greater.\n",
			ethsw->major, ethsw->minor);
		err = -EOPNOTSUPP;
		goto err_close;
	}

	if (!dpaa2_switch_supports_cpu_traffic(ethsw)) {
		err = -EOPNOTSUPP;
		goto err_close;
	}

	dpaa2_switch_detect_features(ethsw);

	err = dpsw_reset(ethsw->mc_io, 0, ethsw->dpsw_handle);
	if (err) {
		dev_err(dev, "dpsw_reset err %d\n", err);
		goto err_close;
	}

	stp_cfg.vlan_id = DEFAULT_VLAN_ID;
	stp_cfg.state = DPSW_STP_STATE_FORWARDING;

	for (i = 0; i < ethsw->sw_attr.num_ifs; i++) {
		err = dpsw_if_disable(ethsw->mc_io, 0, ethsw->dpsw_handle, i);
		if (err) {
			dev_err(dev, "dpsw_if_disable err %d\n", err);
			goto err_close;
		}

		err = dpsw_if_set_stp(ethsw->mc_io, 0, ethsw->dpsw_handle, i,
				      &stp_cfg);
		if (err) {
			dev_err(dev, "dpsw_if_set_stp err %d for port %d\n",
				err, i);
			goto err_close;
		}

		/* Switch starts with all ports configured to VLAN 1. Need to
		 * remove this setting to allow configuration at bridge join
		 */
		vcfg.num_ifs = 1;
		vcfg.if_id[0] = i;
		err = dpsw_vlan_remove_if_untagged(ethsw->mc_io, 0, ethsw->dpsw_handle,
						   DEFAULT_VLAN_ID, &vcfg);
		if (err) {
			dev_err(dev, "dpsw_vlan_remove_if_untagged err %d\n",
				err);
			goto err_close;
		}

		tci_cfg.vlan_id = 4095;
		err = dpsw_if_set_tci(ethsw->mc_io, 0, ethsw->dpsw_handle, i, &tci_cfg);
		if (err) {
			dev_err(dev, "dpsw_if_set_tci err %d\n", err);
			goto err_close;
		}

		err = dpsw_vlan_remove_if(ethsw->mc_io, 0, ethsw->dpsw_handle,
					  DEFAULT_VLAN_ID, &vcfg);
		if (err) {
			dev_err(dev, "dpsw_vlan_remove_if err %d\n", err);
			goto err_close;
		}
	}

	err = dpsw_vlan_remove(ethsw->mc_io, 0, ethsw->dpsw_handle, DEFAULT_VLAN_ID);
	if (err) {
		dev_err(dev, "dpsw_vlan_remove err %d\n", err);
		goto err_close;
	}

	ethsw->workqueue = alloc_ordered_workqueue("%s_%d_ordered",
						   WQ_MEM_RECLAIM, "ethsw",
						   ethsw->sw_attr.id);
	if (!ethsw->workqueue) {
		err = -ENOMEM;
		goto err_close;
	}

	err = dpsw_fdb_remove(ethsw->mc_io, 0, ethsw->dpsw_handle, 0);
	if (err)
		goto err_destroy_ordered_workqueue;

	err = dpaa2_switch_ctrl_if_setup(ethsw);
	if (err)
		goto err_destroy_ordered_workqueue;

	return 0;

err_destroy_ordered_workqueue:
	destroy_workqueue(ethsw->workqueue);

err_close:
	dpsw_close(ethsw->mc_io, 0, ethsw->dpsw_handle);
	return err;
}

/* Add an ACL to redirect frames with specific destination MAC address to
 * control interface
 */
static int dpaa2_switch_port_trap_mac_addr(struct ethsw_port_priv *port_priv,
					   const char *mac)
{
	struct dpaa2_switch_acl_entry acl_entry = {0};

	/* Match on the destination MAC address */
	ether_addr_copy(acl_entry.key.match.l2_dest_mac, mac);
	eth_broadcast_addr(acl_entry.key.mask.l2_dest_mac);

	/* Trap to CPU */
	acl_entry.cfg.precedence = 0;
	acl_entry.cfg.result.action = DPSW_ACL_ACTION_REDIRECT_TO_CTRL_IF;

	return dpaa2_switch_acl_entry_add(port_priv->filter_block, &acl_entry);
}

static int dpaa2_switch_port_init(struct ethsw_port_priv *port_priv, u16 port)
{
	const char stpa[ETH_ALEN] = {0x01, 0x80, 0xc2, 0x00, 0x00, 0x00};
	struct switchdev_obj_port_vlan vlan = {
		.obj.id = SWITCHDEV_OBJ_ID_PORT_VLAN,
		.vid = DEFAULT_VLAN_ID,
		.flags = BRIDGE_VLAN_INFO_UNTAGGED | BRIDGE_VLAN_INFO_PVID,
	};
	struct net_device *netdev = port_priv->netdev;
	struct ethsw_core *ethsw = port_priv->ethsw_data;
	struct dpaa2_switch_filter_block *filter_block;
	struct dpsw_fdb_cfg fdb_cfg = {0};
	struct dpsw_if_attr dpsw_if_attr;
	struct dpaa2_switch_fdb *fdb;
	struct dpsw_acl_cfg acl_cfg;
	u16 fdb_id, acl_tbl_id;
	int err;

	/* Get the Tx queue for this specific port */
	err = dpsw_if_get_attributes(ethsw->mc_io, 0, ethsw->dpsw_handle,
				     port_priv->idx, &dpsw_if_attr);
	if (err) {
		netdev_err(netdev, "dpsw_if_get_attributes err %d\n", err);
		return err;
	}
	port_priv->tx_qdid = dpsw_if_attr.qdid;

	/* Create a FDB table for this particular switch port */
	fdb_cfg.num_fdb_entries = ethsw->sw_attr.max_fdb_entries / ethsw->sw_attr.num_ifs;
	err = dpsw_fdb_add(ethsw->mc_io, 0, ethsw->dpsw_handle,
			   &fdb_id, &fdb_cfg);
	if (err) {
		netdev_err(netdev, "dpsw_fdb_add err %d\n", err);
		return err;
	}

	/* Find an unused dpaa2_switch_fdb structure and use it */
	fdb = dpaa2_switch_fdb_get_unused(ethsw);
	fdb->fdb_id = fdb_id;
	fdb->in_use = true;
	fdb->bridge_dev = NULL;
	port_priv->fdb = fdb;

	/* We need to add VLAN 1 as the PVID on this port until it is under a
	 * bridge since the DPAA2 switch is not able to handle the traffic in a
	 * VLAN unaware fashion
	 */
	err = dpaa2_switch_port_vlans_add(netdev, &vlan);
	if (err)
		return err;

	/* Setup the egress flooding domains (broadcast, unknown unicast */
	err = dpaa2_switch_fdb_set_egress_flood(ethsw, port_priv->fdb->fdb_id);
	if (err)
		return err;

	/* Create an ACL table to be used by this switch port */
	acl_cfg.max_entries = DPAA2_ETHSW_PORT_MAX_ACL_ENTRIES;
	err = dpsw_acl_add(ethsw->mc_io, 0, ethsw->dpsw_handle,
			   &acl_tbl_id, &acl_cfg);
	if (err) {
		netdev_err(netdev, "dpsw_acl_add err %d\n", err);
		return err;
	}

	filter_block = dpaa2_switch_filter_block_get_unused(ethsw);
	filter_block->ethsw = ethsw;
	filter_block->acl_id = acl_tbl_id;
	filter_block->in_use = true;
	filter_block->num_acl_rules = 0;
	INIT_LIST_HEAD(&filter_block->acl_entries);
	INIT_LIST_HEAD(&filter_block->mirror_entries);

	err = dpaa2_switch_port_acl_tbl_bind(port_priv, filter_block);
	if (err)
		return err;

	err = dpaa2_switch_port_trap_mac_addr(port_priv, stpa);
	if (err)
		return err;

	return err;
}

static void dpaa2_switch_ctrl_if_teardown(struct ethsw_core *ethsw)
{
	dpsw_ctrl_if_disable(ethsw->mc_io, 0, ethsw->dpsw_handle);
	dpaa2_switch_free_dpio(ethsw);
	dpaa2_switch_destroy_rings(ethsw);
	dpaa2_switch_drain_bp(ethsw);
	dpaa2_switch_free_dpbp(ethsw);
}

static void dpaa2_switch_teardown(struct fsl_mc_device *sw_dev)
{
	struct device *dev = &sw_dev->dev;
	struct ethsw_core *ethsw = dev_get_drvdata(dev);
	int err;

	dpaa2_switch_ctrl_if_teardown(ethsw);

	destroy_workqueue(ethsw->workqueue);

	err = dpsw_close(ethsw->mc_io, 0, ethsw->dpsw_handle);
	if (err)
		dev_warn(dev, "dpsw_close err %d\n", err);
}

static int dpaa2_switch_remove(struct fsl_mc_device *sw_dev)
{
	struct ethsw_port_priv *port_priv;
	struct ethsw_core *ethsw;
	struct device *dev;
	int i;

	dev = &sw_dev->dev;
	ethsw = dev_get_drvdata(dev);

	dpaa2_switch_teardown_irqs(sw_dev);

	dpsw_disable(ethsw->mc_io, 0, ethsw->dpsw_handle);

	for (i = 0; i < ethsw->sw_attr.num_ifs; i++) {
		port_priv = ethsw->ports[i];
		unregister_netdev(port_priv->netdev);
		dpaa2_switch_remove_port(ethsw, i);
	}

	kfree(ethsw->fdbs);
	kfree(ethsw->filter_blocks);
	kfree(ethsw->ports);

	dpaa2_switch_teardown(sw_dev);

	fsl_mc_portal_free(ethsw->mc_io);

	kfree(ethsw);

	dev_set_drvdata(dev, NULL);

	return 0;
}

static int dpaa2_switch_probe_port(struct ethsw_core *ethsw,
				   u16 port_idx)
{
	struct ethsw_port_priv *port_priv;
	struct device *dev = ethsw->dev;
	struct net_device *port_netdev;
	int err;

	port_netdev = alloc_etherdev(sizeof(struct ethsw_port_priv));
	if (!port_netdev) {
		dev_err(dev, "alloc_etherdev error\n");
		return -ENOMEM;
	}

	port_priv = netdev_priv(port_netdev);
	port_priv->netdev = port_netdev;
	port_priv->ethsw_data = ethsw;

	port_priv->idx = port_idx;
	port_priv->stp_state = BR_STATE_FORWARDING;

	SET_NETDEV_DEV(port_netdev, dev);
	port_netdev->netdev_ops = &dpaa2_switch_port_ops;
	port_netdev->ethtool_ops = &dpaa2_switch_port_ethtool_ops;

	port_netdev->needed_headroom = DPAA2_SWITCH_NEEDED_HEADROOM;

	port_priv->bcast_flood = true;
	port_priv->ucast_flood = true;

	/* Set MTU limits */
	port_netdev->min_mtu = ETH_MIN_MTU;
	port_netdev->max_mtu = ETHSW_MAX_FRAME_LENGTH;

	/* Populate the private port structure so that later calls to
	 * dpaa2_switch_port_init() can use it.
	 */
	ethsw->ports[port_idx] = port_priv;

	/* The DPAA2 switch's ingress path depends on the VLAN table,
	 * thus we are not able to disable VLAN filtering.
	 */
	port_netdev->features = NETIF_F_HW_VLAN_CTAG_FILTER |
				NETIF_F_HW_VLAN_STAG_FILTER |
				NETIF_F_HW_TC;

	err = dpaa2_switch_port_init(port_priv, port_idx);
	if (err)
		goto err_port_probe;

	err = dpaa2_switch_port_set_mac_addr(port_priv);
	if (err)
		goto err_port_probe;

	err = dpaa2_switch_port_set_learning(port_priv, false);
	if (err)
		goto err_port_probe;
	port_priv->learn_ena = false;

	err = dpaa2_switch_port_connect_mac(port_priv);
	if (err)
		goto err_port_probe;

	return 0;

err_port_probe:
	free_netdev(port_netdev);
	ethsw->ports[port_idx] = NULL;

	return err;
}

static int dpaa2_switch_probe(struct fsl_mc_device *sw_dev)
{
	struct device *dev = &sw_dev->dev;
	struct ethsw_core *ethsw;
	int i, err;

	/* Allocate switch core*/
	ethsw = kzalloc(sizeof(*ethsw), GFP_KERNEL);

	if (!ethsw)
		return -ENOMEM;

	ethsw->dev = dev;
	ethsw->iommu_domain = iommu_get_domain_for_dev(dev);
	dev_set_drvdata(dev, ethsw);

	err = fsl_mc_portal_allocate(sw_dev, FSL_MC_IO_ATOMIC_CONTEXT_PORTAL,
				     &ethsw->mc_io);
	if (err) {
		if (err == -ENXIO)
			err = -EPROBE_DEFER;
		else
			dev_err(dev, "fsl_mc_portal_allocate err %d\n", err);
		goto err_free_drvdata;
	}

	err = dpaa2_switch_init(sw_dev);
	if (err)
		goto err_free_cmdport;

	ethsw->ports = kcalloc(ethsw->sw_attr.num_ifs, sizeof(*ethsw->ports),
			       GFP_KERNEL);
	if (!(ethsw->ports)) {
		err = -ENOMEM;
		goto err_teardown;
	}

	ethsw->fdbs = kcalloc(ethsw->sw_attr.num_ifs, sizeof(*ethsw->fdbs),
			      GFP_KERNEL);
	if (!ethsw->fdbs) {
		err = -ENOMEM;
		goto err_free_ports;
	}

	ethsw->filter_blocks = kcalloc(ethsw->sw_attr.num_ifs,
				       sizeof(*ethsw->filter_blocks),
				       GFP_KERNEL);
	if (!ethsw->filter_blocks) {
		err = -ENOMEM;
		goto err_free_fdbs;
	}

	for (i = 0; i < ethsw->sw_attr.num_ifs; i++) {
		err = dpaa2_switch_probe_port(ethsw, i);
		if (err)
			goto err_free_netdev;
	}

	/* Add a NAPI instance for each of the Rx queues. The first port's
	 * net_device will be associated with the instances since we do not have
	 * different queues for each switch ports.
	 */
	for (i = 0; i < DPAA2_SWITCH_RX_NUM_FQS; i++)
		netif_napi_add(ethsw->ports[0]->netdev,
			       &ethsw->fq[i].napi, dpaa2_switch_poll,
			       NAPI_POLL_WEIGHT);

	/* Setup IRQs */
	err = dpaa2_switch_setup_irqs(sw_dev);
	if (err)
		goto err_stop;

	/* By convention, if the mirror port is equal to the number of switch
	 * interfaces, then mirroring of any kind is disabled.
	 */
	ethsw->mirror_port =  ethsw->sw_attr.num_ifs;

	/* Register the netdev only when the entire setup is done and the
	 * switch port interfaces are ready to receive traffic
	 */
	for (i = 0; i < ethsw->sw_attr.num_ifs; i++) {
		err = register_netdev(ethsw->ports[i]->netdev);
		if (err < 0) {
			dev_err(dev, "register_netdev error %d\n", err);
			goto err_unregister_ports;
		}
	}

	return 0;

err_unregister_ports:
	for (i--; i >= 0; i--)
		unregister_netdev(ethsw->ports[i]->netdev);
	dpaa2_switch_teardown_irqs(sw_dev);
err_stop:
	dpsw_disable(ethsw->mc_io, 0, ethsw->dpsw_handle);
err_free_netdev:
	for (i--; i >= 0; i--)
		dpaa2_switch_remove_port(ethsw, i);
	kfree(ethsw->filter_blocks);
err_free_fdbs:
	kfree(ethsw->fdbs);
err_free_ports:
	kfree(ethsw->ports);

err_teardown:
	dpaa2_switch_teardown(sw_dev);

err_free_cmdport:
	fsl_mc_portal_free(ethsw->mc_io);

err_free_drvdata:
	kfree(ethsw);
	dev_set_drvdata(dev, NULL);

	return err;
}

static const struct fsl_mc_device_id dpaa2_switch_match_id_table[] = {
	{
		.vendor = FSL_MC_VENDOR_FREESCALE,
		.obj_type = "dpsw",
	},
	{ .vendor = 0x0 }
};
MODULE_DEVICE_TABLE(fslmc, dpaa2_switch_match_id_table);

static struct fsl_mc_driver dpaa2_switch_drv = {
	.driver = {
		.name = KBUILD_MODNAME,
		.owner = THIS_MODULE,
	},
	.probe = dpaa2_switch_probe,
	.remove = dpaa2_switch_remove,
	.match_id_table = dpaa2_switch_match_id_table
};

static struct notifier_block dpaa2_switch_port_nb __read_mostly = {
	.notifier_call = dpaa2_switch_port_netdevice_event,
};

static struct notifier_block dpaa2_switch_port_switchdev_nb = {
	.notifier_call = dpaa2_switch_port_event,
};

static struct notifier_block dpaa2_switch_port_switchdev_blocking_nb = {
	.notifier_call = dpaa2_switch_port_blocking_event,
};

static int dpaa2_switch_register_notifiers(void)
{
	int err;

	err = register_netdevice_notifier(&dpaa2_switch_port_nb);
	if (err) {
		pr_err("dpaa2-switch: failed to register net_device notifier (%d)\n", err);
		return err;
	}

	err = register_switchdev_notifier(&dpaa2_switch_port_switchdev_nb);
	if (err) {
		pr_err("dpaa2-switch: failed to register switchdev notifier (%d)\n", err);
		goto err_switchdev_nb;
	}

	err = register_switchdev_blocking_notifier(&dpaa2_switch_port_switchdev_blocking_nb);
	if (err) {
		pr_err("dpaa2-switch: failed to register switchdev blocking notifier (%d)\n", err);
		goto err_switchdev_blocking_nb;
	}

	return 0;

err_switchdev_blocking_nb:
	unregister_switchdev_notifier(&dpaa2_switch_port_switchdev_nb);
err_switchdev_nb:
	unregister_netdevice_notifier(&dpaa2_switch_port_nb);

	return err;
}

static void dpaa2_switch_unregister_notifiers(void)
{
	int err;

	err = unregister_switchdev_blocking_notifier(&dpaa2_switch_port_switchdev_blocking_nb);
	if (err)
		pr_err("dpaa2-switch: failed to unregister switchdev blocking notifier (%d)\n",
		       err);

	err = unregister_switchdev_notifier(&dpaa2_switch_port_switchdev_nb);
	if (err)
		pr_err("dpaa2-switch: failed to unregister switchdev notifier (%d)\n", err);

	err = unregister_netdevice_notifier(&dpaa2_switch_port_nb);
	if (err)
		pr_err("dpaa2-switch: failed to unregister net_device notifier (%d)\n", err);
}

static int __init dpaa2_switch_driver_init(void)
{
	int err;

	err = fsl_mc_driver_register(&dpaa2_switch_drv);
	if (err)
		return err;

	err = dpaa2_switch_register_notifiers();
	if (err) {
		fsl_mc_driver_unregister(&dpaa2_switch_drv);
		return err;
	}

	return 0;
}

static void __exit dpaa2_switch_driver_exit(void)
{
	dpaa2_switch_unregister_notifiers();
	fsl_mc_driver_unregister(&dpaa2_switch_drv);
}

module_init(dpaa2_switch_driver_init);
module_exit(dpaa2_switch_driver_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("DPAA2 Ethernet Switch Driver");

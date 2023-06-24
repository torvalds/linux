// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/* Copyright (c) 2019-2020 Marvell International Ltd. All rights reserved */

#include <linux/etherdevice.h>
#include <linux/jiffies.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/netdev_features.h>
#include <linux/of.h>
#include <linux/of_net.h>
#include <linux/if_vlan.h>
#include <linux/phylink.h>

#include "prestera.h"
#include "prestera_hw.h"
#include "prestera_acl.h"
#include "prestera_flow.h"
#include "prestera_span.h"
#include "prestera_rxtx.h"
#include "prestera_devlink.h"
#include "prestera_ethtool.h"
#include "prestera_counter.h"
#include "prestera_switchdev.h"

#define PRESTERA_MTU_DEFAULT	1536

#define PRESTERA_STATS_DELAY_MS	1000

#define PRESTERA_MAC_ADDR_NUM_MAX	255

static struct workqueue_struct *prestera_wq;
static struct workqueue_struct *prestera_owq;

void prestera_queue_work(struct work_struct *work)
{
	queue_work(prestera_owq, work);
}

void prestera_queue_delayed_work(struct delayed_work *work, unsigned long delay)
{
	queue_delayed_work(prestera_wq, work, delay);
}

void prestera_queue_drain(void)
{
	drain_workqueue(prestera_wq);
	drain_workqueue(prestera_owq);
}

int prestera_port_learning_set(struct prestera_port *port, bool learn)
{
	return prestera_hw_port_learning_set(port, learn);
}

int prestera_port_uc_flood_set(struct prestera_port *port, bool flood)
{
	return prestera_hw_port_uc_flood_set(port, flood);
}

int prestera_port_mc_flood_set(struct prestera_port *port, bool flood)
{
	return prestera_hw_port_mc_flood_set(port, flood);
}

int prestera_port_br_locked_set(struct prestera_port *port, bool br_locked)
{
	return prestera_hw_port_br_locked_set(port, br_locked);
}

int prestera_port_pvid_set(struct prestera_port *port, u16 vid)
{
	enum prestera_accept_frm_type frm_type;
	int err;

	frm_type = PRESTERA_ACCEPT_FRAME_TYPE_TAGGED;

	if (vid) {
		err = prestera_hw_vlan_port_vid_set(port, vid);
		if (err)
			return err;

		frm_type = PRESTERA_ACCEPT_FRAME_TYPE_ALL;
	}

	err = prestera_hw_port_accept_frm_type(port, frm_type);
	if (err && frm_type == PRESTERA_ACCEPT_FRAME_TYPE_ALL)
		prestera_hw_vlan_port_vid_set(port, port->pvid);

	port->pvid = vid;
	return 0;
}

struct prestera_port *prestera_port_find_by_hwid(struct prestera_switch *sw,
						 u32 dev_id, u32 hw_id)
{
	struct prestera_port *port = NULL, *tmp;

	read_lock(&sw->port_list_lock);
	list_for_each_entry(tmp, &sw->port_list, list) {
		if (tmp->dev_id == dev_id && tmp->hw_id == hw_id) {
			port = tmp;
			break;
		}
	}
	read_unlock(&sw->port_list_lock);

	return port;
}

struct prestera_port *prestera_find_port(struct prestera_switch *sw, u32 id)
{
	struct prestera_port *port = NULL, *tmp;

	read_lock(&sw->port_list_lock);
	list_for_each_entry(tmp, &sw->port_list, list) {
		if (tmp->id == id) {
			port = tmp;
			break;
		}
	}
	read_unlock(&sw->port_list_lock);

	return port;
}

struct prestera_switch *prestera_switch_get(struct net_device *dev)
{
	struct prestera_port *port;

	port = prestera_port_dev_lower_find(dev);
	return port ? port->sw : NULL;
}

int prestera_port_cfg_mac_read(struct prestera_port *port,
			       struct prestera_port_mac_config *cfg)
{
	*cfg = port->cfg_mac;
	return 0;
}

int prestera_port_cfg_mac_write(struct prestera_port *port,
				struct prestera_port_mac_config *cfg)
{
	int err;

	err = prestera_hw_port_mac_mode_set(port, cfg->admin,
					    cfg->mode, cfg->inband, cfg->speed,
					    cfg->duplex, cfg->fec);
	if (err)
		return err;

	port->cfg_mac = *cfg;
	return 0;
}

static int prestera_port_open(struct net_device *dev)
{
	struct prestera_port *port = netdev_priv(dev);
	struct prestera_port_mac_config cfg_mac;
	int err = 0;

	if (port->phy_link) {
		phylink_start(port->phy_link);
	} else {
		if (port->caps.transceiver == PRESTERA_PORT_TCVR_SFP) {
			err = prestera_port_cfg_mac_read(port, &cfg_mac);
			if (!err) {
				cfg_mac.admin = true;
				err = prestera_port_cfg_mac_write(port,
								  &cfg_mac);
			}
		} else {
			port->cfg_phy.admin = true;
			err = prestera_hw_port_phy_mode_set(port, true,
							    port->autoneg,
							    port->cfg_phy.mode,
							    port->adver_link_modes,
							    port->cfg_phy.mdix);
		}
	}

	netif_start_queue(dev);

	return err;
}

static int prestera_port_close(struct net_device *dev)
{
	struct prestera_port *port = netdev_priv(dev);
	struct prestera_port_mac_config cfg_mac;
	int err = 0;

	netif_stop_queue(dev);

	if (port->phy_link) {
		phylink_stop(port->phy_link);
		phylink_disconnect_phy(port->phy_link);
		err = prestera_port_cfg_mac_read(port, &cfg_mac);
		if (!err) {
			cfg_mac.admin = false;
			prestera_port_cfg_mac_write(port, &cfg_mac);
		}
	} else {
		if (port->caps.transceiver == PRESTERA_PORT_TCVR_SFP) {
			err = prestera_port_cfg_mac_read(port, &cfg_mac);
			if (!err) {
				cfg_mac.admin = false;
				prestera_port_cfg_mac_write(port, &cfg_mac);
			}
		} else {
			port->cfg_phy.admin = false;
			err = prestera_hw_port_phy_mode_set(port, false, port->autoneg,
							    port->cfg_phy.mode,
							    port->adver_link_modes,
							    port->cfg_phy.mdix);
		}
	}

	return err;
}

static void
prestera_port_mac_state_cache_read(struct prestera_port *port,
				   struct prestera_port_mac_state *state)
{
	spin_lock(&port->state_mac_lock);
	*state = port->state_mac;
	spin_unlock(&port->state_mac_lock);
}

static void
prestera_port_mac_state_cache_write(struct prestera_port *port,
				    struct prestera_port_mac_state *state)
{
	spin_lock(&port->state_mac_lock);
	port->state_mac = *state;
	spin_unlock(&port->state_mac_lock);
}

static struct prestera_port *prestera_pcs_to_port(struct phylink_pcs *pcs)
{
	return container_of(pcs, struct prestera_port, phylink_pcs);
}

static void prestera_mac_config(struct phylink_config *config,
				unsigned int an_mode,
				const struct phylink_link_state *state)
{
}

static void prestera_mac_link_down(struct phylink_config *config,
				   unsigned int mode, phy_interface_t interface)
{
	struct net_device *ndev = to_net_dev(config->dev);
	struct prestera_port *port = netdev_priv(ndev);
	struct prestera_port_mac_state state_mac;

	/* Invalidate. Parameters will update on next link event. */
	memset(&state_mac, 0, sizeof(state_mac));
	state_mac.valid = false;
	prestera_port_mac_state_cache_write(port, &state_mac);
}

static void prestera_mac_link_up(struct phylink_config *config,
				 struct phy_device *phy,
				 unsigned int mode, phy_interface_t interface,
				 int speed, int duplex,
				 bool tx_pause, bool rx_pause)
{
}

static struct phylink_pcs *
prestera_mac_select_pcs(struct phylink_config *config,
			phy_interface_t interface)
{
	struct net_device *dev = to_net_dev(config->dev);
	struct prestera_port *port = netdev_priv(dev);

	return &port->phylink_pcs;
}

static void prestera_pcs_get_state(struct phylink_pcs *pcs,
				   struct phylink_link_state *state)
{
	struct prestera_port *port = container_of(pcs, struct prestera_port,
						  phylink_pcs);
	struct prestera_port_mac_state smac;

	prestera_port_mac_state_cache_read(port, &smac);

	if (smac.valid) {
		state->link = smac.oper ? 1 : 0;
		/* AN is completed, when port is up */
		state->an_complete = (smac.oper && port->autoneg) ? 1 : 0;
		state->speed = smac.speed;
		state->duplex = smac.duplex;
	} else {
		state->link = 0;
		state->an_complete = 0;
	}
}

static int prestera_pcs_config(struct phylink_pcs *pcs,
			       unsigned int mode,
			       phy_interface_t interface,
			       const unsigned long *advertising,
			       bool permit_pause_to_mac)
{
	struct prestera_port *port = prestera_pcs_to_port(pcs);
	struct prestera_port_mac_config cfg_mac;
	int err;

	err = prestera_port_cfg_mac_read(port, &cfg_mac);
	if (err)
		return err;

	cfg_mac.admin = true;
	cfg_mac.fec = PRESTERA_PORT_FEC_OFF;

	switch (interface) {
	case PHY_INTERFACE_MODE_10GBASER:
		cfg_mac.speed = SPEED_10000;
		cfg_mac.inband = 0;
		cfg_mac.mode = PRESTERA_MAC_MODE_SR_LR;
		break;
	case PHY_INTERFACE_MODE_2500BASEX:
		cfg_mac.speed = SPEED_2500;
		cfg_mac.duplex = DUPLEX_FULL;
		cfg_mac.inband = test_bit(ETHTOOL_LINK_MODE_Autoneg_BIT,
					  advertising);
		cfg_mac.mode = PRESTERA_MAC_MODE_SGMII;
		break;
	case PHY_INTERFACE_MODE_SGMII:
		cfg_mac.inband = 1;
		cfg_mac.mode = PRESTERA_MAC_MODE_SGMII;
		break;
	case PHY_INTERFACE_MODE_1000BASEX:
	default:
		cfg_mac.speed = SPEED_1000;
		cfg_mac.duplex = DUPLEX_FULL;
		cfg_mac.inband = test_bit(ETHTOOL_LINK_MODE_Autoneg_BIT,
					  advertising);
		cfg_mac.mode = PRESTERA_MAC_MODE_1000BASE_X;
		break;
	}

	err = prestera_port_cfg_mac_write(port, &cfg_mac);
	if (err)
		return err;

	return 0;
}

static void prestera_pcs_an_restart(struct phylink_pcs *pcs)
{
	/* TODO: add 1000basex AN restart support
	 * (Currently FW has no support for 1000baseX AN restart, but it will in the future,
	 * so as for now the function would stay empty.)
	 */
}

static const struct phylink_mac_ops prestera_mac_ops = {
	.validate = phylink_generic_validate,
	.mac_select_pcs = prestera_mac_select_pcs,
	.mac_config = prestera_mac_config,
	.mac_link_down = prestera_mac_link_down,
	.mac_link_up = prestera_mac_link_up,
};

static const struct phylink_pcs_ops prestera_pcs_ops = {
	.pcs_get_state = prestera_pcs_get_state,
	.pcs_config = prestera_pcs_config,
	.pcs_an_restart = prestera_pcs_an_restart,
};

static int prestera_port_sfp_bind(struct prestera_port *port)
{
	struct prestera_switch *sw = port->sw;
	struct device_node *ports, *node;
	struct fwnode_handle *fwnode;
	struct phylink *phy_link;
	int err;

	if (!sw->np)
		return 0;

	of_node_get(sw->np);
	ports = of_find_node_by_name(sw->np, "ports");

	for_each_child_of_node(ports, node) {
		int num;

		err = of_property_read_u32(node, "prestera,port-num", &num);
		if (err) {
			dev_err(sw->dev->dev,
				"device node %pOF has no valid reg property: %d\n",
				node, err);
			goto out;
		}

		if (port->fp_id != num)
			continue;

		port->phylink_pcs.ops = &prestera_pcs_ops;

		port->phy_config.dev = &port->dev->dev;
		port->phy_config.type = PHYLINK_NETDEV;

		fwnode = of_fwnode_handle(node);

		__set_bit(PHY_INTERFACE_MODE_10GBASER,
			  port->phy_config.supported_interfaces);
		__set_bit(PHY_INTERFACE_MODE_2500BASEX,
			  port->phy_config.supported_interfaces);
		__set_bit(PHY_INTERFACE_MODE_SGMII,
			  port->phy_config.supported_interfaces);
		__set_bit(PHY_INTERFACE_MODE_1000BASEX,
			  port->phy_config.supported_interfaces);

		port->phy_config.mac_capabilities =
			MAC_1000 | MAC_2500FD | MAC_10000FD;

		phy_link = phylink_create(&port->phy_config, fwnode,
					  PHY_INTERFACE_MODE_INTERNAL,
					  &prestera_mac_ops);
		if (IS_ERR(phy_link)) {
			netdev_err(port->dev, "failed to create phylink\n");
			err = PTR_ERR(phy_link);
			goto out;
		}

		port->phy_link = phy_link;
		break;
	}

out:
	of_node_put(node);
	of_node_put(ports);
	return err;
}

static int prestera_port_sfp_unbind(struct prestera_port *port)
{
	if (port->phy_link)
		phylink_destroy(port->phy_link);

	return 0;
}

static netdev_tx_t prestera_port_xmit(struct sk_buff *skb,
				      struct net_device *dev)
{
	return prestera_rxtx_xmit(netdev_priv(dev), skb);
}

int prestera_is_valid_mac_addr(struct prestera_port *port, const u8 *addr)
{
	if (!is_valid_ether_addr(addr))
		return -EADDRNOTAVAIL;

	/* firmware requires that port's MAC address contains first 5 bytes
	 * of the base MAC address
	 */
	if (memcmp(port->sw->base_mac, addr, ETH_ALEN - 1))
		return -EINVAL;

	return 0;
}

static int prestera_port_set_mac_address(struct net_device *dev, void *p)
{
	struct prestera_port *port = netdev_priv(dev);
	struct sockaddr *addr = p;
	int err;

	err = prestera_is_valid_mac_addr(port, addr->sa_data);
	if (err)
		return err;

	err = prestera_hw_port_mac_set(port, addr->sa_data);
	if (err)
		return err;

	eth_hw_addr_set(dev, addr->sa_data);

	return 0;
}

static int prestera_port_change_mtu(struct net_device *dev, int mtu)
{
	struct prestera_port *port = netdev_priv(dev);
	int err;

	err = prestera_hw_port_mtu_set(port, mtu);
	if (err)
		return err;

	dev->mtu = mtu;

	return 0;
}

static void prestera_port_get_stats64(struct net_device *dev,
				      struct rtnl_link_stats64 *stats)
{
	struct prestera_port *port = netdev_priv(dev);
	struct prestera_port_stats *port_stats = &port->cached_hw_stats.stats;

	stats->rx_packets = port_stats->broadcast_frames_received +
				port_stats->multicast_frames_received +
				port_stats->unicast_frames_received;

	stats->tx_packets = port_stats->broadcast_frames_sent +
				port_stats->multicast_frames_sent +
				port_stats->unicast_frames_sent;

	stats->rx_bytes = port_stats->good_octets_received;

	stats->tx_bytes = port_stats->good_octets_sent;

	stats->rx_errors = port_stats->rx_error_frame_received;
	stats->tx_errors = port_stats->mac_trans_error;

	stats->rx_dropped = port_stats->buffer_overrun;
	stats->tx_dropped = 0;

	stats->multicast = port_stats->multicast_frames_received;
	stats->collisions = port_stats->excessive_collision;

	stats->rx_crc_errors = port_stats->bad_crc;
}

static void prestera_port_get_hw_stats(struct prestera_port *port)
{
	prestera_hw_port_stats_get(port, &port->cached_hw_stats.stats);
}

static void prestera_port_stats_update(struct work_struct *work)
{
	struct prestera_port *port =
		container_of(work, struct prestera_port,
			     cached_hw_stats.caching_dw.work);

	prestera_port_get_hw_stats(port);

	queue_delayed_work(prestera_wq, &port->cached_hw_stats.caching_dw,
			   msecs_to_jiffies(PRESTERA_STATS_DELAY_MS));
}

static int prestera_port_setup_tc(struct net_device *dev,
				  enum tc_setup_type type,
				  void *type_data)
{
	struct prestera_port *port = netdev_priv(dev);

	switch (type) {
	case TC_SETUP_BLOCK:
		return prestera_flow_block_setup(port, type_data);
	default:
		return -EOPNOTSUPP;
	}
}

static const struct net_device_ops prestera_netdev_ops = {
	.ndo_open = prestera_port_open,
	.ndo_stop = prestera_port_close,
	.ndo_start_xmit = prestera_port_xmit,
	.ndo_setup_tc = prestera_port_setup_tc,
	.ndo_change_mtu = prestera_port_change_mtu,
	.ndo_get_stats64 = prestera_port_get_stats64,
	.ndo_set_mac_address = prestera_port_set_mac_address,
	.ndo_get_devlink_port = prestera_devlink_get_port,
};

int prestera_port_autoneg_set(struct prestera_port *port, u64 link_modes)
{
	int err;

	if (port->autoneg && port->adver_link_modes == link_modes)
		return 0;

	err = prestera_hw_port_phy_mode_set(port, port->cfg_phy.admin,
					    true, 0, link_modes,
					    port->cfg_phy.mdix);
	if (err)
		return err;

	port->adver_fec = BIT(PRESTERA_PORT_FEC_OFF);
	port->adver_link_modes = link_modes;
	port->cfg_phy.mode = 0;
	port->autoneg = true;

	return 0;
}

static void prestera_port_list_add(struct prestera_port *port)
{
	write_lock(&port->sw->port_list_lock);
	list_add(&port->list, &port->sw->port_list);
	write_unlock(&port->sw->port_list_lock);
}

static void prestera_port_list_del(struct prestera_port *port)
{
	write_lock(&port->sw->port_list_lock);
	list_del(&port->list);
	write_unlock(&port->sw->port_list_lock);
}

static int prestera_port_create(struct prestera_switch *sw, u32 id)
{
	struct prestera_port_mac_config cfg_mac;
	struct prestera_port *port;
	struct net_device *dev;
	int err;

	dev = alloc_etherdev(sizeof(*port));
	if (!dev)
		return -ENOMEM;

	port = netdev_priv(dev);

	INIT_LIST_HEAD(&port->vlans_list);
	port->pvid = PRESTERA_DEFAULT_VID;
	port->lag = NULL;
	port->dev = dev;
	port->id = id;
	port->sw = sw;

	spin_lock_init(&port->state_mac_lock);

	err = prestera_hw_port_info_get(port, &port->dev_id, &port->hw_id,
					&port->fp_id);
	if (err) {
		dev_err(prestera_dev(sw), "Failed to get port(%u) info\n", id);
		goto err_port_info_get;
	}

	err = prestera_devlink_port_register(port);
	if (err)
		goto err_dl_port_register;

	dev->features |= NETIF_F_NETNS_LOCAL | NETIF_F_HW_TC;
	dev->netdev_ops = &prestera_netdev_ops;
	dev->ethtool_ops = &prestera_ethtool_ops;
	SET_NETDEV_DEV(dev, sw->dev->dev);

	if (port->caps.transceiver != PRESTERA_PORT_TCVR_SFP)
		netif_carrier_off(dev);

	dev->mtu = min_t(unsigned int, sw->mtu_max, PRESTERA_MTU_DEFAULT);
	dev->min_mtu = sw->mtu_min;
	dev->max_mtu = sw->mtu_max;

	err = prestera_hw_port_mtu_set(port, dev->mtu);
	if (err) {
		dev_err(prestera_dev(sw), "Failed to set port(%u) mtu(%d)\n",
			id, dev->mtu);
		goto err_port_init;
	}

	if (port->fp_id >= PRESTERA_MAC_ADDR_NUM_MAX) {
		err = -EINVAL;
		goto err_port_init;
	}

	eth_hw_addr_gen(dev, sw->base_mac, port->fp_id);
	/* firmware requires that port's MAC address consist of the first
	 * 5 bytes of the base MAC address
	 */
	if (memcmp(dev->dev_addr, sw->base_mac, ETH_ALEN - 1)) {
		dev_warn(prestera_dev(sw), "Port MAC address wraps for port(%u)\n", id);
		dev_addr_mod(dev, 0, sw->base_mac, ETH_ALEN - 1);
	}

	err = prestera_hw_port_mac_set(port, dev->dev_addr);
	if (err) {
		dev_err(prestera_dev(sw), "Failed to set port(%u) mac addr\n", id);
		goto err_port_init;
	}

	err = prestera_hw_port_cap_get(port, &port->caps);
	if (err) {
		dev_err(prestera_dev(sw), "Failed to get port(%u) caps\n", id);
		goto err_port_init;
	}

	port->adver_link_modes = port->caps.supp_link_modes;
	port->adver_fec = 0;
	port->autoneg = true;

	/* initialize config mac */
	if (port->caps.transceiver != PRESTERA_PORT_TCVR_SFP) {
		cfg_mac.admin = true;
		cfg_mac.mode = PRESTERA_MAC_MODE_INTERNAL;
	} else {
		cfg_mac.admin = false;
		cfg_mac.mode = PRESTERA_MAC_MODE_MAX;
	}
	cfg_mac.inband = 0;
	cfg_mac.speed = 0;
	cfg_mac.duplex = DUPLEX_UNKNOWN;
	cfg_mac.fec = PRESTERA_PORT_FEC_OFF;

	err = prestera_port_cfg_mac_write(port, &cfg_mac);
	if (err) {
		dev_err(prestera_dev(sw),
			"Failed to set port(%u) mac mode\n", id);
		goto err_port_init;
	}

	/* initialize config phy (if this is inegral) */
	if (port->caps.transceiver != PRESTERA_PORT_TCVR_SFP) {
		port->cfg_phy.mdix = ETH_TP_MDI_AUTO;
		port->cfg_phy.admin = false;
		err = prestera_hw_port_phy_mode_set(port,
						    port->cfg_phy.admin,
						    false, 0, 0,
						    port->cfg_phy.mdix);
		if (err) {
			dev_err(prestera_dev(sw),
				"Failed to set port(%u) phy mode\n", id);
			goto err_port_init;
		}
	}

	err = prestera_rxtx_port_init(port);
	if (err)
		goto err_port_init;

	INIT_DELAYED_WORK(&port->cached_hw_stats.caching_dw,
			  &prestera_port_stats_update);

	prestera_port_list_add(port);

	err = register_netdev(dev);
	if (err)
		goto err_register_netdev;

	prestera_devlink_port_set(port);

	err = prestera_port_sfp_bind(port);
	if (err)
		goto err_sfp_bind;

	return 0;

err_sfp_bind:
	unregister_netdev(dev);
err_register_netdev:
	prestera_port_list_del(port);
err_port_init:
	prestera_devlink_port_unregister(port);
err_dl_port_register:
err_port_info_get:
	free_netdev(dev);
	return err;
}

static void prestera_port_destroy(struct prestera_port *port)
{
	struct net_device *dev = port->dev;

	cancel_delayed_work_sync(&port->cached_hw_stats.caching_dw);
	prestera_devlink_port_clear(port);
	unregister_netdev(dev);
	prestera_port_list_del(port);
	prestera_devlink_port_unregister(port);
	free_netdev(dev);
}

static void prestera_destroy_ports(struct prestera_switch *sw)
{
	struct prestera_port *port, *tmp;

	list_for_each_entry_safe(port, tmp, &sw->port_list, list)
		prestera_port_destroy(port);
}

static int prestera_create_ports(struct prestera_switch *sw)
{
	struct prestera_port *port, *tmp;
	u32 port_idx;
	int err;

	for (port_idx = 0; port_idx < sw->port_count; port_idx++) {
		err = prestera_port_create(sw, port_idx);
		if (err)
			goto err_port_create;
	}

	return 0;

err_port_create:
	list_for_each_entry_safe(port, tmp, &sw->port_list, list) {
		prestera_port_sfp_unbind(port);
		prestera_port_destroy(port);
	}

	return err;
}

static void prestera_port_handle_event(struct prestera_switch *sw,
				       struct prestera_event *evt, void *arg)
{
	struct prestera_port_mac_state smac;
	struct prestera_port_event *pevt;
	struct delayed_work *caching_dw;
	struct prestera_port *port;

	if (evt->id == PRESTERA_PORT_EVENT_MAC_STATE_CHANGED) {
		pevt = &evt->port_evt;
		port = prestera_find_port(sw, pevt->port_id);
		if (!port || !port->dev)
			return;

		caching_dw = &port->cached_hw_stats.caching_dw;

		memset(&smac, 0, sizeof(smac));
		smac.valid = true;
		smac.oper = pevt->data.mac.oper;
		if (smac.oper) {
			smac.mode = pevt->data.mac.mode;
			smac.speed = pevt->data.mac.speed;
			smac.duplex = pevt->data.mac.duplex;
			smac.fc = pevt->data.mac.fc;
			smac.fec = pevt->data.mac.fec;
		}
		prestera_port_mac_state_cache_write(port, &smac);

		if (port->state_mac.oper) {
			if (port->phy_link)
				phylink_mac_change(port->phy_link, true);
			else
				netif_carrier_on(port->dev);

			if (!delayed_work_pending(caching_dw))
				queue_delayed_work(prestera_wq, caching_dw, 0);
		} else {
			if (port->phy_link)
				phylink_mac_change(port->phy_link, false);
			else if (netif_running(port->dev) && netif_carrier_ok(port->dev))
				netif_carrier_off(port->dev);

			if (delayed_work_pending(caching_dw))
				cancel_delayed_work(caching_dw);
		}
	}
}

static int prestera_event_handlers_register(struct prestera_switch *sw)
{
	return prestera_hw_event_handler_register(sw, PRESTERA_EVENT_TYPE_PORT,
						  prestera_port_handle_event,
						  NULL);
}

static void prestera_event_handlers_unregister(struct prestera_switch *sw)
{
	prestera_hw_event_handler_unregister(sw, PRESTERA_EVENT_TYPE_PORT,
					     prestera_port_handle_event);
}

static int prestera_switch_set_base_mac_addr(struct prestera_switch *sw)
{
	struct device_node *base_mac_np;
	int ret = 0;

	if (sw->np) {
		base_mac_np = of_parse_phandle(sw->np, "base-mac-provider", 0);
		if (base_mac_np) {
			ret = of_get_mac_address(base_mac_np, sw->base_mac);
			of_node_put(base_mac_np);
		}
	}

	if (!is_valid_ether_addr(sw->base_mac) || ret) {
		eth_random_addr(sw->base_mac);
		dev_info(prestera_dev(sw), "using random base mac address\n");
	}

	return prestera_hw_switch_mac_set(sw, sw->base_mac);
}

struct prestera_lag *prestera_lag_by_id(struct prestera_switch *sw, u16 id)
{
	return id < sw->lag_max ? &sw->lags[id] : NULL;
}

static struct prestera_lag *prestera_lag_by_dev(struct prestera_switch *sw,
						struct net_device *dev)
{
	struct prestera_lag *lag;
	u16 id;

	for (id = 0; id < sw->lag_max; id++) {
		lag = &sw->lags[id];
		if (lag->dev == dev)
			return lag;
	}

	return NULL;
}

int prestera_lag_id(struct prestera_switch *sw,
		    struct net_device *lag_dev, u16 *lag_id)
{
	struct prestera_lag *lag;
	int free_id = -1;
	int id;

	for (id = 0; id < sw->lag_max; id++) {
		lag = prestera_lag_by_id(sw, id);
		if (lag->member_count) {
			if (lag->dev == lag_dev) {
				*lag_id = id;
				return 0;
			}
		} else if (free_id < 0) {
			free_id = id;
		}
	}
	if (free_id < 0)
		return -ENOSPC;
	*lag_id = free_id;
	return 0;
}

static struct prestera_lag *prestera_lag_create(struct prestera_switch *sw,
						struct net_device *lag_dev)
{
	struct prestera_lag *lag = NULL;
	u16 id;

	for (id = 0; id < sw->lag_max; id++) {
		lag = &sw->lags[id];
		if (!lag->dev)
			break;
	}
	if (lag) {
		INIT_LIST_HEAD(&lag->members);
		lag->dev = lag_dev;
	}

	return lag;
}

static void prestera_lag_destroy(struct prestera_switch *sw,
				 struct prestera_lag *lag)
{
	WARN_ON(!list_empty(&lag->members));
	lag->member_count = 0;
	lag->dev = NULL;
}

static int prestera_lag_port_add(struct prestera_port *port,
				 struct net_device *lag_dev)
{
	struct prestera_switch *sw = port->sw;
	struct prestera_lag *lag;
	int err;

	lag = prestera_lag_by_dev(sw, lag_dev);
	if (!lag) {
		lag = prestera_lag_create(sw, lag_dev);
		if (!lag)
			return -ENOSPC;
	}

	if (lag->member_count >= sw->lag_member_max)
		return -ENOSPC;

	err = prestera_hw_lag_member_add(port, lag->lag_id);
	if (err) {
		if (!lag->member_count)
			prestera_lag_destroy(sw, lag);
		return err;
	}

	list_add(&port->lag_member, &lag->members);
	lag->member_count++;
	port->lag = lag;

	return 0;
}

static int prestera_lag_port_del(struct prestera_port *port)
{
	struct prestera_switch *sw = port->sw;
	struct prestera_lag *lag = port->lag;
	int err;

	if (!lag || !lag->member_count)
		return -EINVAL;

	err = prestera_hw_lag_member_del(port, lag->lag_id);
	if (err)
		return err;

	list_del(&port->lag_member);
	lag->member_count--;
	port->lag = NULL;

	if (netif_is_bridge_port(lag->dev)) {
		struct net_device *br_dev;

		br_dev = netdev_master_upper_dev_get(lag->dev);

		prestera_bridge_port_leave(br_dev, port);
	}

	if (!lag->member_count)
		prestera_lag_destroy(sw, lag);

	return 0;
}

bool prestera_port_is_lag_member(const struct prestera_port *port)
{
	return !!port->lag;
}

u16 prestera_port_lag_id(const struct prestera_port *port)
{
	return port->lag->lag_id;
}

static int prestera_lag_init(struct prestera_switch *sw)
{
	u16 id;

	sw->lags = kcalloc(sw->lag_max, sizeof(*sw->lags), GFP_KERNEL);
	if (!sw->lags)
		return -ENOMEM;

	for (id = 0; id < sw->lag_max; id++)
		sw->lags[id].lag_id = id;

	return 0;
}

static void prestera_lag_fini(struct prestera_switch *sw)
{
	u8 idx;

	for (idx = 0; idx < sw->lag_max; idx++)
		WARN_ON(sw->lags[idx].member_count);

	kfree(sw->lags);
}

bool prestera_netdev_check(const struct net_device *dev)
{
	return dev->netdev_ops == &prestera_netdev_ops;
}

static int prestera_lower_dev_walk(struct net_device *dev,
				   struct netdev_nested_priv *priv)
{
	struct prestera_port **pport = (struct prestera_port **)priv->data;

	if (prestera_netdev_check(dev)) {
		*pport = netdev_priv(dev);
		return 1;
	}

	return 0;
}

struct prestera_port *prestera_port_dev_lower_find(struct net_device *dev)
{
	struct prestera_port *port = NULL;
	struct netdev_nested_priv priv = {
		.data = (void *)&port,
	};

	if (prestera_netdev_check(dev))
		return netdev_priv(dev);

	netdev_walk_all_lower_dev(dev, prestera_lower_dev_walk, &priv);

	return port;
}

static int prestera_netdev_port_lower_event(struct net_device *dev,
					    unsigned long event, void *ptr)
{
	struct netdev_notifier_changelowerstate_info *info = ptr;
	struct netdev_lag_lower_state_info *lower_state_info;
	struct prestera_port *port = netdev_priv(dev);
	bool enabled;

	if (!netif_is_lag_port(dev))
		return 0;
	if (!prestera_port_is_lag_member(port))
		return 0;

	lower_state_info = info->lower_state_info;
	enabled = lower_state_info->link_up && lower_state_info->tx_enabled;

	return prestera_hw_lag_member_enable(port, port->lag->lag_id, enabled);
}

static bool prestera_lag_master_check(struct net_device *lag_dev,
				      struct netdev_lag_upper_info *info,
				      struct netlink_ext_ack *ext_ack)
{
	if (info->tx_type != NETDEV_LAG_TX_TYPE_HASH) {
		NL_SET_ERR_MSG_MOD(ext_ack, "Unsupported LAG Tx type");
		return false;
	}

	return true;
}

static int prestera_netdev_port_event(struct net_device *lower,
				      struct net_device *dev,
				      unsigned long event, void *ptr)
{
	struct netdev_notifier_info *info = ptr;
	struct netdev_notifier_changeupper_info *cu_info;
	struct prestera_port *port = netdev_priv(dev);
	struct netlink_ext_ack *extack;
	struct net_device *upper;

	extack = netdev_notifier_info_to_extack(info);
	cu_info = container_of(info,
			       struct netdev_notifier_changeupper_info,
			       info);

	switch (event) {
	case NETDEV_PRECHANGEUPPER:
		upper = cu_info->upper_dev;
		if (!netif_is_bridge_master(upper) &&
		    !netif_is_lag_master(upper)) {
			NL_SET_ERR_MSG_MOD(extack, "Unknown upper device type");
			return -EINVAL;
		}

		if (!cu_info->linking)
			break;

		if (netdev_has_any_upper_dev(upper)) {
			NL_SET_ERR_MSG_MOD(extack, "Upper device is already enslaved");
			return -EINVAL;
		}

		if (netif_is_lag_master(upper) &&
		    !prestera_lag_master_check(upper, cu_info->upper_info, extack))
			return -EOPNOTSUPP;
		if (netif_is_lag_master(upper) && vlan_uses_dev(dev)) {
			NL_SET_ERR_MSG_MOD(extack,
					   "Master device is a LAG master and port has a VLAN");
			return -EINVAL;
		}
		if (netif_is_lag_port(dev) && is_vlan_dev(upper) &&
		    !netif_is_lag_master(vlan_dev_real_dev(upper))) {
			NL_SET_ERR_MSG_MOD(extack,
					   "Can not put a VLAN on a LAG port");
			return -EINVAL;
		}
		break;

	case NETDEV_CHANGEUPPER:
		upper = cu_info->upper_dev;
		if (netif_is_bridge_master(upper)) {
			if (cu_info->linking)
				return prestera_bridge_port_join(upper, port,
								 extack);
			else
				prestera_bridge_port_leave(upper, port);
		} else if (netif_is_lag_master(upper)) {
			if (cu_info->linking)
				return prestera_lag_port_add(port, upper);
			else
				prestera_lag_port_del(port);
		}
		break;

	case NETDEV_CHANGELOWERSTATE:
		return prestera_netdev_port_lower_event(dev, event, ptr);
	}

	return 0;
}

static int prestera_netdevice_lag_event(struct net_device *lag_dev,
					unsigned long event, void *ptr)
{
	struct net_device *dev;
	struct list_head *iter;
	int err;

	netdev_for_each_lower_dev(lag_dev, dev, iter) {
		if (prestera_netdev_check(dev)) {
			err = prestera_netdev_port_event(lag_dev, dev, event,
							 ptr);
			if (err)
				return err;
		}
	}

	return 0;
}

static int prestera_netdev_event_handler(struct notifier_block *nb,
					 unsigned long event, void *ptr)
{
	struct net_device *dev = netdev_notifier_info_to_dev(ptr);
	int err = 0;

	if (prestera_netdev_check(dev))
		err = prestera_netdev_port_event(dev, dev, event, ptr);
	else if (netif_is_lag_master(dev))
		err = prestera_netdevice_lag_event(dev, event, ptr);

	return notifier_from_errno(err);
}

struct prestera_mdb_entry *
prestera_mdb_entry_create(struct prestera_switch *sw,
			  const unsigned char *addr, u16 vid)
{
	struct prestera_flood_domain *flood_domain;
	struct prestera_mdb_entry *mdb_entry;

	mdb_entry = kzalloc(sizeof(*mdb_entry), GFP_KERNEL);
	if (!mdb_entry)
		goto err_mdb_alloc;

	flood_domain = prestera_flood_domain_create(sw);
	if (!flood_domain)
		goto err_flood_domain_create;

	mdb_entry->sw = sw;
	mdb_entry->vid = vid;
	mdb_entry->flood_domain = flood_domain;
	ether_addr_copy(mdb_entry->addr, addr);

	if (prestera_hw_mdb_create(mdb_entry))
		goto err_mdb_hw_create;

	return mdb_entry;

err_mdb_hw_create:
	prestera_flood_domain_destroy(flood_domain);
err_flood_domain_create:
	kfree(mdb_entry);
err_mdb_alloc:
	return NULL;
}

void prestera_mdb_entry_destroy(struct prestera_mdb_entry *mdb_entry)
{
	prestera_hw_mdb_destroy(mdb_entry);
	prestera_flood_domain_destroy(mdb_entry->flood_domain);
	kfree(mdb_entry);
}

struct prestera_flood_domain *
prestera_flood_domain_create(struct prestera_switch *sw)
{
	struct prestera_flood_domain *domain;

	domain = kzalloc(sizeof(*domain), GFP_KERNEL);
	if (!domain)
		return NULL;

	domain->sw = sw;

	if (prestera_hw_flood_domain_create(domain)) {
		kfree(domain);
		return NULL;
	}

	INIT_LIST_HEAD(&domain->flood_domain_port_list);

	return domain;
}

void prestera_flood_domain_destroy(struct prestera_flood_domain *flood_domain)
{
	WARN_ON(!list_empty(&flood_domain->flood_domain_port_list));
	WARN_ON_ONCE(prestera_hw_flood_domain_destroy(flood_domain));
	kfree(flood_domain);
}

int
prestera_flood_domain_port_create(struct prestera_flood_domain *flood_domain,
				  struct net_device *dev,
				  u16 vid)
{
	struct prestera_flood_domain_port *flood_domain_port;
	bool is_first_port_in_list = false;
	int err;

	flood_domain_port = kzalloc(sizeof(*flood_domain_port), GFP_KERNEL);
	if (!flood_domain_port) {
		err = -ENOMEM;
		goto err_port_alloc;
	}

	flood_domain_port->vid = vid;

	if (list_empty(&flood_domain->flood_domain_port_list))
		is_first_port_in_list = true;

	list_add(&flood_domain_port->flood_domain_port_node,
		 &flood_domain->flood_domain_port_list);

	flood_domain_port->flood_domain = flood_domain;
	flood_domain_port->dev = dev;

	if (!is_first_port_in_list) {
		err = prestera_hw_flood_domain_ports_reset(flood_domain);
		if (err)
			goto err_prestera_mdb_port_create_hw;
	}

	err = prestera_hw_flood_domain_ports_set(flood_domain);
	if (err)
		goto err_prestera_mdb_port_create_hw;

	return 0;

err_prestera_mdb_port_create_hw:
	list_del(&flood_domain_port->flood_domain_port_node);
	kfree(flood_domain_port);
err_port_alloc:
	return err;
}

void
prestera_flood_domain_port_destroy(struct prestera_flood_domain_port *port)
{
	struct prestera_flood_domain *flood_domain = port->flood_domain;

	list_del(&port->flood_domain_port_node);

	WARN_ON_ONCE(prestera_hw_flood_domain_ports_reset(flood_domain));

	if (!list_empty(&flood_domain->flood_domain_port_list))
		WARN_ON_ONCE(prestera_hw_flood_domain_ports_set(flood_domain));

	kfree(port);
}

struct prestera_flood_domain_port *
prestera_flood_domain_port_find(struct prestera_flood_domain *flood_domain,
				struct net_device *dev, u16 vid)
{
	struct prestera_flood_domain_port *flood_domain_port;

	list_for_each_entry(flood_domain_port,
			    &flood_domain->flood_domain_port_list,
			    flood_domain_port_node)
		if (flood_domain_port->dev == dev &&
		    vid == flood_domain_port->vid)
			return flood_domain_port;

	return NULL;
}

static int prestera_netdev_event_handler_register(struct prestera_switch *sw)
{
	sw->netdev_nb.notifier_call = prestera_netdev_event_handler;

	return register_netdevice_notifier(&sw->netdev_nb);
}

static void prestera_netdev_event_handler_unregister(struct prestera_switch *sw)
{
	unregister_netdevice_notifier(&sw->netdev_nb);
}

static int prestera_switch_init(struct prestera_switch *sw)
{
	int err;

	sw->np = of_find_compatible_node(NULL, NULL, "marvell,prestera");

	err = prestera_hw_switch_init(sw);
	if (err) {
		dev_err(prestera_dev(sw), "Failed to init Switch device\n");
		return err;
	}

	rwlock_init(&sw->port_list_lock);
	INIT_LIST_HEAD(&sw->port_list);

	err = prestera_switch_set_base_mac_addr(sw);
	if (err)
		return err;

	err = prestera_netdev_event_handler_register(sw);
	if (err)
		return err;

	err = prestera_router_init(sw);
	if (err)
		goto err_router_init;

	err = prestera_switchdev_init(sw);
	if (err)
		goto err_swdev_register;

	err = prestera_rxtx_switch_init(sw);
	if (err)
		goto err_rxtx_register;

	err = prestera_event_handlers_register(sw);
	if (err)
		goto err_handlers_register;

	err = prestera_counter_init(sw);
	if (err)
		goto err_counter_init;

	err = prestera_acl_init(sw);
	if (err)
		goto err_acl_init;

	err = prestera_span_init(sw);
	if (err)
		goto err_span_init;

	err = prestera_devlink_traps_register(sw);
	if (err)
		goto err_dl_register;

	err = prestera_lag_init(sw);
	if (err)
		goto err_lag_init;

	err = prestera_create_ports(sw);
	if (err)
		goto err_ports_create;

	prestera_devlink_register(sw);
	return 0;

err_ports_create:
	prestera_lag_fini(sw);
err_lag_init:
	prestera_devlink_traps_unregister(sw);
err_dl_register:
	prestera_span_fini(sw);
err_span_init:
	prestera_acl_fini(sw);
err_acl_init:
	prestera_counter_fini(sw);
err_counter_init:
	prestera_event_handlers_unregister(sw);
err_handlers_register:
	prestera_rxtx_switch_fini(sw);
err_rxtx_register:
	prestera_switchdev_fini(sw);
err_swdev_register:
	prestera_router_fini(sw);
err_router_init:
	prestera_netdev_event_handler_unregister(sw);
	prestera_hw_switch_fini(sw);

	return err;
}

static void prestera_switch_fini(struct prestera_switch *sw)
{
	prestera_devlink_unregister(sw);
	prestera_destroy_ports(sw);
	prestera_lag_fini(sw);
	prestera_devlink_traps_unregister(sw);
	prestera_span_fini(sw);
	prestera_acl_fini(sw);
	prestera_counter_fini(sw);
	prestera_event_handlers_unregister(sw);
	prestera_rxtx_switch_fini(sw);
	prestera_switchdev_fini(sw);
	prestera_router_fini(sw);
	prestera_netdev_event_handler_unregister(sw);
	prestera_hw_switch_fini(sw);
	of_node_put(sw->np);
}

int prestera_device_register(struct prestera_device *dev)
{
	struct prestera_switch *sw;
	int err;

	sw = prestera_devlink_alloc(dev);
	if (!sw)
		return -ENOMEM;

	dev->priv = sw;
	sw->dev = dev;

	err = prestera_switch_init(sw);
	if (err) {
		prestera_devlink_free(sw);
		return err;
	}

	return 0;
}
EXPORT_SYMBOL(prestera_device_register);

void prestera_device_unregister(struct prestera_device *dev)
{
	struct prestera_switch *sw = dev->priv;

	prestera_switch_fini(sw);
	prestera_devlink_free(sw);
}
EXPORT_SYMBOL(prestera_device_unregister);

static int __init prestera_module_init(void)
{
	prestera_wq = alloc_workqueue("prestera", 0, 0);
	if (!prestera_wq)
		return -ENOMEM;

	prestera_owq = alloc_ordered_workqueue("prestera_ordered", 0);
	if (!prestera_owq) {
		destroy_workqueue(prestera_wq);
		return -ENOMEM;
	}

	return 0;
}

static void __exit prestera_module_exit(void)
{
	destroy_workqueue(prestera_wq);
	destroy_workqueue(prestera_owq);
}

module_init(prestera_module_init);
module_exit(prestera_module_exit);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION("Marvell Prestera switch driver");

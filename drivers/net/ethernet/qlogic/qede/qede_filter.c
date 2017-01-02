/* QLogic qede NIC Driver
 * Copyright (c) 2015-2017  QLogic Corporation
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and /or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <net/udp_tunnel.h>
#include <linux/bitops.h>
#include <linux/vmalloc.h>

#include <linux/qed/qed_if.h>
#include "qede.h"

void qede_force_mac(void *dev, u8 *mac, bool forced)
{
	struct qede_dev *edev = dev;

	/* MAC hints take effect only if we haven't set one already */
	if (is_valid_ether_addr(edev->ndev->dev_addr) && !forced)
		return;

	ether_addr_copy(edev->ndev->dev_addr, mac);
	ether_addr_copy(edev->primary_mac, mac);
}

void qede_fill_rss_params(struct qede_dev *edev,
			  struct qed_update_vport_rss_params *rss, u8 *update)
{
	bool need_reset = false;
	int i;

	if (QEDE_RSS_COUNT(edev) <= 1) {
		memset(rss, 0, sizeof(*rss));
		*update = 0;
		return;
	}

	/* Need to validate current RSS config uses valid entries */
	for (i = 0; i < QED_RSS_IND_TABLE_SIZE; i++) {
		if (edev->rss_ind_table[i] >= QEDE_RSS_COUNT(edev)) {
			need_reset = true;
			break;
		}
	}

	if (!(edev->rss_params_inited & QEDE_RSS_INDIR_INITED) || need_reset) {
		for (i = 0; i < QED_RSS_IND_TABLE_SIZE; i++) {
			u16 indir_val, val;

			val = QEDE_RSS_COUNT(edev);
			indir_val = ethtool_rxfh_indir_default(i, val);
			edev->rss_ind_table[i] = indir_val;
		}
		edev->rss_params_inited |= QEDE_RSS_INDIR_INITED;
	}

	/* Now that we have the queue-indirection, prepare the handles */
	for (i = 0; i < QED_RSS_IND_TABLE_SIZE; i++) {
		u16 idx = QEDE_RX_QUEUE_IDX(edev, edev->rss_ind_table[i]);

		rss->rss_ind_table[i] = edev->fp_array[idx].rxq->handle;
	}

	if (!(edev->rss_params_inited & QEDE_RSS_KEY_INITED)) {
		netdev_rss_key_fill(edev->rss_key, sizeof(edev->rss_key));
		edev->rss_params_inited |= QEDE_RSS_KEY_INITED;
	}
	memcpy(rss->rss_key, edev->rss_key, sizeof(rss->rss_key));

	if (!(edev->rss_params_inited & QEDE_RSS_CAPS_INITED)) {
		edev->rss_caps = QED_RSS_IPV4 | QED_RSS_IPV6 |
		    QED_RSS_IPV4_TCP | QED_RSS_IPV6_TCP;
		edev->rss_params_inited |= QEDE_RSS_CAPS_INITED;
	}
	rss->rss_caps = edev->rss_caps;

	*update = 1;
}

static int qede_set_ucast_rx_mac(struct qede_dev *edev,
				 enum qed_filter_xcast_params_type opcode,
				 unsigned char mac[ETH_ALEN])
{
	struct qed_filter_params filter_cmd;

	memset(&filter_cmd, 0, sizeof(filter_cmd));
	filter_cmd.type = QED_FILTER_TYPE_UCAST;
	filter_cmd.filter.ucast.type = opcode;
	filter_cmd.filter.ucast.mac_valid = 1;
	ether_addr_copy(filter_cmd.filter.ucast.mac, mac);

	return edev->ops->filter_config(edev->cdev, &filter_cmd);
}

static int qede_set_ucast_rx_vlan(struct qede_dev *edev,
				  enum qed_filter_xcast_params_type opcode,
				  u16 vid)
{
	struct qed_filter_params filter_cmd;

	memset(&filter_cmd, 0, sizeof(filter_cmd));
	filter_cmd.type = QED_FILTER_TYPE_UCAST;
	filter_cmd.filter.ucast.type = opcode;
	filter_cmd.filter.ucast.vlan_valid = 1;
	filter_cmd.filter.ucast.vlan = vid;

	return edev->ops->filter_config(edev->cdev, &filter_cmd);
}

static int qede_config_accept_any_vlan(struct qede_dev *edev, bool action)
{
	struct qed_update_vport_params *params;
	int rc;

	/* Proceed only if action actually needs to be performed */
	if (edev->accept_any_vlan == action)
		return 0;

	params = vzalloc(sizeof(*params));
	if (!params)
		return -ENOMEM;

	params->vport_id = 0;
	params->accept_any_vlan = action;
	params->update_accept_any_vlan_flg = 1;

	rc = edev->ops->vport_update(edev->cdev, params);
	if (rc) {
		DP_ERR(edev, "Failed to %s accept-any-vlan\n",
		       action ? "enable" : "disable");
	} else {
		DP_INFO(edev, "%s accept-any-vlan\n",
			action ? "enabled" : "disabled");
		edev->accept_any_vlan = action;
	}

	vfree(params);
	return 0;
}

int qede_vlan_rx_add_vid(struct net_device *dev, __be16 proto, u16 vid)
{
	struct qede_dev *edev = netdev_priv(dev);
	struct qede_vlan *vlan, *tmp;
	int rc = 0;

	DP_VERBOSE(edev, NETIF_MSG_IFUP, "Adding vlan 0x%04x\n", vid);

	vlan = kzalloc(sizeof(*vlan), GFP_KERNEL);
	if (!vlan) {
		DP_INFO(edev, "Failed to allocate struct for vlan\n");
		return -ENOMEM;
	}
	INIT_LIST_HEAD(&vlan->list);
	vlan->vid = vid;
	vlan->configured = false;

	/* Verify vlan isn't already configured */
	list_for_each_entry(tmp, &edev->vlan_list, list) {
		if (tmp->vid == vlan->vid) {
			DP_VERBOSE(edev, (NETIF_MSG_IFUP | NETIF_MSG_IFDOWN),
				   "vlan already configured\n");
			kfree(vlan);
			return -EEXIST;
		}
	}

	/* If interface is down, cache this VLAN ID and return */
	__qede_lock(edev);
	if (edev->state != QEDE_STATE_OPEN) {
		DP_VERBOSE(edev, NETIF_MSG_IFDOWN,
			   "Interface is down, VLAN %d will be configured when interface is up\n",
			   vid);
		if (vid != 0)
			edev->non_configured_vlans++;
		list_add(&vlan->list, &edev->vlan_list);
		goto out;
	}

	/* Check for the filter limit.
	 * Note - vlan0 has a reserved filter and can be added without
	 * worrying about quota
	 */
	if ((edev->configured_vlans < edev->dev_info.num_vlan_filters) ||
	    (vlan->vid == 0)) {
		rc = qede_set_ucast_rx_vlan(edev,
					    QED_FILTER_XCAST_TYPE_ADD,
					    vlan->vid);
		if (rc) {
			DP_ERR(edev, "Failed to configure VLAN %d\n",
			       vlan->vid);
			kfree(vlan);
			goto out;
		}
		vlan->configured = true;

		/* vlan0 filter isn't consuming out of our quota */
		if (vlan->vid != 0)
			edev->configured_vlans++;
	} else {
		/* Out of quota; Activate accept-any-VLAN mode */
		if (!edev->non_configured_vlans) {
			rc = qede_config_accept_any_vlan(edev, true);
			if (rc) {
				kfree(vlan);
				goto out;
			}
		}

		edev->non_configured_vlans++;
	}

	list_add(&vlan->list, &edev->vlan_list);

out:
	__qede_unlock(edev);
	return rc;
}

static void qede_del_vlan_from_list(struct qede_dev *edev,
				    struct qede_vlan *vlan)
{
	/* vlan0 filter isn't consuming out of our quota */
	if (vlan->vid != 0) {
		if (vlan->configured)
			edev->configured_vlans--;
		else
			edev->non_configured_vlans--;
	}

	list_del(&vlan->list);
	kfree(vlan);
}

int qede_configure_vlan_filters(struct qede_dev *edev)
{
	int rc = 0, real_rc = 0, accept_any_vlan = 0;
	struct qed_dev_eth_info *dev_info;
	struct qede_vlan *vlan = NULL;

	if (list_empty(&edev->vlan_list))
		return 0;

	dev_info = &edev->dev_info;

	/* Configure non-configured vlans */
	list_for_each_entry(vlan, &edev->vlan_list, list) {
		if (vlan->configured)
			continue;

		/* We have used all our credits, now enable accept_any_vlan */
		if ((vlan->vid != 0) &&
		    (edev->configured_vlans == dev_info->num_vlan_filters)) {
			accept_any_vlan = 1;
			continue;
		}

		DP_VERBOSE(edev, NETIF_MSG_IFUP, "Adding vlan %d\n", vlan->vid);

		rc = qede_set_ucast_rx_vlan(edev, QED_FILTER_XCAST_TYPE_ADD,
					    vlan->vid);
		if (rc) {
			DP_ERR(edev, "Failed to configure VLAN %u\n",
			       vlan->vid);
			real_rc = rc;
			continue;
		}

		vlan->configured = true;
		/* vlan0 filter doesn't consume our VLAN filter's quota */
		if (vlan->vid != 0) {
			edev->non_configured_vlans--;
			edev->configured_vlans++;
		}
	}

	/* enable accept_any_vlan mode if we have more VLANs than credits,
	 * or remove accept_any_vlan mode if we've actually removed
	 * a non-configured vlan, and all remaining vlans are truly configured.
	 */

	if (accept_any_vlan)
		rc = qede_config_accept_any_vlan(edev, true);
	else if (!edev->non_configured_vlans)
		rc = qede_config_accept_any_vlan(edev, false);

	if (rc && !real_rc)
		real_rc = rc;

	return real_rc;
}

int qede_vlan_rx_kill_vid(struct net_device *dev, __be16 proto, u16 vid)
{
	struct qede_dev *edev = netdev_priv(dev);
	struct qede_vlan *vlan = NULL;
	int rc = 0;

	DP_VERBOSE(edev, NETIF_MSG_IFDOWN, "Removing vlan 0x%04x\n", vid);

	/* Find whether entry exists */
	__qede_lock(edev);
	list_for_each_entry(vlan, &edev->vlan_list, list)
		if (vlan->vid == vid)
			break;

	if (!vlan || (vlan->vid != vid)) {
		DP_VERBOSE(edev, (NETIF_MSG_IFUP | NETIF_MSG_IFDOWN),
			   "Vlan isn't configured\n");
		goto out;
	}

	if (edev->state != QEDE_STATE_OPEN) {
		/* As interface is already down, we don't have a VPORT
		 * instance to remove vlan filter. So just update vlan list
		 */
		DP_VERBOSE(edev, NETIF_MSG_IFDOWN,
			   "Interface is down, removing VLAN from list only\n");
		qede_del_vlan_from_list(edev, vlan);
		goto out;
	}

	/* Remove vlan */
	if (vlan->configured) {
		rc = qede_set_ucast_rx_vlan(edev, QED_FILTER_XCAST_TYPE_DEL,
					    vid);
		if (rc) {
			DP_ERR(edev, "Failed to remove VLAN %d\n", vid);
			goto out;
		}
	}

	qede_del_vlan_from_list(edev, vlan);

	/* We have removed a VLAN - try to see if we can
	 * configure non-configured VLAN from the list.
	 */
	rc = qede_configure_vlan_filters(edev);

out:
	__qede_unlock(edev);
	return rc;
}

void qede_vlan_mark_nonconfigured(struct qede_dev *edev)
{
	struct qede_vlan *vlan = NULL;

	if (list_empty(&edev->vlan_list))
		return;

	list_for_each_entry(vlan, &edev->vlan_list, list) {
		if (!vlan->configured)
			continue;

		vlan->configured = false;

		/* vlan0 filter isn't consuming out of our quota */
		if (vlan->vid != 0) {
			edev->non_configured_vlans++;
			edev->configured_vlans--;
		}

		DP_VERBOSE(edev, NETIF_MSG_IFDOWN,
			   "marked vlan %d as non-configured\n", vlan->vid);
	}

	edev->accept_any_vlan = false;
}

static void qede_set_features_reload(struct qede_dev *edev,
				     struct qede_reload_args *args)
{
	edev->ndev->features = args->u.features;
}

int qede_set_features(struct net_device *dev, netdev_features_t features)
{
	struct qede_dev *edev = netdev_priv(dev);
	netdev_features_t changes = features ^ dev->features;
	bool need_reload = false;

	/* No action needed if hardware GRO is disabled during driver load */
	if (changes & NETIF_F_GRO) {
		if (dev->features & NETIF_F_GRO)
			need_reload = !edev->gro_disable;
		else
			need_reload = edev->gro_disable;
	}

	if (need_reload) {
		struct qede_reload_args args;

		args.u.features = features;
		args.func = &qede_set_features_reload;

		/* Make sure that we definitely need to reload.
		 * In case of an eBPF attached program, there will be no FW
		 * aggregations, so no need to actually reload.
		 */
		__qede_lock(edev);
		if (edev->xdp_prog)
			args.func(edev, &args);
		else
			qede_reload(edev, &args, true);
		__qede_unlock(edev);

		return 1;
	}

	return 0;
}

void qede_udp_tunnel_add(struct net_device *dev, struct udp_tunnel_info *ti)
{
	struct qede_dev *edev = netdev_priv(dev);
	u16 t_port = ntohs(ti->port);

	switch (ti->type) {
	case UDP_TUNNEL_TYPE_VXLAN:
		if (edev->vxlan_dst_port)
			return;

		edev->vxlan_dst_port = t_port;

		DP_VERBOSE(edev, QED_MSG_DEBUG, "Added vxlan port=%d\n",
			   t_port);

		set_bit(QEDE_SP_VXLAN_PORT_CONFIG, &edev->sp_flags);
		break;
	case UDP_TUNNEL_TYPE_GENEVE:
		if (edev->geneve_dst_port)
			return;

		edev->geneve_dst_port = t_port;

		DP_VERBOSE(edev, QED_MSG_DEBUG, "Added geneve port=%d\n",
			   t_port);
		set_bit(QEDE_SP_GENEVE_PORT_CONFIG, &edev->sp_flags);
		break;
	default:
		return;
	}

	schedule_delayed_work(&edev->sp_task, 0);
}

void qede_udp_tunnel_del(struct net_device *dev, struct udp_tunnel_info *ti)
{
	struct qede_dev *edev = netdev_priv(dev);
	u16 t_port = ntohs(ti->port);

	switch (ti->type) {
	case UDP_TUNNEL_TYPE_VXLAN:
		if (t_port != edev->vxlan_dst_port)
			return;

		edev->vxlan_dst_port = 0;

		DP_VERBOSE(edev, QED_MSG_DEBUG, "Deleted vxlan port=%d\n",
			   t_port);

		set_bit(QEDE_SP_VXLAN_PORT_CONFIG, &edev->sp_flags);
		break;
	case UDP_TUNNEL_TYPE_GENEVE:
		if (t_port != edev->geneve_dst_port)
			return;

		edev->geneve_dst_port = 0;

		DP_VERBOSE(edev, QED_MSG_DEBUG, "Deleted geneve port=%d\n",
			   t_port);
		set_bit(QEDE_SP_GENEVE_PORT_CONFIG, &edev->sp_flags);
		break;
	default:
		return;
	}

	schedule_delayed_work(&edev->sp_task, 0);
}

static void qede_xdp_reload_func(struct qede_dev *edev,
				 struct qede_reload_args *args)
{
	struct bpf_prog *old;

	old = xchg(&edev->xdp_prog, args->u.new_prog);
	if (old)
		bpf_prog_put(old);
}

static int qede_xdp_set(struct qede_dev *edev, struct bpf_prog *prog)
{
	struct qede_reload_args args;

	if (prog && prog->xdp_adjust_head) {
		DP_ERR(edev, "Does not support bpf_xdp_adjust_head()\n");
		return -EOPNOTSUPP;
	}

	/* If we're called, there was already a bpf reference increment */
	args.func = &qede_xdp_reload_func;
	args.u.new_prog = prog;
	qede_reload(edev, &args, false);

	return 0;
}

int qede_xdp(struct net_device *dev, struct netdev_xdp *xdp)
{
	struct qede_dev *edev = netdev_priv(dev);

	switch (xdp->command) {
	case XDP_SETUP_PROG:
		return qede_xdp_set(edev, xdp->prog);
	case XDP_QUERY_PROG:
		xdp->prog_attached = !!edev->xdp_prog;
		return 0;
	default:
		return -EINVAL;
	}
}

static int qede_set_mcast_rx_mac(struct qede_dev *edev,
				 enum qed_filter_xcast_params_type opcode,
				 unsigned char *mac, int num_macs)
{
	struct qed_filter_params filter_cmd;
	int i;

	memset(&filter_cmd, 0, sizeof(filter_cmd));
	filter_cmd.type = QED_FILTER_TYPE_MCAST;
	filter_cmd.filter.mcast.type = opcode;
	filter_cmd.filter.mcast.num = num_macs;

	for (i = 0; i < num_macs; i++, mac += ETH_ALEN)
		ether_addr_copy(filter_cmd.filter.mcast.mac[i], mac);

	return edev->ops->filter_config(edev->cdev, &filter_cmd);
}

int qede_set_mac_addr(struct net_device *ndev, void *p)
{
	struct qede_dev *edev = netdev_priv(ndev);
	struct sockaddr *addr = p;
	int rc;

	ASSERT_RTNL(); /* @@@TBD To be removed */

	DP_INFO(edev, "Set_mac_addr called\n");

	if (!is_valid_ether_addr(addr->sa_data)) {
		DP_NOTICE(edev, "The MAC address is not valid\n");
		return -EFAULT;
	}

	if (!edev->ops->check_mac(edev->cdev, addr->sa_data)) {
		DP_NOTICE(edev, "qed prevents setting MAC\n");
		return -EINVAL;
	}

	ether_addr_copy(ndev->dev_addr, addr->sa_data);

	if (!netif_running(ndev))  {
		DP_NOTICE(edev, "The device is currently down\n");
		return 0;
	}

	/* Remove the previous primary mac */
	rc = qede_set_ucast_rx_mac(edev, QED_FILTER_XCAST_TYPE_DEL,
				   edev->primary_mac);
	if (rc)
		return rc;

	edev->ops->common->update_mac(edev->cdev, addr->sa_data);

	/* Add MAC filter according to the new unicast HW MAC address */
	ether_addr_copy(edev->primary_mac, ndev->dev_addr);
	return qede_set_ucast_rx_mac(edev, QED_FILTER_XCAST_TYPE_ADD,
				      edev->primary_mac);
}

static int
qede_configure_mcast_filtering(struct net_device *ndev,
			       enum qed_filter_rx_mode_type *accept_flags)
{
	struct qede_dev *edev = netdev_priv(ndev);
	unsigned char *mc_macs, *temp;
	struct netdev_hw_addr *ha;
	int rc = 0, mc_count;
	size_t size;

	size = 64 * ETH_ALEN;

	mc_macs = kzalloc(size, GFP_KERNEL);
	if (!mc_macs) {
		DP_NOTICE(edev,
			  "Failed to allocate memory for multicast MACs\n");
		rc = -ENOMEM;
		goto exit;
	}

	temp = mc_macs;

	/* Remove all previously configured MAC filters */
	rc = qede_set_mcast_rx_mac(edev, QED_FILTER_XCAST_TYPE_DEL,
				   mc_macs, 1);
	if (rc)
		goto exit;

	netif_addr_lock_bh(ndev);

	mc_count = netdev_mc_count(ndev);
	if (mc_count < 64) {
		netdev_for_each_mc_addr(ha, ndev) {
			ether_addr_copy(temp, ha->addr);
			temp += ETH_ALEN;
		}
	}

	netif_addr_unlock_bh(ndev);

	/* Check for all multicast @@@TBD resource allocation */
	if ((ndev->flags & IFF_ALLMULTI) || (mc_count > 64)) {
		if (*accept_flags == QED_FILTER_RX_MODE_TYPE_REGULAR)
			*accept_flags = QED_FILTER_RX_MODE_TYPE_MULTI_PROMISC;
	} else {
		/* Add all multicast MAC filters */
		rc = qede_set_mcast_rx_mac(edev, QED_FILTER_XCAST_TYPE_ADD,
					   mc_macs, mc_count);
	}

exit:
	kfree(mc_macs);
	return rc;
}

void qede_set_rx_mode(struct net_device *ndev)
{
	struct qede_dev *edev = netdev_priv(ndev);

	set_bit(QEDE_SP_RX_MODE, &edev->sp_flags);
	schedule_delayed_work(&edev->sp_task, 0);
}

/* Must be called with qede_lock held */
void qede_config_rx_mode(struct net_device *ndev)
{
	enum qed_filter_rx_mode_type accept_flags;
	struct qede_dev *edev = netdev_priv(ndev);
	struct qed_filter_params rx_mode;
	unsigned char *uc_macs, *temp;
	struct netdev_hw_addr *ha;
	int rc, uc_count;
	size_t size;

	netif_addr_lock_bh(ndev);

	uc_count = netdev_uc_count(ndev);
	size = uc_count * ETH_ALEN;

	uc_macs = kzalloc(size, GFP_ATOMIC);
	if (!uc_macs) {
		DP_NOTICE(edev, "Failed to allocate memory for unicast MACs\n");
		netif_addr_unlock_bh(ndev);
		return;
	}

	temp = uc_macs;
	netdev_for_each_uc_addr(ha, ndev) {
		ether_addr_copy(temp, ha->addr);
		temp += ETH_ALEN;
	}

	netif_addr_unlock_bh(ndev);

	/* Configure the struct for the Rx mode */
	memset(&rx_mode, 0, sizeof(struct qed_filter_params));
	rx_mode.type = QED_FILTER_TYPE_RX_MODE;

	/* Remove all previous unicast secondary macs and multicast macs
	 * (configrue / leave the primary mac)
	 */
	rc = qede_set_ucast_rx_mac(edev, QED_FILTER_XCAST_TYPE_REPLACE,
				   edev->primary_mac);
	if (rc)
		goto out;

	/* Check for promiscuous */
	if (ndev->flags & IFF_PROMISC)
		accept_flags = QED_FILTER_RX_MODE_TYPE_PROMISC;
	else
		accept_flags = QED_FILTER_RX_MODE_TYPE_REGULAR;

	/* Configure all filters regardless, in case promisc is rejected */
	if (uc_count < edev->dev_info.num_mac_filters) {
		int i;

		temp = uc_macs;
		for (i = 0; i < uc_count; i++) {
			rc = qede_set_ucast_rx_mac(edev,
						   QED_FILTER_XCAST_TYPE_ADD,
						   temp);
			if (rc)
				goto out;

			temp += ETH_ALEN;
		}
	} else {
		accept_flags = QED_FILTER_RX_MODE_TYPE_PROMISC;
	}

	rc = qede_configure_mcast_filtering(ndev, &accept_flags);
	if (rc)
		goto out;

	/* take care of VLAN mode */
	if (ndev->flags & IFF_PROMISC) {
		qede_config_accept_any_vlan(edev, true);
	} else if (!edev->non_configured_vlans) {
		/* It's possible that accept_any_vlan mode is set due to a
		 * previous setting of IFF_PROMISC. If vlan credits are
		 * sufficient, disable accept_any_vlan.
		 */
		qede_config_accept_any_vlan(edev, false);
	}

	rx_mode.filter.accept_flags = accept_flags;
	edev->ops->filter_config(edev->cdev, &rx_mode);
out:
	kfree(uc_macs);
}

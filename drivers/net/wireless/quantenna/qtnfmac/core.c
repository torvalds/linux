// SPDX-License-Identifier: GPL-2.0+
/* Copyright (c) 2015-2016 Quantenna Communications. All rights reserved. */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/if_ether.h>
#include <linux/nospec.h>

#include "core.h"
#include "bus.h"
#include "trans.h"
#include "commands.h"
#include "cfg80211.h"
#include "event.h"
#include "util.h"
#include "switchdev.h"

#define QTNF_DMP_MAX_LEN 48
#define QTNF_PRIMARY_VIF_IDX	0

static bool slave_radar = true;
module_param(slave_radar, bool, 0644);
MODULE_PARM_DESC(slave_radar, "set 0 to disable radar detection in slave mode");

static bool dfs_offload;
module_param(dfs_offload, bool, 0644);
MODULE_PARM_DESC(dfs_offload, "set 1 to enable DFS offload to firmware");

static struct dentry *qtnf_debugfs_dir;

bool qtnf_slave_radar_get(void)
{
	return slave_radar;
}

bool qtnf_dfs_offload_get(void)
{
	return dfs_offload;
}

struct qtnf_wmac *qtnf_core_get_mac(const struct qtnf_bus *bus, u8 macid)
{
	struct qtnf_wmac *mac = NULL;

	if (macid >= QTNF_MAX_MAC) {
		pr_err("invalid MAC index %u\n", macid);
		return NULL;
	}

	macid = array_index_nospec(macid, QTNF_MAX_MAC);
	mac = bus->mac[macid];

	if (unlikely(!mac)) {
		pr_err("MAC%u: not initialized\n", macid);
		return NULL;
	}

	return mac;
}

/* Netdev handler for open.
 */
static int qtnf_netdev_open(struct net_device *ndev)
{
	netif_carrier_off(ndev);
	qtnf_netdev_updown(ndev, 1);
	return 0;
}

/* Netdev handler for close.
 */
static int qtnf_netdev_close(struct net_device *ndev)
{
	netif_carrier_off(ndev);
	qtnf_virtual_intf_cleanup(ndev);
	qtnf_netdev_updown(ndev, 0);
	return 0;
}

static void qtnf_packet_send_hi_pri(struct sk_buff *skb)
{
	struct qtnf_vif *vif = qtnf_netdev_get_priv(skb->dev);

	skb_queue_tail(&vif->high_pri_tx_queue, skb);
	queue_work(vif->mac->bus->hprio_workqueue, &vif->high_pri_tx_work);
}

/* Netdev handler for data transmission.
 */
static netdev_tx_t
qtnf_netdev_hard_start_xmit(struct sk_buff *skb, struct net_device *ndev)
{
	struct qtnf_vif *vif;
	struct qtnf_wmac *mac;

	vif = qtnf_netdev_get_priv(ndev);

	if (unlikely(skb->dev != ndev)) {
		pr_err_ratelimited("invalid skb->dev");
		dev_kfree_skb_any(skb);
		return 0;
	}

	if (unlikely(vif->wdev.iftype == NL80211_IFTYPE_UNSPECIFIED)) {
		pr_err_ratelimited("%s: VIF not initialized\n", ndev->name);
		dev_kfree_skb_any(skb);
		return 0;
	}

	mac = vif->mac;
	if (unlikely(!mac)) {
		pr_err_ratelimited("%s: NULL mac pointer", ndev->name);
		dev_kfree_skb_any(skb);
		return 0;
	}

	if (!skb->len || (skb->len > ETH_FRAME_LEN)) {
		pr_err_ratelimited("%s: invalid skb len %d\n", ndev->name,
				   skb->len);
		dev_kfree_skb_any(skb);
		ndev->stats.tx_dropped++;
		return 0;
	}

	/* tx path is enabled: reset vif timeout */
	vif->cons_tx_timeout_cnt = 0;

	if (unlikely(skb->protocol == htons(ETH_P_PAE))) {
		qtnf_packet_send_hi_pri(skb);
		qtnf_update_tx_stats(ndev, skb);
		return NETDEV_TX_OK;
	}

	return qtnf_bus_data_tx(mac->bus, skb, mac->macid, vif->vifid);
}

/* Netdev handler for getting stats.
 */
static void qtnf_netdev_get_stats64(struct net_device *ndev,
				    struct rtnl_link_stats64 *stats)
{
	struct qtnf_vif *vif = qtnf_netdev_get_priv(ndev);
	unsigned int start;
	int cpu;

	netdev_stats_to_stats64(stats, &ndev->stats);

	if (!vif->stats64)
		return;

	for_each_possible_cpu(cpu) {
		struct pcpu_sw_netstats *stats64;
		u64 rx_packets, rx_bytes;
		u64 tx_packets, tx_bytes;

		stats64 = per_cpu_ptr(vif->stats64, cpu);

		do {
			start = u64_stats_fetch_begin_irq(&stats64->syncp);
			rx_packets = stats64->rx_packets;
			rx_bytes = stats64->rx_bytes;
			tx_packets = stats64->tx_packets;
			tx_bytes = stats64->tx_bytes;
		} while (u64_stats_fetch_retry_irq(&stats64->syncp, start));

		stats->rx_packets += rx_packets;
		stats->rx_bytes += rx_bytes;
		stats->tx_packets += tx_packets;
		stats->tx_bytes += tx_bytes;
	}
}

/* Netdev handler for transmission timeout.
 */
static void qtnf_netdev_tx_timeout(struct net_device *ndev, unsigned int txqueue)
{
	struct qtnf_vif *vif = qtnf_netdev_get_priv(ndev);
	struct qtnf_wmac *mac;
	struct qtnf_bus *bus;

	if (unlikely(!vif || !vif->mac || !vif->mac->bus))
		return;

	mac = vif->mac;
	bus = mac->bus;

	pr_warn("VIF%u.%u: Tx timeout- %lu\n", mac->macid, vif->vifid, jiffies);

	qtnf_bus_data_tx_timeout(bus, ndev);
	ndev->stats.tx_errors++;

	if (++vif->cons_tx_timeout_cnt > QTNF_TX_TIMEOUT_TRSHLD) {
		pr_err("Tx timeout threshold exceeded !\n");
		pr_err("schedule interface %s reset !\n", netdev_name(ndev));
		queue_work(bus->workqueue, &vif->reset_work);
	}
}

static int qtnf_netdev_set_mac_address(struct net_device *ndev, void *addr)
{
	struct qtnf_vif *vif = qtnf_netdev_get_priv(ndev);
	struct sockaddr *sa = addr;
	int ret;
	unsigned char old_addr[ETH_ALEN];

	memcpy(old_addr, sa->sa_data, sizeof(old_addr));

	ret = eth_mac_addr(ndev, sa);
	if (ret)
		return ret;

	qtnf_scan_done(vif->mac, true);

	ret = qtnf_cmd_send_change_intf_type(vif, vif->wdev.iftype,
					     vif->wdev.use_4addr,
					     sa->sa_data);

	if (ret)
		memcpy(ndev->dev_addr, old_addr, ETH_ALEN);

	return ret;
}

static int qtnf_netdev_port_parent_id(struct net_device *ndev,
				      struct netdev_phys_item_id *ppid)
{
	const struct qtnf_vif *vif = qtnf_netdev_get_priv(ndev);
	const struct qtnf_bus *bus = vif->mac->bus;

	ppid->id_len = sizeof(bus->hw_id);
	memcpy(&ppid->id, bus->hw_id, ppid->id_len);

	return 0;
}

/* Network device ops handlers */
const struct net_device_ops qtnf_netdev_ops = {
	.ndo_open = qtnf_netdev_open,
	.ndo_stop = qtnf_netdev_close,
	.ndo_start_xmit = qtnf_netdev_hard_start_xmit,
	.ndo_tx_timeout = qtnf_netdev_tx_timeout,
	.ndo_get_stats64 = qtnf_netdev_get_stats64,
	.ndo_set_mac_address = qtnf_netdev_set_mac_address,
	.ndo_get_port_parent_id = qtnf_netdev_port_parent_id,
};

static int qtnf_mac_init_single_band(struct wiphy *wiphy,
				     struct qtnf_wmac *mac,
				     enum nl80211_band band)
{
	int ret;

	wiphy->bands[band] = kzalloc(sizeof(*wiphy->bands[band]), GFP_KERNEL);
	if (!wiphy->bands[band])
		return -ENOMEM;

	wiphy->bands[band]->band = band;

	ret = qtnf_cmd_band_info_get(mac, wiphy->bands[band]);
	if (ret) {
		pr_err("MAC%u: band %u: failed to get chans info: %d\n",
		       mac->macid, band, ret);
		return ret;
	}

	qtnf_band_init_rates(wiphy->bands[band]);

	return 0;
}

static int qtnf_mac_init_bands(struct qtnf_wmac *mac)
{
	struct wiphy *wiphy = priv_to_wiphy(mac);
	int ret = 0;

	if (mac->macinfo.bands_cap & QLINK_BAND_2GHZ) {
		ret = qtnf_mac_init_single_band(wiphy, mac, NL80211_BAND_2GHZ);
		if (ret)
			goto out;
	}

	if (mac->macinfo.bands_cap & QLINK_BAND_5GHZ) {
		ret = qtnf_mac_init_single_band(wiphy, mac, NL80211_BAND_5GHZ);
		if (ret)
			goto out;
	}

	if (mac->macinfo.bands_cap & QLINK_BAND_60GHZ)
		ret = qtnf_mac_init_single_band(wiphy, mac, NL80211_BAND_60GHZ);

out:
	return ret;
}

struct qtnf_vif *qtnf_mac_get_free_vif(struct qtnf_wmac *mac)
{
	struct qtnf_vif *vif;
	int i;

	for (i = 0; i < QTNF_MAX_INTF; i++) {
		vif = &mac->iflist[i];
		if (vif->wdev.iftype == NL80211_IFTYPE_UNSPECIFIED)
			return vif;
	}

	return NULL;
}

struct qtnf_vif *qtnf_mac_get_base_vif(struct qtnf_wmac *mac)
{
	struct qtnf_vif *vif;

	vif = &mac->iflist[QTNF_PRIMARY_VIF_IDX];

	if (vif->wdev.iftype == NL80211_IFTYPE_UNSPECIFIED)
		return NULL;

	return vif;
}

void qtnf_mac_iface_comb_free(struct qtnf_wmac *mac)
{
	struct ieee80211_iface_combination *comb;
	int i;

	if (mac->macinfo.if_comb) {
		for (i = 0; i < mac->macinfo.n_if_comb; i++) {
			comb = &mac->macinfo.if_comb[i];
			kfree(comb->limits);
			comb->limits = NULL;
		}

		kfree(mac->macinfo.if_comb);
		mac->macinfo.if_comb = NULL;
	}
}

void qtnf_mac_ext_caps_free(struct qtnf_wmac *mac)
{
	if (mac->macinfo.extended_capabilities_len) {
		kfree(mac->macinfo.extended_capabilities);
		mac->macinfo.extended_capabilities = NULL;

		kfree(mac->macinfo.extended_capabilities_mask);
		mac->macinfo.extended_capabilities_mask = NULL;

		mac->macinfo.extended_capabilities_len = 0;
	}
}

static void qtnf_vif_reset_handler(struct work_struct *work)
{
	struct qtnf_vif *vif = container_of(work, struct qtnf_vif, reset_work);

	rtnl_lock();

	if (vif->wdev.iftype == NL80211_IFTYPE_UNSPECIFIED) {
		rtnl_unlock();
		return;
	}

	/* stop tx completely */
	netif_tx_stop_all_queues(vif->netdev);
	if (netif_carrier_ok(vif->netdev))
		netif_carrier_off(vif->netdev);

	qtnf_cfg80211_vif_reset(vif);

	rtnl_unlock();
}

static void qtnf_mac_init_primary_intf(struct qtnf_wmac *mac)
{
	struct qtnf_vif *vif = &mac->iflist[QTNF_PRIMARY_VIF_IDX];

	vif->wdev.iftype = NL80211_IFTYPE_STATION;
	vif->bss_priority = QTNF_DEF_BSS_PRIORITY;
	vif->wdev.wiphy = priv_to_wiphy(mac);
	INIT_WORK(&vif->reset_work, qtnf_vif_reset_handler);
	vif->cons_tx_timeout_cnt = 0;
}

static void qtnf_mac_scan_finish(struct qtnf_wmac *mac, bool aborted)
{
	struct cfg80211_scan_info info = {
		.aborted = aborted,
	};

	mutex_lock(&mac->mac_lock);

	if (mac->scan_req) {
		cfg80211_scan_done(mac->scan_req, &info);
		mac->scan_req = NULL;
	}

	mutex_unlock(&mac->mac_lock);
}

void qtnf_scan_done(struct qtnf_wmac *mac, bool aborted)
{
	cancel_delayed_work_sync(&mac->scan_timeout);
	qtnf_mac_scan_finish(mac, aborted);
}

static void qtnf_mac_scan_timeout(struct work_struct *work)
{
	struct qtnf_wmac *mac =
		container_of(work, struct qtnf_wmac, scan_timeout.work);

	pr_warn("MAC%d: scan timed out\n", mac->macid);
	qtnf_mac_scan_finish(mac, true);
}

static void qtnf_vif_send_data_high_pri(struct work_struct *work)
{
	struct qtnf_vif *vif =
		container_of(work, struct qtnf_vif, high_pri_tx_work);
	struct sk_buff *skb;

	if (!vif->netdev ||
	    vif->wdev.iftype == NL80211_IFTYPE_UNSPECIFIED)
		return;

	while ((skb = skb_dequeue(&vif->high_pri_tx_queue))) {
		qtnf_cmd_send_frame(vif, 0, QLINK_FRAME_TX_FLAG_8023,
				    0, skb->data, skb->len);
		dev_kfree_skb_any(skb);
	}
}

static struct qtnf_wmac *qtnf_core_mac_alloc(struct qtnf_bus *bus,
					     unsigned int macid)
{
	struct platform_device *pdev = NULL;
	struct qtnf_wmac *mac;
	struct qtnf_vif *vif;
	struct wiphy *wiphy;
	unsigned int i;

	if (bus->hw_info.num_mac > 1) {
		pdev = platform_device_register_data(bus->dev,
						     dev_name(bus->dev),
						     macid, NULL, 0);
		if (IS_ERR(pdev))
			return ERR_PTR(-EINVAL);
	}

	wiphy = qtnf_wiphy_allocate(bus, pdev);
	if (!wiphy) {
		if (pdev)
			platform_device_unregister(pdev);
		return ERR_PTR(-ENOMEM);
	}

	mac = wiphy_priv(wiphy);

	mac->macid = macid;
	mac->pdev = pdev;
	mac->bus = bus;
	mutex_init(&mac->mac_lock);
	INIT_DELAYED_WORK(&mac->scan_timeout, qtnf_mac_scan_timeout);

	for (i = 0; i < QTNF_MAX_INTF; i++) {
		vif = &mac->iflist[i];

		memset(vif, 0, sizeof(*vif));
		vif->wdev.iftype = NL80211_IFTYPE_UNSPECIFIED;
		vif->mac = mac;
		vif->vifid = i;
		qtnf_sta_list_init(&vif->sta_list);
		INIT_WORK(&vif->high_pri_tx_work, qtnf_vif_send_data_high_pri);
		skb_queue_head_init(&vif->high_pri_tx_queue);
		vif->stats64 = netdev_alloc_pcpu_stats(struct pcpu_sw_netstats);
		if (!vif->stats64)
			pr_warn("VIF%u.%u: per cpu stats allocation failed\n",
				macid, i);
	}

	qtnf_mac_init_primary_intf(mac);
	bus->mac[macid] = mac;

	return mac;
}

static const struct ethtool_ops qtnf_ethtool_ops = {
	.get_drvinfo = cfg80211_get_drvinfo,
};

int qtnf_core_net_attach(struct qtnf_wmac *mac, struct qtnf_vif *vif,
			 const char *name, unsigned char name_assign_type)
{
	struct wiphy *wiphy = priv_to_wiphy(mac);
	struct net_device *dev;
	void *qdev_vif;
	int ret;

	dev = alloc_netdev_mqs(sizeof(struct qtnf_vif *), name,
			       name_assign_type, ether_setup, 1, 1);
	if (!dev)
		return -ENOMEM;

	vif->netdev = dev;

	dev->netdev_ops = &qtnf_netdev_ops;
	dev->needs_free_netdev = true;
	dev_net_set(dev, wiphy_net(wiphy));
	dev->ieee80211_ptr = &vif->wdev;
	ether_addr_copy(dev->dev_addr, vif->mac_addr);
	dev->flags |= IFF_BROADCAST | IFF_MULTICAST;
	dev->watchdog_timeo = QTNF_DEF_WDOG_TIMEOUT;
	dev->tx_queue_len = 100;
	dev->ethtool_ops = &qtnf_ethtool_ops;

	if (qtnf_hwcap_is_set(&mac->bus->hw_info, QLINK_HW_CAPAB_HW_BRIDGE))
		dev->needed_tailroom = sizeof(struct qtnf_frame_meta_info);

	qdev_vif = netdev_priv(dev);
	*((void **)qdev_vif) = vif;

	SET_NETDEV_DEV(dev, wiphy_dev(wiphy));

	ret = register_netdevice(dev);
	if (ret) {
		free_netdev(dev);
		vif->netdev = NULL;
	}

	return ret;
}

static void qtnf_core_mac_detach(struct qtnf_bus *bus, unsigned int macid)
{
	struct qtnf_wmac *mac;
	struct wiphy *wiphy;
	struct qtnf_vif *vif;
	unsigned int i;
	enum nl80211_band band;

	mac = bus->mac[macid];

	if (!mac)
		return;

	wiphy = priv_to_wiphy(mac);

	for (i = 0; i < QTNF_MAX_INTF; i++) {
		vif = &mac->iflist[i];
		rtnl_lock();
		if (vif->netdev &&
		    vif->wdev.iftype != NL80211_IFTYPE_UNSPECIFIED) {
			qtnf_virtual_intf_cleanup(vif->netdev);
			qtnf_del_virtual_intf(wiphy, &vif->wdev);
		}
		rtnl_unlock();
		qtnf_sta_list_free(&vif->sta_list);
		free_percpu(vif->stats64);
	}

	if (mac->wiphy_registered)
		wiphy_unregister(wiphy);

	for (band = NL80211_BAND_2GHZ; band < NUM_NL80211_BANDS; ++band) {
		if (!wiphy->bands[band])
			continue;

		kfree(wiphy->bands[band]->iftype_data);
		wiphy->bands[band]->n_iftype_data = 0;

		kfree(wiphy->bands[band]->channels);
		wiphy->bands[band]->n_channels = 0;

		kfree(wiphy->bands[band]);
		wiphy->bands[band] = NULL;
	}

	platform_device_unregister(mac->pdev);
	qtnf_mac_iface_comb_free(mac);
	qtnf_mac_ext_caps_free(mac);
	kfree(mac->macinfo.wowlan);
	kfree(mac->rd);
	mac->rd = NULL;
	wiphy_free(wiphy);
	bus->mac[macid] = NULL;
}

static int qtnf_core_mac_attach(struct qtnf_bus *bus, unsigned int macid)
{
	struct qtnf_wmac *mac;
	struct qtnf_vif *vif;
	int ret;

	if (!(bus->hw_info.mac_bitmap & BIT(macid))) {
		pr_info("MAC%u is not active in FW\n", macid);
		return 0;
	}

	mac = qtnf_core_mac_alloc(bus, macid);
	if (IS_ERR(mac)) {
		pr_err("MAC%u allocation failed\n", macid);
		return PTR_ERR(mac);
	}

	vif = qtnf_mac_get_base_vif(mac);
	if (!vif) {
		pr_err("MAC%u: primary VIF is not ready\n", macid);
		ret = -EFAULT;
		goto error;
	}

	ret = qtnf_cmd_send_add_intf(vif, vif->wdev.iftype,
				     vif->wdev.use_4addr, vif->mac_addr);
	if (ret) {
		pr_err("MAC%u: failed to add VIF\n", macid);
		goto error;
	}

	ret = qtnf_cmd_get_mac_info(mac);
	if (ret) {
		pr_err("MAC%u: failed to get MAC info\n", macid);
		goto error_del_vif;
	}

	/* Use MAC address of the first active radio as a unique device ID */
	if (is_zero_ether_addr(mac->bus->hw_id))
		ether_addr_copy(mac->bus->hw_id, mac->macaddr);

	ret = qtnf_mac_init_bands(mac);
	if (ret) {
		pr_err("MAC%u: failed to init bands\n", macid);
		goto error_del_vif;
	}

	ret = qtnf_wiphy_register(&bus->hw_info, mac);
	if (ret) {
		pr_err("MAC%u: wiphy registration failed\n", macid);
		goto error_del_vif;
	}

	mac->wiphy_registered = 1;

	rtnl_lock();

	ret = qtnf_core_net_attach(mac, vif, "wlan%d", NET_NAME_ENUM);
	rtnl_unlock();

	if (ret) {
		pr_err("MAC%u: failed to attach netdev\n", macid);
		goto error_del_vif;
	}

	if (qtnf_hwcap_is_set(&bus->hw_info, QLINK_HW_CAPAB_HW_BRIDGE)) {
		ret = qtnf_cmd_netdev_changeupper(vif, vif->netdev->ifindex);
		if (ret)
			goto error;
	}

	pr_debug("MAC%u initialized\n", macid);

	return 0;

error_del_vif:
	qtnf_cmd_send_del_intf(vif);
	vif->wdev.iftype = NL80211_IFTYPE_UNSPECIFIED;
error:
	qtnf_core_mac_detach(bus, macid);
	return ret;
}

bool qtnf_netdev_is_qtn(const struct net_device *ndev)
{
	return ndev->netdev_ops == &qtnf_netdev_ops;
}

static int qtnf_check_br_ports(struct net_device *dev,
			       struct netdev_nested_priv *priv)
{
	struct net_device *ndev = (struct net_device *)priv->data;

	if (dev != ndev && netdev_port_same_parent_id(dev, ndev))
		return -ENOTSUPP;

	return 0;
}

static int qtnf_core_netdevice_event(struct notifier_block *nb,
				     unsigned long event, void *ptr)
{
	struct net_device *ndev = netdev_notifier_info_to_dev(ptr);
	const struct netdev_notifier_changeupper_info *info;
	struct netdev_nested_priv priv = {
		.data = (void *)ndev,
	};
	struct net_device *brdev;
	struct qtnf_vif *vif;
	struct qtnf_bus *bus;
	int br_domain;
	int ret = 0;

	if (!qtnf_netdev_is_qtn(ndev))
		return NOTIFY_DONE;

	if (!net_eq(dev_net(ndev), &init_net))
		return NOTIFY_OK;

	vif = qtnf_netdev_get_priv(ndev);
	bus = vif->mac->bus;

	switch (event) {
	case NETDEV_CHANGEUPPER:
		info = ptr;
		brdev = info->upper_dev;

		if (!netif_is_bridge_master(brdev))
			break;

		pr_debug("[VIF%u.%u] change bridge: %s %s\n",
			 vif->mac->macid, vif->vifid, netdev_name(brdev),
			 info->linking ? "add" : "del");

		if (IS_ENABLED(CONFIG_NET_SWITCHDEV) &&
		    qtnf_hwcap_is_set(&bus->hw_info,
				      QLINK_HW_CAPAB_HW_BRIDGE)) {
			if (info->linking)
				br_domain = brdev->ifindex;
			else
				br_domain = ndev->ifindex;

			ret = qtnf_cmd_netdev_changeupper(vif, br_domain);
		} else {
			ret = netdev_walk_all_lower_dev(brdev,
							qtnf_check_br_ports,
							&priv);
		}

		break;
	default:
		break;
	}

	return notifier_from_errno(ret);
}

int qtnf_core_attach(struct qtnf_bus *bus)
{
	unsigned int i;
	int ret;

	qtnf_trans_init(bus);
	qtnf_bus_data_rx_start(bus);

	bus->workqueue = alloc_ordered_workqueue("QTNF_BUS", 0);
	if (!bus->workqueue) {
		pr_err("failed to alloc main workqueue\n");
		ret = -ENOMEM;
		goto error;
	}

	bus->hprio_workqueue = alloc_workqueue("QTNF_HPRI", WQ_HIGHPRI, 0);
	if (!bus->hprio_workqueue) {
		pr_err("failed to alloc high prio workqueue\n");
		ret = -ENOMEM;
		goto error;
	}

	INIT_WORK(&bus->event_work, qtnf_event_work_handler);

	ret = qtnf_cmd_send_init_fw(bus);
	if (ret) {
		pr_err("failed to init FW: %d\n", ret);
		goto error;
	}

	if (QLINK_VER_MAJOR(bus->hw_info.ql_proto_ver) !=
	    QLINK_PROTO_VER_MAJOR) {
		pr_err("qlink driver vs FW version mismatch: %u vs %u\n",
		       QLINK_PROTO_VER_MAJOR,
		       QLINK_VER_MAJOR(bus->hw_info.ql_proto_ver));
		ret = -EPROTONOSUPPORT;
		goto error;
	}

	bus->fw_state = QTNF_FW_STATE_ACTIVE;
	ret = qtnf_cmd_get_hw_info(bus);
	if (ret) {
		pr_err("failed to get HW info: %d\n", ret);
		goto error;
	}

	if (qtnf_hwcap_is_set(&bus->hw_info, QLINK_HW_CAPAB_HW_BRIDGE) &&
	    bus->bus_ops->data_tx_use_meta_set)
		bus->bus_ops->data_tx_use_meta_set(bus, true);

	if (bus->hw_info.num_mac > QTNF_MAX_MAC) {
		pr_err("no support for number of MACs=%u\n",
		       bus->hw_info.num_mac);
		ret = -ERANGE;
		goto error;
	}

	for (i = 0; i < bus->hw_info.num_mac; i++) {
		ret = qtnf_core_mac_attach(bus, i);

		if (ret) {
			pr_err("MAC%u: attach failed: %d\n", i, ret);
			goto error;
		}
	}

	bus->netdev_nb.notifier_call = qtnf_core_netdevice_event;
	ret = register_netdevice_notifier(&bus->netdev_nb);
	if (ret) {
		pr_err("failed to register netdev notifier: %d\n", ret);
		goto error;
	}

	bus->fw_state = QTNF_FW_STATE_RUNNING;
	return 0;

error:
	qtnf_core_detach(bus);
	return ret;
}
EXPORT_SYMBOL_GPL(qtnf_core_attach);

void qtnf_core_detach(struct qtnf_bus *bus)
{
	unsigned int macid;

	unregister_netdevice_notifier(&bus->netdev_nb);
	qtnf_bus_data_rx_stop(bus);

	for (macid = 0; macid < QTNF_MAX_MAC; macid++)
		qtnf_core_mac_detach(bus, macid);

	if (qtnf_fw_is_up(bus))
		qtnf_cmd_send_deinit_fw(bus);

	bus->fw_state = QTNF_FW_STATE_DETACHED;

	if (bus->workqueue) {
		flush_workqueue(bus->workqueue);
		destroy_workqueue(bus->workqueue);
		bus->workqueue = NULL;
	}

	if (bus->hprio_workqueue) {
		flush_workqueue(bus->hprio_workqueue);
		destroy_workqueue(bus->hprio_workqueue);
		bus->hprio_workqueue = NULL;
	}

	qtnf_trans_free(bus);
}
EXPORT_SYMBOL_GPL(qtnf_core_detach);

static inline int qtnf_is_frame_meta_magic_valid(struct qtnf_frame_meta_info *m)
{
	return m->magic_s == HBM_FRAME_META_MAGIC_PATTERN_S &&
		m->magic_e == HBM_FRAME_META_MAGIC_PATTERN_E;
}

struct net_device *qtnf_classify_skb(struct qtnf_bus *bus, struct sk_buff *skb)
{
	struct qtnf_frame_meta_info *meta;
	struct net_device *ndev = NULL;
	struct qtnf_wmac *mac;
	struct qtnf_vif *vif;

	if (unlikely(bus->fw_state != QTNF_FW_STATE_RUNNING))
		return NULL;

	meta = (struct qtnf_frame_meta_info *)
		(skb_tail_pointer(skb) - sizeof(*meta));

	if (unlikely(!qtnf_is_frame_meta_magic_valid(meta))) {
		pr_err_ratelimited("invalid magic 0x%x:0x%x\n",
				   meta->magic_s, meta->magic_e);
		goto out;
	}

	if (unlikely(meta->macid >= QTNF_MAX_MAC)) {
		pr_err_ratelimited("invalid mac(%u)\n", meta->macid);
		goto out;
	}

	if (unlikely(meta->ifidx >= QTNF_MAX_INTF)) {
		pr_err_ratelimited("invalid vif(%u)\n", meta->ifidx);
		goto out;
	}

	mac = bus->mac[meta->macid];

	if (unlikely(!mac)) {
		pr_err_ratelimited("mac(%d) does not exist\n", meta->macid);
		goto out;
	}

	vif = &mac->iflist[meta->ifidx];

	if (unlikely(vif->wdev.iftype == NL80211_IFTYPE_UNSPECIFIED)) {
		pr_err_ratelimited("vif(%u) does not exists\n", meta->ifidx);
		goto out;
	}

	ndev = vif->netdev;

	if (unlikely(!ndev)) {
		pr_err_ratelimited("netdev for wlan%u.%u does not exists\n",
				   meta->macid, meta->ifidx);
		goto out;
	}

	__skb_trim(skb, skb->len - sizeof(*meta));
	/* Firmware always handles packets that require flooding */
	qtnfmac_switch_mark_skb_flooded(skb);

out:
	return ndev;
}
EXPORT_SYMBOL_GPL(qtnf_classify_skb);

void qtnf_wake_all_queues(struct net_device *ndev)
{
	struct qtnf_vif *vif = qtnf_netdev_get_priv(ndev);
	struct qtnf_wmac *mac;
	struct qtnf_bus *bus;
	int macid;
	int i;

	if (unlikely(!vif || !vif->mac || !vif->mac->bus))
		return;

	bus = vif->mac->bus;

	for (macid = 0; macid < QTNF_MAX_MAC; macid++) {
		if (!(bus->hw_info.mac_bitmap & BIT(macid)))
			continue;

		mac = bus->mac[macid];
		for (i = 0; i < QTNF_MAX_INTF; i++) {
			vif = &mac->iflist[i];
			if (vif->netdev && netif_queue_stopped(vif->netdev))
				netif_tx_wake_all_queues(vif->netdev);
		}
	}
}
EXPORT_SYMBOL_GPL(qtnf_wake_all_queues);

void qtnf_update_rx_stats(struct net_device *ndev, const struct sk_buff *skb)
{
	struct qtnf_vif *vif = qtnf_netdev_get_priv(ndev);
	struct pcpu_sw_netstats *stats64;

	if (unlikely(!vif || !vif->stats64)) {
		ndev->stats.rx_packets++;
		ndev->stats.rx_bytes += skb->len;
		return;
	}

	stats64 = this_cpu_ptr(vif->stats64);

	u64_stats_update_begin(&stats64->syncp);
	stats64->rx_packets++;
	stats64->rx_bytes += skb->len;
	u64_stats_update_end(&stats64->syncp);
}
EXPORT_SYMBOL_GPL(qtnf_update_rx_stats);

void qtnf_update_tx_stats(struct net_device *ndev, const struct sk_buff *skb)
{
	struct qtnf_vif *vif = qtnf_netdev_get_priv(ndev);
	struct pcpu_sw_netstats *stats64;

	if (unlikely(!vif || !vif->stats64)) {
		ndev->stats.tx_packets++;
		ndev->stats.tx_bytes += skb->len;
		return;
	}

	stats64 = this_cpu_ptr(vif->stats64);

	u64_stats_update_begin(&stats64->syncp);
	stats64->tx_packets++;
	stats64->tx_bytes += skb->len;
	u64_stats_update_end(&stats64->syncp);
}
EXPORT_SYMBOL_GPL(qtnf_update_tx_stats);

struct dentry *qtnf_get_debugfs_dir(void)
{
	return qtnf_debugfs_dir;
}
EXPORT_SYMBOL_GPL(qtnf_get_debugfs_dir);

static int __init qtnf_core_register(void)
{
	qtnf_debugfs_dir = debugfs_create_dir(KBUILD_MODNAME, NULL);

	if (IS_ERR(qtnf_debugfs_dir))
		qtnf_debugfs_dir = NULL;

	return 0;
}

static void __exit qtnf_core_exit(void)
{
	debugfs_remove(qtnf_debugfs_dir);
}

module_init(qtnf_core_register);
module_exit(qtnf_core_exit);

MODULE_AUTHOR("Quantenna Communications");
MODULE_DESCRIPTION("Quantenna 802.11 wireless LAN FullMAC driver.");
MODULE_LICENSE("GPL");

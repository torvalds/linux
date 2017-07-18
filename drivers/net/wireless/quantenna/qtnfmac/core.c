/*
 * Copyright (c) 2015-2016 Quantenna Communications, Inc.
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/if_ether.h>

#include "core.h"
#include "bus.h"
#include "trans.h"
#include "commands.h"
#include "cfg80211.h"
#include "event.h"
#include "util.h"

#define QTNF_DMP_MAX_LEN 48
#define QTNF_PRIMARY_VIF_IDX	0

struct qtnf_frame_meta_info {
	u8 magic_s;
	u8 ifidx;
	u8 macid;
	u8 magic_e;
} __packed;

struct qtnf_wmac *qtnf_core_get_mac(const struct qtnf_bus *bus, u8 macid)
{
	struct qtnf_wmac *mac = NULL;

	if (unlikely(macid >= QTNF_MAX_MAC)) {
		pr_err("invalid MAC index %u\n", macid);
		return NULL;
	}

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

/* Netdev handler for data transmission.
 */
static int
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

	return qtnf_bus_data_tx(mac->bus, skb);
}

/* Netdev handler for getting stats.
 */
static struct net_device_stats *qtnf_netdev_get_stats(struct net_device *dev)
{
	return &dev->stats;
}

/* Netdev handler for transmission timeout.
 */
static void qtnf_netdev_tx_timeout(struct net_device *ndev)
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

/* Network device ops handlers */
const struct net_device_ops qtnf_netdev_ops = {
	.ndo_open = qtnf_netdev_open,
	.ndo_stop = qtnf_netdev_close,
	.ndo_start_xmit = qtnf_netdev_hard_start_xmit,
	.ndo_tx_timeout = qtnf_netdev_tx_timeout,
	.ndo_get_stats = qtnf_netdev_get_stats,
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

	ret = qtnf_cmd_get_mac_chan_info(mac, wiphy->bands[band]);
	if (ret) {
		pr_err("MAC%u: band %u: failed to get chans info: %d\n",
		       mac->macid, band, ret);
		return ret;
	}

	qtnf_band_init_rates(wiphy->bands[band]);
	qtnf_band_setup_htvht_caps(&mac->macinfo, wiphy->bands[band]);

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

	vif->wdev.iftype = NL80211_IFTYPE_AP;
	vif->bss_priority = QTNF_DEF_BSS_PRIORITY;
	vif->wdev.wiphy = priv_to_wiphy(mac);
	INIT_WORK(&vif->reset_work, qtnf_vif_reset_handler);
	vif->cons_tx_timeout_cnt = 0;
}

static struct qtnf_wmac *qtnf_core_mac_alloc(struct qtnf_bus *bus,
					     unsigned int macid)
{
	struct wiphy *wiphy;
	struct qtnf_wmac *mac;
	unsigned int i;

	wiphy = qtnf_wiphy_allocate(bus);
	if (!wiphy)
		return ERR_PTR(-ENOMEM);

	mac = wiphy_priv(wiphy);

	mac->macid = macid;
	mac->bus = bus;

	for (i = 0; i < QTNF_MAX_INTF; i++) {
		memset(&mac->iflist[i], 0, sizeof(struct qtnf_vif));
		mac->iflist[i].wdev.iftype = NL80211_IFTYPE_UNSPECIFIED;
		mac->iflist[i].mac = mac;
		mac->iflist[i].vifid = i;
		qtnf_sta_list_init(&mac->iflist[i].sta_list);
	}

	qtnf_mac_init_primary_intf(mac);
	bus->mac[macid] = mac;

	return mac;
}

int qtnf_core_net_attach(struct qtnf_wmac *mac, struct qtnf_vif *vif,
			 const char *name, unsigned char name_assign_type,
			 enum nl80211_iftype iftype)
{
	struct wiphy *wiphy = priv_to_wiphy(mac);
	struct net_device *dev;
	void *qdev_vif;
	int ret;

	dev = alloc_netdev_mqs(sizeof(struct qtnf_vif *), name,
			       name_assign_type, ether_setup, 1, 1);
	if (!dev) {
		memset(&vif->wdev, 0, sizeof(vif->wdev));
		vif->wdev.iftype = NL80211_IFTYPE_UNSPECIFIED;
		return -ENOMEM;
	}

	vif->netdev = dev;

	dev->netdev_ops = &qtnf_netdev_ops;
	dev->needs_free_netdev = true;
	dev_net_set(dev, wiphy_net(wiphy));
	dev->ieee80211_ptr = &vif->wdev;
	dev->ieee80211_ptr->iftype = iftype;
	ether_addr_copy(dev->dev_addr, vif->mac_addr);
	SET_NETDEV_DEV(dev, wiphy_dev(wiphy));
	dev->flags |= IFF_BROADCAST | IFF_MULTICAST;
	dev->watchdog_timeo = QTNF_DEF_WDOG_TIMEOUT;
	dev->tx_queue_len = 100;

	qdev_vif = netdev_priv(dev);
	*((void **)qdev_vif) = vif;

	SET_NETDEV_DEV(dev, mac->bus->dev);

	ret = register_netdevice(dev);
	if (ret) {
		free_netdev(dev);
		vif->wdev.iftype = NL80211_IFTYPE_UNSPECIFIED;
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
	}

	if (mac->wiphy_registered)
		wiphy_unregister(wiphy);

	for (band = NL80211_BAND_2GHZ; band < NUM_NL80211_BANDS; ++band) {
		if (!wiphy->bands[band])
			continue;

		kfree(wiphy->bands[band]->channels);
		wiphy->bands[band]->n_channels = 0;

		kfree(wiphy->bands[band]);
		wiphy->bands[band] = NULL;
	}

	kfree(mac->macinfo.limits);
	kfree(wiphy->iface_combinations);
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

	ret = qtnf_cmd_get_mac_info(mac);
	if (ret) {
		pr_err("MAC%u: failed to get info\n", macid);
		goto error;
	}

	vif = qtnf_mac_get_base_vif(mac);
	if (!vif) {
		pr_err("MAC%u: primary VIF is not ready\n", macid);
		ret = -EFAULT;
		goto error;
	}

	ret = qtnf_cmd_send_add_intf(vif, NL80211_IFTYPE_AP, vif->mac_addr);
	if (ret) {
		pr_err("MAC%u: failed to add VIF\n", macid);
		goto error;
	}

	ret = qtnf_cmd_send_get_phy_params(mac);
	if (ret) {
		pr_err("MAC%u: failed to get PHY settings\n", macid);
		goto error;
	}

	ret = qtnf_mac_init_bands(mac);
	if (ret) {
		pr_err("MAC%u: failed to init bands\n", macid);
		goto error;
	}

	ret = qtnf_wiphy_register(&bus->hw_info, mac);
	if (ret) {
		pr_err("MAC%u: wiphy registration failed\n", macid);
		goto error;
	}

	mac->wiphy_registered = 1;

	rtnl_lock();

	ret = qtnf_core_net_attach(mac, vif, "wlan%d", NET_NAME_ENUM,
				   NL80211_IFTYPE_AP);
	rtnl_unlock();

	if (ret) {
		pr_err("MAC%u: failed to attach netdev\n", macid);
		vif->wdev.iftype = NL80211_IFTYPE_UNSPECIFIED;
		vif->netdev = NULL;
		goto error;
	}

	pr_debug("MAC%u initialized\n", macid);

	return 0;

error:
	qtnf_core_mac_detach(bus, macid);
	return ret;
}

int qtnf_core_attach(struct qtnf_bus *bus)
{
	unsigned int i;
	int ret;

	qtnf_trans_init(bus);

	bus->fw_state = QTNF_FW_STATE_BOOT_DONE;
	qtnf_bus_data_rx_start(bus);

	bus->workqueue = alloc_ordered_workqueue("QTNF_BUS", 0);
	if (!bus->workqueue) {
		pr_err("failed to alloc main workqueue\n");
		ret = -ENOMEM;
		goto error;
	}

	INIT_WORK(&bus->event_work, qtnf_event_work_handler);

	ret = qtnf_cmd_send_init_fw(bus);
	if (ret) {
		pr_err("failed to init FW: %d\n", ret);
		goto error;
	}

	bus->fw_state = QTNF_FW_STATE_ACTIVE;

	ret = qtnf_cmd_get_hw_info(bus);
	if (ret) {
		pr_err("failed to get HW info: %d\n", ret);
		goto error;
	}

	if (bus->hw_info.ql_proto_ver != QLINK_PROTO_VER) {
		pr_err("qlink version mismatch %u != %u\n",
		       QLINK_PROTO_VER, bus->hw_info.ql_proto_ver);
		ret = -EPROTONOSUPPORT;
		goto error;
	}

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

	return 0;

error:
	qtnf_core_detach(bus);

	return ret;
}
EXPORT_SYMBOL_GPL(qtnf_core_attach);

void qtnf_core_detach(struct qtnf_bus *bus)
{
	unsigned int macid;

	qtnf_bus_data_rx_stop(bus);

	for (macid = 0; macid < QTNF_MAX_MAC; macid++)
		qtnf_core_mac_detach(bus, macid);

	if (bus->fw_state == QTNF_FW_STATE_ACTIVE)
		qtnf_cmd_send_deinit_fw(bus);

	bus->fw_state = QTNF_FW_STATE_DEAD;

	if (bus->workqueue) {
		flush_workqueue(bus->workqueue);
		destroy_workqueue(bus->workqueue);
	}

	qtnf_trans_free(bus);
}
EXPORT_SYMBOL_GPL(qtnf_core_detach);

static inline int qtnf_is_frame_meta_magic_valid(struct qtnf_frame_meta_info *m)
{
	return m->magic_s == 0xAB && m->magic_e == 0xBA;
}

struct net_device *qtnf_classify_skb(struct qtnf_bus *bus, struct sk_buff *skb)
{
	struct qtnf_frame_meta_info *meta;
	struct net_device *ndev = NULL;
	struct qtnf_wmac *mac;
	struct qtnf_vif *vif;

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

out:
	return ndev;
}
EXPORT_SYMBOL_GPL(qtnf_classify_skb);

MODULE_AUTHOR("Quantenna Communications");
MODULE_DESCRIPTION("Quantenna 802.11 wireless LAN FullMAC driver.");
MODULE_LICENSE("GPL");

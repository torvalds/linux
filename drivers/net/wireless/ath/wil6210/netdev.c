/*
 * Copyright (c) 2012-2017 Qualcomm Atheros, Inc.
 * Copyright (c) 2018-2019, The Linux Foundation. All rights reserved.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <linux/etherdevice.h>
#include <linux/rtnetlink.h>
#include "wil6210.h"
#include "txrx.h"

bool wil_has_other_active_ifaces(struct wil6210_priv *wil,
				 struct net_device *ndev, bool up, bool ok)
{
	int i;
	struct wil6210_vif *vif;
	struct net_device *ndev_i;

	for (i = 0; i < GET_MAX_VIFS(wil); i++) {
		vif = wil->vifs[i];
		if (vif) {
			ndev_i = vif_to_ndev(vif);
			if (ndev_i != ndev)
				if ((up && (ndev_i->flags & IFF_UP)) ||
				    (ok && netif_carrier_ok(ndev_i)))
					return true;
		}
	}

	return false;
}

bool wil_has_active_ifaces(struct wil6210_priv *wil, bool up, bool ok)
{
	/* use NULL ndev argument to check all interfaces */
	return wil_has_other_active_ifaces(wil, NULL, up, ok);
}

static int wil_open(struct net_device *ndev)
{
	struct wil6210_priv *wil = ndev_to_wil(ndev);
	int rc = 0;

	wil_dbg_misc(wil, "open\n");

	if (debug_fw ||
	    test_bit(WMI_FW_CAPABILITY_WMI_ONLY, wil->fw_capabilities)) {
		wil_err(wil, "while in debug_fw or wmi_only mode\n");
		return -EINVAL;
	}

	if (!wil_has_other_active_ifaces(wil, ndev, true, false)) {
		wil_dbg_misc(wil, "open, first iface\n");
		rc = wil_pm_runtime_get(wil);
		if (rc < 0)
			return rc;

		rc = wil_up(wil);
		if (rc)
			wil_pm_runtime_put(wil);
	}

	return rc;
}

static int wil_stop(struct net_device *ndev)
{
	struct wil6210_priv *wil = ndev_to_wil(ndev);
	int rc = 0;

	wil_dbg_misc(wil, "stop\n");

	if (!wil_has_other_active_ifaces(wil, ndev, true, false)) {
		wil_dbg_misc(wil, "stop, last iface\n");
		rc = wil_down(wil);
		if (!rc)
			wil_pm_runtime_put(wil);
	}

	return rc;
}

static const struct net_device_ops wil_netdev_ops = {
	.ndo_open		= wil_open,
	.ndo_stop		= wil_stop,
	.ndo_start_xmit		= wil_start_xmit,
	.ndo_set_mac_address	= eth_mac_addr,
	.ndo_validate_addr	= eth_validate_addr,
};

static int wil6210_netdev_poll_rx(struct napi_struct *napi, int budget)
{
	struct wil6210_priv *wil = container_of(napi, struct wil6210_priv,
						napi_rx);
	int quota = budget;
	int done;

	wil_rx_handle(wil, &quota);
	done = budget - quota;

	if (done < budget) {
		napi_complete_done(napi, done);
		wil6210_unmask_irq_rx(wil);
		wil_dbg_txrx(wil, "NAPI RX complete\n");
	}

	wil_dbg_txrx(wil, "NAPI RX poll(%d) done %d\n", budget, done);

	return done;
}

static int wil6210_netdev_poll_rx_edma(struct napi_struct *napi, int budget)
{
	struct wil6210_priv *wil = container_of(napi, struct wil6210_priv,
						napi_rx);
	int quota = budget;
	int done;

	wil_rx_handle_edma(wil, &quota);
	done = budget - quota;

	if (done < budget) {
		napi_complete_done(napi, done);
		wil6210_unmask_irq_rx_edma(wil);
		wil_dbg_txrx(wil, "NAPI RX complete\n");
	}

	wil_dbg_txrx(wil, "NAPI RX poll(%d) done %d\n", budget, done);

	return done;
}

static int wil6210_netdev_poll_tx(struct napi_struct *napi, int budget)
{
	struct wil6210_priv *wil = container_of(napi, struct wil6210_priv,
						napi_tx);
	int tx_done = 0;
	uint i;

	/* always process ALL Tx complete, regardless budget - it is fast */
	for (i = 0; i < WIL6210_MAX_TX_RINGS; i++) {
		struct wil_ring *ring = &wil->ring_tx[i];
		struct wil_ring_tx_data *txdata = &wil->ring_tx_data[i];
		struct wil6210_vif *vif;

		if (!ring->va || !txdata->enabled ||
		    txdata->mid >= GET_MAX_VIFS(wil))
			continue;

		vif = wil->vifs[txdata->mid];
		if (unlikely(!vif)) {
			wil_dbg_txrx(wil, "Invalid MID %d\n", txdata->mid);
			continue;
		}

		tx_done += wil_tx_complete(vif, i);
	}

	if (tx_done < budget) {
		napi_complete(napi);
		wil6210_unmask_irq_tx(wil);
		wil_dbg_txrx(wil, "NAPI TX complete\n");
	}

	wil_dbg_txrx(wil, "NAPI TX poll(%d) done %d\n", budget, tx_done);

	return min(tx_done, budget);
}

static int wil6210_netdev_poll_tx_edma(struct napi_struct *napi, int budget)
{
	struct wil6210_priv *wil = container_of(napi, struct wil6210_priv,
						napi_tx);
	int tx_done;
	/* There is only one status TX ring */
	struct wil_status_ring *sring = &wil->srings[wil->tx_sring_idx];

	if (!sring->va)
		return 0;

	tx_done = wil_tx_sring_handler(wil, sring);

	if (tx_done < budget) {
		napi_complete(napi);
		wil6210_unmask_irq_tx_edma(wil);
		wil_dbg_txrx(wil, "NAPI TX complete\n");
	}

	wil_dbg_txrx(wil, "NAPI TX poll(%d) done %d\n", budget, tx_done);

	return min(tx_done, budget);
}

static void wil_dev_setup(struct net_device *dev)
{
	ether_setup(dev);
	dev->max_mtu = mtu_max;
	dev->tx_queue_len = WIL_TX_Q_LEN_DEFAULT;
}

static void wil_vif_deinit(struct wil6210_vif *vif)
{
	del_timer_sync(&vif->scan_timer);
	del_timer_sync(&vif->p2p.discovery_timer);
	cancel_work_sync(&vif->disconnect_worker);
	cancel_work_sync(&vif->p2p.discovery_expired_work);
	cancel_work_sync(&vif->p2p.delayed_listen_work);
	wil_probe_client_flush(vif);
	cancel_work_sync(&vif->probe_client_worker);
	cancel_work_sync(&vif->enable_tx_key_worker);
}

void wil_vif_free(struct wil6210_vif *vif)
{
	struct net_device *ndev = vif_to_ndev(vif);

	wil_vif_deinit(vif);
	free_netdev(ndev);
}

static void wil_ndev_destructor(struct net_device *ndev)
{
	struct wil6210_vif *vif = ndev_to_vif(ndev);

	wil_vif_deinit(vif);
}

static void wil_connect_timer_fn(struct timer_list *t)
{
	struct wil6210_vif *vif = from_timer(vif, t, connect_timer);
	struct wil6210_priv *wil = vif_to_wil(vif);
	bool q;

	wil_err(wil, "Connect timeout detected, disconnect station\n");

	/* reschedule to thread context - disconnect won't
	 * run from atomic context.
	 * queue on wmi_wq to prevent race with connect event.
	 */
	q = queue_work(wil->wmi_wq, &vif->disconnect_worker);
	wil_dbg_wmi(wil, "queue_work of disconnect_worker -> %d\n", q);
}

static void wil_scan_timer_fn(struct timer_list *t)
{
	struct wil6210_vif *vif = from_timer(vif, t, scan_timer);
	struct wil6210_priv *wil = vif_to_wil(vif);

	clear_bit(wil_status_fwready, wil->status);
	wil_err(wil, "Scan timeout detected, start fw error recovery\n");
	wil_fw_error_recovery(wil);
}

static void wil_p2p_discovery_timer_fn(struct timer_list *t)
{
	struct wil6210_vif *vif = from_timer(vif, t, p2p.discovery_timer);
	struct wil6210_priv *wil = vif_to_wil(vif);

	wil_dbg_misc(wil, "p2p_discovery_timer_fn\n");

	schedule_work(&vif->p2p.discovery_expired_work);
}

static void wil_vif_init(struct wil6210_vif *vif)
{
	vif->bcast_ring = -1;

	mutex_init(&vif->probe_client_mutex);

	timer_setup(&vif->connect_timer, wil_connect_timer_fn, 0);
	timer_setup(&vif->scan_timer, wil_scan_timer_fn, 0);
	timer_setup(&vif->p2p.discovery_timer, wil_p2p_discovery_timer_fn, 0);

	INIT_WORK(&vif->probe_client_worker, wil_probe_client_worker);
	INIT_WORK(&vif->disconnect_worker, wil_disconnect_worker);
	INIT_WORK(&vif->p2p.discovery_expired_work, wil_p2p_listen_expired);
	INIT_WORK(&vif->p2p.delayed_listen_work, wil_p2p_delayed_listen_work);
	INIT_WORK(&vif->enable_tx_key_worker, wil_enable_tx_key_worker);

	INIT_LIST_HEAD(&vif->probe_client_pending);

	vif->net_queue_stopped = 1;
}

static u8 wil_vif_find_free_mid(struct wil6210_priv *wil)
{
	u8 i;

	for (i = 0; i < GET_MAX_VIFS(wil); i++) {
		if (!wil->vifs[i])
			return i;
	}

	return U8_MAX;
}

struct wil6210_vif *
wil_vif_alloc(struct wil6210_priv *wil, const char *name,
	      unsigned char name_assign_type, enum nl80211_iftype iftype)
{
	struct net_device *ndev;
	struct wireless_dev *wdev;
	struct wil6210_vif *vif;
	u8 mid;

	mid = wil_vif_find_free_mid(wil);
	if (mid == U8_MAX) {
		wil_err(wil, "no available virtual interface\n");
		return ERR_PTR(-EINVAL);
	}

	ndev = alloc_netdev(sizeof(*vif), name, name_assign_type,
			    wil_dev_setup);
	if (!ndev) {
		dev_err(wil_to_dev(wil), "alloc_netdev failed\n");
		return ERR_PTR(-ENOMEM);
	}
	if (mid == 0) {
		wil->main_ndev = ndev;
	} else {
		ndev->priv_destructor = wil_ndev_destructor;
		ndev->needs_free_netdev = true;
	}

	vif = ndev_to_vif(ndev);
	vif->ndev = ndev;
	vif->wil = wil;
	vif->mid = mid;
	wil_vif_init(vif);

	wdev = &vif->wdev;
	wdev->wiphy = wil->wiphy;
	wdev->iftype = iftype;

	ndev->netdev_ops = &wil_netdev_ops;
	wil_set_ethtoolops(ndev);
	ndev->ieee80211_ptr = wdev;
	ndev->hw_features = NETIF_F_HW_CSUM | NETIF_F_RXCSUM |
			    NETIF_F_SG | NETIF_F_GRO |
			    NETIF_F_TSO | NETIF_F_TSO6;

	ndev->features |= ndev->hw_features;
	SET_NETDEV_DEV(ndev, wiphy_dev(wdev->wiphy));
	wdev->netdev = ndev;
	return vif;
}

void *wil_if_alloc(struct device *dev)
{
	struct wil6210_priv *wil;
	struct wil6210_vif *vif;
	int rc = 0;

	wil = wil_cfg80211_init(dev);
	if (IS_ERR(wil)) {
		dev_err(dev, "wil_cfg80211_init failed\n");
		return wil;
	}

	rc = wil_priv_init(wil);
	if (rc) {
		dev_err(dev, "wil_priv_init failed\n");
		goto out_cfg;
	}

	wil_dbg_misc(wil, "if_alloc\n");

	vif = wil_vif_alloc(wil, "wlan%d", NET_NAME_UNKNOWN,
			    NL80211_IFTYPE_STATION);
	if (IS_ERR(vif)) {
		dev_err(dev, "wil_vif_alloc failed\n");
		rc = -ENOMEM;
		goto out_priv;
	}

	wil->radio_wdev = vif_to_wdev(vif);

	return wil;

out_priv:
	wil_priv_deinit(wil);

out_cfg:
	wil_cfg80211_deinit(wil);

	return ERR_PTR(rc);
}

void wil_if_free(struct wil6210_priv *wil)
{
	struct net_device *ndev = wil->main_ndev;

	wil_dbg_misc(wil, "if_free\n");

	if (!ndev)
		return;

	wil_priv_deinit(wil);

	wil->main_ndev = NULL;
	wil_ndev_destructor(ndev);
	free_netdev(ndev);

	wil_cfg80211_deinit(wil);
}

int wil_vif_add(struct wil6210_priv *wil, struct wil6210_vif *vif)
{
	struct net_device *ndev = vif_to_ndev(vif);
	struct wireless_dev *wdev = vif_to_wdev(vif);
	bool any_active = wil_has_active_ifaces(wil, true, false);
	int rc;

	ASSERT_RTNL();

	if (wil->vifs[vif->mid]) {
		dev_err(&ndev->dev, "VIF with mid %d already in use\n",
			vif->mid);
		return -EEXIST;
	}
	if (any_active && vif->mid != 0) {
		rc = wmi_port_allocate(wil, vif->mid, ndev->dev_addr,
				       wdev->iftype);
		if (rc)
			return rc;
	}
	rc = register_netdevice(ndev);
	if (rc < 0) {
		dev_err(&ndev->dev, "Failed to register netdev: %d\n", rc);
		if (any_active && vif->mid != 0)
			wmi_port_delete(wil, vif->mid);
		return rc;
	}

	wil->vifs[vif->mid] = vif;
	return 0;
}

int wil_if_add(struct wil6210_priv *wil)
{
	struct wiphy *wiphy = wil->wiphy;
	struct net_device *ndev = wil->main_ndev;
	struct wil6210_vif *vif = ndev_to_vif(ndev);
	int rc;

	wil_dbg_misc(wil, "entered");

	strlcpy(wiphy->fw_version, wil->fw_version, sizeof(wiphy->fw_version));

	rc = wiphy_register(wiphy);
	if (rc < 0) {
		wil_err(wil, "failed to register wiphy, err %d\n", rc);
		return rc;
	}

	init_dummy_netdev(&wil->napi_ndev);
	if (wil->use_enhanced_dma_hw) {
		netif_napi_add(&wil->napi_ndev, &wil->napi_rx,
			       wil6210_netdev_poll_rx_edma,
			       WIL6210_NAPI_BUDGET);
		netif_tx_napi_add(&wil->napi_ndev,
				  &wil->napi_tx, wil6210_netdev_poll_tx_edma,
				  WIL6210_NAPI_BUDGET);
	} else {
		netif_napi_add(&wil->napi_ndev, &wil->napi_rx,
			       wil6210_netdev_poll_rx,
			       WIL6210_NAPI_BUDGET);
		netif_tx_napi_add(&wil->napi_ndev,
				  &wil->napi_tx, wil6210_netdev_poll_tx,
				  WIL6210_NAPI_BUDGET);
	}

	wil_update_net_queues_bh(wil, vif, NULL, true);

	rtnl_lock();
	rc = wil_vif_add(wil, vif);
	rtnl_unlock();
	if (rc < 0)
		goto out_wiphy;

	return 0;

out_wiphy:
	wiphy_unregister(wiphy);
	return rc;
}

void wil_vif_remove(struct wil6210_priv *wil, u8 mid)
{
	struct wil6210_vif *vif;
	struct net_device *ndev;
	bool any_active = wil_has_active_ifaces(wil, true, false);

	ASSERT_RTNL();
	if (mid >= GET_MAX_VIFS(wil)) {
		wil_err(wil, "invalid MID: %d\n", mid);
		return;
	}

	vif = wil->vifs[mid];
	if (!vif) {
		wil_err(wil, "MID %d not registered\n", mid);
		return;
	}

	mutex_lock(&wil->mutex);
	wil6210_disconnect(vif, NULL, WLAN_REASON_DEAUTH_LEAVING);
	mutex_unlock(&wil->mutex);

	ndev = vif_to_ndev(vif);
	/* during unregister_netdevice cfg80211_leave may perform operations
	 * such as stop AP, disconnect, so we only clear the VIF afterwards
	 */
	unregister_netdevice(ndev);

	if (any_active && vif->mid != 0)
		wmi_port_delete(wil, vif->mid);

	/* make sure no one is accessing the VIF before removing */
	mutex_lock(&wil->vif_mutex);
	wil->vifs[mid] = NULL;
	/* ensure NAPI code will see the NULL VIF */
	wmb();
	if (test_bit(wil_status_napi_en, wil->status)) {
		napi_synchronize(&wil->napi_rx);
		napi_synchronize(&wil->napi_tx);
	}
	mutex_unlock(&wil->vif_mutex);

	flush_work(&wil->wmi_event_worker);
	del_timer_sync(&vif->connect_timer);
	cancel_work_sync(&vif->disconnect_worker);
	wil_probe_client_flush(vif);
	cancel_work_sync(&vif->probe_client_worker);
	cancel_work_sync(&vif->enable_tx_key_worker);
	/* for VIFs, ndev will be freed by destructor after RTNL is unlocked.
	 * the main interface will be freed in wil_if_free, we need to keep it
	 * a bit longer so logging macros will work.
	 */
}

void wil_if_remove(struct wil6210_priv *wil)
{
	struct net_device *ndev = wil->main_ndev;
	struct wireless_dev *wdev = ndev->ieee80211_ptr;

	wil_dbg_misc(wil, "if_remove\n");

	rtnl_lock();
	wil_vif_remove(wil, 0);
	rtnl_unlock();

	netif_napi_del(&wil->napi_tx);
	netif_napi_del(&wil->napi_rx);

	wiphy_unregister(wdev->wiphy);
}

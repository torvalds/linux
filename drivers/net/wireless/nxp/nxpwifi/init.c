// SPDX-License-Identifier: GPL-2.0-only
/*
 * nxpwifi: HW/FW initialization
 *
 * Copyright 2011-2024 NXP
 */

#include "cfg.h"
#include "util.h"
#include "fw.h"
#include "main.h"
#include "cmdevt.h"
#include "wmm.h"
#include "11n.h"

/* Add a BSS priority node to the adapter list. */
static int nxpwifi_add_bss_prio_tbl(struct nxpwifi_private *priv)
{
	struct nxpwifi_adapter *adapter = priv->adapter;
	struct nxpwifi_bss_prio_node *bss_prio;
	struct nxpwifi_bss_prio_tbl *tbl = adapter->bss_prio_tbl;

	bss_prio = kzalloc_obj(*bss_prio);
	if (!bss_prio)
		return -ENOMEM;

	bss_prio->priv = priv;
	INIT_LIST_HEAD(&bss_prio->list);

	spin_lock_bh(&tbl[priv->bss_priority].bss_prio_lock);
	list_add_tail(&bss_prio->list, &tbl[priv->bss_priority].bss_prio_head);
	spin_unlock_bh(&tbl[priv->bss_priority].bss_prio_lock);

	return 0;
}

static void wakeup_timer_fn(struct timer_list *t)
{
	struct nxpwifi_adapter *adapter = timer_container_of(adapter, t, wakeup_timer);

	nxpwifi_dbg(adapter, ERROR, "Firmware wakeup failed\n");
	adapter->hw_status = NXPWIFI_HW_STATUS_RESET;
	nxpwifi_cancel_all_pending_cmd(adapter);

	if (adapter->if_ops.card_reset)
		adapter->if_ops.card_reset(adapter);
}

/* Initialize priv defaults and lists. */
int nxpwifi_init_priv(struct nxpwifi_private *priv)
{
	u32 i;

	priv->media_connected = false;
	eth_broadcast_addr(priv->curr_addr);
	priv->port_open = false;
	priv->usb_port = NXPWIFI_USB_EP_DATA;
	priv->pkt_tx_ctrl = 0;
	priv->bss_mode = NL80211_IFTYPE_UNSPECIFIED;
	priv->data_rate = 0;	/* Initially indicate the rate as auto */
	priv->is_data_rate_auto = true;
	priv->bcn_avg_factor = DEFAULT_BCN_AVG_FACTOR;
	priv->data_avg_factor = DEFAULT_DATA_AVG_FACTOR;

	priv->auth_flag = 0;
	priv->auth_alg = WLAN_AUTH_NONE;

	priv->sec_info.wep_enabled = 0;
	priv->sec_info.authentication_mode = NL80211_AUTHTYPE_OPEN_SYSTEM;
	priv->sec_info.encryption_mode = 0;
	for (i = 0; i < ARRAY_SIZE(priv->wep_key); i++)
		memset(&priv->wep_key[i], 0, sizeof(struct nxpwifi_wep_key));
	priv->wep_key_curr_index = 0;
	priv->curr_pkt_filter = HOST_ACT_MAC_DYNAMIC_BW_ENABLE |
				HOST_ACT_MAC_RX_ON | HOST_ACT_MAC_TX_ON |
				HOST_ACT_MAC_ETHERNETII_ENABLE;

	priv->beacon_period = 100; /* beacon interval */
	priv->attempted_bss_desc = NULL;
	memset(&priv->curr_bss_params, 0, sizeof(priv->curr_bss_params));
	priv->listen_interval = NXPWIFI_DEFAULT_LISTEN_INTERVAL;

	memset(&priv->prev_ssid, 0, sizeof(priv->prev_ssid));
	memset(&priv->prev_bssid, 0, sizeof(priv->prev_bssid));
	memset(&priv->assoc_rsp_buf, 0, sizeof(priv->assoc_rsp_buf));
	priv->assoc_rsp_size = 0;
	priv->atim_window = 0;
	priv->tx_power_level = 0;
	priv->max_tx_power_level = 0;
	priv->min_tx_power_level = 0;
	priv->tx_ant = 0;
	priv->rx_ant = 0;
	priv->tx_rate = 0;
	priv->rxpd_htinfo = 0;
	priv->rxpd_rate = 0;
	priv->rate_bitmap = 0;
	priv->data_rssi_last = 0;
	priv->data_rssi_avg = 0;
	priv->data_nf_avg = 0;
	priv->data_nf_last = 0;
	priv->bcn_rssi_last = 0;
	priv->bcn_rssi_avg = 0;
	priv->bcn_nf_avg = 0;
	priv->bcn_nf_last = 0;
	memset(&priv->wpa_ie, 0, sizeof(priv->wpa_ie));
	memset(&priv->aes_key, 0, sizeof(priv->aes_key));
	priv->wpa_ie_len = 0;
	priv->wpa_is_gtk_set = false;

	memset(&priv->assoc_tlv_buf, 0, sizeof(priv->assoc_tlv_buf));
	priv->assoc_tlv_buf_len = 0;
	memset(&priv->wps, 0, sizeof(priv->wps));
	memset(&priv->gen_ie_buf, 0, sizeof(priv->gen_ie_buf));
	priv->gen_ie_buf_len = 0;
	memset(priv->vs_ie, 0, sizeof(priv->vs_ie));

	priv->wmm_required = true;
	priv->wmm_enabled = false;
	priv->wmm_qosinfo = 0;
	priv->curr_bcn_buf = NULL;
	priv->curr_bcn_size = 0;
	priv->wps_ie = NULL;
	priv->wps_ie_len = 0;
	priv->ap_11n_enabled = 0;
	memset(&priv->roc_cfg, 0, sizeof(priv->roc_cfg));

	priv->scan_block = false;

	priv->csa_chan = 0;
	priv->csa_expire_time = 0;
	priv->del_list_idx = 0;
	priv->hs2_enabled = false;
	nxpwifi_wmm_init_tos_to_tid_inv(priv);

	nxpwifi_init_11h_params(priv);

	return nxpwifi_add_bss_prio_tbl(priv);
}

/* Allocate command buffer and sleep-confirm skb. */
static int nxpwifi_allocate_adapter(struct nxpwifi_adapter *adapter)
{
	int ret;

	/* Allocate command buffer */
	ret = nxpwifi_alloc_cmd_buffer(adapter);
	if (ret) {
		nxpwifi_dbg(adapter, ERROR,
			    "%s: failed to alloc cmd buffer\n",
			    __func__);
		return ret;
	}

	adapter->sleep_cfm =
		dev_alloc_skb(sizeof(struct nxpwifi_opt_sleep_confirm)
			      + INTF_HEADER_LEN);

	if (!adapter->sleep_cfm) {
		nxpwifi_dbg(adapter, ERROR,
			    "%s: failed to alloc sleep cfm\t"
			    " cmd buffer\n", __func__);
		return -ENOMEM;
	}
	skb_reserve(adapter->sleep_cfm, INTF_HEADER_LEN);

	return 0;
}

/* Initialize adapter defaults and WMM parameters. */
static void nxpwifi_init_adapter(struct nxpwifi_adapter *adapter)
{
	struct nxpwifi_opt_sleep_confirm *sleep_cfm_buf = NULL;

	skb_put(adapter->sleep_cfm, sizeof(struct nxpwifi_opt_sleep_confirm));

	adapter->cmd_sent = false;
	adapter->data_sent = true;

	adapter->intf_hdr_len = INTF_HEADER_LEN;

	adapter->cmd_resp_received = false;
	adapter->event_received = false;
	adapter->data_received = false;
	adapter->assoc_resp_received = false;
	adapter->priv_link_lost = NULL;
	adapter->host_mlme_link_lost = false;

	clear_bit(NXPWIFI_SURPRISE_REMOVED, &adapter->work_flags);

	adapter->hw_status = NXPWIFI_HW_STATUS_INITIALIZING;

	adapter->ps_mode = NXPWIFI_802_11_POWER_MODE_CAM;
	adapter->ps_state = PS_STATE_AWAKE;
	adapter->need_to_wakeup = false;

	adapter->scan_mode = HOST_BSS_MODE_ANY;
	adapter->specific_scan_time = NXPWIFI_SPECIFIC_SCAN_CHAN_TIME;
	adapter->active_scan_time = NXPWIFI_ACTIVE_SCAN_CHAN_TIME;
	adapter->passive_scan_time = NXPWIFI_PASSIVE_SCAN_CHAN_TIME;
	adapter->scan_chan_gap_time = NXPWIFI_DEF_SCAN_CHAN_GAP_TIME;

	adapter->scan_probes = 1;

	adapter->multiple_dtim = 1;

	/* default value in firmware will be used */
	adapter->local_listen_interval = 0;

	adapter->is_deep_sleep = false;

	adapter->delay_null_pkt = false;
	adapter->delay_to_ps = 1000;
	adapter->enhanced_ps_mode = PS_MODE_AUTO;

	/* Disable NULL Pkg generation by default */
	adapter->gen_null_pkt = false;
	/* Disable pps/uapsd mode by default */
	adapter->pps_uapsd_mode = false;
	adapter->pm_wakeup_card_req = false;

	adapter->pm_wakeup_fw_try = false;

	adapter->curr_tx_buf_size = NXPWIFI_TX_DATA_BUF_SIZE_2K;

	clear_bit(NXPWIFI_IS_HS_CONFIGURED, &adapter->work_flags);
	adapter->hs_cfg.conditions = cpu_to_le32(HS_CFG_COND_DEF);
	adapter->hs_cfg.gpio = HS_CFG_GPIO_DEF;
	adapter->hs_cfg.gap = HS_CFG_GAP_DEF;
	adapter->hs_activated = false;

	memset(adapter->event_body, 0, sizeof(adapter->event_body));
	adapter->hw_dot_11n_dev_cap = 0;
	adapter->hw_dev_mcs_support = 0;
	adapter->sec_chan_offset = 0;

	nxpwifi_wmm_init(adapter);
	atomic_set(&adapter->tx_hw_pending, 0);

	sleep_cfm_buf = (struct nxpwifi_opt_sleep_confirm *)
					adapter->sleep_cfm->data;
	memset(sleep_cfm_buf, 0, adapter->sleep_cfm->len);
	sleep_cfm_buf->command = cpu_to_le16(HOST_CMD_802_11_PS_MODE_ENH);
	sleep_cfm_buf->size = cpu_to_le16(adapter->sleep_cfm->len);
	sleep_cfm_buf->result = 0;
	sleep_cfm_buf->action = cpu_to_le16(SLEEP_CONFIRM);
	sleep_cfm_buf->resp_ctrl = cpu_to_le16(RESP_NEEDED);

	memset(&adapter->sleep_period, 0, sizeof(adapter->sleep_period));
	adapter->tx_lock_flag = false;
	adapter->null_pkt_interval = 0;
	adapter->fw_bands = 0;
	adapter->fw_release_number = 0;
	adapter->fw_cap_info = 0;
	memset(&adapter->upld_buf, 0, sizeof(adapter->upld_buf));
	adapter->event_cause = 0;
	adapter->region_code = 0;
	adapter->bcn_miss_time_out = DEFAULT_BCN_MISS_TIMEOUT;
	memset(&adapter->arp_filter, 0, sizeof(adapter->arp_filter));
	adapter->arp_filter_size = 0;
	adapter->max_mgmt_ie_index = MAX_MGMT_IE_INDEX;
	adapter->key_api_major_ver = 0;
	adapter->key_api_minor_ver = 0;
	eth_broadcast_addr(adapter->perm_addr);
	adapter->iface_limit.sta_intf = NXPWIFI_MAX_STA_NUM;
	adapter->iface_limit.uap_intf = NXPWIFI_MAX_UAP_NUM;
	adapter->active_scan_triggered = false;
	timer_setup(&adapter->wakeup_timer, wakeup_timer_fn, 0);
	adapter->devdump_len = 0;
	memset(&adapter->vdll_ctrl, 0, sizeof(adapter->vdll_ctrl));
	adapter->vdll_ctrl.skb = dev_alloc_skb(NXPWIFI_SIZE_OF_CMD_BUFFER);
	atomic_set(&adapter->iface_changing, 0);
}

/* Update trans_start for each Tx queue. */
void nxpwifi_set_trans_start(struct net_device *dev)
{
	int i;

	for (i = 0; i < dev->num_tx_queues; i++)
		txq_trans_cond_update(netdev_get_tx_queue(dev, i));

	netif_trans_update(dev);
}

/* Wake all netdev Tx queues. */
void nxpwifi_wake_up_net_dev_queue(struct net_device *netdev,
				   struct nxpwifi_adapter *adapter)
{
	spin_lock_bh(&adapter->queue_lock);
	netif_tx_wake_all_queues(netdev);
	spin_unlock_bh(&adapter->queue_lock);
}

/* Stop all netdev Tx queues. */
void nxpwifi_stop_net_dev_queue(struct net_device *netdev,
				struct nxpwifi_adapter *adapter)
{
	spin_lock_bh(&adapter->queue_lock);
	netif_tx_stop_all_queues(netdev);
	spin_unlock_bh(&adapter->queue_lock);
}

/* Invalidate list heads. */
static void nxpwifi_invalidate_lists(struct nxpwifi_adapter *adapter)
{
	struct nxpwifi_private *priv;
	s32 i, j;

	list_del(&adapter->cmd_free_q);
	list_del(&adapter->cmd_pending_q);
	list_del(&adapter->scan_pending_q);

	for (i = 0; i < adapter->priv_num; i++)
		list_del(&adapter->bss_prio_tbl[i].bss_prio_head);

	for (i = 0; i < adapter->priv_num; i++) {
		priv = adapter->priv[i];
		for (j = 0; j < MAX_NUM_TID; ++j) {
			list_del(&priv->wmm.tid_tbl_ptr[j].ra_list);
			list_del(&priv->tx_ba_stream_tbl_ptr[j]);
			list_del(&priv->rx_reorder_tbl_ptr[j]);
		}
		list_del(&priv->sta_list);
	}
}

/* Cancel pending work, stop timers, and free adapter buffers. */
static void
nxpwifi_adapter_cleanup(struct nxpwifi_adapter *adapter)
{
	timer_delete(&adapter->wakeup_timer);
	nxpwifi_cancel_all_pending_cmd(adapter);
	wake_up_interruptible(&adapter->cmd_wait_q.wait);
	wake_up_interruptible(&adapter->hs_activate_wait_q);
	if (adapter->vdll_ctrl.vdll_mem) {
		vfree(adapter->vdll_ctrl.vdll_mem);
		adapter->vdll_ctrl.vdll_mem = NULL;
		adapter->vdll_ctrl.vdll_len = 0;
	}
	if (adapter->vdll_ctrl.skb) {
		dev_kfree_skb_any(adapter->vdll_ctrl.skb);
		adapter->vdll_ctrl.skb = NULL;
	}
}

void nxpwifi_free_cmd_buffers(struct nxpwifi_adapter *adapter)
{
	nxpwifi_invalidate_lists(adapter);

	/* Free command buffer */
	nxpwifi_dbg(adapter, INFO, "info: free cmd buffer\n");
	nxpwifi_free_cmd_buffer(adapter);

	if (adapter->sleep_cfm)
		dev_kfree_skb_any(adapter->sleep_cfm);
}

/* Initialize locks and list heads. */
void nxpwifi_init_lock_list(struct nxpwifi_adapter *adapter)
{
	struct nxpwifi_private *priv;
	s32 i, j;

	spin_lock_init(&adapter->int_lock);
	spin_lock_init(&adapter->nxpwifi_cmd_lock);
	spin_lock_init(&adapter->queue_lock);
	for (i = 0; i < adapter->priv_num; i++) {
		priv = adapter->priv[i];
		spin_lock_init(&priv->wmm.ra_list_spinlock);
		spin_lock_init(&priv->curr_bcn_buf_lock);
		spin_lock_init(&priv->sta_list_spinlock);
	}

	/* Initialize cmd_free_q */
	INIT_LIST_HEAD(&adapter->cmd_free_q);
	/* Initialize cmd_pending_q */
	INIT_LIST_HEAD(&adapter->cmd_pending_q);
	/* Initialize scan_pending_q */
	INIT_LIST_HEAD(&adapter->scan_pending_q);

	spin_lock_init(&adapter->cmd_free_q_lock);
	spin_lock_init(&adapter->cmd_pending_q_lock);
	spin_lock_init(&adapter->scan_pending_q_lock);

	skb_queue_head_init(&adapter->rx_mlme_q);
	skb_queue_head_init(&adapter->rx_data_q);
	skb_queue_head_init(&adapter->tx_data_q);

	for (i = 0; i < adapter->priv_num; ++i) {
		INIT_LIST_HEAD(&adapter->bss_prio_tbl[i].bss_prio_head);
		spin_lock_init(&adapter->bss_prio_tbl[i].bss_prio_lock);
	}

	for (i = 0; i < adapter->priv_num; i++) {
		priv = adapter->priv[i];
		for (j = 0; j < MAX_NUM_TID; ++j) {
			INIT_LIST_HEAD(&priv->wmm.tid_tbl_ptr[j].ra_list);
			INIT_LIST_HEAD(&priv->tx_ba_stream_tbl_ptr[j]);
			INIT_LIST_HEAD(&priv->rx_reorder_tbl_ptr[j]);
			spin_lock_init(&priv->tx_ba_stream_tbl_lock[j]);
			spin_lock_init(&priv->rx_reorder_tbl_lock[j]);
		}
		INIT_LIST_HEAD(&priv->sta_list);
		skb_queue_head_init(&priv->bypass_txq);

		spin_lock_init(&priv->ack_status_lock);
		xa_init_flags(&priv->ack_status_frames, XA_FLAGS_ALLOC);
	}
}

/* Init firmware: alloc resources, init adapter/privs, send STA init. */
int nxpwifi_init_fw(struct nxpwifi_adapter *adapter)
{
	int ret;
	struct nxpwifi_private *priv;
	u8 i;
	bool first_sta = true;

	adapter->hw_status = NXPWIFI_HW_STATUS_INITIALIZING;

	/* Allocate memory for member of adapter structure */
	ret = nxpwifi_allocate_adapter(adapter);
	if (ret)
		return ret;

	/* Initialize adapter structure */
	nxpwifi_init_adapter(adapter);

	for (i = 0; i < adapter->priv_num; i++) {
		priv = adapter->priv[i];

		/* Initialize private structure */
		ret = nxpwifi_init_priv(priv);
		if (ret)
			return ret;
	}

	for (i = 0; i < adapter->priv_num; i++) {
		ret = nxpwifi_sta_init_cmd(adapter->priv[i],
					   first_sta, true);
		if (ret)
			return ret;

		first_sta = false;
	}
	spin_lock_bh(&adapter->cmd_pending_q_lock);
	WARN_ON(!list_empty(&adapter->cmd_pending_q));
	spin_unlock_bh(&adapter->cmd_pending_q_lock);
	adapter->hw_status = NXPWIFI_HW_STATUS_READY;

	return 0;
}

/* Remove all BSS priority nodes for this priv. */
static void nxpwifi_delete_bss_prio_tbl(struct nxpwifi_private *priv)
{
	int i;
	struct nxpwifi_adapter *adapter = priv->adapter;
	struct nxpwifi_bss_prio_node *bssprio_node, *tmp_node;
	struct list_head *head;
	spinlock_t *lock; /* bss priority lock */

	for (i = 0; i < adapter->priv_num; ++i) {
		head = &adapter->bss_prio_tbl[i].bss_prio_head;
		lock = &adapter->bss_prio_tbl[i].bss_prio_lock;
		nxpwifi_dbg(adapter, INFO,
			    "info: delete BSS priority table,\t"
			    "bss_type = %d, bss_num = %d, i = %d,\t"
			    "head = %p\n",
			    priv->bss_type, priv->bss_num, i, head);

		{
			spin_lock_bh(lock);
			list_for_each_entry_safe(bssprio_node, tmp_node, head,
						 list) {
				if (bssprio_node->priv == priv) {
					nxpwifi_dbg(adapter, INFO,
						    "info: Delete\t"
						    "node %p, next = %p\n",
						    bssprio_node, tmp_node);
					list_del(&bssprio_node->list);
					kfree(bssprio_node);
				}
			}
			spin_unlock_bh(lock);
		}
	}
}

/* Free per-priv resources and BSS priority entries. */
void nxpwifi_free_priv(struct nxpwifi_private *priv)
{
	nxpwifi_clean_txrx(priv);
	nxpwifi_delete_bss_prio_tbl(priv);
	nxpwifi_free_curr_bcn(priv);
}

/* Shutdown driver: stop work, drain queues, free resources. */
void
nxpwifi_shutdown_drv(struct nxpwifi_adapter *adapter)
{
	struct nxpwifi_private *priv;
	s32 i;
	struct sk_buff *skb;

	/* nxpwifi already shutdown */
	if (adapter->hw_status == NXPWIFI_HW_STATUS_NOT_READY)
		return;

	/* cancel current command */
	if (adapter->curr_cmd) {
		nxpwifi_dbg(adapter, WARN,
			    "curr_cmd is still in processing\n");
		timer_delete_sync(&adapter->cmd_timer);
		nxpwifi_recycle_cmd_node(adapter, adapter->curr_cmd);
		adapter->curr_cmd = NULL;
	}

	/* shut down nxpwifi */
	nxpwifi_dbg(adapter, MSG,
		    "info: shutdown nxpwifi...\n");

	/* Clean up Tx/Rx queues and delete BSS priority table */
	for (i = 0; i < adapter->priv_num; i++) {
		priv = adapter->priv[i];

		nxpwifi_abort_cac(priv);
		nxpwifi_free_priv(priv);
	}

	atomic_set(&adapter->tx_queued, 0);
	while ((skb = skb_dequeue(&adapter->tx_data_q)))
		nxpwifi_write_data_complete(adapter, skb, 0, 0);

	while ((skb = skb_dequeue(&adapter->rx_mlme_q)))
		dev_kfree_skb_any(skb);

	while ((skb = skb_dequeue(&adapter->rx_data_q))) {
		struct nxpwifi_rxinfo *rx_info = NXPWIFI_SKB_RXCB(skb);

		atomic_dec(&adapter->rx_pending);
		priv = adapter->priv[rx_info->bss_num];
		if (priv)
			priv->stats.rx_dropped++;

		dev_kfree_skb_any(skb);
	}

	nxpwifi_adapter_cleanup(adapter);

	adapter->hw_status = NXPWIFI_HW_STATUS_NOT_READY;
}

/* Download FW if needed; check winner and wait until ready. */
int nxpwifi_dnld_fw(struct nxpwifi_adapter *adapter,
		    struct nxpwifi_fw_image *pmfw)
{
	int ret;
	u32 poll_num = 1;

	/* check if firmware is already running */
	ret = adapter->if_ops.check_fw_status(adapter, poll_num);
	if (!ret) {
		nxpwifi_dbg(adapter, MSG,
			    "WLAN FW already running! Skip FW dnld\n");
		return 0;
	}

	/* check if we are the winner for downloading FW */
	if (adapter->if_ops.check_winner_status) {
		adapter->winner = 0;
		ret = adapter->if_ops.check_winner_status(adapter);

		poll_num = MAX_FIRMWARE_POLL_TRIES;
		if (ret) {
			nxpwifi_dbg(adapter, MSG,
				    "WLAN read winner status failed!\n");
			return ret;
		}

		if (!adapter->winner) {
			nxpwifi_dbg(adapter, MSG,
				    "WLAN is not the winner! Skip FW dnld\n");
			goto poll_fw;
		}
	}

	if (pmfw) {
		/* Download firmware with helper */
		ret = adapter->if_ops.prog_fw(adapter, pmfw);
		if (ret) {
			nxpwifi_dbg(adapter, ERROR,
				    "prog_fw failed ret=%#x\n", ret);
			return ret;
		}
	}

poll_fw:
	/* Check if the firmware is downloaded successfully or not */
	ret = adapter->if_ops.check_fw_status(adapter, poll_num);
	if (ret)
		nxpwifi_dbg(adapter, ERROR,
			    "FW failed to be active in time\n");

	return ret;
}
EXPORT_SYMBOL_GPL(nxpwifi_dnld_fw);

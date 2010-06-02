/*
 * Intel Wireless Multicomm 3200 WiFi driver
 *
 * Copyright (C) 2009 Intel Corporation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in
 *     the documentation and/or other materials provided with the
 *     distribution.
 *   * Neither the name of Intel Corporation nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *
 * Intel Corporation <ilw@linux.intel.com>
 * Samuel Ortiz <samuel.ortiz@intel.com>
 * Zhu Yi <yi.zhu@intel.com>
 *
 */

#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/sched.h>
#include <linux/ieee80211.h>
#include <linux/wireless.h>
#include <linux/slab.h>

#include "iwm.h"
#include "debug.h"
#include "bus.h"
#include "umac.h"
#include "commands.h"
#include "hal.h"
#include "fw.h"
#include "rx.h"

static struct iwm_conf def_iwm_conf = {

	.sdio_ior_timeout	= 5000,
	.calib_map		= BIT(CALIB_CFG_DC_IDX)	|
				  BIT(CALIB_CFG_LO_IDX)	|
				  BIT(CALIB_CFG_TX_IQ_IDX)	|
				  BIT(CALIB_CFG_RX_IQ_IDX)	|
				  BIT(SHILOH_PHY_CALIBRATE_BASE_BAND_CMD),
	.expected_calib_map	= BIT(PHY_CALIBRATE_DC_CMD)	|
				  BIT(PHY_CALIBRATE_LO_CMD)	|
				  BIT(PHY_CALIBRATE_TX_IQ_CMD)	|
				  BIT(PHY_CALIBRATE_RX_IQ_CMD)	|
				  BIT(SHILOH_PHY_CALIBRATE_BASE_BAND_CMD),
	.ct_kill_entry		= 110,
	.ct_kill_exit		= 110,
	.reset_on_fatal_err	= 1,
	.auto_connect		= 1,
	.enable_qos		= 1,
	.mode			= UMAC_MODE_BSS,

	/* UMAC configuration */
	.power_index		= 0,
	.frag_threshold		= IEEE80211_MAX_FRAG_THRESHOLD,
	.rts_threshold		= IEEE80211_MAX_RTS_THRESHOLD,
	.cts_to_self		= 0,

	.assoc_timeout		= 2,
	.roam_timeout		= 10,
	.wireless_mode		= WIRELESS_MODE_11A | WIRELESS_MODE_11G |
				  WIRELESS_MODE_11N,

	/* IBSS */
	.ibss_band		= UMAC_BAND_2GHZ,
	.ibss_channel		= 1,

	.mac_addr		= {0x00, 0x02, 0xb3, 0x01, 0x02, 0x03},
};

static int modparam_reset;
module_param_named(reset, modparam_reset, bool, 0644);
MODULE_PARM_DESC(reset, "reset on firmware errors (default 0 [not reset])");

static int modparam_wimax_enable = 1;
module_param_named(wimax_enable, modparam_wimax_enable, bool, 0644);
MODULE_PARM_DESC(wimax_enable, "Enable wimax core (default 1 [wimax enabled])");

int iwm_mode_to_nl80211_iftype(int mode)
{
	switch (mode) {
	case UMAC_MODE_BSS:
		return NL80211_IFTYPE_STATION;
	case UMAC_MODE_IBSS:
		return NL80211_IFTYPE_ADHOC;
	default:
		return NL80211_IFTYPE_UNSPECIFIED;
	}

	return 0;
}

static void iwm_statistics_request(struct work_struct *work)
{
	struct iwm_priv *iwm =
		container_of(work, struct iwm_priv, stats_request.work);

	iwm_send_umac_stats_req(iwm, 0);
}

static void iwm_disconnect_work(struct work_struct *work)
{
	struct iwm_priv *iwm =
		container_of(work, struct iwm_priv, disconnect.work);

	if (iwm->umac_profile_active)
		iwm_invalidate_mlme_profile(iwm);

	clear_bit(IWM_STATUS_ASSOCIATED, &iwm->status);
	iwm->umac_profile_active = 0;
	memset(iwm->bssid, 0, ETH_ALEN);
	iwm->channel = 0;

	iwm_link_off(iwm);

	wake_up_interruptible(&iwm->mlme_queue);

	cfg80211_disconnected(iwm_to_ndev(iwm), 0, NULL, 0, GFP_KERNEL);
}

static void iwm_ct_kill_work(struct work_struct *work)
{
	struct iwm_priv *iwm =
		container_of(work, struct iwm_priv, ct_kill_delay.work);
	struct wiphy *wiphy = iwm_to_wiphy(iwm);

	IWM_INFO(iwm, "CT kill delay timeout\n");

	wiphy_rfkill_set_hw_state(wiphy, false);
}

static int __iwm_up(struct iwm_priv *iwm);
static int __iwm_down(struct iwm_priv *iwm);

static void iwm_reset_worker(struct work_struct *work)
{
	struct iwm_priv *iwm;
	struct iwm_umac_profile *profile = NULL;
	int uninitialized_var(ret), retry = 0;

	iwm = container_of(work, struct iwm_priv, reset_worker);

	/*
	 * XXX: The iwm->mutex is introduced purely for this reset work,
	 * because the other users for iwm_up and iwm_down are only netdev
	 * ndo_open and ndo_stop which are already protected by rtnl.
	 * Please remove iwm->mutex together if iwm_reset_worker() is not
	 * required in the future.
	 */
	if (!mutex_trylock(&iwm->mutex)) {
		IWM_WARN(iwm, "We are in the middle of interface bringing "
			 "UP/DOWN. Skip driver resetting.\n");
		return;
	}

	if (iwm->umac_profile_active) {
		profile = kmalloc(sizeof(struct iwm_umac_profile), GFP_KERNEL);
		if (profile)
			memcpy(profile, iwm->umac_profile, sizeof(*profile));
		else
			IWM_ERR(iwm, "Couldn't alloc memory for profile\n");
	}

	__iwm_down(iwm);

	while (retry++ < 3) {
		ret = __iwm_up(iwm);
		if (!ret)
			break;

		schedule_timeout_uninterruptible(10 * HZ);
	}

	if (ret) {
		IWM_WARN(iwm, "iwm_up() failed: %d\n", ret);

		kfree(profile);
		goto out;
	}

	if (profile) {
		IWM_DBG_MLME(iwm, DBG, "Resend UMAC profile\n");
		memcpy(iwm->umac_profile, profile, sizeof(*profile));
		iwm_send_mlme_profile(iwm);
		kfree(profile);
	} else
		clear_bit(IWM_STATUS_RESETTING, &iwm->status);

 out:
	mutex_unlock(&iwm->mutex);
}

static void iwm_auth_retry_worker(struct work_struct *work)
{
	struct iwm_priv *iwm;
	int i, ret;

	iwm = container_of(work, struct iwm_priv, auth_retry_worker);
	if (iwm->umac_profile_active) {
		ret = iwm_invalidate_mlme_profile(iwm);
		if (ret < 0)
			return;
	}

	iwm->umac_profile->sec.auth_type = UMAC_AUTH_TYPE_LEGACY_PSK;

	ret = iwm_send_mlme_profile(iwm);
	if (ret < 0)
		return;

	for (i = 0; i < IWM_NUM_KEYS; i++)
		if (iwm->keys[i].key_len)
			iwm_set_key(iwm, 0, &iwm->keys[i]);

	iwm_set_tx_key(iwm, iwm->default_key);
}



static void iwm_watchdog(unsigned long data)
{
	struct iwm_priv *iwm = (struct iwm_priv *)data;

	IWM_WARN(iwm, "Watchdog expired: UMAC stalls!\n");

	if (modparam_reset)
		iwm_resetting(iwm);
}

int iwm_priv_init(struct iwm_priv *iwm)
{
	int i, j;
	char name[32];

	iwm->status = 0;
	INIT_LIST_HEAD(&iwm->pending_notif);
	init_waitqueue_head(&iwm->notif_queue);
	init_waitqueue_head(&iwm->nonwifi_queue);
	init_waitqueue_head(&iwm->wifi_ntfy_queue);
	init_waitqueue_head(&iwm->mlme_queue);
	memcpy(&iwm->conf, &def_iwm_conf, sizeof(struct iwm_conf));
	spin_lock_init(&iwm->tx_credit.lock);
	INIT_LIST_HEAD(&iwm->wifi_pending_cmd);
	INIT_LIST_HEAD(&iwm->nonwifi_pending_cmd);
	iwm->wifi_seq_num = UMAC_WIFI_SEQ_NUM_BASE;
	iwm->nonwifi_seq_num = UMAC_NONWIFI_SEQ_NUM_BASE;
	spin_lock_init(&iwm->cmd_lock);
	iwm->scan_id = 1;
	INIT_DELAYED_WORK(&iwm->stats_request, iwm_statistics_request);
	INIT_DELAYED_WORK(&iwm->disconnect, iwm_disconnect_work);
	INIT_DELAYED_WORK(&iwm->ct_kill_delay, iwm_ct_kill_work);
	INIT_WORK(&iwm->reset_worker, iwm_reset_worker);
	INIT_WORK(&iwm->auth_retry_worker, iwm_auth_retry_worker);
	INIT_LIST_HEAD(&iwm->bss_list);

	skb_queue_head_init(&iwm->rx_list);
	INIT_LIST_HEAD(&iwm->rx_tickets);
	spin_lock_init(&iwm->ticket_lock);
	for (i = 0; i < IWM_RX_ID_HASH; i++) {
		INIT_LIST_HEAD(&iwm->rx_packets[i]);
		spin_lock_init(&iwm->packet_lock[i]);
	}

	INIT_WORK(&iwm->rx_worker, iwm_rx_worker);

	iwm->rx_wq = create_singlethread_workqueue(KBUILD_MODNAME "_rx");
	if (!iwm->rx_wq)
		return -EAGAIN;

	for (i = 0; i < IWM_TX_QUEUES; i++) {
		INIT_WORK(&iwm->txq[i].worker, iwm_tx_worker);
		snprintf(name, 32, KBUILD_MODNAME "_tx_%d", i);
		iwm->txq[i].id = i;
		iwm->txq[i].wq = create_singlethread_workqueue(name);
		if (!iwm->txq[i].wq)
			return -EAGAIN;

		skb_queue_head_init(&iwm->txq[i].queue);
		skb_queue_head_init(&iwm->txq[i].stopped_queue);
		spin_lock_init(&iwm->txq[i].lock);
	}

	for (i = 0; i < IWM_NUM_KEYS; i++)
		memset(&iwm->keys[i], 0, sizeof(struct iwm_key));

	iwm->default_key = -1;

	for (i = 0; i < IWM_STA_TABLE_NUM; i++)
		for (j = 0; j < IWM_UMAC_TID_NR; j++) {
			mutex_init(&iwm->sta_table[i].tid_info[j].mutex);
			iwm->sta_table[i].tid_info[j].stopped = false;
		}

	init_timer(&iwm->watchdog);
	iwm->watchdog.function = iwm_watchdog;
	iwm->watchdog.data = (unsigned long)iwm;
	mutex_init(&iwm->mutex);

	iwm->last_fw_err = kzalloc(sizeof(struct iwm_fw_error_hdr),
				   GFP_KERNEL);
	if (iwm->last_fw_err == NULL)
		return -ENOMEM;

	return 0;
}

void iwm_priv_deinit(struct iwm_priv *iwm)
{
	int i;

	for (i = 0; i < IWM_TX_QUEUES; i++)
		destroy_workqueue(iwm->txq[i].wq);

	destroy_workqueue(iwm->rx_wq);
	kfree(iwm->last_fw_err);
}

/*
 * We reset all the structures, and we reset the UMAC.
 * After calling this routine, you're expected to reload
 * the firmware.
 */
void iwm_reset(struct iwm_priv *iwm)
{
	struct iwm_notif *notif, *next;

	if (test_bit(IWM_STATUS_READY, &iwm->status))
		iwm_target_reset(iwm);

	if (test_bit(IWM_STATUS_RESETTING, &iwm->status)) {
		iwm->status = 0;
		set_bit(IWM_STATUS_RESETTING, &iwm->status);
	} else
		iwm->status = 0;
	iwm->scan_id = 1;

	list_for_each_entry_safe(notif, next, &iwm->pending_notif, pending) {
		list_del(&notif->pending);
		kfree(notif->buf);
		kfree(notif);
	}

	iwm_cmd_flush(iwm);

	flush_workqueue(iwm->rx_wq);

	iwm_link_off(iwm);
}

void iwm_resetting(struct iwm_priv *iwm)
{
	set_bit(IWM_STATUS_RESETTING, &iwm->status);

	schedule_work(&iwm->reset_worker);
}

/*
 * Notification code:
 *
 * We're faced with the following issue: Any host command can
 * have an answer or not, and if there's an answer to expect,
 * it can be treated synchronously or asynchronously.
 * To work around the synchronous answer case, we implemented
 * our notification mechanism.
 * When a code path needs to wait for a command response
 * synchronously, it calls notif_handle(), which waits for the
 * right notification to show up, and then process it. Before
 * starting to wait, it registered as a waiter for this specific
 * answer (by toggling a bit in on of the handler_map), so that
 * the rx code knows that it needs to send a notification to the
 * waiting processes. It does so by calling iwm_notif_send(),
 * which adds the notification to the pending notifications list,
 * and then wakes the waiting processes up.
 */
int iwm_notif_send(struct iwm_priv *iwm, struct iwm_wifi_cmd *cmd,
		   u8 cmd_id, u8 source, u8 *buf, unsigned long buf_size)
{
	struct iwm_notif *notif;

	notif = kzalloc(sizeof(struct iwm_notif), GFP_KERNEL);
	if (!notif) {
		IWM_ERR(iwm, "Couldn't alloc memory for notification\n");
		return -ENOMEM;
	}

	INIT_LIST_HEAD(&notif->pending);
	notif->cmd = cmd;
	notif->cmd_id = cmd_id;
	notif->src = source;
	notif->buf = kzalloc(buf_size, GFP_KERNEL);
	if (!notif->buf) {
		IWM_ERR(iwm, "Couldn't alloc notification buffer\n");
		kfree(notif);
		return -ENOMEM;
	}
	notif->buf_size = buf_size;
	memcpy(notif->buf, buf, buf_size);
	list_add_tail(&notif->pending, &iwm->pending_notif);

	wake_up_interruptible(&iwm->notif_queue);

	return 0;
}

static struct iwm_notif *iwm_notif_find(struct iwm_priv *iwm, u32 cmd,
					u8 source)
{
	struct iwm_notif *notif;

	list_for_each_entry(notif, &iwm->pending_notif, pending) {
		if ((notif->cmd_id == cmd) && (notif->src == source)) {
			list_del(&notif->pending);
			return notif;
		}
	}

	return NULL;
}

static struct iwm_notif *iwm_notif_wait(struct iwm_priv *iwm, u32 cmd,
					u8 source, long timeout)
{
	int ret;
	struct iwm_notif *notif;
	unsigned long *map = NULL;

	switch (source) {
	case IWM_SRC_LMAC:
		map = &iwm->lmac_handler_map[0];
		break;
	case IWM_SRC_UMAC:
		map = &iwm->umac_handler_map[0];
		break;
	case IWM_SRC_UDMA:
		map = &iwm->udma_handler_map[0];
		break;
	}

	set_bit(cmd, map);

	ret = wait_event_interruptible_timeout(iwm->notif_queue,
			 ((notif = iwm_notif_find(iwm, cmd, source)) != NULL),
					       timeout);
	clear_bit(cmd, map);

	if (!ret)
		return NULL;

	return notif;
}

int iwm_notif_handle(struct iwm_priv *iwm, u32 cmd, u8 source, long timeout)
{
	int ret;
	struct iwm_notif *notif;

	notif = iwm_notif_wait(iwm, cmd, source, timeout);
	if (!notif)
		return -ETIME;

	ret = iwm_rx_handle_resp(iwm, notif->buf, notif->buf_size, notif->cmd);
	kfree(notif->buf);
	kfree(notif);

	return ret;
}

static int iwm_config_boot_params(struct iwm_priv *iwm)
{
	struct iwm_udma_nonwifi_cmd target_cmd;
	int ret;

	/* check Wimax is off and config debug monitor */
	if (!modparam_wimax_enable) {
		u32 data1 = 0x1f;
		u32 addr1 = 0x606BE258;

		u32 data2_set = 0x0;
		u32 data2_clr = 0x1;
		u32 addr2 = 0x606BE100;

		u32 data3 = 0x1;
		u32 addr3 = 0x606BEC00;

		target_cmd.resp = 0;
		target_cmd.handle_by_hw = 0;
		target_cmd.eop = 1;

		target_cmd.opcode = UMAC_HDI_OUT_OPCODE_WRITE;
		target_cmd.addr = cpu_to_le32(addr1);
		target_cmd.op1_sz = cpu_to_le32(sizeof(u32));
		target_cmd.op2 = 0;

		ret = iwm_hal_send_target_cmd(iwm, &target_cmd, &data1);
		if (ret < 0) {
			IWM_ERR(iwm, "iwm_hal_send_target_cmd failed\n");
			return ret;
		}

		target_cmd.opcode = UMAC_HDI_OUT_OPCODE_READ_MODIFY_WRITE;
		target_cmd.addr = cpu_to_le32(addr2);
		target_cmd.op1_sz = cpu_to_le32(data2_set);
		target_cmd.op2 = cpu_to_le32(data2_clr);

		ret = iwm_hal_send_target_cmd(iwm, &target_cmd, &data1);
		if (ret < 0) {
			IWM_ERR(iwm, "iwm_hal_send_target_cmd failed\n");
			return ret;
		}

		target_cmd.opcode = UMAC_HDI_OUT_OPCODE_WRITE;
		target_cmd.addr = cpu_to_le32(addr3);
		target_cmd.op1_sz = cpu_to_le32(sizeof(u32));
		target_cmd.op2 = 0;

		ret = iwm_hal_send_target_cmd(iwm, &target_cmd, &data3);
		if (ret < 0) {
			IWM_ERR(iwm, "iwm_hal_send_target_cmd failed\n");
			return ret;
		}
	}

	return 0;
}

void iwm_init_default_profile(struct iwm_priv *iwm,
			      struct iwm_umac_profile *profile)
{
	memset(profile, 0, sizeof(struct iwm_umac_profile));

	profile->sec.auth_type = UMAC_AUTH_TYPE_OPEN;
	profile->sec.flags = UMAC_SEC_FLG_LEGACY_PROFILE;
	profile->sec.ucast_cipher = UMAC_CIPHER_TYPE_NONE;
	profile->sec.mcast_cipher = UMAC_CIPHER_TYPE_NONE;

	if (iwm->conf.enable_qos)
		profile->flags |= cpu_to_le16(UMAC_PROFILE_QOS_ALLOWED);

	profile->wireless_mode = iwm->conf.wireless_mode;
	profile->mode = cpu_to_le32(iwm->conf.mode);

	profile->ibss.atim = 0;
	profile->ibss.beacon_interval = 100;
	profile->ibss.join_only = 0;
	profile->ibss.band = iwm->conf.ibss_band;
	profile->ibss.channel = iwm->conf.ibss_channel;
}

void iwm_link_on(struct iwm_priv *iwm)
{
	netif_carrier_on(iwm_to_ndev(iwm));
	netif_tx_wake_all_queues(iwm_to_ndev(iwm));

	iwm_send_umac_stats_req(iwm, 0);
}

void iwm_link_off(struct iwm_priv *iwm)
{
	struct iw_statistics *wstats = &iwm->wstats;
	int i;

	netif_tx_stop_all_queues(iwm_to_ndev(iwm));
	netif_carrier_off(iwm_to_ndev(iwm));

	for (i = 0; i < IWM_TX_QUEUES; i++) {
		skb_queue_purge(&iwm->txq[i].queue);
		skb_queue_purge(&iwm->txq[i].stopped_queue);

		iwm->txq[i].concat_count = 0;
		iwm->txq[i].concat_ptr = iwm->txq[i].concat_buf;

		flush_workqueue(iwm->txq[i].wq);
	}

	iwm_rx_free(iwm);

	cancel_delayed_work_sync(&iwm->stats_request);
	memset(wstats, 0, sizeof(struct iw_statistics));
	wstats->qual.updated = IW_QUAL_ALL_INVALID;

	kfree(iwm->req_ie);
	iwm->req_ie = NULL;
	iwm->req_ie_len = 0;
	kfree(iwm->resp_ie);
	iwm->resp_ie = NULL;
	iwm->resp_ie_len = 0;

	del_timer_sync(&iwm->watchdog);
}

static void iwm_bss_list_clean(struct iwm_priv *iwm)
{
	struct iwm_bss_info *bss, *next;

	list_for_each_entry_safe(bss, next, &iwm->bss_list, node) {
		list_del(&bss->node);
		kfree(bss->bss);
		kfree(bss);
	}
}

static int iwm_channels_init(struct iwm_priv *iwm)
{
	int ret;

	ret = iwm_send_umac_channel_list(iwm);
	if (ret) {
		IWM_ERR(iwm, "Send channel list failed\n");
		return ret;
	}

	ret = iwm_notif_handle(iwm, UMAC_CMD_OPCODE_GET_CHAN_INFO_LIST,
			       IWM_SRC_UMAC, WAIT_NOTIF_TIMEOUT);
	if (ret) {
		IWM_ERR(iwm, "Didn't get a channel list notification\n");
		return ret;
	}

	return 0;
}

static int __iwm_up(struct iwm_priv *iwm)
{
	int ret;
	struct iwm_notif *notif_reboot, *notif_ack = NULL;
	struct wiphy *wiphy = iwm_to_wiphy(iwm);
	u32 wireless_mode;

	ret = iwm_bus_enable(iwm);
	if (ret) {
		IWM_ERR(iwm, "Couldn't enable function\n");
		return ret;
	}

	iwm_rx_setup_handlers(iwm);

	/* Wait for initial BARKER_REBOOT from hardware */
	notif_reboot = iwm_notif_wait(iwm, IWM_BARKER_REBOOT_NOTIFICATION,
				      IWM_SRC_UDMA, 2 * HZ);
	if (!notif_reboot) {
		IWM_ERR(iwm, "Wait for REBOOT_BARKER timeout\n");
		goto err_disable;
	}

	/* We send the barker back */
	ret = iwm_bus_send_chunk(iwm, notif_reboot->buf, 16);
	if (ret) {
		IWM_ERR(iwm, "REBOOT barker response failed\n");
		kfree(notif_reboot);
		goto err_disable;
	}

	kfree(notif_reboot->buf);
	kfree(notif_reboot);

	/* Wait for ACK_BARKER from hardware */
	notif_ack = iwm_notif_wait(iwm, IWM_ACK_BARKER_NOTIFICATION,
				   IWM_SRC_UDMA, 2 * HZ);
	if (!notif_ack) {
		IWM_ERR(iwm, "Wait for ACK_BARKER timeout\n");
		goto err_disable;
	}

	kfree(notif_ack->buf);
	kfree(notif_ack);

	/* We start to config static boot parameters */
	ret = iwm_config_boot_params(iwm);
	if (ret) {
		IWM_ERR(iwm, "Config boot parameters failed\n");
		goto err_disable;
	}

	ret = iwm_read_mac(iwm, iwm_to_ndev(iwm)->dev_addr);
	if (ret) {
		IWM_ERR(iwm, "MAC reading failed\n");
		goto err_disable;
	}
	memcpy(iwm_to_ndev(iwm)->perm_addr, iwm_to_ndev(iwm)->dev_addr,
		ETH_ALEN);

	/* We can load the FWs */
	ret = iwm_load_fw(iwm);
	if (ret) {
		IWM_ERR(iwm, "FW loading failed\n");
		goto err_disable;
	}

	ret = iwm_eeprom_fat_channels(iwm);
	if (ret) {
		IWM_ERR(iwm, "Couldnt read HT channels EEPROM entries\n");
		goto err_fw;
	}

	/*
	 * Read our SKU capabilities.
	 * If it's valid, we AND the configured wireless mode with the
	 * device EEPROM value as the current profile wireless mode.
	 */
	wireless_mode = iwm_eeprom_wireless_mode(iwm);
	if (wireless_mode) {
		iwm->conf.wireless_mode &= wireless_mode;
		if (iwm->umac_profile)
			iwm->umac_profile->wireless_mode =
					iwm->conf.wireless_mode;
	} else
		IWM_ERR(iwm, "Wrong SKU capabilities: 0x%x\n",
			*((u16 *)iwm_eeprom_access(iwm, IWM_EEPROM_SKU_CAP)));

	snprintf(wiphy->fw_version, sizeof(wiphy->fw_version), "L%s_U%s",
		 iwm->lmac_version, iwm->umac_version);

	/* We configure the UMAC and enable the wifi module */
	ret = iwm_send_umac_config(iwm,
			cpu_to_le32(UMAC_RST_CTRL_FLG_WIFI_CORE_EN) |
			cpu_to_le32(UMAC_RST_CTRL_FLG_WIFI_LINK_EN) |
			cpu_to_le32(UMAC_RST_CTRL_FLG_WIFI_MLME_EN));
	if (ret) {
		IWM_ERR(iwm, "UMAC config failed\n");
		goto err_fw;
	}

	ret = iwm_notif_handle(iwm, UMAC_NOTIFY_OPCODE_WIFI_CORE_STATUS,
			       IWM_SRC_UMAC, WAIT_NOTIF_TIMEOUT);
	if (ret) {
		IWM_ERR(iwm, "Didn't get a wifi core status notification\n");
		goto err_fw;
	}

	if (iwm->core_enabled != (UMAC_NTFY_WIFI_CORE_STATUS_LINK_EN |
				  UMAC_NTFY_WIFI_CORE_STATUS_MLME_EN)) {
		IWM_DBG_BOOT(iwm, DBG, "Not all cores enabled:0x%x\n",
			     iwm->core_enabled);
		ret = iwm_notif_handle(iwm, UMAC_NOTIFY_OPCODE_WIFI_CORE_STATUS,
			       IWM_SRC_UMAC, WAIT_NOTIF_TIMEOUT);
		if (ret) {
			IWM_ERR(iwm, "Didn't get a core status notification\n");
			goto err_fw;
		}

		if (iwm->core_enabled != (UMAC_NTFY_WIFI_CORE_STATUS_LINK_EN |
					  UMAC_NTFY_WIFI_CORE_STATUS_MLME_EN)) {
			IWM_ERR(iwm, "Not all cores enabled: 0x%x\n",
				iwm->core_enabled);
			goto err_fw;
		} else {
			IWM_INFO(iwm, "All cores enabled\n");
		}
	}

	ret = iwm_channels_init(iwm);
	if (ret < 0) {
		IWM_ERR(iwm, "Couldn't init channels\n");
		goto err_fw;
	}

	/* Set the READY bit to indicate interface is brought up successfully */
	set_bit(IWM_STATUS_READY, &iwm->status);

	return 0;

 err_fw:
	iwm_eeprom_exit(iwm);

 err_disable:
	ret = iwm_bus_disable(iwm);
	if (ret < 0)
		IWM_ERR(iwm, "Couldn't disable function\n");

	return -EIO;
}

int iwm_up(struct iwm_priv *iwm)
{
	int ret;

	mutex_lock(&iwm->mutex);
	ret = __iwm_up(iwm);
	mutex_unlock(&iwm->mutex);

	return ret;
}

static int __iwm_down(struct iwm_priv *iwm)
{
	int ret;

	/* The interface is already down */
	if (!test_bit(IWM_STATUS_READY, &iwm->status))
		return 0;

	if (iwm->scan_request) {
		cfg80211_scan_done(iwm->scan_request, true);
		iwm->scan_request = NULL;
	}

	clear_bit(IWM_STATUS_READY, &iwm->status);

	iwm_eeprom_exit(iwm);
	iwm_bss_list_clean(iwm);
	iwm_init_default_profile(iwm, iwm->umac_profile);
	iwm->umac_profile_active = false;
	iwm->default_key = -1;
	iwm->core_enabled = 0;

	ret = iwm_bus_disable(iwm);
	if (ret < 0) {
		IWM_ERR(iwm, "Couldn't disable function\n");
		return ret;
	}

	return 0;
}

int iwm_down(struct iwm_priv *iwm)
{
	int ret;

	mutex_lock(&iwm->mutex);
	ret = __iwm_down(iwm);
	mutex_unlock(&iwm->mutex);

	return ret;
}

/*
 * Copyright (c) 2004-2011 Atheros Communications Inc.
 * Copyright (c) 2011-2012 Qualcomm Atheros, Inc.
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

#include "core.h"

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/export.h>
#include <linux/vmalloc.h>

#include "debug.h"
#include "hif-ops.h"
#include "htc-ops.h"
#include "cfg80211.h"

unsigned int debug_mask;
static unsigned int suspend_mode;
static unsigned int wow_mode;
static unsigned int uart_debug;
static unsigned int ath6kl_p2p;
static unsigned int testmode;

module_param(debug_mask, uint, 0644);
module_param(suspend_mode, uint, 0644);
module_param(wow_mode, uint, 0644);
module_param(uart_debug, uint, 0644);
module_param(ath6kl_p2p, uint, 0644);
module_param(testmode, uint, 0644);

void ath6kl_core_tx_complete(struct ath6kl *ar, struct sk_buff *skb)
{
	ath6kl_htc_tx_complete(ar, skb);
}
EXPORT_SYMBOL(ath6kl_core_tx_complete);

void ath6kl_core_rx_complete(struct ath6kl *ar, struct sk_buff *skb, u8 pipe)
{
	ath6kl_htc_rx_complete(ar, skb, pipe);
}
EXPORT_SYMBOL(ath6kl_core_rx_complete);

int ath6kl_core_init(struct ath6kl *ar, enum ath6kl_htc_type htc_type)
{
	struct ath6kl_bmi_target_info targ_info;
	struct net_device *ndev;
	int ret = 0, i;

	switch (htc_type) {
	case ATH6KL_HTC_TYPE_MBOX:
		ath6kl_htc_mbox_attach(ar);
		break;
	case ATH6KL_HTC_TYPE_PIPE:
		ath6kl_htc_pipe_attach(ar);
		break;
	default:
		WARN_ON(1);
		return -ENOMEM;
	}

	ar->ath6kl_wq = create_singlethread_workqueue("ath6kl");
	if (!ar->ath6kl_wq)
		return -ENOMEM;

	ret = ath6kl_bmi_init(ar);
	if (ret)
		goto err_wq;

	/*
	 * Turn on power to get hardware (target) version and leave power
	 * on delibrately as we will boot the hardware anyway within few
	 * seconds.
	 */
	ret = ath6kl_hif_power_on(ar);
	if (ret)
		goto err_bmi_cleanup;

	ret = ath6kl_bmi_get_target_info(ar, &targ_info);
	if (ret)
		goto err_power_off;

	ar->version.target_ver = le32_to_cpu(targ_info.version);
	ar->target_type = le32_to_cpu(targ_info.type);
	ar->wiphy->hw_version = le32_to_cpu(targ_info.version);

	ret = ath6kl_init_hw_params(ar);
	if (ret)
		goto err_power_off;

	ar->htc_target = ath6kl_htc_create(ar);

	if (!ar->htc_target) {
		ret = -ENOMEM;
		goto err_power_off;
	}

	ar->testmode = testmode;

	ret = ath6kl_init_fetch_firmwares(ar);
	if (ret)
		goto err_htc_cleanup;

	/* FIXME: we should free all firmwares in the error cases below */

	/* Indicate that WMI is enabled (although not ready yet) */
	set_bit(WMI_ENABLED, &ar->flag);
	ar->wmi = ath6kl_wmi_init(ar);
	if (!ar->wmi) {
		ath6kl_err("failed to initialize wmi\n");
		ret = -EIO;
		goto err_htc_cleanup;
	}

	ath6kl_dbg(ATH6KL_DBG_TRC, "%s: got wmi @ 0x%p.\n", __func__, ar->wmi);

	/* setup access class priority mappings */
	ar->ac_stream_pri_map[WMM_AC_BK] = 0; /* lowest  */
	ar->ac_stream_pri_map[WMM_AC_BE] = 1;
	ar->ac_stream_pri_map[WMM_AC_VI] = 2;
	ar->ac_stream_pri_map[WMM_AC_VO] = 3; /* highest */

	/* allocate some buffers that handle larger AMSDU frames */
	ath6kl_refill_amsdu_rxbufs(ar, ATH6KL_MAX_AMSDU_RX_BUFFERS);

	ath6kl_cookie_init(ar);

	ar->conf_flags = ATH6KL_CONF_IGNORE_ERP_BARKER |
			 ATH6KL_CONF_ENABLE_11N | ATH6KL_CONF_ENABLE_TX_BURST;

	if (suspend_mode &&
	    suspend_mode >= WLAN_POWER_STATE_CUT_PWR &&
	    suspend_mode <= WLAN_POWER_STATE_WOW)
		ar->suspend_mode = suspend_mode;
	else
		ar->suspend_mode = 0;

	if (suspend_mode == WLAN_POWER_STATE_WOW &&
	    (wow_mode == WLAN_POWER_STATE_CUT_PWR ||
	     wow_mode == WLAN_POWER_STATE_DEEP_SLEEP))
		ar->wow_suspend_mode = wow_mode;
	else
		ar->wow_suspend_mode = 0;

	if (uart_debug)
		ar->conf_flags |= ATH6KL_CONF_UART_DEBUG;

	set_bit(FIRST_BOOT, &ar->flag);

	ath6kl_debug_init(ar);

	ret = ath6kl_init_hw_start(ar);
	if (ret) {
		ath6kl_err("Failed to start hardware: %d\n", ret);
		goto err_rxbuf_cleanup;
	}

	/* give our connected endpoints some buffers */
	ath6kl_rx_refill(ar->htc_target, ar->ctrl_ep);
	ath6kl_rx_refill(ar->htc_target, ar->ac2ep_map[WMM_AC_BE]);

	ret = ath6kl_cfg80211_init(ar);
	if (ret)
		goto err_rxbuf_cleanup;

	ret = ath6kl_debug_init_fs(ar);
	if (ret) {
		wiphy_unregister(ar->wiphy);
		goto err_rxbuf_cleanup;
	}

	for (i = 0; i < ar->vif_max; i++)
		ar->avail_idx_map |= BIT(i);

	rtnl_lock();

	/* Add an initial station interface */
	ndev = ath6kl_interface_add(ar, "wlan%d", NL80211_IFTYPE_STATION, 0,
				    INFRA_NETWORK);

	rtnl_unlock();

	if (!ndev) {
		ath6kl_err("Failed to instantiate a network device\n");
		ret = -ENOMEM;
		wiphy_unregister(ar->wiphy);
		goto err_rxbuf_cleanup;
	}

	ath6kl_dbg(ATH6KL_DBG_TRC, "%s: name=%s dev=0x%p, ar=0x%p\n",
		   __func__, ndev->name, ndev, ar);

	return ret;

err_rxbuf_cleanup:
	ath6kl_debug_cleanup(ar);
	ath6kl_htc_flush_rx_buf(ar->htc_target);
	ath6kl_cleanup_amsdu_rxbufs(ar);
	ath6kl_wmi_shutdown(ar->wmi);
	clear_bit(WMI_ENABLED, &ar->flag);
	ar->wmi = NULL;
err_htc_cleanup:
	ath6kl_htc_cleanup(ar->htc_target);
err_power_off:
	ath6kl_hif_power_off(ar);
err_bmi_cleanup:
	ath6kl_bmi_cleanup(ar);
err_wq:
	destroy_workqueue(ar->ath6kl_wq);

	return ret;
}
EXPORT_SYMBOL(ath6kl_core_init);

struct ath6kl *ath6kl_core_create(struct device *dev)
{
	struct ath6kl *ar;
	u8 ctr;

	ar = ath6kl_cfg80211_create();
	if (!ar)
		return NULL;

	ar->p2p = !!ath6kl_p2p;
	ar->dev = dev;

	ar->vif_max = 1;

	ar->max_norm_iface = 1;

	spin_lock_init(&ar->lock);
	spin_lock_init(&ar->mcastpsq_lock);
	spin_lock_init(&ar->list_lock);

	init_waitqueue_head(&ar->event_wq);
	sema_init(&ar->sem, 1);

	INIT_LIST_HEAD(&ar->amsdu_rx_buffer_queue);
	INIT_LIST_HEAD(&ar->vif_list);

	clear_bit(WMI_ENABLED, &ar->flag);
	clear_bit(SKIP_SCAN, &ar->flag);
	clear_bit(DESTROY_IN_PROGRESS, &ar->flag);

	ar->tx_pwr = 0;
	ar->intra_bss = 1;
	ar->lrssi_roam_threshold = DEF_LRSSI_ROAM_THRESHOLD;

	ar->state = ATH6KL_STATE_OFF;

	memset((u8 *)ar->sta_list, 0,
	       AP_MAX_NUM_STA * sizeof(struct ath6kl_sta));

	/* Init the PS queues */
	for (ctr = 0; ctr < AP_MAX_NUM_STA; ctr++) {
		spin_lock_init(&ar->sta_list[ctr].psq_lock);
		skb_queue_head_init(&ar->sta_list[ctr].psq);
		skb_queue_head_init(&ar->sta_list[ctr].apsdq);
		ar->sta_list[ctr].mgmt_psq_len = 0;
		INIT_LIST_HEAD(&ar->sta_list[ctr].mgmt_psq);
		ar->sta_list[ctr].aggr_conn =
			kzalloc(sizeof(struct aggr_info_conn), GFP_KERNEL);
		if (!ar->sta_list[ctr].aggr_conn) {
			ath6kl_err("Failed to allocate memory for sta aggregation information\n");
			ath6kl_core_destroy(ar);
			return NULL;
		}
	}

	skb_queue_head_init(&ar->mcastpsq);

	memcpy(ar->ap_country_code, DEF_AP_COUNTRY_CODE, 3);

	return ar;
}
EXPORT_SYMBOL(ath6kl_core_create);

void ath6kl_core_cleanup(struct ath6kl *ar)
{
	ath6kl_hif_power_off(ar);

	destroy_workqueue(ar->ath6kl_wq);

	if (ar->htc_target)
		ath6kl_htc_cleanup(ar->htc_target);

	ath6kl_cookie_cleanup(ar);

	ath6kl_cleanup_amsdu_rxbufs(ar);

	ath6kl_bmi_cleanup(ar);

	ath6kl_debug_cleanup(ar);

	kfree(ar->fw_board);
	kfree(ar->fw_otp);
	vfree(ar->fw);
	kfree(ar->fw_patch);
	kfree(ar->fw_testscript);

	ath6kl_cfg80211_cleanup(ar);
}
EXPORT_SYMBOL(ath6kl_core_cleanup);

void ath6kl_core_destroy(struct ath6kl *ar)
{
	ath6kl_cfg80211_destroy(ar);
}
EXPORT_SYMBOL(ath6kl_core_destroy);

MODULE_AUTHOR("Qualcomm Atheros");
MODULE_DESCRIPTION("Core module for AR600x SDIO and USB devices.");
MODULE_LICENSE("Dual BSD/GPL");

/*
 *  Copyright (C) 2008, cozybit Inc.
 *  Copyright (C) 2003-2006, Marvell International Ltd.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or (at
 *  your option) any later version.
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/hardirq.h>
#include <linux/slab.h>

#include <linux/etherdevice.h>
#include <linux/module.h>
#include "libertas_tf.h"

#define DRIVER_RELEASE_VERSION "004.p0"
/* thinfirm version: 5.132.X.pX */
#define LBTF_FW_VER_MIN		0x05840300
#define LBTF_FW_VER_MAX		0x0584ffff
#define QOS_CONTROL_LEN		2

/* Module parameters */
unsigned int lbtf_debug;
EXPORT_SYMBOL_GPL(lbtf_debug);
module_param_named(libertas_tf_debug, lbtf_debug, int, 0644);

static const char lbtf_driver_version[] = "THINFIRM-USB8388-" DRIVER_RELEASE_VERSION
#ifdef DEBUG
	"-dbg"
#endif
	"";

struct workqueue_struct *lbtf_wq;

static const struct ieee80211_channel lbtf_channels[] = {
	{ .center_freq = 2412, .hw_value = 1 },
	{ .center_freq = 2417, .hw_value = 2 },
	{ .center_freq = 2422, .hw_value = 3 },
	{ .center_freq = 2427, .hw_value = 4 },
	{ .center_freq = 2432, .hw_value = 5 },
	{ .center_freq = 2437, .hw_value = 6 },
	{ .center_freq = 2442, .hw_value = 7 },
	{ .center_freq = 2447, .hw_value = 8 },
	{ .center_freq = 2452, .hw_value = 9 },
	{ .center_freq = 2457, .hw_value = 10 },
	{ .center_freq = 2462, .hw_value = 11 },
	{ .center_freq = 2467, .hw_value = 12 },
	{ .center_freq = 2472, .hw_value = 13 },
	{ .center_freq = 2484, .hw_value = 14 },
};

/* This table contains the hardware specific values for the modulation rates. */
static const struct ieee80211_rate lbtf_rates[] = {
	{ .bitrate = 10,
	  .hw_value = 0, },
	{ .bitrate = 20,
	  .hw_value = 1,
	  .flags = IEEE80211_RATE_SHORT_PREAMBLE },
	{ .bitrate = 55,
	  .hw_value = 2,
	  .flags = IEEE80211_RATE_SHORT_PREAMBLE },
	{ .bitrate = 110,
	  .hw_value = 3,
	  .flags = IEEE80211_RATE_SHORT_PREAMBLE },
	{ .bitrate = 60,
	  .hw_value = 5,
	  .flags = 0 },
	{ .bitrate = 90,
	  .hw_value = 6,
	  .flags = 0 },
	{ .bitrate = 120,
	  .hw_value = 7,
	  .flags = 0 },
	{ .bitrate = 180,
	  .hw_value = 8,
	  .flags = 0 },
	{ .bitrate = 240,
	  .hw_value = 9,
	  .flags = 0 },
	{ .bitrate = 360,
	  .hw_value = 10,
	  .flags = 0 },
	{ .bitrate = 480,
	  .hw_value = 11,
	  .flags = 0 },
	{ .bitrate = 540,
	  .hw_value = 12,
	  .flags = 0 },
};

static void lbtf_cmd_work(struct work_struct *work)
{
	struct lbtf_private *priv = container_of(work, struct lbtf_private,
					 cmd_work);

	lbtf_deb_enter(LBTF_DEB_CMD);

	spin_lock_irq(&priv->driver_lock);
	/* command response? */
	if (priv->cmd_response_rxed) {
		priv->cmd_response_rxed = 0;
		spin_unlock_irq(&priv->driver_lock);
		lbtf_process_rx_command(priv);
		spin_lock_irq(&priv->driver_lock);
	}

	if (priv->cmd_timed_out && priv->cur_cmd) {
		struct cmd_ctrl_node *cmdnode = priv->cur_cmd;

		if (++priv->nr_retries > 10) {
			lbtf_complete_command(priv, cmdnode,
					      -ETIMEDOUT);
			priv->nr_retries = 0;
		} else {
			priv->cur_cmd = NULL;

			/* Stick it back at the _top_ of the pending
			 * queue for immediate resubmission */
			list_add(&cmdnode->list, &priv->cmdpendingq);
		}
	}
	priv->cmd_timed_out = 0;
	spin_unlock_irq(&priv->driver_lock);

	if (!priv->fw_ready) {
		lbtf_deb_leave_args(LBTF_DEB_CMD, "fw not ready");
		return;
	}

	/* Execute the next command */
	if (!priv->cur_cmd)
		lbtf_execute_next_command(priv);

	lbtf_deb_leave(LBTF_DEB_CMD);
}

/**
 *  lbtf_setup_firmware: initialize firmware.
 *
 *  @priv    A pointer to struct lbtf_private structure
 *
 *  Returns: 0 on success.
 */
static int lbtf_setup_firmware(struct lbtf_private *priv)
{
	int ret = -1;

	lbtf_deb_enter(LBTF_DEB_FW);
	/*
	 * Read priv address from HW
	 */
	eth_broadcast_addr(priv->current_addr);
	ret = lbtf_update_hw_spec(priv);
	if (ret) {
		ret = -1;
		goto done;
	}

	lbtf_set_mac_control(priv);
	lbtf_set_radio_control(priv);

	ret = 0;
done:
	lbtf_deb_leave_args(LBTF_DEB_FW, "ret: %d", ret);
	return ret;
}

/**
 *  This function handles the timeout of command sending.
 *  It will re-send the same command again.
 */
static void command_timer_fn(unsigned long data)
{
	struct lbtf_private *priv = (struct lbtf_private *)data;
	unsigned long flags;
	lbtf_deb_enter(LBTF_DEB_CMD);

	spin_lock_irqsave(&priv->driver_lock, flags);

	if (!priv->cur_cmd) {
		printk(KERN_DEBUG "libertastf: command timer expired; "
				  "no pending command\n");
		goto out;
	}

	printk(KERN_DEBUG "libertas: command %x timed out\n",
		le16_to_cpu(priv->cur_cmd->cmdbuf->command));

	priv->cmd_timed_out = 1;
	queue_work(lbtf_wq, &priv->cmd_work);
out:
	spin_unlock_irqrestore(&priv->driver_lock, flags);
	lbtf_deb_leave(LBTF_DEB_CMD);
}

static int lbtf_init_adapter(struct lbtf_private *priv)
{
	lbtf_deb_enter(LBTF_DEB_MAIN);
	eth_broadcast_addr(priv->current_addr);
	mutex_init(&priv->lock);

	priv->vif = NULL;
	setup_timer(&priv->command_timer, command_timer_fn,
		(unsigned long)priv);

	INIT_LIST_HEAD(&priv->cmdfreeq);
	INIT_LIST_HEAD(&priv->cmdpendingq);

	spin_lock_init(&priv->driver_lock);

	/* Allocate the command buffers */
	if (lbtf_allocate_cmd_buffer(priv))
		return -1;

	lbtf_deb_leave(LBTF_DEB_MAIN);
	return 0;
}

static void lbtf_free_adapter(struct lbtf_private *priv)
{
	lbtf_deb_enter(LBTF_DEB_MAIN);
	lbtf_free_cmd_buffer(priv);
	del_timer(&priv->command_timer);
	lbtf_deb_leave(LBTF_DEB_MAIN);
}

static void lbtf_op_tx(struct ieee80211_hw *hw,
		       struct ieee80211_tx_control *control,
		       struct sk_buff *skb)
{
	struct lbtf_private *priv = hw->priv;

	priv->skb_to_tx = skb;
	queue_work(lbtf_wq, &priv->tx_work);
	/*
	 * queue will be restarted when we receive transmission feedback if
	 * there are no buffered multicast frames to send
	 */
	ieee80211_stop_queues(priv->hw);
}

static void lbtf_tx_work(struct work_struct *work)
{
	struct lbtf_private *priv = container_of(work, struct lbtf_private,
					 tx_work);
	unsigned int len;
	struct ieee80211_tx_info *info;
	struct txpd *txpd;
	struct sk_buff *skb = NULL;
	int err;

	lbtf_deb_enter(LBTF_DEB_MACOPS | LBTF_DEB_TX);

	if ((priv->vif->type == NL80211_IFTYPE_AP) &&
	    (!skb_queue_empty(&priv->bc_ps_buf)))
		skb = skb_dequeue(&priv->bc_ps_buf);
	else if (priv->skb_to_tx) {
		skb = priv->skb_to_tx;
		priv->skb_to_tx = NULL;
	} else {
		lbtf_deb_leave(LBTF_DEB_MACOPS | LBTF_DEB_TX);
		return;
	}

	len = skb->len;
	info  = IEEE80211_SKB_CB(skb);
	txpd = (struct txpd *)  skb_push(skb, sizeof(struct txpd));

	if (priv->surpriseremoved) {
		dev_kfree_skb_any(skb);
		lbtf_deb_leave(LBTF_DEB_MACOPS | LBTF_DEB_TX);
		return;
	}

	memset(txpd, 0, sizeof(struct txpd));
	/* Activate per-packet rate selection */
	txpd->tx_control |= cpu_to_le32(MRVL_PER_PACKET_RATE |
			     ieee80211_get_tx_rate(priv->hw, info)->hw_value);

	/* copy destination address from 802.11 header */
	memcpy(txpd->tx_dest_addr_high, skb->data + sizeof(struct txpd) + 4,
		ETH_ALEN);
	txpd->tx_packet_length = cpu_to_le16(len);
	txpd->tx_packet_location = cpu_to_le32(sizeof(struct txpd));
	lbtf_deb_hex(LBTF_DEB_TX, "TX Data", skb->data, min_t(unsigned int, skb->len, 100));
	BUG_ON(priv->tx_skb);
	spin_lock_irq(&priv->driver_lock);
	priv->tx_skb = skb;
	err = priv->hw_host_to_card(priv, MVMS_DAT, skb->data, skb->len);
	spin_unlock_irq(&priv->driver_lock);
	if (err) {
		dev_kfree_skb_any(skb);
		priv->tx_skb = NULL;
		pr_err("TX error: %d", err);
	}
	lbtf_deb_leave(LBTF_DEB_MACOPS | LBTF_DEB_TX);
}

static int lbtf_op_start(struct ieee80211_hw *hw)
{
	struct lbtf_private *priv = hw->priv;
	void *card = priv->card;
	int ret = -1;

	lbtf_deb_enter(LBTF_DEB_MACOPS);

	if (!priv->fw_ready)
		/* Upload firmware */
		if (priv->hw_prog_firmware(card))
			goto err_prog_firmware;

	/* poke the firmware */
	priv->capability = WLAN_CAPABILITY_SHORT_PREAMBLE;
	priv->radioon = RADIO_ON;
	priv->mac_control = CMD_ACT_MAC_RX_ON | CMD_ACT_MAC_TX_ON;
	ret = lbtf_setup_firmware(priv);
	if (ret)
		goto err_prog_firmware;

	if ((priv->fwrelease < LBTF_FW_VER_MIN) ||
	    (priv->fwrelease > LBTF_FW_VER_MAX)) {
		ret = -1;
		goto err_prog_firmware;
	}

	printk(KERN_INFO "libertastf: Marvell WLAN 802.11 thinfirm adapter\n");
	lbtf_deb_leave(LBTF_DEB_MACOPS);
	return 0;

err_prog_firmware:
	priv->hw_reset_device(card);
	lbtf_deb_leave_args(LBTF_DEB_MACOPS, "error programming fw; ret=%d", ret);
	return ret;
}

static void lbtf_op_stop(struct ieee80211_hw *hw)
{
	struct lbtf_private *priv = hw->priv;
	unsigned long flags;
	struct sk_buff *skb;

	struct cmd_ctrl_node *cmdnode;

	lbtf_deb_enter(LBTF_DEB_MACOPS);

	/* Flush pending command nodes */
	spin_lock_irqsave(&priv->driver_lock, flags);
	list_for_each_entry(cmdnode, &priv->cmdpendingq, list) {
		cmdnode->result = -ENOENT;
		cmdnode->cmdwaitqwoken = 1;
		wake_up_interruptible(&cmdnode->cmdwait_q);
	}

	spin_unlock_irqrestore(&priv->driver_lock, flags);
	cancel_work_sync(&priv->cmd_work);
	cancel_work_sync(&priv->tx_work);
	while ((skb = skb_dequeue(&priv->bc_ps_buf)))
		dev_kfree_skb_any(skb);
	priv->radioon = RADIO_OFF;
	lbtf_set_radio_control(priv);

	lbtf_deb_leave(LBTF_DEB_MACOPS);
}

static int lbtf_op_add_interface(struct ieee80211_hw *hw,
			struct ieee80211_vif *vif)
{
	struct lbtf_private *priv = hw->priv;
	lbtf_deb_enter(LBTF_DEB_MACOPS);
	if (priv->vif != NULL)
		return -EOPNOTSUPP;

	priv->vif = vif;
	switch (vif->type) {
	case NL80211_IFTYPE_MESH_POINT:
	case NL80211_IFTYPE_AP:
		lbtf_set_mode(priv, LBTF_AP_MODE);
		break;
	case NL80211_IFTYPE_STATION:
		lbtf_set_mode(priv, LBTF_STA_MODE);
		break;
	default:
		priv->vif = NULL;
		return -EOPNOTSUPP;
	}
	lbtf_set_mac_address(priv, (u8 *) vif->addr);
	lbtf_deb_leave(LBTF_DEB_MACOPS);
	return 0;
}

static void lbtf_op_remove_interface(struct ieee80211_hw *hw,
			struct ieee80211_vif *vif)
{
	struct lbtf_private *priv = hw->priv;
	lbtf_deb_enter(LBTF_DEB_MACOPS);

	if (priv->vif->type == NL80211_IFTYPE_AP ||
	    priv->vif->type == NL80211_IFTYPE_MESH_POINT)
		lbtf_beacon_ctrl(priv, 0, 0);
	lbtf_set_mode(priv, LBTF_PASSIVE_MODE);
	lbtf_set_bssid(priv, 0, NULL);
	priv->vif = NULL;
	lbtf_deb_leave(LBTF_DEB_MACOPS);
}

static int lbtf_op_config(struct ieee80211_hw *hw, u32 changed)
{
	struct lbtf_private *priv = hw->priv;
	struct ieee80211_conf *conf = &hw->conf;
	lbtf_deb_enter(LBTF_DEB_MACOPS);

	if (conf->chandef.chan->center_freq != priv->cur_freq) {
		priv->cur_freq = conf->chandef.chan->center_freq;
		lbtf_set_channel(priv, conf->chandef.chan->hw_value);
	}
	lbtf_deb_leave(LBTF_DEB_MACOPS);
	return 0;
}

static u64 lbtf_op_prepare_multicast(struct ieee80211_hw *hw,
				     struct netdev_hw_addr_list *mc_list)
{
	struct lbtf_private *priv = hw->priv;
	int i;
	struct netdev_hw_addr *ha;
	int mc_count = netdev_hw_addr_list_count(mc_list);

	if (!mc_count || mc_count > MRVDRV_MAX_MULTICAST_LIST_SIZE)
		return mc_count;

	priv->nr_of_multicastmacaddr = mc_count;
	i = 0;
	netdev_hw_addr_list_for_each(ha, mc_list)
		memcpy(&priv->multicastlist[i++], ha->addr, ETH_ALEN);

	return mc_count;
}

#define SUPPORTED_FIF_FLAGS  (FIF_PROMISC_IN_BSS | FIF_ALLMULTI)
static void lbtf_op_configure_filter(struct ieee80211_hw *hw,
			unsigned int changed_flags,
			unsigned int *new_flags,
			u64 multicast)
{
	struct lbtf_private *priv = hw->priv;
	int old_mac_control = priv->mac_control;

	lbtf_deb_enter(LBTF_DEB_MACOPS);

	changed_flags &= SUPPORTED_FIF_FLAGS;
	*new_flags &= SUPPORTED_FIF_FLAGS;

	if (!changed_flags) {
		lbtf_deb_leave(LBTF_DEB_MACOPS);
		return;
	}

	if (*new_flags & (FIF_PROMISC_IN_BSS))
		priv->mac_control |= CMD_ACT_MAC_PROMISCUOUS_ENABLE;
	else
		priv->mac_control &= ~CMD_ACT_MAC_PROMISCUOUS_ENABLE;
	if (*new_flags & (FIF_ALLMULTI) ||
	    multicast > MRVDRV_MAX_MULTICAST_LIST_SIZE) {
		priv->mac_control |= CMD_ACT_MAC_ALL_MULTICAST_ENABLE;
		priv->mac_control &= ~CMD_ACT_MAC_MULTICAST_ENABLE;
	} else if (multicast) {
		priv->mac_control |= CMD_ACT_MAC_MULTICAST_ENABLE;
		priv->mac_control &= ~CMD_ACT_MAC_ALL_MULTICAST_ENABLE;
		lbtf_cmd_set_mac_multicast_addr(priv);
	} else {
		priv->mac_control &= ~(CMD_ACT_MAC_MULTICAST_ENABLE |
				       CMD_ACT_MAC_ALL_MULTICAST_ENABLE);
		if (priv->nr_of_multicastmacaddr) {
			priv->nr_of_multicastmacaddr = 0;
			lbtf_cmd_set_mac_multicast_addr(priv);
		}
	}


	if (priv->mac_control != old_mac_control)
		lbtf_set_mac_control(priv);

	lbtf_deb_leave(LBTF_DEB_MACOPS);
}

static void lbtf_op_bss_info_changed(struct ieee80211_hw *hw,
			struct ieee80211_vif *vif,
			struct ieee80211_bss_conf *bss_conf,
			u32 changes)
{
	struct lbtf_private *priv = hw->priv;
	struct sk_buff *beacon;
	lbtf_deb_enter(LBTF_DEB_MACOPS);

	if (changes & (BSS_CHANGED_BEACON | BSS_CHANGED_BEACON_INT)) {
		switch (priv->vif->type) {
		case NL80211_IFTYPE_AP:
		case NL80211_IFTYPE_MESH_POINT:
			beacon = ieee80211_beacon_get(hw, vif);
			if (beacon) {
				lbtf_beacon_set(priv, beacon);
				kfree_skb(beacon);
				lbtf_beacon_ctrl(priv, 1,
						 bss_conf->beacon_int);
			}
			break;
		default:
			break;
		}
	}

	if (changes & BSS_CHANGED_BSSID) {
		bool activate = !is_zero_ether_addr(bss_conf->bssid);
		lbtf_set_bssid(priv, activate, bss_conf->bssid);
	}

	if (changes & BSS_CHANGED_ERP_PREAMBLE) {
		if (bss_conf->use_short_preamble)
			priv->preamble = CMD_TYPE_SHORT_PREAMBLE;
		else
			priv->preamble = CMD_TYPE_LONG_PREAMBLE;
		lbtf_set_radio_control(priv);
	}

	lbtf_deb_leave(LBTF_DEB_MACOPS);
}

static int lbtf_op_get_survey(struct ieee80211_hw *hw, int idx,
				struct survey_info *survey)
{
	struct lbtf_private *priv = hw->priv;
	struct ieee80211_conf *conf = &hw->conf;

	if (idx != 0)
		return -ENOENT;

	survey->channel = conf->chandef.chan;
	survey->filled = SURVEY_INFO_NOISE_DBM;
	survey->noise = priv->noise;

	return 0;
}

static const struct ieee80211_ops lbtf_ops = {
	.tx			= lbtf_op_tx,
	.start			= lbtf_op_start,
	.stop			= lbtf_op_stop,
	.add_interface		= lbtf_op_add_interface,
	.remove_interface	= lbtf_op_remove_interface,
	.config			= lbtf_op_config,
	.prepare_multicast	= lbtf_op_prepare_multicast,
	.configure_filter	= lbtf_op_configure_filter,
	.bss_info_changed	= lbtf_op_bss_info_changed,
	.get_survey		= lbtf_op_get_survey,
};

int lbtf_rx(struct lbtf_private *priv, struct sk_buff *skb)
{
	struct ieee80211_rx_status stats;
	struct rxpd *prxpd;
	int need_padding;
	unsigned int flags;
	struct ieee80211_hdr *hdr;

	lbtf_deb_enter(LBTF_DEB_RX);

	prxpd = (struct rxpd *) skb->data;

	memset(&stats, 0, sizeof(stats));
	if (!(prxpd->status & cpu_to_le16(MRVDRV_RXPD_STATUS_OK)))
		stats.flag |= RX_FLAG_FAILED_FCS_CRC;
	stats.freq = priv->cur_freq;
	stats.band = IEEE80211_BAND_2GHZ;
	stats.signal = prxpd->snr;
	priv->noise = prxpd->nf;
	/* Marvell rate index has a hole at value 4 */
	if (prxpd->rx_rate > 4)
		--prxpd->rx_rate;
	stats.rate_idx = prxpd->rx_rate;
	skb_pull(skb, sizeof(struct rxpd));

	hdr = (struct ieee80211_hdr *)skb->data;
	flags = le32_to_cpu(*(__le32 *)(skb->data + 4));

	need_padding = ieee80211_is_data_qos(hdr->frame_control);
	need_padding ^= ieee80211_has_a4(hdr->frame_control);
	need_padding ^= ieee80211_is_data_qos(hdr->frame_control) &&
			(*ieee80211_get_qos_ctl(hdr) &
			 IEEE80211_QOS_CTL_A_MSDU_PRESENT);

	if (need_padding) {
		memmove(skb->data + 2, skb->data, skb->len);
		skb_reserve(skb, 2);
	}

	memcpy(IEEE80211_SKB_RXCB(skb), &stats, sizeof(stats));

	lbtf_deb_rx("rx data: skb->len-sizeof(RxPd) = %d-%zd = %zd\n",
	       skb->len, sizeof(struct rxpd), skb->len - sizeof(struct rxpd));
	lbtf_deb_hex(LBTF_DEB_RX, "RX Data", skb->data,
	             min_t(unsigned int, skb->len, 100));

	ieee80211_rx_irqsafe(priv->hw, skb);

	lbtf_deb_leave(LBTF_DEB_RX);
	return 0;
}
EXPORT_SYMBOL_GPL(lbtf_rx);

/**
 * lbtf_add_card: Add and initialize the card, no fw upload yet.
 *
 *  @card    A pointer to card
 *
 *  Returns: pointer to struct lbtf_priv.
 */
struct lbtf_private *lbtf_add_card(void *card, struct device *dmdev)
{
	struct ieee80211_hw *hw;
	struct lbtf_private *priv = NULL;

	lbtf_deb_enter(LBTF_DEB_MAIN);

	hw = ieee80211_alloc_hw(sizeof(struct lbtf_private), &lbtf_ops);
	if (!hw)
		goto done;

	priv = hw->priv;
	if (lbtf_init_adapter(priv))
		goto err_init_adapter;

	priv->hw = hw;
	priv->card = card;
	priv->tx_skb = NULL;

	hw->queues = 1;
	hw->flags = IEEE80211_HW_HOST_BROADCAST_PS_BUFFERING;
	hw->extra_tx_headroom = sizeof(struct txpd);
	memcpy(priv->channels, lbtf_channels, sizeof(lbtf_channels));
	memcpy(priv->rates, lbtf_rates, sizeof(lbtf_rates));
	priv->band.n_bitrates = ARRAY_SIZE(lbtf_rates);
	priv->band.bitrates = priv->rates;
	priv->band.n_channels = ARRAY_SIZE(lbtf_channels);
	priv->band.channels = priv->channels;
	hw->wiphy->bands[IEEE80211_BAND_2GHZ] = &priv->band;
	hw->wiphy->interface_modes =
		BIT(NL80211_IFTYPE_STATION) |
		BIT(NL80211_IFTYPE_ADHOC);
	skb_queue_head_init(&priv->bc_ps_buf);

	SET_IEEE80211_DEV(hw, dmdev);

	INIT_WORK(&priv->cmd_work, lbtf_cmd_work);
	INIT_WORK(&priv->tx_work, lbtf_tx_work);
	if (ieee80211_register_hw(hw))
		goto err_init_adapter;

	goto done;

err_init_adapter:
	lbtf_free_adapter(priv);
	ieee80211_free_hw(hw);
	priv = NULL;

done:
	lbtf_deb_leave_args(LBTF_DEB_MAIN, "priv %p", priv);
	return priv;
}
EXPORT_SYMBOL_GPL(lbtf_add_card);


int lbtf_remove_card(struct lbtf_private *priv)
{
	struct ieee80211_hw *hw = priv->hw;

	lbtf_deb_enter(LBTF_DEB_MAIN);

	priv->surpriseremoved = 1;
	del_timer(&priv->command_timer);
	lbtf_free_adapter(priv);
	priv->hw = NULL;
	ieee80211_unregister_hw(hw);
	ieee80211_free_hw(hw);

    lbtf_deb_leave(LBTF_DEB_MAIN);
	return 0;
}
EXPORT_SYMBOL_GPL(lbtf_remove_card);

void lbtf_send_tx_feedback(struct lbtf_private *priv, u8 retrycnt, u8 fail)
{
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(priv->tx_skb);

	ieee80211_tx_info_clear_status(info);
	/*
	 * Commented out, otherwise we never go beyond 1Mbit/s using mac80211
	 * default pid rc algorithm.
	 *
	 * info->status.retry_count = MRVL_DEFAULT_RETRIES - retrycnt;
	 */
	if (!(info->flags & IEEE80211_TX_CTL_NO_ACK) && !fail)
		info->flags |= IEEE80211_TX_STAT_ACK;
	skb_pull(priv->tx_skb, sizeof(struct txpd));
	ieee80211_tx_status_irqsafe(priv->hw, priv->tx_skb);
	priv->tx_skb = NULL;
	if (!priv->skb_to_tx && skb_queue_empty(&priv->bc_ps_buf))
		ieee80211_wake_queues(priv->hw);
	else
		queue_work(lbtf_wq, &priv->tx_work);
}
EXPORT_SYMBOL_GPL(lbtf_send_tx_feedback);

void lbtf_bcn_sent(struct lbtf_private *priv)
{
	struct sk_buff *skb = NULL;

	if (priv->vif->type != NL80211_IFTYPE_AP)
		return;

	if (skb_queue_empty(&priv->bc_ps_buf)) {
		bool tx_buff_bc = false;

		while ((skb = ieee80211_get_buffered_bc(priv->hw, priv->vif))) {
			skb_queue_tail(&priv->bc_ps_buf, skb);
			tx_buff_bc = true;
		}
		if (tx_buff_bc) {
			ieee80211_stop_queues(priv->hw);
			queue_work(lbtf_wq, &priv->tx_work);
		}
	}

	skb = ieee80211_beacon_get(priv->hw, priv->vif);

	if (skb) {
		lbtf_beacon_set(priv, skb);
		kfree_skb(skb);
	}
}
EXPORT_SYMBOL_GPL(lbtf_bcn_sent);

static int __init lbtf_init_module(void)
{
	lbtf_deb_enter(LBTF_DEB_MAIN);
	lbtf_wq = create_workqueue("libertastf");
	if (lbtf_wq == NULL) {
		printk(KERN_ERR "libertastf: couldn't create workqueue\n");
		return -ENOMEM;
	}
	lbtf_deb_leave(LBTF_DEB_MAIN);
	return 0;
}

static void __exit lbtf_exit_module(void)
{
	lbtf_deb_enter(LBTF_DEB_MAIN);
	destroy_workqueue(lbtf_wq);
	lbtf_deb_leave(LBTF_DEB_MAIN);
}

module_init(lbtf_init_module);
module_exit(lbtf_exit_module);

MODULE_DESCRIPTION("Libertas WLAN Thinfirm Driver Library");
MODULE_AUTHOR("Cozybit Inc.");
MODULE_LICENSE("GPL");

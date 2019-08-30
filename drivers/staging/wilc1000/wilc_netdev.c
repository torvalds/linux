// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2012 - 2018 Microchip Technology Inc., and its subsidiaries.
 * All rights reserved.
 */

#include <linux/irq.h>
#include <linux/kthread.h>
#include <linux/firmware.h>
#include <linux/netdevice.h>
#include <linux/inetdevice.h>

#include "wilc_wfi_cfgoperations.h"

#define WILC_MULTICAST_TABLE_SIZE	8

static irqreturn_t isr_uh_routine(int irq, void *user_data)
{
	struct net_device *dev = user_data;
	struct wilc_vif *vif = netdev_priv(dev);
	struct wilc *wilc = vif->wilc;

	if (wilc->close) {
		netdev_err(dev, "Can't handle UH interrupt\n");
		return IRQ_HANDLED;
	}
	return IRQ_WAKE_THREAD;
}

static irqreturn_t isr_bh_routine(int irq, void *userdata)
{
	struct net_device *dev = userdata;
	struct wilc_vif *vif = netdev_priv(userdata);
	struct wilc *wilc = vif->wilc;

	if (wilc->close) {
		netdev_err(dev, "Can't handle BH interrupt\n");
		return IRQ_HANDLED;
	}

	wilc_handle_isr(wilc);

	return IRQ_HANDLED;
}

static int init_irq(struct net_device *dev)
{
	int ret = 0;
	struct wilc_vif *vif = netdev_priv(dev);
	struct wilc *wl = vif->wilc;

	ret = gpiod_direction_input(wl->gpio_irq);
	if (ret) {
		netdev_err(dev, "could not obtain gpio for WILC_INTR\n");
		return ret;
	}

	wl->dev_irq_num = gpiod_to_irq(wl->gpio_irq);

	ret = request_threaded_irq(wl->dev_irq_num, isr_uh_routine,
				   isr_bh_routine,
				   IRQF_TRIGGER_LOW | IRQF_ONESHOT,
				   "WILC_IRQ", dev);
	if (ret < 0)
		netdev_err(dev, "Failed to request IRQ\n");
	else
		netdev_dbg(dev, "IRQ request succeeded IRQ-NUM= %d\n",
			   wl->dev_irq_num);

	return ret;
}

static void deinit_irq(struct net_device *dev)
{
	struct wilc_vif *vif = netdev_priv(dev);
	struct wilc *wilc = vif->wilc;

	/* Deinitialize IRQ */
	if (wilc->dev_irq_num)
		free_irq(wilc->dev_irq_num, wilc);
}

void wilc_mac_indicate(struct wilc *wilc)
{
	s8 status;

	wilc_wlan_cfg_get_val(wilc, WID_STATUS, &status, 1);
	if (wilc->mac_status == WILC_MAC_STATUS_INIT) {
		wilc->mac_status = status;
		complete(&wilc->sync_event);
	} else {
		wilc->mac_status = status;
	}
}

static struct net_device *get_if_handler(struct wilc *wilc, u8 *mac_header)
{
	u8 *bssid, *bssid1;
	int i = 0;

	bssid = mac_header + 10;
	bssid1 = mac_header + 4;

	for (i = 0; i < wilc->vif_num; i++) {
		if (wilc->vif[i]->mode == WILC_STATION_MODE)
			if (ether_addr_equal_unaligned(bssid,
						       wilc->vif[i]->bssid))
				return wilc->vif[i]->ndev;
		if (wilc->vif[i]->mode == WILC_AP_MODE)
			if (ether_addr_equal_unaligned(bssid1,
						       wilc->vif[i]->bssid))
				return wilc->vif[i]->ndev;
	}

	return NULL;
}

void wilc_wlan_set_bssid(struct net_device *wilc_netdev, u8 *bssid, u8 mode)
{
	struct wilc_vif *vif = netdev_priv(wilc_netdev);

	if (bssid)
		ether_addr_copy(vif->bssid, bssid);
	else
		eth_zero_addr(vif->bssid);

	vif->mode = mode;
}

int wilc_wlan_get_num_conn_ifcs(struct wilc *wilc)
{
	u8 i = 0;
	u8 ret_val = 0;

	for (i = 0; i < wilc->vif_num; i++)
		if (!is_zero_ether_addr(wilc->vif[i]->bssid))
			ret_val++;

	return ret_val;
}

static int wilc_txq_task(void *vp)
{
	int ret;
	u32 txq_count;
	struct net_device *dev = vp;
	struct wilc_vif *vif = netdev_priv(dev);
	struct wilc *wl = vif->wilc;

	complete(&wl->txq_thread_started);
	while (1) {
		wait_for_completion(&wl->txq_event);

		if (wl->close) {
			complete(&wl->txq_thread_started);

			while (!kthread_should_stop())
				schedule();
			break;
		}
		do {
			ret = wilc_wlan_handle_txq(dev, &txq_count);
			if (txq_count < FLOW_CONTROL_LOWER_THRESHOLD) {
				if (wl->vif[0]->mac_opened &&
				    netif_queue_stopped(wl->vif[0]->ndev))
					netif_wake_queue(wl->vif[0]->ndev);
				if (wl->vif[1]->mac_opened &&
				    netif_queue_stopped(wl->vif[1]->ndev))
					netif_wake_queue(wl->vif[1]->ndev);
			}
		} while (ret == -ENOBUFS && !wl->close);
	}
	return 0;
}

static int wilc_wlan_get_firmware(struct net_device *dev)
{
	struct wilc_vif *vif = netdev_priv(dev);
	struct wilc *wilc = vif->wilc;
	int chip_id, ret = 0;
	const struct firmware *wilc_firmware;
	char *firmware;

	chip_id = wilc_get_chipid(wilc, false);

	if (chip_id < 0x1003a0)
		firmware = FIRMWARE_1002;
	else
		firmware = FIRMWARE_1003;

	netdev_info(dev, "loading firmware %s\n", firmware);

	if (request_firmware(&wilc_firmware, firmware, wilc->dev) != 0) {
		netdev_err(dev, "%s - firmware not available\n", firmware);
		ret = -1;
		goto fail;
	}
	wilc->firmware = wilc_firmware;

fail:

	return ret;
}

static int wilc_start_firmware(struct net_device *dev)
{
	struct wilc_vif *vif = netdev_priv(dev);
	struct wilc *wilc = vif->wilc;
	int ret = 0;

	ret = wilc_wlan_start(wilc);
	if (ret < 0)
		return ret;

	if (!wait_for_completion_timeout(&wilc->sync_event,
					 msecs_to_jiffies(5000)))
		return -ETIME;

	return 0;
}

static int wilc1000_firmware_download(struct net_device *dev)
{
	struct wilc_vif *vif = netdev_priv(dev);
	struct wilc *wilc = vif->wilc;
	int ret = 0;

	if (!wilc->firmware) {
		netdev_err(dev, "Firmware buffer is NULL\n");
		return -ENOBUFS;
	}

	ret = wilc_wlan_firmware_download(wilc, wilc->firmware->data,
					  wilc->firmware->size);
	if (ret < 0)
		return ret;

	release_firmware(wilc->firmware);
	wilc->firmware = NULL;

	netdev_dbg(dev, "Download Succeeded\n");

	return 0;
}

static int wilc_init_fw_config(struct net_device *dev, struct wilc_vif *vif)
{
	struct wilc_priv *priv;
	struct host_if_drv *hif_drv;
	u8 b;
	u16 hw;
	u32 w;

	netdev_dbg(dev, "Start configuring Firmware\n");
	priv = wiphy_priv(dev->ieee80211_ptr->wiphy);
	hif_drv = (struct host_if_drv *)priv->hif_drv;
	netdev_dbg(dev, "Host = %p\n", hif_drv);

	w = vif->iftype;
	cpu_to_le32s(&w);
	if (!wilc_wlan_cfg_set(vif, 1, WID_SET_OPERATION_MODE, (u8 *)&w, 4,
			       0, 0))
		goto fail;

	b = WILC_FW_BSS_TYPE_INFRA;
	if (!wilc_wlan_cfg_set(vif, 0, WID_BSS_TYPE, &b, 1, 0, 0))
		goto fail;

	b = WILC_FW_TX_RATE_AUTO;
	if (!wilc_wlan_cfg_set(vif, 0, WID_CURRENT_TX_RATE, &b, 1, 0, 0))
		goto fail;

	b = WILC_FW_OPER_MODE_G_MIXED_11B_2;
	if (!wilc_wlan_cfg_set(vif, 0, WID_11G_OPERATING_MODE, &b, 1, 0, 0))
		goto fail;

	b = WILC_FW_PREAMBLE_SHORT;
	if (!wilc_wlan_cfg_set(vif, 0, WID_PREAMBLE, &b, 1, 0, 0))
		goto fail;

	b = WILC_FW_11N_PROT_AUTO;
	if (!wilc_wlan_cfg_set(vif, 0, WID_11N_PROT_MECH, &b, 1, 0, 0))
		goto fail;

	b = WILC_FW_ACTIVE_SCAN;
	if (!wilc_wlan_cfg_set(vif, 0, WID_SCAN_TYPE, &b, 1, 0, 0))
		goto fail;

	b = WILC_FW_SITE_SURVEY_OFF;
	if (!wilc_wlan_cfg_set(vif, 0, WID_SITE_SURVEY, &b, 1, 0, 0))
		goto fail;

	hw = 0xffff;
	cpu_to_le16s(&hw);
	if (!wilc_wlan_cfg_set(vif, 0, WID_RTS_THRESHOLD, (u8 *)&hw, 2, 0, 0))
		goto fail;

	hw = 2346;
	cpu_to_le16s(&hw);
	if (!wilc_wlan_cfg_set(vif, 0, WID_FRAG_THRESHOLD, (u8 *)&hw, 2, 0, 0))
		goto fail;

	b = 0;
	if (!wilc_wlan_cfg_set(vif, 0, WID_BCAST_SSID, &b, 1, 0, 0))
		goto fail;

	b = 1;
	if (!wilc_wlan_cfg_set(vif, 0, WID_QOS_ENABLE, &b, 1, 0, 0))
		goto fail;

	b = WILC_FW_NO_POWERSAVE;
	if (!wilc_wlan_cfg_set(vif, 0, WID_POWER_MANAGEMENT, &b, 1, 0, 0))
		goto fail;

	b = WILC_FW_SEC_NO;
	if (!wilc_wlan_cfg_set(vif, 0, WID_11I_MODE, &b, 1, 0, 0))
		goto fail;

	b = WILC_FW_AUTH_OPEN_SYSTEM;
	if (!wilc_wlan_cfg_set(vif, 0, WID_AUTH_TYPE, &b, 1, 0, 0))
		goto fail;

	b = 3;
	if (!wilc_wlan_cfg_set(vif, 0, WID_LISTEN_INTERVAL, &b, 1, 0, 0))
		goto fail;

	b = 3;
	if (!wilc_wlan_cfg_set(vif, 0, WID_DTIM_PERIOD, &b, 1, 0, 0))
		goto fail;

	b = WILC_FW_ACK_POLICY_NORMAL;
	if (!wilc_wlan_cfg_set(vif, 0, WID_ACK_POLICY, &b, 1, 0, 0))
		goto fail;

	b = 0;
	if (!wilc_wlan_cfg_set(vif, 0, WID_USER_CONTROL_ON_TX_POWER, &b, 1,
			       0, 0))
		goto fail;

	b = 48;
	if (!wilc_wlan_cfg_set(vif, 0, WID_TX_POWER_LEVEL_11A, &b, 1, 0, 0))
		goto fail;

	b = 28;
	if (!wilc_wlan_cfg_set(vif, 0, WID_TX_POWER_LEVEL_11B, &b, 1, 0, 0))
		goto fail;

	hw = 100;
	cpu_to_le16s(&hw);
	if (!wilc_wlan_cfg_set(vif, 0, WID_BEACON_INTERVAL, (u8 *)&hw, 2, 0, 0))
		goto fail;

	b = WILC_FW_REKEY_POLICY_DISABLE;
	if (!wilc_wlan_cfg_set(vif, 0, WID_REKEY_POLICY, &b, 1, 0, 0))
		goto fail;

	w = 84600;
	cpu_to_le32s(&w);
	if (!wilc_wlan_cfg_set(vif, 0, WID_REKEY_PERIOD, (u8 *)&w, 4, 0, 0))
		goto fail;

	w = 500;
	cpu_to_le32s(&w);
	if (!wilc_wlan_cfg_set(vif, 0, WID_REKEY_PACKET_COUNT, (u8 *)&w, 4, 0,
			       0))
		goto fail;

	b = 1;
	if (!wilc_wlan_cfg_set(vif, 0, WID_SHORT_SLOT_ALLOWED, &b, 1, 0,
			       0))
		goto fail;

	b = WILC_FW_ERP_PROT_SELF_CTS;
	if (!wilc_wlan_cfg_set(vif, 0, WID_11N_ERP_PROT_TYPE, &b, 1, 0, 0))
		goto fail;

	b = 1;
	if (!wilc_wlan_cfg_set(vif, 0, WID_11N_ENABLE, &b, 1, 0, 0))
		goto fail;

	b = WILC_FW_11N_OP_MODE_HT_MIXED;
	if (!wilc_wlan_cfg_set(vif, 0, WID_11N_OPERATING_MODE, &b, 1, 0, 0))
		goto fail;

	b = 1;
	if (!wilc_wlan_cfg_set(vif, 0, WID_11N_TXOP_PROT_DISABLE, &b, 1, 0, 0))
		goto fail;

	b = WILC_FW_OBBS_NONHT_DETECT_PROTECT_REPORT;
	if (!wilc_wlan_cfg_set(vif, 0, WID_11N_OBSS_NONHT_DETECTION, &b, 1,
			       0, 0))
		goto fail;

	b = WILC_FW_HT_PROT_RTS_CTS_NONHT;
	if (!wilc_wlan_cfg_set(vif, 0, WID_11N_HT_PROT_TYPE, &b, 1, 0, 0))
		goto fail;

	b = 0;
	if (!wilc_wlan_cfg_set(vif, 0, WID_11N_RIFS_PROT_ENABLE, &b, 1, 0,
			       0))
		goto fail;

	b = 7;
	if (!wilc_wlan_cfg_set(vif, 0, WID_11N_CURRENT_TX_MCS, &b, 1, 0, 0))
		goto fail;

	b = 1;
	if (!wilc_wlan_cfg_set(vif, 0, WID_11N_IMMEDIATE_BA_ENABLED, &b, 1,
			       1, 1))
		goto fail;

	return 0;

fail:
	return -1;
}

static void wlan_deinit_locks(struct net_device *dev)
{
	struct wilc_vif *vif = netdev_priv(dev);
	struct wilc *wilc = vif->wilc;

	mutex_destroy(&wilc->hif_cs);
	mutex_destroy(&wilc->rxq_cs);
	mutex_destroy(&wilc->cfg_cmd_lock);
	mutex_destroy(&wilc->txq_add_to_head_cs);
}

static void wlan_deinitialize_threads(struct net_device *dev)
{
	struct wilc_vif *vif = netdev_priv(dev);
	struct wilc *wl = vif->wilc;

	wl->close = 1;

	complete(&wl->txq_event);

	if (wl->txq_thread) {
		kthread_stop(wl->txq_thread);
		wl->txq_thread = NULL;
	}
}

static void wilc_wlan_deinitialize(struct net_device *dev)
{
	struct wilc_vif *vif = netdev_priv(dev);
	struct wilc *wl = vif->wilc;

	if (!wl) {
		netdev_err(dev, "wl is NULL\n");
		return;
	}

	if (wl->initialized) {
		netdev_info(dev, "Deinitializing wilc1000...\n");

		if (!wl->dev_irq_num &&
		    wl->hif_func->disable_interrupt) {
			mutex_lock(&wl->hif_cs);
			wl->hif_func->disable_interrupt(wl);
			mutex_unlock(&wl->hif_cs);
		}
		complete(&wl->txq_event);

		wlan_deinitialize_threads(dev);
		deinit_irq(dev);

		wilc_wlan_stop(wl);
		wilc_wlan_cleanup(dev);
		wlan_deinit_locks(dev);

		wl->initialized = false;

		netdev_dbg(dev, "wilc1000 deinitialization Done\n");
	} else {
		netdev_dbg(dev, "wilc1000 is not initialized\n");
	}
}

static void wlan_init_locks(struct net_device *dev)
{
	struct wilc_vif *vif = netdev_priv(dev);
	struct wilc *wl = vif->wilc;

	mutex_init(&wl->hif_cs);
	mutex_init(&wl->rxq_cs);
	mutex_init(&wl->cfg_cmd_lock);

	spin_lock_init(&wl->txq_spinlock);
	mutex_init(&wl->txq_add_to_head_cs);

	init_completion(&wl->txq_event);

	init_completion(&wl->cfg_event);
	init_completion(&wl->sync_event);
	init_completion(&wl->txq_thread_started);
}

static int wlan_initialize_threads(struct net_device *dev)
{
	struct wilc_vif *vif = netdev_priv(dev);
	struct wilc *wilc = vif->wilc;

	wilc->txq_thread = kthread_run(wilc_txq_task, (void *)dev,
				       "K_TXQ_TASK");
	if (IS_ERR(wilc->txq_thread)) {
		netdev_err(dev, "couldn't create TXQ thread\n");
		wilc->close = 0;
		return PTR_ERR(wilc->txq_thread);
	}
	wait_for_completion(&wilc->txq_thread_started);

	return 0;
}

static int wilc_wlan_initialize(struct net_device *dev, struct wilc_vif *vif)
{
	int ret = 0;
	struct wilc *wl = vif->wilc;

	if (!wl->initialized) {
		wl->mac_status = WILC_MAC_STATUS_INIT;
		wl->close = 0;

		wlan_init_locks(dev);

		ret = wilc_wlan_init(dev);
		if (ret < 0) {
			ret = -EIO;
			goto fail_locks;
		}

		if (wl->gpio_irq && init_irq(dev)) {
			ret = -EIO;
			goto fail_locks;
		}

		ret = wlan_initialize_threads(dev);
		if (ret < 0) {
			ret = -EIO;
			goto fail_wilc_wlan;
		}

		if (!wl->dev_irq_num &&
		    wl->hif_func->enable_interrupt &&
		    wl->hif_func->enable_interrupt(wl)) {
			ret = -EIO;
			goto fail_irq_init;
		}

		if (wilc_wlan_get_firmware(dev)) {
			ret = -EIO;
			goto fail_irq_enable;
		}

		ret = wilc1000_firmware_download(dev);
		if (ret < 0) {
			ret = -EIO;
			goto fail_irq_enable;
		}

		ret = wilc_start_firmware(dev);
		if (ret < 0) {
			ret = -EIO;
			goto fail_irq_enable;
		}

		if (wilc_wlan_cfg_get(vif, 1, WID_FIRMWARE_VERSION, 1, 0)) {
			int size;
			char firmware_ver[20];

			size = wilc_wlan_cfg_get_val(wl, WID_FIRMWARE_VERSION,
						     firmware_ver,
						     sizeof(firmware_ver));
			firmware_ver[size] = '\0';
			netdev_dbg(dev, "Firmware Ver = %s\n", firmware_ver);
		}
		ret = wilc_init_fw_config(dev, vif);

		if (ret < 0) {
			netdev_err(dev, "Failed to configure firmware\n");
			ret = -EIO;
			goto fail_fw_start;
		}

		wl->initialized = true;
		return 0;

fail_fw_start:
		wilc_wlan_stop(wl);

fail_irq_enable:
		if (!wl->dev_irq_num &&
		    wl->hif_func->disable_interrupt)
			wl->hif_func->disable_interrupt(wl);
fail_irq_init:
		if (wl->dev_irq_num)
			deinit_irq(dev);

		wlan_deinitialize_threads(dev);
fail_wilc_wlan:
		wilc_wlan_cleanup(dev);
fail_locks:
		wlan_deinit_locks(dev);
		netdev_err(dev, "WLAN initialization FAILED\n");
	} else {
		netdev_dbg(dev, "wilc1000 already initialized\n");
	}
	return ret;
}

static int mac_init_fn(struct net_device *ndev)
{
	netif_start_queue(ndev);
	netif_stop_queue(ndev);

	return 0;
}

static int wilc_mac_open(struct net_device *ndev)
{
	struct wilc_vif *vif = netdev_priv(ndev);
	struct wilc *wl = vif->wilc;
	struct wilc_priv *priv = wdev_priv(vif->ndev->ieee80211_ptr);
	unsigned char mac_add[ETH_ALEN] = {0};
	int ret = 0;
	int i = 0;

	if (!wl || !wl->dev) {
		netdev_err(ndev, "device not ready\n");
		return -ENODEV;
	}

	netdev_dbg(ndev, "MAC OPEN[%p]\n", ndev);

	ret = wilc_init_host_int(ndev);
	if (ret < 0)
		return ret;

	ret = wilc_wlan_initialize(ndev, vif);
	if (ret < 0) {
		wilc_deinit_host_int(ndev);
		return ret;
	}

	for (i = 0; i < wl->vif_num; i++) {
		if (ndev == wl->vif[i]->ndev) {
			wilc_set_wfi_drv_handler(vif, wilc_get_vif_idx(vif),
						 vif->iftype, vif->ifc_id);
			wilc_set_operation_mode(vif, vif->iftype);
			break;
		}
	}

	wilc_get_mac_address(vif, mac_add);
	netdev_dbg(ndev, "Mac address: %pM\n", mac_add);
	memcpy(wl->vif[i]->src_addr, mac_add, ETH_ALEN);
	memcpy(ndev->dev_addr, wl->vif[i]->src_addr, ETH_ALEN);

	if (!is_valid_ether_addr(ndev->dev_addr)) {
		netdev_err(ndev, "Wrong MAC address\n");
		wilc_deinit_host_int(ndev);
		wilc_wlan_deinitialize(ndev);
		return -EINVAL;
	}

	wilc_mgmt_frame_register(vif->ndev->ieee80211_ptr->wiphy,
				 vif->ndev->ieee80211_ptr,
				 vif->frame_reg[0].type,
				 vif->frame_reg[0].reg);
	wilc_mgmt_frame_register(vif->ndev->ieee80211_ptr->wiphy,
				 vif->ndev->ieee80211_ptr,
				 vif->frame_reg[1].type,
				 vif->frame_reg[1].reg);
	netif_wake_queue(ndev);
	wl->open_ifcs++;
	priv->p2p.local_random = 0x01;
	vif->mac_opened = 1;
	return 0;
}

static struct net_device_stats *mac_stats(struct net_device *dev)
{
	struct wilc_vif *vif = netdev_priv(dev);

	return &vif->netstats;
}

static void wilc_set_multicast_list(struct net_device *dev)
{
	struct netdev_hw_addr *ha;
	struct wilc_vif *vif = netdev_priv(dev);
	int i;
	u8 *mc_list;
	u8 *cur_mc;

	if (dev->flags & IFF_PROMISC)
		return;

	if (dev->flags & IFF_ALLMULTI ||
	    dev->mc.count > WILC_MULTICAST_TABLE_SIZE) {
		wilc_setup_multicast_filter(vif, 0, 0, NULL);
		return;
	}

	if (dev->mc.count == 0) {
		wilc_setup_multicast_filter(vif, 1, 0, NULL);
		return;
	}

	mc_list = kmalloc_array(dev->mc.count, ETH_ALEN, GFP_ATOMIC);
	if (!mc_list)
		return;

	cur_mc = mc_list;
	i = 0;
	netdev_for_each_mc_addr(ha, dev) {
		memcpy(cur_mc, ha->addr, ETH_ALEN);
		netdev_dbg(dev, "Entry[%d]: %pM\n", i, cur_mc);
		i++;
		cur_mc += ETH_ALEN;
	}

	if (wilc_setup_multicast_filter(vif, 1, dev->mc.count, mc_list))
		kfree(mc_list);
}

static void wilc_tx_complete(void *priv, int status)
{
	struct tx_complete_data *pv_data = priv;

	dev_kfree_skb(pv_data->skb);
	kfree(pv_data);
}

netdev_tx_t wilc_mac_xmit(struct sk_buff *skb, struct net_device *ndev)
{
	struct wilc_vif *vif = netdev_priv(ndev);
	struct wilc *wilc = vif->wilc;
	struct tx_complete_data *tx_data = NULL;
	int queue_count;

	if (skb->dev != ndev) {
		netdev_err(ndev, "Packet not destined to this device\n");
		return 0;
	}

	tx_data = kmalloc(sizeof(*tx_data), GFP_ATOMIC);
	if (!tx_data) {
		dev_kfree_skb(skb);
		netif_wake_queue(ndev);
		return 0;
	}

	tx_data->buff = skb->data;
	tx_data->size = skb->len;
	tx_data->skb  = skb;

	vif->netstats.tx_packets++;
	vif->netstats.tx_bytes += tx_data->size;
	tx_data->bssid = wilc->vif[vif->idx]->bssid;
	queue_count = wilc_wlan_txq_add_net_pkt(ndev, (void *)tx_data,
						tx_data->buff, tx_data->size,
						wilc_tx_complete);

	if (queue_count > FLOW_CONTROL_UPPER_THRESHOLD) {
		if (wilc->vif[0]->mac_opened)
			netif_stop_queue(wilc->vif[0]->ndev);
		if (wilc->vif[1]->mac_opened)
			netif_stop_queue(wilc->vif[1]->ndev);
	}

	return 0;
}

static int wilc_mac_close(struct net_device *ndev)
{
	struct wilc_vif *vif = netdev_priv(ndev);
	struct wilc *wl = vif->wilc;

	netdev_dbg(ndev, "Mac close\n");

	if (wl->open_ifcs > 0)
		wl->open_ifcs--;
	else
		return 0;

	if (vif->ndev) {
		netif_stop_queue(vif->ndev);

		wilc_deinit_host_int(vif->ndev);
	}

	if (wl->open_ifcs == 0) {
		netdev_dbg(ndev, "Deinitializing wilc1000\n");
		wl->close = 1;
		wilc_wlan_deinitialize(ndev);
	}

	vif->mac_opened = 0;

	return 0;
}

void wilc_frmw_to_host(struct wilc *wilc, u8 *buff, u32 size,
		       u32 pkt_offset)
{
	unsigned int frame_len = 0;
	int stats;
	unsigned char *buff_to_send = NULL;
	struct sk_buff *skb;
	struct net_device *wilc_netdev;
	struct wilc_vif *vif;

	if (!wilc)
		return;

	wilc_netdev = get_if_handler(wilc, buff);
	if (!wilc_netdev)
		return;

	buff += pkt_offset;
	vif = netdev_priv(wilc_netdev);

	if (size > 0) {
		frame_len = size;
		buff_to_send = buff;

		skb = dev_alloc_skb(frame_len);
		if (!skb)
			return;

		skb->dev = wilc_netdev;

		skb_put_data(skb, buff_to_send, frame_len);

		skb->protocol = eth_type_trans(skb, wilc_netdev);
		vif->netstats.rx_packets++;
		vif->netstats.rx_bytes += frame_len;
		skb->ip_summed = CHECKSUM_UNNECESSARY;
		stats = netif_rx(skb);
		netdev_dbg(wilc_netdev, "netif_rx ret value is: %d\n", stats);
	}
}

void wilc_wfi_mgmt_rx(struct wilc *wilc, u8 *buff, u32 size)
{
	int i = 0;
	struct wilc_vif *vif;

	for (i = 0; i < wilc->vif_num; i++) {
		vif = netdev_priv(wilc->vif[i]->ndev);
		if (vif->monitor_flag) {
			wilc_wfi_monitor_rx(wilc->monitor_dev, buff, size);
			return;
		}
	}

	vif = netdev_priv(wilc->vif[1]->ndev);
	if ((buff[0] == vif->frame_reg[0].type && vif->frame_reg[0].reg) ||
	    (buff[0] == vif->frame_reg[1].type && vif->frame_reg[1].reg))
		wilc_wfi_p2p_rx(wilc->vif[1]->ndev, buff, size);
}

static const struct net_device_ops wilc_netdev_ops = {
	.ndo_init = mac_init_fn,
	.ndo_open = wilc_mac_open,
	.ndo_stop = wilc_mac_close,
	.ndo_start_xmit = wilc_mac_xmit,
	.ndo_get_stats = mac_stats,
	.ndo_set_rx_mode  = wilc_set_multicast_list,
};

static int dev_state_ev_handler(struct notifier_block *this,
				unsigned long event, void *ptr)
{
	struct in_ifaddr *dev_iface = ptr;
	struct wilc_priv *priv;
	struct host_if_drv *hif_drv;
	struct net_device *dev;
	struct wilc_vif *vif;

	if (!dev_iface || !dev_iface->ifa_dev || !dev_iface->ifa_dev->dev)
		return NOTIFY_DONE;

	dev  = (struct net_device *)dev_iface->ifa_dev->dev;
	if (dev->netdev_ops != &wilc_netdev_ops)
		return NOTIFY_DONE;

	if (!dev->ieee80211_ptr || !dev->ieee80211_ptr->wiphy)
		return NOTIFY_DONE;

	priv = wiphy_priv(dev->ieee80211_ptr->wiphy);
	if (!priv)
		return NOTIFY_DONE;

	hif_drv = (struct host_if_drv *)priv->hif_drv;
	vif = netdev_priv(dev);
	if (!vif || !hif_drv)
		return NOTIFY_DONE;

	switch (event) {
	case NETDEV_UP:
		if (vif->iftype == WILC_STATION_MODE ||
		    vif->iftype == WILC_CLIENT_MODE) {
			hif_drv->ifc_up = 1;
			vif->obtaining_ip = false;
			del_timer(&vif->during_ip_timer);
		}

		if (vif->wilc->enable_ps)
			wilc_set_power_mgmt(vif, 1, 0);

		break;

	case NETDEV_DOWN:
		if (vif->iftype == WILC_STATION_MODE ||
		    vif->iftype == WILC_CLIENT_MODE) {
			hif_drv->ifc_up = 0;
			vif->obtaining_ip = false;
			wilc_set_power_mgmt(vif, 0, 0);
		}

		wilc_resolve_disconnect_aberration(vif);

		break;

	default:
		break;
	}

	return NOTIFY_DONE;
}

static struct notifier_block g_dev_notifier = {
	.notifier_call = dev_state_ev_handler
};

void wilc_netdev_cleanup(struct wilc *wilc)
{
	int i;

	if (!wilc)
		return;

	if (wilc->vif[0]->ndev || wilc->vif[1]->ndev)
		unregister_inetaddr_notifier(&g_dev_notifier);

	if (wilc->firmware) {
		release_firmware(wilc->firmware);
		wilc->firmware = NULL;
	}

	for (i = 0; i < WILC_NUM_CONCURRENT_IFC; i++) {
		if (wilc->vif[i] && wilc->vif[i]->ndev) {
			unregister_netdev(wilc->vif[i]->ndev);
			wilc_free_wiphy(wilc->vif[i]->ndev);
			free_netdev(wilc->vif[i]->ndev);
		}
	}

	wilc_wfi_deinit_mon_interface(wilc);
	flush_workqueue(wilc->hif_workqueue);
	destroy_workqueue(wilc->hif_workqueue);
	wilc_wlan_cfg_deinit(wilc);
	kfree(wilc->bus_data);
	kfree(wilc);
}
EXPORT_SYMBOL_GPL(wilc_netdev_cleanup);

int wilc_netdev_init(struct wilc **wilc, struct device *dev, int io_type,
		     const struct wilc_hif_func *ops)
{
	int i, ret;
	struct wilc_vif *vif;
	struct net_device *ndev;
	struct wilc *wl;

	wl = kzalloc(sizeof(*wl), GFP_KERNEL);
	if (!wl)
		return -ENOMEM;

	ret = wilc_wlan_cfg_init(wl);
	if (ret)
		goto free_wl;

	*wilc = wl;
	wl->io_type = io_type;
	wl->hif_func = ops;
	wl->enable_ps = true;
	wl->chip_ps_state = WILC_CHIP_WAKEDUP;
	INIT_LIST_HEAD(&wl->txq_head.list);
	INIT_LIST_HEAD(&wl->rxq_head.list);

	wl->hif_workqueue = create_singlethread_workqueue("WILC_wq");
	if (!wl->hif_workqueue) {
		ret = -ENOMEM;
		goto free_cfg;
	}

	register_inetaddr_notifier(&g_dev_notifier);

	for (i = 0; i < WILC_NUM_CONCURRENT_IFC; i++) {
		struct wireless_dev *wdev;

		ndev = alloc_etherdev(sizeof(struct wilc_vif));
		if (!ndev) {
			ret = -ENOMEM;
			goto free_ndev;
		}

		vif = netdev_priv(ndev);
		memset(vif, 0, sizeof(struct wilc_vif));

		if (i == 0) {
			strcpy(ndev->name, "wlan%d");
			vif->ifc_id = 1;
		} else {
			strcpy(ndev->name, "p2p%d");
			vif->ifc_id = 0;
		}
		vif->wilc = *wilc;
		vif->ndev = ndev;
		wl->vif[i] = vif;
		wl->vif_num = i + 1;
		vif->idx = i;

		ndev->netdev_ops = &wilc_netdev_ops;

		wdev = wilc_create_wiphy(ndev, dev);
		if (!wdev) {
			netdev_err(ndev, "Can't register WILC Wiphy\n");
			ret = -ENOMEM;
			goto free_ndev;
		}

		SET_NETDEV_DEV(ndev, dev);

		vif->ndev->ieee80211_ptr = wdev;
		vif->ndev->ml_priv = vif;
		wdev->netdev = vif->ndev;
		vif->netstats.rx_packets = 0;
		vif->netstats.tx_packets = 0;
		vif->netstats.rx_bytes = 0;
		vif->netstats.tx_bytes = 0;

		ret = register_netdev(ndev);
		if (ret)
			goto free_ndev;

		vif->iftype = WILC_STATION_MODE;
		vif->mac_opened = 0;
	}

	return 0;

free_ndev:
	for (; i >= 0; i--) {
		if (wl->vif[i]) {
			if (wl->vif[i]->iftype == WILC_STATION_MODE)
				unregister_netdev(wl->vif[i]->ndev);

			if (wl->vif[i]->ndev) {
				wilc_free_wiphy(wl->vif[i]->ndev);
				free_netdev(wl->vif[i]->ndev);
			}
		}
	}
	unregister_inetaddr_notifier(&g_dev_notifier);
	destroy_workqueue(wl->hif_workqueue);
free_cfg:
	wilc_wlan_cfg_deinit(wl);
free_wl:
	kfree(wl);
	return ret;
}
EXPORT_SYMBOL_GPL(wilc_netdev_init);

MODULE_LICENSE("GPL");

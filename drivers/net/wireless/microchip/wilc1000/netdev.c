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

#include "cfg80211.h"
#include "wlan_cfg.h"

#define WILC_MULTICAST_TABLE_SIZE	8
#define WILC_MAX_FW_VERSION_STR_SIZE	50

/* latest API version supported */
#define WILC1000_API_VER		1

#define WILC1000_FW_PREFIX		"atmel/wilc1000_wifi_firmware-"
#define __WILC1000_FW(api)		WILC1000_FW_PREFIX #api ".bin"
#define WILC1000_FW(api)		__WILC1000_FW(api)

static irqreturn_t isr_uh_routine(int irq, void *user_data)
{
	struct wilc *wilc = user_data;

	if (wilc->close) {
		pr_err("Can't handle UH interrupt\n");
		return IRQ_HANDLED;
	}
	return IRQ_WAKE_THREAD;
}

static irqreturn_t isr_bh_routine(int irq, void *userdata)
{
	struct wilc *wilc = userdata;

	if (wilc->close) {
		pr_err("Can't handle BH interrupt\n");
		return IRQ_HANDLED;
	}

	wilc_handle_isr(wilc);

	return IRQ_HANDLED;
}

static int init_irq(struct net_device *dev)
{
	struct wilc_vif *vif = netdev_priv(dev);
	struct wilc *wl = vif->wilc;
	int ret;

	ret = request_threaded_irq(wl->dev_irq_num, isr_uh_routine,
				   isr_bh_routine,
				   IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
				   dev->name, wl);
	if (ret) {
		netdev_err(dev, "Failed to request IRQ [%d]\n", ret);
		return ret;
	}
	netdev_dbg(dev, "IRQ request succeeded IRQ-NUM= %d\n", wl->dev_irq_num);

	return 0;
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
	struct net_device *ndev = NULL;
	struct wilc_vif *vif;
	struct ieee80211_hdr *h = (struct ieee80211_hdr *)mac_header;

	list_for_each_entry_rcu(vif, &wilc->vif_list, list) {
		if (vif->mode == WILC_STATION_MODE)
			if (ether_addr_equal_unaligned(h->addr2, vif->bssid)) {
				ndev = vif->ndev;
				goto out;
			}
		if (vif->mode == WILC_AP_MODE)
			if (ether_addr_equal_unaligned(h->addr1, vif->bssid)) {
				ndev = vif->ndev;
				goto out;
			}
	}
out:
	return ndev;
}

void wilc_wlan_set_bssid(struct net_device *wilc_netdev, const u8 *bssid,
			 u8 mode)
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
	int srcu_idx;
	u8 ret_val = 0;
	struct wilc_vif *vif;

	srcu_idx = srcu_read_lock(&wilc->srcu);
	list_for_each_entry_rcu(vif, &wilc->vif_list, list) {
		if (!is_zero_ether_addr(vif->bssid))
			ret_val++;
	}
	srcu_read_unlock(&wilc->srcu, srcu_idx);
	return ret_val;
}

static int wilc_txq_task(void *vp)
{
	int ret;
	u32 txq_count;
	struct wilc *wl = vp;

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
			ret = wilc_wlan_handle_txq(wl, &txq_count);
			if (txq_count < FLOW_CONTROL_LOWER_THRESHOLD) {
				int srcu_idx;
				struct wilc_vif *ifc;

				srcu_idx = srcu_read_lock(&wl->srcu);
				list_for_each_entry_rcu(ifc, &wl->vif_list,
							list) {
					if (ifc->mac_opened && ifc->ndev)
						netif_wake_queue(ifc->ndev);
				}
				srcu_read_unlock(&wl->srcu, srcu_idx);
			}
		} while (ret == WILC_VMM_ENTRY_FULL_RETRY && !wl->close);
	}
	return 0;
}

static int wilc_wlan_get_firmware(struct net_device *dev)
{
	struct wilc_vif *vif = netdev_priv(dev);
	struct wilc *wilc = vif->wilc;
	int chip_id;
	const struct firmware *wilc_fw;
	int ret;

	chip_id = wilc_get_chipid(wilc, false);

	netdev_info(dev, "ChipID [%x] loading firmware [%s]\n", chip_id,
		    WILC1000_FW(WILC1000_API_VER));

	ret = request_firmware(&wilc_fw, WILC1000_FW(WILC1000_API_VER),
			       wilc->dev);
	if (ret != 0) {
		netdev_err(dev, "%s - firmware not available\n",
			   WILC1000_FW(WILC1000_API_VER));
		return -EINVAL;
	}
	wilc->firmware = wilc_fw;

	return 0;
}

static int wilc_start_firmware(struct net_device *dev)
{
	struct wilc_vif *vif = netdev_priv(dev);
	struct wilc *wilc = vif->wilc;
	int ret = 0;

	ret = wilc_wlan_start(wilc);
	if (ret)
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
	if (ret)
		return ret;

	release_firmware(wilc->firmware);
	wilc->firmware = NULL;

	netdev_dbg(dev, "Download Succeeded\n");

	return 0;
}

static int wilc_init_fw_config(struct net_device *dev, struct wilc_vif *vif)
{
	struct wilc_priv *priv = &vif->priv;
	struct host_if_drv *hif_drv;
	u8 b;
	u16 hw;
	u32 w;

	netdev_dbg(dev, "Start configuring Firmware\n");
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
	return -EINVAL;
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

		wilc_wlan_stop(wl, vif);
		wilc_wlan_cleanup(dev);

		wl->initialized = false;

		netdev_dbg(dev, "wilc1000 deinitialization Done\n");
	} else {
		netdev_dbg(dev, "wilc1000 is not initialized\n");
	}
}

static int wlan_initialize_threads(struct net_device *dev)
{
	struct wilc_vif *vif = netdev_priv(dev);
	struct wilc *wilc = vif->wilc;

	wilc->txq_thread = kthread_run(wilc_txq_task, (void *)wilc,
				       "%s-tx", dev->name);
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

		ret = wilc_wlan_init(dev);
		if (ret)
			return ret;

		ret = wlan_initialize_threads(dev);
		if (ret)
			goto fail_wilc_wlan;

		if (wl->dev_irq_num && init_irq(dev)) {
			ret = -EIO;
			goto fail_threads;
		}

		if (!wl->dev_irq_num &&
		    wl->hif_func->enable_interrupt &&
		    wl->hif_func->enable_interrupt(wl)) {
			ret = -EIO;
			goto fail_irq_init;
		}

		ret = wilc_wlan_get_firmware(dev);
		if (ret)
			goto fail_irq_enable;

		ret = wilc1000_firmware_download(dev);
		if (ret)
			goto fail_irq_enable;

		ret = wilc_start_firmware(dev);
		if (ret)
			goto fail_irq_enable;

		if (wilc_wlan_cfg_get(vif, 1, WID_FIRMWARE_VERSION, 1, 0)) {
			int size;
			char firmware_ver[WILC_MAX_FW_VERSION_STR_SIZE];

			size = wilc_wlan_cfg_get_val(wl, WID_FIRMWARE_VERSION,
						     firmware_ver,
						     sizeof(firmware_ver));
			firmware_ver[size] = '\0';
			netdev_dbg(dev, "Firmware Ver = %s\n", firmware_ver);
		}

		ret = wilc_init_fw_config(dev, vif);
		if (ret) {
			netdev_err(dev, "Failed to configure firmware\n");
			goto fail_fw_start;
		}
		wl->initialized = true;
		return 0;

fail_fw_start:
		wilc_wlan_stop(wl, vif);

fail_irq_enable:
		if (!wl->dev_irq_num &&
		    wl->hif_func->disable_interrupt)
			wl->hif_func->disable_interrupt(wl);
fail_irq_init:
		if (wl->dev_irq_num)
			deinit_irq(dev);
fail_threads:
		wlan_deinitialize_threads(dev);
fail_wilc_wlan:
		wilc_wlan_cleanup(dev);
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
	int ret = 0;
	struct mgmt_frame_regs mgmt_regs = {};
	u8 addr[ETH_ALEN] __aligned(2);

	if (!wl || !wl->dev) {
		netdev_err(ndev, "device not ready\n");
		return -ENODEV;
	}

	netdev_dbg(ndev, "MAC OPEN[%p]\n", ndev);

	ret = wilc_init_host_int(ndev);
	if (ret)
		return ret;

	ret = wilc_wlan_initialize(ndev, vif);
	if (ret) {
		wilc_deinit_host_int(ndev);
		return ret;
	}

	wilc_set_operation_mode(vif, wilc_get_vif_idx(vif), vif->iftype,
				vif->idx);

	if (is_valid_ether_addr(ndev->dev_addr)) {
		ether_addr_copy(addr, ndev->dev_addr);
		wilc_set_mac_address(vif, addr);
	} else {
		wilc_get_mac_address(vif, addr);
		eth_hw_addr_set(ndev, addr);
	}
	netdev_dbg(ndev, "Mac address: %pM\n", ndev->dev_addr);

	if (!is_valid_ether_addr(ndev->dev_addr)) {
		netdev_err(ndev, "Wrong MAC address\n");
		wilc_deinit_host_int(ndev);
		wilc_wlan_deinitialize(ndev);
		return -EINVAL;
	}

	mgmt_regs.interface_stypes = vif->mgmt_reg_stypes;
	/* so we detect a change */
	vif->mgmt_reg_stypes = 0;
	wilc_update_mgmt_frame_registrations(vif->ndev->ieee80211_ptr->wiphy,
					     vif->ndev->ieee80211_ptr,
					     &mgmt_regs);
	netif_wake_queue(ndev);
	wl->open_ifcs++;
	vif->mac_opened = 1;
	return 0;
}

static struct net_device_stats *mac_stats(struct net_device *dev)
{
	struct wilc_vif *vif = netdev_priv(dev);

	return &vif->netstats;
}

static int wilc_set_mac_addr(struct net_device *dev, void *p)
{
	int result;
	struct wilc_vif *vif = netdev_priv(dev);
	struct wilc *wilc = vif->wilc;
	struct sockaddr *addr = (struct sockaddr *)p;
	unsigned char mac_addr[ETH_ALEN];
	struct wilc_vif *tmp_vif;
	int srcu_idx;

	if (!is_valid_ether_addr(addr->sa_data))
		return -EADDRNOTAVAIL;

	if (!vif->mac_opened) {
		eth_commit_mac_addr_change(dev, p);
		return 0;
	}

	/* Verify MAC Address is not already in use: */

	srcu_idx = srcu_read_lock(&wilc->srcu);
	list_for_each_entry_rcu(tmp_vif, &wilc->vif_list, list) {
		wilc_get_mac_address(tmp_vif, mac_addr);
		if (ether_addr_equal(addr->sa_data, mac_addr)) {
			if (vif != tmp_vif) {
				srcu_read_unlock(&wilc->srcu, srcu_idx);
				return -EADDRNOTAVAIL;
			}
			srcu_read_unlock(&wilc->srcu, srcu_idx);
			return 0;
		}
	}
	srcu_read_unlock(&wilc->srcu, srcu_idx);

	result = wilc_set_mac_address(vif, (u8 *)addr->sa_data);
	if (result)
		return result;

	eth_commit_mac_addr_change(dev, p);
	return result;
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
		return NETDEV_TX_OK;
	}

	tx_data = kmalloc(sizeof(*tx_data), GFP_ATOMIC);
	if (!tx_data) {
		dev_kfree_skb(skb);
		netif_wake_queue(ndev);
		return NETDEV_TX_OK;
	}

	tx_data->buff = skb->data;
	tx_data->size = skb->len;
	tx_data->skb  = skb;

	vif->netstats.tx_packets++;
	vif->netstats.tx_bytes += tx_data->size;
	queue_count = wilc_wlan_txq_add_net_pkt(ndev, tx_data,
						tx_data->buff, tx_data->size,
						wilc_tx_complete);

	if (queue_count > FLOW_CONTROL_UPPER_THRESHOLD) {
		int srcu_idx;
		struct wilc_vif *vif;

		srcu_idx = srcu_read_lock(&wilc->srcu);
		list_for_each_entry_rcu(vif, &wilc->vif_list, list) {
			if (vif->mac_opened)
				netif_stop_queue(vif->ndev);
		}
		srcu_read_unlock(&wilc->srcu, srcu_idx);
	}

	return NETDEV_TX_OK;
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
	int srcu_idx;
	struct wilc_vif *vif;

	srcu_idx = srcu_read_lock(&wilc->srcu);
	list_for_each_entry_rcu(vif, &wilc->vif_list, list) {
		u16 type = le16_to_cpup((__le16 *)buff);
		u32 type_bit = BIT(type >> 4);

		if (vif->priv.p2p_listen_state &&
		    vif->mgmt_reg_stypes & type_bit)
			wilc_wfi_p2p_rx(vif, buff, size);

		if (vif->monitor_flag)
			wilc_wfi_monitor_rx(wilc->monitor_dev, buff, size);
	}
	srcu_read_unlock(&wilc->srcu, srcu_idx);
}

static const struct net_device_ops wilc_netdev_ops = {
	.ndo_init = mac_init_fn,
	.ndo_open = wilc_mac_open,
	.ndo_stop = wilc_mac_close,
	.ndo_set_mac_address = wilc_set_mac_addr,
	.ndo_start_xmit = wilc_mac_xmit,
	.ndo_get_stats = mac_stats,
	.ndo_set_rx_mode  = wilc_set_multicast_list,
};

void wilc_netdev_cleanup(struct wilc *wilc)
{
	struct wilc_vif *vif;
	int srcu_idx, ifc_cnt = 0;

	if (!wilc)
		return;

	if (wilc->firmware) {
		release_firmware(wilc->firmware);
		wilc->firmware = NULL;
	}

	srcu_idx = srcu_read_lock(&wilc->srcu);
	list_for_each_entry_rcu(vif, &wilc->vif_list, list) {
		if (vif->ndev)
			unregister_netdev(vif->ndev);
	}
	srcu_read_unlock(&wilc->srcu, srcu_idx);

	wilc_wfi_deinit_mon_interface(wilc, false);
	destroy_workqueue(wilc->hif_workqueue);

	while (ifc_cnt < WILC_NUM_CONCURRENT_IFC) {
		mutex_lock(&wilc->vif_mutex);
		if (wilc->vif_num <= 0) {
			mutex_unlock(&wilc->vif_mutex);
			break;
		}
		vif = wilc_get_wl_to_vif(wilc);
		if (!IS_ERR(vif))
			list_del_rcu(&vif->list);

		wilc->vif_num--;
		mutex_unlock(&wilc->vif_mutex);
		synchronize_srcu(&wilc->srcu);
		ifc_cnt++;
	}

	wilc_wlan_cfg_deinit(wilc);
	wlan_deinit_locks(wilc);
	wiphy_unregister(wilc->wiphy);
	wiphy_free(wilc->wiphy);
}
EXPORT_SYMBOL_GPL(wilc_netdev_cleanup);

static u8 wilc_get_available_idx(struct wilc *wl)
{
	int idx = 0;
	struct wilc_vif *vif;
	int srcu_idx;

	srcu_idx = srcu_read_lock(&wl->srcu);
	list_for_each_entry_rcu(vif, &wl->vif_list, list) {
		if (vif->idx == 0)
			idx = 1;
		else
			idx = 0;
	}
	srcu_read_unlock(&wl->srcu, srcu_idx);
	return idx;
}

struct wilc_vif *wilc_netdev_ifc_init(struct wilc *wl, const char *name,
				      int vif_type, enum nl80211_iftype type,
				      bool rtnl_locked)
{
	struct net_device *ndev;
	struct wilc_vif *vif;
	int ret;

	ndev = alloc_etherdev(sizeof(*vif));
	if (!ndev)
		return ERR_PTR(-ENOMEM);

	vif = netdev_priv(ndev);
	ndev->ieee80211_ptr = &vif->priv.wdev;
	strcpy(ndev->name, name);
	vif->wilc = wl;
	vif->ndev = ndev;
	ndev->ml_priv = vif;

	ndev->netdev_ops = &wilc_netdev_ops;

	SET_NETDEV_DEV(ndev, wiphy_dev(wl->wiphy));

	vif->priv.wdev.wiphy = wl->wiphy;
	vif->priv.wdev.netdev = ndev;
	vif->priv.wdev.iftype = type;
	vif->priv.dev = ndev;

	if (rtnl_locked)
		ret = cfg80211_register_netdevice(ndev);
	else
		ret = register_netdev(ndev);

	if (ret) {
		ret = -EFAULT;
		goto error;
	}

	wl->hif_workqueue = alloc_ordered_workqueue("%s-wq", WQ_MEM_RECLAIM,
						    ndev->name);
	if (!wl->hif_workqueue) {
		ret = -ENOMEM;
		goto error;
	}

	ndev->needs_free_netdev = true;
	vif->iftype = vif_type;
	vif->idx = wilc_get_available_idx(wl);
	vif->mac_opened = 0;
	mutex_lock(&wl->vif_mutex);
	list_add_tail_rcu(&vif->list, &wl->vif_list);
	wl->vif_num += 1;
	mutex_unlock(&wl->vif_mutex);
	synchronize_srcu(&wl->srcu);

	return vif;

  error:
	free_netdev(ndev);
	return ERR_PTR(ret);
}

MODULE_LICENSE("GPL");
MODULE_FIRMWARE(WILC1000_FW(WILC1000_API_VER));

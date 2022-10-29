// SPDX-License-Identifier: GPL-2.0-only
/*
 * This file is part of wl1251
 *
 * Copyright (C) 2008-2009 Nokia Corporation
 */

#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/firmware.h>
#include <linux/delay.h>
#include <linux/irq.h>
#include <linux/crc32.h>
#include <linux/etherdevice.h>
#include <linux/vmalloc.h>
#include <linux/slab.h>
#include <linux/netdevice.h>

#include "wl1251.h"
#include "wl12xx_80211.h"
#include "reg.h"
#include "io.h"
#include "cmd.h"
#include "event.h"
#include "tx.h"
#include "rx.h"
#include "ps.h"
#include "init.h"
#include "debugfs.h"
#include "boot.h"

void wl1251_enable_interrupts(struct wl1251 *wl)
{
	wl->if_ops->enable_irq(wl);
}

void wl1251_disable_interrupts(struct wl1251 *wl)
{
	wl->if_ops->disable_irq(wl);
}

static int wl1251_power_off(struct wl1251 *wl)
{
	return wl->if_ops->power(wl, false);
}

static int wl1251_power_on(struct wl1251 *wl)
{
	return wl->if_ops->power(wl, true);
}

static int wl1251_fetch_firmware(struct wl1251 *wl)
{
	const struct firmware *fw;
	struct device *dev = wiphy_dev(wl->hw->wiphy);
	int ret;

	ret = request_firmware(&fw, WL1251_FW_NAME, dev);

	if (ret < 0) {
		wl1251_error("could not get firmware: %d", ret);
		return ret;
	}

	if (fw->size % 4) {
		wl1251_error("firmware size is not multiple of 32 bits: %zu",
			     fw->size);
		ret = -EILSEQ;
		goto out;
	}

	wl->fw_len = fw->size;
	wl->fw = vmalloc(wl->fw_len);

	if (!wl->fw) {
		wl1251_error("could not allocate memory for the firmware");
		ret = -ENOMEM;
		goto out;
	}

	memcpy(wl->fw, fw->data, wl->fw_len);

	ret = 0;

out:
	release_firmware(fw);

	return ret;
}

static int wl1251_fetch_nvs(struct wl1251 *wl)
{
	const struct firmware *fw;
	struct device *dev = wiphy_dev(wl->hw->wiphy);
	int ret;

	ret = request_firmware(&fw, WL1251_NVS_NAME, dev);

	if (ret < 0) {
		wl1251_error("could not get nvs file: %d", ret);
		return ret;
	}

	if (fw->size % 4) {
		wl1251_error("nvs size is not multiple of 32 bits: %zu",
			     fw->size);
		ret = -EILSEQ;
		goto out;
	}

	wl->nvs = kmemdup(fw->data, fw->size, GFP_KERNEL);

	if (!wl->nvs) {
		wl1251_error("could not allocate memory for the nvs file");
		ret = -ENOMEM;
		goto out;
	}

	wl->nvs_len = fw->size;

	ret = 0;

out:
	release_firmware(fw);

	return ret;
}

static void wl1251_fw_wakeup(struct wl1251 *wl)
{
	u32 elp_reg;

	elp_reg = ELPCTRL_WAKE_UP;
	wl1251_write_elp(wl, HW_ACCESS_ELP_CTRL_REG_ADDR, elp_reg);
	elp_reg = wl1251_read_elp(wl, HW_ACCESS_ELP_CTRL_REG_ADDR);

	if (!(elp_reg & ELPCTRL_WLAN_READY))
		wl1251_warning("WLAN not ready");
}

static int wl1251_chip_wakeup(struct wl1251 *wl)
{
	int ret;

	ret = wl1251_power_on(wl);
	if (ret < 0)
		return ret;

	msleep(WL1251_POWER_ON_SLEEP);
	wl->if_ops->reset(wl);

	/* We don't need a real memory partition here, because we only want
	 * to use the registers at this point. */
	wl1251_set_partition(wl,
			     0x00000000,
			     0x00000000,
			     REGISTERS_BASE,
			     REGISTERS_DOWN_SIZE);

	/* ELP module wake up */
	wl1251_fw_wakeup(wl);

	/* whal_FwCtrl_BootSm() */

	/* 0. read chip id from CHIP_ID */
	wl->chip_id = wl1251_reg_read32(wl, CHIP_ID_B);

	/* 1. check if chip id is valid */

	switch (wl->chip_id) {
	case CHIP_ID_1251_PG12:
		wl1251_debug(DEBUG_BOOT, "chip id 0x%x (1251 PG12)",
			     wl->chip_id);
		break;
	case CHIP_ID_1251_PG11:
		wl1251_debug(DEBUG_BOOT, "chip id 0x%x (1251 PG11)",
			     wl->chip_id);
		break;
	case CHIP_ID_1251_PG10:
	default:
		wl1251_error("unsupported chip id: 0x%x", wl->chip_id);
		ret = -ENODEV;
		goto out;
	}

	if (wl->fw == NULL) {
		ret = wl1251_fetch_firmware(wl);
		if (ret < 0)
			goto out;
	}

out:
	return ret;
}

#define WL1251_IRQ_LOOP_COUNT 10
static void wl1251_irq_work(struct work_struct *work)
{
	u32 intr, ctr = WL1251_IRQ_LOOP_COUNT;
	struct wl1251 *wl =
		container_of(work, struct wl1251, irq_work);
	int ret;

	mutex_lock(&wl->mutex);

	wl1251_debug(DEBUG_IRQ, "IRQ work");

	if (wl->state == WL1251_STATE_OFF)
		goto out;

	ret = wl1251_ps_elp_wakeup(wl);
	if (ret < 0)
		goto out;

	wl1251_reg_write32(wl, ACX_REG_INTERRUPT_MASK, WL1251_ACX_INTR_ALL);

	intr = wl1251_reg_read32(wl, ACX_REG_INTERRUPT_CLEAR);
	wl1251_debug(DEBUG_IRQ, "intr: 0x%x", intr);

	do {
		if (wl->data_path) {
			wl->rx_counter = wl1251_mem_read32(
				wl, wl->data_path->rx_control_addr);

			/* We handle a frmware bug here */
			switch ((wl->rx_counter - wl->rx_handled) & 0xf) {
			case 0:
				wl1251_debug(DEBUG_IRQ,
					     "RX: FW and host in sync");
				intr &= ~WL1251_ACX_INTR_RX0_DATA;
				intr &= ~WL1251_ACX_INTR_RX1_DATA;
				break;
			case 1:
				wl1251_debug(DEBUG_IRQ, "RX: FW +1");
				intr |= WL1251_ACX_INTR_RX0_DATA;
				intr &= ~WL1251_ACX_INTR_RX1_DATA;
				break;
			case 2:
				wl1251_debug(DEBUG_IRQ, "RX: FW +2");
				intr |= WL1251_ACX_INTR_RX0_DATA;
				intr |= WL1251_ACX_INTR_RX1_DATA;
				break;
			default:
				wl1251_warning(
					"RX: FW and host out of sync: %d",
					wl->rx_counter - wl->rx_handled);
				break;
			}

			wl->rx_handled = wl->rx_counter;

			wl1251_debug(DEBUG_IRQ, "RX counter: %d",
				     wl->rx_counter);
		}

		intr &= wl->intr_mask;

		if (intr == 0) {
			wl1251_debug(DEBUG_IRQ, "INTR is 0");
			goto out_sleep;
		}

		if (intr & WL1251_ACX_INTR_RX0_DATA) {
			wl1251_debug(DEBUG_IRQ, "WL1251_ACX_INTR_RX0_DATA");
			wl1251_rx(wl);
		}

		if (intr & WL1251_ACX_INTR_RX1_DATA) {
			wl1251_debug(DEBUG_IRQ, "WL1251_ACX_INTR_RX1_DATA");
			wl1251_rx(wl);
		}

		if (intr & WL1251_ACX_INTR_TX_RESULT) {
			wl1251_debug(DEBUG_IRQ, "WL1251_ACX_INTR_TX_RESULT");
			wl1251_tx_complete(wl);
		}

		if (intr & WL1251_ACX_INTR_EVENT_A) {
			wl1251_debug(DEBUG_IRQ, "WL1251_ACX_INTR_EVENT_A");
			wl1251_event_handle(wl, 0);
		}

		if (intr & WL1251_ACX_INTR_EVENT_B) {
			wl1251_debug(DEBUG_IRQ, "WL1251_ACX_INTR_EVENT_B");
			wl1251_event_handle(wl, 1);
		}

		if (intr & WL1251_ACX_INTR_INIT_COMPLETE)
			wl1251_debug(DEBUG_IRQ,
				     "WL1251_ACX_INTR_INIT_COMPLETE");

		if (--ctr == 0)
			break;

		intr = wl1251_reg_read32(wl, ACX_REG_INTERRUPT_CLEAR);
	} while (intr);

out_sleep:
	wl1251_reg_write32(wl, ACX_REG_INTERRUPT_MASK, ~(wl->intr_mask));
	wl1251_ps_elp_sleep(wl);

out:
	mutex_unlock(&wl->mutex);
}

static int wl1251_join(struct wl1251 *wl, u8 bss_type, u8 channel,
		       u16 beacon_interval, u8 dtim_period)
{
	int ret;

	ret = wl1251_acx_frame_rates(wl, DEFAULT_HW_GEN_TX_RATE,
				     DEFAULT_HW_GEN_MODULATION_TYPE,
				     wl->tx_mgmt_frm_rate,
				     wl->tx_mgmt_frm_mod);
	if (ret < 0)
		goto out;

	/*
	 * Join command applies filters, and if we are not associated,
	 * BSSID filter must be disabled for association to work.
	 */
	if (is_zero_ether_addr(wl->bssid))
		wl->rx_config &= ~CFG_BSSID_FILTER_EN;

	ret = wl1251_cmd_join(wl, bss_type, channel, beacon_interval,
			      dtim_period);
	if (ret < 0)
		goto out;

	ret = wl1251_event_wait(wl, JOIN_EVENT_COMPLETE_ID, 100);
	if (ret < 0)
		wl1251_warning("join timeout");

out:
	return ret;
}

static void wl1251_op_tx(struct ieee80211_hw *hw,
			 struct ieee80211_tx_control *control,
			 struct sk_buff *skb)
{
	struct wl1251 *wl = hw->priv;
	unsigned long flags;

	skb_queue_tail(&wl->tx_queue, skb);

	/*
	 * The chip specific setup must run before the first TX packet -
	 * before that, the tx_work will not be initialized!
	 */

	ieee80211_queue_work(wl->hw, &wl->tx_work);

	/*
	 * The workqueue is slow to process the tx_queue and we need stop
	 * the queue here, otherwise the queue will get too long.
	 */
	if (skb_queue_len(&wl->tx_queue) >= WL1251_TX_QUEUE_HIGH_WATERMARK) {
		wl1251_debug(DEBUG_TX, "op_tx: tx_queue full, stop queues");

		spin_lock_irqsave(&wl->wl_lock, flags);
		ieee80211_stop_queues(wl->hw);
		wl->tx_queue_stopped = true;
		spin_unlock_irqrestore(&wl->wl_lock, flags);
	}
}

static int wl1251_op_start(struct ieee80211_hw *hw)
{
	struct wl1251 *wl = hw->priv;
	struct wiphy *wiphy = hw->wiphy;
	int ret = 0;

	wl1251_debug(DEBUG_MAC80211, "mac80211 start");

	mutex_lock(&wl->mutex);

	if (wl->state != WL1251_STATE_OFF) {
		wl1251_error("cannot start because not in off state: %d",
			     wl->state);
		ret = -EBUSY;
		goto out;
	}

	ret = wl1251_chip_wakeup(wl);
	if (ret < 0)
		goto out;

	ret = wl1251_boot(wl);
	if (ret < 0)
		goto out;

	ret = wl1251_hw_init(wl);
	if (ret < 0)
		goto out;

	ret = wl1251_acx_station_id(wl);
	if (ret < 0)
		goto out;

	wl->state = WL1251_STATE_ON;

	wl1251_info("firmware booted (%s)", wl->fw_ver);

	/* update hw/fw version info in wiphy struct */
	wiphy->hw_version = wl->chip_id;
	strncpy(wiphy->fw_version, wl->fw_ver, sizeof(wiphy->fw_version));

out:
	if (ret < 0)
		wl1251_power_off(wl);

	mutex_unlock(&wl->mutex);

	return ret;
}

static void wl1251_op_stop(struct ieee80211_hw *hw)
{
	struct wl1251 *wl = hw->priv;

	wl1251_info("down");

	wl1251_debug(DEBUG_MAC80211, "mac80211 stop");

	mutex_lock(&wl->mutex);

	WARN_ON(wl->state != WL1251_STATE_ON);

	if (wl->scanning) {
		struct cfg80211_scan_info info = {
			.aborted = true,
		};

		ieee80211_scan_completed(wl->hw, &info);
		wl->scanning = false;
	}

	wl->state = WL1251_STATE_OFF;

	wl1251_disable_interrupts(wl);

	mutex_unlock(&wl->mutex);

	cancel_work_sync(&wl->irq_work);
	cancel_work_sync(&wl->tx_work);
	cancel_delayed_work_sync(&wl->elp_work);

	mutex_lock(&wl->mutex);

	/* let's notify MAC80211 about the remaining pending TX frames */
	wl1251_tx_flush(wl);
	wl1251_power_off(wl);

	eth_zero_addr(wl->bssid);
	wl->listen_int = 1;
	wl->bss_type = MAX_BSS_TYPE;

	wl->data_in_count = 0;
	wl->rx_counter = 0;
	wl->rx_handled = 0;
	wl->rx_current_buffer = 0;
	wl->rx_last_id = 0;
	wl->next_tx_complete = 0;
	wl->elp = false;
	wl->station_mode = STATION_ACTIVE_MODE;
	wl->psm_entry_retry = 0;
	wl->tx_queue_stopped = false;
	wl->power_level = WL1251_DEFAULT_POWER_LEVEL;
	wl->rssi_thold = 0;
	wl->channel = WL1251_DEFAULT_CHANNEL;
	wl->monitor_present = false;
	wl->joined = false;

	wl1251_debugfs_reset(wl);

	mutex_unlock(&wl->mutex);
}

static int wl1251_op_add_interface(struct ieee80211_hw *hw,
				   struct ieee80211_vif *vif)
{
	struct wl1251 *wl = hw->priv;
	int ret = 0;

	vif->driver_flags |= IEEE80211_VIF_BEACON_FILTER |
			     IEEE80211_VIF_SUPPORTS_UAPSD |
			     IEEE80211_VIF_SUPPORTS_CQM_RSSI;

	wl1251_debug(DEBUG_MAC80211, "mac80211 add interface type %d mac %pM",
		     vif->type, vif->addr);

	mutex_lock(&wl->mutex);
	if (wl->vif) {
		ret = -EBUSY;
		goto out;
	}

	wl->vif = vif;

	switch (vif->type) {
	case NL80211_IFTYPE_STATION:
		wl->bss_type = BSS_TYPE_STA_BSS;
		break;
	case NL80211_IFTYPE_ADHOC:
		wl->bss_type = BSS_TYPE_IBSS;
		break;
	default:
		ret = -EOPNOTSUPP;
		goto out;
	}

	if (!ether_addr_equal_unaligned(wl->mac_addr, vif->addr)) {
		memcpy(wl->mac_addr, vif->addr, ETH_ALEN);
		SET_IEEE80211_PERM_ADDR(wl->hw, wl->mac_addr);
		ret = wl1251_acx_station_id(wl);
		if (ret < 0)
			goto out;
	}

out:
	mutex_unlock(&wl->mutex);
	return ret;
}

static void wl1251_op_remove_interface(struct ieee80211_hw *hw,
					 struct ieee80211_vif *vif)
{
	struct wl1251 *wl = hw->priv;

	mutex_lock(&wl->mutex);
	wl1251_debug(DEBUG_MAC80211, "mac80211 remove interface");
	wl->vif = NULL;
	eth_zero_addr(wl->bssid);
	mutex_unlock(&wl->mutex);
}

static int wl1251_build_null_data(struct wl1251 *wl)
{
	struct sk_buff *skb = NULL;
	int size;
	void *ptr;
	int ret = -ENOMEM;

	if (wl->bss_type == BSS_TYPE_IBSS) {
		size = sizeof(struct wl12xx_null_data_template);
		ptr = NULL;
	} else {
		skb = ieee80211_nullfunc_get(wl->hw, wl->vif, -1, false);
		if (!skb)
			goto out;
		size = skb->len;
		ptr = skb->data;
	}

	ret = wl1251_cmd_template_set(wl, CMD_NULL_DATA, ptr, size);

out:
	dev_kfree_skb(skb);
	if (ret)
		wl1251_warning("cmd build null data failed: %d", ret);

	return ret;
}

static int wl1251_build_qos_null_data(struct wl1251 *wl)
{
	struct ieee80211_qos_hdr template;

	memset(&template, 0, sizeof(template));

	memcpy(template.addr1, wl->bssid, ETH_ALEN);
	memcpy(template.addr2, wl->mac_addr, ETH_ALEN);
	memcpy(template.addr3, wl->bssid, ETH_ALEN);

	template.frame_control = cpu_to_le16(IEEE80211_FTYPE_DATA |
					     IEEE80211_STYPE_QOS_NULLFUNC |
					     IEEE80211_FCTL_TODS);

	/* FIXME: not sure what priority to use here */
	template.qos_ctrl = cpu_to_le16(0);

	return wl1251_cmd_template_set(wl, CMD_QOS_NULL_DATA, &template,
				       sizeof(template));
}

static bool wl1251_can_do_pm(struct ieee80211_conf *conf, struct wl1251 *wl)
{
	return (conf->flags & IEEE80211_CONF_PS) && !wl->monitor_present;
}

static int wl1251_op_config(struct ieee80211_hw *hw, u32 changed)
{
	struct wl1251 *wl = hw->priv;
	struct ieee80211_conf *conf = &hw->conf;
	int channel, ret = 0;

	channel = ieee80211_frequency_to_channel(
			conf->chandef.chan->center_freq);

	wl1251_debug(DEBUG_MAC80211,
		     "mac80211 config ch %d monitor %s psm %s power %d",
		     channel,
		     conf->flags & IEEE80211_CONF_MONITOR ? "on" : "off",
		     conf->flags & IEEE80211_CONF_PS ? "on" : "off",
		     conf->power_level);

	mutex_lock(&wl->mutex);

	ret = wl1251_ps_elp_wakeup(wl);
	if (ret < 0)
		goto out;

	if (changed & IEEE80211_CONF_CHANGE_MONITOR) {
		u32 mode;

		if (conf->flags & IEEE80211_CONF_MONITOR) {
			wl->monitor_present = true;
			mode = DF_SNIFF_MODE_ENABLE | DF_ENCRYPTION_DISABLE;
		} else {
			wl->monitor_present = false;
			mode = 0;
		}

		ret = wl1251_acx_feature_cfg(wl, mode);
		if (ret < 0)
			goto out_sleep;
	}

	if (channel != wl->channel) {
		wl->channel = channel;

		/*
		 * Use ENABLE_RX command for channel switching when no
		 * interface is present (monitor mode only).
		 * This leaves the tx path disabled in firmware, whereas
		 * the usual JOIN command seems to transmit some frames
		 * at firmware level.
		 */
		if (wl->vif == NULL) {
			wl->joined = false;
			ret = wl1251_cmd_data_path_rx(wl, wl->channel, 1);
		} else {
			ret = wl1251_join(wl, wl->bss_type, wl->channel,
					  wl->beacon_int, wl->dtim_period);
		}
		if (ret < 0)
			goto out_sleep;
	}

	if (wl1251_can_do_pm(conf, wl) && !wl->psm_requested) {
		wl1251_debug(DEBUG_PSM, "psm enabled");

		wl->psm_requested = true;

		wl->dtim_period = conf->ps_dtim_period;

		ret = wl1251_acx_wr_tbtt_and_dtim(wl, wl->beacon_int,
						  wl->dtim_period);

		/*
		 * mac80211 enables PSM only if we're already associated.
		 */
		ret = wl1251_ps_set_mode(wl, STATION_POWER_SAVE_MODE);
		if (ret < 0)
			goto out_sleep;
	} else if (!wl1251_can_do_pm(conf, wl) && wl->psm_requested) {
		wl1251_debug(DEBUG_PSM, "psm disabled");

		wl->psm_requested = false;

		if (wl->station_mode != STATION_ACTIVE_MODE) {
			ret = wl1251_ps_set_mode(wl, STATION_ACTIVE_MODE);
			if (ret < 0)
				goto out_sleep;
		}
	}

	if (changed & IEEE80211_CONF_CHANGE_IDLE && !wl->scanning) {
		if (conf->flags & IEEE80211_CONF_IDLE) {
			ret = wl1251_ps_set_mode(wl, STATION_IDLE);
			if (ret < 0)
				goto out_sleep;
		} else {
			ret = wl1251_ps_set_mode(wl, STATION_ACTIVE_MODE);
			if (ret < 0)
				goto out_sleep;
			ret = wl1251_join(wl, wl->bss_type, wl->channel,
					  wl->beacon_int, wl->dtim_period);
			if (ret < 0)
				goto out_sleep;
		}
	}

	if (conf->power_level != wl->power_level) {
		ret = wl1251_acx_tx_power(wl, conf->power_level);
		if (ret < 0)
			goto out_sleep;

		wl->power_level = conf->power_level;
	}

out_sleep:
	wl1251_ps_elp_sleep(wl);

out:
	mutex_unlock(&wl->mutex);

	return ret;
}

struct wl1251_filter_params {
	bool enabled;
	int mc_list_length;
	u8 mc_list[ACX_MC_ADDRESS_GROUP_MAX][ETH_ALEN];
};

static u64 wl1251_op_prepare_multicast(struct ieee80211_hw *hw,
				       struct netdev_hw_addr_list *mc_list)
{
	struct wl1251_filter_params *fp;
	struct netdev_hw_addr *ha;
	struct wl1251 *wl = hw->priv;

	if (unlikely(wl->state == WL1251_STATE_OFF))
		return 0;

	fp = kzalloc(sizeof(*fp), GFP_ATOMIC);
	if (!fp) {
		wl1251_error("Out of memory setting filters.");
		return 0;
	}

	/* update multicast filtering parameters */
	fp->mc_list_length = 0;
	if (netdev_hw_addr_list_count(mc_list) > ACX_MC_ADDRESS_GROUP_MAX) {
		fp->enabled = false;
	} else {
		fp->enabled = true;
		netdev_hw_addr_list_for_each(ha, mc_list) {
			memcpy(fp->mc_list[fp->mc_list_length],
					ha->addr, ETH_ALEN);
			fp->mc_list_length++;
		}
	}

	return (u64)(unsigned long)fp;
}

#define WL1251_SUPPORTED_FILTERS (FIF_ALLMULTI | \
				  FIF_FCSFAIL | \
				  FIF_BCN_PRBRESP_PROMISC | \
				  FIF_CONTROL | \
				  FIF_OTHER_BSS | \
				  FIF_PROBE_REQ)

static void wl1251_op_configure_filter(struct ieee80211_hw *hw,
				       unsigned int changed,
				       unsigned int *total, u64 multicast)
{
	struct wl1251_filter_params *fp = (void *)(unsigned long)multicast;
	struct wl1251 *wl = hw->priv;
	int ret;

	wl1251_debug(DEBUG_MAC80211, "mac80211 configure filter");

	*total &= WL1251_SUPPORTED_FILTERS;
	changed &= WL1251_SUPPORTED_FILTERS;

	if (changed == 0) {
		/* no filters which we support changed */
		kfree(fp);
		return;
	}

	mutex_lock(&wl->mutex);

	wl->rx_config = WL1251_DEFAULT_RX_CONFIG;
	wl->rx_filter = WL1251_DEFAULT_RX_FILTER;

	if (*total & FIF_ALLMULTI)
		/*
		 * CFG_MC_FILTER_EN in rx_config needs to be 0 to receive
		 * all multicast frames
		 */
		wl->rx_config &= ~CFG_MC_FILTER_EN;
	if (*total & FIF_FCSFAIL)
		wl->rx_filter |= CFG_RX_FCS_ERROR;
	if (*total & FIF_BCN_PRBRESP_PROMISC) {
		wl->rx_config &= ~CFG_BSSID_FILTER_EN;
		wl->rx_config &= ~CFG_SSID_FILTER_EN;
	}
	if (*total & FIF_CONTROL)
		wl->rx_filter |= CFG_RX_CTL_EN;
	if (*total & FIF_OTHER_BSS || is_zero_ether_addr(wl->bssid))
		wl->rx_config &= ~CFG_BSSID_FILTER_EN;
	if (*total & FIF_PROBE_REQ)
		wl->rx_filter |= CFG_RX_PREQ_EN;

	if (wl->state == WL1251_STATE_OFF)
		goto out;

	ret = wl1251_ps_elp_wakeup(wl);
	if (ret < 0)
		goto out;

	if (*total & FIF_ALLMULTI)
		ret = wl1251_acx_group_address_tbl(wl, false, NULL, 0);
	else if (fp)
		ret = wl1251_acx_group_address_tbl(wl, fp->enabled,
						   fp->mc_list,
						   fp->mc_list_length);
	if (ret < 0)
		goto out;

	/* send filters to firmware */
	wl1251_acx_rx_config(wl, wl->rx_config, wl->rx_filter);

	wl1251_ps_elp_sleep(wl);

out:
	mutex_unlock(&wl->mutex);
	kfree(fp);
}

/* HW encryption */
static int wl1251_set_key_type(struct wl1251 *wl,
			       struct wl1251_cmd_set_keys *key,
			       enum set_key_cmd cmd,
			       struct ieee80211_key_conf *mac80211_key,
			       const u8 *addr)
{
	switch (mac80211_key->cipher) {
	case WLAN_CIPHER_SUITE_WEP40:
	case WLAN_CIPHER_SUITE_WEP104:
		if (is_broadcast_ether_addr(addr))
			key->key_type = KEY_WEP_DEFAULT;
		else
			key->key_type = KEY_WEP_ADDR;

		mac80211_key->hw_key_idx = mac80211_key->keyidx;
		break;
	case WLAN_CIPHER_SUITE_TKIP:
		if (is_broadcast_ether_addr(addr))
			key->key_type = KEY_TKIP_MIC_GROUP;
		else
			key->key_type = KEY_TKIP_MIC_PAIRWISE;

		mac80211_key->hw_key_idx = mac80211_key->keyidx;
		break;
	case WLAN_CIPHER_SUITE_CCMP:
		if (is_broadcast_ether_addr(addr))
			key->key_type = KEY_AES_GROUP;
		else
			key->key_type = KEY_AES_PAIRWISE;
		mac80211_key->flags |= IEEE80211_KEY_FLAG_GENERATE_IV;
		break;
	default:
		wl1251_error("Unknown key cipher 0x%x", mac80211_key->cipher);
		return -EOPNOTSUPP;
	}

	return 0;
}

static int wl1251_op_set_key(struct ieee80211_hw *hw, enum set_key_cmd cmd,
			     struct ieee80211_vif *vif,
			     struct ieee80211_sta *sta,
			     struct ieee80211_key_conf *key)
{
	struct wl1251 *wl = hw->priv;
	struct wl1251_cmd_set_keys *wl_cmd;
	const u8 *addr;
	int ret;

	static const u8 bcast_addr[ETH_ALEN] =
		{ 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

	wl1251_debug(DEBUG_MAC80211, "mac80211 set key");

	wl_cmd = kzalloc(sizeof(*wl_cmd), GFP_KERNEL);
	if (!wl_cmd) {
		ret = -ENOMEM;
		goto out;
	}

	addr = sta ? sta->addr : bcast_addr;

	wl1251_debug(DEBUG_CRYPT, "CMD: 0x%x", cmd);
	wl1251_dump(DEBUG_CRYPT, "ADDR: ", addr, ETH_ALEN);
	wl1251_debug(DEBUG_CRYPT, "Key: algo:0x%x, id:%d, len:%d flags 0x%x",
		     key->cipher, key->keyidx, key->keylen, key->flags);
	wl1251_dump(DEBUG_CRYPT, "KEY: ", key->key, key->keylen);

	if (is_zero_ether_addr(addr)) {
		/* We dont support TX only encryption */
		ret = -EOPNOTSUPP;
		goto out;
	}

	mutex_lock(&wl->mutex);

	switch (cmd) {
	case SET_KEY:
		if (wl->monitor_present) {
			ret = -EOPNOTSUPP;
			goto out_unlock;
		}
		wl_cmd->key_action = KEY_ADD_OR_REPLACE;
		break;
	case DISABLE_KEY:
		wl_cmd->key_action = KEY_REMOVE;
		break;
	default:
		wl1251_error("Unsupported key cmd 0x%x", cmd);
		break;
	}

	ret = wl1251_ps_elp_wakeup(wl);
	if (ret < 0)
		goto out_unlock;

	ret = wl1251_set_key_type(wl, wl_cmd, cmd, key, addr);
	if (ret < 0) {
		wl1251_error("Set KEY type failed");
		goto out_sleep;
	}

	if (wl_cmd->key_type != KEY_WEP_DEFAULT)
		memcpy(wl_cmd->addr, addr, ETH_ALEN);

	if ((wl_cmd->key_type == KEY_TKIP_MIC_GROUP) ||
	    (wl_cmd->key_type == KEY_TKIP_MIC_PAIRWISE)) {
		/*
		 * We get the key in the following form:
		 * TKIP (16 bytes) - TX MIC (8 bytes) - RX MIC (8 bytes)
		 * but the target is expecting:
		 * TKIP - RX MIC - TX MIC
		 */
		memcpy(wl_cmd->key, key->key, 16);
		memcpy(wl_cmd->key + 16, key->key + 24, 8);
		memcpy(wl_cmd->key + 24, key->key + 16, 8);

	} else {
		memcpy(wl_cmd->key, key->key, key->keylen);
	}
	wl_cmd->key_size = key->keylen;

	wl_cmd->id = key->keyidx;
	wl_cmd->ssid_profile = 0;

	wl1251_dump(DEBUG_CRYPT, "TARGET KEY: ", wl_cmd, sizeof(*wl_cmd));

	ret = wl1251_cmd_send(wl, CMD_SET_KEYS, wl_cmd, sizeof(*wl_cmd));
	if (ret < 0) {
		wl1251_warning("could not set keys");
		goto out_sleep;
	}

out_sleep:
	wl1251_ps_elp_sleep(wl);

out_unlock:
	mutex_unlock(&wl->mutex);

out:
	kfree(wl_cmd);

	return ret;
}

static int wl1251_op_hw_scan(struct ieee80211_hw *hw,
			     struct ieee80211_vif *vif,
			     struct ieee80211_scan_request *hw_req)
{
	struct cfg80211_scan_request *req = &hw_req->req;
	struct wl1251 *wl = hw->priv;
	struct sk_buff *skb;
	size_t ssid_len = 0;
	u8 *ssid = NULL;
	int ret;

	wl1251_debug(DEBUG_MAC80211, "mac80211 hw scan");

	if (req->n_ssids) {
		ssid = req->ssids[0].ssid;
		ssid_len = req->ssids[0].ssid_len;
	}

	mutex_lock(&wl->mutex);

	if (wl->scanning) {
		wl1251_debug(DEBUG_SCAN, "scan already in progress");
		ret = -EINVAL;
		goto out;
	}

	ret = wl1251_ps_elp_wakeup(wl);
	if (ret < 0)
		goto out;

	if (hw->conf.flags & IEEE80211_CONF_IDLE) {
		ret = wl1251_ps_set_mode(wl, STATION_ACTIVE_MODE);
		if (ret < 0)
			goto out_sleep;
		ret = wl1251_join(wl, wl->bss_type, wl->channel,
				  wl->beacon_int, wl->dtim_period);
		if (ret < 0)
			goto out_sleep;
	}

	skb = ieee80211_probereq_get(wl->hw, wl->vif->addr, ssid, ssid_len,
				     req->ie_len);
	if (!skb) {
		ret = -ENOMEM;
		goto out_idle;
	}
	if (req->ie_len)
		skb_put_data(skb, req->ie, req->ie_len);

	ret = wl1251_cmd_template_set(wl, CMD_PROBE_REQ, skb->data,
				      skb->len);
	dev_kfree_skb(skb);
	if (ret < 0)
		goto out_idle;

	ret = wl1251_cmd_trigger_scan_to(wl, 0);
	if (ret < 0)
		goto out_idle;

	wl->scanning = true;

	ret = wl1251_cmd_scan(wl, ssid, ssid_len, req->channels,
			      req->n_channels, WL1251_SCAN_NUM_PROBES);
	if (ret < 0) {
		wl1251_debug(DEBUG_SCAN, "scan failed %d", ret);
		wl->scanning = false;
		goto out_idle;
	}
	goto out_sleep;

out_idle:
	if (hw->conf.flags & IEEE80211_CONF_IDLE)
		ret = wl1251_ps_set_mode(wl, STATION_IDLE);
out_sleep:
	wl1251_ps_elp_sleep(wl);

out:
	mutex_unlock(&wl->mutex);

	return ret;
}

static int wl1251_op_set_rts_threshold(struct ieee80211_hw *hw, u32 value)
{
	struct wl1251 *wl = hw->priv;
	int ret;

	mutex_lock(&wl->mutex);

	ret = wl1251_ps_elp_wakeup(wl);
	if (ret < 0)
		goto out;

	ret = wl1251_acx_rts_threshold(wl, (u16) value);
	if (ret < 0)
		wl1251_warning("wl1251_op_set_rts_threshold failed: %d", ret);

	wl1251_ps_elp_sleep(wl);

out:
	mutex_unlock(&wl->mutex);

	return ret;
}

static void wl1251_op_bss_info_changed(struct ieee80211_hw *hw,
				       struct ieee80211_vif *vif,
				       struct ieee80211_bss_conf *bss_conf,
				       u64 changed)
{
	struct wl1251 *wl = hw->priv;
	struct sk_buff *beacon, *skb;
	bool enable;
	int ret;

	wl1251_debug(DEBUG_MAC80211, "mac80211 bss info changed");

	mutex_lock(&wl->mutex);

	ret = wl1251_ps_elp_wakeup(wl);
	if (ret < 0)
		goto out;

	if (changed & BSS_CHANGED_CQM) {
		ret = wl1251_acx_low_rssi(wl, bss_conf->cqm_rssi_thold,
					  WL1251_DEFAULT_LOW_RSSI_WEIGHT,
					  WL1251_DEFAULT_LOW_RSSI_DEPTH,
					  WL1251_ACX_LOW_RSSI_TYPE_EDGE);
		if (ret < 0)
			goto out;
		wl->rssi_thold = bss_conf->cqm_rssi_thold;
	}

	if ((changed & BSS_CHANGED_BSSID) &&
	    memcmp(wl->bssid, bss_conf->bssid, ETH_ALEN)) {
		memcpy(wl->bssid, bss_conf->bssid, ETH_ALEN);

		if (!is_zero_ether_addr(wl->bssid)) {
			ret = wl1251_build_null_data(wl);
			if (ret < 0)
				goto out_sleep;

			ret = wl1251_build_qos_null_data(wl);
			if (ret < 0)
				goto out_sleep;

			ret = wl1251_join(wl, wl->bss_type, wl->channel,
					  wl->beacon_int, wl->dtim_period);
			if (ret < 0)
				goto out_sleep;
		}
	}

	if (changed & BSS_CHANGED_ASSOC) {
		if (vif->cfg.assoc) {
			wl->beacon_int = bss_conf->beacon_int;

			skb = ieee80211_pspoll_get(wl->hw, wl->vif);
			if (!skb)
				goto out_sleep;

			ret = wl1251_cmd_template_set(wl, CMD_PS_POLL,
						      skb->data,
						      skb->len);
			dev_kfree_skb(skb);
			if (ret < 0)
				goto out_sleep;

			ret = wl1251_acx_aid(wl, vif->cfg.aid);
			if (ret < 0)
				goto out_sleep;
		} else {
			/* use defaults when not associated */
			wl->beacon_int = WL1251_DEFAULT_BEACON_INT;
			wl->dtim_period = WL1251_DEFAULT_DTIM_PERIOD;
		}
	}
	if (changed & BSS_CHANGED_ERP_SLOT) {
		if (bss_conf->use_short_slot)
			ret = wl1251_acx_slot(wl, SLOT_TIME_SHORT);
		else
			ret = wl1251_acx_slot(wl, SLOT_TIME_LONG);
		if (ret < 0) {
			wl1251_warning("Set slot time failed %d", ret);
			goto out_sleep;
		}
	}

	if (changed & BSS_CHANGED_ERP_PREAMBLE) {
		if (bss_conf->use_short_preamble)
			wl1251_acx_set_preamble(wl, ACX_PREAMBLE_SHORT);
		else
			wl1251_acx_set_preamble(wl, ACX_PREAMBLE_LONG);
	}

	if (changed & BSS_CHANGED_ERP_CTS_PROT) {
		if (bss_conf->use_cts_prot)
			ret = wl1251_acx_cts_protect(wl, CTSPROTECT_ENABLE);
		else
			ret = wl1251_acx_cts_protect(wl, CTSPROTECT_DISABLE);
		if (ret < 0) {
			wl1251_warning("Set ctsprotect failed %d", ret);
			goto out_sleep;
		}
	}

	if (changed & BSS_CHANGED_ARP_FILTER) {
		__be32 addr = vif->cfg.arp_addr_list[0];
		WARN_ON(wl->bss_type != BSS_TYPE_STA_BSS);

		enable = vif->cfg.arp_addr_cnt == 1 && vif->cfg.assoc;
		ret = wl1251_acx_arp_ip_filter(wl, enable, addr);
		if (ret < 0)
			goto out_sleep;
	}

	if (changed & BSS_CHANGED_BEACON) {
		beacon = ieee80211_beacon_get(hw, vif, 0);
		if (!beacon)
			goto out_sleep;

		ret = wl1251_cmd_template_set(wl, CMD_BEACON, beacon->data,
					      beacon->len);

		if (ret < 0) {
			dev_kfree_skb(beacon);
			goto out_sleep;
		}

		ret = wl1251_cmd_template_set(wl, CMD_PROBE_RESP, beacon->data,
					      beacon->len);

		dev_kfree_skb(beacon);

		if (ret < 0)
			goto out_sleep;

		ret = wl1251_join(wl, wl->bss_type, wl->channel,
				  wl->beacon_int, wl->dtim_period);

		if (ret < 0)
			goto out_sleep;
	}

out_sleep:
	wl1251_ps_elp_sleep(wl);

out:
	mutex_unlock(&wl->mutex);
}


/* can't be const, mac80211 writes to this */
static struct ieee80211_rate wl1251_rates[] = {
	{ .bitrate = 10,
	  .hw_value = 0x1,
	  .hw_value_short = 0x1, },
	{ .bitrate = 20,
	  .hw_value = 0x2,
	  .hw_value_short = 0x2,
	  .flags = IEEE80211_RATE_SHORT_PREAMBLE },
	{ .bitrate = 55,
	  .hw_value = 0x4,
	  .hw_value_short = 0x4,
	  .flags = IEEE80211_RATE_SHORT_PREAMBLE },
	{ .bitrate = 110,
	  .hw_value = 0x20,
	  .hw_value_short = 0x20,
	  .flags = IEEE80211_RATE_SHORT_PREAMBLE },
	{ .bitrate = 60,
	  .hw_value = 0x8,
	  .hw_value_short = 0x8, },
	{ .bitrate = 90,
	  .hw_value = 0x10,
	  .hw_value_short = 0x10, },
	{ .bitrate = 120,
	  .hw_value = 0x40,
	  .hw_value_short = 0x40, },
	{ .bitrate = 180,
	  .hw_value = 0x80,
	  .hw_value_short = 0x80, },
	{ .bitrate = 240,
	  .hw_value = 0x200,
	  .hw_value_short = 0x200, },
	{ .bitrate = 360,
	 .hw_value = 0x400,
	 .hw_value_short = 0x400, },
	{ .bitrate = 480,
	  .hw_value = 0x800,
	  .hw_value_short = 0x800, },
	{ .bitrate = 540,
	  .hw_value = 0x1000,
	  .hw_value_short = 0x1000, },
};

/* can't be const, mac80211 writes to this */
static struct ieee80211_channel wl1251_channels[] = {
	{ .hw_value = 1, .center_freq = 2412},
	{ .hw_value = 2, .center_freq = 2417},
	{ .hw_value = 3, .center_freq = 2422},
	{ .hw_value = 4, .center_freq = 2427},
	{ .hw_value = 5, .center_freq = 2432},
	{ .hw_value = 6, .center_freq = 2437},
	{ .hw_value = 7, .center_freq = 2442},
	{ .hw_value = 8, .center_freq = 2447},
	{ .hw_value = 9, .center_freq = 2452},
	{ .hw_value = 10, .center_freq = 2457},
	{ .hw_value = 11, .center_freq = 2462},
	{ .hw_value = 12, .center_freq = 2467},
	{ .hw_value = 13, .center_freq = 2472},
};

static int wl1251_op_conf_tx(struct ieee80211_hw *hw,
			     struct ieee80211_vif *vif,
			     unsigned int link_id, u16 queue,
			     const struct ieee80211_tx_queue_params *params)
{
	enum wl1251_acx_ps_scheme ps_scheme;
	struct wl1251 *wl = hw->priv;
	int ret;

	mutex_lock(&wl->mutex);

	wl1251_debug(DEBUG_MAC80211, "mac80211 conf tx %d", queue);

	ret = wl1251_ps_elp_wakeup(wl);
	if (ret < 0)
		goto out;

	/* mac80211 uses units of 32 usec */
	ret = wl1251_acx_ac_cfg(wl, wl1251_tx_get_queue(queue),
				params->cw_min, params->cw_max,
				params->aifs, params->txop * 32);
	if (ret < 0)
		goto out_sleep;

	if (params->uapsd)
		ps_scheme = WL1251_ACX_PS_SCHEME_UPSD_TRIGGER;
	else
		ps_scheme = WL1251_ACX_PS_SCHEME_LEGACY;

	ret = wl1251_acx_tid_cfg(wl, wl1251_tx_get_queue(queue),
				 CHANNEL_TYPE_EDCF,
				 wl1251_tx_get_queue(queue), ps_scheme,
				 WL1251_ACX_ACK_POLICY_LEGACY);
	if (ret < 0)
		goto out_sleep;

out_sleep:
	wl1251_ps_elp_sleep(wl);

out:
	mutex_unlock(&wl->mutex);

	return ret;
}

static int wl1251_op_get_survey(struct ieee80211_hw *hw, int idx,
				struct survey_info *survey)
{
	struct wl1251 *wl = hw->priv;
	struct ieee80211_conf *conf = &hw->conf;
 
	if (idx != 0)
		return -ENOENT;
 
	survey->channel = conf->chandef.chan;
	survey->filled = SURVEY_INFO_NOISE_DBM;
	survey->noise = wl->noise;
 
	return 0;
}

/* can't be const, mac80211 writes to this */
static struct ieee80211_supported_band wl1251_band_2ghz = {
	.channels = wl1251_channels,
	.n_channels = ARRAY_SIZE(wl1251_channels),
	.bitrates = wl1251_rates,
	.n_bitrates = ARRAY_SIZE(wl1251_rates),
};

static const struct ieee80211_ops wl1251_ops = {
	.start = wl1251_op_start,
	.stop = wl1251_op_stop,
	.add_interface = wl1251_op_add_interface,
	.remove_interface = wl1251_op_remove_interface,
	.config = wl1251_op_config,
	.prepare_multicast = wl1251_op_prepare_multicast,
	.configure_filter = wl1251_op_configure_filter,
	.tx = wl1251_op_tx,
	.wake_tx_queue = ieee80211_handle_wake_tx_queue,
	.set_key = wl1251_op_set_key,
	.hw_scan = wl1251_op_hw_scan,
	.bss_info_changed = wl1251_op_bss_info_changed,
	.set_rts_threshold = wl1251_op_set_rts_threshold,
	.conf_tx = wl1251_op_conf_tx,
	.get_survey = wl1251_op_get_survey,
};

static int wl1251_read_eeprom_byte(struct wl1251 *wl, off_t offset, u8 *data)
{
	unsigned long timeout;

	wl1251_reg_write32(wl, EE_ADDR, offset);
	wl1251_reg_write32(wl, EE_CTL, EE_CTL_READ);

	/* EE_CTL_READ clears when data is ready */
	timeout = jiffies + msecs_to_jiffies(100);
	while (1) {
		if (!(wl1251_reg_read32(wl, EE_CTL) & EE_CTL_READ))
			break;

		if (time_after(jiffies, timeout))
			return -ETIMEDOUT;

		msleep(1);
	}

	*data = wl1251_reg_read32(wl, EE_DATA);
	return 0;
}

static int wl1251_read_eeprom(struct wl1251 *wl, off_t offset,
			      u8 *data, size_t len)
{
	size_t i;
	int ret;

	wl1251_reg_write32(wl, EE_START, 0);

	for (i = 0; i < len; i++) {
		ret = wl1251_read_eeprom_byte(wl, offset + i, &data[i]);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static int wl1251_read_eeprom_mac(struct wl1251 *wl)
{
	u8 mac[ETH_ALEN];
	int i, ret;

	wl1251_set_partition(wl, 0, 0, REGISTERS_BASE, REGISTERS_DOWN_SIZE);

	ret = wl1251_read_eeprom(wl, 0x1c, mac, sizeof(mac));
	if (ret < 0) {
		wl1251_warning("failed to read MAC address from EEPROM");
		return ret;
	}

	/* MAC is stored in reverse order */
	for (i = 0; i < ETH_ALEN; i++)
		wl->mac_addr[i] = mac[ETH_ALEN - i - 1];

	return 0;
}

#define NVS_OFF_MAC_LEN 0x19
#define NVS_OFF_MAC_ADDR_LO 0x1a
#define NVS_OFF_MAC_ADDR_HI 0x1b
#define NVS_OFF_MAC_DATA 0x1c

static int wl1251_check_nvs_mac(struct wl1251 *wl)
{
	if (wl->nvs_len < 0x24)
		return -ENODATA;

	/* length is 2 and data address is 0x546c (ANDed with 0xfffe) */
	if (wl->nvs[NVS_OFF_MAC_LEN] != 2 ||
	    wl->nvs[NVS_OFF_MAC_ADDR_LO] != 0x6d ||
	    wl->nvs[NVS_OFF_MAC_ADDR_HI] != 0x54)
		return -EINVAL;

	return 0;
}

static int wl1251_read_nvs_mac(struct wl1251 *wl)
{
	u8 mac[ETH_ALEN];
	int i, ret;

	ret = wl1251_check_nvs_mac(wl);
	if (ret)
		return ret;

	/* MAC is stored in reverse order */
	for (i = 0; i < ETH_ALEN; i++)
		mac[i] = wl->nvs[NVS_OFF_MAC_DATA + ETH_ALEN - i - 1];

	/* 00:00:20:07:03:09 is in example file wl1251-nvs.bin, so invalid */
	if (ether_addr_equal_unaligned(mac, "\x00\x00\x20\x07\x03\x09"))
		return -EINVAL;

	memcpy(wl->mac_addr, mac, ETH_ALEN);
	return 0;
}

static int wl1251_write_nvs_mac(struct wl1251 *wl)
{
	int i, ret;

	ret = wl1251_check_nvs_mac(wl);
	if (ret)
		return ret;

	/* MAC is stored in reverse order */
	for (i = 0; i < ETH_ALEN; i++)
		wl->nvs[NVS_OFF_MAC_DATA + i] = wl->mac_addr[ETH_ALEN - i - 1];

	return 0;
}

static int wl1251_register_hw(struct wl1251 *wl)
{
	int ret;

	if (wl->mac80211_registered)
		return 0;

	SET_IEEE80211_PERM_ADDR(wl->hw, wl->mac_addr);

	ret = ieee80211_register_hw(wl->hw);
	if (ret < 0) {
		wl1251_error("unable to register mac80211 hw: %d", ret);
		return ret;
	}

	wl->mac80211_registered = true;

	wl1251_notice("loaded");

	return 0;
}

int wl1251_init_ieee80211(struct wl1251 *wl)
{
	int ret;

	/* The tx descriptor buffer and the TKIP space */
	wl->hw->extra_tx_headroom = sizeof(struct tx_double_buffer_desc)
		+ WL1251_TKIP_IV_SPACE;

	/* unit us */
	/* FIXME: find a proper value */

	ieee80211_hw_set(wl->hw, SIGNAL_DBM);
	ieee80211_hw_set(wl->hw, SUPPORTS_PS);

	wl->hw->wiphy->interface_modes = BIT(NL80211_IFTYPE_STATION) |
					 BIT(NL80211_IFTYPE_ADHOC);
	wl->hw->wiphy->max_scan_ssids = 1;

	/* We set max_scan_ie_len to a random value to make wpa_supplicant scans not
	 * fail, as the driver will the ignore the extra passed IEs anyway
	 */
	wl->hw->wiphy->max_scan_ie_len = 512;

	wl->hw->wiphy->bands[NL80211_BAND_2GHZ] = &wl1251_band_2ghz;

	wl->hw->queues = 4;

	if (wl->nvs == NULL && !wl->use_eeprom) {
		ret = wl1251_fetch_nvs(wl);
		if (ret < 0)
			goto out;
	}

	if (wl->use_eeprom)
		ret = wl1251_read_eeprom_mac(wl);
	else
		ret = wl1251_read_nvs_mac(wl);

	if (ret == 0 && !is_valid_ether_addr(wl->mac_addr))
		ret = -EINVAL;

	if (ret < 0) {
		/*
		 * In case our MAC address is not correctly set,
		 * we use a random but Nokia MAC.
		 */
		static const u8 nokia_oui[3] = {0x00, 0x1f, 0xdf};
		memcpy(wl->mac_addr, nokia_oui, 3);
		get_random_bytes(wl->mac_addr + 3, 3);
		if (!wl->use_eeprom)
			wl1251_write_nvs_mac(wl);
		wl1251_warning("MAC address in eeprom or nvs data is not valid");
		wl1251_warning("Setting random MAC address: %pM", wl->mac_addr);
	}

	ret = wl1251_register_hw(wl);
	if (ret)
		goto out;

	wl1251_debugfs_init(wl);
	wl1251_notice("initialized");

	ret = 0;

out:
	return ret;
}
EXPORT_SYMBOL_GPL(wl1251_init_ieee80211);

struct ieee80211_hw *wl1251_alloc_hw(void)
{
	struct ieee80211_hw *hw;
	struct wl1251 *wl;
	int i;

	hw = ieee80211_alloc_hw(sizeof(*wl), &wl1251_ops);
	if (!hw) {
		wl1251_error("could not alloc ieee80211_hw");
		return ERR_PTR(-ENOMEM);
	}

	wl = hw->priv;
	memset(wl, 0, sizeof(*wl));

	wl->hw = hw;

	wl->data_in_count = 0;

	skb_queue_head_init(&wl->tx_queue);

	INIT_DELAYED_WORK(&wl->elp_work, wl1251_elp_work);
	wl->channel = WL1251_DEFAULT_CHANNEL;
	wl->monitor_present = false;
	wl->joined = false;
	wl->scanning = false;
	wl->bss_type = MAX_BSS_TYPE;
	wl->default_key = 0;
	wl->listen_int = 1;
	wl->rx_counter = 0;
	wl->rx_handled = 0;
	wl->rx_current_buffer = 0;
	wl->rx_last_id = 0;
	wl->rx_config = WL1251_DEFAULT_RX_CONFIG;
	wl->rx_filter = WL1251_DEFAULT_RX_FILTER;
	wl->elp = false;
	wl->station_mode = STATION_ACTIVE_MODE;
	wl->psm_requested = false;
	wl->psm_entry_retry = 0;
	wl->tx_queue_stopped = false;
	wl->power_level = WL1251_DEFAULT_POWER_LEVEL;
	wl->rssi_thold = 0;
	wl->beacon_int = WL1251_DEFAULT_BEACON_INT;
	wl->dtim_period = WL1251_DEFAULT_DTIM_PERIOD;
	wl->vif = NULL;

	for (i = 0; i < FW_TX_CMPLT_BLOCK_SIZE; i++)
		wl->tx_frames[i] = NULL;

	wl->next_tx_complete = 0;

	INIT_WORK(&wl->irq_work, wl1251_irq_work);
	INIT_WORK(&wl->tx_work, wl1251_tx_work);

	wl->state = WL1251_STATE_OFF;
	mutex_init(&wl->mutex);
	spin_lock_init(&wl->wl_lock);

	wl->tx_mgmt_frm_rate = DEFAULT_HW_GEN_TX_RATE;
	wl->tx_mgmt_frm_mod = DEFAULT_HW_GEN_MODULATION_TYPE;

	wl->rx_descriptor = kmalloc(sizeof(*wl->rx_descriptor), GFP_KERNEL);
	if (!wl->rx_descriptor) {
		wl1251_error("could not allocate memory for rx descriptor");
		ieee80211_free_hw(hw);
		return ERR_PTR(-ENOMEM);
	}

	return hw;
}
EXPORT_SYMBOL_GPL(wl1251_alloc_hw);

int wl1251_free_hw(struct wl1251 *wl)
{
	ieee80211_unregister_hw(wl->hw);

	wl1251_debugfs_exit(wl);

	kfree(wl->target_mem_map);
	kfree(wl->data_path);
	vfree(wl->fw);
	wl->fw = NULL;
	kfree(wl->nvs);
	wl->nvs = NULL;

	kfree(wl->rx_descriptor);
	wl->rx_descriptor = NULL;

	ieee80211_free_hw(wl->hw);

	return 0;
}
EXPORT_SYMBOL_GPL(wl1251_free_hw);

MODULE_DESCRIPTION("TI wl1251 Wireless LAN Driver Core");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kalle Valo <kvalo@adurom.com>");
MODULE_FIRMWARE(WL1251_FW_NAME);
MODULE_FIRMWARE(WL1251_NVS_NAME);

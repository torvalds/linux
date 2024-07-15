// SPDX-License-Identifier: GPL-2.0-only
/*
 * This file is part of wl1271
 *
 * Copyright (C) 2009-2010 Nokia Corporation
 *
 * Contact: Luciano Coelho <luciano.coelho@nokia.com>
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/spi/spi.h>
#include <linux/etherdevice.h>
#include <linux/ieee80211.h>
#include <linux/slab.h>

#include "wlcore.h"
#include "debug.h"
#include "io.h"
#include "acx.h"
#include "wl12xx_80211.h"
#include "cmd.h"
#include "event.h"
#include "tx.h"
#include "hw_ops.h"

#define WL1271_CMD_FAST_POLL_COUNT       50
#define WL1271_WAIT_EVENT_FAST_POLL_COUNT 20

/*
 * send command to firmware
 *
 * @wl: wl struct
 * @id: command id
 * @buf: buffer containing the command, must work with dma
 * @len: length of the buffer
 * return the cmd status code on success.
 */
static int __wlcore_cmd_send(struct wl1271 *wl, u16 id, void *buf,
			     size_t len, size_t res_len)
{
	struct wl1271_cmd_header *cmd;
	unsigned long timeout;
	u32 intr;
	int ret;
	u16 status;
	u16 poll_count = 0;

	if (unlikely(wl->state == WLCORE_STATE_RESTARTING &&
		     id != CMD_STOP_FWLOGGER))
		return -EIO;

	if (WARN_ON_ONCE(len < sizeof(*cmd)))
		return -EIO;

	cmd = buf;
	cmd->id = cpu_to_le16(id);
	cmd->status = 0;

	WARN_ON(len % 4 != 0);
	WARN_ON(test_bit(WL1271_FLAG_IN_ELP, &wl->flags));

	ret = wlcore_write(wl, wl->cmd_box_addr, buf, len, false);
	if (ret < 0)
		return ret;

	/*
	 * TODO: we just need this because one bit is in a different
	 * place.  Is there any better way?
	 */
	ret = wl->ops->trigger_cmd(wl, wl->cmd_box_addr, buf, len);
	if (ret < 0)
		return ret;

	timeout = jiffies + msecs_to_jiffies(WL1271_COMMAND_TIMEOUT);

	ret = wlcore_read_reg(wl, REG_INTERRUPT_NO_CLEAR, &intr);
	if (ret < 0)
		return ret;

	while (!(intr & WL1271_ACX_INTR_CMD_COMPLETE)) {
		if (time_after(jiffies, timeout)) {
			wl1271_error("command complete timeout");
			return -ETIMEDOUT;
		}

		poll_count++;
		if (poll_count < WL1271_CMD_FAST_POLL_COUNT)
			udelay(10);
		else
			msleep(1);

		ret = wlcore_read_reg(wl, REG_INTERRUPT_NO_CLEAR, &intr);
		if (ret < 0)
			return ret;
	}

	/* read back the status code of the command */
	if (res_len == 0)
		res_len = sizeof(struct wl1271_cmd_header);

	ret = wlcore_read(wl, wl->cmd_box_addr, cmd, res_len, false);
	if (ret < 0)
		return ret;

	status = le16_to_cpu(cmd->status);

	ret = wlcore_write_reg(wl, REG_INTERRUPT_ACK,
			       WL1271_ACX_INTR_CMD_COMPLETE);
	if (ret < 0)
		return ret;

	return status;
}

/*
 * send command to fw and return cmd status on success
 * valid_rets contains a bitmap of allowed error codes
 */
static int wlcore_cmd_send_failsafe(struct wl1271 *wl, u16 id, void *buf,
				    size_t len, size_t res_len,
				    unsigned long valid_rets)
{
	int ret = __wlcore_cmd_send(wl, id, buf, len, res_len);

	if (ret < 0)
		goto fail;

	/* success is always a valid status */
	valid_rets |= BIT(CMD_STATUS_SUCCESS);

	if (ret >= MAX_COMMAND_STATUS ||
	    !test_bit(ret, &valid_rets)) {
		wl1271_error("command execute failure %d", ret);
		ret = -EIO;
		goto fail;
	}
	return ret;
fail:
	wl12xx_queue_recovery_work(wl);
	return ret;
}

/*
 * wrapper for wlcore_cmd_send that accept only CMD_STATUS_SUCCESS
 * return 0 on success.
 */
int wl1271_cmd_send(struct wl1271 *wl, u16 id, void *buf, size_t len,
		    size_t res_len)
{
	int ret = wlcore_cmd_send_failsafe(wl, id, buf, len, res_len, 0);

	if (ret < 0)
		return ret;
	return 0;
}
EXPORT_SYMBOL_GPL(wl1271_cmd_send);

/*
 * Poll the mailbox event field until any of the bits in the mask is set or a
 * timeout occurs (WL1271_EVENT_TIMEOUT in msecs)
 */
int wlcore_cmd_wait_for_event_or_timeout(struct wl1271 *wl,
					 u32 mask, bool *timeout)
{
	u32 *events_vector;
	u32 event;
	unsigned long timeout_time;
	u16 poll_count = 0;
	int ret = 0;

	*timeout = false;

	events_vector = kmalloc(sizeof(*events_vector), GFP_KERNEL | GFP_DMA);
	if (!events_vector)
		return -ENOMEM;

	timeout_time = jiffies + msecs_to_jiffies(WL1271_EVENT_TIMEOUT);

	ret = pm_runtime_resume_and_get(wl->dev);
	if (ret < 0)
		goto free_vector;

	do {
		if (time_after(jiffies, timeout_time)) {
			wl1271_debug(DEBUG_CMD, "timeout waiting for event %d",
				     (int)mask);
			*timeout = true;
			goto out;
		}

		poll_count++;
		if (poll_count < WL1271_WAIT_EVENT_FAST_POLL_COUNT)
			usleep_range(50, 51);
		else
			usleep_range(1000, 5000);

		/* read from both event fields */
		ret = wlcore_read(wl, wl->mbox_ptr[0], events_vector,
				  sizeof(*events_vector), false);
		if (ret < 0)
			goto out;

		event = *events_vector & mask;

		ret = wlcore_read(wl, wl->mbox_ptr[1], events_vector,
				  sizeof(*events_vector), false);
		if (ret < 0)
			goto out;

		event |= *events_vector & mask;
	} while (!event);

out:
	pm_runtime_mark_last_busy(wl->dev);
	pm_runtime_put_autosuspend(wl->dev);
free_vector:
	kfree(events_vector);
	return ret;
}
EXPORT_SYMBOL_GPL(wlcore_cmd_wait_for_event_or_timeout);

int wl12xx_cmd_role_enable(struct wl1271 *wl, u8 *addr, u8 role_type,
			   u8 *role_id)
{
	struct wl12xx_cmd_role_enable *cmd;
	int ret;

	wl1271_debug(DEBUG_CMD, "cmd role enable");

	if (WARN_ON(*role_id != WL12XX_INVALID_ROLE_ID))
		return -EBUSY;

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (!cmd) {
		ret = -ENOMEM;
		goto out;
	}

	/* get role id */
	cmd->role_id = find_first_zero_bit(wl->roles_map, WL12XX_MAX_ROLES);
	if (cmd->role_id >= WL12XX_MAX_ROLES) {
		ret = -EBUSY;
		goto out_free;
	}

	memcpy(cmd->mac_address, addr, ETH_ALEN);
	cmd->role_type = role_type;

	ret = wl1271_cmd_send(wl, CMD_ROLE_ENABLE, cmd, sizeof(*cmd), 0);
	if (ret < 0) {
		wl1271_error("failed to initiate cmd role enable");
		goto out_free;
	}

	__set_bit(cmd->role_id, wl->roles_map);
	*role_id = cmd->role_id;

out_free:
	kfree(cmd);

out:
	return ret;
}

int wl12xx_cmd_role_disable(struct wl1271 *wl, u8 *role_id)
{
	struct wl12xx_cmd_role_disable *cmd;
	int ret;

	wl1271_debug(DEBUG_CMD, "cmd role disable");

	if (WARN_ON(*role_id == WL12XX_INVALID_ROLE_ID))
		return -ENOENT;

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (!cmd) {
		ret = -ENOMEM;
		goto out;
	}
	cmd->role_id = *role_id;

	ret = wl1271_cmd_send(wl, CMD_ROLE_DISABLE, cmd, sizeof(*cmd), 0);
	if (ret < 0) {
		wl1271_error("failed to initiate cmd role disable");
		goto out_free;
	}

	__clear_bit(*role_id, wl->roles_map);
	*role_id = WL12XX_INVALID_ROLE_ID;

out_free:
	kfree(cmd);

out:
	return ret;
}

static int wlcore_get_new_session_id(struct wl1271 *wl, u8 hlid)
{
	if (wl->session_ids[hlid] >= SESSION_COUNTER_MAX)
		wl->session_ids[hlid] = 0;

	wl->session_ids[hlid]++;

	return wl->session_ids[hlid];
}

int wl12xx_allocate_link(struct wl1271 *wl, struct wl12xx_vif *wlvif, u8 *hlid)
{
	unsigned long flags;
	u8 link = find_first_zero_bit(wl->links_map, wl->num_links);
	if (link >= wl->num_links)
		return -EBUSY;

	wl->session_ids[link] = wlcore_get_new_session_id(wl, link);

	/* these bits are used by op_tx */
	spin_lock_irqsave(&wl->wl_lock, flags);
	__set_bit(link, wl->links_map);
	__set_bit(link, wlvif->links_map);
	spin_unlock_irqrestore(&wl->wl_lock, flags);

	/*
	 * take the last "freed packets" value from the current FW status.
	 * on recovery, we might not have fw_status yet, and
	 * tx_lnk_free_pkts will be NULL. check for it.
	 */
	if (wl->fw_status->counters.tx_lnk_free_pkts)
		wl->links[link].prev_freed_pkts =
			wl->fw_status->counters.tx_lnk_free_pkts[link];
	wl->links[link].wlvif = wlvif;

	/*
	 * Take saved value for total freed packets from wlvif, in case this is
	 * recovery/resume
	 */
	if (wlvif->bss_type != BSS_TYPE_AP_BSS)
		wl->links[link].total_freed_pkts = wlvif->total_freed_pkts;

	*hlid = link;

	wl->active_link_count++;
	return 0;
}

void wl12xx_free_link(struct wl1271 *wl, struct wl12xx_vif *wlvif, u8 *hlid)
{
	unsigned long flags;

	if (*hlid == WL12XX_INVALID_LINK_ID)
		return;

	/* these bits are used by op_tx */
	spin_lock_irqsave(&wl->wl_lock, flags);
	__clear_bit(*hlid, wl->links_map);
	__clear_bit(*hlid, wlvif->links_map);
	spin_unlock_irqrestore(&wl->wl_lock, flags);

	wl->links[*hlid].allocated_pkts = 0;
	wl->links[*hlid].prev_freed_pkts = 0;
	wl->links[*hlid].ba_bitmap = 0;
	eth_zero_addr(wl->links[*hlid].addr);

	/*
	 * At this point op_tx() will not add more packets to the queues. We
	 * can purge them.
	 */
	wl1271_tx_reset_link_queues(wl, *hlid);
	wl->links[*hlid].wlvif = NULL;

	if (wlvif->bss_type == BSS_TYPE_AP_BSS &&
	    *hlid == wlvif->ap.bcast_hlid) {
		u32 sqn_padding = WL1271_TX_SQN_POST_RECOVERY_PADDING;
		/*
		 * save the total freed packets in the wlvif, in case this is
		 * recovery or suspend
		 */
		wlvif->total_freed_pkts = wl->links[*hlid].total_freed_pkts;

		/*
		 * increment the initial seq number on recovery to account for
		 * transmitted packets that we haven't yet got in the FW status
		 */
		if (wlvif->encryption_type == KEY_GEM)
			sqn_padding = WL1271_TX_SQN_POST_RECOVERY_PADDING_GEM;

		if (test_bit(WL1271_FLAG_RECOVERY_IN_PROGRESS, &wl->flags))
			wlvif->total_freed_pkts += sqn_padding;
	}

	wl->links[*hlid].total_freed_pkts = 0;

	*hlid = WL12XX_INVALID_LINK_ID;
	wl->active_link_count--;
	WARN_ON_ONCE(wl->active_link_count < 0);
}

u8 wlcore_get_native_channel_type(u8 nl_channel_type)
{
	switch (nl_channel_type) {
	case NL80211_CHAN_NO_HT:
		return WLCORE_CHAN_NO_HT;
	case NL80211_CHAN_HT20:
		return WLCORE_CHAN_HT20;
	case NL80211_CHAN_HT40MINUS:
		return WLCORE_CHAN_HT40MINUS;
	case NL80211_CHAN_HT40PLUS:
		return WLCORE_CHAN_HT40PLUS;
	default:
		WARN_ON(1);
		return WLCORE_CHAN_NO_HT;
	}
}
EXPORT_SYMBOL_GPL(wlcore_get_native_channel_type);

static int wl12xx_cmd_role_start_dev(struct wl1271 *wl,
				     struct wl12xx_vif *wlvif,
				     enum nl80211_band band,
				     int channel)
{
	struct wl12xx_cmd_role_start *cmd;
	int ret;

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (!cmd) {
		ret = -ENOMEM;
		goto out;
	}

	wl1271_debug(DEBUG_CMD, "cmd role start dev %d", wlvif->dev_role_id);

	cmd->role_id = wlvif->dev_role_id;
	if (band == NL80211_BAND_5GHZ)
		cmd->band = WLCORE_BAND_5GHZ;
	cmd->channel = channel;

	if (wlvif->dev_hlid == WL12XX_INVALID_LINK_ID) {
		ret = wl12xx_allocate_link(wl, wlvif, &wlvif->dev_hlid);
		if (ret)
			goto out_free;
	}
	cmd->device.hlid = wlvif->dev_hlid;
	cmd->device.session = wl->session_ids[wlvif->dev_hlid];

	wl1271_debug(DEBUG_CMD, "role start: roleid=%d, hlid=%d, session=%d",
		     cmd->role_id, cmd->device.hlid, cmd->device.session);

	ret = wl1271_cmd_send(wl, CMD_ROLE_START, cmd, sizeof(*cmd), 0);
	if (ret < 0) {
		wl1271_error("failed to initiate cmd role enable");
		goto err_hlid;
	}

	goto out_free;

err_hlid:
	/* clear links on error */
	wl12xx_free_link(wl, wlvif, &wlvif->dev_hlid);

out_free:
	kfree(cmd);

out:
	return ret;
}

static int wl12xx_cmd_role_stop_dev(struct wl1271 *wl,
				    struct wl12xx_vif *wlvif)
{
	struct wl12xx_cmd_role_stop *cmd;
	int ret;

	if (WARN_ON(wlvif->dev_hlid == WL12XX_INVALID_LINK_ID))
		return -EINVAL;

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (!cmd) {
		ret = -ENOMEM;
		goto out;
	}

	wl1271_debug(DEBUG_CMD, "cmd role stop dev");

	cmd->role_id = wlvif->dev_role_id;
	cmd->disc_type = DISCONNECT_IMMEDIATE;
	cmd->reason = cpu_to_le16(WLAN_REASON_UNSPECIFIED);

	ret = wl1271_cmd_send(wl, CMD_ROLE_STOP, cmd, sizeof(*cmd), 0);
	if (ret < 0) {
		wl1271_error("failed to initiate cmd role stop");
		goto out_free;
	}

	wl12xx_free_link(wl, wlvif, &wlvif->dev_hlid);

out_free:
	kfree(cmd);

out:
	return ret;
}

int wl12xx_cmd_role_start_sta(struct wl1271 *wl, struct wl12xx_vif *wlvif)
{
	struct ieee80211_vif *vif = wl12xx_wlvif_to_vif(wlvif);
	struct wl12xx_cmd_role_start *cmd;
	u32 supported_rates;
	int ret;

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (!cmd) {
		ret = -ENOMEM;
		goto out;
	}

	wl1271_debug(DEBUG_CMD, "cmd role start sta %d", wlvif->role_id);

	cmd->role_id = wlvif->role_id;
	if (wlvif->band == NL80211_BAND_5GHZ)
		cmd->band = WLCORE_BAND_5GHZ;
	cmd->channel = wlvif->channel;
	cmd->sta.basic_rate_set = cpu_to_le32(wlvif->basic_rate_set);
	cmd->sta.beacon_interval = cpu_to_le16(wlvif->beacon_int);
	cmd->sta.ssid_type = WL12XX_SSID_TYPE_ANY;
	cmd->sta.ssid_len = wlvif->ssid_len;
	memcpy(cmd->sta.ssid, wlvif->ssid, wlvif->ssid_len);
	memcpy(cmd->sta.bssid, vif->bss_conf.bssid, ETH_ALEN);

	supported_rates = CONF_TX_ENABLED_RATES | CONF_TX_MCS_RATES |
			  wlcore_hw_sta_get_ap_rate_mask(wl, wlvif);
	if (wlvif->p2p)
		supported_rates &= ~CONF_TX_CCK_RATES;

	cmd->sta.local_rates = cpu_to_le32(supported_rates);

	cmd->channel_type = wlcore_get_native_channel_type(wlvif->channel_type);

	if (wlvif->sta.hlid == WL12XX_INVALID_LINK_ID) {
		ret = wl12xx_allocate_link(wl, wlvif, &wlvif->sta.hlid);
		if (ret)
			goto out_free;
	}
	cmd->sta.hlid = wlvif->sta.hlid;
	cmd->sta.session = wl->session_ids[wlvif->sta.hlid];
	/*
	 * We don't have the correct remote rates in this stage.  The
	 * rates will be reconfigured later, after association, if the
	 * firmware supports ACX_PEER_CAP.  Otherwise, there's nothing
	 * we can do, so use all supported_rates here.
	 */
	cmd->sta.remote_rates = cpu_to_le32(supported_rates);

	wl1271_debug(DEBUG_CMD, "role start: roleid=%d, hlid=%d, session=%d "
		     "basic_rate_set: 0x%x, remote_rates: 0x%x",
		     wlvif->role_id, cmd->sta.hlid, cmd->sta.session,
		     wlvif->basic_rate_set, wlvif->rate_set);

	ret = wl1271_cmd_send(wl, CMD_ROLE_START, cmd, sizeof(*cmd), 0);
	if (ret < 0) {
		wl1271_error("failed to initiate cmd role start sta");
		goto err_hlid;
	}

	wlvif->sta.role_chan_type = wlvif->channel_type;
	goto out_free;

err_hlid:
	/* clear links on error. */
	wl12xx_free_link(wl, wlvif, &wlvif->sta.hlid);

out_free:
	kfree(cmd);

out:
	return ret;
}

/* use this function to stop ibss as well */
int wl12xx_cmd_role_stop_sta(struct wl1271 *wl, struct wl12xx_vif *wlvif)
{
	struct wl12xx_cmd_role_stop *cmd;
	int ret;

	if (WARN_ON(wlvif->sta.hlid == WL12XX_INVALID_LINK_ID))
		return -EINVAL;

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (!cmd) {
		ret = -ENOMEM;
		goto out;
	}

	wl1271_debug(DEBUG_CMD, "cmd role stop sta %d", wlvif->role_id);

	cmd->role_id = wlvif->role_id;
	cmd->disc_type = DISCONNECT_IMMEDIATE;
	cmd->reason = cpu_to_le16(WLAN_REASON_UNSPECIFIED);

	ret = wl1271_cmd_send(wl, CMD_ROLE_STOP, cmd, sizeof(*cmd), 0);
	if (ret < 0) {
		wl1271_error("failed to initiate cmd role stop sta");
		goto out_free;
	}

	wl12xx_free_link(wl, wlvif, &wlvif->sta.hlid);

out_free:
	kfree(cmd);

out:
	return ret;
}

int wl12xx_cmd_role_start_ap(struct wl1271 *wl, struct wl12xx_vif *wlvif)
{
	struct wl12xx_cmd_role_start *cmd;
	struct ieee80211_vif *vif = wl12xx_wlvif_to_vif(wlvif);
	struct ieee80211_bss_conf *bss_conf = &vif->bss_conf;
	u32 supported_rates;
	int ret;

	wl1271_debug(DEBUG_CMD, "cmd role start ap %d", wlvif->role_id);

	/* If MESH --> ssid_len is always 0 */
	if (!ieee80211_vif_is_mesh(vif)) {
		/* trying to use hidden SSID with an old hostapd version */
		if (wlvif->ssid_len == 0 && !bss_conf->hidden_ssid) {
			wl1271_error("got a null SSID from beacon/bss");
			ret = -EINVAL;
			goto out;
		}
	}

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (!cmd) {
		ret = -ENOMEM;
		goto out;
	}

	ret = wl12xx_allocate_link(wl, wlvif, &wlvif->ap.global_hlid);
	if (ret < 0)
		goto out_free;

	ret = wl12xx_allocate_link(wl, wlvif, &wlvif->ap.bcast_hlid);
	if (ret < 0)
		goto out_free_global;

	/* use the previous security seq, if this is a recovery/resume */
	wl->links[wlvif->ap.bcast_hlid].total_freed_pkts =
						wlvif->total_freed_pkts;

	cmd->role_id = wlvif->role_id;
	cmd->ap.aging_period = cpu_to_le16(wl->conf.tx.ap_aging_period);
	cmd->ap.bss_index = WL1271_AP_BSS_INDEX;
	cmd->ap.global_hlid = wlvif->ap.global_hlid;
	cmd->ap.broadcast_hlid = wlvif->ap.bcast_hlid;
	cmd->ap.global_session_id = wl->session_ids[wlvif->ap.global_hlid];
	cmd->ap.bcast_session_id = wl->session_ids[wlvif->ap.bcast_hlid];
	cmd->ap.basic_rate_set = cpu_to_le32(wlvif->basic_rate_set);
	cmd->ap.beacon_interval = cpu_to_le16(wlvif->beacon_int);
	cmd->ap.dtim_interval = bss_conf->dtim_period;
	cmd->ap.beacon_expiry = WL1271_AP_DEF_BEACON_EXP;
	/* FIXME: Change when adding DFS */
	cmd->ap.reset_tsf = 1;  /* By default reset AP TSF */
	cmd->ap.wmm = wlvif->wmm_enabled;
	cmd->channel = wlvif->channel;
	cmd->channel_type = wlcore_get_native_channel_type(wlvif->channel_type);

	if (!bss_conf->hidden_ssid) {
		/* take the SSID from the beacon for backward compatibility */
		cmd->ap.ssid_type = WL12XX_SSID_TYPE_PUBLIC;
		cmd->ap.ssid_len = wlvif->ssid_len;
		memcpy(cmd->ap.ssid, wlvif->ssid, wlvif->ssid_len);
	} else {
		cmd->ap.ssid_type = WL12XX_SSID_TYPE_HIDDEN;
		cmd->ap.ssid_len = vif->cfg.ssid_len;
		memcpy(cmd->ap.ssid, vif->cfg.ssid, vif->cfg.ssid_len);
	}

	supported_rates = CONF_TX_ENABLED_RATES | CONF_TX_MCS_RATES |
		wlcore_hw_ap_get_mimo_wide_rate_mask(wl, wlvif);
	if (wlvif->p2p)
		supported_rates &= ~CONF_TX_CCK_RATES;

	wl1271_debug(DEBUG_CMD, "cmd role start ap with supported_rates 0x%08x",
		     supported_rates);

	cmd->ap.local_rates = cpu_to_le32(supported_rates);

	switch (wlvif->band) {
	case NL80211_BAND_2GHZ:
		cmd->band = WLCORE_BAND_2_4GHZ;
		break;
	case NL80211_BAND_5GHZ:
		cmd->band = WLCORE_BAND_5GHZ;
		break;
	default:
		wl1271_warning("ap start - unknown band: %d", (int)wlvif->band);
		cmd->band = WLCORE_BAND_2_4GHZ;
		break;
	}

	ret = wl1271_cmd_send(wl, CMD_ROLE_START, cmd, sizeof(*cmd), 0);
	if (ret < 0) {
		wl1271_error("failed to initiate cmd role start ap");
		goto out_free_bcast;
	}

	goto out_free;

out_free_bcast:
	wl12xx_free_link(wl, wlvif, &wlvif->ap.bcast_hlid);

out_free_global:
	wl12xx_free_link(wl, wlvif, &wlvif->ap.global_hlid);

out_free:
	kfree(cmd);

out:
	return ret;
}

int wl12xx_cmd_role_stop_ap(struct wl1271 *wl, struct wl12xx_vif *wlvif)
{
	struct wl12xx_cmd_role_stop *cmd;
	int ret;

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (!cmd) {
		ret = -ENOMEM;
		goto out;
	}

	wl1271_debug(DEBUG_CMD, "cmd role stop ap %d", wlvif->role_id);

	cmd->role_id = wlvif->role_id;

	ret = wl1271_cmd_send(wl, CMD_ROLE_STOP, cmd, sizeof(*cmd), 0);
	if (ret < 0) {
		wl1271_error("failed to initiate cmd role stop ap");
		goto out_free;
	}

	wl12xx_free_link(wl, wlvif, &wlvif->ap.bcast_hlid);
	wl12xx_free_link(wl, wlvif, &wlvif->ap.global_hlid);

out_free:
	kfree(cmd);

out:
	return ret;
}

int wl12xx_cmd_role_start_ibss(struct wl1271 *wl, struct wl12xx_vif *wlvif)
{
	struct ieee80211_vif *vif = wl12xx_wlvif_to_vif(wlvif);
	struct wl12xx_cmd_role_start *cmd;
	struct ieee80211_bss_conf *bss_conf = &vif->bss_conf;
	int ret;

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (!cmd) {
		ret = -ENOMEM;
		goto out;
	}

	wl1271_debug(DEBUG_CMD, "cmd role start ibss %d", wlvif->role_id);

	cmd->role_id = wlvif->role_id;
	if (wlvif->band == NL80211_BAND_5GHZ)
		cmd->band = WLCORE_BAND_5GHZ;
	cmd->channel = wlvif->channel;
	cmd->ibss.basic_rate_set = cpu_to_le32(wlvif->basic_rate_set);
	cmd->ibss.beacon_interval = cpu_to_le16(wlvif->beacon_int);
	cmd->ibss.dtim_interval = bss_conf->dtim_period;
	cmd->ibss.ssid_type = WL12XX_SSID_TYPE_ANY;
	cmd->ibss.ssid_len = wlvif->ssid_len;
	memcpy(cmd->ibss.ssid, wlvif->ssid, wlvif->ssid_len);
	memcpy(cmd->ibss.bssid, vif->bss_conf.bssid, ETH_ALEN);
	cmd->sta.local_rates = cpu_to_le32(wlvif->rate_set);

	if (wlvif->sta.hlid == WL12XX_INVALID_LINK_ID) {
		ret = wl12xx_allocate_link(wl, wlvif, &wlvif->sta.hlid);
		if (ret)
			goto out_free;
	}
	cmd->ibss.hlid = wlvif->sta.hlid;
	cmd->ibss.remote_rates = cpu_to_le32(wlvif->rate_set);

	wl1271_debug(DEBUG_CMD, "role start: roleid=%d, hlid=%d, session=%d "
		     "basic_rate_set: 0x%x, remote_rates: 0x%x",
		     wlvif->role_id, cmd->sta.hlid, cmd->sta.session,
		     wlvif->basic_rate_set, wlvif->rate_set);

	wl1271_debug(DEBUG_CMD, "vif->bss_conf.bssid = %pM",
		     vif->bss_conf.bssid);

	ret = wl1271_cmd_send(wl, CMD_ROLE_START, cmd, sizeof(*cmd), 0);
	if (ret < 0) {
		wl1271_error("failed to initiate cmd role enable");
		goto err_hlid;
	}

	goto out_free;

err_hlid:
	/* clear links on error. */
	wl12xx_free_link(wl, wlvif, &wlvif->sta.hlid);

out_free:
	kfree(cmd);

out:
	return ret;
}


/**
 * wl1271_cmd_test - send test command to firmware
 *
 * @wl: wl struct
 * @buf: buffer containing the command, with all headers, must work with dma
 * @buf_len: length of the buffer
 * @answer: is answer needed
 */
int wl1271_cmd_test(struct wl1271 *wl, void *buf, size_t buf_len, u8 answer)
{
	int ret;
	size_t res_len = 0;

	wl1271_debug(DEBUG_CMD, "cmd test");

	if (answer)
		res_len = buf_len;

	ret = wl1271_cmd_send(wl, CMD_TEST, buf, buf_len, res_len);

	if (ret < 0) {
		wl1271_warning("TEST command failed");
		return ret;
	}

	return ret;
}
EXPORT_SYMBOL_GPL(wl1271_cmd_test);

/**
 * wl1271_cmd_interrogate - read acx from firmware
 *
 * @wl: wl struct
 * @id: acx id
 * @buf: buffer for the response, including all headers, must work with dma
 * @cmd_len: length of command
 * @res_len: length of payload
 */
int wl1271_cmd_interrogate(struct wl1271 *wl, u16 id, void *buf,
			   size_t cmd_len, size_t res_len)
{
	struct acx_header *acx = buf;
	int ret;

	wl1271_debug(DEBUG_CMD, "cmd interrogate");

	acx->id = cpu_to_le16(id);

	/* response payload length, does not include any headers */
	acx->len = cpu_to_le16(res_len - sizeof(*acx));

	ret = wl1271_cmd_send(wl, CMD_INTERROGATE, acx, cmd_len, res_len);
	if (ret < 0)
		wl1271_error("INTERROGATE command failed");

	return ret;
}

/**
 * wlcore_cmd_configure_failsafe - write acx value to firmware
 *
 * @wl: wl struct
 * @id: acx id
 * @buf: buffer containing acx, including all headers, must work with dma
 * @len: length of buf
 * @valid_rets: bitmap of valid cmd status codes (i.e. return values).
 * return the cmd status on success.
 */
int wlcore_cmd_configure_failsafe(struct wl1271 *wl, u16 id, void *buf,
				  size_t len, unsigned long valid_rets)
{
	struct acx_header *acx = buf;
	int ret;

	wl1271_debug(DEBUG_CMD, "cmd configure (%d)", id);

	if (WARN_ON_ONCE(len < sizeof(*acx)))
		return -EIO;

	acx->id = cpu_to_le16(id);

	/* payload length, does not include any headers */
	acx->len = cpu_to_le16(len - sizeof(*acx));

	ret = wlcore_cmd_send_failsafe(wl, CMD_CONFIGURE, acx, len, 0,
				       valid_rets);
	if (ret < 0) {
		wl1271_warning("CONFIGURE command NOK");
		return ret;
	}

	return ret;
}

/*
 * wrapper for wlcore_cmd_configure that accepts only success status.
 * return 0 on success
 */
int wl1271_cmd_configure(struct wl1271 *wl, u16 id, void *buf, size_t len)
{
	int ret = wlcore_cmd_configure_failsafe(wl, id, buf, len, 0);

	if (ret < 0)
		return ret;
	return 0;
}
EXPORT_SYMBOL_GPL(wl1271_cmd_configure);

int wl1271_cmd_data_path(struct wl1271 *wl, bool enable)
{
	struct cmd_enabledisable_path *cmd;
	int ret;
	u16 cmd_rx, cmd_tx;

	wl1271_debug(DEBUG_CMD, "cmd data path");

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (!cmd) {
		ret = -ENOMEM;
		goto out;
	}

	/* the channel here is only used for calibration, so hardcoded to 1 */
	cmd->channel = 1;

	if (enable) {
		cmd_rx = CMD_ENABLE_RX;
		cmd_tx = CMD_ENABLE_TX;
	} else {
		cmd_rx = CMD_DISABLE_RX;
		cmd_tx = CMD_DISABLE_TX;
	}

	ret = wl1271_cmd_send(wl, cmd_rx, cmd, sizeof(*cmd), 0);
	if (ret < 0) {
		wl1271_error("rx %s cmd for channel %d failed",
			     enable ? "start" : "stop", cmd->channel);
		goto out;
	}

	wl1271_debug(DEBUG_BOOT, "rx %s cmd channel %d",
		     enable ? "start" : "stop", cmd->channel);

	ret = wl1271_cmd_send(wl, cmd_tx, cmd, sizeof(*cmd), 0);
	if (ret < 0) {
		wl1271_error("tx %s cmd for channel %d failed",
			     enable ? "start" : "stop", cmd->channel);
		goto out;
	}

	wl1271_debug(DEBUG_BOOT, "tx %s cmd channel %d",
		     enable ? "start" : "stop", cmd->channel);

out:
	kfree(cmd);
	return ret;
}
EXPORT_SYMBOL_GPL(wl1271_cmd_data_path);

int wl1271_cmd_ps_mode(struct wl1271 *wl, struct wl12xx_vif *wlvif,
		       u8 ps_mode, u16 auto_ps_timeout)
{
	struct wl1271_cmd_ps_params *ps_params = NULL;
	int ret = 0;

	wl1271_debug(DEBUG_CMD, "cmd set ps mode");

	ps_params = kzalloc(sizeof(*ps_params), GFP_KERNEL);
	if (!ps_params) {
		ret = -ENOMEM;
		goto out;
	}

	ps_params->role_id = wlvif->role_id;
	ps_params->ps_mode = ps_mode;
	ps_params->auto_ps_timeout = auto_ps_timeout;

	ret = wl1271_cmd_send(wl, CMD_SET_PS_MODE, ps_params,
			      sizeof(*ps_params), 0);
	if (ret < 0) {
		wl1271_error("cmd set_ps_mode failed");
		goto out;
	}

out:
	kfree(ps_params);
	return ret;
}

int wl1271_cmd_template_set(struct wl1271 *wl, u8 role_id,
			    u16 template_id, void *buf, size_t buf_len,
			    int index, u32 rates)
{
	struct wl1271_cmd_template_set *cmd;
	int ret = 0;

	wl1271_debug(DEBUG_CMD, "cmd template_set %d (role %d)",
		     template_id, role_id);

	WARN_ON(buf_len > WL1271_CMD_TEMPL_MAX_SIZE);
	buf_len = min_t(size_t, buf_len, WL1271_CMD_TEMPL_MAX_SIZE);

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (!cmd) {
		ret = -ENOMEM;
		goto out;
	}

	/* during initialization wlvif is NULL */
	cmd->role_id = role_id;
	cmd->len = cpu_to_le16(buf_len);
	cmd->template_type = template_id;
	cmd->enabled_rates = cpu_to_le32(rates);
	cmd->short_retry_limit = wl->conf.tx.tmpl_short_retry_limit;
	cmd->long_retry_limit = wl->conf.tx.tmpl_long_retry_limit;
	cmd->index = index;

	if (buf)
		memcpy(cmd->template_data, buf, buf_len);

	ret = wl1271_cmd_send(wl, CMD_SET_TEMPLATE, cmd, sizeof(*cmd), 0);
	if (ret < 0) {
		wl1271_warning("cmd set_template failed: %d", ret);
		goto out_free;
	}

out_free:
	kfree(cmd);

out:
	return ret;
}

int wl12xx_cmd_build_null_data(struct wl1271 *wl, struct wl12xx_vif *wlvif)
{
	struct sk_buff *skb = NULL;
	int size;
	void *ptr;
	int ret = -ENOMEM;


	if (wlvif->bss_type == BSS_TYPE_IBSS) {
		size = sizeof(struct wl12xx_null_data_template);
		ptr = NULL;
	} else {
		skb = ieee80211_nullfunc_get(wl->hw,
					     wl12xx_wlvif_to_vif(wlvif),
					     -1, false);
		if (!skb)
			goto out;
		size = skb->len;
		ptr = skb->data;
	}

	ret = wl1271_cmd_template_set(wl, wlvif->role_id,
				      CMD_TEMPL_NULL_DATA, ptr, size, 0,
				      wlvif->basic_rate);

out:
	dev_kfree_skb(skb);
	if (ret)
		wl1271_warning("cmd build null data failed %d", ret);

	return ret;

}

int wl12xx_cmd_build_klv_null_data(struct wl1271 *wl,
				   struct wl12xx_vif *wlvif)
{
	struct ieee80211_vif *vif = wl12xx_wlvif_to_vif(wlvif);
	struct sk_buff *skb = NULL;
	int ret = -ENOMEM;

	skb = ieee80211_nullfunc_get(wl->hw, vif,-1, false);
	if (!skb)
		goto out;

	ret = wl1271_cmd_template_set(wl, wlvif->role_id, CMD_TEMPL_KLV,
				      skb->data, skb->len,
				      wlvif->sta.klv_template_id,
				      wlvif->basic_rate);

out:
	dev_kfree_skb(skb);
	if (ret)
		wl1271_warning("cmd build klv null data failed %d", ret);

	return ret;

}

int wl1271_cmd_build_ps_poll(struct wl1271 *wl, struct wl12xx_vif *wlvif,
			     u16 aid)
{
	struct ieee80211_vif *vif = wl12xx_wlvif_to_vif(wlvif);
	struct sk_buff *skb;
	int ret = 0;

	skb = ieee80211_pspoll_get(wl->hw, vif);
	if (!skb)
		goto out;

	ret = wl1271_cmd_template_set(wl, wlvif->role_id,
				      CMD_TEMPL_PS_POLL, skb->data,
				      skb->len, 0, wlvif->basic_rate_set);

out:
	dev_kfree_skb(skb);
	return ret;
}

int wl12xx_cmd_build_probe_req(struct wl1271 *wl, struct wl12xx_vif *wlvif,
			       u8 role_id, u8 band,
			       const u8 *ssid, size_t ssid_len,
			       const u8 *ie0, size_t ie0_len, const u8 *ie1,
			       size_t ie1_len, bool sched_scan)
{
	struct ieee80211_vif *vif = wl12xx_wlvif_to_vif(wlvif);
	struct sk_buff *skb;
	int ret;
	u32 rate;
	u16 template_id_2_4 = wl->scan_templ_id_2_4;
	u16 template_id_5 = wl->scan_templ_id_5;

	wl1271_debug(DEBUG_SCAN, "build probe request band %d", band);

	skb = ieee80211_probereq_get(wl->hw, vif->addr, ssid, ssid_len,
				     ie0_len + ie1_len);
	if (!skb) {
		ret = -ENOMEM;
		goto out;
	}
	if (ie0_len)
		skb_put_data(skb, ie0, ie0_len);
	if (ie1_len)
		skb_put_data(skb, ie1, ie1_len);

	if (sched_scan &&
	    (wl->quirks & WLCORE_QUIRK_DUAL_PROBE_TMPL)) {
		template_id_2_4 = wl->sched_scan_templ_id_2_4;
		template_id_5 = wl->sched_scan_templ_id_5;
	}

	rate = wl1271_tx_min_rate_get(wl, wlvif->bitrate_masks[band]);
	if (band == NL80211_BAND_2GHZ)
		ret = wl1271_cmd_template_set(wl, role_id,
					      template_id_2_4,
					      skb->data, skb->len, 0, rate);
	else
		ret = wl1271_cmd_template_set(wl, role_id,
					      template_id_5,
					      skb->data, skb->len, 0, rate);

out:
	dev_kfree_skb(skb);
	return ret;
}
EXPORT_SYMBOL_GPL(wl12xx_cmd_build_probe_req);

struct sk_buff *wl1271_cmd_build_ap_probe_req(struct wl1271 *wl,
					      struct wl12xx_vif *wlvif,
					      struct sk_buff *skb)
{
	struct ieee80211_vif *vif = wl12xx_wlvif_to_vif(wlvif);
	int ret;
	u32 rate;

	if (!skb)
		skb = ieee80211_ap_probereq_get(wl->hw, vif);
	if (!skb)
		goto out;

	wl1271_debug(DEBUG_SCAN, "set ap probe request template");

	rate = wl1271_tx_min_rate_get(wl, wlvif->bitrate_masks[wlvif->band]);
	if (wlvif->band == NL80211_BAND_2GHZ)
		ret = wl1271_cmd_template_set(wl, wlvif->role_id,
					      CMD_TEMPL_CFG_PROBE_REQ_2_4,
					      skb->data, skb->len, 0, rate);
	else
		ret = wl1271_cmd_template_set(wl, wlvif->role_id,
					      CMD_TEMPL_CFG_PROBE_REQ_5,
					      skb->data, skb->len, 0, rate);

	if (ret < 0)
		wl1271_error("Unable to set ap probe request template.");

out:
	return skb;
}

int wl1271_cmd_build_arp_rsp(struct wl1271 *wl, struct wl12xx_vif *wlvif)
{
	int ret, extra = 0;
	u16 fc;
	struct ieee80211_vif *vif = wl12xx_wlvif_to_vif(wlvif);
	struct sk_buff *skb;
	struct wl12xx_arp_rsp_template *tmpl;
	struct ieee80211_hdr_3addr *hdr;
	struct arphdr *arp_hdr;

	skb = dev_alloc_skb(sizeof(*hdr) + sizeof(__le16) + sizeof(*tmpl) +
			    WL1271_EXTRA_SPACE_MAX);
	if (!skb) {
		wl1271_error("failed to allocate buffer for arp rsp template");
		return -ENOMEM;
	}

	skb_reserve(skb, sizeof(*hdr) + WL1271_EXTRA_SPACE_MAX);

	tmpl = skb_put_zero(skb, sizeof(*tmpl));

	/* llc layer */
	memcpy(tmpl->llc_hdr, rfc1042_header, sizeof(rfc1042_header));
	tmpl->llc_type = cpu_to_be16(ETH_P_ARP);

	/* arp header */
	arp_hdr = &tmpl->arp_hdr;
	arp_hdr->ar_hrd = cpu_to_be16(ARPHRD_ETHER);
	arp_hdr->ar_pro = cpu_to_be16(ETH_P_IP);
	arp_hdr->ar_hln = ETH_ALEN;
	arp_hdr->ar_pln = 4;
	arp_hdr->ar_op = cpu_to_be16(ARPOP_REPLY);

	/* arp payload */
	memcpy(tmpl->sender_hw, vif->addr, ETH_ALEN);
	tmpl->sender_ip = wlvif->ip_addr;

	/* encryption space */
	switch (wlvif->encryption_type) {
	case KEY_TKIP:
		if (wl->quirks & WLCORE_QUIRK_TKIP_HEADER_SPACE)
			extra = WL1271_EXTRA_SPACE_TKIP;
		break;
	case KEY_AES:
		extra = WL1271_EXTRA_SPACE_AES;
		break;
	case KEY_NONE:
	case KEY_WEP:
	case KEY_GEM:
		extra = 0;
		break;
	default:
		wl1271_warning("Unknown encryption type: %d",
			       wlvif->encryption_type);
		ret = -EINVAL;
		goto out;
	}

	if (extra) {
		u8 *space = skb_push(skb, extra);
		memset(space, 0, extra);
	}

	/* QoS header - BE */
	if (wlvif->sta.qos)
		memset(skb_push(skb, sizeof(__le16)), 0, sizeof(__le16));

	/* mac80211 header */
	hdr = skb_push(skb, sizeof(*hdr));
	memset(hdr, 0, sizeof(*hdr));
	fc = IEEE80211_FTYPE_DATA | IEEE80211_FCTL_TODS;
	if (wlvif->sta.qos)
		fc |= IEEE80211_STYPE_QOS_DATA;
	else
		fc |= IEEE80211_STYPE_DATA;
	if (wlvif->encryption_type != KEY_NONE)
		fc |= IEEE80211_FCTL_PROTECTED;

	hdr->frame_control = cpu_to_le16(fc);
	memcpy(hdr->addr1, vif->bss_conf.bssid, ETH_ALEN);
	memcpy(hdr->addr2, vif->addr, ETH_ALEN);
	eth_broadcast_addr(hdr->addr3);

	ret = wl1271_cmd_template_set(wl, wlvif->role_id, CMD_TEMPL_ARP_RSP,
				      skb->data, skb->len, 0,
				      wlvif->basic_rate);
out:
	dev_kfree_skb(skb);
	return ret;
}

int wl1271_build_qos_null_data(struct wl1271 *wl, struct ieee80211_vif *vif)
{
	struct wl12xx_vif *wlvif = wl12xx_vif_to_data(vif);
	struct ieee80211_qos_hdr template;

	memset(&template, 0, sizeof(template));

	memcpy(template.addr1, vif->bss_conf.bssid, ETH_ALEN);
	memcpy(template.addr2, vif->addr, ETH_ALEN);
	memcpy(template.addr3, vif->bss_conf.bssid, ETH_ALEN);

	template.frame_control = cpu_to_le16(IEEE80211_FTYPE_DATA |
					     IEEE80211_STYPE_QOS_NULLFUNC |
					     IEEE80211_FCTL_TODS);

	/* FIXME: not sure what priority to use here */
	template.qos_ctrl = cpu_to_le16(0);

	return wl1271_cmd_template_set(wl, wlvif->role_id,
				       CMD_TEMPL_QOS_NULL_DATA, &template,
				       sizeof(template), 0,
				       wlvif->basic_rate);
}

int wl12xx_cmd_set_default_wep_key(struct wl1271 *wl, u8 id, u8 hlid)
{
	struct wl1271_cmd_set_keys *cmd;
	int ret = 0;

	wl1271_debug(DEBUG_CMD, "cmd set_default_wep_key %d", id);

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (!cmd) {
		ret = -ENOMEM;
		goto out;
	}

	cmd->hlid = hlid;
	cmd->key_id = id;
	cmd->lid_key_type = WEP_DEFAULT_LID_TYPE;
	cmd->key_action = cpu_to_le16(KEY_SET_ID);
	cmd->key_type = KEY_WEP;

	ret = wl1271_cmd_send(wl, CMD_SET_KEYS, cmd, sizeof(*cmd), 0);
	if (ret < 0) {
		wl1271_warning("cmd set_default_wep_key failed: %d", ret);
		goto out;
	}

out:
	kfree(cmd);

	return ret;
}

int wl1271_cmd_set_sta_key(struct wl1271 *wl, struct wl12xx_vif *wlvif,
		       u16 action, u8 id, u8 key_type,
		       u8 key_size, const u8 *key, const u8 *addr,
		       u32 tx_seq_32, u16 tx_seq_16)
{
	struct wl1271_cmd_set_keys *cmd;
	int ret = 0;

	/* hlid might have already been deleted */
	if (wlvif->sta.hlid == WL12XX_INVALID_LINK_ID)
		return 0;

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (!cmd) {
		ret = -ENOMEM;
		goto out;
	}

	cmd->hlid = wlvif->sta.hlid;

	if (key_type == KEY_WEP)
		cmd->lid_key_type = WEP_DEFAULT_LID_TYPE;
	else if (is_broadcast_ether_addr(addr))
		cmd->lid_key_type = BROADCAST_LID_TYPE;
	else
		cmd->lid_key_type = UNICAST_LID_TYPE;

	cmd->key_action = cpu_to_le16(action);
	cmd->key_size = key_size;
	cmd->key_type = key_type;

	cmd->ac_seq_num16[0] = cpu_to_le16(tx_seq_16);
	cmd->ac_seq_num32[0] = cpu_to_le32(tx_seq_32);

	cmd->key_id = id;

	if (key_type == KEY_TKIP) {
		/*
		 * We get the key in the following form:
		 * TKIP (16 bytes) - TX MIC (8 bytes) - RX MIC (8 bytes)
		 * but the target is expecting:
		 * TKIP - RX MIC - TX MIC
		 */
		memcpy(cmd->key, key, 16);
		memcpy(cmd->key + 16, key + 24, 8);
		memcpy(cmd->key + 24, key + 16, 8);

	} else {
		memcpy(cmd->key, key, key_size);
	}

	wl1271_dump(DEBUG_CRYPT, "TARGET KEY: ", cmd, sizeof(*cmd));

	ret = wl1271_cmd_send(wl, CMD_SET_KEYS, cmd, sizeof(*cmd), 0);
	if (ret < 0) {
		wl1271_warning("could not set keys");
		goto out;
	}

out:
	kfree(cmd);

	return ret;
}

/*
 * TODO: merge with sta/ibss into 1 set_key function.
 * note there are slight diffs
 */
int wl1271_cmd_set_ap_key(struct wl1271 *wl, struct wl12xx_vif *wlvif,
			  u16 action, u8 id, u8 key_type,
			  u8 key_size, const u8 *key, u8 hlid, u32 tx_seq_32,
			  u16 tx_seq_16, bool is_pairwise)
{
	struct wl1271_cmd_set_keys *cmd;
	int ret = 0;
	u8 lid_type;

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (!cmd)
		return -ENOMEM;

	if (hlid == wlvif->ap.bcast_hlid) {
		if (key_type == KEY_WEP)
			lid_type = WEP_DEFAULT_LID_TYPE;
		else
			lid_type = BROADCAST_LID_TYPE;
	} else if (is_pairwise) {
		lid_type = UNICAST_LID_TYPE;
	} else {
		lid_type = BROADCAST_LID_TYPE;
	}

	wl1271_debug(DEBUG_CRYPT, "ap key action: %d id: %d lid: %d type: %d"
		     " hlid: %d", (int)action, (int)id, (int)lid_type,
		     (int)key_type, (int)hlid);

	cmd->lid_key_type = lid_type;
	cmd->hlid = hlid;
	cmd->key_action = cpu_to_le16(action);
	cmd->key_size = key_size;
	cmd->key_type = key_type;
	cmd->key_id = id;
	cmd->ac_seq_num16[0] = cpu_to_le16(tx_seq_16);
	cmd->ac_seq_num32[0] = cpu_to_le32(tx_seq_32);

	if (key_type == KEY_TKIP) {
		/*
		 * We get the key in the following form:
		 * TKIP (16 bytes) - TX MIC (8 bytes) - RX MIC (8 bytes)
		 * but the target is expecting:
		 * TKIP - RX MIC - TX MIC
		 */
		memcpy(cmd->key, key, 16);
		memcpy(cmd->key + 16, key + 24, 8);
		memcpy(cmd->key + 24, key + 16, 8);
	} else {
		memcpy(cmd->key, key, key_size);
	}

	wl1271_dump(DEBUG_CRYPT, "TARGET AP KEY: ", cmd, sizeof(*cmd));

	ret = wl1271_cmd_send(wl, CMD_SET_KEYS, cmd, sizeof(*cmd), 0);
	if (ret < 0) {
		wl1271_warning("could not set ap keys");
		goto out;
	}

out:
	kfree(cmd);
	return ret;
}

int wl12xx_cmd_set_peer_state(struct wl1271 *wl, struct wl12xx_vif *wlvif,
			      u8 hlid)
{
	struct wl12xx_cmd_set_peer_state *cmd;
	int ret = 0;

	wl1271_debug(DEBUG_CMD, "cmd set peer state (hlid=%d)", hlid);

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (!cmd) {
		ret = -ENOMEM;
		goto out;
	}

	cmd->hlid = hlid;
	cmd->state = WL1271_CMD_STA_STATE_CONNECTED;

	/* wmm param is valid only for station role */
	if (wlvif->bss_type == BSS_TYPE_STA_BSS)
		cmd->wmm = wlvif->wmm_enabled;

	ret = wl1271_cmd_send(wl, CMD_SET_PEER_STATE, cmd, sizeof(*cmd), 0);
	if (ret < 0) {
		wl1271_error("failed to send set peer state command");
		goto out_free;
	}

out_free:
	kfree(cmd);

out:
	return ret;
}

int wl12xx_cmd_add_peer(struct wl1271 *wl, struct wl12xx_vif *wlvif,
			struct ieee80211_sta *sta, u8 hlid)
{
	struct wl12xx_cmd_add_peer *cmd;
	int i, ret;
	u32 sta_rates;

	wl1271_debug(DEBUG_CMD, "cmd add peer %d", (int)hlid);

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (!cmd) {
		ret = -ENOMEM;
		goto out;
	}

	memcpy(cmd->addr, sta->addr, ETH_ALEN);
	cmd->bss_index = WL1271_AP_BSS_INDEX;
	cmd->aid = sta->aid;
	cmd->hlid = hlid;
	cmd->sp_len = sta->max_sp;
	cmd->wmm = sta->wme ? 1 : 0;
	cmd->session_id = wl->session_ids[hlid];
	cmd->role_id = wlvif->role_id;

	for (i = 0; i < NUM_ACCESS_CATEGORIES_COPY; i++)
		if (sta->wme && (sta->uapsd_queues & BIT(i)))
			cmd->psd_type[NUM_ACCESS_CATEGORIES_COPY-1-i] =
					WL1271_PSD_UPSD_TRIGGER;
		else
			cmd->psd_type[NUM_ACCESS_CATEGORIES_COPY-1-i] =
					WL1271_PSD_LEGACY;


	sta_rates = sta->deflink.supp_rates[wlvif->band];
	if (sta->deflink.ht_cap.ht_supported)
		sta_rates |=
			(sta->deflink.ht_cap.mcs.rx_mask[0] << HW_HT_RATES_OFFSET) |
			(sta->deflink.ht_cap.mcs.rx_mask[1] << HW_MIMO_RATES_OFFSET);

	cmd->supported_rates =
		cpu_to_le32(wl1271_tx_enabled_rates_get(wl, sta_rates,
							wlvif->band));

	wl1271_debug(DEBUG_CMD, "new peer rates=0x%x queues=0x%x",
		     cmd->supported_rates, sta->uapsd_queues);

	ret = wl1271_cmd_send(wl, CMD_ADD_PEER, cmd, sizeof(*cmd), 0);
	if (ret < 0) {
		wl1271_error("failed to initiate cmd add peer");
		goto out_free;
	}

out_free:
	kfree(cmd);

out:
	return ret;
}

int wl12xx_cmd_remove_peer(struct wl1271 *wl, struct wl12xx_vif *wlvif,
			   u8 hlid)
{
	struct wl12xx_cmd_remove_peer *cmd;
	int ret;
	bool timeout = false;

	wl1271_debug(DEBUG_CMD, "cmd remove peer %d", (int)hlid);

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (!cmd) {
		ret = -ENOMEM;
		goto out;
	}

	cmd->hlid = hlid;
	/* We never send a deauth, mac80211 is in charge of this */
	cmd->reason_opcode = 0;
	cmd->send_deauth_flag = 0;
	cmd->role_id = wlvif->role_id;

	ret = wl1271_cmd_send(wl, CMD_REMOVE_PEER, cmd, sizeof(*cmd), 0);
	if (ret < 0) {
		wl1271_error("failed to initiate cmd remove peer");
		goto out_free;
	}

	ret = wl->ops->wait_for_event(wl,
				      WLCORE_EVENT_PEER_REMOVE_COMPLETE,
				      &timeout);

	/*
	 * We are ok with a timeout here. The event is sometimes not sent
	 * due to a firmware bug. In case of another error (like SDIO timeout)
	 * queue a recovery.
	 */
	if (ret)
		wl12xx_queue_recovery_work(wl);

out_free:
	kfree(cmd);

out:
	return ret;
}

static int wlcore_get_reg_conf_ch_idx(enum nl80211_band band, u16 ch)
{
	/*
	 * map the given band/channel to the respective predefined
	 * bit expected by the fw
	 */
	switch (band) {
	case NL80211_BAND_2GHZ:
		/* channels 1..14 are mapped to 0..13 */
		if (ch >= 1 && ch <= 14)
			return ch - 1;
		break;
	case NL80211_BAND_5GHZ:
		switch (ch) {
		case 8 ... 16:
			/* channels 8,12,16 are mapped to 18,19,20 */
			return 18 + (ch-8)/4;
		case 34 ... 48:
			/* channels 34,36..48 are mapped to 21..28 */
			return 21 + (ch-34)/2;
		case 52 ... 64:
			/* channels 52,56..64 are mapped to 29..32 */
			return 29 + (ch-52)/4;
		case 100 ... 140:
			/* channels 100,104..140 are mapped to 33..43 */
			return 33 + (ch-100)/4;
		case 149 ... 165:
			/* channels 149,153..165 are mapped to 44..48 */
			return 44 + (ch-149)/4;
		default:
			break;
		}
		break;
	default:
		break;
	}

	wl1271_error("%s: unknown band/channel: %d/%d", __func__, band, ch);
	return -1;
}

void wlcore_set_pending_regdomain_ch(struct wl1271 *wl, u16 channel,
				     enum nl80211_band band)
{
	int ch_bit_idx = 0;

	if (!(wl->quirks & WLCORE_QUIRK_REGDOMAIN_CONF))
		return;

	ch_bit_idx = wlcore_get_reg_conf_ch_idx(band, channel);

	if (ch_bit_idx >= 0 && ch_bit_idx <= WL1271_MAX_CHANNELS)
		__set_bit_le(ch_bit_idx, (long *)wl->reg_ch_conf_pending);
}

int wlcore_cmd_regdomain_config_locked(struct wl1271 *wl)
{
	struct wl12xx_cmd_regdomain_dfs_config *cmd = NULL;
	int ret = 0, i, b, ch_bit_idx;
	__le32 tmp_ch_bitmap[2] __aligned(sizeof(unsigned long));
	struct wiphy *wiphy = wl->hw->wiphy;
	struct ieee80211_supported_band *band;
	bool timeout = false;

	if (!(wl->quirks & WLCORE_QUIRK_REGDOMAIN_CONF))
		return 0;

	wl1271_debug(DEBUG_CMD, "cmd reg domain config");

	memcpy(tmp_ch_bitmap, wl->reg_ch_conf_pending, sizeof(tmp_ch_bitmap));

	for (b = NL80211_BAND_2GHZ; b <= NL80211_BAND_5GHZ; b++) {
		band = wiphy->bands[b];
		for (i = 0; i < band->n_channels; i++) {
			struct ieee80211_channel *channel = &band->channels[i];
			u16 ch = channel->hw_value;
			u32 flags = channel->flags;

			if (flags & (IEEE80211_CHAN_DISABLED |
				     IEEE80211_CHAN_NO_IR))
				continue;

			if ((flags & IEEE80211_CHAN_RADAR) &&
			    channel->dfs_state != NL80211_DFS_AVAILABLE)
				continue;

			ch_bit_idx = wlcore_get_reg_conf_ch_idx(b, ch);
			if (ch_bit_idx < 0)
				continue;

			__set_bit_le(ch_bit_idx, (long *)tmp_ch_bitmap);
		}
	}

	if (!memcmp(tmp_ch_bitmap, wl->reg_ch_conf_last, sizeof(tmp_ch_bitmap)))
		goto out;

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (!cmd) {
		ret = -ENOMEM;
		goto out;
	}

	cmd->ch_bit_map1 = tmp_ch_bitmap[0];
	cmd->ch_bit_map2 = tmp_ch_bitmap[1];
	cmd->dfs_region = wl->dfs_region;

	wl1271_debug(DEBUG_CMD,
		     "cmd reg domain bitmap1: 0x%08x, bitmap2: 0x%08x",
		     cmd->ch_bit_map1, cmd->ch_bit_map2);

	ret = wl1271_cmd_send(wl, CMD_DFS_CHANNEL_CONFIG, cmd, sizeof(*cmd), 0);
	if (ret < 0) {
		wl1271_error("failed to send reg domain dfs config");
		goto out;
	}

	ret = wl->ops->wait_for_event(wl,
				      WLCORE_EVENT_DFS_CONFIG_COMPLETE,
				      &timeout);
	if (ret < 0 || timeout) {
		wl1271_error("reg domain conf %serror",
			     timeout ? "completion " : "");
		ret = timeout ? -ETIMEDOUT : ret;
		goto out;
	}

	memcpy(wl->reg_ch_conf_last, tmp_ch_bitmap, sizeof(tmp_ch_bitmap));
	memset(wl->reg_ch_conf_pending, 0, sizeof(wl->reg_ch_conf_pending));

out:
	kfree(cmd);
	return ret;
}

int wl12xx_cmd_config_fwlog(struct wl1271 *wl)
{
	struct wl12xx_cmd_config_fwlog *cmd;
	int ret = 0;

	wl1271_debug(DEBUG_CMD, "cmd config firmware logger");

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (!cmd) {
		ret = -ENOMEM;
		goto out;
	}

	cmd->logger_mode = wl->conf.fwlog.mode;
	cmd->log_severity = wl->conf.fwlog.severity;
	cmd->timestamp = wl->conf.fwlog.timestamp;
	cmd->output = wl->conf.fwlog.output;
	cmd->threshold = wl->conf.fwlog.threshold;

	ret = wl1271_cmd_send(wl, CMD_CONFIG_FWLOGGER, cmd, sizeof(*cmd), 0);
	if (ret < 0) {
		wl1271_error("failed to send config firmware logger command");
		goto out_free;
	}

out_free:
	kfree(cmd);

out:
	return ret;
}

int wl12xx_cmd_start_fwlog(struct wl1271 *wl)
{
	struct wl12xx_cmd_start_fwlog *cmd;
	int ret = 0;

	wl1271_debug(DEBUG_CMD, "cmd start firmware logger");

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (!cmd) {
		ret = -ENOMEM;
		goto out;
	}

	ret = wl1271_cmd_send(wl, CMD_START_FWLOGGER, cmd, sizeof(*cmd), 0);
	if (ret < 0) {
		wl1271_error("failed to send start firmware logger command");
		goto out_free;
	}

out_free:
	kfree(cmd);

out:
	return ret;
}

int wl12xx_cmd_stop_fwlog(struct wl1271 *wl)
{
	struct wl12xx_cmd_stop_fwlog *cmd;
	int ret = 0;

	wl1271_debug(DEBUG_CMD, "cmd stop firmware logger");

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (!cmd) {
		ret = -ENOMEM;
		goto out;
	}

	ret = wl1271_cmd_send(wl, CMD_STOP_FWLOGGER, cmd, sizeof(*cmd), 0);
	if (ret < 0) {
		wl1271_error("failed to send stop firmware logger command");
		goto out_free;
	}

out_free:
	kfree(cmd);

out:
	return ret;
}

static int wl12xx_cmd_roc(struct wl1271 *wl, struct wl12xx_vif *wlvif,
			  u8 role_id, enum nl80211_band band, u8 channel)
{
	struct wl12xx_cmd_roc *cmd;
	int ret = 0;

	wl1271_debug(DEBUG_CMD, "cmd roc %d (%d)", channel, role_id);

	if (WARN_ON(role_id == WL12XX_INVALID_ROLE_ID))
		return -EINVAL;

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (!cmd) {
		ret = -ENOMEM;
		goto out;
	}

	cmd->role_id = role_id;
	cmd->channel = channel;
	switch (band) {
	case NL80211_BAND_2GHZ:
		cmd->band = WLCORE_BAND_2_4GHZ;
		break;
	case NL80211_BAND_5GHZ:
		cmd->band = WLCORE_BAND_5GHZ;
		break;
	default:
		wl1271_error("roc - unknown band: %d", (int)wlvif->band);
		ret = -EINVAL;
		goto out_free;
	}


	ret = wl1271_cmd_send(wl, CMD_REMAIN_ON_CHANNEL, cmd, sizeof(*cmd), 0);
	if (ret < 0) {
		wl1271_error("failed to send ROC command");
		goto out_free;
	}

out_free:
	kfree(cmd);

out:
	return ret;
}

static int wl12xx_cmd_croc(struct wl1271 *wl, u8 role_id)
{
	struct wl12xx_cmd_croc *cmd;
	int ret = 0;

	wl1271_debug(DEBUG_CMD, "cmd croc (%d)", role_id);

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (!cmd) {
		ret = -ENOMEM;
		goto out;
	}
	cmd->role_id = role_id;

	ret = wl1271_cmd_send(wl, CMD_CANCEL_REMAIN_ON_CHANNEL, cmd,
			      sizeof(*cmd), 0);
	if (ret < 0) {
		wl1271_error("failed to send ROC command");
		goto out_free;
	}

out_free:
	kfree(cmd);

out:
	return ret;
}

int wl12xx_roc(struct wl1271 *wl, struct wl12xx_vif *wlvif, u8 role_id,
	       enum nl80211_band band, u8 channel)
{
	int ret = 0;

	if (WARN_ON(test_bit(role_id, wl->roc_map)))
		return 0;

	ret = wl12xx_cmd_roc(wl, wlvif, role_id, band, channel);
	if (ret < 0)
		goto out;

	__set_bit(role_id, wl->roc_map);
out:
	return ret;
}

int wl12xx_croc(struct wl1271 *wl, u8 role_id)
{
	int ret = 0;

	if (WARN_ON(!test_bit(role_id, wl->roc_map)))
		return 0;

	ret = wl12xx_cmd_croc(wl, role_id);
	if (ret < 0)
		goto out;

	__clear_bit(role_id, wl->roc_map);

	/*
	 * Rearm the tx watchdog when removing the last ROC. This prevents
	 * recoveries due to just finished ROCs - when Tx hasn't yet had
	 * a chance to get out.
	 */
	if (find_first_bit(wl->roc_map, WL12XX_MAX_ROLES) >= WL12XX_MAX_ROLES)
		wl12xx_rearm_tx_watchdog_locked(wl);
out:
	return ret;
}

int wl12xx_cmd_stop_channel_switch(struct wl1271 *wl, struct wl12xx_vif *wlvif)
{
	struct wl12xx_cmd_stop_channel_switch *cmd;
	int ret;

	wl1271_debug(DEBUG_ACX, "cmd stop channel switch");

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (!cmd) {
		ret = -ENOMEM;
		goto out;
	}

	cmd->role_id = wlvif->role_id;

	ret = wl1271_cmd_send(wl, CMD_STOP_CHANNEL_SWICTH, cmd, sizeof(*cmd), 0);
	if (ret < 0) {
		wl1271_error("failed to stop channel switch command");
		goto out_free;
	}

out_free:
	kfree(cmd);

out:
	return ret;
}

/* start dev role and roc on its channel */
int wl12xx_start_dev(struct wl1271 *wl, struct wl12xx_vif *wlvif,
		     enum nl80211_band band, int channel)
{
	int ret;

	if (WARN_ON(!(wlvif->bss_type == BSS_TYPE_STA_BSS ||
		      wlvif->bss_type == BSS_TYPE_IBSS)))
		return -EINVAL;

	/* the dev role is already started for p2p mgmt interfaces */
	if (!wlcore_is_p2p_mgmt(wlvif)) {
		ret = wl12xx_cmd_role_enable(wl,
					     wl12xx_wlvif_to_vif(wlvif)->addr,
					     WL1271_ROLE_DEVICE,
					     &wlvif->dev_role_id);
		if (ret < 0)
			goto out;
	}

	ret = wl12xx_cmd_role_start_dev(wl, wlvif, band, channel);
	if (ret < 0)
		goto out_disable;

	ret = wl12xx_roc(wl, wlvif, wlvif->dev_role_id, band, channel);
	if (ret < 0)
		goto out_stop;

	return 0;

out_stop:
	wl12xx_cmd_role_stop_dev(wl, wlvif);
out_disable:
	if (!wlcore_is_p2p_mgmt(wlvif))
		wl12xx_cmd_role_disable(wl, &wlvif->dev_role_id);
out:
	return ret;
}

/* croc dev hlid, and stop the role */
int wl12xx_stop_dev(struct wl1271 *wl, struct wl12xx_vif *wlvif)
{
	int ret;

	if (WARN_ON(!(wlvif->bss_type == BSS_TYPE_STA_BSS ||
		      wlvif->bss_type == BSS_TYPE_IBSS)))
		return -EINVAL;

	/* flush all pending packets */
	ret = wlcore_tx_work_locked(wl);
	if (ret < 0)
		goto out;

	if (test_bit(wlvif->dev_role_id, wl->roc_map)) {
		ret = wl12xx_croc(wl, wlvif->dev_role_id);
		if (ret < 0)
			goto out;
	}

	ret = wl12xx_cmd_role_stop_dev(wl, wlvif);
	if (ret < 0)
		goto out;

	if (!wlcore_is_p2p_mgmt(wlvif)) {
		ret = wl12xx_cmd_role_disable(wl, &wlvif->dev_role_id);
		if (ret < 0)
			goto out;
	}

out:
	return ret;
}

int wlcore_cmd_generic_cfg(struct wl1271 *wl, struct wl12xx_vif *wlvif,
			   u8 feature, u8 enable, u8 value)
{
	struct wlcore_cmd_generic_cfg *cmd;
	int ret;

	wl1271_debug(DEBUG_CMD,
		     "cmd generic cfg (role %d feature %d enable %d value %d)",
		     wlvif->role_id, feature, enable, value);

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (!cmd)
		return -ENOMEM;

	cmd->role_id = wlvif->role_id;
	cmd->feature = feature;
	cmd->enable = enable;
	cmd->value = value;

	ret = wl1271_cmd_send(wl, CMD_GENERIC_CFG, cmd, sizeof(*cmd), 0);
	if (ret < 0) {
		wl1271_error("failed to send generic cfg command");
		goto out_free;
	}
out_free:
	kfree(cmd);
	return ret;
}
EXPORT_SYMBOL_GPL(wlcore_cmd_generic_cfg);

// SPDX-License-Identifier: GPL-2.0-only
/*
 * This file contains the major functions in WLAN
 * driver. It includes init, exit, open, close and main
 * thread etc..
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/etherdevice.h>
#include <linux/hardirq.h>
#include <linux/netdevice.h>
#include <linux/if_arp.h>
#include <linux/kthread.h>
#include <linux/kfifo.h>
#include <linux/slab.h>
#include <net/cfg80211.h>

#include "host.h"
#include "decl.h"
#include "dev.h"
#include "cfg.h"
#include "debugfs.h"
#include "cmd.h"
#include "mesh.h"

#define DRIVER_RELEASE_VERSION "323.p0"
const char lbs_driver_version[] = "COMM-USB8388-" DRIVER_RELEASE_VERSION
#ifdef  DEBUG
    "-dbg"
#endif
    "";


/* Module parameters */
unsigned int lbs_debug;
EXPORT_SYMBOL_GPL(lbs_debug);
module_param_named(libertas_debug, lbs_debug, int, 0644);

unsigned int lbs_disablemesh;
EXPORT_SYMBOL_GPL(lbs_disablemesh);
module_param_named(libertas_disablemesh, lbs_disablemesh, int, 0644);


/*
 * This global structure is used to send the confirm_sleep command as
 * fast as possible down to the firmware.
 */
struct cmd_confirm_sleep confirm_sleep;


/*
 * the table to keep region code
 */
u16 lbs_region_code_to_index[MRVDRV_MAX_REGION_CODE] =
    { 0x10, 0x20, 0x30, 0x31, 0x32, 0x40 };

/*
 * FW rate table.  FW refers to rates by their index in this table, not by the
 * rate value itself.  Values of 0x00 are
 * reserved positions.
 */
static u8 fw_data_rates[MAX_RATES] =
    { 0x02, 0x04, 0x0B, 0x16, 0x00, 0x0C, 0x12,
      0x18, 0x24, 0x30, 0x48, 0x60, 0x6C, 0x00
};

/**
 *  lbs_fw_index_to_data_rate - use index to get the data rate
 *
 *  @idx:	The index of data rate
 *  returns:	data rate or 0
 */
u32 lbs_fw_index_to_data_rate(u8 idx)
{
	if (idx >= sizeof(fw_data_rates))
		idx = 0;
	return fw_data_rates[idx];
}

/**
 *  lbs_data_rate_to_fw_index - use rate to get the index
 *
 *  @rate:	data rate
 *  returns:	index or 0
 */
u8 lbs_data_rate_to_fw_index(u32 rate)
{
	u8 i;

	if (!rate)
		return 0;

	for (i = 0; i < sizeof(fw_data_rates); i++) {
		if (rate == fw_data_rates[i])
			return i;
	}
	return 0;
}

int lbs_set_iface_type(struct lbs_private *priv, enum nl80211_iftype type)
{
	int ret = 0;

	switch (type) {
	case NL80211_IFTYPE_MONITOR:
		ret = lbs_set_monitor_mode(priv, 1);
		break;
	case NL80211_IFTYPE_STATION:
		if (priv->wdev->iftype == NL80211_IFTYPE_MONITOR)
			ret = lbs_set_monitor_mode(priv, 0);
		if (!ret)
			ret = lbs_set_snmp_mib(priv, SNMP_MIB_OID_BSS_TYPE, 1);
		break;
	case NL80211_IFTYPE_ADHOC:
		if (priv->wdev->iftype == NL80211_IFTYPE_MONITOR)
			ret = lbs_set_monitor_mode(priv, 0);
		if (!ret)
			ret = lbs_set_snmp_mib(priv, SNMP_MIB_OID_BSS_TYPE, 2);
		break;
	default:
		ret = -ENOTSUPP;
	}
	return ret;
}

int lbs_start_iface(struct lbs_private *priv)
{
	struct cmd_ds_802_11_mac_address cmd;
	int ret;

	if (priv->power_restore) {
		ret = priv->power_restore(priv);
		if (ret)
			return ret;
	}

	cmd.hdr.size = cpu_to_le16(sizeof(cmd));
	cmd.action = cpu_to_le16(CMD_ACT_SET);
	memcpy(cmd.macadd, priv->current_addr, ETH_ALEN);

	ret = lbs_cmd_with_response(priv, CMD_802_11_MAC_ADDRESS, &cmd);
	if (ret) {
		lbs_deb_net("set MAC address failed\n");
		goto err;
	}

	ret = lbs_set_iface_type(priv, priv->wdev->iftype);
	if (ret) {
		lbs_deb_net("set iface type failed\n");
		goto err;
	}

	ret = lbs_set_11d_domain_info(priv);
	if (ret) {
		lbs_deb_net("set 11d domain info failed\n");
		goto err;
	}

	lbs_update_channel(priv);

	priv->iface_running = true;
	return 0;

err:
	if (priv->power_save)
		priv->power_save(priv);
	return ret;
}

/**
 *  lbs_dev_open - open the ethX interface
 *
 *  @dev:	A pointer to &net_device structure
 *  returns:	0 or -EBUSY if monitor mode active
 */
static int lbs_dev_open(struct net_device *dev)
{
	struct lbs_private *priv = dev->ml_priv;
	int ret = 0;

	if (!priv->iface_running) {
		ret = lbs_start_iface(priv);
		if (ret)
			goto out;
	}

	spin_lock_irq(&priv->driver_lock);

	netif_carrier_off(dev);

	if (!priv->tx_pending_len)
		netif_wake_queue(dev);

	spin_unlock_irq(&priv->driver_lock);

out:
	return ret;
}

static bool lbs_command_queue_empty(struct lbs_private *priv)
{
	unsigned long flags;
	bool ret;
	spin_lock_irqsave(&priv->driver_lock, flags);
	ret = priv->cur_cmd == NULL && list_empty(&priv->cmdpendingq);
	spin_unlock_irqrestore(&priv->driver_lock, flags);
	return ret;
}

int lbs_stop_iface(struct lbs_private *priv)
{
	unsigned long flags;
	int ret = 0;

	spin_lock_irqsave(&priv->driver_lock, flags);
	priv->iface_running = false;
	kfree_skb(priv->currenttxskb);
	priv->currenttxskb = NULL;
	priv->tx_pending_len = 0;
	spin_unlock_irqrestore(&priv->driver_lock, flags);

	cancel_work_sync(&priv->mcast_work);
	del_timer_sync(&priv->tx_lockup_timer);

	/* Disable command processing, and wait for all commands to complete */
	lbs_deb_main("waiting for commands to complete\n");
	wait_event(priv->waitq, lbs_command_queue_empty(priv));
	lbs_deb_main("all commands completed\n");

	if (priv->power_save)
		ret = priv->power_save(priv);

	return ret;
}

/**
 *  lbs_eth_stop - close the ethX interface
 *
 *  @dev:	A pointer to &net_device structure
 *  returns:	0
 */
static int lbs_eth_stop(struct net_device *dev)
{
	struct lbs_private *priv = dev->ml_priv;

	if (priv->connect_status == LBS_CONNECTED)
		lbs_disconnect(priv, WLAN_REASON_DEAUTH_LEAVING);

	spin_lock_irq(&priv->driver_lock);
	netif_stop_queue(dev);
	spin_unlock_irq(&priv->driver_lock);

	lbs_update_mcast(priv);
	cancel_delayed_work_sync(&priv->scan_work);
	if (priv->scan_req)
		lbs_scan_done(priv);

	netif_carrier_off(priv->dev);

	if (!lbs_iface_active(priv))
		lbs_stop_iface(priv);

	return 0;
}

void lbs_host_to_card_done(struct lbs_private *priv)
{
	unsigned long flags;

	spin_lock_irqsave(&priv->driver_lock, flags);
	del_timer(&priv->tx_lockup_timer);

	priv->dnld_sent = DNLD_RES_RECEIVED;

	/* Wake main thread if commands are pending */
	if (!priv->cur_cmd || priv->tx_pending_len > 0) {
		if (!priv->wakeup_dev_required)
			wake_up(&priv->waitq);
	}

	spin_unlock_irqrestore(&priv->driver_lock, flags);
}
EXPORT_SYMBOL_GPL(lbs_host_to_card_done);

int lbs_set_mac_address(struct net_device *dev, void *addr)
{
	int ret = 0;
	struct lbs_private *priv = dev->ml_priv;
	struct sockaddr *phwaddr = addr;

	/*
	 * Can only set MAC address when all interfaces are down, to be written
	 * to the hardware when one of them is brought up.
	 */
	if (lbs_iface_active(priv))
		return -EBUSY;

	/* In case it was called from the mesh device */
	dev = priv->dev;

	memcpy(priv->current_addr, phwaddr->sa_data, ETH_ALEN);
	memcpy(dev->dev_addr, phwaddr->sa_data, ETH_ALEN);
	if (priv->mesh_dev)
		memcpy(priv->mesh_dev->dev_addr, phwaddr->sa_data, ETH_ALEN);

	return ret;
}


static inline int mac_in_list(unsigned char *list, int list_len,
			      unsigned char *mac)
{
	while (list_len) {
		if (!memcmp(list, mac, ETH_ALEN))
			return 1;
		list += ETH_ALEN;
		list_len--;
	}
	return 0;
}


static int lbs_add_mcast_addrs(struct cmd_ds_mac_multicast_adr *cmd,
			       struct net_device *dev, int nr_addrs)
{
	int i = nr_addrs;
	struct netdev_hw_addr *ha;
	int cnt;

	if ((dev->flags & (IFF_UP|IFF_MULTICAST)) != (IFF_UP|IFF_MULTICAST))
		return nr_addrs;

	netif_addr_lock_bh(dev);
	cnt = netdev_mc_count(dev);
	netdev_for_each_mc_addr(ha, dev) {
		if (mac_in_list(cmd->maclist, nr_addrs, ha->addr)) {
			lbs_deb_net("mcast address %s:%pM skipped\n", dev->name,
				    ha->addr);
			cnt--;
			continue;
		}

		if (i == MRVDRV_MAX_MULTICAST_LIST_SIZE)
			break;
		memcpy(&cmd->maclist[6*i], ha->addr, ETH_ALEN);
		lbs_deb_net("mcast address %s:%pM added to filter\n", dev->name,
			    ha->addr);
		i++;
		cnt--;
	}
	netif_addr_unlock_bh(dev);
	if (cnt)
		return -EOVERFLOW;

	return i;
}

void lbs_update_mcast(struct lbs_private *priv)
{
	struct cmd_ds_mac_multicast_adr mcast_cmd;
	int dev_flags = 0;
	int nr_addrs;
	int old_mac_control = priv->mac_control;

	if (netif_running(priv->dev))
		dev_flags |= priv->dev->flags;
	if (priv->mesh_dev && netif_running(priv->mesh_dev))
		dev_flags |= priv->mesh_dev->flags;

	if (dev_flags & IFF_PROMISC) {
		priv->mac_control |= CMD_ACT_MAC_PROMISCUOUS_ENABLE;
		priv->mac_control &= ~(CMD_ACT_MAC_ALL_MULTICAST_ENABLE |
				       CMD_ACT_MAC_MULTICAST_ENABLE);
		goto out_set_mac_control;
	} else if (dev_flags & IFF_ALLMULTI) {
	do_allmulti:
		priv->mac_control |= CMD_ACT_MAC_ALL_MULTICAST_ENABLE;
		priv->mac_control &= ~(CMD_ACT_MAC_PROMISCUOUS_ENABLE |
				       CMD_ACT_MAC_MULTICAST_ENABLE);
		goto out_set_mac_control;
	}

	/* Once for priv->dev, again for priv->mesh_dev if it exists */
	nr_addrs = lbs_add_mcast_addrs(&mcast_cmd, priv->dev, 0);
	if (nr_addrs >= 0 && priv->mesh_dev)
		nr_addrs = lbs_add_mcast_addrs(&mcast_cmd, priv->mesh_dev, nr_addrs);
	if (nr_addrs < 0)
		goto do_allmulti;

	if (nr_addrs) {
		int size = offsetof(struct cmd_ds_mac_multicast_adr,
				    maclist[6*nr_addrs]);

		mcast_cmd.action = cpu_to_le16(CMD_ACT_SET);
		mcast_cmd.hdr.size = cpu_to_le16(size);
		mcast_cmd.nr_of_adrs = cpu_to_le16(nr_addrs);

		lbs_cmd_async(priv, CMD_MAC_MULTICAST_ADR, &mcast_cmd.hdr, size);

		priv->mac_control |= CMD_ACT_MAC_MULTICAST_ENABLE;
	} else
		priv->mac_control &= ~CMD_ACT_MAC_MULTICAST_ENABLE;

	priv->mac_control &= ~(CMD_ACT_MAC_PROMISCUOUS_ENABLE |
			       CMD_ACT_MAC_ALL_MULTICAST_ENABLE);
 out_set_mac_control:
	if (priv->mac_control != old_mac_control)
		lbs_set_mac_control(priv);
}

static void lbs_set_mcast_worker(struct work_struct *work)
{
	struct lbs_private *priv = container_of(work, struct lbs_private, mcast_work);
	lbs_update_mcast(priv);
}

void lbs_set_multicast_list(struct net_device *dev)
{
	struct lbs_private *priv = dev->ml_priv;

	schedule_work(&priv->mcast_work);
}

/**
 *  lbs_thread - handles the major jobs in the LBS driver.
 *  It handles all events generated by firmware, RX data received
 *  from firmware and TX data sent from kernel.
 *
 *  @data:	A pointer to &lbs_thread structure
 *  returns:	0
 */
static int lbs_thread(void *data)
{
	struct net_device *dev = data;
	struct lbs_private *priv = dev->ml_priv;
	wait_queue_entry_t wait;

	init_waitqueue_entry(&wait, current);

	for (;;) {
		int shouldsleep;
		u8 resp_idx;

		lbs_deb_thread("1: currenttxskb %p, dnld_sent %d\n",
				priv->currenttxskb, priv->dnld_sent);

		add_wait_queue(&priv->waitq, &wait);
		set_current_state(TASK_INTERRUPTIBLE);
		spin_lock_irq(&priv->driver_lock);

		if (kthread_should_stop())
			shouldsleep = 0;	/* Bye */
		else if (priv->surpriseremoved)
			shouldsleep = 1;	/* We need to wait until we're _told_ to die */
		else if (priv->psstate == PS_STATE_SLEEP)
			shouldsleep = 1;	/* Sleep mode. Nothing we can do till it wakes */
		else if (priv->cmd_timed_out)
			shouldsleep = 0;	/* Command timed out. Recover */
		else if (!priv->fw_ready)
			shouldsleep = 1;	/* Firmware not ready. We're waiting for it */
		else if (priv->dnld_sent)
			shouldsleep = 1;	/* Something is en route to the device already */
		else if (priv->tx_pending_len > 0)
			shouldsleep = 0;	/* We've a packet to send */
		else if (priv->resp_len[priv->resp_idx])
			shouldsleep = 0;	/* We have a command response */
		else if (priv->cur_cmd)
			shouldsleep = 1;	/* Can't send a command; one already running */
		else if (!list_empty(&priv->cmdpendingq) &&
					!(priv->wakeup_dev_required))
			shouldsleep = 0;	/* We have a command to send */
		else if (kfifo_len(&priv->event_fifo))
			shouldsleep = 0;	/* We have an event to process */
		else
			shouldsleep = 1;	/* No command */

		if (shouldsleep) {
			lbs_deb_thread("sleeping, connect_status %d, "
				"psmode %d, psstate %d\n",
				priv->connect_status,
				priv->psmode, priv->psstate);
			spin_unlock_irq(&priv->driver_lock);
			schedule();
		} else
			spin_unlock_irq(&priv->driver_lock);

		lbs_deb_thread("2: currenttxskb %p, dnld_send %d\n",
			       priv->currenttxskb, priv->dnld_sent);

		set_current_state(TASK_RUNNING);
		remove_wait_queue(&priv->waitq, &wait);

		lbs_deb_thread("3: currenttxskb %p, dnld_sent %d\n",
			       priv->currenttxskb, priv->dnld_sent);

		if (kthread_should_stop()) {
			lbs_deb_thread("break from main thread\n");
			break;
		}

		if (priv->surpriseremoved) {
			lbs_deb_thread("adapter removed; waiting to die...\n");
			continue;
		}

		lbs_deb_thread("4: currenttxskb %p, dnld_sent %d\n",
		       priv->currenttxskb, priv->dnld_sent);

		/* Process any pending command response */
		spin_lock_irq(&priv->driver_lock);
		resp_idx = priv->resp_idx;
		if (priv->resp_len[resp_idx]) {
			spin_unlock_irq(&priv->driver_lock);
			lbs_process_command_response(priv,
				priv->resp_buf[resp_idx],
				priv->resp_len[resp_idx]);
			spin_lock_irq(&priv->driver_lock);
			priv->resp_len[resp_idx] = 0;
		}
		spin_unlock_irq(&priv->driver_lock);

		/* Process hardware events, e.g. card removed, link lost */
		spin_lock_irq(&priv->driver_lock);
		while (kfifo_len(&priv->event_fifo)) {
			u32 event;

			if (kfifo_out(&priv->event_fifo,
				(unsigned char *) &event, sizeof(event)) !=
				sizeof(event))
					break;
			spin_unlock_irq(&priv->driver_lock);
			lbs_process_event(priv, event);
			spin_lock_irq(&priv->driver_lock);
		}
		spin_unlock_irq(&priv->driver_lock);

		if (priv->wakeup_dev_required) {
			lbs_deb_thread("Waking up device...\n");
			/* Wake up device */
			if (priv->exit_deep_sleep(priv))
				lbs_deb_thread("Wakeup device failed\n");
			continue;
		}

		/* command timeout stuff */
		if (priv->cmd_timed_out && priv->cur_cmd) {
			struct cmd_ctrl_node *cmdnode = priv->cur_cmd;

			netdev_info(dev, "Timeout submitting command 0x%04x\n",
				    le16_to_cpu(cmdnode->cmdbuf->command));
			lbs_complete_command(priv, cmdnode, -ETIMEDOUT);

			/* Reset card, but only when it isn't in the process
			 * of being shutdown anyway. */
			if (!dev->dismantle && priv->reset_card)
				priv->reset_card(priv);
		}
		priv->cmd_timed_out = 0;

		if (!priv->fw_ready)
			continue;

		/* Check if we need to confirm Sleep Request received previously */
		if (priv->psstate == PS_STATE_PRE_SLEEP &&
		    !priv->dnld_sent && !priv->cur_cmd) {
			if (priv->connect_status == LBS_CONNECTED) {
				lbs_deb_thread("pre-sleep, currenttxskb %p, "
					"dnld_sent %d, cur_cmd %p\n",
					priv->currenttxskb, priv->dnld_sent,
					priv->cur_cmd);

				lbs_ps_confirm_sleep(priv);
			} else {
				/* workaround for firmware sending
				 * deauth/linkloss event immediately
				 * after sleep request; remove this
				 * after firmware fixes it
				 */
				priv->psstate = PS_STATE_AWAKE;
				netdev_alert(dev,
					     "ignore PS_SleepConfirm in non-connected state\n");
			}
		}

		/* The PS state is changed during processing of Sleep Request
		 * event above
		 */
		if ((priv->psstate == PS_STATE_SLEEP) ||
		    (priv->psstate == PS_STATE_PRE_SLEEP))
			continue;

		if (priv->is_deep_sleep)
			continue;

		/* Execute the next command */
		if (!priv->dnld_sent && !priv->cur_cmd)
			lbs_execute_next_command(priv);

		spin_lock_irq(&priv->driver_lock);
		if (!priv->dnld_sent && priv->tx_pending_len > 0) {
			int ret = priv->hw_host_to_card(priv, MVMS_DAT,
							priv->tx_pending_buf,
							priv->tx_pending_len);
			if (ret) {
				lbs_deb_tx("host_to_card failed %d\n", ret);
				priv->dnld_sent = DNLD_RES_RECEIVED;
			} else {
				mod_timer(&priv->tx_lockup_timer,
					  jiffies + (HZ * 5));
			}
			priv->tx_pending_len = 0;
			if (!priv->currenttxskb) {
				/* We can wake the queues immediately if we aren't
				   waiting for TX feedback */
				if (priv->connect_status == LBS_CONNECTED)
					netif_wake_queue(priv->dev);
				if (priv->mesh_dev &&
				    netif_running(priv->mesh_dev))
					netif_wake_queue(priv->mesh_dev);
			}
		}
		spin_unlock_irq(&priv->driver_lock);
	}

	del_timer(&priv->command_timer);
	del_timer(&priv->tx_lockup_timer);
	del_timer(&priv->auto_deepsleep_timer);

	return 0;
}

/**
 * lbs_setup_firmware - gets the HW spec from the firmware and sets
 *        some basic parameters
 *
 *  @priv:	A pointer to &struct lbs_private structure
 *  returns:	0 or -1
 */
static int lbs_setup_firmware(struct lbs_private *priv)
{
	int ret = -1;
	s16 curlevel = 0, minlevel = 0, maxlevel = 0;

	/* Read MAC address from firmware */
	eth_broadcast_addr(priv->current_addr);
	ret = lbs_update_hw_spec(priv);
	if (ret)
		goto done;

	/* Read power levels if available */
	ret = lbs_get_tx_power(priv, &curlevel, &minlevel, &maxlevel);
	if (ret == 0) {
		priv->txpower_cur = curlevel;
		priv->txpower_min = minlevel;
		priv->txpower_max = maxlevel;
	}

	/* Send cmd to FW to enable 11D function */
	ret = lbs_set_snmp_mib(priv, SNMP_MIB_OID_11D_ENABLE, 1);
	if (ret)
		goto done;

	ret = lbs_set_mac_control_sync(priv);
done:
	return ret;
}

int lbs_suspend(struct lbs_private *priv)
{
	int ret;

	if (priv->is_deep_sleep) {
		ret = lbs_set_deep_sleep(priv, 0);
		if (ret) {
			netdev_err(priv->dev,
				   "deep sleep cancellation failed: %d\n", ret);
			return ret;
		}
		priv->deep_sleep_required = 1;
	}

	ret = lbs_set_host_sleep(priv, 1);

	netif_device_detach(priv->dev);
	if (priv->mesh_dev)
		netif_device_detach(priv->mesh_dev);

	return ret;
}
EXPORT_SYMBOL_GPL(lbs_suspend);

int lbs_resume(struct lbs_private *priv)
{
	int ret;

	ret = lbs_set_host_sleep(priv, 0);

	netif_device_attach(priv->dev);
	if (priv->mesh_dev)
		netif_device_attach(priv->mesh_dev);

	if (priv->deep_sleep_required) {
		priv->deep_sleep_required = 0;
		ret = lbs_set_deep_sleep(priv, 1);
		if (ret)
			netdev_err(priv->dev,
				   "deep sleep activation failed: %d\n", ret);
	}

	if (priv->setup_fw_on_resume)
		ret = lbs_setup_firmware(priv);

	return ret;
}
EXPORT_SYMBOL_GPL(lbs_resume);

/**
 * lbs_cmd_timeout_handler - handles the timeout of command sending.
 * It will re-send the same command again.
 *
 * @data: &struct lbs_private pointer
 */
static void lbs_cmd_timeout_handler(struct timer_list *t)
{
	struct lbs_private *priv = from_timer(priv, t, command_timer);
	unsigned long flags;

	spin_lock_irqsave(&priv->driver_lock, flags);

	if (!priv->cur_cmd)
		goto out;

	netdev_info(priv->dev, "command 0x%04x timed out\n",
		    le16_to_cpu(priv->cur_cmd->cmdbuf->command));

	priv->cmd_timed_out = 1;

	/*
	 * If the device didn't even acknowledge the command, reset the state
	 * so that we don't block all future commands due to this one timeout.
	 */
	if (priv->dnld_sent == DNLD_CMD_SENT)
		priv->dnld_sent = DNLD_RES_RECEIVED;

	wake_up(&priv->waitq);
out:
	spin_unlock_irqrestore(&priv->driver_lock, flags);
}

/**
 * lbs_tx_lockup_handler - handles the timeout of the passing of TX frames
 * to the hardware. This is known to frequently happen with SD8686 when
 * waking up after a Wake-on-WLAN-triggered resume.
 *
 * @data: &struct lbs_private pointer
 */
static void lbs_tx_lockup_handler(struct timer_list *t)
{
	struct lbs_private *priv = from_timer(priv, t, tx_lockup_timer);
	unsigned long flags;

	spin_lock_irqsave(&priv->driver_lock, flags);

	netdev_info(priv->dev, "TX lockup detected\n");
	if (priv->reset_card)
		priv->reset_card(priv);

	priv->dnld_sent = DNLD_RES_RECEIVED;
	wake_up_interruptible(&priv->waitq);

	spin_unlock_irqrestore(&priv->driver_lock, flags);
}

/**
 * auto_deepsleep_timer_fn - put the device back to deep sleep mode when
 * timer expires and no activity (command, event, data etc.) is detected.
 * @data:	&struct lbs_private pointer
 * returns:	N/A
 */
static void auto_deepsleep_timer_fn(struct timer_list *t)
{
	struct lbs_private *priv = from_timer(priv, t, auto_deepsleep_timer);

	if (priv->is_activity_detected) {
		priv->is_activity_detected = 0;
	} else {
		if (priv->is_auto_deep_sleep_enabled &&
		    (!priv->wakeup_dev_required) &&
		    (priv->connect_status != LBS_CONNECTED)) {
			struct cmd_header cmd;

			lbs_deb_main("Entering auto deep sleep mode...\n");
			memset(&cmd, 0, sizeof(cmd));
			cmd.size = cpu_to_le16(sizeof(cmd));
			lbs_cmd_async(priv, CMD_802_11_DEEP_SLEEP, &cmd,
					sizeof(cmd));
		}
	}
	mod_timer(&priv->auto_deepsleep_timer , jiffies +
				(priv->auto_deep_sleep_timeout * HZ)/1000);
}

int lbs_enter_auto_deep_sleep(struct lbs_private *priv)
{
	priv->is_auto_deep_sleep_enabled = 1;
	if (priv->is_deep_sleep)
		priv->wakeup_dev_required = 1;
	mod_timer(&priv->auto_deepsleep_timer ,
			jiffies + (priv->auto_deep_sleep_timeout * HZ)/1000);

	return 0;
}

int lbs_exit_auto_deep_sleep(struct lbs_private *priv)
{
	priv->is_auto_deep_sleep_enabled = 0;
	priv->auto_deep_sleep_timeout = 0;
	del_timer(&priv->auto_deepsleep_timer);

	return 0;
}

static int lbs_init_adapter(struct lbs_private *priv)
{
	int ret;

	eth_broadcast_addr(priv->current_addr);

	priv->connect_status = LBS_DISCONNECTED;
	priv->channel = DEFAULT_AD_HOC_CHANNEL;
	priv->mac_control = CMD_ACT_MAC_RX_ON | CMD_ACT_MAC_TX_ON;
	priv->radio_on = 1;
	priv->psmode = LBS802_11POWERMODECAM;
	priv->psstate = PS_STATE_FULL_POWER;
	priv->is_deep_sleep = 0;
	priv->is_auto_deep_sleep_enabled = 0;
	priv->deep_sleep_required = 0;
	priv->wakeup_dev_required = 0;
	init_waitqueue_head(&priv->ds_awake_q);
	init_waitqueue_head(&priv->scan_q);
	priv->authtype_auto = 1;
	priv->is_host_sleep_configured = 0;
	priv->is_host_sleep_activated = 0;
	init_waitqueue_head(&priv->host_sleep_q);
	init_waitqueue_head(&priv->fw_waitq);
	mutex_init(&priv->lock);

	timer_setup(&priv->command_timer, lbs_cmd_timeout_handler, 0);
	timer_setup(&priv->tx_lockup_timer, lbs_tx_lockup_handler, 0);
	timer_setup(&priv->auto_deepsleep_timer, auto_deepsleep_timer_fn, 0);

	INIT_LIST_HEAD(&priv->cmdfreeq);
	INIT_LIST_HEAD(&priv->cmdpendingq);

	spin_lock_init(&priv->driver_lock);

	/* Allocate the command buffers */
	if (lbs_allocate_cmd_buffer(priv)) {
		pr_err("Out of memory allocating command buffers\n");
		ret = -ENOMEM;
		goto out;
	}
	priv->resp_idx = 0;
	priv->resp_len[0] = priv->resp_len[1] = 0;

	/* Create the event FIFO */
	ret = kfifo_alloc(&priv->event_fifo, sizeof(u32) * 16, GFP_KERNEL);
	if (ret) {
		pr_err("Out of memory allocating event FIFO buffer\n");
		goto out;
	}

out:
	return ret;
}

static void lbs_free_adapter(struct lbs_private *priv)
{
	lbs_free_cmd_buffer(priv);
	kfifo_free(&priv->event_fifo);
	del_timer(&priv->command_timer);
	del_timer(&priv->tx_lockup_timer);
	del_timer(&priv->auto_deepsleep_timer);
}

static const struct net_device_ops lbs_netdev_ops = {
	.ndo_open 		= lbs_dev_open,
	.ndo_stop		= lbs_eth_stop,
	.ndo_start_xmit		= lbs_hard_start_xmit,
	.ndo_set_mac_address	= lbs_set_mac_address,
	.ndo_set_rx_mode	= lbs_set_multicast_list,
	.ndo_validate_addr	= eth_validate_addr,
};

/**
 * lbs_add_card - adds the card. It will probe the
 * card, allocate the lbs_priv and initialize the device.
 *
 * @card:	A pointer to card
 * @dmdev:	A pointer to &struct device
 * returns:	A pointer to &struct lbs_private structure
 */
struct lbs_private *lbs_add_card(void *card, struct device *dmdev)
{
	struct net_device *dev;
	struct wireless_dev *wdev;
	struct lbs_private *priv = NULL;
	int err;

	/* Allocate an Ethernet device and register it */
	wdev = lbs_cfg_alloc(dmdev);
	if (IS_ERR(wdev)) {
		err = PTR_ERR(wdev);
		pr_err("cfg80211 init failed\n");
		goto err_cfg;
	}

	wdev->iftype = NL80211_IFTYPE_STATION;
	priv = wdev_priv(wdev);
	priv->wdev = wdev;

	err = lbs_init_adapter(priv);
	if (err) {
		pr_err("failed to initialize adapter structure\n");
		goto err_wdev;
	}

	dev = alloc_netdev(0, "wlan%d", NET_NAME_UNKNOWN, ether_setup);
	if (!dev) {
		err = -ENOMEM;
		dev_err(dmdev, "no memory for network device instance\n");
		goto err_adapter;
	}

	dev->ieee80211_ptr = wdev;
	dev->ml_priv = priv;
	SET_NETDEV_DEV(dev, dmdev);
	wdev->netdev = dev;
	priv->dev = dev;

 	dev->netdev_ops = &lbs_netdev_ops;
	dev->watchdog_timeo = 5 * HZ;
	dev->ethtool_ops = &lbs_ethtool_ops;
	dev->flags |= IFF_BROADCAST | IFF_MULTICAST;

	priv->card = card;

	strcpy(dev->name, "wlan%d");

	lbs_deb_thread("Starting main thread...\n");
	init_waitqueue_head(&priv->waitq);
	priv->main_thread = kthread_run(lbs_thread, dev, "lbs_main");
	if (IS_ERR(priv->main_thread)) {
		err = PTR_ERR(priv->main_thread);
		lbs_deb_thread("Error creating main thread.\n");
		goto err_ndev;
	}

	priv->work_thread = create_singlethread_workqueue("lbs_worker");
	INIT_WORK(&priv->mcast_work, lbs_set_mcast_worker);

	priv->wol_criteria = EHS_REMOVE_WAKEUP;
	priv->wol_gpio = 0xff;
	priv->wol_gap = 20;
	priv->ehs_remove_supported = true;

	return priv;

 err_ndev:
	free_netdev(dev);

 err_adapter:
	lbs_free_adapter(priv);

 err_wdev:
	lbs_cfg_free(priv);

 err_cfg:
	return ERR_PTR(err);
}
EXPORT_SYMBOL_GPL(lbs_add_card);


void lbs_remove_card(struct lbs_private *priv)
{
	struct net_device *dev = priv->dev;

	lbs_remove_mesh(priv);

	if (priv->wiphy_registered)
		lbs_scan_deinit(priv);

	lbs_wait_for_firmware_load(priv);

	/* worker thread destruction blocks on the in-flight command which
	 * should have been cleared already in lbs_stop_card().
	 */
	lbs_deb_main("destroying worker thread\n");
	destroy_workqueue(priv->work_thread);
	lbs_deb_main("done destroying worker thread\n");

	if (priv->psmode == LBS802_11POWERMODEMAX_PSP) {
		priv->psmode = LBS802_11POWERMODECAM;
		/* no need to wakeup if already woken up,
		 * on suspend, this exit ps command is not processed
		 * the driver hangs
		 */
		if (priv->psstate != PS_STATE_FULL_POWER)
			lbs_set_ps_mode(priv, PS_MODE_ACTION_EXIT_PS, true);
	}

	if (priv->is_deep_sleep) {
		priv->is_deep_sleep = 0;
		wake_up_interruptible(&priv->ds_awake_q);
	}

	priv->is_host_sleep_configured = 0;
	priv->is_host_sleep_activated = 0;
	wake_up_interruptible(&priv->host_sleep_q);

	/* Stop the thread servicing the interrupts */
	priv->surpriseremoved = 1;
	kthread_stop(priv->main_thread);

	lbs_free_adapter(priv);
	lbs_cfg_free(priv);
	free_netdev(dev);
}
EXPORT_SYMBOL_GPL(lbs_remove_card);


int lbs_rtap_supported(struct lbs_private *priv)
{
	if (MRVL_FW_MAJOR_REV(priv->fwrelease) == MRVL_FW_V5)
		return 1;

	/* newer firmware use a capability mask */
	return ((MRVL_FW_MAJOR_REV(priv->fwrelease) >= MRVL_FW_V10) &&
		(priv->fwcapinfo & MESH_CAPINFO_ENABLE_MASK));
}


int lbs_start_card(struct lbs_private *priv)
{
	struct net_device *dev = priv->dev;
	int ret;

	/* poke the firmware */
	ret = lbs_setup_firmware(priv);
	if (ret)
		goto done;

	if (!lbs_disablemesh)
		lbs_init_mesh(priv);
	else
		pr_info("%s: mesh disabled\n", dev->name);

	ret = lbs_cfg_register(priv);
	if (ret) {
		pr_err("cannot register device\n");
		goto done;
	}

	if (lbs_mesh_activated(priv))
		lbs_start_mesh(priv);

	lbs_debugfs_init_one(priv, dev);

	netdev_info(dev, "Marvell WLAN 802.11 adapter\n");

	ret = 0;

done:
	return ret;
}
EXPORT_SYMBOL_GPL(lbs_start_card);


void lbs_stop_card(struct lbs_private *priv)
{
	struct net_device *dev;

	if (!priv)
		return;
	dev = priv->dev;

	/* If the netdev isn't registered, it means that lbs_start_card() was
	 * never called so we have nothing to do here. */
	if (dev->reg_state != NETREG_REGISTERED)
		return;

	netif_stop_queue(dev);
	netif_carrier_off(dev);

	lbs_debugfs_remove_one(priv);
	lbs_deinit_mesh(priv);
	unregister_netdev(dev);
}
EXPORT_SYMBOL_GPL(lbs_stop_card);


void lbs_queue_event(struct lbs_private *priv, u32 event)
{
	unsigned long flags;

	spin_lock_irqsave(&priv->driver_lock, flags);

	if (priv->psstate == PS_STATE_SLEEP)
		priv->psstate = PS_STATE_AWAKE;

	kfifo_in(&priv->event_fifo, (unsigned char *) &event, sizeof(u32));

	wake_up(&priv->waitq);

	spin_unlock_irqrestore(&priv->driver_lock, flags);
}
EXPORT_SYMBOL_GPL(lbs_queue_event);

void lbs_notify_command_response(struct lbs_private *priv, u8 resp_idx)
{
	if (priv->psstate == PS_STATE_SLEEP)
		priv->psstate = PS_STATE_AWAKE;

	/* Swap buffers by flipping the response index */
	BUG_ON(resp_idx > 1);
	priv->resp_idx = resp_idx;

	wake_up(&priv->waitq);
}
EXPORT_SYMBOL_GPL(lbs_notify_command_response);

static int __init lbs_init_module(void)
{
	memset(&confirm_sleep, 0, sizeof(confirm_sleep));
	confirm_sleep.hdr.command = cpu_to_le16(CMD_802_11_PS_MODE);
	confirm_sleep.hdr.size = cpu_to_le16(sizeof(confirm_sleep));
	confirm_sleep.action = cpu_to_le16(PS_MODE_ACTION_SLEEP_CONFIRMED);
	lbs_debugfs_init();

	return 0;
}

static void __exit lbs_exit_module(void)
{
	lbs_debugfs_remove();
}

module_init(lbs_init_module);
module_exit(lbs_exit_module);

MODULE_DESCRIPTION("Libertas WLAN Driver Library");
MODULE_AUTHOR("Marvell International Ltd.");
MODULE_LICENSE("GPL");

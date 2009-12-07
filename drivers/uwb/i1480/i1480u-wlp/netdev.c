/*
 * WUSB Wire Adapter: WLP interface
 * Driver for the Linux Network stack.
 *
 * Copyright (C) 2005-2006 Intel Corporation
 * Inaky Perez-Gonzalez <inaky.perez-gonzalez@intel.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 *
 * FIXME: docs
 *
 * Implementation of the netdevice linkage (except tx and rx related stuff).
 *
 * ROADMAP:
 *
 *   ENTRY POINTS (Net device):
 *
 *     i1480u_open(): Called when we ifconfig up the interface;
 *                    associates to a UWB host controller, reserves
 *                    bandwidth (MAS), sets up RX USB URB and starts
 *                    the queue.
 *
 *     i1480u_stop(): Called when we ifconfig down a interface;
 *                    reverses _open().
 *
 *     i1480u_set_config():
 */

#include <linux/if_arp.h>
#include <linux/etherdevice.h>

#include "i1480u-wlp.h"

struct i1480u_cmd_set_ip_mas {
	struct uwb_rccb     rccb;
	struct uwb_dev_addr addr;
	u8                  stream;
	u8                  owner;
	u8                  type;	/* enum uwb_drp_type */
	u8                  baMAS[32];
} __attribute__((packed));


static
int i1480u_set_ip_mas(
	struct uwb_rc *rc,
	const struct uwb_dev_addr *dstaddr,
	u8 stream, u8 owner, u8 type, unsigned long *mas)
{

	int result;
	struct i1480u_cmd_set_ip_mas *cmd;
	struct uwb_rc_evt_confirm reply;

	result = -ENOMEM;
	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (cmd == NULL)
		goto error_kzalloc;
	cmd->rccb.bCommandType = 0xfd;
	cmd->rccb.wCommand = cpu_to_le16(0x000e);
	cmd->addr = *dstaddr;
	cmd->stream = stream;
	cmd->owner = owner;
	cmd->type = type;
	if (mas == NULL)
		memset(cmd->baMAS, 0x00, sizeof(cmd->baMAS));
	else
		memcpy(cmd->baMAS, mas, sizeof(cmd->baMAS));
	reply.rceb.bEventType = 0xfd;
	reply.rceb.wEvent = cpu_to_le16(0x000e);
	result = uwb_rc_cmd(rc, "SET-IP-MAS", &cmd->rccb, sizeof(*cmd),
			    &reply.rceb, sizeof(reply));
	if (result < 0)
		goto error_cmd;
	if (reply.bResultCode != UWB_RC_RES_FAIL) {
		dev_err(&rc->uwb_dev.dev,
			"SET-IP-MAS: command execution failed: %d\n",
			reply.bResultCode);
		result = -EIO;
	}
error_cmd:
	kfree(cmd);
error_kzalloc:
	return result;
}

/*
 * Inform a WLP interface of a MAS reservation
 *
 * @rc is assumed refcnted.
 */
/* FIXME: detect if remote device is WLP capable? */
static int i1480u_mas_set_dev(struct uwb_dev *uwb_dev, struct uwb_rc *rc,
			      u8 stream, u8 owner, u8 type, unsigned long *mas)
{
	int result = 0;
	struct device *dev = &rc->uwb_dev.dev;

	result = i1480u_set_ip_mas(rc, &uwb_dev->dev_addr, stream, owner,
				   type, mas);
	if (result < 0) {
		char rcaddrbuf[UWB_ADDR_STRSIZE], devaddrbuf[UWB_ADDR_STRSIZE];
		uwb_dev_addr_print(rcaddrbuf, sizeof(rcaddrbuf),
				   &rc->uwb_dev.dev_addr);
		uwb_dev_addr_print(devaddrbuf, sizeof(devaddrbuf),
				   &uwb_dev->dev_addr);
		dev_err(dev, "Set IP MAS (%s to %s) failed: %d\n",
			rcaddrbuf, devaddrbuf, result);
	}
	return result;
}

/**
 * Called by bandwidth allocator when change occurs in reservation.
 *
 * @rsv:     The reservation that is being established, modified, or
 *           terminated.
 *
 * When a reservation is established, modified, or terminated the upper layer
 * (WLP here) needs set/update the currently available Media Access Slots
 * that can be use for IP traffic.
 *
 * Our action taken during failure depends on how the reservation is being
 * changed:
 * - if reservation is being established we do nothing if we cannot set the
 *   new MAS to be used
 * - if reservation is being terminated we revert back to PCA whether the
 *   SET IP MAS command succeeds or not.
 */
void i1480u_bw_alloc_cb(struct uwb_rsv *rsv)
{
	int result = 0;
	struct i1480u *i1480u = rsv->pal_priv;
	struct device *dev = &i1480u->usb_iface->dev;
	struct uwb_dev *target_dev = rsv->target.dev;
	struct uwb_rc *rc = i1480u->wlp.rc;
	u8 stream = rsv->stream;
	int type = rsv->type;
	int is_owner = rsv->owner == &rc->uwb_dev;
	unsigned long *bmp = rsv->mas.bm;

	dev_err(dev, "WLP callback called - sending set ip mas\n");
	/*user cannot change options while setting configuration*/
	mutex_lock(&i1480u->options.mutex);
	switch (rsv->state) {
	case UWB_RSV_STATE_T_ACCEPTED:
	case UWB_RSV_STATE_O_ESTABLISHED:
		result = i1480u_mas_set_dev(target_dev, rc, stream, is_owner,
					type, bmp);
		if (result < 0) {
			dev_err(dev, "MAS reservation failed: %d\n", result);
			goto out;
		}
		if (is_owner) {
			wlp_tx_hdr_set_delivery_id_type(&i1480u->options.def_tx_hdr,
							WLP_DRP | stream);
			wlp_tx_hdr_set_rts_cts(&i1480u->options.def_tx_hdr, 0);
		}
		break;
	case UWB_RSV_STATE_NONE:
		/* revert back to PCA */
		result = i1480u_mas_set_dev(target_dev, rc, stream, is_owner,
					    type, bmp);
		if (result < 0)
			dev_err(dev, "MAS reservation failed: %d\n", result);
		/* Revert to PCA even though SET IP MAS failed. */
		wlp_tx_hdr_set_delivery_id_type(&i1480u->options.def_tx_hdr,
						i1480u->options.pca_base_priority);
		wlp_tx_hdr_set_rts_cts(&i1480u->options.def_tx_hdr, 1);
		break;
	default:
		dev_err(dev, "unexpected WLP reservation state: %s (%d).\n",
			uwb_rsv_state_str(rsv->state), rsv->state);
		break;
	}
out:
	mutex_unlock(&i1480u->options.mutex);
	return;
}

/**
 *
 * Called on 'ifconfig up'
 */
int i1480u_open(struct net_device *net_dev)
{
	int result;
	struct i1480u *i1480u = netdev_priv(net_dev);
	struct wlp *wlp = &i1480u->wlp;
	struct uwb_rc *rc;
	struct device *dev = &i1480u->usb_iface->dev;

	rc = wlp->rc;
	result = i1480u_rx_setup(i1480u);		/* Alloc RX stuff */
	if (result < 0)
		goto error_rx_setup;

	result = uwb_radio_start(&wlp->pal);
	if (result < 0)
		goto error_radio_start;

	netif_wake_queue(net_dev);
#ifdef i1480u_FLOW_CONTROL
	result = usb_submit_urb(i1480u->notif_urb, GFP_KERNEL);
	if (result < 0) {
		dev_err(dev, "Can't submit notification URB: %d\n", result);
		goto error_notif_urb_submit;
	}
#endif
	/* Interface is up with an address, now we can create WSS */
	result = wlp_wss_setup(net_dev, &wlp->wss);
	if (result < 0) {
		dev_err(dev, "Can't create WSS: %d. \n", result);
		goto error_wss_setup;
	}
	return 0;
error_wss_setup:
#ifdef i1480u_FLOW_CONTROL
	usb_kill_urb(i1480u->notif_urb);
error_notif_urb_submit:
#endif
	uwb_radio_stop(&wlp->pal);
error_radio_start:
	netif_stop_queue(net_dev);
	i1480u_rx_release(i1480u);
error_rx_setup:
	return result;
}


/**
 * Called on 'ifconfig down'
 */
int i1480u_stop(struct net_device *net_dev)
{
	struct i1480u *i1480u = netdev_priv(net_dev);
	struct wlp *wlp = &i1480u->wlp;

	BUG_ON(wlp->rc == NULL);
	wlp_wss_remove(&wlp->wss);
	netif_carrier_off(net_dev);
#ifdef i1480u_FLOW_CONTROL
	usb_kill_urb(i1480u->notif_urb);
#endif
	netif_stop_queue(net_dev);
	uwb_radio_stop(&wlp->pal);
	i1480u_rx_release(i1480u);
	i1480u_tx_release(i1480u);
	return 0;
}

/**
 *
 * Change the interface config--we probably don't have to do anything.
 */
int i1480u_set_config(struct net_device *net_dev, struct ifmap *map)
{
	int result;
	struct i1480u *i1480u = netdev_priv(net_dev);
	BUG_ON(i1480u->wlp.rc == NULL);
	result = 0;
	return result;
}

/**
 * Change the MTU of the interface
 */
int i1480u_change_mtu(struct net_device *net_dev, int mtu)
{
	static union {
		struct wlp_tx_hdr tx;
		struct wlp_rx_hdr rx;
	} i1480u_all_hdrs;

	if (mtu < ETH_HLEN)	/* We encap eth frames */
		return -ERANGE;
	if (mtu > 4000 - sizeof(i1480u_all_hdrs))
		return -ERANGE;
	net_dev->mtu = mtu;
	return 0;
}

/**
 * Stop the network queue
 *
 * Enable WLP substack to stop network queue. We also set the flow control
 * threshold at this time to prevent the flow control from restarting the
 * queue.
 *
 * we are loosing the current threshold value here ... FIXME?
 */
void i1480u_stop_queue(struct wlp *wlp)
{
	struct i1480u *i1480u = container_of(wlp, struct i1480u, wlp);
	struct net_device *net_dev = i1480u->net_dev;
	i1480u->tx_inflight.threshold = 0;
	netif_stop_queue(net_dev);
}

/**
 * Start the network queue
 *
 * Enable WLP substack to start network queue. Also re-enable the flow
 * control to manage the queue again.
 *
 * We re-enable the flow control by storing the default threshold in the
 * flow control threshold. This means that if the user modified the
 * threshold before the queue was stopped and restarted that information
 * will be lost. FIXME?
 */
void i1480u_start_queue(struct wlp *wlp)
{
	struct i1480u *i1480u = container_of(wlp, struct i1480u, wlp);
	struct net_device *net_dev = i1480u->net_dev;
	i1480u->tx_inflight.threshold = i1480u_TX_INFLIGHT_THRESHOLD;
	netif_start_queue(net_dev);
}

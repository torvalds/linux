/*
 * WiMedia Logical Link Control Protocol (WLP)
 *
 * Copyright (C) 2005-2006 Intel Corporation
 * Reinette Chatre <reinette.chatre@intel.com>
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
 */
#include <linux/wlp.h>
#include <linux/slab.h>

#include "wlp-internal.h"

static
void wlp_neighbor_init(struct wlp_neighbor_e *neighbor)
{
	INIT_LIST_HEAD(&neighbor->wssid);
}

/**
 * Create area for device information storage
 *
 * wlp->mutex must be held
 */
int __wlp_alloc_device_info(struct wlp *wlp)
{
	struct device *dev = &wlp->rc->uwb_dev.dev;
	BUG_ON(wlp->dev_info != NULL);
	wlp->dev_info = kzalloc(sizeof(struct wlp_device_info), GFP_KERNEL);
	if (wlp->dev_info == NULL) {
		dev_err(dev, "WLP: Unable to allocate memory for "
			"device information.\n");
		return -ENOMEM;
	}
	return 0;
}


/**
 * Fill in device information using function provided by driver
 *
 * wlp->mutex must be held
 */
static
void __wlp_fill_device_info(struct wlp *wlp)
{
	wlp->fill_device_info(wlp, wlp->dev_info);
}

/**
 * Setup device information
 *
 * Allocate area for device information and populate it.
 *
 * wlp->mutex must be held
 */
int __wlp_setup_device_info(struct wlp *wlp)
{
	int result;
	struct device *dev = &wlp->rc->uwb_dev.dev;

	result = __wlp_alloc_device_info(wlp);
	if (result < 0) {
		dev_err(dev, "WLP: Unable to allocate area for "
			"device information.\n");
		return result;
	}
	__wlp_fill_device_info(wlp);
	return 0;
}

/**
 * Remove information about neighbor stored temporarily
 *
 * Information learned during discovey should only be stored when the
 * device enrolls in the neighbor's WSS. We do need to store this
 * information temporarily in order to present it to the user.
 *
 * We are only interested in keeping neighbor WSS information if that
 * neighbor is accepting enrollment.
 *
 * should be called with wlp->nbmutex held
 */
void wlp_remove_neighbor_tmp_info(struct wlp_neighbor_e *neighbor)
{
	struct wlp_wssid_e *wssid_e, *next;
	u8 keep;
	if (!list_empty(&neighbor->wssid)) {
		list_for_each_entry_safe(wssid_e, next, &neighbor->wssid,
					 node) {
			if (wssid_e->info != NULL) {
				keep = wssid_e->info->accept_enroll;
				kfree(wssid_e->info);
				wssid_e->info = NULL;
				if (!keep) {
					list_del(&wssid_e->node);
					kfree(wssid_e);
				}
			}
		}
	}
	if (neighbor->info != NULL) {
		kfree(neighbor->info);
		neighbor->info = NULL;
	}
}

/*
 * Populate WLP neighborhood cache with neighbor information
 *
 * A new neighbor is found. If it is discoverable then we add it to the
 * neighborhood cache.
 *
 */
static
int wlp_add_neighbor(struct wlp *wlp, struct uwb_dev *dev)
{
	int result = 0;
	int discoverable;
	struct wlp_neighbor_e *neighbor;

	/*
	 * FIXME:
	 * Use contents of WLP IE found in beacon cache to determine if
	 * neighbor is discoverable.
	 * The device does not support WLP IE yet so this still needs to be
	 * done. Until then we assume all devices are discoverable.
	 */
	discoverable = 1; /* will be changed when FIXME disappears */
	if (discoverable) {
		/* Add neighbor to cache for discovery */
		neighbor = kzalloc(sizeof(*neighbor), GFP_KERNEL);
		if (neighbor == NULL) {
			dev_err(&dev->dev, "Unable to create memory for "
				"new neighbor. \n");
			result = -ENOMEM;
			goto error_no_mem;
		}
		wlp_neighbor_init(neighbor);
		uwb_dev_get(dev);
		neighbor->uwb_dev = dev;
		list_add(&neighbor->node, &wlp->neighbors);
	}
error_no_mem:
	return result;
}

/**
 * Remove one neighbor from cache
 */
static
void __wlp_neighbor_release(struct wlp_neighbor_e *neighbor)
{
	struct wlp_wssid_e *wssid_e, *next_wssid_e;

	list_for_each_entry_safe(wssid_e, next_wssid_e,
				 &neighbor->wssid, node) {
		list_del(&wssid_e->node);
		kfree(wssid_e);
	}
	uwb_dev_put(neighbor->uwb_dev);
	list_del(&neighbor->node);
	kfree(neighbor);
}

/**
 * Clear entire neighborhood cache.
 */
static
void __wlp_neighbors_release(struct wlp *wlp)
{
	struct wlp_neighbor_e *neighbor, *next;
	if (list_empty(&wlp->neighbors))
		return;
	list_for_each_entry_safe(neighbor, next, &wlp->neighbors, node) {
		__wlp_neighbor_release(neighbor);
	}
}

static
void wlp_neighbors_release(struct wlp *wlp)
{
	mutex_lock(&wlp->nbmutex);
	__wlp_neighbors_release(wlp);
	mutex_unlock(&wlp->nbmutex);
}



/**
 * Send D1 message to neighbor, receive D2 message
 *
 * @neighbor: neighbor to which D1 message will be sent
 * @wss:      if not NULL, it is an enrollment request for this WSS
 * @wssid:    if wss not NULL, this is the wssid of the WSS in which we
 *            want to enroll
 *
 * A D1/D2 exchange is done for one of two reasons: discovery or
 * enrollment. If done for discovery the D1 message is sent to the neighbor
 * and the contents of the D2 response is stored in a temporary cache.
 * If done for enrollment the @wss and @wssid are provided also. In this
 * case the D1 message is sent to the neighbor, the D2 response is parsed
 * for enrollment of the WSS with wssid.
 *
 * &wss->mutex is held
 */
static
int wlp_d1d2_exchange(struct wlp *wlp, struct wlp_neighbor_e *neighbor,
		      struct wlp_wss *wss, struct wlp_uuid *wssid)
{
	int result;
	struct device *dev = &wlp->rc->uwb_dev.dev;
	DECLARE_COMPLETION_ONSTACK(completion);
	struct wlp_session session;
	struct sk_buff  *skb;
	struct wlp_frame_assoc *resp;
	struct uwb_dev_addr *dev_addr = &neighbor->uwb_dev->dev_addr;

	mutex_lock(&wlp->mutex);
	if (!wlp_uuid_is_set(&wlp->uuid)) {
		dev_err(dev, "WLP: UUID is not set. Set via sysfs to "
			"proceed.\n");
		result = -ENXIO;
		goto out;
	}
	/* Send D1 association frame */
	result = wlp_send_assoc_frame(wlp, wss, dev_addr, WLP_ASSOC_D1);
	if (result < 0) {
		dev_err(dev, "Unable to send D1 frame to neighbor "
			"%02x:%02x (%d)\n", dev_addr->data[1],
			dev_addr->data[0], result);
		goto out;
	}
	/* Create session, wait for response */
	session.exp_message = WLP_ASSOC_D2;
	session.cb = wlp_session_cb;
	session.cb_priv = &completion;
	session.neighbor_addr = *dev_addr;
	BUG_ON(wlp->session != NULL);
	wlp->session = &session;
	/* Wait for D2/F0 frame */
	result = wait_for_completion_interruptible_timeout(&completion,
						   WLP_PER_MSG_TIMEOUT * HZ);
	if (result == 0) {
		result = -ETIMEDOUT;
		dev_err(dev, "Timeout while sending D1 to neighbor "
			     "%02x:%02x.\n", dev_addr->data[1],
			     dev_addr->data[0]);
		goto error_session;
	}
	if (result < 0) {
		dev_err(dev, "Unable to discover/enroll neighbor %02x:%02x.\n",
			dev_addr->data[1], dev_addr->data[0]);
		goto error_session;
	}
	/* Parse message in session->data: it will be either D2 or F0 */
	skb = session.data;
	resp = (void *) skb->data;

	if (resp->type == WLP_ASSOC_F0) {
		result = wlp_parse_f0(wlp, skb);
		if (result < 0)
			dev_err(dev, "WLP: Unable to parse F0 from neighbor "
				"%02x:%02x.\n", dev_addr->data[1],
				dev_addr->data[0]);
		result = -EINVAL;
		goto error_resp_parse;
	}
	if (wss == NULL) {
		/* Discovery */
		result = wlp_parse_d2_frame_to_cache(wlp, skb, neighbor);
		if (result < 0) {
			dev_err(dev, "WLP: Unable to parse D2 message from "
				"neighbor %02x:%02x for discovery.\n",
				dev_addr->data[1], dev_addr->data[0]);
			goto error_resp_parse;
		}
	} else {
		/* Enrollment */
		result = wlp_parse_d2_frame_to_enroll(wss, skb, neighbor,
						      wssid);
		if (result < 0) {
			dev_err(dev, "WLP: Unable to parse D2 message from "
				"neighbor %02x:%02x for enrollment.\n",
				dev_addr->data[1], dev_addr->data[0]);
			goto error_resp_parse;
		}
	}
error_resp_parse:
	kfree_skb(skb);
error_session:
	wlp->session = NULL;
out:
	mutex_unlock(&wlp->mutex);
	return result;
}

/**
 * Enroll into WSS of provided WSSID by using neighbor as registrar
 *
 * &wss->mutex is held
 */
int wlp_enroll_neighbor(struct wlp *wlp, struct wlp_neighbor_e *neighbor,
			struct wlp_wss *wss, struct wlp_uuid *wssid)
{
	int result = 0;
	struct device *dev = &wlp->rc->uwb_dev.dev;
	char buf[WLP_WSS_UUID_STRSIZE];
	struct uwb_dev_addr *dev_addr = &neighbor->uwb_dev->dev_addr;

	wlp_wss_uuid_print(buf, sizeof(buf), wssid);

	result =  wlp_d1d2_exchange(wlp, neighbor, wss, wssid);
	if (result < 0) {
		dev_err(dev, "WLP: D1/D2 message exchange for enrollment "
			"failed. result = %d \n", result);
		goto out;
	}
	if (wss->state != WLP_WSS_STATE_PART_ENROLLED) {
		dev_err(dev, "WLP: Unable to enroll into WSS %s using "
			"neighbor %02x:%02x. \n", buf,
			dev_addr->data[1], dev_addr->data[0]);
		result = -EINVAL;
		goto out;
	}
	if (wss->secure_status == WLP_WSS_SECURE) {
		dev_err(dev, "FIXME: need to complete secure enrollment.\n");
		result = -EINVAL;
		goto error;
	} else {
		wss->state = WLP_WSS_STATE_ENROLLED;
		dev_dbg(dev, "WLP: Success Enrollment into unsecure WSS "
			"%s using neighbor %02x:%02x. \n",
			buf, dev_addr->data[1], dev_addr->data[0]);
	}
out:
	return result;
error:
	wlp_wss_reset(wss);
	return result;
}

/**
 * Discover WSS information of neighbor's active WSS
 */
static
int wlp_discover_neighbor(struct wlp *wlp,
			  struct wlp_neighbor_e *neighbor)
{
	return wlp_d1d2_exchange(wlp, neighbor, NULL, NULL);
}


/**
 * Each neighbor in the neighborhood cache is discoverable. Discover it.
 *
 * Discovery is done through sending of D1 association frame and parsing
 * the D2 association frame response. Only wssid from D2 will be included
 * in neighbor cache, rest is just displayed to user and forgotten.
 *
 * The discovery is not done in parallel. This is simple and enables us to
 * maintain only one association context.
 *
 * The discovery of one neighbor does not affect the other, but if the
 * discovery of a neighbor fails it is removed from the neighborhood cache.
 */
static
int wlp_discover_all_neighbors(struct wlp *wlp)
{
	int result = 0;
	struct device *dev = &wlp->rc->uwb_dev.dev;
	struct wlp_neighbor_e *neighbor, *next;

	list_for_each_entry_safe(neighbor, next, &wlp->neighbors, node) {
		result = wlp_discover_neighbor(wlp, neighbor);
		if (result < 0) {
			dev_err(dev, "WLP: Unable to discover neighbor "
				"%02x:%02x, removing from neighborhood. \n",
				neighbor->uwb_dev->dev_addr.data[1],
				neighbor->uwb_dev->dev_addr.data[0]);
			__wlp_neighbor_release(neighbor);
		}
	}
	return result;
}

static int wlp_add_neighbor_helper(struct device *dev, void *priv)
{
	struct wlp *wlp = priv;
	struct uwb_dev *uwb_dev = to_uwb_dev(dev);

	return wlp_add_neighbor(wlp, uwb_dev);
}

/**
 * Discover WLP neighborhood
 *
 * Will send D1 association frame to all devices in beacon group that have
 * discoverable bit set in WLP IE. D2 frames will be received, information
 * displayed to user in @buf. Partial information (from D2 association
 * frame) will be cached to assist with future association
 * requests.
 *
 * The discovery of the WLP neighborhood is triggered by the user. This
 * should occur infrequently and we thus free current cache and re-allocate
 * memory if needed.
 *
 * If one neighbor fails during initial discovery (determining if it is a
 * neighbor or not), we fail all - note that interaction with neighbor has
 * not occured at this point so if a failure occurs we know something went wrong
 * locally. We thus undo everything.
 */
ssize_t wlp_discover(struct wlp *wlp)
{
	int result = 0;
	struct device *dev = &wlp->rc->uwb_dev.dev;

	mutex_lock(&wlp->nbmutex);
	/* Clear current neighborhood cache. */
	__wlp_neighbors_release(wlp);
	/* Determine which devices in neighborhood. Repopulate cache. */
	result = uwb_dev_for_each(wlp->rc, wlp_add_neighbor_helper, wlp);
	if (result < 0) {
		/* May have partial neighbor information, release all. */
		__wlp_neighbors_release(wlp);
		goto error_dev_for_each;
	}
	/* Discover the properties of devices in neighborhood. */
	result = wlp_discover_all_neighbors(wlp);
	/* In case of failure we still print our partial results. */
	if (result < 0) {
		dev_err(dev, "Unable to fully discover neighborhood. \n");
		result = 0;
	}
error_dev_for_each:
	mutex_unlock(&wlp->nbmutex);
	return result;
}

/**
 * Handle events from UWB stack
 *
 * We handle events conservatively. If a neighbor goes off the air we
 * remove it from the neighborhood. If an association process is in
 * progress this function will block waiting for the nbmutex to become
 * free. The association process will thus be allowed to complete before it
 * is removed.
 */
static
void wlp_uwb_notifs_cb(void *_wlp, struct uwb_dev *uwb_dev,
		       enum uwb_notifs event)
{
	struct wlp *wlp = _wlp;
	struct device *dev = &wlp->rc->uwb_dev.dev;
	struct wlp_neighbor_e *neighbor, *next;
	int result;
	switch (event) {
	case UWB_NOTIF_ONAIR:
		result = wlp_eda_create_node(&wlp->eda,
					     uwb_dev->mac_addr.data,
					     &uwb_dev->dev_addr);
		if (result < 0)
			dev_err(dev, "WLP: Unable to add new neighbor "
				"%02x:%02x to EDA cache.\n",
				uwb_dev->dev_addr.data[1],
				uwb_dev->dev_addr.data[0]);
		break;
	case UWB_NOTIF_OFFAIR:
		wlp_eda_rm_node(&wlp->eda, &uwb_dev->dev_addr);
		mutex_lock(&wlp->nbmutex);
		list_for_each_entry_safe(neighbor, next, &wlp->neighbors, node) {
			if (neighbor->uwb_dev == uwb_dev)
				__wlp_neighbor_release(neighbor);
		}
		mutex_unlock(&wlp->nbmutex);
		break;
	default:
		dev_err(dev, "don't know how to handle event %d from uwb\n",
				event);
	}
}

static void wlp_channel_changed(struct uwb_pal *pal, int channel)
{
	struct wlp *wlp = container_of(pal, struct wlp, pal);

	if (channel < 0)
		netif_carrier_off(wlp->ndev);
	else
		netif_carrier_on(wlp->ndev);
}

int wlp_setup(struct wlp *wlp, struct uwb_rc *rc, struct net_device *ndev)
{
	int result;

	BUG_ON(wlp->fill_device_info == NULL);
	BUG_ON(wlp->xmit_frame == NULL);
	BUG_ON(wlp->stop_queue == NULL);
	BUG_ON(wlp->start_queue == NULL);

	wlp->rc = rc;
	wlp->ndev = ndev;
	wlp_eda_init(&wlp->eda);/* Set up address cache */
	wlp->uwb_notifs_handler.cb = wlp_uwb_notifs_cb;
	wlp->uwb_notifs_handler.data = wlp;
	uwb_notifs_register(rc, &wlp->uwb_notifs_handler);

	uwb_pal_init(&wlp->pal);
	wlp->pal.rc = rc;
	wlp->pal.channel_changed = wlp_channel_changed;
	result = uwb_pal_register(&wlp->pal);
	if (result < 0)
		uwb_notifs_deregister(wlp->rc, &wlp->uwb_notifs_handler);

	return result;
}
EXPORT_SYMBOL_GPL(wlp_setup);

void wlp_remove(struct wlp *wlp)
{
	wlp_neighbors_release(wlp);
	uwb_pal_unregister(&wlp->pal);
	uwb_notifs_deregister(wlp->rc, &wlp->uwb_notifs_handler);
	wlp_eda_release(&wlp->eda);
	mutex_lock(&wlp->mutex);
	if (wlp->dev_info != NULL)
		kfree(wlp->dev_info);
	mutex_unlock(&wlp->mutex);
	wlp->rc = NULL;
}
EXPORT_SYMBOL_GPL(wlp_remove);

/**
 * wlp_reset_all - reset the WLP hardware
 * @wlp: the WLP device to reset.
 *
 * This schedules a full hardware reset of the WLP device.  The radio
 * controller and any other PALs will also be reset.
 */
void wlp_reset_all(struct wlp *wlp)
{
	uwb_rc_reset_all(wlp->rc);
}
EXPORT_SYMBOL_GPL(wlp_reset_all);

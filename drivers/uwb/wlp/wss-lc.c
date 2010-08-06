/*
 * WiMedia Logical Link Control Protocol (WLP)
 *
 * Copyright (C) 2007 Intel Corporation
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
 * Implementation of the WLP association protocol.
 *
 * FIXME: Docs
 *
 * A UWB network interface will configure a WSS through wlp_wss_setup() after
 * the interface has been assigned a MAC address, typically after
 * "ifconfig" has been called. When the interface goes down it should call
 * wlp_wss_remove().
 *
 * When the WSS is ready for use the user interacts via sysfs to create,
 * discover, and activate WSS.
 *
 * wlp_wss_enroll_activate()
 *
 * wlp_wss_create_activate()
 * 	wlp_wss_set_wssid_hash()
 * 		wlp_wss_comp_wssid_hash()
 * 	wlp_wss_sel_bcast_addr()
 * 	wlp_wss_sysfs_add()
 *
 * Called when no more references to WSS exist:
 * 	wlp_wss_release()
 * 		wlp_wss_reset()
 */
#include <linux/etherdevice.h> /* for is_valid_ether_addr */
#include <linux/skbuff.h>
#include <linux/slab.h>
#include <linux/wlp.h>

#include "wlp-internal.h"

size_t wlp_wss_key_print(char *buf, size_t bufsize, u8 *key)
{
	size_t result;

	result = scnprintf(buf, bufsize,
			  "%02x %02x %02x %02x %02x %02x "
			  "%02x %02x %02x %02x %02x %02x "
			  "%02x %02x %02x %02x",
			  key[0], key[1], key[2], key[3],
			  key[4], key[5], key[6], key[7],
			  key[8], key[9], key[10], key[11],
			  key[12], key[13], key[14], key[15]);
	return result;
}

/**
 * Compute WSSID hash
 * WLP Draft 0.99 [7.2.1]
 *
 * The WSSID hash for a WSSID is the result of an octet-wise exclusive-OR
 * of all octets in the WSSID.
 */
static
u8 wlp_wss_comp_wssid_hash(struct wlp_uuid *wssid)
{
	return wssid->data[0]  ^ wssid->data[1]  ^ wssid->data[2]
	       ^ wssid->data[3]  ^ wssid->data[4]  ^ wssid->data[5]
	       ^ wssid->data[6]  ^ wssid->data[7]  ^ wssid->data[8]
	       ^ wssid->data[9]  ^ wssid->data[10] ^ wssid->data[11]
	       ^ wssid->data[12] ^ wssid->data[13] ^ wssid->data[14]
	       ^ wssid->data[15];
}

/**
 * Select a multicast EUI-48 for the WSS broadcast address.
 * WLP Draft 0.99 [7.2.1]
 *
 * Selected based on the WiMedia Alliance OUI, 00-13-88, within the WLP
 * range, [01-13-88-00-01-00, 01-13-88-00-01-FF] inclusive.
 *
 * This address is currently hardcoded.
 * FIXME?
 */
static
struct uwb_mac_addr wlp_wss_sel_bcast_addr(struct wlp_wss *wss)
{
	struct uwb_mac_addr bcast = {
		.data = { 0x01, 0x13, 0x88, 0x00, 0x01, 0x00 }
	};
	return bcast;
}

/**
 * Clear the contents of the WSS structure - all except kobj, mutex, virtual
 *
 * We do not want to reinitialize - the internal kobj should not change as
 * it still points to the parent received during setup. The mutex should
 * remain also. We thus just reset values individually.
 * The virutal address assigned to WSS will remain the same for the
 * lifetime of the WSS. We only reset the fields that can change during its
 * lifetime.
 */
void wlp_wss_reset(struct wlp_wss *wss)
{
	memset(&wss->wssid, 0, sizeof(wss->wssid));
	wss->hash = 0;
	memset(&wss->name[0], 0, sizeof(wss->name));
	memset(&wss->bcast, 0, sizeof(wss->bcast));
	wss->secure_status = WLP_WSS_UNSECURE;
	memset(&wss->master_key[0], 0, sizeof(wss->master_key));
	wss->tag = 0;
	wss->state = WLP_WSS_STATE_NONE;
}

/**
 * Create sysfs infrastructure for WSS
 *
 * The WSS is configured to have the interface as parent (see wlp_wss_setup())
 * a new sysfs directory that includes wssid as its name is created in the
 * interface's sysfs directory. The group of files interacting with WSS are
 * created also.
 */
static
int wlp_wss_sysfs_add(struct wlp_wss *wss, char *wssid_str)
{
	struct wlp *wlp = container_of(wss, struct wlp, wss);
	struct device *dev = &wlp->rc->uwb_dev.dev;
	int result;

	result = kobject_set_name(&wss->kobj, "wss-%s", wssid_str);
	if (result < 0)
		return result;
	wss->kobj.ktype = &wss_ktype;
	result = kobject_init_and_add(&wss->kobj,
			&wss_ktype, wss->kobj.parent, "wlp");
	if (result < 0) {
		dev_err(dev, "WLP: Cannot register WSS kobject.\n");
		goto error_kobject_register;
	}
	result = sysfs_create_group(&wss->kobj, &wss_attr_group);
	if (result < 0) {
		dev_err(dev, "WLP: Cannot register WSS attributes: %d\n",
			result);
		goto error_sysfs_create_group;
	}
	return 0;
error_sysfs_create_group:

	kobject_put(&wss->kobj); /* will free name if needed */
	return result;
error_kobject_register:
	kfree(wss->kobj.name);
	wss->kobj.name = NULL;
	wss->kobj.ktype = NULL;
	return result;
}


/**
 * Release WSS
 *
 * No more references exist to this WSS. We should undo everything that was
 * done in wlp_wss_create_activate() except removing the group. The group
 * is not removed because an object can be unregistered before the group is
 * created. We also undo any additional operations on the WSS after this
 * (addition of members).
 *
 * If memory was allocated for the kobject's name then it will
 * be freed by the kobject system during this time.
 *
 * The EDA cache is removed and reinitialized when the WSS is removed. We
 * thus loose knowledge of members of this WSS at that time and need not do
 * it here.
 */
void wlp_wss_release(struct kobject *kobj)
{
	struct wlp_wss *wss = container_of(kobj, struct wlp_wss, kobj);

	wlp_wss_reset(wss);
}

/**
 * Enroll into a WSS using provided neighbor as registrar
 *
 * First search the neighborhood information to learn which neighbor is
 * referred to, next proceed with enrollment.
 *
 * &wss->mutex is held
 */
static
int wlp_wss_enroll_target(struct wlp_wss *wss, struct wlp_uuid *wssid,
			  struct uwb_dev_addr *dest)
{
	struct wlp *wlp = container_of(wss, struct wlp, wss);
	struct device *dev = &wlp->rc->uwb_dev.dev;
	struct wlp_neighbor_e *neighbor;
	int result = -ENXIO;
	struct uwb_dev_addr *dev_addr;

	mutex_lock(&wlp->nbmutex);
	list_for_each_entry(neighbor, &wlp->neighbors, node) {
		dev_addr = &neighbor->uwb_dev->dev_addr;
		if (!memcmp(dest, dev_addr, sizeof(*dest))) {
			result = wlp_enroll_neighbor(wlp, neighbor, wss, wssid);
			break;
		}
	}
	if (result == -ENXIO)
		dev_err(dev, "WLP: Cannot find neighbor %02x:%02x. \n",
			dest->data[1], dest->data[0]);
	mutex_unlock(&wlp->nbmutex);
	return result;
}

/**
 * Enroll into a WSS previously discovered
 *
 * User provides WSSID of WSS, search for neighbor that has this WSS
 * activated and attempt to enroll.
 *
 * &wss->mutex is held
 */
static
int wlp_wss_enroll_discovered(struct wlp_wss *wss, struct wlp_uuid *wssid)
{
	struct wlp *wlp = container_of(wss, struct wlp, wss);
	struct device *dev = &wlp->rc->uwb_dev.dev;
	struct wlp_neighbor_e *neighbor;
	struct wlp_wssid_e *wssid_e;
	char buf[WLP_WSS_UUID_STRSIZE];
	int result = -ENXIO;


	mutex_lock(&wlp->nbmutex);
	list_for_each_entry(neighbor, &wlp->neighbors, node) {
		list_for_each_entry(wssid_e, &neighbor->wssid, node) {
			if (!memcmp(wssid, &wssid_e->wssid, sizeof(*wssid))) {
				result = wlp_enroll_neighbor(wlp, neighbor,
							     wss, wssid);
				if (result == 0) /* enrollment success */
					goto out;
				break;
			}
		}
	}
out:
	if (result == -ENXIO) {
		wlp_wss_uuid_print(buf, sizeof(buf), wssid);
		dev_err(dev, "WLP: Cannot find WSSID %s in cache. \n", buf);
	}
	mutex_unlock(&wlp->nbmutex);
	return result;
}

/**
 * Enroll into WSS with provided WSSID, registrar may be provided
 *
 * @wss: out WSS that will be enrolled
 * @wssid: wssid of neighboring WSS that we want to enroll in
 * @devaddr: registrar can be specified, will be broadcast (ff:ff) if any
 *           neighbor can be used as registrar.
 *
 * &wss->mutex is held
 */
static
int wlp_wss_enroll(struct wlp_wss *wss, struct wlp_uuid *wssid,
		   struct uwb_dev_addr *devaddr)
{
	int result;
	struct wlp *wlp = container_of(wss, struct wlp, wss);
	struct device *dev = &wlp->rc->uwb_dev.dev;
	char buf[WLP_WSS_UUID_STRSIZE];
	struct uwb_dev_addr bcast = {.data = {0xff, 0xff} };

	wlp_wss_uuid_print(buf, sizeof(buf), wssid);

	if (wss->state != WLP_WSS_STATE_NONE) {
		dev_err(dev, "WLP: Already enrolled in WSS %s.\n", buf);
		result = -EEXIST;
		goto error;
	}
	if (!memcmp(&bcast, devaddr, sizeof(bcast)))
		result = wlp_wss_enroll_discovered(wss, wssid);
	else
		result = wlp_wss_enroll_target(wss, wssid, devaddr);
	if (result < 0) {
		dev_err(dev, "WLP: Unable to enroll into WSS %s, result %d \n",
			buf, result);
		goto error;
	}
	dev_dbg(dev, "Successfully enrolled into WSS %s \n", buf);
	result = wlp_wss_sysfs_add(wss, buf);
	if (result < 0) {
		dev_err(dev, "WLP: Unable to set up sysfs for WSS kobject.\n");
		wlp_wss_reset(wss);
	}
error:
	return result;

}

/**
 * Activate given WSS
 *
 * Prior to activation a WSS must be enrolled. To activate a WSS a device
 * includes the WSS hash in the WLP IE in its beacon in each superframe.
 * WLP 0.99 [7.2.5].
 *
 * The WSS tag is also computed at this time. We only support one activated
 * WSS so we can use the hash as a tag - there will never be a conflict.
 *
 * We currently only support one activated WSS so only one WSS hash is
 * included in the WLP IE.
 */
static
int wlp_wss_activate(struct wlp_wss *wss)
{
	struct wlp *wlp = container_of(wss, struct wlp, wss);
	struct device *dev = &wlp->rc->uwb_dev.dev;
	struct uwb_rc *uwb_rc = wlp->rc;
	int result;
	struct {
		struct wlp_ie wlp_ie;
		u8 hash; /* only include one hash */
	} ie_data;

	BUG_ON(wss->state != WLP_WSS_STATE_ENROLLED);
	wss->hash = wlp_wss_comp_wssid_hash(&wss->wssid);
	wss->tag = wss->hash;
	memset(&ie_data, 0, sizeof(ie_data));
	ie_data.wlp_ie.hdr.element_id = UWB_IE_WLP;
	ie_data.wlp_ie.hdr.length = sizeof(ie_data) - sizeof(struct uwb_ie_hdr);
	wlp_ie_set_hash_length(&ie_data.wlp_ie, sizeof(ie_data.hash));
	ie_data.hash = wss->hash;
	result = uwb_rc_ie_add(uwb_rc, &ie_data.wlp_ie.hdr,
			       sizeof(ie_data));
	if (result < 0) {
		dev_err(dev, "WLP: Unable to add WLP IE to beacon. "
			"result = %d.\n", result);
		goto error_wlp_ie;
	}
	wss->state = WLP_WSS_STATE_ACTIVE;
	result = 0;
error_wlp_ie:
	return result;
}

/**
 * Enroll in and activate WSS identified by provided WSSID
 *
 * The neighborhood cache should contain a list of all neighbors and the
 * WSS they have activated. Based on that cache we search which neighbor we
 * can perform the association process with. The user also has option to
 * specify which neighbor it prefers as registrar.
 * Successful enrollment is followed by activation.
 * Successful activation will create the sysfs directory containing
 * specific information regarding this WSS.
 */
int wlp_wss_enroll_activate(struct wlp_wss *wss, struct wlp_uuid *wssid,
			    struct uwb_dev_addr *devaddr)
{
	struct wlp *wlp = container_of(wss, struct wlp, wss);
	struct device *dev = &wlp->rc->uwb_dev.dev;
	int result = 0;
	char buf[WLP_WSS_UUID_STRSIZE];

	mutex_lock(&wss->mutex);
	result = wlp_wss_enroll(wss, wssid, devaddr);
	if (result < 0) {
		wlp_wss_uuid_print(buf, sizeof(buf), &wss->wssid);
		dev_err(dev, "WLP: Enrollment into WSS %s failed.\n", buf);
		goto error_enroll;
	}
	result = wlp_wss_activate(wss);
	if (result < 0) {
		dev_err(dev, "WLP: Unable to activate WSS. Undoing enrollment "
			"result = %d \n", result);
		/* Undo enrollment */
		wlp_wss_reset(wss);
		goto error_activate;
	}
error_activate:
error_enroll:
	mutex_unlock(&wss->mutex);
	return result;
}

/**
 * Create, enroll, and activate a new WSS
 *
 * @wssid: new wssid provided by user
 * @name:  WSS name requested by used.
 * @sec_status: security status requested by user
 *
 * A user requested the creation of a new WSS. All operations are done
 * locally. The new WSS will be stored locally, the hash will be included
 * in the WLP IE, and the sysfs infrastructure for this WSS will be
 * created.
 */
int wlp_wss_create_activate(struct wlp_wss *wss, struct wlp_uuid *wssid,
			    char *name, unsigned sec_status, unsigned accept)
{
	struct wlp *wlp = container_of(wss, struct wlp, wss);
	struct device *dev = &wlp->rc->uwb_dev.dev;
	int result = 0;
	char buf[WLP_WSS_UUID_STRSIZE];

	result = wlp_wss_uuid_print(buf, sizeof(buf), wssid);

	if (!mutex_trylock(&wss->mutex)) {
		dev_err(dev, "WLP: WLP association session in progress.\n");
		return -EBUSY;
	}
	if (wss->state != WLP_WSS_STATE_NONE) {
		dev_err(dev, "WLP: WSS already exists. Not creating new.\n");
		result = -EEXIST;
		goto out;
	}
	if (wss->kobj.parent == NULL) {
		dev_err(dev, "WLP: WSS parent not ready. Is network interface "
		       "up?\n");
		result = -ENXIO;
		goto out;
	}
	if (sec_status == WLP_WSS_SECURE) {
		dev_err(dev, "WLP: FIXME Creation of secure WSS not "
			"supported yet.\n");
		result = -EINVAL;
		goto out;
	}
	wss->wssid = *wssid;
	memcpy(wss->name, name, sizeof(wss->name));
	wss->bcast = wlp_wss_sel_bcast_addr(wss);
	wss->secure_status = sec_status;
	wss->accept_enroll = accept;
	/*wss->virtual_addr is initialized in call to wlp_wss_setup*/
	/* sysfs infrastructure */
	result = wlp_wss_sysfs_add(wss, buf);
	if (result < 0) {
		dev_err(dev, "Cannot set up sysfs for WSS kobject.\n");
		wlp_wss_reset(wss);
		goto out;
	} else
		result = 0;
	wss->state = WLP_WSS_STATE_ENROLLED;
	result = wlp_wss_activate(wss);
	if (result < 0) {
		dev_err(dev, "WLP: Unable to activate WSS. Undoing "
			"enrollment\n");
		wlp_wss_reset(wss);
		goto out;
	}
	result = 0;
out:
	mutex_unlock(&wss->mutex);
	return result;
}

/**
 * Determine if neighbor has WSS activated
 *
 * @returns: 1 if neighbor has WSS activated, zero otherwise
 *
 * This can be done in two ways:
 * - send a C1 frame, parse C2/F0 response
 * - examine the WLP IE sent by the neighbor
 *
 * The WLP IE is not fully supported in hardware so we use the C1/C2 frame
 * exchange to determine if a WSS is activated. Using the WLP IE should be
 * faster and should be used when it becomes possible.
 */
int wlp_wss_is_active(struct wlp *wlp, struct wlp_wss *wss,
		      struct uwb_dev_addr *dev_addr)
{
	int result = 0;
	struct device *dev = &wlp->rc->uwb_dev.dev;
	DECLARE_COMPLETION_ONSTACK(completion);
	struct wlp_session session;
	struct sk_buff  *skb;
	struct wlp_frame_assoc *resp;
	struct wlp_uuid wssid;

	mutex_lock(&wlp->mutex);
	/* Send C1 association frame */
	result = wlp_send_assoc_frame(wlp, wss, dev_addr, WLP_ASSOC_C1);
	if (result < 0) {
		dev_err(dev, "Unable to send C1 frame to neighbor "
			"%02x:%02x (%d)\n", dev_addr->data[1],
			dev_addr->data[0], result);
		result = 0;
		goto out;
	}
	/* Create session, wait for response */
	session.exp_message = WLP_ASSOC_C2;
	session.cb = wlp_session_cb;
	session.cb_priv = &completion;
	session.neighbor_addr = *dev_addr;
	BUG_ON(wlp->session != NULL);
	wlp->session = &session;
	/* Wait for C2/F0 frame */
	result = wait_for_completion_interruptible_timeout(&completion,
						   WLP_PER_MSG_TIMEOUT * HZ);
	if (result == 0) {
		dev_err(dev, "Timeout while sending C1 to neighbor "
			     "%02x:%02x.\n", dev_addr->data[1],
			     dev_addr->data[0]);
		goto out;
	}
	if (result < 0) {
		dev_err(dev, "Unable to send C1 to neighbor %02x:%02x.\n",
			dev_addr->data[1], dev_addr->data[0]);
		result = 0;
		goto out;
	}
	/* Parse message in session->data: it will be either C2 or F0 */
	skb = session.data;
	resp = (void *) skb->data;
	if (resp->type == WLP_ASSOC_F0) {
		result = wlp_parse_f0(wlp, skb);
		if (result < 0)
			dev_err(dev, "WLP:  unable to parse incoming F0 "
				"frame from neighbor %02x:%02x.\n",
				dev_addr->data[1], dev_addr->data[0]);
		result = 0;
		goto error_resp_parse;
	}
	/* WLP version and message type fields have already been parsed */
	result = wlp_get_wssid(wlp, (void *)resp + sizeof(*resp), &wssid,
			       skb->len - sizeof(*resp));
	if (result < 0) {
		dev_err(dev, "WLP: unable to obtain WSSID from C2 frame.\n");
		result = 0;
		goto error_resp_parse;
	}
	if (!memcmp(&wssid, &wss->wssid, sizeof(wssid)))
		result = 1;
	else {
		dev_err(dev, "WLP: Received a C2 frame without matching "
			"WSSID.\n");
		result = 0;
	}
error_resp_parse:
	kfree_skb(skb);
out:
	wlp->session = NULL;
	mutex_unlock(&wlp->mutex);
	return result;
}

/**
 * Activate connection with neighbor by updating EDA cache
 *
 * @wss:       local WSS to which neighbor wants to connect
 * @dev_addr:  neighbor's address
 * @wssid:     neighbor's WSSID - must be same as our WSS's WSSID
 * @tag:       neighbor's WSS tag used to identify frames transmitted by it
 * @virt_addr: neighbor's virtual EUI-48
 */
static
int wlp_wss_activate_connection(struct wlp *wlp, struct wlp_wss *wss,
				struct uwb_dev_addr *dev_addr,
				struct wlp_uuid *wssid, u8 *tag,
				struct uwb_mac_addr *virt_addr)
{
	struct device *dev = &wlp->rc->uwb_dev.dev;
	int result = 0;

	if (!memcmp(wssid, &wss->wssid, sizeof(*wssid))) {
		/* Update EDA cache */
		result = wlp_eda_update_node(&wlp->eda, dev_addr, wss,
					     (void *) virt_addr->data, *tag,
					     WLP_WSS_CONNECTED);
		if (result < 0)
			dev_err(dev, "WLP: Unable to update EDA cache "
				"with new connected neighbor information.\n");
	} else {
		dev_err(dev, "WLP: Neighbor does not have matching WSSID.\n");
		result = -EINVAL;
	}
	return result;
}

/**
 * Connect to WSS neighbor
 *
 * Use C3/C4 exchange to determine if neighbor has WSS activated and
 * retrieve the WSS tag and virtual EUI-48 of the neighbor.
 */
static
int wlp_wss_connect_neighbor(struct wlp *wlp, struct wlp_wss *wss,
			     struct uwb_dev_addr *dev_addr)
{
	int result;
	struct device *dev = &wlp->rc->uwb_dev.dev;
	struct wlp_uuid wssid;
	u8 tag;
	struct uwb_mac_addr virt_addr;
	DECLARE_COMPLETION_ONSTACK(completion);
	struct wlp_session session;
	struct wlp_frame_assoc *resp;
	struct sk_buff *skb;

	mutex_lock(&wlp->mutex);
	/* Send C3 association frame */
	result = wlp_send_assoc_frame(wlp, wss, dev_addr, WLP_ASSOC_C3);
	if (result < 0) {
		dev_err(dev, "Unable to send C3 frame to neighbor "
			"%02x:%02x (%d)\n", dev_addr->data[1],
			dev_addr->data[0], result);
		goto out;
	}
	/* Create session, wait for response */
	session.exp_message = WLP_ASSOC_C4;
	session.cb = wlp_session_cb;
	session.cb_priv = &completion;
	session.neighbor_addr = *dev_addr;
	BUG_ON(wlp->session != NULL);
	wlp->session = &session;
	/* Wait for C4/F0 frame */
	result = wait_for_completion_interruptible_timeout(&completion,
						   WLP_PER_MSG_TIMEOUT * HZ);
	if (result == 0) {
		dev_err(dev, "Timeout while sending C3 to neighbor "
			     "%02x:%02x.\n", dev_addr->data[1],
			     dev_addr->data[0]);
		result = -ETIMEDOUT;
		goto out;
	}
	if (result < 0) {
		dev_err(dev, "Unable to send C3 to neighbor %02x:%02x.\n",
			dev_addr->data[1], dev_addr->data[0]);
		goto out;
	}
	/* Parse message in session->data: it will be either C4 or F0 */
	skb = session.data;
	resp = (void *) skb->data;
	if (resp->type == WLP_ASSOC_F0) {
		result = wlp_parse_f0(wlp, skb);
		if (result < 0)
			dev_err(dev, "WLP: unable to parse incoming F0 "
				"frame from neighbor %02x:%02x.\n",
				dev_addr->data[1], dev_addr->data[0]);
		result = -EINVAL;
		goto error_resp_parse;
	}
	result = wlp_parse_c3c4_frame(wlp, skb, &wssid, &tag, &virt_addr);
	if (result < 0) {
		dev_err(dev, "WLP: Unable to parse C4 frame from neighbor.\n");
		goto error_resp_parse;
	}
	result = wlp_wss_activate_connection(wlp, wss, dev_addr, &wssid, &tag,
					     &virt_addr);
	if (result < 0) {
		dev_err(dev, "WLP: Unable to activate connection to "
			"neighbor %02x:%02x.\n", dev_addr->data[1],
			dev_addr->data[0]);
		goto error_resp_parse;
	}
error_resp_parse:
	kfree_skb(skb);
out:
	/* Record that we unsuccessfully tried to connect to this neighbor */
	if (result < 0)
		wlp_eda_update_node_state(&wlp->eda, dev_addr,
					  WLP_WSS_CONNECT_FAILED);
	wlp->session = NULL;
	mutex_unlock(&wlp->mutex);
	return result;
}

/**
 * Connect to neighbor with common WSS, send pending frame
 *
 * This function is scheduled when a frame is destined to a neighbor with
 * which we do not have a connection. A copy of the EDA cache entry is
 * provided - not the actual cache entry (because it is protected by a
 * spinlock).
 *
 * First determine if neighbor has the same WSS activated, connect if it
 * does. The C3/C4 exchange is dual purpose to determine if neighbor has
 * WSS activated and proceed with the connection.
 *
 * The frame that triggered the connection setup is sent after connection
 * setup.
 *
 * network queue is stopped - we need to restart when done
 *
 */
static
void wlp_wss_connect_send(struct work_struct *ws)
{
	struct wlp_assoc_conn_ctx *conn_ctx = container_of(ws,
						  struct wlp_assoc_conn_ctx,
						  ws);
	struct wlp *wlp = conn_ctx->wlp;
	struct sk_buff *skb = conn_ctx->skb;
	struct wlp_eda_node *eda_entry = &conn_ctx->eda_entry;
	struct uwb_dev_addr *dev_addr = &eda_entry->dev_addr;
	struct wlp_wss *wss = &wlp->wss;
	int result;
	struct device *dev = &wlp->rc->uwb_dev.dev;

	mutex_lock(&wss->mutex);
	if (wss->state < WLP_WSS_STATE_ACTIVE) {
		if (printk_ratelimit())
			dev_err(dev, "WLP: Attempting to connect with "
				"WSS that is not active or connected.\n");
		dev_kfree_skb(skb);
		goto out;
	}
	/* Establish connection - send C3 rcv C4 */
	result = wlp_wss_connect_neighbor(wlp, wss, dev_addr);
	if (result < 0) {
		if (printk_ratelimit())
			dev_err(dev, "WLP: Unable to establish connection "
				"with neighbor %02x:%02x.\n",
				dev_addr->data[1], dev_addr->data[0]);
		dev_kfree_skb(skb);
		goto out;
	}
	/* EDA entry changed, update the local copy being used */
	result = wlp_copy_eda_node(&wlp->eda, dev_addr, eda_entry);
	if (result < 0) {
		if (printk_ratelimit())
			dev_err(dev, "WLP: Cannot find EDA entry for "
				"neighbor %02x:%02x \n",
				dev_addr->data[1], dev_addr->data[0]);
	}
	result = wlp_wss_prep_hdr(wlp, eda_entry, skb);
	if (result < 0) {
		if (printk_ratelimit())
			dev_err(dev, "WLP: Unable to prepare frame header for "
				"transmission (neighbor %02x:%02x). \n",
				dev_addr->data[1], dev_addr->data[0]);
		dev_kfree_skb(skb);
		goto out;
	}
	BUG_ON(wlp->xmit_frame == NULL);
	result = wlp->xmit_frame(wlp, skb, dev_addr);
	if (result < 0) {
		if (printk_ratelimit())
			dev_err(dev, "WLP: Unable to transmit frame: %d\n",
				result);
		if (result == -ENXIO)
			dev_err(dev, "WLP: Is network interface up? \n");
		/* We could try again ... */
		dev_kfree_skb(skb);/*we need to free if tx fails */
	}
out:
	kfree(conn_ctx);
	BUG_ON(wlp->start_queue == NULL);
	wlp->start_queue(wlp);
	mutex_unlock(&wss->mutex);
}

/**
 * Add WLP header to outgoing skb
 *
 * @eda_entry: pointer to neighbor's entry in the EDA cache
 * @_skb:      skb containing data destined to the neighbor
 */
int wlp_wss_prep_hdr(struct wlp *wlp, struct wlp_eda_node *eda_entry,
		     void *_skb)
{
	struct device *dev = &wlp->rc->uwb_dev.dev;
	int result = 0;
	unsigned char *eth_addr = eda_entry->eth_addr;
	struct uwb_dev_addr *dev_addr = &eda_entry->dev_addr;
	struct sk_buff *skb = _skb;
	struct wlp_frame_std_abbrv_hdr *std_hdr;

	if (eda_entry->state == WLP_WSS_CONNECTED) {
		/* Add WLP header */
		BUG_ON(skb_headroom(skb) < sizeof(*std_hdr));
		std_hdr = (void *) __skb_push(skb, sizeof(*std_hdr));
		std_hdr->hdr.mux_hdr = cpu_to_le16(WLP_PROTOCOL_ID);
		std_hdr->hdr.type = WLP_FRAME_STANDARD;
		std_hdr->tag = eda_entry->wss->tag;
	} else {
		if (printk_ratelimit())
			dev_err(dev, "WLP: Destination neighbor (Ethernet: "
				"%02x:%02x:%02x:%02x:%02x:%02x, Dev: "
				"%02x:%02x) is not connected. \n", eth_addr[0],
				eth_addr[1], eth_addr[2], eth_addr[3],
				eth_addr[4], eth_addr[5], dev_addr->data[1],
				dev_addr->data[0]);
		result = -EINVAL;
	}
	return result;
}


/**
 * Prepare skb for neighbor: connect if not already and prep WLP header
 *
 * This function is called in interrupt context, but it needs to sleep. We
 * temporarily stop the net queue to establish the WLP connection.
 * Setup of the WLP connection and restart of queue is scheduled
 * on the default work queue.
 *
 * run with eda->lock held (spinlock)
 */
int wlp_wss_connect_prep(struct wlp *wlp, struct wlp_eda_node *eda_entry,
			 void *_skb)
{
	int result = 0;
	struct device *dev = &wlp->rc->uwb_dev.dev;
	struct sk_buff *skb = _skb;
	struct wlp_assoc_conn_ctx *conn_ctx;

	if (eda_entry->state == WLP_WSS_UNCONNECTED) {
		/* We don't want any more packets while we set up connection */
		BUG_ON(wlp->stop_queue == NULL);
		wlp->stop_queue(wlp);
		conn_ctx = kmalloc(sizeof(*conn_ctx), GFP_ATOMIC);
		if (conn_ctx == NULL) {
			if (printk_ratelimit())
				dev_err(dev, "WLP: Unable to allocate memory "
					"for connection handling.\n");
			result = -ENOMEM;
			goto out;
		}
		conn_ctx->wlp = wlp;
		conn_ctx->skb = skb;
		conn_ctx->eda_entry = *eda_entry;
		INIT_WORK(&conn_ctx->ws, wlp_wss_connect_send);
		schedule_work(&conn_ctx->ws);
		result = 1;
	} else if (eda_entry->state == WLP_WSS_CONNECT_FAILED) {
		/* Previous connection attempts failed, don't retry - see
		 * conditions for connection in WLP 0.99 [7.6.2] */
		if (printk_ratelimit())
			dev_err(dev, "Could not connect to neighbor "
			 "previously. Not retrying. \n");
		result = -ENONET;
		goto out;
	} else /* eda_entry->state == WLP_WSS_CONNECTED */
		result = wlp_wss_prep_hdr(wlp, eda_entry, skb);
out:
	return result;
}

/**
 * Emulate broadcast: copy skb, send copy to neighbor (connect if not already)
 *
 * We need to copy skbs in the case where we emulate broadcast through
 * unicast. We copy instead of clone because we are modifying the data of
 * the frame after copying ... clones share data so we cannot emulate
 * broadcast using clones.
 *
 * run with eda->lock held (spinlock)
 */
int wlp_wss_send_copy(struct wlp *wlp, struct wlp_eda_node *eda_entry,
		      void *_skb)
{
	int result = -ENOMEM;
	struct device *dev = &wlp->rc->uwb_dev.dev;
	struct sk_buff *skb = _skb;
	struct sk_buff *copy;
	struct uwb_dev_addr *dev_addr = &eda_entry->dev_addr;

	copy = skb_copy(skb, GFP_ATOMIC);
	if (copy == NULL) {
		if (printk_ratelimit())
			dev_err(dev, "WLP: Unable to copy skb for "
				"transmission.\n");
		goto out;
	}
	result = wlp_wss_connect_prep(wlp, eda_entry, copy);
	if (result < 0) {
		if (printk_ratelimit())
			dev_err(dev, "WLP: Unable to connect/send skb "
				"to neighbor.\n");
		dev_kfree_skb_irq(copy);
		goto out;
	} else if (result == 1)
		/* Frame will be transmitted separately */
		goto out;
	BUG_ON(wlp->xmit_frame == NULL);
	result = wlp->xmit_frame(wlp, copy, dev_addr);
	if (result < 0) {
		if (printk_ratelimit())
			dev_err(dev, "WLP: Unable to transmit frame: %d\n",
				result);
		if ((result == -ENXIO) && printk_ratelimit())
			dev_err(dev, "WLP: Is network interface up? \n");
		/* We could try again ... */
		dev_kfree_skb_irq(copy);/*we need to free if tx fails */
	}
out:
	return result;
}


/**
 * Setup WSS
 *
 * Should be called by network driver after the interface has been given a
 * MAC address.
 */
int wlp_wss_setup(struct net_device *net_dev, struct wlp_wss *wss)
{
	struct wlp *wlp = container_of(wss, struct wlp, wss);
	struct device *dev = &wlp->rc->uwb_dev.dev;
	int result = 0;

	mutex_lock(&wss->mutex);
	wss->kobj.parent = &net_dev->dev.kobj;
	if (!is_valid_ether_addr(net_dev->dev_addr)) {
		dev_err(dev, "WLP: Invalid MAC address. Cannot use for"
		       "virtual.\n");
		result = -EINVAL;
		goto out;
	}
	memcpy(wss->virtual_addr.data, net_dev->dev_addr,
	       sizeof(wss->virtual_addr.data));
out:
	mutex_unlock(&wss->mutex);
	return result;
}
EXPORT_SYMBOL_GPL(wlp_wss_setup);

/**
 * Remove WSS
 *
 * Called by client that configured WSS through wlp_wss_setup(). This
 * function is called when client no longer needs WSS, eg. client shuts
 * down.
 *
 * We remove the WLP IE from the beacon before initiating local cleanup.
 */
void wlp_wss_remove(struct wlp_wss *wss)
{
	struct wlp *wlp = container_of(wss, struct wlp, wss);

	mutex_lock(&wss->mutex);
	if (wss->state == WLP_WSS_STATE_ACTIVE)
		uwb_rc_ie_rm(wlp->rc, UWB_IE_WLP);
	if (wss->state != WLP_WSS_STATE_NONE) {
		sysfs_remove_group(&wss->kobj, &wss_attr_group);
		kobject_put(&wss->kobj);
	}
	wss->kobj.parent = NULL;
	memset(&wss->virtual_addr, 0, sizeof(wss->virtual_addr));
	/* Cleanup EDA cache */
	wlp_eda_release(&wlp->eda);
	wlp_eda_init(&wlp->eda);
	mutex_unlock(&wss->mutex);
}
EXPORT_SYMBOL_GPL(wlp_wss_remove);

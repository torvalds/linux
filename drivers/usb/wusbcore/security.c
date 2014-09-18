/*
 * Wireless USB Host Controller
 * Security support: encryption enablement, etc
 *
 * Copyright (C) 2006 Intel Corporation
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
 */
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/usb/ch9.h>
#include <linux/random.h>
#include <linux/export.h>
#include "wusbhc.h"

static void wusbhc_gtk_rekey_work(struct work_struct *work);

int wusbhc_sec_create(struct wusbhc *wusbhc)
{
	wusbhc->gtk.descr.bLength = sizeof(wusbhc->gtk.descr) +
		sizeof(wusbhc->gtk.data);
	wusbhc->gtk.descr.bDescriptorType = USB_DT_KEY;
	wusbhc->gtk.descr.bReserved = 0;
	wusbhc->gtk_index = 0;

	INIT_WORK(&wusbhc->gtk_rekey_work, wusbhc_gtk_rekey_work);

	return 0;
}


/* Called when the HC is destroyed */
void wusbhc_sec_destroy(struct wusbhc *wusbhc)
{
}


/**
 * wusbhc_next_tkid - generate a new, currently unused, TKID
 * @wusbhc:   the WUSB host controller
 * @wusb_dev: the device whose PTK the TKID is for
 *            (or NULL for a TKID for a GTK)
 *
 * The generated TKID consists of two parts: the device's authenticated
 * address (or 0 or a GTK); and an incrementing number.  This ensures
 * that TKIDs cannot be shared between devices and by the time the
 * incrementing number wraps around the older TKIDs will no longer be
 * in use (a maximum of two keys may be active at any one time).
 */
static u32 wusbhc_next_tkid(struct wusbhc *wusbhc, struct wusb_dev *wusb_dev)
{
	u32 *tkid;
	u32 addr;

	if (wusb_dev == NULL) {
		tkid = &wusbhc->gtk_tkid;
		addr = 0;
	} else {
		tkid = &wusb_port_by_idx(wusbhc, wusb_dev->port_idx)->ptk_tkid;
		addr = wusb_dev->addr & 0x7f;
	}

	*tkid = (addr << 8) | ((*tkid + 1) & 0xff);

	return *tkid;
}

static void wusbhc_generate_gtk(struct wusbhc *wusbhc)
{
	const size_t key_size = sizeof(wusbhc->gtk.data);
	u32 tkid;

	tkid = wusbhc_next_tkid(wusbhc, NULL);

	wusbhc->gtk.descr.tTKID[0] = (tkid >>  0) & 0xff;
	wusbhc->gtk.descr.tTKID[1] = (tkid >>  8) & 0xff;
	wusbhc->gtk.descr.tTKID[2] = (tkid >> 16) & 0xff;

	get_random_bytes(wusbhc->gtk.descr.bKeyData, key_size);
}

/**
 * wusbhc_sec_start - start the security management process
 * @wusbhc: the WUSB host controller
 *
 * Generate and set an initial GTK on the host controller.
 *
 * Called when the HC is started.
 */
int wusbhc_sec_start(struct wusbhc *wusbhc)
{
	const size_t key_size = sizeof(wusbhc->gtk.data);
	int result;

	wusbhc_generate_gtk(wusbhc);

	result = wusbhc->set_gtk(wusbhc, wusbhc->gtk_tkid,
				&wusbhc->gtk.descr.bKeyData, key_size);
	if (result < 0)
		dev_err(wusbhc->dev, "cannot set GTK for the host: %d\n",
			result);

	return result;
}

/**
 * wusbhc_sec_stop - stop the security management process
 * @wusbhc: the WUSB host controller
 *
 * Wait for any pending GTK rekeys to stop.
 */
void wusbhc_sec_stop(struct wusbhc *wusbhc)
{
	cancel_work_sync(&wusbhc->gtk_rekey_work);
}


/** @returns encryption type name */
const char *wusb_et_name(u8 x)
{
	switch (x) {
	case USB_ENC_TYPE_UNSECURE:	return "unsecure";
	case USB_ENC_TYPE_WIRED:	return "wired";
	case USB_ENC_TYPE_CCM_1:	return "CCM-1";
	case USB_ENC_TYPE_RSA_1:	return "RSA-1";
	default:			return "unknown";
	}
}
EXPORT_SYMBOL_GPL(wusb_et_name);

/*
 * Set the device encryption method
 *
 * We tell the device which encryption method to use; we do this when
 * setting up the device's security.
 */
static int wusb_dev_set_encryption(struct usb_device *usb_dev, int value)
{
	int result;
	struct device *dev = &usb_dev->dev;
	struct wusb_dev *wusb_dev = usb_dev->wusb_dev;

	if (value) {
		value = wusb_dev->ccm1_etd.bEncryptionValue;
	} else {
		/* FIXME: should be wusb_dev->etd[UNSECURE].bEncryptionValue */
		value = 0;
	}
	/* Set device's */
	result = usb_control_msg(usb_dev, usb_sndctrlpipe(usb_dev, 0),
			USB_REQ_SET_ENCRYPTION,
			USB_DIR_OUT | USB_TYPE_STANDARD | USB_RECIP_DEVICE,
			value, 0, NULL, 0, USB_CTRL_SET_TIMEOUT);
	if (result < 0)
		dev_err(dev, "Can't set device's WUSB encryption to "
			"%s (value %d): %d\n",
			wusb_et_name(wusb_dev->ccm1_etd.bEncryptionType),
			wusb_dev->ccm1_etd.bEncryptionValue,  result);
	return result;
}

/*
 * Set the GTK to be used by a device.
 *
 * The device must be authenticated.
 */
static int wusb_dev_set_gtk(struct wusbhc *wusbhc, struct wusb_dev *wusb_dev)
{
	struct usb_device *usb_dev = wusb_dev->usb_dev;
	u8 key_index = wusb_key_index(wusbhc->gtk_index,
		WUSB_KEY_INDEX_TYPE_GTK, WUSB_KEY_INDEX_ORIGINATOR_HOST);

	return usb_control_msg(
		usb_dev, usb_sndctrlpipe(usb_dev, 0),
		USB_REQ_SET_DESCRIPTOR,
		USB_DIR_OUT | USB_TYPE_STANDARD | USB_RECIP_DEVICE,
		USB_DT_KEY << 8 | key_index, 0,
		&wusbhc->gtk.descr, wusbhc->gtk.descr.bLength,
		USB_CTRL_SET_TIMEOUT);
}


/* FIXME: prototype for adding security */
int wusb_dev_sec_add(struct wusbhc *wusbhc,
		     struct usb_device *usb_dev, struct wusb_dev *wusb_dev)
{
	int result, bytes, secd_size;
	struct device *dev = &usb_dev->dev;
	struct usb_security_descriptor *secd, *new_secd;
	const struct usb_encryption_descriptor *etd, *ccm1_etd = NULL;
	const void *itr, *top;
	char buf[64];

	secd = kmalloc(sizeof(*secd), GFP_KERNEL);
	if (secd == NULL) {
		result = -ENOMEM;
		goto out;
	}

	result = usb_get_descriptor(usb_dev, USB_DT_SECURITY,
				    0, secd, sizeof(*secd));
	if (result < sizeof(*secd)) {
		dev_err(dev, "Can't read security descriptor or "
			"not enough data: %d\n", result);
		goto out;
	}
	secd_size = le16_to_cpu(secd->wTotalLength);
	new_secd = krealloc(secd, secd_size, GFP_KERNEL);
	if (new_secd == NULL) {
		dev_err(dev,
			"Can't allocate space for security descriptors\n");
		goto out;
	}
	secd = new_secd;
	result = usb_get_descriptor(usb_dev, USB_DT_SECURITY,
				    0, secd, secd_size);
	if (result < secd_size) {
		dev_err(dev, "Can't read security descriptor or "
			"not enough data: %d\n", result);
		goto out;
	}
	bytes = 0;
	itr = &secd[1];
	top = (void *)secd + result;
	while (itr < top) {
		etd = itr;
		if (top - itr < sizeof(*etd)) {
			dev_err(dev, "BUG: bad device security descriptor; "
				"not enough data (%zu vs %zu bytes left)\n",
				top - itr, sizeof(*etd));
			break;
		}
		if (etd->bLength < sizeof(*etd)) {
			dev_err(dev, "BUG: bad device encryption descriptor; "
				"descriptor is too short "
				"(%u vs %zu needed)\n",
				etd->bLength, sizeof(*etd));
			break;
		}
		itr += etd->bLength;
		bytes += snprintf(buf + bytes, sizeof(buf) - bytes,
				  "%s (0x%02x/%02x) ",
				  wusb_et_name(etd->bEncryptionType),
				  etd->bEncryptionValue, etd->bAuthKeyIndex);
		if (etd->bEncryptionType == USB_ENC_TYPE_CCM_1)
			ccm1_etd = etd;
	}
	/* This code only supports CCM1 as of now. */
	/* FIXME: user has to choose which sec mode to use?
	 * In theory we want CCM */
	if (ccm1_etd == NULL) {
		dev_err(dev, "WUSB device doesn't support CCM1 encryption, "
			"can't use!\n");
		result = -EINVAL;
		goto out;
	}
	wusb_dev->ccm1_etd = *ccm1_etd;
	dev_dbg(dev, "supported encryption: %s; using %s (0x%02x/%02x)\n",
		buf, wusb_et_name(ccm1_etd->bEncryptionType),
		ccm1_etd->bEncryptionValue, ccm1_etd->bAuthKeyIndex);
	result = 0;
out:
	kfree(secd);
	return result;
}

void wusb_dev_sec_rm(struct wusb_dev *wusb_dev)
{
	/* Nothing so far */
}

/**
 * Update the address of an unauthenticated WUSB device
 *
 * Once we have successfully authenticated, we take it to addr0 state
 * and then to a normal address.
 *
 * Before the device's address (as known by it) was usb_dev->devnum |
 * 0x80 (unauthenticated address). With this we update it to usb_dev->devnum.
 */
int wusb_dev_update_address(struct wusbhc *wusbhc, struct wusb_dev *wusb_dev)
{
	int result = -ENOMEM;
	struct usb_device *usb_dev = wusb_dev->usb_dev;
	struct device *dev = &usb_dev->dev;
	u8 new_address = wusb_dev->addr & 0x7F;

	/* Set address 0 */
	result = usb_control_msg(usb_dev, usb_sndctrlpipe(usb_dev, 0),
			USB_REQ_SET_ADDRESS,
			USB_DIR_OUT | USB_TYPE_STANDARD | USB_RECIP_DEVICE,
			 0, 0, NULL, 0, USB_CTRL_SET_TIMEOUT);
	if (result < 0) {
		dev_err(dev, "auth failed: can't set address 0: %d\n",
			result);
		goto error_addr0;
	}
	result = wusb_set_dev_addr(wusbhc, wusb_dev, 0);
	if (result < 0)
		goto error_addr0;
	usb_set_device_state(usb_dev, USB_STATE_DEFAULT);
	usb_ep0_reinit(usb_dev);

	/* Set new (authenticated) address. */
	result = usb_control_msg(usb_dev, usb_sndctrlpipe(usb_dev, 0),
			USB_REQ_SET_ADDRESS,
			USB_DIR_OUT | USB_TYPE_STANDARD | USB_RECIP_DEVICE,
			new_address, 0, NULL, 0,
			USB_CTRL_SET_TIMEOUT);
	if (result < 0) {
		dev_err(dev, "auth failed: can't set address %u: %d\n",
			new_address, result);
		goto error_addr;
	}
	result = wusb_set_dev_addr(wusbhc, wusb_dev, new_address);
	if (result < 0)
		goto error_addr;
	usb_set_device_state(usb_dev, USB_STATE_ADDRESS);
	usb_ep0_reinit(usb_dev);
	usb_dev->authenticated = 1;
error_addr:
error_addr0:
	return result;
}

/*
 *
 *
 */
/* FIXME: split and cleanup */
int wusb_dev_4way_handshake(struct wusbhc *wusbhc, struct wusb_dev *wusb_dev,
			    struct wusb_ckhdid *ck)
{
	int result = -ENOMEM;
	struct usb_device *usb_dev = wusb_dev->usb_dev;
	struct device *dev = &usb_dev->dev;
	u32 tkid;
	__le32 tkid_le;
	struct usb_handshake *hs;
	struct aes_ccm_nonce ccm_n;
	u8 mic[8];
	struct wusb_keydvt_in keydvt_in;
	struct wusb_keydvt_out keydvt_out;

	hs = kcalloc(3, sizeof(hs[0]), GFP_KERNEL);
	if (hs == NULL) {
		dev_err(dev, "can't allocate handshake data\n");
		goto error_kzalloc;
	}

	/* We need to turn encryption before beginning the 4way
	 * hshake (WUSB1.0[.3.2.2]) */
	result = wusb_dev_set_encryption(usb_dev, 1);
	if (result < 0)
		goto error_dev_set_encryption;

	tkid = wusbhc_next_tkid(wusbhc, wusb_dev);
	tkid_le = cpu_to_le32(tkid);

	hs[0].bMessageNumber = 1;
	hs[0].bStatus = 0;
	memcpy(hs[0].tTKID, &tkid_le, sizeof(hs[0].tTKID));
	hs[0].bReserved = 0;
	memcpy(hs[0].CDID, &wusb_dev->cdid, sizeof(hs[0].CDID));
	get_random_bytes(&hs[0].nonce, sizeof(hs[0].nonce));
	memset(hs[0].MIC, 0, sizeof(hs[0].MIC)); /* Per WUSB1.0[T7-22] */

	result = usb_control_msg(
		usb_dev, usb_sndctrlpipe(usb_dev, 0),
		USB_REQ_SET_HANDSHAKE,
		USB_DIR_OUT | USB_TYPE_STANDARD | USB_RECIP_DEVICE,
		1, 0, &hs[0], sizeof(hs[0]), USB_CTRL_SET_TIMEOUT);
	if (result < 0) {
		dev_err(dev, "Handshake1: request failed: %d\n", result);
		goto error_hs1;
	}

	/* Handshake 2, from the device -- need to verify fields */
	result = usb_control_msg(
		usb_dev, usb_rcvctrlpipe(usb_dev, 0),
		USB_REQ_GET_HANDSHAKE,
		USB_DIR_IN | USB_TYPE_STANDARD | USB_RECIP_DEVICE,
		2, 0, &hs[1], sizeof(hs[1]), USB_CTRL_GET_TIMEOUT);
	if (result < 0) {
		dev_err(dev, "Handshake2: request failed: %d\n", result);
		goto error_hs2;
	}

	result = -EINVAL;
	if (hs[1].bMessageNumber != 2) {
		dev_err(dev, "Handshake2 failed: bad message number %u\n",
			hs[1].bMessageNumber);
		goto error_hs2;
	}
	if (hs[1].bStatus != 0) {
		dev_err(dev, "Handshake2 failed: bad status %u\n",
			hs[1].bStatus);
		goto error_hs2;
	}
	if (memcmp(hs[0].tTKID, hs[1].tTKID, sizeof(hs[0].tTKID))) {
		dev_err(dev, "Handshake2 failed: TKID mismatch "
			"(#1 0x%02x%02x%02x vs #2 0x%02x%02x%02x)\n",
			hs[0].tTKID[0], hs[0].tTKID[1], hs[0].tTKID[2],
			hs[1].tTKID[0], hs[1].tTKID[1], hs[1].tTKID[2]);
		goto error_hs2;
	}
	if (memcmp(hs[0].CDID, hs[1].CDID, sizeof(hs[0].CDID))) {
		dev_err(dev, "Handshake2 failed: CDID mismatch\n");
		goto error_hs2;
	}

	/* Setup the CCM nonce */
	memset(&ccm_n.sfn, 0, sizeof(ccm_n.sfn)); /* Per WUSB1.0[6.5.2] */
	memcpy(ccm_n.tkid, &tkid_le, sizeof(ccm_n.tkid));
	ccm_n.src_addr = wusbhc->uwb_rc->uwb_dev.dev_addr;
	ccm_n.dest_addr.data[0] = wusb_dev->addr;
	ccm_n.dest_addr.data[1] = 0;

	/* Derive the KCK and PTK from CK, the CCM, H and D nonces */
	memcpy(keydvt_in.hnonce, hs[0].nonce, sizeof(keydvt_in.hnonce));
	memcpy(keydvt_in.dnonce, hs[1].nonce, sizeof(keydvt_in.dnonce));
	result = wusb_key_derive(&keydvt_out, ck->data, &ccm_n, &keydvt_in);
	if (result < 0) {
		dev_err(dev, "Handshake2 failed: cannot derive keys: %d\n",
			result);
		goto error_hs2;
	}

	/* Compute MIC and verify it */
	result = wusb_oob_mic(mic, keydvt_out.kck, &ccm_n, &hs[1]);
	if (result < 0) {
		dev_err(dev, "Handshake2 failed: cannot compute MIC: %d\n",
			result);
		goto error_hs2;
	}

	if (memcmp(hs[1].MIC, mic, sizeof(hs[1].MIC))) {
		dev_err(dev, "Handshake2 failed: MIC mismatch\n");
		goto error_hs2;
	}

	/* Send Handshake3 */
	hs[2].bMessageNumber = 3;
	hs[2].bStatus = 0;
	memcpy(hs[2].tTKID, &tkid_le, sizeof(hs[2].tTKID));
	hs[2].bReserved = 0;
	memcpy(hs[2].CDID, &wusb_dev->cdid, sizeof(hs[2].CDID));
	memcpy(hs[2].nonce, hs[0].nonce, sizeof(hs[2].nonce));
	result = wusb_oob_mic(hs[2].MIC, keydvt_out.kck, &ccm_n, &hs[2]);
	if (result < 0) {
		dev_err(dev, "Handshake3 failed: cannot compute MIC: %d\n",
			result);
		goto error_hs2;
	}

	result = usb_control_msg(
		usb_dev, usb_sndctrlpipe(usb_dev, 0),
		USB_REQ_SET_HANDSHAKE,
		USB_DIR_OUT | USB_TYPE_STANDARD | USB_RECIP_DEVICE,
		3, 0, &hs[2], sizeof(hs[2]), USB_CTRL_SET_TIMEOUT);
	if (result < 0) {
		dev_err(dev, "Handshake3: request failed: %d\n", result);
		goto error_hs3;
	}

	result = wusbhc->set_ptk(wusbhc, wusb_dev->port_idx, tkid,
				 keydvt_out.ptk, sizeof(keydvt_out.ptk));
	if (result < 0)
		goto error_wusbhc_set_ptk;

	result = wusb_dev_set_gtk(wusbhc, wusb_dev);
	if (result < 0) {
		dev_err(dev, "Set GTK for device: request failed: %d\n",
			result);
		goto error_wusbhc_set_gtk;
	}

	/* Update the device's address from unauth to auth */
	if (usb_dev->authenticated == 0) {
		result = wusb_dev_update_address(wusbhc, wusb_dev);
		if (result < 0)
			goto error_dev_update_address;
	}
	result = 0;
	dev_info(dev, "device authenticated\n");

error_dev_update_address:
error_wusbhc_set_gtk:
error_wusbhc_set_ptk:
error_hs3:
error_hs2:
error_hs1:
	memset(hs, 0, 3*sizeof(hs[0]));
	memset(&keydvt_out, 0, sizeof(keydvt_out));
	memset(&keydvt_in, 0, sizeof(keydvt_in));
	memset(&ccm_n, 0, sizeof(ccm_n));
	memset(mic, 0, sizeof(mic));
	if (result < 0)
		wusb_dev_set_encryption(usb_dev, 0);
error_dev_set_encryption:
	kfree(hs);
error_kzalloc:
	return result;
}

/*
 * Once all connected and authenticated devices have received the new
 * GTK, switch the host to using it.
 */
static void wusbhc_gtk_rekey_work(struct work_struct *work)
{
	struct wusbhc *wusbhc = container_of(work,
					struct wusbhc, gtk_rekey_work);
	size_t key_size = sizeof(wusbhc->gtk.data);
	int port_idx;
	struct wusb_dev *wusb_dev, *wusb_dev_next;
	LIST_HEAD(rekey_list);

	mutex_lock(&wusbhc->mutex);
	/* generate the new key */
	wusbhc_generate_gtk(wusbhc);
	/* roll the gtk index. */
	wusbhc->gtk_index = (wusbhc->gtk_index + 1) % (WUSB_KEY_INDEX_MAX + 1);
	/*
	 * Save all connected devices on a list while holding wusbhc->mutex and
	 * take a reference to each one.  Then submit the set key request to
	 * them after releasing the lock in order to avoid a deadlock.
	 */
	for (port_idx = 0; port_idx < wusbhc->ports_max; port_idx++) {
		wusb_dev = wusbhc->port[port_idx].wusb_dev;
		if (!wusb_dev || !wusb_dev->usb_dev
			|| !wusb_dev->usb_dev->authenticated)
			continue;

		wusb_dev_get(wusb_dev);
		list_add_tail(&wusb_dev->rekey_node, &rekey_list);
	}
	mutex_unlock(&wusbhc->mutex);

	/* Submit the rekey requests without holding wusbhc->mutex. */
	list_for_each_entry_safe(wusb_dev, wusb_dev_next, &rekey_list,
		rekey_node) {
		list_del_init(&wusb_dev->rekey_node);
		dev_dbg(&wusb_dev->usb_dev->dev,
			"%s: rekey device at port %d\n",
			__func__, wusb_dev->port_idx);

		if (wusb_dev_set_gtk(wusbhc, wusb_dev) < 0) {
			dev_err(&wusb_dev->usb_dev->dev,
				"%s: rekey device at port %d failed\n",
				__func__, wusb_dev->port_idx);
		}
		wusb_dev_put(wusb_dev);
	}

	/* Switch the host controller to use the new GTK. */
	mutex_lock(&wusbhc->mutex);
	wusbhc->set_gtk(wusbhc, wusbhc->gtk_tkid,
		&wusbhc->gtk.descr.bKeyData, key_size);
	mutex_unlock(&wusbhc->mutex);
}

/**
 * wusbhc_gtk_rekey - generate and distribute a new GTK
 * @wusbhc: the WUSB host controller
 *
 * Generate a new GTK and distribute it to all connected and
 * authenticated devices.  When all devices have the new GTK, the host
 * starts using it.
 *
 * This must be called after every device disconnect (see [WUSB]
 * section 6.2.11.2).
 */
void wusbhc_gtk_rekey(struct wusbhc *wusbhc)
{
	/*
	 * We need to submit a URB to the downstream WUSB devices in order to
	 * change the group key.  This can't be done while holding the
	 * wusbhc->mutex since that is also taken in the urb_enqueue routine
	 * and will cause a deadlock.  Instead, queue a work item to do
	 * it when the lock is not held
	 */
	queue_work(wusbd, &wusbhc->gtk_rekey_work);
}

/*
 * Host Wire Adapter:
 * Driver glue, HWA-specific functions, bridges to WAHC and WUSBHC
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
 * The HWA driver is a simple layer that forwards requests to the WAHC
 * (Wire Adater Host Controller) or WUSBHC (Wireless USB Host
 * Controller) layers.
 *
 * Host Wire Adapter is the 'WUSB 1.0 standard' name for Wireless-USB
 * Host Controller that is connected to your system via USB (a USB
 * dongle that implements a USB host...). There is also a Device Wired
 * Adaptor, DWA (Wireless USB hub) that uses the same mechanism for
 * transferring data (it is after all a USB host connected via
 * Wireless USB), we have a common layer called Wire Adapter Host
 * Controller that does all the hard work. The WUSBHC (Wireless USB
 * Host Controller) is the part common to WUSB Host Controllers, the
 * HWA and the PCI-based one, that is implemented following the WHCI
 * spec. All these layers are implemented in ../wusbcore.
 *
 * The main functions are hwahc_op_urb_{en,de}queue(), that pass the
 * job of converting a URB to a Wire Adapter
 *
 * Entry points:
 *
 *   hwahc_driver_*()   Driver initialization, registration and
 *                      teardown.
 *
 *   hwahc_probe()	New device came up, create an instance for
 *                      it [from device enumeration].
 *
 *   hwahc_disconnect()	Remove device instance [from device
 *                      enumeration].
 *
 *   [__]hwahc_op_*()   Host-Wire-Adaptor specific functions for
 *                      starting/stopping/etc (some might be made also
 *                      DWA).
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/workqueue.h>
#include <linux/wait.h>
#include <linux/completion.h>
#include "../wusbcore/wa-hc.h"
#include "../wusbcore/wusbhc.h"

struct hwahc {
	struct wusbhc wusbhc;	/* has to be 1st */
	struct wahc wa;
};

/*
 * FIXME should be wusbhc
 *
 * NOTE: we need to cache the Cluster ID because later...there is no
 *       way to get it :)
 */
static int __hwahc_set_cluster_id(struct hwahc *hwahc, u8 cluster_id)
{
	int result;
	struct wusbhc *wusbhc = &hwahc->wusbhc;
	struct wahc *wa = &hwahc->wa;
	struct device *dev = &wa->usb_iface->dev;

	result = usb_control_msg(wa->usb_dev, usb_sndctrlpipe(wa->usb_dev, 0),
			WUSB_REQ_SET_CLUSTER_ID,
			USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
			cluster_id,
			wa->usb_iface->cur_altsetting->desc.bInterfaceNumber,
			NULL, 0, 1000 /* FIXME: arbitrary */);
	if (result < 0)
		dev_err(dev, "Cannot set WUSB Cluster ID to 0x%02x: %d\n",
			cluster_id, result);
	else
		wusbhc->cluster_id = cluster_id;
	dev_info(dev, "Wireless USB Cluster ID set to 0x%02x\n", cluster_id);
	return result;
}

static int __hwahc_op_set_num_dnts(struct wusbhc *wusbhc, u8 interval, u8 slots)
{
	struct hwahc *hwahc = container_of(wusbhc, struct hwahc, wusbhc);
	struct wahc *wa = &hwahc->wa;

	return usb_control_msg(wa->usb_dev, usb_sndctrlpipe(wa->usb_dev, 0),
			WUSB_REQ_SET_NUM_DNTS,
			USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
			interval << 8 | slots,
			wa->usb_iface->cur_altsetting->desc.bInterfaceNumber,
			NULL, 0, 1000 /* FIXME: arbitrary */);
}

/*
 * Reset a WUSB host controller and wait for it to complete doing it.
 *
 * @usb_hcd:	Pointer to WUSB Host Controller instance.
 *
 */
static int hwahc_op_reset(struct usb_hcd *usb_hcd)
{
	int result;
	struct wusbhc *wusbhc = usb_hcd_to_wusbhc(usb_hcd);
	struct hwahc *hwahc = container_of(wusbhc, struct hwahc, wusbhc);
	struct device *dev = &hwahc->wa.usb_iface->dev;

	mutex_lock(&wusbhc->mutex);
	wa_nep_disarm(&hwahc->wa);
	result = __wa_set_feature(&hwahc->wa, WA_RESET);
	if (result < 0) {
		dev_err(dev, "error commanding HC to reset: %d\n", result);
		goto error_unlock;
	}
	result = __wa_wait_status(&hwahc->wa, WA_STATUS_RESETTING, 0);
	if (result < 0) {
		dev_err(dev, "error waiting for HC to reset: %d\n", result);
		goto error_unlock;
	}
error_unlock:
	mutex_unlock(&wusbhc->mutex);
	return result;
}

/*
 * FIXME: break this function up
 */
static int hwahc_op_start(struct usb_hcd *usb_hcd)
{
	u8 addr;
	int result;
	struct wusbhc *wusbhc = usb_hcd_to_wusbhc(usb_hcd);
	struct hwahc *hwahc = container_of(wusbhc, struct hwahc, wusbhc);

	result = -ENOSPC;
	mutex_lock(&wusbhc->mutex);
	addr = wusb_cluster_id_get();
	if (addr == 0)
		goto error_cluster_id_get;
	result = __hwahc_set_cluster_id(hwahc, addr);
	if (result < 0)
		goto error_set_cluster_id;

	usb_hcd->uses_new_polling = 1;
	usb_hcd->poll_rh = 1;
	usb_hcd->state = HC_STATE_RUNNING;
	result = 0;
out:
	mutex_unlock(&wusbhc->mutex);
	return result;

error_set_cluster_id:
	wusb_cluster_id_put(wusbhc->cluster_id);
error_cluster_id_get:
	goto out;

}

static int hwahc_op_suspend(struct usb_hcd *usb_hcd, pm_message_t msg)
{
	struct wusbhc *wusbhc = usb_hcd_to_wusbhc(usb_hcd);
	struct hwahc *hwahc = container_of(wusbhc, struct hwahc, wusbhc);
	dev_err(wusbhc->dev, "%s (%p [%p], 0x%lx) UNIMPLEMENTED\n", __func__,
		usb_hcd, hwahc, *(unsigned long *) &msg);
	return -ENOSYS;
}

static int hwahc_op_resume(struct usb_hcd *usb_hcd)
{
	struct wusbhc *wusbhc = usb_hcd_to_wusbhc(usb_hcd);
	struct hwahc *hwahc = container_of(wusbhc, struct hwahc, wusbhc);

	dev_err(wusbhc->dev, "%s (%p [%p]) UNIMPLEMENTED\n", __func__,
		usb_hcd, hwahc);
	return -ENOSYS;
}

/*
 * No need to abort pipes, as when this is called, all the children
 * has been disconnected and that has done it [through
 * usb_disable_interface() -> usb_disable_endpoint() ->
 * hwahc_op_ep_disable() - >rpipe_ep_disable()].
 */
static void hwahc_op_stop(struct usb_hcd *usb_hcd)
{
	struct wusbhc *wusbhc = usb_hcd_to_wusbhc(usb_hcd);

	mutex_lock(&wusbhc->mutex);
	wusb_cluster_id_put(wusbhc->cluster_id);
	mutex_unlock(&wusbhc->mutex);
}

static int hwahc_op_get_frame_number(struct usb_hcd *usb_hcd)
{
	struct wusbhc *wusbhc = usb_hcd_to_wusbhc(usb_hcd);
	struct hwahc *hwahc = container_of(wusbhc, struct hwahc, wusbhc);

	dev_err(wusbhc->dev, "%s (%p [%p]) UNIMPLEMENTED\n", __func__,
		usb_hcd, hwahc);
	return -ENOSYS;
}

static int hwahc_op_urb_enqueue(struct usb_hcd *usb_hcd, struct urb *urb,
				gfp_t gfp)
{
	struct wusbhc *wusbhc = usb_hcd_to_wusbhc(usb_hcd);
	struct hwahc *hwahc = container_of(wusbhc, struct hwahc, wusbhc);

	return wa_urb_enqueue(&hwahc->wa, urb->ep, urb, gfp);
}

static int hwahc_op_urb_dequeue(struct usb_hcd *usb_hcd, struct urb *urb,
				int status)
{
	struct wusbhc *wusbhc = usb_hcd_to_wusbhc(usb_hcd);
	struct hwahc *hwahc = container_of(wusbhc, struct hwahc, wusbhc);

	return wa_urb_dequeue(&hwahc->wa, urb);
}

/*
 * Release resources allocated for an endpoint
 *
 * If there is an associated rpipe to this endpoint, go ahead and put it.
 */
static void hwahc_op_endpoint_disable(struct usb_hcd *usb_hcd,
				      struct usb_host_endpoint *ep)
{
	struct wusbhc *wusbhc = usb_hcd_to_wusbhc(usb_hcd);
	struct hwahc *hwahc = container_of(wusbhc, struct hwahc, wusbhc);

	rpipe_ep_disable(&hwahc->wa, ep);
}

static int __hwahc_op_wusbhc_start(struct wusbhc *wusbhc)
{
	int result;
	struct hwahc *hwahc = container_of(wusbhc, struct hwahc, wusbhc);
	struct device *dev = &hwahc->wa.usb_iface->dev;

	result = __wa_set_feature(&hwahc->wa, WA_ENABLE);
	if (result < 0) {
		dev_err(dev, "error commanding HC to start: %d\n", result);
		goto error_stop;
	}
	result = __wa_wait_status(&hwahc->wa, WA_ENABLE, WA_ENABLE);
	if (result < 0) {
		dev_err(dev, "error waiting for HC to start: %d\n", result);
		goto error_stop;
	}
	result = wa_nep_arm(&hwahc->wa, GFP_KERNEL);
	if (result < 0) {
		dev_err(dev, "cannot listen to notifications: %d\n", result);
		goto error_stop;
	}
	return result;

error_stop:
	__wa_clear_feature(&hwahc->wa, WA_ENABLE);
	return result;
}

static void __hwahc_op_wusbhc_stop(struct wusbhc *wusbhc, int delay)
{
	struct hwahc *hwahc = container_of(wusbhc, struct hwahc, wusbhc);
	struct wahc *wa = &hwahc->wa;
	u8 iface_no = wa->usb_iface->cur_altsetting->desc.bInterfaceNumber;
	int ret;

	ret = usb_control_msg(wa->usb_dev, usb_sndctrlpipe(wa->usb_dev, 0),
			      WUSB_REQ_CHAN_STOP,
			      USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
			      delay * 1000,
			      iface_no,
			      NULL, 0, 1000 /* FIXME: arbitrary */);
	if (ret == 0)
		msleep(delay);

	wa_nep_disarm(&hwahc->wa);
	__wa_stop(&hwahc->wa);
}

/*
 * Set the UWB MAS allocation for the WUSB cluster
 *
 * @stream_index: stream to use (-1 for cancelling the allocation)
 * @mas: mas bitmap to use
 */
static int __hwahc_op_bwa_set(struct wusbhc *wusbhc, s8 stream_index,
			      const struct uwb_mas_bm *mas)
{
	int result;
	struct hwahc *hwahc = container_of(wusbhc, struct hwahc, wusbhc);
	struct wahc *wa = &hwahc->wa;
	struct device *dev = &wa->usb_iface->dev;
	u8 mas_le[UWB_NUM_MAS/8];

	/* Set the stream index */
	result = usb_control_msg(wa->usb_dev, usb_sndctrlpipe(wa->usb_dev, 0),
			WUSB_REQ_SET_STREAM_IDX,
			USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
			stream_index,
			wa->usb_iface->cur_altsetting->desc.bInterfaceNumber,
			NULL, 0, 1000 /* FIXME: arbitrary */);
	if (result < 0) {
		dev_err(dev, "Cannot set WUSB stream index: %d\n", result);
		goto out;
	}
	uwb_mas_bm_copy_le(mas_le, mas);
	/* Set the MAS allocation */
	result = usb_control_msg(wa->usb_dev, usb_sndctrlpipe(wa->usb_dev, 0),
			WUSB_REQ_SET_WUSB_MAS,
			USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
			0, wa->usb_iface->cur_altsetting->desc.bInterfaceNumber,
			mas_le, 32, 1000 /* FIXME: arbitrary */);
	if (result < 0)
		dev_err(dev, "Cannot set WUSB MAS allocation: %d\n", result);
out:
	return result;
}

/*
 * Add an IE to the host's MMC
 *
 * @interval:    See WUSB1.0[8.5.3.1]
 * @repeat_cnt:  See WUSB1.0[8.5.3.1]
 * @handle:      See WUSB1.0[8.5.3.1]
 * @wuie:        Pointer to the header of the WUSB IE data to add.
 *               MUST BE allocated in a kmalloc buffer (no stack or
 *               vmalloc).
 *
 * NOTE: the format of the WUSB IEs for MMCs are different to the
 *       normal MBOA MAC IEs (IE Id + Length in MBOA MAC vs. Length +
 *       Id in WUSB IEs). Standards...you gotta love'em.
 */
static int __hwahc_op_mmcie_add(struct wusbhc *wusbhc, u8 interval,
				u8 repeat_cnt, u8 handle,
				struct wuie_hdr *wuie)
{
	struct hwahc *hwahc = container_of(wusbhc, struct hwahc, wusbhc);
	struct wahc *wa = &hwahc->wa;
	u8 iface_no = wa->usb_iface->cur_altsetting->desc.bInterfaceNumber;

	return usb_control_msg(wa->usb_dev, usb_sndctrlpipe(wa->usb_dev, 0),
			WUSB_REQ_ADD_MMC_IE,
			USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
			interval << 8 | repeat_cnt,
			handle << 8 | iface_no,
			wuie, wuie->bLength, 1000 /* FIXME: arbitrary */);
}

/*
 * Remove an IE to the host's MMC
 *
 * @handle:      See WUSB1.0[8.5.3.1]
 */
static int __hwahc_op_mmcie_rm(struct wusbhc *wusbhc, u8 handle)
{
	struct hwahc *hwahc = container_of(wusbhc, struct hwahc, wusbhc);
	struct wahc *wa = &hwahc->wa;
	u8 iface_no = wa->usb_iface->cur_altsetting->desc.bInterfaceNumber;
	return usb_control_msg(wa->usb_dev, usb_sndctrlpipe(wa->usb_dev, 0),
			WUSB_REQ_REMOVE_MMC_IE,
			USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
			0, handle << 8 | iface_no,
			NULL, 0, 1000 /* FIXME: arbitrary */);
}

/*
 * Update device information for a given fake port
 *
 * @port_idx: Fake port to which device is connected (wusbhc index, not
 *            USB port number).
 */
static int __hwahc_op_dev_info_set(struct wusbhc *wusbhc,
				   struct wusb_dev *wusb_dev)
{
	struct hwahc *hwahc = container_of(wusbhc, struct hwahc, wusbhc);
	struct wahc *wa = &hwahc->wa;
	u8 iface_no = wa->usb_iface->cur_altsetting->desc.bInterfaceNumber;
	struct hwa_dev_info *dev_info;
	int ret;

	/* fill out the Device Info buffer and send it */
	dev_info = kzalloc(sizeof(struct hwa_dev_info), GFP_KERNEL);
	if (!dev_info)
		return -ENOMEM;
	uwb_mas_bm_copy_le(dev_info->bmDeviceAvailability,
			   &wusb_dev->availability);
	dev_info->bDeviceAddress = wusb_dev->addr;

	/*
	 * If the descriptors haven't been read yet, use a default PHY
	 * rate of 53.3 Mbit/s only.  The correct value will be used
	 * when this will be called again as part of the
	 * authentication process (which occurs after the descriptors
	 * have been read).
	 */
	if (wusb_dev->wusb_cap_descr)
		dev_info->wPHYRates = wusb_dev->wusb_cap_descr->wPHYRates;
	else
		dev_info->wPHYRates = cpu_to_le16(USB_WIRELESS_PHY_53);

	ret = usb_control_msg(wa->usb_dev, usb_sndctrlpipe(wa->usb_dev, 0),
			WUSB_REQ_SET_DEV_INFO,
			USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
			0, wusb_dev->port_idx << 8 | iface_no,
			dev_info, sizeof(struct hwa_dev_info),
			1000 /* FIXME: arbitrary */);
	kfree(dev_info);
	return ret;
}

/*
 * Set host's idea of which encryption (and key) method to use when
 * talking to ad evice on a given port.
 *
 * If key is NULL, it means disable encryption for that "virtual port"
 * (used when we disconnect).
 */
static int __hwahc_dev_set_key(struct wusbhc *wusbhc, u8 port_idx, u32 tkid,
			       const void *key, size_t key_size,
			       u8 key_idx)
{
	int result = -ENOMEM;
	struct hwahc *hwahc = container_of(wusbhc, struct hwahc, wusbhc);
	struct wahc *wa = &hwahc->wa;
	u8 iface_no = wa->usb_iface->cur_altsetting->desc.bInterfaceNumber;
	struct usb_key_descriptor *keyd;
	size_t keyd_len;

	keyd_len = sizeof(*keyd) + key_size;
	keyd = kzalloc(keyd_len, GFP_KERNEL);
	if (keyd == NULL)
		return -ENOMEM;

	keyd->bLength = keyd_len;
	keyd->bDescriptorType = USB_DT_KEY;
	keyd->tTKID[0] = (tkid >>  0) & 0xff;
	keyd->tTKID[1] = (tkid >>  8) & 0xff;
	keyd->tTKID[2] = (tkid >> 16) & 0xff;
	memcpy(keyd->bKeyData, key, key_size);

	result = usb_control_msg(wa->usb_dev, usb_sndctrlpipe(wa->usb_dev, 0),
			USB_REQ_SET_DESCRIPTOR,
			USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
			USB_DT_KEY << 8 | key_idx,
			port_idx << 8 | iface_no,
			keyd, keyd_len, 1000 /* FIXME: arbitrary */);

	memset(keyd, 0, sizeof(*keyd));	/* clear keys etc. */
	kfree(keyd);
	return result;
}

/*
 * Set host's idea of which encryption (and key) method to use when
 * talking to ad evice on a given port.
 *
 * If key is NULL, it means disable encryption for that "virtual port"
 * (used when we disconnect).
 */
static int __hwahc_op_set_ptk(struct wusbhc *wusbhc, u8 port_idx, u32 tkid,
			      const void *key, size_t key_size)
{
	int result = -ENOMEM;
	struct hwahc *hwahc = container_of(wusbhc, struct hwahc, wusbhc);
	struct wahc *wa = &hwahc->wa;
	u8 iface_no = wa->usb_iface->cur_altsetting->desc.bInterfaceNumber;
	u8 encryption_value;

	/* Tell the host which key to use to talk to the device */
	if (key) {
		u8 key_idx = wusb_key_index(0, WUSB_KEY_INDEX_TYPE_PTK,
					    WUSB_KEY_INDEX_ORIGINATOR_HOST);

		result = __hwahc_dev_set_key(wusbhc, port_idx, tkid,
					     key, key_size, key_idx);
		if (result < 0)
			goto error_set_key;
		encryption_value = wusbhc->ccm1_etd->bEncryptionValue;
	} else {
		/* FIXME: this should come from wusbhc->etd[UNSECURE].value */
		encryption_value = 0;
	}

	/* Set the encryption type for commmunicating with the device */
	result = usb_control_msg(wa->usb_dev, usb_sndctrlpipe(wa->usb_dev, 0),
			USB_REQ_SET_ENCRYPTION,
			USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
			encryption_value, port_idx << 8 | iface_no,
			NULL, 0, 1000 /* FIXME: arbitrary */);
	if (result < 0)
		dev_err(wusbhc->dev, "Can't set host's WUSB encryption for "
			"port index %u to %s (value %d): %d\n", port_idx,
			wusb_et_name(wusbhc->ccm1_etd->bEncryptionType),
			wusbhc->ccm1_etd->bEncryptionValue, result);
error_set_key:
	return result;
}

/*
 * Set host's GTK key
 */
static int __hwahc_op_set_gtk(struct wusbhc *wusbhc, u32 tkid,
			      const void *key, size_t key_size)
{
	u8 key_idx = wusb_key_index(0, WUSB_KEY_INDEX_TYPE_GTK,
				    WUSB_KEY_INDEX_ORIGINATOR_HOST);

	return __hwahc_dev_set_key(wusbhc, 0, tkid, key, key_size, key_idx);
}

/*
 * Get the Wire Adapter class-specific descriptor
 *
 * NOTE: this descriptor comes with the big bundled configuration
 *       descriptor that includes the interfaces' and endpoints', so
 *       we just look for it in the cached copy kept by the USB stack.
 *
 * NOTE2: We convert LE fields to CPU order.
 */
static int wa_fill_descr(struct wahc *wa)
{
	int result;
	struct device *dev = &wa->usb_iface->dev;
	char *itr;
	struct usb_device *usb_dev = wa->usb_dev;
	struct usb_descriptor_header *hdr;
	struct usb_wa_descriptor *wa_descr;
	size_t itr_size, actconfig_idx;

	actconfig_idx = (usb_dev->actconfig - usb_dev->config) /
			sizeof(usb_dev->config[0]);
	itr = usb_dev->rawdescriptors[actconfig_idx];
	itr_size = le16_to_cpu(usb_dev->actconfig->desc.wTotalLength);
	while (itr_size >= sizeof(*hdr)) {
		hdr = (struct usb_descriptor_header *) itr;
		dev_dbg(dev, "Extra device descriptor: "
			"type %02x/%u bytes @ %zu (%zu left)\n",
			hdr->bDescriptorType, hdr->bLength,
			(itr - usb_dev->rawdescriptors[actconfig_idx]),
			itr_size);
		if (hdr->bDescriptorType == USB_DT_WIRE_ADAPTER)
			goto found;
		itr += hdr->bLength;
		itr_size -= hdr->bLength;
	}
	dev_err(dev, "cannot find Wire Adapter Class descriptor\n");
	return -ENODEV;

found:
	result = -EINVAL;
	if (hdr->bLength > itr_size) {	/* is it available? */
		dev_err(dev, "incomplete Wire Adapter Class descriptor "
			"(%zu bytes left, %u needed)\n",
			itr_size, hdr->bLength);
		goto error;
	}
	if (hdr->bLength < sizeof(*wa->wa_descr)) {
		dev_err(dev, "short Wire Adapter Class descriptor\n");
		goto error;
	}
	wa->wa_descr = wa_descr = (struct usb_wa_descriptor *) hdr;
	/* Make LE fields CPU order */
	wa_descr->bcdWAVersion = le16_to_cpu(wa_descr->bcdWAVersion);
	wa_descr->wNumRPipes = le16_to_cpu(wa_descr->wNumRPipes);
	wa_descr->wRPipeMaxBlock = le16_to_cpu(wa_descr->wRPipeMaxBlock);
	if (wa_descr->bcdWAVersion > 0x0100)
		dev_warn(dev, "Wire Adapter v%d.%d newer than groked v1.0\n",
			 wa_descr->bcdWAVersion & 0xff00 >> 8,
			 wa_descr->bcdWAVersion & 0x00ff);
	result = 0;
error:
	return result;
}

static struct hc_driver hwahc_hc_driver = {
	.description = "hwa-hcd",
	.product_desc = "Wireless USB HWA host controller",
	.hcd_priv_size = sizeof(struct hwahc) - sizeof(struct usb_hcd),
	.irq = NULL,			/* FIXME */
	.flags = HCD_USB2,		/* FIXME */
	.reset = hwahc_op_reset,
	.start = hwahc_op_start,
	.pci_suspend = hwahc_op_suspend,
	.pci_resume = hwahc_op_resume,
	.stop = hwahc_op_stop,
	.get_frame_number = hwahc_op_get_frame_number,
	.urb_enqueue = hwahc_op_urb_enqueue,
	.urb_dequeue = hwahc_op_urb_dequeue,
	.endpoint_disable = hwahc_op_endpoint_disable,

	.hub_status_data = wusbhc_rh_status_data,
	.hub_control = wusbhc_rh_control,
	.bus_suspend = wusbhc_rh_suspend,
	.bus_resume = wusbhc_rh_resume,
	.start_port_reset = wusbhc_rh_start_port_reset,
};

static int hwahc_security_create(struct hwahc *hwahc)
{
	int result;
	struct wusbhc *wusbhc = &hwahc->wusbhc;
	struct usb_device *usb_dev = hwahc->wa.usb_dev;
	struct device *dev = &usb_dev->dev;
	struct usb_security_descriptor *secd;
	struct usb_encryption_descriptor *etd;
	void *itr, *top;
	size_t itr_size, needed, bytes;
	u8 index;
	char buf[64];

	/* Find the host's security descriptors in the config descr bundle */
	index = (usb_dev->actconfig - usb_dev->config) /
		sizeof(usb_dev->config[0]);
	itr = usb_dev->rawdescriptors[index];
	itr_size = le16_to_cpu(usb_dev->actconfig->desc.wTotalLength);
	top = itr + itr_size;
	result = __usb_get_extra_descriptor(usb_dev->rawdescriptors[index],
			le16_to_cpu(usb_dev->actconfig->desc.wTotalLength),
			USB_DT_SECURITY, (void **) &secd);
	if (result == -1) {
		dev_warn(dev, "BUG? WUSB host has no security descriptors\n");
		return 0;
	}
	needed = sizeof(*secd);
	if (top - (void *)secd < needed) {
		dev_err(dev, "BUG? Not enough data to process security "
			"descriptor header (%zu bytes left vs %zu needed)\n",
			top - (void *) secd, needed);
		return 0;
	}
	needed = le16_to_cpu(secd->wTotalLength);
	if (top - (void *)secd < needed) {
		dev_err(dev, "BUG? Not enough data to process security "
			"descriptors (%zu bytes left vs %zu needed)\n",
			top - (void *) secd, needed);
		return 0;
	}
	/* Walk over the sec descriptors and store CCM1's on wusbhc */
	itr = (void *) secd + sizeof(*secd);
	top = (void *) secd + le16_to_cpu(secd->wTotalLength);
	index = 0;
	bytes = 0;
	while (itr < top) {
		etd = itr;
		if (top - itr < sizeof(*etd)) {
			dev_err(dev, "BUG: bad host security descriptor; "
				"not enough data (%zu vs %zu left)\n",
				top - itr, sizeof(*etd));
			break;
		}
		if (etd->bLength < sizeof(*etd)) {
			dev_err(dev, "BUG: bad host encryption descriptor; "
				"descriptor is too short "
				"(%zu vs %zu needed)\n",
				(size_t)etd->bLength, sizeof(*etd));
			break;
		}
		itr += etd->bLength;
		bytes += snprintf(buf + bytes, sizeof(buf) - bytes,
				  "%s (0x%02x) ",
				  wusb_et_name(etd->bEncryptionType),
				  etd->bEncryptionValue);
		wusbhc->ccm1_etd = etd;
	}
	dev_info(dev, "supported encryption types: %s\n", buf);
	if (wusbhc->ccm1_etd == NULL) {
		dev_err(dev, "E: host doesn't support CCM-1 crypto\n");
		return 0;
	}
	/* Pretty print what we support */
	return 0;
}

static void hwahc_security_release(struct hwahc *hwahc)
{
	/* nothing to do here so far... */
}

static int hwahc_create(struct hwahc *hwahc, struct usb_interface *iface)
{
	int result;
	struct device *dev = &iface->dev;
	struct wusbhc *wusbhc = &hwahc->wusbhc;
	struct wahc *wa = &hwahc->wa;
	struct usb_device *usb_dev = interface_to_usbdev(iface);

	wa->usb_dev = usb_get_dev(usb_dev);	/* bind the USB device */
	wa->usb_iface = usb_get_intf(iface);
	wusbhc->dev = dev;
	wusbhc->uwb_rc = uwb_rc_get_by_grandpa(iface->dev.parent);
	if (wusbhc->uwb_rc == NULL) {
		result = -ENODEV;
		dev_err(dev, "Cannot get associated UWB Host Controller\n");
		goto error_rc_get;
	}
	result = wa_fill_descr(wa);	/* Get the device descriptor */
	if (result < 0)
		goto error_fill_descriptor;
	if (wa->wa_descr->bNumPorts > USB_MAXCHILDREN) {
		dev_err(dev, "FIXME: USB_MAXCHILDREN too low for WUSB "
			"adapter (%u ports)\n", wa->wa_descr->bNumPorts);
		wusbhc->ports_max = USB_MAXCHILDREN;
	} else {
		wusbhc->ports_max = wa->wa_descr->bNumPorts;
	}
	wusbhc->mmcies_max = wa->wa_descr->bNumMMCIEs;
	wusbhc->start = __hwahc_op_wusbhc_start;
	wusbhc->stop = __hwahc_op_wusbhc_stop;
	wusbhc->mmcie_add = __hwahc_op_mmcie_add;
	wusbhc->mmcie_rm = __hwahc_op_mmcie_rm;
	wusbhc->dev_info_set = __hwahc_op_dev_info_set;
	wusbhc->bwa_set = __hwahc_op_bwa_set;
	wusbhc->set_num_dnts = __hwahc_op_set_num_dnts;
	wusbhc->set_ptk = __hwahc_op_set_ptk;
	wusbhc->set_gtk = __hwahc_op_set_gtk;
	result = hwahc_security_create(hwahc);
	if (result < 0) {
		dev_err(dev, "Can't initialize security: %d\n", result);
		goto error_security_create;
	}
	wa->wusb = wusbhc;	/* FIXME: ugly, need to fix */
	result = wusbhc_create(&hwahc->wusbhc);
	if (result < 0) {
		dev_err(dev, "Can't create WUSB HC structures: %d\n", result);
		goto error_wusbhc_create;
	}
	result = wa_create(&hwahc->wa, iface);
	if (result < 0)
		goto error_wa_create;
	return 0;

error_wa_create:
	wusbhc_destroy(&hwahc->wusbhc);
error_wusbhc_create:
	/* WA Descr fill allocs no resources */
error_security_create:
error_fill_descriptor:
	uwb_rc_put(wusbhc->uwb_rc);
error_rc_get:
	usb_put_intf(iface);
	usb_put_dev(usb_dev);
	return result;
}

static void hwahc_destroy(struct hwahc *hwahc)
{
	struct wusbhc *wusbhc = &hwahc->wusbhc;

	mutex_lock(&wusbhc->mutex);
	__wa_destroy(&hwahc->wa);
	wusbhc_destroy(&hwahc->wusbhc);
	hwahc_security_release(hwahc);
	hwahc->wusbhc.dev = NULL;
	uwb_rc_put(wusbhc->uwb_rc);
	usb_put_intf(hwahc->wa.usb_iface);
	usb_put_dev(hwahc->wa.usb_dev);
	mutex_unlock(&wusbhc->mutex);
}

static void hwahc_init(struct hwahc *hwahc)
{
	wa_init(&hwahc->wa);
}

static int hwahc_probe(struct usb_interface *usb_iface,
		       const struct usb_device_id *id)
{
	int result;
	struct usb_hcd *usb_hcd;
	struct wusbhc *wusbhc;
	struct hwahc *hwahc;
	struct device *dev = &usb_iface->dev;

	result = -ENOMEM;
	usb_hcd = usb_create_hcd(&hwahc_hc_driver, &usb_iface->dev, "wusb-hwa");
	if (usb_hcd == NULL) {
		dev_err(dev, "unable to allocate instance\n");
		goto error_alloc;
	}
	usb_hcd->wireless = 1;
	usb_hcd->flags |= HCD_FLAG_SAW_IRQ;
	wusbhc = usb_hcd_to_wusbhc(usb_hcd);
	hwahc = container_of(wusbhc, struct hwahc, wusbhc);
	hwahc_init(hwahc);
	result = hwahc_create(hwahc, usb_iface);
	if (result < 0) {
		dev_err(dev, "Cannot initialize internals: %d\n", result);
		goto error_hwahc_create;
	}
	result = usb_add_hcd(usb_hcd, 0, 0);
	if (result < 0) {
		dev_err(dev, "Cannot add HCD: %d\n", result);
		goto error_add_hcd;
	}
	result = wusbhc_b_create(&hwahc->wusbhc);
	if (result < 0) {
		dev_err(dev, "Cannot setup phase B of WUSBHC: %d\n", result);
		goto error_wusbhc_b_create;
	}
	return 0;

error_wusbhc_b_create:
	usb_remove_hcd(usb_hcd);
error_add_hcd:
	hwahc_destroy(hwahc);
error_hwahc_create:
	usb_put_hcd(usb_hcd);
error_alloc:
	return result;
}

static void hwahc_disconnect(struct usb_interface *usb_iface)
{
	struct usb_hcd *usb_hcd;
	struct wusbhc *wusbhc;
	struct hwahc *hwahc;

	usb_hcd = usb_get_intfdata(usb_iface);
	wusbhc = usb_hcd_to_wusbhc(usb_hcd);
	hwahc = container_of(wusbhc, struct hwahc, wusbhc);

	wusbhc_b_destroy(&hwahc->wusbhc);
	usb_remove_hcd(usb_hcd);
	hwahc_destroy(hwahc);
	usb_put_hcd(usb_hcd);
}

static struct usb_device_id hwahc_id_table[] = {
	/* FIXME: use class labels for this */
	{ USB_INTERFACE_INFO(0xe0, 0x02, 0x01), },
	{},
};
MODULE_DEVICE_TABLE(usb, hwahc_id_table);

static struct usb_driver hwahc_driver = {
	.name =		"hwa-hc",
	.probe =	hwahc_probe,
	.disconnect =	hwahc_disconnect,
	.id_table =	hwahc_id_table,
};

static int __init hwahc_driver_init(void)
{
	return usb_register(&hwahc_driver);
}
module_init(hwahc_driver_init);

static void __exit hwahc_driver_exit(void)
{
	usb_deregister(&hwahc_driver);
}
module_exit(hwahc_driver_exit);


MODULE_AUTHOR("Inaky Perez-Gonzalez <inaky.perez-gonzalez@intel.com>");
MODULE_DESCRIPTION("Host Wired Adapter USB Host Control Driver");
MODULE_LICENSE("GPL");

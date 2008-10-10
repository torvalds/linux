/*
 * WUSB Wire Adapter: Control/Data Streaming Interface (WUSB[8])
 * MMC (Microscheduled Management Command) handling
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
 * WUIEs and MMC IEs...well, they are almost the same at the end. MMC
 * IEs are Wireless USB IEs that go into the MMC period...[what is
 * that? look in Design-overview.txt].
 *
 *
 * This is a simple subsystem to keep track of which IEs are being
 * sent by the host in the MMC period.
 *
 * For each WUIE we ask to send, we keep it in an array, so we can
 * request its removal later, or replace the content. They are tracked
 * by pointer, so be sure to use the same pointer if you want to
 * remove it or update the contents.
 *
 * FIXME:
 *  - add timers that autoremove intervalled IEs?
 */
#include <linux/usb/wusb.h>
#include "wusbhc.h"

/* Initialize the MMCIEs handling mechanism */
int wusbhc_mmcie_create(struct wusbhc *wusbhc)
{
	u8 mmcies = wusbhc->mmcies_max;
	wusbhc->mmcie = kzalloc(mmcies * sizeof(wusbhc->mmcie[0]), GFP_KERNEL);
	if (wusbhc->mmcie == NULL)
		return -ENOMEM;
	mutex_init(&wusbhc->mmcie_mutex);
	return 0;
}

/* Release resources used by the MMCIEs handling mechanism */
void wusbhc_mmcie_destroy(struct wusbhc *wusbhc)
{
	kfree(wusbhc->mmcie);
}

/*
 * Add or replace an MMC Wireless USB IE.
 *
 * @interval:    See WUSB1.0[8.5.3.1]
 * @repeat_cnt:  See WUSB1.0[8.5.3.1]
 * @handle:      See WUSB1.0[8.5.3.1]
 * @wuie:        Pointer to the header of the WUSB IE data to add.
 *               MUST BE allocated in a kmalloc buffer (no stack or
 *               vmalloc).
 *               THE CALLER ALWAYS OWNS THE POINTER (we don't free it
 *               on remove, we just forget about it).
 * @returns:     0 if ok, < 0 errno code on error.
 *
 * Goes over the *whole* @wusbhc->mmcie array looking for (a) the
 * first free spot and (b) if @wuie is already in the array (aka:
 * transmitted in the MMCs) the spot were it is.
 *
 * If present, we "overwrite it" (update).
 *
 *
 * NOTE: Need special ordering rules -- see below WUSB1.0 Table 7-38.
 *       The host uses the handle as the 'sort' index. We
 *       allocate the last one always for the WUIE_ID_HOST_INFO, and
 *       the rest, first come first serve in inverse order.
 *
 *       Host software must make sure that it adds the other IEs in
 *       the right order... the host hardware is responsible for
 *       placing the WCTA IEs in the right place with the other IEs
 *       set by host software.
 *
 * NOTE: we can access wusbhc->wa_descr without locking because it is
 *       read only.
 */
int wusbhc_mmcie_set(struct wusbhc *wusbhc, u8 interval, u8 repeat_cnt,
		     struct wuie_hdr *wuie)
{
	int result = -ENOBUFS;
	struct device *dev = wusbhc->dev;
	unsigned handle, itr;

	/* Search a handle, taking into account the ordering */
	mutex_lock(&wusbhc->mmcie_mutex);
	switch (wuie->bIEIdentifier) {
	case WUIE_ID_HOST_INFO:
		/* Always last */
		handle = wusbhc->mmcies_max - 1;
		break;
	case WUIE_ID_ISOCH_DISCARD:
		dev_err(wusbhc->dev, "Special ordering case for WUIE ID 0x%x "
			"unimplemented\n", wuie->bIEIdentifier);
		result = -ENOSYS;
		goto error_unlock;
	default:
		/* search for it or find the last empty slot */
		handle = ~0;
		for (itr = 0; itr < wusbhc->mmcies_max - 1; itr++) {
			if (wusbhc->mmcie[itr] == wuie) {
				handle = itr;
				break;
			}
			if (wusbhc->mmcie[itr] == NULL)
				handle = itr;
		}
		if (handle == ~0) {
			if (printk_ratelimit())
				dev_err(dev, "MMC handle space exhausted\n");
			goto error_unlock;
		}
	}
	result = (wusbhc->mmcie_add)(wusbhc, interval, repeat_cnt, handle,
				     wuie);
	if (result >= 0)
		wusbhc->mmcie[handle] = wuie;
error_unlock:
	mutex_unlock(&wusbhc->mmcie_mutex);
	return result;
}
EXPORT_SYMBOL_GPL(wusbhc_mmcie_set);

/*
 * Remove an MMC IE previously added with wusbhc_mmcie_set()
 *
 * @wuie	Pointer used to add the WUIE
 */
void wusbhc_mmcie_rm(struct wusbhc *wusbhc, struct wuie_hdr *wuie)
{
	int result;
	struct device *dev = wusbhc->dev;
	unsigned handle, itr;

	mutex_lock(&wusbhc->mmcie_mutex);
	for (itr = 0; itr < wusbhc->mmcies_max; itr++)
		if (wusbhc->mmcie[itr] == wuie) {
			handle = itr;
			goto found;
		}
	mutex_unlock(&wusbhc->mmcie_mutex);
	return;

found:
	result = (wusbhc->mmcie_rm)(wusbhc, handle);
	if (result == 0)
		wusbhc->mmcie[itr] = NULL;
	else if (printk_ratelimit())
		dev_err(dev, "MMC: Failed to remove IE %p (0x%02x)\n",
			wuie, wuie->bIEIdentifier);
	mutex_unlock(&wusbhc->mmcie_mutex);
	return;
}
EXPORT_SYMBOL_GPL(wusbhc_mmcie_rm);

/*
 * wusbhc_start - start transmitting MMCs and accepting connections
 * @wusbhc: the HC to start
 * @chid: the CHID to use for this host
 *
 * Establishes a cluster reservation, enables device connections, and
 * starts MMCs with appropriate DNTS parameters.
 */
int wusbhc_start(struct wusbhc *wusbhc, const struct wusb_ckhdid *chid)
{
	int result;
	struct device *dev = wusbhc->dev;

	WARN_ON(wusbhc->wuie_host_info != NULL);

	result = wusbhc_rsv_establish(wusbhc);
	if (result < 0) {
		dev_err(dev, "cannot establish cluster reservation: %d\n",
			result);
		goto error_rsv_establish;
	}

	result = wusbhc_devconnect_start(wusbhc, chid);
	if (result < 0) {
		dev_err(dev, "error enabling device connections: %d\n", result);
		goto error_devconnect_start;
	}

	result = wusbhc_sec_start(wusbhc);
	if (result < 0) {
		dev_err(dev, "error starting security in the HC: %d\n", result);
		goto error_sec_start;
	}
	/* FIXME: the choice of the DNTS parameters is somewhat
	 * arbitrary */
	result = wusbhc->set_num_dnts(wusbhc, 0, 15);
	if (result < 0) {
		dev_err(dev, "Cannot set DNTS parameters: %d\n", result);
		goto error_set_num_dnts;
	}
	result = wusbhc->start(wusbhc);
	if (result < 0) {
		dev_err(dev, "error starting wusbch: %d\n", result);
		goto error_wusbhc_start;
	}
	wusbhc->active = 1;
	return 0;

error_wusbhc_start:
	wusbhc_sec_stop(wusbhc);
error_set_num_dnts:
error_sec_start:
	wusbhc_devconnect_stop(wusbhc);
error_devconnect_start:
	wusbhc_rsv_terminate(wusbhc);
error_rsv_establish:
	return result;
}

/*
 * Disconnect all from the WUSB Channel
 *
 * Send a Host Disconnect IE in the MMC, wait, don't send it any more
 */
static int __wusbhc_host_disconnect_ie(struct wusbhc *wusbhc)
{
	int result = -ENOMEM;
	struct wuie_host_disconnect *host_disconnect_ie;
	might_sleep();
	host_disconnect_ie = kmalloc(sizeof(*host_disconnect_ie), GFP_KERNEL);
	if (host_disconnect_ie == NULL)
		goto error_alloc;
	host_disconnect_ie->hdr.bLength       = sizeof(*host_disconnect_ie);
	host_disconnect_ie->hdr.bIEIdentifier = WUIE_ID_HOST_DISCONNECT;
	result = wusbhc_mmcie_set(wusbhc, 0, 0, &host_disconnect_ie->hdr);
	if (result < 0)
		goto error_mmcie_set;

	/* WUSB1.0[8.5.3.1 & 7.5.2] */
	msleep(100);
	wusbhc_mmcie_rm(wusbhc, &host_disconnect_ie->hdr);
error_mmcie_set:
	kfree(host_disconnect_ie);
error_alloc:
	return result;
}

/*
 * wusbhc_stop - stop transmitting MMCs
 * @wusbhc: the HC to stop
 *
 * Send a Host Disconnect IE, wait, remove all the MMCs (stop sending MMCs).
 *
 * If we can't allocate a Host Stop IE, screw it, we don't notify the
 * devices we are disconnecting...
 */
void wusbhc_stop(struct wusbhc *wusbhc)
{
	if (wusbhc->active) {
		wusbhc->active = 0;
		wusbhc->stop(wusbhc);
		wusbhc_sec_stop(wusbhc);
		__wusbhc_host_disconnect_ie(wusbhc);
		wusbhc_devconnect_stop(wusbhc);
		wusbhc_rsv_terminate(wusbhc);
	}
}
EXPORT_SYMBOL_GPL(wusbhc_stop);

/*
 * Change the CHID in a WUSB Channel
 *
 * If it is just a new CHID, send a Host Disconnect IE and then change
 * the CHID IE.
 */
static int __wusbhc_chid_change(struct wusbhc *wusbhc,
				const struct wusb_ckhdid *chid)
{
	int result = -ENOSYS;
	struct device *dev = wusbhc->dev;
	dev_err(dev, "%s() not implemented yet\n", __func__);
	return result;

	BUG_ON(wusbhc->wuie_host_info == NULL);
	__wusbhc_host_disconnect_ie(wusbhc);
	wusbhc->wuie_host_info->CHID = *chid;
	result = wusbhc_mmcie_set(wusbhc, 0, 0, &wusbhc->wuie_host_info->hdr);
	if (result < 0)
		dev_err(dev, "Can't update Host Info WUSB IE: %d\n", result);
	return result;
}

/*
 * Set/reset/update a new CHID
 *
 * Depending on the previous state of the MMCs, start, stop or change
 * the sent MMC. This effectively switches the host controller on and
 * off (radio wise).
 */
int wusbhc_chid_set(struct wusbhc *wusbhc, const struct wusb_ckhdid *chid)
{
	int result = 0;

	if (memcmp(chid, &wusb_ckhdid_zero, sizeof(chid)) == 0)
		chid = NULL;

	mutex_lock(&wusbhc->mutex);
	if (wusbhc->active) {
		if (chid)
			result = __wusbhc_chid_change(wusbhc, chid);
		else
			wusbhc_stop(wusbhc);
	} else {
		if (chid)
			wusbhc_start(wusbhc, chid);
	}
	mutex_unlock(&wusbhc->mutex);
	return result;
}
EXPORT_SYMBOL_GPL(wusbhc_chid_set);

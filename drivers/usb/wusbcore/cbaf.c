/*
 * Wireless USB - Cable Based Association
 *
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
 * WUSB devices have to be paired (authenticated in WUSB lingo) so
 * that they can connect to the system.
 *
 * One way of pairing is using CBA-Cable Based Authentication, devices
 * that can connect via wired or wireless USB. First time you plug
 * them with a cable, pairing is done between host and device and
 * subsequent times, you can connect wirelessly without having to
 * pair. That's the idea.
 *
 * This driver does nothing Earth shattering. It just provides an
 * interface to chat with the wire-connected device so we can get a
 * CDID (device ID) that might have been previously associated to a
 * CHID (host ID) and to set up a new <CHID,CDID,CK> triplet
 * (connection context), with the CK being the secret, or connection
 * key. This is the pairing data.
 *
 * When a device with the CBA capability connects, the probe routine
 * just creates a bunch of sysfs files that a user space enumeration
 * manager uses to allow it to connect wirelessly to the system or not.
 *
 * The process goes like this:
 *
 * 1. device plugs, cbaf is loaded, notifications happen
 *
 * 2. the connection manager sees a device with CBAF capability (the
 *    wusb_{host_info,cdid,cc} files are in /sys/device/blah/OURDEVICE).
 *
 * 3. CM (connection manager) writes the CHID (host ID) and a host
 *    name into the wusb_host_info file. This gets sent to the device.
 *
 * 4. CM cats the wusb_cdid file; this asks the device if it has any
 *    CDID associated to the CHDI we just wrote before. If it does, it
 *    is printed, along with the device 'friendly name' and the band
 *    groups the device supports.
 *
 * 5. CM looks up its database
 *
 * 5.1  If it has a matching CHID,CDID entry, the device has been
 *      authorized before (paired). Now we can optionally ask the user
 *      if he wants to allow the device to connect. Then we generate a
 *      new CDID and CK, send it to the device and update the database
 *      (writing to the wusb_cc file so they are uploaded to the device).
 *
 * 5.2  If the CDID is zero (or we didn't find a matching CDID in our
 *      database), we assume the device is not known. We ask the user
 *      if s/he wants to allow the device to be connected wirelessly
 *      to the system. If nope, nothing else is done (FIXME: maybe
 *      send a zero CDID to clean up our CHID?). If yes, we generate
 *      random CDID and CKs (and write them to the wusb_cc file so
 *      they are uploaded to the device).
 *
 * 6. device is unplugged
 *
 * When the device tries to connect wirelessly, it will present it's
 * CDID to the WUSB host controller with ID CHID, which will query the
 * database. If found, the host will (with a 4way handshake) challenge
 * the device to demonstrate it has the CK secret key (from our
 * database) without actually exchanging it. Once satisfied, crypto
 * keys are derived from the CK, the device is connected and all
 * communication is crypted.
 *
 *
 * NOTES ABOUT THE IMPLEMENTATION
 *
 * The descriptors sent back and forth use this horrible format from
 * hell on which each field is actually a field ID, field length and
 * then the field itself. How stupid can that get, taking into account
 * the structures are defined by the spec?? oh well.
 *
 *
 * FIXME: we don't provide a way to tell the device the pairing failed
 *        (ie: send a CC_DATA_FAIL). Should add some day.
 */
#include <linux/module.h>
#include <linux/ctype.h>
#include <linux/version.h>
#include <linux/usb.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/random.h>
#include <linux/mutex.h>
#include <linux/uwb.h>
#include <linux/usb/wusb.h>
#include <linux/usb/association.h>

#undef D_LOCAL
#define D_LOCAL 6
#include <linux/uwb/debug.h>

/* An instance of a Cable-Based-Association-Framework device */
struct cbaf {
	struct usb_device *usb_dev;
	struct usb_interface *usb_iface;
	void *buffer;
	size_t buffer_size;

	struct wusb_ckhdid chid;/* Host Information */
	char host_name[65];	/* max length:
					Assoc Models Suplement 1.0[T4-7] */
	u16 host_band_groups;

	struct wusb_ckhdid cdid;/* Device Information */
	char device_name[65];	/* max length:
					Assoc Models Suplement 1.0[T4-7] */
	u16 device_band_groups;
	struct wusb_ckhdid ck;	/* Connection Key */
};

/*
 * Verify that a CBAF USB-interface has what we need
 *
 * (like we care, we are going to fail the enumeration if not :)
 *
 * FIXME: ugly function, need to split
 */
static int cbaf_check(struct cbaf *cbaf)
{
	int result;
	struct device *dev = &cbaf->usb_iface->dev;
	struct wusb_cbaf_assoc_info *assoc_info;
	struct wusb_cbaf_assoc_request *assoc_request;
	size_t assoc_size;
	void *itr, *top;
	unsigned ar_index;
	int ar_rhi_idx = -1, ar_assoc_idx = -1;

	result = usb_control_msg(
		cbaf->usb_dev, usb_rcvctrlpipe(cbaf->usb_dev, 0),
		CBAF_REQ_GET_ASSOCIATION_INFORMATION,
		USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
		0, cbaf->usb_iface->cur_altsetting->desc.bInterfaceNumber,
		cbaf->buffer, cbaf->buffer_size, 1000 /* FIXME: arbitrary */);
	if (result < 0) {
		dev_err(dev, "cannot get available association types: %d\n",
			result);
		goto error_get_assoc_types;
	}
	assoc_info = cbaf->buffer;
	if (result < sizeof(*assoc_info)) {
		dev_err(dev, "not enough data to decode association info "
			"header (%zu vs %zu bytes required)\n",
			(size_t)result, sizeof(*assoc_info));
		goto error_bad_header;
	}
	assoc_size = le16_to_cpu(assoc_info->Length);
	if (result < assoc_size) {
		dev_err(dev, "not enough data to decode association info "
			"(%zu vs %zu bytes required)\n",
			(size_t)assoc_size, sizeof(*assoc_info));
		goto error_bad_data;
	}
	/*
	 * From now on, we just verify, but won't error out unless we
	 * don't find the AR_TYPE_WUSB_{RETRIEVE_HOST_INFO,ASSOCIATE}
	 * types.
	 */
	ar_index = 0;
	itr = cbaf->buffer + sizeof(*assoc_info);
	top = cbaf->buffer + assoc_size;
	d_printf(1, dev, "Found %u association requests (%zu bytes)\n",
		 assoc_info->NumAssociationRequests, assoc_size);
	while (itr < top) {
		u16 ar_type, ar_subtype;
		u32 ar_size;
		const char *ar_name;

		assoc_request = itr;
		if (top - itr < sizeof(*assoc_request)) {
			dev_err(dev, "not enough data to decode associaton "
				"request (%zu vs %zu bytes needed)\n",
				top - itr, sizeof(*assoc_request));
			break;
		}
		ar_type = le16_to_cpu(assoc_request->AssociationTypeId);
		ar_subtype = le16_to_cpu(assoc_request->AssociationSubTypeId);
		ar_size = le32_to_cpu(assoc_request->AssociationTypeInfoSize);
		switch (ar_type) {
		case AR_TYPE_WUSB:
			/* Verify we have what is mandated by AMS1.0 */
			switch (ar_subtype) {
			case AR_TYPE_WUSB_RETRIEVE_HOST_INFO:
				ar_name = "retrieve_host_info";
				ar_rhi_idx = ar_index;
				break;
			case AR_TYPE_WUSB_ASSOCIATE:
				/* send assoc data */
				ar_name = "associate";
				ar_assoc_idx = ar_index;
				break;
			default:
				ar_name = "unknown";
			};
			break;
		default:
			ar_name = "unknown";
		};
		d_printf(1, dev, "association request #%02u: 0x%04x/%04x "
			 "(%zu bytes): %s\n",
			 assoc_request->AssociationDataIndex, ar_type,
			 ar_subtype, (size_t)ar_size, ar_name);

		itr += sizeof(*assoc_request);
		ar_index++;
	}
	if (ar_rhi_idx == -1) {
		dev_err(dev, "Missing RETRIEVE_HOST_INFO association "
			"request\n");
		goto error_bad_reqs;
	}
	if (ar_assoc_idx == -1) {
		dev_err(dev, "Missing ASSOCIATE association request\n");
		goto error_bad_reqs;
	}
	return 0;

error_bad_header:
error_bad_data:
error_bad_reqs:
error_get_assoc_types:
	return -EINVAL;
}

static const struct wusb_cbaf_host_info cbaf_host_info_defaults = {
	.AssociationTypeId_hdr    = WUSB_AR_AssociationTypeId,
	.AssociationTypeId    	  = cpu_to_le16(AR_TYPE_WUSB),
	.AssociationSubTypeId_hdr = WUSB_AR_AssociationSubTypeId,
	.AssociationSubTypeId = cpu_to_le16(AR_TYPE_WUSB_RETRIEVE_HOST_INFO),
	.CHID_hdr                 = WUSB_AR_CHID,
	.LangID_hdr               = WUSB_AR_LangID,
	.HostFriendlyName_hdr     = WUSB_AR_HostFriendlyName,
};

/* Send WUSB host information (CHID and name) to a CBAF device */
static int cbaf_send_host_info(struct cbaf *cbaf)
{
	struct wusb_cbaf_host_info *hi;
	size_t hi_size;

	hi = cbaf->buffer;
	memset(hi, 0, sizeof(*hi));
	*hi = cbaf_host_info_defaults;
	hi->CHID = cbaf->chid;
	hi->LangID = 0;	/* FIXME: I guess... */
	strncpy(hi->HostFriendlyName, cbaf->host_name,
		hi->HostFriendlyName_hdr.len);
	hi->HostFriendlyName_hdr.len =
				cpu_to_le16(strlen(hi->HostFriendlyName));
	hi_size = sizeof(*hi) + strlen(hi->HostFriendlyName);
	return usb_control_msg(cbaf->usb_dev, usb_sndctrlpipe(cbaf->usb_dev, 0),
			CBAF_REQ_SET_ASSOCIATION_RESPONSE,
			USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
			0x0101,
			cbaf->usb_iface->cur_altsetting->desc.bInterfaceNumber,
			hi, hi_size, 1000 /* FIXME: arbitrary */);
}

/* Show current CHID info we have set from user space */
static ssize_t cbaf_wusb_host_info_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct usb_interface *iface = to_usb_interface(dev);
	struct cbaf *cbaf = usb_get_intfdata(iface);
	char pr_chid[WUSB_CKHDID_STRSIZE];

	ckhdid_printf(pr_chid, sizeof(pr_chid), &cbaf->chid);
	return scnprintf(buf, PAGE_SIZE, "CHID: %s\nName: %s\n",
			 pr_chid, cbaf->host_name);
}

/*
 * Get a host info CHID from user space and send it to the device.
 *
 * The user can recover a CC from the device associated to that CHID
 * by cat'ing wusb_connection_context.
 */
static ssize_t cbaf_wusb_host_info_store(struct device *dev,
					 struct device_attribute *attr,
					 const char *buf, size_t size)
{
	ssize_t result;
	struct usb_interface *iface = to_usb_interface(dev);
	struct cbaf *cbaf = usb_get_intfdata(iface);

	result = sscanf(buf,
			"%02hhx %02hhx %02hhx %02hhx "
			"%02hhx %02hhx %02hhx %02hhx "
			"%02hhx %02hhx %02hhx %02hhx "
			"%02hhx %02hhx %02hhx %02hhx "
			"%04hx %64s\n",
			&cbaf->chid.data[0] , &cbaf->chid.data[1],
			&cbaf->chid.data[2] , &cbaf->chid.data[3],
			&cbaf->chid.data[4] , &cbaf->chid.data[5],
			&cbaf->chid.data[6] , &cbaf->chid.data[7],
			&cbaf->chid.data[8] , &cbaf->chid.data[9],
			&cbaf->chid.data[10], &cbaf->chid.data[11],
			&cbaf->chid.data[12], &cbaf->chid.data[13],
			&cbaf->chid.data[14], &cbaf->chid.data[15],
			&cbaf->host_band_groups, cbaf->host_name);
	if (result != 18) {
		dev_err(dev, "Unrecognized CHID (need 16 8-bit hex digits, "
			"a 16 bit hex band group mask "
			"and a host name, got only %d)\n", (int)result);
		return -EINVAL;
	}
	result = cbaf_send_host_info(cbaf);
	if (result < 0)
		dev_err(dev, "Couldn't send host information to device: %d\n",
			(int)result);
	else
		d_printf(1, dev, "HI sent, wusb_cc can be read now\n");
	return result < 0 ? result : size;
}
static DEVICE_ATTR(wusb_host_info, 0600, cbaf_wusb_host_info_show,
					 cbaf_wusb_host_info_store);

static const struct wusb_cbaf_device_info cbaf_device_info_defaults = {
	.Length_hdr               = WUSB_AR_Length,
	.CDID_hdr                 = WUSB_AR_CDID,
	.BandGroups_hdr           = WUSB_AR_BandGroups,
	.LangID_hdr               = WUSB_AR_LangID,
	.DeviceFriendlyName_hdr   = WUSB_AR_DeviceFriendlyName,
};

/*
 * Get device's information (CDID) associated to CHID
 *
 * The device will return it's information (CDID, name, bandgroups)
 * associated to the CHID we have set before, or 0 CDID and default
 * name and bandgroup if no CHID set or unknown.
 */
static int cbaf_cdid_get(struct cbaf *cbaf)
{
	int result;
	struct device *dev = &cbaf->usb_iface->dev;
	struct wusb_cbaf_device_info *di;
	size_t needed, dev_name_size;

	di = cbaf->buffer;
	result = usb_control_msg(
		cbaf->usb_dev, usb_rcvctrlpipe(cbaf->usb_dev, 0),
		CBAF_REQ_GET_ASSOCIATION_REQUEST,
		USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
		0x0200, cbaf->usb_iface->cur_altsetting->desc.bInterfaceNumber,
		di, cbaf->buffer_size, 1000 /* FIXME: arbitrary */);
	if (result < 0) {
		dev_err(dev, "Cannot request device information: %d\n", result);
		goto error_req_di;
	}
	needed = result < sizeof(*di) ? sizeof(*di) : le32_to_cpu(di->Length);
	if (result < needed) {
		dev_err(dev, "Not enough data in DEVICE_INFO reply (%zu vs "
			"%zu bytes needed)\n", (size_t)result, needed);
		goto error_bad_di;
	}
	cbaf->cdid = di->CDID;
	dev_name_size = le16_to_cpu(di->DeviceFriendlyName_hdr.len);
	dev_name_size = dev_name_size > 65 - 1 ? 65 - 1 : dev_name_size;
	memcpy(cbaf->device_name, di->DeviceFriendlyName, dev_name_size);
	cbaf->device_name[dev_name_size] = 0;
	cbaf->device_band_groups = le16_to_cpu(di->BandGroups);
	result = 0;
error_req_di:
error_bad_di:
	return result;
}

/*
 * Get device information and print it to sysfs
 *
 * See cbaf_cdid_get()
 */
static ssize_t cbaf_wusb_cdid_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	ssize_t result;
	struct usb_interface *iface = to_usb_interface(dev);
	struct cbaf *cbaf = usb_get_intfdata(iface);
	char pr_cdid[WUSB_CKHDID_STRSIZE];

	result = cbaf_cdid_get(cbaf);
	if (result < 0) {
		dev_err(dev, "Cannot read device information: %d\n",
			(int)result);
		goto error_get_di;
	}
	ckhdid_printf(pr_cdid, sizeof(pr_cdid), &cbaf->cdid);
	result = scnprintf(buf, PAGE_SIZE,
			   "CDID: %s\nName: %s\nBand_groups: 0x%04x\n",
			   pr_cdid, cbaf->device_name,
			   cbaf->device_band_groups);
error_get_di:
	return result;
}
static DEVICE_ATTR(wusb_cdid, 0600, cbaf_wusb_cdid_show, NULL);

static const struct wusb_cbaf_cc_data cbaf_cc_data_defaults = {
	.AssociationTypeId_hdr    = WUSB_AR_AssociationTypeId,
	.AssociationTypeId    	  = cpu_to_le16(AR_TYPE_WUSB),
	.AssociationSubTypeId_hdr = WUSB_AR_AssociationSubTypeId,
	.AssociationSubTypeId     = cpu_to_le16(AR_TYPE_WUSB_ASSOCIATE),
	.Length_hdr               = WUSB_AR_Length,
	.Length               	  = cpu_to_le32(sizeof(struct wusb_cbaf_cc_data)),
	.ConnectionContext_hdr    = WUSB_AR_ConnectionContext,
	.BandGroups_hdr           = WUSB_AR_BandGroups,
};

static const struct wusb_cbaf_cc_data_fail cbaf_cc_data_fail_defaults = {
	.AssociationTypeId_hdr    = WUSB_AR_AssociationTypeId,
	.AssociationSubTypeId_hdr = WUSB_AR_AssociationSubTypeId,
	.Length_hdr               = WUSB_AR_Length,
	.AssociationStatus_hdr    = WUSB_AR_AssociationStatus,
};

/*
 * Send a new CC to the device
 *
 * So we update the CK and send the whole thing to the device
 */
static int cbaf_cc_upload(struct cbaf *cbaf)
{
	int result;
	struct device *dev = &cbaf->usb_iface->dev;
	struct wusb_cbaf_cc_data *ccd;
	char pr_cdid[WUSB_CKHDID_STRSIZE];

	ccd =  cbaf->buffer;
	*ccd = cbaf_cc_data_defaults;
	ccd->CHID = cbaf->chid;
	ccd->CDID = cbaf->cdid;
	ccd->CK = cbaf->ck;
	ccd->BandGroups = cpu_to_le16(cbaf->host_band_groups);
	result = usb_control_msg(
		cbaf->usb_dev, usb_sndctrlpipe(cbaf->usb_dev, 0),
		CBAF_REQ_SET_ASSOCIATION_RESPONSE,
		USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
		0x0201, cbaf->usb_iface->cur_altsetting->desc.bInterfaceNumber,
		ccd, sizeof(*ccd), 1000 /* FIXME: arbitrary */);
	d_printf(1, dev, "Uploaded CC:\n");
	ckhdid_printf(pr_cdid, sizeof(pr_cdid), &ccd->CHID);
	d_printf(1, dev, "  CHID       %s\n", pr_cdid);
	ckhdid_printf(pr_cdid, sizeof(pr_cdid), &ccd->CDID);
	d_printf(1, dev, "  CDID       %s\n", pr_cdid);
	ckhdid_printf(pr_cdid, sizeof(pr_cdid), &ccd->CK);
	d_printf(1, dev, "  CK         %s\n", pr_cdid);
	d_printf(1, dev, "  bandgroups 0x%04x\n", cbaf->host_band_groups);
	return result;
}

/*
 * Send a new CC to the device
 *
 * We take the CDID and CK from user space, the rest from the info we
 * set with host_info.
 */
static ssize_t cbaf_wusb_cc_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t size)
{
	ssize_t result;
	struct usb_interface *iface = to_usb_interface(dev);
	struct cbaf *cbaf = usb_get_intfdata(iface);

	result = sscanf(buf,
			"CDID: %02hhx %02hhx %02hhx %02hhx "
			"%02hhx %02hhx %02hhx %02hhx "
			"%02hhx %02hhx %02hhx %02hhx "
			"%02hhx %02hhx %02hhx %02hhx\n"
			"CK: %02hhx %02hhx %02hhx %02hhx "
			"%02hhx %02hhx %02hhx %02hhx "
			"%02hhx %02hhx %02hhx %02hhx "
			"%02hhx %02hhx %02hhx %02hhx\n",
			&cbaf->cdid.data[0] , &cbaf->cdid.data[1],
			&cbaf->cdid.data[2] , &cbaf->cdid.data[3],
			&cbaf->cdid.data[4] , &cbaf->cdid.data[5],
			&cbaf->cdid.data[6] , &cbaf->cdid.data[7],
			&cbaf->cdid.data[8] , &cbaf->cdid.data[9],
			&cbaf->cdid.data[10], &cbaf->cdid.data[11],
			&cbaf->cdid.data[12], &cbaf->cdid.data[13],
			&cbaf->cdid.data[14], &cbaf->cdid.data[15],

			&cbaf->ck.data[0] , &cbaf->ck.data[1],
			&cbaf->ck.data[2] , &cbaf->ck.data[3],
			&cbaf->ck.data[4] , &cbaf->ck.data[5],
			&cbaf->ck.data[6] , &cbaf->ck.data[7],
			&cbaf->ck.data[8] , &cbaf->ck.data[9],
			&cbaf->ck.data[10], &cbaf->ck.data[11],
			&cbaf->ck.data[12], &cbaf->ck.data[13],
			&cbaf->ck.data[14], &cbaf->ck.data[15]);
	if (result != 32) {
		dev_err(dev, "Unrecognized CHID/CK (need 32 8-bit "
			"hex digits, got only %d)\n", (int)result);
		return -EINVAL;
	}
	result = cbaf_cc_upload(cbaf);
	if (result < 0)
		dev_err(dev, "Couldn't upload connection context: %d\n",
			(int)result);
	else
		d_printf(1, dev, "Connection context uploaded\n");
	return result < 0 ? result : size;
}
static DEVICE_ATTR(wusb_cc, 0600, NULL, cbaf_wusb_cc_store);

static struct attribute *cbaf_dev_attrs[] = {
	&dev_attr_wusb_host_info.attr,
	&dev_attr_wusb_cdid.attr,
	&dev_attr_wusb_cc.attr,
	NULL,
};

static struct attribute_group cbaf_dev_attr_group = {
	.name = NULL,	/* we want them in the same directory */
	.attrs = cbaf_dev_attrs,
};

static int cbaf_probe(struct usb_interface *iface,
		      const struct usb_device_id *id)
{
	int result;
	struct cbaf *cbaf;
	struct device *dev = &iface->dev;

	result = -ENOMEM;
	cbaf = kzalloc(sizeof(*cbaf), GFP_KERNEL);
	if (cbaf == NULL) {
		dev_err(dev, "Unable to allocate instance\n");
		goto error_kzalloc;
	}
	cbaf->buffer = kmalloc(512, GFP_KERNEL);
	if (cbaf->buffer == NULL)
		goto error_kmalloc_buffer;
	cbaf->buffer_size = 512;
	cbaf->usb_dev = usb_get_dev(interface_to_usbdev(iface));
	cbaf->usb_iface = usb_get_intf(iface);
	result = cbaf_check(cbaf);
	if (result < 0)
		goto error_check;
	result = sysfs_create_group(&dev->kobj, &cbaf_dev_attr_group);
	if (result < 0) {
		dev_err(dev, "Can't register sysfs attr group: %d\n", result);
		goto error_create_group;
	}
	usb_set_intfdata(iface, cbaf);
	d_printf(2, dev, "CBA attached\n");
	return 0;

error_create_group:
error_check:
	kfree(cbaf->buffer);
error_kmalloc_buffer:
	kfree(cbaf);
error_kzalloc:
	return result;
}

static void cbaf_disconnect(struct usb_interface *iface)
{
	struct cbaf *cbaf = usb_get_intfdata(iface);
	struct device *dev = &iface->dev;
	sysfs_remove_group(&dev->kobj, &cbaf_dev_attr_group);
	usb_set_intfdata(iface, NULL);
	usb_put_intf(iface);
	kfree(cbaf->buffer);
	/* paranoia: clean up crypto keys */
	memset(cbaf, 0, sizeof(*cbaf));
	kfree(cbaf);
	d_printf(1, dev, "CBA detached\n");
}

static struct usb_device_id cbaf_id_table[] = {
	{ USB_INTERFACE_INFO(0xef, 0x03, 0x01), },
	{ },
};
MODULE_DEVICE_TABLE(usb, cbaf_id_table);

static struct usb_driver cbaf_driver = {
	.name =		"wusb-cbaf",
	.id_table =	cbaf_id_table,
	.probe =	cbaf_probe,
	.disconnect =	cbaf_disconnect,
};

static int __init cbaf_driver_init(void)
{
	return usb_register(&cbaf_driver);
}
module_init(cbaf_driver_init);

static void __exit cbaf_driver_exit(void)
{
	usb_deregister(&cbaf_driver);
}
module_exit(cbaf_driver_exit);

MODULE_AUTHOR("Inaky Perez-Gonzalez <inaky.perez-gonzalez@intel.com>");
MODULE_DESCRIPTION("Wireless USB Cable Based Association");
MODULE_LICENSE("GPL");

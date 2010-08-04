/*
 * Wireless USB - Cable Based Association
 *
 *
 * Copyright (C) 2006 Intel Corporation
 * Inaky Perez-Gonzalez <inaky.perez-gonzalez@intel.com>
 * Copyright (C) 2008 Cambridge Silicon Radio Ltd.
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
 * WUSB devices have to be paired (associated in WUSB lingo) so
 * that they can connect to the system.
 *
 * One way of pairing is using CBA-Cable Based Association. First
 * time you plug the device with a cable, association is done between
 * host and device and subsequent times, you can connect wirelessly
 * without having to associate again. That's the idea.
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
 * 1. Device plugs, cbaf is loaded, notifications happen.
 *
 * 2. The connection manager (CM) sees a device with CBAF capability
 *    (the wusb_chid etc. files in /sys/devices/blah/OURDEVICE).
 *
 * 3. The CM writes the host name, supported band groups, and the CHID
 *    (host ID) into the wusb_host_name, wusb_host_band_groups and
 *    wusb_chid files. These get sent to the device and the CDID (if
 *    any) for this host is requested.
 *
 * 4. The CM can verify that the device's supported band groups
 *    (wusb_device_band_groups) are compatible with the host.
 *
 * 5. The CM reads the wusb_cdid file.
 *
 * 6. The CM looks up its database
 *
 * 6.1 If it has a matching CHID,CDID entry, the device has been
 *     authorized before (paired) and nothing further needs to be
 *     done.
 *
 * 6.2 If the CDID is zero (or the CM doesn't find a matching CDID in
 *     its database), the device is assumed to be not known.  The CM
 *     may associate the host with device by: writing a randomly
 *     generated CDID to wusb_cdid and then a random CK to wusb_ck
 *     (this uploads the new CC to the device).
 *
 *     CMD may choose to prompt the user before associating with a new
 *     device.
 *
 * 7. Device is unplugged.
 *
 * When the device tries to connect wirelessly, it will present its
 * CDID to the WUSB host controller.  The CM will query the
 * database. If the CHID/CDID pair found, it will (with a 4-way
 * handshake) challenge the device to demonstrate it has the CK secret
 * key (from our database) without actually exchanging it. Once
 * satisfied, crypto keys are derived from the CK, the device is
 * connected and all communication is encrypted.
 *
 * References:
 *   [WUSB-AM] Association Models Supplement to the Certified Wireless
 *             Universal Serial Bus Specification, version 1.0.
 */
#include <linux/module.h>
#include <linux/ctype.h>
#include <linux/usb.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/random.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/uwb.h>
#include <linux/usb/wusb.h>
#include <linux/usb/association.h>

#define CBA_NAME_LEN 0x40 /* [WUSB-AM] table 4-7 */

/* An instance of a Cable-Based-Association-Framework device */
struct cbaf {
	struct usb_device *usb_dev;
	struct usb_interface *usb_iface;
	void *buffer;
	size_t buffer_size;

	struct wusb_ckhdid chid;
	char host_name[CBA_NAME_LEN];
	u16 host_band_groups;

	struct wusb_ckhdid cdid;
	char device_name[CBA_NAME_LEN];
	u16 device_band_groups;

	struct wusb_ckhdid ck;
};

/*
 * Verify that a CBAF USB-interface has what we need
 *
 * According to [WUSB-AM], CBA devices should provide at least two
 * interfaces:
 *  - RETRIEVE_HOST_INFO
 *  - ASSOCIATE
 *
 * If the device doesn't provide these interfaces, we do not know how
 * to deal with it.
 */
static int cbaf_check(struct cbaf *cbaf)
{
	int result;
	struct device *dev = &cbaf->usb_iface->dev;
	struct wusb_cbaf_assoc_info *assoc_info;
	struct wusb_cbaf_assoc_request *assoc_request;
	size_t assoc_size;
	void *itr, *top;
	int ar_rhi = 0, ar_assoc = 0;

	result = usb_control_msg(
		cbaf->usb_dev, usb_rcvctrlpipe(cbaf->usb_dev, 0),
		CBAF_REQ_GET_ASSOCIATION_INFORMATION,
		USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
		0, cbaf->usb_iface->cur_altsetting->desc.bInterfaceNumber,
		cbaf->buffer, cbaf->buffer_size, 1000 /* FIXME: arbitrary */);
	if (result < 0) {
		dev_err(dev, "Cannot get available association types: %d\n",
			result);
		return result;
	}

	assoc_info = cbaf->buffer;
	if (result < sizeof(*assoc_info)) {
		dev_err(dev, "Not enough data to decode association info "
			"header (%zu vs %zu bytes required)\n",
			(size_t)result, sizeof(*assoc_info));
		return result;
	}

	assoc_size = le16_to_cpu(assoc_info->Length);
	if (result < assoc_size) {
		dev_err(dev, "Not enough data to decode association info "
			"(%zu vs %zu bytes required)\n",
			(size_t)assoc_size, sizeof(*assoc_info));
		return result;
	}
	/*
	 * From now on, we just verify, but won't error out unless we
	 * don't find the AR_TYPE_WUSB_{RETRIEVE_HOST_INFO,ASSOCIATE}
	 * types.
	 */
	itr = cbaf->buffer + sizeof(*assoc_info);
	top = cbaf->buffer + assoc_size;
	dev_dbg(dev, "Found %u association requests (%zu bytes)\n",
		 assoc_info->NumAssociationRequests, assoc_size);

	while (itr < top) {
		u16 ar_type, ar_subtype;
		u32 ar_size;
		const char *ar_name;

		assoc_request = itr;

		if (top - itr < sizeof(*assoc_request)) {
			dev_err(dev, "Not enough data to decode associaton "
				"request (%zu vs %zu bytes needed)\n",
				top - itr, sizeof(*assoc_request));
			break;
		}

		ar_type = le16_to_cpu(assoc_request->AssociationTypeId);
		ar_subtype = le16_to_cpu(assoc_request->AssociationSubTypeId);
		ar_size = le32_to_cpu(assoc_request->AssociationTypeInfoSize);
		ar_name = "unknown";

		switch (ar_type) {
		case AR_TYPE_WUSB:
			/* Verify we have what is mandated by [WUSB-AM]. */
			switch (ar_subtype) {
			case AR_TYPE_WUSB_RETRIEVE_HOST_INFO:
				ar_name = "RETRIEVE_HOST_INFO";
				ar_rhi = 1;
				break;
			case AR_TYPE_WUSB_ASSOCIATE:
				/* send assoc data */
				ar_name = "ASSOCIATE";
				ar_assoc = 1;
				break;
			};
			break;
		};

		dev_dbg(dev, "Association request #%02u: 0x%04x/%04x "
			 "(%zu bytes): %s\n",
			 assoc_request->AssociationDataIndex, ar_type,
			 ar_subtype, (size_t)ar_size, ar_name);

		itr += sizeof(*assoc_request);
	}

	if (!ar_rhi) {
		dev_err(dev, "Missing RETRIEVE_HOST_INFO association "
			"request\n");
		return -EINVAL;
	}
	if (!ar_assoc) {
		dev_err(dev, "Missing ASSOCIATE association request\n");
		return -EINVAL;
	}

	return 0;
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
	size_t name_len;
	size_t hi_size;

	hi = cbaf->buffer;
	memset(hi, 0, sizeof(*hi));
	*hi = cbaf_host_info_defaults;
	hi->CHID = cbaf->chid;
	hi->LangID = 0;	/* FIXME: I guess... */
	strlcpy(hi->HostFriendlyName, cbaf->host_name, CBA_NAME_LEN);
	name_len = strlen(cbaf->host_name);
	hi->HostFriendlyName_hdr.len = cpu_to_le16(name_len);
	hi_size = sizeof(*hi) + name_len;

	return usb_control_msg(cbaf->usb_dev, usb_sndctrlpipe(cbaf->usb_dev, 0),
			CBAF_REQ_SET_ASSOCIATION_RESPONSE,
			USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
			0x0101,
			cbaf->usb_iface->cur_altsetting->desc.bInterfaceNumber,
			hi, hi_size, 1000 /* FIXME: arbitrary */);
}

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
	size_t needed;

	di = cbaf->buffer;
	result = usb_control_msg(
		cbaf->usb_dev, usb_rcvctrlpipe(cbaf->usb_dev, 0),
		CBAF_REQ_GET_ASSOCIATION_REQUEST,
		USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
		0x0200, cbaf->usb_iface->cur_altsetting->desc.bInterfaceNumber,
		di, cbaf->buffer_size, 1000 /* FIXME: arbitrary */);
	if (result < 0) {
		dev_err(dev, "Cannot request device information: %d\n", result);
		return result;
	}

	needed = result < sizeof(*di) ? sizeof(*di) : le32_to_cpu(di->Length);
	if (result < needed) {
		dev_err(dev, "Not enough data in DEVICE_INFO reply (%zu vs "
			"%zu bytes needed)\n", (size_t)result, needed);
		return result;
	}

	strlcpy(cbaf->device_name, di->DeviceFriendlyName, CBA_NAME_LEN);
	cbaf->cdid = di->CDID;
	cbaf->device_band_groups = le16_to_cpu(di->BandGroups);

	return 0;
}

static ssize_t cbaf_wusb_chid_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct usb_interface *iface = to_usb_interface(dev);
	struct cbaf *cbaf = usb_get_intfdata(iface);
	char pr_chid[WUSB_CKHDID_STRSIZE];

	ckhdid_printf(pr_chid, sizeof(pr_chid), &cbaf->chid);
	return scnprintf(buf, PAGE_SIZE, "%s\n", pr_chid);
}

static ssize_t cbaf_wusb_chid_store(struct device *dev,
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
			"%02hhx %02hhx %02hhx %02hhx",
			&cbaf->chid.data[0] , &cbaf->chid.data[1],
			&cbaf->chid.data[2] , &cbaf->chid.data[3],
			&cbaf->chid.data[4] , &cbaf->chid.data[5],
			&cbaf->chid.data[6] , &cbaf->chid.data[7],
			&cbaf->chid.data[8] , &cbaf->chid.data[9],
			&cbaf->chid.data[10], &cbaf->chid.data[11],
			&cbaf->chid.data[12], &cbaf->chid.data[13],
			&cbaf->chid.data[14], &cbaf->chid.data[15]);

	if (result != 16)
		return -EINVAL;

	result = cbaf_send_host_info(cbaf);
	if (result < 0)
		return result;
	result = cbaf_cdid_get(cbaf);
	if (result < 0)
		return -result;
	return size;
}
static DEVICE_ATTR(wusb_chid, 0600, cbaf_wusb_chid_show, cbaf_wusb_chid_store);

static ssize_t cbaf_wusb_host_name_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct usb_interface *iface = to_usb_interface(dev);
	struct cbaf *cbaf = usb_get_intfdata(iface);

	return scnprintf(buf, PAGE_SIZE, "%s\n", cbaf->host_name);
}

static ssize_t cbaf_wusb_host_name_store(struct device *dev,
					 struct device_attribute *attr,
					 const char *buf, size_t size)
{
	ssize_t result;
	struct usb_interface *iface = to_usb_interface(dev);
	struct cbaf *cbaf = usb_get_intfdata(iface);

	result = sscanf(buf, "%63s", cbaf->host_name);
	if (result != 1)
		return -EINVAL;

	return size;
}
static DEVICE_ATTR(wusb_host_name, 0600, cbaf_wusb_host_name_show,
					 cbaf_wusb_host_name_store);

static ssize_t cbaf_wusb_host_band_groups_show(struct device *dev,
					       struct device_attribute *attr,
					       char *buf)
{
	struct usb_interface *iface = to_usb_interface(dev);
	struct cbaf *cbaf = usb_get_intfdata(iface);

	return scnprintf(buf, PAGE_SIZE, "0x%04x\n", cbaf->host_band_groups);
}

static ssize_t cbaf_wusb_host_band_groups_store(struct device *dev,
						struct device_attribute *attr,
						const char *buf, size_t size)
{
	ssize_t result;
	struct usb_interface *iface = to_usb_interface(dev);
	struct cbaf *cbaf = usb_get_intfdata(iface);
	u16 band_groups = 0;

	result = sscanf(buf, "%04hx", &band_groups);
	if (result != 1)
		return -EINVAL;

	cbaf->host_band_groups = band_groups;

	return size;
}

static DEVICE_ATTR(wusb_host_band_groups, 0600,
		   cbaf_wusb_host_band_groups_show,
		   cbaf_wusb_host_band_groups_store);

static const struct wusb_cbaf_device_info cbaf_device_info_defaults = {
	.Length_hdr               = WUSB_AR_Length,
	.CDID_hdr                 = WUSB_AR_CDID,
	.BandGroups_hdr           = WUSB_AR_BandGroups,
	.LangID_hdr               = WUSB_AR_LangID,
	.DeviceFriendlyName_hdr   = WUSB_AR_DeviceFriendlyName,
};

static ssize_t cbaf_wusb_cdid_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct usb_interface *iface = to_usb_interface(dev);
	struct cbaf *cbaf = usb_get_intfdata(iface);
	char pr_cdid[WUSB_CKHDID_STRSIZE];

	ckhdid_printf(pr_cdid, sizeof(pr_cdid), &cbaf->cdid);
	return scnprintf(buf, PAGE_SIZE, "%s\n", pr_cdid);
}

static ssize_t cbaf_wusb_cdid_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t size)
{
	ssize_t result;
	struct usb_interface *iface = to_usb_interface(dev);
	struct cbaf *cbaf = usb_get_intfdata(iface);
	struct wusb_ckhdid cdid;

	result = sscanf(buf,
			"%02hhx %02hhx %02hhx %02hhx "
			"%02hhx %02hhx %02hhx %02hhx "
			"%02hhx %02hhx %02hhx %02hhx "
			"%02hhx %02hhx %02hhx %02hhx",
			&cdid.data[0] , &cdid.data[1],
			&cdid.data[2] , &cdid.data[3],
			&cdid.data[4] , &cdid.data[5],
			&cdid.data[6] , &cdid.data[7],
			&cdid.data[8] , &cdid.data[9],
			&cdid.data[10], &cdid.data[11],
			&cdid.data[12], &cdid.data[13],
			&cdid.data[14], &cdid.data[15]);
	if (result != 16)
		return -EINVAL;

	cbaf->cdid = cdid;

	return size;
}
static DEVICE_ATTR(wusb_cdid, 0600, cbaf_wusb_cdid_show, cbaf_wusb_cdid_store);

static ssize_t cbaf_wusb_device_band_groups_show(struct device *dev,
						 struct device_attribute *attr,
						 char *buf)
{
	struct usb_interface *iface = to_usb_interface(dev);
	struct cbaf *cbaf = usb_get_intfdata(iface);

	return scnprintf(buf, PAGE_SIZE, "0x%04x\n", cbaf->device_band_groups);
}

static DEVICE_ATTR(wusb_device_band_groups, 0600,
		   cbaf_wusb_device_band_groups_show,
		   NULL);

static ssize_t cbaf_wusb_device_name_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct usb_interface *iface = to_usb_interface(dev);
	struct cbaf *cbaf = usb_get_intfdata(iface);

	return scnprintf(buf, PAGE_SIZE, "%s\n", cbaf->device_name);
}
static DEVICE_ATTR(wusb_device_name, 0600, cbaf_wusb_device_name_show, NULL);

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
 * Send a new CC to the device.
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

	dev_dbg(dev, "Trying to upload CC:\n");
	ckhdid_printf(pr_cdid, sizeof(pr_cdid), &ccd->CHID);
	dev_dbg(dev, "  CHID       %s\n", pr_cdid);
	ckhdid_printf(pr_cdid, sizeof(pr_cdid), &ccd->CDID);
	dev_dbg(dev, "  CDID       %s\n", pr_cdid);
	dev_dbg(dev, "  Bandgroups 0x%04x\n", cbaf->host_band_groups);

	result = usb_control_msg(
		cbaf->usb_dev, usb_sndctrlpipe(cbaf->usb_dev, 0),
		CBAF_REQ_SET_ASSOCIATION_RESPONSE,
		USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
		0x0201, cbaf->usb_iface->cur_altsetting->desc.bInterfaceNumber,
		ccd, sizeof(*ccd), 1000 /* FIXME: arbitrary */);

	return result;
}

static ssize_t cbaf_wusb_ck_store(struct device *dev,
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
			"%02hhx %02hhx %02hhx %02hhx",
			&cbaf->ck.data[0] , &cbaf->ck.data[1],
			&cbaf->ck.data[2] , &cbaf->ck.data[3],
			&cbaf->ck.data[4] , &cbaf->ck.data[5],
			&cbaf->ck.data[6] , &cbaf->ck.data[7],
			&cbaf->ck.data[8] , &cbaf->ck.data[9],
			&cbaf->ck.data[10], &cbaf->ck.data[11],
			&cbaf->ck.data[12], &cbaf->ck.data[13],
			&cbaf->ck.data[14], &cbaf->ck.data[15]);
	if (result != 16)
		return -EINVAL;

	result = cbaf_cc_upload(cbaf);
	if (result < 0)
		return result;

	return size;
}
static DEVICE_ATTR(wusb_ck, 0600, NULL, cbaf_wusb_ck_store);

static struct attribute *cbaf_dev_attrs[] = {
	&dev_attr_wusb_host_name.attr,
	&dev_attr_wusb_host_band_groups.attr,
	&dev_attr_wusb_chid.attr,
	&dev_attr_wusb_cdid.attr,
	&dev_attr_wusb_device_name.attr,
	&dev_attr_wusb_device_band_groups.attr,
	&dev_attr_wusb_ck.attr,
	NULL,
};

static struct attribute_group cbaf_dev_attr_group = {
	.name = NULL,	/* we want them in the same directory */
	.attrs = cbaf_dev_attrs,
};

static int cbaf_probe(struct usb_interface *iface,
		      const struct usb_device_id *id)
{
	struct cbaf *cbaf;
	struct device *dev = &iface->dev;
	int result = -ENOMEM;

	cbaf = kzalloc(sizeof(*cbaf), GFP_KERNEL);
	if (cbaf == NULL)
		goto error_kzalloc;
	cbaf->buffer = kmalloc(512, GFP_KERNEL);
	if (cbaf->buffer == NULL)
		goto error_kmalloc_buffer;

	cbaf->buffer_size = 512;
	cbaf->usb_dev = usb_get_dev(interface_to_usbdev(iface));
	cbaf->usb_iface = usb_get_intf(iface);
	result = cbaf_check(cbaf);
	if (result < 0) {
		dev_err(dev, "This device is not WUSB-CBAF compliant"
			"and is not supported yet.\n");
		goto error_check;
	}

	result = sysfs_create_group(&dev->kobj, &cbaf_dev_attr_group);
	if (result < 0) {
		dev_err(dev, "Can't register sysfs attr group: %d\n", result);
		goto error_create_group;
	}
	usb_set_intfdata(iface, cbaf);
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
	kzfree(cbaf);
}

static const struct usb_device_id cbaf_id_table[] = {
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

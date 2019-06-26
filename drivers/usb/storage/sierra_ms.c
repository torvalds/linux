// SPDX-License-Identifier: GPL-2.0
#include <scsi/scsi.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_device.h>
#include <linux/usb.h>
#include <linux/module.h>
#include <linux/slab.h>

#include "usb.h"
#include "transport.h"
#include "protocol.h"
#include "scsiglue.h"
#include "sierra_ms.h"
#include "debug.h"

#define SWIMS_USB_REQUEST_SetSwocMode	0x0B
#define SWIMS_USB_REQUEST_GetSwocInfo	0x0A
#define SWIMS_USB_INDEX_SetMode		0x0000
#define SWIMS_SET_MODE_Modem		0x0001

#define TRU_NORMAL 			0x01
#define TRU_FORCE_MS 			0x02
#define TRU_FORCE_MODEM 		0x03

static unsigned int swi_tru_install = 1;
module_param(swi_tru_install, uint, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(swi_tru_install, "TRU-Install mode (1=Full Logic (def),"
		 " 2=Force CD-Rom, 3=Force Modem)");

struct swoc_info {
	__u8 rev;
	__u8 reserved[8];
	__u16 LinuxSKU;
	__u16 LinuxVer;
	__u8 reserved2[47];
} __attribute__((__packed__));

static bool containsFullLinuxPackage(struct swoc_info *swocInfo)
{
	if ((swocInfo->LinuxSKU >= 0x2100 && swocInfo->LinuxSKU <= 0x2FFF) ||
	   (swocInfo->LinuxSKU >= 0x7100 && swocInfo->LinuxSKU <= 0x7FFF))
		return true;
	else
		return false;
}

static int sierra_set_ms_mode(struct usb_device *udev, __u16 eSWocMode)
{
	int result;
	dev_dbg(&udev->dev, "SWIMS: %s", "DEVICE MODE SWITCH\n");
	result = usb_control_msg(udev, usb_sndctrlpipe(udev, 0),
			SWIMS_USB_REQUEST_SetSwocMode,	/* __u8 request      */
			USB_TYPE_VENDOR | USB_DIR_OUT,	/* __u8 request type */
			eSWocMode,			/* __u16 value       */
			0x0000,				/* __u16 index       */
			NULL,				/* void *data        */
			0,				/* __u16 size 	     */
			USB_CTRL_SET_TIMEOUT);		/* int timeout       */
	return result;
}


static int sierra_get_swoc_info(struct usb_device *udev,
				struct swoc_info *swocInfo)
{
	int result;

	dev_dbg(&udev->dev, "SWIMS: Attempting to get TRU-Install info\n");

	result = usb_control_msg(udev, usb_rcvctrlpipe(udev, 0),
			SWIMS_USB_REQUEST_GetSwocInfo,	/* __u8 request      */
			USB_TYPE_VENDOR | USB_DIR_IN,	/* __u8 request type */
			0,				/* __u16 value       */
			0,				/* __u16 index       */
			(void *) swocInfo,		/* void *data        */
			sizeof(struct swoc_info),	/* __u16 size 	     */
			USB_CTRL_SET_TIMEOUT);		/* int timeout 	     */

	swocInfo->LinuxSKU = le16_to_cpu(swocInfo->LinuxSKU);
	swocInfo->LinuxVer = le16_to_cpu(swocInfo->LinuxVer);
	return result;
}

static void debug_swoc(const struct device *dev, struct swoc_info *swocInfo)
{
	dev_dbg(dev, "SWIMS: SWoC Rev: %02d\n", swocInfo->rev);
	dev_dbg(dev, "SWIMS: Linux SKU: %04X\n", swocInfo->LinuxSKU);
	dev_dbg(dev, "SWIMS: Linux Version: %04X\n", swocInfo->LinuxVer);
}


static ssize_t truinst_show(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	struct swoc_info *swocInfo;
	struct usb_interface *intf = to_usb_interface(dev);
	struct usb_device *udev = interface_to_usbdev(intf);
	int result;
	if (swi_tru_install == TRU_FORCE_MS) {
		result = snprintf(buf, PAGE_SIZE, "Forced Mass Storage\n");
	} else {
		swocInfo = kmalloc(sizeof(struct swoc_info), GFP_KERNEL);
		if (!swocInfo) {
			snprintf(buf, PAGE_SIZE, "Error\n");
			return -ENOMEM;
		}
		result = sierra_get_swoc_info(udev, swocInfo);
		if (result < 0) {
			dev_dbg(dev, "SWIMS: failed SWoC query\n");
			kfree(swocInfo);
			snprintf(buf, PAGE_SIZE, "Error\n");
			return -EIO;
		}
		debug_swoc(dev, swocInfo);
		result = snprintf(buf, PAGE_SIZE,
			"REV=%02d SKU=%04X VER=%04X\n",
			swocInfo->rev,
			swocInfo->LinuxSKU,
			swocInfo->LinuxVer);
		kfree(swocInfo);
	}
	return result;
}
static DEVICE_ATTR_RO(truinst);

int sierra_ms_init(struct us_data *us)
{
	int result, retries;
	struct swoc_info *swocInfo;
	struct usb_device *udev;
	struct Scsi_Host *sh;

	retries = 3;
	result = 0;
	udev = us->pusb_dev;

	sh = us_to_host(us);
	scsi_get_host_dev(sh);

	/* Force Modem mode */
	if (swi_tru_install == TRU_FORCE_MODEM) {
		usb_stor_dbg(us, "SWIMS: Forcing Modem Mode\n");
		result = sierra_set_ms_mode(udev, SWIMS_SET_MODE_Modem);
		if (result < 0)
			usb_stor_dbg(us, "SWIMS: Failed to switch to modem mode\n");
		return -EIO;
	}
	/* Force Mass Storage mode (keep CD-Rom) */
	else if (swi_tru_install == TRU_FORCE_MS) {
		usb_stor_dbg(us, "SWIMS: Forcing Mass Storage Mode\n");
		goto complete;
	}
	/* Normal TRU-Install Logic */
	else {
		usb_stor_dbg(us, "SWIMS: Normal SWoC Logic\n");

		swocInfo = kmalloc(sizeof(struct swoc_info),
				GFP_KERNEL);
		if (!swocInfo)
			return -ENOMEM;

		retries = 3;
		do {
			retries--;
			result = sierra_get_swoc_info(udev, swocInfo);
			if (result < 0) {
				usb_stor_dbg(us, "SWIMS: Failed SWoC query\n");
				schedule_timeout_uninterruptible(2*HZ);
			}
		} while (retries && result < 0);

		if (result < 0) {
			usb_stor_dbg(us, "SWIMS: Completely failed SWoC query\n");
			kfree(swocInfo);
			return -EIO;
		}

		debug_swoc(&us->pusb_dev->dev, swocInfo);

		/*
		 * If there is not Linux software on the TRU-Install device
		 * then switch to modem mode
		 */
		if (!containsFullLinuxPackage(swocInfo)) {
			usb_stor_dbg(us, "SWIMS: Switching to Modem Mode\n");
			result = sierra_set_ms_mode(udev,
				SWIMS_SET_MODE_Modem);
			if (result < 0)
				usb_stor_dbg(us, "SWIMS: Failed to switch modem\n");
			kfree(swocInfo);
			return -EIO;
		}
		kfree(swocInfo);
	}
complete:
	return device_create_file(&us->pusb_intf->dev, &dev_attr_truinst);
}


/*
 *  Copyright 2009 Citrix Systems, Inc.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 *  For clarity, the licensor of this program does not intend that a
 *  "derivative work" include code which compiles header information from
 *  this program.
 *
 *  This code has been modified from its original by
 *  Hank Janssen <hjanssen@microsoft.com>
 *
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/workqueue.h>
#include <linux/input.h>
#include <linux/hid.h>
#include <linux/hiddev.h>
#include <linux/pci.h>
#include <linux/dmi.h>

//#include "osd.h"
#include "hv_api.h"
#include "logging.h"
#include "version_info.h"
#include "vmbus.h"
#include "mousevsc_api.h"

#define NBITS(x) (((x)/BITS_PER_LONG)+1)


/*
 * Data types
 */
struct input_device_context {
	struct vm_device	*device_ctx;
	struct hid_device	*hid_device;
	struct input_dev_info	device_info;
	int			connected;
};

struct mousevsc_driver_context {
	struct driver_context	drv_ctx;
	struct mousevsc_drv_obj	drv_obj;
};

static struct mousevsc_driver_context g_mousevsc_drv;

void mousevsc_deviceinfo_callback(struct hv_device *dev,
				  struct input_dev_info *info)
{
	struct vm_device *device_ctx = to_vm_device(dev);
	struct input_device_context *input_device_ctx =
		dev_get_drvdata(&device_ctx->device);

	memcpy(&input_device_ctx->device_info, info,
	       sizeof(struct input_dev_info));

	DPRINT_INFO(INPUTVSC_DRV, "mousevsc_deviceinfo_callback()");
}

void mousevsc_inputreport_callback(struct hv_device *dev, void *packet, u32 len)
{
	int ret = 0;

	struct vm_device *device_ctx = to_vm_device(dev);
	struct input_device_context *input_dev_ctx =
		dev_get_drvdata(&device_ctx->device);

	ret = hid_input_report(input_dev_ctx->hid_device,
			      HID_INPUT_REPORT, packet, len, 1);

	DPRINT_DBG(INPUTVSC_DRV, "hid_input_report (ret %d)", ret);
}

int mousevsc_hid_open(struct hid_device *hid)
{
	return 0;
}

void mousevsc_hid_close(struct hid_device *hid)
{
}

int mousevsc_probe(struct device *device)
{
	int ret = 0;

	struct driver_context *driver_ctx =
		driver_to_driver_context(device->driver);
	struct mousevsc_driver_context *mousevsc_drv_ctx =
		(struct mousevsc_driver_context *)driver_ctx;
	struct mousevsc_drv_obj *mousevsc_drv_obj = &mousevsc_drv_ctx->drv_obj;

	struct vm_device *device_ctx = device_to_vm_device(device);
	struct hv_device *device_obj = &device_ctx->device_obj;
	struct input_device_context *input_dev_ctx;

	input_dev_ctx = kmalloc(sizeof(struct input_device_context),
				GFP_KERNEL);

	dev_set_drvdata(device, input_dev_ctx);

	/* Call to the vsc driver to add the device */
	ret = mousevsc_drv_obj->Base.dev_add(device_obj, NULL);

	if (ret != 0) {
		DPRINT_ERR(INPUTVSC_DRV, "unable to add input vsc device");

		return -1;
	}

	return 0;
}


int mousevsc_remove(struct device *device)
{
	int ret = 0;

	struct driver_context *driver_ctx =
		driver_to_driver_context(device->driver);
	struct mousevsc_driver_context *mousevsc_drv_ctx =
		(struct mousevsc_driver_context *)driver_ctx;
	struct mousevsc_drv_obj *mousevsc_drv_obj = &mousevsc_drv_ctx->drv_obj;

	struct vm_device *device_ctx = device_to_vm_device(device);
	struct hv_device *device_obj = &device_ctx->device_obj;
	struct input_device_context *input_dev_ctx;

	input_dev_ctx = kmalloc(sizeof(struct input_device_context),
				GFP_KERNEL);

	dev_set_drvdata(device, input_dev_ctx);

	if (input_dev_ctx->connected) {
		hidinput_disconnect(input_dev_ctx->hid_device);
		input_dev_ctx->connected = 0;
	}

	if (!mousevsc_drv_obj->Base.dev_rm) {
		return -1;
	}

	/*
	 * Call to the vsc driver to let it know that the device
	 * is being removed
	 */
	ret = mousevsc_drv_obj->Base.dev_rm(device_obj);

	if (ret != 0) {
		DPRINT_ERR(INPUTVSC_DRV,
			   "unable to remove vsc device (ret %d)", ret);
	}

	kfree(input_dev_ctx);

	return ret;
}

void mousevsc_reportdesc_callback(struct hv_device *dev, void *packet, u32 len)
{
	struct vm_device *device_ctx = to_vm_device(dev);
	struct input_device_context *input_device_ctx =
		dev_get_drvdata(&device_ctx->device);
	struct hid_device *hid_dev;

	/* hid_debug = -1; */
	hid_dev = kmalloc(sizeof(struct hid_device), GFP_KERNEL);

	if (hid_parse_report(hid_dev, packet, len)) {
		DPRINT_INFO(INPUTVSC_DRV, "Unable to call hd_parse_report");
		return;
	}

	if (hid_dev) {
		DPRINT_INFO(INPUTVSC_DRV, "hid_device created");

		hid_dev->ll_driver->open  = mousevsc_hid_open;
		hid_dev->ll_driver->close = mousevsc_hid_close;

		hid_dev->bus =  0x06;  /* BUS_VIRTUAL */
		hid_dev->vendor = input_device_ctx->device_info.VendorID;
		hid_dev->product = input_device_ctx->device_info.ProductID;
		hid_dev->version = input_device_ctx->device_info.VersionNumber;
		hid_dev->dev = device_ctx->device;

		sprintf(hid_dev->name, "%s",
			input_device_ctx->device_info.Name);

		/*
		 * HJ Do we want to call it with a 0
		 */
		if (!hidinput_connect(hid_dev, 0)) {
			hid_dev->claimed |= HID_CLAIMED_INPUT;

			input_device_ctx->connected = 1;

			DPRINT_INFO(INPUTVSC_DRV,
				     "HID device claimed by input\n");
		}

		if (!hid_dev->claimed) {
			DPRINT_ERR(INPUTVSC_DRV,
				    "HID device not claimed by "
				    "input or hiddev\n");
		}

		input_device_ctx->hid_device = hid_dev;
	}

	kfree(hid_dev);
}

/*
 *
 * Name:	mousevsc_drv_init()
 *
 * Desc:	Driver initialization.
 */
int mousevsc_drv_init(int (*pfn_drv_init)(struct hv_driver *pfn_drv_init))
{
	int ret = 0;
	struct mousevsc_drv_obj *input_drv_obj = &g_mousevsc_drv.drv_obj;
	struct driver_context *drv_ctx = &g_mousevsc_drv.drv_ctx;

//	vmbus_get_interface(&input_drv_obj->Base.VmbusChannelInterface);

	input_drv_obj->OnDeviceInfo = mousevsc_deviceinfo_callback;
	input_drv_obj->OnInputReport = mousevsc_inputreport_callback;
	input_drv_obj->OnReportDescriptor = mousevsc_reportdesc_callback;

	/* Callback to client driver to complete the initialization */
	pfn_drv_init(&input_drv_obj->Base);

	drv_ctx->driver.name = input_drv_obj->Base.name;
	memcpy(&drv_ctx->class_id, &input_drv_obj->Base.dev_type,
	       sizeof(struct hv_guid));

	drv_ctx->probe = mousevsc_probe;
	drv_ctx->remove = mousevsc_remove;

	/* The driver belongs to vmbus */
	vmbus_child_driver_register(drv_ctx);

	return ret;
}


int mousevsc_drv_exit_cb(struct device *dev, void *data)
{
	struct device **curr = (struct device **)data;
	*curr = dev;

	return 1;
}

void mousevsc_drv_exit(void)
{
	struct mousevsc_drv_obj *mousevsc_drv_obj = &g_mousevsc_drv.drv_obj;
	struct driver_context *drv_ctx = &g_mousevsc_drv.drv_ctx;
	int ret;

	struct device *current_dev = NULL;

	while (1) {
		current_dev = NULL;

		/* Get the device */
		ret = driver_for_each_device(&drv_ctx->driver, NULL, (void *)&current_dev, mousevsc_drv_exit_cb);
		if (ret)
			printk(KERN_ERR "Can't find mouse device!\n");

		if (current_dev == NULL)
			break;

		/* Initiate removal from the top-down */
		device_unregister(current_dev);
	}

	if (mousevsc_drv_obj->Base.cleanup)
		mousevsc_drv_obj->Base.cleanup(&mousevsc_drv_obj->Base);

	vmbus_child_driver_unregister(drv_ctx);

	return;
}

static int __init mousevsc_init(void)
{
	int ret;

	DPRINT_INFO(INPUTVSC_DRV, "Hyper-V Mouse driver initializing.");

	ret = mousevsc_drv_init(mouse_vsc_initialize);

	return ret;
}

static void __exit mousevsc_exit(void)
{
	mousevsc_drv_exit();
}

/*
 * We use a PCI table to determine if we should autoload this driver  This is
 * needed by distro tools to determine if the hyperv drivers should be
 * installed and/or configured.  We don't do anything else with the table, but
 * it needs to be present.
 */
const static struct pci_device_id microsoft_hv_pci_table[] = {
	{ PCI_DEVICE(0x1414, 0x5353) },	/* VGA compatible controller */
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, microsoft_hv_pci_table);

MODULE_LICENSE("GPL");
MODULE_VERSION(HV_DRV_VERSION);
module_init(mousevsc_init);
module_exit(mousevsc_exit);


/*=====================================================
 * CopyRight (C) 2007 Qualcomm Inc. All Rights Reserved.
 *
 *
 * This file is part of Express Card USB Driver
 *====================================================
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/usb.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/firmware.h>
#include "ft1000_usb.h"

#include <linux/kthread.h>

MODULE_DESCRIPTION("FT1000 EXPRESS CARD DRIVER");
MODULE_LICENSE("Dual MPL/GPL");
MODULE_SUPPORTED_DEVICE("QFT FT1000 Express Cards");

void *pFileStart;
size_t FileLength;

#define VENDOR_ID 0x1291	/* Qualcomm vendor id */
#define PRODUCT_ID 0x11		/* fake product id */

/* table of devices that work with this driver */
static struct usb_device_id id_table[] = {
	{USB_DEVICE(VENDOR_ID, PRODUCT_ID)},
	{},
};

MODULE_DEVICE_TABLE(usb, id_table);

static bool gPollingfailed;
static int ft1000_poll_thread(void *arg)
{
	int ret;

	while (!kthread_should_stop()) {
		usleep_range(10000, 11000);
		if (!gPollingfailed) {
			ret = ft1000_poll(arg);
			if (ret != 0) {
				pr_debug("polling failed\n");
				gPollingfailed = true;
			}
		}
	}
	return 0;
}

static int ft1000_probe(struct usb_interface *interface,
			const struct usb_device_id *id)
{
	struct usb_host_interface *iface_desc;
	struct usb_endpoint_descriptor *endpoint;
	struct usb_device *dev;
	unsigned numaltsetting;
	int i, ret = 0, size;

	struct ft1000_usb *ft1000dev;
	struct ft1000_info *pft1000info = NULL;
	const struct firmware *dsp_fw;

	ft1000dev = kzalloc(sizeof(struct ft1000_usb), GFP_KERNEL);
	if (!ft1000dev)
		return -ENOMEM;

	dev = interface_to_usbdev(interface);
	pr_debug("usb device descriptor info - number of configuration is %d\n",
		 dev->descriptor.bNumConfigurations);

	ft1000dev->dev = dev;
	ft1000dev->status = 0;
	ft1000dev->net = NULL;
	ft1000dev->tx_urb = usb_alloc_urb(0, GFP_KERNEL);
	ft1000dev->rx_urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!ft1000dev->tx_urb || !ft1000dev->rx_urb) {
		ret = -ENOMEM;
		goto err_fw;
	}

	numaltsetting = interface->num_altsetting;
	pr_debug("number of alt settings is: %d\n", numaltsetting);
	iface_desc = interface->cur_altsetting;
	pr_debug("number of endpoints is: %d\n",
		 iface_desc->desc.bNumEndpoints);
	pr_debug("descriptor type is: %d\n", iface_desc->desc.bDescriptorType);
	pr_debug("interface number is: %d\n",
		 iface_desc->desc.bInterfaceNumber);
	pr_debug("alternatesetting is: %d\n",
		 iface_desc->desc.bAlternateSetting);
	pr_debug("interface class is: %d\n", iface_desc->desc.bInterfaceClass);
	pr_debug("control endpoint info:\n");
	pr_debug("descriptor0 type -- %d\n",
		 iface_desc->endpoint[0].desc.bmAttributes);
	pr_debug("descriptor1 type -- %d\n",
		 iface_desc->endpoint[1].desc.bmAttributes);
	pr_debug("descriptor2 type -- %d\n",
		 iface_desc->endpoint[2].desc.bmAttributes);

	for (i = 0; i < iface_desc->desc.bNumEndpoints; i++) {
		endpoint =
			(struct usb_endpoint_descriptor *)&iface_desc->
			endpoint[i].desc;
		pr_debug("endpoint %d\n", i);
		pr_debug("bEndpointAddress=%x, bmAttributes=%x\n",
			 endpoint->bEndpointAddress, endpoint->bmAttributes);
		if ((endpoint->bEndpointAddress & USB_DIR_IN)
		    && ((endpoint->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) ==
			USB_ENDPOINT_XFER_BULK)) {
			ft1000dev->bulk_in_endpointAddr =
				endpoint->bEndpointAddress;
			pr_debug("in: %d\n", endpoint->bEndpointAddress);
		}

		if (!(endpoint->bEndpointAddress & USB_DIR_IN)
		    && ((endpoint->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) ==
			USB_ENDPOINT_XFER_BULK)) {
			ft1000dev->bulk_out_endpointAddr =
				endpoint->bEndpointAddress;
			pr_debug("out: %d\n", endpoint->bEndpointAddress);
		}
	}

	pr_debug("bulk_in=%d, bulk_out=%d\n",
		 ft1000dev->bulk_in_endpointAddr,
		 ft1000dev->bulk_out_endpointAddr);

	ret = request_firmware(&dsp_fw, "ft3000.img", &dev->dev);
	if (ret < 0) {
		dev_err(interface->usb_dev, "Error request_firmware()\n");
		goto err_fw;
	}

	size = max_t(uint, dsp_fw->size, 4096);
	pFileStart = kmalloc(size, GFP_KERNEL);

	if (!pFileStart) {
		release_firmware(dsp_fw);
		ret = -ENOMEM;
		goto err_fw;
	}

	memcpy(pFileStart, dsp_fw->data, dsp_fw->size);
	FileLength = dsp_fw->size;
	release_firmware(dsp_fw);

	pr_debug("start downloading dsp image...\n");

	ret = init_ft1000_netdev(ft1000dev);
	if (ret)
		goto err_load;

	pft1000info = netdev_priv(ft1000dev->net);

	pr_debug("pft1000info=%p\n", pft1000info);
	ret = dsp_reload(ft1000dev);
	if (ret) {
		dev_err(interface->usb_dev,
			"Problem with DSP image loading\n");
		goto err_load;
	}

	gPollingfailed = false;
	ft1000dev->pPollThread =
		kthread_run(ft1000_poll_thread, ft1000dev, "ft1000_poll");

	if (IS_ERR(ft1000dev->pPollThread)) {
		ret = PTR_ERR(ft1000dev->pPollThread);
		goto err_load;
	}

	msleep(500);

	while (!pft1000info->CardReady) {
		if (gPollingfailed) {
			ret = -EIO;
			goto err_thread;
		}
		msleep(100);
		pr_debug("Waiting for Card Ready\n");
	}

	pr_debug("Card Ready!!!! Registering network device\n");

	ret = reg_ft1000_netdev(ft1000dev, interface);
	if (ret)
		goto err_thread;

	ft1000dev->NetDevRegDone = 1;

	return 0;

err_thread:
	kthread_stop(ft1000dev->pPollThread);
err_load:
	kfree(pFileStart);
err_fw:
	usb_free_urb(ft1000dev->rx_urb);
	usb_free_urb(ft1000dev->tx_urb);
	kfree(ft1000dev);
	return ret;
}

static void ft1000_disconnect(struct usb_interface *interface)
{
	struct ft1000_info *pft1000info;
	struct ft1000_usb *ft1000dev;

	pft1000info = (struct ft1000_info *)usb_get_intfdata(interface);
	pr_debug("In disconnect pft1000info=%p\n", pft1000info);

	if (pft1000info) {
		ft1000dev = pft1000info->priv;
		if (ft1000dev->pPollThread)
			kthread_stop(ft1000dev->pPollThread);

		pr_debug("threads are terminated\n");

		if (ft1000dev->net) {
			pr_debug("destroy char driver\n");
			ft1000_destroy_dev(ft1000dev->net);
			unregister_netdev(ft1000dev->net);
			pr_debug("network device unregistered\n");
			free_netdev(ft1000dev->net);

		}

		usb_free_urb(ft1000dev->rx_urb);
		usb_free_urb(ft1000dev->tx_urb);

		pr_debug("urb freed\n");

		kfree(ft1000dev);
	}
	kfree(pFileStart);
}

static struct usb_driver ft1000_usb_driver = {
	.name = "ft1000usb",
	.probe = ft1000_probe,
	.disconnect = ft1000_disconnect,
	.id_table = id_table,
};

module_usb_driver(ft1000_usb_driver);

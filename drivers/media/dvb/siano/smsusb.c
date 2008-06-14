/*
 *  Driver for the Siano SMS10xx USB dongle
 *
 *  author: Anatoly Greenblat
 *
 *  Copyright (c), 2005-2008 Siano Mobile Silicon, Inc.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 3 as
 *  published by the Free Software Foundation;
 *
 *  Software distributed under the License is distributed on an "AS IS"
 *  basis, WITHOUT WARRANTY OF ANY KIND, either express or implied.
 *
 *  See the GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/usb.h>
#include <linux/firmware.h>

#include "smscoreapi.h"

#define USB1_BUFFER_SIZE		0x1000
#define USB2_BUFFER_SIZE		0x4000

#define MAX_BUFFERS		50
#define MAX_URBS		10

typedef struct _smsusb_device smsusb_device_t;

typedef struct _smsusb_urb
{
	smscore_buffer_t *cb;
	smsusb_device_t	*dev;

	struct urb		urb;
} smsusb_urb_t;

typedef struct _smsusb_device
{
	struct usb_device *udev;
	smscore_device_t *coredev;

	smsusb_urb_t 	surbs[MAX_URBS];

	int				response_alignment;
	int				buffer_size;
} *psmsusb_device_t;

int smsusb_submit_urb(smsusb_device_t *dev, smsusb_urb_t *surb);

void smsusb_onresponse(struct urb *urb)
{
	smsusb_urb_t *surb = (smsusb_urb_t *) urb->context;
	smsusb_device_t *dev = surb->dev;

	if (urb->status < 0)
	{
		printk(KERN_INFO "%s error, urb status %d, %d bytes\n", __func__, urb->status, urb->actual_length);
		return;
	}

	if (urb->actual_length > 0)
	{
		SmsMsgHdr_ST *phdr = (SmsMsgHdr_ST *) surb->cb->p;

		if (urb->actual_length >= phdr->msgLength)
		{
			surb->cb->size = phdr->msgLength;

			if (dev->response_alignment && (phdr->msgFlags & MSG_HDR_FLAG_SPLIT_MSG))
			{
				surb->cb->offset = dev->response_alignment + ((phdr->msgFlags >> 8) & 3);

				// sanity check
				if (((int) phdr->msgLength + surb->cb->offset) > urb->actual_length)
				{
					printk("%s: invalid response msglen %d offset %d size %d\n", __func__, phdr->msgLength, surb->cb->offset, urb->actual_length);
					goto exit_and_resubmit;
				}

				// move buffer pointer and copy header to its new location
				memcpy((char *) phdr + surb->cb->offset,
				       phdr, sizeof(SmsMsgHdr_ST));
			}
			else
				surb->cb->offset = 0;

			smscore_onresponse(dev->coredev, surb->cb);
			surb->cb = NULL;
		}
		else
		{
			printk("%s invalid response msglen %d actual %d\n", __func__, phdr->msgLength, urb->actual_length);
		}
	}

exit_and_resubmit:
	smsusb_submit_urb(dev, surb);
}

int smsusb_submit_urb(smsusb_device_t *dev, smsusb_urb_t *surb)
{
	if (!surb->cb)
	{
		surb->cb = smscore_getbuffer(dev->coredev);
		if (!surb->cb)
		{
			printk(KERN_INFO "%s smscore_getbuffer(...) returned NULL\n", __func__);
			return -ENOMEM;
		}
	}

	usb_fill_bulk_urb(
		&surb->urb,
		dev->udev,
		usb_rcvbulkpipe(dev->udev, 0x81),
		surb->cb->p,
		dev->buffer_size,
		smsusb_onresponse,
		surb
	);
	surb->urb.transfer_dma = surb->cb->phys;
	surb->urb.transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

	return usb_submit_urb(&surb->urb, GFP_ATOMIC);
}

void smsusb_stop_streaming(smsusb_device_t *dev)
{
	int i;

	for (i = 0; i < MAX_URBS; i ++)
	{
		usb_kill_urb(&dev->surbs[i].urb);

		if (dev->surbs[i].cb)
		{
			smscore_putbuffer(dev->coredev, dev->surbs[i].cb);
			dev->surbs[i].cb = NULL;
		}
	}
}

int smsusb_start_streaming(smsusb_device_t *dev)
{
	int i, rc;

	for (i = 0; i < MAX_URBS; i ++)
	{
		rc = smsusb_submit_urb(dev, &dev->surbs[i]);
		if (rc < 0)
		{
			printk(KERN_INFO "%s smsusb_submit_urb(...) failed\n", __func__);
			smsusb_stop_streaming(dev);
			break;
		}
	}

	return rc;
}

int smsusb_sendrequest(void *context, void *buffer, size_t size)
{
	smsusb_device_t *dev = (smsusb_device_t *) context;
	int dummy;

	return usb_bulk_msg(dev->udev, usb_sndbulkpipe(dev->udev, 2), buffer, size, &dummy, 1000);
}

char *smsusb1_fw_lkup[] =
{
	"dvbt_stellar_usb.inp",
	"dvbh_stellar_usb.inp",
	"tdmb_stellar_usb.inp",
	"none",
	"dvbt_bda_stellar_usb.inp",
};

int smsusb1_load_firmware(struct usb_device *udev, int id)
{
	const struct firmware *fw;
	u8 *fw_buffer;
	int rc, dummy;

	if (id < DEVICE_MODE_DVBT || id > DEVICE_MODE_DVBT_BDA)
	{
		printk(KERN_INFO "%s invalid firmware id specified %d\n", __func__, id);
		return -EINVAL;
	}

	rc = request_firmware(&fw, smsusb1_fw_lkup[id], &udev->dev);
	if (rc < 0)
	{
		printk(KERN_INFO "%s failed to open \"%s\" mode %d\n", __func__, smsusb1_fw_lkup[id], id);
		return rc;
	}

	fw_buffer = kmalloc(fw->size, GFP_KERNEL);
	if (fw_buffer)
	{
		memcpy(fw_buffer, fw->data, fw->size);

		rc = usb_bulk_msg(udev, usb_sndbulkpipe(udev, 2), fw_buffer, fw->size, &dummy, 1000);

		printk(KERN_INFO "%s: sent %d(%d) bytes, rc %d\n", __func__, fw->size, dummy, rc);

		kfree(fw_buffer);
	}
	else
	{
		printk(KERN_INFO "failed to allocate firmware buffer\n");
		rc = -ENOMEM;
	}

	release_firmware(fw);

	return rc;
}

void smsusb1_detectmode(void *context, int *mode)
{
	char *product_string = ((smsusb_device_t *) context)->udev->product;

	*mode = DEVICE_MODE_NONE;

	if (!product_string)
	{
		product_string = "none";
		printk("%s product string not found\n", __func__);
	}
	else
	{
		if (strstr(product_string, "DVBH"))
			*mode = 1;
		else if (strstr(product_string, "BDA"))
			*mode = 4;
		else if (strstr(product_string, "DVBT"))
			*mode = 0;
		else if (strstr(product_string, "TDMB"))
			*mode = 2;
	}

	printk("%s: %d \"%s\"\n", __func__, *mode, product_string);
}

int smsusb1_setmode(void *context, int mode)
{
	SmsMsgHdr_ST Msg = { MSG_SW_RELOAD_REQ, 0, HIF_TASK, sizeof(SmsMsgHdr_ST), 0 };

	if (mode < DEVICE_MODE_DVBT || mode > DEVICE_MODE_DVBT_BDA)
	{
		printk(KERN_INFO "%s invalid firmware id specified %d\n", __func__, mode);
		return -EINVAL;
	}

	return smsusb_sendrequest(context, &Msg, sizeof(Msg));
}

void smsusb_term_device(struct usb_interface *intf)
{
	smsusb_device_t *dev = (smsusb_device_t *) usb_get_intfdata(intf);

	if (dev)
	{
		smsusb_stop_streaming(dev);

		// unregister from smscore
		if (dev->coredev)
			smscore_unregister_device(dev->coredev);

		kfree(dev);

		printk(KERN_INFO "%s device %p destroyed\n", __func__, dev);
	}

	usb_set_intfdata(intf, NULL);
}

int smsusb_init_device(struct usb_interface *intf)
{
	smsdevice_params_t params;
	smsusb_device_t *dev;
	int i, rc;

	// create device object
	dev = kzalloc(sizeof(smsusb_device_t), GFP_KERNEL);
	if (!dev)
	{
		printk(KERN_INFO "%s kzalloc(sizeof(smsusb_device_t) failed\n", __func__);
		return -ENOMEM;
	}

	memset(&params, 0, sizeof(params));
	usb_set_intfdata(intf, dev);
	dev->udev = interface_to_usbdev(intf);

	switch (dev->udev->descriptor.idProduct) {
	case 0x100:
		dev->buffer_size = USB1_BUFFER_SIZE;

		params.setmode_handler = smsusb1_setmode;
		params.detectmode_handler = smsusb1_detectmode;
		params.device_type = SMS_STELLAR;
		printk(KERN_INFO "%s stellar device found\n", __func__ );
		break;
	default:
		if (dev->udev->descriptor.idProduct == 0x200) {
			params.device_type = SMS_NOVA_A0;
			printk(KERN_INFO "%s nova A0 found\n", __func__ );
		} else if (dev->udev->descriptor.idProduct == 0x201) {
			params.device_type = SMS_NOVA_B0;
			printk(KERN_INFO "%s nova B0 found\n", __func__);
		} else {
			params.device_type = SMS_VEGA;
			printk(KERN_INFO "%s Vega found\n", __func__);
		}

		dev->buffer_size = USB2_BUFFER_SIZE;
		dev->response_alignment = dev->udev->ep_in[1]->desc.wMaxPacketSize - sizeof(SmsMsgHdr_ST);

		params.flags |= SMS_DEVICE_FAMILY2;
		break;
	}

	params.device = &dev->udev->dev;
	params.buffer_size = dev->buffer_size;
	params.num_buffers = MAX_BUFFERS;
	params.sendrequest_handler = smsusb_sendrequest;
	params.context = dev;
	snprintf(params.devpath, sizeof(params.devpath), "usb\\%d-%s", dev->udev->bus->busnum, dev->udev->devpath);

	// register in smscore
	rc = smscore_register_device(&params, &dev->coredev);
	if (rc < 0)
	{
		printk(KERN_INFO "%s smscore_register_device(...) failed, rc %d\n", __func__, rc);
		smsusb_term_device(intf);
		return rc;
	}

	// initialize urbs
	for (i = 0; i < MAX_URBS; i ++)
	{
		dev->surbs[i].dev = dev;
		usb_init_urb(&dev->surbs[i].urb);
	}

	printk(KERN_INFO "%s smsusb_start_streaming(...).\n", __func__);
	rc = smsusb_start_streaming(dev);
	if (rc < 0)
	{
		printk(KERN_INFO "%s smsusb_start_streaming(...) failed\n", __func__);
		smsusb_term_device(intf);
		return rc;
	}

	rc = smscore_start_device(dev->coredev);
	if (rc < 0)
	{
		printk(KERN_INFO "%s smscore_start_device(...) failed\n", __func__);
		smsusb_term_device(intf);
		return rc;
	}

	printk(KERN_INFO "%s device %p created\n", __func__, dev);

	return rc;
}

int smsusb_probe(struct usb_interface *intf, const struct usb_device_id *id)
{
	struct usb_device *udev = interface_to_usbdev(intf);
	char devpath[32];
	int i, rc;

	rc = usb_clear_halt(udev, usb_rcvbulkpipe(udev, 0x81));
	rc = usb_clear_halt(udev, usb_rcvbulkpipe(udev, 0x02));

	if (intf->num_altsetting > 0)
	{
		rc = usb_set_interface(udev, intf->cur_altsetting->desc.bInterfaceNumber, 0);
		if (rc < 0)
		{
			printk(KERN_INFO "%s usb_set_interface failed, rc %d\n", __func__, rc);
			return rc;
		}
	}

	printk(KERN_INFO "smsusb_probe %d\n", intf->cur_altsetting->desc.bInterfaceNumber);
	for (i = 0; i < intf->cur_altsetting->desc.bNumEndpoints; i ++)
		printk(KERN_INFO "endpoint %d %02x %02x %d\n", i, intf->cur_altsetting->endpoint[i].desc.bEndpointAddress, intf->cur_altsetting->endpoint[i].desc.bmAttributes, intf->cur_altsetting->endpoint[i].desc.wMaxPacketSize);

	if (udev->actconfig->desc.bNumInterfaces == 2 && intf->cur_altsetting->desc.bInterfaceNumber == 0)
	{
		printk(KERN_INFO "rom interface 0 is not used\n");
		return -ENODEV;
	}

	if (intf->cur_altsetting->desc.bInterfaceNumber == 1)
	{
		snprintf(devpath, sizeof(devpath), "usb\\%d-%s", udev->bus->busnum, udev->devpath);
		printk(KERN_INFO "stellar device was found.\n");
		return smsusb1_load_firmware(udev, smscore_registry_getmode(devpath));
	}

	rc = smsusb_init_device(intf);
	printk(KERN_INFO  "%s  rc %d\n", __func__, rc);
	return rc;
}

void smsusb_disconnect(struct usb_interface *intf)
{
	smsusb_term_device(intf);
}

static struct usb_device_id smsusb_id_table [] = {
	{ USB_DEVICE(0x187F, 0x0010) },
	{ USB_DEVICE(0x187F, 0x0100) },
	{ USB_DEVICE(0x187F, 0x0200) },
	{ }		/* Terminating entry */
};
MODULE_DEVICE_TABLE (usb, smsusb_id_table);

static struct usb_driver smsusb_driver = {
	.name			= "smsusb",
	.probe			= smsusb_probe,
	.disconnect 	= smsusb_disconnect,
	.id_table		= smsusb_id_table,
};

int smsusb_register(void)
{
	int rc = usb_register(&smsusb_driver);
	if (rc)
		printk(KERN_INFO "usb_register failed. Error number %d\n", rc);

	printk(KERN_INFO "%s\n", __func__);

	return rc;
}

void smsusb_unregister(void)
{
	/* Regular USB Cleanup */
	usb_deregister(&smsusb_driver);
	printk(KERN_INFO "%s\n", __func__);
}


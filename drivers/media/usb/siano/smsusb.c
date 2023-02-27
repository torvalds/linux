// SPDX-License-Identifier: GPL-2.0-or-later
/****************************************************************

Siano Mobile Silicon, Inc.
MDTV receiver kernel modules.
Copyright (C) 2005-2009, Uri Shkolnik, Anatoly Greenblat


****************************************************************/

#include "smscoreapi.h"

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/usb.h>
#include <linux/firmware.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <media/media-device.h>

#include "sms-cards.h"
#include "smsendian.h"

#define USB1_BUFFER_SIZE		0x1000
#define USB2_BUFFER_SIZE		0x2000

#define MAX_BUFFERS		50
#define MAX_URBS		10

struct smsusb_device_t;

enum smsusb_state {
	SMSUSB_DISCONNECTED,
	SMSUSB_SUSPENDED,
	SMSUSB_ACTIVE
};

struct smsusb_urb_t {
	struct list_head entry;
	struct smscore_buffer_t *cb;
	struct smsusb_device_t *dev;

	struct urb urb;

	/* For the bottom half */
	struct work_struct wq;
};

struct smsusb_device_t {
	struct usb_device *udev;
	struct smscore_device_t *coredev;

	struct smsusb_urb_t	surbs[MAX_URBS];

	int		response_alignment;
	int		buffer_size;

	unsigned char in_ep;
	unsigned char out_ep;
	enum smsusb_state state;
};

static int smsusb_submit_urb(struct smsusb_device_t *dev,
			     struct smsusb_urb_t *surb);

/*
 * Completing URB's callback handler - bottom half (process context)
 * submits the URB prepared on smsusb_onresponse()
 */
static void do_submit_urb(struct work_struct *work)
{
	struct smsusb_urb_t *surb = container_of(work, struct smsusb_urb_t, wq);
	struct smsusb_device_t *dev = surb->dev;

	smsusb_submit_urb(dev, surb);
}

/*
 * Completing URB's callback handler - top half (interrupt context)
 * adds completing sms urb to the global surbs list and activtes the worker
 * thread the surb
 * IMPORTANT - blocking functions must not be called from here !!!

 * @param urb pointer to a completing urb object
 */
static void smsusb_onresponse(struct urb *urb)
{
	struct smsusb_urb_t *surb = (struct smsusb_urb_t *) urb->context;
	struct smsusb_device_t *dev = surb->dev;

	if (urb->status == -ESHUTDOWN) {
		pr_err("error, urb status %d (-ESHUTDOWN), %d bytes\n",
			urb->status, urb->actual_length);
		return;
	}

	if ((urb->actual_length > 0) && (urb->status == 0)) {
		struct sms_msg_hdr *phdr = (struct sms_msg_hdr *)surb->cb->p;

		smsendian_handle_message_header(phdr);
		if (urb->actual_length >= phdr->msg_length) {
			surb->cb->size = phdr->msg_length;

			if (dev->response_alignment &&
			    (phdr->msg_flags & MSG_HDR_FLAG_SPLIT_MSG)) {

				surb->cb->offset =
					dev->response_alignment +
					((phdr->msg_flags >> 8) & 3);

				/* sanity check */
				if (((int) phdr->msg_length +
				     surb->cb->offset) > urb->actual_length) {
					pr_err("invalid response msglen %d offset %d size %d\n",
						phdr->msg_length,
						surb->cb->offset,
						urb->actual_length);
					goto exit_and_resubmit;
				}

				/* move buffer pointer and
				 * copy header to its new location */
				memcpy((char *) phdr + surb->cb->offset,
				       phdr, sizeof(struct sms_msg_hdr));
			} else
				surb->cb->offset = 0;

			pr_debug("received %s(%d) size: %d\n",
				  smscore_translate_msg(phdr->msg_type),
				  phdr->msg_type, phdr->msg_length);

			smsendian_handle_rx_message((struct sms_msg_data *) phdr);

			smscore_onresponse(dev->coredev, surb->cb);
			surb->cb = NULL;
		} else {
			pr_err("invalid response msglen %d actual %d\n",
				phdr->msg_length, urb->actual_length);
		}
	} else
		pr_err("error, urb status %d, %d bytes\n",
			urb->status, urb->actual_length);


exit_and_resubmit:
	INIT_WORK(&surb->wq, do_submit_urb);
	schedule_work(&surb->wq);
}

static int smsusb_submit_urb(struct smsusb_device_t *dev,
			     struct smsusb_urb_t *surb)
{
	if (!surb->cb) {
		/* This function can sleep */
		surb->cb = smscore_getbuffer(dev->coredev);
		if (!surb->cb) {
			pr_err("smscore_getbuffer(...) returned NULL\n");
			return -ENOMEM;
		}
	}

	usb_fill_bulk_urb(
		&surb->urb,
		dev->udev,
		usb_rcvbulkpipe(dev->udev, dev->in_ep),
		surb->cb->p,
		dev->buffer_size,
		smsusb_onresponse,
		surb
	);
	surb->urb.transfer_flags |= URB_FREE_BUFFER;

	return usb_submit_urb(&surb->urb, GFP_ATOMIC);
}

static void smsusb_stop_streaming(struct smsusb_device_t *dev)
{
	int i;

	for (i = 0; i < MAX_URBS; i++) {
		usb_kill_urb(&dev->surbs[i].urb);
		if (dev->surbs[i].wq.func)
			cancel_work_sync(&dev->surbs[i].wq);

		if (dev->surbs[i].cb) {
			smscore_putbuffer(dev->coredev, dev->surbs[i].cb);
			dev->surbs[i].cb = NULL;
		}
	}
}

static int smsusb_start_streaming(struct smsusb_device_t *dev)
{
	int i, rc;

	for (i = 0; i < MAX_URBS; i++) {
		rc = smsusb_submit_urb(dev, &dev->surbs[i]);
		if (rc < 0) {
			pr_err("smsusb_submit_urb(...) failed\n");
			smsusb_stop_streaming(dev);
			break;
		}
	}

	return rc;
}

static int smsusb_sendrequest(void *context, void *buffer, size_t size)
{
	struct smsusb_device_t *dev = (struct smsusb_device_t *) context;
	struct sms_msg_hdr *phdr;
	int dummy, ret;

	if (dev->state != SMSUSB_ACTIVE) {
		pr_debug("Device not active yet\n");
		return -ENOENT;
	}

	phdr = kmemdup(buffer, size, GFP_KERNEL);
	if (!phdr)
		return -ENOMEM;

	pr_debug("sending %s(%d) size: %d\n",
		  smscore_translate_msg(phdr->msg_type), phdr->msg_type,
		  phdr->msg_length);

	smsendian_handle_tx_message((struct sms_msg_data *) phdr);
	smsendian_handle_message_header((struct sms_msg_hdr *)phdr);
	ret = usb_bulk_msg(dev->udev, usb_sndbulkpipe(dev->udev, 2),
			    phdr, size, &dummy, 1000);

	kfree(phdr);
	return ret;
}

static char *smsusb1_fw_lkup[] = {
	"dvbt_stellar_usb.inp",
	"dvbh_stellar_usb.inp",
	"tdmb_stellar_usb.inp",
	"none",
	"dvbt_bda_stellar_usb.inp",
};

static inline char *sms_get_fw_name(int mode, int board_id)
{
	char **fw = sms_get_board(board_id)->fw;
	return (fw && fw[mode]) ? fw[mode] : smsusb1_fw_lkup[mode];
}

static int smsusb1_load_firmware(struct usb_device *udev, int id, int board_id)
{
	const struct firmware *fw;
	u8 *fw_buffer;
	int rc, dummy;
	char *fw_filename;

	if (id < 0)
		id = sms_get_board(board_id)->default_mode;

	if (id < DEVICE_MODE_DVBT || id > DEVICE_MODE_DVBT_BDA) {
		pr_err("invalid firmware id specified %d\n", id);
		return -EINVAL;
	}

	fw_filename = sms_get_fw_name(id, board_id);

	rc = request_firmware(&fw, fw_filename, &udev->dev);
	if (rc < 0) {
		pr_warn("failed to open '%s' mode %d, trying again with default firmware\n",
			fw_filename, id);

		fw_filename = smsusb1_fw_lkup[id];
		rc = request_firmware(&fw, fw_filename, &udev->dev);
		if (rc < 0) {
			pr_warn("failed to open '%s' mode %d\n",
				 fw_filename, id);

			return rc;
		}
	}

	fw_buffer = kmalloc(fw->size, GFP_KERNEL);
	if (fw_buffer) {
		memcpy(fw_buffer, fw->data, fw->size);

		rc = usb_bulk_msg(udev, usb_sndbulkpipe(udev, 2),
				  fw_buffer, fw->size, &dummy, 1000);

		pr_debug("sent %zu(%d) bytes, rc %d\n", fw->size, dummy, rc);

		kfree(fw_buffer);
	} else {
		pr_err("failed to allocate firmware buffer\n");
		rc = -ENOMEM;
	}
	pr_debug("read FW %s, size=%zu\n", fw_filename, fw->size);

	release_firmware(fw);

	return rc;
}

static void smsusb1_detectmode(void *context, int *mode)
{
	char *product_string =
		((struct smsusb_device_t *) context)->udev->product;

	*mode = DEVICE_MODE_NONE;

	if (!product_string) {
		product_string = "none";
		pr_err("product string not found\n");
	} else if (strstr(product_string, "DVBH"))
		*mode = 1;
	else if (strstr(product_string, "BDA"))
		*mode = 4;
	else if (strstr(product_string, "DVBT"))
		*mode = 0;
	else if (strstr(product_string, "TDMB"))
		*mode = 2;

	pr_debug("%d \"%s\"\n", *mode, product_string);
}

static int smsusb1_setmode(void *context, int mode)
{
	struct sms_msg_hdr msg = { MSG_SW_RELOAD_REQ, 0, HIF_TASK,
			     sizeof(struct sms_msg_hdr), 0 };

	if (mode < DEVICE_MODE_DVBT || mode > DEVICE_MODE_DVBT_BDA) {
		pr_err("invalid firmware id specified %d\n", mode);
		return -EINVAL;
	}

	return smsusb_sendrequest(context, &msg, sizeof(msg));
}

static void smsusb_term_device(struct usb_interface *intf)
{
	struct smsusb_device_t *dev = usb_get_intfdata(intf);

	if (dev) {
		dev->state = SMSUSB_DISCONNECTED;

		smsusb_stop_streaming(dev);

		/* unregister from smscore */
		if (dev->coredev)
			smscore_unregister_device(dev->coredev);

		pr_debug("device 0x%p destroyed\n", dev);
		kfree(dev);
	}

	usb_set_intfdata(intf, NULL);
}

static void *siano_media_device_register(struct smsusb_device_t *dev,
					int board_id)
{
#ifdef CONFIG_MEDIA_CONTROLLER_DVB
	struct media_device *mdev;
	struct usb_device *udev = dev->udev;
	struct sms_board *board = sms_get_board(board_id);
	int ret;

	mdev = kzalloc(sizeof(*mdev), GFP_KERNEL);
	if (!mdev)
		return NULL;

	media_device_usb_init(mdev, udev, board->name);

	ret = media_device_register(mdev);
	if (ret) {
		media_device_cleanup(mdev);
		kfree(mdev);
		return NULL;
	}

	pr_info("media controller created\n");

	return mdev;
#else
	return NULL;
#endif
}

static int smsusb_init_device(struct usb_interface *intf, int board_id)
{
	struct smsdevice_params_t params;
	struct smsusb_device_t *dev;
	void *mdev;
	int i, rc;
	int align = 0;

	/* create device object */
	dev = kzalloc(sizeof(struct smsusb_device_t), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	memset(&params, 0, sizeof(params));
	usb_set_intfdata(intf, dev);
	dev->udev = interface_to_usbdev(intf);
	dev->state = SMSUSB_DISCONNECTED;

	for (i = 0; i < intf->cur_altsetting->desc.bNumEndpoints; i++) {
		struct usb_endpoint_descriptor *desc =
				&intf->cur_altsetting->endpoint[i].desc;

		if (desc->bEndpointAddress & USB_DIR_IN) {
			dev->in_ep = desc->bEndpointAddress;
			align = usb_endpoint_maxp(desc) - sizeof(struct sms_msg_hdr);
		} else {
			dev->out_ep = desc->bEndpointAddress;
		}
	}

	pr_debug("in_ep = %02x, out_ep = %02x\n", dev->in_ep, dev->out_ep);
	if (!dev->in_ep || !dev->out_ep || align < 0) {  /* Missing endpoints? */
		smsusb_term_device(intf);
		return -ENODEV;
	}

	params.device_type = sms_get_board(board_id)->type;

	switch (params.device_type) {
	case SMS_STELLAR:
		dev->buffer_size = USB1_BUFFER_SIZE;

		params.setmode_handler = smsusb1_setmode;
		params.detectmode_handler = smsusb1_detectmode;
		break;
	case SMS_UNKNOWN_TYPE:
		pr_err("Unspecified sms device type!\n");
		fallthrough;
	default:
		dev->buffer_size = USB2_BUFFER_SIZE;
		dev->response_alignment = align;

		params.flags |= SMS_DEVICE_FAMILY2;
		break;
	}

	params.device = &dev->udev->dev;
	params.usb_device = dev->udev;
	params.buffer_size = dev->buffer_size;
	params.num_buffers = MAX_BUFFERS;
	params.sendrequest_handler = smsusb_sendrequest;
	params.context = dev;
	usb_make_path(dev->udev, params.devpath, sizeof(params.devpath));

	mdev = siano_media_device_register(dev, board_id);

	/* register in smscore */
	rc = smscore_register_device(&params, &dev->coredev, 0, mdev);
	if (rc < 0) {
		pr_err("smscore_register_device(...) failed, rc %d\n", rc);
		goto err_unregister_device;
	}

	smscore_set_board_id(dev->coredev, board_id);

	dev->coredev->is_usb_device = true;

	/* initialize urbs */
	for (i = 0; i < MAX_URBS; i++) {
		dev->surbs[i].dev = dev;
		usb_init_urb(&dev->surbs[i].urb);
	}

	pr_debug("smsusb_start_streaming(...).\n");
	rc = smsusb_start_streaming(dev);
	if (rc < 0) {
		pr_err("smsusb_start_streaming(...) failed\n");
		goto err_unregister_device;
	}

	dev->state = SMSUSB_ACTIVE;

	rc = smscore_start_device(dev->coredev);
	if (rc < 0) {
		pr_err("smscore_start_device(...) failed\n");
		goto err_unregister_device;
	}

	pr_debug("device 0x%p created\n", dev);

	return rc;

err_unregister_device:
	smsusb_term_device(intf);
#ifdef CONFIG_MEDIA_CONTROLLER_DVB
	media_device_unregister(mdev);
#endif
	kfree(mdev);
	return rc;
}

static int smsusb_probe(struct usb_interface *intf,
			const struct usb_device_id *id)
{
	struct usb_device *udev = interface_to_usbdev(intf);
	char devpath[32];
	int i, rc;

	pr_info("board id=%lu, interface number %d\n",
		 id->driver_info,
		 intf->cur_altsetting->desc.bInterfaceNumber);

	if (sms_get_board(id->driver_info)->intf_num !=
	    intf->cur_altsetting->desc.bInterfaceNumber) {
		pr_debug("interface %d won't be used. Expecting interface %d to popup\n",
			intf->cur_altsetting->desc.bInterfaceNumber,
			sms_get_board(id->driver_info)->intf_num);
		return -ENODEV;
	}

	if (intf->num_altsetting > 1) {
		rc = usb_set_interface(udev,
				       intf->cur_altsetting->desc.bInterfaceNumber,
				       0);
		if (rc < 0) {
			pr_err("usb_set_interface failed, rc %d\n", rc);
			return rc;
		}
	}

	pr_debug("smsusb_probe %d\n",
	       intf->cur_altsetting->desc.bInterfaceNumber);
	for (i = 0; i < intf->cur_altsetting->desc.bNumEndpoints; i++) {
		pr_debug("endpoint %d %02x %02x %d\n", i,
		       intf->cur_altsetting->endpoint[i].desc.bEndpointAddress,
		       intf->cur_altsetting->endpoint[i].desc.bmAttributes,
		       intf->cur_altsetting->endpoint[i].desc.wMaxPacketSize);
		if (intf->cur_altsetting->endpoint[i].desc.bEndpointAddress &
		    USB_DIR_IN)
			rc = usb_clear_halt(udev, usb_rcvbulkpipe(udev,
				intf->cur_altsetting->endpoint[i].desc.bEndpointAddress));
		else
			rc = usb_clear_halt(udev, usb_sndbulkpipe(udev,
				intf->cur_altsetting->endpoint[i].desc.bEndpointAddress));
	}
	if ((udev->actconfig->desc.bNumInterfaces == 2) &&
	    (intf->cur_altsetting->desc.bInterfaceNumber == 0)) {
		pr_debug("rom interface 0 is not used\n");
		return -ENODEV;
	}

	if (id->driver_info == SMS1XXX_BOARD_SIANO_STELLAR_ROM) {
		/* Detected a Siano Stellar uninitialized */

		snprintf(devpath, sizeof(devpath), "usb\\%d-%s",
			 udev->bus->busnum, udev->devpath);
		pr_info("stellar device in cold state was found at %s.\n",
			devpath);
		rc = smsusb1_load_firmware(
				udev, smscore_registry_getmode(devpath),
				id->driver_info);

		/* This device will reset and gain another USB ID */
		if (!rc)
			pr_info("stellar device now in warm state\n");
		else
			pr_err("Failed to put stellar in warm state. Error: %d\n",
			       rc);

		return rc;
	} else {
		rc = smsusb_init_device(intf, id->driver_info);
	}

	pr_info("Device initialized with return code %d\n", rc);
	sms_board_load_modules(id->driver_info);
	return rc;
}

static void smsusb_disconnect(struct usb_interface *intf)
{
	smsusb_term_device(intf);
}

static int smsusb_suspend(struct usb_interface *intf, pm_message_t msg)
{
	struct smsusb_device_t *dev = usb_get_intfdata(intf);
	printk(KERN_INFO "%s  Entering status %d.\n", __func__, msg.event);
	dev->state = SMSUSB_SUSPENDED;
	/*smscore_set_power_mode(dev, SMS_POWER_MODE_SUSPENDED);*/
	smsusb_stop_streaming(dev);
	return 0;
}

static int smsusb_resume(struct usb_interface *intf)
{
	int rc, i;
	struct smsusb_device_t *dev = usb_get_intfdata(intf);
	struct usb_device *udev = interface_to_usbdev(intf);

	printk(KERN_INFO "%s  Entering.\n", __func__);
	usb_clear_halt(udev, usb_rcvbulkpipe(udev, dev->in_ep));
	usb_clear_halt(udev, usb_sndbulkpipe(udev, dev->out_ep));

	for (i = 0; i < intf->cur_altsetting->desc.bNumEndpoints; i++)
		printk(KERN_INFO "endpoint %d %02x %02x %d\n", i,
		       intf->cur_altsetting->endpoint[i].desc.bEndpointAddress,
		       intf->cur_altsetting->endpoint[i].desc.bmAttributes,
		       intf->cur_altsetting->endpoint[i].desc.wMaxPacketSize);

	if (intf->num_altsetting > 0) {
		rc = usb_set_interface(udev,
				       intf->cur_altsetting->desc.
				       bInterfaceNumber, 0);
		if (rc < 0) {
			printk(KERN_INFO "%s usb_set_interface failed, rc %d\n",
			       __func__, rc);
			return rc;
		}
	}

	smsusb_start_streaming(dev);
	return 0;
}

static const struct usb_device_id smsusb_id_table[] = {
	/* This device is only present before firmware load */
	{ USB_DEVICE(0x187f, 0x0010),
		.driver_info = SMS1XXX_BOARD_SIANO_STELLAR_ROM },
	/* This device pops up after firmware load */
	{ USB_DEVICE(0x187f, 0x0100),
		.driver_info = SMS1XXX_BOARD_SIANO_STELLAR },

	{ USB_DEVICE(0x187f, 0x0200),
		.driver_info = SMS1XXX_BOARD_SIANO_NOVA_A },
	{ USB_DEVICE(0x187f, 0x0201),
		.driver_info = SMS1XXX_BOARD_SIANO_NOVA_B },
	{ USB_DEVICE(0x187f, 0x0300),
		.driver_info = SMS1XXX_BOARD_SIANO_VEGA },
	{ USB_DEVICE(0x2040, 0x1700),
		.driver_info = SMS1XXX_BOARD_HAUPPAUGE_CATAMOUNT },
	{ USB_DEVICE(0x2040, 0x1800),
		.driver_info = SMS1XXX_BOARD_HAUPPAUGE_OKEMO_A },
	{ USB_DEVICE(0x2040, 0x1801),
		.driver_info = SMS1XXX_BOARD_HAUPPAUGE_OKEMO_B },
	{ USB_DEVICE(0x2040, 0x2000),
		.driver_info = SMS1XXX_BOARD_HAUPPAUGE_TIGER_MINICARD },
	{ USB_DEVICE(0x2040, 0x2009),
		.driver_info = SMS1XXX_BOARD_HAUPPAUGE_TIGER_MINICARD_R2 },
	{ USB_DEVICE(0x2040, 0x200a),
		.driver_info = SMS1XXX_BOARD_HAUPPAUGE_TIGER_MINICARD },
	{ USB_DEVICE(0x2040, 0x2010),
		.driver_info = SMS1XXX_BOARD_HAUPPAUGE_TIGER_MINICARD },
	{ USB_DEVICE(0x2040, 0x2011),
		.driver_info = SMS1XXX_BOARD_HAUPPAUGE_TIGER_MINICARD },
	{ USB_DEVICE(0x2040, 0x2019),
		.driver_info = SMS1XXX_BOARD_HAUPPAUGE_TIGER_MINICARD },
	{ USB_DEVICE(0x2040, 0x5500),
		.driver_info = SMS1XXX_BOARD_HAUPPAUGE_WINDHAM },
	{ USB_DEVICE(0x2040, 0x5510),
		.driver_info = SMS1XXX_BOARD_HAUPPAUGE_WINDHAM },
	{ USB_DEVICE(0x2040, 0x5520),
		.driver_info = SMS1XXX_BOARD_HAUPPAUGE_WINDHAM },
	{ USB_DEVICE(0x2040, 0x5530),
		.driver_info = SMS1XXX_BOARD_HAUPPAUGE_WINDHAM },
	{ USB_DEVICE(0x2040, 0x5580),
		.driver_info = SMS1XXX_BOARD_HAUPPAUGE_WINDHAM },
	{ USB_DEVICE(0x2040, 0x5590),
		.driver_info = SMS1XXX_BOARD_HAUPPAUGE_WINDHAM },
	{ USB_DEVICE(0x187f, 0x0202),
		.driver_info = SMS1XXX_BOARD_SIANO_NICE },
	{ USB_DEVICE(0x187f, 0x0301),
		.driver_info = SMS1XXX_BOARD_SIANO_VENICE },
	{ USB_DEVICE(0x2040, 0xb900),
		.driver_info = SMS1XXX_BOARD_HAUPPAUGE_WINDHAM },
	{ USB_DEVICE(0x2040, 0xb910),
		.driver_info = SMS1XXX_BOARD_HAUPPAUGE_WINDHAM },
	{ USB_DEVICE(0x2040, 0xb980),
		.driver_info = SMS1XXX_BOARD_HAUPPAUGE_WINDHAM },
	{ USB_DEVICE(0x2040, 0xb990),
		.driver_info = SMS1XXX_BOARD_HAUPPAUGE_WINDHAM },
	{ USB_DEVICE(0x2040, 0xc000),
		.driver_info = SMS1XXX_BOARD_HAUPPAUGE_WINDHAM },
	{ USB_DEVICE(0x2040, 0xc010),
		.driver_info = SMS1XXX_BOARD_HAUPPAUGE_WINDHAM },
	{ USB_DEVICE(0x2040, 0xc080),
		.driver_info = SMS1XXX_BOARD_HAUPPAUGE_WINDHAM },
	{ USB_DEVICE(0x2040, 0xc090),
		.driver_info = SMS1XXX_BOARD_HAUPPAUGE_WINDHAM },
	{ USB_DEVICE(0x2040, 0xc0a0),
		.driver_info = SMS1XXX_BOARD_HAUPPAUGE_WINDHAM },
	{ USB_DEVICE(0x2040, 0xf5a0),
		.driver_info = SMS1XXX_BOARD_HAUPPAUGE_WINDHAM },
	{ USB_DEVICE(0x187f, 0x0202),
		.driver_info = SMS1XXX_BOARD_SIANO_NICE },
	{ USB_DEVICE(0x187f, 0x0301),
		.driver_info = SMS1XXX_BOARD_SIANO_VENICE },
	{ USB_DEVICE(0x187f, 0x0302),
		.driver_info = SMS1XXX_BOARD_SIANO_VENICE },
	{ USB_DEVICE(0x187f, 0x0310),
		.driver_info = SMS1XXX_BOARD_SIANO_MING },
	{ USB_DEVICE(0x187f, 0x0500),
		.driver_info = SMS1XXX_BOARD_SIANO_PELE },
	{ USB_DEVICE(0x187f, 0x0600),
		.driver_info = SMS1XXX_BOARD_SIANO_RIO },
	{ USB_DEVICE(0x187f, 0x0700),
		.driver_info = SMS1XXX_BOARD_SIANO_DENVER_2160 },
	{ USB_DEVICE(0x187f, 0x0800),
		.driver_info = SMS1XXX_BOARD_SIANO_DENVER_1530 },
	{ USB_DEVICE(0x19D2, 0x0086),
		.driver_info = SMS1XXX_BOARD_ZTE_DVB_DATA_CARD },
	{ USB_DEVICE(0x19D2, 0x0078),
		.driver_info = SMS1XXX_BOARD_ONDA_MDTV_DATA_CARD },
	{ USB_DEVICE(0x3275, 0x0080),
		.driver_info = SMS1XXX_BOARD_SIANO_RIO },
	{ USB_DEVICE(0x2013, 0x0257),
		.driver_info = SMS1XXX_BOARD_PCTV_77E },
	{ } /* Terminating entry */
	};

MODULE_DEVICE_TABLE(usb, smsusb_id_table);

static struct usb_driver smsusb_driver = {
	.name			= "smsusb",
	.probe			= smsusb_probe,
	.disconnect		= smsusb_disconnect,
	.id_table		= smsusb_id_table,

	.suspend		= smsusb_suspend,
	.resume			= smsusb_resume,
};

module_usb_driver(smsusb_driver);

MODULE_DESCRIPTION("Driver for the Siano SMS1xxx USB dongle");
MODULE_AUTHOR("Siano Mobile Silicon, INC. (uris@siano-ms.com)");
MODULE_LICENSE("GPL");

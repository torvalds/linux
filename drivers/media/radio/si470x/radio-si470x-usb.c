// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  drivers/media/radio/si470x/radio-si470x-usb.c
 *
 *  USB driver for radios with Silicon Labs Si470x FM Radio Receivers
 *
 *  Copyright (c) 2009 Tobias Lorenz <tobias.lorenz@gmx.net>
 */


/*
 * ToDo:
 * - add firmware download/update support
 */


/* driver definitions */
#define DRIVER_AUTHOR "Tobias Lorenz <tobias.lorenz@gmx.net>"
#define DRIVER_CARD "Silicon Labs Si470x FM Radio"
#define DRIVER_DESC "USB radio driver for Si470x FM Radio Receivers"
#define DRIVER_VERSION "1.0.10"

/* kernel includes */
#include <linux/usb.h>
#include <linux/hid.h>
#include <linux/slab.h>

#include "radio-si470x.h"


/* USB Device ID List */
static const struct usb_device_id si470x_usb_driver_id_table[] = {
	/* Silicon Labs USB FM Radio Reference Design */
	{ USB_DEVICE_AND_INTERFACE_INFO(0x10c4, 0x818a, USB_CLASS_HID, 0, 0) },
	/* ADS/Tech FM Radio Receiver (formerly Instant FM Music) */
	{ USB_DEVICE_AND_INTERFACE_INFO(0x06e1, 0xa155, USB_CLASS_HID, 0, 0) },
	/* KWorld USB FM Radio SnapMusic Mobile 700 (FM700) */
	{ USB_DEVICE_AND_INTERFACE_INFO(0x1b80, 0xd700, USB_CLASS_HID, 0, 0) },
	/* Sanei Electric, Inc. FM USB Radio (sold as DealExtreme.com PCear) */
	{ USB_DEVICE_AND_INTERFACE_INFO(0x10c5, 0x819a, USB_CLASS_HID, 0, 0) },
	/* Axentia ALERT FM USB Receiver */
	{ USB_DEVICE_AND_INTERFACE_INFO(0x12cf, 0x7111, USB_CLASS_HID, 0, 0) },
	/* Terminating entry */
	{ }
};
MODULE_DEVICE_TABLE(usb, si470x_usb_driver_id_table);



/**************************************************************************
 * Module Parameters
 **************************************************************************/

/* Radio Nr */
static int radio_nr = -1;
module_param(radio_nr, int, 0444);
MODULE_PARM_DESC(radio_nr, "Radio Nr");

/* USB timeout */
static unsigned int usb_timeout = 500;
module_param(usb_timeout, uint, 0644);
MODULE_PARM_DESC(usb_timeout, "USB timeout (ms): *500*");

/* RDS buffer blocks */
static unsigned int rds_buf = 100;
module_param(rds_buf, uint, 0444);
MODULE_PARM_DESC(rds_buf, "RDS buffer entries: *100*");

/* RDS maximum block errors */
static unsigned short max_rds_errors = 1;
/* 0 means   0  errors requiring correction */
/* 1 means 1-2  errors requiring correction (used by original USBRadio.exe) */
/* 2 means 3-5  errors requiring correction */
/* 3 means   6+ errors or errors in checkword, correction not possible */
module_param(max_rds_errors, ushort, 0644);
MODULE_PARM_DESC(max_rds_errors, "RDS maximum block errors: *1*");



/**************************************************************************
 * USB HID Reports
 **************************************************************************/

/* Reports 1-16 give direct read/write access to the 16 Si470x registers */
/* with the (REPORT_ID - 1) corresponding to the register address across USB */
/* endpoint 0 using GET_REPORT and SET_REPORT */
#define REGISTER_REPORT_SIZE	(RADIO_REGISTER_SIZE + 1)
#define REGISTER_REPORT(reg)	((reg) + 1)

/* Report 17 gives direct read/write access to the entire Si470x register */
/* map across endpoint 0 using GET_REPORT and SET_REPORT */
#define ENTIRE_REPORT_SIZE	(RADIO_REGISTER_NUM * RADIO_REGISTER_SIZE + 1)
#define ENTIRE_REPORT		17

/* Report 18 is used to send the lowest 6 Si470x registers up the HID */
/* interrupt endpoint 1 to Windows every 20 milliseconds for status */
#define RDS_REPORT_SIZE		(RDS_REGISTER_NUM * RADIO_REGISTER_SIZE + 1)
#define RDS_REPORT		18

/* Report 19: LED state */
#define LED_REPORT_SIZE		3
#define LED_REPORT		19

/* Report 19: stream */
#define STREAM_REPORT_SIZE	3
#define STREAM_REPORT		19

/* Report 20: scratch */
#define SCRATCH_PAGE_SIZE	63
#define SCRATCH_REPORT_SIZE	(SCRATCH_PAGE_SIZE + 1)
#define SCRATCH_REPORT		20

/* Reports 19-22: flash upgrade of the C8051F321 */
#define WRITE_REPORT_SIZE	4
#define WRITE_REPORT		19
#define FLASH_REPORT_SIZE	64
#define FLASH_REPORT		20
#define CRC_REPORT_SIZE		3
#define CRC_REPORT		21
#define RESPONSE_REPORT_SIZE	2
#define RESPONSE_REPORT		22

/* Report 23: currently unused, but can accept 60 byte reports on the HID */
/* interrupt out endpoint 2 every 1 millisecond */
#define UNUSED_REPORT		23

#define MAX_REPORT_SIZE		64



/**************************************************************************
 * Software/Hardware Versions from Scratch Page
 **************************************************************************/
#define RADIO_HW_VERSION			1



/**************************************************************************
 * LED State Definitions
 **************************************************************************/
#define LED_COMMAND		0x35

#define NO_CHANGE_LED		0x00
#define ALL_COLOR_LED		0x01	/* streaming state */
#define BLINK_GREEN_LED		0x02	/* connect state */
#define BLINK_RED_LED		0x04
#define BLINK_ORANGE_LED	0x10	/* disconnect state */
#define SOLID_GREEN_LED		0x20	/* tuning/seeking state */
#define SOLID_RED_LED		0x40	/* bootload state */
#define SOLID_ORANGE_LED	0x80



/**************************************************************************
 * Stream State Definitions
 **************************************************************************/
#define STREAM_COMMAND	0x36
#define STREAM_VIDPID	0x00
#define STREAM_AUDIO	0xff



/**************************************************************************
 * Bootloader / Flash Commands
 **************************************************************************/

/* unique id sent to bootloader and required to put into a bootload state */
#define UNIQUE_BL_ID		0x34

/* mask for the flash data */
#define FLASH_DATA_MASK		0x55

/* bootloader commands */
#define GET_SW_VERSION_COMMAND	0x00
#define SET_PAGE_COMMAND	0x01
#define ERASE_PAGE_COMMAND	0x02
#define WRITE_PAGE_COMMAND	0x03
#define CRC_ON_PAGE_COMMAND	0x04
#define READ_FLASH_BYTE_COMMAND	0x05
#define RESET_DEVICE_COMMAND	0x06
#define GET_HW_VERSION_COMMAND	0x07
#define BLANK			0xff

/* bootloader command responses */
#define COMMAND_OK		0x01
#define COMMAND_FAILED		0x02
#define COMMAND_PENDING		0x03



/**************************************************************************
 * General Driver Functions - REGISTER_REPORTs
 **************************************************************************/

/*
 * si470x_get_report - receive a HID report
 */
static int si470x_get_report(struct si470x_device *radio, void *buf, int size)
{
	unsigned char *report = buf;
	int retval;

	retval = usb_control_msg(radio->usbdev,
		usb_rcvctrlpipe(radio->usbdev, 0),
		HID_REQ_GET_REPORT,
		USB_TYPE_CLASS | USB_RECIP_INTERFACE | USB_DIR_IN,
		report[0], 2,
		buf, size, usb_timeout);

	if (retval < 0)
		dev_warn(&radio->intf->dev,
			"si470x_get_report: usb_control_msg returned %d\n",
			retval);
	return retval;
}


/*
 * si470x_set_report - send a HID report
 */
static int si470x_set_report(struct si470x_device *radio, void *buf, int size)
{
	unsigned char *report = buf;
	int retval;

	retval = usb_control_msg(radio->usbdev,
		usb_sndctrlpipe(radio->usbdev, 0),
		HID_REQ_SET_REPORT,
		USB_TYPE_CLASS | USB_RECIP_INTERFACE | USB_DIR_OUT,
		report[0], 2,
		buf, size, usb_timeout);

	if (retval < 0)
		dev_warn(&radio->intf->dev,
			"si470x_set_report: usb_control_msg returned %d\n",
			retval);
	return retval;
}


/*
 * si470x_get_register - read register
 */
static int si470x_get_register(struct si470x_device *radio, int regnr)
{
	int retval;

	radio->usb_buf[0] = REGISTER_REPORT(regnr);

	retval = si470x_get_report(radio, radio->usb_buf, REGISTER_REPORT_SIZE);

	if (retval >= 0)
		radio->registers[regnr] = get_unaligned_be16(&radio->usb_buf[1]);

	return (retval < 0) ? -EINVAL : 0;
}


/*
 * si470x_set_register - write register
 */
static int si470x_set_register(struct si470x_device *radio, int regnr)
{
	int retval;

	radio->usb_buf[0] = REGISTER_REPORT(regnr);
	put_unaligned_be16(radio->registers[regnr], &radio->usb_buf[1]);

	retval = si470x_set_report(radio, radio->usb_buf, REGISTER_REPORT_SIZE);

	return (retval < 0) ? -EINVAL : 0;
}



/**************************************************************************
 * General Driver Functions - ENTIRE_REPORT
 **************************************************************************/

/*
 * si470x_get_all_registers - read entire registers
 */
static int si470x_get_all_registers(struct si470x_device *radio)
{
	int retval;
	unsigned char regnr;

	radio->usb_buf[0] = ENTIRE_REPORT;

	retval = si470x_get_report(radio, radio->usb_buf, ENTIRE_REPORT_SIZE);

	if (retval >= 0)
		for (regnr = 0; regnr < RADIO_REGISTER_NUM; regnr++)
			radio->registers[regnr] = get_unaligned_be16(
				&radio->usb_buf[regnr * RADIO_REGISTER_SIZE + 1]);

	return (retval < 0) ? -EINVAL : 0;
}



/**************************************************************************
 * General Driver Functions - LED_REPORT
 **************************************************************************/

/*
 * si470x_set_led_state - sets the led state
 */
static int si470x_set_led_state(struct si470x_device *radio,
		unsigned char led_state)
{
	int retval;

	radio->usb_buf[0] = LED_REPORT;
	radio->usb_buf[1] = LED_COMMAND;
	radio->usb_buf[2] = led_state;

	retval = si470x_set_report(radio, radio->usb_buf, LED_REPORT_SIZE);

	return (retval < 0) ? -EINVAL : 0;
}



/**************************************************************************
 * General Driver Functions - SCRATCH_REPORT
 **************************************************************************/

/*
 * si470x_get_scratch_versions - gets the scratch page and version infos
 */
static int si470x_get_scratch_page_versions(struct si470x_device *radio)
{
	int retval;

	radio->usb_buf[0] = SCRATCH_REPORT;

	retval = si470x_get_report(radio, radio->usb_buf, SCRATCH_REPORT_SIZE);

	if (retval < 0)
		dev_warn(&radio->intf->dev, "si470x_get_scratch: si470x_get_report returned %d\n",
			 retval);
	else {
		radio->software_version = radio->usb_buf[1];
		radio->hardware_version = radio->usb_buf[2];
	}

	return (retval < 0) ? -EINVAL : 0;
}



/**************************************************************************
 * RDS Driver Functions
 **************************************************************************/

/*
 * si470x_int_in_callback - rds callback and processing function
 *
 * TODO: do we need to use mutex locks in some sections?
 */
static void si470x_int_in_callback(struct urb *urb)
{
	struct si470x_device *radio = urb->context;
	int retval;
	unsigned char regnr;
	unsigned char blocknum;
	unsigned short bler; /* rds block errors */
	unsigned short rds;
	unsigned char tmpbuf[3];

	if (urb->status) {
		if (urb->status == -ENOENT ||
				urb->status == -ECONNRESET ||
				urb->status == -ESHUTDOWN) {
			return;
		} else {
			dev_warn(&radio->intf->dev,
			 "non-zero urb status (%d)\n", urb->status);
			goto resubmit; /* Maybe we can recover. */
		}
	}

	/* Sometimes the device returns len 0 packets */
	if (urb->actual_length != RDS_REPORT_SIZE)
		goto resubmit;

	radio->registers[STATUSRSSI] =
		get_unaligned_be16(&radio->int_in_buffer[1]);

	if (radio->registers[STATUSRSSI] & STATUSRSSI_STC)
		complete(&radio->completion);

	if ((radio->registers[SYSCONFIG1] & SYSCONFIG1_RDS)) {
		/* Update RDS registers with URB data */
		for (regnr = 1; regnr < RDS_REGISTER_NUM; regnr++)
			radio->registers[STATUSRSSI + regnr] =
			    get_unaligned_be16(&radio->int_in_buffer[
				regnr * RADIO_REGISTER_SIZE + 1]);
		/* get rds blocks */
		if ((radio->registers[STATUSRSSI] & STATUSRSSI_RDSR) == 0) {
			/* No RDS group ready, better luck next time */
			goto resubmit;
		}
		if ((radio->registers[STATUSRSSI] & STATUSRSSI_RDSS) == 0) {
			/* RDS decoder not synchronized */
			goto resubmit;
		}
		for (blocknum = 0; blocknum < 4; blocknum++) {
			switch (blocknum) {
			default:
				bler = (radio->registers[STATUSRSSI] &
						STATUSRSSI_BLERA) >> 9;
				rds = radio->registers[RDSA];
				break;
			case 1:
				bler = (radio->registers[READCHAN] &
						READCHAN_BLERB) >> 14;
				rds = radio->registers[RDSB];
				break;
			case 2:
				bler = (radio->registers[READCHAN] &
						READCHAN_BLERC) >> 12;
				rds = radio->registers[RDSC];
				break;
			case 3:
				bler = (radio->registers[READCHAN] &
						READCHAN_BLERD) >> 10;
				rds = radio->registers[RDSD];
				break;
			}

			/* Fill the V4L2 RDS buffer */
			put_unaligned_le16(rds, &tmpbuf);
			tmpbuf[2] = blocknum;		/* offset name */
			tmpbuf[2] |= blocknum << 3;	/* received offset */
			if (bler > max_rds_errors)
				tmpbuf[2] |= 0x80; /* uncorrectable errors */
			else if (bler > 0)
				tmpbuf[2] |= 0x40; /* corrected error(s) */

			/* copy RDS block to internal buffer */
			memcpy(&radio->buffer[radio->wr_index], &tmpbuf, 3);
			radio->wr_index += 3;

			/* wrap write pointer */
			if (radio->wr_index >= radio->buf_size)
				radio->wr_index = 0;

			/* check for overflow */
			if (radio->wr_index == radio->rd_index) {
				/* increment and wrap read pointer */
				radio->rd_index += 3;
				if (radio->rd_index >= radio->buf_size)
					radio->rd_index = 0;
			}
		}
		if (radio->wr_index != radio->rd_index)
			wake_up_interruptible(&radio->read_queue);
	}

resubmit:
	/* Resubmit if we're still running. */
	if (radio->int_in_running && radio->usbdev) {
		retval = usb_submit_urb(radio->int_in_urb, GFP_ATOMIC);
		if (retval) {
			dev_warn(&radio->intf->dev,
			       "resubmitting urb failed (%d)", retval);
			radio->int_in_running = 0;
		}
	}
	radio->status_rssi_auto_update = radio->int_in_running;
}


static int si470x_fops_open(struct file *file)
{
	return v4l2_fh_open(file);
}

static int si470x_fops_release(struct file *file)
{
	return v4l2_fh_release(file);
}

static void si470x_usb_release(struct v4l2_device *v4l2_dev)
{
	struct si470x_device *radio =
		container_of(v4l2_dev, struct si470x_device, v4l2_dev);

	usb_free_urb(radio->int_in_urb);
	v4l2_ctrl_handler_free(&radio->hdl);
	v4l2_device_unregister(&radio->v4l2_dev);
	kfree(radio->int_in_buffer);
	kfree(radio->buffer);
	kfree(radio->usb_buf);
	kfree(radio);
}


/**************************************************************************
 * Video4Linux Interface
 **************************************************************************/

/*
 * si470x_vidioc_querycap - query device capabilities
 */
static int si470x_vidioc_querycap(struct file *file, void *priv,
				  struct v4l2_capability *capability)
{
	struct si470x_device *radio = video_drvdata(file);

	strscpy(capability->driver, DRIVER_NAME, sizeof(capability->driver));
	strscpy(capability->card, DRIVER_CARD, sizeof(capability->card));
	usb_make_path(radio->usbdev, capability->bus_info,
			sizeof(capability->bus_info));
	return 0;
}


static int si470x_start_usb(struct si470x_device *radio)
{
	int retval;

	/* initialize interrupt urb */
	usb_fill_int_urb(radio->int_in_urb, radio->usbdev,
			usb_rcvintpipe(radio->usbdev,
				radio->int_in_endpoint->bEndpointAddress),
			radio->int_in_buffer,
			le16_to_cpu(radio->int_in_endpoint->wMaxPacketSize),
			si470x_int_in_callback,
			radio,
			radio->int_in_endpoint->bInterval);

	radio->int_in_running = 1;
	mb();

	retval = usb_submit_urb(radio->int_in_urb, GFP_KERNEL);
	if (retval) {
		dev_info(&radio->intf->dev,
				"submitting int urb failed (%d)\n", retval);
		radio->int_in_running = 0;
	}
	radio->status_rssi_auto_update = radio->int_in_running;

	/* start radio */
	retval = si470x_start(radio);
	if (retval < 0)
		return retval;

	v4l2_ctrl_handler_setup(&radio->hdl);

	return retval;
}

/**************************************************************************
 * USB Interface
 **************************************************************************/

/*
 * si470x_usb_driver_probe - probe for the device
 */
static int si470x_usb_driver_probe(struct usb_interface *intf,
		const struct usb_device_id *id)
{
	struct si470x_device *radio;
	struct usb_host_interface *iface_desc;
	struct usb_endpoint_descriptor *endpoint;
	int i, int_end_size, retval;
	unsigned char version_warning = 0;

	/* private data allocation and initialization */
	radio = kzalloc(sizeof(struct si470x_device), GFP_KERNEL);
	if (!radio) {
		retval = -ENOMEM;
		goto err_initial;
	}
	radio->usb_buf = kmalloc(MAX_REPORT_SIZE, GFP_KERNEL);
	if (radio->usb_buf == NULL) {
		retval = -ENOMEM;
		goto err_radio;
	}
	radio->usbdev = interface_to_usbdev(intf);
	radio->intf = intf;
	radio->band = 1; /* Default to 76 - 108 MHz */
	mutex_init(&radio->lock);
	init_completion(&radio->completion);

	radio->get_register = si470x_get_register;
	radio->set_register = si470x_set_register;
	radio->fops_open = si470x_fops_open;
	radio->fops_release = si470x_fops_release;
	radio->vidioc_querycap = si470x_vidioc_querycap;

	iface_desc = intf->cur_altsetting;

	/* Set up interrupt endpoint information. */
	for (i = 0; i < iface_desc->desc.bNumEndpoints; ++i) {
		endpoint = &iface_desc->endpoint[i].desc;
		if (usb_endpoint_is_int_in(endpoint))
			radio->int_in_endpoint = endpoint;
	}
	if (!radio->int_in_endpoint) {
		dev_info(&intf->dev, "could not find interrupt in endpoint\n");
		retval = -EIO;
		goto err_usbbuf;
	}

	int_end_size = le16_to_cpu(radio->int_in_endpoint->wMaxPacketSize);

	radio->int_in_buffer = kmalloc(int_end_size, GFP_KERNEL);
	if (!radio->int_in_buffer) {
		dev_info(&intf->dev, "could not allocate int_in_buffer");
		retval = -ENOMEM;
		goto err_usbbuf;
	}

	radio->int_in_urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!radio->int_in_urb) {
		retval = -ENOMEM;
		goto err_intbuffer;
	}

	radio->v4l2_dev.release = si470x_usb_release;

	/*
	 * The si470x SiLabs reference design uses the same USB IDs as
	 * 'Thanko's Raremono' si4734 based receiver. So check here which we
	 * have: attempt to read the device ID from the si470x: the lower 12
	 * bits should be 0x0242 for the si470x.
	 *
	 * We use this check to determine which device we are dealing with.
	 */
	if (id->idVendor == 0x10c4 && id->idProduct == 0x818a) {
		retval = usb_control_msg(radio->usbdev,
				usb_rcvctrlpipe(radio->usbdev, 0),
				HID_REQ_GET_REPORT,
				USB_TYPE_CLASS | USB_RECIP_INTERFACE | USB_DIR_IN,
				1, 2,
				radio->usb_buf, 3, 500);
		if (retval != 3 ||
		    (get_unaligned_be16(&radio->usb_buf[1]) & 0xfff) != 0x0242) {
			dev_info(&intf->dev, "this is not a si470x device.\n");
			retval = -ENODEV;
			goto err_urb;
		}
	}

	retval = v4l2_device_register(&intf->dev, &radio->v4l2_dev);
	if (retval < 0) {
		dev_err(&intf->dev, "couldn't register v4l2_device\n");
		goto err_urb;
	}

	v4l2_ctrl_handler_init(&radio->hdl, 2);
	v4l2_ctrl_new_std(&radio->hdl, &si470x_ctrl_ops,
			  V4L2_CID_AUDIO_MUTE, 0, 1, 1, 1);
	v4l2_ctrl_new_std(&radio->hdl, &si470x_ctrl_ops,
			  V4L2_CID_AUDIO_VOLUME, 0, 15, 1, 15);
	if (radio->hdl.error) {
		retval = radio->hdl.error;
		dev_err(&intf->dev, "couldn't register control\n");
		goto err_dev;
	}
	radio->videodev = si470x_viddev_template;
	radio->videodev.ctrl_handler = &radio->hdl;
	radio->videodev.lock = &radio->lock;
	radio->videodev.v4l2_dev = &radio->v4l2_dev;
	radio->videodev.release = video_device_release_empty;
	radio->videodev.device_caps =
		V4L2_CAP_HW_FREQ_SEEK | V4L2_CAP_READWRITE | V4L2_CAP_TUNER |
		V4L2_CAP_RADIO | V4L2_CAP_RDS_CAPTURE;
	video_set_drvdata(&radio->videodev, radio);

	/* get device and chip versions */
	if (si470x_get_all_registers(radio) < 0) {
		retval = -EIO;
		goto err_ctrl;
	}
	dev_info(&intf->dev, "DeviceID=0x%4.4hx ChipID=0x%4.4hx\n",
			radio->registers[DEVICEID], radio->registers[SI_CHIPID]);
	if ((radio->registers[SI_CHIPID] & SI_CHIPID_FIRMWARE) < RADIO_FW_VERSION) {
		dev_warn(&intf->dev,
			"This driver is known to work with firmware version %hu,\n",
			RADIO_FW_VERSION);
		dev_warn(&intf->dev,
			"but the device has firmware version %hu.\n",
			radio->registers[SI_CHIPID] & SI_CHIPID_FIRMWARE);
		version_warning = 1;
	}

	/* get software and hardware versions */
	if (si470x_get_scratch_page_versions(radio) < 0) {
		retval = -EIO;
		goto err_ctrl;
	}
	dev_info(&intf->dev, "software version %d, hardware version %d\n",
			radio->software_version, radio->hardware_version);
	if (radio->hardware_version < RADIO_HW_VERSION) {
		dev_warn(&intf->dev,
			"This driver is known to work with hardware version %hu,\n",
			RADIO_HW_VERSION);
		dev_warn(&intf->dev,
			"but the device has hardware version %hu.\n",
			radio->hardware_version);
		version_warning = 1;
	}

	/* give out version warning */
	if (version_warning == 1) {
		dev_warn(&intf->dev,
			"If you have some trouble using this driver,\n");
		dev_warn(&intf->dev,
			"please report to V4L ML at linux-media@vger.kernel.org\n");
	}

	/* set led to connect state */
	si470x_set_led_state(radio, BLINK_GREEN_LED);

	/* rds buffer allocation */
	radio->buf_size = rds_buf * 3;
	radio->buffer = kmalloc(radio->buf_size, GFP_KERNEL);
	if (!radio->buffer) {
		retval = -EIO;
		goto err_ctrl;
	}

	/* rds buffer configuration */
	radio->wr_index = 0;
	radio->rd_index = 0;
	init_waitqueue_head(&radio->read_queue);
	usb_set_intfdata(intf, radio);

	/* start radio */
	retval = si470x_start_usb(radio);
	if (retval < 0 && !radio->int_in_running)
		goto err_buf;
	else if (retval < 0)	/* in case of radio->int_in_running == 1 */
		goto err_all;

	/* set initial frequency */
	si470x_set_freq(radio, 87.5 * FREQ_MUL); /* available in all regions */

	/* register video device */
	retval = video_register_device(&radio->videodev, VFL_TYPE_RADIO,
			radio_nr);
	if (retval) {
		dev_err(&intf->dev, "Could not register video device\n");
		goto err_all;
	}

	return 0;
err_all:
	usb_kill_urb(radio->int_in_urb);
err_buf:
	kfree(radio->buffer);
err_ctrl:
	v4l2_ctrl_handler_free(&radio->hdl);
err_dev:
	v4l2_device_unregister(&radio->v4l2_dev);
err_urb:
	usb_free_urb(radio->int_in_urb);
err_intbuffer:
	kfree(radio->int_in_buffer);
err_usbbuf:
	kfree(radio->usb_buf);
err_radio:
	kfree(radio);
err_initial:
	return retval;
}


/*
 * si470x_usb_driver_suspend - suspend the device
 */
static int si470x_usb_driver_suspend(struct usb_interface *intf,
		pm_message_t message)
{
	struct si470x_device *radio = usb_get_intfdata(intf);

	dev_info(&intf->dev, "suspending now...\n");

	/* shutdown interrupt handler */
	if (radio->int_in_running) {
		radio->int_in_running = 0;
		if (radio->int_in_urb)
			usb_kill_urb(radio->int_in_urb);
	}

	/* cancel read processes */
	wake_up_interruptible(&radio->read_queue);

	/* stop radio */
	si470x_stop(radio);
	return 0;
}


/*
 * si470x_usb_driver_resume - resume the device
 */
static int si470x_usb_driver_resume(struct usb_interface *intf)
{
	struct si470x_device *radio = usb_get_intfdata(intf);
	int ret;

	dev_info(&intf->dev, "resuming now...\n");

	/* start radio */
	ret = si470x_start_usb(radio);
	if (ret == 0)
		v4l2_ctrl_handler_setup(&radio->hdl);

	return ret;
}


/*
 * si470x_usb_driver_disconnect - disconnect the device
 */
static void si470x_usb_driver_disconnect(struct usb_interface *intf)
{
	struct si470x_device *radio = usb_get_intfdata(intf);

	mutex_lock(&radio->lock);
	v4l2_device_disconnect(&radio->v4l2_dev);
	video_unregister_device(&radio->videodev);
	usb_kill_urb(radio->int_in_urb);
	usb_set_intfdata(intf, NULL);
	mutex_unlock(&radio->lock);
	v4l2_device_put(&radio->v4l2_dev);
}


/*
 * si470x_usb_driver - usb driver interface
 *
 * A note on suspend/resume: this driver had only empty suspend/resume
 * functions, and when I tried to test suspend/resume it always disconnected
 * instead of resuming (using my ADS InstantFM stick). So I've decided to
 * remove these callbacks until someone else with better hardware can
 * implement and test this.
 */
static struct usb_driver si470x_usb_driver = {
	.name			= DRIVER_NAME,
	.probe			= si470x_usb_driver_probe,
	.disconnect		= si470x_usb_driver_disconnect,
	.suspend		= si470x_usb_driver_suspend,
	.resume			= si470x_usb_driver_resume,
	.reset_resume		= si470x_usb_driver_resume,
	.id_table		= si470x_usb_driver_id_table,
};

module_usb_driver(si470x_usb_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_VERSION(DRIVER_VERSION);

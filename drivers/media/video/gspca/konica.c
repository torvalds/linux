/*
 * Driver for USB webcams based on Konica chipset. This
 * chipset is used in Intel YC76 camera.
 *
 * Copyright (C) 2010 Hans de Goede <hdegoede@redhat.com>
 *
 * Based on the usbvideo v4l1 konicawc driver which is:
 *
 * Copyright (C) 2002 Simon Evans <spse@secret.org.uk>
 *
 * The code for making gspca work with a webcam with 2 isoc endpoints was
 * taken from the benq gspca subdriver which is:
 *
 * Copyright (C) 2009 Jean-Francois Moine (http://moinejf.free.fr)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#define MODULE_NAME "konica"

#include <linux/input.h>
#include "gspca.h"

MODULE_AUTHOR("Hans de Goede <hdegoede@redhat.com>");
MODULE_DESCRIPTION("Konica chipset USB Camera Driver");
MODULE_LICENSE("GPL");

#define WHITEBAL_REG   0x01
#define BRIGHTNESS_REG 0x02
#define SHARPNESS_REG  0x03
#define CONTRAST_REG   0x04
#define SATURATION_REG 0x05

/* specific webcam descriptor */
struct sd {
	struct gspca_dev gspca_dev;	/* !! must be the first item */
	struct urb *last_data_urb;
	u8 snapshot_pressed;
	u8 brightness;
	u8 contrast;
	u8 saturation;
	u8 whitebal;
	u8 sharpness;
};

/* V4L2 controls supported by the driver */
static int sd_setbrightness(struct gspca_dev *gspca_dev, __s32 val);
static int sd_getbrightness(struct gspca_dev *gspca_dev, __s32 *val);
static int sd_setcontrast(struct gspca_dev *gspca_dev, __s32 val);
static int sd_getcontrast(struct gspca_dev *gspca_dev, __s32 *val);
static int sd_setsaturation(struct gspca_dev *gspca_dev, __s32 val);
static int sd_getsaturation(struct gspca_dev *gspca_dev, __s32 *val);
static int sd_setwhitebal(struct gspca_dev *gspca_dev, __s32 val);
static int sd_getwhitebal(struct gspca_dev *gspca_dev, __s32 *val);
static int sd_setsharpness(struct gspca_dev *gspca_dev, __s32 val);
static int sd_getsharpness(struct gspca_dev *gspca_dev, __s32 *val);

static const struct ctrl sd_ctrls[] = {
#define SD_BRIGHTNESS 0
	{
	    {
		.id      = V4L2_CID_BRIGHTNESS,
		.type    = V4L2_CTRL_TYPE_INTEGER,
		.name    = "Brightness",
		.minimum = 0,
		.maximum = 9,
		.step = 1,
#define BRIGHTNESS_DEFAULT 4
		.default_value = BRIGHTNESS_DEFAULT,
		.flags = 0,
	    },
	    .set = sd_setbrightness,
	    .get = sd_getbrightness,
	},
#define SD_CONTRAST 1
	{
	    {
		.id = V4L2_CID_CONTRAST,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "Contrast",
		.minimum = 0,
		.maximum = 9,
		.step = 4,
#define CONTRAST_DEFAULT 10
		.default_value = CONTRAST_DEFAULT,
		.flags = 0,
	    },
	    .set = sd_setcontrast,
	    .get = sd_getcontrast,
	},
#define SD_SATURATION 2
	{
	    {
		.id	= V4L2_CID_SATURATION,
		.type	= V4L2_CTRL_TYPE_INTEGER,
		.name	= "Saturation",
		.minimum = 0,
		.maximum = 9,
		.step	= 1,
#define SATURATION_DEFAULT 4
		.default_value = SATURATION_DEFAULT,
		.flags = 0,
	    },
	    .set = sd_setsaturation,
	    .get = sd_getsaturation,
	},
#define SD_WHITEBAL 3
	{
	    {
		.id = V4L2_CID_WHITE_BALANCE_TEMPERATURE,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "White Balance",
		.minimum = 0,
		.maximum = 33,
		.step = 1,
#define WHITEBAL_DEFAULT 25
		.default_value = WHITEBAL_DEFAULT,
		.flags = 0,
	    },
	    .set = sd_setwhitebal,
	    .get = sd_getwhitebal,
	},
#define SD_SHARPNESS 4
	{
	    {
		.id = V4L2_CID_SHARPNESS,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "Sharpness",
		.minimum = 0,
		.maximum = 9,
		.step = 1,
#define SHARPNESS_DEFAULT 4
		.default_value = SHARPNESS_DEFAULT,
		.flags = 0,
	    },
	    .set = sd_setsharpness,
	    .get = sd_getsharpness,
	},
};

/* .priv is what goes to register 8 for this mode, known working values:
   0x00 -> 176x144, cropped
   0x01 -> 176x144, cropped
   0x02 -> 176x144, cropped
   0x03 -> 176x144, cropped
   0x04 -> 176x144, binned
   0x05 -> 320x240
   0x06 -> 320x240
   0x07 -> 160x120, cropped
   0x08 -> 160x120, cropped
   0x09 -> 160x120, binned (note has 136 lines)
   0x0a -> 160x120, binned (note has 136 lines)
   0x0b -> 160x120, cropped
*/
static const struct v4l2_pix_format vga_mode[] = {
	{160, 120, V4L2_PIX_FMT_KONICA420, V4L2_FIELD_NONE,
		.bytesperline = 160,
		.sizeimage = 160 * 136 * 3 / 2 + 960,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.priv = 0x0a},
	{176, 144, V4L2_PIX_FMT_KONICA420, V4L2_FIELD_NONE,
		.bytesperline = 176,
		.sizeimage = 176 * 144 * 3 / 2 + 960,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.priv = 0x04},
	{320, 240, V4L2_PIX_FMT_KONICA420, V4L2_FIELD_NONE,
		.bytesperline = 320,
		.sizeimage = 320 * 240 * 3 / 2 + 960,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.priv = 0x05},
};

static void sd_isoc_irq(struct urb *urb);

static void reg_w(struct gspca_dev *gspca_dev, u16 value, u16 index)
{
	struct usb_device *dev = gspca_dev->dev;
	int ret;

	if (gspca_dev->usb_err < 0)
		return;
	ret = usb_control_msg(dev, usb_sndctrlpipe(dev, 0),
			0x02,
			USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			value,
			index,
			NULL,
			0,
			1000);
	if (ret < 0) {
		pr_err("reg_w err %d\n", ret);
		gspca_dev->usb_err = ret;
	}
}

static void reg_r(struct gspca_dev *gspca_dev, u16 value, u16 index)
{
	struct usb_device *dev = gspca_dev->dev;
	int ret;

	if (gspca_dev->usb_err < 0)
		return;
	ret = usb_control_msg(dev, usb_rcvctrlpipe(dev, 0),
			0x03,
			USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			value,
			index,
			gspca_dev->usb_buf,
			2,
			1000);
	if (ret < 0) {
		pr_err("reg_w err %d\n", ret);
		gspca_dev->usb_err = ret;
	}
}

static void konica_stream_on(struct gspca_dev *gspca_dev)
{
	reg_w(gspca_dev, 1, 0x0b);
}

static void konica_stream_off(struct gspca_dev *gspca_dev)
{
	reg_w(gspca_dev, 0, 0x0b);
}

/* this function is called at probe time */
static int sd_config(struct gspca_dev *gspca_dev,
			const struct usb_device_id *id)
{
	struct sd *sd = (struct sd *) gspca_dev;

	gspca_dev->cam.cam_mode = vga_mode;
	gspca_dev->cam.nmodes = ARRAY_SIZE(vga_mode);
	gspca_dev->cam.no_urb_create = 1;
	/* The highest alt setting has an isoc packetsize of 0, so we
	   don't want to use it */
	gspca_dev->nbalt--;

	sd->brightness  = BRIGHTNESS_DEFAULT;
	sd->contrast    = CONTRAST_DEFAULT;
	sd->saturation  = SATURATION_DEFAULT;
	sd->whitebal    = WHITEBAL_DEFAULT;
	sd->sharpness   = SHARPNESS_DEFAULT;

	return 0;
}

/* this function is called at probe and resume time */
static int sd_init(struct gspca_dev *gspca_dev)
{
	/* HDG not sure if these 2 reads are needed */
	reg_r(gspca_dev, 0, 0x10);
	PDEBUG(D_PROBE, "Reg 0x10 reads: %02x %02x",
	       gspca_dev->usb_buf[0], gspca_dev->usb_buf[1]);
	reg_r(gspca_dev, 0, 0x10);
	PDEBUG(D_PROBE, "Reg 0x10 reads: %02x %02x",
	       gspca_dev->usb_buf[0], gspca_dev->usb_buf[1]);
	reg_w(gspca_dev, 0, 0x0d);

	return 0;
}

static int sd_start(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	struct urb *urb;
	int i, n, packet_size;
	struct usb_host_interface *alt;
	struct usb_interface *intf;

	intf = usb_ifnum_to_if(sd->gspca_dev.dev, sd->gspca_dev.iface);
	alt = usb_altnum_to_altsetting(intf, sd->gspca_dev.alt);
	if (!alt) {
		pr_err("Couldn't get altsetting\n");
		return -EIO;
	}

	packet_size = le16_to_cpu(alt->endpoint[0].desc.wMaxPacketSize);

	reg_w(gspca_dev, sd->brightness, BRIGHTNESS_REG);
	reg_w(gspca_dev, sd->whitebal, WHITEBAL_REG);
	reg_w(gspca_dev, sd->contrast, CONTRAST_REG);
	reg_w(gspca_dev, sd->saturation, SATURATION_REG);
	reg_w(gspca_dev, sd->sharpness, SHARPNESS_REG);

	n = gspca_dev->cam.cam_mode[gspca_dev->curr_mode].priv;
	reg_w(gspca_dev, n, 0x08);

	konica_stream_on(gspca_dev);

	if (gspca_dev->usb_err)
		return gspca_dev->usb_err;

	/* create 4 URBs - 2 on endpoint 0x83 and 2 on 0x082 */
#if MAX_NURBS < 4
#error "Not enough URBs in the gspca table"
#endif
#define SD_NPKT 32
	for (n = 0; n < 4; n++) {
		i = n & 1 ? 0 : 1;
		packet_size =
			le16_to_cpu(alt->endpoint[i].desc.wMaxPacketSize);
		urb = usb_alloc_urb(SD_NPKT, GFP_KERNEL);
		if (!urb) {
			pr_err("usb_alloc_urb failed\n");
			return -ENOMEM;
		}
		gspca_dev->urb[n] = urb;
		urb->transfer_buffer = usb_alloc_coherent(gspca_dev->dev,
						packet_size * SD_NPKT,
						GFP_KERNEL,
						&urb->transfer_dma);
		if (urb->transfer_buffer == NULL) {
			pr_err("usb_buffer_alloc failed\n");
			return -ENOMEM;
		}

		urb->dev = gspca_dev->dev;
		urb->context = gspca_dev;
		urb->transfer_buffer_length = packet_size * SD_NPKT;
		urb->pipe = usb_rcvisocpipe(gspca_dev->dev,
					n & 1 ? 0x81 : 0x82);
		urb->transfer_flags = URB_ISO_ASAP
					| URB_NO_TRANSFER_DMA_MAP;
		urb->interval = 1;
		urb->complete = sd_isoc_irq;
		urb->number_of_packets = SD_NPKT;
		for (i = 0; i < SD_NPKT; i++) {
			urb->iso_frame_desc[i].length = packet_size;
			urb->iso_frame_desc[i].offset = packet_size * i;
		}
	}

	return 0;
}

static void sd_stopN(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;

	konica_stream_off(gspca_dev);
#if defined(CONFIG_INPUT) || defined(CONFIG_INPUT_MODULE)
	/* Don't keep the button in the pressed state "forever" if it was
	   pressed when streaming is stopped */
	if (sd->snapshot_pressed) {
		input_report_key(gspca_dev->input_dev, KEY_CAMERA, 0);
		input_sync(gspca_dev->input_dev);
		sd->snapshot_pressed = 0;
	}
#endif
}

/* reception of an URB */
static void sd_isoc_irq(struct urb *urb)
{
	struct gspca_dev *gspca_dev = (struct gspca_dev *) urb->context;
	struct sd *sd = (struct sd *) gspca_dev;
	struct urb *data_urb, *status_urb;
	u8 *data;
	int i, st;

	PDEBUG(D_PACK, "sd isoc irq");
	if (!gspca_dev->streaming)
		return;

	if (urb->status != 0) {
		if (urb->status == -ESHUTDOWN)
			return;		/* disconnection */
#ifdef CONFIG_PM
		if (gspca_dev->frozen)
			return;
#endif
		PDEBUG(D_ERR, "urb status: %d", urb->status);
		st = usb_submit_urb(urb, GFP_ATOMIC);
		if (st < 0)
			pr_err("resubmit urb error %d\n", st);
		return;
	}

	/* if this is a data URB (ep 0x82), wait */
	if (urb->transfer_buffer_length > 32) {
		sd->last_data_urb = urb;
		return;
	}

	status_urb = urb;
	data_urb   = sd->last_data_urb;
	sd->last_data_urb = NULL;

	if (!data_urb || data_urb->start_frame != status_urb->start_frame) {
		PDEBUG(D_ERR|D_PACK, "lost sync on frames");
		goto resubmit;
	}

	if (data_urb->number_of_packets != status_urb->number_of_packets) {
		PDEBUG(D_ERR|D_PACK,
		       "no packets does not match, data: %d, status: %d",
		       data_urb->number_of_packets,
		       status_urb->number_of_packets);
		goto resubmit;
	}

	for (i = 0; i < status_urb->number_of_packets; i++) {
		if (data_urb->iso_frame_desc[i].status ||
		    status_urb->iso_frame_desc[i].status) {
			PDEBUG(D_ERR|D_PACK,
			       "pkt %d data-status %d, status-status %d", i,
			       data_urb->iso_frame_desc[i].status,
			       status_urb->iso_frame_desc[i].status);
			gspca_dev->last_packet_type = DISCARD_PACKET;
			continue;
		}

		if (status_urb->iso_frame_desc[i].actual_length != 1) {
			PDEBUG(D_ERR|D_PACK,
			       "bad status packet length %d",
			       status_urb->iso_frame_desc[i].actual_length);
			gspca_dev->last_packet_type = DISCARD_PACKET;
			continue;
		}

		st = *((u8 *)status_urb->transfer_buffer
				+ status_urb->iso_frame_desc[i].offset);

		data = (u8 *)data_urb->transfer_buffer
				+ data_urb->iso_frame_desc[i].offset;

		/* st: 0x80-0xff: frame start with frame number (ie 0-7f)
		 * otherwise:
		 * bit 0 0: keep packet
		 *	 1: drop packet (padding data)
		 *
		 * bit 4 0 button not clicked
		 *       1 button clicked
		 * button is used to `take a picture' (in software)
		 */
		if (st & 0x80) {
			gspca_frame_add(gspca_dev, LAST_PACKET, NULL, 0);
			gspca_frame_add(gspca_dev, FIRST_PACKET, NULL, 0);
		} else {
#if defined(CONFIG_INPUT) || defined(CONFIG_INPUT_MODULE)
			u8 button_state = st & 0x40 ? 1 : 0;
			if (sd->snapshot_pressed != button_state) {
				input_report_key(gspca_dev->input_dev,
						 KEY_CAMERA,
						 button_state);
				input_sync(gspca_dev->input_dev);
				sd->snapshot_pressed = button_state;
			}
#endif
			if (st & 0x01)
				continue;
		}
		gspca_frame_add(gspca_dev, INTER_PACKET, data,
				data_urb->iso_frame_desc[i].actual_length);
	}

resubmit:
	if (data_urb) {
		st = usb_submit_urb(data_urb, GFP_ATOMIC);
		if (st < 0)
			PDEBUG(D_ERR|D_PACK,
			       "usb_submit_urb(data_urb) ret %d", st);
	}
	st = usb_submit_urb(status_urb, GFP_ATOMIC);
	if (st < 0)
		pr_err("usb_submit_urb(status_urb) ret %d\n", st);
}

static int sd_setbrightness(struct gspca_dev *gspca_dev, __s32 val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	sd->brightness = val;
	if (gspca_dev->streaming) {
		konica_stream_off(gspca_dev);
		reg_w(gspca_dev, sd->brightness, BRIGHTNESS_REG);
		konica_stream_on(gspca_dev);
	}

	return 0;
}

static int sd_getbrightness(struct gspca_dev *gspca_dev, __s32 *val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	*val = sd->brightness;

	return 0;
}

static int sd_setcontrast(struct gspca_dev *gspca_dev, __s32 val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	sd->contrast = val;
	if (gspca_dev->streaming) {
		konica_stream_off(gspca_dev);
		reg_w(gspca_dev, sd->contrast, CONTRAST_REG);
		konica_stream_on(gspca_dev);
	}

	return 0;
}

static int sd_getcontrast(struct gspca_dev *gspca_dev, __s32 *val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	*val = sd->contrast;

	return 0;
}

static int sd_setsaturation(struct gspca_dev *gspca_dev, __s32 val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	sd->saturation = val;
	if (gspca_dev->streaming) {
		konica_stream_off(gspca_dev);
		reg_w(gspca_dev, sd->saturation, SATURATION_REG);
		konica_stream_on(gspca_dev);
	}
	return 0;
}

static int sd_getsaturation(struct gspca_dev *gspca_dev, __s32 *val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	*val = sd->saturation;

	return 0;
}

static int sd_setwhitebal(struct gspca_dev *gspca_dev, __s32 val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	sd->whitebal = val;
	if (gspca_dev->streaming) {
		konica_stream_off(gspca_dev);
		reg_w(gspca_dev, sd->whitebal, WHITEBAL_REG);
		konica_stream_on(gspca_dev);
	}
	return 0;
}

static int sd_getwhitebal(struct gspca_dev *gspca_dev, __s32 *val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	*val = sd->whitebal;

	return 0;
}

static int sd_setsharpness(struct gspca_dev *gspca_dev, __s32 val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	sd->sharpness = val;
	if (gspca_dev->streaming) {
		konica_stream_off(gspca_dev);
		reg_w(gspca_dev, sd->sharpness, SHARPNESS_REG);
		konica_stream_on(gspca_dev);
	}
	return 0;
}

static int sd_getsharpness(struct gspca_dev *gspca_dev, __s32 *val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	*val = sd->sharpness;

	return 0;
}

/* sub-driver description */
static const struct sd_desc sd_desc = {
	.name = MODULE_NAME,
	.ctrls = sd_ctrls,
	.nctrls = ARRAY_SIZE(sd_ctrls),
	.config = sd_config,
	.init = sd_init,
	.start = sd_start,
	.stopN = sd_stopN,
#if defined(CONFIG_INPUT) || defined(CONFIG_INPUT_MODULE)
	.other_input = 1,
#endif
};

/* -- module initialisation -- */
static const struct usb_device_id device_table[] = {
	{USB_DEVICE(0x04c8, 0x0720)}, /* Intel YC 76 */
	{}
};
MODULE_DEVICE_TABLE(usb, device_table);

/* -- device connect -- */
static int sd_probe(struct usb_interface *intf,
			const struct usb_device_id *id)
{
	return gspca_dev_probe(intf, id, &sd_desc, sizeof(struct sd),
				THIS_MODULE);
}

static struct usb_driver sd_driver = {
	.name = MODULE_NAME,
	.id_table = device_table,
	.probe = sd_probe,
	.disconnect = gspca_disconnect,
#ifdef CONFIG_PM
	.suspend = gspca_suspend,
	.resume = gspca_resume,
#endif
};

/* -- module insert / remove -- */
static int __init sd_mod_init(void)
{
	return usb_register(&sd_driver);
}
static void __exit sd_mod_exit(void)
{
	usb_deregister(&sd_driver);
}

module_init(sd_mod_init);
module_exit(sd_mod_exit);

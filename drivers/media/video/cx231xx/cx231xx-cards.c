/*
   cx231xx-cards.c - driver for Conexant Cx23100/101/102
				USB video capture devices

   Copyright (C) 2008 <srinivasa.deevi at conexant dot com>
				Based on em28xx driver

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/usb.h>
#include <media/tuner.h>
#include <media/tveeprom.h>
#include <media/v4l2-common.h>
#include <media/v4l2-chip-ident.h>

#include <media/cx25840.h>
#include "xc5000.h"

#include "cx231xx.h"

static int tuner = -1;
module_param(tuner, int, 0444);
MODULE_PARM_DESC(tuner, "tuner type");

static unsigned int disable_ir;
module_param(disable_ir, int, 0444);
MODULE_PARM_DESC(disable_ir, "disable infrared remote support");

/* Bitmask marking allocated devices from 0 to CX231XX_MAXBOARDS */
static unsigned long cx231xx_devused;

/*
 *  Reset sequences for analog/digital modes
 */

static struct cx231xx_reg_seq RDE250_XCV_TUNER[] = {
	{0x03, 0x01, 10},
	{0x03, 0x00, 30},
	{0x03, 0x01, 10},
	{-1, -1, -1},
};

/*
 *  Board definitions
 */
struct cx231xx_board cx231xx_boards[] = {
	[CX231XX_BOARD_UNKNOWN] = {
		.name = "Unknown CX231xx video grabber",
		.tuner_type = TUNER_ABSENT,
		.input = {{
				.type = CX231XX_VMUX_TELEVISION,
				.vmux = CX231XX_VIN_3_1,
				.amux = CX231XX_AMUX_VIDEO,
				.gpio = 0,
			}, {
				.type = CX231XX_VMUX_COMPOSITE1,
				.vmux = CX231XX_VIN_2_1,
				.amux = CX231XX_AMUX_LINE_IN,
				.gpio = 0,
			}, {
				.type = CX231XX_VMUX_SVIDEO,
				.vmux = CX231XX_VIN_1_1 |
					(CX231XX_VIN_1_2 << 8) |
					CX25840_SVIDEO_ON,
				.amux = CX231XX_AMUX_LINE_IN,
				.gpio = 0,
			}
		},
	},
	[CX231XX_BOARD_CNXT_RDE_250] = {
		.name = "Conexant Hybrid TV - RDE250",
		.tuner_type = TUNER_XC5000,
		.tuner_addr = 0x61,
		.tuner_gpio = RDE250_XCV_TUNER,
		.tuner_sif_gpio = 0x05,
		.tuner_scl_gpio = 0x1a,
		.tuner_sda_gpio = 0x1b,
		.decoder = CX231XX_AVDECODER,
		.demod_xfer_mode = 0,
		.ctl_pin_status_mask = 0xFFFFFFC4,
		.agc_analog_digital_select_gpio = 0x0c,
		.gpio_pin_status_mask = 0x4001000,
		.tuner_i2c_master = 1,
		.demod_i2c_master = 2,
		.has_dvb = 1,
		.demod_addr = 0x02,
		.norm = V4L2_STD_PAL,

		.input = {{
				.type = CX231XX_VMUX_TELEVISION,
				.vmux = CX231XX_VIN_3_1,
				.amux = CX231XX_AMUX_VIDEO,
				.gpio = 0,
			}, {
				.type = CX231XX_VMUX_COMPOSITE1,
				.vmux = CX231XX_VIN_2_1,
				.amux = CX231XX_AMUX_LINE_IN,
				.gpio = 0,
			}, {
				.type = CX231XX_VMUX_SVIDEO,
				.vmux = CX231XX_VIN_1_1 |
					(CX231XX_VIN_1_2 << 8) |
					CX25840_SVIDEO_ON,
				.amux = CX231XX_AMUX_LINE_IN,
				.gpio = 0,
			}
		},
	},

	[CX231XX_BOARD_CNXT_RDU_250] = {
		.name = "Conexant Hybrid TV - RDU250",
		.tuner_type = TUNER_XC5000,
		.tuner_addr = 0x61,
		.tuner_gpio = RDE250_XCV_TUNER,
		.tuner_sif_gpio = 0x05,
		.tuner_scl_gpio = 0x1a,
		.tuner_sda_gpio = 0x1b,
		.decoder = CX231XX_AVDECODER,
		.demod_xfer_mode = 0,
		.ctl_pin_status_mask = 0xFFFFFFC4,
		.agc_analog_digital_select_gpio = 0x0c,
		.gpio_pin_status_mask = 0x4001000,
		.tuner_i2c_master = 1,
		.demod_i2c_master = 2,
		.has_dvb = 1,
		.demod_addr = 0x32,
		.norm = V4L2_STD_NTSC,

		.input = {{
				.type = CX231XX_VMUX_TELEVISION,
				.vmux = CX231XX_VIN_3_1,
				.amux = CX231XX_AMUX_VIDEO,
				.gpio = 0,
			}, {
				.type = CX231XX_VMUX_COMPOSITE1,
				.vmux = CX231XX_VIN_2_1,
				.amux = CX231XX_AMUX_LINE_IN,
				.gpio = 0,
			}, {
				.type = CX231XX_VMUX_SVIDEO,
				.vmux = CX231XX_VIN_1_1 |
					(CX231XX_VIN_1_2 << 8) |
					CX25840_SVIDEO_ON,
				.amux = CX231XX_AMUX_LINE_IN,
				.gpio = 0,
			}
		},
	},
};
const unsigned int cx231xx_bcount = ARRAY_SIZE(cx231xx_boards);

/* table of devices that work with this driver */
struct usb_device_id cx231xx_id_table[] = {
	{USB_DEVICE(0x0572, 0x5A3C),
	 .driver_info = CX231XX_BOARD_UNKNOWN},
	{USB_DEVICE(0x0572, 0x58A2),
	 .driver_info = CX231XX_BOARD_CNXT_RDE_250},
	{USB_DEVICE(0x0572, 0x58A1),
	 .driver_info = CX231XX_BOARD_CNXT_RDU_250},
	{},
};

MODULE_DEVICE_TABLE(usb, cx231xx_id_table);

/* cx231xx_tuner_callback
 * will be used to reset XC5000 tuner using GPIO pin
 */

int cx231xx_tuner_callback(void *ptr, int component, int command, int arg)
{
	int rc = 0;
	struct cx231xx *dev = ptr;

	if (dev->tuner_type == TUNER_XC5000) {
		if (command == XC5000_TUNER_RESET) {
			cx231xx_info
				("Tuner CB: RESET: cmd %d : tuner type %d \n",
				 command, dev->tuner_type);
			cx231xx_set_gpio_value(dev, dev->board.tuner_gpio->bit,
					       1);
			msleep(10);
			cx231xx_set_gpio_value(dev, dev->board.tuner_gpio->bit,
					       0);
			msleep(330);
			cx231xx_set_gpio_value(dev, dev->board.tuner_gpio->bit,
					       1);
			msleep(10);
		}
	}
	return rc;
}
EXPORT_SYMBOL_GPL(cx231xx_tuner_callback);

static inline void cx231xx_set_model(struct cx231xx *dev)
{
	memcpy(&dev->board, &cx231xx_boards[dev->model], sizeof(dev->board));
}

/* Since cx231xx_pre_card_setup() requires a proper dev->model,
 * this won't work for boards with generic PCI IDs
 */
void cx231xx_pre_card_setup(struct cx231xx *dev)
{

	cx231xx_set_model(dev);

	cx231xx_info("Identified as %s (card=%d)\n",
		     dev->board.name, dev->model);

	/* set the direction for GPIO pins */
	cx231xx_set_gpio_direction(dev, dev->board.tuner_gpio->bit, 1);
	cx231xx_set_gpio_value(dev, dev->board.tuner_gpio->bit, 1);
	cx231xx_set_gpio_direction(dev, dev->board.tuner_sif_gpio, 1);

	/* request some modules if any required */

	/* reset the Tuner */
	cx231xx_gpio_set(dev, dev->board.tuner_gpio);

	/* set the mode to Analog mode initially */
	cx231xx_set_mode(dev, CX231XX_ANALOG_MODE);

	/* Unlock device */
	/* cx231xx_set_mode(dev, CX231XX_SUSPEND); */

}

static void cx231xx_config_tuner(struct cx231xx *dev)
{
	struct tuner_setup tun_setup;
	struct v4l2_frequency f;

	if (dev->tuner_type == TUNER_ABSENT)
		return;

	tun_setup.mode_mask = T_ANALOG_TV | T_RADIO;
	tun_setup.type = dev->tuner_type;
	tun_setup.addr = dev->tuner_addr;
	tun_setup.tuner_callback = cx231xx_tuner_callback;

	tuner_call(dev, tuner, s_type_addr, &tun_setup);

#if 0
	if (tun_setup.type == TUNER_XC5000) {
		static struct xc2028_ctrl ctrl = {
			.fname = XC5000_DEFAULT_FIRMWARE,
			.max_len = 64,
			.demod = 0;
		};
		struct v4l2_priv_tun_config cfg = {
			.tuner = dev->tuner_type,
			.priv = &ctrl,
		};
		tuner_call(dev, tuner, s_config, &cfg);
	}
#endif
	/* configure tuner */
	f.tuner = 0;
	f.type = V4L2_TUNER_ANALOG_TV;
	f.frequency = 9076;	/* just a magic number */
	dev->ctl_freq = f.frequency;
	call_all(dev, tuner, s_frequency, &f);

}

/* ----------------------------------------------------------------------- */
void cx231xx_register_i2c_ir(struct cx231xx *dev)
{
	if (disable_ir)
		return;

	/* REVISIT: instantiate IR device */

	/* detect & configure */
	switch (dev->model) {

	case CX231XX_BOARD_CNXT_RDE_250:
		break;
	case CX231XX_BOARD_CNXT_RDU_250:
		break;
	default:
		break;
	}
}

void cx231xx_card_setup(struct cx231xx *dev)
{

	cx231xx_set_model(dev);

	dev->tuner_type = cx231xx_boards[dev->model].tuner_type;
	if (cx231xx_boards[dev->model].tuner_addr)
		dev->tuner_addr = cx231xx_boards[dev->model].tuner_addr;

	/* request some modules */
	if (dev->board.decoder == CX231XX_AVDECODER) {
		dev->sd_cx25840 = v4l2_i2c_new_subdev(&dev->v4l2_dev,
					&dev->i2c_bus[0].i2c_adap,
					"cx25840", "cx25840", 0x88 >> 1);
		if (dev->sd_cx25840 == NULL)
			cx231xx_info("cx25840 subdev registration failure\n");
		cx25840_call(dev, core, load_fw);

	}

	if (dev->board.tuner_type != TUNER_ABSENT) {
		dev->sd_tuner =	v4l2_i2c_new_subdev(&dev->v4l2_dev,
				&dev->i2c_bus[1].i2c_adap,
				"tuner", "tuner", 0xc2 >> 1);
		if (dev->sd_tuner == NULL)
			cx231xx_info("tuner subdev registration failure\n");

		cx231xx_config_tuner(dev);
	}

	cx231xx_config_tuner(dev);

#if 0
	/* TBD  IR will be added later */
	cx231xx_ir_init(dev);
#endif
}

/*
 * cx231xx_config()
 * inits registers with sane defaults
 */
int cx231xx_config(struct cx231xx *dev)
{
	/* TBD need to add cx231xx specific code */
	dev->mute = 1;		/* maybe not the right place... */
	dev->volume = 0x1f;

	return 0;
}

/*
 * cx231xx_config_i2c()
 * configure i2c attached devices
 */
void cx231xx_config_i2c(struct cx231xx *dev)
{
	/* u32 input = INPUT(dev->video_input)->vmux; */

	call_all(dev, video, s_stream, 1);
}

/*
 * cx231xx_realease_resources()
 * unregisters the v4l2,i2c and usb devices
 * called when the device gets disconected or at module unload
*/
void cx231xx_release_resources(struct cx231xx *dev)
{

#if 0		/* TBD IR related  */
	if (dev->ir)
		cx231xx_ir_fini(dev);
#endif

	cx231xx_release_analog_resources(dev);

	cx231xx_remove_from_devlist(dev);

	cx231xx_dev_uninit(dev);

	usb_put_dev(dev->udev);

	/* Mark device as unused */
	cx231xx_devused &= ~(1 << dev->devno);
}

/*
 * cx231xx_init_dev()
 * allocates and inits the device structs, registers i2c bus and v4l device
 */
static int cx231xx_init_dev(struct cx231xx **devhandle, struct usb_device *udev,
			    int minor)
{
	struct cx231xx *dev = *devhandle;
	int retval = -ENOMEM;
	int errCode;
	unsigned int maxh, maxw;

	dev->udev = udev;
	mutex_init(&dev->lock);
	mutex_init(&dev->ctrl_urb_lock);
	mutex_init(&dev->gpio_i2c_lock);

	spin_lock_init(&dev->video_mode.slock);
	spin_lock_init(&dev->vbi_mode.slock);
	spin_lock_init(&dev->sliced_cc_mode.slock);

	init_waitqueue_head(&dev->open);
	init_waitqueue_head(&dev->wait_frame);
	init_waitqueue_head(&dev->wait_stream);

	dev->cx231xx_read_ctrl_reg = cx231xx_read_ctrl_reg;
	dev->cx231xx_write_ctrl_reg = cx231xx_write_ctrl_reg;
	dev->cx231xx_send_usb_command = cx231xx_send_usb_command;
	dev->cx231xx_gpio_i2c_read = cx231xx_gpio_i2c_read;
	dev->cx231xx_gpio_i2c_write = cx231xx_gpio_i2c_write;

	/* Query cx231xx to find what pcb config it is related to */
	initialize_cx231xx(dev);

	/* Cx231xx pre card setup */
	cx231xx_pre_card_setup(dev);

	errCode = cx231xx_config(dev);
	if (errCode) {
		cx231xx_errdev("error configuring device\n");
		return -ENOMEM;
	}

	/* set default norm */
	dev->norm = dev->board.norm;

	/* register i2c bus */
	errCode = cx231xx_dev_init(dev);
	if (errCode < 0) {
		cx231xx_errdev("%s: cx231xx_i2c_register - errCode [%d]!\n",
			       __func__, errCode);
		return errCode;
	}

	/* Do board specific init */
	cx231xx_card_setup(dev);

	/* configure the device */
	cx231xx_config_i2c(dev);

	maxw = norm_maxw(dev);
	maxh = norm_maxh(dev);

	/* set default image size */
	dev->width = maxw;
	dev->height = maxh;
	dev->interlaced = 0;
	dev->hscale = 0;
	dev->vscale = 0;
	dev->video_input = 0;

	errCode = cx231xx_config(dev);
	if (errCode < 0) {
		cx231xx_errdev("%s: cx231xx_config - errCode [%d]!\n",
			       __func__, errCode);
		return errCode;
	}

	/* init video dma queues */
	INIT_LIST_HEAD(&dev->video_mode.vidq.active);
	INIT_LIST_HEAD(&dev->video_mode.vidq.queued);

	/* init vbi dma queues */
	INIT_LIST_HEAD(&dev->vbi_mode.vidq.active);
	INIT_LIST_HEAD(&dev->vbi_mode.vidq.queued);

	/* Reset other chips required if they are tied up with GPIO pins */

	cx231xx_add_into_devlist(dev);

	retval = cx231xx_register_analog_devices(dev);
	if (retval < 0) {
		cx231xx_release_resources(dev);
		goto fail_reg_devices;
	}

	cx231xx_init_extension(dev);

	return 0;

fail_reg_devices:
	mutex_unlock(&dev->lock);
	return retval;
}

#if defined(CONFIG_MODULES) && defined(MODULE)
static void request_module_async(struct work_struct *work)
{
	struct cx231xx *dev = container_of(work,
					   struct cx231xx, request_module_wk);

	if (dev->has_alsa_audio)
		request_module("cx231xx-alsa");

	if (dev->board.has_dvb)
		request_module("cx231xx-dvb");

}

static void request_modules(struct cx231xx *dev)
{
	INIT_WORK(&dev->request_module_wk, request_module_async);
	schedule_work(&dev->request_module_wk);
}
#else
#define request_modules(dev)
#endif /* CONFIG_MODULES */

/*
 * cx231xx_usb_probe()
 * checks for supported devices
 */
static int cx231xx_usb_probe(struct usb_interface *interface,
			     const struct usb_device_id *id)
{
	struct usb_device *udev;
	struct usb_interface *uif;
	struct cx231xx *dev = NULL;
	int retval = -ENODEV;
	int nr = 0, ifnum;
	int i, isoc_pipe = 0;
	char *speed;
	char descr[255] = "";
	struct usb_interface *lif = NULL;
	int skip_interface = 0;
	struct usb_interface_assoc_descriptor *assoc_desc;

	udev = usb_get_dev(interface_to_usbdev(interface));
	ifnum = interface->altsetting[0].desc.bInterfaceNumber;

	if (!ifnum) {
		/*
		 * Interface number 0 - IR interface
		 */
		/* Check to see next free device and mark as used */
		nr = find_first_zero_bit(&cx231xx_devused, CX231XX_MAXBOARDS);
		cx231xx_devused |= 1 << nr;

		if (nr >= CX231XX_MAXBOARDS) {
			cx231xx_err(DRIVER_NAME ": Supports only %i cx231xx boards.\n",
				     CX231XX_MAXBOARDS);
			cx231xx_devused &= ~(1 << nr);
			return -ENOMEM;
		}

		/* allocate memory for our device state and initialize it */
		dev = kzalloc(sizeof(*dev), GFP_KERNEL);
		if (dev == NULL) {
			cx231xx_err(DRIVER_NAME ": out of memory!\n");
			cx231xx_devused &= ~(1 << nr);
			return -ENOMEM;
		}

		snprintf(dev->name, 29, "cx231xx #%d", nr);
		dev->devno = nr;
		dev->model = id->driver_info;
		dev->video_mode.alt = -1;
		dev->interface_count++;

		/* reset gpio dir and value */
		dev->gpio_dir = 0;
		dev->gpio_val = 0;
		dev->xc_fw_load_done = 0;
		dev->has_alsa_audio = 1;
		dev->power_mode = -1;

		/* 0 - vbi ; 1 -sliced cc mode */
		dev->vbi_or_sliced_cc_mode = 0;

		/* get maximum no.of IAD interfaces */
		assoc_desc = udev->actconfig->intf_assoc[0];
		dev->max_iad_interface_count = assoc_desc->bInterfaceCount;

		/* init CIR module TBD */

		/* store the current interface */
		lif = interface;

		switch (udev->speed) {
		case USB_SPEED_LOW:
			speed = "1.5";
			break;
		case USB_SPEED_UNKNOWN:
		case USB_SPEED_FULL:
			speed = "12";
			break;
		case USB_SPEED_HIGH:
			speed = "480";
			break;
		default:
			speed = "unknown";
		}

		if (udev->manufacturer)
			strlcpy(descr, udev->manufacturer, sizeof(descr));

		if (udev->product) {
			if (*descr)
				strlcat(descr, " ", sizeof(descr));
			strlcat(descr, udev->product, sizeof(descr));
		}
		if (*descr)
			strlcat(descr, " ", sizeof(descr));

		cx231xx_info("New device %s@ %s Mbps "
		     "(%04x:%04x) with %d interfaces\n",
		     descr,
		     speed,
		     le16_to_cpu(udev->descriptor.idVendor),
		     le16_to_cpu(udev->descriptor.idProduct),
		     dev->max_iad_interface_count);
	} else {
		/* Get dev structure first */
		dev = usb_get_intfdata(udev->actconfig->interface[0]);
		if (dev == NULL) {
			cx231xx_err(DRIVER_NAME ": out of first interface!\n");
			return -ENODEV;
		}

		/* store the interface 0 back */
		lif = udev->actconfig->interface[0];

		/* increment interface count */
		dev->interface_count++;

		/* get device number */
		nr = dev->devno;

		/*
		 * set skip interface, for all interfaces but
		 * interface 1 and the last one
		 */
		if ((ifnum != 1) && ((dev->interface_count - 1)
				     != dev->max_iad_interface_count))
			skip_interface = 1;

		if (ifnum == 1) {
			assoc_desc = udev->actconfig->intf_assoc[0];
			if (assoc_desc->bFirstInterface != ifnum) {
				cx231xx_err(DRIVER_NAME ": Not found "
					    "matching IAD interface\n");
				return -ENODEV;
			}
		}
	}

	if (skip_interface)
		return -ENODEV;

	cx231xx_info("registering interface %d\n", ifnum);

	/* save our data pointer in this interface device */
	usb_set_intfdata(lif, dev);

	if ((dev->interface_count - 1) != dev->max_iad_interface_count)
		return 0;

	/*
	 * AV device initialization - only done at the last interface
	 */

	/* Create v4l2 device */
	retval = v4l2_device_register(&interface->dev, &dev->v4l2_dev);
	if (retval) {
		cx231xx_errdev("v4l2_device_register failed\n");
		cx231xx_devused &= ~(1 << nr);
		kfree(dev);
		return -EIO;
	}

	/* allocate device struct */
	retval = cx231xx_init_dev(&dev, udev, nr);
	if (retval) {
		cx231xx_devused &= ~(1 << dev->devno);
		v4l2_device_unregister(&dev->v4l2_dev);
		kfree(dev);
		return retval;
	}

	/* compute alternate max packet sizes for video */
	uif = udev->actconfig->interface[dev->current_pcb_config.
		       hs_config_info[0].interface_info.video_index + 1];

	dev->video_mode.end_point_addr = le16_to_cpu(uif->altsetting[0].
			endpoint[isoc_pipe].desc.bEndpointAddress);

	dev->video_mode.num_alt = uif->num_altsetting;
	cx231xx_info("EndPoint Addr 0x%x, Alternate settings: %i\n",
		     dev->video_mode.end_point_addr,
		     dev->video_mode.num_alt);
	dev->video_mode.alt_max_pkt_size =
		kmalloc(32 * dev->video_mode.num_alt, GFP_KERNEL);

	if (dev->video_mode.alt_max_pkt_size == NULL) {
		cx231xx_errdev("out of memory!\n");
		cx231xx_devused &= ~(1 << nr);
		v4l2_device_unregister(&dev->v4l2_dev);
		kfree(dev);
		return -ENOMEM;
	}

	for (i = 0; i < dev->video_mode.num_alt; i++) {
		u16 tmp = le16_to_cpu(uif->altsetting[i].endpoint[isoc_pipe].
				desc.wMaxPacketSize);
		dev->video_mode.alt_max_pkt_size[i] =
		    (tmp & 0x07ff) * (((tmp & 0x1800) >> 11) + 1);
		cx231xx_info("Alternate setting %i, max size= %i\n", i,
			     dev->video_mode.alt_max_pkt_size[i]);
	}

	/* compute alternate max packet sizes for vbi */
	uif = udev->actconfig->interface[dev->current_pcb_config.
				       hs_config_info[0].interface_info.
				       vanc_index + 1];

	dev->vbi_mode.end_point_addr =
	    le16_to_cpu(uif->altsetting[0].endpoint[isoc_pipe].desc.
			bEndpointAddress);

	dev->vbi_mode.num_alt = uif->num_altsetting;
	cx231xx_info("EndPoint Addr 0x%x, Alternate settings: %i\n",
		     dev->vbi_mode.end_point_addr,
		     dev->vbi_mode.num_alt);
	dev->vbi_mode.alt_max_pkt_size =
	    kmalloc(32 * dev->vbi_mode.num_alt, GFP_KERNEL);

	if (dev->vbi_mode.alt_max_pkt_size == NULL) {
		cx231xx_errdev("out of memory!\n");
		cx231xx_devused &= ~(1 << nr);
		v4l2_device_unregister(&dev->v4l2_dev);
		kfree(dev);
		return -ENOMEM;
	}

	for (i = 0; i < dev->vbi_mode.num_alt; i++) {
		u16 tmp =
		    le16_to_cpu(uif->altsetting[i].endpoint[isoc_pipe].
				desc.wMaxPacketSize);
		dev->vbi_mode.alt_max_pkt_size[i] =
		    (tmp & 0x07ff) * (((tmp & 0x1800) >> 11) + 1);
		cx231xx_info("Alternate setting %i, max size= %i\n", i,
			     dev->vbi_mode.alt_max_pkt_size[i]);
	}

	/* compute alternate max packet sizes for sliced CC */
	uif = udev->actconfig->interface[dev->current_pcb_config.
				       hs_config_info[0].interface_info.
				       hanc_index + 1];

	dev->sliced_cc_mode.end_point_addr =
	    le16_to_cpu(uif->altsetting[0].endpoint[isoc_pipe].desc.
			bEndpointAddress);

	dev->sliced_cc_mode.num_alt = uif->num_altsetting;
	cx231xx_info("EndPoint Addr 0x%x, Alternate settings: %i\n",
		     dev->sliced_cc_mode.end_point_addr,
		     dev->sliced_cc_mode.num_alt);
	dev->sliced_cc_mode.alt_max_pkt_size =
		kmalloc(32 * dev->sliced_cc_mode.num_alt, GFP_KERNEL);

	if (dev->sliced_cc_mode.alt_max_pkt_size == NULL) {
		cx231xx_errdev("out of memory!\n");
		cx231xx_devused &= ~(1 << nr);
		v4l2_device_unregister(&dev->v4l2_dev);
		kfree(dev);
		return -ENOMEM;
	}

	for (i = 0; i < dev->sliced_cc_mode.num_alt; i++) {
		u16 tmp = le16_to_cpu(uif->altsetting[i].endpoint[isoc_pipe].
				desc.wMaxPacketSize);
		dev->sliced_cc_mode.alt_max_pkt_size[i] =
		    (tmp & 0x07ff) * (((tmp & 0x1800) >> 11) + 1);
		cx231xx_info("Alternate setting %i, max size= %i\n", i,
			     dev->sliced_cc_mode.alt_max_pkt_size[i]);
	}

	if (dev->current_pcb_config.ts1_source != 0xff) {
		/* compute alternate max packet sizes for TS1 */
		uif = udev->actconfig->interface[dev->current_pcb_config.
					       hs_config_info[0].
					       interface_info.
					       ts1_index + 1];

		dev->ts1_mode.end_point_addr =
		    le16_to_cpu(uif->altsetting[0].endpoint[isoc_pipe].
				desc.bEndpointAddress);

		dev->ts1_mode.num_alt = uif->num_altsetting;
		cx231xx_info("EndPoint Addr 0x%x, Alternate settings: %i\n",
			     dev->ts1_mode.end_point_addr,
			     dev->ts1_mode.num_alt);
		dev->ts1_mode.alt_max_pkt_size =
			kmalloc(32 * dev->ts1_mode.num_alt, GFP_KERNEL);

		if (dev->ts1_mode.alt_max_pkt_size == NULL) {
			cx231xx_errdev("out of memory!\n");
			cx231xx_devused &= ~(1 << nr);
			v4l2_device_unregister(&dev->v4l2_dev);
			kfree(dev);
			return -ENOMEM;
		}

		for (i = 0; i < dev->ts1_mode.num_alt; i++) {
			u16 tmp = le16_to_cpu(uif->altsetting[i].
						endpoint[isoc_pipe].desc.
						wMaxPacketSize);
			dev->ts1_mode.alt_max_pkt_size[i] =
			    (tmp & 0x07ff) * (((tmp & 0x1800) >> 11) + 1);
			cx231xx_info("Alternate setting %i, max size= %i\n", i,
				     dev->ts1_mode.alt_max_pkt_size[i]);
		}
	}

	/* load other modules required */
	request_modules(dev);

	return 0;
}

/*
 * cx231xx_usb_disconnect()
 * called when the device gets diconencted
 * video device will be unregistered on v4l2_close in case it is still open
 */
static void cx231xx_usb_disconnect(struct usb_interface *interface)
{
	struct cx231xx *dev;

	dev = usb_get_intfdata(interface);
	usb_set_intfdata(interface, NULL);

	if (!dev)
		return;

	if (!dev->udev)
		return;

	/* delete v4l2 device */
	v4l2_device_unregister(&dev->v4l2_dev);

	/* wait until all current v4l2 io is finished then deallocate
	   resources */
	mutex_lock(&dev->lock);

	wake_up_interruptible_all(&dev->open);

	if (dev->users) {
		cx231xx_warn
		    ("device /dev/video%d is open! Deregistration and memory "
		     "deallocation are deferred on close.\n", dev->vdev->num);

		dev->state |= DEV_MISCONFIGURED;
		cx231xx_uninit_isoc(dev);
		dev->state |= DEV_DISCONNECTED;
		wake_up_interruptible(&dev->wait_frame);
		wake_up_interruptible(&dev->wait_stream);
	} else {
		dev->state |= DEV_DISCONNECTED;
		cx231xx_release_resources(dev);
	}

	cx231xx_close_extension(dev);

	mutex_unlock(&dev->lock);

	if (!dev->users) {
		kfree(dev->video_mode.alt_max_pkt_size);
		kfree(dev->vbi_mode.alt_max_pkt_size);
		kfree(dev->sliced_cc_mode.alt_max_pkt_size);
		kfree(dev->ts1_mode.alt_max_pkt_size);
		kfree(dev);
	}
}

static struct usb_driver cx231xx_usb_driver = {
	.name = "cx231xx",
	.probe = cx231xx_usb_probe,
	.disconnect = cx231xx_usb_disconnect,
	.id_table = cx231xx_id_table,
};

static int __init cx231xx_module_init(void)
{
	int result;

	printk(KERN_INFO DRIVER_NAME " v4l2 driver loaded.\n");

	/* register this driver with the USB subsystem */
	result = usb_register(&cx231xx_usb_driver);
	if (result)
		cx231xx_err(DRIVER_NAME
			    " usb_register failed. Error number %d.\n", result);

	return result;
}

static void __exit cx231xx_module_exit(void)
{
	/* deregister this driver with the USB subsystem */
	usb_deregister(&cx231xx_usb_driver);
}

module_init(cx231xx_module_init);
module_exit(cx231xx_module_exit);

/*
 * Copyright (C) 2005-2006 Micronas USA Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License (Version 2) as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston MA 02111-1307, USA.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/wait.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/time.h>
#include <linux/mm.h>
#include <linux/usb.h>
#include <linux/i2c.h>
#include <asm/byteorder.h>
#include <media/saa7115.h>
#include <media/tuner.h>
#include <media/uda1342.h>

#include "go7007-priv.h"

static unsigned int assume_endura;
module_param(assume_endura, int, 0644);
MODULE_PARM_DESC(assume_endura,
			"when probing fails, hardware is a Pelco Endura");

/* #define GO7007_I2C_DEBUG */ /* for debugging the EZ-USB I2C adapter */

#define	HPI_STATUS_ADDR	0xFFF4
#define	INT_PARAM_ADDR	0xFFF6
#define	INT_INDEX_ADDR	0xFFF8

/*
 * Pipes on EZ-USB interface:
 *	0 snd - Control
 *	0 rcv - Control
 *	2 snd - Download firmware (control)
 *	4 rcv - Read Interrupt (interrupt)
 *	6 rcv - Read Video (bulk)
 *	8 rcv - Read Audio (bulk)
 */

#define GO7007_USB_EZUSB		(1<<0)
#define GO7007_USB_EZUSB_I2C		(1<<1)

struct go7007_usb_board {
	unsigned int flags;
	struct go7007_board_info main_info;
};

struct go7007_usb {
	const struct go7007_usb_board *board;
	struct mutex i2c_lock;
	struct usb_device *usbdev;
	struct urb *video_urbs[8];
	struct urb *audio_urbs[8];
	struct urb *intr_urb;
};

/*********************** Product specification data ***********************/

static const struct go7007_usb_board board_matrix_ii = {
	.flags		= GO7007_USB_EZUSB,
	.main_info	= {
		.flags		 = GO7007_BOARD_HAS_AUDIO |
					GO7007_BOARD_USE_ONBOARD_I2C,
		.audio_flags	 = GO7007_AUDIO_I2S_MODE_1 |
					GO7007_AUDIO_WORD_16,
		.audio_rate	 = 48000,
		.audio_bclk_div	 = 8,
		.audio_main_div	 = 2,
		.hpi_buffer_cap  = 7,
		.sensor_flags	 = GO7007_SENSOR_656 |
					GO7007_SENSOR_VALID_ENABLE |
					GO7007_SENSOR_TV |
					GO7007_SENSOR_SAA7115 |
					GO7007_SENSOR_VBI |
					GO7007_SENSOR_SCALING,
		.num_i2c_devs	 = 1,
		.i2c_devs	 = {
			{
				.type	= "saa7115",
				.addr	= 0x20,
				.is_video = 1,
			},
		},
		.num_inputs	 = 2,
		.inputs		 = {
			{
				.video_input	= 0,
				.name		= "Composite",
			},
			{
				.video_input	= 9,
				.name		= "S-Video",
			},
		},
		.video_config	= SAA7115_IDQ_IS_DEFAULT,
	},
};

static const struct go7007_usb_board board_matrix_reload = {
	.flags		= GO7007_USB_EZUSB,
	.main_info	= {
		.flags		 = GO7007_BOARD_HAS_AUDIO |
					GO7007_BOARD_USE_ONBOARD_I2C,
		.audio_flags	 = GO7007_AUDIO_I2S_MODE_1 |
					GO7007_AUDIO_I2S_MASTER |
					GO7007_AUDIO_WORD_16,
		.audio_rate	 = 48000,
		.audio_bclk_div	 = 8,
		.audio_main_div	 = 2,
		.hpi_buffer_cap  = 7,
		.sensor_flags	 = GO7007_SENSOR_656 |
					GO7007_SENSOR_TV,
		.num_i2c_devs	 = 1,
		.i2c_devs	 = {
			{
				.type	= "saa7113",
				.addr	= 0x25,
				.is_video = 1,
			},
		},
		.num_inputs	 = 2,
		.inputs		 = {
			{
				.video_input	= 0,
				.name		= "Composite",
			},
			{
				.video_input	= 9,
				.name		= "S-Video",
			},
		},
		.video_config	= SAA7115_IDQ_IS_DEFAULT,
	},
};

static const struct go7007_usb_board board_star_trek = {
	.flags		= GO7007_USB_EZUSB | GO7007_USB_EZUSB_I2C,
	.main_info	= {
		.flags		 = GO7007_BOARD_HAS_AUDIO, /* |
					GO7007_BOARD_HAS_TUNER, */
		.sensor_flags	 = GO7007_SENSOR_656 |
					GO7007_SENSOR_VALID_ENABLE |
					GO7007_SENSOR_TV |
					GO7007_SENSOR_SAA7115 |
					GO7007_SENSOR_VBI |
					GO7007_SENSOR_SCALING,
		.audio_flags	 = GO7007_AUDIO_I2S_MODE_1 |
					GO7007_AUDIO_WORD_16,
		.audio_bclk_div	 = 8,
		.audio_main_div	 = 2,
		.hpi_buffer_cap  = 7,
		.num_i2c_devs	 = 1,
		.i2c_devs	 = {
			{
				.type	= "saa7115",
				.addr	= 0x20,
				.is_video = 1,
			},
		},
		.num_inputs	 = 2,
		.inputs		 = {
		/*	{
		 *		.video_input	= 3,
		 *		.audio_index	= AUDIO_TUNER,
		 *		.name		= "Tuner",
		 *	},
		 */
			{
				.video_input	= 1,
			/*	.audio_index	= AUDIO_EXTERN, */
				.name		= "Composite",
			},
			{
				.video_input	= 8,
			/*	.audio_index	= AUDIO_EXTERN, */
				.name		= "S-Video",
			},
		},
		.video_config	= SAA7115_IDQ_IS_DEFAULT,
	},
};

static const struct go7007_usb_board board_px_tv402u = {
	.flags		= GO7007_USB_EZUSB | GO7007_USB_EZUSB_I2C,
	.main_info	= {
		.flags		 = GO7007_BOARD_HAS_AUDIO |
					GO7007_BOARD_HAS_TUNER,
		.sensor_flags	 = GO7007_SENSOR_656 |
					GO7007_SENSOR_VALID_ENABLE |
					GO7007_SENSOR_TV |
					GO7007_SENSOR_SAA7115 |
					GO7007_SENSOR_VBI |
					GO7007_SENSOR_SCALING,
		.audio_flags	 = GO7007_AUDIO_I2S_MODE_1 |
					GO7007_AUDIO_WORD_16,
		.audio_bclk_div	 = 8,
		.audio_main_div	 = 2,
		.hpi_buffer_cap  = 7,
		.num_i2c_devs	 = 5,
		.i2c_devs	 = {
			{
				.type	= "saa7115",
				.addr	= 0x20,
				.is_video = 1,
			},
			{
				.type	= "uda1342",
				.addr	= 0x1a,
				.is_audio = 1,
			},
			{
				.type	= "tuner",
				.addr	= 0x60,
			},
			{
				.type	= "tuner",
				.addr	= 0x43,
			},
			{
				.type	= "sony-btf-mpx",
				.addr	= 0x44,
			},
		},
		.num_inputs	 = 3,
		.inputs		 = {
			{
				.video_input	= 3,
				.audio_index	= 0,
				.name		= "Tuner",
			},
			{
				.video_input	= 1,
				.audio_index	= 1,
				.name		= "Composite",
			},
			{
				.video_input	= 8,
				.audio_index	= 1,
				.name		= "S-Video",
			},
		},
		.video_config	= SAA7115_IDQ_IS_DEFAULT,
		.num_aud_inputs	 = 2,
		.aud_inputs	 = {
			{
				.audio_input	= UDA1342_IN2,
				.name		= "Tuner",
			},
			{
				.audio_input	= UDA1342_IN1,
				.name		= "Line In",
			},
		},
	},
};

static const struct go7007_usb_board board_xmen = {
	.flags		= 0,
	.main_info	= {
		.flags		  = GO7007_BOARD_USE_ONBOARD_I2C,
		.hpi_buffer_cap   = 0,
		.sensor_flags	  = GO7007_SENSOR_VREF_POLAR,
		.sensor_width	  = 320,
		.sensor_height	  = 240,
		.sensor_framerate = 30030,
		.audio_flags	  = GO7007_AUDIO_ONE_CHANNEL |
					GO7007_AUDIO_I2S_MODE_3 |
					GO7007_AUDIO_WORD_14 |
					GO7007_AUDIO_I2S_MASTER |
					GO7007_AUDIO_BCLK_POLAR |
					GO7007_AUDIO_OKI_MODE,
		.audio_rate	  = 8000,
		.audio_bclk_div	  = 48,
		.audio_main_div	  = 1,
		.num_i2c_devs	  = 1,
		.i2c_devs	  = {
			{
				.type	= "ov7640",
				.addr	= 0x21,
			},
		},
		.num_inputs	  = 1,
		.inputs		  = {
			{
				.name		= "Camera",
			},
		},
	},
};

static const struct go7007_usb_board board_matrix_revolution = {
	.flags		= GO7007_USB_EZUSB,
	.main_info	= {
		.flags		 = GO7007_BOARD_HAS_AUDIO |
					GO7007_BOARD_USE_ONBOARD_I2C,
		.audio_flags	 = GO7007_AUDIO_I2S_MODE_1 |
					GO7007_AUDIO_I2S_MASTER |
					GO7007_AUDIO_WORD_16,
		.audio_rate	 = 48000,
		.audio_bclk_div	 = 8,
		.audio_main_div	 = 2,
		.hpi_buffer_cap  = 7,
		.sensor_flags	 = GO7007_SENSOR_656 |
					GO7007_SENSOR_TV |
					GO7007_SENSOR_VBI,
		.num_i2c_devs	 = 1,
		.i2c_devs	 = {
			{
				.type	= "tw9903",
				.is_video = 1,
				.addr	= 0x44,
			},
		},
		.num_inputs	 = 2,
		.inputs		 = {
			{
				.video_input	= 2,
				.name		= "Composite",
			},
			{
				.video_input	= 8,
				.name		= "S-Video",
			},
		},
	},
};

static const struct go7007_usb_board board_lifeview_lr192 = {
	.flags		= GO7007_USB_EZUSB,
	.main_info	= {
		.flags		 = GO7007_BOARD_HAS_AUDIO |
					GO7007_BOARD_USE_ONBOARD_I2C,
		.audio_flags	 = GO7007_AUDIO_I2S_MODE_1 |
					GO7007_AUDIO_WORD_16,
		.audio_rate	 = 48000,
		.audio_bclk_div	 = 8,
		.audio_main_div	 = 2,
		.hpi_buffer_cap  = 7,
		.sensor_flags	 = GO7007_SENSOR_656 |
					GO7007_SENSOR_VALID_ENABLE |
					GO7007_SENSOR_TV |
					GO7007_SENSOR_VBI |
					GO7007_SENSOR_SCALING,
		.num_i2c_devs	 = 0,
		.num_inputs	 = 1,
		.inputs		 = {
			{
				.video_input	= 0,
				.name		= "Composite",
			},
		},
	},
};

static const struct go7007_usb_board board_endura = {
	.flags		= 0,
	.main_info	= {
		.flags		 = 0,
		.audio_flags	 = GO7007_AUDIO_I2S_MODE_1 |
					GO7007_AUDIO_I2S_MASTER |
					GO7007_AUDIO_WORD_16,
		.audio_rate	 = 8000,
		.audio_bclk_div	 = 48,
		.audio_main_div	 = 8,
		.hpi_buffer_cap  = 0,
		.sensor_flags	 = GO7007_SENSOR_656 |
					GO7007_SENSOR_TV,
		.sensor_h_offset = 8,
		.num_i2c_devs	 = 0,
		.num_inputs	 = 1,
		.inputs		 = {
			{
				.name		= "Camera",
			},
		},
	},
};

static const struct go7007_usb_board board_adlink_mpg24 = {
	.flags		= 0,
	.main_info	= {
		.flags		 = GO7007_BOARD_USE_ONBOARD_I2C,
		.audio_flags	 = GO7007_AUDIO_I2S_MODE_1 |
					GO7007_AUDIO_I2S_MASTER |
					GO7007_AUDIO_WORD_16,
		.audio_rate	 = 48000,
		.audio_bclk_div	 = 8,
		.audio_main_div	 = 2,
		.hpi_buffer_cap  = 0,
		.sensor_flags	 = GO7007_SENSOR_656 |
					GO7007_SENSOR_TV |
					GO7007_SENSOR_VBI,
		.num_i2c_devs	 = 1,
		.i2c_devs	 = {
			{
				.type	= "tw2804",
				.addr	= 0x00, /* yes, really */
				.flags  = I2C_CLIENT_TEN,
				.is_video = 1,
			},
		},
		.num_inputs	 = 1,
		.inputs		 = {
			{
				.name		= "Composite",
			},
		},
	},
};

static const struct go7007_usb_board board_sensoray_2250 = {
	.flags		= GO7007_USB_EZUSB | GO7007_USB_EZUSB_I2C,
	.main_info	= {
		.audio_flags	 = GO7007_AUDIO_I2S_MODE_1 |
					GO7007_AUDIO_I2S_MASTER |
					GO7007_AUDIO_WORD_16,
		.flags		 = GO7007_BOARD_HAS_AUDIO,
		.audio_rate	 = 48000,
		.audio_bclk_div	 = 8,
		.audio_main_div	 = 2,
		.hpi_buffer_cap  = 7,
		.sensor_flags	 = GO7007_SENSOR_656 |
					GO7007_SENSOR_TV,
		.num_i2c_devs	 = 1,
		.i2c_devs	 = {
			{
				.type	= "s2250",
				.addr	= 0x43,
				.is_video = 1,
				.is_audio = 1,
			},
		},
		.num_inputs	 = 2,
		.inputs		 = {
			{
				.video_input	= 0,
				.name		= "Composite",
			},
			{
				.video_input	= 1,
				.name		= "S-Video",
			},
		},
		.num_aud_inputs	 = 3,
		.aud_inputs	 = {
			{
				.audio_input	= 0,
				.name		= "Line In",
			},
			{
				.audio_input	= 1,
				.name		= "Mic",
			},
			{
				.audio_input	= 2,
				.name		= "Mic Boost",
			},
		},
	},
};

static const struct go7007_usb_board board_ads_usbav_709 = {
	.flags		= GO7007_USB_EZUSB,
	.main_info	= {
		.flags		 = GO7007_BOARD_HAS_AUDIO |
					GO7007_BOARD_USE_ONBOARD_I2C,
		.audio_flags	 = GO7007_AUDIO_I2S_MODE_1 |
					GO7007_AUDIO_I2S_MASTER |
					GO7007_AUDIO_WORD_16,
		.audio_rate	 = 48000,
		.audio_bclk_div	 = 8,
		.audio_main_div	 = 2,
		.hpi_buffer_cap  = 7,
		.sensor_flags	 = GO7007_SENSOR_656 |
					GO7007_SENSOR_TV |
					GO7007_SENSOR_VBI,
		.num_i2c_devs	 = 1,
		.i2c_devs	 = {
			{
				.type	= "tw9906",
				.is_video = 1,
				.addr	= 0x44,
			},
		},
		.num_inputs	 = 2,
		.inputs		 = {
			{
				.video_input	= 0,
				.name		= "Composite",
			},
			{
				.video_input	= 10,
				.name		= "S-Video",
			},
		},
	},
};

static const struct usb_device_id go7007_usb_id_table[] = {
	{
		.match_flags	= USB_DEVICE_ID_MATCH_DEVICE_AND_VERSION |
					USB_DEVICE_ID_MATCH_INT_INFO,
		.idVendor	= 0x0eb1,  /* Vendor ID of WIS Technologies */
		.idProduct	= 0x7007,  /* Product ID of GO7007SB chip */
		.bcdDevice_lo	= 0x200,   /* Revision number of XMen */
		.bcdDevice_hi	= 0x200,
		.bInterfaceClass	= 255,
		.bInterfaceSubClass	= 0,
		.bInterfaceProtocol	= 255,
		.driver_info	= (kernel_ulong_t)GO7007_BOARDID_XMEN,
	},
	{
		.match_flags	= USB_DEVICE_ID_MATCH_DEVICE_AND_VERSION,
		.idVendor	= 0x0eb1,  /* Vendor ID of WIS Technologies */
		.idProduct	= 0x7007,  /* Product ID of GO7007SB chip */
		.bcdDevice_lo	= 0x202,   /* Revision number of Matrix II */
		.bcdDevice_hi	= 0x202,
		.driver_info	= (kernel_ulong_t)GO7007_BOARDID_MATRIX_II,
	},
	{
		.match_flags	= USB_DEVICE_ID_MATCH_DEVICE_AND_VERSION,
		.idVendor	= 0x0eb1,  /* Vendor ID of WIS Technologies */
		.idProduct	= 0x7007,  /* Product ID of GO7007SB chip */
		.bcdDevice_lo	= 0x204,   /* Revision number of Matrix */
		.bcdDevice_hi	= 0x204,   /*     Reloaded */
		.driver_info	= (kernel_ulong_t)GO7007_BOARDID_MATRIX_RELOAD,
	},
	{
		.match_flags	= USB_DEVICE_ID_MATCH_DEVICE_AND_VERSION |
					USB_DEVICE_ID_MATCH_INT_INFO,
		.idVendor	= 0x0eb1,  /* Vendor ID of WIS Technologies */
		.idProduct	= 0x7007,  /* Product ID of GO7007SB chip */
		.bcdDevice_lo	= 0x205,   /* Revision number of XMen-II */
		.bcdDevice_hi	= 0x205,
		.bInterfaceClass	= 255,
		.bInterfaceSubClass	= 0,
		.bInterfaceProtocol	= 255,
		.driver_info	= (kernel_ulong_t)GO7007_BOARDID_XMEN_II,
	},
	{
		.match_flags	= USB_DEVICE_ID_MATCH_DEVICE_AND_VERSION,
		.idVendor	= 0x0eb1,  /* Vendor ID of WIS Technologies */
		.idProduct	= 0x7007,  /* Product ID of GO7007SB chip */
		.bcdDevice_lo	= 0x208,   /* Revision number of Star Trek */
		.bcdDevice_hi	= 0x208,
		.driver_info	= (kernel_ulong_t)GO7007_BOARDID_STAR_TREK,
	},
	{
		.match_flags	= USB_DEVICE_ID_MATCH_DEVICE_AND_VERSION |
					USB_DEVICE_ID_MATCH_INT_INFO,
		.idVendor	= 0x0eb1,  /* Vendor ID of WIS Technologies */
		.idProduct	= 0x7007,  /* Product ID of GO7007SB chip */
		.bcdDevice_lo	= 0x209,   /* Revision number of XMen-III */
		.bcdDevice_hi	= 0x209,
		.bInterfaceClass	= 255,
		.bInterfaceSubClass	= 0,
		.bInterfaceProtocol	= 255,
		.driver_info	= (kernel_ulong_t)GO7007_BOARDID_XMEN_III,
	},
	{
		.match_flags	= USB_DEVICE_ID_MATCH_DEVICE_AND_VERSION,
		.idVendor	= 0x0eb1,  /* Vendor ID of WIS Technologies */
		.idProduct	= 0x7007,  /* Product ID of GO7007SB chip */
		.bcdDevice_lo	= 0x210,   /* Revision number of Matrix */
		.bcdDevice_hi	= 0x210,   /*     Revolution */
		.driver_info	= (kernel_ulong_t)GO7007_BOARDID_MATRIX_REV,
	},
	{
		.match_flags	= USB_DEVICE_ID_MATCH_DEVICE_AND_VERSION,
		.idVendor	= 0x093b,  /* Vendor ID of Plextor */
		.idProduct	= 0xa102,  /* Product ID of M402U */
		.bcdDevice_lo	= 0x1,	   /* revision number of Blueberry */
		.bcdDevice_hi	= 0x1,
		.driver_info	= (kernel_ulong_t)GO7007_BOARDID_PX_M402U,
	},
	{
		.match_flags	= USB_DEVICE_ID_MATCH_DEVICE_AND_VERSION,
		.idVendor	= 0x093b,  /* Vendor ID of Plextor */
		.idProduct	= 0xa104,  /* Product ID of TV402U */
		.bcdDevice_lo	= 0x1,
		.bcdDevice_hi	= 0x1,
		.driver_info	= (kernel_ulong_t)GO7007_BOARDID_PX_TV402U,
	},
	{
		.match_flags	= USB_DEVICE_ID_MATCH_DEVICE_AND_VERSION,
		.idVendor	= 0x10fd,  /* Vendor ID of Anubis Electronics */
		.idProduct	= 0xde00,  /* Product ID of Lifeview LR192 */
		.bcdDevice_lo	= 0x1,
		.bcdDevice_hi	= 0x1,
		.driver_info	= (kernel_ulong_t)GO7007_BOARDID_LIFEVIEW_LR192,
	},
	{
		.match_flags	= USB_DEVICE_ID_MATCH_DEVICE_AND_VERSION,
		.idVendor	= 0x1943,  /* Vendor ID Sensoray */
		.idProduct	= 0x2250,  /* Product ID of 2250/2251 */
		.bcdDevice_lo	= 0x1,
		.bcdDevice_hi	= 0x1,
		.driver_info	= (kernel_ulong_t)GO7007_BOARDID_SENSORAY_2250,
	},
	{
		.match_flags	= USB_DEVICE_ID_MATCH_DEVICE_AND_VERSION,
		.idVendor	= 0x06e1,  /* Vendor ID of ADS Technologies */
		.idProduct	= 0x0709,  /* Product ID of DVD Xpress DX2 */
		.bcdDevice_lo	= 0x204,
		.bcdDevice_hi	= 0x204,
		.driver_info	= (kernel_ulong_t)GO7007_BOARDID_ADS_USBAV_709,
	},
	{ }					/* Terminating entry */
};

MODULE_DEVICE_TABLE(usb, go7007_usb_id_table);

/********************* Driver for EZ-USB HPI interface *********************/

static int go7007_usb_vendor_request(struct go7007 *go, int request,
		int value, int index, void *transfer_buffer, int length, int in)
{
	struct go7007_usb *usb = go->hpi_context;
	int timeout = 5000;

	if (in) {
		return usb_control_msg(usb->usbdev,
				usb_rcvctrlpipe(usb->usbdev, 0), request,
				USB_TYPE_VENDOR | USB_RECIP_DEVICE | USB_DIR_IN,
				value, index, transfer_buffer, length, timeout);
	} else {
		return usb_control_msg(usb->usbdev,
				usb_sndctrlpipe(usb->usbdev, 0), request,
				USB_TYPE_VENDOR | USB_RECIP_DEVICE,
				value, index, transfer_buffer, length, timeout);
	}
}

static int go7007_usb_interface_reset(struct go7007 *go)
{
	struct go7007_usb *usb = go->hpi_context;
	u16 intr_val, intr_data;

	if (go->status == STATUS_SHUTDOWN)
		return -1;
	/* Reset encoder */
	if (go7007_write_interrupt(go, 0x0001, 0x0001) < 0)
		return -1;
	msleep(100);

	if (usb->board->flags & GO7007_USB_EZUSB) {
		/* Reset buffer in EZ-USB */
		pr_debug("resetting EZ-USB buffers\n");
		if (go7007_usb_vendor_request(go, 0x10, 0, 0, NULL, 0, 0) < 0 ||
		    go7007_usb_vendor_request(go, 0x10, 0, 0, NULL, 0, 0) < 0)
			return -1;

		/* Reset encoder again */
		if (go7007_write_interrupt(go, 0x0001, 0x0001) < 0)
			return -1;
		msleep(100);
	}

	/* Wait for an interrupt to indicate successful hardware reset */
	if (go7007_read_interrupt(go, &intr_val, &intr_data) < 0 ||
			(intr_val & ~0x1) != 0x55aa) {
		dev_err(go->dev, "unable to reset the USB interface\n");
		return -1;
	}
	return 0;
}

static int go7007_usb_ezusb_write_interrupt(struct go7007 *go,
						int addr, int data)
{
	struct go7007_usb *usb = go->hpi_context;
	int i, r;
	u16 status_reg = 0;
	int timeout = 500;

	pr_debug("WriteInterrupt: %04x %04x\n", addr, data);

	for (i = 0; i < 100; ++i) {
		r = usb_control_msg(usb->usbdev,
				usb_rcvctrlpipe(usb->usbdev, 0), 0x14,
				USB_TYPE_VENDOR | USB_RECIP_DEVICE | USB_DIR_IN,
				0, HPI_STATUS_ADDR, go->usb_buf,
				sizeof(status_reg), timeout);
		if (r < 0)
			break;
		status_reg = le16_to_cpu(*((u16 *)go->usb_buf));
		if (!(status_reg & 0x0010))
			break;
		msleep(10);
	}
	if (r < 0)
		goto write_int_error;
	if (i == 100) {
		dev_err(go->dev, "device is hung, status reg = 0x%04x\n", status_reg);
		return -1;
	}
	r = usb_control_msg(usb->usbdev, usb_sndctrlpipe(usb->usbdev, 0), 0x12,
			USB_TYPE_VENDOR | USB_RECIP_DEVICE, data,
			INT_PARAM_ADDR, NULL, 0, timeout);
	if (r < 0)
		goto write_int_error;
	r = usb_control_msg(usb->usbdev, usb_sndctrlpipe(usb->usbdev, 0),
			0x12, USB_TYPE_VENDOR | USB_RECIP_DEVICE, addr,
			INT_INDEX_ADDR, NULL, 0, timeout);
	if (r < 0)
		goto write_int_error;
	return 0;

write_int_error:
	dev_err(go->dev, "error in WriteInterrupt: %d\n", r);
	return r;
}

static int go7007_usb_onboard_write_interrupt(struct go7007 *go,
						int addr, int data)
{
	struct go7007_usb *usb = go->hpi_context;
	int r;
	int timeout = 500;

	pr_debug("WriteInterrupt: %04x %04x\n", addr, data);

	go->usb_buf[0] = data & 0xff;
	go->usb_buf[1] = data >> 8;
	go->usb_buf[2] = addr & 0xff;
	go->usb_buf[3] = addr >> 8;
	go->usb_buf[4] = go->usb_buf[5] = go->usb_buf[6] = go->usb_buf[7] = 0;
	r = usb_control_msg(usb->usbdev, usb_sndctrlpipe(usb->usbdev, 2), 0x00,
			USB_TYPE_VENDOR | USB_RECIP_ENDPOINT, 0x55aa,
			0xf0f0, go->usb_buf, 8, timeout);
	if (r < 0) {
		dev_err(go->dev, "error in WriteInterrupt: %d\n", r);
		return r;
	}
	return 0;
}

static void go7007_usb_readinterrupt_complete(struct urb *urb)
{
	struct go7007 *go = (struct go7007 *)urb->context;
	u16 *regs = (u16 *)urb->transfer_buffer;
	int status = urb->status;

	if (status) {
		if (status != -ESHUTDOWN &&
				go->status != STATUS_SHUTDOWN) {
			dev_err(go->dev, "error in read interrupt: %d\n", urb->status);
		} else {
			wake_up(&go->interrupt_waitq);
			return;
		}
	} else if (urb->actual_length != urb->transfer_buffer_length) {
		dev_err(go->dev, "short read in interrupt pipe!\n");
	} else {
		go->interrupt_available = 1;
		go->interrupt_data = __le16_to_cpu(regs[0]);
		go->interrupt_value = __le16_to_cpu(regs[1]);
		pr_debug("ReadInterrupt: %04x %04x\n",
				go->interrupt_value, go->interrupt_data);
	}

	wake_up(&go->interrupt_waitq);
}

static int go7007_usb_read_interrupt(struct go7007 *go)
{
	struct go7007_usb *usb = go->hpi_context;
	int r;

	r = usb_submit_urb(usb->intr_urb, GFP_KERNEL);
	if (r < 0) {
		dev_err(go->dev, "unable to submit interrupt urb: %d\n", r);
		return r;
	}
	return 0;
}

static void go7007_usb_read_video_pipe_complete(struct urb *urb)
{
	struct go7007 *go = (struct go7007 *)urb->context;
	int r, status = urb->status;

	if (!vb2_is_streaming(&go->vidq)) {
		wake_up_interruptible(&go->frame_waitq);
		return;
	}
	if (status) {
		dev_err(go->dev, "error in video pipe: %d\n", status);
		return;
	}
	if (urb->actual_length != urb->transfer_buffer_length) {
		dev_err(go->dev, "short read in video pipe!\n");
		return;
	}
	go7007_parse_video_stream(go, urb->transfer_buffer, urb->actual_length);
	r = usb_submit_urb(urb, GFP_ATOMIC);
	if (r < 0)
		dev_err(go->dev, "error in video pipe: %d\n", r);
}

static void go7007_usb_read_audio_pipe_complete(struct urb *urb)
{
	struct go7007 *go = (struct go7007 *)urb->context;
	int r, status = urb->status;

	if (!vb2_is_streaming(&go->vidq))
		return;
	if (status) {
		dev_err(go->dev, "error in audio pipe: %d\n",
			status);
		return;
	}
	if (urb->actual_length != urb->transfer_buffer_length) {
		dev_err(go->dev, "short read in audio pipe!\n");
		return;
	}
	if (go->audio_deliver != NULL)
		go->audio_deliver(go, urb->transfer_buffer, urb->actual_length);
	r = usb_submit_urb(urb, GFP_ATOMIC);
	if (r < 0)
		dev_err(go->dev, "error in audio pipe: %d\n", r);
}

static int go7007_usb_stream_start(struct go7007 *go)
{
	struct go7007_usb *usb = go->hpi_context;
	int i, r;

	for (i = 0; i < 8; ++i) {
		r = usb_submit_urb(usb->video_urbs[i], GFP_KERNEL);
		if (r < 0) {
			dev_err(go->dev, "error submitting video urb %d: %d\n", i, r);
			goto video_submit_failed;
		}
	}
	if (!go->audio_enabled)
		return 0;

	for (i = 0; i < 8; ++i) {
		r = usb_submit_urb(usb->audio_urbs[i], GFP_KERNEL);
		if (r < 0) {
			dev_err(go->dev, "error submitting audio urb %d: %d\n", i, r);
			goto audio_submit_failed;
		}
	}
	return 0;

audio_submit_failed:
	for (i = 0; i < 7; ++i)
		usb_kill_urb(usb->audio_urbs[i]);
video_submit_failed:
	for (i = 0; i < 8; ++i)
		usb_kill_urb(usb->video_urbs[i]);
	return -1;
}

static int go7007_usb_stream_stop(struct go7007 *go)
{
	struct go7007_usb *usb = go->hpi_context;
	int i;

	if (go->status == STATUS_SHUTDOWN)
		return 0;
	for (i = 0; i < 8; ++i)
		usb_kill_urb(usb->video_urbs[i]);
	if (go->audio_enabled)
		for (i = 0; i < 8; ++i)
			usb_kill_urb(usb->audio_urbs[i]);
	return 0;
}

static int go7007_usb_send_firmware(struct go7007 *go, u8 *data, int len)
{
	struct go7007_usb *usb = go->hpi_context;
	int transferred, pipe;
	int timeout = 500;

	pr_debug("DownloadBuffer sending %d bytes\n", len);

	if (usb->board->flags & GO7007_USB_EZUSB)
		pipe = usb_sndbulkpipe(usb->usbdev, 2);
	else
		pipe = usb_sndbulkpipe(usb->usbdev, 3);

	return usb_bulk_msg(usb->usbdev, pipe, data, len,
					&transferred, timeout);
}

static void go7007_usb_release(struct go7007 *go)
{
	struct go7007_usb *usb = go->hpi_context;
	struct urb *vurb, *aurb;
	int i;

	if (usb->intr_urb) {
		usb_kill_urb(usb->intr_urb);
		kfree(usb->intr_urb->transfer_buffer);
		usb_free_urb(usb->intr_urb);
	}

	/* Free USB-related structs */
	for (i = 0; i < 8; ++i) {
		vurb = usb->video_urbs[i];
		if (vurb) {
			usb_kill_urb(vurb);
			kfree(vurb->transfer_buffer);
			usb_free_urb(vurb);
		}
		aurb = usb->audio_urbs[i];
		if (aurb) {
			usb_kill_urb(aurb);
			kfree(aurb->transfer_buffer);
			usb_free_urb(aurb);
		}
	}

	kfree(go->hpi_context);
}

static struct go7007_hpi_ops go7007_usb_ezusb_hpi_ops = {
	.interface_reset	= go7007_usb_interface_reset,
	.write_interrupt	= go7007_usb_ezusb_write_interrupt,
	.read_interrupt		= go7007_usb_read_interrupt,
	.stream_start		= go7007_usb_stream_start,
	.stream_stop		= go7007_usb_stream_stop,
	.send_firmware		= go7007_usb_send_firmware,
	.release		= go7007_usb_release,
};

static struct go7007_hpi_ops go7007_usb_onboard_hpi_ops = {
	.interface_reset	= go7007_usb_interface_reset,
	.write_interrupt	= go7007_usb_onboard_write_interrupt,
	.read_interrupt		= go7007_usb_read_interrupt,
	.stream_start		= go7007_usb_stream_start,
	.stream_stop		= go7007_usb_stream_stop,
	.send_firmware		= go7007_usb_send_firmware,
	.release		= go7007_usb_release,
};

/********************* Driver for EZ-USB I2C adapter *********************/

static int go7007_usb_i2c_master_xfer(struct i2c_adapter *adapter,
					struct i2c_msg msgs[], int num)
{
	struct go7007 *go = i2c_get_adapdata(adapter);
	struct go7007_usb *usb = go->hpi_context;
	u8 *buf = go->usb_buf;
	int buf_len, i;
	int ret = -EIO;

	if (go->status == STATUS_SHUTDOWN)
		return -ENODEV;

	mutex_lock(&usb->i2c_lock);

	for (i = 0; i < num; ++i) {
		/* The hardware command is "write some bytes then read some
		 * bytes", so we try to coalesce a write followed by a read
		 * into a single USB transaction */
		if (i + 1 < num && msgs[i].addr == msgs[i + 1].addr &&
				!(msgs[i].flags & I2C_M_RD) &&
				(msgs[i + 1].flags & I2C_M_RD)) {
#ifdef GO7007_I2C_DEBUG
			pr_debug("i2c write/read %d/%d bytes on %02x\n",
				msgs[i].len, msgs[i + 1].len, msgs[i].addr);
#endif
			buf[0] = 0x01;
			buf[1] = msgs[i].len + 1;
			buf[2] = msgs[i].addr << 1;
			memcpy(&buf[3], msgs[i].buf, msgs[i].len);
			buf_len = msgs[i].len + 3;
			buf[buf_len++] = msgs[++i].len;
		} else if (msgs[i].flags & I2C_M_RD) {
#ifdef GO7007_I2C_DEBUG
			pr_debug("i2c read %d bytes on %02x\n",
					msgs[i].len, msgs[i].addr);
#endif
			buf[0] = 0x01;
			buf[1] = 1;
			buf[2] = msgs[i].addr << 1;
			buf[3] = msgs[i].len;
			buf_len = 4;
		} else {
#ifdef GO7007_I2C_DEBUG
			pr_debug("i2c write %d bytes on %02x\n",
					msgs[i].len, msgs[i].addr);
#endif
			buf[0] = 0x00;
			buf[1] = msgs[i].len + 1;
			buf[2] = msgs[i].addr << 1;
			memcpy(&buf[3], msgs[i].buf, msgs[i].len);
			buf_len = msgs[i].len + 3;
			buf[buf_len++] = 0;
		}
		if (go7007_usb_vendor_request(go, 0x24, 0, 0,
						buf, buf_len, 0) < 0)
			goto i2c_done;
		if (msgs[i].flags & I2C_M_RD) {
			memset(buf, 0, msgs[i].len + 1);
			if (go7007_usb_vendor_request(go, 0x25, 0, 0, buf,
						msgs[i].len + 1, 1) < 0)
				goto i2c_done;
			memcpy(msgs[i].buf, buf + 1, msgs[i].len);
		}
	}
	ret = num;

i2c_done:
	mutex_unlock(&usb->i2c_lock);
	return ret;
}

static u32 go7007_usb_functionality(struct i2c_adapter *adapter)
{
	/* No errors are reported by the hardware, so we don't bother
	 * supporting quick writes to avoid confusing probing */
	return (I2C_FUNC_SMBUS_EMUL) & ~I2C_FUNC_SMBUS_QUICK;
}

static struct i2c_algorithm go7007_usb_algo = {
	.master_xfer	= go7007_usb_i2c_master_xfer,
	.functionality	= go7007_usb_functionality,
};

static struct i2c_adapter go7007_usb_adap_templ = {
	.owner			= THIS_MODULE,
	.name			= "WIS GO7007SB EZ-USB",
	.algo			= &go7007_usb_algo,
};

/********************* USB add/remove functions *********************/

static int go7007_usb_probe(struct usb_interface *intf,
		const struct usb_device_id *id)
{
	struct go7007 *go;
	struct go7007_usb *usb;
	const struct go7007_usb_board *board;
	struct usb_device *usbdev = interface_to_usbdev(intf);
	unsigned num_i2c_devs;
	char *name;
	int video_pipe, i, v_urb_len;

	pr_debug("probing new GO7007 USB board\n");

	switch (id->driver_info) {
	case GO7007_BOARDID_MATRIX_II:
		name = "WIS Matrix II or compatible";
		board = &board_matrix_ii;
		break;
	case GO7007_BOARDID_MATRIX_RELOAD:
		name = "WIS Matrix Reloaded or compatible";
		board = &board_matrix_reload;
		break;
	case GO7007_BOARDID_MATRIX_REV:
		name = "WIS Matrix Revolution or compatible";
		board = &board_matrix_revolution;
		break;
	case GO7007_BOARDID_STAR_TREK:
		name = "WIS Star Trek or compatible";
		board = &board_star_trek;
		break;
	case GO7007_BOARDID_XMEN:
		name = "WIS XMen or compatible";
		board = &board_xmen;
		break;
	case GO7007_BOARDID_XMEN_II:
		name = "WIS XMen II or compatible";
		board = &board_xmen;
		break;
	case GO7007_BOARDID_XMEN_III:
		name = "WIS XMen III or compatible";
		board = &board_xmen;
		break;
	case GO7007_BOARDID_PX_M402U:
		name = "Plextor PX-M402U";
		board = &board_matrix_ii;
		break;
	case GO7007_BOARDID_PX_TV402U:
		name = "Plextor PX-TV402U (unknown tuner)";
		board = &board_px_tv402u;
		break;
	case GO7007_BOARDID_LIFEVIEW_LR192:
		dev_err(&intf->dev, "The Lifeview TV Walker Ultra is not supported. Sorry!\n");
		return -ENODEV;
		name = "Lifeview TV Walker Ultra";
		board = &board_lifeview_lr192;
		break;
	case GO7007_BOARDID_SENSORAY_2250:
		dev_info(&intf->dev, "Sensoray 2250 found\n");
		name = "Sensoray 2250/2251";
		board = &board_sensoray_2250;
		break;
	case GO7007_BOARDID_ADS_USBAV_709:
		name = "ADS Tech DVD Xpress DX2";
		board = &board_ads_usbav_709;
		break;
	default:
		dev_err(&intf->dev, "unknown board ID %d!\n",
				(unsigned int)id->driver_info);
		return -ENODEV;
	}

	go = go7007_alloc(&board->main_info, &intf->dev);
	if (go == NULL)
		return -ENOMEM;

	usb = kzalloc(sizeof(struct go7007_usb), GFP_KERNEL);
	if (usb == NULL) {
		kfree(go);
		return -ENOMEM;
	}

	usb->board = board;
	usb->usbdev = usbdev;
	usb_make_path(usbdev, go->bus_info, sizeof(go->bus_info));
	go->board_id = id->driver_info;
	strncpy(go->name, name, sizeof(go->name));
	if (board->flags & GO7007_USB_EZUSB)
		go->hpi_ops = &go7007_usb_ezusb_hpi_ops;
	else
		go->hpi_ops = &go7007_usb_onboard_hpi_ops;
	go->hpi_context = usb;

	/* Allocate the URB and buffer for receiving incoming interrupts */
	usb->intr_urb = usb_alloc_urb(0, GFP_KERNEL);
	if (usb->intr_urb == NULL)
		goto allocfail;
	usb->intr_urb->transfer_buffer = kmalloc(2*sizeof(u16), GFP_KERNEL);
	if (usb->intr_urb->transfer_buffer == NULL)
		goto allocfail;

	if (go->board_id == GO7007_BOARDID_SENSORAY_2250)
		usb_fill_bulk_urb(usb->intr_urb, usb->usbdev,
			usb_rcvbulkpipe(usb->usbdev, 4),
			usb->intr_urb->transfer_buffer, 2*sizeof(u16),
			go7007_usb_readinterrupt_complete, go);
	else
		usb_fill_int_urb(usb->intr_urb, usb->usbdev,
			usb_rcvintpipe(usb->usbdev, 4),
			usb->intr_urb->transfer_buffer, 2*sizeof(u16),
			go7007_usb_readinterrupt_complete, go, 8);
	usb_set_intfdata(intf, &go->v4l2_dev);

	/* Boot the GO7007 */
	if (go7007_boot_encoder(go, go->board_info->flags &
					GO7007_BOARD_USE_ONBOARD_I2C) < 0)
		goto allocfail;

	/* Register the EZ-USB I2C adapter, if we're using it */
	if (board->flags & GO7007_USB_EZUSB_I2C) {
		memcpy(&go->i2c_adapter, &go7007_usb_adap_templ,
				sizeof(go7007_usb_adap_templ));
		mutex_init(&usb->i2c_lock);
		go->i2c_adapter.dev.parent = go->dev;
		i2c_set_adapdata(&go->i2c_adapter, go);
		if (i2c_add_adapter(&go->i2c_adapter) < 0) {
			dev_err(go->dev, "error: i2c_add_adapter failed\n");
			goto allocfail;
		}
		go->i2c_adapter_online = 1;
	}

	/* Pelco and Adlink reused the XMen and XMen-III vendor and product
	 * IDs for their own incompatible designs.  We can detect XMen boards
	 * by probing the sensor, but there is no way to probe the sensors on
	 * the Pelco and Adlink designs so we default to the Adlink.  If it
	 * is actually a Pelco, the user must set the assume_endura module
	 * parameter. */
	if ((go->board_id == GO7007_BOARDID_XMEN ||
				go->board_id == GO7007_BOARDID_XMEN_III) &&
			go->i2c_adapter_online) {
		union i2c_smbus_data data;

		/* Check to see if register 0x0A is 0x76 */
		i2c_smbus_xfer(&go->i2c_adapter, 0x21, I2C_CLIENT_SCCB,
			I2C_SMBUS_READ, 0x0A, I2C_SMBUS_BYTE_DATA, &data);
		if (data.byte != 0x76) {
			if (assume_endura) {
				go->board_id = GO7007_BOARDID_ENDURA;
				usb->board = board = &board_endura;
				go->board_info = &board->main_info;
				strncpy(go->name, "Pelco Endura",
					sizeof(go->name));
			} else {
				u16 channel;

				/* read channel number from GPIO[1:0] */
				go7007_read_addr(go, 0x3c81, &channel);
				channel &= 0x3;
				go->board_id = GO7007_BOARDID_ADLINK_MPG24;
				usb->board = board = &board_adlink_mpg24;
				go->board_info = &board->main_info;
				go->channel_number = channel;
				snprintf(go->name, sizeof(go->name),
					"Adlink PCI-MPG24, channel #%d",
					channel);
			}
			go7007_update_board(go);
		}
	}

	num_i2c_devs = go->board_info->num_i2c_devs;

	/* Probe the tuner model on the TV402U */
	if (go->board_id == GO7007_BOARDID_PX_TV402U) {
		/* Board strapping indicates tuner model */
		if (go7007_usb_vendor_request(go, 0x41, 0, 0, go->usb_buf, 3,
					1) < 0) {
			dev_err(go->dev, "GPIO read failed!\n");
			goto allocfail;
		}
		switch (go->usb_buf[0] >> 6) {
		case 1:
			go->tuner_type = TUNER_SONY_BTF_PG472Z;
			go->std = V4L2_STD_PAL;
			strncpy(go->name, "Plextor PX-TV402U-EU",
					sizeof(go->name));
			break;
		case 2:
			go->tuner_type = TUNER_SONY_BTF_PK467Z;
			go->std = V4L2_STD_NTSC_M_JP;
			num_i2c_devs -= 2;
			strncpy(go->name, "Plextor PX-TV402U-JP",
					sizeof(go->name));
			break;
		case 3:
			go->tuner_type = TUNER_SONY_BTF_PB463Z;
			num_i2c_devs -= 2;
			strncpy(go->name, "Plextor PX-TV402U-NA",
					sizeof(go->name));
			break;
		default:
			pr_debug("unable to detect tuner type!\n");
			break;
		}
		/* Configure tuner mode selection inputs connected
		 * to the EZ-USB GPIO output pins */
		if (go7007_usb_vendor_request(go, 0x40, 0x7f02, 0,
					NULL, 0, 0) < 0) {
			dev_err(go->dev, "GPIO write failed!\n");
			goto allocfail;
		}
	}

	/* Print a nasty message if the user attempts to use a USB2.0 device in
	 * a USB1.1 port.  There will be silent corruption of the stream. */
	if ((board->flags & GO7007_USB_EZUSB) &&
			usbdev->speed != USB_SPEED_HIGH)
		dev_err(go->dev, "*** WARNING ***  This device must be connected to a USB 2.0 port! Attempting to capture video through a USB 1.1 port will result in stream corruption, even at low bitrates!\n");

	/* Allocate the URBs and buffers for receiving the video stream */
	if (board->flags & GO7007_USB_EZUSB) {
		v_urb_len = 1024;
		video_pipe = usb_rcvbulkpipe(usb->usbdev, 6);
	} else {
		v_urb_len = 512;
		video_pipe = usb_rcvbulkpipe(usb->usbdev, 1);
	}
	for (i = 0; i < 8; ++i) {
		usb->video_urbs[i] = usb_alloc_urb(0, GFP_KERNEL);
		if (usb->video_urbs[i] == NULL)
			goto allocfail;
		usb->video_urbs[i]->transfer_buffer =
						kmalloc(v_urb_len, GFP_KERNEL);
		if (usb->video_urbs[i]->transfer_buffer == NULL)
			goto allocfail;
		usb_fill_bulk_urb(usb->video_urbs[i], usb->usbdev, video_pipe,
				usb->video_urbs[i]->transfer_buffer, v_urb_len,
				go7007_usb_read_video_pipe_complete, go);
	}

	/* Allocate the URBs and buffers for receiving the audio stream */
	if ((board->flags & GO7007_USB_EZUSB) &&
	    (board->flags & GO7007_BOARD_HAS_AUDIO)) {
		for (i = 0; i < 8; ++i) {
			usb->audio_urbs[i] = usb_alloc_urb(0, GFP_KERNEL);
			if (usb->audio_urbs[i] == NULL)
				goto allocfail;
			usb->audio_urbs[i]->transfer_buffer = kmalloc(4096,
								GFP_KERNEL);
			if (usb->audio_urbs[i]->transfer_buffer == NULL)
				goto allocfail;
			usb_fill_bulk_urb(usb->audio_urbs[i], usb->usbdev,
				usb_rcvbulkpipe(usb->usbdev, 8),
				usb->audio_urbs[i]->transfer_buffer, 4096,
				go7007_usb_read_audio_pipe_complete, go);
		}
	}

	/* Do any final GO7007 initialization, then register the
	 * V4L2 and ALSA interfaces */
	if (go7007_register_encoder(go, num_i2c_devs) < 0)
		goto allocfail;

	go->status = STATUS_ONLINE;
	return 0;

allocfail:
	go7007_usb_release(go);
	kfree(go);
	return -ENOMEM;
}

static void go7007_usb_disconnect(struct usb_interface *intf)
{
	struct go7007 *go = to_go7007(usb_get_intfdata(intf));

	mutex_lock(&go->queue_lock);
	mutex_lock(&go->serialize_lock);

	if (go->audio_enabled)
		go7007_snd_remove(go);

	go->status = STATUS_SHUTDOWN;
	v4l2_device_disconnect(&go->v4l2_dev);
	video_unregister_device(&go->vdev);
	mutex_unlock(&go->serialize_lock);
	mutex_unlock(&go->queue_lock);

	v4l2_device_put(&go->v4l2_dev);
}

static struct usb_driver go7007_usb_driver = {
	.name		= "go7007",
	.probe		= go7007_usb_probe,
	.disconnect	= go7007_usb_disconnect,
	.id_table	= go7007_usb_id_table,
};

module_usb_driver(go7007_usb_driver);
MODULE_LICENSE("GPL v2");

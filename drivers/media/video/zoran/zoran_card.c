/*
 * Zoran zr36057/zr36067 PCI controller driver, for the
 * Pinnacle/Miro DC10/DC10+/DC30/DC30+, Iomega Buz, Linux
 * Media Labs LML33/LML33R10.
 *
 * This part handles card-specific data and detection
 *
 * Copyright (C) 2000 Serguei Miridonov <mirsev@cicese.mx>
 *
 * Currently maintained by:
 *   Ronald Bultje    <rbultje@ronald.bitfreak.net>
 *   Laurent Pinchart <laurent.pinchart@skynet.be>
 *   Mailinglist      <mjpeg-users@lists.sf.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/delay.h>

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/vmalloc.h>
#include <linux/slab.h>

#include <linux/proc_fs.h>
#include <linux/i2c.h>
#include <linux/i2c-algo-bit.h>
#include <linux/videodev2.h>
#include <linux/spinlock.h>
#include <linux/sem.h>
#include <linux/kmod.h>
#include <linux/wait.h>

#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/mutex.h>
#include <linux/io.h>
#include <media/v4l2-common.h>
#include <media/bt819.h>

#include "videocodec.h"
#include "zoran.h"
#include "zoran_card.h"
#include "zoran_device.h"
#include "zoran_procfs.h"

extern const struct zoran_format zoran_formats[];

static int card[BUZ_MAX] = { [0 ... (BUZ_MAX-1)] = -1 };
module_param_array(card, int, NULL, 0444);
MODULE_PARM_DESC(card, "Card type");

/*
   The video mem address of the video card.
   The driver has a little database for some videocards
   to determine it from there. If your video card is not in there
   you have either to give it to the driver as a parameter
   or set in in a VIDIOCSFBUF ioctl
 */

static unsigned long vidmem;	/* default = 0 - Video memory base address */
module_param(vidmem, ulong, 0444);
MODULE_PARM_DESC(vidmem, "Default video memory base address");

/*
   Default input and video norm at startup of the driver.
*/

static unsigned int default_input;	/* default 0 = Composite, 1 = S-Video */
module_param(default_input, uint, 0444);
MODULE_PARM_DESC(default_input,
		 "Default input (0=Composite, 1=S-Video, 2=Internal)");

static int default_mux = 1;	/* 6 Eyes input selection */
module_param(default_mux, int, 0644);
MODULE_PARM_DESC(default_mux,
		 "Default 6 Eyes mux setting (Input selection)");

static int default_norm;	/* default 0 = PAL, 1 = NTSC 2 = SECAM */
module_param(default_norm, int, 0444);
MODULE_PARM_DESC(default_norm, "Default norm (0=PAL, 1=NTSC, 2=SECAM)");

/* /dev/videoN, -1 for autodetect */
static int video_nr[BUZ_MAX] = { [0 ... (BUZ_MAX-1)] = -1 };
module_param_array(video_nr, int, NULL, 0444);
MODULE_PARM_DESC(video_nr, "Video device number (-1=Auto)");

int v4l_nbufs = 4;
int v4l_bufsize = 864;		/* Everybody should be able to work with this setting */
module_param(v4l_nbufs, int, 0644);
MODULE_PARM_DESC(v4l_nbufs, "Maximum number of V4L buffers to use");
module_param(v4l_bufsize, int, 0644);
MODULE_PARM_DESC(v4l_bufsize, "Maximum size per V4L buffer (in kB)");

int jpg_nbufs = 32;
int jpg_bufsize = 512;		/* max size for 100% quality full-PAL frame */
module_param(jpg_nbufs, int, 0644);
MODULE_PARM_DESC(jpg_nbufs, "Maximum number of JPG buffers to use");
module_param(jpg_bufsize, int, 0644);
MODULE_PARM_DESC(jpg_bufsize, "Maximum size per JPG buffer (in kB)");

int pass_through = 0;		/* 1=Pass through TV signal when device is not used */
				/* 0=Show color bar when device is not used (LML33: only if lml33dpath=1) */
module_param(pass_through, int, 0644);
MODULE_PARM_DESC(pass_through,
		 "Pass TV signal through to TV-out when idling");

int zr36067_debug = 1;
module_param_named(debug, zr36067_debug, int, 0644);
MODULE_PARM_DESC(debug, "Debug level (0-5)");

MODULE_DESCRIPTION("Zoran-36057/36067 JPEG codec driver");
MODULE_AUTHOR("Serguei Miridonov");
MODULE_LICENSE("GPL");

#define ZR_DEVICE(subven, subdev, data)	{ \
	.vendor = PCI_VENDOR_ID_ZORAN, .device = PCI_DEVICE_ID_ZORAN_36057, \
	.subvendor = (subven), .subdevice = (subdev), .driver_data = (data) }

static struct pci_device_id zr36067_pci_tbl[] = {
	ZR_DEVICE(PCI_VENDOR_ID_MIRO, PCI_DEVICE_ID_MIRO_DC10PLUS, DC10plus),
	ZR_DEVICE(PCI_VENDOR_ID_MIRO, PCI_DEVICE_ID_MIRO_DC30PLUS, DC30plus),
	ZR_DEVICE(PCI_VENDOR_ID_ELECTRONICDESIGNGMBH, PCI_DEVICE_ID_LML_33R10, LML33R10),
	ZR_DEVICE(PCI_VENDOR_ID_IOMEGA, PCI_DEVICE_ID_IOMEGA_BUZ, BUZ),
	ZR_DEVICE(PCI_ANY_ID, PCI_ANY_ID, NUM_CARDS),
	{0}
};
MODULE_DEVICE_TABLE(pci, zr36067_pci_tbl);

static unsigned int zoran_num;		/* number of cards found */

/* videocodec bus functions ZR36060 */
static u32
zr36060_read (struct videocodec *codec,
	      u16                reg)
{
	struct zoran *zr = (struct zoran *) codec->master_data->data;
	__u32 data;

	if (post_office_wait(zr)
	    || post_office_write(zr, 0, 1, reg >> 8)
	    || post_office_write(zr, 0, 2, reg & 0xff)) {
		return -1;
	}

	data = post_office_read(zr, 0, 3) & 0xff;
	return data;
}

static void
zr36060_write (struct videocodec *codec,
	       u16                reg,
	       u32                val)
{
	struct zoran *zr = (struct zoran *) codec->master_data->data;

	if (post_office_wait(zr)
	    || post_office_write(zr, 0, 1, reg >> 8)
	    || post_office_write(zr, 0, 2, reg & 0xff)) {
		return;
	}

	post_office_write(zr, 0, 3, val & 0xff);
}

/* videocodec bus functions ZR36050 */
static u32
zr36050_read (struct videocodec *codec,
	      u16                reg)
{
	struct zoran *zr = (struct zoran *) codec->master_data->data;
	__u32 data;

	if (post_office_wait(zr)
	    || post_office_write(zr, 1, 0, reg >> 2)) {	// reg. HIGHBYTES
		return -1;
	}

	data = post_office_read(zr, 0, reg & 0x03) & 0xff;	// reg. LOWBYTES + read
	return data;
}

static void
zr36050_write (struct videocodec *codec,
	       u16                reg,
	       u32                val)
{
	struct zoran *zr = (struct zoran *) codec->master_data->data;

	if (post_office_wait(zr)
	    || post_office_write(zr, 1, 0, reg >> 2)) {	// reg. HIGHBYTES
		return;
	}

	post_office_write(zr, 0, reg & 0x03, val & 0xff);	// reg. LOWBYTES + wr. data
}

/* videocodec bus functions ZR36016 */
static u32
zr36016_read (struct videocodec *codec,
	      u16                reg)
{
	struct zoran *zr = (struct zoran *) codec->master_data->data;
	__u32 data;

	if (post_office_wait(zr)) {
		return -1;
	}

	data = post_office_read(zr, 2, reg & 0x03) & 0xff;	// read
	return data;
}

/* hack for in zoran_device.c */
void
zr36016_write (struct videocodec *codec,
	       u16                reg,
	       u32                val)
{
	struct zoran *zr = (struct zoran *) codec->master_data->data;

	if (post_office_wait(zr)) {
		return;
	}

	post_office_write(zr, 2, reg & 0x03, val & 0x0ff);	// wr. data
}

/*
 * Board specific information
 */

static void
dc10_init (struct zoran *zr)
{
	dprintk(3, KERN_DEBUG "%s: %s\n", ZR_DEVNAME(zr), __func__);

	/* Pixel clock selection */
	GPIO(zr, 4, 0);
	GPIO(zr, 5, 1);
	/* Enable the video bus sync signals */
	GPIO(zr, 7, 0);
}

static void
dc10plus_init (struct zoran *zr)
{
	dprintk(3, KERN_DEBUG "%s: %s\n", ZR_DEVNAME(zr), __func__);
}

static void
buz_init (struct zoran *zr)
{
	dprintk(3, KERN_DEBUG "%s: %s\n", ZR_DEVNAME(zr), __func__);

	/* some stuff from Iomega */
	pci_write_config_dword(zr->pci_dev, 0xfc, 0x90680f15);
	pci_write_config_dword(zr->pci_dev, 0x0c, 0x00012020);
	pci_write_config_dword(zr->pci_dev, 0xe8, 0xc0200000);
}

static void
lml33_init (struct zoran *zr)
{
	dprintk(3, KERN_DEBUG "%s: %s\n", ZR_DEVNAME(zr), __func__);

	GPIO(zr, 2, 1);		// Set Composite input/output
}

static void
avs6eyes_init (struct zoran *zr)
{
	// AverMedia 6-Eyes original driver by Christer Weinigel

	// Lifted straight from Christer's old driver and
	// modified slightly by Martin Samuelsson.

	int mux = default_mux; /* 1 = BT866, 7 = VID1 */

	GPIO(zr, 4, 1); /* Bt866 SLEEP on */
	udelay(2);

	GPIO(zr, 0, 1); /* ZR36060 /RESET on */
	GPIO(zr, 1, 0); /* ZR36060 /SLEEP on */
	GPIO(zr, 2, mux & 1);   /* MUX S0 */
	GPIO(zr, 3, 0); /* /FRAME on */
	GPIO(zr, 4, 0); /* Bt866 SLEEP off */
	GPIO(zr, 5, mux & 2);   /* MUX S1 */
	GPIO(zr, 6, 0); /* ? */
	GPIO(zr, 7, mux & 4);   /* MUX S2 */

}

static char *
codecid_to_modulename (u16 codecid)
{
	char *name = NULL;

	switch (codecid) {
	case CODEC_TYPE_ZR36060:
		name = "zr36060";
		break;
	case CODEC_TYPE_ZR36050:
		name = "zr36050";
		break;
	case CODEC_TYPE_ZR36016:
		name = "zr36016";
		break;
	}

	return name;
}

// struct tvnorm {
//      u16 Wt, Wa, HStart, HSyncStart, Ht, Ha, VStart;
// };

static struct tvnorm f50sqpixel = { 944, 768, 83, 880, 625, 576, 16 };
static struct tvnorm f60sqpixel = { 780, 640, 51, 716, 525, 480, 12 };
static struct tvnorm f50ccir601 = { 864, 720, 75, 804, 625, 576, 18 };
static struct tvnorm f60ccir601 = { 858, 720, 57, 788, 525, 480, 16 };

static struct tvnorm f50ccir601_lml33 = { 864, 720, 75+34, 804, 625, 576, 18 };
static struct tvnorm f60ccir601_lml33 = { 858, 720, 57+34, 788, 525, 480, 16 };

/* The DC10 (57/16/50) uses VActive as HSync, so HStart must be 0 */
static struct tvnorm f50sqpixel_dc10 = { 944, 768, 0, 880, 625, 576, 0 };
static struct tvnorm f60sqpixel_dc10 = { 780, 640, 0, 716, 525, 480, 12 };

/* FIXME: I cannot swap U and V in saa7114, so i do one
 * pixel left shift in zoran (75 -> 74)
 * (Maxim Yevtyushkin <max@linuxmedialabs.com>) */
static struct tvnorm f50ccir601_lm33r10 = { 864, 720, 74+54, 804, 625, 576, 18 };
static struct tvnorm f60ccir601_lm33r10 = { 858, 720, 56+54, 788, 525, 480, 16 };

/* FIXME: The ks0127 seem incapable of swapping U and V, too, which is why I
 * copy Maxim's left shift hack for the 6 Eyes.
 *
 * Christer's driver used the unshifted norms, though...
 * /Sam  */
static struct tvnorm f50ccir601_avs6eyes = { 864, 720, 74, 804, 625, 576, 18 };
static struct tvnorm f60ccir601_avs6eyes = { 858, 720, 56, 788, 525, 480, 16 };

static const unsigned short vpx3220_addrs[] = { 0x43, 0x47, I2C_CLIENT_END };
static const unsigned short saa7110_addrs[] = { 0x4e, 0x4f, I2C_CLIENT_END };
static const unsigned short saa7111_addrs[] = { 0x25, 0x24, I2C_CLIENT_END };
static const unsigned short saa7114_addrs[] = { 0x21, 0x20, I2C_CLIENT_END };
static const unsigned short adv717x_addrs[] = { 0x6a, 0x6b, 0x2a, 0x2b, I2C_CLIENT_END };
static const unsigned short ks0127_addrs[] = { 0x6c, 0x6d, I2C_CLIENT_END };
static const unsigned short saa7185_addrs[] = { 0x44, I2C_CLIENT_END };
static const unsigned short bt819_addrs[] = { 0x45, I2C_CLIENT_END };
static const unsigned short bt856_addrs[] = { 0x44, I2C_CLIENT_END };
static const unsigned short bt866_addrs[] = { 0x44, I2C_CLIENT_END };

static struct card_info zoran_cards[NUM_CARDS] __devinitdata = {
	{
		.type = DC10_old,
		.name = "DC10(old)",
		.i2c_decoder = "vpx3220a",
		.addrs_decoder = vpx3220_addrs,
		.video_codec = CODEC_TYPE_ZR36050,
		.video_vfe = CODEC_TYPE_ZR36016,

		.inputs = 3,
		.input = {
			{ 1, "Composite" },
			{ 2, "S-Video" },
			{ 0, "Internal/comp" }
		},
		.norms = V4L2_STD_NTSC|V4L2_STD_PAL|V4L2_STD_SECAM,
		.tvn = {
			&f50sqpixel_dc10,
			&f60sqpixel_dc10,
			&f50sqpixel_dc10
		},
		.jpeg_int = 0,
		.vsync_int = ZR36057_ISR_GIRQ1,
		.gpio = { 2, 1, -1, 3, 7, 0, 4, 5 },
		.gpio_pol = { 0, 0, 0, 1, 0, 0, 0, 0 },
		.gpcs = { -1, 0 },
		.vfe_pol = { 0, 0, 0, 0, 0, 0, 0, 0 },
		.gws_not_connected = 0,
		.input_mux = 0,
		.init = &dc10_init,
	}, {
		.type = DC10_new,
		.name = "DC10(new)",
		.i2c_decoder = "saa7110",
		.addrs_decoder = saa7110_addrs,
		.i2c_encoder = "adv7175",
		.addrs_encoder = adv717x_addrs,
		.video_codec = CODEC_TYPE_ZR36060,

		.inputs = 3,
		.input = {
				{ 0, "Composite" },
				{ 7, "S-Video" },
				{ 5, "Internal/comp" }
			},
		.norms = V4L2_STD_NTSC|V4L2_STD_PAL|V4L2_STD_SECAM,
		.tvn = {
				&f50sqpixel,
				&f60sqpixel,
				&f50sqpixel},
		.jpeg_int = ZR36057_ISR_GIRQ0,
		.vsync_int = ZR36057_ISR_GIRQ1,
		.gpio = { 3, 0, 6, 1, 2, -1, 4, 5 },
		.gpio_pol = { 0, 0, 0, 0, 0, 0, 0, 0 },
		.gpcs = { -1, 1},
		.vfe_pol = { 1, 1, 1, 1, 0, 0, 0, 0 },
		.gws_not_connected = 0,
		.input_mux = 0,
		.init = &dc10plus_init,
	}, {
		.type = DC10plus,
		.name = "DC10plus",
		.i2c_decoder = "saa7110",
		.addrs_decoder = saa7110_addrs,
		.i2c_encoder = "adv7175",
		.addrs_encoder = adv717x_addrs,
		.video_codec = CODEC_TYPE_ZR36060,

		.inputs = 3,
		.input = {
			{ 0, "Composite" },
			{ 7, "S-Video" },
			{ 5, "Internal/comp" }
		},
		.norms = V4L2_STD_NTSC|V4L2_STD_PAL|V4L2_STD_SECAM,
		.tvn = {
			&f50sqpixel,
			&f60sqpixel,
			&f50sqpixel
		},
		.jpeg_int = ZR36057_ISR_GIRQ0,
		.vsync_int = ZR36057_ISR_GIRQ1,
		.gpio = { 3, 0, 6, 1, 2, -1, 4, 5 },
		.gpio_pol = { 0, 0, 0, 0, 0, 0, 0, 0 },
		.gpcs = { -1, 1 },
		.vfe_pol = { 1, 1, 1, 1, 0, 0, 0, 0 },
		.gws_not_connected = 0,
		.input_mux = 0,
		.init = &dc10plus_init,
	}, {
		.type = DC30,
		.name = "DC30",
		.i2c_decoder = "vpx3220a",
		.addrs_decoder = vpx3220_addrs,
		.i2c_encoder = "adv7175",
		.addrs_encoder = adv717x_addrs,
		.video_codec = CODEC_TYPE_ZR36050,
		.video_vfe = CODEC_TYPE_ZR36016,

		.inputs = 3,
		.input = {
			{ 1, "Composite" },
			{ 2, "S-Video" },
			{ 0, "Internal/comp" }
		},
		.norms = V4L2_STD_NTSC|V4L2_STD_PAL|V4L2_STD_SECAM,
		.tvn = {
			&f50sqpixel_dc10,
			&f60sqpixel_dc10,
			&f50sqpixel_dc10
		},
		.jpeg_int = 0,
		.vsync_int = ZR36057_ISR_GIRQ1,
		.gpio = { 2, 1, -1, 3, 7, 0, 4, 5 },
		.gpio_pol = { 0, 0, 0, 1, 0, 0, 0, 0 },
		.gpcs = { -1, 0 },
		.vfe_pol = { 0, 0, 0, 0, 0, 0, 0, 0 },
		.gws_not_connected = 0,
		.input_mux = 0,
		.init = &dc10_init,
	}, {
		.type = DC30plus,
		.name = "DC30plus",
		.i2c_decoder = "vpx3220a",
		.addrs_decoder = vpx3220_addrs,
		.i2c_encoder = "adv7175",
		.addrs_encoder = adv717x_addrs,
		.video_codec = CODEC_TYPE_ZR36050,
		.video_vfe = CODEC_TYPE_ZR36016,

		.inputs = 3,
		.input = {
			{ 1, "Composite" },
			{ 2, "S-Video" },
			{ 0, "Internal/comp" }
		},
		.norms = V4L2_STD_NTSC|V4L2_STD_PAL|V4L2_STD_SECAM,
		.tvn = {
			&f50sqpixel_dc10,
			&f60sqpixel_dc10,
			&f50sqpixel_dc10
		},
		.jpeg_int = 0,
		.vsync_int = ZR36057_ISR_GIRQ1,
		.gpio = { 2, 1, -1, 3, 7, 0, 4, 5 },
		.gpio_pol = { 0, 0, 0, 1, 0, 0, 0, 0 },
		.gpcs = { -1, 0 },
		.vfe_pol = { 0, 0, 0, 0, 0, 0, 0, 0 },
		.gws_not_connected = 0,
		.input_mux = 0,
		.init = &dc10_init,
	}, {
		.type = LML33,
		.name = "LML33",
		.i2c_decoder = "bt819a",
		.addrs_decoder = bt819_addrs,
		.i2c_encoder = "bt856",
		.addrs_encoder = bt856_addrs,
		.video_codec = CODEC_TYPE_ZR36060,

		.inputs = 2,
		.input = {
			{ 0, "Composite" },
			{ 7, "S-Video" }
		},
		.norms = V4L2_STD_NTSC|V4L2_STD_PAL,
		.tvn = {
			&f50ccir601_lml33,
			&f60ccir601_lml33,
			NULL
		},
		.jpeg_int = ZR36057_ISR_GIRQ1,
		.vsync_int = ZR36057_ISR_GIRQ0,
		.gpio = { 1, -1, 3, 5, 7, -1, -1, -1 },
		.gpio_pol = { 0, 0, 0, 0, 1, 0, 0, 0 },
		.gpcs = { 3, 1 },
		.vfe_pol = { 1, 1, 0, 0, 0, 1, 0, 0 },
		.gws_not_connected = 1,
		.input_mux = 0,
		.init = &lml33_init,
	}, {
		.type = LML33R10,
		.name = "LML33R10",
		.i2c_decoder = "saa7114",
		.addrs_decoder = saa7114_addrs,
		.i2c_encoder = "adv7170",
		.addrs_encoder = adv717x_addrs,
		.video_codec = CODEC_TYPE_ZR36060,

		.inputs = 2,
		.input = {
			{ 0, "Composite" },
			{ 7, "S-Video" }
		},
		.norms = V4L2_STD_NTSC|V4L2_STD_PAL,
		.tvn = {
			&f50ccir601_lm33r10,
			&f60ccir601_lm33r10,
			NULL
		},
		.jpeg_int = ZR36057_ISR_GIRQ1,
		.vsync_int = ZR36057_ISR_GIRQ0,
		.gpio = { 1, -1, 3, 5, 7, -1, -1, -1 },
		.gpio_pol = { 0, 0, 0, 0, 1, 0, 0, 0 },
		.gpcs = { 3, 1 },
		.vfe_pol = { 1, 1, 0, 0, 0, 1, 0, 0 },
		.gws_not_connected = 1,
		.input_mux = 0,
		.init = &lml33_init,
	}, {
		.type = BUZ,
		.name = "Buz",
		.i2c_decoder = "saa7111",
		.addrs_decoder = saa7111_addrs,
		.i2c_encoder = "saa7185",
		.addrs_encoder = saa7185_addrs,
		.video_codec = CODEC_TYPE_ZR36060,

		.inputs = 2,
		.input = {
			{ 3, "Composite" },
			{ 7, "S-Video" }
		},
		.norms = V4L2_STD_NTSC|V4L2_STD_PAL|V4L2_STD_SECAM,
		.tvn = {
			&f50ccir601,
			&f60ccir601,
			&f50ccir601
		},
		.jpeg_int = ZR36057_ISR_GIRQ1,
		.vsync_int = ZR36057_ISR_GIRQ0,
		.gpio = { 1, -1, 3, -1, -1, -1, -1, -1 },
		.gpio_pol = { 0, 0, 0, 0, 0, 0, 0, 0 },
		.gpcs = { 3, 1 },
		.vfe_pol = { 1, 1, 0, 0, 0, 1, 0, 0 },
		.gws_not_connected = 1,
		.input_mux = 0,
		.init = &buz_init,
	}, {
		.type = AVS6EYES,
		.name = "6-Eyes",
		/* AverMedia chose not to brand the 6-Eyes. Thus it
		   can't be autodetected, and requires card=x. */
		.i2c_decoder = "ks0127",
		.addrs_decoder = ks0127_addrs,
		.i2c_encoder = "bt866",
		.addrs_encoder = bt866_addrs,
		.video_codec = CODEC_TYPE_ZR36060,

		.inputs = 10,
		.input = {
			{ 0, "Composite 1" },
			{ 1, "Composite 2" },
			{ 2, "Composite 3" },
			{ 4, "Composite 4" },
			{ 5, "Composite 5" },
			{ 6, "Composite 6" },
			{ 8, "S-Video 1" },
			{ 9, "S-Video 2" },
			{10, "S-Video 3" },
			{15, "YCbCr" }
		},
		.norms = V4L2_STD_NTSC|V4L2_STD_PAL,
		.tvn = {
			&f50ccir601_avs6eyes,
			&f60ccir601_avs6eyes,
			NULL
		},
		.jpeg_int = ZR36057_ISR_GIRQ1,
		.vsync_int = ZR36057_ISR_GIRQ0,
		.gpio = { 1, 0, 3, -1, -1, -1, -1, -1 },// Validity unknown /Sam
		.gpio_pol = { 0, 0, 0, 0, 0, 0, 0, 0 }, // Validity unknown /Sam
		.gpcs = { 3, 1 },			// Validity unknown /Sam
		.vfe_pol = { 1, 0, 0, 0, 0, 1, 0, 0 },  // Validity unknown /Sam
		.gws_not_connected = 1,
		.input_mux = 1,
		.init = &avs6eyes_init,
	}

};

/*
 * I2C functions
 */
/* software I2C functions */
static int
zoran_i2c_getsda (void *data)
{
	struct zoran *zr = (struct zoran *) data;

	return (btread(ZR36057_I2CBR) >> 1) & 1;
}

static int
zoran_i2c_getscl (void *data)
{
	struct zoran *zr = (struct zoran *) data;

	return btread(ZR36057_I2CBR) & 1;
}

static void
zoran_i2c_setsda (void *data,
		  int   state)
{
	struct zoran *zr = (struct zoran *) data;

	if (state)
		zr->i2cbr |= 2;
	else
		zr->i2cbr &= ~2;
	btwrite(zr->i2cbr, ZR36057_I2CBR);
}

static void
zoran_i2c_setscl (void *data,
		  int   state)
{
	struct zoran *zr = (struct zoran *) data;

	if (state)
		zr->i2cbr |= 1;
	else
		zr->i2cbr &= ~1;
	btwrite(zr->i2cbr, ZR36057_I2CBR);
}

static const struct i2c_algo_bit_data zoran_i2c_bit_data_template = {
	.setsda = zoran_i2c_setsda,
	.setscl = zoran_i2c_setscl,
	.getsda = zoran_i2c_getsda,
	.getscl = zoran_i2c_getscl,
	.udelay = 10,
	.timeout = 100,
};

static int
zoran_register_i2c (struct zoran *zr)
{
	memcpy(&zr->i2c_algo, &zoran_i2c_bit_data_template,
	       sizeof(struct i2c_algo_bit_data));
	zr->i2c_algo.data = zr;
	strlcpy(zr->i2c_adapter.name, ZR_DEVNAME(zr),
		sizeof(zr->i2c_adapter.name));
	i2c_set_adapdata(&zr->i2c_adapter, &zr->v4l2_dev);
	zr->i2c_adapter.algo_data = &zr->i2c_algo;
	zr->i2c_adapter.dev.parent = &zr->pci_dev->dev;
	return i2c_bit_add_bus(&zr->i2c_adapter);
}

static void
zoran_unregister_i2c (struct zoran *zr)
{
	i2c_del_adapter(&zr->i2c_adapter);
}

/* Check a zoran_params struct for correctness, insert default params */

int
zoran_check_jpg_settings (struct zoran              *zr,
			  struct zoran_jpg_settings *settings,
			  int try)
{
	int err = 0, err0 = 0;

	dprintk(4,
		KERN_DEBUG
		"%s: %s - dec: %d, Hdcm: %d, Vdcm: %d, Tdcm: %d\n",
		ZR_DEVNAME(zr), __func__, settings->decimation, settings->HorDcm,
		settings->VerDcm, settings->TmpDcm);
	dprintk(4,
		KERN_DEBUG
		"%s: %s - x: %d, y: %d, w: %d, y: %d\n",
		ZR_DEVNAME(zr), __func__, settings->img_x, settings->img_y,
		settings->img_width, settings->img_height);
	/* Check decimation, set default values for decimation = 1, 2, 4 */
	switch (settings->decimation) {
	case 1:

		settings->HorDcm = 1;
		settings->VerDcm = 1;
		settings->TmpDcm = 1;
		settings->field_per_buff = 2;
		settings->img_x = 0;
		settings->img_y = 0;
		settings->img_width = BUZ_MAX_WIDTH;
		settings->img_height = BUZ_MAX_HEIGHT / 2;
		break;
	case 2:

		settings->HorDcm = 2;
		settings->VerDcm = 1;
		settings->TmpDcm = 2;
		settings->field_per_buff = 1;
		settings->img_x = (BUZ_MAX_WIDTH == 720) ? 8 : 0;
		settings->img_y = 0;
		settings->img_width =
		    (BUZ_MAX_WIDTH == 720) ? 704 : BUZ_MAX_WIDTH;
		settings->img_height = BUZ_MAX_HEIGHT / 2;
		break;
	case 4:

		if (zr->card.type == DC10_new) {
			dprintk(1,
				KERN_DEBUG
				"%s: %s - HDec by 4 is not supported on the DC10\n",
				ZR_DEVNAME(zr), __func__);
			err0++;
			break;
		}

		settings->HorDcm = 4;
		settings->VerDcm = 2;
		settings->TmpDcm = 2;
		settings->field_per_buff = 1;
		settings->img_x = (BUZ_MAX_WIDTH == 720) ? 8 : 0;
		settings->img_y = 0;
		settings->img_width =
		    (BUZ_MAX_WIDTH == 720) ? 704 : BUZ_MAX_WIDTH;
		settings->img_height = BUZ_MAX_HEIGHT / 2;
		break;
	case 0:

		/* We have to check the data the user has set */

		if (settings->HorDcm != 1 && settings->HorDcm != 2 &&
		    (zr->card.type == DC10_new || settings->HorDcm != 4)) {
			settings->HorDcm = clamp(settings->HorDcm, 1, 2);
			err0++;
		}
		if (settings->VerDcm != 1 && settings->VerDcm != 2) {
			settings->VerDcm = clamp(settings->VerDcm, 1, 2);
			err0++;
		}
		if (settings->TmpDcm != 1 && settings->TmpDcm != 2) {
			settings->TmpDcm = clamp(settings->TmpDcm, 1, 2);
			err0++;
		}
		if (settings->field_per_buff != 1 &&
		    settings->field_per_buff != 2) {
			settings->field_per_buff = clamp(settings->field_per_buff, 1, 2);
			err0++;
		}
		if (settings->img_x < 0) {
			settings->img_x = 0;
			err0++;
		}
		if (settings->img_y < 0) {
			settings->img_y = 0;
			err0++;
		}
		if (settings->img_width < 0 || settings->img_width > BUZ_MAX_WIDTH) {
			settings->img_width = clamp(settings->img_width, 0, (int)BUZ_MAX_WIDTH);
			err0++;
		}
		if (settings->img_height < 0 || settings->img_height > BUZ_MAX_HEIGHT / 2) {
			settings->img_height = clamp(settings->img_height, 0, BUZ_MAX_HEIGHT / 2);
			err0++;
		}
		if (settings->img_x + settings->img_width > BUZ_MAX_WIDTH) {
			settings->img_x = BUZ_MAX_WIDTH - settings->img_width;
			err0++;
		}
		if (settings->img_y + settings->img_height > BUZ_MAX_HEIGHT / 2) {
			settings->img_y = BUZ_MAX_HEIGHT / 2 - settings->img_height;
			err0++;
		}
		if (settings->img_width % (16 * settings->HorDcm) != 0) {
			settings->img_width -= settings->img_width % (16 * settings->HorDcm);
			if (settings->img_width == 0)
				settings->img_width = 16 * settings->HorDcm;
			err0++;
		}
		if (settings->img_height % (8 * settings->VerDcm) != 0) {
			settings->img_height -= settings->img_height % (8 * settings->VerDcm);
			if (settings->img_height == 0)
				settings->img_height = 8 * settings->VerDcm;
			err0++;
		}

		if (!try && err0) {
			dprintk(1,
				KERN_ERR
				"%s: %s - error in params for decimation = 0\n",
				ZR_DEVNAME(zr), __func__);
			err++;
		}
		break;
	default:
		dprintk(1,
			KERN_ERR
			"%s: %s - decimation = %d, must be 0, 1, 2 or 4\n",
			ZR_DEVNAME(zr), __func__, settings->decimation);
		err++;
		break;
	}

	if (settings->jpg_comp.quality > 100)
		settings->jpg_comp.quality = 100;
	if (settings->jpg_comp.quality < 5)
		settings->jpg_comp.quality = 5;
	if (settings->jpg_comp.APPn < 0)
		settings->jpg_comp.APPn = 0;
	if (settings->jpg_comp.APPn > 15)
		settings->jpg_comp.APPn = 15;
	if (settings->jpg_comp.APP_len < 0)
		settings->jpg_comp.APP_len = 0;
	if (settings->jpg_comp.APP_len > 60)
		settings->jpg_comp.APP_len = 60;
	if (settings->jpg_comp.COM_len < 0)
		settings->jpg_comp.COM_len = 0;
	if (settings->jpg_comp.COM_len > 60)
		settings->jpg_comp.COM_len = 60;
	if (err)
		return -EINVAL;
	return 0;
}

void
zoran_open_init_params (struct zoran *zr)
{
	int i;

	/* User must explicitly set a window */
	zr->overlay_settings.is_set = 0;
	zr->overlay_mask = NULL;
	zr->overlay_active = ZORAN_FREE;

	zr->v4l_memgrab_active = 0;
	zr->v4l_overlay_active = 0;
	zr->v4l_grab_frame = NO_GRAB_ACTIVE;
	zr->v4l_grab_seq = 0;
	zr->v4l_settings.width = 192;
	zr->v4l_settings.height = 144;
	zr->v4l_settings.format = &zoran_formats[7];	/* YUY2 - YUV-4:2:2 packed */
	zr->v4l_settings.bytesperline =
	    zr->v4l_settings.width *
	    ((zr->v4l_settings.format->depth + 7) / 8);

	/* DMA ring stuff for V4L */
	zr->v4l_pend_tail = 0;
	zr->v4l_pend_head = 0;
	zr->v4l_sync_tail = 0;
	zr->v4l_buffers.active = ZORAN_FREE;
	for (i = 0; i < VIDEO_MAX_FRAME; i++) {
		zr->v4l_buffers.buffer[i].state = BUZ_STATE_USER;	/* nothing going on */
	}
	zr->v4l_buffers.allocated = 0;

	for (i = 0; i < BUZ_MAX_FRAME; i++) {
		zr->jpg_buffers.buffer[i].state = BUZ_STATE_USER;	/* nothing going on */
	}
	zr->jpg_buffers.active = ZORAN_FREE;
	zr->jpg_buffers.allocated = 0;
	/* Set necessary params and call zoran_check_jpg_settings to set the defaults */
	zr->jpg_settings.decimation = 1;
	zr->jpg_settings.jpg_comp.quality = 50;	/* default compression factor 8 */
	if (zr->card.type != BUZ)
		zr->jpg_settings.odd_even = 1;
	else
		zr->jpg_settings.odd_even = 0;
	zr->jpg_settings.jpg_comp.APPn = 0;
	zr->jpg_settings.jpg_comp.APP_len = 0;	/* No APPn marker */
	memset(zr->jpg_settings.jpg_comp.APP_data, 0,
	       sizeof(zr->jpg_settings.jpg_comp.APP_data));
	zr->jpg_settings.jpg_comp.COM_len = 0;	/* No COM marker */
	memset(zr->jpg_settings.jpg_comp.COM_data, 0,
	       sizeof(zr->jpg_settings.jpg_comp.COM_data));
	zr->jpg_settings.jpg_comp.jpeg_markers =
	    V4L2_JPEG_MARKER_DHT | V4L2_JPEG_MARKER_DQT;
	i = zoran_check_jpg_settings(zr, &zr->jpg_settings, 0);
	if (i)
		dprintk(1, KERN_ERR "%s: %s internal error\n",
			ZR_DEVNAME(zr), __func__);

	clear_interrupt_counters(zr);
	zr->testing = 0;
}

static void __devinit
test_interrupts (struct zoran *zr)
{
	DEFINE_WAIT(wait);
	int timeout, icr;

	clear_interrupt_counters(zr);

	zr->testing = 1;
	icr = btread(ZR36057_ICR);
	btwrite(0x78000000 | ZR36057_ICR_IntPinEn, ZR36057_ICR);
	prepare_to_wait(&zr->test_q, &wait, TASK_INTERRUPTIBLE);
	timeout = schedule_timeout(HZ);
	finish_wait(&zr->test_q, &wait);
	btwrite(0, ZR36057_ICR);
	btwrite(0x78000000, ZR36057_ISR);
	zr->testing = 0;
	dprintk(5, KERN_INFO "%s: Testing interrupts...\n", ZR_DEVNAME(zr));
	if (timeout) {
		dprintk(1, ": time spent: %d\n", 1 * HZ - timeout);
	}
	if (zr36067_debug > 1)
		print_interrupts(zr);
	btwrite(icr, ZR36057_ICR);
}

static int __devinit
zr36057_init (struct zoran *zr)
{
	int j, err;

	dprintk(1,
		KERN_INFO
		"%s: %s - initializing card[%d], zr=%p\n",
		ZR_DEVNAME(zr), __func__, zr->id, zr);

	/* default setup of all parameters which will persist between opens */
	zr->user = 0;

	init_waitqueue_head(&zr->v4l_capq);
	init_waitqueue_head(&zr->jpg_capq);
	init_waitqueue_head(&zr->test_q);
	zr->jpg_buffers.allocated = 0;
	zr->v4l_buffers.allocated = 0;

	zr->vbuf_base = (void *) vidmem;
	zr->vbuf_width = 0;
	zr->vbuf_height = 0;
	zr->vbuf_depth = 0;
	zr->vbuf_bytesperline = 0;

	/* Avoid nonsense settings from user for default input/norm */
	if (default_norm < 0 || default_norm > 2)
		default_norm = 0;
	if (default_norm == 0) {
		zr->norm = V4L2_STD_PAL;
		zr->timing = zr->card.tvn[0];
	} else if (default_norm == 1) {
		zr->norm = V4L2_STD_NTSC;
		zr->timing = zr->card.tvn[1];
	} else {
		zr->norm = V4L2_STD_SECAM;
		zr->timing = zr->card.tvn[2];
	}
	if (zr->timing == NULL) {
		dprintk(1,
			KERN_WARNING
			"%s: %s - default TV standard not supported by hardware. PAL will be used.\n",
			ZR_DEVNAME(zr), __func__);
		zr->norm = V4L2_STD_PAL;
		zr->timing = zr->card.tvn[0];
	}

	if (default_input > zr->card.inputs-1) {
		dprintk(1,
			KERN_WARNING
			"%s: default_input value %d out of range (0-%d)\n",
			ZR_DEVNAME(zr), default_input, zr->card.inputs-1);
		default_input = 0;
	}
	zr->input = default_input;

	/* default setup (will be repeated at every open) */
	zoran_open_init_params(zr);

	/* allocate memory *before* doing anything to the hardware
	 * in case allocation fails */
	zr->stat_com = kzalloc(BUZ_NUM_STAT_COM * 4, GFP_KERNEL);
	zr->video_dev = video_device_alloc();
	if (!zr->stat_com || !zr->video_dev) {
		dprintk(1,
			KERN_ERR
			"%s: %s - kmalloc (STAT_COM) failed\n",
			ZR_DEVNAME(zr), __func__);
		err = -ENOMEM;
		goto exit_free;
	}
	for (j = 0; j < BUZ_NUM_STAT_COM; j++) {
		zr->stat_com[j] = cpu_to_le32(1); /* mark as unavailable to zr36057 */
	}

	/*
	 *   Now add the template and register the device unit.
	 */
	memcpy(zr->video_dev, &zoran_template, sizeof(zoran_template));
	zr->video_dev->parent = &zr->pci_dev->dev;
	strcpy(zr->video_dev->name, ZR_DEVNAME(zr));
	err = video_register_device(zr->video_dev, VFL_TYPE_GRABBER, video_nr[zr->id]);
	if (err < 0)
		goto exit_free;
	video_set_drvdata(zr->video_dev, zr);

	zoran_init_hardware(zr);
	if (zr36067_debug > 2)
		detect_guest_activity(zr);
	test_interrupts(zr);
	if (!pass_through) {
		decoder_call(zr, video, s_stream, 0);
		encoder_call(zr, video, s_routing, 2, 0, 0);
	}

	zr->zoran_proc = NULL;
	zr->initialized = 1;
	return 0;

exit_free:
	kfree(zr->stat_com);
	kfree(zr->video_dev);
	return err;
}

static void __devexit zoran_remove(struct pci_dev *pdev)
{
	struct v4l2_device *v4l2_dev = dev_get_drvdata(&pdev->dev);
	struct zoran *zr = to_zoran(v4l2_dev);

	if (!zr->initialized)
		goto exit_free;

	/* unregister videocodec bus */
	if (zr->codec) {
		struct videocodec_master *master = zr->codec->master_data;

		videocodec_detach(zr->codec);
		kfree(master);
	}
	if (zr->vfe) {
		struct videocodec_master *master = zr->vfe->master_data;

		videocodec_detach(zr->vfe);
		kfree(master);
	}

	/* unregister i2c bus */
	zoran_unregister_i2c(zr);
	/* disable PCI bus-mastering */
	zoran_set_pci_master(zr, 0);
	/* put chip into reset */
	btwrite(0, ZR36057_SPGPPCR);
	free_irq(zr->pci_dev->irq, zr);
	/* unmap and free memory */
	kfree(zr->stat_com);
	zoran_proc_cleanup(zr);
	iounmap(zr->zr36057_mem);
	pci_disable_device(zr->pci_dev);
	video_unregister_device(zr->video_dev);
exit_free:
	v4l2_device_unregister(&zr->v4l2_dev);
	kfree(zr);
}

void
zoran_vdev_release (struct video_device *vdev)
{
	kfree(vdev);
}

static struct videocodec_master * __devinit
zoran_setup_videocodec (struct zoran *zr,
			int           type)
{
	struct videocodec_master *m = NULL;

	m = kmalloc(sizeof(struct videocodec_master), GFP_KERNEL);
	if (!m) {
		dprintk(1, KERN_ERR "%s: %s - no memory\n",
			ZR_DEVNAME(zr), __func__);
		return m;
	}

	/* magic and type are unused for master struct. Makes sense only at
	   codec structs.
	   In the past, .type were initialized to the old V4L1 .hardware
	   value, as VID_HARDWARE_ZR36067
	 */
	m->magic = 0L;
	m->type = 0;

	m->flags = CODEC_FLAG_ENCODER | CODEC_FLAG_DECODER;
	strlcpy(m->name, ZR_DEVNAME(zr), sizeof(m->name));
	m->data = zr;

	switch (type)
	{
	case CODEC_TYPE_ZR36060:
		m->readreg = zr36060_read;
		m->writereg = zr36060_write;
		m->flags |= CODEC_FLAG_JPEG | CODEC_FLAG_VFE;
		break;
	case CODEC_TYPE_ZR36050:
		m->readreg = zr36050_read;
		m->writereg = zr36050_write;
		m->flags |= CODEC_FLAG_JPEG;
		break;
	case CODEC_TYPE_ZR36016:
		m->readreg = zr36016_read;
		m->writereg = zr36016_write;
		m->flags |= CODEC_FLAG_VFE;
		break;
	}

	return m;
}

static void zoran_subdev_notify(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct zoran *zr = to_zoran(sd->v4l2_dev);

	/* Bt819 needs to reset its FIFO buffer using #FRST pin and
	   LML33 card uses GPIO(7) for that. */
	if (cmd == BT819_FIFO_RESET_LOW)
		GPIO(zr, 7, 0);
	else if (cmd == BT819_FIFO_RESET_HIGH)
		GPIO(zr, 7, 1);
}

/*
 *   Scan for a Buz card (actually for the PCI controller ZR36057),
 *   request the irq and map the io memory
 */
static int __devinit zoran_probe(struct pci_dev *pdev,
				 const struct pci_device_id *ent)
{
	unsigned char latency, need_latency;
	struct zoran *zr;
	int result;
	struct videocodec_master *master_vfe = NULL;
	struct videocodec_master *master_codec = NULL;
	int card_num;
	char *codec_name, *vfe_name;
	unsigned int nr;


	nr = zoran_num++;
	if (nr >= BUZ_MAX) {
		dprintk(1, KERN_ERR "%s: driver limited to %d card(s) maximum\n",
			ZORAN_NAME, BUZ_MAX);
		return -ENOENT;
	}

	zr = kzalloc(sizeof(struct zoran), GFP_KERNEL);
	if (!zr) {
		dprintk(1, KERN_ERR "%s: %s - kzalloc failed\n",
			ZORAN_NAME, __func__);
		return -ENOMEM;
	}
	zr->v4l2_dev.notify = zoran_subdev_notify;
	if (v4l2_device_register(&pdev->dev, &zr->v4l2_dev))
		goto zr_free_mem;
	zr->pci_dev = pdev;
	zr->id = nr;
	snprintf(ZR_DEVNAME(zr), sizeof(ZR_DEVNAME(zr)), "MJPEG[%u]", zr->id);
	spin_lock_init(&zr->spinlock);
	mutex_init(&zr->resource_lock);
	mutex_init(&zr->other_lock);
	if (pci_enable_device(pdev))
		goto zr_unreg;
	zr->revision = zr->pci_dev->revision;

	dprintk(1,
		KERN_INFO
		"%s: Zoran ZR360%c7 (rev %d), irq: %d, memory: 0x%08llx\n",
		ZR_DEVNAME(zr), zr->revision < 2 ? '5' : '6', zr->revision,
		zr->pci_dev->irq, (uint64_t)pci_resource_start(zr->pci_dev, 0));
	if (zr->revision >= 2) {
		dprintk(1,
			KERN_INFO
			"%s: Subsystem vendor=0x%04x id=0x%04x\n",
			ZR_DEVNAME(zr), zr->pci_dev->subsystem_vendor,
			zr->pci_dev->subsystem_device);
	}

	/* Use auto-detected card type? */
	if (card[nr] == -1) {
		if (zr->revision < 2) {
			dprintk(1,
				KERN_ERR
				"%s: No card type specified, please use the card=X module parameter\n",
				ZR_DEVNAME(zr));
			dprintk(1,
				KERN_ERR
				"%s: It is not possible to auto-detect ZR36057 based cards\n",
				ZR_DEVNAME(zr));
			goto zr_unreg;
		}

		card_num = ent->driver_data;
		if (card_num >= NUM_CARDS) {
			dprintk(1,
				KERN_ERR
				"%s: Unknown card, try specifying card=X module parameter\n",
				ZR_DEVNAME(zr));
			goto zr_unreg;
		}
		dprintk(3,
			KERN_DEBUG
			"%s: %s() - card %s detected\n",
			ZR_DEVNAME(zr), __func__, zoran_cards[card_num].name);
	} else {
		card_num = card[nr];
		if (card_num >= NUM_CARDS || card_num < 0) {
			dprintk(1,
				KERN_ERR
				"%s: User specified card type %d out of range (0 .. %d)\n",
				ZR_DEVNAME(zr), card_num, NUM_CARDS - 1);
			goto zr_unreg;
		}
	}

	/* even though we make this a non pointer and thus
	 * theoretically allow for making changes to this struct
	 * on a per-individual card basis at runtime, this is
	 * strongly discouraged. This structure is intended to
	 * keep general card information, no settings or anything */
	zr->card = zoran_cards[card_num];
	snprintf(ZR_DEVNAME(zr), sizeof(ZR_DEVNAME(zr)),
		 "%s[%u]", zr->card.name, zr->id);

	zr->zr36057_mem = pci_ioremap_bar(zr->pci_dev, 0);
	if (!zr->zr36057_mem) {
		dprintk(1, KERN_ERR "%s: %s() - ioremap failed\n",
			ZR_DEVNAME(zr), __func__);
		goto zr_unreg;
	}

	result = request_irq(zr->pci_dev->irq, zoran_irq,
			     IRQF_SHARED | IRQF_DISABLED, ZR_DEVNAME(zr), zr);
	if (result < 0) {
		if (result == -EINVAL) {
			dprintk(1,
				KERN_ERR
				"%s: %s - bad irq number or handler\n",
				ZR_DEVNAME(zr), __func__);
		} else if (result == -EBUSY) {
			dprintk(1,
				KERN_ERR
				"%s: %s - IRQ %d busy, change your PnP config in BIOS\n",
				ZR_DEVNAME(zr), __func__, zr->pci_dev->irq);
		} else {
			dprintk(1,
				KERN_ERR
				"%s: %s - can't assign irq, error code %d\n",
				ZR_DEVNAME(zr), __func__, result);
		}
		goto zr_unmap;
	}

	/* set PCI latency timer */
	pci_read_config_byte(zr->pci_dev, PCI_LATENCY_TIMER,
			     &latency);
	need_latency = zr->revision > 1 ? 32 : 48;
	if (latency != need_latency) {
		dprintk(2, KERN_INFO "%s: Changing PCI latency from %d to %d\n",
			ZR_DEVNAME(zr), latency, need_latency);
		pci_write_config_byte(zr->pci_dev, PCI_LATENCY_TIMER,
				      need_latency);
	}

	zr36057_restart(zr);
	/* i2c */
	dprintk(2, KERN_INFO "%s: Initializing i2c bus...\n",
		ZR_DEVNAME(zr));

	if (zoran_register_i2c(zr) < 0) {
		dprintk(1, KERN_ERR "%s: %s - can't initialize i2c bus\n",
			ZR_DEVNAME(zr), __func__);
		goto zr_free_irq;
	}

	zr->decoder = v4l2_i2c_new_subdev(&zr->v4l2_dev,
		&zr->i2c_adapter, zr->card.i2c_decoder,
		0, zr->card.addrs_decoder);

	if (zr->card.i2c_encoder)
		zr->encoder = v4l2_i2c_new_subdev(&zr->v4l2_dev,
			&zr->i2c_adapter, zr->card.i2c_encoder,
			0, zr->card.addrs_encoder);

	dprintk(2,
		KERN_INFO "%s: Initializing videocodec bus...\n",
		ZR_DEVNAME(zr));

	if (zr->card.video_codec) {
		codec_name = codecid_to_modulename(zr->card.video_codec);
		if (codec_name) {
			result = request_module(codec_name);
			if (result) {
				dprintk(1,
					KERN_ERR
					"%s: failed to load modules %s: %d\n",
					ZR_DEVNAME(zr), codec_name, result);
			}
		}
	}
	if (zr->card.video_vfe) {
		vfe_name = codecid_to_modulename(zr->card.video_vfe);
		if (vfe_name) {
			result = request_module(vfe_name);
			if (result < 0) {
				dprintk(1,
					KERN_ERR
					"%s: failed to load modules %s: %d\n",
					ZR_DEVNAME(zr), vfe_name, result);
			}
		}
	}

	/* reset JPEG codec */
	jpeg_codec_sleep(zr, 1);
	jpeg_codec_reset(zr);
	/* video bus enabled */
	/* display codec revision */
	if (zr->card.video_codec != 0) {
		master_codec = zoran_setup_videocodec(zr, zr->card.video_codec);
		if (!master_codec)
			goto zr_unreg_i2c;
		zr->codec = videocodec_attach(master_codec);
		if (!zr->codec) {
			dprintk(1, KERN_ERR "%s: %s - no codec found\n",
				ZR_DEVNAME(zr), __func__);
			goto zr_free_codec;
		}
		if (zr->codec->type != zr->card.video_codec) {
			dprintk(1, KERN_ERR "%s: %s - wrong codec\n",
				ZR_DEVNAME(zr), __func__);
			goto zr_detach_codec;
		}
	}
	if (zr->card.video_vfe != 0) {
		master_vfe = zoran_setup_videocodec(zr, zr->card.video_vfe);
		if (!master_vfe)
			goto zr_detach_codec;
		zr->vfe = videocodec_attach(master_vfe);
		if (!zr->vfe) {
			dprintk(1, KERN_ERR "%s: %s - no VFE found\n",
				ZR_DEVNAME(zr), __func__);
			goto zr_free_vfe;
		}
		if (zr->vfe->type != zr->card.video_vfe) {
			dprintk(1, KERN_ERR "%s: %s = wrong VFE\n",
				ZR_DEVNAME(zr), __func__);
			goto zr_detach_vfe;
		}
	}

	/* take care of Natoma chipset and a revision 1 zr36057 */
	if ((pci_pci_problems & PCIPCI_NATOMA) && zr->revision <= 1) {
		zr->jpg_buffers.need_contiguous = 1;
		dprintk(1, KERN_INFO
			"%s: ZR36057/Natoma bug, max. buffer size is 128K\n",
			ZR_DEVNAME(zr));
	}

	if (zr36057_init(zr) < 0)
		goto zr_detach_vfe;

	zoran_proc_init(zr);

	return 0;

zr_detach_vfe:
	videocodec_detach(zr->vfe);
zr_free_vfe:
	kfree(master_vfe);
zr_detach_codec:
	videocodec_detach(zr->codec);
zr_free_codec:
	kfree(master_codec);
zr_unreg_i2c:
	zoran_unregister_i2c(zr);
zr_free_irq:
	btwrite(0, ZR36057_SPGPPCR);
	free_irq(zr->pci_dev->irq, zr);
zr_unmap:
	iounmap(zr->zr36057_mem);
zr_unreg:
	v4l2_device_unregister(&zr->v4l2_dev);
zr_free_mem:
	kfree(zr);

	return -ENODEV;
}

static struct pci_driver zoran_driver = {
	.name = "zr36067",
	.id_table = zr36067_pci_tbl,
	.probe = zoran_probe,
	.remove = __devexit_p(zoran_remove),
};

static int __init zoran_init(void)
{
	int res;

	printk(KERN_INFO "Zoran MJPEG board driver version %d.%d.%d\n",
	       MAJOR_VERSION, MINOR_VERSION, RELEASE_VERSION);

	/* check the parameters we have been given, adjust if necessary */
	if (v4l_nbufs < 2)
		v4l_nbufs = 2;
	if (v4l_nbufs > VIDEO_MAX_FRAME)
		v4l_nbufs = VIDEO_MAX_FRAME;
	/* The user specfies the in KB, we want them in byte
	 * (and page aligned) */
	v4l_bufsize = PAGE_ALIGN(v4l_bufsize * 1024);
	if (v4l_bufsize < 32768)
		v4l_bufsize = 32768;
	/* 2 MB is arbitrary but sufficient for the maximum possible images */
	if (v4l_bufsize > 2048 * 1024)
		v4l_bufsize = 2048 * 1024;
	if (jpg_nbufs < 4)
		jpg_nbufs = 4;
	if (jpg_nbufs > BUZ_MAX_FRAME)
		jpg_nbufs = BUZ_MAX_FRAME;
	jpg_bufsize = PAGE_ALIGN(jpg_bufsize * 1024);
	if (jpg_bufsize < 8192)
		jpg_bufsize = 8192;
	if (jpg_bufsize > (512 * 1024))
		jpg_bufsize = 512 * 1024;
	/* Use parameter for vidmem or try to find a video card */
	if (vidmem) {
		dprintk(1,
			KERN_INFO
			"%s: Using supplied video memory base address @ 0x%lx\n",
			ZORAN_NAME, vidmem);
	}

	/* some mainboards might not do PCI-PCI data transfer well */
	if (pci_pci_problems & (PCIPCI_FAIL|PCIAGP_FAIL|PCIPCI_ALIMAGIK)) {
		dprintk(1,
			KERN_WARNING
			"%s: chipset does not support reliable PCI-PCI DMA\n",
			ZORAN_NAME);
	}

	res = pci_register_driver(&zoran_driver);
	if (res) {
		dprintk(1,
			KERN_ERR
			"%s: Unable to register ZR36057 driver\n",
			ZORAN_NAME);
		return res;
	}

	return 0;
}

static void __exit zoran_exit(void)
{
	pci_unregister_driver(&zoran_driver);
}

module_init(zoran_init);
module_exit(zoran_exit);

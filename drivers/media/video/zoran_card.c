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

#include <linux/proc_fs.h>
#include <linux/i2c.h>
#include <linux/i2c-algo-bit.h>
#include <linux/videodev.h>
#include <media/v4l2-common.h>
#include <linux/spinlock.h>
#include <linux/sem.h>
#include <linux/kmod.h>
#include <linux/wait.h>

#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/video_decoder.h>
#include <linux/video_encoder.h>
#include <linux/mutex.h>

#include <asm/io.h>

#include "videocodec.h"
#include "zoran.h"
#include "zoran_card.h"
#include "zoran_device.h"
#include "zoran_procfs.h"

#define I2C_NAME(x) (x)->name

extern const struct zoran_format zoran_formats[];

static int card[BUZ_MAX] = { -1, -1, -1, -1 };
module_param_array(card, int, NULL, 0);
MODULE_PARM_DESC(card, "The type of card");

static int encoder[BUZ_MAX] = { -1, -1, -1, -1 };
module_param_array(encoder, int, NULL, 0);
MODULE_PARM_DESC(encoder, "i2c TV encoder");

static int decoder[BUZ_MAX] = { -1, -1, -1, -1 };
module_param_array(decoder, int, NULL, 0);
MODULE_PARM_DESC(decoder, "i2c TV decoder");

/*
   The video mem address of the video card.
   The driver has a little database for some videocards
   to determine it from there. If your video card is not in there
   you have either to give it to the driver as a parameter
   or set in in a VIDIOCSFBUF ioctl
 */

static unsigned long vidmem = 0;	/* Video memory base address */
module_param(vidmem, ulong, 0);

/*
   Default input and video norm at startup of the driver.
*/

static int default_input = 0;	/* 0=Composite, 1=S-Video */
module_param(default_input, int, 0);
MODULE_PARM_DESC(default_input,
		 "Default input (0=Composite, 1=S-Video, 2=Internal)");

static int default_mux = 1;	/* 6 Eyes input selection */
module_param(default_mux, int, 0);
MODULE_PARM_DESC(default_mux,
		 "Default 6 Eyes mux setting (Input selection)");

static int default_norm = 0;	/* 0=PAL, 1=NTSC 2=SECAM */
module_param(default_norm, int, 0);
MODULE_PARM_DESC(default_norm, "Default norm (0=PAL, 1=NTSC, 2=SECAM)");

static int video_nr = -1;	/* /dev/videoN, -1 for autodetect */
module_param(video_nr, int, 0);
MODULE_PARM_DESC(video_nr, "video device number");

/*
   Number and size of grab buffers for Video 4 Linux
   The vast majority of applications should not need more than 2,
   the very popular BTTV driver actually does ONLY have 2.
   Time sensitive applications might need more, the maximum
   is VIDEO_MAX_FRAME (defined in <linux/videodev.h>).

   The size is set so that the maximum possible request
   can be satisfied. Decrease  it, if bigphys_area alloc'd
   memory is low. If you don't have the bigphys_area patch,
   set it to 128 KB. Will you allow only to grab small
   images with V4L, but that's better than nothing.

   v4l_bufsize has to be given in KB !

*/

int v4l_nbufs = 2;
int v4l_bufsize = 128;		/* Everybody should be able to work with this setting */
module_param(v4l_nbufs, int, 0);
MODULE_PARM_DESC(v4l_nbufs, "Maximum number of V4L buffers to use");
module_param(v4l_bufsize, int, 0);
MODULE_PARM_DESC(v4l_bufsize, "Maximum size per V4L buffer (in kB)");

int jpg_nbufs = 32;
int jpg_bufsize = 512;		/* max size for 100% quality full-PAL frame */
module_param(jpg_nbufs, int, 0);
MODULE_PARM_DESC(jpg_nbufs, "Maximum number of JPG buffers to use");
module_param(jpg_bufsize, int, 0);
MODULE_PARM_DESC(jpg_bufsize, "Maximum size per JPG buffer (in kB)");

int pass_through = 0;		/* 1=Pass through TV signal when device is not used */
				/* 0=Show color bar when device is not used (LML33: only if lml33dpath=1) */
module_param(pass_through, int, 0);
MODULE_PARM_DESC(pass_through,
		 "Pass TV signal through to TV-out when idling");

static int debug = 1;
int *zr_debug = &debug;
module_param(debug, int, 0);
MODULE_PARM_DESC(debug, "Debug level (0-4)");

MODULE_DESCRIPTION("Zoran-36057/36067 JPEG codec driver");
MODULE_AUTHOR("Serguei Miridonov");
MODULE_LICENSE("GPL");

static struct pci_device_id zr36067_pci_tbl[] = {
	{PCI_VENDOR_ID_ZORAN, PCI_DEVICE_ID_ZORAN_36057,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{0}
};
MODULE_DEVICE_TABLE(pci, zr36067_pci_tbl);

#define dprintk(num, format, args...) \
	do { \
		if (*zr_debug >= num) \
			printk(format, ##args); \
	} while (0)

int zoran_num;			/* number of Buzs in use */
struct zoran zoran[BUZ_MAX];

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
	dprintk(3, KERN_DEBUG "%s: dc10_init()\n", ZR_DEVNAME(zr));

	/* Pixel clock selection */
	GPIO(zr, 4, 0);
	GPIO(zr, 5, 1);
	/* Enable the video bus sync signals */
	GPIO(zr, 7, 0);
}

static void
dc10plus_init (struct zoran *zr)
{
	dprintk(3, KERN_DEBUG "%s: dc10plus_init()\n", ZR_DEVNAME(zr));
}

static void
buz_init (struct zoran *zr)
{
	dprintk(3, KERN_DEBUG "%s: buz_init()\n", ZR_DEVNAME(zr));

	/* some stuff from Iomega */
	pci_write_config_dword(zr->pci_dev, 0xfc, 0x90680f15);
	pci_write_config_dword(zr->pci_dev, 0x0c, 0x00012020);
	pci_write_config_dword(zr->pci_dev, 0xe8, 0xc0200000);
}

static void
lml33_init (struct zoran *zr)
{
	dprintk(3, KERN_DEBUG "%s: lml33_init()\n", ZR_DEVNAME(zr));

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
i2cid_to_modulename (u16 i2c_id)
{
	char *name = NULL;

	switch (i2c_id) {
	case I2C_DRIVERID_SAA7110:
		name = "saa7110";
		break;
	case I2C_DRIVERID_SAA7111A:
		name = "saa7111";
		break;
	case I2C_DRIVERID_SAA7114:
		name = "saa7114";
		break;
	case I2C_DRIVERID_SAA7185B:
		name = "saa7185";
		break;
	case I2C_DRIVERID_ADV7170:
		name = "adv7170";
		break;
	case I2C_DRIVERID_ADV7175:
		name = "adv7175";
		break;
	case I2C_DRIVERID_BT819:
		name = "bt819";
		break;
	case I2C_DRIVERID_BT856:
		name = "bt856";
		break;
	case I2C_DRIVERID_VPX3220:
		name = "vpx3220";
		break;
/*	case I2C_DRIVERID_VPX3224:
		name = "vpx3224";
		break;
	case I2C_DRIVERID_MSE3000:
		name = "mse3000";
		break;*/
	default:
		break;
	}

	return name;
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
	default:
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

static struct card_info zoran_cards[NUM_CARDS] __devinitdata = {
	{
		.type = DC10_old,
		.name = "DC10(old)",
		.i2c_decoder = I2C_DRIVERID_VPX3220,
		/*.i2c_encoder = I2C_DRIVERID_MSE3000,*/
		.video_codec = CODEC_TYPE_ZR36050,
		.video_vfe = CODEC_TYPE_ZR36016,

		.inputs = 3,
		.input = {
			{ 1, "Composite" },
			{ 2, "S-Video" },
			{ 0, "Internal/comp" }
		},
		.norms = 3,
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
		.i2c_decoder = I2C_DRIVERID_SAA7110,
		.i2c_encoder = I2C_DRIVERID_ADV7175,
		.video_codec = CODEC_TYPE_ZR36060,

		.inputs = 3,
		.input = {
				{ 0, "Composite" },
				{ 7, "S-Video" },
				{ 5, "Internal/comp" }
			},
		.norms = 3,
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
		.vendor_id = PCI_VENDOR_ID_MIRO,
		.device_id = PCI_DEVICE_ID_MIRO_DC10PLUS,
		.i2c_decoder = I2C_DRIVERID_SAA7110,
		.i2c_encoder = I2C_DRIVERID_ADV7175,
		.video_codec = CODEC_TYPE_ZR36060,

		.inputs = 3,
		.input = {
			{ 0, "Composite" },
			{ 7, "S-Video" },
			{ 5, "Internal/comp" }
		},
		.norms = 3,
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
		.i2c_decoder = I2C_DRIVERID_VPX3220,
		.i2c_encoder = I2C_DRIVERID_ADV7175,
		.video_codec = CODEC_TYPE_ZR36050,
		.video_vfe = CODEC_TYPE_ZR36016,

		.inputs = 3,
		.input = {
			{ 1, "Composite" },
			{ 2, "S-Video" },
			{ 0, "Internal/comp" }
		},
		.norms = 3,
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
		.vendor_id = PCI_VENDOR_ID_MIRO,
		.device_id = PCI_DEVICE_ID_MIRO_DC30PLUS,
		.i2c_decoder = I2C_DRIVERID_VPX3220,
		.i2c_encoder = I2C_DRIVERID_ADV7175,
		.video_codec = CODEC_TYPE_ZR36050,
		.video_vfe = CODEC_TYPE_ZR36016,

		.inputs = 3,
		.input = {
			{ 1, "Composite" },
			{ 2, "S-Video" },
			{ 0, "Internal/comp" }
		},
		.norms = 3,
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
		.i2c_decoder = I2C_DRIVERID_BT819,
		.i2c_encoder = I2C_DRIVERID_BT856,
		.video_codec = CODEC_TYPE_ZR36060,

		.inputs = 2,
		.input = {
			{ 0, "Composite" },
			{ 7, "S-Video" }
		},
		.norms = 2,
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
		.vendor_id = PCI_VENDOR_ID_ELECTRONICDESIGNGMBH,
		.device_id = PCI_DEVICE_ID_LML_33R10,
		.i2c_decoder = I2C_DRIVERID_SAA7114,
		.i2c_encoder = I2C_DRIVERID_ADV7170,
		.video_codec = CODEC_TYPE_ZR36060,

		.inputs = 2,
		.input = {
			{ 0, "Composite" },
			{ 7, "S-Video" }
		},
		.norms = 2,
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
		.vendor_id = PCI_VENDOR_ID_IOMEGA,
		.device_id = PCI_DEVICE_ID_IOMEGA_BUZ,
		.i2c_decoder = I2C_DRIVERID_SAA7111A,
		.i2c_encoder = I2C_DRIVERID_SAA7185B,
		.video_codec = CODEC_TYPE_ZR36060,

		.inputs = 2,
		.input = {
			{ 3, "Composite" },
			{ 7, "S-Video" }
		},
		.norms = 3,
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
		.vendor_id = -1,
		.device_id = -1,
		.i2c_decoder = I2C_DRIVERID_KS0127,
		.i2c_encoder = I2C_DRIVERID_BT866,
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
		.norms = 2,
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

static int
zoran_i2c_client_register (struct i2c_client *client)
{
	struct zoran *zr = (struct zoran *) i2c_get_adapdata(client->adapter);
	int res = 0;

	dprintk(2,
		KERN_DEBUG "%s: i2c_client_register() - driver id = %d\n",
		ZR_DEVNAME(zr), client->driver->id);

	mutex_lock(&zr->resource_lock);

	if (zr->user > 0) {
		/* we're already busy, so we keep a reference to
		 * them... Could do a lot of stuff here, but this
		 * is easiest. (Did I ever mention I'm a lazy ass?)
		 */
		res = -EBUSY;
		goto clientreg_unlock_and_return;
	}

	if (client->driver->id == zr->card.i2c_decoder)
		zr->decoder = client;
	else if (client->driver->id == zr->card.i2c_encoder)
		zr->encoder = client;
	else {
		res = -ENODEV;
		goto clientreg_unlock_and_return;
	}

clientreg_unlock_and_return:
	mutex_unlock(&zr->resource_lock);

	return res;
}

static int
zoran_i2c_client_unregister (struct i2c_client *client)
{
	struct zoran *zr = (struct zoran *) i2c_get_adapdata(client->adapter);
	int res = 0;

	dprintk(2, KERN_DEBUG "%s: i2c_client_unregister()\n", ZR_DEVNAME(zr));

	mutex_lock(&zr->resource_lock);

	if (zr->user > 0) {
		res = -EBUSY;
		goto clientunreg_unlock_and_return;
	}

	/* try to locate it */
	if (client == zr->encoder) {
		zr->encoder = NULL;
	} else if (client == zr->decoder) {
		zr->decoder = NULL;
		snprintf(ZR_DEVNAME(zr), sizeof(ZR_DEVNAME(zr)), "MJPEG[%d]", zr->id);
	}
clientunreg_unlock_and_return:
	mutex_unlock(&zr->resource_lock);
	return res;
}

static struct i2c_algo_bit_data zoran_i2c_bit_data_template = {
	.setsda = zoran_i2c_setsda,
	.setscl = zoran_i2c_setscl,
	.getsda = zoran_i2c_getsda,
	.getscl = zoran_i2c_getscl,
	.udelay = 10,
	.timeout = 100,
};

static struct i2c_adapter zoran_i2c_adapter_template = {
	.name = "zr36057",
	.id = I2C_HW_B_ZR36067,
	.algo = NULL,
	.client_register = zoran_i2c_client_register,
	.client_unregister = zoran_i2c_client_unregister,
};

static int
zoran_register_i2c (struct zoran *zr)
{
	memcpy(&zr->i2c_algo, &zoran_i2c_bit_data_template,
	       sizeof(struct i2c_algo_bit_data));
	zr->i2c_algo.data = zr;
	memcpy(&zr->i2c_adapter, &zoran_i2c_adapter_template,
	       sizeof(struct i2c_adapter));
	strncpy(I2C_NAME(&zr->i2c_adapter), ZR_DEVNAME(zr),
		sizeof(I2C_NAME(&zr->i2c_adapter)) - 1);
	i2c_set_adapdata(&zr->i2c_adapter, zr);
	zr->i2c_adapter.algo_data = &zr->i2c_algo;
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
			  struct zoran_jpg_settings *settings)
{
	int err = 0, err0 = 0;

	dprintk(4,
		KERN_DEBUG
		"%s: check_jpg_settings() - dec: %d, Hdcm: %d, Vdcm: %d, Tdcm: %d\n",
		ZR_DEVNAME(zr), settings->decimation, settings->HorDcm,
		settings->VerDcm, settings->TmpDcm);
	dprintk(4,
		KERN_DEBUG
		"%s: check_jpg_settings() - x: %d, y: %d, w: %d, y: %d\n",
		ZR_DEVNAME(zr), settings->img_x, settings->img_y,
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
				"%s: check_jpg_settings() - HDec by 4 is not supported on the DC10\n",
				ZR_DEVNAME(zr));
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
		    (zr->card.type == DC10_new || settings->HorDcm != 4))
			err0++;
		if (settings->VerDcm != 1 && settings->VerDcm != 2)
			err0++;
		if (settings->TmpDcm != 1 && settings->TmpDcm != 2)
			err0++;
		if (settings->field_per_buff != 1 &&
		    settings->field_per_buff != 2)
			err0++;
		if (settings->img_x < 0)
			err0++;
		if (settings->img_y < 0)
			err0++;
		if (settings->img_width < 0)
			err0++;
		if (settings->img_height < 0)
			err0++;
		if (settings->img_x + settings->img_width > BUZ_MAX_WIDTH)
			err0++;
		if (settings->img_y + settings->img_height >
		    BUZ_MAX_HEIGHT / 2)
			err0++;
		if (settings->HorDcm && settings->VerDcm) {
			if (settings->img_width %
			    (16 * settings->HorDcm) != 0)
				err0++;
			if (settings->img_height %
			    (8 * settings->VerDcm) != 0)
				err0++;
		}

		if (err0) {
			dprintk(1,
				KERN_ERR
				"%s: check_jpg_settings() - error in params for decimation = 0\n",
				ZR_DEVNAME(zr));
			err++;
		}
		break;
	default:
		dprintk(1,
			KERN_ERR
			"%s: check_jpg_settings() - decimation = %d, must be 0, 1, 2 or 4\n",
			ZR_DEVNAME(zr), settings->decimation);
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
	zr->v4l_settings.format = &zoran_formats[4];	/* YUY2 - YUV-4:2:2 packed */
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
	    JPEG_MARKER_DHT | JPEG_MARKER_DQT;
	i = zoran_check_jpg_settings(zr, &zr->jpg_settings);
	if (i)
		dprintk(1,
			KERN_ERR
			"%s: zoran_open_init_params() internal error\n",
			ZR_DEVNAME(zr));

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
	if (*zr_debug > 1)
		print_interrupts(zr);
	btwrite(icr, ZR36057_ICR);
}

static int __devinit
zr36057_init (struct zoran *zr)
{
	int j, err;
	int two = 2;
	int zero = 0;

	dprintk(1,
		KERN_INFO
		"%s: zr36057_init() - initializing card[%d], zr=%p\n",
		ZR_DEVNAME(zr), zr->id, zr);

	/* default setup of all parameters which will persist between opens */
	zr->user = 0;

	init_waitqueue_head(&zr->v4l_capq);
	init_waitqueue_head(&zr->jpg_capq);
	init_waitqueue_head(&zr->test_q);
	zr->jpg_buffers.allocated = 0;
	zr->v4l_buffers.allocated = 0;

	zr->buffer.base = (void *) vidmem;
	zr->buffer.width = 0;
	zr->buffer.height = 0;
	zr->buffer.depth = 0;
	zr->buffer.bytesperline = 0;

	/* Avoid nonsense settings from user for default input/norm */
	if (default_norm < VIDEO_MODE_PAL &&
	    default_norm > VIDEO_MODE_SECAM)
		default_norm = VIDEO_MODE_PAL;
	zr->norm = default_norm;
	if (!(zr->timing = zr->card.tvn[zr->norm])) {
		dprintk(1,
			KERN_WARNING
			"%s: zr36057_init() - default TV standard not supported by hardware. PAL will be used.\n",
			ZR_DEVNAME(zr));
		zr->norm = VIDEO_MODE_PAL;
		zr->timing = zr->card.tvn[zr->norm];
	}

	zr->input = default_input = (default_input ? 1 : 0);

	/* Should the following be reset at every open ? */
	zr->hue = 32768;
	zr->contrast = 32768;
	zr->saturation = 32768;
	zr->brightness = 32768;

	/* default setup (will be repeated at every open) */
	zoran_open_init_params(zr);

	/* allocate memory *before* doing anything to the hardware
	 * in case allocation fails */
	zr->stat_com = kzalloc(BUZ_NUM_STAT_COM * 4, GFP_KERNEL);
	zr->video_dev = kmalloc(sizeof(struct video_device), GFP_KERNEL);
	if (!zr->stat_com || !zr->video_dev) {
		dprintk(1,
			KERN_ERR
			"%s: zr36057_init() - kmalloc (STAT_COM) failed\n",
			ZR_DEVNAME(zr));
		err = -ENOMEM;
		goto exit_free;
	}
	for (j = 0; j < BUZ_NUM_STAT_COM; j++) {
		zr->stat_com[j] = 1;	/* mark as unavailable to zr36057 */
	}

	/*
	 *   Now add the template and register the device unit.
	 */
	memcpy(zr->video_dev, &zoran_template, sizeof(zoran_template));
	strcpy(zr->video_dev->name, ZR_DEVNAME(zr));
	err = video_register_device(zr->video_dev, VFL_TYPE_GRABBER, video_nr);
	if (err < 0)
		goto exit_unregister;

	zoran_init_hardware(zr);
	if (*zr_debug > 2)
		detect_guest_activity(zr);
	test_interrupts(zr);
	if (!pass_through) {
		decoder_command(zr, DECODER_ENABLE_OUTPUT, &zero);
		encoder_command(zr, ENCODER_SET_INPUT, &two);
	}

	zr->zoran_proc = NULL;
	zr->initialized = 1;
	return 0;

exit_unregister:
	zoran_unregister_i2c(zr);
exit_free:
	kfree(zr->stat_com);
	kfree(zr->video_dev);
	return err;
}

static void
zoran_release (struct zoran *zr)
{
	if (!zr->initialized)
		return;
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
		dprintk(1,
			KERN_ERR
			"%s: zoran_setup_videocodec() - no memory\n",
			ZR_DEVNAME(zr));
		return m;
	}

	m->magic = 0L; /* magic not used */
	m->type = VID_HARDWARE_ZR36067;
	m->flags = CODEC_FLAG_ENCODER | CODEC_FLAG_DECODER;
	strncpy(m->name, ZR_DEVNAME(zr), sizeof(m->name));
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

/*
 *   Scan for a Buz card (actually for the PCI contoler ZR36057),
 *   request the irq and map the io memory
 */
static int __devinit
find_zr36057 (void)
{
	unsigned char latency, need_latency;
	struct zoran *zr;
	struct pci_dev *dev = NULL;
	int result;
	struct videocodec_master *master_vfe = NULL;
	struct videocodec_master *master_codec = NULL;
	int card_num;
	char *i2c_enc_name, *i2c_dec_name, *codec_name, *vfe_name;

	zoran_num = 0;
	while (zoran_num < BUZ_MAX &&
	       (dev = pci_get_device(PCI_VENDOR_ID_ZORAN, PCI_DEVICE_ID_ZORAN_36057, dev)) != NULL) {
		card_num = card[zoran_num];
		zr = &zoran[zoran_num];
		memset(zr, 0, sizeof(struct zoran));	// Just in case if previous cycle failed
		zr->pci_dev = dev;
		//zr->zr36057_mem = NULL;
		zr->id = zoran_num;
		snprintf(ZR_DEVNAME(zr), sizeof(ZR_DEVNAME(zr)), "MJPEG[%u]", zr->id);
		spin_lock_init(&zr->spinlock);
		mutex_init(&zr->resource_lock);
		if (pci_enable_device(dev))
			continue;
		zr->zr36057_adr = pci_resource_start(zr->pci_dev, 0);
		pci_read_config_byte(zr->pci_dev, PCI_CLASS_REVISION,
				     &zr->revision);
		if (zr->revision < 2) {
			dprintk(1,
				KERN_INFO
				"%s: Zoran ZR36057 (rev %d) irq: %d, memory: 0x%08x.\n",
				ZR_DEVNAME(zr), zr->revision, zr->pci_dev->irq,
				zr->zr36057_adr);

			if (card_num == -1) {
				dprintk(1,
					KERN_ERR
					"%s: find_zr36057() - no card specified, please use the card=X insmod option\n",
					ZR_DEVNAME(zr));
				continue;
			}
		} else {
			int i;
			unsigned short ss_vendor, ss_device;

			ss_vendor = zr->pci_dev->subsystem_vendor;
			ss_device = zr->pci_dev->subsystem_device;
			dprintk(1,
				KERN_INFO
				"%s: Zoran ZR36067 (rev %d) irq: %d, memory: 0x%08x\n",
				ZR_DEVNAME(zr), zr->revision, zr->pci_dev->irq,
				zr->zr36057_adr);
			dprintk(1,
				KERN_INFO
				"%s: subsystem vendor=0x%04x id=0x%04x\n",
				ZR_DEVNAME(zr), ss_vendor, ss_device);
			if (card_num == -1) {
				dprintk(3,
					KERN_DEBUG
					"%s: find_zr36057() - trying to autodetect card type\n",
					ZR_DEVNAME(zr));
				for (i=0;i<NUM_CARDS;i++) {
					if (ss_vendor == zoran_cards[i].vendor_id &&
					    ss_device == zoran_cards[i].device_id) {
						dprintk(3,
							KERN_DEBUG
							"%s: find_zr36057() - card %s detected\n",
							ZR_DEVNAME(zr),
							zoran_cards[i].name);
						card_num = i;
						break;
					}
				}
				if (i == NUM_CARDS) {
					dprintk(1,
						KERN_ERR
						"%s: find_zr36057() - unknown card\n",
						ZR_DEVNAME(zr));
					continue;
				}
			}
		}

		if (card_num < 0 || card_num >= NUM_CARDS) {
			dprintk(2,
				KERN_ERR
				"%s: find_zr36057() - invalid cardnum %d\n",
				ZR_DEVNAME(zr), card_num);
			continue;
		}

		/* even though we make this a non pointer and thus
		 * theoretically allow for making changes to this struct
		 * on a per-individual card basis at runtime, this is
		 * strongly discouraged. This structure is intended to
		 * keep general card information, no settings or anything */
		zr->card = zoran_cards[card_num];
		snprintf(ZR_DEVNAME(zr), sizeof(ZR_DEVNAME(zr)),
			 "%s[%u]", zr->card.name, zr->id);

		zr->zr36057_mem = ioremap_nocache(zr->zr36057_adr, 0x1000);
		if (!zr->zr36057_mem) {
			dprintk(1,
				KERN_ERR
				"%s: find_zr36057() - ioremap failed\n",
				ZR_DEVNAME(zr));
			continue;
		}

		result = request_irq(zr->pci_dev->irq,
				     zoran_irq,
				     IRQF_SHARED | IRQF_DISABLED,
				     ZR_DEVNAME(zr),
				     (void *) zr);
		if (result < 0) {
			if (result == -EINVAL) {
				dprintk(1,
					KERN_ERR
					"%s: find_zr36057() - bad irq number or handler\n",
					ZR_DEVNAME(zr));
			} else if (result == -EBUSY) {
				dprintk(1,
					KERN_ERR
					"%s: find_zr36057() - IRQ %d busy, change your PnP config in BIOS\n",
					ZR_DEVNAME(zr), zr->pci_dev->irq);
			} else {
				dprintk(1,
					KERN_ERR
					"%s: find_zr36057() - can't assign irq, error code %d\n",
					ZR_DEVNAME(zr), result);
			}
			goto zr_unmap;
		}

		/* set PCI latency timer */
		pci_read_config_byte(zr->pci_dev, PCI_LATENCY_TIMER,
				     &latency);
		need_latency = zr->revision > 1 ? 32 : 48;
		if (latency != need_latency) {
			dprintk(2,
				KERN_INFO
				"%s: Changing PCI latency from %d to %d.\n",
				ZR_DEVNAME(zr), latency, need_latency);
			pci_write_config_byte(zr->pci_dev,
					      PCI_LATENCY_TIMER,
					      need_latency);
		}

		zr36057_restart(zr);
		/* i2c */
		dprintk(2, KERN_INFO "%s: Initializing i2c bus...\n",
			ZR_DEVNAME(zr));

		/* i2c decoder */
		if (decoder[zr->id] != -1) {
			i2c_dec_name = i2cid_to_modulename(decoder[zr->id]);
			zr->card.i2c_decoder = decoder[zr->id];
		} else if (zr->card.i2c_decoder != 0) {
			i2c_dec_name =
				i2cid_to_modulename(zr->card.i2c_decoder);
		} else {
			i2c_dec_name = NULL;
		}

		if (i2c_dec_name) {
			if ((result = request_module(i2c_dec_name)) < 0) {
				dprintk(1,
					KERN_ERR
					"%s: failed to load module %s: %d\n",
					ZR_DEVNAME(zr), i2c_dec_name, result);
			}
		}

		/* i2c encoder */
		if (encoder[zr->id] != -1) {
			i2c_enc_name = i2cid_to_modulename(encoder[zr->id]);
			zr->card.i2c_encoder = encoder[zr->id];
		} else if (zr->card.i2c_encoder != 0) {
			i2c_enc_name =
				i2cid_to_modulename(zr->card.i2c_encoder);
		} else {
			i2c_enc_name = NULL;
		}

		if (i2c_enc_name) {
			if ((result = request_module(i2c_enc_name)) < 0) {
				dprintk(1,
					KERN_ERR
					"%s: failed to load module %s: %d\n",
					ZR_DEVNAME(zr), i2c_enc_name, result);
			}
		}

		if (zoran_register_i2c(zr) < 0) {
			dprintk(1,
				KERN_ERR
				"%s: find_zr36057() - can't initialize i2c bus\n",
				ZR_DEVNAME(zr));
			goto zr_free_irq;
		}

		dprintk(2,
			KERN_INFO "%s: Initializing videocodec bus...\n",
			ZR_DEVNAME(zr));

		if (zr->card.video_codec != 0 &&
		    (codec_name =
		     codecid_to_modulename(zr->card.video_codec)) != NULL) {
			if ((result = request_module(codec_name)) < 0) {
				dprintk(1,
					KERN_ERR
					"%s: failed to load modules %s: %d\n",
					ZR_DEVNAME(zr), codec_name, result);
			}
		}
		if (zr->card.video_vfe != 0 &&
		    (vfe_name =
		     codecid_to_modulename(zr->card.video_vfe)) != NULL) {
			if ((result = request_module(vfe_name)) < 0) {
				dprintk(1,
					KERN_ERR
					"%s: failed to load modules %s: %d\n",
					ZR_DEVNAME(zr), vfe_name, result);
			}
		}

		/* reset JPEG codec */
		jpeg_codec_sleep(zr, 1);
		jpeg_codec_reset(zr);
		/* video bus enabled */
		/* display codec revision */
		if (zr->card.video_codec != 0) {
			master_codec = zoran_setup_videocodec(zr,
							      zr->card.video_codec);
			if (!master_codec)
				goto zr_unreg_i2c;
			zr->codec = videocodec_attach(master_codec);
			if (!zr->codec) {
				dprintk(1,
					KERN_ERR
					"%s: find_zr36057() - no codec found\n",
					ZR_DEVNAME(zr));
				goto zr_free_codec;
			}
			if (zr->codec->type != zr->card.video_codec) {
				dprintk(1,
					KERN_ERR
					"%s: find_zr36057() - wrong codec\n",
					ZR_DEVNAME(zr));
				goto zr_detach_codec;
			}
		}
		if (zr->card.video_vfe != 0) {
			master_vfe = zoran_setup_videocodec(zr,
							    zr->card.video_vfe);
			if (!master_vfe)
				goto zr_detach_codec;
			zr->vfe = videocodec_attach(master_vfe);
			if (!zr->vfe) {
				dprintk(1,
					KERN_ERR
					"%s: find_zr36057() - no VFE found\n",
					ZR_DEVNAME(zr));
				goto zr_free_vfe;
			}
			if (zr->vfe->type != zr->card.video_vfe) {
				dprintk(1,
					KERN_ERR
					"%s: find_zr36057() = wrong VFE\n",
					ZR_DEVNAME(zr));
				goto zr_detach_vfe;
			}
		}
		/* Success so keep the pci_dev referenced */
		pci_dev_get(zr->pci_dev);
		zoran_num++;
		continue;

		// Init errors
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
		continue;
	}
	if (dev)	/* Clean up ref count on early exit */
		pci_dev_put(dev);

	if (zoran_num == 0) {
		dprintk(1, KERN_INFO "No known MJPEG cards found.\n");
	}
	return zoran_num;
}

static int __init
init_dc10_cards (void)
{
	int i;

	memset(zoran, 0, sizeof(zoran));
	printk(KERN_INFO "Zoran MJPEG board driver version %d.%d.%d\n",
	       MAJOR_VERSION, MINOR_VERSION, RELEASE_VERSION);

	/* Look for cards */
	if (find_zr36057() < 0) {
		return -EIO;
	}
	if (zoran_num == 0)
		return -ENODEV;
	dprintk(1, KERN_INFO "%s: %d card(s) found\n", ZORAN_NAME,
		zoran_num);
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

	/* random nonsense */
	dprintk(5, KERN_DEBUG "Jotti is een held!\n");

	/* some mainboards might not do PCI-PCI data transfer well */
	if (pci_pci_problems & (PCIPCI_FAIL|PCIAGP_FAIL|PCIPCI_ALIMAGIK)) {
		dprintk(1,
			KERN_WARNING
			"%s: chipset does not support reliable PCI-PCI DMA\n",
			ZORAN_NAME);
	}

	/* take care of Natoma chipset and a revision 1 zr36057 */
	for (i = 0; i < zoran_num; i++) {
		struct zoran *zr = &zoran[i];

		if ((pci_pci_problems & PCIPCI_NATOMA) && zr->revision <= 1) {
			zr->jpg_buffers.need_contiguous = 1;
			dprintk(1,
				KERN_INFO
				"%s: ZR36057/Natoma bug, max. buffer size is 128K\n",
				ZR_DEVNAME(zr));
		}

		if (zr36057_init(zr) < 0) {
			for (i = 0; i < zoran_num; i++)
				zoran_release(&zoran[i]);
			return -EIO;
		}
		zoran_proc_init(zr);
	}

	return 0;
}

static void __exit
unload_dc10_cards (void)
{
	int i;

	for (i = 0; i < zoran_num; i++)
		zoran_release(&zoran[i]);
}

module_init(init_dc10_cards);
module_exit(unload_dc10_cards);

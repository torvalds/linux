// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Zoran zr36057/zr36067 PCI controller driver, for the
 * Pinnacle/Miro DC10/DC10+/DC30/DC30+, Iomega Buz, Linux
 * Media Labs LML33/LML33R10.
 *
 * This part handles card-specific data and detection
 *
 * Copyright (C) 2000 Serguei Miridonov <mirsev@cicese.mx>
 */

#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>

#include <linux/i2c.h>
#include <linux/i2c-algo-bit.h>
#include <linux/videodev2.h>
#include <linux/spinlock.h>

#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <media/v4l2-common.h>
#include <media/i2c/bt819.h>

#include "videocodec.h"
#include "zoran.h"
#include "zoran_card.h"
#include "zoran_device.h"

extern const struct zoran_format zoran_formats[];

static int card[BUZ_MAX] = { [0 ... (BUZ_MAX - 1)] = -1 };
module_param_array(card, int, NULL, 0444);
MODULE_PARM_DESC(card, "Card type");

/*
 * The video mem address of the video card. The driver has a little database
 * for some videocards to determine it from there. If your video card is not
 * in there you have either to give it to the driver as a parameter or set
 * in a VIDIOCSFBUF ioctl
 */

static unsigned long vidmem;	/* default = 0 - Video memory base address */
module_param_hw(vidmem, ulong, iomem, 0444);
MODULE_PARM_DESC(vidmem, "Default video memory base address");

/* Default input and video norm at startup of the driver. */

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
static int video_nr[BUZ_MAX] = { [0 ... (BUZ_MAX - 1)] = -1 };
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

/* 1=Pass through TV signal when device is not used */
/* 0=Show color bar when device is not used (LML33: only if lml33dpath=1) */
int pass_through;
module_param(pass_through, int, 0644);
MODULE_PARM_DESC(pass_through,
		 "Pass TV signal through to TV-out when idling");

int zr36067_debug = 1;
module_param_named(debug, zr36067_debug, int, 0644);
MODULE_PARM_DESC(debug, "Debug level (0-5)");

#define ZORAN_VERSION "0.10.1"

MODULE_DESCRIPTION("Zoran-36057/36067 JPEG codec driver");
MODULE_AUTHOR("Serguei Miridonov");
MODULE_LICENSE("GPL");
MODULE_VERSION(ZORAN_VERSION);

#define ZR_DEVICE(subven, subdev, data)	{ \
	.vendor = PCI_VENDOR_ID_ZORAN, .device = PCI_DEVICE_ID_ZORAN_36057, \
	.subvendor = (subven), .subdevice = (subdev), .driver_data = (data) }

static const struct pci_device_id zr36067_pci_tbl[] = {
	ZR_DEVICE(PCI_VENDOR_ID_MIRO, PCI_DEVICE_ID_MIRO_DC10PLUS, DC10_PLUS),
	ZR_DEVICE(PCI_VENDOR_ID_MIRO, PCI_DEVICE_ID_MIRO_DC30PLUS, DC30_PLUS),
	ZR_DEVICE(PCI_VENDOR_ID_ELECTRONICDESIGNGMBH, PCI_DEVICE_ID_LML_33R10, LML33R10),
	ZR_DEVICE(PCI_VENDOR_ID_IOMEGA, PCI_DEVICE_ID_IOMEGA_BUZ, BUZ),
	ZR_DEVICE(PCI_ANY_ID, PCI_ANY_ID, NUM_CARDS),
	{0}
};
MODULE_DEVICE_TABLE(pci, zr36067_pci_tbl);

static unsigned int zoran_num;		/* number of cards found */

/* videocodec bus functions ZR36060 */
static u32 zr36060_read(struct videocodec *codec, u16 reg)
{
	struct zoran *zr = (struct zoran *)codec->master_data->data;
	__u32 data;

	if (post_office_wait(zr) || post_office_write(zr, 0, 1, reg >> 8) ||
	    post_office_write(zr, 0, 2, reg & 0xff))
		return -1;

	data = post_office_read(zr, 0, 3) & 0xff;
	return data;
}

static void zr36060_write(struct videocodec *codec, u16 reg, u32 val)
{
	struct zoran *zr = (struct zoran *)codec->master_data->data;

	if (post_office_wait(zr) || post_office_write(zr, 0, 1, reg >> 8) ||
	    post_office_write(zr, 0, 2, reg & 0xff))
		return;

	post_office_write(zr, 0, 3, val & 0xff);
}

/* videocodec bus functions ZR36050 */
static u32 zr36050_read(struct videocodec *codec, u16 reg)
{
	struct zoran *zr = (struct zoran *)codec->master_data->data;
	__u32 data;

	if (post_office_wait(zr) || post_office_write(zr, 1, 0, reg >> 2))	// reg. HIGHBYTES
		return -1;

	data = post_office_read(zr, 0, reg & 0x03) & 0xff;	// reg. LOWBYTES + read
	return data;
}

static void zr36050_write(struct videocodec *codec, u16 reg, u32 val)
{
	struct zoran *zr = (struct zoran *)codec->master_data->data;

	if (post_office_wait(zr) || post_office_write(zr, 1, 0, reg >> 2))	// reg. HIGHBYTES
		return;

	post_office_write(zr, 0, reg & 0x03, val & 0xff);	// reg. LOWBYTES + wr. data
}

/* videocodec bus functions ZR36016 */
static u32 zr36016_read(struct videocodec *codec, u16 reg)
{
	struct zoran *zr = (struct zoran *)codec->master_data->data;
	__u32 data;

	if (post_office_wait(zr))
		return -1;

	data = post_office_read(zr, 2, reg & 0x03) & 0xff;	// read
	return data;
}

/* hack for in zoran_device.c */
void zr36016_write(struct videocodec *codec, u16 reg, u32 val)
{
	struct zoran *zr = (struct zoran *)codec->master_data->data;

	if (post_office_wait(zr))
		return;

	post_office_write(zr, 2, reg & 0x03, val & 0x0ff);	// wr. data
}

/*
 * Board specific information
 */

static void dc10_init(struct zoran *zr)
{
	pci_dbg(zr->pci_dev, "%s\n", __func__);

	/* Pixel clock selection */
	GPIO(zr, 4, 0);
	GPIO(zr, 5, 1);
	/* Enable the video bus sync signals */
	GPIO(zr, 7, 0);
}

static void dc10plus_init(struct zoran *zr)
{
	pci_dbg(zr->pci_dev, "%s\n", __func__);
}

static void buz_init(struct zoran *zr)
{
	pci_dbg(zr->pci_dev, "%s\n", __func__);

	/* some stuff from Iomega */
	pci_write_config_dword(zr->pci_dev, 0xfc, 0x90680f15);
	pci_write_config_dword(zr->pci_dev, 0x0c, 0x00012020);
	pci_write_config_dword(zr->pci_dev, 0xe8, 0xc0200000);
}

static void lml33_init(struct zoran *zr)
{
	pci_dbg(zr->pci_dev, "%s\n", __func__);

	GPIO(zr, 2, 1);		// Set Composite input/output
}

static void avs6eyes_init(struct zoran *zr)
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

static const char *codecid_to_modulename(u16 codecid)
{
	const char *name = NULL;

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
//      u16 wt, wa, h_start, h_sync_start, ht, ha, v_start;
// };

static const struct tvnorm f50sqpixel = { 944, 768, 83, 880, 625, 576, 16 };
static const struct tvnorm f60sqpixel = { 780, 640, 51, 716, 525, 480, 12 };
static const struct tvnorm f50ccir601 = { 864, 720, 75, 804, 625, 576, 18 };
static const struct tvnorm f60ccir601 = { 858, 720, 57, 788, 525, 480, 16 };

static const struct tvnorm f50ccir601_lml33 = { 864, 720, 75 + 34, 804, 625, 576, 18 };
static const struct tvnorm f60ccir601_lml33 = { 858, 720, 57 + 34, 788, 525, 480, 16 };

/* The DC10 (57/16/50) uses VActive as HSync, so h_start must be 0 */
static const struct tvnorm f50sqpixel_dc10 = { 944, 768, 0, 880, 625, 576, 0 };
static const struct tvnorm f60sqpixel_dc10 = { 780, 640, 0, 716, 525, 480, 12 };

/*
 * FIXME: I cannot swap U and V in saa7114, so i do one pixel left shift in zoran (75 -> 74)
 * (Maxim Yevtyushkin <max@linuxmedialabs.com>)
 */
static const struct tvnorm f50ccir601_lm33r10 = { 864, 720, 74 + 54, 804, 625, 576, 18 };
static const struct tvnorm f60ccir601_lm33r10 = { 858, 720, 56 + 54, 788, 525, 480, 16 };

/*
 * FIXME: The ks0127 seem incapable of swapping U and V, too, which is why I copy Maxim's left
 * shift hack for the 6 Eyes.
 *
 * Christer's driver used the unshifted norms, though...
 * /Sam
 */
static const struct tvnorm f50ccir601_avs6eyes = { 864, 720, 74, 804, 625, 576, 18 };
static const struct tvnorm f60ccir601_avs6eyes = { 858, 720, 56, 788, 525, 480, 16 };

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

static struct card_info zoran_cards[NUM_CARDS] = {
	{
		.type = DC10_OLD,
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
		.norms = V4L2_STD_NTSC | V4L2_STD_PAL | V4L2_STD_SECAM,
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
		.type = DC10_NEW,
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
		.norms = V4L2_STD_NTSC | V4L2_STD_PAL | V4L2_STD_SECAM,
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
		.type = DC10_PLUS,
		.name = "DC10_PLUS",
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
		.norms = V4L2_STD_NTSC | V4L2_STD_PAL | V4L2_STD_SECAM,
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
		.norms = V4L2_STD_NTSC | V4L2_STD_PAL | V4L2_STD_SECAM,
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
		.type = DC30_PLUS,
		.name = "DC30_PLUS",
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
		.norms = V4L2_STD_NTSC | V4L2_STD_PAL | V4L2_STD_SECAM,
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
		.norms = V4L2_STD_NTSC | V4L2_STD_PAL,
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
		.norms = V4L2_STD_NTSC | V4L2_STD_PAL,
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
		.norms = V4L2_STD_NTSC | V4L2_STD_PAL | V4L2_STD_SECAM,
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
/* AverMedia chose not to brand the 6-Eyes. Thus it can't be autodetected, and requires card=x. */
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
		.norms = V4L2_STD_NTSC | V4L2_STD_PAL,
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
static int zoran_i2c_getsda(void *data)
{
	struct zoran *zr = (struct zoran *)data;

	return (btread(ZR36057_I2CBR) >> 1) & 1;
}

static int zoran_i2c_getscl(void *data)
{
	struct zoran *zr = (struct zoran *)data;

	return btread(ZR36057_I2CBR) & 1;
}

static void zoran_i2c_setsda(void *data, int state)
{
	struct zoran *zr = (struct zoran *)data;

	if (state)
		zr->i2cbr |= 2;
	else
		zr->i2cbr &= ~2;
	btwrite(zr->i2cbr, ZR36057_I2CBR);
}

static void zoran_i2c_setscl(void *data, int state)
{
	struct zoran *zr = (struct zoran *)data;

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

static int zoran_register_i2c(struct zoran *zr)
{
	zr->i2c_algo = zoran_i2c_bit_data_template;
	zr->i2c_algo.data = zr;
	strscpy(zr->i2c_adapter.name, ZR_DEVNAME(zr),
		sizeof(zr->i2c_adapter.name));
	i2c_set_adapdata(&zr->i2c_adapter, &zr->v4l2_dev);
	zr->i2c_adapter.algo_data = &zr->i2c_algo;
	zr->i2c_adapter.dev.parent = &zr->pci_dev->dev;
	return i2c_bit_add_bus(&zr->i2c_adapter);
}

static void zoran_unregister_i2c(struct zoran *zr)
{
	i2c_del_adapter(&zr->i2c_adapter);
}

/* Check a zoran_params struct for correctness, insert default params */
int zoran_check_jpg_settings(struct zoran *zr,
			     struct zoran_jpg_settings *settings, int try)
{
	int err = 0, err0 = 0;

	pci_dbg(zr->pci_dev, "%s - dec: %d, Hdcm: %d, Vdcm: %d, Tdcm: %d\n",
		__func__, settings->decimation, settings->hor_dcm,
		settings->ver_dcm, settings->tmp_dcm);
	pci_dbg(zr->pci_dev, "%s - x: %d, y: %d, w: %d, y: %d\n", __func__,
		settings->img_x, settings->img_y,
		settings->img_width, settings->img_height);
	/* Check decimation, set default values for decimation = 1, 2, 4 */
	switch (settings->decimation) {
	case 1:

		settings->hor_dcm = 1;
		settings->ver_dcm = 1;
		settings->tmp_dcm = 1;
		settings->field_per_buff = 2;
		settings->img_x = 0;
		settings->img_y = 0;
		settings->img_width = BUZ_MAX_WIDTH;
		settings->img_height = BUZ_MAX_HEIGHT / 2;
		break;
	case 2:

		settings->hor_dcm = 2;
		settings->ver_dcm = 1;
		settings->tmp_dcm = 2;
		settings->field_per_buff = 1;
		settings->img_x = (BUZ_MAX_WIDTH == 720) ? 8 : 0;
		settings->img_y = 0;
		settings->img_width =
		    (BUZ_MAX_WIDTH == 720) ? 704 : BUZ_MAX_WIDTH;
		settings->img_height = BUZ_MAX_HEIGHT / 2;
		break;
	case 4:

		if (zr->card.type == DC10_NEW) {
			pci_dbg(zr->pci_dev, "%s - HDec by 4 is not supported on the DC10\n", __func__);
			err0++;
			break;
		}

		settings->hor_dcm = 4;
		settings->ver_dcm = 2;
		settings->tmp_dcm = 2;
		settings->field_per_buff = 1;
		settings->img_x = (BUZ_MAX_WIDTH == 720) ? 8 : 0;
		settings->img_y = 0;
		settings->img_width =
		    (BUZ_MAX_WIDTH == 720) ? 704 : BUZ_MAX_WIDTH;
		settings->img_height = BUZ_MAX_HEIGHT / 2;
		break;
	case 0:

		/* We have to check the data the user has set */

		if (settings->hor_dcm != 1 && settings->hor_dcm != 2 &&
		    (zr->card.type == DC10_NEW || settings->hor_dcm != 4)) {
			settings->hor_dcm = clamp(settings->hor_dcm, 1, 2);
			err0++;
		}
		if (settings->ver_dcm != 1 && settings->ver_dcm != 2) {
			settings->ver_dcm = clamp(settings->ver_dcm, 1, 2);
			err0++;
		}
		if (settings->tmp_dcm != 1 && settings->tmp_dcm != 2) {
			settings->tmp_dcm = clamp(settings->tmp_dcm, 1, 2);
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
		if (settings->img_width % (16 * settings->hor_dcm) != 0) {
			settings->img_width -= settings->img_width % (16 * settings->hor_dcm);
			if (settings->img_width == 0)
				settings->img_width = 16 * settings->hor_dcm;
			err0++;
		}
		if (settings->img_height % (8 * settings->ver_dcm) != 0) {
			settings->img_height -= settings->img_height % (8 * settings->ver_dcm);
			if (settings->img_height == 0)
				settings->img_height = 8 * settings->ver_dcm;
			err0++;
		}

		if (!try && err0) {
			pci_err(zr->pci_dev, "%s - error in params for decimation = 0\n", __func__);
			err++;
		}
		break;
	default:
		pci_err(zr->pci_dev, "%s - decimation = %d, must be 0, 1, 2 or 4\n",
			__func__, settings->decimation);
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

static int zoran_init_video_device(struct zoran *zr, struct video_device *video_dev, int dir)
{
	int err;

	/* Now add the template and register the device unit. */
	*video_dev = zoran_template;
	video_dev->v4l2_dev = &zr->v4l2_dev;
	video_dev->lock = &zr->lock;
	video_dev->device_caps = V4L2_CAP_STREAMING | dir;

	strscpy(video_dev->name, ZR_DEVNAME(zr), sizeof(video_dev->name));
	/*
	 * It's not a mem2mem device, but you can both capture and output from one and the same
	 * device. This should really be split up into two device nodes, but that's a job for
	 * another day.
	 */
	video_dev->vfl_dir = VFL_DIR_M2M;
	zoran_queue_init(zr, &zr->vq, V4L2_BUF_TYPE_VIDEO_CAPTURE);

	err = video_register_device(video_dev, VFL_TYPE_VIDEO, video_nr[zr->id]);
	if (err < 0)
		return err;
	video_set_drvdata(video_dev, zr);
	return 0;
}

static void zoran_exit_video_devices(struct zoran *zr)
{
	video_unregister_device(zr->video_dev);
	kfree(zr->video_dev);
}

static int zoran_init_video_devices(struct zoran *zr)
{
	int err;

	zr->video_dev = video_device_alloc();
	if (!zr->video_dev)
		return -ENOMEM;

	err = zoran_init_video_device(zr, zr->video_dev, V4L2_CAP_VIDEO_CAPTURE);
	if (err)
		kfree(zr->video_dev);
	return err;
}

void zoran_open_init_params(struct zoran *zr)
{
	int i;

	zr->v4l_settings.width = 192;
	zr->v4l_settings.height = 144;
	zr->v4l_settings.format = &zoran_formats[7];	/* YUY2 - YUV-4:2:2 packed */
	zr->v4l_settings.bytesperline = zr->v4l_settings.width *
		((zr->v4l_settings.format->depth + 7) / 8);

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
		pci_err(zr->pci_dev, "%s internal error\n", __func__);

	zr->buffer_size = zr->v4l_settings.bytesperline * zr->v4l_settings.height;

	clear_interrupt_counters(zr);
}

static int zr36057_init(struct zoran *zr)
{
	int j, err;

	pci_info(zr->pci_dev, "initializing card[%d]\n", zr->id);

	/* Avoid nonsense settings from user for default input/norm */
	if (default_norm < 0 || default_norm > 2)
		default_norm = 0;
	if (default_norm == 0) {
		zr->norm = V4L2_STD_PAL;
		zr->timing = zr->card.tvn[ZR_NORM_PAL];
	} else if (default_norm == 1) {
		zr->norm = V4L2_STD_NTSC;
		zr->timing = zr->card.tvn[ZR_NORM_NTSC];
	} else {
		zr->norm = V4L2_STD_SECAM;
		zr->timing = zr->card.tvn[ZR_NORM_SECAM];
	}
	if (!zr->timing) {
		pci_warn(zr->pci_dev, "%s - default TV standard not supported by hardware. PAL will be used.\n", __func__);
		zr->norm = V4L2_STD_PAL;
		zr->timing = zr->card.tvn[ZR_NORM_PAL];
	}

	if (default_input > zr->card.inputs - 1) {
		pci_warn(zr->pci_dev, "default_input value %d out of range (0-%d)\n",
			 default_input, zr->card.inputs - 1);
		default_input = 0;
	}
	zr->input = default_input;

	/* default setup (will be repeated at every open) */
	zoran_open_init_params(zr);

	/* allocate memory *before* doing anything to the hardware in case allocation fails */
	zr->stat_com = dma_alloc_coherent(&zr->pci_dev->dev,
					  BUZ_NUM_STAT_COM * sizeof(u32),
					  &zr->p_sc, GFP_KERNEL);
	if (!zr->stat_com) {
		return -ENOMEM;
	}
	for (j = 0; j < BUZ_NUM_STAT_COM; j++)
		zr->stat_com[j] = cpu_to_le32(1); /* mark as unavailable to zr36057 */

	zr->stat_comb = dma_alloc_coherent(&zr->pci_dev->dev,
					   BUZ_NUM_STAT_COM * sizeof(u32) * 2,
					   &zr->p_scb, GFP_KERNEL);
	if (!zr->stat_comb) {
		err = -ENOMEM;
		goto exit_statcom;
	}

	err = zoran_init_video_devices(zr);
	if (err)
		goto exit_statcomb;

	zoran_init_hardware(zr);
	if (!pass_through) {
		decoder_call(zr, video, s_stream, 0);
		encoder_call(zr, video, s_routing, 2, 0, 0);
	}

	zr->initialized = 1;
	return 0;

exit_statcomb:
	dma_free_coherent(&zr->pci_dev->dev, BUZ_NUM_STAT_COM * sizeof(u32) * 2, zr->stat_comb, zr->p_scb);
exit_statcom:
	dma_free_coherent(&zr->pci_dev->dev, BUZ_NUM_STAT_COM * sizeof(u32), zr->stat_com, zr->p_sc);
	return err;
}

static void zoran_remove(struct pci_dev *pdev)
{
	struct v4l2_device *v4l2_dev = dev_get_drvdata(&pdev->dev);
	struct zoran *zr = to_zoran(v4l2_dev);

	if (!zr->initialized)
		goto exit_free;

	zoran_queue_exit(zr);

	/* unregister videocodec bus */
	if (zr->codec)
		videocodec_detach(zr->codec);
	if (zr->vfe)
		videocodec_detach(zr->vfe);

	/* unregister i2c bus */
	zoran_unregister_i2c(zr);
	/* disable PCI bus-mastering */
	zoran_set_pci_master(zr, 0);
	/* put chip into reset */
	btwrite(0, ZR36057_SPGPPCR);
	pci_free_irq(zr->pci_dev, 0, zr);
	/* unmap and free memory */
	dma_free_coherent(&zr->pci_dev->dev, BUZ_NUM_STAT_COM * sizeof(u32), zr->stat_com, zr->p_sc);
	dma_free_coherent(&zr->pci_dev->dev, BUZ_NUM_STAT_COM * sizeof(u32) * 2, zr->stat_comb, zr->p_scb);
	pci_release_regions(pdev);
	pci_disable_device(zr->pci_dev);
	zoran_exit_video_devices(zr);
exit_free:
	v4l2_ctrl_handler_free(&zr->hdl);
	v4l2_device_unregister(&zr->v4l2_dev);
}

void zoran_vdev_release(struct video_device *vdev)
{
	kfree(vdev);
}

static struct videocodec_master *zoran_setup_videocodec(struct zoran *zr,
							int type)
{
	struct videocodec_master *m = NULL;

	m = devm_kmalloc(&zr->pci_dev->dev, sizeof(*m), GFP_KERNEL);
	if (!m)
		return m;

	/*
	 * magic and type are unused for master struct. Makes sense only at codec structs.
	 * In the past, .type were initialized to the old V4L1 .hardware value,
	 * as VID_HARDWARE_ZR36067
	 */
	m->magic = 0L;
	m->type = 0;

	m->flags = CODEC_FLAG_ENCODER | CODEC_FLAG_DECODER;
	strscpy(m->name, ZR_DEVNAME(zr), sizeof(m->name));
	m->data = zr;

	switch (type) {
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

	/*
	 * Bt819 needs to reset its FIFO buffer using #FRST pin and
	 * LML33 card uses GPIO(7) for that.
	 */
	if (cmd == BT819_FIFO_RESET_LOW)
		GPIO(zr, 7, 0);
	else if (cmd == BT819_FIFO_RESET_HIGH)
		GPIO(zr, 7, 1);
}

static int zoran_video_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct zoran *zr = container_of(ctrl->handler, struct zoran, hdl);

	switch (ctrl->id) {
	case V4L2_CID_JPEG_COMPRESSION_QUALITY:
		zr->jpg_settings.jpg_comp.quality = ctrl->val;
		return zoran_check_jpg_settings(zr, &zr->jpg_settings, 0);
	default:
		return -EINVAL;
	}

	return 0;
}

static const struct v4l2_ctrl_ops zoran_video_ctrl_ops = {
	.s_ctrl = zoran_video_set_ctrl,
};

/*
 *   Scan for a Buz card (actually for the PCI controller ZR36057),
 *   request the irq and map the io memory
 */
static int zoran_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	unsigned char latency, need_latency;
	struct zoran *zr;
	int result;
	struct videocodec_master *master_vfe = NULL;
	struct videocodec_master *master_codec = NULL;
	int card_num;
	const char *codec_name, *vfe_name;
	unsigned int nr;
	int err;

	err = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
	if (err)
		return err;
	err = vb2_dma_contig_set_max_seg_size(&pdev->dev, U32_MAX);
	if (err)
		return err;

	nr = zoran_num++;
	if (nr >= BUZ_MAX) {
		pci_err(pdev, "driver limited to %d card(s) maximum\n", BUZ_MAX);
		return -ENOENT;
	}

	zr = devm_kzalloc(&pdev->dev, sizeof(*zr), GFP_KERNEL);
	if (!zr)
		return -ENOMEM;

	zr->v4l2_dev.notify = zoran_subdev_notify;
	if (v4l2_device_register(&pdev->dev, &zr->v4l2_dev))
		goto zr_free_mem;
	zr->pci_dev = pdev;
	zr->id = nr;
	snprintf(ZR_DEVNAME(zr), sizeof(ZR_DEVNAME(zr)), "MJPEG[%u]", zr->id);
	if (v4l2_ctrl_handler_init(&zr->hdl, 10))
		goto zr_unreg;
	zr->v4l2_dev.ctrl_handler = &zr->hdl;
	v4l2_ctrl_new_std(&zr->hdl, &zoran_video_ctrl_ops,
			  V4L2_CID_JPEG_COMPRESSION_QUALITY, 0,
			  100, 1, 50);
	spin_lock_init(&zr->spinlock);
	mutex_init(&zr->lock);
	if (pci_enable_device(pdev))
		goto zr_unreg;
	zr->revision = zr->pci_dev->revision;

	pci_info(zr->pci_dev, "Zoran ZR360%c7 (rev %d), irq: %d, memory: 0x%08llx\n",
		 zr->revision < 2 ? '5' : '6', zr->revision,
		 zr->pci_dev->irq, (uint64_t)pci_resource_start(zr->pci_dev, 0));
	if (zr->revision >= 2)
		pci_info(zr->pci_dev, "Subsystem vendor=0x%04x id=0x%04x\n",
			 zr->pci_dev->subsystem_vendor, zr->pci_dev->subsystem_device);

	/* Use auto-detected card type? */
	if (card[nr] == -1) {
		if (zr->revision < 2) {
			pci_err(pdev, "No card type specified, please use the card=X module parameter\n");
			pci_err(pdev, "It is not possible to auto-detect ZR36057 based cards\n");
			goto zr_unreg;
		}

		card_num = ent->driver_data;
		if (card_num >= NUM_CARDS) {
			pci_err(pdev, "Unknown card, try specifying card=X module parameter\n");
			goto zr_unreg;
		}
		pci_info(zr->pci_dev, "%s() - card %s detected\n", __func__, zoran_cards[card_num].name);
	} else {
		card_num = card[nr];
		if (card_num >= NUM_CARDS || card_num < 0) {
			pci_err(pdev, "User specified card type %d out of range (0 .. %d)\n",
				card_num, NUM_CARDS - 1);
			goto zr_unreg;
		}
	}

	/*
	 * even though we make this a non pointer and thus
	 * theoretically allow for making changes to this struct
	 * on a per-individual card basis at runtime, this is
	 * strongly discouraged. This structure is intended to
	 * keep general card information, no settings or anything
	 */
	zr->card = zoran_cards[card_num];
	snprintf(ZR_DEVNAME(zr), sizeof(ZR_DEVNAME(zr)), "%s[%u]",
		 zr->card.name, zr->id);

	err = pci_request_regions(pdev, ZR_DEVNAME(zr));
	if (err)
		goto zr_unreg;

	zr->zr36057_mem = devm_ioremap(&pdev->dev, pci_resource_start(pdev, 0), pci_resource_len(pdev, 0));
	if (!zr->zr36057_mem) {
		pci_err(pdev, "%s() - ioremap failed\n", __func__);
		goto zr_pci_release;
	}

	result = pci_request_irq(pdev, 0, zoran_irq, NULL, zr, ZR_DEVNAME(zr));
	if (result < 0) {
		if (result == -EINVAL) {
			pci_err(pdev, "%s - bad IRQ number or handler\n", __func__);
		} else if (result == -EBUSY) {
			pci_err(pdev, "%s - IRQ %d busy, change your PnP config in BIOS\n",
				__func__, zr->pci_dev->irq);
		} else {
			pci_err(pdev, "%s - cannot assign IRQ, error code %d\n", __func__, result);
		}
		goto zr_pci_release;
	}

	/* set PCI latency timer */
	pci_read_config_byte(zr->pci_dev, PCI_LATENCY_TIMER,
			     &latency);
	need_latency = zr->revision > 1 ? 32 : 48;
	if (latency != need_latency) {
		pci_info(zr->pci_dev, "Changing PCI latency from %d to %d\n", latency, need_latency);
		pci_write_config_byte(zr->pci_dev, PCI_LATENCY_TIMER, need_latency);
	}

	zr36057_restart(zr);
	/* i2c */
	pci_info(zr->pci_dev, "Initializing i2c bus...\n");

	if (zoran_register_i2c(zr) < 0) {
		pci_err(pdev, "%s - can't initialize i2c bus\n", __func__);
		goto zr_free_irq;
	}

	zr->decoder = v4l2_i2c_new_subdev(&zr->v4l2_dev, &zr->i2c_adapter,
					  zr->card.i2c_decoder, 0,
					  zr->card.addrs_decoder);

	if (zr->card.i2c_encoder)
		zr->encoder = v4l2_i2c_new_subdev(&zr->v4l2_dev, &zr->i2c_adapter,
						  zr->card.i2c_encoder, 0,
						  zr->card.addrs_encoder);

	pci_info(zr->pci_dev, "Initializing videocodec bus...\n");

	if (zr->card.video_codec) {
		codec_name = codecid_to_modulename(zr->card.video_codec);
		if (codec_name) {
			result = request_module(codec_name);
			if (result)
				pci_err(pdev, "failed to load modules %s: %d\n", codec_name, result);
		}
	}
	if (zr->card.video_vfe) {
		vfe_name = codecid_to_modulename(zr->card.video_vfe);
		if (vfe_name) {
			result = request_module(vfe_name);
			if (result < 0)
				pci_err(pdev, "failed to load modules %s: %d\n", vfe_name, result);
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
			pci_err(pdev, "%s - no codec found\n", __func__);
			goto zr_unreg_i2c;
		}
		if (zr->codec->type != zr->card.video_codec) {
			pci_err(pdev, "%s - wrong codec\n", __func__);
			goto zr_detach_codec;
		}
	}
	if (zr->card.video_vfe != 0) {
		master_vfe = zoran_setup_videocodec(zr, zr->card.video_vfe);
		if (!master_vfe)
			goto zr_detach_codec;
		zr->vfe = videocodec_attach(master_vfe);
		if (!zr->vfe) {
			pci_err(pdev, "%s - no VFE found\n", __func__);
			goto zr_detach_codec;
		}
		if (zr->vfe->type != zr->card.video_vfe) {
			pci_err(pdev, "%s = wrong VFE\n", __func__);
			goto zr_detach_vfe;
		}
	}

	/* take care of Natoma chipset and a revision 1 zr36057 */
	if ((pci_pci_problems & PCIPCI_NATOMA) && zr->revision <= 1)
		pci_info(zr->pci_dev, "ZR36057/Natoma bug, max. buffer size is 128K\n");

	if (zr36057_init(zr) < 0)
		goto zr_detach_vfe;

	zr->map_mode = ZORAN_MAP_MODE_RAW;

	return 0;

zr_detach_vfe:
	videocodec_detach(zr->vfe);
zr_detach_codec:
	videocodec_detach(zr->codec);
zr_unreg_i2c:
	zoran_unregister_i2c(zr);
zr_free_irq:
	btwrite(0, ZR36057_SPGPPCR);
	pci_free_irq(zr->pci_dev, 0, zr);
zr_pci_release:
	pci_release_regions(pdev);
zr_unreg:
	v4l2_ctrl_handler_free(&zr->hdl);
	v4l2_device_unregister(&zr->v4l2_dev);
zr_free_mem:

	return -ENODEV;
}

static struct pci_driver zoran_driver = {
	.name = "zr36067",
	.id_table = zr36067_pci_tbl,
	.probe = zoran_probe,
	.remove = zoran_remove,
};

static int __init zoran_init(void)
{
	int res;

	pr_info("Zoran MJPEG board driver version %s\n", ZORAN_VERSION);

	/* check the parameters we have been given, adjust if necessary */
	if (v4l_nbufs < 2)
		v4l_nbufs = 2;
	if (v4l_nbufs > VIDEO_MAX_FRAME)
		v4l_nbufs = VIDEO_MAX_FRAME;
	/* The user specifies the in KB, we want them in byte (and page aligned) */
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
	if (vidmem)
		pr_info("%s: Using supplied video memory base address @ 0x%lx\n", ZORAN_NAME, vidmem);

	/* some mainboards might not do PCI-PCI data transfer well */
	if (pci_pci_problems & (PCIPCI_FAIL | PCIAGP_FAIL | PCIPCI_ALIMAGIK))
		pr_warn("%s: chipset does not support reliable PCI-PCI DMA\n", ZORAN_NAME);

	res = pci_register_driver(&zoran_driver);
	if (res) {
		pr_err("Unable to register ZR36057 driver\n");
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

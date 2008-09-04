/*
 * Sunplus spca561 subdriver
 *
 * Copyright (C) 2004 Michel Xhaard mxhaard@magic.fr
 *
 * V4L2 by Jean-Francois Moine <http://moinejf.free.fr>
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

#define MODULE_NAME "spca561"

#include "gspca.h"

MODULE_AUTHOR("Michel Xhaard <mxhaard@users.sourceforge.net>");
MODULE_DESCRIPTION("GSPCA/SPCA561 USB Camera Driver");
MODULE_LICENSE("GPL");

/* specific webcam descriptor */
struct sd {
	struct gspca_dev gspca_dev;	/* !! must be the first item */

	__u16 contrast;			/* rev72a only */
#define CONTRAST_MIN 0x0000
#define CONTRAST_DEF 0x2000
#define CONTRAST_MAX 0x3fff

	__u16 exposure;			/* rev12a only */
#define EXPOSURE_MIN 1
#define EXPOSURE_DEF 200
#define EXPOSURE_MAX (4095 - 900) /* see set_exposure */

	__u8 brightness;		/* rev72a only */
#define BRIGHTNESS_MIN 0
#define BRIGHTNESS_DEF 32
#define BRIGHTNESS_MAX 63

	__u8 white;			/* rev12a only */
#define WHITE_MIN 1
#define WHITE_DEF 0x40
#define WHITE_MAX 0x7f

	__u8 autogain;
#define AUTOGAIN_MIN 0
#define AUTOGAIN_DEF 1
#define AUTOGAIN_MAX 1

	__u8 gain;			/* rev12a only */
#define GAIN_MIN 0x0
#define GAIN_DEF 0x24
#define GAIN_MAX 0x24

#define EXPO12A_DEF 3
	__u8 expo12a;		/* expo/gain? for rev 12a */

	__u8 chip_revision;
#define Rev012A 0
#define Rev072A 1

	signed char ag_cnt;
#define AG_CNT_START 13
};

static struct v4l2_pix_format sif_012a_mode[] = {
	{160, 120, V4L2_PIX_FMT_SGBRG8, V4L2_FIELD_NONE,
		.bytesperline = 160,
		.sizeimage = 160 * 120,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.priv = 3},
	{176, 144, V4L2_PIX_FMT_SGBRG8, V4L2_FIELD_NONE,
		.bytesperline = 176,
		.sizeimage = 176 * 144,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.priv = 2},
	{320, 240, V4L2_PIX_FMT_SPCA561, V4L2_FIELD_NONE,
		.bytesperline = 320,
		.sizeimage = 320 * 240 * 4 / 8,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.priv = 1},
	{352, 288, V4L2_PIX_FMT_SPCA561, V4L2_FIELD_NONE,
		.bytesperline = 352,
		.sizeimage = 352 * 288 * 4 / 8,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.priv = 0},
};

static struct v4l2_pix_format sif_072a_mode[] = {
	{160, 120, V4L2_PIX_FMT_SGBRG8, V4L2_FIELD_NONE,
		.bytesperline = 160,
		.sizeimage = 160 * 120,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.priv = 3},
	{176, 144, V4L2_PIX_FMT_SGBRG8, V4L2_FIELD_NONE,
		.bytesperline = 176,
		.sizeimage = 176 * 144,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.priv = 2},
	{320, 240, V4L2_PIX_FMT_SGBRG8, V4L2_FIELD_NONE,
		.bytesperline = 320,
		.sizeimage = 320 * 240,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.priv = 1},
	{352, 288, V4L2_PIX_FMT_SGBRG8, V4L2_FIELD_NONE,
		.bytesperline = 352,
		.sizeimage = 352 * 288,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.priv = 0},
};

/*
 * Initialization data
 * I'm not very sure how to split initialization from open data
 * chunks. For now, we'll consider everything as initialization
 */
/* Frame packet header offsets for the spca561 */
#define SPCA561_OFFSET_SNAP 1
#define SPCA561_OFFSET_TYPE 2
#define SPCA561_OFFSET_COMPRESS 3
#define SPCA561_OFFSET_FRAMSEQ   4
#define SPCA561_OFFSET_GPIO 5
#define SPCA561_OFFSET_USBBUFF 6
#define SPCA561_OFFSET_WIN2GRAVE 7
#define SPCA561_OFFSET_WIN2RAVE 8
#define SPCA561_OFFSET_WIN2BAVE 9
#define SPCA561_OFFSET_WIN2GBAVE 10
#define SPCA561_OFFSET_WIN1GRAVE 11
#define SPCA561_OFFSET_WIN1RAVE 12
#define SPCA561_OFFSET_WIN1BAVE 13
#define SPCA561_OFFSET_WIN1GBAVE 14
#define SPCA561_OFFSET_FREQ 15
#define SPCA561_OFFSET_VSYNC 16
#define SPCA561_OFFSET_DATA 1
#define SPCA561_INDEX_I2C_BASE 0x8800
#define SPCA561_SNAPBIT 0x20
#define SPCA561_SNAPCTRL 0x40

static void reg_w_val(struct usb_device *dev, __u16 index, __u8 value)
{
	int ret;

	ret = usb_control_msg(dev, usb_sndctrlpipe(dev, 0),
			      0,		/* request */
			      USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			      value, index, NULL, 0, 500);
	PDEBUG(D_USBO, "reg write: 0x%02x:0x%02x", index, value);
	if (ret < 0)
		PDEBUG(D_ERR, "reg write: error %d", ret);
}

static void write_vector(struct gspca_dev *gspca_dev,
			const __u16 data[][2])
{
	struct usb_device *dev = gspca_dev->dev;
	int i;

	i = 0;
	while (data[i][1] != 0) {
		reg_w_val(dev, data[i][1], data[i][0]);
		i++;
	}
}

/* read 'len' bytes to gspca_dev->usb_buf */
static void reg_r(struct gspca_dev *gspca_dev,
		  __u16 index, __u16 length)
{
	usb_control_msg(gspca_dev->dev,
			usb_rcvctrlpipe(gspca_dev->dev, 0),
			0,			/* request */
			USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			0,			/* value */
			index, gspca_dev->usb_buf, length, 500);
}

static void reg_w_buf(struct gspca_dev *gspca_dev,
		      __u16 index, const __u8 *buffer, __u16 len)
{
	memcpy(gspca_dev->usb_buf, buffer, len);
	usb_control_msg(gspca_dev->dev,
			usb_sndctrlpipe(gspca_dev->dev, 0),
			0,			/* request */
			USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			0,			/* value */
			index, gspca_dev->usb_buf, len, 500);
}

static void i2c_write(struct gspca_dev *gspca_dev, __u16 valeur, __u16 reg)
{
	int retry = 60;
	__u8 DataLow;
	__u8 DataHight;

	DataLow = valeur;
	DataHight = valeur >> 8;
	reg_w_val(gspca_dev->dev, 0x8801, reg);
	reg_w_val(gspca_dev->dev, 0x8805, DataLow);
	reg_w_val(gspca_dev->dev, 0x8800, DataHight);
	while (retry--) {
		reg_r(gspca_dev, 0x8803, 1);
		if (!gspca_dev->usb_buf[0])
			break;
	}
}

static int i2c_read(struct gspca_dev *gspca_dev, __u16 reg, __u8 mode)
{
	int retry = 60;
	__u8 value;
	__u8 vallsb;

	reg_w_val(gspca_dev->dev, 0x8804, 0x92);
	reg_w_val(gspca_dev->dev, 0x8801, reg);
	reg_w_val(gspca_dev->dev, 0x8802, (mode | 0x01));
	while (retry--) {
		reg_r(gspca_dev, 0x8803, 1);
		if (!gspca_dev->usb_buf)
			break;
	}
	if (retry == 0)
		return -1;
	reg_r(gspca_dev, 0x8800, 1);
	value = gspca_dev->usb_buf[0];
	reg_r(gspca_dev, 0x8805, 1);
	vallsb = gspca_dev->usb_buf[0];
	return ((int) value << 8) | vallsb;
}

static const __u16 spca561_init_data[][2] = {
	{0x0000, 0x8114},	/* Software GPIO output data */
	{0x0001, 0x8114},	/* Software GPIO output data */
	{0x0000, 0x8112},	/* Some kind of reset */
	{0x0003, 0x8701},	/* PCLK clock delay adjustment */
	{0x0001, 0x8703},	/* HSYNC from cmos inverted */
	{0x0011, 0x8118},	/* Enable and conf sensor */
	{0x0001, 0x8118},	/* Conf sensor */
	{0x0092, 0x8804},	/* I know nothing about these */
	{0x0010, 0x8802},	/* 0x88xx registers, so I won't */
	/***************/
	{0x000d, 0x8805},	/* sensor default setting */
	{0x0001, 0x8801},	/* 1 <- 0x0d */
	{0x0000, 0x8800},
	{0x0018, 0x8805},
	{0x0002, 0x8801},	/* 2 <- 0x18 */
	{0x0000, 0x8800},
	{0x0065, 0x8805},
	{0x0004, 0x8801},	/* 4 <- 0x01 0x65 */
	{0x0001, 0x8800},
	{0x0021, 0x8805},
	{0x0005, 0x8801},	/* 5 <- 0x21 */
	{0x0000, 0x8800},
	{0x00aa, 0x8805},
	{0x0007, 0x8801},	/* 7 <- 0xaa */
	{0x0000, 0x8800},
	{0x0004, 0x8805},
	{0x0020, 0x8801},	/* 0x20 <- 0x15 0x04 */
	{0x0015, 0x8800},
	{0x0002, 0x8805},
	{0x0039, 0x8801},	/* 0x39 <- 0x02 */
	{0x0000, 0x8800},
	{0x0010, 0x8805},
	{0x0035, 0x8801},	/* 0x35 <- 0x10 */
	{0x0000, 0x8800},
	{0x0049, 0x8805},
	{0x0009, 0x8801},	/* 0x09 <- 0x10 0x49 */
	{0x0010, 0x8800},
	{0x000b, 0x8805},
	{0x0028, 0x8801},	/* 0x28 <- 0x0b */
	{0x0000, 0x8800},
	{0x000f, 0x8805},
	{0x003b, 0x8801},	/* 0x3b <- 0x0f */
	{0x0000, 0x8800},
	{0x0000, 0x8805},
	{0x003c, 0x8801},	/* 0x3c <- 0x00 */
	{0x0000, 0x8800},
	/***************/
	{0x0018, 0x8601},	/* Pixel/line selection for color separation */
	{0x0000, 0x8602},	/* Optical black level for user setting */
	{0x0060, 0x8604},	/* Optical black horizontal offset */
	{0x0002, 0x8605},	/* Optical black vertical offset */
	{0x0000, 0x8603},	/* Non-automatic optical black level */
	{0x0002, 0x865b},	/* Horizontal offset for valid pixels */
	{0x0000, 0x865f},	/* Vertical valid pixels window (x2) */
	{0x00b0, 0x865d},	/* Horizontal valid pixels window (x2) */
	{0x0090, 0x865e},	/* Vertical valid lines window (x2) */
	{0x00e0, 0x8406},	/* Memory buffer threshold */
	{0x0000, 0x8660},	/* Compensation memory stuff */
	{0x0002, 0x8201},	/* Output address for r/w serial EEPROM */
	{0x0008, 0x8200},	/* Clear valid bit for serial EEPROM */
	{0x0001, 0x8200},	/* OprMode to be executed by hardware */
	{0x0007, 0x8201},	/* Output address for r/w serial EEPROM */
	{0x0008, 0x8200},	/* Clear valid bit for serial EEPROM */
	{0x0001, 0x8200},	/* OprMode to be executed by hardware */
	{0x0010, 0x8660},	/* Compensation memory stuff */
	{0x0018, 0x8660},	/* Compensation memory stuff */

	{0x0004, 0x8611},	/* R offset for white balance */
	{0x0004, 0x8612},	/* Gr offset for white balance */
	{0x0007, 0x8613},	/* B offset for white balance */
	{0x0000, 0x8614},	/* Gb offset for white balance */
	{0x008c, 0x8651},	/* R gain for white balance */
	{0x008c, 0x8652},	/* Gr gain for white balance */
	{0x00b5, 0x8653},	/* B gain for white balance */
	{0x008c, 0x8654},	/* Gb gain for white balance */
	{0x0002, 0x8502},	/* Maximum average bit rate stuff */

	{0x0011, 0x8802},
	{0x0087, 0x8700},	/* Set master clock (96Mhz????) */
	{0x0081, 0x8702},	/* Master clock output enable */

	{0x0000, 0x8500},	/* Set image type (352x288 no compression) */
	/* Originally was 0x0010 (352x288 compression) */

	{0x0002, 0x865b},	/* Horizontal offset for valid pixels */
	{0x0003, 0x865c},	/* Vertical offset for valid lines */
	/***************//* sensor active */
	{0x0003, 0x8801},	/* 0x03 <- 0x01 0x21 //289 */
	{0x0021, 0x8805},
	{0x0001, 0x8800},
	{0x0004, 0x8801},	/* 0x04 <- 0x01 0x65 //357 */
	{0x0065, 0x8805},
	{0x0001, 0x8800},
	{0x0005, 0x8801},	/* 0x05 <- 0x2f */
	{0x002f, 0x8805},
	{0x0000, 0x8800},
	{0x0006, 0x8801},	/* 0x06 <- 0 */
	{0x0000, 0x8805},
	{0x0000, 0x8800},
	{0x000a, 0x8801},	/* 0x0a <- 2 */
	{0x0002, 0x8805},
	{0x0000, 0x8800},
	{0x0009, 0x8801},	/* 0x09 <- 0x1061 */
	{0x0061, 0x8805},
	{0x0010, 0x8800},
	{0x0035, 0x8801},	/* 0x35 <-0x14 */
	{0x0014, 0x8805},
	{0x0000, 0x8800},
	{0x0030, 0x8112},	/* ISO and drop packet enable */
	{0x0000, 0x8112},	/* Some kind of reset ???? */
	{0x0009, 0x8118},	/* Enable sensor and set standby */
	{0x0000, 0x8114},	/* Software GPIO output data */
	{0x0000, 0x8114},	/* Software GPIO output data */
	{0x0001, 0x8114},	/* Software GPIO output data */
	{0x0000, 0x8112},	/* Some kind of reset ??? */
	{0x0003, 0x8701},
	{0x0001, 0x8703},
	{0x0011, 0x8118},
	{0x0001, 0x8118},
	/***************/
	{0x0092, 0x8804},
	{0x0010, 0x8802},
	{0x000d, 0x8805},
	{0x0001, 0x8801},
	{0x0000, 0x8800},
	{0x0018, 0x8805},
	{0x0002, 0x8801},
	{0x0000, 0x8800},
	{0x0065, 0x8805},
	{0x0004, 0x8801},
	{0x0001, 0x8800},
	{0x0021, 0x8805},
	{0x0005, 0x8801},
	{0x0000, 0x8800},
	{0x00aa, 0x8805},
	{0x0007, 0x8801},	/* mode 0xaa */
	{0x0000, 0x8800},
	{0x0004, 0x8805},
	{0x0020, 0x8801},
	{0x0015, 0x8800},	/* mode 0x0415 */
	{0x0002, 0x8805},
	{0x0039, 0x8801},
	{0x0000, 0x8800},
	{0x0010, 0x8805},
	{0x0035, 0x8801},
	{0x0000, 0x8800},
	{0x0049, 0x8805},
	{0x0009, 0x8801},
	{0x0010, 0x8800},
	{0x000b, 0x8805},
	{0x0028, 0x8801},
	{0x0000, 0x8800},
	{0x000f, 0x8805},
	{0x003b, 0x8801},
	{0x0000, 0x8800},
	{0x0000, 0x8805},
	{0x003c, 0x8801},
	{0x0000, 0x8800},
	{0x0002, 0x8502},
	{0x0039, 0x8801},
	{0x0000, 0x8805},
	{0x0000, 0x8800},

	{0x0087, 0x8700},	/* overwrite by start */
	{0x0081, 0x8702},
	{0x0000, 0x8500},
/*	{0x0010, 0x8500},  -- Previous line was this */
	{0x0002, 0x865b},
	{0x0003, 0x865c},
	/***************/
	{0x0003, 0x8801},	/* 0x121-> 289 */
	{0x0021, 0x8805},
	{0x0001, 0x8800},
	{0x0004, 0x8801},	/* 0x165 -> 357 */
	{0x0065, 0x8805},
	{0x0001, 0x8800},
	{0x0005, 0x8801},	/* 0x2f //blanking control colonne */
	{0x002f, 0x8805},
	{0x0000, 0x8800},
	{0x0006, 0x8801},	/* 0x00 //blanking mode row */
	{0x0000, 0x8805},
	{0x0000, 0x8800},
	{0x000a, 0x8801},	/* 0x01 //0x02 */
	{0x0001, 0x8805},
	{0x0000, 0x8800},
	{0x0009, 0x8801},	/* 0x1061 - setexposure times && pixel clock
				 * 0001 0 | 000 0110 0001 */
	{0x0061, 0x8805},	/* 61 31 */
	{0x0008, 0x8800},	/* 08 */
	{0x0035, 0x8801},	/* 0x14 - set gain general */
	{0x001f, 0x8805},	/* 0x14 */
	{0x0000, 0x8800},
	{0x000e, 0x8112},	/* white balance - was 30 */
	{}
};


/******************** QC Express etch2 stuff ********************/
static const __u16 Pb100_1map8300[][2] = {
	/* reg, value */
	{0x8320, 0x3304},

	{0x8303, 0x0125},	/* image area */
	{0x8304, 0x0169},
	{0x8328, 0x000b},
	{0x833c, 0x0001},		/*fixme: win:07*/

	{0x832f, 0x1904},		/*fixme: was 0419*/
	{0x8307, 0x00aa},
	{0x8301, 0x0003},
	{0x8302, 0x000e},
	{}
};
static const __u16 Pb100_2map8300[][2] = {
	/* reg, value */
	{0x8339, 0x0000},
	{0x8307, 0x00aa},
	{}
};

static const __u16 spca561_161rev12A_data1[][2] = {
	{0x29, 0x8118},		/* white balance - was 21 */
	{0x08, 0x8114},		/* white balance - was 01 */
	{0x0e, 0x8112},		/* white balance - was 00 */
	{0x00, 0x8102},		/* white balance - new */
	{0x92, 0x8804},
	{0x04, 0x8802},		/* windows uses 08 */
	{}
};
static const __u16 spca561_161rev12A_data2[][2] = {
	{0x21, 0x8118},
	{0x10, 0x8500},
	{0x07, 0x8601},
	{0x07, 0x8602},
	{0x04, 0x8501},
	{0x21, 0x8118},

	{0x07, 0x8201},		/* windows uses 02 */
	{0x08, 0x8200},
	{0x01, 0x8200},

	{0x00, 0x8114},
	{0x01, 0x8114},		/* windows uses 00 */

	{0x90, 0x8604},
	{0x00, 0x8605},
	{0xb0, 0x8603},

	/* sensor gains */
	{0x07, 0x8601},		/* white balance - new */
	{0x07, 0x8602},		/* white balance - new */
	{0x00, 0x8610},		/* *red */
	{0x00, 0x8611},		/* 3f   *green */
	{0x00, 0x8612},		/* green *blue */
	{0x00, 0x8613},		/* blue *green */
	{0x43, 0x8614},		/* green *red - white balance - was 0x35 */
	{0x40, 0x8615},		/* 40   *green - white balance - was 0x35 */
	{0x71, 0x8616},		/* 7a   *blue - white balance - was 0x35 */
	{0x40, 0x8617},		/* 40   *green - white balance - was 0x35 */

	{0x0c, 0x8620},		/* 0c */
	{0xc8, 0x8631},		/* c8 */
	{0xc8, 0x8634},		/* c8 */
	{0x23, 0x8635},		/* 23 */
	{0x1f, 0x8636},		/* 1f */
	{0xdd, 0x8637},		/* dd */
	{0xe1, 0x8638},		/* e1 */
	{0x1d, 0x8639},		/* 1d */
	{0x21, 0x863a},		/* 21 */
	{0xe3, 0x863b},		/* e3 */
	{0xdf, 0x863c},		/* df */
	{0xf0, 0x8505},
	{0x32, 0x850a},
/*	{0x99, 0x8700},		 * - white balance - new (removed) */
	{}
};

static void sensor_mapwrite(struct gspca_dev *gspca_dev,
			    const __u16 sensormap[][2])
{
	int i = 0;
	__u8 usbval[2];

	while (sensormap[i][0]) {
		usbval[0] = sensormap[i][1];
		usbval[1] = sensormap[i][1] >> 8;
		reg_w_buf(gspca_dev, sensormap[i][0], usbval, 2);
		i++;
	}
}
static void init_161rev12A(struct gspca_dev *gspca_dev)
{
/*	sensor_reset(gspca_dev);	(not in win) */
	write_vector(gspca_dev, spca561_161rev12A_data1);
	sensor_mapwrite(gspca_dev, Pb100_1map8300);
/*fixme: should be in sd_start*/
	write_vector(gspca_dev, spca561_161rev12A_data2);
	sensor_mapwrite(gspca_dev, Pb100_2map8300);
}

/* this function is called at probe time */
static int sd_config(struct gspca_dev *gspca_dev,
		     const struct usb_device_id *id)
{
	struct sd *sd = (struct sd *) gspca_dev;
	struct cam *cam;
	__u16 vendor, product;
	__u8 data1, data2;

	/* Read frm global register the USB product and vendor IDs, just to
	 * prove that we can communicate with the device.  This works, which
	 * confirms at we are communicating properly and that the device
	 * is a 561. */
	reg_r(gspca_dev, 0x8104, 1);
	data1 = gspca_dev->usb_buf[0];
	reg_r(gspca_dev, 0x8105, 1);
	data2 = gspca_dev->usb_buf[0];
	vendor = (data2 << 8) | data1;
	reg_r(gspca_dev, 0x8106, 1);
	data1 = gspca_dev->usb_buf[0];
	reg_r(gspca_dev, 0x8107, 1);
	data2 = gspca_dev->usb_buf[0];
	product = (data2 << 8) | data1;
	if (vendor != id->idVendor || product != id->idProduct) {
		PDEBUG(D_PROBE, "Bad vendor / product from device");
		return -EINVAL;
	}

	cam = &gspca_dev->cam;
	cam->epaddr = 0x01;
	gspca_dev->nbalt = 7 + 1;	/* choose alternate 7 first */

	sd->chip_revision = id->driver_info;
	if (sd->chip_revision == Rev012A) {
		cam->cam_mode = sif_012a_mode;
		cam->nmodes = ARRAY_SIZE(sif_012a_mode);
	} else {
		cam->cam_mode = sif_072a_mode;
		cam->nmodes = ARRAY_SIZE(sif_072a_mode);
	}
	sd->brightness = BRIGHTNESS_DEF;
	sd->contrast = CONTRAST_DEF;
	sd->white = WHITE_DEF;
	sd->exposure = EXPOSURE_DEF;
	sd->autogain = AUTOGAIN_DEF;
	sd->gain = GAIN_DEF;
	sd->expo12a = EXPO12A_DEF;
	return 0;
}

/* this function is called at probe and resume time */
static int sd_init_12a(struct gspca_dev *gspca_dev)
{
	PDEBUG(D_STREAM, "Chip revision: 012a");
	init_161rev12A(gspca_dev);
	return 0;
}
static int sd_init_72a(struct gspca_dev *gspca_dev)
{
	PDEBUG(D_STREAM, "Chip revision: 072a");
	write_vector(gspca_dev, spca561_init_data);
	return 0;
}

static void setcontrast(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	struct usb_device *dev = gspca_dev->dev;
	__u8 lowb;

	switch (sd->chip_revision) {
	case Rev072A:
		lowb = sd->contrast >> 8;
		reg_w_val(dev, 0x8651, lowb);
		reg_w_val(dev, 0x8652, lowb);
		reg_w_val(dev, 0x8653, lowb);
		reg_w_val(dev, 0x8654, lowb);
		break;
	default: {
/*	case Rev012A: { */
		static const __u8 Reg8391[] =
			{ 0x92, 0x30, 0x20, 0x00, 0x0c, 0x00, 0x00, 0x00 };

		reg_w_buf(gspca_dev, 0x8391, Reg8391, 8);
		reg_w_buf(gspca_dev, 0x8390, Reg8391, 8);
		break;
	    }
	}
}

/* rev12a only */
static void setwhite(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	__u16 white;
	__u8 reg8614, reg8616;

	white = sd->white;
	/* try to emulate MS-win as possible */
	reg8616 = 0x90 - white * 5 / 8;
	reg_w_val(gspca_dev->dev, 0x8616, reg8616);
	reg8614 = 0x20 + white * 3 / 8;
	reg_w_val(gspca_dev->dev, 0x8614, reg8614);
}

/* rev 12a only */
static void setexposure(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	int expo;
	int clock_divider;
	__u8 data[2];

	/* Register 0x8309 controls exposure for the spca561,
	   the basic exposure setting goes from 1-2047, where 1 is completely
	   dark and 2047 is very bright. It not only influences exposure but
	   also the framerate (to allow for longer exposure) from 1 - 300 it
	   only raises the exposure time then from 300 - 600 it halves the
	   framerate to be able to further raise the exposure time and for every
	   300 more it halves the framerate again. This allows for a maximum
	   exposure time of circa 0.2 - 0.25 seconds (30 / (2000/3000) fps).
	   Sometimes this is not enough, the 1-2047 uses bits 0-10, bits 11-12
	   configure a divider for the base framerate which us used at the
	   exposure setting of 1-300. These bits configure the base framerate
	   according to the following formula: fps = 60 / (value + 2) */
	if (sd->exposure < 2048) {
		expo = sd->exposure;
		clock_divider = 0;
	} else {
		/* Add 900 to make the 0 setting of the second part of the
		   exposure equal to the 2047 setting of the first part. */
		expo = (sd->exposure - 2048) + 900;
		clock_divider = 3;
	}
	expo |= clock_divider << 11;
	data[0] = expo;
	data[1] = expo >> 8;
	reg_w_buf(gspca_dev, 0x8309, data, 2);
}

/* rev 12a only */
static void setgain(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	__u8 data[2];

	data[0] = sd->gain;
	data[1] = 0;
	reg_w_buf(gspca_dev, 0x8335, data, 2);
}

static void setautogain(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;

	if (sd->autogain)
		sd->ag_cnt = AG_CNT_START;
	else
		sd->ag_cnt = -1;
}

static void sd_start_12a(struct gspca_dev *gspca_dev)
{
	struct usb_device *dev = gspca_dev->dev;
	int Clck = 0x8a; /* lower 0x8X values lead to fps > 30 */
	__u8 Reg8307[] = { 0xaa, 0x00 };
	int mode;

	mode = gspca_dev->cam.cam_mode[(int) gspca_dev->curr_mode].priv;
	if (mode <= 1) {
		/* Use compression on 320x240 and above */
		reg_w_val(dev, 0x8500, 0x10 | mode);
	} else {
		/* I couldn't get the compression to work below 320x240
		 * Fortunately at these resolutions the bandwidth
		 * is sufficient to push raw frames at ~20fps */
		reg_w_val(dev, 0x8500, mode);
	}		/* -- qq@kuku.eu.org */
	reg_w_buf(gspca_dev, 0x8307, Reg8307, 2);
	reg_w_val(gspca_dev->dev, 0x8700, Clck);
					/* 0x8f 0x85 0x27 clock */
	reg_w_val(gspca_dev->dev, 0x8112, 0x1e | 0x20);
	reg_w_val(gspca_dev->dev, 0x850b, 0x03);
	setcontrast(gspca_dev);
	setwhite(gspca_dev);
	setautogain(gspca_dev);
	setexposure(gspca_dev);
}
static void sd_start_72a(struct gspca_dev *gspca_dev)
{
	struct usb_device *dev = gspca_dev->dev;
	int Clck;
	int mode;

	mode = gspca_dev->cam.cam_mode[(int) gspca_dev->curr_mode].priv;
	switch (mode) {
	default:
/*	case 0:
	case 1: */
		Clck = 0x25;
		break;
	case 2:
		Clck = 0x22;
		break;
	case 3:
		Clck = 0x21;
		break;
	}
	reg_w_val(dev, 0x8500, mode);	/* mode */
	reg_w_val(dev, 0x8700, Clck);	/* 0x27 clock */
	reg_w_val(dev, 0x8112, 0x10 | 0x20);
	setautogain(gspca_dev);
}

static void sd_stopN(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;

	if (sd->chip_revision == Rev012A) {
		reg_w_val(gspca_dev->dev, 0x8112, 0x0e);
	} else {
		reg_w_val(gspca_dev->dev, 0x8112, 0x20);
/*		reg_w_val(gspca_dev->dev, 0x8102, 0x00); ?? */
	}
}

static void sd_stop0(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;

	if (sd->chip_revision == Rev012A) {
		reg_w_val(gspca_dev->dev, 0x8118, 0x29);
		reg_w_val(gspca_dev->dev, 0x8114, 0x08);
	}
/*	reg_w_val(gspca_dev->dev, 0x8114, 0); */
}

static void do_autogain(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	int expotimes;
	int pixelclk;
	int gainG;
	__u8 R, Gr, Gb, B;
	int y;
	__u8 luma_mean = 110;
	__u8 luma_delta = 20;
	__u8 spring = 4;
	__u8 reg8339[2];

	if (sd->ag_cnt < 0)
		return;
	if (--sd->ag_cnt >= 0)
		return;
	sd->ag_cnt = AG_CNT_START;

	switch (sd->chip_revision) {
	case Rev072A:
		reg_r(gspca_dev, 0x8621, 1);
		Gr = gspca_dev->usb_buf[0];
		reg_r(gspca_dev, 0x8622, 1);
		R = gspca_dev->usb_buf[0];
		reg_r(gspca_dev, 0x8623, 1);
		B = gspca_dev->usb_buf[0];
		reg_r(gspca_dev, 0x8624, 1);
		Gb = gspca_dev->usb_buf[0];
		y = (77 * R + 75 * (Gr + Gb) + 29 * B) >> 8;
		/* u= (128*B-(43*(Gr+Gb+R))) >> 8; */
		/* v= (128*R-(53*(Gr+Gb))-21*B) >> 8; */
		/* PDEBUG(D_CONF,"reading Y %d U %d V %d ",y,u,v); */

		if (y < luma_mean - luma_delta ||
		    y > luma_mean + luma_delta) {
			expotimes = i2c_read(gspca_dev, 0x09, 0x10);
			pixelclk = 0x0800;
			expotimes = expotimes & 0x07ff;
			/* PDEBUG(D_PACK,
				"Exposition Times 0x%03X Clock 0x%04X ",
				expotimes,pixelclk); */
			gainG = i2c_read(gspca_dev, 0x35, 0x10);
			/* PDEBUG(D_PACK,
				"reading Gain register %d", gainG); */

			expotimes += (luma_mean - y) >> spring;
			gainG += (luma_mean - y) / 50;
			/* PDEBUG(D_PACK,
				"compute expotimes %d gain %d",
				expotimes,gainG); */

			if (gainG > 0x3f)
				gainG = 0x3f;
			else if (gainG < 4)
				gainG = 3;
			i2c_write(gspca_dev, gainG, 0x35);

			if (expotimes >= 0x0256)
				expotimes = 0x0256;
			else if (expotimes < 4)
				expotimes = 3;
			i2c_write(gspca_dev, expotimes | pixelclk, 0x09);
		}
		break;
	case Rev012A:
		reg_r(gspca_dev, 0x8330, 2);
		if (gspca_dev->usb_buf[1] > 0x08) {
			reg8339[0] = ++sd->expo12a;
			reg8339[1] = 0;
			reg_w_buf(gspca_dev, 0x8339, reg8339, 2);
		} else if (gspca_dev->usb_buf[1] < 0x02) {
			reg8339[0] = --sd->expo12a;
			reg8339[1] = 0;
			reg_w_buf(gspca_dev, 0x8339, reg8339, 2);
		}
		break;
	}
}

static void sd_pkt_scan(struct gspca_dev *gspca_dev,
			struct gspca_frame *frame, /* target */
			__u8 *data,		/* isoc packet */
			int len)		/* iso packet length */
{
	struct sd *sd = (struct sd *) gspca_dev;

	switch (data[0]) {
	case 0:		/* start of frame */
		frame = gspca_frame_add(gspca_dev, LAST_PACKET, frame,
					data, 0);
		data += SPCA561_OFFSET_DATA;
		len -= SPCA561_OFFSET_DATA;
		if (data[1] & 0x10) {
			/* compressed bayer */
			gspca_frame_add(gspca_dev, FIRST_PACKET,
					frame, data, len);
		} else {
			/* raw bayer (with a header, which we skip) */
			if (sd->chip_revision == Rev012A) {
				data += 20;
				len -= 20;
			} else {
				data += 16;
				len -= 16;
			}
			gspca_frame_add(gspca_dev, FIRST_PACKET,
						frame, data, len);
		}
		return;
	case 0xff:		/* drop */
/*		gspca_dev->last_packet_type = DISCARD_PACKET; */
		return;
	}
	data++;
	len--;
	gspca_frame_add(gspca_dev, INTER_PACKET, frame, data, len);
}

/* rev 72a only */
static void setbrightness(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	__u8 value;

	value = sd->brightness;
	reg_w_val(gspca_dev->dev, 0x8611, value);
	reg_w_val(gspca_dev->dev, 0x8612, value);
	reg_w_val(gspca_dev->dev, 0x8613, value);
	reg_w_val(gspca_dev->dev, 0x8614, value);
}

static void getbrightness(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	__u16 tot;

	tot = 0;
	reg_r(gspca_dev, 0x8611, 1);
	tot += gspca_dev->usb_buf[0];
	reg_r(gspca_dev, 0x8612, 1);
	tot += gspca_dev->usb_buf[0];
	reg_r(gspca_dev, 0x8613, 1);
	tot += gspca_dev->usb_buf[0];
	reg_r(gspca_dev, 0x8614, 1);
	tot += gspca_dev->usb_buf[0];
	sd->brightness = tot >> 2;
}

/* rev72a only */
static void getcontrast(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	__u16 tot;

	tot = 0;
	reg_r(gspca_dev, 0x8651, 1);
	tot += gspca_dev->usb_buf[0];
	reg_r(gspca_dev, 0x8652, 1);
	tot += gspca_dev->usb_buf[0];
	reg_r(gspca_dev, 0x8653, 1);
	tot += gspca_dev->usb_buf[0];
	reg_r(gspca_dev, 0x8654, 1);
	tot += gspca_dev->usb_buf[0];
	sd->contrast = tot << 6;
	PDEBUG(D_CONF, "get contrast %d", sd->contrast);
}

/* rev 72a only */
static int sd_setbrightness(struct gspca_dev *gspca_dev, __s32 val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	sd->brightness = val;
	if (gspca_dev->streaming)
		setbrightness(gspca_dev);
	return 0;
}

static int sd_getbrightness(struct gspca_dev *gspca_dev, __s32 *val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	getbrightness(gspca_dev);
	*val = sd->brightness;
	return 0;
}

/* rev 72a only */
static int sd_setcontrast(struct gspca_dev *gspca_dev, __s32 val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	sd->contrast = val;
	if (gspca_dev->streaming)
		setcontrast(gspca_dev);
	return 0;
}

static int sd_getcontrast(struct gspca_dev *gspca_dev, __s32 *val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	getcontrast(gspca_dev);
	*val = sd->contrast;
	return 0;
}

static int sd_setautogain(struct gspca_dev *gspca_dev, __s32 val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	sd->autogain = val;
	if (gspca_dev->streaming)
		setautogain(gspca_dev);
	return 0;
}

static int sd_getautogain(struct gspca_dev *gspca_dev, __s32 *val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	*val = sd->autogain;
	return 0;
}

/* rev12a only */
static int sd_setwhite(struct gspca_dev *gspca_dev, __s32 val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	sd->white = val;
	if (gspca_dev->streaming)
		setwhite(gspca_dev);
	return 0;
}

static int sd_getwhite(struct gspca_dev *gspca_dev, __s32 *val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	*val = sd->white;
	return 0;
}

/* rev12a only */
static int sd_setexposure(struct gspca_dev *gspca_dev, __s32 val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	sd->exposure = val;
	if (gspca_dev->streaming)
		setexposure(gspca_dev);
	return 0;
}

static int sd_getexposure(struct gspca_dev *gspca_dev, __s32 *val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	*val = sd->exposure;
	return 0;
}

/* rev12a only */
static int sd_setgain(struct gspca_dev *gspca_dev, __s32 val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	sd->gain = val;
	if (gspca_dev->streaming)
		setgain(gspca_dev);
	return 0;
}

static int sd_getgain(struct gspca_dev *gspca_dev, __s32 *val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	*val = sd->gain;
	return 0;
}

/* control tables */
static struct ctrl sd_ctrls_12a[] = {
	{
	    {
		.id = V4L2_CID_DO_WHITE_BALANCE,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "While Balance",
		.minimum = WHITE_MIN,
		.maximum = WHITE_MAX,
		.step = 1,
		.default_value = WHITE_DEF,
	    },
	    .set = sd_setwhite,
	    .get = sd_getwhite,
	},
	{
	    {
		.id = V4L2_CID_EXPOSURE,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "Exposure",
		.minimum = EXPOSURE_MIN,
		.maximum = EXPOSURE_MAX,
		.step = 1,
		.default_value = EXPOSURE_DEF,
	    },
	    .set = sd_setexposure,
	    .get = sd_getexposure,
	},
	{
	    {
		.id = V4L2_CID_AUTOGAIN,
		.type = V4L2_CTRL_TYPE_BOOLEAN,
		.name = "Auto Gain",
		.minimum = AUTOGAIN_MIN,
		.maximum = AUTOGAIN_MAX,
		.step = 1,
		.default_value = AUTOGAIN_DEF,
	    },
	    .set = sd_setautogain,
	    .get = sd_getautogain,
	},
	{
	    {
		.id = V4L2_CID_GAIN,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "Gain",
		.minimum = GAIN_MIN,
		.maximum = GAIN_MAX,
		.step = 1,
		.default_value = GAIN_DEF,
	    },
	    .set = sd_setgain,
	    .get = sd_getgain,
	},
};

static struct ctrl sd_ctrls_72a[] = {
	{
	   {
		.id = V4L2_CID_BRIGHTNESS,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "Brightness",
		.minimum = BRIGHTNESS_MIN,
		.maximum = BRIGHTNESS_MAX,
		.step = 1,
		.default_value = BRIGHTNESS_DEF,
	    },
	    .set = sd_setbrightness,
	    .get = sd_getbrightness,
	},
	{
	    {
		.id = V4L2_CID_CONTRAST,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "Contrast",
		.minimum = CONTRAST_MIN,
		.maximum = CONTRAST_MAX,
		.step = 1,
		.default_value = CONTRAST_DEF,
	    },
	    .set = sd_setcontrast,
	    .get = sd_getcontrast,
	},
	{
	    {
		.id = V4L2_CID_AUTOGAIN,
		.type = V4L2_CTRL_TYPE_BOOLEAN,
		.name = "Auto Gain",
		.minimum = AUTOGAIN_MIN,
		.maximum = AUTOGAIN_MAX,
		.step = 1,
		.default_value = AUTOGAIN_DEF,
	    },
	    .set = sd_setautogain,
	    .get = sd_getautogain,
	},
};

/* sub-driver description */
static const struct sd_desc sd_desc_12a = {
	.name = MODULE_NAME,
	.ctrls = sd_ctrls_12a,
	.nctrls = ARRAY_SIZE(sd_ctrls_12a),
	.config = sd_config,
	.init = sd_init_12a,
	.start = sd_start_12a,
	.stopN = sd_stopN,
	.stop0 = sd_stop0,
	.pkt_scan = sd_pkt_scan,
/*	.dq_callback = do_autogain,	 * fixme */
};
static const struct sd_desc sd_desc_72a = {
	.name = MODULE_NAME,
	.ctrls = sd_ctrls_72a,
	.nctrls = ARRAY_SIZE(sd_ctrls_72a),
	.config = sd_config,
	.init = sd_init_72a,
	.start = sd_start_72a,
	.stopN = sd_stopN,
	.stop0 = sd_stop0,
	.pkt_scan = sd_pkt_scan,
	.dq_callback = do_autogain,
};
static const struct sd_desc *sd_desc[2] = {
	&sd_desc_12a,
	&sd_desc_72a
};

/* -- module initialisation -- */
static const __devinitdata struct usb_device_id device_table[] = {
	{USB_DEVICE(0x041e, 0x401a), .driver_info = Rev072A},
	{USB_DEVICE(0x041e, 0x403b), .driver_info = Rev012A},
	{USB_DEVICE(0x0458, 0x7004), .driver_info = Rev072A},
	{USB_DEVICE(0x046d, 0x0928), .driver_info = Rev012A},
	{USB_DEVICE(0x046d, 0x0929), .driver_info = Rev012A},
	{USB_DEVICE(0x046d, 0x092a), .driver_info = Rev012A},
	{USB_DEVICE(0x046d, 0x092b), .driver_info = Rev012A},
	{USB_DEVICE(0x046d, 0x092c), .driver_info = Rev012A},
	{USB_DEVICE(0x046d, 0x092d), .driver_info = Rev012A},
	{USB_DEVICE(0x046d, 0x092e), .driver_info = Rev012A},
	{USB_DEVICE(0x046d, 0x092f), .driver_info = Rev012A},
	{USB_DEVICE(0x04fc, 0x0561), .driver_info = Rev072A},
	{USB_DEVICE(0x060b, 0xa001), .driver_info = Rev072A},
	{USB_DEVICE(0x10fd, 0x7e50), .driver_info = Rev072A},
	{USB_DEVICE(0xabcd, 0xcdee), .driver_info = Rev072A},
	{}
};

MODULE_DEVICE_TABLE(usb, device_table);

/* -- device connect -- */
static int sd_probe(struct usb_interface *intf,
		    const struct usb_device_id *id)
{
	return gspca_dev_probe(intf, id,
				sd_desc[id->driver_info],
				sizeof(struct sd),
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
	if (usb_register(&sd_driver) < 0)
		return -1;
	PDEBUG(D_PROBE, "registered");
	return 0;
}
static void __exit sd_mod_exit(void)
{
	usb_deregister(&sd_driver);
	PDEBUG(D_PROBE, "deregistered");
}

module_init(sd_mod_init);
module_exit(sd_mod_exit);

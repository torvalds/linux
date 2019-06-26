/**
 *
 * GSPCA sub driver for W996[78]CF JPEG USB Dual Mode Camera Chip.
 *
 * Copyright (C) 2009 Hans de Goede <hdegoede@redhat.com>
 *
 * This module is adapted from the in kernel v4l1 w9968cf driver:
 *
 * Copyright (C) 2002-2004 by Luca Risolia <luca.risolia@studio.unibo.it>
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
 */

/* Note this is not a stand alone driver, it gets included in ov519.c, this
   is a bit of a hack, but it needs the driver code for a lot of different
   ov sensors which is already present in ov519.c (the old v4l1 driver used
   the ovchipcam framework). When we have the time we really should move
   the sensor drivers to v4l2 sub drivers, and properly split of this
   driver from ov519.c */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#define W9968CF_I2C_BUS_DELAY    4 /* delay in us for I2C bit r/w operations */

#define Y_QUANTABLE (&sd->jpeg_hdr[JPEG_QT0_OFFSET])
#define UV_QUANTABLE (&sd->jpeg_hdr[JPEG_QT1_OFFSET])

static const struct v4l2_pix_format w9968cf_vga_mode[] = {
	{160, 120, V4L2_PIX_FMT_UYVY, V4L2_FIELD_NONE,
		.bytesperline = 160 * 2,
		.sizeimage = 160 * 120 * 2,
		.colorspace = V4L2_COLORSPACE_JPEG},
	{176, 144, V4L2_PIX_FMT_UYVY, V4L2_FIELD_NONE,
		.bytesperline = 176 * 2,
		.sizeimage = 176 * 144 * 2,
		.colorspace = V4L2_COLORSPACE_JPEG},
	{320, 240, V4L2_PIX_FMT_JPEG, V4L2_FIELD_NONE,
		.bytesperline = 320 * 2,
		.sizeimage = 320 * 240 * 2,
		.colorspace = V4L2_COLORSPACE_JPEG},
	{352, 288, V4L2_PIX_FMT_JPEG, V4L2_FIELD_NONE,
		.bytesperline = 352 * 2,
		.sizeimage = 352 * 288 * 2,
		.colorspace = V4L2_COLORSPACE_JPEG},
	{640, 480, V4L2_PIX_FMT_JPEG, V4L2_FIELD_NONE,
		.bytesperline = 640 * 2,
		.sizeimage = 640 * 480 * 2,
		.colorspace = V4L2_COLORSPACE_JPEG},
};

static void reg_w(struct sd *sd, u16 index, u16 value);

/*--------------------------------------------------------------------------
  Write 64-bit data to the fast serial bus registers.
  Return 0 on success, -1 otherwise.
  --------------------------------------------------------------------------*/
static void w9968cf_write_fsb(struct sd *sd, u16* data)
{
	struct usb_device *udev = sd->gspca_dev.dev;
	u16 value;
	int ret;

	if (sd->gspca_dev.usb_err < 0)
		return;

	value = *data++;
	memcpy(sd->gspca_dev.usb_buf, data, 6);

	/* Avoid things going to fast for the bridge with a xhci host */
	udelay(150);
	ret = usb_control_msg(udev, usb_sndctrlpipe(udev, 0), 0,
			      USB_TYPE_VENDOR | USB_DIR_OUT | USB_RECIP_DEVICE,
			      value, 0x06, sd->gspca_dev.usb_buf, 6, 500);
	if (ret < 0) {
		pr_err("Write FSB registers failed (%d)\n", ret);
		sd->gspca_dev.usb_err = ret;
	}
}

/*--------------------------------------------------------------------------
  Write data to the serial bus control register.
  Return 0 on success, a negative number otherwise.
  --------------------------------------------------------------------------*/
static void w9968cf_write_sb(struct sd *sd, u16 value)
{
	int ret;

	if (sd->gspca_dev.usb_err < 0)
		return;

	/* Avoid things going to fast for the bridge with a xhci host */
	udelay(150);

	/* We don't use reg_w here, as that would cause all writes when
	   bitbanging i2c to be logged, making the logs impossible to read */
	ret = usb_control_msg(sd->gspca_dev.dev,
		usb_sndctrlpipe(sd->gspca_dev.dev, 0),
		0,
		USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
		value, 0x01, NULL, 0, 500);

	udelay(W9968CF_I2C_BUS_DELAY);

	if (ret < 0) {
		pr_err("Write SB reg [01] %04x failed\n", value);
		sd->gspca_dev.usb_err = ret;
	}
}

/*--------------------------------------------------------------------------
  Read data from the serial bus control register.
  Return 0 on success, a negative number otherwise.
  --------------------------------------------------------------------------*/
static int w9968cf_read_sb(struct sd *sd)
{
	int ret;

	if (sd->gspca_dev.usb_err < 0)
		return -1;

	/* Avoid things going to fast for the bridge with a xhci host */
	udelay(150);

	/* We don't use reg_r here, as the w9968cf is special and has 16
	   bit registers instead of 8 bit */
	ret = usb_control_msg(sd->gspca_dev.dev,
			usb_rcvctrlpipe(sd->gspca_dev.dev, 0),
			1,
			USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			0, 0x01, sd->gspca_dev.usb_buf, 2, 500);
	if (ret >= 0) {
		ret = sd->gspca_dev.usb_buf[0] |
		      (sd->gspca_dev.usb_buf[1] << 8);
	} else {
		pr_err("Read SB reg [01] failed\n");
		sd->gspca_dev.usb_err = ret;
	}

	udelay(W9968CF_I2C_BUS_DELAY);

	return ret;
}

/*--------------------------------------------------------------------------
  Upload quantization tables for the JPEG compression.
  This function is called by w9968cf_start_transfer().
  Return 0 on success, a negative number otherwise.
  --------------------------------------------------------------------------*/
static void w9968cf_upload_quantizationtables(struct sd *sd)
{
	u16 a, b;
	int i, j;

	reg_w(sd, 0x39, 0x0010); /* JPEG clock enable */

	for (i = 0, j = 0; i < 32; i++, j += 2) {
		a = Y_QUANTABLE[j] | ((unsigned)(Y_QUANTABLE[j + 1]) << 8);
		b = UV_QUANTABLE[j] | ((unsigned)(UV_QUANTABLE[j + 1]) << 8);
		reg_w(sd, 0x40 + i, a);
		reg_w(sd, 0x60 + i, b);
	}
	reg_w(sd, 0x39, 0x0012); /* JPEG encoder enable */
}

/****************************************************************************
 * Low-level I2C I/O functions.                                             *
 * The adapter supports the following I2C transfer functions:               *
 * i2c_adap_fastwrite_byte_data() (at 400 kHz bit frequency only)           *
 * i2c_adap_read_byte_data()                                                *
 * i2c_adap_read_byte()                                                     *
 ****************************************************************************/

static void w9968cf_smbus_start(struct sd *sd)
{
	w9968cf_write_sb(sd, 0x0011); /* SDE=1, SDA=0, SCL=1 */
	w9968cf_write_sb(sd, 0x0010); /* SDE=1, SDA=0, SCL=0 */
}

static void w9968cf_smbus_stop(struct sd *sd)
{
	w9968cf_write_sb(sd, 0x0010); /* SDE=1, SDA=0, SCL=0 */
	w9968cf_write_sb(sd, 0x0011); /* SDE=1, SDA=0, SCL=1 */
	w9968cf_write_sb(sd, 0x0013); /* SDE=1, SDA=1, SCL=1 */
}

static void w9968cf_smbus_write_byte(struct sd *sd, u8 v)
{
	u8 bit;
	int sda;

	for (bit = 0 ; bit < 8 ; bit++) {
		sda = (v & 0x80) ? 2 : 0;
		v <<= 1;
		/* SDE=1, SDA=sda, SCL=0 */
		w9968cf_write_sb(sd, 0x10 | sda);
		/* SDE=1, SDA=sda, SCL=1 */
		w9968cf_write_sb(sd, 0x11 | sda);
		/* SDE=1, SDA=sda, SCL=0 */
		w9968cf_write_sb(sd, 0x10 | sda);
	}
}

static void w9968cf_smbus_read_byte(struct sd *sd, u8 *v)
{
	u8 bit;

	/* No need to ensure SDA is high as we are always called after
	   read_ack which ends with SDA high */
	*v = 0;
	for (bit = 0 ; bit < 8 ; bit++) {
		*v <<= 1;
		/* SDE=1, SDA=1, SCL=1 */
		w9968cf_write_sb(sd, 0x0013);
		*v |= (w9968cf_read_sb(sd) & 0x0008) ? 1 : 0;
		/* SDE=1, SDA=1, SCL=0 */
		w9968cf_write_sb(sd, 0x0012);
	}
}

static void w9968cf_smbus_write_nack(struct sd *sd)
{
	/* No need to ensure SDA is high as we are always called after
	   read_byte which ends with SDA high */
	w9968cf_write_sb(sd, 0x0013); /* SDE=1, SDA=1, SCL=1 */
	w9968cf_write_sb(sd, 0x0012); /* SDE=1, SDA=1, SCL=0 */
}

static void w9968cf_smbus_read_ack(struct sd *sd)
{
	struct gspca_dev *gspca_dev = (struct gspca_dev *)sd;
	int sda;

	/* Ensure SDA is high before raising clock to avoid a spurious stop */
	w9968cf_write_sb(sd, 0x0012); /* SDE=1, SDA=1, SCL=0 */
	w9968cf_write_sb(sd, 0x0013); /* SDE=1, SDA=1, SCL=1 */
	sda = w9968cf_read_sb(sd);
	w9968cf_write_sb(sd, 0x0012); /* SDE=1, SDA=1, SCL=0 */
	if (sda >= 0 && (sda & 0x08)) {
		gspca_dbg(gspca_dev, D_USBI, "Did not receive i2c ACK\n");
		sd->gspca_dev.usb_err = -EIO;
	}
}

/* SMBus protocol: S Addr Wr [A] Subaddr [A] Value [A] P */
static void w9968cf_i2c_w(struct sd *sd, u8 reg, u8 value)
{
	struct gspca_dev *gspca_dev = (struct gspca_dev *)sd;
	u16* data = (u16 *)sd->gspca_dev.usb_buf;

	data[0] = 0x082f | ((sd->sensor_addr & 0x80) ? 0x1500 : 0x0);
	data[0] |= (sd->sensor_addr & 0x40) ? 0x4000 : 0x0;
	data[1] = 0x2082 | ((sd->sensor_addr & 0x40) ? 0x0005 : 0x0);
	data[1] |= (sd->sensor_addr & 0x20) ? 0x0150 : 0x0;
	data[1] |= (sd->sensor_addr & 0x10) ? 0x5400 : 0x0;
	data[2] = 0x8208 | ((sd->sensor_addr & 0x08) ? 0x0015 : 0x0);
	data[2] |= (sd->sensor_addr & 0x04) ? 0x0540 : 0x0;
	data[2] |= (sd->sensor_addr & 0x02) ? 0x5000 : 0x0;
	data[3] = 0x1d20 | ((sd->sensor_addr & 0x02) ? 0x0001 : 0x0);
	data[3] |= (sd->sensor_addr & 0x01) ? 0x0054 : 0x0;

	w9968cf_write_fsb(sd, data);

	data[0] = 0x8208 | ((reg & 0x80) ? 0x0015 : 0x0);
	data[0] |= (reg & 0x40) ? 0x0540 : 0x0;
	data[0] |= (reg & 0x20) ? 0x5000 : 0x0;
	data[1] = 0x0820 | ((reg & 0x20) ? 0x0001 : 0x0);
	data[1] |= (reg & 0x10) ? 0x0054 : 0x0;
	data[1] |= (reg & 0x08) ? 0x1500 : 0x0;
	data[1] |= (reg & 0x04) ? 0x4000 : 0x0;
	data[2] = 0x2082 | ((reg & 0x04) ? 0x0005 : 0x0);
	data[2] |= (reg & 0x02) ? 0x0150 : 0x0;
	data[2] |= (reg & 0x01) ? 0x5400 : 0x0;
	data[3] = 0x001d;

	w9968cf_write_fsb(sd, data);

	data[0] = 0x8208 | ((value & 0x80) ? 0x0015 : 0x0);
	data[0] |= (value & 0x40) ? 0x0540 : 0x0;
	data[0] |= (value & 0x20) ? 0x5000 : 0x0;
	data[1] = 0x0820 | ((value & 0x20) ? 0x0001 : 0x0);
	data[1] |= (value & 0x10) ? 0x0054 : 0x0;
	data[1] |= (value & 0x08) ? 0x1500 : 0x0;
	data[1] |= (value & 0x04) ? 0x4000 : 0x0;
	data[2] = 0x2082 | ((value & 0x04) ? 0x0005 : 0x0);
	data[2] |= (value & 0x02) ? 0x0150 : 0x0;
	data[2] |= (value & 0x01) ? 0x5400 : 0x0;
	data[3] = 0xfe1d;

	w9968cf_write_fsb(sd, data);

	gspca_dbg(gspca_dev, D_USBO, "i2c 0x%02x -> [0x%02x]\n", value, reg);
}

/* SMBus protocol: S Addr Wr [A] Subaddr [A] P S Addr+1 Rd [A] [Value] NA P */
static int w9968cf_i2c_r(struct sd *sd, u8 reg)
{
	struct gspca_dev *gspca_dev = (struct gspca_dev *)sd;
	int ret = 0;
	u8 value;

	/* Fast serial bus data control disable */
	w9968cf_write_sb(sd, 0x0013); /* don't change ! */

	w9968cf_smbus_start(sd);
	w9968cf_smbus_write_byte(sd, sd->sensor_addr);
	w9968cf_smbus_read_ack(sd);
	w9968cf_smbus_write_byte(sd, reg);
	w9968cf_smbus_read_ack(sd);
	w9968cf_smbus_stop(sd);
	w9968cf_smbus_start(sd);
	w9968cf_smbus_write_byte(sd, sd->sensor_addr + 1);
	w9968cf_smbus_read_ack(sd);
	w9968cf_smbus_read_byte(sd, &value);
	/* signal we don't want to read anymore, the v4l1 driver used to
	   send an ack here which is very wrong! (and then fixed
	   the issues this gave by retrying reads) */
	w9968cf_smbus_write_nack(sd);
	w9968cf_smbus_stop(sd);

	/* Fast serial bus data control re-enable */
	w9968cf_write_sb(sd, 0x0030);

	if (sd->gspca_dev.usb_err >= 0) {
		ret = value;
		gspca_dbg(gspca_dev, D_USBI, "i2c [0x%02X] -> 0x%02X\n",
			  reg, value);
	} else
		gspca_err(gspca_dev, "i2c read [0x%02x] failed\n", reg);

	return ret;
}

/*--------------------------------------------------------------------------
  Turn on the LED on some webcams. A beep should be heard too.
  Return 0 on success, a negative number otherwise.
  --------------------------------------------------------------------------*/
static void w9968cf_configure(struct sd *sd)
{
	reg_w(sd, 0x00, 0xff00); /* power-down */
	reg_w(sd, 0x00, 0xbf17); /* reset everything */
	reg_w(sd, 0x00, 0xbf10); /* normal operation */
	reg_w(sd, 0x01, 0x0010); /* serial bus, SDS high */
	reg_w(sd, 0x01, 0x0000); /* serial bus, SDS low */
	reg_w(sd, 0x01, 0x0010); /* ..high 'beep-beep' */
	reg_w(sd, 0x01, 0x0030); /* Set sda scl to FSB mode */

	sd->stopped = 1;
}

static void w9968cf_init(struct sd *sd)
{
	unsigned long hw_bufsize = sd->sif ? (352 * 288 * 2) : (640 * 480 * 2),
		      y0 = 0x0000,
		      u0 = y0 + hw_bufsize / 2,
		      v0 = u0 + hw_bufsize / 4,
		      y1 = v0 + hw_bufsize / 4,
		      u1 = y1 + hw_bufsize / 2,
		      v1 = u1 + hw_bufsize / 4;

	reg_w(sd, 0x00, 0xff00); /* power off */
	reg_w(sd, 0x00, 0xbf10); /* power on */

	reg_w(sd, 0x03, 0x405d); /* DRAM timings */
	reg_w(sd, 0x04, 0x0030); /* SDRAM timings */

	reg_w(sd, 0x20, y0 & 0xffff); /* Y buf.0, low */
	reg_w(sd, 0x21, y0 >> 16);    /* Y buf.0, high */
	reg_w(sd, 0x24, u0 & 0xffff); /* U buf.0, low */
	reg_w(sd, 0x25, u0 >> 16);    /* U buf.0, high */
	reg_w(sd, 0x28, v0 & 0xffff); /* V buf.0, low */
	reg_w(sd, 0x29, v0 >> 16);    /* V buf.0, high */

	reg_w(sd, 0x22, y1 & 0xffff); /* Y buf.1, low */
	reg_w(sd, 0x23, y1 >> 16);    /* Y buf.1, high */
	reg_w(sd, 0x26, u1 & 0xffff); /* U buf.1, low */
	reg_w(sd, 0x27, u1 >> 16);    /* U buf.1, high */
	reg_w(sd, 0x2a, v1 & 0xffff); /* V buf.1, low */
	reg_w(sd, 0x2b, v1 >> 16);    /* V buf.1, high */

	reg_w(sd, 0x32, y1 & 0xffff); /* JPEG buf 0 low */
	reg_w(sd, 0x33, y1 >> 16);    /* JPEG buf 0 high */

	reg_w(sd, 0x34, y1 & 0xffff); /* JPEG buf 1 low */
	reg_w(sd, 0x35, y1 >> 16);    /* JPEG bug 1 high */

	reg_w(sd, 0x36, 0x0000);/* JPEG restart interval */
	reg_w(sd, 0x37, 0x0804);/*JPEG VLE FIFO threshold*/
	reg_w(sd, 0x38, 0x0000);/* disable hw up-scaling */
	reg_w(sd, 0x3f, 0x0000); /* JPEG/MCTL test data */
}

static void w9968cf_set_crop_window(struct sd *sd)
{
	int start_cropx, start_cropy,  x, y, fw, fh, cw, ch,
	    max_width, max_height;

	if (sd->sif) {
		max_width  = 352;
		max_height = 288;
	} else {
		max_width  = 640;
		max_height = 480;
	}

	if (sd->sensor == SEN_OV7620) {
		/*
		 * Sigh, this is dependend on the clock / framerate changes
		 * made by the frequency control, sick.
		 *
		 * Note we cannot use v4l2_ctrl_g_ctrl here, as we get called
		 * from ov519.c:setfreq() with the ctrl lock held!
		 */
		if (sd->freq->val == 1) {
			start_cropx = 277;
			start_cropy = 37;
		} else {
			start_cropx = 105;
			start_cropy = 37;
		}
	} else {
		start_cropx = 320;
		start_cropy = 35;
	}

	/* Work around to avoid FP arithmetic */
	#define SC(x) ((x) << 10)

	/* Scaling factors */
	fw = SC(sd->gspca_dev.pixfmt.width) / max_width;
	fh = SC(sd->gspca_dev.pixfmt.height) / max_height;

	cw = (fw >= fh) ? max_width : SC(sd->gspca_dev.pixfmt.width) / fh;
	ch = (fw >= fh) ? SC(sd->gspca_dev.pixfmt.height) / fw : max_height;

	sd->sensor_width = max_width;
	sd->sensor_height = max_height;

	x = (max_width - cw) / 2;
	y = (max_height - ch) / 2;

	reg_w(sd, 0x10, start_cropx + x);
	reg_w(sd, 0x11, start_cropy + y);
	reg_w(sd, 0x12, start_cropx + x + cw);
	reg_w(sd, 0x13, start_cropy + y + ch);
}

static void w9968cf_mode_init_regs(struct sd *sd)
{
	int val, vs_polarity, hs_polarity;

	w9968cf_set_crop_window(sd);

	reg_w(sd, 0x14, sd->gspca_dev.pixfmt.width);
	reg_w(sd, 0x15, sd->gspca_dev.pixfmt.height);

	/* JPEG width & height */
	reg_w(sd, 0x30, sd->gspca_dev.pixfmt.width);
	reg_w(sd, 0x31, sd->gspca_dev.pixfmt.height);

	/* Y & UV frame buffer strides (in WORD) */
	if (w9968cf_vga_mode[sd->gspca_dev.curr_mode].pixelformat ==
	    V4L2_PIX_FMT_JPEG) {
		reg_w(sd, 0x2c, sd->gspca_dev.pixfmt.width / 2);
		reg_w(sd, 0x2d, sd->gspca_dev.pixfmt.width / 4);
	} else
		reg_w(sd, 0x2c, sd->gspca_dev.pixfmt.width);

	reg_w(sd, 0x00, 0xbf17); /* reset everything */
	reg_w(sd, 0x00, 0xbf10); /* normal operation */

	/* Transfer size in WORDS (for UYVY format only) */
	val = sd->gspca_dev.pixfmt.width * sd->gspca_dev.pixfmt.height;
	reg_w(sd, 0x3d, val & 0xffff); /* low bits */
	reg_w(sd, 0x3e, val >> 16);    /* high bits */

	if (w9968cf_vga_mode[sd->gspca_dev.curr_mode].pixelformat ==
	    V4L2_PIX_FMT_JPEG) {
		/* We may get called multiple times (usb isoc bw negotiat.) */
		jpeg_define(sd->jpeg_hdr, sd->gspca_dev.pixfmt.height,
			    sd->gspca_dev.pixfmt.width, 0x22); /* JPEG 420 */
		jpeg_set_qual(sd->jpeg_hdr, v4l2_ctrl_g_ctrl(sd->jpegqual));
		w9968cf_upload_quantizationtables(sd);
		v4l2_ctrl_grab(sd->jpegqual, true);
	}

	/* Video Capture Control Register */
	if (sd->sensor == SEN_OV7620) {
		/* Seems to work around a bug in the image sensor */
		vs_polarity = 1;
		hs_polarity = 1;
	} else {
		vs_polarity = 1;
		hs_polarity = 0;
	}

	val = (vs_polarity << 12) | (hs_polarity << 11);

	/* NOTE: We may not have enough memory to do double buffering while
	   doing compression (amount of memory differs per model cam).
	   So we use the second image buffer also as jpeg stream buffer
	   (see w9968cf_init), and disable double buffering. */
	if (w9968cf_vga_mode[sd->gspca_dev.curr_mode].pixelformat ==
	    V4L2_PIX_FMT_JPEG) {
		/* val |= 0x0002; YUV422P */
		val |= 0x0003; /* YUV420P */
	} else
		val |= 0x0080; /* Enable HW double buffering */

	/* val |= 0x0020; enable clamping */
	/* val |= 0x0008; enable (1-2-1) filter */
	/* val |= 0x000c; enable (2-3-6-3-2) filter */

	val |= 0x8000; /* capt. enable */

	reg_w(sd, 0x16, val);

	sd->gspca_dev.empty_packet = 0;
}

static void w9968cf_stop0(struct sd *sd)
{
	v4l2_ctrl_grab(sd->jpegqual, false);
	reg_w(sd, 0x39, 0x0000); /* disable JPEG encoder */
	reg_w(sd, 0x16, 0x0000); /* stop video capture */
}

/* The w9968cf docs say that a 0 sized packet means EOF (and also SOF
   for the next frame). This seems to simply not be true when operating
   in JPEG mode, in this case there may be empty packets within the
   frame. So in JPEG mode use the JPEG SOI marker to detect SOF.

   Note to make things even more interesting the w9968cf sends *PLANAR* jpeg,
   to be precise it sends: SOI, SOF, DRI, SOS, Y-data, SOS, U-data, SOS,
   V-data, EOI. */
static void w9968cf_pkt_scan(struct gspca_dev *gspca_dev,
			u8 *data,			/* isoc packet */
			int len)			/* iso packet length */
{
	struct sd *sd = (struct sd *) gspca_dev;

	if (w9968cf_vga_mode[gspca_dev->curr_mode].pixelformat ==
	    V4L2_PIX_FMT_JPEG) {
		if (len >= 2 &&
		    data[0] == 0xff &&
		    data[1] == 0xd8) {
			gspca_frame_add(gspca_dev, LAST_PACKET,
					NULL, 0);
			gspca_frame_add(gspca_dev, FIRST_PACKET,
					sd->jpeg_hdr, JPEG_HDR_SZ);
			/* Strip the ff d8, our own header (which adds
			   huffman and quantization tables) already has this */
			len -= 2;
			data += 2;
		}
	} else {
		/* In UYVY mode an empty packet signals EOF */
		if (gspca_dev->empty_packet) {
			gspca_frame_add(gspca_dev, LAST_PACKET,
						NULL, 0);
			gspca_frame_add(gspca_dev, FIRST_PACKET,
					NULL, 0);
			gspca_dev->empty_packet = 0;
		}
	}
	gspca_frame_add(gspca_dev, INTER_PACKET, data, len);
}

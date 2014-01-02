/*
 * Syntek STK1135 subdriver
 *
 * Copyright (c) 2013 Ondrej Zary
 *
 * Based on Syntekdriver (stk11xx) by Nicolas VIVIEN:
 *   http://syntekdriver.sourceforge.net
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

#define MODULE_NAME "stk1135"

#include "gspca.h"
#include "stk1135.h"

MODULE_AUTHOR("Ondrej Zary");
MODULE_DESCRIPTION("Syntek STK1135 USB Camera Driver");
MODULE_LICENSE("GPL");


/* specific webcam descriptor */
struct sd {
	struct gspca_dev gspca_dev;	/* !! must be the first item */

	u8 pkt_seq;
	u8 sensor_page;

	bool flip_status;
	u8 flip_debounce;

	struct v4l2_ctrl *hflip;
	struct v4l2_ctrl *vflip;
};

static const struct v4l2_pix_format stk1135_modes[] = {
	/* default mode (this driver supports variable resolution) */
	{640, 480, V4L2_PIX_FMT_SBGGR8, V4L2_FIELD_NONE,
		.bytesperline = 640,
		.sizeimage = 640 * 480,
		.colorspace = V4L2_COLORSPACE_SRGB},
};

/* -- read a register -- */
static u8 reg_r(struct gspca_dev *gspca_dev, u16 index)
{
	struct usb_device *dev = gspca_dev->dev;
	int ret;

	if (gspca_dev->usb_err < 0)
		return 0;
	ret = usb_control_msg(dev, usb_rcvctrlpipe(dev, 0),
			0x00,
			USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			0x00,
			index,
			gspca_dev->usb_buf, 1,
			500);

	PDEBUG(D_USBI, "reg_r 0x%x=0x%02x", index, gspca_dev->usb_buf[0]);
	if (ret < 0) {
		pr_err("reg_r 0x%x err %d\n", index, ret);
		gspca_dev->usb_err = ret;
		return 0;
	}

	return gspca_dev->usb_buf[0];
}

/* -- write a register -- */
static void reg_w(struct gspca_dev *gspca_dev, u16 index, u8 val)
{
	int ret;
	struct usb_device *dev = gspca_dev->dev;

	if (gspca_dev->usb_err < 0)
		return;
	ret = usb_control_msg(dev, usb_sndctrlpipe(dev, 0),
			0x01,
			USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			val,
			index,
			NULL,
			0,
			500);
	PDEBUG(D_USBO, "reg_w 0x%x:=0x%02x", index, val);
	if (ret < 0) {
		pr_err("reg_w 0x%x err %d\n", index, ret);
		gspca_dev->usb_err = ret;
	}
}

static void reg_w_mask(struct gspca_dev *gspca_dev, u16 index, u8 val, u8 mask)
{
	val = (reg_r(gspca_dev, index) & ~mask) | (val & mask);
	reg_w(gspca_dev, index, val);
}

/* this function is called at probe time */
static int sd_config(struct gspca_dev *gspca_dev,
			const struct usb_device_id *id)
{
	gspca_dev->cam.cam_mode = stk1135_modes;
	gspca_dev->cam.nmodes = ARRAY_SIZE(stk1135_modes);
	return 0;
}

static int stk1135_serial_wait_ready(struct gspca_dev *gspca_dev)
{
	int i = 0;
	u8 val;

	do {
		val = reg_r(gspca_dev, STK1135_REG_SICTL + 1);
		if (i++ > 500) { /* maximum retry count */
			pr_err("serial bus timeout: status=0x%02x\n", val);
			return -1;
		}
	/* repeat if BUSY or WRITE/READ not finished */
	} while ((val & 0x10) || !(val & 0x05));

	return 0;
}

static u8 sensor_read_8(struct gspca_dev *gspca_dev, u8 addr)
{
	reg_w(gspca_dev, STK1135_REG_SBUSR, addr);
	/* begin read */
	reg_w(gspca_dev, STK1135_REG_SICTL, 0x20);
	/* wait until finished */
	if (stk1135_serial_wait_ready(gspca_dev)) {
		pr_err("Sensor read failed\n");
		return 0;
	}

	return reg_r(gspca_dev, STK1135_REG_SBUSR + 1);
}

static u16 sensor_read_16(struct gspca_dev *gspca_dev, u8 addr)
{
	return (sensor_read_8(gspca_dev, addr) << 8) |
		sensor_read_8(gspca_dev, 0xf1);
}

static void sensor_write_8(struct gspca_dev *gspca_dev, u8 addr, u8 data)
{
	/* load address and data registers */
	reg_w(gspca_dev, STK1135_REG_SBUSW, addr);
	reg_w(gspca_dev, STK1135_REG_SBUSW + 1, data);
	/* begin write */
	reg_w(gspca_dev, STK1135_REG_SICTL, 0x01);
	/* wait until finished */
	if (stk1135_serial_wait_ready(gspca_dev)) {
		pr_err("Sensor write failed\n");
		return;
	}
}

static void sensor_write_16(struct gspca_dev *gspca_dev, u8 addr, u16 data)
{
	sensor_write_8(gspca_dev, addr, data >> 8);
	sensor_write_8(gspca_dev, 0xf1, data & 0xff);
}

static void sensor_set_page(struct gspca_dev *gspca_dev, u8 page)
{
	struct sd *sd = (struct sd *) gspca_dev;

	if (page != sd->sensor_page) {
		sensor_write_16(gspca_dev, 0xf0, page);
		sd->sensor_page = page;
	}
}

static u16 sensor_read(struct gspca_dev *gspca_dev, u16 reg)
{
	sensor_set_page(gspca_dev, reg >> 8);
	return sensor_read_16(gspca_dev, reg & 0xff);
}

static void sensor_write(struct gspca_dev *gspca_dev, u16 reg, u16 val)
{
	sensor_set_page(gspca_dev, reg >> 8);
	sensor_write_16(gspca_dev, reg & 0xff, val);
}

static void sensor_write_mask(struct gspca_dev *gspca_dev,
			u16 reg, u16 val, u16 mask)
{
	val = (sensor_read(gspca_dev, reg) & ~mask) | (val & mask);
	sensor_write(gspca_dev, reg, val);
}

struct sensor_val {
	u16 reg;
	u16 val;
};

/* configure MT9M112 sensor */
static void stk1135_configure_mt9m112(struct gspca_dev *gspca_dev)
{
	static const struct sensor_val cfg[] = {
		/* restart&reset, chip enable, reserved */
		{ 0x00d, 0x000b }, { 0x00d, 0x0008 }, { 0x035, 0x0022 },
		/* mode ctl: AWB on, AE both, clip aper corr, defect corr, AE */
		{ 0x106, 0x700e },

		{ 0x2dd, 0x18e0 }, /* B-R thresholds, */

		/* AWB */
		{ 0x21f, 0x0180 }, /* Cb and Cr limits */
		{ 0x220, 0xc814 }, { 0x221, 0x8080 }, /* lum limits, RGB gain */
		{ 0x222, 0xa078 }, { 0x223, 0xa078 }, /* R, B limit */
		{ 0x224, 0x5f20 }, { 0x228, 0xea02 }, /* mtx adj lim, adv ctl */
		{ 0x229, 0x867a }, /* wide gates */

		/* Color correction */
		/* imager gains base, delta, delta signs */
		{ 0x25e, 0x594c }, { 0x25f, 0x4d51 }, { 0x260, 0x0002 },
		/* AWB adv ctl 2, gain offs */
		{ 0x2ef, 0x0008 }, { 0x2f2, 0x0000 },
		/* base matrix signs, scale K1-5, K6-9 */
		{ 0x202, 0x00ee }, { 0x203, 0x3923 }, { 0x204, 0x0724 },
		/* base matrix coef */
		{ 0x209, 0x00cd }, { 0x20a, 0x0093 }, { 0x20b, 0x0004 },/*K1-3*/
		{ 0x20c, 0x005c }, { 0x20d, 0x00d9 }, { 0x20e, 0x0053 },/*K4-6*/
		{ 0x20f, 0x0008 }, { 0x210, 0x0091 }, { 0x211, 0x00cf },/*K7-9*/
		{ 0x215, 0x0000 }, /* delta mtx signs */
		/* delta matrix coef */
		{ 0x216, 0x0000 }, { 0x217, 0x0000 }, { 0x218, 0x0000 },/*D1-3*/
		{ 0x219, 0x0000 }, { 0x21a, 0x0000 }, { 0x21b, 0x0000 },/*D4-6*/
		{ 0x21c, 0x0000 }, { 0x21d, 0x0000 }, { 0x21e, 0x0000 },/*D7-9*/
		/* enable & disable manual WB to apply color corr. settings */
		{ 0x106, 0xf00e }, { 0x106, 0x700e },

		/* Lens shading correction */
		{ 0x180, 0x0007 }, /* control */
		/* vertical knee 0, 2+1, 4+3 */
		{ 0x181, 0xde13 }, { 0x182, 0xebe2 }, { 0x183, 0x00f6 }, /* R */
		{ 0x184, 0xe114 }, { 0x185, 0xeadd }, { 0x186, 0xfdf6 }, /* G */
		{ 0x187, 0xe511 }, { 0x188, 0xede6 }, { 0x189, 0xfbf7 }, /* B */
		/* horizontal knee 0, 2+1, 4+3, 5 */
		{ 0x18a, 0xd613 }, { 0x18b, 0xedec }, /* R .. */
		{ 0x18c, 0xf9f2 }, { 0x18d, 0x0000 }, /* .. R */
		{ 0x18e, 0xd815 }, { 0x18f, 0xe9ea }, /* G .. */
		{ 0x190, 0xf9f1 }, { 0x191, 0x0002 }, /* .. G */
		{ 0x192, 0xde10 }, { 0x193, 0xefef }, /* B .. */
		{ 0x194, 0xfbf4 }, { 0x195, 0x0002 }, /* .. B */
		/* vertical knee 6+5, 8+7 */
		{ 0x1b6, 0x0e06 }, { 0x1b7, 0x2713 }, /* R */
		{ 0x1b8, 0x1106 }, { 0x1b9, 0x2713 }, /* G */
		{ 0x1ba, 0x0c03 }, { 0x1bb, 0x2a0f }, /* B */
		/* horizontal knee 7+6, 9+8, 10 */
		{ 0x1bc, 0x1208 }, { 0x1bd, 0x1a16 }, { 0x1be, 0x0022 }, /* R */
		{ 0x1bf, 0x150a }, { 0x1c0, 0x1c1a }, { 0x1c1, 0x002d }, /* G */
		{ 0x1c2, 0x1109 }, { 0x1c3, 0x1414 }, { 0x1c4, 0x002a }, /* B */
		{ 0x106, 0x740e }, /* enable lens shading correction */

		/* Gamma correction - context A */
		{ 0x153, 0x0b03 }, { 0x154, 0x4722 }, { 0x155, 0xac82 },
		{ 0x156, 0xdac7 }, { 0x157, 0xf5e9 }, { 0x158, 0xff00 },
		/* Gamma correction - context B */
		{ 0x1dc, 0x0b03 }, { 0x1dd, 0x4722 }, { 0x1de, 0xac82 },
		{ 0x1df, 0xdac7 }, { 0x1e0, 0xf5e9 }, { 0x1e1, 0xff00 },

		/* output format: RGB, invert output pixclock, output bayer */
		{ 0x13a, 0x4300 }, { 0x19b, 0x4300 }, /* for context A, B */
		{ 0x108, 0x0180 }, /* format control - enable bayer row flip */

		{ 0x22f, 0xd100 }, { 0x29c, 0xd100 }, /* AE A, B */

		/* default prg conf, prg ctl - by 0x2d2, prg advance - PA1 */
		{ 0x2d2, 0x0000 }, { 0x2cc, 0x0004 }, { 0x2cb, 0x0001 },

		{ 0x22e, 0x0c3c }, { 0x267, 0x1010 }, /* AE tgt ctl, gain lim */

		/* PLL */
		{ 0x065, 0xa000 }, /* clk ctl - enable PLL (clear bit 14) */
		{ 0x066, 0x2003 }, { 0x067, 0x0501 }, /* PLL M=128, N=3, P=1 */
		{ 0x065, 0x2000 }, /* disable PLL bypass (clear bit 15) */

		{ 0x005, 0x01b8 }, { 0x007, 0x00d8 }, /* horiz blanking B, A */

		/* AE line size, shutter delay limit */
		{ 0x239, 0x06c0 }, { 0x23b, 0x040e }, /* for context A */
		{ 0x23a, 0x06c0 }, { 0x23c, 0x0564 }, /* for context B */
		/* shutter width basis 60Hz, 50Hz */
		{ 0x257, 0x0208 }, { 0x258, 0x0271 }, /* for context A */
		{ 0x259, 0x0209 }, { 0x25a, 0x0271 }, /* for context B */

		{ 0x25c, 0x120d }, { 0x25d, 0x1712 }, /* flicker 60Hz, 50Hz */
		{ 0x264, 0x5e1c }, /* reserved */
		/* flicker, AE gain limits, gain zone limits */
		{ 0x25b, 0x0003 }, { 0x236, 0x7810 }, { 0x237, 0x8304 },

		{ 0x008, 0x0021 }, /* vert blanking A */
	};
	int i;
	u16 width, height;

	for (i = 0; i < ARRAY_SIZE(cfg); i++)
		sensor_write(gspca_dev, cfg[i].reg, cfg[i].val);

	/* set output size */
	width = gspca_dev->pixfmt.width;
	height = gspca_dev->pixfmt.height;
	if (width <= 640 && height <= 512) { /* context A (half readout speed)*/
		sensor_write(gspca_dev, 0x1a7, width);
		sensor_write(gspca_dev, 0x1aa, height);
		/* set read mode context A */
		sensor_write(gspca_dev, 0x0c8, 0x0000);
		/* set resize, read mode, vblank, hblank context A */
		sensor_write(gspca_dev, 0x2c8, 0x0000);
	} else { /* context B (full readout speed) */
		sensor_write(gspca_dev, 0x1a1, width);
		sensor_write(gspca_dev, 0x1a4, height);
		/* set read mode context B */
		sensor_write(gspca_dev, 0x0c8, 0x0008);
		/* set resize, read mode, vblank, hblank context B */
		sensor_write(gspca_dev, 0x2c8, 0x040b);
	}
}

static void stk1135_configure_clock(struct gspca_dev *gspca_dev)
{
	/* configure SCLKOUT */
	reg_w(gspca_dev, STK1135_REG_TMGEN, 0x12);
	/* set 1 clock per pixel */
	/* and positive edge clocked pulse high when pixel counter = 0 */
	reg_w(gspca_dev, STK1135_REG_TCP1 + 0, 0x41);
	reg_w(gspca_dev, STK1135_REG_TCP1 + 1, 0x00);
	reg_w(gspca_dev, STK1135_REG_TCP1 + 2, 0x00);
	reg_w(gspca_dev, STK1135_REG_TCP1 + 3, 0x00);

	/* enable CLKOUT for sensor */
	reg_w(gspca_dev, STK1135_REG_SENSO + 0, 0x10);
	/* disable STOP clock */
	reg_w(gspca_dev, STK1135_REG_SENSO + 1, 0x00);
	/* set lower 8 bits of PLL feedback divider */
	reg_w(gspca_dev, STK1135_REG_SENSO + 3, 0x07);
	/* set other PLL parameters */
	reg_w(gspca_dev, STK1135_REG_PLLFD, 0x06);
	/* enable timing generator */
	reg_w(gspca_dev, STK1135_REG_TMGEN, 0x80);
	/* enable PLL */
	reg_w(gspca_dev, STK1135_REG_SENSO + 2, 0x04);

	/* set serial interface clock divider (30MHz/0x1f*16+2) = 60240 kHz) */
	reg_w(gspca_dev, STK1135_REG_SICTL + 2, 0x1f);
}

static void stk1135_camera_disable(struct gspca_dev *gspca_dev)
{
	/* set capture end Y position to 0 */
	reg_w(gspca_dev, STK1135_REG_CIEPO + 2, 0x00);
	reg_w(gspca_dev, STK1135_REG_CIEPO + 3, 0x00);
	/* disable capture */
	reg_w_mask(gspca_dev, STK1135_REG_SCTRL, 0x00, 0x80);

	/* enable sensor standby and diasble chip enable */
	sensor_write_mask(gspca_dev, 0x00d, 0x0004, 0x000c);

	/* disable PLL */
	reg_w_mask(gspca_dev, STK1135_REG_SENSO + 2, 0x00, 0x01);
	/* disable timing generator */
	reg_w(gspca_dev, STK1135_REG_TMGEN, 0x00);
	/* enable STOP clock */
	reg_w(gspca_dev, STK1135_REG_SENSO + 1, 0x20);
	/* disable CLKOUT for sensor */
	reg_w(gspca_dev, STK1135_REG_SENSO, 0x00);

	/* disable sensor (GPIO5) and enable GPIO0,3,6 (?) - sensor standby? */
	reg_w(gspca_dev, STK1135_REG_GCTRL, 0x49);
}

/* this function is called at probe and resume time */
static int sd_init(struct gspca_dev *gspca_dev)
{
	u16 sensor_id;
	char *sensor_name;
	struct sd *sd = (struct sd *) gspca_dev;

	/* set GPIO3,4,5,6 direction to output */
	reg_w(gspca_dev, STK1135_REG_GCTRL + 2, 0x78);
	/* enable sensor (GPIO5) */
	reg_w(gspca_dev, STK1135_REG_GCTRL, (1 << 5));
	/* disable ROM interface */
	reg_w(gspca_dev, STK1135_REG_GCTRL + 3, 0x80);
	/* enable interrupts from GPIO8 (flip sensor) and GPIO9 (???) */
	reg_w(gspca_dev, STK1135_REG_ICTRL + 1, 0x00);
	reg_w(gspca_dev, STK1135_REG_ICTRL + 3, 0x03);
	/* enable remote wakeup from GPIO9 (???) */
	reg_w(gspca_dev, STK1135_REG_RMCTL + 1, 0x00);
	reg_w(gspca_dev, STK1135_REG_RMCTL + 3, 0x02);

	/* reset serial interface */
	reg_w(gspca_dev, STK1135_REG_SICTL, 0x80);
	reg_w(gspca_dev, STK1135_REG_SICTL, 0x00);
	/* set sensor address */
	reg_w(gspca_dev, STK1135_REG_SICTL + 3, 0xba);
	/* disable alt 2-wire serial interface */
	reg_w(gspca_dev, STK1135_REG_ASIC + 3, 0x00);

	stk1135_configure_clock(gspca_dev);

	/* read sensor ID */
	sd->sensor_page = 0xff;
	sensor_id = sensor_read(gspca_dev, 0x000);

	switch (sensor_id) {
	case 0x148c:
		sensor_name = "MT9M112";
		break;
	default:
		sensor_name = "unknown";
	}
	pr_info("Detected sensor type %s (0x%x)\n", sensor_name, sensor_id);

	stk1135_camera_disable(gspca_dev);

	return gspca_dev->usb_err;
}

/* -- start the camera -- */
static int sd_start(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	u16 width, height;

	/* enable sensor (GPIO5) */
	reg_w(gspca_dev, STK1135_REG_GCTRL, (1 << 5));

	stk1135_configure_clock(gspca_dev);

	/* set capture start position X = 0, Y = 0 */
	reg_w(gspca_dev, STK1135_REG_CISPO + 0, 0x00);
	reg_w(gspca_dev, STK1135_REG_CISPO + 1, 0x00);
	reg_w(gspca_dev, STK1135_REG_CISPO + 2, 0x00);
	reg_w(gspca_dev, STK1135_REG_CISPO + 3, 0x00);

	/* set capture end position */
	width = gspca_dev->pixfmt.width;
	height = gspca_dev->pixfmt.height;
	reg_w(gspca_dev, STK1135_REG_CIEPO + 0, width & 0xff);
	reg_w(gspca_dev, STK1135_REG_CIEPO + 1, width >> 8);
	reg_w(gspca_dev, STK1135_REG_CIEPO + 2, height & 0xff);
	reg_w(gspca_dev, STK1135_REG_CIEPO + 3, height >> 8);

	/* set 8-bit mode */
	reg_w(gspca_dev, STK1135_REG_SCTRL, 0x20);

	stk1135_configure_mt9m112(gspca_dev);

	/* enable capture */
	reg_w_mask(gspca_dev, STK1135_REG_SCTRL, 0x80, 0x80);

	if (gspca_dev->usb_err >= 0)
		PDEBUG(D_STREAM, "camera started alt: 0x%02x",
				gspca_dev->alt);

	sd->pkt_seq = 0;

	return gspca_dev->usb_err;
}

static void sd_stopN(struct gspca_dev *gspca_dev)
{
	struct usb_device *dev = gspca_dev->dev;

	usb_set_interface(dev, gspca_dev->iface, 0);

	stk1135_camera_disable(gspca_dev);

	PDEBUG(D_STREAM, "camera stopped");
}

static void sd_pkt_scan(struct gspca_dev *gspca_dev,
			u8 *data,			/* isoc packet */
			int len)			/* iso packet length */
{
	struct sd *sd = (struct sd *) gspca_dev;
	int skip = sizeof(struct stk1135_pkt_header);
	bool flip;
	enum gspca_packet_type pkt_type = INTER_PACKET;
	struct stk1135_pkt_header *hdr = (void *)data;
	u8 seq;

	if (len < 4) {
		PDEBUG(D_PACK, "received short packet (less than 4 bytes)");
		return;
	}

	/* GPIO 8 is flip sensor (1 = normal position, 0 = flipped to back) */
	flip = !(le16_to_cpu(hdr->gpio) & (1 << 8));
	/* it's a switch, needs software debounce */
	if (sd->flip_status != flip)
		sd->flip_debounce++;
	else
		sd->flip_debounce = 0;

	/* check sequence number (not present in new frame packets) */
	if (!(hdr->flags & STK1135_HDR_FRAME_START)) {
		seq = hdr->seq & STK1135_HDR_SEQ_MASK;
		if (seq != sd->pkt_seq) {
			PDEBUG(D_PACK, "received out-of-sequence packet");
			/* resync sequence and discard packet */
			sd->pkt_seq = seq;
			gspca_dev->last_packet_type = DISCARD_PACKET;
			return;
		}
	}
	sd->pkt_seq++;
	if (sd->pkt_seq > STK1135_HDR_SEQ_MASK)
		sd->pkt_seq = 0;

	if (len == sizeof(struct stk1135_pkt_header))
		return;

	if (hdr->flags & STK1135_HDR_FRAME_START) { /* new frame */
		skip = 8;	/* the header is longer */
		gspca_frame_add(gspca_dev, LAST_PACKET, data, 0);
		pkt_type = FIRST_PACKET;
	}
	gspca_frame_add(gspca_dev, pkt_type, data + skip, len - skip);
}

static void sethflip(struct gspca_dev *gspca_dev, s32 val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	if (sd->flip_status)
		val = !val;
	sensor_write_mask(gspca_dev, 0x020, val ? 0x0002 : 0x0000 , 0x0002);
}

static void setvflip(struct gspca_dev *gspca_dev, s32 val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	if (sd->flip_status)
		val = !val;
	sensor_write_mask(gspca_dev, 0x020, val ? 0x0001 : 0x0000 , 0x0001);
}

static void stk1135_dq_callback(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;

	if (sd->flip_debounce > 100) {
		sd->flip_status = !sd->flip_status;
		sethflip(gspca_dev, v4l2_ctrl_g_ctrl(sd->hflip));
		setvflip(gspca_dev, v4l2_ctrl_g_ctrl(sd->vflip));
	}
}

static int sd_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct gspca_dev *gspca_dev =
		container_of(ctrl->handler, struct gspca_dev, ctrl_handler);

	gspca_dev->usb_err = 0;

	if (!gspca_dev->streaming)
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_HFLIP:
		sethflip(gspca_dev, ctrl->val);
		break;
	case V4L2_CID_VFLIP:
		setvflip(gspca_dev, ctrl->val);
		break;
	}

	return gspca_dev->usb_err;
}

static const struct v4l2_ctrl_ops sd_ctrl_ops = {
	.s_ctrl = sd_s_ctrl,
};

static int sd_init_controls(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	struct v4l2_ctrl_handler *hdl = &gspca_dev->ctrl_handler;

	gspca_dev->vdev.ctrl_handler = hdl;
	v4l2_ctrl_handler_init(hdl, 2);
	sd->hflip = v4l2_ctrl_new_std(hdl, &sd_ctrl_ops,
			V4L2_CID_HFLIP, 0, 1, 1, 0);
	sd->vflip = v4l2_ctrl_new_std(hdl, &sd_ctrl_ops,
			V4L2_CID_VFLIP, 0, 1, 1, 0);

	if (hdl->error) {
		pr_err("Could not initialize controls\n");
		return hdl->error;
	}
	return 0;
}

static void stk1135_try_fmt(struct gspca_dev *gspca_dev, struct v4l2_format *fmt)
{
	fmt->fmt.pix.width = clamp(fmt->fmt.pix.width, 32U, 1280U);
	fmt->fmt.pix.height = clamp(fmt->fmt.pix.height, 32U, 1024U);
	/* round up to even numbers */
	fmt->fmt.pix.width += (fmt->fmt.pix.width & 1);
	fmt->fmt.pix.height += (fmt->fmt.pix.height & 1);

	fmt->fmt.pix.bytesperline = fmt->fmt.pix.width;
	fmt->fmt.pix.sizeimage = fmt->fmt.pix.width * fmt->fmt.pix.height;
}

static int stk1135_enum_framesizes(struct gspca_dev *gspca_dev,
			struct v4l2_frmsizeenum *fsize)
{
	if (fsize->index != 0 || fsize->pixel_format != V4L2_PIX_FMT_SBGGR8)
		return -EINVAL;

	fsize->type = V4L2_FRMSIZE_TYPE_STEPWISE;
	fsize->stepwise.min_width = 32;
	fsize->stepwise.min_height = 32;
	fsize->stepwise.max_width = 1280;
	fsize->stepwise.max_height = 1024;
	fsize->stepwise.step_width = 2;
	fsize->stepwise.step_height = 2;

	return 0;
}

/* sub-driver description */
static const struct sd_desc sd_desc = {
	.name = MODULE_NAME,
	.config = sd_config,
	.init = sd_init,
	.init_controls = sd_init_controls,
	.start = sd_start,
	.stopN = sd_stopN,
	.pkt_scan = sd_pkt_scan,
	.dq_callback = stk1135_dq_callback,
	.try_fmt = stk1135_try_fmt,
	.enum_framesizes = stk1135_enum_framesizes,
};

/* -- module initialisation -- */
static const struct usb_device_id device_table[] = {
	{USB_DEVICE(0x174f, 0x6a31)},	/* ASUS laptop, MT9M112 sensor */
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
	.reset_resume = gspca_resume,
#endif
};

module_usb_driver(sd_driver);

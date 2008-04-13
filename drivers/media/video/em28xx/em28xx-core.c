/*
   em28xx-core.c - driver for Empia EM2800/EM2820/2840 USB video capture devices

   Copyright (C) 2005 Ludovico Cavedon <cavedon@sssup.it>
		      Markus Rechberger <mrechberger@gmail.com>
		      Mauro Carvalho Chehab <mchehab@infradead.org>
		      Sascha Sommer <saschasommer@freenet.de>

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
#include <linux/list.h>
#include <linux/module.h>
#include <linux/usb.h>
#include <linux/vmalloc.h>

#include "em28xx.h"

/* #define ENABLE_DEBUG_ISOC_FRAMES */

static unsigned int core_debug;
module_param(core_debug,int,0644);
MODULE_PARM_DESC(core_debug,"enable debug messages [core]");

#define em28xx_coredbg(fmt, arg...) do {\
	if (core_debug) \
		printk(KERN_INFO "%s %s :"fmt, \
			 dev->name, __func__ , ##arg); } while (0)

static unsigned int reg_debug;
module_param(reg_debug,int,0644);
MODULE_PARM_DESC(reg_debug,"enable debug messages [URB reg]");

#define em28xx_regdbg(fmt, arg...) do {\
	if (reg_debug) \
		printk(KERN_INFO "%s %s :"fmt, \
			 dev->name, __func__ , ##arg); } while (0)

static int alt = EM28XX_PINOUT;
module_param(alt, int, 0644);
MODULE_PARM_DESC(alt, "alternate setting to use for video endpoint");

/*
 * em28xx_read_reg_req()
 * reads data from the usb device specifying bRequest
 */
int em28xx_read_reg_req_len(struct em28xx *dev, u8 req, u16 reg,
				   char *buf, int len)
{
	int ret, byte;

	if (dev->state & DEV_DISCONNECTED)
		return(-ENODEV);

	em28xx_regdbg("req=%02x, reg=%02x ", req, reg);

	ret = usb_control_msg(dev->udev, usb_rcvctrlpipe(dev->udev, 0), req,
			      USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			      0x0000, reg, buf, len, HZ);

	if (reg_debug){
		printk(ret < 0 ? " failed!\n" : "%02x values: ", ret);
		for (byte = 0; byte < len; byte++) {
			printk(" %02x", (unsigned char)buf[byte]);
		}
		printk("\n");
	}

	return ret;
}

/*
 * em28xx_read_reg_req()
 * reads data from the usb device specifying bRequest
 */
int em28xx_read_reg_req(struct em28xx *dev, u8 req, u16 reg)
{
	u8 val;
	int ret;

	if (dev->state & DEV_DISCONNECTED)
		return(-ENODEV);

	em28xx_regdbg("req=%02x, reg=%02x:", req, reg);

	ret = usb_control_msg(dev->udev, usb_rcvctrlpipe(dev->udev, 0), req,
			      USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			      0x0000, reg, &val, 1, HZ);

	if (reg_debug)
		printk(ret < 0 ? " failed!\n" :
				 "%02x\n", (unsigned char) val);

	if (ret < 0)
		return ret;

	return val;
}

int em28xx_read_reg(struct em28xx *dev, u16 reg)
{
	return em28xx_read_reg_req(dev, USB_REQ_GET_STATUS, reg);
}

/*
 * em28xx_write_regs_req()
 * sends data to the usb device, specifying bRequest
 */
int em28xx_write_regs_req(struct em28xx *dev, u8 req, u16 reg, char *buf,
				 int len)
{
	int ret;

	/*usb_control_msg seems to expect a kmalloced buffer */
	unsigned char *bufs;

	if (dev->state & DEV_DISCONNECTED)
		return(-ENODEV);

	bufs = kmalloc(len, GFP_KERNEL);

	em28xx_regdbg("req=%02x reg=%02x:", req, reg);

	if (reg_debug) {
		int i;
		for (i = 0; i < len; ++i)
			printk (" %02x", (unsigned char)buf[i]);
		printk ("\n");
	}

	if (!bufs)
		return -ENOMEM;
	memcpy(bufs, buf, len);
	ret = usb_control_msg(dev->udev, usb_sndctrlpipe(dev->udev, 0), req,
			      USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			      0x0000, reg, bufs, len, HZ);
	msleep(5);		/* FIXME: magic number */
	kfree(bufs);
	return ret;
}

int em28xx_write_regs(struct em28xx *dev, u16 reg, char *buf, int len)
{
	return em28xx_write_regs_req(dev, USB_REQ_GET_STATUS, reg, buf, len);
}

/*
 * em28xx_write_reg_bits()
 * sets only some bits (specified by bitmask) of a register, by first reading
 * the actual value
 */
static int em28xx_write_reg_bits(struct em28xx *dev, u16 reg, u8 val,
				 u8 bitmask)
{
	int oldval;
	u8 newval;
	if ((oldval = em28xx_read_reg(dev, reg)) < 0)
		return oldval;
	newval = (((u8) oldval) & ~bitmask) | (val & bitmask);
	return em28xx_write_regs(dev, reg, &newval, 1);
}

/*
 * em28xx_write_ac97()
 * write a 16 bit value to the specified AC97 address (LSB first!)
 */
static int em28xx_write_ac97(struct em28xx *dev, u8 reg, u8 *val)
{
	int ret, i;
	u8 addr = reg & 0x7f;
	if ((ret = em28xx_write_regs(dev, AC97LSB_REG, val, 2)) < 0)
		return ret;
	if ((ret = em28xx_write_regs(dev, AC97ADDR_REG, &addr, 1)) < 0)
		return ret;

	/* Wait up to 50 ms for AC97 command to complete */
	for (i = 0; i < 10; i++) {
		if ((ret = em28xx_read_reg(dev, AC97BUSY_REG)) < 0)
			return ret;
		if (!(ret & 0x01))
			return 0;
		msleep(5);
	}
	em28xx_warn ("AC97 command still being executed: not handled properly!\n");
	return 0;
}

static int em28xx_set_audio_source(struct em28xx *dev)
{
	static char *enable  = "\x08\x08";
	static char *disable = "\x08\x88";
	char *video = enable, *line = disable;
	int ret;
	u8 input;

	if (dev->is_em2800) {
		if (dev->ctl_ainput)
			input = EM2800_AUDIO_SRC_LINE;
		else
			input = EM2800_AUDIO_SRC_TUNER;

		ret = em28xx_write_regs(dev, EM2800_AUDIOSRC_REG, &input, 1);
		if (ret < 0)
			return ret;
	}

	if (dev->has_msp34xx)
		input = EM28XX_AUDIO_SRC_TUNER;
	else {
		switch (dev->ctl_ainput) {
		case EM28XX_AMUX_VIDEO:
			input = EM28XX_AUDIO_SRC_TUNER;
			break;
		case EM28XX_AMUX_LINE_IN:
			input = EM28XX_AUDIO_SRC_LINE;
			break;
		case EM28XX_AMUX_AC97_VIDEO:
			input = EM28XX_AUDIO_SRC_LINE;
			break;
		case EM28XX_AMUX_AC97_LINE_IN:
			input = EM28XX_AUDIO_SRC_LINE;
			video = disable;
			line  = enable;
			break;
		}
	}

	ret = em28xx_write_reg_bits(dev, AUDIOSRC_REG, input, 0xc0);
	if (ret < 0)
		return ret;
	msleep(5);

	/* Sets AC97 mixer registers
	   This is seems to be needed, even for non-ac97 configs
	 */
	ret = em28xx_write_ac97(dev, VIDEO_AC97, video);
	if (ret < 0)
		return ret;

	ret = em28xx_write_ac97(dev, LINE_IN_AC97, line);

	return ret;
}

int em28xx_audio_analog_set(struct em28xx *dev)
{
	int ret;
	char s[2] = { 0x00, 0x00 };
	u8 xclk = 0x07;

	s[0] |= 0x1f - dev->volume;
	s[1] |= 0x1f - dev->volume;

	/* Mute */
	s[1] |= 0x80;
	ret = em28xx_write_ac97(dev, MASTER_AC97, s);

	if (ret < 0)
		return ret;

	if (dev->has_12mhz_i2s)
		xclk |= 0x20;

	if (!dev->mute)
		xclk |= 0x80;

	ret = em28xx_write_reg_bits(dev, XCLK_REG, xclk, 0xa7);
	if (ret < 0)
		return ret;
	msleep(10);

	/* Selects the proper audio input */
	ret = em28xx_set_audio_source(dev);

	/* Unmute device */
	if (!dev->mute)
		s[1] &= ~0x80;
	ret = em28xx_write_ac97(dev, MASTER_AC97, s);

	return ret;
}
EXPORT_SYMBOL_GPL(em28xx_audio_analog_set);

int em28xx_colorlevels_set_default(struct em28xx *dev)
{
	em28xx_write_regs(dev, YGAIN_REG, "\x10", 1);	/* contrast */
	em28xx_write_regs(dev, YOFFSET_REG, "\x00", 1);	/* brightness */
	em28xx_write_regs(dev, UVGAIN_REG, "\x10", 1);	/* saturation */
	em28xx_write_regs(dev, UOFFSET_REG, "\x00", 1);
	em28xx_write_regs(dev, VOFFSET_REG, "\x00", 1);
	em28xx_write_regs(dev, SHARPNESS_REG, "\x00", 1);

	em28xx_write_regs(dev, GAMMA_REG, "\x20", 1);
	em28xx_write_regs(dev, RGAIN_REG, "\x20", 1);
	em28xx_write_regs(dev, GGAIN_REG, "\x20", 1);
	em28xx_write_regs(dev, BGAIN_REG, "\x20", 1);
	em28xx_write_regs(dev, ROFFSET_REG, "\x00", 1);
	em28xx_write_regs(dev, GOFFSET_REG, "\x00", 1);
	return em28xx_write_regs(dev, BOFFSET_REG, "\x00", 1);
}

int em28xx_capture_start(struct em28xx *dev, int start)
{
	int ret;
	/* FIXME: which is the best order? */
	/* video registers are sampled by VREF */
	if ((ret = em28xx_write_reg_bits(dev, USBSUSP_REG, start ? 0x10 : 0x00,
					  0x10)) < 0)
		return ret;
	/* enable video capture */
	return em28xx_write_regs(dev, VINENABLE_REG, start ? "\x67" : "\x27", 1);
}

int em28xx_outfmt_set_yuv422(struct em28xx *dev)
{
	em28xx_write_regs(dev, OUTFMT_REG, "\x34", 1);
	em28xx_write_regs(dev, VINMODE_REG, "\x10", 1);
	return em28xx_write_regs(dev, VINCTRL_REG, "\x11", 1);
}

static int em28xx_accumulator_set(struct em28xx *dev, u8 xmin, u8 xmax,
				  u8 ymin, u8 ymax)
{
	em28xx_coredbg("em28xx Scale: (%d,%d)-(%d,%d)\n", xmin, ymin, xmax, ymax);

	em28xx_write_regs(dev, XMIN_REG, &xmin, 1);
	em28xx_write_regs(dev, XMAX_REG, &xmax, 1);
	em28xx_write_regs(dev, YMIN_REG, &ymin, 1);
	return em28xx_write_regs(dev, YMAX_REG, &ymax, 1);
}

static int em28xx_capture_area_set(struct em28xx *dev, u8 hstart, u8 vstart,
				   u16 width, u16 height)
{
	u8 cwidth = width;
	u8 cheight = height;
	u8 overflow = (height >> 7 & 0x02) | (width >> 8 & 0x01);

	em28xx_coredbg("em28xx Area Set: (%d,%d)\n", (width | (overflow & 2) << 7),
			(height | (overflow & 1) << 8));

	em28xx_write_regs(dev, HSTART_REG, &hstart, 1);
	em28xx_write_regs(dev, VSTART_REG, &vstart, 1);
	em28xx_write_regs(dev, CWIDTH_REG, &cwidth, 1);
	em28xx_write_regs(dev, CHEIGHT_REG, &cheight, 1);
	return em28xx_write_regs(dev, OFLOW_REG, &overflow, 1);
}

static int em28xx_scaler_set(struct em28xx *dev, u16 h, u16 v)
{
	u8 mode;
	/* the em2800 scaler only supports scaling down to 50% */
	if(dev->is_em2800)
		mode = (v ? 0x20 : 0x00) | (h ? 0x10 : 0x00);
	else {
		u8 buf[2];
		buf[0] = h;
		buf[1] = h >> 8;
		em28xx_write_regs(dev, HSCALELOW_REG, (char *)buf, 2);
		buf[0] = v;
		buf[1] = v >> 8;
		em28xx_write_regs(dev, VSCALELOW_REG, (char *)buf, 2);
		/* it seems that both H and V scalers must be active to work correctly */
		mode = (h || v)? 0x30: 0x00;
	}
	return em28xx_write_reg_bits(dev, COMPR_REG, mode, 0x30);
}

/* FIXME: this only function read values from dev */
int em28xx_resolution_set(struct em28xx *dev)
{
	int width, height;
	width = norm_maxw(dev);
	height = norm_maxh(dev) >> 1;

	em28xx_outfmt_set_yuv422(dev);
	em28xx_accumulator_set(dev, 1, (width - 4) >> 2, 1, (height - 4) >> 2);
	em28xx_capture_area_set(dev, 0, 0, width >> 2, height >> 2);
	return em28xx_scaler_set(dev, dev->hscale, dev->vscale);
}

int em28xx_set_alternate(struct em28xx *dev)
{
	int errCode, prev_alt = dev->alt;
	int i;
	unsigned int min_pkt_size = dev->bytesperline + 4;

	/* When image size is bigger than a certain value,
	   the frame size should be increased, otherwise, only
	   green screen will be received.
	 */
	if (dev->frame_size > 720*240*2)
		min_pkt_size *= 2;

	for (i = 0; i < dev->num_alt; i++) {
		/* stop when the selected alt setting offers enough bandwidth */
		if (dev->alt_max_pkt_size[i] >= min_pkt_size) {
			dev->alt = i;
			break;
		/* otherwise make sure that we end up with the maximum bandwidth
		   because the min_pkt_size equation might be wrong...
		*/
		} else if (dev->alt_max_pkt_size[i] >
			   dev->alt_max_pkt_size[dev->alt])
			dev->alt = i;
	}

	if (dev->alt != prev_alt) {
		em28xx_coredbg("minimum isoc packet size: %u (alt=%d)\n",
				min_pkt_size, dev->alt);
		dev->max_pkt_size = dev->alt_max_pkt_size[dev->alt];
		em28xx_coredbg("setting alternate %d with wMaxPacketSize=%u\n",
			       dev->alt, dev->max_pkt_size);
		errCode = usb_set_interface(dev->udev, 0, dev->alt);
		if (errCode < 0) {
			em28xx_errdev ("cannot change alternate number to %d (error=%i)\n",
					dev->alt, errCode);
			return errCode;
		}
	}
	return 0;
}

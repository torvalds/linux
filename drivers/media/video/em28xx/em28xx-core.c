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

/* FIXME */
#define em28xx_isocdbg(fmt, arg...) do {\
	if (core_debug) \
		printk(KERN_INFO "%s %s :"fmt, \
			 dev->name, __func__ , ##arg); } while (0)

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

	if (reg_debug) {
		printk(ret < 0 ? " failed!\n" : "%02x values: ", ret);
		for (byte = 0; byte < len; byte++)
			printk(" %02x", (unsigned char)buf[byte]);

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
		return -ENODEV;

	if (len < 1)
		return -EINVAL;

	bufs = kmalloc(len, GFP_KERNEL);

	em28xx_regdbg("req=%02x reg=%02x:", req, reg);

	if (reg_debug) {
		int i;
		for (i = 0; i < len; ++i)
			printk(" %02x", (unsigned char)buf[i]);
		printk("\n");
	}

	if (!bufs)
		return -ENOMEM;
	memcpy(bufs, buf, len);
	ret = usb_control_msg(dev->udev, usb_sndctrlpipe(dev->udev, 0), req,
			      USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			      0x0000, reg, bufs, len, HZ);
	if (dev->wait_after_write)
		msleep(dev->wait_after_write);

	kfree(bufs);
	return ret;
}

int em28xx_write_regs(struct em28xx *dev, u16 reg, char *buf, int len)
{
	int rc;

	rc = em28xx_write_regs_req(dev, USB_REQ_GET_STATUS, reg, buf, len);

	/* Stores GPO/GPIO values at the cache, if changed
	   Only write values should be stored, since input on a GPIO
	   register will return the input bits.
	   Not sure what happens on reading GPO register.
	 */
	if (rc >= 0) {
		if (reg == EM2880_R04_GPO)
			dev->reg_gpo = buf[0];
		else if (reg == EM28XX_R08_GPIO)
			dev->reg_gpio = buf[0];
	}

	return rc;
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

	/* Uses cache for gpo/gpio registers */
	if (reg == EM2880_R04_GPO)
		oldval = dev->reg_gpo;
	else if (reg == EM28XX_R08_GPIO)
		oldval = dev->reg_gpio;
	else
		oldval = em28xx_read_reg(dev, reg);

	if (oldval < 0)
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

	ret = em28xx_write_regs(dev, EM28XX_R40_AC97LSB, val, 2);
	if (ret < 0)
		return ret;

	ret = em28xx_write_regs(dev, EM28XX_R42_AC97ADDR, &addr, 1);
	if (ret < 0)
		return ret;

	/* Wait up to 50 ms for AC97 command to complete */
	for (i = 0; i < 10; i++) {
		ret = em28xx_read_reg(dev, EM28XX_R43_AC97BUSY);
		if (ret < 0)
			return ret;

		if (!(ret & 0x01))
			return 0;
		msleep(5);
	}
	em28xx_warn("AC97 command still being executed: not handled properly!\n");
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

		ret = em28xx_write_regs(dev, EM2800_R08_AUDIOSRC, &input, 1);
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

	ret = em28xx_write_reg_bits(dev, EM28XX_R0E_AUDIOSRC, input, 0xc0);
	if (ret < 0)
		return ret;
	msleep(5);

	/* Sets AC97 mixer registers
	   This is seems to be needed, even for non-ac97 configs
	 */
	ret = em28xx_write_ac97(dev, EM28XX_R14_VIDEO_AC97, video);
	if (ret < 0)
		return ret;

	ret = em28xx_write_ac97(dev, EM28XX_R10_LINE_IN_AC97, line);

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
	ret = em28xx_write_ac97(dev, EM28XX_R02_MASTER_AC97, s);

	if (ret < 0)
		return ret;

	if (dev->has_12mhz_i2s)
		xclk |= 0x20;

	if (!dev->mute)
		xclk |= 0x80;

	ret = em28xx_write_reg_bits(dev, EM28XX_R0F_XCLK, xclk, 0xa7);
	if (ret < 0)
		return ret;
	msleep(10);

	/* Selects the proper audio input */
	ret = em28xx_set_audio_source(dev);

	/* Unmute device */
	if (!dev->mute)
		s[1] &= ~0x80;
	ret = em28xx_write_ac97(dev, EM28XX_R02_MASTER_AC97, s);

	return ret;
}
EXPORT_SYMBOL_GPL(em28xx_audio_analog_set);

int em28xx_colorlevels_set_default(struct em28xx *dev)
{
	em28xx_write_regs(dev, EM28XX_R20_YGAIN, "\x10", 1);	/* contrast */
	em28xx_write_regs(dev, EM28XX_R21_YOFFSET, "\x00", 1);	/* brightness */
	em28xx_write_regs(dev, EM28XX_R22_UVGAIN, "\x10", 1);	/* saturation */
	em28xx_write_regs(dev, EM28XX_R23_UOFFSET, "\x00", 1);
	em28xx_write_regs(dev, EM28XX_R24_VOFFSET, "\x00", 1);
	em28xx_write_regs(dev, EM28XX_R25_SHARPNESS, "\x00", 1);

	em28xx_write_regs(dev, EM28XX_R14_GAMMA, "\x20", 1);
	em28xx_write_regs(dev, EM28XX_R15_RGAIN, "\x20", 1);
	em28xx_write_regs(dev, EM28XX_R16_GGAIN, "\x20", 1);
	em28xx_write_regs(dev, EM28XX_R17_BGAIN, "\x20", 1);
	em28xx_write_regs(dev, EM28XX_R18_ROFFSET, "\x00", 1);
	em28xx_write_regs(dev, EM28XX_R19_GOFFSET, "\x00", 1);
	return em28xx_write_regs(dev, EM28XX_R1A_BOFFSET, "\x00", 1);
}

int em28xx_capture_start(struct em28xx *dev, int start)
{
	int rc;
	/* FIXME: which is the best order? */
	/* video registers are sampled by VREF */
	rc = em28xx_write_reg_bits(dev, EM28XX_R0C_USBSUSP,
				   start ? 0x10 : 0x00, 0x10);
	if (rc < 0)
		return rc;

	if (!start) {
		/* disable video capture */
		rc = em28xx_write_regs(dev, EM28XX_R12_VINENABLE, "\x27", 1);
		return rc;
	}

	/* enable video capture */
	rc = em28xx_write_regs_req(dev, 0x00, 0x48, "\x00", 1);

	if (dev->mode == EM28XX_ANALOG_MODE)
		rc = em28xx_write_regs(dev, EM28XX_R12_VINENABLE, "\x67", 1);
	else
		rc = em28xx_write_regs(dev, EM28XX_R12_VINENABLE, "\x37", 1);

	msleep(6);

	return rc;
}

int em28xx_outfmt_set_yuv422(struct em28xx *dev)
{
	em28xx_write_regs(dev, EM28XX_R27_OUTFMT, "\x34", 1);
	em28xx_write_regs(dev, EM28XX_R10_VINMODE, "\x10", 1);
	return em28xx_write_regs(dev, EM28XX_R11_VINCTRL, "\x11", 1);
}

static int em28xx_accumulator_set(struct em28xx *dev, u8 xmin, u8 xmax,
				  u8 ymin, u8 ymax)
{
	em28xx_coredbg("em28xx Scale: (%d,%d)-(%d,%d)\n",
			xmin, ymin, xmax, ymax);

	em28xx_write_regs(dev, EM28XX_R28_XMIN, &xmin, 1);
	em28xx_write_regs(dev, EM28XX_R29_XMAX, &xmax, 1);
	em28xx_write_regs(dev, EM28XX_R2A_YMIN, &ymin, 1);
	return em28xx_write_regs(dev, EM28XX_R2B_YMAX, &ymax, 1);
}

static int em28xx_capture_area_set(struct em28xx *dev, u8 hstart, u8 vstart,
				   u16 width, u16 height)
{
	u8 cwidth = width;
	u8 cheight = height;
	u8 overflow = (height >> 7 & 0x02) | (width >> 8 & 0x01);

	em28xx_coredbg("em28xx Area Set: (%d,%d)\n",
			(width | (overflow & 2) << 7),
			(height | (overflow & 1) << 8));

	em28xx_write_regs(dev, EM28XX_R1C_HSTART, &hstart, 1);
	em28xx_write_regs(dev, EM28XX_R1D_VSTART, &vstart, 1);
	em28xx_write_regs(dev, EM28XX_R1E_CWIDTH, &cwidth, 1);
	em28xx_write_regs(dev, EM28XX_R1F_CHEIGHT, &cheight, 1);
	return em28xx_write_regs(dev, EM28XX_R1B_OFLOW, &overflow, 1);
}

static int em28xx_scaler_set(struct em28xx *dev, u16 h, u16 v)
{
	u8 mode;
	/* the em2800 scaler only supports scaling down to 50% */
	if (dev->is_em2800)
		mode = (v ? 0x20 : 0x00) | (h ? 0x10 : 0x00);
	else {
		u8 buf[2];
		buf[0] = h;
		buf[1] = h >> 8;
		em28xx_write_regs(dev, EM28XX_R30_HSCALELOW, (char *)buf, 2);
		buf[0] = v;
		buf[1] = v >> 8;
		em28xx_write_regs(dev, EM28XX_R32_VSCALELOW, (char *)buf, 2);
		/* it seems that both H and V scalers must be active
		   to work correctly */
		mode = (h || v)? 0x30: 0x00;
	}
	return em28xx_write_reg_bits(dev, EM28XX_R26_COMPR, mode, 0x30);
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
	unsigned int min_pkt_size = dev->width * 2 + 4;

	/* When image size is bigger than a certain value,
	   the frame size should be increased, otherwise, only
	   green screen will be received.
	 */
	if (dev->width * 2 * dev->height > 720 * 240 * 2)
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
			em28xx_errdev("cannot change alternate number to %d (error=%i)\n",
					dev->alt, errCode);
			return errCode;
		}
	}
	return 0;
}

int em28xx_gpio_set(struct em28xx *dev, struct em28xx_reg_seq *gpio)
{
	int rc = 0;

	if (!gpio)
		return rc;

	dev->em28xx_write_regs_req(dev, 0x00, 0x48, "\x00", 1);
	if (dev->mode == EM28XX_ANALOG_MODE)
		dev->em28xx_write_regs_req(dev, 0x00, 0x12, "\x67", 1);
	else
		dev->em28xx_write_regs_req(dev, 0x00, 0x12, "\x37", 1);
	msleep(6);

	/* Send GPIO reset sequences specified at board entry */
	while (gpio->sleep >= 0) {
		if (gpio->reg >= 0) {
			rc = em28xx_write_reg_bits(dev,
						   gpio->reg,
						   gpio->val,
						   gpio->mask);
			if (rc < 0)
				return rc;
		}
		if (gpio->sleep > 0)
			msleep(gpio->sleep);

		gpio++;
	}
	return rc;
}

int em28xx_set_mode(struct em28xx *dev, enum em28xx_mode set_mode)
{
	if (dev->mode == set_mode)
		return 0;

	if (set_mode == EM28XX_MODE_UNDEFINED) {
		dev->mode = set_mode;
		return 0;
	}

	dev->mode = set_mode;

	if (dev->mode == EM28XX_DIGITAL_MODE)
		return em28xx_gpio_set(dev, dev->digital_gpio);
	else
		return em28xx_gpio_set(dev, dev->analog_gpio);
}
EXPORT_SYMBOL_GPL(em28xx_set_mode);

/* ------------------------------------------------------------------
	URB control
   ------------------------------------------------------------------*/

/*
 * IRQ callback, called by URB callback
 */
static void em28xx_irq_callback(struct urb *urb)
{
	struct em28xx_dmaqueue  *dma_q = urb->context;
	struct em28xx *dev = container_of(dma_q, struct em28xx, vidq);
	int rc, i;

	/* Copy data from URB */
	spin_lock(&dev->slock);
	rc = dev->isoc_ctl.isoc_copy(dev, urb);
	spin_unlock(&dev->slock);

	/* Reset urb buffers */
	for (i = 0; i < urb->number_of_packets; i++) {
		urb->iso_frame_desc[i].status = 0;
		urb->iso_frame_desc[i].actual_length = 0;
	}
	urb->status = 0;

	urb->status = usb_submit_urb(urb, GFP_ATOMIC);
	if (urb->status) {
		em28xx_isocdbg("urb resubmit failed (error=%i)\n",
			       urb->status);
	}
}

/*
 * Stop and Deallocate URBs
 */
void em28xx_uninit_isoc(struct em28xx *dev)
{
	struct urb *urb;
	int i;

	em28xx_isocdbg("em28xx: called em28xx_uninit_isoc\n");

	dev->isoc_ctl.nfields = -1;
	for (i = 0; i < dev->isoc_ctl.num_bufs; i++) {
		urb = dev->isoc_ctl.urb[i];
		if (urb) {
			usb_kill_urb(urb);
			usb_unlink_urb(urb);
			if (dev->isoc_ctl.transfer_buffer[i]) {
				usb_buffer_free(dev->udev,
					urb->transfer_buffer_length,
					dev->isoc_ctl.transfer_buffer[i],
					urb->transfer_dma);
			}
			usb_free_urb(urb);
			dev->isoc_ctl.urb[i] = NULL;
		}
		dev->isoc_ctl.transfer_buffer[i] = NULL;
	}

	kfree(dev->isoc_ctl.urb);
	kfree(dev->isoc_ctl.transfer_buffer);

	dev->isoc_ctl.urb = NULL;
	dev->isoc_ctl.transfer_buffer = NULL;
	dev->isoc_ctl.num_bufs = 0;

	em28xx_capture_start(dev, 0);
}
EXPORT_SYMBOL_GPL(em28xx_uninit_isoc);

/*
 * Allocate URBs and start IRQ
 */
int em28xx_init_isoc(struct em28xx *dev, int max_packets,
		     int num_bufs, int max_pkt_size,
		     int (*isoc_copy) (struct em28xx *dev, struct urb *urb))
{
	struct em28xx_dmaqueue *dma_q = &dev->vidq;
	int i;
	int sb_size, pipe;
	struct urb *urb;
	int j, k;
	int rc;

	em28xx_isocdbg("em28xx: called em28xx_prepare_isoc\n");

	/* De-allocates all pending stuff */
	em28xx_uninit_isoc(dev);

	dev->isoc_ctl.isoc_copy = isoc_copy;
	dev->isoc_ctl.num_bufs = num_bufs;

	dev->isoc_ctl.urb = kzalloc(sizeof(void *)*num_bufs,  GFP_KERNEL);
	if (!dev->isoc_ctl.urb) {
		em28xx_errdev("cannot alloc memory for usb buffers\n");
		return -ENOMEM;
	}

	dev->isoc_ctl.transfer_buffer = kzalloc(sizeof(void *)*num_bufs,
					      GFP_KERNEL);
	if (!dev->isoc_ctl.urb) {
		em28xx_errdev("cannot allocate memory for usbtransfer\n");
		kfree(dev->isoc_ctl.urb);
		return -ENOMEM;
	}

	dev->isoc_ctl.max_pkt_size = max_pkt_size;
	dev->isoc_ctl.buf = NULL;

	sb_size = max_packets * dev->isoc_ctl.max_pkt_size;

	/* allocate urbs and transfer buffers */
	for (i = 0; i < dev->isoc_ctl.num_bufs; i++) {
		urb = usb_alloc_urb(max_packets, GFP_KERNEL);
		if (!urb) {
			em28xx_err("cannot alloc isoc_ctl.urb %i\n", i);
			em28xx_uninit_isoc(dev);
			return -ENOMEM;
		}
		dev->isoc_ctl.urb[i] = urb;

		dev->isoc_ctl.transfer_buffer[i] = usb_buffer_alloc(dev->udev,
			sb_size, GFP_KERNEL, &urb->transfer_dma);
		if (!dev->isoc_ctl.transfer_buffer[i]) {
			em28xx_err("unable to allocate %i bytes for transfer"
					" buffer %i%s\n",
					sb_size, i,
					in_interrupt()?" while in int":"");
			em28xx_uninit_isoc(dev);
			return -ENOMEM;
		}
		memset(dev->isoc_ctl.transfer_buffer[i], 0, sb_size);

		/* FIXME: this is a hack - should be
			'desc.bEndpointAddress & USB_ENDPOINT_NUMBER_MASK'
			should also be using 'desc.bInterval'
		 */
		pipe = usb_rcvisocpipe(dev->udev,
			dev->mode == EM28XX_ANALOG_MODE ? 0x82 : 0x84);

		usb_fill_int_urb(urb, dev->udev, pipe,
				 dev->isoc_ctl.transfer_buffer[i], sb_size,
				 em28xx_irq_callback, dma_q, 1);

		urb->number_of_packets = max_packets;
		urb->transfer_flags = URB_ISO_ASAP;

		k = 0;
		for (j = 0; j < max_packets; j++) {
			urb->iso_frame_desc[j].offset = k;
			urb->iso_frame_desc[j].length =
						dev->isoc_ctl.max_pkt_size;
			k += dev->isoc_ctl.max_pkt_size;
		}
	}

	init_waitqueue_head(&dma_q->wq);

	em28xx_capture_start(dev, 1);

	/* submit urbs and enables IRQ */
	for (i = 0; i < dev->isoc_ctl.num_bufs; i++) {
		rc = usb_submit_urb(dev->isoc_ctl.urb[i], GFP_ATOMIC);
		if (rc) {
			em28xx_err("submit of urb %i failed (error=%i)\n", i,
				   rc);
			em28xx_uninit_isoc(dev);
			return rc;
		}
	}

	return 0;
}
EXPORT_SYMBOL_GPL(em28xx_init_isoc);

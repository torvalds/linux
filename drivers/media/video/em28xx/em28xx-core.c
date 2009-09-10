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
#include <media/v4l2-common.h>

#include "em28xx.h"

/* #define ENABLE_DEBUG_ISOC_FRAMES */

static unsigned int core_debug;
module_param(core_debug, int, 0644);
MODULE_PARM_DESC(core_debug, "enable debug messages [core]");

#define em28xx_coredbg(fmt, arg...) do {\
	if (core_debug) \
		printk(KERN_INFO "%s %s :"fmt, \
			 dev->name, __func__ , ##arg); } while (0)

static unsigned int reg_debug;
module_param(reg_debug, int, 0644);
MODULE_PARM_DESC(reg_debug, "enable debug messages [URB reg]");

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
	int ret;
	int pipe = usb_rcvctrlpipe(dev->udev, 0);

	if (dev->state & DEV_DISCONNECTED)
		return -ENODEV;

	if (len > URB_MAX_CTRL_SIZE)
		return -EINVAL;

	if (reg_debug) {
		printk(KERN_DEBUG "(pipe 0x%08x): "
			"IN:  %02x %02x %02x %02x %02x %02x %02x %02x ",
			pipe,
			USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			req, 0, 0,
			reg & 0xff, reg >> 8,
			len & 0xff, len >> 8);
	}

	mutex_lock(&dev->ctrl_urb_lock);
	ret = usb_control_msg(dev->udev, pipe, req,
			      USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			      0x0000, reg, dev->urb_buf, len, HZ);
	if (ret < 0) {
		if (reg_debug)
			printk(" failed!\n");
		mutex_unlock(&dev->ctrl_urb_lock);
		return ret;
	}

	if (len)
		memcpy(buf, dev->urb_buf, len);

	mutex_unlock(&dev->ctrl_urb_lock);

	if (reg_debug) {
		int byte;

		printk("<<<");
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
	int ret;
	u8 val;

	ret = em28xx_read_reg_req_len(dev, req, reg, &val, 1);
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
	int pipe = usb_sndctrlpipe(dev->udev, 0);

	if (dev->state & DEV_DISCONNECTED)
		return -ENODEV;

	if ((len < 1) || (len > URB_MAX_CTRL_SIZE))
		return -EINVAL;

	if (reg_debug) {
		int byte;

		printk(KERN_DEBUG "(pipe 0x%08x): "
			"OUT: %02x %02x %02x %02x %02x %02x %02x %02x >>>",
			pipe,
			USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			req, 0, 0,
			reg & 0xff, reg >> 8,
			len & 0xff, len >> 8);

		for (byte = 0; byte < len; byte++)
			printk(" %02x", (unsigned char)buf[byte]);
		printk("\n");
	}

	mutex_lock(&dev->ctrl_urb_lock);
	memcpy(dev->urb_buf, buf, len);
	ret = usb_control_msg(dev->udev, pipe, req,
			      USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			      0x0000, reg, dev->urb_buf, len, HZ);
	mutex_unlock(&dev->ctrl_urb_lock);

	if (dev->wait_after_write)
		msleep(dev->wait_after_write);

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
		if (reg == dev->reg_gpo_num)
			dev->reg_gpo = buf[0];
		else if (reg == dev->reg_gpio_num)
			dev->reg_gpio = buf[0];
	}

	return rc;
}

/* Write a single register */
int em28xx_write_reg(struct em28xx *dev, u16 reg, u8 val)
{
	return em28xx_write_regs(dev, reg, &val, 1);
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
	if (reg == dev->reg_gpo_num)
		oldval = dev->reg_gpo;
	else if (reg == dev->reg_gpio_num)
		oldval = dev->reg_gpio;
	else
		oldval = em28xx_read_reg(dev, reg);

	if (oldval < 0)
		return oldval;

	newval = (((u8) oldval) & ~bitmask) | (val & bitmask);

	return em28xx_write_regs(dev, reg, &newval, 1);
}

/*
 * em28xx_is_ac97_ready()
 * Checks if ac97 is ready
 */
static int em28xx_is_ac97_ready(struct em28xx *dev)
{
	int ret, i;

	/* Wait up to 50 ms for AC97 command to complete */
	for (i = 0; i < 10; i++, msleep(5)) {
		ret = em28xx_read_reg(dev, EM28XX_R43_AC97BUSY);
		if (ret < 0)
			return ret;

		if (!(ret & 0x01))
			return 0;
	}

	em28xx_warn("AC97 command still being executed: not handled properly!\n");
	return -EBUSY;
}

/*
 * em28xx_read_ac97()
 * write a 16 bit value to the specified AC97 address (LSB first!)
 */
int em28xx_read_ac97(struct em28xx *dev, u8 reg)
{
	int ret;
	u8 addr = (reg & 0x7f) | 0x80;
	u16 val;

	ret = em28xx_is_ac97_ready(dev);
	if (ret < 0)
		return ret;

	ret = em28xx_write_regs(dev, EM28XX_R42_AC97ADDR, &addr, 1);
	if (ret < 0)
		return ret;

	ret = dev->em28xx_read_reg_req_len(dev, 0, EM28XX_R40_AC97LSB,
					   (u8 *)&val, sizeof(val));

	if (ret < 0)
		return ret;
	return le16_to_cpu(val);
}

/*
 * em28xx_write_ac97()
 * write a 16 bit value to the specified AC97 address (LSB first!)
 */
int em28xx_write_ac97(struct em28xx *dev, u8 reg, u16 val)
{
	int ret;
	u8 addr = reg & 0x7f;
	__le16 value;

	value = cpu_to_le16(val);

	ret = em28xx_is_ac97_ready(dev);
	if (ret < 0)
		return ret;

	ret = em28xx_write_regs(dev, EM28XX_R40_AC97LSB, (u8 *) &value, 2);
	if (ret < 0)
		return ret;

	ret = em28xx_write_regs(dev, EM28XX_R42_AC97ADDR, &addr, 1);
	if (ret < 0)
		return ret;

	return 0;
}

struct em28xx_vol_table {
	enum em28xx_amux mux;
	u8		 reg;
};

static struct em28xx_vol_table inputs[] = {
	{ EM28XX_AMUX_VIDEO, 	AC97_VIDEO_VOL   },
	{ EM28XX_AMUX_LINE_IN,	AC97_LINEIN_VOL  },
	{ EM28XX_AMUX_PHONE,	AC97_PHONE_VOL   },
	{ EM28XX_AMUX_MIC,	AC97_MIC_VOL     },
	{ EM28XX_AMUX_CD,	AC97_CD_VOL      },
	{ EM28XX_AMUX_AUX,	AC97_AUX_VOL     },
	{ EM28XX_AMUX_PCM_OUT,	AC97_PCM_OUT_VOL },
};

static int set_ac97_input(struct em28xx *dev)
{
	int ret, i;
	enum em28xx_amux amux = dev->ctl_ainput;

	/* EM28XX_AMUX_VIDEO2 is a special case used to indicate that
	   em28xx should point to LINE IN, while AC97 should use VIDEO
	 */
	if (amux == EM28XX_AMUX_VIDEO2)
		amux = EM28XX_AMUX_VIDEO;

	/* Mute all entres but the one that were selected */
	for (i = 0; i < ARRAY_SIZE(inputs); i++) {
		if (amux == inputs[i].mux)
			ret = em28xx_write_ac97(dev, inputs[i].reg, 0x0808);
		else
			ret = em28xx_write_ac97(dev, inputs[i].reg, 0x8000);

		if (ret < 0)
			em28xx_warn("couldn't setup AC97 register %d\n",
				     inputs[i].reg);
	}
	return 0;
}

static int em28xx_set_audio_source(struct em28xx *dev)
{
	int ret;
	u8 input;

	if (dev->board.is_em2800) {
		if (dev->ctl_ainput == EM28XX_AMUX_VIDEO)
			input = EM2800_AUDIO_SRC_TUNER;
		else
			input = EM2800_AUDIO_SRC_LINE;

		ret = em28xx_write_regs(dev, EM2800_R08_AUDIOSRC, &input, 1);
		if (ret < 0)
			return ret;
	}

	if (dev->board.has_msp34xx)
		input = EM28XX_AUDIO_SRC_TUNER;
	else {
		switch (dev->ctl_ainput) {
		case EM28XX_AMUX_VIDEO:
			input = EM28XX_AUDIO_SRC_TUNER;
			break;
		default:
			input = EM28XX_AUDIO_SRC_LINE;
			break;
		}
	}

	if (dev->board.mute_gpio && dev->mute)
		em28xx_gpio_set(dev, dev->board.mute_gpio);
	else
		em28xx_gpio_set(dev, INPUT(dev->ctl_input)->gpio);

	ret = em28xx_write_reg_bits(dev, EM28XX_R0E_AUDIOSRC, input, 0xc0);
	if (ret < 0)
		return ret;
	msleep(5);

	switch (dev->audio_mode.ac97) {
	case EM28XX_NO_AC97:
		break;
	default:
		ret = set_ac97_input(dev);
	}

	return ret;
}

static const struct em28xx_vol_table outputs[] = {
	{ EM28XX_AOUT_MASTER, AC97_MASTER_VOL      },
	{ EM28XX_AOUT_LINE,   AC97_LINE_LEVEL_VOL  },
	{ EM28XX_AOUT_MONO,   AC97_MASTER_MONO_VOL },
	{ EM28XX_AOUT_LFE,    AC97_LFE_MASTER_VOL  },
	{ EM28XX_AOUT_SURR,   AC97_SURR_MASTER_VOL },
};

int em28xx_audio_analog_set(struct em28xx *dev)
{
	int ret, i;
	u8 xclk;

	if (!dev->audio_mode.has_audio)
		return 0;

	/* It is assumed that all devices use master volume for output.
	   It would be possible to use also line output.
	 */
	if (dev->audio_mode.ac97 != EM28XX_NO_AC97) {
		/* Mute all outputs */
		for (i = 0; i < ARRAY_SIZE(outputs); i++) {
			ret = em28xx_write_ac97(dev, outputs[i].reg, 0x8000);
			if (ret < 0)
				em28xx_warn("couldn't setup AC97 register %d\n",
				     outputs[i].reg);
		}
	}

	xclk = dev->board.xclk & 0x7f;
	if (!dev->mute)
		xclk |= EM28XX_XCLK_AUDIO_UNMUTE;

	ret = em28xx_write_reg(dev, EM28XX_R0F_XCLK, xclk);
	if (ret < 0)
		return ret;
	msleep(10);

	/* Selects the proper audio input */
	ret = em28xx_set_audio_source(dev);

	/* Sets volume */
	if (dev->audio_mode.ac97 != EM28XX_NO_AC97) {
		int vol;

		em28xx_write_ac97(dev, AC97_POWER_DOWN_CTRL, 0x4200);
		em28xx_write_ac97(dev, AC97_EXT_AUD_CTRL, 0x0031);
		em28xx_write_ac97(dev, AC97_PCM_IN_SRATE, 0xbb80);

		/* LSB: left channel - both channels with the same level */
		vol = (0x1f - dev->volume) | ((0x1f - dev->volume) << 8);

		/* Mute device, if needed */
		if (dev->mute)
			vol |= 0x8000;

		/* Sets volume */
		for (i = 0; i < ARRAY_SIZE(outputs); i++) {
			if (dev->ctl_aoutput & outputs[i].mux)
				ret = em28xx_write_ac97(dev, outputs[i].reg,
							vol);
			if (ret < 0)
				em28xx_warn("couldn't setup AC97 register %d\n",
				     outputs[i].reg);
		}

		if (dev->ctl_aoutput & EM28XX_AOUT_PCM_IN) {
			int sel = ac97_return_record_select(dev->ctl_aoutput);

			/* Use the same input for both left and right
			   channels */
			sel |= (sel << 8);

			em28xx_write_ac97(dev, AC97_RECORD_SELECT, sel);
		}
	}

	return ret;
}
EXPORT_SYMBOL_GPL(em28xx_audio_analog_set);

int em28xx_audio_setup(struct em28xx *dev)
{
	int vid1, vid2, feat, cfg;
	u32 vid;

	if (dev->chip_id == CHIP_ID_EM2870 || dev->chip_id == CHIP_ID_EM2874) {
		/* Digital only device - don't load any alsa module */
		dev->audio_mode.has_audio = 0;
		dev->has_audio_class = 0;
		dev->has_alsa_audio = 0;
		return 0;
	}

	/* If device doesn't support Usb Audio Class, use vendor class */
	if (!dev->has_audio_class)
		dev->has_alsa_audio = 1;

	dev->audio_mode.has_audio = 1;

	/* See how this device is configured */
	cfg = em28xx_read_reg(dev, EM28XX_R00_CHIPCFG);
	em28xx_info("Config register raw data: 0x%02x\n", cfg);
	if (cfg < 0) {
		/* Register read error?  */
		cfg = EM28XX_CHIPCFG_AC97; /* Be conservative */
	} else if ((cfg & EM28XX_CHIPCFG_AUDIOMASK) == 0x00) {
		/* The device doesn't have vendor audio at all */
		dev->has_alsa_audio = 0;
		dev->audio_mode.has_audio = 0;
		return 0;
	} else if ((cfg & EM28XX_CHIPCFG_AUDIOMASK) ==
		   EM28XX_CHIPCFG_I2S_3_SAMPRATES) {
		em28xx_info("I2S Audio (3 sample rates)\n");
		dev->audio_mode.i2s_3rates = 1;
	} else if ((cfg & EM28XX_CHIPCFG_AUDIOMASK) ==
		   EM28XX_CHIPCFG_I2S_5_SAMPRATES) {
		em28xx_info("I2S Audio (5 sample rates)\n");
		dev->audio_mode.i2s_5rates = 1;
	}

	if ((cfg & EM28XX_CHIPCFG_AUDIOMASK) != EM28XX_CHIPCFG_AC97) {
		/* Skip the code that does AC97 vendor detection */
		dev->audio_mode.ac97 = EM28XX_NO_AC97;
		goto init_audio;
	}

	dev->audio_mode.ac97 = EM28XX_AC97_OTHER;

	vid1 = em28xx_read_ac97(dev, AC97_VENDOR_ID1);
	if (vid1 < 0) {
		/* Device likely doesn't support AC97 */
		em28xx_warn("AC97 chip type couldn't be determined\n");
		goto init_audio;
	}

	vid2 = em28xx_read_ac97(dev, AC97_VENDOR_ID2);
	if (vid2 < 0)
		goto init_audio;

	vid = vid1 << 16 | vid2;

	dev->audio_mode.ac97_vendor_id = vid;
	em28xx_warn("AC97 vendor ID = 0x%08x\n", vid);

	feat = em28xx_read_ac97(dev, AC97_RESET);
	if (feat < 0)
		goto init_audio;

	dev->audio_mode.ac97_feat = feat;
	em28xx_warn("AC97 features = 0x%04x\n", feat);

	/* Try to identify what audio processor we have */
	if ((vid == 0xffffffff) && (feat == 0x6a90))
		dev->audio_mode.ac97 = EM28XX_AC97_EM202;
	else if ((vid >> 8) == 0x838476)
		dev->audio_mode.ac97 = EM28XX_AC97_SIGMATEL;

init_audio:
	/* Reports detected AC97 processor */
	switch (dev->audio_mode.ac97) {
	case EM28XX_NO_AC97:
		em28xx_info("No AC97 audio processor\n");
		break;
	case EM28XX_AC97_EM202:
		em28xx_info("Empia 202 AC97 audio processor detected\n");
		break;
	case EM28XX_AC97_SIGMATEL:
		em28xx_info("Sigmatel audio processor detected(stac 97%02x)\n",
			    dev->audio_mode.ac97_vendor_id & 0xff);
		break;
	case EM28XX_AC97_OTHER:
		em28xx_warn("Unknown AC97 audio processor detected!\n");
		break;
	default:
		break;
	}

	return em28xx_audio_analog_set(dev);
}
EXPORT_SYMBOL_GPL(em28xx_audio_setup);

int em28xx_colorlevels_set_default(struct em28xx *dev)
{
	em28xx_write_reg(dev, EM28XX_R20_YGAIN, 0x10);	/* contrast */
	em28xx_write_reg(dev, EM28XX_R21_YOFFSET, 0x00);	/* brightness */
	em28xx_write_reg(dev, EM28XX_R22_UVGAIN, 0x10);	/* saturation */
	em28xx_write_reg(dev, EM28XX_R23_UOFFSET, 0x00);
	em28xx_write_reg(dev, EM28XX_R24_VOFFSET, 0x00);
	em28xx_write_reg(dev, EM28XX_R25_SHARPNESS, 0x00);

	em28xx_write_reg(dev, EM28XX_R14_GAMMA, 0x20);
	em28xx_write_reg(dev, EM28XX_R15_RGAIN, 0x20);
	em28xx_write_reg(dev, EM28XX_R16_GGAIN, 0x20);
	em28xx_write_reg(dev, EM28XX_R17_BGAIN, 0x20);
	em28xx_write_reg(dev, EM28XX_R18_ROFFSET, 0x00);
	em28xx_write_reg(dev, EM28XX_R19_GOFFSET, 0x00);
	return em28xx_write_reg(dev, EM28XX_R1A_BOFFSET, 0x00);
}

int em28xx_capture_start(struct em28xx *dev, int start)
{
	int rc;

	if (dev->chip_id == CHIP_ID_EM2874) {
		/* The Transport Stream Enable Register moved in em2874 */
		if (!start) {
			rc = em28xx_write_reg_bits(dev, EM2874_R5F_TS_ENABLE,
						   0x00,
						   EM2874_TS1_CAPTURE_ENABLE);
			return rc;
		}

		/* Enable Transport Stream */
		rc = em28xx_write_reg_bits(dev, EM2874_R5F_TS_ENABLE,
					   EM2874_TS1_CAPTURE_ENABLE,
					   EM2874_TS1_CAPTURE_ENABLE);
		return rc;
	}


	/* FIXME: which is the best order? */
	/* video registers are sampled by VREF */
	rc = em28xx_write_reg_bits(dev, EM28XX_R0C_USBSUSP,
				   start ? 0x10 : 0x00, 0x10);
	if (rc < 0)
		return rc;

	if (!start) {
		/* disable video capture */
		rc = em28xx_write_reg(dev, EM28XX_R12_VINENABLE, 0x27);
		return rc;
	}

	if (dev->board.is_webcam)
		rc = em28xx_write_reg(dev, 0x13, 0x0c);

	/* enable video capture */
	rc = em28xx_write_reg(dev, 0x48, 0x00);

	if (dev->mode == EM28XX_ANALOG_MODE)
		rc = em28xx_write_reg(dev, EM28XX_R12_VINENABLE, 0x67);
	else
		rc = em28xx_write_reg(dev, EM28XX_R12_VINENABLE, 0x37);

	msleep(6);

	return rc;
}

int em28xx_set_outfmt(struct em28xx *dev)
{
	int ret;

	ret = em28xx_write_reg_bits(dev, EM28XX_R27_OUTFMT,
				dev->format->reg | 0x20, 0xff);
	if (ret < 0)
			return ret;

	ret = em28xx_write_reg(dev, EM28XX_R10_VINMODE, dev->vinmode);
	if (ret < 0)
		return ret;

	return em28xx_write_reg(dev, EM28XX_R11_VINCTRL, dev->vinctl);
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

	if (dev->board.is_em2800) {
		mode = (v ? 0x20 : 0x00) | (h ? 0x10 : 0x00);
	} else {
		u8 buf[2];

		buf[0] = h;
		buf[1] = h >> 8;
		em28xx_write_regs(dev, EM28XX_R30_HSCALELOW, (char *)buf, 2);

		buf[0] = v;
		buf[1] = v >> 8;
		em28xx_write_regs(dev, EM28XX_R32_VSCALELOW, (char *)buf, 2);
		/* it seems that both H and V scalers must be active
		   to work correctly */
		mode = (h || v) ? 0x30 : 0x00;
	}
	return em28xx_write_reg_bits(dev, EM28XX_R26_COMPR, mode, 0x30);
}

/* FIXME: this only function read values from dev */
int em28xx_resolution_set(struct em28xx *dev)
{
	int width, height;
	width = norm_maxw(dev);
	height = norm_maxh(dev);

	if (!dev->progressive)
		height >>= norm_maxh(dev);

	em28xx_set_outfmt(dev);


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

	if (dev->mode != EM28XX_SUSPEND) {
		em28xx_write_reg(dev, 0x48, 0x00);
		if (dev->mode == EM28XX_ANALOG_MODE)
			em28xx_write_reg(dev, EM28XX_R12_VINENABLE, 0x67);
		else
			em28xx_write_reg(dev, EM28XX_R12_VINENABLE, 0x37);
		msleep(6);
	}

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

	if (set_mode == EM28XX_SUSPEND) {
		dev->mode = set_mode;

		/* FIXME: add suspend support for ac97 */

		return em28xx_gpio_set(dev, dev->board.suspend_gpio);
	}

	dev->mode = set_mode;

	if (dev->mode == EM28XX_DIGITAL_MODE)
		return em28xx_gpio_set(dev, dev->board.dvb_gpio);
	else
		return em28xx_gpio_set(dev, INPUT(dev->ctl_input)->gpio);
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

	switch (urb->status) {
	case 0:             /* success */
	case -ETIMEDOUT:    /* NAK */
		break;
	case -ECONNRESET:   /* kill */
	case -ENOENT:
	case -ESHUTDOWN:
		return;
	default:            /* error */
		em28xx_isocdbg("urb completition error %d.\n", urb->status);
		break;
	}

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
			if (!irqs_disabled())
				usb_kill_urb(urb);
			else
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
	if (!dev->isoc_ctl.transfer_buffer) {
		em28xx_errdev("cannot allocate memory for usb transfer\n");
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
					in_interrupt() ? " while in int" : "");
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
		urb->transfer_flags = URB_ISO_ASAP | URB_NO_TRANSFER_DMA_MAP;

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

/* Determine the packet size for the DVB stream for the given device
   (underlying value programmed into the eeprom) */
int em28xx_isoc_dvb_max_packetsize(struct em28xx *dev)
{
	unsigned int chip_cfg2;
	unsigned int packet_size = 564;

	if (dev->chip_id == CHIP_ID_EM2874) {
		/* FIXME - for now assume 564 like it was before, but the
		   em2874 code should be added to return the proper value... */
		packet_size = 564;
	} else {
		/* TS max packet size stored in bits 1-0 of R01 */
		chip_cfg2 = em28xx_read_reg(dev, EM28XX_R01_CHIPCFG2);
		switch (chip_cfg2 & EM28XX_CHIPCFG2_TS_PACKETSIZE_MASK) {
		case EM28XX_CHIPCFG2_TS_PACKETSIZE_188:
			packet_size = 188;
			break;
		case EM28XX_CHIPCFG2_TS_PACKETSIZE_376:
			packet_size = 376;
			break;
		case EM28XX_CHIPCFG2_TS_PACKETSIZE_564:
			packet_size = 564;
			break;
		case EM28XX_CHIPCFG2_TS_PACKETSIZE_752:
			packet_size = 752;
			break;
		}
	}

	em28xx_coredbg("dvb max packet size=%d\n", packet_size);
	return packet_size;
}
EXPORT_SYMBOL_GPL(em28xx_isoc_dvb_max_packetsize);

/*
 * em28xx_wake_i2c()
 * configure i2c attached devices
 */
void em28xx_wake_i2c(struct em28xx *dev)
{
	v4l2_device_call_all(&dev->v4l2_dev, 0, core,  reset, 0);
	v4l2_device_call_all(&dev->v4l2_dev, 0, video, s_routing,
			INPUT(dev->ctl_input)->vmux, 0, 0);
	v4l2_device_call_all(&dev->v4l2_dev, 0, video, s_stream, 0);
}

/*
 * Device control list
 */

static LIST_HEAD(em28xx_devlist);
static DEFINE_MUTEX(em28xx_devlist_mutex);

struct em28xx *em28xx_get_device(int minor,
				 enum v4l2_buf_type *fh_type,
				 int *has_radio)
{
	struct em28xx *h, *dev = NULL;

	*fh_type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	*has_radio = 0;

	mutex_lock(&em28xx_devlist_mutex);
	list_for_each_entry(h, &em28xx_devlist, devlist) {
		if (h->vdev->minor == minor)
			dev = h;
		if (h->vbi_dev->minor == minor) {
			dev = h;
			*fh_type = V4L2_BUF_TYPE_VBI_CAPTURE;
		}
		if (h->radio_dev &&
		    h->radio_dev->minor == minor) {
			dev = h;
			*has_radio = 1;
		}
	}
	mutex_unlock(&em28xx_devlist_mutex);

	return dev;
}

/*
 * em28xx_realease_resources()
 * unregisters the v4l2,i2c and usb devices
 * called when the device gets disconected or at module unload
*/
void em28xx_remove_from_devlist(struct em28xx *dev)
{
	mutex_lock(&em28xx_devlist_mutex);
	list_del(&dev->devlist);
	mutex_unlock(&em28xx_devlist_mutex);
};

void em28xx_add_into_devlist(struct em28xx *dev)
{
	mutex_lock(&em28xx_devlist_mutex);
	list_add_tail(&dev->devlist, &em28xx_devlist);
	mutex_unlock(&em28xx_devlist_mutex);
};

/*
 * Extension interface
 */

static LIST_HEAD(em28xx_extension_devlist);
static DEFINE_MUTEX(em28xx_extension_devlist_lock);

int em28xx_register_extension(struct em28xx_ops *ops)
{
	struct em28xx *dev = NULL;

	mutex_lock(&em28xx_devlist_mutex);
	mutex_lock(&em28xx_extension_devlist_lock);
	list_add_tail(&ops->next, &em28xx_extension_devlist);
	list_for_each_entry(dev, &em28xx_devlist, devlist) {
		if (dev)
			ops->init(dev);
	}
	printk(KERN_INFO "Em28xx: Initialized (%s) extension\n", ops->name);
	mutex_unlock(&em28xx_extension_devlist_lock);
	mutex_unlock(&em28xx_devlist_mutex);
	return 0;
}
EXPORT_SYMBOL(em28xx_register_extension);

void em28xx_unregister_extension(struct em28xx_ops *ops)
{
	struct em28xx *dev = NULL;

	mutex_lock(&em28xx_devlist_mutex);
	list_for_each_entry(dev, &em28xx_devlist, devlist) {
		if (dev)
			ops->fini(dev);
	}

	mutex_lock(&em28xx_extension_devlist_lock);
	printk(KERN_INFO "Em28xx: Removed (%s) extension\n", ops->name);
	list_del(&ops->next);
	mutex_unlock(&em28xx_extension_devlist_lock);
	mutex_unlock(&em28xx_devlist_mutex);
}
EXPORT_SYMBOL(em28xx_unregister_extension);

void em28xx_init_extension(struct em28xx *dev)
{
	struct em28xx_ops *ops = NULL;

	mutex_lock(&em28xx_extension_devlist_lock);
	if (!list_empty(&em28xx_extension_devlist)) {
		list_for_each_entry(ops, &em28xx_extension_devlist, next) {
			if (ops->init)
				ops->init(dev);
		}
	}
	mutex_unlock(&em28xx_extension_devlist_lock);
}

void em28xx_close_extension(struct em28xx *dev)
{
	struct em28xx_ops *ops = NULL;

	mutex_lock(&em28xx_extension_devlist_lock);
	if (!list_empty(&em28xx_extension_devlist)) {
		list_for_each_entry(ops, &em28xx_extension_devlist, next) {
			if (ops->fini)
				ops->fini(dev);
		}
	}
	mutex_unlock(&em28xx_extension_devlist_lock);
}

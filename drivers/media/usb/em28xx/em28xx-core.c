/*
   em28xx-core.c - driver for Empia EM2800/EM2820/2840 USB video capture devices

   Copyright (C) 2005 Ludovico Cavedon <cavedon@sssup.it>
		      Markus Rechberger <mrechberger@gmail.com>
		      Mauro Carvalho Chehab <mchehab@infradead.org>
		      Sascha Sommer <saschasommer@freenet.de>
   Copyright (C) 2012 Frank Schäfer <fschaefer.oss@googlemail.com>

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
#include <linux/slab.h>
#include <linux/usb.h>
#include <linux/vmalloc.h>
#include <sound/ac97_codec.h>
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

static int alt;
module_param(alt, int, 0644);
MODULE_PARM_DESC(alt, "alternate setting to use for video endpoint");

static unsigned int disable_vbi;
module_param(disable_vbi, int, 0644);
MODULE_PARM_DESC(disable_vbi, "disable vbi support");

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

	if (dev->disconnected)
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
		return usb_translate_errors(ret);
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
EXPORT_SYMBOL_GPL(em28xx_read_reg);

/*
 * em28xx_write_regs_req()
 * sends data to the usb device, specifying bRequest
 */
int em28xx_write_regs_req(struct em28xx *dev, u8 req, u16 reg, char *buf,
				 int len)
{
	int ret;
	int pipe = usb_sndctrlpipe(dev->udev, 0);

	if (dev->disconnected)
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

	if (ret < 0)
		return usb_translate_errors(ret);

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
EXPORT_SYMBOL_GPL(em28xx_write_regs);

/* Write a single register */
int em28xx_write_reg(struct em28xx *dev, u16 reg, u8 val)
{
	return em28xx_write_regs(dev, reg, &val, 1);
}
EXPORT_SYMBOL_GPL(em28xx_write_reg);

/*
 * em28xx_write_reg_bits()
 * sets only some bits (specified by bitmask) of a register, by first reading
 * the actual value
 */
int em28xx_write_reg_bits(struct em28xx *dev, u16 reg, u8 val,
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
EXPORT_SYMBOL_GPL(em28xx_write_reg_bits);

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
EXPORT_SYMBOL_GPL(em28xx_read_ac97);

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
EXPORT_SYMBOL_GPL(em28xx_write_ac97);

struct em28xx_vol_itable {
	enum em28xx_amux mux;
	u8		 reg;
};

static struct em28xx_vol_itable inputs[] = {
	{ EM28XX_AMUX_VIDEO,	AC97_VIDEO	},
	{ EM28XX_AMUX_LINE_IN,	AC97_LINE	},
	{ EM28XX_AMUX_PHONE,	AC97_PHONE	},
	{ EM28XX_AMUX_MIC,	AC97_MIC	},
	{ EM28XX_AMUX_CD,	AC97_CD		},
	{ EM28XX_AMUX_AUX,	AC97_AUX	},
	{ EM28XX_AMUX_PCM_OUT,	AC97_PCM	},
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

struct em28xx_vol_otable {
	enum em28xx_aout mux;
	u8		 reg;
};

static const struct em28xx_vol_otable outputs[] = {
	{ EM28XX_AOUT_MASTER, AC97_MASTER		},
	{ EM28XX_AOUT_LINE,   AC97_HEADPHONE		},
	{ EM28XX_AOUT_MONO,   AC97_MASTER_MONO		},
	{ EM28XX_AOUT_LFE,    AC97_CENTER_LFE_MASTER	},
	{ EM28XX_AOUT_SURR,   AC97_SURROUND_MASTER	},
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

		em28xx_write_ac97(dev, AC97_POWERDOWN, 0x4200);
		em28xx_write_ac97(dev, AC97_EXTENDED_STATUS, 0x0031);
		em28xx_write_ac97(dev, AC97_PCM_LR_ADC_RATE, 0xbb80);

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

			em28xx_write_ac97(dev, AC97_REC_SEL, sel);
		}
	}

	return ret;
}
EXPORT_SYMBOL_GPL(em28xx_audio_analog_set);

int em28xx_audio_setup(struct em28xx *dev)
{
	int vid1, vid2, feat, cfg;
	u32 vid;

	if (dev->chip_id == CHIP_ID_EM2870 || dev->chip_id == CHIP_ID_EM2874
		|| dev->chip_id == CHIP_ID_EM28174) {
		/* Digital only device - don't load any alsa module */
		dev->audio_mode.has_audio = false;
		dev->has_audio_class = false;
		dev->has_alsa_audio = false;
		return 0;
	}

	dev->audio_mode.has_audio = true;

	/* See how this device is configured */
	cfg = em28xx_read_reg(dev, EM28XX_R00_CHIPCFG);
	em28xx_info("Config register raw data: 0x%02x\n", cfg);
	if (cfg < 0) {
		/* Register read error?  */
		cfg = EM28XX_CHIPCFG_AC97; /* Be conservative */
	} else if ((cfg & EM28XX_CHIPCFG_AUDIOMASK) == 0x00) {
		/* The device doesn't have vendor audio at all */
		dev->has_alsa_audio = false;
		dev->audio_mode.has_audio = false;
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
		/*
		 * Device likely doesn't support AC97
		 * Note: (some) em2800 devices without eeprom reports 0x91 on
		 *	 CHIPCFG register, even not having an AC97 chip
		 */
		em28xx_warn("AC97 chip type couldn't be determined\n");
		dev->audio_mode.ac97 = EM28XX_NO_AC97;
		dev->has_alsa_audio = false;
		dev->audio_mode.has_audio = false;
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
	if (((vid == 0xffffffff) || (vid == 0x83847650)) && (feat == 0x6a90))
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

	if (dev->chip_id == CHIP_ID_EM2874 ||
	    dev->chip_id == CHIP_ID_EM2884 ||
	    dev->chip_id == CHIP_ID_EM28174) {
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

int em28xx_vbi_supported(struct em28xx *dev)
{
	/* Modprobe option to manually disable */
	if (disable_vbi == 1)
		return 0;

	if (dev->board.is_webcam)
		return 0;

	/* FIXME: check subdevices for VBI support */

	if (dev->chip_id == CHIP_ID_EM2860 ||
	    dev->chip_id == CHIP_ID_EM2883)
		return 1;

	/* Version of em28xx that does not support VBI */
	return 0;
}

int em28xx_set_outfmt(struct em28xx *dev)
{
	int ret;
	u8 vinctrl;

	ret = em28xx_write_reg_bits(dev, EM28XX_R27_OUTFMT,
				dev->format->reg | 0x20, 0xff);
	if (ret < 0)
			return ret;

	ret = em28xx_write_reg(dev, EM28XX_R10_VINMODE, dev->vinmode);
	if (ret < 0)
		return ret;

	vinctrl = dev->vinctl;
	if (em28xx_vbi_supported(dev) == 1) {
		vinctrl |= EM28XX_VINCTRL_VBI_RAW;
		em28xx_write_reg(dev, EM28XX_R34_VBI_START_H, 0x00);
		em28xx_write_reg(dev, EM28XX_R36_VBI_WIDTH, dev->vbi_width/4);
		em28xx_write_reg(dev, EM28XX_R37_VBI_HEIGHT, dev->vbi_height);
		if (dev->norm & V4L2_STD_525_60) {
			/* NTSC */
			em28xx_write_reg(dev, EM28XX_R35_VBI_START_V, 0x09);
		} else if (dev->norm & V4L2_STD_625_50) {
			/* PAL */
			em28xx_write_reg(dev, EM28XX_R35_VBI_START_V, 0x07);
		}
	}

	return em28xx_write_reg(dev, EM28XX_R11_VINCTRL, vinctrl);
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

static void em28xx_capture_area_set(struct em28xx *dev, u8 hstart, u8 vstart,
				   u16 width, u16 height)
{
	u8 cwidth = width >> 2;
	u8 cheight = height >> 2;
	u8 overflow = (height >> 9 & 0x02) | (width >> 10 & 0x01);
	/* NOTE: size limit: 2047x1023 = 2MPix */

	em28xx_coredbg("capture area set to (%d,%d): %dx%d\n",
		       hstart, vstart,
		       ((overflow & 2) << 9 | cwidth << 2),
		       ((overflow & 1) << 10 | cheight << 2));

	em28xx_write_regs(dev, EM28XX_R1C_HSTART, &hstart, 1);
	em28xx_write_regs(dev, EM28XX_R1D_VSTART, &vstart, 1);
	em28xx_write_regs(dev, EM28XX_R1E_CWIDTH, &cwidth, 1);
	em28xx_write_regs(dev, EM28XX_R1F_CHEIGHT, &cheight, 1);
	em28xx_write_regs(dev, EM28XX_R1B_OFLOW, &overflow, 1);
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

	/* Properly setup VBI */
	dev->vbi_width = 720;
	if (dev->norm & V4L2_STD_525_60)
		dev->vbi_height = 12;
	else
		dev->vbi_height = 18;

	em28xx_set_outfmt(dev);

	em28xx_accumulator_set(dev, 1, (width - 4) >> 2, 1, (height - 4) >> 2);

	/* If we don't set the start position to 2 in VBI mode, we end up
	   with line 20/21 being YUYV encoded instead of being in 8-bit
	   greyscale.  The core of the issue is that line 21 (and line 23 for
	   PAL WSS) are inside of active video region, and as a result they
	   get the pixelformatting associated with that area.  So by cropping
	   it out, we end up with the same format as the rest of the VBI
	   region */
	if (em28xx_vbi_supported(dev) == 1)
		em28xx_capture_area_set(dev, 0, 2, width, height);
	else
		em28xx_capture_area_set(dev, 0, 0, width, height);

	return em28xx_scaler_set(dev, dev->hscale, dev->vscale);
}

/* Set USB alternate setting for analog video */
int em28xx_set_alternate(struct em28xx *dev)
{
	int errCode;
	int i;
	unsigned int min_pkt_size = dev->width * 2 + 4;

	/* NOTE: for isoc transfers, only alt settings > 0 are allowed
		 bulk transfers seem to work only with alt=0 ! */
	dev->alt = 0;
	if ((alt > 0) && (alt < dev->num_alt)) {
		em28xx_coredbg("alternate forced to %d\n", dev->alt);
		dev->alt = alt;
		goto set_alt;
	}
	if (dev->analog_xfer_bulk)
		goto set_alt;

	/* When image size is bigger than a certain value,
	   the frame size should be increased, otherwise, only
	   green screen will be received.
	 */
	if (dev->width * 2 * dev->height > 720 * 240 * 2)
		min_pkt_size *= 2;

	for (i = 0; i < dev->num_alt; i++) {
		/* stop when the selected alt setting offers enough bandwidth */
		if (dev->alt_max_pkt_size_isoc[i] >= min_pkt_size) {
			dev->alt = i;
			break;
		/* otherwise make sure that we end up with the maximum bandwidth
		   because the min_pkt_size equation might be wrong...
		*/
		} else if (dev->alt_max_pkt_size_isoc[i] >
			   dev->alt_max_pkt_size_isoc[dev->alt])
			dev->alt = i;
	}

set_alt:
	/* NOTE: for bulk transfers, we need to call usb_set_interface()
	 * even if the previous settings were the same. Otherwise streaming
	 * fails with all urbs having status = -EOVERFLOW ! */
	if (dev->analog_xfer_bulk) {
		dev->max_pkt_size = 512; /* USB 2.0 spec */
		dev->packet_multiplier = EM28XX_BULK_PACKET_MULTIPLIER;
	} else { /* isoc */
		em28xx_coredbg("minimum isoc packet size: %u (alt=%d)\n",
			       min_pkt_size, dev->alt);
		dev->max_pkt_size =
				  dev->alt_max_pkt_size_isoc[dev->alt];
		dev->packet_multiplier = EM28XX_NUM_ISOC_PACKETS;
	}
	em28xx_coredbg("setting alternate %d with wMaxPacketSize=%u\n",
		       dev->alt, dev->max_pkt_size);
	errCode = usb_set_interface(dev->udev, 0, dev->alt);
	if (errCode < 0) {
		em28xx_errdev("cannot change alternate number to %d (error=%i)\n",
			      dev->alt, errCode);
		return errCode;
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
EXPORT_SYMBOL_GPL(em28xx_gpio_set);

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
 * URB completion handler for isoc/bulk transfers
 */
static void em28xx_irq_callback(struct urb *urb)
{
	struct em28xx *dev = urb->context;
	int i;

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
	dev->usb_ctl.urb_data_copy(dev, urb);
	spin_unlock(&dev->slock);

	/* Reset urb buffers */
	for (i = 0; i < urb->number_of_packets; i++) {
		/* isoc only (bulk: number_of_packets = 0) */
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
void em28xx_uninit_usb_xfer(struct em28xx *dev, enum em28xx_mode mode)
{
	struct urb *urb;
	struct em28xx_usb_bufs *usb_bufs;
	int i;

	em28xx_isocdbg("em28xx: called em28xx_uninit_usb_xfer in mode %d\n",
		       mode);

	if (mode == EM28XX_DIGITAL_MODE)
		usb_bufs = &dev->usb_ctl.digital_bufs;
	else
		usb_bufs = &dev->usb_ctl.analog_bufs;

	for (i = 0; i < usb_bufs->num_bufs; i++) {
		urb = usb_bufs->urb[i];
		if (urb) {
			if (!irqs_disabled())
				usb_kill_urb(urb);
			else
				usb_unlink_urb(urb);

			if (usb_bufs->transfer_buffer[i]) {
				usb_free_coherent(dev->udev,
					urb->transfer_buffer_length,
					usb_bufs->transfer_buffer[i],
					urb->transfer_dma);
			}
			usb_free_urb(urb);
			usb_bufs->urb[i] = NULL;
		}
		usb_bufs->transfer_buffer[i] = NULL;
	}

	kfree(usb_bufs->urb);
	kfree(usb_bufs->transfer_buffer);

	usb_bufs->urb = NULL;
	usb_bufs->transfer_buffer = NULL;
	usb_bufs->num_bufs = 0;

	em28xx_capture_start(dev, 0);
}
EXPORT_SYMBOL_GPL(em28xx_uninit_usb_xfer);

/*
 * Stop URBs
 */
void em28xx_stop_urbs(struct em28xx *dev)
{
	int i;
	struct urb *urb;
	struct em28xx_usb_bufs *isoc_bufs = &dev->usb_ctl.digital_bufs;

	em28xx_isocdbg("em28xx: called em28xx_stop_urbs\n");

	for (i = 0; i < isoc_bufs->num_bufs; i++) {
		urb = isoc_bufs->urb[i];
		if (urb) {
			if (!irqs_disabled())
				usb_kill_urb(urb);
			else
				usb_unlink_urb(urb);
		}
	}

	em28xx_capture_start(dev, 0);
}
EXPORT_SYMBOL_GPL(em28xx_stop_urbs);

/*
 * Allocate URBs
 */
int em28xx_alloc_urbs(struct em28xx *dev, enum em28xx_mode mode, int xfer_bulk,
		      int num_bufs, int max_pkt_size, int packet_multiplier)
{
	struct em28xx_usb_bufs *usb_bufs;
	int i;
	int sb_size, pipe;
	struct urb *urb;
	int j, k;

	em28xx_isocdbg("em28xx: called em28xx_alloc_isoc in mode %d\n", mode);

	/* Check mode and if we have an endpoint for the selected
	   transfer type, select buffer				 */
	if (mode == EM28XX_DIGITAL_MODE) {
		if ((xfer_bulk && !dev->dvb_ep_bulk) ||
		    (!xfer_bulk && !dev->dvb_ep_isoc)) {
			em28xx_errdev("no endpoint for DVB mode and transfer type %d\n",
				      xfer_bulk > 0);
			return -EINVAL;
		}
		usb_bufs = &dev->usb_ctl.digital_bufs;
	} else if (mode == EM28XX_ANALOG_MODE) {
		if ((xfer_bulk && !dev->analog_ep_bulk) ||
		    (!xfer_bulk && !dev->analog_ep_isoc)) {
			em28xx_errdev("no endpoint for analog mode and transfer type %d\n",
				       xfer_bulk > 0);
			return -EINVAL;
		}
		usb_bufs = &dev->usb_ctl.analog_bufs;
	} else {
		em28xx_errdev("invalid mode selected\n");
		return -EINVAL;
	}

	/* De-allocates all pending stuff */
	em28xx_uninit_usb_xfer(dev, mode);

	usb_bufs->num_bufs = num_bufs;

	usb_bufs->urb = kzalloc(sizeof(void *)*num_bufs,  GFP_KERNEL);
	if (!usb_bufs->urb) {
		em28xx_errdev("cannot alloc memory for usb buffers\n");
		return -ENOMEM;
	}

	usb_bufs->transfer_buffer = kzalloc(sizeof(void *)*num_bufs,
					     GFP_KERNEL);
	if (!usb_bufs->transfer_buffer) {
		em28xx_errdev("cannot allocate memory for usb transfer\n");
		kfree(usb_bufs->urb);
		return -ENOMEM;
	}

	usb_bufs->max_pkt_size = max_pkt_size;
	if (xfer_bulk)
		usb_bufs->num_packets = 0;
	else
		usb_bufs->num_packets = packet_multiplier;
	dev->usb_ctl.vid_buf = NULL;
	dev->usb_ctl.vbi_buf = NULL;

	sb_size = packet_multiplier * usb_bufs->max_pkt_size;

	/* allocate urbs and transfer buffers */
	for (i = 0; i < usb_bufs->num_bufs; i++) {
		urb = usb_alloc_urb(usb_bufs->num_packets, GFP_KERNEL);
		if (!urb) {
			em28xx_err("cannot alloc usb_ctl.urb %i\n", i);
			em28xx_uninit_usb_xfer(dev, mode);
			return -ENOMEM;
		}
		usb_bufs->urb[i] = urb;

		usb_bufs->transfer_buffer[i] = usb_alloc_coherent(dev->udev,
			sb_size, GFP_KERNEL, &urb->transfer_dma);
		if (!usb_bufs->transfer_buffer[i]) {
			em28xx_err("unable to allocate %i bytes for transfer"
					" buffer %i%s\n",
					sb_size, i,
					in_interrupt() ? " while in int" : "");
			em28xx_uninit_usb_xfer(dev, mode);
			return -ENOMEM;
		}
		memset(usb_bufs->transfer_buffer[i], 0, sb_size);

		if (xfer_bulk) { /* bulk */
			pipe = usb_rcvbulkpipe(dev->udev,
					       mode == EM28XX_ANALOG_MODE ?
					       dev->analog_ep_bulk :
					       dev->dvb_ep_bulk);
			usb_fill_bulk_urb(urb, dev->udev, pipe,
					  usb_bufs->transfer_buffer[i], sb_size,
					  em28xx_irq_callback, dev);
			urb->transfer_flags = URB_NO_TRANSFER_DMA_MAP;
		} else { /* isoc */
			pipe = usb_rcvisocpipe(dev->udev,
					       mode == EM28XX_ANALOG_MODE ?
					       dev->analog_ep_isoc :
					       dev->dvb_ep_isoc);
			usb_fill_int_urb(urb, dev->udev, pipe,
					 usb_bufs->transfer_buffer[i], sb_size,
					 em28xx_irq_callback, dev, 1);
			urb->transfer_flags = URB_ISO_ASAP |
					      URB_NO_TRANSFER_DMA_MAP;
			k = 0;
			for (j = 0; j < usb_bufs->num_packets; j++) {
				urb->iso_frame_desc[j].offset = k;
				urb->iso_frame_desc[j].length =
							usb_bufs->max_pkt_size;
				k += usb_bufs->max_pkt_size;
			}
		}

		urb->number_of_packets = usb_bufs->num_packets;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(em28xx_alloc_urbs);

/*
 * Allocate URBs and start IRQ
 */
int em28xx_init_usb_xfer(struct em28xx *dev, enum em28xx_mode mode,
		    int xfer_bulk, int num_bufs, int max_pkt_size,
		    int packet_multiplier,
		    int (*urb_data_copy) (struct em28xx *dev, struct urb *urb))
{
	struct em28xx_dmaqueue *dma_q = &dev->vidq;
	struct em28xx_dmaqueue *vbi_dma_q = &dev->vbiq;
	struct em28xx_usb_bufs *usb_bufs;
	int i;
	int rc;
	int alloc;

	em28xx_isocdbg("em28xx: called em28xx_init_usb_xfer in mode %d\n",
		       mode);

	dev->usb_ctl.urb_data_copy = urb_data_copy;

	if (mode == EM28XX_DIGITAL_MODE) {
		usb_bufs = &dev->usb_ctl.digital_bufs;
		/* no need to free/alloc usb buffers in digital mode */
		alloc = 0;
	} else {
		usb_bufs = &dev->usb_ctl.analog_bufs;
		alloc = 1;
	}

	if (alloc) {
		rc = em28xx_alloc_urbs(dev, mode, xfer_bulk, num_bufs,
				       max_pkt_size, packet_multiplier);
		if (rc)
			return rc;
	}

	if (xfer_bulk) {
		rc = usb_clear_halt(dev->udev, usb_bufs->urb[0]->pipe);
		if (rc < 0) {
			em28xx_err("failed to clear USB bulk endpoint stall/halt condition (error=%i)\n",
				   rc);
			em28xx_uninit_usb_xfer(dev, mode);
			return rc;
		}
	}

	init_waitqueue_head(&dma_q->wq);
	init_waitqueue_head(&vbi_dma_q->wq);

	em28xx_capture_start(dev, 1);

	/* submit urbs and enables IRQ */
	for (i = 0; i < usb_bufs->num_bufs; i++) {
		rc = usb_submit_urb(usb_bufs->urb[i], GFP_ATOMIC);
		if (rc) {
			em28xx_err("submit of urb %i failed (error=%i)\n", i,
				   rc);
			em28xx_uninit_usb_xfer(dev, mode);
			return rc;
		}
	}

	return 0;
}
EXPORT_SYMBOL_GPL(em28xx_init_usb_xfer);

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

/*
 * Extension interface
 */

static LIST_HEAD(em28xx_extension_devlist);

int em28xx_register_extension(struct em28xx_ops *ops)
{
	struct em28xx *dev = NULL;

	mutex_lock(&em28xx_devlist_mutex);
	list_add_tail(&ops->next, &em28xx_extension_devlist);
	list_for_each_entry(dev, &em28xx_devlist, devlist) {
		ops->init(dev);
	}
	mutex_unlock(&em28xx_devlist_mutex);
	printk(KERN_INFO "Em28xx: Initialized (%s) extension\n", ops->name);
	return 0;
}
EXPORT_SYMBOL(em28xx_register_extension);

void em28xx_unregister_extension(struct em28xx_ops *ops)
{
	struct em28xx *dev = NULL;

	mutex_lock(&em28xx_devlist_mutex);
	list_for_each_entry(dev, &em28xx_devlist, devlist) {
		ops->fini(dev);
	}
	list_del(&ops->next);
	mutex_unlock(&em28xx_devlist_mutex);
	printk(KERN_INFO "Em28xx: Removed (%s) extension\n", ops->name);
}
EXPORT_SYMBOL(em28xx_unregister_extension);

void em28xx_init_extension(struct em28xx *dev)
{
	const struct em28xx_ops *ops = NULL;

	mutex_lock(&em28xx_devlist_mutex);
	list_add_tail(&dev->devlist, &em28xx_devlist);
	list_for_each_entry(ops, &em28xx_extension_devlist, next) {
		if (ops->init)
			ops->init(dev);
	}
	mutex_unlock(&em28xx_devlist_mutex);
}

void em28xx_close_extension(struct em28xx *dev)
{
	const struct em28xx_ops *ops = NULL;

	mutex_lock(&em28xx_devlist_mutex);
	list_for_each_entry(ops, &em28xx_extension_devlist, next) {
		if (ops->fini)
			ops->fini(dev);
	}
	list_del(&dev->devlist);
	mutex_unlock(&em28xx_devlist_mutex);
}

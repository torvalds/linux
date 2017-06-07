/*
 *  tm6000-core.c - driver for TM5600/TM6000/TM6010 USB video capture devices
 *
 *  Copyright (C) 2006-2007 Mauro Carvalho Chehab <mchehab@infradead.org>
 *
 *  Copyright (C) 2007 Michel Ludwig <michel.ludwig@gmail.com>
 *      - DVB-T support
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation version 2
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/usb.h>
#include <linux/i2c.h>
#include "tm6000.h"
#include "tm6000-regs.h"
#include <media/v4l2-common.h>
#include <media/tuner.h>

#define USB_TIMEOUT	(5 * HZ) /* ms */

int tm6000_read_write_usb(struct tm6000_core *dev, u8 req_type, u8 req,
			  u16 value, u16 index, u8 *buf, u16 len)
{
	int          ret, i;
	unsigned int pipe;
	u8	     *data = NULL;
	int delay = 5000;

	if (len) {
		data = kzalloc(len, GFP_KERNEL);
		if (!data)
			return -ENOMEM;
	}

	mutex_lock(&dev->usb_lock);

	if (req_type & USB_DIR_IN)
		pipe = usb_rcvctrlpipe(dev->udev, 0);
	else {
		pipe = usb_sndctrlpipe(dev->udev, 0);
		memcpy(data, buf, len);
	}

	if (tm6000_debug & V4L2_DEBUG_I2C) {
		printk(KERN_DEBUG "(dev %p, pipe %08x): ", dev->udev, pipe);

		printk(KERN_CONT "%s: %02x %02x %02x %02x %02x %02x %02x %02x ",
			(req_type & USB_DIR_IN) ? " IN" : "OUT",
			req_type, req, value&0xff, value>>8, index&0xff,
			index>>8, len&0xff, len>>8);

		if (!(req_type & USB_DIR_IN)) {
			printk(KERN_CONT ">>> ");
			for (i = 0; i < len; i++)
				printk(KERN_CONT " %02x", buf[i]);
			printk(KERN_CONT "\n");
		}
	}

	ret = usb_control_msg(dev->udev, pipe, req, req_type, value, index,
			      data, len, USB_TIMEOUT);

	if (req_type &  USB_DIR_IN)
		memcpy(buf, data, len);

	if (tm6000_debug & V4L2_DEBUG_I2C) {
		if (ret < 0) {
			if (req_type &  USB_DIR_IN)
				printk(KERN_DEBUG "<<< (len=%d)\n", len);

			printk(KERN_CONT "%s: Error #%d\n", __func__, ret);
		} else if (req_type &  USB_DIR_IN) {
			printk(KERN_CONT "<<< ");
			for (i = 0; i < len; i++)
				printk(KERN_CONT " %02x", buf[i]);
			printk(KERN_CONT "\n");
		}
	}

	kfree(data);

	if (dev->quirks & TM6000_QUIRK_NO_USB_DELAY)
		delay = 0;

	if (req == REQ_16_SET_GET_I2C_WR1_RDN && !(req_type & USB_DIR_IN)) {
		unsigned int tsleep;
		/* Calculate delay time, 14000us for 64 bytes */
		tsleep = (len * 200) + 200;
		if (tsleep < delay)
			tsleep = delay;
		usleep_range(tsleep, tsleep + 1000);
	}
	else if (delay)
		usleep_range(delay, delay + 1000);

	mutex_unlock(&dev->usb_lock);
	return ret;
}

int tm6000_set_reg(struct tm6000_core *dev, u8 req, u16 value, u16 index)
{
	return
		tm6000_read_write_usb(dev, USB_DIR_OUT | USB_TYPE_VENDOR,
				      req, value, index, NULL, 0);
}
EXPORT_SYMBOL_GPL(tm6000_set_reg);

int tm6000_get_reg(struct tm6000_core *dev, u8 req, u16 value, u16 index)
{
	int rc;
	u8 buf[1];

	rc = tm6000_read_write_usb(dev, USB_DIR_IN | USB_TYPE_VENDOR, req,
					value, index, buf, 1);

	if (rc < 0)
		return rc;

	return *buf;
}
EXPORT_SYMBOL_GPL(tm6000_get_reg);

int tm6000_set_reg_mask(struct tm6000_core *dev, u8 req, u16 value,
						u16 index, u16 mask)
{
	int rc;
	u8 buf[1];
	u8 new_index;

	rc = tm6000_read_write_usb(dev, USB_DIR_IN | USB_TYPE_VENDOR, req,
					value, 0, buf, 1);

	if (rc < 0)
		return rc;

	new_index = (buf[0] & ~mask) | (index & mask);

	if (new_index == buf[0])
		return 0;

	return tm6000_read_write_usb(dev, USB_DIR_OUT | USB_TYPE_VENDOR,
				      req, value, new_index, NULL, 0);
}
EXPORT_SYMBOL_GPL(tm6000_set_reg_mask);

int tm6000_get_reg16(struct tm6000_core *dev, u8 req, u16 value, u16 index)
{
	int rc;
	u8 buf[2];

	rc = tm6000_read_write_usb(dev, USB_DIR_IN | USB_TYPE_VENDOR, req,
					value, index, buf, 2);

	if (rc < 0)
		return rc;

	return buf[1]|buf[0]<<8;
}

int tm6000_get_reg32(struct tm6000_core *dev, u8 req, u16 value, u16 index)
{
	int rc;
	u8 buf[4];

	rc = tm6000_read_write_usb(dev, USB_DIR_IN | USB_TYPE_VENDOR, req,
					value, index, buf, 4);

	if (rc < 0)
		return rc;

	return buf[3] | buf[2] << 8 | buf[1] << 16 | buf[0] << 24;
}

int tm6000_i2c_reset(struct tm6000_core *dev, u16 tsleep)
{
	int rc;

	rc = tm6000_set_reg(dev, REQ_03_SET_GET_MCU_PIN, TM6000_GPIO_CLK, 0);
	if (rc < 0)
		return rc;

	msleep(tsleep);

	rc = tm6000_set_reg(dev, REQ_03_SET_GET_MCU_PIN, TM6000_GPIO_CLK, 1);
	msleep(tsleep);

	return rc;
}

void tm6000_set_fourcc_format(struct tm6000_core *dev)
{
	if (dev->dev_type == TM6010) {
		int val;

		val = tm6000_get_reg(dev, TM6010_REQ07_RCC_ACTIVE_IF, 0) & 0xfc;
		if (dev->fourcc == V4L2_PIX_FMT_UYVY)
			tm6000_set_reg(dev, TM6010_REQ07_RCC_ACTIVE_IF, val);
		else
			tm6000_set_reg(dev, TM6010_REQ07_RCC_ACTIVE_IF, val | 1);
	} else {
		if (dev->fourcc == V4L2_PIX_FMT_UYVY)
			tm6000_set_reg(dev, TM6010_REQ07_RC1_TRESHOLD, 0xd0);
		else
			tm6000_set_reg(dev, TM6010_REQ07_RC1_TRESHOLD, 0x90);
	}
}

static void tm6000_set_vbi(struct tm6000_core *dev)
{
	/*
	 * FIXME:
	 * VBI lines and start/end are different between 60Hz and 50Hz
	 * So, it is very likely that we need to change the config to
	 * something that takes it into account, doing something different
	 * if (dev->norm & V4L2_STD_525_60)
	 */

	if (dev->dev_type == TM6010) {
		tm6000_set_reg(dev, TM6010_REQ07_R3F_RESET, 0x01);
		tm6000_set_reg(dev, TM6010_REQ07_R41_TELETEXT_VBI_CODE1, 0x27);
		tm6000_set_reg(dev, TM6010_REQ07_R42_VBI_DATA_HIGH_LEVEL, 0x55);
		tm6000_set_reg(dev, TM6010_REQ07_R43_VBI_DATA_TYPE_LINE7, 0x66);
		tm6000_set_reg(dev, TM6010_REQ07_R44_VBI_DATA_TYPE_LINE8, 0x66);
		tm6000_set_reg(dev, TM6010_REQ07_R45_VBI_DATA_TYPE_LINE9, 0x66);
		tm6000_set_reg(dev,
			TM6010_REQ07_R46_VBI_DATA_TYPE_LINE10, 0x66);
		tm6000_set_reg(dev,
			TM6010_REQ07_R47_VBI_DATA_TYPE_LINE11, 0x66);
		tm6000_set_reg(dev,
			TM6010_REQ07_R48_VBI_DATA_TYPE_LINE12, 0x66);
		tm6000_set_reg(dev,
			TM6010_REQ07_R49_VBI_DATA_TYPE_LINE13, 0x66);
		tm6000_set_reg(dev,
			TM6010_REQ07_R4A_VBI_DATA_TYPE_LINE14, 0x66);
		tm6000_set_reg(dev,
			TM6010_REQ07_R4B_VBI_DATA_TYPE_LINE15, 0x66);
		tm6000_set_reg(dev,
			TM6010_REQ07_R4C_VBI_DATA_TYPE_LINE16, 0x66);
		tm6000_set_reg(dev,
			TM6010_REQ07_R4D_VBI_DATA_TYPE_LINE17, 0x66);
		tm6000_set_reg(dev,
			TM6010_REQ07_R4E_VBI_DATA_TYPE_LINE18, 0x66);
		tm6000_set_reg(dev,
			TM6010_REQ07_R4F_VBI_DATA_TYPE_LINE19, 0x66);
		tm6000_set_reg(dev,
			TM6010_REQ07_R50_VBI_DATA_TYPE_LINE20, 0x66);
		tm6000_set_reg(dev,
			TM6010_REQ07_R51_VBI_DATA_TYPE_LINE21, 0x66);
		tm6000_set_reg(dev,
			TM6010_REQ07_R52_VBI_DATA_TYPE_LINE22, 0x66);
		tm6000_set_reg(dev,
			TM6010_REQ07_R53_VBI_DATA_TYPE_LINE23, 0x00);
		tm6000_set_reg(dev,
			TM6010_REQ07_R54_VBI_DATA_TYPE_RLINES, 0x00);
		tm6000_set_reg(dev,
			TM6010_REQ07_R55_VBI_LOOP_FILTER_GAIN, 0x01);
		tm6000_set_reg(dev,
			TM6010_REQ07_R56_VBI_LOOP_FILTER_I_GAIN, 0x00);
		tm6000_set_reg(dev,
			TM6010_REQ07_R57_VBI_LOOP_FILTER_P_GAIN, 0x02);
		tm6000_set_reg(dev, TM6010_REQ07_R58_VBI_CAPTION_DTO1, 0x35);
		tm6000_set_reg(dev, TM6010_REQ07_R59_VBI_CAPTION_DTO0, 0xa0);
		tm6000_set_reg(dev, TM6010_REQ07_R5A_VBI_TELETEXT_DTO1, 0x11);
		tm6000_set_reg(dev, TM6010_REQ07_R5B_VBI_TELETEXT_DTO0, 0x4c);
		tm6000_set_reg(dev, TM6010_REQ07_R40_TELETEXT_VBI_CODE0, 0x01);
		tm6000_set_reg(dev, TM6010_REQ07_R3F_RESET, 0x00);
	}
}

int tm6000_init_analog_mode(struct tm6000_core *dev)
{
	struct v4l2_frequency f;

	if (dev->dev_type == TM6010) {
		u8 active = TM6010_REQ07_RCC_ACTIVE_IF_AUDIO_ENABLE;

		if (!dev->radio)
			active |= TM6010_REQ07_RCC_ACTIVE_IF_VIDEO_ENABLE;

		/* Enable video and audio */
		tm6000_set_reg_mask(dev, TM6010_REQ07_RCC_ACTIVE_IF,
							active, 0x60);
		/* Disable TS input */
		tm6000_set_reg_mask(dev, TM6010_REQ07_RC0_ACTIVE_VIDEO_SOURCE,
							0x00, 0x40);
	} else {
		/* Enables soft reset */
		tm6000_set_reg(dev, TM6010_REQ07_R3F_RESET, 0x01);

		if (dev->scaler)
			/* Disable Hfilter and Enable TS Drop err */
			tm6000_set_reg(dev, TM6010_REQ07_RC0_ACTIVE_VIDEO_SOURCE, 0x20);
		else	/* Enable Hfilter and disable TS Drop err */
			tm6000_set_reg(dev, TM6010_REQ07_RC0_ACTIVE_VIDEO_SOURCE, 0x80);

		tm6000_set_reg(dev, TM6010_REQ07_RC3_HSTART1, 0x88);
		tm6000_set_reg(dev, TM6000_REQ07_RDA_CLK_SEL, 0x23);
		tm6000_set_reg(dev, TM6010_REQ07_RD1_ADDR_FOR_REQ1, 0xc0);
		tm6000_set_reg(dev, TM6010_REQ07_RD2_ADDR_FOR_REQ2, 0xd8);
		tm6000_set_reg(dev, TM6010_REQ07_RD6_ENDP_REQ1_REQ2, 0x06);
		tm6000_set_reg(dev, TM6000_REQ07_RDF_PWDOWN_ACLK, 0x1f);

		/* AP Software reset */
		tm6000_set_reg(dev, TM6010_REQ07_RFF_SOFT_RESET, 0x08);
		tm6000_set_reg(dev, TM6010_REQ07_RFF_SOFT_RESET, 0x00);

		tm6000_set_fourcc_format(dev);

		/* Disables soft reset */
		tm6000_set_reg(dev, TM6010_REQ07_R3F_RESET, 0x00);
	}
	msleep(20);

	/* Tuner firmware can now be loaded */

	/*
	 * FIXME: This is a hack! xc3028 "sleeps" when no channel is detected
	 * for more than a few seconds. Not sure why, as this behavior does
	 * not happen on other devices with xc3028. So, I suspect that it
	 * is yet another bug at tm6000. After start sleeping, decoding
	 * doesn't start automatically. Instead, it requires some
	 * I2C commands to wake it up. As we want to have image at the
	 * beginning, we needed to add this hack. The better would be to
	 * discover some way to make tm6000 to wake up without this hack.
	 */
	f.frequency = dev->freq;
	v4l2_device_call_all(&dev->v4l2_dev, 0, tuner, s_frequency, &f);

	msleep(100);
	tm6000_set_standard(dev);
	tm6000_set_vbi(dev);
	tm6000_set_audio_bitrate(dev, 48000);

	/* switch dvb led off */
	if (dev->gpio.dvb_led) {
		tm6000_set_reg(dev, REQ_03_SET_GET_MCU_PIN,
			dev->gpio.dvb_led, 0x01);
	}

	return 0;
}

int tm6000_init_digital_mode(struct tm6000_core *dev)
{
	if (dev->dev_type == TM6010) {
		/* Disable video and audio */
		tm6000_set_reg_mask(dev, TM6010_REQ07_RCC_ACTIVE_IF,
				0x00, 0x60);
		/* Enable TS input */
		tm6000_set_reg_mask(dev, TM6010_REQ07_RC0_ACTIVE_VIDEO_SOURCE,
				0x40, 0x40);
		/* all power down, but not the digital data port */
		tm6000_set_reg(dev, TM6010_REQ07_RFE_POWER_DOWN, 0x28);
		tm6000_set_reg(dev, TM6010_REQ08_RE2_POWER_DOWN_CTRL1, 0xfc);
		tm6000_set_reg(dev, TM6010_REQ08_RE6_POWER_DOWN_CTRL2, 0xff);
	} else  {
		tm6000_set_reg(dev, TM6010_REQ07_RFF_SOFT_RESET, 0x08);
		tm6000_set_reg(dev, TM6010_REQ07_RFF_SOFT_RESET, 0x00);
		tm6000_set_reg(dev, TM6010_REQ07_R3F_RESET, 0x01);
		tm6000_set_reg(dev, TM6000_REQ07_RDF_PWDOWN_ACLK, 0x08);
		tm6000_set_reg(dev, TM6000_REQ07_RE2_VADC_STATUS_CTL, 0x0c);
		tm6000_set_reg(dev, TM6000_REQ07_RE8_VADC_PWDOWN_CTL, 0xff);
		tm6000_set_reg(dev, TM6000_REQ07_REB_VADC_AADC_MODE, 0xd8);
		tm6000_set_reg(dev, TM6010_REQ07_RC0_ACTIVE_VIDEO_SOURCE, 0x40);
		tm6000_set_reg(dev, TM6010_REQ07_RC1_TRESHOLD, 0xd0);
		tm6000_set_reg(dev, TM6010_REQ07_RC3_HSTART1, 0x09);
		tm6000_set_reg(dev, TM6000_REQ07_RDA_CLK_SEL, 0x37);
		tm6000_set_reg(dev, TM6010_REQ07_RD1_ADDR_FOR_REQ1, 0xd8);
		tm6000_set_reg(dev, TM6010_REQ07_RD2_ADDR_FOR_REQ2, 0xc0);
		tm6000_set_reg(dev, TM6010_REQ07_RD6_ENDP_REQ1_REQ2, 0x60);

		tm6000_set_reg(dev, TM6000_REQ07_RE2_VADC_STATUS_CTL, 0x0c);
		tm6000_set_reg(dev, TM6000_REQ07_RE8_VADC_PWDOWN_CTL, 0xff);
		tm6000_set_reg(dev, TM6000_REQ07_REB_VADC_AADC_MODE, 0x08);
		msleep(50);

		tm6000_set_reg(dev, REQ_04_EN_DISABLE_MCU_INT, 0x0020, 0x00);
		msleep(50);
		tm6000_set_reg(dev, REQ_04_EN_DISABLE_MCU_INT, 0x0020, 0x01);
		msleep(50);
		tm6000_set_reg(dev, REQ_04_EN_DISABLE_MCU_INT, 0x0020, 0x00);
		msleep(100);
	}

	/* switch dvb led on */
	if (dev->gpio.dvb_led) {
		tm6000_set_reg(dev, REQ_03_SET_GET_MCU_PIN,
			dev->gpio.dvb_led, 0x00);
	}

	return 0;
}
EXPORT_SYMBOL(tm6000_init_digital_mode);

struct reg_init {
	u8 req;
	u8 reg;
	u8 val;
};

/* The meaning of those initializations are unknown */
static struct reg_init tm6000_init_tab[] = {
	/* REG  VALUE */
	{ TM6000_REQ07_RDF_PWDOWN_ACLK, 0x1f },
	{ TM6010_REQ07_RFF_SOFT_RESET, 0x08 },
	{ TM6010_REQ07_RFF_SOFT_RESET, 0x00 },
	{ TM6010_REQ07_RD5_POWERSAVE, 0x4f },
	{ TM6000_REQ07_RDA_CLK_SEL, 0x23 },
	{ TM6000_REQ07_RDB_OUT_SEL, 0x08 },
	{ TM6000_REQ07_RE2_VADC_STATUS_CTL, 0x00 },
	{ TM6000_REQ07_RE3_VADC_INP_LPF_SEL1, 0x10 },
	{ TM6000_REQ07_RE5_VADC_INP_LPF_SEL2, 0x00 },
	{ TM6000_REQ07_RE8_VADC_PWDOWN_CTL, 0x00 },
	{ TM6000_REQ07_REB_VADC_AADC_MODE, 0x64 },	/* 48000 bits/sample, external input */
	{ TM6000_REQ07_REE_VADC_CTRL_SEL_CONTROL, 0xc2 },

	{ TM6010_REQ07_R3F_RESET, 0x01 },		/* Start of soft reset */
	{ TM6010_REQ07_R00_VIDEO_CONTROL0, 0x00 },
	{ TM6010_REQ07_R01_VIDEO_CONTROL1, 0x07 },
	{ TM6010_REQ07_R02_VIDEO_CONTROL2, 0x5f },
	{ TM6010_REQ07_R03_YC_SEP_CONTROL, 0x00 },
	{ TM6010_REQ07_R05_NOISE_THRESHOLD, 0x64 },
	{ TM6010_REQ07_R07_OUTPUT_CONTROL, 0x01 },
	{ TM6010_REQ07_R08_LUMA_CONTRAST_ADJ, 0x82 },
	{ TM6010_REQ07_R09_LUMA_BRIGHTNESS_ADJ, 0x36 },
	{ TM6010_REQ07_R0A_CHROMA_SATURATION_ADJ, 0x50 },
	{ TM6010_REQ07_R0C_CHROMA_AGC_CONTROL, 0x6a },
	{ TM6010_REQ07_R11_AGC_PEAK_CONTROL, 0xc9 },
	{ TM6010_REQ07_R12_AGC_GATE_STARTH, 0x07 },
	{ TM6010_REQ07_R13_AGC_GATE_STARTL, 0x3b },
	{ TM6010_REQ07_R14_AGC_GATE_WIDTH, 0x47 },
	{ TM6010_REQ07_R15_AGC_BP_DELAY, 0x6f },
	{ TM6010_REQ07_R17_HLOOP_MAXSTATE, 0xcd },
	{ TM6010_REQ07_R18_CHROMA_DTO_INCREMENT3, 0x1e },
	{ TM6010_REQ07_R19_CHROMA_DTO_INCREMENT2, 0x8b },
	{ TM6010_REQ07_R1A_CHROMA_DTO_INCREMENT1, 0xa2 },
	{ TM6010_REQ07_R1B_CHROMA_DTO_INCREMENT0, 0xe9 },
	{ TM6010_REQ07_R1C_HSYNC_DTO_INCREMENT3, 0x1c },
	{ TM6010_REQ07_R1D_HSYNC_DTO_INCREMENT2, 0xcc },
	{ TM6010_REQ07_R1E_HSYNC_DTO_INCREMENT1, 0xcc },
	{ TM6010_REQ07_R1F_HSYNC_DTO_INCREMENT0, 0xcd },
	{ TM6010_REQ07_R20_HSYNC_RISING_EDGE_TIME, 0x3c },
	{ TM6010_REQ07_R21_HSYNC_PHASE_OFFSET, 0x3c },
	{ TM6010_REQ07_R2D_CHROMA_BURST_END, 0x48 },
	{ TM6010_REQ07_R2E_ACTIVE_VIDEO_HSTART, 0x88 },
	{ TM6010_REQ07_R30_ACTIVE_VIDEO_VSTART, 0x22 },
	{ TM6010_REQ07_R31_ACTIVE_VIDEO_VHIGHT, 0x61 },
	{ TM6010_REQ07_R32_VSYNC_HLOCK_MIN, 0x74 },
	{ TM6010_REQ07_R33_VSYNC_HLOCK_MAX, 0x1c },
	{ TM6010_REQ07_R34_VSYNC_AGC_MIN, 0x74 },
	{ TM6010_REQ07_R35_VSYNC_AGC_MAX, 0x1c },
	{ TM6010_REQ07_R36_VSYNC_VBI_MIN, 0x7a },
	{ TM6010_REQ07_R37_VSYNC_VBI_MAX, 0x26 },
	{ TM6010_REQ07_R38_VSYNC_THRESHOLD, 0x40 },
	{ TM6010_REQ07_R39_VSYNC_TIME_CONSTANT, 0x0a },
	{ TM6010_REQ07_R42_VBI_DATA_HIGH_LEVEL, 0x55 },
	{ TM6010_REQ07_R51_VBI_DATA_TYPE_LINE21, 0x11 },
	{ TM6010_REQ07_R55_VBI_LOOP_FILTER_GAIN, 0x01 },
	{ TM6010_REQ07_R57_VBI_LOOP_FILTER_P_GAIN, 0x02 },
	{ TM6010_REQ07_R58_VBI_CAPTION_DTO1, 0x35 },
	{ TM6010_REQ07_R59_VBI_CAPTION_DTO0, 0xa0 },
	{ TM6010_REQ07_R80_COMB_FILTER_TRESHOLD, 0x15 },
	{ TM6010_REQ07_R82_COMB_FILTER_CONFIG, 0x42 },
	{ TM6010_REQ07_RC1_TRESHOLD, 0xd0 },
	{ TM6010_REQ07_RC3_HSTART1, 0x88 },
	{ TM6010_REQ07_R3F_RESET, 0x00 },		/* End of the soft reset */
	{ TM6010_REQ05_R18_IMASK7, 0x00 },
};

static struct reg_init tm6010_init_tab[] = {
	{ TM6010_REQ07_RC0_ACTIVE_VIDEO_SOURCE, 0x00 },
	{ TM6010_REQ07_RC4_HSTART0, 0xa0 },
	{ TM6010_REQ07_RC6_HEND0, 0x40 },
	{ TM6010_REQ07_RCA_VEND0, 0x31 },
	{ TM6010_REQ07_RCC_ACTIVE_IF, 0xe1 },
	{ TM6010_REQ07_RE0_DVIDEO_SOURCE, 0x03 },
	{ TM6010_REQ07_RFE_POWER_DOWN, 0x7f },

	{ TM6010_REQ08_RE2_POWER_DOWN_CTRL1, 0xf0 },
	{ TM6010_REQ08_RE3_ADC_IN1_SEL, 0xf4 },
	{ TM6010_REQ08_RE4_ADC_IN2_SEL, 0xf8 },
	{ TM6010_REQ08_RE6_POWER_DOWN_CTRL2, 0x00 },
	{ TM6010_REQ08_REA_BUFF_DRV_CTRL, 0xf2 },
	{ TM6010_REQ08_REB_SIF_GAIN_CTRL, 0xf0 },
	{ TM6010_REQ08_REC_REVERSE_YC_CTRL, 0xc2 },
	{ TM6010_REQ08_RF0_DAUDIO_INPUT_CONFIG, 0x60 },
	{ TM6010_REQ08_RF1_AADC_POWER_DOWN, 0xfc },

	{ TM6010_REQ07_R3F_RESET, 0x01 },
	{ TM6010_REQ07_R00_VIDEO_CONTROL0, 0x00 },
	{ TM6010_REQ07_R01_VIDEO_CONTROL1, 0x07 },
	{ TM6010_REQ07_R02_VIDEO_CONTROL2, 0x5f },
	{ TM6010_REQ07_R03_YC_SEP_CONTROL, 0x00 },
	{ TM6010_REQ07_R05_NOISE_THRESHOLD, 0x64 },
	{ TM6010_REQ07_R07_OUTPUT_CONTROL, 0x01 },
	{ TM6010_REQ07_R08_LUMA_CONTRAST_ADJ, 0x82 },
	{ TM6010_REQ07_R09_LUMA_BRIGHTNESS_ADJ, 0x36 },
	{ TM6010_REQ07_R0A_CHROMA_SATURATION_ADJ, 0x50 },
	{ TM6010_REQ07_R0C_CHROMA_AGC_CONTROL, 0x6a },
	{ TM6010_REQ07_R11_AGC_PEAK_CONTROL, 0xc9 },
	{ TM6010_REQ07_R12_AGC_GATE_STARTH, 0x07 },
	{ TM6010_REQ07_R13_AGC_GATE_STARTL, 0x3b },
	{ TM6010_REQ07_R14_AGC_GATE_WIDTH, 0x47 },
	{ TM6010_REQ07_R15_AGC_BP_DELAY, 0x6f },
	{ TM6010_REQ07_R17_HLOOP_MAXSTATE, 0xcd },
	{ TM6010_REQ07_R18_CHROMA_DTO_INCREMENT3, 0x1e },
	{ TM6010_REQ07_R19_CHROMA_DTO_INCREMENT2, 0x8b },
	{ TM6010_REQ07_R1A_CHROMA_DTO_INCREMENT1, 0xa2 },
	{ TM6010_REQ07_R1B_CHROMA_DTO_INCREMENT0, 0xe9 },
	{ TM6010_REQ07_R1C_HSYNC_DTO_INCREMENT3, 0x1c },
	{ TM6010_REQ07_R1D_HSYNC_DTO_INCREMENT2, 0xcc },
	{ TM6010_REQ07_R1E_HSYNC_DTO_INCREMENT1, 0xcc },
	{ TM6010_REQ07_R1F_HSYNC_DTO_INCREMENT0, 0xcd },
	{ TM6010_REQ07_R20_HSYNC_RISING_EDGE_TIME, 0x3c },
	{ TM6010_REQ07_R21_HSYNC_PHASE_OFFSET, 0x3c },
	{ TM6010_REQ07_R2D_CHROMA_BURST_END, 0x48 },
	{ TM6010_REQ07_R2E_ACTIVE_VIDEO_HSTART, 0x88 },
	{ TM6010_REQ07_R30_ACTIVE_VIDEO_VSTART, 0x22 },
	{ TM6010_REQ07_R31_ACTIVE_VIDEO_VHIGHT, 0x61 },
	{ TM6010_REQ07_R32_VSYNC_HLOCK_MIN, 0x74 },
	{ TM6010_REQ07_R33_VSYNC_HLOCK_MAX, 0x1c },
	{ TM6010_REQ07_R34_VSYNC_AGC_MIN, 0x74 },
	{ TM6010_REQ07_R35_VSYNC_AGC_MAX, 0x1c },
	{ TM6010_REQ07_R36_VSYNC_VBI_MIN, 0x7a },
	{ TM6010_REQ07_R37_VSYNC_VBI_MAX, 0x26 },
	{ TM6010_REQ07_R38_VSYNC_THRESHOLD, 0x40 },
	{ TM6010_REQ07_R39_VSYNC_TIME_CONSTANT, 0x0a },
	{ TM6010_REQ07_R42_VBI_DATA_HIGH_LEVEL, 0x55 },
	{ TM6010_REQ07_R51_VBI_DATA_TYPE_LINE21, 0x11 },
	{ TM6010_REQ07_R55_VBI_LOOP_FILTER_GAIN, 0x01 },
	{ TM6010_REQ07_R57_VBI_LOOP_FILTER_P_GAIN, 0x02 },
	{ TM6010_REQ07_R58_VBI_CAPTION_DTO1, 0x35 },
	{ TM6010_REQ07_R59_VBI_CAPTION_DTO0, 0xa0 },
	{ TM6010_REQ07_R80_COMB_FILTER_TRESHOLD, 0x15 },
	{ TM6010_REQ07_R82_COMB_FILTER_CONFIG, 0x42 },
	{ TM6010_REQ07_RC1_TRESHOLD, 0xd0 },
	{ TM6010_REQ07_RC3_HSTART1, 0x88 },
	{ TM6010_REQ07_R3F_RESET, 0x00 },

	{ TM6010_REQ05_R18_IMASK7, 0x00 },

	{ TM6010_REQ07_RDC_IR_LEADER1, 0xaa },
	{ TM6010_REQ07_RDD_IR_LEADER0, 0x30 },
	{ TM6010_REQ07_RDE_IR_PULSE_CNT1, 0x20 },
	{ TM6010_REQ07_RDF_IR_PULSE_CNT0, 0xd0 },
	{ REQ_04_EN_DISABLE_MCU_INT, 0x02, 0x00 },
	{ TM6010_REQ07_RD8_IR, 0x0f },

	/* set remote wakeup key:any key wakeup */
	{ TM6010_REQ07_RE5_REMOTE_WAKEUP,  0xfe },
	{ TM6010_REQ07_RDA_IR_WAKEUP_SEL,  0xff },
};

int tm6000_init(struct tm6000_core *dev)
{
	int board, rc = 0, i, size;
	struct reg_init *tab;

	/* Check board revision */
	board = tm6000_get_reg32(dev, REQ_40_GET_VERSION, 0, 0);
	if (board >= 0) {
		switch (board & 0xff) {
		case 0xf3:
			printk(KERN_INFO "Found tm6000\n");
			if (dev->dev_type != TM6000)
				dev->dev_type = TM6000;
			break;
		case 0xf4:
			printk(KERN_INFO "Found tm6010\n");
			if (dev->dev_type != TM6010)
				dev->dev_type = TM6010;
			break;
		default:
			printk(KERN_INFO "Unknown board version = 0x%08x\n", board);
		}
	} else
		printk(KERN_ERR "Error %i while retrieving board version\n", board);

	if (dev->dev_type == TM6010) {
		tab = tm6010_init_tab;
		size = ARRAY_SIZE(tm6010_init_tab);
	} else {
		tab = tm6000_init_tab;
		size = ARRAY_SIZE(tm6000_init_tab);
	}

	/* Load board's initialization table */
	for (i = 0; i < size; i++) {
		rc = tm6000_set_reg(dev, tab[i].req, tab[i].reg, tab[i].val);
		if (rc < 0) {
			printk(KERN_ERR "Error %i while setting req %d, reg %d to value %d\n",
			       rc,
					tab[i].req, tab[i].reg, tab[i].val);
			return rc;
		}
	}

	msleep(5); /* Just to be conservative */

	rc = tm6000_cards_setup(dev);

	return rc;
}


int tm6000_set_audio_bitrate(struct tm6000_core *dev, int bitrate)
{
	int val = 0;
	u8 areg_f0 = 0x60; /* ADC MCLK = 250 Fs */
	u8 areg_0a = 0x91; /* SIF 48KHz */

	switch (bitrate) {
	case 48000:
		areg_f0 = 0x60; /* ADC MCLK = 250 Fs */
		areg_0a = 0x91; /* SIF 48KHz */
		dev->audio_bitrate = bitrate;
		break;
	case 32000:
		areg_f0 = 0x00; /* ADC MCLK = 375 Fs */
		areg_0a = 0x90; /* SIF 32KHz */
		dev->audio_bitrate = bitrate;
		break;
	default:
		return -EINVAL;
	}


	/* enable I2S, if we use sif or external I2S device */
	if (dev->dev_type == TM6010) {
		val = tm6000_set_reg(dev, TM6010_REQ08_R0A_A_I2S_MOD, areg_0a);
		if (val < 0)
			return val;

		val = tm6000_set_reg_mask(dev, TM6010_REQ08_RF0_DAUDIO_INPUT_CONFIG,
							areg_f0, 0xf0);
		if (val < 0)
			return val;
	} else {
		val = tm6000_set_reg_mask(dev, TM6000_REQ07_REB_VADC_AADC_MODE,
							areg_f0, 0xf0);
		if (val < 0)
			return val;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(tm6000_set_audio_bitrate);

int tm6000_set_audio_rinput(struct tm6000_core *dev)
{
	if (dev->dev_type == TM6010) {
		/* Audio crossbar setting, default SIF1 */
		u8 areg_f0;
		u8 areg_07 = 0x10;

		switch (dev->rinput.amux) {
		case TM6000_AMUX_SIF1:
		case TM6000_AMUX_SIF2:
			areg_f0 = 0x03;
			areg_07 = 0x30;
			break;
		case TM6000_AMUX_ADC1:
			areg_f0 = 0x00;
			break;
		case TM6000_AMUX_ADC2:
			areg_f0 = 0x08;
			break;
		case TM6000_AMUX_I2S:
			areg_f0 = 0x04;
			break;
		default:
			printk(KERN_INFO "%s: audio input dosn't support\n",
				dev->name);
			return 0;
			break;
		}
		/* Set audio input crossbar */
		tm6000_set_reg_mask(dev, TM6010_REQ08_RF0_DAUDIO_INPUT_CONFIG,
							areg_f0, 0x0f);
		/* Mux overflow workaround */
		tm6000_set_reg_mask(dev, TM6010_REQ07_R07_OUTPUT_CONTROL,
			areg_07, 0xf0);
	} else {
		u8 areg_eb;
		/* Audio setting, default LINE1 */
		switch (dev->rinput.amux) {
		case TM6000_AMUX_ADC1:
			areg_eb = 0x00;
			break;
		case TM6000_AMUX_ADC2:
			areg_eb = 0x04;
			break;
		default:
			printk(KERN_INFO "%s: audio input dosn't support\n",
				dev->name);
			return 0;
			break;
		}
		/* Set audio input */
		tm6000_set_reg_mask(dev, TM6000_REQ07_REB_VADC_AADC_MODE,
							areg_eb, 0x0f);
	}
	return 0;
}

static void tm6010_set_mute_sif(struct tm6000_core *dev, u8 mute)
{
	u8 mute_reg = 0;

	if (mute)
		mute_reg = 0x08;

	tm6000_set_reg_mask(dev, TM6010_REQ08_R0A_A_I2S_MOD, mute_reg, 0x08);
}

static void tm6010_set_mute_adc(struct tm6000_core *dev, u8 mute)
{
	u8 mute_reg = 0;

	if (mute)
		mute_reg = 0x20;

	if (dev->dev_type == TM6010) {
		tm6000_set_reg_mask(dev, TM6010_REQ08_RF2_LEFT_CHANNEL_VOL,
							mute_reg, 0x20);
		tm6000_set_reg_mask(dev, TM6010_REQ08_RF3_RIGHT_CHANNEL_VOL,
							mute_reg, 0x20);
	} else {
		tm6000_set_reg_mask(dev, TM6000_REQ07_REC_VADC_AADC_LVOL,
							mute_reg, 0x20);
		tm6000_set_reg_mask(dev, TM6000_REQ07_RED_VADC_AADC_RVOL,
							mute_reg, 0x20);
	}
}

int tm6000_tvaudio_set_mute(struct tm6000_core *dev, u8 mute)
{
	enum tm6000_mux mux;

	if (dev->radio)
		mux = dev->rinput.amux;
	else
		mux = dev->vinput[dev->input].amux;

	switch (mux) {
	case TM6000_AMUX_SIF1:
	case TM6000_AMUX_SIF2:
		if (dev->dev_type == TM6010)
			tm6010_set_mute_sif(dev, mute);
		else {
			printk(KERN_INFO "ERROR: TM5600 and TM6000 don't has SIF audio inputs. Please check the %s configuration.\n",
			       dev->name);
			return -EINVAL;
		}
		break;
	case TM6000_AMUX_ADC1:
	case TM6000_AMUX_ADC2:
		tm6010_set_mute_adc(dev, mute);
		break;
	default:
		return -EINVAL;
		break;
	}
	return 0;
}

static void tm6010_set_volume_sif(struct tm6000_core *dev, int vol)
{
	u8 vol_reg;

	vol_reg = vol & 0x0F;

	if (vol < 0)
		vol_reg |= 0x40;

	tm6000_set_reg(dev, TM6010_REQ08_R07_A_LEFT_VOL, vol_reg);
	tm6000_set_reg(dev, TM6010_REQ08_R08_A_RIGHT_VOL, vol_reg);
}

static void tm6010_set_volume_adc(struct tm6000_core *dev, int vol)
{
	u8 vol_reg;

	vol_reg = (vol + 0x10) & 0x1f;

	if (dev->dev_type == TM6010) {
		tm6000_set_reg(dev, TM6010_REQ08_RF2_LEFT_CHANNEL_VOL, vol_reg);
		tm6000_set_reg(dev, TM6010_REQ08_RF3_RIGHT_CHANNEL_VOL, vol_reg);
	} else {
		tm6000_set_reg(dev, TM6000_REQ07_REC_VADC_AADC_LVOL, vol_reg);
		tm6000_set_reg(dev, TM6000_REQ07_RED_VADC_AADC_RVOL, vol_reg);
	}
}

void tm6000_set_volume(struct tm6000_core *dev, int vol)
{
	enum tm6000_mux mux;

	if (dev->radio) {
		mux = dev->rinput.amux;
		vol += 8; /* Offset to 0 dB */
	} else
		mux = dev->vinput[dev->input].amux;

	switch (mux) {
	case TM6000_AMUX_SIF1:
	case TM6000_AMUX_SIF2:
		if (dev->dev_type == TM6010)
			tm6010_set_volume_sif(dev, vol);
		else
			printk(KERN_INFO "ERROR: TM5600 and TM6000 don't has SIF audio inputs. Please check the %s configuration.\n",
			       dev->name);
		break;
	case TM6000_AMUX_ADC1:
	case TM6000_AMUX_ADC2:
		tm6010_set_volume_adc(dev, vol);
		break;
	default:
		break;
	}
}

static LIST_HEAD(tm6000_devlist);
static DEFINE_MUTEX(tm6000_devlist_mutex);

/*
 * tm6000_realease_resource()
 */

void tm6000_remove_from_devlist(struct tm6000_core *dev)
{
	mutex_lock(&tm6000_devlist_mutex);
	list_del(&dev->devlist);
	mutex_unlock(&tm6000_devlist_mutex);
};

void tm6000_add_into_devlist(struct tm6000_core *dev)
{
	mutex_lock(&tm6000_devlist_mutex);
	list_add_tail(&dev->devlist, &tm6000_devlist);
	mutex_unlock(&tm6000_devlist_mutex);
};

/*
 * Extension interface
 */

static LIST_HEAD(tm6000_extension_devlist);

int tm6000_call_fillbuf(struct tm6000_core *dev, enum tm6000_ops_type type,
			char *buf, int size)
{
	struct tm6000_ops *ops = NULL;

	/* FIXME: tm6000_extension_devlist_lock should be a spinlock */

	if (!list_empty(&tm6000_extension_devlist)) {
		list_for_each_entry(ops, &tm6000_extension_devlist, next) {
			if (ops->fillbuf && ops->type == type)
				ops->fillbuf(dev, buf, size);
		}
	}

	return 0;
}

int tm6000_register_extension(struct tm6000_ops *ops)
{
	struct tm6000_core *dev = NULL;

	mutex_lock(&tm6000_devlist_mutex);
	list_add_tail(&ops->next, &tm6000_extension_devlist);
	list_for_each_entry(dev, &tm6000_devlist, devlist) {
		ops->init(dev);
		printk(KERN_INFO "%s: Initialized (%s) extension\n",
		       dev->name, ops->name);
	}
	mutex_unlock(&tm6000_devlist_mutex);
	return 0;
}
EXPORT_SYMBOL(tm6000_register_extension);

void tm6000_unregister_extension(struct tm6000_ops *ops)
{
	struct tm6000_core *dev = NULL;

	mutex_lock(&tm6000_devlist_mutex);
	list_for_each_entry(dev, &tm6000_devlist, devlist)
		ops->fini(dev);

	printk(KERN_INFO "tm6000: Remove (%s) extension\n", ops->name);
	list_del(&ops->next);
	mutex_unlock(&tm6000_devlist_mutex);
}
EXPORT_SYMBOL(tm6000_unregister_extension);

void tm6000_init_extension(struct tm6000_core *dev)
{
	struct tm6000_ops *ops = NULL;

	mutex_lock(&tm6000_devlist_mutex);
	if (!list_empty(&tm6000_extension_devlist)) {
		list_for_each_entry(ops, &tm6000_extension_devlist, next) {
			if (ops->init)
				ops->init(dev);
		}
	}
	mutex_unlock(&tm6000_devlist_mutex);
}

void tm6000_close_extension(struct tm6000_core *dev)
{
	struct tm6000_ops *ops = NULL;

	mutex_lock(&tm6000_devlist_mutex);
	if (!list_empty(&tm6000_extension_devlist)) {
		list_for_each_entry(ops, &tm6000_extension_devlist, next) {
			if (ops->fini)
				ops->fini(dev);
		}
	}
	mutex_unlock(&tm6000_devlist_mutex);
}

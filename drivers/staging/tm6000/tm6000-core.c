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
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
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

	if (len)
		data = kzalloc(len, GFP_KERNEL);


	if (req_type & USB_DIR_IN)
		pipe = usb_rcvctrlpipe(dev->udev, 0);
	else {
		pipe = usb_sndctrlpipe(dev->udev, 0);
		memcpy(data, buf, len);
	}

	if (tm6000_debug & V4L2_DEBUG_I2C) {
		printk("(dev %p, pipe %08x): ", dev->udev, pipe);

		printk("%s: %02x %02x %02x %02x %02x %02x %02x %02x ",
			(req_type & USB_DIR_IN) ? " IN" : "OUT",
			req_type, req, value&0xff, value>>8, index&0xff,
			index>>8, len&0xff, len>>8);

		if (!(req_type & USB_DIR_IN)) {
			printk(">>> ");
			for (i = 0; i < len; i++)
				printk(" %02x", buf[i]);
		printk("\n");
		}
	}

	ret = usb_control_msg(dev->udev, pipe, req, req_type, value, index,
			      data, len, USB_TIMEOUT);

	if (req_type &  USB_DIR_IN)
		memcpy(buf, data, len);

	if (tm6000_debug & V4L2_DEBUG_I2C) {
		if (ret < 0) {
			if (req_type &  USB_DIR_IN)
				printk("<<< (len=%d)\n", len);

			printk("%s: Error #%d\n", __FUNCTION__, ret);
		} else if (req_type &  USB_DIR_IN) {
			printk("<<< ");
			for (i = 0; i < len; i++)
				printk(" %02x", buf[i]);
			printk("\n");
		}
	}

	kfree(data);

	msleep(5);

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

		val = tm6000_get_reg(dev, TM6010_REQ07_RCC_ACTIVE_VIDEO_IF, 0) & 0xfc;
		if (dev->fourcc == V4L2_PIX_FMT_UYVY)
			tm6000_set_reg(dev, TM6010_REQ07_RCC_ACTIVE_VIDEO_IF, val);
		else
			tm6000_set_reg(dev, TM6010_REQ07_RCC_ACTIVE_VIDEO_IF, val | 1);
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
		int val;

		/* Enable video */
		val = tm6000_get_reg(dev, TM6010_REQ07_RCC_ACTIVE_VIDEO_IF, 0);
		val |= 0x60;
		tm6000_set_reg(dev, TM6010_REQ07_RCC_ACTIVE_VIDEO_IF, val);
		val = tm6000_get_reg(dev,
			TM6010_REQ07_RC0_ACTIVE_VIDEO_SOURCE, 0);
		val &= ~0x40;
		tm6000_set_reg(dev, TM6010_REQ07_RC0_ACTIVE_VIDEO_SOURCE, val);

		tm6000_set_reg(dev, TM6010_REQ08_RF1_AADC_POWER_DOWN, 0xfc);

	} else {
		/* Enables soft reset */
		tm6000_set_reg(dev, TM6010_REQ07_R3F_RESET, 0x01);

		if (dev->scaler)
			tm6000_set_reg(dev, TM6010_REQ07_RC0_ACTIVE_VIDEO_SOURCE, 0x20);
		else	/* Enable Hfilter and disable TS Drop err */
			tm6000_set_reg(dev, TM6010_REQ07_RC0_ACTIVE_VIDEO_SOURCE, 0x80);

		tm6000_set_reg(dev, TM6010_REQ07_RC3_HSTART1, 0x88);
		tm6000_set_reg(dev, TM6010_REQ07_RD8_IR_WAKEUP_SEL, 0x23);
		tm6000_set_reg(dev, TM6010_REQ07_RD1_ADDR_FOR_REQ1, 0xc0);
		tm6000_set_reg(dev, TM6010_REQ07_RD2_ADDR_FOR_REQ2, 0xd8);
		tm6000_set_reg(dev, TM6010_REQ07_RD6_ENDP_REQ1_REQ2, 0x06);
		tm6000_set_reg(dev, TM6010_REQ07_RD8_IR_PULSE_CNT0, 0x1f);

		/* AP Software reset */
		tm6000_set_reg(dev, TM6010_REQ07_RFF_SOFT_RESET, 0x08);
		tm6000_set_reg(dev, TM6010_REQ07_RFF_SOFT_RESET, 0x00);

		tm6000_set_fourcc_format(dev);

		/* Disables soft reset */
		tm6000_set_reg(dev, TM6010_REQ07_R3F_RESET, 0x00);

		/* E3: Select input 0 - TV tuner */
		tm6000_set_reg(dev, TM6010_REQ07_RE3_OUT_SEL1, 0x00);
		tm6000_set_reg(dev, REQ_07_SET_GET_AVREG, 0xeb, 0x60);

		/* This controls input */
		tm6000_set_reg(dev, REQ_03_SET_GET_MCU_PIN, TM6000_GPIO_2, 0x0);
		tm6000_set_reg(dev, REQ_03_SET_GET_MCU_PIN, TM6000_GPIO_3, 0x01);
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
	tm6000_set_standard(dev, &dev->norm);
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
		int val;
		u8 buf[2];

		/* digital init */
		val = tm6000_get_reg(dev, TM6010_REQ07_RCC_ACTIVE_VIDEO_IF, 0);
		val &= ~0x60;
		tm6000_set_reg(dev, TM6010_REQ07_RCC_ACTIVE_VIDEO_IF, val);
		val = tm6000_get_reg(dev, TM6010_REQ07_RC0_ACTIVE_VIDEO_SOURCE, 0);
		val |= 0x40;
		tm6000_set_reg(dev, TM6010_REQ07_RC0_ACTIVE_VIDEO_SOURCE, val);
		tm6000_set_reg(dev, TM6010_REQ07_RFE_POWER_DOWN, 0x28);
		tm6000_set_reg(dev, TM6010_REQ08_RE2_POWER_DOWN_CTRL1, 0xfc);
		tm6000_set_reg(dev, TM6010_REQ08_RE6_POWER_DOWN_CTRL2, 0xff);
		tm6000_read_write_usb(dev, 0xc0, 0x0e, 0x00c2, 0x0008, buf, 2);
		printk(KERN_INFO"buf %#x %#x\n", buf[0], buf[1]);
	} else  {
		tm6000_set_reg(dev, TM6010_REQ07_RFF_SOFT_RESET, 0x08);
		tm6000_set_reg(dev, TM6010_REQ07_RFF_SOFT_RESET, 0x00);
		tm6000_set_reg(dev, TM6010_REQ07_R3F_RESET, 0x01);
		tm6000_set_reg(dev, TM6010_REQ07_RD8_IR_PULSE_CNT0, 0x08);
		tm6000_set_reg(dev, TM6010_REQ07_RE2_OUT_SEL2, 0x0c);
		tm6000_set_reg(dev, TM6010_REQ07_RE8_TYPESEL_MOS_I2S, 0xff);
		tm6000_set_reg(dev, REQ_07_SET_GET_AVREG, 0x00eb, 0xd8);
		tm6000_set_reg(dev, TM6010_REQ07_RC0_ACTIVE_VIDEO_SOURCE, 0x40);
		tm6000_set_reg(dev, TM6010_REQ07_RC1_TRESHOLD, 0xd0);
		tm6000_set_reg(dev, TM6010_REQ07_RC3_HSTART1, 0x09);
		tm6000_set_reg(dev, TM6010_REQ07_RD8_IR_WAKEUP_SEL, 0x37);
		tm6000_set_reg(dev, TM6010_REQ07_RD1_ADDR_FOR_REQ1, 0xd8);
		tm6000_set_reg(dev, TM6010_REQ07_RD2_ADDR_FOR_REQ2, 0xc0);
		tm6000_set_reg(dev, TM6010_REQ07_RD6_ENDP_REQ1_REQ2, 0x60);

		tm6000_set_reg(dev, TM6010_REQ07_RE2_OUT_SEL2, 0x0c);
		tm6000_set_reg(dev, TM6010_REQ07_RE8_TYPESEL_MOS_I2S, 0xff);
		tm6000_set_reg(dev, REQ_07_SET_GET_AVREG, 0x00eb, 0x08);
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
struct reg_init tm6000_init_tab[] = {
	/* REG  VALUE */
	{ TM6010_REQ07_RD8_IR_PULSE_CNT0, 0x1f },
	{ TM6010_REQ07_RFF_SOFT_RESET, 0x08 },
	{ TM6010_REQ07_RFF_SOFT_RESET, 0x00 },
	{ TM6010_REQ07_RD5_POWERSAVE, 0x4f },
	{ TM6010_REQ07_RD8_IR_WAKEUP_SEL, 0x23 },
	{ TM6010_REQ07_RD8_IR_WAKEUP_ADD, 0x08 },
	{ TM6010_REQ07_RE2_OUT_SEL2, 0x00 },
	{ TM6010_REQ07_RE3_OUT_SEL1, 0x10 },
	{ TM6010_REQ07_RE5_REMOTE_WAKEUP, 0x00 },
	{ TM6010_REQ07_RE8_TYPESEL_MOS_I2S, 0x00 },
	{ REQ_07_SET_GET_AVREG,  0xeb, 0x64 },		/* 48000 bits/sample, external input */
	{ REQ_07_SET_GET_AVREG,  0xee, 0xc2 },
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

struct reg_init tm6010_init_tab[] = {
	{ TM6010_REQ07_RC0_ACTIVE_VIDEO_SOURCE, 0x00 },
	{ TM6010_REQ07_RC4_HSTART0, 0xa0 },
	{ TM6010_REQ07_RC6_HEND0, 0x40 },
	{ TM6010_REQ07_RCA_VEND0, 0x31 },
	{ TM6010_REQ07_RCC_ACTIVE_VIDEO_IF, 0xe1 },
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

	{ TM6010_REQ07_RD8_IR_LEADER1, 0xaa },
	{ TM6010_REQ07_RD8_IR_LEADER0, 0x30 },
	{ TM6010_REQ07_RD8_IR_PULSE_CNT1, 0x20 },
	{ TM6010_REQ07_RD8_IR_PULSE_CNT0, 0xd0 },
	{ REQ_04_EN_DISABLE_MCU_INT, 0x02, 0x00 },
	{ TM6010_REQ07_RD8_IR, 0x2f },

	/* set remote wakeup key:any key wakeup */
	{ TM6010_REQ07_RE5_REMOTE_WAKEUP,  0xfe },
	{ TM6010_REQ07_RD8_IR_WAKEUP_SEL,  0xff },
};

int tm6000_init(struct tm6000_core *dev)
{
	int board, rc = 0, i, size;
	struct reg_init *tab;

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
			printk(KERN_ERR "Error %i while setting req %d, "
					"reg %d to value %d\n", rc,
					tab[i].req, tab[i].reg, tab[i].val);
			return rc;
		}
	}

	msleep(5); /* Just to be conservative */

	/* Check board version - maybe 10Moons specific */
	board = tm6000_get_reg32(dev, REQ_40_GET_VERSION, 0, 0);
	if (board >= 0)
		printk(KERN_INFO "Board version = 0x%08x\n", board);
	else
		printk(KERN_ERR "Error %i while retrieving board version\n", board);

	rc = tm6000_cards_setup(dev);

	return rc;
}

int tm6000_set_audio_bitrate(struct tm6000_core *dev, int bitrate)
{
	int val;

	if (dev->dev_type == TM6010) {
		val = tm6000_get_reg(dev, TM6010_REQ08_R0A_A_I2S_MOD, 0);
		if (val < 0)
			return val;
		val = (val & 0xf0) | 0x1; /* 48 kHz, not muted */
		val = tm6000_set_reg(dev, TM6010_REQ08_R0A_A_I2S_MOD, val);
		if (val < 0)
			return val;
	}

	val = tm6000_get_reg(dev, REQ_07_SET_GET_AVREG, 0xeb, 0x0);
	if (val < 0)
		return val;

	val &= 0x0f;		/* Preserve the audio input control bits */
	switch (bitrate) {
	case 44100:
		val |= 0xd0;
		dev->audio_bitrate = bitrate;
		break;
	case 48000:
		val |= 0x60;
		dev->audio_bitrate = bitrate;
		break;
	}
	val = tm6000_set_reg(dev, REQ_07_SET_GET_AVREG, 0xeb, val);

	return val;
}
EXPORT_SYMBOL_GPL(tm6000_set_audio_bitrate);

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
	mutex_lock(&tm6000_devlist_mutex);
}

// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * av7110_v4l.c: av7110 video4linux interface for DVB and Siemens DVB-C analog module
 *
 * Copyright (C) 1999-2002 Ralph  Metzler
 *                       & Marcus Metzler for convergence integrated media GmbH
 *
 * originally based on code by:
 * Copyright (C) 1998,1999 Christian Theiss <mistert@rz.fh-augsburg.de>
 *
 * the project's page is at https://linuxtv.org
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/timer.h>
#include <linux/poll.h>

#include "av7110.h"
#include "av7110_hw.h"
#include "av7110_av.h"

int msp_writereg(struct av7110 *av7110, u8 dev, u16 reg, u16 val)
{
	u8 msg[5] = { dev, reg >> 8, reg & 0xff, val >> 8 , val & 0xff };
	struct i2c_msg msgs = { .flags = 0, .len = 5, .buf = msg };

	switch (av7110->adac_type) {
	case DVB_ADAC_MSP34x0:
		msgs.addr = 0x40;
		break;
	case DVB_ADAC_MSP34x5:
		msgs.addr = 0x42;
		break;
	default:
		return 0;
	}

	if (i2c_transfer(&av7110->i2c_adap, &msgs, 1) != 1) {
		dprintk(1, "dvb-ttpci: failed @ card %d, %u = %u\n",
		       av7110->dvb_adapter.num, reg, val);
		return -EIO;
	}
	return 0;
}

static int msp_readreg(struct av7110 *av7110, u8 dev, u16 reg, u16 *val)
{
	u8 msg1[3] = { dev, reg >> 8, reg & 0xff };
	u8 msg2[2];
	struct i2c_msg msgs[2] = {
		{ .flags = 0	   , .len = 3, .buf = msg1 },
		{ .flags = I2C_M_RD, .len = 2, .buf = msg2 }
	};

	switch (av7110->adac_type) {
	case DVB_ADAC_MSP34x0:
		msgs[0].addr = 0x40;
		msgs[1].addr = 0x40;
		break;
	case DVB_ADAC_MSP34x5:
		msgs[0].addr = 0x42;
		msgs[1].addr = 0x42;
		break;
	default:
		return 0;
	}

	if (i2c_transfer(&av7110->i2c_adap, &msgs[0], 2) != 2) {
		dprintk(1, "dvb-ttpci: failed @ card %d, %u\n",
		       av7110->dvb_adapter.num, reg);
		return -EIO;
	}
	*val = (msg2[0] << 8) | msg2[1];
	return 0;
}

static struct v4l2_input inputs[4] = {
	{
		.index		= 0,
		.name		= "DVB",
		.type		= V4L2_INPUT_TYPE_CAMERA,
		.audioset	= 1,
		.tuner		= 0, /* ignored */
		.std		= V4L2_STD_PAL_BG|V4L2_STD_NTSC_M,
		.status		= 0,
		.capabilities	= V4L2_IN_CAP_STD,
	}, {
		.index		= 1,
		.name		= "Television",
		.type		= V4L2_INPUT_TYPE_TUNER,
		.audioset	= 1,
		.tuner		= 0,
		.std		= V4L2_STD_PAL_BG|V4L2_STD_NTSC_M,
		.status		= 0,
		.capabilities	= V4L2_IN_CAP_STD,
	}, {
		.index		= 2,
		.name		= "Video",
		.type		= V4L2_INPUT_TYPE_CAMERA,
		.audioset	= 0,
		.tuner		= 0,
		.std		= V4L2_STD_PAL_BG|V4L2_STD_NTSC_M,
		.status		= 0,
		.capabilities	= V4L2_IN_CAP_STD,
	}, {
		.index		= 3,
		.name		= "Y/C",
		.type		= V4L2_INPUT_TYPE_CAMERA,
		.audioset	= 0,
		.tuner		= 0,
		.std		= V4L2_STD_PAL_BG|V4L2_STD_NTSC_M,
		.status		= 0,
		.capabilities	= V4L2_IN_CAP_STD,
	}
};

static int ves1820_writereg(struct saa7146_dev *dev, u8 addr, u8 reg, u8 data)
{
	struct av7110 *av7110 = dev->ext_priv;
	u8 buf[] = { 0x00, reg, data };
	struct i2c_msg msg = { .addr = addr, .flags = 0, .buf = buf, .len = 3 };

	dprintk(4, "dev: %p\n", dev);

	if (1 != i2c_transfer(&av7110->i2c_adap, &msg, 1))
		return -1;
	return 0;
}

static int tuner_write(struct saa7146_dev *dev, u8 addr, u8 data [4])
{
	struct av7110 *av7110 = dev->ext_priv;
	struct i2c_msg msg = { .addr = addr, .flags = 0, .buf = data, .len = 4 };

	dprintk(4, "dev: %p\n", dev);

	if (1 != i2c_transfer(&av7110->i2c_adap, &msg, 1))
		return -1;
	return 0;
}

static int ves1820_set_tv_freq(struct saa7146_dev *dev, u32 freq)
{
	u32 div;
	u8 config;
	u8 buf[4];

	dprintk(4, "freq: 0x%08x\n", freq);

	/* magic number: 614. tuning with the frequency given by v4l2
	   is always off by 614*62.5 = 38375 kHz...*/
	div = freq + 614;

	buf[0] = (div >> 8) & 0x7f;
	buf[1] = div & 0xff;
	buf[2] = 0x8e;

	if (freq < (u32) (16 * 168.25))
		config = 0xa0;
	else if (freq < (u32) (16 * 447.25))
		config = 0x90;
	else
		config = 0x30;
	config &= ~0x02;

	buf[3] = config;

	return tuner_write(dev, 0x61, buf);
}

static int stv0297_set_tv_freq(struct saa7146_dev *dev, u32 freq)
{
	struct av7110 *av7110 = (struct av7110*)dev->ext_priv;
	u32 div;
	u8 data[4];

	div = (freq + 38900000 + 31250) / 62500;

	data[0] = (div >> 8) & 0x7f;
	data[1] = div & 0xff;
	data[2] = 0xce;

	if (freq < 45000000)
		return -EINVAL;
	else if (freq < 137000000)
		data[3] = 0x01;
	else if (freq < 403000000)
		data[3] = 0x02;
	else if (freq < 860000000)
		data[3] = 0x04;
	else
		return -EINVAL;

	if (av7110->fe->ops.i2c_gate_ctrl)
		av7110->fe->ops.i2c_gate_ctrl(av7110->fe, 1);
	return tuner_write(dev, 0x63, data);
}



static struct saa7146_standard analog_standard[];
static struct saa7146_standard dvb_standard[];
static struct saa7146_standard standard[];

static const struct v4l2_audio msp3400_v4l2_audio = {
	.index = 0,
	.name = "Television",
	.capability = V4L2_AUDCAP_STEREO
};

static int av7110_dvb_c_switch(struct saa7146_fh *fh)
{
	struct saa7146_dev *dev = fh->dev;
	struct saa7146_vv *vv = dev->vv_data;
	struct av7110 *av7110 = (struct av7110*)dev->ext_priv;
	u16 adswitch;
	int source, sync, err;

	dprintk(4, "%p\n", av7110);

	if ((vv->video_status & STATUS_OVERLAY) != 0) {
		vv->ov_suspend = vv->video_fh;
		err = saa7146_stop_preview(vv->video_fh); /* side effect: video_status is now 0, video_fh is NULL */
		if (err != 0) {
			dprintk(2, "suspending video failed\n");
			vv->ov_suspend = NULL;
		}
	}

	if (0 != av7110->current_input) {
		dprintk(1, "switching to analog TV:\n");
		adswitch = 1;
		source = SAA7146_HPS_SOURCE_PORT_B;
		sync = SAA7146_HPS_SYNC_PORT_B;
		memcpy(standard, analog_standard, sizeof(struct saa7146_standard) * 2);

		switch (av7110->current_input) {
		case 1:
			dprintk(1, "switching SAA7113 to Analog Tuner Input\n");
			msp_writereg(av7110, MSP_WR_DSP, 0x0008, 0x0000); // loudspeaker source
			msp_writereg(av7110, MSP_WR_DSP, 0x0009, 0x0000); // headphone source
			msp_writereg(av7110, MSP_WR_DSP, 0x000a, 0x0000); // SCART 1 source
			msp_writereg(av7110, MSP_WR_DSP, 0x000e, 0x3000); // FM matrix, mono
			msp_writereg(av7110, MSP_WR_DSP, 0x0000, 0x4f00); // loudspeaker + headphone
			msp_writereg(av7110, MSP_WR_DSP, 0x0007, 0x4f00); // SCART 1 volume

			if (av7110->analog_tuner_flags & ANALOG_TUNER_VES1820) {
				if (ves1820_writereg(dev, 0x09, 0x0f, 0x60))
					dprintk(1, "setting band in demodulator failed\n");
			} else if (av7110->analog_tuner_flags & ANALOG_TUNER_STV0297) {
				saa7146_setgpio(dev, 1, SAA7146_GPIO_OUTHI); // TDA9819 pin9(STD)
				saa7146_setgpio(dev, 3, SAA7146_GPIO_OUTHI); // TDA9819 pin30(VIF)
			}
			if (i2c_writereg(av7110, 0x48, 0x02, 0xd0) != 1)
				dprintk(1, "saa7113 write failed @ card %d", av7110->dvb_adapter.num);
			break;
		case 2:
			dprintk(1, "switching SAA7113 to Video AV CVBS Input\n");
			if (i2c_writereg(av7110, 0x48, 0x02, 0xd2) != 1)
				dprintk(1, "saa7113 write failed @ card %d", av7110->dvb_adapter.num);
			break;
		case 3:
			dprintk(1, "switching SAA7113 to Video AV Y/C Input\n");
			if (i2c_writereg(av7110, 0x48, 0x02, 0xd9) != 1)
				dprintk(1, "saa7113 write failed @ card %d", av7110->dvb_adapter.num);
			break;
		default:
			dprintk(1, "switching SAA7113 to Input: AV7110: SAA7113: invalid input\n");
		}
	} else {
		adswitch = 0;
		source = SAA7146_HPS_SOURCE_PORT_A;
		sync = SAA7146_HPS_SYNC_PORT_A;
		memcpy(standard, dvb_standard, sizeof(struct saa7146_standard) * 2);
		dprintk(1, "switching DVB mode\n");
		msp_writereg(av7110, MSP_WR_DSP, 0x0008, 0x0220); // loudspeaker source
		msp_writereg(av7110, MSP_WR_DSP, 0x0009, 0x0220); // headphone source
		msp_writereg(av7110, MSP_WR_DSP, 0x000a, 0x0220); // SCART 1 source
		msp_writereg(av7110, MSP_WR_DSP, 0x000e, 0x3000); // FM matrix, mono
		msp_writereg(av7110, MSP_WR_DSP, 0x0000, 0x7f00); // loudspeaker + headphone
		msp_writereg(av7110, MSP_WR_DSP, 0x0007, 0x7f00); // SCART 1 volume

		if (av7110->analog_tuner_flags & ANALOG_TUNER_VES1820) {
			if (ves1820_writereg(dev, 0x09, 0x0f, 0x20))
				dprintk(1, "setting band in demodulator failed\n");
		} else if (av7110->analog_tuner_flags & ANALOG_TUNER_STV0297) {
			saa7146_setgpio(dev, 1, SAA7146_GPIO_OUTLO); // TDA9819 pin9(STD)
			saa7146_setgpio(dev, 3, SAA7146_GPIO_OUTLO); // TDA9819 pin30(VIF)
		}
	}

	/* hmm, this does not do anything!? */
	if (av7110_fw_cmd(av7110, COMTYPE_AUDIODAC, ADSwitch, 1, adswitch))
		dprintk(1, "ADSwitch error\n");

	saa7146_set_hps_source_and_sync(dev, source, sync);

	if (vv->ov_suspend != NULL) {
		saa7146_start_preview(vv->ov_suspend);
		vv->ov_suspend = NULL;
	}

	return 0;
}

static int vidioc_g_tuner(struct file *file, void *fh, struct v4l2_tuner *t)
{
	struct saa7146_dev *dev = ((struct saa7146_fh *)fh)->dev;
	struct av7110 *av7110 = (struct av7110 *)dev->ext_priv;
	u16 stereo_det;
	s8 stereo;

	dprintk(2, "VIDIOC_G_TUNER: %d\n", t->index);

	if (!av7110->analog_tuner_flags || t->index != 0)
		return -EINVAL;

	memset(t, 0, sizeof(*t));
	strscpy((char *)t->name, "Television", sizeof(t->name));

	t->type = V4L2_TUNER_ANALOG_TV;
	t->capability = V4L2_TUNER_CAP_NORM | V4L2_TUNER_CAP_STEREO |
		V4L2_TUNER_CAP_LANG1 | V4L2_TUNER_CAP_LANG2 | V4L2_TUNER_CAP_SAP;
	t->rangelow = 772;	/* 48.25 MHZ / 62.5 kHz = 772, see fi1216mk2-specs, page 2 */
	t->rangehigh = 13684;	/* 855.25 MHz / 62.5 kHz = 13684 */
	/* FIXME: add the real signal strength here */
	t->signal = 0xffff;
	t->afc = 0;

	/* FIXME: standard / stereo detection is still broken */
	msp_readreg(av7110, MSP_RD_DEM, 0x007e, &stereo_det);
	dprintk(1, "VIDIOC_G_TUNER: msp3400 TV standard detection: 0x%04x\n", stereo_det);
	msp_readreg(av7110, MSP_RD_DSP, 0x0018, &stereo_det);
	dprintk(1, "VIDIOC_G_TUNER: msp3400 stereo detection: 0x%04x\n", stereo_det);
	stereo = (s8)(stereo_det >> 8);
	if (stereo > 0x10) {
		/* stereo */
		t->rxsubchans = V4L2_TUNER_SUB_STEREO | V4L2_TUNER_SUB_MONO;
		t->audmode = V4L2_TUNER_MODE_STEREO;
	} else if (stereo < -0x10) {
		/* bilingual */
		t->rxsubchans = V4L2_TUNER_SUB_LANG1 | V4L2_TUNER_SUB_LANG2;
		t->audmode = V4L2_TUNER_MODE_LANG1;
	} else /* mono */
		t->rxsubchans = V4L2_TUNER_SUB_MONO;

	return 0;
}

static int vidioc_s_tuner(struct file *file, void *fh, const struct v4l2_tuner *t)
{
	struct saa7146_dev *dev = ((struct saa7146_fh *)fh)->dev;
	struct av7110 *av7110 = (struct av7110 *)dev->ext_priv;
	u16 fm_matrix, src;
	dprintk(2, "VIDIOC_S_TUNER: %d\n", t->index);

	if (!av7110->analog_tuner_flags || av7110->current_input != 1)
		return -EINVAL;

	switch (t->audmode) {
	case V4L2_TUNER_MODE_STEREO:
		dprintk(2, "VIDIOC_S_TUNER: V4L2_TUNER_MODE_STEREO\n");
		fm_matrix = 0x3001; /* stereo */
		src = 0x0020;
		break;
	case V4L2_TUNER_MODE_LANG1_LANG2:
		dprintk(2, "VIDIOC_S_TUNER: V4L2_TUNER_MODE_LANG1_LANG2\n");
		fm_matrix = 0x3000; /* bilingual */
		src = 0x0020;
		break;
	case V4L2_TUNER_MODE_LANG1:
		dprintk(2, "VIDIOC_S_TUNER: V4L2_TUNER_MODE_LANG1\n");
		fm_matrix = 0x3000; /* mono */
		src = 0x0000;
		break;
	case V4L2_TUNER_MODE_LANG2:
		dprintk(2, "VIDIOC_S_TUNER: V4L2_TUNER_MODE_LANG2\n");
		fm_matrix = 0x3000; /* mono */
		src = 0x0010;
		break;
	default: /* case V4L2_TUNER_MODE_MONO: */
		dprintk(2, "VIDIOC_S_TUNER: TDA9840_SET_MONO\n");
		fm_matrix = 0x3000; /* mono */
		src = 0x0030;
		break;
	}
	msp_writereg(av7110, MSP_WR_DSP, 0x000e, fm_matrix);
	msp_writereg(av7110, MSP_WR_DSP, 0x0008, src);
	msp_writereg(av7110, MSP_WR_DSP, 0x0009, src);
	msp_writereg(av7110, MSP_WR_DSP, 0x000a, src);
	return 0;
}

static int vidioc_g_frequency(struct file *file, void *fh, struct v4l2_frequency *f)
{
	struct saa7146_dev *dev = ((struct saa7146_fh *)fh)->dev;
	struct av7110 *av7110 = (struct av7110 *)dev->ext_priv;

	dprintk(2, "VIDIOC_G_FREQ: freq:0x%08x\n", f->frequency);

	if (!av7110->analog_tuner_flags || av7110->current_input != 1)
		return -EINVAL;

	memset(f, 0, sizeof(*f));
	f->type = V4L2_TUNER_ANALOG_TV;
	f->frequency =	av7110->current_freq;
	return 0;
}

static int vidioc_s_frequency(struct file *file, void *fh, const struct v4l2_frequency *f)
{
	struct saa7146_dev *dev = ((struct saa7146_fh *)fh)->dev;
	struct av7110 *av7110 = (struct av7110 *)dev->ext_priv;

	dprintk(2, "VIDIOC_S_FREQUENCY: freq:0x%08x\n", f->frequency);

	if (!av7110->analog_tuner_flags || av7110->current_input != 1)
		return -EINVAL;

	if (V4L2_TUNER_ANALOG_TV != f->type)
		return -EINVAL;

	msp_writereg(av7110, MSP_WR_DSP, 0x0000, 0xffe0); /* fast mute */
	msp_writereg(av7110, MSP_WR_DSP, 0x0007, 0xffe0);

	/* tune in desired frequency */
	if (av7110->analog_tuner_flags & ANALOG_TUNER_VES1820)
		ves1820_set_tv_freq(dev, f->frequency);
	else if (av7110->analog_tuner_flags & ANALOG_TUNER_STV0297)
		stv0297_set_tv_freq(dev, f->frequency);
	av7110->current_freq = f->frequency;

	msp_writereg(av7110, MSP_WR_DSP, 0x0015, 0x003f); /* start stereo detection */
	msp_writereg(av7110, MSP_WR_DSP, 0x0015, 0x0000);
	msp_writereg(av7110, MSP_WR_DSP, 0x0000, 0x4f00); /* loudspeaker + headphone */
	msp_writereg(av7110, MSP_WR_DSP, 0x0007, 0x4f00); /* SCART 1 volume */
	return 0;
}

static int vidioc_enum_input(struct file *file, void *fh, struct v4l2_input *i)
{
	struct saa7146_dev *dev = ((struct saa7146_fh *)fh)->dev;
	struct av7110 *av7110 = (struct av7110 *)dev->ext_priv;

	dprintk(2, "VIDIOC_ENUMINPUT: %d\n", i->index);

	if (av7110->analog_tuner_flags) {
		if (i->index >= 4)
			return -EINVAL;
	} else {
		if (i->index != 0)
			return -EINVAL;
	}

	memcpy(i, &inputs[i->index], sizeof(struct v4l2_input));

	return 0;
}

static int vidioc_g_input(struct file *file, void *fh, unsigned int *input)
{
	struct saa7146_dev *dev = ((struct saa7146_fh *)fh)->dev;
	struct av7110 *av7110 = (struct av7110 *)dev->ext_priv;

	*input = av7110->current_input;
	dprintk(2, "VIDIOC_G_INPUT: %d\n", *input);
	return 0;
}

static int vidioc_s_input(struct file *file, void *fh, unsigned int input)
{
	struct saa7146_dev *dev = ((struct saa7146_fh *)fh)->dev;
	struct av7110 *av7110 = (struct av7110 *)dev->ext_priv;

	dprintk(2, "VIDIOC_S_INPUT: %d\n", input);

	if (!av7110->analog_tuner_flags)
		return input ? -EINVAL : 0;

	if (input >= 4)
		return -EINVAL;

	av7110->current_input = input;
	return av7110_dvb_c_switch(fh);
}

static int vidioc_enumaudio(struct file *file, void *fh, struct v4l2_audio *a)
{
	dprintk(2, "VIDIOC_G_AUDIO: %d\n", a->index);
	if (a->index != 0)
		return -EINVAL;
	*a = msp3400_v4l2_audio;
	return 0;
}

static int vidioc_g_audio(struct file *file, void *fh, struct v4l2_audio *a)
{
	struct saa7146_dev *dev = ((struct saa7146_fh *)fh)->dev;
	struct av7110 *av7110 = (struct av7110 *)dev->ext_priv;

	dprintk(2, "VIDIOC_G_AUDIO: %d\n", a->index);
	if (a->index != 0)
		return -EINVAL;
	if (av7110->current_input >= 2)
		return -EINVAL;
	*a = msp3400_v4l2_audio;
	return 0;
}

static int vidioc_s_audio(struct file *file, void *fh, const struct v4l2_audio *a)
{
	struct saa7146_dev *dev = ((struct saa7146_fh *)fh)->dev;
	struct av7110 *av7110 = (struct av7110 *)dev->ext_priv;

	dprintk(2, "VIDIOC_S_AUDIO: %d\n", a->index);
	if (av7110->current_input >= 2)
		return -EINVAL;
	return a->index ? -EINVAL : 0;
}

static int vidioc_g_sliced_vbi_cap(struct file *file, void *fh,
					struct v4l2_sliced_vbi_cap *cap)
{
	struct saa7146_dev *dev = ((struct saa7146_fh *)fh)->dev;
	struct av7110 *av7110 = (struct av7110 *)dev->ext_priv;

	dprintk(2, "VIDIOC_G_SLICED_VBI_CAP\n");
	if (cap->type != V4L2_BUF_TYPE_SLICED_VBI_OUTPUT)
		return -EINVAL;
	if (FW_VERSION(av7110->arm_app) >= 0x2623) {
		cap->service_set = V4L2_SLICED_WSS_625;
		cap->service_lines[0][23] = V4L2_SLICED_WSS_625;
	}
	return 0;
}

static int vidioc_g_fmt_sliced_vbi_out(struct file *file, void *fh,
					struct v4l2_format *f)
{
	struct saa7146_dev *dev = ((struct saa7146_fh *)fh)->dev;
	struct av7110 *av7110 = (struct av7110 *)dev->ext_priv;

	dprintk(2, "VIDIOC_G_FMT:\n");
	if (FW_VERSION(av7110->arm_app) < 0x2623)
		return -EINVAL;
	memset(&f->fmt.sliced, 0, sizeof f->fmt.sliced);
	if (av7110->wssMode) {
		f->fmt.sliced.service_set = V4L2_SLICED_WSS_625;
		f->fmt.sliced.service_lines[0][23] = V4L2_SLICED_WSS_625;
		f->fmt.sliced.io_size = sizeof(struct v4l2_sliced_vbi_data);
	}
	return 0;
}

static int vidioc_s_fmt_sliced_vbi_out(struct file *file, void *fh,
					struct v4l2_format *f)
{
	struct saa7146_dev *dev = ((struct saa7146_fh *)fh)->dev;
	struct av7110 *av7110 = (struct av7110 *)dev->ext_priv;

	dprintk(2, "VIDIOC_S_FMT\n");
	if (FW_VERSION(av7110->arm_app) < 0x2623)
		return -EINVAL;
	if (f->fmt.sliced.service_set != V4L2_SLICED_WSS_625 &&
	    f->fmt.sliced.service_lines[0][23] != V4L2_SLICED_WSS_625) {
		memset(&f->fmt.sliced, 0, sizeof(f->fmt.sliced));
		/* WSS controlled by firmware */
		av7110->wssMode = 0;
		av7110->wssData = 0;
		return av7110_fw_cmd(av7110, COMTYPE_ENCODER,
				     SetWSSConfig, 1, 0);
	} else {
		memset(&f->fmt.sliced, 0, sizeof(f->fmt.sliced));
		f->fmt.sliced.service_set = V4L2_SLICED_WSS_625;
		f->fmt.sliced.service_lines[0][23] = V4L2_SLICED_WSS_625;
		f->fmt.sliced.io_size = sizeof(struct v4l2_sliced_vbi_data);
		/* WSS controlled by userspace */
		av7110->wssMode = 1;
		av7110->wssData = 0;
	}
	return 0;
}

static int av7110_vbi_reset(struct file *file)
{
	struct saa7146_fh *fh = file->private_data;
	struct saa7146_dev *dev = fh->dev;
	struct av7110 *av7110 = (struct av7110*) dev->ext_priv;

	dprintk(2, "%s\n", __func__);
	av7110->wssMode = 0;
	av7110->wssData = 0;
	if (FW_VERSION(av7110->arm_app) < 0x2623)
		return 0;
	else
		return av7110_fw_cmd(av7110, COMTYPE_ENCODER, SetWSSConfig, 1, 0);
}

static ssize_t av7110_vbi_write(struct file *file, const char __user *data, size_t count, loff_t *ppos)
{
	struct saa7146_fh *fh = file->private_data;
	struct saa7146_dev *dev = fh->dev;
	struct av7110 *av7110 = (struct av7110*) dev->ext_priv;
	struct v4l2_sliced_vbi_data d;
	int rc;

	dprintk(2, "%s\n", __func__);
	if (FW_VERSION(av7110->arm_app) < 0x2623 || !av7110->wssMode || count != sizeof d)
		return -EINVAL;
	if (copy_from_user(&d, data, count))
		return -EFAULT;
	if ((d.id != 0 && d.id != V4L2_SLICED_WSS_625) || d.field != 0 || d.line != 23)
		return -EINVAL;
	if (d.id)
		av7110->wssData = ((d.data[1] << 8) & 0x3f00) | d.data[0];
	else
		av7110->wssData = 0x8000;
	rc = av7110_fw_cmd(av7110, COMTYPE_ENCODER, SetWSSConfig, 2, 1, av7110->wssData);
	return (rc < 0) ? rc : count;
}

/****************************************************************************
 * INITIALIZATION
 ****************************************************************************/

static u8 saa7113_init_regs[] = {
	0x02, 0xd0,
	0x03, 0x23,
	0x04, 0x00,
	0x05, 0x00,
	0x06, 0xe9,
	0x07, 0x0d,
	0x08, 0x98,
	0x09, 0x02,
	0x0a, 0x80,
	0x0b, 0x40,
	0x0c, 0x40,
	0x0d, 0x00,
	0x0e, 0x01,
	0x0f, 0x7c,
	0x10, 0x48,
	0x11, 0x0c,
	0x12, 0x8b,
	0x13, 0x1a,
	0x14, 0x00,
	0x15, 0x00,
	0x16, 0x00,
	0x17, 0x00,
	0x18, 0x00,
	0x19, 0x00,
	0x1a, 0x00,
	0x1b, 0x00,
	0x1c, 0x00,
	0x1d, 0x00,
	0x1e, 0x00,

	0x41, 0x77,
	0x42, 0x77,
	0x43, 0x77,
	0x44, 0x77,
	0x45, 0x77,
	0x46, 0x77,
	0x47, 0x77,
	0x48, 0x77,
	0x49, 0x77,
	0x4a, 0x77,
	0x4b, 0x77,
	0x4c, 0x77,
	0x4d, 0x77,
	0x4e, 0x77,
	0x4f, 0x77,
	0x50, 0x77,
	0x51, 0x77,
	0x52, 0x77,
	0x53, 0x77,
	0x54, 0x77,
	0x55, 0x77,
	0x56, 0x77,
	0x57, 0xff,

	0xff
};


static struct saa7146_ext_vv av7110_vv_data_st;
static struct saa7146_ext_vv av7110_vv_data_c;

int av7110_init_analog_module(struct av7110 *av7110)
{
	u16 version1, version2;

	if (i2c_writereg(av7110, 0x80, 0x0, 0x80) == 1 &&
	    i2c_writereg(av7110, 0x80, 0x0, 0) == 1) {
		pr_info("DVB-C analog module @ card %d detected, initializing MSP3400\n",
			av7110->dvb_adapter.num);
		av7110->adac_type = DVB_ADAC_MSP34x0;
	} else if (i2c_writereg(av7110, 0x84, 0x0, 0x80) == 1 &&
		   i2c_writereg(av7110, 0x84, 0x0, 0) == 1) {
		pr_info("DVB-C analog module @ card %d detected, initializing MSP3415\n",
			av7110->dvb_adapter.num);
		av7110->adac_type = DVB_ADAC_MSP34x5;
	} else
		return -ENODEV;

	msleep(100); // the probing above resets the msp...
	msp_readreg(av7110, MSP_RD_DSP, 0x001e, &version1);
	msp_readreg(av7110, MSP_RD_DSP, 0x001f, &version2);
	dprintk(1, "dvb-ttpci: @ card %d MSP34xx version 0x%04x 0x%04x\n",
		av7110->dvb_adapter.num, version1, version2);
	msp_writereg(av7110, MSP_WR_DSP, 0x0013, 0x0c00);
	msp_writereg(av7110, MSP_WR_DSP, 0x0000, 0x7f00); // loudspeaker + headphone
	msp_writereg(av7110, MSP_WR_DSP, 0x0008, 0x0220); // loudspeaker source
	msp_writereg(av7110, MSP_WR_DSP, 0x0009, 0x0220); // headphone source
	msp_writereg(av7110, MSP_WR_DSP, 0x0004, 0x7f00); // loudspeaker volume
	msp_writereg(av7110, MSP_WR_DSP, 0x000a, 0x0220); // SCART 1 source
	msp_writereg(av7110, MSP_WR_DSP, 0x0007, 0x7f00); // SCART 1 volume
	msp_writereg(av7110, MSP_WR_DSP, 0x000d, 0x1900); // prescale SCART

	if (i2c_writereg(av7110, 0x48, 0x01, 0x00)!=1) {
		pr_info("saa7113 not accessible\n");
	} else {
		u8 *i = saa7113_init_regs;

		if ((av7110->dev->pci->subsystem_vendor == 0x110a) && (av7110->dev->pci->subsystem_device == 0x0000)) {
			/* Fujitsu/Siemens DVB-Cable */
			av7110->analog_tuner_flags |= ANALOG_TUNER_VES1820;
		} else if ((av7110->dev->pci->subsystem_vendor == 0x13c2) && (av7110->dev->pci->subsystem_device == 0x0002)) {
			/* Hauppauge/TT DVB-C premium */
			av7110->analog_tuner_flags |= ANALOG_TUNER_VES1820;
		} else if ((av7110->dev->pci->subsystem_vendor == 0x13c2) && (av7110->dev->pci->subsystem_device == 0x000A)) {
			/* Hauppauge/TT DVB-C premium */
			av7110->analog_tuner_flags |= ANALOG_TUNER_STV0297;
		}

		/* setup for DVB by default */
		if (av7110->analog_tuner_flags & ANALOG_TUNER_VES1820) {
			if (ves1820_writereg(av7110->dev, 0x09, 0x0f, 0x20))
				dprintk(1, "setting band in demodulator failed\n");
		} else if (av7110->analog_tuner_flags & ANALOG_TUNER_STV0297) {
			saa7146_setgpio(av7110->dev, 1, SAA7146_GPIO_OUTLO); // TDA9819 pin9(STD)
			saa7146_setgpio(av7110->dev, 3, SAA7146_GPIO_OUTLO); // TDA9819 pin30(VIF)
		}

		/* init the saa7113 */
		while (*i != 0xff) {
			if (i2c_writereg(av7110, 0x48, i[0], i[1]) != 1) {
				dprintk(1, "saa7113 initialization failed @ card %d", av7110->dvb_adapter.num);
				break;
			}
			i += 2;
		}
		/* setup msp for analog sound: B/G Dual-FM */
		msp_writereg(av7110, MSP_WR_DEM, 0x00bb, 0x02d0); // AD_CV
		msp_writereg(av7110, MSP_WR_DEM, 0x0001,  3); // FIR1
		msp_writereg(av7110, MSP_WR_DEM, 0x0001, 18); // FIR1
		msp_writereg(av7110, MSP_WR_DEM, 0x0001, 27); // FIR1
		msp_writereg(av7110, MSP_WR_DEM, 0x0001, 48); // FIR1
		msp_writereg(av7110, MSP_WR_DEM, 0x0001, 66); // FIR1
		msp_writereg(av7110, MSP_WR_DEM, 0x0001, 72); // FIR1
		msp_writereg(av7110, MSP_WR_DEM, 0x0005,  4); // FIR2
		msp_writereg(av7110, MSP_WR_DEM, 0x0005, 64); // FIR2
		msp_writereg(av7110, MSP_WR_DEM, 0x0005,  0); // FIR2
		msp_writereg(av7110, MSP_WR_DEM, 0x0005,  3); // FIR2
		msp_writereg(av7110, MSP_WR_DEM, 0x0005, 18); // FIR2
		msp_writereg(av7110, MSP_WR_DEM, 0x0005, 27); // FIR2
		msp_writereg(av7110, MSP_WR_DEM, 0x0005, 48); // FIR2
		msp_writereg(av7110, MSP_WR_DEM, 0x0005, 66); // FIR2
		msp_writereg(av7110, MSP_WR_DEM, 0x0005, 72); // FIR2
		msp_writereg(av7110, MSP_WR_DEM, 0x0083, 0xa000); // MODE_REG
		msp_writereg(av7110, MSP_WR_DEM, 0x0093, 0x00aa); // DCO1_LO 5.74MHz
		msp_writereg(av7110, MSP_WR_DEM, 0x009b, 0x04fc); // DCO1_HI
		msp_writereg(av7110, MSP_WR_DEM, 0x00a3, 0x038e); // DCO2_LO 5.5MHz
		msp_writereg(av7110, MSP_WR_DEM, 0x00ab, 0x04c6); // DCO2_HI
		msp_writereg(av7110, MSP_WR_DEM, 0x0056, 0); // LOAD_REG 1/2
	}

	memcpy(standard, dvb_standard, sizeof(struct saa7146_standard) * 2);
	/* set dd1 stream a & b */
	saa7146_write(av7110->dev, DD1_STREAM_B, 0x00000000);
	saa7146_write(av7110->dev, DD1_INIT, 0x03000700);
	saa7146_write(av7110->dev, MC2, (MASK_09 | MASK_25 | MASK_10 | MASK_26));

	return 0;
}

int av7110_init_v4l(struct av7110 *av7110)
{
	struct saa7146_dev* dev = av7110->dev;
	struct saa7146_ext_vv *vv_data;
	int ret;

	/* special case DVB-C: these cards have an analog tuner
	   plus need some special handling, so we have separate
	   saa7146_ext_vv data for these... */
	if (av7110->analog_tuner_flags)
		vv_data = &av7110_vv_data_c;
	else
		vv_data = &av7110_vv_data_st;
	ret = saa7146_vv_init(dev, vv_data);

	if (ret) {
		ERR("cannot init capture device. skipping\n");
		return -ENODEV;
	}
	vv_data->vid_ops.vidioc_enum_input = vidioc_enum_input;
	vv_data->vid_ops.vidioc_g_input = vidioc_g_input;
	vv_data->vid_ops.vidioc_s_input = vidioc_s_input;
	vv_data->vid_ops.vidioc_g_tuner = vidioc_g_tuner;
	vv_data->vid_ops.vidioc_s_tuner = vidioc_s_tuner;
	vv_data->vid_ops.vidioc_g_frequency = vidioc_g_frequency;
	vv_data->vid_ops.vidioc_s_frequency = vidioc_s_frequency;
	vv_data->vid_ops.vidioc_enumaudio = vidioc_enumaudio;
	vv_data->vid_ops.vidioc_g_audio = vidioc_g_audio;
	vv_data->vid_ops.vidioc_s_audio = vidioc_s_audio;
	vv_data->vid_ops.vidioc_g_fmt_vbi_cap = NULL;

	vv_data->vbi_ops.vidioc_g_tuner = vidioc_g_tuner;
	vv_data->vbi_ops.vidioc_s_tuner = vidioc_s_tuner;
	vv_data->vbi_ops.vidioc_g_frequency = vidioc_g_frequency;
	vv_data->vbi_ops.vidioc_s_frequency = vidioc_s_frequency;
	vv_data->vbi_ops.vidioc_g_fmt_vbi_cap = NULL;
	vv_data->vbi_ops.vidioc_g_sliced_vbi_cap = vidioc_g_sliced_vbi_cap;
	vv_data->vbi_ops.vidioc_g_fmt_sliced_vbi_out = vidioc_g_fmt_sliced_vbi_out;
	vv_data->vbi_ops.vidioc_s_fmt_sliced_vbi_out = vidioc_s_fmt_sliced_vbi_out;

	if (FW_VERSION(av7110->arm_app) < 0x2623)
		vv_data->capabilities &= ~V4L2_CAP_SLICED_VBI_OUTPUT;

	if (saa7146_register_device(&av7110->v4l_dev, dev, "av7110", VFL_TYPE_GRABBER)) {
		ERR("cannot register capture device. skipping\n");
		saa7146_vv_release(dev);
		return -ENODEV;
	}
	if (FW_VERSION(av7110->arm_app) >= 0x2623) {
		if (saa7146_register_device(&av7110->vbi_dev, dev, "av7110", VFL_TYPE_VBI))
			ERR("cannot register vbi v4l2 device. skipping\n");
	}
	return 0;
}

int av7110_exit_v4l(struct av7110 *av7110)
{
	struct saa7146_dev* dev = av7110->dev;

	saa7146_unregister_device(&av7110->v4l_dev, av7110->dev);
	saa7146_unregister_device(&av7110->vbi_dev, av7110->dev);

	saa7146_vv_release(dev);

	return 0;
}



/* FIXME: these values are experimental values that look better than the
   values from the latest "official" driver -- at least for me... (MiHu) */
static struct saa7146_standard standard[] = {
	{
		.name	= "PAL",	.id		= V4L2_STD_PAL_BG,
		.v_offset	= 0x15,	.v_field	= 288,
		.h_offset	= 0x48,	.h_pixels	= 708,
		.v_max_out	= 576,	.h_max_out	= 768,
	}, {
		.name	= "NTSC",	.id		= V4L2_STD_NTSC,
		.v_offset	= 0x10,	.v_field	= 244,
		.h_offset	= 0x40,	.h_pixels	= 708,
		.v_max_out	= 480,	.h_max_out	= 640,
	}
};

static struct saa7146_standard analog_standard[] = {
	{
		.name	= "PAL",	.id		= V4L2_STD_PAL_BG,
		.v_offset	= 0x1b,	.v_field	= 288,
		.h_offset	= 0x08,	.h_pixels	= 708,
		.v_max_out	= 576,	.h_max_out	= 768,
	}, {
		.name	= "NTSC",	.id		= V4L2_STD_NTSC,
		.v_offset	= 0x10,	.v_field	= 244,
		.h_offset	= 0x40,	.h_pixels	= 708,
		.v_max_out	= 480,	.h_max_out	= 640,
	}
};

static struct saa7146_standard dvb_standard[] = {
	{
		.name	= "PAL",	.id		= V4L2_STD_PAL_BG,
		.v_offset	= 0x14,	.v_field	= 288,
		.h_offset	= 0x48,	.h_pixels	= 708,
		.v_max_out	= 576,	.h_max_out	= 768,
	}, {
		.name	= "NTSC",	.id		= V4L2_STD_NTSC,
		.v_offset	= 0x10,	.v_field	= 244,
		.h_offset	= 0x40,	.h_pixels	= 708,
		.v_max_out	= 480,	.h_max_out	= 640,
	}
};

static int std_callback(struct saa7146_dev* dev, struct saa7146_standard *std)
{
	struct av7110 *av7110 = (struct av7110*) dev->ext_priv;

	if (std->id & V4L2_STD_PAL) {
		av7110->vidmode = AV7110_VIDEO_MODE_PAL;
		av7110_set_vidmode(av7110, av7110->vidmode);
	}
	else if (std->id & V4L2_STD_NTSC) {
		av7110->vidmode = AV7110_VIDEO_MODE_NTSC;
		av7110_set_vidmode(av7110, av7110->vidmode);
	}
	else
		return -1;

	return 0;
}


static struct saa7146_ext_vv av7110_vv_data_st = {
	.inputs		= 1,
	.audios		= 1,
	.capabilities	= V4L2_CAP_SLICED_VBI_OUTPUT | V4L2_CAP_AUDIO,
	.flags		= 0,

	.stds		= &standard[0],
	.num_stds	= ARRAY_SIZE(standard),
	.std_callback	= &std_callback,

	.vbi_fops.open	= av7110_vbi_reset,
	.vbi_fops.release = av7110_vbi_reset,
	.vbi_fops.write	= av7110_vbi_write,
};

static struct saa7146_ext_vv av7110_vv_data_c = {
	.inputs		= 1,
	.audios		= 1,
	.capabilities	= V4L2_CAP_TUNER | V4L2_CAP_SLICED_VBI_OUTPUT | V4L2_CAP_AUDIO,
	.flags		= SAA7146_USE_PORT_B_FOR_VBI,

	.stds		= &standard[0],
	.num_stds	= ARRAY_SIZE(standard),
	.std_callback	= &std_callback,

	.vbi_fops.open	= av7110_vbi_reset,
	.vbi_fops.release = av7110_vbi_reset,
	.vbi_fops.write	= av7110_vbi_write,
};


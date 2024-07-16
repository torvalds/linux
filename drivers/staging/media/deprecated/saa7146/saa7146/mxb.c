// SPDX-License-Identifier: GPL-2.0-or-later
/*
    mxb - v4l2 driver for the Multimedia eXtension Board

    Copyright (C) 1998-2006 Michael Hunold <michael@mihu.de>

    Visit http://www.themm.net/~mihu/linux/saa7146/mxb.html
    for further details about this card.

*/

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#define DEBUG_VARIABLE debug

#include <media/tuner.h>
#include <media/v4l2-common.h>
#include <media/i2c/saa7115.h>
#include <linux/module.h>
#include <linux/kernel.h>

#include "../common/saa7146_vv.h"
#include "tea6415c.h"
#include "tea6420.h"

#define MXB_AUDIOS	6

#define I2C_SAA7111A  0x24
#define	I2C_TDA9840   0x42
#define	I2C_TEA6415C  0x43
#define	I2C_TEA6420_1 0x4c
#define	I2C_TEA6420_2 0x4d
#define	I2C_TUNER     0x60

#define MXB_BOARD_CAN_DO_VBI(dev)   (dev->revision != 0)

/* global variable */
static int mxb_num;

/* initial frequence the tuner will be tuned to.
   in verden (lower saxony, germany) 4148 is a
   channel called "phoenix" */
static int freq = 4148;
module_param(freq, int, 0644);
MODULE_PARM_DESC(freq, "initial frequency the tuner will be tuned to while setup");

static int debug;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "Turn on/off device debugging (default:off).");

#define MXB_INPUTS 4
enum { TUNER, AUX1, AUX3, AUX3_YC };

static struct v4l2_input mxb_inputs[MXB_INPUTS] = {
	{ TUNER,   "Tuner",          V4L2_INPUT_TYPE_TUNER,  0x3f, 0,
		V4L2_STD_PAL_BG | V4L2_STD_PAL_I, 0, V4L2_IN_CAP_STD },
	{ AUX1,	   "AUX1",           V4L2_INPUT_TYPE_CAMERA, 0x3f, 0,
		V4L2_STD_ALL, 0, V4L2_IN_CAP_STD },
	{ AUX3,	   "AUX3 Composite", V4L2_INPUT_TYPE_CAMERA, 0x3f, 0,
		V4L2_STD_ALL, 0, V4L2_IN_CAP_STD },
	{ AUX3_YC, "AUX3 S-Video",   V4L2_INPUT_TYPE_CAMERA, 0x3f, 0,
		V4L2_STD_ALL, 0, V4L2_IN_CAP_STD },
};

/* this array holds the information, which port of the saa7146 each
   input actually uses. the mxb uses port 0 for every input */
static struct {
	int hps_source;
	int hps_sync;
} input_port_selection[MXB_INPUTS] = {
	{ SAA7146_HPS_SOURCE_PORT_A, SAA7146_HPS_SYNC_PORT_A },
	{ SAA7146_HPS_SOURCE_PORT_A, SAA7146_HPS_SYNC_PORT_A },
	{ SAA7146_HPS_SOURCE_PORT_A, SAA7146_HPS_SYNC_PORT_A },
	{ SAA7146_HPS_SOURCE_PORT_A, SAA7146_HPS_SYNC_PORT_A },
};

/* this array holds the information of the audio source (mxb_audios),
   which has to be switched corresponding to the video source (mxb_channels) */
static int video_audio_connect[MXB_INPUTS] =
	{ 0, 1, 3, 3 };

struct mxb_routing {
	u32 input;
	u32 output;
};

/* these are the available audio sources, which can switched
   to the line- and cd-output individually */
static struct v4l2_audio mxb_audios[MXB_AUDIOS] = {
	    {
		.index	= 0,
		.name	= "Tuner",
		.capability = V4L2_AUDCAP_STEREO,
	} , {
		.index	= 1,
		.name	= "AUX1",
		.capability = V4L2_AUDCAP_STEREO,
	} , {
		.index	= 2,
		.name	= "AUX2",
		.capability = V4L2_AUDCAP_STEREO,
	} , {
		.index	= 3,
		.name	= "AUX3",
		.capability = V4L2_AUDCAP_STEREO,
	} , {
		.index	= 4,
		.name	= "Radio (X9)",
		.capability = V4L2_AUDCAP_STEREO,
	} , {
		.index	= 5,
		.name	= "CD-ROM (X10)",
		.capability = V4L2_AUDCAP_STEREO,
	}
};

/* These are the necessary input-output-pins for bringing one audio source
   (see above) to the CD-output. Note that gain is set to 0 in this table. */
static struct mxb_routing TEA6420_cd[MXB_AUDIOS + 1][2] = {
	{ { 1, 1 }, { 1, 1 } },	/* Tuner */
	{ { 5, 1 }, { 6, 1 } },	/* AUX 1 */
	{ { 4, 1 }, { 6, 1 } },	/* AUX 2 */
	{ { 3, 1 }, { 6, 1 } },	/* AUX 3 */
	{ { 1, 1 }, { 3, 1 } },	/* Radio */
	{ { 1, 1 }, { 2, 1 } },	/* CD-Rom */
	{ { 6, 1 }, { 6, 1 } }	/* Mute */
};

/* These are the necessary input-output-pins for bringing one audio source
   (see above) to the line-output. Note that gain is set to 0 in this table. */
static struct mxb_routing TEA6420_line[MXB_AUDIOS + 1][2] = {
	{ { 2, 3 }, { 1, 2 } },
	{ { 5, 3 }, { 6, 2 } },
	{ { 4, 3 }, { 6, 2 } },
	{ { 3, 3 }, { 6, 2 } },
	{ { 2, 3 }, { 3, 2 } },
	{ { 2, 3 }, { 2, 2 } },
	{ { 6, 3 }, { 6, 2 } }	/* Mute */
};

struct mxb
{
	struct video_device	video_dev;
	struct video_device	vbi_dev;

	struct i2c_adapter	i2c_adapter;

	struct v4l2_subdev	*saa7111a;
	struct v4l2_subdev	*tda9840;
	struct v4l2_subdev	*tea6415c;
	struct v4l2_subdev	*tuner;
	struct v4l2_subdev	*tea6420_1;
	struct v4l2_subdev	*tea6420_2;

	int	cur_mode;	/* current audio mode (mono, stereo, ...) */
	int	cur_input;	/* current input */
	int	cur_audinput;	/* current audio input */
	int	cur_mute;	/* current mute status */
	struct v4l2_frequency	cur_freq;	/* current frequency the tuner is tuned to */
};

#define saa7111a_call(mxb, o, f, args...) \
	v4l2_subdev_call(mxb->saa7111a, o, f, ##args)
#define tda9840_call(mxb, o, f, args...) \
	v4l2_subdev_call(mxb->tda9840, o, f, ##args)
#define tea6415c_call(mxb, o, f, args...) \
	v4l2_subdev_call(mxb->tea6415c, o, f, ##args)
#define tuner_call(mxb, o, f, args...) \
	v4l2_subdev_call(mxb->tuner, o, f, ##args)
#define call_all(dev, o, f, args...) \
	v4l2_device_call_until_err(&dev->v4l2_dev, 0, o, f, ##args)

static void mxb_update_audmode(struct mxb *mxb)
{
	struct v4l2_tuner t = {
		.audmode = mxb->cur_mode,
	};

	tda9840_call(mxb, tuner, s_tuner, &t);
}

static inline void tea6420_route(struct mxb *mxb, int idx)
{
	v4l2_subdev_call(mxb->tea6420_1, audio, s_routing,
		TEA6420_cd[idx][0].input, TEA6420_cd[idx][0].output, 0);
	v4l2_subdev_call(mxb->tea6420_2, audio, s_routing,
		TEA6420_cd[idx][1].input, TEA6420_cd[idx][1].output, 0);
	v4l2_subdev_call(mxb->tea6420_1, audio, s_routing,
		TEA6420_line[idx][0].input, TEA6420_line[idx][0].output, 0);
	v4l2_subdev_call(mxb->tea6420_2, audio, s_routing,
		TEA6420_line[idx][1].input, TEA6420_line[idx][1].output, 0);
}

static struct saa7146_extension extension;

static int mxb_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct saa7146_dev *dev = container_of(ctrl->handler,
				struct saa7146_dev, ctrl_handler);
	struct mxb *mxb = dev->ext_priv;

	switch (ctrl->id) {
	case V4L2_CID_AUDIO_MUTE:
		mxb->cur_mute = ctrl->val;
		/* switch the audio-source */
		tea6420_route(mxb, ctrl->val ? 6 :
				video_audio_connect[mxb->cur_input]);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static const struct v4l2_ctrl_ops mxb_ctrl_ops = {
	.s_ctrl = mxb_s_ctrl,
};

static int mxb_probe(struct saa7146_dev *dev)
{
	struct v4l2_ctrl_handler *hdl = &dev->ctrl_handler;
	struct mxb *mxb = NULL;

	v4l2_ctrl_new_std(hdl, &mxb_ctrl_ops,
			V4L2_CID_AUDIO_MUTE, 0, 1, 1, 1);
	if (hdl->error)
		return hdl->error;
	mxb = kzalloc(sizeof(struct mxb), GFP_KERNEL);
	if (mxb == NULL) {
		DEB_D("not enough kernel memory\n");
		return -ENOMEM;
	}


	snprintf(mxb->i2c_adapter.name, sizeof(mxb->i2c_adapter.name), "mxb%d", mxb_num);

	saa7146_i2c_adapter_prepare(dev, &mxb->i2c_adapter, SAA7146_I2C_BUS_BIT_RATE_480);
	if (i2c_add_adapter(&mxb->i2c_adapter) < 0) {
		DEB_S("cannot register i2c-device. skipping.\n");
		kfree(mxb);
		return -EFAULT;
	}

	mxb->saa7111a = v4l2_i2c_new_subdev(&dev->v4l2_dev, &mxb->i2c_adapter,
			"saa7111", I2C_SAA7111A, NULL);
	mxb->tea6420_1 = v4l2_i2c_new_subdev(&dev->v4l2_dev, &mxb->i2c_adapter,
			"tea6420", I2C_TEA6420_1, NULL);
	mxb->tea6420_2 = v4l2_i2c_new_subdev(&dev->v4l2_dev, &mxb->i2c_adapter,
			"tea6420", I2C_TEA6420_2, NULL);
	mxb->tea6415c = v4l2_i2c_new_subdev(&dev->v4l2_dev, &mxb->i2c_adapter,
			"tea6415c", I2C_TEA6415C, NULL);
	mxb->tda9840 = v4l2_i2c_new_subdev(&dev->v4l2_dev, &mxb->i2c_adapter,
			"tda9840", I2C_TDA9840, NULL);
	mxb->tuner = v4l2_i2c_new_subdev(&dev->v4l2_dev, &mxb->i2c_adapter,
			"tuner", I2C_TUNER, NULL);

	/* check if all devices are present */
	if (!mxb->tea6420_1 || !mxb->tea6420_2 || !mxb->tea6415c ||
	    !mxb->tda9840 || !mxb->saa7111a || !mxb->tuner) {
		pr_err("did not find all i2c devices. aborting\n");
		i2c_del_adapter(&mxb->i2c_adapter);
		kfree(mxb);
		return -ENODEV;
	}

	/* all devices are present, probe was successful */

	/* we store the pointer in our private data field */
	dev->ext_priv = mxb;

	v4l2_ctrl_handler_setup(hdl);

	return 0;
}

/* some init data for the saa7740, the so-called 'sound arena module'.
   there are no specs available, so we simply use some init values */
static struct {
	int	length;
	char	data[9];
} mxb_saa7740_init[] = {
	{ 3, { 0x80, 0x00, 0x00 } },{ 3, { 0x80, 0x89, 0x00 } },
	{ 3, { 0x80, 0xb0, 0x0a } },{ 3, { 0x00, 0x00, 0x00 } },
	{ 3, { 0x49, 0x00, 0x00 } },{ 3, { 0x4a, 0x00, 0x00 } },
	{ 3, { 0x4b, 0x00, 0x00 } },{ 3, { 0x4c, 0x00, 0x00 } },
	{ 3, { 0x4d, 0x00, 0x00 } },{ 3, { 0x4e, 0x00, 0x00 } },
	{ 3, { 0x4f, 0x00, 0x00 } },{ 3, { 0x50, 0x00, 0x00 } },
	{ 3, { 0x51, 0x00, 0x00 } },{ 3, { 0x52, 0x00, 0x00 } },
	{ 3, { 0x53, 0x00, 0x00 } },{ 3, { 0x54, 0x00, 0x00 } },
	{ 3, { 0x55, 0x00, 0x00 } },{ 3, { 0x56, 0x00, 0x00 } },
	{ 3, { 0x57, 0x00, 0x00 } },{ 3, { 0x58, 0x00, 0x00 } },
	{ 3, { 0x59, 0x00, 0x00 } },{ 3, { 0x5a, 0x00, 0x00 } },
	{ 3, { 0x5b, 0x00, 0x00 } },{ 3, { 0x5c, 0x00, 0x00 } },
	{ 3, { 0x5d, 0x00, 0x00 } },{ 3, { 0x5e, 0x00, 0x00 } },
	{ 3, { 0x5f, 0x00, 0x00 } },{ 3, { 0x60, 0x00, 0x00 } },
	{ 3, { 0x61, 0x00, 0x00 } },{ 3, { 0x62, 0x00, 0x00 } },
	{ 3, { 0x63, 0x00, 0x00 } },{ 3, { 0x64, 0x00, 0x00 } },
	{ 3, { 0x65, 0x00, 0x00 } },{ 3, { 0x66, 0x00, 0x00 } },
	{ 3, { 0x67, 0x00, 0x00 } },{ 3, { 0x68, 0x00, 0x00 } },
	{ 3, { 0x69, 0x00, 0x00 } },{ 3, { 0x6a, 0x00, 0x00 } },
	{ 3, { 0x6b, 0x00, 0x00 } },{ 3, { 0x6c, 0x00, 0x00 } },
	{ 3, { 0x6d, 0x00, 0x00 } },{ 3, { 0x6e, 0x00, 0x00 } },
	{ 3, { 0x6f, 0x00, 0x00 } },{ 3, { 0x70, 0x00, 0x00 } },
	{ 3, { 0x71, 0x00, 0x00 } },{ 3, { 0x72, 0x00, 0x00 } },
	{ 3, { 0x73, 0x00, 0x00 } },{ 3, { 0x74, 0x00, 0x00 } },
	{ 3, { 0x75, 0x00, 0x00 } },{ 3, { 0x76, 0x00, 0x00 } },
	{ 3, { 0x77, 0x00, 0x00 } },{ 3, { 0x41, 0x00, 0x42 } },
	{ 3, { 0x42, 0x10, 0x42 } },{ 3, { 0x43, 0x20, 0x42 } },
	{ 3, { 0x44, 0x30, 0x42 } },{ 3, { 0x45, 0x00, 0x01 } },
	{ 3, { 0x46, 0x00, 0x01 } },{ 3, { 0x47, 0x00, 0x01 } },
	{ 3, { 0x48, 0x00, 0x01 } },
	{ 9, { 0x01, 0x03, 0xc5, 0x5c, 0x7a, 0x85, 0x01, 0x00, 0x54 } },
	{ 9, { 0x21, 0x03, 0xc5, 0x5c, 0x7a, 0x85, 0x01, 0x00, 0x54 } },
	{ 9, { 0x09, 0x0b, 0xb4, 0x6b, 0x74, 0x85, 0x95, 0x00, 0x34 } },
	{ 9, { 0x29, 0x0b, 0xb4, 0x6b, 0x74, 0x85, 0x95, 0x00, 0x34 } },
	{ 9, { 0x11, 0x17, 0x43, 0x62, 0x68, 0x89, 0xd1, 0xff, 0xb0 } },
	{ 9, { 0x31, 0x17, 0x43, 0x62, 0x68, 0x89, 0xd1, 0xff, 0xb0 } },
	{ 9, { 0x19, 0x20, 0x62, 0x51, 0x5a, 0x95, 0x19, 0x01, 0x50 } },
	{ 9, { 0x39, 0x20, 0x62, 0x51, 0x5a, 0x95, 0x19, 0x01, 0x50 } },
	{ 9, { 0x05, 0x3e, 0xd2, 0x69, 0x4e, 0x9a, 0x51, 0x00, 0xf0 } },
	{ 9, { 0x25, 0x3e, 0xd2, 0x69, 0x4e, 0x9a, 0x51, 0x00, 0xf0 } },
	{ 9, { 0x0d, 0x3d, 0xa1, 0x40, 0x7d, 0x9f, 0x29, 0xfe, 0x14 } },
	{ 9, { 0x2d, 0x3d, 0xa1, 0x40, 0x7d, 0x9f, 0x29, 0xfe, 0x14 } },
	{ 9, { 0x15, 0x73, 0xa1, 0x50, 0x5d, 0xa6, 0xf5, 0xfe, 0x38 } },
	{ 9, { 0x35, 0x73, 0xa1, 0x50, 0x5d, 0xa6, 0xf5, 0xfe, 0x38 } },
	{ 9, { 0x1d, 0xed, 0xd0, 0x68, 0x29, 0xb4, 0xe1, 0x00, 0xb8 } },
	{ 9, { 0x3d, 0xed, 0xd0, 0x68, 0x29, 0xb4, 0xe1, 0x00, 0xb8 } },
	{ 3, { 0x80, 0xb3, 0x0a } },
	{-1, { 0 } }
};

/* bring hardware to a sane state. this has to be done, just in case someone
   wants to capture from this device before it has been properly initialized.
   the capture engine would badly fail, because no valid signal arrives on the
   saa7146, thus leading to timeouts and stuff. */
static int mxb_init_done(struct saa7146_dev* dev)
{
	struct mxb* mxb = (struct mxb*)dev->ext_priv;
	struct i2c_msg msg;
	struct tuner_setup tun_setup;
	v4l2_std_id std = V4L2_STD_PAL_BG;

	int i, err = 0;

	/* mute audio on tea6420s */
	tea6420_route(mxb, 6);

	/* select video mode in saa7111a */
	saa7111a_call(mxb, video, s_std, std);

	/* select tuner-output on saa7111a */
	saa7111a_call(mxb, video, s_routing, SAA7115_COMPOSITE0,
		SAA7111_FMT_CCIR, 0);

	/* select a tuner type */
	tun_setup.mode_mask = T_ANALOG_TV;
	tun_setup.addr = ADDR_UNSET;
	tun_setup.type = TUNER_PHILIPS_PAL;
	tuner_call(mxb, tuner, s_type_addr, &tun_setup);
	/* tune in some frequency on tuner */
	mxb->cur_freq.tuner = 0;
	mxb->cur_freq.type = V4L2_TUNER_ANALOG_TV;
	mxb->cur_freq.frequency = freq;
	tuner_call(mxb, tuner, s_frequency, &mxb->cur_freq);

	/* set a default video standard */
	/* These two gpio calls set the GPIO pins that control the tda9820 */
	saa7146_write(dev, GPIO_CTRL, 0x00404050);
	saa7111a_call(mxb, core, s_gpio, 1);
	saa7111a_call(mxb, video, s_std, std);
	tuner_call(mxb, video, s_std, std);

	/* switch to tuner-channel on tea6415c */
	tea6415c_call(mxb, video, s_routing, 3, 17, 0);

	/* select tuner-output on multicable on tea6415c */
	tea6415c_call(mxb, video, s_routing, 3, 13, 0);

	/* the rest for mxb */
	mxb->cur_input = 0;
	mxb->cur_audinput = video_audio_connect[mxb->cur_input];
	mxb->cur_mute = 1;

	mxb->cur_mode = V4L2_TUNER_MODE_STEREO;
	mxb_update_audmode(mxb);

	/* check if the saa7740 (aka 'sound arena module') is present
	   on the mxb. if so, we must initialize it. due to lack of
	   information about the saa7740, the values were reverse
	   engineered. */
	msg.addr = 0x1b;
	msg.flags = 0;
	msg.len = mxb_saa7740_init[0].length;
	msg.buf = &mxb_saa7740_init[0].data[0];

	err = i2c_transfer(&mxb->i2c_adapter, &msg, 1);
	if (err == 1) {
		/* the sound arena module is a pos, that's probably the reason
		   philips refuses to hand out a datasheet for the saa7740...
		   it seems to screw up the i2c bus, so we disable fast irq
		   based i2c transactions here and rely on the slow and safe
		   polling method ... */
		extension.flags &= ~SAA7146_USE_I2C_IRQ;
		for (i = 1; ; i++) {
			if (-1 == mxb_saa7740_init[i].length)
				break;

			msg.len = mxb_saa7740_init[i].length;
			msg.buf = &mxb_saa7740_init[i].data[0];
			err = i2c_transfer(&mxb->i2c_adapter, &msg, 1);
			if (err != 1) {
				DEB_D("failed to initialize 'sound arena module'\n");
				goto err;
			}
		}
		pr_info("'sound arena module' detected\n");
	}
err:
	/* the rest for saa7146: you should definitely set some basic values
	   for the input-port handling of the saa7146. */

	/* ext->saa has been filled by the core driver */

	/* some stuff is done via variables */
	saa7146_set_hps_source_and_sync(dev, input_port_selection[mxb->cur_input].hps_source,
			input_port_selection[mxb->cur_input].hps_sync);

	/* some stuff is done via direct write to the registers */

	/* this is ugly, but because of the fact that this is completely
	   hardware dependend, it should be done directly... */
	saa7146_write(dev, DD1_STREAM_B,	0x00000000);
	saa7146_write(dev, DD1_INIT,		0x02000200);
	saa7146_write(dev, MC2, (MASK_09 | MASK_25 | MASK_10 | MASK_26));

	return 0;
}

/* interrupt-handler. this gets called when irq_mask is != 0.
   it must clear the interrupt-bits in irq_mask it has handled */
/*
void mxb_irq_bh(struct saa7146_dev* dev, u32* irq_mask)
{
	struct mxb* mxb = (struct mxb*)dev->ext_priv;
}
*/

static int vidioc_enum_input(struct file *file, void *fh, struct v4l2_input *i)
{
	DEB_EE("VIDIOC_ENUMINPUT %d\n", i->index);
	if (i->index >= MXB_INPUTS)
		return -EINVAL;
	memcpy(i, &mxb_inputs[i->index], sizeof(struct v4l2_input));
	return 0;
}

static int vidioc_g_input(struct file *file, void *fh, unsigned int *i)
{
	struct saa7146_dev *dev = ((struct saa7146_fh *)fh)->dev;
	struct mxb *mxb = (struct mxb *)dev->ext_priv;
	*i = mxb->cur_input;

	DEB_EE("VIDIOC_G_INPUT %d\n", *i);
	return 0;
}

static int vidioc_s_input(struct file *file, void *fh, unsigned int input)
{
	struct saa7146_dev *dev = ((struct saa7146_fh *)fh)->dev;
	struct mxb *mxb = (struct mxb *)dev->ext_priv;
	int err = 0;
	int i = 0;

	DEB_EE("VIDIOC_S_INPUT %d\n", input);

	if (input >= MXB_INPUTS)
		return -EINVAL;

	mxb->cur_input = input;

	saa7146_set_hps_source_and_sync(dev, input_port_selection[input].hps_source,
			input_port_selection[input].hps_sync);

	/* prepare switching of tea6415c and saa7111a;
	   have a look at the 'background'-file for further information  */
	switch (input) {
	case TUNER:
		i = SAA7115_COMPOSITE0;

		err = tea6415c_call(mxb, video, s_routing, 3, 17, 0);

		/* connect tuner-output always to multicable */
		if (!err)
			err = tea6415c_call(mxb, video, s_routing, 3, 13, 0);
		break;
	case AUX3_YC:
		/* nothing to be done here. aux3_yc is
		   directly connected to the saa711a */
		i = SAA7115_SVIDEO1;
		break;
	case AUX3:
		/* nothing to be done here. aux3 is
		   directly connected to the saa711a */
		i = SAA7115_COMPOSITE1;
		break;
	case AUX1:
		i = SAA7115_COMPOSITE0;
		err = tea6415c_call(mxb, video, s_routing, 1, 17, 0);
		break;
	}

	if (err)
		return err;

	/* switch video in saa7111a */
	if (saa7111a_call(mxb, video, s_routing, i, SAA7111_FMT_CCIR, 0))
		pr_err("VIDIOC_S_INPUT: could not address saa7111a\n");

	mxb->cur_audinput = video_audio_connect[input];
	/* switch the audio-source only if necessary */
	if (0 == mxb->cur_mute)
		tea6420_route(mxb, mxb->cur_audinput);
	if (mxb->cur_audinput == 0)
		mxb_update_audmode(mxb);

	return 0;
}

static int vidioc_g_tuner(struct file *file, void *fh, struct v4l2_tuner *t)
{
	struct saa7146_dev *dev = ((struct saa7146_fh *)fh)->dev;
	struct mxb *mxb = (struct mxb *)dev->ext_priv;

	if (t->index) {
		DEB_D("VIDIOC_G_TUNER: channel %d does not have a tuner attached\n",
		      t->index);
		return -EINVAL;
	}

	DEB_EE("VIDIOC_G_TUNER: %d\n", t->index);

	memset(t, 0, sizeof(*t));
	strscpy(t->name, "TV Tuner", sizeof(t->name));
	t->type = V4L2_TUNER_ANALOG_TV;
	t->capability = V4L2_TUNER_CAP_NORM | V4L2_TUNER_CAP_STEREO |
			V4L2_TUNER_CAP_LANG1 | V4L2_TUNER_CAP_LANG2 | V4L2_TUNER_CAP_SAP;
	t->audmode = mxb->cur_mode;
	return call_all(dev, tuner, g_tuner, t);
}

static int vidioc_s_tuner(struct file *file, void *fh, const struct v4l2_tuner *t)
{
	struct saa7146_dev *dev = ((struct saa7146_fh *)fh)->dev;
	struct mxb *mxb = (struct mxb *)dev->ext_priv;

	if (t->index) {
		DEB_D("VIDIOC_S_TUNER: channel %d does not have a tuner attached\n",
		      t->index);
		return -EINVAL;
	}

	mxb->cur_mode = t->audmode;
	return call_all(dev, tuner, s_tuner, t);
}

static int vidioc_querystd(struct file *file, void *fh, v4l2_std_id *norm)
{
	struct saa7146_dev *dev = ((struct saa7146_fh *)fh)->dev;

	return call_all(dev, video, querystd, norm);
}

static int vidioc_g_frequency(struct file *file, void *fh, struct v4l2_frequency *f)
{
	struct saa7146_dev *dev = ((struct saa7146_fh *)fh)->dev;
	struct mxb *mxb = (struct mxb *)dev->ext_priv;

	if (f->tuner)
		return -EINVAL;
	*f = mxb->cur_freq;

	DEB_EE("VIDIOC_G_FREQ: freq:0x%08x\n", mxb->cur_freq.frequency);
	return 0;
}

static int vidioc_s_frequency(struct file *file, void *fh, const struct v4l2_frequency *f)
{
	struct saa7146_dev *dev = ((struct saa7146_fh *)fh)->dev;
	struct mxb *mxb = (struct mxb *)dev->ext_priv;
	struct saa7146_vv *vv = dev->vv_data;

	if (f->tuner)
		return -EINVAL;

	if (V4L2_TUNER_ANALOG_TV != f->type)
		return -EINVAL;

	DEB_EE("VIDIOC_S_FREQUENCY: freq:0x%08x\n", mxb->cur_freq.frequency);

	/* tune in desired frequency */
	tuner_call(mxb, tuner, s_frequency, f);
	/* let the tuner subdev clamp the frequency to the tuner range */
	mxb->cur_freq = *f;
	tuner_call(mxb, tuner, g_frequency, &mxb->cur_freq);
	if (mxb->cur_audinput == 0)
		mxb_update_audmode(mxb);

	if (mxb->cur_input)
		return 0;

	/* hack: changing the frequency should invalidate the vbi-counter (=> alevt) */
	spin_lock(&dev->slock);
	vv->vbi_fieldcount = 0;
	spin_unlock(&dev->slock);

	return 0;
}

static int vidioc_enumaudio(struct file *file, void *fh, struct v4l2_audio *a)
{
	if (a->index >= MXB_AUDIOS)
		return -EINVAL;
	*a = mxb_audios[a->index];
	return 0;
}

static int vidioc_g_audio(struct file *file, void *fh, struct v4l2_audio *a)
{
	struct saa7146_dev *dev = ((struct saa7146_fh *)fh)->dev;
	struct mxb *mxb = (struct mxb *)dev->ext_priv;

	DEB_EE("VIDIOC_G_AUDIO\n");
	*a = mxb_audios[mxb->cur_audinput];
	return 0;
}

static int vidioc_s_audio(struct file *file, void *fh, const struct v4l2_audio *a)
{
	struct saa7146_dev *dev = ((struct saa7146_fh *)fh)->dev;
	struct mxb *mxb = (struct mxb *)dev->ext_priv;

	DEB_D("VIDIOC_S_AUDIO %d\n", a->index);
	if (a->index >= 32 ||
	    !(mxb_inputs[mxb->cur_input].audioset & (1 << a->index)))
		return -EINVAL;

	if (mxb->cur_audinput != a->index) {
		mxb->cur_audinput = a->index;
		tea6420_route(mxb, a->index);
		if (mxb->cur_audinput == 0)
			mxb_update_audmode(mxb);
	}
	return 0;
}

#ifdef CONFIG_VIDEO_ADV_DEBUG
static int vidioc_g_register(struct file *file, void *fh, struct v4l2_dbg_register *reg)
{
	struct saa7146_dev *dev = ((struct saa7146_fh *)fh)->dev;

	if (reg->reg > pci_resource_len(dev->pci, 0) - 4)
		return -EINVAL;
	reg->val = saa7146_read(dev, reg->reg);
	reg->size = 4;
	return 0;
}

static int vidioc_s_register(struct file *file, void *fh, const struct v4l2_dbg_register *reg)
{
	struct saa7146_dev *dev = ((struct saa7146_fh *)fh)->dev;

	if (reg->reg > pci_resource_len(dev->pci, 0) - 4)
		return -EINVAL;
	saa7146_write(dev, reg->reg, reg->val);
	return 0;
}
#endif

static struct saa7146_ext_vv vv_data;

/* this function only gets called when the probing was successful */
static int mxb_attach(struct saa7146_dev *dev, struct saa7146_pci_extension_data *info)
{
	struct mxb *mxb;
	int ret;

	DEB_EE("dev:%p\n", dev);

	ret = saa7146_vv_init(dev, &vv_data);
	if (ret) {
		ERR("Error in saa7146_vv_init()");
		return ret;
	}

	if (mxb_probe(dev)) {
		saa7146_vv_release(dev);
		return -1;
	}
	mxb = (struct mxb *)dev->ext_priv;

	vv_data.vid_ops.vidioc_enum_input = vidioc_enum_input;
	vv_data.vid_ops.vidioc_g_input = vidioc_g_input;
	vv_data.vid_ops.vidioc_s_input = vidioc_s_input;
	vv_data.vid_ops.vidioc_querystd = vidioc_querystd;
	vv_data.vid_ops.vidioc_g_tuner = vidioc_g_tuner;
	vv_data.vid_ops.vidioc_s_tuner = vidioc_s_tuner;
	vv_data.vid_ops.vidioc_g_frequency = vidioc_g_frequency;
	vv_data.vid_ops.vidioc_s_frequency = vidioc_s_frequency;
	vv_data.vid_ops.vidioc_enumaudio = vidioc_enumaudio;
	vv_data.vid_ops.vidioc_g_audio = vidioc_g_audio;
	vv_data.vid_ops.vidioc_s_audio = vidioc_s_audio;
#ifdef CONFIG_VIDEO_ADV_DEBUG
	vv_data.vid_ops.vidioc_g_register = vidioc_g_register;
	vv_data.vid_ops.vidioc_s_register = vidioc_s_register;
#endif
	if (saa7146_register_device(&mxb->video_dev, dev, "mxb", VFL_TYPE_VIDEO)) {
		ERR("cannot register capture v4l2 device. skipping.\n");
		saa7146_vv_release(dev);
		return -1;
	}

	/* initialization stuff (vbi) (only for revision > 0 and for extensions which want it)*/
	if (MXB_BOARD_CAN_DO_VBI(dev)) {
		if (saa7146_register_device(&mxb->vbi_dev, dev, "mxb", VFL_TYPE_VBI)) {
			ERR("cannot register vbi v4l2 device. skipping.\n");
		}
	}

	pr_info("found Multimedia eXtension Board #%d\n", mxb_num);

	mxb_num++;
	mxb_init_done(dev);
	return 0;
}

static int mxb_detach(struct saa7146_dev *dev)
{
	struct mxb *mxb = (struct mxb *)dev->ext_priv;

	DEB_EE("dev:%p\n", dev);

	/* mute audio on tea6420s */
	tea6420_route(mxb, 6);

	saa7146_unregister_device(&mxb->video_dev,dev);
	if (MXB_BOARD_CAN_DO_VBI(dev))
		saa7146_unregister_device(&mxb->vbi_dev, dev);
	saa7146_vv_release(dev);

	mxb_num--;

	i2c_del_adapter(&mxb->i2c_adapter);
	kfree(mxb);

	return 0;
}

static int std_callback(struct saa7146_dev *dev, struct saa7146_standard *standard)
{
	struct mxb *mxb = (struct mxb *)dev->ext_priv;

	if (V4L2_STD_PAL_I == standard->id) {
		v4l2_std_id std = V4L2_STD_PAL_I;

		DEB_D("VIDIOC_S_STD: setting mxb for PAL_I\n");
		/* These two gpio calls set the GPIO pins that control the tda9820 */
		saa7146_write(dev, GPIO_CTRL, 0x00404050);
		saa7111a_call(mxb, core, s_gpio, 0);
		saa7111a_call(mxb, video, s_std, std);
		if (mxb->cur_input == 0)
			tuner_call(mxb, video, s_std, std);
	} else {
		v4l2_std_id std = V4L2_STD_PAL_BG;

		if (mxb->cur_input)
			std = standard->id;
		DEB_D("VIDIOC_S_STD: setting mxb for PAL/NTSC/SECAM\n");
		/* These two gpio calls set the GPIO pins that control the tda9820 */
		saa7146_write(dev, GPIO_CTRL, 0x00404050);
		saa7111a_call(mxb, core, s_gpio, 1);
		saa7111a_call(mxb, video, s_std, std);
		if (mxb->cur_input == 0)
			tuner_call(mxb, video, s_std, std);
	}
	return 0;
}

static struct saa7146_standard standard[] = {
	{
		.name	= "PAL-BG",	.id	= V4L2_STD_PAL_BG,
		.v_offset	= 0x17,	.v_field	= 288,
		.h_offset	= 0x14,	.h_pixels	= 680,
		.v_max_out	= 576,	.h_max_out	= 768,
	}, {
		.name	= "PAL-I",	.id	= V4L2_STD_PAL_I,
		.v_offset	= 0x17,	.v_field	= 288,
		.h_offset	= 0x14,	.h_pixels	= 680,
		.v_max_out	= 576,	.h_max_out	= 768,
	}, {
		.name	= "NTSC",	.id	= V4L2_STD_NTSC,
		.v_offset	= 0x16,	.v_field	= 240,
		.h_offset	= 0x06,	.h_pixels	= 708,
		.v_max_out	= 480,	.h_max_out	= 640,
	}, {
		.name	= "SECAM",	.id	= V4L2_STD_SECAM,
		.v_offset	= 0x14,	.v_field	= 288,
		.h_offset	= 0x14,	.h_pixels	= 720,
		.v_max_out	= 576,	.h_max_out	= 768,
	}
};

static struct saa7146_pci_extension_data mxb = {
	.ext_priv = "Multimedia eXtension Board",
	.ext = &extension,
};

static const struct pci_device_id pci_tbl[] = {
	{
		.vendor    = PCI_VENDOR_ID_PHILIPS,
		.device	   = PCI_DEVICE_ID_PHILIPS_SAA7146,
		.subvendor = 0x0000,
		.subdevice = 0x0000,
		.driver_data = (unsigned long)&mxb,
	}, {
		.vendor	= 0,
	}
};

MODULE_DEVICE_TABLE(pci, pci_tbl);

static struct saa7146_ext_vv vv_data = {
	.inputs		= MXB_INPUTS,
	.capabilities	= V4L2_CAP_TUNER | V4L2_CAP_VBI_CAPTURE | V4L2_CAP_AUDIO,
	.stds		= &standard[0],
	.num_stds	= ARRAY_SIZE(standard),
	.std_callback	= &std_callback,
};

static struct saa7146_extension extension = {
	.name		= "Multimedia eXtension Board",
	.flags		= SAA7146_USE_I2C_IRQ,

	.pci_tbl	= &pci_tbl[0],
	.module		= THIS_MODULE,

	.attach		= mxb_attach,
	.detach		= mxb_detach,

	.irq_mask	= 0,
	.irq_func	= NULL,
};

static int __init mxb_init_module(void)
{
	if (saa7146_register_extension(&extension)) {
		DEB_S("failed to register extension\n");
		return -ENODEV;
	}

	return 0;
}

static void __exit mxb_cleanup_module(void)
{
	saa7146_unregister_extension(&extension);
}

module_init(mxb_init_module);
module_exit(mxb_cleanup_module);

MODULE_DESCRIPTION("video4linux-2 driver for the Siemens-Nixdorf 'Multimedia eXtension board'");
MODULE_AUTHOR("Michael Hunold <michael@mihu.de>");
MODULE_LICENSE("GPL");

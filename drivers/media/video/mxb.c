/*
    mxb - v4l2 driver for the Multimedia eXtension Board

    Copyright (C) 1998-2006 Michael Hunold <michael@mihu.de>

    Visit http://www.mihu.de/linux/saa7146/mxb/
    for further details about this card.

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

#define DEBUG_VARIABLE debug

#include <media/saa7146_vv.h>
#include <media/tuner.h>
#include <media/v4l2-common.h>
#include <media/saa7115.h>

#include "mxb.h"
#include "tea6415c.h"
#include "tea6420.h"

#define	I2C_SAA5246A  0x11
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
	{ TUNER,	"Tuner",		V4L2_INPUT_TYPE_TUNER,	1, 0, V4L2_STD_PAL_BG|V4L2_STD_NTSC_M, 0 },
	{ AUX1,		"AUX1",			V4L2_INPUT_TYPE_CAMERA,	2, 0, V4L2_STD_PAL_BG|V4L2_STD_NTSC_M, 0 },
	{ AUX3,		"AUX3 Composite",	V4L2_INPUT_TYPE_CAMERA,	4, 0, V4L2_STD_PAL_BG|V4L2_STD_NTSC_M, 0 },
	{ AUX3_YC,	"AUX3 S-Video",		V4L2_INPUT_TYPE_CAMERA,	4, 0, V4L2_STD_PAL_BG|V4L2_STD_NTSC_M, 0 },
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

#define MAXCONTROLS	1
static struct v4l2_queryctrl mxb_controls[] = {
	{ V4L2_CID_AUDIO_MUTE, V4L2_CTRL_TYPE_BOOLEAN, "Mute", 0, 1, 1, 0, 0 },
};

struct mxb
{
	struct video_device	*video_dev;
	struct video_device	*vbi_dev;

	struct i2c_adapter	i2c_adapter;

	struct v4l2_subdev	*saa7111a;
	struct v4l2_subdev	*tda9840;
	struct v4l2_subdev	*tea6415c;
	struct v4l2_subdev	*tuner;
	struct v4l2_subdev	*tea6420_1;
	struct v4l2_subdev	*tea6420_2;

	int	cur_mode;	/* current audio mode (mono, stereo, ...) */
	int	cur_input;	/* current input */
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

static inline void tea6420_route_cd(struct mxb *mxb, int idx)
{
	v4l2_subdev_call(mxb->tea6420_1, audio, s_routing,
		TEA6420_cd[idx][0].input, TEA6420_cd[idx][0].output, 0);
	v4l2_subdev_call(mxb->tea6420_2, audio, s_routing,
		TEA6420_cd[idx][1].input, TEA6420_cd[idx][1].output, 0);
}

static inline void tea6420_route_line(struct mxb *mxb, int idx)
{
	v4l2_subdev_call(mxb->tea6420_1, audio, s_routing,
		TEA6420_line[idx][0].input, TEA6420_line[idx][0].output, 0);
	v4l2_subdev_call(mxb->tea6420_2, audio, s_routing,
		TEA6420_line[idx][1].input, TEA6420_line[idx][1].output, 0);
}

static struct saa7146_extension extension;

static int mxb_probe(struct saa7146_dev *dev)
{
	struct mxb *mxb = NULL;
	int err;

	err = saa7146_vv_devinit(dev);
	if (err)
		return err;
	mxb = kzalloc(sizeof(struct mxb), GFP_KERNEL);
	if (mxb == NULL) {
		DEB_D(("not enough kernel memory.\n"));
		return -ENOMEM;
	}

	snprintf(mxb->i2c_adapter.name, sizeof(mxb->i2c_adapter.name), "mxb%d", mxb_num);

	saa7146_i2c_adapter_prepare(dev, &mxb->i2c_adapter, SAA7146_I2C_BUS_BIT_RATE_480);
	if (i2c_add_adapter(&mxb->i2c_adapter) < 0) {
		DEB_S(("cannot register i2c-device. skipping.\n"));
		kfree(mxb);
		return -EFAULT;
	}

	mxb->saa7111a = v4l2_i2c_new_subdev(&dev->v4l2_dev, &mxb->i2c_adapter,
			"saa7115", "saa7111", I2C_SAA7111A, NULL);
	mxb->tea6420_1 = v4l2_i2c_new_subdev(&dev->v4l2_dev, &mxb->i2c_adapter,
			"tea6420", "tea6420", I2C_TEA6420_1, NULL);
	mxb->tea6420_2 = v4l2_i2c_new_subdev(&dev->v4l2_dev, &mxb->i2c_adapter,
			"tea6420", "tea6420", I2C_TEA6420_2, NULL);
	mxb->tea6415c = v4l2_i2c_new_subdev(&dev->v4l2_dev, &mxb->i2c_adapter,
			"tea6415c", "tea6415c", I2C_TEA6415C, NULL);
	mxb->tda9840 = v4l2_i2c_new_subdev(&dev->v4l2_dev, &mxb->i2c_adapter,
			"tda9840", "tda9840", I2C_TDA9840, NULL);
	mxb->tuner = v4l2_i2c_new_subdev(&dev->v4l2_dev, &mxb->i2c_adapter,
			"tuner", "tuner", I2C_TUNER, NULL);
	if (v4l2_i2c_new_subdev(&dev->v4l2_dev, &mxb->i2c_adapter,
			"saa5246a", "saa5246a", I2C_SAA5246A, NULL)) {
		printk(KERN_INFO "mxb: found teletext decoder\n");
	}

	/* check if all devices are present */
	if (!mxb->tea6420_1 || !mxb->tea6420_2 || !mxb->tea6415c ||
	    !mxb->tda9840 || !mxb->saa7111a || !mxb->tuner) {
		printk("mxb: did not find all i2c devices. aborting\n");
		i2c_del_adapter(&mxb->i2c_adapter);
		kfree(mxb);
		return -ENODEV;
	}

	/* all devices are present, probe was successful */

	/* we store the pointer in our private data field */
	dev->ext_priv = mxb;

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

	int i = 0, err = 0;

	/* select video mode in saa7111a */
	saa7111a_call(mxb, core, s_std, std);

	/* select tuner-output on saa7111a */
	i = 0;
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
	tuner_call(mxb, core, s_std, std);

	/* mute audio on tea6420s */
	tea6420_route_line(mxb, 6);
	tea6420_route_cd(mxb, 6);

	/* switch to tuner-channel on tea6415c */
	tea6415c_call(mxb, video, s_routing, 3, 17, 0);

	/* select tuner-output on multicable on tea6415c */
	tea6415c_call(mxb, video, s_routing, 3, 13, 0);

	/* the rest for mxb */
	mxb->cur_input = 0;
	mxb->cur_mute = 1;

	mxb->cur_mode = V4L2_TUNER_MODE_STEREO;

	/* check if the saa7740 (aka 'sound arena module') is present
	   on the mxb. if so, we must initialize it. due to lack of
	   informations about the saa7740, the values were reverse
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
				DEB_D(("failed to initialize 'sound arena module'.\n"));
				goto err;
			}
		}
		INFO(("'sound arena module' detected.\n"));
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

static int vidioc_queryctrl(struct file *file, void *fh, struct v4l2_queryctrl *qc)
{
	struct saa7146_dev *dev = ((struct saa7146_fh *)fh)->dev;
	int i;

	for (i = MAXCONTROLS - 1; i >= 0; i--) {
		if (mxb_controls[i].id == qc->id) {
			*qc = mxb_controls[i];
			DEB_D(("VIDIOC_QUERYCTRL %d.\n", qc->id));
			return 0;
		}
	}
	return dev->ext_vv_data->core_ops->vidioc_queryctrl(file, fh, qc);
}

static int vidioc_g_ctrl(struct file *file, void *fh, struct v4l2_control *vc)
{
	struct saa7146_dev *dev = ((struct saa7146_fh *)fh)->dev;
	struct mxb *mxb = (struct mxb *)dev->ext_priv;
	int i;

	for (i = MAXCONTROLS - 1; i >= 0; i--) {
		if (mxb_controls[i].id == vc->id)
			break;
	}

	if (i < 0)
		return dev->ext_vv_data->core_ops->vidioc_g_ctrl(file, fh, vc);

	if (vc->id == V4L2_CID_AUDIO_MUTE) {
		vc->value = mxb->cur_mute;
		DEB_D(("VIDIOC_G_CTRL V4L2_CID_AUDIO_MUTE:%d.\n", vc->value));
		return 0;
	}

	DEB_EE(("VIDIOC_G_CTRL V4L2_CID_AUDIO_MUTE:%d.\n", vc->value));
	return 0;
}

static int vidioc_s_ctrl(struct file *file, void *fh, struct v4l2_control *vc)
{
	struct saa7146_dev *dev = ((struct saa7146_fh *)fh)->dev;
	struct mxb *mxb = (struct mxb *)dev->ext_priv;
	int i = 0;

	for (i = MAXCONTROLS - 1; i >= 0; i--) {
		if (mxb_controls[i].id == vc->id)
			break;
	}

	if (i < 0)
		return dev->ext_vv_data->core_ops->vidioc_s_ctrl(file, fh, vc);

	if (vc->id == V4L2_CID_AUDIO_MUTE) {
		mxb->cur_mute = vc->value;
		/* switch the audio-source */
		tea6420_route_line(mxb, vc->value ? 6 :
				video_audio_connect[mxb->cur_input]);
		DEB_EE(("VIDIOC_S_CTRL, V4L2_CID_AUDIO_MUTE: %d.\n", vc->value));
	}
	return 0;
}

static int vidioc_enum_input(struct file *file, void *fh, struct v4l2_input *i)
{
	DEB_EE(("VIDIOC_ENUMINPUT %d.\n", i->index));
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

	DEB_EE(("VIDIOC_G_INPUT %d.\n", *i));
	return 0;
}

static int vidioc_s_input(struct file *file, void *fh, unsigned int input)
{
	struct saa7146_dev *dev = ((struct saa7146_fh *)fh)->dev;
	struct mxb *mxb = (struct mxb *)dev->ext_priv;
	int err = 0;
	int i = 0;

	DEB_EE(("VIDIOC_S_INPUT %d.\n", input));

	if (input >= MXB_INPUTS)
		return -EINVAL;

	mxb->cur_input = input;

	saa7146_set_hps_source_and_sync(dev, input_port_selection[input].hps_source,
			input_port_selection[input].hps_sync);

	/* prepare switching of tea6415c and saa7111a;
	   have a look at the 'background'-file for further informations  */
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
		printk(KERN_ERR "VIDIOC_S_INPUT: could not address saa7111a.\n");

	/* switch the audio-source only if necessary */
	if (0 == mxb->cur_mute)
		tea6420_route_line(mxb, video_audio_connect[input]);

	return 0;
}

static int vidioc_g_tuner(struct file *file, void *fh, struct v4l2_tuner *t)
{
	struct saa7146_dev *dev = ((struct saa7146_fh *)fh)->dev;
	struct mxb *mxb = (struct mxb *)dev->ext_priv;

	if (t->index) {
		DEB_D(("VIDIOC_G_TUNER: channel %d does not have a tuner attached.\n", t->index));
		return -EINVAL;
	}

	DEB_EE(("VIDIOC_G_TUNER: %d\n", t->index));

	memset(t, 0, sizeof(*t));
	strlcpy(t->name, "TV Tuner", sizeof(t->name));
	t->type = V4L2_TUNER_ANALOG_TV;
	t->capability = V4L2_TUNER_CAP_NORM | V4L2_TUNER_CAP_STEREO |
			V4L2_TUNER_CAP_LANG1 | V4L2_TUNER_CAP_LANG2 | V4L2_TUNER_CAP_SAP;
	t->audmode = mxb->cur_mode;
	return call_all(dev, tuner, g_tuner, t);
}

static int vidioc_s_tuner(struct file *file, void *fh, struct v4l2_tuner *t)
{
	struct saa7146_dev *dev = ((struct saa7146_fh *)fh)->dev;
	struct mxb *mxb = (struct mxb *)dev->ext_priv;

	if (t->index) {
		DEB_D(("VIDIOC_S_TUNER: channel %d does not have a tuner attached.\n", t->index));
		return -EINVAL;
	}

	mxb->cur_mode = t->audmode;
	return call_all(dev, tuner, s_tuner, t);
}

static int vidioc_g_frequency(struct file *file, void *fh, struct v4l2_frequency *f)
{
	struct saa7146_dev *dev = ((struct saa7146_fh *)fh)->dev;
	struct mxb *mxb = (struct mxb *)dev->ext_priv;

	if (mxb->cur_input) {
		DEB_D(("VIDIOC_G_FREQ: channel %d does not have a tuner!\n",
					mxb->cur_input));
		return -EINVAL;
	}

	*f = mxb->cur_freq;

	DEB_EE(("VIDIOC_G_FREQ: freq:0x%08x.\n", mxb->cur_freq.frequency));
	return 0;
}

static int vidioc_s_frequency(struct file *file, void *fh, struct v4l2_frequency *f)
{
	struct saa7146_dev *dev = ((struct saa7146_fh *)fh)->dev;
	struct mxb *mxb = (struct mxb *)dev->ext_priv;
	struct saa7146_vv *vv = dev->vv_data;

	if (f->tuner)
		return -EINVAL;

	if (V4L2_TUNER_ANALOG_TV != f->type)
		return -EINVAL;

	if (mxb->cur_input) {
		DEB_D(("VIDIOC_S_FREQ: channel %d does not have a tuner!\n", mxb->cur_input));
		return -EINVAL;
	}

	mxb->cur_freq = *f;
	DEB_EE(("VIDIOC_S_FREQUENCY: freq:0x%08x.\n", mxb->cur_freq.frequency));

	/* tune in desired frequency */
	tuner_call(mxb, tuner, s_frequency, &mxb->cur_freq);

	/* hack: changing the frequency should invalidate the vbi-counter (=> alevt) */
	spin_lock(&dev->slock);
	vv->vbi_fieldcount = 0;
	spin_unlock(&dev->slock);

	return 0;
}

static int vidioc_g_audio(struct file *file, void *fh, struct v4l2_audio *a)
{
	struct saa7146_dev *dev = ((struct saa7146_fh *)fh)->dev;
	struct mxb *mxb = (struct mxb *)dev->ext_priv;

	if (a->index > MXB_INPUTS) {
		DEB_D(("VIDIOC_G_AUDIO %d out of range.\n", a->index));
		return -EINVAL;
	}

	DEB_EE(("VIDIOC_G_AUDIO %d.\n", a->index));
	memcpy(a, &mxb_audios[video_audio_connect[mxb->cur_input]], sizeof(struct v4l2_audio));
	return 0;
}

static int vidioc_s_audio(struct file *file, void *fh, struct v4l2_audio *a)
{
	DEB_D(("VIDIOC_S_AUDIO %d.\n", a->index));
	return 0;
}

#ifdef CONFIG_VIDEO_ADV_DEBUG
static int vidioc_g_register(struct file *file, void *fh, struct v4l2_dbg_register *reg)
{
	struct saa7146_dev *dev = ((struct saa7146_fh *)fh)->dev;

	return call_all(dev, core, g_register, reg);
}

static int vidioc_s_register(struct file *file, void *fh, struct v4l2_dbg_register *reg)
{
	struct saa7146_dev *dev = ((struct saa7146_fh *)fh)->dev;

	return call_all(dev, core, s_register, reg);
}
#endif

static long vidioc_default(struct file *file, void *fh, int cmd, void *arg)
{
	struct saa7146_dev *dev = ((struct saa7146_fh *)fh)->dev;
	struct mxb *mxb = (struct mxb *)dev->ext_priv;

	switch (cmd) {
	case MXB_S_AUDIO_CD:
	{
		int i = *(int *)arg;

		if (i < 0 || i >= MXB_AUDIOS) {
			DEB_D(("illegal argument to MXB_S_AUDIO_CD: i:%d.\n", i));
			return -EINVAL;
		}

		DEB_EE(("MXB_S_AUDIO_CD: i:%d.\n", i));

		tea6420_route_cd(mxb, i);
		return 0;
	}
	case MXB_S_AUDIO_LINE:
	{
		int i = *(int *)arg;

		if (i < 0 || i >= MXB_AUDIOS) {
			DEB_D(("illegal argument to MXB_S_AUDIO_LINE: i:%d.\n", i));
			return -EINVAL;
		}

		DEB_EE(("MXB_S_AUDIO_LINE: i:%d.\n", i));
		tea6420_route_line(mxb, i);
		return 0;
	}
	default:
/*
		DEB2(printk("does not handle this ioctl.\n"));
*/
		return -ENOIOCTLCMD;
	}
	return 0;
}

static struct saa7146_ext_vv vv_data;

/* this function only gets called when the probing was successful */
static int mxb_attach(struct saa7146_dev *dev, struct saa7146_pci_extension_data *info)
{
	struct mxb *mxb = (struct mxb *)dev->ext_priv;

	DEB_EE(("dev:%p\n", dev));

	/* checking for i2c-devices can be omitted here, because we
	   already did this in "mxb_vl42_probe" */

	saa7146_vv_init(dev, &vv_data);
	vv_data.ops.vidioc_queryctrl = vidioc_queryctrl;
	vv_data.ops.vidioc_g_ctrl = vidioc_g_ctrl;
	vv_data.ops.vidioc_s_ctrl = vidioc_s_ctrl;
	vv_data.ops.vidioc_enum_input = vidioc_enum_input;
	vv_data.ops.vidioc_g_input = vidioc_g_input;
	vv_data.ops.vidioc_s_input = vidioc_s_input;
	vv_data.ops.vidioc_g_tuner = vidioc_g_tuner;
	vv_data.ops.vidioc_s_tuner = vidioc_s_tuner;
	vv_data.ops.vidioc_g_frequency = vidioc_g_frequency;
	vv_data.ops.vidioc_s_frequency = vidioc_s_frequency;
	vv_data.ops.vidioc_g_audio = vidioc_g_audio;
	vv_data.ops.vidioc_s_audio = vidioc_s_audio;
#ifdef CONFIG_VIDEO_ADV_DEBUG
	vv_data.ops.vidioc_g_register = vidioc_g_register;
	vv_data.ops.vidioc_s_register = vidioc_s_register;
#endif
	vv_data.ops.vidioc_default = vidioc_default;
	if (saa7146_register_device(&mxb->video_dev, dev, "mxb", VFL_TYPE_GRABBER)) {
		ERR(("cannot register capture v4l2 device. skipping.\n"));
		return -1;
	}

	/* initialization stuff (vbi) (only for revision > 0 and for extensions which want it)*/
	if (MXB_BOARD_CAN_DO_VBI(dev)) {
		if (saa7146_register_device(&mxb->vbi_dev, dev, "mxb", VFL_TYPE_VBI)) {
			ERR(("cannot register vbi v4l2 device. skipping.\n"));
		}
	}

	printk("mxb: found Multimedia eXtension Board #%d.\n", mxb_num);

	mxb_num++;
	mxb_init_done(dev);
	return 0;
}

static int mxb_detach(struct saa7146_dev *dev)
{
	struct mxb *mxb = (struct mxb *)dev->ext_priv;

	DEB_EE(("dev:%p\n", dev));

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

		DEB_D(("VIDIOC_S_STD: setting mxb for PAL_I.\n"));
		/* set the 7146 gpio register -- I don't know what this does exactly */
		saa7146_write(dev, GPIO_CTRL, 0x00404050);
		/* unset the 7111 gpio register -- I don't know what this does exactly */
		saa7111a_call(mxb, core, s_gpio, 0);
		tuner_call(mxb, core, s_std, std);
	} else {
		v4l2_std_id std = V4L2_STD_PAL_BG;

		DEB_D(("VIDIOC_S_STD: setting mxb for PAL/NTSC/SECAM.\n"));
		/* set the 7146 gpio register -- I don't know what this does exactly */
		saa7146_write(dev, GPIO_CTRL, 0x00404050);
		/* set the 7111 gpio register -- I don't know what this does exactly */
		saa7111a_call(mxb, core, s_gpio, 1);
		tuner_call(mxb, core, s_std, std);
	}
	return 0;
}

static struct saa7146_standard standard[] = {
	{
		.name	= "PAL-BG", 	.id	= V4L2_STD_PAL_BG,
		.v_offset	= 0x17,	.v_field 	= 288,
		.h_offset	= 0x14,	.h_pixels 	= 680,
		.v_max_out	= 576,	.h_max_out	= 768,
	}, {
		.name	= "PAL-I", 	.id	= V4L2_STD_PAL_I,
		.v_offset	= 0x17,	.v_field 	= 288,
		.h_offset	= 0x14,	.h_pixels 	= 680,
		.v_max_out	= 576,	.h_max_out	= 768,
	}, {
		.name	= "NTSC", 	.id	= V4L2_STD_NTSC,
		.v_offset	= 0x16,	.v_field 	= 240,
		.h_offset	= 0x06,	.h_pixels 	= 708,
		.v_max_out	= 480,	.h_max_out	= 640,
	}, {
		.name	= "SECAM", 	.id	= V4L2_STD_SECAM,
		.v_offset	= 0x14,	.v_field 	= 288,
		.h_offset	= 0x14,	.h_pixels 	= 720,
		.v_max_out	= 576,	.h_max_out	= 768,
	}
};

static struct saa7146_pci_extension_data mxb = {
	.ext_priv = "Multimedia eXtension Board",
	.ext = &extension,
};

static struct pci_device_id pci_tbl[] = {
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
	.capabilities	= V4L2_CAP_TUNER | V4L2_CAP_VBI_CAPTURE,
	.stds		= &standard[0],
	.num_stds	= sizeof(standard)/sizeof(struct saa7146_standard),
	.std_callback	= &std_callback,
};

static struct saa7146_extension extension = {
	.name		= MXB_IDENTIFIER,
	.flags		= SAA7146_USE_I2C_IRQ,

	.pci_tbl	= &pci_tbl[0],
	.module		= THIS_MODULE,

	.probe		= mxb_probe,
	.attach		= mxb_attach,
	.detach		= mxb_detach,

	.irq_mask	= 0,
	.irq_func	= NULL,
};

static int __init mxb_init_module(void)
{
	if (saa7146_register_extension(&extension)) {
		DEB_S(("failed to register extension.\n"));
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

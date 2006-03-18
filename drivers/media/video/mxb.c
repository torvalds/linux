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
#include <linux/video_decoder.h>
#include <media/v4l2-common.h>

#include "mxb.h"
#include "tea6415c.h"
#include "tea6420.h"
#include "tda9840.h"

#define I2C_SAA7111 0x24

#define MXB_BOARD_CAN_DO_VBI(dev)   (dev->revision != 0)

/* global variable */
static int mxb_num = 0;

/* initial frequence the tuner will be tuned to.
   in verden (lower saxony, germany) 4148 is a
   channel called "phoenix" */
static int freq = 4148;
module_param(freq, int, 0644);
MODULE_PARM_DESC(freq, "initial frequency the tuner will be tuned to while setup");

static int debug = 0;
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

/* these are the necessary input-output-pins for bringing one audio source
(see above) to the CD-output */
static struct tea6420_multiplex TEA6420_cd[MXB_AUDIOS+1][2] =
		{
		{{1,1,0},{1,1,0}},	/* Tuner */
		{{5,1,0},{6,1,0}},	/* AUX 1 */
		{{4,1,0},{6,1,0}},	/* AUX 2 */
		{{3,1,0},{6,1,0}},	/* AUX 3 */
		{{1,1,0},{3,1,0}},	/* Radio */
		{{1,1,0},{2,1,0}},	/* CD-Rom */
		{{6,1,0},{6,1,0}}	/* Mute */
		};

/* these are the necessary input-output-pins for bringing one audio source
(see above) to the line-output */
static struct tea6420_multiplex TEA6420_line[MXB_AUDIOS+1][2] =
		{
		{{2,3,0},{1,2,0}},
		{{5,3,0},{6,2,0}},
		{{4,3,0},{6,2,0}},
		{{3,3,0},{6,2,0}},
		{{2,3,0},{3,2,0}},
		{{2,3,0},{2,2,0}},
		{{6,3,0},{6,2,0}}	/* Mute */
		};

#define MAXCONTROLS	1
static struct v4l2_queryctrl mxb_controls[] = {
	{ V4L2_CID_AUDIO_MUTE, V4L2_CTRL_TYPE_BOOLEAN, "Mute", 0, 1, 1, 0, 0 },
};

static struct saa7146_extension_ioctls ioctls[] = {
	{ VIDIOC_ENUMINPUT, 	SAA7146_EXCLUSIVE },
	{ VIDIOC_G_INPUT,	SAA7146_EXCLUSIVE },
	{ VIDIOC_S_INPUT,	SAA7146_EXCLUSIVE },
	{ VIDIOC_QUERYCTRL, 	SAA7146_BEFORE },
	{ VIDIOC_G_CTRL,	SAA7146_BEFORE },
	{ VIDIOC_S_CTRL,	SAA7146_BEFORE },
	{ VIDIOC_G_TUNER, 	SAA7146_EXCLUSIVE },
	{ VIDIOC_S_TUNER, 	SAA7146_EXCLUSIVE },
	{ VIDIOC_G_FREQUENCY,	SAA7146_EXCLUSIVE },
	{ VIDIOC_S_FREQUENCY, 	SAA7146_EXCLUSIVE },
	{ VIDIOC_G_AUDIO, 	SAA7146_EXCLUSIVE },
	{ VIDIOC_S_AUDIO, 	SAA7146_EXCLUSIVE },
	{ MXB_S_AUDIO_CD, 	SAA7146_EXCLUSIVE },	/* custom control */
	{ MXB_S_AUDIO_LINE, 	SAA7146_EXCLUSIVE },	/* custom control */
	{ 0,			0 }
};

struct mxb
{
	struct video_device	*video_dev;
	struct video_device	*vbi_dev;

	struct i2c_adapter	i2c_adapter;

	struct i2c_client*	saa7111a;
	struct i2c_client*	tda9840;
	struct i2c_client*	tea6415c;
	struct i2c_client*	tuner;
	struct i2c_client*	tea6420_1;
	struct i2c_client*	tea6420_2;

	int	cur_mode;	/* current audio mode (mono, stereo, ...) */
	int	cur_input;	/* current input */
	int	cur_mute;	/* current mute status */
	struct v4l2_frequency	cur_freq;	/* current frequency the tuner is tuned to */
};

static struct saa7146_extension extension;

static int mxb_probe(struct saa7146_dev* dev)
{
	struct mxb* mxb = NULL;
	struct i2c_client *client;
	struct list_head *item;
	int result;

	if ((result = request_module("saa7111")) < 0) {
		printk("mxb: saa7111 i2c module not available.\n");
		return -ENODEV;
	}
	if ((result = request_module("tuner")) < 0) {
		printk("mxb: tuner i2c module not available.\n");
		return -ENODEV;
	}
	if ((result = request_module("tea6420")) < 0) {
		printk("mxb: tea6420 i2c module not available.\n");
		return -ENODEV;
	}
	if ((result = request_module("tea6415c")) < 0) {
		printk("mxb: tea6415c i2c module not available.\n");
		return -ENODEV;
	}
	if ((result = request_module("tda9840")) < 0) {
		printk("mxb: tda9840 i2c module not available.\n");
		return -ENODEV;
	}

	mxb = kzalloc(sizeof(struct mxb), GFP_KERNEL);
	if( NULL == mxb ) {
		DEB_D(("not enough kernel memory.\n"));
		return -ENOMEM;
	}

	mxb->i2c_adapter = (struct i2c_adapter) {
		.class = I2C_CLASS_TV_ANALOG,
		.name = "mxb",
	};

	saa7146_i2c_adapter_prepare(dev, &mxb->i2c_adapter, SAA7146_I2C_BUS_BIT_RATE_480);
	if(i2c_add_adapter(&mxb->i2c_adapter) < 0) {
		DEB_S(("cannot register i2c-device. skipping.\n"));
		kfree(mxb);
		return -EFAULT;
	}

	/* loop through all i2c-devices on the bus and look who is there */
	list_for_each(item,&mxb->i2c_adapter.clients) {
		client = list_entry(item, struct i2c_client, list);
		if( I2C_ADDR_TEA6420_1 == client->addr )
			mxb->tea6420_1 = client;
		if( I2C_ADDR_TEA6420_2 == client->addr )
			mxb->tea6420_2 = client;
		if( I2C_TEA6415C_2 == client->addr )
			mxb->tea6415c = client;
		if( I2C_ADDR_TDA9840 == client->addr )
			mxb->tda9840 = client;
		if( I2C_SAA7111 == client->addr )
			mxb->saa7111a = client;
		if( 0x60 == client->addr )
			mxb->tuner = client;
	}

	/* check if all devices are present */
	if(    0 == mxb->tea6420_1	|| 0 == mxb->tea6420_2	|| 0 == mxb->tea6415c
	    || 0 == mxb->tda9840	|| 0 == mxb->saa7111a	|| 0 == mxb->tuner ) {

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
	{-1, { 0} }
};

static const unsigned char mxb_saa7111_init[] = {
	0x00, 0x00,	  /* 00 - ID byte */
	0x01, 0x00,	  /* 01 - reserved */

	/*front end */
	0x02, 0xd8,	  /* 02 - FUSE=x, GUDL=x, MODE=x */
	0x03, 0x23,	  /* 03 - HLNRS=0, VBSL=1, WPOFF=0, HOLDG=0, GAFIX=0, GAI1=256, GAI2=256 */
	0x04, 0x00,	  /* 04 - GAI1=256 */
	0x05, 0x00,	  /* 05 - GAI2=256 */

	/* decoder */
	0x06, 0xf0,	  /* 06 - HSB at  xx(50Hz) /  xx(60Hz) pixels after end of last line */
	0x07, 0x30,	  /* 07 - HSS at  xx(50Hz) /  xx(60Hz) pixels after end of last line */
	0x08, 0xa8,	  /* 08 - AUFD=x, FSEL=x, EXFIL=x, VTRC=x, HPLL=x, VNOI=x */
	0x09, 0x02,	  /* 09 - BYPS=x, PREF=x, BPSS=x, VBLB=x, UPTCV=x, APER=x */
	0x0a, 0x80,	  /* 0a - BRIG=128 */
	0x0b, 0x47,	  /* 0b - CONT=1.109 */
	0x0c, 0x40,	  /* 0c - SATN=1.0 */
	0x0d, 0x00,	  /* 0d - HUE=0 */
	0x0e, 0x01,	  /* 0e - CDTO=0, CSTD=0, DCCF=0, FCTC=0, CHBW=1 */
	0x0f, 0x00,	  /* 0f - reserved */
	0x10, 0xd0,	  /* 10 - OFTS=x, HDEL=x, VRLN=x, YDEL=x */
	0x11, 0x8c,	  /* 11 - GPSW=x, CM99=x, FECO=x, COMPO=x, OEYC=1, OEHV=1, VIPB=0, COLO=0 */
	0x12, 0x80,	  /* 12 - xx output control 2 */
	0x13, 0x30,	  /* 13 - xx output control 3 */
	0x14, 0x00,	  /* 14 - reserved */
	0x15, 0x15,	  /* 15 - VBI */
	0x16, 0x04,	  /* 16 - VBI */
	0x17, 0x00,	  /* 17 - VBI */
};

/* bring hardware to a sane state. this has to be done, just in case someone
   wants to capture from this device before it has been properly initialized.
   the capture engine would badly fail, because no valid signal arrives on the
   saa7146, thus leading to timeouts and stuff. */
static int mxb_init_done(struct saa7146_dev* dev)
{
	struct mxb* mxb = (struct mxb*)dev->ext_priv;
	struct video_decoder_init init;
	struct i2c_msg msg;
	struct tuner_setup tun_setup;
	v4l2_std_id std = V4L2_STD_PAL_BG;

	int i = 0, err = 0;
	struct	tea6415c_multiplex vm;

	/* select video mode in saa7111a */
	i = VIDEO_MODE_PAL;
	/* fixme: currently pointless: gets overwritten by configuration below */
	mxb->saa7111a->driver->command(mxb->saa7111a,DECODER_SET_NORM, &i);

	/* write configuration to saa7111a */
	init.data = mxb_saa7111_init;
	init.len = sizeof(mxb_saa7111_init);
	mxb->saa7111a->driver->command(mxb->saa7111a,DECODER_INIT, &init);

	/* select tuner-output on saa7111a */
	i = 0;
	mxb->saa7111a->driver->command(mxb->saa7111a,DECODER_SET_INPUT, &i);

	/* enable vbi bypass */
	i = 1;
	mxb->saa7111a->driver->command(mxb->saa7111a,DECODER_SET_VBI_BYPASS, &i);

	/* select a tuner type */
	tun_setup.mode_mask = T_ANALOG_TV;
	tun_setup.addr = ADDR_UNSET;
	tun_setup.type = TUNER_PHILIPS_PAL;
	mxb->tuner->driver->command(mxb->tuner,TUNER_SET_TYPE_ADDR, &tun_setup);
	/* tune in some frequency on tuner */
	mxb->cur_freq.tuner = 0;
	mxb->cur_freq.type = V4L2_TUNER_ANALOG_TV;
	mxb->cur_freq.frequency = freq;
	mxb->tuner->driver->command(mxb->tuner, VIDIOC_S_FREQUENCY,
					&mxb->cur_freq);

	/* set a default video standard */
	mxb->tuner->driver->command(mxb->tuner, VIDIOC_S_STD, &std);

	/* mute audio on tea6420s */
	mxb->tea6420_1->driver->command(mxb->tea6420_1,TEA6420_SWITCH, &TEA6420_line[6][0]);
	mxb->tea6420_2->driver->command(mxb->tea6420_2,TEA6420_SWITCH, &TEA6420_line[6][1]);
	mxb->tea6420_1->driver->command(mxb->tea6420_1,TEA6420_SWITCH, &TEA6420_cd[6][0]);
	mxb->tea6420_2->driver->command(mxb->tea6420_2,TEA6420_SWITCH, &TEA6420_cd[6][1]);

	/* switch to tuner-channel on tea6415c*/
	vm.out = 17;
	vm.in  = 3;
	mxb->tea6415c->driver->command(mxb->tea6415c,TEA6415C_SWITCH, &vm);

	/* select tuner-output on multicable on tea6415c*/
	vm.in  = 3;
	vm.out = 13;
	mxb->tea6415c->driver->command(mxb->tea6415c,TEA6415C_SWITCH, &vm);

	/* the rest for mxb */
	mxb->cur_input = 0;
	mxb->cur_mute = 1;

	mxb->cur_mode = V4L2_TUNER_MODE_STEREO;
	mxb->tda9840->driver->command(mxb->tda9840, TDA9840_SWITCH, &mxb->cur_mode);

	/* check if the saa7740 (aka 'sound arena module') is present
	   on the mxb. if so, we must initialize it. due to lack of
	   informations about the saa7740, the values were reverse
	   engineered. */
	msg.addr = 0x1b;
	msg.flags = 0;
	msg.len = mxb_saa7740_init[0].length;
	msg.buf = &mxb_saa7740_init[0].data[0];

	if( 1 == (err = i2c_transfer(&mxb->i2c_adapter, &msg, 1))) {
		/* the sound arena module is a pos, that's probably the reason
		   philips refuses to hand out a datasheet for the saa7740...
		   it seems to screw up the i2c bus, so we disable fast irq
		   based i2c transactions here and rely on the slow and safe
		   polling method ... */
		extension.flags &= ~SAA7146_USE_I2C_IRQ;
		for(i = 1;;i++) {
			if( -1 == mxb_saa7740_init[i].length ) {
				break;
			}

			msg.len = mxb_saa7740_init[i].length;
			msg.buf = &mxb_saa7740_init[i].data[0];
			if( 1 != (err = i2c_transfer(&mxb->i2c_adapter, &msg, 1))) {
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
	saa7146_set_hps_source_and_sync(dev, input_port_selection[mxb->cur_input].hps_source, input_port_selection[mxb->cur_input].hps_sync);

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

static struct saa7146_ext_vv vv_data;

/* this function only gets called when the probing was successful */
static int mxb_attach(struct saa7146_dev* dev, struct saa7146_pci_extension_data *info)
{
	struct mxb* mxb = (struct mxb*)dev->ext_priv;

	DEB_EE(("dev:%p\n",dev));

	/* checking for i2c-devices can be omitted here, because we
	   already did this in "mxb_vl42_probe" */

	saa7146_vv_init(dev,&vv_data);
	if( 0 != saa7146_register_device(&mxb->video_dev, dev, "mxb", VFL_TYPE_GRABBER)) {
		ERR(("cannot register capture v4l2 device. skipping.\n"));
		return -1;
	}

	/* initialization stuff (vbi) (only for revision > 0 and for extensions which want it)*/
	if( 0 != MXB_BOARD_CAN_DO_VBI(dev)) {
		if( 0 != saa7146_register_device(&mxb->vbi_dev, dev, "mxb", VFL_TYPE_VBI)) {
			ERR(("cannot register vbi v4l2 device. skipping.\n"));
		}
	}

	i2c_use_client(mxb->tea6420_1);
	i2c_use_client(mxb->tea6420_2);
	i2c_use_client(mxb->tea6415c);
	i2c_use_client(mxb->tda9840);
	i2c_use_client(mxb->saa7111a);
	i2c_use_client(mxb->tuner);

	printk("mxb: found 'Multimedia eXtension Board'-%d.\n",mxb_num);

	mxb_num++;
	mxb_init_done(dev);
	return 0;
}

static int mxb_detach(struct saa7146_dev* dev)
{
	struct mxb* mxb = (struct mxb*)dev->ext_priv;

	DEB_EE(("dev:%p\n",dev));

	i2c_release_client(mxb->tea6420_1);
	i2c_release_client(mxb->tea6420_2);
	i2c_release_client(mxb->tea6415c);
	i2c_release_client(mxb->tda9840);
	i2c_release_client(mxb->saa7111a);
	i2c_release_client(mxb->tuner);

	saa7146_unregister_device(&mxb->video_dev,dev);
	if( 0 != MXB_BOARD_CAN_DO_VBI(dev)) {
		saa7146_unregister_device(&mxb->vbi_dev,dev);
	}
	saa7146_vv_release(dev);

	mxb_num--;

	i2c_del_adapter(&mxb->i2c_adapter);
	kfree(mxb);

	return 0;
}

static int mxb_ioctl(struct saa7146_fh *fh, unsigned int cmd, void *arg)
{
	struct saa7146_dev *dev = fh->dev;
	struct mxb* mxb = (struct mxb*)dev->ext_priv;
	struct saa7146_vv *vv = dev->vv_data;

	switch(cmd) {
	case VIDIOC_ENUMINPUT:
	{
		struct v4l2_input *i = arg;

		DEB_EE(("VIDIOC_ENUMINPUT %d.\n",i->index));
		if( i->index < 0 || i->index >= MXB_INPUTS) {
			return -EINVAL;
		}
		memcpy(i, &mxb_inputs[i->index], sizeof(struct v4l2_input));

		return 0;
	}
	/* the saa7146 provides some controls (brightness, contrast, saturation)
	   which gets registered *after* this function. because of this we have
	   to return with a value != 0 even if the function succeded.. */
	case VIDIOC_QUERYCTRL:
	{
		struct v4l2_queryctrl *qc = arg;
		int i;

		for (i = MAXCONTROLS - 1; i >= 0; i--) {
			if (mxb_controls[i].id == qc->id) {
				*qc = mxb_controls[i];
				DEB_D(("VIDIOC_QUERYCTRL %d.\n",qc->id));
				return 0;
			}
		}
		return -EAGAIN;
	}
	case VIDIOC_G_CTRL:
	{
		struct v4l2_control *vc = arg;
		int i;

		for (i = MAXCONTROLS - 1; i >= 0; i--) {
			if (mxb_controls[i].id == vc->id) {
				break;
			}
		}

		if( i < 0 ) {
			return -EAGAIN;
		}

		switch (vc->id ) {
			case V4L2_CID_AUDIO_MUTE: {
				vc->value = mxb->cur_mute;
				DEB_D(("VIDIOC_G_CTRL V4L2_CID_AUDIO_MUTE:%d.\n",vc->value));
				return 0;
			}
		}

		DEB_EE(("VIDIOC_G_CTRL V4L2_CID_AUDIO_MUTE:%d.\n",vc->value));
		return 0;
	}

	case VIDIOC_S_CTRL:
	{
		struct	v4l2_control	*vc = arg;
		int i = 0;

		for (i = MAXCONTROLS - 1; i >= 0; i--) {
			if (mxb_controls[i].id == vc->id) {
				break;
			}
		}

		if( i < 0 ) {
			return -EAGAIN;
		}

		switch (vc->id ) {
			case V4L2_CID_AUDIO_MUTE: {
				mxb->cur_mute = vc->value;
				if( 0 == vc->value ) {
					/* switch the audio-source */
					mxb->tea6420_1->driver->command(mxb->tea6420_1,TEA6420_SWITCH, &TEA6420_line[video_audio_connect[mxb->cur_input]][0]);
					mxb->tea6420_2->driver->command(mxb->tea6420_2,TEA6420_SWITCH, &TEA6420_line[video_audio_connect[mxb->cur_input]][1]);
				} else {
					mxb->tea6420_1->driver->command(mxb->tea6420_1,TEA6420_SWITCH, &TEA6420_line[6][0]);
					mxb->tea6420_2->driver->command(mxb->tea6420_2,TEA6420_SWITCH, &TEA6420_line[6][1]);
				}
				DEB_EE(("VIDIOC_S_CTRL, V4L2_CID_AUDIO_MUTE: %d.\n",vc->value));
				break;
			}
		}
		return 0;
	}
	case VIDIOC_G_INPUT:
	{
		int *input = (int *)arg;
		*input = mxb->cur_input;

		DEB_EE(("VIDIOC_G_INPUT %d.\n",*input));
		return 0;
	}
	case VIDIOC_S_INPUT:
	{
		int input = *(int *)arg;
		struct	tea6415c_multiplex vm;
		int i = 0;

		DEB_EE(("VIDIOC_S_INPUT %d.\n",input));

		if (input < 0 || input >= MXB_INPUTS) {
			return -EINVAL;
		}

		/* fixme: locke das setzen des inputs mit hilfe des mutexes
		mutex_lock(&dev->lock);
		video_mux(dev,*i);
		mutex_unlock(&dev->lock);
		*/

		/* fixme: check if streaming capture
		if ( 0 != dev->streaming ) {
			DEB_D(("VIDIOC_S_INPUT illegal while streaming.\n"));
			return -EPERM;
		}
		*/

		mxb->cur_input = input;

		saa7146_set_hps_source_and_sync(dev, input_port_selection[input].hps_source, input_port_selection[input].hps_sync);

		/* prepare switching of tea6415c and saa7111a;
		   have a look at the 'background'-file for further informations  */
		switch( input ) {

			case TUNER:
			{
				i = 0;
				vm.in  = 3;
				vm.out = 17;

			if ( 0 != mxb->tea6415c->driver->command(mxb->tea6415c,TEA6415C_SWITCH, &vm)) {
					printk("VIDIOC_S_INPUT: could not address tea6415c #1\n");
					return -EFAULT;
				}
				/* connect tuner-output always to multicable */
				vm.in  = 3;
				vm.out = 13;
				break;
			}
			case AUX3_YC:
			{
				/* nothing to be done here. aux3_yc is
				   directly connected to the saa711a */
				i = 5;
				break;
			}
			case AUX3:
			{
				/* nothing to be done here. aux3 is
				   directly connected to the saa711a */
				i = 1;
				break;
			}
			case AUX1:
			{
				i = 0;
				vm.in  = 1;
				vm.out = 17;
				break;
			}
		}

		/* switch video in tea6415c only if necessary */
		switch( input ) {
			case TUNER:
			case AUX1:
			{
				if ( 0 != mxb->tea6415c->driver->command(mxb->tea6415c,TEA6415C_SWITCH, &vm)) {
					printk("VIDIOC_S_INPUT: could not address tea6415c #3\n");
					return -EFAULT;
				}
				break;
			}
			default:
			{
				break;
			}
		}

		/* switch video in saa7111a */
		if ( 0 != mxb->saa7111a->driver->command(mxb->saa7111a,DECODER_SET_INPUT, &i)) {
			printk("VIDIOC_S_INPUT: could not address saa7111a #1.\n");
		}

		/* switch the audio-source only if necessary */
		if( 0 == mxb->cur_mute ) {
			mxb->tea6420_1->driver->command(mxb->tea6420_1,TEA6420_SWITCH, &TEA6420_line[video_audio_connect[input]][0]);
			mxb->tea6420_2->driver->command(mxb->tea6420_2,TEA6420_SWITCH, &TEA6420_line[video_audio_connect[input]][1]);
		}

		return 0;
	}
	case VIDIOC_G_TUNER:
	{
		struct v4l2_tuner *t = arg;
		int byte = 0;

		if( 0 != t->index ) {
			DEB_D(("VIDIOC_G_TUNER: channel %d does not have a tuner attached.\n", t->index));
			return -EINVAL;
		}

		DEB_EE(("VIDIOC_G_TUNER: %d\n", t->index));

		memset(t,0,sizeof(*t));
		strcpy(t->name, "Television");

		t->type = V4L2_TUNER_ANALOG_TV;
		t->capability = V4L2_TUNER_CAP_NORM | V4L2_TUNER_CAP_STEREO | V4L2_TUNER_CAP_LANG1 | V4L2_TUNER_CAP_LANG2 | V4L2_TUNER_CAP_SAP;
		t->rangelow = 772;	/* 48.25 MHZ / 62.5 kHz = 772, see fi1216mk2-specs, page 2 */
		t->rangehigh = 13684;	/* 855.25 MHz / 62.5 kHz = 13684 */
		/* FIXME: add the real signal strength here */
		t->signal = 0xffff;
		t->afc = 0;

		mxb->tda9840->driver->command(mxb->tda9840,TDA9840_DETECT, &byte);
		t->audmode = mxb->cur_mode;

		if( byte < 0 ) {
			t->rxsubchans  = V4L2_TUNER_SUB_MONO;
		} else {
			switch(byte) {
				case TDA9840_MONO_DETECT: {
					t->rxsubchans 	= V4L2_TUNER_SUB_MONO;
					DEB_D(("VIDIOC_G_TUNER: V4L2_TUNER_MODE_MONO.\n"));
					break;
				}
				case TDA9840_DUAL_DETECT: {
					t->rxsubchans 	= V4L2_TUNER_SUB_LANG1 | V4L2_TUNER_SUB_LANG2;
					DEB_D(("VIDIOC_G_TUNER: V4L2_TUNER_MODE_LANG1.\n"));
					break;
				}
				case TDA9840_STEREO_DETECT: {
					t->rxsubchans 	= V4L2_TUNER_SUB_STEREO | V4L2_TUNER_SUB_MONO;
					DEB_D(("VIDIOC_G_TUNER: V4L2_TUNER_MODE_STEREO.\n"));
					break;
				}
				default: { /* TDA9840_INCORRECT_DETECT */
					t->rxsubchans 	= V4L2_TUNER_MODE_MONO;
					DEB_D(("VIDIOC_G_TUNER: TDA9840_INCORRECT_DETECT => V4L2_TUNER_MODE_MONO\n"));
					break;
				}
			}
		}

		return 0;
	}
	case VIDIOC_S_TUNER:
	{
		struct v4l2_tuner *t = arg;
		int result = 0;
		int byte = 0;

		if( 0 != t->index ) {
			DEB_D(("VIDIOC_S_TUNER: channel %d does not have a tuner attached.\n",t->index));
			return -EINVAL;
		}

		switch(t->audmode) {
			case V4L2_TUNER_MODE_STEREO: {
				mxb->cur_mode = V4L2_TUNER_MODE_STEREO;
				byte = TDA9840_SET_STEREO;
				DEB_D(("VIDIOC_S_TUNER: V4L2_TUNER_MODE_STEREO\n"));
				break;
			}
			case V4L2_TUNER_MODE_LANG1_LANG2: {
				mxb->cur_mode = V4L2_TUNER_MODE_LANG1_LANG2;
				byte = TDA9840_SET_BOTH;
				DEB_D(("VIDIOC_S_TUNER: V4L2_TUNER_MODE_LANG1_LANG2\n"));
				break;
			}
			case V4L2_TUNER_MODE_LANG1: {
				mxb->cur_mode = V4L2_TUNER_MODE_LANG1;
				byte = TDA9840_SET_LANG1;
				DEB_D(("VIDIOC_S_TUNER: V4L2_TUNER_MODE_LANG1\n"));
				break;
			}
			case V4L2_TUNER_MODE_LANG2: {
				mxb->cur_mode = V4L2_TUNER_MODE_LANG2;
				byte = TDA9840_SET_LANG2;
				DEB_D(("VIDIOC_S_TUNER: V4L2_TUNER_MODE_LANG2\n"));
				break;
			}
			default: { /* case V4L2_TUNER_MODE_MONO: {*/
				mxb->cur_mode = V4L2_TUNER_MODE_MONO;
				byte = TDA9840_SET_MONO;
				DEB_D(("VIDIOC_S_TUNER: TDA9840_SET_MONO\n"));
				break;
			}
		}

		if( 0 != (result = mxb->tda9840->driver->command(mxb->tda9840, TDA9840_SWITCH, &byte))) {
			printk("VIDIOC_S_TUNER error. result:%d, byte:%d\n",result,byte);
		}

		return 0;
	}
	case VIDIOC_G_FREQUENCY:
	{
		struct v4l2_frequency *f = arg;

		if(0 != mxb->cur_input) {
			DEB_D(("VIDIOC_G_FREQ: channel %d does not have a tuner!\n",mxb->cur_input));
			return -EINVAL;
		}

		*f = mxb->cur_freq;

		DEB_EE(("VIDIOC_G_FREQ: freq:0x%08x.\n", mxb->cur_freq.frequency));
		return 0;
	}
	case VIDIOC_S_FREQUENCY:
	{
		struct v4l2_frequency *f = arg;

		if (0 != f->tuner)
			return -EINVAL;

		if (V4L2_TUNER_ANALOG_TV != f->type)
			return -EINVAL;

		if(0 != mxb->cur_input) {
			DEB_D(("VIDIOC_S_FREQ: channel %d does not have a tuner!\n",mxb->cur_input));
			return -EINVAL;
		}

		mxb->cur_freq = *f;
		DEB_EE(("VIDIOC_S_FREQUENCY: freq:0x%08x.\n", mxb->cur_freq.frequency));

		/* tune in desired frequency */
		mxb->tuner->driver->command(mxb->tuner, VIDIOC_S_FREQUENCY, &mxb->cur_freq);

		/* hack: changing the frequency should invalidate the vbi-counter (=> alevt) */
		spin_lock(&dev->slock);
		vv->vbi_fieldcount = 0;
		spin_unlock(&dev->slock);

		return 0;
	}
	case MXB_S_AUDIO_CD:
	{
		int i = *(int*)arg;

		if( i < 0 || i >= MXB_AUDIOS ) {
			DEB_D(("illegal argument to MXB_S_AUDIO_CD: i:%d.\n",i));
			return -EINVAL;
		}

		DEB_EE(("MXB_S_AUDIO_CD: i:%d.\n",i));

		mxb->tea6420_1->driver->command(mxb->tea6420_1,TEA6420_SWITCH, &TEA6420_cd[i][0]);
		mxb->tea6420_2->driver->command(mxb->tea6420_2,TEA6420_SWITCH, &TEA6420_cd[i][1]);

		return 0;
	}
	case MXB_S_AUDIO_LINE:
	{
		int i = *(int*)arg;

		if( i < 0 || i >= MXB_AUDIOS ) {
			DEB_D(("illegal argument to MXB_S_AUDIO_LINE: i:%d.\n",i));
			return -EINVAL;
		}

		DEB_EE(("MXB_S_AUDIO_LINE: i:%d.\n",i));
		mxb->tea6420_1->driver->command(mxb->tea6420_1,TEA6420_SWITCH, &TEA6420_line[i][0]);
		mxb->tea6420_2->driver->command(mxb->tea6420_2,TEA6420_SWITCH, &TEA6420_line[i][1]);

		return 0;
	}
	case VIDIOC_G_AUDIO:
	{
		struct v4l2_audio *a = arg;

		if( a->index < 0 || a->index > MXB_INPUTS ) {
			DEB_D(("VIDIOC_G_AUDIO %d out of range.\n",a->index));
			return -EINVAL;
		}

		DEB_EE(("VIDIOC_G_AUDIO %d.\n",a->index));
		memcpy(a, &mxb_audios[video_audio_connect[mxb->cur_input]], sizeof(struct v4l2_audio));

		return 0;
	}
	case VIDIOC_S_AUDIO:
	{
		struct v4l2_audio *a = arg;
		DEB_D(("VIDIOC_S_AUDIO %d.\n",a->index));
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

static int std_callback(struct saa7146_dev* dev, struct saa7146_standard *std)
{
	struct mxb* mxb = (struct mxb*)dev->ext_priv;
	int zero = 0;
	int one = 1;

	if(V4L2_STD_PAL_I == std->id ) {
		v4l2_std_id std = V4L2_STD_PAL_I;
		DEB_D(("VIDIOC_S_STD: setting mxb for PAL_I.\n"));
		/* set the 7146 gpio register -- I don't know what this does exactly */
		saa7146_write(dev, GPIO_CTRL, 0x00404050);
		/* unset the 7111 gpio register -- I don't know what this does exactly */
		mxb->saa7111a->driver->command(mxb->saa7111a,DECODER_SET_GPIO, &zero);
		mxb->tuner->driver->command(mxb->tuner, VIDIOC_S_STD, &std);
	} else {
		v4l2_std_id std = V4L2_STD_PAL_BG;
		DEB_D(("VIDIOC_S_STD: setting mxb for PAL/NTSC/SECAM.\n"));
		/* set the 7146 gpio register -- I don't know what this does exactly */
		saa7146_write(dev, GPIO_CTRL, 0x00404050);
		/* set the 7111 gpio register -- I don't know what this does exactly */
		mxb->saa7111a->driver->command(mxb->saa7111a,DECODER_SET_GPIO, &one);
		mxb->tuner->driver->command(mxb->tuner, VIDIOC_S_STD, &std);
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
	.ioctls		= &ioctls[0],
	.ioctl		= mxb_ioctl,
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
	if( 0 != saa7146_register_extension(&extension)) {
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

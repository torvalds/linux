/*
 * Copyright (C) 2005-2006 Micronas USA Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License (Version 2) as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston MA 02111-1307, USA.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/videodev2.h>
#include <media/tuner.h>
#include <media/v4l2-common.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-device.h>
#include <linux/slab.h>

MODULE_DESCRIPTION("sony-btf-mpx driver");
MODULE_LICENSE("GPL v2");

static int debug;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "debug level 0=off(default) 1=on");

/* #define MPX_DEBUG */

/*
 * Note:
 *
 * AS(IF/MPX) pin:      LOW      HIGH/OPEN
 * IF/MPX address:   0x42/0x40   0x43/0x44
 */


static int force_mpx_mode = -1;
module_param(force_mpx_mode, int, 0644);

struct sony_btf_mpx {
	struct v4l2_subdev sd;
	int mpxmode;
	u32 audmode;
};

static inline struct sony_btf_mpx *to_state(struct v4l2_subdev *sd)
{
	return container_of(sd, struct sony_btf_mpx, sd);
}

static int mpx_write(struct i2c_client *client, int dev, int addr, int val)
{
	u8 buffer[5];
	struct i2c_msg msg;

	buffer[0] = dev;
	buffer[1] = addr >> 8;
	buffer[2] = addr & 0xff;
	buffer[3] = val >> 8;
	buffer[4] = val & 0xff;
	msg.addr = client->addr;
	msg.flags = 0;
	msg.len = 5;
	msg.buf = buffer;
	i2c_transfer(client->adapter, &msg, 1);
	return 0;
}

/*
 * MPX register values for the BTF-PG472Z:
 *
 *                                 FM_     NICAM_  SCART_
 *          MODUS  SOURCE    ACB   PRESCAL PRESCAL PRESCAL SYSTEM  VOLUME
 *         10/0030 12/0008 12/0013 12/000E 12/0010 12/0000 10/0020 12/0000
 *         ---------------------------------------------------------------
 * Auto     1003    0020    0100    2603    5000    XXXX    0001    7500
 *
 * B/G
 *  Mono    1003    0020    0100    2603    5000    XXXX    0003    7500
 *  A2      1003    0020    0100    2601    5000    XXXX    0003    7500
 *  NICAM   1003    0120    0100    2603    5000    XXXX    0008    7500
 *
 * I
 *  Mono    1003    0020    0100    2603    7900    XXXX    000A    7500
 *  NICAM   1003    0120    0100    2603    7900    XXXX    000A    7500
 *
 * D/K
 *  Mono    1003    0020    0100    2603    5000    XXXX    0004    7500
 *  A2-1    1003    0020    0100    2601    5000    XXXX    0004    7500
 *  A2-2    1003    0020    0100    2601    5000    XXXX    0005    7500
 *  A2-3    1003    0020    0100    2601    5000    XXXX    0007    7500
 *  NICAM   1003    0120    0100    2603    5000    XXXX    000B    7500
 *
 * L/L'
 *  Mono    0003    0200    0100    7C03    5000    2200    0009    7500
 *  NICAM   0003    0120    0100    7C03    5000    XXXX    0009    7500
 *
 * M
 *  Mono    1003    0200    0100    2B03    5000    2B00    0002    7500
 *
 * For Asia, replace the 0x26XX in FM_PRESCALE with 0x14XX.
 *
 * Bilingual selection in A2/NICAM:
 *
 *         High byte of SOURCE     Left chan   Right chan
 *                 0x01              MAIN         SUB
 *                 0x03              MAIN         MAIN
 *                 0x04              SUB          SUB
 *
 * Force mono in NICAM by setting the high byte of SOURCE to 0x02 (L/L') or
 * 0x00 (all other bands).  Force mono in A2 with FMONO_A2:
 *
 *                      FMONO_A2
 *                      10/0022
 *                      --------
 *     Forced mono ON     07F0
 *     Forced mono OFF    0190
 */

static const struct {
	enum { AUD_MONO, AUD_A2, AUD_NICAM, AUD_NICAM_L } audio_mode;
	u16 modus;
	u16 source;
	u16 acb;
	u16 fm_prescale;
	u16 nicam_prescale;
	u16 scart_prescale;
	u16 system;
	u16 volume;
} mpx_audio_modes[] = {
	/* Auto */	{ AUD_MONO,	0x1003, 0x0020, 0x0100, 0x2603,
					0x5000, 0x0000, 0x0001, 0x7500 },
	/* B/G Mono */	{ AUD_MONO,	0x1003, 0x0020, 0x0100, 0x2603,
					0x5000, 0x0000, 0x0003, 0x7500 },
	/* B/G A2 */	{ AUD_A2,	0x1003, 0x0020, 0x0100, 0x2601,
					0x5000, 0x0000, 0x0003, 0x7500 },
	/* B/G NICAM */ { AUD_NICAM,	0x1003, 0x0120, 0x0100, 0x2603,
					0x5000, 0x0000, 0x0008, 0x7500 },
	/* I Mono */	{ AUD_MONO,	0x1003, 0x0020, 0x0100, 0x2603,
					0x7900, 0x0000, 0x000A, 0x7500 },
	/* I NICAM */	{ AUD_NICAM,	0x1003, 0x0120, 0x0100, 0x2603,
					0x7900, 0x0000, 0x000A, 0x7500 },
	/* D/K Mono */	{ AUD_MONO,	0x1003, 0x0020, 0x0100, 0x2603,
					0x5000, 0x0000, 0x0004, 0x7500 },
	/* D/K A2-1 */	{ AUD_A2,	0x1003, 0x0020, 0x0100, 0x2601,
					0x5000, 0x0000, 0x0004, 0x7500 },
	/* D/K A2-2 */	{ AUD_A2,	0x1003, 0x0020, 0x0100, 0x2601,
					0x5000, 0x0000, 0x0005, 0x7500 },
	/* D/K A2-3 */	{ AUD_A2,	0x1003, 0x0020, 0x0100, 0x2601,
					0x5000, 0x0000, 0x0007, 0x7500 },
	/* D/K NICAM */	{ AUD_NICAM,	0x1003, 0x0120, 0x0100, 0x2603,
					0x5000, 0x0000, 0x000B, 0x7500 },
	/* L/L' Mono */	{ AUD_MONO,	0x0003, 0x0200, 0x0100, 0x7C03,
					0x5000, 0x2200, 0x0009, 0x7500 },
	/* L/L' NICAM */{ AUD_NICAM_L,	0x0003, 0x0120, 0x0100, 0x7C03,
					0x5000, 0x0000, 0x0009, 0x7500 },
};

#define MPX_NUM_MODES	ARRAY_SIZE(mpx_audio_modes)

static int mpx_setup(struct sony_btf_mpx *t)
{
	struct i2c_client *client = v4l2_get_subdevdata(&t->sd);
	u16 source = 0;
	u8 buffer[3];
	struct i2c_msg msg;
	int mode = t->mpxmode;

	/* reset MPX */
	buffer[0] = 0x00;
	buffer[1] = 0x80;
	buffer[2] = 0x00;
	msg.addr = client->addr;
	msg.flags = 0;
	msg.len = 3;
	msg.buf = buffer;
	i2c_transfer(client->adapter, &msg, 1);
	buffer[1] = 0x00;
	i2c_transfer(client->adapter, &msg, 1);

	if (t->audmode != V4L2_TUNER_MODE_MONO)
		mode++;

	if (mpx_audio_modes[mode].audio_mode != AUD_MONO) {
		switch (t->audmode) {
		case V4L2_TUNER_MODE_MONO:
			switch (mpx_audio_modes[mode].audio_mode) {
			case AUD_A2:
				source = mpx_audio_modes[mode].source;
				break;
			case AUD_NICAM:
				source = 0x0000;
				break;
			case AUD_NICAM_L:
				source = 0x0200;
				break;
			default:
				break;
			}
			break;
		case V4L2_TUNER_MODE_STEREO:
			source = mpx_audio_modes[mode].source;
			break;
		case V4L2_TUNER_MODE_LANG1:
			source = 0x0300;
			break;
		case V4L2_TUNER_MODE_LANG2:
			source = 0x0400;
			break;
		}
		source |= mpx_audio_modes[mode].source & 0x00ff;
	} else
		source = mpx_audio_modes[mode].source;

	mpx_write(client, 0x10, 0x0030, mpx_audio_modes[mode].modus);
	mpx_write(client, 0x12, 0x0008, source);
	mpx_write(client, 0x12, 0x0013, mpx_audio_modes[mode].acb);
	mpx_write(client, 0x12, 0x000e,
			mpx_audio_modes[mode].fm_prescale);
	mpx_write(client, 0x12, 0x0010,
			mpx_audio_modes[mode].nicam_prescale);
	mpx_write(client, 0x12, 0x000d,
			mpx_audio_modes[mode].scart_prescale);
	mpx_write(client, 0x10, 0x0020, mpx_audio_modes[mode].system);
	mpx_write(client, 0x12, 0x0000, mpx_audio_modes[mode].volume);
	if (mpx_audio_modes[mode].audio_mode == AUD_A2)
		mpx_write(client, 0x10, 0x0022,
			t->audmode == V4L2_TUNER_MODE_MONO ? 0x07f0 : 0x0190);

#ifdef MPX_DEBUG
	{
		u8 buf1[3], buf2[2];
		struct i2c_msg msgs[2];

		v4l2_info(client,
			"MPX registers: %04x %04x %04x %04x %04x %04x %04x %04x\n",
			mpx_audio_modes[mode].modus,
			source,
			mpx_audio_modes[mode].acb,
			mpx_audio_modes[mode].fm_prescale,
			mpx_audio_modes[mode].nicam_prescale,
			mpx_audio_modes[mode].scart_prescale,
			mpx_audio_modes[mode].system,
			mpx_audio_modes[mode].volume);
		buf1[0] = 0x11;
		buf1[1] = 0x00;
		buf1[2] = 0x7e;
		msgs[0].addr = client->addr;
		msgs[0].flags = 0;
		msgs[0].len = 3;
		msgs[0].buf = buf1;
		msgs[1].addr = client->addr;
		msgs[1].flags = I2C_M_RD;
		msgs[1].len = 2;
		msgs[1].buf = buf2;
		i2c_transfer(client->adapter, msgs, 2);
		v4l2_info(client, "MPX system: %02x%02x\n",
				buf2[0], buf2[1]);
		buf1[0] = 0x11;
		buf1[1] = 0x02;
		buf1[2] = 0x00;
		i2c_transfer(client->adapter, msgs, 2);
		v4l2_info(client, "MPX status: %02x%02x\n",
				buf2[0], buf2[1]);
	}
#endif
	return 0;
}


static int sony_btf_mpx_s_std(struct v4l2_subdev *sd, v4l2_std_id std)
{
	struct sony_btf_mpx *t = to_state(sd);
	int default_mpx_mode = 0;

	if (std & V4L2_STD_PAL_BG)
		default_mpx_mode = 1;
	else if (std & V4L2_STD_PAL_I)
		default_mpx_mode = 4;
	else if (std & V4L2_STD_PAL_DK)
		default_mpx_mode = 6;
	else if (std & V4L2_STD_SECAM_L)
		default_mpx_mode = 11;

	if (default_mpx_mode != t->mpxmode) {
		t->mpxmode = default_mpx_mode;
		mpx_setup(t);
	}
	return 0;
}

static int sony_btf_mpx_g_tuner(struct v4l2_subdev *sd, struct v4l2_tuner *vt)
{
	struct sony_btf_mpx *t = to_state(sd);

	vt->capability = V4L2_TUNER_CAP_NORM |
		V4L2_TUNER_CAP_STEREO | V4L2_TUNER_CAP_LANG1 |
		V4L2_TUNER_CAP_LANG2;
	vt->rxsubchans = V4L2_TUNER_SUB_MONO |
		V4L2_TUNER_SUB_STEREO | V4L2_TUNER_SUB_LANG1 |
		V4L2_TUNER_SUB_LANG2;
	vt->audmode = t->audmode;
	return 0;
}

static int sony_btf_mpx_s_tuner(struct v4l2_subdev *sd, const struct v4l2_tuner *vt)
{
	struct sony_btf_mpx *t = to_state(sd);

	if (vt->type != V4L2_TUNER_ANALOG_TV)
		return -EINVAL;

	if (vt->audmode != t->audmode) {
		t->audmode = vt->audmode;
		mpx_setup(t);
	}
	return 0;
}

/* --------------------------------------------------------------------------*/

static const struct v4l2_subdev_core_ops sony_btf_mpx_core_ops = {
	.s_std = sony_btf_mpx_s_std,
};

static const struct v4l2_subdev_tuner_ops sony_btf_mpx_tuner_ops = {
	.s_tuner = sony_btf_mpx_s_tuner,
	.g_tuner = sony_btf_mpx_g_tuner,
};

static const struct v4l2_subdev_ops sony_btf_mpx_ops = {
	.core = &sony_btf_mpx_core_ops,
	.tuner = &sony_btf_mpx_tuner_ops,
};

/* --------------------------------------------------------------------------*/

static int sony_btf_mpx_probe(struct i2c_client *client,
				const struct i2c_device_id *id)
{
	struct sony_btf_mpx *t;
	struct v4l2_subdev *sd;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_I2C_BLOCK))
		return -ENODEV;

	v4l_info(client, "chip found @ 0x%x (%s)\n",
			client->addr << 1, client->adapter->name);

	t = devm_kzalloc(&client->dev, sizeof(*t), GFP_KERNEL);
	if (t == NULL)
		return -ENOMEM;

	sd = &t->sd;
	v4l2_i2c_subdev_init(sd, client, &sony_btf_mpx_ops);

	/* Initialize sony_btf_mpx */
	t->mpxmode = 0;
	t->audmode = V4L2_TUNER_MODE_STEREO;

	return 0;
}

static int sony_btf_mpx_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);

	v4l2_device_unregister_subdev(sd);

	return 0;
}

/* ----------------------------------------------------------------------- */

static const struct i2c_device_id sony_btf_mpx_id[] = {
	{ "sony-btf-mpx", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, sony_btf_mpx_id);

static struct i2c_driver sony_btf_mpx_driver = {
	.driver = {
		.owner	= THIS_MODULE,
		.name	= "sony-btf-mpx",
	},
	.probe = sony_btf_mpx_probe,
	.remove = sony_btf_mpx_remove,
	.id_table = sony_btf_mpx_id,
};
module_i2c_driver(sony_btf_mpx_driver);

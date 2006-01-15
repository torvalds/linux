/*
 * Programming the mspx4xx sound processor family
 *
 * (c) 1997-2001 Gerd Knorr <kraxel@bytesex.org>
 *
 * what works and what doesn't:
 *
 *  AM-Mono
 *      Support for Hauppauge cards added (decoding handled by tuner) added by
 *      Frederic Crozat <fcrozat@mail.dotcom.fr>
 *
 *  FM-Mono
 *      should work. The stereo modes are backward compatible to FM-mono,
 *      therefore FM-Mono should be allways available.
 *
 *  FM-Stereo (B/G, used in germany)
 *      should work, with autodetect
 *
 *  FM-Stereo (satellite)
 *      should work, no autodetect (i.e. default is mono, but you can
 *      switch to stereo -- untested)
 *
 *  NICAM (B/G, L , used in UK, Scandinavia, Spain and France)
 *      should work, with autodetect. Support for NICAM was added by
 *      Pekka Pietikainen <pp@netppl.fi>
 *
 * TODO:
 *   - better SAT support
 *
 * 980623  Thomas Sailer (sailer@ife.ee.ethz.ch)
 *         using soundcore instead of OSS
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */


#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/videodev.h>
#include <linux/videodev2.h>
#include <media/v4l2-common.h>
#include <media/audiochip.h>
#include <linux/kthread.h>
#include <linux/suspend.h>
#include "msp3400.h"

/* ---------------------------------------------------------------------- */

MODULE_DESCRIPTION("device driver for msp34xx TV sound processor");
MODULE_AUTHOR("Gerd Knorr");
MODULE_LICENSE("GPL");

/* module parameters */
static int opmode   = OPMODE_AUTO;
int msp_debug;		 /* msp_debug output */
int msp_once;		 /* no continous stereo monitoring */
int msp_amsound;	 /* hard-wire AM sound at 6.5 Hz (france),
			    the autoscan seems work well only with FM... */
int msp_standard = 1;    /* Override auto detect of audio msp_standard, if needed. */
int msp_dolby;

int msp_stereo_thresh = 0x190; /* a2 threshold for stereo/bilingual
					(msp34xxg only) 0x00a0-0x03c0 */

/* read-only */
module_param(opmode,           int, 0444);

/* read-write */
module_param_named(once,msp_once,                      bool, 0644);
module_param_named(debug,msp_debug,                    int,  0644);
module_param_named(stereo_threshold,msp_stereo_thresh, int,  0644);
module_param_named(standard,msp_standard,              int,  0644);
module_param_named(amsound,msp_amsound,                bool, 0644);
module_param_named(dolby,msp_dolby,                    bool, 0644);

MODULE_PARM_DESC(opmode, "Forces a MSP3400 opmode. 0=Manual, 1=Autodetect, 2=Autodetect and autoselect");
MODULE_PARM_DESC(once, "No continuous stereo monitoring");
MODULE_PARM_DESC(debug, "Enable debug messages [0-3]");
MODULE_PARM_DESC(stereo_threshold, "Sets signal threshold to activate stereo");
MODULE_PARM_DESC(standard, "Specify audio standard: 32 = NTSC, 64 = radio, Default: Autodetect");
MODULE_PARM_DESC(amsound, "Hardwire AM sound at 6.5Hz (France), FM can autoscan");
MODULE_PARM_DESC(dolby, "Activates Dolby processsing");

/* ---------------------------------------------------------------------- */

/* control subaddress */
#define I2C_MSP_CONTROL 0x00
/* demodulator unit subaddress */
#define I2C_MSP_DEM     0x10
/* DSP unit subaddress */
#define I2C_MSP_DSP     0x12

/* Addresses to scan */
static unsigned short normal_i2c[] = { 0x80 >> 1, 0x88 >> 1, I2C_CLIENT_END };
I2C_CLIENT_INSMOD;

/* ----------------------------------------------------------------------- */
/* functions for talking to the MSP3400C Sound processor                   */

int msp_reset(struct i2c_client *client)
{
	/* reset and read revision code */
	static u8 reset_off[3] = { I2C_MSP_CONTROL, 0x80, 0x00 };
	static u8 reset_on[3]  = { I2C_MSP_CONTROL, 0x00, 0x00 };
	static u8 write[3]     = { I2C_MSP_DSP + 1, 0x00, 0x1e };
	u8 read[2];
	struct i2c_msg reset[2] = {
		{ client->addr, I2C_M_IGNORE_NAK, 3, reset_off },
		{ client->addr, I2C_M_IGNORE_NAK, 3, reset_on  },
	};
	struct i2c_msg test[2] = {
		{ client->addr, 0,        3, write },
		{ client->addr, I2C_M_RD, 2, read  },
	};

	v4l_dbg(3, msp_debug, client, "msp_reset\n");
	if (i2c_transfer(client->adapter, &reset[0], 1) != 1 ||
	    i2c_transfer(client->adapter, &reset[1], 1) != 1 ||
	    i2c_transfer(client->adapter, test, 2) != 2) {
		v4l_err(client, "chip reset failed\n");
		return -1;
	}
	return 0;
}

static int msp_read(struct i2c_client *client, int dev, int addr)
{
	int err, retval;
	u8 write[3];
	u8 read[2];
	struct i2c_msg msgs[2] = {
		{ client->addr, 0,        3, write },
		{ client->addr, I2C_M_RD, 2, read  }
	};

	write[0] = dev + 1;
	write[1] = addr >> 8;
	write[2] = addr & 0xff;

	for (err = 0; err < 3; err++) {
		if (i2c_transfer(client->adapter, msgs, 2) == 2)
			break;
		v4l_warn(client, "I/O error #%d (read 0x%02x/0x%02x)\n", err,
		       dev, addr);
		current->state = TASK_INTERRUPTIBLE;
		schedule_timeout(msecs_to_jiffies(10));
	}
	if (err == 3) {
		v4l_warn(client, "giving up, resetting chip. Sound will go off, sorry folks :-|\n");
		msp_reset(client);
		return -1;
	}
	retval = read[0] << 8 | read[1];
	v4l_dbg(3, msp_debug, client, "msp_read(0x%x, 0x%x): 0x%x\n", dev, addr, retval);
	return retval;
}

int msp_read_dem(struct i2c_client *client, int addr)
{
	return msp_read(client, I2C_MSP_DEM, addr);
}

int msp_read_dsp(struct i2c_client *client, int addr)
{
	return msp_read(client, I2C_MSP_DSP, addr);
}

static int msp_write(struct i2c_client *client, int dev, int addr, int val)
{
	int err;
	u8 buffer[5];

	buffer[0] = dev;
	buffer[1] = addr >> 8;
	buffer[2] = addr &  0xff;
	buffer[3] = val  >> 8;
	buffer[4] = val  &  0xff;

	v4l_dbg(3, msp_debug, client, "msp_write(0x%x, 0x%x, 0x%x)\n", dev, addr, val);
	for (err = 0; err < 3; err++) {
		if (i2c_master_send(client, buffer, 5) == 5)
			break;
		v4l_warn(client, "I/O error #%d (write 0x%02x/0x%02x)\n", err,
		       dev, addr);
		current->state = TASK_INTERRUPTIBLE;
		schedule_timeout(msecs_to_jiffies(10));
	}
	if (err == 3) {
		v4l_warn(client, "giving up, resetting chip. Sound will go off, sorry folks :-|\n");
		msp_reset(client);
		return -1;
	}
	return 0;
}

int msp_write_dem(struct i2c_client *client, int addr, int val)
{
	return msp_write(client, I2C_MSP_DEM, addr, val);
}

int msp_write_dsp(struct i2c_client *client, int addr, int val)
{
	return msp_write(client, I2C_MSP_DSP, addr, val);
}

/* ----------------------------------------------------------------------- *
 * bits  9  8  5 - SCART DSP input Select:
 *       0  0  0 - SCART 1 to DSP input (reset position)
 *       0  1  0 - MONO to DSP input
 *       1  0  0 - SCART 2 to DSP input
 *       1  1  1 - Mute DSP input
 *
 * bits 11 10  6 - SCART 1 Output Select:
 *       0  0  0 - undefined (reset position)
 *       0  1  0 - SCART 2 Input to SCART 1 Output (for devices with 2 SCARTS)
 *       1  0  0 - MONO input to SCART 1 Output
 *       1  1  0 - SCART 1 DA to SCART 1 Output
 *       0  0  1 - SCART 2 DA to SCART 1 Output
 *       0  1  1 - SCART 1 Input to SCART 1 Output
 *       1  1  1 - Mute SCART 1 Output
 *
 * bits 13 12  7 - SCART 2 Output Select (for devices with 2 Output SCART):
 *       0  0  0 - SCART 1 DA to SCART 2 Output (reset position)
 *       0  1  0 - SCART 1 Input to SCART 2 Output
 *       1  0  0 - MONO input to SCART 2 Output
 *       0  0  1 - SCART 2 DA to SCART 2 Output
 *       0  1  1 - SCART 2 Input to SCART 2 Output
 *       1  1  0 - Mute SCART 2 Output
 *
 * Bits 4 to 0 should be zero.
 * ----------------------------------------------------------------------- */

static int scarts[3][9] = {
	/* MASK    IN1     IN2     IN1_DA  IN2_DA  IN3     IN4     MONO    MUTE   */
	/* SCART DSP Input select */
	{ 0x0320, 0x0000, 0x0200, -1,     -1,     0x0300, 0x0020, 0x0100, 0x0320 },
	/* SCART1 Output select */
	{ 0x0c40, 0x0440, 0x0400, 0x0c00, 0x0040, 0x0000, 0x0840, 0x0800, 0x0c40 },
	/* SCART2 Output select */
	{ 0x3080, 0x1000, 0x1080, 0x0000, 0x0080, 0x2080, 0x3080, 0x2000, 0x3000 },
};

static char *scart_names[] = {
	"mask", "in1", "in2", "in1 da", "in2 da", "in3", "in4", "mono", "mute"
};

void msp_set_scart(struct i2c_client *client, int in, int out)
{
	struct msp_state *state = i2c_get_clientdata(client);

	state->in_scart=in;

	if (in >= 1 && in <= 8 && out >= 0 && out <= 2) {
		if (-1 == scarts[out][in])
			return;

		state->acb &= ~scarts[out][SCART_MASK];
		state->acb |=  scarts[out][in];
	} else
		state->acb = 0xf60; /* Mute Input and SCART 1 Output */

	v4l_dbg(1, msp_debug, client, "scart switch: %s => %d (ACB=0x%04x)\n",
						scart_names[in], out, state->acb);
	msp_write_dsp(client, 0x13, state->acb);

	/* Sets I2S speed 0 = 1.024 Mbps, 1 = 2.048 Mbps */
	msp_write_dem(client, 0x40, state->i2s_mode);
}

void msp_set_mute(struct i2c_client *client)
{
	struct msp_state *state = i2c_get_clientdata(client);

	v4l_dbg(1, msp_debug, client, "mute audio\n");
	msp_write_dsp(client, 0x0000, 0);
	msp_write_dsp(client, 0x0007, 1);
	if (state->has_scart2_out_volume)
		msp_write_dsp(client, 0x0040, 1);
	if (state->has_headphones)
		msp_write_dsp(client, 0x0006, 0);
}

void msp_set_audio(struct i2c_client *client)
{
	struct msp_state *state = i2c_get_clientdata(client);
	int bal = 0, bass, treble, loudness;
	int val = 0;

	if (!state->muted)
		val = (state->volume * 0x7f / 65535) << 8;

	v4l_dbg(1, msp_debug, client, "mute=%s volume=%d\n",
		state->muted ? "on" : "off", state->volume);

	msp_write_dsp(client, 0x0000, val);
	msp_write_dsp(client, 0x0007, state->muted ? 0x1 : (val | 0x1));
	if (state->has_scart2_out_volume)
		msp_write_dsp(client, 0x0040, state->muted ? 0x1 : (val | 0x1));
	if (state->has_headphones)
		msp_write_dsp(client, 0x0006, val);
	if (!state->has_sound_processing)
		return;

	if (val)
		bal = (u8)((state->balance / 256) - 128);
	bass = ((state->bass - 32768) * 0x60 / 65535) << 8;
	treble = ((state->treble - 32768) * 0x60 / 65535) << 8;
	loudness = state->loudness ? ((5 * 4) << 8) : 0;

	v4l_dbg(1, msp_debug, client, "balance=%d bass=%d treble=%d loudness=%d\n",
		state->balance, state->bass, state->treble, state->loudness);

	msp_write_dsp(client, 0x0001, bal << 8);
	msp_write_dsp(client, 0x0002, bass);
	msp_write_dsp(client, 0x0003, treble);
	msp_write_dsp(client, 0x0004, loudness);
	if (!state->has_headphones)
		return;
	msp_write_dsp(client, 0x0030, bal << 8);
	msp_write_dsp(client, 0x0031, bass);
	msp_write_dsp(client, 0x0032, treble);
	msp_write_dsp(client, 0x0033, loudness);
}

int msp_modus(struct i2c_client *client)
{
	struct msp_state *state = i2c_get_clientdata(client);

	if (state->radio) {
		v4l_dbg(1, msp_debug, client, "video mode selected to Radio\n");
		return 0x0003;
	}

	if (state->v4l2_std & V4L2_STD_PAL) {
		v4l_dbg(1, msp_debug, client, "video mode selected to PAL\n");

#if 1
		/* experimental: not sure this works with all chip versions */
		return 0x7003;
#else
		/* previous value, try this if it breaks ... */
		return 0x1003;
#endif
	}
	if (state->v4l2_std & V4L2_STD_NTSC) {
		v4l_dbg(1, msp_debug, client, "video mode selected to NTSC\n");
		return 0x2003;
	}
	if (state->v4l2_std & V4L2_STD_SECAM) {
		v4l_dbg(1, msp_debug, client, "video mode selected to SECAM\n");
		return 0x0003;
	}
	return 0x0003;
}

/* ------------------------------------------------------------------------ */


static void msp_wake_thread(struct i2c_client *client)
{
	struct msp_state *state = i2c_get_clientdata(client);

	if (NULL == state->kthread)
		return;
	msp_set_mute(client);
	state->watch_stereo = 0;
	state->restart = 1;
	wake_up_interruptible(&state->wq);
}

int msp_sleep(struct msp_state *state, int timeout)
{
	DECLARE_WAITQUEUE(wait, current);

	add_wait_queue(&state->wq, &wait);
	if (!kthread_should_stop()) {
		if (timeout < 0) {
			set_current_state(TASK_INTERRUPTIBLE);
			schedule();
		} else {
			schedule_timeout_interruptible
						(msecs_to_jiffies(timeout));
		}
	}

	remove_wait_queue(&state->wq, &wait);
	try_to_freeze();
	return state->restart;
}

/* ------------------------------------------------------------------------ */

static int msp_mode_v4l2_to_v4l1(int rxsubchans)
{
	int mode = 0;

	if (rxsubchans & V4L2_TUNER_SUB_STEREO)
		mode |= VIDEO_SOUND_STEREO;
	if (rxsubchans & V4L2_TUNER_SUB_LANG2)
		mode |= VIDEO_SOUND_LANG2;
	if (rxsubchans & V4L2_TUNER_SUB_LANG1)
		mode |= VIDEO_SOUND_LANG1;
	if (mode == 0)
		mode |= VIDEO_SOUND_MONO;
	return mode;
}

static int msp_mode_v4l1_to_v4l2(int mode)
{
	if (mode & VIDEO_SOUND_STEREO)
		return V4L2_TUNER_MODE_STEREO;
	if (mode & VIDEO_SOUND_LANG2)
		return V4L2_TUNER_MODE_LANG2;
	if (mode & VIDEO_SOUND_LANG1)
		return V4L2_TUNER_MODE_LANG1;
	return V4L2_TUNER_MODE_MONO;
}

static void msp_any_detect_stereo(struct i2c_client *client)
{
	struct msp_state *state  = i2c_get_clientdata(client);

	switch (state->opmode) {
	case OPMODE_MANUAL:
	case OPMODE_AUTODETECT:
		autodetect_stereo(client);
		break;
	case OPMODE_AUTOSELECT:
		msp34xxg_detect_stereo(client);
		break;
	}
}

static struct v4l2_queryctrl msp_qctrl_std[] = {
	{
		.id            = V4L2_CID_AUDIO_VOLUME,
		.name          = "Volume",
		.minimum       = 0,
		.maximum       = 65535,
		.step          = 65535/100,
		.default_value = 58880,
		.flags         = 0,
		.type          = V4L2_CTRL_TYPE_INTEGER,
	},{
		.id            = V4L2_CID_AUDIO_MUTE,
		.name          = "Mute",
		.minimum       = 0,
		.maximum       = 1,
		.step          = 1,
		.default_value = 1,
		.flags         = 0,
		.type          = V4L2_CTRL_TYPE_BOOLEAN,
	},
};

static struct v4l2_queryctrl msp_qctrl_sound_processing[] = {
	{
		.id            = V4L2_CID_AUDIO_BALANCE,
		.name          = "Balance",
		.minimum       = 0,
		.maximum       = 65535,
		.step          = 65535/100,
		.default_value = 32768,
		.flags         = 0,
		.type          = V4L2_CTRL_TYPE_INTEGER,
	},{
		.id            = V4L2_CID_AUDIO_BASS,
		.name          = "Bass",
		.minimum       = 0,
		.maximum       = 65535,
		.step          = 65535/100,
		.default_value = 32768,
		.type          = V4L2_CTRL_TYPE_INTEGER,
	},{
		.id            = V4L2_CID_AUDIO_TREBLE,
		.name          = "Treble",
		.minimum       = 0,
		.maximum       = 65535,
		.step          = 65535/100,
		.default_value = 32768,
		.type          = V4L2_CTRL_TYPE_INTEGER,
	},{
		.id            = V4L2_CID_AUDIO_LOUDNESS,
		.name          = "Loudness",
		.minimum       = 0,
		.maximum       = 1,
		.step          = 1,
		.default_value = 1,
		.flags         = 0,
		.type          = V4L2_CTRL_TYPE_BOOLEAN,
	},
};


static void msp_any_set_audmode(struct i2c_client *client, int audmode)
{
	struct msp_state *state = i2c_get_clientdata(client);

	switch (state->opmode) {
	case OPMODE_MANUAL:
	case OPMODE_AUTODETECT:
		state->watch_stereo = 0;
		msp3400c_setstereo(client, audmode);
		break;
	case OPMODE_AUTOSELECT:
		msp34xxg_set_audmode(client, audmode);
		break;
	}
}

static int msp_get_ctrl(struct i2c_client *client, struct v4l2_control *ctrl)
{
	struct msp_state *state = i2c_get_clientdata(client);

	switch (ctrl->id) {
	case V4L2_CID_AUDIO_VOLUME:
		ctrl->value = state->volume;
		break;

	case V4L2_CID_AUDIO_MUTE:
		ctrl->value = state->muted;
		break;

	case V4L2_CID_AUDIO_BALANCE:
		if (!state->has_sound_processing)
			return -EINVAL;
		ctrl->value = state->balance;
		break;

	case V4L2_CID_AUDIO_BASS:
		if (!state->has_sound_processing)
			return -EINVAL;
		ctrl->value = state->bass;
		break;

	case V4L2_CID_AUDIO_TREBLE:
		if (!state->has_sound_processing)
			return -EINVAL;
		ctrl->value = state->treble;
		break;

	case V4L2_CID_AUDIO_LOUDNESS:
		if (!state->has_sound_processing)
			return -EINVAL;
		ctrl->value = state->loudness;
		break;

	default:
		return -EINVAL;
	}
	return 0;
}

static int msp_set_ctrl(struct i2c_client *client, struct v4l2_control *ctrl)
{
	struct msp_state *state = i2c_get_clientdata(client);

	switch (ctrl->id) {
	case V4L2_CID_AUDIO_VOLUME:
		state->volume = ctrl->value;
		if (state->volume == 0)
			state->balance = 32768;
		break;

	case V4L2_CID_AUDIO_MUTE:
		if (ctrl->value < 0 || ctrl->value >= 2)
			return -ERANGE;
		state->muted = ctrl->value;
		break;

	case V4L2_CID_AUDIO_BASS:
		if (!state->has_sound_processing)
			return -EINVAL;
		state->bass = ctrl->value;
		break;

	case V4L2_CID_AUDIO_TREBLE:
		if (!state->has_sound_processing)
			return -EINVAL;
		state->treble = ctrl->value;
		break;

	case V4L2_CID_AUDIO_LOUDNESS:
		if (!state->has_sound_processing)
			return -EINVAL;
		state->loudness = ctrl->value;
		break;

	case V4L2_CID_AUDIO_BALANCE:
		if (!state->has_sound_processing)
			return -EINVAL;
		state->balance = ctrl->value;
		break;

	default:
		return -EINVAL;
	}
	msp_set_audio(client);
	return 0;
}

static int msp_command(struct i2c_client *client, unsigned int cmd, void *arg)
{
	struct msp_state *state = i2c_get_clientdata(client);
	u16 *sarg = arg;
	int scart = 0;

	if (msp_debug >= 2)
		v4l_i2c_print_ioctl(client, cmd);

	switch (cmd) {
	case AUDC_SET_INPUT:
		if (*sarg == state->input)
			break;
		state->input = *sarg;
		switch (*sarg) {
		case AUDIO_RADIO:
			/* Hauppauge uses IN2 for the radio */
			state->mode = MSP_MODE_FM_RADIO;
			scart       = SCART_IN2;
			break;
		case AUDIO_EXTERN_1:
			/* IN1 is often used for external input ... */
			state->mode = MSP_MODE_EXTERN;
			scart       = SCART_IN1;
			break;
		case AUDIO_EXTERN_2:
			/* ... sometimes it is IN2 through ;) */
			state->mode = MSP_MODE_EXTERN;
			scart       = SCART_IN2;
			break;
		case AUDIO_TUNER:
			state->mode = -1;
			break;
		default:
			if (*sarg & AUDIO_MUTE)
				msp_set_scart(client, SCART_MUTE, 0);
			break;
		}
		if (scart) {
			state->rxsubchans = V4L2_TUNER_SUB_STEREO;
			state->audmode = V4L2_TUNER_MODE_STEREO;
			msp_set_scart(client, scart, 0);
			msp_write_dsp(client, 0x000d, 0x1900);
			if (state->opmode != OPMODE_AUTOSELECT)
				msp3400c_setstereo(client, state->audmode);
		}
		msp_wake_thread(client);
		break;

	case AUDC_SET_RADIO:
		if (state->radio)
			return 0;
		state->radio = 1;
		v4l_dbg(1, msp_debug, client, "switching to radio mode\n");
		state->watch_stereo = 0;
		switch (state->opmode) {
		case OPMODE_MANUAL:
			/* set msp3400 to FM radio mode */
			msp3400c_setmode(client, MSP_MODE_FM_RADIO);
			msp3400c_setcarrier(client, MSP_CARRIER(10.7),
					    MSP_CARRIER(10.7));
			msp_set_audio(client);
			break;
		case OPMODE_AUTODETECT:
		case OPMODE_AUTOSELECT:
			/* the thread will do for us */
			msp_wake_thread(client);
			break;
		}
		break;

	/* --- v4l ioctls --- */
	/* take care: bttv does userspace copying, we'll get a
	   kernel pointer here... */
	case VIDIOCGAUDIO:
	{
		struct video_audio *va = arg;

		va->flags |= VIDEO_AUDIO_VOLUME | VIDEO_AUDIO_MUTABLE;
		if (state->has_sound_processing)
			va->flags |= VIDEO_AUDIO_BALANCE |
				VIDEO_AUDIO_BASS |
				VIDEO_AUDIO_TREBLE;
		if (state->muted)
			va->flags |= VIDEO_AUDIO_MUTE;
		va->volume = state->volume;
		va->balance = state->volume ? state->balance : 32768;
		va->bass = state->bass;
		va->treble = state->treble;

		if (state->radio)
			break;
		if (state->opmode == OPMODE_AUTOSELECT)
			msp_any_detect_stereo(client);
		va->mode = msp_mode_v4l2_to_v4l1(state->rxsubchans);
		break;
	}

	case VIDIOCSAUDIO:
	{
		struct video_audio *va = arg;

		state->muted = (va->flags & VIDEO_AUDIO_MUTE);
		state->volume = va->volume;
		state->balance = va->balance;
		state->bass = va->bass;
		state->treble = va->treble;
		msp_set_audio(client);

		if (va->mode != 0 && state->radio == 0)
			msp_any_set_audmode(client, msp_mode_v4l1_to_v4l2(va->mode));
		break;
	}

	case VIDIOCSCHAN:
	{
		struct video_channel *vc = arg;
		int update = 0;
		v4l2_std_id std;

		if (state->radio)
			update = 1;
		state->radio = 0;
		if (vc->norm == VIDEO_MODE_PAL)
			std = V4L2_STD_PAL;
		else if (vc->norm == VIDEO_MODE_SECAM)
			std = V4L2_STD_SECAM;
		else
			std = V4L2_STD_NTSC;
		if (std != state->v4l2_std) {
			state->v4l2_std = std;
			update = 1;
		}
		if (update)
			msp_wake_thread(client);
		break;
	}

	case VIDIOCSFREQ:
	case VIDIOC_S_FREQUENCY:
	{
		/* new channel -- kick audio carrier scan */
		msp_wake_thread(client);
		break;
	}

	/* msp34xx specific */
	case MSP_SET_MATRIX:
	{
		struct msp_matrix *mspm = arg;

		msp_set_scart(client, mspm->input, mspm->output);
		break;
	}

	/* --- v4l2 ioctls --- */
	case VIDIOC_S_STD:
	{
		v4l2_std_id *id = arg;
		int update = state->radio || state->v4l2_std != *id;

		state->v4l2_std = *id;
		state->radio = 0;
		if (update)
			msp_wake_thread(client);
		return 0;
	}

	case VIDIOC_ENUMINPUT:
	{
		struct v4l2_input *i = arg;

		if (i->index != 0)
			return -EINVAL;

		i->type = V4L2_INPUT_TYPE_TUNER;
		switch (i->index) {
		case AUDIO_RADIO:
			strcpy(i->name, "Radio");
			break;
		case AUDIO_EXTERN_1:
			strcpy(i->name, "Extern 1");
			break;
		case AUDIO_EXTERN_2:
			strcpy(i->name, "Extern 2");
			break;
		case AUDIO_TUNER:
			strcpy(i->name, "Television");
			break;
		default:
			return -EINVAL;
		}
		return 0;
	}

	case VIDIOC_G_AUDIO:
	{
		struct v4l2_audio *a = arg;

		memset(a, 0, sizeof(*a));

		switch (a->index) {
		case AUDIO_RADIO:
			strcpy(a->name, "Radio");
			break;
		case AUDIO_EXTERN_1:
			strcpy(a->name, "Extern 1");
			break;
		case AUDIO_EXTERN_2:
			strcpy(a->name, "Extern 2");
			break;
		case AUDIO_TUNER:
			strcpy(a->name, "Television");
			break;
		default:
			return -EINVAL;
		}

		msp_any_detect_stereo(client);
		if (state->audmode == V4L2_TUNER_MODE_STEREO) {
			a->capability = V4L2_AUDCAP_STEREO;
		}

		break;
	}

	case VIDIOC_S_AUDIO:
	{
		struct v4l2_audio *sarg = arg;

		switch (sarg->index) {
		case AUDIO_RADIO:
			/* Hauppauge uses IN2 for the radio */
			state->mode = MSP_MODE_FM_RADIO;
			scart       = SCART_IN2;
			break;
		case AUDIO_EXTERN_1:
			/* IN1 is often used for external input ... */
			state->mode = MSP_MODE_EXTERN;
			scart       = SCART_IN1;
			break;
		case AUDIO_EXTERN_2:
			/* ... sometimes it is IN2 through ;) */
			state->mode = MSP_MODE_EXTERN;
			scart       = SCART_IN2;
			break;
		case AUDIO_TUNER:
			state->mode = -1;
			break;
		}
		if (scart) {
			state->rxsubchans = V4L2_TUNER_SUB_STEREO;
			state->audmode = V4L2_TUNER_MODE_STEREO;
			msp_set_scart(client, scart, 0);
			msp_write_dsp(client, 0x000d, 0x1900);
		}
		if (sarg->capability == V4L2_AUDCAP_STEREO) {
			state->audmode = V4L2_TUNER_MODE_STEREO;
		} else {
			state->audmode &= ~V4L2_TUNER_MODE_STEREO;
		}
		msp_any_set_audmode(client, state->audmode);
		msp_wake_thread(client);
		break;
	}

	case VIDIOC_G_TUNER:
	{
		struct v4l2_tuner *vt = arg;

		if (state->radio)
			break;
		if (state->opmode == OPMODE_AUTOSELECT)
			msp_any_detect_stereo(client);
		vt->audmode    = state->audmode;
		vt->rxsubchans = state->rxsubchans;
		vt->capability = V4L2_TUNER_CAP_STEREO |
			V4L2_TUNER_CAP_LANG1 | V4L2_TUNER_CAP_LANG2;
		break;
	}

	case VIDIOC_S_TUNER:
	{
		struct v4l2_tuner *vt = (struct v4l2_tuner *)arg;

		if (state->radio)
			break;
		/* only set audmode */
		if (vt->audmode != -1 && vt->audmode != 0)
			msp_any_set_audmode(client, vt->audmode);
		break;
	}

	case VIDIOC_G_AUDOUT:
	{
		struct v4l2_audioout *a = (struct v4l2_audioout *)arg;
		int idx = a->index;

		memset(a, 0, sizeof(*a));

		switch (idx) {
		case 0:
			strcpy(a->name, "Scart1 Out");
			break;
		case 1:
			strcpy(a->name, "Scart2 Out");
			break;
		case 2:
			strcpy(a->name, "I2S Out");
			break;
		default:
			return -EINVAL;
		}
		break;

	}

	case VIDIOC_S_AUDOUT:
	{
		struct v4l2_audioout *a = (struct v4l2_audioout *)arg;

		if (a->index < 0 || a->index > 2)
			return -EINVAL;

		v4l_dbg(1, msp_debug, client, "Setting audio out on msp34xx to input %i\n", a->index);
		msp_set_scart(client, state->in_scart, a->index + 1);

		break;
	}

	case VIDIOC_INT_I2S_CLOCK_FREQ:
	{
		u32 *a = (u32 *)arg;

		v4l_dbg(1, msp_debug, client, "Setting I2S speed to %d\n", *a);

		switch (*a) {
			case 1024000:
				state->i2s_mode = 0;
				break;
			case 2048000:
				state->i2s_mode = 1;
				break;
			default:
				return -EINVAL;
		}
		break;
	}

	case VIDIOC_QUERYCTRL:
	{
		struct v4l2_queryctrl *qc = arg;
		int i;

		for (i = 0; i < ARRAY_SIZE(msp_qctrl_std); i++)
			if (qc->id && qc->id == msp_qctrl_std[i].id) {
				memcpy(qc, &msp_qctrl_std[i], sizeof(*qc));
				return 0;
			}
		if (!state->has_sound_processing)
			return -EINVAL;
		for (i = 0; i < ARRAY_SIZE(msp_qctrl_sound_processing); i++)
			if (qc->id && qc->id == msp_qctrl_sound_processing[i].id) {
				memcpy(qc, &msp_qctrl_sound_processing[i], sizeof(*qc));
				return 0;
			}
		return -EINVAL;
	}

	case VIDIOC_G_CTRL:
		return msp_get_ctrl(client, arg);

	case VIDIOC_S_CTRL:
		return msp_set_ctrl(client, arg);

	case VIDIOC_LOG_STATUS:
	{
		const char *p;

		if (state->opmode == OPMODE_AUTOSELECT)
			msp_any_detect_stereo(client);
		v4l_info(client, "%s rev1 = 0x%04x rev2 = 0x%04x\n",
				client->name, state->rev1, state->rev2);
		v4l_info(client, "Audio:    volume %d%s\n",
				state->volume, state->muted ? " (muted)" : "");
		if (state->has_sound_processing) {
			v4l_info(client, "Audio:    balance %d bass %d treble %d loudness %s\n",
					state->balance, state->bass, state->treble,
					state->loudness ? "on" : "off");
		}
		switch (state->mode) {
		case MSP_MODE_AM_DETECT: p = "AM (for carrier detect)"; break;
		case MSP_MODE_FM_RADIO: p = "FM Radio"; break;
		case MSP_MODE_FM_TERRA: p = "Terrestial FM-mono + FM-stereo"; break;
		case MSP_MODE_FM_SAT: p = "Satellite FM-mono"; break;
		case MSP_MODE_FM_NICAM1: p = "NICAM/FM (B/G, D/K)"; break;
		case MSP_MODE_FM_NICAM2: p = "NICAM/FM (I)"; break;
		case MSP_MODE_AM_NICAM: p = "NICAM/AM (L)"; break;
		case MSP_MODE_BTSC: p = "BTSC"; break;
		case MSP_MODE_EXTERN: p = "External input"; break;
		default: p = "unknown"; break;
		}
		if (state->opmode == OPMODE_MANUAL) {
			v4l_info(client, "Mode:     %s (%s%s)\n", p,
				(state->rxsubchans & V4L2_TUNER_SUB_STEREO) ? "stereo" : "mono",
				(state->rxsubchans & V4L2_TUNER_SUB_LANG2) ? ", dual" : "");
		} else {
			v4l_info(client, "Mode:     %s\n", p);
			v4l_info(client, "Standard: %s (%s%s)\n",
				msp_standard_std_name(state->std),
				(state->rxsubchans & V4L2_TUNER_SUB_STEREO) ? "stereo" : "mono",
				(state->rxsubchans & V4L2_TUNER_SUB_LANG2) ? ", dual" : "");
		}
		v4l_info(client, "ACB:      0x%04x\n", state->acb);
		break;
	}

	default:
		/* unknown */
		return -EINVAL;
	}
	return 0;
}

static int msp_suspend(struct device * dev, pm_message_t state)
{
	struct i2c_client *client = container_of(dev, struct i2c_client, dev);

	v4l_dbg(1, msp_debug, client, "suspend\n");
	msp_reset(client);
	return 0;
}

static int msp_resume(struct device * dev)
{
	struct i2c_client *client = container_of(dev, struct i2c_client, dev);

	v4l_dbg(1, msp_debug, client, "resume\n");
	msp_wake_thread(client);
	return 0;
}

/* ----------------------------------------------------------------------- */

static struct i2c_driver i2c_driver;

static int msp_attach(struct i2c_adapter *adapter, int address, int kind)
{
	struct i2c_client *client;
	struct msp_state *state;
	int (*thread_func)(void *data) = NULL;
	int msp_hard;
	int msp_family;
	int msp_revision;
	int msp_product, msp_prod_hi, msp_prod_lo;
	int msp_rom;

	client = kmalloc(sizeof(*client), GFP_KERNEL);
	if (client == NULL)
		return -ENOMEM;
	memset(client, 0, sizeof(*client));
	client->addr = address;
	client->adapter = adapter;
	client->driver = &i2c_driver;
	snprintf(client->name, sizeof(client->name) - 1, "msp3400");

	if (msp_reset(client) == -1) {
		v4l_dbg(1, msp_debug, client, "msp3400 not found\n");
		kfree(client);
		return -1;
	}

	state = kmalloc(sizeof(*state), GFP_KERNEL);
	if (state == NULL) {
		kfree(client);
		return -ENOMEM;
	}
	i2c_set_clientdata(client, state);

	memset(state, 0, sizeof(*state));
	state->v4l2_std = V4L2_STD_NTSC;
	state->volume = 58880;	/* 0db gain */
	state->balance = 32768;	/* 0db gain */
	state->bass = 32768;
	state->treble = 32768;
	state->loudness = 0;
	state->input = -1;
	state->muted = 0;
	state->i2s_mode = 0;
	init_waitqueue_head(&state->wq);

	state->rev1 = msp_read_dsp(client, 0x1e);
	if (state->rev1 != -1)
		state->rev2 = msp_read_dsp(client, 0x1f);
	v4l_dbg(1, msp_debug, client, "rev1=0x%04x, rev2=0x%04x\n", state->rev1, state->rev2);
	if (state->rev1 == -1 || (state->rev1 == 0 && state->rev2 == 0)) {
		v4l_dbg(1, msp_debug, client, "not an msp3400 (cannot read chip version)\n");
		kfree(state);
		kfree(client);
		return -1;
	}

	msp_set_audio(client);

	msp_family = ((state->rev1 >> 4) & 0x0f) + 3;
	msp_product = (state->rev2 >> 8) & 0xff;
	msp_prod_hi = msp_product / 10;
	msp_prod_lo = msp_product % 10;
	msp_revision = (state->rev1 & 0x0f) + '@';
	msp_hard = ((state->rev1 >> 8) & 0xff) + '@';
	msp_rom = state->rev2 & 0x1f;
	snprintf(client->name, sizeof(client->name), "MSP%d4%02d%c-%c%d",
			msp_family, msp_product,
			msp_revision, msp_hard, msp_rom);

	/* Has NICAM support: all mspx41x and mspx45x products have NICAM */
	state->has_nicam = msp_prod_hi == 1 || msp_prod_hi == 5;
	/* Has radio support: was added with revision G */
	state->has_radio = msp_revision >= 'G';
	/* Has headphones output: not for stripped down products */
	state->has_headphones = msp_prod_lo < 5;
	/* Has scart4 input: not in pre D revisions, not in stripped D revs */
	state->has_scart4 = msp_family >= 4 || (msp_revision >= 'D' && msp_prod_lo < 5);
	/* Has scart2 and scart3 inputs and scart2 output: not in stripped
	   down products of the '3' family */
	state->has_scart23_in_scart2_out = msp_family >= 4 || msp_prod_lo < 5;
	/* Has scart2 a volume control? Not in pre-D revisions. */
	state->has_scart2_out_volume = msp_revision > 'C' && state->has_scart23_in_scart2_out;
	/* Has a configurable i2s out? */
	state->has_i2s_conf = msp_revision >= 'G' && msp_prod_lo < 7;
	/* Has subwoofer output: not in pre-D revs and not in stripped down products */
	state->has_subwoofer = msp_revision >= 'D' && msp_prod_lo < 5;
	/* Has soundprocessing (bass/treble/balance/loudness/equalizer): not in
	   stripped down products */
	state->has_sound_processing = msp_prod_lo < 7;
	/* Has Virtual Dolby Surround: only in msp34x1 */
	state->has_virtual_dolby_surround = msp_revision == 'G' && msp_prod_lo == 1;
	/* Has Virtual Dolby Surround & Dolby Pro Logic: only in msp34x2 */
	state->has_dolby_pro_logic = msp_revision == 'G' && msp_prod_lo == 2;

	state->opmode = opmode;
	if (state->opmode == OPMODE_AUTO) {
		/* MSP revision G and up have both autodetect and autoselect */
		if (msp_revision >= 'G')
			state->opmode = OPMODE_AUTOSELECT;
		/* MSP revision D and up have autodetect */
		else if (msp_revision >= 'D')
			state->opmode = OPMODE_AUTODETECT;
		else
			state->opmode = OPMODE_MANUAL;
	}

	/* hello world :-) */
	v4l_info(client, "%s found @ 0x%x (%s)\n", client->name, address << 1, adapter->name);
	v4l_info(client, "%s ", client->name);
	if (state->has_nicam && state->has_radio)
		printk("supports nicam and radio, ");
	else if (state->has_nicam)
		printk("supports nicam, ");
	else if (state->has_radio)
		printk("supports radio, ");
	printk("mode is ");

	/* version-specific initialization */
	switch (state->opmode) {
	case OPMODE_MANUAL:
		printk("manual");
		thread_func = msp3400c_thread;
		break;
	case OPMODE_AUTODETECT:
		printk("autodetect");
		thread_func = msp3410d_thread;
		break;
	case OPMODE_AUTOSELECT:
		printk("autodetect and autoselect");
		thread_func = msp34xxg_thread;
		break;
	}
	printk("\n");

	/* startup control thread if needed */
	if (thread_func) {
		state->kthread = kthread_run(thread_func, client, "msp34xx");

		if (state->kthread == NULL)
			v4l_warn(client, "kernel_thread() failed\n");
		msp_wake_thread(client);
	}

	/* done */
	i2c_attach_client(client);

	return 0;
}

static int msp_probe(struct i2c_adapter *adapter)
{
	if (adapter->class & I2C_CLASS_TV_ANALOG)
		return i2c_probe(adapter, &addr_data, msp_attach);
	return 0;
}

static int msp_detach(struct i2c_client *client)
{
	struct msp_state *state = i2c_get_clientdata(client);
	int err;

	/* shutdown control thread */
	if (state->kthread) {
		state->restart = 1;
		kthread_stop(state->kthread);
	}
	msp_reset(client);

	err = i2c_detach_client(client);
	if (err) {
		return err;
	}

	kfree(state);
	kfree(client);
	return 0;
}

/* ----------------------------------------------------------------------- */

/* i2c implementation */
static struct i2c_driver i2c_driver = {
	.id             = I2C_DRIVERID_MSP3400,
	.attach_adapter = msp_probe,
	.detach_client  = msp_detach,
	.command        = msp_command,
	.driver = {
		.name    = "msp3400",
		.suspend = msp_suspend,
		.resume  = msp_resume,
	},
};

static int __init msp3400_init_module(void)
{
	return i2c_add_driver(&i2c_driver);
}

static void __exit msp3400_cleanup_module(void)
{
	i2c_del_driver(&i2c_driver);
}

module_init(msp3400_init_module);
module_exit(msp3400_cleanup_module);

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-basic-offset: 8
 * End:
 */

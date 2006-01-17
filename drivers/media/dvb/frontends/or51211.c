/*
 *    Support for OR51211 (pcHDTV HD-2000) - VSB
 *
 *    Copyright (C) 2005 Kirk Lapray <kirk_lapray@bigfoot.com>
 *
 *    Based on code from Jack Kelliher (kelliher@xmission.com)
 *                           Copyright (C) 2002 & pcHDTV, inc.
 *
 *    This program is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2 of the License, or
 *    (at your option) any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with this program; if not, write to the Free Software
 *    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
*/

/*
 * This driver needs external firmware. Please use the command
 * "<kerneldir>/Documentation/dvb/get_dvb_firmware or51211" to
 * download/extract it, and then copy it to /usr/lib/hotplug/firmware
 * or /lib/firmware (depending on configuration of firmware hotplug).
 */
#define OR51211_DEFAULT_FIRMWARE "dvb-fe-or51211.fw"

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/device.h>
#include <linux/firmware.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <asm/byteorder.h>

#include "dvb_frontend.h"
#include "or51211.h"

static int debug;
#define dprintk(args...) \
	do { \
		if (debug) printk(KERN_DEBUG "or51211: " args); \
	} while (0)

static u8 run_buf[] = {0x7f,0x01};
static u8 cmd_buf[] = {0x04,0x01,0x50,0x80,0x06}; // ATSC

struct or51211_state {

	struct i2c_adapter* i2c;
	struct dvb_frontend_ops ops;

	/* Configuration settings */
	const struct or51211_config* config;

	struct dvb_frontend frontend;
	struct bt878* bt;

	/* Demodulator private data */
	u8 initialized:1;

	/* Tuner private data */
	u32 current_frequency;
};

static int i2c_writebytes (struct or51211_state* state, u8 reg, u8 *buf,
			   int len)
{
	int err;
	struct i2c_msg msg;
	msg.addr	= reg;
	msg.flags	= 0;
	msg.len		= len;
	msg.buf		= buf;

	if ((err = i2c_transfer (state->i2c, &msg, 1)) != 1) {
		printk(KERN_WARNING "or51211: i2c_writebytes error "
		       "(addr %02x, err == %i)\n", reg, err);
		return -EREMOTEIO;
	}

	return 0;
}

static u8 i2c_readbytes (struct or51211_state* state, u8 reg, u8* buf, int len)
{
	int err;
	struct i2c_msg msg;
	msg.addr	= reg;
	msg.flags	= I2C_M_RD;
	msg.len		= len;
	msg.buf		= buf;

	if ((err = i2c_transfer (state->i2c, &msg, 1)) != 1) {
		printk(KERN_WARNING "or51211: i2c_readbytes error "
		       "(addr %02x, err == %i)\n", reg, err);
		return -EREMOTEIO;
	}

	return 0;
}

static int or51211_load_firmware (struct dvb_frontend* fe,
				  const struct firmware *fw)
{
	struct or51211_state* state = fe->demodulator_priv;
	u8 tudata[585];
	int i;

	dprintk("Firmware is %zd bytes\n",fw->size);

	/* Get eprom data */
	tudata[0] = 17;
	if (i2c_writebytes(state,0x50,tudata,1)) {
		printk(KERN_WARNING "or51211:load_firmware error eprom addr\n");
		return -1;
	}
	if (i2c_readbytes(state,0x50,&tudata[145],192)) {
		printk(KERN_WARNING "or51211: load_firmware error eprom\n");
		return -1;
	}

	/* Create firmware buffer */
	for (i = 0; i < 145; i++)
		tudata[i] = fw->data[i];

	for (i = 0; i < 248; i++)
		tudata[i+337] = fw->data[145+i];

	state->config->reset(fe);

	if (i2c_writebytes(state,state->config->demod_address,tudata,585)) {
		printk(KERN_WARNING "or51211: load_firmware error 1\n");
		return -1;
	}
	msleep(1);

	if (i2c_writebytes(state,state->config->demod_address,
			   &fw->data[393],8125)) {
		printk(KERN_WARNING "or51211: load_firmware error 2\n");
		return -1;
	}
	msleep(1);

	if (i2c_writebytes(state,state->config->demod_address,run_buf,2)) {
		printk(KERN_WARNING "or51211: load_firmware error 3\n");
		return -1;
	}

	/* Wait at least 5 msec */
	msleep(10);
	if (i2c_writebytes(state,state->config->demod_address,run_buf,2)) {
		printk(KERN_WARNING "or51211: load_firmware error 4\n");
		return -1;
	}
	msleep(10);

	printk("or51211: Done.\n");
	return 0;
};

static int or51211_setmode(struct dvb_frontend* fe, int mode)
{
	struct or51211_state* state = fe->demodulator_priv;
	u8 rec_buf[14];

	state->config->setmode(fe, mode);

	if (i2c_writebytes(state,state->config->demod_address,run_buf,2)) {
		printk(KERN_WARNING "or51211: setmode error 1\n");
		return -1;
	}

	/* Wait at least 5 msec */
	msleep(10);
	if (i2c_writebytes(state,state->config->demod_address,run_buf,2)) {
		printk(KERN_WARNING "or51211: setmode error 2\n");
		return -1;
	}

	msleep(10);

	/* Set operation mode in Receiver 1 register;
	 * type 1:
	 * data 0x50h  Automatic sets receiver channel conditions
	 *             Automatic NTSC rejection filter
	 *             Enable  MPEG serial data output
	 *             MPEG2tr
	 *             High tuner phase noise
	 *             normal +/-150kHz Carrier acquisition range
	 */
	if (i2c_writebytes(state,state->config->demod_address,cmd_buf,3)) {
		printk(KERN_WARNING "or51211: setmode error 3\n");
		return -1;
	}

	rec_buf[0] = 0x04;
	rec_buf[1] = 0x00;
	rec_buf[2] = 0x03;
	rec_buf[3] = 0x00;
	msleep(20);
	if (i2c_writebytes(state,state->config->demod_address,rec_buf,3)) {
		printk(KERN_WARNING "or51211: setmode error 5\n");
	}
	msleep(3);
	if (i2c_readbytes(state,state->config->demod_address,&rec_buf[10],2)) {
		printk(KERN_WARNING "or51211: setmode error 6");
		return -1;
	}
	dprintk("setmode rec status %02x %02x\n",rec_buf[10],rec_buf[11]);

	return 0;
}

static int or51211_set_parameters(struct dvb_frontend* fe,
				  struct dvb_frontend_parameters *param)
{
	struct or51211_state* state = fe->demodulator_priv;
	u32 freq = 0;
	u16 tunerfreq = 0;
	u8 buf[4];

	/* Change only if we are actually changing the channel */
	if (state->current_frequency != param->frequency) {
		freq = 44000 + (param->frequency/1000);
		tunerfreq = freq * 16/1000;

		dprintk("set_parameters frequency = %d (tunerfreq = %d)\n",
			param->frequency,tunerfreq);

		buf[0] = (tunerfreq >> 8) & 0x7F;
		buf[1] = (tunerfreq & 0xFF);
		buf[2] = 0x8E;

		if (param->frequency < 157250000) {
			buf[3] = 0xA0;
			dprintk("set_parameters VHF low range\n");
		} else if (param->frequency < 454000000) {
			buf[3] = 0x90;
			dprintk("set_parameters VHF high range\n");
		} else {
			buf[3] = 0x30;
			dprintk("set_parameters UHF range\n");
		}
		dprintk("set_parameters tuner bytes: 0x%02x 0x%02x "
			"0x%02x 0x%02x\n",buf[0],buf[1],buf[2],buf[3]);

		if (i2c_writebytes(state,0xC2>>1,buf,4))
			printk(KERN_WARNING "or51211:set_parameters error "
			       "writing to tuner\n");

		/* Set to ATSC mode */
		or51211_setmode(fe,0);

		/* Update current frequency */
		state->current_frequency = param->frequency;
	}
	return 0;
}

static int or51211_read_status(struct dvb_frontend* fe, fe_status_t* status)
{
	struct or51211_state* state = fe->demodulator_priv;
	unsigned char rec_buf[2];
	unsigned char snd_buf[] = {0x04,0x00,0x03,0x00};
	*status = 0;

	/* Receiver Status */
	if (i2c_writebytes(state,state->config->demod_address,snd_buf,3)) {
		printk(KERN_WARNING "or51132: read_status write error\n");
		return -1;
	}
	msleep(3);
	if (i2c_readbytes(state,state->config->demod_address,rec_buf,2)) {
		printk(KERN_WARNING "or51132: read_status read error\n");
		return -1;
	}
	dprintk("read_status %x %x\n",rec_buf[0],rec_buf[1]);

	if (rec_buf[0] &  0x01) { /* Receiver Lock */
		*status |= FE_HAS_SIGNAL;
		*status |= FE_HAS_CARRIER;
		*status |= FE_HAS_VITERBI;
		*status |= FE_HAS_SYNC;
		*status |= FE_HAS_LOCK;
	}
	return 0;
}

/* log10-1 table at .5 increments from 1 to 100.5 */
static unsigned int i100x20log10[] = {
		0,  352,  602,  795,  954, 1088, 1204, 1306, 1397, 1480,
	 1556, 1625, 1690, 1750, 1806, 1858, 1908, 1955, 2000, 2042,
	 2082, 2121, 2158, 2193, 2227, 2260, 2292, 2322, 2352, 2380,
	 2408, 2434, 2460, 2486, 2510, 2534, 2557, 2580, 2602, 2623,
	 2644, 2664, 2684, 2704, 2723, 2742, 2760, 2778, 2795, 2813,
	 2829, 2846, 2862, 2878, 2894, 2909, 2924, 2939, 2954, 2968,
	 2982, 2996, 3010, 3023, 3037, 3050, 3062, 3075, 3088, 3100,
	 3112, 3124, 3136, 3148, 3159, 3170, 3182, 3193, 3204, 3214,
	 3225, 3236, 3246, 3256, 3266, 3276, 3286, 3296, 3306, 3316,
	 3325, 3334, 3344, 3353, 3362, 3371, 3380, 3389, 3397, 3406,
	 3415, 3423, 3432, 3440, 3448, 3456, 3464, 3472, 3480, 3488,
	 3496, 3504, 3511, 3519, 3526, 3534, 3541, 3549, 3556, 3563,
	 3570, 3577, 3584, 3591, 3598, 3605, 3612, 3619, 3625, 3632,
	 3639, 3645, 3652, 3658, 3665, 3671, 3677, 3683, 3690, 3696,
	 3702, 3708, 3714, 3720, 3726, 3732, 3738, 3744, 3750, 3755,
	 3761, 3767, 3772, 3778, 3784, 3789, 3795, 3800, 3806, 3811,
	 3816, 3822, 3827, 3832, 3838, 3843, 3848, 3853, 3858, 3863,
	 3868, 3874, 3879, 3884, 3888, 3893, 3898, 3903, 3908, 3913,
	 3918, 3922, 3927, 3932, 3936, 3941, 3946, 3950, 3955, 3960,
	 3964, 3969, 3973, 3978, 3982, 3986, 3991, 3995, 4000, 4004,
};

static unsigned int denom[] = {1,1,100,1000,10000,100000,1000000,10000000,100000000};

static unsigned int i20Log10(unsigned short val)
{
	unsigned int rntval = 100;
	unsigned int tmp = val;
	unsigned int exp = 1;

	while(tmp > 100) {tmp /= 100; exp++;}

	val = (2 * val)/denom[exp];
	if (exp > 1) rntval = 2000*exp;

	rntval += i100x20log10[val];
	return rntval;
}

static int or51211_read_signal_strength(struct dvb_frontend* fe, u16* strength)
{
	struct or51211_state* state = fe->demodulator_priv;
	u8 rec_buf[2];
	u8 snd_buf[4];
	u8 snr_equ;
	u32 signal_strength;

	/* SNR after Equalizer */
	snd_buf[0] = 0x04;
	snd_buf[1] = 0x00;
	snd_buf[2] = 0x04;
	snd_buf[3] = 0x00;

	if (i2c_writebytes(state,state->config->demod_address,snd_buf,3)) {
		printk(KERN_WARNING "or51211: read_status write error\n");
		return -1;
	}
	msleep(3);
	if (i2c_readbytes(state,state->config->demod_address,rec_buf,2)) {
		printk(KERN_WARNING "or51211: read_status read error\n");
		return -1;
	}
	snr_equ = rec_buf[0] & 0xff;

	/* The value reported back from the frontend will be FFFF=100% 0000=0% */
	signal_strength = (((5334 - i20Log10(snr_equ))/3+5)*65535)/1000;
	if (signal_strength > 0xffff)
		*strength = 0xffff;
	else
		*strength = signal_strength;
	dprintk("read_signal_strength %i\n",*strength);

	return 0;
}

static int or51211_read_snr(struct dvb_frontend* fe, u16* snr)
{
	struct or51211_state* state = fe->demodulator_priv;
	u8 rec_buf[2];
	u8 snd_buf[4];

	/* SNR after Equalizer */
	snd_buf[0] = 0x04;
	snd_buf[1] = 0x00;
	snd_buf[2] = 0x04;
	snd_buf[3] = 0x00;

	if (i2c_writebytes(state,state->config->demod_address,snd_buf,3)) {
		printk(KERN_WARNING "or51211: read_status write error\n");
		return -1;
	}
	msleep(3);
	if (i2c_readbytes(state,state->config->demod_address,rec_buf,2)) {
		printk(KERN_WARNING "or51211: read_status read error\n");
		return -1;
	}
	*snr = rec_buf[0] & 0xff;

	dprintk("read_snr %i\n",*snr);

	return 0;
}

static int or51211_read_ber(struct dvb_frontend* fe, u32* ber)
{
	*ber = -ENOSYS;
	return 0;
}

static int or51211_read_ucblocks(struct dvb_frontend* fe, u32* ucblocks)
{
	*ucblocks = -ENOSYS;
	return 0;
}

static int or51211_sleep(struct dvb_frontend* fe)
{
	return 0;
}

static int or51211_init(struct dvb_frontend* fe)
{
	struct or51211_state* state = fe->demodulator_priv;
	const struct or51211_config* config = state->config;
	const struct firmware* fw;
	unsigned char get_ver_buf[] = {0x04,0x00,0x30,0x00,0x00};
	unsigned char rec_buf[14];
	int ret,i;

	if (!state->initialized) {
		/* Request the firmware, this will block until it uploads */
		printk(KERN_INFO "or51211: Waiting for firmware upload "
		       "(%s)...\n", OR51211_DEFAULT_FIRMWARE);
		ret = config->request_firmware(fe, &fw,
					       OR51211_DEFAULT_FIRMWARE);
		printk(KERN_INFO "or51211:Got Hotplug firmware\n");
		if (ret) {
			printk(KERN_WARNING "or51211: No firmware uploaded "
			       "(timeout or file not found?)\n");
			return ret;
		}

		ret = or51211_load_firmware(fe, fw);
		if (ret) {
			printk(KERN_WARNING "or51211: Writing firmware to "
			       "device failed!\n");
			release_firmware(fw);
			return ret;
		}
		printk(KERN_INFO "or51211: Firmware upload complete.\n");

		/* Set operation mode in Receiver 1 register;
		 * type 1:
		 * data 0x50h  Automatic sets receiver channel conditions
		 *             Automatic NTSC rejection filter
		 *             Enable  MPEG serial data output
		 *             MPEG2tr
		 *             High tuner phase noise
		 *             normal +/-150kHz Carrier acquisition range
		 */
		if (i2c_writebytes(state,state->config->demod_address,
				   cmd_buf,3)) {
			printk(KERN_WARNING "or51211: Load DVR Error 5\n");
			return -1;
		}

		/* Read back ucode version to besure we loaded correctly */
		/* and are really up and running */
		rec_buf[0] = 0x04;
		rec_buf[1] = 0x00;
		rec_buf[2] = 0x03;
		rec_buf[3] = 0x00;
		msleep(30);
		if (i2c_writebytes(state,state->config->demod_address,
				   rec_buf,3)) {
			printk(KERN_WARNING "or51211: Load DVR Error A\n");
			return -1;
		}
		msleep(3);
		if (i2c_readbytes(state,state->config->demod_address,
				  &rec_buf[10],2)) {
			printk(KERN_WARNING "or51211: Load DVR Error B\n");
			return -1;
		}

		rec_buf[0] = 0x04;
		rec_buf[1] = 0x00;
		rec_buf[2] = 0x01;
		rec_buf[3] = 0x00;
		msleep(20);
		if (i2c_writebytes(state,state->config->demod_address,
				   rec_buf,3)) {
			printk(KERN_WARNING "or51211: Load DVR Error C\n");
			return -1;
		}
		msleep(3);
		if (i2c_readbytes(state,state->config->demod_address,
				  &rec_buf[12],2)) {
			printk(KERN_WARNING "or51211: Load DVR Error D\n");
			return -1;
		}

		for (i = 0; i < 8; i++)
			rec_buf[i]=0xed;

		for (i = 0; i < 5; i++) {
			msleep(30);
			get_ver_buf[4] = i+1;
			if (i2c_writebytes(state,state->config->demod_address,
					   get_ver_buf,5)) {
				printk(KERN_WARNING "or51211:Load DVR Error 6"
				       " - %d\n",i);
				return -1;
			}
			msleep(3);

			if (i2c_readbytes(state,state->config->demod_address,
					  &rec_buf[i*2],2)) {
				printk(KERN_WARNING "or51211:Load DVR Error 7"
				       " - %d\n",i);
				return -1;
			}
			/* If we didn't receive the right index, try again */
			if ((int)rec_buf[i*2+1]!=i+1){
			  i--;
			}
		}
		dprintk("read_fwbits %x %x %x %x %x %x %x %x %x %x\n",
			rec_buf[0], rec_buf[1], rec_buf[2], rec_buf[3],
			rec_buf[4], rec_buf[5], rec_buf[6], rec_buf[7],
			rec_buf[8], rec_buf[9]);

		printk(KERN_INFO "or51211: ver TU%02x%02x%02x VSB mode %02x"
		       " Status %02x\n",
		       rec_buf[2], rec_buf[4],rec_buf[6],
		       rec_buf[12],rec_buf[10]);

		rec_buf[0] = 0x04;
		rec_buf[1] = 0x00;
		rec_buf[2] = 0x03;
		rec_buf[3] = 0x00;
		msleep(20);
		if (i2c_writebytes(state,state->config->demod_address,
				   rec_buf,3)) {
			printk(KERN_WARNING "or51211: Load DVR Error 8\n");
			return -1;
		}
		msleep(20);
		if (i2c_readbytes(state,state->config->demod_address,
				  &rec_buf[8],2)) {
			printk(KERN_WARNING "or51211: Load DVR Error 9\n");
			return -1;
		}
		state->initialized = 1;
	}

	return 0;
}

static int or51211_get_tune_settings(struct dvb_frontend* fe,
				     struct dvb_frontend_tune_settings* fesettings)
{
	fesettings->min_delay_ms = 500;
	fesettings->step_size = 0;
	fesettings->max_drift = 0;
	return 0;
}

static void or51211_release(struct dvb_frontend* fe)
{
	struct or51211_state* state = fe->demodulator_priv;
	state->config->sleep(fe);
	kfree(state);
}

static struct dvb_frontend_ops or51211_ops;

struct dvb_frontend* or51211_attach(const struct or51211_config* config,
				    struct i2c_adapter* i2c)
{
	struct or51211_state* state = NULL;

	/* Allocate memory for the internal state */
	state = kmalloc(sizeof(struct or51211_state), GFP_KERNEL);
	if (state == NULL)
		goto error;

	/* Setup the state */
	state->config = config;
	state->i2c = i2c;
	memcpy(&state->ops, &or51211_ops, sizeof(struct dvb_frontend_ops));
	state->initialized = 0;
	state->current_frequency = 0;

	/* Create dvb_frontend */
	state->frontend.ops = &state->ops;
	state->frontend.demodulator_priv = state;
	return &state->frontend;

error:
	kfree(state);
	return NULL;
}

static struct dvb_frontend_ops or51211_ops = {

	.info = {
		.name               = "Oren OR51211 VSB Frontend",
		.type               = FE_ATSC,
		.frequency_min      = 44000000,
		.frequency_max      = 958000000,
		.frequency_stepsize = 166666,
		.caps = FE_CAN_FEC_1_2 | FE_CAN_FEC_2_3 | FE_CAN_FEC_3_4 |
			FE_CAN_FEC_5_6 | FE_CAN_FEC_7_8 | FE_CAN_FEC_AUTO |
			FE_CAN_8VSB
	},

	.release = or51211_release,

	.init = or51211_init,
	.sleep = or51211_sleep,

	.set_frontend = or51211_set_parameters,
	.get_tune_settings = or51211_get_tune_settings,

	.read_status = or51211_read_status,
	.read_ber = or51211_read_ber,
	.read_signal_strength = or51211_read_signal_strength,
	.read_snr = or51211_read_snr,
	.read_ucblocks = or51211_read_ucblocks,
};

module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "Turn on/off frontend debugging (default:off).");

MODULE_DESCRIPTION("Oren OR51211 VSB [pcHDTV HD-2000] Demodulator Driver");
MODULE_AUTHOR("Kirk Lapray");
MODULE_LICENSE("GPL");

EXPORT_SYMBOL(or51211_attach);


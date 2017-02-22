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

#define pr_fmt(fmt)	KBUILD_MODNAME ": %s: " fmt, __func__

/*
 * This driver needs external firmware. Please use the command
 * "<kerneldir>/Documentation/dvb/get_dvb_firmware or51211" to
 * download/extract it, and then copy it to /usr/lib/hotplug/firmware
 * or /lib/firmware (depending on configuration of firmware hotplug).
 */
#define OR51211_DEFAULT_FIRMWARE "dvb-fe-or51211.fw"

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/firmware.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <asm/byteorder.h>

#include "dvb_math.h"
#include "dvb_frontend.h"
#include "or51211.h"

static int debug;
#define dprintk(args...) \
	do { if (debug) pr_debug(args); } while (0)

static u8 run_buf[] = {0x7f,0x01};
static u8 cmd_buf[] = {0x04,0x01,0x50,0x80,0x06}; // ATSC

struct or51211_state {

	struct i2c_adapter* i2c;

	/* Configuration settings */
	const struct or51211_config* config;

	struct dvb_frontend frontend;
	struct bt878* bt;

	/* Demodulator private data */
	u8 initialized:1;
	u32 snr; /* Result of last SNR claculation */

	/* Tuner private data */
	u32 current_frequency;
};

static int i2c_writebytes (struct or51211_state* state, u8 reg, const u8 *buf,
			   int len)
{
	int err;
	struct i2c_msg msg;
	msg.addr	= reg;
	msg.flags	= 0;
	msg.len		= len;
	msg.buf		= (u8 *)buf;

	if ((err = i2c_transfer (state->i2c, &msg, 1)) != 1) {
		pr_warn("error (addr %02x, err == %i)\n", reg, err);
		return -EREMOTEIO;
	}

	return 0;
}

static int i2c_readbytes(struct or51211_state *state, u8 reg, u8 *buf, int len)
{
	int err;
	struct i2c_msg msg;
	msg.addr	= reg;
	msg.flags	= I2C_M_RD;
	msg.len		= len;
	msg.buf		= buf;

	if ((err = i2c_transfer (state->i2c, &msg, 1)) != 1) {
		pr_warn("error (addr %02x, err == %i)\n", reg, err);
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

	dprintk("Firmware is %zu bytes\n", fw->size);

	/* Get eprom data */
	tudata[0] = 17;
	if (i2c_writebytes(state,0x50,tudata,1)) {
		pr_warn("error eprom addr\n");
		return -1;
	}
	if (i2c_readbytes(state,0x50,&tudata[145],192)) {
		pr_warn("error eprom\n");
		return -1;
	}

	/* Create firmware buffer */
	for (i = 0; i < 145; i++)
		tudata[i] = fw->data[i];

	for (i = 0; i < 248; i++)
		tudata[i+337] = fw->data[145+i];

	state->config->reset(fe);

	if (i2c_writebytes(state,state->config->demod_address,tudata,585)) {
		pr_warn("error 1\n");
		return -1;
	}
	msleep(1);

	if (i2c_writebytes(state,state->config->demod_address,
			   &fw->data[393],8125)) {
		pr_warn("error 2\n");
		return -1;
	}
	msleep(1);

	if (i2c_writebytes(state,state->config->demod_address,run_buf,2)) {
		pr_warn("error 3\n");
		return -1;
	}

	/* Wait at least 5 msec */
	msleep(10);
	if (i2c_writebytes(state,state->config->demod_address,run_buf,2)) {
		pr_warn("error 4\n");
		return -1;
	}
	msleep(10);

	pr_info("Done.\n");
	return 0;
};

static int or51211_setmode(struct dvb_frontend* fe, int mode)
{
	struct or51211_state* state = fe->demodulator_priv;
	u8 rec_buf[14];

	state->config->setmode(fe, mode);

	if (i2c_writebytes(state,state->config->demod_address,run_buf,2)) {
		pr_warn("error 1\n");
		return -1;
	}

	/* Wait at least 5 msec */
	msleep(10);
	if (i2c_writebytes(state,state->config->demod_address,run_buf,2)) {
		pr_warn("error 2\n");
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
		pr_warn("error 3\n");
		return -1;
	}

	rec_buf[0] = 0x04;
	rec_buf[1] = 0x00;
	rec_buf[2] = 0x03;
	rec_buf[3] = 0x00;
	msleep(20);
	if (i2c_writebytes(state,state->config->demod_address,rec_buf,3)) {
		pr_warn("error 5\n");
	}
	msleep(3);
	if (i2c_readbytes(state,state->config->demod_address,&rec_buf[10],2)) {
		pr_warn("error 6\n");
		return -1;
	}
	dprintk("rec status %02x %02x\n", rec_buf[10], rec_buf[11]);

	return 0;
}

static int or51211_set_parameters(struct dvb_frontend *fe)
{
	struct dtv_frontend_properties *p = &fe->dtv_property_cache;
	struct or51211_state* state = fe->demodulator_priv;

	/* Change only if we are actually changing the channel */
	if (state->current_frequency != p->frequency) {
		if (fe->ops.tuner_ops.set_params) {
			fe->ops.tuner_ops.set_params(fe);
			if (fe->ops.i2c_gate_ctrl) fe->ops.i2c_gate_ctrl(fe, 0);
		}

		/* Set to ATSC mode */
		or51211_setmode(fe,0);

		/* Update current frequency */
		state->current_frequency = p->frequency;
	}
	return 0;
}

static int or51211_read_status(struct dvb_frontend *fe, enum fe_status *status)
{
	struct or51211_state* state = fe->demodulator_priv;
	unsigned char rec_buf[2];
	unsigned char snd_buf[] = {0x04,0x00,0x03,0x00};
	*status = 0;

	/* Receiver Status */
	if (i2c_writebytes(state,state->config->demod_address,snd_buf,3)) {
		pr_warn("write error\n");
		return -1;
	}
	msleep(3);
	if (i2c_readbytes(state,state->config->demod_address,rec_buf,2)) {
		pr_warn("read error\n");
		return -1;
	}
	dprintk("%x %x\n", rec_buf[0], rec_buf[1]);

	if (rec_buf[0] &  0x01) { /* Receiver Lock */
		*status |= FE_HAS_SIGNAL;
		*status |= FE_HAS_CARRIER;
		*status |= FE_HAS_VITERBI;
		*status |= FE_HAS_SYNC;
		*status |= FE_HAS_LOCK;
	}
	return 0;
}

/* Calculate SNR estimation (scaled by 2^24)

   8-VSB SNR equation from Oren datasheets

   For 8-VSB:
     SNR[dB] = 10 * log10(219037.9454 / MSE^2 )

   We re-write the snr equation as:
     SNR * 2^24 = 10*(c - 2*intlog10(MSE))
   Where for 8-VSB, c = log10(219037.9454) * 2^24 */

static u32 calculate_snr(u32 mse, u32 c)
{
	if (mse == 0) /* No signal */
		return 0;

	mse = 2*intlog10(mse);
	if (mse > c) {
		/* Negative SNR, which is possible, but realisticly the
		demod will lose lock before the signal gets this bad.  The
		API only allows for unsigned values, so just return 0 */
		return 0;
	}
	return 10*(c - mse);
}

static int or51211_read_snr(struct dvb_frontend* fe, u16* snr)
{
	struct or51211_state* state = fe->demodulator_priv;
	u8 rec_buf[2];
	u8 snd_buf[3];

	/* SNR after Equalizer */
	snd_buf[0] = 0x04;
	snd_buf[1] = 0x00;
	snd_buf[2] = 0x04;

	if (i2c_writebytes(state,state->config->demod_address,snd_buf,3)) {
		pr_warn("error writing snr reg\n");
		return -1;
	}
	if (i2c_readbytes(state,state->config->demod_address,rec_buf,2)) {
		pr_warn("read_status read error\n");
		return -1;
	}

	state->snr = calculate_snr(rec_buf[0], 89599047);
	*snr = (state->snr) >> 16;

	dprintk("noise = 0x%02x, snr = %d.%02d dB\n", rec_buf[0],
		state->snr >> 24, (((state->snr>>8) & 0xffff) * 100) >> 16);

	return 0;
}

static int or51211_read_signal_strength(struct dvb_frontend* fe, u16* strength)
{
	/* Calculate Strength from SNR up to 35dB */
	/* Even though the SNR can go higher than 35dB, there is some comfort */
	/* factor in having a range of strong signals that can show at 100%   */
	struct or51211_state* state = (struct or51211_state*)fe->demodulator_priv;
	u16 snr;
	int ret;

	ret = fe->ops.read_snr(fe, &snr);
	if (ret != 0)
		return ret;
	/* Rather than use the 8.8 value snr, use state->snr which is 8.24 */
	/* scale the range 0 - 35*2^24 into 0 - 65535 */
	if (state->snr >= 8960 * 0x10000)
		*strength = 0xffff;
	else
		*strength = state->snr / 8960;

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
		pr_info("Waiting for firmware upload (%s)...\n",
			OR51211_DEFAULT_FIRMWARE);
		ret = config->request_firmware(fe, &fw,
					       OR51211_DEFAULT_FIRMWARE);
		pr_info("Got Hotplug firmware\n");
		if (ret) {
			pr_warn("No firmware uploaded (timeout or file not found?)\n");
			return ret;
		}

		ret = or51211_load_firmware(fe, fw);
		release_firmware(fw);
		if (ret) {
			pr_warn("Writing firmware to device failed!\n");
			return ret;
		}
		pr_info("Firmware upload complete.\n");

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
			pr_warn("Load DVR Error 5\n");
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
			pr_warn("Load DVR Error A\n");
			return -1;
		}
		msleep(3);
		if (i2c_readbytes(state,state->config->demod_address,
				  &rec_buf[10],2)) {
			pr_warn("Load DVR Error B\n");
			return -1;
		}

		rec_buf[0] = 0x04;
		rec_buf[1] = 0x00;
		rec_buf[2] = 0x01;
		rec_buf[3] = 0x00;
		msleep(20);
		if (i2c_writebytes(state,state->config->demod_address,
				   rec_buf,3)) {
			pr_warn("Load DVR Error C\n");
			return -1;
		}
		msleep(3);
		if (i2c_readbytes(state,state->config->demod_address,
				  &rec_buf[12],2)) {
			pr_warn("Load DVR Error D\n");
			return -1;
		}

		for (i = 0; i < 8; i++)
			rec_buf[i]=0xed;

		for (i = 0; i < 5; i++) {
			msleep(30);
			get_ver_buf[4] = i+1;
			if (i2c_writebytes(state,state->config->demod_address,
					   get_ver_buf,5)) {
				pr_warn("Load DVR Error 6 - %d\n", i);
				return -1;
			}
			msleep(3);

			if (i2c_readbytes(state,state->config->demod_address,
					  &rec_buf[i*2],2)) {
				pr_warn("Load DVR Error 7 - %d\n", i);
				return -1;
			}
			/* If we didn't receive the right index, try again */
			if ((int)rec_buf[i*2+1]!=i+1){
			  i--;
			}
		}
		dprintk("read_fwbits %10ph\n", rec_buf);

		pr_info("ver TU%02x%02x%02x VSB mode %02x Status %02x\n",
			rec_buf[2], rec_buf[4], rec_buf[6], rec_buf[12],
			rec_buf[10]);

		rec_buf[0] = 0x04;
		rec_buf[1] = 0x00;
		rec_buf[2] = 0x03;
		rec_buf[3] = 0x00;
		msleep(20);
		if (i2c_writebytes(state,state->config->demod_address,
				   rec_buf,3)) {
			pr_warn("Load DVR Error 8\n");
			return -1;
		}
		msleep(20);
		if (i2c_readbytes(state,state->config->demod_address,
				  &rec_buf[8],2)) {
			pr_warn("Load DVR Error 9\n");
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

static const struct dvb_frontend_ops or51211_ops;

struct dvb_frontend* or51211_attach(const struct or51211_config* config,
				    struct i2c_adapter* i2c)
{
	struct or51211_state* state = NULL;

	/* Allocate memory for the internal state */
	state = kzalloc(sizeof(struct or51211_state), GFP_KERNEL);
	if (state == NULL)
		return NULL;

	/* Setup the state */
	state->config = config;
	state->i2c = i2c;
	state->initialized = 0;
	state->current_frequency = 0;

	/* Create dvb_frontend */
	memcpy(&state->frontend.ops, &or51211_ops, sizeof(struct dvb_frontend_ops));
	state->frontend.demodulator_priv = state;
	return &state->frontend;
}

static const struct dvb_frontend_ops or51211_ops = {
	.delsys = { SYS_ATSC, SYS_DVBC_ANNEX_B },
	.info = {
		.name               = "Oren OR51211 VSB Frontend",
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


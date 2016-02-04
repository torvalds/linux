/*
 *    Support for OR51132 (pcHDTV HD-3000) - VSB/QAM
 *
 *
 *    Copyright (C) 2007 Trent Piepho <xyzzy@speakeasy.org>
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
 * This driver needs two external firmware files. Please copy
 * "dvb-fe-or51132-vsb.fw" and "dvb-fe-or51132-qam.fw" to
 * /usr/lib/hotplug/firmware/ or /lib/firmware/
 * (depending on configuration of firmware hotplug).
 */
#define OR51132_VSB_FIRMWARE "dvb-fe-or51132-vsb.fw"
#define OR51132_QAM_FIRMWARE "dvb-fe-or51132-qam.fw"

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <asm/byteorder.h>

#include "dvb_math.h"
#include "dvb_frontend.h"
#include "or51132.h"

static int debug;
#define dprintk(args...) \
	do { \
		if (debug) printk(KERN_DEBUG "or51132: " args); \
	} while (0)


struct or51132_state
{
	struct i2c_adapter* i2c;

	/* Configuration settings */
	const struct or51132_config* config;

	struct dvb_frontend frontend;

	/* Demodulator private data */
	enum fe_modulation current_modulation;
	u32 snr; /* Result of last SNR calculation */

	/* Tuner private data */
	u32 current_frequency;
};


/* Write buffer to demod */
static int or51132_writebuf(struct or51132_state *state, const u8 *buf, int len)
{
	int err;
	struct i2c_msg msg = { .addr = state->config->demod_address,
			       .flags = 0, .buf = (u8*)buf, .len = len };

	/* msleep(20); */ /* doesn't appear to be necessary */
	if ((err = i2c_transfer(state->i2c, &msg, 1)) != 1) {
		printk(KERN_WARNING "or51132: I2C write (addr 0x%02x len %d) error: %d\n",
		       msg.addr, msg.len, err);
		return -EREMOTEIO;
	}
	return 0;
}

/* Write constant bytes, e.g. or51132_writebytes(state, 0x04, 0x42, 0x00);
   Less code and more efficient that loading a buffer on the stack with
   the bytes to send and then calling or51132_writebuf() on that. */
#define or51132_writebytes(state, data...)  \
	({ static const u8 _data[] = {data}; \
	or51132_writebuf(state, _data, sizeof(_data)); })

/* Read data from demod into buffer.  Returns 0 on success. */
static int or51132_readbuf(struct or51132_state *state, u8 *buf, int len)
{
	int err;
	struct i2c_msg msg = { .addr = state->config->demod_address,
			       .flags = I2C_M_RD, .buf = buf, .len = len };

	/* msleep(20); */ /* doesn't appear to be necessary */
	if ((err = i2c_transfer(state->i2c, &msg, 1)) != 1) {
		printk(KERN_WARNING "or51132: I2C read (addr 0x%02x len %d) error: %d\n",
		       msg.addr, msg.len, err);
		return -EREMOTEIO;
	}
	return 0;
}

/* Reads a 16-bit demod register.  Returns <0 on error. */
static int or51132_readreg(struct or51132_state *state, u8 reg)
{
	u8 buf[2] = { 0x04, reg };
	struct i2c_msg msg[2] = {
		{.addr = state->config->demod_address, .flags = 0,
		 .buf = buf, .len = 2 },
		{.addr = state->config->demod_address, .flags = I2C_M_RD,
		 .buf = buf, .len = 2 }};
	int err;

	if ((err = i2c_transfer(state->i2c, msg, 2)) != 2) {
		printk(KERN_WARNING "or51132: I2C error reading register %d: %d\n",
		       reg, err);
		return -EREMOTEIO;
	}
	return buf[0] | (buf[1] << 8);
}

static int or51132_load_firmware (struct dvb_frontend* fe, const struct firmware *fw)
{
	struct or51132_state* state = fe->demodulator_priv;
	static const u8 run_buf[] = {0x7F,0x01};
	u8 rec_buf[8];
	u32 firmwareAsize, firmwareBsize;
	int i,ret;

	dprintk("Firmware is %Zd bytes\n",fw->size);

	/* Get size of firmware A and B */
	firmwareAsize = le32_to_cpu(*((__le32*)fw->data));
	dprintk("FirmwareA is %i bytes\n",firmwareAsize);
	firmwareBsize = le32_to_cpu(*((__le32*)(fw->data+4)));
	dprintk("FirmwareB is %i bytes\n",firmwareBsize);

	/* Upload firmware */
	if ((ret = or51132_writebuf(state, &fw->data[8], firmwareAsize))) {
		printk(KERN_WARNING "or51132: load_firmware error 1\n");
		return ret;
	}
	if ((ret = or51132_writebuf(state, &fw->data[8+firmwareAsize],
				    firmwareBsize))) {
		printk(KERN_WARNING "or51132: load_firmware error 2\n");
		return ret;
	}

	if ((ret = or51132_writebuf(state, run_buf, 2))) {
		printk(KERN_WARNING "or51132: load_firmware error 3\n");
		return ret;
	}
	if ((ret = or51132_writebuf(state, run_buf, 2))) {
		printk(KERN_WARNING "or51132: load_firmware error 4\n");
		return ret;
	}

	/* 50ms for operation to begin */
	msleep(50);

	/* Read back ucode version to besure we loaded correctly and are really up and running */
	/* Get uCode version */
	if ((ret = or51132_writebytes(state, 0x10, 0x10, 0x00))) {
		printk(KERN_WARNING "or51132: load_firmware error a\n");
		return ret;
	}
	if ((ret = or51132_writebytes(state, 0x04, 0x17))) {
		printk(KERN_WARNING "or51132: load_firmware error b\n");
		return ret;
	}
	if ((ret = or51132_writebytes(state, 0x00, 0x00))) {
		printk(KERN_WARNING "or51132: load_firmware error c\n");
		return ret;
	}
	for (i=0;i<4;i++) {
		/* Once upon a time, this command might have had something
		   to do with getting the firmware version, but it's
		   not used anymore:
		   {0x04,0x00,0x30,0x00,i+1} */
		/* Read 8 bytes, two bytes at a time */
		if ((ret = or51132_readbuf(state, &rec_buf[i*2], 2))) {
			printk(KERN_WARNING
			       "or51132: load_firmware error d - %d\n",i);
			return ret;
		}
	}

	printk(KERN_WARNING
	       "or51132: Version: %02X%02X%02X%02X-%02X%02X%02X%02X (%02X%01X-%01X-%02X%01X-%01X)\n",
	       rec_buf[1],rec_buf[0],rec_buf[3],rec_buf[2],
	       rec_buf[5],rec_buf[4],rec_buf[7],rec_buf[6],
	       rec_buf[3],rec_buf[2]>>4,rec_buf[2]&0x0f,
	       rec_buf[5],rec_buf[4]>>4,rec_buf[4]&0x0f);

	if ((ret = or51132_writebytes(state, 0x10, 0x00, 0x00))) {
		printk(KERN_WARNING "or51132: load_firmware error e\n");
		return ret;
	}
	return 0;
};

static int or51132_init(struct dvb_frontend* fe)
{
	return 0;
}

static int or51132_read_ber(struct dvb_frontend* fe, u32* ber)
{
	*ber = 0;
	return 0;
}

static int or51132_read_ucblocks(struct dvb_frontend* fe, u32* ucblocks)
{
	*ucblocks = 0;
	return 0;
}

static int or51132_sleep(struct dvb_frontend* fe)
{
	return 0;
}

static int or51132_setmode(struct dvb_frontend* fe)
{
	struct or51132_state* state = fe->demodulator_priv;
	u8 cmd_buf1[3] = {0x04, 0x01, 0x5f};
	u8 cmd_buf2[3] = {0x1c, 0x00, 0 };

	dprintk("setmode %d\n",(int)state->current_modulation);

	switch (state->current_modulation) {
	case VSB_8:
		/* Auto CH, Auto NTSC rej, MPEGser, MPEG2tr, phase noise-high */
		cmd_buf1[2] = 0x50;
		/* REC MODE inv IF spectrum, Normal */
		cmd_buf2[1] = 0x03;
		/* Channel MODE ATSC/VSB8 */
		cmd_buf2[2] = 0x06;
		break;
	/* All QAM modes are:
	   Auto-deinterleave; MPEGser, MPEG2tr, phase noise-high
	   REC MODE Normal Carrier Lock */
	case QAM_AUTO:
		/* Channel MODE Auto QAM64/256 */
		cmd_buf2[2] = 0x4f;
		break;
	case QAM_256:
		/* Channel MODE QAM256 */
		cmd_buf2[2] = 0x45;
		break;
	case QAM_64:
		/* Channel MODE QAM64 */
		cmd_buf2[2] = 0x43;
		break;
	default:
		printk(KERN_WARNING
		       "or51132: setmode: Modulation set to unsupported value (%d)\n",
		       state->current_modulation);
		return -EINVAL;
	}

	/* Set Receiver 1 register */
	if (or51132_writebuf(state, cmd_buf1, 3)) {
		printk(KERN_WARNING "or51132: set_mode error 1\n");
		return -EREMOTEIO;
	}
	dprintk("set #1 to %02x\n", cmd_buf1[2]);

	/* Set operation mode in Receiver 6 register */
	if (or51132_writebuf(state, cmd_buf2, 3)) {
		printk(KERN_WARNING "or51132: set_mode error 2\n");
		return -EREMOTEIO;
	}
	dprintk("set #6 to 0x%02x%02x\n", cmd_buf2[1], cmd_buf2[2]);

	return 0;
}

/* Some modulations use the same firmware.  This classifies modulations
   by the firmware they use. */
#define MOD_FWCLASS_UNKNOWN	0
#define MOD_FWCLASS_VSB		1
#define MOD_FWCLASS_QAM		2
static int modulation_fw_class(enum fe_modulation modulation)
{
	switch(modulation) {
	case VSB_8:
		return MOD_FWCLASS_VSB;
	case QAM_AUTO:
	case QAM_64:
	case QAM_256:
		return MOD_FWCLASS_QAM;
	default:
		return MOD_FWCLASS_UNKNOWN;
	}
}

static int or51132_set_parameters(struct dvb_frontend *fe)
{
	struct dtv_frontend_properties *p = &fe->dtv_property_cache;
	int ret;
	struct or51132_state* state = fe->demodulator_priv;
	const struct firmware *fw;
	const char *fwname;
	int clock_mode;

	/* Upload new firmware only if we need a different one */
	if (modulation_fw_class(state->current_modulation) !=
	    modulation_fw_class(p->modulation)) {
		switch (modulation_fw_class(p->modulation)) {
		case MOD_FWCLASS_VSB:
			dprintk("set_parameters VSB MODE\n");
			fwname = OR51132_VSB_FIRMWARE;

			/* Set non-punctured clock for VSB */
			clock_mode = 0;
			break;
		case MOD_FWCLASS_QAM:
			dprintk("set_parameters QAM MODE\n");
			fwname = OR51132_QAM_FIRMWARE;

			/* Set punctured clock for QAM */
			clock_mode = 1;
			break;
		default:
			printk("or51132: Modulation type(%d) UNSUPPORTED\n",
			       p->modulation);
			return -1;
		}
		printk("or51132: Waiting for firmware upload(%s)...\n",
		       fwname);
		ret = request_firmware(&fw, fwname, state->i2c->dev.parent);
		if (ret) {
			printk(KERN_WARNING "or51132: No firmware up"
			       "loaded(timeout or file not found?)\n");
			return ret;
		}
		ret = or51132_load_firmware(fe, fw);
		release_firmware(fw);
		if (ret) {
			printk(KERN_WARNING "or51132: Writing firmware to "
			       "device failed!\n");
			return ret;
		}
		printk("or51132: Firmware upload complete.\n");
		state->config->set_ts_params(fe, clock_mode);
	}
	/* Change only if we are actually changing the modulation */
	if (state->current_modulation != p->modulation) {
		state->current_modulation = p->modulation;
		or51132_setmode(fe);
	}

	if (fe->ops.tuner_ops.set_params) {
		fe->ops.tuner_ops.set_params(fe);
		if (fe->ops.i2c_gate_ctrl) fe->ops.i2c_gate_ctrl(fe, 0);
	}

	/* Set to current mode */
	or51132_setmode(fe);

	/* Update current frequency */
	state->current_frequency = p->frequency;
	return 0;
}

static int or51132_get_parameters(struct dvb_frontend* fe,
				  struct dtv_frontend_properties *p)
{
	struct or51132_state* state = fe->demodulator_priv;
	int status;
	int retry = 1;

start:
	/* Receiver Status */
	if ((status = or51132_readreg(state, 0x00)) < 0) {
		printk(KERN_WARNING "or51132: get_parameters: error reading receiver status\n");
		return -EREMOTEIO;
	}
	switch(status&0xff) {
	case 0x06:
		p->modulation = VSB_8;
		break;
	case 0x43:
		p->modulation = QAM_64;
		break;
	case 0x45:
		p->modulation = QAM_256;
		break;
	default:
		if (retry--)
			goto start;
		printk(KERN_WARNING "or51132: unknown status 0x%02x\n",
		       status&0xff);
		return -EREMOTEIO;
	}

	/* FIXME: Read frequency from frontend, take AFC into account */
	p->frequency = state->current_frequency;

	/* FIXME: How to read inversion setting? Receiver 6 register? */
	p->inversion = INVERSION_AUTO;

	return 0;
}

static int or51132_read_status(struct dvb_frontend *fe, enum fe_status *status)
{
	struct or51132_state* state = fe->demodulator_priv;
	int reg;

	/* Receiver Status */
	if ((reg = or51132_readreg(state, 0x00)) < 0) {
		printk(KERN_WARNING "or51132: read_status: error reading receiver status: %d\n", reg);
		*status = 0;
		return -EREMOTEIO;
	}
	dprintk("%s: read_status %04x\n", __func__, reg);

	if (reg & 0x0100) /* Receiver Lock */
		*status = FE_HAS_SIGNAL|FE_HAS_CARRIER|FE_HAS_VITERBI|
			  FE_HAS_SYNC|FE_HAS_LOCK;
	else
		*status = 0;
	return 0;
}

/* Calculate SNR estimation (scaled by 2^24)

   8-VSB SNR and QAM equations from Oren datasheets

   For 8-VSB:
     SNR[dB] = 10 * log10(897152044.8282 / MSE^2 ) - K

     Where K = 0 if NTSC rejection filter is OFF; and
	   K = 3 if NTSC rejection filter is ON

   For QAM64:
     SNR[dB] = 10 * log10(897152044.8282 / MSE^2 )

   For QAM256:
     SNR[dB] = 10 * log10(907832426.314266  / MSE^2 )

   We re-write the snr equation as:
     SNR * 2^24 = 10*(c - 2*intlog10(MSE))
   Where for QAM256, c = log10(907832426.314266) * 2^24
   and for 8-VSB and QAM64, c = log10(897152044.8282) * 2^24 */

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

static int or51132_read_snr(struct dvb_frontend* fe, u16* snr)
{
	struct or51132_state* state = fe->demodulator_priv;
	int noise, reg;
	u32 c, usK = 0;
	int retry = 1;

start:
	/* SNR after Equalizer */
	noise = or51132_readreg(state, 0x02);
	if (noise < 0) {
		printk(KERN_WARNING "or51132: read_snr: error reading equalizer\n");
		return -EREMOTEIO;
	}
	dprintk("read_snr noise (%d)\n", noise);

	/* Read status, contains modulation type for QAM_AUTO and
	   NTSC filter for VSB */
	reg = or51132_readreg(state, 0x00);
	if (reg < 0) {
		printk(KERN_WARNING "or51132: read_snr: error reading receiver status\n");
		return -EREMOTEIO;
	}

	switch (reg&0xff) {
	case 0x06:
		if (reg & 0x1000) usK = 3 << 24;
		/* Fall through to QAM64 case */
	case 0x43:
		c = 150204167;
		break;
	case 0x45:
		c = 150290396;
		break;
	default:
		printk(KERN_WARNING "or51132: unknown status 0x%02x\n", reg&0xff);
		if (retry--) goto start;
		return -EREMOTEIO;
	}
	dprintk("%s: modulation %02x, NTSC rej O%s\n", __func__,
		reg&0xff, reg&0x1000?"n":"ff");

	/* Calculate SNR using noise, c, and NTSC rejection correction */
	state->snr = calculate_snr(noise, c) - usK;
	*snr = (state->snr) >> 16;

	dprintk("%s: noise = 0x%08x, snr = %d.%02d dB\n", __func__, noise,
		state->snr >> 24, (((state->snr>>8) & 0xffff) * 100) >> 16);

	return 0;
}

static int or51132_read_signal_strength(struct dvb_frontend* fe, u16* strength)
{
	/* Calculate Strength from SNR up to 35dB */
	/* Even though the SNR can go higher than 35dB, there is some comfort */
	/* factor in having a range of strong signals that can show at 100%   */
	struct or51132_state* state = (struct or51132_state*) fe->demodulator_priv;
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

static int or51132_get_tune_settings(struct dvb_frontend* fe, struct dvb_frontend_tune_settings* fe_tune_settings)
{
	fe_tune_settings->min_delay_ms = 500;
	fe_tune_settings->step_size = 0;
	fe_tune_settings->max_drift = 0;

	return 0;
}

static void or51132_release(struct dvb_frontend* fe)
{
	struct or51132_state* state = fe->demodulator_priv;
	kfree(state);
}

static struct dvb_frontend_ops or51132_ops;

struct dvb_frontend* or51132_attach(const struct or51132_config* config,
				    struct i2c_adapter* i2c)
{
	struct or51132_state* state = NULL;

	/* Allocate memory for the internal state */
	state = kzalloc(sizeof(struct or51132_state), GFP_KERNEL);
	if (state == NULL)
		return NULL;

	/* Setup the state */
	state->config = config;
	state->i2c = i2c;
	state->current_frequency = -1;
	state->current_modulation = -1;

	/* Create dvb_frontend */
	memcpy(&state->frontend.ops, &or51132_ops, sizeof(struct dvb_frontend_ops));
	state->frontend.demodulator_priv = state;
	return &state->frontend;
}

static struct dvb_frontend_ops or51132_ops = {
	.delsys = { SYS_ATSC, SYS_DVBC_ANNEX_B },
	.info = {
		.name			= "Oren OR51132 VSB/QAM Frontend",
		.frequency_min		= 44000000,
		.frequency_max		= 958000000,
		.frequency_stepsize	= 166666,
		.caps = FE_CAN_FEC_1_2 | FE_CAN_FEC_2_3 | FE_CAN_FEC_3_4 |
			FE_CAN_FEC_5_6 | FE_CAN_FEC_7_8 | FE_CAN_FEC_AUTO |
			FE_CAN_QAM_64 | FE_CAN_QAM_256 | FE_CAN_QAM_AUTO |
			FE_CAN_8VSB
	},

	.release = or51132_release,

	.init = or51132_init,
	.sleep = or51132_sleep,

	.set_frontend = or51132_set_parameters,
	.get_frontend = or51132_get_parameters,
	.get_tune_settings = or51132_get_tune_settings,

	.read_status = or51132_read_status,
	.read_ber = or51132_read_ber,
	.read_signal_strength = or51132_read_signal_strength,
	.read_snr = or51132_read_snr,
	.read_ucblocks = or51132_read_ucblocks,
};

module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "Turn on/off frontend debugging (default:off).");

MODULE_DESCRIPTION("OR51132 ATSC [pcHDTV HD-3000] (8VSB & ITU J83 AnnexB FEC QAM64/256) Demodulator Driver");
MODULE_AUTHOR("Kirk Lapray");
MODULE_AUTHOR("Trent Piepho");
MODULE_LICENSE("GPL");

EXPORT_SYMBOL(or51132_attach);

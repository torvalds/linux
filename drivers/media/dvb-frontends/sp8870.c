// SPDX-License-Identifier: GPL-2.0-or-later
/*
    Driver for Spase SP8870 demodulator

    Copyright (C) 1999 Juergen Peitz


*/
/*
 * This driver needs external firmware. Please use the command
 * "<kerneldir>/scripts/get_dvb_firmware alps_tdlb7" to
 * download/extract it, and then copy it to /usr/lib/hotplug/firmware
 * or /lib/firmware (depending on configuration of firmware hotplug).
 */
#define SP8870_DEFAULT_FIRMWARE "dvb-fe-sp8870.fw"

#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/firmware.h>
#include <linux/delay.h>
#include <linux/string.h>
#include <linux/slab.h>

#include <media/dvb_frontend.h>
#include "sp8870.h"


struct sp8870_state {

	struct i2c_adapter* i2c;

	const struct sp8870_config* config;

	struct dvb_frontend frontend;

	/* demodulator private data */
	u8 initialised:1;
};

static int debug;
#define dprintk(args...) \
	do { \
		if (debug) printk(KERN_DEBUG "sp8870: " args); \
	} while (0)

/* firmware size for sp8870 */
#define SP8870_FIRMWARE_SIZE 16382

/* starting point for firmware in file 'Sc_main.mc' */
#define SP8870_FIRMWARE_OFFSET 0x0A

static int sp8870_writereg (struct sp8870_state* state, u16 reg, u16 data)
{
	u8 buf [] = { reg >> 8, reg & 0xff, data >> 8, data & 0xff };
	struct i2c_msg msg = { .addr = state->config->demod_address, .flags = 0, .buf = buf, .len = 4 };
	int err;

	if ((err = i2c_transfer (state->i2c, &msg, 1)) != 1) {
		dprintk ("%s: writereg error (err == %i, reg == 0x%02x, data == 0x%02x)\n", __func__, err, reg, data);
		return -EREMOTEIO;
	}

	return 0;
}

static int sp8870_readreg (struct sp8870_state* state, u16 reg)
{
	int ret;
	u8 b0 [] = { reg >> 8 , reg & 0xff };
	u8 b1 [] = { 0, 0 };
	struct i2c_msg msg [] = { { .addr = state->config->demod_address, .flags = 0, .buf = b0, .len = 2 },
			   { .addr = state->config->demod_address, .flags = I2C_M_RD, .buf = b1, .len = 2 } };

	ret = i2c_transfer (state->i2c, msg, 2);

	if (ret != 2) {
		dprintk("%s: readreg error (ret == %i)\n", __func__, ret);
		return -1;
	}

	return (b1[0] << 8 | b1[1]);
}

static int sp8870_firmware_upload (struct sp8870_state* state, const struct firmware *fw)
{
	struct i2c_msg msg;
	const char *fw_buf = fw->data;
	int fw_pos;
	u8 tx_buf[255];
	int tx_len;
	int err = 0;

	dprintk ("%s: ...\n", __func__);

	if (fw->size < SP8870_FIRMWARE_SIZE + SP8870_FIRMWARE_OFFSET)
		return -EINVAL;

	// system controller stop
	sp8870_writereg(state, 0x0F00, 0x0000);

	// instruction RAM register hiword
	sp8870_writereg(state, 0x8F08, ((SP8870_FIRMWARE_SIZE / 2) & 0xFFFF));

	// instruction RAM MWR
	sp8870_writereg(state, 0x8F0A, ((SP8870_FIRMWARE_SIZE / 2) >> 16));

	// do firmware upload
	fw_pos = SP8870_FIRMWARE_OFFSET;
	while (fw_pos < SP8870_FIRMWARE_SIZE + SP8870_FIRMWARE_OFFSET){
		tx_len = (fw_pos <= SP8870_FIRMWARE_SIZE + SP8870_FIRMWARE_OFFSET - 252) ? 252 : SP8870_FIRMWARE_SIZE + SP8870_FIRMWARE_OFFSET - fw_pos;
		// write register 0xCF0A
		tx_buf[0] = 0xCF;
		tx_buf[1] = 0x0A;
		memcpy(&tx_buf[2], fw_buf + fw_pos, tx_len);
		msg.addr = state->config->demod_address;
		msg.flags = 0;
		msg.buf = tx_buf;
		msg.len = tx_len + 2;
		if ((err = i2c_transfer (state->i2c, &msg, 1)) != 1) {
			printk("%s: firmware upload failed!\n", __func__);
			printk ("%s: i2c error (err == %i)\n", __func__, err);
			return err;
		}
		fw_pos += tx_len;
	}

	dprintk ("%s: done!\n", __func__);
	return 0;
};

static void sp8870_microcontroller_stop (struct sp8870_state* state)
{
	sp8870_writereg(state, 0x0F08, 0x000);
	sp8870_writereg(state, 0x0F09, 0x000);

	// microcontroller STOP
	sp8870_writereg(state, 0x0F00, 0x000);
}

static void sp8870_microcontroller_start (struct sp8870_state* state)
{
	sp8870_writereg(state, 0x0F08, 0x000);
	sp8870_writereg(state, 0x0F09, 0x000);

	// microcontroller START
	sp8870_writereg(state, 0x0F00, 0x001);
	// not documented but if we don't read 0x0D01 out here
	// we don't get a correct data valid signal
	sp8870_readreg(state, 0x0D01);
}

static int sp8870_read_data_valid_signal(struct sp8870_state* state)
{
	return (sp8870_readreg(state, 0x0D02) > 0);
}

static int configure_reg0xc05 (struct dtv_frontend_properties *p, u16 *reg0xc05)
{
	int known_parameters = 1;

	*reg0xc05 = 0x000;

	switch (p->modulation) {
	case QPSK:
		break;
	case QAM_16:
		*reg0xc05 |= (1 << 10);
		break;
	case QAM_64:
		*reg0xc05 |= (2 << 10);
		break;
	case QAM_AUTO:
		known_parameters = 0;
		break;
	default:
		return -EINVAL;
	}

	switch (p->hierarchy) {
	case HIERARCHY_NONE:
		break;
	case HIERARCHY_1:
		*reg0xc05 |= (1 << 7);
		break;
	case HIERARCHY_2:
		*reg0xc05 |= (2 << 7);
		break;
	case HIERARCHY_4:
		*reg0xc05 |= (3 << 7);
		break;
	case HIERARCHY_AUTO:
		known_parameters = 0;
		break;
	default:
		return -EINVAL;
	}

	switch (p->code_rate_HP) {
	case FEC_1_2:
		break;
	case FEC_2_3:
		*reg0xc05 |= (1 << 3);
		break;
	case FEC_3_4:
		*reg0xc05 |= (2 << 3);
		break;
	case FEC_5_6:
		*reg0xc05 |= (3 << 3);
		break;
	case FEC_7_8:
		*reg0xc05 |= (4 << 3);
		break;
	case FEC_AUTO:
		known_parameters = 0;
		break;
	default:
		return -EINVAL;
	}

	if (known_parameters)
		*reg0xc05 |= (2 << 1);	/* use specified parameters */
	else
		*reg0xc05 |= (1 << 1);	/* enable autoprobing */

	return 0;
}

static int sp8870_wake_up(struct sp8870_state* state)
{
	// enable TS output and interface pins
	return sp8870_writereg(state, 0xC18, 0x00D);
}

static int sp8870_set_frontend_parameters(struct dvb_frontend *fe)
{
	struct dtv_frontend_properties *p = &fe->dtv_property_cache;
	struct sp8870_state* state = fe->demodulator_priv;
	int  err;
	u16 reg0xc05;

	if ((err = configure_reg0xc05(p, &reg0xc05)))
		return err;

	// system controller stop
	sp8870_microcontroller_stop(state);

	// set tuner parameters
	if (fe->ops.tuner_ops.set_params) {
		fe->ops.tuner_ops.set_params(fe);
		if (fe->ops.i2c_gate_ctrl) fe->ops.i2c_gate_ctrl(fe, 0);
	}

	// sample rate correction bit [23..17]
	sp8870_writereg(state, 0x0319, 0x000A);

	// sample rate correction bit [16..0]
	sp8870_writereg(state, 0x031A, 0x0AAB);

	// integer carrier offset
	sp8870_writereg(state, 0x0309, 0x0400);

	// fractional carrier offset
	sp8870_writereg(state, 0x030A, 0x0000);

	// filter for 6/7/8 Mhz channel
	if (p->bandwidth_hz == 6000000)
		sp8870_writereg(state, 0x0311, 0x0002);
	else if (p->bandwidth_hz == 7000000)
		sp8870_writereg(state, 0x0311, 0x0001);
	else
		sp8870_writereg(state, 0x0311, 0x0000);

	// scan order: 2k first = 0x0000, 8k first = 0x0001
	if (p->transmission_mode == TRANSMISSION_MODE_2K)
		sp8870_writereg(state, 0x0338, 0x0000);
	else
		sp8870_writereg(state, 0x0338, 0x0001);

	sp8870_writereg(state, 0xc05, reg0xc05);

	// read status reg in order to clear pending irqs
	err = sp8870_readreg(state, 0x200);
	if (err)
		return err;

	// system controller start
	sp8870_microcontroller_start(state);

	return 0;
}

static int sp8870_init (struct dvb_frontend* fe)
{
	struct sp8870_state* state = fe->demodulator_priv;
	const struct firmware *fw = NULL;

	sp8870_wake_up(state);
	if (state->initialised) return 0;
	state->initialised = 1;

	dprintk ("%s\n", __func__);


	/* request the firmware, this will block until someone uploads it */
	printk("sp8870: waiting for firmware upload (%s)...\n", SP8870_DEFAULT_FIRMWARE);
	if (state->config->request_firmware(fe, &fw, SP8870_DEFAULT_FIRMWARE)) {
		printk("sp8870: no firmware upload (timeout or file not found?)\n");
		return -EIO;
	}

	if (sp8870_firmware_upload(state, fw)) {
		printk("sp8870: writing firmware to device failed\n");
		release_firmware(fw);
		return -EIO;
	}
	release_firmware(fw);
	printk("sp8870: firmware upload complete\n");

	/* enable TS output and interface pins */
	sp8870_writereg(state, 0xc18, 0x00d);

	// system controller stop
	sp8870_microcontroller_stop(state);

	// ADC mode
	sp8870_writereg(state, 0x0301, 0x0003);

	// Reed Solomon parity bytes passed to output
	sp8870_writereg(state, 0x0C13, 0x0001);

	// MPEG clock is suppressed if no valid data
	sp8870_writereg(state, 0x0C14, 0x0001);

	/* bit 0x010: enable data valid signal */
	sp8870_writereg(state, 0x0D00, 0x010);
	sp8870_writereg(state, 0x0D01, 0x000);

	return 0;
}

static int sp8870_read_status(struct dvb_frontend *fe,
			      enum fe_status *fe_status)
{
	struct sp8870_state* state = fe->demodulator_priv;
	int status;
	int signal;

	*fe_status = 0;

	status = sp8870_readreg (state, 0x0200);
	if (status < 0)
		return -EIO;

	signal = sp8870_readreg (state, 0x0303);
	if (signal < 0)
		return -EIO;

	if (signal > 0x0F)
		*fe_status |= FE_HAS_SIGNAL;
	if (status & 0x08)
		*fe_status |= FE_HAS_SYNC;
	if (status & 0x04)
		*fe_status |= FE_HAS_LOCK | FE_HAS_CARRIER | FE_HAS_VITERBI;

	return 0;
}

static int sp8870_read_ber (struct dvb_frontend* fe, u32 * ber)
{
	struct sp8870_state* state = fe->demodulator_priv;
	int ret;
	u32 tmp;

	*ber = 0;

	ret = sp8870_readreg(state, 0xC08);
	if (ret < 0)
		return -EIO;

	tmp = ret & 0x3F;

	ret = sp8870_readreg(state, 0xC07);
	if (ret < 0)
		return -EIO;

	tmp = ret << 6;
	if (tmp >= 0x3FFF0)
		tmp = ~0;

	*ber = tmp;

	return 0;
}

static int sp8870_read_signal_strength(struct dvb_frontend* fe,  u16 * signal)
{
	struct sp8870_state* state = fe->demodulator_priv;
	int ret;
	u16 tmp;

	*signal = 0;

	ret = sp8870_readreg (state, 0x306);
	if (ret < 0)
		return -EIO;

	tmp = ret << 8;

	ret = sp8870_readreg (state, 0x303);
	if (ret < 0)
		return -EIO;

	tmp |= ret;

	if (tmp)
		*signal = 0xFFFF - tmp;

	return 0;
}

static int sp8870_read_uncorrected_blocks (struct dvb_frontend* fe, u32* ublocks)
{
	struct sp8870_state* state = fe->demodulator_priv;
	int ret;

	*ublocks = 0;

	ret = sp8870_readreg(state, 0xC0C);
	if (ret < 0)
		return -EIO;

	if (ret == 0xFFFF)
		ret = ~0;

	*ublocks = ret;

	return 0;
}

/* number of trials to recover from lockup */
#define MAXTRIALS 5
/* maximum checks for data valid signal */
#define MAXCHECKS 100

/* only for debugging: counter for detected lockups */
static int lockups;
/* only for debugging: counter for channel switches */
static int switches;

static int sp8870_set_frontend(struct dvb_frontend *fe)
{
	struct dtv_frontend_properties *p = &fe->dtv_property_cache;
	struct sp8870_state* state = fe->demodulator_priv;

	/*
	    The firmware of the sp8870 sometimes locks up after setting frontend parameters.
	    We try to detect this by checking the data valid signal.
	    If it is not set after MAXCHECKS we try to recover the lockup by setting
	    the frontend parameters again.
	*/

	int err = 0;
	int valid = 0;
	int trials = 0;
	int check_count = 0;

	dprintk("%s: frequency = %i\n", __func__, p->frequency);

	for (trials = 1; trials <= MAXTRIALS; trials++) {

		err = sp8870_set_frontend_parameters(fe);
		if (err)
			return err;

		for (check_count = 0; check_count < MAXCHECKS; check_count++) {
//			valid = ((sp8870_readreg(i2c, 0x0200) & 4) == 0);
			valid = sp8870_read_data_valid_signal(state);
			if (valid) {
				dprintk("%s: delay = %i usec\n",
					__func__, check_count * 10);
				break;
			}
			udelay(10);
		}
		if (valid)
			break;
	}

	if (!valid) {
		printk("%s: firmware crash!!!!!!\n", __func__);
		return -EIO;
	}

	if (debug) {
		if (valid) {
			if (trials > 1) {
				printk("%s: firmware lockup!!!\n", __func__);
				printk("%s: recovered after %i trial(s))\n",  __func__, trials - 1);
				lockups++;
			}
		}
		switches++;
		printk("%s: switches = %i lockups = %i\n", __func__, switches, lockups);
	}

	return 0;
}

static int sp8870_sleep(struct dvb_frontend* fe)
{
	struct sp8870_state* state = fe->demodulator_priv;

	// tristate TS output and disable interface pins
	return sp8870_writereg(state, 0xC18, 0x000);
}

static int sp8870_get_tune_settings(struct dvb_frontend* fe, struct dvb_frontend_tune_settings* fesettings)
{
	fesettings->min_delay_ms = 350;
	fesettings->step_size = 0;
	fesettings->max_drift = 0;
	return 0;
}

static int sp8870_i2c_gate_ctrl(struct dvb_frontend* fe, int enable)
{
	struct sp8870_state* state = fe->demodulator_priv;

	if (enable) {
		return sp8870_writereg(state, 0x206, 0x001);
	} else {
		return sp8870_writereg(state, 0x206, 0x000);
	}
}

static void sp8870_release(struct dvb_frontend* fe)
{
	struct sp8870_state* state = fe->demodulator_priv;
	kfree(state);
}

static const struct dvb_frontend_ops sp8870_ops;

struct dvb_frontend* sp8870_attach(const struct sp8870_config* config,
				   struct i2c_adapter* i2c)
{
	struct sp8870_state* state = NULL;

	/* allocate memory for the internal state */
	state = kzalloc(sizeof(struct sp8870_state), GFP_KERNEL);
	if (state == NULL) goto error;

	/* setup the state */
	state->config = config;
	state->i2c = i2c;
	state->initialised = 0;

	/* check if the demod is there */
	if (sp8870_readreg(state, 0x0200) < 0) goto error;

	/* create dvb_frontend */
	memcpy(&state->frontend.ops, &sp8870_ops, sizeof(struct dvb_frontend_ops));
	state->frontend.demodulator_priv = state;
	return &state->frontend;

error:
	kfree(state);
	return NULL;
}

static const struct dvb_frontend_ops sp8870_ops = {
	.delsys = { SYS_DVBT },
	.info = {
		.name			= "Spase SP8870 DVB-T",
		.frequency_min_hz	= 470 * MHz,
		.frequency_max_hz	= 860 * MHz,
		.frequency_stepsize_hz	= 166666,
		.caps			= FE_CAN_FEC_1_2 | FE_CAN_FEC_2_3 |
					  FE_CAN_FEC_3_4 | FE_CAN_FEC_5_6 |
					  FE_CAN_FEC_7_8 | FE_CAN_FEC_AUTO |
					  FE_CAN_QPSK | FE_CAN_QAM_16 |
					  FE_CAN_QAM_64 | FE_CAN_QAM_AUTO |
					  FE_CAN_HIERARCHY_AUTO |  FE_CAN_RECOVER
	},

	.release = sp8870_release,

	.init = sp8870_init,
	.sleep = sp8870_sleep,
	.i2c_gate_ctrl = sp8870_i2c_gate_ctrl,

	.set_frontend = sp8870_set_frontend,
	.get_tune_settings = sp8870_get_tune_settings,

	.read_status = sp8870_read_status,
	.read_ber = sp8870_read_ber,
	.read_signal_strength = sp8870_read_signal_strength,
	.read_ucblocks = sp8870_read_uncorrected_blocks,
};

module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "Turn on/off frontend debugging (default:off).");

MODULE_DESCRIPTION("Spase SP8870 DVB-T Demodulator driver");
MODULE_AUTHOR("Juergen Peitz");
MODULE_LICENSE("GPL");

EXPORT_SYMBOL(sp8870_attach);

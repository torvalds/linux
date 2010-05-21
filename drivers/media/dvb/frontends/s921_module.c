/*
 * Driver for Sharp s921 driver
 *
 * Copyright (C) 2008 Markus Rechberger <mrechberger@sundtek.de>
 *
 * All rights reserved.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include "dvb_frontend.h"
#include "s921_module.h"
#include "s921_core.h"

static  unsigned int debug = 0;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug,"s921 debugging (default off)");

#define dprintk(fmt, args...) if (debug) do {\
			printk("s921 debug: " fmt, ##args); } while (0)

struct s921_state
{
	struct dvb_frontend frontend;
	fe_modulation_t current_modulation;
	__u32 snr;
	__u32 current_frequency;
	__u8 addr;
	struct s921_isdb_t dev;
	struct i2c_adapter *i2c;
};

static int s921_set_parameters(struct dvb_frontend *fe, struct dvb_frontend_parameters *param) {
	struct s921_state *state = (struct s921_state *)fe->demodulator_priv;
	struct s921_isdb_t_transmission_mode_params params;
	struct s921_isdb_t_tune_params tune_params;

	tune_params.frequency = param->frequency;
	s921_isdb_cmd(&state->dev, ISDB_T_CMD_SET_PARAM, &params);
	s921_isdb_cmd(&state->dev, ISDB_T_CMD_TUNE, &tune_params);
	mdelay(100);
	return 0;
}

static int s921_init(struct dvb_frontend *fe) {
	printk("s921 init\n");
	return 0;
}

static int s921_sleep(struct dvb_frontend *fe) {
	printk("s921 sleep\n");
	return 0;
}

static int s921_read_status(struct dvb_frontend *fe, fe_status_t *status)
{
	struct s921_state *state = (struct s921_state *)fe->demodulator_priv;
	unsigned int ret;
	mdelay(5);
	s921_isdb_cmd(&state->dev, ISDB_T_CMD_GET_STATUS, &ret);
	*status = 0;

	printk("status: %02x\n", ret);
	if (ret == 1) {
		*status |= FE_HAS_CARRIER;
		*status |= FE_HAS_VITERBI;
		*status |= FE_HAS_LOCK;
		*status |= FE_HAS_SYNC;
		*status |= FE_HAS_SIGNAL;
	}

	return 0;
}

static int s921_read_ber(struct dvb_frontend *fe, __u32 *ber)
{
	dprintk("read ber\n");
	return 0;
}

static int s921_read_snr(struct dvb_frontend *fe, __u16 *snr)
{
	dprintk("read snr\n");
	return 0;
}

static int s921_read_ucblocks(struct dvb_frontend *fe, __u32 *ucblocks)
{
	dprintk("read ucblocks\n");
	return 0;
}

static void s921_release(struct dvb_frontend *fe)
{
	struct s921_state *state = (struct s921_state *)fe->demodulator_priv;
	kfree(state);
}

static struct dvb_frontend_ops demod_s921={
	.info = {
		.name			= "SHARP S921",
		.type			= FE_OFDM,
		.frequency_min		= 473143000,
		.frequency_max		= 767143000,
		.frequency_stepsize	=   6000000,
		.frequency_tolerance	= 0,
		.caps = FE_CAN_FEC_1_2 | FE_CAN_FEC_2_3 |
			FE_CAN_FEC_3_4 | FE_CAN_FEC_5_6 | FE_CAN_FEC_7_8 |
			FE_CAN_FEC_AUTO |
			FE_CAN_QPSK | FE_CAN_QAM_16 | FE_CAN_QAM_64 | FE_CAN_QAM_AUTO |
			FE_CAN_TRANSMISSION_MODE_AUTO | FE_CAN_GUARD_INTERVAL_AUTO |
			FE_CAN_HIERARCHY_AUTO | FE_CAN_RECOVER |
			FE_CAN_MUTE_TS
	},
	.init = s921_init,
	.sleep = s921_sleep,
	.set_frontend = s921_set_parameters,
	.read_snr = s921_read_snr,
	.read_ber = s921_read_ber,
	.read_status = s921_read_status,
	.read_ucblocks = s921_read_ucblocks,
	.release = s921_release,
};

static int s921_write(void *dev, u8 reg, u8 val) {
	struct s921_state *state = dev;
	char buf[2]={reg,val};
	int err;
	struct i2c_msg i2cmsgs = {
		.addr = state->addr,
		.flags = 0,
		.len = 2,
		.buf = buf
	};

	if((err = i2c_transfer(state->i2c, &i2cmsgs, 1))<0) {
		printk("%s i2c_transfer error %d\n", __func__, err);
		if (err < 0)
			return err;
		else
			return -EREMOTEIO;
	}

	return 0;
}

static int s921_read(void *dev, u8 reg) {
	struct s921_state *state = dev;
	u8 b1;
	int ret;
	struct i2c_msg msg[2] = { { .addr = state->addr,
				    .flags = 0,
				    .buf = &reg, .len = 1 },
				  { .addr = state->addr,
				    .flags = I2C_M_RD,
				    .buf = &b1, .len = 1 } };

	ret = i2c_transfer(state->i2c, msg, 2);
	if (ret != 2)
		return ret;
	return b1;
}

struct dvb_frontend* s921_attach(const struct s921_config *config,
					   struct i2c_adapter *i2c)
{

	struct s921_state *state;
	state = kzalloc(sizeof(struct s921_state), GFP_KERNEL);
	if (state == NULL)
		return NULL;

	state->addr = config->i2c_address;
	state->i2c = i2c;
	state->dev.i2c_write = &s921_write;
	state->dev.i2c_read = &s921_read;
	state->dev.priv_dev = state;

	s921_isdb_cmd(&state->dev, ISDB_T_CMD_INIT, NULL);

	memcpy(&state->frontend.ops, &demod_s921, sizeof(struct dvb_frontend_ops));
	state->frontend.demodulator_priv = state;
	return &state->frontend;
}

EXPORT_SYMBOL_GPL(s921_attach);
MODULE_AUTHOR("Markus Rechberger <mrechberger@empiatech.com>");
MODULE_DESCRIPTION("Sharp S921 ISDB-T 1Seg");
MODULE_LICENSE("GPL");

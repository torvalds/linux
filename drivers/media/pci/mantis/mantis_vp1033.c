// SPDX-License-Identifier: GPL-2.0-or-later
/*
	Mantis VP-1033 driver

	Copyright (C) Manu Abraham (abraham.manu@gmail.com)

*/

#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/interrupt.h>

#include <media/dmxdev.h>
#include <media/dvbdev.h>
#include <media/dvb_demux.h>
#include <media/dvb_frontend.h>
#include <media/dvb_net.h>

#include "stv0299.h"
#include "mantis_common.h"
#include "mantis_ioc.h"
#include "mantis_dvb.h"
#include "mantis_vp1033.h"
#include "mantis_reg.h"

static u8 lgtdqcs001f_inittab[] = {
	0x01, 0x15,
	0x02, 0x30,
	0x03, 0x00,
	0x04, 0x2a,
	0x05, 0x85,
	0x06, 0x02,
	0x07, 0x00,
	0x08, 0x00,
	0x0c, 0x01,
	0x0d, 0x81,
	0x0e, 0x44,
	0x0f, 0x94,
	0x10, 0x3c,
	0x11, 0x84,
	0x12, 0xb9,
	0x13, 0xb5,
	0x14, 0x4f,
	0x15, 0xc9,
	0x16, 0x80,
	0x17, 0x36,
	0x18, 0xfb,
	0x19, 0xcf,
	0x1a, 0xbc,
	0x1c, 0x2b,
	0x1d, 0x27,
	0x1e, 0x00,
	0x1f, 0x0b,
	0x20, 0xa1,
	0x21, 0x60,
	0x22, 0x00,
	0x23, 0x00,
	0x28, 0x00,
	0x29, 0x28,
	0x2a, 0x14,
	0x2b, 0x0f,
	0x2c, 0x09,
	0x2d, 0x05,
	0x31, 0x1f,
	0x32, 0x19,
	0x33, 0xfc,
	0x34, 0x13,
	0xff, 0xff,
};

#define MANTIS_MODEL_NAME	"VP-1033"
#define MANTIS_DEV_TYPE		"DVB-S/DSS"

static int lgtdqcs001f_tuner_set(struct dvb_frontend *fe)
{
	struct dtv_frontend_properties *p = &fe->dtv_property_cache;
	struct mantis_pci *mantis	= fe->dvb->priv;
	struct i2c_adapter *adapter	= &mantis->adapter;

	u8 buf[4];
	u32 div;


	struct i2c_msg msg = {.addr = 0x61, .flags = 0, .buf = buf, .len = sizeof(buf)};

	div = p->frequency / 250;

	buf[0] = (div >> 8) & 0x7f;
	buf[1] =  div & 0xff;
	buf[2] =  0x83;
	buf[3] =  0xc0;

	if (p->frequency < 1531000)
		buf[3] |= 0x04;
	else
		buf[3] &= ~0x04;
	if (i2c_transfer(adapter, &msg, 1) < 0) {
		dprintk(MANTIS_ERROR, 1, "Write: I2C Transfer failed");
		return -EIO;
	}
	msleep_interruptible(100);

	return 0;
}

static int lgtdqcs001f_set_symbol_rate(struct dvb_frontend *fe,
				       u32 srate, u32 ratio)
{
	u8 aclk = 0;
	u8 bclk = 0;

	if (srate < 1500000) {
		aclk = 0xb7;
		bclk = 0x47;
	} else if (srate < 3000000) {
		aclk = 0xb7;
		bclk = 0x4b;
	} else if (srate < 7000000) {
		aclk = 0xb7;
		bclk = 0x4f;
	} else if (srate < 14000000) {
		aclk = 0xb7;
		bclk = 0x53;
	} else if (srate < 30000000) {
		aclk = 0xb6;
		bclk = 0x53;
	} else if (srate < 45000000) {
		aclk = 0xb4;
		bclk = 0x51;
	}
	stv0299_writereg(fe, 0x13, aclk);
	stv0299_writereg(fe, 0x14, bclk);

	stv0299_writereg(fe, 0x1f, (ratio >> 16) & 0xff);
	stv0299_writereg(fe, 0x20, (ratio >>  8) & 0xff);
	stv0299_writereg(fe, 0x21,  ratio & 0xf0);

	return 0;
}

static struct stv0299_config lgtdqcs001f_config = {
	.demod_address		= 0x68,
	.inittab		= lgtdqcs001f_inittab,
	.mclk			= 88000000UL,
	.invert			= 0,
	.skip_reinit		= 0,
	.volt13_op0_op1		= STV0299_VOLT13_OP0,
	.min_delay_ms		= 100,
	.set_symbol_rate	= lgtdqcs001f_set_symbol_rate,
};

static int vp1033_frontend_init(struct mantis_pci *mantis, struct dvb_frontend *fe)
{
	struct i2c_adapter *adapter	= &mantis->adapter;

	int err = 0;

	err = mantis_frontend_power(mantis, POWER_ON);
	if (err == 0) {
		mantis_frontend_soft_reset(mantis);
		msleep(250);

		dprintk(MANTIS_ERROR, 1, "Probing for STV0299 (DVB-S)");
		fe = dvb_attach(stv0299_attach, &lgtdqcs001f_config, adapter);

		if (fe) {
			fe->ops.tuner_ops.set_params = lgtdqcs001f_tuner_set;
			dprintk(MANTIS_ERROR, 1, "found STV0299 DVB-S frontend @ 0x%02x",
				lgtdqcs001f_config.demod_address);

			dprintk(MANTIS_ERROR, 1, "Mantis DVB-S STV0299 frontend attach success");
		} else {
			return -1;
		}
	} else {
		dprintk(MANTIS_ERROR, 1, "Frontend on <%s> POWER ON failed! <%d>",
			adapter->name,
			err);

		return -EIO;
	}
	mantis->fe = fe;
	dprintk(MANTIS_ERROR, 1, "Done!");

	return 0;
}

struct mantis_hwconfig vp1033_config = {
	.model_name		= MANTIS_MODEL_NAME,
	.dev_type		= MANTIS_DEV_TYPE,
	.ts_size		= MANTIS_TS_204,

	.baud_rate		= MANTIS_BAUD_9600,
	.parity			= MANTIS_PARITY_NONE,
	.bytes			= 0,

	.frontend_init		= vp1033_frontend_init,
	.power			= GPIF_A12,
	.reset			= GPIF_A13,
};

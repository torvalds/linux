// SPDX-License-Identifier: GPL-2.0-or-later
/*
	Mantis VP-3030 driver

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

#include "zl10353.h"
#include "tda665x.h"
#include "mantis_common.h"
#include "mantis_ioc.h"
#include "mantis_dvb.h"
#include "mantis_vp3030.h"

static struct zl10353_config mantis_vp3030_config = {
	.demod_address		= 0x0f,
};

static struct tda665x_config env57h12d5_config = {
	.name			= "ENV57H12D5 (ET-50DT)",
	.addr			= 0x60,
	.frequency_min		=  47 * MHz,
	.frequency_max		= 862 * MHz,
	.frequency_offst	=   3616667,
	.ref_multiplier		= 6, /* 1/6 MHz */
	.ref_divider		= 100000, /* 1/6 MHz */
};

#define MANTIS_MODEL_NAME	"VP-3030"
#define MANTIS_DEV_TYPE		"DVB-T"


static int vp3030_frontend_init(struct mantis_pci *mantis, struct dvb_frontend *fe)
{
	struct i2c_adapter *adapter	= &mantis->adapter;
	struct mantis_hwconfig *config	= mantis->hwconfig;
	int err = 0;

	mantis_gpio_set_bits(mantis, config->reset, 0);
	msleep(100);
	err = mantis_frontend_power(mantis, POWER_ON);
	msleep(100);
	mantis_gpio_set_bits(mantis, config->reset, 1);

	if (err == 0) {
		msleep(250);
		dprintk(MANTIS_ERROR, 1, "Probing for 10353 (DVB-T)");
		fe = dvb_attach(zl10353_attach, &mantis_vp3030_config, adapter);

		if (!fe)
			return -1;

		dvb_attach(tda665x_attach, fe, &env57h12d5_config, adapter);
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

struct mantis_hwconfig vp3030_config = {
	.model_name	= MANTIS_MODEL_NAME,
	.dev_type	= MANTIS_DEV_TYPE,
	.ts_size	= MANTIS_TS_188,

	.baud_rate	= MANTIS_BAUD_9600,
	.parity		= MANTIS_PARITY_NONE,
	.bytes		= 0,

	.frontend_init	= vp3030_frontend_init,
	.power		= GPIF_A12,
	.reset		= GPIF_A13,

	.i2c_mode	= MANTIS_BYTE_MODE
};

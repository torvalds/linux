// SPDX-License-Identifier: GPL-2.0-or-later
/*
	Hopper VP-3028 driver

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
#include "mantis_common.h"
#include "mantis_ioc.h"
#include "mantis_dvb.h"
#include "hopper_vp3028.h"

static struct zl10353_config hopper_vp3028_config = {
	.demod_address	= 0x0f,
};

#define MANTIS_MODEL_NAME	"VP-3028"
#define MANTIS_DEV_TYPE		"DVB-T"

static int vp3028_frontend_init(struct mantis_pci *mantis, struct dvb_frontend *fe)
{
	struct i2c_adapter *adapter	= &mantis->adapter;
	struct mantis_hwconfig *config	= mantis->hwconfig;
	int err = 0;

	mantis_gpio_set_bits(mantis, config->reset, 0);
	msleep(100);
	err = mantis_frontend_power(mantis, POWER_ON);
	msleep(100);
	mantis_gpio_set_bits(mantis, config->reset, 1);

	err = mantis_frontend_power(mantis, POWER_ON);
	if (err == 0) {
		msleep(250);
		dprintk(MANTIS_ERROR, 1, "Probing for 10353 (DVB-T)");
		fe = dvb_attach(zl10353_attach, &hopper_vp3028_config, adapter);

		if (!fe)
			return -1;
	} else {
		dprintk(MANTIS_ERROR, 1, "Frontend on <%s> POWER ON failed! <%d>",
			adapter->name,
			err);

		return -EIO;
	}
	dprintk(MANTIS_ERROR, 1, "Done!");

	return 0;
}

struct mantis_hwconfig vp3028_config = {
	.model_name	= MANTIS_MODEL_NAME,
	.dev_type	= MANTIS_DEV_TYPE,
	.ts_size	= MANTIS_TS_188,

	.baud_rate	= MANTIS_BAUD_9600,
	.parity		= MANTIS_PARITY_NONE,
	.bytes		= 0,

	.frontend_init	= vp3028_frontend_init,
	.power		= GPIF_A00,
	.reset		= GPIF_A03,
};

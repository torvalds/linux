// SPDX-License-Identifier: GPL-2.0-or-later
/*
	Mantis VP-1034 driver

	Copyright (C) Manu Abraham (abraham.manu@gmail.com)

*/

#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <asm/io.h>

#include <media/dmxdev.h>
#include <media/dvbdev.h>
#include <media/dvb_demux.h>
#include <media/dvb_frontend.h>
#include <media/dvb_net.h>

#include "mb86a16.h"
#include "mantis_common.h"
#include "mantis_ioc.h"
#include "mantis_dvb.h"
#include "mantis_vp1034.h"
#include "mantis_reg.h"

static const struct mb86a16_config vp1034_mb86a16_config = {
	.demod_address	= 0x08,
	.set_voltage	= vp1034_set_voltage,
};

#define MANTIS_MODEL_NAME	"VP-1034"
#define MANTIS_DEV_TYPE		"DVB-S/DSS"

int vp1034_set_voltage(struct dvb_frontend *fe, enum fe_sec_voltage voltage)
{
	struct mantis_pci *mantis = fe->dvb->priv;

	switch (voltage) {
	case SEC_VOLTAGE_13:
		dprintk(MANTIS_ERROR, 1, "Polarization=[13V]");
		mantis_gpio_set_bits(mantis, 13, 1);
		mantis_gpio_set_bits(mantis, 14, 0);
		break;
	case SEC_VOLTAGE_18:
		dprintk(MANTIS_ERROR, 1, "Polarization=[18V]");
		mantis_gpio_set_bits(mantis, 13, 1);
		mantis_gpio_set_bits(mantis, 14, 1);
		break;
	case SEC_VOLTAGE_OFF:
		dprintk(MANTIS_ERROR, 1, "Frontend (dummy) POWERDOWN");
		break;
	default:
		dprintk(MANTIS_ERROR, 1, "Invalid = (%d)", (u32) voltage);
		return -EINVAL;
	}
	mmwrite(0x00, MANTIS_GPIF_DOUT);

	return 0;
}

static int vp1034_frontend_init(struct mantis_pci *mantis, struct dvb_frontend *fe)
{
	struct i2c_adapter *adapter	= &mantis->adapter;

	int err = 0;

	err = mantis_frontend_power(mantis, POWER_ON);
	if (err == 0) {
		mantis_frontend_soft_reset(mantis);
		msleep(250);

		dprintk(MANTIS_ERROR, 1, "Probing for MB86A16 (DVB-S/DSS)");
		fe = dvb_attach(mb86a16_attach, &vp1034_mb86a16_config, adapter);
		if (fe) {
			dprintk(MANTIS_ERROR, 1,
			"found MB86A16 DVB-S/DSS frontend @0x%02x",
			vp1034_mb86a16_config.demod_address);

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

struct mantis_hwconfig vp1034_config = {
	.model_name	= MANTIS_MODEL_NAME,
	.dev_type	= MANTIS_DEV_TYPE,
	.ts_size	= MANTIS_TS_204,

	.baud_rate	= MANTIS_BAUD_9600,
	.parity		= MANTIS_PARITY_NONE,
	.bytes		= 0,

	.frontend_init	= vp1034_frontend_init,
	.power		= GPIF_A12,
	.reset		= GPIF_A13,
};

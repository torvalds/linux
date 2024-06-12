// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * budget.ko: driver for the SAA7146 based Budget DVB cards
 *            without analog video input or CI
 *
 * Compiled from various sources by Michael Hunold <michael@mihu.de>
 *
 * Copyright (C) 2002 Ralph Metzler <rjkm@metzlerbros.de>
 *
 * Copyright (C) 1999-2002 Ralph  Metzler
 *                       & Marcus Metzler for convergence integrated media GmbH
 *
 * 26feb2004 Support for FS Activy Card (Grundig tuner) by
 *           Michael Dreher <michael@5dot1.de>,
 *           Oliver Endriss <o.endriss@gmx.de> and
 *           Andreas 'randy' Weinberger
 *
 * the project's page is at https://linuxtv.org
 */

#include "budget.h"
#include "stv0299.h"
#include "ves1x93.h"
#include "ves1820.h"
#include "l64781.h"
#include "tda8083.h"
#include "s5h1420.h"
#include "tda10086.h"
#include "tda826x.h"
#include "lnbp21.h"
#include "bsru6.h"
#include "bsbe1.h"
#include "tdhd1.h"
#include "stv6110x.h"
#include "stv090x.h"
#include "isl6423.h"
#include "lnbh24.h"


static int diseqc_method;
module_param(diseqc_method, int, 0444);
MODULE_PARM_DESC(diseqc_method, "Select DiSEqC method for subsystem id 13c2:1003, 0: default, 1: more reliable (for newer revisions only)");

DVB_DEFINE_MOD_OPT_ADAPTER_NR(adapter_nr);

static void Set22K(struct budget *budget, int state)
{
	struct saa7146_dev *dev = budget->dev;

	dprintk(2, "budget: %p\n", budget);
	saa7146_setgpio(dev, 3, (state ? SAA7146_GPIO_OUTHI : SAA7146_GPIO_OUTLO));
}

/*
 * Diseqc functions only for TT Budget card
 * taken from the Skyvision DVB driver by
 * Ralph Metzler <rjkm@metzlerbros.de>
 */

static void DiseqcSendBit(struct budget *budget, int data)
{
	struct saa7146_dev *dev = budget->dev;

	dprintk(2, "budget: %p\n", budget);

	saa7146_setgpio(dev, 3, SAA7146_GPIO_OUTHI);
	udelay(data ? 500 : 1000);
	saa7146_setgpio(dev, 3, SAA7146_GPIO_OUTLO);
	udelay(data ? 1000 : 500);
}

static void DiseqcSendByte(struct budget *budget, int data)
{
	int i, par = 1, d;

	dprintk(2, "budget: %p\n", budget);

	for (i = 7; i >= 0; i--) {
		d = (data>>i)&1;
		par ^= d;
		DiseqcSendBit(budget, d);
	}

	DiseqcSendBit(budget, par);
}

static int SendDiSEqCMsg(struct budget *budget, int len, u8 *msg, unsigned long burst)
{
	struct saa7146_dev *dev = budget->dev;
	int i;

	dprintk(2, "budget: %p\n", budget);

	saa7146_setgpio(dev, 3, SAA7146_GPIO_OUTLO);
	mdelay(16);

	for (i = 0; i < len; i++)
		DiseqcSendByte(budget, msg[i]);

	mdelay(16);

	if (burst != -1) {
		if (burst) {
			DiseqcSendByte(budget, 0xff);
		} else {
			saa7146_setgpio(dev, 3, SAA7146_GPIO_OUTHI);
			mdelay(12);
			udelay(500);
			saa7146_setgpio(dev, 3, SAA7146_GPIO_OUTLO);
		}
		msleep(20);
	}

	return 0;
}

/*
 *   Routines for the Fujitsu Siemens Activy budget card
 *   22 kHz tone and DiSEqC are handled by the frontend.
 *   Voltage must be set here.
 *   GPIO 1: LNBP EN, GPIO 2: LNBP VSEL
 */
static int SetVoltage_Activy(struct budget *budget,
			     enum fe_sec_voltage voltage)
{
	struct saa7146_dev *dev = budget->dev;

	dprintk(2, "budget: %p\n", budget);

	switch (voltage) {
	case SEC_VOLTAGE_13:
		saa7146_setgpio(dev, 1, SAA7146_GPIO_OUTHI);
		saa7146_setgpio(dev, 2, SAA7146_GPIO_OUTLO);
		break;
	case SEC_VOLTAGE_18:
		saa7146_setgpio(dev, 1, SAA7146_GPIO_OUTHI);
		saa7146_setgpio(dev, 2, SAA7146_GPIO_OUTHI);
		break;
	case SEC_VOLTAGE_OFF:
		saa7146_setgpio(dev, 1, SAA7146_GPIO_OUTLO);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int siemens_budget_set_voltage(struct dvb_frontend *fe,
				      enum fe_sec_voltage voltage)
{
	struct budget *budget = fe->dvb->priv;

	return SetVoltage_Activy(budget, voltage);
}

static int budget_set_tone(struct dvb_frontend *fe,
			   enum fe_sec_tone_mode tone)
{
	struct budget *budget = fe->dvb->priv;

	switch (tone) {
	case SEC_TONE_ON:
		Set22K(budget, 1);
		break;

	case SEC_TONE_OFF:
		Set22K(budget, 0);
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

static int budget_diseqc_send_master_cmd(struct dvb_frontend *fe, struct dvb_diseqc_master_cmd *cmd)
{
	struct budget *budget = fe->dvb->priv;

	SendDiSEqCMsg(budget, cmd->msg_len, cmd->msg, 0);

	return 0;
}

static int budget_diseqc_send_burst(struct dvb_frontend *fe,
				    enum fe_sec_mini_cmd minicmd)
{
	struct budget *budget = fe->dvb->priv;

	SendDiSEqCMsg(budget, 0, NULL, minicmd);

	return 0;
}

static int alps_bsrv2_tuner_set_params(struct dvb_frontend *fe)
{
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	struct budget *budget = fe->dvb->priv;
	u8 pwr = 0;
	u8 buf[4];
	struct i2c_msg msg = { .addr = 0x61, .flags = 0, .buf = buf, .len = sizeof(buf) };
	u32 div = (c->frequency + 479500) / 125;

	if (c->frequency > 2000000)
		pwr = 3;
	else if (c->frequency > 1800000)
		pwr = 2;
	else if (c->frequency > 1600000)
		pwr = 1;
	else if (c->frequency > 1200000)
		pwr = 0;
	else if (c->frequency >= 1100000)
		pwr = 1;
	else
		pwr = 2;

	buf[0] = (div >> 8) & 0x7f;
	buf[1] = div & 0xff;
	buf[2] = ((div & 0x18000) >> 10) | 0x95;
	buf[3] = (pwr << 6) | 0x30;

	// NOTE: since we're using a prescaler of 2, we set the
	// divisor frequency to 62.5kHz and divide by 125 above

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);
	if (i2c_transfer(&budget->i2c_adap, &msg, 1) != 1)
		return -EIO;
	return 0;
}

static struct ves1x93_config alps_bsrv2_config = {
	.demod_address = 0x08,
	.xin = 90100000UL,
	.invert_pwm = 0,
};

static int alps_tdbe2_tuner_set_params(struct dvb_frontend *fe)
{
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	struct budget *budget = fe->dvb->priv;
	u32 div;
	u8 data[4];
	struct i2c_msg msg = { .addr = 0x62, .flags = 0, .buf = data, .len = sizeof(data) };

	div = (c->frequency + 35937500 + 31250) / 62500;

	data[0] = (div >> 8) & 0x7f;
	data[1] = div & 0xff;
	data[2] = 0x85 | ((div >> 10) & 0x60);
	data[3] = (c->frequency < 174000000 ? 0x88 : c->frequency < 470000000 ? 0x84 : 0x81);

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);
	if (i2c_transfer(&budget->i2c_adap, &msg, 1) != 1)
		return -EIO;
	return 0;
}

static struct ves1820_config alps_tdbe2_config = {
	.demod_address = 0x09,
	.xin = 57840000UL,
	.invert = 1,
	.selagc = VES1820_SELAGC_SIGNAMPERR,
};

static int grundig_29504_401_tuner_set_params(struct dvb_frontend *fe)
{
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	struct budget *budget = fe->dvb->priv;
	u8 *tuner_addr = fe->tuner_priv;
	u32 div;
	u8 cfg, cpump, band_select;
	u8 data[4];
	struct i2c_msg msg = { .flags = 0, .buf = data, .len = sizeof(data) };

	if (tuner_addr)
		msg.addr = *tuner_addr;
	else
		msg.addr = 0x61;

	div = (36125000 + c->frequency) / 166666;

	cfg = 0x88;

	if (c->frequency < 175000000)
		cpump = 2;
	else if (c->frequency < 390000000)
		cpump = 1;
	else if (c->frequency < 470000000)
		cpump = 2;
	else if (c->frequency < 750000000)
		cpump = 1;
	else
		cpump = 3;

	if (c->frequency < 175000000)
		band_select = 0x0e;
	else if (c->frequency < 470000000)
		band_select = 0x05;
	else
		band_select = 0x03;

	data[0] = (div >> 8) & 0x7f;
	data[1] = div & 0xff;
	data[2] = ((div >> 10) & 0x60) | cfg;
	data[3] = (cpump << 6) | band_select;

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);
	if (i2c_transfer(&budget->i2c_adap, &msg, 1) != 1)
		return -EIO;
	return 0;
}

static struct l64781_config grundig_29504_401_config = {
	.demod_address = 0x55,
};

static struct l64781_config grundig_29504_401_config_activy = {
	.demod_address = 0x54,
};

static u8 tuner_address_grundig_29504_401_activy = 0x60;

static int grundig_29504_451_tuner_set_params(struct dvb_frontend *fe)
{
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	struct budget *budget = fe->dvb->priv;
	u32 div;
	u8 data[4];
	struct i2c_msg msg = { .addr = 0x61, .flags = 0, .buf = data, .len = sizeof(data) };

	div = c->frequency / 125;
	data[0] = (div >> 8) & 0x7f;
	data[1] = div & 0xff;
	data[2] = 0x8e;
	data[3] = 0x00;

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);
	if (i2c_transfer(&budget->i2c_adap, &msg, 1) != 1)
		return -EIO;
	return 0;
}

static struct tda8083_config grundig_29504_451_config = {
	.demod_address = 0x68,
};

static int s5h1420_tuner_set_params(struct dvb_frontend *fe)
{
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	struct budget *budget = fe->dvb->priv;
	u32 div;
	u8 data[4];
	struct i2c_msg msg = { .addr = 0x61, .flags = 0, .buf = data, .len = sizeof(data) };

	div = c->frequency / 1000;
	data[0] = (div >> 8) & 0x7f;
	data[1] = div & 0xff;
	data[2] = 0xc2;

	if (div < 1450)
		data[3] = 0x00;
	else if (div < 1850)
		data[3] = 0x40;
	else if (div < 2000)
		data[3] = 0x80;
	else
		data[3] = 0xc0;

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);
	if (i2c_transfer(&budget->i2c_adap, &msg, 1) != 1)
		return -EIO;

	return 0;
}

static struct s5h1420_config s5h1420_config = {
	.demod_address = 0x53,
	.invert = 1,
	.cdclk_polarity = 1,
};

static struct tda10086_config tda10086_config = {
	.demod_address = 0x0e,
	.invert = 0,
	.diseqc_tone = 1,
	.xtal_freq = TDA10086_XTAL_16M,
};

static const struct stv0299_config alps_bsru6_config_activy = {
	.demod_address = 0x68,
	.inittab = alps_bsru6_inittab,
	.mclk = 88000000UL,
	.invert = 1,
	.op0_off = 1,
	.min_delay_ms = 100,
	.set_symbol_rate = alps_bsru6_set_symbol_rate,
};

static const struct stv0299_config alps_bsbe1_config_activy = {
	.demod_address = 0x68,
	.inittab = alps_bsbe1_inittab,
	.mclk = 88000000UL,
	.invert = 1,
	.op0_off = 1,
	.min_delay_ms = 100,
	.set_symbol_rate = alps_bsbe1_set_symbol_rate,
};

static int alps_tdhd1_204_request_firmware(struct dvb_frontend *fe, const struct firmware **fw, char *name)
{
	struct budget *budget = fe->dvb->priv;

	return request_firmware(fw, name, &budget->dev->pci->dev);
}


static int i2c_readreg(struct i2c_adapter *i2c, u8 adr, u8 reg)
{
	u8 val;
	struct i2c_msg msg[] = {
		{ .addr = adr, .flags = 0, .buf = &reg, .len = 1 },
		{ .addr = adr, .flags = I2C_M_RD, .buf = &val, .len = 1 }
	};

	return (i2c_transfer(i2c, msg, 2) != 2) ? -EIO : val;
}

static u8 read_pwm(struct budget *budget)
{
	u8 b = 0xff;
	u8 pwm;
	struct i2c_msg msg[] = { { .addr = 0x50, .flags = 0, .buf = &b, .len = 1 },
				 { .addr = 0x50, .flags = I2C_M_RD, .buf = &pwm, .len = 1} };

	if ((i2c_transfer(&budget->i2c_adap, msg, 2) != 2) || (pwm == 0xff))
		pwm = 0x48;

	return pwm;
}

static struct stv090x_config tt1600_stv090x_config = {
	.device			= STV0903,
	.demod_mode		= STV090x_SINGLE,
	.clk_mode		= STV090x_CLK_EXT,

	.xtal			= 13500000,
	.address		= 0x68,

	.ts1_mode		= STV090x_TSMODE_DVBCI,
	.ts2_mode		= STV090x_TSMODE_SERIAL_CONTINUOUS,

	.repeater_level		= STV090x_RPTLEVEL_16,

	.tuner_init		= NULL,
	.tuner_sleep		= NULL,
	.tuner_set_mode		= NULL,
	.tuner_set_frequency	= NULL,
	.tuner_get_frequency	= NULL,
	.tuner_set_bandwidth	= NULL,
	.tuner_get_bandwidth	= NULL,
	.tuner_set_bbgain	= NULL,
	.tuner_get_bbgain	= NULL,
	.tuner_set_refclk	= NULL,
	.tuner_get_status	= NULL,
};

static struct stv6110x_config tt1600_stv6110x_config = {
	.addr			= 0x60,
	.refclk			= 27000000,
	.clk_div		= 2,
};

static struct isl6423_config tt1600_isl6423_config = {
	.current_max		= SEC_CURRENT_515m,
	.curlim			= SEC_CURRENT_LIM_ON,
	.mod_extern		= 1,
	.addr			= 0x08,
};

static void frontend_init(struct budget *budget)
{
	(void)alps_bsbe1_config; /* avoid warning */

	switch (budget->dev->pci->subsystem_device) {
	case 0x1003: // Hauppauge/TT Nova budget (stv0299/ALPS BSRU6(tsa5059) OR ves1893/ALPS BSRV2(sp5659))
	case 0x1013:
		// try the ALPS BSRV2 first of all
		budget->dvb_frontend = dvb_attach(ves1x93_attach, &alps_bsrv2_config, &budget->i2c_adap);
		if (budget->dvb_frontend) {
			budget->dvb_frontend->ops.tuner_ops.set_params = alps_bsrv2_tuner_set_params;
			budget->dvb_frontend->ops.diseqc_send_master_cmd = budget_diseqc_send_master_cmd;
			budget->dvb_frontend->ops.diseqc_send_burst = budget_diseqc_send_burst;
			budget->dvb_frontend->ops.set_tone = budget_set_tone;
			break;
		}

		// try the ALPS BSRU6 now
		budget->dvb_frontend = dvb_attach(stv0299_attach, &alps_bsru6_config, &budget->i2c_adap);
		if (budget->dvb_frontend) {
			budget->dvb_frontend->ops.tuner_ops.set_params = alps_bsru6_tuner_set_params;
			budget->dvb_frontend->tuner_priv = &budget->i2c_adap;
			if (budget->dev->pci->subsystem_device == 0x1003 && diseqc_method == 0) {
				budget->dvb_frontend->ops.diseqc_send_master_cmd = budget_diseqc_send_master_cmd;
				budget->dvb_frontend->ops.diseqc_send_burst = budget_diseqc_send_burst;
				budget->dvb_frontend->ops.set_tone = budget_set_tone;
			}
			break;
		}
		break;

	case 0x1004: // Hauppauge/TT DVB-C budget (ves1820/ALPS TDBE2(sp5659))

		budget->dvb_frontend = dvb_attach(ves1820_attach, &alps_tdbe2_config, &budget->i2c_adap, read_pwm(budget));
		if (budget->dvb_frontend) {
			budget->dvb_frontend->ops.tuner_ops.set_params = alps_tdbe2_tuner_set_params;
			break;
		}
		break;

	case 0x1005: // Hauppauge/TT Nova-T budget (L64781/Grundig 29504-401(tsa5060))

		budget->dvb_frontend = dvb_attach(l64781_attach, &grundig_29504_401_config, &budget->i2c_adap);
		if (budget->dvb_frontend) {
			budget->dvb_frontend->ops.tuner_ops.set_params = grundig_29504_401_tuner_set_params;
			budget->dvb_frontend->tuner_priv = NULL;
			break;
		}
		break;

	case 0x4f52: /* Cards based on Philips Semi Sylt PCI ref. design */
		budget->dvb_frontend = dvb_attach(stv0299_attach, &alps_bsru6_config, &budget->i2c_adap);
		if (budget->dvb_frontend) {
			pr_info("tuner ALPS BSRU6 in Philips Semi. Sylt detected\n");
			budget->dvb_frontend->ops.tuner_ops.set_params = alps_bsru6_tuner_set_params;
			budget->dvb_frontend->tuner_priv = &budget->i2c_adap;
			break;
		}
		break;

	case 0x4f60: /* Fujitsu Siemens Activy Budget-S PCI rev AL (stv0299/tsa5059) */
	{
		int subtype = i2c_readreg(&budget->i2c_adap, 0x50, 0x67);

		if (subtype < 0)
			break;
		/* fixme: find a better way to identify the card */
		if (subtype < 0x36) {
			/* assume ALPS BSRU6 */
			budget->dvb_frontend = dvb_attach(stv0299_attach, &alps_bsru6_config_activy, &budget->i2c_adap);
			if (budget->dvb_frontend) {
				pr_info("tuner ALPS BSRU6 detected\n");
				budget->dvb_frontend->ops.tuner_ops.set_params = alps_bsru6_tuner_set_params;
				budget->dvb_frontend->tuner_priv = &budget->i2c_adap;
				budget->dvb_frontend->ops.set_voltage = siemens_budget_set_voltage;
				budget->dvb_frontend->ops.dishnetwork_send_legacy_command = NULL;
				break;
			}
		} else {
			/* assume ALPS BSBE1 */
			/* reset tuner */
			saa7146_setgpio(budget->dev, 3, SAA7146_GPIO_OUTLO);
			msleep(50);
			saa7146_setgpio(budget->dev, 3, SAA7146_GPIO_OUTHI);
			msleep(250);
			budget->dvb_frontend = dvb_attach(stv0299_attach, &alps_bsbe1_config_activy, &budget->i2c_adap);
			if (budget->dvb_frontend) {
				pr_info("tuner ALPS BSBE1 detected\n");
				budget->dvb_frontend->ops.tuner_ops.set_params = alps_bsbe1_tuner_set_params;
				budget->dvb_frontend->tuner_priv = &budget->i2c_adap;
				budget->dvb_frontend->ops.set_voltage = siemens_budget_set_voltage;
				budget->dvb_frontend->ops.dishnetwork_send_legacy_command = NULL;
				break;
			}
		}
		break;
	}

	case 0x4f61: // Fujitsu Siemens Activy Budget-S PCI rev GR (tda8083/Grundig 29504-451(tsa5522))
		budget->dvb_frontend = dvb_attach(tda8083_attach, &grundig_29504_451_config, &budget->i2c_adap);
		if (budget->dvb_frontend) {
			budget->dvb_frontend->ops.tuner_ops.set_params = grundig_29504_451_tuner_set_params;
			budget->dvb_frontend->ops.set_voltage = siemens_budget_set_voltage;
			budget->dvb_frontend->ops.dishnetwork_send_legacy_command = NULL;
		}
		break;

	case 0x5f60: /* Fujitsu Siemens Activy Budget-T PCI rev AL (tda10046/ALPS TDHD1-204A) */
		budget->dvb_frontend = dvb_attach(tda10046_attach, &alps_tdhd1_204a_config, &budget->i2c_adap);
		if (budget->dvb_frontend) {
			budget->dvb_frontend->ops.tuner_ops.set_params = alps_tdhd1_204a_tuner_set_params;
			budget->dvb_frontend->tuner_priv = &budget->i2c_adap;
		}
		break;

	case 0x5f61: /* Fujitsu Siemens Activy Budget-T PCI rev GR (L64781/Grundig 29504-401(tsa5060)) */
		budget->dvb_frontend = dvb_attach(l64781_attach, &grundig_29504_401_config_activy, &budget->i2c_adap);
		if (budget->dvb_frontend) {
			budget->dvb_frontend->tuner_priv = &tuner_address_grundig_29504_401_activy;
			budget->dvb_frontend->ops.tuner_ops.set_params = grundig_29504_401_tuner_set_params;
		}
		break;

	case 0x1016: // Hauppauge/TT Nova-S SE (samsung s5h1420/????(tda8260))
	{
		struct dvb_frontend *fe;

		fe = dvb_attach(s5h1420_attach, &s5h1420_config, &budget->i2c_adap);
		if (fe) {
			fe->ops.tuner_ops.set_params = s5h1420_tuner_set_params;
			budget->dvb_frontend = fe;
			if (dvb_attach(lnbp21_attach, fe, &budget->i2c_adap,
				       0, 0) == NULL) {
				pr_err("%s(): No LNBP21 found!\n", __func__);
				goto error_out;
			}
			break;
		}
	}
		fallthrough;
	case 0x1018: // TT Budget-S-1401 (philips tda10086/philips tda8262)
	{
		struct dvb_frontend *fe;

		// gpio2 is connected to CLB - reset it + leave it high
		saa7146_setgpio(budget->dev, 2, SAA7146_GPIO_OUTLO);
		msleep(1);
		saa7146_setgpio(budget->dev, 2, SAA7146_GPIO_OUTHI);
		msleep(1);

		fe = dvb_attach(tda10086_attach, &tda10086_config, &budget->i2c_adap);
		if (fe) {
			budget->dvb_frontend = fe;
			if (dvb_attach(tda826x_attach, fe, 0x60,
				       &budget->i2c_adap, 0) == NULL)
				pr_err("%s(): No tda826x found!\n", __func__);
			if (dvb_attach(lnbp21_attach, fe,
				       &budget->i2c_adap, 0, 0) == NULL) {
				pr_err("%s(): No LNBP21 found!\n", __func__);
				goto error_out;
			}
			break;
		}
	}
		fallthrough;

	case 0x101c: { /* TT S2-1600 */
			const struct stv6110x_devctl *ctl;

			saa7146_setgpio(budget->dev, 2, SAA7146_GPIO_OUTLO);
			msleep(50);
			saa7146_setgpio(budget->dev, 2, SAA7146_GPIO_OUTHI);
			msleep(250);

			budget->dvb_frontend = dvb_attach(stv090x_attach,
							  &tt1600_stv090x_config,
							  &budget->i2c_adap,
							  STV090x_DEMODULATOR_0);

			if (budget->dvb_frontend) {

				ctl = dvb_attach(stv6110x_attach,
						 budget->dvb_frontend,
						 &tt1600_stv6110x_config,
						 &budget->i2c_adap);

				if (ctl) {
					tt1600_stv090x_config.tuner_init	  = ctl->tuner_init;
					tt1600_stv090x_config.tuner_sleep	  = ctl->tuner_sleep;
					tt1600_stv090x_config.tuner_set_mode	  = ctl->tuner_set_mode;
					tt1600_stv090x_config.tuner_set_frequency = ctl->tuner_set_frequency;
					tt1600_stv090x_config.tuner_get_frequency = ctl->tuner_get_frequency;
					tt1600_stv090x_config.tuner_set_bandwidth = ctl->tuner_set_bandwidth;
					tt1600_stv090x_config.tuner_get_bandwidth = ctl->tuner_get_bandwidth;
					tt1600_stv090x_config.tuner_set_bbgain	  = ctl->tuner_set_bbgain;
					tt1600_stv090x_config.tuner_get_bbgain	  = ctl->tuner_get_bbgain;
					tt1600_stv090x_config.tuner_set_refclk	  = ctl->tuner_set_refclk;
					tt1600_stv090x_config.tuner_get_status	  = ctl->tuner_get_status;

					/*
					 * call the init function once to initialize
					 * tuner's clock output divider and demod's
					 * master clock
					 */
					if (budget->dvb_frontend->ops.init)
						budget->dvb_frontend->ops.init(budget->dvb_frontend);

					if (dvb_attach(isl6423_attach,
						       budget->dvb_frontend,
						       &budget->i2c_adap,
						       &tt1600_isl6423_config) == NULL) {
						pr_err("%s(): No Intersil ISL6423 found!\n", __func__);
						goto error_out;
					}
				} else {
					pr_err("%s(): No STV6110(A) Silicon Tuner found!\n", __func__);
					goto error_out;
				}
			}
		}
		break;

	case 0x1020: { /* Omicom S2 */
			const struct stv6110x_devctl *ctl;

			saa7146_setgpio(budget->dev, 2, SAA7146_GPIO_OUTLO);
			msleep(50);
			saa7146_setgpio(budget->dev, 2, SAA7146_GPIO_OUTHI);
			msleep(250);

			budget->dvb_frontend = dvb_attach(stv090x_attach,
							  &tt1600_stv090x_config,
							  &budget->i2c_adap,
							  STV090x_DEMODULATOR_0);

			if (budget->dvb_frontend) {
				pr_info("Omicom S2 detected\n");

				ctl = dvb_attach(stv6110x_attach,
						 budget->dvb_frontend,
						 &tt1600_stv6110x_config,
						 &budget->i2c_adap);

				if (ctl) {
					tt1600_stv090x_config.tuner_init	  = ctl->tuner_init;
					tt1600_stv090x_config.tuner_sleep	  = ctl->tuner_sleep;
					tt1600_stv090x_config.tuner_set_mode	  = ctl->tuner_set_mode;
					tt1600_stv090x_config.tuner_set_frequency = ctl->tuner_set_frequency;
					tt1600_stv090x_config.tuner_get_frequency = ctl->tuner_get_frequency;
					tt1600_stv090x_config.tuner_set_bandwidth = ctl->tuner_set_bandwidth;
					tt1600_stv090x_config.tuner_get_bandwidth = ctl->tuner_get_bandwidth;
					tt1600_stv090x_config.tuner_set_bbgain	  = ctl->tuner_set_bbgain;
					tt1600_stv090x_config.tuner_get_bbgain	  = ctl->tuner_get_bbgain;
					tt1600_stv090x_config.tuner_set_refclk	  = ctl->tuner_set_refclk;
					tt1600_stv090x_config.tuner_get_status	  = ctl->tuner_get_status;

					/*
					 * call the init function once to initialize
					 * tuner's clock output divider and demod's
					 * master clock
					 */
					if (budget->dvb_frontend->ops.init)
						budget->dvb_frontend->ops.init(budget->dvb_frontend);

					if (dvb_attach(lnbh24_attach,
							budget->dvb_frontend,
							&budget->i2c_adap,
							LNBH24_PCL | LNBH24_TTX,
							LNBH24_TEN, 0x14>>1) == NULL) {
						pr_err("No LNBH24 found!\n");
						goto error_out;
					}
				} else {
					pr_err("%s(): No STV6110(A) Silicon Tuner found!\n", __func__);
					goto error_out;
				}
			}
		}
		break;
	}

	if (budget->dvb_frontend == NULL) {
		pr_err("A frontend driver was not found for device [%04x:%04x] subsystem [%04x:%04x]\n",
		       budget->dev->pci->vendor,
		       budget->dev->pci->device,
		       budget->dev->pci->subsystem_vendor,
		       budget->dev->pci->subsystem_device);
	} else {
		if (dvb_register_frontend(&budget->dvb_adapter, budget->dvb_frontend))
			goto error_out;
	}
	return;

error_out:
	pr_err("Frontend registration failed!\n");
	dvb_frontend_detach(budget->dvb_frontend);
	budget->dvb_frontend = NULL;
}

static int budget_attach(struct saa7146_dev *dev, struct saa7146_pci_extension_data *info)
{
	struct budget *budget = NULL;
	int err;

	budget = kmalloc(sizeof(struct budget), GFP_KERNEL);
	if (budget == NULL)
		return -ENOMEM;

	dprintk(2, "dev:%p, info:%p, budget:%p\n", dev, info, budget);

	dev->ext_priv = budget;

	err = ttpci_budget_init(budget, dev, info, THIS_MODULE, adapter_nr);
	if (err) {
		pr_err("==> failed\n");
		kfree(budget);
		return err;
	}

	budget->dvb_adapter.priv = budget;
	frontend_init(budget);

	ttpci_budget_init_hooks(budget);

	return 0;
}

static int budget_detach(struct saa7146_dev *dev)
{
	struct budget *budget = dev->ext_priv;
	int err;

	if (budget->dvb_frontend) {
		dvb_unregister_frontend(budget->dvb_frontend);
		dvb_frontend_detach(budget->dvb_frontend);
	}

	err = ttpci_budget_deinit(budget);

	kfree(budget);
	dev->ext_priv = NULL;

	return err;
}

static struct saa7146_extension budget_extension;

MAKE_BUDGET_INFO(ttbs,	"TT-Budget/WinTV-NOVA-S  PCI",	BUDGET_TT);
MAKE_BUDGET_INFO(ttbc,	"TT-Budget/WinTV-NOVA-C  PCI",	BUDGET_TT);
MAKE_BUDGET_INFO(ttbt,	"TT-Budget/WinTV-NOVA-T  PCI",	BUDGET_TT);
MAKE_BUDGET_INFO(satel,	"SATELCO Multimedia PCI",	BUDGET_TT_HW_DISEQC);
MAKE_BUDGET_INFO(ttbs1401, "TT-Budget-S-1401 PCI", BUDGET_TT);
MAKE_BUDGET_INFO(tt1600, "TT-Budget S2-1600 PCI", BUDGET_TT);
MAKE_BUDGET_INFO(fsacs0, "Fujitsu Siemens Activy Budget-S PCI (rev GR/grundig frontend)", BUDGET_FS_ACTIVY);
MAKE_BUDGET_INFO(fsacs1, "Fujitsu Siemens Activy Budget-S PCI (rev AL/alps frontend)", BUDGET_FS_ACTIVY);
MAKE_BUDGET_INFO(fsact,	 "Fujitsu Siemens Activy Budget-T PCI (rev GR/Grundig frontend)", BUDGET_FS_ACTIVY);
MAKE_BUDGET_INFO(fsact1, "Fujitsu Siemens Activy Budget-T PCI (rev AL/ALPS TDHD1-204A)", BUDGET_FS_ACTIVY);
MAKE_BUDGET_INFO(omicom, "Omicom S2 PCI", BUDGET_TT);
MAKE_BUDGET_INFO(sylt,   "Philips Semi Sylt PCI", BUDGET_TT_HW_DISEQC);

static const struct pci_device_id pci_tbl[] = {
	MAKE_EXTENSION_PCI(ttbs,  0x13c2, 0x1003),
	MAKE_EXTENSION_PCI(ttbc,  0x13c2, 0x1004),
	MAKE_EXTENSION_PCI(ttbt,  0x13c2, 0x1005),
	MAKE_EXTENSION_PCI(satel, 0x13c2, 0x1013),
	MAKE_EXTENSION_PCI(ttbs,  0x13c2, 0x1016),
	MAKE_EXTENSION_PCI(ttbs1401, 0x13c2, 0x1018),
	MAKE_EXTENSION_PCI(tt1600, 0x13c2, 0x101c),
	MAKE_EXTENSION_PCI(fsacs1, 0x1131, 0x4f60),
	MAKE_EXTENSION_PCI(fsacs0, 0x1131, 0x4f61),
	MAKE_EXTENSION_PCI(fsact1, 0x1131, 0x5f60),
	MAKE_EXTENSION_PCI(fsact, 0x1131, 0x5f61),
	MAKE_EXTENSION_PCI(omicom, 0x14c4, 0x1020),
	MAKE_EXTENSION_PCI(sylt, 0x1131, 0x4f52),
	{
		.vendor    = 0,
	}
};

MODULE_DEVICE_TABLE(pci, pci_tbl);

static struct saa7146_extension budget_extension = {
	.name		= "budget dvb",
	.flags		= SAA7146_USE_I2C_IRQ,

	.module		= THIS_MODULE,
	.pci_tbl	= pci_tbl,
	.attach		= budget_attach,
	.detach		= budget_detach,

	.irq_mask	= MASK_10,
	.irq_func	= ttpci_budget_irq10_handler,
};

static int __init budget_init(void)
{
	return saa7146_register_extension(&budget_extension);
}

static void __exit budget_exit(void)
{
	saa7146_unregister_extension(&budget_extension);
}

module_init(budget_init);
module_exit(budget_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Ralph Metzler, Marcus Metzler, Michael Hunold, others");
MODULE_DESCRIPTION("driver for the SAA7146 based so-called budget PCI DVB cards by Siemens, Technotrend, Hauppauge");

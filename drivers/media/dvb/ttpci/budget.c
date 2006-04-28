/*
 * budget.c: driver for the SAA7146 based Budget DVB cards
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
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 * Or, point your browser to http://www.gnu.org/copyleft/gpl.html
 *
 *
 * the project's page is at http://www.linuxtv.org/dvb/
 */

#include "budget.h"
#include "stv0299.h"
#include "ves1x93.h"
#include "ves1820.h"
#include "l64781.h"
#include "tda8083.h"
#include "s5h1420.h"
#include "lnbp21.h"
#include "bsru6.h"

static void Set22K (struct budget *budget, int state)
{
	struct saa7146_dev *dev=budget->dev;
	dprintk(2, "budget: %p\n", budget);
	saa7146_setgpio(dev, 3, (state ? SAA7146_GPIO_OUTHI : SAA7146_GPIO_OUTLO));
}

/* Diseqc functions only for TT Budget card */
/* taken from the Skyvision DVB driver by
   Ralph Metzler <rjkm@metzlerbros.de> */

static void DiseqcSendBit (struct budget *budget, int data)
{
	struct saa7146_dev *dev=budget->dev;
	dprintk(2, "budget: %p\n", budget);

	saa7146_setgpio(dev, 3, SAA7146_GPIO_OUTHI);
	udelay(data ? 500 : 1000);
	saa7146_setgpio(dev, 3, SAA7146_GPIO_OUTLO);
	udelay(data ? 1000 : 500);
}

static void DiseqcSendByte (struct budget *budget, int data)
{
	int i, par=1, d;

	dprintk(2, "budget: %p\n", budget);

	for (i=7; i>=0; i--) {
		d = (data>>i)&1;
		par ^= d;
		DiseqcSendBit(budget, d);
	}

	DiseqcSendBit(budget, par);
}

static int SendDiSEqCMsg (struct budget *budget, int len, u8 *msg, unsigned long burst)
{
	struct saa7146_dev *dev=budget->dev;
	int i;

	dprintk(2, "budget: %p\n", budget);

	saa7146_setgpio(dev, 3, SAA7146_GPIO_OUTLO);
	mdelay(16);

	for (i=0; i<len; i++)
		DiseqcSendByte(budget, msg[i]);

	mdelay(16);

	if (burst!=-1) {
		if (burst)
			DiseqcSendByte(budget, 0xff);
		else {
			saa7146_setgpio(dev, 3, SAA7146_GPIO_OUTHI);
			udelay(12500);
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
static int SetVoltage_Activy (struct budget *budget, fe_sec_voltage_t voltage)
{
	struct saa7146_dev *dev=budget->dev;

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

static int siemens_budget_set_voltage(struct dvb_frontend* fe, fe_sec_voltage_t voltage)
{
	struct budget* budget = (struct budget*) fe->dvb->priv;

	return SetVoltage_Activy (budget, voltage);
}

static int budget_set_tone(struct dvb_frontend* fe, fe_sec_tone_mode_t tone)
{
	struct budget* budget = (struct budget*) fe->dvb->priv;

	switch (tone) {
	case SEC_TONE_ON:
		Set22K (budget, 1);
		break;

	case SEC_TONE_OFF:
		Set22K (budget, 0);
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

static int budget_diseqc_send_master_cmd(struct dvb_frontend* fe, struct dvb_diseqc_master_cmd* cmd)
{
	struct budget* budget = (struct budget*) fe->dvb->priv;

	SendDiSEqCMsg (budget, cmd->msg_len, cmd->msg, 0);

	return 0;
}

static int budget_diseqc_send_burst(struct dvb_frontend* fe, fe_sec_mini_cmd_t minicmd)
{
	struct budget* budget = (struct budget*) fe->dvb->priv;

	SendDiSEqCMsg (budget, 0, NULL, minicmd);

	return 0;
}

static int alps_bsrv2_tuner_set_params(struct dvb_frontend* fe, struct dvb_frontend_parameters* params)
{
	struct budget* budget = (struct budget*) fe->dvb->priv;
	u8 pwr = 0;
	u8 buf[4];
	struct i2c_msg msg = { .addr = 0x61, .flags = 0, .buf = buf, .len = sizeof(buf) };
	u32 div = (params->frequency + 479500) / 125;

	if (params->frequency > 2000000) pwr = 3;
	else if (params->frequency > 1800000) pwr = 2;
	else if (params->frequency > 1600000) pwr = 1;
	else if (params->frequency > 1200000) pwr = 0;
	else if (params->frequency >= 1100000) pwr = 1;
	else pwr = 2;

	buf[0] = (div >> 8) & 0x7f;
	buf[1] = div & 0xff;
	buf[2] = ((div & 0x18000) >> 10) | 0x95;
	buf[3] = (pwr << 6) | 0x30;

	// NOTE: since we're using a prescaler of 2, we set the
	// divisor frequency to 62.5kHz and divide by 125 above

	if (fe->ops->i2c_gate_ctrl)
		fe->ops->i2c_gate_ctrl(fe, 1);
	if (i2c_transfer (&budget->i2c_adap, &msg, 1) != 1) return -EIO;
	return 0;
}

static struct ves1x93_config alps_bsrv2_config =
{
	.demod_address = 0x08,
	.xin = 90100000UL,
	.invert_pwm = 0,
};

static int alps_tdbe2_tuner_set_params(struct dvb_frontend* fe, struct dvb_frontend_parameters* params)
{
	struct budget* budget = (struct budget*) fe->dvb->priv;
	u32 div;
	u8 data[4];
	struct i2c_msg msg = { .addr = 0x62, .flags = 0, .buf = data, .len = sizeof(data) };

	div = (params->frequency + 35937500 + 31250) / 62500;

	data[0] = (div >> 8) & 0x7f;
	data[1] = div & 0xff;
	data[2] = 0x85 | ((div >> 10) & 0x60);
	data[3] = (params->frequency < 174000000 ? 0x88 : params->frequency < 470000000 ? 0x84 : 0x81);

	if (fe->ops->i2c_gate_ctrl)
		fe->ops->i2c_gate_ctrl(fe, 1);
	if (i2c_transfer (&budget->i2c_adap, &msg, 1) != 1) return -EIO;
	return 0;
}

static struct ves1820_config alps_tdbe2_config = {
	.demod_address = 0x09,
	.xin = 57840000UL,
	.invert = 1,
	.selagc = VES1820_SELAGC_SIGNAMPERR,
};

static int grundig_29504_401_tuner_set_params(struct dvb_frontend* fe, struct dvb_frontend_parameters* params)
{
	struct budget* budget = (struct budget*) fe->dvb->priv;
	u32 div;
	u8 cfg, cpump, band_select;
	u8 data[4];
	struct i2c_msg msg = { .addr = 0x61, .flags = 0, .buf = data, .len = sizeof(data) };

	div = (36125000 + params->frequency) / 166666;

	cfg = 0x88;

	if (params->frequency < 175000000) cpump = 2;
	else if (params->frequency < 390000000) cpump = 1;
	else if (params->frequency < 470000000) cpump = 2;
	else if (params->frequency < 750000000) cpump = 1;
	else cpump = 3;

	if (params->frequency < 175000000) band_select = 0x0e;
	else if (params->frequency < 470000000) band_select = 0x05;
	else band_select = 0x03;

	data[0] = (div >> 8) & 0x7f;
	data[1] = div & 0xff;
	data[2] = ((div >> 10) & 0x60) | cfg;
	data[3] = (cpump << 6) | band_select;

	if (fe->ops->i2c_gate_ctrl)
		fe->ops->i2c_gate_ctrl(fe, 1);
	if (i2c_transfer (&budget->i2c_adap, &msg, 1) != 1) return -EIO;
	return 0;
}

static struct l64781_config grundig_29504_401_config = {
	.demod_address = 0x55,
};

static int grundig_29504_451_tuner_set_params(struct dvb_frontend* fe, struct dvb_frontend_parameters* params)
{
	struct budget* budget = (struct budget*) fe->dvb->priv;
	u32 div;
	u8 data[4];
	struct i2c_msg msg = { .addr = 0x61, .flags = 0, .buf = data, .len = sizeof(data) };

	div = params->frequency / 125;
	data[0] = (div >> 8) & 0x7f;
	data[1] = div & 0xff;
	data[2] = 0x8e;
	data[3] = 0x00;

	if (fe->ops->i2c_gate_ctrl)
		fe->ops->i2c_gate_ctrl(fe, 1);
	if (i2c_transfer (&budget->i2c_adap, &msg, 1) != 1) return -EIO;
	return 0;
}

static struct tda8083_config grundig_29504_451_config = {
	.demod_address = 0x68,
};

static int s5h1420_tuner_set_params(struct dvb_frontend* fe, struct dvb_frontend_parameters* params)
{
	struct budget* budget = (struct budget*) fe->dvb->priv;
	u32 div;
	u8 data[4];
	struct i2c_msg msg = { .addr = 0x61, .flags = 0, .buf = data, .len = sizeof(data) };

	div = params->frequency / 1000;
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

	if (fe->ops->i2c_gate_ctrl)
		fe->ops->i2c_gate_ctrl(fe, 1);
	if (i2c_transfer (&budget->i2c_adap, &msg, 1) != 1) return -EIO;

	return 0;
}

static struct s5h1420_config s5h1420_config = {
	.demod_address = 0x53,
	.invert = 1,
};

static u8 read_pwm(struct budget* budget)
{
	u8 b = 0xff;
	u8 pwm;
	struct i2c_msg msg[] = { { .addr = 0x50,.flags = 0,.buf = &b,.len = 1 },
				 { .addr = 0x50,.flags = I2C_M_RD,.buf = &pwm,.len = 1} };

	if ((i2c_transfer(&budget->i2c_adap, msg, 2) != 2) || (pwm == 0xff))
		pwm = 0x48;

	return pwm;
}

static void frontend_init(struct budget *budget)
{
	switch(budget->dev->pci->subsystem_device) {
	case 0x1003: // Hauppauge/TT Nova budget (stv0299/ALPS BSRU6(tsa5059) OR ves1893/ALPS BSRV2(sp5659))
	case 0x1013:
		// try the ALPS BSRV2 first of all
		budget->dvb_frontend = ves1x93_attach(&alps_bsrv2_config, &budget->i2c_adap);
		if (budget->dvb_frontend) {
			budget->dvb_frontend->ops->tuner_ops.set_params = alps_bsrv2_tuner_set_params;
			budget->dvb_frontend->ops->diseqc_send_master_cmd = budget_diseqc_send_master_cmd;
			budget->dvb_frontend->ops->diseqc_send_burst = budget_diseqc_send_burst;
			budget->dvb_frontend->ops->set_tone = budget_set_tone;
			break;
		}

		// try the ALPS BSRU6 now
		budget->dvb_frontend = stv0299_attach(&alps_bsru6_config, &budget->i2c_adap);
		if (budget->dvb_frontend) {
			budget->dvb_frontend->ops->tuner_ops.set_params = alps_bsru6_tuner_set_params;
			budget->dvb_frontend->tuner_priv = &budget->i2c_adap;
			budget->dvb_frontend->ops->diseqc_send_master_cmd = budget_diseqc_send_master_cmd;
			budget->dvb_frontend->ops->diseqc_send_burst = budget_diseqc_send_burst;
			budget->dvb_frontend->ops->set_tone = budget_set_tone;
			break;
		}
		break;

	case 0x1004: // Hauppauge/TT DVB-C budget (ves1820/ALPS TDBE2(sp5659))

		budget->dvb_frontend = ves1820_attach(&alps_tdbe2_config, &budget->i2c_adap, read_pwm(budget));
		if (budget->dvb_frontend) {
			budget->dvb_frontend->ops->tuner_ops.set_params = alps_tdbe2_tuner_set_params;
			break;
		}
		break;

	case 0x1005: // Hauppauge/TT Nova-T budget (L64781/Grundig 29504-401(tsa5060))

		budget->dvb_frontend = l64781_attach(&grundig_29504_401_config, &budget->i2c_adap);
		if (budget->dvb_frontend) {
			budget->dvb_frontend->ops->tuner_ops.set_params = grundig_29504_401_tuner_set_params;
			break;
		}
		break;

	case 0x4f60: // Fujitsu Siemens Activy Budget-S PCI rev AL (stv0299/ALPS BSRU6(tsa5059))
		budget->dvb_frontend = stv0299_attach(&alps_bsru6_config, &budget->i2c_adap);
		if (budget->dvb_frontend) {
			budget->dvb_frontend->ops->tuner_ops.set_params = alps_bsru6_tuner_set_params;
			budget->dvb_frontend->tuner_priv = &budget->i2c_adap;
			budget->dvb_frontend->ops->set_voltage = siemens_budget_set_voltage;
			budget->dvb_frontend->ops->dishnetwork_send_legacy_command = NULL;
		}
		break;

	case 0x4f61: // Fujitsu Siemens Activy Budget-S PCI rev GR (tda8083/Grundig 29504-451(tsa5522))
		budget->dvb_frontend = tda8083_attach(&grundig_29504_451_config, &budget->i2c_adap);
		if (budget->dvb_frontend) {
			budget->dvb_frontend->ops->tuner_ops.set_params = grundig_29504_451_tuner_set_params;
			budget->dvb_frontend->ops->set_voltage = siemens_budget_set_voltage;
			budget->dvb_frontend->ops->dishnetwork_send_legacy_command = NULL;
		}
		break;

	case 0x1016: // Hauppauge/TT Nova-S SE (samsung s5h1420/????(tda8260))
		budget->dvb_frontend = s5h1420_attach(&s5h1420_config, &budget->i2c_adap);
		if (budget->dvb_frontend) {
			budget->dvb_frontend->ops->tuner_ops.set_params = s5h1420_tuner_set_params;
			if (lnbp21_attach(budget->dvb_frontend, &budget->i2c_adap, 0, 0)) {
				printk("%s: No LNBP21 found!\n", __FUNCTION__);
				goto error_out;
			}
			break;
		}
	}

	if (budget->dvb_frontend == NULL) {
		printk("budget: A frontend driver was not found for device %04x/%04x subsystem %04x/%04x\n",
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
	printk("budget: Frontend registration failed!\n");
	if (budget->dvb_frontend->ops->release)
		budget->dvb_frontend->ops->release(budget->dvb_frontend);
	budget->dvb_frontend = NULL;
	return;
}

static int budget_attach (struct saa7146_dev* dev, struct saa7146_pci_extension_data *info)
{
	struct budget *budget = NULL;
	int err;

	budget = kmalloc(sizeof(struct budget), GFP_KERNEL);
	if( NULL == budget ) {
		return -ENOMEM;
	}

	dprintk(2, "dev:%p, info:%p, budget:%p\n", dev, info, budget);

	dev->ext_priv = budget;

	if ((err = ttpci_budget_init (budget, dev, info, THIS_MODULE))) {
		printk("==> failed\n");
		kfree (budget);
		return err;
	}

	budget->dvb_adapter.priv = budget;
	frontend_init(budget);

	return 0;
}

static int budget_detach (struct saa7146_dev* dev)
{
	struct budget *budget = (struct budget*) dev->ext_priv;
	int err;

	if (budget->dvb_frontend) dvb_unregister_frontend(budget->dvb_frontend);

	err = ttpci_budget_deinit (budget);

	kfree (budget);
	dev->ext_priv = NULL;

	return err;
}

static struct saa7146_extension budget_extension;

MAKE_BUDGET_INFO(ttbs,	"TT-Budget/WinTV-NOVA-S  PCI",	BUDGET_TT);
MAKE_BUDGET_INFO(ttbc,	"TT-Budget/WinTV-NOVA-C  PCI",	BUDGET_TT);
MAKE_BUDGET_INFO(ttbt,	"TT-Budget/WinTV-NOVA-T  PCI",	BUDGET_TT);
MAKE_BUDGET_INFO(satel,	"SATELCO Multimedia PCI",	BUDGET_TT_HW_DISEQC);
MAKE_BUDGET_INFO(fsacs0, "Fujitsu Siemens Activy Budget-S PCI (rev GR/grundig frontend)", BUDGET_FS_ACTIVY);
MAKE_BUDGET_INFO(fsacs1, "Fujitsu Siemens Activy Budget-S PCI (rev AL/alps frontend)", BUDGET_FS_ACTIVY);

static struct pci_device_id pci_tbl[] = {
	MAKE_EXTENSION_PCI(ttbs,  0x13c2, 0x1003),
	MAKE_EXTENSION_PCI(ttbc,  0x13c2, 0x1004),
	MAKE_EXTENSION_PCI(ttbt,  0x13c2, 0x1005),
	MAKE_EXTENSION_PCI(satel, 0x13c2, 0x1013),
	MAKE_EXTENSION_PCI(ttbs,  0x13c2, 0x1016),
	MAKE_EXTENSION_PCI(fsacs1,0x1131, 0x4f60),
	MAKE_EXTENSION_PCI(fsacs0,0x1131, 0x4f61),
	{
		.vendor    = 0,
	}
};

MODULE_DEVICE_TABLE(pci, pci_tbl);

static struct saa7146_extension budget_extension = {
	.name		= "budget dvb\0",
	.flags		= SAA7146_I2C_SHORT_DELAY,

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
MODULE_DESCRIPTION("driver for the SAA7146 based so-called "
		   "budget PCI DVB cards by Siemens, Technotrend, Hauppauge");

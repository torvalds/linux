// SPDX-License-Identifier: GPL-2.0
/*
 * ddbridge-max.c: Digital Devices bridge MAX card support
 *
 * Copyright (C) 2010-2017 Digital Devices GmbH
 *                         Ralph Metzler <rjkm@metzlerbros.de>
 *                         Marcus Metzler <mocm@metzlerbros.de>
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/poll.h>
#include <linux/io.h>
#include <linux/pci.h>
#include <linux/pci_ids.h>
#include <linux/timer.h>
#include <linux/i2c.h>
#include <linux/swab.h>
#include <linux/vmalloc.h>

#include "ddbridge.h"
#include "ddbridge-regs.h"
#include "ddbridge-io.h"
#include "ddbridge-mci.h"

#include "ddbridge-max.h"
#include "mxl5xx.h"

/******************************************************************************/

/* MaxS4/8 related modparams */
static int fmode;
module_param(fmode, int, 0444);
MODULE_PARM_DESC(fmode, "frontend emulation mode");

static int fmode_sat = -1;
module_param(fmode_sat, int, 0444);
MODULE_PARM_DESC(fmode_sat, "set frontend emulation mode sat");

static int old_quattro;
module_param(old_quattro, int, 0444);
MODULE_PARM_DESC(old_quattro, "old quattro LNB input order ");

/******************************************************************************/

static int lnb_command(struct ddb *dev, u32 link, u32 lnb, u32 cmd)
{
	u32 c, v = 0, tag = DDB_LINK_TAG(link);

	v = LNB_TONE & (dev->link[link].lnb.tone << (15 - lnb));
	ddbwritel(dev, cmd | v, tag | LNB_CONTROL(lnb));
	for (c = 0; c < 10; c++) {
		v = ddbreadl(dev, tag | LNB_CONTROL(lnb));
		if ((v & LNB_BUSY) == 0)
			break;
		msleep(20);
	}
	if (c == 10)
		dev_info(dev->dev, "%s lnb = %08x  cmd = %08x\n",
			 __func__, lnb, cmd);
	return 0;
}

static int max_send_master_cmd(struct dvb_frontend *fe,
			       struct dvb_diseqc_master_cmd *cmd)
{
	struct ddb_input *input = fe->sec_priv;
	struct ddb_port *port = input->port;
	struct ddb *dev = port->dev;
	struct ddb_dvb *dvb = &port->dvb[input->nr & 1];
	u32 tag = DDB_LINK_TAG(port->lnr);
	int i;
	u32 fmode = dev->link[port->lnr].lnb.fmode;

	if (fmode == 2 || fmode == 1)
		return 0;
	if (dvb->diseqc_send_master_cmd)
		dvb->diseqc_send_master_cmd(fe, cmd);

	mutex_lock(&dev->link[port->lnr].lnb.lock);
	ddbwritel(dev, 0, tag | LNB_BUF_LEVEL(dvb->input));
	for (i = 0; i < cmd->msg_len; i++)
		ddbwritel(dev, cmd->msg[i], tag | LNB_BUF_WRITE(dvb->input));
	lnb_command(dev, port->lnr, dvb->input, LNB_CMD_DISEQC);
	mutex_unlock(&dev->link[port->lnr].lnb.lock);
	return 0;
}

static int lnb_send_diseqc(struct ddb *dev, u32 link, u32 input,
			   struct dvb_diseqc_master_cmd *cmd)
{
	u32 tag = DDB_LINK_TAG(link);
	int i;

	ddbwritel(dev, 0, tag | LNB_BUF_LEVEL(input));
	for (i = 0; i < cmd->msg_len; i++)
		ddbwritel(dev, cmd->msg[i], tag | LNB_BUF_WRITE(input));
	lnb_command(dev, link, input, LNB_CMD_DISEQC);
	return 0;
}

static int lnb_set_sat(struct ddb *dev, u32 link, u32 input, u32 sat, u32 band,
		       u32 hor)
{
	struct dvb_diseqc_master_cmd cmd = {
		.msg = {0xe0, 0x10, 0x38, 0xf0, 0x00, 0x00},
		.msg_len = 4
	};
	cmd.msg[3] = 0xf0 | (((sat << 2) & 0x0c) | (band ? 1 : 0) |
		(hor ? 2 : 0));
	return lnb_send_diseqc(dev, link, input, &cmd);
}

static int lnb_set_tone(struct ddb *dev, u32 link, u32 input,
			enum fe_sec_tone_mode tone)
{
	int s = 0;
	u32 mask = (1ULL << input);

	switch (tone) {
	case SEC_TONE_OFF:
		if (!(dev->link[link].lnb.tone & mask))
			return 0;
		dev->link[link].lnb.tone &= ~(1ULL << input);
		break;
	case SEC_TONE_ON:
		if (dev->link[link].lnb.tone & mask)
			return 0;
		dev->link[link].lnb.tone |= (1ULL << input);
		break;
	default:
		s = -EINVAL;
		break;
	}
	if (!s)
		s = lnb_command(dev, link, input, LNB_CMD_NOP);
	return s;
}

static int lnb_set_voltage(struct ddb *dev, u32 link, u32 input,
			   enum fe_sec_voltage voltage)
{
	int s = 0;

	if (dev->link[link].lnb.oldvoltage[input] == voltage)
		return 0;
	switch (voltage) {
	case SEC_VOLTAGE_OFF:
		if (dev->link[link].lnb.voltage[input])
			return 0;
		lnb_command(dev, link, input, LNB_CMD_OFF);
		break;
	case SEC_VOLTAGE_13:
		lnb_command(dev, link, input, LNB_CMD_LOW);
		break;
	case SEC_VOLTAGE_18:
		lnb_command(dev, link, input, LNB_CMD_HIGH);
		break;
	default:
		s = -EINVAL;
		break;
	}
	dev->link[link].lnb.oldvoltage[input] = voltage;
	return s;
}

static int max_set_input_unlocked(struct dvb_frontend *fe, int in)
{
	struct ddb_input *input = fe->sec_priv;
	struct ddb_port *port = input->port;
	struct ddb *dev = port->dev;
	struct ddb_dvb *dvb = &port->dvb[input->nr & 1];
	int res = 0;

	if (in > 3)
		return -EINVAL;
	if (dvb->input != in) {
		u32 bit = (1ULL << input->nr);
		u32 obit =
			dev->link[port->lnr].lnb.voltage[dvb->input & 3] & bit;

		dev->link[port->lnr].lnb.voltage[dvb->input & 3] &= ~bit;
		dvb->input = in;
		dev->link[port->lnr].lnb.voltage[dvb->input & 3] |= obit;
	}
	res = dvb->set_input(fe, in);
	return res;
}

static int max_set_tone(struct dvb_frontend *fe, enum fe_sec_tone_mode tone)
{
	struct ddb_input *input = fe->sec_priv;
	struct ddb_port *port = input->port;
	struct ddb *dev = port->dev;
	struct ddb_dvb *dvb = &port->dvb[input->nr & 1];
	int tuner = 0;
	int res = 0;
	u32 fmode = dev->link[port->lnr].lnb.fmode;

	mutex_lock(&dev->link[port->lnr].lnb.lock);
	dvb->tone = tone;
	switch (fmode) {
	default:
	case 0:
	case 3:
		res = lnb_set_tone(dev, port->lnr, dvb->input, tone);
		break;
	case 1:
	case 2:
		if (old_quattro) {
			if (dvb->tone == SEC_TONE_ON)
				tuner |= 2;
			if (dvb->voltage == SEC_VOLTAGE_18)
				tuner |= 1;
		} else {
			if (dvb->tone == SEC_TONE_ON)
				tuner |= 1;
			if (dvb->voltage == SEC_VOLTAGE_18)
				tuner |= 2;
		}
		res = max_set_input_unlocked(fe, tuner);
		break;
	}
	mutex_unlock(&dev->link[port->lnr].lnb.lock);
	return res;
}

static int max_set_voltage(struct dvb_frontend *fe, enum fe_sec_voltage voltage)
{
	struct ddb_input *input = fe->sec_priv;
	struct ddb_port *port = input->port;
	struct ddb *dev = port->dev;
	struct ddb_dvb *dvb = &port->dvb[input->nr & 1];
	int tuner = 0;
	u32 nv, ov = dev->link[port->lnr].lnb.voltages;
	int res = 0;
	u32 fmode = dev->link[port->lnr].lnb.fmode;

	mutex_lock(&dev->link[port->lnr].lnb.lock);
	dvb->voltage = voltage;

	switch (fmode) {
	case 3:
	default:
	case 0:
		if (fmode == 3)
			max_set_input_unlocked(fe, 0);
		if (voltage == SEC_VOLTAGE_OFF)
			dev->link[port->lnr].lnb.voltage[dvb->input] &=
				~(1ULL << input->nr);
		else
			dev->link[port->lnr].lnb.voltage[dvb->input] |=
				(1ULL << input->nr);

		res = lnb_set_voltage(dev, port->lnr, dvb->input, voltage);
		break;
	case 1:
	case 2:
		if (voltage == SEC_VOLTAGE_OFF)
			dev->link[port->lnr].lnb.voltages &=
				~(1ULL << input->nr);
		else
			dev->link[port->lnr].lnb.voltages |=
				(1ULL << input->nr);

		nv = dev->link[port->lnr].lnb.voltages;

		if (old_quattro) {
			if (dvb->tone == SEC_TONE_ON)
				tuner |= 2;
			if (dvb->voltage == SEC_VOLTAGE_18)
				tuner |= 1;
		} else {
			if (dvb->tone == SEC_TONE_ON)
				tuner |= 1;
			if (dvb->voltage == SEC_VOLTAGE_18)
				tuner |= 2;
		}
		res = max_set_input_unlocked(fe, tuner);

		if (nv != ov) {
			if (nv) {
				lnb_set_voltage(
					dev, port->lnr,
					0, SEC_VOLTAGE_13);
				if (fmode == 1) {
					lnb_set_voltage(
						dev, port->lnr,
						0, SEC_VOLTAGE_13);
					if (old_quattro) {
						lnb_set_voltage(
							dev, port->lnr,
							1, SEC_VOLTAGE_18);
						lnb_set_voltage(
							dev, port->lnr,
							2, SEC_VOLTAGE_13);
					} else {
						lnb_set_voltage(
							dev, port->lnr,
							1, SEC_VOLTAGE_13);
						lnb_set_voltage(
							dev, port->lnr,
							2, SEC_VOLTAGE_18);
					}
					lnb_set_voltage(
						dev, port->lnr,
						3, SEC_VOLTAGE_18);
				}
			} else {
				lnb_set_voltage(
					dev, port->lnr,
					0, SEC_VOLTAGE_OFF);
				if (fmode == 1) {
					lnb_set_voltage(
						dev, port->lnr,
						1, SEC_VOLTAGE_OFF);
					lnb_set_voltage(
						dev, port->lnr,
						2, SEC_VOLTAGE_OFF);
					lnb_set_voltage(
						dev, port->lnr,
						3, SEC_VOLTAGE_OFF);
				}
			}
		}
		break;
	}
	mutex_unlock(&dev->link[port->lnr].lnb.lock);
	return res;
}

static int max_enable_high_lnb_voltage(struct dvb_frontend *fe, long arg)
{
	return 0;
}

static int max_send_burst(struct dvb_frontend *fe, enum fe_sec_mini_cmd burst)
{
	return 0;
}

static int mxl_fw_read(void *priv, u8 *buf, u32 len)
{
	struct ddb_link *link = priv;
	struct ddb *dev = link->dev;

	dev_info(dev->dev, "Read mxl_fw from link %u\n", link->nr);

	return ddbridge_flashread(dev, link->nr, buf, 0xc0000, len);
}

int ddb_lnb_init_fmode(struct ddb *dev, struct ddb_link *link, u32 fm)
{
	u32 l = link->nr;

	if (link->lnb.fmode == fm)
		return 0;
	dev_info(dev->dev, "Set fmode link %u = %u\n", l, fm);
	mutex_lock(&link->lnb.lock);
	if (fm == 2 || fm == 1) {
		if (fmode_sat >= 0) {
			lnb_set_sat(dev, l, 0, fmode_sat, 0, 0);
			if (old_quattro) {
				lnb_set_sat(dev, l, 1, fmode_sat, 0, 1);
				lnb_set_sat(dev, l, 2, fmode_sat, 1, 0);
			} else {
				lnb_set_sat(dev, l, 1, fmode_sat, 1, 0);
				lnb_set_sat(dev, l, 2, fmode_sat, 0, 1);
			}
			lnb_set_sat(dev, l, 3, fmode_sat, 1, 1);
		}
		lnb_set_tone(dev, l, 0, SEC_TONE_OFF);
		if (old_quattro) {
			lnb_set_tone(dev, l, 1, SEC_TONE_OFF);
			lnb_set_tone(dev, l, 2, SEC_TONE_ON);
		} else {
			lnb_set_tone(dev, l, 1, SEC_TONE_ON);
			lnb_set_tone(dev, l, 2, SEC_TONE_OFF);
		}
		lnb_set_tone(dev, l, 3, SEC_TONE_ON);
	}
	link->lnb.fmode = fm;
	mutex_unlock(&link->lnb.lock);
	return 0;
}

static struct mxl5xx_cfg mxl5xx = {
	.adr      = 0x60,
	.type     = 0x01,
	.clk      = 27000000,
	.ts_clk   = 139,
	.cap      = 12,
	.fw_read  = mxl_fw_read,
};

int ddb_fe_attach_mxl5xx(struct ddb_input *input)
{
	struct ddb *dev = input->port->dev;
	struct i2c_adapter *i2c = &input->port->i2c->adap;
	struct ddb_dvb *dvb = &input->port->dvb[input->nr & 1];
	struct ddb_port *port = input->port;
	struct ddb_link *link = &dev->link[port->lnr];
	struct mxl5xx_cfg cfg;
	int demod, tuner;

	cfg = mxl5xx;
	cfg.fw_priv = link;
	dvb->set_input = NULL;

	demod = input->nr;
	tuner = demod & 3;
	if (fmode == 3)
		tuner = 0;

	dvb->fe = dvb_attach(mxl5xx_attach, i2c, &cfg,
			     demod, tuner, &dvb->set_input);

	if (!dvb->fe) {
		dev_err(dev->dev, "No MXL5XX found!\n");
		return -ENODEV;
	}

	if (!dvb->set_input) {
		dev_err(dev->dev, "No mxl5xx_set_input function pointer!\n");
		return -ENODEV;
	}

	if (input->nr < 4) {
		lnb_command(dev, port->lnr, input->nr, LNB_CMD_INIT);
		lnb_set_voltage(dev, port->lnr, input->nr, SEC_VOLTAGE_OFF);
	}
	ddb_lnb_init_fmode(dev, link, fmode);

	dvb->fe->ops.set_voltage = max_set_voltage;
	dvb->fe->ops.enable_high_lnb_voltage = max_enable_high_lnb_voltage;
	dvb->fe->ops.set_tone = max_set_tone;
	dvb->diseqc_send_master_cmd = dvb->fe->ops.diseqc_send_master_cmd;
	dvb->fe->ops.diseqc_send_master_cmd = max_send_master_cmd;
	dvb->fe->ops.diseqc_send_burst = max_send_burst;
	dvb->fe->sec_priv = input;
	dvb->input = tuner;
	return 0;
}

/******************************************************************************/
/* MAX MCI related functions */

int ddb_fe_attach_mci(struct ddb_input *input, u32 type)
{
	struct ddb *dev = input->port->dev;
	struct ddb_dvb *dvb = &input->port->dvb[input->nr & 1];
	struct ddb_port *port = input->port;
	struct ddb_link *link = &dev->link[port->lnr];
	int demod, tuner;
	struct mci_cfg cfg;

	demod = input->nr;
	tuner = demod & 3;
	switch (type) {
	case DDB_TUNER_MCI_SX8:
		cfg = ddb_max_sx8_cfg;
		if (fmode == 3)
			tuner = 0;
		break;
	default:
		return -EINVAL;
	}
	dvb->fe = ddb_mci_attach(input, &cfg, demod, &dvb->set_input);
	if (!dvb->fe) {
		dev_err(dev->dev, "No MCI card found!\n");
		return -ENODEV;
	}
	if (!dvb->set_input) {
		dev_err(dev->dev, "No MCI set_input function pointer!\n");
		return -ENODEV;
	}
	if (input->nr < 4) {
		lnb_command(dev, port->lnr, input->nr, LNB_CMD_INIT);
		lnb_set_voltage(dev, port->lnr, input->nr, SEC_VOLTAGE_OFF);
	}
	ddb_lnb_init_fmode(dev, link, fmode);

	dvb->fe->ops.set_voltage = max_set_voltage;
	dvb->fe->ops.enable_high_lnb_voltage = max_enable_high_lnb_voltage;
	dvb->fe->ops.set_tone = max_set_tone;
	dvb->diseqc_send_master_cmd = dvb->fe->ops.diseqc_send_master_cmd;
	dvb->fe->ops.diseqc_send_master_cmd = max_send_master_cmd;
	dvb->fe->ops.diseqc_send_burst = max_send_burst;
	dvb->fe->sec_priv = input;
	dvb->input = tuner;
	return 0;
}

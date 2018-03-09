/*
 * ngene-cards.c: nGene PCIe bridge driver - card specific info
 *
 * Copyright (C) 2005-2007 Micronas
 *
 * Copyright (C) 2008-2009 Ralph Metzler <rjkm@metzlerbros.de>
 *                         Modifications for new nGene firmware,
 *                         support for EEPROM-copying,
 *                         support for new dual DVB-S2 card prototype
 *
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 only, as published by the Free Software Foundation.
 *
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * To obtain the license, point your browser to
 * http://www.gnu.org/copyleft/gpl.html
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/pci_ids.h>

#include "ngene.h"

/* demods/tuners */
#include "stv6110x.h"
#include "stv090x.h"
#include "lnbh24.h"
#include "lgdt330x.h"
#include "mt2131.h"
#include "tda18271c2dd.h"
#include "drxk.h"
#include "drxd.h"
#include "dvb-pll.h"
#include "stv0367.h"
#include "stv0367_priv.h"
#include "tda18212.h"
#include "cxd2841er.h"
#include "stv0910.h"
#include "stv6111.h"
#include "lnbh25.h"

/****************************************************************************/
/* I2C transfer functions used for demod/tuner probing***********************/
/****************************************************************************/

static int i2c_io(struct i2c_adapter *adapter, u8 adr,
		  u8 *wbuf, u32 wlen, u8 *rbuf, u32 rlen)
{
	struct i2c_msg msgs[2] = {{.addr = adr,  .flags = 0,
				   .buf  = wbuf, .len   = wlen },
				  {.addr = adr,  .flags = I2C_M_RD,
				   .buf  = rbuf,  .len   = rlen } };
	return (i2c_transfer(adapter, msgs, 2) == 2) ? 0 : -1;
}

static int i2c_write(struct i2c_adapter *adap, u8 adr, u8 *data, int len)
{
	struct i2c_msg msg = {.addr = adr, .flags = 0,
			      .buf = data, .len = len};

	return (i2c_transfer(adap, &msg, 1) == 1) ? 0 : -1;
}

static int i2c_write_reg(struct i2c_adapter *adap, u8 adr,
			 u8 reg, u8 val)
{
	u8 msg[2] = {reg, val};

	return i2c_write(adap, adr, msg, 2);
}

static int i2c_read(struct i2c_adapter *adapter, u8 adr, u8 *val)
{
	struct i2c_msg msgs[1] = {{.addr = adr,  .flags = I2C_M_RD,
				   .buf  = val,  .len   = 1 } };
	return (i2c_transfer(adapter, msgs, 1) == 1) ? 0 : -1;
}

static int i2c_read_reg16(struct i2c_adapter *adapter, u8 adr,
			  u16 reg, u8 *val)
{
	u8 msg[2] = {reg >> 8, reg & 0xff};
	struct i2c_msg msgs[2] = {{.addr = adr, .flags = 0,
				   .buf  = msg, .len   = 2},
				  {.addr = adr, .flags = I2C_M_RD,
				   .buf  = val, .len   = 1} };
	return (i2c_transfer(adapter, msgs, 2) == 2) ? 0 : -1;
}

static int i2c_read_regs(struct i2c_adapter *adapter,
			 u8 adr, u8 reg, u8 *val, u8 len)
{
	struct i2c_msg msgs[2] = {{.addr = adr,  .flags = 0,
				   .buf  = &reg, .len   = 1},
				  {.addr = adr,  .flags = I2C_M_RD,
				   .buf  = val,  .len   = len} };

	return (i2c_transfer(adapter, msgs, 2) == 2) ? 0 : -1;
}

static int i2c_read_reg(struct i2c_adapter *adapter, u8 adr, u8 reg, u8 *val)
{
	return i2c_read_regs(adapter, adr, reg, val, 1);
}

/****************************************************************************/
/* Demod/tuner attachment ***************************************************/
/****************************************************************************/

static struct i2c_adapter *i2c_adapter_from_chan(struct ngene_channel *chan)
{
	/* tuner 1+2: i2c adapter #0, tuner 3+4: i2c adapter #1 */
	if (chan->number < 2)
		return &chan->dev->channel[0].i2c_adapter;

	return &chan->dev->channel[1].i2c_adapter;
}

static int tuner_attach_stv6110(struct ngene_channel *chan)
{
	struct device *pdev = &chan->dev->pci_dev->dev;
	struct i2c_adapter *i2c = i2c_adapter_from_chan(chan);
	struct stv090x_config *feconf = (struct stv090x_config *)
		chan->dev->card_info->fe_config[chan->number];
	struct stv6110x_config *tunerconf = (struct stv6110x_config *)
		chan->dev->card_info->tuner_config[chan->number];
	const struct stv6110x_devctl *ctl;

	if (chan->number < 2)
		i2c = &chan->dev->channel[0].i2c_adapter;
	else
		i2c = &chan->dev->channel[1].i2c_adapter;

	ctl = dvb_attach(stv6110x_attach, chan->fe, tunerconf, i2c);
	if (ctl == NULL) {
		dev_err(pdev, "No STV6110X found!\n");
		return -ENODEV;
	}

	feconf->tuner_init          = ctl->tuner_init;
	feconf->tuner_sleep         = ctl->tuner_sleep;
	feconf->tuner_set_mode      = ctl->tuner_set_mode;
	feconf->tuner_set_frequency = ctl->tuner_set_frequency;
	feconf->tuner_get_frequency = ctl->tuner_get_frequency;
	feconf->tuner_set_bandwidth = ctl->tuner_set_bandwidth;
	feconf->tuner_get_bandwidth = ctl->tuner_get_bandwidth;
	feconf->tuner_set_bbgain    = ctl->tuner_set_bbgain;
	feconf->tuner_get_bbgain    = ctl->tuner_get_bbgain;
	feconf->tuner_set_refclk    = ctl->tuner_set_refclk;
	feconf->tuner_get_status    = ctl->tuner_get_status;

	return 0;
}

static int tuner_attach_stv6111(struct ngene_channel *chan)
{
	struct device *pdev = &chan->dev->pci_dev->dev;
	struct i2c_adapter *i2c = i2c_adapter_from_chan(chan);
	struct dvb_frontend *fe;
	u8 adr = 4 + ((chan->number & 1) ? 0x63 : 0x60);

	fe = dvb_attach(stv6111_attach, chan->fe, i2c, adr);
	if (!fe) {
		fe = dvb_attach(stv6111_attach, chan->fe, i2c, adr & ~4);
		if (!fe) {
			dev_err(pdev, "stv6111_attach() failed!\n");
			return -ENODEV;
		}
	}
	return 0;
}

static int drxk_gate_ctrl(struct dvb_frontend *fe, int enable)
{
	struct ngene_channel *chan = fe->sec_priv;
	int status;

	if (enable) {
		down(&chan->dev->pll_mutex);
		status = chan->gate_ctrl(fe, 1);
	} else {
		status = chan->gate_ctrl(fe, 0);
		up(&chan->dev->pll_mutex);
	}
	return status;
}

static int tuner_attach_tda18271(struct ngene_channel *chan)
{
	struct device *pdev = &chan->dev->pci_dev->dev;
	struct i2c_adapter *i2c = i2c_adapter_from_chan(chan);
	struct dvb_frontend *fe;

	if (chan->fe->ops.i2c_gate_ctrl)
		chan->fe->ops.i2c_gate_ctrl(chan->fe, 1);
	fe = dvb_attach(tda18271c2dd_attach, chan->fe, i2c, 0x60);
	if (chan->fe->ops.i2c_gate_ctrl)
		chan->fe->ops.i2c_gate_ctrl(chan->fe, 0);
	if (!fe) {
		dev_err(pdev, "No TDA18271 found!\n");
		return -ENODEV;
	}

	return 0;
}

static int tuner_tda18212_ping(struct ngene_channel *chan,
			       struct i2c_adapter *i2c,
			       unsigned short adr)
{
	struct device *pdev = &chan->dev->pci_dev->dev;
	u8 tda_id[2];
	u8 subaddr = 0x00;

	dev_dbg(pdev, "stv0367-tda18212 tuner ping\n");
	if (chan->fe->ops.i2c_gate_ctrl)
		chan->fe->ops.i2c_gate_ctrl(chan->fe, 1);

	if (i2c_read_regs(i2c, adr, subaddr, tda_id, sizeof(tda_id)) < 0)
		dev_dbg(pdev, "tda18212 ping 1 fail\n");
	if (i2c_read_regs(i2c, adr, subaddr, tda_id, sizeof(tda_id)) < 0)
		dev_warn(pdev, "tda18212 ping failed, expect problems\n");

	if (chan->fe->ops.i2c_gate_ctrl)
		chan->fe->ops.i2c_gate_ctrl(chan->fe, 0);

	return 0;
}

static int tuner_attach_tda18212(struct ngene_channel *chan, u32 dmdtype)
{
	struct device *pdev = &chan->dev->pci_dev->dev;
	struct i2c_adapter *i2c = i2c_adapter_from_chan(chan);
	struct i2c_client *client;
	struct tda18212_config config = {
		.fe = chan->fe,
		.if_dvbt_6 = 3550,
		.if_dvbt_7 = 3700,
		.if_dvbt_8 = 4150,
		.if_dvbt2_6 = 3250,
		.if_dvbt2_7 = 4000,
		.if_dvbt2_8 = 4000,
		.if_dvbc = 5000,
	};
	u8 addr = (chan->number & 1) ? 0x63 : 0x60;

	/*
	 * due to a hardware quirk with the I2C gate on the stv0367+tda18212
	 * combo, the tda18212 must be probed by reading it's id _twice_ when
	 * cold started, or it very likely will fail.
	 */
	if (dmdtype == DEMOD_TYPE_STV0367)
		tuner_tda18212_ping(chan, i2c, addr);

	/* perform tuner probe/init/attach */
	client = dvb_module_probe("tda18212", NULL, i2c, addr, &config);
	if (!client)
		goto err;

	chan->i2c_client[0] = client;
	chan->i2c_client_fe = 1;

	return 0;
err:
	dev_err(pdev, "TDA18212 tuner not found. Device is not fully operational.\n");
	return -ENODEV;
}

static int tuner_attach_probe(struct ngene_channel *chan)
{
	switch (chan->demod_type) {
	case DEMOD_TYPE_STV090X:
		return tuner_attach_stv6110(chan);
	case DEMOD_TYPE_DRXK:
		return tuner_attach_tda18271(chan);
	case DEMOD_TYPE_STV0367:
	case DEMOD_TYPE_SONY_CT2:
	case DEMOD_TYPE_SONY_ISDBT:
	case DEMOD_TYPE_SONY_C2T2:
	case DEMOD_TYPE_SONY_C2T2I:
		return tuner_attach_tda18212(chan, chan->demod_type);
	case DEMOD_TYPE_STV0910:
		return tuner_attach_stv6111(chan);
	}

	return -EINVAL;
}

static int demod_attach_stv0900(struct ngene_channel *chan)
{
	struct device *pdev = &chan->dev->pci_dev->dev;
	struct i2c_adapter *i2c = i2c_adapter_from_chan(chan);
	struct stv090x_config *feconf = (struct stv090x_config *)
		chan->dev->card_info->fe_config[chan->number];

	/* tuner 1+2: i2c adapter #0, tuner 3+4: i2c adapter #1 */
	/* Note: Both adapters share the same i2c bus, but the demod     */
	/*       driver requires that each demod has its own i2c adapter */
	if (chan->number < 2)
		i2c = &chan->dev->channel[0].i2c_adapter;
	else
		i2c = &chan->dev->channel[1].i2c_adapter;

	chan->fe = dvb_attach(stv090x_attach, feconf, i2c,
			(chan->number & 1) == 0 ? STV090x_DEMODULATOR_0
						: STV090x_DEMODULATOR_1);
	if (chan->fe == NULL) {
		dev_err(pdev, "No STV0900 found!\n");
		return -ENODEV;
	}

	/* store channel info */
	if (feconf->tuner_i2c_lock)
		chan->fe->analog_demod_priv = chan;

	if (!dvb_attach(lnbh24_attach, chan->fe, i2c, 0,
			0, chan->dev->card_info->lnb[chan->number])) {
		dev_err(pdev, "No LNBH24 found!\n");
		dvb_frontend_detach(chan->fe);
		chan->fe = NULL;
		return -ENODEV;
	}

	return 0;
}

static struct stv0910_cfg stv0910_p = {
	.adr      = 0x68,
	.parallel = 1,
	.rptlvl   = 4,
	.clk      = 30000000,
};

static struct lnbh25_config lnbh25_cfg = {
	.i2c_address = 0x0c << 1,
	.data2_config = LNBH25_TEN
};

static int demod_attach_stv0910(struct ngene_channel *chan,
				struct i2c_adapter *i2c)
{
	struct device *pdev = &chan->dev->pci_dev->dev;
	struct stv0910_cfg cfg = stv0910_p;
	struct lnbh25_config lnbcfg = lnbh25_cfg;

	chan->fe = dvb_attach(stv0910_attach, i2c, &cfg, (chan->number & 1));
	if (!chan->fe) {
		cfg.adr = 0x6c;
		chan->fe = dvb_attach(stv0910_attach, i2c,
				      &cfg, (chan->number & 1));
	}
	if (!chan->fe) {
		dev_err(pdev, "stv0910_attach() failed!\n");
		return -ENODEV;
	}

	/*
	 * attach lnbh25 - leftshift by one as the lnbh25 driver expects 8bit
	 * i2c addresses
	 */
	lnbcfg.i2c_address = (((chan->number & 1) ? 0x0d : 0x0c) << 1);
	if (!dvb_attach(lnbh25_attach, chan->fe, &lnbcfg, i2c)) {
		lnbcfg.i2c_address = (((chan->number & 1) ? 0x09 : 0x08) << 1);
		if (!dvb_attach(lnbh25_attach, chan->fe, &lnbcfg, i2c)) {
			dev_err(pdev, "lnbh25_attach() failed!\n");
			dvb_frontend_detach(chan->fe);
			chan->fe = NULL;
			return -ENODEV;
		}
	}

	return 0;
}

static struct stv0367_config ddb_stv0367_config[] = {
	{
		.demod_address = 0x1f,
		.xtal = 27000000,
		.if_khz = 0,
		.if_iq_mode = FE_TER_NORMAL_IF_TUNER,
		.ts_mode = STV0367_SERIAL_PUNCT_CLOCK,
		.clk_pol = STV0367_CLOCKPOLARITY_DEFAULT,
	}, {
		.demod_address = 0x1e,
		.xtal = 27000000,
		.if_khz = 0,
		.if_iq_mode = FE_TER_NORMAL_IF_TUNER,
		.ts_mode = STV0367_SERIAL_PUNCT_CLOCK,
		.clk_pol = STV0367_CLOCKPOLARITY_DEFAULT,
	},
};

static int demod_attach_stv0367(struct ngene_channel *chan,
				struct i2c_adapter *i2c)
{
	struct device *pdev = &chan->dev->pci_dev->dev;

	chan->fe = dvb_attach(stv0367ddb_attach,
			      &ddb_stv0367_config[(chan->number & 1)], i2c);

	if (!chan->fe) {
		dev_err(pdev, "stv0367ddb_attach() failed!\n");
		return -ENODEV;
	}

	chan->fe->sec_priv = chan;
	chan->gate_ctrl = chan->fe->ops.i2c_gate_ctrl;
	chan->fe->ops.i2c_gate_ctrl = drxk_gate_ctrl;
	return 0;
}

static int demod_attach_cxd28xx(struct ngene_channel *chan,
				struct i2c_adapter *i2c, int osc24)
{
	struct device *pdev = &chan->dev->pci_dev->dev;
	struct cxd2841er_config cfg;

	/* the cxd2841er driver expects 8bit/shifted I2C addresses */
	cfg.i2c_addr = ((chan->number & 1) ? 0x6d : 0x6c) << 1;

	cfg.xtal = osc24 ? SONY_XTAL_24000 : SONY_XTAL_20500;
	cfg.flags = CXD2841ER_AUTO_IFHZ | CXD2841ER_EARLY_TUNE |
		CXD2841ER_NO_WAIT_LOCK | CXD2841ER_NO_AGCNEG |
		CXD2841ER_TSBITS | CXD2841ER_TS_SERIAL;

	/* attach frontend */
	chan->fe = dvb_attach(cxd2841er_attach_t_c, &cfg, i2c);

	if (!chan->fe) {
		dev_err(pdev, "CXD28XX attach failed!\n");
		return -ENODEV;
	}

	chan->fe->sec_priv = chan;
	chan->gate_ctrl = chan->fe->ops.i2c_gate_ctrl;
	chan->fe->ops.i2c_gate_ctrl = drxk_gate_ctrl;
	return 0;
}

static void cineS2_tuner_i2c_lock(struct dvb_frontend *fe, int lock)
{
	struct ngene_channel *chan = fe->analog_demod_priv;

	if (lock)
		down(&chan->dev->pll_mutex);
	else
		up(&chan->dev->pll_mutex);
}

static int port_has_stv0900(struct i2c_adapter *i2c, int port)
{
	u8 val;
	if (i2c_read_reg16(i2c, 0x68+port/2, 0xf100, &val) < 0)
		return 0;
	return 1;
}

static int port_has_drxk(struct i2c_adapter *i2c, int port)
{
	u8 val;

	if (i2c_read(i2c, 0x29+port, &val) < 0)
		return 0;
	return 1;
}

static int port_has_stv0367(struct i2c_adapter *i2c)
{
	u8 val;

	if (i2c_read_reg16(i2c, 0x1e, 0xf000, &val) < 0)
		return 0;
	if (val != 0x60)
		return 0;
	if (i2c_read_reg16(i2c, 0x1f, 0xf000, &val) < 0)
		return 0;
	if (val != 0x60)
		return 0;
	return 1;
}

int ngene_port_has_cxd2099(struct i2c_adapter *i2c, u8 *type)
{
	u8 val;
	u8 probe[4] = { 0xe0, 0x00, 0x00, 0x00 }, data[4];
	struct i2c_msg msgs[2] = {{ .addr = 0x40,  .flags = 0,
				    .buf  = probe, .len   = 4 },
				  { .addr = 0x40,  .flags = I2C_M_RD,
				    .buf  = data,  .len   = 4 } };
	val = i2c_transfer(i2c, msgs, 2);
	if (val != 2)
		return 0;

	if (data[0] == 0x02 && data[1] == 0x2b && data[3] == 0x43)
		*type = 2;
	else
		*type = 1;
	return 1;
}

static int demod_attach_drxk(struct ngene_channel *chan,
			     struct i2c_adapter *i2c)
{
	struct device *pdev = &chan->dev->pci_dev->dev;
	struct drxk_config config;

	memset(&config, 0, sizeof(config));
	config.microcode_name = "drxk_a3.mc";
	config.qam_demod_parameter_count = 4;
	config.adr = 0x29 + (chan->number ^ 2);

	chan->fe = dvb_attach(drxk_attach, &config, i2c);
	if (!chan->fe) {
		dev_err(pdev, "No DRXK found!\n");
		return -ENODEV;
	}
	chan->fe->sec_priv = chan;
	chan->gate_ctrl = chan->fe->ops.i2c_gate_ctrl;
	chan->fe->ops.i2c_gate_ctrl = drxk_gate_ctrl;
	return 0;
}

/****************************************************************************/
/* XO2 related lists and functions ******************************************/
/****************************************************************************/

static char *xo2names[] = {
	"DUAL DVB-S2",
	"DUAL DVB-C/T/T2",
	"DUAL DVB-ISDBT",
	"DUAL DVB-C/C2/T/T2",
	"DUAL ATSC",
	"DUAL DVB-C/C2/T/T2/I",
};

static int init_xo2(struct ngene_channel *chan, struct i2c_adapter *i2c)
{
	struct device *pdev = &chan->dev->pci_dev->dev;
	u8 addr = 0x10;
	u8 val, data[2];
	int res;

	res = i2c_read_regs(i2c, addr, 0x04, data, 2);
	if (res < 0)
		return res;

	if (data[0] != 0x01)  {
		dev_info(pdev, "Invalid XO2 on channel %d\n", chan->number);
		return -1;
	}

	i2c_read_reg(i2c, addr, 0x08, &val);
	if (val != 0) {
		i2c_write_reg(i2c, addr, 0x08, 0x00);
		msleep(100);
	}
	/* Enable tuner power, disable pll, reset demods */
	i2c_write_reg(i2c, addr, 0x08, 0x04);
	usleep_range(2000, 3000);
	/* Release demod resets */
	i2c_write_reg(i2c, addr, 0x08, 0x07);

	/*
	 * speed: 0=55,1=75,2=90,3=104 MBit/s
	 * Note: The ngene hardware must be run at 75 MBit/s compared
	 * to more modern ddbridge hardware which runs at 90 MBit/s,
	 * else there will be issues with the data transport and non-
	 * working secondary/slave demods/tuners.
	 */
	i2c_write_reg(i2c, addr, 0x09, 1);

	i2c_write_reg(i2c, addr, 0x0a, 0x01);
	i2c_write_reg(i2c, addr, 0x0b, 0x01);

	usleep_range(2000, 3000);
	/* Start XO2 PLL */
	i2c_write_reg(i2c, addr, 0x08, 0x87);

	return 0;
}

static int port_has_xo2(struct i2c_adapter *i2c, u8 *type, u8 *id)
{
	u8 probe[1] = { 0x00 }, data[4];
	u8 addr = 0x10;

	*type = NGENE_XO2_TYPE_NONE;

	if (i2c_io(i2c, addr, probe, 1, data, 4))
		return 0;
	if (data[0] == 'D' && data[1] == 'F') {
		*id = data[2];
		*type = NGENE_XO2_TYPE_DUOFLEX;
		return 1;
	}
	if (data[0] == 'C' && data[1] == 'I') {
		*id = data[2];
		*type = NGENE_XO2_TYPE_CI;
		return 1;
	}
	return 0;
}

/****************************************************************************/
/* Probing and port/channel handling ****************************************/
/****************************************************************************/

static int cineS2_probe(struct ngene_channel *chan)
{
	struct device *pdev = &chan->dev->pci_dev->dev;
	struct i2c_adapter *i2c = i2c_adapter_from_chan(chan);
	struct stv090x_config *fe_conf;
	u8 buf[3];
	u8 xo2_type, xo2_id, xo2_demodtype;
	u8 sony_osc24 = 0;
	struct i2c_msg i2c_msg = { .flags = 0, .buf = buf };
	int rc;

	if (port_has_xo2(i2c, &xo2_type, &xo2_id)) {
		xo2_id >>= 2;
		dev_dbg(pdev, "XO2 on channel %d (type %d, id %d)\n",
			chan->number, xo2_type, xo2_id);

		switch (xo2_type) {
		case NGENE_XO2_TYPE_DUOFLEX:
			if (chan->number & 1)
				dev_dbg(pdev,
					"skipping XO2 init on odd channel %d",
					chan->number);
			else
				init_xo2(chan, i2c);

			xo2_demodtype = DEMOD_TYPE_XO2 + xo2_id;

			switch (xo2_demodtype) {
			case DEMOD_TYPE_SONY_CT2:
			case DEMOD_TYPE_SONY_ISDBT:
			case DEMOD_TYPE_SONY_C2T2:
			case DEMOD_TYPE_SONY_C2T2I:
				dev_info(pdev, "%s (XO2) on channel %d\n",
					 xo2names[xo2_id], chan->number);
				chan->demod_type = xo2_demodtype;
				if (xo2_demodtype == DEMOD_TYPE_SONY_C2T2I)
					sony_osc24 = 1;

				demod_attach_cxd28xx(chan, i2c, sony_osc24);
				break;
			case DEMOD_TYPE_STV0910:
				dev_info(pdev, "%s (XO2) on channel %d\n",
					 xo2names[xo2_id], chan->number);
				chan->demod_type = xo2_demodtype;
				demod_attach_stv0910(chan, i2c);
				break;
			default:
				dev_warn(pdev,
					 "Unsupported XO2 module on channel %d\n",
					 chan->number);
				return -ENODEV;
			}
			break;
		case NGENE_XO2_TYPE_CI:
			dev_info(pdev, "DuoFlex CI modules not supported\n");
			return -ENODEV;
		default:
			dev_info(pdev, "Unsupported XO2 module type\n");
			return -ENODEV;
		}
	} else if (port_has_stv0900(i2c, chan->number)) {
		chan->demod_type = DEMOD_TYPE_STV090X;
		fe_conf = chan->dev->card_info->fe_config[chan->number];
		/* demod found, attach it */
		rc = demod_attach_stv0900(chan);
		if (rc < 0 || chan->number < 2)
			return rc;

		/* demod #2: reprogram outputs DPN1 & DPN2 */
		i2c_msg.addr = fe_conf->address;
		i2c_msg.len = 3;
		buf[0] = 0xf1;
		switch (chan->number) {
		case 2:
			buf[1] = 0x5c;
			buf[2] = 0xc2;
			break;
		case 3:
			buf[1] = 0x61;
			buf[2] = 0xcc;
			break;
		default:
			return -ENODEV;
		}
		rc = i2c_transfer(i2c, &i2c_msg, 1);
		if (rc != 1) {
			dev_err(pdev, "Could not setup DPNx\n");
			return -EIO;
		}
	} else if (port_has_drxk(i2c, chan->number^2)) {
		chan->demod_type = DEMOD_TYPE_DRXK;
		demod_attach_drxk(chan, i2c);
	} else if (port_has_stv0367(i2c)) {
		chan->demod_type = DEMOD_TYPE_STV0367;
		dev_info(pdev, "STV0367 on channel %d\n", chan->number);
		demod_attach_stv0367(chan, i2c);
	} else {
		dev_info(pdev, "No demod found on chan %d\n", chan->number);
		return -ENODEV;
	}
	return 0;
}


static struct lgdt330x_config aver_m780 = {
	.demod_chip    = LGDT3303,
	.serial_mpeg   = 0x00, /* PARALLEL */
	.clock_polarity_flip = 1,
};

static struct mt2131_config m780_tunerconfig = {
	0xc0 >> 1
};

/* A single func to attach the demo and tuner, rather than
 * use two sep funcs like the current design mandates.
 */
static int demod_attach_lg330x(struct ngene_channel *chan)
{
	struct device *pdev = &chan->dev->pci_dev->dev;

	chan->fe = dvb_attach(lgdt330x_attach, &aver_m780,
			      0xb2 >> 1, &chan->i2c_adapter);
	if (chan->fe == NULL) {
		dev_err(pdev, "No LGDT330x found!\n");
		return -ENODEV;
	}

	dvb_attach(mt2131_attach, chan->fe, &chan->i2c_adapter,
		   &m780_tunerconfig, 0);

	return (chan->fe) ? 0 : -ENODEV;
}

static int demod_attach_drxd(struct ngene_channel *chan)
{
	struct device *pdev = &chan->dev->pci_dev->dev;
	struct drxd_config *feconf;

	feconf = chan->dev->card_info->fe_config[chan->number];

	chan->fe = dvb_attach(drxd_attach, feconf, chan,
			&chan->i2c_adapter, &chan->dev->pci_dev->dev);
	if (!chan->fe) {
		dev_err(pdev, "No DRXD found!\n");
		return -ENODEV;
	}
	return 0;
}

static int tuner_attach_dtt7520x(struct ngene_channel *chan)
{
	struct device *pdev = &chan->dev->pci_dev->dev;
	struct drxd_config *feconf;

	feconf = chan->dev->card_info->fe_config[chan->number];

	if (!dvb_attach(dvb_pll_attach, chan->fe, feconf->pll_address,
			&chan->i2c_adapter,
			feconf->pll_type)) {
		dev_err(pdev, "No pll(%d) found!\n", feconf->pll_type);
		return -ENODEV;
	}
	return 0;
}

/****************************************************************************/
/* EEPROM TAGS **************************************************************/
/****************************************************************************/

#define MICNG_EE_START      0x0100
#define MICNG_EE_END        0x0FF0

#define MICNG_EETAG_END0    0x0000
#define MICNG_EETAG_END1    0xFFFF

/* 0x0001 - 0x000F reserved for housekeeping */
/* 0xFFFF - 0xFFFE reserved for housekeeping */

/* Micronas assigned tags
   EEProm tags for hardware support */

#define MICNG_EETAG_DRXD1_OSCDEVIATION  0x1000  /* 2 Bytes data */
#define MICNG_EETAG_DRXD2_OSCDEVIATION  0x1001  /* 2 Bytes data */

#define MICNG_EETAG_MT2060_1_1STIF      0x1100  /* 2 Bytes data */
#define MICNG_EETAG_MT2060_2_1STIF      0x1101  /* 2 Bytes data */

/* Tag range for OEMs */

#define MICNG_EETAG_OEM_FIRST  0xC000
#define MICNG_EETAG_OEM_LAST   0xFFEF

static int i2c_write_eeprom(struct i2c_adapter *adapter,
			    u8 adr, u16 reg, u8 data)
{
	struct device *pdev = adapter->dev.parent;
	u8 m[3] = {(reg >> 8), (reg & 0xff), data};
	struct i2c_msg msg = {.addr = adr, .flags = 0, .buf = m,
			      .len = sizeof(m)};

	if (i2c_transfer(adapter, &msg, 1) != 1) {
		dev_err(pdev, "Error writing EEPROM!\n");
		return -EIO;
	}
	return 0;
}

static int i2c_read_eeprom(struct i2c_adapter *adapter,
			   u8 adr, u16 reg, u8 *data, int len)
{
	struct device *pdev = adapter->dev.parent;
	u8 msg[2] = {(reg >> 8), (reg & 0xff)};
	struct i2c_msg msgs[2] = {{.addr = adr, .flags = 0,
				   .buf = msg, .len = 2 },
				  {.addr = adr, .flags = I2C_M_RD,
				   .buf = data, .len = len} };

	if (i2c_transfer(adapter, msgs, 2) != 2) {
		dev_err(pdev, "Error reading EEPROM\n");
		return -EIO;
	}
	return 0;
}

static int ReadEEProm(struct i2c_adapter *adapter,
		      u16 Tag, u32 MaxLen, u8 *data, u32 *pLength)
{
	struct device *pdev = adapter->dev.parent;
	int status = 0;
	u16 Addr = MICNG_EE_START, Length, tag = 0;
	u8  EETag[3];

	while (Addr + sizeof(u16) + 1 < MICNG_EE_END) {
		if (i2c_read_eeprom(adapter, 0x50, Addr, EETag, sizeof(EETag)))
			return -1;
		tag = (EETag[0] << 8) | EETag[1];
		if (tag == MICNG_EETAG_END0 || tag == MICNG_EETAG_END1)
			return -1;
		if (tag == Tag)
			break;
		Addr += sizeof(u16) + 1 + EETag[2];
	}
	if (Addr + sizeof(u16) + 1 + EETag[2] > MICNG_EE_END) {
		dev_err(pdev, "Reached EOEE @ Tag = %04x Length = %3d\n",
			tag, EETag[2]);
		return -1;
	}
	Length = EETag[2];
	if (Length > MaxLen)
		Length = (u16) MaxLen;
	if (Length > 0) {
		Addr += sizeof(u16) + 1;
		status = i2c_read_eeprom(adapter, 0x50, Addr, data, Length);
		if (!status) {
			*pLength = EETag[2];
#if 0
			if (Length < EETag[2])
				status = STATUS_BUFFER_OVERFLOW;
#endif
		}
	}
	return status;
}

static int WriteEEProm(struct i2c_adapter *adapter,
		       u16 Tag, u32 Length, u8 *data)
{
	struct device *pdev = adapter->dev.parent;
	int status = 0;
	u16 Addr = MICNG_EE_START;
	u8 EETag[3];
	u16 tag = 0;
	int retry, i;

	while (Addr + sizeof(u16) + 1 < MICNG_EE_END) {
		if (i2c_read_eeprom(adapter, 0x50, Addr, EETag, sizeof(EETag)))
			return -1;
		tag = (EETag[0] << 8) | EETag[1];
		if (tag == MICNG_EETAG_END0 || tag == MICNG_EETAG_END1)
			return -1;
		if (tag == Tag)
			break;
		Addr += sizeof(u16) + 1 + EETag[2];
	}
	if (Addr + sizeof(u16) + 1 + EETag[2] > MICNG_EE_END) {
		dev_err(pdev, "Reached EOEE @ Tag = %04x Length = %3d\n",
			tag, EETag[2]);
		return -1;
	}

	if (Length > EETag[2])
		return -EINVAL;
	/* Note: We write the data one byte at a time to avoid
	   issues with page sizes. (which are different for
	   each manufacture and eeprom size)
	 */
	Addr += sizeof(u16) + 1;
	for (i = 0; i < Length; i++, Addr++) {
		status = i2c_write_eeprom(adapter, 0x50, Addr, data[i]);

		if (status)
			break;

		/* Poll for finishing write cycle */
		retry = 10;
		while (retry) {
			u8 Tmp;

			msleep(50);
			status = i2c_read_eeprom(adapter, 0x50, Addr, &Tmp, 1);
			if (status)
				break;
			if (Tmp != data[i])
				dev_err(pdev, "eeprom write error\n");
			retry -= 1;
		}
		if (status) {
			dev_err(pdev, "Timeout polling eeprom\n");
			break;
		}
	}
	return status;
}

static int eeprom_read_ushort(struct i2c_adapter *adapter, u16 tag, u16 *data)
{
	int stat;
	u8 buf[2];
	u32 len = 0;

	stat = ReadEEProm(adapter, tag, 2, buf, &len);
	if (stat)
		return stat;
	if (len != 2)
		return -EINVAL;

	*data = (buf[0] << 8) | buf[1];
	return 0;
}

static int eeprom_write_ushort(struct i2c_adapter *adapter, u16 tag, u16 data)
{
	int stat;
	u8 buf[2];

	buf[0] = data >> 8;
	buf[1] = data & 0xff;
	stat = WriteEEProm(adapter, tag, 2, buf);
	if (stat)
		return stat;
	return 0;
}

static s16 osc_deviation(void *priv, s16 deviation, int flag)
{
	struct ngene_channel *chan = priv;
	struct device *pdev = &chan->dev->pci_dev->dev;
	struct i2c_adapter *adap = &chan->i2c_adapter;
	u16 data = 0;

	if (flag) {
		data = (u16) deviation;
		dev_info(pdev, "write deviation %d\n",
			 deviation);
		eeprom_write_ushort(adap, 0x1000 + chan->number, data);
	} else {
		if (eeprom_read_ushort(adap, 0x1000 + chan->number, &data))
			data = 0;
		dev_info(pdev, "read deviation %d\n",
			 (s16)data);
	}

	return (s16) data;
}

/****************************************************************************/
/* Switch control (I2C gates, etc.) *****************************************/
/****************************************************************************/


static struct stv090x_config fe_cineS2 = {
	.device         = STV0900,
	.demod_mode     = STV090x_DUAL,
	.clk_mode       = STV090x_CLK_EXT,

	.xtal           = 27000000,
	.address        = 0x68,

	.ts1_mode       = STV090x_TSMODE_SERIAL_PUNCTURED,
	.ts2_mode       = STV090x_TSMODE_SERIAL_PUNCTURED,

	.repeater_level = STV090x_RPTLEVEL_16,

	.adc1_range	= STV090x_ADC_1Vpp,
	.adc2_range	= STV090x_ADC_1Vpp,

	.diseqc_envelope_mode = true,

	.tuner_i2c_lock = cineS2_tuner_i2c_lock,
};

static struct stv090x_config fe_cineS2_2 = {
	.device         = STV0900,
	.demod_mode     = STV090x_DUAL,
	.clk_mode       = STV090x_CLK_EXT,

	.xtal           = 27000000,
	.address        = 0x69,

	.ts1_mode       = STV090x_TSMODE_SERIAL_PUNCTURED,
	.ts2_mode       = STV090x_TSMODE_SERIAL_PUNCTURED,

	.repeater_level = STV090x_RPTLEVEL_16,

	.adc1_range	= STV090x_ADC_1Vpp,
	.adc2_range	= STV090x_ADC_1Vpp,

	.diseqc_envelope_mode = true,

	.tuner_i2c_lock = cineS2_tuner_i2c_lock,
};

static struct stv6110x_config tuner_cineS2_0 = {
	.addr	= 0x60,
	.refclk	= 27000000,
	.clk_div = 1,
};

static struct stv6110x_config tuner_cineS2_1 = {
	.addr	= 0x63,
	.refclk	= 27000000,
	.clk_div = 1,
};

static const struct ngene_info ngene_info_cineS2 = {
	.type		= NGENE_SIDEWINDER,
	.name		= "Linux4Media cineS2 DVB-S2 Twin Tuner",
	.io_type	= {NGENE_IO_TSIN, NGENE_IO_TSIN},
	.demod_attach	= {demod_attach_stv0900, demod_attach_stv0900},
	.tuner_attach	= {tuner_attach_stv6110, tuner_attach_stv6110},
	.fe_config	= {&fe_cineS2, &fe_cineS2},
	.tuner_config	= {&tuner_cineS2_0, &tuner_cineS2_1},
	.lnb		= {0x0b, 0x08},
	.tsf		= {3, 3},
	.fw_version	= 18,
	.msi_supported	= true,
};

static const struct ngene_info ngene_info_satixS2 = {
	.type		= NGENE_SIDEWINDER,
	.name		= "Mystique SaTiX-S2 Dual",
	.io_type	= {NGENE_IO_TSIN, NGENE_IO_TSIN},
	.demod_attach	= {demod_attach_stv0900, demod_attach_stv0900},
	.tuner_attach	= {tuner_attach_stv6110, tuner_attach_stv6110},
	.fe_config	= {&fe_cineS2, &fe_cineS2},
	.tuner_config	= {&tuner_cineS2_0, &tuner_cineS2_1},
	.lnb		= {0x0b, 0x08},
	.tsf		= {3, 3},
	.fw_version	= 18,
	.msi_supported	= true,
};

static const struct ngene_info ngene_info_satixS2v2 = {
	.type		= NGENE_SIDEWINDER,
	.name		= "Mystique SaTiX-S2 Dual (v2)",
	.io_type	= {NGENE_IO_TSIN, NGENE_IO_TSIN, NGENE_IO_TSIN, NGENE_IO_TSIN,
			   NGENE_IO_TSOUT},
	.demod_attach	= {demod_attach_stv0900, demod_attach_stv0900, cineS2_probe, cineS2_probe},
	.tuner_attach	= {tuner_attach_stv6110, tuner_attach_stv6110, tuner_attach_probe, tuner_attach_probe},
	.fe_config	= {&fe_cineS2, &fe_cineS2, &fe_cineS2_2, &fe_cineS2_2},
	.tuner_config	= {&tuner_cineS2_0, &tuner_cineS2_1, &tuner_cineS2_0, &tuner_cineS2_1},
	.lnb		= {0x0a, 0x08, 0x0b, 0x09},
	.tsf		= {3, 3},
	.fw_version	= 18,
	.msi_supported	= true,
};

static const struct ngene_info ngene_info_cineS2v5 = {
	.type		= NGENE_SIDEWINDER,
	.name		= "Linux4Media cineS2 DVB-S2 Twin Tuner (v5)",
	.io_type	= {NGENE_IO_TSIN, NGENE_IO_TSIN, NGENE_IO_TSIN, NGENE_IO_TSIN,
			   NGENE_IO_TSOUT},
	.demod_attach	= {demod_attach_stv0900, demod_attach_stv0900, cineS2_probe, cineS2_probe},
	.tuner_attach	= {tuner_attach_stv6110, tuner_attach_stv6110, tuner_attach_probe, tuner_attach_probe},
	.fe_config	= {&fe_cineS2, &fe_cineS2, &fe_cineS2_2, &fe_cineS2_2},
	.tuner_config	= {&tuner_cineS2_0, &tuner_cineS2_1, &tuner_cineS2_0, &tuner_cineS2_1},
	.lnb		= {0x0a, 0x08, 0x0b, 0x09},
	.tsf		= {3, 3},
	.fw_version	= 18,
	.msi_supported	= true,
};


static const struct ngene_info ngene_info_duoFlex = {
	.type           = NGENE_SIDEWINDER,
	.name           = "Digital Devices DuoFlex PCIe or miniPCIe",
	.io_type        = {NGENE_IO_TSIN, NGENE_IO_TSIN, NGENE_IO_TSIN, NGENE_IO_TSIN,
			   NGENE_IO_TSOUT},
	.demod_attach   = {cineS2_probe, cineS2_probe, cineS2_probe, cineS2_probe},
	.tuner_attach   = {tuner_attach_probe, tuner_attach_probe, tuner_attach_probe, tuner_attach_probe},
	.fe_config      = {&fe_cineS2, &fe_cineS2, &fe_cineS2_2, &fe_cineS2_2},
	.tuner_config   = {&tuner_cineS2_0, &tuner_cineS2_1, &tuner_cineS2_0, &tuner_cineS2_1},
	.lnb            = {0x0a, 0x08, 0x0b, 0x09},
	.tsf            = {3, 3},
	.fw_version     = 18,
	.msi_supported	= true,
};

static const struct ngene_info ngene_info_m780 = {
	.type           = NGENE_APP,
	.name           = "Aver M780 ATSC/QAM-B",

	/* Channel 0 is analog, which is currently unsupported */
	.io_type        = { NGENE_IO_NONE, NGENE_IO_TSIN },
	.demod_attach   = { NULL, demod_attach_lg330x },

	/* Ensure these are NULL else the frame will call them (as funcs) */
	.tuner_attach   = { NULL, NULL, NULL, NULL },
	.fe_config      = { NULL, &aver_m780 },
	.avf            = { 0 },

	/* A custom electrical interface config for the demod to bridge */
	.tsf		= { 4, 4 },
	.fw_version	= 15,
};

static struct drxd_config fe_terratec_dvbt_0 = {
	.index          = 0,
	.demod_address  = 0x70,
	.demod_revision = 0xa2,
	.demoda_address = 0x00,
	.pll_address    = 0x60,
	.pll_type       = DVB_PLL_THOMSON_DTT7520X,
	.clock          = 20000,
	.osc_deviation  = osc_deviation,
};

static struct drxd_config fe_terratec_dvbt_1 = {
	.index          = 1,
	.demod_address  = 0x71,
	.demod_revision = 0xa2,
	.demoda_address = 0x00,
	.pll_address    = 0x60,
	.pll_type       = DVB_PLL_THOMSON_DTT7520X,
	.clock          = 20000,
	.osc_deviation  = osc_deviation,
};

static const struct ngene_info ngene_info_terratec = {
	.type           = NGENE_TERRATEC,
	.name           = "Terratec Integra/Cinergy2400i Dual DVB-T",
	.io_type        = {NGENE_IO_TSIN, NGENE_IO_TSIN},
	.demod_attach   = {demod_attach_drxd, demod_attach_drxd},
	.tuner_attach	= {tuner_attach_dtt7520x, tuner_attach_dtt7520x},
	.fe_config      = {&fe_terratec_dvbt_0, &fe_terratec_dvbt_1},
	.i2c_access     = 1,
};

/****************************************************************************/



/****************************************************************************/
/* PCI Subsystem ID *********************************************************/
/****************************************************************************/

#define NGENE_ID(_subvend, _subdev, _driverdata) { \
	.vendor = NGENE_VID, .device = NGENE_PID, \
	.subvendor = _subvend, .subdevice = _subdev, \
	.driver_data = (unsigned long) &_driverdata }

/****************************************************************************/

static const struct pci_device_id ngene_id_tbl[] = {
	NGENE_ID(0x18c3, 0xab04, ngene_info_cineS2),
	NGENE_ID(0x18c3, 0xab05, ngene_info_cineS2v5),
	NGENE_ID(0x18c3, 0xabc3, ngene_info_cineS2),
	NGENE_ID(0x18c3, 0xabc4, ngene_info_cineS2),
	NGENE_ID(0x18c3, 0xdb01, ngene_info_satixS2),
	NGENE_ID(0x18c3, 0xdb02, ngene_info_satixS2v2),
	NGENE_ID(0x18c3, 0xdd00, ngene_info_cineS2v5),
	NGENE_ID(0x18c3, 0xdd10, ngene_info_duoFlex),
	NGENE_ID(0x18c3, 0xdd20, ngene_info_duoFlex),
	NGENE_ID(0x1461, 0x062e, ngene_info_m780),
	NGENE_ID(0x153b, 0x1167, ngene_info_terratec),
	{0}
};
MODULE_DEVICE_TABLE(pci, ngene_id_tbl);

/****************************************************************************/
/* Init/Exit ****************************************************************/
/****************************************************************************/

static pci_ers_result_t ngene_error_detected(struct pci_dev *dev,
					     enum pci_channel_state state)
{
	dev_err(&dev->dev, "PCI error\n");
	if (state == pci_channel_io_perm_failure)
		return PCI_ERS_RESULT_DISCONNECT;
	if (state == pci_channel_io_frozen)
		return PCI_ERS_RESULT_NEED_RESET;
	return PCI_ERS_RESULT_CAN_RECOVER;
}

static pci_ers_result_t ngene_slot_reset(struct pci_dev *dev)
{
	dev_info(&dev->dev, "slot reset\n");
	return 0;
}

static void ngene_resume(struct pci_dev *dev)
{
	dev_info(&dev->dev, "resume\n");
}

static const struct pci_error_handlers ngene_errors = {
	.error_detected = ngene_error_detected,
	.slot_reset = ngene_slot_reset,
	.resume = ngene_resume,
};

static struct pci_driver ngene_pci_driver = {
	.name        = "ngene",
	.id_table    = ngene_id_tbl,
	.probe       = ngene_probe,
	.remove      = ngene_remove,
	.err_handler = &ngene_errors,
	.shutdown    = ngene_shutdown,
};

static __init int module_init_ngene(void)
{
	/* pr_*() since we don't have a device to use with dev_*() yet */
	pr_info("nGene PCIE bridge driver, Copyright (C) 2005-2007 Micronas\n");

	return pci_register_driver(&ngene_pci_driver);
}

static __exit void module_exit_ngene(void)
{
	pci_unregister_driver(&ngene_pci_driver);
}

module_init(module_init_ngene);
module_exit(module_exit_ngene);

MODULE_DESCRIPTION("nGene");
MODULE_AUTHOR("Micronas, Ralph Metzler, Manfred Voelkel");
MODULE_LICENSE("GPL");

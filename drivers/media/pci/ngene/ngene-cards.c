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
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA
 * Or, point your browser to http://www.gnu.org/copyleft/gpl.html
 */

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


/****************************************************************************/
/* Demod/tuner attachment ***************************************************/
/****************************************************************************/

static int tuner_attach_stv6110(struct ngene_channel *chan)
{
	struct i2c_adapter *i2c;
	struct stv090x_config *feconf = (struct stv090x_config *)
		chan->dev->card_info->fe_config[chan->number];
	struct stv6110x_config *tunerconf = (struct stv6110x_config *)
		chan->dev->card_info->tuner_config[chan->number];
	const struct stv6110x_devctl *ctl;

	/* tuner 1+2: i2c adapter #0, tuner 3+4: i2c adapter #1 */
	if (chan->number < 2)
		i2c = &chan->dev->channel[0].i2c_adapter;
	else
		i2c = &chan->dev->channel[1].i2c_adapter;

	ctl = dvb_attach(stv6110x_attach, chan->fe, tunerconf, i2c);
	if (ctl == NULL) {
		printk(KERN_ERR	DEVICE_NAME ": No STV6110X found!\n");
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
	struct i2c_adapter *i2c;
	struct dvb_frontend *fe;

	i2c = &chan->dev->channel[0].i2c_adapter;
	if (chan->fe->ops.i2c_gate_ctrl)
		chan->fe->ops.i2c_gate_ctrl(chan->fe, 1);
	fe = dvb_attach(tda18271c2dd_attach, chan->fe, i2c, 0x60);
	if (chan->fe->ops.i2c_gate_ctrl)
		chan->fe->ops.i2c_gate_ctrl(chan->fe, 0);
	if (!fe) {
		printk(KERN_ERR "No TDA18271 found!\n");
		return -ENODEV;
	}

	return 0;
}

static int tuner_attach_probe(struct ngene_channel *chan)
{
	if (chan->demod_type == 0)
		return tuner_attach_stv6110(chan);
	if (chan->demod_type == 1)
		return tuner_attach_tda18271(chan);
	return -EINVAL;
}

static int demod_attach_stv0900(struct ngene_channel *chan)
{
	struct i2c_adapter *i2c;
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
		printk(KERN_ERR	DEVICE_NAME ": No STV0900 found!\n");
		return -ENODEV;
	}

	/* store channel info */
	if (feconf->tuner_i2c_lock)
		chan->fe->analog_demod_priv = chan;

	if (!dvb_attach(lnbh24_attach, chan->fe, i2c, 0,
			0, chan->dev->card_info->lnb[chan->number])) {
		printk(KERN_ERR DEVICE_NAME ": No LNBH24 found!\n");
		dvb_frontend_detach(chan->fe);
		chan->fe = NULL;
		return -ENODEV;
	}

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

static int i2c_read(struct i2c_adapter *adapter, u8 adr, u8 *val)
{
	struct i2c_msg msgs[1] = {{.addr = adr,  .flags = I2C_M_RD,
				   .buf  = val,  .len   = 1 } };
	return (i2c_transfer(adapter, msgs, 1) == 1) ? 0 : -1;
}

static int i2c_read_reg16(struct i2c_adapter *adapter, u8 adr,
			  u16 reg, u8 *val)
{
	u8 msg[2] = {reg>>8, reg&0xff};
	struct i2c_msg msgs[2] = {{.addr = adr, .flags = 0,
				   .buf  = msg, .len   = 2},
				  {.addr = adr, .flags = I2C_M_RD,
				   .buf  = val, .len   = 1} };
	return (i2c_transfer(adapter, msgs, 2) == 2) ? 0 : -1;
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

static int demod_attach_drxk(struct ngene_channel *chan,
			     struct i2c_adapter *i2c)
{
	struct drxk_config config;

	memset(&config, 0, sizeof(config));
	config.microcode_name = "drxk_a3.mc";
	config.qam_demod_parameter_count = 4;
	config.adr = 0x29 + (chan->number ^ 2);

	chan->fe = dvb_attach(drxk_attach, &config, i2c);
	if (!chan->fe) {
		printk(KERN_ERR "No DRXK found!\n");
		return -ENODEV;
	}
	chan->fe->sec_priv = chan;
	chan->gate_ctrl = chan->fe->ops.i2c_gate_ctrl;
	chan->fe->ops.i2c_gate_ctrl = drxk_gate_ctrl;
	return 0;
}

static int cineS2_probe(struct ngene_channel *chan)
{
	struct i2c_adapter *i2c;
	struct stv090x_config *fe_conf;
	u8 buf[3];
	struct i2c_msg i2c_msg = { .flags = 0, .buf = buf };
	int rc;

	/* tuner 1+2: i2c adapter #0, tuner 3+4: i2c adapter #1 */
	if (chan->number < 2)
		i2c = &chan->dev->channel[0].i2c_adapter;
	else
		i2c = &chan->dev->channel[1].i2c_adapter;

	if (port_has_stv0900(i2c, chan->number)) {
		chan->demod_type = 0;
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
			printk(KERN_ERR DEVICE_NAME ": could not setup DPNx\n");
			return -EIO;
		}
	} else if (port_has_drxk(i2c, chan->number^2)) {
		chan->demod_type = 1;
		demod_attach_drxk(chan, i2c);
	} else {
		printk(KERN_ERR "No demod found on chan %d\n", chan->number);
		return -ENODEV;
	}
	return 0;
}


static struct lgdt330x_config aver_m780 = {
	.demod_address = 0xb2 >> 1,
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
	chan->fe = dvb_attach(lgdt330x_attach, &aver_m780, &chan->i2c_adapter);
	if (chan->fe == NULL) {
		printk(KERN_ERR	DEVICE_NAME ": No LGDT330x found!\n");
		return -ENODEV;
	}

	dvb_attach(mt2131_attach, chan->fe, &chan->i2c_adapter,
		   &m780_tunerconfig, 0);

	return (chan->fe) ? 0 : -ENODEV;
}

static int demod_attach_drxd(struct ngene_channel *chan)
{
	struct drxd_config *feconf;

	feconf = chan->dev->card_info->fe_config[chan->number];

	chan->fe = dvb_attach(drxd_attach, feconf, chan,
			&chan->i2c_adapter, &chan->dev->pci_dev->dev);
	if (!chan->fe) {
		pr_err("No DRXD found!\n");
		return -ENODEV;
	}
	return 0;
}

static int tuner_attach_dtt7520x(struct ngene_channel *chan)
{
	struct drxd_config *feconf;

	feconf = chan->dev->card_info->fe_config[chan->number];

	if (!dvb_attach(dvb_pll_attach, chan->fe, feconf->pll_address,
			&chan->i2c_adapter,
			feconf->pll_type)) {
		pr_err("No pll(%d) found!\n", feconf->pll_type);
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
	u8 m[3] = {(reg >> 8), (reg & 0xff), data};
	struct i2c_msg msg = {.addr = adr, .flags = 0, .buf = m,
			      .len = sizeof(m)};

	if (i2c_transfer(adapter, &msg, 1) != 1) {
		pr_err(DEVICE_NAME ": Error writing EEPROM!\n");
		return -EIO;
	}
	return 0;
}

static int i2c_read_eeprom(struct i2c_adapter *adapter,
			   u8 adr, u16 reg, u8 *data, int len)
{
	u8 msg[2] = {(reg >> 8), (reg & 0xff)};
	struct i2c_msg msgs[2] = {{.addr = adr, .flags = 0,
				   .buf = msg, .len = 2 },
				  {.addr = adr, .flags = I2C_M_RD,
				   .buf = data, .len = len} };

	if (i2c_transfer(adapter, msgs, 2) != 2) {
		pr_err(DEVICE_NAME ": Error reading EEPROM\n");
		return -EIO;
	}
	return 0;
}

static int ReadEEProm(struct i2c_adapter *adapter,
		      u16 Tag, u32 MaxLen, u8 *data, u32 *pLength)
{
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
		pr_err(DEVICE_NAME
		       ": Reached EOEE @ Tag = %04x Length = %3d\n",
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
		pr_err(DEVICE_NAME
		       ": Reached EOEE @ Tag = %04x Length = %3d\n",
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
				pr_err(DEVICE_NAME
				       "eeprom write error\n");
			retry -= 1;
		}
		if (status) {
			pr_err(DEVICE_NAME
			       ": Timeout polling eeprom\n");
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
	struct i2c_adapter *adap = &chan->i2c_adapter;
	u16 data = 0;

	if (flag) {
		data = (u16) deviation;
		pr_info(DEVICE_NAME ": write deviation %d\n",
		       deviation);
		eeprom_write_ushort(adap, 0x1000 + chan->number, data);
	} else {
		if (eeprom_read_ushort(adap, 0x1000 + chan->number, &data))
			data = 0;
		pr_info(DEVICE_NAME ": read deviation %d\n",
		       (s16) data);
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

static struct ngene_info ngene_info_cineS2 = {
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

static struct ngene_info ngene_info_satixS2 = {
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

static struct ngene_info ngene_info_satixS2v2 = {
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

static struct ngene_info ngene_info_cineS2v5 = {
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


static struct ngene_info ngene_info_duoFlex = {
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

static struct ngene_info ngene_info_m780 = {
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

static struct ngene_info ngene_info_terratec = {
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
	printk(KERN_ERR DEVICE_NAME ": PCI error\n");
	if (state == pci_channel_io_perm_failure)
		return PCI_ERS_RESULT_DISCONNECT;
	if (state == pci_channel_io_frozen)
		return PCI_ERS_RESULT_NEED_RESET;
	return PCI_ERS_RESULT_CAN_RECOVER;
}

static pci_ers_result_t ngene_link_reset(struct pci_dev *dev)
{
	printk(KERN_INFO DEVICE_NAME ": link reset\n");
	return 0;
}

static pci_ers_result_t ngene_slot_reset(struct pci_dev *dev)
{
	printk(KERN_INFO DEVICE_NAME ": slot reset\n");
	return 0;
}

static void ngene_resume(struct pci_dev *dev)
{
	printk(KERN_INFO DEVICE_NAME ": resume\n");
}

static const struct pci_error_handlers ngene_errors = {
	.error_detected = ngene_error_detected,
	.link_reset = ngene_link_reset,
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
	printk(KERN_INFO
	       "nGene PCIE bridge driver, Copyright (C) 2005-2007 Micronas\n");
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

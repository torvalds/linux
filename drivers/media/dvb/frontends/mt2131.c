/*
 *  Driver for Microtune MT2131 "QAM/8VSB single chip tuner"
 *
 *  Copyright (c) 2006 Steven Toth <stoth@hauppauge.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/dvb/frontend.h>
#include <linux/i2c.h>

#include "dvb_frontend.h"

#include "mt2131.h"
#include "mt2131_priv.h"

static int debug;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "Turn on/off debugging (default:off).");

#define dprintk(level,fmt, arg...) if (debug >= level) \
	printk(KERN_INFO "%s: " fmt, "mt2131", ## arg)

static u8 mt2131_config1[] = {
	0x01,
	0x50, 0x00, 0x50, 0x80, 0x00, 0x49, 0xfa, 0x88,
	0x08, 0x77, 0x41, 0x04, 0x00, 0x00, 0x00, 0x32,
	0x7f, 0xda, 0x4c, 0x00, 0x10, 0xaa, 0x78, 0x80,
	0xff, 0x68, 0xa0, 0xff, 0xdd, 0x00, 0x00
};

static u8 mt2131_config2[] = {
	0x10,
	0x7f, 0xc8, 0x0a, 0x5f, 0x00, 0x04
};

static int mt2131_readreg(struct mt2131_priv *priv, u8 reg, u8 *val)
{
	struct i2c_msg msg[2] = {
		{ .addr = priv->cfg->i2c_address, .flags = 0,
		  .buf = &reg, .len = 1 },
		{ .addr = priv->cfg->i2c_address, .flags = I2C_M_RD,
		  .buf = val,  .len = 1 },
	};

	if (i2c_transfer(priv->i2c, msg, 2) != 2) {
		printk(KERN_WARNING "mt2131 I2C read failed\n");
		return -EREMOTEIO;
	}
	return 0;
}

static int mt2131_writereg(struct mt2131_priv *priv, u8 reg, u8 val)
{
	u8 buf[2] = { reg, val };
	struct i2c_msg msg = { .addr = priv->cfg->i2c_address, .flags = 0,
			       .buf = buf, .len = 2 };

	if (i2c_transfer(priv->i2c, &msg, 1) != 1) {
		printk(KERN_WARNING "mt2131 I2C write failed\n");
		return -EREMOTEIO;
	}
	return 0;
}

static int mt2131_writeregs(struct mt2131_priv *priv,u8 *buf, u8 len)
{
	struct i2c_msg msg = { .addr = priv->cfg->i2c_address,
			       .flags = 0, .buf = buf, .len = len };

	if (i2c_transfer(priv->i2c, &msg, 1) != 1) {
		printk(KERN_WARNING "mt2131 I2C write failed (len=%i)\n",
		       (int)len);
		return -EREMOTEIO;
	}
	return 0;
}

static int mt2131_set_params(struct dvb_frontend *fe,
			     struct dvb_frontend_parameters *params)
{
	struct mt2131_priv *priv;
	int ret=0, i;
	u32 freq;
	u8  if_band_center;
	u32 f_lo1, f_lo2;
	u32 div1, num1, div2, num2;
	u8  b[8];
	u8 lockval = 0;

	priv = fe->tuner_priv;
	if (fe->ops.info.type == FE_OFDM)
		priv->bandwidth = params->u.ofdm.bandwidth;
	else
		priv->bandwidth = 0;

	freq = params->frequency / 1000;  // Hz -> kHz
	dprintk(1, "%s() freq=%d\n", __FUNCTION__, freq);

	f_lo1 = freq + MT2131_IF1 * 1000;
	f_lo1 = (f_lo1 / 250) * 250;
	f_lo2 = f_lo1 - freq - MT2131_IF2;

	priv->frequency =  (f_lo1 - f_lo2 - MT2131_IF2) * 1000;

	/* Frequency LO1 = 16MHz * (DIV1 + NUM1/8192 ) */
	num1 = f_lo1 * 64 / (MT2131_FREF / 128);
	div1 = num1 / 8192;
	num1 &= 0x1fff;

	/* Frequency LO2 = 16MHz * (DIV2 + NUM2/8192 ) */
	num2 = f_lo2 * 64 / (MT2131_FREF / 128);
	div2 = num2 / 8192;
	num2 &= 0x1fff;

	if (freq <=   82500) if_band_center = 0x00; else
	if (freq <=  137500) if_band_center = 0x01; else
	if (freq <=  192500) if_band_center = 0x02; else
	if (freq <=  247500) if_band_center = 0x03; else
	if (freq <=  302500) if_band_center = 0x04; else
	if (freq <=  357500) if_band_center = 0x05; else
	if (freq <=  412500) if_band_center = 0x06; else
	if (freq <=  467500) if_band_center = 0x07; else
	if (freq <=  522500) if_band_center = 0x08; else
	if (freq <=  577500) if_band_center = 0x09; else
	if (freq <=  632500) if_band_center = 0x0A; else
	if (freq <=  687500) if_band_center = 0x0B; else
	if (freq <=  742500) if_band_center = 0x0C; else
	if (freq <=  797500) if_band_center = 0x0D; else
	if (freq <=  852500) if_band_center = 0x0E; else
	if (freq <=  907500) if_band_center = 0x0F; else
	if (freq <=  962500) if_band_center = 0x10; else
	if (freq <= 1017500) if_band_center = 0x11; else
	if (freq <= 1072500) if_band_center = 0x12; else if_band_center = 0x13;

	b[0] = 1;
	b[1] = (num1 >> 5) & 0xFF;
	b[2] = (num1 & 0x1F);
	b[3] = div1;
	b[4] = (num2 >> 5) & 0xFF;
	b[5] = num2 & 0x1F;
	b[6] = div2;

	dprintk(1, "IF1: %dMHz IF2: %dMHz\n", MT2131_IF1, MT2131_IF2);
	dprintk(1, "PLL freq=%dkHz  band=%d\n", (int)freq, (int)if_band_center);
	dprintk(1, "PLL f_lo1=%dkHz  f_lo2=%dkHz\n", (int)f_lo1, (int)f_lo2);
	dprintk(1, "PLL div1=%d  num1=%d  div2=%d  num2=%d\n",
		(int)div1, (int)num1, (int)div2, (int)num2);
	dprintk(1, "PLL [1..6]: %2x %2x %2x %2x %2x %2x\n",
		(int)b[1], (int)b[2], (int)b[3], (int)b[4], (int)b[5],
		(int)b[6]);

	ret = mt2131_writeregs(priv,b,7);
	if (ret < 0)
		return ret;

	mt2131_writereg(priv, 0x0b, if_band_center);

	/* Wait for lock */
	i = 0;
	do {
		mt2131_readreg(priv, 0x08, &lockval);
		if ((lockval & 0x88) == 0x88)
			break;
		msleep(4);
		i++;
	} while (i < 10);

	return ret;
}

static int mt2131_get_frequency(struct dvb_frontend *fe, u32 *frequency)
{
	struct mt2131_priv *priv = fe->tuner_priv;
	dprintk(1, "%s()\n", __FUNCTION__);
	*frequency = priv->frequency;
	return 0;
}

static int mt2131_get_bandwidth(struct dvb_frontend *fe, u32 *bandwidth)
{
	struct mt2131_priv *priv = fe->tuner_priv;
	dprintk(1, "%s()\n", __FUNCTION__);
	*bandwidth = priv->bandwidth;
	return 0;
}

static int mt2131_get_status(struct dvb_frontend *fe, u32 *status)
{
	struct mt2131_priv *priv = fe->tuner_priv;
	u8 lock_status = 0;
	u8 afc_status = 0;

	*status = 0;

	mt2131_readreg(priv, 0x08, &lock_status);
	if ((lock_status & 0x88) == 0x88)
		*status = TUNER_STATUS_LOCKED;

	mt2131_readreg(priv, 0x09, &afc_status);
	dprintk(1, "%s() - LO Status = 0x%x, AFC Status = 0x%x\n",
		__FUNCTION__, lock_status, afc_status);

	return 0;
}

static int mt2131_init(struct dvb_frontend *fe)
{
	struct mt2131_priv *priv = fe->tuner_priv;
	int ret;
	dprintk(1, "%s()\n", __FUNCTION__);

	if ((ret = mt2131_writeregs(priv, mt2131_config1,
				    sizeof(mt2131_config1))) < 0)
		return ret;

	mt2131_writereg(priv, 0x0b, 0x09);
	mt2131_writereg(priv, 0x15, 0x47);
	mt2131_writereg(priv, 0x07, 0xf2);
	mt2131_writereg(priv, 0x0b, 0x01);

	if ((ret = mt2131_writeregs(priv, mt2131_config2,
				    sizeof(mt2131_config2))) < 0)
		return ret;

	return ret;
}

static int mt2131_release(struct dvb_frontend *fe)
{
	dprintk(1, "%s()\n", __FUNCTION__);
	kfree(fe->tuner_priv);
	fe->tuner_priv = NULL;
	return 0;
}

static const struct dvb_tuner_ops mt2131_tuner_ops = {
	.info = {
		.name           = "Microtune MT2131",
		.frequency_min  =  48000000,
		.frequency_max  = 860000000,
		.frequency_step =     50000,
	},

	.release       = mt2131_release,
	.init          = mt2131_init,

	.set_params    = mt2131_set_params,
	.get_frequency = mt2131_get_frequency,
	.get_bandwidth = mt2131_get_bandwidth,
	.get_status    = mt2131_get_status
};

struct dvb_frontend * mt2131_attach(struct dvb_frontend *fe,
				    struct i2c_adapter *i2c,
				    struct mt2131_config *cfg, u16 if1)
{
	struct mt2131_priv *priv = NULL;
	u8 id = 0;

	dprintk(1, "%s()\n", __FUNCTION__);

	priv = kzalloc(sizeof(struct mt2131_priv), GFP_KERNEL);
	if (priv == NULL)
		return NULL;

	priv->cfg = cfg;
	priv->bandwidth = 6000000; /* 6MHz */
	priv->i2c = i2c;

	if (mt2131_readreg(priv, 0, &id) != 0) {
		kfree(priv);
		return NULL;
	}
	if ( (id != 0x3E) && (id != 0x3F) ) {
		printk(KERN_ERR "MT2131: Device not found at addr 0x%02x\n",
		       cfg->i2c_address);
		kfree(priv);
		return NULL;
	}

	printk(KERN_INFO "MT2131: successfully identified at address 0x%02x\n",
	       cfg->i2c_address);
	memcpy(&fe->ops.tuner_ops, &mt2131_tuner_ops,
	       sizeof(struct dvb_tuner_ops));

	fe->tuner_priv = priv;
	return fe;
}
EXPORT_SYMBOL(mt2131_attach);

MODULE_AUTHOR("Steven Toth");
MODULE_DESCRIPTION("Microtune MT2131 silicon tuner driver");
MODULE_LICENSE("GPL");

/*
 * Local variables:
 * c-basic-offset: 8
 */

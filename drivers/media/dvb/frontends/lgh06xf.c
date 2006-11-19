/*
 *  lgh06xf.c - ATSC Tuner support for LG TDVS-H06xF
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include "dvb-pll.h"
#include "lgh06xf.h"

#define LG_H06XF_PLL_I2C_ADDR 0x61

struct lgh06xf_priv {
	struct i2c_adapter *i2c;
	u32 frequency;
};

static int lgh06xf_release(struct dvb_frontend *fe)
{
	kfree(fe->tuner_priv);
	fe->tuner_priv = NULL;
	return 0;
}

static int lgh06xf_set_params(struct dvb_frontend* fe,
			      struct dvb_frontend_parameters* params)
{
	struct lgh06xf_priv *priv = fe->tuner_priv;
	u8 buf[4];
	struct i2c_msg msg = { .addr = LG_H06XF_PLL_I2C_ADDR, .flags = 0,
			       .buf = buf, .len = sizeof(buf) };
	u32 div;
	int i;
	int err;

	dvb_pll_configure(&dvb_pll_lg_tdvs_h06xf, buf, params->frequency, 0);
	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);
	if ((err = i2c_transfer(priv->i2c, &msg, 1)) != 1) {
		printk(KERN_WARNING "lgh06xf: %s error "
		       "(addr %02x <- %02x, err = %i)\n",
		       __FUNCTION__, buf[0], buf[1], err);
		if (err < 0)
			return err;
		else
			return -EREMOTEIO;
	}

	/* Set the Auxiliary Byte. */
	buf[0] = buf[2];
	buf[0] &= ~0x20;
	buf[0] |= 0x18;
	buf[1] = 0x50;
	msg.len = 2;
	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);
	if ((err = i2c_transfer(priv->i2c, &msg, 1)) != 1) {
		printk(KERN_WARNING "lgh06xf: %s error "
		       "(addr %02x <- %02x, err = %i)\n",
		       __FUNCTION__, buf[0], buf[1], err);
		if (err < 0)
			return err;
		else
			return -EREMOTEIO;
	}

	// calculate the frequency we set it to
	for (i = 0; i < dvb_pll_lg_tdvs_h06xf.count; i++) {
		if (params->frequency > dvb_pll_lg_tdvs_h06xf.entries[i].limit)
			continue;
		break;
	}
	div = (params->frequency + dvb_pll_lg_tdvs_h06xf.entries[i].offset) /
		dvb_pll_lg_tdvs_h06xf.entries[i].stepsize;
	priv->frequency = (div * dvb_pll_lg_tdvs_h06xf.entries[i].stepsize) -
		dvb_pll_lg_tdvs_h06xf.entries[i].offset;

	return 0;
}

static int lgh06xf_get_frequency(struct dvb_frontend *fe, u32 *frequency)
{
	struct lgh06xf_priv *priv = fe->tuner_priv;
	*frequency = priv->frequency;
	return 0;
}

static struct dvb_tuner_ops lgh06xf_tuner_ops = {
	.release       = lgh06xf_release,
	.set_params    = lgh06xf_set_params,
	.get_frequency = lgh06xf_get_frequency,
};

struct dvb_frontend* lgh06xf_attach(struct dvb_frontend *fe,
				    struct i2c_adapter *i2c)
{
	struct lgh06xf_priv *priv = NULL;

	priv = kzalloc(sizeof(struct lgh06xf_priv), GFP_KERNEL);
	if (priv == NULL)
		return NULL;

	priv->i2c = i2c;

	memcpy(&fe->ops.tuner_ops, &lgh06xf_tuner_ops,
	       sizeof(struct dvb_tuner_ops));

	strlcpy(fe->ops.tuner_ops.info.name, dvb_pll_lg_tdvs_h06xf.name,
		sizeof(fe->ops.tuner_ops.info.name));

	fe->ops.tuner_ops.info.frequency_min = dvb_pll_lg_tdvs_h06xf.min;
	fe->ops.tuner_ops.info.frequency_max = dvb_pll_lg_tdvs_h06xf.max;

	fe->tuner_priv = priv;
	return fe;
}

EXPORT_SYMBOL(lgh06xf_attach);

MODULE_DESCRIPTION("LG TDVS-H06xF ATSC Tuner support");
MODULE_AUTHOR("Michael Krufky");
MODULE_LICENSE("GPL");

/*
 * Local variables:
 * c-basic-offset: 8
 * End:
 */

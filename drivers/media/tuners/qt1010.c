// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Driver for Quantek QT1010 silicon tuner
 *
 *  Copyright (C) 2006 Antti Palosaari <crope@iki.fi>
 *                     Aapo Tahkola <aet@rasterburn.org>
 */
#include "qt1010.h"
#include "qt1010_priv.h"

/* read single register */
static int qt1010_readreg(struct qt1010_priv *priv, u8 reg, u8 *val)
{
	struct i2c_msg msg[2] = {
		{ .addr = priv->cfg->i2c_address,
		  .flags = 0, .buf = &reg, .len = 1 },
		{ .addr = priv->cfg->i2c_address,
		  .flags = I2C_M_RD, .buf = val, .len = 1 },
	};

	if (i2c_transfer(priv->i2c, msg, 2) != 2) {
		dev_warn(&priv->i2c->dev, "%s: i2c rd failed reg=%02x\n",
				KBUILD_MODNAME, reg);
		return -EREMOTEIO;
	}
	return 0;
}

/* write single register */
static int qt1010_writereg(struct qt1010_priv *priv, u8 reg, u8 val)
{
	u8 buf[2] = { reg, val };
	struct i2c_msg msg = { .addr = priv->cfg->i2c_address,
			       .flags = 0, .buf = buf, .len = 2 };

	if (i2c_transfer(priv->i2c, &msg, 1) != 1) {
		dev_warn(&priv->i2c->dev, "%s: i2c wr failed reg=%02x\n",
				KBUILD_MODNAME, reg);
		return -EREMOTEIO;
	}
	return 0;
}

static int qt1010_set_params(struct dvb_frontend *fe)
{
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	struct qt1010_priv *priv;
	int err;
	u32 freq, div, mod1, mod2;
	u8 i, tmpval, reg05;
	qt1010_i2c_oper_t rd[48] = {
		{ QT1010_WR, 0x01, 0x80 },
		{ QT1010_WR, 0x02, 0x3f },
		{ QT1010_WR, 0x05, 0xff }, /* 02 c write */
		{ QT1010_WR, 0x06, 0x44 },
		{ QT1010_WR, 0x07, 0xff }, /* 04 c write */
		{ QT1010_WR, 0x08, 0x08 },
		{ QT1010_WR, 0x09, 0xff }, /* 06 c write */
		{ QT1010_WR, 0x0a, 0xff }, /* 07 c write */
		{ QT1010_WR, 0x0b, 0xff }, /* 08 c write */
		{ QT1010_WR, 0x0c, 0xe1 },
		{ QT1010_WR, 0x1a, 0xff }, /* 10 c write */
		{ QT1010_WR, 0x1b, 0x00 },
		{ QT1010_WR, 0x1c, 0x89 },
		{ QT1010_WR, 0x11, 0xff }, /* 13 c write */
		{ QT1010_WR, 0x12, 0xff }, /* 14 c write */
		{ QT1010_WR, 0x22, 0xff }, /* 15 c write */
		{ QT1010_WR, 0x1e, 0x00 },
		{ QT1010_WR, 0x1e, 0xd0 },
		{ QT1010_RD, 0x22, 0xff }, /* 16 c read */
		{ QT1010_WR, 0x1e, 0x00 },
		{ QT1010_RD, 0x05, 0xff }, /* 20 c read */
		{ QT1010_RD, 0x22, 0xff }, /* 21 c read */
		{ QT1010_WR, 0x23, 0xd0 },
		{ QT1010_WR, 0x1e, 0x00 },
		{ QT1010_WR, 0x1e, 0xe0 },
		{ QT1010_RD, 0x23, 0xff }, /* 25 c read */
		{ QT1010_RD, 0x23, 0xff }, /* 26 c read */
		{ QT1010_WR, 0x1e, 0x00 },
		{ QT1010_WR, 0x24, 0xd0 },
		{ QT1010_WR, 0x1e, 0x00 },
		{ QT1010_WR, 0x1e, 0xf0 },
		{ QT1010_RD, 0x24, 0xff }, /* 31 c read */
		{ QT1010_WR, 0x1e, 0x00 },
		{ QT1010_WR, 0x14, 0x7f },
		{ QT1010_WR, 0x15, 0x7f },
		{ QT1010_WR, 0x05, 0xff }, /* 35 c write */
		{ QT1010_WR, 0x06, 0x00 },
		{ QT1010_WR, 0x15, 0x1f },
		{ QT1010_WR, 0x16, 0xff },
		{ QT1010_WR, 0x18, 0xff },
		{ QT1010_WR, 0x1f, 0xff }, /* 40 c write */
		{ QT1010_WR, 0x20, 0xff }, /* 41 c write */
		{ QT1010_WR, 0x21, 0x53 },
		{ QT1010_WR, 0x25, 0xff }, /* 43 c write */
		{ QT1010_WR, 0x26, 0x15 },
		{ QT1010_WR, 0x00, 0xff }, /* 45 c write */
		{ QT1010_WR, 0x02, 0x00 },
		{ QT1010_WR, 0x01, 0x00 }
	};

#define FREQ1 32000000 /* 32 MHz */
#define FREQ2  4000000 /* 4 MHz Quartz oscillator in the stick? */

	priv = fe->tuner_priv;
	freq = c->frequency;
	div = (freq + QT1010_OFFSET) / QT1010_STEP;
	freq = (div * QT1010_STEP) - QT1010_OFFSET;
	mod1 = (freq + QT1010_OFFSET) % FREQ1;
	mod2 = (freq + QT1010_OFFSET) % FREQ2;
	priv->frequency = freq;

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1); /* open i2c_gate */

	/* reg 05 base value */
	if      (freq < 290000000) reg05 = 0x14; /* 290 MHz */
	else if (freq < 610000000) reg05 = 0x34; /* 610 MHz */
	else if (freq < 802000000) reg05 = 0x54; /* 802 MHz */
	else                       reg05 = 0x74;

	/* 0x5 */
	rd[2].val = reg05;

	/* 07 - set frequency: 32 MHz scale */
	rd[4].val = (freq + QT1010_OFFSET) / FREQ1;

	/* 09 - changes every 8/24 MHz */
	if (mod1 < 8000000) rd[6].val = 0x1d;
	else                rd[6].val = 0x1c;

	/* 0a - set frequency: 4 MHz scale (max 28 MHz) */
	if      (mod1 < 1*FREQ2) rd[7].val = 0x09; /*  +0 MHz */
	else if (mod1 < 2*FREQ2) rd[7].val = 0x08; /*  +4 MHz */
	else if (mod1 < 3*FREQ2) rd[7].val = 0x0f; /*  +8 MHz */
	else if (mod1 < 4*FREQ2) rd[7].val = 0x0e; /* +12 MHz */
	else if (mod1 < 5*FREQ2) rd[7].val = 0x0d; /* +16 MHz */
	else if (mod1 < 6*FREQ2) rd[7].val = 0x0c; /* +20 MHz */
	else if (mod1 < 7*FREQ2) rd[7].val = 0x0b; /* +24 MHz */
	else                     rd[7].val = 0x0a; /* +28 MHz */

	/* 0b - changes every 2/2 MHz */
	if (mod2 < 2000000) rd[8].val = 0x45;
	else                rd[8].val = 0x44;

	/* 1a - set frequency: 125 kHz scale (max 3875 kHz)*/
	tmpval = 0x78; /* byte, overflows intentionally */
	rd[10].val = tmpval-((mod2/QT1010_STEP)*0x08);

	/* 11 */
	rd[13].val = 0xfd; /* TODO: correct value calculation */

	/* 12 */
	rd[14].val = 0x91; /* TODO: correct value calculation */

	/* 22 */
	if      (freq < 450000000) rd[15].val = 0xd0; /* 450 MHz */
	else if (freq < 482000000) rd[15].val = 0xd1; /* 482 MHz */
	else if (freq < 514000000) rd[15].val = 0xd4; /* 514 MHz */
	else if (freq < 546000000) rd[15].val = 0xd7; /* 546 MHz */
	else if (freq < 610000000) rd[15].val = 0xda; /* 610 MHz */
	else                       rd[15].val = 0xd0;

	/* 05 */
	rd[35].val = (reg05 & 0xf0);

	/* 1f */
	if      (mod1 <  8000000) tmpval = 0x00;
	else if (mod1 < 12000000) tmpval = 0x01;
	else if (mod1 < 16000000) tmpval = 0x02;
	else if (mod1 < 24000000) tmpval = 0x03;
	else if (mod1 < 28000000) tmpval = 0x04;
	else                      tmpval = 0x05;
	rd[40].val = (priv->reg1f_init_val + 0x0e + tmpval);

	/* 20 */
	if      (mod1 <  8000000) tmpval = 0x00;
	else if (mod1 < 12000000) tmpval = 0x01;
	else if (mod1 < 20000000) tmpval = 0x02;
	else if (mod1 < 24000000) tmpval = 0x03;
	else if (mod1 < 28000000) tmpval = 0x04;
	else                      tmpval = 0x05;
	rd[41].val = (priv->reg20_init_val + 0x0d + tmpval);

	/* 25 */
	rd[43].val = priv->reg25_init_val;

	/* 00 */
	rd[45].val = 0x92; /* TODO: correct value calculation */

	dev_dbg(&priv->i2c->dev,
			"%s: freq:%u 05:%02x 07:%02x 09:%02x 0a:%02x 0b:%02x " \
			"1a:%02x 11:%02x 12:%02x 22:%02x 05:%02x 1f:%02x " \
			"20:%02x 25:%02x 00:%02x\n", __func__, \
			freq, rd[2].val, rd[4].val, rd[6].val, rd[7].val, \
			rd[8].val, rd[10].val, rd[13].val, rd[14].val, \
			rd[15].val, rd[35].val, rd[40].val, rd[41].val, \
			rd[43].val, rd[45].val);

	for (i = 0; i < ARRAY_SIZE(rd); i++) {
		if (rd[i].oper == QT1010_WR) {
			err = qt1010_writereg(priv, rd[i].reg, rd[i].val);
		} else { /* read is required to proper locking */
			err = qt1010_readreg(priv, rd[i].reg, &tmpval);
		}
		if (err) return err;
	}

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 0); /* close i2c_gate */

	return 0;
}

static int qt1010_init_meas1(struct qt1010_priv *priv,
			     u8 oper, u8 reg, u8 reg_init_val, u8 *retval)
{
	u8 i, val1, val2;
	int err;

	qt1010_i2c_oper_t i2c_data[] = {
		{ QT1010_WR, reg, reg_init_val },
		{ QT1010_WR, 0x1e, 0x00 },
		{ QT1010_WR, 0x1e, oper },
	};

	for (i = 0; i < ARRAY_SIZE(i2c_data); i++) {
		err = qt1010_writereg(priv, i2c_data[i].reg,
				      i2c_data[i].val);
		if (err)
			return err;
	}

	err = qt1010_readreg(priv, reg, &val2);
	if (err)
		return err;
	do {
		val1 = val2;
		err = qt1010_readreg(priv, reg, &val2);
		if (err)
			return err;

		dev_dbg(&priv->i2c->dev, "%s: compare reg:%02x %02x %02x\n",
				__func__, reg, val1, val2);
	} while (val1 != val2);
	*retval = val1;

	return qt1010_writereg(priv, 0x1e, 0x00);
}

static int qt1010_init_meas2(struct qt1010_priv *priv,
			    u8 reg_init_val, u8 *retval)
{
	u8 i, val = 0xff;
	int err;
	qt1010_i2c_oper_t i2c_data[] = {
		{ QT1010_WR, 0x07, reg_init_val },
		{ QT1010_WR, 0x22, 0xd0 },
		{ QT1010_WR, 0x1e, 0x00 },
		{ QT1010_WR, 0x1e, 0xd0 },
		{ QT1010_RD, 0x22, 0xff },
		{ QT1010_WR, 0x1e, 0x00 },
		{ QT1010_WR, 0x22, 0xff }
	};

	for (i = 0; i < ARRAY_SIZE(i2c_data); i++) {
		if (i2c_data[i].oper == QT1010_WR) {
			err = qt1010_writereg(priv, i2c_data[i].reg,
					      i2c_data[i].val);
		} else {
			err = qt1010_readreg(priv, i2c_data[i].reg, &val);
		}
		if (err)
			return err;
	}
	*retval = val;
	return 0;
}

static int qt1010_init(struct dvb_frontend *fe)
{
	struct qt1010_priv *priv = fe->tuner_priv;
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	int err = 0;
	u8 i, tmpval, *valptr = NULL;

	static const qt1010_i2c_oper_t i2c_data[] = {
		{ QT1010_WR, 0x01, 0x80 },
		{ QT1010_WR, 0x0d, 0x84 },
		{ QT1010_WR, 0x0e, 0xb7 },
		{ QT1010_WR, 0x2a, 0x23 },
		{ QT1010_WR, 0x2c, 0xdc },
		{ QT1010_M1, 0x25, 0x40 }, /* get reg 25 init value */
		{ QT1010_M1, 0x81, 0xff }, /* get reg 25 init value */
		{ QT1010_WR, 0x2b, 0x70 },
		{ QT1010_WR, 0x2a, 0x23 },
		{ QT1010_M1, 0x26, 0x08 },
		{ QT1010_M1, 0x82, 0xff },
		{ QT1010_WR, 0x05, 0x14 },
		{ QT1010_WR, 0x06, 0x44 },
		{ QT1010_WR, 0x07, 0x28 },
		{ QT1010_WR, 0x08, 0x0b },
		{ QT1010_WR, 0x11, 0xfd },
		{ QT1010_M1, 0x22, 0x0d },
		{ QT1010_M1, 0xd0, 0xff },
		{ QT1010_WR, 0x06, 0x40 },
		{ QT1010_WR, 0x16, 0xf0 },
		{ QT1010_WR, 0x02, 0x38 },
		{ QT1010_WR, 0x03, 0x18 },
		{ QT1010_WR, 0x20, 0xe0 },
		{ QT1010_M1, 0x1f, 0x20 }, /* get reg 1f init value */
		{ QT1010_M1, 0x84, 0xff }, /* get reg 1f init value */
		{ QT1010_RD, 0x20, 0x20 }, /* get reg 20 init value */
		{ QT1010_WR, 0x03, 0x19 },
		{ QT1010_WR, 0x02, 0x3f },
		{ QT1010_WR, 0x21, 0x53 },
		{ QT1010_RD, 0x21, 0xff },
		{ QT1010_WR, 0x11, 0xfd },
		{ QT1010_WR, 0x05, 0x34 },
		{ QT1010_WR, 0x06, 0x44 },
		{ QT1010_WR, 0x08, 0x08 }
	};

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1); /* open i2c_gate */

	for (i = 0; i < ARRAY_SIZE(i2c_data); i++) {
		switch (i2c_data[i].oper) {
		case QT1010_WR:
			err = qt1010_writereg(priv, i2c_data[i].reg,
					      i2c_data[i].val);
			break;
		case QT1010_RD:
			if (i2c_data[i].val == 0x20)
				valptr = &priv->reg20_init_val;
			else
				valptr = &tmpval;
			err = qt1010_readreg(priv, i2c_data[i].reg, valptr);
			break;
		case QT1010_M1:
			if (i2c_data[i].val == 0x25)
				valptr = &priv->reg25_init_val;
			else if (i2c_data[i].val == 0x1f)
				valptr = &priv->reg1f_init_val;
			else
				valptr = &tmpval;

			BUG_ON(i >= ARRAY_SIZE(i2c_data) - 1);

			err = qt1010_init_meas1(priv, i2c_data[i+1].reg,
						i2c_data[i].reg,
						i2c_data[i].val, valptr);
			i++;
			break;
		}
		if (err)
			return err;
	}

	for (i = 0x31; i < 0x3a; i++) /* 0x31 - 0x39 */
		if ((err = qt1010_init_meas2(priv, i, &tmpval)))
			return err;

	if (!c->frequency)
		c->frequency = 545000000; /* Sigmatek DVB-110 545000000 */
				      /* MSI Megasky 580 GL861 533000000 */
	return qt1010_set_params(fe);
}

static void qt1010_release(struct dvb_frontend *fe)
{
	kfree(fe->tuner_priv);
	fe->tuner_priv = NULL;
}

static int qt1010_get_frequency(struct dvb_frontend *fe, u32 *frequency)
{
	struct qt1010_priv *priv = fe->tuner_priv;
	*frequency = priv->frequency;
	return 0;
}

static int qt1010_get_if_frequency(struct dvb_frontend *fe, u32 *frequency)
{
	*frequency = 36125000;
	return 0;
}

static const struct dvb_tuner_ops qt1010_tuner_ops = {
	.info = {
		.name              = "Quantek QT1010",
		.frequency_min_hz  = QT1010_MIN_FREQ,
		.frequency_max_hz  = QT1010_MAX_FREQ,
		.frequency_step_hz = QT1010_STEP,
	},

	.release       = qt1010_release,
	.init          = qt1010_init,
	/* TODO: implement sleep */

	.set_params    = qt1010_set_params,
	.get_frequency = qt1010_get_frequency,
	.get_if_frequency = qt1010_get_if_frequency,
};

struct dvb_frontend * qt1010_attach(struct dvb_frontend *fe,
				    struct i2c_adapter *i2c,
				    struct qt1010_config *cfg)
{
	struct qt1010_priv *priv = NULL;
	u8 id;

	priv = kzalloc(sizeof(struct qt1010_priv), GFP_KERNEL);
	if (priv == NULL)
		return NULL;

	priv->cfg = cfg;
	priv->i2c = i2c;

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1); /* open i2c_gate */


	/* Try to detect tuner chip. Probably this is not correct register. */
	if (qt1010_readreg(priv, 0x29, &id) != 0 || (id != 0x39)) {
		kfree(priv);
		return NULL;
	}

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 0); /* close i2c_gate */

	dev_info(&priv->i2c->dev,
			"%s: Quantek QT1010 successfully identified\n",
			KBUILD_MODNAME);

	memcpy(&fe->ops.tuner_ops, &qt1010_tuner_ops,
	       sizeof(struct dvb_tuner_ops));

	fe->tuner_priv = priv;
	return fe;
}
EXPORT_SYMBOL(qt1010_attach);

MODULE_DESCRIPTION("Quantek QT1010 silicon tuner driver");
MODULE_AUTHOR("Antti Palosaari <crope@iki.fi>");
MODULE_AUTHOR("Aapo Tahkola <aet@rasterburn.org>");
MODULE_VERSION("0.1");
MODULE_LICENSE("GPL");

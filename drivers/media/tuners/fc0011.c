// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Fitipower FC0011 tuner driver
 *
 * Copyright (C) 2012 Michael Buesch <m@bues.ch>
 *
 * Derived from FC0012 tuner driver:
 * Copyright (C) 2012 Hans-Frieder Vogt <hfvogt@gmx.net>
 */

#include "fc0011.h"


/* Tuner registers */
enum {
	FC11_REG_0,
	FC11_REG_FA,		/* FA */
	FC11_REG_FP,		/* FP */
	FC11_REG_XINHI,		/* XIN high 8 bit */
	FC11_REG_XINLO,		/* XIN low 8 bit */
	FC11_REG_VCO,		/* VCO */
	FC11_REG_VCOSEL,	/* VCO select */
	FC11_REG_7,		/* Unknown tuner reg 7 */
	FC11_REG_8,		/* Unknown tuner reg 8 */
	FC11_REG_9,
	FC11_REG_10,		/* Unknown tuner reg 10 */
	FC11_REG_11,		/* Unknown tuner reg 11 */
	FC11_REG_12,
	FC11_REG_RCCAL,		/* RC calibrate */
	FC11_REG_VCOCAL,	/* VCO calibrate */
	FC11_REG_15,
	FC11_REG_16,		/* Unknown tuner reg 16 */
	FC11_REG_17,

	FC11_NR_REGS,		/* Number of registers */
};

enum FC11_REG_VCOSEL_bits {
	FC11_VCOSEL_2		= 0x08, /* VCO select 2 */
	FC11_VCOSEL_1		= 0x10, /* VCO select 1 */
	FC11_VCOSEL_CLKOUT	= 0x20, /* Fix clock out */
	FC11_VCOSEL_BW7M	= 0x40, /* 7MHz bw */
	FC11_VCOSEL_BW6M	= 0x80, /* 6MHz bw */
};

enum FC11_REG_RCCAL_bits {
	FC11_RCCAL_FORCE	= 0x10, /* force */
};

enum FC11_REG_VCOCAL_bits {
	FC11_VCOCAL_RUN		= 0,	/* VCO calibration run */
	FC11_VCOCAL_VALUEMASK	= 0x3F,	/* VCO calibration value mask */
	FC11_VCOCAL_OK		= 0x40,	/* VCO calibration Ok */
	FC11_VCOCAL_RESET	= 0x80, /* VCO calibration reset */
};


struct fc0011_priv {
	struct i2c_adapter *i2c;
	u8 addr;

	u32 frequency;
	u32 bandwidth;
};


static int fc0011_writereg(struct fc0011_priv *priv, u8 reg, u8 val)
{
	u8 buf[2] = { reg, val };
	struct i2c_msg msg = { .addr = priv->addr,
		.flags = 0, .buf = buf, .len = 2 };

	if (i2c_transfer(priv->i2c, &msg, 1) != 1) {
		dev_err(&priv->i2c->dev,
			"I2C write reg failed, reg: %02x, val: %02x\n",
			reg, val);
		return -EIO;
	}

	return 0;
}

static int fc0011_readreg(struct fc0011_priv *priv, u8 reg, u8 *val)
{
	u8 dummy;
	struct i2c_msg msg[2] = {
		{ .addr = priv->addr,
		  .flags = 0, .buf = &reg, .len = 1 },
		{ .addr = priv->addr,
		  .flags = I2C_M_RD, .buf = val ? : &dummy, .len = 1 },
	};

	if (i2c_transfer(priv->i2c, msg, 2) != 2) {
		dev_err(&priv->i2c->dev,
			"I2C read failed, reg: %02x\n", reg);
		return -EIO;
	}

	return 0;
}

static void fc0011_release(struct dvb_frontend *fe)
{
	kfree(fe->tuner_priv);
	fe->tuner_priv = NULL;
}

static int fc0011_init(struct dvb_frontend *fe)
{
	struct fc0011_priv *priv = fe->tuner_priv;
	int err;

	if (WARN_ON(!fe->callback))
		return -EINVAL;

	err = fe->callback(priv->i2c, DVB_FRONTEND_COMPONENT_TUNER,
			   FC0011_FE_CALLBACK_POWER, priv->addr);
	if (err) {
		dev_err(&priv->i2c->dev, "Power-on callback failed\n");
		return err;
	}
	err = fe->callback(priv->i2c, DVB_FRONTEND_COMPONENT_TUNER,
			   FC0011_FE_CALLBACK_RESET, priv->addr);
	if (err) {
		dev_err(&priv->i2c->dev, "Reset callback failed\n");
		return err;
	}

	return 0;
}

/* Initiate VCO calibration */
static int fc0011_vcocal_trigger(struct fc0011_priv *priv)
{
	int err;

	err = fc0011_writereg(priv, FC11_REG_VCOCAL, FC11_VCOCAL_RESET);
	if (err)
		return err;
	err = fc0011_writereg(priv, FC11_REG_VCOCAL, FC11_VCOCAL_RUN);
	if (err)
		return err;

	return 0;
}

/* Read VCO calibration value */
static int fc0011_vcocal_read(struct fc0011_priv *priv, u8 *value)
{
	int err;

	err = fc0011_writereg(priv, FC11_REG_VCOCAL, FC11_VCOCAL_RUN);
	if (err)
		return err;
	usleep_range(10000, 20000);
	err = fc0011_readreg(priv, FC11_REG_VCOCAL, value);
	if (err)
		return err;

	return 0;
}

static int fc0011_set_params(struct dvb_frontend *fe)
{
	struct dtv_frontend_properties *p = &fe->dtv_property_cache;
	struct fc0011_priv *priv = fe->tuner_priv;
	int err;
	unsigned int i, vco_retries;
	u32 freq = p->frequency / 1000;
	u32 bandwidth = p->bandwidth_hz / 1000;
	u32 fvco, xin, frac, xdiv, xdivr;
	u8 fa, fp, vco_sel, vco_cal;
	u8 regs[FC11_NR_REGS] = { };

	regs[FC11_REG_7] = 0x0F;
	regs[FC11_REG_8] = 0x3E;
	regs[FC11_REG_10] = 0xB8;
	regs[FC11_REG_11] = 0x80;
	regs[FC11_REG_RCCAL] = 0x04;
	err = fc0011_writereg(priv, FC11_REG_7, regs[FC11_REG_7]);
	err |= fc0011_writereg(priv, FC11_REG_8, regs[FC11_REG_8]);
	err |= fc0011_writereg(priv, FC11_REG_10, regs[FC11_REG_10]);
	err |= fc0011_writereg(priv, FC11_REG_11, regs[FC11_REG_11]);
	err |= fc0011_writereg(priv, FC11_REG_RCCAL, regs[FC11_REG_RCCAL]);
	if (err)
		return -EIO;

	/* Set VCO freq and VCO div */
	if (freq < 54000) {
		fvco = freq * 64;
		regs[FC11_REG_VCO] = 0x82;
	} else if (freq < 108000) {
		fvco = freq * 32;
		regs[FC11_REG_VCO] = 0x42;
	} else if (freq < 216000) {
		fvco = freq * 16;
		regs[FC11_REG_VCO] = 0x22;
	} else if (freq < 432000) {
		fvco = freq * 8;
		regs[FC11_REG_VCO] = 0x12;
	} else {
		fvco = freq * 4;
		regs[FC11_REG_VCO] = 0x0A;
	}

	/* Calc XIN. The PLL reference frequency is 18 MHz. */
	xdiv = fvco / 18000;
	WARN_ON(xdiv > 0xFF);
	frac = fvco - xdiv * 18000;
	frac = (frac << 15) / 18000;
	if (frac >= 16384)
		frac += 32786;
	if (!frac)
		xin = 0;
	else
		xin = clamp_t(u32, frac, 512, 65024);
	regs[FC11_REG_XINHI] = xin >> 8;
	regs[FC11_REG_XINLO] = xin;

	/* Calc FP and FA */
	xdivr = xdiv;
	if (fvco - xdiv * 18000 >= 9000)
		xdivr += 1; /* round */
	fp = xdivr / 8;
	fa = xdivr - fp * 8;
	if (fa < 2) {
		fp -= 1;
		fa += 8;
	}
	if (fp > 0x1F) {
		fp = 0x1F;
		fa = 0xF;
	}
	if (fa >= fp) {
		dev_warn(&priv->i2c->dev,
			 "fa %02X >= fp %02X, but trying to continue\n",
			 (unsigned int)(u8)fa, (unsigned int)(u8)fp);
	}
	regs[FC11_REG_FA] = fa;
	regs[FC11_REG_FP] = fp;

	/* Select bandwidth */
	switch (bandwidth) {
	case 8000:
		break;
	case 7000:
		regs[FC11_REG_VCOSEL] |= FC11_VCOSEL_BW7M;
		break;
	default:
		dev_warn(&priv->i2c->dev, "Unsupported bandwidth %u kHz. Using 6000 kHz.\n",
			 bandwidth);
		bandwidth = 6000;
		fallthrough;
	case 6000:
		regs[FC11_REG_VCOSEL] |= FC11_VCOSEL_BW6M;
		break;
	}

	/* Pre VCO select */
	if (fvco < 2320000) {
		vco_sel = 0;
		regs[FC11_REG_VCOSEL] &= ~(FC11_VCOSEL_1 | FC11_VCOSEL_2);
	} else if (fvco < 3080000) {
		vco_sel = 1;
		regs[FC11_REG_VCOSEL] &= ~(FC11_VCOSEL_1 | FC11_VCOSEL_2);
		regs[FC11_REG_VCOSEL] |= FC11_VCOSEL_1;
	} else {
		vco_sel = 2;
		regs[FC11_REG_VCOSEL] &= ~(FC11_VCOSEL_1 | FC11_VCOSEL_2);
		regs[FC11_REG_VCOSEL] |= FC11_VCOSEL_2;
	}

	/* Fix for low freqs */
	if (freq < 45000) {
		regs[FC11_REG_FA] = 0x6;
		regs[FC11_REG_FP] = 0x11;
	}

	/* Clock out fix */
	regs[FC11_REG_VCOSEL] |= FC11_VCOSEL_CLKOUT;

	/* Write the cached registers */
	for (i = FC11_REG_FA; i <= FC11_REG_VCOSEL; i++) {
		err = fc0011_writereg(priv, i, regs[i]);
		if (err)
			return err;
	}

	/* VCO calibration */
	err = fc0011_vcocal_trigger(priv);
	if (err)
		return err;
	err = fc0011_vcocal_read(priv, &vco_cal);
	if (err)
		return err;
	vco_retries = 0;
	while (!(vco_cal & FC11_VCOCAL_OK) && vco_retries < 3) {
		/* Reset the tuner and try again */
		err = fe->callback(priv->i2c, DVB_FRONTEND_COMPONENT_TUNER,
				   FC0011_FE_CALLBACK_RESET, priv->addr);
		if (err) {
			dev_err(&priv->i2c->dev, "Failed to reset tuner\n");
			return err;
		}
		/* Reinit tuner config */
		err = 0;
		for (i = FC11_REG_FA; i <= FC11_REG_VCOSEL; i++)
			err |= fc0011_writereg(priv, i, regs[i]);
		err |= fc0011_writereg(priv, FC11_REG_7, regs[FC11_REG_7]);
		err |= fc0011_writereg(priv, FC11_REG_8, regs[FC11_REG_8]);
		err |= fc0011_writereg(priv, FC11_REG_10, regs[FC11_REG_10]);
		err |= fc0011_writereg(priv, FC11_REG_11, regs[FC11_REG_11]);
		err |= fc0011_writereg(priv, FC11_REG_RCCAL, regs[FC11_REG_RCCAL]);
		if (err)
			return -EIO;
		/* VCO calibration */
		err = fc0011_vcocal_trigger(priv);
		if (err)
			return err;
		err = fc0011_vcocal_read(priv, &vco_cal);
		if (err)
			return err;
		vco_retries++;
	}
	if (!(vco_cal & FC11_VCOCAL_OK)) {
		dev_err(&priv->i2c->dev,
			"Failed to read VCO calibration value (got %02X)\n",
			(unsigned int)vco_cal);
		return -EIO;
	}
	vco_cal &= FC11_VCOCAL_VALUEMASK;

	switch (vco_sel) {
	default:
		WARN_ON(1);
		return -EINVAL;
	case 0:
		if (vco_cal < 8) {
			regs[FC11_REG_VCOSEL] &= ~(FC11_VCOSEL_1 | FC11_VCOSEL_2);
			regs[FC11_REG_VCOSEL] |= FC11_VCOSEL_1;
			err = fc0011_writereg(priv, FC11_REG_VCOSEL,
					      regs[FC11_REG_VCOSEL]);
			if (err)
				return err;
			err = fc0011_vcocal_trigger(priv);
			if (err)
				return err;
		} else {
			regs[FC11_REG_VCOSEL] &= ~(FC11_VCOSEL_1 | FC11_VCOSEL_2);
			err = fc0011_writereg(priv, FC11_REG_VCOSEL,
					      regs[FC11_REG_VCOSEL]);
			if (err)
				return err;
		}
		break;
	case 1:
		if (vco_cal < 5) {
			regs[FC11_REG_VCOSEL] &= ~(FC11_VCOSEL_1 | FC11_VCOSEL_2);
			regs[FC11_REG_VCOSEL] |= FC11_VCOSEL_2;
			err = fc0011_writereg(priv, FC11_REG_VCOSEL,
					      regs[FC11_REG_VCOSEL]);
			if (err)
				return err;
			err = fc0011_vcocal_trigger(priv);
			if (err)
				return err;
		} else if (vco_cal <= 48) {
			regs[FC11_REG_VCOSEL] &= ~(FC11_VCOSEL_1 | FC11_VCOSEL_2);
			regs[FC11_REG_VCOSEL] |= FC11_VCOSEL_1;
			err = fc0011_writereg(priv, FC11_REG_VCOSEL,
					      regs[FC11_REG_VCOSEL]);
			if (err)
				return err;
		} else {
			regs[FC11_REG_VCOSEL] &= ~(FC11_VCOSEL_1 | FC11_VCOSEL_2);
			err = fc0011_writereg(priv, FC11_REG_VCOSEL,
					      regs[FC11_REG_VCOSEL]);
			if (err)
				return err;
			err = fc0011_vcocal_trigger(priv);
			if (err)
				return err;
		}
		break;
	case 2:
		if (vco_cal > 53) {
			regs[FC11_REG_VCOSEL] &= ~(FC11_VCOSEL_1 | FC11_VCOSEL_2);
			regs[FC11_REG_VCOSEL] |= FC11_VCOSEL_1;
			err = fc0011_writereg(priv, FC11_REG_VCOSEL,
					      regs[FC11_REG_VCOSEL]);
			if (err)
				return err;
			err = fc0011_vcocal_trigger(priv);
			if (err)
				return err;
		} else {
			regs[FC11_REG_VCOSEL] &= ~(FC11_VCOSEL_1 | FC11_VCOSEL_2);
			regs[FC11_REG_VCOSEL] |= FC11_VCOSEL_2;
			err = fc0011_writereg(priv, FC11_REG_VCOSEL,
					      regs[FC11_REG_VCOSEL]);
			if (err)
				return err;
		}
		break;
	}
	err = fc0011_vcocal_read(priv, NULL);
	if (err)
		return err;
	usleep_range(10000, 50000);

	err = fc0011_readreg(priv, FC11_REG_RCCAL, &regs[FC11_REG_RCCAL]);
	if (err)
		return err;
	regs[FC11_REG_RCCAL] |= FC11_RCCAL_FORCE;
	err = fc0011_writereg(priv, FC11_REG_RCCAL, regs[FC11_REG_RCCAL]);
	if (err)
		return err;
	regs[FC11_REG_16] = 0xB;
	err = fc0011_writereg(priv, FC11_REG_16, regs[FC11_REG_16]);
	if (err)
		return err;

	dev_dbg(&priv->i2c->dev, "Tuned to fa=%02X fp=%02X xin=%02X%02X vco=%02X vcosel=%02X vcocal=%02X(%u) bw=%u\n",
		(unsigned int)regs[FC11_REG_FA],
		(unsigned int)regs[FC11_REG_FP],
		(unsigned int)regs[FC11_REG_XINHI],
		(unsigned int)regs[FC11_REG_XINLO],
		(unsigned int)regs[FC11_REG_VCO],
		(unsigned int)regs[FC11_REG_VCOSEL],
		(unsigned int)vco_cal, vco_retries,
		(unsigned int)bandwidth);

	priv->frequency = p->frequency;
	priv->bandwidth = p->bandwidth_hz;

	return 0;
}

static int fc0011_get_frequency(struct dvb_frontend *fe, u32 *frequency)
{
	struct fc0011_priv *priv = fe->tuner_priv;

	*frequency = priv->frequency;

	return 0;
}

static int fc0011_get_if_frequency(struct dvb_frontend *fe, u32 *frequency)
{
	*frequency = 0;

	return 0;
}

static int fc0011_get_bandwidth(struct dvb_frontend *fe, u32 *bandwidth)
{
	struct fc0011_priv *priv = fe->tuner_priv;

	*bandwidth = priv->bandwidth;

	return 0;
}

static const struct dvb_tuner_ops fc0011_tuner_ops = {
	.info = {
		.name		  = "Fitipower FC0011",

		.frequency_min_hz =   45 * MHz,
		.frequency_max_hz = 1000 * MHz,
	},

	.release		= fc0011_release,
	.init			= fc0011_init,

	.set_params		= fc0011_set_params,

	.get_frequency		= fc0011_get_frequency,
	.get_if_frequency	= fc0011_get_if_frequency,
	.get_bandwidth		= fc0011_get_bandwidth,
};

struct dvb_frontend *fc0011_attach(struct dvb_frontend *fe,
				   struct i2c_adapter *i2c,
				   const struct fc0011_config *config)
{
	struct fc0011_priv *priv;

	priv = kzalloc(sizeof(struct fc0011_priv), GFP_KERNEL);
	if (!priv)
		return NULL;

	priv->i2c = i2c;
	priv->addr = config->i2c_address;

	fe->tuner_priv = priv;
	fe->ops.tuner_ops = fc0011_tuner_ops;

	dev_info(&priv->i2c->dev, "Fitipower FC0011 tuner attached\n");

	return fe;
}
EXPORT_SYMBOL_GPL(fc0011_attach);

MODULE_DESCRIPTION("Fitipower FC0011 silicon tuner driver");
MODULE_AUTHOR("Michael Buesch <m@bues.ch>");
MODULE_LICENSE("GPL");

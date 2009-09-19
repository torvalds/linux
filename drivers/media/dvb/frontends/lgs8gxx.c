/*
 *    Support for Legend Silicon GB20600 (a.k.a DMB-TH) demodulator
 *    LGS8913, LGS8GL5, LGS8G75
 *    experimental support LGS8G42, LGS8G52
 *
 *    Copyright (C) 2007-2009 David T.L. Wong <davidtlwong@gmail.com>
 *    Copyright (C) 2008 Sirius International (Hong Kong) Limited
 *    Timothy Lee <timothy.lee@siriushk.com> (for initial work on LGS8GL5)
 *
 *    This program is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2 of the License, or
 *    (at your option) any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with this program; if not, write to the Free Software
 *    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include <asm/div64.h>

#include "dvb_frontend.h"

#include "lgs8gxx.h"
#include "lgs8gxx_priv.h"

#define dprintk(args...) \
	do { \
		if (debug) \
			printk(KERN_DEBUG "lgs8gxx: " args); \
	} while (0)

static int debug;
static int fake_signal_str = 1;

module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "Turn on/off frontend debugging (default:off).");

module_param(fake_signal_str, int, 0644);
MODULE_PARM_DESC(fake_signal_str, "fake signal strength for LGS8913."
"Signal strength calculation is slow.(default:on).");

static const u8 lgs8g75_initdat[] = {
	0x01, 0x30, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0x00, 0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0xE4, 0xF5, 0xA8, 0xF5, 0xB8, 0xF5, 0x88, 0xF5,
	0x89, 0xF5, 0x87, 0x75, 0xD0, 0x00, 0x11, 0x50,
	0x11, 0x50, 0xF4, 0xF5, 0x80, 0xF5, 0x90, 0xF5,
	0xA0, 0xF5, 0xB0, 0x75, 0x81, 0x30, 0x80, 0x01,
	0x32, 0x90, 0x80, 0x12, 0x74, 0xFF, 0xF0, 0x90,
	0x80, 0x13, 0x74, 0x1F, 0xF0, 0x90, 0x80, 0x23,
	0x74, 0x01, 0xF0, 0x90, 0x80, 0x22, 0xF0, 0x90,
	0x00, 0x48, 0x74, 0x00, 0xF0, 0x90, 0x80, 0x4D,
	0x74, 0x05, 0xF0, 0x90, 0x80, 0x09, 0xE0, 0x60,
	0x21, 0x12, 0x00, 0xDD, 0x14, 0x60, 0x1B, 0x12,
	0x00, 0xDD, 0x14, 0x60, 0x15, 0x12, 0x00, 0xDD,
	0x14, 0x60, 0x0F, 0x12, 0x00, 0xDD, 0x14, 0x60,
	0x09, 0x12, 0x00, 0xDD, 0x14, 0x60, 0x03, 0x12,
	0x00, 0xDD, 0x90, 0x80, 0x42, 0xE0, 0x60, 0x0B,
	0x14, 0x60, 0x0C, 0x14, 0x60, 0x0D, 0x14, 0x60,
	0x0E, 0x01, 0xB3, 0x74, 0x04, 0x01, 0xB9, 0x74,
	0x05, 0x01, 0xB9, 0x74, 0x07, 0x01, 0xB9, 0x74,
	0x0A, 0xC0, 0xE0, 0x74, 0xC8, 0x12, 0x00, 0xE2,
	0xD0, 0xE0, 0x14, 0x70, 0xF4, 0x90, 0x80, 0x09,
	0xE0, 0x70, 0xAE, 0x12, 0x00, 0xF6, 0x12, 0x00,
	0xFE, 0x90, 0x00, 0x48, 0xE0, 0x04, 0xF0, 0x90,
	0x80, 0x4E, 0xF0, 0x01, 0x73, 0x90, 0x80, 0x08,
	0xF0, 0x22, 0xF8, 0x7A, 0x0C, 0x79, 0xFD, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xD9,
	0xF6, 0xDA, 0xF2, 0xD8, 0xEE, 0x22, 0x90, 0x80,
	0x65, 0xE0, 0x54, 0xFD, 0xF0, 0x22, 0x90, 0x80,
	0x65, 0xE0, 0x44, 0xC2, 0xF0, 0x22
};

/* LGS8GXX internal helper functions */

static int lgs8gxx_write_reg(struct lgs8gxx_state *priv, u8 reg, u8 data)
{
	int ret;
	u8 buf[] = { reg, data };
	struct i2c_msg msg = { .flags = 0, .buf = buf, .len = 2 };

	msg.addr = priv->config->demod_address;
	if (priv->config->prod != LGS8GXX_PROD_LGS8G75 && reg >= 0xC0)
		msg.addr += 0x02;

	if (debug >= 2)
		printk(KERN_DEBUG "%s: reg=0x%02X, data=0x%02X\n",
			__func__, reg, data);

	ret = i2c_transfer(priv->i2c, &msg, 1);

	if (ret != 1)
		dprintk(KERN_DEBUG "%s: error reg=0x%x, data=0x%x, ret=%i\n",
			__func__, reg, data, ret);

	return (ret != 1) ? -1 : 0;
}

static int lgs8gxx_read_reg(struct lgs8gxx_state *priv, u8 reg, u8 *p_data)
{
	int ret;
	u8 dev_addr;

	u8 b0[] = { reg };
	u8 b1[] = { 0 };
	struct i2c_msg msg[] = {
		{ .flags = 0, .buf = b0, .len = 1 },
		{ .flags = I2C_M_RD, .buf = b1, .len = 1 },
	};

	dev_addr = priv->config->demod_address;
	if (priv->config->prod != LGS8GXX_PROD_LGS8G75 && reg >= 0xC0)
		dev_addr += 0x02;
	msg[1].addr =  msg[0].addr = dev_addr;

	ret = i2c_transfer(priv->i2c, msg, 2);
	if (ret != 2) {
		dprintk(KERN_DEBUG "%s: error reg=0x%x, ret=%i\n",
			__func__, reg, ret);
		return -1;
	}

	*p_data = b1[0];
	if (debug >= 2)
		printk(KERN_DEBUG "%s: reg=0x%02X, data=0x%02X\n",
			__func__, reg, b1[0]);
	return 0;
}

static int lgs8gxx_soft_reset(struct lgs8gxx_state *priv)
{
	lgs8gxx_write_reg(priv, 0x02, 0x00);
	msleep(1);
	lgs8gxx_write_reg(priv, 0x02, 0x01);
	msleep(100);

	return 0;
}

static int wait_reg_mask(struct lgs8gxx_state *priv, u8 reg, u8 mask,
	u8 val, u8 delay, u8 tries)
{
	u8 t;
	int i;

	for (i = 0; i < tries; i++) {
		lgs8gxx_read_reg(priv, reg, &t);

		if ((t & mask) == val)
			return 0;
		msleep(delay);
	}

	return 1;
}

static int lgs8gxx_set_ad_mode(struct lgs8gxx_state *priv)
{
	const struct lgs8gxx_config *config = priv->config;
	u8 if_conf;

	if_conf = 0x10; /* AGC output on, RF_AGC output off; */

	if_conf |=
		((config->ext_adc) ? 0x80 : 0x00) |
		((config->if_neg_center) ? 0x04 : 0x00) |
		((config->if_freq == 0) ? 0x08 : 0x00) | /* Baseband */
		((config->adc_signed) ? 0x02 : 0x00) |
		((config->if_neg_edge) ? 0x01 : 0x00);

	if (config->ext_adc &&
		(config->prod == LGS8GXX_PROD_LGS8G52)) {
		lgs8gxx_write_reg(priv, 0xBA, 0x40);
	}

	lgs8gxx_write_reg(priv, 0x07, if_conf);

	return 0;
}

static int lgs8gxx_set_if_freq(struct lgs8gxx_state *priv, u32 freq /*in kHz*/)
{
	u64 val;
	u32 v32;
	u32 if_clk;

	if_clk = priv->config->if_clk_freq;

	val = freq;
	if (freq != 0) {
		val *= (u64)1 << 32;
		if (if_clk != 0)
			do_div(val, if_clk);
		v32 = val & 0xFFFFFFFF;
		dprintk("Set IF Freq to %dkHz\n", freq);
	} else {
		v32 = 0;
		dprintk("Set IF Freq to baseband\n");
	}
	dprintk("AFC_INIT_FREQ = 0x%08X\n", v32);

	if (priv->config->prod == LGS8GXX_PROD_LGS8G75) {
		lgs8gxx_write_reg(priv, 0x08, 0xFF & (v32));
		lgs8gxx_write_reg(priv, 0x09, 0xFF & (v32 >> 8));
		lgs8gxx_write_reg(priv, 0x0A, 0xFF & (v32 >> 16));
		lgs8gxx_write_reg(priv, 0x0B, 0xFF & (v32 >> 24));
	} else {
		lgs8gxx_write_reg(priv, 0x09, 0xFF & (v32));
		lgs8gxx_write_reg(priv, 0x0A, 0xFF & (v32 >> 8));
		lgs8gxx_write_reg(priv, 0x0B, 0xFF & (v32 >> 16));
		lgs8gxx_write_reg(priv, 0x0C, 0xFF & (v32 >> 24));
	}

	return 0;
}

static int lgs8gxx_get_afc_phase(struct lgs8gxx_state *priv)
{
	u64 val;
	u32 v32 = 0;
	u8 reg_addr, t;
	int i;

	if (priv->config->prod == LGS8GXX_PROD_LGS8G75)
		reg_addr = 0x23;
	else
		reg_addr = 0x48;

	for (i = 0; i < 4; i++) {
		lgs8gxx_read_reg(priv, reg_addr, &t);
		v32 <<= 8;
		v32 |= t;
		reg_addr--;
	}

	val = v32;
	val *= priv->config->if_clk_freq;
	val /= (u64)1 << 32;
	dprintk("AFC = %u kHz\n", (u32)val);
	return 0;
}

static int lgs8gxx_set_mode_auto(struct lgs8gxx_state *priv)
{
	u8 t;
	u8 prod = priv->config->prod;

	if (prod == LGS8GXX_PROD_LGS8913)
		lgs8gxx_write_reg(priv, 0xC6, 0x01);

	if (prod == LGS8GXX_PROD_LGS8G75) {
		lgs8gxx_read_reg(priv, 0x0C, &t);
		t &= (~0x04);
		lgs8gxx_write_reg(priv, 0x0C, t | 0x80);
		lgs8gxx_write_reg(priv, 0x39, 0x00);
		lgs8gxx_write_reg(priv, 0x3D, 0x04);
	} else if (prod == LGS8GXX_PROD_LGS8913 ||
		prod == LGS8GXX_PROD_LGS8GL5 ||
		prod == LGS8GXX_PROD_LGS8G42 ||
		prod == LGS8GXX_PROD_LGS8G52 ||
		prod == LGS8GXX_PROD_LGS8G54) {
		lgs8gxx_read_reg(priv, 0x7E, &t);
		lgs8gxx_write_reg(priv, 0x7E, t | 0x01);

		/* clear FEC self reset */
		lgs8gxx_read_reg(priv, 0xC5, &t);
		lgs8gxx_write_reg(priv, 0xC5, t & 0xE0);
	}

	if (prod == LGS8GXX_PROD_LGS8913) {
		/* FEC auto detect */
		lgs8gxx_write_reg(priv, 0xC1, 0x03);

		lgs8gxx_read_reg(priv, 0x7C, &t);
		t = (t & 0x8C) | 0x03;
		lgs8gxx_write_reg(priv, 0x7C, t);

		/* BER test mode */
		lgs8gxx_read_reg(priv, 0xC3, &t);
		t = (t & 0xEF) |  0x10;
		lgs8gxx_write_reg(priv, 0xC3, t);
	}

	if (priv->config->prod == LGS8GXX_PROD_LGS8G52)
		lgs8gxx_write_reg(priv, 0xD9, 0x40);

	return 0;
}

static int lgs8gxx_set_mode_manual(struct lgs8gxx_state *priv)
{
	int ret = 0;
	u8 t;

	if (priv->config->prod == LGS8GXX_PROD_LGS8G75) {
		u8 t2;
		lgs8gxx_read_reg(priv, 0x0C, &t);
		t &= (~0x80);
		lgs8gxx_write_reg(priv, 0x0C, t);

		lgs8gxx_read_reg(priv, 0x0C, &t);
		lgs8gxx_read_reg(priv, 0x19, &t2);

		if (((t&0x03) == 0x01) && (t2&0x01)) {
			lgs8gxx_write_reg(priv, 0x6E, 0x05);
			lgs8gxx_write_reg(priv, 0x39, 0x02);
			lgs8gxx_write_reg(priv, 0x39, 0x03);
			lgs8gxx_write_reg(priv, 0x3D, 0x05);
			lgs8gxx_write_reg(priv, 0x3E, 0x28);
			lgs8gxx_write_reg(priv, 0x53, 0x80);
		} else {
			lgs8gxx_write_reg(priv, 0x6E, 0x3F);
			lgs8gxx_write_reg(priv, 0x39, 0x00);
			lgs8gxx_write_reg(priv, 0x3D, 0x04);
		}

		lgs8gxx_soft_reset(priv);
		return 0;
	}

	/* turn off auto-detect; manual settings */
	lgs8gxx_write_reg(priv, 0x7E, 0);
	if (priv->config->prod == LGS8GXX_PROD_LGS8913)
		lgs8gxx_write_reg(priv, 0xC1, 0);

	ret = lgs8gxx_read_reg(priv, 0xC5, &t);
	t = (t & 0xE0) | 0x06;
	lgs8gxx_write_reg(priv, 0xC5, t);

	lgs8gxx_soft_reset(priv);

	return 0;
}

static int lgs8gxx_is_locked(struct lgs8gxx_state *priv, u8 *locked)
{
	int ret = 0;
	u8 t;

	if (priv->config->prod == LGS8GXX_PROD_LGS8G75)
		ret = lgs8gxx_read_reg(priv, 0x13, &t);
	else
		ret = lgs8gxx_read_reg(priv, 0x4B, &t);
	if (ret != 0)
		return ret;

	if (priv->config->prod == LGS8GXX_PROD_LGS8G75)
		*locked = ((t & 0x80) == 0x80) ? 1 : 0;
	else
		*locked = ((t & 0xC0) == 0xC0) ? 1 : 0;
	return 0;
}

/* Wait for Code Acquisition Lock */
static int lgs8gxx_wait_ca_lock(struct lgs8gxx_state *priv, u8 *locked)
{
	int ret = 0;
	u8 reg, mask, val;

	if (priv->config->prod == LGS8GXX_PROD_LGS8G75) {
		reg = 0x13;
		mask = 0x80;
		val = 0x80;
	} else {
		reg = 0x4B;
		mask = 0xC0;
		val = 0xC0;
	}

	ret = wait_reg_mask(priv, reg, mask, val, 50, 40);
	*locked = (ret == 0) ? 1 : 0;

	return 0;
}

static int lgs8gxx_is_autodetect_finished(struct lgs8gxx_state *priv,
					  u8 *finished)
{
	int ret = 0;
	u8 reg, mask, val;

	if (priv->config->prod == LGS8GXX_PROD_LGS8G75) {
		reg = 0x1f;
		mask = 0xC0;
		val = 0x80;
	} else {
		reg = 0xA4;
		mask = 0x03;
		val = 0x01;
	}

	ret = wait_reg_mask(priv, reg, mask, val, 10, 20);
	*finished = (ret == 0) ? 1 : 0;

	return 0;
}

static int lgs8gxx_autolock_gi(struct lgs8gxx_state *priv, u8 gi, u8 cpn,
	u8 *locked)
{
	int err = 0;
	u8 ad_fini = 0;
	u8 t1, t2;

	if (gi == GI_945)
		dprintk("try GI 945\n");
	else if (gi == GI_595)
		dprintk("try GI 595\n");
	else if (gi == GI_420)
		dprintk("try GI 420\n");
	if (priv->config->prod == LGS8GXX_PROD_LGS8G75) {
		lgs8gxx_read_reg(priv, 0x0C, &t1);
		lgs8gxx_read_reg(priv, 0x18, &t2);
		t1 &= ~(GI_MASK);
		t1 |= gi;
		t2 &= 0xFE;
		t2 |= cpn ? 0x01 : 0x00;
		lgs8gxx_write_reg(priv, 0x0C, t1);
		lgs8gxx_write_reg(priv, 0x18, t2);
	} else {
		lgs8gxx_write_reg(priv, 0x04, gi);
	}
	lgs8gxx_soft_reset(priv);
	err = lgs8gxx_wait_ca_lock(priv, locked);
	if (err || !(*locked))
		return err;
	err = lgs8gxx_is_autodetect_finished(priv, &ad_fini);
	if (err != 0)
		return err;
	if (ad_fini) {
		dprintk("auto detect finished\n");
	} else
		*locked = 0;

	return 0;
}

static int lgs8gxx_auto_detect(struct lgs8gxx_state *priv,
			       u8 *detected_param, u8 *gi)
{
	int i, j;
	int err = 0;
	u8 locked = 0, tmp_gi;

	dprintk("%s\n", __func__);

	lgs8gxx_set_mode_auto(priv);
	if (priv->config->prod == LGS8GXX_PROD_LGS8G75) {
		lgs8gxx_write_reg(priv, 0x67, 0xAA);
		lgs8gxx_write_reg(priv, 0x6E, 0x3F);
	} else {
		/* Guard Interval */
		lgs8gxx_write_reg(priv, 0x03, 00);
	}

	for (i = 0; i < 2; i++) {
		for (j = 0; j < 2; j++) {
			tmp_gi = GI_945;
			err = lgs8gxx_autolock_gi(priv, GI_945, j, &locked);
			if (err)
				goto out;
			if (locked)
				goto locked;
		}
		for (j = 0; j < 2; j++) {
			tmp_gi = GI_420;
			err = lgs8gxx_autolock_gi(priv, GI_420, j, &locked);
			if (err)
				goto out;
			if (locked)
				goto locked;
		}
		tmp_gi = GI_595;
		err = lgs8gxx_autolock_gi(priv, GI_595, 1, &locked);
		if (err)
			goto out;
		if (locked)
			goto locked;
	}

locked:
	if ((err == 0) && (locked == 1)) {
		u8 t;

		if (priv->config->prod != LGS8GXX_PROD_LGS8G75) {
			lgs8gxx_read_reg(priv, 0xA2, &t);
			*detected_param = t;
		} else {
			lgs8gxx_read_reg(priv, 0x1F, &t);
			*detected_param = t & 0x3F;
		}

		if (tmp_gi == GI_945)
			dprintk("GI 945 locked\n");
		else if (tmp_gi == GI_595)
			dprintk("GI 595 locked\n");
		else if (tmp_gi == GI_420)
			dprintk("GI 420 locked\n");
		*gi = tmp_gi;
	}
	if (!locked)
		err = -1;

out:
	return err;
}

static void lgs8gxx_auto_lock(struct lgs8gxx_state *priv)
{
	s8 err;
	u8 gi = 0x2;
	u8 detected_param = 0;

	err = lgs8gxx_auto_detect(priv, &detected_param, &gi);

	if (err != 0) {
		dprintk("lgs8gxx_auto_detect failed\n");
	} else
		dprintk("detected param = 0x%02X\n", detected_param);

	/* Apply detected parameters */
	if (priv->config->prod == LGS8GXX_PROD_LGS8913) {
		u8 inter_leave_len = detected_param & TIM_MASK ;
		/* Fix 8913 time interleaver detection bug */
		inter_leave_len = (inter_leave_len == TIM_MIDDLE) ? 0x60 : 0x40;
		detected_param &= CF_MASK | SC_MASK  | LGS_FEC_MASK;
		detected_param |= inter_leave_len;
	}
	if (priv->config->prod == LGS8GXX_PROD_LGS8G75) {
		u8 t;
		lgs8gxx_read_reg(priv, 0x19, &t);
		t &= 0x81;
		t |= detected_param << 1;
		lgs8gxx_write_reg(priv, 0x19, t);
	} else {
		lgs8gxx_write_reg(priv, 0x7D, detected_param);
		if (priv->config->prod == LGS8GXX_PROD_LGS8913)
			lgs8gxx_write_reg(priv, 0xC0, detected_param);
	}
	/* lgs8gxx_soft_reset(priv); */

	/* Enter manual mode */
	lgs8gxx_set_mode_manual(priv);

	switch (gi) {
	case GI_945:
		priv->curr_gi = 945; break;
	case GI_595:
		priv->curr_gi = 595; break;
	case GI_420:
		priv->curr_gi = 420; break;
	default:
		priv->curr_gi = 945; break;
	}
}

static int lgs8gxx_set_mpeg_mode(struct lgs8gxx_state *priv,
	u8 serial, u8 clk_pol, u8 clk_gated)
{
	int ret = 0;
	u8 t, reg_addr;

	reg_addr = (priv->config->prod == LGS8GXX_PROD_LGS8G75) ? 0x30 : 0xC2;
	ret = lgs8gxx_read_reg(priv, reg_addr, &t);
	if (ret != 0)
		return ret;

	t &= 0xF8;
	t |= serial ? TS_SERIAL : TS_PARALLEL;
	t |= clk_pol ? TS_CLK_INVERTED : TS_CLK_NORMAL;
	t |= clk_gated ? TS_CLK_GATED : TS_CLK_FREERUN;

	ret = lgs8gxx_write_reg(priv, reg_addr, t);
	if (ret != 0)
		return ret;

	return 0;
}

/* A/D input peak-to-peak voltage range */
static int lgs8g75_set_adc_vpp(struct lgs8gxx_state *priv,
	u8 sel)
{
	u8 r26 = 0x73, r27 = 0x90;

	if (priv->config->prod != LGS8GXX_PROD_LGS8G75)
		return 0;

	r26 |= (sel & 0x01) << 7;
	r27 |= (sel & 0x02) >> 1;
	lgs8gxx_write_reg(priv, 0x26, r26);
	lgs8gxx_write_reg(priv, 0x27, r27);

	return 0;
}

/* LGS8913 demod frontend functions */

static int lgs8913_init(struct lgs8gxx_state *priv)
{
	u8 t;

	/* LGS8913 specific */
	lgs8gxx_write_reg(priv, 0xc1, 0x3);

	lgs8gxx_read_reg(priv, 0x7c, &t);
	lgs8gxx_write_reg(priv, 0x7c, (t&0x8c) | 0x3);

	/* LGS8913 specific */
	lgs8gxx_read_reg(priv, 0xc3, &t);
	lgs8gxx_write_reg(priv, 0xc3, t&0x10);


	return 0;
}

static int lgs8g75_init_data(struct lgs8gxx_state *priv)
{
	const u8 *p = lgs8g75_initdat;
	int i;

	lgs8gxx_write_reg(priv, 0xC6, 0x40);

	lgs8gxx_write_reg(priv, 0x3D, 0x04);
	lgs8gxx_write_reg(priv, 0x39, 0x00);

	lgs8gxx_write_reg(priv, 0x3A, 0x00);
	lgs8gxx_write_reg(priv, 0x38, 0x00);
	lgs8gxx_write_reg(priv, 0x3B, 0x00);
	lgs8gxx_write_reg(priv, 0x38, 0x00);

	for (i = 0; i < sizeof(lgs8g75_initdat); i++) {
		lgs8gxx_write_reg(priv, 0x38, 0x00);
		lgs8gxx_write_reg(priv, 0x3A, (u8)(i&0xff));
		lgs8gxx_write_reg(priv, 0x3B, (u8)(i>>8));
		lgs8gxx_write_reg(priv, 0x3C, *p);
		p++;
	}

	lgs8gxx_write_reg(priv, 0x38, 0x00);

	return 0;
}

static int lgs8gxx_init(struct dvb_frontend *fe)
{
	struct lgs8gxx_state *priv =
		(struct lgs8gxx_state *)fe->demodulator_priv;
	const struct lgs8gxx_config *config = priv->config;
	u8 data = 0;
	s8 err;
	dprintk("%s\n", __func__);

	lgs8gxx_read_reg(priv, 0, &data);
	dprintk("reg 0 = 0x%02X\n", data);

	if (config->prod == LGS8GXX_PROD_LGS8G75)
		lgs8g75_set_adc_vpp(priv, config->adc_vpp);

	/* Setup MPEG output format */
	err = lgs8gxx_set_mpeg_mode(priv, config->serial_ts,
				    config->ts_clk_pol,
				    config->ts_clk_gated);
	if (err != 0)
		return -EIO;

	if (config->prod == LGS8GXX_PROD_LGS8913)
		lgs8913_init(priv);
	lgs8gxx_set_if_freq(priv, priv->config->if_freq);
	lgs8gxx_set_ad_mode(priv);

	return 0;
}

static void lgs8gxx_release(struct dvb_frontend *fe)
{
	struct lgs8gxx_state *state = fe->demodulator_priv;
	dprintk("%s\n", __func__);

	kfree(state);
}


static int lgs8gxx_write(struct dvb_frontend *fe, u8 *buf, int len)
{
	struct lgs8gxx_state *priv = fe->demodulator_priv;

	if (len != 2)
		return -EINVAL;

	return lgs8gxx_write_reg(priv, buf[0], buf[1]);
}

static int lgs8gxx_set_fe(struct dvb_frontend *fe,
			  struct dvb_frontend_parameters *fe_params)
{
	struct lgs8gxx_state *priv = fe->demodulator_priv;

	dprintk("%s\n", __func__);

	/* set frequency */
	if (fe->ops.tuner_ops.set_params) {
		fe->ops.tuner_ops.set_params(fe, fe_params);
		if (fe->ops.i2c_gate_ctrl)
			fe->ops.i2c_gate_ctrl(fe, 0);
	}

	/* start auto lock */
	lgs8gxx_auto_lock(priv);

	msleep(10);

	return 0;
}

static int lgs8gxx_get_fe(struct dvb_frontend *fe,
			  struct dvb_frontend_parameters *fe_params)
{
	dprintk("%s\n", __func__);

	/* TODO: get real readings from device */
	/* inversion status */
	fe_params->inversion = INVERSION_OFF;

	/* bandwidth */
	fe_params->u.ofdm.bandwidth = BANDWIDTH_8_MHZ;

	fe_params->u.ofdm.code_rate_HP = FEC_AUTO;
	fe_params->u.ofdm.code_rate_LP = FEC_AUTO;

	fe_params->u.ofdm.constellation = QAM_AUTO;

	/* transmission mode */
	fe_params->u.ofdm.transmission_mode = TRANSMISSION_MODE_AUTO;

	/* guard interval */
	fe_params->u.ofdm.guard_interval = GUARD_INTERVAL_AUTO;

	/* hierarchy */
	fe_params->u.ofdm.hierarchy_information = HIERARCHY_NONE;

	return 0;
}

static
int lgs8gxx_get_tune_settings(struct dvb_frontend *fe,
			      struct dvb_frontend_tune_settings *fesettings)
{
	/* FIXME: copy from tda1004x.c */
	fesettings->min_delay_ms = 800;
	fesettings->step_size = 0;
	fesettings->max_drift = 0;
	return 0;
}

static int lgs8gxx_read_status(struct dvb_frontend *fe, fe_status_t *fe_status)
{
	struct lgs8gxx_state *priv = fe->demodulator_priv;
	s8 ret;
	u8 t, locked = 0;

	dprintk("%s\n", __func__);
	*fe_status = 0;

	lgs8gxx_get_afc_phase(priv);
	lgs8gxx_is_locked(priv, &locked);
	if (priv->config->prod == LGS8GXX_PROD_LGS8G75) {
		if (locked)
			*fe_status |= FE_HAS_SIGNAL | FE_HAS_CARRIER |
				FE_HAS_VITERBI | FE_HAS_SYNC | FE_HAS_LOCK;
		return 0;
	}

	ret = lgs8gxx_read_reg(priv, 0x4B, &t);
	if (ret != 0)
		return -EIO;

	dprintk("Reg 0x4B: 0x%02X\n", t);

	*fe_status = 0;
	if (priv->config->prod == LGS8GXX_PROD_LGS8913) {
		if ((t & 0x40) == 0x40)
			*fe_status |= FE_HAS_SIGNAL | FE_HAS_CARRIER;
		if ((t & 0x80) == 0x80)
			*fe_status |= FE_HAS_VITERBI | FE_HAS_SYNC |
				FE_HAS_LOCK;
	} else {
		if ((t & 0x80) == 0x80)
			*fe_status |= FE_HAS_SIGNAL | FE_HAS_CARRIER |
				FE_HAS_VITERBI | FE_HAS_SYNC | FE_HAS_LOCK;
	}

	/* success */
	dprintk("%s: fe_status=0x%x\n", __func__, *fe_status);
	return 0;
}

static int lgs8gxx_read_signal_agc(struct lgs8gxx_state *priv, u16 *signal)
{
	u16 v;
	u8 agc_lvl[2], cat;

	dprintk("%s()\n", __func__);
	lgs8gxx_read_reg(priv, 0x3F, &agc_lvl[0]);
	lgs8gxx_read_reg(priv, 0x3E, &agc_lvl[1]);

	v = agc_lvl[0];
	v <<= 8;
	v |= agc_lvl[1];

	dprintk("agc_lvl: 0x%04X\n", v);

	if (v < 0x100)
		cat = 0;
	else if (v < 0x190)
		cat = 5;
	else if (v < 0x2A8)
		cat = 4;
	else if (v < 0x381)
		cat = 3;
	else if (v < 0x400)
		cat = 2;
	else if (v == 0x400)
		cat = 1;
	else
		cat = 0;

	*signal = cat * 65535 / 5;

	return 0;
}

static int lgs8913_read_signal_strength(struct lgs8gxx_state *priv, u16 *signal)
{
	u8 t; s8 ret;
	s16 max_strength = 0;
	u8 str;
	u16 i, gi = priv->curr_gi;

	dprintk("%s\n", __func__);

	ret = lgs8gxx_read_reg(priv, 0x4B, &t);
	if (ret != 0)
		return -EIO;

	if (fake_signal_str) {
		if ((t & 0xC0) == 0xC0) {
			dprintk("Fake signal strength\n");
			*signal = 0x7FFF;
		} else
			*signal = 0;
		return 0;
	}

	dprintk("gi = %d\n", gi);
	for (i = 0; i < gi; i++) {

		if ((i & 0xFF) == 0)
			lgs8gxx_write_reg(priv, 0x84, 0x03 & (i >> 8));
		lgs8gxx_write_reg(priv, 0x83, i & 0xFF);

		lgs8gxx_read_reg(priv, 0x94, &str);
		if (max_strength < str)
			max_strength = str;
	}

	*signal = max_strength;
	dprintk("%s: signal=0x%02X\n", __func__, *signal);

	lgs8gxx_read_reg(priv, 0x95, &t);
	dprintk("%s: AVG Noise=0x%02X\n", __func__, t);

	return 0;
}

static int lgs8g75_read_signal_strength(struct lgs8gxx_state *priv, u16 *signal)
{
	u8 t;
	s16 v = 0;

	dprintk("%s\n", __func__);

	lgs8gxx_read_reg(priv, 0xB1, &t);
	v |= t;
	v <<= 8;
	lgs8gxx_read_reg(priv, 0xB0, &t);
	v |= t;

	*signal = v;
	dprintk("%s: signal=0x%02X\n", __func__, *signal);

	return 0;
}

static int lgs8gxx_read_signal_strength(struct dvb_frontend *fe, u16 *signal)
{
	struct lgs8gxx_state *priv = fe->demodulator_priv;

	if (priv->config->prod == LGS8GXX_PROD_LGS8913)
		return lgs8913_read_signal_strength(priv, signal);
	else if (priv->config->prod == LGS8GXX_PROD_LGS8G75)
		return lgs8g75_read_signal_strength(priv, signal);
	else
		return lgs8gxx_read_signal_agc(priv, signal);
}

static int lgs8gxx_read_snr(struct dvb_frontend *fe, u16 *snr)
{
	struct lgs8gxx_state *priv = fe->demodulator_priv;
	u8 t;
	*snr = 0;

	if (priv->config->prod == LGS8GXX_PROD_LGS8G75)
		lgs8gxx_read_reg(priv, 0x34, &t);
	else
		lgs8gxx_read_reg(priv, 0x95, &t);
	dprintk("AVG Noise=0x%02X\n", t);
	*snr = 256 - t;
	*snr <<= 8;
	dprintk("snr=0x%x\n", *snr);

	return 0;
}

static int lgs8gxx_read_ucblocks(struct dvb_frontend *fe, u32 *ucblocks)
{
	*ucblocks = 0;
	dprintk("%s: ucblocks=0x%x\n", __func__, *ucblocks);
	return 0;
}

static void packet_counter_start(struct lgs8gxx_state *priv)
{
	u8 orig, t;

	if (priv->config->prod == LGS8GXX_PROD_LGS8G75) {
		lgs8gxx_read_reg(priv, 0x30, &orig);
		orig &= 0xE7;
		t = orig | 0x10;
		lgs8gxx_write_reg(priv, 0x30, t);
		t = orig | 0x18;
		lgs8gxx_write_reg(priv, 0x30, t);
		t = orig | 0x10;
		lgs8gxx_write_reg(priv, 0x30, t);
	} else {
		lgs8gxx_write_reg(priv, 0xC6, 0x01);
		lgs8gxx_write_reg(priv, 0xC6, 0x41);
		lgs8gxx_write_reg(priv, 0xC6, 0x01);
	}
}

static void packet_counter_stop(struct lgs8gxx_state *priv)
{
	u8 t;

	if (priv->config->prod == LGS8GXX_PROD_LGS8G75) {
		lgs8gxx_read_reg(priv, 0x30, &t);
		t &= 0xE7;
		lgs8gxx_write_reg(priv, 0x30, t);
	} else {
		lgs8gxx_write_reg(priv, 0xC6, 0x81);
	}
}

static int lgs8gxx_read_ber(struct dvb_frontend *fe, u32 *ber)
{
	struct lgs8gxx_state *priv = fe->demodulator_priv;
	u8 reg_err, reg_total, t;
	u32 total_cnt = 0, err_cnt = 0;
	int i;

	dprintk("%s\n", __func__);

	packet_counter_start(priv);
	msleep(200);
	packet_counter_stop(priv);

	if (priv->config->prod == LGS8GXX_PROD_LGS8G75) {
		reg_total = 0x28; reg_err = 0x2C;
	} else {
		reg_total = 0xD0; reg_err = 0xD4;
	}

	for (i = 0; i < 4; i++) {
		total_cnt <<= 8;
		lgs8gxx_read_reg(priv, reg_total+3-i, &t);
		total_cnt |= t;
	}
	for (i = 0; i < 4; i++) {
		err_cnt <<= 8;
		lgs8gxx_read_reg(priv, reg_err+3-i, &t);
		err_cnt |= t;
	}
	dprintk("error=%d total=%d\n", err_cnt, total_cnt);

	if (total_cnt == 0)
		*ber = 0;
	else
		*ber = err_cnt * 100 / total_cnt;

	dprintk("%s: ber=0x%x\n", __func__, *ber);
	return 0;
}

static int lgs8gxx_i2c_gate_ctrl(struct dvb_frontend *fe, int enable)
{
	struct lgs8gxx_state *priv = fe->demodulator_priv;

	if (priv->config->tuner_address == 0)
		return 0;
	if (enable) {
		u8 v = 0x80 | priv->config->tuner_address;
		return lgs8gxx_write_reg(priv, 0x01, v);
	}
	return lgs8gxx_write_reg(priv, 0x01, 0);
}

static struct dvb_frontend_ops lgs8gxx_ops = {
	.info = {
		.name = "Legend Silicon LGS8913/LGS8GXX DMB-TH",
		.type = FE_OFDM,
		.frequency_min = 474000000,
		.frequency_max = 858000000,
		.frequency_stepsize = 10000,
		.caps =
			FE_CAN_FEC_AUTO |
			FE_CAN_QAM_AUTO |
			FE_CAN_TRANSMISSION_MODE_AUTO |
			FE_CAN_GUARD_INTERVAL_AUTO
	},

	.release = lgs8gxx_release,

	.init = lgs8gxx_init,
	.write = lgs8gxx_write,
	.i2c_gate_ctrl = lgs8gxx_i2c_gate_ctrl,

	.set_frontend = lgs8gxx_set_fe,
	.get_frontend = lgs8gxx_get_fe,
	.get_tune_settings = lgs8gxx_get_tune_settings,

	.read_status = lgs8gxx_read_status,
	.read_ber = lgs8gxx_read_ber,
	.read_signal_strength = lgs8gxx_read_signal_strength,
	.read_snr = lgs8gxx_read_snr,
	.read_ucblocks = lgs8gxx_read_ucblocks,
};

struct dvb_frontend *lgs8gxx_attach(const struct lgs8gxx_config *config,
	struct i2c_adapter *i2c)
{
	struct lgs8gxx_state *priv = NULL;
	u8 data = 0;

	dprintk("%s()\n", __func__);

	if (config == NULL || i2c == NULL)
		return NULL;

	priv = kzalloc(sizeof(struct lgs8gxx_state), GFP_KERNEL);
	if (priv == NULL)
		goto error_out;

	priv->config = config;
	priv->i2c = i2c;

	/* check if the demod is there */
	if (lgs8gxx_read_reg(priv, 0, &data) != 0) {
		dprintk("%s lgs8gxx not found at i2c addr 0x%02X\n",
			__func__, priv->config->demod_address);
		goto error_out;
	}

	lgs8gxx_read_reg(priv, 1, &data);

	memcpy(&priv->frontend.ops, &lgs8gxx_ops,
	       sizeof(struct dvb_frontend_ops));
	priv->frontend.demodulator_priv = priv;

	if (config->prod == LGS8GXX_PROD_LGS8G75)
		lgs8g75_init_data(priv);

	return &priv->frontend;

error_out:
	dprintk("%s() error_out\n", __func__);
	kfree(priv);
	return NULL;

}
EXPORT_SYMBOL(lgs8gxx_attach);

MODULE_DESCRIPTION("Legend Silicon LGS8913/LGS8GXX DMB-TH demodulator driver");
MODULE_AUTHOR("David T. L. Wong <davidtlwong@gmail.com>");
MODULE_LICENSE("GPL");

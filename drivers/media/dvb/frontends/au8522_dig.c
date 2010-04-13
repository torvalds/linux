/*
    Auvitek AU8522 QAM/8VSB demodulator driver

    Copyright (C) 2008 Steven Toth <stoth@linuxtv.org>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

*/

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/delay.h>
#include "dvb_frontend.h"
#include "au8522.h"
#include "au8522_priv.h"

static int debug;

/* Despite the name "hybrid_tuner", the framework works just as well for
   hybrid demodulators as well... */
static LIST_HEAD(hybrid_tuner_instance_list);
static DEFINE_MUTEX(au8522_list_mutex);

#define dprintk(arg...)\
	do { if (debug)\
		printk(arg);\
	} while (0)

/* 16 bit registers, 8 bit values */
int au8522_writereg(struct au8522_state *state, u16 reg, u8 data)
{
	int ret;
	u8 buf[] = { (reg >> 8) | 0x80, reg & 0xff, data };

	struct i2c_msg msg = { .addr = state->config->demod_address,
			       .flags = 0, .buf = buf, .len = 3 };

	ret = i2c_transfer(state->i2c, &msg, 1);

	if (ret != 1)
		printk("%s: writereg error (reg == 0x%02x, val == 0x%04x, "
		       "ret == %i)\n", __func__, reg, data, ret);

	return (ret != 1) ? -1 : 0;
}

u8 au8522_readreg(struct au8522_state *state, u16 reg)
{
	int ret;
	u8 b0[] = { (reg >> 8) | 0x40, reg & 0xff };
	u8 b1[] = { 0 };

	struct i2c_msg msg[] = {
		{ .addr = state->config->demod_address, .flags = 0,
		  .buf = b0, .len = 2 },
		{ .addr = state->config->demod_address, .flags = I2C_M_RD,
		  .buf = b1, .len = 1 } };

	ret = i2c_transfer(state->i2c, msg, 2);

	if (ret != 2)
		printk(KERN_ERR "%s: readreg error (ret == %i)\n",
		       __func__, ret);
	return b1[0];
}

static int au8522_i2c_gate_ctrl(struct dvb_frontend *fe, int enable)
{
	struct au8522_state *state = fe->demodulator_priv;

	dprintk("%s(%d)\n", __func__, enable);

	if (enable)
		return au8522_writereg(state, 0x106, 1);
	else
		return au8522_writereg(state, 0x106, 0);
}

struct mse2snr_tab {
	u16 val;
	u16 data;
};

/* VSB SNR lookup table */
static struct mse2snr_tab vsb_mse2snr_tab[] = {
	{   0, 270 },
	{   2, 250 },
	{   3, 240 },
	{   5, 230 },
	{   7, 220 },
	{   9, 210 },
	{  12, 200 },
	{  13, 195 },
	{  15, 190 },
	{  17, 185 },
	{  19, 180 },
	{  21, 175 },
	{  24, 170 },
	{  27, 165 },
	{  31, 160 },
	{  32, 158 },
	{  33, 156 },
	{  36, 152 },
	{  37, 150 },
	{  39, 148 },
	{  40, 146 },
	{  41, 144 },
	{  43, 142 },
	{  44, 140 },
	{  48, 135 },
	{  50, 130 },
	{  43, 142 },
	{  53, 125 },
	{  56, 120 },
	{ 256, 115 },
};

/* QAM64 SNR lookup table */
static struct mse2snr_tab qam64_mse2snr_tab[] = {
	{  15,   0 },
	{  16, 290 },
	{  17, 288 },
	{  18, 286 },
	{  19, 284 },
	{  20, 282 },
	{  21, 281 },
	{  22, 279 },
	{  23, 277 },
	{  24, 275 },
	{  25, 273 },
	{  26, 271 },
	{  27, 269 },
	{  28, 268 },
	{  29, 266 },
	{  30, 264 },
	{  31, 262 },
	{  32, 260 },
	{  33, 259 },
	{  34, 258 },
	{  35, 256 },
	{  36, 255 },
	{  37, 254 },
	{  38, 252 },
	{  39, 251 },
	{  40, 250 },
	{  41, 249 },
	{  42, 248 },
	{  43, 246 },
	{  44, 245 },
	{  45, 244 },
	{  46, 242 },
	{  47, 241 },
	{  48, 240 },
	{  50, 239 },
	{  51, 238 },
	{  53, 237 },
	{  54, 236 },
	{  56, 235 },
	{  57, 234 },
	{  59, 233 },
	{  60, 232 },
	{  62, 231 },
	{  63, 230 },
	{  65, 229 },
	{  67, 228 },
	{  68, 227 },
	{  70, 226 },
	{  71, 225 },
	{  73, 224 },
	{  74, 223 },
	{  76, 222 },
	{  78, 221 },
	{  80, 220 },
	{  82, 219 },
	{  85, 218 },
	{  88, 217 },
	{  90, 216 },
	{  92, 215 },
	{  93, 214 },
	{  94, 212 },
	{  95, 211 },
	{  97, 210 },
	{  99, 209 },
	{ 101, 208 },
	{ 102, 207 },
	{ 104, 206 },
	{ 107, 205 },
	{ 111, 204 },
	{ 114, 203 },
	{ 118, 202 },
	{ 122, 201 },
	{ 125, 200 },
	{ 128, 199 },
	{ 130, 198 },
	{ 132, 197 },
	{ 256, 190 },
};

/* QAM256 SNR lookup table */
static struct mse2snr_tab qam256_mse2snr_tab[] = {
	{  16,   0 },
	{  17, 400 },
	{  18, 398 },
	{  19, 396 },
	{  20, 394 },
	{  21, 392 },
	{  22, 390 },
	{  23, 388 },
	{  24, 386 },
	{  25, 384 },
	{  26, 382 },
	{  27, 380 },
	{  28, 379 },
	{  29, 378 },
	{  30, 377 },
	{  31, 376 },
	{  32, 375 },
	{  33, 374 },
	{  34, 373 },
	{  35, 372 },
	{  36, 371 },
	{  37, 370 },
	{  38, 362 },
	{  39, 354 },
	{  40, 346 },
	{  41, 338 },
	{  42, 330 },
	{  43, 328 },
	{  44, 326 },
	{  45, 324 },
	{  46, 322 },
	{  47, 320 },
	{  48, 319 },
	{  49, 318 },
	{  50, 317 },
	{  51, 316 },
	{  52, 315 },
	{  53, 314 },
	{  54, 313 },
	{  55, 312 },
	{  56, 311 },
	{  57, 310 },
	{  58, 308 },
	{  59, 306 },
	{  60, 304 },
	{  61, 302 },
	{  62, 300 },
	{  63, 298 },
	{  65, 295 },
	{  68, 294 },
	{  70, 293 },
	{  73, 292 },
	{  76, 291 },
	{  78, 290 },
	{  79, 289 },
	{  81, 288 },
	{  82, 287 },
	{  83, 286 },
	{  84, 285 },
	{  85, 284 },
	{  86, 283 },
	{  88, 282 },
	{  89, 281 },
	{ 256, 280 },
};

static int au8522_mse2snr_lookup(struct mse2snr_tab *tab, int sz, int mse,
				 u16 *snr)
{
	int i, ret = -EINVAL;
	dprintk("%s()\n", __func__);

	for (i = 0; i < sz; i++) {
		if (mse < tab[i].val) {
			*snr = tab[i].data;
			ret = 0;
			break;
		}
	}
	dprintk("%s() snr=%d\n", __func__, *snr);
	return ret;
}

static int au8522_set_if(struct dvb_frontend *fe, enum au8522_if_freq if_freq)
{
	struct au8522_state *state = fe->demodulator_priv;
	u8 r0b5, r0b6, r0b7;
	char *ifmhz;

	switch (if_freq) {
	case AU8522_IF_3_25MHZ:
		ifmhz = "3.25";
		r0b5 = 0x00;
		r0b6 = 0x3d;
		r0b7 = 0xa0;
		break;
	case AU8522_IF_4MHZ:
		ifmhz = "4.00";
		r0b5 = 0x00;
		r0b6 = 0x4b;
		r0b7 = 0xd9;
		break;
	case AU8522_IF_6MHZ:
		ifmhz = "6.00";
		r0b5 = 0xfb;
		r0b6 = 0x8e;
		r0b7 = 0x39;
		break;
	default:
		dprintk("%s() IF Frequency not supported\n", __func__);
		return -EINVAL;
	}
	dprintk("%s() %s MHz\n", __func__, ifmhz);
	au8522_writereg(state, 0x80b5, r0b5);
	au8522_writereg(state, 0x80b6, r0b6);
	au8522_writereg(state, 0x80b7, r0b7);

	return 0;
}

/* VSB Modulation table */
static struct {
	u16 reg;
	u16 data;
} VSB_mod_tab[] = {
	{ 0x8090, 0x84 },
	{ 0x4092, 0x11 },
	{ 0x2005, 0x00 },
	{ 0x8091, 0x80 },
	{ 0x80a3, 0x0c },
	{ 0x80a4, 0xe8 },
	{ 0x8081, 0xc4 },
	{ 0x80a5, 0x40 },
	{ 0x80a7, 0x40 },
	{ 0x80a6, 0x67 },
	{ 0x8262, 0x20 },
	{ 0x821c, 0x30 },
	{ 0x80d8, 0x1a },
	{ 0x8227, 0xa0 },
	{ 0x8121, 0xff },
	{ 0x80a8, 0xf0 },
	{ 0x80a9, 0x05 },
	{ 0x80aa, 0x77 },
	{ 0x80ab, 0xf0 },
	{ 0x80ac, 0x05 },
	{ 0x80ad, 0x77 },
	{ 0x80ae, 0x41 },
	{ 0x80af, 0x66 },
	{ 0x821b, 0xcc },
	{ 0x821d, 0x80 },
	{ 0x80a4, 0xe8 },
	{ 0x8231, 0x13 },
};

/* QAM64 Modulation table */
static struct {
	u16 reg;
	u16 data;
} QAM64_mod_tab[] = {
	{ 0x00a3, 0x09 },
	{ 0x00a4, 0x00 },
	{ 0x0081, 0xc4 },
	{ 0x00a5, 0x40 },
	{ 0x00aa, 0x77 },
	{ 0x00ad, 0x77 },
	{ 0x00a6, 0x67 },
	{ 0x0262, 0x20 },
	{ 0x021c, 0x30 },
	{ 0x00b8, 0x3e },
	{ 0x00b9, 0xf0 },
	{ 0x00ba, 0x01 },
	{ 0x00bb, 0x18 },
	{ 0x00bc, 0x50 },
	{ 0x00bd, 0x00 },
	{ 0x00be, 0xea },
	{ 0x00bf, 0xef },
	{ 0x00c0, 0xfc },
	{ 0x00c1, 0xbd },
	{ 0x00c2, 0x1f },
	{ 0x00c3, 0xfc },
	{ 0x00c4, 0xdd },
	{ 0x00c5, 0xaf },
	{ 0x00c6, 0x00 },
	{ 0x00c7, 0x38 },
	{ 0x00c8, 0x30 },
	{ 0x00c9, 0x05 },
	{ 0x00ca, 0x4a },
	{ 0x00cb, 0xd0 },
	{ 0x00cc, 0x01 },
	{ 0x00cd, 0xd9 },
	{ 0x00ce, 0x6f },
	{ 0x00cf, 0xf9 },
	{ 0x00d0, 0x70 },
	{ 0x00d1, 0xdf },
	{ 0x00d2, 0xf7 },
	{ 0x00d3, 0xc2 },
	{ 0x00d4, 0xdf },
	{ 0x00d5, 0x02 },
	{ 0x00d6, 0x9a },
	{ 0x00d7, 0xd0 },
	{ 0x0250, 0x0d },
	{ 0x0251, 0xcd },
	{ 0x0252, 0xe0 },
	{ 0x0253, 0x05 },
	{ 0x0254, 0xa7 },
	{ 0x0255, 0xff },
	{ 0x0256, 0xed },
	{ 0x0257, 0x5b },
	{ 0x0258, 0xae },
	{ 0x0259, 0xe6 },
	{ 0x025a, 0x3d },
	{ 0x025b, 0x0f },
	{ 0x025c, 0x0d },
	{ 0x025d, 0xea },
	{ 0x025e, 0xf2 },
	{ 0x025f, 0x51 },
	{ 0x0260, 0xf5 },
	{ 0x0261, 0x06 },
	{ 0x021a, 0x00 },
	{ 0x0546, 0x40 },
	{ 0x0210, 0xc7 },
	{ 0x0211, 0xaa },
	{ 0x0212, 0xab },
	{ 0x0213, 0x02 },
	{ 0x0502, 0x00 },
	{ 0x0121, 0x04 },
	{ 0x0122, 0x04 },
	{ 0x052e, 0x10 },
	{ 0x00a4, 0xca },
	{ 0x00a7, 0x40 },
	{ 0x0526, 0x01 },
};

/* QAM256 Modulation table */
static struct {
	u16 reg;
	u16 data;
} QAM256_mod_tab[] = {
	{ 0x80a3, 0x09 },
	{ 0x80a4, 0x00 },
	{ 0x8081, 0xc4 },
	{ 0x80a5, 0x40 },
	{ 0x80aa, 0x77 },
	{ 0x80ad, 0x77 },
	{ 0x80a6, 0x67 },
	{ 0x8262, 0x20 },
	{ 0x821c, 0x30 },
	{ 0x80b8, 0x3e },
	{ 0x80b9, 0xf0 },
	{ 0x80ba, 0x01 },
	{ 0x80bb, 0x18 },
	{ 0x80bc, 0x50 },
	{ 0x80bd, 0x00 },
	{ 0x80be, 0xea },
	{ 0x80bf, 0xef },
	{ 0x80c0, 0xfc },
	{ 0x80c1, 0xbd },
	{ 0x80c2, 0x1f },
	{ 0x80c3, 0xfc },
	{ 0x80c4, 0xdd },
	{ 0x80c5, 0xaf },
	{ 0x80c6, 0x00 },
	{ 0x80c7, 0x38 },
	{ 0x80c8, 0x30 },
	{ 0x80c9, 0x05 },
	{ 0x80ca, 0x4a },
	{ 0x80cb, 0xd0 },
	{ 0x80cc, 0x01 },
	{ 0x80cd, 0xd9 },
	{ 0x80ce, 0x6f },
	{ 0x80cf, 0xf9 },
	{ 0x80d0, 0x70 },
	{ 0x80d1, 0xdf },
	{ 0x80d2, 0xf7 },
	{ 0x80d3, 0xc2 },
	{ 0x80d4, 0xdf },
	{ 0x80d5, 0x02 },
	{ 0x80d6, 0x9a },
	{ 0x80d7, 0xd0 },
	{ 0x8250, 0x0d },
	{ 0x8251, 0xcd },
	{ 0x8252, 0xe0 },
	{ 0x8253, 0x05 },
	{ 0x8254, 0xa7 },
	{ 0x8255, 0xff },
	{ 0x8256, 0xed },
	{ 0x8257, 0x5b },
	{ 0x8258, 0xae },
	{ 0x8259, 0xe6 },
	{ 0x825a, 0x3d },
	{ 0x825b, 0x0f },
	{ 0x825c, 0x0d },
	{ 0x825d, 0xea },
	{ 0x825e, 0xf2 },
	{ 0x825f, 0x51 },
	{ 0x8260, 0xf5 },
	{ 0x8261, 0x06 },
	{ 0x821a, 0x00 },
	{ 0x8546, 0x40 },
	{ 0x8210, 0x26 },
	{ 0x8211, 0xf6 },
	{ 0x8212, 0x84 },
	{ 0x8213, 0x02 },
	{ 0x8502, 0x01 },
	{ 0x8121, 0x04 },
	{ 0x8122, 0x04 },
	{ 0x852e, 0x10 },
	{ 0x80a4, 0xca },
	{ 0x80a7, 0x40 },
	{ 0x8526, 0x01 },
};

static int au8522_enable_modulation(struct dvb_frontend *fe,
				    fe_modulation_t m)
{
	struct au8522_state *state = fe->demodulator_priv;
	int i;

	dprintk("%s(0x%08x)\n", __func__, m);

	switch (m) {
	case VSB_8:
		dprintk("%s() VSB_8\n", __func__);
		for (i = 0; i < ARRAY_SIZE(VSB_mod_tab); i++)
			au8522_writereg(state,
				VSB_mod_tab[i].reg,
				VSB_mod_tab[i].data);
		au8522_set_if(fe, state->config->vsb_if);
		break;
	case QAM_64:
		dprintk("%s() QAM 64\n", __func__);
		for (i = 0; i < ARRAY_SIZE(QAM64_mod_tab); i++)
			au8522_writereg(state,
				QAM64_mod_tab[i].reg,
				QAM64_mod_tab[i].data);
		au8522_set_if(fe, state->config->qam_if);
		break;
	case QAM_256:
		dprintk("%s() QAM 256\n", __func__);
		for (i = 0; i < ARRAY_SIZE(QAM256_mod_tab); i++)
			au8522_writereg(state,
				QAM256_mod_tab[i].reg,
				QAM256_mod_tab[i].data);
		au8522_set_if(fe, state->config->qam_if);
		break;
	default:
		dprintk("%s() Invalid modulation\n", __func__);
		return -EINVAL;
	}

	state->current_modulation = m;

	return 0;
}

/* Talk to the demod, set the FEC, GUARD, QAM settings etc */
static int au8522_set_frontend(struct dvb_frontend *fe,
			       struct dvb_frontend_parameters *p)
{
	struct au8522_state *state = fe->demodulator_priv;
	int ret = -EINVAL;

	dprintk("%s(frequency=%d)\n", __func__, p->frequency);

	if ((state->current_frequency == p->frequency) &&
	    (state->current_modulation == p->u.vsb.modulation))
		return 0;

	au8522_enable_modulation(fe, p->u.vsb.modulation);

	/* Allow the demod to settle */
	msleep(100);

	if (fe->ops.tuner_ops.set_params) {
		if (fe->ops.i2c_gate_ctrl)
			fe->ops.i2c_gate_ctrl(fe, 1);
		ret = fe->ops.tuner_ops.set_params(fe, p);
		if (fe->ops.i2c_gate_ctrl)
			fe->ops.i2c_gate_ctrl(fe, 0);
	}

	if (ret < 0)
		return ret;

	state->current_frequency = p->frequency;

	return 0;
}

/* Reset the demod hardware and reset all of the configuration registers
   to a default state. */
int au8522_init(struct dvb_frontend *fe)
{
	struct au8522_state *state = fe->demodulator_priv;
	dprintk("%s()\n", __func__);

	au8522_writereg(state, 0xa4, 1 << 5);

	au8522_i2c_gate_ctrl(fe, 1);

	return 0;
}

static int au8522_led_gpio_enable(struct au8522_state *state, int onoff)
{
	struct au8522_led_config *led_config = state->config->led_cfg;
	u8 val;

	/* bail out if we cant control an LED */
	if (!led_config || !led_config->gpio_output ||
	    !led_config->gpio_output_enable || !led_config->gpio_output_disable)
		return 0;

	val = au8522_readreg(state, 0x4000 |
			     (led_config->gpio_output & ~0xc000));
	if (onoff) {
		/* enable GPIO output */
		val &= ~((led_config->gpio_output_enable >> 8) & 0xff);
		val |=  (led_config->gpio_output_enable & 0xff);
	} else {
		/* disable GPIO output */
		val &= ~((led_config->gpio_output_disable >> 8) & 0xff);
		val |=  (led_config->gpio_output_disable & 0xff);
	}
	return au8522_writereg(state, 0x8000 |
			       (led_config->gpio_output & ~0xc000), val);
}

/* led = 0 | off
 * led = 1 | signal ok
 * led = 2 | signal strong
 * led < 0 | only light led if leds are currently off
 */
static int au8522_led_ctrl(struct au8522_state *state, int led)
{
	struct au8522_led_config *led_config = state->config->led_cfg;
	int i, ret = 0;

	/* bail out if we cant control an LED */
	if (!led_config || !led_config->gpio_leds ||
	    !led_config->num_led_states || !led_config->led_states)
		return 0;

	if (led < 0) {
		/* if LED is already lit, then leave it as-is */
		if (state->led_state)
			return 0;
		else
			led *= -1;
	}

	/* toggle LED if changing state */
	if (state->led_state != led) {
		u8 val;

		dprintk("%s: %d\n", __func__, led);

		au8522_led_gpio_enable(state, 1);

		val = au8522_readreg(state, 0x4000 |
				     (led_config->gpio_leds & ~0xc000));

		/* start with all leds off */
		for (i = 0; i < led_config->num_led_states; i++)
			val &= ~led_config->led_states[i];

		/* set selected LED state */
		if (led < led_config->num_led_states)
			val |= led_config->led_states[led];
		else if (led_config->num_led_states)
			val |=
			led_config->led_states[led_config->num_led_states - 1];

		ret = au8522_writereg(state, 0x8000 |
				      (led_config->gpio_leds & ~0xc000), val);
		if (ret < 0)
			return ret;

		state->led_state = led;

		if (led == 0)
			au8522_led_gpio_enable(state, 0);
	}

	return 0;
}

int au8522_sleep(struct dvb_frontend *fe)
{
	struct au8522_state *state = fe->demodulator_priv;
	dprintk("%s()\n", __func__);

	/* turn off led */
	au8522_led_ctrl(state, 0);

	/* Power down the chip */
	au8522_writereg(state, 0xa4, 1 << 5);

	state->current_frequency = 0;

	return 0;
}

static int au8522_read_status(struct dvb_frontend *fe, fe_status_t *status)
{
	struct au8522_state *state = fe->demodulator_priv;
	u8 reg;
	u32 tuner_status = 0;

	*status = 0;

	if (state->current_modulation == VSB_8) {
		dprintk("%s() Checking VSB_8\n", __func__);
		reg = au8522_readreg(state, 0x4088);
		if ((reg & 0x03) == 0x03)
			*status |= FE_HAS_LOCK | FE_HAS_SYNC | FE_HAS_VITERBI;
	} else {
		dprintk("%s() Checking QAM\n", __func__);
		reg = au8522_readreg(state, 0x4541);
		if (reg & 0x80)
			*status |= FE_HAS_VITERBI;
		if (reg & 0x20)
			*status |= FE_HAS_LOCK | FE_HAS_SYNC;
	}

	switch (state->config->status_mode) {
	case AU8522_DEMODLOCKING:
		dprintk("%s() DEMODLOCKING\n", __func__);
		if (*status & FE_HAS_VITERBI)
			*status |= FE_HAS_CARRIER | FE_HAS_SIGNAL;
		break;
	case AU8522_TUNERLOCKING:
		/* Get the tuner status */
		dprintk("%s() TUNERLOCKING\n", __func__);
		if (fe->ops.tuner_ops.get_status) {
			if (fe->ops.i2c_gate_ctrl)
				fe->ops.i2c_gate_ctrl(fe, 1);

			fe->ops.tuner_ops.get_status(fe, &tuner_status);

			if (fe->ops.i2c_gate_ctrl)
				fe->ops.i2c_gate_ctrl(fe, 0);
		}
		if (tuner_status)
			*status |= FE_HAS_CARRIER | FE_HAS_SIGNAL;
		break;
	}
	state->fe_status = *status;

	if (*status & FE_HAS_LOCK)
		/* turn on LED, if it isn't on already */
		au8522_led_ctrl(state, -1);
	else
		/* turn off LED */
		au8522_led_ctrl(state, 0);

	dprintk("%s() status 0x%08x\n", __func__, *status);

	return 0;
}

static int au8522_led_status(struct au8522_state *state, const u16 *snr)
{
	struct au8522_led_config *led_config = state->config->led_cfg;
	int led;
	u16 strong;

	/* bail out if we cant control an LED */
	if (!led_config)
		return 0;

	if (0 == (state->fe_status & FE_HAS_LOCK))
		return au8522_led_ctrl(state, 0);
	else if (state->current_modulation == QAM_256)
		strong = led_config->qam256_strong;
	else if (state->current_modulation == QAM_64)
		strong = led_config->qam64_strong;
	else /* (state->current_modulation == VSB_8) */
		strong = led_config->vsb8_strong;

	if (*snr >= strong)
		led = 2;
	else
		led = 1;

	if ((state->led_state) &&
	    (((strong < *snr) ? (*snr - strong) : (strong - *snr)) <= 10))
		/* snr didn't change enough to bother
		 * changing the color of the led */
		return 0;

	return au8522_led_ctrl(state, led);
}

static int au8522_read_snr(struct dvb_frontend *fe, u16 *snr)
{
	struct au8522_state *state = fe->demodulator_priv;
	int ret = -EINVAL;

	dprintk("%s()\n", __func__);

	if (state->current_modulation == QAM_256)
		ret = au8522_mse2snr_lookup(qam256_mse2snr_tab,
					    ARRAY_SIZE(qam256_mse2snr_tab),
					    au8522_readreg(state, 0x4522),
					    snr);
	else if (state->current_modulation == QAM_64)
		ret = au8522_mse2snr_lookup(qam64_mse2snr_tab,
					    ARRAY_SIZE(qam64_mse2snr_tab),
					    au8522_readreg(state, 0x4522),
					    snr);
	else /* VSB_8 */
		ret = au8522_mse2snr_lookup(vsb_mse2snr_tab,
					    ARRAY_SIZE(vsb_mse2snr_tab),
					    au8522_readreg(state, 0x4311),
					    snr);

	if (state->config->led_cfg)
		au8522_led_status(state, snr);

	return ret;
}

static int au8522_read_signal_strength(struct dvb_frontend *fe,
				       u16 *signal_strength)
{
	return au8522_read_snr(fe, signal_strength);
}

static int au8522_read_ucblocks(struct dvb_frontend *fe, u32 *ucblocks)
{
	struct au8522_state *state = fe->demodulator_priv;

	if (state->current_modulation == VSB_8)
		*ucblocks = au8522_readreg(state, 0x4087);
	else
		*ucblocks = au8522_readreg(state, 0x4543);

	return 0;
}

static int au8522_read_ber(struct dvb_frontend *fe, u32 *ber)
{
	return au8522_read_ucblocks(fe, ber);
}

static int au8522_get_frontend(struct dvb_frontend *fe,
				struct dvb_frontend_parameters *p)
{
	struct au8522_state *state = fe->demodulator_priv;

	p->frequency = state->current_frequency;
	p->u.vsb.modulation = state->current_modulation;

	return 0;
}

static int au8522_get_tune_settings(struct dvb_frontend *fe,
				    struct dvb_frontend_tune_settings *tune)
{
	tune->min_delay_ms = 1000;
	return 0;
}

static struct dvb_frontend_ops au8522_ops;

int au8522_get_state(struct au8522_state **state, struct i2c_adapter *i2c,
		     u8 client_address)
{
	int ret;

	mutex_lock(&au8522_list_mutex);
	ret = hybrid_tuner_request_state(struct au8522_state, (*state),
					 hybrid_tuner_instance_list,
					 i2c, client_address, "au8522");
	mutex_unlock(&au8522_list_mutex);

	return ret;
}

void au8522_release_state(struct au8522_state *state)
{
	mutex_lock(&au8522_list_mutex);
	if (state != NULL)
		hybrid_tuner_release_state(state);
	mutex_unlock(&au8522_list_mutex);
}


static void au8522_release(struct dvb_frontend *fe)
{
	struct au8522_state *state = fe->demodulator_priv;
	au8522_release_state(state);
}

struct dvb_frontend *au8522_attach(const struct au8522_config *config,
				   struct i2c_adapter *i2c)
{
	struct au8522_state *state = NULL;
	int instance;

	/* allocate memory for the internal state */
	instance = au8522_get_state(&state, i2c, config->demod_address);
	switch (instance) {
	case 0:
		dprintk("%s state allocation failed\n", __func__);
		break;
	case 1:
		/* new demod instance */
		dprintk("%s using new instance\n", __func__);
		break;
	default:
		/* existing demod instance */
		dprintk("%s using existing instance\n", __func__);
		break;
	}

	/* setup the state */
	state->config = config;
	state->i2c = i2c;
	/* create dvb_frontend */
	memcpy(&state->frontend.ops, &au8522_ops,
	       sizeof(struct dvb_frontend_ops));
	state->frontend.demodulator_priv = state;

	if (au8522_init(&state->frontend) != 0) {
		printk(KERN_ERR "%s: Failed to initialize correctly\n",
			__func__);
		goto error;
	}

	/* Note: Leaving the I2C gate open here. */
	au8522_i2c_gate_ctrl(&state->frontend, 1);

	return &state->frontend;

error:
	au8522_release_state(state);
	return NULL;
}
EXPORT_SYMBOL(au8522_attach);

static struct dvb_frontend_ops au8522_ops = {

	.info = {
		.name			= "Auvitek AU8522 QAM/8VSB Frontend",
		.type			= FE_ATSC,
		.frequency_min		= 54000000,
		.frequency_max		= 858000000,
		.frequency_stepsize	= 62500,
		.caps = FE_CAN_QAM_64 | FE_CAN_QAM_256 | FE_CAN_8VSB
	},

	.init                 = au8522_init,
	.sleep                = au8522_sleep,
	.i2c_gate_ctrl        = au8522_i2c_gate_ctrl,
	.set_frontend         = au8522_set_frontend,
	.get_frontend         = au8522_get_frontend,
	.get_tune_settings    = au8522_get_tune_settings,
	.read_status          = au8522_read_status,
	.read_ber             = au8522_read_ber,
	.read_signal_strength = au8522_read_signal_strength,
	.read_snr             = au8522_read_snr,
	.read_ucblocks        = au8522_read_ucblocks,
	.release              = au8522_release,
};

module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "Enable verbose debug messages");

MODULE_DESCRIPTION("Auvitek AU8522 QAM-B/ATSC Demodulator driver");
MODULE_AUTHOR("Steven Toth");
MODULE_LICENSE("GPL");

/*

   i2c tv tuner chip device driver
   controls the philips tda8290+75 tuner chip combo.

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

   This "tda8290" module was split apart from the original "tuner" module.
*/

#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/videodev.h>
#include "tuner-i2c.h"
#include "tda8290.h"
#include "tda827x.h"
#include "tda18271.h"

static int debug;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "enable verbose debug messages");

/* ---------------------------------------------------------------------- */

struct tda8290_priv {
	struct tuner_i2c_props i2c_props;

	unsigned char tda8290_easy_mode;

	unsigned char tda827x_addr;

	unsigned char ver;
#define TDA8290   1
#define TDA8295   2
#define TDA8275   4
#define TDA8275A  8
#define TDA18271 16

	struct tda827x_config cfg;
};

/*---------------------------------------------------------------------*/

static int tda8290_i2c_bridge(struct dvb_frontend *fe, int close)
{
	struct tda8290_priv *priv = fe->analog_demod_priv;

	unsigned char  enable[2] = { 0x21, 0xC0 };
	unsigned char disable[2] = { 0x21, 0x00 };
	unsigned char *msg;

	if (close) {
		msg = enable;
		tuner_i2c_xfer_send(&priv->i2c_props, msg, 2);
		/* let the bridge stabilize */
		msleep(20);
	} else {
		msg = disable;
		tuner_i2c_xfer_send(&priv->i2c_props, msg, 2);
	}

	return 0;
}

static int tda8295_i2c_bridge(struct dvb_frontend *fe, int close)
{
	struct tda8290_priv *priv = fe->analog_demod_priv;

	unsigned char  enable[2] = { 0x45, 0xc1 };
	unsigned char disable[2] = { 0x46, 0x00 };
	unsigned char buf[3] = { 0x45, 0x01, 0x00 };
	unsigned char *msg;

	if (close) {
		msg = enable;
		tuner_i2c_xfer_send(&priv->i2c_props, msg, 2);
		/* let the bridge stabilize */
		msleep(20);
	} else {
		msg = disable;
		tuner_i2c_xfer_send(&priv->i2c_props, msg, 1);
		tuner_i2c_xfer_recv(&priv->i2c_props, &msg[1], 1);

		buf[2] = msg[1];
		buf[2] &= ~0x04;
		tuner_i2c_xfer_send(&priv->i2c_props, buf, 3);
		msleep(5);

		msg[1] |= 0x04;
		tuner_i2c_xfer_send(&priv->i2c_props, msg, 2);
	}

	return 0;
}

/*---------------------------------------------------------------------*/

static void set_audio(struct dvb_frontend *fe,
		      struct analog_parameters *params)
{
	struct tda8290_priv *priv = fe->analog_demod_priv;
	char* mode;

	if (params->std & V4L2_STD_MN) {
		priv->tda8290_easy_mode = 0x01;
		mode = "MN";
	} else if (params->std & V4L2_STD_B) {
		priv->tda8290_easy_mode = 0x02;
		mode = "B";
	} else if (params->std & V4L2_STD_GH) {
		priv->tda8290_easy_mode = 0x04;
		mode = "GH";
	} else if (params->std & V4L2_STD_PAL_I) {
		priv->tda8290_easy_mode = 0x08;
		mode = "I";
	} else if (params->std & V4L2_STD_DK) {
		priv->tda8290_easy_mode = 0x10;
		mode = "DK";
	} else if (params->std & V4L2_STD_SECAM_L) {
		priv->tda8290_easy_mode = 0x20;
		mode = "L";
	} else if (params->std & V4L2_STD_SECAM_LC) {
		priv->tda8290_easy_mode = 0x40;
		mode = "LC";
	} else {
		priv->tda8290_easy_mode = 0x10;
		mode = "xx";
	}

	tuner_dbg("setting tda829x to system %s\n", mode);
}

static void tda8290_set_params(struct dvb_frontend *fe,
			       struct analog_parameters *params)
{
	struct tda8290_priv *priv = fe->analog_demod_priv;

	unsigned char soft_reset[]  = { 0x00, 0x00 };
	unsigned char easy_mode[]   = { 0x01, priv->tda8290_easy_mode };
	unsigned char expert_mode[] = { 0x01, 0x80 };
	unsigned char agc_out_on[]  = { 0x02, 0x00 };
	unsigned char gainset_off[] = { 0x28, 0x14 };
	unsigned char if_agc_spd[]  = { 0x0f, 0x88 };
	unsigned char adc_head_6[]  = { 0x05, 0x04 };
	unsigned char adc_head_9[]  = { 0x05, 0x02 };
	unsigned char adc_head_12[] = { 0x05, 0x01 };
	unsigned char pll_bw_nom[]  = { 0x0d, 0x47 };
	unsigned char pll_bw_low[]  = { 0x0d, 0x27 };
	unsigned char gainset_2[]   = { 0x28, 0x64 };
	unsigned char agc_rst_on[]  = { 0x0e, 0x0b };
	unsigned char agc_rst_off[] = { 0x0e, 0x09 };
	unsigned char if_agc_set[]  = { 0x0f, 0x81 };
	unsigned char addr_adc_sat  = 0x1a;
	unsigned char addr_agc_stat = 0x1d;
	unsigned char addr_pll_stat = 0x1b;
	unsigned char adc_sat, agc_stat,
		      pll_stat;
	int i;

	set_audio(fe, params);

	if (priv->cfg.config)
		tuner_dbg("tda827xa config is 0x%02x\n", priv->cfg.config);
	tuner_i2c_xfer_send(&priv->i2c_props, easy_mode, 2);
	tuner_i2c_xfer_send(&priv->i2c_props, agc_out_on, 2);
	tuner_i2c_xfer_send(&priv->i2c_props, soft_reset, 2);
	msleep(1);

	expert_mode[1] = priv->tda8290_easy_mode + 0x80;
	tuner_i2c_xfer_send(&priv->i2c_props, expert_mode, 2);
	tuner_i2c_xfer_send(&priv->i2c_props, gainset_off, 2);
	tuner_i2c_xfer_send(&priv->i2c_props, if_agc_spd, 2);
	if (priv->tda8290_easy_mode & 0x60)
		tuner_i2c_xfer_send(&priv->i2c_props, adc_head_9, 2);
	else
		tuner_i2c_xfer_send(&priv->i2c_props, adc_head_6, 2);
	tuner_i2c_xfer_send(&priv->i2c_props, pll_bw_nom, 2);

	tda8290_i2c_bridge(fe, 1);

	if (fe->ops.tuner_ops.set_analog_params)
		fe->ops.tuner_ops.set_analog_params(fe, params);

	for (i = 0; i < 3; i++) {
		tuner_i2c_xfer_send(&priv->i2c_props, &addr_pll_stat, 1);
		tuner_i2c_xfer_recv(&priv->i2c_props, &pll_stat, 1);
		if (pll_stat & 0x80) {
			tuner_i2c_xfer_send(&priv->i2c_props, &addr_adc_sat, 1);
			tuner_i2c_xfer_recv(&priv->i2c_props, &adc_sat, 1);
			tuner_i2c_xfer_send(&priv->i2c_props, &addr_agc_stat, 1);
			tuner_i2c_xfer_recv(&priv->i2c_props, &agc_stat, 1);
			tuner_dbg("tda8290 is locked, AGC: %d\n", agc_stat);
			break;
		} else {
			tuner_dbg("tda8290 not locked, no signal?\n");
			msleep(100);
		}
	}
	/* adjust headroom resp. gain */
	if ((agc_stat > 115) || (!(pll_stat & 0x80) && (adc_sat < 20))) {
		tuner_dbg("adjust gain, step 1. Agc: %d, ADC stat: %d, lock: %d\n",
			   agc_stat, adc_sat, pll_stat & 0x80);
		tuner_i2c_xfer_send(&priv->i2c_props, gainset_2, 2);
		msleep(100);
		tuner_i2c_xfer_send(&priv->i2c_props, &addr_agc_stat, 1);
		tuner_i2c_xfer_recv(&priv->i2c_props, &agc_stat, 1);
		tuner_i2c_xfer_send(&priv->i2c_props, &addr_pll_stat, 1);
		tuner_i2c_xfer_recv(&priv->i2c_props, &pll_stat, 1);
		if ((agc_stat > 115) || !(pll_stat & 0x80)) {
			tuner_dbg("adjust gain, step 2. Agc: %d, lock: %d\n",
				   agc_stat, pll_stat & 0x80);
			if (priv->cfg.agcf)
				priv->cfg.agcf(fe);
			msleep(100);
			tuner_i2c_xfer_send(&priv->i2c_props, &addr_agc_stat, 1);
			tuner_i2c_xfer_recv(&priv->i2c_props, &agc_stat, 1);
			tuner_i2c_xfer_send(&priv->i2c_props, &addr_pll_stat, 1);
			tuner_i2c_xfer_recv(&priv->i2c_props, &pll_stat, 1);
			if((agc_stat > 115) || !(pll_stat & 0x80)) {
				tuner_dbg("adjust gain, step 3. Agc: %d\n", agc_stat);
				tuner_i2c_xfer_send(&priv->i2c_props, adc_head_12, 2);
				tuner_i2c_xfer_send(&priv->i2c_props, pll_bw_low, 2);
				msleep(100);
			}
		}
	}

	/* l/ l' deadlock? */
	if(priv->tda8290_easy_mode & 0x60) {
		tuner_i2c_xfer_send(&priv->i2c_props, &addr_adc_sat, 1);
		tuner_i2c_xfer_recv(&priv->i2c_props, &adc_sat, 1);
		tuner_i2c_xfer_send(&priv->i2c_props, &addr_pll_stat, 1);
		tuner_i2c_xfer_recv(&priv->i2c_props, &pll_stat, 1);
		if ((adc_sat > 20) || !(pll_stat & 0x80)) {
			tuner_dbg("trying to resolve SECAM L deadlock\n");
			tuner_i2c_xfer_send(&priv->i2c_props, agc_rst_on, 2);
			msleep(40);
			tuner_i2c_xfer_send(&priv->i2c_props, agc_rst_off, 2);
		}
	}

	tda8290_i2c_bridge(fe, 0);
	tuner_i2c_xfer_send(&priv->i2c_props, if_agc_set, 2);
}

/*---------------------------------------------------------------------*/

static void tda8295_power(struct dvb_frontend *fe, int enable)
{
	struct tda8290_priv *priv = fe->analog_demod_priv;
	unsigned char buf[] = { 0x30, 0x00 }; /* clb_stdbt */

	tuner_i2c_xfer_send(&priv->i2c_props, &buf[0], 1);
	tuner_i2c_xfer_recv(&priv->i2c_props, &buf[1], 1);

	if (enable)
		buf[1] = 0x01;
	else
		buf[1] = 0x03;

	tuner_i2c_xfer_send(&priv->i2c_props, buf, 2);
}

static void tda8295_set_easy_mode(struct dvb_frontend *fe, int enable)
{
	struct tda8290_priv *priv = fe->analog_demod_priv;
	unsigned char buf[] = { 0x01, 0x00 };

	tuner_i2c_xfer_send(&priv->i2c_props, &buf[0], 1);
	tuner_i2c_xfer_recv(&priv->i2c_props, &buf[1], 1);

	if (enable)
		buf[1] = 0x01; /* rising edge sets regs 0x02 - 0x23 */
	else
		buf[1] = 0x00; /* reset active bit */

	tuner_i2c_xfer_send(&priv->i2c_props, buf, 2);
}

static void tda8295_set_video_std(struct dvb_frontend *fe)
{
	struct tda8290_priv *priv = fe->analog_demod_priv;
	unsigned char buf[] = { 0x00, priv->tda8290_easy_mode };

	tuner_i2c_xfer_send(&priv->i2c_props, buf, 2);

	tda8295_set_easy_mode(fe, 1);
	msleep(20);
	tda8295_set_easy_mode(fe, 0);
}

/*---------------------------------------------------------------------*/

static void tda8295_agc1_out(struct dvb_frontend *fe, int enable)
{
	struct tda8290_priv *priv = fe->analog_demod_priv;
	unsigned char buf[] = { 0x02, 0x00 }; /* DIV_FUNC */

	tuner_i2c_xfer_send(&priv->i2c_props, &buf[0], 1);
	tuner_i2c_xfer_recv(&priv->i2c_props, &buf[1], 1);

	if (enable)
		buf[1] &= ~0x40;
	else
		buf[1] |= 0x40;

	tuner_i2c_xfer_send(&priv->i2c_props, buf, 2);
}

static void tda8295_agc2_out(struct dvb_frontend *fe, int enable)
{
	struct tda8290_priv *priv = fe->analog_demod_priv;
	unsigned char set_gpio_cf[]    = { 0x44, 0x00 };
	unsigned char set_gpio_val[]   = { 0x46, 0x00 };

	tuner_i2c_xfer_send(&priv->i2c_props, &set_gpio_cf[0], 1);
	tuner_i2c_xfer_recv(&priv->i2c_props, &set_gpio_cf[1], 1);
	tuner_i2c_xfer_send(&priv->i2c_props, &set_gpio_val[0], 1);
	tuner_i2c_xfer_recv(&priv->i2c_props, &set_gpio_val[1], 1);

	set_gpio_cf[1] &= 0xf0; /* clear GPIO_0 bits 3-0 */

	if (enable) {
		set_gpio_cf[1]  |= 0x01; /* config GPIO_0 as Open Drain Out */
		set_gpio_val[1] &= 0xfe; /* set GPIO_0 pin low */
	}
	tuner_i2c_xfer_send(&priv->i2c_props, set_gpio_cf, 2);
	tuner_i2c_xfer_send(&priv->i2c_props, set_gpio_val, 2);
}

static int tda8295_has_signal(struct dvb_frontend *fe)
{
	struct tda8290_priv *priv = fe->analog_demod_priv;

	unsigned char hvpll_stat = 0x26;
	unsigned char ret;

	tuner_i2c_xfer_send(&priv->i2c_props, &hvpll_stat, 1);
	tuner_i2c_xfer_recv(&priv->i2c_props, &ret, 1);
	return (ret & 0x01) ? 65535 : 0;
}

/*---------------------------------------------------------------------*/

static void tda8295_set_params(struct dvb_frontend *fe,
			       struct analog_parameters *params)
{
	struct tda8290_priv *priv = fe->analog_demod_priv;

	unsigned char blanking_mode[]     = { 0x1d, 0x00 };

	set_audio(fe, params);

	tuner_dbg("%s: freq = %d\n", __func__, params->frequency);

	tda8295_power(fe, 1);
	tda8295_agc1_out(fe, 1);

	tuner_i2c_xfer_send(&priv->i2c_props, &blanking_mode[0], 1);
	tuner_i2c_xfer_recv(&priv->i2c_props, &blanking_mode[1], 1);

	tda8295_set_video_std(fe);

	blanking_mode[1] = 0x03;
	tuner_i2c_xfer_send(&priv->i2c_props, blanking_mode, 2);
	msleep(20);

	tda8295_i2c_bridge(fe, 1);

	if (fe->ops.tuner_ops.set_analog_params)
		fe->ops.tuner_ops.set_analog_params(fe, params);

	if (priv->cfg.agcf)
		priv->cfg.agcf(fe);

	if (tda8295_has_signal(fe))
		tuner_dbg("tda8295 is locked\n");
	else
		tuner_dbg("tda8295 not locked, no signal?\n");

	tda8295_i2c_bridge(fe, 0);
}

/*---------------------------------------------------------------------*/

static int tda8290_has_signal(struct dvb_frontend *fe)
{
	struct tda8290_priv *priv = fe->analog_demod_priv;

	unsigned char i2c_get_afc[1] = { 0x1B };
	unsigned char afc = 0;

	tuner_i2c_xfer_send(&priv->i2c_props, i2c_get_afc, ARRAY_SIZE(i2c_get_afc));
	tuner_i2c_xfer_recv(&priv->i2c_props, &afc, 1);
	return (afc & 0x80)? 65535:0;
}

/*---------------------------------------------------------------------*/

static void tda8290_standby(struct dvb_frontend *fe)
{
	struct tda8290_priv *priv = fe->analog_demod_priv;

	unsigned char cb1[] = { 0x30, 0xD0 };
	unsigned char tda8290_standby[] = { 0x00, 0x02 };
	unsigned char tda8290_agc_tri[] = { 0x02, 0x20 };
	struct i2c_msg msg = {.addr = priv->tda827x_addr, .flags=0, .buf=cb1, .len = 2};

	tda8290_i2c_bridge(fe, 1);
	if (priv->ver & TDA8275A)
		cb1[1] = 0x90;
	i2c_transfer(priv->i2c_props.adap, &msg, 1);
	tda8290_i2c_bridge(fe, 0);
	tuner_i2c_xfer_send(&priv->i2c_props, tda8290_agc_tri, 2);
	tuner_i2c_xfer_send(&priv->i2c_props, tda8290_standby, 2);
}

static void tda8295_standby(struct dvb_frontend *fe)
{
	tda8295_agc1_out(fe, 0); /* Put AGC in tri-state */

	tda8295_power(fe, 0);
}

static void tda8290_init_if(struct dvb_frontend *fe)
{
	struct tda8290_priv *priv = fe->analog_demod_priv;

	unsigned char set_VS[] = { 0x30, 0x6F };
	unsigned char set_GP00_CF[] = { 0x20, 0x01 };
	unsigned char set_GP01_CF[] = { 0x20, 0x0B };

	if ((priv->cfg.config == 1) || (priv->cfg.config == 2))
		tuner_i2c_xfer_send(&priv->i2c_props, set_GP00_CF, 2);
	else
		tuner_i2c_xfer_send(&priv->i2c_props, set_GP01_CF, 2);
	tuner_i2c_xfer_send(&priv->i2c_props, set_VS, 2);
}

static void tda8295_init_if(struct dvb_frontend *fe)
{
	struct tda8290_priv *priv = fe->analog_demod_priv;

	static unsigned char set_adc_ctl[]       = { 0x33, 0x14 };
	static unsigned char set_adc_ctl2[]      = { 0x34, 0x00 };
	static unsigned char set_pll_reg6[]      = { 0x3e, 0x63 };
	static unsigned char set_pll_reg0[]      = { 0x38, 0x23 };
	static unsigned char set_pll_reg7[]      = { 0x3f, 0x01 };
	static unsigned char set_pll_reg10[]     = { 0x42, 0x61 };
	static unsigned char set_gpio_reg0[]     = { 0x44, 0x0b };

	tda8295_power(fe, 1);

	tda8295_set_easy_mode(fe, 0);
	tda8295_set_video_std(fe);

	tuner_i2c_xfer_send(&priv->i2c_props, set_adc_ctl, 2);
	tuner_i2c_xfer_send(&priv->i2c_props, set_adc_ctl2, 2);
	tuner_i2c_xfer_send(&priv->i2c_props, set_pll_reg6, 2);
	tuner_i2c_xfer_send(&priv->i2c_props, set_pll_reg0, 2);
	tuner_i2c_xfer_send(&priv->i2c_props, set_pll_reg7, 2);
	tuner_i2c_xfer_send(&priv->i2c_props, set_pll_reg10, 2);
	tuner_i2c_xfer_send(&priv->i2c_props, set_gpio_reg0, 2);

	tda8295_agc1_out(fe, 0);
	tda8295_agc2_out(fe, 0);
}

static void tda8290_init_tuner(struct dvb_frontend *fe)
{
	struct tda8290_priv *priv = fe->analog_demod_priv;
	unsigned char tda8275_init[]  = { 0x00, 0x00, 0x00, 0x40, 0xdC, 0x04, 0xAf,
					  0x3F, 0x2A, 0x04, 0xFF, 0x00, 0x00, 0x40 };
	unsigned char tda8275a_init[] = { 0x00, 0x00, 0x00, 0x00, 0xdC, 0x05, 0x8b,
					  0x0c, 0x04, 0x20, 0xFF, 0x00, 0x00, 0x4b };
	struct i2c_msg msg = {.addr = priv->tda827x_addr, .flags=0,
			      .buf=tda8275_init, .len = 14};
	if (priv->ver & TDA8275A)
		msg.buf = tda8275a_init;

	tda8290_i2c_bridge(fe, 1);
	i2c_transfer(priv->i2c_props.adap, &msg, 1);
	tda8290_i2c_bridge(fe, 0);
}

/*---------------------------------------------------------------------*/

static void tda829x_release(struct dvb_frontend *fe)
{
	struct tda8290_priv *priv = fe->analog_demod_priv;

	/* only try to release the tuner if we've
	 * attached it from within this module */
	if (priv->ver & (TDA18271 | TDA8275 | TDA8275A))
		if (fe->ops.tuner_ops.release)
			fe->ops.tuner_ops.release(fe);

	kfree(fe->analog_demod_priv);
	fe->analog_demod_priv = NULL;
}

static struct tda18271_config tda829x_tda18271_config = {
	.gate    = TDA18271_GATE_ANALOG,
};

static int tda829x_find_tuner(struct dvb_frontend *fe)
{
	struct tda8290_priv *priv = fe->analog_demod_priv;
	struct analog_demod_ops *analog_ops = &fe->ops.analog_ops;
	int i, ret, tuners_found;
	u32 tuner_addrs;
	u8 data;
	struct i2c_msg msg = { .flags = I2C_M_RD, .buf = &data, .len = 1 };

	if (NULL == analog_ops->i2c_gate_ctrl)
		return -EINVAL;

	analog_ops->i2c_gate_ctrl(fe, 1);

	/* probe for tuner chip */
	tuners_found = 0;
	tuner_addrs = 0;
	for (i = 0x60; i <= 0x63; i++) {
		msg.addr = i;
		ret = i2c_transfer(priv->i2c_props.adap, &msg, 1);
		if (ret == 1) {
			tuners_found++;
			tuner_addrs = (tuner_addrs << 8) + i;
		}
	}
	/* if there is more than one tuner, we expect the right one is
	   behind the bridge and we choose the highest address that doesn't
	   give a response now
	 */

	analog_ops->i2c_gate_ctrl(fe, 0);

	if (tuners_found > 1)
		for (i = 0; i < tuners_found; i++) {
			msg.addr = tuner_addrs  & 0xff;
			ret = i2c_transfer(priv->i2c_props.adap, &msg, 1);
			if (ret == 1)
				tuner_addrs = tuner_addrs >> 8;
			else
				break;
		}

	if (tuner_addrs == 0) {
		tuner_addrs = 0x60;
		tuner_info("could not clearly identify tuner address, "
			   "defaulting to %x\n", tuner_addrs);
	} else {
		tuner_addrs = tuner_addrs & 0xff;
		tuner_info("setting tuner address to %x\n", tuner_addrs);
	}
	priv->tda827x_addr = tuner_addrs;
	msg.addr = tuner_addrs;

	analog_ops->i2c_gate_ctrl(fe, 1);
	ret = i2c_transfer(priv->i2c_props.adap, &msg, 1);

	if (ret != 1) {
		tuner_warn("tuner access failed!\n");
		return -EREMOTEIO;
	}

	if ((data == 0x83) || (data == 0x84)) {
		priv->ver |= TDA18271;
		tda18271_attach(fe, priv->tda827x_addr,
				priv->i2c_props.adap,
				&tda829x_tda18271_config);
	} else {
		if ((data & 0x3c) == 0)
			priv->ver |= TDA8275;
		else
			priv->ver |= TDA8275A;

		tda827x_attach(fe, priv->tda827x_addr, priv->i2c_props.adap, &priv->cfg);
		priv->cfg.switch_addr = priv->i2c_props.addr;
	}
	if (fe->ops.tuner_ops.init)
		fe->ops.tuner_ops.init(fe);

	if (fe->ops.tuner_ops.sleep)
		fe->ops.tuner_ops.sleep(fe);

	analog_ops->i2c_gate_ctrl(fe, 0);

	return 0;
}

static int tda8290_probe(struct tuner_i2c_props *i2c_props)
{
#define TDA8290_ID 0x89
	unsigned char tda8290_id[] = { 0x1f, 0x00 };

	/* detect tda8290 */
	tuner_i2c_xfer_send(i2c_props, &tda8290_id[0], 1);
	tuner_i2c_xfer_recv(i2c_props, &tda8290_id[1], 1);

	if (tda8290_id[1] == TDA8290_ID) {
		if (debug)
			printk(KERN_DEBUG "%s: tda8290 detected @ %d-%04x\n",
			       __func__, i2c_adapter_id(i2c_props->adap),
			       i2c_props->addr);
		return 0;
	}

	return -ENODEV;
}

static int tda8295_probe(struct tuner_i2c_props *i2c_props)
{
#define TDA8295_ID 0x8a
	unsigned char tda8295_id[] = { 0x2f, 0x00 };

	/* detect tda8295 */
	tuner_i2c_xfer_send(i2c_props, &tda8295_id[0], 1);
	tuner_i2c_xfer_recv(i2c_props, &tda8295_id[1], 1);

	if (tda8295_id[1] == TDA8295_ID) {
		if (debug)
			printk(KERN_DEBUG "%s: tda8295 detected @ %d-%04x\n",
			       __func__, i2c_adapter_id(i2c_props->adap),
			       i2c_props->addr);
		return 0;
	}

	return -ENODEV;
}

static struct analog_demod_ops tda8290_ops = {
	.set_params     = tda8290_set_params,
	.has_signal     = tda8290_has_signal,
	.standby        = tda8290_standby,
	.release        = tda829x_release,
	.i2c_gate_ctrl  = tda8290_i2c_bridge,
};

static struct analog_demod_ops tda8295_ops = {
	.set_params     = tda8295_set_params,
	.has_signal     = tda8295_has_signal,
	.standby        = tda8295_standby,
	.release        = tda829x_release,
	.i2c_gate_ctrl  = tda8295_i2c_bridge,
};

struct dvb_frontend *tda829x_attach(struct dvb_frontend *fe,
				    struct i2c_adapter *i2c_adap, u8 i2c_addr,
				    struct tda829x_config *cfg)
{
	struct tda8290_priv *priv = NULL;
	char *name;

	priv = kzalloc(sizeof(struct tda8290_priv), GFP_KERNEL);
	if (priv == NULL)
		return NULL;
	fe->analog_demod_priv = priv;

	priv->i2c_props.addr     = i2c_addr;
	priv->i2c_props.adap     = i2c_adap;
	priv->i2c_props.name     = "tda829x";
	if (cfg) {
		priv->cfg.config         = cfg->lna_cfg;
		priv->cfg.tuner_callback = cfg->tuner_callback;
	}

	if (tda8290_probe(&priv->i2c_props) == 0) {
		priv->ver = TDA8290;
		memcpy(&fe->ops.analog_ops, &tda8290_ops,
		       sizeof(struct analog_demod_ops));
	}

	if (tda8295_probe(&priv->i2c_props) == 0) {
		priv->ver = TDA8295;
		memcpy(&fe->ops.analog_ops, &tda8295_ops,
		       sizeof(struct analog_demod_ops));
	}

	if ((!(cfg) || (TDA829X_PROBE_TUNER == cfg->probe_tuner)) &&
	    (tda829x_find_tuner(fe) < 0))
		goto fail;

	switch (priv->ver) {
	case TDA8290:
		name = "tda8290";
		break;
	case TDA8295:
		name = "tda8295";
		break;
	case TDA8290 | TDA8275:
		name = "tda8290+75";
		break;
	case TDA8295 | TDA8275:
		name = "tda8295+75";
		break;
	case TDA8290 | TDA8275A:
		name = "tda8290+75a";
		break;
	case TDA8295 | TDA8275A:
		name = "tda8295+75a";
		break;
	case TDA8290 | TDA18271:
		name = "tda8290+18271";
		break;
	case TDA8295 | TDA18271:
		name = "tda8295+18271";
		break;
	default:
		goto fail;
	}
	tuner_info("type set to %s\n", name);

	fe->ops.analog_ops.info.name = name;

	if (priv->ver & TDA8290) {
		tda8290_init_tuner(fe);
		tda8290_init_if(fe);
	} else if (priv->ver & TDA8295)
		tda8295_init_if(fe);

	return fe;

fail:
	tda829x_release(fe);
	return NULL;
}
EXPORT_SYMBOL_GPL(tda829x_attach);

int tda829x_probe(struct i2c_adapter *i2c_adap, u8 i2c_addr)
{
	struct tuner_i2c_props i2c_props = {
		.adap = i2c_adap,
		.addr = i2c_addr,
	};

	unsigned char soft_reset[]   = { 0x00, 0x00 };
	unsigned char easy_mode_b[]  = { 0x01, 0x02 };
	unsigned char easy_mode_g[]  = { 0x01, 0x04 };
	unsigned char restore_9886[] = { 0x00, 0xd6, 0x30 };
	unsigned char addr_dto_lsb = 0x07;
	unsigned char data;
#define PROBE_BUFFER_SIZE 8
	unsigned char buf[PROBE_BUFFER_SIZE];
	int i;

	/* rule out tda9887, which would return the same byte repeatedly */
	tuner_i2c_xfer_send(&i2c_props, soft_reset, 1);
	tuner_i2c_xfer_recv(&i2c_props, buf, PROBE_BUFFER_SIZE);
	for (i = 1; i < PROBE_BUFFER_SIZE; i++) {
		if (buf[i] != buf[0])
			break;
	}

	/* all bytes are equal, not a tda829x - probably a tda9887 */
	if (i == PROBE_BUFFER_SIZE)
		return -ENODEV;

	if ((tda8290_probe(&i2c_props) == 0) ||
	    (tda8295_probe(&i2c_props) == 0))
		return 0;

	/* fall back to old probing method */
	tuner_i2c_xfer_send(&i2c_props, easy_mode_b, 2);
	tuner_i2c_xfer_send(&i2c_props, soft_reset, 2);
	tuner_i2c_xfer_send(&i2c_props, &addr_dto_lsb, 1);
	tuner_i2c_xfer_recv(&i2c_props, &data, 1);
	if (data == 0) {
		tuner_i2c_xfer_send(&i2c_props, easy_mode_g, 2);
		tuner_i2c_xfer_send(&i2c_props, soft_reset, 2);
		tuner_i2c_xfer_send(&i2c_props, &addr_dto_lsb, 1);
		tuner_i2c_xfer_recv(&i2c_props, &data, 1);
		if (data == 0x7b) {
			return 0;
		}
	}
	tuner_i2c_xfer_send(&i2c_props, restore_9886, 3);
	return -ENODEV;
}
EXPORT_SYMBOL_GPL(tda829x_probe);

MODULE_DESCRIPTION("Philips/NXP TDA8290/TDA8295 analog IF demodulator driver");
MODULE_AUTHOR("Gerd Knorr, Hartmut Hackmann, Michael Krufky");
MODULE_LICENSE("GPL");

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-basic-offset: 8
 * End:
 */

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
#include "tda8290.h"
#include "tda827x.h"

static int tuner_debug = 0;
module_param_named(debug, tuner_debug, int, 0644);
MODULE_PARM_DESC(debug, "enable verbose debug messages");

#define PREFIX "tda8290 "

/* ---------------------------------------------------------------------- */

struct tda8290_priv {
	struct tuner_i2c_props i2c_props;

	unsigned char tda8290_easy_mode;

	unsigned char tda827x_addr;
	unsigned char tda827x_ver;

	struct tda827x_config cfg;
};

/*---------------------------------------------------------------------*/

static void tda8290_i2c_bridge(struct tuner *t, int close)
{
	struct tda8290_priv *priv = t->priv;

	unsigned char  enable[2] = { 0x21, 0xC0 };
	unsigned char disable[2] = { 0x21, 0x00 };
	unsigned char *msg;
	if(close) {
		msg = enable;
		tuner_i2c_xfer_send(&priv->i2c_props, msg, 2);
		/* let the bridge stabilize */
		msleep(20);
	} else {
		msg = disable;
		tuner_i2c_xfer_send(&priv->i2c_props, msg, 2);
	}
}

/*---------------------------------------------------------------------*/

static void set_audio(struct tuner *t)
{
	struct tda8290_priv *priv = t->priv;
	char* mode;

	priv->cfg.tda827x_lpsel = 0;
	if (t->std & V4L2_STD_MN) {
		priv->cfg.sgIF = 92;
		priv->tda8290_easy_mode = 0x01;
		priv->cfg.tda827x_lpsel = 1;
		mode = "MN";
	} else if (t->std & V4L2_STD_B) {
		priv->cfg.sgIF = 108;
		priv->tda8290_easy_mode = 0x02;
		mode = "B";
	} else if (t->std & V4L2_STD_GH) {
		priv->cfg.sgIF = 124;
		priv->tda8290_easy_mode = 0x04;
		mode = "GH";
	} else if (t->std & V4L2_STD_PAL_I) {
		priv->cfg.sgIF = 124;
		priv->tda8290_easy_mode = 0x08;
		mode = "I";
	} else if (t->std & V4L2_STD_DK) {
		priv->cfg.sgIF = 124;
		priv->tda8290_easy_mode = 0x10;
		mode = "DK";
	} else if (t->std & V4L2_STD_SECAM_L) {
		priv->cfg.sgIF = 124;
		priv->tda8290_easy_mode = 0x20;
		mode = "L";
	} else if (t->std & V4L2_STD_SECAM_LC) {
		priv->cfg.sgIF = 20;
		priv->tda8290_easy_mode = 0x40;
		mode = "LC";
	} else {
		priv->cfg.sgIF = 124;
		priv->tda8290_easy_mode = 0x10;
		mode = "xx";
	}

	if (t->mode == V4L2_TUNER_RADIO)
		priv->cfg.sgIF = 88; /* if frequency is 5.5 MHz */

	tuner_dbg("setting tda8290 to system %s\n", mode);
}

static void tda8290_set_freq(struct tuner *t, unsigned int freq)
{
	struct tda8290_priv *priv = t->priv;
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

	struct analog_parameters params = {
		.frequency = freq,
		.mode      = t->mode,
		.audmode   = t->audmode,
		.std       = t->std
	};

	set_audio(t);

	tuner_dbg("tda827xa config is 0x%02x\n", t->config);
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

	tda8290_i2c_bridge(t, 1);

	if (t->fe.ops.tuner_ops.set_analog_params)
		t->fe.ops.tuner_ops.set_analog_params(&t->fe, &params);

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
				priv->cfg.agcf(&t->fe);
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

	tda8290_i2c_bridge(t, 0);
	tuner_i2c_xfer_send(&priv->i2c_props, if_agc_set, 2);
}

/*---------------------------------------------------------------------*/

static int tda8290_has_signal(struct tuner *t)
{
	struct tda8290_priv *priv = t->priv;

	unsigned char i2c_get_afc[1] = { 0x1B };
	unsigned char afc = 0;

	tuner_i2c_xfer_send(&priv->i2c_props, i2c_get_afc, ARRAY_SIZE(i2c_get_afc));
	tuner_i2c_xfer_recv(&priv->i2c_props, &afc, 1);
	return (afc & 0x80)? 65535:0;
}

/*---------------------------------------------------------------------*/

static void tda8290_standby(struct tuner *t)
{
	struct tda8290_priv *priv = t->priv;
	unsigned char cb1[] = { 0x30, 0xD0 };
	unsigned char tda8290_standby[] = { 0x00, 0x02 };
	unsigned char tda8290_agc_tri[] = { 0x02, 0x20 };
	struct i2c_msg msg = {.addr = priv->tda827x_addr, .flags=0, .buf=cb1, .len = 2};

	tda8290_i2c_bridge(t, 1);
	if (priv->tda827x_ver != 0)
		cb1[1] = 0x90;
	i2c_transfer(priv->i2c_props.adap, &msg, 1);
	tda8290_i2c_bridge(t, 0);
	tuner_i2c_xfer_send(&priv->i2c_props, tda8290_agc_tri, 2);
	tuner_i2c_xfer_send(&priv->i2c_props, tda8290_standby, 2);
}

static void tda8290_init_if(struct tuner *t)
{
	struct tda8290_priv *priv = t->priv;

	unsigned char set_VS[] = { 0x30, 0x6F };
	unsigned char set_GP00_CF[] = { 0x20, 0x01 };
	unsigned char set_GP01_CF[] = { 0x20, 0x0B };

	if ((t->config == 1) || (t->config == 2))
		tuner_i2c_xfer_send(&priv->i2c_props, set_GP00_CF, 2);
	else
		tuner_i2c_xfer_send(&priv->i2c_props, set_GP01_CF, 2);
	tuner_i2c_xfer_send(&priv->i2c_props, set_VS, 2);
}

static void tda8290_init_tuner(struct tuner *t)
{
	struct tda8290_priv *priv = t->priv;
	unsigned char tda8275_init[]  = { 0x00, 0x00, 0x00, 0x40, 0xdC, 0x04, 0xAf,
					  0x3F, 0x2A, 0x04, 0xFF, 0x00, 0x00, 0x40 };
	unsigned char tda8275a_init[] = { 0x00, 0x00, 0x00, 0x00, 0xdC, 0x05, 0x8b,
					  0x0c, 0x04, 0x20, 0xFF, 0x00, 0x00, 0x4b };
	struct i2c_msg msg = {.addr = priv->tda827x_addr, .flags=0,
			      .buf=tda8275_init, .len = 14};
	if (priv->tda827x_ver != 0)
		msg.buf = tda8275a_init;

	tda8290_i2c_bridge(t, 1);
	i2c_transfer(priv->i2c_props.adap, &msg, 1);
	tda8290_i2c_bridge(t, 0);
}

/*---------------------------------------------------------------------*/

static void tda8290_release(struct tuner *t)
{
	if (t->fe.ops.tuner_ops.release)
		t->fe.ops.tuner_ops.release(&t->fe);

	kfree(t->priv);
	t->priv = NULL;
}

static struct tuner_operations tda8290_tuner_ops = {
	.set_tv_freq    = tda8290_set_freq,
	.set_radio_freq = tda8290_set_freq,
	.has_signal     = tda8290_has_signal,
	.standby        = tda8290_standby,
	.release        = tda8290_release,
};

int tda8290_attach(struct tuner *t)
{
	struct tda8290_priv *priv = NULL;
	u8 data;
	int i, ret, tuners_found;
	u32 tuner_addrs;
	struct i2c_msg msg = {.flags=I2C_M_RD, .buf=&data, .len = 1};

	priv = kzalloc(sizeof(struct tda8290_priv), GFP_KERNEL);
	if (priv == NULL)
		return -ENOMEM;
	t->priv = priv;

	priv->i2c_props.addr     = t->i2c.addr;
	priv->i2c_props.adap     = t->i2c.adapter;
	priv->cfg.config         = &t->config;
	priv->cfg.tuner_callback = t->tuner_callback;

	tda8290_i2c_bridge(t, 1);
	/* probe for tuner chip */
	tuners_found = 0;
	tuner_addrs = 0;
	for (i=0x60; i<= 0x63; i++) {
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
	tda8290_i2c_bridge(t, 0);
	if(tuners_found > 1)
		for (i = 0; i < tuners_found; i++) {
			msg.addr = tuner_addrs  & 0xff;
			ret = i2c_transfer(priv->i2c_props.adap, &msg, 1);
			if(ret == 1)
				tuner_addrs = tuner_addrs >> 8;
			else
				break;
		}
	if (tuner_addrs == 0) {
		tuner_addrs = 0x61;
		tuner_info("could not clearly identify tuner address, defaulting to %x\n",
			     tuner_addrs);
	} else {
		tuner_addrs = tuner_addrs & 0xff;
		tuner_info("setting tuner address to %x\n", tuner_addrs);
	}
	priv->tda827x_addr = tuner_addrs;
	msg.addr = tuner_addrs;

	tda8290_i2c_bridge(t, 1);

	ret = i2c_transfer(priv->i2c_props.adap, &msg, 1);
	if( ret != 1)
		tuner_warn("TDA827x access failed!\n");

	if ((data & 0x3c) == 0) {
		strlcpy(t->i2c.name, "tda8290+75", sizeof(t->i2c.name));
		priv->tda827x_ver = 0;
	} else {
		strlcpy(t->i2c.name, "tda8290+75a", sizeof(t->i2c.name));
		priv->tda827x_ver = 2;
	}
	tda827x_attach(&t->fe, priv->tda827x_addr,
		       priv->i2c_props.adap, &priv->cfg);

	/* FIXME: tda827x module doesn't probe the tuner until
	 * tda827x_initial_sleep is called
	 */
	if (t->fe.ops.tuner_ops.sleep)
		t->fe.ops.tuner_ops.sleep(&t->fe);

	memcpy(&t->ops, &tda8290_tuner_ops, sizeof(struct tuner_operations));

	tuner_info("type set to %s\n", t->i2c.name);

	priv->cfg.tda827x_lpsel = 0;
	t->mode = V4L2_TUNER_ANALOG_TV;

	tda8290_init_tuner(t);
	tda8290_init_if(t);
	return 0;
}

int tda8290_probe(struct tuner *t)
{
	struct tuner_i2c_props i2c_props = {
		.adap = t->i2c.adapter,
		.addr = t->i2c.addr
	};

	unsigned char soft_reset[]   = { 0x00, 0x00 };
	unsigned char easy_mode_b[]  = { 0x01, 0x02 };
	unsigned char easy_mode_g[]  = { 0x01, 0x04 };
	unsigned char restore_9886[] = { 0x00, 0xd6, 0x30 };
	unsigned char addr_dto_lsb = 0x07;
	unsigned char data;

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
	return -1;
}

EXPORT_SYMBOL_GPL(tda8290_probe);
EXPORT_SYMBOL_GPL(tda8290_attach);

MODULE_DESCRIPTION("Philips TDA8290 + TDA8275 / TDA8275a tuner driver");
MODULE_AUTHOR("Gerd Knorr, Hartmut Hackmann");
MODULE_LICENSE("GPL");

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-basic-offset: 8
 * End:
 */

/*
 * Copyright (C) 2010 Michael Krufky (mkrufky@kernellabs.com)
 *
 *   This program is free software; you can redistribute it and/or modify it
 *   under the terms of the GNU General Public License as published by the Free
 *   Software Foundation, version 2.
 *
 * see Documentation/dvb/README.dvb-usb for more information
 */

#include <linux/vmalloc.h>
#include <linux/i2c.h>

#include "mxl111sf.h"
#include "mxl111sf-reg.h"
#include "mxl111sf-phy.h"
#include "mxl111sf-i2c.h"
#include "mxl111sf-gpio.h"

#include "mxl111sf-demod.h"
#include "mxl111sf-tuner.h"

#include "lgdt3305.h"

int dvb_usb_mxl111sf_debug;
module_param_named(debug, dvb_usb_mxl111sf_debug, int, 0644);
MODULE_PARM_DESC(debug, "set debugging level "
		 "(1=info, 2=xfer, 4=i2c, 8=reg, 16=adv (or-able)).");

int dvb_usb_mxl111sf_isoc;
module_param_named(isoc, dvb_usb_mxl111sf_isoc, int, 0644);
MODULE_PARM_DESC(isoc, "enable usb isoc xfer (0=bulk, 1=isoc).");

#define ANT_PATH_AUTO 0
#define ANT_PATH_EXTERNAL 1
#define ANT_PATH_INTERNAL 2

int dvb_usb_mxl111sf_rfswitch =
#if 0
		ANT_PATH_AUTO;
#else
		ANT_PATH_EXTERNAL;
#endif

module_param_named(rfswitch, dvb_usb_mxl111sf_rfswitch, int, 0644);
MODULE_PARM_DESC(rfswitch, "force rf switch position (0=auto, 1=ext, 2=int).");

DVB_DEFINE_MOD_OPT_ADAPTER_NR(adapter_nr);

#define deb_info(args...)   dprintk(dvb_usb_mxl111sf_debug, 0x13, args)
#define deb_reg(args...)    dprintk(dvb_usb_mxl111sf_debug, 0x08, args)
#define deb_adv(args...)    dprintk(dvb_usb_mxl111sf_debug, MXL_ADV_DBG, args)

int mxl111sf_ctrl_msg(struct dvb_usb_device *d,
		      u8 cmd, u8 *wbuf, int wlen, u8 *rbuf, int rlen)
{
	int wo = (rbuf == NULL || rlen == 0); /* write-only */
	int ret;
	u8 sndbuf[1+wlen];

	deb_adv("%s(wlen = %d, rlen = %d)\n", __func__, wlen, rlen);

	memset(sndbuf, 0, 1+wlen);

	sndbuf[0] = cmd;
	memcpy(&sndbuf[1], wbuf, wlen);

	ret = (wo) ? dvb_usb_generic_write(d, sndbuf, 1+wlen) :
		dvb_usb_generic_rw(d, sndbuf, 1+wlen, rbuf, rlen, 0);
	mxl_fail(ret);

	return ret;
}

/* ------------------------------------------------------------------------ */

#define MXL_CMD_REG_READ	0xaa
#define MXL_CMD_REG_WRITE	0x55

int mxl111sf_read_reg(struct mxl111sf_state *state, u8 addr, u8 *data)
{
	u8 buf[2];
	int ret;

	ret = mxl111sf_ctrl_msg(state->d, MXL_CMD_REG_READ, &addr, 1, buf, 2);
	if (mxl_fail(ret)) {
		mxl_debug("error reading reg: 0x%02x", addr);
		goto fail;
	}

	if (buf[0] == addr)
		*data = buf[1];
	else {
		err("invalid response reading reg: 0x%02x != 0x%02x, 0x%02x",
		    addr, buf[0], buf[1]);
		ret = -EINVAL;
	}

	deb_reg("R: (0x%02x, 0x%02x)\n", addr, *data);
fail:
	return ret;
}

int mxl111sf_write_reg(struct mxl111sf_state *state, u8 addr, u8 data)
{
	u8 buf[] = { addr, data };
	int ret;

	deb_reg("W: (0x%02x, 0x%02x)\n", addr, data);

	ret = mxl111sf_ctrl_msg(state->d, MXL_CMD_REG_WRITE, buf, 2, NULL, 0);
	if (mxl_fail(ret))
		err("error writing reg: 0x%02x, val: 0x%02x", addr, data);
	return ret;
}

/* ------------------------------------------------------------------------ */

int mxl111sf_write_reg_mask(struct mxl111sf_state *state,
				   u8 addr, u8 mask, u8 data)
{
	int ret;
	u8 val;

	if (mask != 0xff) {
		ret = mxl111sf_read_reg(state, addr, &val);
#if 1
		/* dont know why this usually errors out on the first try */
		if (mxl_fail(ret))
			err("error writing addr: 0x%02x, mask: 0x%02x, "
			    "data: 0x%02x, retrying...", addr, mask, data);

		ret = mxl111sf_read_reg(state, addr, &val);
#endif
		if (mxl_fail(ret))
			goto fail;
	}
	val &= ~mask;
	val |= data;

	ret = mxl111sf_write_reg(state, addr, val);
	mxl_fail(ret);
fail:
	return ret;
}

/* ------------------------------------------------------------------------ */

int mxl111sf_ctrl_program_regs(struct mxl111sf_state *state,
			       struct mxl111sf_reg_ctrl_info *ctrl_reg_info)
{
	int i, ret = 0;

	for (i = 0;  ctrl_reg_info[i].addr |
		     ctrl_reg_info[i].mask |
		     ctrl_reg_info[i].data;  i++) {

		ret = mxl111sf_write_reg_mask(state,
					      ctrl_reg_info[i].addr,
					      ctrl_reg_info[i].mask,
					      ctrl_reg_info[i].data);
		if (mxl_fail(ret)) {
			err("failed on reg #%d (0x%02x)", i,
			    ctrl_reg_info[i].addr);
			break;
		}
	}
	return ret;
}

/* ------------------------------------------------------------------------ */

static int mxl1x1sf_get_chip_info(struct mxl111sf_state *state)
{
	int ret;
	u8 id, ver;
	char *mxl_chip, *mxl_rev;

	if ((state->chip_id) && (state->chip_ver))
		return 0;

	ret = mxl111sf_read_reg(state, CHIP_ID_REG, &id);
	if (mxl_fail(ret))
		goto fail;
	state->chip_id = id;

	ret = mxl111sf_read_reg(state, TOP_CHIP_REV_ID_REG, &ver);
	if (mxl_fail(ret))
		goto fail;
	state->chip_ver = ver;

	switch (id) {
	case 0x61:
		mxl_chip = "MxL101SF";
		break;
	case 0x63:
		mxl_chip = "MxL111SF";
		break;
	default:
		mxl_chip = "UNKNOWN MxL1X1";
		break;
	}
	switch (ver) {
	case 0x36:
		state->chip_rev = MXL111SF_V6;
		mxl_rev = "v6";
		break;
	case 0x08:
		state->chip_rev = MXL111SF_V8_100;
		mxl_rev = "v8_100";
		break;
	case 0x18:
		state->chip_rev = MXL111SF_V8_200;
		mxl_rev = "v8_200";
		break;
	default:
		state->chip_rev = 0;
		mxl_rev = "UNKNOWN REVISION";
		break;
	}
	info("%s detected, %s (0x%x)", mxl_chip, mxl_rev, ver);
fail:
	return ret;
}

#define get_chip_info(state)						\
({									\
	int ___ret;							\
	___ret = mxl1x1sf_get_chip_info(state);				\
	if (mxl_fail(___ret)) {						\
		mxl_debug("failed to get chip info"			\
			  " on first probe attempt");			\
		___ret = mxl1x1sf_get_chip_info(state);			\
		if (mxl_fail(___ret))					\
			err("failed to get chip info during probe");	\
		else							\
			mxl_debug("probe needed a retry "		\
				  "in order to succeed.");		\
	}								\
	___ret;								\
})

/* ------------------------------------------------------------------------ */

static int mxl111sf_power_ctrl(struct dvb_usb_device *d, int onoff)
{
	/* power control depends on which adapter is being woken:
	 * save this for init, instead, via mxl111sf_adap_fe_init */
	return 0;
}

static int mxl111sf_adap_fe_init(struct dvb_frontend *fe)
{
	struct dvb_usb_adapter *adap = fe->dvb->priv;
	struct dvb_usb_device *d = adap->dev;
	struct mxl111sf_state *state = d->priv;
	struct mxl111sf_adap_state *adap_state = adap->fe_adap[fe->id].priv;

	int err;

	/* exit if we didnt initialize the driver yet */
	if (!state->chip_id) {
		mxl_debug("driver not yet initialized, exit.");
		goto fail;
	}

	deb_info("%s()\n", __func__);

	mutex_lock(&state->fe_lock);

	state->alt_mode = adap_state->alt_mode;

	if (usb_set_interface(adap->dev->udev, 0, state->alt_mode) < 0)
		err("set interface failed");

	err = mxl1x1sf_soft_reset(state);
	mxl_fail(err);
	err = mxl111sf_init_tuner_demod(state);
	mxl_fail(err);
	err = mxl1x1sf_set_device_mode(state, adap_state->device_mode);

	mxl_fail(err);
	mxl111sf_enable_usb_output(state);
	mxl_fail(err);
	mxl1x1sf_top_master_ctrl(state, 1);
	mxl_fail(err);

	if ((MXL111SF_GPIO_MOD_DVBT != adap_state->gpio_mode) &&
	    (state->chip_rev > MXL111SF_V6)) {
		mxl111sf_config_pin_mux_modes(state,
					      PIN_MUX_TS_SPI_IN_MODE_1);
		mxl_fail(err);
	}
	err = mxl111sf_init_port_expander(state);
	if (!mxl_fail(err)) {
		state->gpio_mode = adap_state->gpio_mode;
		err = mxl111sf_gpio_mode_switch(state, state->gpio_mode);
		mxl_fail(err);
#if 0
		err = fe->ops.init(fe);
#endif
		msleep(100); /* add short delay after enabling
			      * the demod before touching it */
	}

	return (adap_state->fe_init) ? adap_state->fe_init(fe) : 0;
fail:
	return -ENODEV;
}

static int mxl111sf_adap_fe_sleep(struct dvb_frontend *fe)
{
	struct dvb_usb_adapter *adap = fe->dvb->priv;
	struct dvb_usb_device *d = adap->dev;
	struct mxl111sf_state *state = d->priv;
	struct mxl111sf_adap_state *adap_state = adap->fe_adap[fe->id].priv;
	int err;

	/* exit if we didnt initialize the driver yet */
	if (!state->chip_id) {
		mxl_debug("driver not yet initialized, exit.");
		goto fail;
	}

	deb_info("%s()\n", __func__);

	err = (adap_state->fe_sleep) ? adap_state->fe_sleep(fe) : 0;

	mutex_unlock(&state->fe_lock);

	return err;
fail:
	return -ENODEV;
}


static int mxl111sf_ep6_streaming_ctrl(struct dvb_usb_adapter *adap, int onoff)
{
	struct dvb_usb_device *d = adap->dev;
	struct mxl111sf_state *state = d->priv;
	struct mxl111sf_adap_state *adap_state = adap->fe_adap[adap->active_fe].priv;
	int ret = 0;
	u8 tmp;

	deb_info("%s(%d)\n", __func__, onoff);

	if (onoff) {
		ret = mxl111sf_enable_usb_output(state);
		mxl_fail(ret);
		ret = mxl111sf_config_mpeg_in(state, 1, 1,
					      adap_state->ep6_clockphase,
					      0, 0);
		mxl_fail(ret);
	} else {
		ret = mxl111sf_disable_656_port(state);
		mxl_fail(ret);
	}

	mxl111sf_read_reg(state, 0x12, &tmp);
	tmp &= ~0x04;
	mxl111sf_write_reg(state, 0x12, tmp);

	return ret;
}

static int mxl111sf_ep4_streaming_ctrl(struct dvb_usb_adapter *adap, int onoff)
{
	struct dvb_usb_device *d = adap->dev;
	struct mxl111sf_state *state = d->priv;
	int ret = 0;

	deb_info("%s(%d)\n", __func__, onoff);

	if (onoff) {
		ret = mxl111sf_enable_usb_output(state);
		mxl_fail(ret);
	}

	return ret;
}

/* ------------------------------------------------------------------------ */

static struct lgdt3305_config hauppauge_lgdt3305_config = {
	.i2c_addr           = 0xb2 >> 1,
	.mpeg_mode          = LGDT3305_MPEG_SERIAL,
	.tpclk_edge         = LGDT3305_TPCLK_RISING_EDGE,
	.tpvalid_polarity   = LGDT3305_TP_VALID_HIGH,
	.deny_i2c_rptr      = 1,
	.spectral_inversion = 0,
	.qam_if_khz         = 6000,
	.vsb_if_khz         = 6000,
};

static int mxl111sf_lgdt3305_frontend_attach(struct dvb_usb_adapter *adap)
{
	struct dvb_usb_device *d = adap->dev;
	struct mxl111sf_state *state = d->priv;
	int fe_id = adap->num_frontends_initialized;
	struct mxl111sf_adap_state *adap_state = adap->fe_adap[fe_id].priv;
	int ret;

	deb_adv("%s()\n", __func__);

	/* save a pointer to the dvb_usb_device in device state */
	state->d = d;
	adap_state->alt_mode = (dvb_usb_mxl111sf_isoc) ? 2 : 1;
	state->alt_mode = adap_state->alt_mode;

	if (usb_set_interface(adap->dev->udev, 0, state->alt_mode) < 0)
		err("set interface failed");

	state->gpio_mode = MXL111SF_GPIO_MOD_ATSC;
	adap_state->gpio_mode = state->gpio_mode;
	adap_state->device_mode = MXL_TUNER_MODE;
	adap_state->ep6_clockphase = 1;

	ret = mxl1x1sf_soft_reset(state);
	if (mxl_fail(ret))
		goto fail;
	ret = mxl111sf_init_tuner_demod(state);
	if (mxl_fail(ret))
		goto fail;

	ret = mxl1x1sf_set_device_mode(state, adap_state->device_mode);
	if (mxl_fail(ret))
		goto fail;

	ret = mxl111sf_enable_usb_output(state);
	if (mxl_fail(ret))
		goto fail;
	ret = mxl1x1sf_top_master_ctrl(state, 1);
	if (mxl_fail(ret))
		goto fail;

	ret = mxl111sf_init_port_expander(state);
	if (mxl_fail(ret))
		goto fail;
	ret = mxl111sf_gpio_mode_switch(state, state->gpio_mode);
	if (mxl_fail(ret))
		goto fail;

	adap->fe_adap[fe_id].fe = dvb_attach(lgdt3305_attach,
				 &hauppauge_lgdt3305_config,
				 &adap->dev->i2c_adap);
	if (adap->fe_adap[fe_id].fe) {
		adap_state->fe_init = adap->fe_adap[fe_id].fe->ops.init;
		adap->fe_adap[fe_id].fe->ops.init = mxl111sf_adap_fe_init;
		adap_state->fe_sleep = adap->fe_adap[fe_id].fe->ops.sleep;
		adap->fe_adap[fe_id].fe->ops.sleep = mxl111sf_adap_fe_sleep;
		return 0;
	}
	ret = -EIO;
fail:
	return ret;
}

static struct mxl111sf_demod_config mxl_demod_config = {
	.read_reg        = mxl111sf_read_reg,
	.write_reg       = mxl111sf_write_reg,
	.program_regs    = mxl111sf_ctrl_program_regs,
};

static int mxl111sf_attach_demod(struct dvb_usb_adapter *adap)
{
	struct dvb_usb_device *d = adap->dev;
	struct mxl111sf_state *state = d->priv;
	int fe_id = adap->num_frontends_initialized;
	struct mxl111sf_adap_state *adap_state = adap->fe_adap[fe_id].priv;
	int ret;

	deb_adv("%s()\n", __func__);

	/* save a pointer to the dvb_usb_device in device state */
	state->d = d;
	adap_state->alt_mode = (dvb_usb_mxl111sf_isoc) ? 1 : 2;
	state->alt_mode = adap_state->alt_mode;

	if (usb_set_interface(adap->dev->udev, 0, state->alt_mode) < 0)
		err("set interface failed");

	state->gpio_mode = MXL111SF_GPIO_MOD_DVBT;
	adap_state->gpio_mode = state->gpio_mode;
	adap_state->device_mode = MXL_SOC_MODE;
	adap_state->ep6_clockphase = 1;

	ret = mxl1x1sf_soft_reset(state);
	if (mxl_fail(ret))
		goto fail;
	ret = mxl111sf_init_tuner_demod(state);
	if (mxl_fail(ret))
		goto fail;

	ret = mxl1x1sf_set_device_mode(state, adap_state->device_mode);
	if (mxl_fail(ret))
		goto fail;

	ret = mxl111sf_enable_usb_output(state);
	if (mxl_fail(ret))
		goto fail;
	ret = mxl1x1sf_top_master_ctrl(state, 1);
	if (mxl_fail(ret))
		goto fail;

	/* dont care if this fails */
	mxl111sf_init_port_expander(state);

	adap->fe_adap[fe_id].fe = dvb_attach(mxl111sf_demod_attach, state,
			      &mxl_demod_config);
	if (adap->fe_adap[fe_id].fe) {
		adap_state->fe_init = adap->fe_adap[fe_id].fe->ops.init;
		adap->fe_adap[fe_id].fe->ops.init = mxl111sf_adap_fe_init;
		adap_state->fe_sleep = adap->fe_adap[fe_id].fe->ops.sleep;
		adap->fe_adap[fe_id].fe->ops.sleep = mxl111sf_adap_fe_sleep;
		return 0;
	}
	ret = -EIO;
fail:
	return ret;
}

static inline int mxl111sf_set_ant_path(struct mxl111sf_state *state,
					int antpath)
{
	return mxl111sf_idac_config(state, 1, 1,
				    (antpath == ANT_PATH_INTERNAL) ?
				    0x3f : 0x00, 0);
}

#define DbgAntHunt(x, pwr0, pwr1, pwr2, pwr3) \
	err("%s(%d) FINAL input set to %s rxPwr:%d|%d|%d|%d\n", \
	    __func__, __LINE__, \
	    (ANT_PATH_EXTERNAL == x) ? "EXTERNAL" : "INTERNAL", \
	    pwr0, pwr1, pwr2, pwr3)

#define ANT_HUNT_SLEEP 90
#define ANT_EXT_TWEAK 0

static int mxl111sf_ant_hunt(struct dvb_frontend *fe)
{
	struct dvb_usb_adapter *adap = fe->dvb->priv;
	struct dvb_usb_device *d = adap->dev;
	struct mxl111sf_state *state = d->priv;

	int antctrl = dvb_usb_mxl111sf_rfswitch;

	u16 rxPwrA, rxPwr0, rxPwr1, rxPwr2;

	/* FIXME: must force EXTERNAL for QAM - done elsewhere */
	mxl111sf_set_ant_path(state, antctrl == ANT_PATH_AUTO ?
			      ANT_PATH_EXTERNAL : antctrl);

	if (antctrl == ANT_PATH_AUTO) {
#if 0
		msleep(ANT_HUNT_SLEEP);
#endif
		fe->ops.tuner_ops.get_rf_strength(fe, &rxPwrA);

		mxl111sf_set_ant_path(state, ANT_PATH_EXTERNAL);
		msleep(ANT_HUNT_SLEEP);
		fe->ops.tuner_ops.get_rf_strength(fe, &rxPwr0);

		mxl111sf_set_ant_path(state, ANT_PATH_EXTERNAL);
		msleep(ANT_HUNT_SLEEP);
		fe->ops.tuner_ops.get_rf_strength(fe, &rxPwr1);

		mxl111sf_set_ant_path(state, ANT_PATH_INTERNAL);
		msleep(ANT_HUNT_SLEEP);
		fe->ops.tuner_ops.get_rf_strength(fe, &rxPwr2);

		if (rxPwr1+ANT_EXT_TWEAK >= rxPwr2) {
			/* return with EXTERNAL enabled */
			mxl111sf_set_ant_path(state, ANT_PATH_EXTERNAL);
			DbgAntHunt(ANT_PATH_EXTERNAL, rxPwrA,
				   rxPwr0, rxPwr1, rxPwr2);
		} else {
			/* return with INTERNAL enabled */
			DbgAntHunt(ANT_PATH_INTERNAL, rxPwrA,
				   rxPwr0, rxPwr1, rxPwr2);
		}
	}
	return 0;
}

static struct mxl111sf_tuner_config mxl_tuner_config = {
	.if_freq         = MXL_IF_6_0, /* applies to external IF output, only */
	.invert_spectrum = 0,
	.read_reg        = mxl111sf_read_reg,
	.write_reg       = mxl111sf_write_reg,
	.program_regs    = mxl111sf_ctrl_program_regs,
	.top_master_ctrl = mxl1x1sf_top_master_ctrl,
	.ant_hunt        = mxl111sf_ant_hunt,
};

static int mxl111sf_attach_tuner(struct dvb_usb_adapter *adap)
{
	struct dvb_usb_device *d = adap->dev;
	struct mxl111sf_state *state = d->priv;
	int fe_id = adap->num_frontends_initialized;

	deb_adv("%s()\n", __func__);

	if (NULL != dvb_attach(mxl111sf_tuner_attach,
			       adap->fe_adap[fe_id].fe, state,
			       &mxl_tuner_config))
		return 0;

	return -EIO;
}

static int mxl111sf_fe_ioctl_override(struct dvb_frontend *fe,
				      unsigned int cmd, void *parg,
				      unsigned int stage)
{
	int err = 0;

	switch (stage) {
	case DVB_FE_IOCTL_PRE:

		switch (cmd) {
		case FE_READ_SIGNAL_STRENGTH:
			err = fe->ops.tuner_ops.get_rf_strength(fe, parg);
			/* If no error occurs, prevent dvb-core from handling
			 * this IOCTL, otherwise return the error */
			if (0 == err)
				err = 1;
			break;
		}
		break;

	case DVB_FE_IOCTL_POST:
		/* no post-ioctl handling required */
		break;
	}
	return err;
};

static u32 mxl111sf_i2c_func(struct i2c_adapter *adapter)
{
	return I2C_FUNC_I2C;
}

struct i2c_algorithm mxl111sf_i2c_algo = {
	.master_xfer   = mxl111sf_i2c_xfer,
	.functionality = mxl111sf_i2c_func,
#ifdef NEED_ALGO_CONTROL
	.algo_control = dummy_algo_control,
#endif
};

static struct dvb_usb_device_properties mxl111sf_dvbt_bulk_properties;
static struct dvb_usb_device_properties mxl111sf_dvbt_isoc_properties;
static struct dvb_usb_device_properties mxl111sf_atsc_bulk_properties;
static struct dvb_usb_device_properties mxl111sf_atsc_isoc_properties;

static int mxl111sf_probe(struct usb_interface *intf,
			  const struct usb_device_id *id)
{
	struct dvb_usb_device *d = NULL;

	deb_adv("%s()\n", __func__);

	if (((dvb_usb_mxl111sf_isoc) &&
	     (0 == dvb_usb_device_init(intf,
				       &mxl111sf_dvbt_isoc_properties,
				       THIS_MODULE, &d, adapter_nr) ||
	      0 == dvb_usb_device_init(intf,
				       &mxl111sf_atsc_isoc_properties,
				       THIS_MODULE, &d, adapter_nr))) ||
	    0 == dvb_usb_device_init(intf,
				     &mxl111sf_dvbt_bulk_properties,
				     THIS_MODULE, &d, adapter_nr) ||
	    0 == dvb_usb_device_init(intf,
				     &mxl111sf_atsc_bulk_properties,
				     THIS_MODULE, &d, adapter_nr) || 0) {

		struct mxl111sf_state *state = d->priv;
		static u8 eeprom[256];
		struct i2c_client c;
		int ret;

		ret = get_chip_info(state);
		if (mxl_fail(ret))
			err("failed to get chip info during probe");

		mutex_init(&state->fe_lock);

		if (state->chip_rev > MXL111SF_V6)
			mxl111sf_config_pin_mux_modes(state,
						      PIN_MUX_TS_SPI_IN_MODE_1);

		c.adapter = &d->i2c_adap;
		c.addr = 0xa0 >> 1;

		ret = tveeprom_read(&c, eeprom, sizeof(eeprom));
		if (mxl_fail(ret))
			return 0;
		tveeprom_hauppauge_analog(&c, &state->tv,
					  (0x84 == eeprom[0xa0]) ?
					  eeprom + 0xa0 : eeprom + 0x80);
#if 0
		switch (state->tv.model) {
		case 117001:
		case 126001:
		case 138001:
			break;
		default:
			printk(KERN_WARNING "%s: warning: "
			       "unknown hauppauge model #%d\n",
			       __func__, state->tv.model);
		}
#endif
		return 0;
	}
	err("Your device is not yet supported by this driver. "
	    "See kernellabs.com for more info");
	return -EINVAL;
}

static struct usb_device_id mxl111sf_table[] = {
/* 0 */	{ USB_DEVICE(USB_VID_HAUPPAUGE, 0xc600) }, /* ATSC+ IR     */
	{ USB_DEVICE(USB_VID_HAUPPAUGE, 0xc601) }, /* ATSC         */
	{ USB_DEVICE(USB_VID_HAUPPAUGE, 0xc602) }, /*     +        */
	{ USB_DEVICE(USB_VID_HAUPPAUGE, 0xc603) }, /* ATSC+        */
	{ USB_DEVICE(USB_VID_HAUPPAUGE, 0xc604) }, /* DVBT         */
/* 5 */	{ USB_DEVICE(USB_VID_HAUPPAUGE, 0xc609) }, /* ATSC  IR     */
	{ USB_DEVICE(USB_VID_HAUPPAUGE, 0xc60a) }, /*     + IR     */
	{ USB_DEVICE(USB_VID_HAUPPAUGE, 0xc60b) }, /* ATSC+ IR     */
	{ USB_DEVICE(USB_VID_HAUPPAUGE, 0xc60c) }, /* DVBT  IR     */
	{ USB_DEVICE(USB_VID_HAUPPAUGE, 0xc653) }, /* ATSC+        */
/*10 */	{ USB_DEVICE(USB_VID_HAUPPAUGE, 0xc65b) }, /* ATSC+ IR     */
	{ USB_DEVICE(USB_VID_HAUPPAUGE, 0xb700) }, /* ATSC+ sw     */
	{ USB_DEVICE(USB_VID_HAUPPAUGE, 0xb701) }, /* ATSC  sw     */
	{ USB_DEVICE(USB_VID_HAUPPAUGE, 0xb702) }, /*     + sw     */
	{ USB_DEVICE(USB_VID_HAUPPAUGE, 0xb703) }, /* ATSC+ sw     */
/*15 */	{ USB_DEVICE(USB_VID_HAUPPAUGE, 0xb704) }, /* DVBT  sw     */
	{ USB_DEVICE(USB_VID_HAUPPAUGE, 0xb753) }, /* ATSC+ sw     */
	{ USB_DEVICE(USB_VID_HAUPPAUGE, 0xb763) }, /* ATSC+ no     */
	{ USB_DEVICE(USB_VID_HAUPPAUGE, 0xb764) }, /* DVBT  no     */
	{ USB_DEVICE(USB_VID_HAUPPAUGE, 0xd853) }, /* ATSC+ sw     */
/*20 */	{ USB_DEVICE(USB_VID_HAUPPAUGE, 0xd854) }, /* DVBT  sw     */
	{ USB_DEVICE(USB_VID_HAUPPAUGE, 0xd863) }, /* ATSC+ no     */
	{ USB_DEVICE(USB_VID_HAUPPAUGE, 0xd864) }, /* DVBT  no     */
	{ USB_DEVICE(USB_VID_HAUPPAUGE, 0xd8d3) }, /* ATSC+ sw     */
	{ USB_DEVICE(USB_VID_HAUPPAUGE, 0xd8d4) }, /* DVBT  sw     */
/*25 */	{ USB_DEVICE(USB_VID_HAUPPAUGE, 0xd8e3) }, /* ATSC+ no     */
	{ USB_DEVICE(USB_VID_HAUPPAUGE, 0xd8e4) }, /* DVBT  no     */
	{ USB_DEVICE(USB_VID_HAUPPAUGE, 0xd8ff) }, /* ATSC+        */
	{ USB_DEVICE(USB_VID_HAUPPAUGE, 0xc612) }, /*     +        */
	{ USB_DEVICE(USB_VID_HAUPPAUGE, 0xc613) }, /* ATSC+        */
/*30 */	{ USB_DEVICE(USB_VID_HAUPPAUGE, 0xc61a) }, /*     + IR     */
	{ USB_DEVICE(USB_VID_HAUPPAUGE, 0xc61b) }, /* ATSC+ IR     */
	{ USB_DEVICE(USB_VID_HAUPPAUGE, 0xb757) }, /* ATSC+DVBT sw */
	{ USB_DEVICE(USB_VID_HAUPPAUGE, 0xb767) }, /* ATSC+DVBT no */
	{}		/* Terminating entry */
};
MODULE_DEVICE_TABLE(usb, mxl111sf_table);


#define MXL111SF_EP4_BULK_STREAMING_CONFIG		\
	.streaming_ctrl = mxl111sf_ep4_streaming_ctrl,	\
	.stream = {					\
		.type = USB_BULK,			\
		.count = 5,				\
		.endpoint = 0x04,			\
		.u = {					\
			.bulk = {			\
				.buffersize = 8192,	\
			}				\
		}					\
	}

/* FIXME: works for v6 but not v8 silicon */
#define MXL111SF_EP4_ISOC_STREAMING_CONFIG		\
	.streaming_ctrl = mxl111sf_ep4_streaming_ctrl,	\
	.stream = {					\
		.type = USB_ISOC,			\
		.count = 5,				\
		.endpoint = 0x04,			\
		.u = {					\
			.isoc = {			\
				.framesperurb = 96,	\
				/* FIXME: v6 SILICON: */	\
				.framesize = 564,	\
				.interval = 1,		\
			}				\
		}					\
	}

#define MXL111SF_EP6_BULK_STREAMING_CONFIG		\
	.streaming_ctrl = mxl111sf_ep6_streaming_ctrl,	\
	.stream = {					\
		.type = USB_BULK,			\
		.count = 5,				\
		.endpoint = 0x06,			\
		.u = {					\
			.bulk = {			\
				.buffersize = 8192,	\
			}				\
		}					\
	}

/* FIXME */
#define MXL111SF_EP6_ISOC_STREAMING_CONFIG		\
	.streaming_ctrl = mxl111sf_ep6_streaming_ctrl,	\
	.stream = {					\
		.type = USB_ISOC,			\
		.count = 5,				\
		.endpoint = 0x06,			\
		.u = {					\
			.isoc = {			\
				.framesperurb = 24,	\
				.framesize = 3072,	\
				.interval = 1,		\
			}				\
		}					\
	}

#define MXL111SF_DEFAULT_DEVICE_PROPERTIES			\
	.caps = DVB_USB_IS_AN_I2C_ADAPTER,			\
	.usb_ctrl = DEVICE_SPECIFIC,				\
	/* use usb alt setting 1 for EP4 ISOC transfer (dvb-t),	\
				     EP6 BULK transfer (atsc/qam), \
	   use usb alt setting 2 for EP4 BULK transfer (dvb-t),	\
				     EP6 ISOC transfer (atsc/qam), \
	*/							\
	.power_ctrl       = mxl111sf_power_ctrl,		\
	.i2c_algo         = &mxl111sf_i2c_algo,			\
	.generic_bulk_ctrl_endpoint          = MXL_EP2_REG_WRITE, \
	.generic_bulk_ctrl_endpoint_response = MXL_EP1_REG_READ, \
	.size_of_priv     = sizeof(struct mxl111sf_state)

static struct dvb_usb_device_properties mxl111sf_dvbt_bulk_properties = {
	MXL111SF_DEFAULT_DEVICE_PROPERTIES,

	.num_adapters = 1,
	.adapter = {
		{
		.fe_ioctl_override = mxl111sf_fe_ioctl_override,
		.num_frontends = 1,
		.fe = {{
			.size_of_priv     = sizeof(struct mxl111sf_adap_state),

			.frontend_attach  = mxl111sf_attach_demod,
			.tuner_attach     = mxl111sf_attach_tuner,

			MXL111SF_EP4_BULK_STREAMING_CONFIG,
		} },
		},
	},
	.num_device_descs = 4,
	.devices = {
		{   "Hauppauge 126xxx DVBT (bulk)",
			{ NULL },
			{ &mxl111sf_table[4], &mxl111sf_table[8],
			  NULL },
		},
		{   "Hauppauge 117xxx DVBT (bulk)",
			{ NULL },
			{ &mxl111sf_table[15], &mxl111sf_table[18],
			  NULL },
		},
		{   "Hauppauge 138xxx DVBT (bulk)",
			{ NULL },
			{ &mxl111sf_table[20], &mxl111sf_table[22],
			  &mxl111sf_table[24], &mxl111sf_table[26],
			  NULL },
		},
		{   "Hauppauge 126xxx (tp-bulk)",
			{ NULL },
			{ &mxl111sf_table[28], &mxl111sf_table[30],
			  NULL },
		},
	}
};

static struct dvb_usb_device_properties mxl111sf_dvbt_isoc_properties = {
	MXL111SF_DEFAULT_DEVICE_PROPERTIES,

	.num_adapters = 1,
	.adapter = {
		{
		.fe_ioctl_override = mxl111sf_fe_ioctl_override,
		.num_frontends = 1,
		.fe = {{
			.size_of_priv     = sizeof(struct mxl111sf_adap_state),

			.frontend_attach  = mxl111sf_attach_demod,
			.tuner_attach     = mxl111sf_attach_tuner,

			MXL111SF_EP4_ISOC_STREAMING_CONFIG,
		} },
		},
	},
	.num_device_descs = 4,
	.devices = {
		{   "Hauppauge 126xxx DVBT (isoc)",
			{ NULL },
			{ &mxl111sf_table[4], &mxl111sf_table[8],
			  NULL },
		},
		{   "Hauppauge 117xxx DVBT (isoc)",
			{ NULL },
			{ &mxl111sf_table[15], &mxl111sf_table[18],
			  NULL },
		},
		{   "Hauppauge 138xxx DVBT (isoc)",
			{ NULL },
			{ &mxl111sf_table[20], &mxl111sf_table[22],
			  &mxl111sf_table[24], &mxl111sf_table[26],
			  NULL },
		},
		{   "Hauppauge 126xxx (tp-isoc)",
			{ NULL },
			{ &mxl111sf_table[28], &mxl111sf_table[30],
			  NULL },
		},
	}
};

static struct dvb_usb_device_properties mxl111sf_atsc_bulk_properties = {
	MXL111SF_DEFAULT_DEVICE_PROPERTIES,

	.num_adapters = 1,
	.adapter = {
		{
		.fe_ioctl_override = mxl111sf_fe_ioctl_override,
		.num_frontends = 2,
		.fe = {{
			.size_of_priv     = sizeof(struct mxl111sf_adap_state),

			.frontend_attach  = mxl111sf_lgdt3305_frontend_attach,
			.tuner_attach     = mxl111sf_attach_tuner,

			MXL111SF_EP6_BULK_STREAMING_CONFIG,
		},
		{
			.size_of_priv     = sizeof(struct mxl111sf_adap_state),

			.frontend_attach  = mxl111sf_attach_demod,
			.tuner_attach     = mxl111sf_attach_tuner,

			MXL111SF_EP4_BULK_STREAMING_CONFIG,
		}},
		},
	},
	.num_device_descs = 6,
	.devices = {
		{   "Hauppauge 126xxx ATSC (bulk)",
			{ NULL },
			{ &mxl111sf_table[1], &mxl111sf_table[5],
			  NULL },
		},
		{   "Hauppauge 117xxx ATSC (bulk)",
			{ NULL },
			{ &mxl111sf_table[12],
			  NULL },
		},
		{   "Hauppauge 126xxx ATSC+ (bulk)",
			{ NULL },
			{ &mxl111sf_table[0], &mxl111sf_table[3],
			  &mxl111sf_table[7], &mxl111sf_table[9],
			  &mxl111sf_table[10], NULL },
		},
		{   "Hauppauge 117xxx ATSC+ (bulk)",
			{ NULL },
			{ &mxl111sf_table[11], &mxl111sf_table[14],
			  &mxl111sf_table[16], &mxl111sf_table[17],
			  &mxl111sf_table[32], &mxl111sf_table[33],
			  NULL },
		},
		{   "Hauppauge Mercury (tp-bulk)",
			{ NULL },
			{ &mxl111sf_table[19], &mxl111sf_table[21],
			  &mxl111sf_table[23], &mxl111sf_table[25],
			  &mxl111sf_table[27], NULL },
		},
		{   "Hauppauge WinTV-Aero-M",
			{ NULL },
			{ &mxl111sf_table[29], &mxl111sf_table[31],
			  NULL },
		},
	}
};

static struct dvb_usb_device_properties mxl111sf_atsc_isoc_properties = {
	MXL111SF_DEFAULT_DEVICE_PROPERTIES,

	.num_adapters = 1,
	.adapter = {
		{
		.fe_ioctl_override = mxl111sf_fe_ioctl_override,
		.num_frontends = 2,
		.fe = {{
			.size_of_priv     = sizeof(struct mxl111sf_adap_state),

			.frontend_attach  = mxl111sf_lgdt3305_frontend_attach,
			.tuner_attach     = mxl111sf_attach_tuner,

			MXL111SF_EP6_ISOC_STREAMING_CONFIG,
		},
		{
			.size_of_priv     = sizeof(struct mxl111sf_adap_state),

			.frontend_attach  = mxl111sf_attach_demod,
			.tuner_attach     = mxl111sf_attach_tuner,

			MXL111SF_EP4_ISOC_STREAMING_CONFIG,
		}},
		},
	},
	.num_device_descs = 6,
	.devices = {
		{   "Hauppauge 126xxx ATSC (isoc)",
			{ NULL },
			{ &mxl111sf_table[1], &mxl111sf_table[5],
			  NULL },
		},
		{   "Hauppauge 117xxx ATSC (isoc)",
			{ NULL },
			{ &mxl111sf_table[12],
			  NULL },
		},
		{   "Hauppauge 126xxx ATSC+ (isoc)",
			{ NULL },
			{ &mxl111sf_table[0], &mxl111sf_table[3],
			  &mxl111sf_table[7], &mxl111sf_table[9],
			  &mxl111sf_table[10], NULL },
		},
		{   "Hauppauge 117xxx ATSC+ (isoc)",
			{ NULL },
			{ &mxl111sf_table[11], &mxl111sf_table[14],
			  &mxl111sf_table[16], &mxl111sf_table[17],
			  &mxl111sf_table[32], &mxl111sf_table[33],
			  NULL },
		},
		{   "Hauppauge Mercury (tp-isoc)",
			{ NULL },
			{ &mxl111sf_table[19], &mxl111sf_table[21],
			  &mxl111sf_table[23], &mxl111sf_table[25],
			  &mxl111sf_table[27], NULL },
		},
		{   "Hauppauge WinTV-Aero-M (tp-isoc)",
			{ NULL },
			{ &mxl111sf_table[29], &mxl111sf_table[31],
			  NULL },
		},
	}
};

static struct usb_driver mxl111sf_driver = {
	.name		= "dvb_usb_mxl111sf",
	.probe		= mxl111sf_probe,
	.disconnect     = dvb_usb_device_exit,
	.id_table	= mxl111sf_table,
};

static int __init mxl111sf_module_init(void)
{
	int result = usb_register(&mxl111sf_driver);
	if (result) {
		err("usb_register failed. Error number %d", result);
		return result;
	}

	return 0;
}

static void __exit mxl111sf_module_exit(void)
{
	usb_deregister(&mxl111sf_driver);
}

module_init(mxl111sf_module_init);
module_exit(mxl111sf_module_exit);

MODULE_AUTHOR("Michael Krufky <mkrufky@kernellabs.com>");
MODULE_DESCRIPTION("Driver for MaxLinear MxL111SF");
MODULE_VERSION("1.0");
MODULE_LICENSE("GPL");

/*
 * Local variables:
 * c-basic-offset: 8
 * End:
 */

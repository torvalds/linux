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
#include "lg2160.h"

/* Max transfer size done by I2C transfer functions */
#define MAX_XFER_SIZE  64

int dvb_usb_mxl111sf_debug;
module_param_named(debug, dvb_usb_mxl111sf_debug, int, 0644);
MODULE_PARM_DESC(debug, "set debugging level "
		 "(1=info, 2=xfer, 4=i2c, 8=reg, 16=adv (or-able)).");

int dvb_usb_mxl111sf_isoc;
module_param_named(isoc, dvb_usb_mxl111sf_isoc, int, 0644);
MODULE_PARM_DESC(isoc, "enable usb isoc xfer (0=bulk, 1=isoc).");

int dvb_usb_mxl111sf_spi;
module_param_named(spi, dvb_usb_mxl111sf_spi, int, 0644);
MODULE_PARM_DESC(spi, "use spi rather than tp for data xfer (0=tp, 1=spi).");

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

int mxl111sf_ctrl_msg(struct dvb_usb_device *d,
		      u8 cmd, u8 *wbuf, int wlen, u8 *rbuf, int rlen)
{
	int wo = (rbuf == NULL || rlen == 0); /* write-only */
	int ret;
	u8 sndbuf[MAX_XFER_SIZE];

	if (1 + wlen > sizeof(sndbuf)) {
		pr_warn("%s: len=%d is too big!\n", __func__, wlen);
		return -EOPNOTSUPP;
	}

	pr_debug("%s(wlen = %d, rlen = %d)\n", __func__, wlen, rlen);

	memset(sndbuf, 0, 1+wlen);

	sndbuf[0] = cmd;
	memcpy(&sndbuf[1], wbuf, wlen);

	ret = (wo) ? dvb_usbv2_generic_write(d, sndbuf, 1+wlen) :
		dvb_usbv2_generic_rw(d, sndbuf, 1+wlen, rbuf, rlen);
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
		pr_err("invalid response reading reg: 0x%02x != 0x%02x, 0x%02x",
		    addr, buf[0], buf[1]);
		ret = -EINVAL;
	}

	pr_debug("R: (0x%02x, 0x%02x)\n", addr, buf[1]);
fail:
	return ret;
}

int mxl111sf_write_reg(struct mxl111sf_state *state, u8 addr, u8 data)
{
	u8 buf[] = { addr, data };
	int ret;

	pr_debug("W: (0x%02x, 0x%02x)\n", addr, data);

	ret = mxl111sf_ctrl_msg(state->d, MXL_CMD_REG_WRITE, buf, 2, NULL, 0);
	if (mxl_fail(ret))
		pr_err("error writing reg: 0x%02x, val: 0x%02x", addr, data);
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
			pr_err("error writing addr: 0x%02x, mask: 0x%02x, "
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
			pr_err("failed on reg #%d (0x%02x)", i,
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
	pr_info("%s detected, %s (0x%x)", mxl_chip, mxl_rev, ver);
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
			pr_err("failed to get chip info during probe");	\
		else							\
			mxl_debug("probe needed a retry "		\
				  "in order to succeed.");		\
	}								\
	___ret;								\
})

/* ------------------------------------------------------------------------ */
#if 0
static int mxl111sf_power_ctrl(struct dvb_usb_device *d, int onoff)
{
	/* power control depends on which adapter is being woken:
	 * save this for init, instead, via mxl111sf_adap_fe_init */
	return 0;
}
#endif

static int mxl111sf_adap_fe_init(struct dvb_frontend *fe)
{
	struct dvb_usb_device *d = fe_to_d(fe);
	struct mxl111sf_state *state = fe_to_priv(fe);
	struct mxl111sf_adap_state *adap_state = &state->adap_state[fe->id];
	int err;

	/* exit if we didn't initialize the driver yet */
	if (!state->chip_id) {
		mxl_debug("driver not yet initialized, exit.");
		goto fail;
	}

	pr_debug("%s()\n", __func__);

	mutex_lock(&state->fe_lock);

	state->alt_mode = adap_state->alt_mode;

	if (usb_set_interface(d->udev, 0, state->alt_mode) < 0)
		pr_err("set interface failed");

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
	struct mxl111sf_state *state = fe_to_priv(fe);
	struct mxl111sf_adap_state *adap_state = &state->adap_state[fe->id];
	int err;

	/* exit if we didn't initialize the driver yet */
	if (!state->chip_id) {
		mxl_debug("driver not yet initialized, exit.");
		goto fail;
	}

	pr_debug("%s()\n", __func__);

	err = (adap_state->fe_sleep) ? adap_state->fe_sleep(fe) : 0;

	mutex_unlock(&state->fe_lock);

	return err;
fail:
	return -ENODEV;
}


static int mxl111sf_ep6_streaming_ctrl(struct dvb_frontend *fe, int onoff)
{
	struct mxl111sf_state *state = fe_to_priv(fe);
	struct mxl111sf_adap_state *adap_state = &state->adap_state[fe->id];
	int ret = 0;

	pr_debug("%s(%d)\n", __func__, onoff);

	if (onoff) {
		ret = mxl111sf_enable_usb_output(state);
		mxl_fail(ret);
		ret = mxl111sf_config_mpeg_in(state, 1, 1,
					      adap_state->ep6_clockphase,
					      0, 0);
		mxl_fail(ret);
#if 0
	} else {
		ret = mxl111sf_disable_656_port(state);
		mxl_fail(ret);
#endif
	}

	return ret;
}

static int mxl111sf_ep5_streaming_ctrl(struct dvb_frontend *fe, int onoff)
{
	struct mxl111sf_state *state = fe_to_priv(fe);
	int ret = 0;

	pr_debug("%s(%d)\n", __func__, onoff);

	if (onoff) {
		ret = mxl111sf_enable_usb_output(state);
		mxl_fail(ret);

		ret = mxl111sf_init_i2s_port(state, 200);
		mxl_fail(ret);
		ret = mxl111sf_config_i2s(state, 0, 15);
		mxl_fail(ret);
	} else {
		ret = mxl111sf_disable_i2s_port(state);
		mxl_fail(ret);
	}
	if (state->chip_rev > MXL111SF_V6)
		ret = mxl111sf_config_spi(state, onoff);
	mxl_fail(ret);

	return ret;
}

static int mxl111sf_ep4_streaming_ctrl(struct dvb_frontend *fe, int onoff)
{
	struct mxl111sf_state *state = fe_to_priv(fe);
	int ret = 0;

	pr_debug("%s(%d)\n", __func__, onoff);

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

static int mxl111sf_lgdt3305_frontend_attach(struct dvb_usb_adapter *adap, u8 fe_id)
{
	struct dvb_usb_device *d = adap_to_d(adap);
	struct mxl111sf_state *state = d_to_priv(d);
	struct mxl111sf_adap_state *adap_state = &state->adap_state[fe_id];
	int ret;

	pr_debug("%s()\n", __func__);

	/* save a pointer to the dvb_usb_device in device state */
	state->d = d;
	adap_state->alt_mode = (dvb_usb_mxl111sf_isoc) ? 2 : 1;
	state->alt_mode = adap_state->alt_mode;

	if (usb_set_interface(d->udev, 0, state->alt_mode) < 0)
		pr_err("set interface failed");

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

	adap->fe[fe_id] = dvb_attach(lgdt3305_attach,
				 &hauppauge_lgdt3305_config,
				 &d->i2c_adap);
	if (adap->fe[fe_id]) {
		state->num_frontends++;
		adap_state->fe_init = adap->fe[fe_id]->ops.init;
		adap->fe[fe_id]->ops.init = mxl111sf_adap_fe_init;
		adap_state->fe_sleep = adap->fe[fe_id]->ops.sleep;
		adap->fe[fe_id]->ops.sleep = mxl111sf_adap_fe_sleep;
		return 0;
	}
	ret = -EIO;
fail:
	return ret;
}

static struct lg2160_config hauppauge_lg2160_config = {
	.lg_chip            = LG2160,
	.i2c_addr           = 0x1c >> 1,
	.deny_i2c_rptr      = 1,
	.spectral_inversion = 0,
	.if_khz             = 6000,
};

static int mxl111sf_lg2160_frontend_attach(struct dvb_usb_adapter *adap, u8 fe_id)
{
	struct dvb_usb_device *d = adap_to_d(adap);
	struct mxl111sf_state *state = d_to_priv(d);
	struct mxl111sf_adap_state *adap_state = &state->adap_state[fe_id];
	int ret;

	pr_debug("%s()\n", __func__);

	/* save a pointer to the dvb_usb_device in device state */
	state->d = d;
	adap_state->alt_mode = (dvb_usb_mxl111sf_isoc) ? 2 : 1;
	state->alt_mode = adap_state->alt_mode;

	if (usb_set_interface(d->udev, 0, state->alt_mode) < 0)
		pr_err("set interface failed");

	state->gpio_mode = MXL111SF_GPIO_MOD_MH;
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

	ret = get_chip_info(state);
	if (mxl_fail(ret))
		goto fail;

	adap->fe[fe_id] = dvb_attach(lg2160_attach,
			      &hauppauge_lg2160_config,
			      &d->i2c_adap);
	if (adap->fe[fe_id]) {
		state->num_frontends++;
		adap_state->fe_init = adap->fe[fe_id]->ops.init;
		adap->fe[fe_id]->ops.init = mxl111sf_adap_fe_init;
		adap_state->fe_sleep = adap->fe[fe_id]->ops.sleep;
		adap->fe[fe_id]->ops.sleep = mxl111sf_adap_fe_sleep;
		return 0;
	}
	ret = -EIO;
fail:
	return ret;
}

static struct lg2160_config hauppauge_lg2161_1019_config = {
	.lg_chip            = LG2161_1019,
	.i2c_addr           = 0x1c >> 1,
	.deny_i2c_rptr      = 1,
	.spectral_inversion = 0,
	.if_khz             = 6000,
	.output_if          = 2, /* LG2161_OIF_SPI_MAS */
};

static struct lg2160_config hauppauge_lg2161_1040_config = {
	.lg_chip            = LG2161_1040,
	.i2c_addr           = 0x1c >> 1,
	.deny_i2c_rptr      = 1,
	.spectral_inversion = 0,
	.if_khz             = 6000,
	.output_if          = 4, /* LG2161_OIF_SPI_MAS */
};

static int mxl111sf_lg2161_frontend_attach(struct dvb_usb_adapter *adap, u8 fe_id)
{
	struct dvb_usb_device *d = adap_to_d(adap);
	struct mxl111sf_state *state = d_to_priv(d);
	struct mxl111sf_adap_state *adap_state = &state->adap_state[fe_id];
	int ret;

	pr_debug("%s()\n", __func__);

	/* save a pointer to the dvb_usb_device in device state */
	state->d = d;
	adap_state->alt_mode = (dvb_usb_mxl111sf_isoc) ? 2 : 1;
	state->alt_mode = adap_state->alt_mode;

	if (usb_set_interface(d->udev, 0, state->alt_mode) < 0)
		pr_err("set interface failed");

	state->gpio_mode = MXL111SF_GPIO_MOD_MH;
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

	ret = get_chip_info(state);
	if (mxl_fail(ret))
		goto fail;

	adap->fe[fe_id] = dvb_attach(lg2160_attach,
			      (MXL111SF_V8_200 == state->chip_rev) ?
			      &hauppauge_lg2161_1040_config :
			      &hauppauge_lg2161_1019_config,
			      &d->i2c_adap);
	if (adap->fe[fe_id]) {
		state->num_frontends++;
		adap_state->fe_init = adap->fe[fe_id]->ops.init;
		adap->fe[fe_id]->ops.init = mxl111sf_adap_fe_init;
		adap_state->fe_sleep = adap->fe[fe_id]->ops.sleep;
		adap->fe[fe_id]->ops.sleep = mxl111sf_adap_fe_sleep;
		return 0;
	}
	ret = -EIO;
fail:
	return ret;
}

static struct lg2160_config hauppauge_lg2161_1019_ep6_config = {
	.lg_chip            = LG2161_1019,
	.i2c_addr           = 0x1c >> 1,
	.deny_i2c_rptr      = 1,
	.spectral_inversion = 0,
	.if_khz             = 6000,
	.output_if          = 1, /* LG2161_OIF_SERIAL_TS */
};

static struct lg2160_config hauppauge_lg2161_1040_ep6_config = {
	.lg_chip            = LG2161_1040,
	.i2c_addr           = 0x1c >> 1,
	.deny_i2c_rptr      = 1,
	.spectral_inversion = 0,
	.if_khz             = 6000,
	.output_if          = 7, /* LG2161_OIF_SERIAL_TS */
};

static int mxl111sf_lg2161_ep6_frontend_attach(struct dvb_usb_adapter *adap, u8 fe_id)
{
	struct dvb_usb_device *d = adap_to_d(adap);
	struct mxl111sf_state *state = d_to_priv(d);
	struct mxl111sf_adap_state *adap_state = &state->adap_state[fe_id];
	int ret;

	pr_debug("%s()\n", __func__);

	/* save a pointer to the dvb_usb_device in device state */
	state->d = d;
	adap_state->alt_mode = (dvb_usb_mxl111sf_isoc) ? 2 : 1;
	state->alt_mode = adap_state->alt_mode;

	if (usb_set_interface(d->udev, 0, state->alt_mode) < 0)
		pr_err("set interface failed");

	state->gpio_mode = MXL111SF_GPIO_MOD_MH;
	adap_state->gpio_mode = state->gpio_mode;
	adap_state->device_mode = MXL_TUNER_MODE;
	adap_state->ep6_clockphase = 0;

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

	ret = get_chip_info(state);
	if (mxl_fail(ret))
		goto fail;

	adap->fe[fe_id] = dvb_attach(lg2160_attach,
			      (MXL111SF_V8_200 == state->chip_rev) ?
			      &hauppauge_lg2161_1040_ep6_config :
			      &hauppauge_lg2161_1019_ep6_config,
			      &d->i2c_adap);
	if (adap->fe[fe_id]) {
		state->num_frontends++;
		adap_state->fe_init = adap->fe[fe_id]->ops.init;
		adap->fe[fe_id]->ops.init = mxl111sf_adap_fe_init;
		adap_state->fe_sleep = adap->fe[fe_id]->ops.sleep;
		adap->fe[fe_id]->ops.sleep = mxl111sf_adap_fe_sleep;
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

static int mxl111sf_attach_demod(struct dvb_usb_adapter *adap, u8 fe_id)
{
	struct dvb_usb_device *d = adap_to_d(adap);
	struct mxl111sf_state *state = d_to_priv(d);
	struct mxl111sf_adap_state *adap_state = &state->adap_state[fe_id];
	int ret;

	pr_debug("%s()\n", __func__);

	/* save a pointer to the dvb_usb_device in device state */
	state->d = d;
	adap_state->alt_mode = (dvb_usb_mxl111sf_isoc) ? 1 : 2;
	state->alt_mode = adap_state->alt_mode;

	if (usb_set_interface(d->udev, 0, state->alt_mode) < 0)
		pr_err("set interface failed");

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

	adap->fe[fe_id] = dvb_attach(mxl111sf_demod_attach, state,
			      &mxl_demod_config);
	if (adap->fe[fe_id]) {
		state->num_frontends++;
		adap_state->fe_init = adap->fe[fe_id]->ops.init;
		adap->fe[fe_id]->ops.init = mxl111sf_adap_fe_init;
		adap_state->fe_sleep = adap->fe[fe_id]->ops.sleep;
		adap->fe[fe_id]->ops.sleep = mxl111sf_adap_fe_sleep;
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
	pr_err("%s(%d) FINAL input set to %s rxPwr:%d|%d|%d|%d\n", \
	    __func__, __LINE__, \
	    (ANT_PATH_EXTERNAL == x) ? "EXTERNAL" : "INTERNAL", \
	    pwr0, pwr1, pwr2, pwr3)

#define ANT_HUNT_SLEEP 90
#define ANT_EXT_TWEAK 0

static int mxl111sf_ant_hunt(struct dvb_frontend *fe)
{
	struct mxl111sf_state *state = fe_to_priv(fe);
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
	struct mxl111sf_state *state = adap_to_priv(adap);
	int i;

	pr_debug("%s()\n", __func__);

	for (i = 0; i < state->num_frontends; i++) {
		if (dvb_attach(mxl111sf_tuner_attach, adap->fe[i], state,
				&mxl_tuner_config) == NULL)
			return -EIO;
		adap->fe[i]->ops.read_signal_strength = adap->fe[i]->ops.tuner_ops.get_rf_strength;
	}

	return 0;
}

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

static int mxl111sf_init(struct dvb_usb_device *d)
{
	struct mxl111sf_state *state = d_to_priv(d);
	int ret;
	static u8 eeprom[256];
	struct i2c_client c;

	ret = get_chip_info(state);
	if (mxl_fail(ret))
		pr_err("failed to get chip info during probe");

	mutex_init(&state->fe_lock);

	if (state->chip_rev > MXL111SF_V6)
		mxl111sf_config_pin_mux_modes(state, PIN_MUX_TS_SPI_IN_MODE_1);

	c.adapter = &d->i2c_adap;
	c.addr = 0xa0 >> 1;

	ret = tveeprom_read(&c, eeprom, sizeof(eeprom));
	if (mxl_fail(ret))
		return 0;
	tveeprom_hauppauge_analog(&c, &state->tv, (0x84 == eeprom[0xa0]) ?
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

static int mxl111sf_frontend_attach_dvbt(struct dvb_usb_adapter *adap)
{
	return mxl111sf_attach_demod(adap, 0);
}

static int mxl111sf_frontend_attach_atsc(struct dvb_usb_adapter *adap)
{
	return mxl111sf_lgdt3305_frontend_attach(adap, 0);
}

static int mxl111sf_frontend_attach_mh(struct dvb_usb_adapter *adap)
{
	return mxl111sf_lg2160_frontend_attach(adap, 0);
}

static int mxl111sf_frontend_attach_atsc_mh(struct dvb_usb_adapter *adap)
{
	int ret;
	pr_debug("%s\n", __func__);

	ret = mxl111sf_lgdt3305_frontend_attach(adap, 0);
	if (ret < 0)
		return ret;

	ret = mxl111sf_attach_demod(adap, 1);
	if (ret < 0)
		return ret;

	ret = mxl111sf_lg2160_frontend_attach(adap, 2);
	if (ret < 0)
		return ret;

	return ret;
}

static int mxl111sf_frontend_attach_mercury(struct dvb_usb_adapter *adap)
{
	int ret;
	pr_debug("%s\n", __func__);

	ret = mxl111sf_lgdt3305_frontend_attach(adap, 0);
	if (ret < 0)
		return ret;

	ret = mxl111sf_attach_demod(adap, 1);
	if (ret < 0)
		return ret;

	ret = mxl111sf_lg2161_ep6_frontend_attach(adap, 2);
	if (ret < 0)
		return ret;

	return ret;
}

static int mxl111sf_frontend_attach_mercury_mh(struct dvb_usb_adapter *adap)
{
	int ret;
	pr_debug("%s\n", __func__);

	ret = mxl111sf_attach_demod(adap, 0);
	if (ret < 0)
		return ret;

	if (dvb_usb_mxl111sf_spi)
		ret = mxl111sf_lg2161_frontend_attach(adap, 1);
	else
		ret = mxl111sf_lg2161_ep6_frontend_attach(adap, 1);

	return ret;
}

static void mxl111sf_stream_config_bulk(struct usb_data_stream_properties *stream, u8 endpoint)
{
	pr_debug("%s: endpoint=%d size=8192\n", __func__, endpoint);
	stream->type = USB_BULK;
	stream->count = 5;
	stream->endpoint = endpoint;
	stream->u.bulk.buffersize = 8192;
}

static void mxl111sf_stream_config_isoc(struct usb_data_stream_properties *stream,
		u8 endpoint, int framesperurb, int framesize)
{
	pr_debug("%s: endpoint=%d size=%d\n", __func__, endpoint,
			framesperurb * framesize);
	stream->type = USB_ISOC;
	stream->count = 5;
	stream->endpoint = endpoint;
	stream->u.isoc.framesperurb = framesperurb;
	stream->u.isoc.framesize = framesize;
	stream->u.isoc.interval = 1;
}

/* DVB USB Driver stuff */

/* dvbt       mxl111sf
 * bulk       EP4/BULK/5/8192
 * isoc       EP4/ISOC/5/96/564
 */
static int mxl111sf_get_stream_config_dvbt(struct dvb_frontend *fe,
		u8 *ts_type, struct usb_data_stream_properties *stream)
{
	pr_debug("%s: fe=%d\n", __func__, fe->id);

	*ts_type = DVB_USB_FE_TS_TYPE_188;
	if (dvb_usb_mxl111sf_isoc)
		mxl111sf_stream_config_isoc(stream, 4, 96, 564);
	else
		mxl111sf_stream_config_bulk(stream, 4);
	return 0;
}

static struct dvb_usb_device_properties mxl111sf_props_dvbt = {
	.driver_name = KBUILD_MODNAME,
	.owner = THIS_MODULE,
	.adapter_nr = adapter_nr,
	.size_of_priv = sizeof(struct mxl111sf_state),

	.generic_bulk_ctrl_endpoint = 0x02,
	.generic_bulk_ctrl_endpoint_response = 0x81,

	.i2c_algo          = &mxl111sf_i2c_algo,
	.frontend_attach   = mxl111sf_frontend_attach_dvbt,
	.tuner_attach      = mxl111sf_attach_tuner,
	.init              = mxl111sf_init,
	.streaming_ctrl    = mxl111sf_ep4_streaming_ctrl,
	.get_stream_config = mxl111sf_get_stream_config_dvbt,

	.num_adapters = 1,
	.adapter = {
		{
			.stream = DVB_USB_STREAM_ISOC(6, 5, 24, 3072, 1),
		}
	}
};

/* atsc       lgdt3305
 * bulk       EP6/BULK/5/8192
 * isoc       EP6/ISOC/5/24/3072
 */
static int mxl111sf_get_stream_config_atsc(struct dvb_frontend *fe,
		u8 *ts_type, struct usb_data_stream_properties *stream)
{
	pr_debug("%s: fe=%d\n", __func__, fe->id);

	*ts_type = DVB_USB_FE_TS_TYPE_188;
	if (dvb_usb_mxl111sf_isoc)
		mxl111sf_stream_config_isoc(stream, 6, 24, 3072);
	else
		mxl111sf_stream_config_bulk(stream, 6);
	return 0;
}

static struct dvb_usb_device_properties mxl111sf_props_atsc = {
	.driver_name = KBUILD_MODNAME,
	.owner = THIS_MODULE,
	.adapter_nr = adapter_nr,
	.size_of_priv = sizeof(struct mxl111sf_state),

	.generic_bulk_ctrl_endpoint = 0x02,
	.generic_bulk_ctrl_endpoint_response = 0x81,

	.i2c_algo          = &mxl111sf_i2c_algo,
	.frontend_attach   = mxl111sf_frontend_attach_atsc,
	.tuner_attach      = mxl111sf_attach_tuner,
	.init              = mxl111sf_init,
	.streaming_ctrl    = mxl111sf_ep6_streaming_ctrl,
	.get_stream_config = mxl111sf_get_stream_config_atsc,

	.num_adapters = 1,
	.adapter = {
		{
			.stream = DVB_USB_STREAM_ISOC(6, 5, 24, 3072, 1),
		}
	}
};

/* mh         lg2160
 * bulk       EP5/BULK/5/8192/RAW
 * isoc       EP5/ISOC/5/96/200/RAW
 */
static int mxl111sf_get_stream_config_mh(struct dvb_frontend *fe,
		u8 *ts_type, struct usb_data_stream_properties *stream)
{
	pr_debug("%s: fe=%d\n", __func__, fe->id);

	*ts_type = DVB_USB_FE_TS_TYPE_RAW;
	if (dvb_usb_mxl111sf_isoc)
		mxl111sf_stream_config_isoc(stream, 5, 96, 200);
	else
		mxl111sf_stream_config_bulk(stream, 5);
	return 0;
}

static struct dvb_usb_device_properties mxl111sf_props_mh = {
	.driver_name = KBUILD_MODNAME,
	.owner = THIS_MODULE,
	.adapter_nr = adapter_nr,
	.size_of_priv = sizeof(struct mxl111sf_state),

	.generic_bulk_ctrl_endpoint = 0x02,
	.generic_bulk_ctrl_endpoint_response = 0x81,

	.i2c_algo          = &mxl111sf_i2c_algo,
	.frontend_attach   = mxl111sf_frontend_attach_mh,
	.tuner_attach      = mxl111sf_attach_tuner,
	.init              = mxl111sf_init,
	.streaming_ctrl    = mxl111sf_ep5_streaming_ctrl,
	.get_stream_config = mxl111sf_get_stream_config_mh,

	.num_adapters = 1,
	.adapter = {
		{
			.stream = DVB_USB_STREAM_ISOC(6, 5, 24, 3072, 1),
		}
	}
};

/* atsc mh    lgdt3305           mxl111sf          lg2160
 * bulk       EP6/BULK/5/8192    EP4/BULK/5/8192   EP5/BULK/5/8192/RAW
 * isoc       EP6/ISOC/5/24/3072 EP4/ISOC/5/96/564 EP5/ISOC/5/96/200/RAW
 */
static int mxl111sf_get_stream_config_atsc_mh(struct dvb_frontend *fe,
		u8 *ts_type, struct usb_data_stream_properties *stream)
{
	pr_debug("%s: fe=%d\n", __func__, fe->id);

	if (fe->id == 0) {
		*ts_type = DVB_USB_FE_TS_TYPE_188;
		if (dvb_usb_mxl111sf_isoc)
			mxl111sf_stream_config_isoc(stream, 6, 24, 3072);
		else
			mxl111sf_stream_config_bulk(stream, 6);
	} else if (fe->id == 1) {
		*ts_type = DVB_USB_FE_TS_TYPE_188;
		if (dvb_usb_mxl111sf_isoc)
			mxl111sf_stream_config_isoc(stream, 4, 96, 564);
		else
			mxl111sf_stream_config_bulk(stream, 4);
	} else if (fe->id == 2) {
		*ts_type = DVB_USB_FE_TS_TYPE_RAW;
		if (dvb_usb_mxl111sf_isoc)
			mxl111sf_stream_config_isoc(stream, 5, 96, 200);
		else
			mxl111sf_stream_config_bulk(stream, 5);
	}
	return 0;
}

static int mxl111sf_streaming_ctrl_atsc_mh(struct dvb_frontend *fe, int onoff)
{
	pr_debug("%s: fe=%d onoff=%d\n", __func__, fe->id, onoff);

	if (fe->id == 0)
		return mxl111sf_ep6_streaming_ctrl(fe, onoff);
	else if (fe->id == 1)
		return mxl111sf_ep4_streaming_ctrl(fe, onoff);
	else if (fe->id == 2)
		return mxl111sf_ep5_streaming_ctrl(fe, onoff);
	return 0;
}

static struct dvb_usb_device_properties mxl111sf_props_atsc_mh = {
	.driver_name = KBUILD_MODNAME,
	.owner = THIS_MODULE,
	.adapter_nr = adapter_nr,
	.size_of_priv = sizeof(struct mxl111sf_state),

	.generic_bulk_ctrl_endpoint = 0x02,
	.generic_bulk_ctrl_endpoint_response = 0x81,

	.i2c_algo          = &mxl111sf_i2c_algo,
	.frontend_attach   = mxl111sf_frontend_attach_atsc_mh,
	.tuner_attach      = mxl111sf_attach_tuner,
	.init              = mxl111sf_init,
	.streaming_ctrl    = mxl111sf_streaming_ctrl_atsc_mh,
	.get_stream_config = mxl111sf_get_stream_config_atsc_mh,

	.num_adapters = 1,
	.adapter = {
		{
			.stream = DVB_USB_STREAM_ISOC(6, 5, 24, 3072, 1),
		}
	}
};

/* mercury    lgdt3305           mxl111sf          lg2161
 * tp bulk    EP6/BULK/5/8192    EP4/BULK/5/8192   EP6/BULK/5/8192/RAW
 * tp isoc    EP6/ISOC/5/24/3072 EP4/ISOC/5/96/564 EP6/ISOC/5/24/3072/RAW
 * spi bulk   EP6/BULK/5/8192    EP4/BULK/5/8192   EP5/BULK/5/8192/RAW
 * spi isoc   EP6/ISOC/5/24/3072 EP4/ISOC/5/96/564 EP5/ISOC/5/96/200/RAW
 */
static int mxl111sf_get_stream_config_mercury(struct dvb_frontend *fe,
		u8 *ts_type, struct usb_data_stream_properties *stream)
{
	pr_debug("%s: fe=%d\n", __func__, fe->id);

	if (fe->id == 0) {
		*ts_type = DVB_USB_FE_TS_TYPE_188;
		if (dvb_usb_mxl111sf_isoc)
			mxl111sf_stream_config_isoc(stream, 6, 24, 3072);
		else
			mxl111sf_stream_config_bulk(stream, 6);
	} else if (fe->id == 1) {
		*ts_type = DVB_USB_FE_TS_TYPE_188;
		if (dvb_usb_mxl111sf_isoc)
			mxl111sf_stream_config_isoc(stream, 4, 96, 564);
		else
			mxl111sf_stream_config_bulk(stream, 4);
	} else if (fe->id == 2 && dvb_usb_mxl111sf_spi) {
		*ts_type = DVB_USB_FE_TS_TYPE_RAW;
		if (dvb_usb_mxl111sf_isoc)
			mxl111sf_stream_config_isoc(stream, 5, 96, 200);
		else
			mxl111sf_stream_config_bulk(stream, 5);
	} else if (fe->id == 2 && !dvb_usb_mxl111sf_spi) {
		*ts_type = DVB_USB_FE_TS_TYPE_RAW;
		if (dvb_usb_mxl111sf_isoc)
			mxl111sf_stream_config_isoc(stream, 6, 24, 3072);
		else
			mxl111sf_stream_config_bulk(stream, 6);
	}
	return 0;
}

static int mxl111sf_streaming_ctrl_mercury(struct dvb_frontend *fe, int onoff)
{
	pr_debug("%s: fe=%d onoff=%d\n", __func__, fe->id, onoff);

	if (fe->id == 0)
		return mxl111sf_ep6_streaming_ctrl(fe, onoff);
	else if (fe->id == 1)
		return mxl111sf_ep4_streaming_ctrl(fe, onoff);
	else if (fe->id == 2 && dvb_usb_mxl111sf_spi)
		return mxl111sf_ep5_streaming_ctrl(fe, onoff);
	else if (fe->id == 2 && !dvb_usb_mxl111sf_spi)
		return mxl111sf_ep6_streaming_ctrl(fe, onoff);
	return 0;
}

static struct dvb_usb_device_properties mxl111sf_props_mercury = {
	.driver_name = KBUILD_MODNAME,
	.owner = THIS_MODULE,
	.adapter_nr = adapter_nr,
	.size_of_priv = sizeof(struct mxl111sf_state),

	.generic_bulk_ctrl_endpoint = 0x02,
	.generic_bulk_ctrl_endpoint_response = 0x81,

	.i2c_algo          = &mxl111sf_i2c_algo,
	.frontend_attach   = mxl111sf_frontend_attach_mercury,
	.tuner_attach      = mxl111sf_attach_tuner,
	.init              = mxl111sf_init,
	.streaming_ctrl    = mxl111sf_streaming_ctrl_mercury,
	.get_stream_config = mxl111sf_get_stream_config_mercury,

	.num_adapters = 1,
	.adapter = {
		{
			.stream = DVB_USB_STREAM_ISOC(6, 5, 24, 3072, 1),
		}
	}
};

/* mercury mh mxl111sf          lg2161
 * tp bulk    EP4/BULK/5/8192   EP6/BULK/5/8192/RAW
 * tp isoc    EP4/ISOC/5/96/564 EP6/ISOC/5/24/3072/RAW
 * spi bulk   EP4/BULK/5/8192   EP5/BULK/5/8192/RAW
 * spi isoc   EP4/ISOC/5/96/564 EP5/ISOC/5/96/200/RAW
 */
static int mxl111sf_get_stream_config_mercury_mh(struct dvb_frontend *fe,
		u8 *ts_type, struct usb_data_stream_properties *stream)
{
	pr_debug("%s: fe=%d\n", __func__, fe->id);

	if (fe->id == 0) {
		*ts_type = DVB_USB_FE_TS_TYPE_188;
		if (dvb_usb_mxl111sf_isoc)
			mxl111sf_stream_config_isoc(stream, 4, 96, 564);
		else
			mxl111sf_stream_config_bulk(stream, 4);
	} else if (fe->id == 1 && dvb_usb_mxl111sf_spi) {
		*ts_type = DVB_USB_FE_TS_TYPE_RAW;
		if (dvb_usb_mxl111sf_isoc)
			mxl111sf_stream_config_isoc(stream, 5, 96, 200);
		else
			mxl111sf_stream_config_bulk(stream, 5);
	} else if (fe->id == 1 && !dvb_usb_mxl111sf_spi) {
		*ts_type = DVB_USB_FE_TS_TYPE_RAW;
		if (dvb_usb_mxl111sf_isoc)
			mxl111sf_stream_config_isoc(stream, 6, 24, 3072);
		else
			mxl111sf_stream_config_bulk(stream, 6);
	}
	return 0;
}

static int mxl111sf_streaming_ctrl_mercury_mh(struct dvb_frontend *fe, int onoff)
{
	pr_debug("%s: fe=%d onoff=%d\n", __func__, fe->id, onoff);

	if (fe->id == 0)
		return mxl111sf_ep4_streaming_ctrl(fe, onoff);
	else if (fe->id == 1  && dvb_usb_mxl111sf_spi)
		return mxl111sf_ep5_streaming_ctrl(fe, onoff);
	else if (fe->id == 1 && !dvb_usb_mxl111sf_spi)
		return mxl111sf_ep6_streaming_ctrl(fe, onoff);
	return 0;
}

static struct dvb_usb_device_properties mxl111sf_props_mercury_mh = {
	.driver_name = KBUILD_MODNAME,
	.owner = THIS_MODULE,
	.adapter_nr = adapter_nr,
	.size_of_priv = sizeof(struct mxl111sf_state),

	.generic_bulk_ctrl_endpoint = 0x02,
	.generic_bulk_ctrl_endpoint_response = 0x81,

	.i2c_algo          = &mxl111sf_i2c_algo,
	.frontend_attach   = mxl111sf_frontend_attach_mercury_mh,
	.tuner_attach      = mxl111sf_attach_tuner,
	.init              = mxl111sf_init,
	.streaming_ctrl    = mxl111sf_streaming_ctrl_mercury_mh,
	.get_stream_config = mxl111sf_get_stream_config_mercury_mh,

	.num_adapters = 1,
	.adapter = {
		{
			.stream = DVB_USB_STREAM_ISOC(6, 5, 24, 3072, 1),
		}
	}
};

static const struct usb_device_id mxl111sf_id_table[] = {
	{ DVB_USB_DEVICE(USB_VID_HAUPPAUGE, 0xc600, &mxl111sf_props_atsc_mh, "Hauppauge 126xxx ATSC+", NULL) },
	{ DVB_USB_DEVICE(USB_VID_HAUPPAUGE, 0xc601, &mxl111sf_props_atsc, "Hauppauge 126xxx ATSC", NULL) },
	{ DVB_USB_DEVICE(USB_VID_HAUPPAUGE, 0xc602, &mxl111sf_props_mh, "HCW 126xxx", NULL) },
	{ DVB_USB_DEVICE(USB_VID_HAUPPAUGE, 0xc603, &mxl111sf_props_atsc_mh, "Hauppauge 126xxx ATSC+", NULL) },
	{ DVB_USB_DEVICE(USB_VID_HAUPPAUGE, 0xc604, &mxl111sf_props_dvbt, "Hauppauge 126xxx DVBT", NULL) },
	{ DVB_USB_DEVICE(USB_VID_HAUPPAUGE, 0xc609, &mxl111sf_props_atsc, "Hauppauge 126xxx ATSC", NULL) },
	{ DVB_USB_DEVICE(USB_VID_HAUPPAUGE, 0xc60a, &mxl111sf_props_mh, "HCW 126xxx", NULL) },
	{ DVB_USB_DEVICE(USB_VID_HAUPPAUGE, 0xc60b, &mxl111sf_props_atsc_mh, "Hauppauge 126xxx ATSC+", NULL) },
	{ DVB_USB_DEVICE(USB_VID_HAUPPAUGE, 0xc60c, &mxl111sf_props_dvbt, "Hauppauge 126xxx DVBT", NULL) },
	{ DVB_USB_DEVICE(USB_VID_HAUPPAUGE, 0xc653, &mxl111sf_props_atsc_mh, "Hauppauge 126xxx ATSC+", NULL) },
	{ DVB_USB_DEVICE(USB_VID_HAUPPAUGE, 0xc65b, &mxl111sf_props_atsc_mh, "Hauppauge 126xxx ATSC+", NULL) },
	{ DVB_USB_DEVICE(USB_VID_HAUPPAUGE, 0xb700, &mxl111sf_props_atsc_mh, "Hauppauge 117xxx ATSC+", NULL) },
	{ DVB_USB_DEVICE(USB_VID_HAUPPAUGE, 0xb701, &mxl111sf_props_atsc, "Hauppauge 126xxx ATSC", NULL) },
	{ DVB_USB_DEVICE(USB_VID_HAUPPAUGE, 0xb702, &mxl111sf_props_mh, "HCW 117xxx", NULL) },
	{ DVB_USB_DEVICE(USB_VID_HAUPPAUGE, 0xb703, &mxl111sf_props_atsc_mh, "Hauppauge 117xxx ATSC+", NULL) },
	{ DVB_USB_DEVICE(USB_VID_HAUPPAUGE, 0xb704, &mxl111sf_props_dvbt, "Hauppauge 117xxx DVBT", NULL) },
	{ DVB_USB_DEVICE(USB_VID_HAUPPAUGE, 0xb753, &mxl111sf_props_atsc_mh, "Hauppauge 117xxx ATSC+", NULL) },
	{ DVB_USB_DEVICE(USB_VID_HAUPPAUGE, 0xb763, &mxl111sf_props_atsc_mh, "Hauppauge 117xxx ATSC+", NULL) },
	{ DVB_USB_DEVICE(USB_VID_HAUPPAUGE, 0xb764, &mxl111sf_props_dvbt, "Hauppauge 117xxx DVBT", NULL) },
	{ DVB_USB_DEVICE(USB_VID_HAUPPAUGE, 0xd853, &mxl111sf_props_mercury, "Hauppauge Mercury", NULL) },
	{ DVB_USB_DEVICE(USB_VID_HAUPPAUGE, 0xd854, &mxl111sf_props_dvbt, "Hauppauge 138xxx DVBT", NULL) },
	{ DVB_USB_DEVICE(USB_VID_HAUPPAUGE, 0xd863, &mxl111sf_props_mercury, "Hauppauge Mercury", NULL) },
	{ DVB_USB_DEVICE(USB_VID_HAUPPAUGE, 0xd864, &mxl111sf_props_dvbt, "Hauppauge 138xxx DVBT", NULL) },
	{ DVB_USB_DEVICE(USB_VID_HAUPPAUGE, 0xd8d3, &mxl111sf_props_mercury, "Hauppauge Mercury", NULL) },
	{ DVB_USB_DEVICE(USB_VID_HAUPPAUGE, 0xd8d4, &mxl111sf_props_dvbt, "Hauppauge 138xxx DVBT", NULL) },
	{ DVB_USB_DEVICE(USB_VID_HAUPPAUGE, 0xd8e3, &mxl111sf_props_mercury, "Hauppauge Mercury", NULL) },
	{ DVB_USB_DEVICE(USB_VID_HAUPPAUGE, 0xd8e4, &mxl111sf_props_dvbt, "Hauppauge 138xxx DVBT", NULL) },
	{ DVB_USB_DEVICE(USB_VID_HAUPPAUGE, 0xd8ff, &mxl111sf_props_mercury, "Hauppauge Mercury", NULL) },
	{ DVB_USB_DEVICE(USB_VID_HAUPPAUGE, 0xc612, &mxl111sf_props_mercury_mh, "Hauppauge 126xxx", NULL) },
	{ DVB_USB_DEVICE(USB_VID_HAUPPAUGE, 0xc613, &mxl111sf_props_mercury, "Hauppauge WinTV-Aero-M", NULL) },
	{ DVB_USB_DEVICE(USB_VID_HAUPPAUGE, 0xc61a, &mxl111sf_props_mercury_mh, "Hauppauge 126xxx", NULL) },
	{ DVB_USB_DEVICE(USB_VID_HAUPPAUGE, 0xc61b, &mxl111sf_props_mercury, "Hauppauge WinTV-Aero-M", NULL) },
	{ DVB_USB_DEVICE(USB_VID_HAUPPAUGE, 0xb757, &mxl111sf_props_atsc_mh, "Hauppauge 117xxx ATSC+", NULL) },
	{ DVB_USB_DEVICE(USB_VID_HAUPPAUGE, 0xb767, &mxl111sf_props_atsc_mh, "Hauppauge 117xxx ATSC+", NULL) },
	{ }
};
MODULE_DEVICE_TABLE(usb, mxl111sf_id_table);

static struct usb_driver mxl111sf_usb_driver = {
	.name = KBUILD_MODNAME,
	.id_table = mxl111sf_id_table,
	.probe = dvb_usbv2_probe,
	.disconnect = dvb_usbv2_disconnect,
	.suspend = dvb_usbv2_suspend,
	.resume = dvb_usbv2_resume,
	.no_dynamic_id = 1,
	.soft_unbind = 1,
};

module_usb_driver(mxl111sf_usb_driver);

MODULE_AUTHOR("Michael Krufky <mkrufky@kernellabs.com>");
MODULE_DESCRIPTION("Driver for MaxLinear MxL111SF");
MODULE_VERSION("1.0");
MODULE_LICENSE("GPL");

/*
 * Local variables:
 * c-basic-offset: 8
 * End:
 */

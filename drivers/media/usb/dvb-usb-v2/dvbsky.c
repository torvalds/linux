/*
 * Driver for DVBSky USB2.0 receiver
 *
 * Copyright (C) 2013 Max nibble <nibble.max@gmail.com>
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
 */

#include "dvb_usb.h"
#include "m88ds3103.h"
#include "ts2020.h"
#include "sp2.h"
#include "si2168.h"
#include "si2157.h"

#define DVBSKY_MSG_DELAY	0/*2000*/
#define DVBSKY_BUF_LEN	64

static int dvb_usb_dvbsky_disable_rc;
module_param_named(disable_rc, dvb_usb_dvbsky_disable_rc, int, 0644);
MODULE_PARM_DESC(disable_rc, "Disable inbuilt IR receiver.");

DVB_DEFINE_MOD_OPT_ADAPTER_NR(adapter_nr);

struct dvbsky_state {
	struct mutex stream_mutex;
	u8 ibuf[DVBSKY_BUF_LEN];
	u8 obuf[DVBSKY_BUF_LEN];
	u8 last_lock;
	struct i2c_client *i2c_client_demod;
	struct i2c_client *i2c_client_tuner;
	struct i2c_client *i2c_client_ci;

	/* fe hook functions*/
	int (*fe_set_voltage)(struct dvb_frontend *fe,
		enum fe_sec_voltage voltage);
	int (*fe_read_status)(struct dvb_frontend *fe,
		enum fe_status *status);
};

static int dvbsky_usb_generic_rw(struct dvb_usb_device *d,
		u8 *wbuf, u16 wlen, u8 *rbuf, u16 rlen)
{
	int ret;
	struct dvbsky_state *state = d_to_priv(d);

	mutex_lock(&d->usb_mutex);
	if (wlen != 0)
		memcpy(state->obuf, wbuf, wlen);

	ret = dvb_usbv2_generic_rw_locked(d, state->obuf, wlen,
			state->ibuf, rlen);

	if (!ret && (rlen != 0))
		memcpy(rbuf, state->ibuf, rlen);

	mutex_unlock(&d->usb_mutex);
	return ret;
}

static int dvbsky_stream_ctrl(struct dvb_usb_device *d, u8 onoff)
{
	struct dvbsky_state *state = d_to_priv(d);
	int ret;
	u8 obuf_pre[3] = { 0x37, 0, 0 };
	u8 obuf_post[3] = { 0x36, 3, 0 };

	mutex_lock(&state->stream_mutex);
	ret = dvbsky_usb_generic_rw(d, obuf_pre, 3, NULL, 0);
	if (!ret && onoff) {
		msleep(20);
		ret = dvbsky_usb_generic_rw(d, obuf_post, 3, NULL, 0);
	}
	mutex_unlock(&state->stream_mutex);
	return ret;
}

static int dvbsky_streaming_ctrl(struct dvb_frontend *fe, int onoff)
{
	struct dvb_usb_device *d = fe_to_d(fe);

	return dvbsky_stream_ctrl(d, (onoff == 0) ? 0 : 1);
}

/* GPIO */
static int dvbsky_gpio_ctrl(struct dvb_usb_device *d, u8 gport, u8 value)
{
	int ret;
	u8 obuf[3], ibuf[2];

	obuf[0] = 0x0e;
	obuf[1] = gport;
	obuf[2] = value;
	ret = dvbsky_usb_generic_rw(d, obuf, 3, ibuf, 1);
	if (ret)
		dev_err(&d->udev->dev, "failed=%d\n", ret);
	return ret;
}

/* I2C */
static int dvbsky_i2c_xfer(struct i2c_adapter *adap, struct i2c_msg msg[],
	int num)
{
	struct dvb_usb_device *d = i2c_get_adapdata(adap);
	int ret = 0;
	u8 ibuf[64], obuf[64];

	if (mutex_lock_interruptible(&d->i2c_mutex) < 0)
		return -EAGAIN;

	if (num > 2) {
		dev_err(&d->udev->dev,
		"too many i2c messages[%d], max 2.", num);
		ret = -EOPNOTSUPP;
		goto i2c_error;
	}

	if (num == 1) {
		if (msg[0].len > 60) {
			dev_err(&d->udev->dev,
			"too many i2c bytes[%d], max 60.",
			msg[0].len);
			ret = -EOPNOTSUPP;
			goto i2c_error;
		}
		if (msg[0].flags & I2C_M_RD) {
			/* single read */
			obuf[0] = 0x09;
			obuf[1] = 0;
			obuf[2] = msg[0].len;
			obuf[3] = msg[0].addr;
			ret = dvbsky_usb_generic_rw(d, obuf, 4,
					ibuf, msg[0].len + 1);
			if (ret)
				dev_err(&d->udev->dev, "failed=%d\n", ret);
			if (!ret)
				memcpy(msg[0].buf, &ibuf[1], msg[0].len);
		} else {
			/* write */
			obuf[0] = 0x08;
			obuf[1] = msg[0].addr;
			obuf[2] = msg[0].len;
			memcpy(&obuf[3], msg[0].buf, msg[0].len);
			ret = dvbsky_usb_generic_rw(d, obuf,
					msg[0].len + 3, ibuf, 1);
			if (ret)
				dev_err(&d->udev->dev, "failed=%d\n", ret);
		}
	} else {
		if ((msg[0].len > 60) || (msg[1].len > 60)) {
			dev_err(&d->udev->dev,
			"too many i2c bytes[w-%d][r-%d], max 60.",
			msg[0].len, msg[1].len);
			ret = -EOPNOTSUPP;
			goto i2c_error;
		}
		/* write then read */
		obuf[0] = 0x09;
		obuf[1] = msg[0].len;
		obuf[2] = msg[1].len;
		obuf[3] = msg[0].addr;
		memcpy(&obuf[4], msg[0].buf, msg[0].len);
		ret = dvbsky_usb_generic_rw(d, obuf,
			msg[0].len + 4, ibuf, msg[1].len + 1);
		if (ret)
			dev_err(&d->udev->dev, "failed=%d\n", ret);

		if (!ret)
			memcpy(msg[1].buf, &ibuf[1], msg[1].len);
	}
i2c_error:
	mutex_unlock(&d->i2c_mutex);
	return (ret) ? ret : num;
}

static u32 dvbsky_i2c_func(struct i2c_adapter *adapter)
{
	return I2C_FUNC_I2C;
}

static struct i2c_algorithm dvbsky_i2c_algo = {
	.master_xfer   = dvbsky_i2c_xfer,
	.functionality = dvbsky_i2c_func,
};

#if IS_ENABLED(CONFIG_RC_CORE)
static int dvbsky_rc_query(struct dvb_usb_device *d)
{
	u32 code = 0xffff, scancode;
	u8 rc5_command, rc5_system;
	u8 obuf[2], ibuf[2], toggle;
	int ret;

	obuf[0] = 0x10;
	ret = dvbsky_usb_generic_rw(d, obuf, 1, ibuf, 2);
	if (ret)
		dev_err(&d->udev->dev, "failed=%d\n", ret);
	if (ret == 0)
		code = (ibuf[0] << 8) | ibuf[1];
	if (code != 0xffff) {
		dev_dbg(&d->udev->dev, "rc code: %x\n", code);
		rc5_command = code & 0x3F;
		rc5_system = (code & 0x7C0) >> 6;
		toggle = (code & 0x800) ? 1 : 0;
		scancode = rc5_system << 8 | rc5_command;
		rc_keydown(d->rc_dev, RC_PROTO_RC5, scancode, toggle);
	}
	return 0;
}

static int dvbsky_get_rc_config(struct dvb_usb_device *d, struct dvb_usb_rc *rc)
{
	if (dvb_usb_dvbsky_disable_rc) {
		rc->map_name = NULL;
		return 0;
	}

	rc->allowed_protos = RC_PROTO_BIT_RC5;
	rc->query          = dvbsky_rc_query;
	rc->interval       = 300;
	return 0;
}
#else
	#define dvbsky_get_rc_config NULL
#endif

static int dvbsky_usb_set_voltage(struct dvb_frontend *fe,
	enum fe_sec_voltage voltage)
{
	struct dvb_usb_device *d = fe_to_d(fe);
	struct dvbsky_state *state = d_to_priv(d);
	u8 value;

	if (voltage == SEC_VOLTAGE_OFF)
		value = 0;
	else
		value = 1;
	dvbsky_gpio_ctrl(d, 0x80, value);

	return state->fe_set_voltage(fe, voltage);
}

static int dvbsky_read_mac_addr(struct dvb_usb_adapter *adap, u8 mac[6])
{
	struct dvb_usb_device *d = adap_to_d(adap);
	u8 obuf[] = { 0x1e, 0x00 };
	u8 ibuf[6] = { 0 };
	struct i2c_msg msg[] = {
		{
			.addr = 0x51,
			.flags = 0,
			.buf = obuf,
			.len = 2,
		}, {
			.addr = 0x51,
			.flags = I2C_M_RD,
			.buf = ibuf,
			.len = 6,
		}
	};

	if (i2c_transfer(&d->i2c_adap, msg, 2) == 2)
		memcpy(mac, ibuf, 6);

	return 0;
}

static int dvbsky_usb_read_status(struct dvb_frontend *fe,
				  enum fe_status *status)
{
	struct dvb_usb_device *d = fe_to_d(fe);
	struct dvbsky_state *state = d_to_priv(d);
	int ret;

	ret = state->fe_read_status(fe, status);

	/* it need resync slave fifo when signal change from unlock to lock.*/
	if ((*status & FE_HAS_LOCK) && (!state->last_lock))
		dvbsky_stream_ctrl(d, 1);

	state->last_lock = (*status & FE_HAS_LOCK) ? 1 : 0;
	return ret;
}

static const struct m88ds3103_config dvbsky_s960_m88ds3103_config = {
	.i2c_addr = 0x68,
	.clock = 27000000,
	.i2c_wr_max = 33,
	.clock_out = 0,
	.ts_mode = M88DS3103_TS_CI,
	.ts_clk = 16000,
	.ts_clk_pol = 0,
	.agc = 0x99,
	.lnb_hv_pol = 1,
	.lnb_en_pol = 1,
};

static int dvbsky_s960_attach(struct dvb_usb_adapter *adap)
{
	struct dvbsky_state *state = adap_to_priv(adap);
	struct dvb_usb_device *d = adap_to_d(adap);
	int ret = 0;
	/* demod I2C adapter */
	struct i2c_adapter *i2c_adapter = NULL;
	struct i2c_client *client;
	struct i2c_board_info info;
	struct ts2020_config ts2020_config = {};
	memset(&info, 0, sizeof(struct i2c_board_info));

	/* attach demod */
	adap->fe[0] = dvb_attach(m88ds3103_attach,
			&dvbsky_s960_m88ds3103_config,
			&d->i2c_adap,
			&i2c_adapter);
	if (!adap->fe[0]) {
		dev_err(&d->udev->dev, "dvbsky_s960_attach fail.\n");
		ret = -ENODEV;
		goto fail_attach;
	}

	/* attach tuner */
	ts2020_config.fe = adap->fe[0];
	ts2020_config.get_agc_pwm = m88ds3103_get_agc_pwm;
	strlcpy(info.type, "ts2020", I2C_NAME_SIZE);
	info.addr = 0x60;
	info.platform_data = &ts2020_config;
	request_module("ts2020");
	client = i2c_new_device(i2c_adapter, &info);
	if (client == NULL || client->dev.driver == NULL) {
		dvb_frontend_detach(adap->fe[0]);
		ret = -ENODEV;
		goto fail_attach;
	}

	if (!try_module_get(client->dev.driver->owner)) {
		i2c_unregister_device(client);
		dvb_frontend_detach(adap->fe[0]);
		ret = -ENODEV;
		goto fail_attach;
	}

	/* delegate signal strength measurement to tuner */
	adap->fe[0]->ops.read_signal_strength =
			adap->fe[0]->ops.tuner_ops.get_rf_strength;

	/* hook fe: need to resync the slave fifo when signal locks. */
	state->fe_read_status = adap->fe[0]->ops.read_status;
	adap->fe[0]->ops.read_status = dvbsky_usb_read_status;

	/* hook fe: LNB off/on is control by Cypress usb chip. */
	state->fe_set_voltage = adap->fe[0]->ops.set_voltage;
	adap->fe[0]->ops.set_voltage = dvbsky_usb_set_voltage;

	state->i2c_client_tuner = client;

fail_attach:
	return ret;
}

static int dvbsky_usb_ci_set_voltage(struct dvb_frontend *fe,
	enum fe_sec_voltage voltage)
{
	struct dvb_usb_device *d = fe_to_d(fe);
	struct dvbsky_state *state = d_to_priv(d);
	u8 value;

	if (voltage == SEC_VOLTAGE_OFF)
		value = 0;
	else
		value = 1;
	dvbsky_gpio_ctrl(d, 0x00, value);

	return state->fe_set_voltage(fe, voltage);
}

static int dvbsky_ci_ctrl(void *priv, u8 read, int addr,
					u8 data, int *mem)
{
	struct dvb_usb_device *d = priv;
	int ret = 0;
	u8 command[4], respond[2], command_size, respond_size;

	command[1] = (u8)((addr >> 8) & 0xff); /*high part of address*/
	command[2] = (u8)(addr & 0xff); /*low part of address*/
	if (read) {
		command[0] = 0x71;
		command_size = 3;
		respond_size = 2;
	} else {
		command[0] = 0x70;
		command[3] = data;
		command_size = 4;
		respond_size = 1;
	}
	ret = dvbsky_usb_generic_rw(d, command, command_size,
			respond, respond_size);
	if (ret)
		goto err;
	if (read)
		*mem = respond[1];
	return ret;
err:
	dev_err(&d->udev->dev, "ci control failed=%d\n", ret);
	return ret;
}

static const struct m88ds3103_config dvbsky_s960c_m88ds3103_config = {
	.i2c_addr = 0x68,
	.clock = 27000000,
	.i2c_wr_max = 33,
	.clock_out = 0,
	.ts_mode = M88DS3103_TS_CI,
	.ts_clk = 10000,
	.ts_clk_pol = 1,
	.agc = 0x99,
	.lnb_hv_pol = 0,
	.lnb_en_pol = 1,
};

static int dvbsky_s960c_attach(struct dvb_usb_adapter *adap)
{
	struct dvbsky_state *state = adap_to_priv(adap);
	struct dvb_usb_device *d = adap_to_d(adap);
	int ret = 0;
	/* demod I2C adapter */
	struct i2c_adapter *i2c_adapter = NULL;
	struct i2c_client *client_tuner, *client_ci;
	struct i2c_board_info info;
	struct sp2_config sp2_config;
	struct ts2020_config ts2020_config = {};
	memset(&info, 0, sizeof(struct i2c_board_info));

	/* attach demod */
	adap->fe[0] = dvb_attach(m88ds3103_attach,
			&dvbsky_s960c_m88ds3103_config,
			&d->i2c_adap,
			&i2c_adapter);
	if (!adap->fe[0]) {
		dev_err(&d->udev->dev, "dvbsky_s960ci_attach fail.\n");
		ret = -ENODEV;
		goto fail_attach;
	}

	/* attach tuner */
	ts2020_config.fe = adap->fe[0];
	ts2020_config.get_agc_pwm = m88ds3103_get_agc_pwm;
	strlcpy(info.type, "ts2020", I2C_NAME_SIZE);
	info.addr = 0x60;
	info.platform_data = &ts2020_config;
	request_module("ts2020");
	client_tuner = i2c_new_device(i2c_adapter, &info);
	if (client_tuner == NULL || client_tuner->dev.driver == NULL) {
		ret = -ENODEV;
		goto fail_tuner_device;
	}

	if (!try_module_get(client_tuner->dev.driver->owner)) {
		ret = -ENODEV;
		goto fail_tuner_module;
	}

	/* attach ci controller */
	memset(&sp2_config, 0, sizeof(sp2_config));
	sp2_config.dvb_adap = &adap->dvb_adap;
	sp2_config.priv = d;
	sp2_config.ci_control = dvbsky_ci_ctrl;
	memset(&info, 0, sizeof(struct i2c_board_info));
	strlcpy(info.type, "sp2", I2C_NAME_SIZE);
	info.addr = 0x40;
	info.platform_data = &sp2_config;
	request_module("sp2");
	client_ci = i2c_new_device(&d->i2c_adap, &info);
	if (client_ci == NULL || client_ci->dev.driver == NULL) {
		ret = -ENODEV;
		goto fail_ci_device;
	}

	if (!try_module_get(client_ci->dev.driver->owner)) {
		ret = -ENODEV;
		goto fail_ci_module;
	}

	/* delegate signal strength measurement to tuner */
	adap->fe[0]->ops.read_signal_strength =
			adap->fe[0]->ops.tuner_ops.get_rf_strength;

	/* hook fe: need to resync the slave fifo when signal locks. */
	state->fe_read_status = adap->fe[0]->ops.read_status;
	adap->fe[0]->ops.read_status = dvbsky_usb_read_status;

	/* hook fe: LNB off/on is control by Cypress usb chip. */
	state->fe_set_voltage = adap->fe[0]->ops.set_voltage;
	adap->fe[0]->ops.set_voltage = dvbsky_usb_ci_set_voltage;

	state->i2c_client_tuner = client_tuner;
	state->i2c_client_ci = client_ci;
	return ret;
fail_ci_module:
	i2c_unregister_device(client_ci);
fail_ci_device:
	module_put(client_tuner->dev.driver->owner);
fail_tuner_module:
	i2c_unregister_device(client_tuner);
fail_tuner_device:
	dvb_frontend_detach(adap->fe[0]);
fail_attach:
	return ret;
}

static int dvbsky_t680c_attach(struct dvb_usb_adapter *adap)
{
	struct dvbsky_state *state = adap_to_priv(adap);
	struct dvb_usb_device *d = adap_to_d(adap);
	int ret = 0;
	struct i2c_adapter *i2c_adapter;
	struct i2c_client *client_demod, *client_tuner, *client_ci;
	struct i2c_board_info info;
	struct si2168_config si2168_config;
	struct si2157_config si2157_config;
	struct sp2_config sp2_config;

	/* attach demod */
	memset(&si2168_config, 0, sizeof(si2168_config));
	si2168_config.i2c_adapter = &i2c_adapter;
	si2168_config.fe = &adap->fe[0];
	si2168_config.ts_mode = SI2168_TS_PARALLEL;
	memset(&info, 0, sizeof(struct i2c_board_info));
	strlcpy(info.type, "si2168", I2C_NAME_SIZE);
	info.addr = 0x64;
	info.platform_data = &si2168_config;

	request_module(info.type);
	client_demod = i2c_new_device(&d->i2c_adap, &info);
	if (client_demod == NULL ||
			client_demod->dev.driver == NULL)
		goto fail_demod_device;
	if (!try_module_get(client_demod->dev.driver->owner))
		goto fail_demod_module;

	/* attach tuner */
	memset(&si2157_config, 0, sizeof(si2157_config));
	si2157_config.fe = adap->fe[0];
	si2157_config.if_port = 1;
	memset(&info, 0, sizeof(struct i2c_board_info));
	strlcpy(info.type, "si2157", I2C_NAME_SIZE);
	info.addr = 0x60;
	info.platform_data = &si2157_config;

	request_module(info.type);
	client_tuner = i2c_new_device(i2c_adapter, &info);
	if (client_tuner == NULL ||
			client_tuner->dev.driver == NULL)
		goto fail_tuner_device;
	if (!try_module_get(client_tuner->dev.driver->owner))
		goto fail_tuner_module;

	/* attach ci controller */
	memset(&sp2_config, 0, sizeof(sp2_config));
	sp2_config.dvb_adap = &adap->dvb_adap;
	sp2_config.priv = d;
	sp2_config.ci_control = dvbsky_ci_ctrl;
	memset(&info, 0, sizeof(struct i2c_board_info));
	strlcpy(info.type, "sp2", I2C_NAME_SIZE);
	info.addr = 0x40;
	info.platform_data = &sp2_config;

	request_module(info.type);
	client_ci = i2c_new_device(&d->i2c_adap, &info);

	if (client_ci == NULL || client_ci->dev.driver == NULL)
		goto fail_ci_device;

	if (!try_module_get(client_ci->dev.driver->owner))
		goto fail_ci_module;

	state->i2c_client_demod = client_demod;
	state->i2c_client_tuner = client_tuner;
	state->i2c_client_ci = client_ci;
	return ret;
fail_ci_module:
	i2c_unregister_device(client_ci);
fail_ci_device:
	module_put(client_tuner->dev.driver->owner);
fail_tuner_module:
	i2c_unregister_device(client_tuner);
fail_tuner_device:
	module_put(client_demod->dev.driver->owner);
fail_demod_module:
	i2c_unregister_device(client_demod);
fail_demod_device:
	ret = -ENODEV;
	return ret;
}

static int dvbsky_t330_attach(struct dvb_usb_adapter *adap)
{
	struct dvbsky_state *state = adap_to_priv(adap);
	struct dvb_usb_device *d = adap_to_d(adap);
	int ret = 0;
	struct i2c_adapter *i2c_adapter;
	struct i2c_client *client_demod, *client_tuner;
	struct i2c_board_info info;
	struct si2168_config si2168_config;
	struct si2157_config si2157_config;

	/* attach demod */
	memset(&si2168_config, 0, sizeof(si2168_config));
	si2168_config.i2c_adapter = &i2c_adapter;
	si2168_config.fe = &adap->fe[0];
	si2168_config.ts_mode = SI2168_TS_PARALLEL;
	si2168_config.ts_clock_gapped = true;
	memset(&info, 0, sizeof(struct i2c_board_info));
	strlcpy(info.type, "si2168", I2C_NAME_SIZE);
	info.addr = 0x64;
	info.platform_data = &si2168_config;

	request_module(info.type);
	client_demod = i2c_new_device(&d->i2c_adap, &info);
	if (client_demod == NULL ||
			client_demod->dev.driver == NULL)
		goto fail_demod_device;
	if (!try_module_get(client_demod->dev.driver->owner))
		goto fail_demod_module;

	/* attach tuner */
	memset(&si2157_config, 0, sizeof(si2157_config));
	si2157_config.fe = adap->fe[0];
	si2157_config.if_port = 1;
	memset(&info, 0, sizeof(struct i2c_board_info));
	strlcpy(info.type, "si2157", I2C_NAME_SIZE);
	info.addr = 0x60;
	info.platform_data = &si2157_config;

	request_module(info.type);
	client_tuner = i2c_new_device(i2c_adapter, &info);
	if (client_tuner == NULL ||
			client_tuner->dev.driver == NULL)
		goto fail_tuner_device;
	if (!try_module_get(client_tuner->dev.driver->owner))
		goto fail_tuner_module;

	state->i2c_client_demod = client_demod;
	state->i2c_client_tuner = client_tuner;
	return ret;
fail_tuner_module:
	i2c_unregister_device(client_tuner);
fail_tuner_device:
	module_put(client_demod->dev.driver->owner);
fail_demod_module:
	i2c_unregister_device(client_demod);
fail_demod_device:
	ret = -ENODEV;
	return ret;
}

static int dvbsky_mygica_t230c_attach(struct dvb_usb_adapter *adap)
{
	struct dvbsky_state *state = adap_to_priv(adap);
	struct dvb_usb_device *d = adap_to_d(adap);
	struct i2c_adapter *i2c_adapter;
	struct i2c_client *client_demod, *client_tuner;
	struct i2c_board_info info;
	struct si2168_config si2168_config;
	struct si2157_config si2157_config;

	/* attach demod */
	memset(&si2168_config, 0, sizeof(si2168_config));
	si2168_config.i2c_adapter = &i2c_adapter;
	si2168_config.fe = &adap->fe[0];
	si2168_config.ts_mode = SI2168_TS_PARALLEL;
	si2168_config.ts_clock_inv = 1;
	memset(&info, 0, sizeof(struct i2c_board_info));
	strlcpy(info.type, "si2168", sizeof(info.type));
	info.addr = 0x64;
	info.platform_data = &si2168_config;

	request_module("si2168");
	client_demod = i2c_new_device(&d->i2c_adap, &info);
	if (!client_demod || !client_demod->dev.driver)
		goto fail_demod_device;
	if (!try_module_get(client_demod->dev.driver->owner))
		goto fail_demod_module;

	/* attach tuner */
	memset(&si2157_config, 0, sizeof(si2157_config));
	si2157_config.fe = adap->fe[0];
	si2157_config.if_port = 0;
	memset(&info, 0, sizeof(struct i2c_board_info));
	strlcpy(info.type, "si2141", sizeof(info.type));
	info.addr = 0x60;
	info.platform_data = &si2157_config;

	request_module("si2157");
	client_tuner = i2c_new_device(i2c_adapter, &info);
	if (!client_tuner || !client_tuner->dev.driver)
		goto fail_tuner_device;
	if (!try_module_get(client_tuner->dev.driver->owner))
		goto fail_tuner_module;

	state->i2c_client_demod = client_demod;
	state->i2c_client_tuner = client_tuner;
	return 0;

fail_tuner_module:
	i2c_unregister_device(client_tuner);
fail_tuner_device:
	module_put(client_demod->dev.driver->owner);
fail_demod_module:
	i2c_unregister_device(client_demod);
fail_demod_device:
	return -ENODEV;
}


static int dvbsky_identify_state(struct dvb_usb_device *d, const char **name)
{
	dvbsky_gpio_ctrl(d, 0x04, 1);
	msleep(20);
	dvbsky_gpio_ctrl(d, 0x83, 0);
	dvbsky_gpio_ctrl(d, 0xc0, 1);
	msleep(100);
	dvbsky_gpio_ctrl(d, 0x83, 1);
	dvbsky_gpio_ctrl(d, 0xc0, 0);
	msleep(50);

	return WARM;
}

static int dvbsky_init(struct dvb_usb_device *d)
{
	struct dvbsky_state *state = d_to_priv(d);

	/* use default interface */
	/*
	ret = usb_set_interface(d->udev, 0, 0);
	if (ret)
		return ret;
	*/
	mutex_init(&state->stream_mutex);

	state->last_lock = 0;

	return 0;
}

static void dvbsky_exit(struct dvb_usb_device *d)
{
	struct dvbsky_state *state = d_to_priv(d);
	struct i2c_client *client;

	client = state->i2c_client_tuner;
	/* remove I2C tuner */
	if (client) {
		module_put(client->dev.driver->owner);
		i2c_unregister_device(client);
	}
	client = state->i2c_client_demod;
	/* remove I2C demod */
	if (client) {
		module_put(client->dev.driver->owner);
		i2c_unregister_device(client);
	}
	client = state->i2c_client_ci;
	/* remove I2C ci */
	if (client) {
		module_put(client->dev.driver->owner);
		i2c_unregister_device(client);
	}
}

/* DVB USB Driver stuff */
static struct dvb_usb_device_properties dvbsky_s960_props = {
	.driver_name = KBUILD_MODNAME,
	.owner = THIS_MODULE,
	.adapter_nr = adapter_nr,
	.size_of_priv = sizeof(struct dvbsky_state),

	.generic_bulk_ctrl_endpoint = 0x01,
	.generic_bulk_ctrl_endpoint_response = 0x81,
	.generic_bulk_ctrl_delay = DVBSKY_MSG_DELAY,

	.i2c_algo         = &dvbsky_i2c_algo,
	.frontend_attach  = dvbsky_s960_attach,
	.init             = dvbsky_init,
	.get_rc_config    = dvbsky_get_rc_config,
	.streaming_ctrl   = dvbsky_streaming_ctrl,
	.identify_state	  = dvbsky_identify_state,
	.exit             = dvbsky_exit,
	.read_mac_address = dvbsky_read_mac_addr,

	.num_adapters = 1,
	.adapter = {
		{
			.stream = DVB_USB_STREAM_BULK(0x82, 8, 4096),
		}
	}
};

static struct dvb_usb_device_properties dvbsky_s960c_props = {
	.driver_name = KBUILD_MODNAME,
	.owner = THIS_MODULE,
	.adapter_nr = adapter_nr,
	.size_of_priv = sizeof(struct dvbsky_state),

	.generic_bulk_ctrl_endpoint = 0x01,
	.generic_bulk_ctrl_endpoint_response = 0x81,
	.generic_bulk_ctrl_delay = DVBSKY_MSG_DELAY,

	.i2c_algo         = &dvbsky_i2c_algo,
	.frontend_attach  = dvbsky_s960c_attach,
	.init             = dvbsky_init,
	.get_rc_config    = dvbsky_get_rc_config,
	.streaming_ctrl   = dvbsky_streaming_ctrl,
	.identify_state	  = dvbsky_identify_state,
	.exit             = dvbsky_exit,
	.read_mac_address = dvbsky_read_mac_addr,

	.num_adapters = 1,
	.adapter = {
		{
			.stream = DVB_USB_STREAM_BULK(0x82, 8, 4096),
		}
	}
};

static struct dvb_usb_device_properties dvbsky_t680c_props = {
	.driver_name = KBUILD_MODNAME,
	.owner = THIS_MODULE,
	.adapter_nr = adapter_nr,
	.size_of_priv = sizeof(struct dvbsky_state),

	.generic_bulk_ctrl_endpoint = 0x01,
	.generic_bulk_ctrl_endpoint_response = 0x81,
	.generic_bulk_ctrl_delay = DVBSKY_MSG_DELAY,

	.i2c_algo         = &dvbsky_i2c_algo,
	.frontend_attach  = dvbsky_t680c_attach,
	.init             = dvbsky_init,
	.get_rc_config    = dvbsky_get_rc_config,
	.streaming_ctrl   = dvbsky_streaming_ctrl,
	.identify_state	  = dvbsky_identify_state,
	.exit             = dvbsky_exit,
	.read_mac_address = dvbsky_read_mac_addr,

	.num_adapters = 1,
	.adapter = {
		{
			.stream = DVB_USB_STREAM_BULK(0x82, 8, 4096),
		}
	}
};

static struct dvb_usb_device_properties dvbsky_t330_props = {
	.driver_name = KBUILD_MODNAME,
	.owner = THIS_MODULE,
	.adapter_nr = adapter_nr,
	.size_of_priv = sizeof(struct dvbsky_state),

	.generic_bulk_ctrl_endpoint = 0x01,
	.generic_bulk_ctrl_endpoint_response = 0x81,
	.generic_bulk_ctrl_delay = DVBSKY_MSG_DELAY,

	.i2c_algo         = &dvbsky_i2c_algo,
	.frontend_attach  = dvbsky_t330_attach,
	.init             = dvbsky_init,
	.get_rc_config    = dvbsky_get_rc_config,
	.streaming_ctrl   = dvbsky_streaming_ctrl,
	.identify_state	  = dvbsky_identify_state,
	.exit             = dvbsky_exit,
	.read_mac_address = dvbsky_read_mac_addr,

	.num_adapters = 1,
	.adapter = {
		{
			.stream = DVB_USB_STREAM_BULK(0x82, 8, 4096),
		}
	}
};

static struct dvb_usb_device_properties mygica_t230c_props = {
	.driver_name = KBUILD_MODNAME,
	.owner = THIS_MODULE,
	.adapter_nr = adapter_nr,
	.size_of_priv = sizeof(struct dvbsky_state),

	.generic_bulk_ctrl_endpoint = 0x01,
	.generic_bulk_ctrl_endpoint_response = 0x81,
	.generic_bulk_ctrl_delay = DVBSKY_MSG_DELAY,

	.i2c_algo         = &dvbsky_i2c_algo,
	.frontend_attach  = dvbsky_mygica_t230c_attach,
	.init             = dvbsky_init,
	.get_rc_config    = dvbsky_get_rc_config,
	.streaming_ctrl   = dvbsky_streaming_ctrl,
	.identify_state	  = dvbsky_identify_state,
	.exit             = dvbsky_exit,

	.num_adapters = 1,
	.adapter = {
		{
			.stream = DVB_USB_STREAM_BULK(0x82, 8, 4096),
		}
	}
};

static const struct usb_device_id dvbsky_id_table[] = {
	{ DVB_USB_DEVICE(0x0572, 0x6831,
		&dvbsky_s960_props, "DVBSky S960/S860", RC_MAP_DVBSKY) },
	{ DVB_USB_DEVICE(0x0572, 0x960c,
		&dvbsky_s960c_props, "DVBSky S960CI", RC_MAP_DVBSKY) },
	{ DVB_USB_DEVICE(0x0572, 0x680c,
		&dvbsky_t680c_props, "DVBSky T680CI", RC_MAP_DVBSKY) },
	{ DVB_USB_DEVICE(0x0572, 0x0320,
		&dvbsky_t330_props, "DVBSky T330", RC_MAP_DVBSKY) },
	{ DVB_USB_DEVICE(USB_VID_TECHNOTREND,
		USB_PID_TECHNOTREND_TVSTICK_CT2_4400,
		&dvbsky_t330_props, "TechnoTrend TVStick CT2-4400",
		RC_MAP_TT_1500) },
	{ DVB_USB_DEVICE(USB_VID_TECHNOTREND,
		USB_PID_TECHNOTREND_CONNECT_CT2_4650_CI,
		&dvbsky_t680c_props, "TechnoTrend TT-connect CT2-4650 CI",
		RC_MAP_TT_1500) },
	{ DVB_USB_DEVICE(USB_VID_TECHNOTREND,
		USB_PID_TECHNOTREND_CONNECT_CT2_4650_CI_2,
		&dvbsky_t680c_props, "TechnoTrend TT-connect CT2-4650 CI v1.1",
		RC_MAP_TT_1500) },
	{ DVB_USB_DEVICE(USB_VID_TECHNOTREND,
		USB_PID_TECHNOTREND_CONNECT_S2_4650_CI,
		&dvbsky_s960c_props, "TechnoTrend TT-connect S2-4650 CI",
		RC_MAP_TT_1500) },
	{ DVB_USB_DEVICE(USB_VID_TERRATEC,
		USB_PID_TERRATEC_H7_3,
		&dvbsky_t680c_props, "Terratec H7 Rev.4",
		RC_MAP_TT_1500) },
	{ DVB_USB_DEVICE(USB_VID_TERRATEC, USB_PID_TERRATEC_CINERGY_S2_R4,
		&dvbsky_s960_props, "Terratec Cinergy S2 Rev.4",
		RC_MAP_DVBSKY) },
	{ DVB_USB_DEVICE(USB_VID_CONEXANT, USB_PID_MYGICA_T230C,
		&mygica_t230c_props, "MyGica Mini DVB-T2 USB Stick T230C",
		RC_MAP_TOTAL_MEDIA_IN_HAND_02) },
	{ }
};
MODULE_DEVICE_TABLE(usb, dvbsky_id_table);

static struct usb_driver dvbsky_usb_driver = {
	.name = KBUILD_MODNAME,
	.id_table = dvbsky_id_table,
	.probe = dvb_usbv2_probe,
	.disconnect = dvb_usbv2_disconnect,
	.suspend = dvb_usbv2_suspend,
	.resume = dvb_usbv2_resume,
	.reset_resume = dvb_usbv2_reset_resume,
	.no_dynamic_id = 1,
	.soft_unbind = 1,
};

module_usb_driver(dvbsky_usb_driver);

MODULE_AUTHOR("Max nibble <nibble.max@gmail.com>");
MODULE_DESCRIPTION("Driver for DVBSky USB");
MODULE_LICENSE("GPL");

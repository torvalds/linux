// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Driver for DVBSky USB2.0 receiver
 *
 * Copyright (C) 2013 Max nibble <nibble.max@gmail.com>
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
	static const u8 obuf_pre[3] = { 0x37, 0, 0 };
	static const u8 obuf_post[3] = { 0x36, 3, 0 };
	int ret;

	mutex_lock(&d->usb_mutex);
	memcpy(state->obuf, obuf_pre, 3);
	ret = dvb_usbv2_generic_write_locked(d, state->obuf, 3);
	if (!ret && onoff) {
		msleep(20);
		memcpy(state->obuf, obuf_post, 3);
		ret = dvb_usbv2_generic_write_locked(d, state->obuf, 3);
	}
	mutex_unlock(&d->usb_mutex);
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

static int dvbsky_s960_attach(struct dvb_usb_adapter *adap)
{
	struct dvbsky_state *state = adap_to_priv(adap);
	struct dvb_usb_device *d = adap_to_d(adap);
	struct i2c_adapter *i2c_adapter;
	struct m88ds3103_platform_data m88ds3103_pdata = {};
	struct ts2020_config ts2020_config = {};

	/* attach demod */
	m88ds3103_pdata.clk = 27000000;
	m88ds3103_pdata.i2c_wr_max = 33;
	m88ds3103_pdata.clk_out = 0;
	m88ds3103_pdata.ts_mode = M88DS3103_TS_CI;
	m88ds3103_pdata.ts_clk = 16000;
	m88ds3103_pdata.ts_clk_pol = 0;
	m88ds3103_pdata.agc = 0x99;
	m88ds3103_pdata.lnb_hv_pol = 1,
	m88ds3103_pdata.lnb_en_pol = 1,

	state->i2c_client_demod = dvb_module_probe("m88ds3103", NULL,
						   &d->i2c_adap,
						   0x68, &m88ds3103_pdata);
	if (!state->i2c_client_demod)
		return -ENODEV;

	adap->fe[0] = m88ds3103_pdata.get_dvb_frontend(state->i2c_client_demod);
	i2c_adapter = m88ds3103_pdata.get_i2c_adapter(state->i2c_client_demod);

	/* attach tuner */
	ts2020_config.fe = adap->fe[0];
	ts2020_config.get_agc_pwm = m88ds3103_get_agc_pwm;

	state->i2c_client_tuner = dvb_module_probe("ts2020", NULL,
						   i2c_adapter,
						   0x60, &ts2020_config);
	if (!state->i2c_client_tuner) {
		dvb_module_release(state->i2c_client_demod);
		return -ENODEV;
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

	return 0;
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

static int dvbsky_s960c_attach(struct dvb_usb_adapter *adap)
{
	struct dvbsky_state *state = adap_to_priv(adap);
	struct dvb_usb_device *d = adap_to_d(adap);
	struct i2c_adapter *i2c_adapter;
	struct m88ds3103_platform_data m88ds3103_pdata = {};
	struct ts2020_config ts2020_config = {};
	struct sp2_config sp2_config = {};

	/* attach demod */
	m88ds3103_pdata.clk = 27000000,
	m88ds3103_pdata.i2c_wr_max = 33,
	m88ds3103_pdata.clk_out = 0,
	m88ds3103_pdata.ts_mode = M88DS3103_TS_CI,
	m88ds3103_pdata.ts_clk = 10000,
	m88ds3103_pdata.ts_clk_pol = 1,
	m88ds3103_pdata.agc = 0x99,
	m88ds3103_pdata.lnb_hv_pol = 0,
	m88ds3103_pdata.lnb_en_pol = 1,

	state->i2c_client_demod = dvb_module_probe("m88ds3103", NULL,
						   &d->i2c_adap,
						   0x68, &m88ds3103_pdata);
	if (!state->i2c_client_demod)
		return -ENODEV;

	adap->fe[0] = m88ds3103_pdata.get_dvb_frontend(state->i2c_client_demod);
	i2c_adapter = m88ds3103_pdata.get_i2c_adapter(state->i2c_client_demod);

	/* attach tuner */
	ts2020_config.fe = adap->fe[0];
	ts2020_config.get_agc_pwm = m88ds3103_get_agc_pwm;

	state->i2c_client_tuner = dvb_module_probe("ts2020", NULL,
						   i2c_adapter,
						   0x60, &ts2020_config);
	if (!state->i2c_client_tuner) {
		dvb_module_release(state->i2c_client_demod);
		return -ENODEV;
	}

	/* attach ci controller */
	sp2_config.dvb_adap = &adap->dvb_adap;
	sp2_config.priv = d;
	sp2_config.ci_control = dvbsky_ci_ctrl;

	state->i2c_client_ci = dvb_module_probe("sp2", NULL,
						&d->i2c_adap,
						0x40, &sp2_config);

	if (!state->i2c_client_ci) {
		dvb_module_release(state->i2c_client_tuner);
		dvb_module_release(state->i2c_client_demod);
		return -ENODEV;
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

	return 0;
}

static int dvbsky_t680c_attach(struct dvb_usb_adapter *adap)
{
	struct dvbsky_state *state = adap_to_priv(adap);
	struct dvb_usb_device *d = adap_to_d(adap);
	struct i2c_adapter *i2c_adapter;
	struct si2168_config si2168_config = {};
	struct si2157_config si2157_config = {};
	struct sp2_config sp2_config = {};

	/* attach demod */
	si2168_config.i2c_adapter = &i2c_adapter;
	si2168_config.fe = &adap->fe[0];
	si2168_config.ts_mode = SI2168_TS_PARALLEL;

	state->i2c_client_demod = dvb_module_probe("si2168", NULL,
						   &d->i2c_adap,
						   0x64, &si2168_config);
	if (!state->i2c_client_demod)
		return -ENODEV;

	/* attach tuner */
	si2157_config.fe = adap->fe[0];
	si2157_config.if_port = 1;

	state->i2c_client_tuner = dvb_module_probe("si2157", NULL,
						   i2c_adapter,
						   0x60, &si2157_config);
	if (!state->i2c_client_tuner) {
		dvb_module_release(state->i2c_client_demod);
		return -ENODEV;
	}

	/* attach ci controller */
	sp2_config.dvb_adap = &adap->dvb_adap;
	sp2_config.priv = d;
	sp2_config.ci_control = dvbsky_ci_ctrl;

	state->i2c_client_ci = dvb_module_probe("sp2", NULL,
						&d->i2c_adap,
						0x40, &sp2_config);

	if (!state->i2c_client_ci) {
		dvb_module_release(state->i2c_client_tuner);
		dvb_module_release(state->i2c_client_demod);
		return -ENODEV;
	}

	return 0;
}

static int dvbsky_t330_attach(struct dvb_usb_adapter *adap)
{
	struct dvbsky_state *state = adap_to_priv(adap);
	struct dvb_usb_device *d = adap_to_d(adap);
	struct i2c_adapter *i2c_adapter;
	struct si2168_config si2168_config = {};
	struct si2157_config si2157_config = {};

	/* attach demod */
	si2168_config.i2c_adapter = &i2c_adapter;
	si2168_config.fe = &adap->fe[0];
	si2168_config.ts_mode = SI2168_TS_PARALLEL;
	si2168_config.ts_clock_gapped = true;

	state->i2c_client_demod = dvb_module_probe("si2168", NULL,
						   &d->i2c_adap,
						   0x64, &si2168_config);
	if (!state->i2c_client_demod)
		return -ENODEV;

	/* attach tuner */
	si2157_config.fe = adap->fe[0];
	si2157_config.if_port = 1;

	state->i2c_client_tuner = dvb_module_probe("si2157", NULL,
						   i2c_adapter,
						   0x60, &si2157_config);
	if (!state->i2c_client_tuner) {
		dvb_module_release(state->i2c_client_demod);
		return -ENODEV;
	}

	return 0;
}

static int dvbsky_mygica_t230c_attach(struct dvb_usb_adapter *adap)
{
	struct dvbsky_state *state = adap_to_priv(adap);
	struct dvb_usb_device *d = adap_to_d(adap);
	struct i2c_adapter *i2c_adapter;
	struct si2168_config si2168_config = {};
	struct si2157_config si2157_config = {};

	/* attach demod */
	si2168_config.i2c_adapter = &i2c_adapter;
	si2168_config.fe = &adap->fe[0];
	si2168_config.ts_mode = SI2168_TS_PARALLEL;
	if (le16_to_cpu(d->udev->descriptor.idProduct) == USB_PID_MYGICA_T230C2)
		si2168_config.ts_mode |= SI2168_TS_CLK_MANUAL;
	si2168_config.ts_clock_inv = 1;

	state->i2c_client_demod = dvb_module_probe("si2168", NULL,
						   &d->i2c_adap,
						   0x64, &si2168_config);
	if (!state->i2c_client_demod)
		return -ENODEV;

	/* attach tuner */
	si2157_config.fe = adap->fe[0];
	if (le16_to_cpu(d->udev->descriptor.idProduct) == USB_PID_MYGICA_T230) {
		si2157_config.if_port = 1;
		state->i2c_client_tuner = dvb_module_probe("si2157", NULL,
							   i2c_adapter,
							   0x60,
							   &si2157_config);
	} else {
		si2157_config.if_port = 0;
		state->i2c_client_tuner = dvb_module_probe("si2157", "si2141",
							   i2c_adapter,
							   0x60,
							   &si2157_config);
	}
	if (!state->i2c_client_tuner) {
		dvb_module_release(state->i2c_client_demod);
		return -ENODEV;
	}

	return 0;
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

	state->last_lock = 0;

	return 0;
}

static int dvbsky_frontend_detach(struct dvb_usb_adapter *adap)
{
	struct dvb_usb_device *d = adap_to_d(adap);
	struct dvbsky_state *state = d_to_priv(d);

	dev_dbg(&d->udev->dev, "%s: adap=%d\n", __func__, adap->id);

	dvb_module_release(state->i2c_client_tuner);
	dvb_module_release(state->i2c_client_demod);
	dvb_module_release(state->i2c_client_ci);

	return 0;
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
	.frontend_detach  = dvbsky_frontend_detach,
	.init             = dvbsky_init,
	.get_rc_config    = dvbsky_get_rc_config,
	.streaming_ctrl   = dvbsky_streaming_ctrl,
	.identify_state	  = dvbsky_identify_state,
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
	.frontend_detach  = dvbsky_frontend_detach,
	.init             = dvbsky_init,
	.get_rc_config    = dvbsky_get_rc_config,
	.streaming_ctrl   = dvbsky_streaming_ctrl,
	.identify_state	  = dvbsky_identify_state,
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
	.frontend_detach  = dvbsky_frontend_detach,
	.init             = dvbsky_init,
	.get_rc_config    = dvbsky_get_rc_config,
	.streaming_ctrl   = dvbsky_streaming_ctrl,
	.identify_state	  = dvbsky_identify_state,
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
	.frontend_detach  = dvbsky_frontend_detach,
	.init             = dvbsky_init,
	.get_rc_config    = dvbsky_get_rc_config,
	.streaming_ctrl   = dvbsky_streaming_ctrl,
	.identify_state	  = dvbsky_identify_state,
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
	.frontend_detach  = dvbsky_frontend_detach,
	.init             = dvbsky_init,
	.get_rc_config    = dvbsky_get_rc_config,
	.streaming_ctrl   = dvbsky_streaming_ctrl,
	.identify_state	  = dvbsky_identify_state,

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
	{ DVB_USB_DEVICE(USB_VID_CONEXANT, USB_PID_MYGICA_T230,
		&mygica_t230c_props, "MyGica Mini DVB-T2 USB Stick T230",
		RC_MAP_TOTAL_MEDIA_IN_HAND_02) },
	{ DVB_USB_DEVICE(USB_VID_CONEXANT, USB_PID_MYGICA_T230C,
		&mygica_t230c_props, "MyGica Mini DVB-T2 USB Stick T230C",
		RC_MAP_TOTAL_MEDIA_IN_HAND_02) },
	{ DVB_USB_DEVICE(USB_VID_CONEXANT, USB_PID_MYGICA_T230C_LITE,
		&mygica_t230c_props, "MyGica Mini DVB-T2 USB Stick T230C Lite",
		NULL) },
	{ DVB_USB_DEVICE(USB_VID_CONEXANT, USB_PID_MYGICA_T230C2,
		&mygica_t230c_props, "MyGica Mini DVB-T2 USB Stick T230C v2",
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

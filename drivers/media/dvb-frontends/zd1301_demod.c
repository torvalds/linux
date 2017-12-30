/*
 * ZyDAS ZD1301 driver (demodulator)
 *
 * Copyright (C) 2015 Antti Palosaari <crope@iki.fi>
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

#include "zd1301_demod.h"

static u8 zd1301_demod_gain = 0x38;
module_param_named(gain, zd1301_demod_gain, byte, 0644);
MODULE_PARM_DESC(gain, "gain (value: 0x00 - 0x70, default: 0x38)");

struct zd1301_demod_dev {
	struct platform_device *pdev;
	struct dvb_frontend frontend;
	struct i2c_adapter adapter;
	u8 gain;
};

static int zd1301_demod_wreg(struct zd1301_demod_dev *dev, u16 reg, u8 val)
{
	struct platform_device *pdev = dev->pdev;
	struct zd1301_demod_platform_data *pdata = pdev->dev.platform_data;

	return pdata->reg_write(pdata->reg_priv, reg, val);
}

static int zd1301_demod_rreg(struct zd1301_demod_dev *dev, u16 reg, u8 *val)
{
	struct platform_device *pdev = dev->pdev;
	struct zd1301_demod_platform_data *pdata = pdev->dev.platform_data;

	return pdata->reg_read(pdata->reg_priv, reg, val);
}

static int zd1301_demod_set_frontend(struct dvb_frontend *fe)
{
	struct zd1301_demod_dev *dev = fe->demodulator_priv;
	struct platform_device *pdev = dev->pdev;
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	int ret;
	u32 if_frequency;
	u8 r6a50_val;

	dev_dbg(&pdev->dev, "frequency=%u bandwidth_hz=%u\n",
		c->frequency, c->bandwidth_hz);

	/* Program tuner */
	if (fe->ops.tuner_ops.set_params &&
	    fe->ops.tuner_ops.get_if_frequency) {
		ret = fe->ops.tuner_ops.set_params(fe);
		if (ret)
			goto err;
		ret = fe->ops.tuner_ops.get_if_frequency(fe, &if_frequency);
		if (ret)
			goto err;
	} else {
		ret = -EINVAL;
		goto err;
	}

	dev_dbg(&pdev->dev, "if_frequency=%u\n", if_frequency);
	if (if_frequency != 36150000) {
		ret = -EINVAL;
		goto err;
	}

	switch (c->bandwidth_hz) {
	case 6000000:
		r6a50_val = 0x78;
		break;
	case 7000000:
		r6a50_val = 0x68;
		break;
	case 8000000:
		r6a50_val = 0x58;
		break;
	default:
		ret = -EINVAL;
		goto err;
	}

	ret = zd1301_demod_wreg(dev, 0x6a60, 0x11);
	if (ret)
		goto err;
	ret = zd1301_demod_wreg(dev, 0x6a47, 0x46);
	if (ret)
		goto err;
	ret = zd1301_demod_wreg(dev, 0x6a48, 0x46);
	if (ret)
		goto err;
	ret = zd1301_demod_wreg(dev, 0x6a4a, 0x15);
	if (ret)
		goto err;
	ret = zd1301_demod_wreg(dev, 0x6a4b, 0x63);
	if (ret)
		goto err;
	ret = zd1301_demod_wreg(dev, 0x6a5b, 0x99);
	if (ret)
		goto err;
	ret = zd1301_demod_wreg(dev, 0x6a3b, 0x10);
	if (ret)
		goto err;
	ret = zd1301_demod_wreg(dev, 0x6806, 0x01);
	if (ret)
		goto err;
	ret = zd1301_demod_wreg(dev, 0x6a41, 0x08);
	if (ret)
		goto err;
	ret = zd1301_demod_wreg(dev, 0x6a42, 0x46);
	if (ret)
		goto err;
	ret = zd1301_demod_wreg(dev, 0x6a44, 0x14);
	if (ret)
		goto err;
	ret = zd1301_demod_wreg(dev, 0x6a45, 0x67);
	if (ret)
		goto err;
	ret = zd1301_demod_wreg(dev, 0x6a38, 0x00);
	if (ret)
		goto err;
	ret = zd1301_demod_wreg(dev, 0x6a4c, 0x52);
	if (ret)
		goto err;
	ret = zd1301_demod_wreg(dev, 0x6a49, 0x2a);
	if (ret)
		goto err;
	ret = zd1301_demod_wreg(dev, 0x6840, 0x2e);
	if (ret)
		goto err;
	ret = zd1301_demod_wreg(dev, 0x6a50, r6a50_val);
	if (ret)
		goto err;
	ret = zd1301_demod_wreg(dev, 0x6a38, 0x07);
	if (ret)
		goto err;

	return 0;
err:
	dev_dbg(&pdev->dev, "failed=%d\n", ret);
	return ret;
}

static int zd1301_demod_sleep(struct dvb_frontend *fe)
{
	struct zd1301_demod_dev *dev = fe->demodulator_priv;
	struct platform_device *pdev = dev->pdev;
	int ret;

	dev_dbg(&pdev->dev, "\n");

	ret = zd1301_demod_wreg(dev, 0x6a43, 0x70);
	if (ret)
		goto err;
	ret = zd1301_demod_wreg(dev, 0x684e, 0x00);
	if (ret)
		goto err;
	ret = zd1301_demod_wreg(dev, 0x6849, 0x00);
	if (ret)
		goto err;
	ret = zd1301_demod_wreg(dev, 0x68e2, 0xd7);
	if (ret)
		goto err;
	ret = zd1301_demod_wreg(dev, 0x68e0, 0x39);
	if (ret)
		goto err;
	ret = zd1301_demod_wreg(dev, 0x6840, 0x21);
	if (ret)
		goto err;

	return 0;
err:
	dev_dbg(&pdev->dev, "failed=%d\n", ret);
	return ret;
}

static int zd1301_demod_init(struct dvb_frontend *fe)
{
	struct zd1301_demod_dev *dev = fe->demodulator_priv;
	struct platform_device *pdev = dev->pdev;
	int ret;

	dev_dbg(&pdev->dev, "\n");

	ret = zd1301_demod_wreg(dev, 0x6840, 0x26);
	if (ret)
		goto err;
	ret = zd1301_demod_wreg(dev, 0x68e0, 0xff);
	if (ret)
		goto err;
	ret = zd1301_demod_wreg(dev, 0x68e2, 0xd8);
	if (ret)
		goto err;
	ret = zd1301_demod_wreg(dev, 0x6849, 0x4e);
	if (ret)
		goto err;
	ret = zd1301_demod_wreg(dev, 0x684e, 0x01);
	if (ret)
		goto err;
	ret = zd1301_demod_wreg(dev, 0x6a43, zd1301_demod_gain);
	if (ret)
		goto err;

	return 0;
err:
	dev_dbg(&pdev->dev, "failed=%d\n", ret);
	return ret;
}

static int zd1301_demod_get_tune_settings(struct dvb_frontend *fe,
					  struct dvb_frontend_tune_settings *settings)
{
	struct zd1301_demod_dev *dev = fe->demodulator_priv;
	struct platform_device *pdev = dev->pdev;

	dev_dbg(&pdev->dev, "\n");

	/* ~180ms seems to be enough */
	settings->min_delay_ms = 400;

	return 0;
}

static int zd1301_demod_read_status(struct dvb_frontend *fe,
				    enum fe_status *status)
{
	struct zd1301_demod_dev *dev = fe->demodulator_priv;
	struct platform_device *pdev = dev->pdev;
	int ret;
	u8 u8tmp;

	ret = zd1301_demod_rreg(dev, 0x6a24, &u8tmp);
	if (ret)
		goto err;
	if (u8tmp > 0x00 && u8tmp < 0x20)
		*status = FE_HAS_SIGNAL | FE_HAS_CARRIER | FE_HAS_VITERBI |
			  FE_HAS_SYNC | FE_HAS_LOCK;
	else
		*status = 0;

	dev_dbg(&pdev->dev, "lock byte=%02x\n", u8tmp);

	/*
	 * Interesting registers here are:
	 * 0x6a05: get some gain value
	 * 0x6a06: get about same gain value than set to 0x6a43
	 * 0x6a07: get some gain value
	 * 0x6a43: set gain value by driver
	 * 0x6a24: get demod lock bits (FSM stage?)
	 *
	 * Driver should implement some kind of algorithm to calculate suitable
	 * value for register 0x6a43, based likely values from register 0x6a05
	 * and 0x6a07. Looks like gain register 0x6a43 value could be from
	 * range 0x00 - 0x70.
	 */

	if (dev->gain != zd1301_demod_gain) {
		dev->gain = zd1301_demod_gain;

		ret = zd1301_demod_wreg(dev, 0x6a43, dev->gain);
		if (ret)
			goto err;
	}

	return 0;
err:
	dev_dbg(&pdev->dev, "failed=%d\n", ret);
	return ret;
}

static const struct dvb_frontend_ops zd1301_demod_ops = {
	.delsys = {SYS_DVBT},
	.info = {
		.name = "ZyDAS ZD1301",
		.caps = FE_CAN_FEC_1_2 |
			FE_CAN_FEC_2_3 |
			FE_CAN_FEC_3_4 |
			FE_CAN_FEC_5_6 |
			FE_CAN_FEC_7_8 |
			FE_CAN_FEC_AUTO |
			FE_CAN_QPSK |
			FE_CAN_QAM_16 |
			FE_CAN_QAM_64 |
			FE_CAN_QAM_AUTO |
			FE_CAN_TRANSMISSION_MODE_AUTO |
			FE_CAN_GUARD_INTERVAL_AUTO |
			FE_CAN_HIERARCHY_AUTO |
			FE_CAN_MUTE_TS
	},

	.sleep = zd1301_demod_sleep,
	.init = zd1301_demod_init,
	.set_frontend = zd1301_demod_set_frontend,
	.get_tune_settings = zd1301_demod_get_tune_settings,
	.read_status = zd1301_demod_read_status,
};

struct dvb_frontend *zd1301_demod_get_dvb_frontend(struct platform_device *pdev)
{
	struct zd1301_demod_dev *dev = platform_get_drvdata(pdev);

	dev_dbg(&pdev->dev, "\n");

	return &dev->frontend;
}
EXPORT_SYMBOL(zd1301_demod_get_dvb_frontend);

static int zd1301_demod_i2c_master_xfer(struct i2c_adapter *adapter,
					struct i2c_msg msg[], int num)
{
	struct zd1301_demod_dev *dev = i2c_get_adapdata(adapter);
	struct platform_device *pdev = dev->pdev;
	int ret, i;
	unsigned long timeout;
	u8 u8tmp;

	#define I2C_XFER_TIMEOUT 5
	#define ZD1301_IS_I2C_XFER_WRITE_READ(_msg, _num) \
		(_num == 2 && !(_msg[0].flags & I2C_M_RD) && (_msg[1].flags & I2C_M_RD))
	#define ZD1301_IS_I2C_XFER_WRITE(_msg, _num) \
		(_num == 1 && !(_msg[0].flags & I2C_M_RD))
	#define ZD1301_IS_I2C_XFER_READ(_msg, _num) \
		(_num == 1 && (_msg[0].flags & I2C_M_RD))
	if (ZD1301_IS_I2C_XFER_WRITE_READ(msg, num)) {
		dev_dbg(&pdev->dev, "write&read msg[0].len=%u msg[1].len=%u\n",
			msg[0].len, msg[1].len);
		if (msg[0].len > 1 || msg[1].len > 8) {
			ret = -EOPNOTSUPP;
			goto err;
		}

		ret = zd1301_demod_wreg(dev, 0x6811, 0x80);
		if (ret)
			goto err;
		ret = zd1301_demod_wreg(dev, 0x6812, 0x05);
		if (ret)
			goto err;
		ret = zd1301_demod_wreg(dev, 0x6813, msg[1].addr << 1);
		if (ret)
			goto err;
		ret = zd1301_demod_wreg(dev, 0x6801, msg[0].buf[0]);
		if (ret)
			goto err;
		ret = zd1301_demod_wreg(dev, 0x6802, 0x00);
		if (ret)
			goto err;
		ret = zd1301_demod_wreg(dev, 0x6803, 0x06);
		if (ret)
			goto err;
		ret = zd1301_demod_wreg(dev, 0x6805, 0x00);
		if (ret)
			goto err;
		ret = zd1301_demod_wreg(dev, 0x6804, msg[1].len);
		if (ret)
			goto err;

		/* Poll xfer ready */
		timeout = jiffies + msecs_to_jiffies(I2C_XFER_TIMEOUT);
		for (u8tmp = 1; !time_after(jiffies, timeout) && u8tmp;) {
			usleep_range(500, 800);

			ret = zd1301_demod_rreg(dev, 0x6804, &u8tmp);
			if (ret)
				goto err;
		}

		for (i = 0; i < msg[1].len; i++) {
			ret = zd1301_demod_rreg(dev, 0x0600 + i, &msg[1].buf[i]);
			if (ret)
				goto err;
		}
	} else if (ZD1301_IS_I2C_XFER_WRITE(msg, num)) {
		dev_dbg(&pdev->dev, "write msg[0].len=%u\n", msg[0].len);
		if (msg[0].len > 1 + 8) {
			ret = -EOPNOTSUPP;
			goto err;
		}

		ret = zd1301_demod_wreg(dev, 0x6811, 0x80);
		if (ret)
			goto err;
		ret = zd1301_demod_wreg(dev, 0x6812, 0x01);
		if (ret)
			goto err;
		ret = zd1301_demod_wreg(dev, 0x6813, msg[0].addr << 1);
		if (ret)
			goto err;
		ret = zd1301_demod_wreg(dev, 0x6800, msg[0].buf[0]);
		if (ret)
			goto err;
		ret = zd1301_demod_wreg(dev, 0x6802, 0x00);
		if (ret)
			goto err;
		ret = zd1301_demod_wreg(dev, 0x6803, 0x06);
		if (ret)
			goto err;

		for (i = 0; i < msg[0].len - 1; i++) {
			ret = zd1301_demod_wreg(dev, 0x0600 + i, msg[0].buf[1 + i]);
			if (ret)
				goto err;
		}

		ret = zd1301_demod_wreg(dev, 0x6805, 0x80);
		if (ret)
			goto err;
		ret = zd1301_demod_wreg(dev, 0x6804, msg[0].len - 1);
		if (ret)
			goto err;

		/* Poll xfer ready */
		timeout = jiffies + msecs_to_jiffies(I2C_XFER_TIMEOUT);
		for (u8tmp = 1; !time_after(jiffies, timeout) && u8tmp;) {
			usleep_range(500, 800);

			ret = zd1301_demod_rreg(dev, 0x6804, &u8tmp);
			if (ret)
				goto err;
		}
	} else {
		dev_dbg(&pdev->dev, "unknown msg[0].len=%u\n", msg[0].len);
		ret = -EOPNOTSUPP;
		if (ret)
			goto err;
	}

	return num;
err:
	dev_dbg(&pdev->dev, "failed=%d\n", ret);
	return ret;
}

static u32 zd1301_demod_i2c_functionality(struct i2c_adapter *adapter)
{
	return I2C_FUNC_I2C;
}

static const struct i2c_algorithm zd1301_demod_i2c_algorithm = {
	.master_xfer   = zd1301_demod_i2c_master_xfer,
	.functionality = zd1301_demod_i2c_functionality,
};

struct i2c_adapter *zd1301_demod_get_i2c_adapter(struct platform_device *pdev)
{
	struct zd1301_demod_dev *dev = platform_get_drvdata(pdev);

	dev_dbg(&pdev->dev, "\n");

	return &dev->adapter;
}
EXPORT_SYMBOL(zd1301_demod_get_i2c_adapter);

/* Platform driver interface */
static int zd1301_demod_probe(struct platform_device *pdev)
{
	struct zd1301_demod_dev *dev;
	struct zd1301_demod_platform_data *pdata = pdev->dev.platform_data;
	int ret;

	dev_dbg(&pdev->dev, "\n");

	if (!pdata) {
		ret = -EINVAL;
		dev_err(&pdev->dev, "cannot proceed without platform data\n");
		goto err;
	}
	if (!pdev->dev.parent->driver) {
		ret = -EINVAL;
		dev_dbg(&pdev->dev, "no parent device\n");
		goto err;
	}

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev) {
		ret = -ENOMEM;
		goto err;
	}

	/* Setup the state */
	dev->pdev = pdev;
	dev->gain = zd1301_demod_gain;

	/* Sleep */
	ret = zd1301_demod_wreg(dev, 0x6840, 0x21);
	if (ret)
		goto err_kfree;
	ret = zd1301_demod_wreg(dev, 0x6a38, 0x07);
	if (ret)
		goto err_kfree;

	/* Create I2C adapter */
	strlcpy(dev->adapter.name, "ZyDAS ZD1301 demod", sizeof(dev->adapter.name));
	dev->adapter.algo = &zd1301_demod_i2c_algorithm;
	dev->adapter.algo_data = NULL;
	dev->adapter.dev.parent = pdev->dev.parent;
	i2c_set_adapdata(&dev->adapter, dev);
	ret = i2c_add_adapter(&dev->adapter);
	if (ret) {
		dev_err(&pdev->dev, "I2C adapter add failed %d\n", ret);
		goto err_kfree;
	}

	/* Create dvb frontend */
	memcpy(&dev->frontend.ops, &zd1301_demod_ops, sizeof(dev->frontend.ops));
	dev->frontend.demodulator_priv = dev;
	platform_set_drvdata(pdev, dev);
	dev_info(&pdev->dev, "ZyDAS ZD1301 demod attached\n");

	return 0;
err_kfree:
	kfree(dev);
err:
	dev_dbg(&pdev->dev, "failed=%d\n", ret);
	return ret;
}

static int zd1301_demod_remove(struct platform_device *pdev)
{
	struct zd1301_demod_dev *dev = platform_get_drvdata(pdev);

	dev_dbg(&pdev->dev, "\n");

	i2c_del_adapter(&dev->adapter);
	kfree(dev);

	return 0;
}

static struct platform_driver zd1301_demod_driver = {
	.driver = {
		.name                = "zd1301_demod",
		.suppress_bind_attrs = true,
	},
	.probe          = zd1301_demod_probe,
	.remove         = zd1301_demod_remove,
};
module_platform_driver(zd1301_demod_driver);

MODULE_AUTHOR("Antti Palosaari <crope@iki.fi>");
MODULE_DESCRIPTION("ZyDAS ZD1301 demodulator driver");
MODULE_LICENSE("GPL");

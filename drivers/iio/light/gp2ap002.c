// SPDX-License-Identifier: GPL-2.0-only
/*
 * These are the two Sharp GP2AP002 variants supported by this driver:
 * GP2AP002A00F Ambient Light and Proximity Sensor
 * GP2AP002S00F Proximity Sensor
 *
 * Copyright (C) 2020 Linaro Ltd.
 * Author: Linus Walleij <linus.walleij@linaro.org>
 *
 * Based partly on the code in Sony Ericssons GP2AP00200F driver by
 * Courtney Cavin and Oskar Andero in drivers/input/misc/gp2ap002a00f.c
 * Based partly on a Samsung misc driver submitted by
 * Donggeun Kim & Minkyu Kang in 2011:
 * https://lore.kernel.org/lkml/1315556546-7445-1-git-send-email-dg77.kim@samsung.com/
 * Based partly on a submission by
 * Jonathan Bakker and Paweł Chmiel in january 2019:
 * https://lore.kernel.org/linux-input/20190125175045.22576-1-pawel.mikolaj.chmiel@gmail.com/
 * Based partly on code from the Samsung GT-S7710 by <mjchen@sta.samsung.com>
 * Based partly on the code in LG Electronics GP2AP00200F driver by
 * Kenobi Lee <sungyoung.lee@lge.com> and EunYoung Cho <ey.cho@lge.com>
 */
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/regmap.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/events.h>
#include <linux/iio/consumer.h> /* To get our ADC channel */
#include <linux/iio/types.h> /* To deal with our ADC channel */
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/regulator/consumer.h>
#include <linux/pm_runtime.h>
#include <linux/interrupt.h>
#include <linux/bits.h>
#include <linux/math64.h>
#include <linux/pm.h>

#define GP2AP002_PROX_CHANNEL 0
#define GP2AP002_ALS_CHANNEL 1

/* ------------------------------------------------------------------------ */
/* ADDRESS SYMBOL             DATA                                 Init R/W */
/*                   D7    D6    D5    D4    D3    D2    D1    D0           */
/* ------------------------------------------------------------------------ */
/*    0      PROX     X     X     X     X     X     X     X    VO  H'00   R */
/*    1      GAIN     X     X     X     X  LED0     X     X     X  H'00   W */
/*    2       HYS  HYSD HYSC1 HYSC0     X HYSF3 HYSF2 HYSF1 HYSF0  H'00   W */
/*    3     CYCLE     X     X CYCL2 CYCL1 CYCL0  OSC2     X     X  H'00   W */
/*    4     OPMOD     X     X     X   ASD     X     X  VCON   SSD  H'00   W */
/*    6       CON     X     X     X OCON1 OCON0     X     X     X  H'00   W */
/* ------------------------------------------------------------------------ */
/* VO   :Proximity sensing result(0: no detection, 1: detection)            */
/* LED0 :Select switch for LED driver's On-registence(0:2x higher, 1:normal)*/
/* HYSD/HYSF :Adjusts the receiver sensitivity                              */
/* OSC  :Select switch internal clocl frequency hoppling(0:effective)       */
/* CYCL :Determine the detection cycle(typically 8ms, up to 128x)           */
/* SSD  :Software Shutdown function(0:shutdown, 1:operating)                */
/* VCON :VOUT output method control(0:normal, 1:interrupt)                  */
/* ASD  :Select switch for analog sleep function(0:ineffective, 1:effective)*/
/* OCON :Select switch for enabling/disabling VOUT (00:enable, 11:disable)  */

#define GP2AP002_PROX				0x00
#define GP2AP002_GAIN				0x01
#define GP2AP002_HYS				0x02
#define GP2AP002_CYCLE				0x03
#define GP2AP002_OPMOD				0x04
#define GP2AP002_CON				0x06

#define GP2AP002_PROX_VO_DETECT			BIT(0)

/* Setting this bit to 0 means 2x higher LED resistance */
#define GP2AP002_GAIN_LED_NORMAL		BIT(3)

/*
 * These bits adjusts the proximity sensitivity, determining characteristics
 * of the detection distance and its hysteresis.
 */
#define GP2AP002_HYS_HYSD_SHIFT		7
#define GP2AP002_HYS_HYSD_MASK		BIT(7)
#define GP2AP002_HYS_HYSC_SHIFT		5
#define GP2AP002_HYS_HYSC_MASK		GENMASK(6, 5)
#define GP2AP002_HYS_HYSF_SHIFT		0
#define GP2AP002_HYS_HYSF_MASK		GENMASK(3, 0)
#define GP2AP002_HYS_MASK		(GP2AP002_HYS_HYSD_MASK | \
					 GP2AP002_HYS_HYSC_MASK | \
					 GP2AP002_HYS_HYSF_MASK)

/*
 * These values determine the detection cycle response time
 * 0: 8ms, 1: 16ms, 2: 32ms, 3: 64ms, 4: 128ms,
 * 5: 256ms, 6: 512ms, 7: 1024ms
 */
#define GP2AP002_CYCLE_CYCL_SHIFT	3
#define GP2AP002_CYCLE_CYCL_MASK	GENMASK(5, 3)

/*
 * Select switch for internal clock frequency hopping
 *	0: effective,
 *	1: ineffective
 */
#define GP2AP002_CYCLE_OSC_EFFECTIVE	0
#define GP2AP002_CYCLE_OSC_INEFFECTIVE	BIT(2)
#define GP2AP002_CYCLE_OSC_MASK		BIT(2)

/* Analog sleep effective */
#define GP2AP002_OPMOD_ASD		BIT(4)
/* Enable chip */
#define GP2AP002_OPMOD_SSD_OPERATING	BIT(0)
/* IRQ mode */
#define GP2AP002_OPMOD_VCON_IRQ		BIT(1)
#define GP2AP002_OPMOD_MASK		(BIT(0) | BIT(1) | BIT(4))

/*
 * Select switch for enabling/disabling Vout pin
 * 0: enable
 * 2: force to go Low
 * 3: force to go High
 */
#define GP2AP002_CON_OCON_SHIFT		3
#define GP2AP002_CON_OCON_ENABLE	(0x0 << GP2AP002_CON_OCON_SHIFT)
#define GP2AP002_CON_OCON_LOW		(0x2 << GP2AP002_CON_OCON_SHIFT)
#define GP2AP002_CON_OCON_HIGH		(0x3 << GP2AP002_CON_OCON_SHIFT)
#define GP2AP002_CON_OCON_MASK		(0x3 << GP2AP002_CON_OCON_SHIFT)

/**
 * struct gp2ap002 - GP2AP002 state
 * @map: regmap pointer for the i2c regmap
 * @dev: pointer to parent device
 * @vdd: regulator controlling VDD
 * @vio: regulator controlling VIO
 * @alsout: IIO ADC channel to convert the ALSOUT signal
 * @hys_far: hysteresis control from device tree
 * @hys_close: hysteresis control from device tree
 * @is_gp2ap002s00f: this is the GP2AP002F variant of the chip
 * @irq: the IRQ line used by this device
 * @enabled: we cannot read the status of the hardware so we need to
 * keep track of whether the event is enabled using this state variable
 */
struct gp2ap002 {
	struct regmap *map;
	struct device *dev;
	struct regulator *vdd;
	struct regulator *vio;
	struct iio_channel *alsout;
	u8 hys_far;
	u8 hys_close;
	bool is_gp2ap002s00f;
	int irq;
	bool enabled;
};

static irqreturn_t gp2ap002_prox_irq(int irq, void *d)
{
	struct iio_dev *indio_dev = d;
	struct gp2ap002 *gp2ap002 = iio_priv(indio_dev);
	u64 ev;
	int val;
	int ret;

	if (!gp2ap002->enabled)
		goto err_retrig;

	ret = regmap_read(gp2ap002->map, GP2AP002_PROX, &val);
	if (ret) {
		dev_err(gp2ap002->dev, "error reading proximity\n");
		goto err_retrig;
	}

	if (val & GP2AP002_PROX_VO_DETECT) {
		/* Close */
		dev_dbg(gp2ap002->dev, "close\n");
		ret = regmap_write(gp2ap002->map, GP2AP002_HYS,
				   gp2ap002->hys_far);
		if (ret)
			dev_err(gp2ap002->dev,
				"error setting up proximity hysteresis\n");
		ev = IIO_UNMOD_EVENT_CODE(IIO_PROXIMITY, GP2AP002_PROX_CHANNEL,
					IIO_EV_TYPE_THRESH, IIO_EV_DIR_RISING);
	} else {
		/* Far */
		dev_dbg(gp2ap002->dev, "far\n");
		ret = regmap_write(gp2ap002->map, GP2AP002_HYS,
				   gp2ap002->hys_close);
		if (ret)
			dev_err(gp2ap002->dev,
				"error setting up proximity hysteresis\n");
		ev = IIO_UNMOD_EVENT_CODE(IIO_PROXIMITY, GP2AP002_PROX_CHANNEL,
					IIO_EV_TYPE_THRESH, IIO_EV_DIR_FALLING);
	}
	iio_push_event(indio_dev, ev, iio_get_time_ns(indio_dev));

	/*
	 * After changing hysteresis, we need to wait for one detection
	 * cycle to see if anything changed, or we will just trigger the
	 * previous interrupt again. A detection cycle depends on the CYCLE
	 * register, we are hard-coding ~8 ms in probe() so wait some more
	 * than this, 20-30 ms.
	 */
	usleep_range(20000, 30000);

err_retrig:
	ret = regmap_write(gp2ap002->map, GP2AP002_CON,
			   GP2AP002_CON_OCON_ENABLE);
	if (ret)
		dev_err(gp2ap002->dev, "error setting up VOUT control\n");

	return IRQ_HANDLED;
}

/*
 * This array maps current and lux.
 *
 * Ambient light sensing range is 3 to 55000 lux.
 *
 * This mapping is based on the following formula.
 * illuminance = 10 ^ (current[mA] / 10)
 *
 * When the ADC measures 0, return 0 lux.
 */
static const u16 gp2ap002_illuminance_table[] = {
	0, 1, 1, 2, 2, 3, 4, 5, 6, 8, 10, 12, 16, 20, 25, 32, 40, 50, 63, 79,
	100, 126, 158, 200, 251, 316, 398, 501, 631, 794, 1000, 1259, 1585,
	1995, 2512, 3162, 3981, 5012, 6310, 7943, 10000, 12589, 15849, 19953,
	25119, 31623, 39811, 50119,
};

static int gp2ap002_get_lux(struct gp2ap002 *gp2ap002)
{
	int ret, res;
	u16 lux;

	ret = iio_read_channel_processed(gp2ap002->alsout, &res);
	if (ret < 0)
		return ret;

	dev_dbg(gp2ap002->dev, "read %d mA from ADC\n", res);

	/* ensure we don't under/overflow */
	res = clamp(res, 0, (int)ARRAY_SIZE(gp2ap002_illuminance_table) - 1);
	lux = gp2ap002_illuminance_table[res];

	return (int)lux;
}

static int gp2ap002_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan,
			   int *val, int *val2, long mask)
{
	struct gp2ap002 *gp2ap002 = iio_priv(indio_dev);
	int ret;

	pm_runtime_get_sync(gp2ap002->dev);

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		switch (chan->type) {
		case IIO_LIGHT:
			ret = gp2ap002_get_lux(gp2ap002);
			if (ret < 0)
				return ret;
			*val = ret;
			ret = IIO_VAL_INT;
			goto out;
		default:
			ret = -EINVAL;
			goto out;
		}
	default:
		ret = -EINVAL;
	}

out:
	pm_runtime_mark_last_busy(gp2ap002->dev);
	pm_runtime_put_autosuspend(gp2ap002->dev);

	return ret;
}

static int gp2ap002_init(struct gp2ap002 *gp2ap002)
{
	int ret;

	/* Set up the IR LED resistance */
	ret = regmap_write(gp2ap002->map, GP2AP002_GAIN,
			   GP2AP002_GAIN_LED_NORMAL);
	if (ret) {
		dev_err(gp2ap002->dev, "error setting up LED gain\n");
		return ret;
	}
	ret = regmap_write(gp2ap002->map, GP2AP002_HYS, gp2ap002->hys_far);
	if (ret) {
		dev_err(gp2ap002->dev,
			"error setting up proximity hysteresis\n");
		return ret;
	}

	/* Disable internal frequency hopping */
	ret = regmap_write(gp2ap002->map, GP2AP002_CYCLE,
			   GP2AP002_CYCLE_OSC_INEFFECTIVE);
	if (ret) {
		dev_err(gp2ap002->dev,
			"error setting up internal frequency hopping\n");
		return ret;
	}

	/* Enable chip and IRQ, disable analog sleep */
	ret = regmap_write(gp2ap002->map, GP2AP002_OPMOD,
			   GP2AP002_OPMOD_SSD_OPERATING |
			   GP2AP002_OPMOD_VCON_IRQ);
	if (ret) {
		dev_err(gp2ap002->dev, "error setting up operation mode\n");
		return ret;
	}

	/* Interrupt on VOUT enabled */
	ret = regmap_write(gp2ap002->map, GP2AP002_CON,
			   GP2AP002_CON_OCON_ENABLE);
	if (ret)
		dev_err(gp2ap002->dev, "error setting up VOUT control\n");

	return ret;
}

static int gp2ap002_read_event_config(struct iio_dev *indio_dev,
				      const struct iio_chan_spec *chan,
				      enum iio_event_type type,
				      enum iio_event_direction dir)
{
	struct gp2ap002 *gp2ap002 = iio_priv(indio_dev);

	/*
	 * We just keep track of this internally, as it is not possible to
	 * query the hardware.
	 */
	return gp2ap002->enabled;
}

static int gp2ap002_write_event_config(struct iio_dev *indio_dev,
				       const struct iio_chan_spec *chan,
				       enum iio_event_type type,
				       enum iio_event_direction dir,
				       bool state)
{
	struct gp2ap002 *gp2ap002 = iio_priv(indio_dev);

	if (state) {
		/*
		 * This will bring the regulators up (unless they are on
		 * already) and reintialize the sensor by using runtime_pm
		 * callbacks.
		 */
		pm_runtime_get_sync(gp2ap002->dev);
		gp2ap002->enabled = true;
	} else {
		pm_runtime_mark_last_busy(gp2ap002->dev);
		pm_runtime_put_autosuspend(gp2ap002->dev);
		gp2ap002->enabled = false;
	}

	return 0;
}

static const struct iio_info gp2ap002_info = {
	.read_raw = gp2ap002_read_raw,
	.read_event_config = gp2ap002_read_event_config,
	.write_event_config = gp2ap002_write_event_config,
};

static const struct iio_event_spec gp2ap002_events[] = {
	{
		.type = IIO_EV_TYPE_THRESH,
		.dir = IIO_EV_DIR_EITHER,
		.mask_separate = BIT(IIO_EV_INFO_ENABLE),
	},
};

static const struct iio_chan_spec gp2ap002_channels[] = {
	{
		.type = IIO_PROXIMITY,
		.event_spec = gp2ap002_events,
		.num_event_specs = ARRAY_SIZE(gp2ap002_events),
	},
	{
		.type = IIO_LIGHT,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.channel = GP2AP002_ALS_CHANNEL,
	},
};

/*
 * We need a special regmap because this hardware expects to
 * write single bytes to registers but read a 16bit word on some
 * variants and discard the lower 8 bits so combine
 * i2c_smbus_read_word_data() with i2c_smbus_write_byte_data()
 * selectively like this.
 */
static int gp2ap002_regmap_i2c_read(void *context, unsigned int reg,
				    unsigned int *val)
{
	struct device *dev = context;
	struct i2c_client *i2c = to_i2c_client(dev);
	int ret;

	ret = i2c_smbus_read_word_data(i2c, reg);
	if (ret < 0)
		return ret;

	*val = (ret >> 8) & 0xFF;

	return 0;
}

static int gp2ap002_regmap_i2c_write(void *context, unsigned int reg,
				     unsigned int val)
{
	struct device *dev = context;
	struct i2c_client *i2c = to_i2c_client(dev);

	return i2c_smbus_write_byte_data(i2c, reg, val);
}

static const struct regmap_bus gp2ap002_regmap_bus = {
	.reg_read = gp2ap002_regmap_i2c_read,
	.reg_write = gp2ap002_regmap_i2c_write,
};

static int gp2ap002_probe(struct i2c_client *client)
{
	struct gp2ap002 *gp2ap002;
	struct iio_dev *indio_dev;
	struct device *dev = &client->dev;
	enum iio_chan_type ch_type;
	static const struct regmap_config config = {
		.reg_bits = 8,
		.val_bits = 8,
		.max_register = GP2AP002_CON,
	};
	struct regmap *regmap;
	int num_chan;
	const char *compat;
	u8 val;
	int ret;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*gp2ap002));
	if (!indio_dev)
		return -ENOMEM;
	i2c_set_clientdata(client, indio_dev);

	gp2ap002 = iio_priv(indio_dev);
	gp2ap002->dev = dev;

	/*
	 * Check the device compatible like this makes it possible to use
	 * ACPI PRP0001 for registering the sensor using device tree
	 * properties.
	 */
	ret = device_property_read_string(dev, "compatible", &compat);
	if (ret) {
		dev_err(dev, "cannot check compatible\n");
		return ret;
	}
	gp2ap002->is_gp2ap002s00f = !strcmp(compat, "sharp,gp2ap002s00f");

	regmap = devm_regmap_init(dev, &gp2ap002_regmap_bus, dev, &config);
	if (IS_ERR(regmap)) {
		dev_err(dev, "Failed to register i2c regmap %ld\n", PTR_ERR(regmap));
		return PTR_ERR(regmap);
	}
	gp2ap002->map = regmap;

	/*
	 * The hysteresis settings are coded into the device tree as values
	 * to be written into the hysteresis register. The datasheet defines
	 * modes "A", "B1" and "B2" with fixed values to be use but vendor
	 * code trees for actual devices are tweaking these values and refer to
	 * modes named things like "B1.5". To be able to support any devices,
	 * we allow passing an arbitrary hysteresis setting for "near" and
	 * "far".
	 */

	/* Check the device tree for the IR LED hysteresis */
	ret = device_property_read_u8(dev, "sharp,proximity-far-hysteresis",
				      &val);
	if (ret) {
		dev_err(dev, "failed to obtain proximity far setting\n");
		return ret;
	}
	dev_dbg(dev, "proximity far setting %02x\n", val);
	gp2ap002->hys_far = val;

	ret = device_property_read_u8(dev, "sharp,proximity-close-hysteresis",
				      &val);
	if (ret) {
		dev_err(dev, "failed to obtain proximity close setting\n");
		return ret;
	}
	dev_dbg(dev, "proximity close setting %02x\n", val);
	gp2ap002->hys_close = val;

	/* The GP2AP002A00F has a light sensor too */
	if (!gp2ap002->is_gp2ap002s00f) {
		gp2ap002->alsout = devm_iio_channel_get(dev, "alsout");
		if (IS_ERR(gp2ap002->alsout)) {
			ret = PTR_ERR(gp2ap002->alsout);
			ret = (ret == -ENODEV) ? -EPROBE_DEFER : ret;
			return dev_err_probe(dev, ret, "failed to get ALSOUT ADC channel\n");
		}
		ret = iio_get_channel_type(gp2ap002->alsout, &ch_type);
		if (ret < 0)
			return ret;
		if (ch_type != IIO_CURRENT) {
			dev_err(dev,
				"wrong type of IIO channel specified for ALSOUT\n");
			return -EINVAL;
		}
	}

	gp2ap002->vdd = devm_regulator_get(dev, "vdd");
	if (IS_ERR(gp2ap002->vdd))
		return dev_err_probe(dev, PTR_ERR(gp2ap002->vdd),
				     "failed to get VDD regulator\n");

	gp2ap002->vio = devm_regulator_get(dev, "vio");
	if (IS_ERR(gp2ap002->vio))
		return dev_err_probe(dev, PTR_ERR(gp2ap002->vio),
				     "failed to get VIO regulator\n");

	/* Operating voltage 2.4V .. 3.6V according to datasheet */
	ret = regulator_set_voltage(gp2ap002->vdd, 2400000, 3600000);
	if (ret) {
		dev_err(dev, "failed to sett VDD voltage\n");
		return ret;
	}

	/* VIO should be between 1.65V and VDD */
	ret = regulator_get_voltage(gp2ap002->vdd);
	if (ret < 0) {
		dev_err(dev, "failed to get VDD voltage\n");
		return ret;
	}
	ret = regulator_set_voltage(gp2ap002->vio, 1650000, ret);
	if (ret) {
		dev_err(dev, "failed to set VIO voltage\n");
		return ret;
	}

	ret = regulator_enable(gp2ap002->vdd);
	if (ret) {
		dev_err(dev, "failed to enable VDD regulator\n");
		return ret;
	}
	ret = regulator_enable(gp2ap002->vio);
	if (ret) {
		dev_err(dev, "failed to enable VIO regulator\n");
		goto out_disable_vdd;
	}

	msleep(20);

	/*
	 * Initialize the device and signal to runtime PM that now we are
	 * definitely up and using power.
	 */
	ret = gp2ap002_init(gp2ap002);
	if (ret) {
		dev_err(dev, "initialization failed\n");
		goto out_disable_vio;
	}
	pm_runtime_get_noresume(dev);
	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);
	gp2ap002->enabled = false;

	ret = devm_request_threaded_irq(dev, client->irq, NULL,
					gp2ap002_prox_irq, IRQF_ONESHOT,
					"gp2ap002", indio_dev);
	if (ret) {
		dev_err(dev, "unable to request IRQ\n");
		goto out_put_pm;
	}
	gp2ap002->irq = client->irq;

	/*
	 * As the device takes 20 ms + regulator delay to come up with a fresh
	 * measurement after power-on, do not shut it down unnecessarily.
	 * Set autosuspend to a one second.
	 */
	pm_runtime_set_autosuspend_delay(dev, 1000);
	pm_runtime_use_autosuspend(dev);
	pm_runtime_put(dev);

	indio_dev->info = &gp2ap002_info;
	indio_dev->name = "gp2ap002";
	indio_dev->channels = gp2ap002_channels;
	/* Skip light channel for the proximity-only sensor */
	num_chan = ARRAY_SIZE(gp2ap002_channels);
	if (gp2ap002->is_gp2ap002s00f)
		num_chan--;
	indio_dev->num_channels = num_chan;
	indio_dev->modes = INDIO_DIRECT_MODE;

	ret = iio_device_register(indio_dev);
	if (ret)
		goto out_disable_pm;
	dev_dbg(dev, "Sharp GP2AP002 probed successfully\n");

	return 0;

out_put_pm:
	pm_runtime_put_noidle(dev);
out_disable_pm:
	pm_runtime_disable(dev);
out_disable_vio:
	regulator_disable(gp2ap002->vio);
out_disable_vdd:
	regulator_disable(gp2ap002->vdd);
	return ret;
}

static void gp2ap002_remove(struct i2c_client *client)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(client);
	struct gp2ap002 *gp2ap002 = iio_priv(indio_dev);
	struct device *dev = &client->dev;

	pm_runtime_get_sync(dev);
	pm_runtime_put_noidle(dev);
	pm_runtime_disable(dev);
	iio_device_unregister(indio_dev);
	regulator_disable(gp2ap002->vio);
	regulator_disable(gp2ap002->vdd);
}

static int gp2ap002_runtime_suspend(struct device *dev)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct gp2ap002 *gp2ap002 = iio_priv(indio_dev);
	int ret;

	/* Deactivate the IRQ */
	disable_irq(gp2ap002->irq);

	/* Disable chip and IRQ, everything off */
	ret = regmap_write(gp2ap002->map, GP2AP002_OPMOD, 0x00);
	if (ret) {
		dev_err(gp2ap002->dev, "error setting up operation mode\n");
		return ret;
	}
	/*
	 * As these regulators may be shared, at least we are now in
	 * sleep even if the regulators aren't really turned off.
	 */
	regulator_disable(gp2ap002->vio);
	regulator_disable(gp2ap002->vdd);

	return 0;
}

static int gp2ap002_runtime_resume(struct device *dev)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct gp2ap002 *gp2ap002 = iio_priv(indio_dev);
	int ret;

	ret = regulator_enable(gp2ap002->vdd);
	if (ret) {
		dev_err(dev, "failed to enable VDD regulator in resume path\n");
		return ret;
	}
	ret = regulator_enable(gp2ap002->vio);
	if (ret) {
		dev_err(dev, "failed to enable VIO regulator in resume path\n");
		return ret;
	}

	msleep(20);

	ret = gp2ap002_init(gp2ap002);
	if (ret) {
		dev_err(dev, "re-initialization failed\n");
		return ret;
	}

	/* Re-activate the IRQ */
	enable_irq(gp2ap002->irq);

	return 0;
}

static DEFINE_RUNTIME_DEV_PM_OPS(gp2ap002_dev_pm_ops, gp2ap002_runtime_suspend,
				 gp2ap002_runtime_resume, NULL);

static const struct i2c_device_id gp2ap002_id_table[] = {
	{ "gp2ap002" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, gp2ap002_id_table);

static const struct of_device_id gp2ap002_of_match[] = {
	{ .compatible = "sharp,gp2ap002a00f" },
	{ .compatible = "sharp,gp2ap002s00f" },
	{ },
};
MODULE_DEVICE_TABLE(of, gp2ap002_of_match);

static struct i2c_driver gp2ap002_driver = {
	.driver = {
		.name = "gp2ap002",
		.of_match_table = gp2ap002_of_match,
		.pm = pm_ptr(&gp2ap002_dev_pm_ops),
	},
	.probe = gp2ap002_probe,
	.remove = gp2ap002_remove,
	.id_table = gp2ap002_id_table,
};
module_i2c_driver(gp2ap002_driver);

MODULE_AUTHOR("Linus Walleij <linus.walleij@linaro.org>");
MODULE_DESCRIPTION("GP2AP002 ambient light and proximity sensor driver");
MODULE_LICENSE("GPL v2");

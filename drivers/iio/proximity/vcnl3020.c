// SPDX-License-Identifier: GPL-2.0-only
/*
 * Support for Vishay VCNL3020 proximity sensor on i2c bus.
 * Based on Vishay VCNL4000 driver code.
 */

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/err.h>
#include <linux/delay.h>
#include <linux/regmap.h>
#include <linux/interrupt.h>

#include <linux/iio/iio.h>
#include <linux/iio/events.h>

#define VCNL3020_PROD_ID	0x21

#define VCNL_COMMAND		0x80 /* Command register */
#define VCNL_PROD_REV		0x81 /* Product ID and Revision ID */
#define VCNL_PROXIMITY_RATE	0x82 /* Rate of Proximity Measurement */
#define VCNL_LED_CURRENT	0x83 /* IR LED current for proximity mode */
#define VCNL_PS_RESULT_HI	0x87 /* Proximity result register, MSB */
#define VCNL_PS_RESULT_LO	0x88 /* Proximity result register, LSB */
#define VCNL_PS_ICR		0x89 /* Interrupt Control Register */
#define VCNL_PS_LO_THR_HI	0x8a /* High byte of low threshold value */
#define VCNL_PS_LO_THR_LO	0x8b /* Low byte of low threshold value */
#define VCNL_PS_HI_THR_HI	0x8c /* High byte of high threshold value */
#define VCNL_PS_HI_THR_LO	0x8d /* Low byte of high threshold value */
#define VCNL_ISR		0x8e /* Interrupt Status Register */
#define VCNL_PS_MOD_ADJ		0x8f /* Proximity Modulator Timing Adjustment */

/* Bit masks for COMMAND register */
#define VCNL_PS_RDY		BIT(5) /* proximity data ready? */
#define VCNL_PS_OD		BIT(3) /* start on-demand proximity
					* measurement
					*/

/* Enables periodic proximity measurement */
#define VCNL_PS_EN		BIT(1)

/* Enables state machine and LP oscillator for self timed  measurements */
#define VCNL_PS_SELFTIMED_EN	BIT(0)

/* Bit masks for ICR */

/* Enable interrupts on low or high thresholds */
#define  VCNL_ICR_THRES_EN	BIT(1)

/* Bit masks for ISR */
#define VCNL_INT_TH_HI		BIT(0)	/* High threshold hit */
#define VCNL_INT_TH_LOW		BIT(1)	/* Low threshold hit */

#define VCNL_ON_DEMAND_TIMEOUT_US	100000
#define VCNL_POLL_US			20000

static const int vcnl3020_prox_sampling_frequency[][2] = {
	{1, 950000},
	{3, 906250},
	{7, 812500},
	{16, 625000},
	{31, 250000},
	{62, 500000},
	{125, 0},
	{250, 0},
};

/**
 * struct vcnl3020_data - vcnl3020 specific data.
 * @regmap:	device register map.
 * @dev:	vcnl3020 device.
 * @rev:	revision id.
 * @lock:	lock for protecting access to device hardware registers.
 * @buf:	__be16 buffer.
 */
struct vcnl3020_data {
	struct regmap *regmap;
	struct device *dev;
	u8 rev;
	struct mutex lock;
	__be16 buf;
};

/**
 * struct vcnl3020_property - vcnl3020 property.
 * @name:	property name.
 * @reg:	i2c register offset.
 * @conversion_func:	conversion function.
 */
struct vcnl3020_property {
	const char *name;
	u32 reg;
	u32 (*conversion_func)(u32 *val);
};

static u32 microamp_to_reg(u32 *val)
{
	/*
	 * An example of conversion from uA to reg val:
	 * 200000 uA == 200 mA == 20
	 */
	return *val /= 10000;
};

static const struct vcnl3020_property vcnl3020_led_current_property = {
	.name = "vishay,led-current-microamp",
	.reg = VCNL_LED_CURRENT,
	.conversion_func = microamp_to_reg,
};

static int vcnl3020_get_and_apply_property(struct vcnl3020_data *data,
					   const struct vcnl3020_property *prop)
{
	int rc;
	u32 val;

	rc = device_property_read_u32(data->dev, prop->name, &val);
	if (rc)
		return 0;

	if (prop->conversion_func)
		prop->conversion_func(&val);

	rc = regmap_write(data->regmap, prop->reg, val);
	if (rc) {
		dev_err(data->dev, "Error (%d) setting property (%s)\n",
			rc, prop->name);
	}

	return rc;
}

static int vcnl3020_init(struct vcnl3020_data *data)
{
	int rc;
	unsigned int reg;

	rc = regmap_read(data->regmap, VCNL_PROD_REV, &reg);
	if (rc) {
		dev_err(data->dev,
			"Error (%d) reading product revision\n", rc);
		return rc;
	}

	if (reg != VCNL3020_PROD_ID) {
		dev_err(data->dev,
			"Product id (%x) did not match vcnl3020 (%x)\n", reg,
			VCNL3020_PROD_ID);
		return -ENODEV;
	}

	data->rev = reg;
	mutex_init(&data->lock);

	return vcnl3020_get_and_apply_property(data,
					       &vcnl3020_led_current_property);
};

static bool vcnl3020_is_in_periodic_mode(struct vcnl3020_data *data)
{
	int rc;
	unsigned int cmd;

	rc = regmap_read(data->regmap, VCNL_COMMAND, &cmd);
	if (rc) {
		dev_err(data->dev,
			"Error (%d) reading command register\n", rc);
		return false;
	}

	return !!(cmd & VCNL_PS_SELFTIMED_EN);
}

static int vcnl3020_measure_proximity(struct vcnl3020_data *data, int *val)
{
	int rc;
	unsigned int reg;

	mutex_lock(&data->lock);

	/* Protect against event capture. */
	if (vcnl3020_is_in_periodic_mode(data)) {
		rc = -EBUSY;
		goto err_unlock;
	}

	rc = regmap_write(data->regmap, VCNL_COMMAND, VCNL_PS_OD);
	if (rc)
		goto err_unlock;

	/* wait for data to become ready */
	rc = regmap_read_poll_timeout(data->regmap, VCNL_COMMAND, reg,
				      reg & VCNL_PS_RDY, VCNL_POLL_US,
				      VCNL_ON_DEMAND_TIMEOUT_US);
	if (rc) {
		dev_err(data->dev,
			"Error (%d) reading vcnl3020 command register\n", rc);
		goto err_unlock;
	}

	/* high & low result bytes read */
	rc = regmap_bulk_read(data->regmap, VCNL_PS_RESULT_HI, &data->buf,
			      sizeof(data->buf));
	if (rc)
		goto err_unlock;

	*val = be16_to_cpu(data->buf);

err_unlock:
	mutex_unlock(&data->lock);

	return rc;
}

static int vcnl3020_read_proxy_samp_freq(struct vcnl3020_data *data, int *val,
					 int *val2)
{
	int rc;
	unsigned int prox_rate;

	rc = regmap_read(data->regmap, VCNL_PROXIMITY_RATE, &prox_rate);
	if (rc)
		return rc;

	if (prox_rate >= ARRAY_SIZE(vcnl3020_prox_sampling_frequency))
		return -EINVAL;

	*val = vcnl3020_prox_sampling_frequency[prox_rate][0];
	*val2 = vcnl3020_prox_sampling_frequency[prox_rate][1];

	return 0;
}

static int vcnl3020_write_proxy_samp_freq(struct vcnl3020_data *data, int val,
					  int val2)
{
	unsigned int i;
	int index = -1;
	int rc;

	mutex_lock(&data->lock);

	/* Protect against event capture. */
	if (vcnl3020_is_in_periodic_mode(data)) {
		rc = -EBUSY;
		goto err_unlock;
	}

	for (i = 0; i < ARRAY_SIZE(vcnl3020_prox_sampling_frequency); i++) {
		if (val == vcnl3020_prox_sampling_frequency[i][0] &&
		    val2 == vcnl3020_prox_sampling_frequency[i][1]) {
			index = i;
			break;
		}
	}

	if (index < 0) {
		rc = -EINVAL;
		goto err_unlock;
	}

	rc = regmap_write(data->regmap, VCNL_PROXIMITY_RATE, index);
	if (rc)
		dev_err(data->dev,
			"Error (%d) writing proximity rate register\n", rc);

err_unlock:
	mutex_unlock(&data->lock);

	return rc;
}

static bool vcnl3020_is_thr_enabled(struct vcnl3020_data *data)
{
	int rc;
	unsigned int icr;

	rc = regmap_read(data->regmap, VCNL_PS_ICR, &icr);
	if (rc) {
		dev_err(data->dev,
			"Error (%d) reading ICR register\n", rc);
		return false;
	}

	return !!(icr & VCNL_ICR_THRES_EN);
}

static int vcnl3020_read_event(struct iio_dev *indio_dev,
			       const struct iio_chan_spec *chan,
			       enum iio_event_type type,
			       enum iio_event_direction dir,
			       enum iio_event_info info,
			       int *val, int *val2)
{
	int rc;
	struct vcnl3020_data *data = iio_priv(indio_dev);

	switch (info) {
	case IIO_EV_INFO_VALUE:
		switch (dir) {
		case IIO_EV_DIR_RISING:
			rc = regmap_bulk_read(data->regmap, VCNL_PS_HI_THR_HI,
					      &data->buf, sizeof(data->buf));
			if (rc < 0)
				return rc;
			*val = be16_to_cpu(data->buf);
			return IIO_VAL_INT;
		case IIO_EV_DIR_FALLING:
			rc = regmap_bulk_read(data->regmap, VCNL_PS_LO_THR_HI,
					      &data->buf, sizeof(data->buf));
			if (rc < 0)
				return rc;
			*val = be16_to_cpu(data->buf);
			return IIO_VAL_INT;
		default:
			return -EINVAL;
		}
	default:
		return -EINVAL;
	}
}

static int vcnl3020_write_event(struct iio_dev *indio_dev,
				const struct iio_chan_spec *chan,
				enum iio_event_type type,
				enum iio_event_direction dir,
				enum iio_event_info info,
				int val, int val2)
{
	int rc;
	struct vcnl3020_data *data = iio_priv(indio_dev);

	mutex_lock(&data->lock);

	switch (info) {
	case IIO_EV_INFO_VALUE:
		switch (dir) {
		case IIO_EV_DIR_RISING:
			/* 16 bit word/ low * high */
			data->buf = cpu_to_be16(val);
			rc = regmap_bulk_write(data->regmap, VCNL_PS_HI_THR_HI,
					       &data->buf, sizeof(data->buf));
			if (rc < 0)
				goto err_unlock;
			rc = IIO_VAL_INT;
			goto err_unlock;
		case IIO_EV_DIR_FALLING:
			data->buf = cpu_to_be16(val);
			rc = regmap_bulk_write(data->regmap, VCNL_PS_LO_THR_HI,
					       &data->buf, sizeof(data->buf));
			if (rc < 0)
				goto err_unlock;
			rc = IIO_VAL_INT;
			goto err_unlock;
		default:
			rc = -EINVAL;
			goto err_unlock;
		}
	default:
		rc = -EINVAL;
		goto err_unlock;
	}
err_unlock:
	mutex_unlock(&data->lock);

	return rc;
}

static int vcnl3020_enable_periodic(struct iio_dev *indio_dev,
				    struct vcnl3020_data *data)
{
	int rc;
	int cmd;

	mutex_lock(&data->lock);

	/* Enable periodic measurement of proximity data. */
	cmd = VCNL_PS_EN | VCNL_PS_SELFTIMED_EN;

	rc = regmap_write(data->regmap, VCNL_COMMAND, cmd);
	if (rc) {
		dev_err(data->dev,
			"Error (%d) writing command register\n", rc);
		goto err_unlock;
	}

	/*
	 * Enable interrupts on threshold, for proximity data by
	 * default.
	 */
	rc = regmap_write(data->regmap, VCNL_PS_ICR, VCNL_ICR_THRES_EN);
	if (rc)
		dev_err(data->dev,
			"Error (%d) reading ICR register\n", rc);

err_unlock:
	mutex_unlock(&data->lock);

	return rc;
}

static int vcnl3020_disable_periodic(struct iio_dev *indio_dev,
				     struct vcnl3020_data *data)
{
	int rc;

	mutex_lock(&data->lock);

	rc = regmap_write(data->regmap, VCNL_COMMAND, 0);
	if (rc) {
		dev_err(data->dev,
			"Error (%d) writing command register\n", rc);
		goto err_unlock;
	}

	rc = regmap_write(data->regmap, VCNL_PS_ICR, 0);
	if (rc) {
		dev_err(data->dev,
			"Error (%d) writing ICR register\n", rc);
		goto err_unlock;
	}

	/* Clear interrupt flag bit */
	rc = regmap_write(data->regmap, VCNL_ISR, 0);
	if (rc)
		dev_err(data->dev,
			"Error (%d) writing ISR register\n", rc);

err_unlock:
	mutex_unlock(&data->lock);

	return rc;
}

static int vcnl3020_config_threshold(struct iio_dev *indio_dev, bool state)
{
	struct vcnl3020_data *data = iio_priv(indio_dev);

	if (state) {
		return vcnl3020_enable_periodic(indio_dev, data);
	} else {
		if (!vcnl3020_is_thr_enabled(data))
			return 0;
		return vcnl3020_disable_periodic(indio_dev, data);
	}
}

static int vcnl3020_write_event_config(struct iio_dev *indio_dev,
				       const struct iio_chan_spec *chan,
				       enum iio_event_type type,
				       enum iio_event_direction dir,
				       bool state)
{
	switch (chan->type) {
	case IIO_PROXIMITY:
		return vcnl3020_config_threshold(indio_dev, state);
	default:
		return -EINVAL;
	}
}

static int vcnl3020_read_event_config(struct iio_dev *indio_dev,
				      const struct iio_chan_spec *chan,
				      enum iio_event_type type,
				      enum iio_event_direction dir)
{
	struct vcnl3020_data *data = iio_priv(indio_dev);

	switch (chan->type) {
	case IIO_PROXIMITY:
		return vcnl3020_is_thr_enabled(data);
	default:
		return -EINVAL;
	}
}

static const struct iio_event_spec vcnl3020_event_spec[] = {
	{
		.type = IIO_EV_TYPE_THRESH,
		.dir = IIO_EV_DIR_RISING,
		.mask_separate = BIT(IIO_EV_INFO_VALUE),
	}, {
		.type = IIO_EV_TYPE_THRESH,
		.dir = IIO_EV_DIR_FALLING,
		.mask_separate = BIT(IIO_EV_INFO_VALUE),
	}, {
		.type = IIO_EV_TYPE_THRESH,
		.dir = IIO_EV_DIR_EITHER,
		.mask_separate = BIT(IIO_EV_INFO_ENABLE),
	},
};

static const struct iio_chan_spec vcnl3020_channels[] = {
	{
		.type = IIO_PROXIMITY,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
				      BIT(IIO_CHAN_INFO_SAMP_FREQ),
		.info_mask_separate_available = BIT(IIO_CHAN_INFO_SAMP_FREQ),
		.event_spec = vcnl3020_event_spec,
		.num_event_specs = ARRAY_SIZE(vcnl3020_event_spec),
	},
};

static int vcnl3020_read_raw(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan, int *val,
			     int *val2, long mask)
{
	int rc;
	struct vcnl3020_data *data = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		rc = vcnl3020_measure_proximity(data, val);
		if (rc)
			return rc;
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SAMP_FREQ:
		rc = vcnl3020_read_proxy_samp_freq(data, val, val2);
		if (rc < 0)
			return rc;
		return IIO_VAL_INT_PLUS_MICRO;
	default:
		return -EINVAL;
	}
}

static int vcnl3020_write_raw(struct iio_dev *indio_dev,
			      struct iio_chan_spec const *chan,
			      int val, int val2, long mask)
{
	struct vcnl3020_data *data = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_SAMP_FREQ:
		return vcnl3020_write_proxy_samp_freq(data, val, val2);
	default:
		return -EINVAL;
	}
}

static int vcnl3020_read_avail(struct iio_dev *indio_dev,
			       struct iio_chan_spec const *chan,
			       const int **vals, int *type, int *length,
			       long mask)
{
	switch (mask) {
	case IIO_CHAN_INFO_SAMP_FREQ:
		*vals = (int *)vcnl3020_prox_sampling_frequency;
		*type = IIO_VAL_INT_PLUS_MICRO;
		*length = 2 * ARRAY_SIZE(vcnl3020_prox_sampling_frequency);
		return IIO_AVAIL_LIST;
	default:
		return -EINVAL;
	}
}

static const struct iio_info vcnl3020_info = {
	.read_raw = vcnl3020_read_raw,
	.write_raw = vcnl3020_write_raw,
	.read_avail = vcnl3020_read_avail,
	.read_event_value = vcnl3020_read_event,
	.write_event_value = vcnl3020_write_event,
	.read_event_config = vcnl3020_read_event_config,
	.write_event_config = vcnl3020_write_event_config,
};

static const struct regmap_config vcnl3020_regmap_config = {
	.reg_bits	= 8,
	.val_bits	= 8,
	.max_register	= VCNL_PS_MOD_ADJ,
};

static irqreturn_t vcnl3020_handle_irq_thread(int irq, void *p)
{
	struct iio_dev *indio_dev = p;
	struct vcnl3020_data *data = iio_priv(indio_dev);
	unsigned int isr;
	int rc;

	rc = regmap_read(data->regmap, VCNL_ISR, &isr);
	if (rc) {
		dev_err(data->dev, "Error (%d) reading reg (0x%x)\n",
			rc, VCNL_ISR);
		return IRQ_HANDLED;
	}

	if (!(isr & VCNL_ICR_THRES_EN))
		return IRQ_NONE;

	iio_push_event(indio_dev,
		       IIO_UNMOD_EVENT_CODE(IIO_PROXIMITY, 1,
				            IIO_EV_TYPE_THRESH,
				            IIO_EV_DIR_RISING),
		       iio_get_time_ns(indio_dev));

	rc = regmap_write(data->regmap, VCNL_ISR, isr & VCNL_ICR_THRES_EN);
	if (rc)
		dev_err(data->dev, "Error (%d) writing in reg (0x%x)\n",
			rc, VCNL_ISR);

	return IRQ_HANDLED;
}

static int vcnl3020_probe(struct i2c_client *client)
{
	struct vcnl3020_data *data;
	struct iio_dev *indio_dev;
	struct regmap *regmap;
	int rc;

	regmap = devm_regmap_init_i2c(client, &vcnl3020_regmap_config);
	if (IS_ERR(regmap)) {
		dev_err(&client->dev, "regmap_init failed\n");
		return PTR_ERR(regmap);
	}

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;

	data = iio_priv(indio_dev);
	i2c_set_clientdata(client, indio_dev);
	data->regmap = regmap;
	data->dev = &client->dev;

	rc = vcnl3020_init(data);
	if (rc)
		return rc;

	indio_dev->info = &vcnl3020_info;
	indio_dev->channels = vcnl3020_channels;
	indio_dev->num_channels = ARRAY_SIZE(vcnl3020_channels);
	indio_dev->name = "vcnl3020";
	indio_dev->modes = INDIO_DIRECT_MODE;

	if (client->irq) {
		rc = devm_request_threaded_irq(&client->dev, client->irq,
					       NULL, vcnl3020_handle_irq_thread,
					       IRQF_ONESHOT, indio_dev->name,
					       indio_dev);
		if (rc) {
			dev_err(&client->dev,
				"Error (%d) irq request failed (%u)\n", rc,
				client->irq);
			return rc;
		}
	}

	return devm_iio_device_register(&client->dev, indio_dev);
}

static const struct of_device_id vcnl3020_of_match[] = {
	{
		.compatible = "vishay,vcnl3020",
	},
	{ }
};
MODULE_DEVICE_TABLE(of, vcnl3020_of_match);

static struct i2c_driver vcnl3020_driver = {
	.driver = {
		.name   = "vcnl3020",
		.of_match_table = vcnl3020_of_match,
	},
	.probe      = vcnl3020_probe,
};
module_i2c_driver(vcnl3020_driver);

MODULE_AUTHOR("Ivan Mikhaylov <i.mikhaylov@yadro.com>");
MODULE_DESCRIPTION("Vishay VCNL3020 proximity sensor driver");
MODULE_LICENSE("GPL");

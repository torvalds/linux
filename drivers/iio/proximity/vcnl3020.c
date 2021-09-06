// SPDX-License-Identifier: GPL-2.0-only
/*
 * Support for Vishay VCNL3020 proximity sensor on i2c bus.
 * Based on Vishay VCNL4000 driver code.
 *
 * TODO: interrupts.
 */

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/err.h>
#include <linux/delay.h>
#include <linux/regmap.h>

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>

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
 */
struct vcnl3020_data {
	struct regmap *regmap;
	struct device *dev;
	u8 rev;
	struct mutex lock;
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

static struct vcnl3020_property vcnl3020_led_current_property = {
	.name = "vishay,led-current-microamp",
	.reg = VCNL_LED_CURRENT,
	.conversion_func = microamp_to_reg,
};

static int vcnl3020_get_and_apply_property(struct vcnl3020_data *data,
					   struct vcnl3020_property prop)
{
	int rc;
	u32 val;

	rc = device_property_read_u32(data->dev, prop.name, &val);
	if (rc)
		return 0;

	if (prop.conversion_func)
		prop.conversion_func(&val);

	rc = regmap_write(data->regmap, prop.reg, val);
	if (rc) {
		dev_err(data->dev, "Error (%d) setting property (%s)\n",
			rc, prop.name);
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
					       vcnl3020_led_current_property);
};

static int vcnl3020_measure_proximity(struct vcnl3020_data *data, int *val)
{
	int rc;
	unsigned int reg;
	__be16 res;

	mutex_lock(&data->lock);

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
	rc = regmap_bulk_read(data->regmap, VCNL_PS_RESULT_HI, &res,
			      sizeof(res));
	if (rc)
		goto err_unlock;

	*val = be16_to_cpu(res);

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

	for (i = 0; i < ARRAY_SIZE(vcnl3020_prox_sampling_frequency); i++) {
		if (val == vcnl3020_prox_sampling_frequency[i][0] &&
		    val2 == vcnl3020_prox_sampling_frequency[i][1]) {
			index = i;
			break;
		}
	}

	if (index < 0)
		return -EINVAL;

	return regmap_write(data->regmap, VCNL_PROXIMITY_RATE, index);
}

static const struct iio_chan_spec vcnl3020_channels[] = {
	{
		.type = IIO_PROXIMITY,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
				      BIT(IIO_CHAN_INFO_SAMP_FREQ),
		.info_mask_separate_available = BIT(IIO_CHAN_INFO_SAMP_FREQ),
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
	int rc;
	struct vcnl3020_data *data = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_SAMP_FREQ:
		rc = iio_device_claim_direct_mode(indio_dev);
		if (rc)
			return rc;
		rc = vcnl3020_write_proxy_samp_freq(data, val, val2);
		iio_device_release_direct_mode(indio_dev);
		return rc;
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
};

static const struct regmap_config vcnl3020_regmap_config = {
	.reg_bits	= 8,
	.val_bits	= 8,
	.max_register	= VCNL_PS_MOD_ADJ,
};

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

	return devm_iio_device_register(&client->dev, indio_dev);
}

static const struct of_device_id vcnl3020_of_match[] = {
	{
		.compatible = "vishay,vcnl3020",
	},
	{}
};
MODULE_DEVICE_TABLE(of, vcnl3020_of_match);

static struct i2c_driver vcnl3020_driver = {
	.driver = {
		.name   = "vcnl3020",
		.of_match_table = vcnl3020_of_match,
	},
	.probe_new  = vcnl3020_probe,
};
module_i2c_driver(vcnl3020_driver);

MODULE_AUTHOR("Ivan Mikhaylov <i.mikhaylov@yadro.com>");
MODULE_DESCRIPTION("Vishay VCNL3020 proximity sensor driver");
MODULE_LICENSE("GPL");

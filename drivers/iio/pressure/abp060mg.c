// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2016 - Marcin Malagowski <mrc@bourne.st>
 */
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/iio/iio.h>

#define ABP060MG_ERROR_MASK   0xC000
#define ABP060MG_RESP_TIME_MS 40
#define ABP060MG_MIN_COUNTS   1638  /* = 0x0666 (10% of u14) */
#define ABP060MG_MAX_COUNTS   14745 /* = 0x3999 (90% of u14) */
#define ABP060MG_NUM_COUNTS   (ABP060MG_MAX_COUNTS - ABP060MG_MIN_COUNTS)

enum abp_variant {
	/* gage [kPa] */
	ABP006KG, ABP010KG, ABP016KG, ABP025KG, ABP040KG, ABP060KG, ABP100KG,
	ABP160KG, ABP250KG, ABP400KG, ABP600KG, ABP001GG,
	/* differential [kPa] */
	ABP006KD, ABP010KD, ABP016KD, ABP025KD, ABP040KD, ABP060KD, ABP100KD,
	ABP160KD, ABP250KD, ABP400KD,
	/* gage [psi] */
	ABP001PG, ABP005PG, ABP015PG, ABP030PG, ABP060PG, ABP100PG, ABP150PG,
	/* differential [psi] */
	ABP001PD, ABP005PD, ABP015PD, ABP030PD, ABP060PD,
};

struct abp_config {
	int min;
	int max;
};

static const struct abp_config abp_config[] = {
	/* mbar & kPa variants */
	[ABP006KG] = { .min =       0, .max =     6000 },
	[ABP010KG] = { .min =       0, .max =    10000 },
	[ABP016KG] = { .min =       0, .max =    16000 },
	[ABP025KG] = { .min =       0, .max =    25000 },
	[ABP040KG] = { .min =       0, .max =    40000 },
	[ABP060KG] = { .min =       0, .max =    60000 },
	[ABP100KG] = { .min =       0, .max =   100000 },
	[ABP160KG] = { .min =       0, .max =   160000 },
	[ABP250KG] = { .min =       0, .max =   250000 },
	[ABP400KG] = { .min =       0, .max =   400000 },
	[ABP600KG] = { .min =       0, .max =   600000 },
	[ABP001GG] = { .min =       0, .max =  1000000 },
	[ABP006KD] = { .min =   -6000, .max =     6000 },
	[ABP010KD] = { .min =  -10000, .max =    10000 },
	[ABP016KD] = { .min =  -16000, .max =    16000 },
	[ABP025KD] = { .min =  -25000, .max =    25000 },
	[ABP040KD] = { .min =  -40000, .max =    40000 },
	[ABP060KD] = { .min =  -60000, .max =    60000 },
	[ABP100KD] = { .min = -100000, .max =   100000 },
	[ABP160KD] = { .min = -160000, .max =   160000 },
	[ABP250KD] = { .min = -250000, .max =   250000 },
	[ABP400KD] = { .min = -400000, .max =   400000 },
	/* psi variants (1 psi ~ 6895 Pa) */
	[ABP001PG] = { .min =       0, .max =     6985 },
	[ABP005PG] = { .min =       0, .max =    34474 },
	[ABP015PG] = { .min =       0, .max =   103421 },
	[ABP030PG] = { .min =       0, .max =   206843 },
	[ABP060PG] = { .min =       0, .max =   413686 },
	[ABP100PG] = { .min =       0, .max =   689476 },
	[ABP150PG] = { .min =       0, .max =  1034214 },
	[ABP001PD] = { .min =   -6895, .max =     6895 },
	[ABP005PD] = { .min =  -34474, .max =    34474 },
	[ABP015PD] = { .min = -103421, .max =   103421 },
	[ABP030PD] = { .min = -206843, .max =   206843 },
	[ABP060PD] = { .min = -413686, .max =   413686 },
};

struct abp_state {
	struct i2c_client *client;
	struct mutex lock;

	/*
	 * bus-dependent MEASURE_REQUEST length.
	 * If no SMBUS_QUICK support, need to send dummy byte
	 */
	int mreq_len;

	/* model-dependent values (calculated on probe) */
	int scale;
	int offset;
};

static const struct iio_chan_spec abp060mg_channels[] = {
	{
		.type = IIO_PRESSURE,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
			BIT(IIO_CHAN_INFO_OFFSET) | BIT(IIO_CHAN_INFO_SCALE),
	},
};

static int abp060mg_get_measurement(struct abp_state *state, int *val)
{
	struct i2c_client *client = state->client;
	__be16 buf[2];
	u16 pressure;
	int ret;

	buf[0] = 0;
	ret = i2c_master_send(client, (u8 *)&buf, state->mreq_len);
	if (ret < 0)
		return ret;

	msleep_interruptible(ABP060MG_RESP_TIME_MS);

	ret = i2c_master_recv(client, (u8 *)&buf, sizeof(buf));
	if (ret < 0)
		return ret;

	pressure = be16_to_cpu(buf[0]);
	if (pressure & ABP060MG_ERROR_MASK)
		return -EIO;

	if (pressure < ABP060MG_MIN_COUNTS || pressure > ABP060MG_MAX_COUNTS)
		return -EIO;

	*val = pressure;

	return IIO_VAL_INT;
}

static int abp060mg_read_raw(struct iio_dev *indio_dev,
			struct iio_chan_spec const *chan, int *val,
			int *val2, long mask)
{
	struct abp_state *state = iio_priv(indio_dev);
	int ret;

	mutex_lock(&state->lock);

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		ret = abp060mg_get_measurement(state, val);
		break;
	case IIO_CHAN_INFO_OFFSET:
		*val = state->offset;
		ret = IIO_VAL_INT;
		break;
	case IIO_CHAN_INFO_SCALE:
		*val = state->scale;
		*val2 = ABP060MG_NUM_COUNTS * 1000; /* to kPa */
		ret = IIO_VAL_FRACTIONAL;
		break;
	default:
		ret = -EINVAL;
		break;
	}

	mutex_unlock(&state->lock);
	return ret;
}

static const struct iio_info abp060mg_info = {
	.read_raw = abp060mg_read_raw,
};

static void abp060mg_init_device(struct iio_dev *indio_dev, unsigned long id)
{
	struct abp_state *state = iio_priv(indio_dev);
	const struct abp_config *cfg = &abp_config[id];

	state->scale = cfg->max - cfg->min;
	state->offset = -ABP060MG_MIN_COUNTS;

	if (cfg->min < 0) /* differential */
		state->offset -= ABP060MG_NUM_COUNTS >> 1;
}

static int abp060mg_probe(struct i2c_client *client)
{
	const struct i2c_device_id *id = i2c_client_get_device_id(client);
	struct iio_dev *indio_dev;
	struct abp_state *state;
	unsigned long cfg_id = id->driver_data;

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*state));
	if (!indio_dev)
		return -ENOMEM;

	state = iio_priv(indio_dev);
	i2c_set_clientdata(client, state);
	state->client = client;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_QUICK))
		state->mreq_len = 1;

	abp060mg_init_device(indio_dev, cfg_id);

	indio_dev->name = dev_name(&client->dev);
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->info = &abp060mg_info;

	indio_dev->channels = abp060mg_channels;
	indio_dev->num_channels = ARRAY_SIZE(abp060mg_channels);

	mutex_init(&state->lock);

	return devm_iio_device_register(&client->dev, indio_dev);
}

static const struct i2c_device_id abp060mg_id_table[] = {
	/* mbar & kPa variants (abp060m [60 mbar] == abp006k [6 kPa]) */
	/*    gage: */
	{ .name = "abp060mg", .driver_data = ABP006KG },
	{ .name = "abp006kg", .driver_data = ABP006KG },
	{ .name = "abp100mg", .driver_data = ABP010KG },
	{ .name = "abp010kg", .driver_data = ABP010KG },
	{ .name = "abp160mg", .driver_data = ABP016KG },
	{ .name = "abp016kg", .driver_data = ABP016KG },
	{ .name = "abp250mg", .driver_data = ABP025KG },
	{ .name = "abp025kg", .driver_data = ABP025KG },
	{ .name = "abp400mg", .driver_data = ABP040KG },
	{ .name = "abp040kg", .driver_data = ABP040KG },
	{ .name = "abp600mg", .driver_data = ABP060KG },
	{ .name = "abp060kg", .driver_data = ABP060KG },
	{ .name = "abp001bg", .driver_data = ABP100KG },
	{ .name = "abp100kg", .driver_data = ABP100KG },
	{ .name = "abp1_6bg", .driver_data = ABP160KG },
	{ .name = "abp160kg", .driver_data = ABP160KG },
	{ .name = "abp2_5bg", .driver_data = ABP250KG },
	{ .name = "abp250kg", .driver_data = ABP250KG },
	{ .name = "abp004bg", .driver_data = ABP400KG },
	{ .name = "abp400kg", .driver_data = ABP400KG },
	{ .name = "abp006bg", .driver_data = ABP600KG },
	{ .name = "abp600kg", .driver_data = ABP600KG },
	{ .name = "abp010bg", .driver_data = ABP001GG },
	{ .name = "abp001gg", .driver_data = ABP001GG },
	/*    differential: */
	{ .name = "abp060md", .driver_data = ABP006KD },
	{ .name = "abp006kd", .driver_data = ABP006KD },
	{ .name = "abp100md", .driver_data = ABP010KD },
	{ .name = "abp010kd", .driver_data = ABP010KD },
	{ .name = "abp160md", .driver_data = ABP016KD },
	{ .name = "abp016kd", .driver_data = ABP016KD },
	{ .name = "abp250md", .driver_data = ABP025KD },
	{ .name = "abp025kd", .driver_data = ABP025KD },
	{ .name = "abp400md", .driver_data = ABP040KD },
	{ .name = "abp040kd", .driver_data = ABP040KD },
	{ .name = "abp600md", .driver_data = ABP060KD },
	{ .name = "abp060kd", .driver_data = ABP060KD },
	{ .name = "abp001bd", .driver_data = ABP100KD },
	{ .name = "abp100kd", .driver_data = ABP100KD },
	{ .name = "abp1_6bd", .driver_data = ABP160KD },
	{ .name = "abp160kd", .driver_data = ABP160KD },
	{ .name = "abp2_5bd", .driver_data = ABP250KD },
	{ .name = "abp250kd", .driver_data = ABP250KD },
	{ .name = "abp004bd", .driver_data = ABP400KD },
	{ .name = "abp400kd", .driver_data = ABP400KD },
	/* psi variants */
	/*    gage: */
	{ .name = "abp001pg", .driver_data = ABP001PG },
	{ .name = "abp005pg", .driver_data = ABP005PG },
	{ .name = "abp015pg", .driver_data = ABP015PG },
	{ .name = "abp030pg", .driver_data = ABP030PG },
	{ .name = "abp060pg", .driver_data = ABP060PG },
	{ .name = "abp100pg", .driver_data = ABP100PG },
	{ .name = "abp150pg", .driver_data = ABP150PG },
	/*    differential: */
	{ .name = "abp001pd", .driver_data = ABP001PD },
	{ .name = "abp005pd", .driver_data = ABP005PD },
	{ .name = "abp015pd", .driver_data = ABP015PD },
	{ .name = "abp030pd", .driver_data = ABP030PD },
	{ .name = "abp060pd", .driver_data = ABP060PD },
	{ }
};
MODULE_DEVICE_TABLE(i2c, abp060mg_id_table);

static struct i2c_driver abp060mg_driver = {
	.driver = {
		.name = "abp060mg",
	},
	.probe = abp060mg_probe,
	.id_table = abp060mg_id_table,
};
module_i2c_driver(abp060mg_driver);

MODULE_AUTHOR("Marcin Malagowski <mrc@bourne.st>");
MODULE_DESCRIPTION("Honeywell ABP pressure sensor driver");
MODULE_LICENSE("GPL");

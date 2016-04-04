/*
 * nintendo3ds_ts.c
 *
 * Copyright (C) 2016 Sergi Granell (xerpi)
 * based on ad7879-spi.c
 *
 * Licensed under the GPL-2 or later.
 */

#include <linux/input.h>
#include <linux/input-polldev.h>
#include <linux/input/touchscreen.h>
#include <linux/spi/spi.h>
#include <linux/module.h>
#include <linux/of.h>

#define NINTENDO3DS_TS_NAME	"nintendo3ds_touchscreen"

#define POLL_INTERVAL_DEFAULT	20

#define MAX_12BIT		((1 << 12) - 1)

struct nintendo3ds_ts {
	struct spi_device *spi;
	struct input_polled_dev *polled_dev;
	struct input_dev *input_dev;
	unsigned int max_y;
	bool invert_y;
	bool swap_xy;
	bool pendown;
};

static int spi_write_2(struct spi_device *spi,
		       u8 *tx_buf0, u8 tx_len0,
		       u8 *tx_buf1, u8 tx_len1)
{
	struct spi_message msg;
	struct spi_transfer xfers[2];

	memset(xfers, 0, sizeof(xfers));

	xfers[0].tx_buf = tx_buf0;
	xfers[0].len = tx_len0;

	xfers[1].tx_buf = tx_buf1;
	xfers[1].len = tx_len1;

	spi_message_init(&msg);
	spi_message_add_tail(&xfers[0], &msg);
	spi_message_add_tail(&xfers[1], &msg);

	return spi_sync(spi, &msg);
}

static int spi_write_read(struct spi_device *spi,
			  u8 *tx_buf, u8 tx_len,
			  u8 *rx_buf, u8 rx_len)
{
	struct spi_message msg;
	struct spi_transfer xfers[2];

	memset(xfers, 0, sizeof(xfers));

	xfers[0].tx_buf = tx_buf;
	xfers[0].len = tx_len;

	xfers[1].rx_buf = rx_buf;
	xfers[1].len = rx_len;

	spi_message_init(&msg);

	spi_message_add_tail(&xfers[0], &msg);
	spi_message_add_tail(&xfers[1], &msg);

	return spi_sync(spi, &msg);
}

static void nintendo3ds_ts_spi_offset_mask(struct nintendo3ds_ts *ts, u8 offset, u8 mask0, u8 mask1)
{
	u8 buffer1[4];
	u8 buffer2[0x40];

	buffer1[0] = 1 | (offset << 1);

	spi_write_read(ts->spi, buffer1, 1, buffer2, 1);

	buffer1[0] = offset << 1;
	buffer2[0] = (buffer2[0] & ~mask1) | (mask0 & mask1);

	spi_write_2(ts->spi, buffer1, 1, buffer2, 1);
}

static void nintendo3ds_ts_spi_select_reg(struct nintendo3ds_ts *ts,
					  u8 reg)
{
	u8 buffer1[4];
	u8 buffer2[0x40];

	buffer1[0] = 0;
	buffer2[0] = reg;

	spi_write_2(ts->spi, buffer1, 1, buffer2, 1);
}

static u8 nintendo3ds_ts_spi_read_offset(struct nintendo3ds_ts *ts,
					      u8 offset)
{
	u8 buffer_wr[8];
	u8 buffer_rd[0x40];

	buffer_wr[0] = 1 | (offset << 1);

	spi_write_read(ts->spi, buffer_wr, 1, buffer_rd, 1);

	return buffer_rd[0];
}

static void nintendo3ds_ts_spi_read_offset_array(struct nintendo3ds_ts *ts,
		u8 offset, void *buffer_rd, u8 size_rd)
{
	u8 buffer_wr[0x10];

	buffer_wr[0] = 1 | (offset << 1);

	spi_write_read(ts->spi, buffer_wr, 1, buffer_rd, size_rd);
}

static void nintendo3ds_ts_open(struct input_polled_dev *dev)
{
}

static void nintendo3ds_ts_close(struct input_polled_dev *dev)
{
}

static void nintendo3ds_ts_poll(struct input_polled_dev *polled_dev)
{
	u8 raw_touchdata[0x40]
		__attribute__((aligned(sizeof(u16))));
	struct nintendo3ds_ts *ts;
	struct input_dev *input_dev;
	bool pendown;
	u16 raw_x;
	u16 raw_y;

	ts = polled_dev->private;
	input_dev = ts->input_dev;

	nintendo3ds_ts_spi_select_reg(ts, 0x67);
	nintendo3ds_ts_spi_read_offset(ts, 0x26);
	nintendo3ds_ts_spi_select_reg(ts, 0xFB);
	nintendo3ds_ts_spi_read_offset_array(ts, 1, raw_touchdata, 0x34);

	pendown = !(raw_touchdata[0] & BIT(4));

	if (pendown) {
		raw_x = le16_to_cpu((raw_touchdata[0]  << 8) | raw_touchdata[1]);
		raw_y = le16_to_cpu((raw_touchdata[10] << 8) | raw_touchdata[11]);

		if (!ts->pendown) {
			input_report_key(input_dev, BTN_TOUCH, 1);
			ts->pendown = true;
		}

		if (ts->invert_y)
			raw_y = ts->max_y - raw_y;

		if (likely(ts->swap_xy)) {
			input_report_abs(input_dev, ABS_X, raw_y);
			input_report_abs(input_dev, ABS_Y, raw_x);
		} else {
			input_report_abs(input_dev, ABS_X, raw_x);
			input_report_abs(input_dev, ABS_Y, raw_y);
		}
		input_sync(input_dev);
	} else if (ts->pendown) {
		ts->pendown = false;
		input_report_key(input_dev, BTN_TOUCH, 0);
		input_sync(input_dev);
	}
}

static void nintendo3ds_ts_initialize(struct nintendo3ds_ts *ts)
{
	nintendo3ds_ts_spi_select_reg(ts, 0x67);
	nintendo3ds_ts_spi_offset_mask(ts, 0x26, 0x80, 0x80);
	nintendo3ds_ts_spi_select_reg(ts, 0x67);
	nintendo3ds_ts_spi_offset_mask(ts, 0x24, 0, 0x80);
	nintendo3ds_ts_spi_select_reg(ts, 0x67);
	nintendo3ds_ts_spi_offset_mask(ts, 0x25, 0x10, 0x3C);
}

static int nintendo3ds_spi_ts_probe(struct spi_device *spi)
{
	struct nintendo3ds_ts *ts;
	struct input_polled_dev *polled_dev;
	struct input_dev *input_dev;
	int err;

	spi->bits_per_word = 8;
	spi->mode = SPI_MODE_0;

	err = spi_setup(spi);
	if (err < 0) {
		dev_err(&spi->dev, "%s: SPI setup error %d\n",
			__func__, err);
		goto err;
	}

	ts = devm_kzalloc(&spi->dev, sizeof(struct nintendo3ds_ts), GFP_KERNEL);
	if (!ts) {
		err = -ENOMEM;
		goto err;
	}

	polled_dev = devm_input_allocate_polled_device(&spi->dev);
	if (!polled_dev) {
		dev_err(&spi->dev, "%s: Can't allocate input device, error %d\n",
			__func__, err);
		err = -ENOMEM;
		goto err_alloc_polled_dev;
	}

	polled_dev->private = ts;
	polled_dev->poll = nintendo3ds_ts_poll;
	polled_dev->open = nintendo3ds_ts_open;
	polled_dev->close = nintendo3ds_ts_close;
	polled_dev->poll_interval = POLL_INTERVAL_DEFAULT;

	input_dev = polled_dev->input;
	input_dev->name = "Nintendo 3DS touchscreen";
	input_dev->phys = NINTENDO3DS_TS_NAME "/input0";
	input_dev->id.bustype = BUS_SPI;

	__set_bit(EV_ABS, input_dev->evbit);
	__set_bit(ABS_X, input_dev->absbit);
	__set_bit(ABS_Y, input_dev->absbit);

	__set_bit(EV_KEY, input_dev->evbit);
	__set_bit(BTN_TOUCH, input_dev->keybit);

	input_set_abs_params(input_dev, ABS_X, 0, MAX_12BIT, 0, 0);
	input_set_abs_params(input_dev, ABS_Y, 0, MAX_12BIT, 0, 0);
	touchscreen_parse_properties(input_dev, false);

	ts->invert_y = device_property_read_bool(input_dev->dev.parent,
		"touchscreen-inverted-y");

	ts->swap_xy = device_property_read_bool(input_dev->dev.parent,
		"touchscreen-swapped-x-y");

	ts->max_y = input_abs_get_max(input_dev, ABS_Y) + 1;

	ts->spi = spi;
	ts->polled_dev = polled_dev;
	ts->input_dev = polled_dev->input;
	ts->pendown = false;
	spi_set_drvdata(spi, ts);

	nintendo3ds_ts_initialize(ts);

	err = input_register_polled_device(polled_dev);
	if (err) {
		pr_err("nintendo3ds_ts.c: Failed to register input device\n");
		goto err_free_dev;
	}

	return 0;

err_free_dev:
	input_unregister_polled_device(polled_dev);
	input_free_polled_device(polled_dev);
err_alloc_polled_dev:
	devm_kfree(&spi->dev, ts);
err:
	return err;
}

#ifdef CONFIG_OF
static const struct of_device_id nintendo3ds_spi_ts_dt_ids[] = {
	{ .compatible = "nintendo3ds,spi-touchscreen", },
	{ }
};
MODULE_DEVICE_TABLE(of, nintendo3ds_spi_ts_dt_ids);
#endif

static struct spi_driver nintendo3ds_spi_ts_driver = {
	.driver = {
		.name	= NINTENDO3DS_TS_NAME,
		.of_match_table = of_match_ptr(nintendo3ds_spi_ts_dt_ids),
	},
	.probe		= nintendo3ds_spi_ts_probe
};

module_spi_driver(nintendo3ds_spi_ts_driver);

MODULE_AUTHOR("Sergi Granell <xerpi.g.12@gmail.com>");
MODULE_DESCRIPTION("Nintendo 3DS SPI touchscreen driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("spi:" NINTENDO3DS_TS_NAME);

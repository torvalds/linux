/*
 * nintendo3ds_codec_hid.c
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
#include <linux/delay.h>
#include <linux/of.h>

#define NINTENDO3DS_CODEC_HID_NAME	"nintendo3ds_codec_hid"
#define POLL_INTERVAL_DEFAULT		20
#define MAX_12BIT			((1 << 12) - 1)
#define CIRCLE_PAD_THRESHOLD		150
#define CIRCLE_PAD_FACTOR		150

struct nintendo3ds_codec_hid {
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

static void spi_reg_select(struct spi_device *spi, u8 reg)
{
	u8 buffer1[4];
	u8 buffer2[0x40];

	buffer1[0] = 0;
	buffer2[0] = reg;

	spi_write_2(spi, buffer1, 1, buffer2, 1);
}

static u8 spi_reg_read_offset(struct spi_device *spi, u8 offset)
{
	u8 buffer_wr[8];
	u8 buffer_rd[0x40];

	buffer_wr[0] = 1 | (offset << 1);

	spi_write_read(spi, buffer_wr, 1, buffer_rd, 1);

	return buffer_rd[0];
}

static void spi_reg_write_offset(struct spi_device *spi, u8 reg, u8 val)
{
	u8 buffer1[8];
	u8 buffer2[0x40];

	buffer1[0] = (reg << 1); // Write
	buffer2[0] = val;

	spi_write_2(spi, buffer1, 1, buffer2, 1);
}

static void spi_reg_read_buffer(struct spi_device *spi,
			       u8 offset, void *buffer, u8 size)
{
	u8 buffer_wr[0x10];

	buffer_wr[0] = 1 | (offset << 1);

	spi_write_read(spi, buffer_wr, 1, buffer, size);
}

static void spi_reg_mask_offset(struct spi_device *spi, u8 offset, u8 mask0, u8 mask1)
{
	u8 buffer1[4];
	u8 buffer2[0x40];

	buffer1[0] = 1 | (offset << 1);

	spi_write_read(spi, buffer1, 1, buffer2, 1);

	buffer1[0] = offset << 1;
	buffer2[0] = (buffer2[0] & ~mask1) | (mask0 & mask1);

	spi_write_2(spi, buffer1, 1, buffer2, 1);
}

static void spi_codec_hid_initialize(struct spi_device *spi)
{
	spi_reg_select(spi, 0x67);
	spi_reg_write_offset(spi, 0x24, 0x98);
	spi_reg_select(spi, 0x67);
	spi_reg_write_offset(spi, 0x26, 0x00);
	spi_reg_select(spi, 0x67);
	spi_reg_write_offset(spi, 0x25, 0x43);
	spi_reg_select(spi, 0x67);
	spi_reg_write_offset(spi, 0x24, 0x18);
	spi_reg_select(spi, 0x67);
	spi_reg_write_offset(spi, 0x17, 0x43);
	spi_reg_select(spi, 0x67);
	spi_reg_write_offset(spi, 0x19, 0x69);
	spi_reg_select(spi, 0x67);
	spi_reg_write_offset(spi, 0x1B, 0x80);
	spi_reg_select(spi, 0x67);
	spi_reg_write_offset(spi, 0x27, 0x11);
	spi_reg_select(spi, 0x67);
	spi_reg_write_offset(spi, 0x26, 0xEC);
	spi_reg_select(spi, 0x67);
	spi_reg_write_offset(spi, 0x24, 0x18);
	spi_reg_select(spi, 0x67);
	spi_reg_write_offset(spi, 0x25, 0x53);

	spi_reg_select(spi, 0x67);
	spi_reg_mask_offset(spi, 0x26, 0x80, 0x80);
	spi_reg_select(spi, 0x67);
	spi_reg_mask_offset(spi, 0x24, 0x00, 0x80);
	spi_reg_select(spi, 0x67);
	spi_reg_mask_offset(spi, 0x25, 0x10, 0x3C);
}

static void spi_codec_hid_request_data(struct spi_device *spi, u8 *buffer)
{
	spi_reg_select(spi, 0x67);
	spi_reg_read_offset(spi, 0x26);
	spi_reg_select(spi, 0xFB);
	spi_reg_read_buffer(spi, 1, buffer, 0x34);
}

static void nintendo3ds_codec_hid_open(struct input_polled_dev *dev)
{
}

static void nintendo3ds_codec_hid_close(struct input_polled_dev *dev)
{
}

static void nintendo3ds_codec_hid_poll(struct input_polled_dev *polled_dev)
{
	struct nintendo3ds_codec_hid *codec_hid = polled_dev->private;
	struct input_dev *input_dev = codec_hid->input_dev;
	u8 raw_data[0x40] __attribute__((aligned(sizeof(u16))));
	bool pendown;
	u16 raw_touch_x;
	u16 raw_touch_y;
	s16 raw_circlepad_x;
	s16 raw_circlepad_y;
	bool sync = false;

	spi_codec_hid_request_data(codec_hid->spi, raw_data);

	raw_circlepad_x =
		(s16)le16_to_cpu(((raw_data[0x24] << 8) | raw_data[0x25]) & 0xFFF) - 2048;
	raw_circlepad_y =
		(s16)le16_to_cpu(((raw_data[0x14] << 8) | raw_data[0x15]) & 0xFFF) - 2048;

	if (abs(raw_circlepad_x) > CIRCLE_PAD_THRESHOLD) {
		input_report_rel(input_dev, REL_X,
				 -raw_circlepad_x / CIRCLE_PAD_FACTOR);
		sync = true;
	}

	if (abs(raw_circlepad_y) > CIRCLE_PAD_THRESHOLD) {
		input_report_rel(input_dev, REL_Y,
				 -raw_circlepad_y / CIRCLE_PAD_FACTOR);
		sync = true;
	}

	pendown = !(raw_data[0] & BIT(4));

	if (pendown) {
		raw_touch_x = le16_to_cpu((raw_data[0]  << 8) | raw_data[1]);
		raw_touch_y = le16_to_cpu((raw_data[10] << 8) | raw_data[11]);

		if (!codec_hid->pendown) {
			input_report_key(input_dev, BTN_TOUCH, 1);
			codec_hid->pendown = true;
		}

		if (codec_hid->invert_y)
			raw_touch_y = codec_hid->max_y - raw_touch_y;

		if (likely(codec_hid->swap_xy)) {
			input_report_abs(input_dev, ABS_X, raw_touch_y);
			input_report_abs(input_dev, ABS_Y, raw_touch_x);
		} else {
			input_report_abs(input_dev, ABS_X, raw_touch_x);
			input_report_abs(input_dev, ABS_Y, raw_touch_y);
		}
		sync = true;
	} else if (codec_hid->pendown) {
		codec_hid->pendown = false;
		input_report_key(input_dev, BTN_TOUCH, 0);
		sync = true;
	}

	if (sync)
		input_sync(input_dev);
}

static int nintendo3ds_codec_hid_probe(struct spi_device *spi)
{
	struct nintendo3ds_codec_hid *codec_hid;
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

	codec_hid = devm_kzalloc(&spi->dev, sizeof(struct nintendo3ds_codec_hid), GFP_KERNEL);
	if (!codec_hid) {
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

	polled_dev->private = codec_hid;
	polled_dev->poll = nintendo3ds_codec_hid_poll;
	polled_dev->open = nintendo3ds_codec_hid_open;
	polled_dev->close = nintendo3ds_codec_hid_close;
	polled_dev->poll_interval = POLL_INTERVAL_DEFAULT;

	input_dev = polled_dev->input;
	input_dev->name = "Nintendo 3DS CODEC HID";
	input_dev->phys = NINTENDO3DS_CODEC_HID_NAME "/input0";
	input_dev->id.bustype = BUS_SPI;
	input_dev->dev.parent = &spi->dev;

	/*
	__set_bit(EV_ABS, input_dev->evbit);
	__set_bit(ABS_X, input_dev->absbit);
	__set_bit(ABS_Y, input_dev->absbit);
	*/

	__set_bit(EV_REL, input_dev->evbit);
	__set_bit(REL_X, input_dev->relbit);
	__set_bit(REL_Y, input_dev->relbit);
	__set_bit(REL_WHEEL, input_dev->relbit);

	/*
	__set_bit(EV_KEY, input_dev->evbit);
	__set_bit(BTN_TOUCH, input_dev->keybit);
	*/

	input_set_abs_params(input_dev, ABS_X, 0, MAX_12BIT, 0, 0);
	input_set_abs_params(input_dev, ABS_Y, 0, MAX_12BIT, 0, 0);
	touchscreen_parse_properties(input_dev, false);

	codec_hid->invert_y = device_property_read_bool(input_dev->dev.parent,
		"touchscreen-inverted-y");

	codec_hid->swap_xy = device_property_read_bool(input_dev->dev.parent,
		"touchscreen-swapped-x-y");

	codec_hid->max_y = input_abs_get_max(input_dev, ABS_Y) + 1;

	codec_hid->spi = spi;
	codec_hid->polled_dev = polled_dev;
	codec_hid->input_dev = polled_dev->input;
	codec_hid->pendown = false;
	spi_set_drvdata(spi, codec_hid);

	spi_codec_hid_initialize(spi);

	err = input_register_polled_device(polled_dev);
	if (err) {
		pr_err("nintendo3ds_codec_hid.c: Failed to register input device\n");
		goto err_free_dev;
	}

	return 0;

err_free_dev:
	input_unregister_polled_device(polled_dev);
	input_free_polled_device(polled_dev);
err_alloc_polled_dev:
	devm_kfree(&spi->dev, codec_hid);
err:
	return err;
}

#ifdef CONFIG_OF
static const struct of_device_id nintendo3ds_codec_hid_dt_ids[] = {
	{ .compatible = "nintendo3ds,codec-hid", },
	{ }
};
MODULE_DEVICE_TABLE(of, nintendo3ds_codec_hid_dt_ids);
#endif

static struct spi_driver nintendo3ds_codec_hid_driver = {
	.driver = {
		.name	= NINTENDO3DS_CODEC_HID_NAME,
		.of_match_table = of_match_ptr(nintendo3ds_codec_hid_dt_ids),
	},
	.probe		= nintendo3ds_codec_hid_probe
};

module_spi_driver(nintendo3ds_codec_hid_driver);

MODULE_AUTHOR("Sergi Granell <xerpi.g.12@gmail.com>");
MODULE_DESCRIPTION("Nintendo 3DS CODEC HID driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("spi:" NINTENDO3DS_CODEC_HID_NAME);

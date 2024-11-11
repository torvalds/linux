// SPDX-License-Identifier: GPL-2.0-only
/*
 *  Driver for Ntrig/Microsoft Touchscreens over SPI
 *
 *  Copyright (c) 2016 Red Hat Inc.
 */


#include <linux/kernel.h>

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/input.h>
#include <linux/input/mt.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/spi/spi.h>
#include <linux/acpi.h>

#include <linux/unaligned.h>

#define SURFACE3_PACKET_SIZE	264

#define SURFACE3_REPORT_TOUCH	0xd2
#define SURFACE3_REPORT_PEN	0x16

struct surface3_ts_data {
	struct spi_device *spi;
	struct gpio_desc *gpiod_rst[2];
	struct input_dev *input_dev;
	struct input_dev *pen_input_dev;
	int pen_tool;

	u8 rd_buf[SURFACE3_PACKET_SIZE]		____cacheline_aligned;
};

struct surface3_ts_data_finger {
	u8 status;
	__le16 tracking_id;
	__le16 x;
	__le16 cx;
	__le16 y;
	__le16 cy;
	__le16 width;
	__le16 height;
	u32 padding;
} __packed;

struct surface3_ts_data_pen {
	u8 status;
	__le16 x;
	__le16 y;
	__le16 pressure;
	u8 padding;
} __packed;

static int surface3_spi_read(struct surface3_ts_data *ts_data)
{
	struct spi_device *spi = ts_data->spi;

	memset(ts_data->rd_buf, 0, sizeof(ts_data->rd_buf));
	return spi_read(spi, ts_data->rd_buf, sizeof(ts_data->rd_buf));
}

static void surface3_spi_report_touch(struct surface3_ts_data *ts_data,
				   struct surface3_ts_data_finger *finger)
{
	int st = finger->status & 0x01;
	int slot;

	slot = input_mt_get_slot_by_key(ts_data->input_dev,
				get_unaligned_le16(&finger->tracking_id));
	if (slot < 0)
		return;

	input_mt_slot(ts_data->input_dev, slot);
	input_mt_report_slot_state(ts_data->input_dev, MT_TOOL_FINGER, st);
	if (st) {
		input_report_abs(ts_data->input_dev,
				 ABS_MT_POSITION_X,
				 get_unaligned_le16(&finger->x));
		input_report_abs(ts_data->input_dev,
				 ABS_MT_POSITION_Y,
				 get_unaligned_le16(&finger->y));
		input_report_abs(ts_data->input_dev,
				 ABS_MT_WIDTH_MAJOR,
				 get_unaligned_le16(&finger->width));
		input_report_abs(ts_data->input_dev,
				 ABS_MT_WIDTH_MINOR,
				 get_unaligned_le16(&finger->height));
	}
}

static void surface3_spi_process_touch(struct surface3_ts_data *ts_data, u8 *data)
{
	unsigned int i;

	for (i = 0; i < 13; i++) {
		struct surface3_ts_data_finger *finger;

		finger = (struct surface3_ts_data_finger *)&data[17 +
				i * sizeof(struct surface3_ts_data_finger)];

		/*
		 * When bit 5 of status is 1, it marks the end of the report:
		 * - touch present: 0xe7
		 * - touch released: 0xe4
		 * - nothing valuable: 0xff
		 */
		if (finger->status & 0x10)
			break;

		surface3_spi_report_touch(ts_data, finger);
	}

	input_mt_sync_frame(ts_data->input_dev);
	input_sync(ts_data->input_dev);
}

static void surface3_spi_report_pen(struct surface3_ts_data *ts_data,
				    struct surface3_ts_data_pen *pen)
{
	struct input_dev *dev = ts_data->pen_input_dev;
	int st = pen->status;
	int prox = st & 0x01;
	int rubber = st & 0x18;
	int tool = (prox && rubber) ? BTN_TOOL_RUBBER : BTN_TOOL_PEN;

	/* fake proximity out to switch tools */
	if (ts_data->pen_tool != tool) {
		input_report_key(dev, ts_data->pen_tool, 0);
		input_sync(dev);
		ts_data->pen_tool = tool;
	}

	input_report_key(dev, BTN_TOUCH, st & 0x12);

	input_report_key(dev, ts_data->pen_tool, prox);

	if (st) {
		input_report_key(dev,
				 BTN_STYLUS,
				 st & 0x04);

		input_report_abs(dev,
				 ABS_X,
				 get_unaligned_le16(&pen->x));
		input_report_abs(dev,
				 ABS_Y,
				 get_unaligned_le16(&pen->y));
		input_report_abs(dev,
				 ABS_PRESSURE,
				 get_unaligned_le16(&pen->pressure));
	}
}

static void surface3_spi_process_pen(struct surface3_ts_data *ts_data, u8 *data)
{
	struct surface3_ts_data_pen *pen;

	pen = (struct surface3_ts_data_pen *)&data[15];

	surface3_spi_report_pen(ts_data, pen);
	input_sync(ts_data->pen_input_dev);
}

static void surface3_spi_process(struct surface3_ts_data *ts_data)
{
	static const char header[] = {
		0xff, 0xff, 0xff, 0xff, 0xa5, 0x5a, 0xe7, 0x7e, 0x01
	};
	u8 *data = ts_data->rd_buf;

	if (memcmp(header, data, sizeof(header)))
		dev_err(&ts_data->spi->dev,
			"%s header error: %*ph, ignoring...\n",
			__func__, (int)sizeof(header), data);

	switch (data[9]) {
	case SURFACE3_REPORT_TOUCH:
		surface3_spi_process_touch(ts_data, data);
		break;
	case SURFACE3_REPORT_PEN:
		surface3_spi_process_pen(ts_data, data);
		break;
	default:
		dev_err(&ts_data->spi->dev,
			"%s unknown packet type: %x, ignoring...\n",
			__func__, data[9]);
		break;
	}
}

static irqreturn_t surface3_spi_irq_handler(int irq, void *dev_id)
{
	struct surface3_ts_data *data = dev_id;

	if (surface3_spi_read(data))
		return IRQ_HANDLED;

	dev_dbg(&data->spi->dev, "%s received -> %*ph\n",
		__func__, SURFACE3_PACKET_SIZE, data->rd_buf);
	surface3_spi_process(data);

	return IRQ_HANDLED;
}

static void surface3_spi_power(struct surface3_ts_data *data, bool on)
{
	gpiod_set_value(data->gpiod_rst[0], on);
	gpiod_set_value(data->gpiod_rst[1], on);
	/* let the device settle a little */
	msleep(20);
}

/**
 * surface3_spi_get_gpio_config - Get GPIO config from ACPI/DT
 *
 * @data: surface3_spi_ts_data pointer
 */
static int surface3_spi_get_gpio_config(struct surface3_ts_data *data)
{
	struct device *dev;
	struct gpio_desc *gpiod;
	int i;

	dev = &data->spi->dev;

	/* Get the reset lines GPIO pin number */
	for (i = 0; i < 2; i++) {
		gpiod = devm_gpiod_get_index(dev, NULL, i, GPIOD_OUT_LOW);
		if (IS_ERR(gpiod))
			return dev_err_probe(dev, PTR_ERR(gpiod),
					     "Failed to get power GPIO %d\n", i);

		data->gpiod_rst[i] = gpiod;
	}

	return 0;
}

static int surface3_spi_create_touch_input(struct surface3_ts_data *data)
{
	struct input_dev *input;
	int error;

	input = devm_input_allocate_device(&data->spi->dev);
	if (!input)
		return -ENOMEM;

	data->input_dev = input;

	input_set_abs_params(input, ABS_MT_POSITION_X, 0, 9600, 0, 0);
	input_abs_set_res(input, ABS_MT_POSITION_X, 40);
	input_set_abs_params(input, ABS_MT_POSITION_Y, 0, 7200, 0, 0);
	input_abs_set_res(input, ABS_MT_POSITION_Y, 48);
	input_set_abs_params(input, ABS_MT_WIDTH_MAJOR, 0, 1024, 0, 0);
	input_set_abs_params(input, ABS_MT_WIDTH_MINOR, 0, 1024, 0, 0);
	input_mt_init_slots(input, 10, INPUT_MT_DIRECT);

	input->name = "Surface3 SPI Capacitive TouchScreen";
	input->phys = "input/ts";
	input->id.bustype = BUS_SPI;
	input->id.vendor = 0x045e;	/* Microsoft */
	input->id.product = 0x0001;
	input->id.version = 0x0000;

	error = input_register_device(input);
	if (error) {
		dev_err(&data->spi->dev,
			"Failed to register input device: %d", error);
		return error;
	}

	return 0;
}

static int surface3_spi_create_pen_input(struct surface3_ts_data *data)
{
	struct input_dev *input;
	int error;

	input = devm_input_allocate_device(&data->spi->dev);
	if (!input)
		return -ENOMEM;

	data->pen_input_dev = input;
	data->pen_tool = BTN_TOOL_PEN;

	__set_bit(INPUT_PROP_DIRECT, input->propbit);
	__set_bit(INPUT_PROP_POINTER, input->propbit);
	input_set_abs_params(input, ABS_X, 0, 9600, 0, 0);
	input_abs_set_res(input, ABS_X, 40);
	input_set_abs_params(input, ABS_Y, 0, 7200, 0, 0);
	input_abs_set_res(input, ABS_Y, 48);
	input_set_abs_params(input, ABS_PRESSURE, 0, 1024, 0, 0);
	input_set_capability(input, EV_KEY, BTN_TOUCH);
	input_set_capability(input, EV_KEY, BTN_STYLUS);
	input_set_capability(input, EV_KEY, BTN_TOOL_PEN);
	input_set_capability(input, EV_KEY, BTN_TOOL_RUBBER);

	input->name = "Surface3 SPI Pen Input";
	input->phys = "input/ts";
	input->id.bustype = BUS_SPI;
	input->id.vendor = 0x045e;     /* Microsoft */
	input->id.product = 0x0002;
	input->id.version = 0x0000;

	error = input_register_device(input);
	if (error) {
		dev_err(&data->spi->dev,
			"Failed to register input device: %d", error);
		return error;
	}

	return 0;
}

static int surface3_spi_probe(struct spi_device *spi)
{
	struct surface3_ts_data *data;
	int error;

	/* Set up SPI*/
	spi->bits_per_word = 8;
	spi->mode = SPI_MODE_0;
	error = spi_setup(spi);
	if (error)
		return error;

	data = devm_kzalloc(&spi->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->spi = spi;
	spi_set_drvdata(spi, data);

	error = surface3_spi_get_gpio_config(data);
	if (error)
		return error;

	surface3_spi_power(data, true);
	surface3_spi_power(data, false);
	surface3_spi_power(data, true);

	error = surface3_spi_create_touch_input(data);
	if (error)
		return error;

	error = surface3_spi_create_pen_input(data);
	if (error)
		return error;

	error = devm_request_threaded_irq(&spi->dev, spi->irq,
					  NULL, surface3_spi_irq_handler,
					  IRQF_ONESHOT,
					  "Surface3-irq", data);
	if (error)
		return error;

	return 0;
}

static int surface3_spi_suspend(struct device *dev)
{
	struct spi_device *spi = to_spi_device(dev);
	struct surface3_ts_data *data = spi_get_drvdata(spi);

	disable_irq(data->spi->irq);

	surface3_spi_power(data, false);

	return 0;
}

static int surface3_spi_resume(struct device *dev)
{
	struct spi_device *spi = to_spi_device(dev);
	struct surface3_ts_data *data = spi_get_drvdata(spi);

	surface3_spi_power(data, true);

	enable_irq(data->spi->irq);

	return 0;
}

static DEFINE_SIMPLE_DEV_PM_OPS(surface3_spi_pm_ops,
				surface3_spi_suspend,
				surface3_spi_resume);

#ifdef CONFIG_ACPI
static const struct acpi_device_id surface3_spi_acpi_match[] = {
	{ "MSHW0037", 0 },
	{ }
};
MODULE_DEVICE_TABLE(acpi, surface3_spi_acpi_match);
#endif

static struct spi_driver surface3_spi_driver = {
	.driver = {
		.name	= "Surface3-spi",
		.acpi_match_table = ACPI_PTR(surface3_spi_acpi_match),
		.pm = pm_sleep_ptr(&surface3_spi_pm_ops),
	},
	.probe = surface3_spi_probe,
};

module_spi_driver(surface3_spi_driver);

MODULE_AUTHOR("Benjamin Tissoires <benjamin.tissoires@gmail.com>");
MODULE_DESCRIPTION("Surface 3 SPI touchscreen driver");
MODULE_LICENSE("GPL v2");

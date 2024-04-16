// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for Cypress CY8CTMA140 (TMA140) touchscreen
 * (C) 2020 Linus Walleij <linus.walleij@linaro.org>
 * (C) 2007 Cypress
 * (C) 2007 Google, Inc.
 *
 * Inspired by the tma140_skomer.c driver in the Samsung GT-S7710 code
 * drop. The GT-S7710 is codenamed "Skomer", the code also indicates
 * that the same touchscreen was used in a product called "Lucas".
 *
 * The code drop for GT-S7710 also contains a firmware downloader and
 * 15 (!) versions of the firmware drop from Cypress. But here we assume
 * the firmware got downloaded to the touchscreen flash successfully and
 * just use it to read the fingers. The shipped vendor driver does the
 * same.
 */

#include <asm/unaligned.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/input.h>
#include <linux/input/touchscreen.h>
#include <linux/input/mt.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/i2c.h>
#include <linux/regulator/consumer.h>
#include <linux/delay.h>

#define CY8CTMA140_NAME			"cy8ctma140"

#define CY8CTMA140_MAX_FINGERS		4

#define CY8CTMA140_GET_FINGERS		0x00
#define CY8CTMA140_GET_FW_INFO		0x19

/* This message also fits some bytes for touchkeys, if used */
#define CY8CTMA140_PACKET_SIZE		31

#define CY8CTMA140_INVALID_BUFFER_BIT	5

struct cy8ctma140 {
	struct input_dev *input;
	struct touchscreen_properties props;
	struct device *dev;
	struct i2c_client *client;
	struct regulator_bulk_data regulators[2];
	u8 prev_fingers;
	u8 prev_f1id;
	u8 prev_f2id;
};

static void cy8ctma140_report(struct cy8ctma140 *ts, u8 *data, int n_fingers)
{
	static const u8 contact_offsets[] = { 0x03, 0x09, 0x10, 0x16 };
	u8 *buf;
	u16 x, y;
	u8 w;
	u8 id;
	int slot;
	int i;

	for (i = 0; i < n_fingers; i++) {
		buf = &data[contact_offsets[i]];

		/*
		 * Odd contacts have contact ID in the lower nibble of
		 * the preceding byte, whereas even contacts have it in
		 * the upper nibble of the following byte.
		 */
		id = i % 2 ? buf[-1] & 0x0f : buf[5] >> 4;
		slot = input_mt_get_slot_by_key(ts->input, id);
		if (slot < 0)
			continue;

		x = get_unaligned_be16(buf);
		y = get_unaligned_be16(buf + 2);
		w = buf[4];

		dev_dbg(ts->dev, "finger %d: ID %02x (%d, %d) w: %d\n",
			slot, id, x, y, w);

		input_mt_slot(ts->input, slot);
		input_mt_report_slot_state(ts->input, MT_TOOL_FINGER, true);
		touchscreen_report_pos(ts->input, &ts->props, x, y, true);
		input_report_abs(ts->input, ABS_MT_TOUCH_MAJOR, w);
	}

	input_mt_sync_frame(ts->input);
	input_sync(ts->input);
}

static irqreturn_t cy8ctma140_irq_thread(int irq, void *d)
{
	struct cy8ctma140 *ts = d;
	u8 cmdbuf[] = { CY8CTMA140_GET_FINGERS };
	u8 buf[CY8CTMA140_PACKET_SIZE];
	struct i2c_msg msg[] = {
		{
			.addr = ts->client->addr,
			.flags = 0,
			.len = sizeof(cmdbuf),
			.buf = cmdbuf,
		}, {
			.addr = ts->client->addr,
			.flags = I2C_M_RD,
			.len = sizeof(buf),
			.buf = buf,
		},
	};
	u8 n_fingers;
	int ret;

	ret = i2c_transfer(ts->client->adapter, msg, ARRAY_SIZE(msg));
	if (ret != ARRAY_SIZE(msg)) {
		if (ret < 0)
			dev_err(ts->dev, "error reading message: %d\n", ret);
		else
			dev_err(ts->dev, "wrong number of messages\n");
		goto out;
	}

	if (buf[1] & BIT(CY8CTMA140_INVALID_BUFFER_BIT)) {
		dev_dbg(ts->dev, "invalid event\n");
		goto out;
	}

	n_fingers = buf[2] & 0x0f;
	if (n_fingers > CY8CTMA140_MAX_FINGERS) {
		dev_err(ts->dev, "unexpected number of fingers: %d\n",
			n_fingers);
		goto out;
	}

	cy8ctma140_report(ts, buf, n_fingers);

out:
	return IRQ_HANDLED;
}

static int cy8ctma140_init(struct cy8ctma140 *ts)
{
	u8 addr[1];
	u8 buf[5];
	int ret;

	addr[0] = CY8CTMA140_GET_FW_INFO;
	ret = i2c_master_send(ts->client, addr, 1);
	if (ret < 0) {
		dev_err(ts->dev, "error sending FW info message\n");
		return ret;
	}
	ret = i2c_master_recv(ts->client, buf, 5);
	if (ret < 0) {
		dev_err(ts->dev, "error receiving FW info message\n");
		return ret;
	}
	if (ret != 5) {
		dev_err(ts->dev, "got only %d bytes\n", ret);
		return -EIO;
	}

	dev_dbg(ts->dev, "vendor %c%c, HW ID %.2d, FW ver %.4d\n",
		buf[0], buf[1], buf[3], buf[4]);

	return 0;
}

static int cy8ctma140_power_up(struct cy8ctma140 *ts)
{
	int error;

	error = regulator_bulk_enable(ARRAY_SIZE(ts->regulators),
				      ts->regulators);
	if (error) {
		dev_err(ts->dev, "failed to enable regulators\n");
		return error;
	}

	msleep(250);

	return 0;
}

static void cy8ctma140_power_down(struct cy8ctma140 *ts)
{
	regulator_bulk_disable(ARRAY_SIZE(ts->regulators),
			       ts->regulators);
}

/* Called from the registered devm action */
static void cy8ctma140_power_off_action(void *d)
{
	struct cy8ctma140 *ts = d;

	cy8ctma140_power_down(ts);
}

static int cy8ctma140_probe(struct i2c_client *client,
			    const struct i2c_device_id *id)
{
	struct cy8ctma140 *ts;
	struct input_dev *input;
	struct device *dev = &client->dev;
	int error;

	ts = devm_kzalloc(dev, sizeof(*ts), GFP_KERNEL);
	if (!ts)
		return -ENOMEM;

	input = devm_input_allocate_device(dev);
	if (!input)
		return -ENOMEM;

	ts->dev = dev;
	ts->client = client;
	ts->input = input;

	input_set_capability(input, EV_ABS, ABS_MT_POSITION_X);
	input_set_capability(input, EV_ABS, ABS_MT_POSITION_Y);
	/* One byte for width 0..255 so this is the limit */
	input_set_abs_params(input, ABS_MT_TOUCH_MAJOR, 0, 255, 0, 0);
	/*
	 * This sets up event max/min capabilities and fuzz.
	 * Some DT properties are compulsory so we do not need
	 * to provide defaults for X/Y max or pressure max.
	 *
	 * We just initialize a very simple MT touchscreen here,
	 * some devices use the capability of this touchscreen to
	 * provide touchkeys, and in that case this needs to be
	 * extended to handle touchkey input.
	 *
	 * The firmware takes care of finger tracking and dropping
	 * invalid ranges.
	 */
	touchscreen_parse_properties(input, true, &ts->props);
	input_abs_set_fuzz(input, ABS_MT_POSITION_X, 0);
	input_abs_set_fuzz(input, ABS_MT_POSITION_Y, 0);

	error = input_mt_init_slots(input, CY8CTMA140_MAX_FINGERS,
				  INPUT_MT_DIRECT | INPUT_MT_DROP_UNUSED);
	if (error)
		return error;

	input->name = CY8CTMA140_NAME;
	input->id.bustype = BUS_I2C;
	input_set_drvdata(input, ts);

	/*
	 * VCPIN is the analog voltage supply
	 * VDD is the digital voltage supply
	 * since the voltage range of VDD overlaps that of VCPIN,
	 * many designs to just supply both with a single voltage
	 * source of ~3.3 V.
	 */
	ts->regulators[0].supply = "vcpin";
	ts->regulators[1].supply = "vdd";
	error = devm_regulator_bulk_get(dev, ARRAY_SIZE(ts->regulators),
				      ts->regulators);
	if (error) {
		if (error != -EPROBE_DEFER)
			dev_err(dev, "Failed to get regulators %d\n",
				error);
		return error;
	}

	error = cy8ctma140_power_up(ts);
	if (error)
		return error;

	error = devm_add_action_or_reset(dev, cy8ctma140_power_off_action, ts);
	if (error) {
		dev_err(dev, "failed to install power off handler\n");
		return error;
	}

	error = devm_request_threaded_irq(dev, client->irq,
					  NULL, cy8ctma140_irq_thread,
					  IRQF_ONESHOT, CY8CTMA140_NAME, ts);
	if (error) {
		dev_err(dev, "irq %d busy? error %d\n", client->irq, error);
		return error;
	}

	error = cy8ctma140_init(ts);
	if (error)
		return error;

	error = input_register_device(input);
	if (error)
		return error;

	i2c_set_clientdata(client, ts);

	return 0;
}

static int __maybe_unused cy8ctma140_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct cy8ctma140 *ts = i2c_get_clientdata(client);

	if (!device_may_wakeup(&client->dev))
		cy8ctma140_power_down(ts);

	return 0;
}

static int __maybe_unused cy8ctma140_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct cy8ctma140 *ts = i2c_get_clientdata(client);
	int error;

	if (!device_may_wakeup(&client->dev)) {
		error = cy8ctma140_power_up(ts);
		if (error)
			return error;
	}

	return 0;
}

static SIMPLE_DEV_PM_OPS(cy8ctma140_pm, cy8ctma140_suspend, cy8ctma140_resume);

static const struct i2c_device_id cy8ctma140_idtable[] = {
	{ CY8CTMA140_NAME, 0 },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(i2c, cy8ctma140_idtable);

static const struct of_device_id cy8ctma140_of_match[] = {
	{ .compatible = "cypress,cy8ctma140", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, cy8ctma140_of_match);

static struct i2c_driver cy8ctma140_driver = {
	.driver		= {
		.name	= CY8CTMA140_NAME,
		.pm	= &cy8ctma140_pm,
		.of_match_table = cy8ctma140_of_match,
	},
	.id_table	= cy8ctma140_idtable,
	.probe		= cy8ctma140_probe,
};
module_i2c_driver(cy8ctma140_driver);

MODULE_AUTHOR("Linus Walleij <linus.walleij@linaro.org>");
MODULE_DESCRIPTION("CY8CTMA140 TouchScreen Driver");
MODULE_LICENSE("GPL v2");

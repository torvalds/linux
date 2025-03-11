// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) IBM Corporation 2020
 */

#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/kernel.h>
#include <linux/limits.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/spinlock.h>

#define DEVICE_NAME		"ibm-panel"
#define PANEL_KEYCODES_COUNT	3

struct ibm_panel {
	u8 idx;
	u8 command[11];
	u32 keycodes[PANEL_KEYCODES_COUNT];
	spinlock_t lock;	/* protects writes to idx and command */
	struct input_dev *input;
};

static u8 ibm_panel_calculate_checksum(struct ibm_panel *panel)
{
	u8 chksum;
	u16 sum = 0;
	unsigned int i;

	for (i = 0; i < sizeof(panel->command) - 1; ++i) {
		sum += panel->command[i];
		if (sum & 0xff00) {
			sum &= 0xff;
			sum++;
		}
	}

	chksum = sum & 0xff;
	chksum = ~chksum;
	chksum++;

	return chksum;
}

static void ibm_panel_process_command(struct ibm_panel *panel)
{
	u8 button;
	u8 chksum;

	if (panel->command[0] != 0xff && panel->command[1] != 0xf0) {
		dev_dbg(&panel->input->dev, "command invalid: %02x %02x\n",
			panel->command[0], panel->command[1]);
		return;
	}

	chksum = ibm_panel_calculate_checksum(panel);
	if (chksum != panel->command[sizeof(panel->command) - 1]) {
		dev_dbg(&panel->input->dev,
			"command failed checksum: %u != %u\n", chksum,
			panel->command[sizeof(panel->command) - 1]);
		return;
	}

	button = panel->command[2] & 0xf;
	if (button < PANEL_KEYCODES_COUNT) {
		input_report_key(panel->input, panel->keycodes[button],
				 !(panel->command[2] & 0x80));
		input_sync(panel->input);
	} else {
		dev_dbg(&panel->input->dev, "unknown button %u\n",
			button);
	}
}

static int ibm_panel_i2c_slave_cb(struct i2c_client *client,
				  enum i2c_slave_event event, u8 *val)
{
	struct ibm_panel *panel = i2c_get_clientdata(client);

	dev_dbg(&panel->input->dev, "event: %u data: %02x\n", event, *val);

	guard(spinlock_irqsave)(&panel->lock);

	switch (event) {
	case I2C_SLAVE_STOP:
		if (panel->idx == sizeof(panel->command))
			ibm_panel_process_command(panel);
		else
			dev_dbg(&panel->input->dev,
				"command incorrect size %u\n", panel->idx);
		fallthrough;
	case I2C_SLAVE_WRITE_REQUESTED:
		panel->idx = 0;
		break;
	case I2C_SLAVE_WRITE_RECEIVED:
		if (panel->idx < sizeof(panel->command))
			panel->command[panel->idx++] = *val;
		else
			/*
			 * The command is too long and therefore invalid, so set the index
			 * to it's largest possible value. When a STOP is finally received,
			 * the command will be rejected upon processing.
			 */
			panel->idx = U8_MAX;
		break;
	case I2C_SLAVE_READ_REQUESTED:
	case I2C_SLAVE_READ_PROCESSED:
		*val = 0xff;
		break;
	default:
		break;
	}

	return 0;
}

static int ibm_panel_probe(struct i2c_client *client)
{
	struct ibm_panel *panel;
	int i;
	int error;

	panel = devm_kzalloc(&client->dev, sizeof(*panel), GFP_KERNEL);
	if (!panel)
		return -ENOMEM;

	spin_lock_init(&panel->lock);

	panel->input = devm_input_allocate_device(&client->dev);
	if (!panel->input)
		return -ENOMEM;

	panel->input->name = client->name;
	panel->input->id.bustype = BUS_I2C;

	error = device_property_read_u32_array(&client->dev,
					       "linux,keycodes",
					       panel->keycodes,
					       PANEL_KEYCODES_COUNT);
	if (error) {
		/*
		 * Use gamepad buttons as defaults for compatibility with
		 * existing applications.
		 */
		panel->keycodes[0] = BTN_NORTH;
		panel->keycodes[1] = BTN_SOUTH;
		panel->keycodes[2] = BTN_SELECT;
	}

	for (i = 0; i < PANEL_KEYCODES_COUNT; ++i)
		input_set_capability(panel->input, EV_KEY, panel->keycodes[i]);

	error = input_register_device(panel->input);
	if (error) {
		dev_err(&client->dev,
			"Failed to register input device: %d\n", error);
		return error;
	}

	i2c_set_clientdata(client, panel);
	error = i2c_slave_register(client, ibm_panel_i2c_slave_cb);
	if (error) {
		dev_err(&client->dev,
			"Failed to register as i2c slave: %d\n", error);
		return error;
	}

	return 0;
}

static void ibm_panel_remove(struct i2c_client *client)
{
	i2c_slave_unregister(client);
}

static const struct of_device_id ibm_panel_match[] = {
	{ .compatible = "ibm,op-panel" },
	{ }
};
MODULE_DEVICE_TABLE(of, ibm_panel_match);

static struct i2c_driver ibm_panel_driver = {
	.driver = {
		.name = DEVICE_NAME,
		.of_match_table = ibm_panel_match,
	},
	.probe = ibm_panel_probe,
	.remove = ibm_panel_remove,
};
module_i2c_driver(ibm_panel_driver);

MODULE_AUTHOR("Eddie James <eajames@linux.ibm.com>");
MODULE_DESCRIPTION("IBM Operation Panel Driver");
MODULE_LICENSE("GPL");

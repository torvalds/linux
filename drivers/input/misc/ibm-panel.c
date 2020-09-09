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

#define DEVICE_NAME	"ibm-panel"

struct ibm_panel {
	u8 idx;
	u8 command[11];
	spinlock_t lock;	/* protects writes to idx and command */
	struct input_dev *input;
};

static void ibm_panel_process_command(struct ibm_panel *panel)
{
	u8 i;
	u8 chksum;
	u16 sum = 0;
	int pressed;
	int released;

	if (panel->command[0] != 0xff && panel->command[1] != 0xf0) {
		dev_dbg(&panel->input->dev, "command invalid\n");
		return;
	}

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

	if (chksum != panel->command[sizeof(panel->command) - 1]) {
		dev_dbg(&panel->input->dev, "command failed checksum\n");
		return;
	}

	released = panel->command[2] & 0x80;
	pressed = released ? 0 : 1;

	switch (panel->command[2] & 0xf) {
	case 0:
		input_report_key(panel->input, BTN_NORTH, pressed);
		break;
	case 1:
		input_report_key(panel->input, BTN_SOUTH, pressed);
		break;
	case 2:
		input_report_key(panel->input, BTN_SELECT, pressed);
		break;
	default:
		dev_dbg(&panel->input->dev, "unknown command %u\n",
			panel->command[2] & 0xf);
		return;
	}

	input_sync(panel->input);
}

static int ibm_panel_i2c_slave_cb(struct i2c_client *client,
				  enum i2c_slave_event event, u8 *val)
{
	unsigned long flags;
	struct ibm_panel *panel = i2c_get_clientdata(client);

	dev_dbg(&panel->input->dev, "event: %u data: %02x\n", event, *val);

	spin_lock_irqsave(&panel->lock, flags);

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

	spin_unlock_irqrestore(&panel->lock, flags);

	return 0;
}

static int ibm_panel_probe(struct i2c_client *client,
			   const struct i2c_device_id *id)
{
	int rc;
	struct ibm_panel *panel = devm_kzalloc(&client->dev, sizeof(*panel),
					       GFP_KERNEL);

	if (!panel)
		return -ENOMEM;

	panel->input = devm_input_allocate_device(&client->dev);
	if (!panel->input)
		return -ENOMEM;

	panel->input->name = client->name;
	panel->input->id.bustype = BUS_I2C;
	input_set_capability(panel->input, EV_KEY, BTN_NORTH);
	input_set_capability(panel->input, EV_KEY, BTN_SOUTH);
	input_set_capability(panel->input, EV_KEY, BTN_SELECT);

	rc = input_register_device(panel->input);
	if (rc) {
		dev_err(&client->dev, "Failed to register input device: %d\n",
			rc);
		return rc;
	}

	spin_lock_init(&panel->lock);

	i2c_set_clientdata(client, panel);
	rc = i2c_slave_register(client, ibm_panel_i2c_slave_cb);
	if (rc) {
		input_unregister_device(panel->input);
		return rc;
	}

	return 0;
}

static int ibm_panel_remove(struct i2c_client *client)
{
	int rc;
	struct ibm_panel *panel = i2c_get_clientdata(client);

	rc = i2c_slave_unregister(client);

	input_unregister_device(panel->input);

	return rc;
}

static const struct of_device_id ibm_panel_match[] = {
	{ .compatible = "ibm,op-panel" },
	{ }
};

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

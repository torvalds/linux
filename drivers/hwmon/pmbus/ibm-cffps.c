/*
 * Copyright 2017 IBM Corp.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/bitops.h>
#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/i2c.h>
#include <linux/jiffies.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/pmbus.h>

#include "pmbus.h"

#define CFFPS_FRU_CMD				0x9A
#define CFFPS_PN_CMD				0x9B
#define CFFPS_SN_CMD				0x9E
#define CFFPS_CCIN_CMD				0xBD
#define CFFPS_FW_CMD_START			0xFA
#define CFFPS_FW_NUM_BYTES			4
#define CFFPS_SYS_CONFIG_CMD			0xDA

#define CFFPS_INPUT_HISTORY_CMD			0xD6
#define CFFPS_INPUT_HISTORY_SIZE		100

/* STATUS_MFR_SPECIFIC bits */
#define CFFPS_MFR_FAN_FAULT			BIT(0)
#define CFFPS_MFR_THERMAL_FAULT			BIT(1)
#define CFFPS_MFR_OV_FAULT			BIT(2)
#define CFFPS_MFR_UV_FAULT			BIT(3)
#define CFFPS_MFR_PS_KILL			BIT(4)
#define CFFPS_MFR_OC_FAULT			BIT(5)
#define CFFPS_MFR_VAUX_FAULT			BIT(6)
#define CFFPS_MFR_CURRENT_SHARE_WARNING		BIT(7)

#define CFFPS_LED_BLINK				BIT(0)
#define CFFPS_LED_ON				BIT(1)
#define CFFPS_LED_OFF				BIT(2)
#define CFFPS_BLINK_RATE_MS			250

enum {
	CFFPS_DEBUGFS_INPUT_HISTORY = 0,
	CFFPS_DEBUGFS_FRU,
	CFFPS_DEBUGFS_PN,
	CFFPS_DEBUGFS_SN,
	CFFPS_DEBUGFS_CCIN,
	CFFPS_DEBUGFS_FW,
	CFFPS_DEBUGFS_NUM_ENTRIES
};

struct ibm_cffps_input_history {
	struct mutex update_lock;
	unsigned long last_update;

	u8 byte_count;
	u8 data[CFFPS_INPUT_HISTORY_SIZE];
};

struct ibm_cffps {
	struct i2c_client *client;

	struct ibm_cffps_input_history input_history;

	int debugfs_entries[CFFPS_DEBUGFS_NUM_ENTRIES];

	char led_name[32];
	u8 led_state;
	struct led_classdev led;
};

#define to_psu(x, y) container_of((x), struct ibm_cffps, debugfs_entries[(y)])

static ssize_t ibm_cffps_read_input_history(struct ibm_cffps *psu,
					    char __user *buf, size_t count,
					    loff_t *ppos)
{
	int rc;
	u8 msgbuf0[1] = { CFFPS_INPUT_HISTORY_CMD };
	u8 msgbuf1[CFFPS_INPUT_HISTORY_SIZE + 1] = { 0 };
	struct i2c_msg msg[2] = {
		{
			.addr = psu->client->addr,
			.flags = psu->client->flags,
			.len = 1,
			.buf = msgbuf0,
		}, {
			.addr = psu->client->addr,
			.flags = psu->client->flags | I2C_M_RD,
			.len = CFFPS_INPUT_HISTORY_SIZE + 1,
			.buf = msgbuf1,
		},
	};

	if (!*ppos) {
		mutex_lock(&psu->input_history.update_lock);
		if (time_after(jiffies, psu->input_history.last_update + HZ)) {
			/*
			 * Use a raw i2c transfer, since we need more bytes
			 * than Linux I2C supports through smbus xfr (only 32).
			 */
			rc = i2c_transfer(psu->client->adapter, msg, 2);
			if (rc < 0) {
				mutex_unlock(&psu->input_history.update_lock);
				return rc;
			}

			psu->input_history.byte_count = msgbuf1[0];
			memcpy(psu->input_history.data, &msgbuf1[1],
			       CFFPS_INPUT_HISTORY_SIZE);
			psu->input_history.last_update = jiffies;
		}

		mutex_unlock(&psu->input_history.update_lock);
	}

	return simple_read_from_buffer(buf, count, ppos,
				       psu->input_history.data,
				       psu->input_history.byte_count);
}

static ssize_t ibm_cffps_debugfs_op(struct file *file, char __user *buf,
				    size_t count, loff_t *ppos)
{
	u8 cmd;
	int i, rc;
	int *idxp = file->private_data;
	int idx = *idxp;
	struct ibm_cffps *psu = to_psu(idxp, idx);
	char data[I2C_SMBUS_BLOCK_MAX] = { 0 };

	switch (idx) {
	case CFFPS_DEBUGFS_INPUT_HISTORY:
		return ibm_cffps_read_input_history(psu, buf, count, ppos);
	case CFFPS_DEBUGFS_FRU:
		cmd = CFFPS_FRU_CMD;
		break;
	case CFFPS_DEBUGFS_PN:
		cmd = CFFPS_PN_CMD;
		break;
	case CFFPS_DEBUGFS_SN:
		cmd = CFFPS_SN_CMD;
		break;
	case CFFPS_DEBUGFS_CCIN:
		rc = i2c_smbus_read_word_swapped(psu->client, CFFPS_CCIN_CMD);
		if (rc < 0)
			return rc;

		rc = snprintf(data, 5, "%04X", rc);
		goto done;
	case CFFPS_DEBUGFS_FW:
		for (i = 0; i < CFFPS_FW_NUM_BYTES; ++i) {
			rc = i2c_smbus_read_byte_data(psu->client,
						      CFFPS_FW_CMD_START + i);
			if (rc < 0)
				return rc;

			snprintf(&data[i * 2], 3, "%02X", rc);
		}

		rc = i * 2;
		goto done;
	default:
		return -EINVAL;
	}

	rc = i2c_smbus_read_block_data(psu->client, cmd, data);
	if (rc < 0)
		return rc;

done:
	data[rc] = '\n';
	rc += 2;

	return simple_read_from_buffer(buf, count, ppos, data, rc);
}

static const struct file_operations ibm_cffps_fops = {
	.llseek = noop_llseek,
	.read = ibm_cffps_debugfs_op,
	.open = simple_open,
};

static int ibm_cffps_read_byte_data(struct i2c_client *client, int page,
				    int reg)
{
	int rc, mfr;

	switch (reg) {
	case PMBUS_STATUS_VOUT:
	case PMBUS_STATUS_IOUT:
	case PMBUS_STATUS_TEMPERATURE:
	case PMBUS_STATUS_FAN_12:
		rc = pmbus_read_byte_data(client, page, reg);
		if (rc < 0)
			return rc;

		mfr = pmbus_read_byte_data(client, page,
					   PMBUS_STATUS_MFR_SPECIFIC);
		if (mfr < 0)
			/*
			 * Return the status register instead of an error,
			 * since we successfully read status.
			 */
			return rc;

		/* Add MFR_SPECIFIC bits to the standard pmbus status regs. */
		if (reg == PMBUS_STATUS_FAN_12) {
			if (mfr & CFFPS_MFR_FAN_FAULT)
				rc |= PB_FAN_FAN1_FAULT;
		} else if (reg == PMBUS_STATUS_TEMPERATURE) {
			if (mfr & CFFPS_MFR_THERMAL_FAULT)
				rc |= PB_TEMP_OT_FAULT;
		} else if (reg == PMBUS_STATUS_VOUT) {
			if (mfr & (CFFPS_MFR_OV_FAULT | CFFPS_MFR_VAUX_FAULT))
				rc |= PB_VOLTAGE_OV_FAULT;
			if (mfr & CFFPS_MFR_UV_FAULT)
				rc |= PB_VOLTAGE_UV_FAULT;
		} else if (reg == PMBUS_STATUS_IOUT) {
			if (mfr & CFFPS_MFR_OC_FAULT)
				rc |= PB_IOUT_OC_FAULT;
			if (mfr & CFFPS_MFR_CURRENT_SHARE_WARNING)
				rc |= PB_CURRENT_SHARE_FAULT;
		}
		break;
	default:
		rc = -ENODATA;
		break;
	}

	return rc;
}

static int ibm_cffps_read_word_data(struct i2c_client *client, int page,
				    int reg)
{
	int rc, mfr;

	switch (reg) {
	case PMBUS_STATUS_WORD:
		rc = pmbus_read_word_data(client, page, reg);
		if (rc < 0)
			return rc;

		mfr = pmbus_read_byte_data(client, page,
					   PMBUS_STATUS_MFR_SPECIFIC);
		if (mfr < 0)
			/*
			 * Return the status register instead of an error,
			 * since we successfully read status.
			 */
			return rc;

		if (mfr & CFFPS_MFR_PS_KILL)
			rc |= PB_STATUS_OFF;
		break;
	default:
		rc = -ENODATA;
		break;
	}

	return rc;
}

static void ibm_cffps_led_brightness_set(struct led_classdev *led_cdev,
					 enum led_brightness brightness)
{
	int rc;
	struct ibm_cffps *psu = container_of(led_cdev, struct ibm_cffps, led);

	if (brightness == LED_OFF) {
		psu->led_state = CFFPS_LED_OFF;
	} else {
		brightness = LED_FULL;
		if (psu->led_state != CFFPS_LED_BLINK)
			psu->led_state = CFFPS_LED_ON;
	}

	rc = i2c_smbus_write_byte_data(psu->client, CFFPS_SYS_CONFIG_CMD,
				       psu->led_state);
	if (rc < 0)
		return;

	led_cdev->brightness = brightness;
}

static int ibm_cffps_led_blink_set(struct led_classdev *led_cdev,
				   unsigned long *delay_on,
				   unsigned long *delay_off)
{
	int rc;
	struct ibm_cffps *psu = container_of(led_cdev, struct ibm_cffps, led);

	psu->led_state = CFFPS_LED_BLINK;

	if (led_cdev->brightness == LED_OFF)
		return 0;

	rc = i2c_smbus_write_byte_data(psu->client, CFFPS_SYS_CONFIG_CMD,
				       CFFPS_LED_BLINK);
	if (rc < 0)
		return rc;

	*delay_on = CFFPS_BLINK_RATE_MS;
	*delay_off = CFFPS_BLINK_RATE_MS;

	return 0;
}

static void ibm_cffps_create_led_class(struct ibm_cffps *psu)
{
	int rc;
	struct i2c_client *client = psu->client;
	struct device *dev = &client->dev;

	snprintf(psu->led_name, sizeof(psu->led_name), "%s-%02x", client->name,
		 client->addr);
	psu->led.name = psu->led_name;
	psu->led.max_brightness = LED_FULL;
	psu->led.brightness_set = ibm_cffps_led_brightness_set;
	psu->led.blink_set = ibm_cffps_led_blink_set;

	rc = devm_led_classdev_register(dev, &psu->led);
	if (rc)
		dev_warn(dev, "failed to register led class: %d\n", rc);
}

static struct pmbus_driver_info ibm_cffps_info = {
	.pages = 1,
	.func[0] = PMBUS_HAVE_VIN | PMBUS_HAVE_VOUT | PMBUS_HAVE_IOUT |
		PMBUS_HAVE_PIN | PMBUS_HAVE_FAN12 | PMBUS_HAVE_TEMP |
		PMBUS_HAVE_TEMP2 | PMBUS_HAVE_TEMP3 | PMBUS_HAVE_STATUS_VOUT |
		PMBUS_HAVE_STATUS_IOUT | PMBUS_HAVE_STATUS_INPUT |
		PMBUS_HAVE_STATUS_TEMP | PMBUS_HAVE_STATUS_FAN12,
	.read_byte_data = ibm_cffps_read_byte_data,
	.read_word_data = ibm_cffps_read_word_data,
};

static struct pmbus_platform_data ibm_cffps_pdata = {
	.flags = PMBUS_SKIP_STATUS_CHECK,
};

static int ibm_cffps_probe(struct i2c_client *client,
			   const struct i2c_device_id *id)
{
	int i, rc;
	struct dentry *debugfs;
	struct dentry *ibm_cffps_dir;
	struct ibm_cffps *psu;

	client->dev.platform_data = &ibm_cffps_pdata;
	rc = pmbus_do_probe(client, id, &ibm_cffps_info);
	if (rc)
		return rc;

	/*
	 * Don't fail the probe if there isn't enough memory for leds and
	 * debugfs.
	 */
	psu = devm_kzalloc(&client->dev, sizeof(*psu), GFP_KERNEL);
	if (!psu)
		return 0;

	psu->client = client;
	mutex_init(&psu->input_history.update_lock);
	psu->input_history.last_update = jiffies - HZ;

	ibm_cffps_create_led_class(psu);

	/* Don't fail the probe if we can't create debugfs */
	debugfs = pmbus_get_debugfs_dir(client);
	if (!debugfs)
		return 0;

	ibm_cffps_dir = debugfs_create_dir(client->name, debugfs);
	if (!ibm_cffps_dir)
		return 0;

	for (i = 0; i < CFFPS_DEBUGFS_NUM_ENTRIES; ++i)
		psu->debugfs_entries[i] = i;

	debugfs_create_file("input_history", 0444, ibm_cffps_dir,
			    &psu->debugfs_entries[CFFPS_DEBUGFS_INPUT_HISTORY],
			    &ibm_cffps_fops);
	debugfs_create_file("fru", 0444, ibm_cffps_dir,
			    &psu->debugfs_entries[CFFPS_DEBUGFS_FRU],
			    &ibm_cffps_fops);
	debugfs_create_file("part_number", 0444, ibm_cffps_dir,
			    &psu->debugfs_entries[CFFPS_DEBUGFS_PN],
			    &ibm_cffps_fops);
	debugfs_create_file("serial_number", 0444, ibm_cffps_dir,
			    &psu->debugfs_entries[CFFPS_DEBUGFS_SN],
			    &ibm_cffps_fops);
	debugfs_create_file("ccin", 0444, ibm_cffps_dir,
			    &psu->debugfs_entries[CFFPS_DEBUGFS_CCIN],
			    &ibm_cffps_fops);
	debugfs_create_file("fw_version", 0444, ibm_cffps_dir,
			    &psu->debugfs_entries[CFFPS_DEBUGFS_FW],
			    &ibm_cffps_fops);

	return 0;
}

static const struct i2c_device_id ibm_cffps_id[] = {
	{ "ibm_cffps1", 1 },
	{}
};
MODULE_DEVICE_TABLE(i2c, ibm_cffps_id);

static const struct of_device_id ibm_cffps_of_match[] = {
	{ .compatible = "ibm,cffps1" },
	{}
};
MODULE_DEVICE_TABLE(of, ibm_cffps_of_match);

static struct i2c_driver ibm_cffps_driver = {
	.driver = {
		.name = "ibm-cffps",
		.of_match_table = ibm_cffps_of_match,
	},
	.probe = ibm_cffps_probe,
	.remove = pmbus_do_remove,
	.id_table = ibm_cffps_id,
};

module_i2c_driver(ibm_cffps_driver);

MODULE_AUTHOR("Eddie James");
MODULE_DESCRIPTION("PMBus driver for IBM Common Form Factor power supplies");
MODULE_LICENSE("GPL");

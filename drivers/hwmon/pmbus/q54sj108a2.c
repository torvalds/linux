// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Driver for Delta modules, Q54SJ108A2 series 1/4 Brick DC/DC
 * Regulated Power Module
 *
 * Copyright 2020 Delta LLC.
 */

#include <linux/debugfs.h>
#include <linux/i2c.h>
#include <linux/kstrtox.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include "pmbus.h"

#define STORE_DEFAULT_ALL		0x11
#define ERASE_BLACKBOX_DATA		0xD1
#define READ_HISTORY_EVENT_NUMBER	0xD2
#define READ_HISTORY_EVENTS		0xE0
#define SET_HISTORY_EVENT_OFFSET	0xE1
#define PMBUS_FLASH_KEY_WRITE		0xEC

enum chips {
	q54sj108a2
};

enum {
	Q54SJ108A2_DEBUGFS_OPERATION = 0,
	Q54SJ108A2_DEBUGFS_CLEARFAULT,
	Q54SJ108A2_DEBUGFS_WRITEPROTECT,
	Q54SJ108A2_DEBUGFS_STOREDEFAULT,
	Q54SJ108A2_DEBUGFS_VOOV_RESPONSE,
	Q54SJ108A2_DEBUGFS_IOOC_RESPONSE,
	Q54SJ108A2_DEBUGFS_PMBUS_VERSION,
	Q54SJ108A2_DEBUGFS_MFR_ID,
	Q54SJ108A2_DEBUGFS_MFR_MODEL,
	Q54SJ108A2_DEBUGFS_MFR_REVISION,
	Q54SJ108A2_DEBUGFS_MFR_LOCATION,
	Q54SJ108A2_DEBUGFS_BLACKBOX_ERASE,
	Q54SJ108A2_DEBUGFS_BLACKBOX_READ_OFFSET,
	Q54SJ108A2_DEBUGFS_BLACKBOX_SET_OFFSET,
	Q54SJ108A2_DEBUGFS_BLACKBOX_READ,
	Q54SJ108A2_DEBUGFS_FLASH_KEY,
	Q54SJ108A2_DEBUGFS_NUM_ENTRIES
};

struct q54sj108a2_data {
	enum chips chip;
	struct i2c_client *client;

	int debugfs_entries[Q54SJ108A2_DEBUGFS_NUM_ENTRIES];
};

#define to_psu(x, y) container_of((x), struct q54sj108a2_data, debugfs_entries[(y)])

static struct pmbus_driver_info q54sj108a2_info[] = {
	[q54sj108a2] = {
		.pages = 1,

		/* Source : Delta Q54SJ108A2 */
		.format[PSC_TEMPERATURE] = linear,
		.format[PSC_VOLTAGE_IN] = linear,
		.format[PSC_CURRENT_OUT] = linear,

		.func[0] = PMBUS_HAVE_VIN |
		PMBUS_HAVE_VOUT | PMBUS_HAVE_STATUS_VOUT |
		PMBUS_HAVE_IOUT | PMBUS_HAVE_STATUS_IOUT |
		PMBUS_HAVE_TEMP | PMBUS_HAVE_STATUS_TEMP |
		PMBUS_HAVE_STATUS_INPUT,
	},
};

static ssize_t q54sj108a2_debugfs_read(struct file *file, char __user *buf,
				       size_t count, loff_t *ppos)
{
	int rc;
	int *idxp = file->private_data;
	int idx = *idxp;
	struct q54sj108a2_data *psu = to_psu(idxp, idx);
	char data[I2C_SMBUS_BLOCK_MAX + 2] = { 0 };
	char data_char[I2C_SMBUS_BLOCK_MAX + 2] = { 0 };
	char *res;

	switch (idx) {
	case Q54SJ108A2_DEBUGFS_OPERATION:
		rc = i2c_smbus_read_byte_data(psu->client, PMBUS_OPERATION);
		if (rc < 0)
			return rc;

		rc = snprintf(data, 3, "%02x", rc);
		break;
	case Q54SJ108A2_DEBUGFS_WRITEPROTECT:
		rc = i2c_smbus_read_byte_data(psu->client, PMBUS_WRITE_PROTECT);
		if (rc < 0)
			return rc;

		rc = snprintf(data, 3, "%02x", rc);
		break;
	case Q54SJ108A2_DEBUGFS_VOOV_RESPONSE:
		rc = i2c_smbus_read_byte_data(psu->client, PMBUS_VOUT_OV_FAULT_RESPONSE);
		if (rc < 0)
			return rc;

		rc = snprintf(data, 3, "%02x", rc);
		break;
	case Q54SJ108A2_DEBUGFS_IOOC_RESPONSE:
		rc = i2c_smbus_read_byte_data(psu->client, PMBUS_IOUT_OC_FAULT_RESPONSE);
		if (rc < 0)
			return rc;

		rc = snprintf(data, 3, "%02x", rc);
		break;
	case Q54SJ108A2_DEBUGFS_PMBUS_VERSION:
		rc = i2c_smbus_read_byte_data(psu->client, PMBUS_REVISION);
		if (rc < 0)
			return rc;

		rc = snprintf(data, 3, "%02x", rc);
		break;
	case Q54SJ108A2_DEBUGFS_MFR_ID:
		rc = i2c_smbus_read_block_data(psu->client, PMBUS_MFR_ID, data);
		if (rc < 0)
			return rc;
		break;
	case Q54SJ108A2_DEBUGFS_MFR_MODEL:
		rc = i2c_smbus_read_block_data(psu->client, PMBUS_MFR_MODEL, data);
		if (rc < 0)
			return rc;
		break;
	case Q54SJ108A2_DEBUGFS_MFR_REVISION:
		rc = i2c_smbus_read_block_data(psu->client, PMBUS_MFR_REVISION, data);
		if (rc < 0)
			return rc;
		break;
	case Q54SJ108A2_DEBUGFS_MFR_LOCATION:
		rc = i2c_smbus_read_block_data(psu->client, PMBUS_MFR_LOCATION, data);
		if (rc < 0)
			return rc;
		break;
	case Q54SJ108A2_DEBUGFS_BLACKBOX_READ_OFFSET:
		rc = i2c_smbus_read_byte_data(psu->client, READ_HISTORY_EVENT_NUMBER);
		if (rc < 0)
			return rc;

		rc = snprintf(data, 3, "%02x", rc);
		break;
	case Q54SJ108A2_DEBUGFS_BLACKBOX_READ:
		rc = i2c_smbus_read_block_data(psu->client, READ_HISTORY_EVENTS, data);
		if (rc < 0)
			return rc;

		res = bin2hex(data, data_char, 32);
		rc = res - data;

		break;
	case Q54SJ108A2_DEBUGFS_FLASH_KEY:
		rc = i2c_smbus_read_block_data(psu->client, PMBUS_FLASH_KEY_WRITE, data);
		if (rc < 0)
			return rc;

		res = bin2hex(data, data_char, 4);
		rc = res - data;

		break;
	default:
		return -EINVAL;
	}

	data[rc] = '\n';
	rc += 2;

	return simple_read_from_buffer(buf, count, ppos, data, rc);
}

static ssize_t q54sj108a2_debugfs_write(struct file *file, const char __user *buf,
					size_t count, loff_t *ppos)
{
	u8 flash_key[4];
	u8 dst_data;
	ssize_t rc;
	int *idxp = file->private_data;
	int idx = *idxp;
	struct q54sj108a2_data *psu = to_psu(idxp, idx);

	rc = i2c_smbus_write_byte_data(psu->client, PMBUS_WRITE_PROTECT, 0);
	if (rc)
		return rc;

	switch (idx) {
	case Q54SJ108A2_DEBUGFS_OPERATION:
		rc = kstrtou8_from_user(buf, count, 0, &dst_data);
		if (rc < 0)
			return rc;

		rc = i2c_smbus_write_byte_data(psu->client, PMBUS_OPERATION, dst_data);
		if (rc < 0)
			return rc;

		break;
	case Q54SJ108A2_DEBUGFS_CLEARFAULT:
		rc = i2c_smbus_write_byte(psu->client, PMBUS_CLEAR_FAULTS);
		if (rc < 0)
			return rc;

		break;
	case Q54SJ108A2_DEBUGFS_STOREDEFAULT:
		flash_key[0] = 0x7E;
		flash_key[1] = 0x15;
		flash_key[2] = 0xDC;
		flash_key[3] = 0x42;
		rc = i2c_smbus_write_block_data(psu->client, PMBUS_FLASH_KEY_WRITE, 4, flash_key);
		if (rc < 0)
			return rc;

		rc = i2c_smbus_write_byte(psu->client, STORE_DEFAULT_ALL);
		if (rc < 0)
			return rc;

		break;
	case Q54SJ108A2_DEBUGFS_VOOV_RESPONSE:
		rc = kstrtou8_from_user(buf, count, 0, &dst_data);
		if (rc < 0)
			return rc;

		rc = i2c_smbus_write_byte_data(psu->client, PMBUS_VOUT_OV_FAULT_RESPONSE, dst_data);
		if (rc < 0)
			return rc;

		break;
	case Q54SJ108A2_DEBUGFS_IOOC_RESPONSE:
		rc = kstrtou8_from_user(buf, count, 0, &dst_data);
		if (rc < 0)
			return rc;

		rc = i2c_smbus_write_byte_data(psu->client, PMBUS_IOUT_OC_FAULT_RESPONSE, dst_data);
		if (rc < 0)
			return rc;

		break;
	case Q54SJ108A2_DEBUGFS_BLACKBOX_ERASE:
		rc = i2c_smbus_write_byte(psu->client, ERASE_BLACKBOX_DATA);
		if (rc < 0)
			return rc;

		break;
	case Q54SJ108A2_DEBUGFS_BLACKBOX_SET_OFFSET:
		rc = kstrtou8_from_user(buf, count, 0, &dst_data);
		if (rc < 0)
			return rc;

		rc = i2c_smbus_write_byte_data(psu->client, SET_HISTORY_EVENT_OFFSET, dst_data);
		if (rc < 0)
			return rc;

		break;
	default:
		return -EINVAL;
	}

	return count;
}

static const struct file_operations q54sj108a2_fops = {
	.llseek = noop_llseek,
	.read = q54sj108a2_debugfs_read,
	.write = q54sj108a2_debugfs_write,
	.open = simple_open,
};

static const struct i2c_device_id q54sj108a2_id[] = {
	{ "q54sj108a2", q54sj108a2 },
	{ },
};

MODULE_DEVICE_TABLE(i2c, q54sj108a2_id);

static int q54sj108a2_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	u8 buf[I2C_SMBUS_BLOCK_MAX + 1];
	enum chips chip_id;
	int ret, i;
	struct dentry *debugfs;
	struct dentry *q54sj108a2_dir;
	struct q54sj108a2_data *psu;

	if (!i2c_check_functionality(client->adapter,
				     I2C_FUNC_SMBUS_BYTE_DATA |
				     I2C_FUNC_SMBUS_WORD_DATA |
				     I2C_FUNC_SMBUS_BLOCK_DATA))
		return -ENODEV;

	if (client->dev.of_node)
		chip_id = (enum chips)(unsigned long)of_device_get_match_data(dev);
	else
		chip_id = i2c_match_id(q54sj108a2_id, client)->driver_data;

	ret = i2c_smbus_read_block_data(client, PMBUS_MFR_ID, buf);
	if (ret < 0) {
		dev_err(&client->dev, "Failed to read Manufacturer ID\n");
		return ret;
	}
	if (ret != 6 || strncmp(buf, "DELTA", 5)) {
		buf[ret] = '\0';
		dev_err(dev, "Unsupported Manufacturer ID '%s'\n", buf);
		return -ENODEV;
	}

	/*
	 * The chips support reading PMBUS_MFR_MODEL.
	 */
	ret = i2c_smbus_read_block_data(client, PMBUS_MFR_MODEL, buf);
	if (ret < 0) {
		dev_err(dev, "Failed to read Manufacturer Model\n");
		return ret;
	}
	if (ret != 14 || strncmp(buf, "Q54SJ108A2", 10)) {
		buf[ret] = '\0';
		dev_err(dev, "Unsupported Manufacturer Model '%s'\n", buf);
		return -ENODEV;
	}

	ret = i2c_smbus_read_block_data(client, PMBUS_MFR_REVISION, buf);
	if (ret < 0) {
		dev_err(dev, "Failed to read Manufacturer Revision\n");
		return ret;
	}
	if (ret != 4 || buf[0] != 'S') {
		buf[ret] = '\0';
		dev_err(dev, "Unsupported Manufacturer Revision '%s'\n", buf);
		return -ENODEV;
	}

	ret = pmbus_do_probe(client, &q54sj108a2_info[chip_id]);
	if (ret)
		return ret;

	psu = devm_kzalloc(&client->dev, sizeof(*psu), GFP_KERNEL);
	if (!psu)
		return 0;

	psu->client = client;

	debugfs = pmbus_get_debugfs_dir(client);

	q54sj108a2_dir = debugfs_create_dir(client->name, debugfs);

	for (i = 0; i < Q54SJ108A2_DEBUGFS_NUM_ENTRIES; ++i)
		psu->debugfs_entries[i] = i;

	debugfs_create_file("operation", 0644, q54sj108a2_dir,
			    &psu->debugfs_entries[Q54SJ108A2_DEBUGFS_OPERATION],
			    &q54sj108a2_fops);
	debugfs_create_file("clear_fault", 0200, q54sj108a2_dir,
			    &psu->debugfs_entries[Q54SJ108A2_DEBUGFS_CLEARFAULT],
			    &q54sj108a2_fops);
	debugfs_create_file("write_protect", 0444, q54sj108a2_dir,
			    &psu->debugfs_entries[Q54SJ108A2_DEBUGFS_WRITEPROTECT],
			    &q54sj108a2_fops);
	debugfs_create_file("store_default", 0200, q54sj108a2_dir,
			    &psu->debugfs_entries[Q54SJ108A2_DEBUGFS_STOREDEFAULT],
			    &q54sj108a2_fops);
	debugfs_create_file("vo_ov_response", 0644, q54sj108a2_dir,
			    &psu->debugfs_entries[Q54SJ108A2_DEBUGFS_VOOV_RESPONSE],
			    &q54sj108a2_fops);
	debugfs_create_file("io_oc_response", 0644, q54sj108a2_dir,
			    &psu->debugfs_entries[Q54SJ108A2_DEBUGFS_IOOC_RESPONSE],
			    &q54sj108a2_fops);
	debugfs_create_file("pmbus_revision", 0444, q54sj108a2_dir,
			    &psu->debugfs_entries[Q54SJ108A2_DEBUGFS_PMBUS_VERSION],
			    &q54sj108a2_fops);
	debugfs_create_file("mfr_id", 0444, q54sj108a2_dir,
			    &psu->debugfs_entries[Q54SJ108A2_DEBUGFS_MFR_ID],
			    &q54sj108a2_fops);
	debugfs_create_file("mfr_model", 0444, q54sj108a2_dir,
			    &psu->debugfs_entries[Q54SJ108A2_DEBUGFS_MFR_MODEL],
			    &q54sj108a2_fops);
	debugfs_create_file("mfr_revision", 0444, q54sj108a2_dir,
			    &psu->debugfs_entries[Q54SJ108A2_DEBUGFS_MFR_REVISION],
			    &q54sj108a2_fops);
	debugfs_create_file("mfr_location", 0444, q54sj108a2_dir,
			    &psu->debugfs_entries[Q54SJ108A2_DEBUGFS_MFR_LOCATION],
			    &q54sj108a2_fops);
	debugfs_create_file("blackbox_erase", 0200, q54sj108a2_dir,
			    &psu->debugfs_entries[Q54SJ108A2_DEBUGFS_BLACKBOX_ERASE],
			    &q54sj108a2_fops);
	debugfs_create_file("blackbox_read_offset", 0444, q54sj108a2_dir,
			    &psu->debugfs_entries[Q54SJ108A2_DEBUGFS_BLACKBOX_READ_OFFSET],
			    &q54sj108a2_fops);
	debugfs_create_file("blackbox_set_offset", 0200, q54sj108a2_dir,
			    &psu->debugfs_entries[Q54SJ108A2_DEBUGFS_BLACKBOX_SET_OFFSET],
			    &q54sj108a2_fops);
	debugfs_create_file("blackbox_read", 0444, q54sj108a2_dir,
			    &psu->debugfs_entries[Q54SJ108A2_DEBUGFS_BLACKBOX_READ],
			    &q54sj108a2_fops);
	debugfs_create_file("flash_key", 0444, q54sj108a2_dir,
			    &psu->debugfs_entries[Q54SJ108A2_DEBUGFS_FLASH_KEY],
			    &q54sj108a2_fops);

	return 0;
}

static const struct of_device_id q54sj108a2_of_match[] = {
	{ .compatible = "delta,q54sj108a2", .data = (void *)q54sj108a2 },
	{ },
};

MODULE_DEVICE_TABLE(of, q54sj108a2_of_match);

static struct i2c_driver q54sj108a2_driver = {
	.driver = {
		.name = "q54sj108a2",
		.of_match_table = q54sj108a2_of_match,
	},
	.probe_new = q54sj108a2_probe,
	.id_table = q54sj108a2_id,
};

module_i2c_driver(q54sj108a2_driver);

MODULE_AUTHOR("Xiao.Ma <xiao.mx.ma@deltaww.com>");
MODULE_DESCRIPTION("PMBus driver for Delta Q54SJ108A2 series modules");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS(PMBUS);

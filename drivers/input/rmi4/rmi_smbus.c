// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2015 - 2016 Red Hat, Inc
 * Copyright (c) 2011, 2012 Synaptics Incorporated
 * Copyright (c) 2011 Unixphere
 */

#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/kconfig.h>
#include <linux/lockdep.h>
#include <linux/module.h>
#include <linux/pm.h>
#include <linux/rmi.h>
#include <linux/slab.h>
#include "rmi_driver.h"

#define SMB_PROTOCOL_VERSION_ADDRESS	0xfd
#define SMB_MAX_COUNT			32
#define RMI_SMB2_MAP_SIZE		8 /* 8 entry of 4 bytes each */
#define RMI_SMB2_MAP_FLAGS_WE		0x01

struct mapping_table_entry {
	__le16 rmiaddr;
	u8 readcount;
	u8 flags;
};

struct rmi_smb_xport {
	struct rmi_transport_dev xport;
	struct i2c_client *client;

	struct mutex page_mutex;
	int page;
	u8 table_index;
	struct mutex mappingtable_mutex;
	struct mapping_table_entry mapping_table[RMI_SMB2_MAP_SIZE];
};

static int rmi_smb_get_version(struct rmi_smb_xport *rmi_smb)
{
	struct i2c_client *client = rmi_smb->client;
	int retval;

	/* Check if for SMBus new version device by reading version byte. */
	retval = i2c_smbus_read_byte_data(client, SMB_PROTOCOL_VERSION_ADDRESS);
	if (retval < 0) {
		dev_err(&client->dev, "failed to get SMBus version number!\n");
		return retval;
	}

	return retval + 1;
}

/* SMB block write - wrapper over ic2_smb_write_block */
static int smb_block_write(struct rmi_transport_dev *xport,
			      u8 commandcode, const void *buf, size_t len)
{
	struct rmi_smb_xport *rmi_smb =
		container_of(xport, struct rmi_smb_xport, xport);
	struct i2c_client *client = rmi_smb->client;
	int retval;

	retval = i2c_smbus_write_block_data(client, commandcode, len, buf);

	rmi_dbg(RMI_DEBUG_XPORT, &client->dev,
		"wrote %zd bytes at %#04x: %d (%*ph)\n",
		len, commandcode, retval, (int)len, buf);

	return retval;
}

/*
 * The function to get command code for smbus operations and keeps
 * records to the driver mapping table
 */
static int rmi_smb_get_command_code(struct rmi_transport_dev *xport,
		u16 rmiaddr, int bytecount, bool isread, u8 *commandcode)
{
	struct rmi_smb_xport *rmi_smb =
		container_of(xport, struct rmi_smb_xport, xport);
	struct mapping_table_entry new_map;
	int i;
	int retval = 0;

	mutex_lock(&rmi_smb->mappingtable_mutex);

	for (i = 0; i < RMI_SMB2_MAP_SIZE; i++) {
		struct mapping_table_entry *entry = &rmi_smb->mapping_table[i];

		if (le16_to_cpu(entry->rmiaddr) == rmiaddr) {
			if (isread) {
				if (entry->readcount == bytecount)
					goto exit;
			} else {
				if (entry->flags & RMI_SMB2_MAP_FLAGS_WE) {
					goto exit;
				}
			}
		}
	}

	i = rmi_smb->table_index;
	rmi_smb->table_index = (i + 1) % RMI_SMB2_MAP_SIZE;

	/* constructs mapping table data entry. 4 bytes each entry */
	memset(&new_map, 0, sizeof(new_map));
	new_map.rmiaddr = cpu_to_le16(rmiaddr);
	new_map.readcount = bytecount;
	new_map.flags = !isread ? RMI_SMB2_MAP_FLAGS_WE : 0;

	retval = smb_block_write(xport, i + 0x80, &new_map, sizeof(new_map));
	if (retval < 0) {
		/*
		 * if not written to device mapping table
		 * clear the driver mapping table records
		 */
		memset(&new_map, 0, sizeof(new_map));
	}

	/* save to the driver level mapping table */
	rmi_smb->mapping_table[i] = new_map;

exit:
	mutex_unlock(&rmi_smb->mappingtable_mutex);

	if (retval < 0)
		return retval;

	*commandcode = i;
	return 0;
}

static int rmi_smb_write_block(struct rmi_transport_dev *xport, u16 rmiaddr,
				const void *databuff, size_t len)
{
	int retval = 0;
	u8 commandcode;
	struct rmi_smb_xport *rmi_smb =
		container_of(xport, struct rmi_smb_xport, xport);
	int cur_len = (int)len;

	mutex_lock(&rmi_smb->page_mutex);

	while (cur_len > 0) {
		/*
		 * break into 32 bytes chunks to write get command code
		 */
		int block_len = min_t(int, len, SMB_MAX_COUNT);

		retval = rmi_smb_get_command_code(xport, rmiaddr, block_len,
						  false, &commandcode);
		if (retval < 0)
			goto exit;

		retval = smb_block_write(xport, commandcode,
					 databuff, block_len);
		if (retval < 0)
			goto exit;

		/* prepare to write next block of bytes */
		cur_len -= SMB_MAX_COUNT;
		databuff += SMB_MAX_COUNT;
		rmiaddr += SMB_MAX_COUNT;
	}
exit:
	mutex_unlock(&rmi_smb->page_mutex);
	return retval;
}

/* SMB block read - wrapper over ic2_smb_read_block */
static int smb_block_read(struct rmi_transport_dev *xport,
			     u8 commandcode, void *buf, size_t len)
{
	struct rmi_smb_xport *rmi_smb =
		container_of(xport, struct rmi_smb_xport, xport);
	struct i2c_client *client = rmi_smb->client;
	int retval;

	retval = i2c_smbus_read_block_data(client, commandcode, buf);
	if (retval < 0)
		return retval;

	return retval;
}

static int rmi_smb_read_block(struct rmi_transport_dev *xport, u16 rmiaddr,
			      void *databuff, size_t len)
{
	struct rmi_smb_xport *rmi_smb =
		container_of(xport, struct rmi_smb_xport, xport);
	int retval;
	u8 commandcode;
	int cur_len = (int)len;

	mutex_lock(&rmi_smb->page_mutex);
	memset(databuff, 0, len);

	while (cur_len > 0) {
		/* break into 32 bytes chunks to write get command code */
		int block_len =  min_t(int, cur_len, SMB_MAX_COUNT);

		retval = rmi_smb_get_command_code(xport, rmiaddr, block_len,
						  true, &commandcode);
		if (retval < 0)
			goto exit;

		retval = smb_block_read(xport, commandcode,
					databuff, block_len);
		if (retval < 0)
			goto exit;

		/* prepare to read next block of bytes */
		cur_len -= SMB_MAX_COUNT;
		databuff += SMB_MAX_COUNT;
		rmiaddr += SMB_MAX_COUNT;
	}

	retval = 0;

exit:
	mutex_unlock(&rmi_smb->page_mutex);
	return retval;
}

static void rmi_smb_clear_state(struct rmi_smb_xport *rmi_smb)
{
	/* the mapping table has been flushed, discard the current one */
	mutex_lock(&rmi_smb->mappingtable_mutex);
	memset(rmi_smb->mapping_table, 0, sizeof(rmi_smb->mapping_table));
	mutex_unlock(&rmi_smb->mappingtable_mutex);
}

static int rmi_smb_enable_smbus_mode(struct rmi_smb_xport *rmi_smb)
{
	int retval;

	/* we need to get the smbus version to activate the touchpad */
	retval = rmi_smb_get_version(rmi_smb);
	if (retval < 0)
		return retval;

	return 0;
}

static int rmi_smb_reset(struct rmi_transport_dev *xport, u16 reset_addr)
{
	struct rmi_smb_xport *rmi_smb =
		container_of(xport, struct rmi_smb_xport, xport);

	rmi_smb_clear_state(rmi_smb);

	/*
	 * we do not call the actual reset command, it has to be handled in
	 * PS/2 or there will be races between PS/2 and SMBus.
	 * PS/2 should ensure that a psmouse_reset is called before
	 * intializing the device and after it has been removed to be in a known
	 * state.
	 */
	return rmi_smb_enable_smbus_mode(rmi_smb);
}

static const struct rmi_transport_ops rmi_smb_ops = {
	.write_block	= rmi_smb_write_block,
	.read_block	= rmi_smb_read_block,
	.reset		= rmi_smb_reset,
};

static int rmi_smb_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct rmi_device_platform_data *pdata = dev_get_platdata(&client->dev);
	struct rmi_smb_xport *rmi_smb;
	int smbus_version;
	int error;

	if (!pdata) {
		dev_err(&client->dev, "no platform data, aborting\n");
		return -ENOMEM;
	}

	if (!i2c_check_functionality(client->adapter,
				     I2C_FUNC_SMBUS_READ_BLOCK_DATA |
				     I2C_FUNC_SMBUS_HOST_NOTIFY)) {
		dev_err(&client->dev,
			"adapter does not support required functionality\n");
		return -ENODEV;
	}

	if (client->irq <= 0) {
		dev_err(&client->dev, "no IRQ provided, giving up\n");
		return client->irq ? client->irq : -ENODEV;
	}

	rmi_smb = devm_kzalloc(&client->dev, sizeof(struct rmi_smb_xport),
				GFP_KERNEL);
	if (!rmi_smb)
		return -ENOMEM;

	rmi_dbg(RMI_DEBUG_XPORT, &client->dev, "Probing %s\n",
		dev_name(&client->dev));

	rmi_smb->client = client;
	mutex_init(&rmi_smb->page_mutex);
	mutex_init(&rmi_smb->mappingtable_mutex);

	rmi_smb->xport.dev = &client->dev;
	rmi_smb->xport.pdata = *pdata;
	rmi_smb->xport.pdata.irq = client->irq;
	rmi_smb->xport.proto_name = "smb";
	rmi_smb->xport.ops = &rmi_smb_ops;

	smbus_version = rmi_smb_get_version(rmi_smb);
	if (smbus_version < 0)
		return smbus_version;

	rmi_dbg(RMI_DEBUG_XPORT, &client->dev, "Smbus version is %d",
		smbus_version);

	if (smbus_version != 2 && smbus_version != 3) {
		dev_err(&client->dev, "Unrecognized SMB version %d\n",
				smbus_version);
		return -ENODEV;
	}

	i2c_set_clientdata(client, rmi_smb);

	dev_info(&client->dev, "registering SMbus-connected sensor\n");

	error = rmi_register_transport_device(&rmi_smb->xport);
	if (error) {
		dev_err(&client->dev, "failed to register sensor: %d\n", error);
		return error;
	}

	return 0;
}

static void rmi_smb_remove(struct i2c_client *client)
{
	struct rmi_smb_xport *rmi_smb = i2c_get_clientdata(client);

	rmi_unregister_transport_device(&rmi_smb->xport);
}

static int __maybe_unused rmi_smb_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct rmi_smb_xport *rmi_smb = i2c_get_clientdata(client);
	int ret;

	ret = rmi_driver_suspend(rmi_smb->xport.rmi_dev, true);
	if (ret)
		dev_warn(dev, "Failed to suspend device: %d\n", ret);

	return ret;
}

static int __maybe_unused rmi_smb_runtime_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct rmi_smb_xport *rmi_smb = i2c_get_clientdata(client);
	int ret;

	ret = rmi_driver_suspend(rmi_smb->xport.rmi_dev, false);
	if (ret)
		dev_warn(dev, "Failed to suspend device: %d\n", ret);

	return ret;
}

static int __maybe_unused rmi_smb_resume(struct device *dev)
{
	struct i2c_client *client = container_of(dev, struct i2c_client, dev);
	struct rmi_smb_xport *rmi_smb = i2c_get_clientdata(client);
	struct rmi_device *rmi_dev = rmi_smb->xport.rmi_dev;
	int ret;

	rmi_smb_reset(&rmi_smb->xport, 0);

	rmi_reset(rmi_dev);

	ret = rmi_driver_resume(rmi_smb->xport.rmi_dev, true);
	if (ret)
		dev_warn(dev, "Failed to resume device: %d\n", ret);

	return 0;
}

static int __maybe_unused rmi_smb_runtime_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct rmi_smb_xport *rmi_smb = i2c_get_clientdata(client);
	int ret;

	ret = rmi_driver_resume(rmi_smb->xport.rmi_dev, false);
	if (ret)
		dev_warn(dev, "Failed to resume device: %d\n", ret);

	return 0;
}

static const struct dev_pm_ops rmi_smb_pm = {
	SET_SYSTEM_SLEEP_PM_OPS(rmi_smb_suspend, rmi_smb_resume)
	SET_RUNTIME_PM_OPS(rmi_smb_runtime_suspend, rmi_smb_runtime_resume,
			   NULL)
};

static const struct i2c_device_id rmi_id[] = {
	{ "rmi4_smbus", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, rmi_id);

static struct i2c_driver rmi_smb_driver = {
	.driver = {
		.name	= "rmi4_smbus",
		.pm	= &rmi_smb_pm,
	},
	.id_table	= rmi_id,
	.probe		= rmi_smb_probe,
	.remove		= rmi_smb_remove,
};

module_i2c_driver(rmi_smb_driver);

MODULE_AUTHOR("Andrew Duggan <aduggan@synaptics.com>");
MODULE_AUTHOR("Benjamin Tissoires <benjamin.tissoires@redhat.com>");
MODULE_DESCRIPTION("RMI4 SMBus driver");
MODULE_LICENSE("GPL");

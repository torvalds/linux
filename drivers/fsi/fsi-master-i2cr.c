// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) IBM Corporation 2023 */

#include <linux/device.h>
#include <linux/fsi.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/mutex.h>

#include "fsi-master-i2cr.h"

#define CREATE_TRACE_POINTS
#include <trace/events/fsi_master_i2cr.h>

#define I2CR_ADDRESS_CFAM(a)	((a) >> 2)
#define I2CR_INITIAL_PARITY	true

#define I2CR_STATUS_CMD		0x60002
#define  I2CR_STATUS_ERR	 BIT_ULL(61)
#define I2CR_ERROR_CMD		0x60004
#define I2CR_LOG_CMD		0x60008

static const u8 i2cr_cfam[] = {
	0xc0, 0x02, 0x0d, 0xa6,
	0x80, 0x01, 0x10, 0x02,
	0x80, 0x01, 0x10, 0x02,
	0x80, 0x01, 0x10, 0x02,
	0x80, 0x01, 0x80, 0x52,
	0x80, 0x01, 0x10, 0x02,
	0x80, 0x01, 0x10, 0x02,
	0x80, 0x01, 0x10, 0x02,
	0x80, 0x01, 0x10, 0x02,
	0x80, 0x01, 0x22, 0x2d,
	0x00, 0x00, 0x00, 0x00,
	0xde, 0xad, 0xc0, 0xde
};

static bool i2cr_check_parity32(u32 v, bool parity)
{
	u32 i;

	for (i = 0; i < 32; ++i) {
		if (v & (1u << i))
			parity = !parity;
	}

	return parity;
}

static bool i2cr_check_parity64(u64 v)
{
	u32 i;
	bool parity = I2CR_INITIAL_PARITY;

	for (i = 0; i < 64; ++i) {
		if (v & (1llu << i))
			parity = !parity;
	}

	return parity;
}

static u32 i2cr_get_command(u32 address, bool parity)
{
	address <<= 1;

	if (i2cr_check_parity32(address, parity))
		address |= 1;

	return address;
}

static int i2cr_transfer(struct i2c_client *client, u32 command, u64 *data)
{
	struct i2c_msg msgs[2];
	int ret;

	msgs[0].addr = client->addr;
	msgs[0].flags = 0;
	msgs[0].len = sizeof(command);
	msgs[0].buf = (__u8 *)&command;
	msgs[1].addr = client->addr;
	msgs[1].flags = I2C_M_RD;
	msgs[1].len = sizeof(*data);
	msgs[1].buf = (__u8 *)data;

	ret = i2c_transfer(client->adapter, msgs, 2);
	if (ret == 2)
		return 0;

	trace_i2cr_i2c_error(client, command, ret);

	if (ret < 0)
		return ret;

	return -EIO;
}

static int i2cr_check_status(struct i2c_client *client)
{
	u64 status;
	int ret;

	ret = i2cr_transfer(client, I2CR_STATUS_CMD, &status);
	if (ret)
		return ret;

	if (status & I2CR_STATUS_ERR) {
		u32 buf[3] = { 0, 0, 0 };
		u64 error;
		u64 log;

		i2cr_transfer(client, I2CR_ERROR_CMD, &error);
		i2cr_transfer(client, I2CR_LOG_CMD, &log);

		trace_i2cr_status_error(client, status, error, log);

		buf[0] = I2CR_STATUS_CMD;
		i2c_master_send(client, (const char *)buf, sizeof(buf));

		buf[0] = I2CR_ERROR_CMD;
		i2c_master_send(client, (const char *)buf, sizeof(buf));

		buf[0] = I2CR_LOG_CMD;
		i2c_master_send(client, (const char *)buf, sizeof(buf));

		dev_err(&client->dev, "status:%016llx error:%016llx log:%016llx\n", status, error,
			log);
		return -EREMOTEIO;
	}

	trace_i2cr_status(client, status);
	return 0;
}

int fsi_master_i2cr_read(struct fsi_master_i2cr *i2cr, u32 addr, u64 *data)
{
	u32 command = i2cr_get_command(addr, I2CR_INITIAL_PARITY);
	int ret;

	mutex_lock(&i2cr->lock);

	ret = i2cr_transfer(i2cr->client, command, data);
	if (ret)
		goto unlock;

	ret = i2cr_check_status(i2cr->client);
	if (ret)
		goto unlock;

	trace_i2cr_read(i2cr->client, command, data);

unlock:
	mutex_unlock(&i2cr->lock);
	return ret;
}
EXPORT_SYMBOL_GPL(fsi_master_i2cr_read);

int fsi_master_i2cr_write(struct fsi_master_i2cr *i2cr, u32 addr, u64 data)
{
	u32 buf[3] = { 0 };
	int ret;

	buf[0] = i2cr_get_command(addr, i2cr_check_parity64(data));
	memcpy(&buf[1], &data, sizeof(data));

	mutex_lock(&i2cr->lock);

	ret = i2c_master_send(i2cr->client, (const char *)buf, sizeof(buf));
	if (ret == sizeof(buf)) {
		ret = i2cr_check_status(i2cr->client);
		if (!ret)
			trace_i2cr_write(i2cr->client, buf[0], data);
	} else {
		trace_i2cr_i2c_error(i2cr->client, buf[0], ret);

		if (ret >= 0)
			ret = -EIO;
	}

	mutex_unlock(&i2cr->lock);
	return ret;
}
EXPORT_SYMBOL_GPL(fsi_master_i2cr_write);

static int i2cr_read(struct fsi_master *master, int link, uint8_t id, uint32_t addr, void *val,
		     size_t size)
{
	struct fsi_master_i2cr *i2cr = container_of(master, struct fsi_master_i2cr, master);
	u64 data;
	size_t i;
	int ret;

	if (link || id || (addr & 0xffff0000) || !(size == 1 || size == 2 || size == 4))
		return -EINVAL;

	/*
	 * The I2CR doesn't have CFAM or FSI slave address space - only the
	 * engines. In order for this to work with the FSI core, we need to
	 * emulate at minimum the CFAM config table so that the appropriate
	 * engines are discovered.
	 */
	if (addr < 0xc00) {
		if (addr > sizeof(i2cr_cfam) - 4)
			addr = (addr & 0x3) + (sizeof(i2cr_cfam) - 4);

		memcpy(val, &i2cr_cfam[addr], size);
		return 0;
	}

	ret = fsi_master_i2cr_read(i2cr, I2CR_ADDRESS_CFAM(addr), &data);
	if (ret)
		return ret;

	/*
	 * FSI core expects up to 4 bytes BE back, while I2CR replied with LE
	 * bytes on the wire.
	 */
	for (i = 0; i < size; ++i)
		((u8 *)val)[i] = ((u8 *)&data)[7 - i];

	return 0;
}

static int i2cr_write(struct fsi_master *master, int link, uint8_t id, uint32_t addr,
		      const void *val, size_t size)
{
	struct fsi_master_i2cr *i2cr = container_of(master, struct fsi_master_i2cr, master);
	u64 data = 0;
	size_t i;

	if (link || id || (addr & 0xffff0000) || !(size == 1 || size == 2 || size == 4))
		return -EINVAL;

	/* I2CR writes to CFAM or FSI slave address are a successful no-op. */
	if (addr < 0xc00)
		return 0;

	/*
	 * FSI core passes up to 4 bytes BE, while the I2CR expects LE bytes on
	 * the wire.
	 */
	for (i = 0; i < size; ++i)
		((u8 *)&data)[7 - i] = ((u8 *)val)[i];

	return fsi_master_i2cr_write(i2cr, I2CR_ADDRESS_CFAM(addr), data);
}

static void i2cr_release(struct device *dev)
{
	struct fsi_master_i2cr *i2cr = to_fsi_master_i2cr(to_fsi_master(dev));

	of_node_put(dev->of_node);

	kfree(i2cr);
}

static int i2cr_probe(struct i2c_client *client)
{
	struct fsi_master_i2cr *i2cr;
	int ret;

	i2cr = kzalloc(sizeof(*i2cr), GFP_KERNEL);
	if (!i2cr)
		return -ENOMEM;

	/* Only one I2CR on any given I2C bus (fixed I2C device address) */
	i2cr->master.idx = client->adapter->nr;
	dev_set_name(&i2cr->master.dev, "i2cr%d", i2cr->master.idx);
	i2cr->master.dev.parent = &client->dev;
	i2cr->master.dev.of_node = of_node_get(dev_of_node(&client->dev));
	i2cr->master.dev.release = i2cr_release;

	i2cr->master.n_links = 1;
	i2cr->master.read = i2cr_read;
	i2cr->master.write = i2cr_write;

	mutex_init(&i2cr->lock);
	i2cr->client = client;

	ret = fsi_master_register(&i2cr->master);
	if (ret)
		return ret;

	i2c_set_clientdata(client, i2cr);
	return 0;
}

static void i2cr_remove(struct i2c_client *client)
{
	struct fsi_master_i2cr *i2cr = i2c_get_clientdata(client);

	fsi_master_unregister(&i2cr->master);
}

static const struct of_device_id i2cr_ids[] = {
	{ .compatible = "ibm,i2cr-fsi-master" },
	{ }
};
MODULE_DEVICE_TABLE(of, i2cr_ids);

static struct i2c_driver i2cr_driver = {
	.probe = i2cr_probe,
	.remove = i2cr_remove,
	.driver = {
		.name = "fsi-master-i2cr",
		.of_match_table = i2cr_ids,
	},
};

module_i2c_driver(i2cr_driver)

MODULE_AUTHOR("Eddie James <eajames@linux.ibm.com>");
MODULE_DESCRIPTION("IBM I2C Responder virtual FSI master driver");
MODULE_LICENSE("GPL");

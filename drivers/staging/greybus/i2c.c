// SPDX-License-Identifier: GPL-2.0
/*
 * I2C bridge driver for the Greybus "generic" I2C module.
 *
 * Copyright 2014 Google Inc.
 * Copyright 2014 Linaro Ltd.
 *
 * Released under the GPLv2 only.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/i2c.h>

#include "greybus.h"
#include "gbphy.h"

struct gb_i2c_device {
	struct gb_connection	*connection;
	struct gbphy_device	*gbphy_dev;

	u32			functionality;

	struct i2c_adapter	adapter;
};

/*
 * Map Greybus i2c functionality bits into Linux ones
 */
static u32 gb_i2c_functionality_map(u32 gb_i2c_functionality)
{
	return gb_i2c_functionality;	/* All bits the same for now */
}

static int gb_i2c_functionality_operation(struct gb_i2c_device *gb_i2c_dev)
{
	struct gb_i2c_functionality_response response;
	u32 functionality;
	int ret;

	ret = gb_operation_sync(gb_i2c_dev->connection,
				GB_I2C_TYPE_FUNCTIONALITY,
				NULL, 0, &response, sizeof(response));
	if (ret)
		return ret;

	functionality = le32_to_cpu(response.functionality);
	gb_i2c_dev->functionality = gb_i2c_functionality_map(functionality);

	return 0;
}

/*
 * Map Linux i2c_msg flags into Greybus i2c transfer op flags.
 */
static u16 gb_i2c_transfer_op_flags_map(u16 flags)
{
	return flags;	/* All flags the same for now */
}

static void
gb_i2c_fill_transfer_op(struct gb_i2c_transfer_op *op, struct i2c_msg *msg)
{
	u16 flags = gb_i2c_transfer_op_flags_map(msg->flags);

	op->addr = cpu_to_le16(msg->addr);
	op->flags = cpu_to_le16(flags);
	op->size = cpu_to_le16(msg->len);
}

static struct gb_operation *
gb_i2c_operation_create(struct gb_connection *connection,
			struct i2c_msg *msgs, u32 msg_count)
{
	struct gb_i2c_device *gb_i2c_dev = gb_connection_get_data(connection);
	struct gb_i2c_transfer_request *request;
	struct gb_operation *operation;
	struct gb_i2c_transfer_op *op;
	struct i2c_msg *msg;
	u32 data_out_size = 0;
	u32 data_in_size = 0;
	size_t request_size;
	void *data;
	u16 op_count;
	u32 i;

	if (msg_count > (u32)U16_MAX) {
		dev_err(&gb_i2c_dev->gbphy_dev->dev, "msg_count (%u) too big\n",
			msg_count);
		return NULL;
	}
	op_count = (u16)msg_count;

	/*
	 * In addition to space for all message descriptors we need
	 * to have enough to hold all outbound message data.
	 */
	msg = msgs;
	for (i = 0; i < msg_count; i++, msg++)
		if (msg->flags & I2C_M_RD)
			data_in_size += (u32)msg->len;
		else
			data_out_size += (u32)msg->len;

	request_size = sizeof(*request);
	request_size += msg_count * sizeof(*op);
	request_size += data_out_size;

	/* Response consists only of incoming data */
	operation = gb_operation_create(connection, GB_I2C_TYPE_TRANSFER,
				request_size, data_in_size, GFP_KERNEL);
	if (!operation)
		return NULL;

	request = operation->request->payload;
	request->op_count = cpu_to_le16(op_count);
	/* Fill in the ops array */
	op = &request->ops[0];
	msg = msgs;
	for (i = 0; i < msg_count; i++)
		gb_i2c_fill_transfer_op(op++, msg++);

	if (!data_out_size)
		return operation;

	/* Copy over the outgoing data; it starts after the last op */
	data = op;
	msg = msgs;
	for (i = 0; i < msg_count; i++) {
		if (!(msg->flags & I2C_M_RD)) {
			memcpy(data, msg->buf, msg->len);
			data += msg->len;
		}
		msg++;
	}

	return operation;
}

static void gb_i2c_decode_response(struct i2c_msg *msgs, u32 msg_count,
				struct gb_i2c_transfer_response *response)
{
	struct i2c_msg *msg = msgs;
	u8 *data;
	u32 i;

	if (!response)
		return;
	data = response->data;
	for (i = 0; i < msg_count; i++) {
		if (msg->flags & I2C_M_RD) {
			memcpy(msg->buf, data, msg->len);
			data += msg->len;
		}
		msg++;
	}
}

/*
 * Some i2c transfer operations return results that are expected.
 */
static bool gb_i2c_expected_transfer_error(int errno)
{
	return errno == -EAGAIN || errno == -ENODEV;
}

static int gb_i2c_transfer_operation(struct gb_i2c_device *gb_i2c_dev,
					struct i2c_msg *msgs, u32 msg_count)
{
	struct gb_connection *connection = gb_i2c_dev->connection;
	struct device *dev = &gb_i2c_dev->gbphy_dev->dev;
	struct gb_operation *operation;
	int ret;

	operation = gb_i2c_operation_create(connection, msgs, msg_count);
	if (!operation)
		return -ENOMEM;

	ret = gbphy_runtime_get_sync(gb_i2c_dev->gbphy_dev);
	if (ret)
		goto exit_operation_put;

	ret = gb_operation_request_send_sync(operation);
	if (!ret) {
		struct gb_i2c_transfer_response *response;

		response = operation->response->payload;
		gb_i2c_decode_response(msgs, msg_count, response);
		ret = msg_count;
	} else if (!gb_i2c_expected_transfer_error(ret)) {
		dev_err(dev, "transfer operation failed (%d)\n", ret);
	}

	gbphy_runtime_put_autosuspend(gb_i2c_dev->gbphy_dev);

exit_operation_put:
	gb_operation_put(operation);

	return ret;
}

static int gb_i2c_master_xfer(struct i2c_adapter *adap, struct i2c_msg *msgs,
		int msg_count)
{
	struct gb_i2c_device *gb_i2c_dev;

	gb_i2c_dev = i2c_get_adapdata(adap);

	return gb_i2c_transfer_operation(gb_i2c_dev, msgs, msg_count);
}

#if 0
/* Later */
static int gb_i2c_smbus_xfer(struct i2c_adapter *adap,
			u16 addr, unsigned short flags, char read_write,
			u8 command, int size, union i2c_smbus_data *data)
{
	struct gb_i2c_device *gb_i2c_dev;

	gb_i2c_dev = i2c_get_adapdata(adap);

	return 0;
}
#endif

static u32 gb_i2c_functionality(struct i2c_adapter *adap)
{
	struct gb_i2c_device *gb_i2c_dev = i2c_get_adapdata(adap);

	return gb_i2c_dev->functionality;
}

static const struct i2c_algorithm gb_i2c_algorithm = {
	.master_xfer	= gb_i2c_master_xfer,
	/* .smbus_xfer	= gb_i2c_smbus_xfer, */
	.functionality	= gb_i2c_functionality,
};

/*
 * Do initial setup of the i2c device.  This includes verifying we
 * can support it (based on the protocol version it advertises).
 * If that's OK, we get and cached its functionality bits.
 *
 * Note: gb_i2c_dev->connection is assumed to have been valid.
 */
static int gb_i2c_device_setup(struct gb_i2c_device *gb_i2c_dev)
{
	/* Assume the functionality never changes, just get it once */
	return gb_i2c_functionality_operation(gb_i2c_dev);
}

static int gb_i2c_probe(struct gbphy_device *gbphy_dev,
			 const struct gbphy_device_id *id)
{
	struct gb_connection *connection;
	struct gb_i2c_device *gb_i2c_dev;
	struct i2c_adapter *adapter;
	int ret;

	gb_i2c_dev = kzalloc(sizeof(*gb_i2c_dev), GFP_KERNEL);
	if (!gb_i2c_dev)
		return -ENOMEM;

	connection = gb_connection_create(gbphy_dev->bundle,
					  le16_to_cpu(gbphy_dev->cport_desc->id),
					  NULL);
	if (IS_ERR(connection)) {
		ret = PTR_ERR(connection);
		goto exit_i2cdev_free;
	}

	gb_i2c_dev->connection = connection;
	gb_connection_set_data(connection, gb_i2c_dev);
	gb_i2c_dev->gbphy_dev = gbphy_dev;
	gb_gbphy_set_data(gbphy_dev, gb_i2c_dev);

	ret = gb_connection_enable(connection);
	if (ret)
		goto exit_connection_destroy;

	ret = gb_i2c_device_setup(gb_i2c_dev);
	if (ret)
		goto exit_connection_disable;

	/* Looks good; up our i2c adapter */
	adapter = &gb_i2c_dev->adapter;
	adapter->owner = THIS_MODULE;
	adapter->class = I2C_CLASS_HWMON | I2C_CLASS_SPD;
	adapter->algo = &gb_i2c_algorithm;
	/* adapter->algo_data = what? */

	adapter->dev.parent = &gbphy_dev->dev;
	snprintf(adapter->name, sizeof(adapter->name), "Greybus i2c adapter");
	i2c_set_adapdata(adapter, gb_i2c_dev);

	ret = i2c_add_adapter(adapter);
	if (ret)
		goto exit_connection_disable;

	gbphy_runtime_put_autosuspend(gbphy_dev);
	return 0;

exit_connection_disable:
	gb_connection_disable(connection);
exit_connection_destroy:
	gb_connection_destroy(connection);
exit_i2cdev_free:
	kfree(gb_i2c_dev);

	return ret;
}

static void gb_i2c_remove(struct gbphy_device *gbphy_dev)
{
	struct gb_i2c_device *gb_i2c_dev = gb_gbphy_get_data(gbphy_dev);
	struct gb_connection *connection = gb_i2c_dev->connection;
	int ret;

	ret = gbphy_runtime_get_sync(gbphy_dev);
	if (ret)
		gbphy_runtime_get_noresume(gbphy_dev);

	i2c_del_adapter(&gb_i2c_dev->adapter);
	gb_connection_disable(connection);
	gb_connection_destroy(connection);
	kfree(gb_i2c_dev);
}

static const struct gbphy_device_id gb_i2c_id_table[] = {
	{ GBPHY_PROTOCOL(GREYBUS_PROTOCOL_I2C) },
	{ },
};
MODULE_DEVICE_TABLE(gbphy, gb_i2c_id_table);

static struct gbphy_driver i2c_driver = {
	.name		= "i2c",
	.probe		= gb_i2c_probe,
	.remove		= gb_i2c_remove,
	.id_table	= gb_i2c_id_table,
};

module_gbphy_driver(i2c_driver);
MODULE_LICENSE("GPL v2");

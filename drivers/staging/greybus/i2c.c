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

struct gb_i2c_device {
	struct gb_connection	*connection;

	u32			functionality;
	u16			timeout_msec;
	u8			retries;

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

static int gb_i2c_timeout_operation(struct gb_i2c_device *gb_i2c_dev, u16 msec)
{
	struct gb_i2c_timeout_request request;
	int ret;

	request.msec = cpu_to_le16(msec);
	ret = gb_operation_sync(gb_i2c_dev->connection, GB_I2C_TYPE_TIMEOUT,
				&request, sizeof(request), NULL, 0);
	if (ret)
		pr_err("timeout operation failed (%d)\n", ret);
	else
		gb_i2c_dev->timeout_msec = msec;

	return ret;
}

static int gb_i2c_retries_operation(struct gb_i2c_device *gb_i2c_dev,
				u8 retries)
{
	struct gb_i2c_retries_request request;
	int ret;

	request.retries = retries;
	ret = gb_operation_sync(gb_i2c_dev->connection, GB_I2C_TYPE_RETRIES,
				&request, sizeof(request), NULL, 0);
	if (ret)
		pr_err("retries operation failed (%d)\n", ret);
	else
		gb_i2c_dev->retries = retries;

	return ret;
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
		dev_err(&connection->bundle->dev, "msg_count (%u) too big\n",
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
	struct gb_operation *operation;
	int ret;

	operation = gb_i2c_operation_create(connection, msgs, msg_count);
	if (!operation)
		return -ENOMEM;

	ret = gb_operation_request_send_sync(operation);
	if (!ret) {
		struct gb_i2c_transfer_response *response;

		response = operation->response->payload;
		gb_i2c_decode_response(msgs, msg_count, response);
		ret = msg_count;
	} else if (!gb_i2c_expected_transfer_error(ret)) {
		pr_err("transfer operation failed (%d)\n", ret);
	}

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
 * If that's OK, we get and cached its functionality bits, and
 * set up the retry count and timeout.
 *
 * Note: gb_i2c_dev->connection is assumed to have been valid.
 */
static int gb_i2c_device_setup(struct gb_i2c_device *gb_i2c_dev)
{
	int ret;

	/* Assume the functionality never changes, just get it once */
	ret = gb_i2c_functionality_operation(gb_i2c_dev);
	if (ret)
		return ret;

	/* Set up our default retry count and timeout */
	ret = gb_i2c_retries_operation(gb_i2c_dev, GB_I2C_RETRIES_DEFAULT);
	if (ret)
		return ret;

	return gb_i2c_timeout_operation(gb_i2c_dev, GB_I2C_TIMEOUT_DEFAULT);
}

static int gb_i2c_connection_init(struct gb_connection *connection)
{
	struct gb_i2c_device *gb_i2c_dev;
	struct i2c_adapter *adapter;
	int ret;

	gb_i2c_dev = kzalloc(sizeof(*gb_i2c_dev), GFP_KERNEL);
	if (!gb_i2c_dev)
		return -ENOMEM;

	gb_i2c_dev->connection = connection;	/* refcount? */
	connection->private = gb_i2c_dev;

	ret = gb_i2c_device_setup(gb_i2c_dev);
	if (ret)
		goto out_err;

	/* Looks good; up our i2c adapter */
	adapter = &gb_i2c_dev->adapter;
	adapter->owner = THIS_MODULE;
	adapter->class = I2C_CLASS_HWMON | I2C_CLASS_SPD;
	adapter->algo = &gb_i2c_algorithm;
	/* adapter->algo_data = what? */
	adapter->timeout = gb_i2c_dev->timeout_msec * HZ / 1000;
	adapter->retries = gb_i2c_dev->retries;

	adapter->dev.parent = &connection->bundle->dev;
	snprintf(adapter->name, sizeof(adapter->name), "Greybus i2c adapter");
	i2c_set_adapdata(adapter, gb_i2c_dev);

	ret = i2c_add_adapter(adapter);
	if (ret)
		goto out_err;

	return 0;
out_err:
	/* kref_put(gb_i2c_dev->connection) */
	kfree(gb_i2c_dev);

	return ret;
}

static void gb_i2c_connection_exit(struct gb_connection *connection)
{
	struct gb_i2c_device *gb_i2c_dev = connection->private;

	i2c_del_adapter(&gb_i2c_dev->adapter);
	/* kref_put(gb_i2c_dev->connection) */
	kfree(gb_i2c_dev);
}

static struct gb_protocol i2c_protocol = {
	.name			= "i2c",
	.id			= GREYBUS_PROTOCOL_I2C,
	.major			= GB_I2C_VERSION_MAJOR,
	.minor			= GB_I2C_VERSION_MINOR,
	.connection_init	= gb_i2c_connection_init,
	.connection_exit	= gb_i2c_connection_exit,
	.request_recv		= NULL,	/* no incoming requests */
};

gb_builtin_protocol_driver(i2c_protocol);

/*
 * I2C bridge driver for the Greybus "generic" I2C module.
 *
 * Copyright 2014 Google Inc.
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
	u8			version_major;
	u8			version_minor;

	u32			functionality;
	u16			timeout_msec;
	u8			retries;

	struct i2c_adapter	*adapter;
};

/* Version of the Greybus i2c protocol we support */
#define	GB_I2C_VERSION_MAJOR		0x00
#define	GB_I2C_VERSION_MINOR		0x01

/* Greybus i2c request types */
#define	GB_I2C_TYPE_INVALID		0x00
#define	GB_I2C_TYPE_PROTOCOL_VERSION	0x01
#define	GB_I2C_TYPE_FUNCTIONALITY	0x02
#define	GB_I2C_TYPE_TIMEOUT		0x03
#define	GB_I2C_TYPE_RETRIES		0x04
#define	GB_I2C_TYPE_TRANSFER		0x05
#define	GB_I2C_TYPE_RESPONSE		0x80	/* OR'd with rest */

#define	GB_I2C_RETRIES_DEFAULT		3
#define	GB_I2C_TIMEOUT_DEFAULT		1000	/* milliseconds */

/* version request has no payload */
struct gb_i2c_proto_version_response {
	__u8	status;
	__u8	major;
	__u8	minor;
};

/* functionality request has no payload */
struct gb_i2c_functionality_response {
	__u8	status;
	__le32	functionality;
};

struct gb_i2c_timeout_request {
	__le16	msec;
};
struct gb_i2c_timeout_response {
	__u8	status;
};

struct gb_i2c_retries_request {
	__u8	retries;
};
struct gb_i2c_retries_response {
	__u8	status;
};

/*
 * Outgoing data immediately follows the op count and ops array.
 * The data for each write (master -> slave) op in the array is sent
 * in order, with no (e.g. pad) bytes separating them.
 *
 * Short reads cause the entire transfer request to fail So response
 * payload consists only of bytes read, and the number of bytes is
 * exactly what was specified in the corresponding op.  Like
 * outgoing data, the incoming data is in order and contiguous.
 */
struct gb_i2c_transfer_op {
	__le16	addr;
	__le16	flags;
	__le16	size;
};

struct gb_i2c_transfer_request {
	__le16				op_count;
	struct gb_i2c_transfer_op	ops[0];		/* op_count of these */
};
struct gb_i2c_transfer_response {
	__u8				status;
	__u8				data[0];	/* inbound data */
};

/*
 * This request only uses the connection field, and if successful,
 * fills in the major and minor protocol version of the target.
 */
static int gb_i2c_proto_version_operation(struct gb_i2c_device *gb_i2c_dev)
{
	struct gb_connection *connection = gb_i2c_dev->connection;
	struct gb_operation *operation;
	struct gb_i2c_proto_version_response *response;
	int ret;

	/* A protocol version request has no payload */
	operation = gb_operation_create(connection,
					GB_I2C_TYPE_PROTOCOL_VERSION,
					0, sizeof(*response));
	if (!operation)
		return -ENOMEM;

	/* Synchronous operation--no callback */
	ret = gb_operation_request_send(operation, NULL);
	if (ret) {
		pr_err("version operation failed (%d)\n", ret);
		goto out;
	}

	response = operation->response_payload;
	if (response->status) {
		gb_connection_err(connection, "version response %hhu",
			response->status);
		ret = -EIO;
	} else {
		if (response->major > GB_I2C_VERSION_MAJOR) {
			pr_err("unsupported major version (%hhu > %hhu)\n",
				response->major, GB_I2C_VERSION_MAJOR);
			ret = -ENOTSUPP;
			goto out;
		}
		gb_i2c_dev->version_major = response->major;
		gb_i2c_dev->version_minor = response->minor;
	}
out:

	gb_operation_destroy(operation);

	return ret;
}

/*
 * Map Greybus i2c functionality bits into Linux ones
 */
static u32 gb_i2c_functionality_map(u32 gb_i2c_functionality)
{
	return gb_i2c_functionality;	/* All bits the same for now */
}

static int gb_i2c_functionality_operation(struct gb_i2c_device *gb_i2c_dev)
{
	struct gb_connection *connection = gb_i2c_dev->connection;
	struct gb_operation *operation;
	struct gb_i2c_functionality_response *response;
	u32 functionality;
	int ret;

	/* A functionality request has no payload */
	operation = gb_operation_create(connection,
					GB_I2C_TYPE_FUNCTIONALITY,
					0, sizeof(*response));
	if (!operation)
		return -ENOMEM;

	/* Synchronous operation--no callback */
	ret = gb_operation_request_send(operation, NULL);
	if (ret) {
		pr_err("functionality operation failed (%d)\n", ret);
		goto out;
	}

	response = operation->response_payload;
	if (response->status) {
		gb_connection_err(connection, "functionality response %hhu",
			response->status);
		ret = -EIO;
	} else {
		functionality = le32_to_cpu(response->functionality);
		gb_i2c_dev->functionality =
			gb_i2c_functionality_map(functionality);
	}
out:
	gb_operation_destroy(operation);

	return ret;
}

static int gb_i2c_timeout_operation(struct gb_i2c_device *gb_i2c_dev, u16 msec)
{
	struct gb_connection *connection = gb_i2c_dev->connection;
	struct gb_operation *operation;
	struct gb_i2c_timeout_request *request;
	struct gb_i2c_timeout_response *response;
	int ret;

	operation = gb_operation_create(connection, GB_I2C_TYPE_TIMEOUT,
					sizeof(*request), sizeof(*response));
	if (!operation)
		return -ENOMEM;
	request = operation->request_payload;
	request->msec = cpu_to_le16(msec);

	/* Synchronous operation--no callback */
	ret = gb_operation_request_send(operation, NULL);
	if (ret) {
		pr_err("timeout operation failed (%d)\n", ret);
		goto out;
	}

	response = operation->response_payload;
	if (response->status) {
		gb_connection_err(connection, "timeout response %hhu",
			response->status);
		ret = -EIO;
	} else {
		gb_i2c_dev->timeout_msec = msec;
	}
out:
	gb_operation_destroy(operation);

	return ret;
}

static int gb_i2c_retries_operation(struct gb_i2c_device *gb_i2c_dev,
				u8 retries)
{
	struct gb_connection *connection = gb_i2c_dev->connection;
	struct gb_operation *operation;
	struct gb_i2c_retries_request *request;
	struct gb_i2c_retries_response *response;
	int ret;

	operation = gb_operation_create(connection, GB_I2C_TYPE_RETRIES,
					sizeof(*request), sizeof(*response));
	if (!operation)
		return -ENOMEM;
	request = operation->request_payload;
	request->retries = retries;

	/* Synchronous operation--no callback */
	ret = gb_operation_request_send(operation, NULL);
	if (ret) {
		pr_err("retries operation failed (%d)\n", ret);
		goto out;
	}

	response = operation->response_payload;
	if (response->status) {
		gb_connection_err(connection, "retries response %hhu",
			response->status);
		ret = -EIO;
	} else {
		gb_i2c_dev->retries = retries;
	}
out:
	gb_operation_destroy(operation);

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
gb_i2c_transfer_request(struct gb_connection *connection,
				struct i2c_msg *msgs, u32 msg_count)
{
	struct gb_i2c_transfer_request *request;
	struct gb_operation *operation;
	struct gb_i2c_transfer_op *op;
	struct i2c_msg *msg;
	u32 data_out_size = 0;
	u32 data_in_size = 1;	/* Response begins with a status byte */
	size_t request_size;
	void *data;
	u16 op_count;
	u32 i;

	if (msg_count > (u32)U16_MAX) {
		gb_connection_err(connection, "msg_count (%u) too big",
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

	request_size = sizeof(struct gb_i2c_transfer_request);
	request_size += msg_count * sizeof(struct gb_i2c_transfer_op);
	request_size += data_out_size;

	/* Response consists only of incoming data */
	operation = gb_operation_create(connection, GB_I2C_TYPE_TRANSFER,
				request_size, data_in_size);
	if (!operation)
		return NULL;

	request = operation->request_payload;
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

static void gb_i2c_transfer_response(struct i2c_msg *msgs, u32 msg_count,
					void *data)
{
	struct i2c_msg *msg = msgs;
	u32 i;

	for (i = 0; i < msg_count; i++) {
		if (msg->flags & I2C_M_RD) {
			memcpy(msg->buf, data, msg->len);
			data += msg->len;
		}
		msg++;
	}
}

static int gb_i2c_transfer_operation(struct gb_i2c_device *gb_i2c_dev,
					struct i2c_msg *msgs, u32 msg_count)
{
	struct gb_connection *connection = gb_i2c_dev->connection;
	struct gb_i2c_transfer_response *response;
	struct gb_operation *operation;
	int ret;

	operation = gb_i2c_transfer_request(connection, msgs, msg_count);
	if (!operation)
		return -ENOMEM;

	/* Synchronous operation--no callback */
	ret = gb_operation_request_send(operation, NULL);
	if (ret) {
		pr_err("transfer operation failed (%d)\n", ret);
		goto out;
	}

	response = operation->response_payload;
	if (response->status) {
		if (response->status == GB_OP_RETRY) {
			ret = -EAGAIN;
		} else {
			gb_connection_err(connection, "transfer response %hhu",
				response->status);
			ret = -EIO;
		}
	} else {
		gb_i2c_transfer_response(msgs, msg_count, response->data);
		ret = msg_count;
	}
out:
	gb_operation_destroy(operation);

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

	/* First thing we need to do is check the version */
	ret = gb_i2c_proto_version_operation(gb_i2c_dev);
	if (ret)
		return ret;

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

int gb_i2c_device_init(struct gb_connection *connection)
{
	struct gb_i2c_device *gb_i2c_dev;
	struct i2c_adapter *adapter = NULL;
	int ret;

	gb_i2c_dev = kzalloc(sizeof(*gb_i2c_dev), GFP_KERNEL);
	if (!gb_i2c_dev)
		return -ENOMEM;

	gb_i2c_dev->connection = connection;	/* refcount? */

	ret = gb_i2c_device_setup(gb_i2c_dev);
	if (ret)
		goto out_err;

	/* Looks good; allocate and set up our i2c adapter */
	adapter = kzalloc(sizeof(*adapter), GFP_KERNEL);
	if (!adapter) {
		ret = -ENOMEM;
		goto out_err;
	}

	adapter->owner = THIS_MODULE;
	adapter->class = I2C_CLASS_HWMON | I2C_CLASS_SPD;
	adapter->algo = &gb_i2c_algorithm;
	/* adapter->algo_data = what? */
	adapter->timeout = gb_i2c_dev->timeout_msec * HZ / 1000;
	adapter->retries = gb_i2c_dev->retries;

	/* XXX I think this parent device is wrong, but it uses existing code */
	adapter->dev.parent = &connection->interface->gmod->dev;
	snprintf(adapter->name, sizeof(adapter->name), "Greybus i2c adapter");
	i2c_set_adapdata(adapter, gb_i2c_dev);

	ret = i2c_add_adapter(adapter);
	if (ret)
		goto out_err;

	gb_i2c_dev->adapter = adapter;
	connection->private = gb_i2c_dev;

	return 0;
out_err:
	kfree(adapter);
	/* kref_put(gb_i2c_dev->connection) */
	kfree(gb_i2c_dev);

	return ret;
}

void gb_i2c_device_exit(struct gb_connection *connection)
{
	struct gb_i2c_device *gb_i2c_dev = connection->private;

	i2c_del_adapter(gb_i2c_dev->adapter);
	kfree(gb_i2c_dev->adapter);
	/* kref_put(gb_i2c_dev->connection) */
	kfree(gb_i2c_dev);
}

#if 0
module_greybus_driver(i2c_gb_driver);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Greg Kroah-Hartman <gregkh@linuxfoundation.org>");
#endif

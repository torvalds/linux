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

	struct i2c_adapter	adapter;
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
	__u8	major;
	__u8	minor;
};

/* functionality request has no payload */
struct gb_i2c_functionality_response {
	__le32	functionality;
};

struct gb_i2c_timeout_request {
	__le16	msec;
};
/* timeout response has no payload */

struct gb_i2c_retries_request {
	__u8	retries;
};
/* retries response has no payload */

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
	__u8				data[0];	/* inbound data */
};

/*
 * This request only uses the connection field, and if successful,
 * fills in the major and minor protocol version of the target.
 */
static int gb_i2c_proto_version_operation(struct gb_i2c_device *gb_i2c_dev)
{
	struct gb_i2c_proto_version_response response;
	int ret;

	ret = gb_operation_sync(gb_i2c_dev->connection,
				GB_I2C_TYPE_PROTOCOL_VERSION,
				NULL, 0, &response, sizeof(response));
	if (ret)
		return ret;

	if (response.major > GB_I2C_VERSION_MAJOR) {
		pr_err("unsupported major version (%hhu > %hhu)\n",
			response.major, GB_I2C_VERSION_MAJOR);
		return -ENOTSUPP;
	}
	gb_i2c_dev->version_major = response.major;
	gb_i2c_dev->version_minor = response.minor;
	return 0;
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

	request_size = sizeof(*request);
	request_size += msg_count * sizeof(*op);
	request_size += data_out_size;

	/* Response consists only of incoming data */
	operation = gb_operation_create(connection, GB_I2C_TYPE_TRANSFER,
				request_size, data_in_size);
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

static void gb_i2c_transfer_response(struct i2c_msg *msgs, u32 msg_count,
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

static int gb_i2c_transfer_operation(struct gb_i2c_device *gb_i2c_dev,
					struct i2c_msg *msgs, u32 msg_count)
{
	struct gb_connection *connection = gb_i2c_dev->connection;
	struct gb_operation *operation;
	int ret;

	operation = gb_i2c_transfer_request(connection, msgs, msg_count);
	if (!operation)
		return -ENOMEM;

	ret = gb_operation_request_send_sync(operation);
	if (!ret) {
		struct gb_i2c_transfer_response *response;

		response = operation->response->payload;
		gb_i2c_transfer_response(msgs, msg_count, response);
		ret = msg_count;
	} else if (ret != -EAGAIN) {
		pr_err("transfer operation failed (%d)\n", ret);
	}
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

	adapter->dev.parent = &connection->dev;
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
	.id			= GREYBUS_PROTOCOL_I2C,
	.major			= 0,
	.minor			= 1,
	.connection_init	= gb_i2c_connection_init,
	.connection_exit	= gb_i2c_connection_exit,
	.request_recv		= NULL,	/* no incoming requests */
};

bool gb_i2c_protocol_init(void)
{
	return gb_protocol_register(&i2c_protocol);
}

void gb_i2c_protocol_exit(void)
{
	gb_protocol_deregister(&i2c_protocol);
}

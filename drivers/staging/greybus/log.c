// SPDX-License-Identifier: GPL-2.0
/*
 * Greybus driver for the log protocol
 *
 * Copyright 2016 Google Inc.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/sizes.h>
#include <linux/uaccess.h>
#include <linux/greybus.h>

struct gb_log {
	struct gb_connection *connection;
};

static int gb_log_request_handler(struct gb_operation *op)
{
	struct gb_connection *connection = op->connection;
	struct device *dev = &connection->bundle->dev;
	struct gb_log_send_log_request *receive;
	u16 len;

	if (op->type != GB_LOG_TYPE_SEND_LOG) {
		dev_err(dev, "unknown request type 0x%02x\n", op->type);
		return -EINVAL;
	}

	/* Verify size of payload */
	if (op->request->payload_size < sizeof(*receive)) {
		dev_err(dev, "log request too small (%zu < %zu)\n",
			op->request->payload_size, sizeof(*receive));
		return -EINVAL;
	}
	receive = op->request->payload;
	len = le16_to_cpu(receive->len);
	if (len != (op->request->payload_size - sizeof(*receive))) {
		dev_err(dev, "log request wrong size %d vs %zu\n", len,
			(op->request->payload_size - sizeof(*receive)));
		return -EINVAL;
	}
	if (len == 0) {
		dev_err(dev, "log request of 0 bytes?\n");
		return -EINVAL;
	}

	if (len > GB_LOG_MAX_LEN) {
		dev_err(dev, "log request too big: %d\n", len);
		return -EINVAL;
	}

	/* Ensure the buffer is 0 terminated */
	receive->msg[len - 1] = '\0';

	/*
	 * Print with dev_dbg() so that it can be easily turned off using
	 * dynamic debugging (and prevent any DoS)
	 */
	dev_dbg(dev, "%s", receive->msg);

	return 0;
}

static int gb_log_probe(struct gb_bundle *bundle,
			const struct greybus_bundle_id *id)
{
	struct greybus_descriptor_cport *cport_desc;
	struct gb_connection *connection;
	struct gb_log *log;
	int retval;

	if (bundle->num_cports != 1)
		return -ENODEV;

	cport_desc = &bundle->cport_desc[0];
	if (cport_desc->protocol_id != GREYBUS_PROTOCOL_LOG)
		return -ENODEV;

	log = kzalloc(sizeof(*log), GFP_KERNEL);
	if (!log)
		return -ENOMEM;

	connection = gb_connection_create(bundle, le16_to_cpu(cport_desc->id),
					  gb_log_request_handler);
	if (IS_ERR(connection)) {
		retval = PTR_ERR(connection);
		goto error_free;
	}

	log->connection = connection;
	greybus_set_drvdata(bundle, log);

	retval = gb_connection_enable(connection);
	if (retval)
		goto error_connection_destroy;

	return 0;

error_connection_destroy:
	gb_connection_destroy(connection);
error_free:
	kfree(log);
	return retval;
}

static void gb_log_disconnect(struct gb_bundle *bundle)
{
	struct gb_log *log = greybus_get_drvdata(bundle);
	struct gb_connection *connection = log->connection;

	gb_connection_disable(connection);
	gb_connection_destroy(connection);

	kfree(log);
}

static const struct greybus_bundle_id gb_log_id_table[] = {
	{ GREYBUS_DEVICE_CLASS(GREYBUS_CLASS_LOG) },
	{ }
};
MODULE_DEVICE_TABLE(greybus, gb_log_id_table);

static struct greybus_driver gb_log_driver = {
	.name           = "log",
	.probe          = gb_log_probe,
	.disconnect     = gb_log_disconnect,
	.id_table       = gb_log_id_table,
};
module_greybus_driver(gb_log_driver);

MODULE_DESCRIPTION("Greybus driver for the log protocol");
MODULE_LICENSE("GPL v2");

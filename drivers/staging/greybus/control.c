/*
 * Greybus CPort control protocol.
 *
 * Copyright 2015 Google Inc.
 * Copyright 2015 Linaro Ltd.
 *
 * Released under the GPLv2 only.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include "greybus.h"

/* Define get_version() routine */
define_get_version(gb_control, CONTROL);

/* Get Manifest's size from the interface */
int gb_control_get_manifest_size_operation(struct gb_interface *intf)
{
	struct gb_control_get_manifest_size_response response;
	struct gb_connection *connection = intf->control->connection;
	int ret;

	ret = gb_operation_sync(connection, GB_CONTROL_TYPE_GET_MANIFEST_SIZE,
				NULL, 0, &response, sizeof(response));
	if (ret) {
		dev_err(&connection->dev,
			"%s: Manifest size get operation failed (%d)\n",
			__func__, ret);
		return ret;
	}

	return le16_to_cpu(response.size);
}

/* Reads Manifest from the interface */
int gb_control_get_manifest_operation(struct gb_interface *intf, void *manifest,
				      size_t size)
{
	struct gb_connection *connection = intf->control->connection;

	return gb_operation_sync(connection, GB_CONTROL_TYPE_GET_MANIFEST,
				NULL, 0, manifest, size);
}

int gb_control_connected_operation(struct gb_control *control, u16 cport_id)
{
	struct gb_control_connected_request request;

	request.cport_id = cpu_to_le16(cport_id);
	return gb_operation_sync(control->connection, GB_CONTROL_TYPE_CONNECTED,
				 &request, sizeof(request), NULL, 0);
}

int gb_control_disconnected_operation(struct gb_control *control, u16 cport_id)
{
	struct gb_control_disconnected_request request;

	request.cport_id = cpu_to_le16(cport_id);
	return gb_operation_sync(control->connection,
				 GB_CONTROL_TYPE_DISCONNECTED, &request,
				 sizeof(request), NULL, 0);
}

static int gb_control_request_recv(u8 type, struct gb_operation *op)
{
	struct gb_connection *connection = op->connection;
	struct gb_protocol_version_response *version;

	switch (type) {
	case GB_CONTROL_TYPE_PROBE_AP:
		// TODO
		// Send authenticated block of data, confirming this module is
		// an AP.
		break;
	case GB_CONTROL_TYPE_PROTOCOL_VERSION:
		if (!gb_operation_response_alloc(op, sizeof(*version))) {
			dev_err(&connection->dev,
				"%s: error allocating response\n", __func__);
			return -ENOMEM;
		}

		version = op->response->payload;
		version->major = GB_CONTROL_VERSION_MAJOR;
		version->minor = GB_CONTROL_VERSION_MINOR;
		break;
	case GB_CONTROL_TYPE_CONNECTED:
	case GB_CONTROL_TYPE_DISCONNECTED:
		break;
	default:
		WARN_ON(1);
		break;
	}

	return 0;
}

static int gb_control_connection_init(struct gb_connection *connection)
{
	struct gb_control *control;
	int ret;

	control = kzalloc(sizeof(*control), GFP_KERNEL);
	if (!control)
		return -ENOMEM;

	control->connection = connection;
	connection->private = control;

	ret = get_version(control);
	if (ret)
		kfree(control);

	/* Set interface's control connection */
	connection->bundle->intf->control = control;

	return ret;
}

static void gb_control_connection_exit(struct gb_connection *connection)
{
	struct gb_control *control = connection->private;

	if (WARN_ON(connection->bundle->intf->control != control))
		return;

	connection->bundle->intf->control = NULL;
	kfree(control);
}

static struct gb_protocol control_protocol = {
	.name			= "control",
	.id			= GREYBUS_PROTOCOL_CONTROL,
	.major			= 0,
	.minor			= 1,
	.connection_init	= gb_control_connection_init,
	.connection_exit	= gb_control_connection_exit,
	.request_recv		= gb_control_request_recv,
};

int gb_control_protocol_init(void)
{
	return gb_protocol_register(&control_protocol);
}

void gb_control_protocol_exit(void)
{
	gb_protocol_deregister(&control_protocol);
}

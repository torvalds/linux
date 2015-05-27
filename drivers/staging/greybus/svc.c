/*
 * SVC Greybus driver.
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
#include "greybus_protocols.h"

struct gb_svc {
	struct gb_connection	*connection;
	u8			version_major;
	u8			version_minor;
};

/* Define get_version() routine */
define_get_version(gb_svc, SVC);

static int intf_device_id_operation(struct gb_svc *svc,
				u8 intf_id, u8 device_id)
{
	struct gb_svc_intf_device_id_request request;

	request.intf_id = intf_id;
	request.device_id = device_id;

	return gb_operation_sync(svc->connection, GB_SVC_TYPE_INTF_DEVICE_ID,
				 &request, sizeof(request), NULL, 0);
}

static int intf_reset_operation(struct gb_svc *svc, u8 intf_id)
{
	struct gb_svc_intf_reset_request request;

	request.intf_id = intf_id;

	return gb_operation_sync(svc->connection, GB_SVC_TYPE_INTF_RESET,
				 &request, sizeof(request), NULL, 0);
}

static int connection_create_operation(struct gb_svc *svc,
				u8 intf1_id, u16 cport1_id,
				u8 intf2_id, u16 cport2_id)
{
	struct gb_svc_conn_create_request request;

	request.intf1_id = intf1_id;
	request.cport1_id = cport1_id;
	request.intf2_id = intf2_id;
	request.cport2_id = cport2_id;

	return gb_operation_sync(svc->connection, GB_SVC_TYPE_CONN_CREATE,
				 &request, sizeof(request), NULL, 0);
}

static int connection_destroy_operation(struct gb_svc *svc,
				u8 intf1_id, u16 cport1_id,
				u8 intf2_id, u16 cport2_id)
{
	struct gb_svc_conn_destroy_request request;

	request.intf1_id = intf1_id;
	request.cport1_id = cport1_id;
	request.intf2_id = intf2_id;
	request.cport2_id = cport2_id;

	return gb_operation_sync(svc->connection, GB_SVC_TYPE_CONN_DESTROY,
				 &request, sizeof(request), NULL, 0);
}

int gb_svc_intf_device_id(struct gb_svc *svc, u8 intf_id, u8 device_id)
{
	return intf_device_id_operation(svc, intf_id, device_id);
}
EXPORT_SYMBOL_GPL(gb_svc_intf_device_id);

int gb_svc_intf_reset(struct gb_svc *svc, u8 intf_id)
{
	return intf_reset_operation(svc, intf_id);
}
EXPORT_SYMBOL_GPL(gb_svc_intf_reset);

int gb_svc_connection_create(struct gb_svc *svc,
				u8 intf1_id, u16 cport1_id,
				u8 intf2_id, u16 cport2_id)
{
	return connection_create_operation(svc, intf1_id, cport1_id,
						intf2_id, cport2_id);
}
EXPORT_SYMBOL_GPL(gb_svc_connection_create);

int gb_svc_connection_destroy(struct gb_svc *svc,
				u8 intf1_id, u16 cport1_id,
				u8 intf2_id, u16 cport2_id)
{
	return connection_destroy_operation(svc, intf1_id, cport1_id,
						intf2_id, cport2_id);
}
EXPORT_SYMBOL_GPL(gb_svc_connection_destroy);

static int gb_svc_intf_hotplug_recv(struct gb_operation *op)
{
	struct gb_message *request = op->request;
	struct gb_svc_intf_hotplug_request *hotplug;
	u8 intf_id;
	u32 unipro_mfg_id;
	u32 unipro_prod_id;
	u32 ara_vend_id;
	u32 ara_prod_id;

	if (request->payload_size < sizeof(*hotplug)) {
		dev_err(&op->connection->dev,
			"short hotplug request received\n");
		return -EINVAL;
	}
	hotplug = request->payload;

	/*
	 * Grab the information we need.
	 *
	 * XXX I'd really like to acknowledge receipt, and then
	 * XXX continue processing the request.  There's no need
	 * XXX for the SVC to wait.  In fact, it might be best to
	 * XXX have the SVC get acknowledgement before we proceed.
	 * */
	intf_id = hotplug->intf_id;
	unipro_mfg_id = le32_to_cpu(hotplug->data.unipro_mfg_id);
	unipro_prod_id = le32_to_cpu(hotplug->data.unipro_prod_id);
	ara_vend_id = le32_to_cpu(hotplug->data.ara_vend_id);
	ara_prod_id = le32_to_cpu(hotplug->data.ara_prod_id);

	/* FIXME Set up the interface here; may required firmware download */

	return 0;
}

static int gb_svc_intf_hot_unplug_recv(struct gb_operation *op)
{
	struct gb_message *request = op->request;
	struct gb_svc_intf_hot_unplug_request *hot_unplug;
	u8 intf_id;

	if (request->payload_size < sizeof(*hot_unplug)) {
		dev_err(&op->connection->dev,
			"short hot unplug request received\n");
		return -EINVAL;
	}
	hot_unplug = request->payload;

	intf_id = hot_unplug->intf_id;

	/* FIXME Tear down the interface here */

	return 0;

}

static int gb_svc_intf_reset_recv(struct gb_operation *op)
{
	struct gb_message *request = op->request;
	struct gb_svc_intf_reset_request *reset;
	u8 intf_id;

	if (request->payload_size < sizeof(*reset)) {
		dev_err(&op->connection->dev,
			"short reset request received\n");
		return -EINVAL;
	}
	reset = request->payload;

	intf_id = reset->intf_id;

	/* FIXME Reset the interface here */

	return 0;
}

static int gb_svc_request_recv(u8 type, struct gb_operation *op)
{
	switch (type) {
	case GB_SVC_TYPE_INTF_HOTPLUG:
		return gb_svc_intf_hotplug_recv(op);
	case GB_SVC_TYPE_INTF_HOT_UNPLUG:
		return gb_svc_intf_hot_unplug_recv(op);
	case GB_SVC_TYPE_INTF_RESET:
		return gb_svc_intf_reset_recv(op);
	default:
		dev_err(&op->connection->dev,
			"unsupported request: %hhu\n", type);
		return -EINVAL;
	}
}

/*
 * Do initial setup of the SVC.
 */
static int gb_svc_device_setup(struct gb_svc *gb_svc)
{
	/* First thing we need to do is check the version */
	return get_version(gb_svc);
}

static int gb_svc_connection_init(struct gb_connection *connection)
{
	struct gb_svc *svc;
	int ret;

	svc = kzalloc(sizeof(*svc), GFP_KERNEL);
	if (!svc)
		return -ENOMEM;

	svc->connection = connection;
	connection->private = svc;
	ret = gb_svc_device_setup(svc);
	if (ret)
		kfree(svc);

	return ret;
}

static void gb_svc_connection_exit(struct gb_connection *connection)
{
	struct gb_svc *svc = connection->private;

	if (!svc)
		return;

	kfree(svc);
}

static struct gb_protocol svc_protocol = {
	.name			= "svc",
	.id			= GREYBUS_PROTOCOL_SVC,
	.major			= 0,
	.minor			= 1,
	.connection_init	= gb_svc_connection_init,
	.connection_exit	= gb_svc_connection_exit,
	.request_recv		= gb_svc_request_recv,
};

gb_gpbridge_protocol_driver(svc_protocol);

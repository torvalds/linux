/*
 * Greybus Host Device
 *
 * Copyright 2014-2015 Google Inc.
 * Copyright 2014-2015 Linaro Ltd.
 *
 * Released under the GPLv2 only.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/slab.h>

#include "greybus.h"

static DEFINE_MUTEX(hd_mutex);


static void free_hd(struct kref *kref)
{
	struct gb_host_device *hd;

	hd = container_of(kref, struct gb_host_device, kref);

	ida_destroy(&hd->cport_id_map);
	kfree(hd);
	mutex_unlock(&hd_mutex);
}

struct gb_host_device *gb_hd_create(struct gb_hd_driver *driver,
					struct device *parent,
					size_t buffer_size_max,
					size_t num_cports)
{
	struct gb_host_device *hd;

	/*
	 * Validate that the driver implements all of the callbacks
	 * so that we don't have to every time we make them.
	 */
	if ((!driver->message_send) || (!driver->message_cancel)) {
		pr_err("Must implement all gb_hd_driver callbacks!\n");
		return ERR_PTR(-EINVAL);
	}

	if (buffer_size_max < GB_OPERATION_MESSAGE_SIZE_MIN) {
		dev_err(parent, "greybus host-device buffers too small\n");
		return ERR_PTR(-EINVAL);
	}

	if (num_cports == 0 || num_cports > CPORT_ID_MAX) {
		dev_err(parent, "Invalid number of CPorts: %zu\n", num_cports);
		return ERR_PTR(-EINVAL);
	}

	/*
	 * Make sure to never allocate messages larger than what the Greybus
	 * protocol supports.
	 */
	if (buffer_size_max > GB_OPERATION_MESSAGE_SIZE_MAX) {
		dev_warn(parent, "limiting buffer size to %u\n",
			 GB_OPERATION_MESSAGE_SIZE_MAX);
		buffer_size_max = GB_OPERATION_MESSAGE_SIZE_MAX;
	}

	hd = kzalloc(sizeof(*hd) + driver->hd_priv_size, GFP_KERNEL);
	if (!hd)
		return ERR_PTR(-ENOMEM);

	kref_init(&hd->kref);
	hd->parent = parent;
	hd->driver = driver;
	INIT_LIST_HEAD(&hd->interfaces);
	INIT_LIST_HEAD(&hd->connections);
	ida_init(&hd->cport_id_map);
	hd->buffer_size_max = buffer_size_max;
	hd->num_cports = num_cports;

	return hd;
}
EXPORT_SYMBOL_GPL(gb_hd_create);

int gb_hd_add(struct gb_host_device *hd)
{
	/*
	 * Initialize AP's SVC protocol connection:
	 *
	 * This is required as part of early initialization of the host device
	 * as we need this connection in order to start any kind of message
	 * exchange between the AP and the SVC. SVC will start with a
	 * 'get-version' request followed by a 'svc-hello' message and at that
	 * time we will create a fully initialized svc-connection, as we need
	 * endo-id and AP's interface id for that.
	 */
	if (!gb_ap_svc_connection_create(hd))
		return -ENOMEM;

	return 0;
}
EXPORT_SYMBOL_GPL(gb_hd_add);

void gb_hd_del(struct gb_host_device *hd)
{
	/*
	 * Tear down all interfaces, modules, and the endo that is associated
	 * with this host controller before freeing the memory associated with
	 * the host controller.
	 */
	gb_interfaces_remove(hd);
	gb_endo_remove(hd->endo);

	/* Is the SVC still using the partially uninitialized connection ? */
	if (hd->initial_svc_connection)
		gb_connection_destroy(hd->initial_svc_connection);
}
EXPORT_SYMBOL_GPL(gb_hd_del);

void gb_hd_put(struct gb_host_device *hd)
{
	kref_put_mutex(&hd->kref, free_hd, &hd_mutex);
}
EXPORT_SYMBOL_GPL(gb_hd_put);

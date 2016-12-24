/*
 * Greybus Module code
 *
 * Copyright 2016 Google Inc.
 * Copyright 2016 Linaro Ltd.
 *
 * Released under the GPLv2 only.
 */

#ifndef __MODULE_H
#define __MODULE_H

struct gb_module {
	struct device dev;
	struct gb_host_device *hd;

	struct list_head hd_node;

	u8 module_id;
	size_t num_interfaces;

	bool disconnected;

	struct gb_interface *interfaces[0];
};
#define to_gb_module(d) container_of(d, struct gb_module, dev)

struct gb_module *gb_module_create(struct gb_host_device *hd, u8 module_id,
				   size_t num_interfaces);
int gb_module_add(struct gb_module *module);
void gb_module_del(struct gb_module *module);
void gb_module_put(struct gb_module *module);

#endif /* __MODULE_H */

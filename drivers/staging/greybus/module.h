/*
 * Greybus module code
 *
 * Copyright 2014 Google Inc.
 *
 * Released under the GPLv2 only.
 */

#ifndef __MODULE_H
#define __MODULE_H

/* Greybus "public" definitions" */
struct gb_module {
	struct device dev;
	u8 module_id;		/* Physical location within the Endo */
};
#define to_gb_module(d) container_of(d, struct gb_module, dev)

struct greybus_host_device;

/* Greybus "private" definitions */
struct gb_module *gb_module_find(struct greybus_host_device *hd, u8 module_id);
struct gb_module *gb_module_create(struct device *parent, u8 module_id);
void gb_module_remove_all(struct gb_endo *endo);

#endif /* __MODULE_H */

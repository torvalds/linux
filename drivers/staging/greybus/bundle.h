/*
 * Greybus bundles
 *
 * Copyright 2014 Google Inc.
 * Copyright 2014 Linaro Ltd.
 *
 * Released under the GPLv2 only.
 */

#ifndef __BUNDLE_H
#define __BUNDLE_H

#include <linux/list.h>

/* Greybus "public" definitions" */
struct gb_bundle {
	struct device		dev;
	struct gb_interface	*intf;
	u8			id;
	u8			device_id;
	struct list_head	connections;

	struct list_head	links;	/* interface->bundles */
};
#define to_gb_bundle(d) container_of(d, struct gb_bundle, dev)

#define GB_DEVICE_ID_BAD	0xff

/* Greybus "private" definitions" */
struct gb_bundle *gb_bundle_create(struct gb_interface *intf, u8 module_id);
void gb_bundle_destroy(struct gb_interface *intf);
int gb_bundle_init(struct gb_interface *intf, u8 module_id, u8 device_id);

struct gb_bundle *gb_bundle_find(struct gb_interface *intf, u8 bundle_id);
void gb_bundle_bind_protocols(void);

#endif /* __BUNDLE_H */

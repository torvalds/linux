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
	u8			class;
	struct list_head	connections;
	u8			*state;

	struct list_head	links;	/* interface->bundles */
	void			*private;
};
#define to_gb_bundle(d) container_of(d, struct gb_bundle, dev)

#define GB_DEVICE_ID_BAD	0xff

/* Greybus "private" definitions" */
struct gb_bundle *gb_bundle_create(struct gb_interface *intf, u8 bundle_id,
				   u8 class);
int gb_bundle_add(struct gb_bundle *bundle);
void gb_bundle_destroy(struct gb_bundle *bundle);

#endif /* __BUNDLE_H */

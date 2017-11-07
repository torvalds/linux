// SPDX-License-Identifier: GPL-2.0
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

#define	BUNDLE_ID_NONE	U8_MAX

/* Greybus "public" definitions" */
struct gb_bundle {
	struct device		dev;
	struct gb_interface	*intf;

	u8			id;
	u8			class;
	u8			class_major;
	u8			class_minor;

	size_t			num_cports;
	struct greybus_descriptor_cport *cport_desc;

	struct list_head	connections;
	u8			*state;

	struct list_head	links;	/* interface->bundles */
};
#define to_gb_bundle(d) container_of(d, struct gb_bundle, dev)

/* Greybus "private" definitions" */
struct gb_bundle *gb_bundle_create(struct gb_interface *intf, u8 bundle_id,
				   u8 class);
int gb_bundle_add(struct gb_bundle *bundle);
void gb_bundle_destroy(struct gb_bundle *bundle);

/* Bundle Runtime PM wrappers */
#ifdef CONFIG_PM
static inline int gb_pm_runtime_get_sync(struct gb_bundle *bundle)
{
	int retval;

	retval = pm_runtime_get_sync(&bundle->dev);
	if (retval < 0) {
		dev_err(&bundle->dev,
			"pm_runtime_get_sync failed: %d\n", retval);
		pm_runtime_put_noidle(&bundle->dev);
		return retval;
	}

	return 0;
}

static inline int gb_pm_runtime_put_autosuspend(struct gb_bundle *bundle)
{
	int retval;

	pm_runtime_mark_last_busy(&bundle->dev);
	retval = pm_runtime_put_autosuspend(&bundle->dev);

	return retval;
}

static inline void gb_pm_runtime_get_noresume(struct gb_bundle *bundle)
{
	pm_runtime_get_noresume(&bundle->dev);
}

static inline void gb_pm_runtime_put_noidle(struct gb_bundle *bundle)
{
	pm_runtime_put_noidle(&bundle->dev);
}

#else
static inline int gb_pm_runtime_get_sync(struct gb_bundle *bundle)
{ return 0; }
static inline int gb_pm_runtime_put_autosuspend(struct gb_bundle *bundle)
{ return 0; }

static inline void gb_pm_runtime_get_noresume(struct gb_bundle *bundle) {}
static inline void gb_pm_runtime_put_noidle(struct gb_bundle *bundle) {}
#endif

#endif /* __BUNDLE_H */

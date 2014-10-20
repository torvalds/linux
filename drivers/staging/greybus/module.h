/*
 * Greybus modules
 *
 * Copyright 2014 Google Inc.
 *
 * Released under the GPLv2 only.
 */

#ifndef __MODULE_H
#define __MODULE_H

/* Increase these values if needed */
#define MAX_CPORTS_PER_MODULE	10
#define MAX_STRINGS_PER_MODULE	10

struct gb_module {
	struct device dev;

	struct list_head interfaces;
	struct list_head links;	/* greybus_host_device->modules */
	u8 module_id;		/* Physical location within the Endo */

	/* Information taken from the manifest module descriptor */
	u16 vendor;
	u16 product;
	u16 version;
	char *vendor_string;
	char *product_string;
	u64 unique_id;

	struct greybus_host_device *hd;

	struct gb_tty *gb_tty;
};
#define to_gb_module(d) container_of(d, struct gb_module, dev)

static inline void
gb_module_set_drvdata(struct gb_module *gmod, void *data)
{
	dev_set_drvdata(&gmod->dev, data);
}

static inline void *gb_module_get_drvdata(struct gb_module *gmod)
{
	return dev_get_drvdata(&gmod->dev);
}

const struct greybus_module_id *gb_module_match_id(struct gb_module *gmod,
					const struct greybus_module_id *id);

struct gb_module *gb_module_create(struct greybus_host_device *hd,
					u8 module_id);
void gb_module_destroy(struct gb_module *module);

void gb_module_interfaces_init(struct gb_module *gmod);

#endif /* __MODULE_H */

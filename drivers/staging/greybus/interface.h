/*
 * Greybus Interface Block code
 *
 * Copyright 2014 Google Inc.
 * Copyright 2014 Linaro Ltd.
 *
 * Released under the GPLv2 only.
 */

#ifndef __INTERFACE_H
#define __INTERFACE_H

/* Greybus "public" definitions" */
struct gb_interface {
	struct device dev;

	struct list_head bundles;
	struct list_head links;	/* greybus_host_device->modules */
	u8 module_id;		/* Physical location within the Endo */

	/* Information taken from the manifest module descriptor */
	u16 vendor;
	u16 product;
	char *vendor_string;
	char *product_string;
	u64 unique_id;

	struct greybus_host_device *hd;
};
#define to_gb_interface(d) container_of(d, struct gb_interface, dev)

static inline void gb_interface_set_drvdata(struct gb_interface *intf,
					    void *data)
{
	dev_set_drvdata(&intf->dev, data);
}

static inline void * gb_interface__get_drvdata(struct gb_interface *intf)
{
	return dev_get_drvdata(&intf->dev);
}

/* Greybus "private" definitions */

const struct greybus_interface_id *
	gb_interface_match_id(struct gb_interface *intf,
			      const struct greybus_interface_id *id);

struct gb_interface *gb_interface_find(struct greybus_host_device *hd,
				       u8 module_id);

#endif /* __INTERFACE_H */

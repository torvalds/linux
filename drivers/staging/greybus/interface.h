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
	struct gb_control *control;

	struct list_head bundles;
	struct list_head links;	/* greybus_host_device->interfaces */
	struct list_head manifest_descs;
	u8 interface_id;	/* Physical location within the Endo */
	u8 device_id;		/* Device id allocated for the interface block by the SVC */

	/* Information taken from the manifest descriptor */
	u16 vendor;
	u16 product;
	char *vendor_string;
	char *product_string;
	u64 unique_id;

	struct gb_module *module;
	struct greybus_host_device *hd;
};
#define to_gb_interface(d) container_of(d, struct gb_interface, dev)

static inline void gb_interface_set_drvdata(struct gb_interface *intf,
					    void *data)
{
	dev_set_drvdata(&intf->dev, data);
}

static inline void *gb_interface_get_drvdata(struct gb_interface *intf)
{
	return dev_get_drvdata(&intf->dev);
}

/* Greybus "private" definitions */

struct gb_interface *gb_interface_find(struct greybus_host_device *hd,
				       u8 interface_id);

struct gb_interface *gb_interface_create(struct greybus_host_device *hd,
					 u8 interface_id);
int gb_interface_init(struct gb_interface *intf, u8 device_id);
void gb_interface_remove(struct greybus_host_device *hd, u8 interface_id);
void gb_interfaces_remove(struct greybus_host_device *hd);

int gb_create_bundle_connection(struct gb_interface *intf, u8 class);
#endif /* __INTERFACE_H */

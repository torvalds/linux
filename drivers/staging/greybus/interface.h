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
	struct list_head links;	/* gb_host_device->interfaces */
	struct list_head manifest_descs;
	u8 interface_id;	/* Physical location within the Endo */
	u8 device_id;		/* Device id allocated for the interface block by the SVC */

	/* Information taken from the manifest descriptor */
	char *vendor_string;
	char *product_string;

	/* Information taken from the hotplug event */
	u32 unipro_mfg_id;
	u32 unipro_prod_id;
	u32 vendor_id;
	u32 product_id;

	struct gb_host_device *hd;

	/* The interface needs to boot over unipro */
	bool boot_over_unipro;
	bool disconnected;
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

struct gb_interface *gb_interface_find(struct gb_host_device *hd,
				       u8 interface_id);

struct gb_interface *gb_interface_create(struct gb_host_device *hd,
					 u8 interface_id);
void gb_interface_destroy(struct gb_interface *intf);
int gb_interface_init(struct gb_interface *intf, u8 device_id);
void gb_interface_remove(struct gb_interface *intf);
void gb_interfaces_remove(struct gb_host_device *hd);

#endif /* __INTERFACE_H */

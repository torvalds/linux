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

#define GB_INTERFACE_QUIRK_NO_CPORT_FEATURES		BIT(0)
#define GB_INTERFACE_QUIRK_NO_INIT_STATUS		BIT(1)
#define GB_INTERFACE_QUIRK_NO_ARA_IDS			BIT(2)
#define GB_INTERFACE_QUIRK_FORCED_DISABLE		BIT(3)

struct gb_interface {
	struct device dev;
	struct gb_control *control;

	struct list_head bundles;
	struct list_head module_node;
	struct list_head manifest_descs;
	u8 interface_id;	/* Physical location within the Endo */
	u8 device_id;
	u8 features;		/* Feature flags set in the manifest */

	u32 ddbl1_manufacturer_id;
	u32 ddbl1_product_id;
	u32 vendor_id;
	u32 product_id;
	u64 serial_number;

	struct gb_host_device *hd;
	struct gb_module *module;

	unsigned long quirks;

	struct mutex mutex;

	bool disconnected;
	bool ejected;
	bool active;
	bool enabled;
};
#define to_gb_interface(d) container_of(d, struct gb_interface, dev)

struct gb_interface *gb_interface_create(struct gb_module *module,
					 u8 interface_id);
int gb_interface_activate(struct gb_interface *intf);
void gb_interface_deactivate(struct gb_interface *intf);
int gb_interface_enable(struct gb_interface *intf);
void gb_interface_disable(struct gb_interface *intf);
int gb_interface_add(struct gb_interface *intf);
void gb_interface_del(struct gb_interface *intf);
void gb_interface_put(struct gb_interface *intf);

#endif /* __INTERFACE_H */

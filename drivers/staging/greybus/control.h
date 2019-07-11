/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Greybus CPort control protocol
 *
 * Copyright 2015 Google Inc.
 * Copyright 2015 Linaro Ltd.
 */

#ifndef __CONTROL_H
#define __CONTROL_H

struct gb_control {
	struct device dev;
	struct gb_interface *intf;

	struct gb_connection *connection;

	u8 protocol_major;
	u8 protocol_minor;

	bool has_bundle_activate;
	bool has_bundle_version;

	char *vendor_string;
	char *product_string;
};
#define to_gb_control(d) container_of(d, struct gb_control, dev)

struct gb_control *gb_control_create(struct gb_interface *intf);
int gb_control_enable(struct gb_control *control);
void gb_control_disable(struct gb_control *control);
int gb_control_suspend(struct gb_control *control);
int gb_control_resume(struct gb_control *control);
int gb_control_add(struct gb_control *control);
void gb_control_del(struct gb_control *control);
struct gb_control *gb_control_get(struct gb_control *control);
void gb_control_put(struct gb_control *control);

int gb_control_get_bundle_versions(struct gb_control *control);
int gb_control_connected_operation(struct gb_control *control, u16 cport_id);
int gb_control_disconnected_operation(struct gb_control *control, u16 cport_id);
int gb_control_disconnecting_operation(struct gb_control *control,
				       u16 cport_id);
int gb_control_mode_switch_operation(struct gb_control *control);
void gb_control_mode_switch_prepare(struct gb_control *control);
void gb_control_mode_switch_complete(struct gb_control *control);
int gb_control_get_manifest_size_operation(struct gb_interface *intf);
int gb_control_get_manifest_operation(struct gb_interface *intf, void *manifest,
				      size_t size);
int gb_control_bundle_suspend(struct gb_control *control, u8 bundle_id);
int gb_control_bundle_resume(struct gb_control *control, u8 bundle_id);
int gb_control_bundle_deactivate(struct gb_control *control, u8 bundle_id);
int gb_control_bundle_activate(struct gb_control *control, u8 bundle_id);
int gb_control_interface_suspend_prepare(struct gb_control *control);
int gb_control_interface_deactivate_prepare(struct gb_control *control);
int gb_control_interface_hibernate_abort(struct gb_control *control);
#endif /* __CONTROL_H */

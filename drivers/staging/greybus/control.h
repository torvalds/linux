/*
 * Greybus CPort control protocol
 *
 * Copyright 2015 Google Inc.
 * Copyright 2015 Linaro Ltd.
 *
 * Released under the GPLv2 only.
 */

#ifndef __CONTROL_H
#define __CONTROL_H

struct gb_control {
	struct gb_connection	*connection;
};

int gb_control_connected_operation(struct gb_control *control, u16 cport_id);
int gb_control_disconnected_operation(struct gb_control *control, u16 cport_id);
int gb_control_get_manifest_size_operation(struct gb_interface *intf);
int gb_control_get_manifest_operation(struct gb_interface *intf, void *manifest,
				      size_t size);

int gb_control_protocol_init(void);
void gb_control_protocol_exit(void);
#endif /* __CONTROL_H */

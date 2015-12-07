/*
 * Greybus connections
 *
 * Copyright 2014 Google Inc.
 * Copyright 2014 Linaro Ltd.
 *
 * Released under the GPLv2 only.
 */

#ifndef __CONNECTION_H
#define __CONNECTION_H

#include <linux/list.h>
#include <linux/kfifo.h>

enum gb_connection_state {
	GB_CONNECTION_STATE_INVALID	= 0,
	GB_CONNECTION_STATE_DISABLED	= 1,
	GB_CONNECTION_STATE_ENABLED	= 2,
	GB_CONNECTION_STATE_ERROR	= 3,
	GB_CONNECTION_STATE_DESTROYING	= 4,
};

struct gb_connection {
	struct gb_host_device		*hd;
	struct gb_interface		*intf;
	struct gb_bundle		*bundle;
	struct kref			kref;
	u16				hd_cport_id;
	u16				intf_cport_id;

	struct list_head		hd_links;
	struct list_head		bundle_links;

	struct gb_protocol		*protocol;
	u8				protocol_id;
	u8				major;
	u8				minor;
	u8				module_major;
	u8				module_minor;

	spinlock_t			lock;
	enum gb_connection_state	state;
	struct list_head		operations;

	char				name[16];
	struct workqueue_struct		*wq;

	atomic_t			op_cycle;

	void				*private;
};

struct gb_connection *gb_connection_create_static(struct gb_host_device *hd,
				u16 hd_cport_id, u8 protocol_id);
struct gb_connection *gb_connection_create_dynamic(struct gb_interface *intf,
				struct gb_bundle *bundle, u16 cport_id,
				u8 protocol_id);
void gb_connection_destroy(struct gb_connection *connection);

static inline bool gb_connection_is_static(struct gb_connection *connection)
{
	return !connection->intf;
}

int gb_connection_init(struct gb_connection *connection);
void gb_connection_exit(struct gb_connection *connection);

void greybus_data_rcvd(struct gb_host_device *hd, u16 cport_id,
			u8 *data, size_t length);

void gb_connection_latency_tag_enable(struct gb_connection *connection);
void gb_connection_latency_tag_disable(struct gb_connection *connection);

#endif /* __CONNECTION_H */

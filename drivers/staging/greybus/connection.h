/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Greybus connections
 *
 * Copyright 2014 Google Inc.
 * Copyright 2014 Linaro Ltd.
 */

#ifndef __CONNECTION_H
#define __CONNECTION_H

#include <linux/list.h>
#include <linux/kfifo.h>

#define GB_CONNECTION_FLAG_CSD		BIT(0)
#define GB_CONNECTION_FLAG_NO_FLOWCTRL	BIT(1)
#define GB_CONNECTION_FLAG_OFFLOADED	BIT(2)
#define GB_CONNECTION_FLAG_CDSI1	BIT(3)
#define GB_CONNECTION_FLAG_CONTROL	BIT(4)
#define GB_CONNECTION_FLAG_HIGH_PRIO	BIT(5)

#define GB_CONNECTION_FLAG_CORE_MASK	GB_CONNECTION_FLAG_CONTROL

enum gb_connection_state {
	GB_CONNECTION_STATE_DISABLED		= 0,
	GB_CONNECTION_STATE_ENABLED_TX		= 1,
	GB_CONNECTION_STATE_ENABLED		= 2,
	GB_CONNECTION_STATE_DISCONNECTING	= 3,
};

struct gb_operation;

typedef int (*gb_request_handler_t)(struct gb_operation *);

struct gb_connection {
	struct gb_host_device		*hd;
	struct gb_interface		*intf;
	struct gb_bundle		*bundle;
	struct kref			kref;
	u16				hd_cport_id;
	u16				intf_cport_id;

	struct list_head		hd_links;
	struct list_head		bundle_links;

	gb_request_handler_t		handler;
	unsigned long			flags;

	struct mutex			mutex;
	spinlock_t			lock;
	enum gb_connection_state	state;
	struct list_head		operations;

	char				name[16];
	struct workqueue_struct		*wq;

	atomic_t			op_cycle;

	void				*private;

	bool				mode_switch;
};

struct gb_connection *gb_connection_create_static(struct gb_host_device *hd,
				u16 hd_cport_id, gb_request_handler_t handler);
struct gb_connection *gb_connection_create_control(struct gb_interface *intf);
struct gb_connection *gb_connection_create(struct gb_bundle *bundle,
				u16 cport_id, gb_request_handler_t handler);
struct gb_connection *gb_connection_create_flags(struct gb_bundle *bundle,
				u16 cport_id, gb_request_handler_t handler,
				unsigned long flags);
struct gb_connection *gb_connection_create_offloaded(struct gb_bundle *bundle,
				u16 cport_id, unsigned long flags);
void gb_connection_destroy(struct gb_connection *connection);

static inline bool gb_connection_is_static(struct gb_connection *connection)
{
	return !connection->intf;
}

int gb_connection_enable(struct gb_connection *connection);
int gb_connection_enable_tx(struct gb_connection *connection);
void gb_connection_disable_rx(struct gb_connection *connection);
void gb_connection_disable(struct gb_connection *connection);
void gb_connection_disable_forced(struct gb_connection *connection);

void gb_connection_mode_switch_prepare(struct gb_connection *connection);
void gb_connection_mode_switch_complete(struct gb_connection *connection);

void greybus_data_rcvd(struct gb_host_device *hd, u16 cport_id,
		       u8 *data, size_t length);

void gb_connection_latency_tag_enable(struct gb_connection *connection);
void gb_connection_latency_tag_disable(struct gb_connection *connection);

static inline bool gb_connection_e2efc_enabled(struct gb_connection *connection)
{
	return !(connection->flags & GB_CONNECTION_FLAG_CSD);
}

static inline bool
gb_connection_flow_control_disabled(struct gb_connection *connection)
{
	return connection->flags & GB_CONNECTION_FLAG_NO_FLOWCTRL;
}

static inline bool gb_connection_is_offloaded(struct gb_connection *connection)
{
	return connection->flags & GB_CONNECTION_FLAG_OFFLOADED;
}

static inline bool gb_connection_is_control(struct gb_connection *connection)
{
	return connection->flags & GB_CONNECTION_FLAG_CONTROL;
}

static inline void *gb_connection_get_data(struct gb_connection *connection)
{
	return connection->private;
}

static inline void gb_connection_set_data(struct gb_connection *connection,
					  void *data)
{
	connection->private = data;
}

#endif /* __CONNECTION_H */

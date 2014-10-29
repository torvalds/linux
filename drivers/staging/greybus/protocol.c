/*
 * Greybus protocol handling
 *
 * Copyright 2014 Google Inc.
 *
 * Released under the GPLv2 only.
 */

#include "greybus.h"

/* Global list of registered protocols */
static DEFINE_SPINLOCK(gb_protocols_lock);
static LIST_HEAD(gb_protocols);

/* Caller must hold gb_protocols_lock */
struct gb_protocol *_gb_protocol_find(u8 id)
{
	struct gb_protocol *protocol;

	list_for_each_entry(protocol, &gb_protocols, links)
		if (protocol->id == id)
			return protocol;
	return NULL;
}

/* This is basically for debug */
static struct gb_protocol *gb_protocol_find(u8 id)
{
	struct gb_protocol *protocol;

	spin_lock_irq(&gb_protocols_lock);
	protocol = _gb_protocol_find(id);
	spin_unlock_irq(&gb_protocols_lock);

	return protocol;
}

/* Returns true if protocol was succesfully registered, false otherwise */
bool gb_protocol_register(u8 id)
{
	struct gb_protocol *protocol;
	struct gb_protocol *existing;

	/* Initialize it speculatively */
	protocol = kzalloc(sizeof(*protocol), GFP_KERNEL);
	if (!protocol)
		return false;
	protocol->id = id;
	INIT_LIST_HEAD(&protocol->connections);

	spin_lock_irq(&gb_protocols_lock);
	existing = _gb_protocol_find(id);
	if (!existing)
		list_add(&protocol->links, &gb_protocols);
	spin_unlock_irq(&gb_protocols_lock);

	if (existing) {
		kfree(protocol);
		protocol = NULL;
	}

	return protocol != NULL;
}

/* Returns true if successful, false otherwise */
bool gb_protocol_deregister(struct gb_protocol *protocol)
{
	spin_lock_irq(&gb_protocols_lock);
	if (list_empty(&protocol->connections))
		list_del(&protocol->links);
	else
		protocol = NULL;	/* Protocol is still in use */
	spin_unlock_irq(&gb_protocols_lock);
	kfree(protocol);

	return protocol != NULL;
}

/* Returns true if successful, false otherwise */
bool gb_protocol_get(struct gb_connection *connection, u8 id)
{
	struct gb_protocol *protocol;

	/* Sanity */
	if (!list_empty(&connection->protocol_links) ||
			!connection->protocol->id) {
		gb_connection_err(connection,
			"connection already has protocol");
		return false;
	}

	spin_lock_irq(&gb_protocols_lock);
	protocol = _gb_protocol_find(id);
	if (protocol)
		list_add(&connection->protocol_links, &protocol->connections);
	spin_unlock_irq(&gb_protocols_lock);
	connection->protocol = protocol;

	return protocol != NULL;
}

void gb_protocol_put(struct gb_connection *connection)
{
	struct gb_protocol *protocol = connection->protocol;

	/* Sanity checks */
	if (list_empty(&connection->protocol_links)) {
		gb_connection_err(connection,
			"connection protocol not recorded");
		return;
	}
	if (!protocol || gb_protocol_find(protocol->id) != protocol)  {
		gb_connection_err(connection,
			"connection has undefined protocol");
		return;
	}

	spin_lock_irq(&gb_protocols_lock);
	list_del(&connection->protocol_links);
	connection->protocol = NULL;
	spin_unlock_irq(&gb_protocols_lock);
}

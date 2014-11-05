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
static struct gb_protocol *_gb_protocol_find(u8 id, u8 major, u8 minor)
{
	struct gb_protocol *protocol;

	list_for_each_entry(protocol, &gb_protocols, links)
		if (protocol->id == id && protocol->major == major
					&& protocol->minor == minor)
			return protocol;
	return NULL;
}

/* Returns true if protocol was succesfully registered, false otherwise */
bool gb_protocol_register(u8 id, u8 major, u8 minor)
{
	struct gb_protocol *protocol;
	struct gb_protocol *existing;

	/* Initialize it speculatively */
	protocol = kzalloc(sizeof(*protocol), GFP_KERNEL);
	if (!protocol)
		return false;
	protocol->id = id;
	protocol->major = major;
	protocol->minor = minor;

	spin_lock_irq(&gb_protocols_lock);
	existing = _gb_protocol_find(id, major, minor);
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
	u8 protocol_count;

	spin_lock_irq(&gb_protocols_lock);
	protocol = _gb_protocol_find(protocol->id, protocol->major,
						protocol->minor);
	if (protocol) {
		protocol_count = protocol->count;
		if (!protocol_count)
			list_del(&protocol->links);
	}
	spin_unlock_irq(&gb_protocols_lock);
	kfree(protocol);

	return protocol && !protocol_count;
}

/* Returns the requested protocol if available, or a null pointer */
struct gb_protocol *gb_protocol_get(u8 id, u8 major, u8 minor)
{
	struct gb_protocol *protocol;
	u8 protocol_count;

	spin_lock_irq(&gb_protocols_lock);
	protocol = _gb_protocol_find(id, major, minor);
	if (protocol) {
		protocol_count = protocol->count;
		if (protocol_count != U8_MAX)
			protocol->count++;
	}
	spin_unlock_irq(&gb_protocols_lock);

	if (protocol)
		WARN_ON(protocol_count == U8_MAX);
	else
		pr_err("protocol id %hhu version %hhu.%hhu not found\n",
			id, major, minor);

	return protocol;
}

void gb_protocol_put(struct gb_protocol *protocol)
{
	u8 major = protocol->major;
	u8 minor = protocol->minor;
	u8 protocol_count;

	spin_lock_irq(&gb_protocols_lock);
	protocol = _gb_protocol_find(protocol->id, protocol->major,
						protocol->minor);
	if (protocol) {
		protocol_count = protocol->count;
		if (protocol_count)
			protocol->count--;
	}
	spin_unlock_irq(&gb_protocols_lock);
	if (protocol)
		WARN_ON(!protocol_count);
	else
		pr_err("protocol id %hhu version %hhu.%hhu not found\n",
			protocol->id, major, minor);
}

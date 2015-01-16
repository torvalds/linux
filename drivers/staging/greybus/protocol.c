/*
 * Greybus protocol handling
 *
 * Copyright 2014 Google Inc.
 * Copyright 2014 Linaro Ltd.
 *
 * Released under the GPLv2 only.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include "greybus.h"

/* Global list of registered protocols */
static DEFINE_SPINLOCK(gb_protocols_lock);
static LIST_HEAD(gb_protocols);

/* Caller must hold gb_protocols_lock */
static struct gb_protocol *_gb_protocol_find(u8 id, u8 major, u8 minor)
{
	struct gb_protocol *protocol;

	list_for_each_entry(protocol, &gb_protocols, links) {
		if (protocol->id < id)
			continue;
		if (protocol->id > id)
			break;

		if (protocol->major > major)
			continue;
		if (protocol->major < major)
			break;

		if (protocol->minor > minor)
			continue;
		if (protocol->minor < minor)
			break;

		return protocol;
	}
	return NULL;
}

int __gb_protocol_register(struct gb_protocol *protocol, struct module *module)
{
	struct gb_protocol *existing;
	u8 id = protocol->id;
	u8 major = protocol->major;
	u8 minor = protocol->minor;

	protocol->owner = module;

	/*
	 * The protocols list is sorted first by protocol id (low to
	 * high), then by major version (high to low), and finally
	 * by minor version (high to low).  Searching only by
	 * protocol id will produce the newest implemented version
	 * of the protocol.
	 */
	spin_lock_irq(&gb_protocols_lock);

	list_for_each_entry(existing, &gb_protocols, links) {
		if (existing->id < id)
			continue;
		if (existing->id > id)
			break;

		if (existing->major > major)
			continue;
		if (existing->major < major)
			break;

		if (existing->minor > minor)
			continue;
		if (existing->minor < minor)
			break;

		/* A matching protocol has already been registered */
		spin_unlock_irq(&gb_protocols_lock);

		return -EEXIST;
	}

	/*
	 * We need to insert the protocol here, before the existing one
	 * (or before the head if we searched the whole list)
	 */
	list_add_tail(&protocol->links, &existing->links);
	spin_unlock_irq(&gb_protocols_lock);

	pr_info("Registered %s protocol.\n", protocol->name);

	/*
	 * Go try to bind any unbound connections, as we have a
	 * new protocol in the system
	 */
	gb_bundle_bind_protocols();

	return 0;
}
EXPORT_SYMBOL_GPL(__gb_protocol_register);

/*
 * De-register a previously registered protocol.
 *
 * XXX Currently this fails (and reports an error to the caller) if
 * XXX the protocol is currently in use.  We may want to forcefully
 * XXX kill off a protocol and all its active users at some point.
 * XXX But I think that's better handled by quiescing modules that
 * XXX have users and having those users drop their reference.
 *
 * Returns true if successful, false otherwise.
 */
int gb_protocol_deregister(struct gb_protocol *protocol)
{
	u8 protocol_count = 0;

	if (!protocol)
		return 0;

	spin_lock_irq(&gb_protocols_lock);
	protocol = _gb_protocol_find(protocol->id, protocol->major,
						protocol->minor);
	if (protocol) {
		protocol_count = protocol->count;
		if (!protocol_count)
			list_del(&protocol->links);
	}
	spin_unlock_irq(&gb_protocols_lock);

	if (protocol)
		pr_info("Deregistered %s protocol.\n", protocol->name);

	return protocol && !protocol_count;
}
EXPORT_SYMBOL_GPL(gb_protocol_deregister);

/* Returns the requested protocol if available, or a null pointer */
struct gb_protocol *gb_protocol_get(u8 id, u8 major, u8 minor)
{
	struct gb_protocol *protocol;
	u8 protocol_count;

	spin_lock_irq(&gb_protocols_lock);
	protocol = _gb_protocol_find(id, major, minor);
	if (protocol) {
		if (!try_module_get(protocol->owner)) {
			protocol = NULL;
		} else {
			protocol_count = protocol->count;
			if (protocol_count != U8_MAX)
				protocol->count++;
		}
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
	u8 major;
	u8 minor;
	u8 protocol_count;

	if (!protocol)
		return;

	major = protocol->major;
	minor = protocol->minor;

	spin_lock_irq(&gb_protocols_lock);
	protocol = _gb_protocol_find(protocol->id, protocol->major,
						protocol->minor);
	if (protocol) {
		protocol_count = protocol->count;
		if (protocol_count)
			protocol->count--;
		module_put(protocol->owner);
	}
	spin_unlock_irq(&gb_protocols_lock);
	if (protocol)
		WARN_ON(!protocol_count);
	else
		/* FIXME a different message is needed since this one
		 * will result in a NULL dereference
		 */
		pr_err("protocol id %hhu version %hhu.%hhu not found\n",
			protocol->id, major, minor);
}

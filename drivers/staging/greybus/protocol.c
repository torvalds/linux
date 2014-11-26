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

/* Returns true if protocol was successfully registered, false otherwise */
bool gb_protocol_register(struct gb_protocol *protocol)
{
	struct gb_protocol *existing;
	u8 id = protocol->id;
	u8 major = protocol->major;
	u8 minor = protocol->minor;

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

		return false;
	}

	/*
	 * We need to insert the protocol here, before the existing one
	 * (or before the head if we searched the whole list)
	 */
	list_add_tail(&protocol->links, &existing->links);
	spin_unlock_irq(&gb_protocols_lock);

	return true;
}

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
bool gb_protocol_deregister(struct gb_protocol *protocol)
{
	u8 protocol_count = 0;

	spin_lock_irq(&gb_protocols_lock);
	protocol = _gb_protocol_find(protocol->id, protocol->major,
						protocol->minor);
	if (protocol) {
		protocol_count = protocol->count;
		if (!protocol_count)
			list_del(&protocol->links);
	}
	spin_unlock_irq(&gb_protocols_lock);

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

bool gb_protocol_init(void)
{
	bool ret = true;

	if (!gb_battery_protocol_init()) {
		pr_err("error initializing battery protocol\n");
		ret = false;
	}
	if (!gb_gpio_protocol_init()) {
		pr_err("error initializing gpio protocol\n");
		ret = false;
	}
	if (!gb_i2c_protocol_init()) {
		pr_err("error initializing i2c protocol\n");
		ret = false;
	}
	if (!gb_pwm_protocol_init()) {
		pr_err("error initializing pwm protocol\n");
		ret = false;
	}
	if (!gb_uart_protocol_init()) {
		pr_err("error initializing uart protocol\n");
		ret = false;
	}
	if (!gb_sdio_protocol_init()) {
		pr_err("error initializing sdio protocol\n");
		ret = false;
	}
	if (!gb_vibrator_protocol_init()) {
		pr_err("error initializing vibrator protocol\n");
		ret = false;
	}
	if (!gb_usb_protocol_init()) {
		pr_err("error initializing usb protocol\n");
		ret = false;
	}
	return ret;
}

void gb_protocol_exit(void)
{
	gb_usb_protocol_exit();
	gb_vibrator_protocol_exit();
	gb_sdio_protocol_exit();
	gb_uart_protocol_exit();
	gb_i2c_protocol_exit();
	gb_gpio_protocol_exit();
	gb_battery_protocol_exit();
}

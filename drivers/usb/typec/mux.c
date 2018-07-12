// SPDX-License-Identifier: GPL-2.0
/**
 * USB Type-C Multiplexer/DeMultiplexer Switch support
 *
 * Copyright (C) 2018 Intel Corporation
 * Author: Heikki Krogerus <heikki.krogerus@linux.intel.com>
 *         Hans de Goede <hdegoede@redhat.com>
 */

#include <linux/device.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/usb/typec_mux.h>

static DEFINE_MUTEX(switch_lock);
static DEFINE_MUTEX(mux_lock);
static LIST_HEAD(switch_list);
static LIST_HEAD(mux_list);

static void *typec_switch_match(struct device_connection *con, int ep,
				void *data)
{
	struct typec_switch *sw;

	list_for_each_entry(sw, &switch_list, entry)
		if (!strcmp(con->endpoint[ep], dev_name(sw->dev)))
			return sw;

	/*
	 * We only get called if a connection was found, tell the caller to
	 * wait for the switch to show up.
	 */
	return ERR_PTR(-EPROBE_DEFER);
}

/**
 * typec_switch_get - Find USB Type-C orientation switch
 * @dev: The caller device
 *
 * Finds a switch linked with @dev. Returns a reference to the switch on
 * success, NULL if no matching connection was found, or
 * ERR_PTR(-EPROBE_DEFER) when a connection was found but the switch
 * has not been enumerated yet.
 */
struct typec_switch *typec_switch_get(struct device *dev)
{
	struct typec_switch *sw;

	mutex_lock(&switch_lock);
	sw = device_connection_find_match(dev, "typec-switch", NULL,
					  typec_switch_match);
	if (!IS_ERR_OR_NULL(sw))
		get_device(sw->dev);
	mutex_unlock(&switch_lock);

	return sw;
}
EXPORT_SYMBOL_GPL(typec_switch_get);

/**
 * typec_put_switch - Release USB Type-C orientation switch
 * @sw: USB Type-C orientation switch
 *
 * Decrement reference count for @sw.
 */
void typec_switch_put(struct typec_switch *sw)
{
	if (!IS_ERR_OR_NULL(sw))
		put_device(sw->dev);
}
EXPORT_SYMBOL_GPL(typec_switch_put);

/**
 * typec_switch_register - Register USB Type-C orientation switch
 * @sw: USB Type-C orientation switch
 *
 * This function registers a switch that can be used for routing the correct
 * data pairs depending on the cable plug orientation from the USB Type-C
 * connector to the USB controllers. USB Type-C plugs can be inserted
 * right-side-up or upside-down.
 */
int typec_switch_register(struct typec_switch *sw)
{
	mutex_lock(&switch_lock);
	list_add_tail(&sw->entry, &switch_list);
	mutex_unlock(&switch_lock);

	return 0;
}
EXPORT_SYMBOL_GPL(typec_switch_register);

/**
 * typec_switch_unregister - Unregister USB Type-C orientation switch
 * @sw: USB Type-C orientation switch
 *
 * Unregister switch that was registered with typec_switch_register().
 */
void typec_switch_unregister(struct typec_switch *sw)
{
	mutex_lock(&switch_lock);
	list_del(&sw->entry);
	mutex_unlock(&switch_lock);
}
EXPORT_SYMBOL_GPL(typec_switch_unregister);

/* ------------------------------------------------------------------------- */

static void *typec_mux_match(struct device_connection *con, int ep, void *data)
{
	struct typec_mux *mux;

	list_for_each_entry(mux, &mux_list, entry)
		if (!strcmp(con->endpoint[ep], dev_name(mux->dev)))
			return mux;

	/*
	 * We only get called if a connection was found, tell the caller to
	 * wait for the switch to show up.
	 */
	return ERR_PTR(-EPROBE_DEFER);
}

/**
 * typec_mux_get - Find USB Type-C Multiplexer
 * @dev: The caller device
 *
 * Finds a mux linked to the caller. This function is primarily meant for the
 * Type-C drivers. Returns a reference to the mux on success, NULL if no
 * matching connection was found, or ERR_PTR(-EPROBE_DEFER) when a connection
 * was found but the mux has not been enumerated yet.
 */
struct typec_mux *typec_mux_get(struct device *dev)
{
	struct typec_mux *mux;

	mutex_lock(&mux_lock);
	mux = device_connection_find_match(dev, "typec-mux", NULL,
					   typec_mux_match);
	if (!IS_ERR_OR_NULL(mux))
		get_device(mux->dev);
	mutex_unlock(&mux_lock);

	return mux;
}
EXPORT_SYMBOL_GPL(typec_mux_get);

/**
 * typec_mux_put - Release handle to a Multiplexer
 * @mux: USB Type-C Connector Multiplexer/DeMultiplexer
 *
 * Decrements reference count for @mux.
 */
void typec_mux_put(struct typec_mux *mux)
{
	if (!IS_ERR_OR_NULL(mux))
		put_device(mux->dev);
}
EXPORT_SYMBOL_GPL(typec_mux_put);

/**
 * typec_mux_register - Register Multiplexer routing USB Type-C pins
 * @mux: USB Type-C Connector Multiplexer/DeMultiplexer
 *
 * USB Type-C connectors can be used for alternate modes of operation besides
 * USB when Accessory/Alternate Modes are supported. With some of those modes,
 * the pins on the connector need to be reconfigured. This function registers
 * multiplexer switches routing the pins on the connector.
 */
int typec_mux_register(struct typec_mux *mux)
{
	mutex_lock(&mux_lock);
	list_add_tail(&mux->entry, &mux_list);
	mutex_unlock(&mux_lock);

	return 0;
}
EXPORT_SYMBOL_GPL(typec_mux_register);

/**
 * typec_mux_unregister - Unregister Multiplexer Switch
 * @sw: USB Type-C Connector Multiplexer/DeMultiplexer
 *
 * Unregister mux that was registered with typec_mux_register().
 */
void typec_mux_unregister(struct typec_mux *mux)
{
	mutex_lock(&mux_lock);
	list_del(&mux->entry);
	mutex_unlock(&mux_lock);
}
EXPORT_SYMBOL_GPL(typec_mux_unregister);

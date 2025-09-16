// SPDX-License-Identifier: MIT
/*
 * Copyright (C) 2020 - 2021 Red Hat, Inc.
 *
 * Authors:
 * Hans de Goede <hdegoede@redhat.com>
 */

#include <linux/device.h>
#include <linux/export.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <drm/drm_privacy_screen_machine.h>
#include <drm/drm_privacy_screen_consumer.h>
#include <drm/drm_privacy_screen_driver.h>
#include "drm_internal.h"

/**
 * DOC: overview
 *
 * This class allows non KMS drivers, from e.g. drivers/platform/x86 to
 * register a privacy-screen device, which the KMS drivers can then use
 * to implement the standard privacy-screen properties, see
 * :ref:`Standard Connector Properties<standard_connector_properties>`.
 *
 * KMS drivers using a privacy-screen class device are advised to use the
 * drm_connector_attach_privacy_screen_provider() and
 * drm_connector_update_privacy_screen() helpers for dealing with this.
 */

#define to_drm_privacy_screen(dev) \
	container_of(dev, struct drm_privacy_screen, dev)

static DEFINE_MUTEX(drm_privacy_screen_lookup_lock);
static LIST_HEAD(drm_privacy_screen_lookup_list);

static DEFINE_MUTEX(drm_privacy_screen_devs_lock);
static LIST_HEAD(drm_privacy_screen_devs);

/*** drm_privacy_screen_machine.h functions ***/

/**
 * drm_privacy_screen_lookup_add - add an entry to the static privacy-screen
 *    lookup list
 * @lookup: lookup list entry to add
 *
 * Add an entry to the static privacy-screen lookup list. Note the
 * &struct list_head which is part of the &struct drm_privacy_screen_lookup
 * gets added to a list owned by the privacy-screen core. So the passed in
 * &struct drm_privacy_screen_lookup must not be free-ed until it is removed
 * from the lookup list by calling drm_privacy_screen_lookup_remove().
 */
void drm_privacy_screen_lookup_add(struct drm_privacy_screen_lookup *lookup)
{
	mutex_lock(&drm_privacy_screen_lookup_lock);
	list_add(&lookup->list, &drm_privacy_screen_lookup_list);
	mutex_unlock(&drm_privacy_screen_lookup_lock);
}
EXPORT_SYMBOL(drm_privacy_screen_lookup_add);

/**
 * drm_privacy_screen_lookup_remove - remove an entry to the static
 *    privacy-screen lookup list
 * @lookup: lookup list entry to remove
 *
 * Remove an entry previously added with drm_privacy_screen_lookup_add()
 * from the static privacy-screen lookup list.
 */
void drm_privacy_screen_lookup_remove(struct drm_privacy_screen_lookup *lookup)
{
	mutex_lock(&drm_privacy_screen_lookup_lock);
	list_del(&lookup->list);
	mutex_unlock(&drm_privacy_screen_lookup_lock);
}
EXPORT_SYMBOL(drm_privacy_screen_lookup_remove);

/*** drm_privacy_screen_consumer.h functions ***/

static struct drm_privacy_screen *drm_privacy_screen_get_by_name(
	const char *name)
{
	struct drm_privacy_screen *priv;
	struct device *dev = NULL;

	mutex_lock(&drm_privacy_screen_devs_lock);

	list_for_each_entry(priv, &drm_privacy_screen_devs, list) {
		if (strcmp(dev_name(&priv->dev), name) == 0) {
			dev = get_device(&priv->dev);
			break;
		}
	}

	mutex_unlock(&drm_privacy_screen_devs_lock);

	return dev ? to_drm_privacy_screen(dev) : NULL;
}

/**
 * drm_privacy_screen_get - get a privacy-screen provider
 * @dev: consumer-device for which to get a privacy-screen provider
 * @con_id: (video)connector name for which to get a privacy-screen provider
 *
 * Get a privacy-screen provider for a privacy-screen attached to the
 * display described by the @dev and @con_id parameters.
 *
 * Return:
 * * A pointer to a &struct drm_privacy_screen on success.
 * * ERR_PTR(-ENODEV) if no matching privacy-screen is found
 * * ERR_PTR(-EPROBE_DEFER) if there is a matching privacy-screen,
 *                          but it has not been registered yet.
 */
struct drm_privacy_screen *drm_privacy_screen_get(struct device *dev,
						  const char *con_id)
{
	const char *dev_id = dev ? dev_name(dev) : NULL;
	struct drm_privacy_screen_lookup *l;
	struct drm_privacy_screen *priv;
	const char *provider = NULL;
	int match, best = -1;

	/*
	 * For now we only support using a static lookup table, which is
	 * populated by the drm_privacy_screen_arch_init() call. This should
	 * be extended with device-tree / fw_node lookup when support is added
	 * for device-tree using hardware with a privacy-screen.
	 *
	 * The lookup algorithm was shamelessly taken from the clock
	 * framework:
	 *
	 * We do slightly fuzzy matching here:
	 *  An entry with a NULL ID is assumed to be a wildcard.
	 *  If an entry has a device ID, it must match
	 *  If an entry has a connection ID, it must match
	 * Then we take the most specific entry - with the following order
	 * of precedence: dev+con > dev only > con only.
	 */
	mutex_lock(&drm_privacy_screen_lookup_lock);

	list_for_each_entry(l, &drm_privacy_screen_lookup_list, list) {
		match = 0;

		if (l->dev_id) {
			if (!dev_id || strcmp(l->dev_id, dev_id))
				continue;

			match += 2;
		}

		if (l->con_id) {
			if (!con_id || strcmp(l->con_id, con_id))
				continue;

			match += 1;
		}

		if (match > best) {
			provider = l->provider;
			best = match;
		}
	}

	mutex_unlock(&drm_privacy_screen_lookup_lock);

	if (!provider)
		return ERR_PTR(-ENODEV);

	priv = drm_privacy_screen_get_by_name(provider);
	if (!priv)
		return ERR_PTR(-EPROBE_DEFER);

	return priv;
}
EXPORT_SYMBOL(drm_privacy_screen_get);

/**
 * drm_privacy_screen_put - release a privacy-screen reference
 * @priv: privacy screen reference to release
 *
 * Release a privacy-screen provider reference gotten through
 * drm_privacy_screen_get(). May be called with a NULL or ERR_PTR,
 * in which case it is a no-op.
 */
void drm_privacy_screen_put(struct drm_privacy_screen *priv)
{
	if (IS_ERR_OR_NULL(priv))
		return;

	put_device(&priv->dev);
}
EXPORT_SYMBOL(drm_privacy_screen_put);

/**
 * drm_privacy_screen_set_sw_state - set a privacy-screen's sw-state
 * @priv: privacy screen to set the sw-state for
 * @sw_state: new sw-state value to set
 *
 * Set the sw-state of a privacy screen. If the privacy-screen is not
 * in a locked hw-state, then the actual and hw-state of the privacy-screen
 * will be immediately updated to the new value. If the privacy-screen is
 * in a locked hw-state, then the new sw-state will be remembered as the
 * requested state to put the privacy-screen in when it becomes unlocked.
 *
 * Return: 0 on success, negative error code on failure.
 */
int drm_privacy_screen_set_sw_state(struct drm_privacy_screen *priv,
				    enum drm_privacy_screen_status sw_state)
{
	int ret = 0;

	mutex_lock(&priv->lock);

	if (!priv->ops) {
		ret = -ENODEV;
		goto out;
	}

	/*
	 * As per the DRM connector properties documentation, setting the
	 * sw_state while the hw_state is locked is allowed. In this case
	 * it is a no-op other then storing the new sw_state so that it
	 * can be honored when the state gets unlocked.
	 * Also skip the set if the hw already is in the desired state.
	 */
	if (priv->hw_state >= PRIVACY_SCREEN_DISABLED_LOCKED ||
	    priv->hw_state == sw_state) {
		priv->sw_state = sw_state;
		goto out;
	}

	ret = priv->ops->set_sw_state(priv, sw_state);
out:
	mutex_unlock(&priv->lock);
	return ret;
}
EXPORT_SYMBOL(drm_privacy_screen_set_sw_state);

/**
 * drm_privacy_screen_get_state - get privacy-screen's current state
 * @priv: privacy screen to get the state for
 * @sw_state_ret: address where to store the privacy-screens current sw-state
 * @hw_state_ret: address where to store the privacy-screens current hw-state
 *
 * Get the current state of a privacy-screen, both the sw-state and the
 * hw-state.
 */
void drm_privacy_screen_get_state(struct drm_privacy_screen *priv,
				  enum drm_privacy_screen_status *sw_state_ret,
				  enum drm_privacy_screen_status *hw_state_ret)
{
	mutex_lock(&priv->lock);
	*sw_state_ret = priv->sw_state;
	*hw_state_ret = priv->hw_state;
	mutex_unlock(&priv->lock);
}
EXPORT_SYMBOL(drm_privacy_screen_get_state);

/**
 * drm_privacy_screen_register_notifier - register a notifier
 * @priv: Privacy screen to register the notifier with
 * @nb: Notifier-block for the notifier to register
 *
 * Register a notifier with the privacy-screen to be notified of changes made
 * to the privacy-screen state from outside of the privacy-screen class.
 * E.g. the state may be changed by the hardware itself in response to a
 * hotkey press.
 *
 * The notifier is called with no locks held. The new hw_state and sw_state
 * can be retrieved using the drm_privacy_screen_get_state() function.
 * A pointer to the drm_privacy_screen's struct is passed as the ``void *data``
 * argument of the notifier_block's notifier_call.
 *
 * The notifier will NOT be called when changes are made through
 * drm_privacy_screen_set_sw_state(). It is only called for external changes.
 *
 * Return: 0 on success, negative error code on failure.
 */
int drm_privacy_screen_register_notifier(struct drm_privacy_screen *priv,
					 struct notifier_block *nb)
{
	return blocking_notifier_chain_register(&priv->notifier_head, nb);
}
EXPORT_SYMBOL(drm_privacy_screen_register_notifier);

/**
 * drm_privacy_screen_unregister_notifier - unregister a notifier
 * @priv: Privacy screen to register the notifier with
 * @nb: Notifier-block for the notifier to register
 *
 * Unregister a notifier registered with drm_privacy_screen_register_notifier().
 *
 * Return: 0 on success, negative error code on failure.
 */
int drm_privacy_screen_unregister_notifier(struct drm_privacy_screen *priv,
					   struct notifier_block *nb)
{
	return blocking_notifier_chain_unregister(&priv->notifier_head, nb);
}
EXPORT_SYMBOL(drm_privacy_screen_unregister_notifier);

/*** drm_privacy_screen_driver.h functions ***/

static ssize_t sw_state_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	struct drm_privacy_screen *priv = to_drm_privacy_screen(dev);
	const char * const sw_state_names[] = {
		"Disabled",
		"Enabled",
	};
	ssize_t ret;

	mutex_lock(&priv->lock);

	if (!priv->ops)
		ret = -ENODEV;
	else if (WARN_ON(priv->sw_state >= ARRAY_SIZE(sw_state_names)))
		ret = -ENXIO;
	else
		ret = sprintf(buf, "%s\n", sw_state_names[priv->sw_state]);

	mutex_unlock(&priv->lock);
	return ret;
}
/*
 * RO: Do not allow setting the sw_state through sysfs, this MUST be done
 * through the drm_properties on the drm_connector.
 */
static DEVICE_ATTR_RO(sw_state);

static ssize_t hw_state_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	struct drm_privacy_screen *priv = to_drm_privacy_screen(dev);
	const char * const hw_state_names[] = {
		"Disabled",
		"Enabled",
		"Disabled, locked",
		"Enabled, locked",
	};
	ssize_t ret;

	mutex_lock(&priv->lock);

	if (!priv->ops)
		ret = -ENODEV;
	else if (WARN_ON(priv->hw_state >= ARRAY_SIZE(hw_state_names)))
		ret = -ENXIO;
	else
		ret = sprintf(buf, "%s\n", hw_state_names[priv->hw_state]);

	mutex_unlock(&priv->lock);
	return ret;
}
static DEVICE_ATTR_RO(hw_state);

static struct attribute *drm_privacy_screen_attrs[] = {
	&dev_attr_sw_state.attr,
	&dev_attr_hw_state.attr,
	NULL
};
ATTRIBUTE_GROUPS(drm_privacy_screen);

static struct device_type drm_privacy_screen_type = {
	.name = "privacy_screen",
	.groups = drm_privacy_screen_groups,
};

static void drm_privacy_screen_device_release(struct device *dev)
{
	struct drm_privacy_screen *priv = to_drm_privacy_screen(dev);

	kfree(priv);
}

/**
 * drm_privacy_screen_register - register a privacy-screen
 * @parent: parent-device for the privacy-screen
 * @ops: &struct drm_privacy_screen_ops pointer with ops for the privacy-screen
 * @data: Private data owned by the privacy screen provider
 *
 * Create and register a privacy-screen.
 *
 * Return:
 * * A pointer to the created privacy-screen on success.
 * * An ERR_PTR(errno) on failure.
 */
struct drm_privacy_screen *drm_privacy_screen_register(
	struct device *parent, const struct drm_privacy_screen_ops *ops,
	void *data)
{
	struct drm_privacy_screen *priv;
	int ret;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return ERR_PTR(-ENOMEM);

	mutex_init(&priv->lock);
	BLOCKING_INIT_NOTIFIER_HEAD(&priv->notifier_head);

	priv->dev.class = drm_class;
	priv->dev.type = &drm_privacy_screen_type;
	priv->dev.parent = parent;
	priv->dev.release = drm_privacy_screen_device_release;
	dev_set_name(&priv->dev, "privacy_screen-%s", dev_name(parent));
	priv->drvdata = data;
	priv->ops = ops;

	priv->ops->get_hw_state(priv);

	ret = device_register(&priv->dev);
	if (ret) {
		put_device(&priv->dev);
		return ERR_PTR(ret);
	}

	mutex_lock(&drm_privacy_screen_devs_lock);
	list_add(&priv->list, &drm_privacy_screen_devs);
	mutex_unlock(&drm_privacy_screen_devs_lock);

	return priv;
}
EXPORT_SYMBOL(drm_privacy_screen_register);

/**
 * drm_privacy_screen_unregister - unregister privacy-screen
 * @priv: privacy-screen to unregister
 *
 * Unregister a privacy-screen registered with drm_privacy_screen_register().
 * May be called with a NULL or ERR_PTR, in which case it is a no-op.
 */
void drm_privacy_screen_unregister(struct drm_privacy_screen *priv)
{
	if (IS_ERR_OR_NULL(priv))
		return;

	mutex_lock(&drm_privacy_screen_devs_lock);
	list_del(&priv->list);
	mutex_unlock(&drm_privacy_screen_devs_lock);

	mutex_lock(&priv->lock);
	priv->drvdata = NULL;
	priv->ops = NULL;
	mutex_unlock(&priv->lock);

	device_unregister(&priv->dev);
}
EXPORT_SYMBOL(drm_privacy_screen_unregister);

/**
 * drm_privacy_screen_call_notifier_chain - notify consumers of state change
 * @priv: Privacy screen to register the notifier with
 *
 * A privacy-screen provider driver can call this functions upon external
 * changes to the privacy-screen state. E.g. the state may be changed by the
 * hardware itself in response to a hotkey press.
 * This function must be called without holding the privacy-screen lock.
 * the driver must update sw_state and hw_state to reflect the new state before
 * calling this function.
 * The expected behavior from the driver upon receiving an external state
 * change event is: 1. Take the lock; 2. Update sw_state and hw_state;
 * 3. Release the lock. 4. Call drm_privacy_screen_call_notifier_chain().
 */
void drm_privacy_screen_call_notifier_chain(struct drm_privacy_screen *priv)
{
	blocking_notifier_call_chain(&priv->notifier_head, 0, priv);
}
EXPORT_SYMBOL(drm_privacy_screen_call_notifier_chain);

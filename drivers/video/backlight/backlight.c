/*
 * Backlight Lowlevel Control Abstraction
 *
 * Copyright (C) 2003,2004 Hewlett-Packard Company
 *
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/backlight.h>
#include <linux/notifier.h>
#include <linux/ctype.h>
#include <linux/err.h>
#include <linux/fb.h>
#include <linux/slab.h>

#ifdef CONFIG_PMAC_BACKLIGHT
#include <asm/backlight.h>
#endif

static struct list_head backlight_dev_list;
static struct mutex backlight_dev_list_mutex;
static struct blocking_notifier_head backlight_notifier;

static const char *const backlight_types[] = {
	[BACKLIGHT_RAW] = "raw",
	[BACKLIGHT_PLATFORM] = "platform",
	[BACKLIGHT_FIRMWARE] = "firmware",
};

#if defined(CONFIG_FB) || (defined(CONFIG_FB_MODULE) && \
			   defined(CONFIG_BACKLIGHT_CLASS_DEVICE_MODULE))
/* This callback gets called when something important happens inside a
 * framebuffer driver. We're looking if that important event is blanking,
 * and if it is and necessary, we're switching backlight power as well ...
 */
static int fb_notifier_callback(struct notifier_block *self,
				unsigned long event, void *data)
{
	struct backlight_device *bd;
	struct fb_event *evdata = data;
	int node = evdata->info->node;
	int fb_blank = 0;

	/* If we aren't interested in this event, skip it immediately ... */
	if (event != FB_EVENT_BLANK && event != FB_EVENT_CONBLANK &&
	    event != FB_EARLY_EVENT_BLANK)
		return 0;

	bd = container_of(self, struct backlight_device, fb_notif);
	mutex_lock(&bd->ops_lock);
	if (bd->ops)
		if (!bd->ops->check_fb ||
		    bd->ops->check_fb(bd, evdata->info)) {
			fb_blank = *(int *)evdata->data;
			if (fb_blank == FB_BLANK_UNBLANK &&
			    !bd->fb_bl_on[node] &&
			    event != FB_EARLY_EVENT_BLANK) {
				bd->fb_bl_on[node] = true;
				if (!bd->use_count++) {
					bd->props.state &= ~BL_CORE_FBBLANK;
					bd->props.fb_blank = FB_BLANK_UNBLANK;
					backlight_update_status(bd);
				}
			} else if (fb_blank != FB_BLANK_UNBLANK &&
				   bd->fb_bl_on[node] &&
				   event == FB_EARLY_EVENT_BLANK) {
				bd->fb_bl_on[node] = false;
				if (!(--bd->use_count)) {
					bd->props.state |= BL_CORE_FBBLANK;
					bd->props.fb_blank = fb_blank;
					backlight_update_status(bd);
				}
			}
		}
	mutex_unlock(&bd->ops_lock);
	return 0;
}

static int backlight_register_fb(struct backlight_device *bd)
{
	memset(&bd->fb_notif, 0, sizeof(bd->fb_notif));
	bd->fb_notif.notifier_call = fb_notifier_callback;

	return fb_register_client(&bd->fb_notif);
}

static void backlight_unregister_fb(struct backlight_device *bd)
{
	fb_unregister_client(&bd->fb_notif);
}
#else
static inline int backlight_register_fb(struct backlight_device *bd)
{
	return 0;
}

static inline void backlight_unregister_fb(struct backlight_device *bd)
{
}
#endif /* CONFIG_FB */

static void backlight_generate_event(struct backlight_device *bd,
				     enum backlight_update_reason reason)
{
	char *envp[2];

	switch (reason) {
	case BACKLIGHT_UPDATE_SYSFS:
		envp[0] = "SOURCE=sysfs";
		break;
	case BACKLIGHT_UPDATE_HOTKEY:
		envp[0] = "SOURCE=hotkey";
		break;
	default:
		envp[0] = "SOURCE=unknown";
		break;
	}
	envp[1] = NULL;
	kobject_uevent_env(&bd->dev.kobj, KOBJ_CHANGE, envp);
	sysfs_notify(&bd->dev.kobj, NULL, "actual_brightness");
}

static ssize_t bl_power_show(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct backlight_device *bd = to_backlight_device(dev);

	return sprintf(buf, "%d\n", bd->props.power);
}

static ssize_t bl_power_store(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	int rc;
	struct backlight_device *bd = to_backlight_device(dev);
	unsigned long power;

	rc = kstrtoul(buf, 0, &power);
	if (rc)
		return rc;

	rc = -ENXIO;
	mutex_lock(&bd->ops_lock);
	if (bd->ops) {
		pr_debug("set power to %lu\n", power);
		if (bd->props.power != power) {
			bd->props.power = power;
			backlight_update_status(bd);
		}
		rc = count;
	}
	mutex_unlock(&bd->ops_lock);

	return rc;
}
static DEVICE_ATTR_RW(bl_power);

static ssize_t brightness_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct backlight_device *bd = to_backlight_device(dev);

	return sprintf(buf, "%d\n", bd->props.brightness);
}

int backlight_device_set_brightness(struct backlight_device *bd,
				    unsigned long brightness)
{
	int rc = -ENXIO;

	mutex_lock(&bd->ops_lock);
	if (bd->ops) {
		if (brightness > bd->props.max_brightness)
			rc = -EINVAL;
		else {
			pr_debug("set brightness to %lu\n", brightness);
			bd->props.brightness = brightness;
			backlight_update_status(bd);
			rc = 0;
		}
	}
	mutex_unlock(&bd->ops_lock);

	backlight_generate_event(bd, BACKLIGHT_UPDATE_SYSFS);

	return rc;
}
EXPORT_SYMBOL(backlight_device_set_brightness);

static ssize_t brightness_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	int rc;
	struct backlight_device *bd = to_backlight_device(dev);
	unsigned long brightness;

	rc = kstrtoul(buf, 0, &brightness);
	if (rc)
		return rc;

	rc = backlight_device_set_brightness(bd, brightness);

	return rc ? rc : count;
}
static DEVICE_ATTR_RW(brightness);

static ssize_t type_show(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct backlight_device *bd = to_backlight_device(dev);

	return sprintf(buf, "%s\n", backlight_types[bd->props.type]);
}
static DEVICE_ATTR_RO(type);

static ssize_t max_brightness_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct backlight_device *bd = to_backlight_device(dev);

	return sprintf(buf, "%d\n", bd->props.max_brightness);
}
static DEVICE_ATTR_RO(max_brightness);

static ssize_t actual_brightness_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int rc = -ENXIO;
	struct backlight_device *bd = to_backlight_device(dev);

	mutex_lock(&bd->ops_lock);
	if (bd->ops && bd->ops->get_brightness)
		rc = sprintf(buf, "%d\n", bd->ops->get_brightness(bd));
	else
		rc = sprintf(buf, "%d\n", bd->props.brightness);
	mutex_unlock(&bd->ops_lock);

	return rc;
}
static DEVICE_ATTR_RO(actual_brightness);

static struct class *backlight_class;

#ifdef CONFIG_PM_SLEEP
static int backlight_suspend(struct device *dev)
{
	struct backlight_device *bd = to_backlight_device(dev);

	mutex_lock(&bd->ops_lock);
	if (bd->ops && bd->ops->options & BL_CORE_SUSPENDRESUME) {
		bd->props.state |= BL_CORE_SUSPENDED;
		backlight_update_status(bd);
	}
	mutex_unlock(&bd->ops_lock);

	return 0;
}

static int backlight_resume(struct device *dev)
{
	struct backlight_device *bd = to_backlight_device(dev);

	mutex_lock(&bd->ops_lock);
	if (bd->ops && bd->ops->options & BL_CORE_SUSPENDRESUME) {
		bd->props.state &= ~BL_CORE_SUSPENDED;
		backlight_update_status(bd);
	}
	mutex_unlock(&bd->ops_lock);

	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(backlight_class_dev_pm_ops, backlight_suspend,
			 backlight_resume);

static void bl_device_release(struct device *dev)
{
	struct backlight_device *bd = to_backlight_device(dev);
	kfree(bd);
}

static struct attribute *bl_device_attrs[] = {
	&dev_attr_bl_power.attr,
	&dev_attr_brightness.attr,
	&dev_attr_actual_brightness.attr,
	&dev_attr_max_brightness.attr,
	&dev_attr_type.attr,
	NULL,
};
ATTRIBUTE_GROUPS(bl_device);

/**
 * backlight_force_update - tell the backlight subsystem that hardware state
 *   has changed
 * @bd: the backlight device to update
 *
 * Updates the internal state of the backlight in response to a hardware event,
 * and generate a uevent to notify userspace
 */
void backlight_force_update(struct backlight_device *bd,
			    enum backlight_update_reason reason)
{
	mutex_lock(&bd->ops_lock);
	if (bd->ops && bd->ops->get_brightness)
		bd->props.brightness = bd->ops->get_brightness(bd);
	mutex_unlock(&bd->ops_lock);
	backlight_generate_event(bd, reason);
}
EXPORT_SYMBOL(backlight_force_update);

/**
 * backlight_device_register - create and register a new object of
 *   backlight_device class.
 * @name: the name of the new object(must be the same as the name of the
 *   respective framebuffer device).
 * @parent: a pointer to the parent device
 * @devdata: an optional pointer to be stored for private driver use. The
 *   methods may retrieve it by using bl_get_data(bd).
 * @ops: the backlight operations structure.
 *
 * Creates and registers new backlight device. Returns either an
 * ERR_PTR() or a pointer to the newly allocated device.
 */
struct backlight_device *backlight_device_register(const char *name,
	struct device *parent, void *devdata, const struct backlight_ops *ops,
	const struct backlight_properties *props)
{
	struct backlight_device *new_bd;
	int rc;

	pr_debug("backlight_device_register: name=%s\n", name);

	new_bd = kzalloc(sizeof(struct backlight_device), GFP_KERNEL);
	if (!new_bd)
		return ERR_PTR(-ENOMEM);

	mutex_init(&new_bd->update_lock);
	mutex_init(&new_bd->ops_lock);

	new_bd->dev.class = backlight_class;
	new_bd->dev.parent = parent;
	new_bd->dev.release = bl_device_release;
	dev_set_name(&new_bd->dev, "%s", name);
	dev_set_drvdata(&new_bd->dev, devdata);

	/* Set default properties */
	if (props) {
		memcpy(&new_bd->props, props,
		       sizeof(struct backlight_properties));
		if (props->type <= 0 || props->type >= BACKLIGHT_TYPE_MAX) {
			WARN(1, "%s: invalid backlight type", name);
			new_bd->props.type = BACKLIGHT_RAW;
		}
	} else {
		new_bd->props.type = BACKLIGHT_RAW;
	}

	rc = device_register(&new_bd->dev);
	if (rc) {
		put_device(&new_bd->dev);
		return ERR_PTR(rc);
	}

	rc = backlight_register_fb(new_bd);
	if (rc) {
		device_unregister(&new_bd->dev);
		return ERR_PTR(rc);
	}

	new_bd->ops = ops;

#ifdef CONFIG_PMAC_BACKLIGHT
	mutex_lock(&pmac_backlight_mutex);
	if (!pmac_backlight)
		pmac_backlight = new_bd;
	mutex_unlock(&pmac_backlight_mutex);
#endif

	mutex_lock(&backlight_dev_list_mutex);
	list_add(&new_bd->entry, &backlight_dev_list);
	mutex_unlock(&backlight_dev_list_mutex);

	blocking_notifier_call_chain(&backlight_notifier,
				     BACKLIGHT_REGISTERED, new_bd);

	return new_bd;
}
EXPORT_SYMBOL(backlight_device_register);

struct backlight_device *backlight_device_get_by_type(enum backlight_type type)
{
	bool found = false;
	struct backlight_device *bd;

	mutex_lock(&backlight_dev_list_mutex);
	list_for_each_entry(bd, &backlight_dev_list, entry) {
		if (bd->props.type == type) {
			found = true;
			break;
		}
	}
	mutex_unlock(&backlight_dev_list_mutex);

	return found ? bd : NULL;
}
EXPORT_SYMBOL(backlight_device_get_by_type);

/**
 * backlight_device_unregister - unregisters a backlight device object.
 * @bd: the backlight device object to be unregistered and freed.
 *
 * Unregisters a previously registered via backlight_device_register object.
 */
void backlight_device_unregister(struct backlight_device *bd)
{
	if (!bd)
		return;

	mutex_lock(&backlight_dev_list_mutex);
	list_del(&bd->entry);
	mutex_unlock(&backlight_dev_list_mutex);

#ifdef CONFIG_PMAC_BACKLIGHT
	mutex_lock(&pmac_backlight_mutex);
	if (pmac_backlight == bd)
		pmac_backlight = NULL;
	mutex_unlock(&pmac_backlight_mutex);
#endif

	blocking_notifier_call_chain(&backlight_notifier,
				     BACKLIGHT_UNREGISTERED, bd);

	mutex_lock(&bd->ops_lock);
	bd->ops = NULL;
	mutex_unlock(&bd->ops_lock);

	backlight_unregister_fb(bd);
	device_unregister(&bd->dev);
}
EXPORT_SYMBOL(backlight_device_unregister);

static void devm_backlight_device_release(struct device *dev, void *res)
{
	struct backlight_device *backlight = *(struct backlight_device **)res;

	backlight_device_unregister(backlight);
}

static int devm_backlight_device_match(struct device *dev, void *res,
					void *data)
{
	struct backlight_device **r = res;

	return *r == data;
}

/**
 * backlight_register_notifier - get notified of backlight (un)registration
 * @nb: notifier block with the notifier to call on backlight (un)registration
 *
 * @return 0 on success, otherwise a negative error code
 *
 * Register a notifier to get notified when backlight devices get registered
 * or unregistered.
 */
int backlight_register_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_register(&backlight_notifier, nb);
}
EXPORT_SYMBOL(backlight_register_notifier);

/**
 * backlight_unregister_notifier - unregister a backlight notifier
 * @nb: notifier block to unregister
 *
 * @return 0 on success, otherwise a negative error code
 *
 * Register a notifier to get notified when backlight devices get registered
 * or unregistered.
 */
int backlight_unregister_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_unregister(&backlight_notifier, nb);
}
EXPORT_SYMBOL(backlight_unregister_notifier);

/**
 * devm_backlight_device_register - resource managed backlight_device_register()
 * @dev: the device to register
 * @name: the name of the device
 * @parent: a pointer to the parent device
 * @devdata: an optional pointer to be stored for private driver use
 * @ops: the backlight operations structure
 * @props: the backlight properties
 *
 * @return a struct backlight on success, or an ERR_PTR on error
 *
 * Managed backlight_device_register(). The backlight_device returned
 * from this function are automatically freed on driver detach.
 * See backlight_device_register() for more information.
 */
struct backlight_device *devm_backlight_device_register(struct device *dev,
	const char *name, struct device *parent, void *devdata,
	const struct backlight_ops *ops,
	const struct backlight_properties *props)
{
	struct backlight_device **ptr, *backlight;

	ptr = devres_alloc(devm_backlight_device_release, sizeof(*ptr),
			GFP_KERNEL);
	if (!ptr)
		return ERR_PTR(-ENOMEM);

	backlight = backlight_device_register(name, parent, devdata, ops,
						props);
	if (!IS_ERR(backlight)) {
		*ptr = backlight;
		devres_add(dev, ptr);
	} else {
		devres_free(ptr);
	}

	return backlight;
}
EXPORT_SYMBOL(devm_backlight_device_register);

/**
 * devm_backlight_device_unregister - resource managed backlight_device_unregister()
 * @dev: the device to unregister
 * @bd: the backlight device to unregister
 *
 * Deallocated a backlight allocated with devm_backlight_device_register().
 * Normally this function will not need to be called and the resource management
 * code will ensure that the resource is freed.
 */
void devm_backlight_device_unregister(struct device *dev,
				struct backlight_device *bd)
{
	int rc;

	rc = devres_release(dev, devm_backlight_device_release,
				devm_backlight_device_match, bd);
	WARN_ON(rc);
}
EXPORT_SYMBOL(devm_backlight_device_unregister);

#ifdef CONFIG_OF
static int of_parent_match(struct device *dev, const void *data)
{
	return dev->parent && dev->parent->of_node == data;
}

/**
 * of_find_backlight_by_node() - find backlight device by device-tree node
 * @node: device-tree node of the backlight device
 *
 * Returns a pointer to the backlight device corresponding to the given DT
 * node or NULL if no such backlight device exists or if the device hasn't
 * been probed yet.
 *
 * This function obtains a reference on the backlight device and it is the
 * caller's responsibility to drop the reference by calling put_device() on
 * the backlight device's .dev field.
 */
struct backlight_device *of_find_backlight_by_node(struct device_node *node)
{
	struct device *dev;

	dev = class_find_device(backlight_class, NULL, node, of_parent_match);

	return dev ? to_backlight_device(dev) : NULL;
}
EXPORT_SYMBOL(of_find_backlight_by_node);
#endif

static void __exit backlight_class_exit(void)
{
	class_destroy(backlight_class);
}

static int __init backlight_class_init(void)
{
	backlight_class = class_create(THIS_MODULE, "backlight");
	if (IS_ERR(backlight_class)) {
		pr_warn("Unable to create backlight class; errno = %ld\n",
			PTR_ERR(backlight_class));
		return PTR_ERR(backlight_class);
	}

	backlight_class->dev_groups = bl_device_groups;
	backlight_class->pm = &backlight_class_dev_pm_ops;
	INIT_LIST_HEAD(&backlight_dev_list);
	mutex_init(&backlight_dev_list_mutex);
	BLOCKING_INIT_NOTIFIER_HEAD(&backlight_notifier);

	return 0;
}

/*
 * if this is compiled into the kernel, we need to ensure that the
 * class is registered before users of the class try to register lcd's
 */
postcore_initcall(backlight_class_init);
module_exit(backlight_class_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jamey Hicks <jamey.hicks@hp.com>, Andrew Zabolotny <zap@homelink.ru>");
MODULE_DESCRIPTION("Backlight Lowlevel Control Abstraction");

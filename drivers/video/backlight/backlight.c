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

static const char *const backlight_types[] = {
	[BACKLIGHT_RAW] = "raw",
	[BACKLIGHT_PLATFORM] = "platform",
	[BACKLIGHT_FIRMWARE] = "firmware",
};

#if defined(CONFIG_FB) || (defined(CONFIG_FB_MODULE) && \
			   defined(CONFIG_BACKLIGHT_CLASS_DEVICE_MODULE))
/* This callback gets called when something important happens inside a
 * framebuffer driver. We're looking if that important event is blanking,
 * and if it is, we're switching backlight power as well ...
 */
static int fb_notifier_callback(struct notifier_block *self,
				unsigned long event, void *data)
{
	struct backlight_device *bd;
	struct fb_event *evdata = data;

	/* If we aren't interested in this event, skip it immediately ... */
	if (event != FB_EVENT_BLANK && event != FB_EVENT_CONBLANK)
		return 0;

	bd = container_of(self, struct backlight_device, fb_notif);
	mutex_lock(&bd->ops_lock);
	if (bd->ops)
		if (!bd->ops->check_fb ||
		    bd->ops->check_fb(bd, evdata->info)) {
			bd->props.fb_blank = *(int *)evdata->data;
			if (bd->props.fb_blank == FB_BLANK_UNBLANK)
				bd->props.state &= ~BL_CORE_FBBLANK;
			else
				bd->props.state |= BL_CORE_FBBLANK;
			backlight_update_status(bd);
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

static ssize_t backlight_show_power(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct backlight_device *bd = to_backlight_device(dev);

	return sprintf(buf, "%d\n", bd->props.power);
}

static ssize_t backlight_store_power(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
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

static ssize_t backlight_show_brightness(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct backlight_device *bd = to_backlight_device(dev);

	return sprintf(buf, "%d\n", bd->props.brightness);
}

static ssize_t backlight_store_brightness(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	int rc;
	struct backlight_device *bd = to_backlight_device(dev);
	unsigned long brightness;

	rc = kstrtoul(buf, 0, &brightness);
	if (rc)
		return rc;

	rc = -ENXIO;

	mutex_lock(&bd->ops_lock);
	if (bd->ops) {
		if (brightness > bd->props.max_brightness)
			rc = -EINVAL;
		else {
			pr_debug("set brightness to %lu\n", brightness);
			bd->props.brightness = brightness;
			backlight_update_status(bd);
			rc = count;
		}
	}
	mutex_unlock(&bd->ops_lock);

	backlight_generate_event(bd, BACKLIGHT_UPDATE_SYSFS);

	return rc;
}

static ssize_t backlight_show_type(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct backlight_device *bd = to_backlight_device(dev);

	return sprintf(buf, "%s\n", backlight_types[bd->props.type]);
}

static ssize_t backlight_show_max_brightness(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct backlight_device *bd = to_backlight_device(dev);

	return sprintf(buf, "%d\n", bd->props.max_brightness);
}

static ssize_t backlight_show_actual_brightness(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int rc = -ENXIO;
	struct backlight_device *bd = to_backlight_device(dev);

	mutex_lock(&bd->ops_lock);
	if (bd->ops && bd->ops->get_brightness)
		rc = sprintf(buf, "%d\n", bd->ops->get_brightness(bd));
	mutex_unlock(&bd->ops_lock);

	return rc;
}

static struct class *backlight_class;

static int backlight_suspend(struct device *dev, pm_message_t state)
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

static void bl_device_release(struct device *dev)
{
	struct backlight_device *bd = to_backlight_device(dev);
	kfree(bd);
}

static struct device_attribute bl_device_attributes[] = {
	__ATTR(bl_power, 0644, backlight_show_power, backlight_store_power),
	__ATTR(brightness, 0644, backlight_show_brightness,
		     backlight_store_brightness),
	__ATTR(actual_brightness, 0444, backlight_show_actual_brightness,
		     NULL),
	__ATTR(max_brightness, 0444, backlight_show_max_brightness, NULL),
	__ATTR(type, 0444, backlight_show_type, NULL),
	__ATTR_NULL,
};

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
		kfree(new_bd);
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

	return new_bd;
}
EXPORT_SYMBOL(backlight_device_register);

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

#ifdef CONFIG_PMAC_BACKLIGHT
	mutex_lock(&pmac_backlight_mutex);
	if (pmac_backlight == bd)
		pmac_backlight = NULL;
	mutex_unlock(&pmac_backlight_mutex);
#endif
	mutex_lock(&bd->ops_lock);
	bd->ops = NULL;
	mutex_unlock(&bd->ops_lock);

	backlight_unregister_fb(bd);
	device_unregister(&bd->dev);
}
EXPORT_SYMBOL(backlight_device_unregister);

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

	backlight_class->dev_attrs = bl_device_attributes;
	backlight_class->suspend = backlight_suspend;
	backlight_class->resume = backlight_resume;
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

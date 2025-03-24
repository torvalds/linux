// SPDX-License-Identifier: GPL-2.0-only
/*
 * LCD Lowlevel Control Abstraction
 *
 * Copyright (C) 2003,2004 Hewlett-Packard Company
 *
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/lcd.h>
#include <linux/notifier.h>
#include <linux/ctype.h>
#include <linux/err.h>
#include <linux/fb.h>
#include <linux/slab.h>

#if defined(CONFIG_FB) || (defined(CONFIG_FB_MODULE) && \
			   defined(CONFIG_LCD_CLASS_DEVICE_MODULE))
static int to_lcd_power(int fb_blank)
{
	switch (fb_blank) {
	case FB_BLANK_UNBLANK:
		return LCD_POWER_ON;
	/* deprecated; TODO: should become 'off' */
	case FB_BLANK_NORMAL:
		return LCD_POWER_REDUCED;
	case FB_BLANK_VSYNC_SUSPEND:
		return LCD_POWER_REDUCED_VSYNC_SUSPEND;
	/* 'off' */
	case FB_BLANK_HSYNC_SUSPEND:
	case FB_BLANK_POWERDOWN:
	default:
		return LCD_POWER_OFF;
	}
}

/* This callback gets called when something important happens inside a
 * framebuffer driver. We're looking if that important event is blanking,
 * and if it is, we're switching lcd power as well ...
 */
static int fb_notifier_callback(struct notifier_block *self,
				 unsigned long event, void *data)
{
	struct lcd_device *ld = container_of(self, struct lcd_device, fb_notif);
	struct fb_event *evdata = data;
	struct fb_info *info = evdata->info;
	struct lcd_device *fb_lcd = fb_lcd_device(info);

	guard(mutex)(&ld->ops_lock);

	if (!ld->ops)
		return 0;
	if (ld->ops->controls_device && !ld->ops->controls_device(ld, info->device))
		return 0;
	if (fb_lcd && fb_lcd != ld)
		return 0;

	if (event == FB_EVENT_BLANK) {
		int power = to_lcd_power(*(int *)evdata->data);

		if (ld->ops->set_power)
			ld->ops->set_power(ld, power);
	} else {
		const struct fb_videomode *videomode = evdata->data;

		if (ld->ops->set_mode)
			ld->ops->set_mode(ld, videomode->xres, videomode->yres);
	}

	return 0;
}

static int lcd_register_fb(struct lcd_device *ld)
{
	memset(&ld->fb_notif, 0, sizeof(ld->fb_notif));
	ld->fb_notif.notifier_call = fb_notifier_callback;
	return fb_register_client(&ld->fb_notif);
}

static void lcd_unregister_fb(struct lcd_device *ld)
{
	fb_unregister_client(&ld->fb_notif);
}
#else
static int lcd_register_fb(struct lcd_device *ld)
{
	return 0;
}

static inline void lcd_unregister_fb(struct lcd_device *ld)
{
}
#endif /* CONFIG_FB */

static ssize_t lcd_power_show(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	int rc;
	struct lcd_device *ld = to_lcd_device(dev);

	mutex_lock(&ld->ops_lock);
	if (ld->ops && ld->ops->get_power)
		rc = sprintf(buf, "%d\n", ld->ops->get_power(ld));
	else
		rc = -ENXIO;
	mutex_unlock(&ld->ops_lock);

	return rc;
}

static ssize_t lcd_power_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	int rc;
	struct lcd_device *ld = to_lcd_device(dev);
	unsigned long power;

	rc = kstrtoul(buf, 0, &power);
	if (rc)
		return rc;

	rc = -ENXIO;

	mutex_lock(&ld->ops_lock);
	if (ld->ops && ld->ops->set_power) {
		pr_debug("set power to %lu\n", power);
		ld->ops->set_power(ld, power);
		rc = count;
	}
	mutex_unlock(&ld->ops_lock);

	return rc;
}
static DEVICE_ATTR_RW(lcd_power);

static ssize_t contrast_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int rc = -ENXIO;
	struct lcd_device *ld = to_lcd_device(dev);

	mutex_lock(&ld->ops_lock);
	if (ld->ops && ld->ops->get_contrast)
		rc = sprintf(buf, "%d\n", ld->ops->get_contrast(ld));
	mutex_unlock(&ld->ops_lock);

	return rc;
}

static ssize_t contrast_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	int rc;
	struct lcd_device *ld = to_lcd_device(dev);
	unsigned long contrast;

	rc = kstrtoul(buf, 0, &contrast);
	if (rc)
		return rc;

	rc = -ENXIO;

	mutex_lock(&ld->ops_lock);
	if (ld->ops && ld->ops->set_contrast) {
		pr_debug("set contrast to %lu\n", contrast);
		ld->ops->set_contrast(ld, contrast);
		rc = count;
	}
	mutex_unlock(&ld->ops_lock);

	return rc;
}
static DEVICE_ATTR_RW(contrast);

static ssize_t max_contrast_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct lcd_device *ld = to_lcd_device(dev);

	return sprintf(buf, "%d\n", ld->props.max_contrast);
}
static DEVICE_ATTR_RO(max_contrast);

static void lcd_device_release(struct device *dev)
{
	struct lcd_device *ld = to_lcd_device(dev);
	kfree(ld);
}

static struct attribute *lcd_device_attrs[] = {
	&dev_attr_lcd_power.attr,
	&dev_attr_contrast.attr,
	&dev_attr_max_contrast.attr,
	NULL,
};
ATTRIBUTE_GROUPS(lcd_device);

static const struct class lcd_class = {
	.name = "lcd",
	.dev_groups = lcd_device_groups,
};

/**
 * lcd_device_register - register a new object of lcd_device class.
 * @name: the name of the new object(must be the same as the name of the
 *   respective framebuffer device).
 * @parent: pointer to the parent's struct device .
 * @devdata: an optional pointer to be stored in the device. The
 *   methods may retrieve it by using lcd_get_data(ld).
 * @ops: the lcd operations structure.
 *
 * Creates and registers a new lcd device. Returns either an ERR_PTR()
 * or a pointer to the newly allocated device.
 */
struct lcd_device *lcd_device_register(const char *name, struct device *parent,
		void *devdata, const struct lcd_ops *ops)
{
	struct lcd_device *new_ld;
	int rc;

	pr_debug("lcd_device_register: name=%s\n", name);

	new_ld = kzalloc(sizeof(struct lcd_device), GFP_KERNEL);
	if (!new_ld)
		return ERR_PTR(-ENOMEM);

	mutex_init(&new_ld->ops_lock);
	mutex_init(&new_ld->update_lock);

	new_ld->dev.class = &lcd_class;
	new_ld->dev.parent = parent;
	new_ld->dev.release = lcd_device_release;
	dev_set_name(&new_ld->dev, "%s", name);
	dev_set_drvdata(&new_ld->dev, devdata);

	new_ld->ops = ops;

	rc = device_register(&new_ld->dev);
	if (rc) {
		put_device(&new_ld->dev);
		return ERR_PTR(rc);
	}

	rc = lcd_register_fb(new_ld);
	if (rc) {
		device_unregister(&new_ld->dev);
		return ERR_PTR(rc);
	}

	return new_ld;
}
EXPORT_SYMBOL(lcd_device_register);

/**
 * lcd_device_unregister - unregisters a object of lcd_device class.
 * @ld: the lcd device object to be unregistered and freed.
 *
 * Unregisters a previously registered via lcd_device_register object.
 */
void lcd_device_unregister(struct lcd_device *ld)
{
	if (!ld)
		return;

	mutex_lock(&ld->ops_lock);
	ld->ops = NULL;
	mutex_unlock(&ld->ops_lock);
	lcd_unregister_fb(ld);

	device_unregister(&ld->dev);
}
EXPORT_SYMBOL(lcd_device_unregister);

static void devm_lcd_device_release(struct device *dev, void *res)
{
	struct lcd_device *lcd = *(struct lcd_device **)res;

	lcd_device_unregister(lcd);
}

static int devm_lcd_device_match(struct device *dev, void *res, void *data)
{
	struct lcd_device **r = res;

	return *r == data;
}

/**
 * devm_lcd_device_register - resource managed lcd_device_register()
 * @dev: the device to register
 * @name: the name of the device
 * @parent: a pointer to the parent device
 * @devdata: an optional pointer to be stored for private driver use
 * @ops: the lcd operations structure
 *
 * @return a struct lcd on success, or an ERR_PTR on error
 *
 * Managed lcd_device_register(). The lcd_device returned from this function
 * are automatically freed on driver detach. See lcd_device_register()
 * for more information.
 */
struct lcd_device *devm_lcd_device_register(struct device *dev,
		const char *name, struct device *parent,
		void *devdata, const struct lcd_ops *ops)
{
	struct lcd_device **ptr, *lcd;

	ptr = devres_alloc(devm_lcd_device_release, sizeof(*ptr), GFP_KERNEL);
	if (!ptr)
		return ERR_PTR(-ENOMEM);

	lcd = lcd_device_register(name, parent, devdata, ops);
	if (!IS_ERR(lcd)) {
		*ptr = lcd;
		devres_add(dev, ptr);
	} else {
		devres_free(ptr);
	}

	return lcd;
}
EXPORT_SYMBOL(devm_lcd_device_register);

/**
 * devm_lcd_device_unregister - resource managed lcd_device_unregister()
 * @dev: the device to unregister
 * @ld: the lcd device to unregister
 *
 * Deallocated a lcd allocated with devm_lcd_device_register(). Normally
 * this function will not need to be called and the resource management
 * code will ensure that the resource is freed.
 */
void devm_lcd_device_unregister(struct device *dev, struct lcd_device *ld)
{
	int rc;

	rc = devres_release(dev, devm_lcd_device_release,
				devm_lcd_device_match, ld);
	WARN_ON(rc);
}
EXPORT_SYMBOL(devm_lcd_device_unregister);


static void __exit lcd_class_exit(void)
{
	class_unregister(&lcd_class);
}

static int __init lcd_class_init(void)
{
	int ret;

	ret = class_register(&lcd_class);
	if (ret) {
		pr_warn("Unable to create backlight class; errno = %d\n", ret);
		return ret;
	}

	return 0;
}

/*
 * if this is compiled into the kernel, we need to ensure that the
 * class is registered before users of the class try to register lcd's
 */
postcore_initcall(lcd_class_init);
module_exit(lcd_class_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jamey Hicks <jamey.hicks@hp.com>, Andrew Zabolotny <zap@homelink.ru>");
MODULE_DESCRIPTION("LCD Lowlevel Control Abstraction");

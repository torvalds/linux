/*
 * LCD Lowlevel Control Abstraction
 *
 * Copyright (C) 2003,2004 Hewlett-Packard Company
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/lcd.h>
#include <linux/notifier.h>
#include <linux/ctype.h>
#include <linux/err.h>
#include <linux/fb.h>

#if defined(CONFIG_FB) || (defined(CONFIG_FB_MODULE) && \
			   defined(CONFIG_LCD_CLASS_DEVICE_MODULE))
/* This callback gets called when something important happens inside a
 * framebuffer driver. We're looking if that important event is blanking,
 * and if it is, we're switching lcd power as well ...
 */
static int fb_notifier_callback(struct notifier_block *self,
				 unsigned long event, void *data)
{
	struct lcd_device *ld;
	struct fb_event *evdata = data;

	/* If we aren't interested in this event, skip it immediately ... */
	if (event != FB_EVENT_BLANK)
		return 0;

	ld = container_of(self, struct lcd_device, fb_notif);
	mutex_lock(&ld->ops_lock);
	if (ld->ops)
		if (!ld->ops->check_fb || ld->ops->check_fb(evdata->info))
			ld->ops->set_power(ld, *(int *)evdata->data);
	mutex_unlock(&ld->ops_lock);
	return 0;
}

static int lcd_register_fb(struct lcd_device *ld)
{
	memset(&ld->fb_notif, 0, sizeof(&ld->fb_notif));
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

static ssize_t lcd_show_power(struct class_device *cdev, char *buf)
{
	int rc;
	struct lcd_device *ld = to_lcd_device(cdev);

	mutex_lock(&ld->ops_lock);
	if (ld->ops && ld->ops->get_power)
		rc = sprintf(buf, "%d\n", ld->ops->get_power(ld));
	else
		rc = -ENXIO;
	mutex_unlock(&ld->ops_lock);

	return rc;
}

static ssize_t lcd_store_power(struct class_device *cdev, const char *buf, size_t count)
{
	int rc = -ENXIO;
	char *endp;
	struct lcd_device *ld = to_lcd_device(cdev);
	int power = simple_strtoul(buf, &endp, 0);
	size_t size = endp - buf;

	if (*endp && isspace(*endp))
		size++;
	if (size != count)
		return -EINVAL;

	mutex_lock(&ld->ops_lock);
	if (ld->ops && ld->ops->set_power) {
		pr_debug("lcd: set power to %d\n", power);
		ld->ops->set_power(ld, power);
		rc = count;
	}
	mutex_unlock(&ld->ops_lock);

	return rc;
}

static ssize_t lcd_show_contrast(struct class_device *cdev, char *buf)
{
	int rc = -ENXIO;
	struct lcd_device *ld = to_lcd_device(cdev);

	mutex_lock(&ld->ops_lock);
	if (ld->ops && ld->ops->get_contrast)
		rc = sprintf(buf, "%d\n", ld->ops->get_contrast(ld));
	mutex_unlock(&ld->ops_lock);

	return rc;
}

static ssize_t lcd_store_contrast(struct class_device *cdev, const char *buf, size_t count)
{
	int rc = -ENXIO;
	char *endp;
	struct lcd_device *ld = to_lcd_device(cdev);
	int contrast = simple_strtoul(buf, &endp, 0);
	size_t size = endp - buf;

	if (*endp && isspace(*endp))
		size++;
	if (size != count)
		return -EINVAL;

	mutex_lock(&ld->ops_lock);
	if (ld->ops && ld->ops->set_contrast) {
		pr_debug("lcd: set contrast to %d\n", contrast);
		ld->ops->set_contrast(ld, contrast);
		rc = count;
	}
	mutex_unlock(&ld->ops_lock);

	return rc;
}

static ssize_t lcd_show_max_contrast(struct class_device *cdev, char *buf)
{
	struct lcd_device *ld = to_lcd_device(cdev);

	return sprintf(buf, "%d\n", ld->props.max_contrast);
}

static void lcd_class_release(struct class_device *dev)
{
	struct lcd_device *ld = to_lcd_device(dev);
	kfree(ld);
}

static struct class lcd_class = {
	.name = "lcd",
	.release = lcd_class_release,
};

#define DECLARE_ATTR(_name,_mode,_show,_store)			\
{							 	\
	.attr	= { .name = __stringify(_name), .mode = _mode, .owner = THIS_MODULE },	\
	.show	= _show,					\
	.store	= _store,					\
}

static const struct class_device_attribute lcd_class_device_attributes[] = {
	DECLARE_ATTR(power, 0644, lcd_show_power, lcd_store_power),
	DECLARE_ATTR(contrast, 0644, lcd_show_contrast, lcd_store_contrast),
	DECLARE_ATTR(max_contrast, 0444, lcd_show_max_contrast, NULL),
};

/**
 * lcd_device_register - register a new object of lcd_device class.
 * @name: the name of the new object(must be the same as the name of the
 *   respective framebuffer device).
 * @devdata: an optional pointer to be stored in the class_device. The
 *   methods may retrieve it by using class_get_devdata(ld->class_dev).
 * @ops: the lcd operations structure.
 *
 * Creates and registers a new lcd class_device. Returns either an ERR_PTR()
 * or a pointer to the newly allocated device.
 */
struct lcd_device *lcd_device_register(const char *name, void *devdata,
				       struct lcd_ops *ops)
{
	int i, rc;
	struct lcd_device *new_ld;

	pr_debug("lcd_device_register: name=%s\n", name);

	new_ld = kzalloc(sizeof(struct lcd_device), GFP_KERNEL);
	if (!new_ld)
		return ERR_PTR(-ENOMEM);

	mutex_init(&new_ld->ops_lock);
	mutex_init(&new_ld->update_lock);
	new_ld->ops = ops;
	new_ld->class_dev.class = &lcd_class;
	strlcpy(new_ld->class_dev.class_id, name, KOBJ_NAME_LEN);
	class_set_devdata(&new_ld->class_dev, devdata);

	rc = class_device_register(&new_ld->class_dev);
	if (rc) {
		kfree(new_ld);
		return ERR_PTR(rc);
	}

	rc = lcd_register_fb(new_ld);
	if (rc) {
		class_device_unregister(&new_ld->class_dev);
		return ERR_PTR(rc);
	}

	for (i = 0; i < ARRAY_SIZE(lcd_class_device_attributes); i++) {
		rc = class_device_create_file(&new_ld->class_dev,
					      &lcd_class_device_attributes[i]);
		if (rc) {
			while (--i >= 0)
				class_device_remove_file(&new_ld->class_dev,
							 &lcd_class_device_attributes[i]);
			class_device_unregister(&new_ld->class_dev);
			/* No need to kfree(new_ld) since release() method was called */
			return ERR_PTR(rc);
		}
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
	int i;

	if (!ld)
		return;

	pr_debug("lcd_device_unregister: name=%s\n", ld->class_dev.class_id);

	for (i = 0; i < ARRAY_SIZE(lcd_class_device_attributes); i++)
		class_device_remove_file(&ld->class_dev,
					 &lcd_class_device_attributes[i]);

	mutex_lock(&ld->ops_lock);
	ld->ops = NULL;
	mutex_unlock(&ld->ops_lock);
	lcd_unregister_fb(ld);
	class_device_unregister(&ld->class_dev);
}
EXPORT_SYMBOL(lcd_device_unregister);

static void __exit lcd_class_exit(void)
{
	class_unregister(&lcd_class);
}

static int __init lcd_class_init(void)
{
	return class_register(&lcd_class);
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

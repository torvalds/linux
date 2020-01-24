// SPDX-License-Identifier: GPL-2.0
/*
 * Fieldbus Device Driver Core
 *
 */

#include <linux/mutex.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/idr.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/poll.h>

/* move to <linux/fieldbus_dev.h> when taking this out of staging */
#include "fieldbus_dev.h"

/* Maximum number of fieldbus devices */
#define MAX_FIELDBUSES		32

/* the dev_t structure to store the dynamically allocated fieldbus devices */
static dev_t fieldbus_devt;
static DEFINE_IDA(fieldbus_ida);
static DEFINE_MUTEX(fieldbus_mtx);

static ssize_t online_show(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	struct fieldbus_dev *fb = dev_get_drvdata(dev);

	return sprintf(buf, "%d\n", !!fb->online);
}
static DEVICE_ATTR_RO(online);

static ssize_t enabled_show(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	struct fieldbus_dev *fb = dev_get_drvdata(dev);

	if (!fb->enable_get)
		return -EINVAL;
	return sprintf(buf, "%d\n", !!fb->enable_get(fb));
}

static ssize_t enabled_store(struct device *dev, struct device_attribute *attr,
			     const char *buf, size_t n)
{
	struct fieldbus_dev *fb = dev_get_drvdata(dev);
	bool value;
	int ret;

	if (!fb->simple_enable_set)
		return -ENOTSUPP;
	ret = kstrtobool(buf, &value);
	if (ret)
		return ret;
	ret = fb->simple_enable_set(fb, value);
	if (ret < 0)
		return ret;
	return n;
}
static DEVICE_ATTR_RW(enabled);

static ssize_t card_name_show(struct device *dev, struct device_attribute *attr,
			      char *buf)
{
	struct fieldbus_dev *fb = dev_get_drvdata(dev);

	/*
	 * card_name was provided by child driver, could potentially be long.
	 * protect against buffer overrun.
	 */
	return snprintf(buf, PAGE_SIZE, "%s\n", fb->card_name);
}
static DEVICE_ATTR_RO(card_name);

static ssize_t read_area_size_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct fieldbus_dev *fb = dev_get_drvdata(dev);

	return sprintf(buf, "%zu\n", fb->read_area_sz);
}
static DEVICE_ATTR_RO(read_area_size);

static ssize_t write_area_size_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct fieldbus_dev *fb = dev_get_drvdata(dev);

	return sprintf(buf, "%zu\n", fb->write_area_sz);
}
static DEVICE_ATTR_RO(write_area_size);

static ssize_t fieldbus_id_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct fieldbus_dev *fb = dev_get_drvdata(dev);

	return fb->fieldbus_id_get(fb, buf, PAGE_SIZE);
}
static DEVICE_ATTR_RO(fieldbus_id);

static ssize_t fieldbus_type_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct fieldbus_dev *fb = dev_get_drvdata(dev);
	const char *t;

	switch (fb->fieldbus_type) {
	case FIELDBUS_DEV_TYPE_PROFINET:
		t = "profinet";
		break;
	default:
		t = "unknown";
		break;
	}

	return sprintf(buf, "%s\n", t);
}
static DEVICE_ATTR_RO(fieldbus_type);

static struct attribute *fieldbus_attrs[] = {
	&dev_attr_enabled.attr,
	&dev_attr_card_name.attr,
	&dev_attr_fieldbus_id.attr,
	&dev_attr_read_area_size.attr,
	&dev_attr_write_area_size.attr,
	&dev_attr_online.attr,
	&dev_attr_fieldbus_type.attr,
	NULL,
};

static umode_t fieldbus_is_visible(struct kobject *kobj, struct attribute *attr,
				   int n)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct fieldbus_dev *fb = dev_get_drvdata(dev);
	umode_t mode = attr->mode;

	if (attr == &dev_attr_enabled.attr) {
		mode = 0;
		if (fb->enable_get)
			mode |= 0444;
		if (fb->simple_enable_set)
			mode |= 0200;
	}

	return mode;
}

static const struct attribute_group fieldbus_group = {
	.attrs = fieldbus_attrs,
	.is_visible = fieldbus_is_visible,
};
__ATTRIBUTE_GROUPS(fieldbus);

static struct class fieldbus_class = {
	.name =		"fieldbus_dev",
	.owner =	THIS_MODULE,
	.dev_groups =	fieldbus_groups,
};

struct fb_open_file {
	struct fieldbus_dev *fbdev;
	int dc_event;
};

static int fieldbus_open(struct inode *inode, struct file *filp)
{
	struct fb_open_file *of;
	struct fieldbus_dev *fbdev = container_of(inode->i_cdev,
						struct fieldbus_dev,
						cdev);

	of = kzalloc(sizeof(*of), GFP_KERNEL);
	if (!of)
		return -ENOMEM;
	of->fbdev = fbdev;
	filp->private_data = of;
	return 0;
}

static int fieldbus_release(struct inode *node, struct file *filp)
{
	struct fb_open_file *of = filp->private_data;

	kfree(of);
	return 0;
}

static ssize_t fieldbus_read(struct file *filp, char __user *buf, size_t size,
			     loff_t *offset)
{
	struct fb_open_file *of = filp->private_data;
	struct fieldbus_dev *fbdev = of->fbdev;

	of->dc_event = fbdev->dc_event;
	return fbdev->read_area(fbdev, buf, size, offset);
}

static ssize_t fieldbus_write(struct file *filp, const char __user *buf,
			      size_t size, loff_t *offset)
{
	struct fb_open_file *of = filp->private_data;
	struct fieldbus_dev *fbdev = of->fbdev;

	return fbdev->write_area(fbdev, buf, size, offset);
}

static __poll_t fieldbus_poll(struct file *filp, poll_table *wait)
{
	struct fb_open_file *of = filp->private_data;
	struct fieldbus_dev *fbdev = of->fbdev;
	__poll_t mask = EPOLLIN | EPOLLRDNORM | EPOLLOUT | EPOLLWRNORM;

	poll_wait(filp, &fbdev->dc_wq, wait);
	/* data changed ? */
	if (fbdev->dc_event != of->dc_event)
		mask |= EPOLLPRI | EPOLLERR;
	return mask;
}

static const struct file_operations fieldbus_fops = {
	.open		= fieldbus_open,
	.release	= fieldbus_release,
	.read		= fieldbus_read,
	.write		= fieldbus_write,
	.poll		= fieldbus_poll,
	.llseek		= generic_file_llseek,
	.owner		= THIS_MODULE,
};

void fieldbus_dev_area_updated(struct fieldbus_dev *fb)
{
	fb->dc_event++;
	wake_up_all(&fb->dc_wq);
}
EXPORT_SYMBOL_GPL(fieldbus_dev_area_updated);

void fieldbus_dev_online_changed(struct fieldbus_dev *fb, bool online)
{
	fb->online = online;
	kobject_uevent(&fb->dev->kobj, KOBJ_CHANGE);
}
EXPORT_SYMBOL_GPL(fieldbus_dev_online_changed);

static void __fieldbus_dev_unregister(struct fieldbus_dev *fb)
{
	if (!fb)
		return;
	device_destroy(&fieldbus_class, fb->cdev.dev);
	cdev_del(&fb->cdev);
	ida_simple_remove(&fieldbus_ida, fb->id);
}

void fieldbus_dev_unregister(struct fieldbus_dev *fb)
{
	mutex_lock(&fieldbus_mtx);
	__fieldbus_dev_unregister(fb);
	mutex_unlock(&fieldbus_mtx);
}
EXPORT_SYMBOL_GPL(fieldbus_dev_unregister);

static int __fieldbus_dev_register(struct fieldbus_dev *fb)
{
	dev_t devno;
	int err;

	if (!fb)
		return -EINVAL;
	if (!fb->read_area || !fb->write_area || !fb->fieldbus_id_get)
		return -EINVAL;
	fb->id = ida_simple_get(&fieldbus_ida, 0, MAX_FIELDBUSES, GFP_KERNEL);
	if (fb->id < 0)
		return fb->id;
	devno = MKDEV(MAJOR(fieldbus_devt), fb->id);
	init_waitqueue_head(&fb->dc_wq);
	cdev_init(&fb->cdev, &fieldbus_fops);
	err = cdev_add(&fb->cdev, devno, 1);
	if (err) {
		pr_err("fieldbus_dev%d unable to add device %d:%d\n",
		       fb->id, MAJOR(fieldbus_devt), fb->id);
		goto err_cdev;
	}
	fb->dev = device_create(&fieldbus_class, fb->parent, devno, fb,
				"fieldbus_dev%d", fb->id);
	if (IS_ERR(fb->dev)) {
		err = PTR_ERR(fb->dev);
		goto err_dev_create;
	}
	return 0;

err_dev_create:
	cdev_del(&fb->cdev);
err_cdev:
	ida_simple_remove(&fieldbus_ida, fb->id);
	return err;
}

int fieldbus_dev_register(struct fieldbus_dev *fb)
{
	int err;

	mutex_lock(&fieldbus_mtx);
	err = __fieldbus_dev_register(fb);
	mutex_unlock(&fieldbus_mtx);

	return err;
}
EXPORT_SYMBOL_GPL(fieldbus_dev_register);

static int __init fieldbus_init(void)
{
	int err;

	err = class_register(&fieldbus_class);
	if (err < 0) {
		pr_err("fieldbus_dev: could not register class\n");
		return err;
	}
	err = alloc_chrdev_region(&fieldbus_devt, 0,
				  MAX_FIELDBUSES, "fieldbus_dev");
	if (err < 0) {
		pr_err("fieldbus_dev: unable to allocate char dev region\n");
		goto err_alloc;
	}
	return 0;

err_alloc:
	class_unregister(&fieldbus_class);
	return err;
}

static void __exit fieldbus_exit(void)
{
	unregister_chrdev_region(fieldbus_devt, MAX_FIELDBUSES);
	class_unregister(&fieldbus_class);
	ida_destroy(&fieldbus_ida);
}

subsys_initcall(fieldbus_init);
module_exit(fieldbus_exit);

MODULE_AUTHOR("Sven Van Asbroeck <TheSven73@gmail.com>");
MODULE_AUTHOR("Jonathan Stiles <jonathans@arcx.com>");
MODULE_DESCRIPTION("Fieldbus Device Driver Core");
MODULE_LICENSE("GPL v2");

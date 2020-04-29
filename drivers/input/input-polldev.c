// SPDX-License-Identifier: GPL-2.0-only
/*
 * Generic implementation of a polled input device

 * Copyright (c) 2007 Dmitry Torokhov
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/jiffies.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/workqueue.h>
#include <linux/module.h>
#include <linux/input-polldev.h>

MODULE_AUTHOR("Dmitry Torokhov <dtor@mail.ru>");
MODULE_DESCRIPTION("Generic implementation of a polled input device");
MODULE_LICENSE("GPL v2");

static void input_polldev_queue_work(struct input_polled_dev *dev)
{
	unsigned long delay;

	delay = msecs_to_jiffies(dev->poll_interval);
	if (delay >= HZ)
		delay = round_jiffies_relative(delay);

	queue_delayed_work(system_freezable_wq, &dev->work, delay);
}

static void input_polled_device_work(struct work_struct *work)
{
	struct input_polled_dev *dev =
		container_of(work, struct input_polled_dev, work.work);

	dev->poll(dev);
	input_polldev_queue_work(dev);
}

static int input_open_polled_device(struct input_dev *input)
{
	struct input_polled_dev *dev = input_get_drvdata(input);

	if (dev->open)
		dev->open(dev);

	/* Only start polling if polling is enabled */
	if (dev->poll_interval > 0) {
		dev->poll(dev);
		input_polldev_queue_work(dev);
	}

	return 0;
}

static void input_close_polled_device(struct input_dev *input)
{
	struct input_polled_dev *dev = input_get_drvdata(input);

	cancel_delayed_work_sync(&dev->work);

	if (dev->close)
		dev->close(dev);
}

/* SYSFS interface */

static ssize_t input_polldev_get_poll(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	struct input_polled_dev *polldev = dev_get_drvdata(dev);

	return sprintf(buf, "%d\n", polldev->poll_interval);
}

static ssize_t input_polldev_set_poll(struct device *dev,
				struct device_attribute *attr, const char *buf,
				size_t count)
{
	struct input_polled_dev *polldev = dev_get_drvdata(dev);
	struct input_dev *input = polldev->input;
	unsigned int interval;
	int err;

	err = kstrtouint(buf, 0, &interval);
	if (err)
		return err;

	if (interval < polldev->poll_interval_min)
		return -EINVAL;

	if (interval > polldev->poll_interval_max)
		return -EINVAL;

	mutex_lock(&input->mutex);

	polldev->poll_interval = interval;

	if (input->users) {
		cancel_delayed_work_sync(&polldev->work);
		if (polldev->poll_interval > 0)
			input_polldev_queue_work(polldev);
	}

	mutex_unlock(&input->mutex);

	return count;
}

static DEVICE_ATTR(poll, S_IRUGO | S_IWUSR, input_polldev_get_poll,
					    input_polldev_set_poll);


static ssize_t input_polldev_get_max(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct input_polled_dev *polldev = dev_get_drvdata(dev);

	return sprintf(buf, "%d\n", polldev->poll_interval_max);
}

static DEVICE_ATTR(max, S_IRUGO, input_polldev_get_max, NULL);

static ssize_t input_polldev_get_min(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct input_polled_dev *polldev = dev_get_drvdata(dev);

	return sprintf(buf, "%d\n", polldev->poll_interval_min);
}

static DEVICE_ATTR(min, S_IRUGO, input_polldev_get_min, NULL);

static struct attribute *sysfs_attrs[] = {
	&dev_attr_poll.attr,
	&dev_attr_max.attr,
	&dev_attr_min.attr,
	NULL
};

static struct attribute_group input_polldev_attribute_group = {
	.attrs = sysfs_attrs
};

static const struct attribute_group *input_polldev_attribute_groups[] = {
	&input_polldev_attribute_group,
	NULL
};

/**
 * input_allocate_polled_device - allocate memory for polled device
 *
 * The function allocates memory for a polled device and also
 * for an input device associated with this polled device.
 */
struct input_polled_dev *input_allocate_polled_device(void)
{
	struct input_polled_dev *dev;

	dev = kzalloc(sizeof(struct input_polled_dev), GFP_KERNEL);
	if (!dev)
		return NULL;

	dev->input = input_allocate_device();
	if (!dev->input) {
		kfree(dev);
		return NULL;
	}

	return dev;
}
EXPORT_SYMBOL(input_allocate_polled_device);

struct input_polled_devres {
	struct input_polled_dev *polldev;
};

static int devm_input_polldev_match(struct device *dev, void *res, void *data)
{
	struct input_polled_devres *devres = res;

	return devres->polldev == data;
}

static void devm_input_polldev_release(struct device *dev, void *res)
{
	struct input_polled_devres *devres = res;
	struct input_polled_dev *polldev = devres->polldev;

	dev_dbg(dev, "%s: dropping reference/freeing %s\n",
		__func__, dev_name(&polldev->input->dev));

	input_put_device(polldev->input);
	kfree(polldev);
}

static void devm_input_polldev_unregister(struct device *dev, void *res)
{
	struct input_polled_devres *devres = res;
	struct input_polled_dev *polldev = devres->polldev;

	dev_dbg(dev, "%s: unregistering device %s\n",
		__func__, dev_name(&polldev->input->dev));
	input_unregister_device(polldev->input);

	/*
	 * Note that we are still holding extra reference to the input
	 * device so it will stick around until devm_input_polldev_release()
	 * is called.
	 */
}

/**
 * devm_input_allocate_polled_device - allocate managed polled device
 * @dev: device owning the polled device being created
 *
 * Returns prepared &struct input_polled_dev or %NULL.
 *
 * Managed polled input devices do not need to be explicitly unregistered
 * or freed as it will be done automatically when owner device unbinds
 * from * its driver (or binding fails). Once such managed polled device
 * is allocated, it is ready to be set up and registered in the same
 * fashion as regular polled input devices (using
 * input_register_polled_device() function).
 *
 * If you want to manually unregister and free such managed polled devices,
 * it can be still done by calling input_unregister_polled_device() and
 * input_free_polled_device(), although it is rarely needed.
 *
 * NOTE: the owner device is set up as parent of input device and users
 * should not override it.
 */
struct input_polled_dev *devm_input_allocate_polled_device(struct device *dev)
{
	struct input_polled_dev *polldev;
	struct input_polled_devres *devres;

	devres = devres_alloc(devm_input_polldev_release, sizeof(*devres),
			      GFP_KERNEL);
	if (!devres)
		return NULL;

	polldev = input_allocate_polled_device();
	if (!polldev) {
		devres_free(devres);
		return NULL;
	}

	polldev->input->dev.parent = dev;
	polldev->devres_managed = true;

	devres->polldev = polldev;
	devres_add(dev, devres);

	return polldev;
}
EXPORT_SYMBOL(devm_input_allocate_polled_device);

/**
 * input_free_polled_device - free memory allocated for polled device
 * @dev: device to free
 *
 * The function frees memory allocated for polling device and drops
 * reference to the associated input device.
 */
void input_free_polled_device(struct input_polled_dev *dev)
{
	if (dev) {
		if (dev->devres_managed)
			WARN_ON(devres_destroy(dev->input->dev.parent,
						devm_input_polldev_release,
						devm_input_polldev_match,
						dev));
		input_put_device(dev->input);
		kfree(dev);
	}
}
EXPORT_SYMBOL(input_free_polled_device);

/**
 * input_register_polled_device - register polled device
 * @dev: device to register
 *
 * The function registers previously initialized polled input device
 * with input layer. The device should be allocated with call to
 * input_allocate_polled_device(). Callers should also set up poll()
 * method and set up capabilities (id, name, phys, bits) of the
 * corresponding input_dev structure.
 */
int input_register_polled_device(struct input_polled_dev *dev)
{
	struct input_polled_devres *devres = NULL;
	struct input_dev *input = dev->input;
	int error;

	if (dev->devres_managed) {
		devres = devres_alloc(devm_input_polldev_unregister,
				      sizeof(*devres), GFP_KERNEL);
		if (!devres)
			return -ENOMEM;

		devres->polldev = dev;
	}

	input_set_drvdata(input, dev);
	INIT_DELAYED_WORK(&dev->work, input_polled_device_work);

	if (!dev->poll_interval)
		dev->poll_interval = 500;
	if (!dev->poll_interval_max)
		dev->poll_interval_max = dev->poll_interval;

	input->open = input_open_polled_device;
	input->close = input_close_polled_device;

	input->dev.groups = input_polldev_attribute_groups;

	error = input_register_device(input);
	if (error) {
		devres_free(devres);
		return error;
	}

	/*
	 * Take extra reference to the underlying input device so
	 * that it survives call to input_unregister_polled_device()
	 * and is deleted only after input_free_polled_device()
	 * has been invoked. This is needed to ease task of freeing
	 * sparse keymaps.
	 */
	input_get_device(input);

	if (dev->devres_managed) {
		dev_dbg(input->dev.parent, "%s: registering %s with devres.\n",
			__func__, dev_name(&input->dev));
		devres_add(input->dev.parent, devres);
	}

	return 0;
}
EXPORT_SYMBOL(input_register_polled_device);

/**
 * input_unregister_polled_device - unregister polled device
 * @dev: device to unregister
 *
 * The function unregisters previously registered polled input
 * device from input layer. Polling is stopped and device is
 * ready to be freed with call to input_free_polled_device().
 */
void input_unregister_polled_device(struct input_polled_dev *dev)
{
	if (dev->devres_managed)
		WARN_ON(devres_destroy(dev->input->dev.parent,
					devm_input_polldev_unregister,
					devm_input_polldev_match,
					dev));

	input_unregister_device(dev->input);
}
EXPORT_SYMBOL(input_unregister_polled_device);

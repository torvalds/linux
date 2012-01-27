/******************************************************************************
 * Xen balloon driver - enables returning/claiming memory to/from Xen.
 *
 * Copyright (c) 2003, B Dragovic
 * Copyright (c) 2003-2004, M Williamson, K Fraser
 * Copyright (c) 2005 Dan M. Smith, IBM Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation; or, when distributed
 * separately from the Linux kernel or incorporated into other
 * software packages, subject to the following license:
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this source file (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy, modify,
 * merge, publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/capability.h>

#include <xen/xen.h>
#include <xen/interface/xen.h>
#include <xen/balloon.h>
#include <xen/xenbus.h>
#include <xen/features.h>
#include <xen/page.h>

#define PAGES2KB(_p) ((_p)<<(PAGE_SHIFT-10))

#define BALLOON_CLASS_NAME "xen_memory"

static struct device balloon_dev;

static int register_balloon(struct device *dev);

/* React to a change in the target key */
static void watch_target(struct xenbus_watch *watch,
			 const char **vec, unsigned int len)
{
	unsigned long long new_target;
	int err;

	err = xenbus_scanf(XBT_NIL, "memory", "target", "%llu", &new_target);
	if (err != 1) {
		/* This is ok (for domain0 at least) - so just return */
		return;
	}

	/* The given memory/target value is in KiB, so it needs converting to
	 * pages. PAGE_SHIFT converts bytes to pages, hence PAGE_SHIFT - 10.
	 */
	balloon_set_new_target(new_target >> (PAGE_SHIFT - 10));
}
static struct xenbus_watch target_watch = {
	.node = "memory/target",
	.callback = watch_target,
};


static int balloon_init_watcher(struct notifier_block *notifier,
				unsigned long event,
				void *data)
{
	int err;

	err = register_xenbus_watch(&target_watch);
	if (err)
		printk(KERN_ERR "Failed to set balloon watcher\n");

	return NOTIFY_DONE;
}

static struct notifier_block xenstore_notifier = {
	.notifier_call = balloon_init_watcher,
};

static int __init balloon_init(void)
{
	if (!xen_domain())
		return -ENODEV;

	pr_info("xen-balloon: Initialising balloon driver.\n");

	register_balloon(&balloon_dev);

	register_xen_selfballooning(&balloon_dev);

	register_xenstore_notifier(&xenstore_notifier);

	return 0;
}
subsys_initcall(balloon_init);

static void balloon_exit(void)
{
    /* XXX - release balloon here */
    return;
}

module_exit(balloon_exit);

#define BALLOON_SHOW(name, format, args...)				\
	static ssize_t show_##name(struct device *dev,			\
				   struct device_attribute *attr,	\
				   char *buf)				\
	{								\
		return sprintf(buf, format, ##args);			\
	}								\
	static DEVICE_ATTR(name, S_IRUGO, show_##name, NULL)

BALLOON_SHOW(current_kb, "%lu\n", PAGES2KB(balloon_stats.current_pages));
BALLOON_SHOW(low_kb, "%lu\n", PAGES2KB(balloon_stats.balloon_low));
BALLOON_SHOW(high_kb, "%lu\n", PAGES2KB(balloon_stats.balloon_high));

static DEVICE_ULONG_ATTR(schedule_delay, 0444, balloon_stats.schedule_delay);
static DEVICE_ULONG_ATTR(max_schedule_delay, 0644, balloon_stats.max_schedule_delay);
static DEVICE_ULONG_ATTR(retry_count, 0444, balloon_stats.retry_count);
static DEVICE_ULONG_ATTR(max_retry_count, 0644, balloon_stats.max_retry_count);

static ssize_t show_target_kb(struct device *dev, struct device_attribute *attr,
			      char *buf)
{
	return sprintf(buf, "%lu\n", PAGES2KB(balloon_stats.target_pages));
}

static ssize_t store_target_kb(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf,
			       size_t count)
{
	char *endchar;
	unsigned long long target_bytes;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	target_bytes = simple_strtoull(buf, &endchar, 0) * 1024;

	balloon_set_new_target(target_bytes >> PAGE_SHIFT);

	return count;
}

static DEVICE_ATTR(target_kb, S_IRUGO | S_IWUSR,
		   show_target_kb, store_target_kb);


static ssize_t show_target(struct device *dev, struct device_attribute *attr,
			      char *buf)
{
	return sprintf(buf, "%llu\n",
		       (unsigned long long)balloon_stats.target_pages
		       << PAGE_SHIFT);
}

static ssize_t store_target(struct device *dev,
			    struct device_attribute *attr,
			    const char *buf,
			    size_t count)
{
	char *endchar;
	unsigned long long target_bytes;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	target_bytes = memparse(buf, &endchar);

	balloon_set_new_target(target_bytes >> PAGE_SHIFT);

	return count;
}

static DEVICE_ATTR(target, S_IRUGO | S_IWUSR,
		   show_target, store_target);


static struct device_attribute *balloon_attrs[] = {
	&dev_attr_target_kb,
	&dev_attr_target,
	&dev_attr_schedule_delay.attr,
	&dev_attr_max_schedule_delay.attr,
	&dev_attr_retry_count.attr,
	&dev_attr_max_retry_count.attr
};

static struct attribute *balloon_info_attrs[] = {
	&dev_attr_current_kb.attr,
	&dev_attr_low_kb.attr,
	&dev_attr_high_kb.attr,
	NULL
};

static struct attribute_group balloon_info_group = {
	.name = "info",
	.attrs = balloon_info_attrs
};

static struct bus_type balloon_subsys = {
	.name = BALLOON_CLASS_NAME,
	.dev_name = BALLOON_CLASS_NAME,
};

static int register_balloon(struct device *dev)
{
	int i, error;

	error = subsys_system_register(&balloon_subsys, NULL);
	if (error)
		return error;

	dev->id = 0;
	dev->bus = &balloon_subsys;

	error = device_register(dev);
	if (error) {
		bus_unregister(&balloon_subsys);
		return error;
	}

	for (i = 0; i < ARRAY_SIZE(balloon_attrs); i++) {
		error = device_create_file(dev, balloon_attrs[i]);
		if (error)
			goto fail;
	}

	error = sysfs_create_group(&dev->kobj, &balloon_info_group);
	if (error)
		goto fail;

	return 0;

 fail:
	while (--i >= 0)
		device_remove_file(dev, balloon_attrs[i]);
	device_unregister(dev);
	bus_unregister(&balloon_subsys);
	return error;
}

MODULE_LICENSE("GPL");

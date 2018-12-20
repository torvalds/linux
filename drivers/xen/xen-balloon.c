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

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/mm_types.h>
#include <linux/init.h>
#include <linux/capability.h>

#include <xen/xen.h>
#include <xen/interface/xen.h>
#include <xen/balloon.h>
#include <xen/xenbus.h>
#include <xen/features.h>
#include <xen/page.h>
#include <xen/mem-reservation.h>

#define PAGES2KB(_p) ((_p)<<(PAGE_SHIFT-10))

#define BALLOON_CLASS_NAME "xen_memory"

static struct device balloon_dev;

static int register_balloon(struct device *dev);

/* React to a change in the target key */
static void watch_target(struct xenbus_watch *watch,
			 const char *path, const char *token)
{
	unsigned long long new_target, static_max;
	int err;
	static bool watch_fired;
	static long target_diff;

	err = xenbus_scanf(XBT_NIL, "memory", "target", "%llu", &new_target);
	if (err != 1) {
		/* This is ok (for domain0 at least) - so just return */
		return;
	}

	/* The given memory/target value is in KiB, so it needs converting to
	 * pages. PAGE_SHIFT converts bytes to pages, hence PAGE_SHIFT - 10.
	 */
	new_target >>= PAGE_SHIFT - 10;

	if (!watch_fired) {
		watch_fired = true;

		if ((xenbus_scanf(XBT_NIL, "memory", "static-max",
				  "%llu", &static_max) == 1) ||
		    (xenbus_scanf(XBT_NIL, "memory", "memory_static_max",
				  "%llu", &static_max) == 1))
			static_max >>= PAGE_SHIFT - 10;
		else
			static_max = new_target;

		target_diff = (xen_pv_domain() || xen_initial_domain()) ? 0
				: static_max - balloon_stats.target_pages;
	}

	balloon_set_new_target(new_target - target_diff);
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
		pr_err("Failed to set balloon watcher\n");

	return NOTIFY_DONE;
}

static struct notifier_block xenstore_notifier = {
	.notifier_call = balloon_init_watcher,
};

void xen_balloon_init(void)
{
	register_balloon(&balloon_dev);

	register_xen_selfballooning(&balloon_dev);

	register_xenstore_notifier(&xenstore_notifier);
}
EXPORT_SYMBOL_GPL(xen_balloon_init);

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
static DEVICE_BOOL_ATTR(scrub_pages, 0644, xen_scrub_pages);

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


static struct attribute *balloon_attrs[] = {
	&dev_attr_target_kb.attr,
	&dev_attr_target.attr,
	&dev_attr_schedule_delay.attr.attr,
	&dev_attr_max_schedule_delay.attr.attr,
	&dev_attr_retry_count.attr.attr,
	&dev_attr_max_retry_count.attr.attr,
	&dev_attr_scrub_pages.attr.attr,
	NULL
};

static const struct attribute_group balloon_group = {
	.attrs = balloon_attrs
};

static struct attribute *balloon_info_attrs[] = {
	&dev_attr_current_kb.attr,
	&dev_attr_low_kb.attr,
	&dev_attr_high_kb.attr,
	NULL
};

static const struct attribute_group balloon_info_group = {
	.name = "info",
	.attrs = balloon_info_attrs
};

static const struct attribute_group *balloon_groups[] = {
	&balloon_group,
	&balloon_info_group,
	NULL
};

static struct bus_type balloon_subsys = {
	.name = BALLOON_CLASS_NAME,
	.dev_name = BALLOON_CLASS_NAME,
};

static int register_balloon(struct device *dev)
{
	int error;

	error = subsys_system_register(&balloon_subsys, NULL);
	if (error)
		return error;

	dev->id = 0;
	dev->bus = &balloon_subsys;
	dev->groups = balloon_groups;

	error = device_register(dev);
	if (error) {
		bus_unregister(&balloon_subsys);
		return error;
	}

	return 0;
}

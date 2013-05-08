/******************************************************************************
 * Xen selfballoon driver (and optional frontswap self-shrinking driver)
 *
 * Copyright (c) 2009-2011, Dan Magenheimer, Oracle Corp.
 *
 * This code complements the cleancache and frontswap patchsets to optimize
 * support for Xen Transcendent Memory ("tmem").  The policy it implements
 * is rudimentary and will likely improve over time, but it does work well
 * enough today.
 *
 * Two functionalities are implemented here which both use "control theory"
 * (feedback) to optimize memory utilization. In a virtualized environment
 * such as Xen, RAM is often a scarce resource and we would like to ensure
 * that each of a possibly large number of virtual machines is using RAM
 * efficiently, i.e. using as little as possible when under light load
 * and obtaining as much as possible when memory demands are high.
 * Since RAM needs vary highly dynamically and sometimes dramatically,
 * "hysteresis" is used, that is, memory target is determined not just
 * on current data but also on past data stored in the system.
 *
 * "Selfballooning" creates memory pressure by managing the Xen balloon
 * driver to decrease and increase available kernel memory, driven
 * largely by the target value of "Committed_AS" (see /proc/meminfo).
 * Since Committed_AS does not account for clean mapped pages (i.e. pages
 * in RAM that are identical to pages on disk), selfballooning has the
 * affect of pushing less frequently used clean pagecache pages out of
 * kernel RAM and, presumably using cleancache, into Xen tmem where
 * Xen can more efficiently optimize RAM utilization for such pages.
 *
 * When kernel memory demand unexpectedly increases faster than Xen, via
 * the selfballoon driver, is able to (or chooses to) provide usable RAM,
 * the kernel may invoke swapping.  In most cases, frontswap is able
 * to absorb this swapping into Xen tmem.  However, due to the fact
 * that the kernel swap subsystem assumes swapping occurs to a disk,
 * swapped pages may sit on the disk for a very long time; even if
 * the kernel knows the page will never be used again.  This is because
 * the disk space costs very little and can be overwritten when
 * necessary.  When such stale pages are in frontswap, however, they
 * are taking up valuable real estate.  "Frontswap selfshrinking" works
 * to resolve this:  When frontswap activity is otherwise stable
 * and the guest kernel is not under memory pressure, the "frontswap
 * selfshrinking" accounts for this by providing pressure to remove some
 * pages from frontswap and return them to kernel memory.
 *
 * For both "selfballooning" and "frontswap-selfshrinking", a worker
 * thread is used and sysfs tunables are provided to adjust the frequency
 * and rate of adjustments to achieve the goal, as well as to disable one
 * or both functions independently.
 *
 * While some argue that this functionality can and should be implemented
 * in userspace, it has been observed that bad things happen (e.g. OOMs).
 *
 * System configuration note: Selfballooning should not be enabled on
 * systems without a sufficiently large swap device configured; for best
 * results, it is recommended that total swap be increased by the size
 * of the guest memory.  Also, while technically not required to be
 * configured, it is highly recommended that frontswap also be configured
 * and enabled when selfballooning is running.  So, selfballooning
 * is disabled by default if frontswap is not configured and can only
 * be enabled with the "selfballooning" kernel boot option; similarly
 * selfballooning is enabled by default if frontswap is configured and
 * can be disabled with the "noselfballooning" kernel boot option.  Finally,
 * when frontswap is configured,frontswap-selfshrinking can be disabled
 * with the "tmem.selfshrink=0" kernel boot option.
 *
 * Selfballooning is disallowed in domain0 and force-disabled.
 *
 */

#include <linux/kernel.h>
#include <linux/bootmem.h>
#include <linux/swap.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/module.h>
#include <linux/workqueue.h>
#include <linux/device.h>
#include <xen/balloon.h>
#include <xen/tmem.h>
#include <xen/xen.h>

/* Enable/disable with sysfs. */
static int xen_selfballooning_enabled __read_mostly;

/*
 * Controls rate at which memory target (this iteration) approaches
 * ultimate goal when memory need is increasing (up-hysteresis) or
 * decreasing (down-hysteresis). Higher values of hysteresis cause
 * slower increases/decreases. The default values for the various
 * parameters were deemed reasonable by experimentation, may be
 * workload-dependent, and can all be adjusted via sysfs.
 */
static unsigned int selfballoon_downhysteresis __read_mostly = 8;
static unsigned int selfballoon_uphysteresis __read_mostly = 1;

/* In HZ, controls frequency of worker invocation. */
static unsigned int selfballoon_interval __read_mostly = 5;

/*
 * Minimum usable RAM in MB for selfballooning target for balloon.
 * If non-zero, it is added to totalreserve_pages and self-ballooning
 * will not balloon below the sum.  If zero, a piecewise linear function
 * is calculated as a minimum and added to totalreserve_pages.  Note that
 * setting this value indiscriminately may cause OOMs and crashes.
 */
static unsigned int selfballoon_min_usable_mb;

/*
 * Amount of RAM in MB to add to the target number of pages.
 * Can be used to reserve some more room for caches and the like.
 */
static unsigned int selfballoon_reserved_mb;

static void selfballoon_process(struct work_struct *work);
static DECLARE_DELAYED_WORK(selfballoon_worker, selfballoon_process);

#ifdef CONFIG_FRONTSWAP
#include <linux/frontswap.h>

/* Enable/disable with sysfs. */
static bool frontswap_selfshrinking __read_mostly;

/*
 * The default values for the following parameters were deemed reasonable
 * by experimentation, may be workload-dependent, and can all be
 * adjusted via sysfs.
 */

/* Control rate for frontswap shrinking. Higher hysteresis is slower. */
static unsigned int frontswap_hysteresis __read_mostly = 20;

/*
 * Number of selfballoon worker invocations to wait before observing that
 * frontswap selfshrinking should commence. Note that selfshrinking does
 * not use a separate worker thread.
 */
static unsigned int frontswap_inertia __read_mostly = 3;

/* Countdown to next invocation of frontswap_shrink() */
static unsigned long frontswap_inertia_counter;

/*
 * Invoked by the selfballoon worker thread, uses current number of pages
 * in frontswap (frontswap_curr_pages()), previous status, and control
 * values (hysteresis and inertia) to determine if frontswap should be
 * shrunk and what the new frontswap size should be.  Note that
 * frontswap_shrink is essentially a partial swapoff that immediately
 * transfers pages from the "swap device" (frontswap) back into kernel
 * RAM; despite the name, frontswap "shrinking" is very different from
 * the "shrinker" interface used by the kernel MM subsystem to reclaim
 * memory.
 */
static void frontswap_selfshrink(void)
{
	static unsigned long cur_frontswap_pages;
	static unsigned long last_frontswap_pages;
	static unsigned long tgt_frontswap_pages;

	last_frontswap_pages = cur_frontswap_pages;
	cur_frontswap_pages = frontswap_curr_pages();
	if (!cur_frontswap_pages ||
			(cur_frontswap_pages > last_frontswap_pages)) {
		frontswap_inertia_counter = frontswap_inertia;
		return;
	}
	if (frontswap_inertia_counter && --frontswap_inertia_counter)
		return;
	if (cur_frontswap_pages <= frontswap_hysteresis)
		tgt_frontswap_pages = 0;
	else
		tgt_frontswap_pages = cur_frontswap_pages -
			(cur_frontswap_pages / frontswap_hysteresis);
	frontswap_shrink(tgt_frontswap_pages);
}

/* Disable with kernel boot option. */
static bool use_selfballooning = true;

static int __init xen_noselfballooning_setup(char *s)
{
	use_selfballooning = false;
	return 1;
}

__setup("noselfballooning", xen_noselfballooning_setup);
#else /* !CONFIG_FRONTSWAP */
/* Enable with kernel boot option. */
static bool use_selfballooning;

static int __init xen_selfballooning_setup(char *s)
{
	use_selfballooning = true;
	return 1;
}

__setup("selfballooning", xen_selfballooning_setup);
#endif /* CONFIG_FRONTSWAP */

#define MB2PAGES(mb)	((mb) << (20 - PAGE_SHIFT))

/*
 * Use current balloon size, the goal (vm_committed_as), and hysteresis
 * parameters to set a new target balloon size
 */
static void selfballoon_process(struct work_struct *work)
{
	unsigned long cur_pages, goal_pages, tgt_pages, floor_pages;
	unsigned long useful_pages;
	bool reset_timer = false;

	if (xen_selfballooning_enabled) {
		cur_pages = totalram_pages;
		tgt_pages = cur_pages; /* default is no change */
		goal_pages = vm_memory_committed() +
				totalreserve_pages +
				MB2PAGES(selfballoon_reserved_mb);
#ifdef CONFIG_FRONTSWAP
		/* allow space for frontswap pages to be repatriated */
		if (frontswap_selfshrinking && frontswap_enabled)
			goal_pages += frontswap_curr_pages();
#endif
		if (cur_pages > goal_pages)
			tgt_pages = cur_pages -
				((cur_pages - goal_pages) /
				  selfballoon_downhysteresis);
		else if (cur_pages < goal_pages)
			tgt_pages = cur_pages +
				((goal_pages - cur_pages) /
				  selfballoon_uphysteresis);
		/* else if cur_pages == goal_pages, no change */
		useful_pages = max_pfn - totalreserve_pages;
		if (selfballoon_min_usable_mb != 0)
			floor_pages = totalreserve_pages +
					MB2PAGES(selfballoon_min_usable_mb);
		/* piecewise linear function ending in ~3% slope */
		else if (useful_pages < MB2PAGES(16))
			floor_pages = max_pfn; /* not worth ballooning */
		else if (useful_pages < MB2PAGES(64))
			floor_pages = totalreserve_pages + MB2PAGES(16) +
					((useful_pages - MB2PAGES(16)) >> 1);
		else if (useful_pages < MB2PAGES(512))
			floor_pages = totalreserve_pages + MB2PAGES(40) +
					((useful_pages - MB2PAGES(40)) >> 3);
		else /* useful_pages >= MB2PAGES(512) */
			floor_pages = totalreserve_pages + MB2PAGES(99) +
					((useful_pages - MB2PAGES(99)) >> 5);
		if (tgt_pages < floor_pages)
			tgt_pages = floor_pages;
		balloon_set_new_target(tgt_pages +
			balloon_stats.current_pages - totalram_pages);
		reset_timer = true;
	}
#ifdef CONFIG_FRONTSWAP
	if (frontswap_selfshrinking && frontswap_enabled) {
		frontswap_selfshrink();
		reset_timer = true;
	}
#endif
	if (reset_timer)
		schedule_delayed_work(&selfballoon_worker,
			selfballoon_interval * HZ);
}

#ifdef CONFIG_SYSFS

#include <linux/capability.h>

#define SELFBALLOON_SHOW(name, format, args...)				\
	static ssize_t show_##name(struct device *dev,	\
					  struct device_attribute *attr, \
					  char *buf) \
	{ \
		return sprintf(buf, format, ##args); \
	}

SELFBALLOON_SHOW(selfballooning, "%d\n", xen_selfballooning_enabled);

static ssize_t store_selfballooning(struct device *dev,
			    struct device_attribute *attr,
			    const char *buf,
			    size_t count)
{
	bool was_enabled = xen_selfballooning_enabled;
	unsigned long tmp;
	int err;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	err = strict_strtoul(buf, 10, &tmp);
	if (err || ((tmp != 0) && (tmp != 1)))
		return -EINVAL;

	xen_selfballooning_enabled = !!tmp;
	if (!was_enabled && xen_selfballooning_enabled)
		schedule_delayed_work(&selfballoon_worker,
			selfballoon_interval * HZ);

	return count;
}

static DEVICE_ATTR(selfballooning, S_IRUGO | S_IWUSR,
		   show_selfballooning, store_selfballooning);

SELFBALLOON_SHOW(selfballoon_interval, "%d\n", selfballoon_interval);

static ssize_t store_selfballoon_interval(struct device *dev,
					  struct device_attribute *attr,
					  const char *buf,
					  size_t count)
{
	unsigned long val;
	int err;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;
	err = strict_strtoul(buf, 10, &val);
	if (err || val == 0)
		return -EINVAL;
	selfballoon_interval = val;
	return count;
}

static DEVICE_ATTR(selfballoon_interval, S_IRUGO | S_IWUSR,
		   show_selfballoon_interval, store_selfballoon_interval);

SELFBALLOON_SHOW(selfballoon_downhys, "%d\n", selfballoon_downhysteresis);

static ssize_t store_selfballoon_downhys(struct device *dev,
					 struct device_attribute *attr,
					 const char *buf,
					 size_t count)
{
	unsigned long val;
	int err;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;
	err = strict_strtoul(buf, 10, &val);
	if (err || val == 0)
		return -EINVAL;
	selfballoon_downhysteresis = val;
	return count;
}

static DEVICE_ATTR(selfballoon_downhysteresis, S_IRUGO | S_IWUSR,
		   show_selfballoon_downhys, store_selfballoon_downhys);


SELFBALLOON_SHOW(selfballoon_uphys, "%d\n", selfballoon_uphysteresis);

static ssize_t store_selfballoon_uphys(struct device *dev,
				       struct device_attribute *attr,
				       const char *buf,
				       size_t count)
{
	unsigned long val;
	int err;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;
	err = strict_strtoul(buf, 10, &val);
	if (err || val == 0)
		return -EINVAL;
	selfballoon_uphysteresis = val;
	return count;
}

static DEVICE_ATTR(selfballoon_uphysteresis, S_IRUGO | S_IWUSR,
		   show_selfballoon_uphys, store_selfballoon_uphys);

SELFBALLOON_SHOW(selfballoon_min_usable_mb, "%d\n",
				selfballoon_min_usable_mb);

static ssize_t store_selfballoon_min_usable_mb(struct device *dev,
					       struct device_attribute *attr,
					       const char *buf,
					       size_t count)
{
	unsigned long val;
	int err;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;
	err = strict_strtoul(buf, 10, &val);
	if (err || val == 0)
		return -EINVAL;
	selfballoon_min_usable_mb = val;
	return count;
}

static DEVICE_ATTR(selfballoon_min_usable_mb, S_IRUGO | S_IWUSR,
		   show_selfballoon_min_usable_mb,
		   store_selfballoon_min_usable_mb);

SELFBALLOON_SHOW(selfballoon_reserved_mb, "%d\n",
				selfballoon_reserved_mb);

static ssize_t store_selfballoon_reserved_mb(struct device *dev,
					     struct device_attribute *attr,
					     const char *buf,
					     size_t count)
{
	unsigned long val;
	int err;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;
	err = strict_strtoul(buf, 10, &val);
	if (err || val == 0)
		return -EINVAL;
	selfballoon_reserved_mb = val;
	return count;
}

static DEVICE_ATTR(selfballoon_reserved_mb, S_IRUGO | S_IWUSR,
		   show_selfballoon_reserved_mb,
		   store_selfballoon_reserved_mb);


#ifdef CONFIG_FRONTSWAP
SELFBALLOON_SHOW(frontswap_selfshrinking, "%d\n", frontswap_selfshrinking);

static ssize_t store_frontswap_selfshrinking(struct device *dev,
					     struct device_attribute *attr,
					     const char *buf,
					     size_t count)
{
	bool was_enabled = frontswap_selfshrinking;
	unsigned long tmp;
	int err;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;
	err = strict_strtoul(buf, 10, &tmp);
	if (err || ((tmp != 0) && (tmp != 1)))
		return -EINVAL;
	frontswap_selfshrinking = !!tmp;
	if (!was_enabled && !xen_selfballooning_enabled &&
	     frontswap_selfshrinking)
		schedule_delayed_work(&selfballoon_worker,
			selfballoon_interval * HZ);

	return count;
}

static DEVICE_ATTR(frontswap_selfshrinking, S_IRUGO | S_IWUSR,
		   show_frontswap_selfshrinking, store_frontswap_selfshrinking);

SELFBALLOON_SHOW(frontswap_inertia, "%d\n", frontswap_inertia);

static ssize_t store_frontswap_inertia(struct device *dev,
				       struct device_attribute *attr,
				       const char *buf,
				       size_t count)
{
	unsigned long val;
	int err;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;
	err = strict_strtoul(buf, 10, &val);
	if (err || val == 0)
		return -EINVAL;
	frontswap_inertia = val;
	frontswap_inertia_counter = val;
	return count;
}

static DEVICE_ATTR(frontswap_inertia, S_IRUGO | S_IWUSR,
		   show_frontswap_inertia, store_frontswap_inertia);

SELFBALLOON_SHOW(frontswap_hysteresis, "%d\n", frontswap_hysteresis);

static ssize_t store_frontswap_hysteresis(struct device *dev,
					  struct device_attribute *attr,
					  const char *buf,
					  size_t count)
{
	unsigned long val;
	int err;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;
	err = strict_strtoul(buf, 10, &val);
	if (err || val == 0)
		return -EINVAL;
	frontswap_hysteresis = val;
	return count;
}

static DEVICE_ATTR(frontswap_hysteresis, S_IRUGO | S_IWUSR,
		   show_frontswap_hysteresis, store_frontswap_hysteresis);

#endif /* CONFIG_FRONTSWAP */

static struct attribute *selfballoon_attrs[] = {
	&dev_attr_selfballooning.attr,
	&dev_attr_selfballoon_interval.attr,
	&dev_attr_selfballoon_downhysteresis.attr,
	&dev_attr_selfballoon_uphysteresis.attr,
	&dev_attr_selfballoon_min_usable_mb.attr,
	&dev_attr_selfballoon_reserved_mb.attr,
#ifdef CONFIG_FRONTSWAP
	&dev_attr_frontswap_selfshrinking.attr,
	&dev_attr_frontswap_hysteresis.attr,
	&dev_attr_frontswap_inertia.attr,
#endif
	NULL
};

static const struct attribute_group selfballoon_group = {
	.name = "selfballoon",
	.attrs = selfballoon_attrs
};
#endif

int register_xen_selfballooning(struct device *dev)
{
	int error = -1;

#ifdef CONFIG_SYSFS
	error = sysfs_create_group(&dev->kobj, &selfballoon_group);
#endif
	return error;
}
EXPORT_SYMBOL(register_xen_selfballooning);

int xen_selfballoon_init(bool use_selfballooning, bool use_frontswap_selfshrink)
{
	bool enable = false;

	if (!xen_domain())
		return -ENODEV;

	if (xen_initial_domain()) {
		pr_info("xen/balloon: Xen selfballooning driver "
				"disabled for domain0.\n");
		return -ENODEV;
	}

	xen_selfballooning_enabled = tmem_enabled && use_selfballooning;
	if (xen_selfballooning_enabled) {
		pr_info("xen/balloon: Initializing Xen "
					"selfballooning driver.\n");
		enable = true;
	}
#ifdef CONFIG_FRONTSWAP
	frontswap_selfshrinking = tmem_enabled && use_frontswap_selfshrink;
	if (frontswap_selfshrinking) {
		pr_info("xen/balloon: Initializing frontswap "
					"selfshrinking driver.\n");
		enable = true;
	}
#endif
	if (!enable)
		return -ENODEV;

	schedule_delayed_work(&selfballoon_worker, selfballoon_interval * HZ);

	return 0;
}
EXPORT_SYMBOL(xen_selfballoon_init);

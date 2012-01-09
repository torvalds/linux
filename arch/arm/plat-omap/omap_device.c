/*
 * omap_device implementation
 *
 * Copyright (C) 2009-2010 Nokia Corporation
 * Paul Walmsley, Kevin Hilman
 *
 * Developed in collaboration with (alphabetical order): Benoit
 * Cousson, Thara Gopinath, Tony Lindgren, Rajendra Nayak, Vikram
 * Pandita, Sakari Poussa, Anand Sawant, Santosh Shilimkar, Richard
 * Woodruff
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This code provides a consistent interface for OMAP device drivers
 * to control power management and interconnect properties of their
 * devices.
 *
 * In the medium- to long-term, this code should either be
 * a) implemented via arch-specific pointers in platform_data
 * or
 * b) implemented as a proper omap_bus/omap_device in Linux, no more
 *    platform_data func pointers
 *
 *
 * Guidelines for usage by driver authors:
 *
 * 1. These functions are intended to be used by device drivers via
 * function pointers in struct platform_data.  As an example,
 * omap_device_enable() should be passed to the driver as
 *
 * struct foo_driver_platform_data {
 * ...
 *      int (*device_enable)(struct platform_device *pdev);
 * ...
 * }
 *
 * Note that the generic "device_enable" name is used, rather than
 * "omap_device_enable".  This is so other architectures can pass in their
 * own enable/disable functions here.
 *
 * This should be populated during device setup:
 *
 * ...
 * pdata->device_enable = omap_device_enable;
 * ...
 *
 * 2. Drivers should first check to ensure the function pointer is not null
 * before calling it, as in:
 *
 * if (pdata->device_enable)
 *     pdata->device_enable(pdev);
 *
 * This allows other architectures that don't use similar device_enable()/
 * device_shutdown() functions to execute normally.
 *
 * ...
 *
 * Suggested usage by device drivers:
 *
 * During device initialization:
 * device_enable()
 *
 * During device idle:
 * (save remaining device context if necessary)
 * device_idle();
 *
 * During device resume:
 * device_enable();
 * (restore context if necessary)
 *
 * During device shutdown:
 * device_shutdown()
 * (device must be reinitialized at this point to use it again)
 *
 */
#undef DEBUG

#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/pm_runtime.h>
#include <linux/of.h>
#include <linux/notifier.h>

#include <plat/omap_device.h>
#include <plat/omap_hwmod.h>
#include <plat/clock.h>

/* These parameters are passed to _omap_device_{de,}activate() */
#define USE_WAKEUP_LAT			0
#define IGNORE_WAKEUP_LAT		1

static int omap_device_register(struct platform_device *pdev);
static int omap_early_device_register(struct platform_device *pdev);
static struct omap_device *omap_device_alloc(struct platform_device *pdev,
				      struct omap_hwmod **ohs, int oh_cnt,
				      struct omap_device_pm_latency *pm_lats,
				      int pm_lats_cnt);
static void omap_device_delete(struct omap_device *od);


static struct omap_device_pm_latency omap_default_latency[] = {
	{
		.deactivate_func = omap_device_idle_hwmods,
		.activate_func   = omap_device_enable_hwmods,
		.flags = OMAP_DEVICE_LATENCY_AUTO_ADJUST,
	}
};

/* Private functions */

/**
 * _omap_device_activate - increase device readiness
 * @od: struct omap_device *
 * @ignore_lat: increase to latency target (0) or full readiness (1)?
 *
 * Increase readiness of omap_device @od (thus decreasing device
 * wakeup latency, but consuming more power).  If @ignore_lat is
 * IGNORE_WAKEUP_LAT, make the omap_device fully active.  Otherwise,
 * if @ignore_lat is USE_WAKEUP_LAT, and the device's maximum wakeup
 * latency is greater than the requested maximum wakeup latency, step
 * backwards in the omap_device_pm_latency table to ensure the
 * device's maximum wakeup latency is less than or equal to the
 * requested maximum wakeup latency.  Returns 0.
 */
static int _omap_device_activate(struct omap_device *od, u8 ignore_lat)
{
	struct timespec a, b, c;

	dev_dbg(&od->pdev->dev, "omap_device: activating\n");

	while (od->pm_lat_level > 0) {
		struct omap_device_pm_latency *odpl;
		unsigned long long act_lat = 0;

		od->pm_lat_level--;

		odpl = od->pm_lats + od->pm_lat_level;

		if (!ignore_lat &&
		    (od->dev_wakeup_lat <= od->_dev_wakeup_lat_limit))
			break;

		read_persistent_clock(&a);

		/* XXX check return code */
		odpl->activate_func(od);

		read_persistent_clock(&b);

		c = timespec_sub(b, a);
		act_lat = timespec_to_ns(&c);

		dev_dbg(&od->pdev->dev,
			"omap_device: pm_lat %d: activate: elapsed time "
			"%llu nsec\n", od->pm_lat_level, act_lat);

		if (act_lat > odpl->activate_lat) {
			odpl->activate_lat_worst = act_lat;
			if (odpl->flags & OMAP_DEVICE_LATENCY_AUTO_ADJUST) {
				odpl->activate_lat = act_lat;
				dev_dbg(&od->pdev->dev,
					"new worst case activate latency "
					"%d: %llu\n",
					od->pm_lat_level, act_lat);
			} else
				dev_warn(&od->pdev->dev,
					 "activate latency %d "
					 "higher than exptected. (%llu > %d)\n",
					 od->pm_lat_level, act_lat,
					 odpl->activate_lat);
		}

		od->dev_wakeup_lat -= odpl->activate_lat;
	}

	return 0;
}

/**
 * _omap_device_deactivate - decrease device readiness
 * @od: struct omap_device *
 * @ignore_lat: decrease to latency target (0) or full inactivity (1)?
 *
 * Decrease readiness of omap_device @od (thus increasing device
 * wakeup latency, but conserving power).  If @ignore_lat is
 * IGNORE_WAKEUP_LAT, make the omap_device fully inactive.  Otherwise,
 * if @ignore_lat is USE_WAKEUP_LAT, and the device's maximum wakeup
 * latency is less than the requested maximum wakeup latency, step
 * forwards in the omap_device_pm_latency table to ensure the device's
 * maximum wakeup latency is less than or equal to the requested
 * maximum wakeup latency.  Returns 0.
 */
static int _omap_device_deactivate(struct omap_device *od, u8 ignore_lat)
{
	struct timespec a, b, c;

	dev_dbg(&od->pdev->dev, "omap_device: deactivating\n");

	while (od->pm_lat_level < od->pm_lats_cnt) {
		struct omap_device_pm_latency *odpl;
		unsigned long long deact_lat = 0;

		odpl = od->pm_lats + od->pm_lat_level;

		if (!ignore_lat &&
		    ((od->dev_wakeup_lat + odpl->activate_lat) >
		     od->_dev_wakeup_lat_limit))
			break;

		read_persistent_clock(&a);

		/* XXX check return code */
		odpl->deactivate_func(od);

		read_persistent_clock(&b);

		c = timespec_sub(b, a);
		deact_lat = timespec_to_ns(&c);

		dev_dbg(&od->pdev->dev,
			"omap_device: pm_lat %d: deactivate: elapsed time "
			"%llu nsec\n", od->pm_lat_level, deact_lat);

		if (deact_lat > odpl->deactivate_lat) {
			odpl->deactivate_lat_worst = deact_lat;
			if (odpl->flags & OMAP_DEVICE_LATENCY_AUTO_ADJUST) {
				odpl->deactivate_lat = deact_lat;
				dev_dbg(&od->pdev->dev,
					"new worst case deactivate latency "
					"%d: %llu\n",
					od->pm_lat_level, deact_lat);
			} else
				dev_warn(&od->pdev->dev,
					 "deactivate latency %d "
					 "higher than exptected. (%llu > %d)\n",
					 od->pm_lat_level, deact_lat,
					 odpl->deactivate_lat);
		}

		od->dev_wakeup_lat += odpl->activate_lat;

		od->pm_lat_level++;
	}

	return 0;
}

static void _add_clkdev(struct omap_device *od, const char *clk_alias,
		       const char *clk_name)
{
	struct clk *r;
	struct clk_lookup *l;

	if (!clk_alias || !clk_name)
		return;

	dev_dbg(&od->pdev->dev, "Creating %s -> %s\n", clk_alias, clk_name);

	r = clk_get_sys(dev_name(&od->pdev->dev), clk_alias);
	if (!IS_ERR(r)) {
		dev_warn(&od->pdev->dev,
			 "alias %s already exists\n", clk_alias);
		clk_put(r);
		return;
	}

	r = omap_clk_get_by_name(clk_name);
	if (IS_ERR(r)) {
		dev_err(&od->pdev->dev,
			"omap_clk_get_by_name for %s failed\n", clk_name);
		return;
	}

	l = clkdev_alloc(r, clk_alias, dev_name(&od->pdev->dev));
	if (!l) {
		dev_err(&od->pdev->dev,
			"clkdev_alloc for %s failed\n", clk_alias);
		return;
	}

	clkdev_add(l);
}

/**
 * _add_hwmod_clocks_clkdev - Add clkdev entry for hwmod optional clocks
 * and main clock
 * @od: struct omap_device *od
 * @oh: struct omap_hwmod *oh
 *
 * For the main clock and every optional clock present per hwmod per
 * omap_device, this function adds an entry in the clkdev table of the
 * form <dev-id=dev_name, con-id=role> if it does not exist already.
 *
 * The function is called from inside omap_device_build_ss(), after
 * omap_device_register.
 *
 * This allows drivers to get a pointer to its optional clocks based on its role
 * by calling clk_get(<dev*>, <role>).
 * In the case of the main clock, a "fck" alias is used.
 *
 * No return value.
 */
static void _add_hwmod_clocks_clkdev(struct omap_device *od,
				     struct omap_hwmod *oh)
{
	int i;

	_add_clkdev(od, "fck", oh->main_clk);

	for (i = 0; i < oh->opt_clks_cnt; i++)
		_add_clkdev(od, oh->opt_clks[i].role, oh->opt_clks[i].clk);
}


static struct dev_pm_domain omap_device_pm_domain;

/**
 * omap_device_build_from_dt - build an omap_device with multiple hwmods
 * @pdev_name: name of the platform_device driver to use
 * @pdev_id: this platform_device's connection ID
 * @oh: ptr to the single omap_hwmod that backs this omap_device
 * @pdata: platform_data ptr to associate with the platform_device
 * @pdata_len: amount of memory pointed to by @pdata
 * @pm_lats: pointer to a omap_device_pm_latency array for this device
 * @pm_lats_cnt: ARRAY_SIZE() of @pm_lats
 * @is_early_device: should the device be registered as an early device or not
 *
 * Function for building an omap_device already registered from device-tree
 *
 * Returns 0 or PTR_ERR() on error.
 */
static int omap_device_build_from_dt(struct platform_device *pdev)
{
	struct omap_hwmod **hwmods;
	struct omap_device *od;
	struct omap_hwmod *oh;
	struct device_node *node = pdev->dev.of_node;
	const char *oh_name;
	int oh_cnt, i, ret = 0;

	oh_cnt = of_property_count_strings(node, "ti,hwmods");
	if (!oh_cnt || IS_ERR_VALUE(oh_cnt)) {
		dev_warn(&pdev->dev, "No 'hwmods' to build omap_device\n");
		return -ENODEV;
	}

	hwmods = kzalloc(sizeof(struct omap_hwmod *) * oh_cnt, GFP_KERNEL);
	if (!hwmods) {
		ret = -ENOMEM;
		goto odbfd_exit;
	}

	for (i = 0; i < oh_cnt; i++) {
		of_property_read_string_index(node, "ti,hwmods", i, &oh_name);
		oh = omap_hwmod_lookup(oh_name);
		if (!oh) {
			dev_err(&pdev->dev, "Cannot lookup hwmod '%s'\n",
				oh_name);
			ret = -EINVAL;
			goto odbfd_exit1;
		}
		hwmods[i] = oh;
	}

	od = omap_device_alloc(pdev, hwmods, oh_cnt, NULL, 0);
	if (!od) {
		dev_err(&pdev->dev, "Cannot allocate omap_device for :%s\n",
			oh_name);
		ret = PTR_ERR(od);
		goto odbfd_exit1;
	}

	if (of_get_property(node, "ti,no_idle_on_suspend", NULL))
		omap_device_disable_idle_on_suspend(pdev);

	pdev->dev.pm_domain = &omap_device_pm_domain;

odbfd_exit1:
	kfree(hwmods);
odbfd_exit:
	return ret;
}

static int _omap_device_notifier_call(struct notifier_block *nb,
				      unsigned long event, void *dev)
{
	struct platform_device *pdev = to_platform_device(dev);

	switch (event) {
	case BUS_NOTIFY_ADD_DEVICE:
		if (pdev->dev.of_node)
			omap_device_build_from_dt(pdev);
		break;

	case BUS_NOTIFY_DEL_DEVICE:
		if (pdev->archdata.od)
			omap_device_delete(pdev->archdata.od);
		break;
	}

	return NOTIFY_DONE;
}


/* Public functions for use by core code */

/**
 * omap_device_get_context_loss_count - get lost context count
 * @od: struct omap_device *
 *
 * Using the primary hwmod, query the context loss count for this
 * device.
 *
 * Callers should consider context for this device lost any time this
 * function returns a value different than the value the caller got
 * the last time it called this function.
 *
 * If any hwmods exist for the omap_device assoiated with @pdev,
 * return the context loss counter for that hwmod, otherwise return
 * zero.
 */
int omap_device_get_context_loss_count(struct platform_device *pdev)
{
	struct omap_device *od;
	u32 ret = 0;

	od = to_omap_device(pdev);

	if (od->hwmods_cnt)
		ret = omap_hwmod_get_context_loss_count(od->hwmods[0]);

	return ret;
}

/**
 * omap_device_count_resources - count number of struct resource entries needed
 * @od: struct omap_device *
 *
 * Count the number of struct resource entries needed for this
 * omap_device @od.  Used by omap_device_build_ss() to determine how
 * much memory to allocate before calling
 * omap_device_fill_resources().  Returns the count.
 */
static int omap_device_count_resources(struct omap_device *od)
{
	int c = 0;
	int i;

	for (i = 0; i < od->hwmods_cnt; i++)
		c += omap_hwmod_count_resources(od->hwmods[i]);

	pr_debug("omap_device: %s: counted %d total resources across %d "
		 "hwmods\n", od->pdev->name, c, od->hwmods_cnt);

	return c;
}

/**
 * omap_device_fill_resources - fill in array of struct resource
 * @od: struct omap_device *
 * @res: pointer to an array of struct resource to be filled in
 *
 * Populate one or more empty struct resource pointed to by @res with
 * the resource data for this omap_device @od.  Used by
 * omap_device_build_ss() after calling omap_device_count_resources().
 * Ideally this function would not be needed at all.  If omap_device
 * replaces platform_device, then we can specify our own
 * get_resource()/ get_irq()/etc functions that use the underlying
 * omap_hwmod information.  Or if platform_device is extended to use
 * subarchitecture-specific function pointers, the various
 * platform_device functions can simply call omap_device internal
 * functions to get device resources.  Hacking around the existing
 * platform_device code wastes memory.  Returns 0.
 */
static int omap_device_fill_resources(struct omap_device *od,
				      struct resource *res)
{
	int c = 0;
	int i, r;

	for (i = 0; i < od->hwmods_cnt; i++) {
		r = omap_hwmod_fill_resources(od->hwmods[i], res);
		res += r;
		c += r;
	}

	return 0;
}

/**
 * omap_device_alloc - allocate an omap_device
 * @pdev: platform_device that will be included in this omap_device
 * @oh: ptr to the single omap_hwmod that backs this omap_device
 * @pdata: platform_data ptr to associate with the platform_device
 * @pdata_len: amount of memory pointed to by @pdata
 * @pm_lats: pointer to a omap_device_pm_latency array for this device
 * @pm_lats_cnt: ARRAY_SIZE() of @pm_lats
 *
 * Convenience function for allocating an omap_device structure and filling
 * hwmods, resources and pm_latency attributes.
 *
 * Returns an struct omap_device pointer or ERR_PTR() on error;
 */
static struct omap_device *omap_device_alloc(struct platform_device *pdev,
					struct omap_hwmod **ohs, int oh_cnt,
					struct omap_device_pm_latency *pm_lats,
					int pm_lats_cnt)
{
	int ret = -ENOMEM;
	struct omap_device *od;
	struct resource *res = NULL;
	int i, res_count;
	struct omap_hwmod **hwmods;

	od = kzalloc(sizeof(struct omap_device), GFP_KERNEL);
	if (!od) {
		ret = -ENOMEM;
		goto oda_exit1;
	}
	od->hwmods_cnt = oh_cnt;

	hwmods = kmemdup(ohs, sizeof(struct omap_hwmod *) * oh_cnt, GFP_KERNEL);
	if (!hwmods)
		goto oda_exit2;

	od->hwmods = hwmods;
	od->pdev = pdev;

	/*
	 * HACK: Ideally the resources from DT should match, and hwmod
	 * should just add the missing ones. Since the name is not
	 * properly populated by DT, stick to hwmod resources only.
	 */
	if (pdev->num_resources && pdev->resource)
		dev_warn(&pdev->dev, "%s(): resources already allocated %d\n",
			__func__, pdev->num_resources);

	res_count = omap_device_count_resources(od);
	if (res_count > 0) {
		dev_dbg(&pdev->dev, "%s(): resources allocated from hwmod %d\n",
			__func__, res_count);
		res = kzalloc(sizeof(struct resource) * res_count, GFP_KERNEL);
		if (!res)
			goto oda_exit3;

		omap_device_fill_resources(od, res);

		ret = platform_device_add_resources(pdev, res, res_count);
		kfree(res);

		if (ret)
			goto oda_exit3;
	}

	if (!pm_lats) {
		pm_lats = omap_default_latency;
		pm_lats_cnt = ARRAY_SIZE(omap_default_latency);
	}

	od->pm_lats_cnt = pm_lats_cnt;
	od->pm_lats = kmemdup(pm_lats,
			sizeof(struct omap_device_pm_latency) * pm_lats_cnt,
			GFP_KERNEL);
	if (!od->pm_lats)
		goto oda_exit3;

	pdev->archdata.od = od;

	for (i = 0; i < oh_cnt; i++) {
		hwmods[i]->od = od;
		_add_hwmod_clocks_clkdev(od, hwmods[i]);
	}

	return od;

oda_exit3:
	kfree(hwmods);
oda_exit2:
	kfree(od);
oda_exit1:
	dev_err(&pdev->dev, "omap_device: build failed (%d)\n", ret);

	return ERR_PTR(ret);
}

static void omap_device_delete(struct omap_device *od)
{
	if (!od)
		return;

	od->pdev->archdata.od = NULL;
	kfree(od->pm_lats);
	kfree(od->hwmods);
	kfree(od);
}

/**
 * omap_device_build - build and register an omap_device with one omap_hwmod
 * @pdev_name: name of the platform_device driver to use
 * @pdev_id: this platform_device's connection ID
 * @oh: ptr to the single omap_hwmod that backs this omap_device
 * @pdata: platform_data ptr to associate with the platform_device
 * @pdata_len: amount of memory pointed to by @pdata
 * @pm_lats: pointer to a omap_device_pm_latency array for this device
 * @pm_lats_cnt: ARRAY_SIZE() of @pm_lats
 * @is_early_device: should the device be registered as an early device or not
 *
 * Convenience function for building and registering a single
 * omap_device record, which in turn builds and registers a
 * platform_device record.  See omap_device_build_ss() for more
 * information.  Returns ERR_PTR(-EINVAL) if @oh is NULL; otherwise,
 * passes along the return value of omap_device_build_ss().
 */
struct platform_device *omap_device_build(const char *pdev_name, int pdev_id,
				      struct omap_hwmod *oh, void *pdata,
				      int pdata_len,
				      struct omap_device_pm_latency *pm_lats,
				      int pm_lats_cnt, int is_early_device)
{
	struct omap_hwmod *ohs[] = { oh };

	if (!oh)
		return ERR_PTR(-EINVAL);

	return omap_device_build_ss(pdev_name, pdev_id, ohs, 1, pdata,
				    pdata_len, pm_lats, pm_lats_cnt,
				    is_early_device);
}

/**
 * omap_device_build_ss - build and register an omap_device with multiple hwmods
 * @pdev_name: name of the platform_device driver to use
 * @pdev_id: this platform_device's connection ID
 * @oh: ptr to the single omap_hwmod that backs this omap_device
 * @pdata: platform_data ptr to associate with the platform_device
 * @pdata_len: amount of memory pointed to by @pdata
 * @pm_lats: pointer to a omap_device_pm_latency array for this device
 * @pm_lats_cnt: ARRAY_SIZE() of @pm_lats
 * @is_early_device: should the device be registered as an early device or not
 *
 * Convenience function for building and registering an omap_device
 * subsystem record.  Subsystem records consist of multiple
 * omap_hwmods.  This function in turn builds and registers a
 * platform_device record.  Returns an ERR_PTR() on error, or passes
 * along the return value of omap_device_register().
 */
struct platform_device *omap_device_build_ss(const char *pdev_name, int pdev_id,
					 struct omap_hwmod **ohs, int oh_cnt,
					 void *pdata, int pdata_len,
					 struct omap_device_pm_latency *pm_lats,
					 int pm_lats_cnt, int is_early_device)
{
	int ret = -ENOMEM;
	struct platform_device *pdev;
	struct omap_device *od;

	if (!ohs || oh_cnt == 0 || !pdev_name)
		return ERR_PTR(-EINVAL);

	if (!pdata && pdata_len > 0)
		return ERR_PTR(-EINVAL);

	pdev = platform_device_alloc(pdev_name, pdev_id);
	if (!pdev) {
		ret = -ENOMEM;
		goto odbs_exit;
	}

	/* Set the dev_name early to allow dev_xxx in omap_device_alloc */
	if (pdev->id != -1)
		dev_set_name(&pdev->dev, "%s.%d", pdev->name,  pdev->id);
	else
		dev_set_name(&pdev->dev, "%s", pdev->name);

	od = omap_device_alloc(pdev, ohs, oh_cnt, pm_lats, pm_lats_cnt);
	if (!od)
		goto odbs_exit1;

	ret = platform_device_add_data(pdev, pdata, pdata_len);
	if (ret)
		goto odbs_exit2;

	if (is_early_device)
		ret = omap_early_device_register(pdev);
	else
		ret = omap_device_register(pdev);
	if (ret)
		goto odbs_exit2;

	return pdev;

odbs_exit2:
	omap_device_delete(od);
odbs_exit1:
	platform_device_put(pdev);
odbs_exit:

	pr_err("omap_device: %s: build failed (%d)\n", pdev_name, ret);

	return ERR_PTR(ret);
}

/**
 * omap_early_device_register - register an omap_device as an early platform
 * device.
 * @od: struct omap_device * to register
 *
 * Register the omap_device structure.  This currently just calls
 * platform_early_add_device() on the underlying platform_device.
 * Returns 0 by default.
 */
static int omap_early_device_register(struct platform_device *pdev)
{
	struct platform_device *devices[1];

	devices[0] = pdev;
	early_platform_add_devices(devices, 1);
	return 0;
}

#ifdef CONFIG_PM_RUNTIME
static int _od_runtime_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	int ret;

	ret = pm_generic_runtime_suspend(dev);

	if (!ret)
		omap_device_idle(pdev);

	return ret;
}

static int _od_runtime_idle(struct device *dev)
{
	return pm_generic_runtime_idle(dev);
}

static int _od_runtime_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);

	omap_device_enable(pdev);

	return pm_generic_runtime_resume(dev);
}
#endif

#ifdef CONFIG_SUSPEND
static int _od_suspend_noirq(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct omap_device *od = to_omap_device(pdev);
	int ret;

	if (od->flags & OMAP_DEVICE_NO_IDLE_ON_SUSPEND)
		return pm_generic_suspend_noirq(dev);

	ret = pm_generic_suspend_noirq(dev);

	if (!ret && !pm_runtime_status_suspended(dev)) {
		if (pm_generic_runtime_suspend(dev) == 0) {
			omap_device_idle(pdev);
			od->flags |= OMAP_DEVICE_SUSPENDED;
		}
	}

	return ret;
}

static int _od_resume_noirq(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct omap_device *od = to_omap_device(pdev);

	if (od->flags & OMAP_DEVICE_NO_IDLE_ON_SUSPEND)
		return pm_generic_resume_noirq(dev);

	if ((od->flags & OMAP_DEVICE_SUSPENDED) &&
	    !pm_runtime_status_suspended(dev)) {
		od->flags &= ~OMAP_DEVICE_SUSPENDED;
		omap_device_enable(pdev);
		pm_generic_runtime_resume(dev);
	}

	return pm_generic_resume_noirq(dev);
}
#else
#define _od_suspend_noirq NULL
#define _od_resume_noirq NULL
#endif

static struct dev_pm_domain omap_device_pm_domain = {
	.ops = {
		SET_RUNTIME_PM_OPS(_od_runtime_suspend, _od_runtime_resume,
				   _od_runtime_idle)
		USE_PLATFORM_PM_SLEEP_OPS
		.suspend_noirq = _od_suspend_noirq,
		.resume_noirq = _od_resume_noirq,
	}
};

/**
 * omap_device_register - register an omap_device with one omap_hwmod
 * @od: struct omap_device * to register
 *
 * Register the omap_device structure.  This currently just calls
 * platform_device_register() on the underlying platform_device.
 * Returns the return value of platform_device_register().
 */
static int omap_device_register(struct platform_device *pdev)
{
	pr_debug("omap_device: %s: registering\n", pdev->name);

	pdev->dev.parent = &omap_device_parent;
	pdev->dev.pm_domain = &omap_device_pm_domain;
	return platform_device_add(pdev);
}


/* Public functions for use by device drivers through struct platform_data */

/**
 * omap_device_enable - fully activate an omap_device
 * @od: struct omap_device * to activate
 *
 * Do whatever is necessary for the hwmods underlying omap_device @od
 * to be accessible and ready to operate.  This generally involves
 * enabling clocks, setting SYSCONFIG registers; and in the future may
 * involve remuxing pins.  Device drivers should call this function
 * (through platform_data function pointers) where they would normally
 * enable clocks, etc.  Returns -EINVAL if called when the omap_device
 * is already enabled, or passes along the return value of
 * _omap_device_activate().
 */
int omap_device_enable(struct platform_device *pdev)
{
	int ret;
	struct omap_device *od;

	od = to_omap_device(pdev);

	if (od->_state == OMAP_DEVICE_STATE_ENABLED) {
		dev_warn(&pdev->dev,
			 "omap_device: %s() called from invalid state %d\n",
			 __func__, od->_state);
		return -EINVAL;
	}

	/* Enable everything if we're enabling this device from scratch */
	if (od->_state == OMAP_DEVICE_STATE_UNKNOWN)
		od->pm_lat_level = od->pm_lats_cnt;

	ret = _omap_device_activate(od, IGNORE_WAKEUP_LAT);

	od->dev_wakeup_lat = 0;
	od->_dev_wakeup_lat_limit = UINT_MAX;
	od->_state = OMAP_DEVICE_STATE_ENABLED;

	return ret;
}

/**
 * omap_device_idle - idle an omap_device
 * @od: struct omap_device * to idle
 *
 * Idle omap_device @od by calling as many .deactivate_func() entries
 * in the omap_device's pm_lats table as is possible without exceeding
 * the device's maximum wakeup latency limit, pm_lat_limit.  Device
 * drivers should call this function (through platform_data function
 * pointers) where they would normally disable clocks after operations
 * complete, etc..  Returns -EINVAL if the omap_device is not
 * currently enabled, or passes along the return value of
 * _omap_device_deactivate().
 */
int omap_device_idle(struct platform_device *pdev)
{
	int ret;
	struct omap_device *od;

	od = to_omap_device(pdev);

	if (od->_state != OMAP_DEVICE_STATE_ENABLED) {
		dev_warn(&pdev->dev,
			 "omap_device: %s() called from invalid state %d\n",
			 __func__, od->_state);
		return -EINVAL;
	}

	ret = _omap_device_deactivate(od, USE_WAKEUP_LAT);

	od->_state = OMAP_DEVICE_STATE_IDLE;

	return ret;
}

/**
 * omap_device_shutdown - shut down an omap_device
 * @od: struct omap_device * to shut down
 *
 * Shut down omap_device @od by calling all .deactivate_func() entries
 * in the omap_device's pm_lats table and then shutting down all of
 * the underlying omap_hwmods.  Used when a device is being "removed"
 * or a device driver is being unloaded.  Returns -EINVAL if the
 * omap_device is not currently enabled or idle, or passes along the
 * return value of _omap_device_deactivate().
 */
int omap_device_shutdown(struct platform_device *pdev)
{
	int ret, i;
	struct omap_device *od;

	od = to_omap_device(pdev);

	if (od->_state != OMAP_DEVICE_STATE_ENABLED &&
	    od->_state != OMAP_DEVICE_STATE_IDLE) {
		dev_warn(&pdev->dev,
			 "omap_device: %s() called from invalid state %d\n",
			 __func__, od->_state);
		return -EINVAL;
	}

	ret = _omap_device_deactivate(od, IGNORE_WAKEUP_LAT);

	for (i = 0; i < od->hwmods_cnt; i++)
		omap_hwmod_shutdown(od->hwmods[i]);

	od->_state = OMAP_DEVICE_STATE_SHUTDOWN;

	return ret;
}

/**
 * omap_device_align_pm_lat - activate/deactivate device to match wakeup lat lim
 * @od: struct omap_device *
 *
 * When a device's maximum wakeup latency limit changes, call some of
 * the .activate_func or .deactivate_func function pointers in the
 * omap_device's pm_lats array to ensure that the device's maximum
 * wakeup latency is less than or equal to the new latency limit.
 * Intended to be called by OMAP PM code whenever a device's maximum
 * wakeup latency limit changes (e.g., via
 * omap_pm_set_dev_wakeup_lat()).  Returns 0 if nothing needs to be
 * done (e.g., if the omap_device is not currently idle, or if the
 * wakeup latency is already current with the new limit) or passes
 * along the return value of _omap_device_deactivate() or
 * _omap_device_activate().
 */
int omap_device_align_pm_lat(struct platform_device *pdev,
			     u32 new_wakeup_lat_limit)
{
	int ret = -EINVAL;
	struct omap_device *od;

	od = to_omap_device(pdev);

	if (new_wakeup_lat_limit == od->dev_wakeup_lat)
		return 0;

	od->_dev_wakeup_lat_limit = new_wakeup_lat_limit;

	if (od->_state != OMAP_DEVICE_STATE_IDLE)
		return 0;
	else if (new_wakeup_lat_limit > od->dev_wakeup_lat)
		ret = _omap_device_deactivate(od, USE_WAKEUP_LAT);
	else if (new_wakeup_lat_limit < od->dev_wakeup_lat)
		ret = _omap_device_activate(od, USE_WAKEUP_LAT);

	return ret;
}

/**
 * omap_device_get_pwrdm - return the powerdomain * associated with @od
 * @od: struct omap_device *
 *
 * Return the powerdomain associated with the first underlying
 * omap_hwmod for this omap_device.  Intended for use by core OMAP PM
 * code.  Returns NULL on error or a struct powerdomain * upon
 * success.
 */
struct powerdomain *omap_device_get_pwrdm(struct omap_device *od)
{
	/*
	 * XXX Assumes that all omap_hwmod powerdomains are identical.
	 * This may not necessarily be true.  There should be a sanity
	 * check in here to WARN() if any difference appears.
	 */
	if (!od->hwmods_cnt)
		return NULL;

	return omap_hwmod_get_pwrdm(od->hwmods[0]);
}

/**
 * omap_device_get_mpu_rt_va - return the MPU's virtual addr for the hwmod base
 * @od: struct omap_device *
 *
 * Return the MPU's virtual address for the base of the hwmod, from
 * the ioremap() that the hwmod code does.  Only valid if there is one
 * hwmod associated with this device.  Returns NULL if there are zero
 * or more than one hwmods associated with this omap_device;
 * otherwise, passes along the return value from
 * omap_hwmod_get_mpu_rt_va().
 */
void __iomem *omap_device_get_rt_va(struct omap_device *od)
{
	if (od->hwmods_cnt != 1)
		return NULL;

	return omap_hwmod_get_mpu_rt_va(od->hwmods[0]);
}

/**
 * omap_device_get_by_hwmod_name() - convert a hwmod name to
 * device pointer.
 * @oh_name: name of the hwmod device
 *
 * Returns back a struct device * pointer associated with a hwmod
 * device represented by a hwmod_name
 */
struct device *omap_device_get_by_hwmod_name(const char *oh_name)
{
	struct omap_hwmod *oh;

	if (!oh_name) {
		WARN(1, "%s: no hwmod name!\n", __func__);
		return ERR_PTR(-EINVAL);
	}

	oh = omap_hwmod_lookup(oh_name);
	if (IS_ERR_OR_NULL(oh)) {
		WARN(1, "%s: no hwmod for %s\n", __func__,
			oh_name);
		return ERR_PTR(oh ? PTR_ERR(oh) : -ENODEV);
	}
	if (IS_ERR_OR_NULL(oh->od)) {
		WARN(1, "%s: no omap_device for %s\n", __func__,
			oh_name);
		return ERR_PTR(oh->od ? PTR_ERR(oh->od) : -ENODEV);
	}

	if (IS_ERR_OR_NULL(oh->od->pdev))
		return ERR_PTR(oh->od->pdev ? PTR_ERR(oh->od->pdev) : -ENODEV);

	return &oh->od->pdev->dev;
}
EXPORT_SYMBOL(omap_device_get_by_hwmod_name);

/*
 * Public functions intended for use in omap_device_pm_latency
 * .activate_func and .deactivate_func function pointers
 */

/**
 * omap_device_enable_hwmods - call omap_hwmod_enable() on all hwmods
 * @od: struct omap_device *od
 *
 * Enable all underlying hwmods.  Returns 0.
 */
int omap_device_enable_hwmods(struct omap_device *od)
{
	int i;

	for (i = 0; i < od->hwmods_cnt; i++)
		omap_hwmod_enable(od->hwmods[i]);

	/* XXX pass along return value here? */
	return 0;
}

/**
 * omap_device_idle_hwmods - call omap_hwmod_idle() on all hwmods
 * @od: struct omap_device *od
 *
 * Idle all underlying hwmods.  Returns 0.
 */
int omap_device_idle_hwmods(struct omap_device *od)
{
	int i;

	for (i = 0; i < od->hwmods_cnt; i++)
		omap_hwmod_idle(od->hwmods[i]);

	/* XXX pass along return value here? */
	return 0;
}

/**
 * omap_device_disable_clocks - disable all main and interface clocks
 * @od: struct omap_device *od
 *
 * Disable the main functional clock and interface clock for all of the
 * omap_hwmods associated with the omap_device.  Returns 0.
 */
int omap_device_disable_clocks(struct omap_device *od)
{
	int i;

	for (i = 0; i < od->hwmods_cnt; i++)
		omap_hwmod_disable_clocks(od->hwmods[i]);

	/* XXX pass along return value here? */
	return 0;
}

/**
 * omap_device_enable_clocks - enable all main and interface clocks
 * @od: struct omap_device *od
 *
 * Enable the main functional clock and interface clock for all of the
 * omap_hwmods associated with the omap_device.  Returns 0.
 */
int omap_device_enable_clocks(struct omap_device *od)
{
	int i;

	for (i = 0; i < od->hwmods_cnt; i++)
		omap_hwmod_enable_clocks(od->hwmods[i]);

	/* XXX pass along return value here? */
	return 0;
}

struct device omap_device_parent = {
	.init_name	= "omap",
	.parent         = &platform_bus,
};

static struct notifier_block platform_nb = {
	.notifier_call = _omap_device_notifier_call,
};

static int __init omap_device_init(void)
{
	bus_register_notifier(&platform_bus_type, &platform_nb);
	return device_register(&omap_device_parent);
}
core_initcall(omap_device_init);

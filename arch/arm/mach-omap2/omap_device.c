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
 * In the medium- to long-term, this code should be implemented as a
 * proper omap_bus/omap_device in Linux, no more platform_data func
 * pointers
 *
 *
 */
#undef DEBUG

#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/pm_domain.h>
#include <linux/pm_runtime.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/notifier.h>

#include "common.h"
#include "soc.h"
#include "omap_device.h"
#include "omap_hwmod.h"

/* Private functions */

static void _add_clkdev(struct omap_device *od, const char *clk_alias,
		       const char *clk_name)
{
	struct clk *r;
	int rc;

	if (!clk_alias || !clk_name)
		return;

	dev_dbg(&od->pdev->dev, "Creating %s -> %s\n", clk_alias, clk_name);

	r = clk_get_sys(dev_name(&od->pdev->dev), clk_alias);
	if (!IS_ERR(r)) {
		dev_dbg(&od->pdev->dev,
			 "alias %s already exists\n", clk_alias);
		clk_put(r);
		return;
	}

	r = clk_get_sys(NULL, clk_name);

	if (IS_ERR(r)) {
		struct of_phandle_args clkspec;

		clkspec.np = of_find_node_by_name(NULL, clk_name);

		r = of_clk_get_from_provider(&clkspec);

		rc = clk_register_clkdev(r, clk_alias,
					 dev_name(&od->pdev->dev));
	} else {
		rc = clk_add_alias(clk_alias, dev_name(&od->pdev->dev),
				   clk_name, NULL);
	}

	if (rc) {
		if (rc == -ENODEV || rc == -ENOMEM)
			dev_err(&od->pdev->dev,
				"clkdev_alloc for %s failed\n", clk_alias);
		else
			dev_err(&od->pdev->dev,
				"clk_get for %s failed\n", clk_name);
	}
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


/**
 * omap_device_build_from_dt - build an omap_device with multiple hwmods
 * @pdev_name: name of the platform_device driver to use
 * @pdev_id: this platform_device's connection ID
 * @oh: ptr to the single omap_hwmod that backs this omap_device
 * @pdata: platform_data ptr to associate with the platform_device
 * @pdata_len: amount of memory pointed to by @pdata
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
	bool device_active = false;

	oh_cnt = of_property_count_strings(node, "ti,hwmods");
	if (oh_cnt <= 0) {
		dev_dbg(&pdev->dev, "No 'hwmods' to build omap_device\n");
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
		if (oh->flags & HWMOD_INIT_NO_IDLE)
			device_active = true;
	}

	od = omap_device_alloc(pdev, hwmods, oh_cnt);
	if (IS_ERR(od)) {
		dev_err(&pdev->dev, "Cannot allocate omap_device for :%s\n",
			oh_name);
		ret = PTR_ERR(od);
		goto odbfd_exit1;
	}

	/* Fix up missing resource names */
	for (i = 0; i < pdev->num_resources; i++) {
		struct resource *r = &pdev->resource[i];

		if (r->name == NULL)
			r->name = dev_name(&pdev->dev);
	}

	dev_pm_domain_set(&pdev->dev, &omap_device_pm_domain);

	if (device_active) {
		omap_device_enable(pdev);
		pm_runtime_set_active(&pdev->dev);
	}

odbfd_exit1:
	kfree(hwmods);
odbfd_exit:
	/* if data/we are at fault.. load up a fail handler */
	if (ret)
		dev_pm_domain_set(&pdev->dev, &omap_device_fail_pm_domain);

	return ret;
}

static int _omap_device_notifier_call(struct notifier_block *nb,
				      unsigned long event, void *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct omap_device *od;
	int err;

	switch (event) {
	case BUS_NOTIFY_REMOVED_DEVICE:
		if (pdev->archdata.od)
			omap_device_delete(pdev->archdata.od);
		break;
	case BUS_NOTIFY_UNBOUND_DRIVER:
		od = to_omap_device(pdev);
		if (od && (od->_state == OMAP_DEVICE_STATE_ENABLED)) {
			dev_info(dev, "enabled after unload, idling\n");
			err = omap_device_idle(pdev);
			if (err)
				dev_err(dev, "failed to idle\n");
		}
		break;
	case BUS_NOTIFY_BIND_DRIVER:
		od = to_omap_device(pdev);
		if (od && (od->_state == OMAP_DEVICE_STATE_ENABLED) &&
		    pm_runtime_status_suspended(dev)) {
			od->_driver_status = BUS_NOTIFY_BIND_DRIVER;
			pm_runtime_set_active(dev);
		}
		break;
	case BUS_NOTIFY_ADD_DEVICE:
		if (pdev->dev.of_node)
			omap_device_build_from_dt(pdev);
		omap_auxdata_legacy_init(dev);
		/* fall through */
	default:
		od = to_omap_device(pdev);
		if (od)
			od->_driver_status = event;
	}

	return NOTIFY_DONE;
}

/**
 * _omap_device_enable_hwmods - call omap_hwmod_enable() on all hwmods
 * @od: struct omap_device *od
 *
 * Enable all underlying hwmods.  Returns 0.
 */
static int _omap_device_enable_hwmods(struct omap_device *od)
{
	int ret = 0;
	int i;

	for (i = 0; i < od->hwmods_cnt; i++)
		ret |= omap_hwmod_enable(od->hwmods[i]);

	return ret;
}

/**
 * _omap_device_idle_hwmods - call omap_hwmod_idle() on all hwmods
 * @od: struct omap_device *od
 *
 * Idle all underlying hwmods.  Returns 0.
 */
static int _omap_device_idle_hwmods(struct omap_device *od)
{
	int ret = 0;
	int i;

	for (i = 0; i < od->hwmods_cnt; i++)
		ret |= omap_hwmod_idle(od->hwmods[i]);

	return ret;
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
 * If any hwmods exist for the omap_device associated with @pdev,
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
 * omap_device_alloc - allocate an omap_device
 * @pdev: platform_device that will be included in this omap_device
 * @oh: ptr to the single omap_hwmod that backs this omap_device
 * @pdata: platform_data ptr to associate with the platform_device
 * @pdata_len: amount of memory pointed to by @pdata
 *
 * Convenience function for allocating an omap_device structure and filling
 * hwmods, and resources.
 *
 * Returns an struct omap_device pointer or ERR_PTR() on error;
 */
struct omap_device *omap_device_alloc(struct platform_device *pdev,
					struct omap_hwmod **ohs, int oh_cnt)
{
	int ret = -ENOMEM;
	struct omap_device *od;
	int i;
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
	pdev->archdata.od = od;

	for (i = 0; i < oh_cnt; i++) {
		hwmods[i]->od = od;
		_add_hwmod_clocks_clkdev(od, hwmods[i]);
	}

	return od;

oda_exit2:
	kfree(od);
oda_exit1:
	dev_err(&pdev->dev, "omap_device: build failed (%d)\n", ret);

	return ERR_PTR(ret);
}

void omap_device_delete(struct omap_device *od)
{
	if (!od)
		return;

	od->pdev->archdata.od = NULL;
	kfree(od->hwmods);
	kfree(od);
}

/**
 * omap_device_copy_resources - Add legacy IO and IRQ resources
 * @oh: interconnect target module
 * @pdev: platform device to copy resources to
 *
 * We still have legacy DMA and smartreflex needing resources.
 * Let's populate what they need until we can eventually just
 * remove this function. Note that there should be no need to
 * call this from omap_device_build_from_dt(), nor should there
 * be any need to call it for other devices.
 */
static int
omap_device_copy_resources(struct omap_hwmod *oh,
			   struct platform_device *pdev)
{
	struct device_node *np, *child;
	struct property *prop;
	struct resource *res;
	const char *name;
	int error, irq = 0;

	if (!oh || !oh->od || !oh->od->pdev) {
		error = -EINVAL;
		goto error;
	}

	np = oh->od->pdev->dev.of_node;
	if (!np) {
		error = -ENODEV;
		goto error;
	}

	res = kzalloc(sizeof(*res) * 2, GFP_KERNEL);
	if (!res)
		return -ENOMEM;

	/* Do we have a dts range for the interconnect target module? */
	error = omap_hwmod_parse_module_range(oh, np, res);

	/* No ranges, rely on device reg entry */
	if (error)
		error = of_address_to_resource(np, 0, res);
	if (error)
		goto free;

	/* SmartReflex needs first IO resource name to be "mpu" */
	res[0].name = "mpu";

	/*
	 * We may have a configured "ti,sysc" interconnect target with a
	 * dts child with the interrupt. If so use the first child's
	 * first interrupt for "ti-hwmods" legacy support.
	 */
	of_property_for_each_string(np, "compatible", prop, name)
		if (!strncmp("ti,sysc-", name, 8))
			break;

	child = of_get_next_available_child(np, NULL);

	if (name)
		irq = irq_of_parse_and_map(child, 0);
	if (!irq)
		irq = irq_of_parse_and_map(np, 0);
	if (!irq) {
		error = -EINVAL;
		goto free;
	}

	/* Legacy DMA code needs interrupt name to be "0" */
	res[1].start = irq;
	res[1].end = irq;
	res[1].flags = IORESOURCE_IRQ;
	res[1].name = "0";

	error = platform_device_add_resources(pdev, res, 2);

free:
	kfree(res);

error:
	WARN(error, "%s: %s device %s failed: %i\n",
	     __func__, oh->name, dev_name(&pdev->dev),
	     error);

	return error;
}

/**
 * omap_device_build - build and register an omap_device with one omap_hwmod
 * @pdev_name: name of the platform_device driver to use
 * @pdev_id: this platform_device's connection ID
 * @oh: ptr to the single omap_hwmod that backs this omap_device
 * @pdata: platform_data ptr to associate with the platform_device
 * @pdata_len: amount of memory pointed to by @pdata
 *
 * Convenience function for building and registering a single
 * omap_device record, which in turn builds and registers a
 * platform_device record.  See omap_device_build_ss() for more
 * information.  Returns ERR_PTR(-EINVAL) if @oh is NULL; otherwise,
 * passes along the return value of omap_device_build_ss().
 */
struct platform_device __init *omap_device_build(const char *pdev_name,
						 int pdev_id,
						 struct omap_hwmod *oh,
						 void *pdata, int pdata_len)
{
	int ret = -ENOMEM;
	struct platform_device *pdev;
	struct omap_device *od;

	if (!oh || !pdev_name)
		return ERR_PTR(-EINVAL);

	if (!pdata && pdata_len > 0)
		return ERR_PTR(-EINVAL);

	if (strncmp(oh->name, "smartreflex", 11) &&
	    strncmp(oh->name, "dma", 3)) {
		pr_warn("%s need to update %s to probe with dt\na",
			__func__, pdev_name);
		ret = -ENODEV;
		goto odbs_exit;
	}

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

	/*
	 * Must be called before omap_device_alloc() as oh->od
	 * only contains the currently registered omap_device
	 * and will get overwritten by omap_device_alloc().
	 */
	ret = omap_device_copy_resources(oh, pdev);
	if (ret)
		goto odbs_exit1;

	od = omap_device_alloc(pdev, &oh, 1);
	if (IS_ERR(od)) {
		ret = PTR_ERR(od);
		goto odbs_exit1;
	}

	ret = platform_device_add_data(pdev, pdata, pdata_len);
	if (ret)
		goto odbs_exit2;

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

#ifdef CONFIG_PM
static int _od_runtime_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	int ret;

	ret = pm_generic_runtime_suspend(dev);
	if (ret)
		return ret;

	return omap_device_idle(pdev);
}

static int _od_runtime_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	int ret;

	ret = omap_device_enable(pdev);
	if (ret) {
		dev_err(dev, "use pm_runtime_put_sync_suspend() in driver?\n");
		return ret;
	}

	return pm_generic_runtime_resume(dev);
}

static int _od_fail_runtime_suspend(struct device *dev)
{
	dev_warn(dev, "%s: FIXME: missing hwmod/omap_dev info\n", __func__);
	return -ENODEV;
}

static int _od_fail_runtime_resume(struct device *dev)
{
	dev_warn(dev, "%s: FIXME: missing hwmod/omap_dev info\n", __func__);
	return -ENODEV;
}

#endif

#ifdef CONFIG_SUSPEND
static int _od_suspend_noirq(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct omap_device *od = to_omap_device(pdev);
	int ret;

	/* Don't attempt late suspend on a driver that is not bound */
	if (od->_driver_status != BUS_NOTIFY_BOUND_DRIVER)
		return 0;

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

	if (od->flags & OMAP_DEVICE_SUSPENDED) {
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

struct dev_pm_domain omap_device_fail_pm_domain = {
	.ops = {
		SET_RUNTIME_PM_OPS(_od_fail_runtime_suspend,
				   _od_fail_runtime_resume, NULL)
	}
};

struct dev_pm_domain omap_device_pm_domain = {
	.ops = {
		SET_RUNTIME_PM_OPS(_od_runtime_suspend, _od_runtime_resume,
				   NULL)
		USE_PLATFORM_PM_SLEEP_OPS
		SET_NOIRQ_SYSTEM_SLEEP_PM_OPS(_od_suspend_noirq,
					      _od_resume_noirq)
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
int omap_device_register(struct platform_device *pdev)
{
	pr_debug("omap_device: %s: registering\n", pdev->name);

	dev_pm_domain_set(&pdev->dev, &omap_device_pm_domain);
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
 * indirectly via pm_runtime_get*().  Returns -EINVAL if called when
 * the omap_device is already enabled, or passes along the return
 * value of _omap_device_enable_hwmods().
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

	ret = _omap_device_enable_hwmods(od);

	if (ret == 0)
		od->_state = OMAP_DEVICE_STATE_ENABLED;

	return ret;
}

/**
 * omap_device_idle - idle an omap_device
 * @od: struct omap_device * to idle
 *
 * Idle omap_device @od.  Device drivers call this function indirectly
 * via pm_runtime_put*().  Returns -EINVAL if the omap_device is not
 * currently enabled, or passes along the return value of
 * _omap_device_idle_hwmods().
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

	ret = _omap_device_idle_hwmods(od);

	if (ret == 0)
		od->_state = OMAP_DEVICE_STATE_IDLE;

	return ret;
}

/**
 * omap_device_assert_hardreset - set a device's hardreset line
 * @pdev: struct platform_device * to reset
 * @name: const char * name of the reset line
 *
 * Set the hardreset line identified by @name on the IP blocks
 * associated with the hwmods backing the platform_device @pdev.  All
 * of the hwmods associated with @pdev must have the same hardreset
 * line linked to them for this to work.  Passes along the return value
 * of omap_hwmod_assert_hardreset() in the event of any failure, or
 * returns 0 upon success.
 */
int omap_device_assert_hardreset(struct platform_device *pdev, const char *name)
{
	struct omap_device *od = to_omap_device(pdev);
	int ret = 0;
	int i;

	for (i = 0; i < od->hwmods_cnt; i++) {
		ret = omap_hwmod_assert_hardreset(od->hwmods[i], name);
		if (ret)
			break;
	}

	return ret;
}

/**
 * omap_device_deassert_hardreset - release a device's hardreset line
 * @pdev: struct platform_device * to reset
 * @name: const char * name of the reset line
 *
 * Release the hardreset line identified by @name on the IP blocks
 * associated with the hwmods backing the platform_device @pdev.  All
 * of the hwmods associated with @pdev must have the same hardreset
 * line linked to them for this to work.  Passes along the return
 * value of omap_hwmod_deassert_hardreset() in the event of any
 * failure, or returns 0 upon success.
 */
int omap_device_deassert_hardreset(struct platform_device *pdev,
				   const char *name)
{
	struct omap_device *od = to_omap_device(pdev);
	int ret = 0;
	int i;

	for (i = 0; i < od->hwmods_cnt; i++) {
		ret = omap_hwmod_deassert_hardreset(od->hwmods[i], name);
		if (ret)
			break;
	}

	return ret;
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
	if (!oh) {
		WARN(1, "%s: no hwmod for %s\n", __func__,
			oh_name);
		return ERR_PTR(-ENODEV);
	}
	if (!oh->od) {
		WARN(1, "%s: no omap_device for %s\n", __func__,
			oh_name);
		return ERR_PTR(-ENODEV);
	}

	return &oh->od->pdev->dev;
}

static struct notifier_block platform_nb = {
	.notifier_call = _omap_device_notifier_call,
};

static int __init omap_device_init(void)
{
	bus_register_notifier(&platform_bus_type, &platform_nb);
	return 0;
}
omap_postcore_initcall(omap_device_init);

/**
 * omap_device_late_idle - idle devices without drivers
 * @dev: struct device * associated with omap_device
 * @data: unused
 *
 * Check the driver bound status of this device, and idle it
 * if there is no driver attached.
 */
static int __init omap_device_late_idle(struct device *dev, void *data)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct omap_device *od = to_omap_device(pdev);
	int i;

	if (!od)
		return 0;

	/*
	 * If omap_device state is enabled, but has no driver bound,
	 * idle it.
	 */

	/*
	 * Some devices (like memory controllers) are always kept
	 * enabled, and should not be idled even with no drivers.
	 */
	for (i = 0; i < od->hwmods_cnt; i++)
		if (od->hwmods[i]->flags & HWMOD_INIT_NO_IDLE)
			return 0;

	if (od->_driver_status != BUS_NOTIFY_BOUND_DRIVER &&
	    od->_driver_status != BUS_NOTIFY_BIND_DRIVER) {
		if (od->_state == OMAP_DEVICE_STATE_ENABLED) {
			dev_warn(dev, "%s: enabled but no driver.  Idling\n",
				 __func__);
			omap_device_idle(pdev);
		}
	}

	return 0;
}

static int __init omap_device_late_init(void)
{
	bus_for_each_dev(&platform_bus_type, NULL, NULL, omap_device_late_idle);

	return 0;
}
omap_late_initcall_sync(omap_device_late_init);

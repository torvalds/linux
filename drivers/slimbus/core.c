// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2011-2017, The Linux Foundation
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/idr.h>
#include <linux/of.h>
#include <linux/pm_runtime.h>
#include <linux/slimbus.h>
#include "slimbus.h"

static DEFINE_IDA(ctrl_ida);

static const struct slim_device_id *slim_match(const struct slim_device_id *id,
					       const struct slim_device *sbdev)
{
	while (id->manf_id != 0 || id->prod_code != 0) {
		if (id->manf_id == sbdev->e_addr.manf_id &&
		    id->prod_code == sbdev->e_addr.prod_code)
			return id;
		id++;
	}
	return NULL;
}

static int slim_device_match(struct device *dev, struct device_driver *drv)
{
	struct slim_device *sbdev = to_slim_device(dev);
	struct slim_driver *sbdrv = to_slim_driver(drv);

	return !!slim_match(sbdrv->id_table, sbdev);
}

static int slim_device_probe(struct device *dev)
{
	struct slim_device	*sbdev = to_slim_device(dev);
	struct slim_driver	*sbdrv = to_slim_driver(dev->driver);

	return sbdrv->probe(sbdev);
}

static int slim_device_remove(struct device *dev)
{
	struct slim_device *sbdev = to_slim_device(dev);
	struct slim_driver *sbdrv;

	if (dev->driver) {
		sbdrv = to_slim_driver(dev->driver);
		if (sbdrv->remove)
			sbdrv->remove(sbdev);
	}

	return 0;
}

struct bus_type slimbus_bus = {
	.name		= "slimbus",
	.match		= slim_device_match,
	.probe		= slim_device_probe,
	.remove		= slim_device_remove,
};
EXPORT_SYMBOL_GPL(slimbus_bus);

/*
 * __slim_driver_register() - Client driver registration with SLIMbus
 *
 * @drv:Client driver to be associated with client-device.
 * @owner: owning module/driver
 *
 * This API will register the client driver with the SLIMbus
 * It is called from the driver's module-init function.
 */
int __slim_driver_register(struct slim_driver *drv, struct module *owner)
{
	/* ID table and probe are mandatory */
	if (!drv->id_table || !drv->probe)
		return -EINVAL;

	drv->driver.bus = &slimbus_bus;
	drv->driver.owner = owner;

	return driver_register(&drv->driver);
}
EXPORT_SYMBOL_GPL(__slim_driver_register);

/*
 * slim_driver_unregister() - Undo effect of slim_driver_register
 *
 * @drv: Client driver to be unregistered
 */
void slim_driver_unregister(struct slim_driver *drv)
{
	driver_unregister(&drv->driver);
}
EXPORT_SYMBOL_GPL(slim_driver_unregister);

static void slim_dev_release(struct device *dev)
{
	struct slim_device *sbdev = to_slim_device(dev);

	kfree(sbdev);
}

static int slim_add_device(struct slim_controller *ctrl,
			   struct slim_device *sbdev,
			   struct device_node *node)
{
	sbdev->dev.bus = &slimbus_bus;
	sbdev->dev.parent = ctrl->dev;
	sbdev->dev.release = slim_dev_release;
	sbdev->dev.driver = NULL;
	sbdev->ctrl = ctrl;

	if (node)
		sbdev->dev.of_node = of_node_get(node);

	dev_set_name(&sbdev->dev, "%x:%x:%x:%x",
				  sbdev->e_addr.manf_id,
				  sbdev->e_addr.prod_code,
				  sbdev->e_addr.dev_index,
				  sbdev->e_addr.instance);

	return device_register(&sbdev->dev);
}

static struct slim_device *slim_alloc_device(struct slim_controller *ctrl,
					     struct slim_eaddr *eaddr,
					     struct device_node *node)
{
	struct slim_device *sbdev;
	int ret;

	sbdev = kzalloc(sizeof(*sbdev), GFP_KERNEL);
	if (!sbdev)
		return NULL;

	sbdev->e_addr = *eaddr;
	ret = slim_add_device(ctrl, sbdev, node);
	if (ret) {
		put_device(&sbdev->dev);
		return NULL;
	}

	return sbdev;
}

static void of_register_slim_devices(struct slim_controller *ctrl)
{
	struct device *dev = ctrl->dev;
	struct device_node *node;

	if (!ctrl->dev->of_node)
		return;

	for_each_child_of_node(ctrl->dev->of_node, node) {
		struct slim_device *sbdev;
		struct slim_eaddr e_addr;
		const char *compat = NULL;
		int reg[2], ret;
		int manf_id, prod_code;

		compat = of_get_property(node, "compatible", NULL);
		if (!compat)
			continue;

		ret = sscanf(compat, "slim%x,%x", &manf_id, &prod_code);
		if (ret != 2) {
			dev_err(dev, "Manf ID & Product code not found %s\n",
				compat);
			continue;
		}

		ret = of_property_read_u32_array(node, "reg", reg, 2);
		if (ret) {
			dev_err(dev, "Device and Instance id not found:%d\n",
				ret);
			continue;
		}

		e_addr.dev_index = reg[0];
		e_addr.instance = reg[1];
		e_addr.manf_id = manf_id;
		e_addr.prod_code = prod_code;

		sbdev = slim_alloc_device(ctrl, &e_addr, node);
		if (!sbdev)
			continue;
	}
}

/*
 * slim_register_controller() - Controller bring-up and registration.
 *
 * @ctrl: Controller to be registered.
 *
 * A controller is registered with the framework using this API.
 * If devices on a controller were registered before controller,
 * this will make sure that they get probed when controller is up
 */
int slim_register_controller(struct slim_controller *ctrl)
{
	int id;

	id = ida_simple_get(&ctrl_ida, 0, 0, GFP_KERNEL);
	if (id < 0)
		return id;

	ctrl->id = id;

	if (!ctrl->min_cg)
		ctrl->min_cg = SLIM_MIN_CLK_GEAR;
	if (!ctrl->max_cg)
		ctrl->max_cg = SLIM_MAX_CLK_GEAR;

	ida_init(&ctrl->laddr_ida);
	idr_init(&ctrl->tid_idr);
	mutex_init(&ctrl->lock);
	mutex_init(&ctrl->sched.m_reconf);
	init_completion(&ctrl->sched.pause_comp);

	dev_dbg(ctrl->dev, "Bus [%s] registered:dev:%p\n",
		ctrl->name, ctrl->dev);

	of_register_slim_devices(ctrl);

	return 0;
}
EXPORT_SYMBOL_GPL(slim_register_controller);

/* slim_remove_device: Remove the effect of slim_add_device() */
static void slim_remove_device(struct slim_device *sbdev)
{
	device_unregister(&sbdev->dev);
}

static int slim_ctrl_remove_device(struct device *dev, void *null)
{
	slim_remove_device(to_slim_device(dev));
	return 0;
}

/**
 * slim_unregister_controller() - Controller tear-down.
 *
 * @ctrl: Controller to tear-down.
 */
int slim_unregister_controller(struct slim_controller *ctrl)
{
	/* Remove all clients */
	device_for_each_child(ctrl->dev, NULL, slim_ctrl_remove_device);
	/* Enter Clock Pause */
	slim_ctrl_clk_pause(ctrl, false, 0);
	ida_simple_remove(&ctrl_ida, ctrl->id);

	return 0;
}
EXPORT_SYMBOL_GPL(slim_unregister_controller);

static void slim_device_update_status(struct slim_device *sbdev,
				      enum slim_device_status status)
{
	struct slim_driver *sbdrv;

	if (sbdev->status == status)
		return;

	sbdev->status = status;
	if (!sbdev->dev.driver)
		return;

	sbdrv = to_slim_driver(sbdev->dev.driver);
	if (sbdrv->device_status)
		sbdrv->device_status(sbdev, sbdev->status);
}

/**
 * slim_report_absent() - Controller calls this function when a device
 *	reports absent, OR when the device cannot be communicated with
 *
 * @sbdev: Device that cannot be reached, or sent report absent
 */
void slim_report_absent(struct slim_device *sbdev)
{
	struct slim_controller *ctrl = sbdev->ctrl;

	if (!ctrl)
		return;

	/* invalidate logical addresses */
	mutex_lock(&ctrl->lock);
	sbdev->is_laddr_valid = false;
	mutex_unlock(&ctrl->lock);

	ida_simple_remove(&ctrl->laddr_ida, sbdev->laddr);
	slim_device_update_status(sbdev, SLIM_DEVICE_STATUS_DOWN);
}
EXPORT_SYMBOL_GPL(slim_report_absent);

static bool slim_eaddr_equal(struct slim_eaddr *a, struct slim_eaddr *b)
{
	return (a->manf_id == b->manf_id &&
		a->prod_code == b->prod_code &&
		a->dev_index == b->dev_index &&
		a->instance == b->instance);
}

static int slim_match_dev(struct device *dev, void *data)
{
	struct slim_eaddr *e_addr = data;
	struct slim_device *sbdev = to_slim_device(dev);

	return slim_eaddr_equal(&sbdev->e_addr, e_addr);
}

static struct slim_device *find_slim_device(struct slim_controller *ctrl,
					    struct slim_eaddr *eaddr)
{
	struct slim_device *sbdev;
	struct device *dev;

	dev = device_find_child(ctrl->dev, eaddr, slim_match_dev);
	if (dev) {
		sbdev = to_slim_device(dev);
		return sbdev;
	}

	return NULL;
}

/**
 * slim_get_device() - get handle to a device.
 *
 * @ctrl: Controller on which this device will be added/queried
 * @e_addr: Enumeration address of the device to be queried
 *
 * Return: pointer to a device if it has already reported. Creates a new
 * device and returns pointer to it if the device has not yet enumerated.
 */
struct slim_device *slim_get_device(struct slim_controller *ctrl,
				    struct slim_eaddr *e_addr)
{
	struct slim_device *sbdev;

	sbdev = find_slim_device(ctrl, e_addr);
	if (!sbdev) {
		sbdev = slim_alloc_device(ctrl, e_addr, NULL);
		if (!sbdev)
			return ERR_PTR(-ENOMEM);
	}

	return sbdev;
}
EXPORT_SYMBOL_GPL(slim_get_device);

static int of_slim_match_dev(struct device *dev, void *data)
{
	struct device_node *np = data;
	struct slim_device *sbdev = to_slim_device(dev);

	return (sbdev->dev.of_node == np);
}

static struct slim_device *of_find_slim_device(struct slim_controller *ctrl,
					       struct device_node *np)
{
	struct slim_device *sbdev;
	struct device *dev;

	dev = device_find_child(ctrl->dev, np, of_slim_match_dev);
	if (dev) {
		sbdev = to_slim_device(dev);
		return sbdev;
	}

	return NULL;
}

/**
 * of_slim_get_device() - get handle to a device using dt node.
 *
 * @ctrl: Controller on which this device will be added/queried
 * @np: node pointer to device
 *
 * Return: pointer to a device if it has already reported. Creates a new
 * device and returns pointer to it if the device has not yet enumerated.
 */
struct slim_device *of_slim_get_device(struct slim_controller *ctrl,
				       struct device_node *np)
{
	return of_find_slim_device(ctrl, np);
}
EXPORT_SYMBOL_GPL(of_slim_get_device);

static int slim_device_alloc_laddr(struct slim_device *sbdev,
				   bool report_present)
{
	struct slim_controller *ctrl = sbdev->ctrl;
	u8 laddr;
	int ret;

	mutex_lock(&ctrl->lock);
	if (ctrl->get_laddr) {
		ret = ctrl->get_laddr(ctrl, &sbdev->e_addr, &laddr);
		if (ret < 0)
			goto err;
	} else if (report_present) {
		ret = ida_simple_get(&ctrl->laddr_ida,
				     0, SLIM_LA_MANAGER - 1, GFP_KERNEL);
		if (ret < 0)
			goto err;

		laddr = ret;
	} else {
		ret = -EINVAL;
		goto err;
	}

	if (ctrl->set_laddr) {
		ret = ctrl->set_laddr(ctrl, &sbdev->e_addr, laddr);
		if (ret) {
			ret = -EINVAL;
			goto err;
		}
	}

	sbdev->laddr = laddr;
	sbdev->is_laddr_valid = true;

	slim_device_update_status(sbdev, SLIM_DEVICE_STATUS_UP);

	dev_dbg(ctrl->dev, "setting slimbus l-addr:%x, ea:%x,%x,%x,%x\n",
		laddr, sbdev->e_addr.manf_id, sbdev->e_addr.prod_code,
		sbdev->e_addr.dev_index, sbdev->e_addr.instance);

err:
	mutex_unlock(&ctrl->lock);
	return ret;

}

/**
 * slim_device_report_present() - Report enumerated device.
 *
 * @ctrl: Controller with which device is enumerated.
 * @e_addr: Enumeration address of the device.
 * @laddr: Return logical address (if valid flag is false)
 *
 * Called by controller in response to REPORT_PRESENT. Framework will assign
 * a logical address to this enumeration address.
 * Function returns -EXFULL to indicate that all logical addresses are already
 * taken.
 */
int slim_device_report_present(struct slim_controller *ctrl,
			       struct slim_eaddr *e_addr, u8 *laddr)
{
	struct slim_device *sbdev;
	int ret;

	ret = pm_runtime_get_sync(ctrl->dev);

	if (ctrl->sched.clk_state != SLIM_CLK_ACTIVE) {
		dev_err(ctrl->dev, "slim ctrl not active,state:%d, ret:%d\n",
				    ctrl->sched.clk_state, ret);
		goto slimbus_not_active;
	}

	sbdev = slim_get_device(ctrl, e_addr);
	if (IS_ERR(sbdev))
		return -ENODEV;

	if (sbdev->is_laddr_valid) {
		*laddr = sbdev->laddr;
		return 0;
	}

	ret = slim_device_alloc_laddr(sbdev, true);

slimbus_not_active:
	pm_runtime_mark_last_busy(ctrl->dev);
	pm_runtime_put_autosuspend(ctrl->dev);
	return ret;
}
EXPORT_SYMBOL_GPL(slim_device_report_present);

/**
 * slim_get_logical_addr() - get/allocate logical address of a SLIMbus device.
 *
 * @sbdev: client handle requesting the address.
 *
 * Return: zero if a logical address is valid or a new logical address
 * has been assigned. error code in case of error.
 */
int slim_get_logical_addr(struct slim_device *sbdev)
{
	if (!sbdev->is_laddr_valid)
		return slim_device_alloc_laddr(sbdev, false);

	return 0;
}
EXPORT_SYMBOL_GPL(slim_get_logical_addr);

static void __exit slimbus_exit(void)
{
	bus_unregister(&slimbus_bus);
}
module_exit(slimbus_exit);

static int __init slimbus_init(void)
{
	return bus_register(&slimbus_bus);
}
postcore_initcall(slimbus_init);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("SLIMbus core");

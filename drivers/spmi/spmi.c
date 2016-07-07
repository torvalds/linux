/*
 * Copyright (c) 2012-2015, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/idr.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/spmi.h>
#include <linux/pm_runtime.h>

#include <dt-bindings/spmi/spmi.h>
#define CREATE_TRACE_POINTS
#include <trace/events/spmi.h>

static bool is_registered;
static DEFINE_IDA(ctrl_ida);

static void spmi_dev_release(struct device *dev)
{
	struct spmi_device *sdev = to_spmi_device(dev);
	kfree(sdev);
}

static const struct device_type spmi_dev_type = {
	.release	= spmi_dev_release,
};

static void spmi_ctrl_release(struct device *dev)
{
	struct spmi_controller *ctrl = to_spmi_controller(dev);
	ida_simple_remove(&ctrl_ida, ctrl->nr);
	kfree(ctrl);
}

static const struct device_type spmi_ctrl_type = {
	.release	= spmi_ctrl_release,
};

static int spmi_device_match(struct device *dev, struct device_driver *drv)
{
	if (of_driver_match_device(dev, drv))
		return 1;

	if (drv->name)
		return strncmp(dev_name(dev), drv->name,
			       SPMI_NAME_SIZE) == 0;

	return 0;
}

/**
 * spmi_device_add() - add a device previously constructed via spmi_device_alloc()
 * @sdev:	spmi_device to be added
 */
int spmi_device_add(struct spmi_device *sdev)
{
	struct spmi_controller *ctrl = sdev->ctrl;
	int err;

	dev_set_name(&sdev->dev, "%d-%02x", ctrl->nr, sdev->usid);

	err = device_add(&sdev->dev);
	if (err < 0) {
		dev_err(&sdev->dev, "Can't add %s, status %d\n",
			dev_name(&sdev->dev), err);
		goto err_device_add;
	}

	dev_dbg(&sdev->dev, "device %s registered\n", dev_name(&sdev->dev));

err_device_add:
	return err;
}
EXPORT_SYMBOL_GPL(spmi_device_add);

/**
 * spmi_device_remove(): remove an SPMI device
 * @sdev:	spmi_device to be removed
 */
void spmi_device_remove(struct spmi_device *sdev)
{
	device_unregister(&sdev->dev);
}
EXPORT_SYMBOL_GPL(spmi_device_remove);

static inline int
spmi_cmd(struct spmi_controller *ctrl, u8 opcode, u8 sid)
{
	int ret;

	if (!ctrl || !ctrl->cmd || ctrl->dev.type != &spmi_ctrl_type)
		return -EINVAL;

	ret = ctrl->cmd(ctrl, opcode, sid);
	trace_spmi_cmd(opcode, sid, ret);
	return ret;
}

static inline int spmi_read_cmd(struct spmi_controller *ctrl, u8 opcode,
				u8 sid, u16 addr, u8 *buf, size_t len)
{
	int ret;

	if (!ctrl || !ctrl->read_cmd || ctrl->dev.type != &spmi_ctrl_type)
		return -EINVAL;

	trace_spmi_read_begin(opcode, sid, addr);
	ret = ctrl->read_cmd(ctrl, opcode, sid, addr, buf, len);
	trace_spmi_read_end(opcode, sid, addr, ret, len, buf);
	return ret;
}

static inline int spmi_write_cmd(struct spmi_controller *ctrl, u8 opcode,
				 u8 sid, u16 addr, const u8 *buf, size_t len)
{
	int ret;

	if (!ctrl || !ctrl->write_cmd || ctrl->dev.type != &spmi_ctrl_type)
		return -EINVAL;

	trace_spmi_write_begin(opcode, sid, addr, len, buf);
	ret = ctrl->write_cmd(ctrl, opcode, sid, addr, buf, len);
	trace_spmi_write_end(opcode, sid, addr, ret);
	return ret;
}

/**
 * spmi_register_read() - register read
 * @sdev:	SPMI device.
 * @addr:	slave register address (5-bit address).
 * @buf:	buffer to be populated with data from the Slave.
 *
 * Reads 1 byte of data from a Slave device register.
 */
int spmi_register_read(struct spmi_device *sdev, u8 addr, u8 *buf)
{
	/* 5-bit register address */
	if (addr > 0x1F)
		return -EINVAL;

	return spmi_read_cmd(sdev->ctrl, SPMI_CMD_READ, sdev->usid, addr,
			     buf, 1);
}
EXPORT_SYMBOL_GPL(spmi_register_read);

/**
 * spmi_ext_register_read() - extended register read
 * @sdev:	SPMI device.
 * @addr:	slave register address (8-bit address).
 * @buf:	buffer to be populated with data from the Slave.
 * @len:	the request number of bytes to read (up to 16 bytes).
 *
 * Reads up to 16 bytes of data from the extended register space on a
 * Slave device.
 */
int spmi_ext_register_read(struct spmi_device *sdev, u8 addr, u8 *buf,
			   size_t len)
{
	/* 8-bit register address, up to 16 bytes */
	if (len == 0 || len > 16)
		return -EINVAL;

	return spmi_read_cmd(sdev->ctrl, SPMI_CMD_EXT_READ, sdev->usid, addr,
			     buf, len);
}
EXPORT_SYMBOL_GPL(spmi_ext_register_read);

/**
 * spmi_ext_register_readl() - extended register read long
 * @sdev:	SPMI device.
 * @addr:	slave register address (16-bit address).
 * @buf:	buffer to be populated with data from the Slave.
 * @len:	the request number of bytes to read (up to 8 bytes).
 *
 * Reads up to 8 bytes of data from the extended register space on a
 * Slave device using 16-bit address.
 */
int spmi_ext_register_readl(struct spmi_device *sdev, u16 addr, u8 *buf,
			    size_t len)
{
	/* 16-bit register address, up to 8 bytes */
	if (len == 0 || len > 8)
		return -EINVAL;

	return spmi_read_cmd(sdev->ctrl, SPMI_CMD_EXT_READL, sdev->usid, addr,
			     buf, len);
}
EXPORT_SYMBOL_GPL(spmi_ext_register_readl);

/**
 * spmi_register_write() - register write
 * @sdev:	SPMI device
 * @addr:	slave register address (5-bit address).
 * @data:	buffer containing the data to be transferred to the Slave.
 *
 * Writes 1 byte of data to a Slave device register.
 */
int spmi_register_write(struct spmi_device *sdev, u8 addr, u8 data)
{
	/* 5-bit register address */
	if (addr > 0x1F)
		return -EINVAL;

	return spmi_write_cmd(sdev->ctrl, SPMI_CMD_WRITE, sdev->usid, addr,
			      &data, 1);
}
EXPORT_SYMBOL_GPL(spmi_register_write);

/**
 * spmi_register_zero_write() - register zero write
 * @sdev:	SPMI device.
 * @data:	the data to be written to register 0 (7-bits).
 *
 * Writes data to register 0 of the Slave device.
 */
int spmi_register_zero_write(struct spmi_device *sdev, u8 data)
{
	return spmi_write_cmd(sdev->ctrl, SPMI_CMD_ZERO_WRITE, sdev->usid, 0,
			      &data, 1);
}
EXPORT_SYMBOL_GPL(spmi_register_zero_write);

/**
 * spmi_ext_register_write() - extended register write
 * @sdev:	SPMI device.
 * @addr:	slave register address (8-bit address).
 * @buf:	buffer containing the data to be transferred to the Slave.
 * @len:	the request number of bytes to read (up to 16 bytes).
 *
 * Writes up to 16 bytes of data to the extended register space of a
 * Slave device.
 */
int spmi_ext_register_write(struct spmi_device *sdev, u8 addr, const u8 *buf,
			    size_t len)
{
	/* 8-bit register address, up to 16 bytes */
	if (len == 0 || len > 16)
		return -EINVAL;

	return spmi_write_cmd(sdev->ctrl, SPMI_CMD_EXT_WRITE, sdev->usid, addr,
			      buf, len);
}
EXPORT_SYMBOL_GPL(spmi_ext_register_write);

/**
 * spmi_ext_register_writel() - extended register write long
 * @sdev:	SPMI device.
 * @addr:	slave register address (16-bit address).
 * @buf:	buffer containing the data to be transferred to the Slave.
 * @len:	the request number of bytes to read (up to 8 bytes).
 *
 * Writes up to 8 bytes of data to the extended register space of a
 * Slave device using 16-bit address.
 */
int spmi_ext_register_writel(struct spmi_device *sdev, u16 addr, const u8 *buf,
			     size_t len)
{
	/* 4-bit Slave Identifier, 16-bit register address, up to 8 bytes */
	if (len == 0 || len > 8)
		return -EINVAL;

	return spmi_write_cmd(sdev->ctrl, SPMI_CMD_EXT_WRITEL, sdev->usid,
			      addr, buf, len);
}
EXPORT_SYMBOL_GPL(spmi_ext_register_writel);

/**
 * spmi_command_reset() - sends RESET command to the specified slave
 * @sdev:	SPMI device.
 *
 * The Reset command initializes the Slave and forces all registers to
 * their reset values. The Slave shall enter the STARTUP state after
 * receiving a Reset command.
 */
int spmi_command_reset(struct spmi_device *sdev)
{
	return spmi_cmd(sdev->ctrl, SPMI_CMD_RESET, sdev->usid);
}
EXPORT_SYMBOL_GPL(spmi_command_reset);

/**
 * spmi_command_sleep() - sends SLEEP command to the specified SPMI device
 * @sdev:	SPMI device.
 *
 * The Sleep command causes the Slave to enter the user defined SLEEP state.
 */
int spmi_command_sleep(struct spmi_device *sdev)
{
	return spmi_cmd(sdev->ctrl, SPMI_CMD_SLEEP, sdev->usid);
}
EXPORT_SYMBOL_GPL(spmi_command_sleep);

/**
 * spmi_command_wakeup() - sends WAKEUP command to the specified SPMI device
 * @sdev:	SPMI device.
 *
 * The Wakeup command causes the Slave to move from the SLEEP state to
 * the ACTIVE state.
 */
int spmi_command_wakeup(struct spmi_device *sdev)
{
	return spmi_cmd(sdev->ctrl, SPMI_CMD_WAKEUP, sdev->usid);
}
EXPORT_SYMBOL_GPL(spmi_command_wakeup);

/**
 * spmi_command_shutdown() - sends SHUTDOWN command to the specified SPMI device
 * @sdev:	SPMI device.
 *
 * The Shutdown command causes the Slave to enter the SHUTDOWN state.
 */
int spmi_command_shutdown(struct spmi_device *sdev)
{
	return spmi_cmd(sdev->ctrl, SPMI_CMD_SHUTDOWN, sdev->usid);
}
EXPORT_SYMBOL_GPL(spmi_command_shutdown);

static int spmi_drv_probe(struct device *dev)
{
	const struct spmi_driver *sdrv = to_spmi_driver(dev->driver);
	struct spmi_device *sdev = to_spmi_device(dev);
	int err;

	pm_runtime_get_noresume(dev);
	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);

	err = sdrv->probe(sdev);
	if (err)
		goto fail_probe;

	return 0;

fail_probe:
	pm_runtime_disable(dev);
	pm_runtime_set_suspended(dev);
	pm_runtime_put_noidle(dev);
	return err;
}

static int spmi_drv_remove(struct device *dev)
{
	const struct spmi_driver *sdrv = to_spmi_driver(dev->driver);

	pm_runtime_get_sync(dev);
	sdrv->remove(to_spmi_device(dev));
	pm_runtime_put_noidle(dev);

	pm_runtime_disable(dev);
	pm_runtime_set_suspended(dev);
	pm_runtime_put_noidle(dev);
	return 0;
}

static struct bus_type spmi_bus_type = {
	.name		= "spmi",
	.match		= spmi_device_match,
	.probe		= spmi_drv_probe,
	.remove		= spmi_drv_remove,
};

/**
 * spmi_controller_alloc() - Allocate a new SPMI device
 * @ctrl:	associated controller
 *
 * Caller is responsible for either calling spmi_device_add() to add the
 * newly allocated controller, or calling spmi_device_put() to discard it.
 */
struct spmi_device *spmi_device_alloc(struct spmi_controller *ctrl)
{
	struct spmi_device *sdev;

	sdev = kzalloc(sizeof(*sdev), GFP_KERNEL);
	if (!sdev)
		return NULL;

	sdev->ctrl = ctrl;
	device_initialize(&sdev->dev);
	sdev->dev.parent = &ctrl->dev;
	sdev->dev.bus = &spmi_bus_type;
	sdev->dev.type = &spmi_dev_type;
	return sdev;
}
EXPORT_SYMBOL_GPL(spmi_device_alloc);

/**
 * spmi_controller_alloc() - Allocate a new SPMI controller
 * @parent:	parent device
 * @size:	size of private data
 *
 * Caller is responsible for either calling spmi_controller_add() to add the
 * newly allocated controller, or calling spmi_controller_put() to discard it.
 * The allocated private data region may be accessed via
 * spmi_controller_get_drvdata()
 */
struct spmi_controller *spmi_controller_alloc(struct device *parent,
					      size_t size)
{
	struct spmi_controller *ctrl;
	int id;

	if (WARN_ON(!parent))
		return NULL;

	ctrl = kzalloc(sizeof(*ctrl) + size, GFP_KERNEL);
	if (!ctrl)
		return NULL;

	device_initialize(&ctrl->dev);
	ctrl->dev.type = &spmi_ctrl_type;
	ctrl->dev.bus = &spmi_bus_type;
	ctrl->dev.parent = parent;
	ctrl->dev.of_node = parent->of_node;
	spmi_controller_set_drvdata(ctrl, &ctrl[1]);

	id = ida_simple_get(&ctrl_ida, 0, 0, GFP_KERNEL);
	if (id < 0) {
		dev_err(parent,
			"unable to allocate SPMI controller identifier.\n");
		spmi_controller_put(ctrl);
		return NULL;
	}

	ctrl->nr = id;
	dev_set_name(&ctrl->dev, "spmi-%d", id);

	dev_dbg(&ctrl->dev, "allocated controller 0x%p id %d\n", ctrl, id);
	return ctrl;
}
EXPORT_SYMBOL_GPL(spmi_controller_alloc);

static void of_spmi_register_devices(struct spmi_controller *ctrl)
{
	struct device_node *node;
	int err;

	if (!ctrl->dev.of_node)
		return;

	for_each_available_child_of_node(ctrl->dev.of_node, node) {
		struct spmi_device *sdev;
		u32 reg[2];

		dev_dbg(&ctrl->dev, "adding child %s\n", node->full_name);

		err = of_property_read_u32_array(node, "reg", reg, 2);
		if (err) {
			dev_err(&ctrl->dev,
				"node %s err (%d) does not have 'reg' property\n",
				node->full_name, err);
			continue;
		}

		if (reg[1] != SPMI_USID) {
			dev_err(&ctrl->dev,
				"node %s contains unsupported 'reg' entry\n",
				node->full_name);
			continue;
		}

		if (reg[0] >= SPMI_MAX_SLAVE_ID) {
			dev_err(&ctrl->dev,
				"invalid usid on node %s\n",
				node->full_name);
			continue;
		}

		dev_dbg(&ctrl->dev, "read usid %02x\n", reg[0]);

		sdev = spmi_device_alloc(ctrl);
		if (!sdev)
			continue;

		sdev->dev.of_node = node;
		sdev->usid = (u8) reg[0];

		err = spmi_device_add(sdev);
		if (err) {
			dev_err(&sdev->dev,
				"failure adding device. status %d\n", err);
			spmi_device_put(sdev);
		}
	}
}

/**
 * spmi_controller_add() - Add an SPMI controller
 * @ctrl:	controller to be registered.
 *
 * Register a controller previously allocated via spmi_controller_alloc() with
 * the SPMI core.
 */
int spmi_controller_add(struct spmi_controller *ctrl)
{
	int ret;

	/* Can't register until after driver model init */
	if (WARN_ON(!is_registered))
		return -EAGAIN;

	ret = device_add(&ctrl->dev);
	if (ret)
		return ret;

	if (IS_ENABLED(CONFIG_OF))
		of_spmi_register_devices(ctrl);

	dev_dbg(&ctrl->dev, "spmi-%d registered: dev:%p\n",
		ctrl->nr, &ctrl->dev);

	return 0;
};
EXPORT_SYMBOL_GPL(spmi_controller_add);

/* Remove a device associated with a controller */
static int spmi_ctrl_remove_device(struct device *dev, void *data)
{
	struct spmi_device *spmidev = to_spmi_device(dev);
	if (dev->type == &spmi_dev_type)
		spmi_device_remove(spmidev);
	return 0;
}

/**
 * spmi_controller_remove(): remove an SPMI controller
 * @ctrl:	controller to remove
 *
 * Remove a SPMI controller.  Caller is responsible for calling
 * spmi_controller_put() to discard the allocated controller.
 */
void spmi_controller_remove(struct spmi_controller *ctrl)
{
	int dummy;

	if (!ctrl)
		return;

	dummy = device_for_each_child(&ctrl->dev, NULL,
				      spmi_ctrl_remove_device);
	device_del(&ctrl->dev);
}
EXPORT_SYMBOL_GPL(spmi_controller_remove);

/**
 * spmi_driver_register() - Register client driver with SPMI core
 * @sdrv:	client driver to be associated with client-device.
 *
 * This API will register the client driver with the SPMI framework.
 * It is typically called from the driver's module-init function.
 */
int __spmi_driver_register(struct spmi_driver *sdrv, struct module *owner)
{
	sdrv->driver.bus = &spmi_bus_type;
	sdrv->driver.owner = owner;
	return driver_register(&sdrv->driver);
}
EXPORT_SYMBOL_GPL(__spmi_driver_register);

static void __exit spmi_exit(void)
{
	bus_unregister(&spmi_bus_type);
}
module_exit(spmi_exit);

static int __init spmi_init(void)
{
	int ret;

	ret = bus_register(&spmi_bus_type);
	if (ret)
		return ret;

	is_registered = true;
	return 0;
}
postcore_initcall(spmi_init);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("SPMI module");
MODULE_ALIAS("platform:spmi");

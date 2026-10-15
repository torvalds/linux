// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Arm Limited
 */

#include <linux/arm-smccc-bus.h>
#include <linux/slab.h>

static int arm_smccc_bus_match(struct device *dev,
		const struct device_driver *drv)
{
	const struct arm_smccc_device_id *id_table;
	const struct arm_smccc_device *smccc_dev = to_arm_smccc_device(dev);

	id_table = to_arm_smccc_driver(drv)->id_table;
	if (!id_table)
		return 0;

	while (id_table->func_id) {
		if (smccc_dev->func_id == id_table->func_id)
			return 1;
		id_table++;
	}

	return 0;
}

static int arm_smccc_bus_probe(struct device *dev)
{
	struct arm_smccc_driver *smccc_drv = to_arm_smccc_driver(dev->driver);

	return smccc_drv->probe(to_arm_smccc_device(dev));
}

static void arm_smccc_bus_remove(struct device *dev)
{
	struct arm_smccc_driver *smcc_drv = to_arm_smccc_driver(dev->driver);

	if (smcc_drv->remove)
		smcc_drv->remove(to_arm_smccc_device(dev));
}

static int arm_smccc_bus_uevent(const struct device *dev,
		struct kobj_uevent_env *env)
{
	const struct arm_smccc_device *smccc_dev = to_arm_smccc_device(dev);

	return add_uevent_var(env, "MODALIAS=" ARM_SMCCC_MODULE_PREFIX "f%08X",
			      smccc_dev->func_id);
}

static ssize_t modalias_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct arm_smccc_device *smccc_dev = to_arm_smccc_device(dev);

	return sysfs_emit(buf, ARM_SMCCC_MODULE_PREFIX "f%08X\n",
			  smccc_dev->func_id);
}
static DEVICE_ATTR_RO(modalias);

static struct attribute *arm_smccc_device_attrs[] = {
	&dev_attr_modalias.attr,
	NULL,
};
ATTRIBUTE_GROUPS(arm_smccc_device);

const struct bus_type arm_smccc_bus_type = {
	.name = "arm_smccc",
	.match = arm_smccc_bus_match,
	.probe = arm_smccc_bus_probe,
	.remove = arm_smccc_bus_remove,
	.uevent = arm_smccc_bus_uevent,
	.dev_groups = arm_smccc_device_groups,
};
EXPORT_SYMBOL_GPL(arm_smccc_bus_type);

int arm_smccc_driver_register(struct arm_smccc_driver *driver,
		struct module *owner, const char *mod_name)
{
	if (!driver->probe || !driver->id_table)
		return -EINVAL;

	driver->driver.bus = &arm_smccc_bus_type;
	driver->driver.name = driver->name;
	driver->driver.owner = owner;
	driver->driver.mod_name = mod_name;

	return driver_register(&driver->driver);
}
EXPORT_SYMBOL_GPL(arm_smccc_driver_register);

void arm_smccc_driver_unregister(struct arm_smccc_driver *driver)
{
	driver_unregister(&driver->driver);
}
EXPORT_SYMBOL_GPL(arm_smccc_driver_unregister);

static void arm_smccc_release_device(struct device *dev)
{
	struct arm_smccc_device *smccc_dev = to_arm_smccc_device(dev);

	kfree(smccc_dev);
}

static struct device *arm_smccc_device_alloc(u32 func_id)
{
	struct arm_smccc_device *smccc_dev;

	smccc_dev = kzalloc_obj(*smccc_dev);
	if (!smccc_dev)
		return NULL;

	smccc_dev->func_id = func_id;
	smccc_dev->dev.bus = &arm_smccc_bus_type;
	smccc_dev->dev.release = arm_smccc_release_device;
	device_initialize(&smccc_dev->dev);

	return &smccc_dev->dev;
}

struct arm_smccc_device *arm_smccc_device_register(const char *name, u32 func_id)
{
	int ret;

	if (!name)
		return ERR_PTR(-EINVAL);

	struct device *dev __free(put_device) = arm_smccc_device_alloc(func_id);
	if (!dev)
		return ERR_PTR(-ENOMEM);

	ret = dev_set_name(dev, "%s", name);
	if (ret)
		return ERR_PTR(ret);

	ret = device_add(dev);
	if (ret)
		return ERR_PTR(ret);

	return to_arm_smccc_device(no_free_ptr(dev));
}
EXPORT_SYMBOL_GPL(arm_smccc_device_register);

void arm_smccc_device_unregister(struct arm_smccc_device *smccc_dev)
{
	if (!smccc_dev)
		return;

	device_unregister(&smccc_dev->dev);
}
EXPORT_SYMBOL_GPL(arm_smccc_device_unregister);

static int __init arm_smccc_bus_init(void)
{
	return bus_register(&arm_smccc_bus_type);
}
subsys_initcall(arm_smccc_bus_init);

/*
 * Intel(R) Trace Hub driver core
 *
 * Copyright (C) 2014-2015 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#define pr_fmt(fmt)	KBUILD_MODNAME ": " fmt

#include <linux/types.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/sysfs.h>
#include <linux/kdev_t.h>
#include <linux/debugfs.h>
#include <linux/idr.h>
#include <linux/pci.h>
#include <linux/dma-mapping.h>

#include "intel_th.h"
#include "debug.h"

static DEFINE_IDA(intel_th_ida);

static int intel_th_match(struct device *dev, struct device_driver *driver)
{
	struct intel_th_driver *thdrv = to_intel_th_driver(driver);
	struct intel_th_device *thdev = to_intel_th_device(dev);

	if (thdev->type == INTEL_TH_SWITCH &&
	    (!thdrv->enable || !thdrv->disable))
		return 0;

	return !strcmp(thdev->name, driver->name);
}

static int intel_th_child_remove(struct device *dev, void *data)
{
	device_release_driver(dev);

	return 0;
}

static int intel_th_probe(struct device *dev)
{
	struct intel_th_driver *thdrv = to_intel_th_driver(dev->driver);
	struct intel_th_device *thdev = to_intel_th_device(dev);
	struct intel_th_driver *hubdrv;
	struct intel_th_device *hub = NULL;
	int ret;

	if (thdev->type == INTEL_TH_SWITCH)
		hub = thdev;
	else if (dev->parent)
		hub = to_intel_th_device(dev->parent);

	if (!hub || !hub->dev.driver)
		return -EPROBE_DEFER;

	hubdrv = to_intel_th_driver(hub->dev.driver);

	ret = thdrv->probe(to_intel_th_device(dev));
	if (ret)
		return ret;

	if (thdev->type == INTEL_TH_OUTPUT &&
	    !intel_th_output_assigned(thdev))
		ret = hubdrv->assign(hub, thdev);

	return ret;
}

static int intel_th_remove(struct device *dev)
{
	struct intel_th_driver *thdrv = to_intel_th_driver(dev->driver);
	struct intel_th_device *thdev = to_intel_th_device(dev);
	struct intel_th_device *hub = to_intel_th_device(dev->parent);
	int err;

	if (thdev->type == INTEL_TH_SWITCH) {
		err = device_for_each_child(dev, thdev, intel_th_child_remove);
		if (err)
			return err;
	}

	thdrv->remove(thdev);

	if (intel_th_output_assigned(thdev)) {
		struct intel_th_driver *hubdrv =
			to_intel_th_driver(dev->parent->driver);

		if (hub->dev.driver)
			hubdrv->unassign(hub, thdev);
	}

	return 0;
}

static struct bus_type intel_th_bus = {
	.name		= "intel_th",
	.dev_attrs	= NULL,
	.match		= intel_th_match,
	.probe		= intel_th_probe,
	.remove		= intel_th_remove,
};

static void intel_th_device_free(struct intel_th_device *thdev);

static void intel_th_device_release(struct device *dev)
{
	intel_th_device_free(to_intel_th_device(dev));
}

static struct device_type intel_th_source_device_type = {
	.name		= "intel_th_source_device",
	.release	= intel_th_device_release,
};

static struct intel_th *to_intel_th(struct intel_th_device *thdev)
{
	/*
	 * subdevice tree is flat: if this one is not a switch, its
	 * parent must be
	 */
	if (thdev->type != INTEL_TH_SWITCH)
		thdev = to_intel_th_hub(thdev);

	if (WARN_ON_ONCE(!thdev || thdev->type != INTEL_TH_SWITCH))
		return NULL;

	return dev_get_drvdata(thdev->dev.parent);
}

static char *intel_th_output_devnode(struct device *dev, umode_t *mode,
				     kuid_t *uid, kgid_t *gid)
{
	struct intel_th_device *thdev = to_intel_th_device(dev);
	struct intel_th *th = to_intel_th(thdev);
	char *node;

	if (thdev->id >= 0)
		node = kasprintf(GFP_KERNEL, "intel_th%d/%s%d", th->id,
				 thdev->name, thdev->id);
	else
		node = kasprintf(GFP_KERNEL, "intel_th%d/%s", th->id,
				 thdev->name);

	return node;
}

static ssize_t port_show(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct intel_th_device *thdev = to_intel_th_device(dev);

	if (thdev->output.port >= 0)
		return scnprintf(buf, PAGE_SIZE, "%u\n", thdev->output.port);

	return scnprintf(buf, PAGE_SIZE, "unassigned\n");
}

static DEVICE_ATTR_RO(port);

static int intel_th_output_activate(struct intel_th_device *thdev)
{
	struct intel_th_driver *thdrv = to_intel_th_driver(thdev->dev.driver);

	if (thdrv->activate)
		return thdrv->activate(thdev);

	intel_th_trace_enable(thdev);

	return 0;
}

static void intel_th_output_deactivate(struct intel_th_device *thdev)
{
	struct intel_th_driver *thdrv = to_intel_th_driver(thdev->dev.driver);

	if (thdrv->deactivate)
		thdrv->deactivate(thdev);
	else
		intel_th_trace_disable(thdev);
}

static ssize_t active_show(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	struct intel_th_device *thdev = to_intel_th_device(dev);

	return scnprintf(buf, PAGE_SIZE, "%d\n", thdev->output.active);
}

static ssize_t active_store(struct device *dev, struct device_attribute *attr,
			    const char *buf, size_t size)
{
	struct intel_th_device *thdev = to_intel_th_device(dev);
	unsigned long val;
	int ret;

	ret = kstrtoul(buf, 10, &val);
	if (ret)
		return ret;

	if (!!val != thdev->output.active) {
		if (val)
			ret = intel_th_output_activate(thdev);
		else
			intel_th_output_deactivate(thdev);
	}

	return ret ? ret : size;
}

static DEVICE_ATTR_RW(active);

static struct attribute *intel_th_output_attrs[] = {
	&dev_attr_port.attr,
	&dev_attr_active.attr,
	NULL,
};

ATTRIBUTE_GROUPS(intel_th_output);

static struct device_type intel_th_output_device_type = {
	.name		= "intel_th_output_device",
	.groups		= intel_th_output_groups,
	.release	= intel_th_device_release,
	.devnode	= intel_th_output_devnode,
};

static struct device_type intel_th_switch_device_type = {
	.name		= "intel_th_switch_device",
	.release	= intel_th_device_release,
};

static struct device_type *intel_th_device_type[] = {
	[INTEL_TH_SOURCE]	= &intel_th_source_device_type,
	[INTEL_TH_OUTPUT]	= &intel_th_output_device_type,
	[INTEL_TH_SWITCH]	= &intel_th_switch_device_type,
};

int intel_th_driver_register(struct intel_th_driver *thdrv)
{
	if (!thdrv->probe || !thdrv->remove)
		return -EINVAL;

	thdrv->driver.bus = &intel_th_bus;

	return driver_register(&thdrv->driver);
}
EXPORT_SYMBOL_GPL(intel_th_driver_register);

void intel_th_driver_unregister(struct intel_th_driver *thdrv)
{
	driver_unregister(&thdrv->driver);
}
EXPORT_SYMBOL_GPL(intel_th_driver_unregister);

static struct intel_th_device *
intel_th_device_alloc(struct intel_th *th, unsigned int type, const char *name,
		      int id)
{
	struct device *parent;
	struct intel_th_device *thdev;

	if (type == INTEL_TH_SWITCH)
		parent = th->dev;
	else
		parent = &th->hub->dev;

	thdev = kzalloc(sizeof(*thdev) + strlen(name) + 1, GFP_KERNEL);
	if (!thdev)
		return NULL;

	thdev->id = id;
	thdev->type = type;

	strcpy(thdev->name, name);
	device_initialize(&thdev->dev);
	thdev->dev.bus = &intel_th_bus;
	thdev->dev.type = intel_th_device_type[type];
	thdev->dev.parent = parent;
	thdev->dev.dma_mask = parent->dma_mask;
	thdev->dev.dma_parms = parent->dma_parms;
	dma_set_coherent_mask(&thdev->dev, parent->coherent_dma_mask);
	if (id >= 0)
		dev_set_name(&thdev->dev, "%d-%s%d", th->id, name, id);
	else
		dev_set_name(&thdev->dev, "%d-%s", th->id, name);

	return thdev;
}

static int intel_th_device_add_resources(struct intel_th_device *thdev,
					 struct resource *res, int nres)
{
	struct resource *r;

	r = kmemdup(res, sizeof(*res) * nres, GFP_KERNEL);
	if (!r)
		return -ENOMEM;

	thdev->resource = r;
	thdev->num_resources = nres;

	return 0;
}

static void intel_th_device_remove(struct intel_th_device *thdev)
{
	device_del(&thdev->dev);
	put_device(&thdev->dev);
}

static void intel_th_device_free(struct intel_th_device *thdev)
{
	kfree(thdev->resource);
	kfree(thdev);
}

/*
 * Intel(R) Trace Hub subdevices
 */
static struct intel_th_subdevice {
	const char		*name;
	struct resource		res[3];
	unsigned		nres;
	unsigned		type;
	unsigned		otype;
	unsigned		scrpd;
	int			id;
} intel_th_subdevices[TH_SUBDEVICE_MAX] = {
	{
		.nres	= 1,
		.res	= {
			{
				.start	= REG_GTH_OFFSET,
				.end	= REG_GTH_OFFSET + REG_GTH_LENGTH - 1,
				.flags	= IORESOURCE_MEM,
			},
		},
		.name	= "gth",
		.type	= INTEL_TH_SWITCH,
		.id	= -1,
	},
	{
		.nres	= 2,
		.res	= {
			{
				.start	= REG_MSU_OFFSET,
				.end	= REG_MSU_OFFSET + REG_MSU_LENGTH - 1,
				.flags	= IORESOURCE_MEM,
			},
			{
				.start	= BUF_MSU_OFFSET,
				.end	= BUF_MSU_OFFSET + BUF_MSU_LENGTH - 1,
				.flags	= IORESOURCE_MEM,
			},
		},
		.name	= "msc",
		.id	= 0,
		.type	= INTEL_TH_OUTPUT,
		.otype	= GTH_MSU,
		.scrpd	= SCRPD_MEM_IS_PRIM_DEST | SCRPD_MSC0_IS_ENABLED,
	},
	{
		.nres	= 2,
		.res	= {
			{
				.start	= REG_MSU_OFFSET,
				.end	= REG_MSU_OFFSET + REG_MSU_LENGTH - 1,
				.flags	= IORESOURCE_MEM,
			},
			{
				.start	= BUF_MSU_OFFSET,
				.end	= BUF_MSU_OFFSET + BUF_MSU_LENGTH - 1,
				.flags	= IORESOURCE_MEM,
			},
		},
		.name	= "msc",
		.id	= 1,
		.type	= INTEL_TH_OUTPUT,
		.otype	= GTH_MSU,
		.scrpd	= SCRPD_MEM_IS_PRIM_DEST | SCRPD_MSC1_IS_ENABLED,
	},
	{
		.nres	= 2,
		.res	= {
			{
				.start	= REG_STH_OFFSET,
				.end	= REG_STH_OFFSET + REG_STH_LENGTH - 1,
				.flags	= IORESOURCE_MEM,
			},
			{
				.start	= TH_MMIO_SW,
				.end	= 0,
				.flags	= IORESOURCE_MEM,
			},
		},
		.id	= -1,
		.name	= "sth",
		.type	= INTEL_TH_SOURCE,
	},
	{
		.nres	= 1,
		.res	= {
			{
				.start	= REG_PTI_OFFSET,
				.end	= REG_PTI_OFFSET + REG_PTI_LENGTH - 1,
				.flags	= IORESOURCE_MEM,
			},
		},
		.id	= -1,
		.name	= "pti",
		.type	= INTEL_TH_OUTPUT,
		.otype	= GTH_PTI,
		.scrpd	= SCRPD_PTI_IS_PRIM_DEST,
	},
	{
		.nres	= 1,
		.res	= {
			{
				.start	= REG_DCIH_OFFSET,
				.end	= REG_DCIH_OFFSET + REG_DCIH_LENGTH - 1,
				.flags	= IORESOURCE_MEM,
			},
		},
		.id	= -1,
		.name	= "dcih",
		.type	= INTEL_TH_OUTPUT,
	},
};

static int intel_th_populate(struct intel_th *th, struct resource *devres,
			     unsigned int ndevres, int irq)
{
	struct resource res[3];
	unsigned int req = 0;
	int i, err;

	/* create devices for each intel_th_subdevice */
	for (i = 0; i < ARRAY_SIZE(intel_th_subdevices); i++) {
		struct intel_th_subdevice *subdev = &intel_th_subdevices[i];
		struct intel_th_device *thdev;
		int r;

		thdev = intel_th_device_alloc(th, subdev->type, subdev->name,
					      subdev->id);
		if (!thdev) {
			err = -ENOMEM;
			goto kill_subdevs;
		}

		memcpy(res, subdev->res,
		       sizeof(struct resource) * subdev->nres);

		for (r = 0; r < subdev->nres; r++) {
			int bar = TH_MMIO_CONFIG;

			/*
			 * Take .end == 0 to mean 'take the whole bar',
			 * .start then tells us which bar it is. Default to
			 * TH_MMIO_CONFIG.
			 */
			if (!res[r].end && res[r].flags == IORESOURCE_MEM) {
				bar = res[r].start;
				res[r].start = 0;
				res[r].end = resource_size(&devres[bar]) - 1;
			}

			if (res[r].flags & IORESOURCE_MEM) {
				res[r].start	+= devres[bar].start;
				res[r].end	+= devres[bar].start;

				dev_dbg(th->dev, "%s:%d @ %pR\n",
					subdev->name, r, &res[r]);
			} else if (res[r].flags & IORESOURCE_IRQ) {
				res[r].start	= irq;
			}
		}

		err = intel_th_device_add_resources(thdev, res, subdev->nres);
		if (err) {
			put_device(&thdev->dev);
			goto kill_subdevs;
		}

		if (subdev->type == INTEL_TH_OUTPUT) {
			thdev->dev.devt = MKDEV(th->major, i);
			thdev->output.type = subdev->otype;
			thdev->output.port = -1;
			thdev->output.scratchpad = subdev->scrpd;
		}

		err = device_add(&thdev->dev);
		if (err) {
			put_device(&thdev->dev);
			goto kill_subdevs;
		}

		/* need switch driver to be loaded to enumerate the rest */
		if (subdev->type == INTEL_TH_SWITCH && !req) {
			th->hub = thdev;
			err = request_module("intel_th_%s", subdev->name);
			if (!err)
				req++;
		}

		th->thdev[i] = thdev;
	}

	return 0;

kill_subdevs:
	for (i-- ; i >= 0; i--)
		intel_th_device_remove(th->thdev[i]);

	return err;
}

static int match_devt(struct device *dev, void *data)
{
	dev_t devt = (dev_t)(unsigned long)data;

	return dev->devt == devt;
}

static int intel_th_output_open(struct inode *inode, struct file *file)
{
	const struct file_operations *fops;
	struct intel_th_driver *thdrv;
	struct device *dev;
	int err;

	dev = bus_find_device(&intel_th_bus, NULL,
			      (void *)(unsigned long)inode->i_rdev,
			      match_devt);
	if (!dev || !dev->driver)
		return -ENODEV;

	thdrv = to_intel_th_driver(dev->driver);
	fops = fops_get(thdrv->fops);
	if (!fops)
		return -ENODEV;

	replace_fops(file, fops);

	file->private_data = to_intel_th_device(dev);

	if (file->f_op->open) {
		err = file->f_op->open(inode, file);
		return err;
	}

	return 0;
}

static const struct file_operations intel_th_output_fops = {
	.open	= intel_th_output_open,
	.llseek	= noop_llseek,
};

/**
 * intel_th_alloc() - allocate a new Intel TH device and its subdevices
 * @dev:	parent device
 * @devres:	parent's resources
 * @ndevres:	number of resources
 * @irq:	irq number
 */
struct intel_th *
intel_th_alloc(struct device *dev, struct resource *devres,
	       unsigned int ndevres, int irq)
{
	struct intel_th *th;
	int err;

	th = kzalloc(sizeof(*th), GFP_KERNEL);
	if (!th)
		return ERR_PTR(-ENOMEM);

	th->id = ida_simple_get(&intel_th_ida, 0, 0, GFP_KERNEL);
	if (th->id < 0) {
		err = th->id;
		goto err_alloc;
	}

	th->major = __register_chrdev(0, 0, TH_POSSIBLE_OUTPUTS,
				      "intel_th/output", &intel_th_output_fops);
	if (th->major < 0) {
		err = th->major;
		goto err_ida;
	}
	th->dev = dev;

	dev_set_drvdata(dev, th);

	err = intel_th_populate(th, devres, ndevres, irq);
	if (err)
		goto err_chrdev;

	return th;

err_chrdev:
	__unregister_chrdev(th->major, 0, TH_POSSIBLE_OUTPUTS,
			    "intel_th/output");

err_ida:
	ida_simple_remove(&intel_th_ida, th->id);

err_alloc:
	kfree(th);

	return ERR_PTR(err);
}
EXPORT_SYMBOL_GPL(intel_th_alloc);

void intel_th_free(struct intel_th *th)
{
	int i;

	for (i = 0; i < TH_SUBDEVICE_MAX; i++)
		if (th->thdev[i] != th->hub)
			intel_th_device_remove(th->thdev[i]);

	intel_th_device_remove(th->hub);

	__unregister_chrdev(th->major, 0, TH_POSSIBLE_OUTPUTS,
			    "intel_th/output");

	ida_simple_remove(&intel_th_ida, th->id);

	kfree(th);
}
EXPORT_SYMBOL_GPL(intel_th_free);

/**
 * intel_th_trace_enable() - enable tracing for an output device
 * @thdev:	output device that requests tracing be enabled
 */
int intel_th_trace_enable(struct intel_th_device *thdev)
{
	struct intel_th_device *hub = to_intel_th_device(thdev->dev.parent);
	struct intel_th_driver *hubdrv = to_intel_th_driver(hub->dev.driver);

	if (WARN_ON_ONCE(hub->type != INTEL_TH_SWITCH))
		return -EINVAL;

	if (WARN_ON_ONCE(thdev->type != INTEL_TH_OUTPUT))
		return -EINVAL;

	hubdrv->enable(hub, &thdev->output);

	return 0;
}
EXPORT_SYMBOL_GPL(intel_th_trace_enable);

/**
 * intel_th_trace_disable() - disable tracing for an output device
 * @thdev:	output device that requests tracing be disabled
 */
int intel_th_trace_disable(struct intel_th_device *thdev)
{
	struct intel_th_device *hub = to_intel_th_device(thdev->dev.parent);
	struct intel_th_driver *hubdrv = to_intel_th_driver(hub->dev.driver);

	WARN_ON_ONCE(hub->type != INTEL_TH_SWITCH);
	if (WARN_ON_ONCE(thdev->type != INTEL_TH_OUTPUT))
		return -EINVAL;

	hubdrv->disable(hub, &thdev->output);

	return 0;
}
EXPORT_SYMBOL_GPL(intel_th_trace_disable);

int intel_th_set_output(struct intel_th_device *thdev,
			unsigned int master)
{
	struct intel_th_device *hub = to_intel_th_device(thdev->dev.parent);
	struct intel_th_driver *hubdrv = to_intel_th_driver(hub->dev.driver);

	if (!hubdrv->set_output)
		return -ENOTSUPP;

	return hubdrv->set_output(hub, master);
}
EXPORT_SYMBOL_GPL(intel_th_set_output);

static int __init intel_th_init(void)
{
	intel_th_debug_init();

	return bus_register(&intel_th_bus);
}
subsys_initcall(intel_th_init);

static void __exit intel_th_exit(void)
{
	intel_th_debug_done();

	bus_unregister(&intel_th_bus);
}
module_exit(intel_th_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Intel(R) Trace Hub controller driver");
MODULE_AUTHOR("Alexander Shishkin <alexander.shishkin@linux.intel.com>");

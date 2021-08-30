// SPDX-License-Identifier: GPL-2.0
/*
 * Intel(R) Trace Hub driver core
 *
 * Copyright (C) 2014-2015 Intel Corporation.
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
#include <linux/pm_runtime.h>
#include <linux/dma-mapping.h>

#include "intel_th.h"
#include "debug.h"

static bool host_mode __read_mostly;
module_param(host_mode, bool, 0444);

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

	pm_runtime_set_active(dev);
	pm_runtime_no_callbacks(dev);
	pm_runtime_enable(dev);

	ret = thdrv->probe(to_intel_th_device(dev));
	if (ret)
		goto out_pm;

	if (thdrv->attr_group) {
		ret = sysfs_create_group(&thdev->dev.kobj, thdrv->attr_group);
		if (ret)
			goto out;
	}

	if (thdev->type == INTEL_TH_OUTPUT &&
	    !intel_th_output_assigned(thdev))
		/* does not talk to hardware */
		ret = hubdrv->assign(hub, thdev);

out:
	if (ret)
		thdrv->remove(thdev);

out_pm:
	if (ret)
		pm_runtime_disable(dev);

	return ret;
}

static void intel_th_device_remove(struct intel_th_device *thdev);

static int intel_th_remove(struct device *dev)
{
	struct intel_th_driver *thdrv = to_intel_th_driver(dev->driver);
	struct intel_th_device *thdev = to_intel_th_device(dev);
	struct intel_th_device *hub = to_intel_th_hub(thdev);

	if (thdev->type == INTEL_TH_SWITCH) {
		struct intel_th *th = to_intel_th(hub);
		int i, lowest;

		/*
		 * disconnect outputs
		 *
		 * intel_th_child_remove returns 0 unconditionally, so there is
		 * no need to check the return value of device_for_each_child.
		 */
		device_for_each_child(dev, thdev, intel_th_child_remove);

		/*
		 * Remove outputs, that is, hub's children: they are created
		 * at hub's probe time by having the hub call
		 * intel_th_output_enable() for each of them.
		 */
		for (i = 0, lowest = -1; i < th->num_thdevs; i++) {
			/*
			 * Move the non-output devices from higher up the
			 * th->thdev[] array to lower positions to maintain
			 * a contiguous array.
			 */
			if (th->thdev[i]->type != INTEL_TH_OUTPUT) {
				if (lowest >= 0) {
					th->thdev[lowest] = th->thdev[i];
					th->thdev[i] = NULL;
					++lowest;
				}

				continue;
			}

			if (lowest == -1)
				lowest = i;

			intel_th_device_remove(th->thdev[i]);
			th->thdev[i] = NULL;
		}

		if (lowest >= 0)
			th->num_thdevs = lowest;
	}

	if (thdrv->attr_group)
		sysfs_remove_group(&thdev->dev.kobj, thdrv->attr_group);

	pm_runtime_get_sync(dev);

	thdrv->remove(thdev);

	if (intel_th_output_assigned(thdev)) {
		struct intel_th_driver *hubdrv =
			to_intel_th_driver(dev->parent->driver);

		if (hub->dev.driver)
			/* does not talk to hardware */
			hubdrv->unassign(hub, thdev);
	}

	pm_runtime_disable(dev);
	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);

	return 0;
}

static struct bus_type intel_th_bus = {
	.name		= "intel_th",
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

static void intel_th_trace_prepare(struct intel_th_device *thdev)
{
	struct intel_th_device *hub = to_intel_th_hub(thdev);
	struct intel_th_driver *hubdrv = to_intel_th_driver(hub->dev.driver);

	if (hub->type != INTEL_TH_SWITCH)
		return;

	if (thdev->type != INTEL_TH_OUTPUT)
		return;

	pm_runtime_get_sync(&thdev->dev);
	hubdrv->prepare(hub, &thdev->output);
	pm_runtime_put(&thdev->dev);
}

static int intel_th_output_activate(struct intel_th_device *thdev)
{
	struct intel_th_driver *thdrv =
		to_intel_th_driver_or_null(thdev->dev.driver);
	struct intel_th *th = to_intel_th(thdev);
	int ret = 0;

	if (!thdrv)
		return -ENODEV;

	if (!try_module_get(thdrv->driver.owner))
		return -ENODEV;

	pm_runtime_get_sync(&thdev->dev);

	if (th->activate)
		ret = th->activate(th);
	if (ret)
		goto fail_put;

	intel_th_trace_prepare(thdev);
	if (thdrv->activate)
		ret = thdrv->activate(thdev);
	else
		intel_th_trace_enable(thdev);

	if (ret)
		goto fail_deactivate;

	return 0;

fail_deactivate:
	if (th->deactivate)
		th->deactivate(th);

fail_put:
	pm_runtime_put(&thdev->dev);
	module_put(thdrv->driver.owner);

	return ret;
}

static void intel_th_output_deactivate(struct intel_th_device *thdev)
{
	struct intel_th_driver *thdrv =
		to_intel_th_driver_or_null(thdev->dev.driver);
	struct intel_th *th = to_intel_th(thdev);

	if (!thdrv)
		return;

	if (thdrv->deactivate)
		thdrv->deactivate(thdev);
	else
		intel_th_trace_disable(thdev);

	if (th->deactivate)
		th->deactivate(th);

	pm_runtime_put(&thdev->dev);
	module_put(thdrv->driver.owner);
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

	if (type == INTEL_TH_OUTPUT)
		parent = &th->hub->dev;
	else
		parent = th->dev;

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
static const struct intel_th_subdevice {
	const char		*name;
	struct resource		res[3];
	unsigned		nres;
	unsigned		type;
	unsigned		otype;
	bool			mknode;
	unsigned		scrpd;
	int			id;
} intel_th_subdevices[] = {
	{
		.nres	= 1,
		.res	= {
			{
				/* Handle TSCU and CTS from GTH driver */
				.start	= REG_GTH_OFFSET,
				.end	= REG_CTS_OFFSET + REG_CTS_LENGTH - 1,
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
		.mknode	= true,
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
		.mknode	= true,
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
		.nres	= 2,
		.res	= {
			{
				.start	= REG_STH_OFFSET,
				.end	= REG_STH_OFFSET + REG_STH_LENGTH - 1,
				.flags	= IORESOURCE_MEM,
			},
			{
				.start	= TH_MMIO_RTIT,
				.end	= 0,
				.flags	= IORESOURCE_MEM,
			},
		},
		.id	= -1,
		.name	= "rtit",
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
				.start	= REG_PTI_OFFSET,
				.end	= REG_PTI_OFFSET + REG_PTI_LENGTH - 1,
				.flags	= IORESOURCE_MEM,
			},
		},
		.id	= -1,
		.name	= "lpp",
		.type	= INTEL_TH_OUTPUT,
		.otype	= GTH_LPP,
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

#ifdef CONFIG_MODULES
static void __intel_th_request_hub_module(struct work_struct *work)
{
	struct intel_th *th = container_of(work, struct intel_th,
					   request_module_work);

	request_module("intel_th_%s", th->hub->name);
}

static int intel_th_request_hub_module(struct intel_th *th)
{
	INIT_WORK(&th->request_module_work, __intel_th_request_hub_module);
	schedule_work(&th->request_module_work);

	return 0;
}

static void intel_th_request_hub_module_flush(struct intel_th *th)
{
	flush_work(&th->request_module_work);
}
#else
static inline int intel_th_request_hub_module(struct intel_th *th)
{
	return -EINVAL;
}

static inline void intel_th_request_hub_module_flush(struct intel_th *th)
{
}
#endif /* CONFIG_MODULES */

static struct intel_th_device *
intel_th_subdevice_alloc(struct intel_th *th,
			 const struct intel_th_subdevice *subdev)
{
	struct intel_th_device *thdev;
	struct resource res[3];
	unsigned int req = 0;
	int r, err;

	thdev = intel_th_device_alloc(th, subdev->type, subdev->name,
				      subdev->id);
	if (!thdev)
		return ERR_PTR(-ENOMEM);

	thdev->drvdata = th->drvdata;

	memcpy(res, subdev->res,
	       sizeof(struct resource) * subdev->nres);

	for (r = 0; r < subdev->nres; r++) {
		struct resource *devres = th->resource;
		int bar = TH_MMIO_CONFIG;

		/*
		 * Take .end == 0 to mean 'take the whole bar',
		 * .start then tells us which bar it is. Default to
		 * TH_MMIO_CONFIG.
		 */
		if (!res[r].end && res[r].flags == IORESOURCE_MEM) {
			bar = res[r].start;
			err = -ENODEV;
			if (bar >= th->num_resources)
				goto fail_put_device;
			res[r].start = 0;
			res[r].end = resource_size(&devres[bar]) - 1;
		}

		if (res[r].flags & IORESOURCE_MEM) {
			res[r].start	+= devres[bar].start;
			res[r].end	+= devres[bar].start;

			dev_dbg(th->dev, "%s:%d @ %pR\n",
				subdev->name, r, &res[r]);
		} else if (res[r].flags & IORESOURCE_IRQ) {
			/*
			 * Only pass on the IRQ if we have useful interrupts:
			 * the ones that can be configured via MINTCTL.
			 */
			if (INTEL_TH_CAP(th, has_mintctl) && th->irq != -1)
				res[r].start = th->irq;
		}
	}

	err = intel_th_device_add_resources(thdev, res, subdev->nres);
	if (err)
		goto fail_put_device;

	if (subdev->type == INTEL_TH_OUTPUT) {
		if (subdev->mknode)
			thdev->dev.devt = MKDEV(th->major, th->num_thdevs);
		thdev->output.type = subdev->otype;
		thdev->output.port = -1;
		thdev->output.scratchpad = subdev->scrpd;
	} else if (subdev->type == INTEL_TH_SWITCH) {
		thdev->host_mode =
			INTEL_TH_CAP(th, host_mode_only) ? true : host_mode;
		th->hub = thdev;
	}

	err = device_add(&thdev->dev);
	if (err)
		goto fail_free_res;

	/* need switch driver to be loaded to enumerate the rest */
	if (subdev->type == INTEL_TH_SWITCH && !req) {
		err = intel_th_request_hub_module(th);
		if (!err)
			req++;
	}

	return thdev;

fail_free_res:
	kfree(thdev->resource);

fail_put_device:
	put_device(&thdev->dev);

	return ERR_PTR(err);
}

/**
 * intel_th_output_enable() - find and enable a device for a given output type
 * @th:		Intel TH instance
 * @otype:	output type
 *
 * Go through the unallocated output devices, find the first one whos type
 * matches @otype and instantiate it. These devices are removed when the hub
 * device is removed, see intel_th_remove().
 */
int intel_th_output_enable(struct intel_th *th, unsigned int otype)
{
	struct intel_th_device *thdev;
	int src = 0, dst = 0;

	for (src = 0, dst = 0; dst <= th->num_thdevs; src++, dst++) {
		for (; src < ARRAY_SIZE(intel_th_subdevices); src++) {
			if (intel_th_subdevices[src].type != INTEL_TH_OUTPUT)
				continue;

			if (intel_th_subdevices[src].otype != otype)
				continue;

			break;
		}

		/* no unallocated matching subdevices */
		if (src == ARRAY_SIZE(intel_th_subdevices))
			return -ENODEV;

		for (; dst < th->num_thdevs; dst++) {
			if (th->thdev[dst]->type != INTEL_TH_OUTPUT)
				continue;

			if (th->thdev[dst]->output.type != otype)
				continue;

			break;
		}

		/*
		 * intel_th_subdevices[src] matches our requirements and is
		 * not matched in th::thdev[]
		 */
		if (dst == th->num_thdevs)
			goto found;
	}

	return -ENODEV;

found:
	thdev = intel_th_subdevice_alloc(th, &intel_th_subdevices[src]);
	if (IS_ERR(thdev))
		return PTR_ERR(thdev);

	th->thdev[th->num_thdevs++] = thdev;

	return 0;
}
EXPORT_SYMBOL_GPL(intel_th_output_enable);

static int intel_th_populate(struct intel_th *th)
{
	int src;

	/* create devices for each intel_th_subdevice */
	for (src = 0; src < ARRAY_SIZE(intel_th_subdevices); src++) {
		const struct intel_th_subdevice *subdev =
			&intel_th_subdevices[src];
		struct intel_th_device *thdev;

		/* only allow SOURCE and SWITCH devices in host mode */
		if ((INTEL_TH_CAP(th, host_mode_only) || host_mode) &&
		    subdev->type == INTEL_TH_OUTPUT)
			continue;

		/*
		 * don't enable port OUTPUTs in this path; SWITCH enables them
		 * via intel_th_output_enable()
		 */
		if (subdev->type == INTEL_TH_OUTPUT &&
		    subdev->otype != GTH_NONE)
			continue;

		thdev = intel_th_subdevice_alloc(th, subdev);
		/* note: caller should free subdevices from th::thdev[] */
		if (IS_ERR(thdev)) {
			/* ENODEV for individual subdevices is allowed */
			if (PTR_ERR(thdev) == -ENODEV)
				continue;

			return PTR_ERR(thdev);
		}

		th->thdev[th->num_thdevs++] = thdev;
	}

	return 0;
}

static int intel_th_output_open(struct inode *inode, struct file *file)
{
	const struct file_operations *fops;
	struct intel_th_driver *thdrv;
	struct device *dev;
	int err;

	dev = bus_find_device_by_devt(&intel_th_bus, inode->i_rdev);
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

static irqreturn_t intel_th_irq(int irq, void *data)
{
	struct intel_th *th = data;
	irqreturn_t ret = IRQ_NONE;
	struct intel_th_driver *d;
	int i;

	for (i = 0; i < th->num_thdevs; i++) {
		if (th->thdev[i]->type != INTEL_TH_OUTPUT)
			continue;

		d = to_intel_th_driver(th->thdev[i]->dev.driver);
		if (d && d->irq)
			ret |= d->irq(th->thdev[i]);
	}

	return ret;
}

/**
 * intel_th_alloc() - allocate a new Intel TH device and its subdevices
 * @dev:	parent device
 * @devres:	resources indexed by th_mmio_idx
 * @irq:	irq number
 */
struct intel_th *
intel_th_alloc(struct device *dev, const struct intel_th_drvdata *drvdata,
	       struct resource *devres, unsigned int ndevres)
{
	int err, r, nr_mmios = 0;
	struct intel_th *th;

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
	th->irq = -1;
	th->dev = dev;
	th->drvdata = drvdata;

	for (r = 0; r < ndevres; r++)
		switch (devres[r].flags & IORESOURCE_TYPE_BITS) {
		case IORESOURCE_MEM:
			th->resource[nr_mmios++] = devres[r];
			break;
		case IORESOURCE_IRQ:
			err = devm_request_irq(dev, devres[r].start,
					       intel_th_irq, IRQF_SHARED,
					       dev_name(dev), th);
			if (err)
				goto err_chrdev;

			if (th->irq == -1)
				th->irq = devres[r].start;
			th->num_irqs++;
			break;
		default:
			dev_warn(dev, "Unknown resource type %lx\n",
				 devres[r].flags);
			break;
		}

	th->num_resources = nr_mmios;

	dev_set_drvdata(dev, th);

	pm_runtime_no_callbacks(dev);
	pm_runtime_put(dev);
	pm_runtime_allow(dev);

	err = intel_th_populate(th);
	if (err) {
		/* free the subdevices and undo everything */
		intel_th_free(th);
		return ERR_PTR(err);
	}

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

	intel_th_request_hub_module_flush(th);

	intel_th_device_remove(th->hub);
	for (i = 0; i < th->num_thdevs; i++) {
		if (th->thdev[i] != th->hub)
			intel_th_device_remove(th->thdev[i]);
		th->thdev[i] = NULL;
	}

	th->num_thdevs = 0;

	for (i = 0; i < th->num_irqs; i++)
		devm_free_irq(th->dev, th->irq + i, th);

	pm_runtime_get_sync(th->dev);
	pm_runtime_forbid(th->dev);

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

	pm_runtime_get_sync(&thdev->dev);
	hubdrv->enable(hub, &thdev->output);

	return 0;
}
EXPORT_SYMBOL_GPL(intel_th_trace_enable);

/**
 * intel_th_trace_switch() - execute a switch sequence
 * @thdev:	output device that requests tracing switch
 */
int intel_th_trace_switch(struct intel_th_device *thdev)
{
	struct intel_th_device *hub = to_intel_th_device(thdev->dev.parent);
	struct intel_th_driver *hubdrv = to_intel_th_driver(hub->dev.driver);

	if (WARN_ON_ONCE(hub->type != INTEL_TH_SWITCH))
		return -EINVAL;

	if (WARN_ON_ONCE(thdev->type != INTEL_TH_OUTPUT))
		return -EINVAL;

	hubdrv->trig_switch(hub, &thdev->output);

	return 0;
}
EXPORT_SYMBOL_GPL(intel_th_trace_switch);

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
	pm_runtime_put(&thdev->dev);

	return 0;
}
EXPORT_SYMBOL_GPL(intel_th_trace_disable);

int intel_th_set_output(struct intel_th_device *thdev,
			unsigned int master)
{
	struct intel_th_device *hub = to_intel_th_hub(thdev);
	struct intel_th_driver *hubdrv = to_intel_th_driver(hub->dev.driver);
	int ret;

	/* In host mode, this is up to the external debugger, do nothing. */
	if (hub->host_mode)
		return 0;

	/*
	 * hub is instantiated together with the source device that
	 * calls here, so guaranteed to be present.
	 */
	hubdrv = to_intel_th_driver(hub->dev.driver);
	if (!hubdrv || !try_module_get(hubdrv->driver.owner))
		return -EINVAL;

	if (!hubdrv->set_output) {
		ret = -ENOTSUPP;
		goto out;
	}

	ret = hubdrv->set_output(hub, master);

out:
	module_put(hubdrv->driver.owner);
	return ret;
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

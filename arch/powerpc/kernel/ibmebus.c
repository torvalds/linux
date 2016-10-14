/*
 * IBM PowerPC IBM eBus Infrastructure Support.
 *
 * Copyright (c) 2005 IBM Corporation
 *  Joachim Fenkes <fenkes@de.ibm.com>
 *  Heiko J Schick <schickhj@de.ibm.com>
 *
 * All rights reserved.
 *
 * This source code is distributed under a dual license of GPL v2.0 and OpenIB
 * BSD.
 *
 * OpenIB BSD License
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 *
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials
 * provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <linux/init.h>
#include <linux/export.h>
#include <linux/console.h>
#include <linux/kobject.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/stat.h>
#include <linux/of_platform.h>
#include <asm/ibmebus.h>

static struct device ibmebus_bus_device = { /* fake "parent" device */
	.init_name = "ibmebus",
};

struct bus_type ibmebus_bus_type;

/* These devices will automatically be added to the bus during init */
static const struct of_device_id ibmebus_matches[] __initconst = {
	{ .compatible = "IBM,lhca" },
	{ .compatible = "IBM,lhea" },
	{},
};

static void *ibmebus_alloc_coherent(struct device *dev,
				    size_t size,
				    dma_addr_t *dma_handle,
				    gfp_t flag,
				    unsigned long attrs)
{
	void *mem;

	mem = kmalloc(size, flag);
	*dma_handle = (dma_addr_t)mem;

	return mem;
}

static void ibmebus_free_coherent(struct device *dev,
				  size_t size, void *vaddr,
				  dma_addr_t dma_handle,
				  unsigned long attrs)
{
	kfree(vaddr);
}

static dma_addr_t ibmebus_map_page(struct device *dev,
				   struct page *page,
				   unsigned long offset,
				   size_t size,
				   enum dma_data_direction direction,
				   unsigned long attrs)
{
	return (dma_addr_t)(page_address(page) + offset);
}

static void ibmebus_unmap_page(struct device *dev,
			       dma_addr_t dma_addr,
			       size_t size,
			       enum dma_data_direction direction,
			       unsigned long attrs)
{
	return;
}

static int ibmebus_map_sg(struct device *dev,
			  struct scatterlist *sgl,
			  int nents, enum dma_data_direction direction,
			  unsigned long attrs)
{
	struct scatterlist *sg;
	int i;

	for_each_sg(sgl, sg, nents, i) {
		sg->dma_address = (dma_addr_t) sg_virt(sg);
		sg->dma_length = sg->length;
	}

	return nents;
}

static void ibmebus_unmap_sg(struct device *dev,
			     struct scatterlist *sg,
			     int nents, enum dma_data_direction direction,
			     unsigned long attrs)
{
	return;
}

static int ibmebus_dma_supported(struct device *dev, u64 mask)
{
	return mask == DMA_BIT_MASK(64);
}

static u64 ibmebus_dma_get_required_mask(struct device *dev)
{
	return DMA_BIT_MASK(64);
}

static struct dma_map_ops ibmebus_dma_ops = {
	.alloc              = ibmebus_alloc_coherent,
	.free               = ibmebus_free_coherent,
	.map_sg             = ibmebus_map_sg,
	.unmap_sg           = ibmebus_unmap_sg,
	.dma_supported      = ibmebus_dma_supported,
	.get_required_mask  = ibmebus_dma_get_required_mask,
	.map_page           = ibmebus_map_page,
	.unmap_page         = ibmebus_unmap_page,
};

static int ibmebus_match_path(struct device *dev, void *data)
{
	struct device_node *dn = to_platform_device(dev)->dev.of_node;
	return (dn->full_name &&
		(strcasecmp((char *)data, dn->full_name) == 0));
}

static int ibmebus_match_node(struct device *dev, void *data)
{
	return to_platform_device(dev)->dev.of_node == data;
}

static int ibmebus_create_device(struct device_node *dn)
{
	struct platform_device *dev;
	int ret;

	dev = of_device_alloc(dn, NULL, &ibmebus_bus_device);
	if (!dev)
		return -ENOMEM;

	dev->dev.bus = &ibmebus_bus_type;
	dev->dev.archdata.dma_ops = &ibmebus_dma_ops;

	ret = of_device_add(dev);
	if (ret)
		platform_device_put(dev);
	return ret;
}

static int ibmebus_create_devices(const struct of_device_id *matches)
{
	struct device_node *root, *child;
	int ret = 0;

	root = of_find_node_by_path("/");

	for_each_child_of_node(root, child) {
		if (!of_match_node(matches, child))
			continue;

		if (bus_find_device(&ibmebus_bus_type, NULL, child,
				    ibmebus_match_node))
			continue;

		ret = ibmebus_create_device(child);
		if (ret) {
			printk(KERN_ERR "%s: failed to create device (%i)",
			       __func__, ret);
			of_node_put(child);
			break;
		}
	}

	of_node_put(root);
	return ret;
}

int ibmebus_register_driver(struct platform_driver *drv)
{
	/* If the driver uses devices that ibmebus doesn't know, add them */
	ibmebus_create_devices(drv->driver.of_match_table);

	drv->driver.bus = &ibmebus_bus_type;
	return driver_register(&drv->driver);
}
EXPORT_SYMBOL(ibmebus_register_driver);

void ibmebus_unregister_driver(struct platform_driver *drv)
{
	driver_unregister(&drv->driver);
}
EXPORT_SYMBOL(ibmebus_unregister_driver);

int ibmebus_request_irq(u32 ist, irq_handler_t handler,
			unsigned long irq_flags, const char *devname,
			void *dev_id)
{
	unsigned int irq = irq_create_mapping(NULL, ist);

	if (irq == NO_IRQ)
		return -EINVAL;

	return request_irq(irq, handler, irq_flags, devname, dev_id);
}
EXPORT_SYMBOL(ibmebus_request_irq);

void ibmebus_free_irq(u32 ist, void *dev_id)
{
	unsigned int irq = irq_find_mapping(NULL, ist);

	free_irq(irq, dev_id);
	irq_dispose_mapping(irq);
}
EXPORT_SYMBOL(ibmebus_free_irq);

static char *ibmebus_chomp(const char *in, size_t count)
{
	char *out = kmalloc(count + 1, GFP_KERNEL);

	if (!out)
		return NULL;

	memcpy(out, in, count);
	out[count] = '\0';
	if (out[count - 1] == '\n')
		out[count - 1] = '\0';

	return out;
}

static ssize_t ibmebus_store_probe(struct bus_type *bus,
				   const char *buf, size_t count)
{
	struct device_node *dn = NULL;
	char *path;
	ssize_t rc = 0;

	path = ibmebus_chomp(buf, count);
	if (!path)
		return -ENOMEM;

	if (bus_find_device(&ibmebus_bus_type, NULL, path,
			    ibmebus_match_path)) {
		printk(KERN_WARNING "%s: %s has already been probed\n",
		       __func__, path);
		rc = -EEXIST;
		goto out;
	}

	if ((dn = of_find_node_by_path(path))) {
		rc = ibmebus_create_device(dn);
		of_node_put(dn);
	} else {
		printk(KERN_WARNING "%s: no such device node: %s\n",
		       __func__, path);
		rc = -ENODEV;
	}

out:
	kfree(path);
	if (rc)
		return rc;
	return count;
}
static BUS_ATTR(probe, S_IWUSR, NULL, ibmebus_store_probe);

static ssize_t ibmebus_store_remove(struct bus_type *bus,
				    const char *buf, size_t count)
{
	struct device *dev;
	char *path;

	path = ibmebus_chomp(buf, count);
	if (!path)
		return -ENOMEM;

	if ((dev = bus_find_device(&ibmebus_bus_type, NULL, path,
				   ibmebus_match_path))) {
		of_device_unregister(to_platform_device(dev));

		kfree(path);
		return count;
	} else {
		printk(KERN_WARNING "%s: %s not on the bus\n",
		       __func__, path);

		kfree(path);
		return -ENODEV;
	}
}
static BUS_ATTR(remove, S_IWUSR, NULL, ibmebus_store_remove);

static struct attribute *ibmbus_bus_attrs[] = {
	&bus_attr_probe.attr,
	&bus_attr_remove.attr,
	NULL,
};
ATTRIBUTE_GROUPS(ibmbus_bus);

static int ibmebus_bus_bus_match(struct device *dev, struct device_driver *drv)
{
	const struct of_device_id *matches = drv->of_match_table;

	if (!matches)
		return 0;

	return of_match_device(matches, dev) != NULL;
}

static int ibmebus_bus_device_probe(struct device *dev)
{
	int error = -ENODEV;
	struct platform_driver *drv;
	struct platform_device *of_dev;

	drv = to_platform_driver(dev->driver);
	of_dev = to_platform_device(dev);

	if (!drv->probe)
		return error;

	of_dev_get(of_dev);

	if (of_driver_match_device(dev, dev->driver))
		error = drv->probe(of_dev);
	if (error)
		of_dev_put(of_dev);

	return error;
}

static int ibmebus_bus_device_remove(struct device *dev)
{
	struct platform_device *of_dev = to_platform_device(dev);
	struct platform_driver *drv = to_platform_driver(dev->driver);

	if (dev->driver && drv->remove)
		drv->remove(of_dev);
	return 0;
}

static void ibmebus_bus_device_shutdown(struct device *dev)
{
	struct platform_device *of_dev = to_platform_device(dev);
	struct platform_driver *drv = to_platform_driver(dev->driver);

	if (dev->driver && drv->shutdown)
		drv->shutdown(of_dev);
}

/*
 * ibmebus_bus_device_attrs
 */
static ssize_t devspec_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct platform_device *ofdev;

	ofdev = to_platform_device(dev);
	return sprintf(buf, "%s\n", ofdev->dev.of_node->full_name);
}

static ssize_t name_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct platform_device *ofdev;

	ofdev = to_platform_device(dev);
	return sprintf(buf, "%s\n", ofdev->dev.of_node->name);
}

static ssize_t modalias_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	ssize_t len = of_device_get_modalias(dev, buf, PAGE_SIZE - 2);
	buf[len] = '\n';
	buf[len+1] = 0;
	return len+1;
}

static struct device_attribute ibmebus_bus_device_attrs[] = {
	__ATTR_RO(devspec),
	__ATTR_RO(name),
	__ATTR_RO(modalias),
	__ATTR_NULL
};

#ifdef CONFIG_PM_SLEEP
static int ibmebus_bus_legacy_suspend(struct device *dev, pm_message_t mesg)
{
	struct platform_device *of_dev = to_platform_device(dev);
	struct platform_driver *drv = to_platform_driver(dev->driver);
	int ret = 0;

	if (dev->driver && drv->suspend)
		ret = drv->suspend(of_dev, mesg);
	return ret;
}

static int ibmebus_bus_legacy_resume(struct device *dev)
{
	struct platform_device *of_dev = to_platform_device(dev);
	struct platform_driver *drv = to_platform_driver(dev->driver);
	int ret = 0;

	if (dev->driver && drv->resume)
		ret = drv->resume(of_dev);
	return ret;
}

static int ibmebus_bus_pm_prepare(struct device *dev)
{
	struct device_driver *drv = dev->driver;
	int ret = 0;

	if (drv && drv->pm && drv->pm->prepare)
		ret = drv->pm->prepare(dev);

	return ret;
}

static void ibmebus_bus_pm_complete(struct device *dev)
{
	struct device_driver *drv = dev->driver;

	if (drv && drv->pm && drv->pm->complete)
		drv->pm->complete(dev);
}

#ifdef CONFIG_SUSPEND

static int ibmebus_bus_pm_suspend(struct device *dev)
{
	struct device_driver *drv = dev->driver;
	int ret = 0;

	if (!drv)
		return 0;

	if (drv->pm) {
		if (drv->pm->suspend)
			ret = drv->pm->suspend(dev);
	} else {
		ret = ibmebus_bus_legacy_suspend(dev, PMSG_SUSPEND);
	}

	return ret;
}

static int ibmebus_bus_pm_suspend_noirq(struct device *dev)
{
	struct device_driver *drv = dev->driver;
	int ret = 0;

	if (!drv)
		return 0;

	if (drv->pm) {
		if (drv->pm->suspend_noirq)
			ret = drv->pm->suspend_noirq(dev);
	}

	return ret;
}

static int ibmebus_bus_pm_resume(struct device *dev)
{
	struct device_driver *drv = dev->driver;
	int ret = 0;

	if (!drv)
		return 0;

	if (drv->pm) {
		if (drv->pm->resume)
			ret = drv->pm->resume(dev);
	} else {
		ret = ibmebus_bus_legacy_resume(dev);
	}

	return ret;
}

static int ibmebus_bus_pm_resume_noirq(struct device *dev)
{
	struct device_driver *drv = dev->driver;
	int ret = 0;

	if (!drv)
		return 0;

	if (drv->pm) {
		if (drv->pm->resume_noirq)
			ret = drv->pm->resume_noirq(dev);
	}

	return ret;
}

#else /* !CONFIG_SUSPEND */

#define ibmebus_bus_pm_suspend		NULL
#define ibmebus_bus_pm_resume		NULL
#define ibmebus_bus_pm_suspend_noirq	NULL
#define ibmebus_bus_pm_resume_noirq	NULL

#endif /* !CONFIG_SUSPEND */

#ifdef CONFIG_HIBERNATE_CALLBACKS

static int ibmebus_bus_pm_freeze(struct device *dev)
{
	struct device_driver *drv = dev->driver;
	int ret = 0;

	if (!drv)
		return 0;

	if (drv->pm) {
		if (drv->pm->freeze)
			ret = drv->pm->freeze(dev);
	} else {
		ret = ibmebus_bus_legacy_suspend(dev, PMSG_FREEZE);
	}

	return ret;
}

static int ibmebus_bus_pm_freeze_noirq(struct device *dev)
{
	struct device_driver *drv = dev->driver;
	int ret = 0;

	if (!drv)
		return 0;

	if (drv->pm) {
		if (drv->pm->freeze_noirq)
			ret = drv->pm->freeze_noirq(dev);
	}

	return ret;
}

static int ibmebus_bus_pm_thaw(struct device *dev)
{
	struct device_driver *drv = dev->driver;
	int ret = 0;

	if (!drv)
		return 0;

	if (drv->pm) {
		if (drv->pm->thaw)
			ret = drv->pm->thaw(dev);
	} else {
		ret = ibmebus_bus_legacy_resume(dev);
	}

	return ret;
}

static int ibmebus_bus_pm_thaw_noirq(struct device *dev)
{
	struct device_driver *drv = dev->driver;
	int ret = 0;

	if (!drv)
		return 0;

	if (drv->pm) {
		if (drv->pm->thaw_noirq)
			ret = drv->pm->thaw_noirq(dev);
	}

	return ret;
}

static int ibmebus_bus_pm_poweroff(struct device *dev)
{
	struct device_driver *drv = dev->driver;
	int ret = 0;

	if (!drv)
		return 0;

	if (drv->pm) {
		if (drv->pm->poweroff)
			ret = drv->pm->poweroff(dev);
	} else {
		ret = ibmebus_bus_legacy_suspend(dev, PMSG_HIBERNATE);
	}

	return ret;
}

static int ibmebus_bus_pm_poweroff_noirq(struct device *dev)
{
	struct device_driver *drv = dev->driver;
	int ret = 0;

	if (!drv)
		return 0;

	if (drv->pm) {
		if (drv->pm->poweroff_noirq)
			ret = drv->pm->poweroff_noirq(dev);
	}

	return ret;
}

static int ibmebus_bus_pm_restore(struct device *dev)
{
	struct device_driver *drv = dev->driver;
	int ret = 0;

	if (!drv)
		return 0;

	if (drv->pm) {
		if (drv->pm->restore)
			ret = drv->pm->restore(dev);
	} else {
		ret = ibmebus_bus_legacy_resume(dev);
	}

	return ret;
}

static int ibmebus_bus_pm_restore_noirq(struct device *dev)
{
	struct device_driver *drv = dev->driver;
	int ret = 0;

	if (!drv)
		return 0;

	if (drv->pm) {
		if (drv->pm->restore_noirq)
			ret = drv->pm->restore_noirq(dev);
	}

	return ret;
}

#else /* !CONFIG_HIBERNATE_CALLBACKS */

#define ibmebus_bus_pm_freeze		NULL
#define ibmebus_bus_pm_thaw		NULL
#define ibmebus_bus_pm_poweroff		NULL
#define ibmebus_bus_pm_restore		NULL
#define ibmebus_bus_pm_freeze_noirq	NULL
#define ibmebus_bus_pm_thaw_noirq		NULL
#define ibmebus_bus_pm_poweroff_noirq	NULL
#define ibmebus_bus_pm_restore_noirq	NULL

#endif /* !CONFIG_HIBERNATE_CALLBACKS */

static struct dev_pm_ops ibmebus_bus_dev_pm_ops = {
	.prepare = ibmebus_bus_pm_prepare,
	.complete = ibmebus_bus_pm_complete,
	.suspend = ibmebus_bus_pm_suspend,
	.resume = ibmebus_bus_pm_resume,
	.freeze = ibmebus_bus_pm_freeze,
	.thaw = ibmebus_bus_pm_thaw,
	.poweroff = ibmebus_bus_pm_poweroff,
	.restore = ibmebus_bus_pm_restore,
	.suspend_noirq = ibmebus_bus_pm_suspend_noirq,
	.resume_noirq = ibmebus_bus_pm_resume_noirq,
	.freeze_noirq = ibmebus_bus_pm_freeze_noirq,
	.thaw_noirq = ibmebus_bus_pm_thaw_noirq,
	.poweroff_noirq = ibmebus_bus_pm_poweroff_noirq,
	.restore_noirq = ibmebus_bus_pm_restore_noirq,
};

#define IBMEBUS_BUS_PM_OPS_PTR	(&ibmebus_bus_dev_pm_ops)

#else /* !CONFIG_PM_SLEEP */

#define IBMEBUS_BUS_PM_OPS_PTR	NULL

#endif /* !CONFIG_PM_SLEEP */

struct bus_type ibmebus_bus_type = {
	.name      = "ibmebus",
	.uevent    = of_device_uevent_modalias,
	.bus_groups = ibmbus_bus_groups,
	.match     = ibmebus_bus_bus_match,
	.probe     = ibmebus_bus_device_probe,
	.remove    = ibmebus_bus_device_remove,
	.shutdown  = ibmebus_bus_device_shutdown,
	.dev_attrs = ibmebus_bus_device_attrs,
	.pm        = IBMEBUS_BUS_PM_OPS_PTR,
};
EXPORT_SYMBOL(ibmebus_bus_type);

static int __init ibmebus_bus_init(void)
{
	int err;

	printk(KERN_INFO "IBM eBus Device Driver\n");

	err = bus_register(&ibmebus_bus_type);
	if (err) {
		printk(KERN_ERR "%s: failed to register IBM eBus.\n",
		       __func__);
		return err;
	}

	err = device_register(&ibmebus_bus_device);
	if (err) {
		printk(KERN_WARNING "%s: device_register returned %i\n",
		       __func__, err);
		bus_unregister(&ibmebus_bus_type);

		return err;
	}

	err = ibmebus_create_devices(ibmebus_matches);
	if (err) {
		device_unregister(&ibmebus_bus_device);
		bus_unregister(&ibmebus_bus_type);
		return err;
	}

	return 0;
}
postcore_initcall(ibmebus_bus_init);

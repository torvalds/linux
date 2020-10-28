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
#include <linux/dma-map-ops.h>
#include <linux/interrupt.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/stat.h>
#include <linux/of_platform.h>
#include <asm/ibmebus.h>
#include <asm/machdep.h>

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

static const struct dma_map_ops ibmebus_dma_ops = {
	.alloc              = ibmebus_alloc_coherent,
	.free               = ibmebus_free_coherent,
	.map_sg             = ibmebus_map_sg,
	.unmap_sg           = ibmebus_unmap_sg,
	.dma_supported      = ibmebus_dma_supported,
	.get_required_mask  = ibmebus_dma_get_required_mask,
	.map_page           = ibmebus_map_page,
	.unmap_page         = ibmebus_unmap_page,
};

static int ibmebus_match_path(struct device *dev, const void *data)
{
	struct device_node *dn = to_platform_device(dev)->dev.of_node;
	return (of_find_node_by_path(data) == dn);
}

static int ibmebus_match_node(struct device *dev, const void *data)
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
	dev->dev.dma_ops = &ibmebus_dma_ops;

	ret = of_device_add(dev);
	if (ret)
		platform_device_put(dev);
	return ret;
}

static int ibmebus_create_devices(const struct of_device_id *matches)
{
	struct device_node *root, *child;
	struct device *dev;
	int ret = 0;

	root = of_find_node_by_path("/");

	for_each_child_of_node(root, child) {
		if (!of_match_node(matches, child))
			continue;

		dev = bus_find_device(&ibmebus_bus_type, NULL, child,
				      ibmebus_match_node);
		if (dev) {
			put_device(dev);
			continue;
		}

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

	if (!irq)
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

static ssize_t probe_store(struct bus_type *bus, const char *buf, size_t count)
{
	struct device_node *dn = NULL;
	struct device *dev;
	char *path;
	ssize_t rc = 0;

	path = ibmebus_chomp(buf, count);
	if (!path)
		return -ENOMEM;

	dev = bus_find_device(&ibmebus_bus_type, NULL, path,
			      ibmebus_match_path);
	if (dev) {
		put_device(dev);
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
static BUS_ATTR_WO(probe);

static ssize_t remove_store(struct bus_type *bus, const char *buf, size_t count)
{
	struct device *dev;
	char *path;

	path = ibmebus_chomp(buf, count);
	if (!path)
		return -ENOMEM;

	if ((dev = bus_find_device(&ibmebus_bus_type, NULL, path,
				   ibmebus_match_path))) {
		of_device_unregister(to_platform_device(dev));
		put_device(dev);

		kfree(path);
		return count;
	} else {
		printk(KERN_WARNING "%s: %s not on the bus\n",
		       __func__, path);

		kfree(path);
		return -ENODEV;
	}
}
static BUS_ATTR_WO(remove);

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
	return sprintf(buf, "%pOF\n", ofdev->dev.of_node);
}
static DEVICE_ATTR_RO(devspec);

static ssize_t name_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct platform_device *ofdev;

	ofdev = to_platform_device(dev);
	return sprintf(buf, "%pOFn\n", ofdev->dev.of_node);
}
static DEVICE_ATTR_RO(name);

static ssize_t modalias_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	return of_device_modalias(dev, buf, PAGE_SIZE);
}
static DEVICE_ATTR_RO(modalias);

static struct attribute *ibmebus_bus_device_attrs[] = {
	&dev_attr_devspec.attr,
	&dev_attr_name.attr,
	&dev_attr_modalias.attr,
	NULL,
};
ATTRIBUTE_GROUPS(ibmebus_bus_device);

struct bus_type ibmebus_bus_type = {
	.name      = "ibmebus",
	.uevent    = of_device_uevent_modalias,
	.bus_groups = ibmbus_bus_groups,
	.match     = ibmebus_bus_bus_match,
	.probe     = ibmebus_bus_device_probe,
	.remove    = ibmebus_bus_device_remove,
	.shutdown  = ibmebus_bus_device_shutdown,
	.dev_groups = ibmebus_bus_device_groups,
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
machine_postcore_initcall(pseries, ibmebus_bus_init);

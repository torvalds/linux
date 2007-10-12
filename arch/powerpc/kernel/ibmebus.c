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
#include <linux/console.h>
#include <linux/kobject.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <asm/ibmebus.h>
#include <asm/abs_addr.h>

static struct device ibmebus_bus_device = { /* fake "parent" device */
	.bus_id = "ibmebus",
};

struct bus_type ibmebus_bus_type;

static void *ibmebus_alloc_coherent(struct device *dev,
				    size_t size,
				    dma_addr_t *dma_handle,
				    gfp_t flag)
{
	void *mem;

	mem = kmalloc(size, flag);
	*dma_handle = (dma_addr_t)mem;

	return mem;
}

static void ibmebus_free_coherent(struct device *dev,
				  size_t size, void *vaddr,
				  dma_addr_t dma_handle)
{
	kfree(vaddr);
}

static dma_addr_t ibmebus_map_single(struct device *dev,
				     void *ptr,
				     size_t size,
				     enum dma_data_direction direction)
{
	return (dma_addr_t)(ptr);
}

static void ibmebus_unmap_single(struct device *dev,
				 dma_addr_t dma_addr,
				 size_t size,
				 enum dma_data_direction direction)
{
	return;
}

static int ibmebus_map_sg(struct device *dev,
			  struct scatterlist *sg,
			  int nents, enum dma_data_direction direction)
{
	int i;

	for (i = 0; i < nents; i++) {
		sg[i].dma_address = (dma_addr_t)page_address(sg[i].page)
			+ sg[i].offset;
		sg[i].dma_length = sg[i].length;
	}

	return nents;
}

static void ibmebus_unmap_sg(struct device *dev,
			     struct scatterlist *sg,
			     int nents, enum dma_data_direction direction)
{
	return;
}

static int ibmebus_dma_supported(struct device *dev, u64 mask)
{
	return 1;
}

static struct dma_mapping_ops ibmebus_dma_ops = {
	.alloc_coherent = ibmebus_alloc_coherent,
	.free_coherent  = ibmebus_free_coherent,
	.map_single     = ibmebus_map_single,
	.unmap_single   = ibmebus_unmap_single,
	.map_sg         = ibmebus_map_sg,
	.unmap_sg       = ibmebus_unmap_sg,
	.dma_supported  = ibmebus_dma_supported,
};

static int ibmebus_bus_probe(struct device *dev)
{
	struct ibmebus_dev *ibmebusdev    = to_ibmebus_dev(dev);
	struct ibmebus_driver *ibmebusdrv = to_ibmebus_driver(dev->driver);
	const struct of_device_id *id;
	int error = -ENODEV;

	if (!ibmebusdrv->probe)
		return error;

	id = of_match_device(ibmebusdrv->id_table, &ibmebusdev->ofdev);
	if (id) {
		error = ibmebusdrv->probe(ibmebusdev, id);
	}

	return error;
}

static int ibmebus_bus_remove(struct device *dev)
{
	struct ibmebus_dev *ibmebusdev    = to_ibmebus_dev(dev);
	struct ibmebus_driver *ibmebusdrv = to_ibmebus_driver(dev->driver);

	if (ibmebusdrv->remove) {
		return ibmebusdrv->remove(ibmebusdev);
	}

	return 0;
}

static void __devinit ibmebus_dev_release(struct device *dev)
{
	of_node_put(to_ibmebus_dev(dev)->ofdev.node);
	kfree(to_ibmebus_dev(dev));
}

static int __devinit ibmebus_register_device_common(
	struct ibmebus_dev *dev, const char *name)
{
	int err = 0;

	dev->ofdev.dev.parent  = &ibmebus_bus_device;
	dev->ofdev.dev.bus     = &ibmebus_bus_type;
	dev->ofdev.dev.release = ibmebus_dev_release;

	dev->ofdev.dev.archdata.of_node = dev->ofdev.node;
	dev->ofdev.dev.archdata.dma_ops = &ibmebus_dma_ops;
	dev->ofdev.dev.archdata.numa_node = of_node_to_nid(dev->ofdev.node);

	/* An ibmebusdev is based on a of_device. We have to change the
	 * bus type to use our own DMA mapping operations.
	 */
	if ((err = of_device_register(&dev->ofdev)) != 0) {
		printk(KERN_ERR "%s: failed to register device (%d).\n",
		       __FUNCTION__, err);
		return -ENODEV;
	}

	return 0;
}

static struct ibmebus_dev* __devinit ibmebus_register_device_node(
	struct device_node *dn)
{
	struct ibmebus_dev *dev;
	int i, len, bus_len;

	dev = kzalloc(sizeof(struct ibmebus_dev), GFP_KERNEL);
	if (!dev)
		return ERR_PTR(-ENOMEM);

	dev->ofdev.node = of_node_get(dn);

	len = strlen(dn->full_name + 1);
	bus_len = min(len, BUS_ID_SIZE - 1);
	memcpy(dev->ofdev.dev.bus_id, dn->full_name + 1
	       + (len - bus_len), bus_len);
	for (i = 0; i < bus_len; i++)
		if (dev->ofdev.dev.bus_id[i] == '/')
			dev->ofdev.dev.bus_id[i] = '_';

	/* Register with generic device framework. */
	if (ibmebus_register_device_common(dev, dn->name) != 0) {
		kfree(dev);
		return ERR_PTR(-ENODEV);
	}

	return dev;
}

static void ibmebus_probe_of_nodes(char* name)
{
	struct device_node *dn = NULL;

	while ((dn = of_find_node_by_name(dn, name))) {
		if (IS_ERR(ibmebus_register_device_node(dn))) {
			of_node_put(dn);
			return;
		}
	}

	of_node_put(dn);

	return;
}

static void ibmebus_add_devices_by_id(struct of_device_id *idt)
{
	while (strlen(idt->name) > 0) {
		ibmebus_probe_of_nodes(idt->name);
		idt++;
	}

	return;
}

static int ibmebus_match_name(struct device *dev, void *data)
{
	const struct ibmebus_dev *ebus_dev = to_ibmebus_dev(dev);
	const char *name;

	name = of_get_property(ebus_dev->ofdev.node, "name", NULL);

	if (name && (strcmp(data, name) == 0))
		return 1;

	return 0;
}

static int ibmebus_unregister_device(struct device *dev)
{
	of_device_unregister(to_of_device(dev));

	return 0;
}

static void ibmebus_remove_devices_by_id(struct of_device_id *idt)
{
	struct device *dev;

	while (strlen(idt->name) > 0) {
		while ((dev = bus_find_device(&ibmebus_bus_type, NULL,
					      (void*)idt->name,
					      ibmebus_match_name))) {
			ibmebus_unregister_device(dev);
		}
		idt++;
	}

	return;
}

int ibmebus_register_driver(struct ibmebus_driver *drv)
{
	int err = 0;

	drv->driver.name   = drv->name;
	drv->driver.bus    = &ibmebus_bus_type;
	drv->driver.probe  = ibmebus_bus_probe;
	drv->driver.remove = ibmebus_bus_remove;

	if ((err = driver_register(&drv->driver) != 0))
		return err;

	/* remove all supported devices first, in case someone
	 * probed them manually before registering the driver */
	ibmebus_remove_devices_by_id(drv->id_table);
	ibmebus_add_devices_by_id(drv->id_table);

	return 0;
}
EXPORT_SYMBOL(ibmebus_register_driver);

void ibmebus_unregister_driver(struct ibmebus_driver *drv)
{
	driver_unregister(&drv->driver);
	ibmebus_remove_devices_by_id(drv->id_table);
}
EXPORT_SYMBOL(ibmebus_unregister_driver);

int ibmebus_request_irq(struct ibmebus_dev *dev,
			u32 ist,
			irq_handler_t handler,
			unsigned long irq_flags, const char * devname,
			void *dev_id)
{
	unsigned int irq = irq_create_mapping(NULL, ist);

	if (irq == NO_IRQ)
		return -EINVAL;

	return request_irq(irq, handler,
			   irq_flags, devname, dev_id);
}
EXPORT_SYMBOL(ibmebus_request_irq);

void ibmebus_free_irq(struct ibmebus_dev *dev, u32 ist, void *dev_id)
{
	unsigned int irq = irq_find_mapping(NULL, ist);

	free_irq(irq, dev_id);
}
EXPORT_SYMBOL(ibmebus_free_irq);

static int ibmebus_bus_match(struct device *dev, struct device_driver *drv)
{
	const struct ibmebus_dev *ebus_dev = to_ibmebus_dev(dev);
	struct ibmebus_driver *ebus_drv    = to_ibmebus_driver(drv);
	const struct of_device_id *ids     = ebus_drv->id_table;
	const struct of_device_id *found_id;

	if (!ids)
		return 0;

	found_id = of_match_device(ids, &ebus_dev->ofdev);
	if (found_id)
		return 1;

	return 0;
}

static ssize_t name_show(struct device *dev,
			 struct device_attribute *attr, char *buf)
{
	struct ibmebus_dev *ebus_dev = to_ibmebus_dev(dev);
	const char *name = of_get_property(ebus_dev->ofdev.node, "name", NULL);
	return sprintf(buf, "%s\n", name);
}

static struct device_attribute ibmebus_dev_attrs[] = {
	__ATTR_RO(name),
	__ATTR_NULL
};

static int ibmebus_match_path(struct device *dev, void *data)
{
	int rc;
	struct device_node *dn =
		of_node_get(to_ibmebus_dev(dev)->ofdev.node);

	rc = (dn->full_name && (strcasecmp((char*)data, dn->full_name) == 0));

	of_node_put(dn);
	return rc;
}

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
	struct ibmebus_dev *dev;
	char *path;
	ssize_t rc;

	path = ibmebus_chomp(buf, count);
	if (!path)
		return -ENOMEM;

	if (bus_find_device(&ibmebus_bus_type, NULL, path,
			    ibmebus_match_path)) {
		printk(KERN_WARNING "%s: %s has already been probed\n",
		       __FUNCTION__, path);
		rc = -EEXIST;
		goto out;
	}

	if ((dn = of_find_node_by_path(path))) {
		dev = ibmebus_register_device_node(dn);
		of_node_put(dn);
		rc = IS_ERR(dev) ? PTR_ERR(dev) : count;
	} else {
		printk(KERN_WARNING "%s: no such device node: %s\n",
		       __FUNCTION__, path);
		rc = -ENODEV;
	}

out:
	kfree(path);
	return rc;
}

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
		ibmebus_unregister_device(dev);

		kfree(path);
		return count;
	} else {
		printk(KERN_WARNING "%s: %s not on the bus\n",
		       __FUNCTION__, path);

		kfree(path);
		return -ENODEV;
	}
}

static struct bus_attribute ibmebus_bus_attrs[] = {
	__ATTR(probe, S_IWUSR, NULL, ibmebus_store_probe),
	__ATTR(remove, S_IWUSR, NULL, ibmebus_store_remove),
	__ATTR_NULL
};

struct bus_type ibmebus_bus_type = {
	.name      = "ibmebus",
	.match     = ibmebus_bus_match,
	.dev_attrs = ibmebus_dev_attrs,
	.bus_attrs = ibmebus_bus_attrs
};
EXPORT_SYMBOL(ibmebus_bus_type);

static int __init ibmebus_bus_init(void)
{
	int err;

	printk(KERN_INFO "IBM eBus Device Driver\n");

	err = bus_register(&ibmebus_bus_type);
	if (err) {
		printk(KERN_ERR ":%s: failed to register IBM eBus.\n",
		       __FUNCTION__);
		return err;
	}

	err = device_register(&ibmebus_bus_device);
	if (err) {
		printk(KERN_WARNING "%s: device_register returned %i\n",
		       __FUNCTION__, err);
		bus_unregister(&ibmebus_bus_type);

		return err;
	}

	return 0;
}
__initcall(ibmebus_bus_init);

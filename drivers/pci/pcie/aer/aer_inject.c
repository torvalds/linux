/*
 * PCIE AER software error injection support.
 *
 * Debuging PCIE AER code is quite difficult because it is hard to
 * trigger various real hardware errors. Software based error
 * injection can fake almost all kinds of errors with the help of a
 * user space helper tool aer-inject, which can be gotten from:
 *   http://www.kernel.org/pub/linux/utils/pci/aer-inject/
 *
 * Copyright 2009 Intel Corporation.
 *     Huang Ying <ying.huang@intel.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2
 * of the License.
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/miscdevice.h>
#include <linux/pci.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include "aerdrv.h"

struct aer_error_inj {
	u8 bus;
	u8 dev;
	u8 fn;
	u32 uncor_status;
	u32 cor_status;
	u32 header_log0;
	u32 header_log1;
	u32 header_log2;
	u32 header_log3;
};

struct aer_error {
	struct list_head list;
	unsigned int bus;
	unsigned int devfn;
	int pos_cap_err;

	u32 uncor_status;
	u32 cor_status;
	u32 header_log0;
	u32 header_log1;
	u32 header_log2;
	u32 header_log3;
	u32 root_status;
	u32 source_id;
};

struct pci_bus_ops {
	struct list_head list;
	struct pci_bus *bus;
	struct pci_ops *ops;
};

static LIST_HEAD(einjected);

static LIST_HEAD(pci_bus_ops_list);

/* Protect einjected and pci_bus_ops_list */
static DEFINE_SPINLOCK(inject_lock);

static void aer_error_init(struct aer_error *err, unsigned int bus,
			   unsigned int devfn, int pos_cap_err)
{
	INIT_LIST_HEAD(&err->list);
	err->bus = bus;
	err->devfn = devfn;
	err->pos_cap_err = pos_cap_err;
}

/* inject_lock must be held before calling */
static struct aer_error *__find_aer_error(unsigned int bus, unsigned int devfn)
{
	struct aer_error *err;

	list_for_each_entry(err, &einjected, list) {
		if (bus == err->bus && devfn == err->devfn)
			return err;
	}
	return NULL;
}

/* inject_lock must be held before calling */
static struct aer_error *__find_aer_error_by_dev(struct pci_dev *dev)
{
	return __find_aer_error(dev->bus->number, dev->devfn);
}

/* inject_lock must be held before calling */
static struct pci_ops *__find_pci_bus_ops(struct pci_bus *bus)
{
	struct pci_bus_ops *bus_ops;

	list_for_each_entry(bus_ops, &pci_bus_ops_list, list) {
		if (bus_ops->bus == bus)
			return bus_ops->ops;
	}
	return NULL;
}

static struct pci_bus_ops *pci_bus_ops_pop(void)
{
	unsigned long flags;
	struct pci_bus_ops *bus_ops = NULL;

	spin_lock_irqsave(&inject_lock, flags);
	if (list_empty(&pci_bus_ops_list))
		bus_ops = NULL;
	else {
		struct list_head *lh = pci_bus_ops_list.next;
		list_del(lh);
		bus_ops = list_entry(lh, struct pci_bus_ops, list);
	}
	spin_unlock_irqrestore(&inject_lock, flags);
	return bus_ops;
}

static u32 *find_pci_config_dword(struct aer_error *err, int where,
				  int *prw1cs)
{
	int rw1cs = 0;
	u32 *target = NULL;

	if (err->pos_cap_err == -1)
		return NULL;

	switch (where - err->pos_cap_err) {
	case PCI_ERR_UNCOR_STATUS:
		target = &err->uncor_status;
		rw1cs = 1;
		break;
	case PCI_ERR_COR_STATUS:
		target = &err->cor_status;
		rw1cs = 1;
		break;
	case PCI_ERR_HEADER_LOG:
		target = &err->header_log0;
		break;
	case PCI_ERR_HEADER_LOG+4:
		target = &err->header_log1;
		break;
	case PCI_ERR_HEADER_LOG+8:
		target = &err->header_log2;
		break;
	case PCI_ERR_HEADER_LOG+12:
		target = &err->header_log3;
		break;
	case PCI_ERR_ROOT_STATUS:
		target = &err->root_status;
		rw1cs = 1;
		break;
	case PCI_ERR_ROOT_COR_SRC:
		target = &err->source_id;
		break;
	}
	if (prw1cs)
		*prw1cs = rw1cs;
	return target;
}

static int pci_read_aer(struct pci_bus *bus, unsigned int devfn, int where,
			int size, u32 *val)
{
	u32 *sim;
	struct aer_error *err;
	unsigned long flags;
	struct pci_ops *ops;

	spin_lock_irqsave(&inject_lock, flags);
	if (size != sizeof(u32))
		goto out;
	err = __find_aer_error(bus->number, devfn);
	if (!err)
		goto out;

	sim = find_pci_config_dword(err, where, NULL);
	if (sim) {
		*val = *sim;
		spin_unlock_irqrestore(&inject_lock, flags);
		return 0;
	}
out:
	ops = __find_pci_bus_ops(bus);
	spin_unlock_irqrestore(&inject_lock, flags);
	return ops->read(bus, devfn, where, size, val);
}

int pci_write_aer(struct pci_bus *bus, unsigned int devfn, int where, int size,
		  u32 val)
{
	u32 *sim;
	struct aer_error *err;
	unsigned long flags;
	int rw1cs;
	struct pci_ops *ops;

	spin_lock_irqsave(&inject_lock, flags);
	if (size != sizeof(u32))
		goto out;
	err = __find_aer_error(bus->number, devfn);
	if (!err)
		goto out;

	sim = find_pci_config_dword(err, where, &rw1cs);
	if (sim) {
		if (rw1cs)
			*sim ^= val;
		else
			*sim = val;
		spin_unlock_irqrestore(&inject_lock, flags);
		return 0;
	}
out:
	ops = __find_pci_bus_ops(bus);
	spin_unlock_irqrestore(&inject_lock, flags);
	return ops->write(bus, devfn, where, size, val);
}

static struct pci_ops pci_ops_aer = {
	.read = pci_read_aer,
	.write = pci_write_aer,
};

static void pci_bus_ops_init(struct pci_bus_ops *bus_ops,
			     struct pci_bus *bus,
			     struct pci_ops *ops)
{
	INIT_LIST_HEAD(&bus_ops->list);
	bus_ops->bus = bus;
	bus_ops->ops = ops;
}

static int pci_bus_set_aer_ops(struct pci_bus *bus)
{
	struct pci_ops *ops;
	struct pci_bus_ops *bus_ops;
	unsigned long flags;

	bus_ops = kmalloc(sizeof(*bus_ops), GFP_KERNEL);
	if (!bus_ops)
		return -ENOMEM;
	ops = pci_bus_set_ops(bus, &pci_ops_aer);
	spin_lock_irqsave(&inject_lock, flags);
	if (ops == &pci_ops_aer)
		goto out;
	pci_bus_ops_init(bus_ops, bus, ops);
	list_add(&bus_ops->list, &pci_bus_ops_list);
	bus_ops = NULL;
out:
	spin_unlock_irqrestore(&inject_lock, flags);
	kfree(bus_ops);
	return 0;
}

static struct pci_dev *pcie_find_root_port(struct pci_dev *dev)
{
	while (1) {
		if (!dev->is_pcie)
			break;
		if (dev->pcie_type == PCI_EXP_TYPE_ROOT_PORT)
			return dev;
		if (!dev->bus->self)
			break;
		dev = dev->bus->self;
	}
	return NULL;
}

static int find_aer_device_iter(struct device *device, void *data)
{
	struct pcie_device **result = data;
	struct pcie_device *pcie_dev;

	if (device->bus == &pcie_port_bus_type) {
		pcie_dev = to_pcie_device(device);
		if (pcie_dev->service & PCIE_PORT_SERVICE_AER) {
			*result = pcie_dev;
			return 1;
		}
	}
	return 0;
}

static int find_aer_device(struct pci_dev *dev, struct pcie_device **result)
{
	return device_for_each_child(&dev->dev, result, find_aer_device_iter);
}

static int aer_inject(struct aer_error_inj *einj)
{
	struct aer_error *err, *rperr;
	struct aer_error *err_alloc = NULL, *rperr_alloc = NULL;
	struct pci_dev *dev, *rpdev;
	struct pcie_device *edev;
	unsigned long flags;
	unsigned int devfn = PCI_DEVFN(einj->dev, einj->fn);
	int pos_cap_err, rp_pos_cap_err;
	u32 sever, cor_mask, uncor_mask;
	int ret = 0;

	dev = pci_get_bus_and_slot(einj->bus, devfn);
	if (!dev)
		return -EINVAL;
	rpdev = pcie_find_root_port(dev);
	if (!rpdev) {
		ret = -EINVAL;
		goto out_put;
	}

	pos_cap_err = pci_find_ext_capability(dev, PCI_EXT_CAP_ID_ERR);
	if (!pos_cap_err) {
		ret = -EIO;
		goto out_put;
	}
	pci_read_config_dword(dev, pos_cap_err + PCI_ERR_UNCOR_SEVER, &sever);
	pci_read_config_dword(dev, pos_cap_err + PCI_ERR_COR_MASK, &cor_mask);
	pci_read_config_dword(dev, pos_cap_err + PCI_ERR_UNCOR_MASK,
			      &uncor_mask);

	rp_pos_cap_err = pci_find_ext_capability(rpdev, PCI_EXT_CAP_ID_ERR);
	if (!rp_pos_cap_err) {
		ret = -EIO;
		goto out_put;
	}

	err_alloc =  kzalloc(sizeof(struct aer_error), GFP_KERNEL);
	if (!err_alloc) {
		ret = -ENOMEM;
		goto out_put;
	}
	rperr_alloc =  kzalloc(sizeof(struct aer_error), GFP_KERNEL);
	if (!rperr_alloc) {
		ret = -ENOMEM;
		goto out_put;
	}

	spin_lock_irqsave(&inject_lock, flags);

	err = __find_aer_error_by_dev(dev);
	if (!err) {
		err = err_alloc;
		err_alloc = NULL;
		aer_error_init(err, einj->bus, devfn, pos_cap_err);
		list_add(&err->list, &einjected);
	}
	err->uncor_status |= einj->uncor_status;
	err->cor_status |= einj->cor_status;
	err->header_log0 = einj->header_log0;
	err->header_log1 = einj->header_log1;
	err->header_log2 = einj->header_log2;
	err->header_log3 = einj->header_log3;

	if (einj->cor_status && !(einj->cor_status & ~cor_mask)) {
		ret = -EINVAL;
		printk(KERN_WARNING "The correctable error(s) is masked "
				"by device\n");
		spin_unlock_irqrestore(&inject_lock, flags);
		goto out_put;
	}
	if (einj->uncor_status && !(einj->uncor_status & ~uncor_mask)) {
		ret = -EINVAL;
		printk(KERN_WARNING "The uncorrectable error(s) is masked "
				"by device\n");
		spin_unlock_irqrestore(&inject_lock, flags);
		goto out_put;
	}

	rperr = __find_aer_error_by_dev(rpdev);
	if (!rperr) {
		rperr = rperr_alloc;
		rperr_alloc = NULL;
		aer_error_init(rperr, rpdev->bus->number, rpdev->devfn,
			       rp_pos_cap_err);
		list_add(&rperr->list, &einjected);
	}
	if (einj->cor_status) {
		if (rperr->root_status & PCI_ERR_ROOT_COR_RCV)
			rperr->root_status |= PCI_ERR_ROOT_MULTI_COR_RCV;
		else
			rperr->root_status |= PCI_ERR_ROOT_COR_RCV;
		rperr->source_id &= 0xffff0000;
		rperr->source_id |= (einj->bus << 8) | devfn;
	}
	if (einj->uncor_status) {
		if (rperr->root_status & PCI_ERR_ROOT_UNCOR_RCV)
			rperr->root_status |= PCI_ERR_ROOT_MULTI_UNCOR_RCV;
		if (sever & einj->uncor_status) {
			rperr->root_status |= PCI_ERR_ROOT_FATAL_RCV;
			if (!(rperr->root_status & PCI_ERR_ROOT_UNCOR_RCV))
				rperr->root_status |= PCI_ERR_ROOT_FIRST_FATAL;
		} else
			rperr->root_status |= PCI_ERR_ROOT_NONFATAL_RCV;
		rperr->root_status |= PCI_ERR_ROOT_UNCOR_RCV;
		rperr->source_id &= 0x0000ffff;
		rperr->source_id |= ((einj->bus << 8) | devfn) << 16;
	}
	spin_unlock_irqrestore(&inject_lock, flags);

	ret = pci_bus_set_aer_ops(dev->bus);
	if (ret)
		goto out_put;
	ret = pci_bus_set_aer_ops(rpdev->bus);
	if (ret)
		goto out_put;

	if (find_aer_device(rpdev, &edev)) {
		if (!get_service_data(edev)) {
			printk(KERN_WARNING "AER service is not initialized\n");
			ret = -EINVAL;
			goto out_put;
		}
		aer_irq(-1, edev);
	}
	else
		ret = -EINVAL;
out_put:
	kfree(err_alloc);
	kfree(rperr_alloc);
	pci_dev_put(dev);
	return ret;
}

static ssize_t aer_inject_write(struct file *filp, const char __user *ubuf,
				size_t usize, loff_t *off)
{
	struct aer_error_inj einj;
	int ret;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (usize != sizeof(struct aer_error_inj))
		return -EINVAL;

	if (copy_from_user(&einj, ubuf, usize))
		return -EFAULT;

	ret = aer_inject(&einj);
	return ret ? ret : usize;
}

static const struct file_operations aer_inject_fops = {
	.write = aer_inject_write,
	.owner = THIS_MODULE,
};

static struct miscdevice aer_inject_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "aer_inject",
	.fops = &aer_inject_fops,
};

static int __init aer_inject_init(void)
{
	return misc_register(&aer_inject_device);
}

static void __exit aer_inject_exit(void)
{
	struct aer_error *err, *err_next;
	unsigned long flags;
	struct pci_bus_ops *bus_ops;

	misc_deregister(&aer_inject_device);

	while ((bus_ops = pci_bus_ops_pop())) {
		pci_bus_set_ops(bus_ops->bus, bus_ops->ops);
		kfree(bus_ops);
	}

	spin_lock_irqsave(&inject_lock, flags);
	list_for_each_entry_safe(err, err_next, &pci_bus_ops_list, list) {
		list_del(&err->list);
		kfree(err);
	}
	spin_unlock_irqrestore(&inject_lock, flags);
}

module_init(aer_inject_init);
module_exit(aer_inject_exit);

MODULE_DESCRIPTION("PCIE AER software error injector");
MODULE_LICENSE("GPL");

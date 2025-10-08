// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2025 Ant Group
 * Author: Tiwei Bie <tiwei.btw@antgroup.com>
 */

#define pr_fmt(fmt) "vfio-uml: " fmt

#include <linux/module.h>
#include <linux/logic_iomem.h>
#include <linux/mutex.h>
#include <linux/list.h>
#include <linux/string.h>
#include <linux/unaligned.h>
#include <irq_kern.h>
#include <init.h>
#include <os.h>

#include "mconsole_kern.h"
#include "virt-pci.h"
#include "vfio_user.h"

#define to_vdev(_pdev) container_of(_pdev, struct uml_vfio_device, pdev)

struct uml_vfio_intr_ctx {
	struct uml_vfio_device *dev;
	int irq;
};

struct uml_vfio_device {
	const char *name;
	int group;

	struct um_pci_device pdev;
	struct uml_vfio_user_device udev;
	struct uml_vfio_intr_ctx *intr_ctx;

	int msix_cap;
	int msix_bar;
	int msix_offset;
	int msix_size;
	u32 *msix_data;

	struct list_head list;
};

struct uml_vfio_group {
	int id;
	int fd;
	int users;
	struct list_head list;
};

static struct {
	int fd;
	int users;
} uml_vfio_container = { .fd = -1 };
static DEFINE_MUTEX(uml_vfio_container_mtx);

static LIST_HEAD(uml_vfio_groups);
static DEFINE_MUTEX(uml_vfio_groups_mtx);

static LIST_HEAD(uml_vfio_devices);
static DEFINE_MUTEX(uml_vfio_devices_mtx);

static int uml_vfio_set_container(int group_fd)
{
	int err;

	guard(mutex)(&uml_vfio_container_mtx);

	err = uml_vfio_user_set_container(uml_vfio_container.fd, group_fd);
	if (err)
		return err;

	uml_vfio_container.users++;
	if (uml_vfio_container.users > 1)
		return 0;

	err = uml_vfio_user_setup_iommu(uml_vfio_container.fd);
	if (err) {
		uml_vfio_user_unset_container(uml_vfio_container.fd, group_fd);
		uml_vfio_container.users--;
	}
	return err;
}

static void uml_vfio_unset_container(int group_fd)
{
	guard(mutex)(&uml_vfio_container_mtx);

	uml_vfio_user_unset_container(uml_vfio_container.fd, group_fd);
	uml_vfio_container.users--;
}

static int uml_vfio_open_group(int group_id)
{
	struct uml_vfio_group *group;
	int err;

	guard(mutex)(&uml_vfio_groups_mtx);

	list_for_each_entry(group, &uml_vfio_groups, list) {
		if (group->id == group_id) {
			group->users++;
			return group->fd;
		}
	}

	group = kzalloc(sizeof(*group), GFP_KERNEL);
	if (!group)
		return -ENOMEM;

	group->fd = uml_vfio_user_open_group(group_id);
	if (group->fd < 0) {
		err = group->fd;
		goto free_group;
	}

	err = uml_vfio_set_container(group->fd);
	if (err)
		goto close_group;

	group->id = group_id;
	group->users = 1;

	list_add(&group->list, &uml_vfio_groups);

	return group->fd;

close_group:
	os_close_file(group->fd);
free_group:
	kfree(group);
	return err;
}

static int uml_vfio_release_group(int group_fd)
{
	struct uml_vfio_group *group;

	guard(mutex)(&uml_vfio_groups_mtx);

	list_for_each_entry(group, &uml_vfio_groups, list) {
		if (group->fd == group_fd) {
			group->users--;
			if (group->users == 0) {
				uml_vfio_unset_container(group_fd);
				os_close_file(group_fd);
				list_del(&group->list);
				kfree(group);
			}
			return 0;
		}
	}

	return -ENOENT;
}

static irqreturn_t uml_vfio_interrupt(int unused, void *opaque)
{
	struct uml_vfio_intr_ctx *ctx = opaque;
	struct uml_vfio_device *dev = ctx->dev;
	int index = ctx - dev->intr_ctx;
	int irqfd = dev->udev.irqfd[index];
	int irq = dev->msix_data[index];
	uint64_t v;
	int r;

	do {
		r = os_read_file(irqfd, &v, sizeof(v));
		if (r == sizeof(v))
			generic_handle_irq(irq);
	} while (r == sizeof(v) || r == -EINTR);
	WARN(r != -EAGAIN, "read returned %d\n", r);

	return IRQ_HANDLED;
}

static int uml_vfio_activate_irq(struct uml_vfio_device *dev, int index)
{
	struct uml_vfio_intr_ctx *ctx = &dev->intr_ctx[index];
	int err, irqfd;

	if (ctx->irq >= 0)
		return 0;

	irqfd = uml_vfio_user_activate_irq(&dev->udev, index);
	if (irqfd < 0)
		return irqfd;

	ctx->irq = um_request_irq(UM_IRQ_ALLOC, irqfd, IRQ_READ,
				  uml_vfio_interrupt, 0,
				  "vfio-uml", ctx);
	if (ctx->irq < 0) {
		err = ctx->irq;
		goto deactivate;
	}

	err = add_sigio_fd(irqfd);
	if (err)
		goto free_irq;

	return 0;

free_irq:
	um_free_irq(ctx->irq, ctx);
	ctx->irq = -1;
deactivate:
	uml_vfio_user_deactivate_irq(&dev->udev, index);
	return err;
}

static int uml_vfio_deactivate_irq(struct uml_vfio_device *dev, int index)
{
	struct uml_vfio_intr_ctx *ctx = &dev->intr_ctx[index];

	if (ctx->irq >= 0) {
		ignore_sigio_fd(dev->udev.irqfd[index]);
		um_free_irq(ctx->irq, ctx);
		uml_vfio_user_deactivate_irq(&dev->udev, index);
		ctx->irq = -1;
	}
	return 0;
}

static int uml_vfio_update_msix_cap(struct uml_vfio_device *dev,
				    unsigned int offset, int size,
				    unsigned long val)
{
	/*
	 * Here, we handle only the operations we care about,
	 * ignoring the rest.
	 */
	if (size == 2 && offset == dev->msix_cap + PCI_MSIX_FLAGS) {
		switch (val & ~PCI_MSIX_FLAGS_QSIZE) {
		case PCI_MSIX_FLAGS_ENABLE:
		case 0:
			return uml_vfio_user_update_irqs(&dev->udev);
		}
	}
	return 0;
}

static int uml_vfio_update_msix_table(struct uml_vfio_device *dev,
				      unsigned int offset, int size,
				      unsigned long val)
{
	int index;

	/*
	 * Here, we handle only the operations we care about,
	 * ignoring the rest.
	 */
	offset -= dev->msix_offset + PCI_MSIX_ENTRY_DATA;

	if (size != 4 || offset % PCI_MSIX_ENTRY_SIZE != 0)
		return 0;

	index = offset / PCI_MSIX_ENTRY_SIZE;
	if (index >= dev->udev.irq_count)
		return -EINVAL;

	dev->msix_data[index] = val;

	return val ? uml_vfio_activate_irq(dev, index) :
		uml_vfio_deactivate_irq(dev, index);
}

static unsigned long __uml_vfio_cfgspace_read(struct uml_vfio_device *dev,
					      unsigned int offset, int size)
{
	u8 data[8];

	memset(data, 0xff, sizeof(data));

	if (uml_vfio_user_cfgspace_read(&dev->udev, offset, data, size))
		return ULONG_MAX;

	switch (size) {
	case 1:
		return data[0];
	case 2:
		return le16_to_cpup((void *)data);
	case 4:
		return le32_to_cpup((void *)data);
#ifdef CONFIG_64BIT
	case 8:
		return le64_to_cpup((void *)data);
#endif
	default:
		return ULONG_MAX;
	}
}

static unsigned long uml_vfio_cfgspace_read(struct um_pci_device *pdev,
					    unsigned int offset, int size)
{
	struct uml_vfio_device *dev = to_vdev(pdev);

	return __uml_vfio_cfgspace_read(dev, offset, size);
}

static void __uml_vfio_cfgspace_write(struct uml_vfio_device *dev,
				      unsigned int offset, int size,
				      unsigned long val)
{
	u8 data[8];

	switch (size) {
	case 1:
		data[0] = (u8)val;
		break;
	case 2:
		put_unaligned_le16(val, (void *)data);
		break;
	case 4:
		put_unaligned_le32(val, (void *)data);
		break;
#ifdef CONFIG_64BIT
	case 8:
		put_unaligned_le64(val, (void *)data);
		break;
#endif
	}

	WARN_ON(uml_vfio_user_cfgspace_write(&dev->udev, offset, data, size));
}

static void uml_vfio_cfgspace_write(struct um_pci_device *pdev,
				    unsigned int offset, int size,
				    unsigned long val)
{
	struct uml_vfio_device *dev = to_vdev(pdev);

	if (offset < dev->msix_cap + PCI_CAP_MSIX_SIZEOF &&
	    offset + size > dev->msix_cap)
		WARN_ON(uml_vfio_update_msix_cap(dev, offset, size, val));

	__uml_vfio_cfgspace_write(dev, offset, size, val);
}

static void uml_vfio_bar_copy_from(struct um_pci_device *pdev, int bar,
				   void *buffer, unsigned int offset, int size)
{
	struct uml_vfio_device *dev = to_vdev(pdev);

	memset(buffer, 0xff, size);
	uml_vfio_user_bar_read(&dev->udev, bar, offset, buffer, size);
}

static unsigned long uml_vfio_bar_read(struct um_pci_device *pdev, int bar,
				       unsigned int offset, int size)
{
	u8 data[8];

	uml_vfio_bar_copy_from(pdev, bar, data, offset, size);

	switch (size) {
	case 1:
		return data[0];
	case 2:
		return le16_to_cpup((void *)data);
	case 4:
		return le32_to_cpup((void *)data);
#ifdef CONFIG_64BIT
	case 8:
		return le64_to_cpup((void *)data);
#endif
	default:
		return ULONG_MAX;
	}
}

static void uml_vfio_bar_copy_to(struct um_pci_device *pdev, int bar,
				 unsigned int offset, const void *buffer,
				 int size)
{
	struct uml_vfio_device *dev = to_vdev(pdev);

	uml_vfio_user_bar_write(&dev->udev, bar, offset, buffer, size);
}

static void uml_vfio_bar_write(struct um_pci_device *pdev, int bar,
			       unsigned int offset, int size,
			       unsigned long val)
{
	struct uml_vfio_device *dev = to_vdev(pdev);
	u8 data[8];

	if (bar == dev->msix_bar && offset + size > dev->msix_offset &&
	    offset < dev->msix_offset + dev->msix_size)
		WARN_ON(uml_vfio_update_msix_table(dev, offset, size, val));

	switch (size) {
	case 1:
		data[0] = (u8)val;
		break;
	case 2:
		put_unaligned_le16(val, (void *)data);
		break;
	case 4:
		put_unaligned_le32(val, (void *)data);
		break;
#ifdef CONFIG_64BIT
	case 8:
		put_unaligned_le64(val, (void *)data);
		break;
#endif
	}

	uml_vfio_bar_copy_to(pdev, bar, offset, data, size);
}

static void uml_vfio_bar_set(struct um_pci_device *pdev, int bar,
			     unsigned int offset, u8 value, int size)
{
	struct uml_vfio_device *dev = to_vdev(pdev);
	int i;

	for (i = 0; i < size; i++)
		uml_vfio_user_bar_write(&dev->udev, bar, offset + i, &value, 1);
}

static const struct um_pci_ops uml_vfio_um_pci_ops = {
	.cfgspace_read	= uml_vfio_cfgspace_read,
	.cfgspace_write	= uml_vfio_cfgspace_write,
	.bar_read	= uml_vfio_bar_read,
	.bar_write	= uml_vfio_bar_write,
	.bar_copy_from	= uml_vfio_bar_copy_from,
	.bar_copy_to	= uml_vfio_bar_copy_to,
	.bar_set	= uml_vfio_bar_set,
};

static u8 uml_vfio_find_capability(struct uml_vfio_device *dev, u8 cap)
{
	u8 id, pos;
	u16 ent;
	int ttl = 48; /* PCI_FIND_CAP_TTL */

	pos = __uml_vfio_cfgspace_read(dev, PCI_CAPABILITY_LIST, sizeof(pos));

	while (pos && ttl--) {
		ent = __uml_vfio_cfgspace_read(dev, pos, sizeof(ent));

		id = ent & 0xff;
		if (id == 0xff)
			break;
		if (id == cap)
			return pos;

		pos = ent >> 8;
	}

	return 0;
}

static int uml_vfio_read_msix_table(struct uml_vfio_device *dev)
{
	unsigned int off;
	u16 flags;
	u32 tbl;

	off = uml_vfio_find_capability(dev, PCI_CAP_ID_MSIX);
	if (!off)
		return -ENOTSUPP;

	dev->msix_cap = off;

	tbl = __uml_vfio_cfgspace_read(dev, off + PCI_MSIX_TABLE, sizeof(tbl));
	flags = __uml_vfio_cfgspace_read(dev, off + PCI_MSIX_FLAGS, sizeof(flags));

	dev->msix_bar = tbl & PCI_MSIX_TABLE_BIR;
	dev->msix_offset = tbl & PCI_MSIX_TABLE_OFFSET;
	dev->msix_size = ((flags & PCI_MSIX_FLAGS_QSIZE) + 1) * PCI_MSIX_ENTRY_SIZE;

	dev->msix_data = kzalloc(dev->msix_size, GFP_KERNEL);
	if (!dev->msix_data)
		return -ENOMEM;

	return 0;
}

static void uml_vfio_open_device(struct uml_vfio_device *dev)
{
	struct uml_vfio_intr_ctx *ctx;
	int err, group_id, i;

	group_id = uml_vfio_user_get_group_id(dev->name);
	if (group_id < 0) {
		pr_err("Failed to get group id (%s), error %d\n",
		       dev->name, group_id);
		goto free_dev;
	}

	dev->group = uml_vfio_open_group(group_id);
	if (dev->group < 0) {
		pr_err("Failed to open group %d (%s), error %d\n",
		       group_id, dev->name, dev->group);
		goto free_dev;
	}

	err = uml_vfio_user_setup_device(&dev->udev, dev->group, dev->name);
	if (err) {
		pr_err("Failed to setup device (%s), error %d\n",
		       dev->name, err);
		goto release_group;
	}

	err = uml_vfio_read_msix_table(dev);
	if (err) {
		pr_err("Failed to read MSI-X table (%s), error %d\n",
		       dev->name, err);
		goto teardown_udev;
	}

	dev->intr_ctx = kmalloc_array(dev->udev.irq_count,
				      sizeof(struct uml_vfio_intr_ctx),
				      GFP_KERNEL);
	if (!dev->intr_ctx) {
		pr_err("Failed to allocate interrupt context (%s)\n",
		       dev->name);
		goto free_msix;
	}

	for (i = 0; i < dev->udev.irq_count; i++) {
		ctx = &dev->intr_ctx[i];
		ctx->dev = dev;
		ctx->irq = -1;
	}

	dev->pdev.ops = &uml_vfio_um_pci_ops;

	err = um_pci_device_register(&dev->pdev);
	if (err) {
		pr_err("Failed to register UM PCI device (%s), error %d\n",
		       dev->name, err);
		goto free_intr_ctx;
	}

	return;

free_intr_ctx:
	kfree(dev->intr_ctx);
free_msix:
	kfree(dev->msix_data);
teardown_udev:
	uml_vfio_user_teardown_device(&dev->udev);
release_group:
	uml_vfio_release_group(dev->group);
free_dev:
	list_del(&dev->list);
	kfree(dev->name);
	kfree(dev);
}

static void uml_vfio_release_device(struct uml_vfio_device *dev)
{
	int i;

	for (i = 0; i < dev->udev.irq_count; i++)
		uml_vfio_deactivate_irq(dev, i);
	uml_vfio_user_update_irqs(&dev->udev);

	um_pci_device_unregister(&dev->pdev);
	kfree(dev->intr_ctx);
	kfree(dev->msix_data);
	uml_vfio_user_teardown_device(&dev->udev);
	uml_vfio_release_group(dev->group);
	list_del(&dev->list);
	kfree(dev->name);
	kfree(dev);
}

static struct uml_vfio_device *uml_vfio_find_device(const char *device)
{
	struct uml_vfio_device *dev;

	list_for_each_entry(dev, &uml_vfio_devices, list) {
		if (!strcmp(dev->name, device))
			return dev;
	}
	return NULL;
}

static struct uml_vfio_device *uml_vfio_add_device(const char *device)
{
	struct uml_vfio_device *dev;
	int fd;

	guard(mutex)(&uml_vfio_devices_mtx);

	if (uml_vfio_container.fd < 0) {
		fd = uml_vfio_user_open_container();
		if (fd < 0)
			return ERR_PTR(fd);
		uml_vfio_container.fd = fd;
	}

	if (uml_vfio_find_device(device))
		return ERR_PTR(-EEXIST);

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return ERR_PTR(-ENOMEM);

	dev->name = kstrdup(device, GFP_KERNEL);
	if (!dev->name) {
		kfree(dev);
		return ERR_PTR(-ENOMEM);
	}

	list_add_tail(&dev->list, &uml_vfio_devices);
	return dev;
}

static int uml_vfio_cmdline_set(const char *device, const struct kernel_param *kp)
{
	struct uml_vfio_device *dev;

	dev = uml_vfio_add_device(device);
	if (IS_ERR(dev))
		return PTR_ERR(dev);
	return 0;
}

static int uml_vfio_cmdline_get(char *buffer, const struct kernel_param *kp)
{
	return 0;
}

static const struct kernel_param_ops uml_vfio_cmdline_param_ops = {
	.set = uml_vfio_cmdline_set,
	.get = uml_vfio_cmdline_get,
};

device_param_cb(device, &uml_vfio_cmdline_param_ops, NULL, 0400);
__uml_help(uml_vfio_cmdline_param_ops,
"vfio_uml.device=<domain:bus:slot.function>\n"
"    Pass through a PCI device to UML via VFIO. Currently, only MSI-X\n"
"    capable devices are supported, and it is assumed that drivers will\n"
"    use MSI-X. This parameter can be specified multiple times to pass\n"
"    through multiple PCI devices to UML.\n\n"
);

static int uml_vfio_mc_config(char *str, char **error_out)
{
	struct uml_vfio_device *dev;

	if (*str != '=') {
		*error_out = "Invalid config";
		return -EINVAL;
	}
	str += 1;

	dev = uml_vfio_add_device(str);
	if (IS_ERR(dev))
		return PTR_ERR(dev);
	uml_vfio_open_device(dev);
	return 0;
}

static int uml_vfio_mc_id(char **str, int *start_out, int *end_out)
{
	return -EOPNOTSUPP;
}

static int uml_vfio_mc_remove(int n, char **error_out)
{
	return -EOPNOTSUPP;
}

static struct mc_device uml_vfio_mc = {
	.list           = LIST_HEAD_INIT(uml_vfio_mc.list),
	.name           = "vfio_uml.device",
	.config         = uml_vfio_mc_config,
	.get_config     = NULL,
	.id             = uml_vfio_mc_id,
	.remove         = uml_vfio_mc_remove,
};

static int __init uml_vfio_init(void)
{
	struct uml_vfio_device *dev, *n;

	sigio_broken();

	/* If the opening fails, the device will be released. */
	list_for_each_entry_safe(dev, n, &uml_vfio_devices, list)
		uml_vfio_open_device(dev);

	mconsole_register_dev(&uml_vfio_mc);

	return 0;
}
late_initcall(uml_vfio_init);

static void __exit uml_vfio_exit(void)
{
	struct uml_vfio_device *dev, *n;

	list_for_each_entry_safe(dev, n, &uml_vfio_devices, list)
		uml_vfio_release_device(dev);

	if (uml_vfio_container.fd >= 0)
		os_close_file(uml_vfio_container.fd);
}
module_exit(uml_vfio_exit);

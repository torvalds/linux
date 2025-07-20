// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2025 Ant Group
 * Author: Tiwei Bie <tiwei.btw@antgroup.com>
 */
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/eventfd.h>
#include <linux/limits.h>
#include <linux/vfio.h>
#include <linux/pci_regs.h>
#include <as-layout.h>
#include <um_malloc.h>

#include "vfio_user.h"

int uml_vfio_user_open_container(void)
{
	int r, fd;

	fd = open("/dev/vfio/vfio", O_RDWR);
	if (fd < 0)
		return -errno;

	r = ioctl(fd, VFIO_GET_API_VERSION);
	if (r != VFIO_API_VERSION) {
		r = r < 0 ? -errno : -EINVAL;
		goto error;
	}

	r = ioctl(fd, VFIO_CHECK_EXTENSION, VFIO_TYPE1_IOMMU);
	if (r <= 0) {
		r = r < 0 ? -errno : -EINVAL;
		goto error;
	}

	return fd;

error:
	close(fd);
	return r;
}

int uml_vfio_user_setup_iommu(int container)
{
	/*
	 * This is a bit tricky. See the big comment in
	 * vhost_user_set_mem_table() in virtio_uml.c.
	 */
	unsigned long reserved = uml_reserved - uml_physmem;
	struct vfio_iommu_type1_dma_map dma_map = {
		.argsz = sizeof(dma_map),
		.flags = VFIO_DMA_MAP_FLAG_READ | VFIO_DMA_MAP_FLAG_WRITE,
		.vaddr = uml_reserved,
		.iova = reserved,
		.size = physmem_size - reserved,
	};

	if (ioctl(container, VFIO_SET_IOMMU, VFIO_TYPE1_IOMMU) < 0)
		return -errno;

	if (ioctl(container, VFIO_IOMMU_MAP_DMA, &dma_map) < 0)
		return -errno;

	return 0;
}

int uml_vfio_user_get_group_id(const char *device)
{
	char *path, *buf, *end;
	const char *name;
	int r;

	path = uml_kmalloc(PATH_MAX, UM_GFP_KERNEL);
	if (!path)
		return -ENOMEM;

	sprintf(path, "/sys/bus/pci/devices/%s/iommu_group", device);

	buf = uml_kmalloc(PATH_MAX + 1, UM_GFP_KERNEL);
	if (!buf) {
		r = -ENOMEM;
		goto free_path;
	}

	r = readlink(path, buf, PATH_MAX);
	if (r < 0) {
		r = -errno;
		goto free_buf;
	}
	buf[r] = '\0';

	name = basename(buf);

	r = strtoul(name, &end, 10);
	if (*end != '\0' || end == name) {
		r = -EINVAL;
		goto free_buf;
	}

free_buf:
	kfree(buf);
free_path:
	kfree(path);
	return r;
}

int uml_vfio_user_open_group(int group_id)
{
	char *path;
	int fd;

	path = uml_kmalloc(PATH_MAX, UM_GFP_KERNEL);
	if (!path)
		return -ENOMEM;

	sprintf(path, "/dev/vfio/%d", group_id);

	fd = open(path, O_RDWR);
	if (fd < 0) {
		fd = -errno;
		goto out;
	}

out:
	kfree(path);
	return fd;
}

int uml_vfio_user_set_container(int container, int group)
{
	if (ioctl(group, VFIO_GROUP_SET_CONTAINER, &container) < 0)
		return -errno;
	return 0;
}

int uml_vfio_user_unset_container(int container, int group)
{
	if (ioctl(group, VFIO_GROUP_UNSET_CONTAINER, &container) < 0)
		return -errno;
	return 0;
}

static int vfio_set_irqs(int device, int start, int count, int *irqfd)
{
	struct vfio_irq_set *irq_set;
	int argsz = sizeof(*irq_set) + sizeof(*irqfd) * count;
	int err = 0;

	irq_set = uml_kmalloc(argsz, UM_GFP_KERNEL);
	if (!irq_set)
		return -ENOMEM;

	irq_set->argsz = argsz;
	irq_set->flags = VFIO_IRQ_SET_DATA_EVENTFD | VFIO_IRQ_SET_ACTION_TRIGGER;
	irq_set->index = VFIO_PCI_MSIX_IRQ_INDEX;
	irq_set->start = start;
	irq_set->count = count;
	memcpy(irq_set->data, irqfd, sizeof(*irqfd) * count);

	if (ioctl(device, VFIO_DEVICE_SET_IRQS, irq_set) < 0) {
		err = -errno;
		goto out;
	}

out:
	kfree(irq_set);
	return err;
}

int uml_vfio_user_setup_device(struct uml_vfio_user_device *dev,
			       int group, const char *device)
{
	struct vfio_device_info device_info = { .argsz = sizeof(device_info) };
	struct vfio_irq_info irq_info = { .argsz = sizeof(irq_info) };
	int err, i;

	dev->device = ioctl(group, VFIO_GROUP_GET_DEVICE_FD, device);
	if (dev->device < 0)
		return -errno;

	if (ioctl(dev->device, VFIO_DEVICE_GET_INFO, &device_info) < 0) {
		err = -errno;
		goto close_device;
	}

	dev->num_regions = device_info.num_regions;
	if (dev->num_regions > VFIO_PCI_CONFIG_REGION_INDEX + 1)
		dev->num_regions = VFIO_PCI_CONFIG_REGION_INDEX + 1;

	dev->region = uml_kmalloc(sizeof(*dev->region) * dev->num_regions,
				  UM_GFP_KERNEL);
	if (!dev->region) {
		err = -ENOMEM;
		goto close_device;
	}

	for (i = 0; i < dev->num_regions; i++) {
		struct vfio_region_info region = {
			.argsz = sizeof(region),
			.index = i,
		};
		if (ioctl(dev->device, VFIO_DEVICE_GET_REGION_INFO, &region) < 0) {
			err = -errno;
			goto free_region;
		}
		dev->region[i].size = region.size;
		dev->region[i].offset = region.offset;
	}

	/* Only MSI-X is supported currently. */
	irq_info.index = VFIO_PCI_MSIX_IRQ_INDEX;
	if (ioctl(dev->device, VFIO_DEVICE_GET_IRQ_INFO, &irq_info) < 0) {
		err = -errno;
		goto free_region;
	}

	dev->irq_count = irq_info.count;

	dev->irqfd = uml_kmalloc(sizeof(int) * dev->irq_count, UM_GFP_KERNEL);
	if (!dev->irqfd) {
		err = -ENOMEM;
		goto free_region;
	}

	memset(dev->irqfd, -1, sizeof(int) * dev->irq_count);

	err = vfio_set_irqs(dev->device, 0, dev->irq_count, dev->irqfd);
	if (err)
		goto free_irqfd;

	return 0;

free_irqfd:
	kfree(dev->irqfd);
free_region:
	kfree(dev->region);
close_device:
	close(dev->device);
	return err;
}

void uml_vfio_user_teardown_device(struct uml_vfio_user_device *dev)
{
	kfree(dev->irqfd);
	kfree(dev->region);
	close(dev->device);
}

int uml_vfio_user_activate_irq(struct uml_vfio_user_device *dev, int index)
{
	int irqfd;

	irqfd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
	if (irqfd < 0)
		return -errno;

	dev->irqfd[index] = irqfd;
	return irqfd;
}

void uml_vfio_user_deactivate_irq(struct uml_vfio_user_device *dev, int index)
{
	close(dev->irqfd[index]);
	dev->irqfd[index] = -1;
}

int uml_vfio_user_update_irqs(struct uml_vfio_user_device *dev)
{
	return vfio_set_irqs(dev->device, 0, dev->irq_count, dev->irqfd);
}

static int vfio_region_read(struct uml_vfio_user_device *dev, unsigned int index,
			    uint64_t offset, void *buf, uint64_t size)
{
	if (index >= dev->num_regions || offset + size > dev->region[index].size)
		return -EINVAL;

	if (pread(dev->device, buf, size, dev->region[index].offset + offset) < 0)
		return -errno;

	return 0;
}

static int vfio_region_write(struct uml_vfio_user_device *dev, unsigned int index,
			     uint64_t offset, const void *buf, uint64_t size)
{
	if (index >= dev->num_regions || offset + size > dev->region[index].size)
		return -EINVAL;

	if (pwrite(dev->device, buf, size, dev->region[index].offset + offset) < 0)
		return -errno;

	return 0;
}

int uml_vfio_user_cfgspace_read(struct uml_vfio_user_device *dev,
				unsigned int offset, void *buf, int size)
{
	return vfio_region_read(dev, VFIO_PCI_CONFIG_REGION_INDEX,
				offset, buf, size);
}

int uml_vfio_user_cfgspace_write(struct uml_vfio_user_device *dev,
				 unsigned int offset, const void *buf, int size)
{
	return vfio_region_write(dev, VFIO_PCI_CONFIG_REGION_INDEX,
				 offset, buf, size);
}

int uml_vfio_user_bar_read(struct uml_vfio_user_device *dev, int bar,
			   unsigned int offset, void *buf, int size)
{
	return vfio_region_read(dev, bar, offset, buf, size);
}

int uml_vfio_user_bar_write(struct uml_vfio_user_device *dev, int bar,
			    unsigned int offset, const void *buf, int size)
{
	return vfio_region_write(dev, bar, offset, buf, size);
}

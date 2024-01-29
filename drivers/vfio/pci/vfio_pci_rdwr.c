// SPDX-License-Identifier: GPL-2.0-only
/*
 * VFIO PCI I/O Port & MMIO access
 *
 * Copyright (C) 2012 Red Hat, Inc.  All rights reserved.
 *     Author: Alex Williamson <alex.williamson@redhat.com>
 *
 * Derived from original vfio:
 * Copyright 2010 Cisco Systems, Inc.  All rights reserved.
 * Author: Tom Lyon, pugs@cisco.com
 */

#include <linux/fs.h>
#include <linux/pci.h>
#include <linux/uaccess.h>
#include <linux/io.h>
#include <linux/vfio.h>
#include <linux/vgaarb.h>

#include "vfio_pci_priv.h"

#ifdef __LITTLE_ENDIAN
#define vfio_ioread64	ioread64
#define vfio_iowrite64	iowrite64
#define vfio_ioread32	ioread32
#define vfio_iowrite32	iowrite32
#define vfio_ioread16	ioread16
#define vfio_iowrite16	iowrite16
#else
#define vfio_ioread64	ioread64be
#define vfio_iowrite64	iowrite64be
#define vfio_ioread32	ioread32be
#define vfio_iowrite32	iowrite32be
#define vfio_ioread16	ioread16be
#define vfio_iowrite16	iowrite16be
#endif
#define vfio_ioread8	ioread8
#define vfio_iowrite8	iowrite8

#define VFIO_IOWRITE(size) \
int vfio_pci_core_iowrite##size(struct vfio_pci_core_device *vdev,	\
			bool test_mem, u##size val, void __iomem *io)	\
{									\
	if (test_mem) {							\
		down_read(&vdev->memory_lock);				\
		if (!__vfio_pci_memory_enabled(vdev)) {			\
			up_read(&vdev->memory_lock);			\
			return -EIO;					\
		}							\
	}								\
									\
	vfio_iowrite##size(val, io);					\
									\
	if (test_mem)							\
		up_read(&vdev->memory_lock);				\
									\
	return 0;							\
}									\
EXPORT_SYMBOL_GPL(vfio_pci_core_iowrite##size);

VFIO_IOWRITE(8)
VFIO_IOWRITE(16)
VFIO_IOWRITE(32)
#ifdef iowrite64
VFIO_IOWRITE(64)
#endif

#define VFIO_IOREAD(size) \
int vfio_pci_core_ioread##size(struct vfio_pci_core_device *vdev,	\
			bool test_mem, u##size *val, void __iomem *io)	\
{									\
	if (test_mem) {							\
		down_read(&vdev->memory_lock);				\
		if (!__vfio_pci_memory_enabled(vdev)) {			\
			up_read(&vdev->memory_lock);			\
			return -EIO;					\
		}							\
	}								\
									\
	*val = vfio_ioread##size(io);					\
									\
	if (test_mem)							\
		up_read(&vdev->memory_lock);				\
									\
	return 0;							\
}									\
EXPORT_SYMBOL_GPL(vfio_pci_core_ioread##size);

VFIO_IOREAD(8)
VFIO_IOREAD(16)
VFIO_IOREAD(32)

/*
 * Read or write from an __iomem region (MMIO or I/O port) with an excluded
 * range which is inaccessible.  The excluded range drops writes and fills
 * reads with -1.  This is intended for handling MSI-X vector tables and
 * leftover space for ROM BARs.
 */
static ssize_t do_io_rw(struct vfio_pci_core_device *vdev, bool test_mem,
			void __iomem *io, char __user *buf,
			loff_t off, size_t count, size_t x_start,
			size_t x_end, bool iswrite)
{
	ssize_t done = 0;
	int ret;

	while (count) {
		size_t fillable, filled;

		if (off < x_start)
			fillable = min(count, (size_t)(x_start - off));
		else if (off >= x_end)
			fillable = count;
		else
			fillable = 0;

		if (fillable >= 4 && !(off % 4)) {
			u32 val;

			if (iswrite) {
				if (copy_from_user(&val, buf, 4))
					return -EFAULT;

				ret = vfio_pci_core_iowrite32(vdev, test_mem,
							      val, io + off);
				if (ret)
					return ret;
			} else {
				ret = vfio_pci_core_ioread32(vdev, test_mem,
							     &val, io + off);
				if (ret)
					return ret;

				if (copy_to_user(buf, &val, 4))
					return -EFAULT;
			}

			filled = 4;
		} else if (fillable >= 2 && !(off % 2)) {
			u16 val;

			if (iswrite) {
				if (copy_from_user(&val, buf, 2))
					return -EFAULT;

				ret = vfio_pci_core_iowrite16(vdev, test_mem,
							      val, io + off);
				if (ret)
					return ret;
			} else {
				ret = vfio_pci_core_ioread16(vdev, test_mem,
							     &val, io + off);
				if (ret)
					return ret;

				if (copy_to_user(buf, &val, 2))
					return -EFAULT;
			}

			filled = 2;
		} else if (fillable) {
			u8 val;

			if (iswrite) {
				if (copy_from_user(&val, buf, 1))
					return -EFAULT;

				ret = vfio_pci_core_iowrite8(vdev, test_mem,
							     val, io + off);
				if (ret)
					return ret;
			} else {
				ret = vfio_pci_core_ioread8(vdev, test_mem,
							    &val, io + off);
				if (ret)
					return ret;

				if (copy_to_user(buf, &val, 1))
					return -EFAULT;
			}

			filled = 1;
		} else {
			/* Fill reads with -1, drop writes */
			filled = min(count, (size_t)(x_end - off));
			if (!iswrite) {
				u8 val = 0xFF;
				size_t i;

				for (i = 0; i < filled; i++)
					if (copy_to_user(buf + i, &val, 1))
						return -EFAULT;
			}
		}

		count -= filled;
		done += filled;
		off += filled;
		buf += filled;
	}

	return done;
}

int vfio_pci_core_setup_barmap(struct vfio_pci_core_device *vdev, int bar)
{
	struct pci_dev *pdev = vdev->pdev;
	int ret;
	void __iomem *io;

	if (vdev->barmap[bar])
		return 0;

	ret = pci_request_selected_regions(pdev, 1 << bar, "vfio");
	if (ret)
		return ret;

	io = pci_iomap(pdev, bar, 0);
	if (!io) {
		pci_release_selected_regions(pdev, 1 << bar);
		return -ENOMEM;
	}

	vdev->barmap[bar] = io;

	return 0;
}
EXPORT_SYMBOL_GPL(vfio_pci_core_setup_barmap);

ssize_t vfio_pci_bar_rw(struct vfio_pci_core_device *vdev, char __user *buf,
			size_t count, loff_t *ppos, bool iswrite)
{
	struct pci_dev *pdev = vdev->pdev;
	loff_t pos = *ppos & VFIO_PCI_OFFSET_MASK;
	int bar = VFIO_PCI_OFFSET_TO_INDEX(*ppos);
	size_t x_start = 0, x_end = 0;
	resource_size_t end;
	void __iomem *io;
	struct resource *res = &vdev->pdev->resource[bar];
	ssize_t done;

	if (pci_resource_start(pdev, bar))
		end = pci_resource_len(pdev, bar);
	else if (bar == PCI_ROM_RESOURCE &&
		 pdev->resource[bar].flags & IORESOURCE_ROM_SHADOW)
		end = 0x20000;
	else
		return -EINVAL;

	if (pos >= end)
		return -EINVAL;

	count = min(count, (size_t)(end - pos));

	if (bar == PCI_ROM_RESOURCE) {
		/*
		 * The ROM can fill less space than the BAR, so we start the
		 * excluded range at the end of the actual ROM.  This makes
		 * filling large ROM BARs much faster.
		 */
		io = pci_map_rom(pdev, &x_start);
		if (!io) {
			done = -ENOMEM;
			goto out;
		}
		x_end = end;
	} else {
		int ret = vfio_pci_core_setup_barmap(vdev, bar);
		if (ret) {
			done = ret;
			goto out;
		}

		io = vdev->barmap[bar];
	}

	if (bar == vdev->msix_bar) {
		x_start = vdev->msix_offset;
		x_end = vdev->msix_offset + vdev->msix_size;
	}

	done = do_io_rw(vdev, res->flags & IORESOURCE_MEM, io, buf, pos,
			count, x_start, x_end, iswrite);

	if (done >= 0)
		*ppos += done;

	if (bar == PCI_ROM_RESOURCE)
		pci_unmap_rom(pdev, io);
out:
	return done;
}

#ifdef CONFIG_VFIO_PCI_VGA
ssize_t vfio_pci_vga_rw(struct vfio_pci_core_device *vdev, char __user *buf,
			       size_t count, loff_t *ppos, bool iswrite)
{
	int ret;
	loff_t off, pos = *ppos & VFIO_PCI_OFFSET_MASK;
	void __iomem *iomem = NULL;
	unsigned int rsrc;
	bool is_ioport;
	ssize_t done;

	if (!vdev->has_vga)
		return -EINVAL;

	if (pos > 0xbfffful)
		return -EINVAL;

	switch ((u32)pos) {
	case 0xa0000 ... 0xbffff:
		count = min(count, (size_t)(0xc0000 - pos));
		iomem = ioremap(0xa0000, 0xbffff - 0xa0000 + 1);
		off = pos - 0xa0000;
		rsrc = VGA_RSRC_LEGACY_MEM;
		is_ioport = false;
		break;
	case 0x3b0 ... 0x3bb:
		count = min(count, (size_t)(0x3bc - pos));
		iomem = ioport_map(0x3b0, 0x3bb - 0x3b0 + 1);
		off = pos - 0x3b0;
		rsrc = VGA_RSRC_LEGACY_IO;
		is_ioport = true;
		break;
	case 0x3c0 ... 0x3df:
		count = min(count, (size_t)(0x3e0 - pos));
		iomem = ioport_map(0x3c0, 0x3df - 0x3c0 + 1);
		off = pos - 0x3c0;
		rsrc = VGA_RSRC_LEGACY_IO;
		is_ioport = true;
		break;
	default:
		return -EINVAL;
	}

	if (!iomem)
		return -ENOMEM;

	ret = vga_get_interruptible(vdev->pdev, rsrc);
	if (ret) {
		is_ioport ? ioport_unmap(iomem) : iounmap(iomem);
		return ret;
	}

	/*
	 * VGA MMIO is a legacy, non-BAR resource that hopefully allows
	 * probing, so we don't currently worry about access in relation
	 * to the memory enable bit in the command register.
	 */
	done = do_io_rw(vdev, false, iomem, buf, off, count, 0, 0, iswrite);

	vga_put(vdev->pdev, rsrc);

	is_ioport ? ioport_unmap(iomem) : iounmap(iomem);

	if (done >= 0)
		*ppos += done;

	return done;
}
#endif

static void vfio_pci_ioeventfd_do_write(struct vfio_pci_ioeventfd *ioeventfd,
					bool test_mem)
{
	switch (ioeventfd->count) {
	case 1:
		vfio_pci_core_iowrite8(ioeventfd->vdev, test_mem,
				       ioeventfd->data, ioeventfd->addr);
		break;
	case 2:
		vfio_pci_core_iowrite16(ioeventfd->vdev, test_mem,
					ioeventfd->data, ioeventfd->addr);
		break;
	case 4:
		vfio_pci_core_iowrite32(ioeventfd->vdev, test_mem,
					ioeventfd->data, ioeventfd->addr);
		break;
#ifdef iowrite64
	case 8:
		vfio_pci_core_iowrite64(ioeventfd->vdev, test_mem,
					ioeventfd->data, ioeventfd->addr);
		break;
#endif
	}
}

static int vfio_pci_ioeventfd_handler(void *opaque, void *unused)
{
	struct vfio_pci_ioeventfd *ioeventfd = opaque;
	struct vfio_pci_core_device *vdev = ioeventfd->vdev;

	if (ioeventfd->test_mem) {
		if (!down_read_trylock(&vdev->memory_lock))
			return 1; /* Lock contended, use thread */
		if (!__vfio_pci_memory_enabled(vdev)) {
			up_read(&vdev->memory_lock);
			return 0;
		}
	}

	vfio_pci_ioeventfd_do_write(ioeventfd, false);

	if (ioeventfd->test_mem)
		up_read(&vdev->memory_lock);

	return 0;
}

static void vfio_pci_ioeventfd_thread(void *opaque, void *unused)
{
	struct vfio_pci_ioeventfd *ioeventfd = opaque;

	vfio_pci_ioeventfd_do_write(ioeventfd, ioeventfd->test_mem);
}

int vfio_pci_ioeventfd(struct vfio_pci_core_device *vdev, loff_t offset,
		       uint64_t data, int count, int fd)
{
	struct pci_dev *pdev = vdev->pdev;
	loff_t pos = offset & VFIO_PCI_OFFSET_MASK;
	int ret, bar = VFIO_PCI_OFFSET_TO_INDEX(offset);
	struct vfio_pci_ioeventfd *ioeventfd;

	/* Only support ioeventfds into BARs */
	if (bar > VFIO_PCI_BAR5_REGION_INDEX)
		return -EINVAL;

	if (pos + count > pci_resource_len(pdev, bar))
		return -EINVAL;

	/* Disallow ioeventfds working around MSI-X table writes */
	if (bar == vdev->msix_bar &&
	    !(pos + count <= vdev->msix_offset ||
	      pos >= vdev->msix_offset + vdev->msix_size))
		return -EINVAL;

#ifndef iowrite64
	if (count == 8)
		return -EINVAL;
#endif

	ret = vfio_pci_core_setup_barmap(vdev, bar);
	if (ret)
		return ret;

	mutex_lock(&vdev->ioeventfds_lock);

	list_for_each_entry(ioeventfd, &vdev->ioeventfds_list, next) {
		if (ioeventfd->pos == pos && ioeventfd->bar == bar &&
		    ioeventfd->data == data && ioeventfd->count == count) {
			if (fd == -1) {
				vfio_virqfd_disable(&ioeventfd->virqfd);
				list_del(&ioeventfd->next);
				vdev->ioeventfds_nr--;
				kfree(ioeventfd);
				ret = 0;
			} else
				ret = -EEXIST;

			goto out_unlock;
		}
	}

	if (fd < 0) {
		ret = -ENODEV;
		goto out_unlock;
	}

	if (vdev->ioeventfds_nr >= VFIO_PCI_IOEVENTFD_MAX) {
		ret = -ENOSPC;
		goto out_unlock;
	}

	ioeventfd = kzalloc(sizeof(*ioeventfd), GFP_KERNEL_ACCOUNT);
	if (!ioeventfd) {
		ret = -ENOMEM;
		goto out_unlock;
	}

	ioeventfd->vdev = vdev;
	ioeventfd->addr = vdev->barmap[bar] + pos;
	ioeventfd->data = data;
	ioeventfd->pos = pos;
	ioeventfd->bar = bar;
	ioeventfd->count = count;
	ioeventfd->test_mem = vdev->pdev->resource[bar].flags & IORESOURCE_MEM;

	ret = vfio_virqfd_enable(ioeventfd, vfio_pci_ioeventfd_handler,
				 vfio_pci_ioeventfd_thread, NULL,
				 &ioeventfd->virqfd, fd);
	if (ret) {
		kfree(ioeventfd);
		goto out_unlock;
	}

	list_add(&ioeventfd->next, &vdev->ioeventfds_list);
	vdev->ioeventfds_nr++;

out_unlock:
	mutex_unlock(&vdev->ioeventfds_lock);

	return ret;
}

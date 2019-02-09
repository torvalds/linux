/*
 * VFIO PCI I/O Port & MMIO access
 *
 * Copyright (C) 2012 Red Hat, Inc.  All rights reserved.
 *     Author: Alex Williamson <alex.williamson@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Derived from original vfio:
 * Copyright 2010 Cisco Systems, Inc.  All rights reserved.
 * Author: Tom Lyon, pugs@cisco.com
 */

#include <linux/fs.h>
#include <linux/pci.h>
#include <linux/uaccess.h>
#include <linux/io.h>
#include <linux/vgaarb.h>

#include "vfio_pci_private.h"

/*
 * Read or write from an __iomem region (MMIO or I/O port) with an excluded
 * range which is inaccessible.  The excluded range drops writes and fills
 * reads with -1.  This is intended for handling MSI-X vector tables and
 * leftover space for ROM BARs.
 */
static ssize_t do_io_rw(void __iomem *io, char __user *buf,
			loff_t off, size_t count, size_t x_start,
			size_t x_end, bool iswrite)
{
	ssize_t done = 0;

	while (count) {
		size_t fillable, filled;

		if (off < x_start)
			fillable = min(count, (size_t)(x_start - off));
		else if (off >= x_end)
			fillable = count;
		else
			fillable = 0;

		if (fillable >= 4 && !(off % 4)) {
			__le32 val;

			if (iswrite) {
				if (copy_from_user(&val, buf, 4))
					return -EFAULT;

				iowrite32(le32_to_cpu(val), io + off);
			} else {
				val = cpu_to_le32(ioread32(io + off));

				if (copy_to_user(buf, &val, 4))
					return -EFAULT;
			}

			filled = 4;
		} else if (fillable >= 2 && !(off % 2)) {
			__le16 val;

			if (iswrite) {
				if (copy_from_user(&val, buf, 2))
					return -EFAULT;

				iowrite16(le16_to_cpu(val), io + off);
			} else {
				val = cpu_to_le16(ioread16(io + off));

				if (copy_to_user(buf, &val, 2))
					return -EFAULT;
			}

			filled = 2;
		} else if (fillable) {
			u8 val;

			if (iswrite) {
				if (copy_from_user(&val, buf, 1))
					return -EFAULT;

				iowrite8(val, io + off);
			} else {
				val = ioread8(io + off);

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

ssize_t vfio_pci_bar_rw(struct vfio_pci_device *vdev, char __user *buf,
			size_t count, loff_t *ppos, bool iswrite)
{
	struct pci_dev *pdev = vdev->pdev;
	loff_t pos = *ppos & VFIO_PCI_OFFSET_MASK;
	int bar = VFIO_PCI_OFFSET_TO_INDEX(*ppos);
	size_t x_start = 0, x_end = 0;
	resource_size_t end;
	void __iomem *io;
	ssize_t done;

	if (!pci_resource_start(pdev, bar))
		return -EINVAL;

	end = pci_resource_len(pdev, bar);

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
		if (!io)
			return -ENOMEM;
		x_end = end;
	} else if (!vdev->barmap[bar]) {
		int ret;

		ret = pci_request_selected_regions(pdev, 1 << bar, "vfio");
		if (ret)
			return ret;

		io = pci_iomap(pdev, bar, 0);
		if (!io) {
			pci_release_selected_regions(pdev, 1 << bar);
			return -ENOMEM;
		}

		vdev->barmap[bar] = io;
	} else
		io = vdev->barmap[bar];

	if (bar == vdev->msix_bar) {
		x_start = vdev->msix_offset;
		x_end = vdev->msix_offset + vdev->msix_size;
	}

	done = do_io_rw(io, buf, pos, count, x_start, x_end, iswrite);

	if (done >= 0)
		*ppos += done;

	if (bar == PCI_ROM_RESOURCE)
		pci_unmap_rom(pdev, io);

	return done;
}

ssize_t vfio_pci_vga_rw(struct vfio_pci_device *vdev, char __user *buf,
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

	switch (pos) {
	case 0xa0000 ... 0xbffff:
		count = min(count, (size_t)(0xc0000 - pos));
		iomem = ioremap_nocache(0xa0000, 0xbffff - 0xa0000 + 1);
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

	done = do_io_rw(iomem, buf, off, count, 0, 0, iswrite);

	vga_put(vdev->pdev, rsrc);

	is_ioport ? ioport_unmap(iomem) : iounmap(iomem);

	if (done >= 0)
		*ppos += done;

	return done;
}

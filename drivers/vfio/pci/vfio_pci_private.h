/*
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

#include <linux/mutex.h>
#include <linux/pci.h>

#ifndef VFIO_PCI_PRIVATE_H
#define VFIO_PCI_PRIVATE_H

#define VFIO_PCI_OFFSET_SHIFT   40

#define VFIO_PCI_OFFSET_TO_INDEX(off)	(off >> VFIO_PCI_OFFSET_SHIFT)
#define VFIO_PCI_INDEX_TO_OFFSET(index)	((u64)(index) << VFIO_PCI_OFFSET_SHIFT)
#define VFIO_PCI_OFFSET_MASK	(((u64)(1) << VFIO_PCI_OFFSET_SHIFT) - 1)

struct vfio_pci_irq_ctx {
	struct eventfd_ctx	*trigger;
	struct virqfd		*unmask;
	struct virqfd		*mask;
	char			*name;
	bool			masked;
};

struct vfio_pci_device {
	struct pci_dev		*pdev;
	void __iomem		*barmap[PCI_STD_RESOURCE_END + 1];
	u8			*pci_config_map;
	u8			*vconfig;
	struct perm_bits	*msi_perm;
	spinlock_t		irqlock;
	struct mutex		igate;
	struct msix_entry	*msix;
	struct vfio_pci_irq_ctx	*ctx;
	int			num_ctx;
	int			irq_type;
	u8			msi_qmax;
	u8			msix_bar;
	u16			msix_size;
	u32			msix_offset;
	u32			rbar[7];
	bool			pci_2_3;
	bool			virq_disabled;
	bool			reset_works;
	bool			extended_caps;
	bool			bardirty;
	struct pci_saved_state	*pci_saved_state;
	atomic_t		refcnt;
};

#define is_intx(vdev) (vdev->irq_type == VFIO_PCI_INTX_IRQ_INDEX)
#define is_msi(vdev) (vdev->irq_type == VFIO_PCI_MSI_IRQ_INDEX)
#define is_msix(vdev) (vdev->irq_type == VFIO_PCI_MSIX_IRQ_INDEX)
#define is_irq_none(vdev) (!(is_intx(vdev) || is_msi(vdev) || is_msix(vdev)))
#define irq_is(vdev, type) (vdev->irq_type == type)

extern void vfio_pci_intx_mask(struct vfio_pci_device *vdev);
extern void vfio_pci_intx_unmask(struct vfio_pci_device *vdev);

extern int vfio_pci_set_irqs_ioctl(struct vfio_pci_device *vdev,
				   uint32_t flags, unsigned index,
				   unsigned start, unsigned count, void *data);

extern ssize_t vfio_pci_config_readwrite(struct vfio_pci_device *vdev,
					 char __user *buf, size_t count,
					 loff_t *ppos, bool iswrite);
extern ssize_t vfio_pci_mem_readwrite(struct vfio_pci_device *vdev,
				      char __user *buf, size_t count,
				      loff_t *ppos, bool iswrite);
extern ssize_t vfio_pci_io_readwrite(struct vfio_pci_device *vdev,
				     char __user *buf, size_t count,
				     loff_t *ppos, bool iswrite);

extern int vfio_pci_init_perm_bits(void);
extern void vfio_pci_uninit_perm_bits(void);

extern int vfio_pci_virqfd_init(void);
extern void vfio_pci_virqfd_exit(void);

extern int vfio_config_init(struct vfio_pci_device *vdev);
extern void vfio_config_free(struct vfio_pci_device *vdev);
#endif /* VFIO_PCI_PRIVATE_H */

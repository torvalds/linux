/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2012 Red Hat, Inc.  All rights reserved.
 *     Author: Alex Williamson <alex.williamson@redhat.com>
 *
 * Derived from original vfio:
 * Copyright 2010 Cisco Systems, Inc.  All rights reserved.
 * Author: Tom Lyon, pugs@cisco.com
 */

#include <linux/mutex.h>
#include <linux/pci.h>
#include <linux/vfio.h>
#include <linux/irqbypass.h>
#include <linux/types.h>
#include <linux/uuid.h>
#include <linux/notifier.h>

#ifndef VFIO_PCI_CORE_H
#define VFIO_PCI_CORE_H

#define VFIO_PCI_OFFSET_SHIFT   40

#define VFIO_PCI_OFFSET_TO_INDEX(off)	(off >> VFIO_PCI_OFFSET_SHIFT)
#define VFIO_PCI_INDEX_TO_OFFSET(index)	((u64)(index) << VFIO_PCI_OFFSET_SHIFT)
#define VFIO_PCI_OFFSET_MASK	(((u64)(1) << VFIO_PCI_OFFSET_SHIFT) - 1)

/* Special capability IDs predefined access */
#define PCI_CAP_ID_INVALID		0xFF	/* default raw access */
#define PCI_CAP_ID_INVALID_VIRT		0xFE	/* default virt access */

/* Cap maximum number of ioeventfds per device (arbitrary) */
#define VFIO_PCI_IOEVENTFD_MAX		1000

struct vfio_pci_ioeventfd {
	struct list_head	next;
	struct vfio_pci_core_device	*vdev;
	struct virqfd		*virqfd;
	void __iomem		*addr;
	uint64_t		data;
	loff_t			pos;
	int			bar;
	int			count;
	bool			test_mem;
};

struct vfio_pci_irq_ctx {
	struct eventfd_ctx	*trigger;
	struct virqfd		*unmask;
	struct virqfd		*mask;
	char			*name;
	bool			masked;
	struct irq_bypass_producer	producer;
};

struct vfio_pci_core_device;
struct vfio_pci_region;

struct vfio_pci_regops {
	ssize_t (*rw)(struct vfio_pci_core_device *vdev, char __user *buf,
		      size_t count, loff_t *ppos, bool iswrite);
	void	(*release)(struct vfio_pci_core_device *vdev,
			   struct vfio_pci_region *region);
	int	(*mmap)(struct vfio_pci_core_device *vdev,
			struct vfio_pci_region *region,
			struct vm_area_struct *vma);
	int	(*add_capability)(struct vfio_pci_core_device *vdev,
				  struct vfio_pci_region *region,
				  struct vfio_info_cap *caps);
};

struct vfio_pci_region {
	u32				type;
	u32				subtype;
	const struct vfio_pci_regops	*ops;
	void				*data;
	size_t				size;
	u32				flags;
};

struct vfio_pci_dummy_resource {
	struct resource		resource;
	int			index;
	struct list_head	res_next;
};

struct vfio_pci_vf_token {
	struct mutex		lock;
	uuid_t			uuid;
	int			users;
};

struct vfio_pci_mmap_vma {
	struct vm_area_struct	*vma;
	struct list_head	vma_next;
};

struct vfio_pci_core_device {
	struct vfio_device	vdev;
	struct pci_dev		*pdev;
	void __iomem		*barmap[PCI_STD_NUM_BARS];
	bool			bar_mmap_supported[PCI_STD_NUM_BARS];
	u8			*pci_config_map;
	u8			*vconfig;
	struct perm_bits	*msi_perm;
	spinlock_t		irqlock;
	struct mutex		igate;
	struct vfio_pci_irq_ctx	*ctx;
	int			num_ctx;
	int			irq_type;
	int			num_regions;
	struct vfio_pci_region	*region;
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
	bool			has_vga;
	bool			needs_reset;
	bool			nointx;
	bool			needs_pm_restore;
	struct pci_saved_state	*pci_saved_state;
	struct pci_saved_state	*pm_save;
	int			ioeventfds_nr;
	struct eventfd_ctx	*err_trigger;
	struct eventfd_ctx	*req_trigger;
	struct list_head	dummy_resources_list;
	struct mutex		ioeventfds_lock;
	struct list_head	ioeventfds_list;
	struct vfio_pci_vf_token	*vf_token;
	struct notifier_block	nb;
	struct mutex		vma_lock;
	struct list_head	vma_list;
	struct rw_semaphore	memory_lock;
};

#define is_intx(vdev) (vdev->irq_type == VFIO_PCI_INTX_IRQ_INDEX)
#define is_msi(vdev) (vdev->irq_type == VFIO_PCI_MSI_IRQ_INDEX)
#define is_msix(vdev) (vdev->irq_type == VFIO_PCI_MSIX_IRQ_INDEX)
#define is_irq_none(vdev) (!(is_intx(vdev) || is_msi(vdev) || is_msix(vdev)))
#define irq_is(vdev, type) (vdev->irq_type == type)

extern void vfio_pci_intx_mask(struct vfio_pci_core_device *vdev);
extern void vfio_pci_intx_unmask(struct vfio_pci_core_device *vdev);

extern int vfio_pci_set_irqs_ioctl(struct vfio_pci_core_device *vdev,
				   uint32_t flags, unsigned index,
				   unsigned start, unsigned count, void *data);

extern ssize_t vfio_pci_config_rw(struct vfio_pci_core_device *vdev,
				  char __user *buf, size_t count,
				  loff_t *ppos, bool iswrite);

extern ssize_t vfio_pci_bar_rw(struct vfio_pci_core_device *vdev, char __user *buf,
			       size_t count, loff_t *ppos, bool iswrite);

extern ssize_t vfio_pci_vga_rw(struct vfio_pci_core_device *vdev, char __user *buf,
			       size_t count, loff_t *ppos, bool iswrite);

extern long vfio_pci_ioeventfd(struct vfio_pci_core_device *vdev, loff_t offset,
			       uint64_t data, int count, int fd);

extern int vfio_pci_init_perm_bits(void);
extern void vfio_pci_uninit_perm_bits(void);

extern int vfio_config_init(struct vfio_pci_core_device *vdev);
extern void vfio_config_free(struct vfio_pci_core_device *vdev);

extern int vfio_pci_register_dev_region(struct vfio_pci_core_device *vdev,
					unsigned int type, unsigned int subtype,
					const struct vfio_pci_regops *ops,
					size_t size, u32 flags, void *data);

extern int vfio_pci_set_power_state(struct vfio_pci_core_device *vdev,
				    pci_power_t state);

extern bool __vfio_pci_memory_enabled(struct vfio_pci_core_device *vdev);
extern void vfio_pci_zap_and_down_write_memory_lock(struct vfio_pci_core_device
						    *vdev);
extern u16 vfio_pci_memory_lock_and_enable(struct vfio_pci_core_device *vdev);
extern void vfio_pci_memory_unlock_and_restore(struct vfio_pci_core_device *vdev,
					       u16 cmd);

#ifdef CONFIG_VFIO_PCI_IGD
extern int vfio_pci_igd_init(struct vfio_pci_core_device *vdev);
#else
static inline int vfio_pci_igd_init(struct vfio_pci_core_device *vdev)
{
	return -ENODEV;
}
#endif

#ifdef CONFIG_S390
extern int vfio_pci_info_zdev_add_caps(struct vfio_pci_core_device *vdev,
				       struct vfio_info_cap *caps);
#else
static inline int vfio_pci_info_zdev_add_caps(struct vfio_pci_core_device *vdev,
					      struct vfio_info_cap *caps)
{
	return -ENODEV;
}
#endif

/* Will be exported for vfio pci drivers usage */
void vfio_pci_core_cleanup(void);
int vfio_pci_core_init(void);
void vfio_pci_core_set_params(bool nointxmask, bool is_disable_vga,
			      bool is_disable_idle_d3);
void vfio_pci_core_close_device(struct vfio_device *core_vdev);
void vfio_pci_core_init_device(struct vfio_pci_core_device *vdev,
			       struct pci_dev *pdev,
			       const struct vfio_device_ops *vfio_pci_ops);
int vfio_pci_core_register_device(struct vfio_pci_core_device *vdev);
void vfio_pci_core_uninit_device(struct vfio_pci_core_device *vdev);
void vfio_pci_core_unregister_device(struct vfio_pci_core_device *vdev);
int vfio_pci_core_sriov_configure(struct pci_dev *pdev, int nr_virtfn);
extern const struct pci_error_handlers vfio_pci_core_err_handlers;
long vfio_pci_core_ioctl(struct vfio_device *core_vdev, unsigned int cmd,
		unsigned long arg);
ssize_t vfio_pci_core_read(struct vfio_device *core_vdev, char __user *buf,
		size_t count, loff_t *ppos);
ssize_t vfio_pci_core_write(struct vfio_device *core_vdev, const char __user *buf,
		size_t count, loff_t *ppos);
int vfio_pci_core_mmap(struct vfio_device *core_vdev, struct vm_area_struct *vma);
void vfio_pci_core_request(struct vfio_device *core_vdev, unsigned int count);
int vfio_pci_core_match(struct vfio_device *core_vdev, char *buf);
int vfio_pci_core_enable(struct vfio_pci_core_device *vdev);
void vfio_pci_core_disable(struct vfio_pci_core_device *vdev);
void vfio_pci_core_finish_enable(struct vfio_pci_core_device *vdev);

static inline bool vfio_pci_is_vga(struct pci_dev *pdev)
{
	return (pdev->class >> 8) == PCI_CLASS_DISPLAY_VGA;
}

#endif /* VFIO_PCI_CORE_H */

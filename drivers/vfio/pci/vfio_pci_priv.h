/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef VFIO_PCI_PRIV_H
#define VFIO_PCI_PRIV_H

#include <linux/vfio_pci_core.h>

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

bool vfio_pci_intx_mask(struct vfio_pci_core_device *vdev);
void vfio_pci_intx_unmask(struct vfio_pci_core_device *vdev);

int vfio_pci_set_irqs_ioctl(struct vfio_pci_core_device *vdev, uint32_t flags,
			    unsigned index, unsigned start, unsigned count,
			    void *data);

ssize_t vfio_pci_config_rw(struct vfio_pci_core_device *vdev, char __user *buf,
			   size_t count, loff_t *ppos, bool iswrite);

ssize_t vfio_pci_bar_rw(struct vfio_pci_core_device *vdev, char __user *buf,
			size_t count, loff_t *ppos, bool iswrite);

#ifdef CONFIG_VFIO_PCI_VGA
ssize_t vfio_pci_vga_rw(struct vfio_pci_core_device *vdev, char __user *buf,
			size_t count, loff_t *ppos, bool iswrite);
#else
static inline ssize_t vfio_pci_vga_rw(struct vfio_pci_core_device *vdev,
				      char __user *buf, size_t count,
				      loff_t *ppos, bool iswrite)
{
	return -EINVAL;
}
#endif

int vfio_pci_ioeventfd(struct vfio_pci_core_device *vdev, loff_t offset,
		       uint64_t data, int count, int fd);

int vfio_pci_init_perm_bits(void);
void vfio_pci_uninit_perm_bits(void);

int vfio_config_init(struct vfio_pci_core_device *vdev);
void vfio_config_free(struct vfio_pci_core_device *vdev);

int vfio_pci_set_power_state(struct vfio_pci_core_device *vdev,
			     pci_power_t state);

bool __vfio_pci_memory_enabled(struct vfio_pci_core_device *vdev);
void vfio_pci_zap_and_down_write_memory_lock(struct vfio_pci_core_device *vdev);
u16 vfio_pci_memory_lock_and_enable(struct vfio_pci_core_device *vdev);
void vfio_pci_memory_unlock_and_restore(struct vfio_pci_core_device *vdev,
					u16 cmd);

#ifdef CONFIG_VFIO_PCI_IGD
int vfio_pci_igd_init(struct vfio_pci_core_device *vdev);
#else
static inline int vfio_pci_igd_init(struct vfio_pci_core_device *vdev)
{
	return -ENODEV;
}
#endif

#ifdef CONFIG_VFIO_PCI_ZDEV_KVM
int vfio_pci_info_zdev_add_caps(struct vfio_pci_core_device *vdev,
				struct vfio_info_cap *caps);
int vfio_pci_zdev_open_device(struct vfio_pci_core_device *vdev);
void vfio_pci_zdev_close_device(struct vfio_pci_core_device *vdev);
#else
static inline int vfio_pci_info_zdev_add_caps(struct vfio_pci_core_device *vdev,
					      struct vfio_info_cap *caps)
{
	return -ENODEV;
}

static inline int vfio_pci_zdev_open_device(struct vfio_pci_core_device *vdev)
{
	return 0;
}

static inline void vfio_pci_zdev_close_device(struct vfio_pci_core_device *vdev)
{}
#endif

static inline bool vfio_pci_is_vga(struct pci_dev *pdev)
{
	return (pdev->class >> 8) == PCI_CLASS_DISPLAY_VGA;
}

#endif

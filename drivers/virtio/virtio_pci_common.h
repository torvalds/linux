#ifndef _DRIVERS_VIRTIO_VIRTIO_PCI_COMMON_H
#define _DRIVERS_VIRTIO_VIRTIO_PCI_COMMON_H
/*
 * Virtio PCI driver - APIs for common functionality for all device versions
 *
 * This module allows virtio devices to be used over a virtual PCI device.
 * This can be used with QEMU based VMMs like KVM or Xen.
 *
 * Copyright IBM Corp. 2007
 * Copyright Red Hat, Inc. 2014
 *
 * Authors:
 *  Anthony Liguori  <aliguori@us.ibm.com>
 *  Rusty Russell <rusty@rustcorp.com.au>
 *  Michael S. Tsirkin <mst@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include <linux/module.h>
#include <linux/list.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/virtio.h>
#include <linux/virtio_config.h>
#include <linux/virtio_ring.h>
#include <linux/virtio_pci.h>
#include <linux/highmem.h>
#include <linux/spinlock.h>

struct virtio_pci_vq_info {
	/* the actual virtqueue */
	struct virtqueue *vq;

	/* the number of entries in the queue */
	int num;

	/* the virtual address of the ring queue */
	void *queue;

	/* the list node for the virtqueues list */
	struct list_head node;

	/* MSI-X vector (or none) */
	unsigned msix_vector;
};

/* Our device structure */
struct virtio_pci_device {
	struct virtio_device vdev;
	struct pci_dev *pci_dev;

	/* the IO mapping for the PCI config space */
	void __iomem *ioaddr;

	/* the IO mapping for ISR operation */
	void __iomem *isr;

	/* a list of queues so we can dispatch IRQs */
	spinlock_t lock;
	struct list_head virtqueues;

	/* array of all queues for house-keeping */
	struct virtio_pci_vq_info **vqs;

	/* MSI-X support */
	int msix_enabled;
	int intx_enabled;
	struct msix_entry *msix_entries;
	cpumask_var_t *msix_affinity_masks;
	/* Name strings for interrupts. This size should be enough,
	 * and I'm too lazy to allocate each name separately. */
	char (*msix_names)[256];
	/* Number of available vectors */
	unsigned msix_vectors;
	/* Vectors allocated, excluding per-vq vectors if any */
	unsigned msix_used_vectors;

	/* Whether we have vector per vq */
	bool per_vq_vectors;

	struct virtqueue *(*setup_vq)(struct virtio_pci_device *vp_dev,
				      struct virtio_pci_vq_info *info,
				      unsigned idx,
				      void (*callback)(struct virtqueue *vq),
				      const char *name,
				      u16 msix_vec);
	void (*del_vq)(struct virtio_pci_vq_info *info);

	u16 (*config_vector)(struct virtio_pci_device *vp_dev, u16 vector);
};

/* Constants for MSI-X */
/* Use first vector for configuration changes, second and the rest for
 * virtqueues Thus, we need at least 2 vectors for MSI. */
enum {
	VP_MSIX_CONFIG_VECTOR = 0,
	VP_MSIX_VQ_VECTOR = 1,
};

/* Convert a generic virtio device to our structure */
static struct virtio_pci_device *to_vp_device(struct virtio_device *vdev)
{
	return container_of(vdev, struct virtio_pci_device, vdev);
}

/* wait for pending irq handlers */
void vp_synchronize_vectors(struct virtio_device *vdev);
/* the notify function used when creating a virt queue */
bool vp_notify(struct virtqueue *vq);
/* the config->del_vqs() implementation */
void vp_del_vqs(struct virtio_device *vdev);
/* the config->find_vqs() implementation */
int vp_find_vqs(struct virtio_device *vdev, unsigned nvqs,
		       struct virtqueue *vqs[],
		       vq_callback_t *callbacks[],
		       const char *names[]);
const char *vp_bus_name(struct virtio_device *vdev);

/* Setup the affinity for a virtqueue:
 * - force the affinity for per vq vector
 * - OR over all affinities for shared MSI
 * - ignore the affinity request if we're using INTX
 */
int vp_set_vq_affinity(struct virtqueue *vq, int cpu);
void virtio_pci_release_dev(struct device *);

int virtio_pci_legacy_probe(struct pci_dev *pci_dev,
			    const struct pci_device_id *id);
void virtio_pci_legacy_remove(struct pci_dev *pci_dev);

#endif

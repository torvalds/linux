/* SPDX-License-Identifier: GPL-2.0-or-later */
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
#include <linux/virtio_pci_legacy.h>
#include <linux/virtio_pci_modern.h>
#include <linux/highmem.h>
#include <linux/spinlock.h>
#include <linux/mutex.h>

struct virtio_pci_vq_info {
	/* the actual virtqueue */
	struct virtqueue *vq;

	/* the list node for the virtqueues list */
	struct list_head node;

	/* MSI-X vector (or none) */
	unsigned int msix_vector;
};

struct virtio_pci_admin_vq {
	/* Virtqueue info associated with this admin queue. */
	struct virtio_pci_vq_info info;
	/* serializing admin commands execution and virtqueue deletion */
	struct mutex cmd_lock;
	u64 supported_cmds;
	/* Name of the admin queue: avq.$vq_index. */
	char name[10];
	u16 vq_index;
};

/* Our device structure */
struct virtio_pci_device {
	struct virtio_device vdev;
	struct pci_dev *pci_dev;
	union {
		struct virtio_pci_legacy_device ldev;
		struct virtio_pci_modern_device mdev;
	};
	bool is_legacy;

	/* Where to read and clear interrupt */
	u8 __iomem *isr;

	/* a list of queues so we can dispatch IRQs */
	spinlock_t lock;
	struct list_head virtqueues;

	/* Array of all virtqueues reported in the
	 * PCI common config num_queues field
	 */
	struct virtio_pci_vq_info **vqs;

	struct virtio_pci_admin_vq admin_vq;

	/* MSI-X support */
	int msix_enabled;
	int intx_enabled;
	cpumask_var_t *msix_affinity_masks;
	/* Name strings for interrupts. This size should be enough,
	 * and I'm too lazy to allocate each name separately. */
	char (*msix_names)[256];
	/* Number of available vectors */
	unsigned int msix_vectors;
	/* Vectors allocated, excluding per-vq vectors if any */
	unsigned int msix_used_vectors;

	/* Whether we have vector per vq */
	bool per_vq_vectors;

	struct virtqueue *(*setup_vq)(struct virtio_pci_device *vp_dev,
				      struct virtio_pci_vq_info *info,
				      unsigned int idx,
				      void (*callback)(struct virtqueue *vq),
				      const char *name,
				      bool ctx,
				      u16 msix_vec);
	void (*del_vq)(struct virtio_pci_vq_info *info);

	u16 (*config_vector)(struct virtio_pci_device *vp_dev, u16 vector);
	bool (*is_avq)(struct virtio_device *vdev, unsigned int index);
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
int vp_find_vqs(struct virtio_device *vdev, unsigned int nvqs,
		struct virtqueue *vqs[], vq_callback_t *callbacks[],
		const char * const names[], const bool *ctx,
		struct irq_affinity *desc);
const char *vp_bus_name(struct virtio_device *vdev);

/* Setup the affinity for a virtqueue:
 * - force the affinity for per vq vector
 * - OR over all affinities for shared MSI
 * - ignore the affinity request if we're using INTX
 */
int vp_set_vq_affinity(struct virtqueue *vq, const struct cpumask *cpu_mask);

const struct cpumask *vp_get_vq_affinity(struct virtio_device *vdev, int index);

#if IS_ENABLED(CONFIG_VIRTIO_PCI_LEGACY)
int virtio_pci_legacy_probe(struct virtio_pci_device *);
void virtio_pci_legacy_remove(struct virtio_pci_device *);
#else
static inline int virtio_pci_legacy_probe(struct virtio_pci_device *vp_dev)
{
	return -ENODEV;
}
static inline void virtio_pci_legacy_remove(struct virtio_pci_device *vp_dev)
{
}
#endif
int virtio_pci_modern_probe(struct virtio_pci_device *);
void virtio_pci_modern_remove(struct virtio_pci_device *);

struct virtio_device *virtio_pci_vf_get_pf_dev(struct pci_dev *pdev);

#define VIRTIO_LEGACY_ADMIN_CMD_BITMAP \
	(BIT_ULL(VIRTIO_ADMIN_CMD_LEGACY_COMMON_CFG_WRITE) | \
	 BIT_ULL(VIRTIO_ADMIN_CMD_LEGACY_COMMON_CFG_READ) | \
	 BIT_ULL(VIRTIO_ADMIN_CMD_LEGACY_DEV_CFG_WRITE) | \
	 BIT_ULL(VIRTIO_ADMIN_CMD_LEGACY_DEV_CFG_READ) | \
	 BIT_ULL(VIRTIO_ADMIN_CMD_LEGACY_NOTIFY_INFO))

/* Unlike modern drivers which support hardware virtio devices, legacy drivers
 * assume software-based devices: e.g. they don't use proper memory barriers
 * on ARM, use big endian on PPC, etc. X86 drivers are mostly ok though, more
 * or less by chance. For now, only support legacy IO on X86.
 */
#ifdef CONFIG_VIRTIO_PCI_ADMIN_LEGACY
#define VIRTIO_ADMIN_CMD_BITMAP VIRTIO_LEGACY_ADMIN_CMD_BITMAP
#else
#define VIRTIO_ADMIN_CMD_BITMAP 0
#endif

int vp_modern_admin_cmd_exec(struct virtio_device *vdev,
			     struct virtio_admin_cmd *cmd);

#endif

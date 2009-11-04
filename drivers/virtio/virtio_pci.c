/*
 * Virtio PCI driver
 *
 * This module allows virtio devices to be used over a virtual PCI device.
 * This can be used with QEMU based VMMs like KVM or Xen.
 *
 * Copyright IBM Corp. 2007
 *
 * Authors:
 *  Anthony Liguori  <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include <linux/module.h>
#include <linux/list.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/virtio.h>
#include <linux/virtio_config.h>
#include <linux/virtio_ring.h>
#include <linux/virtio_pci.h>
#include <linux/highmem.h>
#include <linux/spinlock.h>

MODULE_AUTHOR("Anthony Liguori <aliguori@us.ibm.com>");
MODULE_DESCRIPTION("virtio-pci");
MODULE_LICENSE("GPL");
MODULE_VERSION("1");

/* Our device structure */
struct virtio_pci_device
{
	struct virtio_device vdev;
	struct pci_dev *pci_dev;

	/* the IO mapping for the PCI config space */
	void __iomem *ioaddr;

	/* a list of queues so we can dispatch IRQs */
	spinlock_t lock;
	struct list_head virtqueues;

	/* MSI-X support */
	int msix_enabled;
	int intx_enabled;
	struct msix_entry *msix_entries;
	/* Name strings for interrupts. This size should be enough,
	 * and I'm too lazy to allocate each name separately. */
	char (*msix_names)[256];
	/* Number of available vectors */
	unsigned msix_vectors;
	/* Vectors allocated, excluding per-vq vectors if any */
	unsigned msix_used_vectors;
	/* Whether we have vector per vq */
	bool per_vq_vectors;
};

/* Constants for MSI-X */
/* Use first vector for configuration changes, second and the rest for
 * virtqueues Thus, we need at least 2 vectors for MSI. */
enum {
	VP_MSIX_CONFIG_VECTOR = 0,
	VP_MSIX_VQ_VECTOR = 1,
};

struct virtio_pci_vq_info
{
	/* the actual virtqueue */
	struct virtqueue *vq;

	/* the number of entries in the queue */
	int num;

	/* the index of the queue */
	int queue_index;

	/* the virtual address of the ring queue */
	void *queue;

	/* the list node for the virtqueues list */
	struct list_head node;

	/* MSI-X vector (or none) */
	unsigned msix_vector;
};

/* Qumranet donated their vendor ID for devices 0x1000 thru 0x10FF. */
static struct pci_device_id virtio_pci_id_table[] = {
	{ 0x1af4, PCI_ANY_ID, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
	{ 0 },
};

MODULE_DEVICE_TABLE(pci, virtio_pci_id_table);

/* A PCI device has it's own struct device and so does a virtio device so
 * we create a place for the virtio devices to show up in sysfs.  I think it
 * would make more sense for virtio to not insist on having it's own device. */
static struct device *virtio_pci_root;

/* Convert a generic virtio device to our structure */
static struct virtio_pci_device *to_vp_device(struct virtio_device *vdev)
{
	return container_of(vdev, struct virtio_pci_device, vdev);
}

/* virtio config->get_features() implementation */
static u32 vp_get_features(struct virtio_device *vdev)
{
	struct virtio_pci_device *vp_dev = to_vp_device(vdev);

	/* When someone needs more than 32 feature bits, we'll need to
	 * steal a bit to indicate that the rest are somewhere else. */
	return ioread32(vp_dev->ioaddr + VIRTIO_PCI_HOST_FEATURES);
}

/* virtio config->finalize_features() implementation */
static void vp_finalize_features(struct virtio_device *vdev)
{
	struct virtio_pci_device *vp_dev = to_vp_device(vdev);

	/* Give virtio_ring a chance to accept features. */
	vring_transport_features(vdev);

	/* We only support 32 feature bits. */
	BUILD_BUG_ON(ARRAY_SIZE(vdev->features) != 1);
	iowrite32(vdev->features[0], vp_dev->ioaddr+VIRTIO_PCI_GUEST_FEATURES);
}

/* virtio config->get() implementation */
static void vp_get(struct virtio_device *vdev, unsigned offset,
		   void *buf, unsigned len)
{
	struct virtio_pci_device *vp_dev = to_vp_device(vdev);
	void __iomem *ioaddr = vp_dev->ioaddr +
				VIRTIO_PCI_CONFIG(vp_dev) + offset;
	u8 *ptr = buf;
	int i;

	for (i = 0; i < len; i++)
		ptr[i] = ioread8(ioaddr + i);
}

/* the config->set() implementation.  it's symmetric to the config->get()
 * implementation */
static void vp_set(struct virtio_device *vdev, unsigned offset,
		   const void *buf, unsigned len)
{
	struct virtio_pci_device *vp_dev = to_vp_device(vdev);
	void __iomem *ioaddr = vp_dev->ioaddr +
				VIRTIO_PCI_CONFIG(vp_dev) + offset;
	const u8 *ptr = buf;
	int i;

	for (i = 0; i < len; i++)
		iowrite8(ptr[i], ioaddr + i);
}

/* config->{get,set}_status() implementations */
static u8 vp_get_status(struct virtio_device *vdev)
{
	struct virtio_pci_device *vp_dev = to_vp_device(vdev);
	return ioread8(vp_dev->ioaddr + VIRTIO_PCI_STATUS);
}

static void vp_set_status(struct virtio_device *vdev, u8 status)
{
	struct virtio_pci_device *vp_dev = to_vp_device(vdev);
	/* We should never be setting status to 0. */
	BUG_ON(status == 0);
	iowrite8(status, vp_dev->ioaddr + VIRTIO_PCI_STATUS);
}

static void vp_reset(struct virtio_device *vdev)
{
	struct virtio_pci_device *vp_dev = to_vp_device(vdev);
	/* 0 status means a reset. */
	iowrite8(0, vp_dev->ioaddr + VIRTIO_PCI_STATUS);
}

/* the notify function used when creating a virt queue */
static void vp_notify(struct virtqueue *vq)
{
	struct virtio_pci_device *vp_dev = to_vp_device(vq->vdev);
	struct virtio_pci_vq_info *info = vq->priv;

	/* we write the queue's selector into the notification register to
	 * signal the other end */
	iowrite16(info->queue_index, vp_dev->ioaddr + VIRTIO_PCI_QUEUE_NOTIFY);
}

/* Handle a configuration change: Tell driver if it wants to know. */
static irqreturn_t vp_config_changed(int irq, void *opaque)
{
	struct virtio_pci_device *vp_dev = opaque;
	struct virtio_driver *drv;
	drv = container_of(vp_dev->vdev.dev.driver,
			   struct virtio_driver, driver);

	if (drv && drv->config_changed)
		drv->config_changed(&vp_dev->vdev);
	return IRQ_HANDLED;
}

/* Notify all virtqueues on an interrupt. */
static irqreturn_t vp_vring_interrupt(int irq, void *opaque)
{
	struct virtio_pci_device *vp_dev = opaque;
	struct virtio_pci_vq_info *info;
	irqreturn_t ret = IRQ_NONE;
	unsigned long flags;

	spin_lock_irqsave(&vp_dev->lock, flags);
	list_for_each_entry(info, &vp_dev->virtqueues, node) {
		if (vring_interrupt(irq, info->vq) == IRQ_HANDLED)
			ret = IRQ_HANDLED;
	}
	spin_unlock_irqrestore(&vp_dev->lock, flags);

	return ret;
}

/* A small wrapper to also acknowledge the interrupt when it's handled.
 * I really need an EIO hook for the vring so I can ack the interrupt once we
 * know that we'll be handling the IRQ but before we invoke the callback since
 * the callback may notify the host which results in the host attempting to
 * raise an interrupt that we would then mask once we acknowledged the
 * interrupt. */
static irqreturn_t vp_interrupt(int irq, void *opaque)
{
	struct virtio_pci_device *vp_dev = opaque;
	u8 isr;

	/* reading the ISR has the effect of also clearing it so it's very
	 * important to save off the value. */
	isr = ioread8(vp_dev->ioaddr + VIRTIO_PCI_ISR);

	/* It's definitely not us if the ISR was not high */
	if (!isr)
		return IRQ_NONE;

	/* Configuration change?  Tell driver if it wants to know. */
	if (isr & VIRTIO_PCI_ISR_CONFIG)
		vp_config_changed(irq, opaque);

	return vp_vring_interrupt(irq, opaque);
}

static void vp_free_vectors(struct virtio_device *vdev)
{
	struct virtio_pci_device *vp_dev = to_vp_device(vdev);
	int i;

	if (vp_dev->intx_enabled) {
		free_irq(vp_dev->pci_dev->irq, vp_dev);
		vp_dev->intx_enabled = 0;
	}

	for (i = 0; i < vp_dev->msix_used_vectors; ++i)
		free_irq(vp_dev->msix_entries[i].vector, vp_dev);

	if (vp_dev->msix_enabled) {
		/* Disable the vector used for configuration */
		iowrite16(VIRTIO_MSI_NO_VECTOR,
			  vp_dev->ioaddr + VIRTIO_MSI_CONFIG_VECTOR);
		/* Flush the write out to device */
		ioread16(vp_dev->ioaddr + VIRTIO_MSI_CONFIG_VECTOR);

		pci_disable_msix(vp_dev->pci_dev);
		vp_dev->msix_enabled = 0;
		vp_dev->msix_vectors = 0;
	}

	vp_dev->msix_used_vectors = 0;
	kfree(vp_dev->msix_names);
	vp_dev->msix_names = NULL;
	kfree(vp_dev->msix_entries);
	vp_dev->msix_entries = NULL;
}

static int vp_request_msix_vectors(struct virtio_device *vdev, int nvectors,
				   bool per_vq_vectors)
{
	struct virtio_pci_device *vp_dev = to_vp_device(vdev);
	const char *name = dev_name(&vp_dev->vdev.dev);
	unsigned i, v;
	int err = -ENOMEM;

	vp_dev->msix_entries = kmalloc(nvectors * sizeof *vp_dev->msix_entries,
				       GFP_KERNEL);
	if (!vp_dev->msix_entries)
		goto error;
	vp_dev->msix_names = kmalloc(nvectors * sizeof *vp_dev->msix_names,
				     GFP_KERNEL);
	if (!vp_dev->msix_names)
		goto error;

	for (i = 0; i < nvectors; ++i)
		vp_dev->msix_entries[i].entry = i;

	/* pci_enable_msix returns positive if we can't get this many. */
	err = pci_enable_msix(vp_dev->pci_dev, vp_dev->msix_entries, nvectors);
	if (err > 0)
		err = -ENOSPC;
	if (err)
		goto error;
	vp_dev->msix_vectors = nvectors;
	vp_dev->msix_enabled = 1;

	/* Set the vector used for configuration */
	v = vp_dev->msix_used_vectors;
	snprintf(vp_dev->msix_names[v], sizeof *vp_dev->msix_names,
		 "%s-config", name);
	err = request_irq(vp_dev->msix_entries[v].vector,
			  vp_config_changed, 0, vp_dev->msix_names[v],
			  vp_dev);
	if (err)
		goto error;
	++vp_dev->msix_used_vectors;

	iowrite16(v, vp_dev->ioaddr + VIRTIO_MSI_CONFIG_VECTOR);
	/* Verify we had enough resources to assign the vector */
	v = ioread16(vp_dev->ioaddr + VIRTIO_MSI_CONFIG_VECTOR);
	if (v == VIRTIO_MSI_NO_VECTOR) {
		err = -EBUSY;
		goto error;
	}

	if (!per_vq_vectors) {
		/* Shared vector for all VQs */
		v = vp_dev->msix_used_vectors;
		snprintf(vp_dev->msix_names[v], sizeof *vp_dev->msix_names,
			 "%s-virtqueues", name);
		err = request_irq(vp_dev->msix_entries[v].vector,
				  vp_vring_interrupt, 0, vp_dev->msix_names[v],
				  vp_dev);
		if (err)
			goto error;
		++vp_dev->msix_used_vectors;
	}
	return 0;
error:
	vp_free_vectors(vdev);
	return err;
}

static int vp_request_intx(struct virtio_device *vdev)
{
	int err;
	struct virtio_pci_device *vp_dev = to_vp_device(vdev);

	err = request_irq(vp_dev->pci_dev->irq, vp_interrupt,
			  IRQF_SHARED, dev_name(&vdev->dev), vp_dev);
	if (!err)
		vp_dev->intx_enabled = 1;
	return err;
}

static struct virtqueue *setup_vq(struct virtio_device *vdev, unsigned index,
				  void (*callback)(struct virtqueue *vq),
				  const char *name,
				  u16 msix_vec)
{
	struct virtio_pci_device *vp_dev = to_vp_device(vdev);
	struct virtio_pci_vq_info *info;
	struct virtqueue *vq;
	unsigned long flags, size;
	u16 num;
	int err;

	/* Select the queue we're interested in */
	iowrite16(index, vp_dev->ioaddr + VIRTIO_PCI_QUEUE_SEL);

	/* Check if queue is either not available or already active. */
	num = ioread16(vp_dev->ioaddr + VIRTIO_PCI_QUEUE_NUM);
	if (!num || ioread32(vp_dev->ioaddr + VIRTIO_PCI_QUEUE_PFN))
		return ERR_PTR(-ENOENT);

	/* allocate and fill out our structure the represents an active
	 * queue */
	info = kmalloc(sizeof(struct virtio_pci_vq_info), GFP_KERNEL);
	if (!info)
		return ERR_PTR(-ENOMEM);

	info->queue_index = index;
	info->num = num;
	info->msix_vector = msix_vec;

	size = PAGE_ALIGN(vring_size(num, VIRTIO_PCI_VRING_ALIGN));
	info->queue = alloc_pages_exact(size, GFP_KERNEL|__GFP_ZERO);
	if (info->queue == NULL) {
		err = -ENOMEM;
		goto out_info;
	}

	/* activate the queue */
	iowrite32(virt_to_phys(info->queue) >> VIRTIO_PCI_QUEUE_ADDR_SHIFT,
		  vp_dev->ioaddr + VIRTIO_PCI_QUEUE_PFN);

	/* create the vring */
	vq = vring_new_virtqueue(info->num, VIRTIO_PCI_VRING_ALIGN,
				 vdev, info->queue, vp_notify, callback, name);
	if (!vq) {
		err = -ENOMEM;
		goto out_activate_queue;
	}

	vq->priv = info;
	info->vq = vq;

	if (msix_vec != VIRTIO_MSI_NO_VECTOR) {
		iowrite16(msix_vec, vp_dev->ioaddr + VIRTIO_MSI_QUEUE_VECTOR);
		msix_vec = ioread16(vp_dev->ioaddr + VIRTIO_MSI_QUEUE_VECTOR);
		if (msix_vec == VIRTIO_MSI_NO_VECTOR) {
			err = -EBUSY;
			goto out_assign;
		}
	}

	spin_lock_irqsave(&vp_dev->lock, flags);
	list_add(&info->node, &vp_dev->virtqueues);
	spin_unlock_irqrestore(&vp_dev->lock, flags);

	return vq;

out_assign:
	vring_del_virtqueue(vq);
out_activate_queue:
	iowrite32(0, vp_dev->ioaddr + VIRTIO_PCI_QUEUE_PFN);
	free_pages_exact(info->queue, size);
out_info:
	kfree(info);
	return ERR_PTR(err);
}

static void vp_del_vq(struct virtqueue *vq)
{
	struct virtio_pci_device *vp_dev = to_vp_device(vq->vdev);
	struct virtio_pci_vq_info *info = vq->priv;
	unsigned long flags, size;

	spin_lock_irqsave(&vp_dev->lock, flags);
	list_del(&info->node);
	spin_unlock_irqrestore(&vp_dev->lock, flags);

	iowrite16(info->queue_index, vp_dev->ioaddr + VIRTIO_PCI_QUEUE_SEL);

	if (vp_dev->msix_enabled) {
		iowrite16(VIRTIO_MSI_NO_VECTOR,
			  vp_dev->ioaddr + VIRTIO_MSI_QUEUE_VECTOR);
		/* Flush the write out to device */
		ioread8(vp_dev->ioaddr + VIRTIO_PCI_ISR);
	}

	vring_del_virtqueue(vq);

	/* Select and deactivate the queue */
	iowrite32(0, vp_dev->ioaddr + VIRTIO_PCI_QUEUE_PFN);

	size = PAGE_ALIGN(vring_size(info->num, VIRTIO_PCI_VRING_ALIGN));
	free_pages_exact(info->queue, size);
	kfree(info);
}

/* the config->del_vqs() implementation */
static void vp_del_vqs(struct virtio_device *vdev)
{
	struct virtio_pci_device *vp_dev = to_vp_device(vdev);
	struct virtqueue *vq, *n;
	struct virtio_pci_vq_info *info;

	list_for_each_entry_safe(vq, n, &vdev->vqs, list) {
		info = vq->priv;
		if (vp_dev->per_vq_vectors)
			free_irq(vp_dev->msix_entries[info->msix_vector].vector,
				 vq);
		vp_del_vq(vq);
	}
	vp_dev->per_vq_vectors = false;

	vp_free_vectors(vdev);
}

static int vp_try_to_find_vqs(struct virtio_device *vdev, unsigned nvqs,
			      struct virtqueue *vqs[],
			      vq_callback_t *callbacks[],
			      const char *names[],
			      bool use_msix,
			      bool per_vq_vectors)
{
	struct virtio_pci_device *vp_dev = to_vp_device(vdev);
	u16 msix_vec;
	int i, err, nvectors, allocated_vectors;

	if (!use_msix) {
		/* Old style: one normal interrupt for change and all vqs. */
		err = vp_request_intx(vdev);
		if (err)
			goto error_request;
	} else {
		if (per_vq_vectors) {
			/* Best option: one for change interrupt, one per vq. */
			nvectors = 1;
			for (i = 0; i < nvqs; ++i)
				if (callbacks[i])
					++nvectors;
		} else {
			/* Second best: one for change, shared for all vqs. */
			nvectors = 2;
		}

		err = vp_request_msix_vectors(vdev, nvectors, per_vq_vectors);
		if (err)
			goto error_request;
	}

	vp_dev->per_vq_vectors = per_vq_vectors;
	allocated_vectors = vp_dev->msix_used_vectors;
	for (i = 0; i < nvqs; ++i) {
		if (!callbacks[i] || !vp_dev->msix_enabled)
			msix_vec = VIRTIO_MSI_NO_VECTOR;
		else if (vp_dev->per_vq_vectors)
			msix_vec = allocated_vectors++;
		else
			msix_vec = VP_MSIX_VQ_VECTOR;
		vqs[i] = setup_vq(vdev, i, callbacks[i], names[i], msix_vec);
		if (IS_ERR(vqs[i])) {
			err = PTR_ERR(vqs[i]);
			goto error_find;
		}

		if (!vp_dev->per_vq_vectors || msix_vec == VIRTIO_MSI_NO_VECTOR)
			continue;

		/* allocate per-vq irq if available and necessary */
		snprintf(vp_dev->msix_names[msix_vec],
			 sizeof *vp_dev->msix_names,
			 "%s-%s",
			 dev_name(&vp_dev->vdev.dev), names[i]);
		err = request_irq(vp_dev->msix_entries[msix_vec].vector,
				  vring_interrupt, 0,
				  vp_dev->msix_names[msix_vec],
				  vqs[i]);
		if (err) {
			vp_del_vq(vqs[i]);
			goto error_find;
		}
	}
	return 0;

error_find:
	vp_del_vqs(vdev);

error_request:
	return err;
}

/* the config->find_vqs() implementation */
static int vp_find_vqs(struct virtio_device *vdev, unsigned nvqs,
		       struct virtqueue *vqs[],
		       vq_callback_t *callbacks[],
		       const char *names[])
{
	int err;

	/* Try MSI-X with one vector per queue. */
	err = vp_try_to_find_vqs(vdev, nvqs, vqs, callbacks, names, true, true);
	if (!err)
		return 0;
	/* Fallback: MSI-X with one vector for config, one shared for queues. */
	err = vp_try_to_find_vqs(vdev, nvqs, vqs, callbacks, names,
				 true, false);
	if (!err)
		return 0;
	/* Finally fall back to regular interrupts. */
	return vp_try_to_find_vqs(vdev, nvqs, vqs, callbacks, names,
				  false, false);
}

static struct virtio_config_ops virtio_pci_config_ops = {
	.get		= vp_get,
	.set		= vp_set,
	.get_status	= vp_get_status,
	.set_status	= vp_set_status,
	.reset		= vp_reset,
	.find_vqs	= vp_find_vqs,
	.del_vqs	= vp_del_vqs,
	.get_features	= vp_get_features,
	.finalize_features = vp_finalize_features,
};

static void virtio_pci_release_dev(struct device *_d)
{
	struct virtio_device *dev = container_of(_d, struct virtio_device, dev);
	struct virtio_pci_device *vp_dev = to_vp_device(dev);
	struct pci_dev *pci_dev = vp_dev->pci_dev;

	vp_del_vqs(dev);
	pci_set_drvdata(pci_dev, NULL);
	pci_iounmap(pci_dev, vp_dev->ioaddr);
	pci_release_regions(pci_dev);
	pci_disable_device(pci_dev);
	kfree(vp_dev);
}

/* the PCI probing function */
static int __devinit virtio_pci_probe(struct pci_dev *pci_dev,
				      const struct pci_device_id *id)
{
	struct virtio_pci_device *vp_dev;
	int err;

	/* We only own devices >= 0x1000 and <= 0x103f: leave the rest. */
	if (pci_dev->device < 0x1000 || pci_dev->device > 0x103f)
		return -ENODEV;

	if (pci_dev->revision != VIRTIO_PCI_ABI_VERSION) {
		printk(KERN_ERR "virtio_pci: expected ABI version %d, got %d\n",
		       VIRTIO_PCI_ABI_VERSION, pci_dev->revision);
		return -ENODEV;
	}

	/* allocate our structure and fill it out */
	vp_dev = kzalloc(sizeof(struct virtio_pci_device), GFP_KERNEL);
	if (vp_dev == NULL)
		return -ENOMEM;

	vp_dev->vdev.dev.parent = virtio_pci_root;
	vp_dev->vdev.dev.release = virtio_pci_release_dev;
	vp_dev->vdev.config = &virtio_pci_config_ops;
	vp_dev->pci_dev = pci_dev;
	INIT_LIST_HEAD(&vp_dev->virtqueues);
	spin_lock_init(&vp_dev->lock);

	/* enable the device */
	err = pci_enable_device(pci_dev);
	if (err)
		goto out;

	err = pci_request_regions(pci_dev, "virtio-pci");
	if (err)
		goto out_enable_device;

	vp_dev->ioaddr = pci_iomap(pci_dev, 0, 0);
	if (vp_dev->ioaddr == NULL)
		goto out_req_regions;

	pci_set_drvdata(pci_dev, vp_dev);

	/* we use the subsystem vendor/device id as the virtio vendor/device
	 * id.  this allows us to use the same PCI vendor/device id for all
	 * virtio devices and to identify the particular virtio driver by
	 * the subsytem ids */
	vp_dev->vdev.id.vendor = pci_dev->subsystem_vendor;
	vp_dev->vdev.id.device = pci_dev->subsystem_device;

	/* finally register the virtio device */
	err = register_virtio_device(&vp_dev->vdev);
	if (err)
		goto out_set_drvdata;

	return 0;

out_set_drvdata:
	pci_set_drvdata(pci_dev, NULL);
	pci_iounmap(pci_dev, vp_dev->ioaddr);
out_req_regions:
	pci_release_regions(pci_dev);
out_enable_device:
	pci_disable_device(pci_dev);
out:
	kfree(vp_dev);
	return err;
}

static void __devexit virtio_pci_remove(struct pci_dev *pci_dev)
{
	struct virtio_pci_device *vp_dev = pci_get_drvdata(pci_dev);

	unregister_virtio_device(&vp_dev->vdev);
}

#ifdef CONFIG_PM
static int virtio_pci_suspend(struct pci_dev *pci_dev, pm_message_t state)
{
	pci_save_state(pci_dev);
	pci_set_power_state(pci_dev, PCI_D3hot);
	return 0;
}

static int virtio_pci_resume(struct pci_dev *pci_dev)
{
	pci_restore_state(pci_dev);
	pci_set_power_state(pci_dev, PCI_D0);
	return 0;
}
#endif

static struct pci_driver virtio_pci_driver = {
	.name		= "virtio-pci",
	.id_table	= virtio_pci_id_table,
	.probe		= virtio_pci_probe,
	.remove		= virtio_pci_remove,
#ifdef CONFIG_PM
	.suspend	= virtio_pci_suspend,
	.resume		= virtio_pci_resume,
#endif
};

static int __init virtio_pci_init(void)
{
	int err;

	virtio_pci_root = root_device_register("virtio-pci");
	if (IS_ERR(virtio_pci_root))
		return PTR_ERR(virtio_pci_root);

	err = pci_register_driver(&virtio_pci_driver);
	if (err)
		root_device_unregister(virtio_pci_root);

	return err;
}

module_init(virtio_pci_init);

static void __exit virtio_pci_exit(void)
{
	pci_unregister_driver(&virtio_pci_driver);
	root_device_unregister(virtio_pci_root);
}

module_exit(virtio_pci_exit);

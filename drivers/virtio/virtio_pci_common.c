// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Virtio PCI driver - common functionality for all device versions
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

#include "virtio_pci_common.h"

static bool force_legacy = false;

#if IS_ENABLED(CONFIG_VIRTIO_PCI_LEGACY)
module_param(force_legacy, bool, 0444);
MODULE_PARM_DESC(force_legacy,
		 "Force legacy mode for transitional virtio 1 devices");
#endif

/* wait for pending irq handlers */
void vp_synchronize_vectors(struct virtio_device *vdev)
{
	struct virtio_pci_device *vp_dev = to_vp_device(vdev);
	int i;

	if (vp_dev->intx_enabled)
		synchronize_irq(vp_dev->pci_dev->irq);

	for (i = 0; i < vp_dev->msix_vectors; ++i)
		synchronize_irq(pci_irq_vector(vp_dev->pci_dev, i));
}

/* the notify function used when creating a virt queue */
bool vp_notify(struct virtqueue *vq)
{
	/* we write the queue's selector into the notification register to
	 * signal the other end */
	iowrite16(vq->index, (void __iomem *)vq->priv);
	return true;
}

/* Handle a configuration change: Tell driver if it wants to know. */
static irqreturn_t vp_config_changed(int irq, void *opaque)
{
	struct virtio_pci_device *vp_dev = opaque;

	virtio_config_changed(&vp_dev->vdev);
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
	isr = ioread8(vp_dev->isr);

	/* It's definitely not us if the ISR was not high */
	if (!isr)
		return IRQ_NONE;

	/* Configuration change?  Tell driver if it wants to know. */
	if (isr & VIRTIO_PCI_ISR_CONFIG)
		vp_config_changed(irq, opaque);

	return vp_vring_interrupt(irq, opaque);
}

static int vp_request_msix_vectors(struct virtio_device *vdev, int nvectors,
				   bool per_vq_vectors, struct irq_affinity *desc)
{
	struct virtio_pci_device *vp_dev = to_vp_device(vdev);
	const char *name = dev_name(&vp_dev->vdev.dev);
	unsigned int flags = PCI_IRQ_MSIX;
	unsigned int i, v;
	int err = -ENOMEM;

	vp_dev->msix_vectors = nvectors;

	vp_dev->msix_names = kmalloc_array(nvectors,
					   sizeof(*vp_dev->msix_names),
					   GFP_KERNEL);
	if (!vp_dev->msix_names)
		goto error;
	vp_dev->msix_affinity_masks
		= kcalloc(nvectors, sizeof(*vp_dev->msix_affinity_masks),
			  GFP_KERNEL);
	if (!vp_dev->msix_affinity_masks)
		goto error;
	for (i = 0; i < nvectors; ++i)
		if (!alloc_cpumask_var(&vp_dev->msix_affinity_masks[i],
					GFP_KERNEL))
			goto error;

	if (desc) {
		flags |= PCI_IRQ_AFFINITY;
		desc->pre_vectors++; /* virtio config vector */
	}

	err = pci_alloc_irq_vectors_affinity(vp_dev->pci_dev, nvectors,
					     nvectors, flags, desc);
	if (err < 0)
		goto error;
	vp_dev->msix_enabled = 1;

	/* Set the vector used for configuration */
	v = vp_dev->msix_used_vectors;
	snprintf(vp_dev->msix_names[v], sizeof *vp_dev->msix_names,
		 "%s-config", name);
	err = request_irq(pci_irq_vector(vp_dev->pci_dev, v),
			  vp_config_changed, 0, vp_dev->msix_names[v],
			  vp_dev);
	if (err)
		goto error;
	++vp_dev->msix_used_vectors;

	v = vp_dev->config_vector(vp_dev, v);
	/* Verify we had enough resources to assign the vector */
	if (v == VIRTIO_MSI_NO_VECTOR) {
		err = -EBUSY;
		goto error;
	}

	if (!per_vq_vectors) {
		/* Shared vector for all VQs */
		v = vp_dev->msix_used_vectors;
		snprintf(vp_dev->msix_names[v], sizeof *vp_dev->msix_names,
			 "%s-virtqueues", name);
		err = request_irq(pci_irq_vector(vp_dev->pci_dev, v),
				  vp_vring_interrupt, 0, vp_dev->msix_names[v],
				  vp_dev);
		if (err)
			goto error;
		++vp_dev->msix_used_vectors;
	}
	return 0;
error:
	return err;
}

static struct virtqueue *vp_setup_vq(struct virtio_device *vdev, unsigned int index,
				     void (*callback)(struct virtqueue *vq),
				     const char *name,
				     bool ctx,
				     u16 msix_vec)
{
	struct virtio_pci_device *vp_dev = to_vp_device(vdev);
	struct virtio_pci_vq_info *info = kmalloc(sizeof *info, GFP_KERNEL);
	struct virtqueue *vq;
	unsigned long flags;

	/* fill out our structure that represents an active queue */
	if (!info)
		return ERR_PTR(-ENOMEM);

	vq = vp_dev->setup_vq(vp_dev, info, index, callback, name, ctx,
			      msix_vec);
	if (IS_ERR(vq))
		goto out_info;

	info->vq = vq;
	if (callback) {
		spin_lock_irqsave(&vp_dev->lock, flags);
		list_add(&info->node, &vp_dev->virtqueues);
		spin_unlock_irqrestore(&vp_dev->lock, flags);
	} else {
		INIT_LIST_HEAD(&info->node);
	}

	vp_dev->vqs[index] = info;
	return vq;

out_info:
	kfree(info);
	return vq;
}

static void vp_del_vq(struct virtqueue *vq)
{
	struct virtio_pci_device *vp_dev = to_vp_device(vq->vdev);
	struct virtio_pci_vq_info *info = vp_dev->vqs[vq->index];
	unsigned long flags;

	spin_lock_irqsave(&vp_dev->lock, flags);
	list_del(&info->node);
	spin_unlock_irqrestore(&vp_dev->lock, flags);

	vp_dev->del_vq(info);
	kfree(info);
}

/* the config->del_vqs() implementation */
void vp_del_vqs(struct virtio_device *vdev)
{
	struct virtio_pci_device *vp_dev = to_vp_device(vdev);
	struct virtqueue *vq, *n;
	int i;

	list_for_each_entry_safe(vq, n, &vdev->vqs, list) {
		if (vp_dev->per_vq_vectors) {
			int v = vp_dev->vqs[vq->index]->msix_vector;

			if (v != VIRTIO_MSI_NO_VECTOR) {
				int irq = pci_irq_vector(vp_dev->pci_dev, v);

				irq_set_affinity_hint(irq, NULL);
				free_irq(irq, vq);
			}
		}
		vp_del_vq(vq);
	}
	vp_dev->per_vq_vectors = false;

	if (vp_dev->intx_enabled) {
		free_irq(vp_dev->pci_dev->irq, vp_dev);
		vp_dev->intx_enabled = 0;
	}

	for (i = 0; i < vp_dev->msix_used_vectors; ++i)
		free_irq(pci_irq_vector(vp_dev->pci_dev, i), vp_dev);

	if (vp_dev->msix_affinity_masks) {
		for (i = 0; i < vp_dev->msix_vectors; i++)
			free_cpumask_var(vp_dev->msix_affinity_masks[i]);
	}

	if (vp_dev->msix_enabled) {
		/* Disable the vector used for configuration */
		vp_dev->config_vector(vp_dev, VIRTIO_MSI_NO_VECTOR);

		pci_free_irq_vectors(vp_dev->pci_dev);
		vp_dev->msix_enabled = 0;
	}

	vp_dev->msix_vectors = 0;
	vp_dev->msix_used_vectors = 0;
	kfree(vp_dev->msix_names);
	vp_dev->msix_names = NULL;
	kfree(vp_dev->msix_affinity_masks);
	vp_dev->msix_affinity_masks = NULL;
	kfree(vp_dev->vqs);
	vp_dev->vqs = NULL;
}

static int vp_find_vqs_msix(struct virtio_device *vdev, unsigned int nvqs,
		struct virtqueue *vqs[], vq_callback_t *callbacks[],
		const char * const names[], bool per_vq_vectors,
		const bool *ctx,
		struct irq_affinity *desc)
{
	struct virtio_pci_device *vp_dev = to_vp_device(vdev);
	u16 msix_vec;
	int i, err, nvectors, allocated_vectors, queue_idx = 0;

	vp_dev->vqs = kcalloc(nvqs, sizeof(*vp_dev->vqs), GFP_KERNEL);
	if (!vp_dev->vqs)
		return -ENOMEM;

	if (per_vq_vectors) {
		/* Best option: one for change interrupt, one per vq. */
		nvectors = 1;
		for (i = 0; i < nvqs; ++i)
			if (names[i] && callbacks[i])
				++nvectors;
	} else {
		/* Second best: one for change, shared for all vqs. */
		nvectors = 2;
	}

	err = vp_request_msix_vectors(vdev, nvectors, per_vq_vectors,
				      per_vq_vectors ? desc : NULL);
	if (err)
		goto error_find;

	vp_dev->per_vq_vectors = per_vq_vectors;
	allocated_vectors = vp_dev->msix_used_vectors;
	for (i = 0; i < nvqs; ++i) {
		if (!names[i]) {
			vqs[i] = NULL;
			continue;
		}

		if (!callbacks[i])
			msix_vec = VIRTIO_MSI_NO_VECTOR;
		else if (vp_dev->per_vq_vectors)
			msix_vec = allocated_vectors++;
		else
			msix_vec = VP_MSIX_VQ_VECTOR;
		vqs[i] = vp_setup_vq(vdev, queue_idx++, callbacks[i], names[i],
				     ctx ? ctx[i] : false,
				     msix_vec);
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
		err = request_irq(pci_irq_vector(vp_dev->pci_dev, msix_vec),
				  vring_interrupt, 0,
				  vp_dev->msix_names[msix_vec],
				  vqs[i]);
		if (err)
			goto error_find;
	}
	return 0;

error_find:
	vp_del_vqs(vdev);
	return err;
}

static int vp_find_vqs_intx(struct virtio_device *vdev, unsigned int nvqs,
		struct virtqueue *vqs[], vq_callback_t *callbacks[],
		const char * const names[], const bool *ctx)
{
	struct virtio_pci_device *vp_dev = to_vp_device(vdev);
	int i, err, queue_idx = 0;

	vp_dev->vqs = kcalloc(nvqs, sizeof(*vp_dev->vqs), GFP_KERNEL);
	if (!vp_dev->vqs)
		return -ENOMEM;

	err = request_irq(vp_dev->pci_dev->irq, vp_interrupt, IRQF_SHARED,
			dev_name(&vdev->dev), vp_dev);
	if (err)
		goto out_del_vqs;

	vp_dev->intx_enabled = 1;
	vp_dev->per_vq_vectors = false;
	for (i = 0; i < nvqs; ++i) {
		if (!names[i]) {
			vqs[i] = NULL;
			continue;
		}
		vqs[i] = vp_setup_vq(vdev, queue_idx++, callbacks[i], names[i],
				     ctx ? ctx[i] : false,
				     VIRTIO_MSI_NO_VECTOR);
		if (IS_ERR(vqs[i])) {
			err = PTR_ERR(vqs[i]);
			goto out_del_vqs;
		}
	}

	return 0;
out_del_vqs:
	vp_del_vqs(vdev);
	return err;
}

/* the config->find_vqs() implementation */
int vp_find_vqs(struct virtio_device *vdev, unsigned int nvqs,
		struct virtqueue *vqs[], vq_callback_t *callbacks[],
		const char * const names[], const bool *ctx,
		struct irq_affinity *desc)
{
	int err;

	/* Try MSI-X with one vector per queue. */
	err = vp_find_vqs_msix(vdev, nvqs, vqs, callbacks, names, true, ctx, desc);
	if (!err)
		return 0;
	/* Fallback: MSI-X with one vector for config, one shared for queues. */
	err = vp_find_vqs_msix(vdev, nvqs, vqs, callbacks, names, false, ctx, desc);
	if (!err)
		return 0;
	/* Finally fall back to regular interrupts. */
	return vp_find_vqs_intx(vdev, nvqs, vqs, callbacks, names, ctx);
}

const char *vp_bus_name(struct virtio_device *vdev)
{
	struct virtio_pci_device *vp_dev = to_vp_device(vdev);

	return pci_name(vp_dev->pci_dev);
}

/* Setup the affinity for a virtqueue:
 * - force the affinity for per vq vector
 * - OR over all affinities for shared MSI
 * - ignore the affinity request if we're using INTX
 */
int vp_set_vq_affinity(struct virtqueue *vq, const struct cpumask *cpu_mask)
{
	struct virtio_device *vdev = vq->vdev;
	struct virtio_pci_device *vp_dev = to_vp_device(vdev);
	struct virtio_pci_vq_info *info = vp_dev->vqs[vq->index];
	struct cpumask *mask;
	unsigned int irq;

	if (!vq->callback)
		return -EINVAL;

	if (vp_dev->msix_enabled) {
		mask = vp_dev->msix_affinity_masks[info->msix_vector];
		irq = pci_irq_vector(vp_dev->pci_dev, info->msix_vector);
		if (!cpu_mask)
			irq_set_affinity_hint(irq, NULL);
		else {
			cpumask_copy(mask, cpu_mask);
			irq_set_affinity_hint(irq, mask);
		}
	}
	return 0;
}

const struct cpumask *vp_get_vq_affinity(struct virtio_device *vdev, int index)
{
	struct virtio_pci_device *vp_dev = to_vp_device(vdev);

	if (!vp_dev->per_vq_vectors ||
	    vp_dev->vqs[index]->msix_vector == VIRTIO_MSI_NO_VECTOR)
		return NULL;

	return pci_irq_get_affinity(vp_dev->pci_dev,
				    vp_dev->vqs[index]->msix_vector);
}

#ifdef CONFIG_PM_SLEEP
static int virtio_pci_freeze(struct device *dev)
{
	struct pci_dev *pci_dev = to_pci_dev(dev);
	struct virtio_pci_device *vp_dev = pci_get_drvdata(pci_dev);
	int ret;

	ret = virtio_device_freeze(&vp_dev->vdev);

	if (!ret)
		pci_disable_device(pci_dev);
	return ret;
}

static int virtio_pci_restore(struct device *dev)
{
	struct pci_dev *pci_dev = to_pci_dev(dev);
	struct virtio_pci_device *vp_dev = pci_get_drvdata(pci_dev);
	int ret;

	ret = pci_enable_device(pci_dev);
	if (ret)
		return ret;

	pci_set_master(pci_dev);
	return virtio_device_restore(&vp_dev->vdev);
}

static const struct dev_pm_ops virtio_pci_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(virtio_pci_freeze, virtio_pci_restore)
};
#endif


/* Qumranet donated their vendor ID for devices 0x1000 thru 0x10FF. */
static const struct pci_device_id virtio_pci_id_table[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_REDHAT_QUMRANET, PCI_ANY_ID) },
	{ 0 }
};

MODULE_DEVICE_TABLE(pci, virtio_pci_id_table);

static void virtio_pci_release_dev(struct device *_d)
{
	struct virtio_device *vdev = dev_to_virtio(_d);
	struct virtio_pci_device *vp_dev = to_vp_device(vdev);

	/* As struct device is a kobject, it's not safe to
	 * free the memory (including the reference counter itself)
	 * until it's release callback. */
	kfree(vp_dev);
}

static int virtio_pci_probe(struct pci_dev *pci_dev,
			    const struct pci_device_id *id)
{
	struct virtio_pci_device *vp_dev, *reg_dev = NULL;
	int rc;

	/* allocate our structure and fill it out */
	vp_dev = kzalloc(sizeof(struct virtio_pci_device), GFP_KERNEL);
	if (!vp_dev)
		return -ENOMEM;

	pci_set_drvdata(pci_dev, vp_dev);
	vp_dev->vdev.dev.parent = &pci_dev->dev;
	vp_dev->vdev.dev.release = virtio_pci_release_dev;
	vp_dev->pci_dev = pci_dev;
	INIT_LIST_HEAD(&vp_dev->virtqueues);
	spin_lock_init(&vp_dev->lock);

	/* enable the device */
	rc = pci_enable_device(pci_dev);
	if (rc)
		goto err_enable_device;

	if (force_legacy) {
		rc = virtio_pci_legacy_probe(vp_dev);
		/* Also try modern mode if we can't map BAR0 (no IO space). */
		if (rc == -ENODEV || rc == -ENOMEM)
			rc = virtio_pci_modern_probe(vp_dev);
		if (rc)
			goto err_probe;
	} else {
		rc = virtio_pci_modern_probe(vp_dev);
		if (rc == -ENODEV)
			rc = virtio_pci_legacy_probe(vp_dev);
		if (rc)
			goto err_probe;
	}

	pci_set_master(pci_dev);

	vp_dev->is_legacy = vp_dev->ldev.ioaddr ? true : false;

	rc = register_virtio_device(&vp_dev->vdev);
	reg_dev = vp_dev;
	if (rc)
		goto err_register;

	return 0;

err_register:
	if (vp_dev->is_legacy)
		virtio_pci_legacy_remove(vp_dev);
	else
		virtio_pci_modern_remove(vp_dev);
err_probe:
	pci_disable_device(pci_dev);
err_enable_device:
	if (reg_dev)
		put_device(&vp_dev->vdev.dev);
	else
		kfree(vp_dev);
	return rc;
}

static void virtio_pci_remove(struct pci_dev *pci_dev)
{
	struct virtio_pci_device *vp_dev = pci_get_drvdata(pci_dev);
	struct device *dev = get_device(&vp_dev->vdev.dev);

	/*
	 * Device is marked broken on surprise removal so that virtio upper
	 * layers can abort any ongoing operation.
	 */
	if (!pci_device_is_present(pci_dev))
		virtio_break_device(&vp_dev->vdev);

	pci_disable_sriov(pci_dev);

	unregister_virtio_device(&vp_dev->vdev);

	if (vp_dev->is_legacy)
		virtio_pci_legacy_remove(vp_dev);
	else
		virtio_pci_modern_remove(vp_dev);

	pci_disable_device(pci_dev);
	put_device(dev);
}

static int virtio_pci_sriov_configure(struct pci_dev *pci_dev, int num_vfs)
{
	struct virtio_pci_device *vp_dev = pci_get_drvdata(pci_dev);
	struct virtio_device *vdev = &vp_dev->vdev;
	int ret;

	if (!(vdev->config->get_status(vdev) & VIRTIO_CONFIG_S_DRIVER_OK))
		return -EBUSY;

	if (!__virtio_test_bit(vdev, VIRTIO_F_SR_IOV))
		return -EINVAL;

	if (pci_vfs_assigned(pci_dev))
		return -EPERM;

	if (num_vfs == 0) {
		pci_disable_sriov(pci_dev);
		return 0;
	}

	ret = pci_enable_sriov(pci_dev, num_vfs);
	if (ret < 0)
		return ret;

	return num_vfs;
}

static struct pci_driver virtio_pci_driver = {
	.name		= "virtio-pci",
	.id_table	= virtio_pci_id_table,
	.probe		= virtio_pci_probe,
	.remove		= virtio_pci_remove,
#ifdef CONFIG_PM_SLEEP
	.driver.pm	= &virtio_pci_pm_ops,
#endif
	.sriov_configure = virtio_pci_sriov_configure,
};

module_pci_driver(virtio_pci_driver);

MODULE_AUTHOR("Anthony Liguori <aliguori@us.ibm.com>");
MODULE_DESCRIPTION("virtio-pci");
MODULE_LICENSE("GPL");
MODULE_VERSION("1");

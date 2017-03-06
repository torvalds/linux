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
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
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

	synchronize_irq(pci_irq_vector(vp_dev->pci_dev, 0));
	for (i = 1; i < vp_dev->msix_vectors; i++)
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
	irqreturn_t ret = IRQ_NONE;
	struct virtqueue *vq;

	list_for_each_entry(vq, &vp_dev->vdev.vqs, list) {
		if (vq->callback && vring_interrupt(irq, vq) == IRQ_HANDLED)
			ret = IRQ_HANDLED;
	}

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

static void vp_remove_vqs(struct virtio_device *vdev)
{
	struct virtio_pci_device *vp_dev = to_vp_device(vdev);
	struct virtqueue *vq, *n;

	list_for_each_entry_safe(vq, n, &vdev->vqs, list) {
		if (vp_dev->msix_vector_map) {
			int v = vp_dev->msix_vector_map[vq->index];

			if (v != VIRTIO_MSI_NO_VECTOR)
				free_irq(pci_irq_vector(vp_dev->pci_dev, v),
					vq);
		}
		vp_dev->del_vq(vq);
	}
}

/* the config->del_vqs() implementation */
void vp_del_vqs(struct virtio_device *vdev)
{
	struct virtio_pci_device *vp_dev = to_vp_device(vdev);
	int i;

	if (WARN_ON_ONCE(list_empty_careful(&vdev->vqs)))
		return;

	vp_remove_vqs(vdev);

	if (vp_dev->pci_dev->msix_enabled) {
		for (i = 0; i < vp_dev->msix_vectors; i++)
			free_cpumask_var(vp_dev->msix_affinity_masks[i]);

		/* Disable the vector used for configuration */
		vp_dev->config_vector(vp_dev, VIRTIO_MSI_NO_VECTOR);

		kfree(vp_dev->msix_affinity_masks);
		kfree(vp_dev->msix_names);
		kfree(vp_dev->msix_vector_map);
	}

	free_irq(pci_irq_vector(vp_dev->pci_dev, 0), vp_dev);
	pci_free_irq_vectors(vp_dev->pci_dev);
}

static int vp_find_vqs_msix(struct virtio_device *vdev, unsigned nvqs,
		struct virtqueue *vqs[], vq_callback_t *callbacks[],
		const char * const names[], struct irq_affinity *desc)
{
	struct virtio_pci_device *vp_dev = to_vp_device(vdev);
	const char *name = dev_name(&vp_dev->vdev.dev);
	int i, err = -ENOMEM, allocated_vectors, nvectors;
	unsigned flags = PCI_IRQ_MSIX;
	bool shared = false;
	u16 msix_vec;

	if (desc) {
		flags |= PCI_IRQ_AFFINITY;
		desc->pre_vectors++; /* virtio config vector */
	}

	nvectors = 1;
	for (i = 0; i < nvqs; i++)
		if (callbacks[i])
			nvectors++;

	/* Try one vector per queue first. */
	err = pci_alloc_irq_vectors_affinity(vp_dev->pci_dev, nvectors,
			nvectors, flags, desc);
	if (err < 0) {
		/* Fallback to one vector for config, one shared for queues. */
		shared = true;
		err = pci_alloc_irq_vectors(vp_dev->pci_dev, 2, 2,
				PCI_IRQ_MSIX);
		if (err < 0)
			return err;
	}
	if (err < 0)
		return err;

	vp_dev->msix_vectors = nvectors;
	vp_dev->msix_names = kmalloc_array(nvectors,
			sizeof(*vp_dev->msix_names), GFP_KERNEL);
	if (!vp_dev->msix_names)
		goto out_free_irq_vectors;

	vp_dev->msix_affinity_masks = kcalloc(nvectors,
			sizeof(*vp_dev->msix_affinity_masks), GFP_KERNEL);
	if (!vp_dev->msix_affinity_masks)
		goto out_free_msix_names;

	for (i = 0; i < nvectors; ++i) {
		if (!alloc_cpumask_var(&vp_dev->msix_affinity_masks[i],
				GFP_KERNEL))
			goto out_free_msix_affinity_masks;
	}

	/* Set the vector used for configuration */
	snprintf(vp_dev->msix_names[0], sizeof(*vp_dev->msix_names),
		 "%s-config", name);
	err = request_irq(pci_irq_vector(vp_dev->pci_dev, 0), vp_config_changed,
			0, vp_dev->msix_names[0], vp_dev);
	if (err)
		goto out_free_msix_affinity_masks;

	/* Verify we had enough resources to assign the vector */
	if (vp_dev->config_vector(vp_dev, 0) == VIRTIO_MSI_NO_VECTOR) {
		err = -EBUSY;
		goto out_free_config_irq;
	}

	vp_dev->msix_vector_map = kmalloc_array(nvqs,
			sizeof(*vp_dev->msix_vector_map), GFP_KERNEL);
	if (!vp_dev->msix_vector_map)
		goto out_disable_config_irq;

	allocated_vectors = 1; /* vector 0 is the config interrupt */
	for (i = 0; i < nvqs; ++i) {
		if (!names[i]) {
			vqs[i] = NULL;
			continue;
		}

		if (callbacks[i])
			msix_vec = allocated_vectors;
		else
			msix_vec = VIRTIO_MSI_NO_VECTOR;

		vqs[i] = vp_dev->setup_vq(vp_dev, i, callbacks[i], names[i],
				msix_vec);
		if (IS_ERR(vqs[i])) {
			err = PTR_ERR(vqs[i]);
			goto out_remove_vqs;
		}

		if (msix_vec == VIRTIO_MSI_NO_VECTOR) {
			vp_dev->msix_vector_map[i] = VIRTIO_MSI_NO_VECTOR;
			continue;
		}

		snprintf(vp_dev->msix_names[i + 1],
			 sizeof(*vp_dev->msix_names), "%s-%s",
			 dev_name(&vp_dev->vdev.dev), names[i]);
		err = request_irq(pci_irq_vector(vp_dev->pci_dev, msix_vec),
				  vring_interrupt, IRQF_SHARED,
				  vp_dev->msix_names[i + 1], vqs[i]);
		if (err) {
			/* don't free this irq on error */
			vp_dev->msix_vector_map[i] = VIRTIO_MSI_NO_VECTOR;
			goto out_remove_vqs;
		}
		vp_dev->msix_vector_map[i] = msix_vec;

		/*
		 * Use a different vector for each queue if they are available,
		 * else share the same vector for all VQs.
		 */
		if (!shared)
			allocated_vectors++;
	}

	return 0;

out_remove_vqs:
	vp_remove_vqs(vdev);
	kfree(vp_dev->msix_vector_map);
out_disable_config_irq:
	vp_dev->config_vector(vp_dev, VIRTIO_MSI_NO_VECTOR);
out_free_config_irq:
	free_irq(pci_irq_vector(vp_dev->pci_dev, 0), vp_dev);
out_free_msix_affinity_masks:
	for (i = 0; i < nvectors; i++) {
		if (vp_dev->msix_affinity_masks[i])
			free_cpumask_var(vp_dev->msix_affinity_masks[i]);
	}
	kfree(vp_dev->msix_affinity_masks);
out_free_msix_names:
	kfree(vp_dev->msix_names);
out_free_irq_vectors:
	pci_free_irq_vectors(vp_dev->pci_dev);
	return err;
}

static int vp_find_vqs_intx(struct virtio_device *vdev, unsigned nvqs,
		struct virtqueue *vqs[], vq_callback_t *callbacks[],
		const char * const names[])
{
	struct virtio_pci_device *vp_dev = to_vp_device(vdev);
	int i, err;

	err = request_irq(vp_dev->pci_dev->irq, vp_interrupt, IRQF_SHARED,
			dev_name(&vdev->dev), vp_dev);
	if (err)
		return err;

	for (i = 0; i < nvqs; ++i) {
		if (!names[i]) {
			vqs[i] = NULL;
			continue;
		}
		vqs[i] = vp_dev->setup_vq(vp_dev, i, callbacks[i], names[i],
				VIRTIO_MSI_NO_VECTOR);
		if (IS_ERR(vqs[i])) {
			err = PTR_ERR(vqs[i]);
			goto out_remove_vqs;
		}
	}

	return 0;

out_remove_vqs:
	vp_remove_vqs(vdev);
	free_irq(pci_irq_vector(vp_dev->pci_dev, 0), vp_dev);
	return err;
}

/* the config->find_vqs() implementation */
int vp_find_vqs(struct virtio_device *vdev, unsigned nvqs,
		struct virtqueue *vqs[], vq_callback_t *callbacks[],
		const char * const names[], struct irq_affinity *desc)
{
	int err;

	err = vp_find_vqs_msix(vdev, nvqs, vqs, callbacks, names, desc);
	if (!err)
		return 0;
	return vp_find_vqs_intx(vdev, nvqs, vqs, callbacks, names);
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
int vp_set_vq_affinity(struct virtqueue *vq, int cpu)
{
	struct virtio_device *vdev = vq->vdev;
	struct virtio_pci_device *vp_dev = to_vp_device(vdev);

	if (!vq->callback)
		return -EINVAL;

	if (vp_dev->pci_dev->msix_enabled) {
		int vec = vp_dev->msix_vector_map[vq->index];
		struct cpumask *mask = vp_dev->msix_affinity_masks[vec];
		unsigned int irq = pci_irq_vector(vp_dev->pci_dev, vec);

		if (cpu == -1)
			irq_set_affinity_hint(irq, NULL);
		else {
			cpumask_clear(mask);
			cpumask_set_cpu(cpu, mask);
			irq_set_affinity_hint(irq, mask);
		}
	}
	return 0;
}

const struct cpumask *vp_get_vq_affinity(struct virtio_device *vdev, int index)
{
	struct virtio_pci_device *vp_dev = to_vp_device(vdev);
	unsigned int *map = vp_dev->msix_vector_map;

	if (!map || map[index] == VIRTIO_MSI_NO_VECTOR)
		return NULL;

	return pci_irq_get_affinity(vp_dev->pci_dev, map[index]);
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
	struct virtio_pci_device *vp_dev;
	int rc;

	/* allocate our structure and fill it out */
	vp_dev = kzalloc(sizeof(struct virtio_pci_device), GFP_KERNEL);
	if (!vp_dev)
		return -ENOMEM;

	pci_set_drvdata(pci_dev, vp_dev);
	vp_dev->vdev.dev.parent = &pci_dev->dev;
	vp_dev->vdev.dev.release = virtio_pci_release_dev;
	vp_dev->pci_dev = pci_dev;

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

	rc = register_virtio_device(&vp_dev->vdev);
	if (rc)
		goto err_register;

	return 0;

err_register:
	if (vp_dev->ioaddr)
	     virtio_pci_legacy_remove(vp_dev);
	else
	     virtio_pci_modern_remove(vp_dev);
err_probe:
	pci_disable_device(pci_dev);
err_enable_device:
	kfree(vp_dev);
	return rc;
}

static void virtio_pci_remove(struct pci_dev *pci_dev)
{
	struct virtio_pci_device *vp_dev = pci_get_drvdata(pci_dev);
	struct device *dev = get_device(&vp_dev->vdev.dev);

	unregister_virtio_device(&vp_dev->vdev);

	if (vp_dev->ioaddr)
		virtio_pci_legacy_remove(vp_dev);
	else
		virtio_pci_modern_remove(vp_dev);

	pci_disable_device(pci_dev);
	put_device(dev);
}

static struct pci_driver virtio_pci_driver = {
	.name		= "virtio-pci",
	.id_table	= virtio_pci_id_table,
	.probe		= virtio_pci_probe,
	.remove		= virtio_pci_remove,
#ifdef CONFIG_PM_SLEEP
	.driver.pm	= &virtio_pci_pm_ops,
#endif
};

module_pci_driver(virtio_pci_driver);

MODULE_AUTHOR("Anthony Liguori <aliguori@us.ibm.com>");
MODULE_DESCRIPTION("virtio-pci");
MODULE_LICENSE("GPL");
MODULE_VERSION("1");

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
	void *ioaddr;

	/* a list of queues so we can dispatch IRQs */
	spinlock_t lock;
	struct list_head virtqueues;
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
static struct device virtio_pci_root = {
	.parent		= NULL,
	.bus_id		= "virtio-pci",
};

/* Unique numbering for devices under the kvm root */
static unsigned int dev_index;

/* Convert a generic virtio device to our structure */
static struct virtio_pci_device *to_vp_device(struct virtio_device *vdev)
{
	return container_of(vdev, struct virtio_pci_device, vdev);
}

/* virtio config->feature() implementation */
static bool vp_feature(struct virtio_device *vdev, unsigned bit)
{
	struct virtio_pci_device *vp_dev = to_vp_device(vdev);
	u32 mask;

	/* Since this function is supposed to have the side effect of
	 * enabling a queried feature, we simulate that by doing a read
	 * from the host feature bitmask and then writing to the guest
	 * feature bitmask */
	mask = ioread32(vp_dev->ioaddr + VIRTIO_PCI_HOST_FEATURES);
	if (mask & (1 << bit)) {
		mask |= (1 << bit);
		iowrite32(mask, vp_dev->ioaddr + VIRTIO_PCI_GUEST_FEATURES);
	}

	return !!(mask & (1 << bit));
}

/* virtio config->get() implementation */
static void vp_get(struct virtio_device *vdev, unsigned offset,
		   void *buf, unsigned len)
{
	struct virtio_pci_device *vp_dev = to_vp_device(vdev);
	void *ioaddr = vp_dev->ioaddr + VIRTIO_PCI_CONFIG + offset;
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
	void *ioaddr = vp_dev->ioaddr + VIRTIO_PCI_CONFIG + offset;
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
	return iowrite8(status, vp_dev->ioaddr + VIRTIO_PCI_STATUS);
}

static void vp_reset(struct virtio_device *vdev)
{
	struct virtio_pci_device *vp_dev = to_vp_device(vdev);
	/* 0 status means a reset. */
	return iowrite8(0, vp_dev->ioaddr + VIRTIO_PCI_STATUS);
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

/* A small wrapper to also acknowledge the interrupt when it's handled.
 * I really need an EIO hook for the vring so I can ack the interrupt once we
 * know that we'll be handling the IRQ but before we invoke the callback since
 * the callback may notify the host which results in the host attempting to
 * raise an interrupt that we would then mask once we acknowledged the
 * interrupt. */
static irqreturn_t vp_interrupt(int irq, void *opaque)
{
	struct virtio_pci_device *vp_dev = opaque;
	struct virtio_pci_vq_info *info;
	irqreturn_t ret = IRQ_NONE;
	u8 isr;

	/* reading the ISR has the effect of also clearing it so it's very
	 * important to save off the value. */
	isr = ioread8(vp_dev->ioaddr + VIRTIO_PCI_ISR);

	/* It's definitely not us if the ISR was not high */
	if (!isr)
		return IRQ_NONE;

	/* Configuration change?  Tell driver if it wants to know. */
	if (isr & VIRTIO_PCI_ISR_CONFIG) {
		struct virtio_driver *drv;
		drv = container_of(vp_dev->vdev.dev.driver,
				   struct virtio_driver, driver);

		if (drv->config_changed)
			drv->config_changed(&vp_dev->vdev);
	}

	spin_lock(&vp_dev->lock);
	list_for_each_entry(info, &vp_dev->virtqueues, node) {
		if (vring_interrupt(irq, info->vq) == IRQ_HANDLED)
			ret = IRQ_HANDLED;
	}
	spin_unlock(&vp_dev->lock);

	return ret;
}

/* the config->find_vq() implementation */
static struct virtqueue *vp_find_vq(struct virtio_device *vdev, unsigned index,
				    void (*callback)(struct virtqueue *vq))
{
	struct virtio_pci_device *vp_dev = to_vp_device(vdev);
	struct virtio_pci_vq_info *info;
	struct virtqueue *vq;
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

	info->queue = kzalloc(PAGE_ALIGN(vring_size(num,PAGE_SIZE)), GFP_KERNEL);
	if (info->queue == NULL) {
		err = -ENOMEM;
		goto out_info;
	}

	/* activate the queue */
	iowrite32(virt_to_phys(info->queue) >> PAGE_SHIFT,
		  vp_dev->ioaddr + VIRTIO_PCI_QUEUE_PFN);

	/* create the vring */
	vq = vring_new_virtqueue(info->num, vdev, info->queue,
				 vp_notify, callback);
	if (!vq) {
		err = -ENOMEM;
		goto out_activate_queue;
	}

	vq->priv = info;
	info->vq = vq;

	spin_lock(&vp_dev->lock);
	list_add(&info->node, &vp_dev->virtqueues);
	spin_unlock(&vp_dev->lock);

	return vq;

out_activate_queue:
	iowrite32(0, vp_dev->ioaddr + VIRTIO_PCI_QUEUE_PFN);
	kfree(info->queue);
out_info:
	kfree(info);
	return ERR_PTR(err);
}

/* the config->del_vq() implementation */
static void vp_del_vq(struct virtqueue *vq)
{
	struct virtio_pci_device *vp_dev = to_vp_device(vq->vdev);
	struct virtio_pci_vq_info *info = vq->priv;

	spin_lock(&vp_dev->lock);
	list_del(&info->node);
	spin_unlock(&vp_dev->lock);

	vring_del_virtqueue(vq);

	/* Select and deactivate the queue */
	iowrite16(info->queue_index, vp_dev->ioaddr + VIRTIO_PCI_QUEUE_SEL);
	iowrite32(0, vp_dev->ioaddr + VIRTIO_PCI_QUEUE_PFN);

	kfree(info->queue);
	kfree(info);
}

static struct virtio_config_ops virtio_pci_config_ops = {
	.feature	= vp_feature,
	.get		= vp_get,
	.set		= vp_set,
	.get_status	= vp_get_status,
	.set_status	= vp_set_status,
	.reset		= vp_reset,
	.find_vq	= vp_find_vq,
	.del_vq		= vp_del_vq,
};

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

	snprintf(vp_dev->vdev.dev.bus_id, BUS_ID_SIZE, "virtio%d", dev_index);
	vp_dev->vdev.index = dev_index;
	dev_index++;

	vp_dev->vdev.dev.parent = &virtio_pci_root;
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

	/* register a handler for the queue with the PCI device's interrupt */
	err = request_irq(vp_dev->pci_dev->irq, vp_interrupt, IRQF_SHARED,
			  vp_dev->vdev.dev.bus_id, vp_dev);
	if (err)
		goto out_set_drvdata;

	/* finally register the virtio device */
	err = register_virtio_device(&vp_dev->vdev);
	if (err)
		goto out_req_irq;

	return 0;

out_req_irq:
	free_irq(pci_dev->irq, vp_dev);
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

	free_irq(pci_dev->irq, vp_dev);
	pci_set_drvdata(pci_dev, NULL);
	pci_iounmap(pci_dev, vp_dev->ioaddr);
	pci_release_regions(pci_dev);
	pci_disable_device(pci_dev);
	kfree(vp_dev);
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

	err = device_register(&virtio_pci_root);
	if (err)
		return err;

	err = pci_register_driver(&virtio_pci_driver);
	if (err)
		device_unregister(&virtio_pci_root);

	return err;
}

module_init(virtio_pci_init);

static void __exit virtio_pci_exit(void)
{
	device_unregister(&virtio_pci_root);
	pci_unregister_driver(&virtio_pci_driver);
}

module_exit(virtio_pci_exit);

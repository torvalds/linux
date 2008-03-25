/*
 * kvm_virtio.c - virtio for kvm on s390
 *
 * Copyright IBM Corp. 2008
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License (version 2 only)
 * as published by the Free Software Foundation.
 *
 *    Author(s): Christian Borntraeger <borntraeger@de.ibm.com>
 */

#include <linux/init.h>
#include <linux/bootmem.h>
#include <linux/err.h>
#include <linux/virtio.h>
#include <linux/virtio_config.h>
#include <linux/interrupt.h>
#include <linux/virtio_ring.h>
#include <asm/io.h>
#include <asm/kvm_para.h>
#include <asm/kvm_virtio.h>
#include <asm/setup.h>
#include <asm/s390_ext.h>

#define VIRTIO_SUBCODE_64 0x0D00

/*
 * The pointer to our (page) of device descriptions.
 */
static void *kvm_devices;

/*
 * Unique numbering for kvm devices.
 */
static unsigned int dev_index;

struct kvm_device {
	struct virtio_device vdev;
	struct kvm_device_desc *desc;
};

#define to_kvmdev(vd) container_of(vd, struct kvm_device, vdev)

/*
 * memory layout:
 * - kvm_device_descriptor
 *        struct kvm_device_desc
 * - configuration
 *        struct kvm_vqconfig
 * - feature bits
 * - config space
 */
static struct kvm_vqconfig *kvm_vq_config(const struct kvm_device_desc *desc)
{
	return (struct kvm_vqconfig *)(desc + 1);
}

static u8 *kvm_vq_features(const struct kvm_device_desc *desc)
{
	return (u8 *)(kvm_vq_config(desc) + desc->num_vq);
}

static u8 *kvm_vq_configspace(const struct kvm_device_desc *desc)
{
	return kvm_vq_features(desc) + desc->feature_len * 2;
}

/*
 * The total size of the config page used by this device (incl. desc)
 */
static unsigned desc_size(const struct kvm_device_desc *desc)
{
	return sizeof(*desc)
		+ desc->num_vq * sizeof(struct kvm_vqconfig)
		+ desc->feature_len * 2
		+ desc->config_len;
}

/*
 * This tests (and acknowleges) a feature bit.
 */
static bool kvm_feature(struct virtio_device *vdev, unsigned fbit)
{
	struct kvm_device_desc *desc = to_kvmdev(vdev)->desc;
	u8 *features;

	if (fbit / 8 > desc->feature_len)
		return false;

	features = kvm_vq_features(desc);
	if (!(features[fbit / 8] & (1 << (fbit % 8))))
		return false;

	/*
	 * We set the matching bit in the other half of the bitmap to tell the
	 * Host we want to use this feature.
	 */
	features[desc->feature_len + fbit / 8] |= (1 << (fbit % 8));
	return true;
}

/*
 * Reading and writing elements in config space
 */
static void kvm_get(struct virtio_device *vdev, unsigned int offset,
		   void *buf, unsigned len)
{
	struct kvm_device_desc *desc = to_kvmdev(vdev)->desc;

	BUG_ON(offset + len > desc->config_len);
	memcpy(buf, kvm_vq_configspace(desc) + offset, len);
}

static void kvm_set(struct virtio_device *vdev, unsigned int offset,
		   const void *buf, unsigned len)
{
	struct kvm_device_desc *desc = to_kvmdev(vdev)->desc;

	BUG_ON(offset + len > desc->config_len);
	memcpy(kvm_vq_configspace(desc) + offset, buf, len);
}

/*
 * The operations to get and set the status word just access
 * the status field of the device descriptor. set_status will also
 * make a hypercall to the host, to tell about status changes
 */
static u8 kvm_get_status(struct virtio_device *vdev)
{
	return to_kvmdev(vdev)->desc->status;
}

static void kvm_set_status(struct virtio_device *vdev, u8 status)
{
	BUG_ON(!status);
	to_kvmdev(vdev)->desc->status = status;
	kvm_hypercall1(KVM_S390_VIRTIO_SET_STATUS,
		       (unsigned long) to_kvmdev(vdev)->desc);
}

/*
 * To reset the device, we use the KVM_VIRTIO_RESET hypercall, using the
 * descriptor address. The Host will zero the status and all the
 * features.
 */
static void kvm_reset(struct virtio_device *vdev)
{
	kvm_hypercall1(KVM_S390_VIRTIO_RESET,
		       (unsigned long) to_kvmdev(vdev)->desc);
}

/*
 * When the virtio_ring code wants to notify the Host, it calls us here and we
 * make a hypercall.  We hand the address  of the virtqueue so the Host
 * knows which virtqueue we're talking about.
 */
static void kvm_notify(struct virtqueue *vq)
{
	struct kvm_vqconfig *config = vq->priv;

	kvm_hypercall1(KVM_S390_VIRTIO_NOTIFY, config->address);
}

/*
 * This routine finds the first virtqueue described in the configuration of
 * this device and sets it up.
 */
static struct virtqueue *kvm_find_vq(struct virtio_device *vdev,
				    unsigned index,
				    void (*callback)(struct virtqueue *vq))
{
	struct kvm_device *kdev = to_kvmdev(vdev);
	struct kvm_vqconfig *config;
	struct virtqueue *vq;
	int err;

	if (index >= kdev->desc->num_vq)
		return ERR_PTR(-ENOENT);

	config = kvm_vq_config(kdev->desc)+index;

	if (add_shared_memory(config->address,
				vring_size(config->num, PAGE_SIZE))) {
		err = -ENOMEM;
		goto out;
	}

	vq = vring_new_virtqueue(config->num, vdev, (void *) config->address,
				 kvm_notify, callback);
	if (!vq) {
		err = -ENOMEM;
		goto unmap;
	}

	/*
	 * register a callback token
	 * The host will sent this via the external interrupt parameter
	 */
	config->token = (u64) vq;

	vq->priv = config;
	return vq;
unmap:
	remove_shared_memory(config->address, vring_size(config->num,
			     PAGE_SIZE));
out:
	return ERR_PTR(err);
}

static void kvm_del_vq(struct virtqueue *vq)
{
	struct kvm_vqconfig *config = vq->priv;

	vring_del_virtqueue(vq);
	remove_shared_memory(config->address,
			     vring_size(config->num, PAGE_SIZE));
}

/*
 * The config ops structure as defined by virtio config
 */
static struct virtio_config_ops kvm_vq_configspace_ops = {
	.feature = kvm_feature,
	.get = kvm_get,
	.set = kvm_set,
	.get_status = kvm_get_status,
	.set_status = kvm_set_status,
	.reset = kvm_reset,
	.find_vq = kvm_find_vq,
	.del_vq = kvm_del_vq,
};

/*
 * The root device for the kvm virtio devices.
 * This makes them appear as /sys/devices/kvm_s390/0,1,2 not /sys/devices/0,1,2.
 */
static struct device kvm_root = {
	.parent = NULL,
	.bus_id = "kvm_s390",
};

/*
 * adds a new device and register it with virtio
 * appropriate drivers are loaded by the device model
 */
static void add_kvm_device(struct kvm_device_desc *d)
{
	struct kvm_device *kdev;

	kdev = kzalloc(sizeof(*kdev), GFP_KERNEL);
	if (!kdev) {
		printk(KERN_EMERG "Cannot allocate kvm dev %u\n",
		       dev_index++);
		return;
	}

	kdev->vdev.dev.parent = &kvm_root;
	kdev->vdev.index = dev_index++;
	kdev->vdev.id.device = d->type;
	kdev->vdev.config = &kvm_vq_configspace_ops;
	kdev->desc = d;

	if (register_virtio_device(&kdev->vdev) != 0) {
		printk(KERN_ERR "Failed to register kvm device %u\n",
		       kdev->vdev.index);
		kfree(kdev);
	}
}

/*
 * scan_devices() simply iterates through the device page.
 * The type 0 is reserved to mean "end of devices".
 */
static void scan_devices(void)
{
	unsigned int i;
	struct kvm_device_desc *d;

	for (i = 0; i < PAGE_SIZE; i += desc_size(d)) {
		d = kvm_devices + i;

		if (d->type == 0)
			break;

		add_kvm_device(d);
	}
}

/*
 * we emulate the request_irq behaviour on top of s390 extints
 */
static void kvm_extint_handler(u16 code)
{
	void *data = (void *) *(long *) __LC_PFAULT_INTPARM;
	u16 subcode = S390_lowcore.cpu_addr;

	if ((subcode & 0xff00) != VIRTIO_SUBCODE_64)
		return;

	vring_interrupt(0, data);
}

/*
 * Init function for virtio
 * devices are in a single page above top of "normal" mem
 */
static int __init kvm_devices_init(void)
{
	int rc;

	if (!MACHINE_IS_KVM)
		return -ENODEV;

	rc = device_register(&kvm_root);
	if (rc) {
		printk(KERN_ERR "Could not register kvm_s390 root device");
		return rc;
	}

	if (add_shared_memory((max_pfn) << PAGE_SHIFT, PAGE_SIZE)) {
		device_unregister(&kvm_root);
		return -ENOMEM;
	}

	kvm_devices  = (void *) (max_pfn << PAGE_SHIFT);

	ctl_set_bit(0, 9);
	register_external_interrupt(0x2603, kvm_extint_handler);

	scan_devices();
	return 0;
}

/*
 * We do this after core stuff, but before the drivers.
 */
postcore_initcall(kvm_devices_init);

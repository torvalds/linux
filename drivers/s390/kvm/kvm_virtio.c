/*
 * virtio for kvm on s390
 *
 * Copyright IBM Corp. 2008
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License (version 2 only)
 * as published by the Free Software Foundation.
 *
 *    Author(s): Christian Borntraeger <borntraeger@de.ibm.com>
 */

#include <linux/kernel_stat.h>
#include <linux/init.h>
#include <linux/bootmem.h>
#include <linux/err.h>
#include <linux/virtio.h>
#include <linux/virtio_config.h>
#include <linux/slab.h>
#include <linux/virtio_console.h>
#include <linux/interrupt.h>
#include <linux/virtio_ring.h>
#include <linux/export.h>
#include <linux/pfn.h>
#include <asm/io.h>
#include <asm/kvm_para.h>
#include <asm/kvm_virtio.h>
#include <asm/sclp.h>
#include <asm/setup.h>
#include <asm/irq.h>

#define VIRTIO_SUBCODE_64 0x0D00

/*
 * The pointer to our (page) of device descriptions.
 */
static void *kvm_devices;
static struct work_struct hotplug_work;

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

/* This gets the device's feature bits. */
static u32 kvm_get_features(struct virtio_device *vdev)
{
	unsigned int i;
	u32 features = 0;
	struct kvm_device_desc *desc = to_kvmdev(vdev)->desc;
	u8 *in_features = kvm_vq_features(desc);

	for (i = 0; i < min(desc->feature_len * 8, 32); i++)
		if (in_features[i / 8] & (1 << (i % 8)))
			features |= (1 << i);
	return features;
}

static void kvm_finalize_features(struct virtio_device *vdev)
{
	unsigned int i, bits;
	struct kvm_device_desc *desc = to_kvmdev(vdev)->desc;
	/* Second half of bitmap is features we accept. */
	u8 *out_features = kvm_vq_features(desc) + desc->feature_len;

	/* Give virtio_ring a chance to accept features. */
	vring_transport_features(vdev);

	memset(out_features, 0, desc->feature_len);
	bits = min_t(unsigned, desc->feature_len, sizeof(vdev->features)) * 8;
	for (i = 0; i < bits; i++) {
		if (test_bit(i, vdev->features))
			out_features[i / 8] |= (1 << (i % 8));
	}
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
				     void (*callback)(struct virtqueue *vq),
				     const char *name)
{
	struct kvm_device *kdev = to_kvmdev(vdev);
	struct kvm_vqconfig *config;
	struct virtqueue *vq;
	int err;

	if (index >= kdev->desc->num_vq)
		return ERR_PTR(-ENOENT);

	if (!name)
		return NULL;

	config = kvm_vq_config(kdev->desc)+index;

	err = vmem_add_mapping(config->address,
			       vring_size(config->num,
					  KVM_S390_VIRTIO_RING_ALIGN));
	if (err)
		goto out;

	vq = vring_new_virtqueue(index, config->num, KVM_S390_VIRTIO_RING_ALIGN,
				 vdev, true, (void *) config->address,
				 kvm_notify, callback, name);
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
	vmem_remove_mapping(config->address,
			    vring_size(config->num,
				       KVM_S390_VIRTIO_RING_ALIGN));
out:
	return ERR_PTR(err);
}

static void kvm_del_vq(struct virtqueue *vq)
{
	struct kvm_vqconfig *config = vq->priv;

	vring_del_virtqueue(vq);
	vmem_remove_mapping(config->address,
			    vring_size(config->num,
				       KVM_S390_VIRTIO_RING_ALIGN));
}

static void kvm_del_vqs(struct virtio_device *vdev)
{
	struct virtqueue *vq, *n;

	list_for_each_entry_safe(vq, n, &vdev->vqs, list)
		kvm_del_vq(vq);
}

static int kvm_find_vqs(struct virtio_device *vdev, unsigned nvqs,
			struct virtqueue *vqs[],
			vq_callback_t *callbacks[],
			const char *names[])
{
	struct kvm_device *kdev = to_kvmdev(vdev);
	int i;

	/* We must have this many virtqueues. */
	if (nvqs > kdev->desc->num_vq)
		return -ENOENT;

	for (i = 0; i < nvqs; ++i) {
		vqs[i] = kvm_find_vq(vdev, i, callbacks[i], names[i]);
		if (IS_ERR(vqs[i]))
			goto error;
	}
	return 0;

error:
	kvm_del_vqs(vdev);
	return PTR_ERR(vqs[i]);
}

static const char *kvm_bus_name(struct virtio_device *vdev)
{
	return "";
}

/*
 * The config ops structure as defined by virtio config
 */
static const struct virtio_config_ops kvm_vq_configspace_ops = {
	.get_features = kvm_get_features,
	.finalize_features = kvm_finalize_features,
	.get = kvm_get,
	.set = kvm_set,
	.get_status = kvm_get_status,
	.set_status = kvm_set_status,
	.reset = kvm_reset,
	.find_vqs = kvm_find_vqs,
	.del_vqs = kvm_del_vqs,
	.bus_name = kvm_bus_name,
};

/*
 * The root device for the kvm virtio devices.
 * This makes them appear as /sys/devices/kvm_s390/0,1,2 not /sys/devices/0,1,2.
 */
static struct device *kvm_root;

/*
 * adds a new device and register it with virtio
 * appropriate drivers are loaded by the device model
 */
static void add_kvm_device(struct kvm_device_desc *d, unsigned int offset)
{
	struct kvm_device *kdev;

	kdev = kzalloc(sizeof(*kdev), GFP_KERNEL);
	if (!kdev) {
		printk(KERN_EMERG "Cannot allocate kvm dev %u type %u\n",
		       offset, d->type);
		return;
	}

	kdev->vdev.dev.parent = kvm_root;
	kdev->vdev.id.device = d->type;
	kdev->vdev.config = &kvm_vq_configspace_ops;
	kdev->desc = d;

	if (register_virtio_device(&kdev->vdev) != 0) {
		printk(KERN_ERR "Failed to register kvm device %u type %u\n",
		       offset, d->type);
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

		add_kvm_device(d, i);
	}
}

/*
 * match for a kvm device with a specific desc pointer
 */
static int match_desc(struct device *dev, void *data)
{
	struct virtio_device *vdev = dev_to_virtio(dev);
	struct kvm_device *kdev = to_kvmdev(vdev);

	return kdev->desc == data;
}

/*
 * hotplug_device tries to find changes in the device page.
 */
static void hotplug_devices(struct work_struct *dummy)
{
	unsigned int i;
	struct kvm_device_desc *d;
	struct device *dev;

	for (i = 0; i < PAGE_SIZE; i += desc_size(d)) {
		d = kvm_devices + i;

		/* end of list */
		if (d->type == 0)
			break;

		/* device already exists */
		dev = device_find_child(kvm_root, d, match_desc);
		if (dev) {
			/* XXX check for hotplug remove */
			put_device(dev);
			continue;
		}

		/* new device */
		printk(KERN_INFO "Adding new virtio device %p\n", d);
		add_kvm_device(d, i);
	}
}

/*
 * we emulate the request_irq behaviour on top of s390 extints
 */
static void kvm_extint_handler(struct ext_code ext_code,
			       unsigned int param32, unsigned long param64)
{
	struct virtqueue *vq;
	u32 param;

	if ((ext_code.subcode & 0xff00) != VIRTIO_SUBCODE_64)
		return;
	inc_irq_stat(IRQEXT_VRT);

	/* The LSB might be overloaded, we have to mask it */
	vq = (struct virtqueue *)(param64 & ~1UL);

	/* We use ext_params to decide what this interrupt means */
	param = param32 & VIRTIO_PARAM_MASK;

	switch (param) {
	case VIRTIO_PARAM_CONFIG_CHANGED:
	{
		struct virtio_driver *drv;
		drv = container_of(vq->vdev->dev.driver,
				   struct virtio_driver, driver);
		if (drv->config_changed)
			drv->config_changed(vq->vdev);

		break;
	}
	case VIRTIO_PARAM_DEV_ADD:
		schedule_work(&hotplug_work);
		break;
	case VIRTIO_PARAM_VRING_INTERRUPT:
	default:
		vring_interrupt(0, vq);
		break;
	}
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

	kvm_root = root_device_register("kvm_s390");
	if (IS_ERR(kvm_root)) {
		rc = PTR_ERR(kvm_root);
		printk(KERN_ERR "Could not register kvm_s390 root device");
		return rc;
	}

	rc = vmem_add_mapping(real_memory_size, PAGE_SIZE);
	if (rc) {
		root_device_unregister(kvm_root);
		return rc;
	}

	kvm_devices = (void *) real_memory_size;

	INIT_WORK(&hotplug_work, hotplug_devices);

	service_subclass_irq_register();
	register_external_interrupt(0x2603, kvm_extint_handler);

	scan_devices();
	return 0;
}

/* code for early console output with virtio_console */
static __init int early_put_chars(u32 vtermno, const char *buf, int count)
{
	char scratch[17];
	unsigned int len = count;

	if (len > sizeof(scratch) - 1)
		len = sizeof(scratch) - 1;
	scratch[len] = '\0';
	memcpy(scratch, buf, len);
	kvm_hypercall1(KVM_S390_VIRTIO_NOTIFY, __pa(scratch));
	return len;
}

static int __init s390_virtio_console_init(void)
{
	if (sclp_has_vt220() || sclp_has_linemode())
		return -ENODEV;
	return virtio_cons_early_init(early_put_chars);
}
console_initcall(s390_virtio_console_init);


/*
 * We do this after core stuff, but before the drivers.
 */
postcore_initcall(kvm_devices_init);

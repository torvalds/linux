/*
 * Virtio memory mapped device driver
 *
 * Copyright 2011, ARM Ltd.
 *
 * This module allows virtio devices to be used over a virtual, memory mapped
 * platform device.
 *
 * The guest device(s) may be instantiated in one of three equivalent ways:
 *
 * 1. Static platform device in board's code, eg.:
 *
 *	static struct platform_device v2m_virtio_device = {
 *		.name = "virtio-mmio",
 *		.id = -1,
 *		.num_resources = 2,
 *		.resource = (struct resource []) {
 *			{
 *				.start = 0x1001e000,
 *				.end = 0x1001e0ff,
 *				.flags = IORESOURCE_MEM,
 *			}, {
 *				.start = 42 + 32,
 *				.end = 42 + 32,
 *				.flags = IORESOURCE_IRQ,
 *			},
 *		}
 *	};
 *
 * 2. Device Tree node, eg.:
 *
 *		virtio_block@1e000 {
 *			compatible = "virtio,mmio";
 *			reg = <0x1e000 0x100>;
 *			interrupts = <42>;
 *		}
 *
 * 3. Kernel module (or command line) parameter. Can be used more than once -
 *    one device will be created for each one. Syntax:
 *
 *		[virtio_mmio.]device=<size>@<baseaddr>:<irq>[:<id>]
 *    where:
 *		<size>     := size (can use standard suffixes like K, M or G)
 *		<baseaddr> := physical base address
 *		<irq>      := interrupt number (as passed to request_irq())
 *		<id>       := (optional) platform device id
 *    eg.:
 *		virtio_mmio.device=0x100@0x100b0000:48 \
 *				virtio_mmio.device=1K@0x1001e000:74
 *
 *
 *
 * Registers layout (all 32-bit wide):
 *
 * offset d. name             description
 * ------ -- ---------------- -----------------
 *
 * 0x000  R  MagicValue       Magic value "virt"
 * 0x004  R  Version          Device version (current max. 1)
 * 0x008  R  DeviceID         Virtio device ID
 * 0x00c  R  VendorID         Virtio vendor ID
 *
 * 0x010  R  HostFeatures     Features supported by the host
 * 0x014  W  HostFeaturesSel  Set of host features to access via HostFeatures
 *
 * 0x020  W  GuestFeatures    Features activated by the guest
 * 0x024  W  GuestFeaturesSel Set of activated features to set via GuestFeatures
 * 0x028  W  GuestPageSize    Size of guest's memory page in bytes
 *
 * 0x030  W  QueueSel         Queue selector
 * 0x034  R  QueueNumMax      Maximum size of the currently selected queue
 * 0x038  W  QueueNum         Queue size for the currently selected queue
 * 0x03c  W  QueueAlign       Used Ring alignment for the current queue
 * 0x040  RW QueuePFN         PFN for the currently selected queue
 *
 * 0x050  W  QueueNotify      Queue notifier
 * 0x060  R  InterruptStatus  Interrupt status register
 * 0x064  W  InterruptACK     Interrupt acknowledge register
 * 0x070  RW Status           Device status register
 *
 * 0x100+ RW                  Device-specific configuration space
 *
 * Based on Virtio PCI driver by Anthony Liguori, copyright IBM Corp. 2007
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#define pr_fmt(fmt) "virtio-mmio: " fmt

#include <linux/highmem.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/virtio.h>
#include <linux/virtio_config.h>
#include <linux/virtio_mmio.h>
#include <linux/virtio_ring.h>



/* The alignment to use between consumer and producer parts of vring.
 * Currently hardcoded to the page size. */
#define VIRTIO_MMIO_VRING_ALIGN		PAGE_SIZE



#define to_virtio_mmio_device(_plat_dev) \
	container_of(_plat_dev, struct virtio_mmio_device, vdev)

struct virtio_mmio_device {
	struct virtio_device vdev;
	struct platform_device *pdev;

	void __iomem *base;
	unsigned long version;

	/* a list of queues so we can dispatch IRQs */
	spinlock_t lock;
	struct list_head virtqueues;
};

struct virtio_mmio_vq_info {
	/* the actual virtqueue */
	struct virtqueue *vq;

	/* the number of entries in the queue */
	unsigned int num;

	/* the virtual address of the ring queue */
	void *queue;

	/* the list node for the virtqueues list */
	struct list_head node;
};



/* Configuration interface */

static u32 vm_get_features(struct virtio_device *vdev)
{
	struct virtio_mmio_device *vm_dev = to_virtio_mmio_device(vdev);

	/* TODO: Features > 32 bits */
	writel(0, vm_dev->base + VIRTIO_MMIO_HOST_FEATURES_SEL);

	return readl(vm_dev->base + VIRTIO_MMIO_HOST_FEATURES);
}

static void vm_finalize_features(struct virtio_device *vdev)
{
	struct virtio_mmio_device *vm_dev = to_virtio_mmio_device(vdev);

	/* Give virtio_ring a chance to accept features. */
	vring_transport_features(vdev);

	writel(0, vm_dev->base + VIRTIO_MMIO_GUEST_FEATURES_SEL);
	writel(vdev->features, vm_dev->base + VIRTIO_MMIO_GUEST_FEATURES);
}

static void vm_get(struct virtio_device *vdev, unsigned offset,
		   void *buf, unsigned len)
{
	struct virtio_mmio_device *vm_dev = to_virtio_mmio_device(vdev);
	u8 *ptr = buf;
	int i;

	for (i = 0; i < len; i++)
		ptr[i] = readb(vm_dev->base + VIRTIO_MMIO_CONFIG + offset + i);
}

static void vm_set(struct virtio_device *vdev, unsigned offset,
		   const void *buf, unsigned len)
{
	struct virtio_mmio_device *vm_dev = to_virtio_mmio_device(vdev);
	const u8 *ptr = buf;
	int i;

	for (i = 0; i < len; i++)
		writeb(ptr[i], vm_dev->base + VIRTIO_MMIO_CONFIG + offset + i);
}

static u8 vm_get_status(struct virtio_device *vdev)
{
	struct virtio_mmio_device *vm_dev = to_virtio_mmio_device(vdev);

	return readl(vm_dev->base + VIRTIO_MMIO_STATUS) & 0xff;
}

static void vm_set_status(struct virtio_device *vdev, u8 status)
{
	struct virtio_mmio_device *vm_dev = to_virtio_mmio_device(vdev);

	/* We should never be setting status to 0. */
	BUG_ON(status == 0);

	writel(status, vm_dev->base + VIRTIO_MMIO_STATUS);
}

static void vm_reset(struct virtio_device *vdev)
{
	struct virtio_mmio_device *vm_dev = to_virtio_mmio_device(vdev);

	/* 0 status means a reset. */
	writel(0, vm_dev->base + VIRTIO_MMIO_STATUS);
}



/* Transport interface */

/* the notify function used when creating a virt queue */
static bool vm_notify(struct virtqueue *vq)
{
	struct virtio_mmio_device *vm_dev = to_virtio_mmio_device(vq->vdev);

	/* We write the queue's selector into the notification register to
	 * signal the other end */
	writel(vq->index, vm_dev->base + VIRTIO_MMIO_QUEUE_NOTIFY);
	return true;
}

/* Notify all virtqueues on an interrupt. */
static irqreturn_t vm_interrupt(int irq, void *opaque)
{
	struct virtio_mmio_device *vm_dev = opaque;
	struct virtio_mmio_vq_info *info;
	unsigned long status;
	unsigned long flags;
	irqreturn_t ret = IRQ_NONE;

	/* Read and acknowledge interrupts */
	status = readl(vm_dev->base + VIRTIO_MMIO_INTERRUPT_STATUS);
	writel(status, vm_dev->base + VIRTIO_MMIO_INTERRUPT_ACK);

	if (unlikely(status & VIRTIO_MMIO_INT_CONFIG)) {
		virtio_config_changed(&vm_dev->vdev);
		ret = IRQ_HANDLED;
	}

	if (likely(status & VIRTIO_MMIO_INT_VRING)) {
		spin_lock_irqsave(&vm_dev->lock, flags);
		list_for_each_entry(info, &vm_dev->virtqueues, node)
			ret |= vring_interrupt(irq, info->vq);
		spin_unlock_irqrestore(&vm_dev->lock, flags);
	}

	return ret;
}



static void vm_del_vq(struct virtqueue *vq)
{
	struct virtio_mmio_device *vm_dev = to_virtio_mmio_device(vq->vdev);
	struct virtio_mmio_vq_info *info = vq->priv;
	unsigned long flags, size;
	unsigned int index = vq->index;

	spin_lock_irqsave(&vm_dev->lock, flags);
	list_del(&info->node);
	spin_unlock_irqrestore(&vm_dev->lock, flags);

	vring_del_virtqueue(vq);

	/* Select and deactivate the queue */
	writel(index, vm_dev->base + VIRTIO_MMIO_QUEUE_SEL);
	writel(0, vm_dev->base + VIRTIO_MMIO_QUEUE_PFN);

	size = PAGE_ALIGN(vring_size(info->num, VIRTIO_MMIO_VRING_ALIGN));
	free_pages_exact(info->queue, size);
	kfree(info);
}

static void vm_del_vqs(struct virtio_device *vdev)
{
	struct virtio_mmio_device *vm_dev = to_virtio_mmio_device(vdev);
	struct virtqueue *vq, *n;

	list_for_each_entry_safe(vq, n, &vdev->vqs, list)
		vm_del_vq(vq);

	free_irq(platform_get_irq(vm_dev->pdev, 0), vm_dev);
}



static struct virtqueue *vm_setup_vq(struct virtio_device *vdev, unsigned index,
				  void (*callback)(struct virtqueue *vq),
				  const char *name)
{
	struct virtio_mmio_device *vm_dev = to_virtio_mmio_device(vdev);
	struct virtio_mmio_vq_info *info;
	struct virtqueue *vq;
	unsigned long flags, size;
	int err;

	if (!name)
		return NULL;

	/* Select the queue we're interested in */
	writel(index, vm_dev->base + VIRTIO_MMIO_QUEUE_SEL);

	/* Queue shouldn't already be set up. */
	if (readl(vm_dev->base + VIRTIO_MMIO_QUEUE_PFN)) {
		err = -ENOENT;
		goto error_available;
	}

	/* Allocate and fill out our active queue description */
	info = kmalloc(sizeof(*info), GFP_KERNEL);
	if (!info) {
		err = -ENOMEM;
		goto error_kmalloc;
	}

	/* Allocate pages for the queue - start with a queue as big as
	 * possible (limited by maximum size allowed by device), drop down
	 * to a minimal size, just big enough to fit descriptor table
	 * and two rings (which makes it "alignment_size * 2")
	 */
	info->num = readl(vm_dev->base + VIRTIO_MMIO_QUEUE_NUM_MAX);

	/* If the device reports a 0 entry queue, we won't be able to
	 * use it to perform I/O, and vring_new_virtqueue() can't create
	 * empty queues anyway, so don't bother to set up the device.
	 */
	if (info->num == 0) {
		err = -ENOENT;
		goto error_alloc_pages;
	}

	while (1) {
		size = PAGE_ALIGN(vring_size(info->num,
				VIRTIO_MMIO_VRING_ALIGN));
		/* Did the last iter shrink the queue below minimum size? */
		if (size < VIRTIO_MMIO_VRING_ALIGN * 2) {
			err = -ENOMEM;
			goto error_alloc_pages;
		}

		info->queue = alloc_pages_exact(size, GFP_KERNEL | __GFP_ZERO);
		if (info->queue)
			break;

		info->num /= 2;
	}

	/* Activate the queue */
	writel(info->num, vm_dev->base + VIRTIO_MMIO_QUEUE_NUM);
	writel(VIRTIO_MMIO_VRING_ALIGN,
			vm_dev->base + VIRTIO_MMIO_QUEUE_ALIGN);
	writel(virt_to_phys(info->queue) >> PAGE_SHIFT,
			vm_dev->base + VIRTIO_MMIO_QUEUE_PFN);

	/* Create the vring */
	vq = vring_new_virtqueue(index, info->num, VIRTIO_MMIO_VRING_ALIGN, vdev,
				 true, info->queue, vm_notify, callback, name);
	if (!vq) {
		err = -ENOMEM;
		goto error_new_virtqueue;
	}

	vq->priv = info;
	info->vq = vq;

	spin_lock_irqsave(&vm_dev->lock, flags);
	list_add(&info->node, &vm_dev->virtqueues);
	spin_unlock_irqrestore(&vm_dev->lock, flags);

	return vq;

error_new_virtqueue:
	writel(0, vm_dev->base + VIRTIO_MMIO_QUEUE_PFN);
	free_pages_exact(info->queue, size);
error_alloc_pages:
	kfree(info);
error_kmalloc:
error_available:
	return ERR_PTR(err);
}

static int vm_find_vqs(struct virtio_device *vdev, unsigned nvqs,
		       struct virtqueue *vqs[],
		       vq_callback_t *callbacks[],
		       const char *names[])
{
	struct virtio_mmio_device *vm_dev = to_virtio_mmio_device(vdev);
	unsigned int irq = platform_get_irq(vm_dev->pdev, 0);
	int i, err;

	err = request_irq(irq, vm_interrupt, IRQF_SHARED,
			dev_name(&vdev->dev), vm_dev);
	if (err)
		return err;

	for (i = 0; i < nvqs; ++i) {
		vqs[i] = vm_setup_vq(vdev, i, callbacks[i], names[i]);
		if (IS_ERR(vqs[i])) {
			vm_del_vqs(vdev);
			return PTR_ERR(vqs[i]);
		}
	}

	return 0;
}

static const char *vm_bus_name(struct virtio_device *vdev)
{
	struct virtio_mmio_device *vm_dev = to_virtio_mmio_device(vdev);

	return vm_dev->pdev->name;
}

static const struct virtio_config_ops virtio_mmio_config_ops = {
	.get		= vm_get,
	.set		= vm_set,
	.get_status	= vm_get_status,
	.set_status	= vm_set_status,
	.reset		= vm_reset,
	.find_vqs	= vm_find_vqs,
	.del_vqs	= vm_del_vqs,
	.get_features	= vm_get_features,
	.finalize_features = vm_finalize_features,
	.bus_name	= vm_bus_name,
};



/* Platform device */

static int virtio_mmio_probe(struct platform_device *pdev)
{
	struct virtio_mmio_device *vm_dev;
	struct resource *mem;
	unsigned long magic;

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!mem)
		return -EINVAL;

	if (!devm_request_mem_region(&pdev->dev, mem->start,
			resource_size(mem), pdev->name))
		return -EBUSY;

	vm_dev = devm_kzalloc(&pdev->dev, sizeof(*vm_dev), GFP_KERNEL);
	if (!vm_dev)
		return  -ENOMEM;

	vm_dev->vdev.dev.parent = &pdev->dev;
	vm_dev->vdev.config = &virtio_mmio_config_ops;
	vm_dev->pdev = pdev;
	INIT_LIST_HEAD(&vm_dev->virtqueues);
	spin_lock_init(&vm_dev->lock);

	vm_dev->base = devm_ioremap(&pdev->dev, mem->start, resource_size(mem));
	if (vm_dev->base == NULL)
		return -EFAULT;

	/* Check magic value */
	magic = readl(vm_dev->base + VIRTIO_MMIO_MAGIC_VALUE);
	if (magic != ('v' | 'i' << 8 | 'r' << 16 | 't' << 24)) {
		dev_warn(&pdev->dev, "Wrong magic value 0x%08lx!\n", magic);
		return -ENODEV;
	}

	/* Check device version */
	vm_dev->version = readl(vm_dev->base + VIRTIO_MMIO_VERSION);
	if (vm_dev->version != 1) {
		dev_err(&pdev->dev, "Version %ld not supported!\n",
				vm_dev->version);
		return -ENXIO;
	}

	vm_dev->vdev.id.device = readl(vm_dev->base + VIRTIO_MMIO_DEVICE_ID);
	vm_dev->vdev.id.vendor = readl(vm_dev->base + VIRTIO_MMIO_VENDOR_ID);

	writel(PAGE_SIZE, vm_dev->base + VIRTIO_MMIO_GUEST_PAGE_SIZE);

	platform_set_drvdata(pdev, vm_dev);

	return register_virtio_device(&vm_dev->vdev);
}

static int virtio_mmio_remove(struct platform_device *pdev)
{
	struct virtio_mmio_device *vm_dev = platform_get_drvdata(pdev);

	unregister_virtio_device(&vm_dev->vdev);

	return 0;
}



/* Devices list parameter */

#if defined(CONFIG_VIRTIO_MMIO_CMDLINE_DEVICES)

static struct device vm_cmdline_parent = {
	.init_name = "virtio-mmio-cmdline",
};

static int vm_cmdline_parent_registered;
static int vm_cmdline_id;

static int vm_cmdline_set(const char *device,
		const struct kernel_param *kp)
{
	int err;
	struct resource resources[2] = {};
	char *str;
	long long int base, size;
	unsigned int irq;
	int processed, consumed = 0;
	struct platform_device *pdev;

	/* Consume "size" part of the command line parameter */
	size = memparse(device, &str);

	/* Get "@<base>:<irq>[:<id>]" chunks */
	processed = sscanf(str, "@%lli:%u%n:%d%n",
			&base, &irq, &consumed,
			&vm_cmdline_id, &consumed);

	/*
	 * sscanf() must processes at least 2 chunks; also there
	 * must be no extra characters after the last chunk, so
	 * str[consumed] must be '\0'
	 */
	if (processed < 2 || str[consumed])
		return -EINVAL;

	resources[0].flags = IORESOURCE_MEM;
	resources[0].start = base;
	resources[0].end = base + size - 1;

	resources[1].flags = IORESOURCE_IRQ;
	resources[1].start = resources[1].end = irq;

	if (!vm_cmdline_parent_registered) {
		err = device_register(&vm_cmdline_parent);
		if (err) {
			pr_err("Failed to register parent device!\n");
			return err;
		}
		vm_cmdline_parent_registered = 1;
	}

	pr_info("Registering device virtio-mmio.%d at 0x%llx-0x%llx, IRQ %d.\n",
		       vm_cmdline_id,
		       (unsigned long long)resources[0].start,
		       (unsigned long long)resources[0].end,
		       (int)resources[1].start);

	pdev = platform_device_register_resndata(&vm_cmdline_parent,
			"virtio-mmio", vm_cmdline_id++,
			resources, ARRAY_SIZE(resources), NULL, 0);
	if (IS_ERR(pdev))
		return PTR_ERR(pdev);

	return 0;
}

static int vm_cmdline_get_device(struct device *dev, void *data)
{
	char *buffer = data;
	unsigned int len = strlen(buffer);
	struct platform_device *pdev = to_platform_device(dev);

	snprintf(buffer + len, PAGE_SIZE - len, "0x%llx@0x%llx:%llu:%d\n",
			pdev->resource[0].end - pdev->resource[0].start + 1ULL,
			(unsigned long long)pdev->resource[0].start,
			(unsigned long long)pdev->resource[1].start,
			pdev->id);
	return 0;
}

static int vm_cmdline_get(char *buffer, const struct kernel_param *kp)
{
	buffer[0] = '\0';
	device_for_each_child(&vm_cmdline_parent, buffer,
			vm_cmdline_get_device);
	return strlen(buffer) + 1;
}

static struct kernel_param_ops vm_cmdline_param_ops = {
	.set = vm_cmdline_set,
	.get = vm_cmdline_get,
};

device_param_cb(device, &vm_cmdline_param_ops, NULL, S_IRUSR);

static int vm_unregister_cmdline_device(struct device *dev,
		void *data)
{
	platform_device_unregister(to_platform_device(dev));

	return 0;
}

static void vm_unregister_cmdline_devices(void)
{
	if (vm_cmdline_parent_registered) {
		device_for_each_child(&vm_cmdline_parent, NULL,
				vm_unregister_cmdline_device);
		device_unregister(&vm_cmdline_parent);
		vm_cmdline_parent_registered = 0;
	}
}

#else

static void vm_unregister_cmdline_devices(void)
{
}

#endif

/* Platform driver */

static struct of_device_id virtio_mmio_match[] = {
	{ .compatible = "virtio,mmio", },
	{},
};
MODULE_DEVICE_TABLE(of, virtio_mmio_match);

static struct platform_driver virtio_mmio_driver = {
	.probe		= virtio_mmio_probe,
	.remove		= virtio_mmio_remove,
	.driver		= {
		.name	= "virtio-mmio",
		.owner	= THIS_MODULE,
		.of_match_table	= virtio_mmio_match,
	},
};

static int __init virtio_mmio_init(void)
{
	return platform_driver_register(&virtio_mmio_driver);
}

static void __exit virtio_mmio_exit(void)
{
	platform_driver_unregister(&virtio_mmio_driver);
	vm_unregister_cmdline_devices();
}

module_init(virtio_mmio_init);
module_exit(virtio_mmio_exit);

MODULE_AUTHOR("Pawel Moll <pawel.moll@arm.com>");
MODULE_DESCRIPTION("Platform bus driver for memory mapped virtio devices");
MODULE_LICENSE("GPL");

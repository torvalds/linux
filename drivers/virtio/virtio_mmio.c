// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Virtio memory mapped device driver
 *
 * Copyright 2011-2014, ARM Ltd.
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
 * Based on Virtio PCI driver by Anthony Liguori, copyright IBM Corp. 2007
 */

#define pr_fmt(fmt) "virtio-mmio: " fmt

#include <linux/acpi.h>
#include <linux/dma-mapping.h>
#include <linux/highmem.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/of_address.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/virtio.h>
#include <linux/virtio_config.h>
#include <uapi/linux/virtio_mmio.h>
#include <linux/virtio_ring.h>
#include <linux/delay.h>

#ifdef CONFIG_GH_VIRTIO_DEBUG
#define CREATE_TRACE_POINTS
#include <trace/events/gh_virtio_frontend.h>
#undef CREATE_TRACE_POINTS
#endif

#ifdef CONFIG_VIRTIO_MMIO_SWIOTLB
#include <linux/swiotlb.h>
#include <linux/dma-direct.h>
#endif


/* The alignment to use between consumer and producer parts of vring.
 * Currently hardcoded to the page size. */
#define VIRTIO_MMIO_VRING_ALIGN		PAGE_SIZE



#define to_virtio_mmio_device(_plat_dev) \
	container_of(_plat_dev, struct virtio_mmio_device, vdev)

#ifdef CONFIG_VIRTIO_MMIO_SWIOTLB
struct virtio_mem_pool {
	void *virt_base;
	dma_addr_t dma_base;
	size_t size;
	unsigned long *bitmap;
	spinlock_t lock;
};
#endif

struct virtio_mmio_device {
	struct virtio_device vdev;
	struct platform_device *pdev;

	void __iomem *base;
	unsigned long version;

	/* a list of queues so we can dispatch IRQs */
	spinlock_t lock;
	struct list_head virtqueues;
#ifdef CONFIG_VIRTIO_MMIO_SWIOTLB
	struct virtio_mem_pool *mem_pool;
#endif
};

struct virtio_mmio_vq_info {
	/* the actual virtqueue */
	struct virtqueue *vq;

	/* the list node for the virtqueues list */
	struct list_head node;
};



/* Configuration interface */

static u64 vm_get_features(struct virtio_device *vdev)
{
	struct virtio_mmio_device *vm_dev = to_virtio_mmio_device(vdev);
	u64 features;

	writel(1, vm_dev->base + VIRTIO_MMIO_DEVICE_FEATURES_SEL);
	features = readl(vm_dev->base + VIRTIO_MMIO_DEVICE_FEATURES);
	features <<= 32;

	writel(0, vm_dev->base + VIRTIO_MMIO_DEVICE_FEATURES_SEL);
	features |= readl(vm_dev->base + VIRTIO_MMIO_DEVICE_FEATURES);

	return features;
}

static int vm_finalize_features(struct virtio_device *vdev)
{
	struct virtio_mmio_device *vm_dev = to_virtio_mmio_device(vdev);

	/* Give virtio_ring a chance to accept features. */
	vring_transport_features(vdev);

	/* Make sure there are no mixed devices */
	if (vm_dev->version == 2 &&
			!__virtio_test_bit(vdev, VIRTIO_F_VERSION_1)) {
		dev_err(&vdev->dev, "New virtio-mmio devices (version 2) must provide VIRTIO_F_VERSION_1 feature!\n");
		return -EINVAL;
	}

	writel(1, vm_dev->base + VIRTIO_MMIO_DRIVER_FEATURES_SEL);
	writel((u32)(vdev->features >> 32),
			vm_dev->base + VIRTIO_MMIO_DRIVER_FEATURES);

	writel(0, vm_dev->base + VIRTIO_MMIO_DRIVER_FEATURES_SEL);
	writel((u32)vdev->features,
			vm_dev->base + VIRTIO_MMIO_DRIVER_FEATURES);

	return 0;
}

static void vm_get(struct virtio_device *vdev, unsigned int offset,
		   void *buf, unsigned int len)
{
	struct virtio_mmio_device *vm_dev = to_virtio_mmio_device(vdev);
	void __iomem *base = vm_dev->base + VIRTIO_MMIO_CONFIG;
	u8 b;
	__le16 w;
	__le32 l;

	if (vm_dev->version == 1) {
		u8 *ptr = buf;
		int i;

		for (i = 0; i < len; i++)
			ptr[i] = readb(base + offset + i);
		return;
	}

	switch (len) {
	case 1:
		b = readb(base + offset);
		memcpy(buf, &b, sizeof b);
		break;
	case 2:
		w = cpu_to_le16(readw(base + offset));
		memcpy(buf, &w, sizeof w);
		break;
	case 4:
		l = cpu_to_le32(readl(base + offset));
		memcpy(buf, &l, sizeof l);
		break;
	case 8:
		l = cpu_to_le32(readl(base + offset));
		memcpy(buf, &l, sizeof l);
		l = cpu_to_le32(ioread32(base + offset + sizeof l));
		memcpy(buf + sizeof l, &l, sizeof l);
		break;
	default:
		BUG();
	}
}

static void vm_set(struct virtio_device *vdev, unsigned int offset,
		   const void *buf, unsigned int len)
{
	struct virtio_mmio_device *vm_dev = to_virtio_mmio_device(vdev);
	void __iomem *base = vm_dev->base + VIRTIO_MMIO_CONFIG;
	u8 b;
	__le16 w;
	__le32 l;

	if (vm_dev->version == 1) {
		const u8 *ptr = buf;
		int i;

		for (i = 0; i < len; i++)
			writeb(ptr[i], base + offset + i);

		return;
	}

	switch (len) {
	case 1:
		memcpy(&b, buf, sizeof b);
		writeb(b, base + offset);
		break;
	case 2:
		memcpy(&w, buf, sizeof w);
		writew(le16_to_cpu(w), base + offset);
		break;
	case 4:
		memcpy(&l, buf, sizeof l);
		writel(le32_to_cpu(l), base + offset);
		break;
	case 8:
		memcpy(&l, buf, sizeof l);
		writel(le32_to_cpu(l), base + offset);
		memcpy(&l, buf + sizeof l, sizeof l);
		writel(le32_to_cpu(l), base + offset + sizeof l);
		break;
	default:
		BUG();
	}
}

static u32 vm_generation(struct virtio_device *vdev)
{
	struct virtio_mmio_device *vm_dev = to_virtio_mmio_device(vdev);

	if (vm_dev->version == 1)
		return 0;
	else
		return readl(vm_dev->base + VIRTIO_MMIO_CONFIG_GENERATION);
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

	/*
	 * Per memory-barriers.txt, wmb() is not needed to guarantee
	 * that the cache coherent memory writes have completed
	 * before writing to the MMIO region.
	 */
	writel(status, vm_dev->base + VIRTIO_MMIO_STATUS);
}

static void vm_reset(struct virtio_device *vdev)
{
	struct virtio_mmio_device *vm_dev = to_virtio_mmio_device(vdev);

	/* 0 status means a reset. */
	writel(0, vm_dev->base + VIRTIO_MMIO_STATUS);
#ifdef CONFIG_VIRTIO_MMIO_POLL_RESET
	/* After writing 0 to device_status, the driver MUST wait for a read of
	 * device_status to return 0 before reinitializing the device.
	 */
	while (readl(vm_dev->base + VIRTIO_MMIO_STATUS))
		usleep_range(1000, 1100);
#endif
}



/* Transport interface */

/* the notify function used when creating a virt queue */
static bool vm_notify(struct virtqueue *vq)
{
	struct virtio_mmio_device *vm_dev = to_virtio_mmio_device(vq->vdev);

#ifdef CONFIG_GH_VIRTIO_DEBUG
	trace_virtio_mmio_vm_notify(vq->vdev->index, vq->index);
#endif
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
#ifdef CONFIG_GH_VIRTIO_DEBUG
	trace_virtio_mmio_vm_interrupt(vm_dev->vdev.index, status);
#endif

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
	unsigned long flags;
	unsigned int index = vq->index;

	spin_lock_irqsave(&vm_dev->lock, flags);
	list_del(&info->node);
	spin_unlock_irqrestore(&vm_dev->lock, flags);

	/* Select and deactivate the queue */
	writel(index, vm_dev->base + VIRTIO_MMIO_QUEUE_SEL);
	if (vm_dev->version == 1) {
		writel(0, vm_dev->base + VIRTIO_MMIO_QUEUE_PFN);
	} else {
		writel(0, vm_dev->base + VIRTIO_MMIO_QUEUE_READY);
		WARN_ON(readl(vm_dev->base + VIRTIO_MMIO_QUEUE_READY));
	}

	vring_del_virtqueue(vq);

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

static void vm_synchronize_cbs(struct virtio_device *vdev)
{
	struct virtio_mmio_device *vm_dev = to_virtio_mmio_device(vdev);

	synchronize_irq(platform_get_irq(vm_dev->pdev, 0));
}

static struct virtqueue *vm_setup_vq(struct virtio_device *vdev, unsigned int index,
				  void (*callback)(struct virtqueue *vq),
				  const char *name, bool ctx)
{
	struct virtio_mmio_device *vm_dev = to_virtio_mmio_device(vdev);
	struct virtio_mmio_vq_info *info;
	struct virtqueue *vq;
	unsigned long flags;
	unsigned int num;
	int err;

	if (!name)
		return NULL;

	/* Select the queue we're interested in */
	writel(index, vm_dev->base + VIRTIO_MMIO_QUEUE_SEL);

	/* Queue shouldn't already be set up. */
	if (readl(vm_dev->base + (vm_dev->version == 1 ?
			VIRTIO_MMIO_QUEUE_PFN : VIRTIO_MMIO_QUEUE_READY))) {
		err = -ENOENT;
		goto error_available;
	}

	/* Allocate and fill out our active queue description */
	info = kmalloc(sizeof(*info), GFP_KERNEL);
	if (!info) {
		err = -ENOMEM;
		goto error_kmalloc;
	}

	num = readl(vm_dev->base + VIRTIO_MMIO_QUEUE_NUM_MAX);
	if (num == 0) {
		err = -ENOENT;
		goto error_new_virtqueue;
	}

	/* Create the vring */
	vq = vring_create_virtqueue(index, num, VIRTIO_MMIO_VRING_ALIGN, vdev,
				 true, true, ctx, vm_notify, callback, name);
	if (!vq) {
		err = -ENOMEM;
		goto error_new_virtqueue;
	}

	vq->num_max = num;

	/* Activate the queue */
	writel(virtqueue_get_vring_size(vq), vm_dev->base + VIRTIO_MMIO_QUEUE_NUM);
	if (vm_dev->version == 1) {
		u64 q_pfn = virtqueue_get_desc_addr(vq) >> PAGE_SHIFT;

		/*
		 * virtio-mmio v1 uses a 32bit QUEUE PFN. If we have something
		 * that doesn't fit in 32bit, fail the setup rather than
		 * pretending to be successful.
		 */
		if (q_pfn >> 32) {
			dev_err(&vdev->dev,
				"platform bug: legacy virtio-mmio must not be used with RAM above 0x%llxGB\n",
				0x1ULL << (32 + PAGE_SHIFT - 30));
			err = -E2BIG;
			goto error_bad_pfn;
		}

		writel(PAGE_SIZE, vm_dev->base + VIRTIO_MMIO_QUEUE_ALIGN);
		writel(q_pfn, vm_dev->base + VIRTIO_MMIO_QUEUE_PFN);
	} else {
		u64 addr;

		addr = virtqueue_get_desc_addr(vq);
		writel((u32)addr, vm_dev->base + VIRTIO_MMIO_QUEUE_DESC_LOW);
		writel((u32)(addr >> 32),
				vm_dev->base + VIRTIO_MMIO_QUEUE_DESC_HIGH);

		addr = virtqueue_get_avail_addr(vq);
		writel((u32)addr, vm_dev->base + VIRTIO_MMIO_QUEUE_AVAIL_LOW);
		writel((u32)(addr >> 32),
				vm_dev->base + VIRTIO_MMIO_QUEUE_AVAIL_HIGH);

		addr = virtqueue_get_used_addr(vq);
		writel((u32)addr, vm_dev->base + VIRTIO_MMIO_QUEUE_USED_LOW);
		writel((u32)(addr >> 32),
				vm_dev->base + VIRTIO_MMIO_QUEUE_USED_HIGH);

		writel(1, vm_dev->base + VIRTIO_MMIO_QUEUE_READY);
	}

	vq->priv = info;
	info->vq = vq;

	spin_lock_irqsave(&vm_dev->lock, flags);
	list_add(&info->node, &vm_dev->virtqueues);
	spin_unlock_irqrestore(&vm_dev->lock, flags);

	return vq;

error_bad_pfn:
	vring_del_virtqueue(vq);
error_new_virtqueue:
	if (vm_dev->version == 1) {
		writel(0, vm_dev->base + VIRTIO_MMIO_QUEUE_PFN);
	} else {
		writel(0, vm_dev->base + VIRTIO_MMIO_QUEUE_READY);
		WARN_ON(readl(vm_dev->base + VIRTIO_MMIO_QUEUE_READY));
	}
	kfree(info);
error_kmalloc:
error_available:
	return ERR_PTR(err);
}

static int vm_find_vqs(struct virtio_device *vdev, unsigned int nvqs,
		       struct virtqueue *vqs[],
		       vq_callback_t *callbacks[],
		       const char * const names[],
		       const bool *ctx,
		       struct irq_affinity *desc)
{
	struct virtio_mmio_device *vm_dev = to_virtio_mmio_device(vdev);
	int irq = platform_get_irq(vm_dev->pdev, 0);
	int i, err, queue_idx = 0;

	if (irq < 0)
		return irq;

	err = request_irq(irq, vm_interrupt, IRQF_SHARED,
			dev_name(&vdev->dev), vm_dev);
	if (err)
		return err;

	if (of_property_read_bool(vm_dev->pdev->dev.of_node, "wakeup-source"))
		enable_irq_wake(irq);

	for (i = 0; i < nvqs; ++i) {
		if (!names[i]) {
			vqs[i] = NULL;
			continue;
		}

		vqs[i] = vm_setup_vq(vdev, queue_idx++, callbacks[i], names[i],
				     ctx ? ctx[i] : false);
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

static bool vm_get_shm_region(struct virtio_device *vdev,
			      struct virtio_shm_region *region, u8 id)
{
	struct virtio_mmio_device *vm_dev = to_virtio_mmio_device(vdev);
	u64 len, addr;

	/* Select the region we're interested in */
	writel(id, vm_dev->base + VIRTIO_MMIO_SHM_SEL);

	/* Read the region size */
	len = (u64) readl(vm_dev->base + VIRTIO_MMIO_SHM_LEN_LOW);
	len |= (u64) readl(vm_dev->base + VIRTIO_MMIO_SHM_LEN_HIGH) << 32;

	region->len = len;

	/* Check if region length is -1. If that's the case, the shared memory
	 * region does not exist and there is no need to proceed further.
	 */
	if (len == ~(u64)0)
		return false;

	/* Read the region base address */
	addr = (u64) readl(vm_dev->base + VIRTIO_MMIO_SHM_BASE_LOW);
	addr |= (u64) readl(vm_dev->base + VIRTIO_MMIO_SHM_BASE_HIGH) << 32;

	region->addr = addr;

	return true;
}

static const struct virtio_config_ops virtio_mmio_config_ops = {
	.get		= vm_get,
	.set		= vm_set,
	.generation	= vm_generation,
	.get_status	= vm_get_status,
	.set_status	= vm_set_status,
	.reset		= vm_reset,
	.find_vqs	= vm_find_vqs,
	.del_vqs	= vm_del_vqs,
	.get_features	= vm_get_features,
	.finalize_features = vm_finalize_features,
	.bus_name	= vm_bus_name,
	.get_shm_region = vm_get_shm_region,
	.synchronize_cbs = vm_synchronize_cbs,
};

#ifdef CONFIG_PM_SLEEP
static int virtio_mmio_freeze(struct device *dev)
{
	struct virtio_mmio_device *vm_dev = dev_get_drvdata(dev);

	return virtio_device_freeze(&vm_dev->vdev);
}

static int virtio_mmio_restore(struct device *dev)
{
	struct virtio_mmio_device *vm_dev = dev_get_drvdata(dev);

	if (vm_dev->version == 1)
		writel(PAGE_SIZE, vm_dev->base + VIRTIO_MMIO_GUEST_PAGE_SIZE);

	return virtio_device_restore(&vm_dev->vdev);
}

static const struct dev_pm_ops virtio_mmio_pm_ops = {
	.freeze         = virtio_mmio_freeze,
	.restore        = virtio_mmio_restore,
};
#endif

static void virtio_mmio_release_dev(struct device *_d)
{
	struct virtio_device *vdev =
			container_of(_d, struct virtio_device, dev);
	struct virtio_mmio_device *vm_dev = to_virtio_mmio_device(vdev);

	kfree(vm_dev);
}

#ifdef CONFIG_VIRTIO_MMIO_SWIOTLB
static phys_addr_t virtio_swiotlb_base;
static phys_addr_t virtio_swiotlb_dma_base;
static size_t virtio_swiotlb_size;

static int virtio_get_shm(struct device_node *np, phys_addr_t *base,
		phys_addr_t *dma_base, size_t *size)
{
	const __be64 *val;
	int len;
	struct device_node *shm_np;
	struct resource res_mem;
	int ret;

	shm_np = of_parse_phandle(np, "memory-region", 0);
	if (!shm_np) {
		pr_err("%s: Invalid memory-region\n", __func__);
		return -EINVAL;
	}

	ret = of_address_to_resource(shm_np, 0, &res_mem);
	if (ret) {
		pr_err("%s: of_address_to_resource failed ret %d\n", __func__, ret);
		return -EINVAL;
	}

	*base = res_mem.start;
	*size = resource_size(&res_mem);
	of_node_put(shm_np);

	if (!*base || !*size) {
		pr_err("%s: Invalid memory-region base %llx size %d\n", __func__, *base, *size);
		return -EINVAL;
	}

	val = of_get_property(np, "dma_base", &len);
	if (!val || len != 8) {
		pr_err("%s: Invalid dma_base prop val %llx size %d\n", __func__, val, len);
		return -EINVAL;
	}
	*dma_base = __be64_to_cpup(val);

	pr_debug("%s: shm base %llx size %llx dma_base %llx\n", __func__, *base, *size, *dma_base);

	return 0;
}

static int __init virtio_swiotlb_init(void)
{
	void __iomem *vbase;
	int ret;
	unsigned long nslabs;
	phys_addr_t base;
	phys_addr_t dma_base;
	size_t size;
	struct device_node *np;

	np = of_find_node_by_path("/swiotlb");
	if (!np)
		return 0;

	ret = virtio_get_shm(np, &base, &dma_base, &size);
	of_node_put(np);

	if (ret)
		return ret;

	nslabs = (size >> IO_TLB_SHIFT);
	nslabs = ALIGN_DOWN(nslabs, IO_TLB_SEGSIZE);

	if (!nslabs)
		return -EINVAL;

	vbase = ioremap_cache(base, size);
	if (!vbase)
		return -EINVAL;

	ret = swiotlb_late_init_with_tblpaddr(vbase, dma_base, nslabs);
	if (ret) {
		iounmap(vbase);
		return ret;
	}

	virtio_swiotlb_base = base;
	virtio_swiotlb_dma_base = dma_base;
	virtio_swiotlb_size = size;

	return 0;
}

static void *virtio_alloc_coherent(struct device *dev, size_t size,
		dma_addr_t *dma_handle, gfp_t flags, unsigned long attrs)
{
	struct platform_device *pdev =
				container_of(dev, struct platform_device, dev);
	struct virtio_mmio_device *vm_dev = platform_get_drvdata(pdev);
	int pageno;
	unsigned long irq_flags;
	int order = get_order(size);
	void *ret = NULL;
	struct virtio_mem_pool *mem = vm_dev->mem_pool;

	if (!mem || (size > (mem->size << PAGE_SHIFT)))
		return NULL;

	spin_lock_irqsave(&mem->lock, irq_flags);

	pageno = bitmap_find_free_region(mem->bitmap, mem->size, order);
	if (pageno >= 0) {
		*dma_handle = mem->dma_base + (pageno << PAGE_SHIFT);
		ret = mem->virt_base + (pageno << PAGE_SHIFT);
	}

	spin_unlock_irqrestore(&mem->lock, irq_flags);

	return ret;
}


static void virtio_free_coherent(struct device *dev, size_t size, void *vaddr,
				dma_addr_t dma_handle, unsigned long attrs)
{
	struct platform_device *pdev =
			container_of(dev, struct platform_device, dev);
	struct virtio_mmio_device *vm_dev = platform_get_drvdata(pdev);
	int pageno;
	unsigned long flags;
	struct virtio_mem_pool *mem = vm_dev->mem_pool;

	if (!mem)
		return;

	spin_lock_irqsave(&mem->lock, flags);
	if (vaddr >= mem->virt_base &&
			vaddr < (mem->virt_base + (mem->size << PAGE_SHIFT))) {
		pageno = (vaddr - mem->virt_base) >> PAGE_SHIFT;
		bitmap_release_region(mem->bitmap, pageno, get_order(size));
	}

	spin_unlock_irqrestore(&mem->lock, flags);
}

static dma_addr_t virtio_map_page(struct device *dev, struct page *page,
				unsigned long offset, size_t size,
				enum dma_data_direction dir,
				unsigned long attrs)
{
	phys_addr_t phys = page_to_phys(page) + offset;

	return swiotlb_map(dev, phys, size, dir, attrs);
}

static void virtio_unmap_page(struct device *dev, dma_addr_t dev_addr,
			size_t size, enum dma_data_direction dir,
			unsigned long attrs)
{
	BUG_ON(!is_swiotlb_buffer(dev, dev_addr));

	swiotlb_tbl_unmap_single(dev, dev_addr, size, dir, attrs);
}

size_t virtio_max_mapping_size(struct device *dev)
{
	return SZ_4K;
}

static const struct dma_map_ops virtio_dma_ops = {
	.alloc                  = virtio_alloc_coherent,
	.free                   = virtio_free_coherent,
	.map_page               = virtio_map_page,
	.unmap_page             = virtio_unmap_page,
	.max_mapping_size	= virtio_max_mapping_size,
};

static inline int
get_ring_base(struct platform_device *pdev, phys_addr_t *ring_base,
				phys_addr_t *ring_dma_base, size_t *ring_size)
{
	int ret;

	if (!virtio_swiotlb_base)
		return 0;

	ret = virtio_get_shm(pdev->dev.of_node, ring_base, ring_dma_base, ring_size);

	return ret ? 0 : 1;
}

static int setup_virtio_dma_ops(struct platform_device *pdev)
{
	struct virtio_mmio_device *vm_dev = platform_get_drvdata(pdev);
	phys_addr_t ring_base, ring_dma_base;
	size_t ring_size, pages;
	struct virtio_mem_pool *vmem_pool;
	unsigned long bitmap_size;

	if (!vm_dev || !get_ring_base(pdev, &ring_base,
					&ring_dma_base, &ring_size))
		return 0;

	vmem_pool = devm_kzalloc(&pdev->dev, sizeof(struct virtio_mem_pool),
				GFP_KERNEL);
	if (!vmem_pool)
		return -ENOMEM;

	pages = ring_size >> PAGE_SHIFT;
	if (!pages) {
		pr_err("%s: Ring size too small\n", __func__);
		return -EINVAL;
	}

	if (ULONG_MAX / sizeof(unsigned long) < BITS_TO_LONGS(pages)) {
		pr_err("%s: Ring size too large %lu\n", __func__, ring_size);
		return -EINVAL;
	}

	bitmap_size = BITS_TO_LONGS(pages) * sizeof(long);
	vmem_pool->bitmap = devm_kzalloc(&pdev->dev, bitmap_size, GFP_KERNEL);
	if (!vmem_pool->bitmap)
		return -ENOMEM;

	/* Note: Mapped as 'normal/cacheable' memory */
	vmem_pool->virt_base = ioremap_cache(ring_base, ring_size);
	if (!vmem_pool->virt_base) {
		pr_err("Unable to ioremap %pK size %lx\n",
					(void *)ring_base, ring_size);
		return -ENOMEM;
	}
	memset(vmem_pool->virt_base, 0, ring_size);

	vmem_pool->dma_base = ring_dma_base;
	vmem_pool->size = pages;
	spin_lock_init(&vmem_pool->lock);
	vm_dev->mem_pool = vmem_pool;
	set_dma_ops(&pdev->dev, &virtio_dma_ops);

	dev_dbg(&pdev->dev, "virtio_mem_pool: virt_base %llx pages %lx\n",
					vmem_pool->virt_base, pages);
	return 0;
}
#else	/* CONFIG_VIRTIO_MMIO_SWIOTLB */

static inline int setup_virtio_dma_ops(struct platform_device *pdev)
{
	return 0;
}

static inline int virtio_swiotlb_init(void)
{
	return 0;
}

#endif	/* CONFIG_VIRTIO_MMIO_SWIOTLB */

/* Platform device */
static int virtio_mmio_probe(struct platform_device *pdev)
{
	struct virtio_mmio_device *vm_dev;
	unsigned long magic;
	int rc;

	vm_dev = kzalloc(sizeof(*vm_dev), GFP_KERNEL);
	if (!vm_dev)
		return -ENOMEM;

	vm_dev->vdev.dev.parent = &pdev->dev;
	vm_dev->vdev.dev.release = virtio_mmio_release_dev;
	vm_dev->vdev.config = &virtio_mmio_config_ops;
	vm_dev->pdev = pdev;
	INIT_LIST_HEAD(&vm_dev->virtqueues);
	spin_lock_init(&vm_dev->lock);

	vm_dev->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(vm_dev->base)) {
		rc = PTR_ERR(vm_dev->base);
		goto free_vm_dev;
	}

	/* Check magic value */
	magic = readl(vm_dev->base + VIRTIO_MMIO_MAGIC_VALUE);
	if (magic != ('v' | 'i' << 8 | 'r' << 16 | 't' << 24)) {
		dev_warn(&pdev->dev, "Wrong magic value 0x%08lx!\n", magic);
		rc = -ENODEV;
		goto free_vm_dev;
	}

	/* Check device version */
	vm_dev->version = readl(vm_dev->base + VIRTIO_MMIO_VERSION);
	if (vm_dev->version < 1 || vm_dev->version > 2) {
		dev_err(&pdev->dev, "Version %ld not supported!\n",
				vm_dev->version);
		rc = -ENXIO;
		goto free_vm_dev;
	}

	vm_dev->vdev.id.device = readl(vm_dev->base + VIRTIO_MMIO_DEVICE_ID);
	if (vm_dev->vdev.id.device == 0) {
		/*
		 * virtio-mmio device with an ID 0 is a (dummy) placeholder
		 * with no function. End probing now with no error reported.
		 */
		rc = -ENODEV;
		goto free_vm_dev;
	}
	vm_dev->vdev.id.vendor = readl(vm_dev->base + VIRTIO_MMIO_VENDOR_ID);

	if (vm_dev->version == 1) {
		writel(PAGE_SIZE, vm_dev->base + VIRTIO_MMIO_GUEST_PAGE_SIZE);

		rc = dma_set_mask(&pdev->dev, DMA_BIT_MASK(64));
		/*
		 * In the legacy case, ensure our coherently-allocated virtio
		 * ring will be at an address expressable as a 32-bit PFN.
		 */
		if (!rc)
			dma_set_coherent_mask(&pdev->dev,
					      DMA_BIT_MASK(32 + PAGE_SHIFT));
	} else {
		rc = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64));
	}
	if (rc)
		rc = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
	if (rc)
		dev_warn(&pdev->dev, "Failed to enable 64-bit or 32-bit DMA.  Trying to continue, but this might not work.\n");

	platform_set_drvdata(pdev, vm_dev);

	rc = setup_virtio_dma_ops(pdev);
	if (rc) {
		put_device(&vm_dev->vdev.dev);
		return rc;
	}

	rc = register_virtio_device(&vm_dev->vdev);
	if (rc)
		put_device(&vm_dev->vdev.dev);

	return rc;

free_vm_dev:
	kfree(vm_dev);
	return rc;
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
	long long base, size;
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
	 * sscanf() must process at least 2 chunks; also there
	 * must be no extra characters after the last chunk, so
	 * str[consumed] must be '\0'
	 */
	if (processed < 2 || str[consumed] || irq == 0)
		return -EINVAL;

	resources[0].flags = IORESOURCE_MEM;
	resources[0].start = base;
	resources[0].end = base + size - 1;

	resources[1].flags = IORESOURCE_IRQ;
	resources[1].start = resources[1].end = irq;

	if (!vm_cmdline_parent_registered) {
		err = device_register(&vm_cmdline_parent);
		if (err) {
			put_device(&vm_cmdline_parent);
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

	return PTR_ERR_OR_ZERO(pdev);
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

static const struct kernel_param_ops vm_cmdline_param_ops = {
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

static const struct of_device_id virtio_mmio_match[] = {
	{ .compatible = "virtio,mmio", },
	{},
};
MODULE_DEVICE_TABLE(of, virtio_mmio_match);

#ifdef CONFIG_ACPI
static const struct acpi_device_id virtio_mmio_acpi_match[] = {
	{ "LNRO0005", },
	{ }
};
MODULE_DEVICE_TABLE(acpi, virtio_mmio_acpi_match);
#endif

static struct platform_driver virtio_mmio_driver = {
	.probe		= virtio_mmio_probe,
	.remove		= virtio_mmio_remove,
	.driver		= {
		.name	= "virtio-mmio",
		.of_match_table	= virtio_mmio_match,
		.acpi_match_table = ACPI_PTR(virtio_mmio_acpi_match),
#if IS_ENABLED(CONFIG_PM_SLEEP) && !IS_ENABLED(CONFIG_VIRTIO_MMIO_SWIOTLB)
		.pm	= &virtio_mmio_pm_ops,
#endif
	},
};

static int __init virtio_mmio_init(void)
{
	int ret;

	ret = virtio_swiotlb_init();
	if (ret)
		return ret;

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

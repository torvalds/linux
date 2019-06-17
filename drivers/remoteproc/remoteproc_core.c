/*
 * Remote Processor Framework
 *
 * Copyright (C) 2011 Texas Instruments, Inc.
 * Copyright (C) 2011 Google, Inc.
 *
 * Ohad Ben-Cohen <ohad@wizery.com>
 * Brian Swetland <swetland@google.com>
 * Mark Grosen <mgrosen@ti.com>
 * Fernando Guzman Lugo <fernando.lugo@ti.com>
 * Suman Anna <s-anna@ti.com>
 * Robert Tivy <rtivy@ti.com>
 * Armando Uribe De Leon <x0095078@ti.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#define pr_fmt(fmt)    "%s: " fmt, __func__

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/dma-mapping.h>
#include <linux/firmware.h>
#include <linux/string.h>
#include <linux/debugfs.h>
#include <linux/devcoredump.h>
#include <linux/remoteproc.h>
#include <linux/iommu.h>
#include <linux/idr.h>
#include <linux/elf.h>
#include <linux/crc32.h>
#include <linux/of_reserved_mem.h>
#include <linux/virtio_ids.h>
#include <linux/virtio_ring.h>
#include <asm/byteorder.h>
#include <linux/platform_device.h>

#include "remoteproc_internal.h"

#define HIGH_BITS_MASK 0xFFFFFFFF00000000ULL

static DEFINE_MUTEX(rproc_list_mutex);
static LIST_HEAD(rproc_list);

typedef int (*rproc_handle_resources_t)(struct rproc *rproc,
				struct resource_table *table, int len);
typedef int (*rproc_handle_resource_t)(struct rproc *rproc,
				 void *, int offset, int avail);

static int rproc_alloc_carveout(struct rproc *rproc,
				struct rproc_mem_entry *mem);
static int rproc_release_carveout(struct rproc *rproc,
				  struct rproc_mem_entry *mem);

/* Unique indices for remoteproc devices */
static DEFINE_IDA(rproc_dev_index);

static const char * const rproc_crash_names[] = {
	[RPROC_MMUFAULT]	= "mmufault",
	[RPROC_WATCHDOG]	= "watchdog",
	[RPROC_FATAL_ERROR]	= "fatal error",
};

/* translate rproc_crash_type to string */
static const char *rproc_crash_to_string(enum rproc_crash_type type)
{
	if (type < ARRAY_SIZE(rproc_crash_names))
		return rproc_crash_names[type];
	return "unknown";
}

/*
 * This is the IOMMU fault handler we register with the IOMMU API
 * (when relevant; not all remote processors access memory through
 * an IOMMU).
 *
 * IOMMU core will invoke this handler whenever the remote processor
 * will try to access an unmapped device address.
 */
static int rproc_iommu_fault(struct iommu_domain *domain, struct device *dev,
			     unsigned long iova, int flags, void *token)
{
	struct rproc *rproc = token;

	dev_err(dev, "iommu fault: da 0x%lx flags 0x%x\n", iova, flags);

	rproc_report_crash(rproc, RPROC_MMUFAULT);

	/*
	 * Let the iommu core know we're not really handling this fault;
	 * we just used it as a recovery trigger.
	 */
	return -ENOSYS;
}

static int rproc_enable_iommu(struct rproc *rproc)
{
	struct iommu_domain *domain;
	struct device *dev = rproc->dev.parent;
	int ret;

	if (!rproc->has_iommu) {
		dev_dbg(dev, "iommu not present\n");
		return 0;
	}

	domain = iommu_domain_alloc(dev->bus);
	if (!domain) {
		dev_err(dev, "can't alloc iommu domain\n");
		return -ENOMEM;
	}

	iommu_set_fault_handler(domain, rproc_iommu_fault, rproc);

	ret = iommu_attach_device(domain, dev);
	if (ret) {
		dev_err(dev, "can't attach iommu device: %d\n", ret);
		goto free_domain;
	}

	rproc->domain = domain;

	return 0;

free_domain:
	iommu_domain_free(domain);
	return ret;
}

static void rproc_disable_iommu(struct rproc *rproc)
{
	struct iommu_domain *domain = rproc->domain;
	struct device *dev = rproc->dev.parent;

	if (!domain)
		return;

	iommu_detach_device(domain, dev);
	iommu_domain_free(domain);
}

phys_addr_t rproc_va_to_pa(void *cpu_addr)
{
	/*
	 * Return physical address according to virtual address location
	 * - in vmalloc: if region ioremapped or defined as dma_alloc_coherent
	 * - in kernel: if region allocated in generic dma memory pool
	 */
	if (is_vmalloc_addr(cpu_addr)) {
		return page_to_phys(vmalloc_to_page(cpu_addr)) +
				    offset_in_page(cpu_addr);
	}

	WARN_ON(!virt_addr_valid(cpu_addr));
	return virt_to_phys(cpu_addr);
}
EXPORT_SYMBOL(rproc_va_to_pa);

/**
 * rproc_da_to_va() - lookup the kernel virtual address for a remoteproc address
 * @rproc: handle of a remote processor
 * @da: remoteproc device address to translate
 * @len: length of the memory region @da is pointing to
 *
 * Some remote processors will ask us to allocate them physically contiguous
 * memory regions (which we call "carveouts"), and map them to specific
 * device addresses (which are hardcoded in the firmware). They may also have
 * dedicated memory regions internal to the processors, and use them either
 * exclusively or alongside carveouts.
 *
 * They may then ask us to copy objects into specific device addresses (e.g.
 * code/data sections) or expose us certain symbols in other device address
 * (e.g. their trace buffer).
 *
 * This function is a helper function with which we can go over the allocated
 * carveouts and translate specific device addresses to kernel virtual addresses
 * so we can access the referenced memory. This function also allows to perform
 * translations on the internal remoteproc memory regions through a platform
 * implementation specific da_to_va ops, if present.
 *
 * The function returns a valid kernel address on success or NULL on failure.
 *
 * Note: phys_to_virt(iommu_iova_to_phys(rproc->domain, da)) will work too,
 * but only on kernel direct mapped RAM memory. Instead, we're just using
 * here the output of the DMA API for the carveouts, which should be more
 * correct.
 */
void *rproc_da_to_va(struct rproc *rproc, u64 da, int len)
{
	struct rproc_mem_entry *carveout;
	void *ptr = NULL;

	if (rproc->ops->da_to_va) {
		ptr = rproc->ops->da_to_va(rproc, da, len);
		if (ptr)
			goto out;
	}

	list_for_each_entry(carveout, &rproc->carveouts, node) {
		int offset = da - carveout->da;

		/*  Verify that carveout is allocated */
		if (!carveout->va)
			continue;

		/* try next carveout if da is too small */
		if (offset < 0)
			continue;

		/* try next carveout if da is too large */
		if (offset + len > carveout->len)
			continue;

		ptr = carveout->va + offset;

		break;
	}

out:
	return ptr;
}
EXPORT_SYMBOL(rproc_da_to_va);

/**
 * rproc_find_carveout_by_name() - lookup the carveout region by a name
 * @rproc: handle of a remote processor
 * @name,..: carveout name to find (standard printf format)
 *
 * Platform driver has the capability to register some pre-allacoted carveout
 * (physically contiguous memory regions) before rproc firmware loading and
 * associated resource table analysis. These regions may be dedicated memory
 * regions internal to the coprocessor or specified DDR region with specific
 * attributes
 *
 * This function is a helper function with which we can go over the
 * allocated carveouts and return associated region characteristics like
 * coprocessor address, length or processor virtual address.
 *
 * Return: a valid pointer on carveout entry on success or NULL on failure.
 */
struct rproc_mem_entry *
rproc_find_carveout_by_name(struct rproc *rproc, const char *name, ...)
{
	va_list args;
	char _name[32];
	struct rproc_mem_entry *carveout, *mem = NULL;

	if (!name)
		return NULL;

	va_start(args, name);
	vsnprintf(_name, sizeof(_name), name, args);
	va_end(args);

	list_for_each_entry(carveout, &rproc->carveouts, node) {
		/* Compare carveout and requested names */
		if (!strcmp(carveout->name, _name)) {
			mem = carveout;
			break;
		}
	}

	return mem;
}

/**
 * rproc_check_carveout_da() - Check specified carveout da configuration
 * @rproc: handle of a remote processor
 * @mem: pointer on carveout to check
 * @da: area device address
 * @len: associated area size
 *
 * This function is a helper function to verify requested device area (couple
 * da, len) is part of specified carveout.
 * If da is not set (defined as FW_RSC_ADDR_ANY), only requested length is
 * checked.
 *
 * Return: 0 if carveout matches request else error
 */
static int rproc_check_carveout_da(struct rproc *rproc,
				   struct rproc_mem_entry *mem, u32 da, u32 len)
{
	struct device *dev = &rproc->dev;
	int delta;

	/* Check requested resource length */
	if (len > mem->len) {
		dev_err(dev, "Registered carveout doesn't fit len request\n");
		return -EINVAL;
	}

	if (da != FW_RSC_ADDR_ANY && mem->da == FW_RSC_ADDR_ANY) {
		/* Address doesn't match registered carveout configuration */
		return -EINVAL;
	} else if (da != FW_RSC_ADDR_ANY && mem->da != FW_RSC_ADDR_ANY) {
		delta = da - mem->da;

		/* Check requested resource belongs to registered carveout */
		if (delta < 0) {
			dev_err(dev,
				"Registered carveout doesn't fit da request\n");
			return -EINVAL;
		}

		if (delta + len > mem->len) {
			dev_err(dev,
				"Registered carveout doesn't fit len request\n");
			return -EINVAL;
		}
	}

	return 0;
}

int rproc_alloc_vring(struct rproc_vdev *rvdev, int i)
{
	struct rproc *rproc = rvdev->rproc;
	struct device *dev = &rproc->dev;
	struct rproc_vring *rvring = &rvdev->vring[i];
	struct fw_rsc_vdev *rsc;
	int ret, size, notifyid;
	struct rproc_mem_entry *mem;

	/* actual size of vring (in bytes) */
	size = PAGE_ALIGN(vring_size(rvring->len, rvring->align));

	rsc = (void *)rproc->table_ptr + rvdev->rsc_offset;

	/* Search for pre-registered carveout */
	mem = rproc_find_carveout_by_name(rproc, "vdev%dvring%d", rvdev->index,
					  i);
	if (mem) {
		if (rproc_check_carveout_da(rproc, mem, rsc->vring[i].da, size))
			return -ENOMEM;
	} else {
		/* Register carveout in in list */
		mem = rproc_mem_entry_init(dev, 0, 0, size, rsc->vring[i].da,
					   rproc_alloc_carveout,
					   rproc_release_carveout,
					   "vdev%dvring%d",
					   rvdev->index, i);
		if (!mem) {
			dev_err(dev, "Can't allocate memory entry structure\n");
			return -ENOMEM;
		}

		rproc_add_carveout(rproc, mem);
	}

	/*
	 * Assign an rproc-wide unique index for this vring
	 * TODO: assign a notifyid for rvdev updates as well
	 * TODO: support predefined notifyids (via resource table)
	 */
	ret = idr_alloc(&rproc->notifyids, rvring, 0, 0, GFP_KERNEL);
	if (ret < 0) {
		dev_err(dev, "idr_alloc failed: %d\n", ret);
		return ret;
	}
	notifyid = ret;

	/* Potentially bump max_notifyid */
	if (notifyid > rproc->max_notifyid)
		rproc->max_notifyid = notifyid;

	rvring->notifyid = notifyid;

	/* Let the rproc know the notifyid of this vring.*/
	rsc->vring[i].notifyid = notifyid;
	return 0;
}

static int
rproc_parse_vring(struct rproc_vdev *rvdev, struct fw_rsc_vdev *rsc, int i)
{
	struct rproc *rproc = rvdev->rproc;
	struct device *dev = &rproc->dev;
	struct fw_rsc_vdev_vring *vring = &rsc->vring[i];
	struct rproc_vring *rvring = &rvdev->vring[i];

	dev_dbg(dev, "vdev rsc: vring%d: da 0x%x, qsz %d, align %d\n",
		i, vring->da, vring->num, vring->align);

	/* verify queue size and vring alignment are sane */
	if (!vring->num || !vring->align) {
		dev_err(dev, "invalid qsz (%d) or alignment (%d)\n",
			vring->num, vring->align);
		return -EINVAL;
	}

	rvring->len = vring->num;
	rvring->align = vring->align;
	rvring->rvdev = rvdev;

	return 0;
}

void rproc_free_vring(struct rproc_vring *rvring)
{
	struct rproc *rproc = rvring->rvdev->rproc;
	int idx = rvring->rvdev->vring - rvring;
	struct fw_rsc_vdev *rsc;

	idr_remove(&rproc->notifyids, rvring->notifyid);

	/* reset resource entry info */
	rsc = (void *)rproc->table_ptr + rvring->rvdev->rsc_offset;
	rsc->vring[idx].da = 0;
	rsc->vring[idx].notifyid = -1;
}

static int rproc_vdev_do_start(struct rproc_subdev *subdev)
{
	struct rproc_vdev *rvdev = container_of(subdev, struct rproc_vdev, subdev);

	return rproc_add_virtio_dev(rvdev, rvdev->id);
}

static void rproc_vdev_do_stop(struct rproc_subdev *subdev, bool crashed)
{
	struct rproc_vdev *rvdev = container_of(subdev, struct rproc_vdev, subdev);
	int ret;

	ret = device_for_each_child(&rvdev->dev, NULL, rproc_remove_virtio_dev);
	if (ret)
		dev_warn(&rvdev->dev, "can't remove vdev child device: %d\n", ret);
}

/**
 * rproc_rvdev_release() - release the existence of a rvdev
 *
 * @dev: the subdevice's dev
 */
static void rproc_rvdev_release(struct device *dev)
{
	struct rproc_vdev *rvdev = container_of(dev, struct rproc_vdev, dev);

	of_reserved_mem_device_release(dev);

	kfree(rvdev);
}

/**
 * rproc_handle_vdev() - handle a vdev fw resource
 * @rproc: the remote processor
 * @rsc: the vring resource descriptor
 * @avail: size of available data (for sanity checking the image)
 *
 * This resource entry requests the host to statically register a virtio
 * device (vdev), and setup everything needed to support it. It contains
 * everything needed to make it possible: the virtio device id, virtio
 * device features, vrings information, virtio config space, etc...
 *
 * Before registering the vdev, the vrings are allocated from non-cacheable
 * physically contiguous memory. Currently we only support two vrings per
 * remote processor (temporary limitation). We might also want to consider
 * doing the vring allocation only later when ->find_vqs() is invoked, and
 * then release them upon ->del_vqs().
 *
 * Note: @da is currently not really handled correctly: we dynamically
 * allocate it using the DMA API, ignoring requested hard coded addresses,
 * and we don't take care of any required IOMMU programming. This is all
 * going to be taken care of when the generic iommu-based DMA API will be
 * merged. Meanwhile, statically-addressed iommu-based firmware images should
 * use RSC_DEVMEM resource entries to map their required @da to the physical
 * address of their base CMA region (ouch, hacky!).
 *
 * Returns 0 on success, or an appropriate error code otherwise
 */
static int rproc_handle_vdev(struct rproc *rproc, struct fw_rsc_vdev *rsc,
			     int offset, int avail)
{
	struct device *dev = &rproc->dev;
	struct rproc_vdev *rvdev;
	int i, ret;
	char name[16];

	/* make sure resource isn't truncated */
	if (sizeof(*rsc) + rsc->num_of_vrings * sizeof(struct fw_rsc_vdev_vring)
			+ rsc->config_len > avail) {
		dev_err(dev, "vdev rsc is truncated\n");
		return -EINVAL;
	}

	/* make sure reserved bytes are zeroes */
	if (rsc->reserved[0] || rsc->reserved[1]) {
		dev_err(dev, "vdev rsc has non zero reserved bytes\n");
		return -EINVAL;
	}

	dev_dbg(dev, "vdev rsc: id %d, dfeatures 0x%x, cfg len %d, %d vrings\n",
		rsc->id, rsc->dfeatures, rsc->config_len, rsc->num_of_vrings);

	/* we currently support only two vrings per rvdev */
	if (rsc->num_of_vrings > ARRAY_SIZE(rvdev->vring)) {
		dev_err(dev, "too many vrings: %d\n", rsc->num_of_vrings);
		return -EINVAL;
	}

	rvdev = kzalloc(sizeof(*rvdev), GFP_KERNEL);
	if (!rvdev)
		return -ENOMEM;

	kref_init(&rvdev->refcount);

	rvdev->id = rsc->id;
	rvdev->rproc = rproc;
	rvdev->index = rproc->nb_vdev++;

	/* Initialise vdev subdevice */
	snprintf(name, sizeof(name), "vdev%dbuffer", rvdev->index);
	rvdev->dev.parent = rproc->dev.parent;
	rvdev->dev.release = rproc_rvdev_release;
	dev_set_name(&rvdev->dev, "%s#%s", dev_name(rvdev->dev.parent), name);
	dev_set_drvdata(&rvdev->dev, rvdev);

	ret = device_register(&rvdev->dev);
	if (ret) {
		put_device(&rvdev->dev);
		return ret;
	}
	/* Make device dma capable by inheriting from parent's capabilities */
	set_dma_ops(&rvdev->dev, get_dma_ops(rproc->dev.parent));

	ret = dma_coerce_mask_and_coherent(&rvdev->dev,
					   dma_get_mask(rproc->dev.parent));
	if (ret) {
		dev_warn(dev,
			 "Failed to set DMA mask %llx. Trying to continue... %x\n",
			 dma_get_mask(rproc->dev.parent), ret);
	}

	/* parse the vrings */
	for (i = 0; i < rsc->num_of_vrings; i++) {
		ret = rproc_parse_vring(rvdev, rsc, i);
		if (ret)
			goto free_rvdev;
	}

	/* remember the resource offset*/
	rvdev->rsc_offset = offset;

	/* allocate the vring resources */
	for (i = 0; i < rsc->num_of_vrings; i++) {
		ret = rproc_alloc_vring(rvdev, i);
		if (ret)
			goto unwind_vring_allocations;
	}

	list_add_tail(&rvdev->node, &rproc->rvdevs);

	rvdev->subdev.start = rproc_vdev_do_start;
	rvdev->subdev.stop = rproc_vdev_do_stop;

	rproc_add_subdev(rproc, &rvdev->subdev);

	return 0;

unwind_vring_allocations:
	for (i--; i >= 0; i--)
		rproc_free_vring(&rvdev->vring[i]);
free_rvdev:
	device_unregister(&rvdev->dev);
	return ret;
}

void rproc_vdev_release(struct kref *ref)
{
	struct rproc_vdev *rvdev = container_of(ref, struct rproc_vdev, refcount);
	struct rproc_vring *rvring;
	struct rproc *rproc = rvdev->rproc;
	int id;

	for (id = 0; id < ARRAY_SIZE(rvdev->vring); id++) {
		rvring = &rvdev->vring[id];
		rproc_free_vring(rvring);
	}

	rproc_remove_subdev(rproc, &rvdev->subdev);
	list_del(&rvdev->node);
	device_unregister(&rvdev->dev);
}

/**
 * rproc_handle_trace() - handle a shared trace buffer resource
 * @rproc: the remote processor
 * @rsc: the trace resource descriptor
 * @avail: size of available data (for sanity checking the image)
 *
 * In case the remote processor dumps trace logs into memory,
 * export it via debugfs.
 *
 * Currently, the 'da' member of @rsc should contain the device address
 * where the remote processor is dumping the traces. Later we could also
 * support dynamically allocating this address using the generic
 * DMA API (but currently there isn't a use case for that).
 *
 * Returns 0 on success, or an appropriate error code otherwise
 */
static int rproc_handle_trace(struct rproc *rproc, struct fw_rsc_trace *rsc,
			      int offset, int avail)
{
	struct rproc_debug_trace *trace;
	struct device *dev = &rproc->dev;
	char name[15];

	if (sizeof(*rsc) > avail) {
		dev_err(dev, "trace rsc is truncated\n");
		return -EINVAL;
	}

	/* make sure reserved bytes are zeroes */
	if (rsc->reserved) {
		dev_err(dev, "trace rsc has non zero reserved bytes\n");
		return -EINVAL;
	}

	trace = kzalloc(sizeof(*trace), GFP_KERNEL);
	if (!trace)
		return -ENOMEM;

	/* set the trace buffer dma properties */
	trace->trace_mem.len = rsc->len;
	trace->trace_mem.da = rsc->da;

	/* set pointer on rproc device */
	trace->rproc = rproc;

	/* make sure snprintf always null terminates, even if truncating */
	snprintf(name, sizeof(name), "trace%d", rproc->num_traces);

	/* create the debugfs entry */
	trace->tfile = rproc_create_trace_file(name, rproc, trace);
	if (!trace->tfile) {
		kfree(trace);
		return -EINVAL;
	}

	list_add_tail(&trace->node, &rproc->traces);

	rproc->num_traces++;

	dev_dbg(dev, "%s added: da 0x%x, len 0x%x\n",
		name, rsc->da, rsc->len);

	return 0;
}

/**
 * rproc_handle_devmem() - handle devmem resource entry
 * @rproc: remote processor handle
 * @rsc: the devmem resource entry
 * @avail: size of available data (for sanity checking the image)
 *
 * Remote processors commonly need to access certain on-chip peripherals.
 *
 * Some of these remote processors access memory via an iommu device,
 * and might require us to configure their iommu before they can access
 * the on-chip peripherals they need.
 *
 * This resource entry is a request to map such a peripheral device.
 *
 * These devmem entries will contain the physical address of the device in
 * the 'pa' member. If a specific device address is expected, then 'da' will
 * contain it (currently this is the only use case supported). 'len' will
 * contain the size of the physical region we need to map.
 *
 * Currently we just "trust" those devmem entries to contain valid physical
 * addresses, but this is going to change: we want the implementations to
 * tell us ranges of physical addresses the firmware is allowed to request,
 * and not allow firmwares to request access to physical addresses that
 * are outside those ranges.
 */
static int rproc_handle_devmem(struct rproc *rproc, struct fw_rsc_devmem *rsc,
			       int offset, int avail)
{
	struct rproc_mem_entry *mapping;
	struct device *dev = &rproc->dev;
	int ret;

	/* no point in handling this resource without a valid iommu domain */
	if (!rproc->domain)
		return -EINVAL;

	if (sizeof(*rsc) > avail) {
		dev_err(dev, "devmem rsc is truncated\n");
		return -EINVAL;
	}

	/* make sure reserved bytes are zeroes */
	if (rsc->reserved) {
		dev_err(dev, "devmem rsc has non zero reserved bytes\n");
		return -EINVAL;
	}

	mapping = kzalloc(sizeof(*mapping), GFP_KERNEL);
	if (!mapping)
		return -ENOMEM;

	ret = iommu_map(rproc->domain, rsc->da, rsc->pa, rsc->len, rsc->flags);
	if (ret) {
		dev_err(dev, "failed to map devmem: %d\n", ret);
		goto out;
	}

	/*
	 * We'll need this info later when we'll want to unmap everything
	 * (e.g. on shutdown).
	 *
	 * We can't trust the remote processor not to change the resource
	 * table, so we must maintain this info independently.
	 */
	mapping->da = rsc->da;
	mapping->len = rsc->len;
	list_add_tail(&mapping->node, &rproc->mappings);

	dev_dbg(dev, "mapped devmem pa 0x%x, da 0x%x, len 0x%x\n",
		rsc->pa, rsc->da, rsc->len);

	return 0;

out:
	kfree(mapping);
	return ret;
}

/**
 * rproc_alloc_carveout() - allocated specified carveout
 * @rproc: rproc handle
 * @mem: the memory entry to allocate
 *
 * This function allocate specified memory entry @mem using
 * dma_alloc_coherent() as default allocator
 */
static int rproc_alloc_carveout(struct rproc *rproc,
				struct rproc_mem_entry *mem)
{
	struct rproc_mem_entry *mapping = NULL;
	struct device *dev = &rproc->dev;
	dma_addr_t dma;
	void *va;
	int ret;

	va = dma_alloc_coherent(dev->parent, mem->len, &dma, GFP_KERNEL);
	if (!va) {
		dev_err(dev->parent,
			"failed to allocate dma memory: len 0x%x\n", mem->len);
		return -ENOMEM;
	}

	dev_dbg(dev, "carveout va %pK, dma %pad, len 0x%x\n",
		va, &dma, mem->len);

	if (mem->da != FW_RSC_ADDR_ANY && !rproc->domain) {
		/*
		 * Check requested da is equal to dma address
		 * and print a warn message in case of missalignment.
		 * Don't stop rproc_start sequence as coprocessor may
		 * build pa to da translation on its side.
		 */
		if (mem->da != (u32)dma)
			dev_warn(dev->parent,
				 "Allocated carveout doesn't fit device address request\n");
	}

	/*
	 * Ok, this is non-standard.
	 *
	 * Sometimes we can't rely on the generic iommu-based DMA API
	 * to dynamically allocate the device address and then set the IOMMU
	 * tables accordingly, because some remote processors might
	 * _require_ us to use hard coded device addresses that their
	 * firmware was compiled with.
	 *
	 * In this case, we must use the IOMMU API directly and map
	 * the memory to the device address as expected by the remote
	 * processor.
	 *
	 * Obviously such remote processor devices should not be configured
	 * to use the iommu-based DMA API: we expect 'dma' to contain the
	 * physical address in this case.
	 */
	if (mem->da != FW_RSC_ADDR_ANY && rproc->domain) {
		mapping = kzalloc(sizeof(*mapping), GFP_KERNEL);
		if (!mapping) {
			ret = -ENOMEM;
			goto dma_free;
		}

		ret = iommu_map(rproc->domain, mem->da, dma, mem->len,
				mem->flags);
		if (ret) {
			dev_err(dev, "iommu_map failed: %d\n", ret);
			goto free_mapping;
		}

		/*
		 * We'll need this info later when we'll want to unmap
		 * everything (e.g. on shutdown).
		 *
		 * We can't trust the remote processor not to change the
		 * resource table, so we must maintain this info independently.
		 */
		mapping->da = mem->da;
		mapping->len = mem->len;
		list_add_tail(&mapping->node, &rproc->mappings);

		dev_dbg(dev, "carveout mapped 0x%x to %pad\n",
			mem->da, &dma);
	}

	if (mem->da == FW_RSC_ADDR_ANY) {
		/* Update device address as undefined by requester */
		if ((u64)dma & HIGH_BITS_MASK)
			dev_warn(dev, "DMA address cast in 32bit to fit resource table format\n");

		mem->da = (u32)dma;
	}

	mem->dma = dma;
	mem->va = va;

	return 0;

free_mapping:
	kfree(mapping);
dma_free:
	dma_free_coherent(dev->parent, mem->len, va, dma);
	return ret;
}

/**
 * rproc_release_carveout() - release acquired carveout
 * @rproc: rproc handle
 * @mem: the memory entry to release
 *
 * This function releases specified memory entry @mem allocated via
 * rproc_alloc_carveout() function by @rproc.
 */
static int rproc_release_carveout(struct rproc *rproc,
				  struct rproc_mem_entry *mem)
{
	struct device *dev = &rproc->dev;

	/* clean up carveout allocations */
	dma_free_coherent(dev->parent, mem->len, mem->va, mem->dma);
	return 0;
}

/**
 * rproc_handle_carveout() - handle phys contig memory allocation requests
 * @rproc: rproc handle
 * @rsc: the resource entry
 * @avail: size of available data (for image validation)
 *
 * This function will handle firmware requests for allocation of physically
 * contiguous memory regions.
 *
 * These request entries should come first in the firmware's resource table,
 * as other firmware entries might request placing other data objects inside
 * these memory regions (e.g. data/code segments, trace resource entries, ...).
 *
 * Allocating memory this way helps utilizing the reserved physical memory
 * (e.g. CMA) more efficiently, and also minimizes the number of TLB entries
 * needed to map it (in case @rproc is using an IOMMU). Reducing the TLB
 * pressure is important; it may have a substantial impact on performance.
 */
static int rproc_handle_carveout(struct rproc *rproc,
				 struct fw_rsc_carveout *rsc,
				 int offset, int avail)
{
	struct rproc_mem_entry *carveout;
	struct device *dev = &rproc->dev;

	if (sizeof(*rsc) > avail) {
		dev_err(dev, "carveout rsc is truncated\n");
		return -EINVAL;
	}

	/* make sure reserved bytes are zeroes */
	if (rsc->reserved) {
		dev_err(dev, "carveout rsc has non zero reserved bytes\n");
		return -EINVAL;
	}

	dev_dbg(dev, "carveout rsc: name: %s, da 0x%x, pa 0x%x, len 0x%x, flags 0x%x\n",
		rsc->name, rsc->da, rsc->pa, rsc->len, rsc->flags);

	/*
	 * Check carveout rsc already part of a registered carveout,
	 * Search by name, then check the da and length
	 */
	carveout = rproc_find_carveout_by_name(rproc, rsc->name);

	if (carveout) {
		if (carveout->rsc_offset != FW_RSC_ADDR_ANY) {
			dev_err(dev,
				"Carveout already associated to resource table\n");
			return -ENOMEM;
		}

		if (rproc_check_carveout_da(rproc, carveout, rsc->da, rsc->len))
			return -ENOMEM;

		/* Update memory carveout with resource table info */
		carveout->rsc_offset = offset;
		carveout->flags = rsc->flags;

		return 0;
	}

	/* Register carveout in in list */
	carveout = rproc_mem_entry_init(dev, 0, 0, rsc->len, rsc->da,
					rproc_alloc_carveout,
					rproc_release_carveout, rsc->name);
	if (!carveout) {
		dev_err(dev, "Can't allocate memory entry structure\n");
		return -ENOMEM;
	}

	carveout->flags = rsc->flags;
	carveout->rsc_offset = offset;
	rproc_add_carveout(rproc, carveout);

	return 0;
}

/**
 * rproc_add_carveout() - register an allocated carveout region
 * @rproc: rproc handle
 * @mem: memory entry to register
 *
 * This function registers specified memory entry in @rproc carveouts list.
 * Specified carveout should have been allocated before registering.
 */
void rproc_add_carveout(struct rproc *rproc, struct rproc_mem_entry *mem)
{
	list_add_tail(&mem->node, &rproc->carveouts);
}
EXPORT_SYMBOL(rproc_add_carveout);

/**
 * rproc_mem_entry_init() - allocate and initialize rproc_mem_entry struct
 * @dev: pointer on device struct
 * @va: virtual address
 * @dma: dma address
 * @len: memory carveout length
 * @da: device address
 * @alloc: memory carveout allocation function
 * @release: memory carveout release function
 * @name: carveout name
 *
 * This function allocates a rproc_mem_entry struct and fill it with parameters
 * provided by client.
 */
struct rproc_mem_entry *
rproc_mem_entry_init(struct device *dev,
		     void *va, dma_addr_t dma, int len, u32 da,
		     int (*alloc)(struct rproc *, struct rproc_mem_entry *),
		     int (*release)(struct rproc *, struct rproc_mem_entry *),
		     const char *name, ...)
{
	struct rproc_mem_entry *mem;
	va_list args;

	mem = kzalloc(sizeof(*mem), GFP_KERNEL);
	if (!mem)
		return mem;

	mem->va = va;
	mem->dma = dma;
	mem->da = da;
	mem->len = len;
	mem->alloc = alloc;
	mem->release = release;
	mem->rsc_offset = FW_RSC_ADDR_ANY;
	mem->of_resm_idx = -1;

	va_start(args, name);
	vsnprintf(mem->name, sizeof(mem->name), name, args);
	va_end(args);

	return mem;
}
EXPORT_SYMBOL(rproc_mem_entry_init);

/**
 * rproc_of_resm_mem_entry_init() - allocate and initialize rproc_mem_entry struct
 * from a reserved memory phandle
 * @dev: pointer on device struct
 * @of_resm_idx: reserved memory phandle index in "memory-region"
 * @len: memory carveout length
 * @da: device address
 * @name: carveout name
 *
 * This function allocates a rproc_mem_entry struct and fill it with parameters
 * provided by client.
 */
struct rproc_mem_entry *
rproc_of_resm_mem_entry_init(struct device *dev, u32 of_resm_idx, int len,
			     u32 da, const char *name, ...)
{
	struct rproc_mem_entry *mem;
	va_list args;

	mem = kzalloc(sizeof(*mem), GFP_KERNEL);
	if (!mem)
		return mem;

	mem->da = da;
	mem->len = len;
	mem->rsc_offset = FW_RSC_ADDR_ANY;
	mem->of_resm_idx = of_resm_idx;

	va_start(args, name);
	vsnprintf(mem->name, sizeof(mem->name), name, args);
	va_end(args);

	return mem;
}
EXPORT_SYMBOL(rproc_of_resm_mem_entry_init);

/**
 * A lookup table for resource handlers. The indices are defined in
 * enum fw_resource_type.
 */
static rproc_handle_resource_t rproc_loading_handlers[RSC_LAST] = {
	[RSC_CARVEOUT] = (rproc_handle_resource_t)rproc_handle_carveout,
	[RSC_DEVMEM] = (rproc_handle_resource_t)rproc_handle_devmem,
	[RSC_TRACE] = (rproc_handle_resource_t)rproc_handle_trace,
	[RSC_VDEV] = (rproc_handle_resource_t)rproc_handle_vdev,
};

/* handle firmware resource entries before booting the remote processor */
static int rproc_handle_resources(struct rproc *rproc,
				  rproc_handle_resource_t handlers[RSC_LAST])
{
	struct device *dev = &rproc->dev;
	rproc_handle_resource_t handler;
	int ret = 0, i;

	if (!rproc->table_ptr)
		return 0;

	for (i = 0; i < rproc->table_ptr->num; i++) {
		int offset = rproc->table_ptr->offset[i];
		struct fw_rsc_hdr *hdr = (void *)rproc->table_ptr + offset;
		int avail = rproc->table_sz - offset - sizeof(*hdr);
		void *rsc = (void *)hdr + sizeof(*hdr);

		/* make sure table isn't truncated */
		if (avail < 0) {
			dev_err(dev, "rsc table is truncated\n");
			return -EINVAL;
		}

		dev_dbg(dev, "rsc: type %d\n", hdr->type);

		if (hdr->type >= RSC_VENDOR_START &&
		    hdr->type <= RSC_VENDOR_END) {
			ret = rproc_handle_rsc(rproc, hdr->type, rsc,
					       offset + sizeof(*hdr), avail);
			if (ret == RSC_HANDLED)
				continue;
			else if (ret < 0)
				break;

			dev_warn(dev, "unsupported vendor resource %d\n",
				 hdr->type);
			continue;
		}

		if (hdr->type >= RSC_LAST) {
			dev_warn(dev, "unsupported resource %d\n", hdr->type);
			continue;
		}

		handler = handlers[hdr->type];
		if (!handler)
			continue;

		ret = handler(rproc, rsc, offset + sizeof(*hdr), avail);
		if (ret)
			break;
	}

	return ret;
}

static int rproc_prepare_subdevices(struct rproc *rproc)
{
	struct rproc_subdev *subdev;
	int ret;

	list_for_each_entry(subdev, &rproc->subdevs, node) {
		if (subdev->prepare) {
			ret = subdev->prepare(subdev);
			if (ret)
				goto unroll_preparation;
		}
	}

	return 0;

unroll_preparation:
	list_for_each_entry_continue_reverse(subdev, &rproc->subdevs, node) {
		if (subdev->unprepare)
			subdev->unprepare(subdev);
	}

	return ret;
}

static int rproc_start_subdevices(struct rproc *rproc)
{
	struct rproc_subdev *subdev;
	int ret;

	list_for_each_entry(subdev, &rproc->subdevs, node) {
		if (subdev->start) {
			ret = subdev->start(subdev);
			if (ret)
				goto unroll_registration;
		}
	}

	return 0;

unroll_registration:
	list_for_each_entry_continue_reverse(subdev, &rproc->subdevs, node) {
		if (subdev->stop)
			subdev->stop(subdev, true);
	}

	return ret;
}

static void rproc_stop_subdevices(struct rproc *rproc, bool crashed)
{
	struct rproc_subdev *subdev;

	list_for_each_entry_reverse(subdev, &rproc->subdevs, node) {
		if (subdev->stop)
			subdev->stop(subdev, crashed);
	}
}

static void rproc_unprepare_subdevices(struct rproc *rproc)
{
	struct rproc_subdev *subdev;

	list_for_each_entry_reverse(subdev, &rproc->subdevs, node) {
		if (subdev->unprepare)
			subdev->unprepare(subdev);
	}
}

/**
 * rproc_alloc_registered_carveouts() - allocate all carveouts registered
 * in the list
 * @rproc: the remote processor handle
 *
 * This function parses registered carveout list, performs allocation
 * if alloc() ops registered and updates resource table information
 * if rsc_offset set.
 *
 * Return: 0 on success
 */
static int rproc_alloc_registered_carveouts(struct rproc *rproc)
{
	struct rproc_mem_entry *entry, *tmp;
	struct fw_rsc_carveout *rsc;
	struct device *dev = &rproc->dev;
	u64 pa;
	int ret;

	list_for_each_entry_safe(entry, tmp, &rproc->carveouts, node) {
		if (entry->alloc) {
			ret = entry->alloc(rproc, entry);
			if (ret) {
				dev_err(dev, "Unable to allocate carveout %s: %d\n",
					entry->name, ret);
				return -ENOMEM;
			}
		}

		if (entry->rsc_offset != FW_RSC_ADDR_ANY) {
			/* update resource table */
			rsc = (void *)rproc->table_ptr + entry->rsc_offset;

			/*
			 * Some remote processors might need to know the pa
			 * even though they are behind an IOMMU. E.g., OMAP4's
			 * remote M3 processor needs this so it can control
			 * on-chip hardware accelerators that are not behind
			 * the IOMMU, and therefor must know the pa.
			 *
			 * Generally we don't want to expose physical addresses
			 * if we don't have to (remote processors are generally
			 * _not_ trusted), so we might want to do this only for
			 * remote processor that _must_ have this (e.g. OMAP4's
			 * dual M3 subsystem).
			 *
			 * Non-IOMMU processors might also want to have this info.
			 * In this case, the device address and the physical address
			 * are the same.
			 */

			/* Use va if defined else dma to generate pa */
			if (entry->va)
				pa = (u64)rproc_va_to_pa(entry->va);
			else
				pa = (u64)entry->dma;

			if (((u64)pa) & HIGH_BITS_MASK)
				dev_warn(dev,
					 "Physical address cast in 32bit to fit resource table format\n");

			rsc->pa = (u32)pa;
			rsc->da = entry->da;
			rsc->len = entry->len;
		}
	}

	return 0;
}

/**
 * rproc_coredump_cleanup() - clean up dump_segments list
 * @rproc: the remote processor handle
 */
static void rproc_coredump_cleanup(struct rproc *rproc)
{
	struct rproc_dump_segment *entry, *tmp;

	list_for_each_entry_safe(entry, tmp, &rproc->dump_segments, node) {
		list_del(&entry->node);
		kfree(entry);
	}
}

/**
 * rproc_resource_cleanup() - clean up and free all acquired resources
 * @rproc: rproc handle
 *
 * This function will free all resources acquired for @rproc, and it
 * is called whenever @rproc either shuts down or fails to boot.
 */
static void rproc_resource_cleanup(struct rproc *rproc)
{
	struct rproc_mem_entry *entry, *tmp;
	struct rproc_debug_trace *trace, *ttmp;
	struct rproc_vdev *rvdev, *rvtmp;
	struct device *dev = &rproc->dev;

	/* clean up debugfs trace entries */
	list_for_each_entry_safe(trace, ttmp, &rproc->traces, node) {
		rproc_remove_trace_file(trace->tfile);
		rproc->num_traces--;
		list_del(&trace->node);
		kfree(trace);
	}

	/* clean up iommu mapping entries */
	list_for_each_entry_safe(entry, tmp, &rproc->mappings, node) {
		size_t unmapped;

		unmapped = iommu_unmap(rproc->domain, entry->da, entry->len);
		if (unmapped != entry->len) {
			/* nothing much to do besides complaining */
			dev_err(dev, "failed to unmap %u/%zu\n", entry->len,
				unmapped);
		}

		list_del(&entry->node);
		kfree(entry);
	}

	/* clean up carveout allocations */
	list_for_each_entry_safe(entry, tmp, &rproc->carveouts, node) {
		if (entry->release)
			entry->release(rproc, entry);
		list_del(&entry->node);
		kfree(entry);
	}

	/* clean up remote vdev entries */
	list_for_each_entry_safe(rvdev, rvtmp, &rproc->rvdevs, node)
		kref_put(&rvdev->refcount, rproc_vdev_release);

	rproc_coredump_cleanup(rproc);
}

static int rproc_start(struct rproc *rproc, const struct firmware *fw)
{
	struct resource_table *loaded_table;
	struct device *dev = &rproc->dev;
	int ret;

	/* load the ELF segments to memory */
	ret = rproc_load_segments(rproc, fw);
	if (ret) {
		dev_err(dev, "Failed to load program segments: %d\n", ret);
		return ret;
	}

	/*
	 * The starting device has been given the rproc->cached_table as the
	 * resource table. The address of the vring along with the other
	 * allocated resources (carveouts etc) is stored in cached_table.
	 * In order to pass this information to the remote device we must copy
	 * this information to device memory. We also update the table_ptr so
	 * that any subsequent changes will be applied to the loaded version.
	 */
	loaded_table = rproc_find_loaded_rsc_table(rproc, fw);
	if (loaded_table) {
		memcpy(loaded_table, rproc->cached_table, rproc->table_sz);
		rproc->table_ptr = loaded_table;
	}

	ret = rproc_prepare_subdevices(rproc);
	if (ret) {
		dev_err(dev, "failed to prepare subdevices for %s: %d\n",
			rproc->name, ret);
		goto reset_table_ptr;
	}

	/* power up the remote processor */
	ret = rproc->ops->start(rproc);
	if (ret) {
		dev_err(dev, "can't start rproc %s: %d\n", rproc->name, ret);
		goto unprepare_subdevices;
	}

	/* Start any subdevices for the remote processor */
	ret = rproc_start_subdevices(rproc);
	if (ret) {
		dev_err(dev, "failed to probe subdevices for %s: %d\n",
			rproc->name, ret);
		goto stop_rproc;
	}

	rproc->state = RPROC_RUNNING;

	dev_info(dev, "remote processor %s is now up\n", rproc->name);

	return 0;

stop_rproc:
	rproc->ops->stop(rproc);
unprepare_subdevices:
	rproc_unprepare_subdevices(rproc);
reset_table_ptr:
	rproc->table_ptr = rproc->cached_table;

	return ret;
}

/*
 * take a firmware and boot a remote processor with it.
 */
static int rproc_fw_boot(struct rproc *rproc, const struct firmware *fw)
{
	struct device *dev = &rproc->dev;
	const char *name = rproc->firmware;
	int ret;

	ret = rproc_fw_sanity_check(rproc, fw);
	if (ret)
		return ret;

	dev_info(dev, "Booting fw image %s, size %zd\n", name, fw->size);

	/*
	 * if enabling an IOMMU isn't relevant for this rproc, this is
	 * just a nop
	 */
	ret = rproc_enable_iommu(rproc);
	if (ret) {
		dev_err(dev, "can't enable iommu: %d\n", ret);
		return ret;
	}

	rproc->bootaddr = rproc_get_boot_addr(rproc, fw);

	/* Load resource table, core dump segment list etc from the firmware */
	ret = rproc_parse_fw(rproc, fw);
	if (ret)
		goto disable_iommu;

	/* reset max_notifyid */
	rproc->max_notifyid = -1;

	/* reset handled vdev */
	rproc->nb_vdev = 0;

	/* handle fw resources which are required to boot rproc */
	ret = rproc_handle_resources(rproc, rproc_loading_handlers);
	if (ret) {
		dev_err(dev, "Failed to process resources: %d\n", ret);
		goto clean_up_resources;
	}

	/* Allocate carveout resources associated to rproc */
	ret = rproc_alloc_registered_carveouts(rproc);
	if (ret) {
		dev_err(dev, "Failed to allocate associated carveouts: %d\n",
			ret);
		goto clean_up_resources;
	}

	ret = rproc_start(rproc, fw);
	if (ret)
		goto clean_up_resources;

	return 0;

clean_up_resources:
	rproc_resource_cleanup(rproc);
	kfree(rproc->cached_table);
	rproc->cached_table = NULL;
	rproc->table_ptr = NULL;
disable_iommu:
	rproc_disable_iommu(rproc);
	return ret;
}

/*
 * take a firmware and boot it up.
 *
 * Note: this function is called asynchronously upon registration of the
 * remote processor (so we must wait until it completes before we try
 * to unregister the device. one other option is just to use kref here,
 * that might be cleaner).
 */
static void rproc_auto_boot_callback(const struct firmware *fw, void *context)
{
	struct rproc *rproc = context;

	rproc_boot(rproc);

	release_firmware(fw);
}

static int rproc_trigger_auto_boot(struct rproc *rproc)
{
	int ret;

	/*
	 * We're initiating an asynchronous firmware loading, so we can
	 * be built-in kernel code, without hanging the boot process.
	 */
	ret = request_firmware_nowait(THIS_MODULE, FW_ACTION_HOTPLUG,
				      rproc->firmware, &rproc->dev, GFP_KERNEL,
				      rproc, rproc_auto_boot_callback);
	if (ret < 0)
		dev_err(&rproc->dev, "request_firmware_nowait err: %d\n", ret);

	return ret;
}

static int rproc_stop(struct rproc *rproc, bool crashed)
{
	struct device *dev = &rproc->dev;
	int ret;

	/* Stop any subdevices for the remote processor */
	rproc_stop_subdevices(rproc, crashed);

	/* the installed resource table is no longer accessible */
	rproc->table_ptr = rproc->cached_table;

	/* power off the remote processor */
	ret = rproc->ops->stop(rproc);
	if (ret) {
		dev_err(dev, "can't stop rproc: %d\n", ret);
		return ret;
	}

	rproc_unprepare_subdevices(rproc);

	rproc->state = RPROC_OFFLINE;

	dev_info(dev, "stopped remote processor %s\n", rproc->name);

	return 0;
}

/**
 * rproc_coredump_add_segment() - add segment of device memory to coredump
 * @rproc:	handle of a remote processor
 * @da:		device address
 * @size:	size of segment
 *
 * Add device memory to the list of segments to be included in a coredump for
 * the remoteproc.
 *
 * Return: 0 on success, negative errno on error.
 */
int rproc_coredump_add_segment(struct rproc *rproc, dma_addr_t da, size_t size)
{
	struct rproc_dump_segment *segment;

	segment = kzalloc(sizeof(*segment), GFP_KERNEL);
	if (!segment)
		return -ENOMEM;

	segment->da = da;
	segment->size = size;

	list_add_tail(&segment->node, &rproc->dump_segments);

	return 0;
}
EXPORT_SYMBOL(rproc_coredump_add_segment);

/**
 * rproc_coredump_add_custom_segment() - add custom coredump segment
 * @rproc:	handle of a remote processor
 * @da:		device address
 * @size:	size of segment
 * @dumpfn:	custom dump function called for each segment during coredump
 * @priv:	private data
 *
 * Add device memory to the list of segments to be included in the coredump
 * and associate the segment with the given custom dump function and private
 * data.
 *
 * Return: 0 on success, negative errno on error.
 */
int rproc_coredump_add_custom_segment(struct rproc *rproc,
				      dma_addr_t da, size_t size,
				      void (*dumpfn)(struct rproc *rproc,
						     struct rproc_dump_segment *segment,
						     void *dest),
				      void *priv)
{
	struct rproc_dump_segment *segment;

	segment = kzalloc(sizeof(*segment), GFP_KERNEL);
	if (!segment)
		return -ENOMEM;

	segment->da = da;
	segment->size = size;
	segment->priv = priv;
	segment->dump = dumpfn;

	list_add_tail(&segment->node, &rproc->dump_segments);

	return 0;
}
EXPORT_SYMBOL(rproc_coredump_add_custom_segment);

/**
 * rproc_coredump() - perform coredump
 * @rproc:	rproc handle
 *
 * This function will generate an ELF header for the registered segments
 * and create a devcoredump device associated with rproc.
 */
static void rproc_coredump(struct rproc *rproc)
{
	struct rproc_dump_segment *segment;
	struct elf32_phdr *phdr;
	struct elf32_hdr *ehdr;
	size_t data_size;
	size_t offset;
	void *data;
	void *ptr;
	int phnum = 0;

	if (list_empty(&rproc->dump_segments))
		return;

	data_size = sizeof(*ehdr);
	list_for_each_entry(segment, &rproc->dump_segments, node) {
		data_size += sizeof(*phdr) + segment->size;

		phnum++;
	}

	data = vmalloc(data_size);
	if (!data)
		return;

	ehdr = data;

	memset(ehdr, 0, sizeof(*ehdr));
	memcpy(ehdr->e_ident, ELFMAG, SELFMAG);
	ehdr->e_ident[EI_CLASS] = ELFCLASS32;
	ehdr->e_ident[EI_DATA] = ELFDATA2LSB;
	ehdr->e_ident[EI_VERSION] = EV_CURRENT;
	ehdr->e_ident[EI_OSABI] = ELFOSABI_NONE;
	ehdr->e_type = ET_CORE;
	ehdr->e_machine = EM_NONE;
	ehdr->e_version = EV_CURRENT;
	ehdr->e_entry = rproc->bootaddr;
	ehdr->e_phoff = sizeof(*ehdr);
	ehdr->e_ehsize = sizeof(*ehdr);
	ehdr->e_phentsize = sizeof(*phdr);
	ehdr->e_phnum = phnum;

	phdr = data + ehdr->e_phoff;
	offset = ehdr->e_phoff + sizeof(*phdr) * ehdr->e_phnum;
	list_for_each_entry(segment, &rproc->dump_segments, node) {
		memset(phdr, 0, sizeof(*phdr));
		phdr->p_type = PT_LOAD;
		phdr->p_offset = offset;
		phdr->p_vaddr = segment->da;
		phdr->p_paddr = segment->da;
		phdr->p_filesz = segment->size;
		phdr->p_memsz = segment->size;
		phdr->p_flags = PF_R | PF_W | PF_X;
		phdr->p_align = 0;

		if (segment->dump) {
			segment->dump(rproc, segment, data + offset);
		} else {
			ptr = rproc_da_to_va(rproc, segment->da, segment->size);
			if (!ptr) {
				dev_err(&rproc->dev,
					"invalid coredump segment (%pad, %zu)\n",
					&segment->da, segment->size);
				memset(data + offset, 0xff, segment->size);
			} else {
				memcpy(data + offset, ptr, segment->size);
			}
		}

		offset += phdr->p_filesz;
		phdr++;
	}

	dev_coredumpv(&rproc->dev, data, data_size, GFP_KERNEL);
}

/**
 * rproc_trigger_recovery() - recover a remoteproc
 * @rproc: the remote processor
 *
 * The recovery is done by resetting all the virtio devices, that way all the
 * rpmsg drivers will be reseted along with the remote processor making the
 * remoteproc functional again.
 *
 * This function can sleep, so it cannot be called from atomic context.
 */
int rproc_trigger_recovery(struct rproc *rproc)
{
	const struct firmware *firmware_p;
	struct device *dev = &rproc->dev;
	int ret;

	dev_err(dev, "recovering %s\n", rproc->name);

	ret = mutex_lock_interruptible(&rproc->lock);
	if (ret)
		return ret;

	ret = rproc_stop(rproc, true);
	if (ret)
		goto unlock_mutex;

	/* generate coredump */
	rproc_coredump(rproc);

	/* load firmware */
	ret = request_firmware(&firmware_p, rproc->firmware, dev);
	if (ret < 0) {
		dev_err(dev, "request_firmware failed: %d\n", ret);
		goto unlock_mutex;
	}

	/* boot the remote processor up again */
	ret = rproc_start(rproc, firmware_p);

	release_firmware(firmware_p);

unlock_mutex:
	mutex_unlock(&rproc->lock);
	return ret;
}

/**
 * rproc_crash_handler_work() - handle a crash
 *
 * This function needs to handle everything related to a crash, like cpu
 * registers and stack dump, information to help to debug the fatal error, etc.
 */
static void rproc_crash_handler_work(struct work_struct *work)
{
	struct rproc *rproc = container_of(work, struct rproc, crash_handler);
	struct device *dev = &rproc->dev;

	dev_dbg(dev, "enter %s\n", __func__);

	mutex_lock(&rproc->lock);

	if (rproc->state == RPROC_CRASHED || rproc->state == RPROC_OFFLINE) {
		/* handle only the first crash detected */
		mutex_unlock(&rproc->lock);
		return;
	}

	rproc->state = RPROC_CRASHED;
	dev_err(dev, "handling crash #%u in %s\n", ++rproc->crash_cnt,
		rproc->name);

	mutex_unlock(&rproc->lock);

	if (!rproc->recovery_disabled)
		rproc_trigger_recovery(rproc);
}

/**
 * rproc_boot() - boot a remote processor
 * @rproc: handle of a remote processor
 *
 * Boot a remote processor (i.e. load its firmware, power it on, ...).
 *
 * If the remote processor is already powered on, this function immediately
 * returns (successfully).
 *
 * Returns 0 on success, and an appropriate error value otherwise.
 */
int rproc_boot(struct rproc *rproc)
{
	const struct firmware *firmware_p;
	struct device *dev;
	int ret;

	if (!rproc) {
		pr_err("invalid rproc handle\n");
		return -EINVAL;
	}

	dev = &rproc->dev;

	ret = mutex_lock_interruptible(&rproc->lock);
	if (ret) {
		dev_err(dev, "can't lock rproc %s: %d\n", rproc->name, ret);
		return ret;
	}

	if (rproc->state == RPROC_DELETED) {
		ret = -ENODEV;
		dev_err(dev, "can't boot deleted rproc %s\n", rproc->name);
		goto unlock_mutex;
	}

	/* skip the boot process if rproc is already powered up */
	if (atomic_inc_return(&rproc->power) > 1) {
		ret = 0;
		goto unlock_mutex;
	}

	dev_info(dev, "powering up %s\n", rproc->name);

	/* load firmware */
	ret = request_firmware(&firmware_p, rproc->firmware, dev);
	if (ret < 0) {
		dev_err(dev, "request_firmware failed: %d\n", ret);
		goto downref_rproc;
	}

	ret = rproc_fw_boot(rproc, firmware_p);

	release_firmware(firmware_p);

downref_rproc:
	if (ret)
		atomic_dec(&rproc->power);
unlock_mutex:
	mutex_unlock(&rproc->lock);
	return ret;
}
EXPORT_SYMBOL(rproc_boot);

/**
 * rproc_shutdown() - power off the remote processor
 * @rproc: the remote processor
 *
 * Power off a remote processor (previously booted with rproc_boot()).
 *
 * In case @rproc is still being used by an additional user(s), then
 * this function will just decrement the power refcount and exit,
 * without really powering off the device.
 *
 * Every call to rproc_boot() must (eventually) be accompanied by a call
 * to rproc_shutdown(). Calling rproc_shutdown() redundantly is a bug.
 *
 * Notes:
 * - we're not decrementing the rproc's refcount, only the power refcount.
 *   which means that the @rproc handle stays valid even after rproc_shutdown()
 *   returns, and users can still use it with a subsequent rproc_boot(), if
 *   needed.
 */
void rproc_shutdown(struct rproc *rproc)
{
	struct device *dev = &rproc->dev;
	int ret;

	ret = mutex_lock_interruptible(&rproc->lock);
	if (ret) {
		dev_err(dev, "can't lock rproc %s: %d\n", rproc->name, ret);
		return;
	}

	/* if the remote proc is still needed, bail out */
	if (!atomic_dec_and_test(&rproc->power))
		goto out;

	ret = rproc_stop(rproc, false);
	if (ret) {
		atomic_inc(&rproc->power);
		goto out;
	}

	/* clean up all acquired resources */
	rproc_resource_cleanup(rproc);

	rproc_disable_iommu(rproc);

	/* Free the copy of the resource table */
	kfree(rproc->cached_table);
	rproc->cached_table = NULL;
	rproc->table_ptr = NULL;
out:
	mutex_unlock(&rproc->lock);
}
EXPORT_SYMBOL(rproc_shutdown);

/**
 * rproc_get_by_phandle() - find a remote processor by phandle
 * @phandle: phandle to the rproc
 *
 * Finds an rproc handle using the remote processor's phandle, and then
 * return a handle to the rproc.
 *
 * This function increments the remote processor's refcount, so always
 * use rproc_put() to decrement it back once rproc isn't needed anymore.
 *
 * Returns the rproc handle on success, and NULL on failure.
 */
#ifdef CONFIG_OF
struct rproc *rproc_get_by_phandle(phandle phandle)
{
	struct rproc *rproc = NULL, *r;
	struct device_node *np;

	np = of_find_node_by_phandle(phandle);
	if (!np)
		return NULL;

	mutex_lock(&rproc_list_mutex);
	list_for_each_entry(r, &rproc_list, node) {
		if (r->dev.parent && r->dev.parent->of_node == np) {
			/* prevent underlying implementation from being removed */
			if (!try_module_get(r->dev.parent->driver->owner)) {
				dev_err(&r->dev, "can't get owner\n");
				break;
			}

			rproc = r;
			get_device(&rproc->dev);
			break;
		}
	}
	mutex_unlock(&rproc_list_mutex);

	of_node_put(np);

	return rproc;
}
#else
struct rproc *rproc_get_by_phandle(phandle phandle)
{
	return NULL;
}
#endif
EXPORT_SYMBOL(rproc_get_by_phandle);

/**
 * rproc_add() - register a remote processor
 * @rproc: the remote processor handle to register
 *
 * Registers @rproc with the remoteproc framework, after it has been
 * allocated with rproc_alloc().
 *
 * This is called by the platform-specific rproc implementation, whenever
 * a new remote processor device is probed.
 *
 * Returns 0 on success and an appropriate error code otherwise.
 *
 * Note: this function initiates an asynchronous firmware loading
 * context, which will look for virtio devices supported by the rproc's
 * firmware.
 *
 * If found, those virtio devices will be created and added, so as a result
 * of registering this remote processor, additional virtio drivers might be
 * probed.
 */
int rproc_add(struct rproc *rproc)
{
	struct device *dev = &rproc->dev;
	int ret;

	ret = device_add(dev);
	if (ret < 0)
		return ret;

	dev_info(dev, "%s is available\n", rproc->name);

	/* create debugfs entries */
	rproc_create_debug_dir(rproc);

	/* if rproc is marked always-on, request it to boot */
	if (rproc->auto_boot) {
		ret = rproc_trigger_auto_boot(rproc);
		if (ret < 0)
			return ret;
	}

	/* expose to rproc_get_by_phandle users */
	mutex_lock(&rproc_list_mutex);
	list_add(&rproc->node, &rproc_list);
	mutex_unlock(&rproc_list_mutex);

	return 0;
}
EXPORT_SYMBOL(rproc_add);

/**
 * rproc_type_release() - release a remote processor instance
 * @dev: the rproc's device
 *
 * This function should _never_ be called directly.
 *
 * It will be called by the driver core when no one holds a valid pointer
 * to @dev anymore.
 */
static void rproc_type_release(struct device *dev)
{
	struct rproc *rproc = container_of(dev, struct rproc, dev);

	dev_info(&rproc->dev, "releasing %s\n", rproc->name);

	idr_destroy(&rproc->notifyids);

	if (rproc->index >= 0)
		ida_simple_remove(&rproc_dev_index, rproc->index);

	kfree(rproc->firmware);
	kfree(rproc->ops);
	kfree(rproc);
}

static const struct device_type rproc_type = {
	.name		= "remoteproc",
	.release	= rproc_type_release,
};

/**
 * rproc_alloc() - allocate a remote processor handle
 * @dev: the underlying device
 * @name: name of this remote processor
 * @ops: platform-specific handlers (mainly start/stop)
 * @firmware: name of firmware file to load, can be NULL
 * @len: length of private data needed by the rproc driver (in bytes)
 *
 * Allocates a new remote processor handle, but does not register
 * it yet. if @firmware is NULL, a default name is used.
 *
 * This function should be used by rproc implementations during initialization
 * of the remote processor.
 *
 * After creating an rproc handle using this function, and when ready,
 * implementations should then call rproc_add() to complete
 * the registration of the remote processor.
 *
 * On success the new rproc is returned, and on failure, NULL.
 *
 * Note: _never_ directly deallocate @rproc, even if it was not registered
 * yet. Instead, when you need to unroll rproc_alloc(), use rproc_free().
 */
struct rproc *rproc_alloc(struct device *dev, const char *name,
			  const struct rproc_ops *ops,
			  const char *firmware, int len)
{
	struct rproc *rproc;
	char *p, *template = "rproc-%s-fw";
	int name_len;

	if (!dev || !name || !ops)
		return NULL;

	if (!firmware) {
		/*
		 * If the caller didn't pass in a firmware name then
		 * construct a default name.
		 */
		name_len = strlen(name) + strlen(template) - 2 + 1;
		p = kmalloc(name_len, GFP_KERNEL);
		if (!p)
			return NULL;
		snprintf(p, name_len, template, name);
	} else {
		p = kstrdup(firmware, GFP_KERNEL);
		if (!p)
			return NULL;
	}

	rproc = kzalloc(sizeof(struct rproc) + len, GFP_KERNEL);
	if (!rproc) {
		kfree(p);
		return NULL;
	}

	rproc->ops = kmemdup(ops, sizeof(*ops), GFP_KERNEL);
	if (!rproc->ops) {
		kfree(p);
		kfree(rproc);
		return NULL;
	}

	rproc->firmware = p;
	rproc->name = name;
	rproc->priv = &rproc[1];
	rproc->auto_boot = true;

	device_initialize(&rproc->dev);
	rproc->dev.parent = dev;
	rproc->dev.type = &rproc_type;
	rproc->dev.class = &rproc_class;
	rproc->dev.driver_data = rproc;

	/* Assign a unique device index and name */
	rproc->index = ida_simple_get(&rproc_dev_index, 0, 0, GFP_KERNEL);
	if (rproc->index < 0) {
		dev_err(dev, "ida_simple_get failed: %d\n", rproc->index);
		put_device(&rproc->dev);
		return NULL;
	}

	dev_set_name(&rproc->dev, "remoteproc%d", rproc->index);

	atomic_set(&rproc->power, 0);

	/* Default to ELF loader if no load function is specified */
	if (!rproc->ops->load) {
		rproc->ops->load = rproc_elf_load_segments;
		rproc->ops->parse_fw = rproc_elf_load_rsc_table;
		rproc->ops->find_loaded_rsc_table = rproc_elf_find_loaded_rsc_table;
		rproc->ops->sanity_check = rproc_elf_sanity_check;
		rproc->ops->get_boot_addr = rproc_elf_get_boot_addr;
	}

	mutex_init(&rproc->lock);

	idr_init(&rproc->notifyids);

	INIT_LIST_HEAD(&rproc->carveouts);
	INIT_LIST_HEAD(&rproc->mappings);
	INIT_LIST_HEAD(&rproc->traces);
	INIT_LIST_HEAD(&rproc->rvdevs);
	INIT_LIST_HEAD(&rproc->subdevs);
	INIT_LIST_HEAD(&rproc->dump_segments);

	INIT_WORK(&rproc->crash_handler, rproc_crash_handler_work);

	rproc->state = RPROC_OFFLINE;

	return rproc;
}
EXPORT_SYMBOL(rproc_alloc);

/**
 * rproc_free() - unroll rproc_alloc()
 * @rproc: the remote processor handle
 *
 * This function decrements the rproc dev refcount.
 *
 * If no one holds any reference to rproc anymore, then its refcount would
 * now drop to zero, and it would be freed.
 */
void rproc_free(struct rproc *rproc)
{
	put_device(&rproc->dev);
}
EXPORT_SYMBOL(rproc_free);

/**
 * rproc_put() - release rproc reference
 * @rproc: the remote processor handle
 *
 * This function decrements the rproc dev refcount.
 *
 * If no one holds any reference to rproc anymore, then its refcount would
 * now drop to zero, and it would be freed.
 */
void rproc_put(struct rproc *rproc)
{
	module_put(rproc->dev.parent->driver->owner);
	put_device(&rproc->dev);
}
EXPORT_SYMBOL(rproc_put);

/**
 * rproc_del() - unregister a remote processor
 * @rproc: rproc handle to unregister
 *
 * This function should be called when the platform specific rproc
 * implementation decides to remove the rproc device. it should
 * _only_ be called if a previous invocation of rproc_add()
 * has completed successfully.
 *
 * After rproc_del() returns, @rproc isn't freed yet, because
 * of the outstanding reference created by rproc_alloc. To decrement that
 * one last refcount, one still needs to call rproc_free().
 *
 * Returns 0 on success and -EINVAL if @rproc isn't valid.
 */
int rproc_del(struct rproc *rproc)
{
	if (!rproc)
		return -EINVAL;

	/* if rproc is marked always-on, rproc_add() booted it */
	/* TODO: make sure this works with rproc->power > 1 */
	if (rproc->auto_boot)
		rproc_shutdown(rproc);

	mutex_lock(&rproc->lock);
	rproc->state = RPROC_DELETED;
	mutex_unlock(&rproc->lock);

	rproc_delete_debug_dir(rproc);

	/* the rproc is downref'ed as soon as it's removed from the klist */
	mutex_lock(&rproc_list_mutex);
	list_del(&rproc->node);
	mutex_unlock(&rproc_list_mutex);

	device_del(&rproc->dev);

	return 0;
}
EXPORT_SYMBOL(rproc_del);

/**
 * rproc_add_subdev() - add a subdevice to a remoteproc
 * @rproc: rproc handle to add the subdevice to
 * @subdev: subdev handle to register
 *
 * Caller is responsible for populating optional subdevice function pointers.
 */
void rproc_add_subdev(struct rproc *rproc, struct rproc_subdev *subdev)
{
	list_add_tail(&subdev->node, &rproc->subdevs);
}
EXPORT_SYMBOL(rproc_add_subdev);

/**
 * rproc_remove_subdev() - remove a subdevice from a remoteproc
 * @rproc: rproc handle to remove the subdevice from
 * @subdev: subdev handle, previously registered with rproc_add_subdev()
 */
void rproc_remove_subdev(struct rproc *rproc, struct rproc_subdev *subdev)
{
	list_del(&subdev->node);
}
EXPORT_SYMBOL(rproc_remove_subdev);

/**
 * rproc_get_by_child() - acquire rproc handle of @dev's ancestor
 * @dev:	child device to find ancestor of
 *
 * Returns the ancestor rproc instance, or NULL if not found.
 */
struct rproc *rproc_get_by_child(struct device *dev)
{
	for (dev = dev->parent; dev; dev = dev->parent) {
		if (dev->type == &rproc_type)
			return dev->driver_data;
	}

	return NULL;
}
EXPORT_SYMBOL(rproc_get_by_child);

/**
 * rproc_report_crash() - rproc crash reporter function
 * @rproc: remote processor
 * @type: crash type
 *
 * This function must be called every time a crash is detected by the low-level
 * drivers implementing a specific remoteproc. This should not be called from a
 * non-remoteproc driver.
 *
 * This function can be called from atomic/interrupt context.
 */
void rproc_report_crash(struct rproc *rproc, enum rproc_crash_type type)
{
	if (!rproc) {
		pr_err("NULL rproc pointer\n");
		return;
	}

	dev_err(&rproc->dev, "crash detected in %s: type %s\n",
		rproc->name, rproc_crash_to_string(type));

	/* create a new task to handle the error */
	schedule_work(&rproc->crash_handler);
}
EXPORT_SYMBOL(rproc_report_crash);

static int __init remoteproc_init(void)
{
	rproc_init_sysfs();
	rproc_init_debugfs();

	return 0;
}
module_init(remoteproc_init);

static void __exit remoteproc_exit(void)
{
	ida_destroy(&rproc_dev_index);

	rproc_exit_debugfs();
	rproc_exit_sysfs();
}
module_exit(remoteproc_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Generic Remote Processor Framework");

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
#include <linux/remoteproc.h>
#include <linux/iommu.h>
#include <linux/klist.h>
#include <linux/elf.h>
#include <linux/virtio_ids.h>
#include <linux/virtio_ring.h>
#include <asm/byteorder.h>

#include "remoteproc_internal.h"

static void klist_rproc_get(struct klist_node *n);
static void klist_rproc_put(struct klist_node *n);

/*
 * klist of the available remote processors.
 *
 * We need this in order to support name-based lookups (needed by the
 * rproc_get_by_name()).
 *
 * That said, we don't use rproc_get_by_name() at this point.
 * The use cases that do require its existence should be
 * scrutinized, and hopefully migrated to rproc_boot() using device-based
 * binding.
 *
 * If/when this materializes, we could drop the klist (and the by_name
 * API).
 */
static DEFINE_KLIST(rprocs, klist_rproc_get, klist_rproc_put);

typedef int (*rproc_handle_resources_t)(struct rproc *rproc,
				struct resource_table *table, int len);
typedef int (*rproc_handle_resource_t)(struct rproc *rproc, void *, int avail);

/*
 * This is the IOMMU fault handler we register with the IOMMU API
 * (when relevant; not all remote processors access memory through
 * an IOMMU).
 *
 * IOMMU core will invoke this handler whenever the remote processor
 * will try to access an unmapped device address.
 *
 * Currently this is mostly a stub, but it will be later used to trigger
 * the recovery of the remote processor.
 */
static int rproc_iommu_fault(struct iommu_domain *domain, struct device *dev,
		unsigned long iova, int flags)
{
	dev_err(dev, "iommu fault: da 0x%lx flags 0x%x\n", iova, flags);

	/*
	 * Let the iommu core know we're not really handling this fault;
	 * we just plan to use this as a recovery trigger.
	 */
	return -ENOSYS;
}

static int rproc_enable_iommu(struct rproc *rproc)
{
	struct iommu_domain *domain;
	struct device *dev = rproc->dev;
	int ret;

	/*
	 * We currently use iommu_present() to decide if an IOMMU
	 * setup is needed.
	 *
	 * This works for simple cases, but will easily fail with
	 * platforms that do have an IOMMU, but not for this specific
	 * rproc.
	 *
	 * This will be easily solved by introducing hw capabilities
	 * that will be set by the remoteproc driver.
	 */
	if (!iommu_present(dev->bus)) {
		dev_dbg(dev, "iommu not found\n");
		return 0;
	}

	domain = iommu_domain_alloc(dev->bus);
	if (!domain) {
		dev_err(dev, "can't alloc iommu domain\n");
		return -ENOMEM;
	}

	iommu_set_fault_handler(domain, rproc_iommu_fault);

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
	struct device *dev = rproc->dev;

	if (!domain)
		return;

	iommu_detach_device(domain, dev);
	iommu_domain_free(domain);

	return;
}

/*
 * Some remote processors will ask us to allocate them physically contiguous
 * memory regions (which we call "carveouts"), and map them to specific
 * device addresses (which are hardcoded in the firmware).
 *
 * They may then ask us to copy objects into specific device addresses (e.g.
 * code/data sections) or expose us certain symbols in other device address
 * (e.g. their trace buffer).
 *
 * This function is an internal helper with which we can go over the allocated
 * carveouts and translate specific device address to kernel virtual addresses
 * so we can access the referenced memory.
 *
 * Note: phys_to_virt(iommu_iova_to_phys(rproc->domain, da)) will work too,
 * but only on kernel direct mapped RAM memory. Instead, we're just using
 * here the output of the DMA API, which should be more correct.
 */
static void *rproc_da_to_va(struct rproc *rproc, u64 da, int len)
{
	struct rproc_mem_entry *carveout;
	void *ptr = NULL;

	list_for_each_entry(carveout, &rproc->carveouts, node) {
		int offset = da - carveout->da;

		/* try next carveout if da is too small */
		if (offset < 0)
			continue;

		/* try next carveout if da is too large */
		if (offset + len > carveout->len)
			continue;

		ptr = carveout->va + offset;

		break;
	}

	return ptr;
}

/**
 * rproc_load_segments() - load firmware segments to memory
 * @rproc: remote processor which will be booted using these fw segments
 * @elf_data: the content of the ELF firmware image
 * @len: firmware size (in bytes)
 *
 * This function loads the firmware segments to memory, where the remote
 * processor expects them.
 *
 * Some remote processors will expect their code and data to be placed
 * in specific device addresses, and can't have them dynamically assigned.
 *
 * We currently support only those kind of remote processors, and expect
 * the program header's paddr member to contain those addresses. We then go
 * through the physically contiguous "carveout" memory regions which we
 * allocated (and mapped) earlier on behalf of the remote processor,
 * and "translate" device address to kernel addresses, so we can copy the
 * segments where they are expected.
 *
 * Currently we only support remote processors that required carveout
 * allocations and got them mapped onto their iommus. Some processors
 * might be different: they might not have iommus, and would prefer to
 * directly allocate memory for every segment/resource. This is not yet
 * supported, though.
 */
static int
rproc_load_segments(struct rproc *rproc, const u8 *elf_data, size_t len)
{
	struct device *dev = rproc->dev;
	struct elf32_hdr *ehdr;
	struct elf32_phdr *phdr;
	int i, ret = 0;

	ehdr = (struct elf32_hdr *)elf_data;
	phdr = (struct elf32_phdr *)(elf_data + ehdr->e_phoff);

	/* go through the available ELF segments */
	for (i = 0; i < ehdr->e_phnum; i++, phdr++) {
		u32 da = phdr->p_paddr;
		u32 memsz = phdr->p_memsz;
		u32 filesz = phdr->p_filesz;
		u32 offset = phdr->p_offset;
		void *ptr;

		if (phdr->p_type != PT_LOAD)
			continue;

		dev_dbg(dev, "phdr: type %d da 0x%x memsz 0x%x filesz 0x%x\n",
					phdr->p_type, da, memsz, filesz);

		if (filesz > memsz) {
			dev_err(dev, "bad phdr filesz 0x%x memsz 0x%x\n",
							filesz, memsz);
			ret = -EINVAL;
			break;
		}

		if (offset + filesz > len) {
			dev_err(dev, "truncated fw: need 0x%x avail 0x%x\n",
					offset + filesz, len);
			ret = -EINVAL;
			break;
		}

		/* grab the kernel address for this device address */
		ptr = rproc_da_to_va(rproc, da, memsz);
		if (!ptr) {
			dev_err(dev, "bad phdr da 0x%x mem 0x%x\n", da, memsz);
			ret = -EINVAL;
			break;
		}

		/* put the segment where the remote processor expects it */
		if (phdr->p_filesz)
			memcpy(ptr, elf_data + phdr->p_offset, filesz);

		/*
		 * Zero out remaining memory for this segment.
		 *
		 * This isn't strictly required since dma_alloc_coherent already
		 * did this for us. albeit harmless, we may consider removing
		 * this.
		 */
		if (memsz > filesz)
			memset(ptr + filesz, 0, memsz - filesz);
	}

	return ret;
}

static int
__rproc_handle_vring(struct rproc_vdev *rvdev, struct fw_rsc_vdev *rsc, int i)
{
	struct rproc *rproc = rvdev->rproc;
	struct device *dev = rproc->dev;
	struct fw_rsc_vdev_vring *vring = &rsc->vring[i];
	dma_addr_t dma;
	void *va;
	int ret, size, notifyid;

	dev_dbg(dev, "vdev rsc: vring%d: da %x, qsz %d, align %d\n",
				i, vring->da, vring->num, vring->align);

	/* make sure reserved bytes are zeroes */
	if (vring->reserved) {
		dev_err(dev, "vring rsc has non zero reserved bytes\n");
		return -EINVAL;
	}

	/* verify queue size and vring alignment are sane */
	if (!vring->num || !vring->align) {
		dev_err(dev, "invalid qsz (%d) or alignment (%d)\n",
						vring->num, vring->align);
		return -EINVAL;
	}

	/* actual size of vring (in bytes) */
	size = PAGE_ALIGN(vring_size(vring->num, vring->align));

	if (!idr_pre_get(&rproc->notifyids, GFP_KERNEL)) {
		dev_err(dev, "idr_pre_get failed\n");
		return -ENOMEM;
	}

	/*
	 * Allocate non-cacheable memory for the vring. In the future
	 * this call will also configure the IOMMU for us
	 */
	va = dma_alloc_coherent(dev, size, &dma, GFP_KERNEL);
	if (!va) {
		dev_err(dev, "dma_alloc_coherent failed\n");
		return -EINVAL;
	}

	/* assign an rproc-wide unique index for this vring */
	/* TODO: assign a notifyid for rvdev updates as well */
	ret = idr_get_new(&rproc->notifyids, &rvdev->vring[i], &notifyid);
	if (ret) {
		dev_err(dev, "idr_get_new failed: %d\n", ret);
		dma_free_coherent(dev, size, va, dma);
		return ret;
	}

	/* let the rproc know the da and notifyid of this vring */
	/* TODO: expose this to remote processor */
	vring->da = dma;
	vring->notifyid = notifyid;

	dev_dbg(dev, "vring%d: va %p dma %x size %x idr %d\n", i, va,
					dma, size, notifyid);

	rvdev->vring[i].len = vring->num;
	rvdev->vring[i].align = vring->align;
	rvdev->vring[i].va = va;
	rvdev->vring[i].dma = dma;
	rvdev->vring[i].notifyid = notifyid;
	rvdev->vring[i].rvdev = rvdev;

	return 0;
}

static void __rproc_free_vrings(struct rproc_vdev *rvdev, int i)
{
	struct rproc *rproc = rvdev->rproc;

	for (i--; i >= 0; i--) {
		struct rproc_vring *rvring = &rvdev->vring[i];
		int size = PAGE_ALIGN(vring_size(rvring->len, rvring->align));

		dma_free_coherent(rproc->dev, size, rvring->va, rvring->dma);
		idr_remove(&rproc->notifyids, rvring->notifyid);
	}
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
								int avail)
{
	struct device *dev = rproc->dev;
	struct rproc_vdev *rvdev;
	int i, ret;

	/* make sure resource isn't truncated */
	if (sizeof(*rsc) + rsc->num_of_vrings * sizeof(struct fw_rsc_vdev_vring)
			+ rsc->config_len > avail) {
		dev_err(rproc->dev, "vdev rsc is truncated\n");
		return -EINVAL;
	}

	/* make sure reserved bytes are zeroes */
	if (rsc->reserved[0] || rsc->reserved[1]) {
		dev_err(dev, "vdev rsc has non zero reserved bytes\n");
		return -EINVAL;
	}

	dev_dbg(dev, "vdev rsc: id %d, dfeatures %x, cfg len %d, %d vrings\n",
		rsc->id, rsc->dfeatures, rsc->config_len, rsc->num_of_vrings);

	/* we currently support only two vrings per rvdev */
	if (rsc->num_of_vrings > ARRAY_SIZE(rvdev->vring)) {
		dev_err(dev, "too many vrings: %d\n", rsc->num_of_vrings);
		return -EINVAL;
	}

	rvdev = kzalloc(sizeof(struct rproc_vdev), GFP_KERNEL);
	if (!rvdev)
		return -ENOMEM;

	rvdev->rproc = rproc;

	/* allocate the vrings */
	for (i = 0; i < rsc->num_of_vrings; i++) {
		ret = __rproc_handle_vring(rvdev, rsc, i);
		if (ret)
			goto free_vrings;
	}

	/* remember the device features */
	rvdev->dfeatures = rsc->dfeatures;

	list_add_tail(&rvdev->node, &rproc->rvdevs);

	/* it is now safe to add the virtio device */
	ret = rproc_add_virtio_dev(rvdev, rsc->id);
	if (ret)
		goto free_vrings;

	return 0;

free_vrings:
	__rproc_free_vrings(rvdev, i);
	kfree(rvdev);
	return ret;
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
								int avail)
{
	struct rproc_mem_entry *trace;
	struct device *dev = rproc->dev;
	void *ptr;
	char name[15];

	if (sizeof(*rsc) > avail) {
		dev_err(rproc->dev, "trace rsc is truncated\n");
		return -EINVAL;
	}

	/* make sure reserved bytes are zeroes */
	if (rsc->reserved) {
		dev_err(dev, "trace rsc has non zero reserved bytes\n");
		return -EINVAL;
	}

	/* what's the kernel address of this resource ? */
	ptr = rproc_da_to_va(rproc, rsc->da, rsc->len);
	if (!ptr) {
		dev_err(dev, "erroneous trace resource entry\n");
		return -EINVAL;
	}

	trace = kzalloc(sizeof(*trace), GFP_KERNEL);
	if (!trace) {
		dev_err(dev, "kzalloc trace failed\n");
		return -ENOMEM;
	}

	/* set the trace buffer dma properties */
	trace->len = rsc->len;
	trace->va = ptr;

	/* make sure snprintf always null terminates, even if truncating */
	snprintf(name, sizeof(name), "trace%d", rproc->num_traces);

	/* create the debugfs entry */
	trace->priv = rproc_create_trace_file(name, rproc, trace);
	if (!trace->priv) {
		trace->va = NULL;
		kfree(trace);
		return -EINVAL;
	}

	list_add_tail(&trace->node, &rproc->traces);

	rproc->num_traces++;

	dev_dbg(dev, "%s added: va %p, da 0x%x, len 0x%x\n", name, ptr,
						rsc->da, rsc->len);

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
								int avail)
{
	struct rproc_mem_entry *mapping;
	int ret;

	/* no point in handling this resource without a valid iommu domain */
	if (!rproc->domain)
		return -EINVAL;

	if (sizeof(*rsc) > avail) {
		dev_err(rproc->dev, "devmem rsc is truncated\n");
		return -EINVAL;
	}

	/* make sure reserved bytes are zeroes */
	if (rsc->reserved) {
		dev_err(rproc->dev, "devmem rsc has non zero reserved bytes\n");
		return -EINVAL;
	}

	mapping = kzalloc(sizeof(*mapping), GFP_KERNEL);
	if (!mapping) {
		dev_err(rproc->dev, "kzalloc mapping failed\n");
		return -ENOMEM;
	}

	ret = iommu_map(rproc->domain, rsc->da, rsc->pa, rsc->len, rsc->flags);
	if (ret) {
		dev_err(rproc->dev, "failed to map devmem: %d\n", ret);
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

	dev_dbg(rproc->dev, "mapped devmem pa 0x%x, da 0x%x, len 0x%x\n",
					rsc->pa, rsc->da, rsc->len);

	return 0;

out:
	kfree(mapping);
	return ret;
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
				struct fw_rsc_carveout *rsc, int avail)
{
	struct rproc_mem_entry *carveout, *mapping;
	struct device *dev = rproc->dev;
	dma_addr_t dma;
	void *va;
	int ret;

	if (sizeof(*rsc) > avail) {
		dev_err(rproc->dev, "carveout rsc is truncated\n");
		return -EINVAL;
	}

	/* make sure reserved bytes are zeroes */
	if (rsc->reserved) {
		dev_err(dev, "carveout rsc has non zero reserved bytes\n");
		return -EINVAL;
	}

	dev_dbg(dev, "carveout rsc: da %x, pa %x, len %x, flags %x\n",
			rsc->da, rsc->pa, rsc->len, rsc->flags);

	mapping = kzalloc(sizeof(*mapping), GFP_KERNEL);
	if (!mapping) {
		dev_err(dev, "kzalloc mapping failed\n");
		return -ENOMEM;
	}

	carveout = kzalloc(sizeof(*carveout), GFP_KERNEL);
	if (!carveout) {
		dev_err(dev, "kzalloc carveout failed\n");
		ret = -ENOMEM;
		goto free_mapping;
	}

	va = dma_alloc_coherent(dev, rsc->len, &dma, GFP_KERNEL);
	if (!va) {
		dev_err(dev, "failed to dma alloc carveout: %d\n", rsc->len);
		ret = -ENOMEM;
		goto free_carv;
	}

	dev_dbg(dev, "carveout va %p, dma %x, len 0x%x\n", va, dma, rsc->len);

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
	if (rproc->domain) {
		ret = iommu_map(rproc->domain, rsc->da, dma, rsc->len,
								rsc->flags);
		if (ret) {
			dev_err(dev, "iommu_map failed: %d\n", ret);
			goto dma_free;
		}

		/*
		 * We'll need this info later when we'll want to unmap
		 * everything (e.g. on shutdown).
		 *
		 * We can't trust the remote processor not to change the
		 * resource table, so we must maintain this info independently.
		 */
		mapping->da = rsc->da;
		mapping->len = rsc->len;
		list_add_tail(&mapping->node, &rproc->mappings);

		dev_dbg(dev, "carveout mapped 0x%x to 0x%x\n", rsc->da, dma);

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
		 */
		rsc->pa = dma;
	}

	carveout->va = va;
	carveout->len = rsc->len;
	carveout->dma = dma;
	carveout->da = rsc->da;

	list_add_tail(&carveout->node, &rproc->carveouts);

	return 0;

dma_free:
	dma_free_coherent(dev, rsc->len, va, dma);
free_carv:
	kfree(carveout);
free_mapping:
	kfree(mapping);
	return ret;
}

/*
 * A lookup table for resource handlers. The indices are defined in
 * enum fw_resource_type.
 */
static rproc_handle_resource_t rproc_handle_rsc[] = {
	[RSC_CARVEOUT] = (rproc_handle_resource_t)rproc_handle_carveout,
	[RSC_DEVMEM] = (rproc_handle_resource_t)rproc_handle_devmem,
	[RSC_TRACE] = (rproc_handle_resource_t)rproc_handle_trace,
	[RSC_VDEV] = NULL, /* VDEVs were handled upon registrarion */
};

/* handle firmware resource entries before booting the remote processor */
static int
rproc_handle_boot_rsc(struct rproc *rproc, struct resource_table *table, int len)
{
	struct device *dev = rproc->dev;
	rproc_handle_resource_t handler;
	int ret = 0, i;

	for (i = 0; i < table->num; i++) {
		int offset = table->offset[i];
		struct fw_rsc_hdr *hdr = (void *)table + offset;
		int avail = len - offset - sizeof(*hdr);
		void *rsc = (void *)hdr + sizeof(*hdr);

		/* make sure table isn't truncated */
		if (avail < 0) {
			dev_err(dev, "rsc table is truncated\n");
			return -EINVAL;
		}

		dev_dbg(dev, "rsc: type %d\n", hdr->type);

		if (hdr->type >= RSC_LAST) {
			dev_warn(dev, "unsupported resource %d\n", hdr->type);
			continue;
		}

		handler = rproc_handle_rsc[hdr->type];
		if (!handler)
			continue;

		ret = handler(rproc, rsc, avail);
		if (ret)
			break;
	}

	return ret;
}

/* handle firmware resource entries while registering the remote processor */
static int
rproc_handle_virtio_rsc(struct rproc *rproc, struct resource_table *table, int len)
{
	struct device *dev = rproc->dev;
	int ret = 0, i;

	for (i = 0; i < table->num; i++) {
		int offset = table->offset[i];
		struct fw_rsc_hdr *hdr = (void *)table + offset;
		int avail = len - offset - sizeof(*hdr);
		struct fw_rsc_vdev *vrsc;

		/* make sure table isn't truncated */
		if (avail < 0) {
			dev_err(dev, "rsc table is truncated\n");
			return -EINVAL;
		}

		dev_dbg(dev, "%s: rsc type %d\n", __func__, hdr->type);

		if (hdr->type != RSC_VDEV)
			continue;

		vrsc = (struct fw_rsc_vdev *)hdr->data;

		ret = rproc_handle_vdev(rproc, vrsc, avail);
		if (ret)
			break;
	}

	return ret;
}

/**
 * rproc_find_rsc_table() - find the resource table
 * @rproc: the rproc handle
 * @elf_data: the content of the ELF firmware image
 * @len: firmware size (in bytes)
 * @tablesz: place holder for providing back the table size
 *
 * This function finds the resource table inside the remote processor's
 * firmware. It is used both upon the registration of @rproc (in order
 * to look for and register the supported virito devices), and when the
 * @rproc is booted.
 *
 * Returns the pointer to the resource table if it is found, and write its
 * size into @tablesz. If a valid table isn't found, NULL is returned
 * (and @tablesz isn't set).
 */
static struct resource_table *
rproc_find_rsc_table(struct rproc *rproc, const u8 *elf_data, size_t len,
							int *tablesz)
{
	struct elf32_hdr *ehdr;
	struct elf32_shdr *shdr;
	const char *name_table;
	struct device *dev = rproc->dev;
	struct resource_table *table = NULL;
	int i;

	ehdr = (struct elf32_hdr *)elf_data;
	shdr = (struct elf32_shdr *)(elf_data + ehdr->e_shoff);
	name_table = elf_data + shdr[ehdr->e_shstrndx].sh_offset;

	/* look for the resource table and handle it */
	for (i = 0; i < ehdr->e_shnum; i++, shdr++) {
		int size = shdr->sh_size;
		int offset = shdr->sh_offset;

		if (strcmp(name_table + shdr->sh_name, ".resource_table"))
			continue;

		table = (struct resource_table *)(elf_data + offset);

		/* make sure we have the entire table */
		if (offset + size > len) {
			dev_err(dev, "resource table truncated\n");
			return NULL;
		}

		/* make sure table has at least the header */
		if (sizeof(struct resource_table) > size) {
			dev_err(dev, "header-less resource table\n");
			return NULL;
		}

		/* we don't support any version beyond the first */
		if (table->ver != 1) {
			dev_err(dev, "unsupported fw ver: %d\n", table->ver);
			return NULL;
		}

		/* make sure reserved bytes are zeroes */
		if (table->reserved[0] || table->reserved[1]) {
			dev_err(dev, "non zero reserved bytes\n");
			return NULL;
		}

		/* make sure the offsets array isn't truncated */
		if (table->num * sizeof(table->offset[0]) +
				sizeof(struct resource_table) > size) {
			dev_err(dev, "resource table incomplete\n");
			return NULL;
		}

		*tablesz = shdr->sh_size;
		break;
	}

	return table;
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
	struct device *dev = rproc->dev;

	/* clean up debugfs trace entries */
	list_for_each_entry_safe(entry, tmp, &rproc->traces, node) {
		rproc_remove_trace_file(entry->priv);
		rproc->num_traces--;
		list_del(&entry->node);
		kfree(entry);
	}

	/* clean up carveout allocations */
	list_for_each_entry_safe(entry, tmp, &rproc->carveouts, node) {
		dma_free_coherent(dev, entry->len, entry->va, entry->dma);
		list_del(&entry->node);
		kfree(entry);
	}

	/* clean up iommu mapping entries */
	list_for_each_entry_safe(entry, tmp, &rproc->mappings, node) {
		size_t unmapped;

		unmapped = iommu_unmap(rproc->domain, entry->da, entry->len);
		if (unmapped != entry->len) {
			/* nothing much to do besides complaining */
			dev_err(dev, "failed to unmap %u/%u\n", entry->len,
								unmapped);
		}

		list_del(&entry->node);
		kfree(entry);
	}
}

/* make sure this fw image is sane */
static int rproc_fw_sanity_check(struct rproc *rproc, const struct firmware *fw)
{
	const char *name = rproc->firmware;
	struct device *dev = rproc->dev;
	struct elf32_hdr *ehdr;
	char class;

	if (!fw) {
		dev_err(dev, "failed to load %s\n", name);
		return -EINVAL;
	}

	if (fw->size < sizeof(struct elf32_hdr)) {
		dev_err(dev, "Image is too small\n");
		return -EINVAL;
	}

	ehdr = (struct elf32_hdr *)fw->data;

	/* We only support ELF32 at this point */
	class = ehdr->e_ident[EI_CLASS];
	if (class != ELFCLASS32) {
		dev_err(dev, "Unsupported class: %d\n", class);
		return -EINVAL;
	}

	/* We assume the firmware has the same endianess as the host */
# ifdef __LITTLE_ENDIAN
	if (ehdr->e_ident[EI_DATA] != ELFDATA2LSB) {
# else /* BIG ENDIAN */
	if (ehdr->e_ident[EI_DATA] != ELFDATA2MSB) {
# endif
		dev_err(dev, "Unsupported firmware endianess\n");
		return -EINVAL;
	}

	if (fw->size < ehdr->e_shoff + sizeof(struct elf32_shdr)) {
		dev_err(dev, "Image is too small\n");
		return -EINVAL;
	}

	if (memcmp(ehdr->e_ident, ELFMAG, SELFMAG)) {
		dev_err(dev, "Image is corrupted (bad magic)\n");
		return -EINVAL;
	}

	if (ehdr->e_phnum == 0) {
		dev_err(dev, "No loadable segments\n");
		return -EINVAL;
	}

	if (ehdr->e_phoff > fw->size) {
		dev_err(dev, "Firmware size is too small\n");
		return -EINVAL;
	}

	return 0;
}

/*
 * take a firmware and boot a remote processor with it.
 */
static int rproc_fw_boot(struct rproc *rproc, const struct firmware *fw)
{
	struct device *dev = rproc->dev;
	const char *name = rproc->firmware;
	struct elf32_hdr *ehdr;
	struct resource_table *table;
	int ret, tablesz;

	ret = rproc_fw_sanity_check(rproc, fw);
	if (ret)
		return ret;

	ehdr = (struct elf32_hdr *)fw->data;

	dev_info(dev, "Booting fw image %s, size %d\n", name, fw->size);

	/*
	 * if enabling an IOMMU isn't relevant for this rproc, this is
	 * just a nop
	 */
	ret = rproc_enable_iommu(rproc);
	if (ret) {
		dev_err(dev, "can't enable iommu: %d\n", ret);
		return ret;
	}

	/*
	 * The ELF entry point is the rproc's boot addr (though this is not
	 * a configurable property of all remote processors: some will always
	 * boot at a specific hardcoded address).
	 */
	rproc->bootaddr = ehdr->e_entry;

	/* look for the resource table */
	table = rproc_find_rsc_table(rproc, fw->data, fw->size, &tablesz);
	if (!table)
		goto clean_up;

	/* handle fw resources which are required to boot rproc */
	ret = rproc_handle_boot_rsc(rproc, table, tablesz);
	if (ret) {
		dev_err(dev, "Failed to process resources: %d\n", ret);
		goto clean_up;
	}

	/* load the ELF segments to memory */
	ret = rproc_load_segments(rproc, fw->data, fw->size);
	if (ret) {
		dev_err(dev, "Failed to load program segments: %d\n", ret);
		goto clean_up;
	}

	/* power up the remote processor */
	ret = rproc->ops->start(rproc);
	if (ret) {
		dev_err(dev, "can't start rproc %s: %d\n", rproc->name, ret);
		goto clean_up;
	}

	rproc->state = RPROC_RUNNING;

	dev_info(dev, "remote processor %s is now up\n", rproc->name);

	return 0;

clean_up:
	rproc_resource_cleanup(rproc);
	rproc_disable_iommu(rproc);
	return ret;
}

/*
 * take a firmware and look for virtio devices to register.
 *
 * Note: this function is called asynchronously upon registration of the
 * remote processor (so we must wait until it completes before we try
 * to unregister the device. one other option is just to use kref here,
 * that might be cleaner).
 */
static void rproc_fw_config_virtio(const struct firmware *fw, void *context)
{
	struct rproc *rproc = context;
	struct resource_table *table;
	int ret, tablesz;

	if (rproc_fw_sanity_check(rproc, fw) < 0)
		goto out;

	/* look for the resource table */
	table = rproc_find_rsc_table(rproc, fw->data, fw->size, &tablesz);
	if (!table)
		goto out;

	/* look for virtio devices and register them */
	ret = rproc_handle_virtio_rsc(rproc, table, tablesz);
	if (ret)
		goto out;

out:
	release_firmware(fw);
	/* allow rproc_unregister() contexts, if any, to proceed */
	complete_all(&rproc->firmware_loading_complete);
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

	dev = rproc->dev;

	ret = mutex_lock_interruptible(&rproc->lock);
	if (ret) {
		dev_err(dev, "can't lock rproc %s: %d\n", rproc->name, ret);
		return ret;
	}

	/* loading a firmware is required */
	if (!rproc->firmware) {
		dev_err(dev, "%s: no firmware to load\n", __func__);
		ret = -EINVAL;
		goto unlock_mutex;
	}

	/* prevent underlying implementation from being removed */
	if (!try_module_get(dev->driver->owner)) {
		dev_err(dev, "%s: can't get owner\n", __func__);
		ret = -EINVAL;
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
	if (ret) {
		module_put(dev->driver->owner);
		atomic_dec(&rproc->power);
	}
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
 * - don't call rproc_shutdown() to unroll rproc_get_by_name(), exactly
 *   because rproc_shutdown() _does not_ decrement the refcount of @rproc.
 *   To decrement the refcount of @rproc, use rproc_put() (but _only_ if
 *   you acquired @rproc using rproc_get_by_name()).
 */
void rproc_shutdown(struct rproc *rproc)
{
	struct device *dev = rproc->dev;
	int ret;

	ret = mutex_lock_interruptible(&rproc->lock);
	if (ret) {
		dev_err(dev, "can't lock rproc %s: %d\n", rproc->name, ret);
		return;
	}

	/* if the remote proc is still needed, bail out */
	if (!atomic_dec_and_test(&rproc->power))
		goto out;

	/* power off the remote processor */
	ret = rproc->ops->stop(rproc);
	if (ret) {
		atomic_inc(&rproc->power);
		dev_err(dev, "can't stop rproc: %d\n", ret);
		goto out;
	}

	/* clean up all acquired resources */
	rproc_resource_cleanup(rproc);

	rproc_disable_iommu(rproc);

	rproc->state = RPROC_OFFLINE;

	dev_info(dev, "stopped remote processor %s\n", rproc->name);

out:
	mutex_unlock(&rproc->lock);
	if (!ret)
		module_put(dev->driver->owner);
}
EXPORT_SYMBOL(rproc_shutdown);

/**
 * rproc_release() - completely deletes the existence of a remote processor
 * @kref: the rproc's kref
 *
 * This function should _never_ be called directly.
 *
 * The only reasonable location to use it is as an argument when kref_put'ing
 * @rproc's refcount.
 *
 * This way it will be called when no one holds a valid pointer to this @rproc
 * anymore (and obviously after it is removed from the rprocs klist).
 *
 * Note: this function is not static because rproc_vdev_release() needs it when
 * it decrements @rproc's refcount.
 */
void rproc_release(struct kref *kref)
{
	struct rproc *rproc = container_of(kref, struct rproc, refcount);
	struct rproc_vdev *rvdev, *rvtmp;

	dev_info(rproc->dev, "removing %s\n", rproc->name);

	rproc_delete_debug_dir(rproc);

	/* clean up remote vdev entries */
	list_for_each_entry_safe(rvdev, rvtmp, &rproc->rvdevs, node) {
		__rproc_free_vrings(rvdev, RVDEV_NUM_VRINGS);
		list_del(&rvdev->node);
	}

	/*
	 * At this point no one holds a reference to rproc anymore,
	 * so we can directly unroll rproc_alloc()
	 */
	rproc_free(rproc);
}

/* will be called when an rproc is added to the rprocs klist */
static void klist_rproc_get(struct klist_node *n)
{
	struct rproc *rproc = container_of(n, struct rproc, node);

	kref_get(&rproc->refcount);
}

/* will be called when an rproc is removed from the rprocs klist */
static void klist_rproc_put(struct klist_node *n)
{
	struct rproc *rproc = container_of(n, struct rproc, node);

	kref_put(&rproc->refcount, rproc_release);
}

static struct rproc *next_rproc(struct klist_iter *i)
{
	struct klist_node *n;

	n = klist_next(i);
	if (!n)
		return NULL;

	return container_of(n, struct rproc, node);
}

/**
 * rproc_get_by_name() - find a remote processor by name and boot it
 * @name: name of the remote processor
 *
 * Finds an rproc handle using the remote processor's name, and then
 * boot it. If it's already powered on, then just immediately return
 * (successfully).
 *
 * Returns the rproc handle on success, and NULL on failure.
 *
 * This function increments the remote processor's refcount, so always
 * use rproc_put() to decrement it back once rproc isn't needed anymore.
 *
 * Note: currently this function (and its counterpart rproc_put()) are not
 * being used. We need to scrutinize the use cases
 * that still need them, and see if we can migrate them to use the non
 * name-based boot/shutdown interface.
 */
struct rproc *rproc_get_by_name(const char *name)
{
	struct rproc *rproc;
	struct klist_iter i;
	int ret;

	/* find the remote processor, and upref its refcount */
	klist_iter_init(&rprocs, &i);
	while ((rproc = next_rproc(&i)) != NULL)
		if (!strcmp(rproc->name, name)) {
			kref_get(&rproc->refcount);
			break;
		}
	klist_iter_exit(&i);

	/* can't find this rproc ? */
	if (!rproc) {
		pr_err("can't find remote processor %s\n", name);
		return NULL;
	}

	ret = rproc_boot(rproc);
	if (ret < 0) {
		kref_put(&rproc->refcount, rproc_release);
		return NULL;
	}

	return rproc;
}
EXPORT_SYMBOL(rproc_get_by_name);

/**
 * rproc_put() - decrement the refcount of a remote processor, and shut it down
 * @rproc: the remote processor
 *
 * This function tries to shutdown @rproc, and it then decrements its
 * refcount.
 *
 * After this function returns, @rproc may _not_ be used anymore, and its
 * handle should be considered invalid.
 *
 * This function should be called _iff_ the @rproc handle was grabbed by
 * calling rproc_get_by_name().
 */
void rproc_put(struct rproc *rproc)
{
	/* try to power off the remote processor */
	rproc_shutdown(rproc);

	/* downref rproc's refcount */
	kref_put(&rproc->refcount, rproc_release);
}
EXPORT_SYMBOL(rproc_put);

/**
 * rproc_register() - register a remote processor
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
int rproc_register(struct rproc *rproc)
{
	struct device *dev = rproc->dev;
	int ret = 0;

	/* expose to rproc_get_by_name users */
	klist_add_tail(&rproc->node, &rprocs);

	dev_info(rproc->dev, "%s is available\n", rproc->name);

	dev_info(dev, "Note: remoteproc is still under development and considered experimental.\n");
	dev_info(dev, "THE BINARY FORMAT IS NOT YET FINALIZED, and backward compatibility isn't yet guaranteed.\n");

	/* create debugfs entries */
	rproc_create_debug_dir(rproc);

	/* rproc_unregister() calls must wait until async loader completes */
	init_completion(&rproc->firmware_loading_complete);

	/*
	 * We must retrieve early virtio configuration info from
	 * the firmware (e.g. whether to register a virtio device,
	 * what virtio features does it support, ...).
	 *
	 * We're initiating an asynchronous firmware loading, so we can
	 * be built-in kernel code, without hanging the boot process.
	 */
	ret = request_firmware_nowait(THIS_MODULE, FW_ACTION_HOTPLUG,
					rproc->firmware, dev, GFP_KERNEL,
					rproc, rproc_fw_config_virtio);
	if (ret < 0) {
		dev_err(dev, "request_firmware_nowait failed: %d\n", ret);
		complete_all(&rproc->firmware_loading_complete);
		klist_remove(&rproc->node);
	}

	return ret;
}
EXPORT_SYMBOL(rproc_register);

/**
 * rproc_alloc() - allocate a remote processor handle
 * @dev: the underlying device
 * @name: name of this remote processor
 * @ops: platform-specific handlers (mainly start/stop)
 * @firmware: name of firmware file to load
 * @len: length of private data needed by the rproc driver (in bytes)
 *
 * Allocates a new remote processor handle, but does not register
 * it yet.
 *
 * This function should be used by rproc implementations during initialization
 * of the remote processor.
 *
 * After creating an rproc handle using this function, and when ready,
 * implementations should then call rproc_register() to complete
 * the registration of the remote processor.
 *
 * On success the new rproc is returned, and on failure, NULL.
 *
 * Note: _never_ directly deallocate @rproc, even if it was not registered
 * yet. Instead, if you just need to unroll rproc_alloc(), use rproc_free().
 */
struct rproc *rproc_alloc(struct device *dev, const char *name,
				const struct rproc_ops *ops,
				const char *firmware, int len)
{
	struct rproc *rproc;

	if (!dev || !name || !ops)
		return NULL;

	rproc = kzalloc(sizeof(struct rproc) + len, GFP_KERNEL);
	if (!rproc) {
		dev_err(dev, "%s: kzalloc failed\n", __func__);
		return NULL;
	}

	rproc->dev = dev;
	rproc->name = name;
	rproc->ops = ops;
	rproc->firmware = firmware;
	rproc->priv = &rproc[1];

	atomic_set(&rproc->power, 0);

	kref_init(&rproc->refcount);

	mutex_init(&rproc->lock);

	idr_init(&rproc->notifyids);

	INIT_LIST_HEAD(&rproc->carveouts);
	INIT_LIST_HEAD(&rproc->mappings);
	INIT_LIST_HEAD(&rproc->traces);
	INIT_LIST_HEAD(&rproc->rvdevs);

	rproc->state = RPROC_OFFLINE;

	return rproc;
}
EXPORT_SYMBOL(rproc_alloc);

/**
 * rproc_free() - free an rproc handle that was allocated by rproc_alloc
 * @rproc: the remote processor handle
 *
 * This function should _only_ be used if @rproc was only allocated,
 * but not registered yet.
 *
 * If @rproc was already successfully registered (by calling rproc_register()),
 * then use rproc_unregister() instead.
 */
void rproc_free(struct rproc *rproc)
{
	idr_remove_all(&rproc->notifyids);
	idr_destroy(&rproc->notifyids);

	kfree(rproc);
}
EXPORT_SYMBOL(rproc_free);

/**
 * rproc_unregister() - unregister a remote processor
 * @rproc: rproc handle to unregister
 *
 * Unregisters a remote processor, and decrements its refcount.
 * If its refcount drops to zero, then @rproc will be freed. If not,
 * it will be freed later once the last reference is dropped.
 *
 * This function should be called when the platform specific rproc
 * implementation decides to remove the rproc device. it should
 * _only_ be called if a previous invocation of rproc_register()
 * has completed successfully.
 *
 * After rproc_unregister() returns, @rproc is _not_ valid anymore and
 * it shouldn't be used. More specifically, don't call rproc_free()
 * or try to directly free @rproc after rproc_unregister() returns;
 * none of these are needed, and calling them is a bug.
 *
 * Returns 0 on success and -EINVAL if @rproc isn't valid.
 */
int rproc_unregister(struct rproc *rproc)
{
	struct rproc_vdev *rvdev;

	if (!rproc)
		return -EINVAL;

	/* if rproc is just being registered, wait */
	wait_for_completion(&rproc->firmware_loading_complete);

	/* clean up remote vdev entries */
	list_for_each_entry(rvdev, &rproc->rvdevs, node)
		rproc_remove_virtio_dev(rvdev);

	/* the rproc is downref'ed as soon as it's removed from the klist */
	klist_del(&rproc->node);

	/* the rproc will only be released after its refcount drops to zero */
	kref_put(&rproc->refcount, rproc_release);

	return 0;
}
EXPORT_SYMBOL(rproc_unregister);

static int __init remoteproc_init(void)
{
	rproc_init_debugfs();
	return 0;
}
module_init(remoteproc_init);

static void __exit remoteproc_exit(void)
{
	rproc_exit_debugfs();
}
module_exit(remoteproc_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Generic Remote Processor Framework");

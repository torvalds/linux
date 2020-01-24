// SPDX-License-Identifier: GPL-2.0-only
/*
 * VFIO PCI NVIDIA Whitherspoon GPU support a.k.a. NVLink2.
 *
 * Copyright (C) 2018 IBM Corp.  All rights reserved.
 *     Author: Alexey Kardashevskiy <aik@ozlabs.ru>
 *
 * Register an on-GPU RAM region for cacheable access.
 *
 * Derived from original vfio_pci_igd.c:
 * Copyright (C) 2016 Red Hat, Inc.  All rights reserved.
 *	Author: Alex Williamson <alex.williamson@redhat.com>
 */

#include <linux/io.h>
#include <linux/pci.h>
#include <linux/uaccess.h>
#include <linux/vfio.h>
#include <linux/sched/mm.h>
#include <linux/mmu_context.h>
#include <asm/kvm_ppc.h>
#include "vfio_pci_private.h"

#define CREATE_TRACE_POINTS
#include "trace.h"

EXPORT_TRACEPOINT_SYMBOL_GPL(vfio_pci_nvgpu_mmap_fault);
EXPORT_TRACEPOINT_SYMBOL_GPL(vfio_pci_nvgpu_mmap);
EXPORT_TRACEPOINT_SYMBOL_GPL(vfio_pci_npu2_mmap);

struct vfio_pci_nvgpu_data {
	unsigned long gpu_hpa; /* GPU RAM physical address */
	unsigned long gpu_tgt; /* TGT address of corresponding GPU RAM */
	unsigned long useraddr; /* GPU RAM userspace address */
	unsigned long size; /* Size of the GPU RAM window (usually 128GB) */
	struct mm_struct *mm;
	struct mm_iommu_table_group_mem_t *mem; /* Pre-registered RAM descr. */
	struct pci_dev *gpdev;
	struct notifier_block group_notifier;
};

static size_t vfio_pci_nvgpu_rw(struct vfio_pci_device *vdev,
		char __user *buf, size_t count, loff_t *ppos, bool iswrite)
{
	unsigned int i = VFIO_PCI_OFFSET_TO_INDEX(*ppos) - VFIO_PCI_NUM_REGIONS;
	struct vfio_pci_nvgpu_data *data = vdev->region[i].data;
	loff_t pos = *ppos & VFIO_PCI_OFFSET_MASK;
	loff_t posaligned = pos & PAGE_MASK, posoff = pos & ~PAGE_MASK;
	size_t sizealigned;
	void __iomem *ptr;

	if (pos >= vdev->region[i].size)
		return -EINVAL;

	count = min(count, (size_t)(vdev->region[i].size - pos));

	/*
	 * We map only a bit of GPU RAM for a short time instead of mapping it
	 * for the guest lifetime as:
	 *
	 * 1) we do not know GPU RAM size, only aperture which is 4-8 times
	 *    bigger than actual RAM size (16/32GB RAM vs. 128GB aperture);
	 * 2) mapping GPU RAM allows CPU to prefetch and if this happens
	 *    before NVLink bridge is reset (which fences GPU RAM),
	 *    hardware management interrupts (HMI) might happen, this
	 *    will freeze NVLink bridge.
	 *
	 * This is not fast path anyway.
	 */
	sizealigned = _ALIGN_UP(posoff + count, PAGE_SIZE);
	ptr = ioremap_cache(data->gpu_hpa + posaligned, sizealigned);
	if (!ptr)
		return -EFAULT;

	if (iswrite) {
		if (copy_from_user(ptr + posoff, buf, count))
			count = -EFAULT;
		else
			*ppos += count;
	} else {
		if (copy_to_user(buf, ptr + posoff, count))
			count = -EFAULT;
		else
			*ppos += count;
	}

	iounmap(ptr);

	return count;
}

static void vfio_pci_nvgpu_release(struct vfio_pci_device *vdev,
		struct vfio_pci_region *region)
{
	struct vfio_pci_nvgpu_data *data = region->data;
	long ret;

	/* If there were any mappings at all... */
	if (data->mm) {
		ret = mm_iommu_put(data->mm, data->mem);
		WARN_ON(ret);

		mmdrop(data->mm);
	}

	vfio_unregister_notifier(&data->gpdev->dev, VFIO_GROUP_NOTIFY,
			&data->group_notifier);

	pnv_npu2_unmap_lpar_dev(data->gpdev);

	kfree(data);
}

static vm_fault_t vfio_pci_nvgpu_mmap_fault(struct vm_fault *vmf)
{
	vm_fault_t ret;
	struct vm_area_struct *vma = vmf->vma;
	struct vfio_pci_region *region = vma->vm_private_data;
	struct vfio_pci_nvgpu_data *data = region->data;
	unsigned long vmf_off = (vmf->address - vma->vm_start) >> PAGE_SHIFT;
	unsigned long nv2pg = data->gpu_hpa >> PAGE_SHIFT;
	unsigned long vm_pgoff = vma->vm_pgoff &
		((1U << (VFIO_PCI_OFFSET_SHIFT - PAGE_SHIFT)) - 1);
	unsigned long pfn = nv2pg + vm_pgoff + vmf_off;

	ret = vmf_insert_pfn(vma, vmf->address, pfn);
	trace_vfio_pci_nvgpu_mmap_fault(data->gpdev, pfn << PAGE_SHIFT,
			vmf->address, ret);

	return ret;
}

static const struct vm_operations_struct vfio_pci_nvgpu_mmap_vmops = {
	.fault = vfio_pci_nvgpu_mmap_fault,
};

static int vfio_pci_nvgpu_mmap(struct vfio_pci_device *vdev,
		struct vfio_pci_region *region, struct vm_area_struct *vma)
{
	int ret;
	struct vfio_pci_nvgpu_data *data = region->data;

	if (data->useraddr)
		return -EPERM;

	if (vma->vm_end - vma->vm_start > data->size)
		return -EINVAL;

	vma->vm_private_data = region;
	vma->vm_flags |= VM_PFNMAP;
	vma->vm_ops = &vfio_pci_nvgpu_mmap_vmops;

	/*
	 * Calling mm_iommu_newdev() here once as the region is not
	 * registered yet and therefore right initialization will happen now.
	 * Other places will use mm_iommu_find() which returns
	 * registered @mem and does not go gup().
	 */
	data->useraddr = vma->vm_start;
	data->mm = current->mm;

	atomic_inc(&data->mm->mm_count);
	ret = (int) mm_iommu_newdev(data->mm, data->useraddr,
			vma_pages(vma), data->gpu_hpa, &data->mem);

	trace_vfio_pci_nvgpu_mmap(vdev->pdev, data->gpu_hpa, data->useraddr,
			vma->vm_end - vma->vm_start, ret);

	return ret;
}

static int vfio_pci_nvgpu_add_capability(struct vfio_pci_device *vdev,
		struct vfio_pci_region *region, struct vfio_info_cap *caps)
{
	struct vfio_pci_nvgpu_data *data = region->data;
	struct vfio_region_info_cap_nvlink2_ssatgt cap = {
		.header.id = VFIO_REGION_INFO_CAP_NVLINK2_SSATGT,
		.header.version = 1,
		.tgt = data->gpu_tgt
	};

	return vfio_info_add_capability(caps, &cap.header, sizeof(cap));
}

static const struct vfio_pci_regops vfio_pci_nvgpu_regops = {
	.rw = vfio_pci_nvgpu_rw,
	.release = vfio_pci_nvgpu_release,
	.mmap = vfio_pci_nvgpu_mmap,
	.add_capability = vfio_pci_nvgpu_add_capability,
};

static int vfio_pci_nvgpu_group_notifier(struct notifier_block *nb,
		unsigned long action, void *opaque)
{
	struct kvm *kvm = opaque;
	struct vfio_pci_nvgpu_data *data = container_of(nb,
			struct vfio_pci_nvgpu_data,
			group_notifier);

	if (action == VFIO_GROUP_NOTIFY_SET_KVM && kvm &&
			pnv_npu2_map_lpar_dev(data->gpdev,
				kvm->arch.lpid, MSR_DR | MSR_PR))
		return NOTIFY_BAD;

	return NOTIFY_OK;
}

int vfio_pci_nvdia_v100_nvlink2_init(struct vfio_pci_device *vdev)
{
	int ret;
	u64 reg[2];
	u64 tgt = 0;
	struct device_node *npu_node, *mem_node;
	struct pci_dev *npu_dev;
	struct vfio_pci_nvgpu_data *data;
	uint32_t mem_phandle = 0;
	unsigned long events = VFIO_GROUP_NOTIFY_SET_KVM;

	/*
	 * PCI config space does not tell us about NVLink presense but
	 * platform does, use this.
	 */
	npu_dev = pnv_pci_get_npu_dev(vdev->pdev, 0);
	if (!npu_dev)
		return -ENODEV;

	npu_node = pci_device_to_OF_node(npu_dev);
	if (!npu_node)
		return -EINVAL;

	if (of_property_read_u32(npu_node, "memory-region", &mem_phandle))
		return -EINVAL;

	mem_node = of_find_node_by_phandle(mem_phandle);
	if (!mem_node)
		return -EINVAL;

	if (of_property_read_variable_u64_array(mem_node, "reg", reg,
				ARRAY_SIZE(reg), ARRAY_SIZE(reg)) !=
			ARRAY_SIZE(reg))
		return -EINVAL;

	if (of_property_read_u64(npu_node, "ibm,device-tgt-addr", &tgt)) {
		dev_warn(&vdev->pdev->dev, "No ibm,device-tgt-addr found\n");
		return -EFAULT;
	}

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->gpu_hpa = reg[0];
	data->gpu_tgt = tgt;
	data->size = reg[1];

	dev_dbg(&vdev->pdev->dev, "%lx..%lx\n", data->gpu_hpa,
			data->gpu_hpa + data->size - 1);

	data->gpdev = vdev->pdev;
	data->group_notifier.notifier_call = vfio_pci_nvgpu_group_notifier;

	ret = vfio_register_notifier(&data->gpdev->dev, VFIO_GROUP_NOTIFY,
			&events, &data->group_notifier);
	if (ret)
		goto free_exit;

	/*
	 * We have just set KVM, we do not need the listener anymore.
	 * Also, keeping it registered means that if more than one GPU is
	 * assigned, we will get several similar notifiers notifying about
	 * the same device again which does not help with anything.
	 */
	vfio_unregister_notifier(&data->gpdev->dev, VFIO_GROUP_NOTIFY,
			&data->group_notifier);

	ret = vfio_pci_register_dev_region(vdev,
			PCI_VENDOR_ID_NVIDIA | VFIO_REGION_TYPE_PCI_VENDOR_TYPE,
			VFIO_REGION_SUBTYPE_NVIDIA_NVLINK2_RAM,
			&vfio_pci_nvgpu_regops,
			data->size,
			VFIO_REGION_INFO_FLAG_READ |
			VFIO_REGION_INFO_FLAG_WRITE |
			VFIO_REGION_INFO_FLAG_MMAP,
			data);
	if (ret)
		goto free_exit;

	return 0;
free_exit:
	kfree(data);

	return ret;
}

/*
 * IBM NPU2 bridge
 */
struct vfio_pci_npu2_data {
	void *base; /* ATSD register virtual address, for emulated access */
	unsigned long mmio_atsd; /* ATSD physical address */
	unsigned long gpu_tgt; /* TGT address of corresponding GPU RAM */
	unsigned int link_speed; /* The link speed from DT's ibm,nvlink-speed */
};

static size_t vfio_pci_npu2_rw(struct vfio_pci_device *vdev,
		char __user *buf, size_t count, loff_t *ppos, bool iswrite)
{
	unsigned int i = VFIO_PCI_OFFSET_TO_INDEX(*ppos) - VFIO_PCI_NUM_REGIONS;
	struct vfio_pci_npu2_data *data = vdev->region[i].data;
	loff_t pos = *ppos & VFIO_PCI_OFFSET_MASK;

	if (pos >= vdev->region[i].size)
		return -EINVAL;

	count = min(count, (size_t)(vdev->region[i].size - pos));

	if (iswrite) {
		if (copy_from_user(data->base + pos, buf, count))
			return -EFAULT;
	} else {
		if (copy_to_user(buf, data->base + pos, count))
			return -EFAULT;
	}
	*ppos += count;

	return count;
}

static int vfio_pci_npu2_mmap(struct vfio_pci_device *vdev,
		struct vfio_pci_region *region, struct vm_area_struct *vma)
{
	int ret;
	struct vfio_pci_npu2_data *data = region->data;
	unsigned long req_len = vma->vm_end - vma->vm_start;

	if (req_len != PAGE_SIZE)
		return -EINVAL;

	vma->vm_flags |= VM_PFNMAP;
	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);

	ret = remap_pfn_range(vma, vma->vm_start, data->mmio_atsd >> PAGE_SHIFT,
			req_len, vma->vm_page_prot);
	trace_vfio_pci_npu2_mmap(vdev->pdev, data->mmio_atsd, vma->vm_start,
			vma->vm_end - vma->vm_start, ret);

	return ret;
}

static void vfio_pci_npu2_release(struct vfio_pci_device *vdev,
		struct vfio_pci_region *region)
{
	struct vfio_pci_npu2_data *data = region->data;

	memunmap(data->base);
	kfree(data);
}

static int vfio_pci_npu2_add_capability(struct vfio_pci_device *vdev,
		struct vfio_pci_region *region, struct vfio_info_cap *caps)
{
	struct vfio_pci_npu2_data *data = region->data;
	struct vfio_region_info_cap_nvlink2_ssatgt captgt = {
		.header.id = VFIO_REGION_INFO_CAP_NVLINK2_SSATGT,
		.header.version = 1,
		.tgt = data->gpu_tgt
	};
	struct vfio_region_info_cap_nvlink2_lnkspd capspd = {
		.header.id = VFIO_REGION_INFO_CAP_NVLINK2_LNKSPD,
		.header.version = 1,
		.link_speed = data->link_speed
	};
	int ret;

	ret = vfio_info_add_capability(caps, &captgt.header, sizeof(captgt));
	if (ret)
		return ret;

	return vfio_info_add_capability(caps, &capspd.header, sizeof(capspd));
}

static const struct vfio_pci_regops vfio_pci_npu2_regops = {
	.rw = vfio_pci_npu2_rw,
	.mmap = vfio_pci_npu2_mmap,
	.release = vfio_pci_npu2_release,
	.add_capability = vfio_pci_npu2_add_capability,
};

int vfio_pci_ibm_npu2_init(struct vfio_pci_device *vdev)
{
	int ret;
	struct vfio_pci_npu2_data *data;
	struct device_node *nvlink_dn;
	u32 nvlink_index = 0;
	struct pci_dev *npdev = vdev->pdev;
	struct device_node *npu_node = pci_device_to_OF_node(npdev);
	struct pci_controller *hose = pci_bus_to_host(npdev->bus);
	u64 mmio_atsd = 0;
	u64 tgt = 0;
	u32 link_speed = 0xff;

	/*
	 * PCI config space does not tell us about NVLink presense but
	 * platform does, use this.
	 */
	if (!pnv_pci_get_gpu_dev(vdev->pdev))
		return -ENODEV;

	/*
	 * NPU2 normally has 8 ATSD registers (for concurrency) and 6 links
	 * so we can allocate one register per link, using nvlink index as
	 * a key.
	 * There is always at least one ATSD register so as long as at least
	 * NVLink bridge #0 is passed to the guest, ATSD will be available.
	 */
	nvlink_dn = of_parse_phandle(npdev->dev.of_node, "ibm,nvlink", 0);
	if (WARN_ON(of_property_read_u32(nvlink_dn, "ibm,npu-link-index",
			&nvlink_index)))
		return -ENODEV;

	if (of_property_read_u64_index(hose->dn, "ibm,mmio-atsd", nvlink_index,
			&mmio_atsd)) {
		dev_warn(&vdev->pdev->dev, "No available ATSD found\n");
		mmio_atsd = 0;
	}

	if (of_property_read_u64(npu_node, "ibm,device-tgt-addr", &tgt)) {
		dev_warn(&vdev->pdev->dev, "No ibm,device-tgt-addr found\n");
		return -EFAULT;
	}

	if (of_property_read_u32(npu_node, "ibm,nvlink-speed", &link_speed)) {
		dev_warn(&vdev->pdev->dev, "No ibm,nvlink-speed found\n");
		return -EFAULT;
	}

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->mmio_atsd = mmio_atsd;
	data->gpu_tgt = tgt;
	data->link_speed = link_speed;
	if (data->mmio_atsd) {
		data->base = memremap(data->mmio_atsd, SZ_64K, MEMREMAP_WT);
		if (!data->base) {
			ret = -ENOMEM;
			goto free_exit;
		}
	}

	/*
	 * We want to expose the capability even if this specific NVLink
	 * did not get its own ATSD register because capabilities
	 * belong to VFIO regions and normally there will be ATSD register
	 * assigned to the NVLink bridge.
	 */
	ret = vfio_pci_register_dev_region(vdev,
			PCI_VENDOR_ID_IBM |
			VFIO_REGION_TYPE_PCI_VENDOR_TYPE,
			VFIO_REGION_SUBTYPE_IBM_NVLINK2_ATSD,
			&vfio_pci_npu2_regops,
			data->mmio_atsd ? PAGE_SIZE : 0,
			VFIO_REGION_INFO_FLAG_READ |
			VFIO_REGION_INFO_FLAG_WRITE |
			VFIO_REGION_INFO_FLAG_MMAP,
			data);
	if (ret)
		goto free_exit;

	return 0;

free_exit:
	if (data->base)
		memunmap(data->base);
	kfree(data);

	return ret;
}

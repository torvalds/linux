// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2024, NVIDIA CORPORATION & AFFILIATES. All rights reserved
 */

#include <linux/sizes.h>
#include <linux/vfio_pci_core.h>
#include <linux/delay.h>
#include <linux/jiffies.h>

/*
 * The device memory usable to the workloads running in the VM is cached
 * and showcased as a 64b device BAR (comprising of BAR4 and BAR5 region)
 * to the VM and is represented as usemem.
 * Moreover, the VM GPU device driver needs a non-cacheable region to
 * support the MIG feature. This region is also exposed as a 64b BAR
 * (comprising of BAR2 and BAR3 region) and represented as resmem.
 */
#define RESMEM_REGION_INDEX VFIO_PCI_BAR2_REGION_INDEX
#define USEMEM_REGION_INDEX VFIO_PCI_BAR4_REGION_INDEX

/* A hardwired and constant ABI value between the GPU FW and VFIO driver. */
#define MEMBLK_SIZE SZ_512M

#define DVSEC_BITMAP_OFFSET 0xA
#define MIG_SUPPORTED_WITH_CACHED_RESMEM BIT(0)

#define GPU_CAP_DVSEC_REGISTER 3

#define C2C_LINK_BAR0_OFFSET 0x1498
#define HBM_TRAINING_BAR0_OFFSET 0x200BC
#define STATUS_READY 0xFF

#define POLL_QUANTUM_MS 1000
#define POLL_TIMEOUT_MS (30 * 1000)

/*
 * The state of the two device memory region - resmem and usemem - is
 * saved as struct mem_region.
 */
struct mem_region {
	phys_addr_t memphys;    /* Base physical address of the region */
	size_t memlength;       /* Region size */
	size_t bar_size;        /* Reported region BAR size */
	__le64 bar_val;         /* Emulated BAR offset registers */
	union {
		void *memaddr;
		void __iomem *ioaddr;
	};                      /* Base virtual address of the region */
};

struct nvgrace_gpu_pci_core_device {
	struct vfio_pci_core_device core_device;
	/* Cached and usable memory for the VM. */
	struct mem_region usemem;
	/* Non cached memory carved out from the end of device memory */
	struct mem_region resmem;
	/* Lock to control device memory kernel mapping */
	struct mutex remap_lock;
	bool has_mig_hw_bug;
};

static void nvgrace_gpu_init_fake_bar_emu_regs(struct vfio_device *core_vdev)
{
	struct nvgrace_gpu_pci_core_device *nvdev =
		container_of(core_vdev, struct nvgrace_gpu_pci_core_device,
			     core_device.vdev);

	nvdev->resmem.bar_val = 0;
	nvdev->usemem.bar_val = 0;
}

/* Choose the structure corresponding to the fake BAR with a given index. */
static struct mem_region *
nvgrace_gpu_memregion(int index,
		      struct nvgrace_gpu_pci_core_device *nvdev)
{
	if (index == USEMEM_REGION_INDEX)
		return &nvdev->usemem;

	if (nvdev->resmem.memlength && index == RESMEM_REGION_INDEX)
		return &nvdev->resmem;

	return NULL;
}

static int nvgrace_gpu_open_device(struct vfio_device *core_vdev)
{
	struct vfio_pci_core_device *vdev =
		container_of(core_vdev, struct vfio_pci_core_device, vdev);
	struct nvgrace_gpu_pci_core_device *nvdev =
		container_of(core_vdev, struct nvgrace_gpu_pci_core_device,
			     core_device.vdev);
	int ret;

	ret = vfio_pci_core_enable(vdev);
	if (ret)
		return ret;

	if (nvdev->usemem.memlength) {
		nvgrace_gpu_init_fake_bar_emu_regs(core_vdev);
		mutex_init(&nvdev->remap_lock);
	}

	vfio_pci_core_finish_enable(vdev);

	return 0;
}

static void nvgrace_gpu_close_device(struct vfio_device *core_vdev)
{
	struct nvgrace_gpu_pci_core_device *nvdev =
		container_of(core_vdev, struct nvgrace_gpu_pci_core_device,
			     core_device.vdev);

	/* Unmap the mapping to the device memory cached region */
	if (nvdev->usemem.memaddr) {
		memunmap(nvdev->usemem.memaddr);
		nvdev->usemem.memaddr = NULL;
	}

	/* Unmap the mapping to the device memory non-cached region */
	if (nvdev->resmem.ioaddr) {
		iounmap(nvdev->resmem.ioaddr);
		nvdev->resmem.ioaddr = NULL;
	}

	mutex_destroy(&nvdev->remap_lock);

	vfio_pci_core_close_device(core_vdev);
}

static int nvgrace_gpu_mmap(struct vfio_device *core_vdev,
			    struct vm_area_struct *vma)
{
	struct nvgrace_gpu_pci_core_device *nvdev =
		container_of(core_vdev, struct nvgrace_gpu_pci_core_device,
			     core_device.vdev);
	struct mem_region *memregion;
	unsigned long start_pfn;
	u64 req_len, pgoff, end;
	unsigned int index;
	int ret = 0;

	index = vma->vm_pgoff >> (VFIO_PCI_OFFSET_SHIFT - PAGE_SHIFT);

	memregion = nvgrace_gpu_memregion(index, nvdev);
	if (!memregion)
		return vfio_pci_core_mmap(core_vdev, vma);

	/*
	 * Request to mmap the BAR. Map to the CPU accessible memory on the
	 * GPU using the memory information gathered from the system ACPI
	 * tables.
	 */
	pgoff = vma->vm_pgoff &
		((1U << (VFIO_PCI_OFFSET_SHIFT - PAGE_SHIFT)) - 1);

	if (check_sub_overflow(vma->vm_end, vma->vm_start, &req_len) ||
	    check_add_overflow(PHYS_PFN(memregion->memphys), pgoff, &start_pfn) ||
	    check_add_overflow(PFN_PHYS(pgoff), req_len, &end))
		return -EOVERFLOW;

	/*
	 * Check that the mapping request does not go beyond available device
	 * memory size
	 */
	if (end > memregion->memlength)
		return -EINVAL;

	/*
	 * The carved out region of the device memory needs the NORMAL_NC
	 * property. Communicate as such to the hypervisor.
	 */
	if (index == RESMEM_REGION_INDEX) {
		/*
		 * The nvgrace-gpu module has no issues with uncontained
		 * failures on NORMAL_NC accesses. VM_ALLOW_ANY_UNCACHED is
		 * set to communicate to the KVM to S2 map as NORMAL_NC.
		 * This opens up guest usage of NORMAL_NC for this mapping.
		 */
		vm_flags_set(vma, VM_ALLOW_ANY_UNCACHED);

		vma->vm_page_prot = pgprot_writecombine(vma->vm_page_prot);
	}

	/*
	 * Perform a PFN map to the memory and back the device BAR by the
	 * GPU memory.
	 *
	 * The available GPU memory size may not be power-of-2 aligned. The
	 * remainder is only backed by vfio_device_ops read/write handlers.
	 *
	 * During device reset, the GPU is safely disconnected to the CPU
	 * and access to the BAR will be immediately returned preventing
	 * machine check.
	 */
	ret = remap_pfn_range(vma, vma->vm_start, start_pfn,
			      req_len, vma->vm_page_prot);
	if (ret)
		return ret;

	vma->vm_pgoff = start_pfn;

	return 0;
}

static long
nvgrace_gpu_ioctl_get_region_info(struct vfio_device *core_vdev,
				  unsigned long arg)
{
	struct nvgrace_gpu_pci_core_device *nvdev =
		container_of(core_vdev, struct nvgrace_gpu_pci_core_device,
			     core_device.vdev);
	unsigned long minsz = offsetofend(struct vfio_region_info, offset);
	struct vfio_info_cap caps = { .buf = NULL, .size = 0 };
	struct vfio_region_info_cap_sparse_mmap *sparse;
	struct vfio_region_info info;
	struct mem_region *memregion;
	u32 size;
	int ret;

	if (copy_from_user(&info, (void __user *)arg, minsz))
		return -EFAULT;

	if (info.argsz < minsz)
		return -EINVAL;

	/*
	 * Request to determine the BAR region information. Send the
	 * GPU memory information.
	 */
	memregion = nvgrace_gpu_memregion(info.index, nvdev);
	if (!memregion)
		return vfio_pci_core_ioctl(core_vdev,
					   VFIO_DEVICE_GET_REGION_INFO, arg);

	size = struct_size(sparse, areas, 1);

	/*
	 * Setup for sparse mapping for the device memory. Only the
	 * available device memory on the hardware is shown as a
	 * mappable region.
	 */
	sparse = kzalloc(size, GFP_KERNEL);
	if (!sparse)
		return -ENOMEM;

	sparse->nr_areas = 1;
	sparse->areas[0].offset = 0;
	sparse->areas[0].size = memregion->memlength;
	sparse->header.id = VFIO_REGION_INFO_CAP_SPARSE_MMAP;
	sparse->header.version = 1;

	ret = vfio_info_add_capability(&caps, &sparse->header, size);
	kfree(sparse);
	if (ret)
		return ret;

	info.offset = VFIO_PCI_INDEX_TO_OFFSET(info.index);
	/*
	 * The region memory size may not be power-of-2 aligned.
	 * Given that the memory is a BAR and may not be
	 * aligned, roundup to the next power-of-2.
	 */
	info.size = memregion->bar_size;
	info.flags = VFIO_REGION_INFO_FLAG_READ |
		     VFIO_REGION_INFO_FLAG_WRITE |
		     VFIO_REGION_INFO_FLAG_MMAP;

	if (caps.size) {
		info.flags |= VFIO_REGION_INFO_FLAG_CAPS;
		if (info.argsz < sizeof(info) + caps.size) {
			info.argsz = sizeof(info) + caps.size;
			info.cap_offset = 0;
		} else {
			vfio_info_cap_shift(&caps, sizeof(info));
			if (copy_to_user((void __user *)arg +
					 sizeof(info), caps.buf,
					 caps.size)) {
				kfree(caps.buf);
				return -EFAULT;
			}
			info.cap_offset = sizeof(info);
		}
		kfree(caps.buf);
	}
	return copy_to_user((void __user *)arg, &info, minsz) ?
			    -EFAULT : 0;
}

static long nvgrace_gpu_ioctl(struct vfio_device *core_vdev,
			      unsigned int cmd, unsigned long arg)
{
	switch (cmd) {
	case VFIO_DEVICE_GET_REGION_INFO:
		return nvgrace_gpu_ioctl_get_region_info(core_vdev, arg);
	case VFIO_DEVICE_IOEVENTFD:
		return -ENOTTY;
	case VFIO_DEVICE_RESET:
		nvgrace_gpu_init_fake_bar_emu_regs(core_vdev);
		fallthrough;
	default:
		return vfio_pci_core_ioctl(core_vdev, cmd, arg);
	}
}

static __le64
nvgrace_gpu_get_read_value(size_t bar_size, u64 flags, __le64 val64)
{
	u64 tmp_val;

	tmp_val = le64_to_cpu(val64);
	tmp_val &= ~(bar_size - 1);
	tmp_val |= flags;

	return cpu_to_le64(tmp_val);
}

/*
 * Both the usable (usemem) and the reserved (resmem) device memory region
 * are exposed as a 64b fake device BARs in the VM. These fake BARs must
 * respond to the accesses on their respective PCI config space offsets.
 *
 * resmem BAR owns PCI_BASE_ADDRESS_2 & PCI_BASE_ADDRESS_3.
 * usemem BAR owns PCI_BASE_ADDRESS_4 & PCI_BASE_ADDRESS_5.
 */
static ssize_t
nvgrace_gpu_read_config_emu(struct vfio_device *core_vdev,
			    char __user *buf, size_t count, loff_t *ppos)
{
	struct nvgrace_gpu_pci_core_device *nvdev =
		container_of(core_vdev, struct nvgrace_gpu_pci_core_device,
			     core_device.vdev);
	u64 pos = *ppos & VFIO_PCI_OFFSET_MASK;
	struct mem_region *memregion = NULL;
	__le64 val64;
	size_t register_offset;
	loff_t copy_offset;
	size_t copy_count;
	int ret;

	ret = vfio_pci_core_read(core_vdev, buf, count, ppos);
	if (ret < 0)
		return ret;

	if (vfio_pci_core_range_intersect_range(pos, count, PCI_BASE_ADDRESS_2,
						sizeof(val64),
						&copy_offset, &copy_count,
						&register_offset))
		memregion = nvgrace_gpu_memregion(RESMEM_REGION_INDEX, nvdev);
	else if (vfio_pci_core_range_intersect_range(pos, count,
						     PCI_BASE_ADDRESS_4,
						     sizeof(val64),
						     &copy_offset, &copy_count,
						     &register_offset))
		memregion = nvgrace_gpu_memregion(USEMEM_REGION_INDEX, nvdev);

	if (memregion) {
		val64 = nvgrace_gpu_get_read_value(memregion->bar_size,
						   PCI_BASE_ADDRESS_MEM_TYPE_64 |
						   PCI_BASE_ADDRESS_MEM_PREFETCH,
						   memregion->bar_val);
		if (copy_to_user(buf + copy_offset,
				 (void *)&val64 + register_offset, copy_count)) {
			/*
			 * The position has been incremented in
			 * vfio_pci_core_read. Reset the offset back to the
			 * starting position.
			 */
			*ppos -= count;
			return -EFAULT;
		}
	}

	return count;
}

static ssize_t
nvgrace_gpu_write_config_emu(struct vfio_device *core_vdev,
			     const char __user *buf, size_t count, loff_t *ppos)
{
	struct nvgrace_gpu_pci_core_device *nvdev =
		container_of(core_vdev, struct nvgrace_gpu_pci_core_device,
			     core_device.vdev);
	u64 pos = *ppos & VFIO_PCI_OFFSET_MASK;
	struct mem_region *memregion = NULL;
	size_t register_offset;
	loff_t copy_offset;
	size_t copy_count;

	if (vfio_pci_core_range_intersect_range(pos, count, PCI_BASE_ADDRESS_2,
						sizeof(u64), &copy_offset,
						&copy_count, &register_offset))
		memregion = nvgrace_gpu_memregion(RESMEM_REGION_INDEX, nvdev);
	else if (vfio_pci_core_range_intersect_range(pos, count, PCI_BASE_ADDRESS_4,
						     sizeof(u64), &copy_offset,
						     &copy_count, &register_offset))
		memregion = nvgrace_gpu_memregion(USEMEM_REGION_INDEX, nvdev);

	if (memregion) {
		if (copy_from_user((void *)&memregion->bar_val + register_offset,
				   buf + copy_offset, copy_count))
			return -EFAULT;
		*ppos += copy_count;
		return copy_count;
	}

	return vfio_pci_core_write(core_vdev, buf, count, ppos);
}

/*
 * Ad hoc map the device memory in the module kernel VA space. Primarily needed
 * as vfio does not require the userspace driver to only perform accesses through
 * mmaps of the vfio-pci BAR regions and such accesses should be supported using
 * vfio_device_ops read/write implementations.
 *
 * The usemem region is cacheable memory and hence is memremaped.
 * The resmem region is non-cached and is mapped using ioremap_wc (NORMAL_NC).
 */
static int
nvgrace_gpu_map_device_mem(int index,
			   struct nvgrace_gpu_pci_core_device *nvdev)
{
	struct mem_region *memregion;
	int ret = 0;

	memregion = nvgrace_gpu_memregion(index, nvdev);
	if (!memregion)
		return -EINVAL;

	mutex_lock(&nvdev->remap_lock);

	if (memregion->memaddr)
		goto unlock;

	if (index == USEMEM_REGION_INDEX)
		memregion->memaddr = memremap(memregion->memphys,
					      memregion->memlength,
					      MEMREMAP_WB);
	else
		memregion->ioaddr = ioremap_wc(memregion->memphys,
					       memregion->memlength);

	if (!memregion->memaddr)
		ret = -ENOMEM;

unlock:
	mutex_unlock(&nvdev->remap_lock);

	return ret;
}

/*
 * Read the data from the device memory (mapped either through ioremap
 * or memremap) into the user buffer.
 */
static int
nvgrace_gpu_map_and_read(struct nvgrace_gpu_pci_core_device *nvdev,
			 char __user *buf, size_t mem_count, loff_t *ppos)
{
	unsigned int index = VFIO_PCI_OFFSET_TO_INDEX(*ppos);
	u64 offset = *ppos & VFIO_PCI_OFFSET_MASK;
	int ret;

	if (!mem_count)
		return 0;

	/*
	 * Handle read on the BAR regions. Map to the target device memory
	 * physical address and copy to the request read buffer.
	 */
	ret = nvgrace_gpu_map_device_mem(index, nvdev);
	if (ret)
		return ret;

	if (index == USEMEM_REGION_INDEX) {
		if (copy_to_user(buf,
				 (u8 *)nvdev->usemem.memaddr + offset,
				 mem_count))
			ret = -EFAULT;
	} else {
		/*
		 * The hardware ensures that the system does not crash when
		 * the device memory is accessed with the memory enable
		 * turned off. It synthesizes ~0 on such read. So there is
		 * no need to check or support the disablement/enablement of
		 * BAR through PCI_COMMAND config space register. Pass
		 * test_mem flag as false.
		 */
		ret = vfio_pci_core_do_io_rw(&nvdev->core_device, false,
					     nvdev->resmem.ioaddr,
					     buf, offset, mem_count,
					     0, 0, false);
	}

	return ret;
}

/*
 * Read count bytes from the device memory at an offset. The actual device
 * memory size (available) may not be a power-of-2. So the driver fakes
 * the size to a power-of-2 (reported) when exposing to a user space driver.
 *
 * Reads starting beyond the reported size generate -EINVAL; reads extending
 * beyond the actual device size is filled with ~0; reads extending beyond
 * the reported size are truncated.
 */
static ssize_t
nvgrace_gpu_read_mem(struct nvgrace_gpu_pci_core_device *nvdev,
		     char __user *buf, size_t count, loff_t *ppos)
{
	u64 offset = *ppos & VFIO_PCI_OFFSET_MASK;
	unsigned int index = VFIO_PCI_OFFSET_TO_INDEX(*ppos);
	struct mem_region *memregion;
	size_t mem_count, i;
	u8 val = 0xFF;
	int ret;

	/* No need to do NULL check as caller does. */
	memregion = nvgrace_gpu_memregion(index, nvdev);

	if (offset >= memregion->bar_size)
		return -EINVAL;

	/* Clip short the read request beyond reported BAR size */
	count = min(count, memregion->bar_size - (size_t)offset);

	/*
	 * Determine how many bytes to be actually read from the device memory.
	 * Read request beyond the actual device memory size is filled with ~0,
	 * while those beyond the actual reported size is skipped.
	 */
	if (offset >= memregion->memlength)
		mem_count = 0;
	else
		mem_count = min(count, memregion->memlength - (size_t)offset);

	ret = nvgrace_gpu_map_and_read(nvdev, buf, mem_count, ppos);
	if (ret)
		return ret;

	/*
	 * Only the device memory present on the hardware is mapped, which may
	 * not be power-of-2 aligned. A read to an offset beyond the device memory
	 * size is filled with ~0.
	 */
	for (i = mem_count; i < count; i++) {
		ret = put_user(val, (unsigned char __user *)(buf + i));
		if (ret)
			return ret;
	}

	*ppos += count;
	return count;
}

static ssize_t
nvgrace_gpu_read(struct vfio_device *core_vdev,
		 char __user *buf, size_t count, loff_t *ppos)
{
	unsigned int index = VFIO_PCI_OFFSET_TO_INDEX(*ppos);
	struct nvgrace_gpu_pci_core_device *nvdev =
		container_of(core_vdev, struct nvgrace_gpu_pci_core_device,
			     core_device.vdev);

	if (nvgrace_gpu_memregion(index, nvdev))
		return nvgrace_gpu_read_mem(nvdev, buf, count, ppos);

	if (index == VFIO_PCI_CONFIG_REGION_INDEX)
		return nvgrace_gpu_read_config_emu(core_vdev, buf, count, ppos);

	return vfio_pci_core_read(core_vdev, buf, count, ppos);
}

/*
 * Write the data to the device memory (mapped either through ioremap
 * or memremap) from the user buffer.
 */
static int
nvgrace_gpu_map_and_write(struct nvgrace_gpu_pci_core_device *nvdev,
			  const char __user *buf, size_t mem_count,
			  loff_t *ppos)
{
	unsigned int index = VFIO_PCI_OFFSET_TO_INDEX(*ppos);
	loff_t pos = *ppos & VFIO_PCI_OFFSET_MASK;
	int ret;

	if (!mem_count)
		return 0;

	ret = nvgrace_gpu_map_device_mem(index, nvdev);
	if (ret)
		return ret;

	if (index == USEMEM_REGION_INDEX) {
		if (copy_from_user((u8 *)nvdev->usemem.memaddr + pos,
				   buf, mem_count))
			return -EFAULT;
	} else {
		/*
		 * The hardware ensures that the system does not crash when
		 * the device memory is accessed with the memory enable
		 * turned off. It drops such writes. So there is no need to
		 * check or support the disablement/enablement of BAR
		 * through PCI_COMMAND config space register. Pass test_mem
		 * flag as false.
		 */
		ret = vfio_pci_core_do_io_rw(&nvdev->core_device, false,
					     nvdev->resmem.ioaddr,
					     (char __user *)buf, pos, mem_count,
					     0, 0, true);
	}

	return ret;
}

/*
 * Write count bytes to the device memory at a given offset. The actual device
 * memory size (available) may not be a power-of-2. So the driver fakes the
 * size to a power-of-2 (reported) when exposing to a user space driver.
 *
 * Writes extending beyond the reported size are truncated; writes starting
 * beyond the reported size generate -EINVAL.
 */
static ssize_t
nvgrace_gpu_write_mem(struct nvgrace_gpu_pci_core_device *nvdev,
		      size_t count, loff_t *ppos, const char __user *buf)
{
	unsigned int index = VFIO_PCI_OFFSET_TO_INDEX(*ppos);
	u64 offset = *ppos & VFIO_PCI_OFFSET_MASK;
	struct mem_region *memregion;
	size_t mem_count;
	int ret = 0;

	/* No need to do NULL check as caller does. */
	memregion = nvgrace_gpu_memregion(index, nvdev);

	if (offset >= memregion->bar_size)
		return -EINVAL;

	/* Clip short the write request beyond reported BAR size */
	count = min(count, memregion->bar_size - (size_t)offset);

	/*
	 * Determine how many bytes to be actually written to the device memory.
	 * Do not write to the offset beyond available size.
	 */
	if (offset >= memregion->memlength)
		goto exitfn;

	/*
	 * Only the device memory present on the hardware is mapped, which may
	 * not be power-of-2 aligned. Drop access outside the available device
	 * memory on the hardware.
	 */
	mem_count = min(count, memregion->memlength - (size_t)offset);

	ret = nvgrace_gpu_map_and_write(nvdev, buf, mem_count, ppos);
	if (ret)
		return ret;

exitfn:
	*ppos += count;
	return count;
}

static ssize_t
nvgrace_gpu_write(struct vfio_device *core_vdev,
		  const char __user *buf, size_t count, loff_t *ppos)
{
	struct nvgrace_gpu_pci_core_device *nvdev =
		container_of(core_vdev, struct nvgrace_gpu_pci_core_device,
			     core_device.vdev);
	unsigned int index = VFIO_PCI_OFFSET_TO_INDEX(*ppos);

	if (nvgrace_gpu_memregion(index, nvdev))
		return nvgrace_gpu_write_mem(nvdev, count, ppos, buf);

	if (index == VFIO_PCI_CONFIG_REGION_INDEX)
		return nvgrace_gpu_write_config_emu(core_vdev, buf, count, ppos);

	return vfio_pci_core_write(core_vdev, buf, count, ppos);
}

static const struct vfio_device_ops nvgrace_gpu_pci_ops = {
	.name		= "nvgrace-gpu-vfio-pci",
	.init		= vfio_pci_core_init_dev,
	.release	= vfio_pci_core_release_dev,
	.open_device	= nvgrace_gpu_open_device,
	.close_device	= nvgrace_gpu_close_device,
	.ioctl		= nvgrace_gpu_ioctl,
	.device_feature	= vfio_pci_core_ioctl_feature,
	.read		= nvgrace_gpu_read,
	.write		= nvgrace_gpu_write,
	.mmap		= nvgrace_gpu_mmap,
	.request	= vfio_pci_core_request,
	.match		= vfio_pci_core_match,
	.match_token_uuid = vfio_pci_core_match_token_uuid,
	.bind_iommufd	= vfio_iommufd_physical_bind,
	.unbind_iommufd	= vfio_iommufd_physical_unbind,
	.attach_ioas	= vfio_iommufd_physical_attach_ioas,
	.detach_ioas	= vfio_iommufd_physical_detach_ioas,
};

static const struct vfio_device_ops nvgrace_gpu_pci_core_ops = {
	.name		= "nvgrace-gpu-vfio-pci-core",
	.init		= vfio_pci_core_init_dev,
	.release	= vfio_pci_core_release_dev,
	.open_device	= nvgrace_gpu_open_device,
	.close_device	= vfio_pci_core_close_device,
	.ioctl		= vfio_pci_core_ioctl,
	.device_feature	= vfio_pci_core_ioctl_feature,
	.read		= vfio_pci_core_read,
	.write		= vfio_pci_core_write,
	.mmap		= vfio_pci_core_mmap,
	.request	= vfio_pci_core_request,
	.match		= vfio_pci_core_match,
	.match_token_uuid = vfio_pci_core_match_token_uuid,
	.bind_iommufd	= vfio_iommufd_physical_bind,
	.unbind_iommufd	= vfio_iommufd_physical_unbind,
	.attach_ioas	= vfio_iommufd_physical_attach_ioas,
	.detach_ioas	= vfio_iommufd_physical_detach_ioas,
};

static int
nvgrace_gpu_fetch_memory_property(struct pci_dev *pdev,
				  u64 *pmemphys, u64 *pmemlength)
{
	int ret;

	/*
	 * The memory information is present in the system ACPI tables as DSD
	 * properties nvidia,gpu-mem-base-pa and nvidia,gpu-mem-size.
	 */
	ret = device_property_read_u64(&pdev->dev, "nvidia,gpu-mem-base-pa",
				       pmemphys);
	if (ret)
		return ret;

	if (*pmemphys > type_max(phys_addr_t))
		return -EOVERFLOW;

	ret = device_property_read_u64(&pdev->dev, "nvidia,gpu-mem-size",
				       pmemlength);
	if (ret)
		return ret;

	if (*pmemlength > type_max(size_t))
		return -EOVERFLOW;

	/*
	 * If the C2C link is not up due to an error, the coherent device
	 * memory size is returned as 0. Fail in such case.
	 */
	if (*pmemlength == 0)
		return -ENOMEM;

	return ret;
}

static int
nvgrace_gpu_init_nvdev_struct(struct pci_dev *pdev,
			      struct nvgrace_gpu_pci_core_device *nvdev,
			      u64 memphys, u64 memlength)
{
	int ret = 0;
	u64 resmem_size = 0;

	/*
	 * On Grace Hopper systems, the VM GPU device driver needs a non-cacheable
	 * region to support the MIG feature owing to a hardware bug. Since the
	 * device memory is mapped as NORMAL cached, carve out a region from the end
	 * with a different NORMAL_NC property (called as reserved memory and
	 * represented as resmem). This region then is exposed as a 64b BAR
	 * (region 2 and 3) to the VM, while exposing the rest (termed as usable
	 * memory and represented using usemem) as cacheable 64b BAR (region 4 and 5).
	 *
	 *               devmem (memlength)
	 * |-------------------------------------------------|
	 * |                                           |
	 * usemem.memphys                              resmem.memphys
	 *
	 * This hardware bug is fixed on the Grace Blackwell platforms and the
	 * presence of the bug can be determined through nvdev->has_mig_hw_bug.
	 * Thus on systems with the hardware fix, there is no need to partition
	 * the GPU device memory and the entire memory is usable and mapped as
	 * NORMAL cached (i.e. resmem size is 0).
	 */
	if (nvdev->has_mig_hw_bug)
		resmem_size = SZ_1G;

	nvdev->usemem.memphys = memphys;

	/*
	 * The device memory exposed to the VM is added to the kernel by the
	 * VM driver module in chunks of memory block size. Note that only the
	 * usable memory (usemem) is added to the kernel for usage by the VM
	 * workloads.
	 */
	if (check_sub_overflow(memlength, resmem_size,
			       &nvdev->usemem.memlength)) {
		ret = -EOVERFLOW;
		goto done;
	}

	/*
	 * The usemem region is exposed as a 64B Bar composed of region 4 and 5.
	 * Calculate and save the BAR size for the region.
	 */
	nvdev->usemem.bar_size = roundup_pow_of_two(nvdev->usemem.memlength);

	/*
	 * If the hardware has the fix for MIG, there is no requirement
	 * for splitting the device memory to create RESMEM. The entire
	 * device memory is usable and will be USEMEM. Return here for
	 * such case.
	 */
	if (!nvdev->has_mig_hw_bug)
		goto done;

	/*
	 * When the device memory is split to workaround the MIG bug on
	 * Grace Hopper, the USEMEM part of the device memory has to be
	 * MEMBLK_SIZE aligned. This is a hardwired ABI value between the
	 * GPU FW and VFIO driver. The VM device driver is also aware of it
	 * and make use of the value for its calculation to determine USEMEM
	 * size. Note that the device memory may not be 512M aligned.
	 */
	nvdev->usemem.memlength = round_down(nvdev->usemem.memlength,
					     MEMBLK_SIZE);
	if (nvdev->usemem.memlength == 0) {
		ret = -EINVAL;
		goto done;
	}

	if ((check_add_overflow(nvdev->usemem.memphys,
				nvdev->usemem.memlength,
				&nvdev->resmem.memphys)) ||
	    (check_sub_overflow(memlength, nvdev->usemem.memlength,
				&nvdev->resmem.memlength))) {
		ret = -EOVERFLOW;
		goto done;
	}

	/*
	 * The resmem region is exposed as a 64b BAR composed of region 2 and 3
	 * for Grace Hopper. Calculate and save the BAR size for the region.
	 */
	nvdev->resmem.bar_size = roundup_pow_of_two(nvdev->resmem.memlength);
done:
	return ret;
}

static bool nvgrace_gpu_has_mig_hw_bug(struct pci_dev *pdev)
{
	int pcie_dvsec;
	u16 dvsec_ctrl16;

	pcie_dvsec = pci_find_dvsec_capability(pdev, PCI_VENDOR_ID_NVIDIA,
					       GPU_CAP_DVSEC_REGISTER);

	if (pcie_dvsec) {
		pci_read_config_word(pdev,
				     pcie_dvsec + DVSEC_BITMAP_OFFSET,
				     &dvsec_ctrl16);

		if (dvsec_ctrl16 & MIG_SUPPORTED_WITH_CACHED_RESMEM)
			return false;
	}

	return true;
}

/*
 * To reduce the system bootup time, the HBM training has
 * been moved out of the UEFI on the Grace-Blackwell systems.
 *
 * The onus of checking whether the HBM training has completed
 * thus falls on the module. The HBM training status can be
 * determined from a BAR0 register.
 *
 * Similarly, another BAR0 register exposes the status of the
 * CPU-GPU chip-to-chip (C2C) cache coherent interconnect.
 *
 * Poll these register and check for 30s. If the HBM training is
 * not complete or if the C2C link is not ready, fail the probe.
 *
 * While the wait is not required on Grace Hopper systems, it
 * is beneficial to make the check to ensure the device is in an
 * expected state.
 *
 * Ensure that the BAR0 region is enabled before accessing the
 * registers.
 */
static int nvgrace_gpu_wait_device_ready(struct pci_dev *pdev)
{
	unsigned long timeout = jiffies + msecs_to_jiffies(POLL_TIMEOUT_MS);
	void __iomem *io;
	int ret = -ETIME;

	ret = pci_enable_device(pdev);
	if (ret)
		return ret;

	ret = pci_request_selected_regions(pdev, 1 << 0, KBUILD_MODNAME);
	if (ret)
		goto request_region_exit;

	io = pci_iomap(pdev, 0, 0);
	if (!io) {
		ret = -ENOMEM;
		goto iomap_exit;
	}

	do {
		if ((ioread32(io + C2C_LINK_BAR0_OFFSET) == STATUS_READY) &&
		    (ioread32(io + HBM_TRAINING_BAR0_OFFSET) == STATUS_READY)) {
			ret = 0;
			goto reg_check_exit;
		}
		msleep(POLL_QUANTUM_MS);
	} while (!time_after(jiffies, timeout));

reg_check_exit:
	pci_iounmap(pdev, io);
iomap_exit:
	pci_release_selected_regions(pdev, 1 << 0);
request_region_exit:
	pci_disable_device(pdev);
	return ret;
}

static int nvgrace_gpu_probe(struct pci_dev *pdev,
			     const struct pci_device_id *id)
{
	const struct vfio_device_ops *ops = &nvgrace_gpu_pci_core_ops;
	struct nvgrace_gpu_pci_core_device *nvdev;
	u64 memphys, memlength;
	int ret;

	ret = nvgrace_gpu_wait_device_ready(pdev);
	if (ret)
		return ret;

	ret = nvgrace_gpu_fetch_memory_property(pdev, &memphys, &memlength);
	if (!ret)
		ops = &nvgrace_gpu_pci_ops;

	nvdev = vfio_alloc_device(nvgrace_gpu_pci_core_device, core_device.vdev,
				  &pdev->dev, ops);
	if (IS_ERR(nvdev))
		return PTR_ERR(nvdev);

	dev_set_drvdata(&pdev->dev, &nvdev->core_device);

	if (ops == &nvgrace_gpu_pci_ops) {
		nvdev->has_mig_hw_bug = nvgrace_gpu_has_mig_hw_bug(pdev);

		/*
		 * Device memory properties are identified in the host ACPI
		 * table. Set the nvgrace_gpu_pci_core_device structure.
		 */
		ret = nvgrace_gpu_init_nvdev_struct(pdev, nvdev,
						    memphys, memlength);
		if (ret)
			goto out_put_vdev;
	}

	ret = vfio_pci_core_register_device(&nvdev->core_device);
	if (ret)
		goto out_put_vdev;

	return ret;

out_put_vdev:
	vfio_put_device(&nvdev->core_device.vdev);
	return ret;
}

static void nvgrace_gpu_remove(struct pci_dev *pdev)
{
	struct vfio_pci_core_device *core_device = dev_get_drvdata(&pdev->dev);

	vfio_pci_core_unregister_device(core_device);
	vfio_put_device(&core_device->vdev);
}

static const struct pci_device_id nvgrace_gpu_vfio_pci_table[] = {
	/* GH200 120GB */
	{ PCI_DRIVER_OVERRIDE_DEVICE_VFIO(PCI_VENDOR_ID_NVIDIA, 0x2342) },
	/* GH200 480GB */
	{ PCI_DRIVER_OVERRIDE_DEVICE_VFIO(PCI_VENDOR_ID_NVIDIA, 0x2345) },
	/* GH200 SKU */
	{ PCI_DRIVER_OVERRIDE_DEVICE_VFIO(PCI_VENDOR_ID_NVIDIA, 0x2348) },
	/* GB200 SKU */
	{ PCI_DRIVER_OVERRIDE_DEVICE_VFIO(PCI_VENDOR_ID_NVIDIA, 0x2941) },
	/* GB300 SKU */
	{ PCI_DRIVER_OVERRIDE_DEVICE_VFIO(PCI_VENDOR_ID_NVIDIA, 0x31C2) },
	{}
};

MODULE_DEVICE_TABLE(pci, nvgrace_gpu_vfio_pci_table);

static struct pci_driver nvgrace_gpu_vfio_pci_driver = {
	.name = KBUILD_MODNAME,
	.id_table = nvgrace_gpu_vfio_pci_table,
	.probe = nvgrace_gpu_probe,
	.remove = nvgrace_gpu_remove,
	.err_handler = &vfio_pci_core_err_handlers,
	.driver_managed_dma = true,
};

module_pci_driver(nvgrace_gpu_vfio_pci_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Ankit Agrawal <ankita@nvidia.com>");
MODULE_AUTHOR("Aniket Agashe <aniketa@nvidia.com>");
MODULE_DESCRIPTION("VFIO NVGRACE GPU PF - User Level driver for NVIDIA devices with CPU coherently accessible device memory");

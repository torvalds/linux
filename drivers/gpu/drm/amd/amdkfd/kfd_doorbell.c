/*
 * Copyright 2014 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */
#include "kfd_priv.h"
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/idr.h>

/*
 * This extension supports a kernel level doorbells management for the
 * kernel queues using the first doorbell page reserved for the kernel.
 */

static DEFINE_IDA(doorbell_ida);
static unsigned int max_doorbell_slices;

/*
 * Each device exposes a doorbell aperture, a PCI MMIO aperture that
 * receives 32-bit writes that are passed to queues as wptr values.
 * The doorbells are intended to be written by applications as part
 * of queueing work on user-mode queues.
 * We assign doorbells to applications in PAGE_SIZE-sized and aligned chunks.
 * We map the doorbell address space into user-mode when a process creates
 * its first queue on each device.
 * Although the mapping is done by KFD, it is equivalent to an mmap of
 * the /dev/kfd with the particular device encoded in the mmap offset.
 * There will be other uses for mmap of /dev/kfd, so only a range of
 * offsets (KFD_MMAP_DOORBELL_START-END) is used for doorbells.
 */

/* # of doorbell bytes allocated for each process. */
size_t kfd_doorbell_process_slice(struct kfd_dev *kfd)
{
	return roundup(kfd->device_info->doorbell_size *
			KFD_MAX_NUM_OF_QUEUES_PER_PROCESS,
			PAGE_SIZE);
}

/* Doorbell calculations for device init. */
int kfd_doorbell_init(struct kfd_dev *kfd)
{
	size_t doorbell_start_offset;
	size_t doorbell_aperture_size;
	size_t doorbell_process_limit;

	/*
	 * We start with calculations in bytes because the input data might
	 * only be byte-aligned.
	 * Only after we have done the rounding can we assume any alignment.
	 */

	doorbell_start_offset =
			roundup(kfd->shared_resources.doorbell_start_offset,
					kfd_doorbell_process_slice(kfd));

	doorbell_aperture_size =
			rounddown(kfd->shared_resources.doorbell_aperture_size,
					kfd_doorbell_process_slice(kfd));

	if (doorbell_aperture_size > doorbell_start_offset)
		doorbell_process_limit =
			(doorbell_aperture_size - doorbell_start_offset) /
						kfd_doorbell_process_slice(kfd);
	else
		return -ENOSPC;

	if (!max_doorbell_slices ||
	    doorbell_process_limit < max_doorbell_slices)
		max_doorbell_slices = doorbell_process_limit;

	kfd->doorbell_base = kfd->shared_resources.doorbell_physical_address +
				doorbell_start_offset;

	kfd->doorbell_id_offset = doorbell_start_offset / sizeof(u32);

	kfd->doorbell_kernel_ptr = ioremap(kfd->doorbell_base,
					   kfd_doorbell_process_slice(kfd));

	if (!kfd->doorbell_kernel_ptr)
		return -ENOMEM;

	pr_debug("Doorbell initialization:\n");
	pr_debug("doorbell base           == 0x%08lX\n",
			(uintptr_t)kfd->doorbell_base);

	pr_debug("doorbell_id_offset      == 0x%08lX\n",
			kfd->doorbell_id_offset);

	pr_debug("doorbell_process_limit  == 0x%08lX\n",
			doorbell_process_limit);

	pr_debug("doorbell_kernel_offset  == 0x%08lX\n",
			(uintptr_t)kfd->doorbell_base);

	pr_debug("doorbell aperture size  == 0x%08lX\n",
			kfd->shared_resources.doorbell_aperture_size);

	pr_debug("doorbell kernel address == %p\n", kfd->doorbell_kernel_ptr);

	return 0;
}

void kfd_doorbell_fini(struct kfd_dev *kfd)
{
	if (kfd->doorbell_kernel_ptr)
		iounmap(kfd->doorbell_kernel_ptr);
}

int kfd_doorbell_mmap(struct kfd_dev *dev, struct kfd_process *process,
		      struct vm_area_struct *vma)
{
	phys_addr_t address;

	/*
	 * For simplicitly we only allow mapping of the entire doorbell
	 * allocation of a single device & process.
	 */
	if (vma->vm_end - vma->vm_start != kfd_doorbell_process_slice(dev))
		return -EINVAL;

	/* Calculate physical address of doorbell */
	address = kfd_get_process_doorbells(dev, process);

	vma->vm_flags |= VM_IO | VM_DONTCOPY | VM_DONTEXPAND | VM_NORESERVE |
				VM_DONTDUMP | VM_PFNMAP;

	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);

	pr_debug("Mapping doorbell page\n"
		 "     target user address == 0x%08llX\n"
		 "     physical address    == 0x%08llX\n"
		 "     vm_flags            == 0x%04lX\n"
		 "     size                == 0x%04lX\n",
		 (unsigned long long) vma->vm_start, address, vma->vm_flags,
		 kfd_doorbell_process_slice(dev));


	return io_remap_pfn_range(vma,
				vma->vm_start,
				address >> PAGE_SHIFT,
				kfd_doorbell_process_slice(dev),
				vma->vm_page_prot);
}


/* get kernel iomem pointer for a doorbell */
void __iomem *kfd_get_kernel_doorbell(struct kfd_dev *kfd,
					unsigned int *doorbell_off)
{
	u32 inx;

	mutex_lock(&kfd->doorbell_mutex);
	inx = find_first_zero_bit(kfd->doorbell_available_index,
					KFD_MAX_NUM_OF_QUEUES_PER_PROCESS);

	__set_bit(inx, kfd->doorbell_available_index);
	mutex_unlock(&kfd->doorbell_mutex);

	if (inx >= KFD_MAX_NUM_OF_QUEUES_PER_PROCESS)
		return NULL;

	inx *= kfd->device_info->doorbell_size / sizeof(u32);

	/*
	 * Calculating the kernel doorbell offset using the first
	 * doorbell page.
	 */
	*doorbell_off = kfd->doorbell_id_offset + inx;

	pr_debug("Get kernel queue doorbell\n"
			"     doorbell offset   == 0x%08X\n"
			"     doorbell index    == 0x%x\n",
		*doorbell_off, inx);

	return kfd->doorbell_kernel_ptr + inx;
}

void kfd_release_kernel_doorbell(struct kfd_dev *kfd, u32 __iomem *db_addr)
{
	unsigned int inx;

	inx = (unsigned int)(db_addr - kfd->doorbell_kernel_ptr)
		* sizeof(u32) / kfd->device_info->doorbell_size;

	mutex_lock(&kfd->doorbell_mutex);
	__clear_bit(inx, kfd->doorbell_available_index);
	mutex_unlock(&kfd->doorbell_mutex);
}

void write_kernel_doorbell(void __iomem *db, u32 value)
{
	if (db) {
		writel(value, db);
		pr_debug("Writing %d to doorbell address %p\n", value, db);
	}
}

void write_kernel_doorbell64(void __iomem *db, u64 value)
{
	if (db) {
		WARN(((unsigned long)db & 7) != 0,
		     "Unaligned 64-bit doorbell");
		writeq(value, (u64 __iomem *)db);
		pr_debug("writing %llu to doorbell address %p\n", value, db);
	}
}

unsigned int kfd_doorbell_id_to_offset(struct kfd_dev *kfd,
					struct kfd_process *process,
					unsigned int doorbell_id)
{
	/*
	 * doorbell_id_offset accounts for doorbells taken by KGD.
	 * index * kfd_doorbell_process_slice/sizeof(u32) adjusts to
	 * the process's doorbells. The offset returned is in dword
	 * units regardless of the ASIC-dependent doorbell size.
	 */
	return kfd->doorbell_id_offset +
		process->doorbell_index
		* kfd_doorbell_process_slice(kfd) / sizeof(u32) +
		doorbell_id * kfd->device_info->doorbell_size / sizeof(u32);
}

uint64_t kfd_get_number_elems(struct kfd_dev *kfd)
{
	uint64_t num_of_elems = (kfd->shared_resources.doorbell_aperture_size -
				kfd->shared_resources.doorbell_start_offset) /
					kfd_doorbell_process_slice(kfd) + 1;

	return num_of_elems;

}

phys_addr_t kfd_get_process_doorbells(struct kfd_dev *dev,
					struct kfd_process *process)
{
	return dev->doorbell_base +
		process->doorbell_index * kfd_doorbell_process_slice(dev);
}

int kfd_alloc_process_doorbells(struct kfd_process *process)
{
	int r = ida_simple_get(&doorbell_ida, 1, max_doorbell_slices,
				GFP_KERNEL);
	if (r > 0)
		process->doorbell_index = r;

	return r;
}

void kfd_free_process_doorbells(struct kfd_process *process)
{
	if (process->doorbell_index)
		ida_simple_remove(&doorbell_ida, process->doorbell_index);
}

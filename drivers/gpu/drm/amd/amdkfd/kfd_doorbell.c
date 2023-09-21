// SPDX-License-Identifier: GPL-2.0 OR MIT
/*
 * Copyright 2014-2022 Advanced Micro Devices, Inc.
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
	if (!kfd->shared_resources.enable_mes)
		return roundup(kfd->device_info.doorbell_size *
				KFD_MAX_NUM_OF_QUEUES_PER_PROCESS,
				PAGE_SIZE);
	else
		return amdgpu_mes_doorbell_process_slice(
					(struct amdgpu_device *)kfd->adev);
}

/* Doorbell calculations for device init. */
int kfd_doorbell_init(struct kfd_dev *kfd)
{
	int size = PAGE_SIZE;
	int r;

	/*
	 * Todo: KFD kernel level operations need only one doorbell for
	 * ring test/HWS. So instead of reserving a whole page here for
	 * kernel, reserve and consume a doorbell from existing KGD kernel
	 * doorbell page.
	 */

	/* Bitmap to dynamically allocate doorbells from kernel page */
	kfd->doorbell_bitmap = bitmap_zalloc(size / sizeof(u32), GFP_KERNEL);
	if (!kfd->doorbell_bitmap) {
		DRM_ERROR("Failed to allocate kernel doorbell bitmap\n");
		return -ENOMEM;
	}

	/* Alloc a doorbell page for KFD kernel usages */
	r = amdgpu_bo_create_kernel(kfd->adev,
				    size,
				    PAGE_SIZE,
				    AMDGPU_GEM_DOMAIN_DOORBELL,
				    &kfd->doorbells,
				    NULL,
				    (void **)&kfd->doorbell_kernel_ptr);
	if (r) {
		pr_err("failed to allocate kernel doorbells\n");
		bitmap_free(kfd->doorbell_bitmap);
		return r;
	}

	pr_debug("Doorbell kernel address == %p\n", kfd->doorbell_kernel_ptr);
	return 0;
}

void kfd_doorbell_fini(struct kfd_dev *kfd)
{
	bitmap_free(kfd->doorbell_bitmap);
	amdgpu_bo_free_kernel(&kfd->doorbells, NULL,
			     (void **)&kfd->doorbell_kernel_ptr);
}

int kfd_doorbell_mmap(struct kfd_node *dev, struct kfd_process *process,
		      struct vm_area_struct *vma)
{
	phys_addr_t address;
	struct kfd_process_device *pdd;

	/*
	 * For simplicitly we only allow mapping of the entire doorbell
	 * allocation of a single device & process.
	 */
	if (vma->vm_end - vma->vm_start != kfd_doorbell_process_slice(dev->kfd))
		return -EINVAL;

	pdd = kfd_get_process_device_data(dev, process);
	if (!pdd)
		return -EINVAL;

	/* Calculate physical address of doorbell */
	address = kfd_get_process_doorbells(pdd);
	if (!address)
		return -ENOMEM;
	vm_flags_set(vma, VM_IO | VM_DONTCOPY | VM_DONTEXPAND | VM_NORESERVE |
				VM_DONTDUMP | VM_PFNMAP);

	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);

	pr_debug("Mapping doorbell page\n"
		 "     target user address == 0x%08llX\n"
		 "     physical address    == 0x%08llX\n"
		 "     vm_flags            == 0x%04lX\n"
		 "     size                == 0x%04lX\n",
		 (unsigned long long) vma->vm_start, address, vma->vm_flags,
		 kfd_doorbell_process_slice(dev->kfd));


	return io_remap_pfn_range(vma,
				vma->vm_start,
				address >> PAGE_SHIFT,
				kfd_doorbell_process_slice(dev->kfd),
				vma->vm_page_prot);
}


/* get kernel iomem pointer for a doorbell */
void __iomem *kfd_get_kernel_doorbell(struct kfd_dev *kfd,
					unsigned int *doorbell_off)
{
	u32 inx;

	mutex_lock(&kfd->doorbell_mutex);
	inx = find_first_zero_bit(kfd->doorbell_bitmap, PAGE_SIZE / sizeof(u32));

	__set_bit(inx, kfd->doorbell_bitmap);
	mutex_unlock(&kfd->doorbell_mutex);

	if (inx >= KFD_MAX_NUM_OF_QUEUES_PER_PROCESS)
		return NULL;

	*doorbell_off = amdgpu_doorbell_index_on_bar(kfd->adev, kfd->doorbells, inx);
	inx *= 2;

	pr_debug("Get kernel queue doorbell\n"
			"     doorbell offset   == 0x%08X\n"
			"     doorbell index    == 0x%x\n",
		*doorbell_off, inx);

	return kfd->doorbell_kernel_ptr + inx;
}

void kfd_release_kernel_doorbell(struct kfd_dev *kfd, u32 __iomem *db_addr)
{
	unsigned int inx;

	inx = (unsigned int)(db_addr - kfd->doorbell_kernel_ptr);
	inx /= 2;

	mutex_lock(&kfd->doorbell_mutex);
	__clear_bit(inx, kfd->doorbell_bitmap);
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

static int init_doorbell_bitmap(struct qcm_process_device *qpd,
				struct kfd_dev *dev)
{
	unsigned int i;
	int range_start = dev->shared_resources.non_cp_doorbells_start;
	int range_end = dev->shared_resources.non_cp_doorbells_end;

	if (!KFD_IS_SOC15(dev))
		return 0;

	/* Mask out doorbells reserved for SDMA, IH, and VCN on SOC15. */
	pr_debug("reserved doorbell 0x%03x - 0x%03x\n", range_start, range_end);
	pr_debug("reserved doorbell 0x%03x - 0x%03x\n",
			range_start + KFD_QUEUE_DOORBELL_MIRROR_OFFSET,
			range_end + KFD_QUEUE_DOORBELL_MIRROR_OFFSET);

	for (i = 0; i < KFD_MAX_NUM_OF_QUEUES_PER_PROCESS / 2; i++) {
		if (i >= range_start && i <= range_end) {
			__set_bit(i, qpd->doorbell_bitmap);
			__set_bit(i + KFD_QUEUE_DOORBELL_MIRROR_OFFSET,
				  qpd->doorbell_bitmap);
		}
	}

	return 0;
}

phys_addr_t kfd_get_process_doorbells(struct kfd_process_device *pdd)
{
	struct amdgpu_device *adev = pdd->dev->adev;
	uint32_t first_db_index;

	if (!pdd->qpd.proc_doorbells) {
		if (kfd_alloc_process_doorbells(pdd->dev->kfd, pdd))
			/* phys_addr_t 0 is error */
			return 0;
	}

	first_db_index = amdgpu_doorbell_index_on_bar(adev, pdd->qpd.proc_doorbells, 0);
	return adev->doorbell.base + first_db_index * sizeof(uint32_t);
}

int kfd_alloc_process_doorbells(struct kfd_dev *kfd, struct kfd_process_device *pdd)
{
	int r;
	struct qcm_process_device *qpd = &pdd->qpd;

	/* Allocate bitmap for dynamic doorbell allocation */
	qpd->doorbell_bitmap = bitmap_zalloc(KFD_MAX_NUM_OF_QUEUES_PER_PROCESS,
					     GFP_KERNEL);
	if (!qpd->doorbell_bitmap) {
		DRM_ERROR("Failed to allocate process doorbell bitmap\n");
		return -ENOMEM;
	}

	r = init_doorbell_bitmap(&pdd->qpd, kfd);
	if (r) {
		DRM_ERROR("Failed to initialize process doorbells\n");
		r = -ENOMEM;
		goto err;
	}

	/* Allocate doorbells for this process */
	r = amdgpu_bo_create_kernel(kfd->adev,
				    kfd_doorbell_process_slice(kfd),
				    PAGE_SIZE,
				    AMDGPU_GEM_DOMAIN_DOORBELL,
				    &qpd->proc_doorbells,
				    NULL,
				    NULL);
	if (r) {
		DRM_ERROR("Failed to allocate process doorbells\n");
		goto err;
	}
	return 0;

err:
	bitmap_free(qpd->doorbell_bitmap);
	qpd->doorbell_bitmap = NULL;
	return r;
}

void kfd_free_process_doorbells(struct kfd_dev *kfd, struct kfd_process_device *pdd)
{
	struct qcm_process_device *qpd = &pdd->qpd;

	if (qpd->doorbell_bitmap) {
		bitmap_free(qpd->doorbell_bitmap);
		qpd->doorbell_bitmap = NULL;
	}

	amdgpu_bo_free_kernel(&qpd->proc_doorbells, NULL, NULL);
}

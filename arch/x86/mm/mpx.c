/*
 * mpx.c - Memory Protection eXtensions
 *
 * Copyright (c) 2014, Intel Corporation.
 * Qiaowei Ren <qiaowei.ren@intel.com>
 * Dave Hansen <dave.hansen@intel.com>
 */
#include <linux/kernel.h>
#include <linux/syscalls.h>
#include <linux/sched/sysctl.h>

#include <asm/mman.h>
#include <asm/mpx.h>

static const char *mpx_mapping_name(struct vm_area_struct *vma)
{
	return "[mpx]";
}

static struct vm_operations_struct mpx_vma_ops = {
	.name = mpx_mapping_name,
};

/*
 * This is really a simplified "vm_mmap". it only handles MPX
 * bounds tables (the bounds directory is user-allocated).
 *
 * Later on, we use the vma->vm_ops to uniquely identify these
 * VMAs.
 */
static unsigned long mpx_mmap(unsigned long len)
{
	unsigned long ret;
	unsigned long addr, pgoff;
	struct mm_struct *mm = current->mm;
	vm_flags_t vm_flags;
	struct vm_area_struct *vma;

	/* Only bounds table and bounds directory can be allocated here */
	if (len != MPX_BD_SIZE_BYTES && len != MPX_BT_SIZE_BYTES)
		return -EINVAL;

	down_write(&mm->mmap_sem);

	/* Too many mappings? */
	if (mm->map_count > sysctl_max_map_count) {
		ret = -ENOMEM;
		goto out;
	}

	/* Obtain the address to map to. we verify (or select) it and ensure
	 * that it represents a valid section of the address space.
	 */
	addr = get_unmapped_area(NULL, 0, len, 0, MAP_ANONYMOUS | MAP_PRIVATE);
	if (addr & ~PAGE_MASK) {
		ret = addr;
		goto out;
	}

	vm_flags = VM_READ | VM_WRITE | VM_MPX |
			mm->def_flags | VM_MAYREAD | VM_MAYWRITE | VM_MAYEXEC;

	/* Set pgoff according to addr for anon_vma */
	pgoff = addr >> PAGE_SHIFT;

	ret = mmap_region(NULL, addr, len, vm_flags, pgoff);
	if (IS_ERR_VALUE(ret))
		goto out;

	vma = find_vma(mm, ret);
	if (!vma) {
		ret = -ENOMEM;
		goto out;
	}
	vma->vm_ops = &mpx_vma_ops;

	if (vm_flags & VM_LOCKED) {
		up_write(&mm->mmap_sem);
		mm_populate(ret, len);
		return ret;
	}

out:
	up_write(&mm->mmap_sem);
	return ret;
}

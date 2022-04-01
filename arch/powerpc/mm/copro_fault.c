// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * CoProcessor (SPU/AFU) mm fault handler
 *
 * (C) Copyright IBM Deutschland Entwicklung GmbH 2007
 *
 * Author: Arnd Bergmann <arndb@de.ibm.com>
 * Author: Jeremy Kerr <jk@ozlabs.org>
 */
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/export.h>
#include <asm/reg.h>
#include <asm/copro.h>
#include <asm/spu.h>
#include <misc/cxl-base.h>

/*
 * This ought to be kept in sync with the powerpc specific do_page_fault
 * function. Currently, there are a few corner cases that we haven't had
 * to handle fortunately.
 */
int copro_handle_mm_fault(struct mm_struct *mm, unsigned long ea,
		unsigned long dsisr, vm_fault_t *flt)
{
	struct vm_area_struct *vma;
	unsigned long is_write;
	int ret;

	if (mm == NULL)
		return -EFAULT;

	if (mm->pgd == NULL)
		return -EFAULT;

	mmap_read_lock(mm);
	ret = -EFAULT;
	vma = find_vma(mm, ea);
	if (!vma)
		goto out_unlock;

	if (ea < vma->vm_start) {
		if (!(vma->vm_flags & VM_GROWSDOWN))
			goto out_unlock;
		if (expand_stack(vma, ea))
			goto out_unlock;
	}

	is_write = dsisr & DSISR_ISSTORE;
	if (is_write) {
		if (!(vma->vm_flags & VM_WRITE))
			goto out_unlock;
	} else {
		if (!(vma->vm_flags & (VM_READ | VM_EXEC)))
			goto out_unlock;
		/*
		 * PROT_NONE is covered by the VMA check above.
		 * and hash should get a NOHPTE fault instead of
		 * a PROTFAULT in case fixup is needed for things
		 * like autonuma.
		 */
		if (!radix_enabled())
			WARN_ON_ONCE(dsisr & DSISR_PROTFAULT);
	}

	ret = 0;
	*flt = handle_mm_fault(vma, ea, is_write ? FAULT_FLAG_WRITE : 0, NULL);
	if (unlikely(*flt & VM_FAULT_ERROR)) {
		if (*flt & VM_FAULT_OOM) {
			ret = -ENOMEM;
			goto out_unlock;
		} else if (*flt & (VM_FAULT_SIGBUS | VM_FAULT_SIGSEGV)) {
			ret = -EFAULT;
			goto out_unlock;
		}
		BUG();
	}

out_unlock:
	mmap_read_unlock(mm);
	return ret;
}
EXPORT_SYMBOL_GPL(copro_handle_mm_fault);

#ifdef CONFIG_PPC_64S_HASH_MMU
int copro_calculate_slb(struct mm_struct *mm, u64 ea, struct copro_slb *slb)
{
	u64 vsid, vsidkey;
	int psize, ssize;

	switch (get_region_id(ea)) {
	case USER_REGION_ID:
		pr_devel("%s: 0x%llx -- USER_REGION_ID\n", __func__, ea);
		if (mm == NULL)
			return 1;
		psize = get_slice_psize(mm, ea);
		ssize = user_segment_size(ea);
		vsid = get_user_vsid(&mm->context, ea, ssize);
		vsidkey = SLB_VSID_USER;
		break;
	case VMALLOC_REGION_ID:
		pr_devel("%s: 0x%llx -- VMALLOC_REGION_ID\n", __func__, ea);
		psize = mmu_vmalloc_psize;
		ssize = mmu_kernel_ssize;
		vsid = get_kernel_vsid(ea, mmu_kernel_ssize);
		vsidkey = SLB_VSID_KERNEL;
		break;
	case IO_REGION_ID:
		pr_devel("%s: 0x%llx -- IO_REGION_ID\n", __func__, ea);
		psize = mmu_io_psize;
		ssize = mmu_kernel_ssize;
		vsid = get_kernel_vsid(ea, mmu_kernel_ssize);
		vsidkey = SLB_VSID_KERNEL;
		break;
	case LINEAR_MAP_REGION_ID:
		pr_devel("%s: 0x%llx -- LINEAR_MAP_REGION_ID\n", __func__, ea);
		psize = mmu_linear_psize;
		ssize = mmu_kernel_ssize;
		vsid = get_kernel_vsid(ea, mmu_kernel_ssize);
		vsidkey = SLB_VSID_KERNEL;
		break;
	default:
		pr_debug("%s: invalid region access at %016llx\n", __func__, ea);
		return 1;
	}
	/* Bad address */
	if (!vsid)
		return 1;

	vsid = (vsid << slb_vsid_shift(ssize)) | vsidkey;

	vsid |= mmu_psize_defs[psize].sllp |
		((ssize == MMU_SEGSIZE_1T) ? SLB_VSID_B_1T : 0);

	slb->esid = (ea & (ssize == MMU_SEGSIZE_1T ? ESID_MASK_1T : ESID_MASK)) | SLB_ESID_V;
	slb->vsid = vsid;

	return 0;
}
EXPORT_SYMBOL_GPL(copro_calculate_slb);

void copro_flush_all_slbs(struct mm_struct *mm)
{
#ifdef CONFIG_SPU_BASE
	spu_flush_all_slbs(mm);
#endif
	cxl_slbia(mm);
}
EXPORT_SYMBOL_GPL(copro_flush_all_slbs);
#endif

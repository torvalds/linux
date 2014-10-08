/*
 * CoProcessor (SPU/AFU) mm fault handler
 *
 * (C) Copyright IBM Deutschland Entwicklung GmbH 2007
 *
 * Author: Arnd Bergmann <arndb@de.ibm.com>
 * Author: Jeremy Kerr <jk@ozlabs.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/export.h>
#include <asm/reg.h>
#include <asm/copro.h>
#include <asm/spu.h>

/*
 * This ought to be kept in sync with the powerpc specific do_page_fault
 * function. Currently, there are a few corner cases that we haven't had
 * to handle fortunately.
 */
int copro_handle_mm_fault(struct mm_struct *mm, unsigned long ea,
		unsigned long dsisr, unsigned *flt)
{
	struct vm_area_struct *vma;
	unsigned long is_write;
	int ret;

	if (mm == NULL)
		return -EFAULT;

	if (mm->pgd == NULL)
		return -EFAULT;

	down_read(&mm->mmap_sem);
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
		if (dsisr & DSISR_PROTFAULT)
			goto out_unlock;
		if (!(vma->vm_flags & (VM_READ | VM_EXEC)))
			goto out_unlock;
	}

	ret = 0;
	*flt = handle_mm_fault(mm, vma, ea, is_write ? FAULT_FLAG_WRITE : 0);
	if (unlikely(*flt & VM_FAULT_ERROR)) {
		if (*flt & VM_FAULT_OOM) {
			ret = -ENOMEM;
			goto out_unlock;
		} else if (*flt & VM_FAULT_SIGBUS) {
			ret = -EFAULT;
			goto out_unlock;
		}
		BUG();
	}

	if (*flt & VM_FAULT_MAJOR)
		current->maj_flt++;
	else
		current->min_flt++;

out_unlock:
	up_read(&mm->mmap_sem);
	return ret;
}
EXPORT_SYMBOL_GPL(copro_handle_mm_fault);

int copro_calculate_slb(struct mm_struct *mm, u64 ea, struct copro_slb *slb)
{
	u64 vsid;
	int psize, ssize;

	slb->esid = (ea & ESID_MASK) | SLB_ESID_V;

	switch (REGION_ID(ea)) {
	case USER_REGION_ID:
		pr_devel("%s: 0x%llx -- USER_REGION_ID\n", __func__, ea);
		psize = get_slice_psize(mm, ea);
		ssize = user_segment_size(ea);
		vsid = get_vsid(mm->context.id, ea, ssize);
		break;
	case VMALLOC_REGION_ID:
		pr_devel("%s: 0x%llx -- VMALLOC_REGION_ID\n", __func__, ea);
		if (ea < VMALLOC_END)
			psize = mmu_vmalloc_psize;
		else
			psize = mmu_io_psize;
		ssize = mmu_kernel_ssize;
		vsid = get_kernel_vsid(ea, mmu_kernel_ssize);
		break;
	case KERNEL_REGION_ID:
		pr_devel("%s: 0x%llx -- KERNEL_REGION_ID\n", __func__, ea);
		psize = mmu_linear_psize;
		ssize = mmu_kernel_ssize;
		vsid = get_kernel_vsid(ea, mmu_kernel_ssize);
		break;
	default:
		pr_debug("%s: invalid region access at %016llx\n", __func__, ea);
		return 1;
	}

	vsid = (vsid << slb_vsid_shift(ssize)) | SLB_VSID_USER;

	vsid |= mmu_psize_defs[psize].sllp |
		((ssize == MMU_SEGSIZE_1T) ? SLB_VSID_B_1T : 0);

	slb->vsid = vsid;

	return 0;
}
EXPORT_SYMBOL_GPL(copro_calculate_slb);

void copro_flush_all_slbs(struct mm_struct *mm)
{
#ifdef CONFIG_SPU_BASE
	spu_flush_all_slbs(mm);
#endif
}
EXPORT_SYMBOL_GPL(copro_flush_all_slbs);

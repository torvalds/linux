/*
 *  arch/s390/lib/uaccess_pt.c
 *
 *  User access functions based on page table walks.
 *
 *    Copyright IBM Corp. 2006
 *    Author(s): Gerald Schaefer (gerald.schaefer@de.ibm.com)
 */

#include <linux/errno.h>
#include <asm/uaccess.h>
#include <linux/mm.h>
#include <asm/futex.h>

static inline int __handle_fault(struct mm_struct *mm, unsigned long address,
				 int write_access)
{
	struct vm_area_struct *vma;
	int ret = -EFAULT;

	down_read(&mm->mmap_sem);
	vma = find_vma(mm, address);
	if (unlikely(!vma))
		goto out;
	if (unlikely(vma->vm_start > address)) {
		if (!(vma->vm_flags & VM_GROWSDOWN))
			goto out;
		if (expand_stack(vma, address))
			goto out;
	}

	if (!write_access) {
		/* page not present, check vm flags */
		if (!(vma->vm_flags & (VM_READ | VM_EXEC | VM_WRITE)))
			goto out;
	} else {
		if (!(vma->vm_flags & VM_WRITE))
			goto out;
	}

survive:
	switch (handle_mm_fault(mm, vma, address, write_access)) {
	case VM_FAULT_MINOR:
		current->min_flt++;
		break;
	case VM_FAULT_MAJOR:
		current->maj_flt++;
		break;
	case VM_FAULT_SIGBUS:
		goto out_sigbus;
	case VM_FAULT_OOM:
		goto out_of_memory;
	default:
		BUG();
	}
	ret = 0;
out:
	up_read(&mm->mmap_sem);
	return ret;

out_of_memory:
	up_read(&mm->mmap_sem);
	if (current->pid == 1) {
		yield();
		goto survive;
	}
	printk("VM: killing process %s\n", current->comm);
	return ret;

out_sigbus:
	up_read(&mm->mmap_sem);
	current->thread.prot_addr = address;
	current->thread.trap_no = 0x11;
	force_sig(SIGBUS, current);
	return ret;
}

static inline size_t __user_copy_pt(unsigned long uaddr, void *kptr,
				    size_t n, int write_user)
{
	struct mm_struct *mm = current->mm;
	unsigned long offset, pfn, done, size;
	pgd_t *pgd;
	pmd_t *pmd;
	pte_t *pte;
	void *from, *to;

	done = 0;
retry:
	spin_lock(&mm->page_table_lock);
	do {
		pgd = pgd_offset(mm, uaddr);
		if (pgd_none(*pgd) || unlikely(pgd_bad(*pgd)))
			goto fault;

		pmd = pmd_offset(pgd, uaddr);
		if (pmd_none(*pmd) || unlikely(pmd_bad(*pmd)))
			goto fault;

		pte = pte_offset_map(pmd, uaddr);
		if (!pte || !pte_present(*pte) ||
		    (write_user && !pte_write(*pte)))
			goto fault;

		pfn = pte_pfn(*pte);
		if (!pfn_valid(pfn))
			goto out;

		offset = uaddr & (PAGE_SIZE - 1);
		size = min(n - done, PAGE_SIZE - offset);
		if (write_user) {
			to = (void *)((pfn << PAGE_SHIFT) + offset);
			from = kptr + done;
		} else {
			from = (void *)((pfn << PAGE_SHIFT) + offset);
			to = kptr + done;
		}
		memcpy(to, from, size);
		done += size;
		uaddr += size;
	} while (done < n);
out:
	spin_unlock(&mm->page_table_lock);
	return n - done;
fault:
	spin_unlock(&mm->page_table_lock);
	if (__handle_fault(mm, uaddr, write_user))
		return n - done;
	goto retry;
}

size_t copy_from_user_pt(size_t n, const void __user *from, void *to)
{
	size_t rc;

	if (segment_eq(get_fs(), KERNEL_DS)) {
		memcpy(to, (void __kernel __force *) from, n);
		return 0;
	}
	rc = __user_copy_pt((unsigned long) from, to, n, 0);
	if (unlikely(rc))
		memset(to + n - rc, 0, rc);
	return rc;
}

size_t copy_to_user_pt(size_t n, void __user *to, const void *from)
{
	if (segment_eq(get_fs(), KERNEL_DS)) {
		memcpy((void __kernel __force *) to, from, n);
		return 0;
	}
	return __user_copy_pt((unsigned long) to, (void *) from, n, 1);
}

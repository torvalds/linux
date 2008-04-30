/*
 *  arch/s390/lib/uaccess_pt.c
 *
 *  User access functions based on page table walks for enhanced
 *  system layout without hardware support.
 *
 *    Copyright IBM Corp. 2006
 *    Author(s): Gerald Schaefer (gerald.schaefer@de.ibm.com)
 */

#include <linux/errno.h>
#include <linux/hardirq.h>
#include <linux/mm.h>
#include <asm/uaccess.h>
#include <asm/futex.h>
#include "uaccess.h"

static inline pte_t *follow_table(struct mm_struct *mm, unsigned long addr)
{
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;

	pgd = pgd_offset(mm, addr);
	if (pgd_none(*pgd) || unlikely(pgd_bad(*pgd)))
		return NULL;

	pud = pud_offset(pgd, addr);
	if (pud_none(*pud) || unlikely(pud_bad(*pud)))
		return NULL;

	pmd = pmd_offset(pud, addr);
	if (pmd_none(*pmd) || unlikely(pmd_bad(*pmd)))
		return NULL;

	return pte_offset_map(pmd, addr);
}

static int __handle_fault(struct mm_struct *mm, unsigned long address,
			  int write_access)
{
	struct vm_area_struct *vma;
	int ret = -EFAULT;
	int fault;

	if (in_atomic())
		return ret;
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
	fault = handle_mm_fault(mm, vma, address, write_access);
	if (unlikely(fault & VM_FAULT_ERROR)) {
		if (fault & VM_FAULT_OOM)
			goto out_of_memory;
		else if (fault & VM_FAULT_SIGBUS)
			goto out_sigbus;
		BUG();
	}
	if (fault & VM_FAULT_MAJOR)
		current->maj_flt++;
	else
		current->min_flt++;
	ret = 0;
out:
	up_read(&mm->mmap_sem);
	return ret;

out_of_memory:
	up_read(&mm->mmap_sem);
	if (is_global_init(current)) {
		yield();
		down_read(&mm->mmap_sem);
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

static size_t __user_copy_pt(unsigned long uaddr, void *kptr,
			     size_t n, int write_user)
{
	struct mm_struct *mm = current->mm;
	unsigned long offset, pfn, done, size;
	pte_t *pte;
	void *from, *to;

	done = 0;
retry:
	spin_lock(&mm->page_table_lock);
	do {
		pte = follow_table(mm, uaddr);
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

/*
 * Do DAT for user address by page table walk, return kernel address.
 * This function needs to be called with current->mm->page_table_lock held.
 */
static unsigned long __dat_user_addr(unsigned long uaddr)
{
	struct mm_struct *mm = current->mm;
	unsigned long pfn, ret;
	pte_t *pte;
	int rc;

	ret = 0;
retry:
	pte = follow_table(mm, uaddr);
	if (!pte || !pte_present(*pte))
		goto fault;

	pfn = pte_pfn(*pte);
	if (!pfn_valid(pfn))
		goto out;

	ret = (pfn << PAGE_SHIFT) + (uaddr & (PAGE_SIZE - 1));
out:
	return ret;
fault:
	spin_unlock(&mm->page_table_lock);
	rc = __handle_fault(mm, uaddr, 0);
	spin_lock(&mm->page_table_lock);
	if (rc)
		goto out;
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

static size_t clear_user_pt(size_t n, void __user *to)
{
	long done, size, ret;

	if (segment_eq(get_fs(), KERNEL_DS)) {
		memset((void __kernel __force *) to, 0, n);
		return 0;
	}
	done = 0;
	do {
		if (n - done > PAGE_SIZE)
			size = PAGE_SIZE;
		else
			size = n - done;
		ret = __user_copy_pt((unsigned long) to + done,
				      &empty_zero_page, size, 1);
		done += size;
		if (ret)
			return ret + n - done;
	} while (done < n);
	return 0;
}

static size_t strnlen_user_pt(size_t count, const char __user *src)
{
	char *addr;
	unsigned long uaddr = (unsigned long) src;
	struct mm_struct *mm = current->mm;
	unsigned long offset, pfn, done, len;
	pte_t *pte;
	size_t len_str;

	if (segment_eq(get_fs(), KERNEL_DS))
		return strnlen((const char __kernel __force *) src, count) + 1;
	done = 0;
retry:
	spin_lock(&mm->page_table_lock);
	do {
		pte = follow_table(mm, uaddr);
		if (!pte || !pte_present(*pte))
			goto fault;

		pfn = pte_pfn(*pte);
		if (!pfn_valid(pfn)) {
			done = -1;
			goto out;
		}

		offset = uaddr & (PAGE_SIZE-1);
		addr = (char *)(pfn << PAGE_SHIFT) + offset;
		len = min(count - done, PAGE_SIZE - offset);
		len_str = strnlen(addr, len);
		done += len_str;
		uaddr += len_str;
	} while ((len_str == len) && (done < count));
out:
	spin_unlock(&mm->page_table_lock);
	return done + 1;
fault:
	spin_unlock(&mm->page_table_lock);
	if (__handle_fault(mm, uaddr, 0)) {
		return 0;
	}
	goto retry;
}

static size_t strncpy_from_user_pt(size_t count, const char __user *src,
				   char *dst)
{
	size_t n = strnlen_user_pt(count, src);

	if (!n)
		return -EFAULT;
	if (n > count)
		n = count;
	if (segment_eq(get_fs(), KERNEL_DS)) {
		memcpy(dst, (const char __kernel __force *) src, n);
		if (dst[n-1] == '\0')
			return n-1;
		else
			return n;
	}
	if (__user_copy_pt((unsigned long) src, dst, n, 0))
		return -EFAULT;
	if (dst[n-1] == '\0')
		return n-1;
	else
		return n;
}

static size_t copy_in_user_pt(size_t n, void __user *to,
			      const void __user *from)
{
	struct mm_struct *mm = current->mm;
	unsigned long offset_from, offset_to, offset_max, pfn_from, pfn_to,
		      uaddr, done, size;
	unsigned long uaddr_from = (unsigned long) from;
	unsigned long uaddr_to = (unsigned long) to;
	pte_t *pte_from, *pte_to;
	int write_user;

	if (segment_eq(get_fs(), KERNEL_DS)) {
		memcpy((void __force *) to, (void __force *) from, n);
		return 0;
	}
	done = 0;
retry:
	spin_lock(&mm->page_table_lock);
	do {
		pte_from = follow_table(mm, uaddr_from);
		if (!pte_from || !pte_present(*pte_from)) {
			uaddr = uaddr_from;
			write_user = 0;
			goto fault;
		}

		pte_to = follow_table(mm, uaddr_to);
		if (!pte_to || !pte_present(*pte_to) || !pte_write(*pte_to)) {
			uaddr = uaddr_to;
			write_user = 1;
			goto fault;
		}

		pfn_from = pte_pfn(*pte_from);
		if (!pfn_valid(pfn_from))
			goto out;
		pfn_to = pte_pfn(*pte_to);
		if (!pfn_valid(pfn_to))
			goto out;

		offset_from = uaddr_from & (PAGE_SIZE-1);
		offset_to = uaddr_from & (PAGE_SIZE-1);
		offset_max = max(offset_from, offset_to);
		size = min(n - done, PAGE_SIZE - offset_max);

		memcpy((void *)(pfn_to << PAGE_SHIFT) + offset_to,
		       (void *)(pfn_from << PAGE_SHIFT) + offset_from, size);
		done += size;
		uaddr_from += size;
		uaddr_to += size;
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

#define __futex_atomic_op(insn, ret, oldval, newval, uaddr, oparg)	\
	asm volatile("0: l   %1,0(%6)\n"				\
		     "1: " insn						\
		     "2: cs  %1,%2,0(%6)\n"				\
		     "3: jl  1b\n"					\
		     "   lhi %0,0\n"					\
		     "4:\n"						\
		     EX_TABLE(0b,4b) EX_TABLE(2b,4b) EX_TABLE(3b,4b)	\
		     : "=d" (ret), "=&d" (oldval), "=&d" (newval),	\
		       "=m" (*uaddr)					\
		     : "0" (-EFAULT), "d" (oparg), "a" (uaddr),		\
		       "m" (*uaddr) : "cc" );

static int __futex_atomic_op_pt(int op, int __user *uaddr, int oparg, int *old)
{
	int oldval = 0, newval, ret;

	switch (op) {
	case FUTEX_OP_SET:
		__futex_atomic_op("lr %2,%5\n",
				  ret, oldval, newval, uaddr, oparg);
		break;
	case FUTEX_OP_ADD:
		__futex_atomic_op("lr %2,%1\nar %2,%5\n",
				  ret, oldval, newval, uaddr, oparg);
		break;
	case FUTEX_OP_OR:
		__futex_atomic_op("lr %2,%1\nor %2,%5\n",
				  ret, oldval, newval, uaddr, oparg);
		break;
	case FUTEX_OP_ANDN:
		__futex_atomic_op("lr %2,%1\nnr %2,%5\n",
				  ret, oldval, newval, uaddr, oparg);
		break;
	case FUTEX_OP_XOR:
		__futex_atomic_op("lr %2,%1\nxr %2,%5\n",
				  ret, oldval, newval, uaddr, oparg);
		break;
	default:
		ret = -ENOSYS;
	}
	if (ret == 0)
		*old = oldval;
	return ret;
}

int futex_atomic_op_pt(int op, int __user *uaddr, int oparg, int *old)
{
	int ret;

	if (segment_eq(get_fs(), KERNEL_DS))
		return __futex_atomic_op_pt(op, uaddr, oparg, old);
	spin_lock(&current->mm->page_table_lock);
	uaddr = (int __user *) __dat_user_addr((unsigned long) uaddr);
	if (!uaddr) {
		spin_unlock(&current->mm->page_table_lock);
		return -EFAULT;
	}
	get_page(virt_to_page(uaddr));
	spin_unlock(&current->mm->page_table_lock);
	ret = __futex_atomic_op_pt(op, uaddr, oparg, old);
	put_page(virt_to_page(uaddr));
	return ret;
}

static int __futex_atomic_cmpxchg_pt(int __user *uaddr, int oldval, int newval)
{
	int ret;

	asm volatile("0: cs   %1,%4,0(%5)\n"
		     "1: lr   %0,%1\n"
		     "2:\n"
		     EX_TABLE(0b,2b) EX_TABLE(1b,2b)
		     : "=d" (ret), "+d" (oldval), "=m" (*uaddr)
		     : "0" (-EFAULT), "d" (newval), "a" (uaddr), "m" (*uaddr)
		     : "cc", "memory" );
	return ret;
}

int futex_atomic_cmpxchg_pt(int __user *uaddr, int oldval, int newval)
{
	int ret;

	if (segment_eq(get_fs(), KERNEL_DS))
		return __futex_atomic_cmpxchg_pt(uaddr, oldval, newval);
	spin_lock(&current->mm->page_table_lock);
	uaddr = (int __user *) __dat_user_addr((unsigned long) uaddr);
	if (!uaddr) {
		spin_unlock(&current->mm->page_table_lock);
		return -EFAULT;
	}
	get_page(virt_to_page(uaddr));
	spin_unlock(&current->mm->page_table_lock);
	ret = __futex_atomic_cmpxchg_pt(uaddr, oldval, newval);
	put_page(virt_to_page(uaddr));
	return ret;
}

struct uaccess_ops uaccess_pt = {
	.copy_from_user		= copy_from_user_pt,
	.copy_from_user_small	= copy_from_user_pt,
	.copy_to_user		= copy_to_user_pt,
	.copy_to_user_small	= copy_to_user_pt,
	.copy_in_user		= copy_in_user_pt,
	.clear_user		= clear_user_pt,
	.strnlen_user		= strnlen_user_pt,
	.strncpy_from_user	= strncpy_from_user_pt,
	.futex_atomic_op	= futex_atomic_op_pt,
	.futex_atomic_cmpxchg	= futex_atomic_cmpxchg_pt,
};

/*
 *  User access functions based on page table walks for enhanced
 *  system layout without hardware support.
 *
 *    Copyright IBM Corp. 2006, 2012
 *    Author(s): Gerald Schaefer (gerald.schaefer@de.ibm.com)
 */

#include <linux/errno.h>
#include <linux/hardirq.h>
#include <linux/mm.h>
#include <linux/hugetlb.h>
#include <asm/uaccess.h>
#include <asm/futex.h>
#include "uaccess.h"


/*
 * Returns kernel address for user virtual address. If the returned address is
 * >= -4095 (IS_ERR_VALUE(x) returns true), a fault has occured and the address
 * contains the (negative) exception code.
 */
static __always_inline unsigned long follow_table(struct mm_struct *mm,
						  unsigned long addr, int write)
{
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *ptep;

	pgd = pgd_offset(mm, addr);
	if (pgd_none(*pgd) || unlikely(pgd_bad(*pgd)))
		return -0x3aUL;

	pud = pud_offset(pgd, addr);
	if (pud_none(*pud) || unlikely(pud_bad(*pud)))
		return -0x3bUL;

	pmd = pmd_offset(pud, addr);
	if (pmd_none(*pmd))
		return -0x10UL;
	if (pmd_large(*pmd)) {
		if (write && (pmd_val(*pmd) & _SEGMENT_ENTRY_RO))
			return -0x04UL;
		return (pmd_val(*pmd) & HPAGE_MASK) + (addr & ~HPAGE_MASK);
	}
	if (unlikely(pmd_bad(*pmd)))
		return -0x10UL;

	ptep = pte_offset_map(pmd, addr);
	if (!pte_present(*ptep))
		return -0x11UL;
	if (write && !pte_write(*ptep))
		return -0x04UL;

	return (pte_val(*ptep) & PAGE_MASK) + (addr & ~PAGE_MASK);
}

static __always_inline size_t __user_copy_pt(unsigned long uaddr, void *kptr,
					     size_t n, int write_user)
{
	struct mm_struct *mm = current->mm;
	unsigned long offset, done, size, kaddr;
	void *from, *to;

	done = 0;
retry:
	spin_lock(&mm->page_table_lock);
	do {
		kaddr = follow_table(mm, uaddr, write_user);
		if (IS_ERR_VALUE(kaddr))
			goto fault;

		offset = uaddr & ~PAGE_MASK;
		size = min(n - done, PAGE_SIZE - offset);
		if (write_user) {
			to = (void *) kaddr;
			from = kptr + done;
		} else {
			from = (void *) kaddr;
			to = kptr + done;
		}
		memcpy(to, from, size);
		done += size;
		uaddr += size;
	} while (done < n);
	spin_unlock(&mm->page_table_lock);
	return n - done;
fault:
	spin_unlock(&mm->page_table_lock);
	if (__handle_fault(uaddr, -kaddr, write_user))
		return n - done;
	goto retry;
}

/*
 * Do DAT for user address by page table walk, return kernel address.
 * This function needs to be called with current->mm->page_table_lock held.
 */
static __always_inline unsigned long __dat_user_addr(unsigned long uaddr,
						     int write)
{
	struct mm_struct *mm = current->mm;
	unsigned long kaddr;
	int rc;

retry:
	kaddr = follow_table(mm, uaddr, write);
	if (IS_ERR_VALUE(kaddr))
		goto fault;

	return kaddr;
fault:
	spin_unlock(&mm->page_table_lock);
	rc = __handle_fault(uaddr, -kaddr, write);
	spin_lock(&mm->page_table_lock);
	if (!rc)
		goto retry;
	return 0;
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
	unsigned long uaddr = (unsigned long) src;
	struct mm_struct *mm = current->mm;
	unsigned long offset, done, len, kaddr;
	size_t len_str;

	if (segment_eq(get_fs(), KERNEL_DS))
		return strnlen((const char __kernel __force *) src, count) + 1;
	done = 0;
retry:
	spin_lock(&mm->page_table_lock);
	do {
		kaddr = follow_table(mm, uaddr, 0);
		if (IS_ERR_VALUE(kaddr))
			goto fault;

		offset = uaddr & ~PAGE_MASK;
		len = min(count - done, PAGE_SIZE - offset);
		len_str = strnlen((char *) kaddr, len);
		done += len_str;
		uaddr += len_str;
	} while ((len_str == len) && (done < count));
	spin_unlock(&mm->page_table_lock);
	return done + 1;
fault:
	spin_unlock(&mm->page_table_lock);
	if (__handle_fault(uaddr, -kaddr, 0))
		return 0;
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
	unsigned long offset_max, uaddr, done, size, error_code;
	unsigned long uaddr_from = (unsigned long) from;
	unsigned long uaddr_to = (unsigned long) to;
	unsigned long kaddr_to, kaddr_from;
	int write_user;

	if (segment_eq(get_fs(), KERNEL_DS)) {
		memcpy((void __force *) to, (void __force *) from, n);
		return 0;
	}
	done = 0;
retry:
	spin_lock(&mm->page_table_lock);
	do {
		write_user = 0;
		uaddr = uaddr_from;
		kaddr_from = follow_table(mm, uaddr_from, 0);
		error_code = kaddr_from;
		if (IS_ERR_VALUE(error_code))
			goto fault;

		write_user = 1;
		uaddr = uaddr_to;
		kaddr_to = follow_table(mm, uaddr_to, 1);
		error_code = (unsigned long) kaddr_to;
		if (IS_ERR_VALUE(error_code))
			goto fault;

		offset_max = max(uaddr_from & ~PAGE_MASK,
				 uaddr_to & ~PAGE_MASK);
		size = min(n - done, PAGE_SIZE - offset_max);

		memcpy((void *) kaddr_to, (void *) kaddr_from, size);
		done += size;
		uaddr_from += size;
		uaddr_to += size;
	} while (done < n);
	spin_unlock(&mm->page_table_lock);
	return n - done;
fault:
	spin_unlock(&mm->page_table_lock);
	if (__handle_fault(uaddr, -error_code, write_user))
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

static int __futex_atomic_op_pt(int op, u32 __user *uaddr, int oparg, int *old)
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

int futex_atomic_op_pt(int op, u32 __user *uaddr, int oparg, int *old)
{
	int ret;

	if (segment_eq(get_fs(), KERNEL_DS))
		return __futex_atomic_op_pt(op, uaddr, oparg, old);
	spin_lock(&current->mm->page_table_lock);
	uaddr = (u32 __force __user *)
		__dat_user_addr((__force unsigned long) uaddr, 1);
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

static int __futex_atomic_cmpxchg_pt(u32 *uval, u32 __user *uaddr,
				     u32 oldval, u32 newval)
{
	int ret;

	asm volatile("0: cs   %1,%4,0(%5)\n"
		     "1: la   %0,0\n"
		     "2:\n"
		     EX_TABLE(0b,2b) EX_TABLE(1b,2b)
		     : "=d" (ret), "+d" (oldval), "=m" (*uaddr)
		     : "0" (-EFAULT), "d" (newval), "a" (uaddr), "m" (*uaddr)
		     : "cc", "memory" );
	*uval = oldval;
	return ret;
}

int futex_atomic_cmpxchg_pt(u32 *uval, u32 __user *uaddr,
			    u32 oldval, u32 newval)
{
	int ret;

	if (segment_eq(get_fs(), KERNEL_DS))
		return __futex_atomic_cmpxchg_pt(uval, uaddr, oldval, newval);
	spin_lock(&current->mm->page_table_lock);
	uaddr = (u32 __force __user *)
		__dat_user_addr((__force unsigned long) uaddr, 1);
	if (!uaddr) {
		spin_unlock(&current->mm->page_table_lock);
		return -EFAULT;
	}
	get_page(virt_to_page(uaddr));
	spin_unlock(&current->mm->page_table_lock);
	ret = __futex_atomic_cmpxchg_pt(uval, uaddr, oldval, newval);
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

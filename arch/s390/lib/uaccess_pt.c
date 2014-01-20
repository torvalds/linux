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

#ifndef CONFIG_64BIT
#define AHI	"ahi"
#define SLR	"slr"
#else
#define AHI	"aghi"
#define SLR	"slgr"
#endif

static size_t strnlen_kernel(size_t count, const char __user *src)
{
	register unsigned long reg0 asm("0") = 0UL;
	unsigned long tmp1, tmp2;

	asm volatile(
		"   la	  %2,0(%1)\n"
		"   la	  %3,0(%0,%1)\n"
		"  "SLR"  %0,%0\n"
		"0: srst  %3,%2\n"
		"   jo	  0b\n"
		"   la	  %0,1(%3)\n"	/* strnlen_kernel results includes \0 */
		"  "SLR"  %0,%1\n"
		"1:\n"
		EX_TABLE(0b,1b)
		: "+a" (count), "+a" (src), "=a" (tmp1), "=a" (tmp2)
		: "d" (reg0) : "cc", "memory");
	return count;
}

static size_t copy_in_kernel(size_t count, void __user *to,
			     const void __user *from)
{
	unsigned long tmp1;

	asm volatile(
		"  "AHI"  %0,-1\n"
		"   jo	  5f\n"
		"   bras  %3,3f\n"
		"0:"AHI"  %0,257\n"
		"1: mvc	  0(1,%1),0(%2)\n"
		"   la	  %1,1(%1)\n"
		"   la	  %2,1(%2)\n"
		"  "AHI"  %0,-1\n"
		"   jnz	  1b\n"
		"   j	  5f\n"
		"2: mvc	  0(256,%1),0(%2)\n"
		"   la	  %1,256(%1)\n"
		"   la	  %2,256(%2)\n"
		"3:"AHI"  %0,-256\n"
		"   jnm	  2b\n"
		"4: ex	  %0,1b-0b(%3)\n"
		"5:"SLR"  %0,%0\n"
		"6:\n"
		EX_TABLE(1b,6b) EX_TABLE(2b,0b) EX_TABLE(4b,0b)
		: "+a" (count), "+a" (to), "+a" (from), "=a" (tmp1)
		: : "cc", "memory");
	return count;
}

/*
 * Returns kernel address for user virtual address. If the returned address is
 * >= -4095 (IS_ERR_VALUE(x) returns true), a fault has occurred and the
 * address contains the (negative) exception code.
 */
#ifdef CONFIG_64BIT

static unsigned long follow_table(struct mm_struct *mm,
				  unsigned long address, int write)
{
	unsigned long *table = (unsigned long *)__pa(mm->pgd);

	if (unlikely(address > mm->context.asce_limit - 1))
		return -0x38UL;
	switch (mm->context.asce_bits & _ASCE_TYPE_MASK) {
	case _ASCE_TYPE_REGION1:
		table = table + ((address >> 53) & 0x7ff);
		if (unlikely(*table & _REGION_ENTRY_INVALID))
			return -0x39UL;
		table = (unsigned long *)(*table & _REGION_ENTRY_ORIGIN);
		/* fallthrough */
	case _ASCE_TYPE_REGION2:
		table = table + ((address >> 42) & 0x7ff);
		if (unlikely(*table & _REGION_ENTRY_INVALID))
			return -0x3aUL;
		table = (unsigned long *)(*table & _REGION_ENTRY_ORIGIN);
		/* fallthrough */
	case _ASCE_TYPE_REGION3:
		table = table + ((address >> 31) & 0x7ff);
		if (unlikely(*table & _REGION_ENTRY_INVALID))
			return -0x3bUL;
		table = (unsigned long *)(*table & _REGION_ENTRY_ORIGIN);
		/* fallthrough */
	case _ASCE_TYPE_SEGMENT:
		table = table + ((address >> 20) & 0x7ff);
		if (unlikely(*table & _SEGMENT_ENTRY_INVALID))
			return -0x10UL;
		if (unlikely(*table & _SEGMENT_ENTRY_LARGE)) {
			if (write && (*table & _SEGMENT_ENTRY_PROTECT))
				return -0x04UL;
			return (*table & _SEGMENT_ENTRY_ORIGIN_LARGE) +
				(address & ~_SEGMENT_ENTRY_ORIGIN_LARGE);
		}
		table = (unsigned long *)(*table & _SEGMENT_ENTRY_ORIGIN);
	}
	table = table + ((address >> 12) & 0xff);
	if (unlikely(*table & _PAGE_INVALID))
		return -0x11UL;
	if (write && (*table & _PAGE_PROTECT))
		return -0x04UL;
	return (*table & PAGE_MASK) + (address & ~PAGE_MASK);
}

#else /* CONFIG_64BIT */

static unsigned long follow_table(struct mm_struct *mm,
				  unsigned long address, int write)
{
	unsigned long *table = (unsigned long *)__pa(mm->pgd);

	table = table + ((address >> 20) & 0x7ff);
	if (unlikely(*table & _SEGMENT_ENTRY_INVALID))
		return -0x10UL;
	table = (unsigned long *)(*table & _SEGMENT_ENTRY_ORIGIN);
	table = table + ((address >> 12) & 0xff);
	if (unlikely(*table & _PAGE_INVALID))
		return -0x11UL;
	if (write && (*table & _PAGE_PROTECT))
		return -0x04UL;
	return (*table & PAGE_MASK) + (address & ~PAGE_MASK);
}

#endif /* CONFIG_64BIT */

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

	if (segment_eq(get_fs(), KERNEL_DS))
		return copy_in_kernel(n, (void __user *) to, from);
	rc = __user_copy_pt((unsigned long) from, to, n, 0);
	if (unlikely(rc))
		memset(to + n - rc, 0, rc);
	return rc;
}

size_t copy_to_user_pt(size_t n, void __user *to, const void *from)
{
	if (segment_eq(get_fs(), KERNEL_DS))
		return copy_in_kernel(n, to, (void __user *) from);
	return __user_copy_pt((unsigned long) to, (void *) from, n, 1);
}

static size_t clear_user_pt(size_t n, void __user *to)
{
	void *zpage = (void *) empty_zero_page;
	long done, size, ret;

	done = 0;
	do {
		if (n - done > PAGE_SIZE)
			size = PAGE_SIZE;
		else
			size = n - done;
		if (segment_eq(get_fs(), KERNEL_DS))
			ret = copy_in_kernel(n, to, (void __user *) zpage);
		else
			ret = __user_copy_pt((unsigned long) to, zpage, size, 1);
		done += size;
		to += size;
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

	if (unlikely(!count))
		return 0;
	if (segment_eq(get_fs(), KERNEL_DS))
		return strnlen_kernel(count, src);
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
	size_t done, len, offset, len_str;

	if (unlikely(!count))
		return 0;
	done = 0;
	do {
		offset = (size_t)src & ~PAGE_MASK;
		len = min(count - done, PAGE_SIZE - offset);
		if (segment_eq(get_fs(), KERNEL_DS)) {
			if (copy_in_kernel(len, (void __user *) dst, src))
				return -EFAULT;
		} else {
			if (__user_copy_pt((unsigned long) src, dst, len, 0))
				return -EFAULT;
		}
		len_str = strnlen(dst, len);
		done += len_str;
		src += len_str;
		dst += len_str;
	} while ((len_str == len) && (done < count));
	return done;
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

	if (segment_eq(get_fs(), KERNEL_DS))
		return copy_in_kernel(n, to, from);
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
	.copy_to_user		= copy_to_user_pt,
	.copy_in_user		= copy_in_user_pt,
	.clear_user		= clear_user_pt,
	.strnlen_user		= strnlen_user_pt,
	.strncpy_from_user	= strncpy_from_user_pt,
	.futex_atomic_op	= futex_atomic_op_pt,
	.futex_atomic_cmpxchg	= futex_atomic_cmpxchg_pt,
};

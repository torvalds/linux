// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2002 - 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 */

#include <linux/err.h>
#include <linux/highmem.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <asm/current.h>
#include <asm/page.h>
#include <kern_util.h>
#include <asm/futex.h>
#include <os.h>

pte_t *virt_to_pte(struct mm_struct *mm, unsigned long addr)
{
	pgd_t *pgd;
	p4d_t *p4d;
	pud_t *pud;
	pmd_t *pmd;

	if (mm == NULL)
		return NULL;

	pgd = pgd_offset(mm, addr);
	if (!pgd_present(*pgd))
		return NULL;

	p4d = p4d_offset(pgd, addr);
	if (!p4d_present(*p4d))
		return NULL;

	pud = pud_offset(p4d, addr);
	if (!pud_present(*pud))
		return NULL;

	pmd = pmd_offset(pud, addr);
	if (!pmd_present(*pmd))
		return NULL;

	return pte_offset_kernel(pmd, addr);
}

static pte_t *maybe_map(unsigned long virt, int is_write)
{
	pte_t *pte = virt_to_pte(current->mm, virt);
	int err, dummy_code;

	if ((pte == NULL) || !pte_present(*pte) ||
	    (is_write && !pte_write(*pte))) {
		err = handle_page_fault(virt, 0, is_write, 1, &dummy_code);
		if (err)
			return NULL;
		pte = virt_to_pte(current->mm, virt);
	}
	if (!pte_present(*pte))
		pte = NULL;

	return pte;
}

static int do_op_one_page(unsigned long addr, int len, int is_write,
		 int (*op)(unsigned long addr, int len, void *arg), void *arg)
{
	struct page *page;
	pte_t *pte;
	int n;

	pte = maybe_map(addr, is_write);
	if (pte == NULL)
		return -1;

	page = pte_page(*pte);
#ifdef CONFIG_64BIT
	pagefault_disable();
	addr = (unsigned long) page_address(page) +
		(addr & ~PAGE_MASK);
#else
	addr = (unsigned long) kmap_atomic(page) +
		(addr & ~PAGE_MASK);
#endif
	n = (*op)(addr, len, arg);

#ifdef CONFIG_64BIT
	pagefault_enable();
#else
	kunmap_atomic((void *)addr);
#endif

	return n;
}

static long buffer_op(unsigned long addr, int len, int is_write,
		      int (*op)(unsigned long, int, void *), void *arg)
{
	long size, remain, n;

	size = min(PAGE_ALIGN(addr) - addr, (unsigned long) len);
	remain = len;

	n = do_op_one_page(addr, size, is_write, op, arg);
	if (n != 0) {
		remain = (n < 0 ? remain : 0);
		goto out;
	}

	addr += size;
	remain -= size;
	if (remain == 0)
		goto out;

	while (addr < ((addr + remain) & PAGE_MASK)) {
		n = do_op_one_page(addr, PAGE_SIZE, is_write, op, arg);
		if (n != 0) {
			remain = (n < 0 ? remain : 0);
			goto out;
		}

		addr += PAGE_SIZE;
		remain -= PAGE_SIZE;
	}
	if (remain == 0)
		goto out;

	n = do_op_one_page(addr, remain, is_write, op, arg);
	if (n != 0) {
		remain = (n < 0 ? remain : 0);
		goto out;
	}

	return 0;
 out:
	return remain;
}

static int copy_chunk_from_user(unsigned long from, int len, void *arg)
{
	unsigned long *to_ptr = arg, to = *to_ptr;

	memcpy((void *) to, (void *) from, len);
	*to_ptr += len;
	return 0;
}

unsigned long raw_copy_from_user(void *to, const void __user *from, unsigned long n)
{
	if (uaccess_kernel()) {
		memcpy(to, (__force void*)from, n);
		return 0;
	}

	return buffer_op((unsigned long) from, n, 0, copy_chunk_from_user, &to);
}
EXPORT_SYMBOL(raw_copy_from_user);

static int copy_chunk_to_user(unsigned long to, int len, void *arg)
{
	unsigned long *from_ptr = arg, from = *from_ptr;

	memcpy((void *) to, (void *) from, len);
	*from_ptr += len;
	return 0;
}

unsigned long raw_copy_to_user(void __user *to, const void *from, unsigned long n)
{
	if (uaccess_kernel()) {
		memcpy((__force void *) to, from, n);
		return 0;
	}

	return buffer_op((unsigned long) to, n, 1, copy_chunk_to_user, &from);
}
EXPORT_SYMBOL(raw_copy_to_user);

static int strncpy_chunk_from_user(unsigned long from, int len, void *arg)
{
	char **to_ptr = arg, *to = *to_ptr;
	int n;

	strncpy(to, (void *) from, len);
	n = strnlen(to, len);
	*to_ptr += n;

	if (n < len)
	        return 1;
	return 0;
}

long __strncpy_from_user(char *dst, const char __user *src, long count)
{
	long n;
	char *ptr = dst;

	if (uaccess_kernel()) {
		strncpy(dst, (__force void *) src, count);
		return strnlen(dst, count);
	}

	n = buffer_op((unsigned long) src, count, 0, strncpy_chunk_from_user,
		      &ptr);
	if (n != 0)
		return -EFAULT;
	return strnlen(dst, count);
}
EXPORT_SYMBOL(__strncpy_from_user);

static int clear_chunk(unsigned long addr, int len, void *unused)
{
	memset((void *) addr, 0, len);
	return 0;
}

unsigned long __clear_user(void __user *mem, unsigned long len)
{
	if (uaccess_kernel()) {
		memset((__force void*)mem, 0, len);
		return 0;
	}

	return buffer_op((unsigned long) mem, len, 1, clear_chunk, NULL);
}
EXPORT_SYMBOL(__clear_user);

static int strnlen_chunk(unsigned long str, int len, void *arg)
{
	int *len_ptr = arg, n;

	n = strnlen((void *) str, len);
	*len_ptr += n;

	if (n < len)
		return 1;
	return 0;
}

long __strnlen_user(const void __user *str, long len)
{
	int count = 0, n;

	if (uaccess_kernel())
		return strnlen((__force char*)str, len) + 1;

	n = buffer_op((unsigned long) str, len, 0, strnlen_chunk, &count);
	if (n == 0)
		return count + 1;
	return 0;
}
EXPORT_SYMBOL(__strnlen_user);

/**
 * arch_futex_atomic_op_inuser() - Atomic arithmetic operation with constant
 *			  argument and comparison of the previous
 *			  futex value with another constant.
 *
 * @encoded_op:	encoded operation to execute
 * @uaddr:	pointer to user space address
 *
 * Return:
 * 0 - On success
 * -EFAULT - User access resulted in a page fault
 * -EAGAIN - Atomic operation was unable to complete due to contention
 * -ENOSYS - Operation not supported
 */

int arch_futex_atomic_op_inuser(int op, u32 oparg, int *oval, u32 __user *uaddr)
{
	int oldval, ret;
	struct page *page;
	unsigned long addr = (unsigned long) uaddr;
	pte_t *pte;

	ret = -EFAULT;
	if (!access_ok(uaddr, sizeof(*uaddr)))
		return -EFAULT;
	preempt_disable();
	pte = maybe_map(addr, 1);
	if (pte == NULL)
		goto out_inuser;

	page = pte_page(*pte);
#ifdef CONFIG_64BIT
	pagefault_disable();
	addr = (unsigned long) page_address(page) +
			(((unsigned long) addr) & ~PAGE_MASK);
#else
	addr = (unsigned long) kmap_atomic(page) +
		((unsigned long) addr & ~PAGE_MASK);
#endif
	uaddr = (u32 *) addr;
	oldval = *uaddr;

	ret = 0;

	switch (op) {
	case FUTEX_OP_SET:
		*uaddr = oparg;
		break;
	case FUTEX_OP_ADD:
		*uaddr += oparg;
		break;
	case FUTEX_OP_OR:
		*uaddr |= oparg;
		break;
	case FUTEX_OP_ANDN:
		*uaddr &= ~oparg;
		break;
	case FUTEX_OP_XOR:
		*uaddr ^= oparg;
		break;
	default:
		ret = -ENOSYS;
	}
#ifdef CONFIG_64BIT
	pagefault_enable();
#else
	kunmap_atomic((void *)addr);
#endif

out_inuser:
	preempt_enable();

	if (ret == 0)
		*oval = oldval;

	return ret;
}
EXPORT_SYMBOL(arch_futex_atomic_op_inuser);

/**
 * futex_atomic_cmpxchg_inatomic() - Compare and exchange the content of the
 *				uaddr with newval if the current value is
 *				oldval.
 * @uval:	pointer to store content of @uaddr
 * @uaddr:	pointer to user space address
 * @oldval:	old value
 * @newval:	new value to store to @uaddr
 *
 * Return:
 * 0 - On success
 * -EFAULT - User access resulted in a page fault
 * -EAGAIN - Atomic operation was unable to complete due to contention
 * -ENOSYS - Function not implemented (only if !HAVE_FUTEX_CMPXCHG)
 */

int futex_atomic_cmpxchg_inatomic(u32 *uval, u32 __user *uaddr,
			      u32 oldval, u32 newval)
{
	struct page *page;
	pte_t *pte;
	int ret = -EFAULT;

	if (!access_ok(uaddr, sizeof(*uaddr)))
		return -EFAULT;

	preempt_disable();
	pte = maybe_map((unsigned long) uaddr, 1);
	if (pte == NULL)
		goto out_inatomic;

	page = pte_page(*pte);
#ifdef CONFIG_64BIT
	pagefault_disable();
	uaddr = page_address(page) + (((unsigned long) uaddr) & ~PAGE_MASK);
#else
	uaddr = kmap_atomic(page) + ((unsigned long) uaddr & ~PAGE_MASK);
#endif

	*uval = *uaddr;

	ret = cmpxchg(uaddr, oldval, newval);

#ifdef CONFIG_64BIT
	pagefault_enable();
#else
	kunmap_atomic(uaddr);
#endif
	ret = 0;

out_inatomic:
	preempt_enable();
	return ret;
}
EXPORT_SYMBOL(futex_atomic_cmpxchg_inatomic);

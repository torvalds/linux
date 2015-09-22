/*
 * Copyright (C) 2002 - 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 * Licensed under the GPL
 */

#include <linux/err.h>
#include <linux/highmem.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <asm/current.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <kern_util.h>
#include <os.h>

pte_t *virt_to_pte(struct mm_struct *mm, unsigned long addr)
{
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;

	if (mm == NULL)
		return NULL;

	pgd = pgd_offset(mm, addr);
	if (!pgd_present(*pgd))
		return NULL;

	pud = pud_offset(pgd, addr);
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
	jmp_buf buf;
	struct page *page;
	pte_t *pte;
	int n, faulted;

	pte = maybe_map(addr, is_write);
	if (pte == NULL)
		return -1;

	page = pte_page(*pte);
	addr = (unsigned long) kmap_atomic(page) +
		(addr & ~PAGE_MASK);

	current->thread.fault_catcher = &buf;

	faulted = UML_SETJMP(&buf);
	if (faulted == 0)
		n = (*op)(addr, len, arg);
	else
		n = -1;

	current->thread.fault_catcher = NULL;

	kunmap_atomic((void *)addr);

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

long __copy_from_user(void *to, const void __user *from, unsigned long n)
{
	if (segment_eq(get_fs(), KERNEL_DS)) {
		memcpy(to, (__force void*)from, n);
		return 0;
	}

	return buffer_op((unsigned long) from, n, 0, copy_chunk_from_user, &to);
}
EXPORT_SYMBOL(__copy_from_user);

static int copy_chunk_to_user(unsigned long to, int len, void *arg)
{
	unsigned long *from_ptr = arg, from = *from_ptr;

	memcpy((void *) to, (void *) from, len);
	*from_ptr += len;
	return 0;
}

long __copy_to_user(void __user *to, const void *from, unsigned long n)
{
	if (segment_eq(get_fs(), KERNEL_DS)) {
		memcpy((__force void *) to, from, n);
		return 0;
	}

	return buffer_op((unsigned long) to, n, 1, copy_chunk_to_user, &from);
}
EXPORT_SYMBOL(__copy_to_user);

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

	if (segment_eq(get_fs(), KERNEL_DS)) {
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
	if (segment_eq(get_fs(), KERNEL_DS)) {
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

	if (segment_eq(get_fs(), KERNEL_DS))
		return strnlen((__force char*)str, len) + 1;

	n = buffer_op((unsigned long) str, len, 0, strnlen_chunk, &count);
	if (n == 0)
		return count + 1;
	return 0;
}
EXPORT_SYMBOL(__strnlen_user);

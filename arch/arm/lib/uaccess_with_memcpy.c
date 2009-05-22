/*
 *  linux/arch/arm/lib/uaccess_with_memcpy.c
 *
 *  Written by: Lennert Buytenhek and Nicolas Pitre
 *  Copyright (C) 2009 Marvell Semiconductor
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/ctype.h>
#include <linux/uaccess.h>
#include <linux/rwsem.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/hardirq.h> /* for in_atomic() */
#include <asm/current.h>
#include <asm/page.h>

static int
pin_page_for_write(const void __user *_addr, pte_t **ptep, spinlock_t **ptlp)
{
	unsigned long addr = (unsigned long)_addr;
	pgd_t *pgd;
	pmd_t *pmd;
	pte_t *pte;
	spinlock_t *ptl;

	pgd = pgd_offset(current->mm, addr);
	if (unlikely(pgd_none(*pgd) || pgd_bad(*pgd)))
		return 0;

	pmd = pmd_offset(pgd, addr);
	if (unlikely(pmd_none(*pmd) || pmd_bad(*pmd)))
		return 0;

	pte = pte_offset_map_lock(current->mm, pmd, addr, &ptl);
	if (unlikely(!pte_present(*pte) || !pte_young(*pte) ||
	    !pte_write(*pte) || !pte_dirty(*pte))) {
		pte_unmap_unlock(pte, ptl);
		return 0;
	}

	*ptep = pte;
	*ptlp = ptl;

	return 1;
}

static unsigned long noinline
__copy_to_user_memcpy(void __user *to, const void *from, unsigned long n)
{
	int atomic;

	if (unlikely(segment_eq(get_fs(), KERNEL_DS))) {
		memcpy((void *)to, from, n);
		return 0;
	}

	/* the mmap semaphore is taken only if not in an atomic context */
	atomic = in_atomic();

	if (!atomic)
		down_read(&current->mm->mmap_sem);
	while (n) {
		pte_t *pte;
		spinlock_t *ptl;
		int tocopy;

		while (!pin_page_for_write(to, &pte, &ptl)) {
			if (!atomic)
				up_read(&current->mm->mmap_sem);
			if (__put_user(0, (char __user *)to))
				goto out;
			if (!atomic)
				down_read(&current->mm->mmap_sem);
		}

		tocopy = (~(unsigned long)to & ~PAGE_MASK) + 1;
		if (tocopy > n)
			tocopy = n;

		memcpy((void *)to, from, tocopy);
		to += tocopy;
		from += tocopy;
		n -= tocopy;

		pte_unmap_unlock(pte, ptl);
	}
	if (!atomic)
		up_read(&current->mm->mmap_sem);

out:
	return n;
}

unsigned long
__copy_to_user(void __user *to, const void *from, unsigned long n)
{
	/*
	 * This test is stubbed out of the main function above to keep
	 * the overhead for small copies low by avoiding a large
	 * register dump on the stack just to reload them right away.
	 * With frame pointer disabled, tail call optimization kicks in
	 * as well making this test almost invisible.
	 */
	if (n < 1024)
		return __copy_to_user_std(to, from, n);
	return __copy_to_user_memcpy(to, from, n);
}
	
static unsigned long noinline
__clear_user_memset(void __user *addr, unsigned long n)
{
	if (unlikely(segment_eq(get_fs(), KERNEL_DS))) {
		memset((void *)addr, 0, n);
		return 0;
	}

	down_read(&current->mm->mmap_sem);
	while (n) {
		pte_t *pte;
		spinlock_t *ptl;
		int tocopy;

		while (!pin_page_for_write(addr, &pte, &ptl)) {
			up_read(&current->mm->mmap_sem);
			if (__put_user(0, (char __user *)addr))
				goto out;
			down_read(&current->mm->mmap_sem);
		}

		tocopy = (~(unsigned long)addr & ~PAGE_MASK) + 1;
		if (tocopy > n)
			tocopy = n;

		memset((void *)addr, 0, tocopy);
		addr += tocopy;
		n -= tocopy;

		pte_unmap_unlock(pte, ptl);
	}
	up_read(&current->mm->mmap_sem);

out:
	return n;
}

unsigned long __clear_user(void __user *addr, unsigned long n)
{
	/* See rational for this in __copy_to_user() above. */
	if (n < 256)
		return __clear_user_std(addr, n);
	return __clear_user_memset(addr, n);
}

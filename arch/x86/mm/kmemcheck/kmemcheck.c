/**
 * kmemcheck - a heavyweight memory checker for the linux kernel
 * Copyright (C) 2007, 2008  Vegard Nossum <vegardno@ifi.uio.no>
 * (With a lot of help from Ingo Molnar and Pekka Enberg.)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License (version 2) as
 * published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kallsyms.h>
#include <linux/kernel.h>
#include <linux/kmemcheck.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/page-flags.h>
#include <linux/percpu.h>
#include <linux/ptrace.h>
#include <linux/string.h>
#include <linux/types.h>

#include <asm/cacheflush.h>
#include <asm/kmemcheck.h>
#include <asm/pgtable.h>
#include <asm/tlbflush.h>

#include "error.h"
#include "opcode.h"
#include "pte.h"
#include "selftest.h"
#include "shadow.h"


#ifdef CONFIG_KMEMCHECK_DISABLED_BY_DEFAULT
#  define KMEMCHECK_ENABLED 0
#endif

#ifdef CONFIG_KMEMCHECK_ENABLED_BY_DEFAULT
#  define KMEMCHECK_ENABLED 1
#endif

#ifdef CONFIG_KMEMCHECK_ONESHOT_BY_DEFAULT
#  define KMEMCHECK_ENABLED 2
#endif

int kmemcheck_enabled = KMEMCHECK_ENABLED;

int __init kmemcheck_init(void)
{
#ifdef CONFIG_SMP
	/*
	 * Limit SMP to use a single CPU. We rely on the fact that this code
	 * runs before SMP is set up.
	 */
	if (setup_max_cpus > 1) {
		printk(KERN_INFO
			"kmemcheck: Limiting number of CPUs to 1.\n");
		setup_max_cpus = 1;
	}
#endif

	if (!kmemcheck_selftest()) {
		printk(KERN_INFO "kmemcheck: self-tests failed; disabling\n");
		kmemcheck_enabled = 0;
		return -EINVAL;
	}

	printk(KERN_INFO "kmemcheck: Initialized\n");
	return 0;
}

early_initcall(kmemcheck_init);

/*
 * We need to parse the kmemcheck= option before any memory is allocated.
 */
static int __init param_kmemcheck(char *str)
{
	if (!str)
		return -EINVAL;

	sscanf(str, "%d", &kmemcheck_enabled);
	return 0;
}

early_param("kmemcheck", param_kmemcheck);

int kmemcheck_show_addr(unsigned long address)
{
	pte_t *pte;

	pte = kmemcheck_pte_lookup(address);
	if (!pte)
		return 0;

	set_pte(pte, __pte(pte_val(*pte) | _PAGE_PRESENT));
	__flush_tlb_one(address);
	return 1;
}

int kmemcheck_hide_addr(unsigned long address)
{
	pte_t *pte;

	pte = kmemcheck_pte_lookup(address);
	if (!pte)
		return 0;

	set_pte(pte, __pte(pte_val(*pte) & ~_PAGE_PRESENT));
	__flush_tlb_one(address);
	return 1;
}

struct kmemcheck_context {
	bool busy;
	int balance;

	/*
	 * There can be at most two memory operands to an instruction, but
	 * each address can cross a page boundary -- so we may need up to
	 * four addresses that must be hidden/revealed for each fault.
	 */
	unsigned long addr[4];
	unsigned long n_addrs;
	unsigned long flags;

	/* Data size of the instruction that caused a fault. */
	unsigned int size;
};

static DEFINE_PER_CPU(struct kmemcheck_context, kmemcheck_context);

bool kmemcheck_active(struct pt_regs *regs)
{
	struct kmemcheck_context *data = &__get_cpu_var(kmemcheck_context);

	return data->balance > 0;
}

/* Save an address that needs to be shown/hidden */
static void kmemcheck_save_addr(unsigned long addr)
{
	struct kmemcheck_context *data = &__get_cpu_var(kmemcheck_context);

	BUG_ON(data->n_addrs >= ARRAY_SIZE(data->addr));
	data->addr[data->n_addrs++] = addr;
}

static unsigned int kmemcheck_show_all(void)
{
	struct kmemcheck_context *data = &__get_cpu_var(kmemcheck_context);
	unsigned int i;
	unsigned int n;

	n = 0;
	for (i = 0; i < data->n_addrs; ++i)
		n += kmemcheck_show_addr(data->addr[i]);

	return n;
}

static unsigned int kmemcheck_hide_all(void)
{
	struct kmemcheck_context *data = &__get_cpu_var(kmemcheck_context);
	unsigned int i;
	unsigned int n;

	n = 0;
	for (i = 0; i < data->n_addrs; ++i)
		n += kmemcheck_hide_addr(data->addr[i]);

	return n;
}

/*
 * Called from the #PF handler.
 */
void kmemcheck_show(struct pt_regs *regs)
{
	struct kmemcheck_context *data = &__get_cpu_var(kmemcheck_context);

	BUG_ON(!irqs_disabled());

	if (unlikely(data->balance != 0)) {
		kmemcheck_show_all();
		kmemcheck_error_save_bug(regs);
		data->balance = 0;
		return;
	}

	/*
	 * None of the addresses actually belonged to kmemcheck. Note that
	 * this is not an error.
	 */
	if (kmemcheck_show_all() == 0)
		return;

	++data->balance;

	/*
	 * The IF needs to be cleared as well, so that the faulting
	 * instruction can run "uninterrupted". Otherwise, we might take
	 * an interrupt and start executing that before we've had a chance
	 * to hide the page again.
	 *
	 * NOTE: In the rare case of multiple faults, we must not override
	 * the original flags:
	 */
	if (!(regs->flags & X86_EFLAGS_TF))
		data->flags = regs->flags;

	regs->flags |= X86_EFLAGS_TF;
	regs->flags &= ~X86_EFLAGS_IF;
}

/*
 * Called from the #DB handler.
 */
void kmemcheck_hide(struct pt_regs *regs)
{
	struct kmemcheck_context *data = &__get_cpu_var(kmemcheck_context);
	int n;

	BUG_ON(!irqs_disabled());

	if (unlikely(data->balance != 1)) {
		kmemcheck_show_all();
		kmemcheck_error_save_bug(regs);
		data->n_addrs = 0;
		data->balance = 0;

		if (!(data->flags & X86_EFLAGS_TF))
			regs->flags &= ~X86_EFLAGS_TF;
		if (data->flags & X86_EFLAGS_IF)
			regs->flags |= X86_EFLAGS_IF;
		return;
	}

	if (kmemcheck_enabled)
		n = kmemcheck_hide_all();
	else
		n = kmemcheck_show_all();

	if (n == 0)
		return;

	--data->balance;

	data->n_addrs = 0;

	if (!(data->flags & X86_EFLAGS_TF))
		regs->flags &= ~X86_EFLAGS_TF;
	if (data->flags & X86_EFLAGS_IF)
		regs->flags |= X86_EFLAGS_IF;
}

void kmemcheck_show_pages(struct page *p, unsigned int n)
{
	unsigned int i;

	for (i = 0; i < n; ++i) {
		unsigned long address;
		pte_t *pte;
		unsigned int level;

		address = (unsigned long) page_address(&p[i]);
		pte = lookup_address(address, &level);
		BUG_ON(!pte);
		BUG_ON(level != PG_LEVEL_4K);

		set_pte(pte, __pte(pte_val(*pte) | _PAGE_PRESENT));
		set_pte(pte, __pte(pte_val(*pte) & ~_PAGE_HIDDEN));
		__flush_tlb_one(address);
	}
}

bool kmemcheck_page_is_tracked(struct page *p)
{
	/* This will also check the "hidden" flag of the PTE. */
	return kmemcheck_pte_lookup((unsigned long) page_address(p));
}

void kmemcheck_hide_pages(struct page *p, unsigned int n)
{
	unsigned int i;

	for (i = 0; i < n; ++i) {
		unsigned long address;
		pte_t *pte;
		unsigned int level;

		address = (unsigned long) page_address(&p[i]);
		pte = lookup_address(address, &level);
		BUG_ON(!pte);
		BUG_ON(level != PG_LEVEL_4K);

		set_pte(pte, __pte(pte_val(*pte) & ~_PAGE_PRESENT));
		set_pte(pte, __pte(pte_val(*pte) | _PAGE_HIDDEN));
		__flush_tlb_one(address);
	}
}

/* Access may NOT cross page boundary */
static void kmemcheck_read_strict(struct pt_regs *regs,
	unsigned long addr, unsigned int size)
{
	void *shadow;
	enum kmemcheck_shadow status;

	shadow = kmemcheck_shadow_lookup(addr);
	if (!shadow)
		return;

	kmemcheck_save_addr(addr);
	status = kmemcheck_shadow_test(shadow, size);
	if (status == KMEMCHECK_SHADOW_INITIALIZED)
		return;

	if (kmemcheck_enabled)
		kmemcheck_error_save(status, addr, size, regs);

	if (kmemcheck_enabled == 2)
		kmemcheck_enabled = 0;

	/* Don't warn about it again. */
	kmemcheck_shadow_set(shadow, size);
}

bool kmemcheck_is_obj_initialized(unsigned long addr, size_t size)
{
	enum kmemcheck_shadow status;
	void *shadow;

	shadow = kmemcheck_shadow_lookup(addr);
	if (!shadow)
		return true;

	status = kmemcheck_shadow_test(shadow, size);

	return status == KMEMCHECK_SHADOW_INITIALIZED;
}

/* Access may cross page boundary */
static void kmemcheck_read(struct pt_regs *regs,
	unsigned long addr, unsigned int size)
{
	unsigned long page = addr & PAGE_MASK;
	unsigned long next_addr = addr + size - 1;
	unsigned long next_page = next_addr & PAGE_MASK;

	if (likely(page == next_page)) {
		kmemcheck_read_strict(regs, addr, size);
		return;
	}

	/*
	 * What we do is basically to split the access across the
	 * two pages and handle each part separately. Yes, this means
	 * that we may now see reads that are 3 + 5 bytes, for
	 * example (and if both are uninitialized, there will be two
	 * reports), but it makes the code a lot simpler.
	 */
	kmemcheck_read_strict(regs, addr, next_page - addr);
	kmemcheck_read_strict(regs, next_page, next_addr - next_page);
}

static void kmemcheck_write_strict(struct pt_regs *regs,
	unsigned long addr, unsigned int size)
{
	void *shadow;

	shadow = kmemcheck_shadow_lookup(addr);
	if (!shadow)
		return;

	kmemcheck_save_addr(addr);
	kmemcheck_shadow_set(shadow, size);
}

static void kmemcheck_write(struct pt_regs *regs,
	unsigned long addr, unsigned int size)
{
	unsigned long page = addr & PAGE_MASK;
	unsigned long next_addr = addr + size - 1;
	unsigned long next_page = next_addr & PAGE_MASK;

	if (likely(page == next_page)) {
		kmemcheck_write_strict(regs, addr, size);
		return;
	}

	/* See comment in kmemcheck_read(). */
	kmemcheck_write_strict(regs, addr, next_page - addr);
	kmemcheck_write_strict(regs, next_page, next_addr - next_page);
}

/*
 * Copying is hard. We have two addresses, each of which may be split across
 * a page (and each page will have different shadow addresses).
 */
static void kmemcheck_copy(struct pt_regs *regs,
	unsigned long src_addr, unsigned long dst_addr, unsigned int size)
{
	uint8_t shadow[8];
	enum kmemcheck_shadow status;

	unsigned long page;
	unsigned long next_addr;
	unsigned long next_page;

	uint8_t *x;
	unsigned int i;
	unsigned int n;

	BUG_ON(size > sizeof(shadow));

	page = src_addr & PAGE_MASK;
	next_addr = src_addr + size - 1;
	next_page = next_addr & PAGE_MASK;

	if (likely(page == next_page)) {
		/* Same page */
		x = kmemcheck_shadow_lookup(src_addr);
		if (x) {
			kmemcheck_save_addr(src_addr);
			for (i = 0; i < size; ++i)
				shadow[i] = x[i];
		} else {
			for (i = 0; i < size; ++i)
				shadow[i] = KMEMCHECK_SHADOW_INITIALIZED;
		}
	} else {
		n = next_page - src_addr;
		BUG_ON(n > sizeof(shadow));

		/* First page */
		x = kmemcheck_shadow_lookup(src_addr);
		if (x) {
			kmemcheck_save_addr(src_addr);
			for (i = 0; i < n; ++i)
				shadow[i] = x[i];
		} else {
			/* Not tracked */
			for (i = 0; i < n; ++i)
				shadow[i] = KMEMCHECK_SHADOW_INITIALIZED;
		}

		/* Second page */
		x = kmemcheck_shadow_lookup(next_page);
		if (x) {
			kmemcheck_save_addr(next_page);
			for (i = n; i < size; ++i)
				shadow[i] = x[i - n];
		} else {
			/* Not tracked */
			for (i = n; i < size; ++i)
				shadow[i] = KMEMCHECK_SHADOW_INITIALIZED;
		}
	}

	page = dst_addr & PAGE_MASK;
	next_addr = dst_addr + size - 1;
	next_page = next_addr & PAGE_MASK;

	if (likely(page == next_page)) {
		/* Same page */
		x = kmemcheck_shadow_lookup(dst_addr);
		if (x) {
			kmemcheck_save_addr(dst_addr);
			for (i = 0; i < size; ++i) {
				x[i] = shadow[i];
				shadow[i] = KMEMCHECK_SHADOW_INITIALIZED;
			}
		}
	} else {
		n = next_page - dst_addr;
		BUG_ON(n > sizeof(shadow));

		/* First page */
		x = kmemcheck_shadow_lookup(dst_addr);
		if (x) {
			kmemcheck_save_addr(dst_addr);
			for (i = 0; i < n; ++i) {
				x[i] = shadow[i];
				shadow[i] = KMEMCHECK_SHADOW_INITIALIZED;
			}
		}

		/* Second page */
		x = kmemcheck_shadow_lookup(next_page);
		if (x) {
			kmemcheck_save_addr(next_page);
			for (i = n; i < size; ++i) {
				x[i - n] = shadow[i];
				shadow[i] = KMEMCHECK_SHADOW_INITIALIZED;
			}
		}
	}

	status = kmemcheck_shadow_test(shadow, size);
	if (status == KMEMCHECK_SHADOW_INITIALIZED)
		return;

	if (kmemcheck_enabled)
		kmemcheck_error_save(status, src_addr, size, regs);

	if (kmemcheck_enabled == 2)
		kmemcheck_enabled = 0;
}

enum kmemcheck_method {
	KMEMCHECK_READ,
	KMEMCHECK_WRITE,
};

static void kmemcheck_access(struct pt_regs *regs,
	unsigned long fallback_address, enum kmemcheck_method fallback_method)
{
	const uint8_t *insn;
	const uint8_t *insn_primary;
	unsigned int size;

	struct kmemcheck_context *data = &__get_cpu_var(kmemcheck_context);

	/* Recursive fault -- ouch. */
	if (data->busy) {
		kmemcheck_show_addr(fallback_address);
		kmemcheck_error_save_bug(regs);
		return;
	}

	data->busy = true;

	insn = (const uint8_t *) regs->ip;
	insn_primary = kmemcheck_opcode_get_primary(insn);

	kmemcheck_opcode_decode(insn, &size);

	switch (insn_primary[0]) {
#ifdef CONFIG_KMEMCHECK_BITOPS_OK
		/* AND, OR, XOR */
		/*
		 * Unfortunately, these instructions have to be excluded from
		 * our regular checking since they access only some (and not
		 * all) bits. This clears out "bogus" bitfield-access warnings.
		 */
	case 0x80:
	case 0x81:
	case 0x82:
	case 0x83:
		switch ((insn_primary[1] >> 3) & 7) {
			/* OR */
		case 1:
			/* AND */
		case 4:
			/* XOR */
		case 6:
			kmemcheck_write(regs, fallback_address, size);
			goto out;

			/* ADD */
		case 0:
			/* ADC */
		case 2:
			/* SBB */
		case 3:
			/* SUB */
		case 5:
			/* CMP */
		case 7:
			break;
		}
		break;
#endif

		/* MOVS, MOVSB, MOVSW, MOVSD */
	case 0xa4:
	case 0xa5:
		/*
		 * These instructions are special because they take two
		 * addresses, but we only get one page fault.
		 */
		kmemcheck_copy(regs, regs->si, regs->di, size);
		goto out;

		/* CMPS, CMPSB, CMPSW, CMPSD */
	case 0xa6:
	case 0xa7:
		kmemcheck_read(regs, regs->si, size);
		kmemcheck_read(regs, regs->di, size);
		goto out;
	}

	/*
	 * If the opcode isn't special in any way, we use the data from the
	 * page fault handler to determine the address and type of memory
	 * access.
	 */
	switch (fallback_method) {
	case KMEMCHECK_READ:
		kmemcheck_read(regs, fallback_address, size);
		goto out;
	case KMEMCHECK_WRITE:
		kmemcheck_write(regs, fallback_address, size);
		goto out;
	}

out:
	data->busy = false;
}

bool kmemcheck_fault(struct pt_regs *regs, unsigned long address,
	unsigned long error_code)
{
	pte_t *pte;

	/*
	 * XXX: Is it safe to assume that memory accesses from virtual 86
	 * mode or non-kernel code segments will _never_ access kernel
	 * memory (e.g. tracked pages)? For now, we need this to avoid
	 * invoking kmemcheck for PnP BIOS calls.
	 */
	if (regs->flags & X86_VM_MASK)
		return false;
	if (regs->cs != __KERNEL_CS)
		return false;

	pte = kmemcheck_pte_lookup(address);
	if (!pte)
		return false;

	if (error_code & 2)
		kmemcheck_access(regs, address, KMEMCHECK_WRITE);
	else
		kmemcheck_access(regs, address, KMEMCHECK_READ);

	kmemcheck_show(regs);
	return true;
}

bool kmemcheck_trap(struct pt_regs *regs)
{
	if (!kmemcheck_active(regs))
		return false;

	/* We're done. */
	kmemcheck_hide(regs);
	return true;
}

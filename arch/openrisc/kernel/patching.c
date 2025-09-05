// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2020 SiFive
 * Copyright (C) 2025 Chen Miao
 */

#include <linux/mm.h>
#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <linux/uaccess.h>

#include <asm/insn-def.h>
#include <asm/cacheflush.h>
#include <asm/page.h>
#include <asm/fixmap.h>
#include <asm/text-patching.h>
#include <asm/sections.h>

static DEFINE_RAW_SPINLOCK(patch_lock);

static __always_inline void *patch_map(void *addr, int fixmap)
{
	uintptr_t uaddr = (uintptr_t) addr;
	phys_addr_t phys;

	if (core_kernel_text(uaddr)) {
		phys = __pa_symbol(addr);
	} else {
		struct page *page = vmalloc_to_page(addr);
		BUG_ON(!page);
		phys = page_to_phys(page) + offset_in_page(addr);
	}

	return (void *)set_fixmap_offset(fixmap, phys);
}

static void patch_unmap(int fixmap)
{
	clear_fixmap(fixmap);
}

static int __patch_insn_write(void *addr, u32 insn)
{
	void *waddr = addr;
	unsigned long flags = 0;
	int ret;

	raw_spin_lock_irqsave(&patch_lock, flags);

	waddr = patch_map(addr, FIX_TEXT_POKE0);

	ret = copy_to_kernel_nofault(waddr, &insn, OPENRISC_INSN_SIZE);
	local_icache_range_inv((unsigned long)waddr,
			       (unsigned long)waddr + OPENRISC_INSN_SIZE);

	patch_unmap(FIX_TEXT_POKE0);

	raw_spin_unlock_irqrestore(&patch_lock, flags);

	return ret;
}

/*
 * patch_insn_write - Write a single instruction to a specified memory location
 * This API provides a single-instruction patching, primarily used for runtime
 * code modification.
 * By the way, the insn size must be 4 bytes.
 */
int patch_insn_write(void *addr, u32 insn)
{
	u32 *tp = addr;
	int ret;

	if ((uintptr_t) tp & 0x3)
		return -EINVAL;

	ret = __patch_insn_write(tp, insn);

	return ret;
}

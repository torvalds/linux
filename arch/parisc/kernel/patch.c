// SPDX-License-Identifier: GPL-2.0
 /*
  * functions to patch RO kernel text during runtime
  *
  * Copyright (c) 2019 Sven Schnelle <svens@stackframe.org>
  */

#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <linux/kprobes.h>
#include <linux/mm.h>
#include <linux/stop_machine.h>

#include <asm/cacheflush.h>
#include <asm/fixmap.h>
#include <asm/text-patching.h>

struct patch {
	void *addr;
	u32 *insn;
	unsigned int len;
};

static DEFINE_RAW_SPINLOCK(patch_lock);

static void __kprobes *patch_map(void *addr, int fixmap, unsigned long *flags,
				 int *need_unmap)
{
	unsigned long uintaddr = (uintptr_t) addr;
	bool module = !core_kernel_text(uintaddr);
	struct page *page;

	*need_unmap = 0;
	if (module && IS_ENABLED(CONFIG_STRICT_MODULE_RWX))
		page = vmalloc_to_page(addr);
	else if (!module && IS_ENABLED(CONFIG_STRICT_KERNEL_RWX))
		page = virt_to_page(addr);
	else
		return addr;

	*need_unmap = 1;
	set_fixmap(fixmap, page_to_phys(page));
	raw_spin_lock_irqsave(&patch_lock, *flags);

	return (void *) (__fix_to_virt(fixmap) + (uintaddr & ~PAGE_MASK));
}

static void __kprobes patch_unmap(int fixmap, unsigned long *flags)
{
	clear_fixmap(fixmap);

	raw_spin_unlock_irqrestore(&patch_lock, *flags);
}

void __kprobes __patch_text_multiple(void *addr, u32 *insn, unsigned int len)
{
	unsigned long start = (unsigned long)addr;
	unsigned long end = (unsigned long)addr + len;
	unsigned long flags;
	u32 *p, *fixmap;
	int mapped;

	/* Make sure we don't have any aliases in cache */
	flush_kernel_dcache_range_asm(start, end);
	flush_kernel_icache_range_asm(start, end);
	flush_tlb_kernel_range(start, end);

	p = fixmap = patch_map(addr, FIX_TEXT_POKE0, &flags, &mapped);

	while (len >= 4) {
		*p++ = *insn++;
		addr += sizeof(u32);
		len -= sizeof(u32);
		if (len && offset_in_page(addr) == 0) {
			/*
			 * We're crossing a page boundary, so
			 * need to remap
			 */
			flush_kernel_dcache_range_asm((unsigned long)fixmap,
						      (unsigned long)p);
			flush_tlb_kernel_range((unsigned long)fixmap,
					       (unsigned long)p);
			if (mapped)
				patch_unmap(FIX_TEXT_POKE0, &flags);
			p = fixmap = patch_map(addr, FIX_TEXT_POKE0, &flags,
						&mapped);
		}
	}

	flush_kernel_dcache_range_asm((unsigned long)fixmap, (unsigned long)p);
	flush_tlb_kernel_range((unsigned long)fixmap, (unsigned long)p);
	if (mapped)
		patch_unmap(FIX_TEXT_POKE0, &flags);
}

void __kprobes __patch_text(void *addr, u32 insn)
{
	__patch_text_multiple(addr, &insn, sizeof(insn));
}

static int __kprobes patch_text_stop_machine(void *data)
{
	struct patch *patch = data;

	__patch_text_multiple(patch->addr, patch->insn, patch->len);
	return 0;
}

void __kprobes patch_text(void *addr, unsigned int insn)
{
	struct patch patch = {
		.addr = addr,
		.insn = &insn,
		.len = sizeof(insn),
	};

	stop_machine_cpuslocked(patch_text_stop_machine, &patch, NULL);
}

void __kprobes patch_text_multiple(void *addr, u32 *insn, unsigned int len)
{

	struct patch patch = {
		.addr = addr,
		.insn = insn,
		.len = len
	};

	stop_machine_cpuslocked(patch_text_stop_machine, &patch, NULL);
}

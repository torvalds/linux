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
#include <asm/patch.h>

struct patch {
	void *addr;
	unsigned int insn;
};

static void __kprobes *patch_map(void *addr, int fixmap)
{
	unsigned long uintaddr = (uintptr_t) addr;
	bool module = !core_kernel_text(uintaddr);
	struct page *page;

	if (module && IS_ENABLED(CONFIG_STRICT_MODULE_RWX))
		page = vmalloc_to_page(addr);
	else if (!module && IS_ENABLED(CONFIG_STRICT_KERNEL_RWX))
		page = virt_to_page(addr);
	else
		return addr;

	set_fixmap(fixmap, page_to_phys(page));

	return (void *) (__fix_to_virt(fixmap) + (uintaddr & ~PAGE_MASK));
}

static void __kprobes patch_unmap(int fixmap)
{
	clear_fixmap(fixmap);
}

void __kprobes __patch_text(void *addr, unsigned int insn)
{
	void *waddr = addr;
	int size;

	waddr = patch_map(addr, FIX_TEXT_POKE0);
	*(u32 *)waddr = insn;
	size = sizeof(u32);
	flush_kernel_vmap_range(waddr, size);
	patch_unmap(FIX_TEXT_POKE0);
	flush_icache_range((uintptr_t)(addr),
			   (uintptr_t)(addr) + size);
}

static int __kprobes patch_text_stop_machine(void *data)
{
	struct patch *patch = data;

	__patch_text(patch->addr, patch->insn);

	return 0;
}

void __kprobes patch_text(void *addr, unsigned int insn)
{
	struct patch patch = {
		.addr = addr,
		.insn = insn,
	};

	stop_machine_cpuslocked(patch_text_stop_machine, &patch, NULL);
}

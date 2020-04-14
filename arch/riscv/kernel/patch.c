// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020 SiFive
 */

#include <linux/spinlock.h>
#include <linux/mm.h>
#include <linux/uaccess.h>
#include <linux/stop_machine.h>
#include <asm/kprobes.h>
#include <asm/cacheflush.h>
#include <asm/fixmap.h>

struct riscv_insn_patch {
	void *addr;
	u32 insn;
	atomic_t cpu_count;
};

#ifdef CONFIG_MMU
static DEFINE_RAW_SPINLOCK(patch_lock);

static void __kprobes *patch_map(void *addr, int fixmap)
{
	uintptr_t uintaddr = (uintptr_t) addr;
	struct page *page;

	if (core_kernel_text(uintaddr))
		page = phys_to_page(__pa_symbol(addr));
	else if (IS_ENABLED(CONFIG_STRICT_MODULE_RWX))
		page = vmalloc_to_page(addr);
	else
		return addr;

	BUG_ON(!page);

	return (void *)set_fixmap_offset(fixmap, page_to_phys(page) +
					 (uintaddr & ~PAGE_MASK));
}

static void __kprobes patch_unmap(int fixmap)
{
	clear_fixmap(fixmap);
}

static int __kprobes riscv_insn_write(void *addr, const void *insn, size_t len)
{
	void *waddr = addr;
	bool across_pages = (((uintptr_t) addr & ~PAGE_MASK) + len) > PAGE_SIZE;
	unsigned long flags = 0;
	int ret;

	raw_spin_lock_irqsave(&patch_lock, flags);

	if (across_pages)
		patch_map(addr + len, FIX_TEXT_POKE1);

	waddr = patch_map(addr, FIX_TEXT_POKE0);

	ret = probe_kernel_write(waddr, insn, len);

	patch_unmap(FIX_TEXT_POKE0);

	if (across_pages)
		patch_unmap(FIX_TEXT_POKE1);

	raw_spin_unlock_irqrestore(&patch_lock, flags);

	return ret;
}
#else
static int __kprobes riscv_insn_write(void *addr, const void *insn, size_t len)
{
	return probe_kernel_write(addr, insn, len);
}
#endif /* CONFIG_MMU */

int __kprobes riscv_patch_text_nosync(void *addr, const void *insns, size_t len)
{
	u32 *tp = addr;
	int ret;

	ret = riscv_insn_write(tp, insns, len);

	if (!ret)
		flush_icache_range((uintptr_t) tp, (uintptr_t) tp + len);

	return ret;
}

static int __kprobes riscv_patch_text_cb(void *data)
{
	struct riscv_insn_patch *patch = data;
	int ret = 0;

	if (atomic_inc_return(&patch->cpu_count) == 1) {
		ret =
		    riscv_patch_text_nosync(patch->addr, &patch->insn,
					    GET_INSN_LENGTH(patch->insn));
		atomic_inc(&patch->cpu_count);
	} else {
		while (atomic_read(&patch->cpu_count) <= num_online_cpus())
			cpu_relax();
		smp_mb();
	}

	return ret;
}

int __kprobes riscv_patch_text(void *addr, u32 insn)
{
	struct riscv_insn_patch patch = {
		.addr = addr,
		.insn = insn,
		.cpu_count = ATOMIC_INIT(0),
	};

	return stop_machine_cpuslocked(riscv_patch_text_cb,
				       &patch, cpu_online_mask);
}

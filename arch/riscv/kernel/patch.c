// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020 SiFive
 */

#include <linux/spinlock.h>
#include <linux/mm.h>
#include <linux/memory.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <linux/stop_machine.h>
#include <asm/kprobes.h>
#include <asm/cacheflush.h>
#include <asm/fixmap.h>
#include <asm/ftrace.h>
#include <asm/patch.h>
#include <asm/sections.h>

struct patch_insn {
	void *addr;
	u32 *insns;
	size_t len;
	atomic_t cpu_count;
};

int riscv_patch_in_stop_machine = false;

#ifdef CONFIG_MMU

static inline bool is_kernel_exittext(uintptr_t addr)
{
	return system_state < SYSTEM_RUNNING &&
		addr >= (uintptr_t)__exittext_begin &&
		addr < (uintptr_t)__exittext_end;
}

/*
 * The fix_to_virt(, idx) needs a const value (not a dynamic variable of
 * reg-a0) or BUILD_BUG_ON failed with "idx >= __end_of_fixed_addresses".
 * So use '__always_inline' and 'const unsigned int fixmap' here.
 */
static __always_inline void *patch_map(void *addr, const unsigned int fixmap)
{
	uintptr_t uintaddr = (uintptr_t) addr;
	struct page *page;

	if (core_kernel_text(uintaddr) || is_kernel_exittext(uintaddr))
		page = phys_to_page(__pa_symbol(addr));
	else if (IS_ENABLED(CONFIG_STRICT_MODULE_RWX))
		page = vmalloc_to_page(addr);
	else
		return addr;

	BUG_ON(!page);

	return (void *)set_fixmap_offset(fixmap, page_to_phys(page) +
					 offset_in_page(addr));
}

static void patch_unmap(int fixmap)
{
	clear_fixmap(fixmap);
}
NOKPROBE_SYMBOL(patch_unmap);

static int __patch_insn_set(void *addr, u8 c, size_t len)
{
	bool across_pages = (offset_in_page(addr) + len) > PAGE_SIZE;
	void *waddr = addr;

	/*
	 * Only two pages can be mapped at a time for writing.
	 */
	if (len + offset_in_page(addr) > 2 * PAGE_SIZE)
		return -EINVAL;
	/*
	 * Before reaching here, it was expected to lock the text_mutex
	 * already, so we don't need to give another lock here and could
	 * ensure that it was safe between each cores.
	 */
	lockdep_assert_held(&text_mutex);

	preempt_disable();

	if (across_pages)
		patch_map(addr + PAGE_SIZE, FIX_TEXT_POKE1);

	waddr = patch_map(addr, FIX_TEXT_POKE0);

	memset(waddr, c, len);

	/*
	 * We could have just patched a function that is about to be
	 * called so make sure we don't execute partially patched
	 * instructions by flushing the icache as soon as possible.
	 */
	local_flush_icache_range((unsigned long)waddr,
				 (unsigned long)waddr + len);

	patch_unmap(FIX_TEXT_POKE0);

	if (across_pages)
		patch_unmap(FIX_TEXT_POKE1);

	preempt_enable();

	return 0;
}
NOKPROBE_SYMBOL(__patch_insn_set);

static int __patch_insn_write(void *addr, const void *insn, size_t len)
{
	bool across_pages = (offset_in_page(addr) + len) > PAGE_SIZE;
	void *waddr = addr;
	int ret;

	/*
	 * Only two pages can be mapped at a time for writing.
	 */
	if (len + offset_in_page(addr) > 2 * PAGE_SIZE)
		return -EINVAL;

	/*
	 * Before reaching here, it was expected to lock the text_mutex
	 * already, so we don't need to give another lock here and could
	 * ensure that it was safe between each cores.
	 *
	 * We're currently using stop_machine() for ftrace & kprobes, and while
	 * that ensures text_mutex is held before installing the mappings it
	 * does not ensure text_mutex is held by the calling thread.  That's
	 * safe but triggers a lockdep failure, so just elide it for that
	 * specific case.
	 */
	if (!riscv_patch_in_stop_machine)
		lockdep_assert_held(&text_mutex);

	preempt_disable();

	if (across_pages)
		patch_map(addr + PAGE_SIZE, FIX_TEXT_POKE1);

	waddr = patch_map(addr, FIX_TEXT_POKE0);

	ret = copy_to_kernel_nofault(waddr, insn, len);

	/*
	 * We could have just patched a function that is about to be
	 * called so make sure we don't execute partially patched
	 * instructions by flushing the icache as soon as possible.
	 */
	local_flush_icache_range((unsigned long)waddr,
				 (unsigned long)waddr + len);

	patch_unmap(FIX_TEXT_POKE0);

	if (across_pages)
		patch_unmap(FIX_TEXT_POKE1);

	preempt_enable();

	return ret;
}
NOKPROBE_SYMBOL(__patch_insn_write);
#else
static int __patch_insn_set(void *addr, u8 c, size_t len)
{
	memset(addr, c, len);

	return 0;
}
NOKPROBE_SYMBOL(__patch_insn_set);

static int __patch_insn_write(void *addr, const void *insn, size_t len)
{
	return copy_to_kernel_nofault(addr, insn, len);
}
NOKPROBE_SYMBOL(__patch_insn_write);
#endif /* CONFIG_MMU */

static int patch_insn_set(void *addr, u8 c, size_t len)
{
	size_t size;
	int ret;

	/*
	 * __patch_insn_set() can only work on 2 pages at a time so call it in a
	 * loop with len <= 2 * PAGE_SIZE.
	 */
	while (len) {
		size = min(len, PAGE_SIZE * 2 - offset_in_page(addr));
		ret = __patch_insn_set(addr, c, size);
		if (ret)
			return ret;

		addr += size;
		len -= size;
	}

	return 0;
}
NOKPROBE_SYMBOL(patch_insn_set);

int patch_text_set_nosync(void *addr, u8 c, size_t len)
{
	int ret;

	ret = patch_insn_set(addr, c, len);
	if (!ret)
		flush_icache_range((uintptr_t)addr, (uintptr_t)addr + len);

	return ret;
}
NOKPROBE_SYMBOL(patch_text_set_nosync);

int patch_insn_write(void *addr, const void *insn, size_t len)
{
	size_t size;
	int ret;

	/*
	 * Copy the instructions to the destination address, two pages at a time
	 * because __patch_insn_write() can only handle len <= 2 * PAGE_SIZE.
	 */
	while (len) {
		size = min(len, PAGE_SIZE * 2 - offset_in_page(addr));
		ret = __patch_insn_write(addr, insn, size);
		if (ret)
			return ret;

		addr += size;
		insn += size;
		len -= size;
	}

	return 0;
}
NOKPROBE_SYMBOL(patch_insn_write);

int patch_text_nosync(void *addr, const void *insns, size_t len)
{
	int ret;

	ret = patch_insn_write(addr, insns, len);
	if (!ret)
		flush_icache_range((uintptr_t)addr, (uintptr_t)addr + len);

	return ret;
}
NOKPROBE_SYMBOL(patch_text_nosync);

static int patch_text_cb(void *data)
{
	struct patch_insn *patch = data;
	int ret = 0;

	if (atomic_inc_return(&patch->cpu_count) == num_online_cpus()) {
		ret = patch_insn_write(patch->addr, patch->insns, patch->len);
		/*
		 * Make sure the patching store is effective *before* we
		 * increment the counter which releases all waiting CPUs
		 * by using the release variant of atomic increment. The
		 * release pairs with the call to local_flush_icache_all()
		 * on the waiting CPU.
		 */
		atomic_inc_return_release(&patch->cpu_count);
	} else {
		while (atomic_read(&patch->cpu_count) <= num_online_cpus())
			cpu_relax();

		local_flush_icache_all();
	}

	return ret;
}
NOKPROBE_SYMBOL(patch_text_cb);

int patch_text(void *addr, u32 *insns, size_t len)
{
	int ret;
	struct patch_insn patch = {
		.addr = addr,
		.insns = insns,
		.len = len,
		.cpu_count = ATOMIC_INIT(0),
	};

	/*
	 * kprobes takes text_mutex, before calling patch_text(), but as we call
	 * calls stop_machine(), the lockdep assertion in patch_insn_write()
	 * gets confused by the context in which the lock is taken.
	 * Instead, ensure the lock is held before calling stop_machine(), and
	 * set riscv_patch_in_stop_machine to skip the check in
	 * patch_insn_write().
	 */
	lockdep_assert_held(&text_mutex);
	riscv_patch_in_stop_machine = true;
	ret = stop_machine_cpuslocked(patch_text_cb, &patch, cpu_online_mask);
	riscv_patch_in_stop_machine = false;
	return ret;
}
NOKPROBE_SYMBOL(patch_text);

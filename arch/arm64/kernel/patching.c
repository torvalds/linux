// SPDX-License-Identifier: GPL-2.0-only
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/spinlock.h>
#include <linux/stop_machine.h>
#include <linux/uaccess.h>

#include <asm/cacheflush.h>
#include <asm/fixmap.h>
#include <asm/insn.h>
#include <asm/kprobes.h>
#include <asm/patching.h>
#include <asm/sections.h>

static DEFINE_RAW_SPINLOCK(patch_lock);

static bool is_exit_text(unsigned long addr)
{
	/* discarded with init text/data */
	return system_state < SYSTEM_RUNNING &&
		addr >= (unsigned long)__exittext_begin &&
		addr < (unsigned long)__exittext_end;
}

static bool is_image_text(unsigned long addr)
{
	return core_kernel_text(addr) || is_exit_text(addr);
}

static void __kprobes *patch_map(void *addr, int fixmap)
{
	unsigned long uintaddr = (uintptr_t) addr;
	bool image = is_image_text(uintaddr);
	struct page *page;

	if (image)
		page = phys_to_page(__pa_symbol(addr));
	else if (IS_ENABLED(CONFIG_EXECMEM))
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
/*
 * In ARMv8-A, A64 instructions have a fixed length of 32 bits and are always
 * little-endian.
 */
int __kprobes aarch64_insn_read(void *addr, u32 *insnp)
{
	int ret;
	__le32 val;

	ret = copy_from_kernel_nofault(&val, addr, AARCH64_INSN_SIZE);
	if (!ret)
		*insnp = le32_to_cpu(val);

	return ret;
}

static int __kprobes __aarch64_insn_write(void *addr, __le32 insn)
{
	void *waddr = addr;
	unsigned long flags = 0;
	int ret;

	raw_spin_lock_irqsave(&patch_lock, flags);
	waddr = patch_map(addr, FIX_TEXT_POKE0);

	ret = copy_to_kernel_nofault(waddr, &insn, AARCH64_INSN_SIZE);

	patch_unmap(FIX_TEXT_POKE0);
	raw_spin_unlock_irqrestore(&patch_lock, flags);

	return ret;
}

int __kprobes aarch64_insn_write(void *addr, u32 insn)
{
	return __aarch64_insn_write(addr, cpu_to_le32(insn));
}

noinstr int aarch64_insn_write_literal_u64(void *addr, u64 val)
{
	u64 *waddr;
	unsigned long flags;
	int ret;

	raw_spin_lock_irqsave(&patch_lock, flags);
	waddr = patch_map(addr, FIX_TEXT_POKE0);

	ret = copy_to_kernel_nofault(waddr, &val, sizeof(val));

	patch_unmap(FIX_TEXT_POKE0);
	raw_spin_unlock_irqrestore(&patch_lock, flags);

	return ret;
}

typedef void text_poke_f(void *dst, void *src, size_t patched, size_t len);

static void *__text_poke(text_poke_f func, void *addr, void *src, size_t len)
{
	unsigned long flags;
	size_t patched = 0;
	size_t size;
	void *waddr;
	void *ptr;

	raw_spin_lock_irqsave(&patch_lock, flags);

	while (patched < len) {
		ptr = addr + patched;
		size = min_t(size_t, PAGE_SIZE - offset_in_page(ptr),
			     len - patched);

		waddr = patch_map(ptr, FIX_TEXT_POKE0);
		func(waddr, src, patched, size);
		patch_unmap(FIX_TEXT_POKE0);

		patched += size;
	}
	raw_spin_unlock_irqrestore(&patch_lock, flags);

	flush_icache_range((uintptr_t)addr, (uintptr_t)addr + len);

	return addr;
}

static void text_poke_memcpy(void *dst, void *src, size_t patched, size_t len)
{
	copy_to_kernel_nofault(dst, src + patched, len);
}

static void text_poke_memset(void *dst, void *src, size_t patched, size_t len)
{
	u32 c = *(u32 *)src;

	memset32(dst, c, len / 4);
}

/**
 * aarch64_insn_copy - Copy instructions into (an unused part of) RX memory
 * @dst: address to modify
 * @src: source of the copy
 * @len: length to copy
 *
 * Useful for JITs to dump new code blocks into unused regions of RX memory.
 */
noinstr void *aarch64_insn_copy(void *dst, void *src, size_t len)
{
	/* A64 instructions must be word aligned */
	if ((uintptr_t)dst & 0x3)
		return NULL;

	return __text_poke(text_poke_memcpy, dst, src, len);
}

/**
 * aarch64_insn_set - memset for RX memory regions.
 * @dst: address to modify
 * @insn: value to set
 * @len: length of memory region.
 *
 * Useful for JITs to fill regions of RX memory with illegal instructions.
 */
noinstr void *aarch64_insn_set(void *dst, u32 insn, size_t len)
{
	if ((uintptr_t)dst & 0x3)
		return NULL;

	return __text_poke(text_poke_memset, dst, &insn, len);
}

int __kprobes aarch64_insn_patch_text_nosync(void *addr, u32 insn)
{
	u32 *tp = addr;
	int ret;

	/* A64 instructions must be word aligned */
	if ((uintptr_t)tp & 0x3)
		return -EINVAL;

	ret = aarch64_insn_write(tp, insn);
	if (ret == 0)
		caches_clean_inval_pou((uintptr_t)tp,
				     (uintptr_t)tp + AARCH64_INSN_SIZE);

	return ret;
}

struct aarch64_insn_patch {
	void		**text_addrs;
	u32		*new_insns;
	int		insn_cnt;
	atomic_t	cpu_count;
};

static int __kprobes aarch64_insn_patch_text_cb(void *arg)
{
	int i, ret = 0;
	struct aarch64_insn_patch *pp = arg;

	/* The last CPU becomes master */
	if (atomic_inc_return(&pp->cpu_count) == num_online_cpus()) {
		for (i = 0; ret == 0 && i < pp->insn_cnt; i++)
			ret = aarch64_insn_patch_text_nosync(pp->text_addrs[i],
							     pp->new_insns[i]);
		/* Notify other processors with an additional increment. */
		atomic_inc(&pp->cpu_count);
	} else {
		while (atomic_read(&pp->cpu_count) <= num_online_cpus())
			cpu_relax();
		isb();
	}

	return ret;
}

int __kprobes aarch64_insn_patch_text(void *addrs[], u32 insns[], int cnt)
{
	struct aarch64_insn_patch patch = {
		.text_addrs = addrs,
		.new_insns = insns,
		.insn_cnt = cnt,
		.cpu_count = ATOMIC_INIT(0),
	};

	if (cnt <= 0)
		return -EINVAL;

	return stop_machine_cpuslocked(aarch64_insn_patch_text_cb, &patch,
				       cpu_online_mask);
}

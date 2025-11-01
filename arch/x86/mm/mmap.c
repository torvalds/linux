// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Flexible mmap layout support
 *
 * Based on code by Ingo Molnar and Andi Kleen, copyrighted
 * as follows:
 *
 * Copyright 2003-2009 Red Hat Inc.
 * All Rights Reserved.
 * Copyright 2005 Andi Kleen, SUSE Labs.
 * Copyright 2007 Jiri Kosina, SUSE Labs.
 */

#include <linux/personality.h>
#include <linux/mm.h>
#include <linux/random.h>
#include <linux/limits.h>
#include <linux/sched/signal.h>
#include <linux/sched/mm.h>
#include <linux/compat.h>
#include <linux/elf-randomize.h>
#include <asm/elf.h>
#include <asm/io.h>

#include "physaddr.h"

struct va_alignment __read_mostly va_align = {
	.flags = -1,
};

unsigned long task_size_32bit(void)
{
	return IA32_PAGE_OFFSET;
}

unsigned long task_size_64bit(int full_addr_space)
{
	return full_addr_space ? TASK_SIZE_MAX : DEFAULT_MAP_WINDOW;
}

static unsigned long stack_maxrandom_size(unsigned long task_size)
{
	unsigned long max = 0;
	if (current->flags & PF_RANDOMIZE) {
		max = (-1UL) & __STACK_RND_MASK(task_size == task_size_32bit());
		max <<= PAGE_SHIFT;
	}

	return max;
}

#ifdef CONFIG_COMPAT
# define mmap32_rnd_bits  mmap_rnd_compat_bits
# define mmap64_rnd_bits  mmap_rnd_bits
#else
# define mmap32_rnd_bits  mmap_rnd_bits
# define mmap64_rnd_bits  mmap_rnd_bits
#endif

#define SIZE_128M    (128 * 1024 * 1024UL)

static int mmap_is_legacy(void)
{
	if (current->personality & ADDR_COMPAT_LAYOUT)
		return 1;

	return sysctl_legacy_va_layout;
}

static unsigned long arch_rnd(unsigned int rndbits)
{
	if (!(current->flags & PF_RANDOMIZE))
		return 0;
	return (get_random_long() & ((1UL << rndbits) - 1)) << PAGE_SHIFT;
}

unsigned long arch_mmap_rnd(void)
{
	return arch_rnd(mmap_is_ia32() ? mmap32_rnd_bits : mmap64_rnd_bits);
}

static unsigned long mmap_base(unsigned long rnd, unsigned long task_size,
			       const struct rlimit *rlim_stack)
{
	unsigned long gap = rlim_stack->rlim_cur;
	unsigned long pad = stack_maxrandom_size(task_size) + stack_guard_gap;

	/* Values close to RLIM_INFINITY can overflow. */
	if (gap + pad > gap)
		gap += pad;

	/*
	 * Top of mmap area (just below the process stack).
	 * Leave an at least ~128 MB hole with possible stack randomization.
	 */
	gap = clamp(gap, SIZE_128M, (task_size / 6) * 5);

	return PAGE_ALIGN(task_size - gap - rnd);
}

static unsigned long mmap_legacy_base(unsigned long rnd,
				      unsigned long task_size)
{
	return __TASK_UNMAPPED_BASE(task_size) + rnd;
}

/*
 * This function, called very early during the creation of a new
 * process VM image, sets up which VM layout function to use:
 */
static void arch_pick_mmap_base(unsigned long *base, unsigned long *legacy_base,
		unsigned long random_factor, unsigned long task_size,
		const struct rlimit *rlim_stack)
{
	*legacy_base = mmap_legacy_base(random_factor, task_size);
	if (mmap_is_legacy())
		*base = *legacy_base;
	else
		*base = mmap_base(random_factor, task_size, rlim_stack);
}

void arch_pick_mmap_layout(struct mm_struct *mm, const struct rlimit *rlim_stack)
{
	if (mmap_is_legacy())
		mm_flags_clear(MMF_TOPDOWN, mm);
	else
		mm_flags_set(MMF_TOPDOWN, mm);

	arch_pick_mmap_base(&mm->mmap_base, &mm->mmap_legacy_base,
			arch_rnd(mmap64_rnd_bits), task_size_64bit(0),
			rlim_stack);

#ifdef CONFIG_HAVE_ARCH_COMPAT_MMAP_BASES
	/*
	 * The mmap syscall mapping base decision depends solely on the
	 * syscall type (64-bit or compat). This applies for 64bit
	 * applications and 32bit applications. The 64bit syscall uses
	 * mmap_base, the compat syscall uses mmap_compat_base.
	 */
	arch_pick_mmap_base(&mm->mmap_compat_base, &mm->mmap_compat_legacy_base,
			arch_rnd(mmap32_rnd_bits), task_size_32bit(),
			rlim_stack);
#endif
}

unsigned long get_mmap_base(int is_legacy)
{
	struct mm_struct *mm = current->mm;

#ifdef CONFIG_HAVE_ARCH_COMPAT_MMAP_BASES
	if (in_32bit_syscall()) {
		return is_legacy ? mm->mmap_compat_legacy_base
				 : mm->mmap_compat_base;
	}
#endif
	return is_legacy ? mm->mmap_legacy_base : mm->mmap_base;
}

/**
 * mmap_address_hint_valid - Validate the address hint of mmap
 * @addr:	Address hint
 * @len:	Mapping length
 *
 * Check whether @addr and @addr + @len result in a valid mapping.
 *
 * On 32bit this only checks whether @addr + @len is <= TASK_SIZE.
 *
 * On 64bit with 5-level page tables another sanity check is required
 * because mappings requested by mmap(@addr, 0) which cross the 47-bit
 * virtual address boundary can cause the following theoretical issue:
 *
 *  An application calls mmap(addr, 0), i.e. without MAP_FIXED, where @addr
 *  is below the border of the 47-bit address space and @addr + @len is
 *  above the border.
 *
 *  With 4-level paging this request succeeds, but the resulting mapping
 *  address will always be within the 47-bit virtual address space, because
 *  the hint address does not result in a valid mapping and is
 *  ignored. Hence applications which are not prepared to handle virtual
 *  addresses above 47-bit work correctly.
 *
 *  With 5-level paging this request would be granted and result in a
 *  mapping which crosses the border of the 47-bit virtual address
 *  space. If the application cannot handle addresses above 47-bit this
 *  will lead to misbehaviour and hard to diagnose failures.
 *
 * Therefore ignore address hints which would result in a mapping crossing
 * the 47-bit virtual address boundary.
 *
 * Note, that in the same scenario with MAP_FIXED the behaviour is
 * different. The request with @addr < 47-bit and @addr + @len > 47-bit
 * fails on a 4-level paging machine but succeeds on a 5-level paging
 * machine. It is reasonable to expect that an application does not rely on
 * the failure of such a fixed mapping request, so the restriction is not
 * applied.
 */
bool mmap_address_hint_valid(unsigned long addr, unsigned long len)
{
	if (TASK_SIZE - len < addr)
		return false;

	return (addr > DEFAULT_MAP_WINDOW) == (addr + len > DEFAULT_MAP_WINDOW);
}

/* Can we access it for direct reading/writing? Must be RAM: */
int valid_phys_addr_range(phys_addr_t addr, size_t count)
{
	return addr + count - 1 <= __pa(high_memory - 1);
}

/* Can we access it through mmap? Must be a valid physical address: */
int valid_mmap_phys_addr_range(unsigned long pfn, size_t count)
{
	phys_addr_t addr = (phys_addr_t)pfn << PAGE_SHIFT;

	return phys_addr_valid(addr + count - 1);
}

/*
 * Only allow root to set high MMIO mappings to PROT_NONE.
 * This prevents an unpriv. user to set them to PROT_NONE and invert
 * them, then pointing to valid memory for L1TF speculation.
 *
 * Note: for locked down kernels may want to disable the root override.
 */
bool pfn_modify_allowed(unsigned long pfn, pgprot_t prot)
{
	if (!boot_cpu_has_bug(X86_BUG_L1TF))
		return true;
	if (!__pte_needs_invert(pgprot_val(prot)))
		return true;
	/* If it's real memory always allow */
	if (pfn_valid(pfn))
		return true;
	if (pfn >= l1tf_pfn_limit() && !capable(CAP_SYS_ADMIN))
		return false;
	return true;
}

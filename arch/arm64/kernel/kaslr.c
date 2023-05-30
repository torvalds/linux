// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2016 Linaro Ltd <ard.biesheuvel@linaro.org>
 */

#include <linux/cache.h>
#include <linux/crc32.h>
#include <linux/init.h>
#include <linux/libfdt.h>
#include <linux/mm_types.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/pgtable.h>
#include <linux/random.h>

#include <asm/fixmap.h>
#include <asm/kernel-pgtable.h>
#include <asm/memory.h>
#include <asm/mmu.h>
#include <asm/sections.h>
#include <asm/setup.h>

u64 __ro_after_init module_alloc_base;
u16 __initdata memstart_offset_seed;

struct arm64_ftr_override kaslr_feature_override __initdata;

bool __ro_after_init __kaslr_is_enabled = false;

void __init kaslr_init(void)
{
	if (kaslr_feature_override.val & kaslr_feature_override.mask & 0xf) {
		pr_info("KASLR disabled on command line\n");
		return;
	}

	/*
	 * The KASLR offset modulo MIN_KIMG_ALIGN is taken from the physical
	 * placement of the image rather than from the seed, so a displacement
	 * of less than MIN_KIMG_ALIGN means that no seed was provided.
	 */
	if (kaslr_offset() < MIN_KIMG_ALIGN) {
		pr_warn("KASLR disabled due to lack of seed\n");
		return;
	}

	pr_info("KASLR enabled\n");
	__kaslr_is_enabled = true;
}

int kaslr_module_init(void)
{
	u64 module_range;
	u32 seed;

	/*
	 * Set a reasonable default for module_alloc_base in case
	 * we end up running with module randomization disabled.
	 */
	module_alloc_base = (u64)_etext - MODULES_VSIZE;

	seed = get_random_u32();

	if (IS_ENABLED(CONFIG_RANDOMIZE_MODULE_REGION_FULL)) {
		/*
		 * Randomize the module region over a 2 GB window covering the
		 * kernel. This reduces the risk of modules leaking information
		 * about the address of the kernel itself, but results in
		 * branches between modules and the core kernel that are
		 * resolved via PLTs. (Branches between modules will be
		 * resolved normally.)
		 */
		module_range = SZ_2G - (u64)(_end - _stext);
		module_alloc_base = max((u64)_end - SZ_2G, (u64)MODULES_VADDR);
	} else {
		/*
		 * Randomize the module region by setting module_alloc_base to
		 * a PAGE_SIZE multiple in the range [_etext - MODULES_VSIZE,
		 * _stext) . This guarantees that the resulting region still
		 * covers [_stext, _etext], and that all relative branches can
		 * be resolved without veneers unless this region is exhausted
		 * and we fall back to a larger 2GB window in module_alloc()
		 * when ARM64_MODULE_PLTS is enabled.
		 */
		module_range = MODULES_VSIZE - (u64)(_etext - _stext);
	}

	/* use the lower 21 bits to randomize the base of the module region */
	module_alloc_base += (module_range * (seed & ((1 << 21) - 1))) >> 21;
	module_alloc_base &= PAGE_MASK;

	return 0;
}
subsys_initcall(kaslr_module_init)

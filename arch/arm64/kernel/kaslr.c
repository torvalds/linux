/*
 * Copyright (C) 2016 Linaro Ltd <ard.biesheuvel@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/cache.h>
#include <linux/crc32.h>
#include <linux/init.h>
#include <linux/libfdt.h>
#include <linux/mm_types.h>
#include <linux/sched.h>
#include <linux/types.h>

#include <asm/cacheflush.h>
#include <asm/fixmap.h>
#include <asm/kernel-pgtable.h>
#include <asm/memory.h>
#include <asm/mmu.h>
#include <asm/pgtable.h>
#include <asm/sections.h>

u64 __ro_after_init module_alloc_base;
u16 __initdata memstart_offset_seed;

static __init u64 get_kaslr_seed(void *fdt)
{
	int node, len;
	fdt64_t *prop;
	u64 ret;

	node = fdt_path_offset(fdt, "/chosen");
	if (node < 0)
		return 0;

	prop = fdt_getprop_w(fdt, node, "kaslr-seed", &len);
	if (!prop || len != sizeof(u64))
		return 0;

	ret = fdt64_to_cpu(*prop);
	*prop = 0;
	return ret;
}

static __init const u8 *kaslr_get_cmdline(void *fdt)
{
	static __initconst const u8 default_cmdline[] = CONFIG_CMDLINE;

	if (!IS_ENABLED(CONFIG_CMDLINE_FORCE)) {
		int node;
		const u8 *prop;

		node = fdt_path_offset(fdt, "/chosen");
		if (node < 0)
			goto out;

		prop = fdt_getprop(fdt, node, "bootargs", NULL);
		if (!prop)
			goto out;
		return prop;
	}
out:
	return default_cmdline;
}

extern void *__init __fixmap_remap_fdt(phys_addr_t dt_phys, int *size,
				       pgprot_t prot);

/*
 * This routine will be executed with the kernel mapped at its default virtual
 * address, and if it returns successfully, the kernel will be remapped, and
 * start_kernel() will be executed from a randomized virtual offset. The
 * relocation will result in all absolute references (e.g., static variables
 * containing function pointers) to be reinitialized, and zero-initialized
 * .bss variables will be reset to 0.
 */
u64 __init kaslr_early_init(u64 dt_phys)
{
	void *fdt;
	u64 seed, offset, mask, module_range;
	const u8 *cmdline, *str;
	int size;

	/*
	 * Set a reasonable default for module_alloc_base in case
	 * we end up running with module randomization disabled.
	 */
	module_alloc_base = (u64)_etext - MODULES_VSIZE;
	__flush_dcache_area(&module_alloc_base, sizeof(module_alloc_base));

	/*
	 * Try to map the FDT early. If this fails, we simply bail,
	 * and proceed with KASLR disabled. We will make another
	 * attempt at mapping the FDT in setup_machine()
	 */
	early_fixmap_init();
	fdt = __fixmap_remap_fdt(dt_phys, &size, PAGE_KERNEL);
	if (!fdt)
		return 0;

	/*
	 * Retrieve (and wipe) the seed from the FDT
	 */
	seed = get_kaslr_seed(fdt);
	if (!seed)
		return 0;

	/*
	 * Check if 'nokaslr' appears on the command line, and
	 * return 0 if that is the case.
	 */
	cmdline = kaslr_get_cmdline(fdt);
	str = strstr(cmdline, "nokaslr");
	if (str == cmdline || (str > cmdline && *(str - 1) == ' '))
		return 0;

	/*
	 * OK, so we are proceeding with KASLR enabled. Calculate a suitable
	 * kernel image offset from the seed. Let's place the kernel in the
	 * middle half of the VMALLOC area (VA_BITS - 2), and stay clear of
	 * the lower and upper quarters to avoid colliding with other
	 * allocations.
	 * Even if we could randomize at page granularity for 16k and 64k pages,
	 * let's always round to 2 MB so we don't interfere with the ability to
	 * map using contiguous PTEs
	 */
	mask = ((1UL << (VA_BITS - 2)) - 1) & ~(SZ_2M - 1);
	offset = BIT(VA_BITS - 3) + (seed & mask);

	/* use the top 16 bits to randomize the linear region */
	memstart_offset_seed = seed >> 48;

	if (IS_ENABLED(CONFIG_KASAN))
		/*
		 * KASAN does not expect the module region to intersect the
		 * vmalloc region, since shadow memory is allocated for each
		 * module at load time, whereas the vmalloc region is shadowed
		 * by KASAN zero pages. So keep modules out of the vmalloc
		 * region if KASAN is enabled, and put the kernel well within
		 * 4 GB of the module region.
		 */
		return offset % SZ_2G;

	if (IS_ENABLED(CONFIG_RANDOMIZE_MODULE_REGION_FULL)) {
		/*
		 * Randomize the module region over a 4 GB window covering the
		 * kernel. This reduces the risk of modules leaking information
		 * about the address of the kernel itself, but results in
		 * branches between modules and the core kernel that are
		 * resolved via PLTs. (Branches between modules will be
		 * resolved normally.)
		 */
		module_range = SZ_4G - (u64)(_end - _stext);
		module_alloc_base = max((u64)_end + offset - SZ_4G,
					(u64)MODULES_VADDR);
	} else {
		/*
		 * Randomize the module region by setting module_alloc_base to
		 * a PAGE_SIZE multiple in the range [_etext - MODULES_VSIZE,
		 * _stext) . This guarantees that the resulting region still
		 * covers [_stext, _etext], and that all relative branches can
		 * be resolved without veneers.
		 */
		module_range = MODULES_VSIZE - (u64)(_etext - _stext);
		module_alloc_base = (u64)_etext + offset - MODULES_VSIZE;
	}

	/* use the lower 21 bits to randomize the base of the module region */
	module_alloc_base += (module_range * (seed & ((1 << 21) - 1))) >> 21;
	module_alloc_base &= PAGE_MASK;

	__flush_dcache_area(&module_alloc_base, sizeof(module_alloc_base));
	__flush_dcache_area(&memstart_offset_seed, sizeof(memstart_offset_seed));

	return offset;
}

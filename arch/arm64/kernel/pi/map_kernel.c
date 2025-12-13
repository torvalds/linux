// SPDX-License-Identifier: GPL-2.0-only
// Copyright 2023 Google LLC
// Author: Ard Biesheuvel <ardb@google.com>

#include <linux/init.h>
#include <linux/libfdt.h>
#include <linux/linkage.h>
#include <linux/types.h>
#include <linux/sizes.h>
#include <linux/string.h>

#include <asm/memory.h>
#include <asm/pgalloc.h>
#include <asm/pgtable.h>
#include <asm/tlbflush.h>

#include "pi.h"

extern const u8 __eh_frame_start[], __eh_frame_end[];

extern void idmap_cpu_replace_ttbr1(phys_addr_t pgdir);

static void __init map_segment(pgd_t *pg_dir, phys_addr_t *pgd, u64 va_offset,
			       void *start, void *end, pgprot_t prot,
			       bool may_use_cont, int root_level)
{
	map_range(pgd, ((u64)start + va_offset) & ~PAGE_OFFSET,
		  ((u64)end + va_offset) & ~PAGE_OFFSET, (u64)start,
		  prot, root_level, (pte_t *)pg_dir, may_use_cont, 0);
}

static void __init unmap_segment(pgd_t *pg_dir, u64 va_offset, void *start,
				 void *end, int root_level)
{
	map_segment(pg_dir, NULL, va_offset, start, end, __pgprot(0),
		    false, root_level);
}

static void __init map_kernel(u64 kaslr_offset, u64 va_offset, int root_level)
{
	bool enable_scs = IS_ENABLED(CONFIG_UNWIND_PATCH_PAC_INTO_SCS);
	bool twopass = IS_ENABLED(CONFIG_RELOCATABLE);
	phys_addr_t pgdp = (phys_addr_t)init_pg_dir + PAGE_SIZE;
	pgprot_t text_prot = PAGE_KERNEL_ROX;
	pgprot_t data_prot = PAGE_KERNEL;
	pgprot_t prot;

	/*
	 * External debuggers may need to write directly to the text mapping to
	 * install SW breakpoints. Allow this (only) when explicitly requested
	 * with rodata=off.
	 */
	if (arm64_test_sw_feature_override(ARM64_SW_FEATURE_OVERRIDE_RODATA_OFF))
		text_prot = PAGE_KERNEL_EXEC;

	/*
	 * We only enable the shadow call stack dynamically if we are running
	 * on a system that does not implement PAC or BTI. PAC and SCS provide
	 * roughly the same level of protection, and BTI relies on the PACIASP
	 * instructions serving as landing pads, preventing us from patching
	 * those instructions into something else.
	 */
	if (IS_ENABLED(CONFIG_ARM64_PTR_AUTH_KERNEL) && cpu_has_pac())
		enable_scs = false;

	if (IS_ENABLED(CONFIG_ARM64_BTI_KERNEL) && cpu_has_bti()) {
		enable_scs = false;

		/*
		 * If we have a CPU that supports BTI and a kernel built for
		 * BTI then mark the kernel executable text as guarded pages
		 * now so we don't have to rewrite the page tables later.
		 */
		text_prot = __pgprot_modify(text_prot, PTE_GP, PTE_GP);
	}

	/* Map all code read-write on the first pass if needed */
	twopass |= enable_scs;
	prot = twopass ? data_prot : text_prot;

	/*
	 * [_stext, _text) isn't executed after boot and contains some
	 * non-executable, unpredictable data, so map it non-executable.
	 */
	map_segment(init_pg_dir, &pgdp, va_offset, _text, _stext, data_prot,
		    false, root_level);
	map_segment(init_pg_dir, &pgdp, va_offset, _stext, _etext, prot,
		    !twopass, root_level);
	map_segment(init_pg_dir, &pgdp, va_offset, __start_rodata,
		    __inittext_begin, data_prot, false, root_level);
	map_segment(init_pg_dir, &pgdp, va_offset, __inittext_begin,
		    __inittext_end, prot, false, root_level);
	map_segment(init_pg_dir, &pgdp, va_offset, __initdata_begin,
		    __initdata_end, data_prot, false, root_level);
	map_segment(init_pg_dir, &pgdp, va_offset, _data, _end, data_prot,
		    true, root_level);
	dsb(ishst);

	idmap_cpu_replace_ttbr1((phys_addr_t)init_pg_dir);

	if (twopass) {
		if (IS_ENABLED(CONFIG_RELOCATABLE))
			relocate_kernel(kaslr_offset);

		if (enable_scs) {
			scs_patch(__eh_frame_start + va_offset,
				  __eh_frame_end - __eh_frame_start, false);
			asm("ic ialluis");

			dynamic_scs_is_enabled = true;
		}

		/*
		 * Unmap the text region before remapping it, to avoid
		 * potential TLB conflicts when creating the contiguous
		 * descriptors.
		 */
		unmap_segment(init_pg_dir, va_offset, _stext, _etext,
			      root_level);
		dsb(ishst);
		isb();
		__tlbi(vmalle1);
		isb();

		/*
		 * Remap these segments with different permissions
		 * No new page table allocations should be needed
		 */
		map_segment(init_pg_dir, NULL, va_offset, _stext, _etext,
			    text_prot, true, root_level);
		map_segment(init_pg_dir, NULL, va_offset, __inittext_begin,
			    __inittext_end, text_prot, false, root_level);
	}

	/* Copy the root page table to its final location */
	memcpy((void *)swapper_pg_dir + va_offset, init_pg_dir, PAGE_SIZE);
	dsb(ishst);
	idmap_cpu_replace_ttbr1((phys_addr_t)swapper_pg_dir);
}

static void noinline __section(".idmap.text") set_ttbr0_for_lpa2(phys_addr_t ttbr)
{
	u64 sctlr = read_sysreg(sctlr_el1);
	u64 tcr = read_sysreg(tcr_el1) | TCR_DS;
	u64 mmfr0 = read_sysreg(id_aa64mmfr0_el1);
	u64 parange = cpuid_feature_extract_unsigned_field(mmfr0,
							   ID_AA64MMFR0_EL1_PARANGE_SHIFT);

	tcr &= ~TCR_IPS_MASK;
	tcr |= parange << TCR_IPS_SHIFT;

	asm("	msr	sctlr_el1, %0		;"
	    "	isb				;"
	    "   msr     ttbr0_el1, %1		;"
	    "   msr     tcr_el1, %2		;"
	    "	isb				;"
	    "	tlbi    vmalle1			;"
	    "	dsb     nsh			;"
	    "	isb				;"
	    "	msr     sctlr_el1, %3		;"
	    "	isb				;"
	    ::	"r"(sctlr & ~SCTLR_ELx_M), "r"(ttbr), "r"(tcr), "r"(sctlr));
}

static void __init remap_idmap_for_lpa2(void)
{
	/* clear the bits that change meaning once LPA2 is turned on */
	ptdesc_t mask = PTE_SHARED;

	/*
	 * We have to clear bits [9:8] in all block or page descriptors in the
	 * initial ID map, as otherwise they will be (mis)interpreted as
	 * physical address bits once we flick the LPA2 switch (TCR.DS). Since
	 * we cannot manipulate live descriptors in that way without creating
	 * potential TLB conflicts, let's create another temporary ID map in a
	 * LPA2 compatible fashion, and update the initial ID map while running
	 * from that.
	 */
	create_init_idmap(init_pg_dir, mask);
	dsb(ishst);
	set_ttbr0_for_lpa2((phys_addr_t)init_pg_dir);

	/*
	 * Recreate the initial ID map with the same granularity as before.
	 * Don't bother with the FDT, we no longer need it after this.
	 */
	memset(init_idmap_pg_dir, 0,
	       (char *)init_idmap_pg_end - (char *)init_idmap_pg_dir);

	create_init_idmap(init_idmap_pg_dir, mask);
	dsb(ishst);

	/* switch back to the updated initial ID map */
	set_ttbr0_for_lpa2((phys_addr_t)init_idmap_pg_dir);

	/* wipe the temporary ID map from memory */
	memset(init_pg_dir, 0, (char *)init_pg_end - (char *)init_pg_dir);
}

static void *__init map_fdt(phys_addr_t fdt)
{
	static u8 ptes[INIT_IDMAP_FDT_SIZE] __initdata __aligned(PAGE_SIZE);
	phys_addr_t efdt = fdt + MAX_FDT_SIZE;
	phys_addr_t ptep = (phys_addr_t)ptes; /* We're idmapped when called */

	/*
	 * Map up to MAX_FDT_SIZE bytes, but avoid overlap with
	 * the kernel image.
	 */
	map_range(&ptep, fdt, (u64)_text > fdt ? min((u64)_text, efdt) : efdt,
		  fdt, PAGE_KERNEL, IDMAP_ROOT_LEVEL,
		  (pte_t *)init_idmap_pg_dir, false, 0);
	dsb(ishst);

	return (void *)fdt;
}

/*
 * PI version of the Cavium Eratum 27456 detection, which makes it
 * impossible to use non-global mappings.
 */
static bool __init ng_mappings_allowed(void)
{
	static const struct midr_range cavium_erratum_27456_cpus[] __initconst = {
		/* Cavium ThunderX, T88 pass 1.x - 2.1 */
		MIDR_RANGE(MIDR_THUNDERX, 0, 0, 1, 1),
		/* Cavium ThunderX, T81 pass 1.0 */
		MIDR_REV(MIDR_THUNDERX_81XX, 0, 0),
		{},
	};

	for (const struct midr_range *r = cavium_erratum_27456_cpus; r->model; r++) {
		if (midr_is_cpu_model_range(read_cpuid_id(), r->model,
					    r->rv_min, r->rv_max))
			return false;
	}

	return true;
}

asmlinkage void __init early_map_kernel(u64 boot_status, phys_addr_t fdt)
{
	static char const chosen_str[] __initconst = "/chosen";
	u64 va_base, pa_base = (u64)&_text;
	u64 kaslr_offset = pa_base % MIN_KIMG_ALIGN;
	int root_level = 4 - CONFIG_PGTABLE_LEVELS;
	int va_bits = VA_BITS;
	int chosen;
	void *fdt_mapped = map_fdt(fdt);

	/* Clear BSS and the initial page tables */
	memset(__bss_start, 0, (char *)init_pg_end - (char *)__bss_start);

	/* Parse the command line for CPU feature overrides */
	chosen = fdt_path_offset(fdt_mapped, chosen_str);
	init_feature_override(boot_status, fdt_mapped, chosen);

	if (IS_ENABLED(CONFIG_ARM64_64K_PAGES) && !cpu_has_lva()) {
		va_bits = VA_BITS_MIN;
	} else if (IS_ENABLED(CONFIG_ARM64_LPA2) && !cpu_has_lpa2()) {
		va_bits = VA_BITS_MIN;
		root_level++;
	}

	if (va_bits > VA_BITS_MIN)
		sysreg_clear_set(tcr_el1, TCR_T1SZ_MASK, TCR_T1SZ(va_bits));

	/*
	 * The virtual KASLR displacement modulo 2MiB is decided by the
	 * physical placement of the image, as otherwise, we might not be able
	 * to create the early kernel mapping using 2 MiB block descriptors. So
	 * take the low bits of the KASLR offset from the physical address, and
	 * fill in the high bits from the seed.
	 */
	if (IS_ENABLED(CONFIG_RANDOMIZE_BASE)) {
		u64 kaslr_seed = kaslr_early_init(fdt_mapped, chosen);

		if (kaslr_seed && kaslr_requires_kpti())
			arm64_use_ng_mappings = ng_mappings_allowed();

		kaslr_offset |= kaslr_seed & ~(MIN_KIMG_ALIGN - 1);
	}

	if (IS_ENABLED(CONFIG_ARM64_LPA2) && va_bits > VA_BITS_MIN)
		remap_idmap_for_lpa2();

	va_base = KIMAGE_VADDR + kaslr_offset;
	map_kernel(kaslr_offset, va_base - pa_base, root_level);
}

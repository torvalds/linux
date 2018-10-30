// SPDX-License-Identifier: GPL-2.0
/*
 *  prepare to run common code
 *
 *  Copyright (C) 2000 Andrea Arcangeli <andrea@suse.de> SuSE
 */

#define DISABLE_BRANCH_PROFILING

/* cpu_feature_enabled() cannot be used this early */
#define USE_EARLY_PGTABLE_L5

#include <linux/init.h>
#include <linux/linkage.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/percpu.h>
#include <linux/start_kernel.h>
#include <linux/io.h>
#include <linux/memblock.h>
#include <linux/mem_encrypt.h>

#include <asm/processor.h>
#include <asm/proto.h>
#include <asm/smp.h>
#include <asm/setup.h>
#include <asm/desc.h>
#include <asm/pgtable.h>
#include <asm/tlbflush.h>
#include <asm/sections.h>
#include <asm/kdebug.h>
#include <asm/e820/api.h>
#include <asm/bios_ebda.h>
#include <asm/bootparam_utils.h>
#include <asm/microcode.h>
#include <asm/kasan.h>
#include <asm/fixmap.h>

/*
 * Manage page tables very early on.
 */
extern pmd_t early_dynamic_pgts[EARLY_DYNAMIC_PAGE_TABLES][PTRS_PER_PMD];
static unsigned int __initdata next_early_pgt;
pmdval_t early_pmd_flags = __PAGE_KERNEL_LARGE & ~(_PAGE_GLOBAL | _PAGE_NX);

#ifdef CONFIG_X86_5LEVEL
unsigned int __pgtable_l5_enabled __ro_after_init;
unsigned int pgdir_shift __ro_after_init = 39;
EXPORT_SYMBOL(pgdir_shift);
unsigned int ptrs_per_p4d __ro_after_init = 1;
EXPORT_SYMBOL(ptrs_per_p4d);
#endif

#ifdef CONFIG_DYNAMIC_MEMORY_LAYOUT
unsigned long page_offset_base __ro_after_init = __PAGE_OFFSET_BASE_L4;
EXPORT_SYMBOL(page_offset_base);
unsigned long vmalloc_base __ro_after_init = __VMALLOC_BASE_L4;
EXPORT_SYMBOL(vmalloc_base);
unsigned long vmemmap_base __ro_after_init = __VMEMMAP_BASE_L4;
EXPORT_SYMBOL(vmemmap_base);
#endif

#define __head	__section(.head.text)

static void __head *fixup_pointer(void *ptr, unsigned long physaddr)
{
	return ptr - (void *)_text + (void *)physaddr;
}

static unsigned long __head *fixup_long(void *ptr, unsigned long physaddr)
{
	return fixup_pointer(ptr, physaddr);
}

#ifdef CONFIG_X86_5LEVEL
static unsigned int __head *fixup_int(void *ptr, unsigned long physaddr)
{
	return fixup_pointer(ptr, physaddr);
}

static bool __head check_la57_support(unsigned long physaddr)
{
	/*
	 * 5-level paging is detected and enabled at kernel decomression
	 * stage. Only check if it has been enabled there.
	 */
	if (!(native_read_cr4() & X86_CR4_LA57))
		return false;

	*fixup_int(&__pgtable_l5_enabled, physaddr) = 1;
	*fixup_int(&pgdir_shift, physaddr) = 48;
	*fixup_int(&ptrs_per_p4d, physaddr) = 512;
	*fixup_long(&page_offset_base, physaddr) = __PAGE_OFFSET_BASE_L5;
	*fixup_long(&vmalloc_base, physaddr) = __VMALLOC_BASE_L5;
	*fixup_long(&vmemmap_base, physaddr) = __VMEMMAP_BASE_L5;

	return true;
}
#else
static bool __head check_la57_support(unsigned long physaddr)
{
	return false;
}
#endif

/* Code in __startup_64() can be relocated during execution, but the compiler
 * doesn't have to generate PC-relative relocations when accessing globals from
 * that function. Clang actually does not generate them, which leads to
 * boot-time crashes. To work around this problem, every global pointer must
 * be adjusted using fixup_pointer().
 */
unsigned long __head __startup_64(unsigned long physaddr,
				  struct boot_params *bp)
{
	unsigned long vaddr, vaddr_end;
	unsigned long load_delta, *p;
	unsigned long pgtable_flags;
	pgdval_t *pgd;
	p4dval_t *p4d;
	pudval_t *pud;
	pmdval_t *pmd, pmd_entry;
	pteval_t *mask_ptr;
	bool la57;
	int i;
	unsigned int *next_pgt_ptr;

	la57 = check_la57_support(physaddr);

	/* Is the address too large? */
	if (physaddr >> MAX_PHYSMEM_BITS)
		for (;;);

	/*
	 * Compute the delta between the address I am compiled to run at
	 * and the address I am actually running at.
	 */
	load_delta = physaddr - (unsigned long)(_text - __START_KERNEL_map);

	/* Is the address not 2M aligned? */
	if (load_delta & ~PMD_PAGE_MASK)
		for (;;);

	/* Activate Secure Memory Encryption (SME) if supported and enabled */
	sme_enable(bp);

	/* Include the SME encryption mask in the fixup value */
	load_delta += sme_get_me_mask();

	/* Fixup the physical addresses in the page table */

	pgd = fixup_pointer(&early_top_pgt, physaddr);
	p = pgd + pgd_index(__START_KERNEL_map);
	if (la57)
		*p = (unsigned long)level4_kernel_pgt;
	else
		*p = (unsigned long)level3_kernel_pgt;
	*p += _PAGE_TABLE_NOENC - __START_KERNEL_map + load_delta;

	if (la57) {
		p4d = fixup_pointer(&level4_kernel_pgt, physaddr);
		p4d[511] += load_delta;
	}

	pud = fixup_pointer(&level3_kernel_pgt, physaddr);
	pud[510] += load_delta;
	pud[511] += load_delta;

	pmd = fixup_pointer(level2_fixmap_pgt, physaddr);
	for (i = FIXMAP_PMD_TOP; i > FIXMAP_PMD_TOP - FIXMAP_PMD_NUM; i--)
		pmd[i] += load_delta;

	/*
	 * Set up the identity mapping for the switchover.  These
	 * entries should *NOT* have the global bit set!  This also
	 * creates a bunch of nonsense entries but that is fine --
	 * it avoids problems around wraparound.
	 */

	next_pgt_ptr = fixup_pointer(&next_early_pgt, physaddr);
	pud = fixup_pointer(early_dynamic_pgts[(*next_pgt_ptr)++], physaddr);
	pmd = fixup_pointer(early_dynamic_pgts[(*next_pgt_ptr)++], physaddr);

	pgtable_flags = _KERNPG_TABLE_NOENC + sme_get_me_mask();

	if (la57) {
		p4d = fixup_pointer(early_dynamic_pgts[next_early_pgt++], physaddr);

		i = (physaddr >> PGDIR_SHIFT) % PTRS_PER_PGD;
		pgd[i + 0] = (pgdval_t)p4d + pgtable_flags;
		pgd[i + 1] = (pgdval_t)p4d + pgtable_flags;

		i = (physaddr >> P4D_SHIFT) % PTRS_PER_P4D;
		p4d[i + 0] = (pgdval_t)pud + pgtable_flags;
		p4d[i + 1] = (pgdval_t)pud + pgtable_flags;
	} else {
		i = (physaddr >> PGDIR_SHIFT) % PTRS_PER_PGD;
		pgd[i + 0] = (pgdval_t)pud + pgtable_flags;
		pgd[i + 1] = (pgdval_t)pud + pgtable_flags;
	}

	i = (physaddr >> PUD_SHIFT) % PTRS_PER_PUD;
	pud[i + 0] = (pudval_t)pmd + pgtable_flags;
	pud[i + 1] = (pudval_t)pmd + pgtable_flags;

	pmd_entry = __PAGE_KERNEL_LARGE_EXEC & ~_PAGE_GLOBAL;
	/* Filter out unsupported __PAGE_KERNEL_* bits: */
	mask_ptr = fixup_pointer(&__supported_pte_mask, physaddr);
	pmd_entry &= *mask_ptr;
	pmd_entry += sme_get_me_mask();
	pmd_entry +=  physaddr;

	for (i = 0; i < DIV_ROUND_UP(_end - _text, PMD_SIZE); i++) {
		int idx = i + (physaddr >> PMD_SHIFT) % PTRS_PER_PMD;
		pmd[idx] = pmd_entry + i * PMD_SIZE;
	}

	/*
	 * Fixup the kernel text+data virtual addresses. Note that
	 * we might write invalid pmds, when the kernel is relocated
	 * cleanup_highmap() fixes this up along with the mappings
	 * beyond _end.
	 */

	pmd = fixup_pointer(level2_kernel_pgt, physaddr);
	for (i = 0; i < PTRS_PER_PMD; i++) {
		if (pmd[i] & _PAGE_PRESENT)
			pmd[i] += load_delta;
	}

	/*
	 * Fixup phys_base - remove the memory encryption mask to obtain
	 * the true physical address.
	 */
	*fixup_long(&phys_base, physaddr) += load_delta - sme_get_me_mask();

	/* Encrypt the kernel and related (if SME is active) */
	sme_encrypt_kernel(bp);

	/*
	 * Clear the memory encryption mask from the .bss..decrypted section.
	 * The bss section will be memset to zero later in the initialization so
	 * there is no need to zero it after changing the memory encryption
	 * attribute.
	 */
	if (mem_encrypt_active()) {
		vaddr = (unsigned long)__start_bss_decrypted;
		vaddr_end = (unsigned long)__end_bss_decrypted;
		for (; vaddr < vaddr_end; vaddr += PMD_SIZE) {
			i = pmd_index(vaddr);
			pmd[i] -= sme_get_me_mask();
		}
	}

	/*
	 * Return the SME encryption mask (if SME is active) to be used as a
	 * modifier for the initial pgdir entry programmed into CR3.
	 */
	return sme_get_me_mask();
}

unsigned long __startup_secondary_64(void)
{
	/*
	 * Return the SME encryption mask (if SME is active) to be used as a
	 * modifier for the initial pgdir entry programmed into CR3.
	 */
	return sme_get_me_mask();
}

/* Wipe all early page tables except for the kernel symbol map */
static void __init reset_early_page_tables(void)
{
	memset(early_top_pgt, 0, sizeof(pgd_t)*(PTRS_PER_PGD-1));
	next_early_pgt = 0;
	write_cr3(__sme_pa_nodebug(early_top_pgt));
}

/* Create a new PMD entry */
int __init __early_make_pgtable(unsigned long address, pmdval_t pmd)
{
	unsigned long physaddr = address - __PAGE_OFFSET;
	pgdval_t pgd, *pgd_p;
	p4dval_t p4d, *p4d_p;
	pudval_t pud, *pud_p;
	pmdval_t *pmd_p;

	/* Invalid address or early pgt is done ?  */
	if (physaddr >= MAXMEM || read_cr3_pa() != __pa_nodebug(early_top_pgt))
		return -1;

again:
	pgd_p = &early_top_pgt[pgd_index(address)].pgd;
	pgd = *pgd_p;

	/*
	 * The use of __START_KERNEL_map rather than __PAGE_OFFSET here is
	 * critical -- __PAGE_OFFSET would point us back into the dynamic
	 * range and we might end up looping forever...
	 */
	if (!pgtable_l5_enabled())
		p4d_p = pgd_p;
	else if (pgd)
		p4d_p = (p4dval_t *)((pgd & PTE_PFN_MASK) + __START_KERNEL_map - phys_base);
	else {
		if (next_early_pgt >= EARLY_DYNAMIC_PAGE_TABLES) {
			reset_early_page_tables();
			goto again;
		}

		p4d_p = (p4dval_t *)early_dynamic_pgts[next_early_pgt++];
		memset(p4d_p, 0, sizeof(*p4d_p) * PTRS_PER_P4D);
		*pgd_p = (pgdval_t)p4d_p - __START_KERNEL_map + phys_base + _KERNPG_TABLE;
	}
	p4d_p += p4d_index(address);
	p4d = *p4d_p;

	if (p4d)
		pud_p = (pudval_t *)((p4d & PTE_PFN_MASK) + __START_KERNEL_map - phys_base);
	else {
		if (next_early_pgt >= EARLY_DYNAMIC_PAGE_TABLES) {
			reset_early_page_tables();
			goto again;
		}

		pud_p = (pudval_t *)early_dynamic_pgts[next_early_pgt++];
		memset(pud_p, 0, sizeof(*pud_p) * PTRS_PER_PUD);
		*p4d_p = (p4dval_t)pud_p - __START_KERNEL_map + phys_base + _KERNPG_TABLE;
	}
	pud_p += pud_index(address);
	pud = *pud_p;

	if (pud)
		pmd_p = (pmdval_t *)((pud & PTE_PFN_MASK) + __START_KERNEL_map - phys_base);
	else {
		if (next_early_pgt >= EARLY_DYNAMIC_PAGE_TABLES) {
			reset_early_page_tables();
			goto again;
		}

		pmd_p = (pmdval_t *)early_dynamic_pgts[next_early_pgt++];
		memset(pmd_p, 0, sizeof(*pmd_p) * PTRS_PER_PMD);
		*pud_p = (pudval_t)pmd_p - __START_KERNEL_map + phys_base + _KERNPG_TABLE;
	}
	pmd_p[pmd_index(address)] = pmd;

	return 0;
}

int __init early_make_pgtable(unsigned long address)
{
	unsigned long physaddr = address - __PAGE_OFFSET;
	pmdval_t pmd;

	pmd = (physaddr & PMD_MASK) + early_pmd_flags;

	return __early_make_pgtable(address, pmd);
}

/* Don't add a printk in there. printk relies on the PDA which is not initialized 
   yet. */
static void __init clear_bss(void)
{
	memset(__bss_start, 0,
	       (unsigned long) __bss_stop - (unsigned long) __bss_start);
}

static unsigned long get_cmd_line_ptr(void)
{
	unsigned long cmd_line_ptr = boot_params.hdr.cmd_line_ptr;

	cmd_line_ptr |= (u64)boot_params.ext_cmd_line_ptr << 32;

	return cmd_line_ptr;
}

static void __init copy_bootdata(char *real_mode_data)
{
	char * command_line;
	unsigned long cmd_line_ptr;

	/*
	 * If SME is active, this will create decrypted mappings of the
	 * boot data in advance of the copy operations.
	 */
	sme_map_bootdata(real_mode_data);

	memcpy(&boot_params, real_mode_data, sizeof boot_params);
	sanitize_boot_params(&boot_params);
	cmd_line_ptr = get_cmd_line_ptr();
	if (cmd_line_ptr) {
		command_line = __va(cmd_line_ptr);
		memcpy(boot_command_line, command_line, COMMAND_LINE_SIZE);
	}

	/*
	 * The old boot data is no longer needed and won't be reserved,
	 * freeing up that memory for use by the system. If SME is active,
	 * we need to remove the mappings that were created so that the
	 * memory doesn't remain mapped as decrypted.
	 */
	sme_unmap_bootdata(real_mode_data);
}

asmlinkage __visible void __init x86_64_start_kernel(char * real_mode_data)
{
	/*
	 * Build-time sanity checks on the kernel image and module
	 * area mappings. (these are purely build-time and produce no code)
	 */
	BUILD_BUG_ON(MODULES_VADDR < __START_KERNEL_map);
	BUILD_BUG_ON(MODULES_VADDR - __START_KERNEL_map < KERNEL_IMAGE_SIZE);
	BUILD_BUG_ON(MODULES_LEN + KERNEL_IMAGE_SIZE > 2*PUD_SIZE);
	BUILD_BUG_ON((__START_KERNEL_map & ~PMD_MASK) != 0);
	BUILD_BUG_ON((MODULES_VADDR & ~PMD_MASK) != 0);
	BUILD_BUG_ON(!(MODULES_VADDR > __START_KERNEL));
	MAYBE_BUILD_BUG_ON(!(((MODULES_END - 1) & PGDIR_MASK) ==
				(__START_KERNEL & PGDIR_MASK)));
	BUILD_BUG_ON(__fix_to_virt(__end_of_fixed_addresses) <= MODULES_END);

	cr4_init_shadow();

	/* Kill off the identity-map trampoline */
	reset_early_page_tables();

	clear_bss();

	clear_page(init_top_pgt);

	/*
	 * SME support may update early_pmd_flags to include the memory
	 * encryption mask, so it needs to be called before anything
	 * that may generate a page fault.
	 */
	sme_early_init();

	kasan_early_init();

	idt_setup_early_handler();

	copy_bootdata(__va(real_mode_data));

	/*
	 * Load microcode early on BSP.
	 */
	load_ucode_bsp();

	/* set init_top_pgt kernel high mapping*/
	init_top_pgt[511] = early_top_pgt[511];

	x86_64_start_reservations(real_mode_data);
}

void __init x86_64_start_reservations(char *real_mode_data)
{
	/* version is always not zero if it is copied */
	if (!boot_params.hdr.version)
		copy_bootdata(__va(real_mode_data));

	x86_verify_bootdata_version();

	x86_early_init_platform_quirks();

	switch (boot_params.hdr.hardware_subarch) {
	case X86_SUBARCH_INTEL_MID:
		x86_intel_mid_early_setup();
		break;
	default:
		break;
	}

	start_kernel();
}

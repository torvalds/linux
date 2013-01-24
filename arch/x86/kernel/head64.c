/*
 *  prepare to run common code
 *
 *  Copyright (C) 2000 Andrea Arcangeli <andrea@suse.de> SuSE
 */

#include <linux/init.h>
#include <linux/linkage.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/percpu.h>
#include <linux/start_kernel.h>
#include <linux/io.h>
#include <linux/memblock.h>

#include <asm/processor.h>
#include <asm/proto.h>
#include <asm/smp.h>
#include <asm/setup.h>
#include <asm/desc.h>
#include <asm/pgtable.h>
#include <asm/tlbflush.h>
#include <asm/sections.h>
#include <asm/kdebug.h>
#include <asm/e820.h>
#include <asm/bios_ebda.h>
#include <asm/bootparam_utils.h>

/*
 * Manage page tables very early on.
 */
extern pgd_t early_level4_pgt[PTRS_PER_PGD];
extern pmd_t early_dynamic_pgts[EARLY_DYNAMIC_PAGE_TABLES][PTRS_PER_PMD];
static unsigned int __initdata next_early_pgt = 2;

/* Wipe all early page tables except for the kernel symbol map */
static void __init reset_early_page_tables(void)
{
	unsigned long i;

	for (i = 0; i < PTRS_PER_PGD-1; i++)
		early_level4_pgt[i].pgd = 0;

	next_early_pgt = 0;

	write_cr3(__pa(early_level4_pgt));
}

/* Create a new PMD entry */
int __init early_make_pgtable(unsigned long address)
{
	unsigned long physaddr = address - __PAGE_OFFSET;
	unsigned long i;
	pgdval_t pgd, *pgd_p;
	pudval_t *pud_p;
	pmdval_t pmd, *pmd_p;

	/* Invalid address or early pgt is done ?  */
	if (physaddr >= MAXMEM || read_cr3() != __pa(early_level4_pgt))
		return -1;

	i = (address >> PGDIR_SHIFT) & (PTRS_PER_PGD - 1);
	pgd_p = &early_level4_pgt[i].pgd;
	pgd = *pgd_p;

	/*
	 * The use of __START_KERNEL_map rather than __PAGE_OFFSET here is
	 * critical -- __PAGE_OFFSET would point us back into the dynamic
	 * range and we might end up looping forever...
	 */
	if (pgd && next_early_pgt < EARLY_DYNAMIC_PAGE_TABLES) {
		pud_p = (pudval_t *)((pgd & PTE_PFN_MASK) + __START_KERNEL_map - phys_base);
	} else {
		if (next_early_pgt >= EARLY_DYNAMIC_PAGE_TABLES-1)
			reset_early_page_tables();

		pud_p = (pudval_t *)early_dynamic_pgts[next_early_pgt++];
		for (i = 0; i < PTRS_PER_PUD; i++)
			pud_p[i] = 0;

		*pgd_p = (pgdval_t)pud_p - __START_KERNEL_map + phys_base + _KERNPG_TABLE;
	}
	i = (address >> PUD_SHIFT) & (PTRS_PER_PUD - 1);
	pud_p += i;

	pmd_p = (pmdval_t *)early_dynamic_pgts[next_early_pgt++];
	pmd = (physaddr & PUD_MASK) + (__PAGE_KERNEL_LARGE & ~_PAGE_GLOBAL);
	for (i = 0; i < PTRS_PER_PMD; i++) {
		pmd_p[i] = pmd;
		pmd += PMD_SIZE;
	}

	*pud_p = (pudval_t)pmd_p - __START_KERNEL_map + phys_base + _KERNPG_TABLE;

	return 0;
}

/* Don't add a printk in there. printk relies on the PDA which is not initialized 
   yet. */
static void __init clear_bss(void)
{
	memset(__bss_start, 0,
	       (unsigned long) __bss_stop - (unsigned long) __bss_start);
}

static void __init copy_bootdata(char *real_mode_data)
{
	char * command_line;

	memcpy(&boot_params, real_mode_data, sizeof boot_params);
	sanitize_boot_params(&boot_params);
	if (boot_params.hdr.cmd_line_ptr) {
		command_line = __va(boot_params.hdr.cmd_line_ptr);
		memcpy(boot_command_line, command_line, COMMAND_LINE_SIZE);
	}
}

void __init x86_64_start_kernel(char * real_mode_data)
{
	int i;

	/*
	 * Build-time sanity checks on the kernel image and module
	 * area mappings. (these are purely build-time and produce no code)
	 */
	BUILD_BUG_ON(MODULES_VADDR < KERNEL_IMAGE_START);
	BUILD_BUG_ON(MODULES_VADDR-KERNEL_IMAGE_START < KERNEL_IMAGE_SIZE);
	BUILD_BUG_ON(MODULES_LEN + KERNEL_IMAGE_SIZE > 2*PUD_SIZE);
	BUILD_BUG_ON((KERNEL_IMAGE_START & ~PMD_MASK) != 0);
	BUILD_BUG_ON((MODULES_VADDR & ~PMD_MASK) != 0);
	BUILD_BUG_ON(!(MODULES_VADDR > __START_KERNEL));
	BUILD_BUG_ON(!(((MODULES_END - 1) & PGDIR_MASK) ==
				(__START_KERNEL & PGDIR_MASK)));
	BUILD_BUG_ON(__fix_to_virt(__end_of_fixed_addresses) <= MODULES_END);

	/* Kill off the identity-map trampoline */
	reset_early_page_tables();

	/* clear bss before set_intr_gate with early_idt_handler */
	clear_bss();

	/* XXX - this is wrong... we need to build page tables from scratch */
	max_pfn_mapped = KERNEL_IMAGE_SIZE >> PAGE_SHIFT;

	for (i = 0; i < NUM_EXCEPTION_VECTORS; i++) {
#ifdef CONFIG_EARLY_PRINTK
		set_intr_gate(i, &early_idt_handlers[i]);
#else
		set_intr_gate(i, early_idt_handler);
#endif
	}
	load_idt((const struct desc_ptr *)&idt_descr);

	copy_bootdata(__va(real_mode_data));

	if (console_loglevel == 10)
		early_printk("Kernel alive\n");

	clear_page(init_level4_pgt);
	/* set init_level4_pgt kernel high mapping*/
	init_level4_pgt[511] = early_level4_pgt[511];

	x86_64_start_reservations(real_mode_data);
}

void __init x86_64_start_reservations(char *real_mode_data)
{
	/* version is always not zero if it is copied */
	if (!boot_params.hdr.version)
		copy_bootdata(__va(real_mode_data));

	memblock_reserve(__pa_symbol(&_text),
			 __pa_symbol(&__bss_stop) - __pa_symbol(&_text));

#ifdef CONFIG_BLK_DEV_INITRD
	/* Reserve INITRD */
	if (boot_params.hdr.type_of_loader && boot_params.hdr.ramdisk_image) {
		/* Assume only end is not page aligned */
		unsigned long ramdisk_image = boot_params.hdr.ramdisk_image;
		unsigned long ramdisk_size  = boot_params.hdr.ramdisk_size;
		unsigned long ramdisk_end   = PAGE_ALIGN(ramdisk_image + ramdisk_size);
		memblock_reserve(ramdisk_image, ramdisk_end - ramdisk_image);
	}
#endif

	reserve_ebda_region();

	/*
	 * At this point everything still needed from the boot loader
	 * or BIOS or kernel text should be early reserved or marked not
	 * RAM in e820. All other memory is free game.
	 */

	start_kernel();
}

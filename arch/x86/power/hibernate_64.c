/*
 * Hibernation support for x86-64
 *
 * Distribute under GPLv2
 *
 * Copyright (c) 2007 Rafael J. Wysocki <rjw@sisk.pl>
 * Copyright (c) 2002 Pavel Machek <pavel@ucw.cz>
 * Copyright (c) 2001 Patrick Mochel <mochel@osdl.org>
 */

#include <linux/gfp.h>
#include <linux/smp.h>
#include <linux/suspend.h>

#include <asm/init.h>
#include <asm/proto.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/mtrr.h>
#include <asm/sections.h>
#include <asm/suspend.h>
#include <asm/tlbflush.h>

/* Defined in hibernate_asm_64.S */
extern asmlinkage __visible int restore_image(void);

/*
 * Address to jump to in the last phase of restore in order to get to the image
 * kernel's text (this value is passed in the image header).
 */
unsigned long restore_jump_address __visible;
unsigned long jump_address_phys;

/*
 * Value of the cr3 register from before the hibernation (this value is passed
 * in the image header).
 */
unsigned long restore_cr3 __visible;

unsigned long temp_level4_pgt __visible;

unsigned long relocated_restore_code __visible;

static int set_up_temporary_text_mapping(pgd_t *pgd)
{
	pmd_t *pmd;
	pud_t *pud;

	/*
	 * The new mapping only has to cover the page containing the image
	 * kernel's entry point (jump_address_phys), because the switch over to
	 * it is carried out by relocated code running from a page allocated
	 * specifically for this purpose and covered by the identity mapping, so
	 * the temporary kernel text mapping is only needed for the final jump.
	 * Moreover, in that mapping the virtual address of the image kernel's
	 * entry point must be the same as its virtual address in the image
	 * kernel (restore_jump_address), so the image kernel's
	 * restore_registers() code doesn't find itself in a different area of
	 * the virtual address space after switching over to the original page
	 * tables used by the image kernel.
	 */
	pud = (pud_t *)get_safe_page(GFP_ATOMIC);
	if (!pud)
		return -ENOMEM;

	pmd = (pmd_t *)get_safe_page(GFP_ATOMIC);
	if (!pmd)
		return -ENOMEM;

	set_pmd(pmd + pmd_index(restore_jump_address),
		__pmd((jump_address_phys & PMD_MASK) | __PAGE_KERNEL_LARGE_EXEC));
	set_pud(pud + pud_index(restore_jump_address),
		__pud(__pa(pmd) | _KERNPG_TABLE));
	set_pgd(pgd + pgd_index(restore_jump_address),
		__pgd(__pa(pud) | _KERNPG_TABLE));

	return 0;
}

static void *alloc_pgt_page(void *context)
{
	return (void *)get_safe_page(GFP_ATOMIC);
}

static int set_up_temporary_mappings(void)
{
	struct x86_mapping_info info = {
		.alloc_pgt_page	= alloc_pgt_page,
		.pmd_flag	= __PAGE_KERNEL_LARGE_EXEC,
		.offset		= __PAGE_OFFSET,
	};
	unsigned long mstart, mend;
	pgd_t *pgd;
	int result;
	int i;

	pgd = (pgd_t *)get_safe_page(GFP_ATOMIC);
	if (!pgd)
		return -ENOMEM;

	/* Prepare a temporary mapping for the kernel text */
	result = set_up_temporary_text_mapping(pgd);
	if (result)
		return result;

	/* Set up the direct mapping from scratch */
	for (i = 0; i < nr_pfn_mapped; i++) {
		mstart = pfn_mapped[i].start << PAGE_SHIFT;
		mend   = pfn_mapped[i].end << PAGE_SHIFT;

		result = kernel_ident_mapping_init(&info, pgd, mstart, mend);
		if (result)
			return result;
	}

	temp_level4_pgt = (unsigned long)pgd - __PAGE_OFFSET;
	return 0;
}

static int relocate_restore_code(void)
{
	pgd_t *pgd;
	pud_t *pud;

	relocated_restore_code = get_safe_page(GFP_ATOMIC);
	if (!relocated_restore_code)
		return -ENOMEM;

	memcpy((void *)relocated_restore_code, &core_restore_code, PAGE_SIZE);

	/* Make the page containing the relocated code executable */
	pgd = (pgd_t *)__va(read_cr3()) + pgd_index(relocated_restore_code);
	pud = pud_offset(pgd, relocated_restore_code);
	if (pud_large(*pud)) {
		set_pud(pud, __pud(pud_val(*pud) & ~_PAGE_NX));
	} else {
		pmd_t *pmd = pmd_offset(pud, relocated_restore_code);

		if (pmd_large(*pmd)) {
			set_pmd(pmd, __pmd(pmd_val(*pmd) & ~_PAGE_NX));
		} else {
			pte_t *pte = pte_offset_kernel(pmd, relocated_restore_code);

			set_pte(pte, __pte(pte_val(*pte) & ~_PAGE_NX));
		}
	}
	__flush_tlb_all();

	return 0;
}

int swsusp_arch_resume(void)
{
	int error;

	/* We have got enough memory and from now on we cannot recover */
	error = set_up_temporary_mappings();
	if (error)
		return error;

	error = relocate_restore_code();
	if (error)
		return error;

	restore_image();
	return 0;
}

/*
 *	pfn_is_nosave - check if given pfn is in the 'nosave' section
 */

int pfn_is_nosave(unsigned long pfn)
{
	unsigned long nosave_begin_pfn = __pa_symbol(&__nosave_begin) >> PAGE_SHIFT;
	unsigned long nosave_end_pfn = PAGE_ALIGN(__pa_symbol(&__nosave_end)) >> PAGE_SHIFT;
	return (pfn >= nosave_begin_pfn) && (pfn < nosave_end_pfn);
}

struct restore_data_record {
	unsigned long jump_address;
	unsigned long jump_address_phys;
	unsigned long cr3;
	unsigned long magic;
};

#define RESTORE_MAGIC	0x123456789ABCDEF0UL

/**
 *	arch_hibernation_header_save - populate the architecture specific part
 *		of a hibernation image header
 *	@addr: address to save the data at
 */
int arch_hibernation_header_save(void *addr, unsigned int max_size)
{
	struct restore_data_record *rdr = addr;

	if (max_size < sizeof(struct restore_data_record))
		return -EOVERFLOW;
	rdr->jump_address = (unsigned long)&restore_registers;
	rdr->jump_address_phys = __pa_symbol(&restore_registers);
	rdr->cr3 = restore_cr3;
	rdr->magic = RESTORE_MAGIC;
	return 0;
}

/**
 *	arch_hibernation_header_restore - read the architecture specific data
 *		from the hibernation image header
 *	@addr: address to read the data from
 */
int arch_hibernation_header_restore(void *addr)
{
	struct restore_data_record *rdr = addr;

	restore_jump_address = rdr->jump_address;
	jump_address_phys = rdr->jump_address_phys;
	restore_cr3 = rdr->cr3;
	return (rdr->magic == RESTORE_MAGIC) ? 0 : -EINVAL;
}

/*
 * Hibernation support for x86-64
 *
 * Distribute under GPLv2
 *
 * Copyright (c) 2007 Rafael J. Wysocki <rjw@sisk.pl>
 * Copyright (c) 2002 Pavel Machek <pavel@suse.cz>
 * Copyright (c) 2001 Patrick Mochel <mochel@osdl.org>
 */

#include <linux/smp.h>
#include <linux/suspend.h>
#include <asm/proto.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/mtrr.h>

/* References to section boundaries */
extern const void __nosave_begin, __nosave_end;

/* Defined in hibernate_asm_64.S */
extern int restore_image(void);

/*
 * Address to jump to in the last phase of restore in order to get to the image
 * kernel's text (this value is passed in the image header).
 */
unsigned long restore_jump_address;

/*
 * Value of the cr3 register from before the hibernation (this value is passed
 * in the image header).
 */
unsigned long restore_cr3;

pgd_t *temp_level4_pgt;

void *relocated_restore_code;

static int res_phys_pud_init(pud_t *pud, unsigned long address, unsigned long end)
{
	long i, j;

	i = pud_index(address);
	pud = pud + i;
	for (; i < PTRS_PER_PUD; pud++, i++) {
		unsigned long paddr;
		pmd_t *pmd;

		paddr = address + i*PUD_SIZE;
		if (paddr >= end)
			break;

		pmd = (pmd_t *)get_safe_page(GFP_ATOMIC);
		if (!pmd)
			return -ENOMEM;
		set_pud(pud, __pud(__pa(pmd) | _KERNPG_TABLE));
		for (j = 0; j < PTRS_PER_PMD; pmd++, j++, paddr += PMD_SIZE) {
			unsigned long pe;

			if (paddr >= end)
				break;
			pe = __PAGE_KERNEL_LARGE_EXEC | paddr;
			pe &= __supported_pte_mask;
			set_pmd(pmd, __pmd(pe));
		}
	}
	return 0;
}

static int set_up_temporary_mappings(void)
{
	unsigned long start, end, next;
	int error;

	temp_level4_pgt = (pgd_t *)get_safe_page(GFP_ATOMIC);
	if (!temp_level4_pgt)
		return -ENOMEM;

	/* It is safe to reuse the original kernel mapping */
	set_pgd(temp_level4_pgt + pgd_index(__START_KERNEL_map),
		init_level4_pgt[pgd_index(__START_KERNEL_map)]);

	/* Set up the direct mapping from scratch */
	start = (unsigned long)pfn_to_kaddr(0);
	end = (unsigned long)pfn_to_kaddr(end_pfn);

	for (; start < end; start = next) {
		pud_t *pud = (pud_t *)get_safe_page(GFP_ATOMIC);
		if (!pud)
			return -ENOMEM;
		next = start + PGDIR_SIZE;
		if (next > end)
			next = end;
		if ((error = res_phys_pud_init(pud, __pa(start), __pa(next))))
			return error;
		set_pgd(temp_level4_pgt + pgd_index(start),
			mk_kernel_pgd(__pa(pud)));
	}
	return 0;
}

int swsusp_arch_resume(void)
{
	int error;

	/* We have got enough memory and from now on we cannot recover */
	if ((error = set_up_temporary_mappings()))
		return error;

	relocated_restore_code = (void *)get_safe_page(GFP_ATOMIC);
	if (!relocated_restore_code)
		return -ENOMEM;
	memcpy(relocated_restore_code, &core_restore_code,
	       &restore_registers - &core_restore_code);

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
	unsigned long cr3;
	unsigned long magic;
};

#define RESTORE_MAGIC	0x0123456789ABCDEFUL

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
	rdr->jump_address = restore_jump_address;
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
	restore_cr3 = rdr->cr3;
	return (rdr->magic == RESTORE_MAGIC) ? 0 : -EINVAL;
}

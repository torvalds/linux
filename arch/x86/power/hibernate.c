// SPDX-License-Identifier: GPL-2.0
/*
 * Hibernation support for x86
 *
 * Copyright (c) 2007 Rafael J. Wysocki <rjw@sisk.pl>
 * Copyright (c) 2002 Pavel Machek <pavel@ucw.cz>
 * Copyright (c) 2001 Patrick Mochel <mochel@osdl.org>
 */
#include <linux/gfp.h>
#include <linux/smp.h>
#include <linux/suspend.h>
#include <linux/scatterlist.h>
#include <linux/kdebug.h>
#include <linux/cpu.h>
#include <linux/pgtable.h>
#include <linux/types.h>
#include <linux/crc32.h>

#include <asm/e820/api.h>
#include <asm/init.h>
#include <asm/proto.h>
#include <asm/page.h>
#include <asm/mtrr.h>
#include <asm/sections.h>
#include <asm/suspend.h>
#include <asm/tlbflush.h>

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
unsigned long temp_pgt __visible;
unsigned long relocated_restore_code __visible;

/**
 *	pfn_is_nosave - check if given pfn is in the 'nosave' section
 */
int pfn_is_nosave(unsigned long pfn)
{
	unsigned long nosave_begin_pfn;
	unsigned long nosave_end_pfn;

	nosave_begin_pfn = __pa_symbol(&__nosave_begin) >> PAGE_SHIFT;
	nosave_end_pfn = PAGE_ALIGN(__pa_symbol(&__nosave_end)) >> PAGE_SHIFT;

	return pfn >= nosave_begin_pfn && pfn < nosave_end_pfn;
}

struct restore_data_record {
	unsigned long jump_address;
	unsigned long jump_address_phys;
	unsigned long cr3;
	unsigned long magic;
	unsigned long e820_checksum;
};

/**
 * compute_e820_crc32 - calculate crc32 of a given e820 table
 *
 * @table: the e820 table to be calculated
 *
 * Return: the resulting checksum
 */
static inline u32 compute_e820_crc32(struct e820_table *table)
{
	int size = offsetof(struct e820_table, entries) +
		sizeof(struct e820_entry) * table->nr_entries;

	return ~crc32_le(~0, (unsigned char const *)table, size);
}

#ifdef CONFIG_X86_64
#define RESTORE_MAGIC	0x23456789ABCDEF02UL
#else
#define RESTORE_MAGIC	0x12345679UL
#endif

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
	rdr->magic = RESTORE_MAGIC;
	rdr->jump_address = (unsigned long)restore_registers;
	rdr->jump_address_phys = __pa_symbol(restore_registers);

	/*
	 * The restore code fixes up CR3 and CR4 in the following sequence:
	 *
	 * [in hibernation asm]
	 * 1. CR3 <= temporary page tables
	 * 2. CR4 <= mmu_cr4_features (from the kernel that restores us)
	 * 3. CR3 <= rdr->cr3
	 * 4. CR4 <= mmu_cr4_features (from us, i.e. the image kernel)
	 * [in restore_processor_state()]
	 * 5. CR4 <= saved CR4
	 * 6. CR3 <= saved CR3
	 *
	 * Our mmu_cr4_features has CR4.PCIDE=0, and toggling
	 * CR4.PCIDE while CR3's PCID bits are nonzero is illegal, so
	 * rdr->cr3 needs to point to valid page tables but must not
	 * have any of the PCID bits set.
	 */
	rdr->cr3 = restore_cr3 & ~CR3_PCID_MASK;

	rdr->e820_checksum = compute_e820_crc32(e820_table_firmware);
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

	if (rdr->magic != RESTORE_MAGIC) {
		pr_crit("Unrecognized hibernate image header format!\n");
		return -EINVAL;
	}

	restore_jump_address = rdr->jump_address;
	jump_address_phys = rdr->jump_address_phys;
	restore_cr3 = rdr->cr3;

	if (rdr->e820_checksum != compute_e820_crc32(e820_table_firmware)) {
		pr_crit("Hibernate inconsistent memory map detected!\n");
		return -ENODEV;
	}

	return 0;
}

int relocate_restore_code(void)
{
	pgd_t *pgd;
	p4d_t *p4d;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte;

	relocated_restore_code = get_safe_page(GFP_ATOMIC);
	if (!relocated_restore_code)
		return -ENOMEM;

	__memcpy((void *)relocated_restore_code, core_restore_code, PAGE_SIZE);

	/* Make the page containing the relocated code executable */
	pgd = (pgd_t *)__va(read_cr3_pa()) +
		pgd_index(relocated_restore_code);
	p4d = p4d_offset(pgd, relocated_restore_code);
	if (p4d_large(*p4d)) {
		set_p4d(p4d, __p4d(p4d_val(*p4d) & ~_PAGE_NX));
		goto out;
	}
	pud = pud_offset(p4d, relocated_restore_code);
	if (pud_large(*pud)) {
		set_pud(pud, __pud(pud_val(*pud) & ~_PAGE_NX));
		goto out;
	}
	pmd = pmd_offset(pud, relocated_restore_code);
	if (pmd_large(*pmd)) {
		set_pmd(pmd, __pmd(pmd_val(*pmd) & ~_PAGE_NX));
		goto out;
	}
	pte = pte_offset_kernel(pmd, relocated_restore_code);
	set_pte(pte, __pte(pte_val(*pte) & ~_PAGE_NX));
out:
	__flush_tlb_all();
	return 0;
}

int arch_resume_nosmt(void)
{
	int ret = 0;
	/*
	 * We reached this while coming out of hibernation. This means
	 * that SMT siblings are sleeping in hlt, as mwait is not safe
	 * against control transition during resume (see comment in
	 * hibernate_resume_nonboot_cpu_disable()).
	 *
	 * If the resumed kernel has SMT disabled, we have to take all the
	 * SMT siblings out of hlt, and offline them again so that they
	 * end up in mwait proper.
	 *
	 * Called with hotplug disabled.
	 */
	cpu_hotplug_enable();
	if (cpu_smt_control == CPU_SMT_DISABLED ||
			cpu_smt_control == CPU_SMT_FORCE_DISABLED) {
		enum cpuhp_smt_control old = cpu_smt_control;

		ret = cpuhp_smt_enable();
		if (ret)
			goto out;
		ret = cpuhp_smt_disable(old);
		if (ret)
			goto out;
	}
out:
	cpu_hotplug_disable();
	return ret;
}

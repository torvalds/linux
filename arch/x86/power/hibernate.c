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

#include <crypto/hash.h>

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


#define MD5_DIGEST_SIZE 16

struct restore_data_record {
	unsigned long jump_address;
	unsigned long jump_address_phys;
	unsigned long cr3;
	unsigned long magic;
	u8 e820_digest[MD5_DIGEST_SIZE];
};

#if IS_BUILTIN(CONFIG_CRYPTO_MD5)
/**
 * get_e820_md5 - calculate md5 according to given e820 table
 *
 * @table: the e820 table to be calculated
 * @buf: the md5 result to be stored to
 */
static int get_e820_md5(struct e820_table *table, void *buf)
{
	struct crypto_shash *tfm;
	struct shash_desc *desc;
	int size;
	int ret = 0;

	tfm = crypto_alloc_shash("md5", 0, 0);
	if (IS_ERR(tfm))
		return -ENOMEM;

	desc = kmalloc(sizeof(struct shash_desc) + crypto_shash_descsize(tfm),
		       GFP_KERNEL);
	if (!desc) {
		ret = -ENOMEM;
		goto free_tfm;
	}

	desc->tfm = tfm;

	size = offsetof(struct e820_table, entries) +
		sizeof(struct e820_entry) * table->nr_entries;

	if (crypto_shash_digest(desc, (u8 *)table, size, buf))
		ret = -EINVAL;

	kzfree(desc);

free_tfm:
	crypto_free_shash(tfm);
	return ret;
}

static int hibernation_e820_save(void *buf)
{
	return get_e820_md5(e820_table_firmware, buf);
}

static bool hibernation_e820_mismatch(void *buf)
{
	int ret;
	u8 result[MD5_DIGEST_SIZE];

	memset(result, 0, MD5_DIGEST_SIZE);
	/* If there is no digest in suspend kernel, let it go. */
	if (!memcmp(result, buf, MD5_DIGEST_SIZE))
		return false;

	ret = get_e820_md5(e820_table_firmware, result);
	if (ret)
		return true;

	return memcmp(result, buf, MD5_DIGEST_SIZE) ? true : false;
}
#else
static int hibernation_e820_save(void *buf)
{
	return 0;
}

static bool hibernation_e820_mismatch(void *buf)
{
	/* If md5 is not builtin for restore kernel, let it go. */
	return false;
}
#endif

#ifdef CONFIG_X86_64
#define RESTORE_MAGIC	0x23456789ABCDEF01UL
#else
#define RESTORE_MAGIC	0x12345678UL
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

	return hibernation_e820_save(rdr->e820_digest);
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

	if (hibernation_e820_mismatch(rdr->e820_digest)) {
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

	memcpy((void *)relocated_restore_code, core_restore_code, PAGE_SIZE);

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

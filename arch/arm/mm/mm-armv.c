/*
 *  linux/arch/arm/mm/mm-armv.c
 *
 *  Copyright (C) 1998-2002 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  Page table sludge for ARM v3 and v4 processor architectures.
 */
#include <linux/config.h>
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/bootmem.h>
#include <linux/highmem.h>
#include <linux/nodemask.h>

#include <asm/pgalloc.h>
#include <asm/page.h>
#include <asm/io.h>
#include <asm/setup.h>
#include <asm/tlbflush.h>

#include <asm/mach/map.h>

#define CPOLICY_UNCACHED	0
#define CPOLICY_BUFFERED	1
#define CPOLICY_WRITETHROUGH	2
#define CPOLICY_WRITEBACK	3
#define CPOLICY_WRITEALLOC	4

static unsigned int cachepolicy __initdata = CPOLICY_WRITEBACK;
static unsigned int ecc_mask __initdata = 0;
pgprot_t pgprot_kernel;

EXPORT_SYMBOL(pgprot_kernel);

pmd_t *top_pmd;

struct cachepolicy {
	const char	policy[16];
	unsigned int	cr_mask;
	unsigned int	pmd;
	unsigned int	pte;
};

static struct cachepolicy cache_policies[] __initdata = {
	{
		.policy		= "uncached",
		.cr_mask	= CR_W|CR_C,
		.pmd		= PMD_SECT_UNCACHED,
		.pte		= 0,
	}, {
		.policy		= "buffered",
		.cr_mask	= CR_C,
		.pmd		= PMD_SECT_BUFFERED,
		.pte		= PTE_BUFFERABLE,
	}, {
		.policy		= "writethrough",
		.cr_mask	= 0,
		.pmd		= PMD_SECT_WT,
		.pte		= PTE_CACHEABLE,
	}, {
		.policy		= "writeback",
		.cr_mask	= 0,
		.pmd		= PMD_SECT_WB,
		.pte		= PTE_BUFFERABLE|PTE_CACHEABLE,
	}, {
		.policy		= "writealloc",
		.cr_mask	= 0,
		.pmd		= PMD_SECT_WBWA,
		.pte		= PTE_BUFFERABLE|PTE_CACHEABLE,
	}
};

/*
 * These are useful for identifing cache coherency
 * problems by allowing the cache or the cache and
 * writebuffer to be turned off.  (Note: the write
 * buffer should not be on and the cache off).
 */
static void __init early_cachepolicy(char **p)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(cache_policies); i++) {
		int len = strlen(cache_policies[i].policy);

		if (memcmp(*p, cache_policies[i].policy, len) == 0) {
			cachepolicy = i;
			cr_alignment &= ~cache_policies[i].cr_mask;
			cr_no_alignment &= ~cache_policies[i].cr_mask;
			*p += len;
			break;
		}
	}
	if (i == ARRAY_SIZE(cache_policies))
		printk(KERN_ERR "ERROR: unknown or unsupported cache policy\n");
	flush_cache_all();
	set_cr(cr_alignment);
}

static void __init early_nocache(char **__unused)
{
	char *p = "buffered";
	printk(KERN_WARNING "nocache is deprecated; use cachepolicy=%s\n", p);
	early_cachepolicy(&p);
}

static void __init early_nowrite(char **__unused)
{
	char *p = "uncached";
	printk(KERN_WARNING "nowb is deprecated; use cachepolicy=%s\n", p);
	early_cachepolicy(&p);
}

static void __init early_ecc(char **p)
{
	if (memcmp(*p, "on", 2) == 0) {
		ecc_mask = PMD_PROTECTION;
		*p += 2;
	} else if (memcmp(*p, "off", 3) == 0) {
		ecc_mask = 0;
		*p += 3;
	}
}

__early_param("nocache", early_nocache);
__early_param("nowb", early_nowrite);
__early_param("cachepolicy=", early_cachepolicy);
__early_param("ecc=", early_ecc);

static int __init noalign_setup(char *__unused)
{
	cr_alignment &= ~CR_A;
	cr_no_alignment &= ~CR_A;
	set_cr(cr_alignment);
	return 1;
}

__setup("noalign", noalign_setup);

#define FIRST_KERNEL_PGD_NR	(FIRST_USER_PGD_NR + USER_PTRS_PER_PGD)

static inline pmd_t *pmd_off(pgd_t *pgd, unsigned long virt)
{
	return pmd_offset(pgd, virt);
}

static inline pmd_t *pmd_off_k(unsigned long virt)
{
	return pmd_off(pgd_offset_k(virt), virt);
}

/*
 * need to get a 16k page for level 1
 */
pgd_t *get_pgd_slow(struct mm_struct *mm)
{
	pgd_t *new_pgd, *init_pgd;
	pmd_t *new_pmd, *init_pmd;
	pte_t *new_pte, *init_pte;

	new_pgd = (pgd_t *)__get_free_pages(GFP_KERNEL, 2);
	if (!new_pgd)
		goto no_pgd;

	memzero(new_pgd, FIRST_KERNEL_PGD_NR * sizeof(pgd_t));

	/*
	 * Copy over the kernel and IO PGD entries
	 */
	init_pgd = pgd_offset_k(0);
	memcpy(new_pgd + FIRST_KERNEL_PGD_NR, init_pgd + FIRST_KERNEL_PGD_NR,
		       (PTRS_PER_PGD - FIRST_KERNEL_PGD_NR) * sizeof(pgd_t));

	clean_dcache_area(new_pgd, PTRS_PER_PGD * sizeof(pgd_t));

	if (!vectors_high()) {
		/*
		 * This lock is here just to satisfy pmd_alloc and pte_lock
		 */
		spin_lock(&mm->page_table_lock);

		/*
		 * On ARM, first page must always be allocated since it
		 * contains the machine vectors.
		 */
		new_pmd = pmd_alloc(mm, new_pgd, 0);
		if (!new_pmd)
			goto no_pmd;

		new_pte = pte_alloc_map(mm, new_pmd, 0);
		if (!new_pte)
			goto no_pte;

		init_pmd = pmd_offset(init_pgd, 0);
		init_pte = pte_offset_map_nested(init_pmd, 0);
		set_pte(new_pte, *init_pte);
		pte_unmap_nested(init_pte);
		pte_unmap(new_pte);

		spin_unlock(&mm->page_table_lock);
	}

	return new_pgd;

no_pte:
	spin_unlock(&mm->page_table_lock);
	pmd_free(new_pmd);
	free_pages((unsigned long)new_pgd, 2);
	return NULL;

no_pmd:
	spin_unlock(&mm->page_table_lock);
	free_pages((unsigned long)new_pgd, 2);
	return NULL;

no_pgd:
	return NULL;
}

void free_pgd_slow(pgd_t *pgd)
{
	pmd_t *pmd;
	struct page *pte;

	if (!pgd)
		return;

	/* pgd is always present and good */
	pmd = pmd_off(pgd, 0);
	if (pmd_none(*pmd))
		goto free;
	if (pmd_bad(*pmd)) {
		pmd_ERROR(*pmd);
		pmd_clear(pmd);
		goto free;
	}

	pte = pmd_page(*pmd);
	pmd_clear(pmd);
	dec_page_state(nr_page_table_pages);
	pte_free(pte);
	pmd_free(pmd);
free:
	free_pages((unsigned long) pgd, 2);
}

/*
 * Create a SECTION PGD between VIRT and PHYS in domain
 * DOMAIN with protection PROT.  This operates on half-
 * pgdir entry increments.
 */
static inline void
alloc_init_section(unsigned long virt, unsigned long phys, int prot)
{
	pmd_t *pmdp = pmd_off_k(virt);

	if (virt & (1 << 20))
		pmdp++;

	*pmdp = __pmd(phys | prot);
	flush_pmd_entry(pmdp);
}

/*
 * Create a SUPER SECTION PGD between VIRT and PHYS with protection PROT
 */
static inline void
alloc_init_supersection(unsigned long virt, unsigned long phys, int prot)
{
	int i;

	for (i = 0; i < 16; i += 1) {
		alloc_init_section(virt, phys, prot | PMD_SECT_SUPER);

		virt += (PGDIR_SIZE / 2);
	}
}

/*
 * Add a PAGE mapping between VIRT and PHYS in domain
 * DOMAIN with protection PROT.  Note that due to the
 * way we map the PTEs, we must allocate two PTE_SIZE'd
 * blocks - one for the Linux pte table, and one for
 * the hardware pte table.
 */
static inline void
alloc_init_page(unsigned long virt, unsigned long phys, unsigned int prot_l1, pgprot_t prot)
{
	pmd_t *pmdp = pmd_off_k(virt);
	pte_t *ptep;

	if (pmd_none(*pmdp)) {
		ptep = alloc_bootmem_low_pages(2 * PTRS_PER_PTE *
					       sizeof(pte_t));

		__pmd_populate(pmdp, __pa(ptep) | prot_l1);
	}
	ptep = pte_offset_kernel(pmdp, virt);

	set_pte(ptep, pfn_pte(phys >> PAGE_SHIFT, prot));
}

/*
 * Clear any PGD mapping.  On a two-level page table system,
 * the clearance is done by the middle-level functions (pmd)
 * rather than the top-level (pgd) functions.
 */
static inline void clear_mapping(unsigned long virt)
{
	pmd_clear(pmd_off_k(virt));
}

struct mem_types {
	unsigned int	prot_pte;
	unsigned int	prot_l1;
	unsigned int	prot_sect;
	unsigned int	domain;
};

static struct mem_types mem_types[] __initdata = {
	[MT_DEVICE] = {
		.prot_pte  = L_PTE_PRESENT | L_PTE_YOUNG | L_PTE_DIRTY |
				L_PTE_WRITE,
		.prot_l1   = PMD_TYPE_TABLE,
		.prot_sect = PMD_TYPE_SECT | PMD_SECT_UNCACHED |
				PMD_SECT_AP_WRITE,
		.domain    = DOMAIN_IO,
	},
	[MT_CACHECLEAN] = {
		.prot_sect = PMD_TYPE_SECT,
		.domain    = DOMAIN_KERNEL,
	},
	[MT_MINICLEAN] = {
		.prot_sect = PMD_TYPE_SECT | PMD_SECT_MINICACHE,
		.domain    = DOMAIN_KERNEL,
	},
	[MT_LOW_VECTORS] = {
		.prot_pte  = L_PTE_PRESENT | L_PTE_YOUNG | L_PTE_DIRTY |
				L_PTE_EXEC,
		.prot_l1   = PMD_TYPE_TABLE,
		.domain    = DOMAIN_USER,
	},
	[MT_HIGH_VECTORS] = {
		.prot_pte  = L_PTE_PRESENT | L_PTE_YOUNG | L_PTE_DIRTY |
				L_PTE_USER | L_PTE_EXEC,
		.prot_l1   = PMD_TYPE_TABLE,
		.domain    = DOMAIN_USER,
	},
	[MT_MEMORY] = {
		.prot_sect = PMD_TYPE_SECT | PMD_SECT_AP_WRITE,
		.domain    = DOMAIN_KERNEL,
	},
	[MT_ROM] = {
		.prot_sect = PMD_TYPE_SECT,
		.domain    = DOMAIN_KERNEL,
	},
	[MT_IXP2000_DEVICE] = { /* IXP2400 requires XCB=101 for on-chip I/O */
		.prot_pte  = L_PTE_PRESENT | L_PTE_YOUNG | L_PTE_DIRTY |
				L_PTE_WRITE,
		.prot_l1   = PMD_TYPE_TABLE,
		.prot_sect = PMD_TYPE_SECT | PMD_SECT_UNCACHED |
				PMD_SECT_AP_WRITE | PMD_SECT_BUFFERABLE |
				PMD_SECT_TEX(1),
		.domain    = DOMAIN_IO,
	}
};

/*
 * Adjust the PMD section entries according to the CPU in use.
 */
static void __init build_mem_type_table(void)
{
	struct cachepolicy *cp;
	unsigned int cr = get_cr();
	unsigned int user_pgprot;
	int cpu_arch = cpu_architecture();
	int i;

#if defined(CONFIG_CPU_DCACHE_DISABLE)
	if (cachepolicy > CPOLICY_BUFFERED)
		cachepolicy = CPOLICY_BUFFERED;
#elif defined(CONFIG_CPU_DCACHE_WRITETHROUGH)
	if (cachepolicy > CPOLICY_WRITETHROUGH)
		cachepolicy = CPOLICY_WRITETHROUGH;
#endif
	if (cpu_arch < CPU_ARCH_ARMv5) {
		if (cachepolicy >= CPOLICY_WRITEALLOC)
			cachepolicy = CPOLICY_WRITEBACK;
		ecc_mask = 0;
	}

	if (cpu_arch <= CPU_ARCH_ARMv5TEJ) {
		for (i = 0; i < ARRAY_SIZE(mem_types); i++) {
			if (mem_types[i].prot_l1)
				mem_types[i].prot_l1 |= PMD_BIT4;
			if (mem_types[i].prot_sect)
				mem_types[i].prot_sect |= PMD_BIT4;
		}
	}

	cp = &cache_policies[cachepolicy];
	user_pgprot = cp->pte;

	/*
	 * ARMv6 and above have extended page tables.
	 */
	if (cpu_arch >= CPU_ARCH_ARMv6 && (cr & CR_XP)) {
		/*
		 * bit 4 becomes XN which we must clear for the
		 * kernel memory mapping.
		 */
		mem_types[MT_MEMORY].prot_sect &= ~PMD_BIT4;
		mem_types[MT_ROM].prot_sect &= ~PMD_BIT4;
		/*
		 * Mark cache clean areas and XIP ROM read only
		 * from SVC mode and no access from userspace.
		 */
		mem_types[MT_ROM].prot_sect |= PMD_SECT_APX|PMD_SECT_AP_WRITE;
		mem_types[MT_MINICLEAN].prot_sect |= PMD_SECT_APX|PMD_SECT_AP_WRITE;
		mem_types[MT_CACHECLEAN].prot_sect |= PMD_SECT_APX|PMD_SECT_AP_WRITE;

		/*
		 * Mark the device area as "shared device"
		 */
		mem_types[MT_DEVICE].prot_pte |= L_PTE_BUFFERABLE;
		mem_types[MT_DEVICE].prot_sect |= PMD_SECT_BUFFERED;

		/*
		 * User pages need to be mapped with the ASID
		 * (iow, non-global)
		 */
		user_pgprot |= L_PTE_ASID;
	}

	if (cpu_arch >= CPU_ARCH_ARMv5) {
		mem_types[MT_LOW_VECTORS].prot_pte |= cp->pte & PTE_CACHEABLE;
		mem_types[MT_HIGH_VECTORS].prot_pte |= cp->pte & PTE_CACHEABLE;
	} else {
		mem_types[MT_LOW_VECTORS].prot_pte |= cp->pte;
		mem_types[MT_HIGH_VECTORS].prot_pte |= cp->pte;
		mem_types[MT_MINICLEAN].prot_sect &= ~PMD_SECT_TEX(1);
	}

	mem_types[MT_LOW_VECTORS].prot_l1 |= ecc_mask;
	mem_types[MT_HIGH_VECTORS].prot_l1 |= ecc_mask;
	mem_types[MT_MEMORY].prot_sect |= ecc_mask | cp->pmd;
	mem_types[MT_ROM].prot_sect |= cp->pmd;

	for (i = 0; i < 16; i++) {
		unsigned long v = pgprot_val(protection_map[i]);
		v = (v & ~(PTE_BUFFERABLE|PTE_CACHEABLE)) | user_pgprot;
		protection_map[i] = __pgprot(v);
	}

	pgprot_kernel = __pgprot(L_PTE_PRESENT | L_PTE_YOUNG |
				 L_PTE_DIRTY | L_PTE_WRITE |
				 L_PTE_EXEC | cp->pte);

	switch (cp->pmd) {
	case PMD_SECT_WT:
		mem_types[MT_CACHECLEAN].prot_sect |= PMD_SECT_WT;
		break;
	case PMD_SECT_WB:
	case PMD_SECT_WBWA:
		mem_types[MT_CACHECLEAN].prot_sect |= PMD_SECT_WB;
		break;
	}
	printk("Memory policy: ECC %sabled, Data cache %s\n",
		ecc_mask ? "en" : "dis", cp->policy);
}

#define vectors_base()	(vectors_high() ? 0xffff0000 : 0)

/*
 * Create the page directory entries and any necessary
 * page tables for the mapping specified by `md'.  We
 * are able to cope here with varying sizes and address
 * offsets, and we take full advantage of sections and
 * supersections.
 */
static void __init create_mapping(struct map_desc *md)
{
	unsigned long virt, length;
	int prot_sect, prot_l1, domain;
	pgprot_t prot_pte;
	long off;

	if (md->virtual != vectors_base() && md->virtual < TASK_SIZE) {
		printk(KERN_WARNING "BUG: not creating mapping for "
		       "0x%08lx at 0x%08lx in user region\n",
		       md->physical, md->virtual);
		return;
	}

	if ((md->type == MT_DEVICE || md->type == MT_ROM) &&
	    md->virtual >= PAGE_OFFSET && md->virtual < VMALLOC_END) {
		printk(KERN_WARNING "BUG: mapping for 0x%08lx at 0x%08lx "
		       "overlaps vmalloc space\n",
		       md->physical, md->virtual);
	}

	domain	  = mem_types[md->type].domain;
	prot_pte  = __pgprot(mem_types[md->type].prot_pte);
	prot_l1   = mem_types[md->type].prot_l1 | PMD_DOMAIN(domain);
	prot_sect = mem_types[md->type].prot_sect | PMD_DOMAIN(domain);

	virt   = md->virtual;
	off    = md->physical - virt;
	length = md->length;

	if (mem_types[md->type].prot_l1 == 0 &&
	    (virt & 0xfffff || (virt + off) & 0xfffff || (virt + length) & 0xfffff)) {
		printk(KERN_WARNING "BUG: map for 0x%08lx at 0x%08lx can not "
		       "be mapped using pages, ignoring.\n",
		       md->physical, md->virtual);
		return;
	}

	while ((virt & 0xfffff || (virt + off) & 0xfffff) && length >= PAGE_SIZE) {
		alloc_init_page(virt, virt + off, prot_l1, prot_pte);

		virt   += PAGE_SIZE;
		length -= PAGE_SIZE;
	}

	/* N.B.	ARMv6 supersections are only defined to work with domain 0.
	 *	Since domain assignments can in fact be arbitrary, the
	 *	'domain == 0' check below is required to insure that ARMv6
	 *	supersections are only allocated for domain 0 regardless
	 *	of the actual domain assignments in use.
	 */
	if (cpu_architecture() >= CPU_ARCH_ARMv6 && domain == 0) {
		/* Align to supersection boundary */
		while ((virt & ~SUPERSECTION_MASK || (virt + off) &
			~SUPERSECTION_MASK) && length >= (PGDIR_SIZE / 2)) {
			alloc_init_section(virt, virt + off, prot_sect);

			virt   += (PGDIR_SIZE / 2);
			length -= (PGDIR_SIZE / 2);
		}

		while (length >= SUPERSECTION_SIZE) {
			alloc_init_supersection(virt, virt + off, prot_sect);

			virt   += SUPERSECTION_SIZE;
			length -= SUPERSECTION_SIZE;
		}
	}

	/*
	 * A section mapping covers half a "pgdir" entry.
	 */
	while (length >= (PGDIR_SIZE / 2)) {
		alloc_init_section(virt, virt + off, prot_sect);

		virt   += (PGDIR_SIZE / 2);
		length -= (PGDIR_SIZE / 2);
	}

	while (length >= PAGE_SIZE) {
		alloc_init_page(virt, virt + off, prot_l1, prot_pte);

		virt   += PAGE_SIZE;
		length -= PAGE_SIZE;
	}
}

/*
 * In order to soft-boot, we need to insert a 1:1 mapping in place of
 * the user-mode pages.  This will then ensure that we have predictable
 * results when turning the mmu off
 */
void setup_mm_for_reboot(char mode)
{
	unsigned long base_pmdval;
	pgd_t *pgd;
	int i;

	if (current->mm && current->mm->pgd)
		pgd = current->mm->pgd;
	else
		pgd = init_mm.pgd;

	base_pmdval = PMD_SECT_AP_WRITE | PMD_SECT_AP_READ | PMD_TYPE_SECT;
	if (cpu_architecture() <= CPU_ARCH_ARMv5TEJ)
		base_pmdval |= PMD_BIT4;

	for (i = 0; i < FIRST_USER_PGD_NR + USER_PTRS_PER_PGD; i++, pgd++) {
		unsigned long pmdval = (i << PGDIR_SHIFT) | base_pmdval;
		pmd_t *pmd;

		pmd = pmd_off(pgd, i << PGDIR_SHIFT);
		pmd[0] = __pmd(pmdval);
		pmd[1] = __pmd(pmdval + (1 << (PGDIR_SHIFT - 1)));
		flush_pmd_entry(pmd);
	}
}

extern void _stext, _etext;

/*
 * Setup initial mappings.  We use the page we allocated for zero page to hold
 * the mappings, which will get overwritten by the vectors in traps_init().
 * The mappings must be in virtual address order.
 */
void __init memtable_init(struct meminfo *mi)
{
	struct map_desc *init_maps, *p, *q;
	unsigned long address = 0;
	int i;

	build_mem_type_table();

	init_maps = p = alloc_bootmem_low_pages(PAGE_SIZE);

#ifdef CONFIG_XIP_KERNEL
	p->physical   = CONFIG_XIP_PHYS_ADDR & PMD_MASK;
	p->virtual    = (unsigned long)&_stext & PMD_MASK;
	p->length     = ((unsigned long)&_etext - p->virtual + ~PMD_MASK) & PMD_MASK;
	p->type       = MT_ROM;
	p ++;
#endif

	for (i = 0; i < mi->nr_banks; i++) {
		if (mi->bank[i].size == 0)
			continue;

		p->physical   = mi->bank[i].start;
		p->virtual    = __phys_to_virt(p->physical);
		p->length     = mi->bank[i].size;
		p->type       = MT_MEMORY;
		p ++;
	}

#ifdef FLUSH_BASE
	p->physical   = FLUSH_BASE_PHYS;
	p->virtual    = FLUSH_BASE;
	p->length     = PGDIR_SIZE;
	p->type       = MT_CACHECLEAN;
	p ++;
#endif

#ifdef FLUSH_BASE_MINICACHE
	p->physical   = FLUSH_BASE_PHYS + PGDIR_SIZE;
	p->virtual    = FLUSH_BASE_MINICACHE;
	p->length     = PGDIR_SIZE;
	p->type       = MT_MINICLEAN;
	p ++;
#endif

	/*
	 * Go through the initial mappings, but clear out any
	 * pgdir entries that are not in the description.
	 */
	q = init_maps;
	do {
		if (address < q->virtual || q == p) {
			clear_mapping(address);
			address += PGDIR_SIZE;
		} else {
			create_mapping(q);

			address = q->virtual + q->length;
			address = (address + PGDIR_SIZE - 1) & PGDIR_MASK;

			q ++;
		}
	} while (address != 0);

	/*
	 * Create a mapping for the machine vectors at the high-vectors
	 * location (0xffff0000).  If we aren't using high-vectors, also
	 * create a mapping at the low-vectors virtual address.
	 */
	init_maps->physical   = virt_to_phys(init_maps);
	init_maps->virtual    = 0xffff0000;
	init_maps->length     = PAGE_SIZE;
	init_maps->type       = MT_HIGH_VECTORS;
	create_mapping(init_maps);

	if (!vectors_high()) {
		init_maps->virtual = 0;
		init_maps->type = MT_LOW_VECTORS;
		create_mapping(init_maps);
	}

	flush_cache_all();
	local_flush_tlb_all();

	top_pmd = pmd_off_k(0xffff0000);
}

/*
 * Create the architecture specific mappings
 */
void __init iotable_init(struct map_desc *io_desc, int nr)
{
	int i;

	for (i = 0; i < nr; i++)
		create_mapping(io_desc + i);
}

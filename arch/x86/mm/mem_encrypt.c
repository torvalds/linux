/*
 * AMD Memory Encryption Support
 *
 * Copyright (C) 2016 Advanced Micro Devices, Inc.
 *
 * Author: Tom Lendacky <thomas.lendacky@amd.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#define DISABLE_BRANCH_PROFILING

#include <linux/linkage.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/dma-mapping.h>
#include <linux/swiotlb.h>
#include <linux/mem_encrypt.h>

#include <asm/tlbflush.h>
#include <asm/fixmap.h>
#include <asm/setup.h>
#include <asm/bootparam.h>
#include <asm/set_memory.h>
#include <asm/cacheflush.h>
#include <asm/sections.h>
#include <asm/processor-flags.h>
#include <asm/msr.h>
#include <asm/cmdline.h>

static char sme_cmdline_arg[] __initdata = "mem_encrypt";
static char sme_cmdline_on[]  __initdata = "on";
static char sme_cmdline_off[] __initdata = "off";

/*
 * Since SME related variables are set early in the boot process they must
 * reside in the .data section so as not to be zeroed out when the .bss
 * section is later cleared.
 */
u64 sme_me_mask __section(.data) = 0;
EXPORT_SYMBOL(sme_me_mask);

/* Buffer used for early in-place encryption by BSP, no locking needed */
static char sme_early_buffer[PAGE_SIZE] __aligned(PAGE_SIZE);

/*
 * This routine does not change the underlying encryption setting of the
 * page(s) that map this memory. It assumes that eventually the memory is
 * meant to be accessed as either encrypted or decrypted but the contents
 * are currently not in the desired state.
 *
 * This routine follows the steps outlined in the AMD64 Architecture
 * Programmer's Manual Volume 2, Section 7.10.8 Encrypt-in-Place.
 */
static void __init __sme_early_enc_dec(resource_size_t paddr,
				       unsigned long size, bool enc)
{
	void *src, *dst;
	size_t len;

	if (!sme_me_mask)
		return;

	local_flush_tlb();
	wbinvd();

	/*
	 * There are limited number of early mapping slots, so map (at most)
	 * one page at time.
	 */
	while (size) {
		len = min_t(size_t, sizeof(sme_early_buffer), size);

		/*
		 * Create mappings for the current and desired format of
		 * the memory. Use a write-protected mapping for the source.
		 */
		src = enc ? early_memremap_decrypted_wp(paddr, len) :
			    early_memremap_encrypted_wp(paddr, len);

		dst = enc ? early_memremap_encrypted(paddr, len) :
			    early_memremap_decrypted(paddr, len);

		/*
		 * If a mapping can't be obtained to perform the operation,
		 * then eventual access of that area in the desired mode
		 * will cause a crash.
		 */
		BUG_ON(!src || !dst);

		/*
		 * Use a temporary buffer, of cache-line multiple size, to
		 * avoid data corruption as documented in the APM.
		 */
		memcpy(sme_early_buffer, src, len);
		memcpy(dst, sme_early_buffer, len);

		early_memunmap(dst, len);
		early_memunmap(src, len);

		paddr += len;
		size -= len;
	}
}

void __init sme_early_encrypt(resource_size_t paddr, unsigned long size)
{
	__sme_early_enc_dec(paddr, size, true);
}

void __init sme_early_decrypt(resource_size_t paddr, unsigned long size)
{
	__sme_early_enc_dec(paddr, size, false);
}

static void __init __sme_early_map_unmap_mem(void *vaddr, unsigned long size,
					     bool map)
{
	unsigned long paddr = (unsigned long)vaddr - __PAGE_OFFSET;
	pmdval_t pmd_flags, pmd;

	/* Use early_pmd_flags but remove the encryption mask */
	pmd_flags = __sme_clr(early_pmd_flags);

	do {
		pmd = map ? (paddr & PMD_MASK) + pmd_flags : 0;
		__early_make_pgtable((unsigned long)vaddr, pmd);

		vaddr += PMD_SIZE;
		paddr += PMD_SIZE;
		size = (size <= PMD_SIZE) ? 0 : size - PMD_SIZE;
	} while (size);

	__native_flush_tlb();
}

void __init sme_unmap_bootdata(char *real_mode_data)
{
	struct boot_params *boot_data;
	unsigned long cmdline_paddr;

	if (!sme_active())
		return;

	/* Get the command line address before unmapping the real_mode_data */
	boot_data = (struct boot_params *)real_mode_data;
	cmdline_paddr = boot_data->hdr.cmd_line_ptr | ((u64)boot_data->ext_cmd_line_ptr << 32);

	__sme_early_map_unmap_mem(real_mode_data, sizeof(boot_params), false);

	if (!cmdline_paddr)
		return;

	__sme_early_map_unmap_mem(__va(cmdline_paddr), COMMAND_LINE_SIZE, false);
}

void __init sme_map_bootdata(char *real_mode_data)
{
	struct boot_params *boot_data;
	unsigned long cmdline_paddr;

	if (!sme_active())
		return;

	__sme_early_map_unmap_mem(real_mode_data, sizeof(boot_params), true);

	/* Get the command line address after mapping the real_mode_data */
	boot_data = (struct boot_params *)real_mode_data;
	cmdline_paddr = boot_data->hdr.cmd_line_ptr | ((u64)boot_data->ext_cmd_line_ptr << 32);

	if (!cmdline_paddr)
		return;

	__sme_early_map_unmap_mem(__va(cmdline_paddr), COMMAND_LINE_SIZE, true);
}

void __init sme_early_init(void)
{
	unsigned int i;

	if (!sme_me_mask)
		return;

	early_pmd_flags = __sme_set(early_pmd_flags);

	__supported_pte_mask = __sme_set(__supported_pte_mask);

	/* Update the protection map with memory encryption mask */
	for (i = 0; i < ARRAY_SIZE(protection_map); i++)
		protection_map[i] = pgprot_encrypted(protection_map[i]);
}

/* Architecture __weak replacement functions */
void __init mem_encrypt_init(void)
{
	if (!sme_me_mask)
		return;

	/* Call into SWIOTLB to update the SWIOTLB DMA buffers */
	swiotlb_update_mem_attributes();

	pr_info("AMD Secure Memory Encryption (SME) active\n");
}

void swiotlb_set_mem_attributes(void *vaddr, unsigned long size)
{
	WARN(PAGE_ALIGN(size) != size,
	     "size is not page-aligned (%#lx)\n", size);

	/* Make the SWIOTLB buffer area decrypted */
	set_memory_decrypted((unsigned long)vaddr, size >> PAGE_SHIFT);
}

struct sme_populate_pgd_data {
	void	*pgtable_area;
	pgd_t	*pgd;

	pmdval_t pmd_flags;
	pteval_t pte_flags;
	unsigned long paddr;

	unsigned long vaddr;
	unsigned long vaddr_end;
};

static void __init sme_clear_pgd(struct sme_populate_pgd_data *ppd)
{
	unsigned long pgd_start, pgd_end, pgd_size;
	pgd_t *pgd_p;

	pgd_start = ppd->vaddr & PGDIR_MASK;
	pgd_end = ppd->vaddr_end & PGDIR_MASK;

	pgd_size = (((pgd_end - pgd_start) / PGDIR_SIZE) + 1) * sizeof(pgd_t);

	pgd_p = ppd->pgd + pgd_index(ppd->vaddr);

	memset(pgd_p, 0, pgd_size);
}

#define PGD_FLAGS		_KERNPG_TABLE_NOENC
#define P4D_FLAGS		_KERNPG_TABLE_NOENC
#define PUD_FLAGS		_KERNPG_TABLE_NOENC
#define PMD_FLAGS		_KERNPG_TABLE_NOENC

#define PMD_FLAGS_LARGE		(__PAGE_KERNEL_LARGE_EXEC & ~_PAGE_GLOBAL)

#define PMD_FLAGS_DEC		PMD_FLAGS_LARGE
#define PMD_FLAGS_DEC_WP	((PMD_FLAGS_DEC & ~_PAGE_CACHE_MASK) | \
				 (_PAGE_PAT | _PAGE_PWT))

#define PMD_FLAGS_ENC		(PMD_FLAGS_LARGE | _PAGE_ENC)

#define PTE_FLAGS		(__PAGE_KERNEL_EXEC & ~_PAGE_GLOBAL)

#define PTE_FLAGS_DEC		PTE_FLAGS
#define PTE_FLAGS_DEC_WP	((PTE_FLAGS_DEC & ~_PAGE_CACHE_MASK) | \
				 (_PAGE_PAT | _PAGE_PWT))

#define PTE_FLAGS_ENC		(PTE_FLAGS | _PAGE_ENC)

static pmd_t __init *sme_prepare_pgd(struct sme_populate_pgd_data *ppd)
{
	pgd_t *pgd_p;
	p4d_t *p4d_p;
	pud_t *pud_p;
	pmd_t *pmd_p;

	pgd_p = ppd->pgd + pgd_index(ppd->vaddr);
	if (native_pgd_val(*pgd_p)) {
		if (IS_ENABLED(CONFIG_X86_5LEVEL))
			p4d_p = (p4d_t *)(native_pgd_val(*pgd_p) & ~PTE_FLAGS_MASK);
		else
			pud_p = (pud_t *)(native_pgd_val(*pgd_p) & ~PTE_FLAGS_MASK);
	} else {
		pgd_t pgd;

		if (IS_ENABLED(CONFIG_X86_5LEVEL)) {
			p4d_p = ppd->pgtable_area;
			memset(p4d_p, 0, sizeof(*p4d_p) * PTRS_PER_P4D);
			ppd->pgtable_area += sizeof(*p4d_p) * PTRS_PER_P4D;

			pgd = native_make_pgd((pgdval_t)p4d_p + PGD_FLAGS);
		} else {
			pud_p = ppd->pgtable_area;
			memset(pud_p, 0, sizeof(*pud_p) * PTRS_PER_PUD);
			ppd->pgtable_area += sizeof(*pud_p) * PTRS_PER_PUD;

			pgd = native_make_pgd((pgdval_t)pud_p + PGD_FLAGS);
		}
		native_set_pgd(pgd_p, pgd);
	}

	if (IS_ENABLED(CONFIG_X86_5LEVEL)) {
		p4d_p += p4d_index(ppd->vaddr);
		if (native_p4d_val(*p4d_p)) {
			pud_p = (pud_t *)(native_p4d_val(*p4d_p) & ~PTE_FLAGS_MASK);
		} else {
			p4d_t p4d;

			pud_p = ppd->pgtable_area;
			memset(pud_p, 0, sizeof(*pud_p) * PTRS_PER_PUD);
			ppd->pgtable_area += sizeof(*pud_p) * PTRS_PER_PUD;

			p4d = native_make_p4d((pudval_t)pud_p + P4D_FLAGS);
			native_set_p4d(p4d_p, p4d);
		}
	}

	pud_p += pud_index(ppd->vaddr);
	if (native_pud_val(*pud_p)) {
		if (native_pud_val(*pud_p) & _PAGE_PSE)
			return NULL;

		pmd_p = (pmd_t *)(native_pud_val(*pud_p) & ~PTE_FLAGS_MASK);
	} else {
		pud_t pud;

		pmd_p = ppd->pgtable_area;
		memset(pmd_p, 0, sizeof(*pmd_p) * PTRS_PER_PMD);
		ppd->pgtable_area += sizeof(*pmd_p) * PTRS_PER_PMD;

		pud = native_make_pud((pmdval_t)pmd_p + PUD_FLAGS);
		native_set_pud(pud_p, pud);
	}

	return pmd_p;
}

static void __init sme_populate_pgd_large(struct sme_populate_pgd_data *ppd)
{
	pmd_t *pmd_p;

	pmd_p = sme_prepare_pgd(ppd);
	if (!pmd_p)
		return;

	pmd_p += pmd_index(ppd->vaddr);
	if (!native_pmd_val(*pmd_p) || !(native_pmd_val(*pmd_p) & _PAGE_PSE))
		native_set_pmd(pmd_p, native_make_pmd(ppd->paddr | ppd->pmd_flags));
}

static void __init sme_populate_pgd(struct sme_populate_pgd_data *ppd)
{
	pmd_t *pmd_p;
	pte_t *pte_p;

	pmd_p = sme_prepare_pgd(ppd);
	if (!pmd_p)
		return;

	pmd_p += pmd_index(ppd->vaddr);
	if (native_pmd_val(*pmd_p)) {
		if (native_pmd_val(*pmd_p) & _PAGE_PSE)
			return;

		pte_p = (pte_t *)(native_pmd_val(*pmd_p) & ~PTE_FLAGS_MASK);
	} else {
		pmd_t pmd;

		pte_p = ppd->pgtable_area;
		memset(pte_p, 0, sizeof(*pte_p) * PTRS_PER_PTE);
		ppd->pgtable_area += sizeof(*pte_p) * PTRS_PER_PTE;

		pmd = native_make_pmd((pteval_t)pte_p + PMD_FLAGS);
		native_set_pmd(pmd_p, pmd);
	}

	pte_p += pte_index(ppd->vaddr);
	if (!native_pte_val(*pte_p))
		native_set_pte(pte_p, native_make_pte(ppd->paddr | ppd->pte_flags));
}

static void __init __sme_map_range_pmd(struct sme_populate_pgd_data *ppd)
{
	while (ppd->vaddr < ppd->vaddr_end) {
		sme_populate_pgd_large(ppd);

		ppd->vaddr += PMD_PAGE_SIZE;
		ppd->paddr += PMD_PAGE_SIZE;
	}
}

static void __init __sme_map_range_pte(struct sme_populate_pgd_data *ppd)
{
	while (ppd->vaddr < ppd->vaddr_end) {
		sme_populate_pgd(ppd);

		ppd->vaddr += PAGE_SIZE;
		ppd->paddr += PAGE_SIZE;
	}
}

static void __init __sme_map_range(struct sme_populate_pgd_data *ppd,
				   pmdval_t pmd_flags, pteval_t pte_flags)
{
	unsigned long vaddr_end;

	ppd->pmd_flags = pmd_flags;
	ppd->pte_flags = pte_flags;

	/* Save original end value since we modify the struct value */
	vaddr_end = ppd->vaddr_end;

	/* If start is not 2MB aligned, create PTE entries */
	ppd->vaddr_end = ALIGN(ppd->vaddr, PMD_PAGE_SIZE);
	__sme_map_range_pte(ppd);

	/* Create PMD entries */
	ppd->vaddr_end = vaddr_end & PMD_PAGE_MASK;
	__sme_map_range_pmd(ppd);

	/* If end is not 2MB aligned, create PTE entries */
	ppd->vaddr_end = vaddr_end;
	__sme_map_range_pte(ppd);
}

static void __init sme_map_range_encrypted(struct sme_populate_pgd_data *ppd)
{
	__sme_map_range(ppd, PMD_FLAGS_ENC, PTE_FLAGS_ENC);
}

static void __init sme_map_range_decrypted(struct sme_populate_pgd_data *ppd)
{
	__sme_map_range(ppd, PMD_FLAGS_DEC, PTE_FLAGS_DEC);
}

static void __init sme_map_range_decrypted_wp(struct sme_populate_pgd_data *ppd)
{
	__sme_map_range(ppd, PMD_FLAGS_DEC_WP, PTE_FLAGS_DEC_WP);
}

static unsigned long __init sme_pgtable_calc(unsigned long len)
{
	unsigned long p4d_size, pud_size, pmd_size, pte_size;
	unsigned long total;

	/*
	 * Perform a relatively simplistic calculation of the pagetable
	 * entries that are needed. Those mappings will be covered mostly
	 * by 2MB PMD entries so we can conservatively calculate the required
	 * number of P4D, PUD and PMD structures needed to perform the
	 * mappings.  For mappings that are not 2MB aligned, PTE mappings
	 * would be needed for the start and end portion of the address range
	 * that fall outside of the 2MB alignment.  This results in, at most,
	 * two extra pages to hold PTE entries for each range that is mapped.
	 * Incrementing the count for each covers the case where the addresses
	 * cross entries.
	 */
	if (IS_ENABLED(CONFIG_X86_5LEVEL)) {
		p4d_size = (ALIGN(len, PGDIR_SIZE) / PGDIR_SIZE) + 1;
		p4d_size *= sizeof(p4d_t) * PTRS_PER_P4D;
		pud_size = (ALIGN(len, P4D_SIZE) / P4D_SIZE) + 1;
		pud_size *= sizeof(pud_t) * PTRS_PER_PUD;
	} else {
		p4d_size = 0;
		pud_size = (ALIGN(len, PGDIR_SIZE) / PGDIR_SIZE) + 1;
		pud_size *= sizeof(pud_t) * PTRS_PER_PUD;
	}
	pmd_size = (ALIGN(len, PUD_SIZE) / PUD_SIZE) + 1;
	pmd_size *= sizeof(pmd_t) * PTRS_PER_PMD;
	pte_size = 2 * sizeof(pte_t) * PTRS_PER_PTE;

	total = p4d_size + pud_size + pmd_size + pte_size;

	/*
	 * Now calculate the added pagetable structures needed to populate
	 * the new pagetables.
	 */
	if (IS_ENABLED(CONFIG_X86_5LEVEL)) {
		p4d_size = ALIGN(total, PGDIR_SIZE) / PGDIR_SIZE;
		p4d_size *= sizeof(p4d_t) * PTRS_PER_P4D;
		pud_size = ALIGN(total, P4D_SIZE) / P4D_SIZE;
		pud_size *= sizeof(pud_t) * PTRS_PER_PUD;
	} else {
		p4d_size = 0;
		pud_size = ALIGN(total, PGDIR_SIZE) / PGDIR_SIZE;
		pud_size *= sizeof(pud_t) * PTRS_PER_PUD;
	}
	pmd_size = ALIGN(total, PUD_SIZE) / PUD_SIZE;
	pmd_size *= sizeof(pmd_t) * PTRS_PER_PMD;

	total += p4d_size + pud_size + pmd_size;

	return total;
}

void __init __nostackprotector sme_encrypt_kernel(struct boot_params *bp)
{
	unsigned long workarea_start, workarea_end, workarea_len;
	unsigned long execute_start, execute_end, execute_len;
	unsigned long kernel_start, kernel_end, kernel_len;
	unsigned long initrd_start, initrd_end, initrd_len;
	struct sme_populate_pgd_data ppd;
	unsigned long pgtable_area_len;
	unsigned long decrypted_base;

	if (!sme_active())
		return;

	/*
	 * Prepare for encrypting the kernel and initrd by building new
	 * pagetables with the necessary attributes needed to encrypt the
	 * kernel in place.
	 *
	 *   One range of virtual addresses will map the memory occupied
	 *   by the kernel and initrd as encrypted.
	 *
	 *   Another range of virtual addresses will map the memory occupied
	 *   by the kernel and initrd as decrypted and write-protected.
	 *
	 *     The use of write-protect attribute will prevent any of the
	 *     memory from being cached.
	 */

	/* Physical addresses gives us the identity mapped virtual addresses */
	kernel_start = __pa_symbol(_text);
	kernel_end = ALIGN(__pa_symbol(_end), PMD_PAGE_SIZE);
	kernel_len = kernel_end - kernel_start;

	initrd_start = 0;
	initrd_end = 0;
	initrd_len = 0;
#ifdef CONFIG_BLK_DEV_INITRD
	initrd_len = (unsigned long)bp->hdr.ramdisk_size |
		     ((unsigned long)bp->ext_ramdisk_size << 32);
	if (initrd_len) {
		initrd_start = (unsigned long)bp->hdr.ramdisk_image |
			       ((unsigned long)bp->ext_ramdisk_image << 32);
		initrd_end = PAGE_ALIGN(initrd_start + initrd_len);
		initrd_len = initrd_end - initrd_start;
	}
#endif

	/* Set the encryption workarea to be immediately after the kernel */
	workarea_start = kernel_end;

	/*
	 * Calculate required number of workarea bytes needed:
	 *   executable encryption area size:
	 *     stack page (PAGE_SIZE)
	 *     encryption routine page (PAGE_SIZE)
	 *     intermediate copy buffer (PMD_PAGE_SIZE)
	 *   pagetable structures for the encryption of the kernel
	 *   pagetable structures for workarea (in case not currently mapped)
	 */
	execute_start = workarea_start;
	execute_end = execute_start + (PAGE_SIZE * 2) + PMD_PAGE_SIZE;
	execute_len = execute_end - execute_start;

	/*
	 * One PGD for both encrypted and decrypted mappings and a set of
	 * PUDs and PMDs for each of the encrypted and decrypted mappings.
	 */
	pgtable_area_len = sizeof(pgd_t) * PTRS_PER_PGD;
	pgtable_area_len += sme_pgtable_calc(execute_end - kernel_start) * 2;
	if (initrd_len)
		pgtable_area_len += sme_pgtable_calc(initrd_len) * 2;

	/* PUDs and PMDs needed in the current pagetables for the workarea */
	pgtable_area_len += sme_pgtable_calc(execute_len + pgtable_area_len);

	/*
	 * The total workarea includes the executable encryption area and
	 * the pagetable area. The start of the workarea is already 2MB
	 * aligned, align the end of the workarea on a 2MB boundary so that
	 * we don't try to create/allocate PTE entries from the workarea
	 * before it is mapped.
	 */
	workarea_len = execute_len + pgtable_area_len;
	workarea_end = ALIGN(workarea_start + workarea_len, PMD_PAGE_SIZE);

	/*
	 * Set the address to the start of where newly created pagetable
	 * structures (PGDs, PUDs and PMDs) will be allocated. New pagetable
	 * structures are created when the workarea is added to the current
	 * pagetables and when the new encrypted and decrypted kernel
	 * mappings are populated.
	 */
	ppd.pgtable_area = (void *)execute_end;

	/*
	 * Make sure the current pagetable structure has entries for
	 * addressing the workarea.
	 */
	ppd.pgd = (pgd_t *)native_read_cr3_pa();
	ppd.paddr = workarea_start;
	ppd.vaddr = workarea_start;
	ppd.vaddr_end = workarea_end;
	sme_map_range_decrypted(&ppd);

	/* Flush the TLB - no globals so cr3 is enough */
	native_write_cr3(__native_read_cr3());

	/*
	 * A new pagetable structure is being built to allow for the kernel
	 * and initrd to be encrypted. It starts with an empty PGD that will
	 * then be populated with new PUDs and PMDs as the encrypted and
	 * decrypted kernel mappings are created.
	 */
	ppd.pgd = ppd.pgtable_area;
	memset(ppd.pgd, 0, sizeof(pgd_t) * PTRS_PER_PGD);
	ppd.pgtable_area += sizeof(pgd_t) * PTRS_PER_PGD;

	/*
	 * A different PGD index/entry must be used to get different
	 * pagetable entries for the decrypted mapping. Choose the next
	 * PGD index and convert it to a virtual address to be used as
	 * the base of the mapping.
	 */
	decrypted_base = (pgd_index(workarea_end) + 1) & (PTRS_PER_PGD - 1);
	if (initrd_len) {
		unsigned long check_base;

		check_base = (pgd_index(initrd_end) + 1) & (PTRS_PER_PGD - 1);
		decrypted_base = max(decrypted_base, check_base);
	}
	decrypted_base <<= PGDIR_SHIFT;

	/* Add encrypted kernel (identity) mappings */
	ppd.paddr = kernel_start;
	ppd.vaddr = kernel_start;
	ppd.vaddr_end = kernel_end;
	sme_map_range_encrypted(&ppd);

	/* Add decrypted, write-protected kernel (non-identity) mappings */
	ppd.paddr = kernel_start;
	ppd.vaddr = kernel_start + decrypted_base;
	ppd.vaddr_end = kernel_end + decrypted_base;
	sme_map_range_decrypted_wp(&ppd);

	if (initrd_len) {
		/* Add encrypted initrd (identity) mappings */
		ppd.paddr = initrd_start;
		ppd.vaddr = initrd_start;
		ppd.vaddr_end = initrd_end;
		sme_map_range_encrypted(&ppd);
		/*
		 * Add decrypted, write-protected initrd (non-identity) mappings
		 */
		ppd.paddr = initrd_start;
		ppd.vaddr = initrd_start + decrypted_base;
		ppd.vaddr_end = initrd_end + decrypted_base;
		sme_map_range_decrypted_wp(&ppd);
	}

	/* Add decrypted workarea mappings to both kernel mappings */
	ppd.paddr = workarea_start;
	ppd.vaddr = workarea_start;
	ppd.vaddr_end = workarea_end;
	sme_map_range_decrypted(&ppd);

	ppd.paddr = workarea_start;
	ppd.vaddr = workarea_start + decrypted_base;
	ppd.vaddr_end = workarea_end + decrypted_base;
	sme_map_range_decrypted(&ppd);

	/* Perform the encryption */
	sme_encrypt_execute(kernel_start, kernel_start + decrypted_base,
			    kernel_len, workarea_start, (unsigned long)ppd.pgd);

	if (initrd_len)
		sme_encrypt_execute(initrd_start, initrd_start + decrypted_base,
				    initrd_len, workarea_start,
				    (unsigned long)ppd.pgd);

	/*
	 * At this point we are running encrypted.  Remove the mappings for
	 * the decrypted areas - all that is needed for this is to remove
	 * the PGD entry/entries.
	 */
	ppd.vaddr = kernel_start + decrypted_base;
	ppd.vaddr_end = kernel_end + decrypted_base;
	sme_clear_pgd(&ppd);

	if (initrd_len) {
		ppd.vaddr = initrd_start + decrypted_base;
		ppd.vaddr_end = initrd_end + decrypted_base;
		sme_clear_pgd(&ppd);
	}

	ppd.vaddr = workarea_start + decrypted_base;
	ppd.vaddr_end = workarea_end + decrypted_base;
	sme_clear_pgd(&ppd);

	/* Flush the TLB - no globals so cr3 is enough */
	native_write_cr3(__native_read_cr3());
}

void __init __nostackprotector sme_enable(struct boot_params *bp)
{
	const char *cmdline_ptr, *cmdline_arg, *cmdline_on, *cmdline_off;
	unsigned int eax, ebx, ecx, edx;
	bool active_by_default;
	unsigned long me_mask;
	char buffer[16];
	u64 msr;

	/* Check for the SME support leaf */
	eax = 0x80000000;
	ecx = 0;
	native_cpuid(&eax, &ebx, &ecx, &edx);
	if (eax < 0x8000001f)
		return;

	/*
	 * Check for the SME feature:
	 *   CPUID Fn8000_001F[EAX] - Bit 0
	 *     Secure Memory Encryption support
	 *   CPUID Fn8000_001F[EBX] - Bits 5:0
	 *     Pagetable bit position used to indicate encryption
	 */
	eax = 0x8000001f;
	ecx = 0;
	native_cpuid(&eax, &ebx, &ecx, &edx);
	if (!(eax & 1))
		return;

	me_mask = 1UL << (ebx & 0x3f);

	/* Check if SME is enabled */
	msr = __rdmsr(MSR_K8_SYSCFG);
	if (!(msr & MSR_K8_SYSCFG_MEM_ENCRYPT))
		return;

	/*
	 * Fixups have not been applied to phys_base yet and we're running
	 * identity mapped, so we must obtain the address to the SME command
	 * line argument data using rip-relative addressing.
	 */
	asm ("lea sme_cmdline_arg(%%rip), %0"
	     : "=r" (cmdline_arg)
	     : "p" (sme_cmdline_arg));
	asm ("lea sme_cmdline_on(%%rip), %0"
	     : "=r" (cmdline_on)
	     : "p" (sme_cmdline_on));
	asm ("lea sme_cmdline_off(%%rip), %0"
	     : "=r" (cmdline_off)
	     : "p" (sme_cmdline_off));

	if (IS_ENABLED(CONFIG_AMD_MEM_ENCRYPT_ACTIVE_BY_DEFAULT))
		active_by_default = true;
	else
		active_by_default = false;

	cmdline_ptr = (const char *)((u64)bp->hdr.cmd_line_ptr |
				     ((u64)bp->ext_cmd_line_ptr << 32));

	cmdline_find_option(cmdline_ptr, cmdline_arg, buffer, sizeof(buffer));

	if (!strncmp(buffer, cmdline_on, sizeof(buffer)))
		sme_me_mask = me_mask;
	else if (!strncmp(buffer, cmdline_off, sizeof(buffer)))
		sme_me_mask = 0;
	else
		sme_me_mask = active_by_default ? me_mask : 0;
}

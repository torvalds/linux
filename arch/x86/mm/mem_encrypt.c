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

#include "mm_internal.h"

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
DEFINE_STATIC_KEY_FALSE(sev_enable_key);
EXPORT_SYMBOL_GPL(sev_enable_key);

static bool sev_enabled __section(.data);

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

	if (sev_active())
		swiotlb_force = SWIOTLB_FORCE;
}

static void *sev_alloc(struct device *dev, size_t size, dma_addr_t *dma_handle,
		       gfp_t gfp, unsigned long attrs)
{
	unsigned long dma_mask;
	unsigned int order;
	struct page *page;
	void *vaddr = NULL;

	dma_mask = dma_alloc_coherent_mask(dev, gfp);
	order = get_order(size);

	/*
	 * Memory will be memset to zero after marking decrypted, so don't
	 * bother clearing it before.
	 */
	gfp &= ~__GFP_ZERO;

	page = alloc_pages_node(dev_to_node(dev), gfp, order);
	if (page) {
		dma_addr_t addr;

		/*
		 * Since we will be clearing the encryption bit, check the
		 * mask with it already cleared.
		 */
		addr = __sme_clr(phys_to_dma(dev, page_to_phys(page)));
		if ((addr + size) > dma_mask) {
			__free_pages(page, get_order(size));
		} else {
			vaddr = page_address(page);
			*dma_handle = addr;
		}
	}

	if (!vaddr)
		vaddr = swiotlb_alloc_coherent(dev, size, dma_handle, gfp);

	if (!vaddr)
		return NULL;

	/* Clear the SME encryption bit for DMA use if not swiotlb area */
	if (!is_swiotlb_buffer(dma_to_phys(dev, *dma_handle))) {
		set_memory_decrypted((unsigned long)vaddr, 1 << order);
		memset(vaddr, 0, PAGE_SIZE << order);
		*dma_handle = __sme_clr(*dma_handle);
	}

	return vaddr;
}

static void sev_free(struct device *dev, size_t size, void *vaddr,
		     dma_addr_t dma_handle, unsigned long attrs)
{
	/* Set the SME encryption bit for re-use if not swiotlb area */
	if (!is_swiotlb_buffer(dma_to_phys(dev, dma_handle)))
		set_memory_encrypted((unsigned long)vaddr,
				     1 << get_order(size));

	swiotlb_free_coherent(dev, size, vaddr, dma_handle);
}

static void __init __set_clr_pte_enc(pte_t *kpte, int level, bool enc)
{
	pgprot_t old_prot, new_prot;
	unsigned long pfn, pa, size;
	pte_t new_pte;

	switch (level) {
	case PG_LEVEL_4K:
		pfn = pte_pfn(*kpte);
		old_prot = pte_pgprot(*kpte);
		break;
	case PG_LEVEL_2M:
		pfn = pmd_pfn(*(pmd_t *)kpte);
		old_prot = pmd_pgprot(*(pmd_t *)kpte);
		break;
	case PG_LEVEL_1G:
		pfn = pud_pfn(*(pud_t *)kpte);
		old_prot = pud_pgprot(*(pud_t *)kpte);
		break;
	default:
		return;
	}

	new_prot = old_prot;
	if (enc)
		pgprot_val(new_prot) |= _PAGE_ENC;
	else
		pgprot_val(new_prot) &= ~_PAGE_ENC;

	/* If prot is same then do nothing. */
	if (pgprot_val(old_prot) == pgprot_val(new_prot))
		return;

	pa = pfn << page_level_shift(level);
	size = page_level_size(level);

	/*
	 * We are going to perform in-place en-/decryption and change the
	 * physical page attribute from C=1 to C=0 or vice versa. Flush the
	 * caches to ensure that data gets accessed with the correct C-bit.
	 */
	clflush_cache_range(__va(pa), size);

	/* Encrypt/decrypt the contents in-place */
	if (enc)
		sme_early_encrypt(pa, size);
	else
		sme_early_decrypt(pa, size);

	/* Change the page encryption mask. */
	new_pte = pfn_pte(pfn, new_prot);
	set_pte_atomic(kpte, new_pte);
}

static int __init early_set_memory_enc_dec(unsigned long vaddr,
					   unsigned long size, bool enc)
{
	unsigned long vaddr_end, vaddr_next;
	unsigned long psize, pmask;
	int split_page_size_mask;
	int level, ret;
	pte_t *kpte;

	vaddr_next = vaddr;
	vaddr_end = vaddr + size;

	for (; vaddr < vaddr_end; vaddr = vaddr_next) {
		kpte = lookup_address(vaddr, &level);
		if (!kpte || pte_none(*kpte)) {
			ret = 1;
			goto out;
		}

		if (level == PG_LEVEL_4K) {
			__set_clr_pte_enc(kpte, level, enc);
			vaddr_next = (vaddr & PAGE_MASK) + PAGE_SIZE;
			continue;
		}

		psize = page_level_size(level);
		pmask = page_level_mask(level);

		/*
		 * Check whether we can change the large page in one go.
		 * We request a split when the address is not aligned and
		 * the number of pages to set/clear encryption bit is smaller
		 * than the number of pages in the large page.
		 */
		if (vaddr == (vaddr & pmask) &&
		    ((vaddr_end - vaddr) >= psize)) {
			__set_clr_pte_enc(kpte, level, enc);
			vaddr_next = (vaddr & pmask) + psize;
			continue;
		}

		/*
		 * The virtual address is part of a larger page, create the next
		 * level page table mapping (4K or 2M). If it is part of a 2M
		 * page then we request a split of the large page into 4K
		 * chunks. A 1GB large page is split into 2M pages, resp.
		 */
		if (level == PG_LEVEL_2M)
			split_page_size_mask = 0;
		else
			split_page_size_mask = 1 << PG_LEVEL_2M;

		kernel_physical_mapping_init(__pa(vaddr & pmask),
					     __pa((vaddr_end & pmask) + psize),
					     split_page_size_mask);
	}

	ret = 0;

out:
	__flush_tlb_all();
	return ret;
}

int __init early_set_memory_decrypted(unsigned long vaddr, unsigned long size)
{
	return early_set_memory_enc_dec(vaddr, size, false);
}

int __init early_set_memory_encrypted(unsigned long vaddr, unsigned long size)
{
	return early_set_memory_enc_dec(vaddr, size, true);
}

/*
 * SME and SEV are very similar but they are not the same, so there are
 * times that the kernel will need to distinguish between SME and SEV. The
 * sme_active() and sev_active() functions are used for this.  When a
 * distinction isn't needed, the mem_encrypt_active() function can be used.
 *
 * The trampoline code is a good example for this requirement.  Before
 * paging is activated, SME will access all memory as decrypted, but SEV
 * will access all memory as encrypted.  So, when APs are being brought
 * up under SME the trampoline area cannot be encrypted, whereas under SEV
 * the trampoline area must be encrypted.
 */
bool sme_active(void)
{
	return sme_me_mask && !sev_enabled;
}
EXPORT_SYMBOL_GPL(sme_active);

bool sev_active(void)
{
	return sme_me_mask && sev_enabled;
}
EXPORT_SYMBOL_GPL(sev_active);

static const struct dma_map_ops sev_dma_ops = {
	.alloc                  = sev_alloc,
	.free                   = sev_free,
	.map_page               = swiotlb_map_page,
	.unmap_page             = swiotlb_unmap_page,
	.map_sg                 = swiotlb_map_sg_attrs,
	.unmap_sg               = swiotlb_unmap_sg_attrs,
	.sync_single_for_cpu    = swiotlb_sync_single_for_cpu,
	.sync_single_for_device = swiotlb_sync_single_for_device,
	.sync_sg_for_cpu        = swiotlb_sync_sg_for_cpu,
	.sync_sg_for_device     = swiotlb_sync_sg_for_device,
	.mapping_error          = swiotlb_dma_mapping_error,
};

/* Architecture __weak replacement functions */
void __init mem_encrypt_init(void)
{
	if (!sme_me_mask)
		return;

	/* Call into SWIOTLB to update the SWIOTLB DMA buffers */
	swiotlb_update_mem_attributes();

	/*
	 * With SEV, DMA operations cannot use encryption. New DMA ops
	 * are required in order to mark the DMA areas as decrypted or
	 * to use bounce buffers.
	 */
	if (sev_active())
		dma_ops = &sev_dma_ops;

	/*
	 * With SEV, we need to unroll the rep string I/O instructions.
	 */
	if (sev_active())
		static_branch_enable(&sev_enable_key);

	pr_info("AMD %s active\n",
		sev_active() ? "Secure Encrypted Virtualization (SEV)"
			     : "Secure Memory Encryption (SME)");
}

void swiotlb_set_mem_attributes(void *vaddr, unsigned long size)
{
	WARN(PAGE_ALIGN(size) != size,
	     "size is not page-aligned (%#lx)\n", size);

	/* Make the SWIOTLB buffer area decrypted */
	set_memory_decrypted((unsigned long)vaddr, size >> PAGE_SHIFT);
}

static void __init sme_clear_pgd(pgd_t *pgd_base, unsigned long start,
				 unsigned long end)
{
	unsigned long pgd_start, pgd_end, pgd_size;
	pgd_t *pgd_p;

	pgd_start = start & PGDIR_MASK;
	pgd_end = end & PGDIR_MASK;

	pgd_size = (((pgd_end - pgd_start) / PGDIR_SIZE) + 1);
	pgd_size *= sizeof(pgd_t);

	pgd_p = pgd_base + pgd_index(start);

	memset(pgd_p, 0, pgd_size);
}

#define PGD_FLAGS	_KERNPG_TABLE_NOENC
#define P4D_FLAGS	_KERNPG_TABLE_NOENC
#define PUD_FLAGS	_KERNPG_TABLE_NOENC
#define PMD_FLAGS	(__PAGE_KERNEL_LARGE_EXEC & ~_PAGE_GLOBAL)

static void __init *sme_populate_pgd(pgd_t *pgd_base, void *pgtable_area,
				     unsigned long vaddr, pmdval_t pmd_val)
{
	pgd_t *pgd_p;
	p4d_t *p4d_p;
	pud_t *pud_p;
	pmd_t *pmd_p;

	pgd_p = pgd_base + pgd_index(vaddr);
	if (native_pgd_val(*pgd_p)) {
		if (IS_ENABLED(CONFIG_X86_5LEVEL))
			p4d_p = (p4d_t *)(native_pgd_val(*pgd_p) & ~PTE_FLAGS_MASK);
		else
			pud_p = (pud_t *)(native_pgd_val(*pgd_p) & ~PTE_FLAGS_MASK);
	} else {
		pgd_t pgd;

		if (IS_ENABLED(CONFIG_X86_5LEVEL)) {
			p4d_p = pgtable_area;
			memset(p4d_p, 0, sizeof(*p4d_p) * PTRS_PER_P4D);
			pgtable_area += sizeof(*p4d_p) * PTRS_PER_P4D;

			pgd = native_make_pgd((pgdval_t)p4d_p + PGD_FLAGS);
		} else {
			pud_p = pgtable_area;
			memset(pud_p, 0, sizeof(*pud_p) * PTRS_PER_PUD);
			pgtable_area += sizeof(*pud_p) * PTRS_PER_PUD;

			pgd = native_make_pgd((pgdval_t)pud_p + PGD_FLAGS);
		}
		native_set_pgd(pgd_p, pgd);
	}

	if (IS_ENABLED(CONFIG_X86_5LEVEL)) {
		p4d_p += p4d_index(vaddr);
		if (native_p4d_val(*p4d_p)) {
			pud_p = (pud_t *)(native_p4d_val(*p4d_p) & ~PTE_FLAGS_MASK);
		} else {
			p4d_t p4d;

			pud_p = pgtable_area;
			memset(pud_p, 0, sizeof(*pud_p) * PTRS_PER_PUD);
			pgtable_area += sizeof(*pud_p) * PTRS_PER_PUD;

			p4d = native_make_p4d((pudval_t)pud_p + P4D_FLAGS);
			native_set_p4d(p4d_p, p4d);
		}
	}

	pud_p += pud_index(vaddr);
	if (native_pud_val(*pud_p)) {
		if (native_pud_val(*pud_p) & _PAGE_PSE)
			goto out;

		pmd_p = (pmd_t *)(native_pud_val(*pud_p) & ~PTE_FLAGS_MASK);
	} else {
		pud_t pud;

		pmd_p = pgtable_area;
		memset(pmd_p, 0, sizeof(*pmd_p) * PTRS_PER_PMD);
		pgtable_area += sizeof(*pmd_p) * PTRS_PER_PMD;

		pud = native_make_pud((pmdval_t)pmd_p + PUD_FLAGS);
		native_set_pud(pud_p, pud);
	}

	pmd_p += pmd_index(vaddr);
	if (!native_pmd_val(*pmd_p) || !(native_pmd_val(*pmd_p) & _PAGE_PSE))
		native_set_pmd(pmd_p, native_make_pmd(pmd_val));

out:
	return pgtable_area;
}

static unsigned long __init sme_pgtable_calc(unsigned long len)
{
	unsigned long p4d_size, pud_size, pmd_size;
	unsigned long total;

	/*
	 * Perform a relatively simplistic calculation of the pagetable
	 * entries that are needed. That mappings will be covered by 2MB
	 * PMD entries so we can conservatively calculate the required
	 * number of P4D, PUD and PMD structures needed to perform the
	 * mappings. Incrementing the count for each covers the case where
	 * the addresses cross entries.
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

	total = p4d_size + pud_size + pmd_size;

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

void __init sme_encrypt_kernel(void)
{
	unsigned long workarea_start, workarea_end, workarea_len;
	unsigned long execute_start, execute_end, execute_len;
	unsigned long kernel_start, kernel_end, kernel_len;
	unsigned long pgtable_area_len;
	unsigned long paddr, pmd_flags;
	unsigned long decrypted_base;
	void *pgtable_area;
	pgd_t *pgd;

	if (!sme_active())
		return;

	/*
	 * Prepare for encrypting the kernel by building new pagetables with
	 * the necessary attributes needed to encrypt the kernel in place.
	 *
	 *   One range of virtual addresses will map the memory occupied
	 *   by the kernel as encrypted.
	 *
	 *   Another range of virtual addresses will map the memory occupied
	 *   by the kernel as decrypted and write-protected.
	 *
	 *     The use of write-protect attribute will prevent any of the
	 *     memory from being cached.
	 */

	/* Physical addresses gives us the identity mapped virtual addresses */
	kernel_start = __pa_symbol(_text);
	kernel_end = ALIGN(__pa_symbol(_end), PMD_PAGE_SIZE);
	kernel_len = kernel_end - kernel_start;

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

	/* PUDs and PMDs needed in the current pagetables for the workarea */
	pgtable_area_len += sme_pgtable_calc(execute_len + pgtable_area_len);

	/*
	 * The total workarea includes the executable encryption area and
	 * the pagetable area.
	 */
	workarea_len = execute_len + pgtable_area_len;
	workarea_end = workarea_start + workarea_len;

	/*
	 * Set the address to the start of where newly created pagetable
	 * structures (PGDs, PUDs and PMDs) will be allocated. New pagetable
	 * structures are created when the workarea is added to the current
	 * pagetables and when the new encrypted and decrypted kernel
	 * mappings are populated.
	 */
	pgtable_area = (void *)execute_end;

	/*
	 * Make sure the current pagetable structure has entries for
	 * addressing the workarea.
	 */
	pgd = (pgd_t *)native_read_cr3_pa();
	paddr = workarea_start;
	while (paddr < workarea_end) {
		pgtable_area = sme_populate_pgd(pgd, pgtable_area,
						paddr,
						paddr + PMD_FLAGS);

		paddr += PMD_PAGE_SIZE;
	}

	/* Flush the TLB - no globals so cr3 is enough */
	native_write_cr3(__native_read_cr3());

	/*
	 * A new pagetable structure is being built to allow for the kernel
	 * to be encrypted. It starts with an empty PGD that will then be
	 * populated with new PUDs and PMDs as the encrypted and decrypted
	 * kernel mappings are created.
	 */
	pgd = pgtable_area;
	memset(pgd, 0, sizeof(*pgd) * PTRS_PER_PGD);
	pgtable_area += sizeof(*pgd) * PTRS_PER_PGD;

	/* Add encrypted kernel (identity) mappings */
	pmd_flags = PMD_FLAGS | _PAGE_ENC;
	paddr = kernel_start;
	while (paddr < kernel_end) {
		pgtable_area = sme_populate_pgd(pgd, pgtable_area,
						paddr,
						paddr + pmd_flags);

		paddr += PMD_PAGE_SIZE;
	}

	/*
	 * A different PGD index/entry must be used to get different
	 * pagetable entries for the decrypted mapping. Choose the next
	 * PGD index and convert it to a virtual address to be used as
	 * the base of the mapping.
	 */
	decrypted_base = (pgd_index(workarea_end) + 1) & (PTRS_PER_PGD - 1);
	decrypted_base <<= PGDIR_SHIFT;

	/* Add decrypted, write-protected kernel (non-identity) mappings */
	pmd_flags = (PMD_FLAGS & ~_PAGE_CACHE_MASK) | (_PAGE_PAT | _PAGE_PWT);
	paddr = kernel_start;
	while (paddr < kernel_end) {
		pgtable_area = sme_populate_pgd(pgd, pgtable_area,
						paddr + decrypted_base,
						paddr + pmd_flags);

		paddr += PMD_PAGE_SIZE;
	}

	/* Add decrypted workarea mappings to both kernel mappings */
	paddr = workarea_start;
	while (paddr < workarea_end) {
		pgtable_area = sme_populate_pgd(pgd, pgtable_area,
						paddr,
						paddr + PMD_FLAGS);

		pgtable_area = sme_populate_pgd(pgd, pgtable_area,
						paddr + decrypted_base,
						paddr + PMD_FLAGS);

		paddr += PMD_PAGE_SIZE;
	}

	/* Perform the encryption */
	sme_encrypt_execute(kernel_start, kernel_start + decrypted_base,
			    kernel_len, workarea_start, (unsigned long)pgd);

	/*
	 * At this point we are running encrypted.  Remove the mappings for
	 * the decrypted areas - all that is needed for this is to remove
	 * the PGD entry/entries.
	 */
	sme_clear_pgd(pgd, kernel_start + decrypted_base,
		      kernel_end + decrypted_base);

	sme_clear_pgd(pgd, workarea_start + decrypted_base,
		      workarea_end + decrypted_base);

	/* Flush the TLB - no globals so cr3 is enough */
	native_write_cr3(__native_read_cr3());
}

void __init __nostackprotector sme_enable(struct boot_params *bp)
{
	const char *cmdline_ptr, *cmdline_arg, *cmdline_on, *cmdline_off;
	unsigned int eax, ebx, ecx, edx;
	unsigned long feature_mask;
	bool active_by_default;
	unsigned long me_mask;
	char buffer[16];
	u64 msr;

	/* Check for the SME/SEV support leaf */
	eax = 0x80000000;
	ecx = 0;
	native_cpuid(&eax, &ebx, &ecx, &edx);
	if (eax < 0x8000001f)
		return;

#define AMD_SME_BIT	BIT(0)
#define AMD_SEV_BIT	BIT(1)
	/*
	 * Set the feature mask (SME or SEV) based on whether we are
	 * running under a hypervisor.
	 */
	eax = 1;
	ecx = 0;
	native_cpuid(&eax, &ebx, &ecx, &edx);
	feature_mask = (ecx & BIT(31)) ? AMD_SEV_BIT : AMD_SME_BIT;

	/*
	 * Check for the SME/SEV feature:
	 *   CPUID Fn8000_001F[EAX]
	 *   - Bit 0 - Secure Memory Encryption support
	 *   - Bit 1 - Secure Encrypted Virtualization support
	 *   CPUID Fn8000_001F[EBX]
	 *   - Bits 5:0 - Pagetable bit position used to indicate encryption
	 */
	eax = 0x8000001f;
	ecx = 0;
	native_cpuid(&eax, &ebx, &ecx, &edx);
	if (!(eax & feature_mask))
		return;

	me_mask = 1UL << (ebx & 0x3f);

	/* Check if memory encryption is enabled */
	if (feature_mask == AMD_SME_BIT) {
		/* For SME, check the SYSCFG MSR */
		msr = __rdmsr(MSR_K8_SYSCFG);
		if (!(msr & MSR_K8_SYSCFG_MEM_ENCRYPT))
			return;
	} else {
		/* For SEV, check the SEV MSR */
		msr = __rdmsr(MSR_AMD64_SEV);
		if (!(msr & MSR_AMD64_SEV_ENABLED))
			return;

		/* SEV state cannot be controlled by a command line option */
		sme_me_mask = me_mask;
		sev_enabled = true;
		return;
	}

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

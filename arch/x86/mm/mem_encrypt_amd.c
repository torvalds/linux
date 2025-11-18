// SPDX-License-Identifier: GPL-2.0-only
/*
 * AMD Memory Encryption Support
 *
 * Copyright (C) 2016-2024 Advanced Micro Devices, Inc.
 *
 * Author: Tom Lendacky <thomas.lendacky@amd.com>
 */

#include <linux/linkage.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/dma-direct.h>
#include <linux/swiotlb.h>
#include <linux/mem_encrypt.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/bitops.h>
#include <linux/dma-mapping.h>
#include <linux/cc_platform.h>

#include <asm/tlbflush.h>
#include <asm/fixmap.h>
#include <asm/setup.h>
#include <asm/mem_encrypt.h>
#include <asm/bootparam.h>
#include <asm/set_memory.h>
#include <asm/cacheflush.h>
#include <asm/processor-flags.h>
#include <asm/msr.h>
#include <asm/cmdline.h>
#include <asm/sev.h>
#include <asm/ia32.h>

#include "mm_internal.h"

/*
 * Since SME related variables are set early in the boot process they must
 * reside in the .data section so as not to be zeroed out when the .bss
 * section is later cleared.
 */
u64 sme_me_mask __section(".data") = 0;
SYM_PIC_ALIAS(sme_me_mask);
u64 sev_status __section(".data") = 0;
SYM_PIC_ALIAS(sev_status);
u64 sev_check_data __section(".data") = 0;
EXPORT_SYMBOL(sme_me_mask);

/* Buffer used for early in-place encryption by BSP, no locking needed */
static char sme_early_buffer[PAGE_SIZE] __initdata __aligned(PAGE_SIZE);

/*
 * SNP-specific routine which needs to additionally change the page state from
 * private to shared before copying the data from the source to destination and
 * restore after the copy.
 */
static inline void __init snp_memcpy(void *dst, void *src, size_t sz,
				     unsigned long paddr, bool decrypt)
{
	unsigned long npages = PAGE_ALIGN(sz) >> PAGE_SHIFT;

	if (decrypt) {
		/*
		 * @paddr needs to be accessed decrypted, mark the page shared in
		 * the RMP table before copying it.
		 */
		early_snp_set_memory_shared((unsigned long)__va(paddr), paddr, npages);

		memcpy(dst, src, sz);

		/* Restore the page state after the memcpy. */
		early_snp_set_memory_private((unsigned long)__va(paddr), paddr, npages);
	} else {
		/*
		 * @paddr need to be accessed encrypted, no need for the page state
		 * change.
		 */
		memcpy(dst, src, sz);
	}
}

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
		if (cc_platform_has(CC_ATTR_GUEST_SEV_SNP)) {
			snp_memcpy(sme_early_buffer, src, len, paddr, enc);
			snp_memcpy(dst, sme_early_buffer, len, paddr, !enc);
		} else {
			memcpy(sme_early_buffer, src, len);
			memcpy(dst, sme_early_buffer, len);
		}

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

	flush_tlb_local();
}

void __init sme_unmap_bootdata(char *real_mode_data)
{
	struct boot_params *boot_data;
	unsigned long cmdline_paddr;

	if (!cc_platform_has(CC_ATTR_HOST_MEM_ENCRYPT))
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

	if (!cc_platform_has(CC_ATTR_HOST_MEM_ENCRYPT))
		return;

	__sme_early_map_unmap_mem(real_mode_data, sizeof(boot_params), true);

	/* Get the command line address after mapping the real_mode_data */
	boot_data = (struct boot_params *)real_mode_data;
	cmdline_paddr = boot_data->hdr.cmd_line_ptr | ((u64)boot_data->ext_cmd_line_ptr << 32);

	if (!cmdline_paddr)
		return;

	__sme_early_map_unmap_mem(__va(cmdline_paddr), COMMAND_LINE_SIZE, true);
}

static unsigned long pg_level_to_pfn(int level, pte_t *kpte, pgprot_t *ret_prot)
{
	unsigned long pfn = 0;
	pgprot_t prot;

	switch (level) {
	case PG_LEVEL_4K:
		pfn = pte_pfn(*kpte);
		prot = pte_pgprot(*kpte);
		break;
	case PG_LEVEL_2M:
		pfn = pmd_pfn(*(pmd_t *)kpte);
		prot = pmd_pgprot(*(pmd_t *)kpte);
		break;
	case PG_LEVEL_1G:
		pfn = pud_pfn(*(pud_t *)kpte);
		prot = pud_pgprot(*(pud_t *)kpte);
		break;
	default:
		WARN_ONCE(1, "Invalid level for kpte\n");
		return 0;
	}

	if (ret_prot)
		*ret_prot = prot;

	return pfn;
}

static bool amd_enc_tlb_flush_required(bool enc)
{
	return true;
}

static bool amd_enc_cache_flush_required(void)
{
	return !cpu_feature_enabled(X86_FEATURE_SME_COHERENT);
}

static void enc_dec_hypercall(unsigned long vaddr, unsigned long size, bool enc)
{
#ifdef CONFIG_PARAVIRT
	unsigned long vaddr_end = vaddr + size;

	while (vaddr < vaddr_end) {
		int psize, pmask, level;
		unsigned long pfn;
		pte_t *kpte;

		kpte = lookup_address(vaddr, &level);
		if (!kpte || pte_none(*kpte)) {
			WARN_ONCE(1, "kpte lookup for vaddr\n");
			return;
		}

		pfn = pg_level_to_pfn(level, kpte, NULL);
		if (!pfn)
			continue;

		psize = page_level_size(level);
		pmask = page_level_mask(level);

		notify_page_enc_status_changed(pfn, psize >> PAGE_SHIFT, enc);

		vaddr = (vaddr & pmask) + psize;
	}
#endif
}

static int amd_enc_status_change_prepare(unsigned long vaddr, int npages, bool enc)
{
	/*
	 * To maintain the security guarantees of SEV-SNP guests, make sure
	 * to invalidate the memory before encryption attribute is cleared.
	 */
	if (cc_platform_has(CC_ATTR_GUEST_SEV_SNP) && !enc)
		snp_set_memory_shared(vaddr, npages);

	return 0;
}

/* Return true unconditionally: return value doesn't matter for the SEV side */
static int amd_enc_status_change_finish(unsigned long vaddr, int npages, bool enc)
{
	/*
	 * After memory is mapped encrypted in the page table, validate it
	 * so that it is consistent with the page table updates.
	 */
	if (cc_platform_has(CC_ATTR_GUEST_SEV_SNP) && enc)
		snp_set_memory_private(vaddr, npages);

	if (!cc_platform_has(CC_ATTR_HOST_MEM_ENCRYPT))
		enc_dec_hypercall(vaddr, npages << PAGE_SHIFT, enc);

	return 0;
}

int prepare_pte_enc(struct pte_enc_desc *d)
{
	pgprot_t old_prot;

	d->pfn = pg_level_to_pfn(d->pte_level, d->kpte, &old_prot);
	if (!d->pfn)
		return 1;

	d->new_pgprot = old_prot;
	if (d->encrypt)
		pgprot_val(d->new_pgprot) |= _PAGE_ENC;
	else
		pgprot_val(d->new_pgprot) &= ~_PAGE_ENC;

	/* If prot is same then do nothing. */
	if (pgprot_val(old_prot) == pgprot_val(d->new_pgprot))
		return 1;

	d->pa = d->pfn << PAGE_SHIFT;
	d->size = page_level_size(d->pte_level);

	/*
	 * In-place en-/decryption and physical page attribute change
	 * from C=1 to C=0 or vice versa will be performed. Flush the
	 * caches to ensure that data gets accessed with the correct
	 * C-bit.
	 */
	if (d->va)
		clflush_cache_range(d->va, d->size);
	else
		clflush_cache_range(__va(d->pa), d->size);

	return 0;
}

void set_pte_enc_mask(pte_t *kpte, unsigned long pfn, pgprot_t new_prot)
{
	pte_t new_pte;

	/* Change the page encryption mask. */
	new_pte = pfn_pte(pfn, new_prot);
	set_pte_atomic(kpte, new_pte);
}

static void __init __set_clr_pte_enc(pte_t *kpte, int level, bool enc)
{
	struct pte_enc_desc d = {
		.kpte	     = kpte,
		.pte_level   = level,
		.encrypt     = enc
	};

	if (prepare_pte_enc(&d))
		return;

	/* Encrypt/decrypt the contents in-place */
	if (enc) {
		sme_early_encrypt(d.pa, d.size);
	} else {
		sme_early_decrypt(d.pa, d.size);

		/*
		 * ON SNP, the page state in the RMP table must happen
		 * before the page table updates.
		 */
		early_snp_set_memory_shared((unsigned long)__va(d.pa), d.pa, 1);
	}

	set_pte_enc_mask(kpte, d.pfn, d.new_pgprot);

	/*
	 * If page is set encrypted in the page table, then update the RMP table to
	 * add this page as private.
	 */
	if (enc)
		early_snp_set_memory_private((unsigned long)__va(d.pa), d.pa, 1);
}

static int __init early_set_memory_enc_dec(unsigned long vaddr,
					   unsigned long size, bool enc)
{
	unsigned long vaddr_end, vaddr_next, start;
	unsigned long psize, pmask;
	int split_page_size_mask;
	int level, ret;
	pte_t *kpte;

	start = vaddr;
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

		/*
		 * kernel_physical_mapping_change() does not flush the TLBs, so
		 * a TLB flush is required after we exit from the for loop.
		 */
		kernel_physical_mapping_change(__pa(vaddr & pmask),
					       __pa((vaddr_end & pmask) + psize),
					       split_page_size_mask);
	}

	ret = 0;

	early_set_mem_enc_dec_hypercall(start, size, enc);
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

void __init early_set_mem_enc_dec_hypercall(unsigned long vaddr, unsigned long size, bool enc)
{
	enc_dec_hypercall(vaddr, size, enc);
}

void __init sme_early_init(void)
{
	if (!sme_me_mask)
		return;

	early_pmd_flags = __sme_set(early_pmd_flags);

	__supported_pte_mask = __sme_set(__supported_pte_mask);

	/* Update the protection map with memory encryption mask */
	add_encrypt_protection_map();

	x86_platform.guest.enc_status_change_prepare = amd_enc_status_change_prepare;
	x86_platform.guest.enc_status_change_finish  = amd_enc_status_change_finish;
	x86_platform.guest.enc_tlb_flush_required    = amd_enc_tlb_flush_required;
	x86_platform.guest.enc_cache_flush_required  = amd_enc_cache_flush_required;
	x86_platform.guest.enc_kexec_begin	     = snp_kexec_begin;
	x86_platform.guest.enc_kexec_finish	     = snp_kexec_finish;

	/*
	 * AMD-SEV-ES intercepts the RDMSR to read the X2APIC ID in the
	 * parallel bringup low level code. That raises #VC which cannot be
	 * handled there.
	 * It does not provide a RDMSR GHCB protocol so the early startup
	 * code cannot directly communicate with the secure firmware. The
	 * alternative solution to retrieve the APIC ID via CPUID(0xb),
	 * which is covered by the GHCB protocol, is not viable either
	 * because there is no enforcement of the CPUID(0xb) provided
	 * "initial" APIC ID to be the same as the real APIC ID.
	 * Disable parallel bootup.
	 */
	if (sev_status & MSR_AMD64_SEV_ES_ENABLED)
		x86_cpuinit.parallel_bringup = false;

	/*
	 * The VMM is capable of injecting interrupt 0x80 and triggering the
	 * compatibility syscall path.
	 *
	 * By default, the 32-bit emulation is disabled in order to ensure
	 * the safety of the VM.
	 */
	if (sev_status & MSR_AMD64_SEV_ENABLED)
		ia32_disable();

	/*
	 * Override init functions that scan the ROM region in SEV-SNP guests,
	 * as this memory is not pre-validated and would thus cause a crash.
	 */
	if (sev_status & MSR_AMD64_SEV_SNP_ENABLED) {
		x86_init.mpparse.find_mptable = x86_init_noop;
		x86_init.pci.init_irq = x86_init_noop;
		x86_init.resources.probe_roms = x86_init_noop;

		/*
		 * DMI setup behavior for SEV-SNP guests depends on
		 * efi_enabled(EFI_CONFIG_TABLES), which hasn't been
		 * parsed yet. snp_dmi_setup() will run after that
		 * parsing has happened.
		 */
		x86_init.resources.dmi_setup = snp_dmi_setup;
	}

	if (sev_status & MSR_AMD64_SNP_SECURE_TSC)
		setup_force_cpu_cap(X86_FEATURE_TSC_RELIABLE);
}

void __init mem_encrypt_free_decrypted_mem(void)
{
	unsigned long vaddr, vaddr_end, npages;
	int r;

	vaddr = (unsigned long)__start_bss_decrypted_unused;
	vaddr_end = (unsigned long)__end_bss_decrypted;
	npages = (vaddr_end - vaddr) >> PAGE_SHIFT;

	/*
	 * If the unused memory range was mapped decrypted, change the encryption
	 * attribute from decrypted to encrypted before freeing it. Base the
	 * re-encryption on the same condition used for the decryption in
	 * sme_postprocess_startup(). Higher level abstractions, such as
	 * CC_ATTR_MEM_ENCRYPT, aren't necessarily equivalent in a Hyper-V VM
	 * using vTOM, where sme_me_mask is always zero.
	 */
	if (sme_me_mask) {
		r = set_memory_encrypted(vaddr, npages);
		if (r) {
			pr_warn("failed to free unused decrypted pages\n");
			return;
		}
	}

	free_init_pages("unused decrypted", vaddr, vaddr_end);
}

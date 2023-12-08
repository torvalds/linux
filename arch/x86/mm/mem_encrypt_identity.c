// SPDX-License-Identifier: GPL-2.0-only
/*
 * AMD Memory Encryption Support
 *
 * Copyright (C) 2016 Advanced Micro Devices, Inc.
 *
 * Author: Tom Lendacky <thomas.lendacky@amd.com>
 */

#define DISABLE_BRANCH_PROFILING

/*
 * Since we're dealing with identity mappings, physical and virtual
 * addresses are the same, so override these defines which are ultimately
 * used by the headers in misc.h.
 */
#define __pa(x)  ((unsigned long)(x))
#define __va(x)  ((void *)((unsigned long)(x)))

/*
 * Special hack: we have to be careful, because no indirections are
 * allowed here, and paravirt_ops is a kind of one. As it will only run in
 * baremetal anyway, we just keep it from happening. (This list needs to
 * be extended when new paravirt and debugging variants are added.)
 */
#undef CONFIG_PARAVIRT
#undef CONFIG_PARAVIRT_XXL
#undef CONFIG_PARAVIRT_SPINLOCKS

/*
 * This code runs before CPU feature bits are set. By default, the
 * pgtable_l5_enabled() function uses bit X86_FEATURE_LA57 to determine if
 * 5-level paging is active, so that won't work here. USE_EARLY_PGTABLE_L5
 * is provided to handle this situation and, instead, use a variable that
 * has been set by the early boot code.
 */
#define USE_EARLY_PGTABLE_L5

#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/mem_encrypt.h>
#include <linux/cc_platform.h>

#include <asm/setup.h>
#include <asm/sections.h>
#include <asm/cmdline.h>
#include <asm/coco.h>
#include <asm/sev.h>

#include "mm_internal.h"

#define PGD_FLAGS		_KERNPG_TABLE_NOENC
#define P4D_FLAGS		_KERNPG_TABLE_NOENC
#define PUD_FLAGS		_KERNPG_TABLE_NOENC
#define PMD_FLAGS		_KERNPG_TABLE_NOENC

#define PMD_FLAGS_LARGE		(__PAGE_KERNEL_LARGE_EXEC & ~_PAGE_GLOBAL)

#define PMD_FLAGS_DEC		PMD_FLAGS_LARGE
#define PMD_FLAGS_DEC_WP	((PMD_FLAGS_DEC & ~_PAGE_LARGE_CACHE_MASK) | \
				 (_PAGE_PAT_LARGE | _PAGE_PWT))

#define PMD_FLAGS_ENC		(PMD_FLAGS_LARGE | _PAGE_ENC)

#define PTE_FLAGS		(__PAGE_KERNEL_EXEC & ~_PAGE_GLOBAL)

#define PTE_FLAGS_DEC		PTE_FLAGS
#define PTE_FLAGS_DEC_WP	((PTE_FLAGS_DEC & ~_PAGE_CACHE_MASK) | \
				 (_PAGE_PAT | _PAGE_PWT))

#define PTE_FLAGS_ENC		(PTE_FLAGS | _PAGE_ENC)

struct sme_populate_pgd_data {
	void    *pgtable_area;
	pgd_t   *pgd;

	pmdval_t pmd_flags;
	pteval_t pte_flags;
	unsigned long paddr;

	unsigned long vaddr;
	unsigned long vaddr_end;
};

/*
 * This work area lives in the .init.scratch section, which lives outside of
 * the kernel proper. It is sized to hold the intermediate copy buffer and
 * more than enough pagetable pages.
 *
 * By using this section, the kernel can be encrypted in place and it
 * avoids any possibility of boot parameters or initramfs images being
 * placed such that the in-place encryption logic overwrites them.  This
 * section is 2MB aligned to allow for simple pagetable setup using only
 * PMD entries (see vmlinux.lds.S).
 */
static char sme_workarea[2 * PMD_PAGE_SIZE] __section(".init.scratch");

static char sme_cmdline_arg[] __initdata = "mem_encrypt";
static char sme_cmdline_on[]  __initdata = "on";
static char sme_cmdline_off[] __initdata = "off";

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

static pud_t __init *sme_prepare_pgd(struct sme_populate_pgd_data *ppd)
{
	pgd_t *pgd;
	p4d_t *p4d;
	pud_t *pud;
	pmd_t *pmd;

	pgd = ppd->pgd + pgd_index(ppd->vaddr);
	if (pgd_none(*pgd)) {
		p4d = ppd->pgtable_area;
		memset(p4d, 0, sizeof(*p4d) * PTRS_PER_P4D);
		ppd->pgtable_area += sizeof(*p4d) * PTRS_PER_P4D;
		set_pgd(pgd, __pgd(PGD_FLAGS | __pa(p4d)));
	}

	p4d = p4d_offset(pgd, ppd->vaddr);
	if (p4d_none(*p4d)) {
		pud = ppd->pgtable_area;
		memset(pud, 0, sizeof(*pud) * PTRS_PER_PUD);
		ppd->pgtable_area += sizeof(*pud) * PTRS_PER_PUD;
		set_p4d(p4d, __p4d(P4D_FLAGS | __pa(pud)));
	}

	pud = pud_offset(p4d, ppd->vaddr);
	if (pud_none(*pud)) {
		pmd = ppd->pgtable_area;
		memset(pmd, 0, sizeof(*pmd) * PTRS_PER_PMD);
		ppd->pgtable_area += sizeof(*pmd) * PTRS_PER_PMD;
		set_pud(pud, __pud(PUD_FLAGS | __pa(pmd)));
	}

	if (pud_large(*pud))
		return NULL;

	return pud;
}

static void __init sme_populate_pgd_large(struct sme_populate_pgd_data *ppd)
{
	pud_t *pud;
	pmd_t *pmd;

	pud = sme_prepare_pgd(ppd);
	if (!pud)
		return;

	pmd = pmd_offset(pud, ppd->vaddr);
	if (pmd_large(*pmd))
		return;

	set_pmd(pmd, __pmd(ppd->paddr | ppd->pmd_flags));
}

static void __init sme_populate_pgd(struct sme_populate_pgd_data *ppd)
{
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte;

	pud = sme_prepare_pgd(ppd);
	if (!pud)
		return;

	pmd = pmd_offset(pud, ppd->vaddr);
	if (pmd_none(*pmd)) {
		pte = ppd->pgtable_area;
		memset(pte, 0, sizeof(*pte) * PTRS_PER_PTE);
		ppd->pgtable_area += sizeof(*pte) * PTRS_PER_PTE;
		set_pmd(pmd, __pmd(PMD_FLAGS | __pa(pte)));
	}

	if (pmd_large(*pmd))
		return;

	pte = pte_offset_map(pmd, ppd->vaddr);
	if (pte_none(*pte))
		set_pte(pte, __pte(ppd->paddr | ppd->pte_flags));
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
	unsigned long entries = 0, tables = 0;

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

	/* PGDIR_SIZE is equal to P4D_SIZE on 4-level machine. */
	if (PTRS_PER_P4D > 1)
		entries += (DIV_ROUND_UP(len, PGDIR_SIZE) + 1) * sizeof(p4d_t) * PTRS_PER_P4D;
	entries += (DIV_ROUND_UP(len, P4D_SIZE) + 1) * sizeof(pud_t) * PTRS_PER_PUD;
	entries += (DIV_ROUND_UP(len, PUD_SIZE) + 1) * sizeof(pmd_t) * PTRS_PER_PMD;
	entries += 2 * sizeof(pte_t) * PTRS_PER_PTE;

	/*
	 * Now calculate the added pagetable structures needed to populate
	 * the new pagetables.
	 */

	if (PTRS_PER_P4D > 1)
		tables += DIV_ROUND_UP(entries, PGDIR_SIZE) * sizeof(p4d_t) * PTRS_PER_P4D;
	tables += DIV_ROUND_UP(entries, P4D_SIZE) * sizeof(pud_t) * PTRS_PER_PUD;
	tables += DIV_ROUND_UP(entries, PUD_SIZE) * sizeof(pmd_t) * PTRS_PER_PMD;

	return entries + tables;
}

void __init sme_encrypt_kernel(struct boot_params *bp)
{
	unsigned long workarea_start, workarea_end, workarea_len;
	unsigned long execute_start, execute_end, execute_len;
	unsigned long kernel_start, kernel_end, kernel_len;
	unsigned long initrd_start, initrd_end, initrd_len;
	struct sme_populate_pgd_data ppd;
	unsigned long pgtable_area_len;
	unsigned long decrypted_base;

	/*
	 * This is early code, use an open coded check for SME instead of
	 * using cc_platform_has(). This eliminates worries about removing
	 * instrumentation or checking boot_cpu_data in the cc_platform_has()
	 * function.
	 */
	if (!sme_get_me_mask() || sev_status & MSR_AMD64_SEV_ENABLED)
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

	/*
	 * We're running identity mapped, so we must obtain the address to the
	 * SME encryption workarea using rip-relative addressing.
	 */
	asm ("lea sme_workarea(%%rip), %0"
	     : "=r" (workarea_start)
	     : "p" (sme_workarea));

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

void __init sme_enable(struct boot_params *bp)
{
	const char *cmdline_ptr, *cmdline_arg, *cmdline_on, *cmdline_off;
	unsigned int eax, ebx, ecx, edx;
	unsigned long feature_mask;
	bool active_by_default;
	unsigned long me_mask;
	char buffer[16];
	bool snp;
	u64 msr;

	snp = snp_init(bp);

	/* Check for the SME/SEV support leaf */
	eax = 0x80000000;
	ecx = 0;
	native_cpuid(&eax, &ebx, &ecx, &edx);
	if (eax < 0x8000001f)
		return;

#define AMD_SME_BIT	BIT(0)
#define AMD_SEV_BIT	BIT(1)

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
	/* Check whether SEV or SME is supported */
	if (!(eax & (AMD_SEV_BIT | AMD_SME_BIT)))
		return;

	me_mask = 1UL << (ebx & 0x3f);

	/* Check the SEV MSR whether SEV or SME is enabled */
	sev_status   = __rdmsr(MSR_AMD64_SEV);
	feature_mask = (sev_status & MSR_AMD64_SEV_ENABLED) ? AMD_SEV_BIT : AMD_SME_BIT;

	/* The SEV-SNP CC blob should never be present unless SEV-SNP is enabled. */
	if (snp && !(sev_status & MSR_AMD64_SEV_SNP_ENABLED))
		snp_abort();

	/* Check if memory encryption is enabled */
	if (feature_mask == AMD_SME_BIT) {
		/*
		 * No SME if Hypervisor bit is set. This check is here to
		 * prevent a guest from trying to enable SME. For running as a
		 * KVM guest the MSR_AMD64_SYSCFG will be sufficient, but there
		 * might be other hypervisors which emulate that MSR as non-zero
		 * or even pass it through to the guest.
		 * A malicious hypervisor can still trick a guest into this
		 * path, but there is no way to protect against that.
		 */
		eax = 1;
		ecx = 0;
		native_cpuid(&eax, &ebx, &ecx, &edx);
		if (ecx & BIT(31))
			return;

		/* For SME, check the SYSCFG MSR */
		msr = __rdmsr(MSR_AMD64_SYSCFG);
		if (!(msr & MSR_AMD64_SYSCFG_MEM_ENCRYPT))
			return;
	} else {
		/* SEV state cannot be controlled by a command line option */
		sme_me_mask = me_mask;
		goto out;
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

	if (cmdline_find_option(cmdline_ptr, cmdline_arg, buffer, sizeof(buffer)) < 0)
		return;

	if (!strncmp(buffer, cmdline_on, sizeof(buffer)))
		sme_me_mask = me_mask;
	else if (!strncmp(buffer, cmdline_off, sizeof(buffer)))
		sme_me_mask = 0;
	else
		sme_me_mask = active_by_default ? me_mask : 0;
out:
	if (sme_me_mask) {
		physical_mask &= ~sme_me_mask;
		cc_set_vendor(CC_VENDOR_AMD);
		cc_set_mask(sme_me_mask);
	}
}

/*
 * IPMMU VMSA
 *
 * Copyright (C) 2014 Renesas Electronics Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 */

#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/export.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/iommu.h>
#include <linux/module.h>
#include <linux/platform_data/ipmmu-vmsa.h>
#include <linux/platform_device.h>
#include <linux/sizes.h>
#include <linux/slab.h>

#include <asm/dma-iommu.h>
#include <asm/pgalloc.h>

struct ipmmu_vmsa_device {
	struct device *dev;
	void __iomem *base;
	struct list_head list;

	const struct ipmmu_vmsa_platform_data *pdata;
	unsigned int num_utlbs;

	struct dma_iommu_mapping *mapping;
};

struct ipmmu_vmsa_domain {
	struct ipmmu_vmsa_device *mmu;
	struct iommu_domain *io_domain;

	unsigned int context_id;
	spinlock_t lock;			/* Protects mappings */
	pgd_t *pgd;
};

struct ipmmu_vmsa_archdata {
	struct ipmmu_vmsa_device *mmu;
	unsigned int utlb;
};

static DEFINE_SPINLOCK(ipmmu_devices_lock);
static LIST_HEAD(ipmmu_devices);

#define TLB_LOOP_TIMEOUT		100	/* 100us */

/* -----------------------------------------------------------------------------
 * Registers Definition
 */

#define IM_CTX_SIZE			0x40

#define IMCTR				0x0000
#define IMCTR_TRE			(1 << 17)
#define IMCTR_AFE			(1 << 16)
#define IMCTR_RTSEL_MASK		(3 << 4)
#define IMCTR_RTSEL_SHIFT		4
#define IMCTR_TREN			(1 << 3)
#define IMCTR_INTEN			(1 << 2)
#define IMCTR_FLUSH			(1 << 1)
#define IMCTR_MMUEN			(1 << 0)

#define IMCAAR				0x0004

#define IMTTBCR				0x0008
#define IMTTBCR_EAE			(1 << 31)
#define IMTTBCR_PMB			(1 << 30)
#define IMTTBCR_SH1_NON_SHAREABLE	(0 << 28)
#define IMTTBCR_SH1_OUTER_SHAREABLE	(2 << 28)
#define IMTTBCR_SH1_INNER_SHAREABLE	(3 << 28)
#define IMTTBCR_SH1_MASK		(3 << 28)
#define IMTTBCR_ORGN1_NC		(0 << 26)
#define IMTTBCR_ORGN1_WB_WA		(1 << 26)
#define IMTTBCR_ORGN1_WT		(2 << 26)
#define IMTTBCR_ORGN1_WB		(3 << 26)
#define IMTTBCR_ORGN1_MASK		(3 << 26)
#define IMTTBCR_IRGN1_NC		(0 << 24)
#define IMTTBCR_IRGN1_WB_WA		(1 << 24)
#define IMTTBCR_IRGN1_WT		(2 << 24)
#define IMTTBCR_IRGN1_WB		(3 << 24)
#define IMTTBCR_IRGN1_MASK		(3 << 24)
#define IMTTBCR_TSZ1_MASK		(7 << 16)
#define IMTTBCR_TSZ1_SHIFT		16
#define IMTTBCR_SH0_NON_SHAREABLE	(0 << 12)
#define IMTTBCR_SH0_OUTER_SHAREABLE	(2 << 12)
#define IMTTBCR_SH0_INNER_SHAREABLE	(3 << 12)
#define IMTTBCR_SH0_MASK		(3 << 12)
#define IMTTBCR_ORGN0_NC		(0 << 10)
#define IMTTBCR_ORGN0_WB_WA		(1 << 10)
#define IMTTBCR_ORGN0_WT		(2 << 10)
#define IMTTBCR_ORGN0_WB		(3 << 10)
#define IMTTBCR_ORGN0_MASK		(3 << 10)
#define IMTTBCR_IRGN0_NC		(0 << 8)
#define IMTTBCR_IRGN0_WB_WA		(1 << 8)
#define IMTTBCR_IRGN0_WT		(2 << 8)
#define IMTTBCR_IRGN0_WB		(3 << 8)
#define IMTTBCR_IRGN0_MASK		(3 << 8)
#define IMTTBCR_SL0_LVL_2		(0 << 4)
#define IMTTBCR_SL0_LVL_1		(1 << 4)
#define IMTTBCR_TSZ0_MASK		(7 << 0)
#define IMTTBCR_TSZ0_SHIFT		O

#define IMBUSCR				0x000c
#define IMBUSCR_DVM			(1 << 2)
#define IMBUSCR_BUSSEL_SYS		(0 << 0)
#define IMBUSCR_BUSSEL_CCI		(1 << 0)
#define IMBUSCR_BUSSEL_IMCAAR		(2 << 0)
#define IMBUSCR_BUSSEL_CCI_IMCAAR	(3 << 0)
#define IMBUSCR_BUSSEL_MASK		(3 << 0)

#define IMTTLBR0			0x0010
#define IMTTUBR0			0x0014
#define IMTTLBR1			0x0018
#define IMTTUBR1			0x001c

#define IMSTR				0x0020
#define IMSTR_ERRLVL_MASK		(3 << 12)
#define IMSTR_ERRLVL_SHIFT		12
#define IMSTR_ERRCODE_TLB_FORMAT	(1 << 8)
#define IMSTR_ERRCODE_ACCESS_PERM	(4 << 8)
#define IMSTR_ERRCODE_SECURE_ACCESS	(5 << 8)
#define IMSTR_ERRCODE_MASK		(7 << 8)
#define IMSTR_MHIT			(1 << 4)
#define IMSTR_ABORT			(1 << 2)
#define IMSTR_PF			(1 << 1)
#define IMSTR_TF			(1 << 0)

#define IMMAIR0				0x0028
#define IMMAIR1				0x002c
#define IMMAIR_ATTR_MASK		0xff
#define IMMAIR_ATTR_DEVICE		0x04
#define IMMAIR_ATTR_NC			0x44
#define IMMAIR_ATTR_WBRWA		0xff
#define IMMAIR_ATTR_SHIFT(n)		((n) << 3)
#define IMMAIR_ATTR_IDX_NC		0
#define IMMAIR_ATTR_IDX_WBRWA		1
#define IMMAIR_ATTR_IDX_DEV		2

#define IMEAR				0x0030

#define IMPCTR				0x0200
#define IMPSTR				0x0208
#define IMPEAR				0x020c
#define IMPMBA(n)			(0x0280 + ((n) * 4))
#define IMPMBD(n)			(0x02c0 + ((n) * 4))

#define IMUCTR(n)			(0x0300 + ((n) * 16))
#define IMUCTR_FIXADDEN			(1 << 31)
#define IMUCTR_FIXADD_MASK		(0xff << 16)
#define IMUCTR_FIXADD_SHIFT		16
#define IMUCTR_TTSEL_MMU(n)		((n) << 4)
#define IMUCTR_TTSEL_PMB		(8 << 4)
#define IMUCTR_TTSEL_MASK		(15 << 4)
#define IMUCTR_FLUSH			(1 << 1)
#define IMUCTR_MMUEN			(1 << 0)

#define IMUASID(n)			(0x0308 + ((n) * 16))
#define IMUASID_ASID8_MASK		(0xff << 8)
#define IMUASID_ASID8_SHIFT		8
#define IMUASID_ASID0_MASK		(0xff << 0)
#define IMUASID_ASID0_SHIFT		0

/* -----------------------------------------------------------------------------
 * Page Table Bits
 */

/*
 * VMSA states in section B3.6.3 "Control of Secure or Non-secure memory access,
 * Long-descriptor format" that the NStable bit being set in a table descriptor
 * will result in the NStable and NS bits of all child entries being ignored and
 * considered as being set. The IPMMU seems not to comply with this, as it
 * generates a secure access page fault if any of the NStable and NS bits isn't
 * set when running in non-secure mode.
 */
#ifndef PMD_NSTABLE
#define PMD_NSTABLE			(_AT(pmdval_t, 1) << 63)
#endif

#define ARM_VMSA_PTE_XN			(((pteval_t)3) << 53)
#define ARM_VMSA_PTE_CONT		(((pteval_t)1) << 52)
#define ARM_VMSA_PTE_AF			(((pteval_t)1) << 10)
#define ARM_VMSA_PTE_SH_NS		(((pteval_t)0) << 8)
#define ARM_VMSA_PTE_SH_OS		(((pteval_t)2) << 8)
#define ARM_VMSA_PTE_SH_IS		(((pteval_t)3) << 8)
#define ARM_VMSA_PTE_SH_MASK		(((pteval_t)3) << 8)
#define ARM_VMSA_PTE_NS			(((pteval_t)1) << 5)
#define ARM_VMSA_PTE_PAGE		(((pteval_t)3) << 0)

/* Stage-1 PTE */
#define ARM_VMSA_PTE_nG			(((pteval_t)1) << 11)
#define ARM_VMSA_PTE_AP_UNPRIV		(((pteval_t)1) << 6)
#define ARM_VMSA_PTE_AP_RDONLY		(((pteval_t)2) << 6)
#define ARM_VMSA_PTE_AP_MASK		(((pteval_t)3) << 6)
#define ARM_VMSA_PTE_ATTRINDX_MASK	(((pteval_t)3) << 2)
#define ARM_VMSA_PTE_ATTRINDX_SHIFT	2

#define ARM_VMSA_PTE_ATTRS_MASK \
	(ARM_VMSA_PTE_XN | ARM_VMSA_PTE_CONT | ARM_VMSA_PTE_nG | \
	 ARM_VMSA_PTE_AF | ARM_VMSA_PTE_SH_MASK | ARM_VMSA_PTE_AP_MASK | \
	 ARM_VMSA_PTE_NS | ARM_VMSA_PTE_ATTRINDX_MASK)

#define ARM_VMSA_PTE_CONT_ENTRIES	16
#define ARM_VMSA_PTE_CONT_SIZE		(PAGE_SIZE * ARM_VMSA_PTE_CONT_ENTRIES)

#define IPMMU_PTRS_PER_PTE		512
#define IPMMU_PTRS_PER_PMD		512
#define IPMMU_PTRS_PER_PGD		4

/* -----------------------------------------------------------------------------
 * Read/Write Access
 */

static u32 ipmmu_read(struct ipmmu_vmsa_device *mmu, unsigned int offset)
{
	return ioread32(mmu->base + offset);
}

static void ipmmu_write(struct ipmmu_vmsa_device *mmu, unsigned int offset,
			u32 data)
{
	iowrite32(data, mmu->base + offset);
}

static u32 ipmmu_ctx_read(struct ipmmu_vmsa_domain *domain, unsigned int reg)
{
	return ipmmu_read(domain->mmu, domain->context_id * IM_CTX_SIZE + reg);
}

static void ipmmu_ctx_write(struct ipmmu_vmsa_domain *domain, unsigned int reg,
			    u32 data)
{
	ipmmu_write(domain->mmu, domain->context_id * IM_CTX_SIZE + reg, data);
}

/* -----------------------------------------------------------------------------
 * TLB and microTLB Management
 */

/* Wait for any pending TLB invalidations to complete */
static void ipmmu_tlb_sync(struct ipmmu_vmsa_domain *domain)
{
	unsigned int count = 0;

	while (ipmmu_ctx_read(domain, IMCTR) & IMCTR_FLUSH) {
		cpu_relax();
		if (++count == TLB_LOOP_TIMEOUT) {
			dev_err_ratelimited(domain->mmu->dev,
			"TLB sync timed out -- MMU may be deadlocked\n");
			return;
		}
		udelay(1);
	}
}

static void ipmmu_tlb_invalidate(struct ipmmu_vmsa_domain *domain)
{
	u32 reg;

	reg = ipmmu_ctx_read(domain, IMCTR);
	reg |= IMCTR_FLUSH;
	ipmmu_ctx_write(domain, IMCTR, reg);

	ipmmu_tlb_sync(domain);
}

/*
 * Enable MMU translation for the microTLB.
 */
static void ipmmu_utlb_enable(struct ipmmu_vmsa_domain *domain,
			      unsigned int utlb)
{
	struct ipmmu_vmsa_device *mmu = domain->mmu;

	/*
	 * TODO: Reference-count the microTLB as several bus masters can be
	 * connected to the same microTLB.
	 */

	/* TODO: What should we set the ASID to ? */
	ipmmu_write(mmu, IMUASID(utlb), 0);
	/* TODO: Do we need to flush the microTLB ? */
	ipmmu_write(mmu, IMUCTR(utlb),
		    IMUCTR_TTSEL_MMU(domain->context_id) | IMUCTR_FLUSH |
		    IMUCTR_MMUEN);
}

/*
 * Disable MMU translation for the microTLB.
 */
static void ipmmu_utlb_disable(struct ipmmu_vmsa_domain *domain,
			       unsigned int utlb)
{
	struct ipmmu_vmsa_device *mmu = domain->mmu;

	ipmmu_write(mmu, IMUCTR(utlb), 0);
}

static void ipmmu_flush_pgtable(struct ipmmu_vmsa_device *mmu, void *addr,
				size_t size)
{
	unsigned long offset = (unsigned long)addr & ~PAGE_MASK;

	/*
	 * TODO: Add support for coherent walk through CCI with DVM and remove
	 * cache handling.
	 */
	dma_map_page(mmu->dev, virt_to_page(addr), offset, size, DMA_TO_DEVICE);
}

/* -----------------------------------------------------------------------------
 * Domain/Context Management
 */

static int ipmmu_domain_init_context(struct ipmmu_vmsa_domain *domain)
{
	phys_addr_t ttbr;
	u32 reg;

	/*
	 * TODO: When adding support for multiple contexts, find an unused
	 * context.
	 */
	domain->context_id = 0;

	/* TTBR0 */
	ipmmu_flush_pgtable(domain->mmu, domain->pgd,
			    IPMMU_PTRS_PER_PGD * sizeof(*domain->pgd));
	ttbr = __pa(domain->pgd);
	ipmmu_ctx_write(domain, IMTTLBR0, ttbr);
	ipmmu_ctx_write(domain, IMTTUBR0, ttbr >> 32);

	/*
	 * TTBCR
	 * We use long descriptors with inner-shareable WBWA tables and allocate
	 * the whole 32-bit VA space to TTBR0.
	 */
	ipmmu_ctx_write(domain, IMTTBCR, IMTTBCR_EAE |
			IMTTBCR_SH0_INNER_SHAREABLE | IMTTBCR_ORGN0_WB_WA |
			IMTTBCR_IRGN0_WB_WA | IMTTBCR_SL0_LVL_1);

	/*
	 * MAIR0
	 * We need three attributes only, non-cacheable, write-back read/write
	 * allocate and device memory.
	 */
	reg = (IMMAIR_ATTR_NC << IMMAIR_ATTR_SHIFT(IMMAIR_ATTR_IDX_NC))
	    | (IMMAIR_ATTR_WBRWA << IMMAIR_ATTR_SHIFT(IMMAIR_ATTR_IDX_WBRWA))
	    | (IMMAIR_ATTR_DEVICE << IMMAIR_ATTR_SHIFT(IMMAIR_ATTR_IDX_DEV));
	ipmmu_ctx_write(domain, IMMAIR0, reg);

	/* IMBUSCR */
	ipmmu_ctx_write(domain, IMBUSCR,
			ipmmu_ctx_read(domain, IMBUSCR) &
			~(IMBUSCR_DVM | IMBUSCR_BUSSEL_MASK));

	/*
	 * IMSTR
	 * Clear all interrupt flags.
	 */
	ipmmu_ctx_write(domain, IMSTR, ipmmu_ctx_read(domain, IMSTR));

	/*
	 * IMCTR
	 * Enable the MMU and interrupt generation. The long-descriptor
	 * translation table format doesn't use TEX remapping. Don't enable AF
	 * software management as we have no use for it. Flush the TLB as
	 * required when modifying the context registers.
	 */
	ipmmu_ctx_write(domain, IMCTR, IMCTR_INTEN | IMCTR_FLUSH | IMCTR_MMUEN);

	return 0;
}

static void ipmmu_domain_destroy_context(struct ipmmu_vmsa_domain *domain)
{
	/*
	 * Disable the context. Flush the TLB as required when modifying the
	 * context registers.
	 *
	 * TODO: Is TLB flush really needed ?
	 */
	ipmmu_ctx_write(domain, IMCTR, IMCTR_FLUSH);
	ipmmu_tlb_sync(domain);
}

/* -----------------------------------------------------------------------------
 * Fault Handling
 */

static irqreturn_t ipmmu_domain_irq(struct ipmmu_vmsa_domain *domain)
{
	const u32 err_mask = IMSTR_MHIT | IMSTR_ABORT | IMSTR_PF | IMSTR_TF;
	struct ipmmu_vmsa_device *mmu = domain->mmu;
	u32 status;
	u32 iova;

	status = ipmmu_ctx_read(domain, IMSTR);
	if (!(status & err_mask))
		return IRQ_NONE;

	iova = ipmmu_ctx_read(domain, IMEAR);

	/*
	 * Clear the error status flags. Unlike traditional interrupt flag
	 * registers that must be cleared by writing 1, this status register
	 * seems to require 0. The error address register must be read before,
	 * otherwise its value will be 0.
	 */
	ipmmu_ctx_write(domain, IMSTR, 0);

	/* Log fatal errors. */
	if (status & IMSTR_MHIT)
		dev_err_ratelimited(mmu->dev, "Multiple TLB hits @0x%08x\n",
				    iova);
	if (status & IMSTR_ABORT)
		dev_err_ratelimited(mmu->dev, "Page Table Walk Abort @0x%08x\n",
				    iova);

	if (!(status & (IMSTR_PF | IMSTR_TF)))
		return IRQ_NONE;

	/*
	 * Try to handle page faults and translation faults.
	 *
	 * TODO: We need to look up the faulty device based on the I/O VA. Use
	 * the IOMMU device for now.
	 */
	if (!report_iommu_fault(domain->io_domain, mmu->dev, iova, 0))
		return IRQ_HANDLED;

	dev_err_ratelimited(mmu->dev,
			    "Unhandled fault: status 0x%08x iova 0x%08x\n",
			    status, iova);

	return IRQ_HANDLED;
}

static irqreturn_t ipmmu_irq(int irq, void *dev)
{
	struct ipmmu_vmsa_device *mmu = dev;
	struct iommu_domain *io_domain;
	struct ipmmu_vmsa_domain *domain;

	if (!mmu->mapping)
		return IRQ_NONE;

	io_domain = mmu->mapping->domain;
	domain = io_domain->priv;

	return ipmmu_domain_irq(domain);
}

/* -----------------------------------------------------------------------------
 * Page Table Management
 */

#define pud_pgtable(pud) pfn_to_page(__phys_to_pfn(pud_val(pud) & PHYS_MASK))

static void ipmmu_free_ptes(pmd_t *pmd)
{
	pgtable_t table = pmd_pgtable(*pmd);
	__free_page(table);
}

static void ipmmu_free_pmds(pud_t *pud)
{
	pmd_t *pmd = pmd_offset(pud, 0);
	pgtable_t table;
	unsigned int i;

	for (i = 0; i < IPMMU_PTRS_PER_PMD; ++i) {
		if (!pmd_table(*pmd))
			continue;

		ipmmu_free_ptes(pmd);
		pmd++;
	}

	table = pud_pgtable(*pud);
	__free_page(table);
}

static void ipmmu_free_pgtables(struct ipmmu_vmsa_domain *domain)
{
	pgd_t *pgd, *pgd_base = domain->pgd;
	unsigned int i;

	/*
	 * Recursively free the page tables for this domain. We don't care about
	 * speculative TLB filling, because the TLB will be nuked next time this
	 * context bank is re-allocated and no devices currently map to these
	 * tables.
	 */
	pgd = pgd_base;
	for (i = 0; i < IPMMU_PTRS_PER_PGD; ++i) {
		if (pgd_none(*pgd))
			continue;
		ipmmu_free_pmds((pud_t *)pgd);
		pgd++;
	}

	kfree(pgd_base);
}

/*
 * We can't use the (pgd|pud|pmd|pte)_populate or the set_(pgd|pud|pmd|pte)
 * functions as they would flush the CPU TLB.
 */

static pte_t *ipmmu_alloc_pte(struct ipmmu_vmsa_device *mmu, pmd_t *pmd,
			      unsigned long iova)
{
	pte_t *pte;

	if (!pmd_none(*pmd))
		return pte_offset_kernel(pmd, iova);

	pte = (pte_t *)get_zeroed_page(GFP_ATOMIC);
	if (!pte)
		return NULL;

	ipmmu_flush_pgtable(mmu, pte, PAGE_SIZE);
	*pmd = __pmd(__pa(pte) | PMD_NSTABLE | PMD_TYPE_TABLE);
	ipmmu_flush_pgtable(mmu, pmd, sizeof(*pmd));

	return pte + pte_index(iova);
}

static pmd_t *ipmmu_alloc_pmd(struct ipmmu_vmsa_device *mmu, pgd_t *pgd,
			      unsigned long iova)
{
	pud_t *pud = (pud_t *)pgd;
	pmd_t *pmd;

	if (!pud_none(*pud))
		return pmd_offset(pud, iova);

	pmd = (pmd_t *)get_zeroed_page(GFP_ATOMIC);
	if (!pmd)
		return NULL;

	ipmmu_flush_pgtable(mmu, pmd, PAGE_SIZE);
	*pud = __pud(__pa(pmd) | PMD_NSTABLE | PMD_TYPE_TABLE);
	ipmmu_flush_pgtable(mmu, pud, sizeof(*pud));

	return pmd + pmd_index(iova);
}

static u64 ipmmu_page_prot(unsigned int prot, u64 type)
{
	u64 pgprot = ARM_VMSA_PTE_XN | ARM_VMSA_PTE_nG | ARM_VMSA_PTE_AF
		   | ARM_VMSA_PTE_SH_IS | ARM_VMSA_PTE_AP_UNPRIV
		   | ARM_VMSA_PTE_NS | type;

	if (!(prot & IOMMU_WRITE) && (prot & IOMMU_READ))
		pgprot |= ARM_VMSA_PTE_AP_RDONLY;

	if (prot & IOMMU_CACHE)
		pgprot |= IMMAIR_ATTR_IDX_WBRWA << ARM_VMSA_PTE_ATTRINDX_SHIFT;

	if (prot & IOMMU_EXEC)
		pgprot &= ~ARM_VMSA_PTE_XN;
	else if (!(prot & (IOMMU_READ | IOMMU_WRITE)))
		/* If no access create a faulting entry to avoid TLB fills. */
		pgprot &= ~ARM_VMSA_PTE_PAGE;

	return pgprot;
}

static int ipmmu_alloc_init_pte(struct ipmmu_vmsa_device *mmu, pmd_t *pmd,
				unsigned long iova, unsigned long pfn,
				size_t size, int prot)
{
	pteval_t pteval = ipmmu_page_prot(prot, ARM_VMSA_PTE_PAGE);
	unsigned int num_ptes = 1;
	pte_t *pte, *start;
	unsigned int i;

	pte = ipmmu_alloc_pte(mmu, pmd, iova);
	if (!pte)
		return -ENOMEM;

	start = pte;

	/*
	 * Install the page table entries. We can be called both for a single
	 * page or for a block of 16 physically contiguous pages. In the latter
	 * case set the PTE contiguous hint.
	 */
	if (size == SZ_64K) {
		pteval |= ARM_VMSA_PTE_CONT;
		num_ptes = ARM_VMSA_PTE_CONT_ENTRIES;
	}

	for (i = num_ptes; i; --i)
		*pte++ = pfn_pte(pfn++, __pgprot(pteval));

	ipmmu_flush_pgtable(mmu, start, sizeof(*pte) * num_ptes);

	return 0;
}

static int ipmmu_alloc_init_pmd(struct ipmmu_vmsa_device *mmu, pmd_t *pmd,
				unsigned long iova, unsigned long pfn,
				int prot)
{
	pmdval_t pmdval = ipmmu_page_prot(prot, PMD_TYPE_SECT);

	*pmd = pfn_pmd(pfn, __pgprot(pmdval));
	ipmmu_flush_pgtable(mmu, pmd, sizeof(*pmd));

	return 0;
}

static int ipmmu_create_mapping(struct ipmmu_vmsa_domain *domain,
				unsigned long iova, phys_addr_t paddr,
				size_t size, int prot)
{
	struct ipmmu_vmsa_device *mmu = domain->mmu;
	pgd_t *pgd = domain->pgd;
	unsigned long flags;
	unsigned long pfn;
	pmd_t *pmd;
	int ret;

	if (!pgd)
		return -EINVAL;

	if (size & ~PAGE_MASK)
		return -EINVAL;

	if (paddr & ~((1ULL << 40) - 1))
		return -ERANGE;

	pfn = __phys_to_pfn(paddr);
	pgd += pgd_index(iova);

	/* Update the page tables. */
	spin_lock_irqsave(&domain->lock, flags);

	pmd = ipmmu_alloc_pmd(mmu, pgd, iova);
	if (!pmd) {
		ret = -ENOMEM;
		goto done;
	}

	switch (size) {
	case SZ_2M:
		ret = ipmmu_alloc_init_pmd(mmu, pmd, iova, pfn, prot);
		break;
	case SZ_64K:
	case SZ_4K:
		ret = ipmmu_alloc_init_pte(mmu, pmd, iova, pfn, size, prot);
		break;
	default:
		ret = -EINVAL;
		break;
	}

done:
	spin_unlock_irqrestore(&domain->lock, flags);

	if (!ret)
		ipmmu_tlb_invalidate(domain);

	return ret;
}

static void ipmmu_clear_pud(struct ipmmu_vmsa_device *mmu, pud_t *pud)
{
	/* Free the page table. */
	pgtable_t table = pud_pgtable(*pud);
	__free_page(table);

	/* Clear the PUD. */
	*pud = __pud(0);
	ipmmu_flush_pgtable(mmu, pud, sizeof(*pud));
}

static void ipmmu_clear_pmd(struct ipmmu_vmsa_device *mmu, pud_t *pud,
			    pmd_t *pmd)
{
	unsigned int i;

	/* Free the page table. */
	if (pmd_table(*pmd)) {
		pgtable_t table = pmd_pgtable(*pmd);
		__free_page(table);
	}

	/* Clear the PMD. */
	*pmd = __pmd(0);
	ipmmu_flush_pgtable(mmu, pmd, sizeof(*pmd));

	/* Check whether the PUD is still needed. */
	pmd = pmd_offset(pud, 0);
	for (i = 0; i < IPMMU_PTRS_PER_PMD; ++i) {
		if (!pmd_none(pmd[i]))
			return;
	}

	/* Clear the parent PUD. */
	ipmmu_clear_pud(mmu, pud);
}

static void ipmmu_clear_pte(struct ipmmu_vmsa_device *mmu, pud_t *pud,
			    pmd_t *pmd, pte_t *pte, unsigned int num_ptes)
{
	unsigned int i;

	/* Clear the PTE. */
	for (i = num_ptes; i; --i)
		pte[i-1] = __pte(0);

	ipmmu_flush_pgtable(mmu, pte, sizeof(*pte) * num_ptes);

	/* Check whether the PMD is still needed. */
	pte = pte_offset_kernel(pmd, 0);
	for (i = 0; i < IPMMU_PTRS_PER_PTE; ++i) {
		if (!pte_none(pte[i]))
			return;
	}

	/* Clear the parent PMD. */
	ipmmu_clear_pmd(mmu, pud, pmd);
}

static int ipmmu_split_pmd(struct ipmmu_vmsa_device *mmu, pmd_t *pmd)
{
	pte_t *pte, *start;
	pteval_t pteval;
	unsigned long pfn;
	unsigned int i;

	pte = (pte_t *)get_zeroed_page(GFP_ATOMIC);
	if (!pte)
		return -ENOMEM;

	/* Copy the PMD attributes. */
	pteval = (pmd_val(*pmd) & ARM_VMSA_PTE_ATTRS_MASK)
	       | ARM_VMSA_PTE_CONT | ARM_VMSA_PTE_PAGE;

	pfn = pmd_pfn(*pmd);
	start = pte;

	for (i = IPMMU_PTRS_PER_PTE; i; --i)
		*pte++ = pfn_pte(pfn++, __pgprot(pteval));

	ipmmu_flush_pgtable(mmu, start, PAGE_SIZE);
	*pmd = __pmd(__pa(start) | PMD_NSTABLE | PMD_TYPE_TABLE);
	ipmmu_flush_pgtable(mmu, pmd, sizeof(*pmd));

	return 0;
}

static void ipmmu_split_pte(struct ipmmu_vmsa_device *mmu, pte_t *pte)
{
	unsigned int i;

	for (i = ARM_VMSA_PTE_CONT_ENTRIES; i; --i)
		pte[i-1] = __pte(pte_val(*pte) & ~ARM_VMSA_PTE_CONT);

	ipmmu_flush_pgtable(mmu, pte, sizeof(*pte) * ARM_VMSA_PTE_CONT_ENTRIES);
}

static int ipmmu_clear_mapping(struct ipmmu_vmsa_domain *domain,
			       unsigned long iova, size_t size)
{
	struct ipmmu_vmsa_device *mmu = domain->mmu;
	unsigned long flags;
	pgd_t *pgd = domain->pgd;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte;
	int ret = 0;

	if (!pgd)
		return -EINVAL;

	if (size & ~PAGE_MASK)
		return -EINVAL;

	pgd += pgd_index(iova);
	pud = (pud_t *)pgd;

	spin_lock_irqsave(&domain->lock, flags);

	/* If there's no PUD or PMD we're done. */
	if (pud_none(*pud))
		goto done;

	pmd = pmd_offset(pud, iova);
	if (pmd_none(*pmd))
		goto done;

	/*
	 * When freeing a 2MB block just clear the PMD. In the unlikely case the
	 * block is mapped as individual pages this will free the corresponding
	 * PTE page table.
	 */
	if (size == SZ_2M) {
		ipmmu_clear_pmd(mmu, pud, pmd);
		goto done;
	}

	/*
	 * If the PMD has been mapped as a section remap it as pages to allow
	 * freeing individual pages.
	 */
	if (pmd_sect(*pmd))
		ipmmu_split_pmd(mmu, pmd);

	pte = pte_offset_kernel(pmd, iova);

	/*
	 * When freeing a 64kB block just clear the PTE entries. We don't have
	 * to care about the contiguous hint of the surrounding entries.
	 */
	if (size == SZ_64K) {
		ipmmu_clear_pte(mmu, pud, pmd, pte, ARM_VMSA_PTE_CONT_ENTRIES);
		goto done;
	}

	/*
	 * If the PTE has been mapped with the contiguous hint set remap it and
	 * its surrounding PTEs to allow unmapping a single page.
	 */
	if (pte_val(*pte) & ARM_VMSA_PTE_CONT)
		ipmmu_split_pte(mmu, pte);

	/* Clear the PTE. */
	ipmmu_clear_pte(mmu, pud, pmd, pte, 1);

done:
	spin_unlock_irqrestore(&domain->lock, flags);

	if (ret)
		ipmmu_tlb_invalidate(domain);

	return 0;
}

/* -----------------------------------------------------------------------------
 * IOMMU Operations
 */

static int ipmmu_domain_init(struct iommu_domain *io_domain)
{
	struct ipmmu_vmsa_domain *domain;

	domain = kzalloc(sizeof(*domain), GFP_KERNEL);
	if (!domain)
		return -ENOMEM;

	spin_lock_init(&domain->lock);

	domain->pgd = kzalloc(IPMMU_PTRS_PER_PGD * sizeof(pgd_t), GFP_KERNEL);
	if (!domain->pgd) {
		kfree(domain);
		return -ENOMEM;
	}

	io_domain->priv = domain;
	domain->io_domain = io_domain;

	return 0;
}

static void ipmmu_domain_destroy(struct iommu_domain *io_domain)
{
	struct ipmmu_vmsa_domain *domain = io_domain->priv;

	/*
	 * Free the domain resources. We assume that all devices have already
	 * been detached.
	 */
	ipmmu_domain_destroy_context(domain);
	ipmmu_free_pgtables(domain);
	kfree(domain);
}

static int ipmmu_attach_device(struct iommu_domain *io_domain,
			       struct device *dev)
{
	struct ipmmu_vmsa_archdata *archdata = dev->archdata.iommu;
	struct ipmmu_vmsa_device *mmu = archdata->mmu;
	struct ipmmu_vmsa_domain *domain = io_domain->priv;
	unsigned long flags;
	int ret = 0;

	if (!mmu) {
		dev_err(dev, "Cannot attach to IPMMU\n");
		return -ENXIO;
	}

	spin_lock_irqsave(&domain->lock, flags);

	if (!domain->mmu) {
		/* The domain hasn't been used yet, initialize it. */
		domain->mmu = mmu;
		ret = ipmmu_domain_init_context(domain);
	} else if (domain->mmu != mmu) {
		/*
		 * Something is wrong, we can't attach two devices using
		 * different IOMMUs to the same domain.
		 */
		dev_err(dev, "Can't attach IPMMU %s to domain on IPMMU %s\n",
			dev_name(mmu->dev), dev_name(domain->mmu->dev));
		ret = -EINVAL;
	}

	spin_unlock_irqrestore(&domain->lock, flags);

	if (ret < 0)
		return ret;

	ipmmu_utlb_enable(domain, archdata->utlb);

	return 0;
}

static void ipmmu_detach_device(struct iommu_domain *io_domain,
				struct device *dev)
{
	struct ipmmu_vmsa_archdata *archdata = dev->archdata.iommu;
	struct ipmmu_vmsa_domain *domain = io_domain->priv;

	ipmmu_utlb_disable(domain, archdata->utlb);

	/*
	 * TODO: Optimize by disabling the context when no device is attached.
	 */
}

static int ipmmu_map(struct iommu_domain *io_domain, unsigned long iova,
		     phys_addr_t paddr, size_t size, int prot)
{
	struct ipmmu_vmsa_domain *domain = io_domain->priv;

	if (!domain)
		return -ENODEV;

	return ipmmu_create_mapping(domain, iova, paddr, size, prot);
}

static size_t ipmmu_unmap(struct iommu_domain *io_domain, unsigned long iova,
			  size_t size)
{
	struct ipmmu_vmsa_domain *domain = io_domain->priv;
	int ret;

	ret = ipmmu_clear_mapping(domain, iova, size);
	return ret ? 0 : size;
}

static phys_addr_t ipmmu_iova_to_phys(struct iommu_domain *io_domain,
				      dma_addr_t iova)
{
	struct ipmmu_vmsa_domain *domain = io_domain->priv;
	pgd_t pgd;
	pud_t pud;
	pmd_t pmd;
	pte_t pte;

	/* TODO: Is locking needed ? */

	if (!domain->pgd)
		return 0;

	pgd = *(domain->pgd + pgd_index(iova));
	if (pgd_none(pgd))
		return 0;

	pud = *pud_offset(&pgd, iova);
	if (pud_none(pud))
		return 0;

	pmd = *pmd_offset(&pud, iova);
	if (pmd_none(pmd))
		return 0;

	if (pmd_sect(pmd))
		return __pfn_to_phys(pmd_pfn(pmd)) | (iova & ~PMD_MASK);

	pte = *(pmd_page_vaddr(pmd) + pte_index(iova));
	if (pte_none(pte))
		return 0;

	return __pfn_to_phys(pte_pfn(pte)) | (iova & ~PAGE_MASK);
}

static int ipmmu_find_utlb(struct ipmmu_vmsa_device *mmu, struct device *dev)
{
	const struct ipmmu_vmsa_master *master = mmu->pdata->masters;
	const char *devname = dev_name(dev);
	unsigned int i;

	for (i = 0; i < mmu->pdata->num_masters; ++i, ++master) {
		if (strcmp(master->name, devname) == 0)
			return master->utlb;
	}

	return -1;
}

static int ipmmu_add_device(struct device *dev)
{
	struct ipmmu_vmsa_archdata *archdata;
	struct ipmmu_vmsa_device *mmu;
	struct iommu_group *group;
	int utlb = -1;
	int ret;

	if (dev->archdata.iommu) {
		dev_warn(dev, "IOMMU driver already assigned to device %s\n",
			 dev_name(dev));
		return -EINVAL;
	}

	/* Find the master corresponding to the device. */
	spin_lock(&ipmmu_devices_lock);

	list_for_each_entry(mmu, &ipmmu_devices, list) {
		utlb = ipmmu_find_utlb(mmu, dev);
		if (utlb >= 0) {
			/*
			 * TODO Take a reference to the MMU to protect
			 * against device removal.
			 */
			break;
		}
	}

	spin_unlock(&ipmmu_devices_lock);

	if (utlb < 0)
		return -ENODEV;

	if (utlb >= mmu->num_utlbs)
		return -EINVAL;

	/* Create a device group and add the device to it. */
	group = iommu_group_alloc();
	if (IS_ERR(group)) {
		dev_err(dev, "Failed to allocate IOMMU group\n");
		return PTR_ERR(group);
	}

	ret = iommu_group_add_device(group, dev);
	iommu_group_put(group);

	if (ret < 0) {
		dev_err(dev, "Failed to add device to IPMMU group\n");
		return ret;
	}

	archdata = kzalloc(sizeof(*archdata), GFP_KERNEL);
	if (!archdata) {
		ret = -ENOMEM;
		goto error;
	}

	archdata->mmu = mmu;
	archdata->utlb = utlb;
	dev->archdata.iommu = archdata;

	/*
	 * Create the ARM mapping, used by the ARM DMA mapping core to allocate
	 * VAs. This will allocate a corresponding IOMMU domain.
	 *
	 * TODO:
	 * - Create one mapping per context (TLB).
	 * - Make the mapping size configurable ? We currently use a 2GB mapping
	 *   at a 1GB offset to ensure that NULL VAs will fault.
	 */
	if (!mmu->mapping) {
		struct dma_iommu_mapping *mapping;

		mapping = arm_iommu_create_mapping(&platform_bus_type,
						   SZ_1G, SZ_2G);
		if (IS_ERR(mapping)) {
			dev_err(mmu->dev, "failed to create ARM IOMMU mapping\n");
			return PTR_ERR(mapping);
		}

		mmu->mapping = mapping;
	}

	/* Attach the ARM VA mapping to the device. */
	ret = arm_iommu_attach_device(dev, mmu->mapping);
	if (ret < 0) {
		dev_err(dev, "Failed to attach device to VA mapping\n");
		goto error;
	}

	return 0;

error:
	kfree(dev->archdata.iommu);
	dev->archdata.iommu = NULL;
	iommu_group_remove_device(dev);
	return ret;
}

static void ipmmu_remove_device(struct device *dev)
{
	arm_iommu_detach_device(dev);
	iommu_group_remove_device(dev);
	kfree(dev->archdata.iommu);
	dev->archdata.iommu = NULL;
}

static const struct iommu_ops ipmmu_ops = {
	.domain_init = ipmmu_domain_init,
	.domain_destroy = ipmmu_domain_destroy,
	.attach_dev = ipmmu_attach_device,
	.detach_dev = ipmmu_detach_device,
	.map = ipmmu_map,
	.unmap = ipmmu_unmap,
	.map_sg = default_iommu_map_sg,
	.iova_to_phys = ipmmu_iova_to_phys,
	.add_device = ipmmu_add_device,
	.remove_device = ipmmu_remove_device,
	.pgsize_bitmap = SZ_2M | SZ_64K | SZ_4K,
};

/* -----------------------------------------------------------------------------
 * Probe/remove and init
 */

static void ipmmu_device_reset(struct ipmmu_vmsa_device *mmu)
{
	unsigned int i;

	/* Disable all contexts. */
	for (i = 0; i < 4; ++i)
		ipmmu_write(mmu, i * IM_CTX_SIZE + IMCTR, 0);
}

static int ipmmu_probe(struct platform_device *pdev)
{
	struct ipmmu_vmsa_device *mmu;
	struct resource *res;
	int irq;
	int ret;

	if (!pdev->dev.platform_data) {
		dev_err(&pdev->dev, "missing platform data\n");
		return -EINVAL;
	}

	mmu = devm_kzalloc(&pdev->dev, sizeof(*mmu), GFP_KERNEL);
	if (!mmu) {
		dev_err(&pdev->dev, "cannot allocate device data\n");
		return -ENOMEM;
	}

	mmu->dev = &pdev->dev;
	mmu->pdata = pdev->dev.platform_data;
	mmu->num_utlbs = 32;

	/* Map I/O memory and request IRQ. */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	mmu->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(mmu->base))
		return PTR_ERR(mmu->base);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(&pdev->dev, "no IRQ found\n");
		return irq;
	}

	ret = devm_request_irq(&pdev->dev, irq, ipmmu_irq, 0,
			       dev_name(&pdev->dev), mmu);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to request IRQ %d\n", irq);
		return irq;
	}

	ipmmu_device_reset(mmu);

	/*
	 * We can't create the ARM mapping here as it requires the bus to have
	 * an IOMMU, which only happens when bus_set_iommu() is called in
	 * ipmmu_init() after the probe function returns.
	 */

	spin_lock(&ipmmu_devices_lock);
	list_add(&mmu->list, &ipmmu_devices);
	spin_unlock(&ipmmu_devices_lock);

	platform_set_drvdata(pdev, mmu);

	return 0;
}

static int ipmmu_remove(struct platform_device *pdev)
{
	struct ipmmu_vmsa_device *mmu = platform_get_drvdata(pdev);

	spin_lock(&ipmmu_devices_lock);
	list_del(&mmu->list);
	spin_unlock(&ipmmu_devices_lock);

	arm_iommu_release_mapping(mmu->mapping);

	ipmmu_device_reset(mmu);

	return 0;
}

static struct platform_driver ipmmu_driver = {
	.driver = {
		.owner = THIS_MODULE,
		.name = "ipmmu-vmsa",
	},
	.probe = ipmmu_probe,
	.remove	= ipmmu_remove,
};

static int __init ipmmu_init(void)
{
	int ret;

	ret = platform_driver_register(&ipmmu_driver);
	if (ret < 0)
		return ret;

	if (!iommu_present(&platform_bus_type))
		bus_set_iommu(&platform_bus_type, &ipmmu_ops);

	return 0;
}

static void __exit ipmmu_exit(void)
{
	return platform_driver_unregister(&ipmmu_driver);
}

subsys_initcall(ipmmu_init);
module_exit(ipmmu_exit);

MODULE_DESCRIPTION("IOMMU API for Renesas VMSA-compatible IPMMU");
MODULE_AUTHOR("Laurent Pinchart <laurent.pinchart@ideasonboard.com>");
MODULE_LICENSE("GPL v2");

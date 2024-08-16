// SPDX-License-Identifier: GPL-2.0-only
/*
 * AMD SVM-SEV Host Support.
 *
 * Copyright (C) 2023 Advanced Micro Devices, Inc.
 *
 * Author: Ashish Kalra <ashish.kalra@amd.com>
 *
 */

#include <linux/cc_platform.h>
#include <linux/printk.h>
#include <linux/mm_types.h>
#include <linux/set_memory.h>
#include <linux/memblock.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/cpumask.h>
#include <linux/iommu.h>
#include <linux/amd-iommu.h>

#include <asm/sev.h>
#include <asm/processor.h>
#include <asm/setup.h>
#include <asm/svm.h>
#include <asm/smp.h>
#include <asm/cpu.h>
#include <asm/apic.h>
#include <asm/cpuid.h>
#include <asm/cmdline.h>
#include <asm/iommu.h>

/*
 * The RMP entry format is not architectural. The format is defined in PPR
 * Family 19h Model 01h, Rev B1 processor.
 */
struct rmpentry {
	union {
		struct {
			u64 assigned	: 1,
			    pagesize	: 1,
			    immutable	: 1,
			    rsvd1	: 9,
			    gpa		: 39,
			    asid	: 10,
			    vmsa	: 1,
			    validated	: 1,
			    rsvd2	: 1;
		};
		u64 lo;
	};
	u64 hi;
} __packed;

/*
 * The first 16KB from the RMP_BASE is used by the processor for the
 * bookkeeping, the range needs to be added during the RMP entry lookup.
 */
#define RMPTABLE_CPU_BOOKKEEPING_SZ	0x4000

/* Mask to apply to a PFN to get the first PFN of a 2MB page */
#define PFN_PMD_MASK	GENMASK_ULL(63, PMD_SHIFT - PAGE_SHIFT)

static u64 probed_rmp_base, probed_rmp_size;
static struct rmpentry *rmptable __ro_after_init;
static u64 rmptable_max_pfn __ro_after_init;

static LIST_HEAD(snp_leaked_pages_list);
static DEFINE_SPINLOCK(snp_leaked_pages_list_lock);

static unsigned long snp_nr_leaked_pages;

#undef pr_fmt
#define pr_fmt(fmt)	"SEV-SNP: " fmt

static int __mfd_enable(unsigned int cpu)
{
	u64 val;

	if (!cc_platform_has(CC_ATTR_HOST_SEV_SNP))
		return 0;

	rdmsrl(MSR_AMD64_SYSCFG, val);

	val |= MSR_AMD64_SYSCFG_MFDM;

	wrmsrl(MSR_AMD64_SYSCFG, val);

	return 0;
}

static __init void mfd_enable(void *arg)
{
	__mfd_enable(smp_processor_id());
}

static int __snp_enable(unsigned int cpu)
{
	u64 val;

	if (!cc_platform_has(CC_ATTR_HOST_SEV_SNP))
		return 0;

	rdmsrl(MSR_AMD64_SYSCFG, val);

	val |= MSR_AMD64_SYSCFG_SNP_EN;
	val |= MSR_AMD64_SYSCFG_SNP_VMPL_EN;

	wrmsrl(MSR_AMD64_SYSCFG, val);

	return 0;
}

static __init void snp_enable(void *arg)
{
	__snp_enable(smp_processor_id());
}

#define RMP_ADDR_MASK GENMASK_ULL(51, 13)

bool snp_probe_rmptable_info(void)
{
	u64 rmp_sz, rmp_base, rmp_end;

	rdmsrl(MSR_AMD64_RMP_BASE, rmp_base);
	rdmsrl(MSR_AMD64_RMP_END, rmp_end);

	if (!(rmp_base & RMP_ADDR_MASK) || !(rmp_end & RMP_ADDR_MASK)) {
		pr_err("Memory for the RMP table has not been reserved by BIOS\n");
		return false;
	}

	if (rmp_base > rmp_end) {
		pr_err("RMP configuration not valid: base=%#llx, end=%#llx\n", rmp_base, rmp_end);
		return false;
	}

	rmp_sz = rmp_end - rmp_base + 1;

	probed_rmp_base = rmp_base;
	probed_rmp_size = rmp_sz;

	pr_info("RMP table physical range [0x%016llx - 0x%016llx]\n",
		rmp_base, rmp_end);

	return true;
}

static void __init __snp_fixup_e820_tables(u64 pa)
{
	if (IS_ALIGNED(pa, PMD_SIZE))
		return;

	/*
	 * Handle cases where the RMP table placement by the BIOS is not
	 * 2M aligned and the kexec kernel could try to allocate
	 * from within that chunk which then causes a fatal RMP fault.
	 *
	 * The e820_table needs to be updated as it is converted to
	 * kernel memory resources and used by KEXEC_FILE_LOAD syscall
	 * to load kexec segments.
	 *
	 * The e820_table_firmware needs to be updated as it is exposed
	 * to sysfs and used by the KEXEC_LOAD syscall to load kexec
	 * segments.
	 *
	 * The e820_table_kexec needs to be updated as it passed to
	 * the kexec-ed kernel.
	 */
	pa = ALIGN_DOWN(pa, PMD_SIZE);
	if (e820__mapped_any(pa, pa + PMD_SIZE, E820_TYPE_RAM)) {
		pr_info("Reserving start/end of RMP table on a 2MB boundary [0x%016llx]\n", pa);
		e820__range_update(pa, PMD_SIZE, E820_TYPE_RAM, E820_TYPE_RESERVED);
		e820__range_update_table(e820_table_kexec, pa, PMD_SIZE, E820_TYPE_RAM, E820_TYPE_RESERVED);
		e820__range_update_table(e820_table_firmware, pa, PMD_SIZE, E820_TYPE_RAM, E820_TYPE_RESERVED);
	}
}

void __init snp_fixup_e820_tables(void)
{
	__snp_fixup_e820_tables(probed_rmp_base);
	__snp_fixup_e820_tables(probed_rmp_base + probed_rmp_size);
}

/*
 * Do the necessary preparations which are verified by the firmware as
 * described in the SNP_INIT_EX firmware command description in the SNP
 * firmware ABI spec.
 */
static int __init snp_rmptable_init(void)
{
	u64 max_rmp_pfn, calc_rmp_sz, rmptable_size, rmp_end, val;
	void *rmptable_start;

	if (!cc_platform_has(CC_ATTR_HOST_SEV_SNP))
		return 0;

	if (!amd_iommu_snp_en)
		goto nosnp;

	if (!probed_rmp_size)
		goto nosnp;

	rmp_end = probed_rmp_base + probed_rmp_size - 1;

	/*
	 * Calculate the amount the memory that must be reserved by the BIOS to
	 * address the whole RAM, including the bookkeeping area. The RMP itself
	 * must also be covered.
	 */
	max_rmp_pfn = max_pfn;
	if (PFN_UP(rmp_end) > max_pfn)
		max_rmp_pfn = PFN_UP(rmp_end);

	calc_rmp_sz = (max_rmp_pfn << 4) + RMPTABLE_CPU_BOOKKEEPING_SZ;
	if (calc_rmp_sz > probed_rmp_size) {
		pr_err("Memory reserved for the RMP table does not cover full system RAM (expected 0x%llx got 0x%llx)\n",
		       calc_rmp_sz, probed_rmp_size);
		goto nosnp;
	}

	rmptable_start = memremap(probed_rmp_base, probed_rmp_size, MEMREMAP_WB);
	if (!rmptable_start) {
		pr_err("Failed to map RMP table\n");
		goto nosnp;
	}

	/*
	 * Check if SEV-SNP is already enabled, this can happen in case of
	 * kexec boot.
	 */
	rdmsrl(MSR_AMD64_SYSCFG, val);
	if (val & MSR_AMD64_SYSCFG_SNP_EN)
		goto skip_enable;

	memset(rmptable_start, 0, probed_rmp_size);

	/* Flush the caches to ensure that data is written before SNP is enabled. */
	wbinvd_on_all_cpus();

	/* MtrrFixDramModEn must be enabled on all the CPUs prior to enabling SNP. */
	on_each_cpu(mfd_enable, NULL, 1);

	on_each_cpu(snp_enable, NULL, 1);

skip_enable:
	rmptable_start += RMPTABLE_CPU_BOOKKEEPING_SZ;
	rmptable_size = probed_rmp_size - RMPTABLE_CPU_BOOKKEEPING_SZ;

	rmptable = (struct rmpentry *)rmptable_start;
	rmptable_max_pfn = rmptable_size / sizeof(struct rmpentry) - 1;

	cpuhp_setup_state(CPUHP_AP_ONLINE_DYN, "x86/rmptable_init:online", __snp_enable, NULL);

	/*
	 * Setting crash_kexec_post_notifiers to 'true' to ensure that SNP panic
	 * notifier is invoked to do SNP IOMMU shutdown before kdump.
	 */
	crash_kexec_post_notifiers = true;

	return 0;

nosnp:
	cc_platform_clear(CC_ATTR_HOST_SEV_SNP);
	return -ENOSYS;
}

/*
 * This must be called after the IOMMU has been initialized.
 */
device_initcall(snp_rmptable_init);

static struct rmpentry *get_rmpentry(u64 pfn)
{
	if (WARN_ON_ONCE(pfn > rmptable_max_pfn))
		return ERR_PTR(-EFAULT);

	return &rmptable[pfn];
}

static struct rmpentry *__snp_lookup_rmpentry(u64 pfn, int *level)
{
	struct rmpentry *large_entry, *entry;

	if (!cc_platform_has(CC_ATTR_HOST_SEV_SNP))
		return ERR_PTR(-ENODEV);

	entry = get_rmpentry(pfn);
	if (IS_ERR(entry))
		return entry;

	/*
	 * Find the authoritative RMP entry for a PFN. This can be either a 4K
	 * RMP entry or a special large RMP entry that is authoritative for a
	 * whole 2M area.
	 */
	large_entry = get_rmpentry(pfn & PFN_PMD_MASK);
	if (IS_ERR(large_entry))
		return large_entry;

	*level = RMP_TO_PG_LEVEL(large_entry->pagesize);

	return entry;
}

int snp_lookup_rmpentry(u64 pfn, bool *assigned, int *level)
{
	struct rmpentry *e;

	e = __snp_lookup_rmpentry(pfn, level);
	if (IS_ERR(e))
		return PTR_ERR(e);

	*assigned = !!e->assigned;
	return 0;
}
EXPORT_SYMBOL_GPL(snp_lookup_rmpentry);

/*
 * Dump the raw RMP entry for a particular PFN. These bits are documented in the
 * PPR for a particular CPU model and provide useful information about how a
 * particular PFN is being utilized by the kernel/firmware at the time certain
 * unexpected events occur, such as RMP faults.
 */
static void dump_rmpentry(u64 pfn)
{
	u64 pfn_i, pfn_end;
	struct rmpentry *e;
	int level;

	e = __snp_lookup_rmpentry(pfn, &level);
	if (IS_ERR(e)) {
		pr_err("Failed to read RMP entry for PFN 0x%llx, error %ld\n",
		       pfn, PTR_ERR(e));
		return;
	}

	if (e->assigned) {
		pr_info("PFN 0x%llx, RMP entry: [0x%016llx - 0x%016llx]\n",
			pfn, e->lo, e->hi);
		return;
	}

	/*
	 * If the RMP entry for a particular PFN is not in an assigned state,
	 * then it is sometimes useful to get an idea of whether or not any RMP
	 * entries for other PFNs within the same 2MB region are assigned, since
	 * those too can affect the ability to access a particular PFN in
	 * certain situations, such as when the PFN is being accessed via a 2MB
	 * mapping in the host page table.
	 */
	pfn_i = ALIGN_DOWN(pfn, PTRS_PER_PMD);
	pfn_end = pfn_i + PTRS_PER_PMD;

	pr_info("PFN 0x%llx unassigned, dumping non-zero entries in 2M PFN region: [0x%llx - 0x%llx]\n",
		pfn, pfn_i, pfn_end);

	while (pfn_i < pfn_end) {
		e = __snp_lookup_rmpentry(pfn_i, &level);
		if (IS_ERR(e)) {
			pr_err("Error %ld reading RMP entry for PFN 0x%llx\n",
			       PTR_ERR(e), pfn_i);
			pfn_i++;
			continue;
		}

		if (e->lo || e->hi)
			pr_info("PFN: 0x%llx, [0x%016llx - 0x%016llx]\n", pfn_i, e->lo, e->hi);
		pfn_i++;
	}
}

void snp_dump_hva_rmpentry(unsigned long hva)
{
	unsigned long paddr;
	unsigned int level;
	pgd_t *pgd;
	pte_t *pte;

	pgd = __va(read_cr3_pa());
	pgd += pgd_index(hva);
	pte = lookup_address_in_pgd(pgd, hva, &level);

	if (!pte) {
		pr_err("Can't dump RMP entry for HVA %lx: no PTE/PFN found\n", hva);
		return;
	}

	paddr = PFN_PHYS(pte_pfn(*pte)) | (hva & ~page_level_mask(level));
	dump_rmpentry(PHYS_PFN(paddr));
}

/*
 * PSMASH a 2MB aligned page into 4K pages in the RMP table while preserving the
 * Validated bit.
 */
int psmash(u64 pfn)
{
	unsigned long paddr = pfn << PAGE_SHIFT;
	int ret;

	if (!cc_platform_has(CC_ATTR_HOST_SEV_SNP))
		return -ENODEV;

	if (!pfn_valid(pfn))
		return -EINVAL;

	/* Binutils version 2.36 supports the PSMASH mnemonic. */
	asm volatile(".byte 0xF3, 0x0F, 0x01, 0xFF"
		      : "=a" (ret)
		      : "a" (paddr)
		      : "memory", "cc");

	return ret;
}
EXPORT_SYMBOL_GPL(psmash);

/*
 * If the kernel uses a 2MB or larger directmap mapping to write to an address,
 * and that mapping contains any 4KB pages that are set to private in the RMP
 * table, an RMP #PF will trigger and cause a host crash. Hypervisor code that
 * owns the PFNs being transitioned will never attempt such a write, but other
 * kernel tasks writing to other PFNs in the range may trigger these checks
 * inadvertently due a large directmap mapping that happens to overlap such a
 * PFN.
 *
 * Prevent this by splitting any 2MB+ mappings that might end up containing a
 * mix of private/shared PFNs as a result of a subsequent RMPUPDATE for the
 * PFN/rmp_level passed in.
 *
 * Note that there is no attempt here to scan all the RMP entries for the 2MB
 * physical range, since it would only be worthwhile in determining if a
 * subsequent RMPUPDATE for a 4KB PFN would result in all the entries being of
 * the same shared/private state, thus avoiding the need to split the mapping.
 * But that would mean the entries are currently in a mixed state, and so the
 * mapping would have already been split as a result of prior transitions.
 * And since the 4K split is only done if the mapping is 2MB+, and there isn't
 * currently a mechanism in place to restore 2MB+ mappings, such a check would
 * not provide any usable benefit.
 *
 * More specifics on how these checks are carried out can be found in APM
 * Volume 2, "RMP and VMPL Access Checks".
 */
static int adjust_direct_map(u64 pfn, int rmp_level)
{
	unsigned long vaddr;
	unsigned int level;
	int npages, ret;
	pte_t *pte;

	/*
	 * pfn_to_kaddr() will return a vaddr only within the direct
	 * map range.
	 */
	vaddr = (unsigned long)pfn_to_kaddr(pfn);

	/* Only 4KB/2MB RMP entries are supported by current hardware. */
	if (WARN_ON_ONCE(rmp_level > PG_LEVEL_2M))
		return -EINVAL;

	if (!pfn_valid(pfn))
		return -EINVAL;

	if (rmp_level == PG_LEVEL_2M &&
	    (!IS_ALIGNED(pfn, PTRS_PER_PMD) || !pfn_valid(pfn + PTRS_PER_PMD - 1)))
		return -EINVAL;

	/*
	 * If an entire 2MB physical range is being transitioned, then there is
	 * no risk of RMP #PFs due to write accesses from overlapping mappings,
	 * since even accesses from 1GB mappings will be treated as 2MB accesses
	 * as far as RMP table checks are concerned.
	 */
	if (rmp_level == PG_LEVEL_2M)
		return 0;

	pte = lookup_address(vaddr, &level);
	if (!pte || pte_none(*pte))
		return 0;

	if (level == PG_LEVEL_4K)
		return 0;

	npages = page_level_size(rmp_level) / PAGE_SIZE;
	ret = set_memory_4k(vaddr, npages);
	if (ret)
		pr_warn("Failed to split direct map for PFN 0x%llx, ret: %d\n",
			pfn, ret);

	return ret;
}

/*
 * It is expected that those operations are seldom enough so that no mutual
 * exclusion of updaters is needed and thus the overlap error condition below
 * should happen very rarely and would get resolved relatively quickly by
 * the firmware.
 *
 * If not, one could consider introducing a mutex or so here to sync concurrent
 * RMP updates and thus diminish the amount of cases where firmware needs to
 * lock 2M ranges to protect against concurrent updates.
 *
 * The optimal solution would be range locking to avoid locking disjoint
 * regions unnecessarily but there's no support for that yet.
 */
static int rmpupdate(u64 pfn, struct rmp_state *state)
{
	unsigned long paddr = pfn << PAGE_SHIFT;
	int ret, level;

	if (!cc_platform_has(CC_ATTR_HOST_SEV_SNP))
		return -ENODEV;

	level = RMP_TO_PG_LEVEL(state->pagesize);

	if (adjust_direct_map(pfn, level))
		return -EFAULT;

	do {
		/* Binutils version 2.36 supports the RMPUPDATE mnemonic. */
		asm volatile(".byte 0xF2, 0x0F, 0x01, 0xFE"
			     : "=a" (ret)
			     : "a" (paddr), "c" ((unsigned long)state)
			     : "memory", "cc");
	} while (ret == RMPUPDATE_FAIL_OVERLAP);

	if (ret) {
		pr_err("RMPUPDATE failed for PFN %llx, pg_level: %d, ret: %d\n",
		       pfn, level, ret);
		dump_rmpentry(pfn);
		dump_stack();
		return -EFAULT;
	}

	return 0;
}

/* Transition a page to guest-owned/private state in the RMP table. */
int rmp_make_private(u64 pfn, u64 gpa, enum pg_level level, u32 asid, bool immutable)
{
	struct rmp_state state;

	memset(&state, 0, sizeof(state));
	state.assigned = 1;
	state.asid = asid;
	state.immutable = immutable;
	state.gpa = gpa;
	state.pagesize = PG_LEVEL_TO_RMP(level);

	return rmpupdate(pfn, &state);
}
EXPORT_SYMBOL_GPL(rmp_make_private);

/* Transition a page to hypervisor-owned/shared state in the RMP table. */
int rmp_make_shared(u64 pfn, enum pg_level level)
{
	struct rmp_state state;

	memset(&state, 0, sizeof(state));
	state.pagesize = PG_LEVEL_TO_RMP(level);

	return rmpupdate(pfn, &state);
}
EXPORT_SYMBOL_GPL(rmp_make_shared);

void snp_leak_pages(u64 pfn, unsigned int npages)
{
	struct page *page = pfn_to_page(pfn);

	pr_warn("Leaking PFN range 0x%llx-0x%llx\n", pfn, pfn + npages);

	spin_lock(&snp_leaked_pages_list_lock);
	while (npages--) {

		/*
		 * Reuse the page's buddy list for chaining into the leaked
		 * pages list. This page should not be on a free list currently
		 * and is also unsafe to be added to a free list.
		 */
		if (likely(!PageCompound(page)) ||

			/*
			 * Skip inserting tail pages of compound page as
			 * page->buddy_list of tail pages is not usable.
			 */
		    (PageHead(page) && compound_nr(page) <= npages))
			list_add_tail(&page->buddy_list, &snp_leaked_pages_list);

		dump_rmpentry(pfn);
		snp_nr_leaked_pages++;
		pfn++;
		page++;
	}
	spin_unlock(&snp_leaked_pages_list_lock);
}
EXPORT_SYMBOL_GPL(snp_leak_pages);

void kdump_sev_callback(void)
{
	/*
	 * Do wbinvd() on remote CPUs when SNP is enabled in order to
	 * safely do SNP_SHUTDOWN on the local CPU.
	 */
	if (cc_platform_has(CC_ATTR_HOST_SEV_SNP))
		wbinvd();
}

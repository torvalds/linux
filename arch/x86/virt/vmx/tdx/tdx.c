// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright(c) 2023 Intel Corporation.
 *
 * Intel Trusted Domain Extensions (TDX) support
 */

#include "asm/page_types.h"
#define pr_fmt(fmt)	"virt/tdx: " fmt

#include <linux/types.h>
#include <linux/cache.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/printk.h>
#include <linux/cpu.h>
#include <linux/spinlock.h>
#include <linux/percpu-defs.h>
#include <linux/mutex.h>
#include <linux/list.h>
#include <linux/memblock.h>
#include <linux/memory.h>
#include <linux/minmax.h>
#include <linux/sizes.h>
#include <linux/pfn.h>
#include <linux/align.h>
#include <linux/sort.h>
#include <linux/log2.h>
#include <linux/acpi.h>
#include <linux/suspend.h>
#include <linux/syscore_ops.h>
#include <linux/idr.h>
#include <linux/vmalloc.h>
#include <asm/page.h>
#include <asm/special_insns.h>
#include <asm/msr-index.h>
#include <asm/msr.h>
#include <asm/cpufeature.h>
#include <asm/tdx.h>
#include <asm/shared/tdx_errno.h>
#include <asm/cpu_device_id.h>
#include <asm/processor.h>
#include <asm/mce.h>
#include <asm/virt.h>
#include <asm/vmx.h>

#include "seamcall_internal.h"
#include "tdx.h"

/* Number of DPAMT pages to be provided to TDX module per 2MB region of PA */
#define TDX_DPAMT_ENTRY_PAGE_CNT 2

struct tdx_module_state {
	bool initialized;
	bool sysinit_done;
	int sysinit_ret;
};

static struct tdx_module_state tdx_module_state;
static u32 tdx_global_keyid __ro_after_init;
static u32 tdx_guest_keyid_start __ro_after_init;
static u32 tdx_nr_guest_keyids __ro_after_init;

static DEFINE_IDA(tdx_guest_keyid_pool);

static DEFINE_PER_CPU(bool, tdx_lp_initialized);

static struct tdmr_info_list tdx_tdmr_list;

/*
 * On a machine with DPAMT, the kernel maintains a reference counter
 * for every 2MB range. The counter indicates how many users there are for
 * the DPAMT at the 2MB range. The kernel allocates DPAMT refcounts at
 * initialization.
 */
static atomic_t *dpamt_refcounts;

/* All TDX-usable memory regions.  Protected by mem_hotplug_lock. */
static LIST_HEAD(tdx_memlist);

static struct tdx_sys_info tdx_sysinfo;

static DEFINE_RAW_SPINLOCK(sysinit_lock);

/*
 * Do the module global initialization once and return its result.
 * It can be done on any cpu, and from task or IRQ context.
 */
static int try_init_module_global(void)
{
	struct tdx_module_args args = {};
	int ret;

	raw_spin_lock(&sysinit_lock);

	/* Return the "cached" return code. */
	if (tdx_module_state.sysinit_done) {
		ret = tdx_module_state.sysinit_ret;
		goto out;
	}

	/* RCX is module attributes and all bits are reserved */
	args.rcx = 0;
	ret = seamcall_prerr(TDH_SYS_INIT, &args);

	/*
	 * The first SEAMCALL also detects the TDX module, thus
	 * it can fail due to the TDX module is not loaded.
	 * Dump message to let the user know.
	 */
	if (ret == -ENODEV)
		pr_err("module not loaded\n");

	/* Save the return code for later callers. */
	tdx_module_state.sysinit_done = true;
	tdx_module_state.sysinit_ret = ret;
out:
	raw_spin_unlock(&sysinit_lock);
	return ret;
}

/**
 * Enable VMXON and then do one-time TDX module per-cpu initialization SEAMCALL
 * (and TDX module global initialization SEAMCALL if not done) on local cpu to
 * make this cpu be ready to run any other SEAMCALLs.
 */
int tdx_cpu_enable(void)
{
	struct tdx_module_args args = {};
	int ret;

	if (__this_cpu_read(tdx_lp_initialized))
		return 0;

	/*
	 * The TDX module global initialization is the very first step
	 * to enable TDX.  Need to do it first (if hasn't been done)
	 * before the per-cpu initialization.
	 */
	ret = try_init_module_global();
	if (ret)
		return ret;

	ret = seamcall_prerr(TDH_SYS_LP_INIT, &args);
	if (ret)
		return ret;

	__this_cpu_write(tdx_lp_initialized, true);

	return 0;
}

static int tdx_online_cpu(unsigned int cpu)
{
	int ret;

	ret = x86_virt_get_ref(X86_FEATURE_VMX);
	if (ret)
		return ret;

	ret = tdx_cpu_enable();
	if (ret)
		x86_virt_put_ref(X86_FEATURE_VMX);

	return ret;
}

static void tdx_cpu_flush_cache(void)
{
	lockdep_assert_preemption_disabled();

	if (!this_cpu_read(cache_state_incoherent))
		return;

	wbinvd();
	this_cpu_write(cache_state_incoherent, false);
}

static int tdx_offline_cpu(unsigned int cpu)
{
	int i;

	/* No TD is running.  Allow any cpu to be offline. */
	if (ida_is_empty(&tdx_guest_keyid_pool))
		goto done;

	/*
	 * In order to reclaim TDX HKID, (i.e. when deleting guest TD), need to
	 * call TDH.PHYMEM.PAGE.WBINVD on all packages to program all memory
	 * controller with pconfig.  If we have active TDX HKID, refuse to
	 * offline the last online cpu.
	 */
	for_each_online_cpu(i) {
		/*
		 * Found another online cpu on the same package.
		 * Allow to offline.
		 */
		if (i != cpu && topology_physical_package_id(i) ==
				topology_physical_package_id(cpu))
			goto done;
	}

	/*
	 * This is the last cpu of this package.  Don't offline it.
	 *
	 * Because it's hard for human operator to understand the
	 * reason, warn it.
	 */
#define MSG_ALLPKG_ONLINE \
	"TDX requires all packages to have an online CPU. Delete all TDs in order to offline all CPUs of a package.\n"
	pr_warn_ratelimited(MSG_ALLPKG_ONLINE);
	return -EBUSY;

done:
	/*
	 * Flush cache on the CPU going offline to ensure no dirty
	 * cachelines of TDX private memory remain. This may be
	 * redundant with WBINVD done elsewhere during CPU offline
	 * (e.g. hlt_play_dead()), but do it explicitly for safety.
	 */
	tdx_cpu_flush_cache();
	x86_virt_put_ref(X86_FEATURE_VMX);
	return 0;
}

static void tdx_shutdown_cpu(void *ign)
{
	/*
	 * Flush cache in preparation for kexec - this is necessary to avoid
	 * having dirty private memory cachelines when the new kernel boots,
	 * but WBINVD is a relatively expensive operation and doing it during
	 * kexec can exacerbate races in native_stop_other_cpus().  Do it
	 * now, since this is a safe moment and there is going to be no more
	 * TDX activity on this CPU from this point on.
	 */
	tdx_cpu_flush_cache();
	x86_virt_put_ref(X86_FEATURE_VMX);
}

static void tdx_shutdown(void *ign)
{
	tdx_sys_disable();
	on_each_cpu(tdx_shutdown_cpu, NULL, 1);
}

static int tdx_suspend(void *ign)
{
	x86_virt_put_ref(X86_FEATURE_VMX);
	return 0;
}

static void tdx_resume(void *ign)
{
	WARN_ON_ONCE(x86_virt_get_ref(X86_FEATURE_VMX));
}

static const struct syscore_ops tdx_syscore_ops = {
	.suspend = tdx_suspend,
	.resume = tdx_resume,
	.shutdown = tdx_shutdown,
};

static struct syscore tdx_syscore = {
	.ops = &tdx_syscore_ops,
};

/*
 * Allocate DPAMT reference counters for all physical memory.
 *
 * It consumes 2MB for every 1TB of physical memory.
 */
static __init int init_dpamt_refcounts(void)
{
	size_t size = DIV_ROUND_UP(max_pfn, PTRS_PER_PTE) * sizeof(*dpamt_refcounts);

	if (!tdx_supports_dynamic_pamt(&tdx_sysinfo))
		return 0;

	dpamt_refcounts = vzalloc(size);
	if (!dpamt_refcounts)
		return -ENOMEM;

	return 0;
}

static __init void free_dpamt_refcounts(void)
{
	if (!tdx_supports_dynamic_pamt(&tdx_sysinfo))
		return;

	vfree(dpamt_refcounts);
	dpamt_refcounts = NULL;
}

static atomic_t *tdx_find_dpamt_refcount(unsigned long pfn)
{
	/* Find which PMD a PFN is in. */
	unsigned long index = pfn >> (PMD_SHIFT - PAGE_SHIFT);

	return &dpamt_refcounts[index];
}

/*
 * Add a memory region as a TDX memory block.  The caller must make sure
 * all memory regions are added in address ascending order and don't
 * overlap.
 */
static __init int add_tdx_memblock(struct list_head *tmb_list,
				   unsigned long start_pfn,
				   unsigned long end_pfn, int nid)
{
	struct tdx_memblock *tmb;

	tmb = kmalloc_obj(*tmb);
	if (!tmb)
		return -ENOMEM;

	INIT_LIST_HEAD(&tmb->list);
	tmb->start_pfn = start_pfn;
	tmb->end_pfn = end_pfn;
	tmb->nid = nid;

	/* @tmb_list is protected by mem_hotplug_lock */
	list_add_tail(&tmb->list, tmb_list);
	return 0;
}

static __init void free_tdx_memlist(struct list_head *tmb_list)
{
	/* @tmb_list is protected by mem_hotplug_lock */
	while (!list_empty(tmb_list)) {
		struct tdx_memblock *tmb = list_first_entry(tmb_list,
				struct tdx_memblock, list);

		list_del(&tmb->list);
		kfree(tmb);
	}
}

/*
 * Ensure that all memblock memory regions are convertible to TDX
 * memory.  Once this has been established, stash the memblock
 * ranges off in a secondary structure because memblock is modified
 * in memory hotplug while TDX memory regions are fixed.
 */
static __init int build_tdx_memlist(struct list_head *tmb_list)
{
	unsigned long start_pfn, end_pfn;
	int i, nid, ret;

	for_each_mem_pfn_range(i, MAX_NUMNODES, &start_pfn, &end_pfn, &nid) {
		/*
		 * The first 1MB is not reported as TDX convertible memory.
		 * Although the first 1MB is always reserved and won't end up
		 * to the page allocator, it is still in memblock's memory
		 * regions.  Skip them manually to exclude them as TDX memory.
		 */
		start_pfn = max(start_pfn, PHYS_PFN(SZ_1M));
		if (start_pfn >= end_pfn)
			continue;

		/*
		 * Add the memory regions as TDX memory.  The regions in
		 * memblock has already guaranteed they are in address
		 * ascending order and don't overlap.
		 */
		ret = add_tdx_memblock(tmb_list, start_pfn, end_pfn, nid);
		if (ret)
			goto err;
	}

	return 0;
err:
	free_tdx_memlist(tmb_list);
	return ret;
}

static int read_sys_metadata_field(u64 field_id, u64 *data)
{
	struct tdx_module_args args = {};
	int ret;

	/*
	 * TDH.SYS.RD -- reads one global metadata field
	 *  - RDX (in): the field to read
	 *  - R8 (out): the field data
	 */
	args.rdx = field_id;
	ret = seamcall_prerr_ret(TDH_SYS_RD, &args);
	if (ret)
		return ret;

	*data = args.r8;

	return 0;
}

#include "tdx_global_metadata.c"

static __init int check_features(struct tdx_sys_info *sysinfo)
{
	u64 tdx_features0 = sysinfo->features.tdx_features0;

	if (!(tdx_features0 & TDX_FEATURES0_NO_RBP_MOD)) {
		pr_err("frame pointer (RBP) clobber bug present, upgrade TDX module\n");
		return -EINVAL;
	}

	return 0;
}

/* Calculate the actual TDMR size */
static __init int tdmr_size_single(u16 max_reserved_per_tdmr)
{
	int tdmr_sz;

	/*
	 * The actual size of TDMR depends on the maximum
	 * number of reserved areas.
	 */
	tdmr_sz = sizeof(struct tdmr_info);
	tdmr_sz += sizeof(struct tdmr_reserved_area) * max_reserved_per_tdmr;

	return ALIGN(tdmr_sz, TDMR_INFO_ALIGNMENT);
}

static __init int alloc_tdmr_list(struct tdmr_info_list *tdmr_list,
				  struct tdx_sys_info_tdmr *sysinfo_tdmr)
{
	size_t tdmr_sz, tdmr_array_sz;
	void *tdmr_array;

	tdmr_sz = tdmr_size_single(sysinfo_tdmr->max_reserved_per_tdmr);
	tdmr_array_sz = tdmr_sz * sysinfo_tdmr->max_tdmrs;

	/*
	 * To keep things simple, allocate all TDMRs together.
	 * The buffer needs to be physically contiguous to make
	 * sure each TDMR is physically contiguous.
	 */
	tdmr_array = alloc_pages_exact(tdmr_array_sz,
			GFP_KERNEL | __GFP_ZERO);
	if (!tdmr_array)
		return -ENOMEM;

	tdmr_list->tdmrs = tdmr_array;

	/*
	 * Keep the size of TDMR to find the target TDMR
	 * at a given index in the TDMR list.
	 */
	tdmr_list->tdmr_sz = tdmr_sz;
	tdmr_list->max_tdmrs = sysinfo_tdmr->max_tdmrs;
	tdmr_list->nr_consumed_tdmrs = 0;

	return 0;
}

static __init void free_tdmr_list(struct tdmr_info_list *tdmr_list)
{
	free_pages_exact(tdmr_list->tdmrs,
			tdmr_list->max_tdmrs * tdmr_list->tdmr_sz);
}

/* Get the TDMR from the list at the given index. */
static struct tdmr_info *tdmr_entry(struct tdmr_info_list *tdmr_list,
				    int idx)
{
	int tdmr_info_offset = tdmr_list->tdmr_sz * idx;

	return (void *)tdmr_list->tdmrs + tdmr_info_offset;
}

#define TDMR_ALIGNMENT		SZ_1G
#define TDMR_ALIGN_DOWN(_addr)	ALIGN_DOWN((_addr), TDMR_ALIGNMENT)
#define TDMR_ALIGN_UP(_addr)	ALIGN((_addr), TDMR_ALIGNMENT)

static inline u64 tdmr_end(struct tdmr_info *tdmr)
{
	return tdmr->base + tdmr->size;
}

/*
 * Take the memory referenced in @tmb_list and populate the
 * preallocated @tdmr_list, following all the special alignment
 * and size rules for TDMR.
 */
static __init int fill_out_tdmrs(struct list_head *tmb_list,
				 struct tdmr_info_list *tdmr_list)
{
	struct tdx_memblock *tmb;
	int tdmr_idx = 0;

	/*
	 * Loop over TDX memory regions and fill out TDMRs to cover them.
	 * To keep it simple, always try to use one TDMR to cover one
	 * memory region.
	 *
	 * In practice TDX supports at least 64 TDMRs.  A 2-socket system
	 * typically only consumes less than 10 of those.  This code is
	 * dumb and simple and may use more TMDRs than is strictly
	 * required.
	 */
	list_for_each_entry(tmb, tmb_list, list) {
		struct tdmr_info *tdmr = tdmr_entry(tdmr_list, tdmr_idx);
		u64 start, end;

		start = TDMR_ALIGN_DOWN(PFN_PHYS(tmb->start_pfn));
		end   = TDMR_ALIGN_UP(PFN_PHYS(tmb->end_pfn));

		/*
		 * A valid size indicates the current TDMR has already
		 * been filled out to cover the previous memory region(s).
		 */
		if (tdmr->size) {
			/*
			 * Loop to the next if the current memory region
			 * has already been fully covered.
			 */
			if (end <= tdmr_end(tdmr))
				continue;

			/* Otherwise, skip the already covered part. */
			if (start < tdmr_end(tdmr))
				start = tdmr_end(tdmr);

			/*
			 * Create a new TDMR to cover the current memory
			 * region, or the remaining part of it.
			 */
			tdmr_idx++;
			if (tdmr_idx >= tdmr_list->max_tdmrs) {
				pr_warn("initialization failed: TDMRs exhausted.\n");
				return -ENOSPC;
			}

			tdmr = tdmr_entry(tdmr_list, tdmr_idx);
		}

		tdmr->base = start;
		tdmr->size = end - start;
	}

	/* @tdmr_idx is always the index of the last valid TDMR. */
	tdmr_list->nr_consumed_tdmrs = tdmr_idx + 1;

	/*
	 * Warn early that kernel is about to run out of TDMRs.
	 *
	 * This is an indication that TDMR allocation has to be
	 * reworked to be smarter to not run into an issue.
	 */
	if (tdmr_list->max_tdmrs - tdmr_list->nr_consumed_tdmrs < TDMR_NR_WARN)
		pr_warn("consumed TDMRs reaching limit: %d used out of %d\n",
				tdmr_list->nr_consumed_tdmrs,
				tdmr_list->max_tdmrs);

	return 0;
}

static __init unsigned long tdmr_get_pamt_bitmap_sz(struct tdmr_info *tdmr)
{
	unsigned long pamt_sz, nr_pamt_entries;
	int bits_per_entry;

	bits_per_entry = tdx_sysinfo.tdmr.pamt_page_bitmap_entry_bits;
	nr_pamt_entries = tdmr->size >> PAGE_SHIFT;
	pamt_sz = DIV_ROUND_UP(nr_pamt_entries * bits_per_entry, BITS_PER_BYTE);

	return PAGE_ALIGN(pamt_sz);
}

/*
 * Calculate PAMT size given a TDMR and a page size.  The returned
 * PAMT size is always aligned up to 4K page boundary.
 */
static __init unsigned long tdmr_get_pamt_sz(struct tdmr_info *tdmr, int pgsz)
{
	unsigned long pamt_sz, nr_pamt_entries;
	const int tdx_pg_size_shift[TDX_PS_NR] = { PAGE_SHIFT, PMD_SHIFT, PUD_SHIFT };
	const u16 pamt_entry_size[TDX_PS_NR] = {
		tdx_sysinfo.tdmr.pamt_4k_entry_size,
		tdx_sysinfo.tdmr.pamt_2m_entry_size,
		tdx_sysinfo.tdmr.pamt_1g_entry_size,
	};

	nr_pamt_entries = tdmr->size >> tdx_pg_size_shift[pgsz];
	pamt_sz = nr_pamt_entries * pamt_entry_size[pgsz];

	/* TDX requires PAMT size must be 4K aligned */
	return PAGE_ALIGN(pamt_sz);
}

/*
 * Locate a NUMA node which should hold the allocation of the @tdmr
 * PAMT.  This node will have some memory covered by the TDMR.  The
 * relative amount of memory covered is not considered.
 */
static __init int tdmr_get_nid(struct tdmr_info *tdmr, struct list_head *tmb_list)
{
	struct tdx_memblock *tmb;

	/*
	 * A TDMR must cover at least part of one TMB.  That TMB will end
	 * after the TDMR begins.  But, that TMB may have started before
	 * the TDMR.  Find the next 'tmb' that _ends_ after this TDMR
	 * begins.  Ignore 'tmb' start addresses.  They are irrelevant.
	 */
	list_for_each_entry(tmb, tmb_list, list) {
		if (tmb->end_pfn > PHYS_PFN(tdmr->base))
			return tmb->nid;
	}

	/*
	 * Fall back to allocating the TDMR's metadata from node 0 when
	 * no TDX memory block can be found.  This should never happen
	 * since TDMRs originate from TDX memory blocks.
	 */
	pr_warn("TDMR [0x%llx, 0x%llx): unable to find local NUMA node for PAMT allocation, fallback to use node 0.\n",
			tdmr->base, tdmr_end(tdmr));
	return 0;
}

/*
 * Allocate PAMTs from the local NUMA node of some memory in @tmb_list
 * within @tdmr, and set up PAMTs for @tdmr.
 */
static __init int tdmr_set_up_pamt(struct tdmr_info *tdmr,
				   struct list_head *tmb_list)
{
	unsigned long tdmr_pamt_size;
	struct page *pamt;
	int nid;

	nid = tdmr_get_nid(tdmr, tmb_list);

	/*
	 * Calculate the PAMT size for each TDX supported page size
	 * and the total PAMT size.
	 */
	tdmr->pamt_1g_size = tdmr_get_pamt_sz(tdmr, TDX_PS_1G);
	tdmr->pamt_2m_size = tdmr_get_pamt_sz(tdmr, TDX_PS_2M);

	if (tdx_supports_dynamic_pamt(&tdx_sysinfo)) {
		/* With DPAMT, PAMT_4K is replaced with a bitmap */
		tdmr->pamt_4k_size = tdmr_get_pamt_bitmap_sz(tdmr);
	} else {
		tdmr->pamt_4k_size = tdmr_get_pamt_sz(tdmr, TDX_PS_4K);
	}

	tdmr_pamt_size = tdmr->pamt_4k_size + tdmr->pamt_2m_size + tdmr->pamt_1g_size;

	/*
	 * Allocate one chunk of physically contiguous memory for all
	 * PAMTs.  This helps minimize the PAMT's use of reserved areas
	 * in overlapped TDMRs.
	 */
	pamt = alloc_contig_pages(tdmr_pamt_size >> PAGE_SHIFT, GFP_KERNEL,
			nid, &node_online_map);

	/*
	 * tdmr->pamt_4k_base is still zero so the error
	 * path of the caller will skip freeing the PAMT.
	 */
	if (!pamt)
		return -ENOMEM;

	tdmr->pamt_4k_base = page_to_phys(pamt);
	tdmr->pamt_2m_base = tdmr->pamt_4k_base + tdmr->pamt_4k_size;
	tdmr->pamt_1g_base = tdmr->pamt_2m_base + tdmr->pamt_2m_size;

	return 0;
}

static void tdmr_get_pamt(struct tdmr_info *tdmr, unsigned long *pamt_base,
			  unsigned long *pamt_size)
{
	unsigned long pamt_bs, pamt_sz;

	/*
	 * The PAMT was allocated in one contiguous unit.  The 4K PAMT
	 * should always point to the beginning of that allocation.
	 */
	pamt_bs = tdmr->pamt_4k_base;
	pamt_sz = tdmr->pamt_4k_size + tdmr->pamt_2m_size + tdmr->pamt_1g_size;

	WARN_ON_ONCE((pamt_bs & ~PAGE_MASK) || (pamt_sz & ~PAGE_MASK));

	*pamt_base = pamt_bs;
	*pamt_size = pamt_sz;
}

static __init void tdmr_do_pamt_func(struct tdmr_info *tdmr,
		void (*pamt_func)(unsigned long base, unsigned long size))
{
	unsigned long pamt_base, pamt_size;

	tdmr_get_pamt(tdmr, &pamt_base, &pamt_size);

	/* Do nothing if PAMT hasn't been allocated for this TDMR */
	if (!pamt_base)
		return;

	pamt_func(pamt_base, pamt_size);
}

static __init void free_pamt(unsigned long pamt_base, unsigned long pamt_size)
{
	free_contig_range(pamt_base >> PAGE_SHIFT, pamt_size >> PAGE_SHIFT);
}

static __init void tdmr_free_pamt(struct tdmr_info *tdmr)
{
	tdmr_do_pamt_func(tdmr, free_pamt);
}

static __init void tdmrs_free_pamt_all(struct tdmr_info_list *tdmr_list)
{
	int i;

	for (i = 0; i < tdmr_list->nr_consumed_tdmrs; i++)
		tdmr_free_pamt(tdmr_entry(tdmr_list, i));
}

/* Allocate and set up PAMTs for all TDMRs */
static __init int tdmrs_set_up_pamt_all(struct tdmr_info_list *tdmr_list,
					struct list_head *tmb_list)
{
	int i, ret = 0;

	for (i = 0; i < tdmr_list->nr_consumed_tdmrs; i++) {
		ret = tdmr_set_up_pamt(tdmr_entry(tdmr_list, i), tmb_list);
		if (ret)
			goto err;
	}

	return 0;
err:
	tdmrs_free_pamt_all(tdmr_list);
	return ret;
}

/*
 * Convert TDX private pages back to normal by using MOVDIR64B to clear these
 * pages. Typically, any write to the page will convert it from TDX private back
 * to normal kernel memory. Systems with the X86_BUG_TDX_PW_MCE erratum need to
 * do the conversion explicitly via MOVDIR64B.
 */
void tdx_quirk_reset_paddr(unsigned long base, unsigned long size)
{
	const void *zero_page = (const void *)page_address(ZERO_PAGE(0));
	unsigned long phys, end;

	if (!boot_cpu_has_bug(X86_BUG_TDX_PW_MCE))
		return;

	end = base + size;
	for (phys = base; phys < end; phys += 64)
		movdir64b(__va(phys), zero_page);

	/*
	 * MOVDIR64B uses WC protocol.  Use memory barrier to
	 * make sure any later user of these pages sees the
	 * updated data.
	 */
	mb();
}
EXPORT_SYMBOL_FOR_KVM(tdx_quirk_reset_paddr);

static __init void tdmr_quirk_reset_pamt(struct tdmr_info *tdmr)

{
	tdmr_do_pamt_func(tdmr, tdx_quirk_reset_paddr);
}

static __init void tdmrs_quirk_reset_pamt_all(struct tdmr_info_list *tdmr_list)
{
	int i;

	for (i = 0; i < tdmr_list->nr_consumed_tdmrs; i++)
		tdmr_quirk_reset_pamt(tdmr_entry(tdmr_list, i));
}

static __init unsigned long tdmrs_count_pamt_kb(struct tdmr_info_list *tdmr_list)
{
	unsigned long pamt_size = 0;
	int i;

	for (i = 0; i < tdmr_list->nr_consumed_tdmrs; i++) {
		unsigned long base, size;

		tdmr_get_pamt(tdmr_entry(tdmr_list, i), &base, &size);
		pamt_size += size;
	}

	return pamt_size / 1024;
}

static __init int tdmr_add_rsvd_area(struct tdmr_info *tdmr, int *p_idx,
				     u64 addr, u64 size, u16 max_reserved_per_tdmr)
{
	struct tdmr_reserved_area *rsvd_areas = tdmr->reserved_areas;
	int idx = *p_idx;

	/* Reserved area must be 4K aligned in offset and size */
	if (WARN_ON(addr & ~PAGE_MASK || size & ~PAGE_MASK))
		return -EINVAL;

	if (idx >= max_reserved_per_tdmr) {
		pr_warn("initialization failed: TDMR [0x%llx, 0x%llx): reserved areas exhausted.\n",
				tdmr->base, tdmr_end(tdmr));
		return -ENOSPC;
	}

	/*
	 * Consume one reserved area per call.  Make no effort to
	 * optimize or reduce the number of reserved areas which are
	 * consumed by contiguous reserved areas, for instance.
	 */
	rsvd_areas[idx].offset = addr - tdmr->base;
	rsvd_areas[idx].size = size;

	*p_idx = idx + 1;

	return 0;
}

/*
 * Go through @tmb_list to find holes between memory areas.  If any of
 * those holes fall within @tdmr, set up a TDMR reserved area to cover
 * the hole.
 */
static __init int tdmr_populate_rsvd_holes(struct list_head *tmb_list,
					   struct tdmr_info *tdmr,
					   int *rsvd_idx,
					   u16 max_reserved_per_tdmr)
{
	struct tdx_memblock *tmb;
	u64 prev_end;
	int ret;

	/*
	 * Start looking for reserved blocks at the
	 * beginning of the TDMR.
	 */
	prev_end = tdmr->base;
	list_for_each_entry(tmb, tmb_list, list) {
		u64 start, end;

		start = PFN_PHYS(tmb->start_pfn);
		end   = PFN_PHYS(tmb->end_pfn);

		/* Break if this region is after the TDMR */
		if (start >= tdmr_end(tdmr))
			break;

		/* Exclude regions before this TDMR */
		if (end < tdmr->base)
			continue;

		/*
		 * Skip over memory areas that
		 * have already been dealt with.
		 */
		if (start <= prev_end) {
			prev_end = end;
			continue;
		}

		/* Add the hole before this region */
		ret = tdmr_add_rsvd_area(tdmr, rsvd_idx, prev_end,
				start - prev_end,
				max_reserved_per_tdmr);
		if (ret)
			return ret;

		prev_end = end;
	}

	/* Add the hole after the last region if it exists. */
	if (prev_end < tdmr_end(tdmr)) {
		ret = tdmr_add_rsvd_area(tdmr, rsvd_idx, prev_end,
				tdmr_end(tdmr) - prev_end,
				max_reserved_per_tdmr);
		if (ret)
			return ret;
	}

	return 0;
}

/*
 * Go through @tdmr_list to find all PAMTs.  If any of those PAMTs
 * overlaps with @tdmr, set up a TDMR reserved area to cover the
 * overlapping part.
 */
static __init int tdmr_populate_rsvd_pamts(struct tdmr_info_list *tdmr_list,
					   struct tdmr_info *tdmr,
					   int *rsvd_idx,
					   u16 max_reserved_per_tdmr)
{
	int i, ret;

	for (i = 0; i < tdmr_list->nr_consumed_tdmrs; i++) {
		struct tdmr_info *tmp = tdmr_entry(tdmr_list, i);
		unsigned long pamt_base, pamt_size, pamt_end;

		tdmr_get_pamt(tmp, &pamt_base, &pamt_size);
		/* Each TDMR must already have PAMT allocated */
		WARN_ON_ONCE(!pamt_size || !pamt_base);

		pamt_end = pamt_base + pamt_size;
		/* Skip PAMTs outside of the given TDMR */
		if ((pamt_end <= tdmr->base) ||
				(pamt_base >= tdmr_end(tdmr)))
			continue;

		/* Only mark the part within the TDMR as reserved */
		if (pamt_base < tdmr->base)
			pamt_base = tdmr->base;
		if (pamt_end > tdmr_end(tdmr))
			pamt_end = tdmr_end(tdmr);

		ret = tdmr_add_rsvd_area(tdmr, rsvd_idx, pamt_base,
				pamt_end - pamt_base,
				max_reserved_per_tdmr);
		if (ret)
			return ret;
	}

	return 0;
}

/* Compare function called by sort() for TDMR reserved areas */
static __init int rsvd_area_cmp_func(const void *a, const void *b)
{
	struct tdmr_reserved_area *r1 = (struct tdmr_reserved_area *)a;
	struct tdmr_reserved_area *r2 = (struct tdmr_reserved_area *)b;

	if (r1->offset + r1->size <= r2->offset)
		return -1;
	if (r1->offset >= r2->offset + r2->size)
		return 1;

	/* Reserved areas cannot overlap.  The caller must guarantee. */
	WARN_ON_ONCE(1);
	return -1;
}

/*
 * Populate reserved areas for the given @tdmr, including memory holes
 * (via @tmb_list) and PAMTs (via @tdmr_list).
 */
static __init int tdmr_populate_rsvd_areas(struct tdmr_info *tdmr,
					   struct list_head *tmb_list,
					   struct tdmr_info_list *tdmr_list,
					   u16 max_reserved_per_tdmr)
{
	int ret, rsvd_idx = 0;

	ret = tdmr_populate_rsvd_holes(tmb_list, tdmr, &rsvd_idx,
			max_reserved_per_tdmr);
	if (ret)
		return ret;

	ret = tdmr_populate_rsvd_pamts(tdmr_list, tdmr, &rsvd_idx,
			max_reserved_per_tdmr);
	if (ret)
		return ret;

	/* TDX requires reserved areas listed in address ascending order */
	sort(tdmr->reserved_areas, rsvd_idx, sizeof(struct tdmr_reserved_area),
			rsvd_area_cmp_func, NULL);

	return 0;
}

/*
 * Populate reserved areas for all TDMRs in @tdmr_list, including memory
 * holes (via @tmb_list) and PAMTs.
 */
static __init int tdmrs_populate_rsvd_areas_all(struct tdmr_info_list *tdmr_list,
						struct list_head *tmb_list,
						u16 max_reserved_per_tdmr)
{
	int i;

	for (i = 0; i < tdmr_list->nr_consumed_tdmrs; i++) {
		int ret;

		ret = tdmr_populate_rsvd_areas(tdmr_entry(tdmr_list, i),
				tmb_list, tdmr_list, max_reserved_per_tdmr);
		if (ret)
			return ret;
	}

	return 0;
}

/*
 * Construct a list of TDMRs on the preallocated space in @tdmr_list
 * to cover all TDX memory regions in @tmb_list based on the TDX module
 * TDMR global information in @sysinfo_tdmr.
 */
static __init int construct_tdmrs(struct list_head *tmb_list,
				  struct tdmr_info_list *tdmr_list,
				  struct tdx_sys_info_tdmr *sysinfo_tdmr)
{
	int ret;

	ret = fill_out_tdmrs(tmb_list, tdmr_list);
	if (ret)
		return ret;

	ret = tdmrs_set_up_pamt_all(tdmr_list, tmb_list);
	if (ret)
		return ret;

	ret = tdmrs_populate_rsvd_areas_all(tdmr_list, tmb_list,
			sysinfo_tdmr->max_reserved_per_tdmr);
	if (ret)
		tdmrs_free_pamt_all(tdmr_list);

	/*
	 * The tdmr_info_list is read-only from here on out.
	 * Ensure that these writes are seen by other CPUs.
	 * Pairs with a smp_rmb() in is_pamt_page().
	 */
	smp_wmb();

	return ret;
}

#define TDX_SYS_CONFIG_DYNAMIC_PAMT	BIT(16)

static __init int config_tdx_module(struct tdmr_info_list *tdmr_list,
				    u64 global_keyid)
{
	struct tdx_module_args args = {};
	u64 *tdmr_pa_array;
	size_t array_sz;
	int i, ret;

	/*
	 * TDMRs are passed to the TDX module via an array of physical
	 * addresses of each TDMR.  The array itself also has certain
	 * alignment requirement.
	 */
	array_sz = tdmr_list->nr_consumed_tdmrs * sizeof(u64);
	array_sz = roundup_pow_of_two(array_sz);
	if (array_sz < TDMR_INFO_PA_ARRAY_ALIGNMENT)
		array_sz = TDMR_INFO_PA_ARRAY_ALIGNMENT;

	tdmr_pa_array = kzalloc(array_sz, GFP_KERNEL);
	if (!tdmr_pa_array)
		return -ENOMEM;

	for (i = 0; i < tdmr_list->nr_consumed_tdmrs; i++)
		tdmr_pa_array[i] = __pa(tdmr_entry(tdmr_list, i));

	args.rcx = __pa(tdmr_pa_array);
	args.rdx = tdmr_list->nr_consumed_tdmrs;
	args.r8 = global_keyid;

	if (tdx_supports_dynamic_pamt(&tdx_sysinfo)) {
		pr_info("Enable Dynamic PAMT\n");
		args.r8 |= TDX_SYS_CONFIG_DYNAMIC_PAMT;
	}

	ret = seamcall_prerr(TDH_SYS_CONFIG, &args);

	/* Free the array as it is not required anymore. */
	kfree(tdmr_pa_array);

	return ret;
}

static __init int do_global_key_config(void *unused)
{
	struct tdx_module_args args = {};

	return seamcall_prerr(TDH_SYS_KEY_CONFIG, &args);
}

/*
 * Attempt to configure the global KeyID on all physical packages.
 *
 * This requires running code on at least one CPU in each package.
 * TDMR initialization) will fail will fail if any package in the
 * system has no online CPUs.
 *
 * This code takes no affirmative steps to online CPUs.  Callers (aka.
 * KVM) can ensure success by ensuring sufficient CPUs are online and
 * can run SEAMCALLs.
 */
static __init int config_global_keyid(void)
{
	cpumask_var_t packages;
	int cpu, ret = -EINVAL;

	if (!zalloc_cpumask_var(&packages, GFP_KERNEL))
		return -ENOMEM;

	/*
	 * Hardware doesn't guarantee cache coherency across different
	 * KeyIDs.  The kernel needs to flush PAMT's dirty cachelines
	 * (associated with KeyID 0) before the TDX module can use the
	 * global KeyID to access the PAMT.  Given PAMTs are potentially
	 * large (~1/256th of system RAM), just use WBINVD.
	 */
	wbinvd_on_all_cpus();

	for_each_online_cpu(cpu) {
		/*
		 * The key configuration only needs to be done once per
		 * package and will return an error if configured more
		 * than once.  Avoid doing it multiple times per package.
		 */
		if (cpumask_test_and_set_cpu(topology_physical_package_id(cpu),
					packages))
			continue;

		/*
		 * TDH.SYS.KEY.CONFIG cannot run concurrently on
		 * different cpus.  Do it one by one.
		 */
		ret = smp_call_on_cpu(cpu, do_global_key_config, NULL, true);
		if (ret)
			break;
	}

	free_cpumask_var(packages);
	return ret;
}

static __init int init_tdmr(struct tdmr_info *tdmr)
{
	u64 next;

	/*
	 * Initializing a TDMR can be time consuming.  To avoid long
	 * SEAMCALLs, the TDX module may only initialize a part of the
	 * TDMR in each call.
	 */
	do {
		struct tdx_module_args args = {
			.rcx = tdmr->base,
		};
		int ret;

		ret = seamcall_prerr_ret(TDH_SYS_TDMR_INIT, &args);
		if (ret)
			return ret;
		/*
		 * RDX contains 'next-to-initialize' address if
		 * TDH.SYS.TDMR.INIT did not fully complete and
		 * should be retried.
		 */
		next = args.rdx;
		cond_resched();
		/* Keep making SEAMCALLs until the TDMR is done */
	} while (next < tdmr->base + tdmr->size);

	return 0;
}

static __init int init_tdmrs(struct tdmr_info_list *tdmr_list)
{
	int i;

	/*
	 * This operation is costly.  It can be parallelized,
	 * but keep it simple for now.
	 */
	for (i = 0; i < tdmr_list->nr_consumed_tdmrs; i++) {
		int ret;

		ret = init_tdmr(tdmr_entry(tdmr_list, i));
		if (ret)
			return ret;
	}

	return 0;
}

static __init int init_tdx_module(void)
{
	int ret;

	ret = get_tdx_sys_info(&tdx_sysinfo);
	if (ret)
		return ret;

	/* Check whether the kernel can support this module */
	ret = check_features(&tdx_sysinfo);
	if (ret)
		return ret;

	/*
	 * To keep things simple, assume that all TDX-protected memory
	 * will come from the page allocator.  Make sure all pages in the
	 * page allocator are TDX-usable memory.
	 *
	 * Build the list of "TDX-usable" memory regions which cover all
	 * pages in the page allocator to guarantee that.  Do it while
	 * holding mem_hotplug_lock read-lock as the memory hotplug code
	 * path reads the @tdx_memlist to reject any new memory.
	 */
	get_online_mems();

	ret = init_dpamt_refcounts();
	if (ret)
		goto out_put_tdxmem;

	ret = build_tdx_memlist(&tdx_memlist);
	if (ret)
		goto err_free_dpamt_refcounts;

	/* Allocate enough space for constructing TDMRs */
	ret = alloc_tdmr_list(&tdx_tdmr_list, &tdx_sysinfo.tdmr);
	if (ret)
		goto err_free_tdxmem;

	/* Cover all TDX-usable memory regions in TDMRs */
	ret = construct_tdmrs(&tdx_memlist, &tdx_tdmr_list, &tdx_sysinfo.tdmr);
	if (ret)
		goto err_free_tdmrs;

	/* Pass the TDMRs and the global KeyID to the TDX module */
	ret = config_tdx_module(&tdx_tdmr_list, tdx_global_keyid);
	if (ret)
		goto err_free_pamts;

	/* Config the key of global KeyID on all packages */
	ret = config_global_keyid();
	if (ret)
		goto err_reset_pamts;

	/* Initialize TDMRs to complete the TDX module initialization */
	ret = init_tdmrs(&tdx_tdmr_list);
	if (ret)
		goto err_reset_pamts;

	pr_info("%lu KB allocated for PAMT\n", tdmrs_count_pamt_kb(&tdx_tdmr_list));

out_put_tdxmem:
	/*
	 * @tdx_memlist is written here and read at memory hotplug time.
	 * Lock out memory hotplug code while building it.
	 */
	put_online_mems();
	return ret;

err_reset_pamts:
	/*
	 * Part of PAMTs may already have been initialized by the
	 * TDX module.  Flush cache before returning PAMTs back
	 * to the kernel.
	 */
	wbinvd_on_all_cpus();
	tdmrs_quirk_reset_pamt_all(&tdx_tdmr_list);
err_free_pamts:
	tdmrs_free_pamt_all(&tdx_tdmr_list);
err_free_tdmrs:
	free_tdmr_list(&tdx_tdmr_list);
err_free_tdxmem:
	free_tdx_memlist(&tdx_memlist);
err_free_dpamt_refcounts:
	free_dpamt_refcounts();
	goto out_put_tdxmem;
}

static __init int tdx_enable(void)
{
	enum cpuhp_state state;
	int ret;

	if (!cpu_feature_enabled(X86_FEATURE_TDX_HOST_PLATFORM)) {
		pr_err("TDX not supported by the host platform\n");
		return -ENODEV;
	}

	if (!cpu_feature_enabled(X86_FEATURE_XSAVE)) {
		pr_err("XSAVE is required for TDX\n");
		return -EINVAL;
	}

	if (!cpu_feature_enabled(X86_FEATURE_MOVDIR64B)) {
		pr_err("MOVDIR64B is required for TDX\n");
		return -EINVAL;
	}

	if (!cpu_feature_enabled(X86_FEATURE_SELFSNOOP)) {
		pr_err("Self-snoop is required for TDX\n");
		return -ENODEV;
	}

	state = cpuhp_setup_state(CPUHP_AP_ONLINE_DYN, "virt/tdx:online",
				  tdx_online_cpu, tdx_offline_cpu);
	if (state < 0)
		return state;

	ret = init_tdx_module();
	if (ret) {
		pr_err("TDX-Module initialization failed (%d)\n", ret);
		cpuhp_remove_state(state);
		return ret;
	}

	register_syscore(&tdx_syscore);

	tdx_module_state.initialized = true;
	pr_info("TDX-Module initialized\n");
	return 0;
}
subsys_initcall(tdx_enable);

int tdx_module_shutdown(void)
{
	struct tdx_sys_info_handoff handoff = {};
	struct tdx_module_args args = {};
	int ret;
	int cpu;

	ret = get_tdx_sys_info_handoff(&handoff);
	/*
	 * Handoff information is required for proper
	 * shutdown. Refuse to shut down without it.
	 */
	if (ret)
		return ret;

	/*
	 * Use the module's handoff version as it is the highest the
	 * module can produce and most likely supported by newer modules.
	 */
	args.rcx = handoff.module_hv;

	ret = seamcall_prerr(TDH_SYS_SHUTDOWN, &args);
	if (ret)
		return ret;

	/*
	 * Clear global and per-CPU initialization flags so the new module
	 * can be fully re-initialized after a successful update.
	 *
	 * No locks needed as no concurrent accesses can occur here.
	 */
	memset(&tdx_module_state, 0, sizeof(tdx_module_state));
	for_each_possible_cpu(cpu)
		per_cpu(tdx_lp_initialized, cpu) = false;

	return 0;
}

int tdx_module_run_update(void)
{
	struct tdx_module_args args = {};
	int ret;

	ret = seamcall_prerr(TDH_SYS_UPDATE, &args);
	if (ret)
		return ret;

	ret = get_tdx_sys_info_version(&tdx_sysinfo.version);
	/*
	 * Only fails if there is something unexpected
	 * and severely wrong with the module.
	 */
	WARN_ON_ONCE(ret);

	tdx_module_state.initialized = true;
	return 0;
}

static bool is_pamt_page(unsigned long phys)
{
	struct tdmr_info_list *tdmr_list = &tdx_tdmr_list;
	int i;

	/* Ensure that all remote 'tdmr_list' writes are visible: */
	smp_rmb();

	/*
	 * The TDX module is no longer returning TDX_SYS_NOT_READY and
	 * is initialized.  The 'tdmr_list' was initialized long ago
	 * and is now read-only.
	 */
	for (i = 0; i < tdmr_list->nr_consumed_tdmrs; i++) {
		unsigned long base, size;

		tdmr_get_pamt(tdmr_entry(tdmr_list, i), &base, &size);

		if (phys >= base && phys < (base + size))
			return true;
	}

	return false;
}

/*
 * Return whether the memory page at the given physical address is TDX
 * private memory or not.
 *
 * This can be imprecise for two known reasons:
 * 1. PAMTs are private memory and exist before the TDX module is
 *    ready and TDH_PHYMEM_PAGE_RDMD works.  This is a relatively
 *    short window that occurs once per boot.
 * 2. TDH_PHYMEM_PAGE_RDMD reflects the TDX module's knowledge of the
 *    page.  However, the page can still cause #MC until it has been
 *    fully converted to shared using 64-byte writes like MOVDIR64B.
 *    Buggy hosts might still leave #MC-causing memory in place which
 *    this function can not detect.
 */
static bool paddr_is_tdx_private(unsigned long phys)
{
	struct tdx_module_args args = {
		.rcx = phys & PAGE_MASK,
	};
	u64 sret;

	if (!boot_cpu_has(X86_FEATURE_TDX_HOST_PLATFORM))
		return false;

	/* Get page type from the TDX module */
	sret = __seamcall_dirty_cache(__seamcall_ret, TDH_PHYMEM_PAGE_RDMD, &args);

	/*
	 * The SEAMCALL will not return success unless there is a
	 * working, "ready" TDX module.  Assume an absence of TDX
	 * private pages until SEAMCALL is working.
	 */
	if (sret)
		return false;

	/*
	 * SEAMCALL was successful -- read page type (via RCX):
	 *
	 *  - PT_NDA:	Page is not used by the TDX module
	 *  - PT_RSVD:	Reserved for Non-TDX use
	 *  - Others:	Page is used by the TDX module
	 *
	 * Note PAMT pages are marked as PT_RSVD but they are also TDX
	 * private memory.
	 */
	switch (args.rcx) {
	case PT_NDA:
		return false;
	case PT_RSVD:
		return is_pamt_page(phys);
	default:
		return true;
	}
}

/*
 * Some TDX-capable CPUs have an erratum.  A write to TDX private
 * memory poisons that memory, and a subsequent read of that memory
 * triggers #MC.
 *
 * Help distinguish erratum-triggered #MCs from a normal hardware one.
 * Just print additional message to show such #MC may be result of the
 * erratum.
 */
const char *tdx_dump_mce_info(struct mce *m)
{
	if (!m || !mce_is_memory_error(m) || !mce_usable_address(m))
		return NULL;

	if (!paddr_is_tdx_private(m->addr))
		return NULL;

	return "TDX private memory error. Possible kernel bug.";
}

static __init int record_keyid_partitioning(u32 *tdx_keyid_start,
					    u32 *nr_tdx_keyids)
{
	u32 _nr_mktme_keyids, _tdx_keyid_start, _nr_tdx_keyids;
	struct msr val;
	int ret;

	/*
	 * IA32_MKTME_KEYID_PARTIONING:
	 *   Bit [31:0]:	Number of MKTME KeyIDs.
	 *   Bit [63:32]:	Number of TDX private KeyIDs.
	 */
	ret = rdmsrq_safe(MSR_IA32_MKTME_KEYID_PARTITIONING, &val.q);
	_nr_mktme_keyids = val.l;
	_nr_tdx_keyids = val.h;
	if (ret || !_nr_tdx_keyids)
		return -EINVAL;

	/* TDX KeyIDs start after the last MKTME KeyID. */
	_tdx_keyid_start = _nr_mktme_keyids + 1;

	*tdx_keyid_start = _tdx_keyid_start;
	*nr_tdx_keyids = _nr_tdx_keyids;

	return 0;
}

static bool is_tdx_memory(unsigned long start_pfn, unsigned long end_pfn)
{
	struct tdx_memblock *tmb;

	/*
	 * This check assumes that the start_pfn<->end_pfn range does not
	 * cross multiple @tdx_memlist entries.  A single memory online
	 * event across multiple memblocks (from which @tdx_memlist
	 * entries are derived at the time of module initialization) is
	 * not possible.  This is because memory offline/online is done
	 * on granularity of 'struct memory_block', and the hotpluggable
	 * memory region (one memblock) must be multiple of memory_block.
	 */
	list_for_each_entry(tmb, &tdx_memlist, list) {
		if (start_pfn >= tmb->start_pfn && end_pfn <= tmb->end_pfn)
			return true;
	}
	return false;
}

static int tdx_memory_notifier(struct notifier_block *nb, unsigned long action,
			       void *v)
{
	struct memory_notify *mn = v;

	if (action != MEM_GOING_ONLINE)
		return NOTIFY_OK;

	/*
	 * Empty list means TDX isn't enabled.  Allow any memory
	 * to go online.
	 */
	if (list_empty(&tdx_memlist))
		return NOTIFY_OK;

	/*
	 * The TDX memory configuration is static and can not be
	 * changed.  Reject onlining any memory which is outside of
	 * the static configuration whether it supports TDX or not.
	 */
	if (is_tdx_memory(mn->start_pfn, mn->start_pfn + mn->nr_pages))
		return NOTIFY_OK;

	return NOTIFY_BAD;
}

static struct notifier_block tdx_memory_nb = {
	.notifier_call = tdx_memory_notifier,
};

static void __init check_tdx_erratum(void)
{
	u64 basic_msr;

	/*
	 * These CPUs have an erratum.  A partial write from non-TD
	 * software (e.g. via MOVNTI variants or UC/WC mapping) to TDX
	 * private memory poisons that memory, and a subsequent read of
	 * that memory triggers #MC.
	 */
	switch (boot_cpu_data.x86_vfm) {
	case INTEL_SAPPHIRERAPIDS_X:
	case INTEL_EMERALDRAPIDS_X:
		setup_force_cpu_bug(X86_BUG_TDX_PW_MCE);
	}

	/*
	 * Some TDX-capable CPUs have an erratum where the current VMCS is
	 * cleared after calling into P-SEAMLDR.
	 */
	rdmsrq(MSR_IA32_VMX_BASIC, basic_msr);
	if (!(basic_msr & VMX_BASIC_NO_SEAMRET_INVD_VMCS))
		setup_force_cpu_bug(X86_BUG_SEAMRET_INVD_VMCS);
}

void __init tdx_init(void)
{
	u32 tdx_keyid_start, nr_tdx_keyids;
	int err;

	err = record_keyid_partitioning(&tdx_keyid_start, &nr_tdx_keyids);
	if (err)
		return;

	pr_info("BIOS enabled: private KeyID range [%u, %u)\n",
			tdx_keyid_start, tdx_keyid_start + nr_tdx_keyids);

	/*
	 * The TDX module itself requires one 'global KeyID' to protect
	 * its metadata.  If there's only one TDX KeyID, there won't be
	 * any left for TDX guests thus there's no point to enable TDX
	 * at all.
	 */
	if (nr_tdx_keyids < 2) {
		pr_err("initialization failed: too few private KeyIDs available.\n");
		return;
	}

	/*
	 * At this point, hibernation_available() indicates whether or
	 * not hibernation support has been permanently disabled.
	 */
	if (hibernation_available()) {
		pr_err("initialization failed: Hibernation support is enabled\n");
		return;
	}

	err = register_memory_notifier(&tdx_memory_nb);
	if (err) {
		pr_err("initialization failed: register_memory_notifier() failed (%d)\n",
				err);
		return;
	}

#if defined(CONFIG_ACPI) && defined(CONFIG_SUSPEND)
	pr_info("Disable ACPI S3. Turn off TDX in the BIOS to use ACPI S3.\n");
	acpi_suspend_lowlevel = NULL;
#endif

	/*
	 * Just use the first TDX KeyID as the 'global KeyID' and
	 * leave the rest for TDX guests.
	 */
	tdx_global_keyid = tdx_keyid_start;
	tdx_guest_keyid_start = tdx_keyid_start + 1;
	tdx_nr_guest_keyids = nr_tdx_keyids - 1;

	setup_force_cpu_cap(X86_FEATURE_TDX_HOST_PLATFORM);

	check_tdx_erratum();
}

const struct tdx_sys_info *tdx_get_sysinfo(void)
{
	if (!tdx_module_state.initialized)
		return NULL;

	return (const struct tdx_sys_info *)&tdx_sysinfo;
}
EXPORT_SYMBOL_FOR_MODULES(tdx_get_sysinfo, "kvm-intel,tdx-host");

u32 tdx_get_nr_guest_keyids(void)
{
	return tdx_nr_guest_keyids;
}
EXPORT_SYMBOL_FOR_KVM(tdx_get_nr_guest_keyids);

int tdx_guest_keyid_alloc(void)
{
	return ida_alloc_range(&tdx_guest_keyid_pool, tdx_guest_keyid_start,
			       tdx_guest_keyid_start + tdx_nr_guest_keyids - 1,
			       GFP_KERNEL);
}
EXPORT_SYMBOL_FOR_KVM(tdx_guest_keyid_alloc);

void tdx_guest_keyid_free(unsigned int keyid)
{
	ida_free(&tdx_guest_keyid_pool, keyid);
}
EXPORT_SYMBOL_FOR_KVM(tdx_guest_keyid_free);

static inline u64 tdx_tdr_pa(struct tdx_td *td)
{
	return page_to_phys(td->tdr_page);
}

/*
 * The TDX module exposes a CLFLUSH_BEFORE_ALLOC bit to specify whether
 * a CLFLUSH of pages is required before handing them to the TDX module.
 * Be conservative and make the code simpler by doing the CLFLUSH
 * unconditionally.
 */
static void tdx_clflush_page(struct page *page)
{
	clflush_cache_range(page_to_virt(page), PAGE_SIZE);
}

static void tdx_clflush_pfn(kvm_pfn_t pfn)
{
	clflush_cache_range(__va(PFN_PHYS(pfn)), PAGE_SIZE);
}

static int pg_level_to_tdx_sept_level(enum pg_level level)
{
	WARN_ON_ONCE(level == PG_LEVEL_NONE);
	return level - 1;
}

noinstr u64 tdh_vp_enter(struct tdx_vp *td, struct tdx_module_args *args)
{
	args->rcx = td->tdvpr_pa;

	return __seamcall_dirty_cache(__seamcall_saved_ret, TDH_VP_ENTER, args);
}
EXPORT_SYMBOL_FOR_KVM(tdh_vp_enter);

u64 tdh_mng_addcx(struct tdx_td *td, struct page *tdcs_page)
{
	struct tdx_module_args args = {
		.rcx = page_to_phys(tdcs_page),
		.rdx = tdx_tdr_pa(td),
	};

	tdx_clflush_page(tdcs_page);
	return seamcall(TDH_MNG_ADDCX, &args);
}
EXPORT_SYMBOL_FOR_KVM(tdh_mng_addcx);

u64 tdh_mem_page_add(struct tdx_td *td, u64 gpa, kvm_pfn_t pfn, struct page *source,
		     u64 *ext_err1, u64 *ext_err2)
{
	struct tdx_module_args args = {
		.rcx = gpa,
		.rdx = tdx_tdr_pa(td),
		.r8 = PFN_PHYS(pfn),
		.r9 = page_to_phys(source),
	};
	u64 ret;

	tdx_clflush_pfn(pfn);
	ret = seamcall_ret(TDH_MEM_PAGE_ADD, &args);

	*ext_err1 = args.rcx;
	*ext_err2 = args.rdx;

	return ret;
}
EXPORT_SYMBOL_FOR_KVM(tdh_mem_page_add);

u64 tdh_mem_sept_add(struct tdx_td *td, u64 gpa, enum pg_level level,
		     struct page *page, u64 *ext_err1, u64 *ext_err2)
{
	struct tdx_module_args args = {
		.rcx = gpa | pg_level_to_tdx_sept_level(level),
		.rdx = tdx_tdr_pa(td),
		.r8 = page_to_phys(page),
	};
	u64 ret;

	tdx_clflush_page(page);
	ret = seamcall_ret(TDH_MEM_SEPT_ADD, &args);

	*ext_err1 = args.rcx;
	*ext_err2 = args.rdx;

	return ret;
}
EXPORT_SYMBOL_FOR_KVM(tdh_mem_sept_add);

u64 tdh_vp_addcx(struct tdx_vp *vp, struct page *tdcx_page)
{
	struct tdx_module_args args = {
		.rcx = page_to_phys(tdcx_page),
		.rdx = vp->tdvpr_pa,
	};

	tdx_clflush_page(tdcx_page);
	return seamcall(TDH_VP_ADDCX, &args);
}
EXPORT_SYMBOL_FOR_KVM(tdh_vp_addcx);

u64 tdh_mem_page_aug(struct tdx_td *td, u64 gpa, enum pg_level level,
		     kvm_pfn_t pfn, u64 *ext_err1, u64 *ext_err2)
{
	struct tdx_module_args args = {
		.rcx = gpa | pg_level_to_tdx_sept_level(level),
		.rdx = tdx_tdr_pa(td),
		.r8 = PFN_PHYS(pfn),
	};
	u64 ret;

	tdx_clflush_pfn(pfn);
	ret = seamcall_ret(TDH_MEM_PAGE_AUG, &args);

	*ext_err1 = args.rcx;
	*ext_err2 = args.rdx;

	return ret;
}
EXPORT_SYMBOL_FOR_KVM(tdh_mem_page_aug);

u64 tdh_mem_range_block(struct tdx_td *td, u64 gpa, enum pg_level level,
			u64 *ext_err1, u64 *ext_err2)
{
	struct tdx_module_args args = {
		.rcx = gpa | pg_level_to_tdx_sept_level(level),
		.rdx = tdx_tdr_pa(td),
	};
	u64 ret;

	ret = seamcall_ret(TDH_MEM_RANGE_BLOCK, &args);

	*ext_err1 = args.rcx;
	*ext_err2 = args.rdx;

	return ret;
}
EXPORT_SYMBOL_FOR_KVM(tdh_mem_range_block);

u64 tdh_mng_key_config(struct tdx_td *td)
{
	struct tdx_module_args args = {
		.rcx = tdx_tdr_pa(td),
	};

	return seamcall(TDH_MNG_KEY_CONFIG, &args);
}
EXPORT_SYMBOL_FOR_KVM(tdh_mng_key_config);

u64 tdh_mng_create(struct tdx_td *td, u16 hkid)
{
	struct tdx_module_args args = {
		.rcx = tdx_tdr_pa(td),
		.rdx = hkid,
	};

	tdx_clflush_page(td->tdr_page);
	return seamcall(TDH_MNG_CREATE, &args);
}
EXPORT_SYMBOL_FOR_KVM(tdh_mng_create);

u64 tdh_vp_create(struct tdx_td *td, struct tdx_vp *vp)
{
	struct tdx_module_args args = {
		.rcx = vp->tdvpr_pa,
		.rdx = tdx_tdr_pa(td),
	};

	tdx_clflush_page(vp->tdvpr_page);
	return seamcall(TDH_VP_CREATE, &args);
}
EXPORT_SYMBOL_FOR_KVM(tdh_vp_create);

u64 tdh_mng_rd(struct tdx_td *td, u64 field, u64 *data)
{
	struct tdx_module_args args = {
		.rcx = tdx_tdr_pa(td),
		.rdx = field,
	};
	u64 ret;

	ret = seamcall_ret(TDH_MNG_RD, &args);

	/* R8: Content of the field, or 0 in case of error. */
	*data = args.r8;

	return ret;
}
EXPORT_SYMBOL_FOR_KVM(tdh_mng_rd);

u64 tdh_mr_extend(struct tdx_td *td, u64 gpa, u64 *ext_err1, u64 *ext_err2)
{
	struct tdx_module_args args = {
		.rcx = gpa,
		.rdx = tdx_tdr_pa(td),
	};
	u64 ret;

	ret = seamcall_ret(TDH_MR_EXTEND, &args);

	*ext_err1 = args.rcx;
	*ext_err2 = args.rdx;

	return ret;
}
EXPORT_SYMBOL_FOR_KVM(tdh_mr_extend);

u64 tdh_mr_finalize(struct tdx_td *td)
{
	struct tdx_module_args args = {
		.rcx = tdx_tdr_pa(td),
	};

	return seamcall(TDH_MR_FINALIZE, &args);
}
EXPORT_SYMBOL_FOR_KVM(tdh_mr_finalize);

u64 tdh_vp_flush(struct tdx_vp *vp)
{
	struct tdx_module_args args = {
		.rcx = vp->tdvpr_pa,
	};

	return seamcall(TDH_VP_FLUSH, &args);
}
EXPORT_SYMBOL_FOR_KVM(tdh_vp_flush);

u64 tdh_mng_vpflushdone(struct tdx_td *td)
{
	struct tdx_module_args args = {
		.rcx = tdx_tdr_pa(td),
	};

	return seamcall(TDH_MNG_VPFLUSHDONE, &args);
}
EXPORT_SYMBOL_FOR_KVM(tdh_mng_vpflushdone);

u64 tdh_mng_key_freeid(struct tdx_td *td)
{
	struct tdx_module_args args = {
		.rcx = tdx_tdr_pa(td),
	};

	return seamcall(TDH_MNG_KEY_FREEID, &args);
}
EXPORT_SYMBOL_FOR_KVM(tdh_mng_key_freeid);

u64 tdh_mng_init(struct tdx_td *td, u64 td_params, u64 *extended_err)
{
	struct tdx_module_args args = {
		.rcx = tdx_tdr_pa(td),
		.rdx = td_params,
	};
	u64 ret;

	ret = seamcall_ret(TDH_MNG_INIT, &args);

	*extended_err = args.rcx;

	return ret;
}
EXPORT_SYMBOL_FOR_KVM(tdh_mng_init);

u64 tdh_vp_rd(struct tdx_vp *vp, u64 field, u64 *data)
{
	struct tdx_module_args args = {
		.rcx = vp->tdvpr_pa,
		.rdx = field,
	};
	u64 ret;

	ret = seamcall_ret(TDH_VP_RD, &args);

	/* R8: Content of the field, or 0 in case of error. */
	*data = args.r8;

	return ret;
}
EXPORT_SYMBOL_FOR_KVM(tdh_vp_rd);

u64 tdh_vp_wr(struct tdx_vp *vp, u64 field, u64 data, u64 mask)
{
	struct tdx_module_args args = {
		.rcx = vp->tdvpr_pa,
		.rdx = field,
		.r8 = data,
		.r9 = mask,
	};

	return seamcall(TDH_VP_WR, &args);
}
EXPORT_SYMBOL_FOR_KVM(tdh_vp_wr);

u64 tdh_vp_init(struct tdx_vp *vp, u64 initial_rcx, u32 x2apicid)
{
	struct tdx_module_args args = {
		.rcx = vp->tdvpr_pa,
		.rdx = initial_rcx,
		.r8 = x2apicid,
		/* apicid requires version == 1. */
		.version = 1,
	};

	return seamcall(TDH_VP_INIT, &args);
}
EXPORT_SYMBOL_FOR_KVM(tdh_vp_init);

/*
 * TDX ABI defines output operands as PT, OWNER and SIZE. These are TDX defined fomats.
 * So despite the names, they must be interpted specially as described by the spec. Return
 * them only for error reporting purposes.
 */
u64 tdh_phymem_page_reclaim(struct page *page, u64 *tdx_pt, u64 *tdx_owner, u64 *tdx_size)
{
	struct tdx_module_args args = {
		.rcx = page_to_phys(page),
	};
	u64 ret;

	ret = seamcall_ret(TDH_PHYMEM_PAGE_RECLAIM, &args);

	*tdx_pt = args.rcx;
	*tdx_owner = args.rdx;
	*tdx_size = args.r8;

	return ret;
}
EXPORT_SYMBOL_FOR_KVM(tdh_phymem_page_reclaim);

u64 tdh_mem_track(struct tdx_td *td)
{
	struct tdx_module_args args = {
		.rcx = tdx_tdr_pa(td),
	};

	return seamcall(TDH_MEM_TRACK, &args);
}
EXPORT_SYMBOL_FOR_KVM(tdh_mem_track);

u64 tdh_mem_page_remove(struct tdx_td *td, u64 gpa, enum pg_level level,
			u64 *ext_err1, u64 *ext_err2)
{
	struct tdx_module_args args = {
		.rcx = gpa | pg_level_to_tdx_sept_level(level),
		.rdx = tdx_tdr_pa(td),
	};
	u64 ret;

	ret = seamcall_ret(TDH_MEM_PAGE_REMOVE, &args);

	*ext_err1 = args.rcx;
	*ext_err2 = args.rdx;

	return ret;
}
EXPORT_SYMBOL_FOR_KVM(tdh_mem_page_remove);

u64 tdh_phymem_cache_wb(bool resume)
{
	struct tdx_module_args args = {
		.rcx = resume ? 1 : 0,
	};

	return seamcall(TDH_PHYMEM_CACHE_WB, &args);
}
EXPORT_SYMBOL_FOR_KVM(tdh_phymem_cache_wb);

static inline u64 mk_keyed_paddr(u16 hkid, kvm_pfn_t pfn)
{
	/* KeyID bits are just above the physical address bits. */
	return PFN_PHYS(pfn) | ((u64)hkid << boot_cpu_data.x86_phys_bits);
}

u64 tdh_phymem_page_wbinvd_tdr(struct tdx_td *td)
{
	struct tdx_module_args args = {};

	args.rcx = mk_keyed_paddr(tdx_global_keyid, page_to_pfn(td->tdr_page));

	return seamcall(TDH_PHYMEM_PAGE_WBINVD, &args);
}
EXPORT_SYMBOL_FOR_KVM(tdh_phymem_page_wbinvd_tdr);

u64 tdh_phymem_page_wbinvd_hkid(u64 hkid, kvm_pfn_t pfn)
{
	struct tdx_module_args args = {};

	args.rcx = mk_keyed_paddr(hkid, pfn);

	return seamcall(TDH_PHYMEM_PAGE_WBINVD, &args);
}
EXPORT_SYMBOL_FOR_KVM(tdh_phymem_page_wbinvd_hkid);

bool tdx_supports_dynamic_pamt(const struct tdx_sys_info *sysinfo)
{
	return sysinfo->features.tdx_features0 & TDX_FEATURES0_DYNAMIC_PAMT;
}
EXPORT_SYMBOL_FOR_KVM(tdx_supports_dynamic_pamt);

static struct page *tdx_alloc_page_pamt_cache(struct tdx_pamt_cache *cache)
{
	struct page *page;

	page = list_first_entry_or_null(&cache->page_list, struct page, lru);
	if (page) {
		list_del(&page->lru);
		cache->cnt--;
	}

	return page;
}

static struct page *alloc_dpamt_page(struct tdx_pamt_cache *cache)
{
	if (cache)
		return tdx_alloc_page_pamt_cache(cache);

	return alloc_page(GFP_KERNEL_ACCOUNT);
}

static int alloc_pamt_array(struct page **pamt_pages, struct tdx_pamt_cache *cache)
{
	int i, j;

	for (i = 0; i < TDX_DPAMT_ENTRY_PAGE_CNT; i++) {
		pamt_pages[i] = alloc_dpamt_page(cache);
		if (!pamt_pages[i])
			goto err;
	}

	return 0;

err:
	for (j = 0; j < i; j++)
		__free_page(pamt_pages[j]);

	return -ENOMEM;
}

static void free_pamt_array(struct page **pamt_pages)
{
	int i;

	for (i = 0; i < TDX_DPAMT_ENTRY_PAGE_CNT; i++) {
		/*
		 * Reset pages unconditionally to cover cases
		 * where they were passed to the TDX module.
		 */
		tdx_quirk_reset_paddr(page_to_phys(pamt_pages[i]), PAGE_SIZE);

		__free_page(pamt_pages[i]);
	}
}

/* Helper for building DPAMT seamcall() arguments. */
static u64 pamt_2mb_arg(kvm_pfn_t pfn)
{
	/* Find the 2MB-wide DPAMT region for 'pfn': */
	unsigned long hpa_2mb = ALIGN_DOWN(pfn << PAGE_SHIFT, PMD_SIZE);

	/*
	 * TDX ABI requires specifying the page level the installed DPAMT
	 * backing will cover, even though today only 2MB is supported.
	 */
	return hpa_2mb | TDX_PS_2M;
}

/* Add DPAMT backing for the 2MB region surrounding the given pfn. */
static u64 tdh_phymem_pamt_add(kvm_pfn_t pfn, struct page **pamt_pages)
{
	struct tdx_module_args args = {
		.rcx = pamt_2mb_arg(pfn),
		.rdx = page_to_phys(pamt_pages[0]),
		.r8  = page_to_phys(pamt_pages[1]),
	};

	return seamcall(TDH_PHYMEM_PAMT_ADD, &args);
}

/* Remove DPAMT backing for the 2MB region surrounding the given pfn. */
static u64 tdh_phymem_pamt_remove(kvm_pfn_t pfn, struct page **pamt_pages)
{
	struct tdx_module_args args = {
		.rcx = pamt_2mb_arg(pfn),
	};
	u64 ret;

	ret = seamcall_ret(TDH_PHYMEM_PAMT_REMOVE, &args);
	if (ret)
		return ret;

	/* Copy PAMT pages out of the struct per the TDX ABI */
	pamt_pages[0] = phys_to_page(args.rdx);
	pamt_pages[1] = phys_to_page(args.r8);

	return 0;
}

/* Serializes adding/removing DPAMT memory */
static DEFINE_SPINLOCK(dpamt_lock);

/* Bump DPAMT refcount for the given pfn and allocate DPAMT backing if needed. */
int tdx_pamt_get(kvm_pfn_t pfn, struct tdx_pamt_cache *cache)
{
	struct page *pamt_pages[TDX_DPAMT_ENTRY_PAGE_CNT];
	atomic_t *dpamt_refcount;
	u64 tdx_status;
	int ret;

	if (!tdx_supports_dynamic_pamt(&tdx_sysinfo))
		return 0;

	ret = alloc_pamt_array(pamt_pages, cache);
	if (ret)
		return ret;

	dpamt_refcount = tdx_find_dpamt_refcount(pfn);

	spin_lock(&dpamt_lock);

	/*
	 * If the DPAMT entry is already added (i.e. refcount >= 1),
	 * then just increment the refcount.
	 */
	if (atomic_inc_not_zero(dpamt_refcount))
		goto out_free;

	/* Try to add the PAMT page and take the refcount 0->1. */
	tdx_status = tdh_phymem_pamt_add(pfn, pamt_pages);
	if (WARN_ON_ONCE(tdx_status != TDX_SUCCESS)) {
		ret = -EIO;
		goto out_free;
	}

	atomic_set(dpamt_refcount, 1);
	spin_unlock(&dpamt_lock);
	return 0;

out_free:
	spin_unlock(&dpamt_lock);
	free_pamt_array(pamt_pages);

	return ret;
}
EXPORT_SYMBOL_FOR_KVM(tdx_pamt_get);

/* Drop DPAMT refcount for the given pfn and free DPAMT backing if needed. */
void tdx_pamt_put(kvm_pfn_t pfn)
{
	struct page *pamt_pages[TDX_DPAMT_ENTRY_PAGE_CNT] = {};
	atomic_t *dpamt_refcount;
	u64 tdx_status;

	if (!tdx_supports_dynamic_pamt(&tdx_sysinfo))
		return;

	dpamt_refcount = tdx_find_dpamt_refcount(pfn);

	spin_lock(&dpamt_lock);
	/*
	 * If there is more than 1 reference on the DPAMT entry, don't
	 * remove it yet. Just decrement the refcount.
	 */
	if (atomic_read(dpamt_refcount) > 1) {
		atomic_dec(dpamt_refcount);
		goto out_unlock;
	}

	/* Try to remove the pamt page and take the refcount 1->0. */
	tdx_status = tdh_phymem_pamt_remove(pfn, pamt_pages);

	/*
	 * Don't free pamt_pages as it could hold garbage when
	 * tdh_phymem_pamt_remove() fails.  Don't panic/BUG_ON(), as
	 * there is no risk of data corruption, but do yell loudly as
	 * failure indicates a kernel bug, memory is being leaked, and
	 * the dangling DPAMT entry may cause future operations to fail.
	 */
	if (WARN_ON_ONCE(tdx_status != TDX_SUCCESS))
		goto out_unlock;

	atomic_set(dpamt_refcount, 0);
	spin_unlock(&dpamt_lock);
	free_pamt_array(pamt_pages);
	return;
out_unlock:
	spin_unlock(&dpamt_lock);
}
EXPORT_SYMBOL_FOR_KVM(tdx_pamt_put);

void tdx_free_pamt_cache(struct tdx_pamt_cache *cache)
{
	struct page *page;

	while ((page = tdx_alloc_page_pamt_cache(cache)))
		__free_page(page);
}
EXPORT_SYMBOL_FOR_KVM(tdx_free_pamt_cache);

int tdx_topup_pamt_cache(struct tdx_pamt_cache *cache, unsigned long npages)
{
	if (WARN_ON_ONCE(!tdx_supports_dynamic_pamt(&tdx_sysinfo)))
		return 0;

	npages *= TDX_DPAMT_ENTRY_PAGE_CNT;

	while (cache->cnt < npages) {
		struct page *page = alloc_page(GFP_KERNEL_ACCOUNT);

		if (!page)
			return -ENOMEM;

		list_add(&page->lru, &cache->page_list);
		cache->cnt++;
	}

	return 0;
}
EXPORT_SYMBOL_FOR_KVM(tdx_topup_pamt_cache);

/*
 * Return a page that can be gifted to the TDX module for use as a "control"
 * page, i.e. pages that are used for control structures for a given TDX
 * guest, and thus obtain TDX protections, including DPAMT tracking.
 */
struct page *tdx_alloc_control_page(void)
{
	struct page *page;

	page = alloc_page(GFP_KERNEL_ACCOUNT);
	if (!page)
		return NULL;

	if (tdx_pamt_get(page_to_pfn(page), NULL)) {
		__free_page(page);
		return NULL;
	}

	return page;
}
EXPORT_SYMBOL_FOR_KVM(tdx_alloc_control_page);

/*
 * Free a page that was gifted to the TDX module for use as a control
 * page. After this, the page is no longer protected by TDX.
 */
void tdx_free_control_page(struct page *page)
{
	if (!page)
		return;

	tdx_pamt_put(page_to_pfn(page));
	__free_page(page);
}
EXPORT_SYMBOL_FOR_KVM(tdx_free_control_page);

void tdx_sys_disable(void)
{
	struct tdx_module_args args = {};
	u64 ret;

	/*
	 * Don't loop forever.
	 *
	 *  - TDX_INTERRUPTED_RESUMABLE guarantees forward progress between
	 *    calls.
	 *
	 *  - TDX_SYS_BUSY could be returned due to contention with other
	 *    TDH.SYS.* SEAMCALLs, but will lock out *new* TDH.SYS.* SEAMCALLs,
	 *    so that SYS.DISABLE can eventually make progress.
	 *
	 * This is a 'destructive' SEAMCALL, in that no other SEAMCALL can be
	 * run after this until a full reinitialization is done.
	 */
	do {
		ret = seamcall(TDH_SYS_DISABLE, &args);
	} while (ret == TDX_INTERRUPTED_RESUMABLE || ret == TDX_SYS_BUSY);

	/*
	 * Print SEAMCALL failures, but not SW-defined error codes
	 * (SEAMCALL faulted with #GP/#UD, TDX not supported).
	 */
	if (ret && (ret & TDX_SW_ERROR) != TDX_SW_ERROR)
		pr_err("TDH.SYS.DISABLE failed: 0x%016llx\n", ret);
}

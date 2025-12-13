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
#include <linux/idr.h>
#include <asm/page.h>
#include <asm/special_insns.h>
#include <asm/msr-index.h>
#include <asm/msr.h>
#include <asm/cpufeature.h>
#include <asm/tdx.h>
#include <asm/cpu_device_id.h>
#include <asm/processor.h>
#include <asm/mce.h>
#include "tdx.h"

static u32 tdx_global_keyid __ro_after_init;
static u32 tdx_guest_keyid_start __ro_after_init;
static u32 tdx_nr_guest_keyids __ro_after_init;

static DEFINE_IDA(tdx_guest_keyid_pool);

static DEFINE_PER_CPU(bool, tdx_lp_initialized);

static struct tdmr_info_list tdx_tdmr_list;

static enum tdx_module_status_t tdx_module_status;
static DEFINE_MUTEX(tdx_module_lock);

/* All TDX-usable memory regions.  Protected by mem_hotplug_lock. */
static LIST_HEAD(tdx_memlist);

static struct tdx_sys_info tdx_sysinfo;

typedef void (*sc_err_func_t)(u64 fn, u64 err, struct tdx_module_args *args);

static inline void seamcall_err(u64 fn, u64 err, struct tdx_module_args *args)
{
	pr_err("SEAMCALL (0x%016llx) failed: 0x%016llx\n", fn, err);
}

static inline void seamcall_err_ret(u64 fn, u64 err,
				    struct tdx_module_args *args)
{
	seamcall_err(fn, err, args);
	pr_err("RCX 0x%016llx RDX 0x%016llx R08 0x%016llx\n",
			args->rcx, args->rdx, args->r8);
	pr_err("R09 0x%016llx R10 0x%016llx R11 0x%016llx\n",
			args->r9, args->r10, args->r11);
}

static __always_inline int sc_retry_prerr(sc_func_t func,
					  sc_err_func_t err_func,
					  u64 fn, struct tdx_module_args *args)
{
	u64 sret = sc_retry(func, fn, args);

	if (sret == TDX_SUCCESS)
		return 0;

	if (sret == TDX_SEAMCALL_VMFAILINVALID)
		return -ENODEV;

	if (sret == TDX_SEAMCALL_GP)
		return -EOPNOTSUPP;

	if (sret == TDX_SEAMCALL_UD)
		return -EACCES;

	err_func(fn, sret, args);
	return -EIO;
}

#define seamcall_prerr(__fn, __args)						\
	sc_retry_prerr(__seamcall, seamcall_err, (__fn), (__args))

#define seamcall_prerr_ret(__fn, __args)					\
	sc_retry_prerr(__seamcall_ret, seamcall_err_ret, (__fn), (__args))

/*
 * Do the module global initialization once and return its result.
 * It can be done on any cpu.  It's always called with interrupts
 * disabled.
 */
static int try_init_module_global(void)
{
	struct tdx_module_args args = {};
	static DEFINE_RAW_SPINLOCK(sysinit_lock);
	static bool sysinit_done;
	static int sysinit_ret;

	lockdep_assert_irqs_disabled();

	raw_spin_lock(&sysinit_lock);

	if (sysinit_done)
		goto out;

	/* RCX is module attributes and all bits are reserved */
	args.rcx = 0;
	sysinit_ret = seamcall_prerr(TDH_SYS_INIT, &args);

	/*
	 * The first SEAMCALL also detects the TDX module, thus
	 * it can fail due to the TDX module is not loaded.
	 * Dump message to let the user know.
	 */
	if (sysinit_ret == -ENODEV)
		pr_err("module not loaded\n");

	sysinit_done = true;
out:
	raw_spin_unlock(&sysinit_lock);
	return sysinit_ret;
}

/**
 * tdx_cpu_enable - Enable TDX on local cpu
 *
 * Do one-time TDX module per-cpu initialization SEAMCALL (and TDX module
 * global initialization SEAMCALL if not done) on local cpu to make this
 * cpu be ready to run any other SEAMCALLs.
 *
 * Always call this function via IPI function calls.
 *
 * Return 0 on success, otherwise errors.
 */
int tdx_cpu_enable(void)
{
	struct tdx_module_args args = {};
	int ret;

	if (!boot_cpu_has(X86_FEATURE_TDX_HOST_PLATFORM))
		return -ENODEV;

	lockdep_assert_irqs_disabled();

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
EXPORT_SYMBOL_GPL(tdx_cpu_enable);

/*
 * Add a memory region as a TDX memory block.  The caller must make sure
 * all memory regions are added in address ascending order and don't
 * overlap.
 */
static int add_tdx_memblock(struct list_head *tmb_list, unsigned long start_pfn,
			    unsigned long end_pfn, int nid)
{
	struct tdx_memblock *tmb;

	tmb = kmalloc(sizeof(*tmb), GFP_KERNEL);
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

static void free_tdx_memlist(struct list_head *tmb_list)
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
static int build_tdx_memlist(struct list_head *tmb_list)
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

static int check_features(struct tdx_sys_info *sysinfo)
{
	u64 tdx_features0 = sysinfo->features.tdx_features0;

	if (!(tdx_features0 & TDX_FEATURES0_NO_RBP_MOD)) {
		pr_err("frame pointer (RBP) clobber bug present, upgrade TDX module\n");
		return -EINVAL;
	}

	return 0;
}

/* Calculate the actual TDMR size */
static int tdmr_size_single(u16 max_reserved_per_tdmr)
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

static int alloc_tdmr_list(struct tdmr_info_list *tdmr_list,
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

static void free_tdmr_list(struct tdmr_info_list *tdmr_list)
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
static int fill_out_tdmrs(struct list_head *tmb_list,
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

/*
 * Calculate PAMT size given a TDMR and a page size.  The returned
 * PAMT size is always aligned up to 4K page boundary.
 */
static unsigned long tdmr_get_pamt_sz(struct tdmr_info *tdmr, int pgsz,
				      u16 pamt_entry_size)
{
	unsigned long pamt_sz, nr_pamt_entries;

	switch (pgsz) {
	case TDX_PS_4K:
		nr_pamt_entries = tdmr->size >> PAGE_SHIFT;
		break;
	case TDX_PS_2M:
		nr_pamt_entries = tdmr->size >> PMD_SHIFT;
		break;
	case TDX_PS_1G:
		nr_pamt_entries = tdmr->size >> PUD_SHIFT;
		break;
	default:
		WARN_ON_ONCE(1);
		return 0;
	}

	pamt_sz = nr_pamt_entries * pamt_entry_size;
	/* TDX requires PAMT size must be 4K aligned */
	pamt_sz = ALIGN(pamt_sz, PAGE_SIZE);

	return pamt_sz;
}

/*
 * Locate a NUMA node which should hold the allocation of the @tdmr
 * PAMT.  This node will have some memory covered by the TDMR.  The
 * relative amount of memory covered is not considered.
 */
static int tdmr_get_nid(struct tdmr_info *tdmr, struct list_head *tmb_list)
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
static int tdmr_set_up_pamt(struct tdmr_info *tdmr,
			    struct list_head *tmb_list,
			    u16 pamt_entry_size[])
{
	unsigned long pamt_base[TDX_PS_NR];
	unsigned long pamt_size[TDX_PS_NR];
	unsigned long tdmr_pamt_base;
	unsigned long tdmr_pamt_size;
	struct page *pamt;
	int pgsz, nid;

	nid = tdmr_get_nid(tdmr, tmb_list);

	/*
	 * Calculate the PAMT size for each TDX supported page size
	 * and the total PAMT size.
	 */
	tdmr_pamt_size = 0;
	for (pgsz = TDX_PS_4K; pgsz < TDX_PS_NR; pgsz++) {
		pamt_size[pgsz] = tdmr_get_pamt_sz(tdmr, pgsz,
					pamt_entry_size[pgsz]);
		tdmr_pamt_size += pamt_size[pgsz];
	}

	/*
	 * Allocate one chunk of physically contiguous memory for all
	 * PAMTs.  This helps minimize the PAMT's use of reserved areas
	 * in overlapped TDMRs.
	 */
	pamt = alloc_contig_pages(tdmr_pamt_size >> PAGE_SHIFT, GFP_KERNEL,
			nid, &node_online_map);
	if (!pamt)
		return -ENOMEM;

	/*
	 * Break the contiguous allocation back up into the
	 * individual PAMTs for each page size.
	 */
	tdmr_pamt_base = page_to_pfn(pamt) << PAGE_SHIFT;
	for (pgsz = TDX_PS_4K; pgsz < TDX_PS_NR; pgsz++) {
		pamt_base[pgsz] = tdmr_pamt_base;
		tdmr_pamt_base += pamt_size[pgsz];
	}

	tdmr->pamt_4k_base = pamt_base[TDX_PS_4K];
	tdmr->pamt_4k_size = pamt_size[TDX_PS_4K];
	tdmr->pamt_2m_base = pamt_base[TDX_PS_2M];
	tdmr->pamt_2m_size = pamt_size[TDX_PS_2M];
	tdmr->pamt_1g_base = pamt_base[TDX_PS_1G];
	tdmr->pamt_1g_size = pamt_size[TDX_PS_1G];

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

static void tdmr_do_pamt_func(struct tdmr_info *tdmr,
		void (*pamt_func)(unsigned long base, unsigned long size))
{
	unsigned long pamt_base, pamt_size;

	tdmr_get_pamt(tdmr, &pamt_base, &pamt_size);

	/* Do nothing if PAMT hasn't been allocated for this TDMR */
	if (!pamt_size)
		return;

	if (WARN_ON_ONCE(!pamt_base))
		return;

	pamt_func(pamt_base, pamt_size);
}

static void free_pamt(unsigned long pamt_base, unsigned long pamt_size)
{
	free_contig_range(pamt_base >> PAGE_SHIFT, pamt_size >> PAGE_SHIFT);
}

static void tdmr_free_pamt(struct tdmr_info *tdmr)
{
	tdmr_do_pamt_func(tdmr, free_pamt);
}

static void tdmrs_free_pamt_all(struct tdmr_info_list *tdmr_list)
{
	int i;

	for (i = 0; i < tdmr_list->nr_consumed_tdmrs; i++)
		tdmr_free_pamt(tdmr_entry(tdmr_list, i));
}

/* Allocate and set up PAMTs for all TDMRs */
static int tdmrs_set_up_pamt_all(struct tdmr_info_list *tdmr_list,
				 struct list_head *tmb_list,
				 u16 pamt_entry_size[])
{
	int i, ret = 0;

	for (i = 0; i < tdmr_list->nr_consumed_tdmrs; i++) {
		ret = tdmr_set_up_pamt(tdmr_entry(tdmr_list, i), tmb_list,
				pamt_entry_size);
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
static void tdx_quirk_reset_paddr(unsigned long base, unsigned long size)
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

void tdx_quirk_reset_page(struct page *page)
{
	tdx_quirk_reset_paddr(page_to_phys(page), PAGE_SIZE);
}
EXPORT_SYMBOL_GPL(tdx_quirk_reset_page);

static void tdmr_quirk_reset_pamt(struct tdmr_info *tdmr)
{
	tdmr_do_pamt_func(tdmr, tdx_quirk_reset_paddr);
}

static void tdmrs_quirk_reset_pamt_all(struct tdmr_info_list *tdmr_list)
{
	int i;

	for (i = 0; i < tdmr_list->nr_consumed_tdmrs; i++)
		tdmr_quirk_reset_pamt(tdmr_entry(tdmr_list, i));
}

static unsigned long tdmrs_count_pamt_kb(struct tdmr_info_list *tdmr_list)
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

static int tdmr_add_rsvd_area(struct tdmr_info *tdmr, int *p_idx, u64 addr,
			      u64 size, u16 max_reserved_per_tdmr)
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
static int tdmr_populate_rsvd_holes(struct list_head *tmb_list,
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
static int tdmr_populate_rsvd_pamts(struct tdmr_info_list *tdmr_list,
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
static int rsvd_area_cmp_func(const void *a, const void *b)
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
static int tdmr_populate_rsvd_areas(struct tdmr_info *tdmr,
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
static int tdmrs_populate_rsvd_areas_all(struct tdmr_info_list *tdmr_list,
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
static int construct_tdmrs(struct list_head *tmb_list,
			   struct tdmr_info_list *tdmr_list,
			   struct tdx_sys_info_tdmr *sysinfo_tdmr)
{
	u16 pamt_entry_size[TDX_PS_NR] = {
		sysinfo_tdmr->pamt_4k_entry_size,
		sysinfo_tdmr->pamt_2m_entry_size,
		sysinfo_tdmr->pamt_1g_entry_size,
	};
	int ret;

	ret = fill_out_tdmrs(tmb_list, tdmr_list);
	if (ret)
		return ret;

	ret = tdmrs_set_up_pamt_all(tdmr_list, tmb_list, pamt_entry_size);
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

static int config_tdx_module(struct tdmr_info_list *tdmr_list, u64 global_keyid)
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
	ret = seamcall_prerr(TDH_SYS_CONFIG, &args);

	/* Free the array as it is not required anymore. */
	kfree(tdmr_pa_array);

	return ret;
}

static int do_global_key_config(void *unused)
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
static int config_global_keyid(void)
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

static int init_tdmr(struct tdmr_info *tdmr)
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

static int init_tdmrs(struct tdmr_info_list *tdmr_list)
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

static int init_tdx_module(void)
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

	ret = build_tdx_memlist(&tdx_memlist);
	if (ret)
		goto out_put_tdxmem;

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
	goto out_put_tdxmem;
}

static int __tdx_enable(void)
{
	int ret;

	ret = init_tdx_module();
	if (ret) {
		pr_err("module initialization failed (%d)\n", ret);
		tdx_module_status = TDX_MODULE_ERROR;
		return ret;
	}

	pr_info("module initialized\n");
	tdx_module_status = TDX_MODULE_INITIALIZED;

	return 0;
}

/**
 * tdx_enable - Enable TDX module to make it ready to run TDX guests
 *
 * This function assumes the caller has: 1) held read lock of CPU hotplug
 * lock to prevent any new cpu from becoming online; 2) done both VMXON
 * and tdx_cpu_enable() on all online cpus.
 *
 * This function requires there's at least one online cpu for each CPU
 * package to succeed.
 *
 * This function can be called in parallel by multiple callers.
 *
 * Return 0 if TDX is enabled successfully, otherwise error.
 */
int tdx_enable(void)
{
	int ret;

	if (!boot_cpu_has(X86_FEATURE_TDX_HOST_PLATFORM))
		return -ENODEV;

	lockdep_assert_cpus_held();

	mutex_lock(&tdx_module_lock);

	switch (tdx_module_status) {
	case TDX_MODULE_UNINITIALIZED:
		ret = __tdx_enable();
		break;
	case TDX_MODULE_INITIALIZED:
		/* Already initialized, great, tell the caller. */
		ret = 0;
		break;
	default:
		/* Failed to initialize in the previous attempts */
		ret = -EINVAL;
		break;
	}

	mutex_unlock(&tdx_module_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(tdx_enable);

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
	int ret;

	/*
	 * IA32_MKTME_KEYID_PARTIONING:
	 *   Bit [31:0]:	Number of MKTME KeyIDs.
	 *   Bit [63:32]:	Number of TDX private KeyIDs.
	 */
	ret = rdmsr_safe(MSR_IA32_MKTME_KEYID_PARTITIONING, &_nr_mktme_keyids,
			&_nr_tdx_keyids);
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
	const struct tdx_sys_info *p = NULL;

	/* Make sure all fields in @tdx_sysinfo have been populated */
	mutex_lock(&tdx_module_lock);
	if (tdx_module_status == TDX_MODULE_INITIALIZED)
		p = (const struct tdx_sys_info *)&tdx_sysinfo;
	mutex_unlock(&tdx_module_lock);

	return p;
}
EXPORT_SYMBOL_GPL(tdx_get_sysinfo);

u32 tdx_get_nr_guest_keyids(void)
{
	return tdx_nr_guest_keyids;
}
EXPORT_SYMBOL_GPL(tdx_get_nr_guest_keyids);

int tdx_guest_keyid_alloc(void)
{
	return ida_alloc_range(&tdx_guest_keyid_pool, tdx_guest_keyid_start,
			       tdx_guest_keyid_start + tdx_nr_guest_keyids - 1,
			       GFP_KERNEL);
}
EXPORT_SYMBOL_GPL(tdx_guest_keyid_alloc);

void tdx_guest_keyid_free(unsigned int keyid)
{
	ida_free(&tdx_guest_keyid_pool, keyid);
}
EXPORT_SYMBOL_GPL(tdx_guest_keyid_free);

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

noinstr u64 tdh_vp_enter(struct tdx_vp *td, struct tdx_module_args *args)
{
	args->rcx = td->tdvpr_pa;

	return __seamcall_dirty_cache(__seamcall_saved_ret, TDH_VP_ENTER, args);
}
EXPORT_SYMBOL_GPL(tdh_vp_enter);

u64 tdh_mng_addcx(struct tdx_td *td, struct page *tdcs_page)
{
	struct tdx_module_args args = {
		.rcx = page_to_phys(tdcs_page),
		.rdx = tdx_tdr_pa(td),
	};

	tdx_clflush_page(tdcs_page);
	return seamcall(TDH_MNG_ADDCX, &args);
}
EXPORT_SYMBOL_GPL(tdh_mng_addcx);

u64 tdh_mem_page_add(struct tdx_td *td, u64 gpa, struct page *page, struct page *source, u64 *ext_err1, u64 *ext_err2)
{
	struct tdx_module_args args = {
		.rcx = gpa,
		.rdx = tdx_tdr_pa(td),
		.r8 = page_to_phys(page),
		.r9 = page_to_phys(source),
	};
	u64 ret;

	tdx_clflush_page(page);
	ret = seamcall_ret(TDH_MEM_PAGE_ADD, &args);

	*ext_err1 = args.rcx;
	*ext_err2 = args.rdx;

	return ret;
}
EXPORT_SYMBOL_GPL(tdh_mem_page_add);

u64 tdh_mem_sept_add(struct tdx_td *td, u64 gpa, int level, struct page *page, u64 *ext_err1, u64 *ext_err2)
{
	struct tdx_module_args args = {
		.rcx = gpa | level,
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
EXPORT_SYMBOL_GPL(tdh_mem_sept_add);

u64 tdh_vp_addcx(struct tdx_vp *vp, struct page *tdcx_page)
{
	struct tdx_module_args args = {
		.rcx = page_to_phys(tdcx_page),
		.rdx = vp->tdvpr_pa,
	};

	tdx_clflush_page(tdcx_page);
	return seamcall(TDH_VP_ADDCX, &args);
}
EXPORT_SYMBOL_GPL(tdh_vp_addcx);

u64 tdh_mem_page_aug(struct tdx_td *td, u64 gpa, int level, struct page *page, u64 *ext_err1, u64 *ext_err2)
{
	struct tdx_module_args args = {
		.rcx = gpa | level,
		.rdx = tdx_tdr_pa(td),
		.r8 = page_to_phys(page),
	};
	u64 ret;

	tdx_clflush_page(page);
	ret = seamcall_ret(TDH_MEM_PAGE_AUG, &args);

	*ext_err1 = args.rcx;
	*ext_err2 = args.rdx;

	return ret;
}
EXPORT_SYMBOL_GPL(tdh_mem_page_aug);

u64 tdh_mem_range_block(struct tdx_td *td, u64 gpa, int level, u64 *ext_err1, u64 *ext_err2)
{
	struct tdx_module_args args = {
		.rcx = gpa | level,
		.rdx = tdx_tdr_pa(td),
	};
	u64 ret;

	ret = seamcall_ret(TDH_MEM_RANGE_BLOCK, &args);

	*ext_err1 = args.rcx;
	*ext_err2 = args.rdx;

	return ret;
}
EXPORT_SYMBOL_GPL(tdh_mem_range_block);

u64 tdh_mng_key_config(struct tdx_td *td)
{
	struct tdx_module_args args = {
		.rcx = tdx_tdr_pa(td),
	};

	return seamcall(TDH_MNG_KEY_CONFIG, &args);
}
EXPORT_SYMBOL_GPL(tdh_mng_key_config);

u64 tdh_mng_create(struct tdx_td *td, u16 hkid)
{
	struct tdx_module_args args = {
		.rcx = tdx_tdr_pa(td),
		.rdx = hkid,
	};

	tdx_clflush_page(td->tdr_page);
	return seamcall(TDH_MNG_CREATE, &args);
}
EXPORT_SYMBOL_GPL(tdh_mng_create);

u64 tdh_vp_create(struct tdx_td *td, struct tdx_vp *vp)
{
	struct tdx_module_args args = {
		.rcx = vp->tdvpr_pa,
		.rdx = tdx_tdr_pa(td),
	};

	tdx_clflush_page(vp->tdvpr_page);
	return seamcall(TDH_VP_CREATE, &args);
}
EXPORT_SYMBOL_GPL(tdh_vp_create);

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
EXPORT_SYMBOL_GPL(tdh_mng_rd);

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
EXPORT_SYMBOL_GPL(tdh_mr_extend);

u64 tdh_mr_finalize(struct tdx_td *td)
{
	struct tdx_module_args args = {
		.rcx = tdx_tdr_pa(td),
	};

	return seamcall(TDH_MR_FINALIZE, &args);
}
EXPORT_SYMBOL_GPL(tdh_mr_finalize);

u64 tdh_vp_flush(struct tdx_vp *vp)
{
	struct tdx_module_args args = {
		.rcx = vp->tdvpr_pa,
	};

	return seamcall(TDH_VP_FLUSH, &args);
}
EXPORT_SYMBOL_GPL(tdh_vp_flush);

u64 tdh_mng_vpflushdone(struct tdx_td *td)
{
	struct tdx_module_args args = {
		.rcx = tdx_tdr_pa(td),
	};

	return seamcall(TDH_MNG_VPFLUSHDONE, &args);
}
EXPORT_SYMBOL_GPL(tdh_mng_vpflushdone);

u64 tdh_mng_key_freeid(struct tdx_td *td)
{
	struct tdx_module_args args = {
		.rcx = tdx_tdr_pa(td),
	};

	return seamcall(TDH_MNG_KEY_FREEID, &args);
}
EXPORT_SYMBOL_GPL(tdh_mng_key_freeid);

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
EXPORT_SYMBOL_GPL(tdh_mng_init);

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
EXPORT_SYMBOL_GPL(tdh_vp_rd);

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
EXPORT_SYMBOL_GPL(tdh_vp_wr);

u64 tdh_vp_init(struct tdx_vp *vp, u64 initial_rcx, u32 x2apicid)
{
	struct tdx_module_args args = {
		.rcx = vp->tdvpr_pa,
		.rdx = initial_rcx,
		.r8 = x2apicid,
	};

	/* apicid requires version == 1. */
	return seamcall(TDH_VP_INIT | (1ULL << TDX_VERSION_SHIFT), &args);
}
EXPORT_SYMBOL_GPL(tdh_vp_init);

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
EXPORT_SYMBOL_GPL(tdh_phymem_page_reclaim);

u64 tdh_mem_track(struct tdx_td *td)
{
	struct tdx_module_args args = {
		.rcx = tdx_tdr_pa(td),
	};

	return seamcall(TDH_MEM_TRACK, &args);
}
EXPORT_SYMBOL_GPL(tdh_mem_track);

u64 tdh_mem_page_remove(struct tdx_td *td, u64 gpa, u64 level, u64 *ext_err1, u64 *ext_err2)
{
	struct tdx_module_args args = {
		.rcx = gpa | level,
		.rdx = tdx_tdr_pa(td),
	};
	u64 ret;

	ret = seamcall_ret(TDH_MEM_PAGE_REMOVE, &args);

	*ext_err1 = args.rcx;
	*ext_err2 = args.rdx;

	return ret;
}
EXPORT_SYMBOL_GPL(tdh_mem_page_remove);

u64 tdh_phymem_cache_wb(bool resume)
{
	struct tdx_module_args args = {
		.rcx = resume ? 1 : 0,
	};

	return seamcall(TDH_PHYMEM_CACHE_WB, &args);
}
EXPORT_SYMBOL_GPL(tdh_phymem_cache_wb);

u64 tdh_phymem_page_wbinvd_tdr(struct tdx_td *td)
{
	struct tdx_module_args args = {};

	args.rcx = mk_keyed_paddr(tdx_global_keyid, td->tdr_page);

	return seamcall(TDH_PHYMEM_PAGE_WBINVD, &args);
}
EXPORT_SYMBOL_GPL(tdh_phymem_page_wbinvd_tdr);

u64 tdh_phymem_page_wbinvd_hkid(u64 hkid, struct page *page)
{
	struct tdx_module_args args = {};

	args.rcx = mk_keyed_paddr(hkid, page);

	return seamcall(TDH_PHYMEM_PAGE_WBINVD, &args);
}
EXPORT_SYMBOL_GPL(tdh_phymem_page_wbinvd_hkid);

#ifdef CONFIG_KEXEC_CORE
void tdx_cpu_flush_cache_for_kexec(void)
{
	lockdep_assert_preemption_disabled();

	if (!this_cpu_read(cache_state_incoherent))
		return;

	/*
	 * Private memory cachelines need to be clean at the time of
	 * kexec.  Write them back now, as the caller promises that
	 * there should be no more SEAMCALLs on this CPU.
	 */
	wbinvd();
	this_cpu_write(cache_state_incoherent, false);
}
EXPORT_SYMBOL_GPL(tdx_cpu_flush_cache_for_kexec);
#endif

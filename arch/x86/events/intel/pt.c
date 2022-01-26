// SPDX-License-Identifier: GPL-2.0-only
/*
 * Intel(R) Processor Trace PMU driver for perf
 * Copyright (c) 2013-2014, Intel Corporation.
 *
 * Intel PT is specified in the Intel Architecture Instruction Set Extensions
 * Programming Reference:
 * http://software.intel.com/en-us/intel-isa-extensions
 */

#undef DEBUG

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/types.h>
#include <linux/bits.h>
#include <linux/limits.h>
#include <linux/slab.h>
#include <linux/device.h>

#include <asm/perf_event.h>
#include <asm/insn.h>
#include <asm/io.h>
#include <asm/intel_pt.h>
#include <asm/intel-family.h>

#include "../perf_event.h"
#include "pt.h"

static DEFINE_PER_CPU(struct pt, pt_ctx);

static struct pt_pmu pt_pmu;

/*
 * Capabilities of Intel PT hardware, such as number of address bits or
 * supported output schemes, are cached and exported to userspace as "caps"
 * attribute group of pt pmu device
 * (/sys/bus/event_source/devices/intel_pt/caps/) so that userspace can store
 * relevant bits together with intel_pt traces.
 *
 * These are necessary for both trace decoding (payloads_lip, contains address
 * width encoded in IP-related packets), and event configuration (bitmasks with
 * permitted values for certain bit fields).
 */
#define PT_CAP(_n, _l, _r, _m)						\
	[PT_CAP_ ## _n] = { .name = __stringify(_n), .leaf = _l,	\
			    .reg = _r, .mask = _m }

static struct pt_cap_desc {
	const char	*name;
	u32		leaf;
	u8		reg;
	u32		mask;
} pt_caps[] = {
	PT_CAP(max_subleaf,		0, CPUID_EAX, 0xffffffff),
	PT_CAP(cr3_filtering,		0, CPUID_EBX, BIT(0)),
	PT_CAP(psb_cyc,			0, CPUID_EBX, BIT(1)),
	PT_CAP(ip_filtering,		0, CPUID_EBX, BIT(2)),
	PT_CAP(mtc,			0, CPUID_EBX, BIT(3)),
	PT_CAP(ptwrite,			0, CPUID_EBX, BIT(4)),
	PT_CAP(power_event_trace,	0, CPUID_EBX, BIT(5)),
	PT_CAP(event_trace,		0, CPUID_EBX, BIT(7)),
	PT_CAP(tnt_disable,		0, CPUID_EBX, BIT(8)),
	PT_CAP(topa_output,		0, CPUID_ECX, BIT(0)),
	PT_CAP(topa_multiple_entries,	0, CPUID_ECX, BIT(1)),
	PT_CAP(single_range_output,	0, CPUID_ECX, BIT(2)),
	PT_CAP(output_subsys,		0, CPUID_ECX, BIT(3)),
	PT_CAP(payloads_lip,		0, CPUID_ECX, BIT(31)),
	PT_CAP(num_address_ranges,	1, CPUID_EAX, 0x7),
	PT_CAP(mtc_periods,		1, CPUID_EAX, 0xffff0000),
	PT_CAP(cycle_thresholds,	1, CPUID_EBX, 0xffff),
	PT_CAP(psb_periods,		1, CPUID_EBX, 0xffff0000),
};

u32 intel_pt_validate_cap(u32 *caps, enum pt_capabilities capability)
{
	struct pt_cap_desc *cd = &pt_caps[capability];
	u32 c = caps[cd->leaf * PT_CPUID_REGS_NUM + cd->reg];
	unsigned int shift = __ffs(cd->mask);

	return (c & cd->mask) >> shift;
}
EXPORT_SYMBOL_GPL(intel_pt_validate_cap);

u32 intel_pt_validate_hw_cap(enum pt_capabilities cap)
{
	return intel_pt_validate_cap(pt_pmu.caps, cap);
}
EXPORT_SYMBOL_GPL(intel_pt_validate_hw_cap);

static ssize_t pt_cap_show(struct device *cdev,
			   struct device_attribute *attr,
			   char *buf)
{
	struct dev_ext_attribute *ea =
		container_of(attr, struct dev_ext_attribute, attr);
	enum pt_capabilities cap = (long)ea->var;

	return snprintf(buf, PAGE_SIZE, "%x\n", intel_pt_validate_hw_cap(cap));
}

static struct attribute_group pt_cap_group __ro_after_init = {
	.name	= "caps",
};

PMU_FORMAT_ATTR(pt,		"config:0"	);
PMU_FORMAT_ATTR(cyc,		"config:1"	);
PMU_FORMAT_ATTR(pwr_evt,	"config:4"	);
PMU_FORMAT_ATTR(fup_on_ptw,	"config:5"	);
PMU_FORMAT_ATTR(mtc,		"config:9"	);
PMU_FORMAT_ATTR(tsc,		"config:10"	);
PMU_FORMAT_ATTR(noretcomp,	"config:11"	);
PMU_FORMAT_ATTR(ptw,		"config:12"	);
PMU_FORMAT_ATTR(branch,		"config:13"	);
PMU_FORMAT_ATTR(event,		"config:31"	);
PMU_FORMAT_ATTR(notnt,		"config:55"	);
PMU_FORMAT_ATTR(mtc_period,	"config:14-17"	);
PMU_FORMAT_ATTR(cyc_thresh,	"config:19-22"	);
PMU_FORMAT_ATTR(psb_period,	"config:24-27"	);

static struct attribute *pt_formats_attr[] = {
	&format_attr_pt.attr,
	&format_attr_cyc.attr,
	&format_attr_pwr_evt.attr,
	&format_attr_event.attr,
	&format_attr_notnt.attr,
	&format_attr_fup_on_ptw.attr,
	&format_attr_mtc.attr,
	&format_attr_tsc.attr,
	&format_attr_noretcomp.attr,
	&format_attr_ptw.attr,
	&format_attr_branch.attr,
	&format_attr_mtc_period.attr,
	&format_attr_cyc_thresh.attr,
	&format_attr_psb_period.attr,
	NULL,
};

static struct attribute_group pt_format_group = {
	.name	= "format",
	.attrs	= pt_formats_attr,
};

static ssize_t
pt_timing_attr_show(struct device *dev, struct device_attribute *attr,
		    char *page)
{
	struct perf_pmu_events_attr *pmu_attr =
		container_of(attr, struct perf_pmu_events_attr, attr);

	switch (pmu_attr->id) {
	case 0:
		return sprintf(page, "%lu\n", pt_pmu.max_nonturbo_ratio);
	case 1:
		return sprintf(page, "%u:%u\n",
			       pt_pmu.tsc_art_num,
			       pt_pmu.tsc_art_den);
	default:
		break;
	}

	return -EINVAL;
}

PMU_EVENT_ATTR(max_nonturbo_ratio, timing_attr_max_nonturbo_ratio, 0,
	       pt_timing_attr_show);
PMU_EVENT_ATTR(tsc_art_ratio, timing_attr_tsc_art_ratio, 1,
	       pt_timing_attr_show);

static struct attribute *pt_timing_attr[] = {
	&timing_attr_max_nonturbo_ratio.attr.attr,
	&timing_attr_tsc_art_ratio.attr.attr,
	NULL,
};

static struct attribute_group pt_timing_group = {
	.attrs	= pt_timing_attr,
};

static const struct attribute_group *pt_attr_groups[] = {
	&pt_cap_group,
	&pt_format_group,
	&pt_timing_group,
	NULL,
};

static int __init pt_pmu_hw_init(void)
{
	struct dev_ext_attribute *de_attrs;
	struct attribute **attrs;
	size_t size;
	u64 reg;
	int ret;
	long i;

	rdmsrl(MSR_PLATFORM_INFO, reg);
	pt_pmu.max_nonturbo_ratio = (reg & 0xff00) >> 8;

	/*
	 * if available, read in TSC to core crystal clock ratio,
	 * otherwise, zero for numerator stands for "not enumerated"
	 * as per SDM
	 */
	if (boot_cpu_data.cpuid_level >= CPUID_TSC_LEAF) {
		u32 eax, ebx, ecx, edx;

		cpuid(CPUID_TSC_LEAF, &eax, &ebx, &ecx, &edx);

		pt_pmu.tsc_art_num = ebx;
		pt_pmu.tsc_art_den = eax;
	}

	/* model-specific quirks */
	switch (boot_cpu_data.x86_model) {
	case INTEL_FAM6_BROADWELL:
	case INTEL_FAM6_BROADWELL_D:
	case INTEL_FAM6_BROADWELL_G:
	case INTEL_FAM6_BROADWELL_X:
		/* not setting BRANCH_EN will #GP, erratum BDM106 */
		pt_pmu.branch_en_always_on = true;
		break;
	default:
		break;
	}

	if (boot_cpu_has(X86_FEATURE_VMX)) {
		/*
		 * Intel SDM, 36.5 "Tracing post-VMXON" says that
		 * "IA32_VMX_MISC[bit 14]" being 1 means PT can trace
		 * post-VMXON.
		 */
		rdmsrl(MSR_IA32_VMX_MISC, reg);
		if (reg & BIT(14))
			pt_pmu.vmx = true;
	}

	for (i = 0; i < PT_CPUID_LEAVES; i++) {
		cpuid_count(20, i,
			    &pt_pmu.caps[CPUID_EAX + i*PT_CPUID_REGS_NUM],
			    &pt_pmu.caps[CPUID_EBX + i*PT_CPUID_REGS_NUM],
			    &pt_pmu.caps[CPUID_ECX + i*PT_CPUID_REGS_NUM],
			    &pt_pmu.caps[CPUID_EDX + i*PT_CPUID_REGS_NUM]);
	}

	ret = -ENOMEM;
	size = sizeof(struct attribute *) * (ARRAY_SIZE(pt_caps)+1);
	attrs = kzalloc(size, GFP_KERNEL);
	if (!attrs)
		goto fail;

	size = sizeof(struct dev_ext_attribute) * (ARRAY_SIZE(pt_caps)+1);
	de_attrs = kzalloc(size, GFP_KERNEL);
	if (!de_attrs)
		goto fail;

	for (i = 0; i < ARRAY_SIZE(pt_caps); i++) {
		struct dev_ext_attribute *de_attr = de_attrs + i;

		de_attr->attr.attr.name = pt_caps[i].name;

		sysfs_attr_init(&de_attr->attr.attr);

		de_attr->attr.attr.mode		= S_IRUGO;
		de_attr->attr.show		= pt_cap_show;
		de_attr->var			= (void *)i;

		attrs[i] = &de_attr->attr.attr;
	}

	pt_cap_group.attrs = attrs;

	return 0;

fail:
	kfree(attrs);

	return ret;
}

#define RTIT_CTL_CYC_PSB (RTIT_CTL_CYCLEACC	| \
			  RTIT_CTL_CYC_THRESH	| \
			  RTIT_CTL_PSB_FREQ)

#define RTIT_CTL_MTC	(RTIT_CTL_MTC_EN	| \
			 RTIT_CTL_MTC_RANGE)

#define RTIT_CTL_PTW	(RTIT_CTL_PTW_EN	| \
			 RTIT_CTL_FUP_ON_PTW)

/*
 * Bit 0 (TraceEn) in the attr.config is meaningless as the
 * corresponding bit in the RTIT_CTL can only be controlled
 * by the driver; therefore, repurpose it to mean: pass
 * through the bit that was previously assumed to be always
 * on for PT, thereby allowing the user to *not* set it if
 * they so wish. See also pt_event_valid() and pt_config().
 */
#define RTIT_CTL_PASSTHROUGH RTIT_CTL_TRACEEN

#define PT_CONFIG_MASK (RTIT_CTL_TRACEEN	| \
			RTIT_CTL_TSC_EN		| \
			RTIT_CTL_DISRETC	| \
			RTIT_CTL_BRANCH_EN	| \
			RTIT_CTL_CYC_PSB	| \
			RTIT_CTL_MTC		| \
			RTIT_CTL_PWR_EVT_EN	| \
			RTIT_CTL_EVENT_EN	| \
			RTIT_CTL_NOTNT		| \
			RTIT_CTL_FUP_ON_PTW	| \
			RTIT_CTL_PTW_EN)

static bool pt_event_valid(struct perf_event *event)
{
	u64 config = event->attr.config;
	u64 allowed, requested;

	if ((config & PT_CONFIG_MASK) != config)
		return false;

	if (config & RTIT_CTL_CYC_PSB) {
		if (!intel_pt_validate_hw_cap(PT_CAP_psb_cyc))
			return false;

		allowed = intel_pt_validate_hw_cap(PT_CAP_psb_periods);
		requested = (config & RTIT_CTL_PSB_FREQ) >>
			RTIT_CTL_PSB_FREQ_OFFSET;
		if (requested && (!(allowed & BIT(requested))))
			return false;

		allowed = intel_pt_validate_hw_cap(PT_CAP_cycle_thresholds);
		requested = (config & RTIT_CTL_CYC_THRESH) >>
			RTIT_CTL_CYC_THRESH_OFFSET;
		if (requested && (!(allowed & BIT(requested))))
			return false;
	}

	if (config & RTIT_CTL_MTC) {
		/*
		 * In the unlikely case that CPUID lists valid mtc periods,
		 * but not the mtc capability, drop out here.
		 *
		 * Spec says that setting mtc period bits while mtc bit in
		 * CPUID is 0 will #GP, so better safe than sorry.
		 */
		if (!intel_pt_validate_hw_cap(PT_CAP_mtc))
			return false;

		allowed = intel_pt_validate_hw_cap(PT_CAP_mtc_periods);
		if (!allowed)
			return false;

		requested = (config & RTIT_CTL_MTC_RANGE) >>
			RTIT_CTL_MTC_RANGE_OFFSET;

		if (!(allowed & BIT(requested)))
			return false;
	}

	if (config & RTIT_CTL_PWR_EVT_EN &&
	    !intel_pt_validate_hw_cap(PT_CAP_power_event_trace))
		return false;

	if (config & RTIT_CTL_EVENT_EN &&
	    !intel_pt_validate_hw_cap(PT_CAP_event_trace))
		return false;

	if (config & RTIT_CTL_NOTNT &&
	    !intel_pt_validate_hw_cap(PT_CAP_tnt_disable))
		return false;

	if (config & RTIT_CTL_PTW) {
		if (!intel_pt_validate_hw_cap(PT_CAP_ptwrite))
			return false;

		/* FUPonPTW without PTW doesn't make sense */
		if ((config & RTIT_CTL_FUP_ON_PTW) &&
		    !(config & RTIT_CTL_PTW_EN))
			return false;
	}

	/*
	 * Setting bit 0 (TraceEn in RTIT_CTL MSR) in the attr.config
	 * clears the assumption that BranchEn must always be enabled,
	 * as was the case with the first implementation of PT.
	 * If this bit is not set, the legacy behavior is preserved
	 * for compatibility with the older userspace.
	 *
	 * Re-using bit 0 for this purpose is fine because it is never
	 * directly set by the user; previous attempts at setting it in
	 * the attr.config resulted in -EINVAL.
	 */
	if (config & RTIT_CTL_PASSTHROUGH) {
		/*
		 * Disallow not setting BRANCH_EN where BRANCH_EN is
		 * always required.
		 */
		if (pt_pmu.branch_en_always_on &&
		    !(config & RTIT_CTL_BRANCH_EN))
			return false;
	} else {
		/*
		 * Disallow BRANCH_EN without the PASSTHROUGH.
		 */
		if (config & RTIT_CTL_BRANCH_EN)
			return false;
	}

	return true;
}

/*
 * PT configuration helpers
 * These all are cpu affine and operate on a local PT
 */

static void pt_config_start(struct perf_event *event)
{
	struct pt *pt = this_cpu_ptr(&pt_ctx);
	u64 ctl = event->hw.config;

	ctl |= RTIT_CTL_TRACEEN;
	if (READ_ONCE(pt->vmx_on))
		perf_aux_output_flag(&pt->handle, PERF_AUX_FLAG_PARTIAL);
	else
		wrmsrl(MSR_IA32_RTIT_CTL, ctl);

	WRITE_ONCE(event->hw.config, ctl);
}

/* Address ranges and their corresponding msr configuration registers */
static const struct pt_address_range {
	unsigned long	msr_a;
	unsigned long	msr_b;
	unsigned int	reg_off;
} pt_address_ranges[] = {
	{
		.msr_a	 = MSR_IA32_RTIT_ADDR0_A,
		.msr_b	 = MSR_IA32_RTIT_ADDR0_B,
		.reg_off = RTIT_CTL_ADDR0_OFFSET,
	},
	{
		.msr_a	 = MSR_IA32_RTIT_ADDR1_A,
		.msr_b	 = MSR_IA32_RTIT_ADDR1_B,
		.reg_off = RTIT_CTL_ADDR1_OFFSET,
	},
	{
		.msr_a	 = MSR_IA32_RTIT_ADDR2_A,
		.msr_b	 = MSR_IA32_RTIT_ADDR2_B,
		.reg_off = RTIT_CTL_ADDR2_OFFSET,
	},
	{
		.msr_a	 = MSR_IA32_RTIT_ADDR3_A,
		.msr_b	 = MSR_IA32_RTIT_ADDR3_B,
		.reg_off = RTIT_CTL_ADDR3_OFFSET,
	}
};

static u64 pt_config_filters(struct perf_event *event)
{
	struct pt_filters *filters = event->hw.addr_filters;
	struct pt *pt = this_cpu_ptr(&pt_ctx);
	unsigned int range = 0;
	u64 rtit_ctl = 0;

	if (!filters)
		return 0;

	perf_event_addr_filters_sync(event);

	for (range = 0; range < filters->nr_filters; range++) {
		struct pt_filter *filter = &filters->filter[range];

		/*
		 * Note, if the range has zero start/end addresses due
		 * to its dynamic object not being loaded yet, we just
		 * go ahead and program zeroed range, which will simply
		 * produce no data. Note^2: if executable code at 0x0
		 * is a concern, we can set up an "invalid" configuration
		 * such as msr_b < msr_a.
		 */

		/* avoid redundant msr writes */
		if (pt->filters.filter[range].msr_a != filter->msr_a) {
			wrmsrl(pt_address_ranges[range].msr_a, filter->msr_a);
			pt->filters.filter[range].msr_a = filter->msr_a;
		}

		if (pt->filters.filter[range].msr_b != filter->msr_b) {
			wrmsrl(pt_address_ranges[range].msr_b, filter->msr_b);
			pt->filters.filter[range].msr_b = filter->msr_b;
		}

		rtit_ctl |= (u64)filter->config << pt_address_ranges[range].reg_off;
	}

	return rtit_ctl;
}

static void pt_config(struct perf_event *event)
{
	struct pt *pt = this_cpu_ptr(&pt_ctx);
	struct pt_buffer *buf = perf_get_aux(&pt->handle);
	u64 reg;

	/* First round: clear STATUS, in particular the PSB byte counter. */
	if (!event->hw.config) {
		perf_event_itrace_started(event);
		wrmsrl(MSR_IA32_RTIT_STATUS, 0);
	}

	reg = pt_config_filters(event);
	reg |= RTIT_CTL_TRACEEN;
	if (!buf->single)
		reg |= RTIT_CTL_TOPA;

	/*
	 * Previously, we had BRANCH_EN on by default, but now that PT has
	 * grown features outside of branch tracing, it is useful to allow
	 * the user to disable it. Setting bit 0 in the event's attr.config
	 * allows BRANCH_EN to pass through instead of being always on. See
	 * also the comment in pt_event_valid().
	 */
	if (event->attr.config & BIT(0)) {
		reg |= event->attr.config & RTIT_CTL_BRANCH_EN;
	} else {
		reg |= RTIT_CTL_BRANCH_EN;
	}

	if (!event->attr.exclude_kernel)
		reg |= RTIT_CTL_OS;
	if (!event->attr.exclude_user)
		reg |= RTIT_CTL_USR;

	reg |= (event->attr.config & PT_CONFIG_MASK);

	event->hw.config = reg;
	pt_config_start(event);
}

static void pt_config_stop(struct perf_event *event)
{
	struct pt *pt = this_cpu_ptr(&pt_ctx);
	u64 ctl = READ_ONCE(event->hw.config);

	/* may be already stopped by a PMI */
	if (!(ctl & RTIT_CTL_TRACEEN))
		return;

	ctl &= ~RTIT_CTL_TRACEEN;
	if (!READ_ONCE(pt->vmx_on))
		wrmsrl(MSR_IA32_RTIT_CTL, ctl);

	WRITE_ONCE(event->hw.config, ctl);

	/*
	 * A wrmsr that disables trace generation serializes other PT
	 * registers and causes all data packets to be written to memory,
	 * but a fence is required for the data to become globally visible.
	 *
	 * The below WMB, separating data store and aux_head store matches
	 * the consumer's RMB that separates aux_head load and data load.
	 */
	wmb();
}

/**
 * struct topa - ToPA metadata
 * @list:	linkage to struct pt_buffer's list of tables
 * @offset:	offset of the first entry in this table in the buffer
 * @size:	total size of all entries in this table
 * @last:	index of the last initialized entry in this table
 * @z_count:	how many times the first entry repeats
 */
struct topa {
	struct list_head	list;
	u64			offset;
	size_t			size;
	int			last;
	unsigned int		z_count;
};

/*
 * Keep ToPA table-related metadata on the same page as the actual table,
 * taking up a few words from the top
 */

#define TENTS_PER_PAGE	\
	((PAGE_SIZE - sizeof(struct topa)) / sizeof(struct topa_entry))

/**
 * struct topa_page - page-sized ToPA table with metadata at the top
 * @table:	actual ToPA table entries, as understood by PT hardware
 * @topa:	metadata
 */
struct topa_page {
	struct topa_entry	table[TENTS_PER_PAGE];
	struct topa		topa;
};

static inline struct topa_page *topa_to_page(struct topa *topa)
{
	return container_of(topa, struct topa_page, topa);
}

static inline struct topa_page *topa_entry_to_page(struct topa_entry *te)
{
	return (struct topa_page *)((unsigned long)te & PAGE_MASK);
}

static inline phys_addr_t topa_pfn(struct topa *topa)
{
	return PFN_DOWN(virt_to_phys(topa_to_page(topa)));
}

/* make -1 stand for the last table entry */
#define TOPA_ENTRY(t, i)				\
	((i) == -1					\
		? &topa_to_page(t)->table[(t)->last]	\
		: &topa_to_page(t)->table[(i)])
#define TOPA_ENTRY_SIZE(t, i) (sizes(TOPA_ENTRY((t), (i))->size))
#define TOPA_ENTRY_PAGES(t, i) (1 << TOPA_ENTRY((t), (i))->size)

static void pt_config_buffer(struct pt_buffer *buf)
{
	struct pt *pt = this_cpu_ptr(&pt_ctx);
	u64 reg, mask;
	void *base;

	if (buf->single) {
		base = buf->data_pages[0];
		mask = (buf->nr_pages * PAGE_SIZE - 1) >> 7;
	} else {
		base = topa_to_page(buf->cur)->table;
		mask = (u64)buf->cur_idx;
	}

	reg = virt_to_phys(base);
	if (pt->output_base != reg) {
		pt->output_base = reg;
		wrmsrl(MSR_IA32_RTIT_OUTPUT_BASE, reg);
	}

	reg = 0x7f | (mask << 7) | ((u64)buf->output_off << 32);
	if (pt->output_mask != reg) {
		pt->output_mask = reg;
		wrmsrl(MSR_IA32_RTIT_OUTPUT_MASK, reg);
	}
}

/**
 * topa_alloc() - allocate page-sized ToPA table
 * @cpu:	CPU on which to allocate.
 * @gfp:	Allocation flags.
 *
 * Return:	On success, return the pointer to ToPA table page.
 */
static struct topa *topa_alloc(int cpu, gfp_t gfp)
{
	int node = cpu_to_node(cpu);
	struct topa_page *tp;
	struct page *p;

	p = alloc_pages_node(node, gfp | __GFP_ZERO, 0);
	if (!p)
		return NULL;

	tp = page_address(p);
	tp->topa.last = 0;

	/*
	 * In case of singe-entry ToPA, always put the self-referencing END
	 * link as the 2nd entry in the table
	 */
	if (!intel_pt_validate_hw_cap(PT_CAP_topa_multiple_entries)) {
		TOPA_ENTRY(&tp->topa, 1)->base = page_to_phys(p) >> TOPA_SHIFT;
		TOPA_ENTRY(&tp->topa, 1)->end = 1;
	}

	return &tp->topa;
}

/**
 * topa_free() - free a page-sized ToPA table
 * @topa:	Table to deallocate.
 */
static void topa_free(struct topa *topa)
{
	free_page((unsigned long)topa);
}

/**
 * topa_insert_table() - insert a ToPA table into a buffer
 * @buf:	 PT buffer that's being extended.
 * @topa:	 New topa table to be inserted.
 *
 * If it's the first table in this buffer, set up buffer's pointers
 * accordingly; otherwise, add a END=1 link entry to @topa to the current
 * "last" table and adjust the last table pointer to @topa.
 */
static void topa_insert_table(struct pt_buffer *buf, struct topa *topa)
{
	struct topa *last = buf->last;

	list_add_tail(&topa->list, &buf->tables);

	if (!buf->first) {
		buf->first = buf->last = buf->cur = topa;
		return;
	}

	topa->offset = last->offset + last->size;
	buf->last = topa;

	if (!intel_pt_validate_hw_cap(PT_CAP_topa_multiple_entries))
		return;

	BUG_ON(last->last != TENTS_PER_PAGE - 1);

	TOPA_ENTRY(last, -1)->base = topa_pfn(topa);
	TOPA_ENTRY(last, -1)->end = 1;
}

/**
 * topa_table_full() - check if a ToPA table is filled up
 * @topa:	ToPA table.
 */
static bool topa_table_full(struct topa *topa)
{
	/* single-entry ToPA is a special case */
	if (!intel_pt_validate_hw_cap(PT_CAP_topa_multiple_entries))
		return !!topa->last;

	return topa->last == TENTS_PER_PAGE - 1;
}

/**
 * topa_insert_pages() - create a list of ToPA tables
 * @buf:	PT buffer being initialized.
 * @gfp:	Allocation flags.
 *
 * This initializes a list of ToPA tables with entries from
 * the data_pages provided by rb_alloc_aux().
 *
 * Return:	0 on success or error code.
 */
static int topa_insert_pages(struct pt_buffer *buf, int cpu, gfp_t gfp)
{
	struct topa *topa = buf->last;
	int order = 0;
	struct page *p;

	p = virt_to_page(buf->data_pages[buf->nr_pages]);
	if (PagePrivate(p))
		order = page_private(p);

	if (topa_table_full(topa)) {
		topa = topa_alloc(cpu, gfp);
		if (!topa)
			return -ENOMEM;

		topa_insert_table(buf, topa);
	}

	if (topa->z_count == topa->last - 1) {
		if (order == TOPA_ENTRY(topa, topa->last - 1)->size)
			topa->z_count++;
	}

	TOPA_ENTRY(topa, -1)->base = page_to_phys(p) >> TOPA_SHIFT;
	TOPA_ENTRY(topa, -1)->size = order;
	if (!buf->snapshot &&
	    !intel_pt_validate_hw_cap(PT_CAP_topa_multiple_entries)) {
		TOPA_ENTRY(topa, -1)->intr = 1;
		TOPA_ENTRY(topa, -1)->stop = 1;
	}

	topa->last++;
	topa->size += sizes(order);

	buf->nr_pages += 1ul << order;

	return 0;
}

/**
 * pt_topa_dump() - print ToPA tables and their entries
 * @buf:	PT buffer.
 */
static void pt_topa_dump(struct pt_buffer *buf)
{
	struct topa *topa;

	list_for_each_entry(topa, &buf->tables, list) {
		struct topa_page *tp = topa_to_page(topa);
		int i;

		pr_debug("# table @%p, off %llx size %zx\n", tp->table,
			 topa->offset, topa->size);
		for (i = 0; i < TENTS_PER_PAGE; i++) {
			pr_debug("# entry @%p (%lx sz %u %c%c%c) raw=%16llx\n",
				 &tp->table[i],
				 (unsigned long)tp->table[i].base << TOPA_SHIFT,
				 sizes(tp->table[i].size),
				 tp->table[i].end ?  'E' : ' ',
				 tp->table[i].intr ? 'I' : ' ',
				 tp->table[i].stop ? 'S' : ' ',
				 *(u64 *)&tp->table[i]);
			if ((intel_pt_validate_hw_cap(PT_CAP_topa_multiple_entries) &&
			     tp->table[i].stop) ||
			    tp->table[i].end)
				break;
			if (!i && topa->z_count)
				i += topa->z_count;
		}
	}
}

/**
 * pt_buffer_advance() - advance to the next output region
 * @buf:	PT buffer.
 *
 * Advance the current pointers in the buffer to the next ToPA entry.
 */
static void pt_buffer_advance(struct pt_buffer *buf)
{
	buf->output_off = 0;
	buf->cur_idx++;

	if (buf->cur_idx == buf->cur->last) {
		if (buf->cur == buf->last)
			buf->cur = buf->first;
		else
			buf->cur = list_entry(buf->cur->list.next, struct topa,
					      list);
		buf->cur_idx = 0;
	}
}

/**
 * pt_update_head() - calculate current offsets and sizes
 * @pt:		Per-cpu pt context.
 *
 * Update buffer's current write pointer position and data size.
 */
static void pt_update_head(struct pt *pt)
{
	struct pt_buffer *buf = perf_get_aux(&pt->handle);
	u64 topa_idx, base, old;

	if (buf->single) {
		local_set(&buf->data_size, buf->output_off);
		return;
	}

	/* offset of the first region in this table from the beginning of buf */
	base = buf->cur->offset + buf->output_off;

	/* offset of the current output region within this table */
	for (topa_idx = 0; topa_idx < buf->cur_idx; topa_idx++)
		base += TOPA_ENTRY_SIZE(buf->cur, topa_idx);

	if (buf->snapshot) {
		local_set(&buf->data_size, base);
	} else {
		old = (local64_xchg(&buf->head, base) &
		       ((buf->nr_pages << PAGE_SHIFT) - 1));
		if (base < old)
			base += buf->nr_pages << PAGE_SHIFT;

		local_add(base - old, &buf->data_size);
	}
}

/**
 * pt_buffer_region() - obtain current output region's address
 * @buf:	PT buffer.
 */
static void *pt_buffer_region(struct pt_buffer *buf)
{
	return phys_to_virt(TOPA_ENTRY(buf->cur, buf->cur_idx)->base << TOPA_SHIFT);
}

/**
 * pt_buffer_region_size() - obtain current output region's size
 * @buf:	PT buffer.
 */
static size_t pt_buffer_region_size(struct pt_buffer *buf)
{
	return TOPA_ENTRY_SIZE(buf->cur, buf->cur_idx);
}

/**
 * pt_handle_status() - take care of possible status conditions
 * @pt:		Per-cpu pt context.
 */
static void pt_handle_status(struct pt *pt)
{
	struct pt_buffer *buf = perf_get_aux(&pt->handle);
	int advance = 0;
	u64 status;

	rdmsrl(MSR_IA32_RTIT_STATUS, status);

	if (status & RTIT_STATUS_ERROR) {
		pr_err_ratelimited("ToPA ERROR encountered, trying to recover\n");
		pt_topa_dump(buf);
		status &= ~RTIT_STATUS_ERROR;
	}

	if (status & RTIT_STATUS_STOPPED) {
		status &= ~RTIT_STATUS_STOPPED;

		/*
		 * On systems that only do single-entry ToPA, hitting STOP
		 * means we are already losing data; need to let the decoder
		 * know.
		 */
		if (!intel_pt_validate_hw_cap(PT_CAP_topa_multiple_entries) ||
		    buf->output_off == pt_buffer_region_size(buf)) {
			perf_aux_output_flag(&pt->handle,
			                     PERF_AUX_FLAG_TRUNCATED);
			advance++;
		}
	}

	/*
	 * Also on single-entry ToPA implementations, interrupt will come
	 * before the output reaches its output region's boundary.
	 */
	if (!intel_pt_validate_hw_cap(PT_CAP_topa_multiple_entries) &&
	    !buf->snapshot &&
	    pt_buffer_region_size(buf) - buf->output_off <= TOPA_PMI_MARGIN) {
		void *head = pt_buffer_region(buf);

		/* everything within this margin needs to be zeroed out */
		memset(head + buf->output_off, 0,
		       pt_buffer_region_size(buf) -
		       buf->output_off);
		advance++;
	}

	if (advance)
		pt_buffer_advance(buf);

	wrmsrl(MSR_IA32_RTIT_STATUS, status);
}

/**
 * pt_read_offset() - translate registers into buffer pointers
 * @buf:	PT buffer.
 *
 * Set buffer's output pointers from MSR values.
 */
static void pt_read_offset(struct pt_buffer *buf)
{
	struct pt *pt = this_cpu_ptr(&pt_ctx);
	struct topa_page *tp;

	if (!buf->single) {
		rdmsrl(MSR_IA32_RTIT_OUTPUT_BASE, pt->output_base);
		tp = phys_to_virt(pt->output_base);
		buf->cur = &tp->topa;
	}

	rdmsrl(MSR_IA32_RTIT_OUTPUT_MASK, pt->output_mask);
	/* offset within current output region */
	buf->output_off = pt->output_mask >> 32;
	/* index of current output region within this table */
	if (!buf->single)
		buf->cur_idx = (pt->output_mask & 0xffffff80) >> 7;
}

static struct topa_entry *
pt_topa_entry_for_page(struct pt_buffer *buf, unsigned int pg)
{
	struct topa_page *tp;
	struct topa *topa;
	unsigned int idx, cur_pg = 0, z_pg = 0, start_idx = 0;

	/*
	 * Indicates a bug in the caller.
	 */
	if (WARN_ON_ONCE(pg >= buf->nr_pages))
		return NULL;

	/*
	 * First, find the ToPA table where @pg fits. With high
	 * order allocations, there shouldn't be many of these.
	 */
	list_for_each_entry(topa, &buf->tables, list) {
		if (topa->offset + topa->size > pg << PAGE_SHIFT)
			goto found;
	}

	/*
	 * Hitting this means we have a problem in the ToPA
	 * allocation code.
	 */
	WARN_ON_ONCE(1);

	return NULL;

found:
	/*
	 * Indicates a problem in the ToPA allocation code.
	 */
	if (WARN_ON_ONCE(topa->last == -1))
		return NULL;

	tp = topa_to_page(topa);
	cur_pg = PFN_DOWN(topa->offset);
	if (topa->z_count) {
		z_pg = TOPA_ENTRY_PAGES(topa, 0) * (topa->z_count + 1);
		start_idx = topa->z_count + 1;
	}

	/*
	 * Multiple entries at the beginning of the table have the same size,
	 * ideally all of them; if @pg falls there, the search is done.
	 */
	if (pg >= cur_pg && pg < cur_pg + z_pg) {
		idx = (pg - cur_pg) / TOPA_ENTRY_PAGES(topa, 0);
		return &tp->table[idx];
	}

	/*
	 * Otherwise, slow path: iterate through the remaining entries.
	 */
	for (idx = start_idx, cur_pg += z_pg; idx < topa->last; idx++) {
		if (cur_pg + TOPA_ENTRY_PAGES(topa, idx) > pg)
			return &tp->table[idx];

		cur_pg += TOPA_ENTRY_PAGES(topa, idx);
	}

	/*
	 * Means we couldn't find a ToPA entry in the table that does match.
	 */
	WARN_ON_ONCE(1);

	return NULL;
}

static struct topa_entry *
pt_topa_prev_entry(struct pt_buffer *buf, struct topa_entry *te)
{
	unsigned long table = (unsigned long)te & ~(PAGE_SIZE - 1);
	struct topa_page *tp;
	struct topa *topa;

	tp = (struct topa_page *)table;
	if (tp->table != te)
		return --te;

	topa = &tp->topa;
	if (topa == buf->first)
		topa = buf->last;
	else
		topa = list_prev_entry(topa, list);

	tp = topa_to_page(topa);

	return &tp->table[topa->last - 1];
}

/**
 * pt_buffer_reset_markers() - place interrupt and stop bits in the buffer
 * @buf:	PT buffer.
 * @handle:	Current output handle.
 *
 * Place INT and STOP marks to prevent overwriting old data that the consumer
 * hasn't yet collected and waking up the consumer after a certain fraction of
 * the buffer has filled up. Only needed and sensible for non-snapshot counters.
 *
 * This obviously relies on buf::head to figure out buffer markers, so it has
 * to be called after pt_buffer_reset_offsets() and before the hardware tracing
 * is enabled.
 */
static int pt_buffer_reset_markers(struct pt_buffer *buf,
				   struct perf_output_handle *handle)

{
	unsigned long head = local64_read(&buf->head);
	unsigned long idx, npages, wakeup;

	if (buf->single)
		return 0;

	/* can't stop in the middle of an output region */
	if (buf->output_off + handle->size + 1 < pt_buffer_region_size(buf)) {
		perf_aux_output_flag(handle, PERF_AUX_FLAG_TRUNCATED);
		return -EINVAL;
	}


	/* single entry ToPA is handled by marking all regions STOP=1 INT=1 */
	if (!intel_pt_validate_hw_cap(PT_CAP_topa_multiple_entries))
		return 0;

	/* clear STOP and INT from current entry */
	if (buf->stop_te) {
		buf->stop_te->stop = 0;
		buf->stop_te->intr = 0;
	}

	if (buf->intr_te)
		buf->intr_te->intr = 0;

	/* how many pages till the STOP marker */
	npages = handle->size >> PAGE_SHIFT;

	/* if it's on a page boundary, fill up one more page */
	if (!offset_in_page(head + handle->size + 1))
		npages++;

	idx = (head >> PAGE_SHIFT) + npages;
	idx &= buf->nr_pages - 1;

	if (idx != buf->stop_pos) {
		buf->stop_pos = idx;
		buf->stop_te = pt_topa_entry_for_page(buf, idx);
		buf->stop_te = pt_topa_prev_entry(buf, buf->stop_te);
	}

	wakeup = handle->wakeup >> PAGE_SHIFT;

	/* in the worst case, wake up the consumer one page before hard stop */
	idx = (head >> PAGE_SHIFT) + npages - 1;
	if (idx > wakeup)
		idx = wakeup;

	idx &= buf->nr_pages - 1;
	if (idx != buf->intr_pos) {
		buf->intr_pos = idx;
		buf->intr_te = pt_topa_entry_for_page(buf, idx);
		buf->intr_te = pt_topa_prev_entry(buf, buf->intr_te);
	}

	buf->stop_te->stop = 1;
	buf->stop_te->intr = 1;
	buf->intr_te->intr = 1;

	return 0;
}

/**
 * pt_buffer_reset_offsets() - adjust buffer's write pointers from aux_head
 * @buf:	PT buffer.
 * @head:	Write pointer (aux_head) from AUX buffer.
 *
 * Find the ToPA table and entry corresponding to given @head and set buffer's
 * "current" pointers accordingly. This is done after we have obtained the
 * current aux_head position from a successful call to perf_aux_output_begin()
 * to make sure the hardware is writing to the right place.
 *
 * This function modifies buf::{cur,cur_idx,output_off} that will be programmed
 * into PT msrs when the tracing is enabled and buf::head and buf::data_size,
 * which are used to determine INT and STOP markers' locations by a subsequent
 * call to pt_buffer_reset_markers().
 */
static void pt_buffer_reset_offsets(struct pt_buffer *buf, unsigned long head)
{
	struct topa_page *cur_tp;
	struct topa_entry *te;
	int pg;

	if (buf->snapshot)
		head &= (buf->nr_pages << PAGE_SHIFT) - 1;

	if (!buf->single) {
		pg = (head >> PAGE_SHIFT) & (buf->nr_pages - 1);
		te = pt_topa_entry_for_page(buf, pg);

		cur_tp = topa_entry_to_page(te);
		buf->cur = &cur_tp->topa;
		buf->cur_idx = te - TOPA_ENTRY(buf->cur, 0);
		buf->output_off = head & (pt_buffer_region_size(buf) - 1);
	} else {
		buf->output_off = head;
	}

	local64_set(&buf->head, head);
	local_set(&buf->data_size, 0);
}

/**
 * pt_buffer_fini_topa() - deallocate ToPA structure of a buffer
 * @buf:	PT buffer.
 */
static void pt_buffer_fini_topa(struct pt_buffer *buf)
{
	struct topa *topa, *iter;

	if (buf->single)
		return;

	list_for_each_entry_safe(topa, iter, &buf->tables, list) {
		/*
		 * right now, this is in free_aux() path only, so
		 * no need to unlink this table from the list
		 */
		topa_free(topa);
	}
}

/**
 * pt_buffer_init_topa() - initialize ToPA table for pt buffer
 * @buf:	PT buffer.
 * @size:	Total size of all regions within this ToPA.
 * @gfp:	Allocation flags.
 */
static int pt_buffer_init_topa(struct pt_buffer *buf, int cpu,
			       unsigned long nr_pages, gfp_t gfp)
{
	struct topa *topa;
	int err;

	topa = topa_alloc(cpu, gfp);
	if (!topa)
		return -ENOMEM;

	topa_insert_table(buf, topa);

	while (buf->nr_pages < nr_pages) {
		err = topa_insert_pages(buf, cpu, gfp);
		if (err) {
			pt_buffer_fini_topa(buf);
			return -ENOMEM;
		}
	}

	/* link last table to the first one, unless we're double buffering */
	if (intel_pt_validate_hw_cap(PT_CAP_topa_multiple_entries)) {
		TOPA_ENTRY(buf->last, -1)->base = topa_pfn(buf->first);
		TOPA_ENTRY(buf->last, -1)->end = 1;
	}

	pt_topa_dump(buf);
	return 0;
}

static int pt_buffer_try_single(struct pt_buffer *buf, int nr_pages)
{
	struct page *p = virt_to_page(buf->data_pages[0]);
	int ret = -ENOTSUPP, order = 0;

	/*
	 * We can use single range output mode
	 * + in snapshot mode, where we don't need interrupts;
	 * + if the hardware supports it;
	 * + if the entire buffer is one contiguous allocation.
	 */
	if (!buf->snapshot)
		goto out;

	if (!intel_pt_validate_hw_cap(PT_CAP_single_range_output))
		goto out;

	if (PagePrivate(p))
		order = page_private(p);

	if (1 << order != nr_pages)
		goto out;

	buf->single = true;
	buf->nr_pages = nr_pages;
	ret = 0;
out:
	return ret;
}

/**
 * pt_buffer_setup_aux() - set up topa tables for a PT buffer
 * @cpu:	Cpu on which to allocate, -1 means current.
 * @pages:	Array of pointers to buffer pages passed from perf core.
 * @nr_pages:	Number of pages in the buffer.
 * @snapshot:	If this is a snapshot/overwrite counter.
 *
 * This is a pmu::setup_aux callback that sets up ToPA tables and all the
 * bookkeeping for an AUX buffer.
 *
 * Return:	Our private PT buffer structure.
 */
static void *
pt_buffer_setup_aux(struct perf_event *event, void **pages,
		    int nr_pages, bool snapshot)
{
	struct pt_buffer *buf;
	int node, ret, cpu = event->cpu;

	if (!nr_pages)
		return NULL;

	/*
	 * Only support AUX sampling in snapshot mode, where we don't
	 * generate NMIs.
	 */
	if (event->attr.aux_sample_size && !snapshot)
		return NULL;

	if (cpu == -1)
		cpu = raw_smp_processor_id();
	node = cpu_to_node(cpu);

	buf = kzalloc_node(sizeof(struct pt_buffer), GFP_KERNEL, node);
	if (!buf)
		return NULL;

	buf->snapshot = snapshot;
	buf->data_pages = pages;
	buf->stop_pos = -1;
	buf->intr_pos = -1;

	INIT_LIST_HEAD(&buf->tables);

	ret = pt_buffer_try_single(buf, nr_pages);
	if (!ret)
		return buf;

	ret = pt_buffer_init_topa(buf, cpu, nr_pages, GFP_KERNEL);
	if (ret) {
		kfree(buf);
		return NULL;
	}

	return buf;
}

/**
 * pt_buffer_free_aux() - perf AUX deallocation path callback
 * @data:	PT buffer.
 */
static void pt_buffer_free_aux(void *data)
{
	struct pt_buffer *buf = data;

	pt_buffer_fini_topa(buf);
	kfree(buf);
}

static int pt_addr_filters_init(struct perf_event *event)
{
	struct pt_filters *filters;
	int node = event->cpu == -1 ? -1 : cpu_to_node(event->cpu);

	if (!intel_pt_validate_hw_cap(PT_CAP_num_address_ranges))
		return 0;

	filters = kzalloc_node(sizeof(struct pt_filters), GFP_KERNEL, node);
	if (!filters)
		return -ENOMEM;

	if (event->parent)
		memcpy(filters, event->parent->hw.addr_filters,
		       sizeof(*filters));

	event->hw.addr_filters = filters;

	return 0;
}

static void pt_addr_filters_fini(struct perf_event *event)
{
	kfree(event->hw.addr_filters);
	event->hw.addr_filters = NULL;
}

#ifdef CONFIG_X86_64
/* Clamp to a canonical address greater-than-or-equal-to the address given */
static u64 clamp_to_ge_canonical_addr(u64 vaddr, u8 vaddr_bits)
{
	return __is_canonical_address(vaddr, vaddr_bits) ?
	       vaddr :
	       -BIT_ULL(vaddr_bits - 1);
}

/* Clamp to a canonical address less-than-or-equal-to the address given */
static u64 clamp_to_le_canonical_addr(u64 vaddr, u8 vaddr_bits)
{
	return __is_canonical_address(vaddr, vaddr_bits) ?
	       vaddr :
	       BIT_ULL(vaddr_bits - 1) - 1;
}
#else
#define clamp_to_ge_canonical_addr(x, y) (x)
#define clamp_to_le_canonical_addr(x, y) (x)
#endif

static int pt_event_addr_filters_validate(struct list_head *filters)
{
	struct perf_addr_filter *filter;
	int range = 0;

	list_for_each_entry(filter, filters, entry) {
		/*
		 * PT doesn't support single address triggers and
		 * 'start' filters.
		 */
		if (!filter->size ||
		    filter->action == PERF_ADDR_FILTER_ACTION_START)
			return -EOPNOTSUPP;

		if (++range > intel_pt_validate_hw_cap(PT_CAP_num_address_ranges))
			return -EOPNOTSUPP;
	}

	return 0;
}

static void pt_event_addr_filters_sync(struct perf_event *event)
{
	struct perf_addr_filters_head *head = perf_event_addr_filters(event);
	unsigned long msr_a, msr_b;
	struct perf_addr_filter_range *fr = event->addr_filter_ranges;
	struct pt_filters *filters = event->hw.addr_filters;
	struct perf_addr_filter *filter;
	int range = 0;

	if (!filters)
		return;

	list_for_each_entry(filter, &head->list, entry) {
		if (filter->path.dentry && !fr[range].start) {
			msr_a = msr_b = 0;
		} else {
			unsigned long n = fr[range].size - 1;
			unsigned long a = fr[range].start;
			unsigned long b;

			if (a > ULONG_MAX - n)
				b = ULONG_MAX;
			else
				b = a + n;
			/*
			 * Apply the offset. 64-bit addresses written to the
			 * MSRs must be canonical, but the range can encompass
			 * non-canonical addresses. Since software cannot
			 * execute at non-canonical addresses, adjusting to
			 * canonical addresses does not affect the result of the
			 * address filter.
			 */
			msr_a = clamp_to_ge_canonical_addr(a, boot_cpu_data.x86_virt_bits);
			msr_b = clamp_to_le_canonical_addr(b, boot_cpu_data.x86_virt_bits);
			if (msr_b < msr_a)
				msr_a = msr_b = 0;
		}

		filters->filter[range].msr_a  = msr_a;
		filters->filter[range].msr_b  = msr_b;
		if (filter->action == PERF_ADDR_FILTER_ACTION_FILTER)
			filters->filter[range].config = 1;
		else
			filters->filter[range].config = 2;
		range++;
	}

	filters->nr_filters = range;
}

/**
 * intel_pt_interrupt() - PT PMI handler
 */
void intel_pt_interrupt(void)
{
	struct pt *pt = this_cpu_ptr(&pt_ctx);
	struct pt_buffer *buf;
	struct perf_event *event = pt->handle.event;

	/*
	 * There may be a dangling PT bit in the interrupt status register
	 * after PT has been disabled by pt_event_stop(). Make sure we don't
	 * do anything (particularly, re-enable) for this event here.
	 */
	if (!READ_ONCE(pt->handle_nmi))
		return;

	if (!event)
		return;

	pt_config_stop(event);

	buf = perf_get_aux(&pt->handle);
	if (!buf)
		return;

	pt_read_offset(buf);

	pt_handle_status(pt);

	pt_update_head(pt);

	perf_aux_output_end(&pt->handle, local_xchg(&buf->data_size, 0));

	if (!event->hw.state) {
		int ret;

		buf = perf_aux_output_begin(&pt->handle, event);
		if (!buf) {
			event->hw.state = PERF_HES_STOPPED;
			return;
		}

		pt_buffer_reset_offsets(buf, pt->handle.head);
		/* snapshot counters don't use PMI, so it's safe */
		ret = pt_buffer_reset_markers(buf, &pt->handle);
		if (ret) {
			perf_aux_output_end(&pt->handle, 0);
			return;
		}

		pt_config_buffer(buf);
		pt_config_start(event);
	}
}

void intel_pt_handle_vmx(int on)
{
	struct pt *pt = this_cpu_ptr(&pt_ctx);
	struct perf_event *event;
	unsigned long flags;

	/* PT plays nice with VMX, do nothing */
	if (pt_pmu.vmx)
		return;

	/*
	 * VMXON will clear RTIT_CTL.TraceEn; we need to make
	 * sure to not try to set it while VMX is on. Disable
	 * interrupts to avoid racing with pmu callbacks;
	 * concurrent PMI should be handled fine.
	 */
	local_irq_save(flags);
	WRITE_ONCE(pt->vmx_on, on);

	/*
	 * If an AUX transaction is in progress, it will contain
	 * gap(s), so flag it PARTIAL to inform the user.
	 */
	event = pt->handle.event;
	if (event)
		perf_aux_output_flag(&pt->handle,
		                     PERF_AUX_FLAG_PARTIAL);

	/* Turn PTs back on */
	if (!on && event)
		wrmsrl(MSR_IA32_RTIT_CTL, event->hw.config);

	local_irq_restore(flags);
}
EXPORT_SYMBOL_GPL(intel_pt_handle_vmx);

/*
 * PMU callbacks
 */

static void pt_event_start(struct perf_event *event, int mode)
{
	struct hw_perf_event *hwc = &event->hw;
	struct pt *pt = this_cpu_ptr(&pt_ctx);
	struct pt_buffer *buf;

	buf = perf_aux_output_begin(&pt->handle, event);
	if (!buf)
		goto fail_stop;

	pt_buffer_reset_offsets(buf, pt->handle.head);
	if (!buf->snapshot) {
		if (pt_buffer_reset_markers(buf, &pt->handle))
			goto fail_end_stop;
	}

	WRITE_ONCE(pt->handle_nmi, 1);
	hwc->state = 0;

	pt_config_buffer(buf);
	pt_config(event);

	return;

fail_end_stop:
	perf_aux_output_end(&pt->handle, 0);
fail_stop:
	hwc->state = PERF_HES_STOPPED;
}

static void pt_event_stop(struct perf_event *event, int mode)
{
	struct pt *pt = this_cpu_ptr(&pt_ctx);

	/*
	 * Protect against the PMI racing with disabling wrmsr,
	 * see comment in intel_pt_interrupt().
	 */
	WRITE_ONCE(pt->handle_nmi, 0);

	pt_config_stop(event);

	if (event->hw.state == PERF_HES_STOPPED)
		return;

	event->hw.state = PERF_HES_STOPPED;

	if (mode & PERF_EF_UPDATE) {
		struct pt_buffer *buf = perf_get_aux(&pt->handle);

		if (!buf)
			return;

		if (WARN_ON_ONCE(pt->handle.event != event))
			return;

		pt_read_offset(buf);

		pt_handle_status(pt);

		pt_update_head(pt);

		if (buf->snapshot)
			pt->handle.head =
				local_xchg(&buf->data_size,
					   buf->nr_pages << PAGE_SHIFT);
		perf_aux_output_end(&pt->handle, local_xchg(&buf->data_size, 0));
	}
}

static long pt_event_snapshot_aux(struct perf_event *event,
				  struct perf_output_handle *handle,
				  unsigned long size)
{
	struct pt *pt = this_cpu_ptr(&pt_ctx);
	struct pt_buffer *buf = perf_get_aux(&pt->handle);
	unsigned long from = 0, to;
	long ret;

	if (WARN_ON_ONCE(!buf))
		return 0;

	/*
	 * Sampling is only allowed on snapshot events;
	 * see pt_buffer_setup_aux().
	 */
	if (WARN_ON_ONCE(!buf->snapshot))
		return 0;

	/*
	 * Here, handle_nmi tells us if the tracing is on
	 */
	if (READ_ONCE(pt->handle_nmi))
		pt_config_stop(event);

	pt_read_offset(buf);
	pt_update_head(pt);

	to = local_read(&buf->data_size);
	if (to < size)
		from = buf->nr_pages << PAGE_SHIFT;
	from += to - size;

	ret = perf_output_copy_aux(&pt->handle, handle, from, to);

	/*
	 * If the tracing was on when we turned up, restart it.
	 * Compiler barrier not needed as we couldn't have been
	 * preempted by anything that touches pt->handle_nmi.
	 */
	if (pt->handle_nmi)
		pt_config_start(event);

	return ret;
}

static void pt_event_del(struct perf_event *event, int mode)
{
	pt_event_stop(event, PERF_EF_UPDATE);
}

static int pt_event_add(struct perf_event *event, int mode)
{
	struct pt *pt = this_cpu_ptr(&pt_ctx);
	struct hw_perf_event *hwc = &event->hw;
	int ret = -EBUSY;

	if (pt->handle.event)
		goto fail;

	if (mode & PERF_EF_START) {
		pt_event_start(event, 0);
		ret = -EINVAL;
		if (hwc->state == PERF_HES_STOPPED)
			goto fail;
	} else {
		hwc->state = PERF_HES_STOPPED;
	}

	ret = 0;
fail:

	return ret;
}

static void pt_event_read(struct perf_event *event)
{
}

static void pt_event_destroy(struct perf_event *event)
{
	pt_addr_filters_fini(event);
	x86_del_exclusive(x86_lbr_exclusive_pt);
}

static int pt_event_init(struct perf_event *event)
{
	if (event->attr.type != pt_pmu.pmu.type)
		return -ENOENT;

	if (!pt_event_valid(event))
		return -EINVAL;

	if (x86_add_exclusive(x86_lbr_exclusive_pt))
		return -EBUSY;

	if (pt_addr_filters_init(event)) {
		x86_del_exclusive(x86_lbr_exclusive_pt);
		return -ENOMEM;
	}

	event->destroy = pt_event_destroy;

	return 0;
}

void cpu_emergency_stop_pt(void)
{
	struct pt *pt = this_cpu_ptr(&pt_ctx);

	if (pt->handle.event)
		pt_event_stop(pt->handle.event, PERF_EF_UPDATE);
}

int is_intel_pt_event(struct perf_event *event)
{
	return event->pmu == &pt_pmu.pmu;
}

static __init int pt_init(void)
{
	int ret, cpu, prior_warn = 0;

	BUILD_BUG_ON(sizeof(struct topa) > PAGE_SIZE);

	if (!boot_cpu_has(X86_FEATURE_INTEL_PT))
		return -ENODEV;

	cpus_read_lock();
	for_each_online_cpu(cpu) {
		u64 ctl;

		ret = rdmsrl_safe_on_cpu(cpu, MSR_IA32_RTIT_CTL, &ctl);
		if (!ret && (ctl & RTIT_CTL_TRACEEN))
			prior_warn++;
	}
	cpus_read_unlock();

	if (prior_warn) {
		x86_add_exclusive(x86_lbr_exclusive_pt);
		pr_warn("PT is enabled at boot time, doing nothing\n");

		return -EBUSY;
	}

	ret = pt_pmu_hw_init();
	if (ret)
		return ret;

	if (!intel_pt_validate_hw_cap(PT_CAP_topa_output)) {
		pr_warn("ToPA output is not supported on this CPU\n");
		return -ENODEV;
	}

	if (!intel_pt_validate_hw_cap(PT_CAP_topa_multiple_entries))
		pt_pmu.pmu.capabilities = PERF_PMU_CAP_AUX_NO_SG;

	pt_pmu.pmu.capabilities	|= PERF_PMU_CAP_EXCLUSIVE | PERF_PMU_CAP_ITRACE;
	pt_pmu.pmu.attr_groups		 = pt_attr_groups;
	pt_pmu.pmu.task_ctx_nr		 = perf_sw_context;
	pt_pmu.pmu.event_init		 = pt_event_init;
	pt_pmu.pmu.add			 = pt_event_add;
	pt_pmu.pmu.del			 = pt_event_del;
	pt_pmu.pmu.start		 = pt_event_start;
	pt_pmu.pmu.stop			 = pt_event_stop;
	pt_pmu.pmu.snapshot_aux		 = pt_event_snapshot_aux;
	pt_pmu.pmu.read			 = pt_event_read;
	pt_pmu.pmu.setup_aux		 = pt_buffer_setup_aux;
	pt_pmu.pmu.free_aux		 = pt_buffer_free_aux;
	pt_pmu.pmu.addr_filters_sync     = pt_event_addr_filters_sync;
	pt_pmu.pmu.addr_filters_validate = pt_event_addr_filters_validate;
	pt_pmu.pmu.nr_addr_filters       =
		intel_pt_validate_hw_cap(PT_CAP_num_address_ranges);

	ret = perf_pmu_register(&pt_pmu.pmu, "intel_pt", -1);

	return ret;
}
arch_initcall(pt_init);

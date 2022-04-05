/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Intel(R) Processor Trace PMU driver for perf
 * Copyright (c) 2013-2014, Intel Corporation.
 *
 * Intel PT is specified in the Intel Architecture Instruction Set Extensions
 * Programming Reference:
 * http://software.intel.com/en-us/intel-isa-extensions
 */

#ifndef __INTEL_PT_H__
#define __INTEL_PT_H__

/*
 * Single-entry ToPA: when this close to region boundary, switch
 * buffers to avoid losing data.
 */
#define TOPA_PMI_MARGIN 512

#define TOPA_SHIFT 12

static inline unsigned int sizes(unsigned int tsz)
{
	return 1 << (tsz + TOPA_SHIFT);
};

struct topa_entry {
	u64	end	: 1;
	u64	rsvd0	: 1;
	u64	intr	: 1;
	u64	rsvd1	: 1;
	u64	stop	: 1;
	u64	rsvd2	: 1;
	u64	size	: 4;
	u64	rsvd3	: 2;
	u64	base	: 36;
	u64	rsvd4	: 16;
};

/* TSC to Core Crystal Clock Ratio */
#define CPUID_TSC_LEAF		0x15

struct pt_pmu {
	struct pmu		pmu;
	u32			caps[PT_CPUID_REGS_NUM * PT_CPUID_LEAVES];
	bool			vmx;
	bool			branch_en_always_on;
	unsigned long		max_nonturbo_ratio;
	unsigned int		tsc_art_num;
	unsigned int		tsc_art_den;
};

/**
 * struct pt_buffer - buffer configuration; one buffer per task_struct or
 *		cpu, depending on perf event configuration
 * @tables:	list of ToPA tables in this buffer
 * @first:	shorthand for first topa table
 * @last:	shorthand for last topa table
 * @cur:	current topa table
 * @nr_pages:	buffer size in pages
 * @cur_idx:	current output region's index within @cur table
 * @output_off:	offset within the current output region
 * @data_size:	running total of the amount of data in this buffer
 * @lost:	if data was lost/truncated
 * @head:	logical write offset inside the buffer
 * @snapshot:	if this is for a snapshot/overwrite counter
 * @stop_pos:	STOP topa entry index
 * @intr_pos:	INT topa entry index
 * @stop_te:	STOP topa entry pointer
 * @intr_te:	INT topa entry pointer
 * @data_pages:	array of pages from perf
 * @topa_index:	table of topa entries indexed by page offset
 */
struct pt_buffer {
	struct list_head	tables;
	struct topa		*first, *last, *cur;
	unsigned int		cur_idx;
	size_t			output_off;
	unsigned long		nr_pages;
	local_t			data_size;
	local64_t		head;
	bool			snapshot;
	long			stop_pos, intr_pos;
	struct topa_entry	*stop_te, *intr_te;
	void			**data_pages;
};

#define PT_FILTERS_NUM	4

/**
 * struct pt_filter - IP range filter configuration
 * @msr_a:	range start, goes to RTIT_ADDRn_A
 * @msr_b:	range end, goes to RTIT_ADDRn_B
 * @config:	4-bit field in RTIT_CTL
 */
struct pt_filter {
	unsigned long	msr_a;
	unsigned long	msr_b;
	unsigned long	config;
};

/**
 * struct pt_filters - IP range filtering context
 * @filter:	filters defined for this context
 * @nr_filters:	number of defined filters in the @filter array
 */
struct pt_filters {
	struct pt_filter	filter[PT_FILTERS_NUM];
	unsigned int		nr_filters;
};

/**
 * struct pt - per-cpu pt context
 * @handle:	perf output handle
 * @filters:		last configured filters
 * @handle_nmi:	do handle PT PMI on this cpu, there's an active event
 * @vmx_on:	1 if VMX is ON on this cpu
 */
struct pt {
	struct perf_output_handle handle;
	struct pt_filters	filters;
	int			handle_nmi;
	int			vmx_on;
};

#endif /* __INTEL_PT_H__ */

/*
 * Intel(R) Processor Trace PMU driver for perf
 * Copyright (c) 2013-2014, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
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

#define PT_CPUID_LEAVES		2
#define PT_CPUID_REGS_NUM	4 /* number of regsters (eax, ebx, ecx, edx) */

enum pt_capabilities {
	PT_CAP_max_subleaf = 0,
	PT_CAP_cr3_filtering,
	PT_CAP_psb_cyc,
	PT_CAP_mtc,
	PT_CAP_topa_output,
	PT_CAP_topa_multiple_entries,
	PT_CAP_single_range_output,
	PT_CAP_payloads_lip,
	PT_CAP_mtc_periods,
	PT_CAP_cycle_thresholds,
	PT_CAP_psb_periods,
};

struct pt_pmu {
	struct pmu		pmu;
	u32			caps[PT_CPUID_REGS_NUM * PT_CPUID_LEAVES];
};

/**
 * struct pt_buffer - buffer configuration; one buffer per task_struct or
 *		cpu, depending on perf event configuration
 * @cpu:	cpu for per-cpu allocation
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
 * @stop_pos:	STOP topa entry in the buffer
 * @intr_pos:	INT topa entry in the buffer
 * @data_pages:	array of pages from perf
 * @topa_index:	table of topa entries indexed by page offset
 */
struct pt_buffer {
	int			cpu;
	struct list_head	tables;
	struct topa		*first, *last, *cur;
	unsigned int		cur_idx;
	size_t			output_off;
	unsigned long		nr_pages;
	local_t			data_size;
	local_t			lost;
	local64_t		head;
	bool			snapshot;
	unsigned long		stop_pos, intr_pos;
	void			**data_pages;
	struct topa_entry	*topa_index[0];
};

/**
 * struct pt - per-cpu pt context
 * @handle:	perf output handle
 * @handle_nmi:	do handle PT PMI on this cpu, there's an active event
 */
struct pt {
	struct perf_output_handle handle;
	int			handle_nmi;
};

#endif /* __INTEL_PT_H__ */

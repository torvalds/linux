// SPDX-License-Identifier: GPL-2.0
/*
 * This driver enables Trace Buffer Extension (TRBE) as a per-cpu coresight
 * sink device could then pair with an appropriate per-cpu coresight source
 * device (ETE) thus generating required trace data. Trace can be enabled
 * via the perf framework.
 *
 * The AUX buffer handling is inspired from Arm SPE PMU driver.
 *
 * Copyright (C) 2020 ARM Ltd.
 *
 * Author: Anshuman Khandual <anshuman.khandual@arm.com>
 */
#define DRVNAME "arm_trbe"

#define pr_fmt(fmt) DRVNAME ": " fmt

#include <asm/barrier.h>
#include <asm/cpufeature.h>

#include "coresight-self-hosted-trace.h"
#include "coresight-trbe.h"

#define PERF_IDX2OFF(idx, buf) ((idx) % ((buf)->nr_pages << PAGE_SHIFT))

/*
 * A padding packet that will help the user space tools
 * in skipping relevant sections in the captured trace
 * data which could not be decoded. TRBE doesn't support
 * formatting the trace data, unlike the legacy CoreSight
 * sinks and thus we use ETE trace packets to pad the
 * sections of the buffer.
 */
#define ETE_IGNORE_PACKET		0x70

/*
 * Minimum amount of meaningful trace will contain:
 * A-Sync, Trace Info, Trace On, Address, Atom.
 * This is about 44bytes of ETE trace. To be on
 * the safer side, we assume 64bytes is the minimum
 * space required for a meaningful session, before
 * we hit a "WRAP" event.
 */
#define TRBE_TRACE_MIN_BUF_SIZE		64

enum trbe_fault_action {
	TRBE_FAULT_ACT_WRAP,
	TRBE_FAULT_ACT_SPURIOUS,
	TRBE_FAULT_ACT_FATAL,
};

struct trbe_buf {
	/*
	 * Even though trbe_base represents vmap()
	 * mapped allocated buffer's start address,
	 * it's being as unsigned long for various
	 * arithmetic and comparision operations &
	 * also to be consistent with trbe_write &
	 * trbe_limit sibling pointers.
	 */
	unsigned long trbe_base;
	/* The base programmed into the TRBE */
	unsigned long trbe_hw_base;
	unsigned long trbe_limit;
	unsigned long trbe_write;
	int nr_pages;
	void **pages;
	bool snapshot;
	struct trbe_cpudata *cpudata;
};

/*
 * TRBE erratum list
 *
 * The errata are defined in arm64 generic cpu_errata framework.
 * Since the errata work arounds could be applied individually
 * to the affected CPUs inside the TRBE driver, we need to know if
 * a given CPU is affected by the erratum. Unlike the other erratum
 * work arounds, TRBE driver needs to check multiple times during
 * a trace session. Thus we need a quicker access to per-CPU
 * errata and not issue costly this_cpu_has_cap() everytime.
 * We keep a set of the affected errata in trbe_cpudata, per TRBE.
 *
 * We rely on the corresponding cpucaps to be defined for a given
 * TRBE erratum. We map the given cpucap into a TRBE internal number
 * to make the tracking of the errata lean.
 *
 * This helps in :
 *   - Not duplicating the detection logic
 *   - Streamlined detection of erratum across the system
 */
#define TRBE_WORKAROUND_OVERWRITE_FILL_MODE	0
#define TRBE_WORKAROUND_WRITE_OUT_OF_RANGE	1
#define TRBE_NEEDS_DRAIN_AFTER_DISABLE		2
#define TRBE_NEEDS_CTXT_SYNC_AFTER_ENABLE	3
#define TRBE_IS_BROKEN				4

static int trbe_errata_cpucaps[] = {
	[TRBE_WORKAROUND_OVERWRITE_FILL_MODE] = ARM64_WORKAROUND_TRBE_OVERWRITE_FILL_MODE,
	[TRBE_WORKAROUND_WRITE_OUT_OF_RANGE] = ARM64_WORKAROUND_TRBE_WRITE_OUT_OF_RANGE,
	[TRBE_NEEDS_DRAIN_AFTER_DISABLE] = ARM64_WORKAROUND_2064142,
	[TRBE_NEEDS_CTXT_SYNC_AFTER_ENABLE] = ARM64_WORKAROUND_2038923,
	[TRBE_IS_BROKEN] = ARM64_WORKAROUND_1902691,
	-1,		/* Sentinel, must be the last entry */
};

/* The total number of listed errata in trbe_errata_cpucaps */
#define TRBE_ERRATA_MAX			(ARRAY_SIZE(trbe_errata_cpucaps) - 1)

/*
 * Safe limit for the number of bytes that may be overwritten
 * when ARM64_WORKAROUND_TRBE_OVERWRITE_FILL_MODE is triggered.
 */
#define TRBE_WORKAROUND_OVERWRITE_FILL_MODE_SKIP_BYTES	256

/*
 * struct trbe_cpudata: TRBE instance specific data
 * @trbe_flag		- TRBE dirty/access flag support
 * @trbe_hw_align	- Actual TRBE alignment required for TRBPTR_EL1.
 * @trbe_align		- Software alignment used for the TRBPTR_EL1.
 * @cpu			- CPU this TRBE belongs to.
 * @mode		- Mode of current operation. (perf/disabled)
 * @drvdata		- TRBE specific drvdata
 * @errata		- Bit map for the errata on this TRBE.
 */
struct trbe_cpudata {
	bool trbe_flag;
	u64 trbe_hw_align;
	u64 trbe_align;
	int cpu;
	enum cs_mode mode;
	struct trbe_buf *buf;
	struct trbe_drvdata *drvdata;
	DECLARE_BITMAP(errata, TRBE_ERRATA_MAX);
};

struct trbe_drvdata {
	struct trbe_cpudata __percpu *cpudata;
	struct perf_output_handle * __percpu *handle;
	struct hlist_node hotplug_node;
	int irq;
	cpumask_t supported_cpus;
	enum cpuhp_state trbe_online;
	struct platform_device *pdev;
};

static void trbe_check_errata(struct trbe_cpudata *cpudata)
{
	int i;

	for (i = 0; i < TRBE_ERRATA_MAX; i++) {
		int cap = trbe_errata_cpucaps[i];

		if (WARN_ON_ONCE(cap < 0))
			return;
		if (this_cpu_has_cap(cap))
			set_bit(i, cpudata->errata);
	}
}

static inline bool trbe_has_erratum(struct trbe_cpudata *cpudata, int i)
{
	return (i < TRBE_ERRATA_MAX) && test_bit(i, cpudata->errata);
}

static inline bool trbe_may_overwrite_in_fill_mode(struct trbe_cpudata *cpudata)
{
	return trbe_has_erratum(cpudata, TRBE_WORKAROUND_OVERWRITE_FILL_MODE);
}

static inline bool trbe_may_write_out_of_range(struct trbe_cpudata *cpudata)
{
	return trbe_has_erratum(cpudata, TRBE_WORKAROUND_WRITE_OUT_OF_RANGE);
}

static inline bool trbe_needs_drain_after_disable(struct trbe_cpudata *cpudata)
{
	/*
	 * Errata affected TRBE implementation will need TSB CSYNC and
	 * DSB in order to prevent subsequent writes into certain TRBE
	 * system registers from being ignored and not effected.
	 */
	return trbe_has_erratum(cpudata, TRBE_NEEDS_DRAIN_AFTER_DISABLE);
}

static inline bool trbe_needs_ctxt_sync_after_enable(struct trbe_cpudata *cpudata)
{
	/*
	 * Errata affected TRBE implementation will need an additional
	 * context synchronization in order to prevent an inconsistent
	 * TRBE prohibited region view on the CPU which could possibly
	 * corrupt the TRBE buffer or the TRBE state.
	 */
	return trbe_has_erratum(cpudata, TRBE_NEEDS_CTXT_SYNC_AFTER_ENABLE);
}

static inline bool trbe_is_broken(struct trbe_cpudata *cpudata)
{
	return trbe_has_erratum(cpudata, TRBE_IS_BROKEN);
}

static int trbe_alloc_node(struct perf_event *event)
{
	if (event->cpu == -1)
		return NUMA_NO_NODE;
	return cpu_to_node(event->cpu);
}

static inline void trbe_drain_buffer(void)
{
	tsb_csync();
	dsb(nsh);
}

static inline void set_trbe_enabled(struct trbe_cpudata *cpudata, u64 trblimitr)
{
	/*
	 * Enable the TRBE without clearing LIMITPTR which
	 * might be required for fetching the buffer limits.
	 */
	trblimitr |= TRBLIMITR_EL1_E;
	write_sysreg_s(trblimitr, SYS_TRBLIMITR_EL1);

	/* Synchronize the TRBE enable event */
	isb();

	if (trbe_needs_ctxt_sync_after_enable(cpudata))
		isb();
}

static inline void set_trbe_disabled(struct trbe_cpudata *cpudata)
{
	u64 trblimitr = read_sysreg_s(SYS_TRBLIMITR_EL1);

	/*
	 * Disable the TRBE without clearing LIMITPTR which
	 * might be required for fetching the buffer limits.
	 */
	trblimitr &= ~TRBLIMITR_EL1_E;
	write_sysreg_s(trblimitr, SYS_TRBLIMITR_EL1);

	if (trbe_needs_drain_after_disable(cpudata))
		trbe_drain_buffer();
	isb();
}

static void trbe_drain_and_disable_local(struct trbe_cpudata *cpudata)
{
	trbe_drain_buffer();
	set_trbe_disabled(cpudata);
}

static void trbe_reset_local(struct trbe_cpudata *cpudata)
{
	trbe_drain_and_disable_local(cpudata);
	write_sysreg_s(0, SYS_TRBLIMITR_EL1);
	write_sysreg_s(0, SYS_TRBPTR_EL1);
	write_sysreg_s(0, SYS_TRBBASER_EL1);
	write_sysreg_s(0, SYS_TRBSR_EL1);
}

static void trbe_report_wrap_event(struct perf_output_handle *handle)
{
	/*
	 * Mark the buffer to indicate that there was a WRAP event by
	 * setting the COLLISION flag. This indicates to the user that
	 * the TRBE trace collection was stopped without stopping the
	 * ETE and thus there might be some amount of trace that was
	 * lost between the time the WRAP was detected and the IRQ
	 * was consumed by the CPU.
	 *
	 * Setting the TRUNCATED flag would move the event to STOPPED
	 * state unnecessarily, even when there is space left in the
	 * ring buffer. Using the COLLISION flag doesn't have this side
	 * effect. We only set TRUNCATED flag when there is no space
	 * left in the ring buffer.
	 */
	perf_aux_output_flag(handle, PERF_AUX_FLAG_COLLISION);
}

static void trbe_stop_and_truncate_event(struct perf_output_handle *handle)
{
	struct trbe_buf *buf = etm_perf_sink_config(handle);

	/*
	 * We cannot proceed with the buffer collection and we
	 * do not have any data for the current session. The
	 * etm_perf driver expects to close out the aux_buffer
	 * at event_stop(). So disable the TRBE here and leave
	 * the update_buffer() to return a 0 size.
	 */
	trbe_drain_and_disable_local(buf->cpudata);
	perf_aux_output_flag(handle, PERF_AUX_FLAG_TRUNCATED);
	perf_aux_output_end(handle, 0);
	*this_cpu_ptr(buf->cpudata->drvdata->handle) = NULL;
}

/*
 * TRBE Buffer Management
 *
 * The TRBE buffer spans from the base pointer till the limit pointer. When enabled,
 * it starts writing trace data from the write pointer onward till the limit pointer.
 * When the write pointer reaches the address just before the limit pointer, it gets
 * wrapped around again to the base pointer. This is called a TRBE wrap event, which
 * generates a maintenance interrupt when operated in WRAP or FILL mode. This driver
 * uses FILL mode, where the TRBE stops the trace collection at wrap event. The IRQ
 * handler updates the AUX buffer and re-enables the TRBE with updated WRITE and
 * LIMIT pointers.
 *
 *	Wrap around with an IRQ
 *	------ < ------ < ------- < ----- < -----
 *	|					|
 *	------ > ------ > ------- > ----- > -----
 *
 *	+---------------+-----------------------+
 *	|		|			|
 *	+---------------+-----------------------+
 *	Base Pointer	Write Pointer		Limit Pointer
 *
 * The base and limit pointers always needs to be PAGE_SIZE aligned. But the write
 * pointer can be aligned to the implementation defined TRBE trace buffer alignment
 * as captured in trbe_cpudata->trbe_align.
 *
 *
 *		head		tail		wakeup
 *	+---------------------------------------+----- ~ ~ ------
 *	|$$$$$$$|################|$$$$$$$$$$$$$$|		|
 *	+---------------------------------------+----- ~ ~ ------
 *	Base Pointer	Write Pointer		Limit Pointer
 *
 * The perf_output_handle indices (head, tail, wakeup) are monotonically increasing
 * values which tracks all the driver writes and user reads from the perf auxiliary
 * buffer. Generally [head..tail] is the area where the driver can write into unless
 * the wakeup is behind the tail. Enabled TRBE buffer span needs to be adjusted and
 * configured depending on the perf_output_handle indices, so that the driver does
 * not override into areas in the perf auxiliary buffer which is being or yet to be
 * consumed from the user space. The enabled TRBE buffer area is a moving subset of
 * the allocated perf auxiliary buffer.
 */

static void __trbe_pad_buf(struct trbe_buf *buf, u64 offset, int len)
{
	memset((void *)buf->trbe_base + offset, ETE_IGNORE_PACKET, len);
}

static void trbe_pad_buf(struct perf_output_handle *handle, int len)
{
	struct trbe_buf *buf = etm_perf_sink_config(handle);
	u64 head = PERF_IDX2OFF(handle->head, buf);

	__trbe_pad_buf(buf, head, len);
	if (!buf->snapshot)
		perf_aux_output_skip(handle, len);
}

static unsigned long trbe_snapshot_offset(struct perf_output_handle *handle)
{
	struct trbe_buf *buf = etm_perf_sink_config(handle);

	/*
	 * The ETE trace has alignment synchronization packets allowing
	 * the decoder to reset in case of an overflow or corruption.
	 * So we can use the entire buffer for the snapshot mode.
	 */
	return buf->nr_pages * PAGE_SIZE;
}

static u64 trbe_min_trace_buf_size(struct perf_output_handle *handle)
{
	u64 size = TRBE_TRACE_MIN_BUF_SIZE;
	struct trbe_buf *buf = etm_perf_sink_config(handle);
	struct trbe_cpudata *cpudata = buf->cpudata;

	/*
	 * When the TRBE is affected by an erratum that could make it
	 * write to the next "virtually addressed" page beyond the LIMIT.
	 * We need to make sure there is always a PAGE after the LIMIT,
	 * within the buffer. Thus we ensure there is at least an extra
	 * page than normal. With this we could then adjust the LIMIT
	 * pointer down by a PAGE later.
	 */
	if (trbe_may_write_out_of_range(cpudata))
		size += PAGE_SIZE;
	return size;
}

/*
 * TRBE Limit Calculation
 *
 * The following markers are used to illustrate various TRBE buffer situations.
 *
 * $$$$ - Data area, unconsumed captured trace data, not to be overridden
 * #### - Free area, enabled, trace will be written
 * %%%% - Free area, disabled, trace will not be written
 * ==== - Free area, padded with ETE_IGNORE_PACKET, trace will be skipped
 */
static unsigned long __trbe_normal_offset(struct perf_output_handle *handle)
{
	struct trbe_buf *buf = etm_perf_sink_config(handle);
	struct trbe_cpudata *cpudata = buf->cpudata;
	const u64 bufsize = buf->nr_pages * PAGE_SIZE;
	u64 limit = bufsize;
	u64 head, tail, wakeup;

	head = PERF_IDX2OFF(handle->head, buf);

	/*
	 *		head
	 *	------->|
	 *	|
	 *	head	TRBE align	tail
	 * +----|-------|---------------|-------+
	 * |$$$$|=======|###############|$$$$$$$|
	 * +----|-------|---------------|-------+
	 * trbe_base				trbe_base + nr_pages
	 *
	 * Perf aux buffer output head position can be misaligned depending on
	 * various factors including user space reads. In case misaligned, head
	 * needs to be aligned before TRBE can be configured. Pad the alignment
	 * gap with ETE_IGNORE_PACKET bytes that will be ignored by user tools
	 * and skip this section thus advancing the head.
	 */
	if (!IS_ALIGNED(head, cpudata->trbe_align)) {
		unsigned long delta = roundup(head, cpudata->trbe_align) - head;

		delta = min(delta, handle->size);
		trbe_pad_buf(handle, delta);
		head = PERF_IDX2OFF(handle->head, buf);
	}

	/*
	 *	head = tail (size = 0)
	 * +----|-------------------------------+
	 * |$$$$|$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$	|
	 * +----|-------------------------------+
	 * trbe_base				trbe_base + nr_pages
	 *
	 * Perf aux buffer does not have any space for the driver to write into.
	 */
	if (!handle->size)
		return 0;

	/* Compute the tail and wakeup indices now that we've aligned head */
	tail = PERF_IDX2OFF(handle->head + handle->size, buf);
	wakeup = PERF_IDX2OFF(handle->wakeup, buf);

	/*
	 * Lets calculate the buffer area which TRBE could write into. There
	 * are three possible scenarios here. Limit needs to be aligned with
	 * PAGE_SIZE per the TRBE requirement. Always avoid clobbering the
	 * unconsumed data.
	 *
	 * 1) head < tail
	 *
	 *	head			tail
	 * +----|-----------------------|-------+
	 * |$$$$|#######################|$$$$$$$|
	 * +----|-----------------------|-------+
	 * trbe_base			limit	trbe_base + nr_pages
	 *
	 * TRBE could write into [head..tail] area. Unless the tail is right at
	 * the end of the buffer, neither an wrap around nor an IRQ is expected
	 * while being enabled.
	 *
	 * 2) head == tail
	 *
	 *	head = tail (size > 0)
	 * +----|-------------------------------+
	 * |%%%%|###############################|
	 * +----|-------------------------------+
	 * trbe_base				limit = trbe_base + nr_pages
	 *
	 * TRBE should just write into [head..base + nr_pages] area even though
	 * the entire buffer is empty. Reason being, when the trace reaches the
	 * end of the buffer, it will just wrap around with an IRQ giving an
	 * opportunity to reconfigure the buffer.
	 *
	 * 3) tail < head
	 *
	 *	tail			head
	 * +----|-----------------------|-------+
	 * |%%%%|$$$$$$$$$$$$$$$$$$$$$$$|#######|
	 * +----|-----------------------|-------+
	 * trbe_base				limit = trbe_base + nr_pages
	 *
	 * TRBE should just write into [head..base + nr_pages] area even though
	 * the [trbe_base..tail] is also empty. Reason being, when the trace
	 * reaches the end of the buffer, it will just wrap around with an IRQ
	 * giving an opportunity to reconfigure the buffer.
	 */
	if (head < tail)
		limit = round_down(tail, PAGE_SIZE);

	/*
	 * Wakeup may be arbitrarily far into the future. If it's not in the
	 * current generation, either we'll wrap before hitting it, or it's
	 * in the past and has been handled already.
	 *
	 * If there's a wakeup before we wrap, arrange to be woken up by the
	 * page boundary following it. Keep the tail boundary if that's lower.
	 *
	 *	head		wakeup	tail
	 * +----|---------------|-------|-------+
	 * |$$$$|###############|%%%%%%%|$$$$$$$|
	 * +----|---------------|-------|-------+
	 * trbe_base		limit		trbe_base + nr_pages
	 */
	if (handle->wakeup < (handle->head + handle->size) && head <= wakeup)
		limit = min(limit, round_up(wakeup, PAGE_SIZE));

	/*
	 * There are two situation when this can happen i.e limit is before
	 * the head and hence TRBE cannot be configured.
	 *
	 * 1) head < tail (aligned down with PAGE_SIZE) and also they are both
	 * within the same PAGE size range.
	 *
	 *			PAGE_SIZE
	 *		|----------------------|
	 *
	 *		limit	head	tail
	 * +------------|------|--------|-------+
	 * |$$$$$$$$$$$$$$$$$$$|========|$$$$$$$|
	 * +------------|------|--------|-------+
	 * trbe_base				trbe_base + nr_pages
	 *
	 * 2) head < wakeup (aligned up with PAGE_SIZE) < tail and also both
	 * head and wakeup are within same PAGE size range.
	 *
	 *		PAGE_SIZE
	 *	|----------------------|
	 *
	 *	limit	head	wakeup  tail
	 * +----|------|-------|--------|-------+
	 * |$$$$$$$$$$$|=======|========|$$$$$$$|
	 * +----|------|-------|--------|-------+
	 * trbe_base				trbe_base + nr_pages
	 */
	if (limit > head)
		return limit;

	trbe_pad_buf(handle, handle->size);
	return 0;
}

static unsigned long trbe_normal_offset(struct perf_output_handle *handle)
{
	struct trbe_buf *buf = etm_perf_sink_config(handle);
	u64 limit = __trbe_normal_offset(handle);
	u64 head = PERF_IDX2OFF(handle->head, buf);

	/*
	 * If the head is too close to the limit and we don't
	 * have space for a meaningful run, we rather pad it
	 * and start fresh.
	 *
	 * We might have to do this more than once to make sure
	 * we have enough required space.
	 */
	while (limit && ((limit - head) < trbe_min_trace_buf_size(handle))) {
		trbe_pad_buf(handle, limit - head);
		limit = __trbe_normal_offset(handle);
		head = PERF_IDX2OFF(handle->head, buf);
	}
	return limit;
}

static unsigned long compute_trbe_buffer_limit(struct perf_output_handle *handle)
{
	struct trbe_buf *buf = etm_perf_sink_config(handle);
	unsigned long offset;

	if (buf->snapshot)
		offset = trbe_snapshot_offset(handle);
	else
		offset = trbe_normal_offset(handle);
	return buf->trbe_base + offset;
}

static void clr_trbe_status(void)
{
	u64 trbsr = read_sysreg_s(SYS_TRBSR_EL1);

	WARN_ON(is_trbe_enabled());
	trbsr &= ~TRBSR_EL1_IRQ;
	trbsr &= ~TRBSR_EL1_TRG;
	trbsr &= ~TRBSR_EL1_WRAP;
	trbsr &= ~TRBSR_EL1_EC_MASK;
	trbsr &= ~TRBSR_EL1_BSC_MASK;
	trbsr &= ~TRBSR_EL1_S;
	write_sysreg_s(trbsr, SYS_TRBSR_EL1);
}

static void set_trbe_limit_pointer_enabled(struct trbe_buf *buf)
{
	u64 trblimitr = read_sysreg_s(SYS_TRBLIMITR_EL1);
	unsigned long addr = buf->trbe_limit;

	WARN_ON(!IS_ALIGNED(addr, (1UL << TRBLIMITR_EL1_LIMIT_SHIFT)));
	WARN_ON(!IS_ALIGNED(addr, PAGE_SIZE));

	trblimitr &= ~TRBLIMITR_EL1_nVM;
	trblimitr &= ~TRBLIMITR_EL1_FM_MASK;
	trblimitr &= ~TRBLIMITR_EL1_TM_MASK;
	trblimitr &= ~TRBLIMITR_EL1_LIMIT_MASK;

	/*
	 * Fill trace buffer mode is used here while configuring the
	 * TRBE for trace capture. In this particular mode, the trace
	 * collection is stopped and a maintenance interrupt is raised
	 * when the current write pointer wraps. This pause in trace
	 * collection gives the software an opportunity to capture the
	 * trace data in the interrupt handler, before reconfiguring
	 * the TRBE.
	 */
	trblimitr |= (TRBLIMITR_EL1_FM_FILL << TRBLIMITR_EL1_FM_SHIFT) &
		     TRBLIMITR_EL1_FM_MASK;

	/*
	 * Trigger mode is not used here while configuring the TRBE for
	 * the trace capture. Hence just keep this in the ignore mode.
	 */
	trblimitr |= (TRBLIMITR_EL1_TM_IGNR << TRBLIMITR_EL1_TM_SHIFT) &
		     TRBLIMITR_EL1_TM_MASK;
	trblimitr |= (addr & PAGE_MASK);
	set_trbe_enabled(buf->cpudata, trblimitr);
}

static void trbe_enable_hw(struct trbe_buf *buf)
{
	WARN_ON(buf->trbe_hw_base < buf->trbe_base);
	WARN_ON(buf->trbe_write < buf->trbe_hw_base);
	WARN_ON(buf->trbe_write >= buf->trbe_limit);
	set_trbe_disabled(buf->cpudata);
	clr_trbe_status();
	set_trbe_base_pointer(buf->trbe_hw_base);
	set_trbe_write_pointer(buf->trbe_write);

	/*
	 * Synchronize all the register updates
	 * till now before enabling the TRBE.
	 */
	isb();
	set_trbe_limit_pointer_enabled(buf);
}

static enum trbe_fault_action trbe_get_fault_act(struct perf_output_handle *handle,
						 u64 trbsr)
{
	int ec = get_trbe_ec(trbsr);
	int bsc = get_trbe_bsc(trbsr);
	struct trbe_buf *buf = etm_perf_sink_config(handle);
	struct trbe_cpudata *cpudata = buf->cpudata;

	WARN_ON(is_trbe_running(trbsr));
	if (is_trbe_trg(trbsr) || is_trbe_abort(trbsr))
		return TRBE_FAULT_ACT_FATAL;

	if ((ec == TRBE_EC_STAGE1_ABORT) || (ec == TRBE_EC_STAGE2_ABORT))
		return TRBE_FAULT_ACT_FATAL;

	/*
	 * If the trbe is affected by TRBE_WORKAROUND_OVERWRITE_FILL_MODE,
	 * it might write data after a WRAP event in the fill mode.
	 * Thus the check TRBPTR == TRBBASER will not be honored.
	 */
	if ((is_trbe_wrap(trbsr) && (ec == TRBE_EC_OTHERS) && (bsc == TRBE_BSC_FILLED)) &&
	    (trbe_may_overwrite_in_fill_mode(cpudata) ||
	     get_trbe_write_pointer() == get_trbe_base_pointer()))
		return TRBE_FAULT_ACT_WRAP;

	return TRBE_FAULT_ACT_SPURIOUS;
}

static unsigned long trbe_get_trace_size(struct perf_output_handle *handle,
					 struct trbe_buf *buf, bool wrap)
{
	u64 write;
	u64 start_off, end_off;
	u64 size;
	u64 overwrite_skip = TRBE_WORKAROUND_OVERWRITE_FILL_MODE_SKIP_BYTES;

	/*
	 * If the TRBE has wrapped around the write pointer has
	 * wrapped and should be treated as limit.
	 *
	 * When the TRBE is affected by TRBE_WORKAROUND_WRITE_OUT_OF_RANGE,
	 * it may write upto 64bytes beyond the "LIMIT". The driver already
	 * keeps a valid page next to the LIMIT and we could potentially
	 * consume the trace data that may have been collected there. But we
	 * cannot be really sure it is available, and the TRBPTR may not
	 * indicate the same. Also, affected cores are also affected by another
	 * erratum which forces the PAGE_SIZE alignment on the TRBPTR, and thus
	 * could potentially pad an entire PAGE_SIZE - 64bytes, to get those
	 * 64bytes. Thus we ignore the potential triggering of the erratum
	 * on WRAP and limit the data to LIMIT.
	 */
	if (wrap)
		write = get_trbe_limit_pointer();
	else
		write = get_trbe_write_pointer();

	/*
	 * TRBE may use a different base address than the base
	 * of the ring buffer. Thus use the beginning of the ring
	 * buffer to compute the offsets.
	 */
	end_off = write - buf->trbe_base;
	start_off = PERF_IDX2OFF(handle->head, buf);

	if (WARN_ON_ONCE(end_off < start_off))
		return 0;

	size = end_off - start_off;
	/*
	 * If the TRBE is affected by the following erratum, we must fill
	 * the space we skipped with IGNORE packets. And we are always
	 * guaranteed to have at least a PAGE_SIZE space in the buffer.
	 */
	if (trbe_has_erratum(buf->cpudata, TRBE_WORKAROUND_OVERWRITE_FILL_MODE) &&
	    !WARN_ON(size < overwrite_skip))
		__trbe_pad_buf(buf, start_off, overwrite_skip);

	return size;
}

static void *arm_trbe_alloc_buffer(struct coresight_device *csdev,
				   struct perf_event *event, void **pages,
				   int nr_pages, bool snapshot)
{
	struct trbe_buf *buf;
	struct page **pglist;
	int i;

	/*
	 * TRBE LIMIT and TRBE WRITE pointers must be page aligned. But with
	 * just a single page, there would not be any room left while writing
	 * into a partially filled TRBE buffer after the page size alignment.
	 * Hence restrict the minimum buffer size as two pages.
	 */
	if (nr_pages < 2)
		return NULL;

	buf = kzalloc_node(sizeof(*buf), GFP_KERNEL, trbe_alloc_node(event));
	if (!buf)
		return ERR_PTR(-ENOMEM);

	pglist = kcalloc(nr_pages, sizeof(*pglist), GFP_KERNEL);
	if (!pglist) {
		kfree(buf);
		return ERR_PTR(-ENOMEM);
	}

	for (i = 0; i < nr_pages; i++)
		pglist[i] = virt_to_page(pages[i]);

	buf->trbe_base = (unsigned long)vmap(pglist, nr_pages, VM_MAP, PAGE_KERNEL);
	if (!buf->trbe_base) {
		kfree(pglist);
		kfree(buf);
		return ERR_PTR(-ENOMEM);
	}
	buf->trbe_limit = buf->trbe_base + nr_pages * PAGE_SIZE;
	buf->trbe_write = buf->trbe_base;
	buf->snapshot = snapshot;
	buf->nr_pages = nr_pages;
	buf->pages = pages;
	kfree(pglist);
	return buf;
}

static void arm_trbe_free_buffer(void *config)
{
	struct trbe_buf *buf = config;

	vunmap((void *)buf->trbe_base);
	kfree(buf);
}

static unsigned long arm_trbe_update_buffer(struct coresight_device *csdev,
					    struct perf_output_handle *handle,
					    void *config)
{
	struct trbe_drvdata *drvdata = dev_get_drvdata(csdev->dev.parent);
	struct trbe_cpudata *cpudata = dev_get_drvdata(&csdev->dev);
	struct trbe_buf *buf = config;
	enum trbe_fault_action act;
	unsigned long size, status;
	unsigned long flags;
	bool wrap = false;

	WARN_ON(buf->cpudata != cpudata);
	WARN_ON(cpudata->cpu != smp_processor_id());
	WARN_ON(cpudata->drvdata != drvdata);
	if (cpudata->mode != CS_MODE_PERF)
		return 0;

	/*
	 * We are about to disable the TRBE. And this could in turn
	 * fill up the buffer triggering, an IRQ. This could be consumed
	 * by the PE asynchronously, causing a race here against
	 * the IRQ handler in closing out the handle. So, let us
	 * make sure the IRQ can't trigger while we are collecting
	 * the buffer. We also make sure that a WRAP event is handled
	 * accordingly.
	 */
	local_irq_save(flags);

	/*
	 * If the TRBE was disabled due to lack of space in the AUX buffer or a
	 * spurious fault, the driver leaves it disabled, truncating the buffer.
	 * Since the etm_perf driver expects to close out the AUX buffer, the
	 * driver skips it. Thus, just pass in 0 size here to indicate that the
	 * buffer was truncated.
	 */
	if (!is_trbe_enabled()) {
		size = 0;
		goto done;
	}
	/*
	 * perf handle structure needs to be shared with the TRBE IRQ handler for
	 * capturing trace data and restarting the handle. There is a probability
	 * of an undefined reference based crash when etm event is being stopped
	 * while a TRBE IRQ also getting processed. This happens due the release
	 * of perf handle via perf_aux_output_end() in etm_event_stop(). Stopping
	 * the TRBE here will ensure that no IRQ could be generated when the perf
	 * handle gets freed in etm_event_stop().
	 */
	trbe_drain_and_disable_local(cpudata);

	/* Check if there is a pending interrupt and handle it here */
	status = read_sysreg_s(SYS_TRBSR_EL1);
	if (is_trbe_irq(status)) {

		/*
		 * Now that we are handling the IRQ here, clear the IRQ
		 * from the status, to let the irq handler know that it
		 * is taken care of.
		 */
		clr_trbe_irq();
		isb();

		act = trbe_get_fault_act(handle, status);
		/*
		 * If this was not due to a WRAP event, we have some
		 * errors and as such buffer is empty.
		 */
		if (act != TRBE_FAULT_ACT_WRAP) {
			size = 0;
			goto done;
		}

		trbe_report_wrap_event(handle);
		wrap = true;
	}

	size = trbe_get_trace_size(handle, buf, wrap);

done:
	local_irq_restore(flags);

	if (buf->snapshot)
		handle->head += size;
	return size;
}


static int trbe_apply_work_around_before_enable(struct trbe_buf *buf)
{
	/*
	 * TRBE_WORKAROUND_OVERWRITE_FILL_MODE causes the TRBE to overwrite a few cache
	 * line size from the "TRBBASER_EL1" in the event of a "FILL".
	 * Thus, we could loose some amount of the trace at the base.
	 *
	 * Before Fix:
	 *
	 *  normal-BASE     head (normal-TRBPTR)         tail (normal-LIMIT)
	 *  |                   \/                       /
	 *   -------------------------------------------------------------
	 *  |   Pg0      |   Pg1       |           |          |  PgN     |
	 *   -------------------------------------------------------------
	 *
	 * In the normal course of action, we would set the TRBBASER to the
	 * beginning of the ring-buffer (normal-BASE). But with the erratum,
	 * the TRBE could overwrite the contents at the "normal-BASE", after
	 * hitting the "normal-LIMIT", since it doesn't stop as expected. And
	 * this is wrong. This could result in overwriting trace collected in
	 * one of the previous runs, being consumed by the user. So we must
	 * always make sure that the TRBBASER is within the region
	 * [head, head+size]. Note that TRBBASER must be PAGE aligned,
	 *
	 *  After moving the BASE:
	 *
	 *  normal-BASE     head (normal-TRBPTR)         tail (normal-LIMIT)
	 *  |                   \/                       /
	 *   -------------------------------------------------------------
	 *  |         |          |xyzdef.     |..   tuvw|                |
	 *   -------------------------------------------------------------
	 *                      /
	 *              New-BASER
	 *
	 * Also, we would set the TRBPTR to head (after adjusting for
	 * alignment) at normal-PTR. This would mean that the last few bytes
	 * of the trace (say, "xyz") might overwrite the first few bytes of
	 * trace written ("abc"). More importantly they will appear in what
	 * userspace sees as the beginning of the trace, which is wrong. We may
	 * not always have space to move the latest trace "xyz" to the correct
	 * order as it must appear beyond the LIMIT. (i.e, [head..head+size]).
	 * Thus it is easier to ignore those bytes than to complicate the
	 * driver to move it, assuming that the erratum was triggered and
	 * doing additional checks to see if there is indeed allowed space at
	 * TRBLIMITR.LIMIT.
	 *
	 *  Thus the full workaround will move the BASE and the PTR and would
	 *  look like (after padding at the skipped bytes at the end of
	 *  session) :
	 *
	 *  normal-BASE     head (normal-TRBPTR)         tail (normal-LIMIT)
	 *  |                   \/                       /
	 *   -------------------------------------------------------------
	 *  |         |          |///abc..     |..  rst|                |
	 *   -------------------------------------------------------------
	 *                      /    |
	 *              New-BASER    New-TRBPTR
	 *
	 * To summarize, with the work around:
	 *
	 *  - We always align the offset for the next session to PAGE_SIZE
	 *    (This is to ensure we can program the TRBBASER to this offset
	 *    within the region [head...head+size]).
	 *
	 *  - At TRBE enable:
	 *     - Set the TRBBASER to the page aligned offset of the current
	 *       proposed write offset. (which is guaranteed to be aligned
	 *       as above)
	 *     - Move the TRBPTR to skip first 256bytes (that might be
	 *       overwritten with the erratum). This ensures that the trace
	 *       generated in the session is not re-written.
	 *
	 *  - At trace collection:
	 *     - Pad the 256bytes skipped above again with IGNORE packets.
	 */
	if (trbe_has_erratum(buf->cpudata, TRBE_WORKAROUND_OVERWRITE_FILL_MODE)) {
		if (WARN_ON(!IS_ALIGNED(buf->trbe_write, PAGE_SIZE)))
			return -EINVAL;
		buf->trbe_hw_base = buf->trbe_write;
		buf->trbe_write += TRBE_WORKAROUND_OVERWRITE_FILL_MODE_SKIP_BYTES;
	}

	/*
	 * TRBE_WORKAROUND_WRITE_OUT_OF_RANGE could cause the TRBE to write to
	 * the next page after the TRBLIMITR.LIMIT. For perf, the "next page"
	 * may be:
	 *     - The page beyond the ring buffer. This could mean, TRBE could
	 *       corrupt another entity (kernel / user)
	 *     - A portion of the "ring buffer" consumed by the userspace.
	 *       i.e, a page outisde [head, head + size].
	 *
	 * We work around this by:
	 *     - Making sure that we have at least an extra space of PAGE left
	 *       in the ring buffer [head, head + size], than we normally do
	 *       without the erratum. See trbe_min_trace_buf_size().
	 *
	 *     - Adjust the TRBLIMITR.LIMIT to leave the extra PAGE outside
	 *       the TRBE's range (i.e [TRBBASER, TRBLIMITR.LIMI] ).
	 */
	if (trbe_has_erratum(buf->cpudata, TRBE_WORKAROUND_WRITE_OUT_OF_RANGE)) {
		s64 space = buf->trbe_limit - buf->trbe_write;
		/*
		 * We must have more than a PAGE_SIZE worth space in the proposed
		 * range for the TRBE.
		 */
		if (WARN_ON(space <= PAGE_SIZE ||
			    !IS_ALIGNED(buf->trbe_limit, PAGE_SIZE)))
			return -EINVAL;
		buf->trbe_limit -= PAGE_SIZE;
	}

	return 0;
}

static int __arm_trbe_enable(struct trbe_buf *buf,
			     struct perf_output_handle *handle)
{
	int ret = 0;

	perf_aux_output_flag(handle, PERF_AUX_FLAG_CORESIGHT_FORMAT_RAW);
	buf->trbe_limit = compute_trbe_buffer_limit(handle);
	buf->trbe_write = buf->trbe_base + PERF_IDX2OFF(handle->head, buf);
	if (buf->trbe_limit == buf->trbe_base) {
		ret = -ENOSPC;
		goto err;
	}
	/* Set the base of the TRBE to the buffer base */
	buf->trbe_hw_base = buf->trbe_base;

	ret = trbe_apply_work_around_before_enable(buf);
	if (ret)
		goto err;

	*this_cpu_ptr(buf->cpudata->drvdata->handle) = handle;
	trbe_enable_hw(buf);
	return 0;
err:
	trbe_stop_and_truncate_event(handle);
	return ret;
}

static int arm_trbe_enable(struct coresight_device *csdev, u32 mode, void *data)
{
	struct trbe_drvdata *drvdata = dev_get_drvdata(csdev->dev.parent);
	struct trbe_cpudata *cpudata = dev_get_drvdata(&csdev->dev);
	struct perf_output_handle *handle = data;
	struct trbe_buf *buf = etm_perf_sink_config(handle);

	WARN_ON(cpudata->cpu != smp_processor_id());
	WARN_ON(cpudata->drvdata != drvdata);
	if (mode != CS_MODE_PERF)
		return -EINVAL;

	cpudata->buf = buf;
	cpudata->mode = mode;
	buf->cpudata = cpudata;

	return __arm_trbe_enable(buf, handle);
}

static int arm_trbe_disable(struct coresight_device *csdev)
{
	struct trbe_drvdata *drvdata = dev_get_drvdata(csdev->dev.parent);
	struct trbe_cpudata *cpudata = dev_get_drvdata(&csdev->dev);
	struct trbe_buf *buf = cpudata->buf;

	WARN_ON(buf->cpudata != cpudata);
	WARN_ON(cpudata->cpu != smp_processor_id());
	WARN_ON(cpudata->drvdata != drvdata);
	if (cpudata->mode != CS_MODE_PERF)
		return -EINVAL;

	trbe_drain_and_disable_local(cpudata);
	buf->cpudata = NULL;
	cpudata->buf = NULL;
	cpudata->mode = CS_MODE_DISABLED;
	return 0;
}

static void trbe_handle_spurious(struct perf_output_handle *handle)
{
	struct trbe_buf *buf = etm_perf_sink_config(handle);
	u64 trblimitr = read_sysreg_s(SYS_TRBLIMITR_EL1);

	/*
	 * If the IRQ was spurious, simply re-enable the TRBE
	 * back without modifying the buffer parameters to
	 * retain the trace collected so far.
	 */
	set_trbe_enabled(buf->cpudata, trblimitr);
}

static int trbe_handle_overflow(struct perf_output_handle *handle)
{
	struct perf_event *event = handle->event;
	struct trbe_buf *buf = etm_perf_sink_config(handle);
	unsigned long size;
	struct etm_event_data *event_data;

	size = trbe_get_trace_size(handle, buf, true);
	if (buf->snapshot)
		handle->head += size;

	trbe_report_wrap_event(handle);
	perf_aux_output_end(handle, size);
	event_data = perf_aux_output_begin(handle, event);
	if (!event_data) {
		/*
		 * We are unable to restart the trace collection,
		 * thus leave the TRBE disabled. The etm-perf driver
		 * is able to detect this with a disconnected handle
		 * (handle->event = NULL).
		 */
		trbe_drain_and_disable_local(buf->cpudata);
		*this_cpu_ptr(buf->cpudata->drvdata->handle) = NULL;
		return -EINVAL;
	}

	return __arm_trbe_enable(buf, handle);
}

static bool is_perf_trbe(struct perf_output_handle *handle)
{
	struct trbe_buf *buf = etm_perf_sink_config(handle);
	struct trbe_cpudata *cpudata = buf->cpudata;
	struct trbe_drvdata *drvdata = cpudata->drvdata;
	int cpu = smp_processor_id();

	WARN_ON(buf->trbe_hw_base != get_trbe_base_pointer());
	WARN_ON(buf->trbe_limit != get_trbe_limit_pointer());

	if (cpudata->mode != CS_MODE_PERF)
		return false;

	if (cpudata->cpu != cpu)
		return false;

	if (!cpumask_test_cpu(cpu, &drvdata->supported_cpus))
		return false;

	return true;
}

static irqreturn_t arm_trbe_irq_handler(int irq, void *dev)
{
	struct perf_output_handle **handle_ptr = dev;
	struct perf_output_handle *handle = *handle_ptr;
	struct trbe_buf *buf = etm_perf_sink_config(handle);
	enum trbe_fault_action act;
	u64 status;
	bool truncated = false;
	u64 trfcr;

	/* Reads to TRBSR_EL1 is fine when TRBE is active */
	status = read_sysreg_s(SYS_TRBSR_EL1);
	/*
	 * If the pending IRQ was handled by update_buffer callback
	 * we have nothing to do here.
	 */
	if (!is_trbe_irq(status))
		return IRQ_NONE;

	/* Prohibit the CPU from tracing before we disable the TRBE */
	trfcr = cpu_prohibit_trace();
	/*
	 * Ensure the trace is visible to the CPUs and
	 * any external aborts have been resolved.
	 */
	trbe_drain_and_disable_local(buf->cpudata);
	clr_trbe_irq();
	isb();

	if (WARN_ON_ONCE(!handle) || !perf_get_aux(handle))
		return IRQ_NONE;

	if (!is_perf_trbe(handle))
		return IRQ_NONE;

	act = trbe_get_fault_act(handle, status);
	switch (act) {
	case TRBE_FAULT_ACT_WRAP:
		truncated = !!trbe_handle_overflow(handle);
		break;
	case TRBE_FAULT_ACT_SPURIOUS:
		trbe_handle_spurious(handle);
		break;
	case TRBE_FAULT_ACT_FATAL:
		trbe_stop_and_truncate_event(handle);
		truncated = true;
		break;
	}

	/*
	 * If the buffer was truncated, ensure perf callbacks
	 * have completed, which will disable the event.
	 *
	 * Otherwise, restore the trace filter controls to
	 * allow the tracing.
	 */
	if (truncated)
		irq_work_run();
	else
		write_trfcr(trfcr);

	return IRQ_HANDLED;
}

static const struct coresight_ops_sink arm_trbe_sink_ops = {
	.enable		= arm_trbe_enable,
	.disable	= arm_trbe_disable,
	.alloc_buffer	= arm_trbe_alloc_buffer,
	.free_buffer	= arm_trbe_free_buffer,
	.update_buffer	= arm_trbe_update_buffer,
};

static const struct coresight_ops arm_trbe_cs_ops = {
	.sink_ops	= &arm_trbe_sink_ops,
};

static ssize_t align_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct trbe_cpudata *cpudata = dev_get_drvdata(dev);

	return sprintf(buf, "%llx\n", cpudata->trbe_hw_align);
}
static DEVICE_ATTR_RO(align);

static ssize_t flag_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct trbe_cpudata *cpudata = dev_get_drvdata(dev);

	return sprintf(buf, "%d\n", cpudata->trbe_flag);
}
static DEVICE_ATTR_RO(flag);

static struct attribute *arm_trbe_attrs[] = {
	&dev_attr_align.attr,
	&dev_attr_flag.attr,
	NULL,
};

static const struct attribute_group arm_trbe_group = {
	.attrs = arm_trbe_attrs,
};

static const struct attribute_group *arm_trbe_groups[] = {
	&arm_trbe_group,
	NULL,
};

static void arm_trbe_enable_cpu(void *info)
{
	struct trbe_drvdata *drvdata = info;
	struct trbe_cpudata *cpudata = this_cpu_ptr(drvdata->cpudata);

	trbe_reset_local(cpudata);
	enable_percpu_irq(drvdata->irq, IRQ_TYPE_NONE);
}

static void arm_trbe_register_coresight_cpu(struct trbe_drvdata *drvdata, int cpu)
{
	struct trbe_cpudata *cpudata = per_cpu_ptr(drvdata->cpudata, cpu);
	struct coresight_device *trbe_csdev = coresight_get_percpu_sink(cpu);
	struct coresight_desc desc = { 0 };
	struct device *dev;

	if (WARN_ON(trbe_csdev))
		return;

	/* If the TRBE was not probed on the CPU, we shouldn't be here */
	if (WARN_ON(!cpudata->drvdata))
		return;

	dev = &cpudata->drvdata->pdev->dev;
	desc.name = devm_kasprintf(dev, GFP_KERNEL, "trbe%d", cpu);
	if (!desc.name)
		goto cpu_clear;

	desc.type = CORESIGHT_DEV_TYPE_SINK;
	desc.subtype.sink_subtype = CORESIGHT_DEV_SUBTYPE_SINK_PERCPU_SYSMEM;
	desc.ops = &arm_trbe_cs_ops;
	desc.pdata = dev_get_platdata(dev);
	desc.groups = arm_trbe_groups;
	desc.dev = dev;
	trbe_csdev = coresight_register(&desc);
	if (IS_ERR(trbe_csdev))
		goto cpu_clear;

	dev_set_drvdata(&trbe_csdev->dev, cpudata);
	coresight_set_percpu_sink(cpu, trbe_csdev);
	return;
cpu_clear:
	cpumask_clear_cpu(cpu, &drvdata->supported_cpus);
}

/*
 * Must be called with preemption disabled, for trbe_check_errata().
 */
static void arm_trbe_probe_cpu(void *info)
{
	struct trbe_drvdata *drvdata = info;
	int cpu = smp_processor_id();
	struct trbe_cpudata *cpudata = per_cpu_ptr(drvdata->cpudata, cpu);
	u64 trbidr;

	if (WARN_ON(!cpudata))
		goto cpu_clear;

	if (!is_trbe_available()) {
		pr_err("TRBE is not implemented on cpu %d\n", cpu);
		goto cpu_clear;
	}

	trbidr = read_sysreg_s(SYS_TRBIDR_EL1);
	if (!is_trbe_programmable(trbidr)) {
		pr_err("TRBE is owned in higher exception level on cpu %d\n", cpu);
		goto cpu_clear;
	}

	cpudata->trbe_hw_align = 1ULL << get_trbe_address_align(trbidr);
	if (cpudata->trbe_hw_align > SZ_2K) {
		pr_err("Unsupported alignment on cpu %d\n", cpu);
		goto cpu_clear;
	}

	/*
	 * Run the TRBE erratum checks, now that we know
	 * this instance is about to be registered.
	 */
	trbe_check_errata(cpudata);

	if (trbe_is_broken(cpudata)) {
		pr_err("Disabling TRBE on cpu%d due to erratum\n", cpu);
		goto cpu_clear;
	}

	/*
	 * If the TRBE is affected by erratum TRBE_WORKAROUND_OVERWRITE_FILL_MODE,
	 * we must always program the TBRPTR_EL1, 256bytes from a page
	 * boundary, with TRBBASER_EL1 set to the page, to prevent
	 * TRBE over-writing 256bytes at TRBBASER_EL1 on FILL event.
	 *
	 * Thus make sure we always align our write pointer to a PAGE_SIZE,
	 * which also guarantees that we have at least a PAGE_SIZE space in
	 * the buffer (TRBLIMITR is PAGE aligned) and thus we can skip
	 * the required bytes at the base.
	 */
	if (trbe_may_overwrite_in_fill_mode(cpudata))
		cpudata->trbe_align = PAGE_SIZE;
	else
		cpudata->trbe_align = cpudata->trbe_hw_align;

	cpudata->trbe_flag = get_trbe_flag_update(trbidr);
	cpudata->cpu = cpu;
	cpudata->drvdata = drvdata;
	return;
cpu_clear:
	cpumask_clear_cpu(cpu, &drvdata->supported_cpus);
}

static void arm_trbe_remove_coresight_cpu(void *info)
{
	int cpu = smp_processor_id();
	struct trbe_drvdata *drvdata = info;
	struct trbe_cpudata *cpudata = per_cpu_ptr(drvdata->cpudata, cpu);
	struct coresight_device *trbe_csdev = coresight_get_percpu_sink(cpu);

	disable_percpu_irq(drvdata->irq);
	trbe_reset_local(cpudata);
	if (trbe_csdev) {
		coresight_unregister(trbe_csdev);
		cpudata->drvdata = NULL;
		coresight_set_percpu_sink(cpu, NULL);
	}
}

static int arm_trbe_probe_coresight(struct trbe_drvdata *drvdata)
{
	int cpu;

	drvdata->cpudata = alloc_percpu(typeof(*drvdata->cpudata));
	if (!drvdata->cpudata)
		return -ENOMEM;

	for_each_cpu(cpu, &drvdata->supported_cpus) {
		/* If we fail to probe the CPU, let us defer it to hotplug callbacks */
		if (smp_call_function_single(cpu, arm_trbe_probe_cpu, drvdata, 1))
			continue;
		if (cpumask_test_cpu(cpu, &drvdata->supported_cpus))
			arm_trbe_register_coresight_cpu(drvdata, cpu);
		if (cpumask_test_cpu(cpu, &drvdata->supported_cpus))
			smp_call_function_single(cpu, arm_trbe_enable_cpu, drvdata, 1);
	}
	return 0;
}

static int arm_trbe_remove_coresight(struct trbe_drvdata *drvdata)
{
	int cpu;

	for_each_cpu(cpu, &drvdata->supported_cpus)
		smp_call_function_single(cpu, arm_trbe_remove_coresight_cpu, drvdata, 1);
	free_percpu(drvdata->cpudata);
	return 0;
}

static void arm_trbe_probe_hotplugged_cpu(struct trbe_drvdata *drvdata)
{
	preempt_disable();
	arm_trbe_probe_cpu(drvdata);
	preempt_enable();
}

static int arm_trbe_cpu_startup(unsigned int cpu, struct hlist_node *node)
{
	struct trbe_drvdata *drvdata = hlist_entry_safe(node, struct trbe_drvdata, hotplug_node);

	if (cpumask_test_cpu(cpu, &drvdata->supported_cpus)) {

		/*
		 * If this CPU was not probed for TRBE,
		 * initialize it now.
		 */
		if (!coresight_get_percpu_sink(cpu)) {
			arm_trbe_probe_hotplugged_cpu(drvdata);
			if (cpumask_test_cpu(cpu, &drvdata->supported_cpus))
				arm_trbe_register_coresight_cpu(drvdata, cpu);
			if (cpumask_test_cpu(cpu, &drvdata->supported_cpus))
				arm_trbe_enable_cpu(drvdata);
		} else {
			arm_trbe_enable_cpu(drvdata);
		}
	}
	return 0;
}

static int arm_trbe_cpu_teardown(unsigned int cpu, struct hlist_node *node)
{
	struct trbe_drvdata *drvdata = hlist_entry_safe(node, struct trbe_drvdata, hotplug_node);

	if (cpumask_test_cpu(cpu, &drvdata->supported_cpus)) {
		struct trbe_cpudata *cpudata = per_cpu_ptr(drvdata->cpudata, cpu);

		disable_percpu_irq(drvdata->irq);
		trbe_reset_local(cpudata);
	}
	return 0;
}

static int arm_trbe_probe_cpuhp(struct trbe_drvdata *drvdata)
{
	enum cpuhp_state trbe_online;
	int ret;

	trbe_online = cpuhp_setup_state_multi(CPUHP_AP_ONLINE_DYN, DRVNAME,
					      arm_trbe_cpu_startup, arm_trbe_cpu_teardown);
	if (trbe_online < 0)
		return trbe_online;

	ret = cpuhp_state_add_instance(trbe_online, &drvdata->hotplug_node);
	if (ret) {
		cpuhp_remove_multi_state(trbe_online);
		return ret;
	}
	drvdata->trbe_online = trbe_online;
	return 0;
}

static void arm_trbe_remove_cpuhp(struct trbe_drvdata *drvdata)
{
	cpuhp_state_remove_instance(drvdata->trbe_online, &drvdata->hotplug_node);
	cpuhp_remove_multi_state(drvdata->trbe_online);
}

static int arm_trbe_probe_irq(struct platform_device *pdev,
			      struct trbe_drvdata *drvdata)
{
	int ret;

	drvdata->irq = platform_get_irq(pdev, 0);
	if (drvdata->irq < 0) {
		pr_err("IRQ not found for the platform device\n");
		return drvdata->irq;
	}

	if (!irq_is_percpu(drvdata->irq)) {
		pr_err("IRQ is not a PPI\n");
		return -EINVAL;
	}

	if (irq_get_percpu_devid_partition(drvdata->irq, &drvdata->supported_cpus))
		return -EINVAL;

	drvdata->handle = alloc_percpu(struct perf_output_handle *);
	if (!drvdata->handle)
		return -ENOMEM;

	ret = request_percpu_irq(drvdata->irq, arm_trbe_irq_handler, DRVNAME, drvdata->handle);
	if (ret) {
		free_percpu(drvdata->handle);
		return ret;
	}
	return 0;
}

static void arm_trbe_remove_irq(struct trbe_drvdata *drvdata)
{
	free_percpu_irq(drvdata->irq, drvdata->handle);
	free_percpu(drvdata->handle);
}

static int arm_trbe_device_probe(struct platform_device *pdev)
{
	struct coresight_platform_data *pdata;
	struct trbe_drvdata *drvdata;
	struct device *dev = &pdev->dev;
	int ret;

	/* Trace capture is not possible with kernel page table isolation */
	if (arm64_kernel_unmapped_at_el0()) {
		pr_err("TRBE wouldn't work if kernel gets unmapped at EL0\n");
		return -EOPNOTSUPP;
	}

	drvdata = devm_kzalloc(dev, sizeof(*drvdata), GFP_KERNEL);
	if (!drvdata)
		return -ENOMEM;

	pdata = coresight_get_platform_data(dev);
	if (IS_ERR(pdata))
		return PTR_ERR(pdata);

	dev_set_drvdata(dev, drvdata);
	dev->platform_data = pdata;
	drvdata->pdev = pdev;
	ret = arm_trbe_probe_irq(pdev, drvdata);
	if (ret)
		return ret;

	ret = arm_trbe_probe_coresight(drvdata);
	if (ret)
		goto probe_failed;

	ret = arm_trbe_probe_cpuhp(drvdata);
	if (ret)
		goto cpuhp_failed;

	return 0;
cpuhp_failed:
	arm_trbe_remove_coresight(drvdata);
probe_failed:
	arm_trbe_remove_irq(drvdata);
	return ret;
}

static int arm_trbe_device_remove(struct platform_device *pdev)
{
	struct trbe_drvdata *drvdata = platform_get_drvdata(pdev);

	arm_trbe_remove_cpuhp(drvdata);
	arm_trbe_remove_coresight(drvdata);
	arm_trbe_remove_irq(drvdata);
	return 0;
}

static const struct of_device_id arm_trbe_of_match[] = {
	{ .compatible = "arm,trace-buffer-extension"},
	{},
};
MODULE_DEVICE_TABLE(of, arm_trbe_of_match);

static struct platform_driver arm_trbe_driver = {
	.driver	= {
		.name = DRVNAME,
		.of_match_table = of_match_ptr(arm_trbe_of_match),
		.suppress_bind_attrs = true,
	},
	.probe	= arm_trbe_device_probe,
	.remove	= arm_trbe_device_remove,
};

static int __init arm_trbe_init(void)
{
	int ret;

	ret = platform_driver_register(&arm_trbe_driver);
	if (!ret)
		return 0;

	pr_err("Error registering %s platform driver\n", DRVNAME);
	return ret;
}

static void __exit arm_trbe_exit(void)
{
	platform_driver_unregister(&arm_trbe_driver);
}
module_init(arm_trbe_init);
module_exit(arm_trbe_exit);

MODULE_AUTHOR("Anshuman Khandual <anshuman.khandual@arm.com>");
MODULE_DESCRIPTION("Arm Trace Buffer Extension (TRBE) driver");
MODULE_LICENSE("GPL v2");

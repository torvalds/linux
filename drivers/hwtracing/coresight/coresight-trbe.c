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
 * struct trbe_cpudata: TRBE instance specific data
 * @trbe_flag		- TRBE dirty/access flag support
 * @trbe_hw_align	- Actual TRBE alignment required for TRBPTR_EL1.
 * @trbe_align		- Software alignment used for the TRBPTR_EL1.
 * @cpu			- CPU this TRBE belongs to.
 * @mode		- Mode of current operation. (perf/disabled)
 * @drvdata		- TRBE specific drvdata
 */
struct trbe_cpudata {
	bool trbe_flag;
	u64 trbe_hw_align;
	u64 trbe_align;
	int cpu;
	enum cs_mode mode;
	struct trbe_buf *buf;
	struct trbe_drvdata *drvdata;
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

static int trbe_alloc_node(struct perf_event *event)
{
	if (event->cpu == -1)
		return NUMA_NO_NODE;
	return cpu_to_node(event->cpu);
}

static void trbe_drain_buffer(void)
{
	tsb_csync();
	dsb(nsh);
}

static void trbe_drain_and_disable_local(void)
{
	u64 trblimitr = read_sysreg_s(SYS_TRBLIMITR_EL1);

	trbe_drain_buffer();

	/*
	 * Disable the TRBE without clearing LIMITPTR which
	 * might be required for fetching the buffer limits.
	 */
	trblimitr &= ~TRBLIMITR_ENABLE;
	write_sysreg_s(trblimitr, SYS_TRBLIMITR_EL1);
	isb();
}

static void trbe_reset_local(void)
{
	trbe_drain_and_disable_local();
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
	trbe_drain_and_disable_local();
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
	 */
	if (limit && (limit - head < TRBE_TRACE_MIN_BUF_SIZE)) {
		trbe_pad_buf(handle, limit - head);
		limit = __trbe_normal_offset(handle);
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
	trbsr &= ~TRBSR_IRQ;
	trbsr &= ~TRBSR_TRG;
	trbsr &= ~TRBSR_WRAP;
	trbsr &= ~(TRBSR_EC_MASK << TRBSR_EC_SHIFT);
	trbsr &= ~(TRBSR_BSC_MASK << TRBSR_BSC_SHIFT);
	trbsr &= ~TRBSR_STOP;
	write_sysreg_s(trbsr, SYS_TRBSR_EL1);
}

static void set_trbe_limit_pointer_enabled(unsigned long addr)
{
	u64 trblimitr = read_sysreg_s(SYS_TRBLIMITR_EL1);

	WARN_ON(!IS_ALIGNED(addr, (1UL << TRBLIMITR_LIMIT_SHIFT)));
	WARN_ON(!IS_ALIGNED(addr, PAGE_SIZE));

	trblimitr &= ~TRBLIMITR_NVM;
	trblimitr &= ~(TRBLIMITR_FILL_MODE_MASK << TRBLIMITR_FILL_MODE_SHIFT);
	trblimitr &= ~(TRBLIMITR_TRIG_MODE_MASK << TRBLIMITR_TRIG_MODE_SHIFT);
	trblimitr &= ~(TRBLIMITR_LIMIT_MASK << TRBLIMITR_LIMIT_SHIFT);

	/*
	 * Fill trace buffer mode is used here while configuring the
	 * TRBE for trace capture. In this particular mode, the trace
	 * collection is stopped and a maintenance interrupt is raised
	 * when the current write pointer wraps. This pause in trace
	 * collection gives the software an opportunity to capture the
	 * trace data in the interrupt handler, before reconfiguring
	 * the TRBE.
	 */
	trblimitr |= (TRBE_FILL_MODE_FILL & TRBLIMITR_FILL_MODE_MASK) << TRBLIMITR_FILL_MODE_SHIFT;

	/*
	 * Trigger mode is not used here while configuring the TRBE for
	 * the trace capture. Hence just keep this in the ignore mode.
	 */
	trblimitr |= (TRBE_TRIG_MODE_IGNORE & TRBLIMITR_TRIG_MODE_MASK) <<
		      TRBLIMITR_TRIG_MODE_SHIFT;
	trblimitr |= (addr & PAGE_MASK);

	trblimitr |= TRBLIMITR_ENABLE;
	write_sysreg_s(trblimitr, SYS_TRBLIMITR_EL1);

	/* Synchronize the TRBE enable event */
	isb();
}

static void trbe_enable_hw(struct trbe_buf *buf)
{
	WARN_ON(buf->trbe_hw_base < buf->trbe_base);
	WARN_ON(buf->trbe_write < buf->trbe_hw_base);
	WARN_ON(buf->trbe_write >= buf->trbe_limit);
	set_trbe_disabled();
	isb();
	clr_trbe_status();
	set_trbe_base_pointer(buf->trbe_hw_base);
	set_trbe_write_pointer(buf->trbe_write);

	/*
	 * Synchronize all the register updates
	 * till now before enabling the TRBE.
	 */
	isb();
	set_trbe_limit_pointer_enabled(buf->trbe_limit);
}

static enum trbe_fault_action trbe_get_fault_act(u64 trbsr)
{
	int ec = get_trbe_ec(trbsr);
	int bsc = get_trbe_bsc(trbsr);

	WARN_ON(is_trbe_running(trbsr));
	if (is_trbe_trg(trbsr) || is_trbe_abort(trbsr))
		return TRBE_FAULT_ACT_FATAL;

	if ((ec == TRBE_EC_STAGE1_ABORT) || (ec == TRBE_EC_STAGE2_ABORT))
		return TRBE_FAULT_ACT_FATAL;

	if (is_trbe_wrap(trbsr) && (ec == TRBE_EC_OTHERS) && (bsc == TRBE_BSC_FILLED)) {
		if (get_trbe_write_pointer() == get_trbe_base_pointer())
			return TRBE_FAULT_ACT_WRAP;
	}
	return TRBE_FAULT_ACT_SPURIOUS;
}

static unsigned long trbe_get_trace_size(struct perf_output_handle *handle,
					 struct trbe_buf *buf, bool wrap)
{
	u64 write;
	u64 start_off, end_off;

	/*
	 * If the TRBE has wrapped around the write pointer has
	 * wrapped and should be treated as limit.
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
	return (end_off - start_off);
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
	trbe_drain_and_disable_local();

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

		act = trbe_get_fault_act(status);
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

static int __arm_trbe_enable(struct trbe_buf *buf,
			     struct perf_output_handle *handle)
{
	perf_aux_output_flag(handle, PERF_AUX_FLAG_CORESIGHT_FORMAT_RAW);
	buf->trbe_limit = compute_trbe_buffer_limit(handle);
	buf->trbe_write = buf->trbe_base + PERF_IDX2OFF(handle->head, buf);
	if (buf->trbe_limit == buf->trbe_base) {
		trbe_stop_and_truncate_event(handle);
		return -ENOSPC;
	}
	/* Set the base of the TRBE to the buffer base */
	buf->trbe_hw_base = buf->trbe_base;
	*this_cpu_ptr(buf->cpudata->drvdata->handle) = handle;
	trbe_enable_hw(buf);
	return 0;
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

	trbe_drain_and_disable_local();
	buf->cpudata = NULL;
	cpudata->buf = NULL;
	cpudata->mode = CS_MODE_DISABLED;
	return 0;
}

static void trbe_handle_spurious(struct perf_output_handle *handle)
{
	u64 limitr = read_sysreg_s(SYS_TRBLIMITR_EL1);

	/*
	 * If the IRQ was spurious, simply re-enable the TRBE
	 * back without modifying the buffer parameters to
	 * retain the trace collected so far.
	 */
	limitr |= TRBLIMITR_ENABLE;
	write_sysreg_s(limitr, SYS_TRBLIMITR_EL1);
	isb();
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
		trbe_drain_and_disable_local();
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
	trbe_drain_and_disable_local();
	clr_trbe_irq();
	isb();

	if (WARN_ON_ONCE(!handle) || !perf_get_aux(handle))
		return IRQ_NONE;

	if (!is_perf_trbe(handle))
		return IRQ_NONE;

	act = trbe_get_fault_act(status);
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

	trbe_reset_local();
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
	trbe_reset_local();
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

static int arm_trbe_cpu_startup(unsigned int cpu, struct hlist_node *node)
{
	struct trbe_drvdata *drvdata = hlist_entry_safe(node, struct trbe_drvdata, hotplug_node);

	if (cpumask_test_cpu(cpu, &drvdata->supported_cpus)) {

		/*
		 * If this CPU was not probed for TRBE,
		 * initialize it now.
		 */
		if (!coresight_get_percpu_sink(cpu)) {
			arm_trbe_probe_cpu(drvdata);
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
		disable_percpu_irq(drvdata->irq);
		trbe_reset_local();
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

	if (arm64_kernel_unmapped_at_el0()) {
		pr_err("TRBE wouldn't work if kernel gets unmapped at EL0\n");
		return -EOPNOTSUPP;
	}

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

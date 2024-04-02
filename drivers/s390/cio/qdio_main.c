// SPDX-License-Identifier: GPL-2.0
/*
 * Linux for s390 qdio support, buffer handling, qdio API and module support.
 *
 * Copyright IBM Corp. 2000, 2008
 * Author(s): Utz Bacher <utz.bacher@de.ibm.com>
 *	      Jan Glauber <jang@linux.vnet.ibm.com>
 * 2.6 cio integration by Cornelia Huck <cornelia.huck@de.ibm.com>
 */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/kmemleak.h>
#include <linux/delay.h>
#include <linux/gfp.h>
#include <linux/io.h>
#include <linux/atomic.h>
#include <asm/debug.h>
#include <asm/qdio.h>
#include <asm/ipl.h>

#include "cio.h"
#include "css.h"
#include "device.h"
#include "qdio.h"
#include "qdio_debug.h"

MODULE_AUTHOR("Utz Bacher <utz.bacher@de.ibm.com>,"\
	"Jan Glauber <jang@linux.vnet.ibm.com>");
MODULE_DESCRIPTION("QDIO base support");
MODULE_LICENSE("GPL");

static inline int do_siga_sync(unsigned long schid,
			       unsigned long out_mask, unsigned long in_mask,
			       unsigned int fc)
{
	int cc;

	asm volatile(
		"	lgr	0,%[fc]\n"
		"	lgr	1,%[schid]\n"
		"	lgr	2,%[out]\n"
		"	lgr	3,%[in]\n"
		"	siga	0\n"
		"	ipm	%[cc]\n"
		"	srl	%[cc],28\n"
		: [cc] "=&d" (cc)
		: [fc] "d" (fc), [schid] "d" (schid),
		  [out] "d" (out_mask), [in] "d" (in_mask)
		: "cc", "0", "1", "2", "3");
	return cc;
}

static inline int do_siga_input(unsigned long schid, unsigned long mask,
				unsigned long fc)
{
	int cc;

	asm volatile(
		"	lgr	0,%[fc]\n"
		"	lgr	1,%[schid]\n"
		"	lgr	2,%[mask]\n"
		"	siga	0\n"
		"	ipm	%[cc]\n"
		"	srl	%[cc],28\n"
		: [cc] "=&d" (cc)
		: [fc] "d" (fc), [schid] "d" (schid), [mask] "d" (mask)
		: "cc", "0", "1", "2");
	return cc;
}

/**
 * do_siga_output - perform SIGA-w/wt function
 * @schid: subchannel id or in case of QEBSM the subchannel token
 * @mask: which output queues to process
 * @bb: busy bit indicator, set only if SIGA-w/wt could not access a buffer
 * @fc: function code to perform
 * @aob: asynchronous operation block
 *
 * Returns condition code.
 * Note: For IQDC unicast queues only the highest priority queue is processed.
 */
static inline int do_siga_output(unsigned long schid, unsigned long mask,
				 unsigned int *bb, unsigned long fc,
				 dma64_t aob)
{
	int cc;

	asm volatile(
		"	lgr	0,%[fc]\n"
		"	lgr	1,%[schid]\n"
		"	lgr	2,%[mask]\n"
		"	lgr	3,%[aob]\n"
		"	siga	0\n"
		"	lgr	%[fc],0\n"
		"	ipm	%[cc]\n"
		"	srl	%[cc],28\n"
		: [cc] "=&d" (cc), [fc] "+&d" (fc)
		: [schid] "d" (schid), [mask] "d" (mask), [aob] "d" (aob)
		: "cc", "0", "1", "2", "3");
	*bb = fc >> 31;
	return cc;
}

/**
 * qdio_do_eqbs - extract buffer states for QEBSM
 * @q: queue to manipulate
 * @state: state of the extracted buffers
 * @start: buffer number to start at
 * @count: count of buffers to examine
 * @auto_ack: automatically acknowledge buffers
 *
 * Returns the number of successfully extracted equal buffer states.
 * Stops processing if a state is different from the last buffers state.
 */
static int qdio_do_eqbs(struct qdio_q *q, unsigned char *state,
			int start, int count, int auto_ack)
{
	int tmp_count = count, tmp_start = start, nr = q->nr;
	unsigned int ccq = 0;

	qperf_inc(q, eqbs);

	if (!q->is_input_q)
		nr += q->irq_ptr->nr_input_qs;
again:
	ccq = do_eqbs(q->irq_ptr->sch_token, state, nr, &tmp_start, &tmp_count,
		      auto_ack);

	switch (ccq) {
	case 0:
	case 32:
		/* all done, or next buffer state different */
		return count - tmp_count;
	case 96:
		/* not all buffers processed */
		qperf_inc(q, eqbs_partial);
		DBF_DEV_EVENT(DBF_INFO, q->irq_ptr, "EQBS part:%02x",
			tmp_count);
		return count - tmp_count;
	case 97:
		/* no buffer processed */
		DBF_DEV_EVENT(DBF_WARN, q->irq_ptr, "EQBS again:%2d", ccq);
		goto again;
	default:
		DBF_ERROR("%4x ccq:%3d", SCH_NO(q), ccq);
		DBF_ERROR("%4x EQBS ERROR", SCH_NO(q));
		DBF_ERROR("%3d%3d%2d", count, tmp_count, nr);
		q->handler(q->irq_ptr->cdev, QDIO_ERROR_GET_BUF_STATE, q->nr,
			   q->first_to_check, count, q->irq_ptr->int_parm);
		return 0;
	}
}

/**
 * qdio_do_sqbs - set buffer states for QEBSM
 * @q: queue to manipulate
 * @state: new state of the buffers
 * @start: first buffer number to change
 * @count: how many buffers to change
 *
 * Returns the number of successfully changed buffers.
 * Does retrying until the specified count of buffer states is set or an
 * error occurs.
 */
static int qdio_do_sqbs(struct qdio_q *q, unsigned char state, int start,
			int count)
{
	unsigned int ccq = 0;
	int tmp_count = count, tmp_start = start;
	int nr = q->nr;

	qperf_inc(q, sqbs);

	if (!q->is_input_q)
		nr += q->irq_ptr->nr_input_qs;
again:
	ccq = do_sqbs(q->irq_ptr->sch_token, state, nr, &tmp_start, &tmp_count);

	switch (ccq) {
	case 0:
	case 32:
		/* all done, or active buffer adapter-owned */
		WARN_ON_ONCE(tmp_count);
		return count - tmp_count;
	case 96:
		/* not all buffers processed */
		DBF_DEV_EVENT(DBF_INFO, q->irq_ptr, "SQBS again:%2d", ccq);
		qperf_inc(q, sqbs_partial);
		goto again;
	default:
		DBF_ERROR("%4x ccq:%3d", SCH_NO(q), ccq);
		DBF_ERROR("%4x SQBS ERROR", SCH_NO(q));
		DBF_ERROR("%3d%3d%2d", count, tmp_count, nr);
		q->handler(q->irq_ptr->cdev, QDIO_ERROR_SET_BUF_STATE, q->nr,
			   q->first_to_check, count, q->irq_ptr->int_parm);
		return 0;
	}
}

/*
 * Returns number of examined buffers and their common state in *state.
 * Requested number of buffers-to-examine must be > 0.
 */
static inline int get_buf_states(struct qdio_q *q, unsigned int bufnr,
				 unsigned char *state, unsigned int count,
				 int auto_ack)
{
	unsigned char __state = 0;
	int i = 1;

	if (is_qebsm(q))
		return qdio_do_eqbs(q, state, bufnr, count, auto_ack);

	/* get initial state: */
	__state = q->slsb.val[bufnr];

	/* Bail out early if there is no work on the queue: */
	if (__state & SLSB_OWNER_CU)
		goto out;

	for (; i < count; i++) {
		bufnr = next_buf(bufnr);

		/* stop if next state differs from initial state: */
		if (q->slsb.val[bufnr] != __state)
			break;
	}

out:
	*state = __state;
	return i;
}

static inline int get_buf_state(struct qdio_q *q, unsigned int bufnr,
				unsigned char *state, int auto_ack)
{
	return get_buf_states(q, bufnr, state, 1, auto_ack);
}

/* wrap-around safe setting of slsb states, returns number of changed buffers */
static inline int set_buf_states(struct qdio_q *q, int bufnr,
				 unsigned char state, int count)
{
	int i;

	if (is_qebsm(q))
		return qdio_do_sqbs(q, state, bufnr, count);

	/* Ensure that all preceding changes to the SBALs are visible: */
	mb();

	for (i = 0; i < count; i++) {
		WRITE_ONCE(q->slsb.val[bufnr], state);
		bufnr = next_buf(bufnr);
	}

	/* Make our SLSB changes visible: */
	mb();

	return count;
}

static inline int set_buf_state(struct qdio_q *q, int bufnr,
				unsigned char state)
{
	return set_buf_states(q, bufnr, state, 1);
}

/* set slsb states to initial state */
static void qdio_init_buf_states(struct qdio_irq *irq_ptr)
{
	struct qdio_q *q;
	int i;

	for_each_input_queue(irq_ptr, q, i)
		set_buf_states(q, 0, SLSB_P_INPUT_NOT_INIT,
			       QDIO_MAX_BUFFERS_PER_Q);
	for_each_output_queue(irq_ptr, q, i)
		set_buf_states(q, 0, SLSB_P_OUTPUT_NOT_INIT,
			       QDIO_MAX_BUFFERS_PER_Q);
}

static inline int qdio_siga_sync(struct qdio_q *q, unsigned int output,
			  unsigned int input)
{
	unsigned long schid = *((u32 *) &q->irq_ptr->schid);
	unsigned int fc = QDIO_SIGA_SYNC;
	int cc;

	DBF_DEV_EVENT(DBF_INFO, q->irq_ptr, "siga-s:%1d", q->nr);
	qperf_inc(q, siga_sync);

	if (is_qebsm(q)) {
		schid = q->irq_ptr->sch_token;
		fc |= QDIO_SIGA_QEBSM_FLAG;
	}

	cc = do_siga_sync(schid, output, input, fc);
	if (unlikely(cc))
		DBF_ERROR("%4x SIGA-S:%2d", SCH_NO(q), cc);
	return (cc) ? -EIO : 0;
}

static inline int qdio_sync_input_queue(struct qdio_q *q)
{
	return qdio_siga_sync(q, 0, q->mask);
}

static inline int qdio_sync_output_queue(struct qdio_q *q)
{
	return qdio_siga_sync(q, q->mask, 0);
}

static inline int qdio_siga_sync_q(struct qdio_q *q)
{
	if (q->is_input_q)
		return qdio_sync_input_queue(q);
	else
		return qdio_sync_output_queue(q);
}

static int qdio_siga_output(struct qdio_q *q, unsigned int count,
			    unsigned int *busy_bit, dma64_t aob)
{
	unsigned long schid = *((u32 *) &q->irq_ptr->schid);
	unsigned int fc = QDIO_SIGA_WRITE;
	u64 start_time = 0;
	int retries = 0, cc;

	if (queue_type(q) == QDIO_IQDIO_QFMT && !multicast_outbound(q)) {
		if (count > 1)
			fc = QDIO_SIGA_WRITEM;
		else if (aob)
			fc = QDIO_SIGA_WRITEQ;
	}

	if (is_qebsm(q)) {
		schid = q->irq_ptr->sch_token;
		fc |= QDIO_SIGA_QEBSM_FLAG;
	}
again:
	cc = do_siga_output(schid, q->mask, busy_bit, fc, aob);

	/* hipersocket busy condition */
	if (unlikely(*busy_bit)) {
		retries++;

		if (!start_time) {
			start_time = get_tod_clock_fast();
			goto again;
		}
		if (get_tod_clock_fast() - start_time < QDIO_BUSY_BIT_PATIENCE)
			goto again;
	}
	if (retries) {
		DBF_DEV_EVENT(DBF_WARN, q->irq_ptr,
			      "%4x cc2 BB1:%1d", SCH_NO(q), q->nr);
		DBF_DEV_EVENT(DBF_WARN, q->irq_ptr, "count:%u", retries);
	}
	return cc;
}

static inline int qdio_siga_input(struct qdio_q *q)
{
	unsigned long schid = *((u32 *) &q->irq_ptr->schid);
	unsigned int fc = QDIO_SIGA_READ;
	int cc;

	DBF_DEV_EVENT(DBF_INFO, q->irq_ptr, "siga-r:%1d", q->nr);
	qperf_inc(q, siga_read);

	if (is_qebsm(q)) {
		schid = q->irq_ptr->sch_token;
		fc |= QDIO_SIGA_QEBSM_FLAG;
	}

	cc = do_siga_input(schid, q->mask, fc);
	if (unlikely(cc))
		DBF_ERROR("%4x SIGA-R:%2d", SCH_NO(q), cc);
	return (cc) ? -EIO : 0;
}

int debug_get_buf_state(struct qdio_q *q, unsigned int bufnr,
			unsigned char *state)
{
	if (qdio_need_siga_sync(q->irq_ptr))
		qdio_siga_sync_q(q);
	return get_buf_state(q, bufnr, state, 0);
}

static inline void qdio_stop_polling(struct qdio_q *q)
{
	if (!q->u.in.batch_count)
		return;

	qperf_inc(q, stop_polling);

	/* show the card that we are not polling anymore */
	set_buf_states(q, q->u.in.batch_start, SLSB_P_INPUT_NOT_INIT,
		       q->u.in.batch_count);
	q->u.in.batch_count = 0;
}

static inline void account_sbals(struct qdio_q *q, unsigned int count)
{
	q->q_stats.nr_sbal_total += count;
	q->q_stats.nr_sbals[ilog2(count)]++;
}

static void process_buffer_error(struct qdio_q *q, unsigned int start,
				 int count)
{
	/* special handling for no target buffer empty */
	if (queue_type(q) == QDIO_IQDIO_QFMT && !q->is_input_q &&
	    q->sbal[start]->element[15].sflags == 0x10) {
		qperf_inc(q, target_full);
		DBF_DEV_EVENT(DBF_INFO, q->irq_ptr, "OUTFULL FTC:%02x", start);
		return;
	}

	DBF_ERROR("%4x BUF ERROR", SCH_NO(q));
	DBF_ERROR((q->is_input_q) ? "IN:%2d" : "OUT:%2d", q->nr);
	DBF_ERROR("FTC:%3d C:%3d", start, count);
	DBF_ERROR("F14:%2x F15:%2x",
		  q->sbal[start]->element[14].sflags,
		  q->sbal[start]->element[15].sflags);
}

static inline void inbound_handle_work(struct qdio_q *q, unsigned int start,
				       int count, bool auto_ack)
{
	/* ACK the newest SBAL: */
	if (!auto_ack)
		set_buf_state(q, add_buf(start, count - 1), SLSB_P_INPUT_ACK);

	if (!q->u.in.batch_count)
		q->u.in.batch_start = start;
	q->u.in.batch_count += count;
}

static int get_inbound_buffer_frontier(struct qdio_q *q, unsigned int start,
				       unsigned int *error)
{
	unsigned char state = 0;
	int count;

	q->timestamp = get_tod_clock_fast();

	count = atomic_read(&q->nr_buf_used);
	if (!count)
		return 0;

	if (qdio_need_siga_sync(q->irq_ptr))
		qdio_sync_input_queue(q);

	count = get_buf_states(q, start, &state, count, 1);
	if (!count)
		return 0;

	switch (state) {
	case SLSB_P_INPUT_PRIMED:
		DBF_DEV_EVENT(DBF_INFO, q->irq_ptr, "in prim:%1d %02x", q->nr,
			      count);

		inbound_handle_work(q, start, count, is_qebsm(q));
		if (atomic_sub_return(count, &q->nr_buf_used) == 0)
			qperf_inc(q, inbound_queue_full);
		if (q->irq_ptr->perf_stat_enabled)
			account_sbals(q, count);
		return count;
	case SLSB_P_INPUT_ERROR:
		DBF_DEV_EVENT(DBF_INFO, q->irq_ptr, "in err:%1d %02x", q->nr,
			      count);

		*error = QDIO_ERROR_SLSB_STATE;
		process_buffer_error(q, start, count);
		inbound_handle_work(q, start, count, false);
		if (atomic_sub_return(count, &q->nr_buf_used) == 0)
			qperf_inc(q, inbound_queue_full);
		if (q->irq_ptr->perf_stat_enabled)
			account_sbals_error(q, count);
		return count;
	case SLSB_CU_INPUT_EMPTY:
		if (q->irq_ptr->perf_stat_enabled)
			q->q_stats.nr_sbal_nop++;
		DBF_DEV_EVENT(DBF_INFO, q->irq_ptr, "in nop:%1d %#02x",
			      q->nr, start);
		return 0;
	case SLSB_P_INPUT_NOT_INIT:
	case SLSB_P_INPUT_ACK:
		/* We should never see this state, throw a WARN: */
	default:
		dev_WARN_ONCE(&q->irq_ptr->cdev->dev, 1,
			      "found state %#x at index %u on queue %u\n",
			      state, start, q->nr);
		return 0;
	}
}

int qdio_inspect_input_queue(struct ccw_device *cdev, unsigned int nr,
			     unsigned int *bufnr, unsigned int *error)
{
	struct qdio_irq *irq = cdev->private->qdio_data;
	unsigned int start;
	struct qdio_q *q;
	int count;

	if (!irq)
		return -ENODEV;

	q = irq->input_qs[nr];
	start = q->first_to_check;
	*error = 0;

	count = get_inbound_buffer_frontier(q, start, error);
	if (count == 0)
		return 0;

	*bufnr = start;
	q->first_to_check = add_buf(start, count);
	return count;
}
EXPORT_SYMBOL_GPL(qdio_inspect_input_queue);

static inline int qdio_inbound_q_done(struct qdio_q *q, unsigned int start)
{
	unsigned char state = 0;

	if (!atomic_read(&q->nr_buf_used))
		return 1;

	if (qdio_need_siga_sync(q->irq_ptr))
		qdio_sync_input_queue(q);
	get_buf_state(q, start, &state, 0);

	if (state == SLSB_P_INPUT_PRIMED || state == SLSB_P_INPUT_ERROR)
		/* more work coming */
		return 0;

	return 1;
}

static int get_outbound_buffer_frontier(struct qdio_q *q, unsigned int start,
					unsigned int *error)
{
	unsigned char state = 0;
	int count;

	q->timestamp = get_tod_clock_fast();

	count = atomic_read(&q->nr_buf_used);
	if (!count)
		return 0;

	if (qdio_need_siga_sync(q->irq_ptr))
		qdio_sync_output_queue(q);

	count = get_buf_states(q, start, &state, count, 0);
	if (!count)
		return 0;

	switch (state) {
	case SLSB_P_OUTPUT_PENDING:
		*error = QDIO_ERROR_SLSB_PENDING;
		fallthrough;
	case SLSB_P_OUTPUT_EMPTY:
		/* the adapter got it */
		DBF_DEV_EVENT(DBF_INFO, q->irq_ptr,
			"out empty:%1d %02x", q->nr, count);

		atomic_sub(count, &q->nr_buf_used);
		if (q->irq_ptr->perf_stat_enabled)
			account_sbals(q, count);
		return count;
	case SLSB_P_OUTPUT_ERROR:
		DBF_DEV_EVENT(DBF_INFO, q->irq_ptr, "out error:%1d %02x",
			      q->nr, count);

		*error = QDIO_ERROR_SLSB_STATE;
		process_buffer_error(q, start, count);
		atomic_sub(count, &q->nr_buf_used);
		if (q->irq_ptr->perf_stat_enabled)
			account_sbals_error(q, count);
		return count;
	case SLSB_CU_OUTPUT_PRIMED:
		/* the adapter has not fetched the output yet */
		if (q->irq_ptr->perf_stat_enabled)
			q->q_stats.nr_sbal_nop++;
		DBF_DEV_EVENT(DBF_INFO, q->irq_ptr, "out primed:%1d",
			      q->nr);
		return 0;
	case SLSB_P_OUTPUT_HALTED:
		return 0;
	case SLSB_P_OUTPUT_NOT_INIT:
		/* We should never see this state, throw a WARN: */
	default:
		dev_WARN_ONCE(&q->irq_ptr->cdev->dev, 1,
			      "found state %#x at index %u on queue %u\n",
			      state, start, q->nr);
		return 0;
	}
}

int qdio_inspect_output_queue(struct ccw_device *cdev, unsigned int nr,
			      unsigned int *bufnr, unsigned int *error)
{
	struct qdio_irq *irq = cdev->private->qdio_data;
	unsigned int start;
	struct qdio_q *q;
	int count;

	if (!irq)
		return -ENODEV;

	q = irq->output_qs[nr];
	start = q->first_to_check;
	*error = 0;

	count = get_outbound_buffer_frontier(q, start, error);
	if (count == 0)
		return 0;

	*bufnr = start;
	q->first_to_check = add_buf(start, count);
	return count;
}
EXPORT_SYMBOL_GPL(qdio_inspect_output_queue);

static int qdio_kick_outbound_q(struct qdio_q *q, unsigned int count,
				dma64_t aob)
{
	int retries = 0, cc;
	unsigned int busy_bit;

	if (!qdio_need_siga_out(q->irq_ptr))
		return 0;

	DBF_DEV_EVENT(DBF_INFO, q->irq_ptr, "siga-w:%1d", q->nr);
retry:
	qperf_inc(q, siga_write);

	cc = qdio_siga_output(q, count, &busy_bit, aob);
	switch (cc) {
	case 0:
		break;
	case 2:
		if (busy_bit) {
			while (++retries < QDIO_BUSY_BIT_RETRIES) {
				mdelay(QDIO_BUSY_BIT_RETRY_DELAY);
				goto retry;
			}
			DBF_ERROR("%4x cc2 BBC:%1d", SCH_NO(q), q->nr);
			cc = -EBUSY;
		} else {
			DBF_DEV_EVENT(DBF_INFO, q->irq_ptr, "siga-w cc2:%1d", q->nr);
			cc = -ENOBUFS;
		}
		break;
	case 1:
	case 3:
		DBF_ERROR("%4x SIGA-W:%1d", SCH_NO(q), cc);
		cc = -EIO;
		break;
	}
	if (retries) {
		DBF_ERROR("%4x cc2 BB2:%1d", SCH_NO(q), q->nr);
		DBF_ERROR("count:%u", retries);
	}
	return cc;
}

static inline void qdio_set_state(struct qdio_irq *irq_ptr,
				  enum qdio_irq_states state)
{
	DBF_DEV_EVENT(DBF_INFO, irq_ptr, "newstate: %1d", state);

	irq_ptr->state = state;
	mb();
}

static void qdio_irq_check_sense(struct qdio_irq *irq_ptr, struct irb *irb)
{
	if (irb->esw.esw0.erw.cons) {
		DBF_ERROR("%4x sense:", irq_ptr->schid.sch_no);
		DBF_ERROR_HEX(irb, 64);
		DBF_ERROR_HEX(irb->ecw, 64);
	}
}

/* PCI interrupt handler */
static void qdio_int_handler_pci(struct qdio_irq *irq_ptr)
{
	if (unlikely(irq_ptr->state != QDIO_IRQ_STATE_ACTIVE))
		return;

	qdio_deliver_irq(irq_ptr);
	irq_ptr->last_data_irq_time = S390_lowcore.int_clock;
}

static void qdio_handle_activate_check(struct qdio_irq *irq_ptr,
				       unsigned long intparm, int cstat,
				       int dstat)
{
	unsigned int first_to_check = 0;

	DBF_ERROR("%4x ACT CHECK", irq_ptr->schid.sch_no);
	DBF_ERROR("intp :%lx", intparm);
	DBF_ERROR("ds: %2x cs:%2x", dstat, cstat);

	/* zfcp wants this: */
	if (irq_ptr->nr_input_qs)
		first_to_check = irq_ptr->input_qs[0]->first_to_check;

	irq_ptr->error_handler(irq_ptr->cdev, QDIO_ERROR_ACTIVATE, 0,
			       first_to_check, 0, irq_ptr->int_parm);
	qdio_set_state(irq_ptr, QDIO_IRQ_STATE_STOPPED);
	/*
	 * In case of z/VM LGR (Live Guest Migration) QDIO recovery will happen.
	 * Therefore we call the LGR detection function here.
	 */
	lgr_info_log();
}

static void qdio_establish_handle_irq(struct qdio_irq *irq_ptr, int cstat,
				      int dstat)
{
	DBF_DEV_EVENT(DBF_INFO, irq_ptr, "qest irq");

	if (cstat)
		goto error;
	if (dstat & ~(DEV_STAT_DEV_END | DEV_STAT_CHN_END))
		goto error;
	if (!(dstat & DEV_STAT_DEV_END))
		goto error;
	qdio_set_state(irq_ptr, QDIO_IRQ_STATE_ESTABLISHED);
	return;

error:
	DBF_ERROR("%4x EQ:error", irq_ptr->schid.sch_no);
	DBF_ERROR("ds: %2x cs:%2x", dstat, cstat);
	qdio_set_state(irq_ptr, QDIO_IRQ_STATE_ERR);
}

/* qdio interrupt handler */
void qdio_int_handler(struct ccw_device *cdev, unsigned long intparm,
		      struct irb *irb)
{
	struct qdio_irq *irq_ptr = cdev->private->qdio_data;
	struct subchannel_id schid;
	int cstat, dstat;

	if (!intparm || !irq_ptr) {
		ccw_device_get_schid(cdev, &schid);
		DBF_ERROR("qint:%4x", schid.sch_no);
		return;
	}

	if (irq_ptr->perf_stat_enabled)
		irq_ptr->perf_stat.qdio_int++;

	if (IS_ERR(irb)) {
		DBF_ERROR("%4x IO error", irq_ptr->schid.sch_no);
		qdio_set_state(irq_ptr, QDIO_IRQ_STATE_ERR);
		wake_up(&cdev->private->wait_q);
		return;
	}
	qdio_irq_check_sense(irq_ptr, irb);
	cstat = irb->scsw.cmd.cstat;
	dstat = irb->scsw.cmd.dstat;

	switch (irq_ptr->state) {
	case QDIO_IRQ_STATE_INACTIVE:
		qdio_establish_handle_irq(irq_ptr, cstat, dstat);
		break;
	case QDIO_IRQ_STATE_CLEANUP:
		qdio_set_state(irq_ptr, QDIO_IRQ_STATE_INACTIVE);
		break;
	case QDIO_IRQ_STATE_ESTABLISHED:
	case QDIO_IRQ_STATE_ACTIVE:
		if (cstat & SCHN_STAT_PCI) {
			qdio_int_handler_pci(irq_ptr);
			return;
		}
		if (cstat || dstat)
			qdio_handle_activate_check(irq_ptr, intparm, cstat,
						   dstat);
		break;
	case QDIO_IRQ_STATE_STOPPED:
		break;
	default:
		WARN_ON_ONCE(1);
	}
	wake_up(&cdev->private->wait_q);
}

/**
 * qdio_get_ssqd_desc - get qdio subchannel description
 * @cdev: ccw device to get description for
 * @data: where to store the ssqd
 *
 * Returns 0 or an error code. The results of the chsc are stored in the
 * specified structure.
 */
int qdio_get_ssqd_desc(struct ccw_device *cdev,
		       struct qdio_ssqd_desc *data)
{
	struct subchannel_id schid;

	if (!cdev || !cdev->private)
		return -EINVAL;

	ccw_device_get_schid(cdev, &schid);
	DBF_EVENT("get ssqd:%4x", schid.sch_no);
	return qdio_setup_get_ssqd(NULL, &schid, data);
}
EXPORT_SYMBOL_GPL(qdio_get_ssqd_desc);

static int qdio_cancel_ccw(struct qdio_irq *irq, int how)
{
	struct ccw_device *cdev = irq->cdev;
	long timeout;
	int rc;

	spin_lock_irq(get_ccwdev_lock(cdev));
	qdio_set_state(irq, QDIO_IRQ_STATE_CLEANUP);
	if (how & QDIO_FLAG_CLEANUP_USING_CLEAR)
		rc = ccw_device_clear(cdev, QDIO_DOING_CLEANUP);
	else
		/* default behaviour is halt */
		rc = ccw_device_halt(cdev, QDIO_DOING_CLEANUP);
	spin_unlock_irq(get_ccwdev_lock(cdev));
	if (rc) {
		DBF_ERROR("%4x SHUTD ERR", irq->schid.sch_no);
		DBF_ERROR("rc:%4d", rc);
		return rc;
	}

	timeout = wait_event_interruptible_timeout(cdev->private->wait_q,
						   irq->state == QDIO_IRQ_STATE_INACTIVE ||
						   irq->state == QDIO_IRQ_STATE_ERR,
						   10 * HZ);
	if (timeout <= 0)
		rc = (timeout == -ERESTARTSYS) ? -EINTR : -ETIME;

	return rc;
}

/**
 * qdio_shutdown - shut down a qdio subchannel
 * @cdev: associated ccw device
 * @how: use halt or clear to shutdown
 */
int qdio_shutdown(struct ccw_device *cdev, int how)
{
	struct qdio_irq *irq_ptr = cdev->private->qdio_data;
	struct subchannel_id schid;
	int rc;

	if (!irq_ptr)
		return -ENODEV;

	WARN_ON_ONCE(irqs_disabled());
	ccw_device_get_schid(cdev, &schid);
	DBF_EVENT("qshutdown:%4x", schid.sch_no);

	mutex_lock(&irq_ptr->setup_mutex);
	/*
	 * Subchannel was already shot down. We cannot prevent being called
	 * twice since cio may trigger a shutdown asynchronously.
	 */
	if (irq_ptr->state == QDIO_IRQ_STATE_INACTIVE) {
		mutex_unlock(&irq_ptr->setup_mutex);
		return 0;
	}

	/*
	 * Indicate that the device is going down.
	 */
	qdio_set_state(irq_ptr, QDIO_IRQ_STATE_STOPPED);

	qdio_shutdown_debug_entries(irq_ptr);

	rc = qdio_cancel_ccw(irq_ptr, how);
	qdio_shutdown_thinint(irq_ptr);
	qdio_shutdown_irq(irq_ptr);

	qdio_set_state(irq_ptr, QDIO_IRQ_STATE_INACTIVE);
	mutex_unlock(&irq_ptr->setup_mutex);
	if (rc)
		return rc;
	return 0;
}
EXPORT_SYMBOL_GPL(qdio_shutdown);

/**
 * qdio_free - free data structures for a qdio subchannel
 * @cdev: associated ccw device
 */
int qdio_free(struct ccw_device *cdev)
{
	struct qdio_irq *irq_ptr = cdev->private->qdio_data;
	struct subchannel_id schid;

	if (!irq_ptr)
		return -ENODEV;

	ccw_device_get_schid(cdev, &schid);
	DBF_EVENT("qfree:%4x", schid.sch_no);
	DBF_DEV_EVENT(DBF_ERR, irq_ptr, "dbf abandoned");
	mutex_lock(&irq_ptr->setup_mutex);

	irq_ptr->debug_area = NULL;
	cdev->private->qdio_data = NULL;
	mutex_unlock(&irq_ptr->setup_mutex);

	qdio_free_queues(irq_ptr);
	free_page((unsigned long) irq_ptr->qdr);
	free_page(irq_ptr->chsc_page);
	kfree(irq_ptr->ccw);
	free_page((unsigned long) irq_ptr);
	return 0;
}
EXPORT_SYMBOL_GPL(qdio_free);

/**
 * qdio_allocate - allocate qdio queues and associated data
 * @cdev: associated ccw device
 * @no_input_qs: allocate this number of Input Queues
 * @no_output_qs: allocate this number of Output Queues
 */
int qdio_allocate(struct ccw_device *cdev, unsigned int no_input_qs,
		  unsigned int no_output_qs)
{
	struct subchannel_id schid;
	struct qdio_irq *irq_ptr;
	int rc = -ENOMEM;

	ccw_device_get_schid(cdev, &schid);
	DBF_EVENT("qallocate:%4x", schid.sch_no);

	if (no_input_qs > QDIO_MAX_QUEUES_PER_IRQ ||
	    no_output_qs > QDIO_MAX_QUEUES_PER_IRQ)
		return -EINVAL;

	irq_ptr = (void *) get_zeroed_page(GFP_KERNEL);
	if (!irq_ptr)
		return -ENOMEM;

	irq_ptr->ccw = kmalloc(sizeof(*irq_ptr->ccw), GFP_KERNEL | GFP_DMA);
	if (!irq_ptr->ccw)
		goto err_ccw;

	/* kmemleak doesn't scan the page-allocated irq_ptr: */
	kmemleak_not_leak(irq_ptr->ccw);

	irq_ptr->cdev = cdev;
	mutex_init(&irq_ptr->setup_mutex);
	if (qdio_allocate_dbf(irq_ptr))
		goto err_dbf;

	DBF_DEV_EVENT(DBF_ERR, irq_ptr, "alloc niq:%1u noq:%1u", no_input_qs,
		      no_output_qs);

	/*
	 * Allocate a page for the chsc calls in qdio_establish.
	 * Must be pre-allocated since a zfcp recovery will call
	 * qdio_establish. In case of low memory and swap on a zfcp disk
	 * we may not be able to allocate memory otherwise.
	 */
	irq_ptr->chsc_page = get_zeroed_page(GFP_KERNEL);
	if (!irq_ptr->chsc_page)
		goto err_chsc;

	/* qdr is used in ccw1.cda which is u32 */
	irq_ptr->qdr = (struct qdr *) get_zeroed_page(GFP_KERNEL | GFP_DMA);
	if (!irq_ptr->qdr)
		goto err_qdr;

	rc = qdio_allocate_qs(irq_ptr, no_input_qs, no_output_qs);
	if (rc)
		goto err_queues;

	cdev->private->qdio_data = irq_ptr;
	qdio_set_state(irq_ptr, QDIO_IRQ_STATE_INACTIVE);
	return 0;

err_queues:
	free_page((unsigned long) irq_ptr->qdr);
err_qdr:
	free_page(irq_ptr->chsc_page);
err_chsc:
err_dbf:
	kfree(irq_ptr->ccw);
err_ccw:
	free_page((unsigned long) irq_ptr);
	return rc;
}
EXPORT_SYMBOL_GPL(qdio_allocate);

static void qdio_trace_init_data(struct qdio_irq *irq,
				 struct qdio_initialize *data)
{
	DBF_DEV_EVENT(DBF_ERR, irq, "qfmt:%1u", data->q_format);
	DBF_DEV_EVENT(DBF_ERR, irq, "qpff%4x", data->qib_param_field_format);
	DBF_DEV_HEX(irq, &data->qib_param_field, sizeof(void *), DBF_ERR);
	DBF_DEV_EVENT(DBF_ERR, irq, "niq:%1u noq:%1u", data->no_input_qs,
		      data->no_output_qs);
	DBF_DEV_HEX(irq, &data->input_handler, sizeof(void *), DBF_ERR);
	DBF_DEV_HEX(irq, &data->output_handler, sizeof(void *), DBF_ERR);
	DBF_DEV_HEX(irq, &data->int_parm, sizeof(long), DBF_ERR);
	DBF_DEV_HEX(irq, &data->input_sbal_addr_array, sizeof(void *), DBF_ERR);
	DBF_DEV_HEX(irq, &data->output_sbal_addr_array, sizeof(void *),
		    DBF_ERR);
}

/**
 * qdio_establish - establish queues on a qdio subchannel
 * @cdev: associated ccw device
 * @init_data: initialization data
 */
int qdio_establish(struct ccw_device *cdev,
		   struct qdio_initialize *init_data)
{
	struct qdio_irq *irq_ptr = cdev->private->qdio_data;
	struct subchannel_id schid;
	struct ciw *ciw;
	long timeout;
	int rc;

	ccw_device_get_schid(cdev, &schid);
	DBF_EVENT("qestablish:%4x", schid.sch_no);

	if (!irq_ptr)
		return -ENODEV;

	if (init_data->no_input_qs > irq_ptr->max_input_qs ||
	    init_data->no_output_qs > irq_ptr->max_output_qs)
		return -EINVAL;

	/* Needed as error_handler: */
	if (!init_data->input_handler)
		return -EINVAL;

	if (init_data->no_output_qs && !init_data->output_handler)
		return -EINVAL;

	if (!init_data->input_sbal_addr_array ||
	    !init_data->output_sbal_addr_array)
		return -EINVAL;

	if (!init_data->irq_poll)
		return -EINVAL;

	ciw = ccw_device_get_ciw(cdev, CIW_TYPE_EQUEUE);
	if (!ciw) {
		DBF_ERROR("%4x NO EQ", schid.sch_no);
		return -EIO;
	}

	mutex_lock(&irq_ptr->setup_mutex);
	qdio_trace_init_data(irq_ptr, init_data);
	qdio_setup_irq(irq_ptr, init_data);

	rc = qdio_establish_thinint(irq_ptr);
	if (rc)
		goto err_thinint;

	/* establish q */
	irq_ptr->ccw->cmd_code = ciw->cmd;
	irq_ptr->ccw->flags = CCW_FLAG_SLI;
	irq_ptr->ccw->count = ciw->count;
	irq_ptr->ccw->cda = virt_to_dma32(irq_ptr->qdr);

	spin_lock_irq(get_ccwdev_lock(cdev));
	ccw_device_set_options_mask(cdev, 0);

	rc = ccw_device_start(cdev, irq_ptr->ccw, QDIO_DOING_ESTABLISH, 0, 0);
	spin_unlock_irq(get_ccwdev_lock(cdev));
	if (rc) {
		DBF_ERROR("%4x est IO ERR", irq_ptr->schid.sch_no);
		DBF_ERROR("rc:%4x", rc);
		goto err_ccw_start;
	}

	timeout = wait_event_interruptible_timeout(cdev->private->wait_q,
						   irq_ptr->state == QDIO_IRQ_STATE_ESTABLISHED ||
						   irq_ptr->state == QDIO_IRQ_STATE_ERR, HZ);
	if (timeout <= 0) {
		rc = (timeout == -ERESTARTSYS) ? -EINTR : -ETIME;
		goto err_ccw_timeout;
	}

	if (irq_ptr->state != QDIO_IRQ_STATE_ESTABLISHED) {
		rc = -EIO;
		goto err_ccw_error;
	}

	qdio_setup_ssqd_info(irq_ptr);

	/* qebsm is now setup if available, initialize buffer states */
	qdio_init_buf_states(irq_ptr);

	mutex_unlock(&irq_ptr->setup_mutex);
	qdio_print_subchannel_info(irq_ptr);
	qdio_setup_debug_entries(irq_ptr);
	return 0;

err_ccw_timeout:
	qdio_cancel_ccw(irq_ptr, QDIO_FLAG_CLEANUP_USING_CLEAR);
err_ccw_error:
err_ccw_start:
	qdio_shutdown_thinint(irq_ptr);
err_thinint:
	qdio_shutdown_irq(irq_ptr);
	qdio_set_state(irq_ptr, QDIO_IRQ_STATE_INACTIVE);
	mutex_unlock(&irq_ptr->setup_mutex);
	return rc;
}
EXPORT_SYMBOL_GPL(qdio_establish);

/**
 * qdio_activate - activate queues on a qdio subchannel
 * @cdev: associated cdev
 */
int qdio_activate(struct ccw_device *cdev)
{
	struct qdio_irq *irq_ptr = cdev->private->qdio_data;
	struct subchannel_id schid;
	struct ciw *ciw;
	int rc;

	ccw_device_get_schid(cdev, &schid);
	DBF_EVENT("qactivate:%4x", schid.sch_no);

	if (!irq_ptr)
		return -ENODEV;

	ciw = ccw_device_get_ciw(cdev, CIW_TYPE_AQUEUE);
	if (!ciw) {
		DBF_ERROR("%4x NO AQ", schid.sch_no);
		return -EIO;
	}

	mutex_lock(&irq_ptr->setup_mutex);
	if (irq_ptr->state == QDIO_IRQ_STATE_INACTIVE) {
		rc = -EBUSY;
		goto out;
	}

	irq_ptr->ccw->cmd_code = ciw->cmd;
	irq_ptr->ccw->flags = CCW_FLAG_SLI;
	irq_ptr->ccw->count = ciw->count;
	irq_ptr->ccw->cda = 0;

	spin_lock_irq(get_ccwdev_lock(cdev));
	ccw_device_set_options(cdev, CCWDEV_REPORT_ALL);

	rc = ccw_device_start(cdev, irq_ptr->ccw, QDIO_DOING_ACTIVATE,
			      0, DOIO_DENY_PREFETCH);
	spin_unlock_irq(get_ccwdev_lock(cdev));
	if (rc) {
		DBF_ERROR("%4x act IO ERR", irq_ptr->schid.sch_no);
		DBF_ERROR("rc:%4x", rc);
		goto out;
	}

	/* wait for subchannel to become active */
	msleep(5);

	switch (irq_ptr->state) {
	case QDIO_IRQ_STATE_STOPPED:
	case QDIO_IRQ_STATE_ERR:
		rc = -EIO;
		break;
	default:
		qdio_set_state(irq_ptr, QDIO_IRQ_STATE_ACTIVE);
		rc = 0;
	}
out:
	mutex_unlock(&irq_ptr->setup_mutex);
	return rc;
}
EXPORT_SYMBOL_GPL(qdio_activate);

/**
 * handle_inbound - reset processed input buffers
 * @q: queue containing the buffers
 * @bufnr: first buffer to process
 * @count: how many buffers are emptied
 */
static int handle_inbound(struct qdio_q *q, int bufnr, int count)
{
	int overlap;

	qperf_inc(q, inbound_call);

	/* If any processed SBALs are returned to HW, adjust our tracking: */
	overlap = min_t(int, count - sub_buf(q->u.in.batch_start, bufnr),
			     q->u.in.batch_count);
	if (overlap > 0) {
		q->u.in.batch_start = add_buf(q->u.in.batch_start, overlap);
		q->u.in.batch_count -= overlap;
	}

	count = set_buf_states(q, bufnr, SLSB_CU_INPUT_EMPTY, count);
	atomic_add(count, &q->nr_buf_used);

	if (qdio_need_siga_in(q->irq_ptr))
		return qdio_siga_input(q);

	return 0;
}

/**
 * qdio_add_bufs_to_input_queue - process buffers on an Input Queue
 * @cdev: associated ccw_device for the qdio subchannel
 * @q_nr: queue number
 * @bufnr: buffer number
 * @count: how many buffers to process
 */
int qdio_add_bufs_to_input_queue(struct ccw_device *cdev, unsigned int q_nr,
				 unsigned int bufnr, unsigned int count)
{
	struct qdio_irq *irq_ptr = cdev->private->qdio_data;

	if (bufnr >= QDIO_MAX_BUFFERS_PER_Q || count > QDIO_MAX_BUFFERS_PER_Q)
		return -EINVAL;

	if (!irq_ptr)
		return -ENODEV;

	DBF_DEV_EVENT(DBF_INFO, irq_ptr, "addi b:%02x c:%02x", bufnr, count);

	if (irq_ptr->state != QDIO_IRQ_STATE_ACTIVE)
		return -EIO;
	if (!count)
		return 0;

	return handle_inbound(irq_ptr->input_qs[q_nr], bufnr, count);
}
EXPORT_SYMBOL_GPL(qdio_add_bufs_to_input_queue);

/**
 * handle_outbound - process filled outbound buffers
 * @q: queue containing the buffers
 * @bufnr: first buffer to process
 * @count: how many buffers are filled
 * @aob: asynchronous operation block
 */
static int handle_outbound(struct qdio_q *q, unsigned int bufnr, unsigned int count,
			   struct qaob *aob)
{
	unsigned char state = 0;
	int used, rc = 0;

	qperf_inc(q, outbound_call);

	count = set_buf_states(q, bufnr, SLSB_CU_OUTPUT_PRIMED, count);
	used = atomic_add_return(count, &q->nr_buf_used);

	if (used == QDIO_MAX_BUFFERS_PER_Q)
		qperf_inc(q, outbound_queue_full);

	if (queue_type(q) == QDIO_IQDIO_QFMT) {
		dma64_t phys_aob = aob ? virt_to_dma64(aob) : 0;

		WARN_ON_ONCE(!IS_ALIGNED(dma64_to_u64(phys_aob), 256));
		rc = qdio_kick_outbound_q(q, count, phys_aob);
	} else if (qdio_need_siga_sync(q->irq_ptr)) {
		rc = qdio_sync_output_queue(q);
	} else if (count < QDIO_MAX_BUFFERS_PER_Q &&
		   get_buf_state(q, prev_buf(bufnr), &state, 0) > 0 &&
		   state == SLSB_CU_OUTPUT_PRIMED) {
		/* The previous buffer is not processed yet, tack on. */
		qperf_inc(q, fast_requeue);
	} else {
		rc = qdio_kick_outbound_q(q, count, 0);
	}

	return rc;
}

/**
 * qdio_add_bufs_to_output_queue - process buffers on an Output Queue
 * @cdev: associated ccw_device for the qdio subchannel
 * @q_nr: queue number
 * @bufnr: buffer number
 * @count: how many buffers to process
 * @aob: asynchronous operation block
 */
int qdio_add_bufs_to_output_queue(struct ccw_device *cdev, unsigned int q_nr,
				  unsigned int bufnr, unsigned int count,
				  struct qaob *aob)
{
	struct qdio_irq *irq_ptr = cdev->private->qdio_data;

	if (bufnr >= QDIO_MAX_BUFFERS_PER_Q || count > QDIO_MAX_BUFFERS_PER_Q)
		return -EINVAL;

	if (!irq_ptr)
		return -ENODEV;

	DBF_DEV_EVENT(DBF_INFO, irq_ptr, "addo b:%02x c:%02x", bufnr, count);

	if (irq_ptr->state != QDIO_IRQ_STATE_ACTIVE)
		return -EIO;
	if (!count)
		return 0;

	return handle_outbound(irq_ptr->output_qs[q_nr], bufnr, count, aob);
}
EXPORT_SYMBOL_GPL(qdio_add_bufs_to_output_queue);

/**
 * qdio_start_irq - enable interrupt processing for the device
 * @cdev: associated ccw_device for the qdio subchannel
 *
 * Return codes
 *   0 - success
 *   1 - irqs not started since new data is available
 */
int qdio_start_irq(struct ccw_device *cdev)
{
	struct qdio_q *q;
	struct qdio_irq *irq_ptr = cdev->private->qdio_data;
	unsigned int i;

	if (!irq_ptr)
		return -ENODEV;

	for_each_input_queue(irq_ptr, q, i)
		qdio_stop_polling(q);

	clear_bit(QDIO_IRQ_DISABLED, &irq_ptr->poll_state);

	/*
	 * We need to check again to not lose initiative after
	 * resetting the ACK state.
	 */
	if (test_nonshared_ind(irq_ptr))
		goto rescan;

	for_each_input_queue(irq_ptr, q, i) {
		if (!qdio_inbound_q_done(q, q->first_to_check))
			goto rescan;
	}

	return 0;

rescan:
	if (test_and_set_bit(QDIO_IRQ_DISABLED, &irq_ptr->poll_state))
		return 0;
	else
		return 1;

}
EXPORT_SYMBOL(qdio_start_irq);

/**
 * qdio_stop_irq - disable interrupt processing for the device
 * @cdev: associated ccw_device for the qdio subchannel
 *
 * Return codes
 *   0 - interrupts were already disabled
 *   1 - interrupts successfully disabled
 */
int qdio_stop_irq(struct ccw_device *cdev)
{
	struct qdio_irq *irq_ptr = cdev->private->qdio_data;

	if (!irq_ptr)
		return -ENODEV;

	if (test_and_set_bit(QDIO_IRQ_DISABLED, &irq_ptr->poll_state))
		return 0;
	else
		return 1;
}
EXPORT_SYMBOL(qdio_stop_irq);

static int __init init_QDIO(void)
{
	int rc;

	rc = qdio_debug_init();
	if (rc)
		return rc;
	rc = qdio_setup_init();
	if (rc)
		goto out_debug;
	rc = qdio_thinint_init();
	if (rc)
		goto out_cache;
	return 0;

out_cache:
	qdio_setup_exit();
out_debug:
	qdio_debug_exit();
	return rc;
}

static void __exit exit_QDIO(void)
{
	qdio_thinint_exit();
	qdio_setup_exit();
	qdio_debug_exit();
}

module_init(init_QDIO);
module_exit(exit_QDIO);

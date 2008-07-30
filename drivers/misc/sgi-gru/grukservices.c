/*
 * SN Platform GRU Driver
 *
 *              KERNEL SERVICES THAT USE THE GRU
 *
 *  Copyright (c) 2008 Silicon Graphics, Inc.  All Rights Reserved.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/smp_lock.h>
#include <linux/spinlock.h>
#include <linux/device.h>
#include <linux/miscdevice.h>
#include <linux/proc_fs.h>
#include <linux/interrupt.h>
#include <linux/uaccess.h>
#include "gru.h"
#include "grulib.h"
#include "grutables.h"
#include "grukservices.h"
#include "gru_instructions.h"
#include <asm/uv/uv_hub.h>

/*
 * Kernel GRU Usage
 *
 * The following is an interim algorithm for management of kernel GRU
 * resources. This will likely be replaced when we better understand the
 * kernel/user requirements.
 *
 * At boot time, the kernel permanently reserves a fixed number of
 * CBRs/DSRs for each cpu to use. The resources are all taken from
 * the GRU chiplet 1 on the blade. This leaves the full set of resources
 * of chiplet 0 available to be allocated to a single user.
 */

/* Blade percpu resources PERMANENTLY reserved for kernel use */
#define GRU_NUM_KERNEL_CBR      1
#define GRU_NUM_KERNEL_DSR_BYTES 256
#define KERNEL_CTXNUM           15

/* GRU instruction attributes for all instructions */
#define IMA			IMA_CB_DELAY

/* GRU cacheline size is always 64 bytes - even on arches with 128 byte lines */
#define __gru_cacheline_aligned__                               \
	__attribute__((__aligned__(GRU_CACHE_LINE_BYTES)))

#define MAGIC	0x1234567887654321UL

/* Default retry count for GRU errors on kernel instructions */
#define EXCEPTION_RETRY_LIMIT	3

/* Status of message queue sections */
#define MQS_EMPTY		0
#define MQS_FULL		1
#define MQS_NOOP		2

/*----------------- RESOURCE MANAGEMENT -------------------------------------*/
/* optimized for x86_64 */
struct message_queue {
	union gru_mesqhead	head __gru_cacheline_aligned__;	/* CL 0 */
	int			qlines;				/* DW 1 */
	long 			hstatus[2];
	void 			*next __gru_cacheline_aligned__;/* CL 1 */
	void 			*limit;
	void 			*start;
	void 			*start2;
	char			data ____cacheline_aligned;	/* CL 2 */
};

/* First word in every message - used by mesq interface */
struct message_header {
	char	present;
	char	present2;
	char 	lines;
	char	fill;
};

#define QLINES(mq)	((mq) + offsetof(struct message_queue, qlines))
#define HSTATUS(mq, h)	((mq) + offsetof(struct message_queue, hstatus[h]))

static int gru_get_cpu_resources(int dsr_bytes, void **cb, void **dsr)
{
	struct gru_blade_state *bs;
	int lcpu;

	BUG_ON(dsr_bytes > GRU_NUM_KERNEL_DSR_BYTES);
	preempt_disable();
	bs = gru_base[uv_numa_blade_id()];
	lcpu = uv_blade_processor_id();
	*cb = bs->kernel_cb + lcpu * GRU_HANDLE_STRIDE;
	*dsr = bs->kernel_dsr + lcpu * GRU_NUM_KERNEL_DSR_BYTES;
	return 0;
}

static void gru_free_cpu_resources(void *cb, void *dsr)
{
	preempt_enable();
}

int gru_get_cb_exception_detail(void *cb,
		struct control_block_extended_exc_detail *excdet)
{
	struct gru_control_block_extended *cbe;

	cbe = get_cbe(GRUBASE(cb), get_cb_number(cb));
	excdet->opc = cbe->opccpy;
	excdet->exopc = cbe->exopccpy;
	excdet->ecause = cbe->ecause;
	excdet->exceptdet0 = cbe->idef1upd;
	excdet->exceptdet1 = cbe->idef3upd;
	return 0;
}

char *gru_get_cb_exception_detail_str(int ret, void *cb,
				      char *buf, int size)
{
	struct gru_control_block_status *gen = (void *)cb;
	struct control_block_extended_exc_detail excdet;

	if (ret > 0 && gen->istatus == CBS_EXCEPTION) {
		gru_get_cb_exception_detail(cb, &excdet);
		snprintf(buf, size,
			"GRU exception: cb %p, opc %d, exopc %d, ecause 0x%x,"
			"excdet0 0x%lx, excdet1 0x%x",
			gen, excdet.opc, excdet.exopc, excdet.ecause,
			excdet.exceptdet0, excdet.exceptdet1);
	} else {
		snprintf(buf, size, "No exception");
	}
	return buf;
}

static int gru_wait_idle_or_exception(struct gru_control_block_status *gen)
{
	while (gen->istatus >= CBS_ACTIVE) {
		cpu_relax();
		barrier();
	}
	return gen->istatus;
}

static int gru_retry_exception(void *cb)
{
	struct gru_control_block_status *gen = (void *)cb;
	struct control_block_extended_exc_detail excdet;
	int retry = EXCEPTION_RETRY_LIMIT;

	while (1)  {
		if (gru_get_cb_message_queue_substatus(cb))
			break;
		if (gru_wait_idle_or_exception(gen) == CBS_IDLE)
			return CBS_IDLE;

		gru_get_cb_exception_detail(cb, &excdet);
		if (excdet.ecause & ~EXCEPTION_RETRY_BITS)
			break;
		if (retry-- == 0)
			break;
		gen->icmd = 1;
		gru_flush_cache(gen);
	}
	return CBS_EXCEPTION;
}

int gru_check_status_proc(void *cb)
{
	struct gru_control_block_status *gen = (void *)cb;
	int ret;

	ret = gen->istatus;
	if (ret != CBS_EXCEPTION)
		return ret;
	return gru_retry_exception(cb);

}

int gru_wait_proc(void *cb)
{
	struct gru_control_block_status *gen = (void *)cb;
	int ret;

	ret = gru_wait_idle_or_exception(gen);
	if (ret == CBS_EXCEPTION)
		ret = gru_retry_exception(cb);

	return ret;
}

void gru_abort(int ret, void *cb, char *str)
{
	char buf[GRU_EXC_STR_SIZE];

	panic("GRU FATAL ERROR: %s - %s\n", str,
	      gru_get_cb_exception_detail_str(ret, cb, buf, sizeof(buf)));
}

void gru_wait_abort_proc(void *cb)
{
	int ret;

	ret = gru_wait_proc(cb);
	if (ret)
		gru_abort(ret, cb, "gru_wait_abort");
}


/*------------------------------ MESSAGE QUEUES -----------------------------*/

/* Internal status . These are NOT returned to the user. */
#define MQIE_AGAIN		-1	/* try again */


/*
 * Save/restore the "present" flag that is in the second line of 2-line
 * messages
 */
static inline int get_present2(void *p)
{
	struct message_header *mhdr = p + GRU_CACHE_LINE_BYTES;
	return mhdr->present;
}

static inline void restore_present2(void *p, int val)
{
	struct message_header *mhdr = p + GRU_CACHE_LINE_BYTES;
	mhdr->present = val;
}

/*
 * Create a message queue.
 * 	qlines - message queue size in cache lines. Includes 2-line header.
 */
int gru_create_message_queue(void *p, unsigned int bytes)
{
	struct message_queue *mq = p;
	unsigned int qlines;

	qlines = bytes / GRU_CACHE_LINE_BYTES - 2;
	memset(mq, 0, bytes);
	mq->start = &mq->data;
	mq->start2 = &mq->data + (qlines / 2 - 1) * GRU_CACHE_LINE_BYTES;
	mq->next = &mq->data;
	mq->limit = &mq->data + (qlines - 2) * GRU_CACHE_LINE_BYTES;
	mq->qlines = qlines;
	mq->hstatus[0] = 0;
	mq->hstatus[1] = 1;
	mq->head = gru_mesq_head(2, qlines / 2 + 1);
	return 0;
}
EXPORT_SYMBOL_GPL(gru_create_message_queue);

/*
 * Send a NOOP message to a message queue
 * 	Returns:
 * 		 0 - if queue is full after the send. This is the normal case
 * 		     but various races can change this.
 *		-1 - if mesq sent successfully but queue not full
 *		>0 - unexpected error. MQE_xxx returned
 */
static int send_noop_message(void *cb,
				unsigned long mq, void *mesg)
{
	const struct message_header noop_header = {
					.present = MQS_NOOP, .lines = 1};
	unsigned long m;
	int substatus, ret;
	struct message_header save_mhdr, *mhdr = mesg;

	STAT(mesq_noop);
	save_mhdr = *mhdr;
	*mhdr = noop_header;
	gru_mesq(cb, mq, gru_get_tri(mhdr), 1, IMA);
	ret = gru_wait(cb);

	if (ret) {
		substatus = gru_get_cb_message_queue_substatus(cb);
		switch (substatus) {
		case CBSS_NO_ERROR:
			STAT(mesq_noop_unexpected_error);
			ret = MQE_UNEXPECTED_CB_ERR;
			break;
		case CBSS_LB_OVERFLOWED:
			STAT(mesq_noop_lb_overflow);
			ret = MQE_CONGESTION;
			break;
		case CBSS_QLIMIT_REACHED:
			STAT(mesq_noop_qlimit_reached);
			ret = 0;
			break;
		case CBSS_AMO_NACKED:
			STAT(mesq_noop_amo_nacked);
			ret = MQE_CONGESTION;
			break;
		case CBSS_PUT_NACKED:
			STAT(mesq_noop_put_nacked);
			m = mq + (gru_get_amo_value_head(cb) << 6);
			gru_vstore(cb, m, gru_get_tri(mesg), XTYPE_CL, 1, 1,
						IMA);
			if (gru_wait(cb) == CBS_IDLE)
				ret = MQIE_AGAIN;
			else
				ret = MQE_UNEXPECTED_CB_ERR;
			break;
		case CBSS_PAGE_OVERFLOW:
		default:
			BUG();
		}
	}
	*mhdr = save_mhdr;
	return ret;
}

/*
 * Handle a gru_mesq full.
 */
static int send_message_queue_full(void *cb,
			   unsigned long mq, void *mesg, int lines)
{
	union gru_mesqhead mqh;
	unsigned int limit, head;
	unsigned long avalue;
	int half, qlines, save;

	/* Determine if switching to first/second half of q */
	avalue = gru_get_amo_value(cb);
	head = gru_get_amo_value_head(cb);
	limit = gru_get_amo_value_limit(cb);

	/*
	 * Fetch "qlines" from the queue header. Since the queue may be
	 * in memory that can't be accessed using socket addresses, use
	 * the GRU to access the data. Use DSR space from the message.
	 */
	save = *(int *)mesg;
	gru_vload(cb, QLINES(mq), gru_get_tri(mesg), XTYPE_W, 1, 1, IMA);
	if (gru_wait(cb) != CBS_IDLE)
		goto cberr;
	qlines = *(int *)mesg;
	*(int *)mesg = save;
	half = (limit != qlines);

	if (half)
		mqh = gru_mesq_head(qlines / 2 + 1, qlines);
	else
		mqh = gru_mesq_head(2, qlines / 2 + 1);

	/* Try to get lock for switching head pointer */
	gru_gamir(cb, EOP_IR_CLR, HSTATUS(mq, half), XTYPE_DW, IMA);
	if (gru_wait(cb) != CBS_IDLE)
		goto cberr;
	if (!gru_get_amo_value(cb)) {
		STAT(mesq_qf_locked);
		return MQE_QUEUE_FULL;
	}

	/* Got the lock. Send optional NOP if queue not full, */
	if (head != limit) {
		if (send_noop_message(cb, mq, mesg)) {
			gru_gamir(cb, EOP_IR_INC, HSTATUS(mq, half),
					XTYPE_DW, IMA);
			if (gru_wait(cb) != CBS_IDLE)
				goto cberr;
			STAT(mesq_qf_noop_not_full);
			return MQIE_AGAIN;
		}
		avalue++;
	}

	/* Then flip queuehead to other half of queue. */
	gru_gamer(cb, EOP_ERR_CSWAP, mq, XTYPE_DW, mqh.val, avalue, IMA);
	if (gru_wait(cb) != CBS_IDLE)
		goto cberr;

	/* If not successfully in swapping queue head, clear the hstatus lock */
	if (gru_get_amo_value(cb) != avalue) {
		STAT(mesq_qf_switch_head_failed);
		gru_gamir(cb, EOP_IR_INC, HSTATUS(mq, half), XTYPE_DW, IMA);
		if (gru_wait(cb) != CBS_IDLE)
			goto cberr;
	}
	return MQIE_AGAIN;
cberr:
	STAT(mesq_qf_unexpected_error);
	return MQE_UNEXPECTED_CB_ERR;
}


/*
 * Handle a gru_mesq failure. Some of these failures are software recoverable
 * or retryable.
 */
static int send_message_failure(void *cb,
				unsigned long mq,
				void *mesg,
				int lines)
{
	int substatus, ret = 0;
	unsigned long m;

	substatus = gru_get_cb_message_queue_substatus(cb);
	switch (substatus) {
	case CBSS_NO_ERROR:
		STAT(mesq_send_unexpected_error);
		ret = MQE_UNEXPECTED_CB_ERR;
		break;
	case CBSS_LB_OVERFLOWED:
		STAT(mesq_send_lb_overflow);
		ret = MQE_CONGESTION;
		break;
	case CBSS_QLIMIT_REACHED:
		STAT(mesq_send_qlimit_reached);
		ret = send_message_queue_full(cb, mq, mesg, lines);
		break;
	case CBSS_AMO_NACKED:
		STAT(mesq_send_amo_nacked);
		ret = MQE_CONGESTION;
		break;
	case CBSS_PUT_NACKED:
		STAT(mesq_send_put_nacked);
		m =mq + (gru_get_amo_value_head(cb) << 6);
		gru_vstore(cb, m, gru_get_tri(mesg), XTYPE_CL, lines, 1, IMA);
		if (gru_wait(cb) == CBS_IDLE)
			ret = MQE_OK;
		else
			ret = MQE_UNEXPECTED_CB_ERR;
		break;
	default:
		BUG();
	}
	return ret;
}

/*
 * Send a message to a message queue
 * 	cb	GRU control block to use to send message
 * 	mq	message queue
 * 	mesg	message. ust be vaddr within a GSEG
 * 	bytes	message size (<= 2 CL)
 */
int gru_send_message_gpa(unsigned long mq, void *mesg, unsigned int bytes)
{
	struct message_header *mhdr;
	void *cb;
	void *dsr;
	int istatus, clines, ret;

	STAT(mesq_send);
	BUG_ON(bytes < sizeof(int) || bytes > 2 * GRU_CACHE_LINE_BYTES);

	clines = (bytes + GRU_CACHE_LINE_BYTES - 1) / GRU_CACHE_LINE_BYTES;
	if (gru_get_cpu_resources(bytes, &cb, &dsr))
		return MQE_BUG_NO_RESOURCES;
	memcpy(dsr, mesg, bytes);
	mhdr = dsr;
	mhdr->present = MQS_FULL;
	mhdr->lines = clines;
	if (clines == 2) {
		mhdr->present2 = get_present2(mhdr);
		restore_present2(mhdr, MQS_FULL);
	}

	do {
		ret = MQE_OK;
		gru_mesq(cb, mq, gru_get_tri(mhdr), clines, IMA);
		istatus = gru_wait(cb);
		if (istatus != CBS_IDLE)
			ret = send_message_failure(cb, mq, dsr, clines);
	} while (ret == MQIE_AGAIN);
	gru_free_cpu_resources(cb, dsr);

	if (ret)
		STAT(mesq_send_failed);
	return ret;
}
EXPORT_SYMBOL_GPL(gru_send_message_gpa);

/*
 * Advance the receive pointer for the queue to the next message.
 */
void gru_free_message(void *rmq, void *mesg)
{
	struct message_queue *mq = rmq;
	struct message_header *mhdr = mq->next;
	void *next, *pnext;
	int half = -1;
	int lines = mhdr->lines;

	if (lines == 2)
		restore_present2(mhdr, MQS_EMPTY);
	mhdr->present = MQS_EMPTY;

	pnext = mq->next;
	next = pnext + GRU_CACHE_LINE_BYTES * lines;
	if (next == mq->limit) {
		next = mq->start;
		half = 1;
	} else if (pnext < mq->start2 && next >= mq->start2) {
		half = 0;
	}

	if (half >= 0)
		mq->hstatus[half] = 1;
	mq->next = next;
}
EXPORT_SYMBOL_GPL(gru_free_message);

/*
 * Get next message from message queue. Return NULL if no message
 * present. User must call next_message() to move to next message.
 * 	rmq	message queue
 */
void *gru_get_next_message(void *rmq)
{
	struct message_queue *mq = rmq;
	struct message_header *mhdr = mq->next;
	int present = mhdr->present;

	/* skip NOOP messages */
	STAT(mesq_receive);
	while (present == MQS_NOOP) {
		gru_free_message(rmq, mhdr);
		mhdr = mq->next;
		present = mhdr->present;
	}

	/* Wait for both halves of 2 line messages */
	if (present == MQS_FULL && mhdr->lines == 2 &&
				get_present2(mhdr) == MQS_EMPTY)
		present = MQS_EMPTY;

	if (!present) {
		STAT(mesq_receive_none);
		return NULL;
	}

	if (mhdr->lines == 2)
		restore_present2(mhdr, mhdr->present2);

	return mhdr;
}
EXPORT_SYMBOL_GPL(gru_get_next_message);

/* ---------------------- GRU DATA COPY FUNCTIONS ---------------------------*/

/*
 * Copy a block of data using the GRU resources
 */
int gru_copy_gpa(unsigned long dest_gpa, unsigned long src_gpa,
				unsigned int bytes)
{
	void *cb;
	void *dsr;
	int ret;

	STAT(copy_gpa);
	if (gru_get_cpu_resources(GRU_NUM_KERNEL_DSR_BYTES, &cb, &dsr))
		return MQE_BUG_NO_RESOURCES;
	gru_bcopy(cb, src_gpa, dest_gpa, gru_get_tri(dsr),
		  XTYPE_B, bytes, GRU_NUM_KERNEL_DSR_BYTES, IMA);
	ret = gru_wait(cb);
	gru_free_cpu_resources(cb, dsr);
	return ret;
}
EXPORT_SYMBOL_GPL(gru_copy_gpa);

/* ------------------- KERNEL QUICKTESTS RUN AT STARTUP ----------------*/
/* 	Temp - will delete after we gain confidence in the GRU		*/
static __cacheline_aligned unsigned long word0;
static __cacheline_aligned unsigned long word1;

static int quicktest(struct gru_state *gru)
{
	void *cb;
	void *ds;
	unsigned long *p;

	cb = get_gseg_base_address_cb(gru->gs_gru_base_vaddr, KERNEL_CTXNUM, 0);
	ds = get_gseg_base_address_ds(gru->gs_gru_base_vaddr, KERNEL_CTXNUM, 0);
	p = ds;
	word0 = MAGIC;

	gru_vload(cb, uv_gpa(&word0), 0, XTYPE_DW, 1, 1, IMA);
	if (gru_wait(cb) != CBS_IDLE)
		BUG();

	if (*(unsigned long *)ds != MAGIC)
		BUG();
	gru_vstore(cb, uv_gpa(&word1), 0, XTYPE_DW, 1, 1, IMA);
	if (gru_wait(cb) != CBS_IDLE)
		BUG();

	if (word0 != word1 || word0 != MAGIC) {
		printk
		    ("GRU quicktest err: gru %d, found 0x%lx, expected 0x%lx\n",
		     gru->gs_gid, word1, MAGIC);
		BUG();		/* ZZZ should not be fatal */
	}

	return 0;
}


int gru_kservices_init(struct gru_state *gru)
{
	struct gru_blade_state *bs;
	struct gru_context_configuration_handle *cch;
	unsigned long cbr_map, dsr_map;
	int err, num, cpus_possible;

	/*
	 * Currently, resources are reserved ONLY on the second chiplet
	 * on each blade. This leaves ALL resources on chiplet 0 available
	 * for user code.
	 */
	bs = gru->gs_blade;
	if (gru != &bs->bs_grus[1])
		return 0;

	cpus_possible = uv_blade_nr_possible_cpus(gru->gs_blade_id);

	num = GRU_NUM_KERNEL_CBR * cpus_possible;
	cbr_map = gru_reserve_cb_resources(gru, GRU_CB_COUNT_TO_AU(num), NULL);
	gru->gs_reserved_cbrs += num;

	num = GRU_NUM_KERNEL_DSR_BYTES * cpus_possible;
	dsr_map = gru_reserve_ds_resources(gru, GRU_DS_BYTES_TO_AU(num), NULL);
	gru->gs_reserved_dsr_bytes += num;

	gru->gs_active_contexts++;
	__set_bit(KERNEL_CTXNUM, &gru->gs_context_map);
	cch = get_cch(gru->gs_gru_base_vaddr, KERNEL_CTXNUM);

	bs->kernel_cb = get_gseg_base_address_cb(gru->gs_gru_base_vaddr,
					KERNEL_CTXNUM, 0);
	bs->kernel_dsr = get_gseg_base_address_ds(gru->gs_gru_base_vaddr,
					KERNEL_CTXNUM, 0);

	lock_cch_handle(cch);
	cch->tfm_fault_bit_enable = 0;
	cch->tlb_int_enable = 0;
	cch->tfm_done_bit_enable = 0;
	cch->unmap_enable = 1;
	err = cch_allocate(cch, 0, cbr_map, dsr_map);
	if (err) {
		gru_dbg(grudev,
			"Unable to allocate kernel CCH: gru %d, err %d\n",
			gru->gs_gid, err);
		BUG();
	}
	if (cch_start(cch)) {
		gru_dbg(grudev, "Unable to start kernel CCH: gru %d, err %d\n",
			gru->gs_gid, err);
		BUG();
	}
	unlock_cch_handle(cch);

	if (gru_options & GRU_QUICKLOOK)
		quicktest(gru);
	return 0;
}

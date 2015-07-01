/**********************************************************************
 * Author: Cavium, Inc.
 *
 * Contact: support@cavium.com
 *          Please include "LiquidIO" in the subject.
 *
 * Copyright (c) 2003-2015 Cavium, Inc.
 *
 * This file is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, Version 2, as
 * published by the Free Software Foundation.
 *
 * This file is distributed in the hope that it will be useful, but
 * AS-IS and WITHOUT ANY WARRANTY; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE, TITLE, or
 * NONINFRINGEMENT.  See the GNU General Public License for more
 * details.
 *
 * This file may also be available under a different license from Cavium.
 * Contact Cavium, Inc. for more information
 **********************************************************************/
#include <linux/version.h>
#include <linux/types.h>
#include <linux/list.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/kthread.h>
#include <linux/netdevice.h>
#include <linux/vmalloc.h>
#include "octeon_config.h"
#include "liquidio_common.h"
#include "octeon_droq.h"
#include "octeon_iq.h"
#include "response_manager.h"
#include "octeon_device.h"
#include "octeon_nic.h"
#include "octeon_main.h"
#include "octeon_network.h"
#include "cn66xx_regs.h"
#include "cn66xx_device.h"
#include "cn68xx_regs.h"
#include "cn68xx_device.h"
#include "liquidio_image.h"

#define INCR_INSTRQUEUE_PKT_COUNT(octeon_dev_ptr, iq_no, field, count)  \
	(octeon_dev_ptr->instr_queue[iq_no]->stats.field += count)

struct iq_post_status {
	int status;
	int index;
};

static void check_db_timeout(struct work_struct *work);
static void  __check_db_timeout(struct octeon_device *oct, unsigned long iq_no);

static void (*reqtype_free_fn[MAX_OCTEON_DEVICES][REQTYPE_LAST + 1]) (void *);

static inline int IQ_INSTR_MODE_64B(struct octeon_device *oct, int iq_no)
{
	struct octeon_instr_queue *iq =
	    (struct octeon_instr_queue *)oct->instr_queue[iq_no];
	return iq->iqcmd_64B;
}

#define IQ_INSTR_MODE_32B(oct, iq_no)  (!IQ_INSTR_MODE_64B(oct, iq_no))

/* Define this to return the request status comaptible to old code */
/*#define OCTEON_USE_OLD_REQ_STATUS*/

/* Return 0 on success, 1 on failure */
int octeon_init_instr_queue(struct octeon_device *oct,
			    u32 iq_no, u32 num_descs)
{
	struct octeon_instr_queue *iq;
	struct octeon_iq_config *conf = NULL;
	u32 q_size;
	struct cavium_wq *db_wq;

	if (OCTEON_CN6XXX(oct))
		conf = &(CFG_GET_IQ_CFG(CHIP_FIELD(oct, cn6xxx, conf)));

	if (!conf) {
		dev_err(&oct->pci_dev->dev, "Unsupported Chip %x\n",
			oct->chip_id);
		return 1;
	}

	if (num_descs & (num_descs - 1)) {
		dev_err(&oct->pci_dev->dev,
			"Number of descriptors for instr queue %d not in power of 2.\n",
			iq_no);
		return 1;
	}

	q_size = (u32)conf->instr_type * num_descs;

	iq = oct->instr_queue[iq_no];

	iq->base_addr = lio_dma_alloc(oct, q_size,
				      (dma_addr_t *)&iq->base_addr_dma);
	if (!iq->base_addr) {
		dev_err(&oct->pci_dev->dev, "Cannot allocate memory for instr queue %d\n",
			iq_no);
		return 1;
	}

	iq->max_count = num_descs;

	/* Initialize a list to holds requests that have been posted to Octeon
	 * but has yet to be fetched by octeon
	 */
	iq->request_list = vmalloc(sizeof(*iq->request_list) * num_descs);
	if (!iq->request_list) {
		lio_dma_free(oct, q_size, iq->base_addr, iq->base_addr_dma);
		dev_err(&oct->pci_dev->dev, "Alloc failed for IQ[%d] nr free list\n",
			iq_no);
		return 1;
	}

	memset(iq->request_list, 0, sizeof(*iq->request_list) * num_descs);

	dev_dbg(&oct->pci_dev->dev, "IQ[%d]: base: %p basedma: %llx count: %d\n",
		iq_no, iq->base_addr, iq->base_addr_dma, iq->max_count);

	iq->iq_no = iq_no;
	iq->fill_threshold = (u32)conf->db_min;
	iq->fill_cnt = 0;
	iq->host_write_index = 0;
	iq->octeon_read_index = 0;
	iq->flush_index = 0;
	iq->last_db_time = 0;
	iq->do_auto_flush = 1;
	iq->db_timeout = (u32)conf->db_timeout;
	atomic_set(&iq->instr_pending, 0);

	/* Initialize the spinlock for this instruction queue */
	spin_lock_init(&iq->lock);

	oct->io_qmask.iq |= (1 << iq_no);

	/* Set the 32B/64B mode for each input queue */
	oct->io_qmask.iq64B |= ((conf->instr_type == 64) << iq_no);
	iq->iqcmd_64B = (conf->instr_type == 64);

	oct->fn_list.setup_iq_regs(oct, iq_no);

	oct->check_db_wq[iq_no].wq = create_workqueue("check_iq_db");
	if (!oct->check_db_wq[iq_no].wq) {
		lio_dma_free(oct, q_size, iq->base_addr, iq->base_addr_dma);
		dev_err(&oct->pci_dev->dev, "check db wq create failed for iq %d\n",
			iq_no);
		return 1;
	}

	db_wq = &oct->check_db_wq[iq_no];

	INIT_DELAYED_WORK(&db_wq->wk.work, check_db_timeout);
	db_wq->wk.ctxptr = oct;
	db_wq->wk.ctxul = iq_no;
	queue_delayed_work(db_wq->wq, &db_wq->wk.work, msecs_to_jiffies(1));

	return 0;
}

int octeon_delete_instr_queue(struct octeon_device *oct, u32 iq_no)
{
	u64 desc_size = 0, q_size;
	struct octeon_instr_queue *iq = oct->instr_queue[iq_no];

	cancel_delayed_work_sync(&oct->check_db_wq[iq_no].wk.work);
	flush_workqueue(oct->check_db_wq[iq_no].wq);
	destroy_workqueue(oct->check_db_wq[iq_no].wq);

	if (OCTEON_CN6XXX(oct))
		desc_size =
		    CFG_GET_IQ_INSTR_TYPE(CHIP_FIELD(oct, cn6xxx, conf));

	vfree(iq->request_list);

	if (iq->base_addr) {
		q_size = iq->max_count * desc_size;
		lio_dma_free(oct, (u32)q_size, iq->base_addr,
			     iq->base_addr_dma);
		return 0;
	}
	return 1;
}

/* Return 0 on success, 1 on failure */
int octeon_setup_iq(struct octeon_device *oct,
		    u32 iq_no,
		    u32 num_descs,
		    void *app_ctx)
{
	if (oct->instr_queue[iq_no]) {
		dev_dbg(&oct->pci_dev->dev, "IQ is in use. Cannot create the IQ: %d again\n",
			iq_no);
		oct->instr_queue[iq_no]->app_ctx = app_ctx;
		return 0;
	}
	oct->instr_queue[iq_no] =
	    vmalloc(sizeof(struct octeon_instr_queue));
	if (!oct->instr_queue[iq_no])
		return 1;

	memset(oct->instr_queue[iq_no], 0,
	       sizeof(struct octeon_instr_queue));

	oct->instr_queue[iq_no]->app_ctx = app_ctx;
	if (octeon_init_instr_queue(oct, iq_no, num_descs)) {
		vfree(oct->instr_queue[iq_no]);
		oct->instr_queue[iq_no] = NULL;
		return 1;
	}

	oct->num_iqs++;
	oct->fn_list.enable_io_queues(oct);
	return 0;
}

int lio_wait_for_instr_fetch(struct octeon_device *oct)
{
	int i, retry = 1000, pending, instr_cnt = 0;

	do {
		instr_cnt = 0;

		/*for (i = 0; i < oct->num_iqs; i++) {*/
		for (i = 0; i < MAX_OCTEON_INSTR_QUEUES; i++) {
			if (!(oct->io_qmask.iq & (1UL << i)))
				continue;
			pending =
			    atomic_read(&oct->
					       instr_queue[i]->instr_pending);
			if (pending)
				__check_db_timeout(oct, i);
			instr_cnt += pending;
		}

		if (instr_cnt == 0)
			break;

		schedule_timeout_uninterruptible(1);

	} while (retry-- && instr_cnt);

	return instr_cnt;
}

static inline void
ring_doorbell(struct octeon_device *oct, struct octeon_instr_queue *iq)
{
	if (atomic_read(&oct->status) == OCT_DEV_RUNNING) {
		writel(iq->fill_cnt, iq->doorbell_reg);
		/* make sure doorbell write goes through */
		mmiowb();
		iq->fill_cnt = 0;
		iq->last_db_time = jiffies;
		return;
	}
}

static inline void __copy_cmd_into_iq(struct octeon_instr_queue *iq,
				      u8 *cmd)
{
	u8 *iqptr, cmdsize;

	cmdsize = ((iq->iqcmd_64B) ? 64 : 32);
	iqptr = iq->base_addr + (cmdsize * iq->host_write_index);

	memcpy(iqptr, cmd, cmdsize);
}

static inline int
__post_command(struct octeon_device *octeon_dev __attribute__((unused)),
	       struct octeon_instr_queue *iq,
	       u32 force_db __attribute__((unused)), u8 *cmd)
{
	u32 index = -1;

	/* This ensures that the read index does not wrap around to the same
	 * position if queue gets full before Octeon could fetch any instr.
	 */
	if (atomic_read(&iq->instr_pending) >= (s32)(iq->max_count - 1))
		return -1;

	__copy_cmd_into_iq(iq, cmd);

	/* "index" is returned, host_write_index is modified. */
	index = iq->host_write_index;
	INCR_INDEX_BY1(iq->host_write_index, iq->max_count);
	iq->fill_cnt++;

	/* Flush the command into memory. We need to be sure the data is in
	 * memory before indicating that the instruction is pending.
	 */
	wmb();

	atomic_inc(&iq->instr_pending);

	return index;
}

static inline struct iq_post_status
__post_command2(struct octeon_device *octeon_dev __attribute__((unused)),
		struct octeon_instr_queue *iq,
		u32 force_db __attribute__((unused)), u8 *cmd)
{
	struct iq_post_status st;

	st.status = IQ_SEND_OK;

	/* This ensures that the read index does not wrap around to the same
	 * position if queue gets full before Octeon could fetch any instr.
	 */
	if (atomic_read(&iq->instr_pending) >= (s32)(iq->max_count - 1)) {
		st.status = IQ_SEND_FAILED;
		st.index = -1;
		return st;
	}

	if (atomic_read(&iq->instr_pending) >= (s32)(iq->max_count - 2))
		st.status = IQ_SEND_STOP;

	__copy_cmd_into_iq(iq, cmd);

	/* "index" is returned, host_write_index is modified. */
	st.index = iq->host_write_index;
	INCR_INDEX_BY1(iq->host_write_index, iq->max_count);
	iq->fill_cnt++;

	/* Flush the command into memory. We need to be sure the data is in
	 * memory before indicating that the instruction is pending.
	 */
	wmb();

	atomic_inc(&iq->instr_pending);

	return st;
}

int
octeon_register_reqtype_free_fn(struct octeon_device *oct, int reqtype,
				void (*fn)(void *))
{
	if (reqtype > REQTYPE_LAST) {
		dev_err(&oct->pci_dev->dev, "%s: Invalid reqtype: %d\n",
			__func__, reqtype);
		return -EINVAL;
	}

	reqtype_free_fn[oct->octeon_id][reqtype] = fn;

	return 0;
}

static inline void
__add_to_request_list(struct octeon_instr_queue *iq,
		      int idx, void *buf, int reqtype)
{
	iq->request_list[idx].buf = buf;
	iq->request_list[idx].reqtype = reqtype;
}

int
lio_process_iq_request_list(struct octeon_device *oct,
			    struct octeon_instr_queue *iq)
{
	int reqtype;
	void *buf;
	u32 old = iq->flush_index;
	u32 inst_count = 0;
	unsigned pkts_compl = 0, bytes_compl = 0;
	struct octeon_soft_command *sc;
	struct octeon_instr_irh *irh;

	while (old != iq->octeon_read_index) {
		reqtype = iq->request_list[old].reqtype;
		buf     = iq->request_list[old].buf;

		if (reqtype == REQTYPE_NONE)
			goto skip_this;

		octeon_update_tx_completion_counters(buf, reqtype, &pkts_compl,
						     &bytes_compl);

		switch (reqtype) {
		case REQTYPE_NORESP_NET:
		case REQTYPE_NORESP_NET_SG:
		case REQTYPE_RESP_NET_SG:
			reqtype_free_fn[oct->octeon_id][reqtype](buf);
			break;
		case REQTYPE_RESP_NET:
		case REQTYPE_SOFT_COMMAND:
			sc = buf;

			irh = (struct octeon_instr_irh *)&sc->cmd.irh;
			if (irh->rflag) {
				/* We're expecting a response from Octeon.
				 * It's up to lio_process_ordered_list() to
				 * process  sc. Add sc to the ordered soft
				 * command response list because we expect
				 * a response from Octeon.
				 */
				spin_lock_bh(&oct->response_list
					[OCTEON_ORDERED_SC_LIST].lock);
				atomic_inc(&oct->response_list
					[OCTEON_ORDERED_SC_LIST].
					pending_req_count);
				list_add_tail(&sc->node, &oct->response_list
					[OCTEON_ORDERED_SC_LIST].head);
				spin_unlock_bh(&oct->response_list
					[OCTEON_ORDERED_SC_LIST].lock);
			} else {
				if (sc->callback) {
					sc->callback(oct, OCTEON_REQUEST_DONE,
						     sc->callback_arg);
				}
			}
			break;
		default:
			dev_err(&oct->pci_dev->dev,
				"%s Unknown reqtype: %d buf: %p at idx %d\n",
				__func__, reqtype, buf, old);
		}

		iq->request_list[old].buf = NULL;
		iq->request_list[old].reqtype = 0;

 skip_this:
		inst_count++;
		INCR_INDEX_BY1(old, iq->max_count);
	}
	if (bytes_compl)
		octeon_report_tx_completion_to_bql(iq->app_ctx, pkts_compl,
						   bytes_compl);
	iq->flush_index = old;

	return inst_count;
}

static inline void
update_iq_indices(struct octeon_device *oct, struct octeon_instr_queue *iq)
{
	u32 inst_processed = 0;

	/* Calculate how many commands Octeon has read and move the read index
	 * accordingly.
	 */
	iq->octeon_read_index = oct->fn_list.update_iq_read_idx(oct, iq);

	/* Move the NORESPONSE requests to the per-device completion list. */
	if (iq->flush_index != iq->octeon_read_index)
		inst_processed = lio_process_iq_request_list(oct, iq);

	if (inst_processed) {
		atomic_sub(inst_processed, &iq->instr_pending);
		iq->stats.instr_processed += inst_processed;
	}
}

static void
octeon_flush_iq(struct octeon_device *oct, struct octeon_instr_queue *iq,
		u32 pending_thresh)
{
	if (atomic_read(&iq->instr_pending) >= (s32)pending_thresh) {
		spin_lock_bh(&iq->lock);
		update_iq_indices(oct, iq);
		spin_unlock_bh(&iq->lock);
	}
}

static void __check_db_timeout(struct octeon_device *oct, unsigned long iq_no)
{
	struct octeon_instr_queue *iq;
	u64 next_time;

	if (!oct)
		return;
	iq = oct->instr_queue[iq_no];
	if (!iq)
		return;

	/* If jiffies - last_db_time < db_timeout do nothing  */
	next_time = iq->last_db_time + iq->db_timeout;
	if (!time_after(jiffies, (unsigned long)next_time))
		return;
	iq->last_db_time = jiffies;

	/* Get the lock and prevent tasklets. This routine gets called from
	 * the poll thread. Instructions can now be posted in tasklet context
	 */
	spin_lock_bh(&iq->lock);
	if (iq->fill_cnt != 0)
		ring_doorbell(oct, iq);

	spin_unlock_bh(&iq->lock);

	/* Flush the instruction queue */
	if (iq->do_auto_flush)
		octeon_flush_iq(oct, iq, 1);
}

/* Called by the Poll thread at regular intervals to check the instruction
 * queue for commands to be posted and for commands that were fetched by Octeon.
 */
static void check_db_timeout(struct work_struct *work)
{
	struct cavium_wk *wk = (struct cavium_wk *)work;
	struct octeon_device *oct = (struct octeon_device *)wk->ctxptr;
	unsigned long iq_no = wk->ctxul;
	struct cavium_wq *db_wq = &oct->check_db_wq[iq_no];

	__check_db_timeout(oct, iq_no);
	queue_delayed_work(db_wq->wq, &db_wq->wk.work, msecs_to_jiffies(1));
}

int
octeon_send_command(struct octeon_device *oct, u32 iq_no,
		    u32 force_db, void *cmd, void *buf,
		    u32 datasize, u32 reqtype)
{
	struct iq_post_status st;
	struct octeon_instr_queue *iq = oct->instr_queue[iq_no];

	spin_lock_bh(&iq->lock);

	st = __post_command2(oct, iq, force_db, cmd);

	if (st.status != IQ_SEND_FAILED) {
		octeon_report_sent_bytes_to_bql(buf, reqtype);
		__add_to_request_list(iq, st.index, buf, reqtype);
		INCR_INSTRQUEUE_PKT_COUNT(oct, iq_no, bytes_sent, datasize);
		INCR_INSTRQUEUE_PKT_COUNT(oct, iq_no, instr_posted, 1);

		if (iq->fill_cnt >= iq->fill_threshold || force_db)
			ring_doorbell(oct, iq);
	} else {
		INCR_INSTRQUEUE_PKT_COUNT(oct, iq_no, instr_dropped, 1);
	}

	spin_unlock_bh(&iq->lock);

	if (iq->do_auto_flush)
		octeon_flush_iq(oct, iq, 2);

	return st.status;
}

void
octeon_prepare_soft_command(struct octeon_device *oct,
			    struct octeon_soft_command *sc,
			    u8 opcode,
			    u8 subcode,
			    u32 irh_ossp,
			    u64 ossp0,
			    u64 ossp1)
{
	struct octeon_config *oct_cfg;
	struct octeon_instr_ih *ih;
	struct octeon_instr_irh *irh;
	struct octeon_instr_rdp *rdp;

	BUG_ON(opcode > 15);
	BUG_ON(subcode > 127);

	oct_cfg = octeon_get_conf(oct);

	ih          = (struct octeon_instr_ih *)&sc->cmd.ih;
	ih->tagtype = ATOMIC_TAG;
	ih->tag     = LIO_CONTROL;
	ih->raw     = 1;
	ih->grp     = CFG_GET_CTRL_Q_GRP(oct_cfg);

	if (sc->datasize) {
		ih->dlengsz = sc->datasize;
		ih->rs = 1;
	}

	irh            = (struct octeon_instr_irh *)&sc->cmd.irh;
	irh->opcode    = opcode;
	irh->subcode   = subcode;

	/* opcode/subcode specific parameters (ossp) */
	irh->ossp       = irh_ossp;
	sc->cmd.ossp[0] = ossp0;
	sc->cmd.ossp[1] = ossp1;

	if (sc->rdatasize) {
		rdp            = (struct octeon_instr_rdp *)&sc->cmd.rdp;
		rdp->pcie_port = oct->pcie_port;
		rdp->rlen      = sc->rdatasize;

		irh->rflag =  1;
		irh->len   =  4;
		ih->fsz    = 40; /* irh+ossp[0]+ossp[1]+rdp+rptr = 40 bytes */
	} else {
		irh->rflag =  0;
		irh->len   =  2;
		ih->fsz    = 24; /* irh + ossp[0] + ossp[1] = 24 bytes */
	}

	while (!(oct->io_qmask.iq & (1 << sc->iq_no)))
		sc->iq_no++;
}

int octeon_send_soft_command(struct octeon_device *oct,
			     struct octeon_soft_command *sc)
{
	struct octeon_instr_ih *ih;
	struct octeon_instr_irh *irh;
	struct octeon_instr_rdp *rdp;

	ih = (struct octeon_instr_ih *)&sc->cmd.ih;
	if (ih->dlengsz) {
		BUG_ON(!sc->dmadptr);
		sc->cmd.dptr = sc->dmadptr;
	}

	irh = (struct octeon_instr_irh *)&sc->cmd.irh;
	if (irh->rflag) {
		BUG_ON(!sc->dmarptr);
		BUG_ON(!sc->status_word);
		*sc->status_word = COMPLETION_WORD_INIT;

		rdp = (struct octeon_instr_rdp *)&sc->cmd.rdp;

		sc->cmd.rptr = sc->dmarptr;
	}

	if (sc->wait_time)
		sc->timeout = jiffies + sc->wait_time;

	return octeon_send_command(oct, sc->iq_no, 1, &sc->cmd, sc,
				   (u32)ih->dlengsz, REQTYPE_SOFT_COMMAND);
}

int octeon_setup_sc_buffer_pool(struct octeon_device *oct)
{
	int i;
	u64 dma_addr;
	struct octeon_soft_command *sc;

	INIT_LIST_HEAD(&oct->sc_buf_pool.head);
	spin_lock_init(&oct->sc_buf_pool.lock);
	atomic_set(&oct->sc_buf_pool.alloc_buf_count, 0);

	for (i = 0; i < MAX_SOFT_COMMAND_BUFFERS; i++) {
		sc = (struct octeon_soft_command *)
			lio_dma_alloc(oct,
				      SOFT_COMMAND_BUFFER_SIZE,
					  (dma_addr_t *)&dma_addr);
		if (!sc)
			return 1;

		sc->dma_addr = dma_addr;
		sc->size = SOFT_COMMAND_BUFFER_SIZE;

		list_add_tail(&sc->node, &oct->sc_buf_pool.head);
	}

	return 0;
}

int octeon_free_sc_buffer_pool(struct octeon_device *oct)
{
	struct list_head *tmp, *tmp2;
	struct octeon_soft_command *sc;

	spin_lock(&oct->sc_buf_pool.lock);

	list_for_each_safe(tmp, tmp2, &oct->sc_buf_pool.head) {
		list_del(tmp);

		sc = (struct octeon_soft_command *)tmp;

		lio_dma_free(oct, sc->size, sc, sc->dma_addr);
	}

	INIT_LIST_HEAD(&oct->sc_buf_pool.head);

	spin_unlock(&oct->sc_buf_pool.lock);

	return 0;
}

struct octeon_soft_command *octeon_alloc_soft_command(struct octeon_device *oct,
						      u32 datasize,
						      u32 rdatasize,
						      u32 ctxsize)
{
	u64 dma_addr;
	u32 size;
	u32 offset = sizeof(struct octeon_soft_command);
	struct octeon_soft_command *sc = NULL;
	struct list_head *tmp;

	BUG_ON((offset + datasize + rdatasize + ctxsize) >
	       SOFT_COMMAND_BUFFER_SIZE);

	spin_lock(&oct->sc_buf_pool.lock);

	if (list_empty(&oct->sc_buf_pool.head)) {
		spin_unlock(&oct->sc_buf_pool.lock);
		return NULL;
	}

	list_for_each(tmp, &oct->sc_buf_pool.head)
		break;

	list_del(tmp);

	atomic_inc(&oct->sc_buf_pool.alloc_buf_count);

	spin_unlock(&oct->sc_buf_pool.lock);

	sc = (struct octeon_soft_command *)tmp;

	dma_addr = sc->dma_addr;
	size = sc->size;

	memset(sc, 0, sc->size);

	sc->dma_addr = dma_addr;
	sc->size = size;

	if (ctxsize) {
		sc->ctxptr = (u8 *)sc + offset;
		sc->ctxsize = ctxsize;
	}

	/* Start data at 128 byte boundary */
	offset = (offset + ctxsize + 127) & 0xffffff80;

	if (datasize) {
		sc->virtdptr = (u8 *)sc + offset;
		sc->dmadptr = dma_addr + offset;
		sc->datasize = datasize;
	}

	/* Start rdata at 128 byte boundary */
	offset = (offset + datasize + 127) & 0xffffff80;

	if (rdatasize) {
		BUG_ON(rdatasize < 16);
		sc->virtrptr = (u8 *)sc + offset;
		sc->dmarptr = dma_addr + offset;
		sc->rdatasize = rdatasize;
		sc->status_word = (u64 *)((u8 *)(sc->virtrptr) + rdatasize - 8);
	}

	return sc;
}

void octeon_free_soft_command(struct octeon_device *oct,
			      struct octeon_soft_command *sc)
{
	spin_lock(&oct->sc_buf_pool.lock);

	list_add_tail(&sc->node, &oct->sc_buf_pool.head);

	atomic_dec(&oct->sc_buf_pool.alloc_buf_count);

	spin_unlock(&oct->sc_buf_pool.lock);
}

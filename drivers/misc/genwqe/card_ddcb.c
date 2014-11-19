/**
 * IBM Accelerator Family 'GenWQE'
 *
 * (C) Copyright IBM Corp. 2013
 *
 * Author: Frank Haverkamp <haver@linux.vnet.ibm.com>
 * Author: Joerg-Stephan Vogt <jsvogt@de.ibm.com>
 * Author: Michael Jung <mijung@de.ibm.com>
 * Author: Michael Ruettger <michael@ibmra.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License (version 2 only)
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

/*
 * Device Driver Control Block (DDCB) queue support. Definition of
 * interrupt handlers for queue support as well as triggering the
 * health monitor code in case of problems. The current hardware uses
 * an MSI interrupt which is shared between error handling and
 * functional code.
 */

#include <linux/types.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/pci.h>
#include <linux/string.h>
#include <linux/dma-mapping.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/crc-itu-t.h>

#include "card_base.h"
#include "card_ddcb.h"

/*
 * N: next DDCB, this is where the next DDCB will be put.
 * A: active DDCB, this is where the code will look for the next completion.
 * x: DDCB is enqueued, we are waiting for its completion.

 * Situation (1): Empty queue
 *  +---+---+---+---+---+---+---+---+
 *  | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
 *  |   |   |   |   |   |   |   |   |
 *  +---+---+---+---+---+---+---+---+
 *           A/N
 *  enqueued_ddcbs = A - N = 2 - 2 = 0
 *
 * Situation (2): Wrapped, N > A
 *  +---+---+---+---+---+---+---+---+
 *  | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
 *  |   |   | x | x |   |   |   |   |
 *  +---+---+---+---+---+---+---+---+
 *            A       N
 *  enqueued_ddcbs = N - A = 4 - 2 = 2
 *
 * Situation (3): Queue wrapped, A > N
 *  +---+---+---+---+---+---+---+---+
 *  | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
 *  | x | x |   |   | x | x | x | x |
 *  +---+---+---+---+---+---+---+---+
 *            N       A
 *  enqueued_ddcbs = queue_max  - (A - N) = 8 - (4 - 2) = 6
 *
 * Situation (4a): Queue full N > A
 *  +---+---+---+---+---+---+---+---+
 *  | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
 *  | x | x | x | x | x | x | x |   |
 *  +---+---+---+---+---+---+---+---+
 *    A                           N
 *
 *  enqueued_ddcbs = N - A = 7 - 0 = 7
 *
 * Situation (4a): Queue full A > N
 *  +---+---+---+---+---+---+---+---+
 *  | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
 *  | x | x | x |   | x | x | x | x |
 *  +---+---+---+---+---+---+---+---+
 *                N   A
 *  enqueued_ddcbs = queue_max - (A - N) = 8 - (4 - 3) = 7
 */

static int queue_empty(struct ddcb_queue *queue)
{
	return queue->ddcb_next == queue->ddcb_act;
}

static int queue_enqueued_ddcbs(struct ddcb_queue *queue)
{
	if (queue->ddcb_next >= queue->ddcb_act)
		return queue->ddcb_next - queue->ddcb_act;

	return queue->ddcb_max - (queue->ddcb_act - queue->ddcb_next);
}

static int queue_free_ddcbs(struct ddcb_queue *queue)
{
	int free_ddcbs = queue->ddcb_max - queue_enqueued_ddcbs(queue) - 1;

	if (WARN_ON_ONCE(free_ddcbs < 0)) { /* must never ever happen! */
		return 0;
	}
	return free_ddcbs;
}

/*
 * Use of the PRIV field in the DDCB for queue debugging:
 *
 * (1) Trying to get rid of a DDCB which saw a timeout:
 *     pddcb->priv[6] = 0xcc;   # cleared
 *
 * (2) Append a DDCB via NEXT bit:
 *     pddcb->priv[7] = 0xaa;	# appended
 *
 * (3) DDCB needed tapping:
 *     pddcb->priv[7] = 0xbb;   # tapped
 *
 * (4) DDCB marked as correctly finished:
 *     pddcb->priv[6] = 0xff;	# finished
 */

static inline void ddcb_mark_tapped(struct ddcb *pddcb)
{
	pddcb->priv[7] = 0xbb;  /* tapped */
}

static inline void ddcb_mark_appended(struct ddcb *pddcb)
{
	pddcb->priv[7] = 0xaa;	/* appended */
}

static inline void ddcb_mark_cleared(struct ddcb *pddcb)
{
	pddcb->priv[6] = 0xcc; /* cleared */
}

static inline void ddcb_mark_finished(struct ddcb *pddcb)
{
	pddcb->priv[6] = 0xff;	/* finished */
}

static inline void ddcb_mark_unused(struct ddcb *pddcb)
{
	pddcb->priv_64 = cpu_to_be64(0); /* not tapped */
}

/**
 * genwqe_crc16() - Generate 16-bit crc as required for DDCBs
 * @buff:       pointer to data buffer
 * @len:        length of data for calculation
 * @init:       initial crc (0xffff at start)
 *
 * Polynomial = x^16 + x^12 + x^5 + 1   (0x1021)
 * Example: 4 bytes 0x01 0x02 0x03 0x04 with init = 0xffff
 *          should result in a crc16 of 0x89c3
 *
 * Return: crc16 checksum in big endian format !
 */
static inline u16 genwqe_crc16(const u8 *buff, size_t len, u16 init)
{
	return crc_itu_t(init, buff, len);
}

static void print_ddcb_info(struct genwqe_dev *cd, struct ddcb_queue *queue)
{
	int i;
	struct ddcb *pddcb;
	unsigned long flags;
	struct pci_dev *pci_dev = cd->pci_dev;

	spin_lock_irqsave(&cd->print_lock, flags);

	dev_info(&pci_dev->dev,
		 "DDCB list for card #%d (ddcb_act=%d / ddcb_next=%d):\n",
		 cd->card_idx, queue->ddcb_act, queue->ddcb_next);

	pddcb = queue->ddcb_vaddr;
	for (i = 0; i < queue->ddcb_max; i++) {
		dev_err(&pci_dev->dev,
			"  %c %-3d: RETC=%03x SEQ=%04x "
			"HSI=%02X SHI=%02x PRIV=%06llx CMD=%03x\n",
			i == queue->ddcb_act ? '>' : ' ',
			i,
			be16_to_cpu(pddcb->retc_16),
			be16_to_cpu(pddcb->seqnum_16),
			pddcb->hsi,
			pddcb->shi,
			be64_to_cpu(pddcb->priv_64),
			pddcb->cmd);
		pddcb++;
	}
	spin_unlock_irqrestore(&cd->print_lock, flags);
}

struct genwqe_ddcb_cmd *ddcb_requ_alloc(void)
{
	struct ddcb_requ *req;

	req = kzalloc(sizeof(*req), GFP_ATOMIC);
	if (!req)
		return NULL;

	return &req->cmd;
}

void ddcb_requ_free(struct genwqe_ddcb_cmd *cmd)
{
	struct ddcb_requ *req = container_of(cmd, struct ddcb_requ, cmd);
	kfree(req);
}

static inline enum genwqe_requ_state ddcb_requ_get_state(struct ddcb_requ *req)
{
	return req->req_state;
}

static inline void ddcb_requ_set_state(struct ddcb_requ *req,
				       enum genwqe_requ_state new_state)
{
	req->req_state = new_state;
}

static inline int ddcb_requ_collect_debug_data(struct ddcb_requ *req)
{
	return req->cmd.ddata_addr != 0x0;
}

/**
 * ddcb_requ_finished() - Returns the hardware state of the associated DDCB
 * @cd:          pointer to genwqe device descriptor
 * @req:         DDCB work request
 *
 * Status of ddcb_requ mirrors this hardware state, but is copied in
 * the ddcb_requ on interrupt/polling function. The lowlevel code
 * should check the hardware state directly, the higher level code
 * should check the copy.
 *
 * This function will also return true if the state of the queue is
 * not GENWQE_CARD_USED. This enables us to purge all DDCBs in the
 * shutdown case.
 */
static int ddcb_requ_finished(struct genwqe_dev *cd, struct ddcb_requ *req)
{
	return (ddcb_requ_get_state(req) == GENWQE_REQU_FINISHED) ||
		(cd->card_state != GENWQE_CARD_USED);
}

/**
 * enqueue_ddcb() - Enqueue a DDCB
 * @cd:         pointer to genwqe device descriptor
 * @queue:	queue this operation should be done on
 * @ddcb_no:    pointer to ddcb number being tapped
 *
 * Start execution of DDCB by tapping or append to queue via NEXT
 * bit. This is done by an atomic 'compare and swap' instruction and
 * checking SHI and HSI of the previous DDCB.
 *
 * This function must only be called with ddcb_lock held.
 *
 * Return: 1 if new DDCB is appended to previous
 *         2 if DDCB queue is tapped via register/simulation
 */
#define RET_DDCB_APPENDED 1
#define RET_DDCB_TAPPED   2

static int enqueue_ddcb(struct genwqe_dev *cd, struct ddcb_queue *queue,
			struct ddcb *pddcb, int ddcb_no)
{
	unsigned int try;
	int prev_no;
	struct ddcb *prev_ddcb;
	__be32 old, new, icrc_hsi_shi;
	u64 num;

	/*
	 * For performance checks a Dispatch Timestamp can be put into
	 * DDCB It is supposed to use the SLU's free running counter,
	 * but this requires PCIe cycles.
	 */
	ddcb_mark_unused(pddcb);

	/* check previous DDCB if already fetched */
	prev_no = (ddcb_no == 0) ? queue->ddcb_max - 1 : ddcb_no - 1;
	prev_ddcb = &queue->ddcb_vaddr[prev_no];

	/*
	 * It might have happened that the HSI.FETCHED bit is
	 * set. Retry in this case. Therefore I expect maximum 2 times
	 * trying.
	 */
	ddcb_mark_appended(pddcb);
	for (try = 0; try < 2; try++) {
		old = prev_ddcb->icrc_hsi_shi_32; /* read SHI/HSI in BE32 */

		/* try to append via NEXT bit if prev DDCB is not completed */
		if ((old & DDCB_COMPLETED_BE32) != 0x00000000)
			break;

		new = (old | DDCB_NEXT_BE32);

		wmb();
		icrc_hsi_shi = cmpxchg(&prev_ddcb->icrc_hsi_shi_32, old, new);

		if (icrc_hsi_shi == old)
			return RET_DDCB_APPENDED; /* appended to queue */
	}

	/* Queue must be re-started by updating QUEUE_OFFSET */
	ddcb_mark_tapped(pddcb);
	num = (u64)ddcb_no << 8;

	wmb();
	__genwqe_writeq(cd, queue->IO_QUEUE_OFFSET, num); /* start queue */

	return RET_DDCB_TAPPED;
}

/**
 * copy_ddcb_results() - Copy output state from real DDCB to request
 *
 * Copy DDCB ASV to request struct. There is no endian
 * conversion made, since data structure in ASV is still
 * unknown here.
 *
 * This is needed by:
 *   - genwqe_purge_ddcb()
 *   - genwqe_check_ddcb_queue()
 */
static void copy_ddcb_results(struct ddcb_requ *req, int ddcb_no)
{
	struct ddcb_queue *queue = req->queue;
	struct ddcb *pddcb = &queue->ddcb_vaddr[req->num];

	memcpy(&req->cmd.asv[0], &pddcb->asv[0], DDCB_ASV_LENGTH);

	/* copy status flags of the variant part */
	req->cmd.vcrc     = be16_to_cpu(pddcb->vcrc_16);
	req->cmd.deque_ts = be64_to_cpu(pddcb->deque_ts_64);
	req->cmd.cmplt_ts = be64_to_cpu(pddcb->cmplt_ts_64);

	req->cmd.attn     = be16_to_cpu(pddcb->attn_16);
	req->cmd.progress = be32_to_cpu(pddcb->progress_32);
	req->cmd.retc     = be16_to_cpu(pddcb->retc_16);

	if (ddcb_requ_collect_debug_data(req)) {
		int prev_no = (ddcb_no == 0) ?
			queue->ddcb_max - 1 : ddcb_no - 1;
		struct ddcb *prev_pddcb = &queue->ddcb_vaddr[prev_no];

		memcpy(&req->debug_data.ddcb_finished, pddcb,
		       sizeof(req->debug_data.ddcb_finished));
		memcpy(&req->debug_data.ddcb_prev, prev_pddcb,
		       sizeof(req->debug_data.ddcb_prev));
	}
}

/**
 * genwqe_check_ddcb_queue() - Checks DDCB queue for completed work equests.
 * @cd:         pointer to genwqe device descriptor
 *
 * Return: Number of DDCBs which were finished
 */
static int genwqe_check_ddcb_queue(struct genwqe_dev *cd,
				   struct ddcb_queue *queue)
{
	unsigned long flags;
	int ddcbs_finished = 0;
	struct pci_dev *pci_dev = cd->pci_dev;

	spin_lock_irqsave(&queue->ddcb_lock, flags);

	/* FIXME avoid soft locking CPU */
	while (!queue_empty(queue) && (ddcbs_finished < queue->ddcb_max)) {

		struct ddcb *pddcb;
		struct ddcb_requ *req;
		u16 vcrc, vcrc_16, retc_16;

		pddcb = &queue->ddcb_vaddr[queue->ddcb_act];

		if ((pddcb->icrc_hsi_shi_32 & DDCB_COMPLETED_BE32) ==
		    0x00000000)
			goto go_home; /* not completed, continue waiting */

		/* Note: DDCB could be purged */

		req = queue->ddcb_req[queue->ddcb_act];
		if (req == NULL) {
			/* this occurs if DDCB is purged, not an error */
			/* Move active DDCB further; Nothing to do anymore. */
			goto pick_next_one;
		}

		/*
		 * HSI=0x44 (fetched and completed), but RETC is
		 * 0x101, or even worse 0x000.
		 *
		 * In case of seeing the queue in inconsistent state
		 * we read the errcnts and the queue status to provide
		 * a trigger for our PCIe analyzer stop capturing.
		 */
		retc_16 = be16_to_cpu(pddcb->retc_16);
		if ((pddcb->hsi == 0x44) && (retc_16 <= 0x101)) {
			u64 errcnts, status;
			u64 ddcb_offs = (u64)pddcb - (u64)queue->ddcb_vaddr;

			errcnts = __genwqe_readq(cd, queue->IO_QUEUE_ERRCNTS);
			status  = __genwqe_readq(cd, queue->IO_QUEUE_STATUS);

			dev_err(&pci_dev->dev,
				"[%s] SEQN=%04x HSI=%02x RETC=%03x "
				" Q_ERRCNTS=%016llx Q_STATUS=%016llx\n"
				" DDCB_DMA_ADDR=%016llx\n",
				__func__, be16_to_cpu(pddcb->seqnum_16),
				pddcb->hsi, retc_16, errcnts, status,
				queue->ddcb_daddr + ddcb_offs);
		}

		copy_ddcb_results(req, queue->ddcb_act);
		queue->ddcb_req[queue->ddcb_act] = NULL; /* take from queue */

		dev_dbg(&pci_dev->dev, "FINISHED DDCB#%d\n", req->num);
		genwqe_hexdump(pci_dev, pddcb, sizeof(*pddcb));

		ddcb_mark_finished(pddcb);

		/* calculate CRC_16 to see if VCRC is correct */
		vcrc = genwqe_crc16(pddcb->asv,
				   VCRC_LENGTH(req->cmd.asv_length),
				   0xffff);
		vcrc_16 = be16_to_cpu(pddcb->vcrc_16);
		if (vcrc != vcrc_16) {
			printk_ratelimited(KERN_ERR
				"%s %s: err: wrong VCRC pre=%02x vcrc_len=%d "
				"bytes vcrc_data=%04x is not vcrc_card=%04x\n",
				GENWQE_DEVNAME, dev_name(&pci_dev->dev),
				pddcb->pre, VCRC_LENGTH(req->cmd.asv_length),
				vcrc, vcrc_16);
		}

		ddcb_requ_set_state(req, GENWQE_REQU_FINISHED);
		queue->ddcbs_completed++;
		queue->ddcbs_in_flight--;

		/* wake up process waiting for this DDCB */
		wake_up_interruptible(&queue->ddcb_waitqs[queue->ddcb_act]);

pick_next_one:
		queue->ddcb_act = (queue->ddcb_act + 1) % queue->ddcb_max;
		ddcbs_finished++;
	}

 go_home:
	spin_unlock_irqrestore(&queue->ddcb_lock, flags);
	return ddcbs_finished;
}

/**
 * __genwqe_wait_ddcb(): Waits until DDCB is completed
 * @cd:         pointer to genwqe device descriptor
 * @req:        pointer to requsted DDCB parameters
 *
 * The Service Layer will update the RETC in DDCB when processing is
 * pending or done.
 *
 * Return: > 0 remaining jiffies, DDCB completed
 *           -ETIMEDOUT	when timeout
 *           -ERESTARTSYS when ^C
 *           -EINVAL when unknown error condition
 *
 * When an error is returned the called needs to ensure that
 * purge_ddcb() is being called to get the &req removed from the
 * queue.
 */
int __genwqe_wait_ddcb(struct genwqe_dev *cd, struct ddcb_requ *req)
{
	int rc;
	unsigned int ddcb_no;
	struct ddcb_queue *queue;
	struct pci_dev *pci_dev = cd->pci_dev;

	if (req == NULL)
		return -EINVAL;

	queue = req->queue;
	if (queue == NULL)
		return -EINVAL;

	ddcb_no = req->num;
	if (ddcb_no >= queue->ddcb_max)
		return -EINVAL;

	rc = wait_event_interruptible_timeout(queue->ddcb_waitqs[ddcb_no],
				ddcb_requ_finished(cd, req),
				genwqe_ddcb_software_timeout * HZ);

	/*
	 * We need to distinguish 3 cases here:
	 *   1. rc == 0              timeout occured
	 *   2. rc == -ERESTARTSYS   signal received
	 *   3. rc > 0               remaining jiffies condition is true
	 */
	if (rc == 0) {
		struct ddcb_queue *queue = req->queue;
		struct ddcb *pddcb;

		/*
		 * Timeout may be caused by long task switching time.
		 * When timeout happens, check if the request has
		 * meanwhile completed.
		 */
		genwqe_check_ddcb_queue(cd, req->queue);
		if (ddcb_requ_finished(cd, req))
			return rc;

		dev_err(&pci_dev->dev,
			"[%s] err: DDCB#%d timeout rc=%d state=%d req @ %p\n",
			__func__, req->num, rc,	ddcb_requ_get_state(req),
			req);
		dev_err(&pci_dev->dev,
			"[%s]      IO_QUEUE_STATUS=0x%016llx\n", __func__,
			__genwqe_readq(cd, queue->IO_QUEUE_STATUS));

		pddcb = &queue->ddcb_vaddr[req->num];
		genwqe_hexdump(pci_dev, pddcb, sizeof(*pddcb));

		print_ddcb_info(cd, req->queue);
		return -ETIMEDOUT;

	} else if (rc == -ERESTARTSYS) {
		return rc;
		/*
		 * EINTR:       Stops the application
		 * ERESTARTSYS: Restartable systemcall; called again
		 */

	} else if (rc < 0) {
		dev_err(&pci_dev->dev,
			"[%s] err: DDCB#%d unknown result (rc=%d) %d!\n",
			__func__, req->num, rc, ddcb_requ_get_state(req));
		return -EINVAL;
	}

	/* Severe error occured. Driver is forced to stop operation */
	if (cd->card_state != GENWQE_CARD_USED) {
		dev_err(&pci_dev->dev,
			"[%s] err: DDCB#%d forced to stop (rc=%d)\n",
			__func__, req->num, rc);
		return -EIO;
	}
	return rc;
}

/**
 * get_next_ddcb() - Get next available DDCB
 * @cd:         pointer to genwqe device descriptor
 *
 * DDCB's content is completely cleared but presets for PRE and
 * SEQNUM. This function must only be called when ddcb_lock is held.
 *
 * Return: NULL if no empty DDCB available otherwise ptr to next DDCB.
 */
static struct ddcb *get_next_ddcb(struct genwqe_dev *cd,
				  struct ddcb_queue *queue,
				  int *num)
{
	u64 *pu64;
	struct ddcb *pddcb;

	if (queue_free_ddcbs(queue) == 0) /* queue is  full */
		return NULL;

	/* find new ddcb */
	pddcb = &queue->ddcb_vaddr[queue->ddcb_next];

	/* if it is not completed, we are not allowed to use it */
	/* barrier(); */
	if ((pddcb->icrc_hsi_shi_32 & DDCB_COMPLETED_BE32) == 0x00000000)
		return NULL;

	*num = queue->ddcb_next;	/* internal DDCB number */
	queue->ddcb_next = (queue->ddcb_next + 1) % queue->ddcb_max;

	/* clear important DDCB fields */
	pu64 = (u64 *)pddcb;
	pu64[0] = 0ULL;		/* offs 0x00 (ICRC,HSI,SHI,...) */
	pu64[1] = 0ULL;		/* offs 0x01 (ACFUNC,CMD...) */

	/* destroy previous results in ASV */
	pu64[0x80/8] = 0ULL;	/* offs 0x80 (ASV + 0) */
	pu64[0x88/8] = 0ULL;	/* offs 0x88 (ASV + 0x08) */
	pu64[0x90/8] = 0ULL;	/* offs 0x90 (ASV + 0x10) */
	pu64[0x98/8] = 0ULL;	/* offs 0x98 (ASV + 0x18) */
	pu64[0xd0/8] = 0ULL;	/* offs 0xd0 (RETC,ATTN...) */

	pddcb->pre = DDCB_PRESET_PRE; /* 128 */
	pddcb->seqnum_16 = cpu_to_be16(queue->ddcb_seq++);
	return pddcb;
}

/**
 * __genwqe_purge_ddcb() - Remove a DDCB from the workqueue
 * @cd:         genwqe device descriptor
 * @req:        DDCB request
 *
 * This will fail when the request was already FETCHED. In this case
 * we need to wait until it is finished. Else the DDCB can be
 * reused. This function also ensures that the request data structure
 * is removed from ddcb_req[].
 *
 * Do not forget to call this function when genwqe_wait_ddcb() fails,
 * such that the request gets really removed from ddcb_req[].
 *
 * Return: 0 success
 */
int __genwqe_purge_ddcb(struct genwqe_dev *cd, struct ddcb_requ *req)
{
	struct ddcb *pddcb = NULL;
	unsigned int t;
	unsigned long flags;
	struct ddcb_queue *queue = req->queue;
	struct pci_dev *pci_dev = cd->pci_dev;
	u64 queue_status;
	__be32 icrc_hsi_shi = 0x0000;
	__be32 old, new;

	/* unsigned long flags; */
	if (genwqe_ddcb_software_timeout <= 0) {
		dev_err(&pci_dev->dev,
			"[%s] err: software timeout is not set!\n", __func__);
		return -EFAULT;
	}

	pddcb = &queue->ddcb_vaddr[req->num];

	for (t = 0; t < genwqe_ddcb_software_timeout * 10; t++) {

		spin_lock_irqsave(&queue->ddcb_lock, flags);

		/* Check if req was meanwhile finished */
		if (ddcb_requ_get_state(req) == GENWQE_REQU_FINISHED)
			goto go_home;

		/* try to set PURGE bit if FETCHED/COMPLETED are not set */
		old = pddcb->icrc_hsi_shi_32;	/* read SHI/HSI in BE32 */
		if ((old & DDCB_FETCHED_BE32) == 0x00000000) {

			new = (old | DDCB_PURGE_BE32);
			icrc_hsi_shi = cmpxchg(&pddcb->icrc_hsi_shi_32,
					       old, new);
			if (icrc_hsi_shi == old)
				goto finish_ddcb;
		}

		/* normal finish with HSI bit */
		barrier();
		icrc_hsi_shi = pddcb->icrc_hsi_shi_32;
		if (icrc_hsi_shi & DDCB_COMPLETED_BE32)
			goto finish_ddcb;

		spin_unlock_irqrestore(&queue->ddcb_lock, flags);

		/*
		 * Here the check_ddcb() function will most likely
		 * discover this DDCB to be finished some point in
		 * time. It will mark the req finished and free it up
		 * in the list.
		 */

		copy_ddcb_results(req, req->num); /* for the failing case */
		msleep(100); /* sleep for 1/10 second and try again */
		continue;

finish_ddcb:
		copy_ddcb_results(req, req->num);
		ddcb_requ_set_state(req, GENWQE_REQU_FINISHED);
		queue->ddcbs_in_flight--;
		queue->ddcb_req[req->num] = NULL; /* delete from array */
		ddcb_mark_cleared(pddcb);

		/* Move active DDCB further; Nothing to do here anymore. */

		/*
		 * We need to ensure that there is at least one free
		 * DDCB in the queue. To do that, we must update
		 * ddcb_act only if the COMPLETED bit is set for the
		 * DDCB we are working on else we treat that DDCB even
		 * if we PURGED it as occupied (hardware is supposed
		 * to set the COMPLETED bit yet!).
		 */
		icrc_hsi_shi = pddcb->icrc_hsi_shi_32;
		if ((icrc_hsi_shi & DDCB_COMPLETED_BE32) &&
		    (queue->ddcb_act == req->num)) {
			queue->ddcb_act = ((queue->ddcb_act + 1) %
					   queue->ddcb_max);
		}
go_home:
		spin_unlock_irqrestore(&queue->ddcb_lock, flags);
		return 0;
	}

	/*
	 * If the card is dead and the queue is forced to stop, we
	 * might see this in the queue status register.
	 */
	queue_status = __genwqe_readq(cd, queue->IO_QUEUE_STATUS);

	dev_dbg(&pci_dev->dev, "UN/FINISHED DDCB#%d\n", req->num);
	genwqe_hexdump(pci_dev, pddcb, sizeof(*pddcb));

	dev_err(&pci_dev->dev,
		"[%s] err: DDCB#%d not purged and not completed "
		"after %d seconds QSTAT=%016llx!!\n",
		__func__, req->num, genwqe_ddcb_software_timeout,
		queue_status);

	print_ddcb_info(cd, req->queue);

	return -EFAULT;
}

int genwqe_init_debug_data(struct genwqe_dev *cd, struct genwqe_debug_data *d)
{
	int len;
	struct pci_dev *pci_dev = cd->pci_dev;

	if (d == NULL) {
		dev_err(&pci_dev->dev,
			"[%s] err: invalid memory for debug data!\n",
			__func__);
		return -EFAULT;
	}

	len  = sizeof(d->driver_version);
	snprintf(d->driver_version, len, "%s", DRV_VERS_STRING);
	d->slu_unitcfg = cd->slu_unitcfg;
	d->app_unitcfg = cd->app_unitcfg;
	return 0;
}

/**
 * __genwqe_enqueue_ddcb() - Enqueue a DDCB
 * @cd:          pointer to genwqe device descriptor
 * @req:         pointer to DDCB execution request
 *
 * Return: 0 if enqueuing succeeded
 *         -EIO if card is unusable/PCIe problems
 *         -EBUSY if enqueuing failed
 */
int __genwqe_enqueue_ddcb(struct genwqe_dev *cd, struct ddcb_requ *req)
{
	struct ddcb *pddcb;
	unsigned long flags;
	struct ddcb_queue *queue;
	struct pci_dev *pci_dev = cd->pci_dev;
	u16 icrc;

	if (cd->card_state != GENWQE_CARD_USED) {
		printk_ratelimited(KERN_ERR
			"%s %s: [%s] Card is unusable/PCIe problem Req#%d\n",
			GENWQE_DEVNAME, dev_name(&pci_dev->dev),
			__func__, req->num);
		return -EIO;
	}

	queue = req->queue = &cd->queue;

	/* FIXME circumvention to improve performance when no irq is
	 * there.
	 */
	if (genwqe_polling_enabled)
		genwqe_check_ddcb_queue(cd, queue);

	/*
	 * It must be ensured to process all DDCBs in successive
	 * order. Use a lock here in order to prevent nested DDCB
	 * enqueuing.
	 */
	spin_lock_irqsave(&queue->ddcb_lock, flags);

	pddcb = get_next_ddcb(cd, queue, &req->num);	/* get ptr and num */
	if (pddcb == NULL) {
		spin_unlock_irqrestore(&queue->ddcb_lock, flags);
		queue->busy++;
		return -EBUSY;
	}

	if (queue->ddcb_req[req->num] != NULL) {
		spin_unlock_irqrestore(&queue->ddcb_lock, flags);

		dev_err(&pci_dev->dev,
			"[%s] picked DDCB %d with req=%p still in use!!\n",
			__func__, req->num, req);
		return -EFAULT;
	}
	ddcb_requ_set_state(req, GENWQE_REQU_ENQUEUED);
	queue->ddcb_req[req->num] = req;

	pddcb->cmdopts_16 = cpu_to_be16(req->cmd.cmdopts);
	pddcb->cmd = req->cmd.cmd;
	pddcb->acfunc = req->cmd.acfunc;	/* functional unit */

	/*
	 * We know that we can get retc 0x104 with CRC error, do not
	 * stop the queue in those cases for this command. XDIR = 1
	 * does not work for old SLU versions.
	 *
	 * Last bitstream with the old XDIR behavior had SLU_ID
	 * 0x34199.
	 */
	if ((cd->slu_unitcfg & 0xFFFF0ull) > 0x34199ull)
		pddcb->xdir = 0x1;
	else
		pddcb->xdir = 0x0;


	pddcb->psp = (((req->cmd.asiv_length / 8) << 4) |
		      ((req->cmd.asv_length  / 8)));
	pddcb->disp_ts_64 = cpu_to_be64(req->cmd.disp_ts);

	/*
	 * If copying the whole DDCB_ASIV_LENGTH is impacting
	 * performance we need to change it to
	 * req->cmd.asiv_length. But simulation benefits from some
	 * non-architectured bits behind the architectured content.
	 *
	 * How much data is copied depends on the availability of the
	 * ATS field, which was introduced late. If the ATS field is
	 * supported ASIV is 8 bytes shorter than it used to be. Since
	 * the ATS field is copied too, the code should do exactly
	 * what it did before, but I wanted to make copying of the ATS
	 * field very explicit.
	 */
	if (genwqe_get_slu_id(cd) <= 0x2) {
		memcpy(&pddcb->__asiv[0],	/* destination */
		       &req->cmd.__asiv[0],	/* source */
		       DDCB_ASIV_LENGTH);	/* req->cmd.asiv_length */
	} else {
		pddcb->n.ats_64 = cpu_to_be64(req->cmd.ats);
		memcpy(&pddcb->n.asiv[0],	/* destination */
			&req->cmd.asiv[0],	/* source */
			DDCB_ASIV_LENGTH_ATS);	/* req->cmd.asiv_length */
	}

	pddcb->icrc_hsi_shi_32 = cpu_to_be32(0x00000000); /* for crc */

	/*
	 * Calculate CRC_16 for corresponding range PSP(7:4). Include
	 * empty 4 bytes prior to the data.
	 */
	icrc = genwqe_crc16((const u8 *)pddcb,
			   ICRC_LENGTH(req->cmd.asiv_length), 0xffff);
	pddcb->icrc_hsi_shi_32 = cpu_to_be32((u32)icrc << 16);

	/* enable DDCB completion irq */
	if (!genwqe_polling_enabled)
		pddcb->icrc_hsi_shi_32 |= DDCB_INTR_BE32;

	dev_dbg(&pci_dev->dev, "INPUT DDCB#%d\n", req->num);
	genwqe_hexdump(pci_dev, pddcb, sizeof(*pddcb));

	if (ddcb_requ_collect_debug_data(req)) {
		/* use the kernel copy of debug data. copying back to
		   user buffer happens later */

		genwqe_init_debug_data(cd, &req->debug_data);
		memcpy(&req->debug_data.ddcb_before, pddcb,
		       sizeof(req->debug_data.ddcb_before));
	}

	enqueue_ddcb(cd, queue, pddcb, req->num);
	queue->ddcbs_in_flight++;

	if (queue->ddcbs_in_flight > queue->ddcbs_max_in_flight)
		queue->ddcbs_max_in_flight = queue->ddcbs_in_flight;

	ddcb_requ_set_state(req, GENWQE_REQU_TAPPED);
	spin_unlock_irqrestore(&queue->ddcb_lock, flags);
	wake_up_interruptible(&cd->queue_waitq);

	return 0;
}

/**
 * __genwqe_execute_raw_ddcb() - Setup and execute DDCB
 * @cd:         pointer to genwqe device descriptor
 * @req:        user provided DDCB request
 */
int __genwqe_execute_raw_ddcb(struct genwqe_dev *cd,
			     struct genwqe_ddcb_cmd *cmd)
{
	int rc = 0;
	struct pci_dev *pci_dev = cd->pci_dev;
	struct ddcb_requ *req = container_of(cmd, struct ddcb_requ, cmd);

	if (cmd->asiv_length > DDCB_ASIV_LENGTH) {
		dev_err(&pci_dev->dev, "[%s] err: wrong asiv_length of %d\n",
			__func__, cmd->asiv_length);
		return -EINVAL;
	}
	if (cmd->asv_length > DDCB_ASV_LENGTH) {
		dev_err(&pci_dev->dev, "[%s] err: wrong asv_length of %d\n",
			__func__, cmd->asiv_length);
		return -EINVAL;
	}
	rc = __genwqe_enqueue_ddcb(cd, req);
	if (rc != 0)
		return rc;

	rc = __genwqe_wait_ddcb(cd, req);
	if (rc < 0)		/* error or signal interrupt */
		goto err_exit;

	if (ddcb_requ_collect_debug_data(req)) {
		if (copy_to_user((struct genwqe_debug_data __user *)
				 (unsigned long)cmd->ddata_addr,
				 &req->debug_data,
				 sizeof(struct genwqe_debug_data)))
			return -EFAULT;
	}

	/*
	 * Higher values than 0x102 indicate completion with faults,
	 * lower values than 0x102 indicate processing faults. Note
	 * that DDCB might have been purged. E.g. Cntl+C.
	 */
	if (cmd->retc != DDCB_RETC_COMPLETE) {
		/* This might happen e.g. flash read, and needs to be
		   handled by the upper layer code. */
		rc = -EBADMSG;	/* not processed/error retc */
	}

	return rc;

 err_exit:
	__genwqe_purge_ddcb(cd, req);

	if (ddcb_requ_collect_debug_data(req)) {
		if (copy_to_user((struct genwqe_debug_data __user *)
				 (unsigned long)cmd->ddata_addr,
				 &req->debug_data,
				 sizeof(struct genwqe_debug_data)))
			return -EFAULT;
	}
	return rc;
}

/**
 * genwqe_next_ddcb_ready() - Figure out if the next DDCB is already finished
 *
 * We use this as condition for our wait-queue code.
 */
static int genwqe_next_ddcb_ready(struct genwqe_dev *cd)
{
	unsigned long flags;
	struct ddcb *pddcb;
	struct ddcb_queue *queue = &cd->queue;

	spin_lock_irqsave(&queue->ddcb_lock, flags);

	if (queue_empty(queue)) { /* emtpy queue */
		spin_unlock_irqrestore(&queue->ddcb_lock, flags);
		return 0;
	}

	pddcb = &queue->ddcb_vaddr[queue->ddcb_act];
	if (pddcb->icrc_hsi_shi_32 & DDCB_COMPLETED_BE32) { /* ddcb ready */
		spin_unlock_irqrestore(&queue->ddcb_lock, flags);
		return 1;
	}

	spin_unlock_irqrestore(&queue->ddcb_lock, flags);
	return 0;
}

/**
 * genwqe_ddcbs_in_flight() - Check how many DDCBs are in flight
 *
 * Keep track on the number of DDCBs which ware currently in the
 * queue. This is needed for statistics as well as conditon if we want
 * to wait or better do polling in case of no interrupts available.
 */
int genwqe_ddcbs_in_flight(struct genwqe_dev *cd)
{
	unsigned long flags;
	int ddcbs_in_flight = 0;
	struct ddcb_queue *queue = &cd->queue;

	spin_lock_irqsave(&queue->ddcb_lock, flags);
	ddcbs_in_flight += queue->ddcbs_in_flight;
	spin_unlock_irqrestore(&queue->ddcb_lock, flags);

	return ddcbs_in_flight;
}

static int setup_ddcb_queue(struct genwqe_dev *cd, struct ddcb_queue *queue)
{
	int rc, i;
	struct ddcb *pddcb;
	u64 val64;
	unsigned int queue_size;
	struct pci_dev *pci_dev = cd->pci_dev;

	if (genwqe_ddcb_max < 2)
		return -EINVAL;

	queue_size = roundup(genwqe_ddcb_max * sizeof(struct ddcb), PAGE_SIZE);

	queue->ddcbs_in_flight = 0;  /* statistics */
	queue->ddcbs_max_in_flight = 0;
	queue->ddcbs_completed = 0;
	queue->busy = 0;

	queue->ddcb_seq	  = 0x100; /* start sequence number */
	queue->ddcb_max	  = genwqe_ddcb_max; /* module parameter */
	queue->ddcb_vaddr = __genwqe_alloc_consistent(cd, queue_size,
						&queue->ddcb_daddr);
	if (queue->ddcb_vaddr == NULL) {
		dev_err(&pci_dev->dev,
			"[%s] **err: could not allocate DDCB **\n", __func__);
		return -ENOMEM;
	}
	memset(queue->ddcb_vaddr, 0, queue_size);

	queue->ddcb_req = kzalloc(sizeof(struct ddcb_requ *) *
				  queue->ddcb_max, GFP_KERNEL);
	if (!queue->ddcb_req) {
		rc = -ENOMEM;
		goto free_ddcbs;
	}

	queue->ddcb_waitqs = kzalloc(sizeof(wait_queue_head_t) *
				     queue->ddcb_max, GFP_KERNEL);
	if (!queue->ddcb_waitqs) {
		rc = -ENOMEM;
		goto free_requs;
	}

	for (i = 0; i < queue->ddcb_max; i++) {
		pddcb = &queue->ddcb_vaddr[i];		     /* DDCBs */
		pddcb->icrc_hsi_shi_32 = DDCB_COMPLETED_BE32;
		pddcb->retc_16 = cpu_to_be16(0xfff);

		queue->ddcb_req[i] = NULL;		     /* requests */
		init_waitqueue_head(&queue->ddcb_waitqs[i]); /* waitqueues */
	}

	queue->ddcb_act  = 0;
	queue->ddcb_next = 0;	/* queue is empty */

	spin_lock_init(&queue->ddcb_lock);
	init_waitqueue_head(&queue->ddcb_waitq);

	val64 = ((u64)(queue->ddcb_max - 1) <<  8); /* lastptr */
	__genwqe_writeq(cd, queue->IO_QUEUE_CONFIG,  0x07);  /* iCRC/vCRC */
	__genwqe_writeq(cd, queue->IO_QUEUE_SEGMENT, queue->ddcb_daddr);
	__genwqe_writeq(cd, queue->IO_QUEUE_INITSQN, queue->ddcb_seq);
	__genwqe_writeq(cd, queue->IO_QUEUE_WRAP,    val64);
	return 0;

 free_requs:
	kfree(queue->ddcb_req);
	queue->ddcb_req = NULL;
 free_ddcbs:
	__genwqe_free_consistent(cd, queue_size, queue->ddcb_vaddr,
				queue->ddcb_daddr);
	queue->ddcb_vaddr = NULL;
	queue->ddcb_daddr = 0ull;
	return -ENODEV;

}

static int ddcb_queue_initialized(struct ddcb_queue *queue)
{
	return queue->ddcb_vaddr != NULL;
}

static void free_ddcb_queue(struct genwqe_dev *cd, struct ddcb_queue *queue)
{
	unsigned int queue_size;

	queue_size = roundup(queue->ddcb_max * sizeof(struct ddcb), PAGE_SIZE);

	kfree(queue->ddcb_req);
	queue->ddcb_req = NULL;

	if (queue->ddcb_vaddr) {
		__genwqe_free_consistent(cd, queue_size, queue->ddcb_vaddr,
					queue->ddcb_daddr);
		queue->ddcb_vaddr = NULL;
		queue->ddcb_daddr = 0ull;
	}
}

static irqreturn_t genwqe_pf_isr(int irq, void *dev_id)
{
	u64 gfir;
	struct genwqe_dev *cd = (struct genwqe_dev *)dev_id;
	struct pci_dev *pci_dev = cd->pci_dev;

	/*
	 * In case of fatal FIR error the queue is stopped, such that
	 * we can safely check it without risking anything.
	 */
	cd->irqs_processed++;
	wake_up_interruptible(&cd->queue_waitq);

	/*
	 * Checking for errors before kicking the queue might be
	 * safer, but slower for the good-case ... See above.
	 */
	gfir = __genwqe_readq(cd, IO_SLC_CFGREG_GFIR);
	if (((gfir & GFIR_ERR_TRIGGER) != 0x0) &&
	    !pci_channel_offline(pci_dev)) {

		if (cd->use_platform_recovery) {
			/*
			 * Since we use raw accessors, EEH errors won't be
			 * detected by the platform until we do a non-raw
			 * MMIO or config space read
			 */
			readq(cd->mmio + IO_SLC_CFGREG_GFIR);

			/* Don't do anything if the PCI channel is frozen */
			if (pci_channel_offline(pci_dev))
				goto exit;
		}

		wake_up_interruptible(&cd->health_waitq);

		/*
		 * By default GFIRs causes recovery actions. This
		 * count is just for debug when recovery is masked.
		 */
		dev_err_ratelimited(&pci_dev->dev,
				    "[%s] GFIR=%016llx\n",
				    __func__, gfir);
	}

 exit:
	return IRQ_HANDLED;
}

static irqreturn_t genwqe_vf_isr(int irq, void *dev_id)
{
	struct genwqe_dev *cd = (struct genwqe_dev *)dev_id;

	cd->irqs_processed++;
	wake_up_interruptible(&cd->queue_waitq);

	return IRQ_HANDLED;
}

/**
 * genwqe_card_thread() - Work thread for the DDCB queue
 *
 * The idea is to check if there are DDCBs in processing. If there are
 * some finished DDCBs, we process them and wakeup the
 * requestors. Otherwise we give other processes time using
 * cond_resched().
 */
static int genwqe_card_thread(void *data)
{
	int should_stop = 0, rc = 0;
	struct genwqe_dev *cd = (struct genwqe_dev *)data;

	while (!kthread_should_stop()) {

		genwqe_check_ddcb_queue(cd, &cd->queue);

		if (genwqe_polling_enabled) {
			rc = wait_event_interruptible_timeout(
				cd->queue_waitq,
				genwqe_ddcbs_in_flight(cd) ||
				(should_stop = kthread_should_stop()), 1);
		} else {
			rc = wait_event_interruptible_timeout(
				cd->queue_waitq,
				genwqe_next_ddcb_ready(cd) ||
				(should_stop = kthread_should_stop()), HZ);
		}
		if (should_stop)
			break;

		/*
		 * Avoid soft lockups on heavy loads; we do not want
		 * to disable our interrupts.
		 */
		cond_resched();
	}
	return 0;
}

/**
 * genwqe_setup_service_layer() - Setup DDCB queue
 * @cd:         pointer to genwqe device descriptor
 *
 * Allocate DDCBs. Configure Service Layer Controller (SLC).
 *
 * Return: 0 success
 */
int genwqe_setup_service_layer(struct genwqe_dev *cd)
{
	int rc;
	struct ddcb_queue *queue;
	struct pci_dev *pci_dev = cd->pci_dev;

	if (genwqe_is_privileged(cd)) {
		rc = genwqe_card_reset(cd);
		if (rc < 0) {
			dev_err(&pci_dev->dev,
				"[%s] err: reset failed.\n", __func__);
			return rc;
		}
		genwqe_read_softreset(cd);
	}

	queue = &cd->queue;
	queue->IO_QUEUE_CONFIG  = IO_SLC_QUEUE_CONFIG;
	queue->IO_QUEUE_STATUS  = IO_SLC_QUEUE_STATUS;
	queue->IO_QUEUE_SEGMENT = IO_SLC_QUEUE_SEGMENT;
	queue->IO_QUEUE_INITSQN = IO_SLC_QUEUE_INITSQN;
	queue->IO_QUEUE_OFFSET  = IO_SLC_QUEUE_OFFSET;
	queue->IO_QUEUE_WRAP    = IO_SLC_QUEUE_WRAP;
	queue->IO_QUEUE_WTIME   = IO_SLC_QUEUE_WTIME;
	queue->IO_QUEUE_ERRCNTS = IO_SLC_QUEUE_ERRCNTS;
	queue->IO_QUEUE_LRW     = IO_SLC_QUEUE_LRW;

	rc = setup_ddcb_queue(cd, queue);
	if (rc != 0) {
		rc = -ENODEV;
		goto err_out;
	}

	init_waitqueue_head(&cd->queue_waitq);
	cd->card_thread = kthread_run(genwqe_card_thread, cd,
				      GENWQE_DEVNAME "%d_thread",
				      cd->card_idx);
	if (IS_ERR(cd->card_thread)) {
		rc = PTR_ERR(cd->card_thread);
		cd->card_thread = NULL;
		goto stop_free_queue;
	}

	rc = genwqe_set_interrupt_capability(cd, GENWQE_MSI_IRQS);
	if (rc) {
		rc = -ENODEV;
		goto stop_kthread;
	}

	/*
	 * We must have all wait-queues initialized when we enable the
	 * interrupts. Otherwise we might crash if we get an early
	 * irq.
	 */
	init_waitqueue_head(&cd->health_waitq);

	if (genwqe_is_privileged(cd)) {
		rc = request_irq(pci_dev->irq, genwqe_pf_isr, IRQF_SHARED,
				 GENWQE_DEVNAME, cd);
	} else {
		rc = request_irq(pci_dev->irq, genwqe_vf_isr, IRQF_SHARED,
				 GENWQE_DEVNAME, cd);
	}
	if (rc < 0) {
		dev_err(&pci_dev->dev, "irq %d not free.\n", pci_dev->irq);
		goto stop_irq_cap;
	}

	cd->card_state = GENWQE_CARD_USED;
	return 0;

 stop_irq_cap:
	genwqe_reset_interrupt_capability(cd);
 stop_kthread:
	kthread_stop(cd->card_thread);
	cd->card_thread = NULL;
 stop_free_queue:
	free_ddcb_queue(cd, queue);
 err_out:
	return rc;
}

/**
 * queue_wake_up_all() - Handles fatal error case
 *
 * The PCI device got unusable and we have to stop all pending
 * requests as fast as we can. The code after this must purge the
 * DDCBs in question and ensure that all mappings are freed.
 */
static int queue_wake_up_all(struct genwqe_dev *cd)
{
	unsigned int i;
	unsigned long flags;
	struct ddcb_queue *queue = &cd->queue;

	spin_lock_irqsave(&queue->ddcb_lock, flags);

	for (i = 0; i < queue->ddcb_max; i++)
		wake_up_interruptible(&queue->ddcb_waitqs[queue->ddcb_act]);

	spin_unlock_irqrestore(&queue->ddcb_lock, flags);

	return 0;
}

/**
 * genwqe_finish_queue() - Remove any genwqe devices and user-interfaces
 *
 * Relies on the pre-condition that there are no users of the card
 * device anymore e.g. with open file-descriptors.
 *
 * This function must be robust enough to be called twice.
 */
int genwqe_finish_queue(struct genwqe_dev *cd)
{
	int i, rc = 0, in_flight;
	int waitmax = genwqe_ddcb_software_timeout;
	struct pci_dev *pci_dev = cd->pci_dev;
	struct ddcb_queue *queue = &cd->queue;

	if (!ddcb_queue_initialized(queue))
		return 0;

	/* Do not wipe out the error state. */
	if (cd->card_state == GENWQE_CARD_USED)
		cd->card_state = GENWQE_CARD_UNUSED;

	/* Wake up all requests in the DDCB queue such that they
	   should be removed nicely. */
	queue_wake_up_all(cd);

	/* We must wait to get rid of the DDCBs in flight */
	for (i = 0; i < waitmax; i++) {
		in_flight = genwqe_ddcbs_in_flight(cd);

		if (in_flight == 0)
			break;

		dev_dbg(&pci_dev->dev,
			"  DEBUG [%d/%d] waiting for queue to get empty: "
			"%d requests!\n", i, waitmax, in_flight);

		/*
		 * Severe severe error situation: The card itself has
		 * 16 DDCB queues, each queue has e.g. 32 entries,
		 * each DDBC has a hardware timeout of currently 250
		 * msec but the PFs have a hardware timeout of 8 sec
		 * ... so I take something large.
		 */
		msleep(1000);
	}
	if (i == waitmax) {
		dev_err(&pci_dev->dev, "  [%s] err: queue is not empty!!\n",
			__func__);
		rc = -EIO;
	}
	return rc;
}

/**
 * genwqe_release_service_layer() - Shutdown DDCB queue
 * @cd:       genwqe device descriptor
 *
 * This function must be robust enough to be called twice.
 */
int genwqe_release_service_layer(struct genwqe_dev *cd)
{
	struct pci_dev *pci_dev = cd->pci_dev;

	if (!ddcb_queue_initialized(&cd->queue))
		return 1;

	free_irq(pci_dev->irq, cd);
	genwqe_reset_interrupt_capability(cd);

	if (cd->card_thread != NULL) {
		kthread_stop(cd->card_thread);
		cd->card_thread = NULL;
	}

	free_ddcb_queue(cd, &cd->queue);
	return 0;
}

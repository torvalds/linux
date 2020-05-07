// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2012-2019 ARM Limited (or its affiliates). */

#include <linux/kernel.h>
#include <linux/nospec.h>
#include "cc_driver.h"
#include "cc_buffer_mgr.h"
#include "cc_request_mgr.h"
#include "cc_pm.h"

#define CC_MAX_POLL_ITER	10
/* The highest descriptor count in used */
#define CC_MAX_DESC_SEQ_LEN	23

struct cc_req_mgr_handle {
	/* Request manager resources */
	unsigned int hw_queue_size; /* HW capability */
	unsigned int min_free_hw_slots;
	unsigned int max_used_sw_slots;
	struct cc_crypto_req req_queue[MAX_REQUEST_QUEUE_SIZE];
	u32 req_queue_head;
	u32 req_queue_tail;
	u32 axi_completed;
	u32 q_free_slots;
	/* This lock protects access to HW register
	 * that must be single request at a time
	 */
	spinlock_t hw_lock;
	struct cc_hw_desc compl_desc;
	u8 *dummy_comp_buff;
	dma_addr_t dummy_comp_buff_dma;

	/* backlog queue */
	struct list_head backlog;
	unsigned int bl_len;
	spinlock_t bl_lock; /* protect backlog queue */

#ifdef COMP_IN_WQ
	struct workqueue_struct *workq;
	struct delayed_work compwork;
#else
	struct tasklet_struct comptask;
#endif
};

struct cc_bl_item {
	struct cc_crypto_req creq;
	struct cc_hw_desc desc[CC_MAX_DESC_SEQ_LEN];
	unsigned int len;
	struct list_head list;
	bool notif;
};

static const u32 cc_cpp_int_masks[CC_CPP_NUM_ALGS][CC_CPP_NUM_SLOTS] = {
	{ BIT(CC_HOST_IRR_REE_OP_ABORTED_AES_0_INT_BIT_SHIFT),
	  BIT(CC_HOST_IRR_REE_OP_ABORTED_AES_1_INT_BIT_SHIFT),
	  BIT(CC_HOST_IRR_REE_OP_ABORTED_AES_2_INT_BIT_SHIFT),
	  BIT(CC_HOST_IRR_REE_OP_ABORTED_AES_3_INT_BIT_SHIFT),
	  BIT(CC_HOST_IRR_REE_OP_ABORTED_AES_4_INT_BIT_SHIFT),
	  BIT(CC_HOST_IRR_REE_OP_ABORTED_AES_5_INT_BIT_SHIFT),
	  BIT(CC_HOST_IRR_REE_OP_ABORTED_AES_6_INT_BIT_SHIFT),
	  BIT(CC_HOST_IRR_REE_OP_ABORTED_AES_7_INT_BIT_SHIFT) },
	{ BIT(CC_HOST_IRR_REE_OP_ABORTED_SM_0_INT_BIT_SHIFT),
	  BIT(CC_HOST_IRR_REE_OP_ABORTED_SM_1_INT_BIT_SHIFT),
	  BIT(CC_HOST_IRR_REE_OP_ABORTED_SM_2_INT_BIT_SHIFT),
	  BIT(CC_HOST_IRR_REE_OP_ABORTED_SM_3_INT_BIT_SHIFT),
	  BIT(CC_HOST_IRR_REE_OP_ABORTED_SM_4_INT_BIT_SHIFT),
	  BIT(CC_HOST_IRR_REE_OP_ABORTED_SM_5_INT_BIT_SHIFT),
	  BIT(CC_HOST_IRR_REE_OP_ABORTED_SM_6_INT_BIT_SHIFT),
	  BIT(CC_HOST_IRR_REE_OP_ABORTED_SM_7_INT_BIT_SHIFT) }
};

static void comp_handler(unsigned long devarg);
#ifdef COMP_IN_WQ
static void comp_work_handler(struct work_struct *work);
#endif

static inline u32 cc_cpp_int_mask(enum cc_cpp_alg alg, int slot)
{
	alg = array_index_nospec(alg, CC_CPP_NUM_ALGS);
	slot = array_index_nospec(slot, CC_CPP_NUM_SLOTS);

	return cc_cpp_int_masks[alg][slot];
}

void cc_req_mgr_fini(struct cc_drvdata *drvdata)
{
	struct cc_req_mgr_handle *req_mgr_h = drvdata->request_mgr_handle;
	struct device *dev = drvdata_to_dev(drvdata);

	if (!req_mgr_h)
		return; /* Not allocated */

	if (req_mgr_h->dummy_comp_buff_dma) {
		dma_free_coherent(dev, sizeof(u32), req_mgr_h->dummy_comp_buff,
				  req_mgr_h->dummy_comp_buff_dma);
	}

	dev_dbg(dev, "max_used_hw_slots=%d\n", (req_mgr_h->hw_queue_size -
						req_mgr_h->min_free_hw_slots));
	dev_dbg(dev, "max_used_sw_slots=%d\n", req_mgr_h->max_used_sw_slots);

#ifdef COMP_IN_WQ
	flush_workqueue(req_mgr_h->workq);
	destroy_workqueue(req_mgr_h->workq);
#else
	/* Kill tasklet */
	tasklet_kill(&req_mgr_h->comptask);
#endif
	kzfree(req_mgr_h);
	drvdata->request_mgr_handle = NULL;
}

int cc_req_mgr_init(struct cc_drvdata *drvdata)
{
	struct cc_req_mgr_handle *req_mgr_h;
	struct device *dev = drvdata_to_dev(drvdata);
	int rc = 0;

	req_mgr_h = kzalloc(sizeof(*req_mgr_h), GFP_KERNEL);
	if (!req_mgr_h) {
		rc = -ENOMEM;
		goto req_mgr_init_err;
	}

	drvdata->request_mgr_handle = req_mgr_h;

	spin_lock_init(&req_mgr_h->hw_lock);
	spin_lock_init(&req_mgr_h->bl_lock);
	INIT_LIST_HEAD(&req_mgr_h->backlog);

#ifdef COMP_IN_WQ
	dev_dbg(dev, "Initializing completion workqueue\n");
	req_mgr_h->workq = create_singlethread_workqueue("ccree");
	if (!req_mgr_h->workq) {
		dev_err(dev, "Failed creating work queue\n");
		rc = -ENOMEM;
		goto req_mgr_init_err;
	}
	INIT_DELAYED_WORK(&req_mgr_h->compwork, comp_work_handler);
#else
	dev_dbg(dev, "Initializing completion tasklet\n");
	tasklet_init(&req_mgr_h->comptask, comp_handler,
		     (unsigned long)drvdata);
#endif
	req_mgr_h->hw_queue_size = cc_ioread(drvdata,
					     CC_REG(DSCRPTR_QUEUE_SRAM_SIZE));
	dev_dbg(dev, "hw_queue_size=0x%08X\n", req_mgr_h->hw_queue_size);
	if (req_mgr_h->hw_queue_size < MIN_HW_QUEUE_SIZE) {
		dev_err(dev, "Invalid HW queue size = %u (Min. required is %u)\n",
			req_mgr_h->hw_queue_size, MIN_HW_QUEUE_SIZE);
		rc = -ENOMEM;
		goto req_mgr_init_err;
	}
	req_mgr_h->min_free_hw_slots = req_mgr_h->hw_queue_size;
	req_mgr_h->max_used_sw_slots = 0;

	/* Allocate DMA word for "dummy" completion descriptor use */
	req_mgr_h->dummy_comp_buff =
		dma_alloc_coherent(dev, sizeof(u32),
				   &req_mgr_h->dummy_comp_buff_dma,
				   GFP_KERNEL);
	if (!req_mgr_h->dummy_comp_buff) {
		dev_err(dev, "Not enough memory to allocate DMA (%zu) dropped buffer\n",
			sizeof(u32));
		rc = -ENOMEM;
		goto req_mgr_init_err;
	}

	/* Init. "dummy" completion descriptor */
	hw_desc_init(&req_mgr_h->compl_desc);
	set_din_const(&req_mgr_h->compl_desc, 0, sizeof(u32));
	set_dout_dlli(&req_mgr_h->compl_desc, req_mgr_h->dummy_comp_buff_dma,
		      sizeof(u32), NS_BIT, 1);
	set_flow_mode(&req_mgr_h->compl_desc, BYPASS);
	set_queue_last_ind(drvdata, &req_mgr_h->compl_desc);

	return 0;

req_mgr_init_err:
	cc_req_mgr_fini(drvdata);
	return rc;
}

static void enqueue_seq(struct cc_drvdata *drvdata, struct cc_hw_desc seq[],
			unsigned int seq_len)
{
	int i, w;
	void __iomem *reg = drvdata->cc_base + CC_REG(DSCRPTR_QUEUE_WORD0);
	struct device *dev = drvdata_to_dev(drvdata);

	/*
	 * We do indeed write all 6 command words to the same
	 * register. The HW supports this.
	 */

	for (i = 0; i < seq_len; i++) {
		for (w = 0; w <= 5; w++)
			writel_relaxed(seq[i].word[w], reg);

		if (cc_dump_desc)
			dev_dbg(dev, "desc[%02d]: 0x%08X 0x%08X 0x%08X 0x%08X 0x%08X 0x%08X\n",
				i, seq[i].word[0], seq[i].word[1],
				seq[i].word[2], seq[i].word[3],
				seq[i].word[4], seq[i].word[5]);
	}
}

/**
 * request_mgr_complete() - Completion will take place if and only if user
 * requested completion by cc_send_sync_request().
 *
 * @dev: Device pointer
 * @dx_compl_h: The completion event to signal
 * @dummy: unused error code
 */
static void request_mgr_complete(struct device *dev, void *dx_compl_h,
				 int dummy)
{
	struct completion *this_compl = dx_compl_h;

	complete(this_compl);
}

static int cc_queues_status(struct cc_drvdata *drvdata,
			    struct cc_req_mgr_handle *req_mgr_h,
			    unsigned int total_seq_len)
{
	unsigned long poll_queue;
	struct device *dev = drvdata_to_dev(drvdata);

	/* SW queue is checked only once as it will not
	 * be changed during the poll because the spinlock_bh
	 * is held by the thread
	 */
	if (((req_mgr_h->req_queue_head + 1) & (MAX_REQUEST_QUEUE_SIZE - 1)) ==
	    req_mgr_h->req_queue_tail) {
		dev_err(dev, "SW FIFO is full. req_queue_head=%d sw_fifo_len=%d\n",
			req_mgr_h->req_queue_head, MAX_REQUEST_QUEUE_SIZE);
		return -ENOSPC;
	}

	if (req_mgr_h->q_free_slots >= total_seq_len)
		return 0;

	/* Wait for space in HW queue. Poll constant num of iterations. */
	for (poll_queue = 0; poll_queue < CC_MAX_POLL_ITER ; poll_queue++) {
		req_mgr_h->q_free_slots =
			cc_ioread(drvdata, CC_REG(DSCRPTR_QUEUE_CONTENT));
		if (req_mgr_h->q_free_slots < req_mgr_h->min_free_hw_slots)
			req_mgr_h->min_free_hw_slots = req_mgr_h->q_free_slots;

		if (req_mgr_h->q_free_slots >= total_seq_len) {
			/* If there is enough place return */
			return 0;
		}

		dev_dbg(dev, "HW FIFO is full. q_free_slots=%d total_seq_len=%d\n",
			req_mgr_h->q_free_slots, total_seq_len);
	}
	/* No room in the HW queue try again later */
	dev_dbg(dev, "HW FIFO full, timeout. req_queue_head=%d sw_fifo_len=%d q_free_slots=%d total_seq_len=%d\n",
		req_mgr_h->req_queue_head, MAX_REQUEST_QUEUE_SIZE,
		req_mgr_h->q_free_slots, total_seq_len);
	return -ENOSPC;
}

/**
 * cc_do_send_request() - Enqueue caller request to crypto hardware.
 * Need to be called with HW lock held and PM running
 *
 * @drvdata: Associated device driver context
 * @cc_req: The request to enqueue
 * @desc: The crypto sequence
 * @len: The crypto sequence length
 * @add_comp: If "true": add an artificial dout DMA to mark completion
 *
 */
static void cc_do_send_request(struct cc_drvdata *drvdata,
			       struct cc_crypto_req *cc_req,
			       struct cc_hw_desc *desc, unsigned int len,
			       bool add_comp)
{
	struct cc_req_mgr_handle *req_mgr_h = drvdata->request_mgr_handle;
	unsigned int used_sw_slots;
	unsigned int total_seq_len = len; /*initial sequence length*/
	struct device *dev = drvdata_to_dev(drvdata);

	used_sw_slots = ((req_mgr_h->req_queue_head -
			  req_mgr_h->req_queue_tail) &
			 (MAX_REQUEST_QUEUE_SIZE - 1));
	if (used_sw_slots > req_mgr_h->max_used_sw_slots)
		req_mgr_h->max_used_sw_slots = used_sw_slots;

	/* Enqueue request - must be locked with HW lock*/
	req_mgr_h->req_queue[req_mgr_h->req_queue_head] = *cc_req;
	req_mgr_h->req_queue_head = (req_mgr_h->req_queue_head + 1) &
				    (MAX_REQUEST_QUEUE_SIZE - 1);

	dev_dbg(dev, "Enqueue request head=%u\n", req_mgr_h->req_queue_head);

	/*
	 * We are about to push command to the HW via the command registers
	 * that may reference host memory. We need to issue a memory barrier
	 * to make sure there are no outstanding memory writes
	 */
	wmb();

	/* STAT_PHASE_4: Push sequence */

	enqueue_seq(drvdata, desc, len);

	if (add_comp) {
		enqueue_seq(drvdata, &req_mgr_h->compl_desc, 1);
		total_seq_len++;
	}

	if (req_mgr_h->q_free_slots < total_seq_len) {
		/* This situation should never occur. Maybe indicating problem
		 * with resuming power. Set the free slot count to 0 and hope
		 * for the best.
		 */
		dev_err(dev, "HW free slot count mismatch.");
		req_mgr_h->q_free_slots = 0;
	} else {
		/* Update the free slots in HW queue */
		req_mgr_h->q_free_slots -= total_seq_len;
	}
}

static void cc_enqueue_backlog(struct cc_drvdata *drvdata,
			       struct cc_bl_item *bli)
{
	struct cc_req_mgr_handle *mgr = drvdata->request_mgr_handle;
	struct device *dev = drvdata_to_dev(drvdata);

	spin_lock_bh(&mgr->bl_lock);
	list_add_tail(&bli->list, &mgr->backlog);
	++mgr->bl_len;
	dev_dbg(dev, "+++bl len: %d\n", mgr->bl_len);
	spin_unlock_bh(&mgr->bl_lock);
	tasklet_schedule(&mgr->comptask);
}

static void cc_proc_backlog(struct cc_drvdata *drvdata)
{
	struct cc_req_mgr_handle *mgr = drvdata->request_mgr_handle;
	struct cc_bl_item *bli;
	struct cc_crypto_req *creq;
	void *req;
	struct device *dev = drvdata_to_dev(drvdata);
	int rc;

	spin_lock(&mgr->bl_lock);

	while (mgr->bl_len) {
		bli = list_first_entry(&mgr->backlog, struct cc_bl_item, list);
		dev_dbg(dev, "---bl len: %d\n", mgr->bl_len);

		spin_unlock(&mgr->bl_lock);


		creq = &bli->creq;
		req = creq->user_arg;

		/*
		 * Notify the request we're moving out of the backlog
		 * but only if we haven't done so already.
		 */
		if (!bli->notif) {
			creq->user_cb(dev, req, -EINPROGRESS);
			bli->notif = true;
		}

		spin_lock(&mgr->hw_lock);

		rc = cc_queues_status(drvdata, mgr, bli->len);
		if (rc) {
			/*
			 * There is still no room in the FIFO for
			 * this request. Bail out. We'll return here
			 * on the next completion irq.
			 */
			spin_unlock(&mgr->hw_lock);
			return;
		}

		cc_do_send_request(drvdata, &bli->creq, bli->desc, bli->len,
				   false);
		spin_unlock(&mgr->hw_lock);

		/* Remove ourselves from the backlog list */
		spin_lock(&mgr->bl_lock);
		list_del(&bli->list);
		--mgr->bl_len;
		kfree(bli);
	}

	spin_unlock(&mgr->bl_lock);
}

int cc_send_request(struct cc_drvdata *drvdata, struct cc_crypto_req *cc_req,
		    struct cc_hw_desc *desc, unsigned int len,
		    struct crypto_async_request *req)
{
	int rc;
	struct cc_req_mgr_handle *mgr = drvdata->request_mgr_handle;
	struct device *dev = drvdata_to_dev(drvdata);
	bool backlog_ok = req->flags & CRYPTO_TFM_REQ_MAY_BACKLOG;
	gfp_t flags = cc_gfp_flags(req);
	struct cc_bl_item *bli;

	rc = cc_pm_get(dev);
	if (rc) {
		dev_err(dev, "cc_pm_get returned %x\n", rc);
		return rc;
	}

	spin_lock_bh(&mgr->hw_lock);
	rc = cc_queues_status(drvdata, mgr, len);

#ifdef CC_DEBUG_FORCE_BACKLOG
	if (backlog_ok)
		rc = -ENOSPC;
#endif /* CC_DEBUG_FORCE_BACKLOG */

	if (rc == -ENOSPC && backlog_ok) {
		spin_unlock_bh(&mgr->hw_lock);

		bli = kmalloc(sizeof(*bli), flags);
		if (!bli) {
			cc_pm_put_suspend(dev);
			return -ENOMEM;
		}

		memcpy(&bli->creq, cc_req, sizeof(*cc_req));
		memcpy(&bli->desc, desc, len * sizeof(*desc));
		bli->len = len;
		bli->notif = false;
		cc_enqueue_backlog(drvdata, bli);
		return -EBUSY;
	}

	if (!rc) {
		cc_do_send_request(drvdata, cc_req, desc, len, false);
		rc = -EINPROGRESS;
	}

	spin_unlock_bh(&mgr->hw_lock);
	return rc;
}

int cc_send_sync_request(struct cc_drvdata *drvdata,
			 struct cc_crypto_req *cc_req, struct cc_hw_desc *desc,
			 unsigned int len)
{
	int rc;
	struct device *dev = drvdata_to_dev(drvdata);
	struct cc_req_mgr_handle *mgr = drvdata->request_mgr_handle;

	init_completion(&cc_req->seq_compl);
	cc_req->user_cb = request_mgr_complete;
	cc_req->user_arg = &cc_req->seq_compl;

	rc = cc_pm_get(dev);
	if (rc) {
		dev_err(dev, "cc_pm_get returned %x\n", rc);
		return rc;
	}

	while (true) {
		spin_lock_bh(&mgr->hw_lock);
		rc = cc_queues_status(drvdata, mgr, len + 1);

		if (!rc)
			break;

		spin_unlock_bh(&mgr->hw_lock);
		wait_for_completion_interruptible(&drvdata->hw_queue_avail);
		reinit_completion(&drvdata->hw_queue_avail);
	}

	cc_do_send_request(drvdata, cc_req, desc, len, true);
	spin_unlock_bh(&mgr->hw_lock);
	wait_for_completion(&cc_req->seq_compl);
	return 0;
}

/**
 * send_request_init() - Enqueue caller request to crypto hardware during init
 * process.
 * Assume this function is not called in the middle of a flow,
 * since we set QUEUE_LAST_IND flag in the last descriptor.
 *
 * @drvdata: Associated device driver context
 * @desc: The crypto sequence
 * @len: The crypto sequence length
 *
 * Return:
 * Returns "0" upon success
 */
int send_request_init(struct cc_drvdata *drvdata, struct cc_hw_desc *desc,
		      unsigned int len)
{
	struct cc_req_mgr_handle *req_mgr_h = drvdata->request_mgr_handle;
	unsigned int total_seq_len = len; /*initial sequence length*/
	int rc = 0;

	/* Wait for space in HW and SW FIFO. Poll for as much as FIFO_TIMEOUT.
	 */
	rc = cc_queues_status(drvdata, req_mgr_h, total_seq_len);
	if (rc)
		return rc;

	set_queue_last_ind(drvdata, &desc[(len - 1)]);

	/*
	 * We are about to push command to the HW via the command registers
	 * that may reference host memory. We need to issue a memory barrier
	 * to make sure there are no outstanding memory writes
	 */
	wmb();
	enqueue_seq(drvdata, desc, len);

	/* Update the free slots in HW queue */
	req_mgr_h->q_free_slots =
		cc_ioread(drvdata, CC_REG(DSCRPTR_QUEUE_CONTENT));

	return 0;
}

void complete_request(struct cc_drvdata *drvdata)
{
	struct cc_req_mgr_handle *request_mgr_handle =
						drvdata->request_mgr_handle;

	complete(&drvdata->hw_queue_avail);
#ifdef COMP_IN_WQ
	queue_delayed_work(request_mgr_handle->workq,
			   &request_mgr_handle->compwork, 0);
#else
	tasklet_schedule(&request_mgr_handle->comptask);
#endif
}

#ifdef COMP_IN_WQ
static void comp_work_handler(struct work_struct *work)
{
	struct cc_drvdata *drvdata =
		container_of(work, struct cc_drvdata, compwork.work);

	comp_handler((unsigned long)drvdata);
}
#endif

static void proc_completions(struct cc_drvdata *drvdata)
{
	struct cc_crypto_req *cc_req;
	struct device *dev = drvdata_to_dev(drvdata);
	struct cc_req_mgr_handle *request_mgr_handle =
						drvdata->request_mgr_handle;
	unsigned int *tail = &request_mgr_handle->req_queue_tail;
	unsigned int *head = &request_mgr_handle->req_queue_head;
	int rc;
	u32 mask;

	while (request_mgr_handle->axi_completed) {
		request_mgr_handle->axi_completed--;

		/* Dequeue request */
		if (*head == *tail) {
			/* We are supposed to handle a completion but our
			 * queue is empty. This is not normal. Return and
			 * hope for the best.
			 */
			dev_err(dev, "Request queue is empty head == tail %u\n",
				*head);
			break;
		}

		cc_req = &request_mgr_handle->req_queue[*tail];

		if (cc_req->cpp.is_cpp) {

			dev_dbg(dev, "CPP request completion slot: %d alg:%d\n",
				cc_req->cpp.slot, cc_req->cpp.alg);
			mask = cc_cpp_int_mask(cc_req->cpp.alg,
					       cc_req->cpp.slot);
			rc = (drvdata->irq & mask ? -EPERM : 0);
			dev_dbg(dev, "Got mask: %x irq: %x rc: %d\n", mask,
				drvdata->irq, rc);
		} else {
			dev_dbg(dev, "None CPP request completion\n");
			rc = 0;
		}

		if (cc_req->user_cb)
			cc_req->user_cb(dev, cc_req->user_arg, rc);
		*tail = (*tail + 1) & (MAX_REQUEST_QUEUE_SIZE - 1);
		dev_dbg(dev, "Dequeue request tail=%u\n", *tail);
		dev_dbg(dev, "Request completed. axi_completed=%d\n",
			request_mgr_handle->axi_completed);
		cc_pm_put_suspend(dev);
	}
}

static inline u32 cc_axi_comp_count(struct cc_drvdata *drvdata)
{
	return FIELD_GET(AXIM_MON_COMP_VALUE,
			 cc_ioread(drvdata, drvdata->axim_mon_offset));
}

/* Deferred service handler, run as interrupt-fired tasklet */
static void comp_handler(unsigned long devarg)
{
	struct cc_drvdata *drvdata = (struct cc_drvdata *)devarg;
	struct cc_req_mgr_handle *request_mgr_handle =
						drvdata->request_mgr_handle;
	struct device *dev = drvdata_to_dev(drvdata);
	u32 irq;

	dev_dbg(dev, "Completion handler called!\n");
	irq = (drvdata->irq & drvdata->comp_mask);

	/* To avoid the interrupt from firing as we unmask it,
	 * we clear it now
	 */
	cc_iowrite(drvdata, CC_REG(HOST_ICR), irq);

	/* Avoid race with above clear: Test completion counter once more */

	request_mgr_handle->axi_completed += cc_axi_comp_count(drvdata);

	dev_dbg(dev, "AXI completion after updated: %d\n",
		request_mgr_handle->axi_completed);

	while (request_mgr_handle->axi_completed) {
		do {
			drvdata->irq |= cc_ioread(drvdata, CC_REG(HOST_IRR));
			irq = (drvdata->irq & drvdata->comp_mask);
			proc_completions(drvdata);

			/* At this point (after proc_completions()),
			 * request_mgr_handle->axi_completed is 0.
			 */
			request_mgr_handle->axi_completed +=
						cc_axi_comp_count(drvdata);
		} while (request_mgr_handle->axi_completed > 0);

		cc_iowrite(drvdata, CC_REG(HOST_ICR), irq);

		request_mgr_handle->axi_completed += cc_axi_comp_count(drvdata);
	}

	/* after verifying that there is nothing to do,
	 * unmask AXI completion interrupt
	 */
	cc_iowrite(drvdata, CC_REG(HOST_IMR),
		   cc_ioread(drvdata, CC_REG(HOST_IMR)) & ~drvdata->comp_mask);

	cc_proc_backlog(drvdata);
	dev_dbg(dev, "Comp. handler done.\n");
}

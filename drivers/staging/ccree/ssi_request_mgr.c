/*
 * Copyright (C) 2012-2017 ARM Limited or its affiliates.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <crypto/ctr.h>
#include <linux/pm_runtime.h>
#include "ssi_driver.h"
#include "ssi_buffer_mgr.h"
#include "ssi_request_mgr.h"
#include "ssi_ivgen.h"
#include "ssi_pm.h"

#define CC_MAX_POLL_ITER	10

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
	struct cc_hw_desc monitor_desc;

#ifdef COMP_IN_WQ
	struct workqueue_struct *workq;
	struct delayed_work compwork;
#else
	struct tasklet_struct comptask;
#endif
#if defined(CONFIG_PM)
	bool is_runtime_suspended;
#endif
};

static void comp_handler(unsigned long devarg);
#ifdef COMP_IN_WQ
static void comp_work_handler(struct work_struct *work);
#endif

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
	memset(req_mgr_h, 0, sizeof(struct cc_req_mgr_handle));
	kfree(req_mgr_h);
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
#ifdef COMP_IN_WQ
	dev_dbg(dev, "Initializing completion workqueue\n");
	req_mgr_h->workq = create_singlethread_workqueue("arm_cc7x_wq");
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
	set_queue_last_ind(&req_mgr_h->compl_desc);

	return 0;

req_mgr_init_err:
	cc_req_mgr_fini(drvdata);
	return rc;
}

static void enqueue_seq(struct cc_drvdata *drvdata, struct cc_hw_desc seq[],
			unsigned int seq_len)
{
	int i, w;
	void * __iomem reg = drvdata->cc_base + CC_REG(DSCRPTR_QUEUE_WORD0);
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

/*!
 * Completion will take place if and only if user requested completion
 * by setting "is_dout = 0" in send_request().
 *
 * \param dev
 * \param dx_compl_h The completion event to signal
 */
static void request_mgr_complete(struct device *dev, void *dx_compl_h)
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
	 * be chaned during the poll because the spinlock_bh
	 * is held by the thread
	 */
	if (((req_mgr_h->req_queue_head + 1) & (MAX_REQUEST_QUEUE_SIZE - 1)) ==
	    req_mgr_h->req_queue_tail) {
		dev_err(dev, "SW FIFO is full. req_queue_head=%d sw_fifo_len=%d\n",
			req_mgr_h->req_queue_head, MAX_REQUEST_QUEUE_SIZE);
		return -EBUSY;
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
	return -EAGAIN;
}

/*!
 * Enqueue caller request to crypto hardware.
 *
 * \param drvdata
 * \param cc_req The request to enqueue
 * \param desc The crypto sequence
 * \param len The crypto sequence length
 * \param is_dout If "true": completion is handled by the caller
 *	  If "false": this function adds a dummy descriptor completion
 *	  and waits upon completion signal.
 *
 * \return int Returns -EINPROGRESS if "is_dout=true"; "0" if "is_dout=false"
 */
int send_request(struct cc_drvdata *drvdata, struct cc_crypto_req *cc_req,
		 struct cc_hw_desc *desc, unsigned int len, bool is_dout)
{
	struct cc_req_mgr_handle *req_mgr_h = drvdata->request_mgr_handle;
	unsigned int used_sw_slots;
	unsigned int iv_seq_len = 0;
	unsigned int total_seq_len = len; /*initial sequence length*/
	struct cc_hw_desc iv_seq[CC_IVPOOL_SEQ_LEN];
	struct device *dev = drvdata_to_dev(drvdata);
	int rc;
	unsigned int max_required_seq_len =
		(total_seq_len +
		 ((cc_req->ivgen_dma_addr_len == 0) ? 0 :
		  CC_IVPOOL_SEQ_LEN) + (!is_dout ? 1 : 0));

#if defined(CONFIG_PM)
	rc = cc_pm_get(dev);
	if (rc) {
		dev_err(dev, "ssi_power_mgr_runtime_get returned %x\n", rc);
		return rc;
	}
#endif

	do {
		spin_lock_bh(&req_mgr_h->hw_lock);

		/* Check if there is enough place in the SW/HW queues
		 * in case iv gen add the max size and in case of no dout add 1
		 * for the internal completion descriptor
		 */
		rc = cc_queues_status(drvdata, req_mgr_h, max_required_seq_len);
		if (rc == 0)
			/* There is enough place in the queue */
			break;
		/* something wrong release the spinlock*/
		spin_unlock_bh(&req_mgr_h->hw_lock);

		if (rc != -EAGAIN) {
			/* Any error other than HW queue full
			 * (SW queue is full)
			 */
#if defined(CONFIG_PM)
			cc_pm_put_suspend(dev);
#endif
			return rc;
		}

		/* HW queue is full - wait for it to clear up */
		wait_for_completion_interruptible(&drvdata->hw_queue_avail);
		reinit_completion(&drvdata->hw_queue_avail);
	} while (1);

	/* Additional completion descriptor is needed incase caller did not
	 * enabled any DLLI/MLLI DOUT bit in the given sequence
	 */
	if (!is_dout) {
		init_completion(&cc_req->seq_compl);
		cc_req->user_cb = request_mgr_complete;
		cc_req->user_arg = &cc_req->seq_compl;
		total_seq_len++;
	}

	if (cc_req->ivgen_dma_addr_len > 0) {
		dev_dbg(dev, "Acquire IV from pool into %d DMA addresses %pad, %pad, %pad, IV-size=%u\n",
			cc_req->ivgen_dma_addr_len,
			&cc_req->ivgen_dma_addr[0],
			&cc_req->ivgen_dma_addr[1],
			&cc_req->ivgen_dma_addr[2],
			cc_req->ivgen_size);

		/* Acquire IV from pool */
		rc = cc_get_iv(drvdata, cc_req->ivgen_dma_addr,
			       cc_req->ivgen_dma_addr_len,
			       cc_req->ivgen_size,
			       iv_seq, &iv_seq_len);

		if (rc) {
			dev_err(dev, "Failed to generate IV (rc=%d)\n", rc);
			spin_unlock_bh(&req_mgr_h->hw_lock);
#if defined(CONFIG_PM)
			cc_pm_put_suspend(dev);
#endif
			return rc;
		}

		total_seq_len += iv_seq_len;
	}

	used_sw_slots = ((req_mgr_h->req_queue_head -
			  req_mgr_h->req_queue_tail) &
			 (MAX_REQUEST_QUEUE_SIZE - 1));
	if (used_sw_slots > req_mgr_h->max_used_sw_slots)
		req_mgr_h->max_used_sw_slots = used_sw_slots;

	/* Enqueue request - must be locked with HW lock*/
	req_mgr_h->req_queue[req_mgr_h->req_queue_head] = *cc_req;
	req_mgr_h->req_queue_head = (req_mgr_h->req_queue_head + 1) &
				    (MAX_REQUEST_QUEUE_SIZE - 1);
	/* TODO: Use circ_buf.h ? */

	dev_dbg(dev, "Enqueue request head=%u\n", req_mgr_h->req_queue_head);

	/*
	 * We are about to push command to the HW via the command registers
	 * that may refernece hsot memory. We need to issue a memory barrier
	 * to make sure there are no outstnading memory writes
	 */
	wmb();

	/* STAT_PHASE_4: Push sequence */
	enqueue_seq(drvdata, iv_seq, iv_seq_len);
	enqueue_seq(drvdata, desc, len);
	enqueue_seq(drvdata, &req_mgr_h->compl_desc, (is_dout ? 0 : 1));

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

	spin_unlock_bh(&req_mgr_h->hw_lock);

	if (!is_dout) {
		/* Wait upon sequence completion.
		 *  Return "0" -Operation done successfully.
		 */
		wait_for_completion(&cc_req->seq_compl);
		return 0;
	}
	/* Operation still in process */
	return -EINPROGRESS;
}

/*!
 * Enqueue caller request to crypto hardware during init process.
 * assume this function is not called in middle of a flow,
 * since we set QUEUE_LAST_IND flag in the last descriptor.
 *
 * \param drvdata
 * \param desc The crypto sequence
 * \param len The crypto sequence length
 *
 * \return int Returns "0" upon success
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

	set_queue_last_ind(&desc[(len - 1)]);

	/*
	 * We are about to push command to the HW via the command registers
	 * that may refernece hsot memory. We need to issue a memory barrier
	 * to make sure there are no outstnading memory writes
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
#if defined(CONFIG_PM)
	int rc = 0;
#endif

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

		if (cc_req->user_cb)
			cc_req->user_cb(dev, cc_req->user_arg);
		*tail = (*tail + 1) & (MAX_REQUEST_QUEUE_SIZE - 1);
		dev_dbg(dev, "Dequeue request tail=%u\n", *tail);
		dev_dbg(dev, "Request completed. axi_completed=%d\n",
			request_mgr_handle->axi_completed);
#if defined(CONFIG_PM)
		rc = cc_pm_put_suspend(dev);
		if (rc)
			dev_err(dev, "Failed to set runtime suspension %d\n",
				rc);
#endif
	}
}

static inline u32 cc_axi_comp_count(struct cc_drvdata *drvdata)
{
	return FIELD_GET(AXIM_MON_COMP_VALUE,
			 cc_ioread(drvdata, CC_REG(AXIM_MON_COMP)));
}

/* Deferred service handler, run as interrupt-fired tasklet */
static void comp_handler(unsigned long devarg)
{
	struct cc_drvdata *drvdata = (struct cc_drvdata *)devarg;
	struct cc_req_mgr_handle *request_mgr_handle =
						drvdata->request_mgr_handle;

	u32 irq;

	irq = (drvdata->irq & CC_COMP_IRQ_MASK);

	if (irq & CC_COMP_IRQ_MASK) {
		/* To avoid the interrupt from firing as we unmask it,
		 * we clear it now
		 */
		cc_iowrite(drvdata, CC_REG(HOST_ICR), CC_COMP_IRQ_MASK);

		/* Avoid race with above clear: Test completion counter
		 * once more
		 */
		request_mgr_handle->axi_completed +=
				cc_axi_comp_count(drvdata);

		while (request_mgr_handle->axi_completed) {
			do {
				proc_completions(drvdata);
				/* At this point (after proc_completions()),
				 * request_mgr_handle->axi_completed is 0.
				 */
				request_mgr_handle->axi_completed =
						cc_axi_comp_count(drvdata);
			} while (request_mgr_handle->axi_completed > 0);

			cc_iowrite(drvdata, CC_REG(HOST_ICR),
				   CC_COMP_IRQ_MASK);

			request_mgr_handle->axi_completed +=
					cc_axi_comp_count(drvdata);
		}
	}
	/* after verifing that there is nothing to do,
	 * unmask AXI completion interrupt
	 */
	cc_iowrite(drvdata, CC_REG(HOST_IMR),
		   cc_ioread(drvdata, CC_REG(HOST_IMR)) & ~irq);
}

/*
 * resume the queue configuration - no need to take the lock as this happens
 * inside the spin lock protection
 */
#if defined(CONFIG_PM)
int cc_resume_req_queue(struct cc_drvdata *drvdata)
{
	struct cc_req_mgr_handle *request_mgr_handle =
		drvdata->request_mgr_handle;

	spin_lock_bh(&request_mgr_handle->hw_lock);
	request_mgr_handle->is_runtime_suspended = false;
	spin_unlock_bh(&request_mgr_handle->hw_lock);

	return 0;
}

/*
 * suspend the queue configuration. Since it is used for the runtime suspend
 * only verify that the queue can be suspended.
 */
int cc_suspend_req_queue(struct cc_drvdata *drvdata)
{
	struct cc_req_mgr_handle *request_mgr_handle =
						drvdata->request_mgr_handle;

	/* lock the send_request */
	spin_lock_bh(&request_mgr_handle->hw_lock);
	if (request_mgr_handle->req_queue_head !=
	    request_mgr_handle->req_queue_tail) {
		spin_unlock_bh(&request_mgr_handle->hw_lock);
		return -EBUSY;
	}
	request_mgr_handle->is_runtime_suspended = true;
	spin_unlock_bh(&request_mgr_handle->hw_lock);

	return 0;
}

bool cc_req_queue_suspended(struct cc_drvdata *drvdata)
{
	struct cc_req_mgr_handle *request_mgr_handle =
						drvdata->request_mgr_handle;

	return	request_mgr_handle->is_runtime_suspended;
}

#endif


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

#include "ssi_config.h"
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <crypto/ctr.h>
#ifdef FLUSH_CACHE_ALL
#include <asm/cacheflush.h>
#endif
#include <linux/pm_runtime.h>
#include "ssi_driver.h"
#include "ssi_buffer_mgr.h"
#include "ssi_request_mgr.h"
#include "ssi_sysfs.h"
#include "ssi_ivgen.h"
#include "ssi_pm.h"

#define SSI_MAX_POLL_ITER	10

struct ssi_request_mgr_handle {
	/* Request manager resources */
	unsigned int hw_queue_size; /* HW capability */
	unsigned int min_free_hw_slots;
	unsigned int max_used_sw_slots;
	struct ssi_crypto_req req_queue[MAX_REQUEST_QUEUE_SIZE];
	u32 req_queue_head;
	u32 req_queue_tail;
	u32 axi_completed;
	u32 q_free_slots;
	spinlock_t hw_lock;
	struct cc_hw_desc compl_desc;
	u8 *dummy_comp_buff;
	dma_addr_t dummy_comp_buff_dma;
	struct cc_hw_desc monitor_desc;

	volatile unsigned long monitor_lock;
#ifdef COMP_IN_WQ
	struct workqueue_struct *workq;
	struct delayed_work compwork;
#else
	struct tasklet_struct comptask;
#endif
#if defined(CONFIG_PM_RUNTIME) || defined(CONFIG_PM_SLEEP)
	bool is_runtime_suspended;
#endif
};

static void comp_handler(unsigned long devarg);
#ifdef COMP_IN_WQ
static void comp_work_handler(struct work_struct *work);
#endif

void request_mgr_fini(struct ssi_drvdata *drvdata)
{
	struct ssi_request_mgr_handle *req_mgr_h = drvdata->request_mgr_handle;

	if (!req_mgr_h)
		return; /* Not allocated */

	if (req_mgr_h->dummy_comp_buff_dma != 0) {
		dma_free_coherent(&drvdata->plat_dev->dev,
				  sizeof(u32), req_mgr_h->dummy_comp_buff,
				  req_mgr_h->dummy_comp_buff_dma);
	}

	SSI_LOG_DEBUG("max_used_hw_slots=%d\n", (req_mgr_h->hw_queue_size -
						req_mgr_h->min_free_hw_slots));
	SSI_LOG_DEBUG("max_used_sw_slots=%d\n", req_mgr_h->max_used_sw_slots);

#ifdef COMP_IN_WQ
	flush_workqueue(req_mgr_h->workq);
	destroy_workqueue(req_mgr_h->workq);
#else
	/* Kill tasklet */
	tasklet_kill(&req_mgr_h->comptask);
#endif
	memset(req_mgr_h, 0, sizeof(struct ssi_request_mgr_handle));
	kfree(req_mgr_h);
	drvdata->request_mgr_handle = NULL;
}

int request_mgr_init(struct ssi_drvdata *drvdata)
{
	struct ssi_request_mgr_handle *req_mgr_h;
	int rc = 0;

	req_mgr_h = kzalloc(sizeof(struct ssi_request_mgr_handle), GFP_KERNEL);
	if (!req_mgr_h) {
		rc = -ENOMEM;
		goto req_mgr_init_err;
	}

	drvdata->request_mgr_handle = req_mgr_h;

	spin_lock_init(&req_mgr_h->hw_lock);
#ifdef COMP_IN_WQ
	SSI_LOG_DEBUG("Initializing completion workqueue\n");
	req_mgr_h->workq = create_singlethread_workqueue("arm_cc7x_wq");
	if (unlikely(!req_mgr_h->workq)) {
		SSI_LOG_ERR("Failed creating work queue\n");
		rc = -ENOMEM;
		goto req_mgr_init_err;
	}
	INIT_DELAYED_WORK(&req_mgr_h->compwork, comp_work_handler);
#else
	SSI_LOG_DEBUG("Initializing completion tasklet\n");
	tasklet_init(&req_mgr_h->comptask, comp_handler, (unsigned long)drvdata);
#endif
	req_mgr_h->hw_queue_size = READ_REGISTER(drvdata->cc_base +
		CC_REG_OFFSET(CRY_KERNEL, DSCRPTR_QUEUE_SRAM_SIZE));
	SSI_LOG_DEBUG("hw_queue_size=0x%08X\n", req_mgr_h->hw_queue_size);
	if (req_mgr_h->hw_queue_size < MIN_HW_QUEUE_SIZE) {
		SSI_LOG_ERR("Invalid HW queue size = %u (Min. required is %u)\n",
			    req_mgr_h->hw_queue_size, MIN_HW_QUEUE_SIZE);
		rc = -ENOMEM;
		goto req_mgr_init_err;
	}
	req_mgr_h->min_free_hw_slots = req_mgr_h->hw_queue_size;
	req_mgr_h->max_used_sw_slots = 0;

	/* Allocate DMA word for "dummy" completion descriptor use */
	req_mgr_h->dummy_comp_buff = dma_alloc_coherent(&drvdata->plat_dev->dev,
		sizeof(u32), &req_mgr_h->dummy_comp_buff_dma, GFP_KERNEL);
	if (!req_mgr_h->dummy_comp_buff) {
		SSI_LOG_ERR("Not enough memory to allocate DMA (%zu) dropped "
			   "buffer\n", sizeof(u32));
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
	request_mgr_fini(drvdata);
	return rc;
}

static inline void enqueue_seq(
	void __iomem *cc_base,
	struct cc_hw_desc seq[], unsigned int seq_len)
{
	int i;

	for (i = 0; i < seq_len; i++) {
		writel_relaxed(seq[i].word[0], (volatile void __iomem *)(cc_base + CC_REG_OFFSET(CRY_KERNEL, DSCRPTR_QUEUE_WORD0)));
		writel_relaxed(seq[i].word[1], (volatile void __iomem *)(cc_base + CC_REG_OFFSET(CRY_KERNEL, DSCRPTR_QUEUE_WORD0)));
		writel_relaxed(seq[i].word[2], (volatile void __iomem *)(cc_base + CC_REG_OFFSET(CRY_KERNEL, DSCRPTR_QUEUE_WORD0)));
		writel_relaxed(seq[i].word[3], (volatile void __iomem *)(cc_base + CC_REG_OFFSET(CRY_KERNEL, DSCRPTR_QUEUE_WORD0)));
		writel_relaxed(seq[i].word[4], (volatile void __iomem *)(cc_base + CC_REG_OFFSET(CRY_KERNEL, DSCRPTR_QUEUE_WORD0)));
		wmb();
		writel_relaxed(seq[i].word[5], (volatile void __iomem *)(cc_base + CC_REG_OFFSET(CRY_KERNEL, DSCRPTR_QUEUE_WORD0)));
#ifdef DX_DUMP_DESCS
		SSI_LOG_DEBUG("desc[%02d]: 0x%08X 0x%08X 0x%08X 0x%08X 0x%08X 0x%08X\n", i,
			      seq[i].word[0], seq[i].word[1], seq[i].word[2],
			      seq[i].word[3], seq[i].word[4], seq[i].word[5]);
#endif
	}
}

/*!
 * Completion will take place if and only if user requested completion
 * by setting "is_dout = 0" in send_request().
 *
 * \param dev
 * \param dx_compl_h The completion event to signal
 */
static void request_mgr_complete(struct device *dev, void *dx_compl_h, void __iomem *cc_base)
{
	struct completion *this_compl = dx_compl_h;

	complete(this_compl);
}

static inline int request_mgr_queues_status_check(
		struct ssi_request_mgr_handle *req_mgr_h,
		void __iomem *cc_base,
		unsigned int total_seq_len)
{
	unsigned long poll_queue;

	/* SW queue is checked only once as it will not
	 * be chaned during the poll becasue the spinlock_bh
	 * is held by the thread
	 */
	if (unlikely(((req_mgr_h->req_queue_head + 1) &
		      (MAX_REQUEST_QUEUE_SIZE - 1)) ==
		     req_mgr_h->req_queue_tail)) {
		SSI_LOG_ERR("SW FIFO is full. req_queue_head=%d sw_fifo_len=%d\n",
			    req_mgr_h->req_queue_head, MAX_REQUEST_QUEUE_SIZE);
		return -EBUSY;
	}

	if ((likely(req_mgr_h->q_free_slots >= total_seq_len)))
		return 0;

	/* Wait for space in HW queue. Poll constant num of iterations. */
	for (poll_queue = 0; poll_queue < SSI_MAX_POLL_ITER ; poll_queue++) {
		req_mgr_h->q_free_slots =
			CC_HAL_READ_REGISTER(CC_REG_OFFSET(CRY_KERNEL,
							   DSCRPTR_QUEUE_CONTENT));
		if (unlikely(req_mgr_h->q_free_slots <
						req_mgr_h->min_free_hw_slots)) {
			req_mgr_h->min_free_hw_slots = req_mgr_h->q_free_slots;
		}

		if (likely(req_mgr_h->q_free_slots >= total_seq_len)) {
			/* If there is enough place return */
			return 0;
		}

		SSI_LOG_DEBUG("HW FIFO is full. q_free_slots=%d total_seq_len=%d\n",
			      req_mgr_h->q_free_slots, total_seq_len);
	}
	/* No room in the HW queue try again later */
	SSI_LOG_DEBUG("HW FIFO full, timeout. req_queue_head=%d "
		   "sw_fifo_len=%d q_free_slots=%d total_seq_len=%d\n",
		     req_mgr_h->req_queue_head,
		   MAX_REQUEST_QUEUE_SIZE,
		   req_mgr_h->q_free_slots,
		   total_seq_len);
	return -EAGAIN;
}

/*!
 * Enqueue caller request to crypto hardware.
 *
 * \param drvdata
 * \param ssi_req The request to enqueue
 * \param desc The crypto sequence
 * \param len The crypto sequence length
 * \param is_dout If "true": completion is handled by the caller
 *	  If "false": this function adds a dummy descriptor completion
 *	  and waits upon completion signal.
 *
 * \return int Returns -EINPROGRESS if "is_dout=true"; "0" if "is_dout=false"
 */
int send_request(
	struct ssi_drvdata *drvdata, struct ssi_crypto_req *ssi_req,
	struct cc_hw_desc *desc, unsigned int len, bool is_dout)
{
	void __iomem *cc_base = drvdata->cc_base;
	struct ssi_request_mgr_handle *req_mgr_h = drvdata->request_mgr_handle;
	unsigned int used_sw_slots;
	unsigned int iv_seq_len = 0;
	unsigned int total_seq_len = len; /*initial sequence length*/
	struct cc_hw_desc iv_seq[SSI_IVPOOL_SEQ_LEN];
	int rc;
	unsigned int max_required_seq_len = (total_seq_len +
					((ssi_req->ivgen_dma_addr_len == 0) ? 0 :
					SSI_IVPOOL_SEQ_LEN) +
					((is_dout == 0) ? 1 : 0));

#if defined(CONFIG_PM_RUNTIME) || defined(CONFIG_PM_SLEEP)
	rc = ssi_power_mgr_runtime_get(&drvdata->plat_dev->dev);
	if (rc != 0) {
		SSI_LOG_ERR("ssi_power_mgr_runtime_get returned %x\n", rc);
		return rc;
	}
#endif

	do {
		spin_lock_bh(&req_mgr_h->hw_lock);

		/* Check if there is enough place in the SW/HW queues
		 * in case iv gen add the max size and in case of no dout add 1
		 * for the internal completion descriptor
		 */
		rc = request_mgr_queues_status_check(req_mgr_h, cc_base,
						     max_required_seq_len);
		if (likely(rc == 0))
			/* There is enough place in the queue */
			break;
		/* something wrong release the spinlock*/
		spin_unlock_bh(&req_mgr_h->hw_lock);

		if (rc != -EAGAIN) {
			/* Any error other than HW queue full
			 * (SW queue is full)
			 */
#if defined(CONFIG_PM_RUNTIME) || defined(CONFIG_PM_SLEEP)
			ssi_power_mgr_runtime_put_suspend(&drvdata->plat_dev->dev);
#endif
			return rc;
		}

		/* HW queue is full - short sleep */
		msleep(1);
	} while (1);

	/* Additional completion descriptor is needed incase caller did not
	 * enabled any DLLI/MLLI DOUT bit in the given sequence
	 */
	if (!is_dout) {
		init_completion(&ssi_req->seq_compl);
		ssi_req->user_cb = request_mgr_complete;
		ssi_req->user_arg = &ssi_req->seq_compl;
		total_seq_len++;
	}

	if (ssi_req->ivgen_dma_addr_len > 0) {
		SSI_LOG_DEBUG("Acquire IV from pool into %d DMA addresses 0x%llX, 0x%llX, 0x%llX, IV-size=%u\n",
			      ssi_req->ivgen_dma_addr_len,
			      (unsigned long long)ssi_req->ivgen_dma_addr[0],
			      (unsigned long long)ssi_req->ivgen_dma_addr[1],
			      (unsigned long long)ssi_req->ivgen_dma_addr[2],
			      ssi_req->ivgen_size);

		/* Acquire IV from pool */
		rc = ssi_ivgen_getiv(drvdata, ssi_req->ivgen_dma_addr,
				     ssi_req->ivgen_dma_addr_len,
				     ssi_req->ivgen_size, iv_seq, &iv_seq_len);

		if (unlikely(rc != 0)) {
			SSI_LOG_ERR("Failed to generate IV (rc=%d)\n", rc);
			spin_unlock_bh(&req_mgr_h->hw_lock);
#if defined(CONFIG_PM_RUNTIME) || defined(CONFIG_PM_SLEEP)
			ssi_power_mgr_runtime_put_suspend(&drvdata->plat_dev->dev);
#endif
			return rc;
		}

		total_seq_len += iv_seq_len;
	}

	used_sw_slots = ((req_mgr_h->req_queue_head - req_mgr_h->req_queue_tail) & (MAX_REQUEST_QUEUE_SIZE - 1));
	if (unlikely(used_sw_slots > req_mgr_h->max_used_sw_slots))
		req_mgr_h->max_used_sw_slots = used_sw_slots;

	/* Enqueue request - must be locked with HW lock*/
	req_mgr_h->req_queue[req_mgr_h->req_queue_head] = *ssi_req;
	req_mgr_h->req_queue_head = (req_mgr_h->req_queue_head + 1) & (MAX_REQUEST_QUEUE_SIZE - 1);
	/* TODO: Use circ_buf.h ? */

	SSI_LOG_DEBUG("Enqueue request head=%u\n", req_mgr_h->req_queue_head);

#ifdef FLUSH_CACHE_ALL
	flush_cache_all();
#endif

	/* STAT_PHASE_4: Push sequence */
	enqueue_seq(cc_base, iv_seq, iv_seq_len);
	enqueue_seq(cc_base, desc, len);
	enqueue_seq(cc_base, &req_mgr_h->compl_desc, (is_dout ? 0 : 1));

	if (unlikely(req_mgr_h->q_free_slots < total_seq_len)) {
		/*This means that there was a problem with the resume*/
		BUG();
	}
	/* Update the free slots in HW queue */
	req_mgr_h->q_free_slots -= total_seq_len;

	spin_unlock_bh(&req_mgr_h->hw_lock);

	if (!is_dout) {
		/* Wait upon sequence completion.
		 *  Return "0" -Operation done successfully.
		 */
		wait_for_completion(&ssi_req->seq_compl);
		return 0;
	} else {
		/* Operation still in process */
		return -EINPROGRESS;
	}
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
int send_request_init(
	struct ssi_drvdata *drvdata, struct cc_hw_desc *desc, unsigned int len)
{
	void __iomem *cc_base = drvdata->cc_base;
	struct ssi_request_mgr_handle *req_mgr_h = drvdata->request_mgr_handle;
	unsigned int total_seq_len = len; /*initial sequence length*/
	int rc = 0;

	/* Wait for space in HW and SW FIFO. Poll for as much as FIFO_TIMEOUT. */
	rc = request_mgr_queues_status_check(req_mgr_h, cc_base, total_seq_len);
	if (unlikely(rc != 0))
		return rc;

	set_queue_last_ind(&desc[(len - 1)]);

	enqueue_seq(cc_base, desc, len);

	/* Update the free slots in HW queue */
	req_mgr_h->q_free_slots = CC_HAL_READ_REGISTER(CC_REG_OFFSET(CRY_KERNEL,
								     DSCRPTR_QUEUE_CONTENT));

	return 0;
}

void complete_request(struct ssi_drvdata *drvdata)
{
	struct ssi_request_mgr_handle *request_mgr_handle =
						drvdata->request_mgr_handle;
#ifdef COMP_IN_WQ
	queue_delayed_work(request_mgr_handle->workq, &request_mgr_handle->compwork, 0);
#else
	tasklet_schedule(&request_mgr_handle->comptask);
#endif
}

#ifdef COMP_IN_WQ
static void comp_work_handler(struct work_struct *work)
{
	struct ssi_drvdata *drvdata =
		container_of(work, struct ssi_drvdata, compwork.work);

	comp_handler((unsigned long)drvdata);
}
#endif

static void proc_completions(struct ssi_drvdata *drvdata)
{
	struct ssi_crypto_req *ssi_req;
	struct platform_device *plat_dev = drvdata->plat_dev;
	struct ssi_request_mgr_handle *request_mgr_handle =
						drvdata->request_mgr_handle;
#if defined(CONFIG_PM_RUNTIME) || defined(CONFIG_PM_SLEEP)
	int rc = 0;
#endif

	while (request_mgr_handle->axi_completed) {
		request_mgr_handle->axi_completed--;

		/* Dequeue request */
		if (unlikely(request_mgr_handle->req_queue_head == request_mgr_handle->req_queue_tail)) {
			SSI_LOG_ERR("Request queue is empty req_queue_head==req_queue_tail==%u\n", request_mgr_handle->req_queue_head);
			BUG();
		}

		ssi_req = &request_mgr_handle->req_queue[request_mgr_handle->req_queue_tail];

#ifdef FLUSH_CACHE_ALL
		flush_cache_all();
#endif

#ifdef COMPLETION_DELAY
		/* Delay */
		{
			u32 axi_err;
			int i;

			SSI_LOG_INFO("Delay\n");
			for (i = 0; i < 1000000; i++)
				axi_err = READ_REGISTER(drvdata->cc_base + CC_REG_OFFSET(CRY_KERNEL, AXIM_MON_ERR));
		}
#endif /* COMPLETION_DELAY */

		if (likely(ssi_req->user_cb))
			ssi_req->user_cb(&plat_dev->dev, ssi_req->user_arg, drvdata->cc_base);
		request_mgr_handle->req_queue_tail = (request_mgr_handle->req_queue_tail + 1) & (MAX_REQUEST_QUEUE_SIZE - 1);
		SSI_LOG_DEBUG("Dequeue request tail=%u\n", request_mgr_handle->req_queue_tail);
		SSI_LOG_DEBUG("Request completed. axi_completed=%d\n", request_mgr_handle->axi_completed);
#if defined(CONFIG_PM_RUNTIME) || defined(CONFIG_PM_SLEEP)
		rc = ssi_power_mgr_runtime_put_suspend(&plat_dev->dev);
		if (rc != 0)
			SSI_LOG_ERR("Failed to set runtime suspension %d\n", rc);
#endif
	}
}

static inline u32 cc_axi_comp_count(void __iomem *cc_base)
{
	/* The CC_HAL_READ_REGISTER macro implictly requires and uses
	 * a base MMIO register address variable named cc_base.
	 */
	return FIELD_GET(AXIM_MON_COMP_VALUE,
			 CC_HAL_READ_REGISTER(AXIM_MON_BASE_OFFSET));
}

/* Deferred service handler, run as interrupt-fired tasklet */
static void comp_handler(unsigned long devarg)
{
	struct ssi_drvdata *drvdata = (struct ssi_drvdata *)devarg;
	void __iomem *cc_base = drvdata->cc_base;
	struct ssi_request_mgr_handle *request_mgr_handle =
						drvdata->request_mgr_handle;

	u32 irq;

	irq = (drvdata->irq & SSI_COMP_IRQ_MASK);

	if (irq & SSI_COMP_IRQ_MASK) {
		/* To avoid the interrupt from firing as we unmask it, we clear it now */
		CC_HAL_WRITE_REGISTER(CC_REG_OFFSET(HOST_RGF, HOST_ICR), SSI_COMP_IRQ_MASK);

		/* Avoid race with above clear: Test completion counter once more */
		request_mgr_handle->axi_completed +=
				cc_axi_comp_count(cc_base);

		while (request_mgr_handle->axi_completed) {
			do {
				proc_completions(drvdata);
				/* At this point (after proc_completions()),
				 * request_mgr_handle->axi_completed is 0.
				 */
				request_mgr_handle->axi_completed =
						cc_axi_comp_count(cc_base);
			} while (request_mgr_handle->axi_completed > 0);

			/* To avoid the interrupt from firing as we unmask it, we clear it now */
			CC_HAL_WRITE_REGISTER(CC_REG_OFFSET(HOST_RGF, HOST_ICR), SSI_COMP_IRQ_MASK);

			/* Avoid race with above clear: Test completion counter once more */
			request_mgr_handle->axi_completed +=
					cc_axi_comp_count(cc_base);
		}
	}
	/* after verifing that there is nothing to do, Unmask AXI completion interrupt */
	CC_HAL_WRITE_REGISTER(CC_REG_OFFSET(HOST_RGF, HOST_IMR),
			      CC_HAL_READ_REGISTER(CC_REG_OFFSET(HOST_RGF, HOST_IMR)) & ~irq);
}

/*
 * resume the queue configuration - no need to take the lock as this happens inside
 * the spin lock protection
 */
#if defined(CONFIG_PM_RUNTIME) || defined(CONFIG_PM_SLEEP)
int ssi_request_mgr_runtime_resume_queue(struct ssi_drvdata *drvdata)
{
	struct ssi_request_mgr_handle *request_mgr_handle = drvdata->request_mgr_handle;

	spin_lock_bh(&request_mgr_handle->hw_lock);
	request_mgr_handle->is_runtime_suspended = false;
	spin_unlock_bh(&request_mgr_handle->hw_lock);

	return 0;
}

/*
 * suspend the queue configuration. Since it is used for the runtime suspend
 * only verify that the queue can be suspended.
 */
int ssi_request_mgr_runtime_suspend_queue(struct ssi_drvdata *drvdata)
{
	struct ssi_request_mgr_handle *request_mgr_handle =
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

bool ssi_request_mgr_is_queue_runtime_suspend(struct ssi_drvdata *drvdata)
{
	struct ssi_request_mgr_handle *request_mgr_handle =
						drvdata->request_mgr_handle;

	return	request_mgr_handle->is_runtime_suspended;
}

#endif


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
#include "ssi_fips.h"
#include "ssi_fips_local.h"

#define SSI_MAX_POLL_ITER	10

#define AXIM_MON_BASE_OFFSET CC_REG_OFFSET(CRY_KERNEL, AXIM_MON_COMP)

#ifdef CC_CYCLE_COUNT

#define MONITOR_CNTR_BIT 0

/**
 * Monitor descriptor.
 * Used to measure CC performance.
 */
#define INIT_CC_MONITOR_DESC(desc_p) \
do { \
	HW_DESC_INIT(desc_p); \
	HW_DESC_SET_DIN_MONITOR_CNTR(desc_p); \
} while (0)

/**
 * Try adding monitor descriptor BEFORE enqueuing sequence.
 */
#define CC_CYCLE_DESC_HEAD(cc_base_addr, desc_p, lock_p, is_monitored_p) \
do { \
	if (!test_and_set_bit(MONITOR_CNTR_BIT, (lock_p))) { \
		enqueue_seq((cc_base_addr), (desc_p), 1); \
		*(is_monitored_p) = true; \
	} else { \
		*(is_monitored_p) = false; \
	} \
} while (0)

/**
 * If CC_CYCLE_DESC_HEAD was successfully added:
 * 1. Add memory barrier descriptor to ensure last AXI transaction.
 * 2. Add monitor descriptor to sequence tail AFTER enqueuing sequence.
 */
#define CC_CYCLE_DESC_TAIL(cc_base_addr, desc_p, is_monitored) \
do { \
	if ((is_monitored) == true) { \
		HwDesc_s barrier_desc; \
		HW_DESC_INIT(&barrier_desc); \
		HW_DESC_SET_DIN_NO_DMA(&barrier_desc, 0, 0xfffff0); \
		HW_DESC_SET_DOUT_NO_DMA(&barrier_desc, 0, 0, 1); \
		enqueue_seq((cc_base_addr), &barrier_desc, 1); \
		enqueue_seq((cc_base_addr), (desc_p), 1); \
	} \
} while (0)

/**
 * Try reading CC monitor counter value upon sequence complete.
 * Can only succeed if the lock_p is taken by the owner of the given request.
 */
#define END_CC_MONITOR_COUNT(cc_base_addr, stat_op_type, stat_phase, monitor_null_cycles, lock_p, is_monitored) \
do { \
	uint32_t elapsed_cycles; \
	if ((is_monitored) == true) { \
		elapsed_cycles = READ_REGISTER((cc_base_addr) + CC_REG_OFFSET(CRY_KERNEL, DSCRPTR_MEASURE_CNTR)); \
		clear_bit(MONITOR_CNTR_BIT, (lock_p)); \
		if (elapsed_cycles > 0) \
			update_cc_stat(stat_op_type, stat_phase, (elapsed_cycles - monitor_null_cycles)); \
	} \
} while (0)

#else /*CC_CYCLE_COUNT*/

#define INIT_CC_MONITOR_DESC(desc_p) do { } while (0)
#define CC_CYCLE_DESC_HEAD(cc_base_addr, desc_p, lock_p, is_monitored_p) do { } while (0)
#define CC_CYCLE_DESC_TAIL(cc_base_addr, desc_p, is_monitored) do { } while (0)
#define END_CC_MONITOR_COUNT(cc_base_addr, stat_op_type, stat_phase, monitor_null_cycles, lock_p, is_monitored) do { } while (0)
#endif /*CC_CYCLE_COUNT*/


struct ssi_request_mgr_handle {
	/* Request manager resources */
	unsigned int hw_queue_size; /* HW capability */
	unsigned int min_free_hw_slots;
	unsigned int max_used_sw_slots;
	struct ssi_crypto_req req_queue[MAX_REQUEST_QUEUE_SIZE];
	uint32_t req_queue_head;
	uint32_t req_queue_tail;
	uint32_t axi_completed;
	uint32_t q_free_slots;
	spinlock_t hw_lock;
	HwDesc_s compl_desc;
	uint8_t *dummy_comp_buff;
	dma_addr_t dummy_comp_buff_dma;
	HwDesc_s monitor_desc;
	volatile unsigned long monitor_lock;
#ifdef COMP_IN_WQ
	struct workqueue_struct *workq;
	struct delayed_work compwork;
#else
	struct tasklet_struct comptask;
#endif
#if defined (CONFIG_PM_RUNTIME) || defined (CONFIG_PM_SLEEP)
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

	if (req_mgr_h == NULL)
		return; /* Not allocated */

	if (req_mgr_h->dummy_comp_buff_dma != 0) {
		SSI_RESTORE_DMA_ADDR_TO_48BIT(req_mgr_h->dummy_comp_buff_dma);
		dma_free_coherent(&drvdata->plat_dev->dev,
				  sizeof(uint32_t), req_mgr_h->dummy_comp_buff,
				  req_mgr_h->dummy_comp_buff_dma);
	}

	SSI_LOG_DEBUG("max_used_hw_slots=%d\n", (req_mgr_h->hw_queue_size -
						req_mgr_h->min_free_hw_slots) );
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
#ifdef CC_CYCLE_COUNT
	HwDesc_s monitor_desc[2];
	struct ssi_crypto_req monitor_req = {0};
#endif
	struct ssi_request_mgr_handle *req_mgr_h;
	int rc = 0;

	req_mgr_h = kzalloc(sizeof(struct ssi_request_mgr_handle),GFP_KERNEL);
	if (req_mgr_h == NULL) {
		rc = -ENOMEM;
		goto req_mgr_init_err;
	}

	drvdata->request_mgr_handle = req_mgr_h;

	spin_lock_init(&req_mgr_h->hw_lock);
#ifdef COMP_IN_WQ
	SSI_LOG_DEBUG("Initializing completion workqueue\n");
	req_mgr_h->workq = create_singlethread_workqueue("arm_cc7x_wq");
	if (unlikely(req_mgr_h->workq == NULL)) {
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
		sizeof(uint32_t), &req_mgr_h->dummy_comp_buff_dma, GFP_KERNEL);
	if (!req_mgr_h->dummy_comp_buff) {
		SSI_LOG_ERR("Not enough memory to allocate DMA (%zu) dropped "
			   "buffer\n", sizeof(uint32_t));
		rc = -ENOMEM;
		goto req_mgr_init_err;
	}
	SSI_UPDATE_DMA_ADDR_TO_48BIT(req_mgr_h->dummy_comp_buff_dma,
							     sizeof(uint32_t));

	/* Init. "dummy" completion descriptor */
	HW_DESC_INIT(&req_mgr_h->compl_desc);
	HW_DESC_SET_DIN_CONST(&req_mgr_h->compl_desc, 0, sizeof(uint32_t));
	HW_DESC_SET_DOUT_DLLI(&req_mgr_h->compl_desc,
		req_mgr_h->dummy_comp_buff_dma,
		sizeof(uint32_t), NS_BIT, 1);
	HW_DESC_SET_FLOW_MODE(&req_mgr_h->compl_desc, BYPASS);
	HW_DESC_SET_QUEUE_LAST_IND(&req_mgr_h->compl_desc);

#ifdef CC_CYCLE_COUNT
	/* For CC-HW cycle performance trace */
	INIT_CC_MONITOR_DESC(&req_mgr_h->monitor_desc);
	set_bit(MONITOR_CNTR_BIT, &req_mgr_h->monitor_lock);
	monitor_desc[0] = req_mgr_h->monitor_desc;
	monitor_desc[1] = req_mgr_h->monitor_desc;

	rc = send_request(drvdata, &monitor_req, monitor_desc, 2, 0);
	if (unlikely(rc != 0))
		goto req_mgr_init_err;

	drvdata->monitor_null_cycles = READ_REGISTER(drvdata->cc_base +
		CC_REG_OFFSET(CRY_KERNEL, DSCRPTR_MEASURE_CNTR));
	SSI_LOG_ERR("Calibration time=0x%08x\n", drvdata->monitor_null_cycles);

	clear_bit(MONITOR_CNTR_BIT, &req_mgr_h->monitor_lock);
#endif

	return 0;

req_mgr_init_err:
	request_mgr_fini(drvdata);
	return rc;
}

static inline void enqueue_seq(
	void __iomem *cc_base,
	HwDesc_s seq[], unsigned int seq_len)
{
	int i;

	for (i = 0; i < seq_len; i++) {
		writel_relaxed(seq[i].word[0], (volatile void __iomem *)(cc_base+CC_REG_OFFSET(CRY_KERNEL, DSCRPTR_QUEUE_WORD0)));
		writel_relaxed(seq[i].word[1], (volatile void __iomem *)(cc_base+CC_REG_OFFSET(CRY_KERNEL, DSCRPTR_QUEUE_WORD0)));
		writel_relaxed(seq[i].word[2], (volatile void __iomem *)(cc_base+CC_REG_OFFSET(CRY_KERNEL, DSCRPTR_QUEUE_WORD0)));
		writel_relaxed(seq[i].word[3], (volatile void __iomem *)(cc_base+CC_REG_OFFSET(CRY_KERNEL, DSCRPTR_QUEUE_WORD0)));
		writel_relaxed(seq[i].word[4], (volatile void __iomem *)(cc_base+CC_REG_OFFSET(CRY_KERNEL, DSCRPTR_QUEUE_WORD0)));
		wmb();
		writel_relaxed(seq[i].word[5], (volatile void __iomem *)(cc_base+CC_REG_OFFSET(CRY_KERNEL, DSCRPTR_QUEUE_WORD0)));
#ifdef DX_DUMP_DESCS
		SSI_LOG_DEBUG("desc[%02d]: 0x%08X 0x%08X 0x%08X 0x%08X 0x%08X 0x%08X\n", i,
			seq[i].word[0], seq[i].word[1], seq[i].word[2], seq[i].word[3], seq[i].word[4], seq[i].word[5]);
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
	   be chaned during the poll becasue the spinlock_bh
	   is held by the thread */
	if (unlikely(((req_mgr_h->req_queue_head + 1) &
		      (MAX_REQUEST_QUEUE_SIZE - 1)) ==
		     req_mgr_h->req_queue_tail)) {
		SSI_LOG_ERR("SW FIFO is full. req_queue_head=%d sw_fifo_len=%d\n",
			   req_mgr_h->req_queue_head, MAX_REQUEST_QUEUE_SIZE);
		return -EBUSY;
	}

	if ((likely(req_mgr_h->q_free_slots >= total_seq_len)) ) {
		return 0;
	}
	/* Wait for space in HW queue. Poll constant num of iterations. */
	for (poll_queue =0; poll_queue < SSI_MAX_POLL_ITER ; poll_queue ++) {
		req_mgr_h->q_free_slots =
			CC_HAL_READ_REGISTER(
				CC_REG_OFFSET(CRY_KERNEL,
						 DSCRPTR_QUEUE_CONTENT));
		if (unlikely(req_mgr_h->q_free_slots <
						req_mgr_h->min_free_hw_slots)) {
			req_mgr_h->min_free_hw_slots = req_mgr_h->q_free_slots;
		}

		if (likely (req_mgr_h->q_free_slots >= total_seq_len)) {
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
 *      	  If "false": this function adds a dummy descriptor completion
 *      	  and waits upon completion signal.
 *
 * \return int Returns -EINPROGRESS if "is_dout=true"; "0" if "is_dout=false"
 */
int send_request(
	struct ssi_drvdata *drvdata, struct ssi_crypto_req *ssi_req,
	HwDesc_s *desc, unsigned int len, bool is_dout)
{
	void __iomem *cc_base = drvdata->cc_base;
	struct ssi_request_mgr_handle *req_mgr_h = drvdata->request_mgr_handle;
	unsigned int used_sw_slots;
	unsigned int iv_seq_len = 0;
	unsigned int total_seq_len = len; /*initial sequence length*/
	HwDesc_s iv_seq[SSI_IVPOOL_SEQ_LEN];
	int rc;
	unsigned int max_required_seq_len = (total_seq_len +
					((ssi_req->ivgen_dma_addr_len == 0) ? 0 :
					SSI_IVPOOL_SEQ_LEN ) +
					((is_dout == 0 )? 1 : 0));
	DECL_CYCLE_COUNT_RESOURCES;

#if defined (CONFIG_PM_RUNTIME) || defined (CONFIG_PM_SLEEP)
	rc = ssi_power_mgr_runtime_get(&drvdata->plat_dev->dev);
	if (rc != 0) {
		SSI_LOG_ERR("ssi_power_mgr_runtime_get returned %x\n",rc);
		spin_unlock_bh(&req_mgr_h->hw_lock);
		return rc;
	}
#endif

	do {
		spin_lock_bh(&req_mgr_h->hw_lock);

		/* Check if there is enough place in the SW/HW queues
		in case iv gen add the max size and in case of no dout add 1
		for the internal completion descriptor */
		rc = request_mgr_queues_status_check(req_mgr_h,
					       cc_base,
					       max_required_seq_len);
		if (likely(rc == 0 ))
			/* There is enough place in the queue */
			break;
		/* something wrong release the spinlock*/
		spin_unlock_bh(&req_mgr_h->hw_lock);

		if (rc != -EAGAIN) {
			/* Any error other than HW queue full
			   (SW queue is full) */
#if defined (CONFIG_PM_RUNTIME) || defined (CONFIG_PM_SLEEP)
			ssi_power_mgr_runtime_put_suspend(&drvdata->plat_dev->dev);
#endif
			return rc;
		}

		/* HW queue is full - short sleep */
		msleep(1);
	} while (1);

	/* Additional completion descriptor is needed incase caller did not
	   enabled any DLLI/MLLI DOUT bit in the given sequence */
	if (!is_dout) {
		init_completion(&ssi_req->seq_compl);
		ssi_req->user_cb = request_mgr_complete;
		ssi_req->user_arg = &(ssi_req->seq_compl);
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
		rc = ssi_ivgen_getiv(drvdata, ssi_req->ivgen_dma_addr, ssi_req->ivgen_dma_addr_len,
			ssi_req->ivgen_size, iv_seq, &iv_seq_len);

		if (unlikely(rc != 0)) {
			SSI_LOG_ERR("Failed to generate IV (rc=%d)\n", rc);
			spin_unlock_bh(&req_mgr_h->hw_lock);
#if defined (CONFIG_PM_RUNTIME) || defined (CONFIG_PM_SLEEP)
			ssi_power_mgr_runtime_put_suspend(&drvdata->plat_dev->dev);
#endif
			return rc;
		}

		total_seq_len += iv_seq_len;
	}

	used_sw_slots = ((req_mgr_h->req_queue_head - req_mgr_h->req_queue_tail) & (MAX_REQUEST_QUEUE_SIZE-1));
	if (unlikely(used_sw_slots > req_mgr_h->max_used_sw_slots)) {
		req_mgr_h->max_used_sw_slots = used_sw_slots;
	}

	CC_CYCLE_DESC_HEAD(cc_base, &req_mgr_h->monitor_desc,
			&req_mgr_h->monitor_lock, &ssi_req->is_monitored_p);

	/* Enqueue request - must be locked with HW lock*/
	req_mgr_h->req_queue[req_mgr_h->req_queue_head] = *ssi_req;
	START_CYCLE_COUNT_AT(req_mgr_h->req_queue[req_mgr_h->req_queue_head].submit_cycle);
	req_mgr_h->req_queue_head = (req_mgr_h->req_queue_head + 1) & (MAX_REQUEST_QUEUE_SIZE - 1);
	/* TODO: Use circ_buf.h ? */

	SSI_LOG_DEBUG("Enqueue request head=%u\n", req_mgr_h->req_queue_head);

#ifdef FLUSH_CACHE_ALL
	flush_cache_all();
#endif

	/* STAT_PHASE_4: Push sequence */
	START_CYCLE_COUNT();
	enqueue_seq(cc_base, iv_seq, iv_seq_len);
	enqueue_seq(cc_base, desc, len);
	enqueue_seq(cc_base, &req_mgr_h->compl_desc, (is_dout ? 0 : 1));
	END_CYCLE_COUNT(ssi_req->op_type, STAT_PHASE_4);

	CC_CYCLE_DESC_TAIL(cc_base, &req_mgr_h->monitor_desc, ssi_req->is_monitored_p);

	if (unlikely(req_mgr_h->q_free_slots < total_seq_len)) {
		/*This means that there was a problem with the resume*/
		BUG();
	}
	/* Update the free slots in HW queue */
	req_mgr_h->q_free_slots -= total_seq_len;

	spin_unlock_bh(&req_mgr_h->hw_lock);

	if (!is_dout) {
		/* Wait upon sequence completion.
		*  Return "0" -Operation done successfully. */
		return wait_for_completion_interruptible(&ssi_req->seq_compl);
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
	struct ssi_drvdata *drvdata, HwDesc_s *desc, unsigned int len)
{
	void __iomem *cc_base = drvdata->cc_base;
	struct ssi_request_mgr_handle *req_mgr_h = drvdata->request_mgr_handle;
	unsigned int total_seq_len = len; /*initial sequence length*/
	int rc = 0;

	/* Wait for space in HW and SW FIFO. Poll for as much as FIFO_TIMEOUT. */
	rc = request_mgr_queues_status_check(req_mgr_h, cc_base, total_seq_len);
	if (unlikely(rc != 0 )) {
		return rc;
	}
	HW_DESC_SET_QUEUE_LAST_IND(&desc[len-1]);

	enqueue_seq(cc_base, desc, len);

	/* Update the free slots in HW queue */
	req_mgr_h->q_free_slots = CC_HAL_READ_REGISTER(
					CC_REG_OFFSET(CRY_KERNEL,
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
	struct ssi_request_mgr_handle * request_mgr_handle =
						drvdata->request_mgr_handle;
#if defined (CONFIG_PM_RUNTIME) || defined (CONFIG_PM_SLEEP)
	int rc = 0;
#endif
	DECL_CYCLE_COUNT_RESOURCES;

	while(request_mgr_handle->axi_completed) {
		request_mgr_handle->axi_completed--;

		/* Dequeue request */
		if (unlikely(request_mgr_handle->req_queue_head == request_mgr_handle->req_queue_tail)) {
			SSI_LOG_ERR("Request queue is empty req_queue_head==req_queue_tail==%u\n", request_mgr_handle->req_queue_head);
			BUG();
		}

		ssi_req = &request_mgr_handle->req_queue[request_mgr_handle->req_queue_tail];
		END_CYCLE_COUNT_AT(ssi_req->submit_cycle, ssi_req->op_type, STAT_PHASE_5); /* Seq. Comp. */
		END_CC_MONITOR_COUNT(drvdata->cc_base, ssi_req->op_type, STAT_PHASE_6,
			drvdata->monitor_null_cycles, &request_mgr_handle->monitor_lock, ssi_req->is_monitored_p);

#ifdef FLUSH_CACHE_ALL
		flush_cache_all();
#endif

#ifdef COMPLETION_DELAY
		/* Delay */
		{
			uint32_t axi_err;
			int i;
			SSI_LOG_INFO("Delay\n");
			for (i=0;i<1000000;i++) {
				axi_err = READ_REGISTER(drvdata->cc_base + CC_REG_OFFSET(CRY_KERNEL, AXIM_MON_ERR));
			}
		}
#endif /* COMPLETION_DELAY */

		if (likely(ssi_req->user_cb != NULL)) {
			START_CYCLE_COUNT();
			ssi_req->user_cb(&plat_dev->dev, ssi_req->user_arg, drvdata->cc_base);
			END_CYCLE_COUNT(STAT_OP_TYPE_GENERIC, STAT_PHASE_3);
		}
		request_mgr_handle->req_queue_tail = (request_mgr_handle->req_queue_tail + 1) & (MAX_REQUEST_QUEUE_SIZE - 1);
		SSI_LOG_DEBUG("Dequeue request tail=%u\n", request_mgr_handle->req_queue_tail);
		SSI_LOG_DEBUG("Request completed. axi_completed=%d\n", request_mgr_handle->axi_completed);
#if defined (CONFIG_PM_RUNTIME) || defined (CONFIG_PM_SLEEP)
		rc = ssi_power_mgr_runtime_put_suspend(&plat_dev->dev);
		if (rc != 0) {
			SSI_LOG_ERR("Failed to set runtime suspension %d\n",rc);
		}
#endif
	}
}

/* Deferred service handler, run as interrupt-fired tasklet */
static void comp_handler(unsigned long devarg)
{
	struct ssi_drvdata *drvdata = (struct ssi_drvdata *)devarg;
	void __iomem *cc_base = drvdata->cc_base;
	struct ssi_request_mgr_handle * request_mgr_handle =
						drvdata->request_mgr_handle;

	uint32_t irq;

	DECL_CYCLE_COUNT_RESOURCES;

	START_CYCLE_COUNT();

	irq = (drvdata->irq & SSI_COMP_IRQ_MASK);

	if (irq & SSI_COMP_IRQ_MASK) {
		/* To avoid the interrupt from firing as we unmask it, we clear it now */
		CC_HAL_WRITE_REGISTER(CC_REG_OFFSET(HOST_RGF, HOST_ICR), SSI_COMP_IRQ_MASK);

		/* Avoid race with above clear: Test completion counter once more */
		request_mgr_handle->axi_completed += CC_REG_FLD_GET(CRY_KERNEL, AXIM_MON_COMP, VALUE,
			CC_HAL_READ_REGISTER(AXIM_MON_BASE_OFFSET));

		/* ISR-to-Tasklet latency */
		if (request_mgr_handle->axi_completed) {
			/* Only if actually reflects ISR-to-completion-handling latency, i.e.,
			   not duplicate as a result of interrupt after AXIM_MON_ERR clear, before end of loop */
			END_CYCLE_COUNT_AT(drvdata->isr_exit_cycles, STAT_OP_TYPE_GENERIC, STAT_PHASE_1);
		}

		while (request_mgr_handle->axi_completed) {
			do {
				proc_completions(drvdata);
				/* At this point (after proc_completions()), request_mgr_handle->axi_completed is always 0.
				   The following assignment was changed to = (previously was +=) to conform KW restrictions. */
				request_mgr_handle->axi_completed = CC_REG_FLD_GET(CRY_KERNEL, AXIM_MON_COMP, VALUE,
					CC_HAL_READ_REGISTER(AXIM_MON_BASE_OFFSET));
			} while (request_mgr_handle->axi_completed > 0);

			/* To avoid the interrupt from firing as we unmask it, we clear it now */
			CC_HAL_WRITE_REGISTER(CC_REG_OFFSET(HOST_RGF, HOST_ICR), SSI_COMP_IRQ_MASK);

			/* Avoid race with above clear: Test completion counter once more */
			request_mgr_handle->axi_completed += CC_REG_FLD_GET(CRY_KERNEL, AXIM_MON_COMP, VALUE,
				CC_HAL_READ_REGISTER(AXIM_MON_BASE_OFFSET));
		}

	}
	/* after verifing that there is nothing to do, Unmask AXI completion interrupt */
	CC_HAL_WRITE_REGISTER(CC_REG_OFFSET(HOST_RGF, HOST_IMR),
		CC_HAL_READ_REGISTER(
		CC_REG_OFFSET(HOST_RGF, HOST_IMR)) & ~irq);
	END_CYCLE_COUNT(STAT_OP_TYPE_GENERIC, STAT_PHASE_2);
}

/*
resume the queue configuration - no need to take the lock as this happens inside
the spin lock protection
*/
#if defined (CONFIG_PM_RUNTIME) || defined (CONFIG_PM_SLEEP)
int ssi_request_mgr_runtime_resume_queue(struct ssi_drvdata *drvdata)
{
	struct ssi_request_mgr_handle * request_mgr_handle = drvdata->request_mgr_handle;

	spin_lock_bh(&request_mgr_handle->hw_lock);
	request_mgr_handle->is_runtime_suspended = false;
	spin_unlock_bh(&request_mgr_handle->hw_lock);

	return 0 ;
}

/*
suspend the queue configuration. Since it is used for the runtime suspend
only verify that the queue can be suspended.
*/
int ssi_request_mgr_runtime_suspend_queue(struct ssi_drvdata *drvdata)
{
	struct ssi_request_mgr_handle * request_mgr_handle =
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
	struct ssi_request_mgr_handle * request_mgr_handle =
						drvdata->request_mgr_handle;

	return 	request_mgr_handle->is_runtime_suspended;
}

#endif


// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2025 Broadcom.
#include <linux/pci.h>

#include "roce_hsi.h"
#include "bng_res.h"
#include "bng_fw.h"

void bng_re_free_rcfw_channel(struct bng_re_rcfw *rcfw)
{
	kfree(rcfw->crsqe_tbl);
	bng_re_free_hwq(rcfw->res, &rcfw->cmdq.hwq);
	bng_re_free_hwq(rcfw->res, &rcfw->creq.hwq);
	rcfw->pdev = NULL;
}

int bng_re_alloc_fw_channel(struct bng_re_res *res,
			    struct bng_re_rcfw *rcfw)
{
	struct bng_re_hwq_attr hwq_attr = {};
	struct bng_re_sg_info sginfo = {};
	struct bng_re_cmdq_ctx *cmdq;
	struct bng_re_creq_ctx *creq;

	rcfw->pdev = res->pdev;
	cmdq = &rcfw->cmdq;
	creq = &rcfw->creq;
	rcfw->res = res;

	sginfo.pgsize = PAGE_SIZE;
	sginfo.pgshft = PAGE_SHIFT;

	hwq_attr.sginfo = &sginfo;
	hwq_attr.res = rcfw->res;
	hwq_attr.depth = BNG_FW_CREQE_MAX_CNT;
	hwq_attr.stride = BNG_FW_CREQE_UNITS;
	hwq_attr.type = BNG_HWQ_TYPE_QUEUE;

	if (bng_re_alloc_init_hwq(&creq->hwq, &hwq_attr)) {
		dev_err(&rcfw->pdev->dev,
			"HW channel CREQ allocation failed\n");
		goto fail;
	}

	rcfw->cmdq_depth = BNG_FW_CMDQE_MAX_CNT;

	sginfo.pgsize = bng_fw_cmdqe_page_size(rcfw->cmdq_depth);
	hwq_attr.depth = rcfw->cmdq_depth & 0x7FFFFFFF;
	hwq_attr.stride = BNG_FW_CMDQE_UNITS;
	hwq_attr.type = BNG_HWQ_TYPE_CTX;
	if (bng_re_alloc_init_hwq(&cmdq->hwq, &hwq_attr)) {
		dev_err(&rcfw->pdev->dev,
			"HW channel CMDQ allocation failed\n");
		goto fail;
	}

	rcfw->crsqe_tbl = kcalloc(cmdq->hwq.max_elements,
				  sizeof(*rcfw->crsqe_tbl), GFP_KERNEL);
	if (!rcfw->crsqe_tbl)
		goto fail;

	spin_lock_init(&rcfw->tbl_lock);

	rcfw->max_timeout = res->cctx->hwrm_cmd_max_timeout;
	return 0;

fail:
	bng_re_free_rcfw_channel(rcfw);
	return -ENOMEM;
}

static int bng_re_process_qp_event(struct bng_re_rcfw *rcfw,
				   struct creq_qp_event *qp_event,
				   u32 *num_wait)
{
	struct bng_re_hwq *hwq = &rcfw->cmdq.hwq;
	struct bng_re_crsqe *crsqe;
	u32 req_size;
	u16 cookie;
	bool is_waiter_alive;
	struct pci_dev *pdev;
	u32 wait_cmds = 0;
	int rc = 0;

	pdev = rcfw->pdev;
	switch (qp_event->event) {
	case CREQ_QP_EVENT_EVENT_QP_ERROR_NOTIFICATION:
		dev_err(&pdev->dev, "Received QP error notification\n");
		break;
	default:
		/*
		 * Command Response
		 * cmdq->lock needs to be acquired to synchronie
		 * the command send and completion reaping. This function
		 * is always called with creq->lock held. Using
		 * the nested variant of spin_lock.
		 *
		 */

		spin_lock_nested(&hwq->lock, SINGLE_DEPTH_NESTING);
		cookie = le16_to_cpu(qp_event->cookie);
		cookie &= BNG_FW_MAX_COOKIE_VALUE;
		crsqe = &rcfw->crsqe_tbl[cookie];

		if (WARN_ONCE(test_bit(FIRMWARE_STALL_DETECTED,
				       &rcfw->cmdq.flags),
		    "Unreponsive rcfw channel detected.!!")) {
			dev_info(&pdev->dev,
				 "rcfw timedout: cookie = %#x, free_slots = %d",
				 cookie, crsqe->free_slots);
			spin_unlock(&hwq->lock);
			return rc;
		}

		if (crsqe->is_waiter_alive) {
			if (crsqe->resp) {
				memcpy(crsqe->resp, qp_event, sizeof(*qp_event));
				/* Insert write memory barrier to ensure that
				 * response data is copied before clearing the
				 * flags
				 */
				smp_wmb();
			}
		}

		wait_cmds++;

		req_size = crsqe->req_size;
		is_waiter_alive = crsqe->is_waiter_alive;

		crsqe->req_size = 0;
		if (!is_waiter_alive)
			crsqe->resp = NULL;

		crsqe->is_in_used = false;

		hwq->cons += req_size;

		spin_unlock(&hwq->lock);
	}
	*num_wait += wait_cmds;
	return rc;
}

/* function events */
static int bng_re_process_func_event(struct bng_re_rcfw *rcfw,
				     struct creq_func_event *func_event)
{
	switch (func_event->event) {
	case CREQ_FUNC_EVENT_EVENT_TX_WQE_ERROR:
	case CREQ_FUNC_EVENT_EVENT_TX_DATA_ERROR:
	case CREQ_FUNC_EVENT_EVENT_RX_WQE_ERROR:
	case CREQ_FUNC_EVENT_EVENT_RX_DATA_ERROR:
	case CREQ_FUNC_EVENT_EVENT_CQ_ERROR:
	case CREQ_FUNC_EVENT_EVENT_TQM_ERROR:
	case CREQ_FUNC_EVENT_EVENT_CFCQ_ERROR:
	case CREQ_FUNC_EVENT_EVENT_CFCS_ERROR:
	case CREQ_FUNC_EVENT_EVENT_CFCC_ERROR:
	case CREQ_FUNC_EVENT_EVENT_CFCM_ERROR:
	case CREQ_FUNC_EVENT_EVENT_TIM_ERROR:
	case CREQ_FUNC_EVENT_EVENT_VF_COMM_REQUEST:
	case CREQ_FUNC_EVENT_EVENT_RESOURCE_EXHAUSTED:
		break;
	default:
		return -EINVAL;
	}

	return 0;
}



/* CREQ Completion handlers */
static void bng_re_service_creq(struct tasklet_struct *t)
{
	struct bng_re_rcfw *rcfw = from_tasklet(rcfw, t, creq.creq_tasklet);
	struct bng_re_creq_ctx *creq = &rcfw->creq;
	u32 type, budget = BNG_FW_CREQ_ENTRY_POLL_BUDGET;
	struct bng_re_hwq *hwq = &creq->hwq;
	struct creq_base *creqe;
	u32 num_wakeup = 0;
	u32 hw_polled = 0;

	/* Service the CREQ until budget is over */
	spin_lock_bh(&hwq->lock);
	while (budget > 0) {
		creqe = bng_re_get_qe(hwq, hwq->cons, NULL);
		if (!BNG_FW_CREQ_CMP_VALID(creqe, creq->creq_db.dbinfo.flags))
			break;
		/* The valid test of the entry must be done first before
		 * reading any further.
		 */
		dma_rmb();

		type = creqe->type & CREQ_BASE_TYPE_MASK;
		switch (type) {
		case CREQ_BASE_TYPE_QP_EVENT:
			bng_re_process_qp_event
				(rcfw, (struct creq_qp_event *)creqe,
				 &num_wakeup);
			creq->stats.creq_qp_event_processed++;
			break;
		case CREQ_BASE_TYPE_FUNC_EVENT:
			if (!bng_re_process_func_event
			    (rcfw, (struct creq_func_event *)creqe))
				creq->stats.creq_func_event_processed++;
			else
				dev_warn(&rcfw->pdev->dev,
					 "aeqe:%#x Not handled\n", type);
			break;
		default:
			if (type != ASYNC_EVENT_CMPL_TYPE_HWRM_ASYNC_EVENT)
				dev_warn(&rcfw->pdev->dev,
					 "creqe with event 0x%x not handled\n",
					 type);
			break;
		}
		budget--;
		hw_polled++;
		bng_re_hwq_incr_cons(hwq->max_elements, &hwq->cons,
				     1, &creq->creq_db.dbinfo.flags);
	}

	if (hw_polled)
		bng_re_ring_nq_db(&creq->creq_db.dbinfo,
				  rcfw->res->cctx, true);
	spin_unlock_bh(&hwq->lock);
	if (num_wakeup)
		wake_up_nr(&rcfw->cmdq.waitq, num_wakeup);
}

static int bng_re_map_cmdq_mbox(struct bng_re_rcfw *rcfw)
{
	struct bng_re_cmdq_mbox *mbox;
	resource_size_t bar_reg;
	struct pci_dev *pdev;

	pdev = rcfw->pdev;
	mbox = &rcfw->cmdq.cmdq_mbox;

	mbox->reg.bar_id = BNG_FW_COMM_PCI_BAR_REGION;
	mbox->reg.len = BNG_FW_COMM_SIZE;
	mbox->reg.bar_base = pci_resource_start(pdev, mbox->reg.bar_id);
	if (!mbox->reg.bar_base) {
		dev_err(&pdev->dev,
			"CMDQ BAR region %d resc start is 0!\n",
			mbox->reg.bar_id);
		return -ENOMEM;
	}

	bar_reg = mbox->reg.bar_base + BNG_FW_COMM_BASE_OFFSET;
	mbox->reg.len = BNG_FW_COMM_SIZE;
	mbox->reg.bar_reg = ioremap(bar_reg, mbox->reg.len);
	if (!mbox->reg.bar_reg) {
		dev_err(&pdev->dev,
			"CMDQ BAR region %d mapping failed\n",
			mbox->reg.bar_id);
		return -ENOMEM;
	}

	mbox->prod = (void  __iomem *)(mbox->reg.bar_reg +
			BNG_FW_PF_VF_COMM_PROD_OFFSET);
	mbox->db = (void __iomem *)(mbox->reg.bar_reg + BNG_FW_COMM_TRIG_OFFSET);
	return 0;
}

static irqreturn_t bng_re_creq_irq(int irq, void *dev_instance)
{
	struct bng_re_rcfw *rcfw = dev_instance;
	struct bng_re_creq_ctx *creq;
	struct bng_re_hwq *hwq;
	u32 sw_cons;

	creq = &rcfw->creq;
	hwq = &creq->hwq;
	/* Prefetch the CREQ element */
	sw_cons = HWQ_CMP(hwq->cons, hwq);
	prefetch(bng_re_get_qe(hwq, sw_cons, NULL));

	tasklet_schedule(&creq->creq_tasklet);

	return IRQ_HANDLED;
}

int bng_re_rcfw_start_irq(struct bng_re_rcfw *rcfw, int msix_vector,
			  bool need_init)
{
	struct bng_re_creq_ctx *creq;
	struct bng_re_res *res;
	int rc;

	creq = &rcfw->creq;
	res = rcfw->res;

	if (creq->irq_handler_avail)
		return -EFAULT;

	creq->msix_vec = msix_vector;
	if (need_init)
		tasklet_setup(&creq->creq_tasklet, bng_re_service_creq);
	else
		tasklet_enable(&creq->creq_tasklet);

	creq->irq_name = kasprintf(GFP_KERNEL, "bng_re-creq@pci:%s",
				   pci_name(res->pdev));
	if (!creq->irq_name)
		return -ENOMEM;
	rc = request_irq(creq->msix_vec, bng_re_creq_irq, 0,
			 creq->irq_name, rcfw);
	if (rc) {
		kfree(creq->irq_name);
		creq->irq_name = NULL;
		tasklet_disable(&creq->creq_tasklet);
		return rc;
	}
	creq->irq_handler_avail = true;

	bng_re_ring_nq_db(&creq->creq_db.dbinfo, res->cctx, true);
	atomic_inc(&rcfw->rcfw_intr_enabled);

	return 0;
}

static int bng_re_map_creq_db(struct bng_re_rcfw *rcfw, u32 reg_offt)
{
	struct bng_re_creq_db *creq_db;
	resource_size_t bar_reg;
	struct pci_dev *pdev;

	pdev = rcfw->pdev;
	creq_db = &rcfw->creq.creq_db;

	creq_db->dbinfo.flags = 0;
	creq_db->reg.bar_id = BNG_FW_COMM_CONS_PCI_BAR_REGION;
	creq_db->reg.bar_base = pci_resource_start(pdev, creq_db->reg.bar_id);
	if (!creq_db->reg.bar_id)
		dev_err(&pdev->dev,
			"CREQ BAR region %d resc start is 0!",
			creq_db->reg.bar_id);

	bar_reg = creq_db->reg.bar_base + reg_offt;

	creq_db->reg.len = BNG_FW_CREQ_DB_LEN;
	creq_db->reg.bar_reg = ioremap(bar_reg, creq_db->reg.len);
	if (!creq_db->reg.bar_reg) {
		dev_err(&pdev->dev,
			"CREQ BAR region %d mapping failed",
			creq_db->reg.bar_id);
		return -ENOMEM;
	}
	creq_db->dbinfo.db = creq_db->reg.bar_reg;
	creq_db->dbinfo.hwq = &rcfw->creq.hwq;
	creq_db->dbinfo.xid = rcfw->creq.ring_id;
	return 0;
}

void bng_re_rcfw_stop_irq(struct bng_re_rcfw *rcfw, bool kill)
{
	struct bng_re_creq_ctx *creq;

	creq = &rcfw->creq;

	if (!creq->irq_handler_avail)
		return;

	creq->irq_handler_avail = false;
	/* Mask h/w interrupts */
	bng_re_ring_nq_db(&creq->creq_db.dbinfo, rcfw->res->cctx, false);
	/* Sync with last running IRQ-handler */
	synchronize_irq(creq->msix_vec);
	free_irq(creq->msix_vec, rcfw);
	kfree(creq->irq_name);
	creq->irq_name = NULL;
	atomic_set(&rcfw->rcfw_intr_enabled, 0);
	if (kill)
		tasklet_kill(&creq->creq_tasklet);
	tasklet_disable(&creq->creq_tasklet);
}

void bng_re_disable_rcfw_channel(struct bng_re_rcfw *rcfw)
{
	struct bng_re_creq_ctx *creq;
	struct bng_re_cmdq_ctx *cmdq;

	creq = &rcfw->creq;
	cmdq = &rcfw->cmdq;
	/* Make sure the HW channel is stopped! */
	bng_re_rcfw_stop_irq(rcfw, true);

	iounmap(cmdq->cmdq_mbox.reg.bar_reg);
	iounmap(creq->creq_db.reg.bar_reg);

	cmdq->cmdq_mbox.reg.bar_reg = NULL;
	creq->creq_db.reg.bar_reg = NULL;
	creq->msix_vec = 0;
}

int bng_re_enable_fw_channel(struct bng_re_rcfw *rcfw,
			     int msix_vector,
			     int cp_bar_reg_off)
{
	struct bng_re_cmdq_ctx *cmdq;
	int rc;

	cmdq = &rcfw->cmdq;

	/* Assign defaults */
	cmdq->seq_num = 0;
	set_bit(FIRMWARE_FIRST_FLAG, &cmdq->flags);
	init_waitqueue_head(&cmdq->waitq);

	rc = bng_re_map_cmdq_mbox(rcfw);
	if (rc)
		return rc;

	rc = bng_re_map_creq_db(rcfw, cp_bar_reg_off);
	if (rc)
		return rc;

	rc = bng_re_rcfw_start_irq(rcfw, msix_vector, true);
	if (rc) {
		dev_err(&rcfw->pdev->dev,
			"Failed to request IRQ for CREQ rc = 0x%x\n", rc);
		bng_re_disable_rcfw_channel(rcfw);
		return rc;
	}

	return 0;
}

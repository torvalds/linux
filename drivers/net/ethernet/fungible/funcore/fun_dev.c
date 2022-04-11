// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)

#include <linux/aer.h>
#include <linux/bitmap.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/io-64-nonatomic-lo-hi.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/nvme.h>
#include <linux/pci.h>
#include <linux/wait.h>
#include <linux/sched/signal.h>

#include "fun_queue.h"
#include "fun_dev.h"

#define FUN_ADMIN_CMD_TO_MS 3000

enum {
	AQA_ASQS_SHIFT = 0,
	AQA_ACQS_SHIFT = 16,
	AQA_MIN_QUEUE_SIZE = 2,
	AQA_MAX_QUEUE_SIZE = 4096
};

/* context for admin commands */
struct fun_cmd_ctx {
	fun_admin_callback_t cb;  /* callback to invoke on completion */
	void *cb_data;            /* user data provided to callback */
	int cpu;                  /* CPU where the cmd's tag was allocated */
};

/* Context for synchronous admin commands. */
struct fun_sync_cmd_ctx {
	struct completion compl;
	u8 *rsp_buf;              /* caller provided response buffer */
	unsigned int rsp_len;     /* response buffer size */
	u8 rsp_status;            /* command response status */
};

/* Wait for the CSTS.RDY bit to match @enabled. */
static int fun_wait_ready(struct fun_dev *fdev, bool enabled)
{
	unsigned int cap_to = NVME_CAP_TIMEOUT(fdev->cap_reg);
	u32 bit = enabled ? NVME_CSTS_RDY : 0;
	unsigned long deadline;

	deadline = ((cap_to + 1) * HZ / 2) + jiffies; /* CAP.TO is in 500ms */

	for (;;) {
		u32 csts = readl(fdev->bar + NVME_REG_CSTS);

		if (csts == ~0) {
			dev_err(fdev->dev, "CSTS register read %#x\n", csts);
			return -EIO;
		}

		if ((csts & NVME_CSTS_RDY) == bit)
			return 0;

		if (time_is_before_jiffies(deadline))
			break;

		msleep(100);
	}

	dev_err(fdev->dev,
		"Timed out waiting for device to indicate RDY %u; aborting %s\n",
		enabled, enabled ? "initialization" : "reset");
	return -ETIMEDOUT;
}

/* Check CSTS and return an error if it is unreadable or has unexpected
 * RDY value.
 */
static int fun_check_csts_rdy(struct fun_dev *fdev, unsigned int expected_rdy)
{
	u32 csts = readl(fdev->bar + NVME_REG_CSTS);
	u32 actual_rdy = csts & NVME_CSTS_RDY;

	if (csts == ~0) {
		dev_err(fdev->dev, "CSTS register read %#x\n", csts);
		return -EIO;
	}
	if (actual_rdy != expected_rdy) {
		dev_err(fdev->dev, "Unexpected CSTS RDY %u\n", actual_rdy);
		return -EINVAL;
	}
	return 0;
}

/* Check that CSTS RDY has the expected value. Then write a new value to the CC
 * register and wait for CSTS RDY to match the new CC ENABLE state.
 */
static int fun_update_cc_enable(struct fun_dev *fdev, unsigned int initial_rdy)
{
	int rc = fun_check_csts_rdy(fdev, initial_rdy);

	if (rc)
		return rc;
	writel(fdev->cc_reg, fdev->bar + NVME_REG_CC);
	return fun_wait_ready(fdev, !!(fdev->cc_reg & NVME_CC_ENABLE));
}

static int fun_disable_ctrl(struct fun_dev *fdev)
{
	fdev->cc_reg &= ~(NVME_CC_SHN_MASK | NVME_CC_ENABLE);
	return fun_update_cc_enable(fdev, 1);
}

static int fun_enable_ctrl(struct fun_dev *fdev, u32 admin_cqesz_log2,
			   u32 admin_sqesz_log2)
{
	fdev->cc_reg = (admin_cqesz_log2 << NVME_CC_IOCQES_SHIFT) |
		       (admin_sqesz_log2 << NVME_CC_IOSQES_SHIFT) |
		       ((PAGE_SHIFT - 12) << NVME_CC_MPS_SHIFT) |
		       NVME_CC_ENABLE;

	return fun_update_cc_enable(fdev, 0);
}

static int fun_map_bars(struct fun_dev *fdev, const char *name)
{
	struct pci_dev *pdev = to_pci_dev(fdev->dev);
	int err;

	err = pci_request_mem_regions(pdev, name);
	if (err) {
		dev_err(&pdev->dev,
			"Couldn't get PCI memory resources, err %d\n", err);
		return err;
	}

	fdev->bar = pci_ioremap_bar(pdev, 0);
	if (!fdev->bar) {
		dev_err(&pdev->dev, "Couldn't map BAR 0\n");
		pci_release_mem_regions(pdev);
		return -ENOMEM;
	}

	return 0;
}

static void fun_unmap_bars(struct fun_dev *fdev)
{
	struct pci_dev *pdev = to_pci_dev(fdev->dev);

	if (fdev->bar) {
		iounmap(fdev->bar);
		fdev->bar = NULL;
		pci_release_mem_regions(pdev);
	}
}

static int fun_set_dma_masks(struct device *dev)
{
	int err;

	err = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(64));
	if (err)
		dev_err(dev, "DMA mask configuration failed, err %d\n", err);
	return err;
}

static irqreturn_t fun_admin_irq(int irq, void *data)
{
	struct fun_queue *funq = data;

	return fun_process_cq(funq, 0) ? IRQ_HANDLED : IRQ_NONE;
}

static void fun_complete_admin_cmd(struct fun_queue *funq, void *data,
				   void *entry, const struct fun_cqe_info *info)
{
	const struct fun_admin_rsp_common *rsp_common = entry;
	struct fun_dev *fdev = funq->fdev;
	struct fun_cmd_ctx *cmd_ctx;
	int cpu;
	u16 cid;

	if (info->sqhd == cpu_to_be16(0xffff)) {
		dev_dbg(fdev->dev, "adminq event");
		if (fdev->adminq_cb)
			fdev->adminq_cb(fdev, entry);
		return;
	}

	cid = be16_to_cpu(rsp_common->cid);
	dev_dbg(fdev->dev, "admin CQE cid %u, op %u, ret %u\n", cid,
		rsp_common->op, rsp_common->ret);

	cmd_ctx = &fdev->cmd_ctx[cid];
	if (cmd_ctx->cpu < 0) {
		dev_err(fdev->dev,
			"admin CQE with CID=%u, op=%u does not match a pending command\n",
			cid, rsp_common->op);
		return;
	}

	if (cmd_ctx->cb)
		cmd_ctx->cb(fdev, entry, xchg(&cmd_ctx->cb_data, NULL));

	cpu = cmd_ctx->cpu;
	cmd_ctx->cpu = -1;
	sbitmap_queue_clear(&fdev->admin_sbq, cid, cpu);
}

static int fun_init_cmd_ctx(struct fun_dev *fdev, unsigned int ntags)
{
	unsigned int i;

	fdev->cmd_ctx = kvcalloc(ntags, sizeof(*fdev->cmd_ctx), GFP_KERNEL);
	if (!fdev->cmd_ctx)
		return -ENOMEM;

	for (i = 0; i < ntags; i++)
		fdev->cmd_ctx[i].cpu = -1;

	return 0;
}

/* Allocate and enable an admin queue and assign it the first IRQ vector. */
static int fun_enable_admin_queue(struct fun_dev *fdev,
				  const struct fun_dev_params *areq)
{
	struct fun_queue_alloc_req qreq = {
		.cqe_size_log2 = areq->cqe_size_log2,
		.sqe_size_log2 = areq->sqe_size_log2,
		.cq_depth = areq->cq_depth,
		.sq_depth = areq->sq_depth,
		.rq_depth = areq->rq_depth,
	};
	unsigned int ntags = areq->sq_depth - 1;
	struct fun_queue *funq;
	int rc;

	if (fdev->admin_q)
		return -EEXIST;

	if (areq->sq_depth < AQA_MIN_QUEUE_SIZE ||
	    areq->sq_depth > AQA_MAX_QUEUE_SIZE ||
	    areq->cq_depth < AQA_MIN_QUEUE_SIZE ||
	    areq->cq_depth > AQA_MAX_QUEUE_SIZE)
		return -EINVAL;

	fdev->admin_q = fun_alloc_queue(fdev, 0, &qreq);
	if (!fdev->admin_q)
		return -ENOMEM;

	rc = fun_init_cmd_ctx(fdev, ntags);
	if (rc)
		goto free_q;

	rc = sbitmap_queue_init_node(&fdev->admin_sbq, ntags, -1, false,
				     GFP_KERNEL, dev_to_node(fdev->dev));
	if (rc)
		goto free_cmd_ctx;

	funq = fdev->admin_q;
	funq->cq_vector = 0;
	rc = fun_request_irq(funq, dev_name(fdev->dev), fun_admin_irq, funq);
	if (rc)
		goto free_sbq;

	fun_set_cq_callback(funq, fun_complete_admin_cmd, NULL);
	fdev->adminq_cb = areq->event_cb;

	writel((funq->sq_depth - 1) << AQA_ASQS_SHIFT |
	       (funq->cq_depth - 1) << AQA_ACQS_SHIFT,
	       fdev->bar + NVME_REG_AQA);

	writeq(funq->sq_dma_addr, fdev->bar + NVME_REG_ASQ);
	writeq(funq->cq_dma_addr, fdev->bar + NVME_REG_ACQ);

	rc = fun_enable_ctrl(fdev, areq->cqe_size_log2, areq->sqe_size_log2);
	if (rc)
		goto free_irq;

	if (areq->rq_depth) {
		rc = fun_create_rq(funq);
		if (rc)
			goto disable_ctrl;

		funq_rq_post(funq);
	}

	return 0;

disable_ctrl:
	fun_disable_ctrl(fdev);
free_irq:
	fun_free_irq(funq);
free_sbq:
	sbitmap_queue_free(&fdev->admin_sbq);
free_cmd_ctx:
	kvfree(fdev->cmd_ctx);
	fdev->cmd_ctx = NULL;
free_q:
	fun_free_queue(fdev->admin_q);
	fdev->admin_q = NULL;
	return rc;
}

static void fun_disable_admin_queue(struct fun_dev *fdev)
{
	struct fun_queue *admq = fdev->admin_q;

	if (!admq)
		return;

	fun_disable_ctrl(fdev);

	fun_free_irq(admq);
	__fun_process_cq(admq, 0);

	sbitmap_queue_free(&fdev->admin_sbq);

	kvfree(fdev->cmd_ctx);
	fdev->cmd_ctx = NULL;

	fun_free_queue(admq);
	fdev->admin_q = NULL;
}

/* Return %true if the admin queue has stopped servicing commands as can be
 * detected through registers. This isn't exhaustive and may provide false
 * negatives.
 */
static bool fun_adminq_stopped(struct fun_dev *fdev)
{
	u32 csts = readl(fdev->bar + NVME_REG_CSTS);

	return (csts & (NVME_CSTS_CFS | NVME_CSTS_RDY)) != NVME_CSTS_RDY;
}

static int fun_wait_for_tag(struct fun_dev *fdev, int *cpup)
{
	struct sbitmap_queue *sbq = &fdev->admin_sbq;
	struct sbq_wait_state *ws = &sbq->ws[0];
	DEFINE_SBQ_WAIT(wait);
	int tag;

	for (;;) {
		sbitmap_prepare_to_wait(sbq, ws, &wait, TASK_UNINTERRUPTIBLE);
		if (fdev->suppress_cmds) {
			tag = -ESHUTDOWN;
			break;
		}
		tag = sbitmap_queue_get(sbq, cpup);
		if (tag >= 0)
			break;
		schedule();
	}

	sbitmap_finish_wait(sbq, ws, &wait);
	return tag;
}

/* Submit an asynchronous admin command. Caller is responsible for implementing
 * any waiting or timeout. Upon command completion the callback @cb is called.
 */
int fun_submit_admin_cmd(struct fun_dev *fdev, struct fun_admin_req_common *cmd,
			 fun_admin_callback_t cb, void *cb_data, bool wait_ok)
{
	struct fun_queue *funq = fdev->admin_q;
	unsigned int cmdsize = cmd->len8 * 8;
	struct fun_cmd_ctx *cmd_ctx;
	int tag, cpu, rc = 0;

	if (WARN_ON(cmdsize > (1 << funq->sqe_size_log2)))
		return -EMSGSIZE;

	tag = sbitmap_queue_get(&fdev->admin_sbq, &cpu);
	if (tag < 0) {
		if (!wait_ok)
			return -EAGAIN;
		tag = fun_wait_for_tag(fdev, &cpu);
		if (tag < 0)
			return tag;
	}

	cmd->cid = cpu_to_be16(tag);

	cmd_ctx = &fdev->cmd_ctx[tag];
	cmd_ctx->cb = cb;
	cmd_ctx->cb_data = cb_data;

	spin_lock(&funq->sq_lock);

	if (unlikely(fdev->suppress_cmds)) {
		rc = -ESHUTDOWN;
		sbitmap_queue_clear(&fdev->admin_sbq, tag, cpu);
	} else {
		cmd_ctx->cpu = cpu;
		memcpy(fun_sqe_at(funq, funq->sq_tail), cmd, cmdsize);

		dev_dbg(fdev->dev, "admin cmd @ %u: %8ph\n", funq->sq_tail,
			cmd);

		if (++funq->sq_tail == funq->sq_depth)
			funq->sq_tail = 0;
		writel(funq->sq_tail, funq->sq_db);
	}
	spin_unlock(&funq->sq_lock);
	return rc;
}

/* Abandon a pending admin command by clearing the issuer's callback data.
 * Failure indicates that the command either has already completed or its
 * completion is racing with this call.
 */
static bool fun_abandon_admin_cmd(struct fun_dev *fd,
				  const struct fun_admin_req_common *cmd,
				  void *cb_data)
{
	u16 cid = be16_to_cpu(cmd->cid);
	struct fun_cmd_ctx *cmd_ctx = &fd->cmd_ctx[cid];

	return cmpxchg(&cmd_ctx->cb_data, cb_data, NULL) == cb_data;
}

/* Stop submission of new admin commands and wake up any processes waiting for
 * tags. Already submitted commands are left to complete or time out.
 */
static void fun_admin_stop(struct fun_dev *fdev)
{
	spin_lock(&fdev->admin_q->sq_lock);
	fdev->suppress_cmds = true;
	spin_unlock(&fdev->admin_q->sq_lock);
	sbitmap_queue_wake_all(&fdev->admin_sbq);
}

/* The callback for synchronous execution of admin commands. It copies the
 * command response to the caller's buffer and signals completion.
 */
static void fun_admin_cmd_sync_cb(struct fun_dev *fd, void *rsp, void *cb_data)
{
	const struct fun_admin_rsp_common *rsp_common = rsp;
	struct fun_sync_cmd_ctx *ctx = cb_data;

	if (!ctx)
		return;         /* command issuer timed out and left */
	if (ctx->rsp_buf) {
		unsigned int rsp_len = rsp_common->len8 * 8;

		if (unlikely(rsp_len > ctx->rsp_len)) {
			dev_err(fd->dev,
				"response for op %u is %uB > response buffer %uB\n",
				rsp_common->op, rsp_len, ctx->rsp_len);
			rsp_len = ctx->rsp_len;
		}
		memcpy(ctx->rsp_buf, rsp, rsp_len);
	}
	ctx->rsp_status = rsp_common->ret;
	complete(&ctx->compl);
}

/* Submit a synchronous admin command. */
int fun_submit_admin_sync_cmd(struct fun_dev *fdev,
			      struct fun_admin_req_common *cmd, void *rsp,
			      size_t rspsize, unsigned int timeout)
{
	struct fun_sync_cmd_ctx ctx = {
		.compl = COMPLETION_INITIALIZER_ONSTACK(ctx.compl),
		.rsp_buf = rsp,
		.rsp_len = rspsize,
	};
	unsigned int cmdlen = cmd->len8 * 8;
	unsigned long jiffies_left;
	int ret;

	ret = fun_submit_admin_cmd(fdev, cmd, fun_admin_cmd_sync_cb, &ctx,
				   true);
	if (ret)
		return ret;

	if (!timeout)
		timeout = FUN_ADMIN_CMD_TO_MS;

	jiffies_left = wait_for_completion_timeout(&ctx.compl,
						   msecs_to_jiffies(timeout));
	if (!jiffies_left) {
		/* The command timed out. Attempt to cancel it so we can return.
		 * But if the command is in the process of completing we'll
		 * wait for it.
		 */
		if (fun_abandon_admin_cmd(fdev, cmd, &ctx)) {
			dev_err(fdev->dev, "admin command timed out: %*ph\n",
				cmdlen, cmd);
			fun_admin_stop(fdev);
			/* see if the timeout was due to a queue failure */
			if (fun_adminq_stopped(fdev))
				dev_err(fdev->dev,
					"device does not accept admin commands\n");

			return -ETIMEDOUT;
		}
		wait_for_completion(&ctx.compl);
	}

	if (ctx.rsp_status) {
		dev_err(fdev->dev, "admin command failed, err %d: %*ph\n",
			ctx.rsp_status, cmdlen, cmd);
	}

	return -ctx.rsp_status;
}
EXPORT_SYMBOL_GPL(fun_submit_admin_sync_cmd);

/* Return the number of device resources of the requested type. */
int fun_get_res_count(struct fun_dev *fdev, enum fun_admin_op res)
{
	union {
		struct fun_admin_res_count_req req;
		struct fun_admin_res_count_rsp rsp;
	} cmd;
	int rc;

	cmd.req.common = FUN_ADMIN_REQ_COMMON_INIT2(res, sizeof(cmd.req));
	cmd.req.count = FUN_ADMIN_SIMPLE_SUBOP_INIT(FUN_ADMIN_SUBOP_RES_COUNT,
						    0, 0);

	rc = fun_submit_admin_sync_cmd(fdev, &cmd.req.common, &cmd.rsp,
				       sizeof(cmd), 0);
	return rc ? rc : be32_to_cpu(cmd.rsp.count.data);
}
EXPORT_SYMBOL_GPL(fun_get_res_count);

/* Request that the instance of resource @res with the given id be deleted. */
int fun_res_destroy(struct fun_dev *fdev, enum fun_admin_op res,
		    unsigned int flags, u32 id)
{
	struct fun_admin_generic_destroy_req req = {
		.common = FUN_ADMIN_REQ_COMMON_INIT2(res, sizeof(req)),
		.destroy = FUN_ADMIN_SIMPLE_SUBOP_INIT(FUN_ADMIN_SUBOP_DESTROY,
						       flags, id)
	};

	return fun_submit_admin_sync_cmd(fdev, &req.common, NULL, 0, 0);
}
EXPORT_SYMBOL_GPL(fun_res_destroy);

/* Bind two entities of the given types and IDs. */
int fun_bind(struct fun_dev *fdev, enum fun_admin_bind_type type0,
	     unsigned int id0, enum fun_admin_bind_type type1,
	     unsigned int id1)
{
	struct {
		struct fun_admin_bind_req req;
		struct fun_admin_bind_entry entry[2];
	} cmd = {
		.req.common = FUN_ADMIN_REQ_COMMON_INIT2(FUN_ADMIN_OP_BIND,
							 sizeof(cmd)),
		.entry[0] = FUN_ADMIN_BIND_ENTRY_INIT(type0, id0),
		.entry[1] = FUN_ADMIN_BIND_ENTRY_INIT(type1, id1),
	};

	return fun_submit_admin_sync_cmd(fdev, &cmd.req.common, NULL, 0, 0);
}
EXPORT_SYMBOL_GPL(fun_bind);

static int fun_get_dev_limits(struct fun_dev *fdev)
{
	struct pci_dev *pdev = to_pci_dev(fdev->dev);
	unsigned int cq_count, sq_count, num_dbs;
	int rc;

	rc = fun_get_res_count(fdev, FUN_ADMIN_OP_EPCQ);
	if (rc < 0)
		return rc;
	cq_count = rc;

	rc = fun_get_res_count(fdev, FUN_ADMIN_OP_EPSQ);
	if (rc < 0)
		return rc;
	sq_count = rc;

	/* The admin queue consumes 1 CQ and at least 1 SQ. To be usable the
	 * device must provide additional queues.
	 */
	if (cq_count < 2 || sq_count < 2 + !!fdev->admin_q->rq_depth)
		return -EINVAL;

	/* Calculate the max QID based on SQ/CQ/doorbell counts.
	 * SQ/CQ doorbells alternate.
	 */
	num_dbs = (pci_resource_len(pdev, 0) - NVME_REG_DBS) >>
		  (2 + NVME_CAP_STRIDE(fdev->cap_reg));
	fdev->max_qid = min3(cq_count, sq_count, num_dbs / 2) - 1;
	fdev->kern_end_qid = fdev->max_qid + 1;
	return 0;
}

/* Allocate all MSI-X vectors available on a function and at least @min_vecs. */
static int fun_alloc_irqs(struct pci_dev *pdev, unsigned int min_vecs)
{
	int vecs, num_msix = pci_msix_vec_count(pdev);

	if (num_msix < 0)
		return num_msix;
	if (min_vecs > num_msix)
		return -ERANGE;

	vecs = pci_alloc_irq_vectors(pdev, min_vecs, num_msix, PCI_IRQ_MSIX);
	if (vecs > 0) {
		dev_info(&pdev->dev,
			 "Allocated %d IRQ vectors of %d requested\n",
			 vecs, num_msix);
	} else {
		dev_err(&pdev->dev,
			"Unable to allocate at least %u IRQ vectors\n",
			min_vecs);
	}
	return vecs;
}

/* Allocate and initialize the IRQ manager state. */
static int fun_alloc_irq_mgr(struct fun_dev *fdev)
{
	fdev->irq_map = bitmap_zalloc(fdev->num_irqs, GFP_KERNEL);
	if (!fdev->irq_map)
		return -ENOMEM;

	spin_lock_init(&fdev->irqmgr_lock);
	/* mark IRQ 0 allocated, it is used by the admin queue */
	__set_bit(0, fdev->irq_map);
	fdev->irqs_avail = fdev->num_irqs - 1;
	return 0;
}

/* Reserve @nirqs of the currently available IRQs and return their indices. */
int fun_reserve_irqs(struct fun_dev *fdev, unsigned int nirqs, u16 *irq_indices)
{
	unsigned int b, n = 0;
	int err = -ENOSPC;

	if (!nirqs)
		return 0;

	spin_lock(&fdev->irqmgr_lock);
	if (nirqs > fdev->irqs_avail)
		goto unlock;

	for_each_clear_bit(b, fdev->irq_map, fdev->num_irqs) {
		__set_bit(b, fdev->irq_map);
		irq_indices[n++] = b;
		if (n >= nirqs)
			break;
	}

	WARN_ON(n < nirqs);
	fdev->irqs_avail -= n;
	err = n;
unlock:
	spin_unlock(&fdev->irqmgr_lock);
	return err;
}
EXPORT_SYMBOL(fun_reserve_irqs);

/* Release @nirqs previously allocated IRQS with the supplied indices. */
void fun_release_irqs(struct fun_dev *fdev, unsigned int nirqs,
		      u16 *irq_indices)
{
	unsigned int i;

	spin_lock(&fdev->irqmgr_lock);
	for (i = 0; i < nirqs; i++)
		__clear_bit(irq_indices[i], fdev->irq_map);
	fdev->irqs_avail += nirqs;
	spin_unlock(&fdev->irqmgr_lock);
}
EXPORT_SYMBOL(fun_release_irqs);

static void fun_serv_handler(struct work_struct *work)
{
	struct fun_dev *fd = container_of(work, struct fun_dev, service_task);

	if (test_bit(FUN_SERV_DISABLED, &fd->service_flags))
		return;
	if (fd->serv_cb)
		fd->serv_cb(fd);
}

void fun_serv_stop(struct fun_dev *fd)
{
	set_bit(FUN_SERV_DISABLED, &fd->service_flags);
	cancel_work_sync(&fd->service_task);
}
EXPORT_SYMBOL_GPL(fun_serv_stop);

void fun_serv_restart(struct fun_dev *fd)
{
	clear_bit(FUN_SERV_DISABLED, &fd->service_flags);
	if (fd->service_flags)
		schedule_work(&fd->service_task);
}
EXPORT_SYMBOL_GPL(fun_serv_restart);

void fun_serv_sched(struct fun_dev *fd)
{
	if (!test_bit(FUN_SERV_DISABLED, &fd->service_flags))
		schedule_work(&fd->service_task);
}
EXPORT_SYMBOL_GPL(fun_serv_sched);

/* Check and try to get the device into a proper state for initialization,
 * i.e., CSTS.RDY = CC.EN = 0.
 */
static int sanitize_dev(struct fun_dev *fdev)
{
	int rc;

	fdev->cap_reg = readq(fdev->bar + NVME_REG_CAP);
	fdev->cc_reg = readl(fdev->bar + NVME_REG_CC);

	/* First get RDY to agree with the current EN. Give RDY the opportunity
	 * to complete a potential recent EN change.
	 */
	rc = fun_wait_ready(fdev, fdev->cc_reg & NVME_CC_ENABLE);
	if (rc)
		return rc;

	/* Next, reset the device if EN is currently 1. */
	if (fdev->cc_reg & NVME_CC_ENABLE)
		rc = fun_disable_ctrl(fdev);

	return rc;
}

/* Undo the device initialization of fun_dev_enable(). */
void fun_dev_disable(struct fun_dev *fdev)
{
	struct pci_dev *pdev = to_pci_dev(fdev->dev);

	pci_set_drvdata(pdev, NULL);

	if (fdev->fw_handle != FUN_HCI_ID_INVALID) {
		fun_res_destroy(fdev, FUN_ADMIN_OP_SWUPGRADE, 0,
				fdev->fw_handle);
		fdev->fw_handle = FUN_HCI_ID_INVALID;
	}

	fun_disable_admin_queue(fdev);

	bitmap_free(fdev->irq_map);
	pci_free_irq_vectors(pdev);

	pci_clear_master(pdev);
	pci_disable_pcie_error_reporting(pdev);
	pci_disable_device(pdev);

	fun_unmap_bars(fdev);
}
EXPORT_SYMBOL(fun_dev_disable);

/* Perform basic initialization of a device, including
 * - PCI config space setup and BAR0 mapping
 * - interrupt management initialization
 * - 1 admin queue setup
 * - determination of some device limits, such as number of queues.
 */
int fun_dev_enable(struct fun_dev *fdev, struct pci_dev *pdev,
		   const struct fun_dev_params *areq, const char *name)
{
	int rc;

	fdev->dev = &pdev->dev;
	rc = fun_map_bars(fdev, name);
	if (rc)
		return rc;

	rc = fun_set_dma_masks(fdev->dev);
	if (rc)
		goto unmap;

	rc = pci_enable_device_mem(pdev);
	if (rc) {
		dev_err(&pdev->dev, "Couldn't enable device, err %d\n", rc);
		goto unmap;
	}

	pci_enable_pcie_error_reporting(pdev);

	rc = sanitize_dev(fdev);
	if (rc)
		goto disable_dev;

	fdev->fw_handle = FUN_HCI_ID_INVALID;
	fdev->q_depth = NVME_CAP_MQES(fdev->cap_reg) + 1;
	fdev->db_stride = 1 << NVME_CAP_STRIDE(fdev->cap_reg);
	fdev->dbs = fdev->bar + NVME_REG_DBS;

	INIT_WORK(&fdev->service_task, fun_serv_handler);
	fdev->service_flags = FUN_SERV_DISABLED;
	fdev->serv_cb = areq->serv_cb;

	rc = fun_alloc_irqs(pdev, areq->min_msix + 1); /* +1 for admin CQ */
	if (rc < 0)
		goto disable_dev;
	fdev->num_irqs = rc;

	rc = fun_alloc_irq_mgr(fdev);
	if (rc)
		goto free_irqs;

	pci_set_master(pdev);
	rc = fun_enable_admin_queue(fdev, areq);
	if (rc)
		goto free_irq_mgr;

	rc = fun_get_dev_limits(fdev);
	if (rc < 0)
		goto disable_admin;

	pci_save_state(pdev);
	pci_set_drvdata(pdev, fdev);
	pcie_print_link_status(pdev);
	dev_dbg(fdev->dev, "q_depth %u, db_stride %u, max qid %d kern_end_qid %d\n",
		fdev->q_depth, fdev->db_stride, fdev->max_qid,
		fdev->kern_end_qid);
	return 0;

disable_admin:
	fun_disable_admin_queue(fdev);
free_irq_mgr:
	pci_clear_master(pdev);
	bitmap_free(fdev->irq_map);
free_irqs:
	pci_free_irq_vectors(pdev);
disable_dev:
	pci_disable_pcie_error_reporting(pdev);
	pci_disable_device(pdev);
unmap:
	fun_unmap_bars(fdev);
	return rc;
}
EXPORT_SYMBOL(fun_dev_enable);

MODULE_AUTHOR("Dimitris Michailidis <dmichail@fungible.com>");
MODULE_DESCRIPTION("Core services driver for Fungible devices");
MODULE_LICENSE("Dual BSD/GPL");

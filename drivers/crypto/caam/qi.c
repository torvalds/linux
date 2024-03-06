// SPDX-License-Identifier: GPL-2.0
/*
 * CAAM/SEC 4.x QI transport/backend driver
 * Queue Interface backend functionality
 *
 * Copyright 2013-2016 Freescale Semiconductor, Inc.
 * Copyright 2016-2017, 2019-2020 NXP
 */

#include <linux/cpumask.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/netdevice.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <soc/fsl/qman.h>

#include "debugfs.h"
#include "regs.h"
#include "qi.h"
#include "desc.h"
#include "intern.h"
#include "desc_constr.h"

#define PREHDR_RSLS_SHIFT	31
#define PREHDR_ABS		BIT(25)

/*
 * Use a reasonable backlog of frames (per CPU) as congestion threshold,
 * so that resources used by the in-flight buffers do not become a memory hog.
 */
#define MAX_RSP_FQ_BACKLOG_PER_CPU	256

#define CAAM_QI_ENQUEUE_RETRIES	10000

#define CAAM_NAPI_WEIGHT	63

/*
 * caam_napi - struct holding CAAM NAPI-related params
 * @irqtask: IRQ task for QI backend
 * @p: QMan portal
 */
struct caam_napi {
	struct napi_struct irqtask;
	struct qman_portal *p;
};

/*
 * caam_qi_pcpu_priv - percpu private data structure to main list of pending
 *                     responses expected on each cpu.
 * @caam_napi: CAAM NAPI params
 * @net_dev: netdev used by NAPI
 * @rsp_fq: response FQ from CAAM
 */
struct caam_qi_pcpu_priv {
	struct caam_napi caam_napi;
	struct net_device net_dev;
	struct qman_fq *rsp_fq;
} ____cacheline_aligned;

static DEFINE_PER_CPU(struct caam_qi_pcpu_priv, pcpu_qipriv);
static DEFINE_PER_CPU(int, last_cpu);

/*
 * caam_qi_priv - CAAM QI backend private params
 * @cgr: QMan congestion group
 */
struct caam_qi_priv {
	struct qman_cgr cgr;
};

static struct caam_qi_priv qipriv ____cacheline_aligned;

/*
 * This is written by only one core - the one that initialized the CGR - and
 * read by multiple cores (all the others).
 */
bool caam_congested __read_mostly;
EXPORT_SYMBOL(caam_congested);

/*
 * This is a cache of buffers, from which the users of CAAM QI driver
 * can allocate short (CAAM_QI_MEMCACHE_SIZE) buffers. It's faster than
 * doing malloc on the hotpath.
 * NOTE: A more elegant solution would be to have some headroom in the frames
 *       being processed. This could be added by the dpaa-ethernet driver.
 *       This would pose a problem for userspace application processing which
 *       cannot know of this limitation. So for now, this will work.
 * NOTE: The memcache is SMP-safe. No need to handle spinlocks in-here
 */
static struct kmem_cache *qi_cache;

static void *caam_iova_to_virt(struct iommu_domain *domain,
			       dma_addr_t iova_addr)
{
	phys_addr_t phys_addr;

	phys_addr = domain ? iommu_iova_to_phys(domain, iova_addr) : iova_addr;

	return phys_to_virt(phys_addr);
}

int caam_qi_enqueue(struct device *qidev, struct caam_drv_req *req)
{
	struct qm_fd fd;
	dma_addr_t addr;
	int ret;
	int num_retries = 0;

	qm_fd_clear_fd(&fd);
	qm_fd_set_compound(&fd, qm_sg_entry_get_len(&req->fd_sgt[1]));

	addr = dma_map_single(qidev, req->fd_sgt, sizeof(req->fd_sgt),
			      DMA_BIDIRECTIONAL);
	if (dma_mapping_error(qidev, addr)) {
		dev_err(qidev, "DMA mapping error for QI enqueue request\n");
		return -EIO;
	}
	qm_fd_addr_set64(&fd, addr);

	do {
		ret = qman_enqueue(req->drv_ctx->req_fq, &fd);
		if (likely(!ret)) {
			refcount_inc(&req->drv_ctx->refcnt);
			return 0;
		}

		if (ret != -EBUSY)
			break;
		num_retries++;
	} while (num_retries < CAAM_QI_ENQUEUE_RETRIES);

	dev_err(qidev, "qman_enqueue failed: %d\n", ret);

	return ret;
}
EXPORT_SYMBOL(caam_qi_enqueue);

static void caam_fq_ern_cb(struct qman_portal *qm, struct qman_fq *fq,
			   const union qm_mr_entry *msg)
{
	const struct qm_fd *fd;
	struct caam_drv_req *drv_req;
	struct device *qidev = &(raw_cpu_ptr(&pcpu_qipriv)->net_dev.dev);
	struct caam_drv_private *priv = dev_get_drvdata(qidev);

	fd = &msg->ern.fd;

	drv_req = caam_iova_to_virt(priv->domain, qm_fd_addr_get64(fd));
	if (!drv_req) {
		dev_err(qidev,
			"Can't find original request for CAAM response\n");
		return;
	}

	refcount_dec(&drv_req->drv_ctx->refcnt);

	if (qm_fd_get_format(fd) != qm_fd_compound) {
		dev_err(qidev, "Non-compound FD from CAAM\n");
		return;
	}

	dma_unmap_single(drv_req->drv_ctx->qidev, qm_fd_addr(fd),
			 sizeof(drv_req->fd_sgt), DMA_BIDIRECTIONAL);

	if (fd->status)
		drv_req->cbk(drv_req, be32_to_cpu(fd->status));
	else
		drv_req->cbk(drv_req, JRSTA_SSRC_QI);
}

static struct qman_fq *create_caam_req_fq(struct device *qidev,
					  struct qman_fq *rsp_fq,
					  dma_addr_t hwdesc,
					  int fq_sched_flag)
{
	int ret;
	struct qman_fq *req_fq;
	struct qm_mcc_initfq opts;

	req_fq = kzalloc(sizeof(*req_fq), GFP_ATOMIC);
	if (!req_fq)
		return ERR_PTR(-ENOMEM);

	req_fq->cb.ern = caam_fq_ern_cb;
	req_fq->cb.fqs = NULL;

	ret = qman_create_fq(0, QMAN_FQ_FLAG_DYNAMIC_FQID |
				QMAN_FQ_FLAG_TO_DCPORTAL, req_fq);
	if (ret) {
		dev_err(qidev, "Failed to create session req FQ\n");
		goto create_req_fq_fail;
	}

	memset(&opts, 0, sizeof(opts));
	opts.we_mask = cpu_to_be16(QM_INITFQ_WE_FQCTRL | QM_INITFQ_WE_DESTWQ |
				   QM_INITFQ_WE_CONTEXTB |
				   QM_INITFQ_WE_CONTEXTA | QM_INITFQ_WE_CGID);
	opts.fqd.fq_ctrl = cpu_to_be16(QM_FQCTRL_CPCSTASH | QM_FQCTRL_CGE);
	qm_fqd_set_destwq(&opts.fqd, qm_channel_caam, 2);
	opts.fqd.context_b = cpu_to_be32(qman_fq_fqid(rsp_fq));
	qm_fqd_context_a_set64(&opts.fqd, hwdesc);
	opts.fqd.cgid = qipriv.cgr.cgrid;

	ret = qman_init_fq(req_fq, fq_sched_flag, &opts);
	if (ret) {
		dev_err(qidev, "Failed to init session req FQ\n");
		goto init_req_fq_fail;
	}

	dev_dbg(qidev, "Allocated request FQ %u for CPU %u\n", req_fq->fqid,
		smp_processor_id());
	return req_fq;

init_req_fq_fail:
	qman_destroy_fq(req_fq);
create_req_fq_fail:
	kfree(req_fq);
	return ERR_PTR(ret);
}

static int empty_retired_fq(struct device *qidev, struct qman_fq *fq)
{
	int ret;

	ret = qman_volatile_dequeue(fq, QMAN_VOLATILE_FLAG_WAIT_INT |
				    QMAN_VOLATILE_FLAG_FINISH,
				    QM_VDQCR_PRECEDENCE_VDQCR |
				    QM_VDQCR_NUMFRAMES_TILLEMPTY);
	if (ret) {
		dev_err(qidev, "Volatile dequeue fail for FQ: %u\n", fq->fqid);
		return ret;
	}

	do {
		struct qman_portal *p;

		p = qman_get_affine_portal(smp_processor_id());
		qman_p_poll_dqrr(p, 16);
	} while (fq->flags & QMAN_FQ_STATE_NE);

	return 0;
}

static int kill_fq(struct device *qidev, struct qman_fq *fq)
{
	u32 flags;
	int ret;

	ret = qman_retire_fq(fq, &flags);
	if (ret < 0) {
		dev_err(qidev, "qman_retire_fq failed: %d\n", ret);
		return ret;
	}

	if (!ret)
		goto empty_fq;

	/* Async FQ retirement condition */
	if (ret == 1) {
		/* Retry till FQ gets in retired state */
		do {
			msleep(20);
		} while (fq->state != qman_fq_state_retired);

		WARN_ON(fq->flags & QMAN_FQ_STATE_BLOCKOOS);
		WARN_ON(fq->flags & QMAN_FQ_STATE_ORL);
	}

empty_fq:
	if (fq->flags & QMAN_FQ_STATE_NE) {
		ret = empty_retired_fq(qidev, fq);
		if (ret) {
			dev_err(qidev, "empty_retired_fq fail for FQ: %u\n",
				fq->fqid);
			return ret;
		}
	}

	ret = qman_oos_fq(fq);
	if (ret)
		dev_err(qidev, "OOS of FQID: %u failed\n", fq->fqid);

	qman_destroy_fq(fq);
	kfree(fq);

	return ret;
}

static int empty_caam_fq(struct qman_fq *fq, struct caam_drv_ctx *drv_ctx)
{
	int ret;
	int retries = 10;
	struct qm_mcr_queryfq_np np;

	/* Wait till the older CAAM FQ get empty */
	do {
		ret = qman_query_fq_np(fq, &np);
		if (ret)
			return ret;

		if (!qm_mcr_np_get(&np, frm_cnt))
			break;

		msleep(20);
	} while (1);

	/* Wait until pending jobs from this FQ are processed by CAAM */
	do {
		if (refcount_read(&drv_ctx->refcnt) == 1)
			break;

		msleep(20);
	} while (--retries);

	if (!retries)
		dev_warn_once(drv_ctx->qidev, "%d frames from FQID %u still pending in CAAM\n",
			      refcount_read(&drv_ctx->refcnt), fq->fqid);

	return 0;
}

int caam_drv_ctx_update(struct caam_drv_ctx *drv_ctx, u32 *sh_desc)
{
	int ret;
	u32 num_words;
	struct qman_fq *new_fq, *old_fq;
	struct device *qidev = drv_ctx->qidev;

	num_words = desc_len(sh_desc);
	if (num_words > MAX_SDLEN) {
		dev_err(qidev, "Invalid descriptor len: %d words\n", num_words);
		return -EINVAL;
	}

	/* Note down older req FQ */
	old_fq = drv_ctx->req_fq;

	/* Create a new req FQ in parked state */
	new_fq = create_caam_req_fq(drv_ctx->qidev, drv_ctx->rsp_fq,
				    drv_ctx->context_a, 0);
	if (IS_ERR(new_fq)) {
		dev_err(qidev, "FQ allocation for shdesc update failed\n");
		return PTR_ERR(new_fq);
	}

	/* Hook up new FQ to context so that new requests keep queuing */
	drv_ctx->req_fq = new_fq;

	/* Empty and remove the older FQ */
	ret = empty_caam_fq(old_fq, drv_ctx);
	if (ret) {
		dev_err(qidev, "Old CAAM FQ empty failed: %d\n", ret);

		/* We can revert to older FQ */
		drv_ctx->req_fq = old_fq;

		if (kill_fq(qidev, new_fq))
			dev_warn(qidev, "New CAAM FQ kill failed\n");

		return ret;
	}

	/*
	 * Re-initialise pre-header. Set RSLS and SDLEN.
	 * Update the shared descriptor for driver context.
	 */
	drv_ctx->prehdr[0] = cpu_to_caam32((1 << PREHDR_RSLS_SHIFT) |
					   num_words);
	drv_ctx->prehdr[1] = cpu_to_caam32(PREHDR_ABS);
	memcpy(drv_ctx->sh_desc, sh_desc, desc_bytes(sh_desc));
	dma_sync_single_for_device(qidev, drv_ctx->context_a,
				   sizeof(drv_ctx->sh_desc) +
				   sizeof(drv_ctx->prehdr),
				   DMA_BIDIRECTIONAL);

	/* Put the new FQ in scheduled state */
	ret = qman_schedule_fq(new_fq);
	if (ret) {
		dev_err(qidev, "Fail to sched new CAAM FQ, ecode = %d\n", ret);

		/*
		 * We can kill new FQ and revert to old FQ.
		 * Since the desc is already modified, it is success case
		 */

		drv_ctx->req_fq = old_fq;

		if (kill_fq(qidev, new_fq))
			dev_warn(qidev, "New CAAM FQ kill failed\n");
	} else if (kill_fq(qidev, old_fq)) {
		dev_warn(qidev, "Old CAAM FQ kill failed\n");
	}

	return 0;
}
EXPORT_SYMBOL(caam_drv_ctx_update);

struct caam_drv_ctx *caam_drv_ctx_init(struct device *qidev,
				       int *cpu,
				       u32 *sh_desc)
{
	size_t size;
	u32 num_words;
	dma_addr_t hwdesc;
	struct caam_drv_ctx *drv_ctx;
	const cpumask_t *cpus = qman_affine_cpus();

	num_words = desc_len(sh_desc);
	if (num_words > MAX_SDLEN) {
		dev_err(qidev, "Invalid descriptor len: %d words\n",
			num_words);
		return ERR_PTR(-EINVAL);
	}

	drv_ctx = kzalloc(sizeof(*drv_ctx), GFP_ATOMIC);
	if (!drv_ctx)
		return ERR_PTR(-ENOMEM);

	/*
	 * Initialise pre-header - set RSLS and SDLEN - and shared descriptor
	 * and dma-map them.
	 */
	drv_ctx->prehdr[0] = cpu_to_caam32((1 << PREHDR_RSLS_SHIFT) |
					   num_words);
	drv_ctx->prehdr[1] = cpu_to_caam32(PREHDR_ABS);
	memcpy(drv_ctx->sh_desc, sh_desc, desc_bytes(sh_desc));
	size = sizeof(drv_ctx->prehdr) + sizeof(drv_ctx->sh_desc);
	hwdesc = dma_map_single(qidev, drv_ctx->prehdr, size,
				DMA_BIDIRECTIONAL);
	if (dma_mapping_error(qidev, hwdesc)) {
		dev_err(qidev, "DMA map error for preheader + shdesc\n");
		kfree(drv_ctx);
		return ERR_PTR(-ENOMEM);
	}
	drv_ctx->context_a = hwdesc;

	/* If given CPU does not own the portal, choose another one that does */
	if (!cpumask_test_cpu(*cpu, cpus)) {
		int *pcpu = &get_cpu_var(last_cpu);

		*pcpu = cpumask_next(*pcpu, cpus);
		if (*pcpu >= nr_cpu_ids)
			*pcpu = cpumask_first(cpus);
		*cpu = *pcpu;

		put_cpu_var(last_cpu);
	}
	drv_ctx->cpu = *cpu;

	/* Find response FQ hooked with this CPU */
	drv_ctx->rsp_fq = per_cpu(pcpu_qipriv.rsp_fq, drv_ctx->cpu);

	/* Attach request FQ */
	drv_ctx->req_fq = create_caam_req_fq(qidev, drv_ctx->rsp_fq, hwdesc,
					     QMAN_INITFQ_FLAG_SCHED);
	if (IS_ERR(drv_ctx->req_fq)) {
		dev_err(qidev, "create_caam_req_fq failed\n");
		dma_unmap_single(qidev, hwdesc, size, DMA_BIDIRECTIONAL);
		kfree(drv_ctx);
		return ERR_PTR(-ENOMEM);
	}

	/* init reference counter used to track references to request FQ */
	refcount_set(&drv_ctx->refcnt, 1);

	drv_ctx->qidev = qidev;
	return drv_ctx;
}
EXPORT_SYMBOL(caam_drv_ctx_init);

void *qi_cache_alloc(gfp_t flags)
{
	return kmem_cache_alloc(qi_cache, flags);
}
EXPORT_SYMBOL(qi_cache_alloc);

void qi_cache_free(void *obj)
{
	kmem_cache_free(qi_cache, obj);
}
EXPORT_SYMBOL(qi_cache_free);

static int caam_qi_poll(struct napi_struct *napi, int budget)
{
	struct caam_napi *np = container_of(napi, struct caam_napi, irqtask);

	int cleaned = qman_p_poll_dqrr(np->p, budget);

	if (cleaned < budget) {
		napi_complete(napi);
		qman_p_irqsource_add(np->p, QM_PIRQ_DQRI);
	}

	return cleaned;
}

void caam_drv_ctx_rel(struct caam_drv_ctx *drv_ctx)
{
	if (IS_ERR_OR_NULL(drv_ctx))
		return;

	/* Remove request FQ */
	if (kill_fq(drv_ctx->qidev, drv_ctx->req_fq))
		dev_err(drv_ctx->qidev, "Crypto session req FQ kill failed\n");

	dma_unmap_single(drv_ctx->qidev, drv_ctx->context_a,
			 sizeof(drv_ctx->sh_desc) + sizeof(drv_ctx->prehdr),
			 DMA_BIDIRECTIONAL);
	kfree(drv_ctx);
}
EXPORT_SYMBOL(caam_drv_ctx_rel);

static void caam_qi_shutdown(void *data)
{
	int i;
	struct device *qidev = data;
	struct caam_qi_priv *priv = &qipriv;
	const cpumask_t *cpus = qman_affine_cpus();

	for_each_cpu(i, cpus) {
		struct napi_struct *irqtask;

		irqtask = &per_cpu_ptr(&pcpu_qipriv.caam_napi, i)->irqtask;
		napi_disable(irqtask);
		netif_napi_del(irqtask);

		if (kill_fq(qidev, per_cpu(pcpu_qipriv.rsp_fq, i)))
			dev_err(qidev, "Rsp FQ kill failed, cpu: %d\n", i);
	}

	qman_delete_cgr_safe(&priv->cgr);
	qman_release_cgrid(priv->cgr.cgrid);

	kmem_cache_destroy(qi_cache);
}

static void cgr_cb(struct qman_portal *qm, struct qman_cgr *cgr, int congested)
{
	caam_congested = congested;

	if (congested) {
		caam_debugfs_qi_congested();

		pr_debug_ratelimited("CAAM entered congestion\n");

	} else {
		pr_debug_ratelimited("CAAM exited congestion\n");
	}
}

static int caam_qi_napi_schedule(struct qman_portal *p, struct caam_napi *np,
				 bool sched_napi)
{
	if (sched_napi) {
		/* Disable QMan IRQ source and invoke NAPI */
		qman_p_irqsource_remove(p, QM_PIRQ_DQRI);
		np->p = p;
		napi_schedule(&np->irqtask);
		return 1;
	}
	return 0;
}

static enum qman_cb_dqrr_result caam_rsp_fq_dqrr_cb(struct qman_portal *p,
						    struct qman_fq *rsp_fq,
						    const struct qm_dqrr_entry *dqrr,
						    bool sched_napi)
{
	struct caam_napi *caam_napi = raw_cpu_ptr(&pcpu_qipriv.caam_napi);
	struct caam_drv_req *drv_req;
	const struct qm_fd *fd;
	struct device *qidev = &(raw_cpu_ptr(&pcpu_qipriv)->net_dev.dev);
	struct caam_drv_private *priv = dev_get_drvdata(qidev);
	u32 status;

	if (caam_qi_napi_schedule(p, caam_napi, sched_napi))
		return qman_cb_dqrr_stop;

	fd = &dqrr->fd;

	drv_req = caam_iova_to_virt(priv->domain, qm_fd_addr_get64(fd));
	if (unlikely(!drv_req)) {
		dev_err(qidev,
			"Can't find original request for caam response\n");
		return qman_cb_dqrr_consume;
	}

	refcount_dec(&drv_req->drv_ctx->refcnt);

	status = be32_to_cpu(fd->status);
	if (unlikely(status)) {
		u32 ssrc = status & JRSTA_SSRC_MASK;
		u8 err_id = status & JRSTA_CCBERR_ERRID_MASK;

		if (ssrc != JRSTA_SSRC_CCB_ERROR ||
		    err_id != JRSTA_CCBERR_ERRID_ICVCHK)
			dev_err_ratelimited(qidev,
					    "Error: %#x in CAAM response FD\n",
					    status);
	}

	if (unlikely(qm_fd_get_format(fd) != qm_fd_compound)) {
		dev_err(qidev, "Non-compound FD from CAAM\n");
		return qman_cb_dqrr_consume;
	}

	dma_unmap_single(drv_req->drv_ctx->qidev, qm_fd_addr(fd),
			 sizeof(drv_req->fd_sgt), DMA_BIDIRECTIONAL);

	drv_req->cbk(drv_req, status);
	return qman_cb_dqrr_consume;
}

static int alloc_rsp_fq_cpu(struct device *qidev, unsigned int cpu)
{
	struct qm_mcc_initfq opts;
	struct qman_fq *fq;
	int ret;

	fq = kzalloc(sizeof(*fq), GFP_KERNEL);
	if (!fq)
		return -ENOMEM;

	fq->cb.dqrr = caam_rsp_fq_dqrr_cb;

	ret = qman_create_fq(0, QMAN_FQ_FLAG_NO_ENQUEUE |
			     QMAN_FQ_FLAG_DYNAMIC_FQID, fq);
	if (ret) {
		dev_err(qidev, "Rsp FQ create failed\n");
		kfree(fq);
		return -ENODEV;
	}

	memset(&opts, 0, sizeof(opts));
	opts.we_mask = cpu_to_be16(QM_INITFQ_WE_FQCTRL | QM_INITFQ_WE_DESTWQ |
				   QM_INITFQ_WE_CONTEXTB |
				   QM_INITFQ_WE_CONTEXTA | QM_INITFQ_WE_CGID);
	opts.fqd.fq_ctrl = cpu_to_be16(QM_FQCTRL_CTXASTASHING |
				       QM_FQCTRL_CPCSTASH | QM_FQCTRL_CGE);
	qm_fqd_set_destwq(&opts.fqd, qman_affine_channel(cpu), 3);
	opts.fqd.cgid = qipriv.cgr.cgrid;
	opts.fqd.context_a.stashing.exclusive =	QM_STASHING_EXCL_CTX |
						QM_STASHING_EXCL_DATA;
	qm_fqd_set_stashing(&opts.fqd, 0, 1, 1);

	ret = qman_init_fq(fq, QMAN_INITFQ_FLAG_SCHED, &opts);
	if (ret) {
		dev_err(qidev, "Rsp FQ init failed\n");
		kfree(fq);
		return -ENODEV;
	}

	per_cpu(pcpu_qipriv.rsp_fq, cpu) = fq;

	dev_dbg(qidev, "Allocated response FQ %u for CPU %u", fq->fqid, cpu);
	return 0;
}

static int init_cgr(struct device *qidev)
{
	int ret;
	struct qm_mcc_initcgr opts;
	const u64 val = (u64)cpumask_weight(qman_affine_cpus()) *
			MAX_RSP_FQ_BACKLOG_PER_CPU;

	ret = qman_alloc_cgrid(&qipriv.cgr.cgrid);
	if (ret) {
		dev_err(qidev, "CGR alloc failed for rsp FQs: %d\n", ret);
		return ret;
	}

	qipriv.cgr.cb = cgr_cb;
	memset(&opts, 0, sizeof(opts));
	opts.we_mask = cpu_to_be16(QM_CGR_WE_CSCN_EN | QM_CGR_WE_CS_THRES |
				   QM_CGR_WE_MODE);
	opts.cgr.cscn_en = QM_CGR_EN;
	opts.cgr.mode = QMAN_CGR_MODE_FRAME;
	qm_cgr_cs_thres_set64(&opts.cgr.cs_thres, val, 1);

	ret = qman_create_cgr(&qipriv.cgr, QMAN_CGR_FLAG_USE_INIT, &opts);
	if (ret) {
		dev_err(qidev, "Error %d creating CAAM CGRID: %u\n", ret,
			qipriv.cgr.cgrid);
		return ret;
	}

	dev_dbg(qidev, "Congestion threshold set to %llu\n", val);
	return 0;
}

static int alloc_rsp_fqs(struct device *qidev)
{
	int ret, i;
	const cpumask_t *cpus = qman_affine_cpus();

	/*Now create response FQs*/
	for_each_cpu(i, cpus) {
		ret = alloc_rsp_fq_cpu(qidev, i);
		if (ret) {
			dev_err(qidev, "CAAM rsp FQ alloc failed, cpu: %u", i);
			return ret;
		}
	}

	return 0;
}

static void free_rsp_fqs(void)
{
	int i;
	const cpumask_t *cpus = qman_affine_cpus();

	for_each_cpu(i, cpus)
		kfree(per_cpu(pcpu_qipriv.rsp_fq, i));
}

int caam_qi_init(struct platform_device *caam_pdev)
{
	int err, i;
	struct device *ctrldev = &caam_pdev->dev, *qidev;
	struct caam_drv_private *ctrlpriv;
	const cpumask_t *cpus = qman_affine_cpus();

	ctrlpriv = dev_get_drvdata(ctrldev);
	qidev = ctrldev;

	/* Initialize the congestion detection */
	err = init_cgr(qidev);
	if (err) {
		dev_err(qidev, "CGR initialization failed: %d\n", err);
		return err;
	}

	/* Initialise response FQs */
	err = alloc_rsp_fqs(qidev);
	if (err) {
		dev_err(qidev, "Can't allocate CAAM response FQs: %d\n", err);
		free_rsp_fqs();
		return err;
	}

	/*
	 * Enable the NAPI contexts on each of the core which has an affine
	 * portal.
	 */
	for_each_cpu(i, cpus) {
		struct caam_qi_pcpu_priv *priv = per_cpu_ptr(&pcpu_qipriv, i);
		struct caam_napi *caam_napi = &priv->caam_napi;
		struct napi_struct *irqtask = &caam_napi->irqtask;
		struct net_device *net_dev = &priv->net_dev;

		net_dev->dev = *qidev;
		INIT_LIST_HEAD(&net_dev->napi_list);

		netif_napi_add_tx_weight(net_dev, irqtask, caam_qi_poll,
					 CAAM_NAPI_WEIGHT);

		napi_enable(irqtask);
	}

	qi_cache = kmem_cache_create("caamqicache", CAAM_QI_MEMCACHE_SIZE,
				     dma_get_cache_alignment(), 0, NULL);
	if (!qi_cache) {
		dev_err(qidev, "Can't allocate CAAM cache\n");
		free_rsp_fqs();
		return -ENOMEM;
	}

	caam_debugfs_qi_init(ctrlpriv);

	err = devm_add_action_or_reset(qidev, caam_qi_shutdown, ctrlpriv);
	if (err)
		return err;

	dev_info(qidev, "Linux CAAM Queue I/F driver initialised\n");
	return 0;
}

// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2016 Avago Technologies.  All rights reserved.
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/blk-mq.h>
#include <linux/parser.h>
#include <linux/random.h>
#include <uapi/scsi/fc/fc_fs.h>
#include <uapi/scsi/fc/fc_els.h>

#include "nvmet.h"
#include <linux/nvme-fc-driver.h>
#include <linux/nvme-fc.h>


/* *************************** Data Structures/Defines ****************** */


#define NVMET_LS_CTX_COUNT		256

/* for this implementation, assume small single frame rqst/rsp */
#define NVME_FC_MAX_LS_BUFFER_SIZE		2048

struct nvmet_fc_tgtport;
struct nvmet_fc_tgt_assoc;

struct nvmet_fc_ls_iod {
	struct nvmefc_tgt_ls_req	*lsreq;
	struct nvmefc_tgt_fcp_req	*fcpreq;	/* only if RS */

	struct list_head		ls_list;	/* tgtport->ls_list */

	struct nvmet_fc_tgtport		*tgtport;
	struct nvmet_fc_tgt_assoc	*assoc;

	u8				*rqstbuf;
	u8				*rspbuf;
	u16				rqstdatalen;
	dma_addr_t			rspdma;

	struct scatterlist		sg[2];

	struct work_struct		work;
} __aligned(sizeof(unsigned long long));

/* desired maximum for a single sequence - if sg list allows it */
#define NVMET_FC_MAX_SEQ_LENGTH		(256 * 1024)

enum nvmet_fcp_datadir {
	NVMET_FCP_NODATA,
	NVMET_FCP_WRITE,
	NVMET_FCP_READ,
	NVMET_FCP_ABORTED,
};

struct nvmet_fc_fcp_iod {
	struct nvmefc_tgt_fcp_req	*fcpreq;

	struct nvme_fc_cmd_iu		cmdiubuf;
	struct nvme_fc_ersp_iu		rspiubuf;
	dma_addr_t			rspdma;
	struct scatterlist		*next_sg;
	struct scatterlist		*data_sg;
	int				data_sg_cnt;
	u32				offset;
	enum nvmet_fcp_datadir		io_dir;
	bool				active;
	bool				abort;
	bool				aborted;
	bool				writedataactive;
	spinlock_t			flock;

	struct nvmet_req		req;
	struct work_struct		defer_work;

	struct nvmet_fc_tgtport		*tgtport;
	struct nvmet_fc_tgt_queue	*queue;

	struct list_head		fcp_list;	/* tgtport->fcp_list */
};

struct nvmet_fc_tgtport {

	struct nvmet_fc_target_port	fc_target_port;

	struct list_head		tgt_list; /* nvmet_fc_target_list */
	struct device			*dev;	/* dev for dma mapping */
	struct nvmet_fc_target_template	*ops;

	struct nvmet_fc_ls_iod		*iod;
	spinlock_t			lock;
	struct list_head		ls_list;
	struct list_head		ls_busylist;
	struct list_head		assoc_list;
	struct ida			assoc_cnt;
	struct nvmet_fc_port_entry	*pe;
	struct kref			ref;
	u32				max_sg_cnt;
};

struct nvmet_fc_port_entry {
	struct nvmet_fc_tgtport		*tgtport;
	struct nvmet_port		*port;
	u64				node_name;
	u64				port_name;
	struct list_head		pe_list;
};

struct nvmet_fc_defer_fcp_req {
	struct list_head		req_list;
	struct nvmefc_tgt_fcp_req	*fcp_req;
};

struct nvmet_fc_tgt_queue {
	bool				ninetypercent;
	u16				qid;
	u16				sqsize;
	u16				ersp_ratio;
	__le16				sqhd;
	atomic_t			connected;
	atomic_t			sqtail;
	atomic_t			zrspcnt;
	atomic_t			rsn;
	spinlock_t			qlock;
	struct nvmet_cq			nvme_cq;
	struct nvmet_sq			nvme_sq;
	struct nvmet_fc_tgt_assoc	*assoc;
	struct list_head		fod_list;
	struct list_head		pending_cmd_list;
	struct list_head		avail_defer_list;
	struct workqueue_struct		*work_q;
	struct kref			ref;
	struct nvmet_fc_fcp_iod		fod[];		/* array of fcp_iods */
} __aligned(sizeof(unsigned long long));

struct nvmet_fc_tgt_assoc {
	u64				association_id;
	u32				a_id;
	struct nvmet_fc_tgtport		*tgtport;
	struct list_head		a_list;
	struct nvmet_fc_tgt_queue	*queues[NVMET_NR_QUEUES + 1];
	struct kref			ref;
	struct work_struct		del_work;
};


static inline int
nvmet_fc_iodnum(struct nvmet_fc_ls_iod *iodptr)
{
	return (iodptr - iodptr->tgtport->iod);
}

static inline int
nvmet_fc_fodnum(struct nvmet_fc_fcp_iod *fodptr)
{
	return (fodptr - fodptr->queue->fod);
}


/*
 * Association and Connection IDs:
 *
 * Association ID will have random number in upper 6 bytes and zero
 *   in lower 2 bytes
 *
 * Connection IDs will be Association ID with QID or'd in lower 2 bytes
 *
 * note: Association ID = Connection ID for queue 0
 */
#define BYTES_FOR_QID			sizeof(u16)
#define BYTES_FOR_QID_SHIFT		(BYTES_FOR_QID * 8)
#define NVMET_FC_QUEUEID_MASK		((u64)((1 << BYTES_FOR_QID_SHIFT) - 1))

static inline u64
nvmet_fc_makeconnid(struct nvmet_fc_tgt_assoc *assoc, u16 qid)
{
	return (assoc->association_id | qid);
}

static inline u64
nvmet_fc_getassociationid(u64 connectionid)
{
	return connectionid & ~NVMET_FC_QUEUEID_MASK;
}

static inline u16
nvmet_fc_getqueueid(u64 connectionid)
{
	return (u16)(connectionid & NVMET_FC_QUEUEID_MASK);
}

static inline struct nvmet_fc_tgtport *
targetport_to_tgtport(struct nvmet_fc_target_port *targetport)
{
	return container_of(targetport, struct nvmet_fc_tgtport,
				 fc_target_port);
}

static inline struct nvmet_fc_fcp_iod *
nvmet_req_to_fod(struct nvmet_req *nvme_req)
{
	return container_of(nvme_req, struct nvmet_fc_fcp_iod, req);
}


/* *************************** Globals **************************** */


static DEFINE_SPINLOCK(nvmet_fc_tgtlock);

static LIST_HEAD(nvmet_fc_target_list);
static DEFINE_IDA(nvmet_fc_tgtport_cnt);
static LIST_HEAD(nvmet_fc_portentry_list);


static void nvmet_fc_handle_ls_rqst_work(struct work_struct *work);
static void nvmet_fc_fcp_rqst_op_defer_work(struct work_struct *work);
static void nvmet_fc_tgt_a_put(struct nvmet_fc_tgt_assoc *assoc);
static int nvmet_fc_tgt_a_get(struct nvmet_fc_tgt_assoc *assoc);
static void nvmet_fc_tgt_q_put(struct nvmet_fc_tgt_queue *queue);
static int nvmet_fc_tgt_q_get(struct nvmet_fc_tgt_queue *queue);
static void nvmet_fc_tgtport_put(struct nvmet_fc_tgtport *tgtport);
static int nvmet_fc_tgtport_get(struct nvmet_fc_tgtport *tgtport);
static void nvmet_fc_handle_fcp_rqst(struct nvmet_fc_tgtport *tgtport,
					struct nvmet_fc_fcp_iod *fod);
static void nvmet_fc_delete_target_assoc(struct nvmet_fc_tgt_assoc *assoc);


/* *********************** FC-NVME DMA Handling **************************** */

/*
 * The fcloop device passes in a NULL device pointer. Real LLD's will
 * pass in a valid device pointer. If NULL is passed to the dma mapping
 * routines, depending on the platform, it may or may not succeed, and
 * may crash.
 *
 * As such:
 * Wrapper all the dma routines and check the dev pointer.
 *
 * If simple mappings (return just a dma address, we'll noop them,
 * returning a dma address of 0.
 *
 * On more complex mappings (dma_map_sg), a pseudo routine fills
 * in the scatter list, setting all dma addresses to 0.
 */

static inline dma_addr_t
fc_dma_map_single(struct device *dev, void *ptr, size_t size,
		enum dma_data_direction dir)
{
	return dev ? dma_map_single(dev, ptr, size, dir) : (dma_addr_t)0L;
}

static inline int
fc_dma_mapping_error(struct device *dev, dma_addr_t dma_addr)
{
	return dev ? dma_mapping_error(dev, dma_addr) : 0;
}

static inline void
fc_dma_unmap_single(struct device *dev, dma_addr_t addr, size_t size,
	enum dma_data_direction dir)
{
	if (dev)
		dma_unmap_single(dev, addr, size, dir);
}

static inline void
fc_dma_sync_single_for_cpu(struct device *dev, dma_addr_t addr, size_t size,
		enum dma_data_direction dir)
{
	if (dev)
		dma_sync_single_for_cpu(dev, addr, size, dir);
}

static inline void
fc_dma_sync_single_for_device(struct device *dev, dma_addr_t addr, size_t size,
		enum dma_data_direction dir)
{
	if (dev)
		dma_sync_single_for_device(dev, addr, size, dir);
}

/* pseudo dma_map_sg call */
static int
fc_map_sg(struct scatterlist *sg, int nents)
{
	struct scatterlist *s;
	int i;

	WARN_ON(nents == 0 || sg[0].length == 0);

	for_each_sg(sg, s, nents, i) {
		s->dma_address = 0L;
#ifdef CONFIG_NEED_SG_DMA_LENGTH
		s->dma_length = s->length;
#endif
	}
	return nents;
}

static inline int
fc_dma_map_sg(struct device *dev, struct scatterlist *sg, int nents,
		enum dma_data_direction dir)
{
	return dev ? dma_map_sg(dev, sg, nents, dir) : fc_map_sg(sg, nents);
}

static inline void
fc_dma_unmap_sg(struct device *dev, struct scatterlist *sg, int nents,
		enum dma_data_direction dir)
{
	if (dev)
		dma_unmap_sg(dev, sg, nents, dir);
}


/* *********************** FC-NVME Port Management ************************ */


static int
nvmet_fc_alloc_ls_iodlist(struct nvmet_fc_tgtport *tgtport)
{
	struct nvmet_fc_ls_iod *iod;
	int i;

	iod = kcalloc(NVMET_LS_CTX_COUNT, sizeof(struct nvmet_fc_ls_iod),
			GFP_KERNEL);
	if (!iod)
		return -ENOMEM;

	tgtport->iod = iod;

	for (i = 0; i < NVMET_LS_CTX_COUNT; iod++, i++) {
		INIT_WORK(&iod->work, nvmet_fc_handle_ls_rqst_work);
		iod->tgtport = tgtport;
		list_add_tail(&iod->ls_list, &tgtport->ls_list);

		iod->rqstbuf = kcalloc(2, NVME_FC_MAX_LS_BUFFER_SIZE,
			GFP_KERNEL);
		if (!iod->rqstbuf)
			goto out_fail;

		iod->rspbuf = iod->rqstbuf + NVME_FC_MAX_LS_BUFFER_SIZE;

		iod->rspdma = fc_dma_map_single(tgtport->dev, iod->rspbuf,
						NVME_FC_MAX_LS_BUFFER_SIZE,
						DMA_TO_DEVICE);
		if (fc_dma_mapping_error(tgtport->dev, iod->rspdma))
			goto out_fail;
	}

	return 0;

out_fail:
	kfree(iod->rqstbuf);
	list_del(&iod->ls_list);
	for (iod--, i--; i >= 0; iod--, i--) {
		fc_dma_unmap_single(tgtport->dev, iod->rspdma,
				NVME_FC_MAX_LS_BUFFER_SIZE, DMA_TO_DEVICE);
		kfree(iod->rqstbuf);
		list_del(&iod->ls_list);
	}

	kfree(iod);

	return -EFAULT;
}

static void
nvmet_fc_free_ls_iodlist(struct nvmet_fc_tgtport *tgtport)
{
	struct nvmet_fc_ls_iod *iod = tgtport->iod;
	int i;

	for (i = 0; i < NVMET_LS_CTX_COUNT; iod++, i++) {
		fc_dma_unmap_single(tgtport->dev,
				iod->rspdma, NVME_FC_MAX_LS_BUFFER_SIZE,
				DMA_TO_DEVICE);
		kfree(iod->rqstbuf);
		list_del(&iod->ls_list);
	}
	kfree(tgtport->iod);
}

static struct nvmet_fc_ls_iod *
nvmet_fc_alloc_ls_iod(struct nvmet_fc_tgtport *tgtport)
{
	struct nvmet_fc_ls_iod *iod;
	unsigned long flags;

	spin_lock_irqsave(&tgtport->lock, flags);
	iod = list_first_entry_or_null(&tgtport->ls_list,
					struct nvmet_fc_ls_iod, ls_list);
	if (iod)
		list_move_tail(&iod->ls_list, &tgtport->ls_busylist);
	spin_unlock_irqrestore(&tgtport->lock, flags);
	return iod;
}


static void
nvmet_fc_free_ls_iod(struct nvmet_fc_tgtport *tgtport,
			struct nvmet_fc_ls_iod *iod)
{
	unsigned long flags;

	spin_lock_irqsave(&tgtport->lock, flags);
	list_move(&iod->ls_list, &tgtport->ls_list);
	spin_unlock_irqrestore(&tgtport->lock, flags);
}

static void
nvmet_fc_prep_fcp_iodlist(struct nvmet_fc_tgtport *tgtport,
				struct nvmet_fc_tgt_queue *queue)
{
	struct nvmet_fc_fcp_iod *fod = queue->fod;
	int i;

	for (i = 0; i < queue->sqsize; fod++, i++) {
		INIT_WORK(&fod->defer_work, nvmet_fc_fcp_rqst_op_defer_work);
		fod->tgtport = tgtport;
		fod->queue = queue;
		fod->active = false;
		fod->abort = false;
		fod->aborted = false;
		fod->fcpreq = NULL;
		list_add_tail(&fod->fcp_list, &queue->fod_list);
		spin_lock_init(&fod->flock);

		fod->rspdma = fc_dma_map_single(tgtport->dev, &fod->rspiubuf,
					sizeof(fod->rspiubuf), DMA_TO_DEVICE);
		if (fc_dma_mapping_error(tgtport->dev, fod->rspdma)) {
			list_del(&fod->fcp_list);
			for (fod--, i--; i >= 0; fod--, i--) {
				fc_dma_unmap_single(tgtport->dev, fod->rspdma,
						sizeof(fod->rspiubuf),
						DMA_TO_DEVICE);
				fod->rspdma = 0L;
				list_del(&fod->fcp_list);
			}

			return;
		}
	}
}

static void
nvmet_fc_destroy_fcp_iodlist(struct nvmet_fc_tgtport *tgtport,
				struct nvmet_fc_tgt_queue *queue)
{
	struct nvmet_fc_fcp_iod *fod = queue->fod;
	int i;

	for (i = 0; i < queue->sqsize; fod++, i++) {
		if (fod->rspdma)
			fc_dma_unmap_single(tgtport->dev, fod->rspdma,
				sizeof(fod->rspiubuf), DMA_TO_DEVICE);
	}
}

static struct nvmet_fc_fcp_iod *
nvmet_fc_alloc_fcp_iod(struct nvmet_fc_tgt_queue *queue)
{
	struct nvmet_fc_fcp_iod *fod;

	lockdep_assert_held(&queue->qlock);

	fod = list_first_entry_or_null(&queue->fod_list,
					struct nvmet_fc_fcp_iod, fcp_list);
	if (fod) {
		list_del(&fod->fcp_list);
		fod->active = true;
		/*
		 * no queue reference is taken, as it was taken by the
		 * queue lookup just prior to the allocation. The iod
		 * will "inherit" that reference.
		 */
	}
	return fod;
}


static void
nvmet_fc_queue_fcp_req(struct nvmet_fc_tgtport *tgtport,
		       struct nvmet_fc_tgt_queue *queue,
		       struct nvmefc_tgt_fcp_req *fcpreq)
{
	struct nvmet_fc_fcp_iod *fod = fcpreq->nvmet_fc_private;

	/*
	 * put all admin cmds on hw queue id 0. All io commands go to
	 * the respective hw queue based on a modulo basis
	 */
	fcpreq->hwqid = queue->qid ?
			((queue->qid - 1) % tgtport->ops->max_hw_queues) : 0;

	nvmet_fc_handle_fcp_rqst(tgtport, fod);
}

static void
nvmet_fc_fcp_rqst_op_defer_work(struct work_struct *work)
{
	struct nvmet_fc_fcp_iod *fod =
		container_of(work, struct nvmet_fc_fcp_iod, defer_work);

	/* Submit deferred IO for processing */
	nvmet_fc_queue_fcp_req(fod->tgtport, fod->queue, fod->fcpreq);

}

static void
nvmet_fc_free_fcp_iod(struct nvmet_fc_tgt_queue *queue,
			struct nvmet_fc_fcp_iod *fod)
{
	struct nvmefc_tgt_fcp_req *fcpreq = fod->fcpreq;
	struct nvmet_fc_tgtport *tgtport = fod->tgtport;
	struct nvmet_fc_defer_fcp_req *deferfcp;
	unsigned long flags;

	fc_dma_sync_single_for_cpu(tgtport->dev, fod->rspdma,
				sizeof(fod->rspiubuf), DMA_TO_DEVICE);

	fcpreq->nvmet_fc_private = NULL;

	fod->active = false;
	fod->abort = false;
	fod->aborted = false;
	fod->writedataactive = false;
	fod->fcpreq = NULL;

	tgtport->ops->fcp_req_release(&tgtport->fc_target_port, fcpreq);

	/* release the queue lookup reference on the completed IO */
	nvmet_fc_tgt_q_put(queue);

	spin_lock_irqsave(&queue->qlock, flags);
	deferfcp = list_first_entry_or_null(&queue->pending_cmd_list,
				struct nvmet_fc_defer_fcp_req, req_list);
	if (!deferfcp) {
		list_add_tail(&fod->fcp_list, &fod->queue->fod_list);
		spin_unlock_irqrestore(&queue->qlock, flags);
		return;
	}

	/* Re-use the fod for the next pending cmd that was deferred */
	list_del(&deferfcp->req_list);

	fcpreq = deferfcp->fcp_req;

	/* deferfcp can be reused for another IO at a later date */
	list_add_tail(&deferfcp->req_list, &queue->avail_defer_list);

	spin_unlock_irqrestore(&queue->qlock, flags);

	/* Save NVME CMD IO in fod */
	memcpy(&fod->cmdiubuf, fcpreq->rspaddr, fcpreq->rsplen);

	/* Setup new fcpreq to be processed */
	fcpreq->rspaddr = NULL;
	fcpreq->rsplen  = 0;
	fcpreq->nvmet_fc_private = fod;
	fod->fcpreq = fcpreq;
	fod->active = true;

	/* inform LLDD IO is now being processed */
	tgtport->ops->defer_rcv(&tgtport->fc_target_port, fcpreq);

	/*
	 * Leave the queue lookup get reference taken when
	 * fod was originally allocated.
	 */

	queue_work(queue->work_q, &fod->defer_work);
}

static struct nvmet_fc_tgt_queue *
nvmet_fc_alloc_target_queue(struct nvmet_fc_tgt_assoc *assoc,
			u16 qid, u16 sqsize)
{
	struct nvmet_fc_tgt_queue *queue;
	unsigned long flags;
	int ret;

	if (qid > NVMET_NR_QUEUES)
		return NULL;

	queue = kzalloc(struct_size(queue, fod, sqsize), GFP_KERNEL);
	if (!queue)
		return NULL;

	if (!nvmet_fc_tgt_a_get(assoc))
		goto out_free_queue;

	queue->work_q = alloc_workqueue("ntfc%d.%d.%d", 0, 0,
				assoc->tgtport->fc_target_port.port_num,
				assoc->a_id, qid);
	if (!queue->work_q)
		goto out_a_put;

	queue->qid = qid;
	queue->sqsize = sqsize;
	queue->assoc = assoc;
	INIT_LIST_HEAD(&queue->fod_list);
	INIT_LIST_HEAD(&queue->avail_defer_list);
	INIT_LIST_HEAD(&queue->pending_cmd_list);
	atomic_set(&queue->connected, 0);
	atomic_set(&queue->sqtail, 0);
	atomic_set(&queue->rsn, 1);
	atomic_set(&queue->zrspcnt, 0);
	spin_lock_init(&queue->qlock);
	kref_init(&queue->ref);

	nvmet_fc_prep_fcp_iodlist(assoc->tgtport, queue);

	ret = nvmet_sq_init(&queue->nvme_sq);
	if (ret)
		goto out_fail_iodlist;

	WARN_ON(assoc->queues[qid]);
	spin_lock_irqsave(&assoc->tgtport->lock, flags);
	assoc->queues[qid] = queue;
	spin_unlock_irqrestore(&assoc->tgtport->lock, flags);

	return queue;

out_fail_iodlist:
	nvmet_fc_destroy_fcp_iodlist(assoc->tgtport, queue);
	destroy_workqueue(queue->work_q);
out_a_put:
	nvmet_fc_tgt_a_put(assoc);
out_free_queue:
	kfree(queue);
	return NULL;
}


static void
nvmet_fc_tgt_queue_free(struct kref *ref)
{
	struct nvmet_fc_tgt_queue *queue =
		container_of(ref, struct nvmet_fc_tgt_queue, ref);
	unsigned long flags;

	spin_lock_irqsave(&queue->assoc->tgtport->lock, flags);
	queue->assoc->queues[queue->qid] = NULL;
	spin_unlock_irqrestore(&queue->assoc->tgtport->lock, flags);

	nvmet_fc_destroy_fcp_iodlist(queue->assoc->tgtport, queue);

	nvmet_fc_tgt_a_put(queue->assoc);

	destroy_workqueue(queue->work_q);

	kfree(queue);
}

static void
nvmet_fc_tgt_q_put(struct nvmet_fc_tgt_queue *queue)
{
	kref_put(&queue->ref, nvmet_fc_tgt_queue_free);
}

static int
nvmet_fc_tgt_q_get(struct nvmet_fc_tgt_queue *queue)
{
	return kref_get_unless_zero(&queue->ref);
}


static void
nvmet_fc_delete_target_queue(struct nvmet_fc_tgt_queue *queue)
{
	struct nvmet_fc_tgtport *tgtport = queue->assoc->tgtport;
	struct nvmet_fc_fcp_iod *fod = queue->fod;
	struct nvmet_fc_defer_fcp_req *deferfcp, *tempptr;
	unsigned long flags;
	int i, writedataactive;
	bool disconnect;

	disconnect = atomic_xchg(&queue->connected, 0);

	spin_lock_irqsave(&queue->qlock, flags);
	/* about outstanding io's */
	for (i = 0; i < queue->sqsize; fod++, i++) {
		if (fod->active) {
			spin_lock(&fod->flock);
			fod->abort = true;
			writedataactive = fod->writedataactive;
			spin_unlock(&fod->flock);
			/*
			 * only call lldd abort routine if waiting for
			 * writedata. other outstanding ops should finish
			 * on their own.
			 */
			if (writedataactive) {
				spin_lock(&fod->flock);
				fod->aborted = true;
				spin_unlock(&fod->flock);
				tgtport->ops->fcp_abort(
					&tgtport->fc_target_port, fod->fcpreq);
			}
		}
	}

	/* Cleanup defer'ed IOs in queue */
	list_for_each_entry_safe(deferfcp, tempptr, &queue->avail_defer_list,
				req_list) {
		list_del(&deferfcp->req_list);
		kfree(deferfcp);
	}

	for (;;) {
		deferfcp = list_first_entry_or_null(&queue->pending_cmd_list,
				struct nvmet_fc_defer_fcp_req, req_list);
		if (!deferfcp)
			break;

		list_del(&deferfcp->req_list);
		spin_unlock_irqrestore(&queue->qlock, flags);

		tgtport->ops->defer_rcv(&tgtport->fc_target_port,
				deferfcp->fcp_req);

		tgtport->ops->fcp_abort(&tgtport->fc_target_port,
				deferfcp->fcp_req);

		tgtport->ops->fcp_req_release(&tgtport->fc_target_port,
				deferfcp->fcp_req);

		/* release the queue lookup reference */
		nvmet_fc_tgt_q_put(queue);

		kfree(deferfcp);

		spin_lock_irqsave(&queue->qlock, flags);
	}
	spin_unlock_irqrestore(&queue->qlock, flags);

	flush_workqueue(queue->work_q);

	if (disconnect)
		nvmet_sq_destroy(&queue->nvme_sq);

	nvmet_fc_tgt_q_put(queue);
}

static struct nvmet_fc_tgt_queue *
nvmet_fc_find_target_queue(struct nvmet_fc_tgtport *tgtport,
				u64 connection_id)
{
	struct nvmet_fc_tgt_assoc *assoc;
	struct nvmet_fc_tgt_queue *queue;
	u64 association_id = nvmet_fc_getassociationid(connection_id);
	u16 qid = nvmet_fc_getqueueid(connection_id);
	unsigned long flags;

	if (qid > NVMET_NR_QUEUES)
		return NULL;

	spin_lock_irqsave(&tgtport->lock, flags);
	list_for_each_entry(assoc, &tgtport->assoc_list, a_list) {
		if (association_id == assoc->association_id) {
			queue = assoc->queues[qid];
			if (queue &&
			    (!atomic_read(&queue->connected) ||
			     !nvmet_fc_tgt_q_get(queue)))
				queue = NULL;
			spin_unlock_irqrestore(&tgtport->lock, flags);
			return queue;
		}
	}
	spin_unlock_irqrestore(&tgtport->lock, flags);
	return NULL;
}

static void
nvmet_fc_delete_assoc(struct work_struct *work)
{
	struct nvmet_fc_tgt_assoc *assoc =
		container_of(work, struct nvmet_fc_tgt_assoc, del_work);

	nvmet_fc_delete_target_assoc(assoc);
	nvmet_fc_tgt_a_put(assoc);
}

static struct nvmet_fc_tgt_assoc *
nvmet_fc_alloc_target_assoc(struct nvmet_fc_tgtport *tgtport)
{
	struct nvmet_fc_tgt_assoc *assoc, *tmpassoc;
	unsigned long flags;
	u64 ran;
	int idx;
	bool needrandom = true;

	assoc = kzalloc(sizeof(*assoc), GFP_KERNEL);
	if (!assoc)
		return NULL;

	idx = ida_simple_get(&tgtport->assoc_cnt, 0, 0, GFP_KERNEL);
	if (idx < 0)
		goto out_free_assoc;

	if (!nvmet_fc_tgtport_get(tgtport))
		goto out_ida_put;

	assoc->tgtport = tgtport;
	assoc->a_id = idx;
	INIT_LIST_HEAD(&assoc->a_list);
	kref_init(&assoc->ref);
	INIT_WORK(&assoc->del_work, nvmet_fc_delete_assoc);

	while (needrandom) {
		get_random_bytes(&ran, sizeof(ran) - BYTES_FOR_QID);
		ran = ran << BYTES_FOR_QID_SHIFT;

		spin_lock_irqsave(&tgtport->lock, flags);
		needrandom = false;
		list_for_each_entry(tmpassoc, &tgtport->assoc_list, a_list)
			if (ran == tmpassoc->association_id) {
				needrandom = true;
				break;
			}
		if (!needrandom) {
			assoc->association_id = ran;
			list_add_tail(&assoc->a_list, &tgtport->assoc_list);
		}
		spin_unlock_irqrestore(&tgtport->lock, flags);
	}

	return assoc;

out_ida_put:
	ida_simple_remove(&tgtport->assoc_cnt, idx);
out_free_assoc:
	kfree(assoc);
	return NULL;
}

static void
nvmet_fc_target_assoc_free(struct kref *ref)
{
	struct nvmet_fc_tgt_assoc *assoc =
		container_of(ref, struct nvmet_fc_tgt_assoc, ref);
	struct nvmet_fc_tgtport *tgtport = assoc->tgtport;
	unsigned long flags;

	spin_lock_irqsave(&tgtport->lock, flags);
	list_del(&assoc->a_list);
	spin_unlock_irqrestore(&tgtport->lock, flags);
	ida_simple_remove(&tgtport->assoc_cnt, assoc->a_id);
	kfree(assoc);
	nvmet_fc_tgtport_put(tgtport);
}

static void
nvmet_fc_tgt_a_put(struct nvmet_fc_tgt_assoc *assoc)
{
	kref_put(&assoc->ref, nvmet_fc_target_assoc_free);
}

static int
nvmet_fc_tgt_a_get(struct nvmet_fc_tgt_assoc *assoc)
{
	return kref_get_unless_zero(&assoc->ref);
}

static void
nvmet_fc_delete_target_assoc(struct nvmet_fc_tgt_assoc *assoc)
{
	struct nvmet_fc_tgtport *tgtport = assoc->tgtport;
	struct nvmet_fc_tgt_queue *queue;
	unsigned long flags;
	int i;

	spin_lock_irqsave(&tgtport->lock, flags);
	for (i = NVMET_NR_QUEUES; i >= 0; i--) {
		queue = assoc->queues[i];
		if (queue) {
			if (!nvmet_fc_tgt_q_get(queue))
				continue;
			spin_unlock_irqrestore(&tgtport->lock, flags);
			nvmet_fc_delete_target_queue(queue);
			nvmet_fc_tgt_q_put(queue);
			spin_lock_irqsave(&tgtport->lock, flags);
		}
	}
	spin_unlock_irqrestore(&tgtport->lock, flags);

	nvmet_fc_tgt_a_put(assoc);
}

static struct nvmet_fc_tgt_assoc *
nvmet_fc_find_target_assoc(struct nvmet_fc_tgtport *tgtport,
				u64 association_id)
{
	struct nvmet_fc_tgt_assoc *assoc;
	struct nvmet_fc_tgt_assoc *ret = NULL;
	unsigned long flags;

	spin_lock_irqsave(&tgtport->lock, flags);
	list_for_each_entry(assoc, &tgtport->assoc_list, a_list) {
		if (association_id == assoc->association_id) {
			ret = assoc;
			nvmet_fc_tgt_a_get(assoc);
			break;
		}
	}
	spin_unlock_irqrestore(&tgtport->lock, flags);

	return ret;
}

static void
nvmet_fc_portentry_bind(struct nvmet_fc_tgtport *tgtport,
			struct nvmet_fc_port_entry *pe,
			struct nvmet_port *port)
{
	lockdep_assert_held(&nvmet_fc_tgtlock);

	pe->tgtport = tgtport;
	tgtport->pe = pe;

	pe->port = port;
	port->priv = pe;

	pe->node_name = tgtport->fc_target_port.node_name;
	pe->port_name = tgtport->fc_target_port.port_name;
	INIT_LIST_HEAD(&pe->pe_list);

	list_add_tail(&pe->pe_list, &nvmet_fc_portentry_list);
}

static void
nvmet_fc_portentry_unbind(struct nvmet_fc_port_entry *pe)
{
	unsigned long flags;

	spin_lock_irqsave(&nvmet_fc_tgtlock, flags);
	if (pe->tgtport)
		pe->tgtport->pe = NULL;
	list_del(&pe->pe_list);
	spin_unlock_irqrestore(&nvmet_fc_tgtlock, flags);
}

/*
 * called when a targetport deregisters. Breaks the relationship
 * with the nvmet port, but leaves the port_entry in place so that
 * re-registration can resume operation.
 */
static void
nvmet_fc_portentry_unbind_tgt(struct nvmet_fc_tgtport *tgtport)
{
	struct nvmet_fc_port_entry *pe;
	unsigned long flags;

	spin_lock_irqsave(&nvmet_fc_tgtlock, flags);
	pe = tgtport->pe;
	if (pe)
		pe->tgtport = NULL;
	tgtport->pe = NULL;
	spin_unlock_irqrestore(&nvmet_fc_tgtlock, flags);
}

/*
 * called when a new targetport is registered. Looks in the
 * existing nvmet port_entries to see if the nvmet layer is
 * configured for the targetport's wwn's. (the targetport existed,
 * nvmet configured, the lldd unregistered the tgtport, and is now
 * reregistering the same targetport).  If so, set the nvmet port
 * port entry on the targetport.
 */
static void
nvmet_fc_portentry_rebind_tgt(struct nvmet_fc_tgtport *tgtport)
{
	struct nvmet_fc_port_entry *pe;
	unsigned long flags;

	spin_lock_irqsave(&nvmet_fc_tgtlock, flags);
	list_for_each_entry(pe, &nvmet_fc_portentry_list, pe_list) {
		if (tgtport->fc_target_port.node_name == pe->node_name &&
		    tgtport->fc_target_port.port_name == pe->port_name) {
			WARN_ON(pe->tgtport);
			tgtport->pe = pe;
			pe->tgtport = tgtport;
			break;
		}
	}
	spin_unlock_irqrestore(&nvmet_fc_tgtlock, flags);
}

/**
 * nvme_fc_register_targetport - transport entry point called by an
 *                              LLDD to register the existence of a local
 *                              NVME subystem FC port.
 * @pinfo:     pointer to information about the port to be registered
 * @template:  LLDD entrypoints and operational parameters for the port
 * @dev:       physical hardware device node port corresponds to. Will be
 *             used for DMA mappings
 * @portptr:   pointer to a local port pointer. Upon success, the routine
 *             will allocate a nvme_fc_local_port structure and place its
 *             address in the local port pointer. Upon failure, local port
 *             pointer will be set to NULL.
 *
 * Returns:
 * a completion status. Must be 0 upon success; a negative errno
 * (ex: -ENXIO) upon failure.
 */
int
nvmet_fc_register_targetport(struct nvmet_fc_port_info *pinfo,
			struct nvmet_fc_target_template *template,
			struct device *dev,
			struct nvmet_fc_target_port **portptr)
{
	struct nvmet_fc_tgtport *newrec;
	unsigned long flags;
	int ret, idx;

	if (!template->xmt_ls_rsp || !template->fcp_op ||
	    !template->fcp_abort ||
	    !template->fcp_req_release || !template->targetport_delete ||
	    !template->max_hw_queues || !template->max_sgl_segments ||
	    !template->max_dif_sgl_segments || !template->dma_boundary) {
		ret = -EINVAL;
		goto out_regtgt_failed;
	}

	newrec = kzalloc((sizeof(*newrec) + template->target_priv_sz),
			 GFP_KERNEL);
	if (!newrec) {
		ret = -ENOMEM;
		goto out_regtgt_failed;
	}

	idx = ida_simple_get(&nvmet_fc_tgtport_cnt, 0, 0, GFP_KERNEL);
	if (idx < 0) {
		ret = -ENOSPC;
		goto out_fail_kfree;
	}

	if (!get_device(dev) && dev) {
		ret = -ENODEV;
		goto out_ida_put;
	}

	newrec->fc_target_port.node_name = pinfo->node_name;
	newrec->fc_target_port.port_name = pinfo->port_name;
	newrec->fc_target_port.private = &newrec[1];
	newrec->fc_target_port.port_id = pinfo->port_id;
	newrec->fc_target_port.port_num = idx;
	INIT_LIST_HEAD(&newrec->tgt_list);
	newrec->dev = dev;
	newrec->ops = template;
	spin_lock_init(&newrec->lock);
	INIT_LIST_HEAD(&newrec->ls_list);
	INIT_LIST_HEAD(&newrec->ls_busylist);
	INIT_LIST_HEAD(&newrec->assoc_list);
	kref_init(&newrec->ref);
	ida_init(&newrec->assoc_cnt);
	newrec->max_sg_cnt = template->max_sgl_segments;

	ret = nvmet_fc_alloc_ls_iodlist(newrec);
	if (ret) {
		ret = -ENOMEM;
		goto out_free_newrec;
	}

	nvmet_fc_portentry_rebind_tgt(newrec);

	spin_lock_irqsave(&nvmet_fc_tgtlock, flags);
	list_add_tail(&newrec->tgt_list, &nvmet_fc_target_list);
	spin_unlock_irqrestore(&nvmet_fc_tgtlock, flags);

	*portptr = &newrec->fc_target_port;
	return 0;

out_free_newrec:
	put_device(dev);
out_ida_put:
	ida_simple_remove(&nvmet_fc_tgtport_cnt, idx);
out_fail_kfree:
	kfree(newrec);
out_regtgt_failed:
	*portptr = NULL;
	return ret;
}
EXPORT_SYMBOL_GPL(nvmet_fc_register_targetport);


static void
nvmet_fc_free_tgtport(struct kref *ref)
{
	struct nvmet_fc_tgtport *tgtport =
		container_of(ref, struct nvmet_fc_tgtport, ref);
	struct device *dev = tgtport->dev;
	unsigned long flags;

	spin_lock_irqsave(&nvmet_fc_tgtlock, flags);
	list_del(&tgtport->tgt_list);
	spin_unlock_irqrestore(&nvmet_fc_tgtlock, flags);

	nvmet_fc_free_ls_iodlist(tgtport);

	/* let the LLDD know we've finished tearing it down */
	tgtport->ops->targetport_delete(&tgtport->fc_target_port);

	ida_simple_remove(&nvmet_fc_tgtport_cnt,
			tgtport->fc_target_port.port_num);

	ida_destroy(&tgtport->assoc_cnt);

	kfree(tgtport);

	put_device(dev);
}

static void
nvmet_fc_tgtport_put(struct nvmet_fc_tgtport *tgtport)
{
	kref_put(&tgtport->ref, nvmet_fc_free_tgtport);
}

static int
nvmet_fc_tgtport_get(struct nvmet_fc_tgtport *tgtport)
{
	return kref_get_unless_zero(&tgtport->ref);
}

static void
__nvmet_fc_free_assocs(struct nvmet_fc_tgtport *tgtport)
{
	struct nvmet_fc_tgt_assoc *assoc, *next;
	unsigned long flags;

	spin_lock_irqsave(&tgtport->lock, flags);
	list_for_each_entry_safe(assoc, next,
				&tgtport->assoc_list, a_list) {
		if (!nvmet_fc_tgt_a_get(assoc))
			continue;
		if (!schedule_work(&assoc->del_work))
			nvmet_fc_tgt_a_put(assoc);
	}
	spin_unlock_irqrestore(&tgtport->lock, flags);
}

/*
 * nvmet layer has called to terminate an association
 */
static void
nvmet_fc_delete_ctrl(struct nvmet_ctrl *ctrl)
{
	struct nvmet_fc_tgtport *tgtport, *next;
	struct nvmet_fc_tgt_assoc *assoc;
	struct nvmet_fc_tgt_queue *queue;
	unsigned long flags;
	bool found_ctrl = false;

	/* this is a bit ugly, but don't want to make locks layered */
	spin_lock_irqsave(&nvmet_fc_tgtlock, flags);
	list_for_each_entry_safe(tgtport, next, &nvmet_fc_target_list,
			tgt_list) {
		if (!nvmet_fc_tgtport_get(tgtport))
			continue;
		spin_unlock_irqrestore(&nvmet_fc_tgtlock, flags);

		spin_lock_irqsave(&tgtport->lock, flags);
		list_for_each_entry(assoc, &tgtport->assoc_list, a_list) {
			queue = assoc->queues[0];
			if (queue && queue->nvme_sq.ctrl == ctrl) {
				if (nvmet_fc_tgt_a_get(assoc))
					found_ctrl = true;
				break;
			}
		}
		spin_unlock_irqrestore(&tgtport->lock, flags);

		nvmet_fc_tgtport_put(tgtport);

		if (found_ctrl) {
			if (!schedule_work(&assoc->del_work))
				nvmet_fc_tgt_a_put(assoc);
			return;
		}

		spin_lock_irqsave(&nvmet_fc_tgtlock, flags);
	}
	spin_unlock_irqrestore(&nvmet_fc_tgtlock, flags);
}

/**
 * nvme_fc_unregister_targetport - transport entry point called by an
 *                              LLDD to deregister/remove a previously
 *                              registered a local NVME subsystem FC port.
 * @target_port: pointer to the (registered) target port that is to be
 *               deregistered.
 *
 * Returns:
 * a completion status. Must be 0 upon success; a negative errno
 * (ex: -ENXIO) upon failure.
 */
int
nvmet_fc_unregister_targetport(struct nvmet_fc_target_port *target_port)
{
	struct nvmet_fc_tgtport *tgtport = targetport_to_tgtport(target_port);

	nvmet_fc_portentry_unbind_tgt(tgtport);

	/* terminate any outstanding associations */
	__nvmet_fc_free_assocs(tgtport);

	nvmet_fc_tgtport_put(tgtport);

	return 0;
}
EXPORT_SYMBOL_GPL(nvmet_fc_unregister_targetport);


/* *********************** FC-NVME LS Handling **************************** */


static void
nvmet_fc_format_rsp_hdr(void *buf, u8 ls_cmd, __be32 desc_len, u8 rqst_ls_cmd)
{
	struct fcnvme_ls_acc_hdr *acc = buf;

	acc->w0.ls_cmd = ls_cmd;
	acc->desc_list_len = desc_len;
	acc->rqst.desc_tag = cpu_to_be32(FCNVME_LSDESC_RQST);
	acc->rqst.desc_len =
			fcnvme_lsdesc_len(sizeof(struct fcnvme_lsdesc_rqst));
	acc->rqst.w0.ls_cmd = rqst_ls_cmd;
}

static int
nvmet_fc_format_rjt(void *buf, u16 buflen, u8 ls_cmd,
			u8 reason, u8 explanation, u8 vendor)
{
	struct fcnvme_ls_rjt *rjt = buf;

	nvmet_fc_format_rsp_hdr(buf, FCNVME_LSDESC_RQST,
			fcnvme_lsdesc_len(sizeof(struct fcnvme_ls_rjt)),
			ls_cmd);
	rjt->rjt.desc_tag = cpu_to_be32(FCNVME_LSDESC_RJT);
	rjt->rjt.desc_len = fcnvme_lsdesc_len(sizeof(struct fcnvme_lsdesc_rjt));
	rjt->rjt.reason_code = reason;
	rjt->rjt.reason_explanation = explanation;
	rjt->rjt.vendor = vendor;

	return sizeof(struct fcnvme_ls_rjt);
}

/* Validation Error indexes into the string table below */
enum {
	VERR_NO_ERROR		= 0,
	VERR_CR_ASSOC_LEN	= 1,
	VERR_CR_ASSOC_RQST_LEN	= 2,
	VERR_CR_ASSOC_CMD	= 3,
	VERR_CR_ASSOC_CMD_LEN	= 4,
	VERR_ERSP_RATIO		= 5,
	VERR_ASSOC_ALLOC_FAIL	= 6,
	VERR_QUEUE_ALLOC_FAIL	= 7,
	VERR_CR_CONN_LEN	= 8,
	VERR_CR_CONN_RQST_LEN	= 9,
	VERR_ASSOC_ID		= 10,
	VERR_ASSOC_ID_LEN	= 11,
	VERR_NO_ASSOC		= 12,
	VERR_CONN_ID		= 13,
	VERR_CONN_ID_LEN	= 14,
	VERR_NO_CONN		= 15,
	VERR_CR_CONN_CMD	= 16,
	VERR_CR_CONN_CMD_LEN	= 17,
	VERR_DISCONN_LEN	= 18,
	VERR_DISCONN_RQST_LEN	= 19,
	VERR_DISCONN_CMD	= 20,
	VERR_DISCONN_CMD_LEN	= 21,
	VERR_DISCONN_SCOPE	= 22,
	VERR_RS_LEN		= 23,
	VERR_RS_RQST_LEN	= 24,
	VERR_RS_CMD		= 25,
	VERR_RS_CMD_LEN		= 26,
	VERR_RS_RCTL		= 27,
	VERR_RS_RO		= 28,
};

static char *validation_errors[] = {
	"OK",
	"Bad CR_ASSOC Length",
	"Bad CR_ASSOC Rqst Length",
	"Not CR_ASSOC Cmd",
	"Bad CR_ASSOC Cmd Length",
	"Bad Ersp Ratio",
	"Association Allocation Failed",
	"Queue Allocation Failed",
	"Bad CR_CONN Length",
	"Bad CR_CONN Rqst Length",
	"Not Association ID",
	"Bad Association ID Length",
	"No Association",
	"Not Connection ID",
	"Bad Connection ID Length",
	"No Connection",
	"Not CR_CONN Cmd",
	"Bad CR_CONN Cmd Length",
	"Bad DISCONN Length",
	"Bad DISCONN Rqst Length",
	"Not DISCONN Cmd",
	"Bad DISCONN Cmd Length",
	"Bad Disconnect Scope",
	"Bad RS Length",
	"Bad RS Rqst Length",
	"Not RS Cmd",
	"Bad RS Cmd Length",
	"Bad RS R_CTL",
	"Bad RS Relative Offset",
};

static void
nvmet_fc_ls_create_association(struct nvmet_fc_tgtport *tgtport,
			struct nvmet_fc_ls_iod *iod)
{
	struct fcnvme_ls_cr_assoc_rqst *rqst =
				(struct fcnvme_ls_cr_assoc_rqst *)iod->rqstbuf;
	struct fcnvme_ls_cr_assoc_acc *acc =
				(struct fcnvme_ls_cr_assoc_acc *)iod->rspbuf;
	struct nvmet_fc_tgt_queue *queue;
	int ret = 0;

	memset(acc, 0, sizeof(*acc));

	/*
	 * FC-NVME spec changes. There are initiators sending different
	 * lengths as padding sizes for Create Association Cmd descriptor
	 * was incorrect.
	 * Accept anything of "minimum" length. Assume format per 1.15
	 * spec (with HOSTID reduced to 16 bytes), ignore how long the
	 * trailing pad length is.
	 */
	if (iod->rqstdatalen < FCNVME_LSDESC_CRA_RQST_MINLEN)
		ret = VERR_CR_ASSOC_LEN;
	else if (be32_to_cpu(rqst->desc_list_len) <
			FCNVME_LSDESC_CRA_RQST_MIN_LISTLEN)
		ret = VERR_CR_ASSOC_RQST_LEN;
	else if (rqst->assoc_cmd.desc_tag !=
			cpu_to_be32(FCNVME_LSDESC_CREATE_ASSOC_CMD))
		ret = VERR_CR_ASSOC_CMD;
	else if (be32_to_cpu(rqst->assoc_cmd.desc_len) <
			FCNVME_LSDESC_CRA_CMD_DESC_MIN_DESCLEN)
		ret = VERR_CR_ASSOC_CMD_LEN;
	else if (!rqst->assoc_cmd.ersp_ratio ||
		 (be16_to_cpu(rqst->assoc_cmd.ersp_ratio) >=
				be16_to_cpu(rqst->assoc_cmd.sqsize)))
		ret = VERR_ERSP_RATIO;

	else {
		/* new association w/ admin queue */
		iod->assoc = nvmet_fc_alloc_target_assoc(tgtport);
		if (!iod->assoc)
			ret = VERR_ASSOC_ALLOC_FAIL;
		else {
			queue = nvmet_fc_alloc_target_queue(iod->assoc, 0,
					be16_to_cpu(rqst->assoc_cmd.sqsize));
			if (!queue)
				ret = VERR_QUEUE_ALLOC_FAIL;
		}
	}

	if (ret) {
		dev_err(tgtport->dev,
			"Create Association LS failed: %s\n",
			validation_errors[ret]);
		iod->lsreq->rsplen = nvmet_fc_format_rjt(acc,
				NVME_FC_MAX_LS_BUFFER_SIZE, rqst->w0.ls_cmd,
				FCNVME_RJT_RC_LOGIC,
				FCNVME_RJT_EXP_NONE, 0);
		return;
	}

	queue->ersp_ratio = be16_to_cpu(rqst->assoc_cmd.ersp_ratio);
	atomic_set(&queue->connected, 1);
	queue->sqhd = 0;	/* best place to init value */

	/* format a response */

	iod->lsreq->rsplen = sizeof(*acc);

	nvmet_fc_format_rsp_hdr(acc, FCNVME_LS_ACC,
			fcnvme_lsdesc_len(
				sizeof(struct fcnvme_ls_cr_assoc_acc)),
			FCNVME_LS_CREATE_ASSOCIATION);
	acc->associd.desc_tag = cpu_to_be32(FCNVME_LSDESC_ASSOC_ID);
	acc->associd.desc_len =
			fcnvme_lsdesc_len(
				sizeof(struct fcnvme_lsdesc_assoc_id));
	acc->associd.association_id =
			cpu_to_be64(nvmet_fc_makeconnid(iod->assoc, 0));
	acc->connectid.desc_tag = cpu_to_be32(FCNVME_LSDESC_CONN_ID);
	acc->connectid.desc_len =
			fcnvme_lsdesc_len(
				sizeof(struct fcnvme_lsdesc_conn_id));
	acc->connectid.connection_id = acc->associd.association_id;
}

static void
nvmet_fc_ls_create_connection(struct nvmet_fc_tgtport *tgtport,
			struct nvmet_fc_ls_iod *iod)
{
	struct fcnvme_ls_cr_conn_rqst *rqst =
				(struct fcnvme_ls_cr_conn_rqst *)iod->rqstbuf;
	struct fcnvme_ls_cr_conn_acc *acc =
				(struct fcnvme_ls_cr_conn_acc *)iod->rspbuf;
	struct nvmet_fc_tgt_queue *queue;
	int ret = 0;

	memset(acc, 0, sizeof(*acc));

	if (iod->rqstdatalen < sizeof(struct fcnvme_ls_cr_conn_rqst))
		ret = VERR_CR_CONN_LEN;
	else if (rqst->desc_list_len !=
			fcnvme_lsdesc_len(
				sizeof(struct fcnvme_ls_cr_conn_rqst)))
		ret = VERR_CR_CONN_RQST_LEN;
	else if (rqst->associd.desc_tag != cpu_to_be32(FCNVME_LSDESC_ASSOC_ID))
		ret = VERR_ASSOC_ID;
	else if (rqst->associd.desc_len !=
			fcnvme_lsdesc_len(
				sizeof(struct fcnvme_lsdesc_assoc_id)))
		ret = VERR_ASSOC_ID_LEN;
	else if (rqst->connect_cmd.desc_tag !=
			cpu_to_be32(FCNVME_LSDESC_CREATE_CONN_CMD))
		ret = VERR_CR_CONN_CMD;
	else if (rqst->connect_cmd.desc_len !=
			fcnvme_lsdesc_len(
				sizeof(struct fcnvme_lsdesc_cr_conn_cmd)))
		ret = VERR_CR_CONN_CMD_LEN;
	else if (!rqst->connect_cmd.ersp_ratio ||
		 (be16_to_cpu(rqst->connect_cmd.ersp_ratio) >=
				be16_to_cpu(rqst->connect_cmd.sqsize)))
		ret = VERR_ERSP_RATIO;

	else {
		/* new io queue */
		iod->assoc = nvmet_fc_find_target_assoc(tgtport,
				be64_to_cpu(rqst->associd.association_id));
		if (!iod->assoc)
			ret = VERR_NO_ASSOC;
		else {
			queue = nvmet_fc_alloc_target_queue(iod->assoc,
					be16_to_cpu(rqst->connect_cmd.qid),
					be16_to_cpu(rqst->connect_cmd.sqsize));
			if (!queue)
				ret = VERR_QUEUE_ALLOC_FAIL;

			/* release get taken in nvmet_fc_find_target_assoc */
			nvmet_fc_tgt_a_put(iod->assoc);
		}
	}

	if (ret) {
		dev_err(tgtport->dev,
			"Create Connection LS failed: %s\n",
			validation_errors[ret]);
		iod->lsreq->rsplen = nvmet_fc_format_rjt(acc,
				NVME_FC_MAX_LS_BUFFER_SIZE, rqst->w0.ls_cmd,
				(ret == VERR_NO_ASSOC) ?
					FCNVME_RJT_RC_INV_ASSOC :
					FCNVME_RJT_RC_LOGIC,
				FCNVME_RJT_EXP_NONE, 0);
		return;
	}

	queue->ersp_ratio = be16_to_cpu(rqst->connect_cmd.ersp_ratio);
	atomic_set(&queue->connected, 1);
	queue->sqhd = 0;	/* best place to init value */

	/* format a response */

	iod->lsreq->rsplen = sizeof(*acc);

	nvmet_fc_format_rsp_hdr(acc, FCNVME_LS_ACC,
			fcnvme_lsdesc_len(sizeof(struct fcnvme_ls_cr_conn_acc)),
			FCNVME_LS_CREATE_CONNECTION);
	acc->connectid.desc_tag = cpu_to_be32(FCNVME_LSDESC_CONN_ID);
	acc->connectid.desc_len =
			fcnvme_lsdesc_len(
				sizeof(struct fcnvme_lsdesc_conn_id));
	acc->connectid.connection_id =
			cpu_to_be64(nvmet_fc_makeconnid(iod->assoc,
				be16_to_cpu(rqst->connect_cmd.qid)));
}

static void
nvmet_fc_ls_disconnect(struct nvmet_fc_tgtport *tgtport,
			struct nvmet_fc_ls_iod *iod)
{
	struct fcnvme_ls_disconnect_rqst *rqst =
			(struct fcnvme_ls_disconnect_rqst *)iod->rqstbuf;
	struct fcnvme_ls_disconnect_acc *acc =
			(struct fcnvme_ls_disconnect_acc *)iod->rspbuf;
	struct nvmet_fc_tgt_assoc *assoc;
	int ret = 0;

	memset(acc, 0, sizeof(*acc));

	if (iod->rqstdatalen < sizeof(struct fcnvme_ls_disconnect_rqst))
		ret = VERR_DISCONN_LEN;
	else if (rqst->desc_list_len !=
			fcnvme_lsdesc_len(
				sizeof(struct fcnvme_ls_disconnect_rqst)))
		ret = VERR_DISCONN_RQST_LEN;
	else if (rqst->associd.desc_tag != cpu_to_be32(FCNVME_LSDESC_ASSOC_ID))
		ret = VERR_ASSOC_ID;
	else if (rqst->associd.desc_len !=
			fcnvme_lsdesc_len(
				sizeof(struct fcnvme_lsdesc_assoc_id)))
		ret = VERR_ASSOC_ID_LEN;
	else if (rqst->discon_cmd.desc_tag !=
			cpu_to_be32(FCNVME_LSDESC_DISCONN_CMD))
		ret = VERR_DISCONN_CMD;
	else if (rqst->discon_cmd.desc_len !=
			fcnvme_lsdesc_len(
				sizeof(struct fcnvme_lsdesc_disconn_cmd)))
		ret = VERR_DISCONN_CMD_LEN;
	else if ((rqst->discon_cmd.scope != FCNVME_DISCONN_ASSOCIATION) &&
			(rqst->discon_cmd.scope != FCNVME_DISCONN_CONNECTION))
		ret = VERR_DISCONN_SCOPE;
	else {
		/* match an active association */
		assoc = nvmet_fc_find_target_assoc(tgtport,
				be64_to_cpu(rqst->associd.association_id));
		iod->assoc = assoc;
		if (!assoc)
			ret = VERR_NO_ASSOC;
	}

	if (ret) {
		dev_err(tgtport->dev,
			"Disconnect LS failed: %s\n",
			validation_errors[ret]);
		iod->lsreq->rsplen = nvmet_fc_format_rjt(acc,
				NVME_FC_MAX_LS_BUFFER_SIZE, rqst->w0.ls_cmd,
				(ret == VERR_NO_ASSOC) ?
					FCNVME_RJT_RC_INV_ASSOC :
					(ret == VERR_NO_CONN) ?
						FCNVME_RJT_RC_INV_CONN :
						FCNVME_RJT_RC_LOGIC,
				FCNVME_RJT_EXP_NONE, 0);
		return;
	}

	/* format a response */

	iod->lsreq->rsplen = sizeof(*acc);

	nvmet_fc_format_rsp_hdr(acc, FCNVME_LS_ACC,
			fcnvme_lsdesc_len(
				sizeof(struct fcnvme_ls_disconnect_acc)),
			FCNVME_LS_DISCONNECT);

	/* release get taken in nvmet_fc_find_target_assoc */
	nvmet_fc_tgt_a_put(iod->assoc);

	nvmet_fc_delete_target_assoc(iod->assoc);
}


/* *********************** NVME Ctrl Routines **************************** */


static void nvmet_fc_fcp_nvme_cmd_done(struct nvmet_req *nvme_req);

static const struct nvmet_fabrics_ops nvmet_fc_tgt_fcp_ops;

static void
nvmet_fc_xmt_ls_rsp_done(struct nvmefc_tgt_ls_req *lsreq)
{
	struct nvmet_fc_ls_iod *iod = lsreq->nvmet_fc_private;
	struct nvmet_fc_tgtport *tgtport = iod->tgtport;

	fc_dma_sync_single_for_cpu(tgtport->dev, iod->rspdma,
				NVME_FC_MAX_LS_BUFFER_SIZE, DMA_TO_DEVICE);
	nvmet_fc_free_ls_iod(tgtport, iod);
	nvmet_fc_tgtport_put(tgtport);
}

static void
nvmet_fc_xmt_ls_rsp(struct nvmet_fc_tgtport *tgtport,
				struct nvmet_fc_ls_iod *iod)
{
	int ret;

	fc_dma_sync_single_for_device(tgtport->dev, iod->rspdma,
				  NVME_FC_MAX_LS_BUFFER_SIZE, DMA_TO_DEVICE);

	ret = tgtport->ops->xmt_ls_rsp(&tgtport->fc_target_port, iod->lsreq);
	if (ret)
		nvmet_fc_xmt_ls_rsp_done(iod->lsreq);
}

/*
 * Actual processing routine for received FC-NVME LS Requests from the LLD
 */
static void
nvmet_fc_handle_ls_rqst(struct nvmet_fc_tgtport *tgtport,
			struct nvmet_fc_ls_iod *iod)
{
	struct fcnvme_ls_rqst_w0 *w0 =
			(struct fcnvme_ls_rqst_w0 *)iod->rqstbuf;

	iod->lsreq->nvmet_fc_private = iod;
	iod->lsreq->rspbuf = iod->rspbuf;
	iod->lsreq->rspdma = iod->rspdma;
	iod->lsreq->done = nvmet_fc_xmt_ls_rsp_done;
	/* Be preventative. handlers will later set to valid length */
	iod->lsreq->rsplen = 0;

	iod->assoc = NULL;

	/*
	 * handlers:
	 *   parse request input, execute the request, and format the
	 *   LS response
	 */
	switch (w0->ls_cmd) {
	case FCNVME_LS_CREATE_ASSOCIATION:
		/* Creates Association and initial Admin Queue/Connection */
		nvmet_fc_ls_create_association(tgtport, iod);
		break;
	case FCNVME_LS_CREATE_CONNECTION:
		/* Creates an IO Queue/Connection */
		nvmet_fc_ls_create_connection(tgtport, iod);
		break;
	case FCNVME_LS_DISCONNECT:
		/* Terminate a Queue/Connection or the Association */
		nvmet_fc_ls_disconnect(tgtport, iod);
		break;
	default:
		iod->lsreq->rsplen = nvmet_fc_format_rjt(iod->rspbuf,
				NVME_FC_MAX_LS_BUFFER_SIZE, w0->ls_cmd,
				FCNVME_RJT_RC_INVAL, FCNVME_RJT_EXP_NONE, 0);
	}

	nvmet_fc_xmt_ls_rsp(tgtport, iod);
}

/*
 * Actual processing routine for received FC-NVME LS Requests from the LLD
 */
static void
nvmet_fc_handle_ls_rqst_work(struct work_struct *work)
{
	struct nvmet_fc_ls_iod *iod =
		container_of(work, struct nvmet_fc_ls_iod, work);
	struct nvmet_fc_tgtport *tgtport = iod->tgtport;

	nvmet_fc_handle_ls_rqst(tgtport, iod);
}


/**
 * nvmet_fc_rcv_ls_req - transport entry point called by an LLDD
 *                       upon the reception of a NVME LS request.
 *
 * The nvmet-fc layer will copy payload to an internal structure for
 * processing.  As such, upon completion of the routine, the LLDD may
 * immediately free/reuse the LS request buffer passed in the call.
 *
 * If this routine returns error, the LLDD should abort the exchange.
 *
 * @target_port: pointer to the (registered) target port the LS was
 *              received on.
 * @lsreq:      pointer to a lsreq request structure to be used to reference
 *              the exchange corresponding to the LS.
 * @lsreqbuf:   pointer to the buffer containing the LS Request
 * @lsreqbuf_len: length, in bytes, of the received LS request
 */
int
nvmet_fc_rcv_ls_req(struct nvmet_fc_target_port *target_port,
			struct nvmefc_tgt_ls_req *lsreq,
			void *lsreqbuf, u32 lsreqbuf_len)
{
	struct nvmet_fc_tgtport *tgtport = targetport_to_tgtport(target_port);
	struct nvmet_fc_ls_iod *iod;

	if (lsreqbuf_len > NVME_FC_MAX_LS_BUFFER_SIZE)
		return -E2BIG;

	if (!nvmet_fc_tgtport_get(tgtport))
		return -ESHUTDOWN;

	iod = nvmet_fc_alloc_ls_iod(tgtport);
	if (!iod) {
		nvmet_fc_tgtport_put(tgtport);
		return -ENOENT;
	}

	iod->lsreq = lsreq;
	iod->fcpreq = NULL;
	memcpy(iod->rqstbuf, lsreqbuf, lsreqbuf_len);
	iod->rqstdatalen = lsreqbuf_len;

	schedule_work(&iod->work);

	return 0;
}
EXPORT_SYMBOL_GPL(nvmet_fc_rcv_ls_req);


/*
 * **********************
 * Start of FCP handling
 * **********************
 */

static int
nvmet_fc_alloc_tgt_pgs(struct nvmet_fc_fcp_iod *fod)
{
	struct scatterlist *sg;
	unsigned int nent;

	sg = sgl_alloc(fod->req.transfer_len, GFP_KERNEL, &nent);
	if (!sg)
		goto out;

	fod->data_sg = sg;
	fod->data_sg_cnt = nent;
	fod->data_sg_cnt = fc_dma_map_sg(fod->tgtport->dev, sg, nent,
				((fod->io_dir == NVMET_FCP_WRITE) ?
					DMA_FROM_DEVICE : DMA_TO_DEVICE));
				/* note: write from initiator perspective */
	fod->next_sg = fod->data_sg;

	return 0;

out:
	return NVME_SC_INTERNAL;
}

static void
nvmet_fc_free_tgt_pgs(struct nvmet_fc_fcp_iod *fod)
{
	if (!fod->data_sg || !fod->data_sg_cnt)
		return;

	fc_dma_unmap_sg(fod->tgtport->dev, fod->data_sg, fod->data_sg_cnt,
				((fod->io_dir == NVMET_FCP_WRITE) ?
					DMA_FROM_DEVICE : DMA_TO_DEVICE));
	sgl_free(fod->data_sg);
	fod->data_sg = NULL;
	fod->data_sg_cnt = 0;
}


static bool
queue_90percent_full(struct nvmet_fc_tgt_queue *q, u32 sqhd)
{
	u32 sqtail, used;

	/* egad, this is ugly. And sqtail is just a best guess */
	sqtail = atomic_read(&q->sqtail) % q->sqsize;

	used = (sqtail < sqhd) ? (sqtail + q->sqsize - sqhd) : (sqtail - sqhd);
	return ((used * 10) >= (((u32)(q->sqsize - 1) * 9)));
}

/*
 * Prep RSP payload.
 * May be a NVMET_FCOP_RSP or NVMET_FCOP_READDATA_RSP op
 */
static void
nvmet_fc_prep_fcp_rsp(struct nvmet_fc_tgtport *tgtport,
				struct nvmet_fc_fcp_iod *fod)
{
	struct nvme_fc_ersp_iu *ersp = &fod->rspiubuf;
	struct nvme_common_command *sqe = &fod->cmdiubuf.sqe.common;
	struct nvme_completion *cqe = &ersp->cqe;
	u32 *cqewd = (u32 *)cqe;
	bool send_ersp = false;
	u32 rsn, rspcnt, xfr_length;

	if (fod->fcpreq->op == NVMET_FCOP_READDATA_RSP)
		xfr_length = fod->req.transfer_len;
	else
		xfr_length = fod->offset;

	/*
	 * check to see if we can send a 0's rsp.
	 *   Note: to send a 0's response, the NVME-FC host transport will
	 *   recreate the CQE. The host transport knows: sq id, SQHD (last
	 *   seen in an ersp), and command_id. Thus it will create a
	 *   zero-filled CQE with those known fields filled in. Transport
	 *   must send an ersp for any condition where the cqe won't match
	 *   this.
	 *
	 * Here are the FC-NVME mandated cases where we must send an ersp:
	 *  every N responses, where N=ersp_ratio
	 *  force fabric commands to send ersp's (not in FC-NVME but good
	 *    practice)
	 *  normal cmds: any time status is non-zero, or status is zero
	 *     but words 0 or 1 are non-zero.
	 *  the SQ is 90% or more full
	 *  the cmd is a fused command
	 *  transferred data length not equal to cmd iu length
	 */
	rspcnt = atomic_inc_return(&fod->queue->zrspcnt);
	if (!(rspcnt % fod->queue->ersp_ratio) ||
	    sqe->opcode == nvme_fabrics_command ||
	    xfr_length != fod->req.transfer_len ||
	    (le16_to_cpu(cqe->status) & 0xFFFE) || cqewd[0] || cqewd[1] ||
	    (sqe->flags & (NVME_CMD_FUSE_FIRST | NVME_CMD_FUSE_SECOND)) ||
	    queue_90percent_full(fod->queue, le16_to_cpu(cqe->sq_head)))
		send_ersp = true;

	/* re-set the fields */
	fod->fcpreq->rspaddr = ersp;
	fod->fcpreq->rspdma = fod->rspdma;

	if (!send_ersp) {
		memset(ersp, 0, NVME_FC_SIZEOF_ZEROS_RSP);
		fod->fcpreq->rsplen = NVME_FC_SIZEOF_ZEROS_RSP;
	} else {
		ersp->iu_len = cpu_to_be16(sizeof(*ersp)/sizeof(u32));
		rsn = atomic_inc_return(&fod->queue->rsn);
		ersp->rsn = cpu_to_be32(rsn);
		ersp->xfrd_len = cpu_to_be32(xfr_length);
		fod->fcpreq->rsplen = sizeof(*ersp);
	}

	fc_dma_sync_single_for_device(tgtport->dev, fod->rspdma,
				  sizeof(fod->rspiubuf), DMA_TO_DEVICE);
}

static void nvmet_fc_xmt_fcp_op_done(struct nvmefc_tgt_fcp_req *fcpreq);

static void
nvmet_fc_abort_op(struct nvmet_fc_tgtport *tgtport,
				struct nvmet_fc_fcp_iod *fod)
{
	struct nvmefc_tgt_fcp_req *fcpreq = fod->fcpreq;

	/* data no longer needed */
	nvmet_fc_free_tgt_pgs(fod);

	/*
	 * if an ABTS was received or we issued the fcp_abort early
	 * don't call abort routine again.
	 */
	/* no need to take lock - lock was taken earlier to get here */
	if (!fod->aborted)
		tgtport->ops->fcp_abort(&tgtport->fc_target_port, fcpreq);

	nvmet_fc_free_fcp_iod(fod->queue, fod);
}

static void
nvmet_fc_xmt_fcp_rsp(struct nvmet_fc_tgtport *tgtport,
				struct nvmet_fc_fcp_iod *fod)
{
	int ret;

	fod->fcpreq->op = NVMET_FCOP_RSP;
	fod->fcpreq->timeout = 0;

	nvmet_fc_prep_fcp_rsp(tgtport, fod);

	ret = tgtport->ops->fcp_op(&tgtport->fc_target_port, fod->fcpreq);
	if (ret)
		nvmet_fc_abort_op(tgtport, fod);
}

static void
nvmet_fc_transfer_fcp_data(struct nvmet_fc_tgtport *tgtport,
				struct nvmet_fc_fcp_iod *fod, u8 op)
{
	struct nvmefc_tgt_fcp_req *fcpreq = fod->fcpreq;
	struct scatterlist *sg = fod->next_sg;
	unsigned long flags;
	u32 remaininglen = fod->req.transfer_len - fod->offset;
	u32 tlen = 0;
	int ret;

	fcpreq->op = op;
	fcpreq->offset = fod->offset;
	fcpreq->timeout = NVME_FC_TGTOP_TIMEOUT_SEC;

	/*
	 * for next sequence:
	 *  break at a sg element boundary
	 *  attempt to keep sequence length capped at
	 *    NVMET_FC_MAX_SEQ_LENGTH but allow sequence to
	 *    be longer if a single sg element is larger
	 *    than that amount. This is done to avoid creating
	 *    a new sg list to use for the tgtport api.
	 */
	fcpreq->sg = sg;
	fcpreq->sg_cnt = 0;
	while (tlen < remaininglen &&
	       fcpreq->sg_cnt < tgtport->max_sg_cnt &&
	       tlen + sg_dma_len(sg) < NVMET_FC_MAX_SEQ_LENGTH) {
		fcpreq->sg_cnt++;
		tlen += sg_dma_len(sg);
		sg = sg_next(sg);
	}
	if (tlen < remaininglen && fcpreq->sg_cnt == 0) {
		fcpreq->sg_cnt++;
		tlen += min_t(u32, sg_dma_len(sg), remaininglen);
		sg = sg_next(sg);
	}
	if (tlen < remaininglen)
		fod->next_sg = sg;
	else
		fod->next_sg = NULL;

	fcpreq->transfer_length = tlen;
	fcpreq->transferred_length = 0;
	fcpreq->fcp_error = 0;
	fcpreq->rsplen = 0;

	/*
	 * If the last READDATA request: check if LLDD supports
	 * combined xfr with response.
	 */
	if ((op == NVMET_FCOP_READDATA) &&
	    ((fod->offset + fcpreq->transfer_length) == fod->req.transfer_len) &&
	    (tgtport->ops->target_features & NVMET_FCTGTFEAT_READDATA_RSP)) {
		fcpreq->op = NVMET_FCOP_READDATA_RSP;
		nvmet_fc_prep_fcp_rsp(tgtport, fod);
	}

	ret = tgtport->ops->fcp_op(&tgtport->fc_target_port, fod->fcpreq);
	if (ret) {
		/*
		 * should be ok to set w/o lock as its in the thread of
		 * execution (not an async timer routine) and doesn't
		 * contend with any clearing action
		 */
		fod->abort = true;

		if (op == NVMET_FCOP_WRITEDATA) {
			spin_lock_irqsave(&fod->flock, flags);
			fod->writedataactive = false;
			spin_unlock_irqrestore(&fod->flock, flags);
			nvmet_req_complete(&fod->req, NVME_SC_INTERNAL);
		} else /* NVMET_FCOP_READDATA or NVMET_FCOP_READDATA_RSP */ {
			fcpreq->fcp_error = ret;
			fcpreq->transferred_length = 0;
			nvmet_fc_xmt_fcp_op_done(fod->fcpreq);
		}
	}
}

static inline bool
__nvmet_fc_fod_op_abort(struct nvmet_fc_fcp_iod *fod, bool abort)
{
	struct nvmefc_tgt_fcp_req *fcpreq = fod->fcpreq;
	struct nvmet_fc_tgtport *tgtport = fod->tgtport;

	/* if in the middle of an io and we need to tear down */
	if (abort) {
		if (fcpreq->op == NVMET_FCOP_WRITEDATA) {
			nvmet_req_complete(&fod->req, NVME_SC_INTERNAL);
			return true;
		}

		nvmet_fc_abort_op(tgtport, fod);
		return true;
	}

	return false;
}

/*
 * actual done handler for FCP operations when completed by the lldd
 */
static void
nvmet_fc_fod_op_done(struct nvmet_fc_fcp_iod *fod)
{
	struct nvmefc_tgt_fcp_req *fcpreq = fod->fcpreq;
	struct nvmet_fc_tgtport *tgtport = fod->tgtport;
	unsigned long flags;
	bool abort;

	spin_lock_irqsave(&fod->flock, flags);
	abort = fod->abort;
	fod->writedataactive = false;
	spin_unlock_irqrestore(&fod->flock, flags);

	switch (fcpreq->op) {

	case NVMET_FCOP_WRITEDATA:
		if (__nvmet_fc_fod_op_abort(fod, abort))
			return;
		if (fcpreq->fcp_error ||
		    fcpreq->transferred_length != fcpreq->transfer_length) {
			spin_lock(&fod->flock);
			fod->abort = true;
			spin_unlock(&fod->flock);

			nvmet_req_complete(&fod->req, NVME_SC_INTERNAL);
			return;
		}

		fod->offset += fcpreq->transferred_length;
		if (fod->offset != fod->req.transfer_len) {
			spin_lock_irqsave(&fod->flock, flags);
			fod->writedataactive = true;
			spin_unlock_irqrestore(&fod->flock, flags);

			/* transfer the next chunk */
			nvmet_fc_transfer_fcp_data(tgtport, fod,
						NVMET_FCOP_WRITEDATA);
			return;
		}

		/* data transfer complete, resume with nvmet layer */
		nvmet_req_execute(&fod->req);
		break;

	case NVMET_FCOP_READDATA:
	case NVMET_FCOP_READDATA_RSP:
		if (__nvmet_fc_fod_op_abort(fod, abort))
			return;
		if (fcpreq->fcp_error ||
		    fcpreq->transferred_length != fcpreq->transfer_length) {
			nvmet_fc_abort_op(tgtport, fod);
			return;
		}

		/* success */

		if (fcpreq->op == NVMET_FCOP_READDATA_RSP) {
			/* data no longer needed */
			nvmet_fc_free_tgt_pgs(fod);
			nvmet_fc_free_fcp_iod(fod->queue, fod);
			return;
		}

		fod->offset += fcpreq->transferred_length;
		if (fod->offset != fod->req.transfer_len) {
			/* transfer the next chunk */
			nvmet_fc_transfer_fcp_data(tgtport, fod,
						NVMET_FCOP_READDATA);
			return;
		}

		/* data transfer complete, send response */

		/* data no longer needed */
		nvmet_fc_free_tgt_pgs(fod);

		nvmet_fc_xmt_fcp_rsp(tgtport, fod);

		break;

	case NVMET_FCOP_RSP:
		if (__nvmet_fc_fod_op_abort(fod, abort))
			return;
		nvmet_fc_free_fcp_iod(fod->queue, fod);
		break;

	default:
		break;
	}
}

static void
nvmet_fc_xmt_fcp_op_done(struct nvmefc_tgt_fcp_req *fcpreq)
{
	struct nvmet_fc_fcp_iod *fod = fcpreq->nvmet_fc_private;

	nvmet_fc_fod_op_done(fod);
}

/*
 * actual completion handler after execution by the nvmet layer
 */
static void
__nvmet_fc_fcp_nvme_cmd_done(struct nvmet_fc_tgtport *tgtport,
			struct nvmet_fc_fcp_iod *fod, int status)
{
	struct nvme_common_command *sqe = &fod->cmdiubuf.sqe.common;
	struct nvme_completion *cqe = &fod->rspiubuf.cqe;
	unsigned long flags;
	bool abort;

	spin_lock_irqsave(&fod->flock, flags);
	abort = fod->abort;
	spin_unlock_irqrestore(&fod->flock, flags);

	/* if we have a CQE, snoop the last sq_head value */
	if (!status)
		fod->queue->sqhd = cqe->sq_head;

	if (abort) {
		nvmet_fc_abort_op(tgtport, fod);
		return;
	}

	/* if an error handling the cmd post initial parsing */
	if (status) {
		/* fudge up a failed CQE status for our transport error */
		memset(cqe, 0, sizeof(*cqe));
		cqe->sq_head = fod->queue->sqhd;	/* echo last cqe sqhd */
		cqe->sq_id = cpu_to_le16(fod->queue->qid);
		cqe->command_id = sqe->command_id;
		cqe->status = cpu_to_le16(status);
	} else {

		/*
		 * try to push the data even if the SQE status is non-zero.
		 * There may be a status where data still was intended to
		 * be moved
		 */
		if ((fod->io_dir == NVMET_FCP_READ) && (fod->data_sg_cnt)) {
			/* push the data over before sending rsp */
			nvmet_fc_transfer_fcp_data(tgtport, fod,
						NVMET_FCOP_READDATA);
			return;
		}

		/* writes & no data - fall thru */
	}

	/* data no longer needed */
	nvmet_fc_free_tgt_pgs(fod);

	nvmet_fc_xmt_fcp_rsp(tgtport, fod);
}


static void
nvmet_fc_fcp_nvme_cmd_done(struct nvmet_req *nvme_req)
{
	struct nvmet_fc_fcp_iod *fod = nvmet_req_to_fod(nvme_req);
	struct nvmet_fc_tgtport *tgtport = fod->tgtport;

	__nvmet_fc_fcp_nvme_cmd_done(tgtport, fod, 0);
}


/*
 * Actual processing routine for received FC-NVME I/O Requests from the LLD
 */
static void
nvmet_fc_handle_fcp_rqst(struct nvmet_fc_tgtport *tgtport,
			struct nvmet_fc_fcp_iod *fod)
{
	struct nvme_fc_cmd_iu *cmdiu = &fod->cmdiubuf;
	u32 xfrlen = be32_to_cpu(cmdiu->data_len);
	int ret;

	/*
	 * if there is no nvmet mapping to the targetport there
	 * shouldn't be requests. just terminate them.
	 */
	if (!tgtport->pe)
		goto transport_error;

	/*
	 * Fused commands are currently not supported in the linux
	 * implementation.
	 *
	 * As such, the implementation of the FC transport does not
	 * look at the fused commands and order delivery to the upper
	 * layer until we have both based on csn.
	 */

	fod->fcpreq->done = nvmet_fc_xmt_fcp_op_done;

	if (cmdiu->flags & FCNVME_CMD_FLAGS_WRITE) {
		fod->io_dir = NVMET_FCP_WRITE;
		if (!nvme_is_write(&cmdiu->sqe))
			goto transport_error;
	} else if (cmdiu->flags & FCNVME_CMD_FLAGS_READ) {
		fod->io_dir = NVMET_FCP_READ;
		if (nvme_is_write(&cmdiu->sqe))
			goto transport_error;
	} else {
		fod->io_dir = NVMET_FCP_NODATA;
		if (xfrlen)
			goto transport_error;
	}

	fod->req.cmd = &fod->cmdiubuf.sqe;
	fod->req.rsp = &fod->rspiubuf.cqe;
	fod->req.port = tgtport->pe->port;

	/* clear any response payload */
	memset(&fod->rspiubuf, 0, sizeof(fod->rspiubuf));

	fod->data_sg = NULL;
	fod->data_sg_cnt = 0;

	ret = nvmet_req_init(&fod->req,
				&fod->queue->nvme_cq,
				&fod->queue->nvme_sq,
				&nvmet_fc_tgt_fcp_ops);
	if (!ret) {
		/* bad SQE content or invalid ctrl state */
		/* nvmet layer has already called op done to send rsp. */
		return;
	}

	fod->req.transfer_len = xfrlen;

	/* keep a running counter of tail position */
	atomic_inc(&fod->queue->sqtail);

	if (fod->req.transfer_len) {
		ret = nvmet_fc_alloc_tgt_pgs(fod);
		if (ret) {
			nvmet_req_complete(&fod->req, ret);
			return;
		}
	}
	fod->req.sg = fod->data_sg;
	fod->req.sg_cnt = fod->data_sg_cnt;
	fod->offset = 0;

	if (fod->io_dir == NVMET_FCP_WRITE) {
		/* pull the data over before invoking nvmet layer */
		nvmet_fc_transfer_fcp_data(tgtport, fod, NVMET_FCOP_WRITEDATA);
		return;
	}

	/*
	 * Reads or no data:
	 *
	 * can invoke the nvmet_layer now. If read data, cmd completion will
	 * push the data
	 */
	nvmet_req_execute(&fod->req);
	return;

transport_error:
	nvmet_fc_abort_op(tgtport, fod);
}

/**
 * nvmet_fc_rcv_fcp_req - transport entry point called by an LLDD
 *                       upon the reception of a NVME FCP CMD IU.
 *
 * Pass a FC-NVME FCP CMD IU received from the FC link to the nvmet-fc
 * layer for processing.
 *
 * The nvmet_fc layer allocates a local job structure (struct
 * nvmet_fc_fcp_iod) from the queue for the io and copies the
 * CMD IU buffer to the job structure. As such, on a successful
 * completion (returns 0), the LLDD may immediately free/reuse
 * the CMD IU buffer passed in the call.
 *
 * However, in some circumstances, due to the packetized nature of FC
 * and the api of the FC LLDD which may issue a hw command to send the
 * response, but the LLDD may not get the hw completion for that command
 * and upcall the nvmet_fc layer before a new command may be
 * asynchronously received - its possible for a command to be received
 * before the LLDD and nvmet_fc have recycled the job structure. It gives
 * the appearance of more commands received than fits in the sq.
 * To alleviate this scenario, a temporary queue is maintained in the
 * transport for pending LLDD requests waiting for a queue job structure.
 * In these "overrun" cases, a temporary queue element is allocated
 * the LLDD request and CMD iu buffer information remembered, and the
 * routine returns a -EOVERFLOW status. Subsequently, when a queue job
 * structure is freed, it is immediately reallocated for anything on the
 * pending request list. The LLDDs defer_rcv() callback is called,
 * informing the LLDD that it may reuse the CMD IU buffer, and the io
 * is then started normally with the transport.
 *
 * The LLDD, when receiving an -EOVERFLOW completion status, is to treat
 * the completion as successful but must not reuse the CMD IU buffer
 * until the LLDD's defer_rcv() callback has been called for the
 * corresponding struct nvmefc_tgt_fcp_req pointer.
 *
 * If there is any other condition in which an error occurs, the
 * transport will return a non-zero status indicating the error.
 * In all cases other than -EOVERFLOW, the transport has not accepted the
 * request and the LLDD should abort the exchange.
 *
 * @target_port: pointer to the (registered) target port the FCP CMD IU
 *              was received on.
 * @fcpreq:     pointer to a fcpreq request structure to be used to reference
 *              the exchange corresponding to the FCP Exchange.
 * @cmdiubuf:   pointer to the buffer containing the FCP CMD IU
 * @cmdiubuf_len: length, in bytes, of the received FCP CMD IU
 */
int
nvmet_fc_rcv_fcp_req(struct nvmet_fc_target_port *target_port,
			struct nvmefc_tgt_fcp_req *fcpreq,
			void *cmdiubuf, u32 cmdiubuf_len)
{
	struct nvmet_fc_tgtport *tgtport = targetport_to_tgtport(target_port);
	struct nvme_fc_cmd_iu *cmdiu = cmdiubuf;
	struct nvmet_fc_tgt_queue *queue;
	struct nvmet_fc_fcp_iod *fod;
	struct nvmet_fc_defer_fcp_req *deferfcp;
	unsigned long flags;

	/* validate iu, so the connection id can be used to find the queue */
	if ((cmdiubuf_len != sizeof(*cmdiu)) ||
			(cmdiu->scsi_id != NVME_CMD_SCSI_ID) ||
			(cmdiu->fc_id != NVME_CMD_FC_ID) ||
			(be16_to_cpu(cmdiu->iu_len) != (sizeof(*cmdiu)/4)))
		return -EIO;

	queue = nvmet_fc_find_target_queue(tgtport,
				be64_to_cpu(cmdiu->connection_id));
	if (!queue)
		return -ENOTCONN;

	/*
	 * note: reference taken by find_target_queue
	 * After successful fod allocation, the fod will inherit the
	 * ownership of that reference and will remove the reference
	 * when the fod is freed.
	 */

	spin_lock_irqsave(&queue->qlock, flags);

	fod = nvmet_fc_alloc_fcp_iod(queue);
	if (fod) {
		spin_unlock_irqrestore(&queue->qlock, flags);

		fcpreq->nvmet_fc_private = fod;
		fod->fcpreq = fcpreq;

		memcpy(&fod->cmdiubuf, cmdiubuf, cmdiubuf_len);

		nvmet_fc_queue_fcp_req(tgtport, queue, fcpreq);

		return 0;
	}

	if (!tgtport->ops->defer_rcv) {
		spin_unlock_irqrestore(&queue->qlock, flags);
		/* release the queue lookup reference */
		nvmet_fc_tgt_q_put(queue);
		return -ENOENT;
	}

	deferfcp = list_first_entry_or_null(&queue->avail_defer_list,
			struct nvmet_fc_defer_fcp_req, req_list);
	if (deferfcp) {
		/* Just re-use one that was previously allocated */
		list_del(&deferfcp->req_list);
	} else {
		spin_unlock_irqrestore(&queue->qlock, flags);

		/* Now we need to dynamically allocate one */
		deferfcp = kmalloc(sizeof(*deferfcp), GFP_KERNEL);
		if (!deferfcp) {
			/* release the queue lookup reference */
			nvmet_fc_tgt_q_put(queue);
			return -ENOMEM;
		}
		spin_lock_irqsave(&queue->qlock, flags);
	}

	/* For now, use rspaddr / rsplen to save payload information */
	fcpreq->rspaddr = cmdiubuf;
	fcpreq->rsplen  = cmdiubuf_len;
	deferfcp->fcp_req = fcpreq;

	/* defer processing till a fod becomes available */
	list_add_tail(&deferfcp->req_list, &queue->pending_cmd_list);

	/* NOTE: the queue lookup reference is still valid */

	spin_unlock_irqrestore(&queue->qlock, flags);

	return -EOVERFLOW;
}
EXPORT_SYMBOL_GPL(nvmet_fc_rcv_fcp_req);

/**
 * nvmet_fc_rcv_fcp_abort - transport entry point called by an LLDD
 *                       upon the reception of an ABTS for a FCP command
 *
 * Notify the transport that an ABTS has been received for a FCP command
 * that had been given to the transport via nvmet_fc_rcv_fcp_req(). The
 * LLDD believes the command is still being worked on
 * (template_ops->fcp_req_release() has not been called).
 *
 * The transport will wait for any outstanding work (an op to the LLDD,
 * which the lldd should complete with error due to the ABTS; or the
 * completion from the nvmet layer of the nvme command), then will
 * stop processing and call the nvmet_fc_rcv_fcp_req() callback to
 * return the i/o context to the LLDD.  The LLDD may send the BA_ACC
 * to the ABTS either after return from this function (assuming any
 * outstanding op work has been terminated) or upon the callback being
 * called.
 *
 * @target_port: pointer to the (registered) target port the FCP CMD IU
 *              was received on.
 * @fcpreq:     pointer to the fcpreq request structure that corresponds
 *              to the exchange that received the ABTS.
 */
void
nvmet_fc_rcv_fcp_abort(struct nvmet_fc_target_port *target_port,
			struct nvmefc_tgt_fcp_req *fcpreq)
{
	struct nvmet_fc_fcp_iod *fod = fcpreq->nvmet_fc_private;
	struct nvmet_fc_tgt_queue *queue;
	unsigned long flags;

	if (!fod || fod->fcpreq != fcpreq)
		/* job appears to have already completed, ignore abort */
		return;

	queue = fod->queue;

	spin_lock_irqsave(&queue->qlock, flags);
	if (fod->active) {
		/*
		 * mark as abort. The abort handler, invoked upon completion
		 * of any work, will detect the aborted status and do the
		 * callback.
		 */
		spin_lock(&fod->flock);
		fod->abort = true;
		fod->aborted = true;
		spin_unlock(&fod->flock);
	}
	spin_unlock_irqrestore(&queue->qlock, flags);
}
EXPORT_SYMBOL_GPL(nvmet_fc_rcv_fcp_abort);


struct nvmet_fc_traddr {
	u64	nn;
	u64	pn;
};

static int
__nvme_fc_parse_u64(substring_t *sstr, u64 *val)
{
	u64 token64;

	if (match_u64(sstr, &token64))
		return -EINVAL;
	*val = token64;

	return 0;
}

/*
 * This routine validates and extracts the WWN's from the TRADDR string.
 * As kernel parsers need the 0x to determine number base, universally
 * build string to parse with 0x prefix before parsing name strings.
 */
static int
nvme_fc_parse_traddr(struct nvmet_fc_traddr *traddr, char *buf, size_t blen)
{
	char name[2 + NVME_FC_TRADDR_HEXNAMELEN + 1];
	substring_t wwn = { name, &name[sizeof(name)-1] };
	int nnoffset, pnoffset;

	/* validate if string is one of the 2 allowed formats */
	if (strnlen(buf, blen) == NVME_FC_TRADDR_MAXLENGTH &&
			!strncmp(buf, "nn-0x", NVME_FC_TRADDR_OXNNLEN) &&
			!strncmp(&buf[NVME_FC_TRADDR_MAX_PN_OFFSET],
				"pn-0x", NVME_FC_TRADDR_OXNNLEN)) {
		nnoffset = NVME_FC_TRADDR_OXNNLEN;
		pnoffset = NVME_FC_TRADDR_MAX_PN_OFFSET +
						NVME_FC_TRADDR_OXNNLEN;
	} else if ((strnlen(buf, blen) == NVME_FC_TRADDR_MINLENGTH &&
			!strncmp(buf, "nn-", NVME_FC_TRADDR_NNLEN) &&
			!strncmp(&buf[NVME_FC_TRADDR_MIN_PN_OFFSET],
				"pn-", NVME_FC_TRADDR_NNLEN))) {
		nnoffset = NVME_FC_TRADDR_NNLEN;
		pnoffset = NVME_FC_TRADDR_MIN_PN_OFFSET + NVME_FC_TRADDR_NNLEN;
	} else
		goto out_einval;

	name[0] = '0';
	name[1] = 'x';
	name[2 + NVME_FC_TRADDR_HEXNAMELEN] = 0;

	memcpy(&name[2], &buf[nnoffset], NVME_FC_TRADDR_HEXNAMELEN);
	if (__nvme_fc_parse_u64(&wwn, &traddr->nn))
		goto out_einval;

	memcpy(&name[2], &buf[pnoffset], NVME_FC_TRADDR_HEXNAMELEN);
	if (__nvme_fc_parse_u64(&wwn, &traddr->pn))
		goto out_einval;

	return 0;

out_einval:
	pr_warn("%s: bad traddr string\n", __func__);
	return -EINVAL;
}

static int
nvmet_fc_add_port(struct nvmet_port *port)
{
	struct nvmet_fc_tgtport *tgtport;
	struct nvmet_fc_port_entry *pe;
	struct nvmet_fc_traddr traddr = { 0L, 0L };
	unsigned long flags;
	int ret;

	/* validate the address info */
	if ((port->disc_addr.trtype != NVMF_TRTYPE_FC) ||
	    (port->disc_addr.adrfam != NVMF_ADDR_FAMILY_FC))
		return -EINVAL;

	/* map the traddr address info to a target port */

	ret = nvme_fc_parse_traddr(&traddr, port->disc_addr.traddr,
			sizeof(port->disc_addr.traddr));
	if (ret)
		return ret;

	pe = kzalloc(sizeof(*pe), GFP_KERNEL);
	if (!pe)
		return -ENOMEM;

	ret = -ENXIO;
	spin_lock_irqsave(&nvmet_fc_tgtlock, flags);
	list_for_each_entry(tgtport, &nvmet_fc_target_list, tgt_list) {
		if ((tgtport->fc_target_port.node_name == traddr.nn) &&
		    (tgtport->fc_target_port.port_name == traddr.pn)) {
			/* a FC port can only be 1 nvmet port id */
			if (!tgtport->pe) {
				nvmet_fc_portentry_bind(tgtport, pe, port);
				ret = 0;
			} else
				ret = -EALREADY;
			break;
		}
	}
	spin_unlock_irqrestore(&nvmet_fc_tgtlock, flags);

	if (ret)
		kfree(pe);

	return ret;
}

static void
nvmet_fc_remove_port(struct nvmet_port *port)
{
	struct nvmet_fc_port_entry *pe = port->priv;

	nvmet_fc_portentry_unbind(pe);

	kfree(pe);
}

static const struct nvmet_fabrics_ops nvmet_fc_tgt_fcp_ops = {
	.owner			= THIS_MODULE,
	.type			= NVMF_TRTYPE_FC,
	.msdbd			= 1,
	.add_port		= nvmet_fc_add_port,
	.remove_port		= nvmet_fc_remove_port,
	.queue_response		= nvmet_fc_fcp_nvme_cmd_done,
	.delete_ctrl		= nvmet_fc_delete_ctrl,
};

static int __init nvmet_fc_init_module(void)
{
	return nvmet_register_transport(&nvmet_fc_tgt_fcp_ops);
}

static void __exit nvmet_fc_exit_module(void)
{
	/* sanity check - all lports should be removed */
	if (!list_empty(&nvmet_fc_target_list))
		pr_warn("%s: targetport list not empty\n", __func__);

	nvmet_unregister_transport(&nvmet_fc_tgt_fcp_ops);

	ida_destroy(&nvmet_fc_tgtport_cnt);
}

module_init(nvmet_fc_init_module);
module_exit(nvmet_fc_exit_module);

MODULE_LICENSE("GPL v2");

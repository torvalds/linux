// SPDX-License-Identifier: GPL-2.0-only

/* Copyright (c) 2019-2021, The Linux Foundation. All rights reserved. */
/* Copyright (c) 2021-2023 Qualcomm Innovation Center, Inc. All rights reserved. */

#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/dma-buf.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/kref.h>
#include <linux/list.h>
#include <linux/math64.h>
#include <linux/mm.h>
#include <linux/moduleparam.h>
#include <linux/scatterlist.h>
#include <linux/spinlock.h>
#include <linux/srcu.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/wait.h>
#include <drm/drm_file.h>
#include <drm/drm_gem.h>
#include <drm/drm_prime.h>
#include <drm/drm_print.h>
#include <uapi/drm/qaic_accel.h>

#include "qaic.h"

#define SEM_VAL_MASK	GENMASK_ULL(11, 0)
#define SEM_INDEX_MASK	GENMASK_ULL(4, 0)
#define BULK_XFER	BIT(3)
#define GEN_COMPLETION	BIT(4)
#define INBOUND_XFER	1
#define OUTBOUND_XFER	2
#define REQHP_OFF	0x0 /* we read this */
#define REQTP_OFF	0x4 /* we write this */
#define RSPHP_OFF	0x8 /* we write this */
#define RSPTP_OFF	0xc /* we read this */

#define ENCODE_SEM(val, index, sync, cmd, flags)			\
		({							\
			FIELD_PREP(GENMASK(11, 0), (val)) |		\
			FIELD_PREP(GENMASK(20, 16), (index)) |		\
			FIELD_PREP(BIT(22), (sync)) |			\
			FIELD_PREP(GENMASK(26, 24), (cmd)) |		\
			FIELD_PREP(GENMASK(30, 29), (flags)) |		\
			FIELD_PREP(BIT(31), (cmd) ? 1 : 0);		\
		})
#define NUM_EVENTS	128
#define NUM_DELAYS	10

static unsigned int wait_exec_default_timeout_ms = 5000; /* 5 sec default */
module_param(wait_exec_default_timeout_ms, uint, 0600);
MODULE_PARM_DESC(wait_exec_default_timeout_ms, "Default timeout for DRM_IOCTL_QAIC_WAIT_BO");

static unsigned int datapath_poll_interval_us = 100; /* 100 usec default */
module_param(datapath_poll_interval_us, uint, 0600);
MODULE_PARM_DESC(datapath_poll_interval_us,
		 "Amount of time to sleep between activity when datapath polling is enabled");

struct dbc_req {
	/*
	 * A request ID is assigned to each memory handle going in DMA queue.
	 * As a single memory handle can enqueue multiple elements in DMA queue
	 * all of them will have the same request ID.
	 */
	__le16	req_id;
	/* Future use */
	__u8	seq_id;
	/*
	 * Special encoded variable
	 * 7	0 - Do not force to generate MSI after DMA is completed
	 *	1 - Force to generate MSI after DMA is completed
	 * 6:5	Reserved
	 * 4	1 - Generate completion element in the response queue
	 *	0 - No Completion Code
	 * 3	0 - DMA request is a Link list transfer
	 *	1 - DMA request is a Bulk transfer
	 * 2	Reserved
	 * 1:0	00 - No DMA transfer involved
	 *	01 - DMA transfer is part of inbound transfer
	 *	10 - DMA transfer has outbound transfer
	 *	11 - NA
	 */
	__u8	cmd;
	__le32	resv;
	/* Source address for the transfer */
	__le64	src_addr;
	/* Destination address for the transfer */
	__le64	dest_addr;
	/* Length of transfer request */
	__le32	len;
	__le32	resv2;
	/* Doorbell address */
	__le64	db_addr;
	/*
	 * Special encoded variable
	 * 7	1 - Doorbell(db) write
	 *	0 - No doorbell write
	 * 6:2	Reserved
	 * 1:0	00 - 32 bit access, db address must be aligned to 32bit-boundary
	 *	01 - 16 bit access, db address must be aligned to 16bit-boundary
	 *	10 - 8 bit access, db address must be aligned to 8bit-boundary
	 *	11 - Reserved
	 */
	__u8	db_len;
	__u8	resv3;
	__le16	resv4;
	/* 32 bit data written to doorbell address */
	__le32	db_data;
	/*
	 * Special encoded variable
	 * All the fields of sem_cmdX are passed from user and all are ORed
	 * together to form sem_cmd.
	 * 0:11		Semaphore value
	 * 15:12	Reserved
	 * 20:16	Semaphore index
	 * 21		Reserved
	 * 22		Semaphore Sync
	 * 23		Reserved
	 * 26:24	Semaphore command
	 * 28:27	Reserved
	 * 29		Semaphore DMA out bound sync fence
	 * 30		Semaphore DMA in bound sync fence
	 * 31		Enable semaphore command
	 */
	__le32	sem_cmd0;
	__le32	sem_cmd1;
	__le32	sem_cmd2;
	__le32	sem_cmd3;
} __packed;

struct dbc_rsp {
	/* Request ID of the memory handle whose DMA transaction is completed */
	__le16	req_id;
	/* Status of the DMA transaction. 0 : Success otherwise failure */
	__le16	status;
} __packed;

inline int get_dbc_req_elem_size(void)
{
	return sizeof(struct dbc_req);
}

inline int get_dbc_rsp_elem_size(void)
{
	return sizeof(struct dbc_rsp);
}

static void free_slice(struct kref *kref)
{
	struct bo_slice *slice = container_of(kref, struct bo_slice, ref_count);

	slice->bo->total_slice_nents -= slice->nents;
	list_del(&slice->slice);
	drm_gem_object_put(&slice->bo->base);
	sg_free_table(slice->sgt);
	kfree(slice->sgt);
	kfree(slice->reqs);
	kfree(slice);
}

static int clone_range_of_sgt_for_slice(struct qaic_device *qdev, struct sg_table **sgt_out,
					struct sg_table *sgt_in, u64 size, u64 offset)
{
	int total_len, len, nents, offf = 0, offl = 0;
	struct scatterlist *sg, *sgn, *sgf, *sgl;
	struct sg_table *sgt;
	int ret, j;

	/* find out number of relevant nents needed for this mem */
	total_len = 0;
	sgf = NULL;
	sgl = NULL;
	nents = 0;

	size = size ? size : PAGE_SIZE;
	for (sg = sgt_in->sgl; sg; sg = sg_next(sg)) {
		len = sg_dma_len(sg);

		if (!len)
			continue;
		if (offset >= total_len && offset < total_len + len) {
			sgf = sg;
			offf = offset - total_len;
		}
		if (sgf)
			nents++;
		if (offset + size >= total_len &&
		    offset + size <= total_len + len) {
			sgl = sg;
			offl = offset + size - total_len;
			break;
		}
		total_len += len;
	}

	if (!sgf || !sgl) {
		ret = -EINVAL;
		goto out;
	}

	sgt = kzalloc(sizeof(*sgt), GFP_KERNEL);
	if (!sgt) {
		ret = -ENOMEM;
		goto out;
	}

	ret = sg_alloc_table(sgt, nents, GFP_KERNEL);
	if (ret)
		goto free_sgt;

	/* copy relevant sg node and fix page and length */
	sgn = sgf;
	for_each_sgtable_sg(sgt, sg, j) {
		memcpy(sg, sgn, sizeof(*sg));
		if (sgn == sgf) {
			sg_dma_address(sg) += offf;
			sg_dma_len(sg) -= offf;
			sg_set_page(sg, sg_page(sgn), sg_dma_len(sg), offf);
		} else {
			offf = 0;
		}
		if (sgn == sgl) {
			sg_dma_len(sg) = offl - offf;
			sg_set_page(sg, sg_page(sgn), offl - offf, offf);
			sg_mark_end(sg);
			break;
		}
		sgn = sg_next(sgn);
	}

	*sgt_out = sgt;
	return ret;

free_sgt:
	kfree(sgt);
out:
	*sgt_out = NULL;
	return ret;
}

static int encode_reqs(struct qaic_device *qdev, struct bo_slice *slice,
		       struct qaic_attach_slice_entry *req)
{
	__le64 db_addr = cpu_to_le64(req->db_addr);
	__le32 db_data = cpu_to_le32(req->db_data);
	struct scatterlist *sg;
	__u8 cmd = BULK_XFER;
	int presync_sem;
	u64 dev_addr;
	__u8 db_len;
	int i;

	if (!slice->no_xfer)
		cmd |= (slice->dir == DMA_TO_DEVICE ? INBOUND_XFER : OUTBOUND_XFER);

	if (req->db_len && !IS_ALIGNED(req->db_addr, req->db_len / 8))
		return -EINVAL;

	presync_sem = req->sem0.presync + req->sem1.presync + req->sem2.presync + req->sem3.presync;
	if (presync_sem > 1)
		return -EINVAL;

	presync_sem = req->sem0.presync << 0 | req->sem1.presync << 1 |
		      req->sem2.presync << 2 | req->sem3.presync << 3;

	switch (req->db_len) {
	case 32:
		db_len = BIT(7);
		break;
	case 16:
		db_len = BIT(7) | 1;
		break;
	case 8:
		db_len = BIT(7) | 2;
		break;
	case 0:
		db_len = 0; /* doorbell is not active for this command */
		break;
	default:
		return -EINVAL; /* should never hit this */
	}

	/*
	 * When we end up splitting up a single request (ie a buf slice) into
	 * multiple DMA requests, we have to manage the sync data carefully.
	 * There can only be one presync sem. That needs to be on every xfer
	 * so that the DMA engine doesn't transfer data before the receiver is
	 * ready. We only do the doorbell and postsync sems after the xfer.
	 * To guarantee previous xfers for the request are complete, we use a
	 * fence.
	 */
	dev_addr = req->dev_addr;
	for_each_sgtable_sg(slice->sgt, sg, i) {
		slice->reqs[i].cmd = cmd;
		slice->reqs[i].src_addr = cpu_to_le64(slice->dir == DMA_TO_DEVICE ?
						      sg_dma_address(sg) : dev_addr);
		slice->reqs[i].dest_addr = cpu_to_le64(slice->dir == DMA_TO_DEVICE ?
						       dev_addr : sg_dma_address(sg));
		/*
		 * sg_dma_len(sg) returns size of a DMA segment, maximum DMA
		 * segment size is set to UINT_MAX by qaic and hence return
		 * values of sg_dma_len(sg) can never exceed u32 range. So,
		 * by down sizing we are not corrupting the value.
		 */
		slice->reqs[i].len = cpu_to_le32((u32)sg_dma_len(sg));
		switch (presync_sem) {
		case BIT(0):
			slice->reqs[i].sem_cmd0 = cpu_to_le32(ENCODE_SEM(req->sem0.val,
									 req->sem0.index,
									 req->sem0.presync,
									 req->sem0.cmd,
									 req->sem0.flags));
			break;
		case BIT(1):
			slice->reqs[i].sem_cmd1 = cpu_to_le32(ENCODE_SEM(req->sem1.val,
									 req->sem1.index,
									 req->sem1.presync,
									 req->sem1.cmd,
									 req->sem1.flags));
			break;
		case BIT(2):
			slice->reqs[i].sem_cmd2 = cpu_to_le32(ENCODE_SEM(req->sem2.val,
									 req->sem2.index,
									 req->sem2.presync,
									 req->sem2.cmd,
									 req->sem2.flags));
			break;
		case BIT(3):
			slice->reqs[i].sem_cmd3 = cpu_to_le32(ENCODE_SEM(req->sem3.val,
									 req->sem3.index,
									 req->sem3.presync,
									 req->sem3.cmd,
									 req->sem3.flags));
			break;
		}
		dev_addr += sg_dma_len(sg);
	}
	/* add post transfer stuff to last segment */
	i--;
	slice->reqs[i].cmd |= GEN_COMPLETION;
	slice->reqs[i].db_addr = db_addr;
	slice->reqs[i].db_len = db_len;
	slice->reqs[i].db_data = db_data;
	/*
	 * Add a fence if we have more than one request going to the hardware
	 * representing the entirety of the user request, and the user request
	 * has no presync condition.
	 * Fences are expensive, so we try to avoid them. We rely on the
	 * hardware behavior to avoid needing one when there is a presync
	 * condition. When a presync exists, all requests for that same
	 * presync will be queued into a fifo. Thus, since we queue the
	 * post xfer activity only on the last request we queue, the hardware
	 * will ensure that the last queued request is processed last, thus
	 * making sure the post xfer activity happens at the right time without
	 * a fence.
	 */
	if (i && !presync_sem)
		req->sem0.flags |= (slice->dir == DMA_TO_DEVICE ?
				    QAIC_SEM_INSYNCFENCE : QAIC_SEM_OUTSYNCFENCE);
	slice->reqs[i].sem_cmd0 = cpu_to_le32(ENCODE_SEM(req->sem0.val, req->sem0.index,
							 req->sem0.presync, req->sem0.cmd,
							 req->sem0.flags));
	slice->reqs[i].sem_cmd1 = cpu_to_le32(ENCODE_SEM(req->sem1.val, req->sem1.index,
							 req->sem1.presync, req->sem1.cmd,
							 req->sem1.flags));
	slice->reqs[i].sem_cmd2 = cpu_to_le32(ENCODE_SEM(req->sem2.val, req->sem2.index,
							 req->sem2.presync, req->sem2.cmd,
							 req->sem2.flags));
	slice->reqs[i].sem_cmd3 = cpu_to_le32(ENCODE_SEM(req->sem3.val, req->sem3.index,
							 req->sem3.presync, req->sem3.cmd,
							 req->sem3.flags));

	return 0;
}

static int qaic_map_one_slice(struct qaic_device *qdev, struct qaic_bo *bo,
			      struct qaic_attach_slice_entry *slice_ent)
{
	struct sg_table *sgt = NULL;
	struct bo_slice *slice;
	int ret;

	ret = clone_range_of_sgt_for_slice(qdev, &sgt, bo->sgt, slice_ent->size, slice_ent->offset);
	if (ret)
		goto out;

	slice = kmalloc(sizeof(*slice), GFP_KERNEL);
	if (!slice) {
		ret = -ENOMEM;
		goto free_sgt;
	}

	slice->reqs = kcalloc(sgt->nents, sizeof(*slice->reqs), GFP_KERNEL);
	if (!slice->reqs) {
		ret = -ENOMEM;
		goto free_slice;
	}

	slice->no_xfer = !slice_ent->size;
	slice->sgt = sgt;
	slice->nents = sgt->nents;
	slice->dir = bo->dir;
	slice->bo = bo;
	slice->size = slice_ent->size;
	slice->offset = slice_ent->offset;

	ret = encode_reqs(qdev, slice, slice_ent);
	if (ret)
		goto free_req;

	bo->total_slice_nents += sgt->nents;
	kref_init(&slice->ref_count);
	drm_gem_object_get(&bo->base);
	list_add_tail(&slice->slice, &bo->slices);

	return 0;

free_req:
	kfree(slice->reqs);
free_slice:
	kfree(slice);
free_sgt:
	sg_free_table(sgt);
	kfree(sgt);
out:
	return ret;
}

static int create_sgt(struct qaic_device *qdev, struct sg_table **sgt_out, u64 size)
{
	struct scatterlist *sg;
	struct sg_table *sgt;
	struct page **pages;
	int *pages_order;
	int buf_extra;
	int max_order;
	int nr_pages;
	int ret = 0;
	int i, j, k;
	int order;

	if (size) {
		nr_pages = DIV_ROUND_UP(size, PAGE_SIZE);
		/*
		 * calculate how much extra we are going to allocate, to remove
		 * later
		 */
		buf_extra = (PAGE_SIZE - size % PAGE_SIZE) % PAGE_SIZE;
		max_order = min(MAX_ORDER - 1, get_order(size));
	} else {
		/* allocate a single page for book keeping */
		nr_pages = 1;
		buf_extra = 0;
		max_order = 0;
	}

	pages = kvmalloc_array(nr_pages, sizeof(*pages) + sizeof(*pages_order), GFP_KERNEL);
	if (!pages) {
		ret = -ENOMEM;
		goto out;
	}
	pages_order = (void *)pages + sizeof(*pages) * nr_pages;

	/*
	 * Allocate requested memory using alloc_pages. It is possible to allocate
	 * the requested memory in multiple chunks by calling alloc_pages
	 * multiple times. Use SG table to handle multiple allocated pages.
	 */
	i = 0;
	while (nr_pages > 0) {
		order = min(get_order(nr_pages * PAGE_SIZE), max_order);
		while (1) {
			pages[i] = alloc_pages(GFP_KERNEL | GFP_HIGHUSER |
					       __GFP_NOWARN | __GFP_ZERO |
					       (order ? __GFP_NORETRY : __GFP_RETRY_MAYFAIL),
					       order);
			if (pages[i])
				break;
			if (!order--) {
				ret = -ENOMEM;
				goto free_partial_alloc;
			}
		}

		max_order = order;
		pages_order[i] = order;

		nr_pages -= 1 << order;
		if (nr_pages <= 0)
			/* account for over allocation */
			buf_extra += abs(nr_pages) * PAGE_SIZE;
		i++;
	}

	sgt = kmalloc(sizeof(*sgt), GFP_KERNEL);
	if (!sgt) {
		ret = -ENOMEM;
		goto free_partial_alloc;
	}

	if (sg_alloc_table(sgt, i, GFP_KERNEL)) {
		ret = -ENOMEM;
		goto free_sgt;
	}

	/* Populate the SG table with the allocated memory pages */
	sg = sgt->sgl;
	for (k = 0; k < i; k++, sg = sg_next(sg)) {
		/* Last entry requires special handling */
		if (k < i - 1) {
			sg_set_page(sg, pages[k], PAGE_SIZE << pages_order[k], 0);
		} else {
			sg_set_page(sg, pages[k], (PAGE_SIZE << pages_order[k]) - buf_extra, 0);
			sg_mark_end(sg);
		}
	}

	kvfree(pages);
	*sgt_out = sgt;
	return ret;

free_sgt:
	kfree(sgt);
free_partial_alloc:
	for (j = 0; j < i; j++)
		__free_pages(pages[j], pages_order[j]);
	kvfree(pages);
out:
	*sgt_out = NULL;
	return ret;
}

static bool invalid_sem(struct qaic_sem *sem)
{
	if (sem->val & ~SEM_VAL_MASK || sem->index & ~SEM_INDEX_MASK ||
	    !(sem->presync == 0 || sem->presync == 1) || sem->pad ||
	    sem->flags & ~(QAIC_SEM_INSYNCFENCE | QAIC_SEM_OUTSYNCFENCE) ||
	    sem->cmd > QAIC_SEM_WAIT_GT_0)
		return true;
	return false;
}

static int qaic_validate_req(struct qaic_device *qdev, struct qaic_attach_slice_entry *slice_ent,
			     u32 count, u64 total_size)
{
	int i;

	for (i = 0; i < count; i++) {
		if (!(slice_ent[i].db_len == 32 || slice_ent[i].db_len == 16 ||
		      slice_ent[i].db_len == 8 || slice_ent[i].db_len == 0) ||
		      invalid_sem(&slice_ent[i].sem0) || invalid_sem(&slice_ent[i].sem1) ||
		      invalid_sem(&slice_ent[i].sem2) || invalid_sem(&slice_ent[i].sem3))
			return -EINVAL;

		if (slice_ent[i].offset + slice_ent[i].size > total_size)
			return -EINVAL;
	}

	return 0;
}

static void qaic_free_sgt(struct sg_table *sgt)
{
	struct scatterlist *sg;

	for (sg = sgt->sgl; sg; sg = sg_next(sg))
		if (sg_page(sg))
			__free_pages(sg_page(sg), get_order(sg->length));
	sg_free_table(sgt);
	kfree(sgt);
}

static void qaic_gem_print_info(struct drm_printer *p, unsigned int indent,
				const struct drm_gem_object *obj)
{
	struct qaic_bo *bo = to_qaic_bo(obj);

	drm_printf_indent(p, indent, "BO DMA direction %d\n", bo->dir);
}

static const struct vm_operations_struct drm_vm_ops = {
	.open = drm_gem_vm_open,
	.close = drm_gem_vm_close,
};

static int qaic_gem_object_mmap(struct drm_gem_object *obj, struct vm_area_struct *vma)
{
	struct qaic_bo *bo = to_qaic_bo(obj);
	unsigned long offset = 0;
	struct scatterlist *sg;
	int ret = 0;

	if (obj->import_attach)
		return -EINVAL;

	for (sg = bo->sgt->sgl; sg; sg = sg_next(sg)) {
		if (sg_page(sg)) {
			ret = remap_pfn_range(vma, vma->vm_start + offset, page_to_pfn(sg_page(sg)),
					      sg->length, vma->vm_page_prot);
			if (ret)
				goto out;
			offset += sg->length;
		}
	}

out:
	return ret;
}

static void qaic_free_object(struct drm_gem_object *obj)
{
	struct qaic_bo *bo = to_qaic_bo(obj);

	if (obj->import_attach) {
		/* DMABUF/PRIME Path */
		drm_prime_gem_destroy(obj, NULL);
	} else {
		/* Private buffer allocation path */
		qaic_free_sgt(bo->sgt);
	}

	mutex_destroy(&bo->lock);
	drm_gem_object_release(obj);
	kfree(bo);
}

static const struct drm_gem_object_funcs qaic_gem_funcs = {
	.free = qaic_free_object,
	.print_info = qaic_gem_print_info,
	.mmap = qaic_gem_object_mmap,
	.vm_ops = &drm_vm_ops,
};

static void qaic_init_bo(struct qaic_bo *bo, bool reinit)
{
	if (reinit) {
		bo->sliced = false;
		reinit_completion(&bo->xfer_done);
	} else {
		mutex_init(&bo->lock);
		init_completion(&bo->xfer_done);
	}
	complete_all(&bo->xfer_done);
	INIT_LIST_HEAD(&bo->slices);
}

static struct qaic_bo *qaic_alloc_init_bo(void)
{
	struct qaic_bo *bo;

	bo = kzalloc(sizeof(*bo), GFP_KERNEL);
	if (!bo)
		return ERR_PTR(-ENOMEM);

	qaic_init_bo(bo, false);

	return bo;
}

int qaic_create_bo_ioctl(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	struct qaic_create_bo *args = data;
	int usr_rcu_id, qdev_rcu_id;
	struct drm_gem_object *obj;
	struct qaic_device *qdev;
	struct qaic_user *usr;
	struct qaic_bo *bo;
	size_t size;
	int ret;

	if (args->pad)
		return -EINVAL;

	size = PAGE_ALIGN(args->size);
	if (size == 0)
		return -EINVAL;

	usr = file_priv->driver_priv;
	usr_rcu_id = srcu_read_lock(&usr->qddev_lock);
	if (!usr->qddev) {
		ret = -ENODEV;
		goto unlock_usr_srcu;
	}

	qdev = usr->qddev->qdev;
	qdev_rcu_id = srcu_read_lock(&qdev->dev_lock);
	if (qdev->in_reset) {
		ret = -ENODEV;
		goto unlock_dev_srcu;
	}

	bo = qaic_alloc_init_bo();
	if (IS_ERR(bo)) {
		ret = PTR_ERR(bo);
		goto unlock_dev_srcu;
	}
	obj = &bo->base;

	drm_gem_private_object_init(dev, obj, size);

	obj->funcs = &qaic_gem_funcs;
	ret = create_sgt(qdev, &bo->sgt, size);
	if (ret)
		goto free_bo;

	ret = drm_gem_handle_create(file_priv, obj, &args->handle);
	if (ret)
		goto free_sgt;

	bo->handle = args->handle;
	drm_gem_object_put(obj);
	srcu_read_unlock(&qdev->dev_lock, qdev_rcu_id);
	srcu_read_unlock(&usr->qddev_lock, usr_rcu_id);

	return 0;

free_sgt:
	qaic_free_sgt(bo->sgt);
free_bo:
	kfree(bo);
unlock_dev_srcu:
	srcu_read_unlock(&qdev->dev_lock, qdev_rcu_id);
unlock_usr_srcu:
	srcu_read_unlock(&usr->qddev_lock, usr_rcu_id);
	return ret;
}

int qaic_mmap_bo_ioctl(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	struct qaic_mmap_bo *args = data;
	int usr_rcu_id, qdev_rcu_id;
	struct drm_gem_object *obj;
	struct qaic_device *qdev;
	struct qaic_user *usr;
	int ret;

	usr = file_priv->driver_priv;
	usr_rcu_id = srcu_read_lock(&usr->qddev_lock);
	if (!usr->qddev) {
		ret = -ENODEV;
		goto unlock_usr_srcu;
	}

	qdev = usr->qddev->qdev;
	qdev_rcu_id = srcu_read_lock(&qdev->dev_lock);
	if (qdev->in_reset) {
		ret = -ENODEV;
		goto unlock_dev_srcu;
	}

	obj = drm_gem_object_lookup(file_priv, args->handle);
	if (!obj) {
		ret = -ENOENT;
		goto unlock_dev_srcu;
	}

	ret = drm_gem_create_mmap_offset(obj);
	if (ret == 0)
		args->offset = drm_vma_node_offset_addr(&obj->vma_node);

	drm_gem_object_put(obj);

unlock_dev_srcu:
	srcu_read_unlock(&qdev->dev_lock, qdev_rcu_id);
unlock_usr_srcu:
	srcu_read_unlock(&usr->qddev_lock, usr_rcu_id);
	return ret;
}

struct drm_gem_object *qaic_gem_prime_import(struct drm_device *dev, struct dma_buf *dma_buf)
{
	struct dma_buf_attachment *attach;
	struct drm_gem_object *obj;
	struct qaic_bo *bo;
	size_t size;
	int ret;

	bo = qaic_alloc_init_bo();
	if (IS_ERR(bo)) {
		ret = PTR_ERR(bo);
		goto out;
	}

	obj = &bo->base;
	get_dma_buf(dma_buf);

	attach = dma_buf_attach(dma_buf, dev->dev);
	if (IS_ERR(attach)) {
		ret = PTR_ERR(attach);
		goto attach_fail;
	}

	size = PAGE_ALIGN(attach->dmabuf->size);
	if (size == 0) {
		ret = -EINVAL;
		goto size_align_fail;
	}

	drm_gem_private_object_init(dev, obj, size);
	/*
	 * skipping dma_buf_map_attachment() as we do not know the direction
	 * just yet. Once the direction is known in the subsequent IOCTL to
	 * attach slicing, we can do it then.
	 */

	obj->funcs = &qaic_gem_funcs;
	obj->import_attach = attach;
	obj->resv = dma_buf->resv;

	return obj;

size_align_fail:
	dma_buf_detach(dma_buf, attach);
attach_fail:
	dma_buf_put(dma_buf);
	kfree(bo);
out:
	return ERR_PTR(ret);
}

static int qaic_prepare_import_bo(struct qaic_bo *bo, struct qaic_attach_slice_hdr *hdr)
{
	struct drm_gem_object *obj = &bo->base;
	struct sg_table *sgt;
	int ret;

	if (obj->import_attach->dmabuf->size < hdr->size)
		return -EINVAL;

	sgt = dma_buf_map_attachment(obj->import_attach, hdr->dir);
	if (IS_ERR(sgt)) {
		ret = PTR_ERR(sgt);
		return ret;
	}

	bo->sgt = sgt;

	return 0;
}

static int qaic_prepare_export_bo(struct qaic_device *qdev, struct qaic_bo *bo,
				  struct qaic_attach_slice_hdr *hdr)
{
	int ret;

	if (bo->base.size < hdr->size)
		return -EINVAL;

	ret = dma_map_sgtable(&qdev->pdev->dev, bo->sgt, hdr->dir, 0);
	if (ret)
		return -EFAULT;

	return 0;
}

static int qaic_prepare_bo(struct qaic_device *qdev, struct qaic_bo *bo,
			   struct qaic_attach_slice_hdr *hdr)
{
	int ret;

	if (bo->base.import_attach)
		ret = qaic_prepare_import_bo(bo, hdr);
	else
		ret = qaic_prepare_export_bo(qdev, bo, hdr);
	bo->dir = hdr->dir;
	bo->dbc = &qdev->dbc[hdr->dbc_id];
	bo->nr_slice = hdr->count;

	return ret;
}

static void qaic_unprepare_import_bo(struct qaic_bo *bo)
{
	dma_buf_unmap_attachment(bo->base.import_attach, bo->sgt, bo->dir);
	bo->sgt = NULL;
}

static void qaic_unprepare_export_bo(struct qaic_device *qdev, struct qaic_bo *bo)
{
	dma_unmap_sgtable(&qdev->pdev->dev, bo->sgt, bo->dir, 0);
}

static void qaic_unprepare_bo(struct qaic_device *qdev, struct qaic_bo *bo)
{
	if (bo->base.import_attach)
		qaic_unprepare_import_bo(bo);
	else
		qaic_unprepare_export_bo(qdev, bo);

	bo->dir = 0;
	bo->dbc = NULL;
	bo->nr_slice = 0;
}

static void qaic_free_slices_bo(struct qaic_bo *bo)
{
	struct bo_slice *slice, *temp;

	list_for_each_entry_safe(slice, temp, &bo->slices, slice)
		kref_put(&slice->ref_count, free_slice);
	if (WARN_ON_ONCE(bo->total_slice_nents != 0))
		bo->total_slice_nents = 0;
	bo->nr_slice = 0;
}

static int qaic_attach_slicing_bo(struct qaic_device *qdev, struct qaic_bo *bo,
				  struct qaic_attach_slice_hdr *hdr,
				  struct qaic_attach_slice_entry *slice_ent)
{
	int ret, i;

	for (i = 0; i < hdr->count; i++) {
		ret = qaic_map_one_slice(qdev, bo, &slice_ent[i]);
		if (ret) {
			qaic_free_slices_bo(bo);
			return ret;
		}
	}

	if (bo->total_slice_nents > bo->dbc->nelem) {
		qaic_free_slices_bo(bo);
		return -ENOSPC;
	}

	return 0;
}

int qaic_attach_slice_bo_ioctl(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	struct qaic_attach_slice_entry *slice_ent;
	struct qaic_attach_slice *args = data;
	int rcu_id, usr_rcu_id, qdev_rcu_id;
	struct dma_bridge_chan	*dbc;
	struct drm_gem_object *obj;
	struct qaic_device *qdev;
	unsigned long arg_size;
	struct qaic_user *usr;
	u8 __user *user_data;
	struct qaic_bo *bo;
	int ret;

	if (args->hdr.count == 0)
		return -EINVAL;

	arg_size = args->hdr.count * sizeof(*slice_ent);
	if (arg_size / args->hdr.count != sizeof(*slice_ent))
		return -EINVAL;

	if (args->hdr.size == 0)
		return -EINVAL;

	if (!(args->hdr.dir == DMA_TO_DEVICE || args->hdr.dir == DMA_FROM_DEVICE))
		return -EINVAL;

	if (args->data == 0)
		return -EINVAL;

	usr = file_priv->driver_priv;
	usr_rcu_id = srcu_read_lock(&usr->qddev_lock);
	if (!usr->qddev) {
		ret = -ENODEV;
		goto unlock_usr_srcu;
	}

	qdev = usr->qddev->qdev;
	qdev_rcu_id = srcu_read_lock(&qdev->dev_lock);
	if (qdev->in_reset) {
		ret = -ENODEV;
		goto unlock_dev_srcu;
	}

	if (args->hdr.dbc_id >= qdev->num_dbc) {
		ret = -EINVAL;
		goto unlock_dev_srcu;
	}

	user_data = u64_to_user_ptr(args->data);

	slice_ent = kzalloc(arg_size, GFP_KERNEL);
	if (!slice_ent) {
		ret = -EINVAL;
		goto unlock_dev_srcu;
	}

	ret = copy_from_user(slice_ent, user_data, arg_size);
	if (ret) {
		ret = -EFAULT;
		goto free_slice_ent;
	}

	ret = qaic_validate_req(qdev, slice_ent, args->hdr.count, args->hdr.size);
	if (ret)
		goto free_slice_ent;

	obj = drm_gem_object_lookup(file_priv, args->hdr.handle);
	if (!obj) {
		ret = -ENOENT;
		goto free_slice_ent;
	}

	bo = to_qaic_bo(obj);
	ret = mutex_lock_interruptible(&bo->lock);
	if (ret)
		goto put_bo;

	if (bo->sliced) {
		ret = -EINVAL;
		goto unlock_bo;
	}

	dbc = &qdev->dbc[args->hdr.dbc_id];
	rcu_id = srcu_read_lock(&dbc->ch_lock);
	if (dbc->usr != usr) {
		ret = -EINVAL;
		goto unlock_ch_srcu;
	}

	ret = qaic_prepare_bo(qdev, bo, &args->hdr);
	if (ret)
		goto unlock_ch_srcu;

	ret = qaic_attach_slicing_bo(qdev, bo, &args->hdr, slice_ent);
	if (ret)
		goto unprepare_bo;

	if (args->hdr.dir == DMA_TO_DEVICE)
		dma_sync_sgtable_for_cpu(&qdev->pdev->dev, bo->sgt, args->hdr.dir);

	bo->sliced = true;
	list_add_tail(&bo->bo_list, &bo->dbc->bo_lists);
	srcu_read_unlock(&dbc->ch_lock, rcu_id);
	mutex_unlock(&bo->lock);
	kfree(slice_ent);
	srcu_read_unlock(&qdev->dev_lock, qdev_rcu_id);
	srcu_read_unlock(&usr->qddev_lock, usr_rcu_id);

	return 0;

unprepare_bo:
	qaic_unprepare_bo(qdev, bo);
unlock_ch_srcu:
	srcu_read_unlock(&dbc->ch_lock, rcu_id);
unlock_bo:
	mutex_unlock(&bo->lock);
put_bo:
	drm_gem_object_put(obj);
free_slice_ent:
	kfree(slice_ent);
unlock_dev_srcu:
	srcu_read_unlock(&qdev->dev_lock, qdev_rcu_id);
unlock_usr_srcu:
	srcu_read_unlock(&usr->qddev_lock, usr_rcu_id);
	return ret;
}

static inline int copy_exec_reqs(struct qaic_device *qdev, struct bo_slice *slice, u32 dbc_id,
				 u32 head, u32 *ptail)
{
	struct dma_bridge_chan *dbc = &qdev->dbc[dbc_id];
	struct dbc_req *reqs = slice->reqs;
	u32 tail = *ptail;
	u32 avail;

	avail = head - tail;
	if (head <= tail)
		avail += dbc->nelem;

	--avail;

	if (avail < slice->nents)
		return -EAGAIN;

	if (tail + slice->nents > dbc->nelem) {
		avail = dbc->nelem - tail;
		avail = min_t(u32, avail, slice->nents);
		memcpy(dbc->req_q_base + tail * get_dbc_req_elem_size(), reqs,
		       sizeof(*reqs) * avail);
		reqs += avail;
		avail = slice->nents - avail;
		if (avail)
			memcpy(dbc->req_q_base, reqs, sizeof(*reqs) * avail);
	} else {
		memcpy(dbc->req_q_base + tail * get_dbc_req_elem_size(), reqs,
		       sizeof(*reqs) * slice->nents);
	}

	*ptail = (tail + slice->nents) % dbc->nelem;

	return 0;
}

/*
 * Based on the value of resize we may only need to transmit first_n
 * entries and the last entry, with last_bytes to send from the last entry.
 * Note that first_n could be 0.
 */
static inline int copy_partial_exec_reqs(struct qaic_device *qdev, struct bo_slice *slice,
					 u64 resize, u32 dbc_id, u32 head, u32 *ptail)
{
	struct dma_bridge_chan *dbc = &qdev->dbc[dbc_id];
	struct dbc_req *reqs = slice->reqs;
	struct dbc_req *last_req;
	u32 tail = *ptail;
	u64 total_bytes;
	u64 last_bytes;
	u32 first_n;
	u32 avail;
	int ret;
	int i;

	avail = head - tail;
	if (head <= tail)
		avail += dbc->nelem;

	--avail;

	total_bytes = 0;
	for (i = 0; i < slice->nents; i++) {
		total_bytes += le32_to_cpu(reqs[i].len);
		if (total_bytes >= resize)
			break;
	}

	if (total_bytes < resize) {
		/* User space should have used the full buffer path. */
		ret = -EINVAL;
		return ret;
	}

	first_n = i;
	last_bytes = i ? resize + le32_to_cpu(reqs[i].len) - total_bytes : resize;

	if (avail < (first_n + 1))
		return -EAGAIN;

	if (first_n) {
		if (tail + first_n > dbc->nelem) {
			avail = dbc->nelem - tail;
			avail = min_t(u32, avail, first_n);
			memcpy(dbc->req_q_base + tail * get_dbc_req_elem_size(), reqs,
			       sizeof(*reqs) * avail);
			last_req = reqs + avail;
			avail = first_n - avail;
			if (avail)
				memcpy(dbc->req_q_base, last_req, sizeof(*reqs) * avail);
		} else {
			memcpy(dbc->req_q_base + tail * get_dbc_req_elem_size(), reqs,
			       sizeof(*reqs) * first_n);
		}
	}

	/* Copy over the last entry. Here we need to adjust len to the left over
	 * size, and set src and dst to the entry it is copied to.
	 */
	last_req = dbc->req_q_base + (tail + first_n) % dbc->nelem * get_dbc_req_elem_size();
	memcpy(last_req, reqs + slice->nents - 1, sizeof(*reqs));

	/*
	 * last_bytes holds size of a DMA segment, maximum DMA segment size is
	 * set to UINT_MAX by qaic and hence last_bytes can never exceed u32
	 * range. So, by down sizing we are not corrupting the value.
	 */
	last_req->len = cpu_to_le32((u32)last_bytes);
	last_req->src_addr = reqs[first_n].src_addr;
	last_req->dest_addr = reqs[first_n].dest_addr;

	*ptail = (tail + first_n + 1) % dbc->nelem;

	return 0;
}

static int send_bo_list_to_device(struct qaic_device *qdev, struct drm_file *file_priv,
				  struct qaic_execute_entry *exec, unsigned int count,
				  bool is_partial, struct dma_bridge_chan *dbc, u32 head,
				  u32 *tail)
{
	struct qaic_partial_execute_entry *pexec = (struct qaic_partial_execute_entry *)exec;
	struct drm_gem_object *obj;
	struct bo_slice *slice;
	unsigned long flags;
	struct qaic_bo *bo;
	bool queued;
	int i, j;
	int ret;

	for (i = 0; i < count; i++) {
		/*
		 * ref count will be decremented when the transfer of this
		 * buffer is complete. It is inside dbc_irq_threaded_fn().
		 */
		obj = drm_gem_object_lookup(file_priv,
					    is_partial ? pexec[i].handle : exec[i].handle);
		if (!obj) {
			ret = -ENOENT;
			goto failed_to_send_bo;
		}

		bo = to_qaic_bo(obj);
		ret = mutex_lock_interruptible(&bo->lock);
		if (ret)
			goto failed_to_send_bo;

		if (!bo->sliced) {
			ret = -EINVAL;
			goto unlock_bo;
		}

		if (is_partial && pexec[i].resize > bo->base.size) {
			ret = -EINVAL;
			goto unlock_bo;
		}

		spin_lock_irqsave(&dbc->xfer_lock, flags);
		queued = bo->queued;
		bo->queued = true;
		if (queued) {
			spin_unlock_irqrestore(&dbc->xfer_lock, flags);
			ret = -EINVAL;
			goto unlock_bo;
		}

		bo->req_id = dbc->next_req_id++;

		list_for_each_entry(slice, &bo->slices, slice) {
			/*
			 * If this slice does not fall under the given
			 * resize then skip this slice and continue the loop
			 */
			if (is_partial && pexec[i].resize && pexec[i].resize <= slice->offset)
				continue;

			for (j = 0; j < slice->nents; j++)
				slice->reqs[j].req_id = cpu_to_le16(bo->req_id);

			/*
			 * If it is a partial execute ioctl call then check if
			 * resize has cut this slice short then do a partial copy
			 * else do complete copy
			 */
			if (is_partial && pexec[i].resize &&
			    pexec[i].resize < slice->offset + slice->size)
				ret = copy_partial_exec_reqs(qdev, slice,
							     pexec[i].resize - slice->offset,
							     dbc->id, head, tail);
			else
				ret = copy_exec_reqs(qdev, slice, dbc->id, head, tail);
			if (ret) {
				bo->queued = false;
				spin_unlock_irqrestore(&dbc->xfer_lock, flags);
				goto unlock_bo;
			}
		}
		reinit_completion(&bo->xfer_done);
		list_add_tail(&bo->xfer_list, &dbc->xfer_list);
		spin_unlock_irqrestore(&dbc->xfer_lock, flags);
		dma_sync_sgtable_for_device(&qdev->pdev->dev, bo->sgt, bo->dir);
		mutex_unlock(&bo->lock);
	}

	return 0;

unlock_bo:
	mutex_unlock(&bo->lock);
failed_to_send_bo:
	if (likely(obj))
		drm_gem_object_put(obj);
	for (j = 0; j < i; j++) {
		spin_lock_irqsave(&dbc->xfer_lock, flags);
		bo = list_last_entry(&dbc->xfer_list, struct qaic_bo, xfer_list);
		obj = &bo->base;
		bo->queued = false;
		list_del(&bo->xfer_list);
		spin_unlock_irqrestore(&dbc->xfer_lock, flags);
		dma_sync_sgtable_for_cpu(&qdev->pdev->dev, bo->sgt, bo->dir);
		drm_gem_object_put(obj);
	}
	return ret;
}

static void update_profiling_data(struct drm_file *file_priv,
				  struct qaic_execute_entry *exec, unsigned int count,
				  bool is_partial, u64 received_ts, u64 submit_ts, u32 queue_level)
{
	struct qaic_partial_execute_entry *pexec = (struct qaic_partial_execute_entry *)exec;
	struct drm_gem_object *obj;
	struct qaic_bo *bo;
	int i;

	for (i = 0; i < count; i++) {
		/*
		 * Since we already committed the BO to hardware, the only way
		 * this should fail is a pending signal. We can't cancel the
		 * submit to hardware, so we have to just skip the profiling
		 * data. In case the signal is not fatal to the process, we
		 * return success so that the user doesn't try to resubmit.
		 */
		obj = drm_gem_object_lookup(file_priv,
					    is_partial ? pexec[i].handle : exec[i].handle);
		if (!obj)
			break;
		bo = to_qaic_bo(obj);
		bo->perf_stats.req_received_ts = received_ts;
		bo->perf_stats.req_submit_ts = submit_ts;
		bo->perf_stats.queue_level_before = queue_level;
		queue_level += bo->total_slice_nents;
		drm_gem_object_put(obj);
	}
}

static int __qaic_execute_bo_ioctl(struct drm_device *dev, void *data, struct drm_file *file_priv,
				   bool is_partial)
{
	struct qaic_execute *args = data;
	struct qaic_execute_entry *exec;
	struct dma_bridge_chan *dbc;
	int usr_rcu_id, qdev_rcu_id;
	struct qaic_device *qdev;
	struct qaic_user *usr;
	u8 __user *user_data;
	unsigned long n;
	u64 received_ts;
	u32 queue_level;
	u64 submit_ts;
	int rcu_id;
	u32 head;
	u32 tail;
	u64 size;
	int ret;

	received_ts = ktime_get_ns();

	size = is_partial ? sizeof(struct qaic_partial_execute_entry) : sizeof(*exec);
	n = (unsigned long)size * args->hdr.count;
	if (args->hdr.count == 0 || n / args->hdr.count != size)
		return -EINVAL;

	user_data = u64_to_user_ptr(args->data);

	exec = kcalloc(args->hdr.count, size, GFP_KERNEL);
	if (!exec)
		return -ENOMEM;

	if (copy_from_user(exec, user_data, n)) {
		ret = -EFAULT;
		goto free_exec;
	}

	usr = file_priv->driver_priv;
	usr_rcu_id = srcu_read_lock(&usr->qddev_lock);
	if (!usr->qddev) {
		ret = -ENODEV;
		goto unlock_usr_srcu;
	}

	qdev = usr->qddev->qdev;
	qdev_rcu_id = srcu_read_lock(&qdev->dev_lock);
	if (qdev->in_reset) {
		ret = -ENODEV;
		goto unlock_dev_srcu;
	}

	if (args->hdr.dbc_id >= qdev->num_dbc) {
		ret = -EINVAL;
		goto unlock_dev_srcu;
	}

	dbc = &qdev->dbc[args->hdr.dbc_id];

	rcu_id = srcu_read_lock(&dbc->ch_lock);
	if (!dbc->usr || dbc->usr->handle != usr->handle) {
		ret = -EPERM;
		goto release_ch_rcu;
	}

	head = readl(dbc->dbc_base + REQHP_OFF);
	tail = readl(dbc->dbc_base + REQTP_OFF);

	if (head == U32_MAX || tail == U32_MAX) {
		/* PCI link error */
		ret = -ENODEV;
		goto release_ch_rcu;
	}

	queue_level = head <= tail ? tail - head : dbc->nelem - (head - tail);

	ret = send_bo_list_to_device(qdev, file_priv, exec, args->hdr.count, is_partial, dbc,
				     head, &tail);
	if (ret)
		goto release_ch_rcu;

	/* Finalize commit to hardware */
	submit_ts = ktime_get_ns();
	writel(tail, dbc->dbc_base + REQTP_OFF);

	update_profiling_data(file_priv, exec, args->hdr.count, is_partial, received_ts,
			      submit_ts, queue_level);

	if (datapath_polling)
		schedule_work(&dbc->poll_work);

release_ch_rcu:
	srcu_read_unlock(&dbc->ch_lock, rcu_id);
unlock_dev_srcu:
	srcu_read_unlock(&qdev->dev_lock, qdev_rcu_id);
unlock_usr_srcu:
	srcu_read_unlock(&usr->qddev_lock, usr_rcu_id);
free_exec:
	kfree(exec);
	return ret;
}

int qaic_execute_bo_ioctl(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	return __qaic_execute_bo_ioctl(dev, data, file_priv, false);
}

int qaic_partial_execute_bo_ioctl(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	return __qaic_execute_bo_ioctl(dev, data, file_priv, true);
}

/*
 * Our interrupt handling is a bit more complicated than a simple ideal, but
 * sadly necessary.
 *
 * Each dbc has a completion queue. Entries in the queue correspond to DMA
 * requests which the device has processed. The hardware already has a built
 * in irq mitigation. When the device puts an entry into the queue, it will
 * only trigger an interrupt if the queue was empty. Therefore, when adding
 * the Nth event to a non-empty queue, the hardware doesn't trigger an
 * interrupt. This means the host doesn't get additional interrupts signaling
 * the same thing - the queue has something to process.
 * This behavior can be overridden in the DMA request.
 * This means that when the host receives an interrupt, it is required to
 * drain the queue.
 *
 * This behavior is what NAPI attempts to accomplish, although we can't use
 * NAPI as we don't have a netdev. We use threaded irqs instead.
 *
 * However, there is a situation where the host drains the queue fast enough
 * that every event causes an interrupt. Typically this is not a problem as
 * the rate of events would be low. However, that is not the case with
 * lprnet for example. On an Intel Xeon D-2191 where we run 8 instances of
 * lprnet, the host receives roughly 80k interrupts per second from the device
 * (per /proc/interrupts). While NAPI documentation indicates the host should
 * just chug along, sadly that behavior causes instability in some hosts.
 *
 * Therefore, we implement an interrupt disable scheme similar to NAPI. The
 * key difference is that we will delay after draining the queue for a small
 * time to allow additional events to come in via polling. Using the above
 * lprnet workload, this reduces the number of interrupts processed from
 * ~80k/sec to about 64 in 5 minutes and appears to solve the system
 * instability.
 */
irqreturn_t dbc_irq_handler(int irq, void *data)
{
	struct dma_bridge_chan *dbc = data;
	int rcu_id;
	u32 head;
	u32 tail;

	rcu_id = srcu_read_lock(&dbc->ch_lock);

	if (!dbc->usr) {
		srcu_read_unlock(&dbc->ch_lock, rcu_id);
		return IRQ_HANDLED;
	}

	head = readl(dbc->dbc_base + RSPHP_OFF);
	if (head == U32_MAX) { /* PCI link error */
		srcu_read_unlock(&dbc->ch_lock, rcu_id);
		return IRQ_NONE;
	}

	tail = readl(dbc->dbc_base + RSPTP_OFF);
	if (tail == U32_MAX) { /* PCI link error */
		srcu_read_unlock(&dbc->ch_lock, rcu_id);
		return IRQ_NONE;
	}

	if (head == tail) { /* queue empty */
		srcu_read_unlock(&dbc->ch_lock, rcu_id);
		return IRQ_NONE;
	}

	disable_irq_nosync(irq);
	srcu_read_unlock(&dbc->ch_lock, rcu_id);
	return IRQ_WAKE_THREAD;
}

void irq_polling_work(struct work_struct *work)
{
	struct dma_bridge_chan *dbc = container_of(work, struct dma_bridge_chan,  poll_work);
	unsigned long flags;
	int rcu_id;
	u32 head;
	u32 tail;

	rcu_id = srcu_read_lock(&dbc->ch_lock);

	while (1) {
		if (dbc->qdev->in_reset) {
			srcu_read_unlock(&dbc->ch_lock, rcu_id);
			return;
		}
		if (!dbc->usr) {
			srcu_read_unlock(&dbc->ch_lock, rcu_id);
			return;
		}
		spin_lock_irqsave(&dbc->xfer_lock, flags);
		if (list_empty(&dbc->xfer_list)) {
			spin_unlock_irqrestore(&dbc->xfer_lock, flags);
			srcu_read_unlock(&dbc->ch_lock, rcu_id);
			return;
		}
		spin_unlock_irqrestore(&dbc->xfer_lock, flags);

		head = readl(dbc->dbc_base + RSPHP_OFF);
		if (head == U32_MAX) { /* PCI link error */
			srcu_read_unlock(&dbc->ch_lock, rcu_id);
			return;
		}

		tail = readl(dbc->dbc_base + RSPTP_OFF);
		if (tail == U32_MAX) { /* PCI link error */
			srcu_read_unlock(&dbc->ch_lock, rcu_id);
			return;
		}

		if (head != tail) {
			irq_wake_thread(dbc->irq, dbc);
			srcu_read_unlock(&dbc->ch_lock, rcu_id);
			return;
		}

		cond_resched();
		usleep_range(datapath_poll_interval_us, 2 * datapath_poll_interval_us);
	}
}

irqreturn_t dbc_irq_threaded_fn(int irq, void *data)
{
	struct dma_bridge_chan *dbc = data;
	int event_count = NUM_EVENTS;
	int delay_count = NUM_DELAYS;
	struct qaic_device *qdev;
	struct qaic_bo *bo, *i;
	struct dbc_rsp *rsp;
	unsigned long flags;
	int rcu_id;
	u16 status;
	u16 req_id;
	u32 head;
	u32 tail;

	rcu_id = srcu_read_lock(&dbc->ch_lock);

	head = readl(dbc->dbc_base + RSPHP_OFF);
	if (head == U32_MAX) /* PCI link error */
		goto error_out;

	qdev = dbc->qdev;
read_fifo:

	if (!event_count) {
		event_count = NUM_EVENTS;
		cond_resched();
	}

	/*
	 * if this channel isn't assigned or gets unassigned during processing
	 * we have nothing further to do
	 */
	if (!dbc->usr)
		goto error_out;

	tail = readl(dbc->dbc_base + RSPTP_OFF);
	if (tail == U32_MAX) /* PCI link error */
		goto error_out;

	if (head == tail) { /* queue empty */
		if (delay_count) {
			--delay_count;
			usleep_range(100, 200);
			goto read_fifo; /* check for a new event */
		}
		goto normal_out;
	}

	delay_count = NUM_DELAYS;
	while (head != tail) {
		if (!event_count)
			break;
		--event_count;
		rsp = dbc->rsp_q_base + head * sizeof(*rsp);
		req_id = le16_to_cpu(rsp->req_id);
		status = le16_to_cpu(rsp->status);
		if (status)
			pci_dbg(qdev->pdev, "req_id %d failed with status %d\n", req_id, status);
		spin_lock_irqsave(&dbc->xfer_lock, flags);
		/*
		 * A BO can receive multiple interrupts, since a BO can be
		 * divided into multiple slices and a buffer receives as many
		 * interrupts as slices. So until it receives interrupts for
		 * all the slices we cannot mark that buffer complete.
		 */
		list_for_each_entry_safe(bo, i, &dbc->xfer_list, xfer_list) {
			if (bo->req_id == req_id)
				bo->nr_slice_xfer_done++;
			else
				continue;

			if (bo->nr_slice_xfer_done < bo->nr_slice)
				break;

			/*
			 * At this point we have received all the interrupts for
			 * BO, which means BO execution is complete.
			 */
			dma_sync_sgtable_for_cpu(&qdev->pdev->dev, bo->sgt, bo->dir);
			bo->nr_slice_xfer_done = 0;
			bo->queued = false;
			list_del(&bo->xfer_list);
			bo->perf_stats.req_processed_ts = ktime_get_ns();
			complete_all(&bo->xfer_done);
			drm_gem_object_put(&bo->base);
			break;
		}
		spin_unlock_irqrestore(&dbc->xfer_lock, flags);
		head = (head + 1) % dbc->nelem;
	}

	/*
	 * Update the head pointer of response queue and let the device know
	 * that we have consumed elements from the queue.
	 */
	writel(head, dbc->dbc_base + RSPHP_OFF);

	/* elements might have been put in the queue while we were processing */
	goto read_fifo;

normal_out:
	if (likely(!datapath_polling))
		enable_irq(irq);
	else
		schedule_work(&dbc->poll_work);
	/* checking the fifo and enabling irqs is a race, missed event check */
	tail = readl(dbc->dbc_base + RSPTP_OFF);
	if (tail != U32_MAX && head != tail) {
		if (likely(!datapath_polling))
			disable_irq_nosync(irq);
		goto read_fifo;
	}
	srcu_read_unlock(&dbc->ch_lock, rcu_id);
	return IRQ_HANDLED;

error_out:
	srcu_read_unlock(&dbc->ch_lock, rcu_id);
	if (likely(!datapath_polling))
		enable_irq(irq);
	else
		schedule_work(&dbc->poll_work);

	return IRQ_HANDLED;
}

int qaic_wait_bo_ioctl(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	struct qaic_wait *args = data;
	int usr_rcu_id, qdev_rcu_id;
	struct dma_bridge_chan *dbc;
	struct drm_gem_object *obj;
	struct qaic_device *qdev;
	unsigned long timeout;
	struct qaic_user *usr;
	struct qaic_bo *bo;
	int rcu_id;
	int ret;

	if (args->pad != 0)
		return -EINVAL;

	usr = file_priv->driver_priv;
	usr_rcu_id = srcu_read_lock(&usr->qddev_lock);
	if (!usr->qddev) {
		ret = -ENODEV;
		goto unlock_usr_srcu;
	}

	qdev = usr->qddev->qdev;
	qdev_rcu_id = srcu_read_lock(&qdev->dev_lock);
	if (qdev->in_reset) {
		ret = -ENODEV;
		goto unlock_dev_srcu;
	}

	if (args->dbc_id >= qdev->num_dbc) {
		ret = -EINVAL;
		goto unlock_dev_srcu;
	}

	dbc = &qdev->dbc[args->dbc_id];

	rcu_id = srcu_read_lock(&dbc->ch_lock);
	if (dbc->usr != usr) {
		ret = -EPERM;
		goto unlock_ch_srcu;
	}

	obj = drm_gem_object_lookup(file_priv, args->handle);
	if (!obj) {
		ret = -ENOENT;
		goto unlock_ch_srcu;
	}

	bo = to_qaic_bo(obj);
	timeout = args->timeout ? args->timeout : wait_exec_default_timeout_ms;
	timeout = msecs_to_jiffies(timeout);
	ret = wait_for_completion_interruptible_timeout(&bo->xfer_done, timeout);
	if (!ret) {
		ret = -ETIMEDOUT;
		goto put_obj;
	}
	if (ret > 0)
		ret = 0;

	if (!dbc->usr)
		ret = -EPERM;

put_obj:
	drm_gem_object_put(obj);
unlock_ch_srcu:
	srcu_read_unlock(&dbc->ch_lock, rcu_id);
unlock_dev_srcu:
	srcu_read_unlock(&qdev->dev_lock, qdev_rcu_id);
unlock_usr_srcu:
	srcu_read_unlock(&usr->qddev_lock, usr_rcu_id);
	return ret;
}

int qaic_perf_stats_bo_ioctl(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	struct qaic_perf_stats_entry *ent = NULL;
	struct qaic_perf_stats *args = data;
	int usr_rcu_id, qdev_rcu_id;
	struct drm_gem_object *obj;
	struct qaic_device *qdev;
	struct qaic_user *usr;
	struct qaic_bo *bo;
	int ret, i;

	usr = file_priv->driver_priv;
	usr_rcu_id = srcu_read_lock(&usr->qddev_lock);
	if (!usr->qddev) {
		ret = -ENODEV;
		goto unlock_usr_srcu;
	}

	qdev = usr->qddev->qdev;
	qdev_rcu_id = srcu_read_lock(&qdev->dev_lock);
	if (qdev->in_reset) {
		ret = -ENODEV;
		goto unlock_dev_srcu;
	}

	if (args->hdr.dbc_id >= qdev->num_dbc) {
		ret = -EINVAL;
		goto unlock_dev_srcu;
	}

	ent = kcalloc(args->hdr.count, sizeof(*ent), GFP_KERNEL);
	if (!ent) {
		ret = -EINVAL;
		goto unlock_dev_srcu;
	}

	ret = copy_from_user(ent, u64_to_user_ptr(args->data), args->hdr.count * sizeof(*ent));
	if (ret) {
		ret = -EFAULT;
		goto free_ent;
	}

	for (i = 0; i < args->hdr.count; i++) {
		obj = drm_gem_object_lookup(file_priv, ent[i].handle);
		if (!obj) {
			ret = -ENOENT;
			goto free_ent;
		}
		bo = to_qaic_bo(obj);
		/*
		 * perf stats ioctl is called before wait ioctl is complete then
		 * the latency information is invalid.
		 */
		if (bo->perf_stats.req_processed_ts < bo->perf_stats.req_submit_ts) {
			ent[i].device_latency_us = 0;
		} else {
			ent[i].device_latency_us = div_u64((bo->perf_stats.req_processed_ts -
							    bo->perf_stats.req_submit_ts), 1000);
		}
		ent[i].submit_latency_us = div_u64((bo->perf_stats.req_submit_ts -
						    bo->perf_stats.req_received_ts), 1000);
		ent[i].queue_level_before = bo->perf_stats.queue_level_before;
		ent[i].num_queue_element = bo->total_slice_nents;
		drm_gem_object_put(obj);
	}

	if (copy_to_user(u64_to_user_ptr(args->data), ent, args->hdr.count * sizeof(*ent)))
		ret = -EFAULT;

free_ent:
	kfree(ent);
unlock_dev_srcu:
	srcu_read_unlock(&qdev->dev_lock, qdev_rcu_id);
unlock_usr_srcu:
	srcu_read_unlock(&usr->qddev_lock, usr_rcu_id);
	return ret;
}

static void detach_slice_bo(struct qaic_device *qdev, struct qaic_bo *bo)
{
	qaic_free_slices_bo(bo);
	qaic_unprepare_bo(qdev, bo);
	qaic_init_bo(bo, true);
	list_del(&bo->bo_list);
	drm_gem_object_put(&bo->base);
}

int qaic_detach_slice_bo_ioctl(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	struct qaic_detach_slice *args = data;
	int rcu_id, usr_rcu_id, qdev_rcu_id;
	struct dma_bridge_chan *dbc;
	struct drm_gem_object *obj;
	struct qaic_device *qdev;
	struct qaic_user *usr;
	unsigned long flags;
	struct qaic_bo *bo;
	int ret;

	if (args->pad != 0)
		return -EINVAL;

	usr = file_priv->driver_priv;
	usr_rcu_id = srcu_read_lock(&usr->qddev_lock);
	if (!usr->qddev) {
		ret = -ENODEV;
		goto unlock_usr_srcu;
	}

	qdev = usr->qddev->qdev;
	qdev_rcu_id = srcu_read_lock(&qdev->dev_lock);
	if (qdev->in_reset) {
		ret = -ENODEV;
		goto unlock_dev_srcu;
	}

	obj = drm_gem_object_lookup(file_priv, args->handle);
	if (!obj) {
		ret = -ENOENT;
		goto unlock_dev_srcu;
	}

	bo = to_qaic_bo(obj);
	ret = mutex_lock_interruptible(&bo->lock);
	if (ret)
		goto put_bo;

	if (!bo->sliced) {
		ret = -EINVAL;
		goto unlock_bo;
	}

	dbc = bo->dbc;
	rcu_id = srcu_read_lock(&dbc->ch_lock);
	if (dbc->usr != usr) {
		ret = -EINVAL;
		goto unlock_ch_srcu;
	}

	/* Check if BO is committed to H/W for DMA */
	spin_lock_irqsave(&dbc->xfer_lock, flags);
	if (bo->queued) {
		spin_unlock_irqrestore(&dbc->xfer_lock, flags);
		ret = -EBUSY;
		goto unlock_ch_srcu;
	}
	spin_unlock_irqrestore(&dbc->xfer_lock, flags);

	detach_slice_bo(qdev, bo);

unlock_ch_srcu:
	srcu_read_unlock(&dbc->ch_lock, rcu_id);
unlock_bo:
	mutex_unlock(&bo->lock);
put_bo:
	drm_gem_object_put(obj);
unlock_dev_srcu:
	srcu_read_unlock(&qdev->dev_lock, qdev_rcu_id);
unlock_usr_srcu:
	srcu_read_unlock(&usr->qddev_lock, usr_rcu_id);
	return ret;
}

static void empty_xfer_list(struct qaic_device *qdev, struct dma_bridge_chan *dbc)
{
	unsigned long flags;
	struct qaic_bo *bo;

	spin_lock_irqsave(&dbc->xfer_lock, flags);
	while (!list_empty(&dbc->xfer_list)) {
		bo = list_first_entry(&dbc->xfer_list, typeof(*bo), xfer_list);
		bo->queued = false;
		list_del(&bo->xfer_list);
		spin_unlock_irqrestore(&dbc->xfer_lock, flags);
		bo->nr_slice_xfer_done = 0;
		bo->req_id = 0;
		bo->perf_stats.req_received_ts = 0;
		bo->perf_stats.req_submit_ts = 0;
		bo->perf_stats.req_processed_ts = 0;
		bo->perf_stats.queue_level_before = 0;
		dma_sync_sgtable_for_cpu(&qdev->pdev->dev, bo->sgt, bo->dir);
		complete_all(&bo->xfer_done);
		drm_gem_object_put(&bo->base);
		spin_lock_irqsave(&dbc->xfer_lock, flags);
	}
	spin_unlock_irqrestore(&dbc->xfer_lock, flags);
}

int disable_dbc(struct qaic_device *qdev, u32 dbc_id, struct qaic_user *usr)
{
	if (!qdev->dbc[dbc_id].usr || qdev->dbc[dbc_id].usr->handle != usr->handle)
		return -EPERM;

	qdev->dbc[dbc_id].usr = NULL;
	synchronize_srcu(&qdev->dbc[dbc_id].ch_lock);
	return 0;
}

/**
 * enable_dbc - Enable the DBC. DBCs are disabled by removing the context of
 * user. Add user context back to DBC to enable it. This function trusts the
 * DBC ID passed and expects the DBC to be disabled.
 * @qdev: Qranium device handle
 * @dbc_id: ID of the DBC
 * @usr: User context
 */
void enable_dbc(struct qaic_device *qdev, u32 dbc_id, struct qaic_user *usr)
{
	qdev->dbc[dbc_id].usr = usr;
}

void wakeup_dbc(struct qaic_device *qdev, u32 dbc_id)
{
	struct dma_bridge_chan *dbc = &qdev->dbc[dbc_id];

	dbc->usr = NULL;
	empty_xfer_list(qdev, dbc);
	synchronize_srcu(&dbc->ch_lock);
	/*
	 * Threads holding channel lock, may add more elements in the xfer_list.
	 * Flush out these elements from xfer_list.
	 */
	empty_xfer_list(qdev, dbc);
}

void release_dbc(struct qaic_device *qdev, u32 dbc_id)
{
	struct qaic_bo *bo, *bo_temp;
	struct dma_bridge_chan *dbc;

	dbc = &qdev->dbc[dbc_id];
	if (!dbc->in_use)
		return;

	wakeup_dbc(qdev, dbc_id);

	dma_free_coherent(&qdev->pdev->dev, dbc->total_size, dbc->req_q_base, dbc->dma_addr);
	dbc->total_size = 0;
	dbc->req_q_base = NULL;
	dbc->dma_addr = 0;
	dbc->nelem = 0;
	dbc->usr = NULL;

	list_for_each_entry_safe(bo, bo_temp, &dbc->bo_lists, bo_list) {
		drm_gem_object_get(&bo->base);
		mutex_lock(&bo->lock);
		detach_slice_bo(qdev, bo);
		mutex_unlock(&bo->lock);
		drm_gem_object_put(&bo->base);
	}

	dbc->in_use = false;
	wake_up(&dbc->dbc_release);
}

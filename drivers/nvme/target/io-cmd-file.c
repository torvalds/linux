// SPDX-License-Identifier: GPL-2.0
/*
 * NVMe Over Fabrics Target File I/O commands implementation.
 * Copyright (c) 2017-2018 Western Digital Corporation or its
 * affiliates.
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/uio.h>
#include <linux/falloc.h>
#include <linux/file.h>
#include "nvmet.h"

#define NVMET_MAX_MPOOL_BVEC		16
#define NVMET_MIN_MPOOL_OBJ		16

void nvmet_file_ns_disable(struct nvmet_ns *ns)
{
	if (ns->file) {
		mempool_destroy(ns->bvec_pool);
		ns->bvec_pool = NULL;
		kmem_cache_destroy(ns->bvec_cache);
		ns->bvec_cache = NULL;
		fput(ns->file);
		ns->file = NULL;
	}
}

int nvmet_file_ns_enable(struct nvmet_ns *ns)
{
	int ret;
	struct kstat stat;

	ns->file = filp_open(ns->device_path,
			O_RDWR | O_LARGEFILE | O_DIRECT, 0);
	if (IS_ERR(ns->file)) {
		pr_err("failed to open file %s: (%ld)\n",
				ns->device_path, PTR_ERR(ns->file));
		return PTR_ERR(ns->file);
	}

	ret = vfs_getattr(&ns->file->f_path,
			&stat, STATX_SIZE, AT_STATX_FORCE_SYNC);
	if (ret)
		goto err;

	ns->size = stat.size;
	ns->blksize_shift = file_inode(ns->file)->i_blkbits;

	ns->bvec_cache = kmem_cache_create("nvmet-bvec",
			NVMET_MAX_MPOOL_BVEC * sizeof(struct bio_vec),
			0, SLAB_HWCACHE_ALIGN, NULL);
	if (!ns->bvec_cache) {
		ret = -ENOMEM;
		goto err;
	}

	ns->bvec_pool = mempool_create(NVMET_MIN_MPOOL_OBJ, mempool_alloc_slab,
			mempool_free_slab, ns->bvec_cache);

	if (!ns->bvec_pool) {
		ret = -ENOMEM;
		goto err;
	}

	return ret;
err:
	ns->size = 0;
	ns->blksize_shift = 0;
	nvmet_file_ns_disable(ns);
	return ret;
}

static void nvmet_file_init_bvec(struct bio_vec *bv, struct sg_page_iter *iter)
{
	bv->bv_page = sg_page_iter_page(iter);
	bv->bv_offset = iter->sg->offset;
	bv->bv_len = PAGE_SIZE - iter->sg->offset;
}

static ssize_t nvmet_file_submit_bvec(struct nvmet_req *req, loff_t pos,
		unsigned long nr_segs, size_t count)
{
	struct kiocb *iocb = &req->f.iocb;
	ssize_t (*call_iter)(struct kiocb *iocb, struct iov_iter *iter);
	struct iov_iter iter;
	int ki_flags = 0, rw;
	ssize_t ret;

	if (req->cmd->rw.opcode == nvme_cmd_write) {
		if (req->cmd->rw.control & cpu_to_le16(NVME_RW_FUA))
			ki_flags = IOCB_DSYNC;
		call_iter = req->ns->file->f_op->write_iter;
		rw = WRITE;
	} else {
		call_iter = req->ns->file->f_op->read_iter;
		rw = READ;
	}

	iov_iter_bvec(&iter, ITER_BVEC | rw, req->f.bvec, nr_segs, count);

	iocb->ki_pos = pos;
	iocb->ki_filp = req->ns->file;
	iocb->ki_flags = IOCB_DIRECT | ki_flags;

	ret = call_iter(iocb, &iter);

	if (ret != -EIOCBQUEUED && iocb->ki_complete)
		iocb->ki_complete(iocb, ret, 0);

	return ret;
}

static void nvmet_file_io_done(struct kiocb *iocb, long ret, long ret2)
{
	struct nvmet_req *req = container_of(iocb, struct nvmet_req, f.iocb);

	if (req->f.bvec != req->inline_bvec) {
		if (likely(req->f.mpool_alloc == false))
			kfree(req->f.bvec);
		else
			mempool_free(req->f.bvec, req->ns->bvec_pool);
	}

	nvmet_req_complete(req, ret != req->data_len ?
			NVME_SC_INTERNAL | NVME_SC_DNR : 0);
}

static void nvmet_file_execute_rw(struct nvmet_req *req)
{
	ssize_t nr_bvec = DIV_ROUND_UP(req->data_len, PAGE_SIZE);
	struct sg_page_iter sg_pg_iter;
	unsigned long bv_cnt = 0;
	bool is_sync = false;
	size_t len = 0, total_len = 0;
	ssize_t ret = 0;
	loff_t pos;

	if (!req->sg_cnt || !nr_bvec) {
		nvmet_req_complete(req, 0);
		return;
	}

	if (nr_bvec > NVMET_MAX_INLINE_BIOVEC)
		req->f.bvec = kmalloc_array(nr_bvec, sizeof(struct bio_vec),
				GFP_KERNEL);
	else
		req->f.bvec = req->inline_bvec;

	req->f.mpool_alloc = false;
	if (unlikely(!req->f.bvec)) {
		/* fallback under memory pressure */
		req->f.bvec = mempool_alloc(req->ns->bvec_pool, GFP_KERNEL);
		req->f.mpool_alloc = true;
		if (nr_bvec > NVMET_MAX_MPOOL_BVEC)
			is_sync = true;
	}

	pos = le64_to_cpu(req->cmd->rw.slba) << req->ns->blksize_shift;

	memset(&req->f.iocb, 0, sizeof(struct kiocb));
	for_each_sg_page(req->sg, &sg_pg_iter, req->sg_cnt, 0) {
		nvmet_file_init_bvec(&req->f.bvec[bv_cnt], &sg_pg_iter);
		len += req->f.bvec[bv_cnt].bv_len;
		total_len += req->f.bvec[bv_cnt].bv_len;
		bv_cnt++;

		WARN_ON_ONCE((nr_bvec - 1) < 0);

		if (unlikely(is_sync) &&
		    (nr_bvec - 1 == 0 || bv_cnt == NVMET_MAX_MPOOL_BVEC)) {
			ret = nvmet_file_submit_bvec(req, pos, bv_cnt, len);
			if (ret < 0)
				goto out;
			pos += len;
			bv_cnt = 0;
			len = 0;
		}
		nr_bvec--;
	}

	if (WARN_ON_ONCE(total_len != req->data_len))
		ret = -EIO;
out:
	if (unlikely(is_sync || ret)) {
		nvmet_file_io_done(&req->f.iocb, ret < 0 ? ret : total_len, 0);
		return;
	}
	req->f.iocb.ki_complete = nvmet_file_io_done;
	nvmet_file_submit_bvec(req, pos, bv_cnt, total_len);
}

static void nvmet_file_flush_work(struct work_struct *w)
{
	struct nvmet_req *req = container_of(w, struct nvmet_req, f.work);
	int ret;

	ret = vfs_fsync(req->ns->file, 1);

	nvmet_req_complete(req, ret < 0 ? NVME_SC_INTERNAL | NVME_SC_DNR : 0);
}

static void nvmet_file_execute_flush(struct nvmet_req *req)
{
	INIT_WORK(&req->f.work, nvmet_file_flush_work);
	schedule_work(&req->f.work);
}

static void nvmet_file_execute_discard(struct nvmet_req *req)
{
	int mode = FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE;
	struct nvme_dsm_range range;
	loff_t offset;
	loff_t len;
	int i, ret;

	for (i = 0; i <= le32_to_cpu(req->cmd->dsm.nr); i++) {
		if (nvmet_copy_from_sgl(req, i * sizeof(range), &range,
					sizeof(range)))
			break;
		offset = le64_to_cpu(range.slba) << req->ns->blksize_shift;
		len = le32_to_cpu(range.nlb) << req->ns->blksize_shift;
		ret = vfs_fallocate(req->ns->file, mode, offset, len);
		if (ret)
			break;
	}

	nvmet_req_complete(req, ret < 0 ? NVME_SC_INTERNAL | NVME_SC_DNR : 0);
}

static void nvmet_file_dsm_work(struct work_struct *w)
{
	struct nvmet_req *req = container_of(w, struct nvmet_req, f.work);

	switch (le32_to_cpu(req->cmd->dsm.attributes)) {
	case NVME_DSMGMT_AD:
		nvmet_file_execute_discard(req);
		return;
	case NVME_DSMGMT_IDR:
	case NVME_DSMGMT_IDW:
	default:
		/* Not supported yet */
		nvmet_req_complete(req, 0);
		return;
	}
}

static void nvmet_file_execute_dsm(struct nvmet_req *req)
{
	INIT_WORK(&req->f.work, nvmet_file_dsm_work);
	schedule_work(&req->f.work);
}

static void nvmet_file_write_zeroes_work(struct work_struct *w)
{
	struct nvmet_req *req = container_of(w, struct nvmet_req, f.work);
	struct nvme_write_zeroes_cmd *write_zeroes = &req->cmd->write_zeroes;
	int mode = FALLOC_FL_ZERO_RANGE | FALLOC_FL_KEEP_SIZE;
	loff_t offset;
	loff_t len;
	int ret;

	offset = le64_to_cpu(write_zeroes->slba) << req->ns->blksize_shift;
	len = (((sector_t)le16_to_cpu(write_zeroes->length) + 1) <<
			req->ns->blksize_shift);

	ret = vfs_fallocate(req->ns->file, mode, offset, len);
	nvmet_req_complete(req, ret < 0 ? NVME_SC_INTERNAL | NVME_SC_DNR : 0);
}

static void nvmet_file_execute_write_zeroes(struct nvmet_req *req)
{
	INIT_WORK(&req->f.work, nvmet_file_write_zeroes_work);
	schedule_work(&req->f.work);
}

u16 nvmet_file_parse_io_cmd(struct nvmet_req *req)
{
	struct nvme_command *cmd = req->cmd;

	switch (cmd->common.opcode) {
	case nvme_cmd_read:
	case nvme_cmd_write:
		req->execute = nvmet_file_execute_rw;
		req->data_len = nvmet_rw_len(req);
		return 0;
	case nvme_cmd_flush:
		req->execute = nvmet_file_execute_flush;
		req->data_len = 0;
		return 0;
	case nvme_cmd_dsm:
		req->execute = nvmet_file_execute_dsm;
		req->data_len = (le32_to_cpu(cmd->dsm.nr) + 1) *
			sizeof(struct nvme_dsm_range);
		return 0;
	case nvme_cmd_write_zeroes:
		req->execute = nvmet_file_execute_write_zeroes;
		req->data_len = 0;
		return 0;
	default:
		pr_err("unhandled cmd for file ns %d on qid %d\n",
				cmd->common.opcode, req->sq->qid);
		return NVME_SC_INVALID_OPCODE | NVME_SC_DNR;
	}
}

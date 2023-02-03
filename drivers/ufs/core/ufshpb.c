// SPDX-License-Identifier: GPL-2.0
/*
 * Universal Flash Storage Host Performance Booster
 *
 * Copyright (C) 2017-2021 Samsung Electronics Co., Ltd.
 *
 * Authors:
 *	Yongmyung Lee <ymhungry.lee@samsung.com>
 *	Jinyoung Choi <j-young.choi@samsung.com>
 */

#include <asm/unaligned.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/module.h>
#include <scsi/scsi_cmnd.h>

#include "ufshcd-priv.h"
#include "ufshpb.h"
#include "../../scsi/sd.h"

#define ACTIVATION_THRESHOLD 8 /* 8 IOs */
#define READ_TO_MS 1000
#define READ_TO_EXPIRIES 100
#define POLLING_INTERVAL_MS 200
#define THROTTLE_MAP_REQ_DEFAULT 1

/* memory management */
static struct kmem_cache *ufshpb_mctx_cache;
static mempool_t *ufshpb_mctx_pool;
static mempool_t *ufshpb_page_pool;
/* A cache size of 2MB can cache ppn in the 1GB range. */
static unsigned int ufshpb_host_map_kbytes = 2048;
static int tot_active_srgn_pages;

static struct workqueue_struct *ufshpb_wq;

static void ufshpb_update_active_info(struct ufshpb_lu *hpb, int rgn_idx,
				      int srgn_idx);

bool ufshpb_is_allowed(struct ufs_hba *hba)
{
	return !(hba->ufshpb_dev.hpb_disabled);
}

/* HPB version 1.0 is called as legacy version. */
bool ufshpb_is_legacy(struct ufs_hba *hba)
{
	return hba->ufshpb_dev.is_legacy;
}

static struct ufshpb_lu *ufshpb_get_hpb_data(struct scsi_device *sdev)
{
	return sdev->hostdata;
}

static int ufshpb_get_state(struct ufshpb_lu *hpb)
{
	return atomic_read(&hpb->hpb_state);
}

static void ufshpb_set_state(struct ufshpb_lu *hpb, int state)
{
	atomic_set(&hpb->hpb_state, state);
}

static int ufshpb_is_valid_srgn(struct ufshpb_region *rgn,
				struct ufshpb_subregion *srgn)
{
	return rgn->rgn_state != HPB_RGN_INACTIVE &&
		srgn->srgn_state == HPB_SRGN_VALID;
}

static bool ufshpb_is_read_cmd(struct scsi_cmnd *cmd)
{
	return req_op(scsi_cmd_to_rq(cmd)) == REQ_OP_READ;
}

static bool ufshpb_is_write_or_discard(struct scsi_cmnd *cmd)
{
	return op_is_write(req_op(scsi_cmd_to_rq(cmd))) ||
	       op_is_discard(req_op(scsi_cmd_to_rq(cmd)));
}

static bool ufshpb_is_supported_chunk(struct ufshpb_lu *hpb, int transfer_len)
{
	return transfer_len <= hpb->pre_req_max_tr_len;
}

static bool ufshpb_is_general_lun(int lun)
{
	return lun < UFS_UPIU_MAX_UNIT_NUM_ID;
}

static bool ufshpb_is_pinned_region(struct ufshpb_lu *hpb, int rgn_idx)
{
	return hpb->lu_pinned_end != PINNED_NOT_SET &&
	       rgn_idx >= hpb->lu_pinned_start && rgn_idx <= hpb->lu_pinned_end;
}

static void ufshpb_kick_map_work(struct ufshpb_lu *hpb)
{
	bool ret = false;
	unsigned long flags;

	if (ufshpb_get_state(hpb) != HPB_PRESENT)
		return;

	spin_lock_irqsave(&hpb->rsp_list_lock, flags);
	if (!list_empty(&hpb->lh_inact_rgn) || !list_empty(&hpb->lh_act_srgn))
		ret = true;
	spin_unlock_irqrestore(&hpb->rsp_list_lock, flags);

	if (ret)
		queue_work(ufshpb_wq, &hpb->map_work);
}

static bool ufshpb_is_hpb_rsp_valid(struct ufs_hba *hba,
				    struct ufshcd_lrb *lrbp,
				    struct utp_hpb_rsp *rsp_field)
{
	/* Check HPB_UPDATE_ALERT */
	if (!(lrbp->ucd_rsp_ptr->header.dword_2 &
	      UPIU_HEADER_DWORD(0, 2, 0, 0)))
		return false;

	if (be16_to_cpu(rsp_field->sense_data_len) != DEV_SENSE_SEG_LEN ||
	    rsp_field->desc_type != DEV_DES_TYPE ||
	    rsp_field->additional_len != DEV_ADDITIONAL_LEN ||
	    rsp_field->active_rgn_cnt > MAX_ACTIVE_NUM ||
	    rsp_field->inactive_rgn_cnt > MAX_INACTIVE_NUM ||
	    rsp_field->hpb_op == HPB_RSP_NONE ||
	    (rsp_field->hpb_op == HPB_RSP_REQ_REGION_UPDATE &&
	     !rsp_field->active_rgn_cnt && !rsp_field->inactive_rgn_cnt))
		return false;

	if (!ufshpb_is_general_lun(rsp_field->lun)) {
		dev_warn(hba->dev, "ufshpb: lun(%d) not supported\n",
			 lrbp->lun);
		return false;
	}

	return true;
}

static void ufshpb_iterate_rgn(struct ufshpb_lu *hpb, int rgn_idx, int srgn_idx,
			       int srgn_offset, int cnt, bool set_dirty)
{
	struct ufshpb_region *rgn;
	struct ufshpb_subregion *srgn, *prev_srgn = NULL;
	int set_bit_len;
	int bitmap_len;
	unsigned long flags;

next_srgn:
	rgn = hpb->rgn_tbl + rgn_idx;
	srgn = rgn->srgn_tbl + srgn_idx;

	if (likely(!srgn->is_last))
		bitmap_len = hpb->entries_per_srgn;
	else
		bitmap_len = hpb->last_srgn_entries;

	if ((srgn_offset + cnt) > bitmap_len)
		set_bit_len = bitmap_len - srgn_offset;
	else
		set_bit_len = cnt;

	spin_lock_irqsave(&hpb->rgn_state_lock, flags);
	if (rgn->rgn_state != HPB_RGN_INACTIVE) {
		if (set_dirty) {
			if (srgn->srgn_state == HPB_SRGN_VALID)
				bitmap_set(srgn->mctx->ppn_dirty, srgn_offset,
					   set_bit_len);
		} else if (hpb->is_hcm) {
			 /* rewind the read timer for lru regions */
			rgn->read_timeout = ktime_add_ms(ktime_get(),
					rgn->hpb->params.read_timeout_ms);
			rgn->read_timeout_expiries =
				rgn->hpb->params.read_timeout_expiries;
		}
	}
	spin_unlock_irqrestore(&hpb->rgn_state_lock, flags);

	if (hpb->is_hcm && prev_srgn != srgn) {
		bool activate = false;

		spin_lock(&rgn->rgn_lock);
		if (set_dirty) {
			rgn->reads -= srgn->reads;
			srgn->reads = 0;
			set_bit(RGN_FLAG_DIRTY, &rgn->rgn_flags);
		} else {
			srgn->reads++;
			rgn->reads++;
			if (srgn->reads == hpb->params.activation_thld)
				activate = true;
		}
		spin_unlock(&rgn->rgn_lock);

		if (activate ||
		    test_and_clear_bit(RGN_FLAG_UPDATE, &rgn->rgn_flags)) {
			spin_lock_irqsave(&hpb->rsp_list_lock, flags);
			ufshpb_update_active_info(hpb, rgn_idx, srgn_idx);
			spin_unlock_irqrestore(&hpb->rsp_list_lock, flags);
			dev_dbg(&hpb->sdev_ufs_lu->sdev_dev,
				"activate region %d-%d\n", rgn_idx, srgn_idx);
		}

		prev_srgn = srgn;
	}

	srgn_offset = 0;
	if (++srgn_idx == hpb->srgns_per_rgn) {
		srgn_idx = 0;
		rgn_idx++;
	}

	cnt -= set_bit_len;
	if (cnt > 0)
		goto next_srgn;
}

static bool ufshpb_test_ppn_dirty(struct ufshpb_lu *hpb, int rgn_idx,
				  int srgn_idx, int srgn_offset, int cnt)
{
	struct ufshpb_region *rgn;
	struct ufshpb_subregion *srgn;
	int bitmap_len;
	int bit_len;

next_srgn:
	rgn = hpb->rgn_tbl + rgn_idx;
	srgn = rgn->srgn_tbl + srgn_idx;

	if (likely(!srgn->is_last))
		bitmap_len = hpb->entries_per_srgn;
	else
		bitmap_len = hpb->last_srgn_entries;

	if (!ufshpb_is_valid_srgn(rgn, srgn))
		return true;

	/*
	 * If the region state is active, mctx must be allocated.
	 * In this case, check whether the region is evicted or
	 * mctx allocation fail.
	 */
	if (unlikely(!srgn->mctx)) {
		dev_err(&hpb->sdev_ufs_lu->sdev_dev,
			"no mctx in region %d subregion %d.\n",
			srgn->rgn_idx, srgn->srgn_idx);
		return true;
	}

	if ((srgn_offset + cnt) > bitmap_len)
		bit_len = bitmap_len - srgn_offset;
	else
		bit_len = cnt;

	if (find_next_bit(srgn->mctx->ppn_dirty, bit_len + srgn_offset,
			  srgn_offset) < bit_len + srgn_offset)
		return true;

	srgn_offset = 0;
	if (++srgn_idx == hpb->srgns_per_rgn) {
		srgn_idx = 0;
		rgn_idx++;
	}

	cnt -= bit_len;
	if (cnt > 0)
		goto next_srgn;

	return false;
}

static inline bool is_rgn_dirty(struct ufshpb_region *rgn)
{
	return test_bit(RGN_FLAG_DIRTY, &rgn->rgn_flags);
}

static int ufshpb_fill_ppn_from_page(struct ufshpb_lu *hpb,
				     struct ufshpb_map_ctx *mctx, int pos,
				     int len, __be64 *ppn_buf)
{
	struct page *page;
	int index, offset;
	int copied;

	index = pos / (PAGE_SIZE / HPB_ENTRY_SIZE);
	offset = pos % (PAGE_SIZE / HPB_ENTRY_SIZE);

	if ((offset + len) <= (PAGE_SIZE / HPB_ENTRY_SIZE))
		copied = len;
	else
		copied = (PAGE_SIZE / HPB_ENTRY_SIZE) - offset;

	page = mctx->m_page[index];
	if (unlikely(!page)) {
		dev_err(&hpb->sdev_ufs_lu->sdev_dev,
			"error. cannot find page in mctx\n");
		return -ENOMEM;
	}

	memcpy(ppn_buf, page_address(page) + (offset * HPB_ENTRY_SIZE),
	       copied * HPB_ENTRY_SIZE);

	return copied;
}

static void
ufshpb_get_pos_from_lpn(struct ufshpb_lu *hpb, unsigned long lpn, int *rgn_idx,
			int *srgn_idx, int *offset)
{
	int rgn_offset;

	*rgn_idx = lpn >> hpb->entries_per_rgn_shift;
	rgn_offset = lpn & hpb->entries_per_rgn_mask;
	*srgn_idx = rgn_offset >> hpb->entries_per_srgn_shift;
	*offset = rgn_offset & hpb->entries_per_srgn_mask;
}

static void
ufshpb_set_hpb_read_to_upiu(struct ufs_hba *hba, struct ufshcd_lrb *lrbp,
			    __be64 ppn, u8 transfer_len)
{
	unsigned char *cdb = lrbp->cmd->cmnd;
	__be64 ppn_tmp = ppn;
	cdb[0] = UFSHPB_READ;

	if (hba->dev_quirks & UFS_DEVICE_QUIRK_SWAP_L2P_ENTRY_FOR_HPB_READ)
		ppn_tmp = (__force __be64)swab64((__force u64)ppn);

	/* ppn value is stored as big-endian in the host memory */
	memcpy(&cdb[6], &ppn_tmp, sizeof(__be64));
	cdb[14] = transfer_len;
	cdb[15] = 0;

	lrbp->cmd->cmd_len = UFS_CDB_SIZE;
}

/*
 * This function will set up HPB read command using host-side L2P map data.
 */
int ufshpb_prep(struct ufs_hba *hba, struct ufshcd_lrb *lrbp)
{
	struct ufshpb_lu *hpb;
	struct ufshpb_region *rgn;
	struct ufshpb_subregion *srgn;
	struct scsi_cmnd *cmd = lrbp->cmd;
	u32 lpn;
	__be64 ppn;
	unsigned long flags;
	int transfer_len, rgn_idx, srgn_idx, srgn_offset;
	int err = 0;

	hpb = ufshpb_get_hpb_data(cmd->device);
	if (!hpb)
		return -ENODEV;

	if (ufshpb_get_state(hpb) == HPB_INIT)
		return -ENODEV;

	if (ufshpb_get_state(hpb) != HPB_PRESENT) {
		dev_notice(&hpb->sdev_ufs_lu->sdev_dev,
			   "%s: ufshpb state is not PRESENT", __func__);
		return -ENODEV;
	}

	if (blk_rq_is_passthrough(scsi_cmd_to_rq(cmd)) ||
	    (!ufshpb_is_write_or_discard(cmd) &&
	     !ufshpb_is_read_cmd(cmd)))
		return 0;

	transfer_len = sectors_to_logical(cmd->device,
					  blk_rq_sectors(scsi_cmd_to_rq(cmd)));
	if (unlikely(!transfer_len))
		return 0;

	lpn = sectors_to_logical(cmd->device, blk_rq_pos(scsi_cmd_to_rq(cmd)));
	ufshpb_get_pos_from_lpn(hpb, lpn, &rgn_idx, &srgn_idx, &srgn_offset);
	rgn = hpb->rgn_tbl + rgn_idx;
	srgn = rgn->srgn_tbl + srgn_idx;

	/* If command type is WRITE or DISCARD, set bitmap as dirty */
	if (ufshpb_is_write_or_discard(cmd)) {
		ufshpb_iterate_rgn(hpb, rgn_idx, srgn_idx, srgn_offset,
				   transfer_len, true);
		return 0;
	}

	if (!ufshpb_is_supported_chunk(hpb, transfer_len))
		return 0;

	if (hpb->is_hcm) {
		/*
		 * in host control mode, reads are the main source for
		 * activation trials.
		 */
		ufshpb_iterate_rgn(hpb, rgn_idx, srgn_idx, srgn_offset,
				   transfer_len, false);

		/* keep those counters normalized */
		if (rgn->reads > hpb->entries_per_srgn)
			schedule_work(&hpb->ufshpb_normalization_work);
	}

	spin_lock_irqsave(&hpb->rgn_state_lock, flags);
	if (ufshpb_test_ppn_dirty(hpb, rgn_idx, srgn_idx, srgn_offset,
				   transfer_len)) {
		hpb->stats.miss_cnt++;
		spin_unlock_irqrestore(&hpb->rgn_state_lock, flags);
		return 0;
	}

	err = ufshpb_fill_ppn_from_page(hpb, srgn->mctx, srgn_offset, 1, &ppn);
	spin_unlock_irqrestore(&hpb->rgn_state_lock, flags);
	if (unlikely(err < 0)) {
		/*
		 * In this case, the region state is active,
		 * but the ppn table is not allocated.
		 * Make sure that ppn table must be allocated on
		 * active state.
		 */
		dev_err(hba->dev, "get ppn failed. err %d\n", err);
		return err;
	}

	ufshpb_set_hpb_read_to_upiu(hba, lrbp, ppn, transfer_len);

	hpb->stats.hit_cnt++;
	return 0;
}

static struct ufshpb_req *ufshpb_get_req(struct ufshpb_lu *hpb, int rgn_idx,
					 enum req_op op, bool atomic)
{
	struct ufshpb_req *rq;
	struct request *req;
	int retries = HPB_MAP_REQ_RETRIES;

	rq = kmem_cache_alloc(hpb->map_req_cache, GFP_KERNEL);
	if (!rq)
		return NULL;

retry:
	req = blk_mq_alloc_request(hpb->sdev_ufs_lu->request_queue, op,
			      BLK_MQ_REQ_NOWAIT);

	if (!atomic && (PTR_ERR(req) == -EWOULDBLOCK) && (--retries > 0)) {
		usleep_range(3000, 3100);
		goto retry;
	}

	if (IS_ERR(req))
		goto free_rq;

	rq->hpb = hpb;
	rq->req = req;
	rq->rb.rgn_idx = rgn_idx;

	return rq;

free_rq:
	kmem_cache_free(hpb->map_req_cache, rq);
	return NULL;
}

static void ufshpb_put_req(struct ufshpb_lu *hpb, struct ufshpb_req *rq)
{
	blk_mq_free_request(rq->req);
	kmem_cache_free(hpb->map_req_cache, rq);
}

static struct ufshpb_req *ufshpb_get_map_req(struct ufshpb_lu *hpb,
					     struct ufshpb_subregion *srgn)
{
	struct ufshpb_req *map_req;
	struct bio *bio;
	unsigned long flags;

	if (hpb->is_hcm &&
	    hpb->num_inflight_map_req >= hpb->params.inflight_map_req) {
		dev_info(&hpb->sdev_ufs_lu->sdev_dev,
			 "map_req throttle. inflight %d throttle %d",
			 hpb->num_inflight_map_req,
			 hpb->params.inflight_map_req);
		return NULL;
	}

	map_req = ufshpb_get_req(hpb, srgn->rgn_idx, REQ_OP_DRV_IN, false);
	if (!map_req)
		return NULL;

	bio = bio_alloc(NULL, hpb->pages_per_srgn, 0, GFP_KERNEL);
	if (!bio) {
		ufshpb_put_req(hpb, map_req);
		return NULL;
	}

	map_req->bio = bio;

	map_req->rb.srgn_idx = srgn->srgn_idx;
	map_req->rb.mctx = srgn->mctx;

	spin_lock_irqsave(&hpb->param_lock, flags);
	hpb->num_inflight_map_req++;
	spin_unlock_irqrestore(&hpb->param_lock, flags);

	return map_req;
}

static void ufshpb_put_map_req(struct ufshpb_lu *hpb,
			       struct ufshpb_req *map_req)
{
	unsigned long flags;

	bio_put(map_req->bio);
	ufshpb_put_req(hpb, map_req);

	spin_lock_irqsave(&hpb->param_lock, flags);
	hpb->num_inflight_map_req--;
	spin_unlock_irqrestore(&hpb->param_lock, flags);
}

static int ufshpb_clear_dirty_bitmap(struct ufshpb_lu *hpb,
				     struct ufshpb_subregion *srgn)
{
	struct ufshpb_region *rgn;
	u32 num_entries = hpb->entries_per_srgn;

	if (!srgn->mctx) {
		dev_err(&hpb->sdev_ufs_lu->sdev_dev,
			"no mctx in region %d subregion %d.\n",
			srgn->rgn_idx, srgn->srgn_idx);
		return -1;
	}

	if (unlikely(srgn->is_last))
		num_entries = hpb->last_srgn_entries;

	bitmap_zero(srgn->mctx->ppn_dirty, num_entries);

	rgn = hpb->rgn_tbl + srgn->rgn_idx;
	clear_bit(RGN_FLAG_DIRTY, &rgn->rgn_flags);

	return 0;
}

static void ufshpb_update_active_info(struct ufshpb_lu *hpb, int rgn_idx,
				      int srgn_idx)
{
	struct ufshpb_region *rgn;
	struct ufshpb_subregion *srgn;

	rgn = hpb->rgn_tbl + rgn_idx;
	srgn = rgn->srgn_tbl + srgn_idx;

	list_del_init(&rgn->list_inact_rgn);

	if (list_empty(&srgn->list_act_srgn))
		list_add_tail(&srgn->list_act_srgn, &hpb->lh_act_srgn);

	hpb->stats.rcmd_active_cnt++;
}

static void ufshpb_update_inactive_info(struct ufshpb_lu *hpb, int rgn_idx)
{
	struct ufshpb_region *rgn;
	struct ufshpb_subregion *srgn;
	int srgn_idx;

	rgn = hpb->rgn_tbl + rgn_idx;

	for_each_sub_region(rgn, srgn_idx, srgn)
		list_del_init(&srgn->list_act_srgn);

	if (list_empty(&rgn->list_inact_rgn))
		list_add_tail(&rgn->list_inact_rgn, &hpb->lh_inact_rgn);

	hpb->stats.rcmd_inactive_cnt++;
}

static void ufshpb_activate_subregion(struct ufshpb_lu *hpb,
				      struct ufshpb_subregion *srgn)
{
	struct ufshpb_region *rgn;

	/*
	 * If there is no mctx in subregion
	 * after I/O progress for HPB_READ_BUFFER, the region to which the
	 * subregion belongs was evicted.
	 * Make sure the region must not evict in I/O progress
	 */
	if (!srgn->mctx) {
		dev_err(&hpb->sdev_ufs_lu->sdev_dev,
			"no mctx in region %d subregion %d.\n",
			srgn->rgn_idx, srgn->srgn_idx);
		srgn->srgn_state = HPB_SRGN_INVALID;
		return;
	}

	rgn = hpb->rgn_tbl + srgn->rgn_idx;

	if (unlikely(rgn->rgn_state == HPB_RGN_INACTIVE)) {
		dev_err(&hpb->sdev_ufs_lu->sdev_dev,
			"region %d subregion %d evicted\n",
			srgn->rgn_idx, srgn->srgn_idx);
		srgn->srgn_state = HPB_SRGN_INVALID;
		return;
	}
	srgn->srgn_state = HPB_SRGN_VALID;
}

static enum rq_end_io_ret ufshpb_umap_req_compl_fn(struct request *req,
						   blk_status_t error)
{
	struct ufshpb_req *umap_req = req->end_io_data;

	ufshpb_put_req(umap_req->hpb, umap_req);
	return RQ_END_IO_NONE;
}

static enum rq_end_io_ret ufshpb_map_req_compl_fn(struct request *req,
						  blk_status_t error)
{
	struct ufshpb_req *map_req = req->end_io_data;
	struct ufshpb_lu *hpb = map_req->hpb;
	struct ufshpb_subregion *srgn;
	unsigned long flags;

	srgn = hpb->rgn_tbl[map_req->rb.rgn_idx].srgn_tbl +
		map_req->rb.srgn_idx;

	ufshpb_clear_dirty_bitmap(hpb, srgn);
	spin_lock_irqsave(&hpb->rgn_state_lock, flags);
	ufshpb_activate_subregion(hpb, srgn);
	spin_unlock_irqrestore(&hpb->rgn_state_lock, flags);

	ufshpb_put_map_req(map_req->hpb, map_req);
	return RQ_END_IO_NONE;
}

static void ufshpb_set_unmap_cmd(unsigned char *cdb, struct ufshpb_region *rgn)
{
	cdb[0] = UFSHPB_WRITE_BUFFER;
	cdb[1] = rgn ? UFSHPB_WRITE_BUFFER_INACT_SINGLE_ID :
			  UFSHPB_WRITE_BUFFER_INACT_ALL_ID;
	if (rgn)
		put_unaligned_be16(rgn->rgn_idx, &cdb[2]);
	cdb[9] = 0x00;
}

static void ufshpb_set_read_buf_cmd(unsigned char *cdb, int rgn_idx,
				    int srgn_idx, int srgn_mem_size)
{
	cdb[0] = UFSHPB_READ_BUFFER;
	cdb[1] = UFSHPB_READ_BUFFER_ID;

	put_unaligned_be16(rgn_idx, &cdb[2]);
	put_unaligned_be16(srgn_idx, &cdb[4]);
	put_unaligned_be24(srgn_mem_size, &cdb[6]);

	cdb[9] = 0x00;
}

static void ufshpb_execute_umap_req(struct ufshpb_lu *hpb,
				   struct ufshpb_req *umap_req,
				   struct ufshpb_region *rgn)
{
	struct request *req = umap_req->req;
	struct scsi_cmnd *scmd = blk_mq_rq_to_pdu(req);

	req->timeout = 0;
	req->end_io_data = umap_req;
	req->end_io = ufshpb_umap_req_compl_fn;

	ufshpb_set_unmap_cmd(scmd->cmnd, rgn);
	scmd->cmd_len = HPB_WRITE_BUFFER_CMD_LENGTH;

	blk_execute_rq_nowait(req, true);

	hpb->stats.umap_req_cnt++;
}

static int ufshpb_execute_map_req(struct ufshpb_lu *hpb,
				  struct ufshpb_req *map_req, bool last)
{
	struct request_queue *q;
	struct request *req;
	struct scsi_cmnd *scmd;
	int mem_size = hpb->srgn_mem_size;
	int ret = 0;
	int i;

	q = hpb->sdev_ufs_lu->request_queue;
	for (i = 0; i < hpb->pages_per_srgn; i++) {
		ret = bio_add_pc_page(q, map_req->bio, map_req->rb.mctx->m_page[i],
				      PAGE_SIZE, 0);
		if (ret != PAGE_SIZE) {
			dev_err(&hpb->sdev_ufs_lu->sdev_dev,
				   "bio_add_pc_page fail %d - %d\n",
				   map_req->rb.rgn_idx, map_req->rb.srgn_idx);
			return ret;
		}
	}

	req = map_req->req;

	blk_rq_append_bio(req, map_req->bio);

	req->end_io_data = map_req;
	req->end_io = ufshpb_map_req_compl_fn;

	if (unlikely(last))
		mem_size = hpb->last_srgn_entries * HPB_ENTRY_SIZE;

	scmd = blk_mq_rq_to_pdu(req);
	ufshpb_set_read_buf_cmd(scmd->cmnd, map_req->rb.rgn_idx,
				map_req->rb.srgn_idx, mem_size);
	scmd->cmd_len = HPB_READ_BUFFER_CMD_LENGTH;

	blk_execute_rq_nowait(req, true);

	hpb->stats.map_req_cnt++;
	return 0;
}

static struct ufshpb_map_ctx *ufshpb_get_map_ctx(struct ufshpb_lu *hpb,
						 bool last)
{
	struct ufshpb_map_ctx *mctx;
	u32 num_entries = hpb->entries_per_srgn;
	int i, j;

	mctx = mempool_alloc(ufshpb_mctx_pool, GFP_KERNEL);
	if (!mctx)
		return NULL;

	mctx->m_page = kmem_cache_alloc(hpb->m_page_cache, GFP_KERNEL);
	if (!mctx->m_page)
		goto release_mctx;

	if (unlikely(last))
		num_entries = hpb->last_srgn_entries;

	mctx->ppn_dirty = bitmap_zalloc(num_entries, GFP_KERNEL);
	if (!mctx->ppn_dirty)
		goto release_m_page;

	for (i = 0; i < hpb->pages_per_srgn; i++) {
		mctx->m_page[i] = mempool_alloc(ufshpb_page_pool, GFP_KERNEL);
		if (!mctx->m_page[i]) {
			for (j = 0; j < i; j++)
				mempool_free(mctx->m_page[j], ufshpb_page_pool);
			goto release_ppn_dirty;
		}
		clear_page(page_address(mctx->m_page[i]));
	}

	return mctx;

release_ppn_dirty:
	bitmap_free(mctx->ppn_dirty);
release_m_page:
	kmem_cache_free(hpb->m_page_cache, mctx->m_page);
release_mctx:
	mempool_free(mctx, ufshpb_mctx_pool);
	return NULL;
}

static void ufshpb_put_map_ctx(struct ufshpb_lu *hpb,
			       struct ufshpb_map_ctx *mctx)
{
	int i;

	for (i = 0; i < hpb->pages_per_srgn; i++)
		mempool_free(mctx->m_page[i], ufshpb_page_pool);

	bitmap_free(mctx->ppn_dirty);
	kmem_cache_free(hpb->m_page_cache, mctx->m_page);
	mempool_free(mctx, ufshpb_mctx_pool);
}

static int ufshpb_check_srgns_issue_state(struct ufshpb_lu *hpb,
					  struct ufshpb_region *rgn)
{
	struct ufshpb_subregion *srgn;
	int srgn_idx;

	for_each_sub_region(rgn, srgn_idx, srgn)
		if (srgn->srgn_state == HPB_SRGN_ISSUED)
			return -EPERM;

	return 0;
}

static void ufshpb_read_to_handler(struct work_struct *work)
{
	struct ufshpb_lu *hpb = container_of(work, struct ufshpb_lu,
					     ufshpb_read_to_work.work);
	struct victim_select_info *lru_info = &hpb->lru_info;
	struct ufshpb_region *rgn, *next_rgn;
	unsigned long flags;
	unsigned int poll;
	LIST_HEAD(expired_list);

	if (test_and_set_bit(TIMEOUT_WORK_RUNNING, &hpb->work_data_bits))
		return;

	spin_lock_irqsave(&hpb->rgn_state_lock, flags);

	list_for_each_entry_safe(rgn, next_rgn, &lru_info->lh_lru_rgn,
				 list_lru_rgn) {
		bool timedout = ktime_after(ktime_get(), rgn->read_timeout);

		if (timedout) {
			rgn->read_timeout_expiries--;
			if (is_rgn_dirty(rgn) ||
			    rgn->read_timeout_expiries == 0)
				list_add(&rgn->list_expired_rgn, &expired_list);
			else
				rgn->read_timeout = ktime_add_ms(ktime_get(),
						hpb->params.read_timeout_ms);
		}
	}

	spin_unlock_irqrestore(&hpb->rgn_state_lock, flags);

	list_for_each_entry_safe(rgn, next_rgn, &expired_list,
				 list_expired_rgn) {
		list_del_init(&rgn->list_expired_rgn);
		spin_lock_irqsave(&hpb->rsp_list_lock, flags);
		ufshpb_update_inactive_info(hpb, rgn->rgn_idx);
		spin_unlock_irqrestore(&hpb->rsp_list_lock, flags);
	}

	ufshpb_kick_map_work(hpb);

	clear_bit(TIMEOUT_WORK_RUNNING, &hpb->work_data_bits);

	poll = hpb->params.timeout_polling_interval_ms;
	schedule_delayed_work(&hpb->ufshpb_read_to_work,
			      msecs_to_jiffies(poll));
}

static void ufshpb_add_lru_info(struct victim_select_info *lru_info,
				struct ufshpb_region *rgn)
{
	rgn->rgn_state = HPB_RGN_ACTIVE;
	list_add_tail(&rgn->list_lru_rgn, &lru_info->lh_lru_rgn);
	atomic_inc(&lru_info->active_cnt);
	if (rgn->hpb->is_hcm) {
		rgn->read_timeout =
			ktime_add_ms(ktime_get(),
				     rgn->hpb->params.read_timeout_ms);
		rgn->read_timeout_expiries =
			rgn->hpb->params.read_timeout_expiries;
	}
}

static void ufshpb_hit_lru_info(struct victim_select_info *lru_info,
				struct ufshpb_region *rgn)
{
	list_move_tail(&rgn->list_lru_rgn, &lru_info->lh_lru_rgn);
}

static struct ufshpb_region *ufshpb_victim_lru_info(struct ufshpb_lu *hpb)
{
	struct victim_select_info *lru_info = &hpb->lru_info;
	struct ufshpb_region *rgn, *victim_rgn = NULL;

	list_for_each_entry(rgn, &lru_info->lh_lru_rgn, list_lru_rgn) {
		if (ufshpb_check_srgns_issue_state(hpb, rgn))
			continue;

		/*
		 * in host control mode, verify that the exiting region
		 * has fewer reads
		 */
		if (hpb->is_hcm &&
		    rgn->reads > hpb->params.eviction_thld_exit)
			continue;

		victim_rgn = rgn;
		break;
	}

	if (!victim_rgn)
		dev_err(&hpb->sdev_ufs_lu->sdev_dev,
			"%s: no region allocated\n",
			__func__);

	return victim_rgn;
}

static void ufshpb_cleanup_lru_info(struct victim_select_info *lru_info,
				    struct ufshpb_region *rgn)
{
	list_del_init(&rgn->list_lru_rgn);
	rgn->rgn_state = HPB_RGN_INACTIVE;
	atomic_dec(&lru_info->active_cnt);
}

static void ufshpb_purge_active_subregion(struct ufshpb_lu *hpb,
					  struct ufshpb_subregion *srgn)
{
	if (srgn->srgn_state != HPB_SRGN_UNUSED) {
		ufshpb_put_map_ctx(hpb, srgn->mctx);
		srgn->srgn_state = HPB_SRGN_UNUSED;
		srgn->mctx = NULL;
	}
}

static int ufshpb_issue_umap_req(struct ufshpb_lu *hpb,
				 struct ufshpb_region *rgn,
				 bool atomic)
{
	struct ufshpb_req *umap_req;
	int rgn_idx = rgn ? rgn->rgn_idx : 0;

	umap_req = ufshpb_get_req(hpb, rgn_idx, REQ_OP_DRV_OUT, atomic);
	if (!umap_req)
		return -ENOMEM;

	ufshpb_execute_umap_req(hpb, umap_req, rgn);

	return 0;
}

static int ufshpb_issue_umap_single_req(struct ufshpb_lu *hpb,
					struct ufshpb_region *rgn)
{
	return ufshpb_issue_umap_req(hpb, rgn, true);
}

static void __ufshpb_evict_region(struct ufshpb_lu *hpb,
				 struct ufshpb_region *rgn)
{
	struct victim_select_info *lru_info;
	struct ufshpb_subregion *srgn;
	int srgn_idx;

	lru_info = &hpb->lru_info;

	dev_dbg(&hpb->sdev_ufs_lu->sdev_dev, "evict region %d\n", rgn->rgn_idx);

	ufshpb_cleanup_lru_info(lru_info, rgn);

	for_each_sub_region(rgn, srgn_idx, srgn)
		ufshpb_purge_active_subregion(hpb, srgn);
}

static int ufshpb_evict_region(struct ufshpb_lu *hpb, struct ufshpb_region *rgn)
{
	unsigned long flags;
	int ret = 0;

	spin_lock_irqsave(&hpb->rgn_state_lock, flags);
	if (rgn->rgn_state == HPB_RGN_PINNED) {
		dev_warn(&hpb->sdev_ufs_lu->sdev_dev,
			 "pinned region cannot drop-out. region %d\n",
			 rgn->rgn_idx);
		goto out;
	}

	if (!list_empty(&rgn->list_lru_rgn)) {
		if (ufshpb_check_srgns_issue_state(hpb, rgn)) {
			ret = -EBUSY;
			goto out;
		}

		if (hpb->is_hcm) {
			spin_unlock_irqrestore(&hpb->rgn_state_lock, flags);
			ret = ufshpb_issue_umap_single_req(hpb, rgn);
			spin_lock_irqsave(&hpb->rgn_state_lock, flags);
			if (ret)
				goto out;
		}

		__ufshpb_evict_region(hpb, rgn);
	}
out:
	spin_unlock_irqrestore(&hpb->rgn_state_lock, flags);
	return ret;
}

static int ufshpb_issue_map_req(struct ufshpb_lu *hpb,
				struct ufshpb_region *rgn,
				struct ufshpb_subregion *srgn)
{
	struct ufshpb_req *map_req;
	unsigned long flags;
	int ret;
	int err = -EAGAIN;
	bool alloc_required = false;
	enum HPB_SRGN_STATE state = HPB_SRGN_INVALID;

	spin_lock_irqsave(&hpb->rgn_state_lock, flags);

	if (ufshpb_get_state(hpb) != HPB_PRESENT) {
		dev_notice(&hpb->sdev_ufs_lu->sdev_dev,
			   "%s: ufshpb state is not PRESENT\n", __func__);
		goto unlock_out;
	}

	if ((rgn->rgn_state == HPB_RGN_INACTIVE) &&
	    (srgn->srgn_state == HPB_SRGN_INVALID)) {
		err = 0;
		goto unlock_out;
	}

	if (srgn->srgn_state == HPB_SRGN_UNUSED)
		alloc_required = true;

	/*
	 * If the subregion is already ISSUED state,
	 * a specific event (e.g., GC or wear-leveling, etc.) occurs in
	 * the device and HPB response for map loading is received.
	 * In this case, after finishing the HPB_READ_BUFFER,
	 * the next HPB_READ_BUFFER is performed again to obtain the latest
	 * map data.
	 */
	if (srgn->srgn_state == HPB_SRGN_ISSUED)
		goto unlock_out;

	srgn->srgn_state = HPB_SRGN_ISSUED;
	spin_unlock_irqrestore(&hpb->rgn_state_lock, flags);

	if (alloc_required) {
		srgn->mctx = ufshpb_get_map_ctx(hpb, srgn->is_last);
		if (!srgn->mctx) {
			dev_err(&hpb->sdev_ufs_lu->sdev_dev,
			    "get map_ctx failed. region %d - %d\n",
			    rgn->rgn_idx, srgn->srgn_idx);
			state = HPB_SRGN_UNUSED;
			goto change_srgn_state;
		}
	}

	map_req = ufshpb_get_map_req(hpb, srgn);
	if (!map_req)
		goto change_srgn_state;


	ret = ufshpb_execute_map_req(hpb, map_req, srgn->is_last);
	if (ret) {
		dev_err(&hpb->sdev_ufs_lu->sdev_dev,
			   "%s: issue map_req failed: %d, region %d - %d\n",
			   __func__, ret, srgn->rgn_idx, srgn->srgn_idx);
		goto free_map_req;
	}
	return 0;

free_map_req:
	ufshpb_put_map_req(hpb, map_req);
change_srgn_state:
	spin_lock_irqsave(&hpb->rgn_state_lock, flags);
	srgn->srgn_state = state;
unlock_out:
	spin_unlock_irqrestore(&hpb->rgn_state_lock, flags);
	return err;
}

static int ufshpb_add_region(struct ufshpb_lu *hpb, struct ufshpb_region *rgn)
{
	struct ufshpb_region *victim_rgn = NULL;
	struct victim_select_info *lru_info = &hpb->lru_info;
	unsigned long flags;
	int ret = 0;

	spin_lock_irqsave(&hpb->rgn_state_lock, flags);
	/*
	 * If region belongs to lru_list, just move the region
	 * to the front of lru list because the state of the region
	 * is already active-state.
	 */
	if (!list_empty(&rgn->list_lru_rgn)) {
		ufshpb_hit_lru_info(lru_info, rgn);
		goto out;
	}

	if (rgn->rgn_state == HPB_RGN_INACTIVE) {
		if (atomic_read(&lru_info->active_cnt) ==
		    lru_info->max_lru_active_cnt) {
			/*
			 * If the maximum number of active regions
			 * is exceeded, evict the least recently used region.
			 * This case may occur when the device responds
			 * to the eviction information late.
			 * It is okay to evict the least recently used region,
			 * because the device could detect this region
			 * by not issuing HPB_READ
			 *
			 * in host control mode, verify that the entering
			 * region has enough reads
			 */
			if (hpb->is_hcm &&
			    rgn->reads < hpb->params.eviction_thld_enter) {
				ret = -EACCES;
				goto out;
			}

			victim_rgn = ufshpb_victim_lru_info(hpb);
			if (!victim_rgn) {
				dev_warn(&hpb->sdev_ufs_lu->sdev_dev,
				    "cannot get victim region %s\n",
				    hpb->is_hcm ? "" : "error");
				ret = -ENOMEM;
				goto out;
			}

			dev_dbg(&hpb->sdev_ufs_lu->sdev_dev,
				"LRU full (%d), choose victim %d\n",
				atomic_read(&lru_info->active_cnt),
				victim_rgn->rgn_idx);

			if (hpb->is_hcm) {
				spin_unlock_irqrestore(&hpb->rgn_state_lock,
						       flags);
				ret = ufshpb_issue_umap_single_req(hpb,
								victim_rgn);
				spin_lock_irqsave(&hpb->rgn_state_lock,
						  flags);
				if (ret)
					goto out;
			}

			__ufshpb_evict_region(hpb, victim_rgn);
		}

		/*
		 * When a region is added to lru_info list_head,
		 * it is guaranteed that the subregion has been
		 * assigned all mctx. If failed, try to receive mctx again
		 * without being added to lru_info list_head
		 */
		ufshpb_add_lru_info(lru_info, rgn);
	}
out:
	spin_unlock_irqrestore(&hpb->rgn_state_lock, flags);
	return ret;
}
/**
 *ufshpb_submit_region_inactive() - submit a region to be inactivated later
 *@hpb: per-LU HPB instance
 *@region_index: the index associated with the region that will be inactivated later
 */
static void ufshpb_submit_region_inactive(struct ufshpb_lu *hpb, int region_index)
{
	int subregion_index;
	struct ufshpb_region *rgn;
	struct ufshpb_subregion *srgn;

	/*
	 * Remove this region from active region list and add it to inactive list
	 */
	spin_lock(&hpb->rsp_list_lock);
	ufshpb_update_inactive_info(hpb, region_index);
	spin_unlock(&hpb->rsp_list_lock);

	rgn = hpb->rgn_tbl + region_index;

	/*
	 * Set subregion state to be HPB_SRGN_INVALID, there will no HPB read on this subregion
	 */
	spin_lock(&hpb->rgn_state_lock);
	if (rgn->rgn_state != HPB_RGN_INACTIVE) {
		for (subregion_index = 0; subregion_index < rgn->srgn_cnt; subregion_index++) {
			srgn = rgn->srgn_tbl + subregion_index;
			if (srgn->srgn_state == HPB_SRGN_VALID)
				srgn->srgn_state = HPB_SRGN_INVALID;
		}
	}
	spin_unlock(&hpb->rgn_state_lock);
}

static void ufshpb_rsp_req_region_update(struct ufshpb_lu *hpb,
					 struct utp_hpb_rsp *rsp_field)
{
	struct ufshpb_region *rgn;
	struct ufshpb_subregion *srgn;
	int i, rgn_i, srgn_i;

	BUILD_BUG_ON(sizeof(struct ufshpb_active_field) != HPB_ACT_FIELD_SIZE);
	/*
	 * If the active region and the inactive region are the same,
	 * we will inactivate this region.
	 * The device could check this (region inactivated) and
	 * will response the proper active region information
	 */
	for (i = 0; i < rsp_field->active_rgn_cnt; i++) {
		rgn_i =
			be16_to_cpu(rsp_field->hpb_active_field[i].active_rgn);
		srgn_i =
			be16_to_cpu(rsp_field->hpb_active_field[i].active_srgn);

		rgn = hpb->rgn_tbl + rgn_i;
		if (hpb->is_hcm &&
		    (rgn->rgn_state != HPB_RGN_ACTIVE || is_rgn_dirty(rgn))) {
			/*
			 * in host control mode, subregion activation
			 * recommendations are only allowed to active regions.
			 * Also, ignore recommendations for dirty regions - the
			 * host will make decisions concerning those by himself
			 */
			continue;
		}

		dev_dbg(&hpb->sdev_ufs_lu->sdev_dev,
			"activate(%d) region %d - %d\n", i, rgn_i, srgn_i);

		spin_lock(&hpb->rsp_list_lock);
		ufshpb_update_active_info(hpb, rgn_i, srgn_i);
		spin_unlock(&hpb->rsp_list_lock);

		srgn = rgn->srgn_tbl + srgn_i;

		/* blocking HPB_READ */
		spin_lock(&hpb->rgn_state_lock);
		if (srgn->srgn_state == HPB_SRGN_VALID)
			srgn->srgn_state = HPB_SRGN_INVALID;
		spin_unlock(&hpb->rgn_state_lock);
	}

	if (hpb->is_hcm) {
		/*
		 * in host control mode the device is not allowed to inactivate
		 * regions
		 */
		goto out;
	}

	for (i = 0; i < rsp_field->inactive_rgn_cnt; i++) {
		rgn_i = be16_to_cpu(rsp_field->hpb_inactive_field[i]);
		dev_dbg(&hpb->sdev_ufs_lu->sdev_dev, "inactivate(%d) region %d\n", i, rgn_i);
		ufshpb_submit_region_inactive(hpb, rgn_i);
	}

out:
	dev_dbg(&hpb->sdev_ufs_lu->sdev_dev, "Noti: #ACT %u #INACT %u\n",
		rsp_field->active_rgn_cnt, rsp_field->inactive_rgn_cnt);

	if (ufshpb_get_state(hpb) == HPB_PRESENT)
		queue_work(ufshpb_wq, &hpb->map_work);
}

/*
 * Set the flags of all active regions to RGN_FLAG_UPDATE to let host side reload L2P entries later
 */
static void ufshpb_set_regions_update(struct ufshpb_lu *hpb)
{
	struct victim_select_info *lru_info = &hpb->lru_info;
	struct ufshpb_region *rgn;
	unsigned long flags;

	spin_lock_irqsave(&hpb->rgn_state_lock, flags);

	list_for_each_entry(rgn, &lru_info->lh_lru_rgn, list_lru_rgn)
		set_bit(RGN_FLAG_UPDATE, &rgn->rgn_flags);

	spin_unlock_irqrestore(&hpb->rgn_state_lock, flags);
}

static void ufshpb_dev_reset_handler(struct ufs_hba *hba)
{
	struct scsi_device *sdev;
	struct ufshpb_lu *hpb;

	__shost_for_each_device(sdev, hba->host) {
		hpb = ufshpb_get_hpb_data(sdev);
		if (!hpb)
			continue;

		if (hpb->is_hcm) {
			/*
			 * For the HPB host control mode, in case device powered up and lost HPB
			 * information, we will set the region flag to be RGN_FLAG_UPDATE, it will
			 * let host reload its L2P entries(reactivate region in the UFS device).
			 */
			ufshpb_set_regions_update(hpb);
		} else {
			/*
			 * For the HPB device control mode, if host side receives 02h:HPB Operation
			 * in UPIU response, which means device recommends the host side should
			 * inactivate all active regions. Here we add all active regions to inactive
			 * list, they will be inactivated later in ufshpb_map_work_handler().
			 */
			struct victim_select_info *lru_info = &hpb->lru_info;
			struct ufshpb_region *rgn;

			list_for_each_entry(rgn, &lru_info->lh_lru_rgn, list_lru_rgn)
				ufshpb_submit_region_inactive(hpb, rgn->rgn_idx);

			if (ufshpb_get_state(hpb) == HPB_PRESENT)
				queue_work(ufshpb_wq, &hpb->map_work);
		}
	}
}

/*
 * This function will parse recommended active subregion information in sense
 * data field of response UPIU with SAM_STAT_GOOD state.
 */
void ufshpb_rsp_upiu(struct ufs_hba *hba, struct ufshcd_lrb *lrbp)
{
	struct ufshpb_lu *hpb = ufshpb_get_hpb_data(lrbp->cmd->device);
	struct utp_hpb_rsp *rsp_field = &lrbp->ucd_rsp_ptr->hr;
	int data_seg_len;

	data_seg_len = be32_to_cpu(lrbp->ucd_rsp_ptr->header.dword_2)
		& MASK_RSP_UPIU_DATA_SEG_LEN;

	/* If data segment length is zero, rsp_field is not valid */
	if (!data_seg_len)
		return;

	if (unlikely(lrbp->lun != rsp_field->lun)) {
		struct scsi_device *sdev;
		bool found = false;

		__shost_for_each_device(sdev, hba->host) {
			hpb = ufshpb_get_hpb_data(sdev);

			if (!hpb)
				continue;

			if (rsp_field->lun == hpb->lun) {
				found = true;
				break;
			}
		}

		if (!found)
			return;
	}

	if (!hpb)
		return;

	if (ufshpb_get_state(hpb) == HPB_INIT)
		return;

	if ((ufshpb_get_state(hpb) != HPB_PRESENT) &&
	    (ufshpb_get_state(hpb) != HPB_SUSPEND)) {
		dev_notice(&hpb->sdev_ufs_lu->sdev_dev,
			   "%s: ufshpb state is not PRESENT/SUSPEND\n",
			   __func__);
		return;
	}

	BUILD_BUG_ON(sizeof(struct utp_hpb_rsp) != UTP_HPB_RSP_SIZE);

	if (!ufshpb_is_hpb_rsp_valid(hba, lrbp, rsp_field))
		return;

	hpb->stats.rcmd_noti_cnt++;

	switch (rsp_field->hpb_op) {
	case HPB_RSP_REQ_REGION_UPDATE:
		if (data_seg_len != DEV_DATA_SEG_LEN)
			dev_warn(&hpb->sdev_ufs_lu->sdev_dev,
				 "%s: data seg length is not same.\n",
				 __func__);
		ufshpb_rsp_req_region_update(hpb, rsp_field);
		break;
	case HPB_RSP_DEV_RESET:
		dev_warn(&hpb->sdev_ufs_lu->sdev_dev,
			 "UFS device lost HPB information during PM.\n");
		ufshpb_dev_reset_handler(hba);

		break;
	default:
		dev_notice(&hpb->sdev_ufs_lu->sdev_dev,
			   "hpb_op is not available: %d\n",
			   rsp_field->hpb_op);
		break;
	}
}

static void ufshpb_add_active_list(struct ufshpb_lu *hpb,
				   struct ufshpb_region *rgn,
				   struct ufshpb_subregion *srgn)
{
	if (!list_empty(&rgn->list_inact_rgn))
		return;

	if (!list_empty(&srgn->list_act_srgn)) {
		list_move(&srgn->list_act_srgn, &hpb->lh_act_srgn);
		return;
	}

	list_add(&srgn->list_act_srgn, &hpb->lh_act_srgn);
}

static void ufshpb_add_pending_evict_list(struct ufshpb_lu *hpb,
					  struct ufshpb_region *rgn,
					  struct list_head *pending_list)
{
	struct ufshpb_subregion *srgn;
	int srgn_idx;

	if (!list_empty(&rgn->list_inact_rgn))
		return;

	for_each_sub_region(rgn, srgn_idx, srgn)
		if (!list_empty(&srgn->list_act_srgn))
			return;

	list_add_tail(&rgn->list_inact_rgn, pending_list);
}

static void ufshpb_run_active_subregion_list(struct ufshpb_lu *hpb)
{
	struct ufshpb_region *rgn;
	struct ufshpb_subregion *srgn;
	unsigned long flags;
	int ret = 0;

	spin_lock_irqsave(&hpb->rsp_list_lock, flags);
	while ((srgn = list_first_entry_or_null(&hpb->lh_act_srgn,
						struct ufshpb_subregion,
						list_act_srgn))) {
		if (ufshpb_get_state(hpb) == HPB_SUSPEND)
			break;

		list_del_init(&srgn->list_act_srgn);
		spin_unlock_irqrestore(&hpb->rsp_list_lock, flags);

		rgn = hpb->rgn_tbl + srgn->rgn_idx;
		ret = ufshpb_add_region(hpb, rgn);
		if (ret)
			goto active_failed;

		ret = ufshpb_issue_map_req(hpb, rgn, srgn);
		if (ret) {
			dev_err(&hpb->sdev_ufs_lu->sdev_dev,
			    "issue map_req failed. ret %d, region %d - %d\n",
			    ret, rgn->rgn_idx, srgn->srgn_idx);
			goto active_failed;
		}
		spin_lock_irqsave(&hpb->rsp_list_lock, flags);
	}
	spin_unlock_irqrestore(&hpb->rsp_list_lock, flags);
	return;

active_failed:
	dev_err(&hpb->sdev_ufs_lu->sdev_dev, "failed to activate region %d - %d, will retry\n",
		   rgn->rgn_idx, srgn->srgn_idx);
	spin_lock_irqsave(&hpb->rsp_list_lock, flags);
	ufshpb_add_active_list(hpb, rgn, srgn);
	spin_unlock_irqrestore(&hpb->rsp_list_lock, flags);
}

static void ufshpb_run_inactive_region_list(struct ufshpb_lu *hpb)
{
	struct ufshpb_region *rgn;
	unsigned long flags;
	int ret;
	LIST_HEAD(pending_list);

	spin_lock_irqsave(&hpb->rsp_list_lock, flags);
	while ((rgn = list_first_entry_or_null(&hpb->lh_inact_rgn,
					       struct ufshpb_region,
					       list_inact_rgn))) {
		if (ufshpb_get_state(hpb) == HPB_SUSPEND)
			break;

		list_del_init(&rgn->list_inact_rgn);
		spin_unlock_irqrestore(&hpb->rsp_list_lock, flags);

		ret = ufshpb_evict_region(hpb, rgn);
		if (ret) {
			spin_lock_irqsave(&hpb->rsp_list_lock, flags);
			ufshpb_add_pending_evict_list(hpb, rgn, &pending_list);
			spin_unlock_irqrestore(&hpb->rsp_list_lock, flags);
		}

		spin_lock_irqsave(&hpb->rsp_list_lock, flags);
	}

	list_splice(&pending_list, &hpb->lh_inact_rgn);
	spin_unlock_irqrestore(&hpb->rsp_list_lock, flags);
}

static void ufshpb_normalization_work_handler(struct work_struct *work)
{
	struct ufshpb_lu *hpb = container_of(work, struct ufshpb_lu,
					     ufshpb_normalization_work);
	int rgn_idx;
	u8 factor = hpb->params.normalization_factor;

	for (rgn_idx = 0; rgn_idx < hpb->rgns_per_lu; rgn_idx++) {
		struct ufshpb_region *rgn = hpb->rgn_tbl + rgn_idx;
		int srgn_idx;

		spin_lock(&rgn->rgn_lock);
		rgn->reads = 0;
		for (srgn_idx = 0; srgn_idx < hpb->srgns_per_rgn; srgn_idx++) {
			struct ufshpb_subregion *srgn = rgn->srgn_tbl + srgn_idx;

			srgn->reads >>= factor;
			rgn->reads += srgn->reads;
		}
		spin_unlock(&rgn->rgn_lock);

		if (rgn->rgn_state != HPB_RGN_ACTIVE || rgn->reads)
			continue;

		/* if region is active but has no reads - inactivate it */
		spin_lock(&hpb->rsp_list_lock);
		ufshpb_update_inactive_info(hpb, rgn->rgn_idx);
		spin_unlock(&hpb->rsp_list_lock);
	}
}

static void ufshpb_map_work_handler(struct work_struct *work)
{
	struct ufshpb_lu *hpb = container_of(work, struct ufshpb_lu, map_work);

	if (ufshpb_get_state(hpb) != HPB_PRESENT) {
		dev_notice(&hpb->sdev_ufs_lu->sdev_dev,
			   "%s: ufshpb state is not PRESENT\n", __func__);
		return;
	}

	ufshpb_run_inactive_region_list(hpb);
	ufshpb_run_active_subregion_list(hpb);
}

/*
 * this function doesn't need to hold lock due to be called in init.
 * (rgn_state_lock, rsp_list_lock, etc..)
 */
static int ufshpb_init_pinned_active_region(struct ufs_hba *hba,
					    struct ufshpb_lu *hpb,
					    struct ufshpb_region *rgn)
{
	struct ufshpb_subregion *srgn;
	int srgn_idx, i;
	int err = 0;

	for_each_sub_region(rgn, srgn_idx, srgn) {
		srgn->mctx = ufshpb_get_map_ctx(hpb, srgn->is_last);
		srgn->srgn_state = HPB_SRGN_INVALID;
		if (!srgn->mctx) {
			err = -ENOMEM;
			dev_err(hba->dev,
				"alloc mctx for pinned region failed\n");
			goto release;
		}

		list_add_tail(&srgn->list_act_srgn, &hpb->lh_act_srgn);
	}

	rgn->rgn_state = HPB_RGN_PINNED;
	return 0;

release:
	for (i = 0; i < srgn_idx; i++) {
		srgn = rgn->srgn_tbl + i;
		ufshpb_put_map_ctx(hpb, srgn->mctx);
	}
	return err;
}

static void ufshpb_init_subregion_tbl(struct ufshpb_lu *hpb,
				      struct ufshpb_region *rgn, bool last)
{
	int srgn_idx;
	struct ufshpb_subregion *srgn;

	for_each_sub_region(rgn, srgn_idx, srgn) {
		INIT_LIST_HEAD(&srgn->list_act_srgn);

		srgn->rgn_idx = rgn->rgn_idx;
		srgn->srgn_idx = srgn_idx;
		srgn->srgn_state = HPB_SRGN_UNUSED;
	}

	if (unlikely(last && hpb->last_srgn_entries))
		srgn->is_last = true;
}

static int ufshpb_alloc_subregion_tbl(struct ufshpb_lu *hpb,
				      struct ufshpb_region *rgn, int srgn_cnt)
{
	rgn->srgn_tbl = kvcalloc(srgn_cnt, sizeof(struct ufshpb_subregion),
				 GFP_KERNEL);
	if (!rgn->srgn_tbl)
		return -ENOMEM;

	rgn->srgn_cnt = srgn_cnt;
	return 0;
}

static void ufshpb_lu_parameter_init(struct ufs_hba *hba,
				     struct ufshpb_lu *hpb,
				     struct ufshpb_dev_info *hpb_dev_info,
				     struct ufshpb_lu_info *hpb_lu_info)
{
	u32 entries_per_rgn;
	u64 rgn_mem_size, tmp;

	if (ufshpb_is_legacy(hba))
		hpb->pre_req_max_tr_len = HPB_LEGACY_CHUNK_HIGH;
	else
		hpb->pre_req_max_tr_len = hpb_dev_info->max_hpb_single_cmd;

	hpb->lu_pinned_start = hpb_lu_info->pinned_start;
	hpb->lu_pinned_end = hpb_lu_info->num_pinned ?
		(hpb_lu_info->pinned_start + hpb_lu_info->num_pinned - 1)
		: PINNED_NOT_SET;
	hpb->lru_info.max_lru_active_cnt =
		hpb_lu_info->max_active_rgns - hpb_lu_info->num_pinned;

	rgn_mem_size = (1ULL << hpb_dev_info->rgn_size) * HPB_RGN_SIZE_UNIT
			* HPB_ENTRY_SIZE;
	do_div(rgn_mem_size, HPB_ENTRY_BLOCK_SIZE);
	hpb->srgn_mem_size = (1ULL << hpb_dev_info->srgn_size)
		* HPB_RGN_SIZE_UNIT / HPB_ENTRY_BLOCK_SIZE * HPB_ENTRY_SIZE;

	tmp = rgn_mem_size;
	do_div(tmp, HPB_ENTRY_SIZE);
	entries_per_rgn = (u32)tmp;
	hpb->entries_per_rgn_shift = ilog2(entries_per_rgn);
	hpb->entries_per_rgn_mask = entries_per_rgn - 1;

	hpb->entries_per_srgn = hpb->srgn_mem_size / HPB_ENTRY_SIZE;
	hpb->entries_per_srgn_shift = ilog2(hpb->entries_per_srgn);
	hpb->entries_per_srgn_mask = hpb->entries_per_srgn - 1;

	tmp = rgn_mem_size;
	do_div(tmp, hpb->srgn_mem_size);
	hpb->srgns_per_rgn = (int)tmp;

	hpb->rgns_per_lu = DIV_ROUND_UP(hpb_lu_info->num_blocks,
				entries_per_rgn);
	hpb->srgns_per_lu = DIV_ROUND_UP(hpb_lu_info->num_blocks,
				(hpb->srgn_mem_size / HPB_ENTRY_SIZE));
	hpb->last_srgn_entries = hpb_lu_info->num_blocks
				 % (hpb->srgn_mem_size / HPB_ENTRY_SIZE);

	hpb->pages_per_srgn = DIV_ROUND_UP(hpb->srgn_mem_size, PAGE_SIZE);

	if (hpb_dev_info->control_mode == HPB_HOST_CONTROL)
		hpb->is_hcm = true;
}

static int ufshpb_alloc_region_tbl(struct ufs_hba *hba, struct ufshpb_lu *hpb)
{
	struct ufshpb_region *rgn_table, *rgn;
	int rgn_idx, i;
	int ret = 0;

	rgn_table = kvcalloc(hpb->rgns_per_lu, sizeof(struct ufshpb_region),
			    GFP_KERNEL);
	if (!rgn_table)
		return -ENOMEM;

	for (rgn_idx = 0; rgn_idx < hpb->rgns_per_lu; rgn_idx++) {
		int srgn_cnt = hpb->srgns_per_rgn;
		bool last_srgn = false;

		rgn = rgn_table + rgn_idx;
		rgn->rgn_idx = rgn_idx;

		spin_lock_init(&rgn->rgn_lock);

		INIT_LIST_HEAD(&rgn->list_inact_rgn);
		INIT_LIST_HEAD(&rgn->list_lru_rgn);
		INIT_LIST_HEAD(&rgn->list_expired_rgn);

		if (rgn_idx == hpb->rgns_per_lu - 1) {
			srgn_cnt = ((hpb->srgns_per_lu - 1) %
				    hpb->srgns_per_rgn) + 1;
			last_srgn = true;
		}

		ret = ufshpb_alloc_subregion_tbl(hpb, rgn, srgn_cnt);
		if (ret)
			goto release_srgn_table;
		ufshpb_init_subregion_tbl(hpb, rgn, last_srgn);

		if (ufshpb_is_pinned_region(hpb, rgn_idx)) {
			ret = ufshpb_init_pinned_active_region(hba, hpb, rgn);
			if (ret)
				goto release_srgn_table;
		} else {
			rgn->rgn_state = HPB_RGN_INACTIVE;
		}

		rgn->rgn_flags = 0;
		rgn->hpb = hpb;
	}

	hpb->rgn_tbl = rgn_table;

	return 0;

release_srgn_table:
	for (i = 0; i <= rgn_idx; i++)
		kvfree(rgn_table[i].srgn_tbl);

	kvfree(rgn_table);
	return ret;
}

static void ufshpb_destroy_subregion_tbl(struct ufshpb_lu *hpb,
					 struct ufshpb_region *rgn)
{
	int srgn_idx;
	struct ufshpb_subregion *srgn;

	for_each_sub_region(rgn, srgn_idx, srgn)
		if (srgn->srgn_state != HPB_SRGN_UNUSED) {
			srgn->srgn_state = HPB_SRGN_UNUSED;
			ufshpb_put_map_ctx(hpb, srgn->mctx);
		}
}

static void ufshpb_destroy_region_tbl(struct ufshpb_lu *hpb)
{
	int rgn_idx;

	for (rgn_idx = 0; rgn_idx < hpb->rgns_per_lu; rgn_idx++) {
		struct ufshpb_region *rgn;

		rgn = hpb->rgn_tbl + rgn_idx;
		if (rgn->rgn_state != HPB_RGN_INACTIVE) {
			rgn->rgn_state = HPB_RGN_INACTIVE;

			ufshpb_destroy_subregion_tbl(hpb, rgn);
		}

		kvfree(rgn->srgn_tbl);
	}

	kvfree(hpb->rgn_tbl);
}

/* SYSFS functions */
#define ufshpb_sysfs_attr_show_func(__name)				\
static ssize_t __name##_show(struct device *dev,			\
	struct device_attribute *attr, char *buf)			\
{									\
	struct scsi_device *sdev = to_scsi_device(dev);			\
	struct ufshpb_lu *hpb = ufshpb_get_hpb_data(sdev);		\
									\
	if (!hpb)							\
		return -ENODEV;						\
									\
	return sysfs_emit(buf, "%llu\n", hpb->stats.__name);		\
}									\
\
static DEVICE_ATTR_RO(__name)

ufshpb_sysfs_attr_show_func(hit_cnt);
ufshpb_sysfs_attr_show_func(miss_cnt);
ufshpb_sysfs_attr_show_func(rcmd_noti_cnt);
ufshpb_sysfs_attr_show_func(rcmd_active_cnt);
ufshpb_sysfs_attr_show_func(rcmd_inactive_cnt);
ufshpb_sysfs_attr_show_func(map_req_cnt);
ufshpb_sysfs_attr_show_func(umap_req_cnt);

static struct attribute *hpb_dev_stat_attrs[] = {
	&dev_attr_hit_cnt.attr,
	&dev_attr_miss_cnt.attr,
	&dev_attr_rcmd_noti_cnt.attr,
	&dev_attr_rcmd_active_cnt.attr,
	&dev_attr_rcmd_inactive_cnt.attr,
	&dev_attr_map_req_cnt.attr,
	&dev_attr_umap_req_cnt.attr,
	NULL,
};

struct attribute_group ufs_sysfs_hpb_stat_group = {
	.name = "hpb_stats",
	.attrs = hpb_dev_stat_attrs,
};

/* SYSFS functions */
#define ufshpb_sysfs_param_show_func(__name)				\
static ssize_t __name##_show(struct device *dev,			\
	struct device_attribute *attr, char *buf)			\
{									\
	struct scsi_device *sdev = to_scsi_device(dev);			\
	struct ufshpb_lu *hpb = ufshpb_get_hpb_data(sdev);		\
									\
	if (!hpb)							\
		return -ENODEV;						\
									\
	return sysfs_emit(buf, "%d\n", hpb->params.__name);		\
}

ufshpb_sysfs_param_show_func(requeue_timeout_ms);
static ssize_t
requeue_timeout_ms_store(struct device *dev, struct device_attribute *attr,
			 const char *buf, size_t count)
{
	struct scsi_device *sdev = to_scsi_device(dev);
	struct ufshpb_lu *hpb = ufshpb_get_hpb_data(sdev);
	int val;

	if (!hpb)
		return -ENODEV;

	if (kstrtouint(buf, 0, &val))
		return -EINVAL;

	if (val < 0)
		return -EINVAL;

	hpb->params.requeue_timeout_ms = val;

	return count;
}
static DEVICE_ATTR_RW(requeue_timeout_ms);

ufshpb_sysfs_param_show_func(activation_thld);
static ssize_t
activation_thld_store(struct device *dev, struct device_attribute *attr,
		      const char *buf, size_t count)
{
	struct scsi_device *sdev = to_scsi_device(dev);
	struct ufshpb_lu *hpb = ufshpb_get_hpb_data(sdev);
	int val;

	if (!hpb)
		return -ENODEV;

	if (!hpb->is_hcm)
		return -EOPNOTSUPP;

	if (kstrtouint(buf, 0, &val))
		return -EINVAL;

	if (val <= 0)
		return -EINVAL;

	hpb->params.activation_thld = val;

	return count;
}
static DEVICE_ATTR_RW(activation_thld);

ufshpb_sysfs_param_show_func(normalization_factor);
static ssize_t
normalization_factor_store(struct device *dev, struct device_attribute *attr,
			   const char *buf, size_t count)
{
	struct scsi_device *sdev = to_scsi_device(dev);
	struct ufshpb_lu *hpb = ufshpb_get_hpb_data(sdev);
	int val;

	if (!hpb)
		return -ENODEV;

	if (!hpb->is_hcm)
		return -EOPNOTSUPP;

	if (kstrtouint(buf, 0, &val))
		return -EINVAL;

	if (val <= 0 || val > ilog2(hpb->entries_per_srgn))
		return -EINVAL;

	hpb->params.normalization_factor = val;

	return count;
}
static DEVICE_ATTR_RW(normalization_factor);

ufshpb_sysfs_param_show_func(eviction_thld_enter);
static ssize_t
eviction_thld_enter_store(struct device *dev, struct device_attribute *attr,
			  const char *buf, size_t count)
{
	struct scsi_device *sdev = to_scsi_device(dev);
	struct ufshpb_lu *hpb = ufshpb_get_hpb_data(sdev);
	int val;

	if (!hpb)
		return -ENODEV;

	if (!hpb->is_hcm)
		return -EOPNOTSUPP;

	if (kstrtouint(buf, 0, &val))
		return -EINVAL;

	if (val <= hpb->params.eviction_thld_exit)
		return -EINVAL;

	hpb->params.eviction_thld_enter = val;

	return count;
}
static DEVICE_ATTR_RW(eviction_thld_enter);

ufshpb_sysfs_param_show_func(eviction_thld_exit);
static ssize_t
eviction_thld_exit_store(struct device *dev, struct device_attribute *attr,
			 const char *buf, size_t count)
{
	struct scsi_device *sdev = to_scsi_device(dev);
	struct ufshpb_lu *hpb = ufshpb_get_hpb_data(sdev);
	int val;

	if (!hpb)
		return -ENODEV;

	if (!hpb->is_hcm)
		return -EOPNOTSUPP;

	if (kstrtouint(buf, 0, &val))
		return -EINVAL;

	if (val <= hpb->params.activation_thld)
		return -EINVAL;

	hpb->params.eviction_thld_exit = val;

	return count;
}
static DEVICE_ATTR_RW(eviction_thld_exit);

ufshpb_sysfs_param_show_func(read_timeout_ms);
static ssize_t
read_timeout_ms_store(struct device *dev, struct device_attribute *attr,
		      const char *buf, size_t count)
{
	struct scsi_device *sdev = to_scsi_device(dev);
	struct ufshpb_lu *hpb = ufshpb_get_hpb_data(sdev);
	int val;

	if (!hpb)
		return -ENODEV;

	if (!hpb->is_hcm)
		return -EOPNOTSUPP;

	if (kstrtouint(buf, 0, &val))
		return -EINVAL;

	/* read_timeout >> timeout_polling_interval */
	if (val < hpb->params.timeout_polling_interval_ms * 2)
		return -EINVAL;

	hpb->params.read_timeout_ms = val;

	return count;
}
static DEVICE_ATTR_RW(read_timeout_ms);

ufshpb_sysfs_param_show_func(read_timeout_expiries);
static ssize_t
read_timeout_expiries_store(struct device *dev, struct device_attribute *attr,
			    const char *buf, size_t count)
{
	struct scsi_device *sdev = to_scsi_device(dev);
	struct ufshpb_lu *hpb = ufshpb_get_hpb_data(sdev);
	int val;

	if (!hpb)
		return -ENODEV;

	if (!hpb->is_hcm)
		return -EOPNOTSUPP;

	if (kstrtouint(buf, 0, &val))
		return -EINVAL;

	if (val <= 0)
		return -EINVAL;

	hpb->params.read_timeout_expiries = val;

	return count;
}
static DEVICE_ATTR_RW(read_timeout_expiries);

ufshpb_sysfs_param_show_func(timeout_polling_interval_ms);
static ssize_t
timeout_polling_interval_ms_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	struct scsi_device *sdev = to_scsi_device(dev);
	struct ufshpb_lu *hpb = ufshpb_get_hpb_data(sdev);
	int val;

	if (!hpb)
		return -ENODEV;

	if (!hpb->is_hcm)
		return -EOPNOTSUPP;

	if (kstrtouint(buf, 0, &val))
		return -EINVAL;

	/* timeout_polling_interval << read_timeout */
	if (val <= 0 || val > hpb->params.read_timeout_ms / 2)
		return -EINVAL;

	hpb->params.timeout_polling_interval_ms = val;

	return count;
}
static DEVICE_ATTR_RW(timeout_polling_interval_ms);

ufshpb_sysfs_param_show_func(inflight_map_req);
static ssize_t inflight_map_req_store(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t count)
{
	struct scsi_device *sdev = to_scsi_device(dev);
	struct ufshpb_lu *hpb = ufshpb_get_hpb_data(sdev);
	int val;

	if (!hpb)
		return -ENODEV;

	if (!hpb->is_hcm)
		return -EOPNOTSUPP;

	if (kstrtouint(buf, 0, &val))
		return -EINVAL;

	if (val <= 0 || val > hpb->sdev_ufs_lu->queue_depth - 1)
		return -EINVAL;

	hpb->params.inflight_map_req = val;

	return count;
}
static DEVICE_ATTR_RW(inflight_map_req);

static void ufshpb_hcm_param_init(struct ufshpb_lu *hpb)
{
	hpb->params.activation_thld = ACTIVATION_THRESHOLD;
	hpb->params.normalization_factor = 1;
	hpb->params.eviction_thld_enter = (ACTIVATION_THRESHOLD << 5);
	hpb->params.eviction_thld_exit = (ACTIVATION_THRESHOLD << 4);
	hpb->params.read_timeout_ms = READ_TO_MS;
	hpb->params.read_timeout_expiries = READ_TO_EXPIRIES;
	hpb->params.timeout_polling_interval_ms = POLLING_INTERVAL_MS;
	hpb->params.inflight_map_req = THROTTLE_MAP_REQ_DEFAULT;
}

static struct attribute *hpb_dev_param_attrs[] = {
	&dev_attr_requeue_timeout_ms.attr,
	&dev_attr_activation_thld.attr,
	&dev_attr_normalization_factor.attr,
	&dev_attr_eviction_thld_enter.attr,
	&dev_attr_eviction_thld_exit.attr,
	&dev_attr_read_timeout_ms.attr,
	&dev_attr_read_timeout_expiries.attr,
	&dev_attr_timeout_polling_interval_ms.attr,
	&dev_attr_inflight_map_req.attr,
	NULL,
};

struct attribute_group ufs_sysfs_hpb_param_group = {
	.name = "hpb_params",
	.attrs = hpb_dev_param_attrs,
};

static int ufshpb_pre_req_mempool_init(struct ufshpb_lu *hpb)
{
	struct ufshpb_req *pre_req = NULL, *t;
	int qd = hpb->sdev_ufs_lu->queue_depth / 2;
	int i;

	INIT_LIST_HEAD(&hpb->lh_pre_req_free);

	hpb->pre_req = kcalloc(qd, sizeof(struct ufshpb_req), GFP_KERNEL);
	hpb->throttle_pre_req = qd;
	hpb->num_inflight_pre_req = 0;

	if (!hpb->pre_req)
		goto release_mem;

	for (i = 0; i < qd; i++) {
		pre_req = hpb->pre_req + i;
		INIT_LIST_HEAD(&pre_req->list_req);
		pre_req->req = NULL;

		pre_req->bio = bio_alloc(NULL, 1, 0, GFP_KERNEL);
		if (!pre_req->bio)
			goto release_mem;

		pre_req->wb.m_page = alloc_page(GFP_KERNEL | __GFP_ZERO);
		if (!pre_req->wb.m_page) {
			bio_put(pre_req->bio);
			goto release_mem;
		}

		list_add_tail(&pre_req->list_req, &hpb->lh_pre_req_free);
	}

	return 0;
release_mem:
	list_for_each_entry_safe(pre_req, t, &hpb->lh_pre_req_free, list_req) {
		list_del_init(&pre_req->list_req);
		bio_put(pre_req->bio);
		__free_page(pre_req->wb.m_page);
	}

	kfree(hpb->pre_req);
	return -ENOMEM;
}

static void ufshpb_pre_req_mempool_destroy(struct ufshpb_lu *hpb)
{
	struct ufshpb_req *pre_req = NULL;
	int i;

	for (i = 0; i < hpb->throttle_pre_req; i++) {
		pre_req = hpb->pre_req + i;
		bio_put(hpb->pre_req[i].bio);
		if (!pre_req->wb.m_page)
			__free_page(hpb->pre_req[i].wb.m_page);
		list_del_init(&pre_req->list_req);
	}

	kfree(hpb->pre_req);
}

static void ufshpb_stat_init(struct ufshpb_lu *hpb)
{
	hpb->stats.hit_cnt = 0;
	hpb->stats.miss_cnt = 0;
	hpb->stats.rcmd_noti_cnt = 0;
	hpb->stats.rcmd_active_cnt = 0;
	hpb->stats.rcmd_inactive_cnt = 0;
	hpb->stats.map_req_cnt = 0;
	hpb->stats.umap_req_cnt = 0;
}

static void ufshpb_param_init(struct ufshpb_lu *hpb)
{
	hpb->params.requeue_timeout_ms = HPB_REQUEUE_TIME_MS;
	if (hpb->is_hcm)
		ufshpb_hcm_param_init(hpb);
}

static int ufshpb_lu_hpb_init(struct ufs_hba *hba, struct ufshpb_lu *hpb)
{
	int ret;

	spin_lock_init(&hpb->rgn_state_lock);
	spin_lock_init(&hpb->rsp_list_lock);
	spin_lock_init(&hpb->param_lock);

	INIT_LIST_HEAD(&hpb->lru_info.lh_lru_rgn);
	INIT_LIST_HEAD(&hpb->lh_act_srgn);
	INIT_LIST_HEAD(&hpb->lh_inact_rgn);
	INIT_LIST_HEAD(&hpb->list_hpb_lu);

	INIT_WORK(&hpb->map_work, ufshpb_map_work_handler);
	if (hpb->is_hcm) {
		INIT_WORK(&hpb->ufshpb_normalization_work,
			  ufshpb_normalization_work_handler);
		INIT_DELAYED_WORK(&hpb->ufshpb_read_to_work,
				  ufshpb_read_to_handler);
	}

	hpb->map_req_cache = kmem_cache_create("ufshpb_req_cache",
			  sizeof(struct ufshpb_req), 0, 0, NULL);
	if (!hpb->map_req_cache) {
		dev_err(hba->dev, "ufshpb(%d) ufshpb_req_cache create fail",
			hpb->lun);
		return -ENOMEM;
	}

	hpb->m_page_cache = kmem_cache_create("ufshpb_m_page_cache",
			  sizeof(struct page *) * hpb->pages_per_srgn,
			  0, 0, NULL);
	if (!hpb->m_page_cache) {
		dev_err(hba->dev, "ufshpb(%d) ufshpb_m_page_cache create fail",
			hpb->lun);
		ret = -ENOMEM;
		goto release_req_cache;
	}

	ret = ufshpb_pre_req_mempool_init(hpb);
	if (ret) {
		dev_err(hba->dev, "ufshpb(%d) pre_req_mempool init fail",
			hpb->lun);
		goto release_m_page_cache;
	}

	ret = ufshpb_alloc_region_tbl(hba, hpb);
	if (ret)
		goto release_pre_req_mempool;

	ufshpb_stat_init(hpb);
	ufshpb_param_init(hpb);

	if (hpb->is_hcm) {
		unsigned int poll;

		poll = hpb->params.timeout_polling_interval_ms;
		schedule_delayed_work(&hpb->ufshpb_read_to_work,
				      msecs_to_jiffies(poll));
	}

	return 0;

release_pre_req_mempool:
	ufshpb_pre_req_mempool_destroy(hpb);
release_m_page_cache:
	kmem_cache_destroy(hpb->m_page_cache);
release_req_cache:
	kmem_cache_destroy(hpb->map_req_cache);
	return ret;
}

static struct ufshpb_lu *
ufshpb_alloc_hpb_lu(struct ufs_hba *hba, struct scsi_device *sdev,
		    struct ufshpb_dev_info *hpb_dev_info,
		    struct ufshpb_lu_info *hpb_lu_info)
{
	struct ufshpb_lu *hpb;
	int ret;

	hpb = kzalloc(sizeof(struct ufshpb_lu), GFP_KERNEL);
	if (!hpb)
		return NULL;

	hpb->lun = sdev->lun;
	hpb->sdev_ufs_lu = sdev;

	ufshpb_lu_parameter_init(hba, hpb, hpb_dev_info, hpb_lu_info);

	ret = ufshpb_lu_hpb_init(hba, hpb);
	if (ret) {
		dev_err(hba->dev, "hpb lu init failed. ret %d", ret);
		goto release_hpb;
	}

	sdev->hostdata = hpb;
	return hpb;

release_hpb:
	kfree(hpb);
	return NULL;
}

static void ufshpb_discard_rsp_lists(struct ufshpb_lu *hpb)
{
	struct ufshpb_region *rgn, *next_rgn;
	struct ufshpb_subregion *srgn, *next_srgn;
	unsigned long flags;

	/*
	 * If the device reset occurred, the remaining HPB region information
	 * may be stale. Therefore, by discarding the lists of HPB response
	 * that remained after reset, we prevent unnecessary work.
	 */
	spin_lock_irqsave(&hpb->rsp_list_lock, flags);
	list_for_each_entry_safe(rgn, next_rgn, &hpb->lh_inact_rgn,
				 list_inact_rgn)
		list_del_init(&rgn->list_inact_rgn);

	list_for_each_entry_safe(srgn, next_srgn, &hpb->lh_act_srgn,
				 list_act_srgn)
		list_del_init(&srgn->list_act_srgn);
	spin_unlock_irqrestore(&hpb->rsp_list_lock, flags);
}

static void ufshpb_cancel_jobs(struct ufshpb_lu *hpb)
{
	if (hpb->is_hcm) {
		cancel_delayed_work_sync(&hpb->ufshpb_read_to_work);
		cancel_work_sync(&hpb->ufshpb_normalization_work);
	}
	cancel_work_sync(&hpb->map_work);
}

static bool ufshpb_check_hpb_reset_query(struct ufs_hba *hba)
{
	int err = 0;
	bool flag_res = true;
	int try;

	/* wait for the device to complete HPB reset query */
	for (try = 0; try < HPB_RESET_REQ_RETRIES; try++) {
		dev_dbg(hba->dev,
			"%s start flag reset polling %d times\n",
			__func__, try);

		/* Poll fHpbReset flag to be cleared */
		err = ufshcd_query_flag(hba, UPIU_QUERY_OPCODE_READ_FLAG,
				QUERY_FLAG_IDN_HPB_RESET, 0, &flag_res);

		if (err) {
			dev_err(hba->dev,
				"%s reading fHpbReset flag failed with error %d\n",
				__func__, err);
			return flag_res;
		}

		if (!flag_res)
			goto out;

		usleep_range(1000, 1100);
	}
	if (flag_res) {
		dev_err(hba->dev,
			"%s fHpbReset was not cleared by the device\n",
			__func__);
	}
out:
	return flag_res;
}

/**
 * ufshpb_toggle_state - switch HPB state of all LUs
 * @hba: per-adapter instance
 * @src: expected current HPB state
 * @dest: target HPB state to switch to
 */
void ufshpb_toggle_state(struct ufs_hba *hba, enum UFSHPB_STATE src, enum UFSHPB_STATE dest)
{
	struct ufshpb_lu *hpb;
	struct scsi_device *sdev;

	shost_for_each_device(sdev, hba->host) {
		hpb = ufshpb_get_hpb_data(sdev);

		if (!hpb || ufshpb_get_state(hpb) != src)
			continue;
		ufshpb_set_state(hpb, dest);

		if (dest == HPB_RESET) {
			ufshpb_cancel_jobs(hpb);
			ufshpb_discard_rsp_lists(hpb);
		}
	}
}

void ufshpb_suspend(struct ufs_hba *hba)
{
	struct ufshpb_lu *hpb;
	struct scsi_device *sdev;

	shost_for_each_device(sdev, hba->host) {
		hpb = ufshpb_get_hpb_data(sdev);
		if (!hpb || ufshpb_get_state(hpb) != HPB_PRESENT)
			continue;

		ufshpb_set_state(hpb, HPB_SUSPEND);
		ufshpb_cancel_jobs(hpb);
	}
}

void ufshpb_resume(struct ufs_hba *hba)
{
	struct ufshpb_lu *hpb;
	struct scsi_device *sdev;

	shost_for_each_device(sdev, hba->host) {
		hpb = ufshpb_get_hpb_data(sdev);
		if (!hpb || ufshpb_get_state(hpb) != HPB_SUSPEND)
			continue;

		ufshpb_set_state(hpb, HPB_PRESENT);
		ufshpb_kick_map_work(hpb);
		if (hpb->is_hcm) {
			unsigned int poll = hpb->params.timeout_polling_interval_ms;

			schedule_delayed_work(&hpb->ufshpb_read_to_work, msecs_to_jiffies(poll));
		}
	}
}

static int ufshpb_get_lu_info(struct ufs_hba *hba, int lun,
			      struct ufshpb_lu_info *hpb_lu_info)
{
	u16 max_active_rgns;
	u8 lu_enable;
	int size;
	int ret;
	char desc_buf[QUERY_DESC_MAX_SIZE];

	ufshcd_map_desc_id_to_length(hba, QUERY_DESC_IDN_UNIT, &size);

	ufshcd_rpm_get_sync(hba);
	ret = ufshcd_query_descriptor_retry(hba, UPIU_QUERY_OPCODE_READ_DESC,
					    QUERY_DESC_IDN_UNIT, lun, 0,
					    desc_buf, &size);
	ufshcd_rpm_put_sync(hba);

	if (ret) {
		dev_err(hba->dev,
			"%s: idn: %d lun: %d  query request failed",
			__func__, QUERY_DESC_IDN_UNIT, lun);
		return ret;
	}

	lu_enable = desc_buf[UNIT_DESC_PARAM_LU_ENABLE];
	if (lu_enable != LU_ENABLED_HPB_FUNC)
		return -ENODEV;

	max_active_rgns = get_unaligned_be16(
			desc_buf + UNIT_DESC_PARAM_HPB_LU_MAX_ACTIVE_RGNS);
	if (!max_active_rgns) {
		dev_err(hba->dev,
			"lun %d wrong number of max active regions\n", lun);
		return -ENODEV;
	}

	hpb_lu_info->num_blocks = get_unaligned_be64(
			desc_buf + UNIT_DESC_PARAM_LOGICAL_BLK_COUNT);
	hpb_lu_info->pinned_start = get_unaligned_be16(
			desc_buf + UNIT_DESC_PARAM_HPB_PIN_RGN_START_OFF);
	hpb_lu_info->num_pinned = get_unaligned_be16(
			desc_buf + UNIT_DESC_PARAM_HPB_NUM_PIN_RGNS);
	hpb_lu_info->max_active_rgns = max_active_rgns;

	return 0;
}

void ufshpb_destroy_lu(struct ufs_hba *hba, struct scsi_device *sdev)
{
	struct ufshpb_lu *hpb = ufshpb_get_hpb_data(sdev);

	if (!hpb)
		return;

	ufshpb_set_state(hpb, HPB_FAILED);

	sdev = hpb->sdev_ufs_lu;
	sdev->hostdata = NULL;

	ufshpb_cancel_jobs(hpb);

	ufshpb_pre_req_mempool_destroy(hpb);
	ufshpb_destroy_region_tbl(hpb);

	kmem_cache_destroy(hpb->map_req_cache);
	kmem_cache_destroy(hpb->m_page_cache);

	list_del_init(&hpb->list_hpb_lu);

	kfree(hpb);
}

static void ufshpb_hpb_lu_prepared(struct ufs_hba *hba)
{
	int pool_size;
	struct ufshpb_lu *hpb;
	struct scsi_device *sdev;
	bool init_success;

	if (tot_active_srgn_pages == 0) {
		ufshpb_remove(hba);
		return;
	}

	init_success = !ufshpb_check_hpb_reset_query(hba);

	pool_size = PAGE_ALIGN(ufshpb_host_map_kbytes * 1024) / PAGE_SIZE;
	if (pool_size > tot_active_srgn_pages) {
		mempool_resize(ufshpb_mctx_pool, tot_active_srgn_pages);
		mempool_resize(ufshpb_page_pool, tot_active_srgn_pages);
	}

	shost_for_each_device(sdev, hba->host) {
		hpb = ufshpb_get_hpb_data(sdev);
		if (!hpb)
			continue;

		if (init_success) {
			ufshpb_set_state(hpb, HPB_PRESENT);
			if ((hpb->lu_pinned_end - hpb->lu_pinned_start) > 0)
				queue_work(ufshpb_wq, &hpb->map_work);
		} else {
			dev_err(hba->dev, "destroy HPB lu %d\n", hpb->lun);
			ufshpb_destroy_lu(hba, sdev);
		}
	}

	if (!init_success)
		ufshpb_remove(hba);
}

void ufshpb_init_hpb_lu(struct ufs_hba *hba, struct scsi_device *sdev)
{
	struct ufshpb_lu *hpb;
	int ret;
	struct ufshpb_lu_info hpb_lu_info = { 0 };
	int lun = sdev->lun;

	if (lun >= hba->dev_info.max_lu_supported)
		goto out;

	ret = ufshpb_get_lu_info(hba, lun, &hpb_lu_info);
	if (ret)
		goto out;

	hpb = ufshpb_alloc_hpb_lu(hba, sdev, &hba->ufshpb_dev,
				  &hpb_lu_info);
	if (!hpb)
		goto out;

	tot_active_srgn_pages += hpb_lu_info.max_active_rgns *
			hpb->srgns_per_rgn * hpb->pages_per_srgn;

out:
	/* All LUs are initialized */
	if (atomic_dec_and_test(&hba->ufshpb_dev.slave_conf_cnt))
		ufshpb_hpb_lu_prepared(hba);
}

static int ufshpb_init_mem_wq(struct ufs_hba *hba)
{
	int ret;
	unsigned int pool_size;

	ufshpb_mctx_cache = kmem_cache_create("ufshpb_mctx_cache",
					sizeof(struct ufshpb_map_ctx),
					0, 0, NULL);
	if (!ufshpb_mctx_cache) {
		dev_err(hba->dev, "ufshpb: cannot init mctx cache\n");
		return -ENOMEM;
	}

	pool_size = PAGE_ALIGN(ufshpb_host_map_kbytes * 1024) / PAGE_SIZE;
	dev_info(hba->dev, "%s:%d ufshpb_host_map_kbytes %u pool_size %u\n",
	       __func__, __LINE__, ufshpb_host_map_kbytes, pool_size);

	ufshpb_mctx_pool = mempool_create_slab_pool(pool_size,
						    ufshpb_mctx_cache);
	if (!ufshpb_mctx_pool) {
		dev_err(hba->dev, "ufshpb: cannot init mctx pool\n");
		ret = -ENOMEM;
		goto release_mctx_cache;
	}

	ufshpb_page_pool = mempool_create_page_pool(pool_size, 0);
	if (!ufshpb_page_pool) {
		dev_err(hba->dev, "ufshpb: cannot init page pool\n");
		ret = -ENOMEM;
		goto release_mctx_pool;
	}

	ufshpb_wq = alloc_workqueue("ufshpb-wq",
					WQ_UNBOUND | WQ_MEM_RECLAIM, 0);
	if (!ufshpb_wq) {
		dev_err(hba->dev, "ufshpb: alloc workqueue failed\n");
		ret = -ENOMEM;
		goto release_page_pool;
	}

	return 0;

release_page_pool:
	mempool_destroy(ufshpb_page_pool);
release_mctx_pool:
	mempool_destroy(ufshpb_mctx_pool);
release_mctx_cache:
	kmem_cache_destroy(ufshpb_mctx_cache);
	return ret;
}

void ufshpb_get_geo_info(struct ufs_hba *hba, u8 *geo_buf)
{
	struct ufshpb_dev_info *hpb_info = &hba->ufshpb_dev;
	int max_active_rgns = 0;
	int hpb_num_lu;

	hpb_num_lu = geo_buf[GEOMETRY_DESC_PARAM_HPB_NUMBER_LU];
	if (hpb_num_lu == 0) {
		dev_err(hba->dev, "No HPB LU supported\n");
		hpb_info->hpb_disabled = true;
		return;
	}

	hpb_info->rgn_size = geo_buf[GEOMETRY_DESC_PARAM_HPB_REGION_SIZE];
	hpb_info->srgn_size = geo_buf[GEOMETRY_DESC_PARAM_HPB_SUBREGION_SIZE];
	max_active_rgns = get_unaligned_be16(geo_buf +
			  GEOMETRY_DESC_PARAM_HPB_MAX_ACTIVE_REGS);

	if (hpb_info->rgn_size == 0 || hpb_info->srgn_size == 0 ||
	    max_active_rgns == 0) {
		dev_err(hba->dev, "No HPB supported device\n");
		hpb_info->hpb_disabled = true;
		return;
	}
}

void ufshpb_get_dev_info(struct ufs_hba *hba, u8 *desc_buf)
{
	struct ufshpb_dev_info *hpb_dev_info = &hba->ufshpb_dev;
	int version, ret;
	int max_single_cmd;

	hpb_dev_info->control_mode = desc_buf[DEVICE_DESC_PARAM_HPB_CONTROL];

	version = get_unaligned_be16(desc_buf + DEVICE_DESC_PARAM_HPB_VER);
	if ((version != HPB_SUPPORT_VERSION) &&
	    (version != HPB_SUPPORT_LEGACY_VERSION)) {
		dev_err(hba->dev, "%s: HPB %x version is not supported.\n",
			__func__, version);
		hpb_dev_info->hpb_disabled = true;
		return;
	}

	if (version == HPB_SUPPORT_LEGACY_VERSION)
		hpb_dev_info->is_legacy = true;

	/*
	 * Get the number of user logical unit to check whether all
	 * scsi_device finish initialization
	 */
	hpb_dev_info->num_lu = desc_buf[DEVICE_DESC_PARAM_NUM_LU];

	if (hpb_dev_info->is_legacy)
		return;

	ret = ufshcd_query_attr_retry(hba, UPIU_QUERY_OPCODE_READ_ATTR,
		QUERY_ATTR_IDN_MAX_HPB_SINGLE_CMD, 0, 0, &max_single_cmd);

	if (ret)
		hpb_dev_info->max_hpb_single_cmd = HPB_LEGACY_CHUNK_HIGH;
	else
		hpb_dev_info->max_hpb_single_cmd = min(max_single_cmd + 1, HPB_MULTI_CHUNK_HIGH);
}

void ufshpb_init(struct ufs_hba *hba)
{
	struct ufshpb_dev_info *hpb_dev_info = &hba->ufshpb_dev;
	int try;
	int ret;

	if (!ufshpb_is_allowed(hba) || !hba->dev_info.hpb_enabled)
		return;

	if (ufshpb_init_mem_wq(hba)) {
		hpb_dev_info->hpb_disabled = true;
		return;
	}

	atomic_set(&hpb_dev_info->slave_conf_cnt, hpb_dev_info->num_lu);
	tot_active_srgn_pages = 0;
	/* issue HPB reset query */
	for (try = 0; try < HPB_RESET_REQ_RETRIES; try++) {
		ret = ufshcd_query_flag(hba, UPIU_QUERY_OPCODE_SET_FLAG,
					QUERY_FLAG_IDN_HPB_RESET, 0, NULL);
		if (!ret)
			break;
	}
}

void ufshpb_remove(struct ufs_hba *hba)
{
	mempool_destroy(ufshpb_page_pool);
	mempool_destroy(ufshpb_mctx_pool);
	kmem_cache_destroy(ufshpb_mctx_cache);

	destroy_workqueue(ufshpb_wq);
}

module_param(ufshpb_host_map_kbytes, uint, 0644);
MODULE_PARM_DESC(ufshpb_host_map_kbytes,
	"ufshpb host mapping memory kilo-bytes for ufshpb memory-pool");

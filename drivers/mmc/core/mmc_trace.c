/*
 *  linux/drivers/mmc/core/mmc_trace.c
 *
 *  Copyright (C) 2013 Samsung Electronics Co., Ltd. All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/tracepoint.h>
#include <linux/types.h>
#include <linux/blkdev.h>
#include <linux/blktrace_api.h>
#include <linux/module.h>
#include <linux/mmc/mmc_trace.h>
#include <linux/mmc/core.h>
#include <linux/mmc/host.h>
#include "../card/queue.h"

#ifdef CONFIG_BLK_DEV_IO_TRACE

/* trace part */
static const struct {
	const char *act[2];
	/* This function is to action something for preparation */
	int	   (*prefunc)(void);
} ta_info[] = {
	[__MMC_TA_ASYNC_REQ_FETCH] = {{ "eN", "fetch_async_reqest" }, NULL },
	[__MMC_TA_REQ_FETCH] = {{ "eF", "fetch_request" }, NULL },
	[__MMC_TA_REQ_DONE] = {{ "eC", "request_done" }, NULL },
	[__MMC_TA_PRE_DONE] = {{ "eP", "prepare_done" }, NULL },
	[__MMC_TA_MMC_ISSUE] = {{ "eI", "cmd_issue" }, NULL },
	[__MMC_TA_MMC_DONE] = {{ "eD", "cmd_done" }, NULL },
	[__MMC_TA_MMC_DMA_DONE] = {{ "eA", "dma_done" }, NULL },
};

static inline struct request *__get_request(struct mmc_queue_req *mqrq)
{
	return mqrq ? mqrq->req : NULL;
}

static inline struct request_queue *__get_request_queue(struct request *req)
{
	return req ? req->q : NULL;
}

void mmc_add_trace(unsigned int type, void *m)
{
	struct request *req;
	struct request_queue *q;
	struct blk_trace *bt;
	struct mmc_trace mt;
	struct request_list *rl;
	struct mmc_queue_req *mqrq = (struct mmc_queue_req *)m;

	req = __get_request(mqrq);
	if (unlikely(!req))
		return;

	q = __get_request_queue(req);
	if (unlikely(!q))
		return;

	bt = q->blk_trace;

	if (likely(!bt) || unlikely(bt->trace_state != Blktrace_running))
		return;

	/* trace type */
	mt.ta_type = type;

	memcpy(mt.ta_info, ta_info[type].act[0], 3);

	/* request information */
	rl = &q->rq;

	/* async, sync info update for trace */
	mt.cnt_sync = rl->count[BLK_RW_SYNC];
	mt.cnt_async = rl->count[BLK_RW_ASYNC];

	/* add trace point */
	blk_add_driver_data(q, req, &mt, sizeof(struct mmc_trace));
}
EXPORT_SYMBOL(mmc_add_trace);

/* profiling part */
#define MMC_PROFILE_COUNT_MAX			65536
#define MMC_PROFILE_MAX_SLOT			10

static bool init_lock = true;
static spinlock_t mmc_profile_lock;

/* For offerring this profiling information to debugging tool */
struct mmc_profile_slot_info mmc_profile_info[5];
EXPORT_SYMBOL(mmc_profile_info);

int mmc_profile_alloc(u32 slot, u32 count)
{
	struct mmc_profile_data *profile;
	int err = 0;

	if (slot > MMC_PROFILE_MAX_SLOT) {
		pr_err("%s : %d slot is not support\n", __func__, slot);
		err = -1;
		goto out;
	}

	if (count > MMC_PROFILE_COUNT_MAX) {
		pr_err("%s: profile count is too much (%d)\n",
					__func__, count);
		err = -1;
		goto out;
	}

	if (mmc_profile_info[slot].using == true) {
		pr_err("%s : %d slot is already used\n", __func__, slot);
		err = -1;
		goto out;
	}

	profile = kmalloc(sizeof(struct mmc_profile_data) * count, GFP_KERNEL);

	if (!profile) {
		pr_err("%s : memory allocation is failed\n", __func__);
		err = -1;
		goto out;
	}

	if (init_lock) {
		spin_lock_init(&mmc_profile_lock);
		init_lock = false;
	}

	mmc_profile_info[slot].using = true;
	mmc_profile_info[slot].count_max = count;
	mmc_profile_info[slot].count_cur = 0;
	mmc_profile_info[slot].data = profile;
out:
	return err;
}
EXPORT_SYMBOL(mmc_profile_alloc);

int mmc_profile_free(u32 slot)
{
	int err = 0;

	if (slot > MMC_PROFILE_MAX_SLOT) {
		pr_err("%s : %d slot is not support\n", __func__, slot);
		err = -1;
		goto out;
	}

	if (mmc_profile_info[slot].using == false) {
		pr_err("%s : %d slot is not used\n", __func__, slot);
		err = -1;
		goto out;
	}

	kfree(mmc_profile_info[slot].data);
	mmc_profile_info[slot].data = NULL;
	mmc_profile_info[slot].using = false;
	mmc_profile_info[slot].count_cur = 0;
	mmc_profile_info[slot].count_max = 0;
out:
	return err;
}
EXPORT_SYMBOL(mmc_profile_free);

int mmc_profile_start(u32 slot)
{
	struct mmc_profile_slot_info *slot_info;
	struct mmc_profile_data *data;
	unsigned long flags;
	int err = 0;

	if (slot > MMC_PROFILE_MAX_SLOT) {
		pr_err("%s : %d slot is not support\n", __func__, slot);
		err = -1;
		goto out;
	}

	slot_info = &mmc_profile_info[slot];
	data = mmc_profile_info[slot].data;
	if (slot_info->using == false ||
		slot_info->count_max <= slot_info->count_cur || !data) {
		pr_err("%s : used-%d, count_max-%d, count_cur-%d\n",
				__func__, slot_info->using,
				slot_info->count_max, slot_info->count_cur);
		err = -1;
		goto out;
	}

	spin_lock_irqsave(&mmc_profile_lock, flags);
	do_gettimeofday(&slot_info->start);
	spin_unlock_irqrestore(&mmc_profile_lock, flags);
out:
	return err;
}
EXPORT_SYMBOL(mmc_profile_start);

int mmc_profile_end(u32 slot, u32 record)
{
	struct mmc_profile_slot_info *slot_info;
	struct mmc_profile_data *data;
	unsigned long flags;
	int err = 0;

	if (slot > MMC_PROFILE_MAX_SLOT) {
		pr_err("%s : %d slot is not support\n", __func__, slot);
		err = -1;
		goto out;
	}

	slot_info = &mmc_profile_info[slot];
	data = mmc_profile_info[slot].data;
	if (slot_info->using == false || !data) {
		pr_err("%s : used-%d, count_max-%d, count_cur-%d\n",
				__func__, slot_info->using,
				slot_info->count_max, slot_info->count_cur);
		err = -1;
		goto out;
	}

	spin_lock_irqsave(&mmc_profile_lock, flags);
	do_gettimeofday(&slot_info->end);
	data[slot_info->count_cur].elap_time =
	slot_info->end.tv_sec == slot_info->start.tv_sec ?
		slot_info->end.tv_usec - slot_info->start.tv_usec :
		USEC_PER_SEC - slot_info->start.tv_usec
			+ slot_info->end.tv_usec;
	data[slot_info->count_cur].record = record;
	slot_info->count_cur++;
	spin_unlock_irqrestore(&mmc_profile_lock, flags);
out:
	return err;
}
EXPORT_SYMBOL(mmc_profile_end);

u32 mmc_profile_result_time(u32 slot, u32 start, u32 end)
{
	struct mmc_profile_slot_info *slot_info;
	struct mmc_profile_data *data;
	u32 total = 0;
	u32 result, i;

	if (slot > MMC_PROFILE_MAX_SLOT) {
		pr_err("%s : %d slot is not support\n", __func__, slot);
		result = 0;
		goto out;
	}

	slot_info = &mmc_profile_info[slot];
	data = mmc_profile_info[slot].data;
	if (slot_info->using == false || !data) {
		pr_err("%s : used-%d, count_max-%d, count_cur-%d\n",
				__func__, slot_info->using,
				slot_info->count_max, slot_info->count_cur);
		result = 0;
		goto out;
	}

	if (end <= start || slot_info->count_cur < end) {
		pr_err("%s : strange range start-%d, end-%d\n",
				__func__, start, end);
		result = 0;
		goto out;
	}

	for (i = start; i < end; i++)
		total += data[i].elap_time;

	result = total / (end - start);
out:
	return result;
}
EXPORT_SYMBOL(mmc_profile_result_time);

u32 mmc_profile_get_count(u32 slot)
{
	struct mmc_profile_slot_info *slot_info;
	u32 result;

	if (slot > MMC_PROFILE_MAX_SLOT) {
		pr_err("%s : %d slot is not support\n", __func__, slot);
		result = 0;
		goto out;
	}

	slot_info = &mmc_profile_info[slot];
	if (slot_info->using == false) {
		pr_err("%s : used-%d, count_max-%d, count_cur-%d\n",
				__func__, slot_info->using,
				slot_info->count_max, slot_info->count_cur);
		result = 0;
		goto out;
	}
	result = slot_info->count_cur;
out:
	return result;
}
EXPORT_SYMBOL(mmc_profile_get_count);

#endif /* CONFIG_BLK_DEV_IO_TRACE */


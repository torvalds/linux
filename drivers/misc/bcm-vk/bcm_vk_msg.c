// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2018-2020 Broadcom.
 */

#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/hash.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/poll.h>
#include <linux/sizes.h>
#include <linux/spinlock.h>
#include <linux/timer.h>

#include "bcm_vk.h"
#include "bcm_vk_msg.h"
#include "bcm_vk_sg.h"

/* functions to manipulate the transport id in msg block */
#define BCM_VK_MSG_Q_SHIFT	 4
#define BCM_VK_MSG_Q_MASK	 0xF
#define BCM_VK_MSG_ID_MASK	 0xFFF

#define BCM_VK_DMA_DRAIN_MAX_MS	  2000

/* number x q_size will be the max number of msg processed per loop */
#define BCM_VK_MSG_PROC_MAX_LOOP 2

/* module parameter */
static bool hb_mon = true;
module_param(hb_mon, bool, 0444);
MODULE_PARM_DESC(hb_mon, "Monitoring heartbeat continuously.\n");
static int batch_log = 1;
module_param(batch_log, int, 0444);
MODULE_PARM_DESC(batch_log, "Max num of logs per batch operation.\n");

static bool hb_mon_is_on(void)
{
	return hb_mon;
}

static u32 get_q_num(const struct vk_msg_blk *msg)
{
	u32 q_num = msg->trans_id & BCM_VK_MSG_Q_MASK;

	if (q_num >= VK_MSGQ_PER_CHAN_MAX)
		q_num = VK_MSGQ_NUM_DEFAULT;
	return q_num;
}

static void set_q_num(struct vk_msg_blk *msg, u32 q_num)
{
	u32 trans_q;

	if (q_num >= VK_MSGQ_PER_CHAN_MAX)
		trans_q = VK_MSGQ_NUM_DEFAULT;
	else
		trans_q = q_num;

	msg->trans_id = (msg->trans_id & ~BCM_VK_MSG_Q_MASK) | trans_q;
}

static u32 get_msg_id(const struct vk_msg_blk *msg)
{
	return ((msg->trans_id >> BCM_VK_MSG_Q_SHIFT) & BCM_VK_MSG_ID_MASK);
}

static void set_msg_id(struct vk_msg_blk *msg, u32 val)
{
	msg->trans_id = (val << BCM_VK_MSG_Q_SHIFT) | get_q_num(msg);
}

static u32 msgq_inc(const struct bcm_vk_sync_qinfo *qinfo, u32 idx, u32 inc)
{
	return ((idx + inc) & qinfo->q_mask);
}

static
struct vk_msg_blk __iomem *msgq_blk_addr(const struct bcm_vk_sync_qinfo *qinfo,
					 u32 idx)
{
	return qinfo->q_start + (VK_MSGQ_BLK_SIZE * idx);
}

static u32 msgq_occupied(const struct bcm_vk_msgq __iomem *msgq,
			 const struct bcm_vk_sync_qinfo *qinfo)
{
	u32 wr_idx, rd_idx;

	wr_idx = readl_relaxed(&msgq->wr_idx);
	rd_idx = readl_relaxed(&msgq->rd_idx);

	return ((wr_idx - rd_idx) & qinfo->q_mask);
}

static
u32 msgq_avail_space(const struct bcm_vk_msgq __iomem *msgq,
		     const struct bcm_vk_sync_qinfo *qinfo)
{
	return (qinfo->q_size - msgq_occupied(msgq, qinfo) - 1);
}

/* number of retries when enqueue message fails before returning EAGAIN */
#define BCM_VK_H2VK_ENQ_RETRY 10
#define BCM_VK_H2VK_ENQ_RETRY_DELAY_MS 50

bool bcm_vk_drv_access_ok(struct bcm_vk *vk)
{
	return (!!atomic_read(&vk->msgq_inited));
}

void bcm_vk_set_host_alert(struct bcm_vk *vk, u32 bit_mask)
{
	struct bcm_vk_alert *alert = &vk->host_alert;
	unsigned long flags;

	/* use irqsave version as this maybe called inside timer interrupt */
	spin_lock_irqsave(&vk->host_alert_lock, flags);
	alert->notfs |= bit_mask;
	spin_unlock_irqrestore(&vk->host_alert_lock, flags);

	if (test_and_set_bit(BCM_VK_WQ_NOTF_PEND, vk->wq_offload) == 0)
		queue_work(vk->wq_thread, &vk->wq_work);
}

/*
 * Heartbeat related defines
 * The heartbeat from host is a last resort.  If stuck condition happens
 * on the card, firmware is supposed to detect it.  Therefore, the heartbeat
 * values used will be more relaxed on the driver, which need to be bigger
 * than the watchdog timeout on the card.  The watchdog timeout on the card
 * is 20s, with a jitter of 2s => 22s.  We use a value of 27s here.
 */
#define BCM_VK_HB_TIMER_S 3
#define BCM_VK_HB_TIMER_VALUE (BCM_VK_HB_TIMER_S * HZ)
#define BCM_VK_HB_LOST_MAX (27 / BCM_VK_HB_TIMER_S)

static void bcm_vk_hb_poll(struct timer_list *t)
{
	u32 uptime_s;
	struct bcm_vk_hb_ctrl *hb = container_of(t, struct bcm_vk_hb_ctrl,
						 timer);
	struct bcm_vk *vk = container_of(hb, struct bcm_vk, hb_ctrl);

	if (bcm_vk_drv_access_ok(vk) && hb_mon_is_on()) {
		/* read uptime from register and compare */
		uptime_s = vkread32(vk, BAR_0, BAR_OS_UPTIME);

		if (uptime_s == hb->last_uptime)
			hb->lost_cnt++;
		else /* reset to avoid accumulation */
			hb->lost_cnt = 0;

		dev_dbg(&vk->pdev->dev, "Last uptime %d current %d, lost %d\n",
			hb->last_uptime, uptime_s, hb->lost_cnt);

		/*
		 * if the interface goes down without any activity, a value
		 * of 0xFFFFFFFF will be continuously read, and the detection
		 * will be happened eventually.
		 */
		hb->last_uptime = uptime_s;
	} else {
		/* reset heart beat lost cnt */
		hb->lost_cnt = 0;
	}

	/* next, check if heartbeat exceeds limit */
	if (hb->lost_cnt > BCM_VK_HB_LOST_MAX) {
		dev_err(&vk->pdev->dev, "Heartbeat Misses %d times, %d s!\n",
			BCM_VK_HB_LOST_MAX,
			BCM_VK_HB_LOST_MAX * BCM_VK_HB_TIMER_S);

		bcm_vk_blk_drv_access(vk);
		bcm_vk_set_host_alert(vk, ERR_LOG_HOST_HB_FAIL);
	}
	/* re-arm timer */
	mod_timer(&hb->timer, jiffies + BCM_VK_HB_TIMER_VALUE);
}

void bcm_vk_hb_init(struct bcm_vk *vk)
{
	struct bcm_vk_hb_ctrl *hb = &vk->hb_ctrl;

	timer_setup(&hb->timer, bcm_vk_hb_poll, 0);
	mod_timer(&hb->timer, jiffies + BCM_VK_HB_TIMER_VALUE);
}

void bcm_vk_hb_deinit(struct bcm_vk *vk)
{
	struct bcm_vk_hb_ctrl *hb = &vk->hb_ctrl;

	del_timer(&hb->timer);
}

static void bcm_vk_msgid_bitmap_clear(struct bcm_vk *vk,
				      unsigned int start,
				      unsigned int nbits)
{
	spin_lock(&vk->msg_id_lock);
	bitmap_clear(vk->bmap, start, nbits);
	spin_unlock(&vk->msg_id_lock);
}

/*
 * allocate a ctx per file struct
 */
static struct bcm_vk_ctx *bcm_vk_get_ctx(struct bcm_vk *vk, const pid_t pid)
{
	u32 i;
	struct bcm_vk_ctx *ctx = NULL;
	u32 hash_idx = hash_32(pid, VK_PID_HT_SHIFT_BIT);

	spin_lock(&vk->ctx_lock);

	/* check if it is in reset, if so, don't allow */
	if (vk->reset_pid) {
		dev_err(&vk->pdev->dev,
			"No context allowed during reset by pid %d\n",
			vk->reset_pid);

		goto in_reset_exit;
	}

	for (i = 0; i < ARRAY_SIZE(vk->ctx); i++) {
		if (!vk->ctx[i].in_use) {
			vk->ctx[i].in_use = true;
			ctx = &vk->ctx[i];
			break;
		}
	}

	if (!ctx) {
		dev_err(&vk->pdev->dev, "All context in use\n");

		goto all_in_use_exit;
	}

	/* set the pid and insert it to hash table */
	ctx->pid = pid;
	ctx->hash_idx = hash_idx;
	list_add_tail(&ctx->node, &vk->pid_ht[hash_idx].head);

	/* increase kref */
	kref_get(&vk->kref);

	/* clear counter */
	atomic_set(&ctx->pend_cnt, 0);
	atomic_set(&ctx->dma_cnt, 0);
	init_waitqueue_head(&ctx->rd_wq);

all_in_use_exit:
in_reset_exit:
	spin_unlock(&vk->ctx_lock);

	return ctx;
}

static u16 bcm_vk_get_msg_id(struct bcm_vk *vk)
{
	u16 rc = VK_MSG_ID_OVERFLOW;
	u16 test_bit_count = 0;

	spin_lock(&vk->msg_id_lock);
	while (test_bit_count < (VK_MSG_ID_BITMAP_SIZE - 1)) {
		/*
		 * first time come in this loop, msg_id will be 0
		 * and the first one tested will be 1.  We skip
		 * VK_SIMPLEX_MSG_ID (0) for one way host2vk
		 * communication
		 */
		vk->msg_id++;
		if (vk->msg_id == VK_MSG_ID_BITMAP_SIZE)
			vk->msg_id = 1;

		if (test_bit(vk->msg_id, vk->bmap)) {
			test_bit_count++;
			continue;
		}
		rc = vk->msg_id;
		bitmap_set(vk->bmap, vk->msg_id, 1);
		break;
	}
	spin_unlock(&vk->msg_id_lock);

	return rc;
}

static int bcm_vk_free_ctx(struct bcm_vk *vk, struct bcm_vk_ctx *ctx)
{
	u32 idx;
	u32 hash_idx;
	pid_t pid;
	struct bcm_vk_ctx *entry;
	int count = 0;

	if (!ctx) {
		dev_err(&vk->pdev->dev, "NULL context detected\n");
		return -EINVAL;
	}
	idx = ctx->idx;
	pid = ctx->pid;

	spin_lock(&vk->ctx_lock);

	if (!vk->ctx[idx].in_use) {
		dev_err(&vk->pdev->dev, "context[%d] not in use!\n", idx);
	} else {
		vk->ctx[idx].in_use = false;
		vk->ctx[idx].miscdev = NULL;

		/* Remove it from hash list and see if it is the last one. */
		list_del(&ctx->node);
		hash_idx = ctx->hash_idx;
		list_for_each_entry(entry, &vk->pid_ht[hash_idx].head, node) {
			if (entry->pid == pid)
				count++;
		}
	}

	spin_unlock(&vk->ctx_lock);

	return count;
}

static void bcm_vk_free_wkent(struct device *dev, struct bcm_vk_wkent *entry)
{
	int proc_cnt;

	bcm_vk_sg_free(dev, entry->dma, VK_DMA_MAX_ADDRS, &proc_cnt);
	if (proc_cnt)
		atomic_dec(&entry->ctx->dma_cnt);

	kfree(entry->to_h_msg);
	kfree(entry);
}

static void bcm_vk_drain_all_pend(struct device *dev,
				  struct bcm_vk_msg_chan *chan,
				  struct bcm_vk_ctx *ctx)
{
	u32 num;
	struct bcm_vk_wkent *entry, *tmp;
	struct bcm_vk *vk;
	struct list_head del_q;

	if (ctx)
		vk = container_of(ctx->miscdev, struct bcm_vk, miscdev);

	INIT_LIST_HEAD(&del_q);
	spin_lock(&chan->pendq_lock);
	for (num = 0; num < chan->q_nr; num++) {
		list_for_each_entry_safe(entry, tmp, &chan->pendq[num], node) {
			if ((!ctx) || (entry->ctx->idx == ctx->idx)) {
				list_move_tail(&entry->node, &del_q);
			}
		}
	}
	spin_unlock(&chan->pendq_lock);

	/* batch clean up */
	num = 0;
	list_for_each_entry_safe(entry, tmp, &del_q, node) {
		list_del(&entry->node);
		num++;
		if (ctx) {
			struct vk_msg_blk *msg;
			int bit_set;
			bool responded;
			u32 msg_id;

			/* if it is specific ctx, log for any stuck */
			msg = entry->to_v_msg;
			msg_id = get_msg_id(msg);
			bit_set = test_bit(msg_id, vk->bmap);
			responded = entry->to_h_msg ? true : false;
			if (num <= batch_log)
				dev_info(dev,
					 "Drained: fid %u size %u msg 0x%x(seq-%x) ctx 0x%x[fd-%d] args:[0x%x 0x%x] resp %s, bmap %d\n",
					 msg->function_id, msg->size,
					 msg_id, entry->seq_num,
					 msg->context_id, entry->ctx->idx,
					 msg->cmd, msg->arg,
					 responded ? "T" : "F", bit_set);
			if (responded)
				atomic_dec(&ctx->pend_cnt);
			else if (bit_set)
				bcm_vk_msgid_bitmap_clear(vk, msg_id, 1);
		}
		bcm_vk_free_wkent(dev, entry);
	}
	if (num && ctx)
		dev_info(dev, "Total drained items %d [fd-%d]\n",
			 num, ctx->idx);
}

void bcm_vk_drain_msg_on_reset(struct bcm_vk *vk)
{
	bcm_vk_drain_all_pend(&vk->pdev->dev, &vk->to_v_msg_chan, NULL);
	bcm_vk_drain_all_pend(&vk->pdev->dev, &vk->to_h_msg_chan, NULL);
}

/*
 * Function to sync up the messages queue info that is provided by BAR1
 */
int bcm_vk_sync_msgq(struct bcm_vk *vk, bool force_sync)
{
	struct bcm_vk_msgq __iomem *msgq;
	struct device *dev = &vk->pdev->dev;
	u32 msgq_off;
	u32 num_q;
	struct bcm_vk_msg_chan *chan_list[] = {&vk->to_v_msg_chan,
					       &vk->to_h_msg_chan};
	struct bcm_vk_msg_chan *chan;
	int i, j;
	int ret = 0;

	/*
	 * If the driver is loaded at startup where vk OS is not up yet,
	 * the msgq-info may not be available until a later time.  In
	 * this case, we skip and the sync function is supposed to be
	 * called again.
	 */
	if (!bcm_vk_msgq_marker_valid(vk)) {
		dev_info(dev, "BAR1 msgq marker not initialized.\n");
		return -EAGAIN;
	}

	msgq_off = vkread32(vk, BAR_1, VK_BAR1_MSGQ_CTRL_OFF);

	/* each side is always half the total  */
	num_q = vkread32(vk, BAR_1, VK_BAR1_MSGQ_NR) / 2;
	if (!num_q || (num_q > VK_MSGQ_PER_CHAN_MAX)) {
		dev_err(dev,
			"Advertised msgq %d error - max %d allowed\n",
			num_q, VK_MSGQ_PER_CHAN_MAX);
		return -EINVAL;
	}

	vk->to_v_msg_chan.q_nr = num_q;
	vk->to_h_msg_chan.q_nr = num_q;

	/* first msgq location */
	msgq = vk->bar[BAR_1] + msgq_off;

	/*
	 * if this function is called when it is already inited,
	 * something is wrong
	 */
	if (bcm_vk_drv_access_ok(vk) && !force_sync) {
		dev_err(dev, "Msgq info already in sync\n");
		return -EPERM;
	}

	for (i = 0; i < ARRAY_SIZE(chan_list); i++) {
		chan = chan_list[i];
		memset(chan->sync_qinfo, 0, sizeof(chan->sync_qinfo));

		for (j = 0; j < num_q; j++) {
			struct bcm_vk_sync_qinfo *qinfo;
			u32 msgq_start;
			u32 msgq_size;
			u32 msgq_nxt;
			u32 msgq_db_offset, q_db_offset;

			chan->msgq[j] = msgq;
			msgq_start = readl_relaxed(&msgq->start);
			msgq_size = readl_relaxed(&msgq->size);
			msgq_nxt = readl_relaxed(&msgq->nxt);
			msgq_db_offset = readl_relaxed(&msgq->db_offset);
			q_db_offset = (msgq_db_offset & ((1 << DB_SHIFT) - 1));
			if (q_db_offset  == (~msgq_db_offset >> DB_SHIFT))
				msgq_db_offset = q_db_offset;
			else
				/* fall back to default */
				msgq_db_offset = VK_BAR0_Q_DB_BASE(j);

			dev_info(dev,
				 "MsgQ[%d] type %d num %d, @ 0x%x, db_offset 0x%x rd_idx %d wr_idx %d, size %d, nxt 0x%x\n",
				 j,
				 readw_relaxed(&msgq->type),
				 readw_relaxed(&msgq->num),
				 msgq_start,
				 msgq_db_offset,
				 readl_relaxed(&msgq->rd_idx),
				 readl_relaxed(&msgq->wr_idx),
				 msgq_size,
				 msgq_nxt);

			qinfo = &chan->sync_qinfo[j];
			/* formulate and record static info */
			qinfo->q_start = vk->bar[BAR_1] + msgq_start;
			qinfo->q_size = msgq_size;
			/* set low threshold as 50% or 1/2 */
			qinfo->q_low = qinfo->q_size >> 1;
			qinfo->q_mask = qinfo->q_size - 1;
			qinfo->q_db_offset = msgq_db_offset;

			msgq++;
		}
	}
	atomic_set(&vk->msgq_inited, 1);

	return ret;
}

static int bcm_vk_msg_chan_init(struct bcm_vk_msg_chan *chan)
{
	u32 i;

	mutex_init(&chan->msgq_mutex);
	spin_lock_init(&chan->pendq_lock);
	for (i = 0; i < VK_MSGQ_MAX_NR; i++)
		INIT_LIST_HEAD(&chan->pendq[i]);

	return 0;
}

static void bcm_vk_append_pendq(struct bcm_vk_msg_chan *chan, u16 q_num,
				struct bcm_vk_wkent *entry)
{
	struct bcm_vk_ctx *ctx;

	spin_lock(&chan->pendq_lock);
	list_add_tail(&entry->node, &chan->pendq[q_num]);
	if (entry->to_h_msg) {
		ctx = entry->ctx;
		atomic_inc(&ctx->pend_cnt);
		wake_up_interruptible(&ctx->rd_wq);
	}
	spin_unlock(&chan->pendq_lock);
}

static u32 bcm_vk_append_ib_sgl(struct bcm_vk *vk,
				struct bcm_vk_wkent *entry,
				struct _vk_data *data,
				unsigned int num_planes)
{
	unsigned int i;
	unsigned int item_cnt = 0;
	struct device *dev = &vk->pdev->dev;
	struct bcm_vk_msg_chan *chan = &vk->to_v_msg_chan;
	struct vk_msg_blk *msg = &entry->to_v_msg[0];
	struct bcm_vk_msgq __iomem *msgq;
	struct bcm_vk_sync_qinfo *qinfo;
	u32 ib_sgl_size = 0;
	u8 *buf = (u8 *)&entry->to_v_msg[entry->to_v_blks];
	u32 avail;
	u32 q_num;

	/* check if high watermark is hit, and if so, skip */
	q_num = get_q_num(msg);
	msgq = chan->msgq[q_num];
	qinfo = &chan->sync_qinfo[q_num];
	avail = msgq_avail_space(msgq, qinfo);
	if (avail < qinfo->q_low) {
		dev_dbg(dev, "Skip inserting inband SGL, [0x%x/0x%x]\n",
			avail, qinfo->q_size);
		return 0;
	}

	for (i = 0; i < num_planes; i++) {
		if (data[i].address &&
		    (ib_sgl_size + data[i].size) <= vk->ib_sgl_size) {
			item_cnt++;
			memcpy(buf, entry->dma[i].sglist, data[i].size);
			ib_sgl_size += data[i].size;
			buf += data[i].size;
		}
	}

	dev_dbg(dev, "Num %u sgl items appended, size 0x%x, room 0x%x\n",
		item_cnt, ib_sgl_size, vk->ib_sgl_size);

	/* round up size */
	ib_sgl_size = (ib_sgl_size + VK_MSGQ_BLK_SIZE - 1)
		       >> VK_MSGQ_BLK_SZ_SHIFT;

	return ib_sgl_size;
}

void bcm_to_v_q_doorbell(struct bcm_vk *vk, u32 q_num, u32 db_val)
{
	struct bcm_vk_msg_chan *chan = &vk->to_v_msg_chan;
	struct bcm_vk_sync_qinfo *qinfo = &chan->sync_qinfo[q_num];

	vkwrite32(vk, db_val, BAR_0, qinfo->q_db_offset);
}

static int bcm_to_v_msg_enqueue(struct bcm_vk *vk, struct bcm_vk_wkent *entry)
{
	static u32 seq_num;
	struct bcm_vk_msg_chan *chan = &vk->to_v_msg_chan;
	struct device *dev = &vk->pdev->dev;
	struct vk_msg_blk *src = &entry->to_v_msg[0];

	struct vk_msg_blk __iomem *dst;
	struct bcm_vk_msgq __iomem *msgq;
	struct bcm_vk_sync_qinfo *qinfo;
	u32 q_num = get_q_num(src);
	u32 wr_idx; /* local copy */
	u32 i;
	u32 avail;
	u32 retry;

	if (entry->to_v_blks != src->size + 1) {
		dev_err(dev, "number of blks %d not matching %d MsgId[0x%x]: func %d ctx 0x%x\n",
			entry->to_v_blks,
			src->size + 1,
			get_msg_id(src),
			src->function_id,
			src->context_id);
		return -EMSGSIZE;
	}

	msgq = chan->msgq[q_num];
	qinfo = &chan->sync_qinfo[q_num];

	mutex_lock(&chan->msgq_mutex);

	avail = msgq_avail_space(msgq, qinfo);

	/* if not enough space, return EAGAIN and let app handles it */
	retry = 0;
	while ((avail < entry->to_v_blks) &&
	       (retry++ < BCM_VK_H2VK_ENQ_RETRY)) {
		mutex_unlock(&chan->msgq_mutex);

		msleep(BCM_VK_H2VK_ENQ_RETRY_DELAY_MS);
		mutex_lock(&chan->msgq_mutex);
		avail = msgq_avail_space(msgq, qinfo);
	}
	if (retry > BCM_VK_H2VK_ENQ_RETRY) {
		mutex_unlock(&chan->msgq_mutex);
		return -EAGAIN;
	}

	/* at this point, mutex is taken and there is enough space */
	entry->seq_num = seq_num++; /* update debug seq number */
	wr_idx = readl_relaxed(&msgq->wr_idx);

	if (wr_idx >= qinfo->q_size) {
		dev_crit(dev, "Invalid wr_idx 0x%x => max 0x%x!",
			 wr_idx, qinfo->q_size);
		bcm_vk_blk_drv_access(vk);
		bcm_vk_set_host_alert(vk, ERR_LOG_HOST_PCIE_DWN);
		goto idx_err;
	}

	dst = msgq_blk_addr(qinfo, wr_idx);
	for (i = 0; i < entry->to_v_blks; i++) {
		memcpy_toio(dst, src, sizeof(*dst));

		src++;
		wr_idx = msgq_inc(qinfo, wr_idx, 1);
		dst = msgq_blk_addr(qinfo, wr_idx);
	}

	/* flush the write pointer */
	writel(wr_idx, &msgq->wr_idx);

	/* log new info for debugging */
	dev_dbg(dev,
		"MsgQ[%d] [Rd Wr] = [%d %d] blks inserted %d - Q = [u-%d a-%d]/%d\n",
		readl_relaxed(&msgq->num),
		readl_relaxed(&msgq->rd_idx),
		wr_idx,
		entry->to_v_blks,
		msgq_occupied(msgq, qinfo),
		msgq_avail_space(msgq, qinfo),
		readl_relaxed(&msgq->size));
	/*
	 * press door bell based on queue number. 1 is added to the wr_idx
	 * to avoid the value of 0 appearing on the VK side to distinguish
	 * from initial value.
	 */
	bcm_to_v_q_doorbell(vk, q_num, wr_idx + 1);
idx_err:
	mutex_unlock(&chan->msgq_mutex);
	return 0;
}

int bcm_vk_send_shutdown_msg(struct bcm_vk *vk, u32 shut_type,
			     const pid_t pid, const u32 q_num)
{
	int rc = 0;
	struct bcm_vk_wkent *entry;
	struct device *dev = &vk->pdev->dev;

	/*
	 * check if the marker is still good.  Sometimes, the PCIe interface may
	 * have gone done, and if so and we ship down thing based on broken
	 * values, kernel may panic.
	 */
	if (!bcm_vk_msgq_marker_valid(vk)) {
		dev_info(dev, "PCIe comm chan - invalid marker (0x%x)!\n",
			 vkread32(vk, BAR_1, VK_BAR1_MSGQ_DEF_RDY));
		return -EINVAL;
	}

	entry = kzalloc(struct_size(entry, to_v_msg, 1), GFP_KERNEL);
	if (!entry)
		return -ENOMEM;

	/* fill up necessary data */
	entry->to_v_msg[0].function_id = VK_FID_SHUTDOWN;
	set_q_num(&entry->to_v_msg[0], q_num);
	set_msg_id(&entry->to_v_msg[0], VK_SIMPLEX_MSG_ID);
	entry->to_v_blks = 1; /* always 1 block */

	entry->to_v_msg[0].cmd = shut_type;
	entry->to_v_msg[0].arg = pid;

	rc = bcm_to_v_msg_enqueue(vk, entry);
	if (rc)
		dev_err(dev,
			"Sending shutdown message to q %d for pid %d fails.\n",
			get_q_num(&entry->to_v_msg[0]), pid);

	kfree(entry);

	return rc;
}

static int bcm_vk_handle_last_sess(struct bcm_vk *vk, const pid_t pid,
				   const u32 q_num)
{
	int rc = 0;
	struct device *dev = &vk->pdev->dev;

	/*
	 * don't send down or do anything if message queue is not initialized
	 * and if it is the reset session, clear it.
	 */
	if (!bcm_vk_drv_access_ok(vk)) {
		if (vk->reset_pid == pid)
			vk->reset_pid = 0;
		return -EPERM;
	}

	dev_dbg(dev, "No more sessions, shut down pid %d\n", pid);

	/* only need to do it if it is not the reset process */
	if (vk->reset_pid != pid)
		rc = bcm_vk_send_shutdown_msg(vk, VK_SHUTDOWN_PID, pid, q_num);
	else
		/* put reset_pid to 0 if it is exiting last session */
		vk->reset_pid = 0;

	return rc;
}

static struct bcm_vk_wkent *bcm_vk_dequeue_pending(struct bcm_vk *vk,
						   struct bcm_vk_msg_chan *chan,
						   u16 q_num,
						   u16 msg_id)
{
	struct bcm_vk_wkent *entry = NULL, *iter;

	spin_lock(&chan->pendq_lock);
	list_for_each_entry(iter, &chan->pendq[q_num], node) {
		if (get_msg_id(&iter->to_v_msg[0]) == msg_id) {
			list_del(&iter->node);
			entry = iter;
			bcm_vk_msgid_bitmap_clear(vk, msg_id, 1);
			break;
		}
	}
	spin_unlock(&chan->pendq_lock);
	return entry;
}

s32 bcm_to_h_msg_dequeue(struct bcm_vk *vk)
{
	struct device *dev = &vk->pdev->dev;
	struct bcm_vk_msg_chan *chan = &vk->to_h_msg_chan;
	struct vk_msg_blk *data;
	struct vk_msg_blk __iomem *src;
	struct vk_msg_blk *dst;
	struct bcm_vk_msgq __iomem *msgq;
	struct bcm_vk_sync_qinfo *qinfo;
	struct bcm_vk_wkent *entry;
	u32 rd_idx, wr_idx;
	u32 q_num, msg_id, j;
	u32 num_blks;
	s32 total = 0;
	int cnt = 0;
	int msg_processed = 0;
	int max_msg_to_process;
	bool exit_loop;

	/*
	 * drain all the messages from the queues, and find its pending
	 * entry in the to_v queue, based on msg_id & q_num, and move the
	 * entry to the to_h pending queue, waiting for user space
	 * program to extract
	 */
	mutex_lock(&chan->msgq_mutex);

	for (q_num = 0; q_num < chan->q_nr; q_num++) {
		msgq = chan->msgq[q_num];
		qinfo = &chan->sync_qinfo[q_num];
		max_msg_to_process = BCM_VK_MSG_PROC_MAX_LOOP * qinfo->q_size;

		rd_idx = readl_relaxed(&msgq->rd_idx);
		wr_idx = readl_relaxed(&msgq->wr_idx);
		msg_processed = 0;
		exit_loop = false;
		while ((rd_idx != wr_idx) && !exit_loop) {
			u8 src_size;

			/*
			 * Make a local copy and get pointer to src blk
			 * The rd_idx is masked before getting the pointer to
			 * avoid out of bound access in case the interface goes
			 * down.  It will end up pointing to the last block in
			 * the buffer, but subsequent src->size check would be
			 * able to catch this.
			 */
			src = msgq_blk_addr(qinfo, rd_idx & qinfo->q_mask);
			src_size = readb(&src->size);

			if ((rd_idx >= qinfo->q_size) ||
			    (src_size > (qinfo->q_size - 1))) {
				dev_crit(dev,
					 "Invalid rd_idx 0x%x or size 0x%x => max 0x%x!",
					 rd_idx, src_size, qinfo->q_size);
				bcm_vk_blk_drv_access(vk);
				bcm_vk_set_host_alert(vk,
						      ERR_LOG_HOST_PCIE_DWN);
				goto idx_err;
			}

			num_blks = src_size + 1;
			data = kzalloc(num_blks * VK_MSGQ_BLK_SIZE, GFP_KERNEL);
			if (data) {
				/* copy messages and linearize it */
				dst = data;
				for (j = 0; j < num_blks; j++) {
					memcpy_fromio(dst, src, sizeof(*dst));

					dst++;
					rd_idx = msgq_inc(qinfo, rd_idx, 1);
					src = msgq_blk_addr(qinfo, rd_idx);
				}
				total++;
			} else {
				/*
				 * if we could not allocate memory in kernel,
				 * that is fatal.
				 */
				dev_crit(dev, "Kernel mem allocation failure.\n");
				total = -ENOMEM;
				goto idx_err;
			}

			/* flush rd pointer after a message is dequeued */
			writel(rd_idx, &msgq->rd_idx);

			/* log new info for debugging */
			dev_dbg(dev,
				"MsgQ[%d] [Rd Wr] = [%d %d] blks extracted %d - Q = [u-%d a-%d]/%d\n",
				readl_relaxed(&msgq->num),
				rd_idx,
				wr_idx,
				num_blks,
				msgq_occupied(msgq, qinfo),
				msgq_avail_space(msgq, qinfo),
				readl_relaxed(&msgq->size));

			/*
			 * No need to search if it is an autonomous one-way
			 * message from driver, as these messages do not bear
			 * a to_v pending item. Currently, only the shutdown
			 * message falls into this category.
			 */
			if (data->function_id == VK_FID_SHUTDOWN) {
				kfree(data);
				continue;
			}

			msg_id = get_msg_id(data);
			/* lookup original message in to_v direction */
			entry = bcm_vk_dequeue_pending(vk,
						       &vk->to_v_msg_chan,
						       q_num,
						       msg_id);

			/*
			 * if there is message to does not have prior send,
			 * this is the location to add here
			 */
			if (entry) {
				entry->to_h_blks = num_blks;
				entry->to_h_msg = data;
				bcm_vk_append_pendq(&vk->to_h_msg_chan,
						    q_num, entry);

			} else {
				if (cnt++ < batch_log)
					dev_info(dev,
						 "Could not find MsgId[0x%x] for resp func %d bmap %d\n",
						 msg_id, data->function_id,
						 test_bit(msg_id, vk->bmap));
				kfree(data);
			}
			/* Fetch wr_idx to handle more back-to-back events */
			wr_idx = readl(&msgq->wr_idx);

			/*
			 * cap the max so that even we try to handle more back-to-back events,
			 * so that it won't hold CPU too long or in case rd/wr idexes are
			 * corrupted which triggers infinite looping.
			 */
			if (++msg_processed >= max_msg_to_process) {
				dev_warn(dev, "Q[%d] Per loop processing exceeds %d\n",
					 q_num, max_msg_to_process);
				exit_loop = true;
			}
		}
	}
idx_err:
	mutex_unlock(&chan->msgq_mutex);
	dev_dbg(dev, "total %d drained from queues\n", total);

	return total;
}

/*
 * init routine for all required data structures
 */
static int bcm_vk_data_init(struct bcm_vk *vk)
{
	int i;

	spin_lock_init(&vk->ctx_lock);
	for (i = 0; i < ARRAY_SIZE(vk->ctx); i++) {
		vk->ctx[i].in_use = false;
		vk->ctx[i].idx = i;	/* self identity */
		vk->ctx[i].miscdev = NULL;
	}
	spin_lock_init(&vk->msg_id_lock);
	spin_lock_init(&vk->host_alert_lock);
	vk->msg_id = 0;

	/* initialize hash table */
	for (i = 0; i < VK_PID_HT_SZ; i++)
		INIT_LIST_HEAD(&vk->pid_ht[i].head);

	return 0;
}

irqreturn_t bcm_vk_msgq_irqhandler(int irq, void *dev_id)
{
	struct bcm_vk *vk = dev_id;

	if (!bcm_vk_drv_access_ok(vk)) {
		dev_err(&vk->pdev->dev,
			"Interrupt %d received when msgq not inited\n", irq);
		goto skip_schedule_work;
	}

	queue_work(vk->wq_thread, &vk->wq_work);

skip_schedule_work:
	return IRQ_HANDLED;
}

int bcm_vk_open(struct inode *inode, struct file *p_file)
{
	struct bcm_vk_ctx *ctx;
	struct miscdevice *miscdev = (struct miscdevice *)p_file->private_data;
	struct bcm_vk *vk = container_of(miscdev, struct bcm_vk, miscdev);
	struct device *dev = &vk->pdev->dev;
	int rc = 0;

	/* get a context and set it up for file */
	ctx = bcm_vk_get_ctx(vk, task_tgid_nr(current));
	if (!ctx) {
		dev_err(dev, "Error allocating context\n");
		rc = -ENOMEM;
	} else {
		/*
		 * set up context and replace private data with context for
		 * other methods to use.  Reason for the context is because
		 * it is allowed for multiple sessions to open the sysfs, and
		 * for each file open, when upper layer query the response,
		 * only those that are tied to a specific open should be
		 * returned.  The context->idx will be used for such binding
		 */
		ctx->miscdev = miscdev;
		p_file->private_data = ctx;
		dev_dbg(dev, "ctx_returned with idx %d, pid %d\n",
			ctx->idx, ctx->pid);
	}
	return rc;
}

ssize_t bcm_vk_read(struct file *p_file,
		    char __user *buf,
		    size_t count,
		    loff_t *f_pos)
{
	ssize_t rc = -ENOMSG;
	struct bcm_vk_ctx *ctx = p_file->private_data;
	struct bcm_vk *vk = container_of(ctx->miscdev, struct bcm_vk,
					 miscdev);
	struct device *dev = &vk->pdev->dev;
	struct bcm_vk_msg_chan *chan = &vk->to_h_msg_chan;
	struct bcm_vk_wkent *entry = NULL, *iter;
	u32 q_num;
	u32 rsp_length;

	if (!bcm_vk_drv_access_ok(vk))
		return -EPERM;

	dev_dbg(dev, "Buf count %zu\n", count);

	/*
	 * search through the pendq on the to_h chan, and return only those
	 * that belongs to the same context.  Search is always from the high to
	 * the low priority queues
	 */
	spin_lock(&chan->pendq_lock);
	for (q_num = 0; q_num < chan->q_nr; q_num++) {
		list_for_each_entry(iter, &chan->pendq[q_num], node) {
			if (iter->ctx->idx == ctx->idx) {
				if (count >=
				    (iter->to_h_blks * VK_MSGQ_BLK_SIZE)) {
					list_del(&iter->node);
					atomic_dec(&ctx->pend_cnt);
					entry = iter;
				} else {
					/* buffer not big enough */
					rc = -EMSGSIZE;
				}
				goto read_loop_exit;
			}
		}
	}
read_loop_exit:
	spin_unlock(&chan->pendq_lock);

	if (entry) {
		/* retrieve the passed down msg_id */
		set_msg_id(&entry->to_h_msg[0], entry->usr_msg_id);
		rsp_length = entry->to_h_blks * VK_MSGQ_BLK_SIZE;
		if (copy_to_user(buf, entry->to_h_msg, rsp_length) == 0)
			rc = rsp_length;

		bcm_vk_free_wkent(dev, entry);
	} else if (rc == -EMSGSIZE) {
		struct vk_msg_blk tmp_msg = entry->to_h_msg[0];

		/*
		 * in this case, return just the first block, so
		 * that app knows what size it is looking for.
		 */
		set_msg_id(&tmp_msg, entry->usr_msg_id);
		tmp_msg.size = entry->to_h_blks - 1;
		if (copy_to_user(buf, &tmp_msg, VK_MSGQ_BLK_SIZE) != 0) {
			dev_err(dev, "Error return 1st block in -EMSGSIZE\n");
			rc = -EFAULT;
		}
	}
	return rc;
}

ssize_t bcm_vk_write(struct file *p_file,
		     const char __user *buf,
		     size_t count,
		     loff_t *f_pos)
{
	ssize_t rc;
	struct bcm_vk_ctx *ctx = p_file->private_data;
	struct bcm_vk *vk = container_of(ctx->miscdev, struct bcm_vk,
					 miscdev);
	struct bcm_vk_msgq __iomem *msgq;
	struct device *dev = &vk->pdev->dev;
	struct bcm_vk_wkent *entry;
	u32 sgl_extra_blks;
	u32 q_num;
	u32 msg_size;
	u32 msgq_size;

	if (!bcm_vk_drv_access_ok(vk))
		return -EPERM;

	dev_dbg(dev, "Msg count %zu\n", count);

	/* first, do sanity check where count should be multiple of basic blk */
	if (count & (VK_MSGQ_BLK_SIZE - 1)) {
		dev_err(dev, "Failure with size %zu not multiple of %zu\n",
			count, VK_MSGQ_BLK_SIZE);
		rc = -EINVAL;
		goto write_err;
	}

	/* allocate the work entry + buffer for size count and inband sgl */
	entry = kzalloc(sizeof(*entry) + count + vk->ib_sgl_size,
			GFP_KERNEL);
	if (!entry) {
		rc = -ENOMEM;
		goto write_err;
	}

	/* now copy msg from user space, and then formulate the work entry */
	if (copy_from_user(&entry->to_v_msg[0], buf, count)) {
		rc = -EFAULT;
		goto write_free_ent;
	}

	entry->to_v_blks = count >> VK_MSGQ_BLK_SZ_SHIFT;
	entry->ctx = ctx;

	/* do a check on the blk size which could not exceed queue space */
	q_num = get_q_num(&entry->to_v_msg[0]);
	msgq = vk->to_v_msg_chan.msgq[q_num];
	msgq_size = readl_relaxed(&msgq->size);
	if (entry->to_v_blks + (vk->ib_sgl_size >> VK_MSGQ_BLK_SZ_SHIFT)
	    > (msgq_size - 1)) {
		dev_err(dev, "Blk size %d exceed max queue size allowed %d\n",
			entry->to_v_blks, msgq_size - 1);
		rc = -EINVAL;
		goto write_free_ent;
	}

	/* Use internal message id */
	entry->usr_msg_id = get_msg_id(&entry->to_v_msg[0]);
	rc = bcm_vk_get_msg_id(vk);
	if (rc == VK_MSG_ID_OVERFLOW) {
		dev_err(dev, "msg_id overflow\n");
		rc = -EOVERFLOW;
		goto write_free_ent;
	}
	set_msg_id(&entry->to_v_msg[0], rc);
	ctx->q_num = q_num;

	dev_dbg(dev,
		"[Q-%d]Message ctx id %d, usr_msg_id 0x%x sent msg_id 0x%x\n",
		ctx->q_num, ctx->idx, entry->usr_msg_id,
		get_msg_id(&entry->to_v_msg[0]));

	if (entry->to_v_msg[0].function_id == VK_FID_TRANS_BUF) {
		/* Convert any pointers to sg list */
		unsigned int num_planes;
		int dir;
		struct _vk_data *data;

		/*
		 * check if we are in reset, if so, no buffer transfer is
		 * allowed and return error.
		 */
		if (vk->reset_pid) {
			dev_dbg(dev, "No Transfer allowed during reset, pid %d.\n",
				ctx->pid);
			rc = -EACCES;
			goto write_free_msgid;
		}

		num_planes = entry->to_v_msg[0].cmd & VK_CMD_PLANES_MASK;
		if ((entry->to_v_msg[0].cmd & VK_CMD_MASK) == VK_CMD_DOWNLOAD)
			dir = DMA_FROM_DEVICE;
		else
			dir = DMA_TO_DEVICE;

		/* Calculate vk_data location */
		/* Go to end of the message */
		msg_size = entry->to_v_msg[0].size;
		if (msg_size > entry->to_v_blks) {
			rc = -EMSGSIZE;
			goto write_free_msgid;
		}

		data = (struct _vk_data *)&entry->to_v_msg[msg_size + 1];

		/* Now back up to the start of the pointers */
		data -= num_planes;

		/* Convert user addresses to DMA SG List */
		rc = bcm_vk_sg_alloc(dev, entry->dma, dir, data, num_planes);
		if (rc)
			goto write_free_msgid;

		atomic_inc(&ctx->dma_cnt);
		/* try to embed inband sgl */
		sgl_extra_blks = bcm_vk_append_ib_sgl(vk, entry, data,
						      num_planes);
		entry->to_v_blks += sgl_extra_blks;
		entry->to_v_msg[0].size += sgl_extra_blks;
	} else if (entry->to_v_msg[0].function_id == VK_FID_INIT &&
		   entry->to_v_msg[0].context_id == VK_NEW_CTX) {
		/*
		 * Init happens in 2 stages, only the first stage contains the
		 * pid that needs translating.
		 */
		pid_t org_pid, pid;

		/*
		 * translate the pid into the unique host space as user
		 * may run sessions inside containers or process
		 * namespaces.
		 */
#define VK_MSG_PID_MASK 0xffffff00
#define VK_MSG_PID_SH   8
		org_pid = (entry->to_v_msg[0].arg & VK_MSG_PID_MASK)
			   >> VK_MSG_PID_SH;

		pid = task_tgid_nr(current);
		entry->to_v_msg[0].arg =
			(entry->to_v_msg[0].arg & ~VK_MSG_PID_MASK) |
			(pid << VK_MSG_PID_SH);
		if (org_pid != pid)
			dev_dbg(dev, "In PID 0x%x(%d), converted PID 0x%x(%d)\n",
				org_pid, org_pid, pid, pid);
	}

	/*
	 * store work entry to pending queue until a response is received.
	 * This needs to be done before enqueuing the message
	 */
	bcm_vk_append_pendq(&vk->to_v_msg_chan, q_num, entry);

	rc = bcm_to_v_msg_enqueue(vk, entry);
	if (rc) {
		dev_err(dev, "Fail to enqueue msg to to_v queue\n");

		/* remove message from pending list */
		entry = bcm_vk_dequeue_pending
			       (vk,
				&vk->to_v_msg_chan,
				q_num,
				get_msg_id(&entry->to_v_msg[0]));
		goto write_free_ent;
	}

	return count;

write_free_msgid:
	bcm_vk_msgid_bitmap_clear(vk, get_msg_id(&entry->to_v_msg[0]), 1);
write_free_ent:
	kfree(entry);
write_err:
	return rc;
}

__poll_t bcm_vk_poll(struct file *p_file, struct poll_table_struct *wait)
{
	__poll_t ret = 0;
	int cnt;
	struct bcm_vk_ctx *ctx = p_file->private_data;
	struct bcm_vk *vk = container_of(ctx->miscdev, struct bcm_vk, miscdev);
	struct device *dev = &vk->pdev->dev;

	poll_wait(p_file, &ctx->rd_wq, wait);

	cnt = atomic_read(&ctx->pend_cnt);
	if (cnt) {
		ret = (__force __poll_t)(POLLIN | POLLRDNORM);
		if (cnt < 0) {
			dev_err(dev, "Error cnt %d, setting back to 0", cnt);
			atomic_set(&ctx->pend_cnt, 0);
		}
	}

	return ret;
}

int bcm_vk_release(struct inode *inode, struct file *p_file)
{
	int ret;
	struct bcm_vk_ctx *ctx = p_file->private_data;
	struct bcm_vk *vk = container_of(ctx->miscdev, struct bcm_vk, miscdev);
	struct device *dev = &vk->pdev->dev;
	pid_t pid = ctx->pid;
	int dma_cnt;
	unsigned long timeout, start_time;

	/*
	 * if there are outstanding DMA transactions, need to delay long enough
	 * to ensure that the card side would have stopped touching the host buffer
	 * and its SGL list.  A race condition could happen if the host app is killed
	 * abruptly, eg kill -9, while some DMA transfer orders are still inflight.
	 * Nothing could be done except for a delay as host side is running in a
	 * completely async fashion.
	 */
	start_time = jiffies;
	timeout = start_time + msecs_to_jiffies(BCM_VK_DMA_DRAIN_MAX_MS);
	do {
		if (time_after(jiffies, timeout)) {
			dev_warn(dev, "%d dma still pending for [fd-%d] pid %d\n",
				 dma_cnt, ctx->idx, pid);
			break;
		}
		dma_cnt = atomic_read(&ctx->dma_cnt);
		cpu_relax();
		cond_resched();
	} while (dma_cnt);
	dev_dbg(dev, "Draining for [fd-%d] pid %d - delay %d ms\n",
		ctx->idx, pid, jiffies_to_msecs(jiffies - start_time));

	bcm_vk_drain_all_pend(&vk->pdev->dev, &vk->to_v_msg_chan, ctx);
	bcm_vk_drain_all_pend(&vk->pdev->dev, &vk->to_h_msg_chan, ctx);

	ret = bcm_vk_free_ctx(vk, ctx);
	if (ret == 0)
		ret = bcm_vk_handle_last_sess(vk, pid, ctx->q_num);
	else
		ret = 0;

	kref_put(&vk->kref, bcm_vk_release_data);

	return ret;
}

int bcm_vk_msg_init(struct bcm_vk *vk)
{
	struct device *dev = &vk->pdev->dev;
	int ret;

	if (bcm_vk_data_init(vk)) {
		dev_err(dev, "Error initializing internal data structures\n");
		return -EINVAL;
	}

	if (bcm_vk_msg_chan_init(&vk->to_v_msg_chan) ||
	    bcm_vk_msg_chan_init(&vk->to_h_msg_chan)) {
		dev_err(dev, "Error initializing communication channel\n");
		return -EIO;
	}

	/* read msgq info if ready */
	ret = bcm_vk_sync_msgq(vk, false);
	if (ret && (ret != -EAGAIN)) {
		dev_err(dev, "Error reading comm msg Q info\n");
		return -EIO;
	}

	return 0;
}

void bcm_vk_msg_remove(struct bcm_vk *vk)
{
	bcm_vk_blk_drv_access(vk);

	/* drain all pending items */
	bcm_vk_drain_all_pend(&vk->pdev->dev, &vk->to_v_msg_chan, NULL);
	bcm_vk_drain_all_pend(&vk->pdev->dev, &vk->to_h_msg_chan, NULL);
}


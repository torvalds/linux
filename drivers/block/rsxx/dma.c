// SPDX-License-Identifier: GPL-2.0-or-later
/*
* Filename: dma.c
*
* Authors: Joshua Morris <josh.h.morris@us.ibm.com>
*	Philip Kelleher <pjk1939@linux.vnet.ibm.com>
*
* (C) Copyright 2013 IBM Corporation
*/

#include <linux/slab.h>
#include "rsxx_priv.h"

struct rsxx_dma {
	struct list_head	 list;
	u8			 cmd;
	unsigned int		 laddr;     /* Logical address */
	struct {
		u32		 off;
		u32		 cnt;
	} sub_page;
	dma_addr_t		 dma_addr;
	struct page		 *page;
	unsigned int		 pg_off;    /* Page Offset */
	rsxx_dma_cb		 cb;
	void			 *cb_data;
};

/* This timeout is used to detect a stalled DMA channel */
#define DMA_ACTIVITY_TIMEOUT	msecs_to_jiffies(10000)

struct hw_status {
	u8	status;
	u8	tag;
	__le16	count;
	__le32	_rsvd2;
	__le64	_rsvd3;
} __packed;

enum rsxx_dma_status {
	DMA_SW_ERR    = 0x1,
	DMA_HW_FAULT  = 0x2,
	DMA_CANCELLED = 0x4,
};

struct hw_cmd {
	u8	command;
	u8	tag;
	u8	_rsvd;
	u8	sub_page; /* Bit[0:2]: 512byte offset */
			  /* Bit[4:6]: 512byte count */
	__le32	device_addr;
	__le64	host_addr;
} __packed;

enum rsxx_hw_cmd {
	HW_CMD_BLK_DISCARD	= 0x70,
	HW_CMD_BLK_WRITE	= 0x80,
	HW_CMD_BLK_READ		= 0xC0,
	HW_CMD_BLK_RECON_READ	= 0xE0,
};

enum rsxx_hw_status {
	HW_STATUS_CRC		= 0x01,
	HW_STATUS_HARD_ERR	= 0x02,
	HW_STATUS_SOFT_ERR	= 0x04,
	HW_STATUS_FAULT		= 0x08,
};

static struct kmem_cache *rsxx_dma_pool;

struct dma_tracker {
	int			next_tag;
	struct rsxx_dma	*dma;
};

#define DMA_TRACKER_LIST_SIZE8 (sizeof(struct dma_tracker_list) + \
		(sizeof(struct dma_tracker) * RSXX_MAX_OUTSTANDING_CMDS))

struct dma_tracker_list {
	spinlock_t		lock;
	int			head;
	struct dma_tracker	list[0];
};


/*----------------- Misc Utility Functions -------------------*/
static unsigned int rsxx_addr8_to_laddr(u64 addr8, struct rsxx_cardinfo *card)
{
	unsigned long long tgt_addr8;

	tgt_addr8 = ((addr8 >> card->_stripe.upper_shift) &
		      card->_stripe.upper_mask) |
		    ((addr8) & card->_stripe.lower_mask);
	do_div(tgt_addr8, RSXX_HW_BLK_SIZE);
	return tgt_addr8;
}

static unsigned int rsxx_get_dma_tgt(struct rsxx_cardinfo *card, u64 addr8)
{
	unsigned int tgt;

	tgt = (addr8 >> card->_stripe.target_shift) & card->_stripe.target_mask;

	return tgt;
}

void rsxx_dma_queue_reset(struct rsxx_cardinfo *card)
{
	/* Reset all DMA Command/Status Queues */
	iowrite32(DMA_QUEUE_RESET, card->regmap + RESET);
}

static unsigned int get_dma_size(struct rsxx_dma *dma)
{
	if (dma->sub_page.cnt)
		return dma->sub_page.cnt << 9;
	else
		return RSXX_HW_BLK_SIZE;
}


/*----------------- DMA Tracker -------------------*/
static void set_tracker_dma(struct dma_tracker_list *trackers,
			    int tag,
			    struct rsxx_dma *dma)
{
	trackers->list[tag].dma = dma;
}

static struct rsxx_dma *get_tracker_dma(struct dma_tracker_list *trackers,
					    int tag)
{
	return trackers->list[tag].dma;
}

static int pop_tracker(struct dma_tracker_list *trackers)
{
	int tag;

	spin_lock(&trackers->lock);
	tag = trackers->head;
	if (tag != -1) {
		trackers->head = trackers->list[tag].next_tag;
		trackers->list[tag].next_tag = -1;
	}
	spin_unlock(&trackers->lock);

	return tag;
}

static void push_tracker(struct dma_tracker_list *trackers, int tag)
{
	spin_lock(&trackers->lock);
	trackers->list[tag].next_tag = trackers->head;
	trackers->head = tag;
	trackers->list[tag].dma = NULL;
	spin_unlock(&trackers->lock);
}


/*----------------- Interrupt Coalescing -------------*/
/*
 * Interrupt Coalescing Register Format:
 * Interrupt Timer (64ns units) [15:0]
 * Interrupt Count [24:16]
 * Reserved [31:25]
*/
#define INTR_COAL_LATENCY_MASK       (0x0000ffff)

#define INTR_COAL_COUNT_SHIFT        16
#define INTR_COAL_COUNT_BITS         9
#define INTR_COAL_COUNT_MASK         (((1 << INTR_COAL_COUNT_BITS) - 1) << \
					INTR_COAL_COUNT_SHIFT)
#define INTR_COAL_LATENCY_UNITS_NS   64


static u32 dma_intr_coal_val(u32 mode, u32 count, u32 latency)
{
	u32 latency_units = latency / INTR_COAL_LATENCY_UNITS_NS;

	if (mode == RSXX_INTR_COAL_DISABLED)
		return 0;

	return ((count << INTR_COAL_COUNT_SHIFT) & INTR_COAL_COUNT_MASK) |
			(latency_units & INTR_COAL_LATENCY_MASK);

}

static void dma_intr_coal_auto_tune(struct rsxx_cardinfo *card)
{
	int i;
	u32 q_depth = 0;
	u32 intr_coal;

	if (card->config.data.intr_coal.mode != RSXX_INTR_COAL_AUTO_TUNE ||
	    unlikely(card->eeh_state))
		return;

	for (i = 0; i < card->n_targets; i++)
		q_depth += atomic_read(&card->ctrl[i].stats.hw_q_depth);

	intr_coal = dma_intr_coal_val(card->config.data.intr_coal.mode,
				      q_depth / 2,
				      card->config.data.intr_coal.latency);
	iowrite32(intr_coal, card->regmap + INTR_COAL);
}

/*----------------- RSXX DMA Handling -------------------*/
static void rsxx_free_dma(struct rsxx_dma_ctrl *ctrl, struct rsxx_dma *dma)
{
	if (dma->cmd != HW_CMD_BLK_DISCARD) {
		if (!dma_mapping_error(&ctrl->card->dev->dev, dma->dma_addr)) {
			dma_unmap_page(&ctrl->card->dev->dev, dma->dma_addr,
				       get_dma_size(dma),
				       dma->cmd == HW_CMD_BLK_WRITE ?
						   DMA_TO_DEVICE :
						   DMA_FROM_DEVICE);
		}
	}

	kmem_cache_free(rsxx_dma_pool, dma);
}

static void rsxx_complete_dma(struct rsxx_dma_ctrl *ctrl,
				  struct rsxx_dma *dma,
				  unsigned int status)
{
	if (status & DMA_SW_ERR)
		ctrl->stats.dma_sw_err++;
	if (status & DMA_HW_FAULT)
		ctrl->stats.dma_hw_fault++;
	if (status & DMA_CANCELLED)
		ctrl->stats.dma_cancelled++;

	if (dma->cb)
		dma->cb(ctrl->card, dma->cb_data, status ? 1 : 0);

	rsxx_free_dma(ctrl, dma);
}

int rsxx_cleanup_dma_queue(struct rsxx_dma_ctrl *ctrl,
			   struct list_head *q, unsigned int done)
{
	struct rsxx_dma *dma;
	struct rsxx_dma *tmp;
	int cnt = 0;

	list_for_each_entry_safe(dma, tmp, q, list) {
		list_del(&dma->list);
		if (done & COMPLETE_DMA)
			rsxx_complete_dma(ctrl, dma, DMA_CANCELLED);
		else
			rsxx_free_dma(ctrl, dma);
		cnt++;
	}

	return cnt;
}

static void rsxx_requeue_dma(struct rsxx_dma_ctrl *ctrl,
				 struct rsxx_dma *dma)
{
	/*
	 * Requeued DMAs go to the front of the queue so they are issued
	 * first.
	 */
	spin_lock_bh(&ctrl->queue_lock);
	ctrl->stats.sw_q_depth++;
	list_add(&dma->list, &ctrl->queue);
	spin_unlock_bh(&ctrl->queue_lock);
}

static void rsxx_handle_dma_error(struct rsxx_dma_ctrl *ctrl,
				      struct rsxx_dma *dma,
				      u8 hw_st)
{
	unsigned int status = 0;
	int requeue_cmd = 0;

	dev_dbg(CARD_TO_DEV(ctrl->card),
		"Handling DMA error(cmd x%02x, laddr x%08x st:x%02x)\n",
		dma->cmd, dma->laddr, hw_st);

	if (hw_st & HW_STATUS_CRC)
		ctrl->stats.crc_errors++;
	if (hw_st & HW_STATUS_HARD_ERR)
		ctrl->stats.hard_errors++;
	if (hw_st & HW_STATUS_SOFT_ERR)
		ctrl->stats.soft_errors++;

	switch (dma->cmd) {
	case HW_CMD_BLK_READ:
		if (hw_st & (HW_STATUS_CRC | HW_STATUS_HARD_ERR)) {
			if (ctrl->card->scrub_hard) {
				dma->cmd = HW_CMD_BLK_RECON_READ;
				requeue_cmd = 1;
				ctrl->stats.reads_retried++;
			} else {
				status |= DMA_HW_FAULT;
				ctrl->stats.reads_failed++;
			}
		} else if (hw_st & HW_STATUS_FAULT) {
			status |= DMA_HW_FAULT;
			ctrl->stats.reads_failed++;
		}

		break;
	case HW_CMD_BLK_RECON_READ:
		if (hw_st & (HW_STATUS_CRC | HW_STATUS_HARD_ERR)) {
			/* Data could not be reconstructed. */
			status |= DMA_HW_FAULT;
			ctrl->stats.reads_failed++;
		}

		break;
	case HW_CMD_BLK_WRITE:
		status |= DMA_HW_FAULT;
		ctrl->stats.writes_failed++;

		break;
	case HW_CMD_BLK_DISCARD:
		status |= DMA_HW_FAULT;
		ctrl->stats.discards_failed++;

		break;
	default:
		dev_err(CARD_TO_DEV(ctrl->card),
			"Unknown command in DMA!(cmd: x%02x "
			   "laddr x%08x st: x%02x\n",
			   dma->cmd, dma->laddr, hw_st);
		status |= DMA_SW_ERR;

		break;
	}

	if (requeue_cmd)
		rsxx_requeue_dma(ctrl, dma);
	else
		rsxx_complete_dma(ctrl, dma, status);
}

static void dma_engine_stalled(struct timer_list *t)
{
	struct rsxx_dma_ctrl *ctrl = from_timer(ctrl, t, activity_timer);
	int cnt;

	if (atomic_read(&ctrl->stats.hw_q_depth) == 0 ||
	    unlikely(ctrl->card->eeh_state))
		return;

	if (ctrl->cmd.idx != ioread32(ctrl->regmap + SW_CMD_IDX)) {
		/*
		 * The dma engine was stalled because the SW_CMD_IDX write
		 * was lost. Issue it again to recover.
		 */
		dev_warn(CARD_TO_DEV(ctrl->card),
			"SW_CMD_IDX write was lost, re-writing...\n");
		iowrite32(ctrl->cmd.idx, ctrl->regmap + SW_CMD_IDX);
		mod_timer(&ctrl->activity_timer,
			  jiffies + DMA_ACTIVITY_TIMEOUT);
	} else {
		dev_warn(CARD_TO_DEV(ctrl->card),
			"DMA channel %d has stalled, faulting interface.\n",
			ctrl->id);
		ctrl->card->dma_fault = 1;

		/* Clean up the DMA queue */
		spin_lock(&ctrl->queue_lock);
		cnt = rsxx_cleanup_dma_queue(ctrl, &ctrl->queue, COMPLETE_DMA);
		spin_unlock(&ctrl->queue_lock);

		cnt += rsxx_dma_cancel(ctrl);

		if (cnt)
			dev_info(CARD_TO_DEV(ctrl->card),
				"Freed %d queued DMAs on channel %d\n",
				cnt, ctrl->id);
	}
}

static void rsxx_issue_dmas(struct rsxx_dma_ctrl *ctrl)
{
	struct rsxx_dma *dma;
	int tag;
	int cmds_pending = 0;
	struct hw_cmd *hw_cmd_buf;
	int dir;

	hw_cmd_buf = ctrl->cmd.buf;

	if (unlikely(ctrl->card->halt) ||
	    unlikely(ctrl->card->eeh_state))
		return;

	while (1) {
		spin_lock_bh(&ctrl->queue_lock);
		if (list_empty(&ctrl->queue)) {
			spin_unlock_bh(&ctrl->queue_lock);
			break;
		}
		spin_unlock_bh(&ctrl->queue_lock);

		tag = pop_tracker(ctrl->trackers);
		if (tag == -1)
			break;

		spin_lock_bh(&ctrl->queue_lock);
		dma = list_entry(ctrl->queue.next, struct rsxx_dma, list);
		list_del(&dma->list);
		ctrl->stats.sw_q_depth--;
		spin_unlock_bh(&ctrl->queue_lock);

		/*
		 * This will catch any DMAs that slipped in right before the
		 * fault, but was queued after all the other DMAs were
		 * cancelled.
		 */
		if (unlikely(ctrl->card->dma_fault)) {
			push_tracker(ctrl->trackers, tag);
			rsxx_complete_dma(ctrl, dma, DMA_CANCELLED);
			continue;
		}

		if (dma->cmd != HW_CMD_BLK_DISCARD) {
			if (dma->cmd == HW_CMD_BLK_WRITE)
				dir = DMA_TO_DEVICE;
			else
				dir = DMA_FROM_DEVICE;

			/*
			 * The function dma_map_page is placed here because we
			 * can only, by design, issue up to 255 commands to the
			 * hardware at one time per DMA channel. So the maximum
			 * amount of mapped memory would be 255 * 4 channels *
			 * 4096 Bytes which is less than 2GB, the limit of a x8
			 * Non-HWWD PCIe slot. This way the dma_map_page
			 * function should never fail because of a lack of
			 * mappable memory.
			 */
			dma->dma_addr = dma_map_page(&ctrl->card->dev->dev, dma->page,
					dma->pg_off, dma->sub_page.cnt << 9, dir);
			if (dma_mapping_error(&ctrl->card->dev->dev, dma->dma_addr)) {
				push_tracker(ctrl->trackers, tag);
				rsxx_complete_dma(ctrl, dma, DMA_CANCELLED);
				continue;
			}
		}

		set_tracker_dma(ctrl->trackers, tag, dma);
		hw_cmd_buf[ctrl->cmd.idx].command  = dma->cmd;
		hw_cmd_buf[ctrl->cmd.idx].tag      = tag;
		hw_cmd_buf[ctrl->cmd.idx]._rsvd    = 0;
		hw_cmd_buf[ctrl->cmd.idx].sub_page =
					((dma->sub_page.cnt & 0x7) << 4) |
					 (dma->sub_page.off & 0x7);

		hw_cmd_buf[ctrl->cmd.idx].device_addr =
					cpu_to_le32(dma->laddr);

		hw_cmd_buf[ctrl->cmd.idx].host_addr =
					cpu_to_le64(dma->dma_addr);

		dev_dbg(CARD_TO_DEV(ctrl->card),
			"Issue DMA%d(laddr %d tag %d) to idx %d\n",
			ctrl->id, dma->laddr, tag, ctrl->cmd.idx);

		ctrl->cmd.idx = (ctrl->cmd.idx + 1) & RSXX_CS_IDX_MASK;
		cmds_pending++;

		if (dma->cmd == HW_CMD_BLK_WRITE)
			ctrl->stats.writes_issued++;
		else if (dma->cmd == HW_CMD_BLK_DISCARD)
			ctrl->stats.discards_issued++;
		else
			ctrl->stats.reads_issued++;
	}

	/* Let HW know we've queued commands. */
	if (cmds_pending) {
		atomic_add(cmds_pending, &ctrl->stats.hw_q_depth);
		mod_timer(&ctrl->activity_timer,
			  jiffies + DMA_ACTIVITY_TIMEOUT);

		if (unlikely(ctrl->card->eeh_state)) {
			del_timer_sync(&ctrl->activity_timer);
			return;
		}

		iowrite32(ctrl->cmd.idx, ctrl->regmap + SW_CMD_IDX);
	}
}

static void rsxx_dma_done(struct rsxx_dma_ctrl *ctrl)
{
	struct rsxx_dma *dma;
	unsigned long flags;
	u16 count;
	u8 status;
	u8 tag;
	struct hw_status *hw_st_buf;

	hw_st_buf = ctrl->status.buf;

	if (unlikely(ctrl->card->halt) ||
	    unlikely(ctrl->card->dma_fault) ||
	    unlikely(ctrl->card->eeh_state))
		return;

	count = le16_to_cpu(hw_st_buf[ctrl->status.idx].count);

	while (count == ctrl->e_cnt) {
		/*
		 * The read memory-barrier is necessary to keep aggressive
		 * processors/optimizers (such as the PPC Apple G5) from
		 * reordering the following status-buffer tag & status read
		 * *before* the count read on subsequent iterations of the
		 * loop!
		 */
		rmb();

		status = hw_st_buf[ctrl->status.idx].status;
		tag    = hw_st_buf[ctrl->status.idx].tag;

		dma = get_tracker_dma(ctrl->trackers, tag);
		if (dma == NULL) {
			spin_lock_irqsave(&ctrl->card->irq_lock, flags);
			rsxx_disable_ier(ctrl->card, CR_INTR_DMA_ALL);
			spin_unlock_irqrestore(&ctrl->card->irq_lock, flags);

			dev_err(CARD_TO_DEV(ctrl->card),
				"No tracker for tag %d "
				"(idx %d id %d)\n",
				tag, ctrl->status.idx, ctrl->id);
			return;
		}

		dev_dbg(CARD_TO_DEV(ctrl->card),
			"Completing DMA%d"
			"(laddr x%x tag %d st: x%x cnt: x%04x) from idx %d.\n",
			ctrl->id, dma->laddr, tag, status, count,
			ctrl->status.idx);

		atomic_dec(&ctrl->stats.hw_q_depth);

		mod_timer(&ctrl->activity_timer,
			  jiffies + DMA_ACTIVITY_TIMEOUT);

		if (status)
			rsxx_handle_dma_error(ctrl, dma, status);
		else
			rsxx_complete_dma(ctrl, dma, 0);

		push_tracker(ctrl->trackers, tag);

		ctrl->status.idx = (ctrl->status.idx + 1) &
				   RSXX_CS_IDX_MASK;
		ctrl->e_cnt++;

		count = le16_to_cpu(hw_st_buf[ctrl->status.idx].count);
	}

	dma_intr_coal_auto_tune(ctrl->card);

	if (atomic_read(&ctrl->stats.hw_q_depth) == 0)
		del_timer_sync(&ctrl->activity_timer);

	spin_lock_irqsave(&ctrl->card->irq_lock, flags);
	rsxx_enable_ier(ctrl->card, CR_INTR_DMA(ctrl->id));
	spin_unlock_irqrestore(&ctrl->card->irq_lock, flags);

	spin_lock_bh(&ctrl->queue_lock);
	if (ctrl->stats.sw_q_depth)
		queue_work(ctrl->issue_wq, &ctrl->issue_dma_work);
	spin_unlock_bh(&ctrl->queue_lock);
}

static void rsxx_schedule_issue(struct work_struct *work)
{
	struct rsxx_dma_ctrl *ctrl;

	ctrl = container_of(work, struct rsxx_dma_ctrl, issue_dma_work);

	mutex_lock(&ctrl->work_lock);
	rsxx_issue_dmas(ctrl);
	mutex_unlock(&ctrl->work_lock);
}

static void rsxx_schedule_done(struct work_struct *work)
{
	struct rsxx_dma_ctrl *ctrl;

	ctrl = container_of(work, struct rsxx_dma_ctrl, dma_done_work);

	mutex_lock(&ctrl->work_lock);
	rsxx_dma_done(ctrl);
	mutex_unlock(&ctrl->work_lock);
}

static blk_status_t rsxx_queue_discard(struct rsxx_cardinfo *card,
				  struct list_head *q,
				  unsigned int laddr,
				  rsxx_dma_cb cb,
				  void *cb_data)
{
	struct rsxx_dma *dma;

	dma = kmem_cache_alloc(rsxx_dma_pool, GFP_KERNEL);
	if (!dma)
		return BLK_STS_RESOURCE;

	dma->cmd          = HW_CMD_BLK_DISCARD;
	dma->laddr        = laddr;
	dma->dma_addr     = 0;
	dma->sub_page.off = 0;
	dma->sub_page.cnt = 0;
	dma->page         = NULL;
	dma->pg_off       = 0;
	dma->cb	          = cb;
	dma->cb_data      = cb_data;

	dev_dbg(CARD_TO_DEV(card), "Queuing[D] laddr %x\n", dma->laddr);

	list_add_tail(&dma->list, q);

	return 0;
}

static blk_status_t rsxx_queue_dma(struct rsxx_cardinfo *card,
			      struct list_head *q,
			      int dir,
			      unsigned int dma_off,
			      unsigned int dma_len,
			      unsigned int laddr,
			      struct page *page,
			      unsigned int pg_off,
			      rsxx_dma_cb cb,
			      void *cb_data)
{
	struct rsxx_dma *dma;

	dma = kmem_cache_alloc(rsxx_dma_pool, GFP_KERNEL);
	if (!dma)
		return BLK_STS_RESOURCE;

	dma->cmd          = dir ? HW_CMD_BLK_WRITE : HW_CMD_BLK_READ;
	dma->laddr        = laddr;
	dma->sub_page.off = (dma_off >> 9);
	dma->sub_page.cnt = (dma_len >> 9);
	dma->page         = page;
	dma->pg_off       = pg_off;
	dma->cb	          = cb;
	dma->cb_data      = cb_data;

	dev_dbg(CARD_TO_DEV(card),
		"Queuing[%c] laddr %x off %d cnt %d page %p pg_off %d\n",
		dir ? 'W' : 'R', dma->laddr, dma->sub_page.off,
		dma->sub_page.cnt, dma->page, dma->pg_off);

	/* Queue the DMA */
	list_add_tail(&dma->list, q);

	return 0;
}

blk_status_t rsxx_dma_queue_bio(struct rsxx_cardinfo *card,
			   struct bio *bio,
			   atomic_t *n_dmas,
			   rsxx_dma_cb cb,
			   void *cb_data)
{
	struct list_head dma_list[RSXX_MAX_TARGETS];
	struct bio_vec bvec;
	struct bvec_iter iter;
	unsigned long long addr8;
	unsigned int laddr;
	unsigned int bv_len;
	unsigned int bv_off;
	unsigned int dma_off;
	unsigned int dma_len;
	int dma_cnt[RSXX_MAX_TARGETS];
	int tgt;
	blk_status_t st;
	int i;

	addr8 = bio->bi_iter.bi_sector << 9; /* sectors are 512 bytes */
	atomic_set(n_dmas, 0);

	for (i = 0; i < card->n_targets; i++) {
		INIT_LIST_HEAD(&dma_list[i]);
		dma_cnt[i] = 0;
	}

	if (bio_op(bio) == REQ_OP_DISCARD) {
		bv_len = bio->bi_iter.bi_size;

		while (bv_len > 0) {
			tgt   = rsxx_get_dma_tgt(card, addr8);
			laddr = rsxx_addr8_to_laddr(addr8, card);

			st = rsxx_queue_discard(card, &dma_list[tgt], laddr,
						    cb, cb_data);
			if (st)
				goto bvec_err;

			dma_cnt[tgt]++;
			atomic_inc(n_dmas);
			addr8  += RSXX_HW_BLK_SIZE;
			bv_len -= RSXX_HW_BLK_SIZE;
		}
	} else {
		bio_for_each_segment(bvec, bio, iter) {
			bv_len = bvec.bv_len;
			bv_off = bvec.bv_offset;

			while (bv_len > 0) {
				tgt   = rsxx_get_dma_tgt(card, addr8);
				laddr = rsxx_addr8_to_laddr(addr8, card);
				dma_off = addr8 & RSXX_HW_BLK_MASK;
				dma_len = min(bv_len,
					      RSXX_HW_BLK_SIZE - dma_off);

				st = rsxx_queue_dma(card, &dma_list[tgt],
							bio_data_dir(bio),
							dma_off, dma_len,
							laddr, bvec.bv_page,
							bv_off, cb, cb_data);
				if (st)
					goto bvec_err;

				dma_cnt[tgt]++;
				atomic_inc(n_dmas);
				addr8  += dma_len;
				bv_off += dma_len;
				bv_len -= dma_len;
			}
		}
	}

	for (i = 0; i < card->n_targets; i++) {
		if (!list_empty(&dma_list[i])) {
			spin_lock_bh(&card->ctrl[i].queue_lock);
			card->ctrl[i].stats.sw_q_depth += dma_cnt[i];
			list_splice_tail(&dma_list[i], &card->ctrl[i].queue);
			spin_unlock_bh(&card->ctrl[i].queue_lock);

			queue_work(card->ctrl[i].issue_wq,
				   &card->ctrl[i].issue_dma_work);
		}
	}

	return 0;

bvec_err:
	for (i = 0; i < card->n_targets; i++)
		rsxx_cleanup_dma_queue(&card->ctrl[i], &dma_list[i],
					FREE_DMA);
	return st;
}


/*----------------- DMA Engine Initialization & Setup -------------------*/
int rsxx_hw_buffers_init(struct pci_dev *dev, struct rsxx_dma_ctrl *ctrl)
{
	ctrl->status.buf = dma_alloc_coherent(&dev->dev, STATUS_BUFFER_SIZE8,
				&ctrl->status.dma_addr, GFP_KERNEL);
	ctrl->cmd.buf = dma_alloc_coherent(&dev->dev, COMMAND_BUFFER_SIZE8,
				&ctrl->cmd.dma_addr, GFP_KERNEL);
	if (ctrl->status.buf == NULL || ctrl->cmd.buf == NULL)
		return -ENOMEM;

	memset(ctrl->status.buf, 0xac, STATUS_BUFFER_SIZE8);
	iowrite32(lower_32_bits(ctrl->status.dma_addr),
		ctrl->regmap + SB_ADD_LO);
	iowrite32(upper_32_bits(ctrl->status.dma_addr),
		ctrl->regmap + SB_ADD_HI);

	memset(ctrl->cmd.buf, 0x83, COMMAND_BUFFER_SIZE8);
	iowrite32(lower_32_bits(ctrl->cmd.dma_addr), ctrl->regmap + CB_ADD_LO);
	iowrite32(upper_32_bits(ctrl->cmd.dma_addr), ctrl->regmap + CB_ADD_HI);

	ctrl->status.idx = ioread32(ctrl->regmap + HW_STATUS_CNT);
	if (ctrl->status.idx > RSXX_MAX_OUTSTANDING_CMDS) {
		dev_crit(&dev->dev, "Failed reading status cnt x%x\n",
			ctrl->status.idx);
		return -EINVAL;
	}
	iowrite32(ctrl->status.idx, ctrl->regmap + HW_STATUS_CNT);
	iowrite32(ctrl->status.idx, ctrl->regmap + SW_STATUS_CNT);

	ctrl->cmd.idx = ioread32(ctrl->regmap + HW_CMD_IDX);
	if (ctrl->cmd.idx > RSXX_MAX_OUTSTANDING_CMDS) {
		dev_crit(&dev->dev, "Failed reading cmd cnt x%x\n",
			ctrl->status.idx);
		return -EINVAL;
	}
	iowrite32(ctrl->cmd.idx, ctrl->regmap + HW_CMD_IDX);
	iowrite32(ctrl->cmd.idx, ctrl->regmap + SW_CMD_IDX);

	return 0;
}

static int rsxx_dma_ctrl_init(struct pci_dev *dev,
				  struct rsxx_dma_ctrl *ctrl)
{
	int i;
	int st;

	memset(&ctrl->stats, 0, sizeof(ctrl->stats));

	ctrl->trackers = vmalloc(DMA_TRACKER_LIST_SIZE8);
	if (!ctrl->trackers)
		return -ENOMEM;

	ctrl->trackers->head = 0;
	for (i = 0; i < RSXX_MAX_OUTSTANDING_CMDS; i++) {
		ctrl->trackers->list[i].next_tag = i + 1;
		ctrl->trackers->list[i].dma = NULL;
	}
	ctrl->trackers->list[RSXX_MAX_OUTSTANDING_CMDS-1].next_tag = -1;
	spin_lock_init(&ctrl->trackers->lock);

	spin_lock_init(&ctrl->queue_lock);
	mutex_init(&ctrl->work_lock);
	INIT_LIST_HEAD(&ctrl->queue);

	timer_setup(&ctrl->activity_timer, dma_engine_stalled, 0);

	ctrl->issue_wq = alloc_ordered_workqueue(DRIVER_NAME"_issue", 0);
	if (!ctrl->issue_wq)
		return -ENOMEM;

	ctrl->done_wq = alloc_ordered_workqueue(DRIVER_NAME"_done", 0);
	if (!ctrl->done_wq)
		return -ENOMEM;

	INIT_WORK(&ctrl->issue_dma_work, rsxx_schedule_issue);
	INIT_WORK(&ctrl->dma_done_work, rsxx_schedule_done);

	st = rsxx_hw_buffers_init(dev, ctrl);
	if (st)
		return st;

	return 0;
}

static int rsxx_dma_stripe_setup(struct rsxx_cardinfo *card,
			      unsigned int stripe_size8)
{
	if (!is_power_of_2(stripe_size8)) {
		dev_err(CARD_TO_DEV(card),
			"stripe_size is NOT a power of 2!\n");
		return -EINVAL;
	}

	card->_stripe.lower_mask = stripe_size8 - 1;

	card->_stripe.upper_mask  = ~(card->_stripe.lower_mask);
	card->_stripe.upper_shift = ffs(card->n_targets) - 1;

	card->_stripe.target_mask = card->n_targets - 1;
	card->_stripe.target_shift = ffs(stripe_size8) - 1;

	dev_dbg(CARD_TO_DEV(card), "_stripe.lower_mask   = x%016llx\n",
		card->_stripe.lower_mask);
	dev_dbg(CARD_TO_DEV(card), "_stripe.upper_shift  = x%016llx\n",
		card->_stripe.upper_shift);
	dev_dbg(CARD_TO_DEV(card), "_stripe.upper_mask   = x%016llx\n",
		card->_stripe.upper_mask);
	dev_dbg(CARD_TO_DEV(card), "_stripe.target_mask  = x%016llx\n",
		card->_stripe.target_mask);
	dev_dbg(CARD_TO_DEV(card), "_stripe.target_shift = x%016llx\n",
		card->_stripe.target_shift);

	return 0;
}

int rsxx_dma_configure(struct rsxx_cardinfo *card)
{
	u32 intr_coal;

	intr_coal = dma_intr_coal_val(card->config.data.intr_coal.mode,
				      card->config.data.intr_coal.count,
				      card->config.data.intr_coal.latency);
	iowrite32(intr_coal, card->regmap + INTR_COAL);

	return rsxx_dma_stripe_setup(card, card->config.data.stripe_size);
}

int rsxx_dma_setup(struct rsxx_cardinfo *card)
{
	unsigned long flags;
	int st;
	int i;

	dev_info(CARD_TO_DEV(card),
		"Initializing %d DMA targets\n",
		card->n_targets);

	/* Regmap is divided up into 4K chunks. One for each DMA channel */
	for (i = 0; i < card->n_targets; i++)
		card->ctrl[i].regmap = card->regmap + (i * 4096);

	card->dma_fault = 0;

	/* Reset the DMA queues */
	rsxx_dma_queue_reset(card);

	/************* Setup DMA Control *************/
	for (i = 0; i < card->n_targets; i++) {
		st = rsxx_dma_ctrl_init(card->dev, &card->ctrl[i]);
		if (st)
			goto failed_dma_setup;

		card->ctrl[i].card = card;
		card->ctrl[i].id = i;
	}

	card->scrub_hard = 1;

	if (card->config_valid)
		rsxx_dma_configure(card);

	/* Enable the interrupts after all setup has completed. */
	for (i = 0; i < card->n_targets; i++) {
		spin_lock_irqsave(&card->irq_lock, flags);
		rsxx_enable_ier_and_isr(card, CR_INTR_DMA(i));
		spin_unlock_irqrestore(&card->irq_lock, flags);
	}

	return 0;

failed_dma_setup:
	for (i = 0; i < card->n_targets; i++) {
		struct rsxx_dma_ctrl *ctrl = &card->ctrl[i];

		if (ctrl->issue_wq) {
			destroy_workqueue(ctrl->issue_wq);
			ctrl->issue_wq = NULL;
		}

		if (ctrl->done_wq) {
			destroy_workqueue(ctrl->done_wq);
			ctrl->done_wq = NULL;
		}

		if (ctrl->trackers)
			vfree(ctrl->trackers);

		if (ctrl->status.buf)
			dma_free_coherent(&card->dev->dev, STATUS_BUFFER_SIZE8,
					  ctrl->status.buf,
					  ctrl->status.dma_addr);
		if (ctrl->cmd.buf)
			dma_free_coherent(&card->dev->dev, COMMAND_BUFFER_SIZE8,
					  ctrl->cmd.buf, ctrl->cmd.dma_addr);
	}

	return st;
}

int rsxx_dma_cancel(struct rsxx_dma_ctrl *ctrl)
{
	struct rsxx_dma *dma;
	int i;
	int cnt = 0;

	/* Clean up issued DMAs */
	for (i = 0; i < RSXX_MAX_OUTSTANDING_CMDS; i++) {
		dma = get_tracker_dma(ctrl->trackers, i);
		if (dma) {
			atomic_dec(&ctrl->stats.hw_q_depth);
			rsxx_complete_dma(ctrl, dma, DMA_CANCELLED);
			push_tracker(ctrl->trackers, i);
			cnt++;
		}
	}

	return cnt;
}

void rsxx_dma_destroy(struct rsxx_cardinfo *card)
{
	struct rsxx_dma_ctrl *ctrl;
	int i;

	for (i = 0; i < card->n_targets; i++) {
		ctrl = &card->ctrl[i];

		if (ctrl->issue_wq) {
			destroy_workqueue(ctrl->issue_wq);
			ctrl->issue_wq = NULL;
		}

		if (ctrl->done_wq) {
			destroy_workqueue(ctrl->done_wq);
			ctrl->done_wq = NULL;
		}

		if (timer_pending(&ctrl->activity_timer))
			del_timer_sync(&ctrl->activity_timer);

		/* Clean up the DMA queue */
		spin_lock_bh(&ctrl->queue_lock);
		rsxx_cleanup_dma_queue(ctrl, &ctrl->queue, COMPLETE_DMA);
		spin_unlock_bh(&ctrl->queue_lock);

		rsxx_dma_cancel(ctrl);

		vfree(ctrl->trackers);

		dma_free_coherent(&card->dev->dev, STATUS_BUFFER_SIZE8,
				  ctrl->status.buf, ctrl->status.dma_addr);
		dma_free_coherent(&card->dev->dev, COMMAND_BUFFER_SIZE8,
				  ctrl->cmd.buf, ctrl->cmd.dma_addr);
	}
}

int rsxx_eeh_save_issued_dmas(struct rsxx_cardinfo *card)
{
	int i;
	int j;
	int cnt;
	struct rsxx_dma *dma;
	struct list_head *issued_dmas;

	issued_dmas = kcalloc(card->n_targets, sizeof(*issued_dmas),
			      GFP_KERNEL);
	if (!issued_dmas)
		return -ENOMEM;

	for (i = 0; i < card->n_targets; i++) {
		INIT_LIST_HEAD(&issued_dmas[i]);
		cnt = 0;
		for (j = 0; j < RSXX_MAX_OUTSTANDING_CMDS; j++) {
			dma = get_tracker_dma(card->ctrl[i].trackers, j);
			if (dma == NULL)
				continue;

			if (dma->cmd == HW_CMD_BLK_WRITE)
				card->ctrl[i].stats.writes_issued--;
			else if (dma->cmd == HW_CMD_BLK_DISCARD)
				card->ctrl[i].stats.discards_issued--;
			else
				card->ctrl[i].stats.reads_issued--;

			if (dma->cmd != HW_CMD_BLK_DISCARD) {
				dma_unmap_page(&card->dev->dev, dma->dma_addr,
					       get_dma_size(dma),
					       dma->cmd == HW_CMD_BLK_WRITE ?
					       DMA_TO_DEVICE :
					       DMA_FROM_DEVICE);
			}

			list_add_tail(&dma->list, &issued_dmas[i]);
			push_tracker(card->ctrl[i].trackers, j);
			cnt++;
		}

		spin_lock_bh(&card->ctrl[i].queue_lock);
		list_splice(&issued_dmas[i], &card->ctrl[i].queue);

		atomic_sub(cnt, &card->ctrl[i].stats.hw_q_depth);
		card->ctrl[i].stats.sw_q_depth += cnt;
		card->ctrl[i].e_cnt = 0;
		spin_unlock_bh(&card->ctrl[i].queue_lock);
	}

	kfree(issued_dmas);

	return 0;
}

int rsxx_dma_init(void)
{
	rsxx_dma_pool = KMEM_CACHE(rsxx_dma, SLAB_HWCACHE_ALIGN);
	if (!rsxx_dma_pool)
		return -ENOMEM;

	return 0;
}


void rsxx_dma_cleanup(void)
{
	kmem_cache_destroy(rsxx_dma_pool);
}


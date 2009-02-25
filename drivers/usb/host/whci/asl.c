/*
 * Wireless Host Controller (WHC) asynchronous schedule management.
 *
 * Copyright (C) 2007 Cambridge Silicon Radio Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <linux/kernel.h>
#include <linux/dma-mapping.h>
#include <linux/uwb/umc.h>
#include <linux/usb.h>

#include "../../wusbcore/wusbhc.h"

#include "whcd.h"

static void qset_get_next_prev(struct whc *whc, struct whc_qset *qset,
			       struct whc_qset **next, struct whc_qset **prev)
{
	struct list_head *n, *p;

	BUG_ON(list_empty(&whc->async_list));

	n = qset->list_node.next;
	if (n == &whc->async_list)
		n = n->next;
	p = qset->list_node.prev;
	if (p == &whc->async_list)
		p = p->prev;

	*next = container_of(n, struct whc_qset, list_node);
	*prev = container_of(p, struct whc_qset, list_node);

}

static void asl_qset_insert_begin(struct whc *whc, struct whc_qset *qset)
{
	list_move(&qset->list_node, &whc->async_list);
	qset->in_sw_list = true;
}

static void asl_qset_insert(struct whc *whc, struct whc_qset *qset)
{
	struct whc_qset *next, *prev;

	qset_clear(whc, qset);

	/* Link into ASL. */
	qset_get_next_prev(whc, qset, &next, &prev);
	whc_qset_set_link_ptr(&qset->qh.link, next->qset_dma);
	whc_qset_set_link_ptr(&prev->qh.link, qset->qset_dma);
	qset->in_hw_list = true;
}

static void asl_qset_remove(struct whc *whc, struct whc_qset *qset)
{
	struct whc_qset *prev, *next;

	qset_get_next_prev(whc, qset, &next, &prev);

	list_move(&qset->list_node, &whc->async_removed_list);
	qset->in_sw_list = false;

	/*
	 * No more qsets in the ASL?  The caller must stop the ASL as
	 * it's no longer valid.
	 */
	if (list_empty(&whc->async_list))
		return;

	/* Remove from ASL. */
	whc_qset_set_link_ptr(&prev->qh.link, next->qset_dma);
	qset->in_hw_list = false;
}

/**
 * process_qset - process any recently inactivated or halted qTDs in a
 * qset.
 *
 * After inactive qTDs are removed, new qTDs can be added if the
 * urb queue still contains URBs.
 *
 * Returns any additional WUSBCMD bits for the ASL sync command (i.e.,
 * WUSBCMD_ASYNC_QSET_RM if a halted qset was removed).
 */
static uint32_t process_qset(struct whc *whc, struct whc_qset *qset)
{
	enum whc_update update = 0;
	uint32_t status = 0;

	while (qset->ntds) {
		struct whc_qtd *td;
		int t;

		t = qset->td_start;
		td = &qset->qtd[qset->td_start];
		status = le32_to_cpu(td->status);

		/*
		 * Nothing to do with a still active qTD.
		 */
		if (status & QTD_STS_ACTIVE)
			break;

		if (status & QTD_STS_HALTED) {
			/* Ug, an error. */
			process_halted_qtd(whc, qset, td);
			goto done;
		}

		/* Mmm, a completed qTD. */
		process_inactive_qtd(whc, qset, td);
	}

	update |= qset_add_qtds(whc, qset);

done:
	/*
	 * Remove this qset from the ASL if requested, but only if has
	 * no qTDs.
	 */
	if (qset->remove && qset->ntds == 0) {
		asl_qset_remove(whc, qset);
		update |= WHC_UPDATE_REMOVED;
	}
	return update;
}

void asl_start(struct whc *whc)
{
	struct whc_qset *qset;

	qset = list_first_entry(&whc->async_list, struct whc_qset, list_node);

	le_writeq(qset->qset_dma | QH_LINK_NTDS(8), whc->base + WUSBASYNCLISTADDR);

	whc_write_wusbcmd(whc, WUSBCMD_ASYNC_EN, WUSBCMD_ASYNC_EN);
	whci_wait_for(&whc->umc->dev, whc->base + WUSBSTS,
		      WUSBSTS_ASYNC_SCHED, WUSBSTS_ASYNC_SCHED,
		      1000, "start ASL");
}

void asl_stop(struct whc *whc)
{
	whc_write_wusbcmd(whc, WUSBCMD_ASYNC_EN, 0);
	whci_wait_for(&whc->umc->dev, whc->base + WUSBSTS,
		      WUSBSTS_ASYNC_SCHED, 0,
		      1000, "stop ASL");
}

/**
 * asl_update - request an ASL update and wait for the hardware to be synced
 * @whc: the WHCI HC
 * @wusbcmd: WUSBCMD value to start the update.
 *
 * If the WUSB HC is inactive (i.e., the ASL is stopped) then the
 * update must be skipped as the hardware may not respond to update
 * requests.
 */
void asl_update(struct whc *whc, uint32_t wusbcmd)
{
	struct wusbhc *wusbhc = &whc->wusbhc;
	long t;

	mutex_lock(&wusbhc->mutex);
	if (wusbhc->active) {
		whc_write_wusbcmd(whc, wusbcmd, wusbcmd);
		t = wait_event_timeout(
			whc->async_list_wq,
			(le_readl(whc->base + WUSBCMD) & WUSBCMD_ASYNC_UPDATED) == 0,
			msecs_to_jiffies(1000));
		if (t == 0)
			whc_hw_error(whc, "ASL update timeout");
	}
	mutex_unlock(&wusbhc->mutex);
}

/**
 * scan_async_work - scan the ASL for qsets to process.
 *
 * Process each qset in the ASL in turn and then signal the WHC that
 * the ASL has been updated.
 *
 * Then start, stop or update the asynchronous schedule as required.
 */
void scan_async_work(struct work_struct *work)
{
	struct whc *whc = container_of(work, struct whc, async_work);
	struct whc_qset *qset, *t;
	enum whc_update update = 0;

	spin_lock_irq(&whc->lock);

	/*
	 * Transerve the software list backwards so new qsets can be
	 * safely inserted into the ASL without making it non-circular.
	 */
	list_for_each_entry_safe_reverse(qset, t, &whc->async_list, list_node) {
		if (!qset->in_hw_list) {
			asl_qset_insert(whc, qset);
			update |= WHC_UPDATE_ADDED;
		}

		update |= process_qset(whc, qset);
	}

	spin_unlock_irq(&whc->lock);

	if (update) {
		uint32_t wusbcmd = WUSBCMD_ASYNC_UPDATED | WUSBCMD_ASYNC_SYNCED_DB;
		if (update & WHC_UPDATE_REMOVED)
			wusbcmd |= WUSBCMD_ASYNC_QSET_RM;
		asl_update(whc, wusbcmd);
	}

	/*
	 * Now that the ASL is updated, complete the removal of any
	 * removed qsets.
	 */
	spin_lock_irq(&whc->lock);

	list_for_each_entry_safe(qset, t, &whc->async_removed_list, list_node) {
		qset_remove_complete(whc, qset);
	}

	spin_unlock_irq(&whc->lock);
}

/**
 * asl_urb_enqueue - queue an URB onto the asynchronous list (ASL).
 * @whc: the WHCI host controller
 * @urb: the URB to enqueue
 * @mem_flags: flags for any memory allocations
 *
 * The qset for the endpoint is obtained and the urb queued on to it.
 *
 * Work is scheduled to update the hardware's view of the ASL.
 */
int asl_urb_enqueue(struct whc *whc, struct urb *urb, gfp_t mem_flags)
{
	struct whc_qset *qset;
	int err;
	unsigned long flags;

	spin_lock_irqsave(&whc->lock, flags);

	qset = get_qset(whc, urb, GFP_ATOMIC);
	if (qset == NULL)
		err = -ENOMEM;
	else
		err = qset_add_urb(whc, qset, urb, GFP_ATOMIC);
	if (!err) {
		usb_hcd_link_urb_to_ep(&whc->wusbhc.usb_hcd, urb);
		if (!qset->in_sw_list)
			asl_qset_insert_begin(whc, qset);
	}

	spin_unlock_irqrestore(&whc->lock, flags);

	if (!err)
		queue_work(whc->workqueue, &whc->async_work);

	return 0;
}

/**
 * asl_urb_dequeue - remove an URB (qset) from the async list.
 * @whc: the WHCI host controller
 * @urb: the URB to dequeue
 * @status: the current status of the URB
 *
 * URBs that do yet have qTDs can simply be removed from the software
 * queue, otherwise the qset must be removed from the ASL so the qTDs
 * can be removed.
 */
int asl_urb_dequeue(struct whc *whc, struct urb *urb, int status)
{
	struct whc_urb *wurb = urb->hcpriv;
	struct whc_qset *qset = wurb->qset;
	struct whc_std *std, *t;
	int ret;
	unsigned long flags;

	spin_lock_irqsave(&whc->lock, flags);

	ret = usb_hcd_check_unlink_urb(&whc->wusbhc.usb_hcd, urb, status);
	if (ret < 0)
		goto out;

	list_for_each_entry_safe(std, t, &qset->stds, list_node) {
		if (std->urb == urb)
			qset_free_std(whc, std);
		else
			std->qtd = NULL; /* so this std is re-added when the qset is */
	}

	asl_qset_remove(whc, qset);
	wurb->status = status;
	wurb->is_async = true;
	queue_work(whc->workqueue, &wurb->dequeue_work);

out:
	spin_unlock_irqrestore(&whc->lock, flags);

	return ret;
}

/**
 * asl_qset_delete - delete a qset from the ASL
 */
void asl_qset_delete(struct whc *whc, struct whc_qset *qset)
{
	qset->remove = 1;
	queue_work(whc->workqueue, &whc->async_work);
	qset_delete(whc, qset);
}

/**
 * asl_init - initialize the asynchronous schedule list
 *
 * A dummy qset with no qTDs is added to the ASL to simplify removing
 * qsets (no need to stop the ASL when the last qset is removed).
 */
int asl_init(struct whc *whc)
{
	struct whc_qset *qset;

	qset = qset_alloc(whc, GFP_KERNEL);
	if (qset == NULL)
		return -ENOMEM;

	asl_qset_insert_begin(whc, qset);
	asl_qset_insert(whc, qset);

	return 0;
}

/**
 * asl_clean_up - free ASL resources
 *
 * The ASL is stopped and empty except for the dummy qset.
 */
void asl_clean_up(struct whc *whc)
{
	struct whc_qset *qset;

	if (!list_empty(&whc->async_list)) {
		qset = list_first_entry(&whc->async_list, struct whc_qset, list_node);
		list_del(&qset->list_node);
		qset_free(whc, qset);
	}
}

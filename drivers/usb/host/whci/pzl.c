/*
 * Wireless Host Controller (WHC) periodic schedule management.
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

static void update_pzl_pointers(struct whc *whc, int period, u64 addr)
{
	switch (period) {
	case 0:
		whc_qset_set_link_ptr(&whc->pz_list[0], addr);
		whc_qset_set_link_ptr(&whc->pz_list[2], addr);
		whc_qset_set_link_ptr(&whc->pz_list[4], addr);
		whc_qset_set_link_ptr(&whc->pz_list[6], addr);
		whc_qset_set_link_ptr(&whc->pz_list[8], addr);
		whc_qset_set_link_ptr(&whc->pz_list[10], addr);
		whc_qset_set_link_ptr(&whc->pz_list[12], addr);
		whc_qset_set_link_ptr(&whc->pz_list[14], addr);
		break;
	case 1:
		whc_qset_set_link_ptr(&whc->pz_list[1], addr);
		whc_qset_set_link_ptr(&whc->pz_list[5], addr);
		whc_qset_set_link_ptr(&whc->pz_list[9], addr);
		whc_qset_set_link_ptr(&whc->pz_list[13], addr);
		break;
	case 2:
		whc_qset_set_link_ptr(&whc->pz_list[3], addr);
		whc_qset_set_link_ptr(&whc->pz_list[11], addr);
		break;
	case 3:
		whc_qset_set_link_ptr(&whc->pz_list[7], addr);
		break;
	case 4:
		whc_qset_set_link_ptr(&whc->pz_list[15], addr);
		break;
	}
}

/*
 * Return the 'period' to use for this qset.  The minimum interval for
 * the endpoint is used so whatever urbs are submitted the device is
 * polled often enough.
 */
static int qset_get_period(struct whc *whc, struct whc_qset *qset)
{
	uint8_t bInterval = qset->ep->desc.bInterval;

	if (bInterval < 6)
		bInterval = 6;
	if (bInterval > 10)
		bInterval = 10;
	return bInterval - 6;
}

static void qset_insert_in_sw_list(struct whc *whc, struct whc_qset *qset)
{
	int period;

	period = qset_get_period(whc, qset);

	qset_clear(whc, qset);
	list_move(&qset->list_node, &whc->periodic_list[period]);
	qset->in_sw_list = true;
}

static void pzl_qset_remove(struct whc *whc, struct whc_qset *qset)
{
	list_move(&qset->list_node, &whc->periodic_removed_list);
	qset->in_hw_list = false;
	qset->in_sw_list = false;
}

/**
 * pzl_process_qset - process any recently inactivated or halted qTDs
 * in a qset.
 *
 * After inactive qTDs are removed, new qTDs can be added if the
 * urb queue still contains URBs.
 *
 * Returns the schedule updates required.
 */
static enum whc_update pzl_process_qset(struct whc *whc, struct whc_qset *qset)
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

	if (!qset->remove)
		update |= qset_add_qtds(whc, qset);

done:
	/*
	 * If there are no qTDs in this qset, remove it from the PZL.
	 */
	if (qset->remove && qset->ntds == 0) {
		pzl_qset_remove(whc, qset);
		update |= WHC_UPDATE_REMOVED;
	}

	return update;
}

/**
 * pzl_start - start the periodic schedule
 * @whc: the WHCI host controller
 *
 * The PZL must be valid (e.g., all entries in the list should have
 * the T bit set).
 */
void pzl_start(struct whc *whc)
{
	le_writeq(whc->pz_list_dma, whc->base + WUSBPERIODICLISTBASE);

	whc_write_wusbcmd(whc, WUSBCMD_PERIODIC_EN, WUSBCMD_PERIODIC_EN);
	whci_wait_for(&whc->umc->dev, whc->base + WUSBSTS,
		      WUSBSTS_PERIODIC_SCHED, WUSBSTS_PERIODIC_SCHED,
		      1000, "start PZL");
}

/**
 * pzl_stop - stop the periodic schedule
 * @whc: the WHCI host controller
 */
void pzl_stop(struct whc *whc)
{
	whc_write_wusbcmd(whc, WUSBCMD_PERIODIC_EN, 0);
	whci_wait_for(&whc->umc->dev, whc->base + WUSBSTS,
		      WUSBSTS_PERIODIC_SCHED, 0,
		      1000, "stop PZL");
}

/**
 * pzl_update - request a PZL update and wait for the hardware to be synced
 * @whc: the WHCI HC
 * @wusbcmd: WUSBCMD value to start the update.
 *
 * If the WUSB HC is inactive (i.e., the PZL is stopped) then the
 * update must be skipped as the hardware may not respond to update
 * requests.
 */
void pzl_update(struct whc *whc, uint32_t wusbcmd)
{
	struct wusbhc *wusbhc = &whc->wusbhc;
	long t;

	mutex_lock(&wusbhc->mutex);
	if (wusbhc->active) {
		whc_write_wusbcmd(whc, wusbcmd, wusbcmd);
		t = wait_event_timeout(
			whc->periodic_list_wq,
			(le_readl(whc->base + WUSBCMD) & WUSBCMD_PERIODIC_UPDATED) == 0,
			msecs_to_jiffies(1000));
		if (t == 0)
			whc_hw_error(whc, "PZL update timeout");
	}
	mutex_unlock(&wusbhc->mutex);
}

static void update_pzl_hw_view(struct whc *whc)
{
	struct whc_qset *qset, *t;
	int period;
	u64 tmp_qh = 0;

	for (period = 0; period < 5; period++) {
		list_for_each_entry_safe(qset, t, &whc->periodic_list[period], list_node) {
			whc_qset_set_link_ptr(&qset->qh.link, tmp_qh);
			tmp_qh = qset->qset_dma;
			qset->in_hw_list = true;
		}
		update_pzl_pointers(whc, period, tmp_qh);
	}
}

/**
 * scan_periodic_work - scan the PZL for qsets to process.
 *
 * Process each qset in the PZL in turn and then signal the WHC that
 * the PZL has been updated.
 *
 * Then start, stop or update the periodic schedule as required.
 */
void scan_periodic_work(struct work_struct *work)
{
	struct whc *whc = container_of(work, struct whc, periodic_work);
	struct whc_qset *qset, *t;
	enum whc_update update = 0;
	int period;

	spin_lock_irq(&whc->lock);

	for (period = 4; period >= 0; period--) {
		list_for_each_entry_safe(qset, t, &whc->periodic_list[period], list_node) {
			if (!qset->in_hw_list)
				update |= WHC_UPDATE_ADDED;
			update |= pzl_process_qset(whc, qset);
		}
	}

	if (update & (WHC_UPDATE_ADDED | WHC_UPDATE_REMOVED))
		update_pzl_hw_view(whc);

	spin_unlock_irq(&whc->lock);

	if (update) {
		uint32_t wusbcmd = WUSBCMD_PERIODIC_UPDATED | WUSBCMD_PERIODIC_SYNCED_DB;
		if (update & WHC_UPDATE_REMOVED)
			wusbcmd |= WUSBCMD_PERIODIC_QSET_RM;
		pzl_update(whc, wusbcmd);
	}

	/*
	 * Now that the PZL is updated, complete the removal of any
	 * removed qsets.
	 */
	spin_lock_irq(&whc->lock);

	list_for_each_entry_safe(qset, t, &whc->periodic_removed_list, list_node) {
		qset_remove_complete(whc, qset);
	}

	spin_unlock_irq(&whc->lock);
}

/**
 * pzl_urb_enqueue - queue an URB onto the periodic list (PZL)
 * @whc: the WHCI host controller
 * @urb: the URB to enqueue
 * @mem_flags: flags for any memory allocations
 *
 * The qset for the endpoint is obtained and the urb queued on to it.
 *
 * Work is scheduled to update the hardware's view of the PZL.
 */
int pzl_urb_enqueue(struct whc *whc, struct urb *urb, gfp_t mem_flags)
{
	struct whc_qset *qset;
	int err;
	unsigned long flags;

	spin_lock_irqsave(&whc->lock, flags);

	err = usb_hcd_link_urb_to_ep(&whc->wusbhc.usb_hcd, urb);
	if (err < 0) {
		spin_unlock_irqrestore(&whc->lock, flags);
		return err;
	}

	qset = get_qset(whc, urb, GFP_ATOMIC);
	if (qset == NULL)
		err = -ENOMEM;
	else
		err = qset_add_urb(whc, qset, urb, GFP_ATOMIC);
	if (!err) {
		if (!qset->in_sw_list)
			qset_insert_in_sw_list(whc, qset);
	} else
		usb_hcd_unlink_urb_from_ep(&whc->wusbhc.usb_hcd, urb);

	spin_unlock_irqrestore(&whc->lock, flags);

	if (!err)
		queue_work(whc->workqueue, &whc->periodic_work);

	return err;
}

/**
 * pzl_urb_dequeue - remove an URB (qset) from the periodic list
 * @whc: the WHCI host controller
 * @urb: the URB to dequeue
 * @status: the current status of the URB
 *
 * URBs that do yet have qTDs can simply be removed from the software
 * queue, otherwise the qset must be removed so the qTDs can be safely
 * removed.
 */
int pzl_urb_dequeue(struct whc *whc, struct urb *urb, int status)
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

	pzl_qset_remove(whc, qset);
	wurb->status = status;
	wurb->is_async = false;
	queue_work(whc->workqueue, &wurb->dequeue_work);

out:
	spin_unlock_irqrestore(&whc->lock, flags);

	return ret;
}

/**
 * pzl_qset_delete - delete a qset from the PZL
 */
void pzl_qset_delete(struct whc *whc, struct whc_qset *qset)
{
	qset->remove = 1;
	queue_work(whc->workqueue, &whc->periodic_work);
	qset_delete(whc, qset);
}

/**
 * pzl_init - initialize the periodic zone list
 * @whc: the WHCI host controller
 */
int pzl_init(struct whc *whc)
{
	int i;

	whc->pz_list = dma_alloc_coherent(&whc->umc->dev, sizeof(u64) * 16,
					  &whc->pz_list_dma, GFP_KERNEL);
	if (whc->pz_list == NULL)
		return -ENOMEM;

	/* Set T bit on all elements in PZL. */
	for (i = 0; i < 16; i++)
		whc->pz_list[i] = cpu_to_le64(QH_LINK_NTDS(8) | QH_LINK_T);

	le_writeq(whc->pz_list_dma, whc->base + WUSBPERIODICLISTBASE);

	return 0;
}

/**
 * pzl_clean_up - free PZL resources
 * @whc: the WHCI host controller
 *
 * The PZL is stopped and empty.
 */
void pzl_clean_up(struct whc *whc)
{
	if (whc->pz_list)
		dma_free_coherent(&whc->umc->dev,  sizeof(u64) * 16, whc->pz_list,
				  whc->pz_list_dma);
}

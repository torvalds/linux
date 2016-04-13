/*
 * hcd_ddma.c - DesignWare HS OTG Controller descriptor DMA routines
 *
 * Copyright (C) 2004-2013 Synopsys, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The names of the above-listed copyright holders may not be used
 *    to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 * ALTERNATIVELY, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") as published by the Free Software
 * Foundation; either version 2 of the License, or (at your option) any
 * later version.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
 * IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * This file contains the Descriptor DMA implementation for Host mode
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/dma-mapping.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/usb.h>

#include <linux/usb/hcd.h>
#include <linux/usb/ch11.h>

#include "core.h"
#include "hcd.h"

static u16 dwc2_frame_list_idx(u16 frame)
{
	return frame & (FRLISTEN_64_SIZE - 1);
}

static u16 dwc2_desclist_idx_inc(u16 idx, u16 inc, u8 speed)
{
	return (idx + inc) &
		((speed == USB_SPEED_HIGH ? MAX_DMA_DESC_NUM_HS_ISOC :
		  MAX_DMA_DESC_NUM_GENERIC) - 1);
}

static u16 dwc2_desclist_idx_dec(u16 idx, u16 inc, u8 speed)
{
	return (idx - inc) &
		((speed == USB_SPEED_HIGH ? MAX_DMA_DESC_NUM_HS_ISOC :
		  MAX_DMA_DESC_NUM_GENERIC) - 1);
}

static u16 dwc2_max_desc_num(struct dwc2_qh *qh)
{
	return (qh->ep_type == USB_ENDPOINT_XFER_ISOC &&
		qh->dev_speed == USB_SPEED_HIGH) ?
		MAX_DMA_DESC_NUM_HS_ISOC : MAX_DMA_DESC_NUM_GENERIC;
}

static u16 dwc2_frame_incr_val(struct dwc2_qh *qh)
{
	return qh->dev_speed == USB_SPEED_HIGH ?
	       (qh->host_interval + 8 - 1) / 8 : qh->host_interval;
}

static int dwc2_desc_list_alloc(struct dwc2_hsotg *hsotg, struct dwc2_qh *qh,
				gfp_t flags)
{
	struct kmem_cache *desc_cache;

	if (qh->ep_type == USB_ENDPOINT_XFER_ISOC
	    && qh->dev_speed == USB_SPEED_HIGH)
		desc_cache = hsotg->desc_hsisoc_cache;
	else
		desc_cache = hsotg->desc_gen_cache;

	qh->desc_list_sz = sizeof(struct dwc2_hcd_dma_desc) *
						dwc2_max_desc_num(qh);

	qh->desc_list = kmem_cache_zalloc(desc_cache, flags | GFP_DMA);
	if (!qh->desc_list)
		return -ENOMEM;

	qh->desc_list_dma = dma_map_single(hsotg->dev, qh->desc_list,
					   qh->desc_list_sz,
					   DMA_TO_DEVICE);

	qh->n_bytes = kzalloc(sizeof(u32) * dwc2_max_desc_num(qh), flags);
	if (!qh->n_bytes) {
		dma_unmap_single(hsotg->dev, qh->desc_list_dma,
				 qh->desc_list_sz,
				 DMA_FROM_DEVICE);
		kmem_cache_free(desc_cache, qh->desc_list);
		qh->desc_list = NULL;
		return -ENOMEM;
	}

	return 0;
}

static void dwc2_desc_list_free(struct dwc2_hsotg *hsotg, struct dwc2_qh *qh)
{
	struct kmem_cache *desc_cache;

	if (qh->ep_type == USB_ENDPOINT_XFER_ISOC
	    && qh->dev_speed == USB_SPEED_HIGH)
		desc_cache = hsotg->desc_hsisoc_cache;
	else
		desc_cache = hsotg->desc_gen_cache;

	if (qh->desc_list) {
		dma_unmap_single(hsotg->dev, qh->desc_list_dma,
				 qh->desc_list_sz, DMA_FROM_DEVICE);
		kmem_cache_free(desc_cache, qh->desc_list);
		qh->desc_list = NULL;
	}

	kfree(qh->n_bytes);
	qh->n_bytes = NULL;
}

static int dwc2_frame_list_alloc(struct dwc2_hsotg *hsotg, gfp_t mem_flags)
{
	if (hsotg->frame_list)
		return 0;

	hsotg->frame_list_sz = 4 * FRLISTEN_64_SIZE;
	hsotg->frame_list = kzalloc(hsotg->frame_list_sz, GFP_ATOMIC | GFP_DMA);
	if (!hsotg->frame_list)
		return -ENOMEM;

	hsotg->frame_list_dma = dma_map_single(hsotg->dev, hsotg->frame_list,
					       hsotg->frame_list_sz,
					       DMA_TO_DEVICE);

	return 0;
}

static void dwc2_frame_list_free(struct dwc2_hsotg *hsotg)
{
	unsigned long flags;

	spin_lock_irqsave(&hsotg->lock, flags);

	if (!hsotg->frame_list) {
		spin_unlock_irqrestore(&hsotg->lock, flags);
		return;
	}

	dma_unmap_single(hsotg->dev, hsotg->frame_list_dma,
			 hsotg->frame_list_sz, DMA_FROM_DEVICE);

	kfree(hsotg->frame_list);
	hsotg->frame_list = NULL;

	spin_unlock_irqrestore(&hsotg->lock, flags);

}

static void dwc2_per_sched_enable(struct dwc2_hsotg *hsotg, u32 fr_list_en)
{
	u32 hcfg;
	unsigned long flags;

	spin_lock_irqsave(&hsotg->lock, flags);

	hcfg = dwc2_readl(hsotg->regs + HCFG);
	if (hcfg & HCFG_PERSCHEDENA) {
		/* already enabled */
		spin_unlock_irqrestore(&hsotg->lock, flags);
		return;
	}

	dwc2_writel(hsotg->frame_list_dma, hsotg->regs + HFLBADDR);

	hcfg &= ~HCFG_FRLISTEN_MASK;
	hcfg |= fr_list_en | HCFG_PERSCHEDENA;
	dev_vdbg(hsotg->dev, "Enabling Periodic schedule\n");
	dwc2_writel(hcfg, hsotg->regs + HCFG);

	spin_unlock_irqrestore(&hsotg->lock, flags);
}

static void dwc2_per_sched_disable(struct dwc2_hsotg *hsotg)
{
	u32 hcfg;
	unsigned long flags;

	spin_lock_irqsave(&hsotg->lock, flags);

	hcfg = dwc2_readl(hsotg->regs + HCFG);
	if (!(hcfg & HCFG_PERSCHEDENA)) {
		/* already disabled */
		spin_unlock_irqrestore(&hsotg->lock, flags);
		return;
	}

	hcfg &= ~HCFG_PERSCHEDENA;
	dev_vdbg(hsotg->dev, "Disabling Periodic schedule\n");
	dwc2_writel(hcfg, hsotg->regs + HCFG);

	spin_unlock_irqrestore(&hsotg->lock, flags);
}

/*
 * Activates/Deactivates FrameList entries for the channel based on endpoint
 * servicing period
 */
static void dwc2_update_frame_list(struct dwc2_hsotg *hsotg, struct dwc2_qh *qh,
				   int enable)
{
	struct dwc2_host_chan *chan;
	u16 i, j, inc;

	if (!hsotg) {
		pr_err("hsotg = %p\n", hsotg);
		return;
	}

	if (!qh->channel) {
		dev_err(hsotg->dev, "qh->channel = %p\n", qh->channel);
		return;
	}

	if (!hsotg->frame_list) {
		dev_err(hsotg->dev, "hsotg->frame_list = %p\n",
			hsotg->frame_list);
		return;
	}

	chan = qh->channel;
	inc = dwc2_frame_incr_val(qh);
	if (qh->ep_type == USB_ENDPOINT_XFER_ISOC)
		i = dwc2_frame_list_idx(qh->next_active_frame);
	else
		i = 0;

	j = i;
	do {
		if (enable)
			hsotg->frame_list[j] |= 1 << chan->hc_num;
		else
			hsotg->frame_list[j] &= ~(1 << chan->hc_num);
		j = (j + inc) & (FRLISTEN_64_SIZE - 1);
	} while (j != i);

	/*
	 * Sync frame list since controller will access it if periodic
	 * channel is currently enabled.
	 */
	dma_sync_single_for_device(hsotg->dev,
				   hsotg->frame_list_dma,
				   hsotg->frame_list_sz,
				   DMA_TO_DEVICE);

	if (!enable)
		return;

	chan->schinfo = 0;
	if (chan->speed == USB_SPEED_HIGH && qh->host_interval) {
		j = 1;
		/* TODO - check this */
		inc = (8 + qh->host_interval - 1) / qh->host_interval;
		for (i = 0; i < inc; i++) {
			chan->schinfo |= j;
			j = j << qh->host_interval;
		}
	} else {
		chan->schinfo = 0xff;
	}
}

static void dwc2_release_channel_ddma(struct dwc2_hsotg *hsotg,
				      struct dwc2_qh *qh)
{
	struct dwc2_host_chan *chan = qh->channel;

	if (dwc2_qh_is_non_per(qh)) {
		if (hsotg->core_params->uframe_sched > 0)
			hsotg->available_host_channels++;
		else
			hsotg->non_periodic_channels--;
	} else {
		dwc2_update_frame_list(hsotg, qh, 0);
		hsotg->available_host_channels++;
	}

	/*
	 * The condition is added to prevent double cleanup try in case of
	 * device disconnect. See channel cleanup in dwc2_hcd_disconnect().
	 */
	if (chan->qh) {
		if (!list_empty(&chan->hc_list_entry))
			list_del(&chan->hc_list_entry);
		dwc2_hc_cleanup(hsotg, chan);
		list_add_tail(&chan->hc_list_entry, &hsotg->free_hc_list);
		chan->qh = NULL;
	}

	qh->channel = NULL;
	qh->ntd = 0;

	if (qh->desc_list)
		memset(qh->desc_list, 0, sizeof(struct dwc2_hcd_dma_desc) *
		       dwc2_max_desc_num(qh));
}

/**
 * dwc2_hcd_qh_init_ddma() - Initializes a QH structure's Descriptor DMA
 * related members
 *
 * @hsotg: The HCD state structure for the DWC OTG controller
 * @qh:    The QH to init
 *
 * Return: 0 if successful, negative error code otherwise
 *
 * Allocates memory for the descriptor list. For the first periodic QH,
 * allocates memory for the FrameList and enables periodic scheduling.
 */
int dwc2_hcd_qh_init_ddma(struct dwc2_hsotg *hsotg, struct dwc2_qh *qh,
			  gfp_t mem_flags)
{
	int retval;

	if (qh->do_split) {
		dev_err(hsotg->dev,
			"SPLIT Transfers are not supported in Descriptor DMA mode.\n");
		retval = -EINVAL;
		goto err0;
	}

	retval = dwc2_desc_list_alloc(hsotg, qh, mem_flags);
	if (retval)
		goto err0;

	if (qh->ep_type == USB_ENDPOINT_XFER_ISOC ||
	    qh->ep_type == USB_ENDPOINT_XFER_INT) {
		if (!hsotg->frame_list) {
			retval = dwc2_frame_list_alloc(hsotg, mem_flags);
			if (retval)
				goto err1;
			/* Enable periodic schedule on first periodic QH */
			dwc2_per_sched_enable(hsotg, HCFG_FRLISTEN_64);
		}
	}

	qh->ntd = 0;
	return 0;

err1:
	dwc2_desc_list_free(hsotg, qh);
err0:
	return retval;
}

/**
 * dwc2_hcd_qh_free_ddma() - Frees a QH structure's Descriptor DMA related
 * members
 *
 * @hsotg: The HCD state structure for the DWC OTG controller
 * @qh:    The QH to free
 *
 * Frees descriptor list memory associated with the QH. If QH is periodic and
 * the last, frees FrameList memory and disables periodic scheduling.
 */
void dwc2_hcd_qh_free_ddma(struct dwc2_hsotg *hsotg, struct dwc2_qh *qh)
{
	unsigned long flags;

	dwc2_desc_list_free(hsotg, qh);

	/*
	 * Channel still assigned due to some reasons.
	 * Seen on Isoc URB dequeue. Channel halted but no subsequent
	 * ChHalted interrupt to release the channel. Afterwards
	 * when it comes here from endpoint disable routine
	 * channel remains assigned.
	 */
	spin_lock_irqsave(&hsotg->lock, flags);
	if (qh->channel)
		dwc2_release_channel_ddma(hsotg, qh);
	spin_unlock_irqrestore(&hsotg->lock, flags);

	if ((qh->ep_type == USB_ENDPOINT_XFER_ISOC ||
	     qh->ep_type == USB_ENDPOINT_XFER_INT) &&
	    (hsotg->core_params->uframe_sched > 0 ||
	     !hsotg->periodic_channels) && hsotg->frame_list) {
		dwc2_per_sched_disable(hsotg);
		dwc2_frame_list_free(hsotg);
	}
}

static u8 dwc2_frame_to_desc_idx(struct dwc2_qh *qh, u16 frame_idx)
{
	if (qh->dev_speed == USB_SPEED_HIGH)
		/* Descriptor set (8 descriptors) index which is 8-aligned */
		return (frame_idx & ((MAX_DMA_DESC_NUM_HS_ISOC / 8) - 1)) * 8;
	else
		return frame_idx & (MAX_DMA_DESC_NUM_GENERIC - 1);
}

/*
 * Determine starting frame for Isochronous transfer.
 * Few frames skipped to prevent race condition with HC.
 */
static u16 dwc2_calc_starting_frame(struct dwc2_hsotg *hsotg,
				    struct dwc2_qh *qh, u16 *skip_frames)
{
	u16 frame;

	hsotg->frame_number = dwc2_hcd_get_frame_number(hsotg);

	/*
	 * next_active_frame is always frame number (not uFrame) both in FS
	 * and HS!
	 */

	/*
	 * skip_frames is used to limit activated descriptors number
	 * to avoid the situation when HC services the last activated
	 * descriptor firstly.
	 * Example for FS:
	 * Current frame is 1, scheduled frame is 3. Since HC always fetches
	 * the descriptor corresponding to curr_frame+1, the descriptor
	 * corresponding to frame 2 will be fetched. If the number of
	 * descriptors is max=64 (or greather) the list will be fully programmed
	 * with Active descriptors and it is possible case (rare) that the
	 * latest descriptor(considering rollback) corresponding to frame 2 will
	 * be serviced first. HS case is more probable because, in fact, up to
	 * 11 uframes (16 in the code) may be skipped.
	 */
	if (qh->dev_speed == USB_SPEED_HIGH) {
		/*
		 * Consider uframe counter also, to start xfer asap. If half of
		 * the frame elapsed skip 2 frames otherwise just 1 frame.
		 * Starting descriptor index must be 8-aligned, so if the
		 * current frame is near to complete the next one is skipped as
		 * well.
		 */
		if (dwc2_micro_frame_num(hsotg->frame_number) >= 5) {
			*skip_frames = 2 * 8;
			frame = dwc2_frame_num_inc(hsotg->frame_number,
						   *skip_frames);
		} else {
			*skip_frames = 1 * 8;
			frame = dwc2_frame_num_inc(hsotg->frame_number,
						   *skip_frames);
		}

		frame = dwc2_full_frame_num(frame);
	} else {
		/*
		 * Two frames are skipped for FS - the current and the next.
		 * But for descriptor programming, 1 frame (descriptor) is
		 * enough, see example above.
		 */
		*skip_frames = 1;
		frame = dwc2_frame_num_inc(hsotg->frame_number, 2);
	}

	return frame;
}

/*
 * Calculate initial descriptor index for isochronous transfer based on
 * scheduled frame
 */
static u16 dwc2_recalc_initial_desc_idx(struct dwc2_hsotg *hsotg,
					struct dwc2_qh *qh)
{
	u16 frame, fr_idx, fr_idx_tmp, skip_frames;

	/*
	 * With current ISOC processing algorithm the channel is being released
	 * when no more QTDs in the list (qh->ntd == 0). Thus this function is
	 * called only when qh->ntd == 0 and qh->channel == 0.
	 *
	 * So qh->channel != NULL branch is not used and just not removed from
	 * the source file. It is required for another possible approach which
	 * is, do not disable and release the channel when ISOC session
	 * completed, just move QH to inactive schedule until new QTD arrives.
	 * On new QTD, the QH moved back to 'ready' schedule, starting frame and
	 * therefore starting desc_index are recalculated. In this case channel
	 * is released only on ep_disable.
	 */

	/*
	 * Calculate starting descriptor index. For INTERRUPT endpoint it is
	 * always 0.
	 */
	if (qh->channel) {
		frame = dwc2_calc_starting_frame(hsotg, qh, &skip_frames);
		/*
		 * Calculate initial descriptor index based on FrameList current
		 * bitmap and servicing period
		 */
		fr_idx_tmp = dwc2_frame_list_idx(frame);
		fr_idx = (FRLISTEN_64_SIZE +
			  dwc2_frame_list_idx(qh->next_active_frame) -
			  fr_idx_tmp) % dwc2_frame_incr_val(qh);
		fr_idx = (fr_idx + fr_idx_tmp) % FRLISTEN_64_SIZE;
	} else {
		qh->next_active_frame = dwc2_calc_starting_frame(hsotg, qh,
							   &skip_frames);
		fr_idx = dwc2_frame_list_idx(qh->next_active_frame);
	}

	qh->td_first = qh->td_last = dwc2_frame_to_desc_idx(qh, fr_idx);

	return skip_frames;
}

#define ISOC_URB_GIVEBACK_ASAP

#define MAX_ISOC_XFER_SIZE_FS	1023
#define MAX_ISOC_XFER_SIZE_HS	3072
#define DESCNUM_THRESHOLD	4

static void dwc2_fill_host_isoc_dma_desc(struct dwc2_hsotg *hsotg,
					 struct dwc2_qtd *qtd,
					 struct dwc2_qh *qh, u32 max_xfer_size,
					 u16 idx)
{
	struct dwc2_hcd_dma_desc *dma_desc = &qh->desc_list[idx];
	struct dwc2_hcd_iso_packet_desc *frame_desc;

	memset(dma_desc, 0, sizeof(*dma_desc));
	frame_desc = &qtd->urb->iso_descs[qtd->isoc_frame_index_last];

	if (frame_desc->length > max_xfer_size)
		qh->n_bytes[idx] = max_xfer_size;
	else
		qh->n_bytes[idx] = frame_desc->length;

	dma_desc->buf = (u32)(qtd->urb->dma + frame_desc->offset);
	dma_desc->status = qh->n_bytes[idx] << HOST_DMA_ISOC_NBYTES_SHIFT &
			   HOST_DMA_ISOC_NBYTES_MASK;

	/* Set active bit */
	dma_desc->status |= HOST_DMA_A;

	qh->ntd++;
	qtd->isoc_frame_index_last++;

#ifdef ISOC_URB_GIVEBACK_ASAP
	/* Set IOC for each descriptor corresponding to last frame of URB */
	if (qtd->isoc_frame_index_last == qtd->urb->packet_count)
		dma_desc->status |= HOST_DMA_IOC;
#endif

	dma_sync_single_for_device(hsotg->dev,
			qh->desc_list_dma +
			(idx * sizeof(struct dwc2_hcd_dma_desc)),
			sizeof(struct dwc2_hcd_dma_desc),
			DMA_TO_DEVICE);
}

static void dwc2_init_isoc_dma_desc(struct dwc2_hsotg *hsotg,
				    struct dwc2_qh *qh, u16 skip_frames)
{
	struct dwc2_qtd *qtd;
	u32 max_xfer_size;
	u16 idx, inc, n_desc = 0, ntd_max = 0;
	u16 cur_idx;
	u16 next_idx;

	idx = qh->td_last;
	inc = qh->host_interval;
	hsotg->frame_number = dwc2_hcd_get_frame_number(hsotg);
	cur_idx = dwc2_frame_list_idx(hsotg->frame_number);
	next_idx = dwc2_desclist_idx_inc(qh->td_last, inc, qh->dev_speed);

	/*
	 * Ensure current frame number didn't overstep last scheduled
	 * descriptor. If it happens, the only way to recover is to move
	 * qh->td_last to current frame number + 1.
	 * So that next isoc descriptor will be scheduled on frame number + 1
	 * and not on a past frame.
	 */
	if (dwc2_frame_idx_num_gt(cur_idx, next_idx) || (cur_idx == next_idx)) {
		if (inc < 32) {
			dev_vdbg(hsotg->dev,
				 "current frame number overstep last descriptor\n");
			qh->td_last = dwc2_desclist_idx_inc(cur_idx, inc,
							    qh->dev_speed);
			idx = qh->td_last;
		}
	}

	if (qh->host_interval) {
		ntd_max = (dwc2_max_desc_num(qh) + qh->host_interval - 1) /
				qh->host_interval;
		if (skip_frames && !qh->channel)
			ntd_max -= skip_frames / qh->host_interval;
	}

	max_xfer_size = qh->dev_speed == USB_SPEED_HIGH ?
			MAX_ISOC_XFER_SIZE_HS : MAX_ISOC_XFER_SIZE_FS;

	list_for_each_entry(qtd, &qh->qtd_list, qtd_list_entry) {
		if (qtd->in_process &&
		    qtd->isoc_frame_index_last ==
		    qtd->urb->packet_count)
			continue;

		qtd->isoc_td_first = idx;
		while (qh->ntd < ntd_max && qtd->isoc_frame_index_last <
						qtd->urb->packet_count) {
			dwc2_fill_host_isoc_dma_desc(hsotg, qtd, qh,
						     max_xfer_size, idx);
			idx = dwc2_desclist_idx_inc(idx, inc, qh->dev_speed);
			n_desc++;
		}
		qtd->isoc_td_last = idx;
		qtd->in_process = 1;
	}

	qh->td_last = idx;

#ifdef ISOC_URB_GIVEBACK_ASAP
	/* Set IOC for last descriptor if descriptor list is full */
	if (qh->ntd == ntd_max) {
		idx = dwc2_desclist_idx_dec(qh->td_last, inc, qh->dev_speed);
		qh->desc_list[idx].status |= HOST_DMA_IOC;
		dma_sync_single_for_device(hsotg->dev,
					   qh->desc_list_dma + (idx *
					   sizeof(struct dwc2_hcd_dma_desc)),
					   sizeof(struct dwc2_hcd_dma_desc),
					   DMA_TO_DEVICE);
	}
#else
	/*
	 * Set IOC bit only for one descriptor. Always try to be ahead of HW
	 * processing, i.e. on IOC generation driver activates next descriptor
	 * but core continues to process descriptors following the one with IOC
	 * set.
	 */

	if (n_desc > DESCNUM_THRESHOLD)
		/*
		 * Move IOC "up". Required even if there is only one QTD
		 * in the list, because QTDs might continue to be queued,
		 * but during the activation it was only one queued.
		 * Actually more than one QTD might be in the list if this
		 * function called from XferCompletion - QTDs was queued during
		 * HW processing of the previous descriptor chunk.
		 */
		idx = dwc2_desclist_idx_dec(idx, inc * ((qh->ntd + 1) / 2),
					    qh->dev_speed);
	else
		/*
		 * Set the IOC for the latest descriptor if either number of
		 * descriptors is not greater than threshold or no more new
		 * descriptors activated
		 */
		idx = dwc2_desclist_idx_dec(qh->td_last, inc, qh->dev_speed);

	qh->desc_list[idx].status |= HOST_DMA_IOC;
	dma_sync_single_for_device(hsotg->dev,
				   qh->desc_list_dma +
				   (idx * sizeof(struct dwc2_hcd_dma_desc)),
				   sizeof(struct dwc2_hcd_dma_desc),
				   DMA_TO_DEVICE);
#endif
}

static void dwc2_fill_host_dma_desc(struct dwc2_hsotg *hsotg,
				    struct dwc2_host_chan *chan,
				    struct dwc2_qtd *qtd, struct dwc2_qh *qh,
				    int n_desc)
{
	struct dwc2_hcd_dma_desc *dma_desc = &qh->desc_list[n_desc];
	int len = chan->xfer_len;

	if (len > MAX_DMA_DESC_SIZE - (chan->max_packet - 1))
		len = MAX_DMA_DESC_SIZE - (chan->max_packet - 1);

	if (chan->ep_is_in) {
		int num_packets;

		if (len > 0 && chan->max_packet)
			num_packets = (len + chan->max_packet - 1)
					/ chan->max_packet;
		else
			/* Need 1 packet for transfer length of 0 */
			num_packets = 1;

		/* Always program an integral # of packets for IN transfers */
		len = num_packets * chan->max_packet;
	}

	dma_desc->status = len << HOST_DMA_NBYTES_SHIFT & HOST_DMA_NBYTES_MASK;
	qh->n_bytes[n_desc] = len;

	if (qh->ep_type == USB_ENDPOINT_XFER_CONTROL &&
	    qtd->control_phase == DWC2_CONTROL_SETUP)
		dma_desc->status |= HOST_DMA_SUP;

	dma_desc->buf = (u32)chan->xfer_dma;

	dma_sync_single_for_device(hsotg->dev,
				   qh->desc_list_dma +
				   (n_desc * sizeof(struct dwc2_hcd_dma_desc)),
				   sizeof(struct dwc2_hcd_dma_desc),
				   DMA_TO_DEVICE);

	/*
	 * Last (or only) descriptor of IN transfer with actual size less
	 * than MaxPacket
	 */
	if (len > chan->xfer_len) {
		chan->xfer_len = 0;
	} else {
		chan->xfer_dma += len;
		chan->xfer_len -= len;
	}
}

static void dwc2_init_non_isoc_dma_desc(struct dwc2_hsotg *hsotg,
					struct dwc2_qh *qh)
{
	struct dwc2_qtd *qtd;
	struct dwc2_host_chan *chan = qh->channel;
	int n_desc = 0;

	dev_vdbg(hsotg->dev, "%s(): qh=%p dma=%08lx len=%d\n", __func__, qh,
		 (unsigned long)chan->xfer_dma, chan->xfer_len);

	/*
	 * Start with chan->xfer_dma initialized in assign_and_init_hc(), then
	 * if SG transfer consists of multiple URBs, this pointer is re-assigned
	 * to the buffer of the currently processed QTD. For non-SG request
	 * there is always one QTD active.
	 */

	list_for_each_entry(qtd, &qh->qtd_list, qtd_list_entry) {
		dev_vdbg(hsotg->dev, "qtd=%p\n", qtd);

		if (n_desc) {
			/* SG request - more than 1 QTD */
			chan->xfer_dma = qtd->urb->dma +
					qtd->urb->actual_length;
			chan->xfer_len = qtd->urb->length -
					qtd->urb->actual_length;
			dev_vdbg(hsotg->dev, "buf=%08lx len=%d\n",
				 (unsigned long)chan->xfer_dma, chan->xfer_len);
		}

		qtd->n_desc = 0;
		do {
			if (n_desc > 1) {
				qh->desc_list[n_desc - 1].status |= HOST_DMA_A;
				dev_vdbg(hsotg->dev,
					 "set A bit in desc %d (%p)\n",
					 n_desc - 1,
					 &qh->desc_list[n_desc - 1]);
				dma_sync_single_for_device(hsotg->dev,
					qh->desc_list_dma +
					((n_desc - 1) *
					sizeof(struct dwc2_hcd_dma_desc)),
					sizeof(struct dwc2_hcd_dma_desc),
					DMA_TO_DEVICE);
			}
			dwc2_fill_host_dma_desc(hsotg, chan, qtd, qh, n_desc);
			dev_vdbg(hsotg->dev,
				 "desc %d (%p) buf=%08x status=%08x\n",
				 n_desc, &qh->desc_list[n_desc],
				 qh->desc_list[n_desc].buf,
				 qh->desc_list[n_desc].status);
			qtd->n_desc++;
			n_desc++;
		} while (chan->xfer_len > 0 &&
			 n_desc != MAX_DMA_DESC_NUM_GENERIC);

		dev_vdbg(hsotg->dev, "n_desc=%d\n", n_desc);
		qtd->in_process = 1;
		if (qh->ep_type == USB_ENDPOINT_XFER_CONTROL)
			break;
		if (n_desc == MAX_DMA_DESC_NUM_GENERIC)
			break;
	}

	if (n_desc) {
		qh->desc_list[n_desc - 1].status |=
				HOST_DMA_IOC | HOST_DMA_EOL | HOST_DMA_A;
		dev_vdbg(hsotg->dev, "set IOC/EOL/A bits in desc %d (%p)\n",
			 n_desc - 1, &qh->desc_list[n_desc - 1]);
		dma_sync_single_for_device(hsotg->dev,
					   qh->desc_list_dma + (n_desc - 1) *
					   sizeof(struct dwc2_hcd_dma_desc),
					   sizeof(struct dwc2_hcd_dma_desc),
					   DMA_TO_DEVICE);
		if (n_desc > 1) {
			qh->desc_list[0].status |= HOST_DMA_A;
			dev_vdbg(hsotg->dev, "set A bit in desc 0 (%p)\n",
				 &qh->desc_list[0]);
			dma_sync_single_for_device(hsotg->dev,
					qh->desc_list_dma,
					sizeof(struct dwc2_hcd_dma_desc),
					DMA_TO_DEVICE);
		}
		chan->ntd = n_desc;
	}
}

/**
 * dwc2_hcd_start_xfer_ddma() - Starts a transfer in Descriptor DMA mode
 *
 * @hsotg: The HCD state structure for the DWC OTG controller
 * @qh:    The QH to init
 *
 * Return: 0 if successful, negative error code otherwise
 *
 * For Control and Bulk endpoints, initializes descriptor list and starts the
 * transfer. For Interrupt and Isochronous endpoints, initializes descriptor
 * list then updates FrameList, marking appropriate entries as active.
 *
 * For Isochronous endpoints the starting descriptor index is calculated based
 * on the scheduled frame, but only on the first transfer descriptor within a
 * session. Then the transfer is started via enabling the channel.
 *
 * For Isochronous endpoints the channel is not halted on XferComplete
 * interrupt so remains assigned to the endpoint(QH) until session is done.
 */
void dwc2_hcd_start_xfer_ddma(struct dwc2_hsotg *hsotg, struct dwc2_qh *qh)
{
	/* Channel is already assigned */
	struct dwc2_host_chan *chan = qh->channel;
	u16 skip_frames = 0;

	switch (chan->ep_type) {
	case USB_ENDPOINT_XFER_CONTROL:
	case USB_ENDPOINT_XFER_BULK:
		dwc2_init_non_isoc_dma_desc(hsotg, qh);
		dwc2_hc_start_transfer_ddma(hsotg, chan);
		break;
	case USB_ENDPOINT_XFER_INT:
		dwc2_init_non_isoc_dma_desc(hsotg, qh);
		dwc2_update_frame_list(hsotg, qh, 1);
		dwc2_hc_start_transfer_ddma(hsotg, chan);
		break;
	case USB_ENDPOINT_XFER_ISOC:
		if (!qh->ntd)
			skip_frames = dwc2_recalc_initial_desc_idx(hsotg, qh);
		dwc2_init_isoc_dma_desc(hsotg, qh, skip_frames);

		if (!chan->xfer_started) {
			dwc2_update_frame_list(hsotg, qh, 1);

			/*
			 * Always set to max, instead of actual size. Otherwise
			 * ntd will be changed with channel being enabled. Not
			 * recommended.
			 */
			chan->ntd = dwc2_max_desc_num(qh);

			/* Enable channel only once for ISOC */
			dwc2_hc_start_transfer_ddma(hsotg, chan);
		}

		break;
	default:
		break;
	}
}

#define DWC2_CMPL_DONE		1
#define DWC2_CMPL_STOP		2

static int dwc2_cmpl_host_isoc_dma_desc(struct dwc2_hsotg *hsotg,
					struct dwc2_host_chan *chan,
					struct dwc2_qtd *qtd,
					struct dwc2_qh *qh, u16 idx)
{
	struct dwc2_hcd_dma_desc *dma_desc;
	struct dwc2_hcd_iso_packet_desc *frame_desc;
	u16 remain = 0;
	int rc = 0;

	if (!qtd->urb)
		return -EINVAL;

	dma_sync_single_for_cpu(hsotg->dev, qh->desc_list_dma + (idx *
				sizeof(struct dwc2_hcd_dma_desc)),
				sizeof(struct dwc2_hcd_dma_desc),
				DMA_FROM_DEVICE);

	dma_desc = &qh->desc_list[idx];

	frame_desc = &qtd->urb->iso_descs[qtd->isoc_frame_index_last];
	dma_desc->buf = (u32)(qtd->urb->dma + frame_desc->offset);
	if (chan->ep_is_in)
		remain = (dma_desc->status & HOST_DMA_ISOC_NBYTES_MASK) >>
			 HOST_DMA_ISOC_NBYTES_SHIFT;

	if ((dma_desc->status & HOST_DMA_STS_MASK) == HOST_DMA_STS_PKTERR) {
		/*
		 * XactError, or unable to complete all the transactions
		 * in the scheduled micro-frame/frame, both indicated by
		 * HOST_DMA_STS_PKTERR
		 */
		qtd->urb->error_count++;
		frame_desc->actual_length = qh->n_bytes[idx] - remain;
		frame_desc->status = -EPROTO;
	} else {
		/* Success */
		frame_desc->actual_length = qh->n_bytes[idx] - remain;
		frame_desc->status = 0;
	}

	if (++qtd->isoc_frame_index == qtd->urb->packet_count) {
		/*
		 * urb->status is not used for isoc transfers here. The
		 * individual frame_desc status are used instead.
		 */
		dwc2_host_complete(hsotg, qtd, 0);
		dwc2_hcd_qtd_unlink_and_free(hsotg, qtd, qh);

		/*
		 * This check is necessary because urb_dequeue can be called
		 * from urb complete callback (sound driver for example). All
		 * pending URBs are dequeued there, so no need for further
		 * processing.
		 */
		if (chan->halt_status == DWC2_HC_XFER_URB_DEQUEUE)
			return -1;
		rc = DWC2_CMPL_DONE;
	}

	qh->ntd--;

	/* Stop if IOC requested descriptor reached */
	if (dma_desc->status & HOST_DMA_IOC)
		rc = DWC2_CMPL_STOP;

	return rc;
}

static void dwc2_complete_isoc_xfer_ddma(struct dwc2_hsotg *hsotg,
					 struct dwc2_host_chan *chan,
					 enum dwc2_halt_status halt_status)
{
	struct dwc2_hcd_iso_packet_desc *frame_desc;
	struct dwc2_qtd *qtd, *qtd_tmp;
	struct dwc2_qh *qh;
	u16 idx;
	int rc;

	qh = chan->qh;
	idx = qh->td_first;

	if (chan->halt_status == DWC2_HC_XFER_URB_DEQUEUE) {
		list_for_each_entry(qtd, &qh->qtd_list, qtd_list_entry)
			qtd->in_process = 0;
		return;
	}

	if (halt_status == DWC2_HC_XFER_AHB_ERR ||
	    halt_status == DWC2_HC_XFER_BABBLE_ERR) {
		/*
		 * Channel is halted in these error cases, considered as serious
		 * issues.
		 * Complete all URBs marking all frames as failed, irrespective
		 * whether some of the descriptors (frames) succeeded or not.
		 * Pass error code to completion routine as well, to update
		 * urb->status, some of class drivers might use it to stop
		 * queing transfer requests.
		 */
		int err = halt_status == DWC2_HC_XFER_AHB_ERR ?
			  -EIO : -EOVERFLOW;

		list_for_each_entry_safe(qtd, qtd_tmp, &qh->qtd_list,
					 qtd_list_entry) {
			if (qtd->urb) {
				for (idx = 0; idx < qtd->urb->packet_count;
				     idx++) {
					frame_desc = &qtd->urb->iso_descs[idx];
					frame_desc->status = err;
				}

				dwc2_host_complete(hsotg, qtd, err);
			}

			dwc2_hcd_qtd_unlink_and_free(hsotg, qtd, qh);
		}

		return;
	}

	list_for_each_entry_safe(qtd, qtd_tmp, &qh->qtd_list, qtd_list_entry) {
		if (!qtd->in_process)
			break;

		/*
		 * Ensure idx corresponds to descriptor where first urb of this
		 * qtd was added. In fact, during isoc desc init, dwc2 may skip
		 * an index if current frame number is already over this index.
		 */
		if (idx != qtd->isoc_td_first) {
			dev_vdbg(hsotg->dev,
				 "try to complete %d instead of %d\n",
				 idx, qtd->isoc_td_first);
			idx = qtd->isoc_td_first;
		}

		do {
			struct dwc2_qtd *qtd_next;
			u16 cur_idx;

			rc = dwc2_cmpl_host_isoc_dma_desc(hsotg, chan, qtd, qh,
							  idx);
			if (rc < 0)
				return;
			idx = dwc2_desclist_idx_inc(idx, qh->host_interval,
						    chan->speed);
			if (!rc)
				continue;

			if (rc == DWC2_CMPL_DONE)
				break;

			/* rc == DWC2_CMPL_STOP */

			if (qh->host_interval >= 32)
				goto stop_scan;

			qh->td_first = idx;
			cur_idx = dwc2_frame_list_idx(hsotg->frame_number);
			qtd_next = list_first_entry(&qh->qtd_list,
						    struct dwc2_qtd,
						    qtd_list_entry);
			if (dwc2_frame_idx_num_gt(cur_idx,
						  qtd_next->isoc_td_last))
				break;

			goto stop_scan;

		} while (idx != qh->td_first);
	}

stop_scan:
	qh->td_first = idx;
}

static int dwc2_update_non_isoc_urb_state_ddma(struct dwc2_hsotg *hsotg,
					struct dwc2_host_chan *chan,
					struct dwc2_qtd *qtd,
					struct dwc2_hcd_dma_desc *dma_desc,
					enum dwc2_halt_status halt_status,
					u32 n_bytes, int *xfer_done)
{
	struct dwc2_hcd_urb *urb = qtd->urb;
	u16 remain = 0;

	if (chan->ep_is_in)
		remain = (dma_desc->status & HOST_DMA_NBYTES_MASK) >>
			 HOST_DMA_NBYTES_SHIFT;

	dev_vdbg(hsotg->dev, "remain=%d dwc2_urb=%p\n", remain, urb);

	if (halt_status == DWC2_HC_XFER_AHB_ERR) {
		dev_err(hsotg->dev, "EIO\n");
		urb->status = -EIO;
		return 1;
	}

	if ((dma_desc->status & HOST_DMA_STS_MASK) == HOST_DMA_STS_PKTERR) {
		switch (halt_status) {
		case DWC2_HC_XFER_STALL:
			dev_vdbg(hsotg->dev, "Stall\n");
			urb->status = -EPIPE;
			break;
		case DWC2_HC_XFER_BABBLE_ERR:
			dev_err(hsotg->dev, "Babble\n");
			urb->status = -EOVERFLOW;
			break;
		case DWC2_HC_XFER_XACT_ERR:
			dev_err(hsotg->dev, "XactErr\n");
			urb->status = -EPROTO;
			break;
		default:
			dev_err(hsotg->dev,
				"%s: Unhandled descriptor error status (%d)\n",
				__func__, halt_status);
			break;
		}
		return 1;
	}

	if (dma_desc->status & HOST_DMA_A) {
		dev_vdbg(hsotg->dev,
			 "Active descriptor encountered on channel %d\n",
			 chan->hc_num);
		return 0;
	}

	if (chan->ep_type == USB_ENDPOINT_XFER_CONTROL) {
		if (qtd->control_phase == DWC2_CONTROL_DATA) {
			urb->actual_length += n_bytes - remain;
			if (remain || urb->actual_length >= urb->length) {
				/*
				 * For Control Data stage do not set urb->status
				 * to 0, to prevent URB callback. Set it when
				 * Status phase is done. See below.
				 */
				*xfer_done = 1;
			}
		} else if (qtd->control_phase == DWC2_CONTROL_STATUS) {
			urb->status = 0;
			*xfer_done = 1;
		}
		/* No handling for SETUP stage */
	} else {
		/* BULK and INTR */
		urb->actual_length += n_bytes - remain;
		dev_vdbg(hsotg->dev, "length=%d actual=%d\n", urb->length,
			 urb->actual_length);
		if (remain || urb->actual_length >= urb->length) {
			urb->status = 0;
			*xfer_done = 1;
		}
	}

	return 0;
}

static int dwc2_process_non_isoc_desc(struct dwc2_hsotg *hsotg,
				      struct dwc2_host_chan *chan,
				      int chnum, struct dwc2_qtd *qtd,
				      int desc_num,
				      enum dwc2_halt_status halt_status,
				      int *xfer_done)
{
	struct dwc2_qh *qh = chan->qh;
	struct dwc2_hcd_urb *urb = qtd->urb;
	struct dwc2_hcd_dma_desc *dma_desc;
	u32 n_bytes;
	int failed;

	dev_vdbg(hsotg->dev, "%s()\n", __func__);

	if (!urb)
		return -EINVAL;

	dma_sync_single_for_cpu(hsotg->dev,
				qh->desc_list_dma + (desc_num *
				sizeof(struct dwc2_hcd_dma_desc)),
				sizeof(struct dwc2_hcd_dma_desc),
				DMA_FROM_DEVICE);

	dma_desc = &qh->desc_list[desc_num];
	n_bytes = qh->n_bytes[desc_num];
	dev_vdbg(hsotg->dev,
		 "qtd=%p dwc2_urb=%p desc_num=%d desc=%p n_bytes=%d\n",
		 qtd, urb, desc_num, dma_desc, n_bytes);
	failed = dwc2_update_non_isoc_urb_state_ddma(hsotg, chan, qtd, dma_desc,
						     halt_status, n_bytes,
						     xfer_done);
	if (failed || (*xfer_done && urb->status != -EINPROGRESS)) {
		dwc2_host_complete(hsotg, qtd, urb->status);
		dwc2_hcd_qtd_unlink_and_free(hsotg, qtd, qh);
		dev_vdbg(hsotg->dev, "failed=%1x xfer_done=%1x\n",
			 failed, *xfer_done);
		return failed;
	}

	if (qh->ep_type == USB_ENDPOINT_XFER_CONTROL) {
		switch (qtd->control_phase) {
		case DWC2_CONTROL_SETUP:
			if (urb->length > 0)
				qtd->control_phase = DWC2_CONTROL_DATA;
			else
				qtd->control_phase = DWC2_CONTROL_STATUS;
			dev_vdbg(hsotg->dev,
				 "  Control setup transaction done\n");
			break;
		case DWC2_CONTROL_DATA:
			if (*xfer_done) {
				qtd->control_phase = DWC2_CONTROL_STATUS;
				dev_vdbg(hsotg->dev,
					 "  Control data transfer done\n");
			} else if (desc_num + 1 == qtd->n_desc) {
				/*
				 * Last descriptor for Control data stage which
				 * is not completed yet
				 */
				dwc2_hcd_save_data_toggle(hsotg, chan, chnum,
							  qtd);
			}
			break;
		default:
			break;
		}
	}

	return 0;
}

static void dwc2_complete_non_isoc_xfer_ddma(struct dwc2_hsotg *hsotg,
					     struct dwc2_host_chan *chan,
					     int chnum,
					     enum dwc2_halt_status halt_status)
{
	struct list_head *qtd_item, *qtd_tmp;
	struct dwc2_qh *qh = chan->qh;
	struct dwc2_qtd *qtd = NULL;
	int xfer_done;
	int desc_num = 0;

	if (chan->halt_status == DWC2_HC_XFER_URB_DEQUEUE) {
		list_for_each_entry(qtd, &qh->qtd_list, qtd_list_entry)
			qtd->in_process = 0;
		return;
	}

	list_for_each_safe(qtd_item, qtd_tmp, &qh->qtd_list) {
		int i;
		int qtd_desc_count;

		qtd = list_entry(qtd_item, struct dwc2_qtd, qtd_list_entry);
		xfer_done = 0;
		qtd_desc_count = qtd->n_desc;

		for (i = 0; i < qtd_desc_count; i++) {
			if (dwc2_process_non_isoc_desc(hsotg, chan, chnum, qtd,
						       desc_num, halt_status,
						       &xfer_done)) {
				qtd = NULL;
				goto stop_scan;
			}

			desc_num++;
		}
	}

stop_scan:
	if (qh->ep_type != USB_ENDPOINT_XFER_CONTROL) {
		/*
		 * Resetting the data toggle for bulk and interrupt endpoints
		 * in case of stall. See handle_hc_stall_intr().
		 */
		if (halt_status == DWC2_HC_XFER_STALL)
			qh->data_toggle = DWC2_HC_PID_DATA0;
		else
			dwc2_hcd_save_data_toggle(hsotg, chan, chnum, NULL);
	}

	if (halt_status == DWC2_HC_XFER_COMPLETE) {
		if (chan->hcint & HCINTMSK_NYET) {
			/*
			 * Got a NYET on the last transaction of the transfer.
			 * It means that the endpoint should be in the PING
			 * state at the beginning of the next transfer.
			 */
			qh->ping_state = 1;
		}
	}
}

/**
 * dwc2_hcd_complete_xfer_ddma() - Scans the descriptor list, updates URB's
 * status and calls completion routine for the URB if it's done. Called from
 * interrupt handlers.
 *
 * @hsotg:       The HCD state structure for the DWC OTG controller
 * @chan:        Host channel the transfer is completed on
 * @chnum:       Index of Host channel registers
 * @halt_status: Reason the channel is being halted or just XferComplete
 *               for isochronous transfers
 *
 * Releases the channel to be used by other transfers.
 * In case of Isochronous endpoint the channel is not halted until the end of
 * the session, i.e. QTD list is empty.
 * If periodic channel released the FrameList is updated accordingly.
 * Calls transaction selection routines to activate pending transfers.
 */
void dwc2_hcd_complete_xfer_ddma(struct dwc2_hsotg *hsotg,
				 struct dwc2_host_chan *chan, int chnum,
				 enum dwc2_halt_status halt_status)
{
	struct dwc2_qh *qh = chan->qh;
	int continue_isoc_xfer = 0;
	enum dwc2_transaction_type tr_type;

	if (chan->ep_type == USB_ENDPOINT_XFER_ISOC) {
		dwc2_complete_isoc_xfer_ddma(hsotg, chan, halt_status);

		/* Release the channel if halted or session completed */
		if (halt_status != DWC2_HC_XFER_COMPLETE ||
		    list_empty(&qh->qtd_list)) {
			struct dwc2_qtd *qtd, *qtd_tmp;

			/*
			 * Kill all remainings QTDs since channel has been
			 * halted.
			 */
			list_for_each_entry_safe(qtd, qtd_tmp,
						 &qh->qtd_list,
						 qtd_list_entry) {
				dwc2_host_complete(hsotg, qtd,
						   -ECONNRESET);
				dwc2_hcd_qtd_unlink_and_free(hsotg,
							     qtd, qh);
			}

			/* Halt the channel if session completed */
			if (halt_status == DWC2_HC_XFER_COMPLETE)
				dwc2_hc_halt(hsotg, chan, halt_status);
			dwc2_release_channel_ddma(hsotg, qh);
			dwc2_hcd_qh_unlink(hsotg, qh);
		} else {
			/* Keep in assigned schedule to continue transfer */
			list_move_tail(&qh->qh_list_entry,
				       &hsotg->periodic_sched_assigned);
			/*
			 * If channel has been halted during giveback of urb
			 * then prevent any new scheduling.
			 */
			if (!chan->halt_status)
				continue_isoc_xfer = 1;
		}
		/*
		 * Todo: Consider the case when period exceeds FrameList size.
		 * Frame Rollover interrupt should be used.
		 */
	} else {
		/*
		 * Scan descriptor list to complete the URB(s), then release
		 * the channel
		 */
		dwc2_complete_non_isoc_xfer_ddma(hsotg, chan, chnum,
						 halt_status);
		dwc2_release_channel_ddma(hsotg, qh);
		dwc2_hcd_qh_unlink(hsotg, qh);

		if (!list_empty(&qh->qtd_list)) {
			/*
			 * Add back to inactive non-periodic schedule on normal
			 * completion
			 */
			dwc2_hcd_qh_add(hsotg, qh);
		}
	}

	tr_type = dwc2_hcd_select_transactions(hsotg);
	if (tr_type != DWC2_TRANSACTION_NONE || continue_isoc_xfer) {
		if (continue_isoc_xfer) {
			if (tr_type == DWC2_TRANSACTION_NONE)
				tr_type = DWC2_TRANSACTION_PERIODIC;
			else if (tr_type == DWC2_TRANSACTION_NON_PERIODIC)
				tr_type = DWC2_TRANSACTION_ALL;
		}
		dwc2_hcd_queue_transactions(hsotg, tr_type);
	}
}

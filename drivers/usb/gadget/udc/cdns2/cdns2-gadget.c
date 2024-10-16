// SPDX-License-Identifier: GPL-2.0
/*
 * Cadence USBHS-DEV Driver - gadget side.
 *
 * Copyright (C) 2023 Cadence Design Systems.
 *
 * Authors: Pawel Laszczak <pawell@cadence.com>
 */

/*
 * Work around 1:
 * At some situations, the controller may get stale data address in TRB
 * at below sequences:
 * 1. Controller read TRB includes data address
 * 2. Software updates TRBs includes data address and Cycle bit
 * 3. Controller read TRB which includes Cycle bit
 * 4. DMA run with stale data address
 *
 * To fix this problem, driver needs to make the first TRB in TD as invalid.
 * After preparing all TRBs driver needs to check the position of DMA and
 * if the DMA point to the first just added TRB and doorbell is 1,
 * then driver must defer making this TRB as valid. This TRB will be make
 * as valid during adding next TRB only if DMA is stopped or at TRBERR
 * interrupt.
 *
 */

#include <linux/dma-mapping.h>
#include <linux/pm_runtime.h>
#include <linux/interrupt.h>
#include <linux/property.h>
#include <linux/dmapool.h>
#include <linux/iopoll.h>

#include "cdns2-gadget.h"
#include "cdns2-trace.h"

/**
 * set_reg_bit_32 - set bit in given 32 bits register.
 * @ptr: register address.
 * @mask: bits to set.
 */
static void set_reg_bit_32(void __iomem *ptr, u32 mask)
{
	mask = readl(ptr) | mask;
	writel(mask, ptr);
}

/*
 * clear_reg_bit_32 - clear bit in given 32 bits register.
 * @ptr: register address.
 * @mask: bits to clear.
 */
static void clear_reg_bit_32(void __iomem *ptr, u32 mask)
{
	mask = readl(ptr) & ~mask;
	writel(mask, ptr);
}

/* Clear bit in given 8 bits register. */
static void clear_reg_bit_8(void __iomem *ptr, u8 mask)
{
	mask = readb(ptr) & ~mask;
	writeb(mask, ptr);
}

/* Set bit in given 16 bits register. */
void set_reg_bit_8(void __iomem *ptr, u8 mask)
{
	mask = readb(ptr) | mask;
	writeb(mask, ptr);
}

static int cdns2_get_dma_pos(struct cdns2_device *pdev,
			     struct cdns2_endpoint *pep)
{
	int dma_index;

	dma_index = readl(&pdev->adma_regs->ep_traddr) - pep->ring.dma;

	return dma_index / TRB_SIZE;
}

/* Get next private request from list. */
struct cdns2_request *cdns2_next_preq(struct list_head *list)
{
	return list_first_entry_or_null(list, struct cdns2_request, list);
}

void cdns2_select_ep(struct cdns2_device *pdev, u32 ep)
{
	if (pdev->selected_ep == ep)
		return;

	pdev->selected_ep = ep;
	writel(ep, &pdev->adma_regs->ep_sel);
}

dma_addr_t cdns2_trb_virt_to_dma(struct cdns2_endpoint *pep,
				 struct cdns2_trb *trb)
{
	u32 offset = (char *)trb - (char *)pep->ring.trbs;

	return pep->ring.dma + offset;
}

static void cdns2_free_tr_segment(struct cdns2_endpoint *pep)
{
	struct cdns2_device *pdev = pep->pdev;
	struct cdns2_ring *ring = &pep->ring;

	if (pep->ring.trbs) {
		dma_pool_free(pdev->eps_dma_pool, ring->trbs, ring->dma);
		memset(ring, 0, sizeof(*ring));
	}
}

/* Allocates Transfer Ring segment. */
static int cdns2_alloc_tr_segment(struct cdns2_endpoint *pep)
{
	struct cdns2_device *pdev = pep->pdev;
	struct cdns2_trb *link_trb;
	struct cdns2_ring *ring;

	ring = &pep->ring;

	if (!ring->trbs) {
		ring->trbs = dma_pool_alloc(pdev->eps_dma_pool,
					    GFP_DMA32 | GFP_ATOMIC,
					    &ring->dma);
		if (!ring->trbs)
			return -ENOMEM;
	}

	memset(ring->trbs, 0, TR_SEG_SIZE);

	if (!pep->num)
		return 0;

	/* Initialize the last TRB as Link TRB */
	link_trb = (ring->trbs + (TRBS_PER_SEGMENT - 1));
	link_trb->buffer = cpu_to_le32(TRB_BUFFER(ring->dma));
	link_trb->control = cpu_to_le32(TRB_CYCLE | TRB_TYPE(TRB_LINK) |
					TRB_TOGGLE);

	return 0;
}

/*
 * Stalls and flushes selected endpoint.
 * Endpoint must be selected before invoking this function.
 */
static void cdns2_ep_stall_flush(struct cdns2_endpoint *pep)
{
	struct cdns2_device *pdev = pep->pdev;
	int val;

	trace_cdns2_ep_halt(pep, 1, 1);

	writel(DMA_EP_CMD_DFLUSH, &pdev->adma_regs->ep_cmd);

	/* Wait for DFLUSH cleared. */
	readl_poll_timeout_atomic(&pdev->adma_regs->ep_cmd, val,
				  !(val & DMA_EP_CMD_DFLUSH), 1, 1000);
	pep->ep_state |= EP_STALLED;
	pep->ep_state &= ~EP_STALL_PENDING;
}

/*
 * Increment a trb index.
 *
 * The index should never point to the last link TRB in TR. After incrementing,
 * if it point to the link TRB, wrap around to the beginning and revert
 * cycle state bit. The link TRB is always at the last TRB entry.
 */
static void cdns2_ep_inc_trb(int *index, u8 *cs, int trb_in_seg)
{
	(*index)++;
	if (*index == (trb_in_seg - 1)) {
		*index = 0;
		*cs ^=  1;
	}
}

static void cdns2_ep_inc_enq(struct cdns2_ring *ring)
{
	ring->free_trbs--;
	cdns2_ep_inc_trb(&ring->enqueue, &ring->pcs, TRBS_PER_SEGMENT);
}

static void cdns2_ep_inc_deq(struct cdns2_ring *ring)
{
	ring->free_trbs++;
	cdns2_ep_inc_trb(&ring->dequeue, &ring->ccs, TRBS_PER_SEGMENT);
}

/*
 * Enable/disable LPM.
 *
 * If bit USBCS_LPMNYET is not set and device receive Extended Token packet,
 * then controller answer with ACK handshake.
 * If bit USBCS_LPMNYET is set and device receive Extended Token packet,
 * then controller answer with NYET handshake.
 */
static void cdns2_enable_l1(struct cdns2_device *pdev, int enable)
{
	if (enable) {
		clear_reg_bit_8(&pdev->usb_regs->usbcs, USBCS_LPMNYET);
		writeb(LPMCLOCK_SLEEP_ENTRY, &pdev->usb_regs->lpmclock);
	} else {
		set_reg_bit_8(&pdev->usb_regs->usbcs, USBCS_LPMNYET);
	}
}

static enum usb_device_speed cdns2_get_speed(struct cdns2_device *pdev)
{
	u8 speed = readb(&pdev->usb_regs->speedctrl);

	if (speed & SPEEDCTRL_HS)
		return USB_SPEED_HIGH;
	else if (speed & SPEEDCTRL_FS)
		return USB_SPEED_FULL;

	return USB_SPEED_UNKNOWN;
}

static struct cdns2_trb *cdns2_next_trb(struct cdns2_endpoint *pep,
					struct cdns2_trb *trb)
{
	if (trb == (pep->ring.trbs + (TRBS_PER_SEGMENT - 1)))
		return pep->ring.trbs;
	else
		return ++trb;
}

void cdns2_gadget_giveback(struct cdns2_endpoint *pep,
			   struct cdns2_request *preq,
			   int status)
{
	struct usb_request *request = &preq->request;
	struct cdns2_device *pdev = pep->pdev;

	list_del_init(&preq->list);

	if (request->status == -EINPROGRESS)
		request->status = status;

	usb_gadget_unmap_request_by_dev(pdev->dev, request, pep->dir);

	/* All TRBs have finished, clear the counter. */
	preq->finished_trb = 0;

	trace_cdns2_request_giveback(preq);

	if (request->complete) {
		spin_unlock(&pdev->lock);
		usb_gadget_giveback_request(&pep->endpoint, request);
		spin_lock(&pdev->lock);
	}

	if (request->buf == pdev->zlp_buf)
		cdns2_gadget_ep_free_request(&pep->endpoint, request);
}

static void cdns2_wa1_restore_cycle_bit(struct cdns2_endpoint *pep)
{
	/* Work around for stale data address in TRB. */
	if (pep->wa1_set) {
		trace_cdns2_wa1(pep, "restore cycle bit");

		pep->wa1_set = 0;
		pep->wa1_trb_index = 0xFFFF;
		if (pep->wa1_cycle_bit)
			pep->wa1_trb->control |= cpu_to_le32(0x1);
		else
			pep->wa1_trb->control &= cpu_to_le32(~0x1);
	}
}

static int cdns2_wa1_update_guard(struct cdns2_endpoint *pep,
				  struct cdns2_trb *trb)
{
	struct cdns2_device *pdev = pep->pdev;

	if (!pep->wa1_set) {
		u32 doorbell;

		doorbell = !!(readl(&pdev->adma_regs->ep_cmd) & DMA_EP_CMD_DRDY);

		if (doorbell) {
			pep->wa1_cycle_bit = pep->ring.pcs ? TRB_CYCLE : 0;
			pep->wa1_set = 1;
			pep->wa1_trb = trb;
			pep->wa1_trb_index = pep->ring.enqueue;
			trace_cdns2_wa1(pep, "set guard");
			return 0;
		}
	}
	return 1;
}

static void cdns2_wa1_tray_restore_cycle_bit(struct cdns2_device *pdev,
					     struct cdns2_endpoint *pep)
{
	int dma_index;
	u32 doorbell;

	doorbell = !!(readl(&pdev->adma_regs->ep_cmd) & DMA_EP_CMD_DRDY);
	dma_index = cdns2_get_dma_pos(pdev, pep);

	if (!doorbell || dma_index != pep->wa1_trb_index)
		cdns2_wa1_restore_cycle_bit(pep);
}

static int cdns2_prepare_ring(struct cdns2_device *pdev,
			      struct cdns2_endpoint *pep,
			      int num_trbs)
{
	struct cdns2_trb *link_trb = NULL;
	int doorbell, dma_index;
	struct cdns2_ring *ring;
	u32 ch_bit = 0;

	ring = &pep->ring;

	if (num_trbs > ring->free_trbs) {
		pep->ep_state |= EP_RING_FULL;
		trace_cdns2_no_room_on_ring("Ring full\n");
		return -ENOBUFS;
	}

	if ((ring->enqueue + num_trbs)  >= (TRBS_PER_SEGMENT - 1)) {
		doorbell = !!(readl(&pdev->adma_regs->ep_cmd) & DMA_EP_CMD_DRDY);
		dma_index = cdns2_get_dma_pos(pdev, pep);

		/* Driver can't update LINK TRB if it is current processed. */
		if (doorbell && dma_index == TRBS_PER_SEGMENT - 1) {
			pep->ep_state |= EP_DEFERRED_DRDY;
			return -ENOBUFS;
		}

		/* Update C bt in Link TRB before starting DMA. */
		link_trb = ring->trbs + (TRBS_PER_SEGMENT - 1);

		/*
		 * For TRs size equal 2 enabling TRB_CHAIN for epXin causes
		 * that DMA stuck at the LINK TRB.
		 * On the other hand, removing TRB_CHAIN for longer TRs for
		 * epXout cause that DMA stuck after handling LINK TRB.
		 * To eliminate this strange behavioral driver set TRB_CHAIN
		 * bit only for TR size > 2.
		 */
		if (pep->type == USB_ENDPOINT_XFER_ISOC || TRBS_PER_SEGMENT > 2)
			ch_bit = TRB_CHAIN;

		link_trb->control = cpu_to_le32(((ring->pcs) ? TRB_CYCLE : 0) |
				    TRB_TYPE(TRB_LINK) | TRB_TOGGLE | ch_bit);
	}

	return 0;
}

static void cdns2_dbg_request_trbs(struct cdns2_endpoint *pep,
				   struct cdns2_request *preq)
{
	struct cdns2_trb *link_trb = pep->ring.trbs + (TRBS_PER_SEGMENT - 1);
	struct cdns2_trb *trb = preq->trb;
	int num_trbs = preq->num_of_trb;
	int i = 0;

	while (i < num_trbs) {
		trace_cdns2_queue_trb(pep, trb + i);
		if (trb + i == link_trb) {
			trb = pep->ring.trbs;
			num_trbs = num_trbs - i;
			i = 0;
		} else {
			i++;
		}
	}
}

static unsigned int cdns2_count_trbs(struct cdns2_endpoint *pep,
				     u64 addr, u64 len)
{
	unsigned int num_trbs = 1;

	if (pep->type == USB_ENDPOINT_XFER_ISOC) {
		/*
		 * To speed up DMA performance address should not exceed 4KB.
		 * for high bandwidth transfer and driver will split
		 * such buffer into two TRBs.
		 */
		num_trbs = DIV_ROUND_UP(len +
					(addr & (TRB_MAX_ISO_BUFF_SIZE - 1)),
					TRB_MAX_ISO_BUFF_SIZE);

		if (pep->interval > 1)
			num_trbs = pep->dir ? num_trbs * pep->interval : 1;
	} else if (pep->dir) {
		/*
		 * One extra link trb for IN direction.
		 * Sometimes DMA doesn't want advance to next TD and transfer
		 * hangs. This extra Link TRB force DMA to advance to next TD.
		 */
		num_trbs++;
	}

	return num_trbs;
}

static unsigned int cdns2_count_sg_trbs(struct cdns2_endpoint *pep,
					struct usb_request *req)
{
	unsigned int i, len, full_len, num_trbs = 0;
	struct scatterlist *sg;
	int trb_len = 0;

	full_len = req->length;

	for_each_sg(req->sg, sg, req->num_sgs, i) {
		len = sg_dma_len(sg);
		num_trbs += cdns2_count_trbs(pep, sg_dma_address(sg), len);
		len = min(len, full_len);

		/*
		 * For HS ISO transfer TRBs should not exceed max packet size.
		 * When DMA is working, and data exceed max packet size then
		 * some data will be read in single mode instead burst mode.
		 * This behavior will drastically reduce the copying speed.
		 * To avoid this we need one or two extra TRBs.
		 * This issue occurs for UVC class with sg_supported = 1
		 * because buffers addresses are not aligned to 1024.
		 */
		if (pep->type == USB_ENDPOINT_XFER_ISOC) {
			u8 temp;

			trb_len += len;
			temp = trb_len >> 10;

			if (temp) {
				if (trb_len % 1024)
					num_trbs = num_trbs + temp;
				else
					num_trbs = num_trbs + temp - 1;

				trb_len = trb_len - (temp << 10);
			}
		}

		full_len -= len;
		if (full_len == 0)
			break;
	}

	return num_trbs;
}

/*
 * Function prepares the array with optimized AXI burst value for different
 * transfer lengths. Controller handles the final data which are less
 * then AXI burst size as single byte transactions.
 * e.g.:
 * Let's assume that driver prepares trb with trb->length 700 and burst size
 * will be set to 128. In this case the controller will handle a first 512 as
 * single AXI transaction but the next 188 bytes will be handled
 * as 47 separate AXI transaction.
 * The better solution is to use the burst size equal 16 and then we will
 * have only 25 AXI transaction (10 * 64 + 15 *4).
 */
static void cdsn2_isoc_burst_opt(struct cdns2_device *pdev)
{
	int axi_burst_option[]  =  {1, 2, 4, 8, 16, 32, 64, 128};
	int best_burst;
	int array_size;
	int opt_burst;
	int trb_size;
	int i, j;

	array_size = ARRAY_SIZE(axi_burst_option);

	for (i = 0; i <= MAX_ISO_SIZE; i++) {
		trb_size = i / 4;
		best_burst = trb_size ? trb_size : 1;

		for (j = 0; j < array_size; j++) {
			opt_burst = trb_size / axi_burst_option[j];
			opt_burst += trb_size % axi_burst_option[j];

			if (opt_burst < best_burst) {
				best_burst = opt_burst;
				pdev->burst_opt[i] = axi_burst_option[j];
			}
		}
	}
}

static void cdns2_ep_tx_isoc(struct cdns2_endpoint *pep,
			     struct cdns2_request *preq,
			     int num_trbs)
{
	struct scatterlist *sg = NULL;
	u32 remaining_packet_size = 0;
	struct cdns2_trb *trb;
	bool first_trb = true;
	dma_addr_t trb_dma;
	u32 trb_buff_len;
	u32 block_length;
	int td_idx = 0;
	int split_size;
	u32 full_len;
	int enqd_len;
	int sent_len;
	int sg_iter;
	u32 control;
	int num_tds;
	u32 length;

	/*
	 * For OUT direction 1 TD per interval is enough
	 * because TRBs are not dumped by controller.
	 */
	num_tds = pep->dir ? pep->interval : 1;
	split_size = preq->request.num_sgs ? 1024 : 3072;

	for (td_idx = 0; td_idx < num_tds; td_idx++) {
		if (preq->request.num_sgs) {
			sg = preq->request.sg;
			trb_dma = sg_dma_address(sg);
			block_length = sg_dma_len(sg);
		} else {
			trb_dma = preq->request.dma;
			block_length = preq->request.length;
		}

		full_len = preq->request.length;
		sg_iter = preq->request.num_sgs ? preq->request.num_sgs : 1;
		remaining_packet_size = split_size;

		for (enqd_len = 0;  enqd_len < full_len;
		     enqd_len += trb_buff_len) {
			if (remaining_packet_size == 0)
				remaining_packet_size = split_size;

			/*
			 * Calculate TRB length.- buffer can't across 4KB
			 * and max packet size.
			 */
			trb_buff_len = TRB_BUFF_LEN_UP_TO_BOUNDARY(trb_dma);
			trb_buff_len = min(trb_buff_len, remaining_packet_size);
			trb_buff_len = min(trb_buff_len, block_length);

			if (trb_buff_len > full_len - enqd_len)
				trb_buff_len = full_len - enqd_len;

			control = TRB_TYPE(TRB_NORMAL);

			/*
			 * For IN direction driver has to set the IOC for
			 * last TRB in last TD.
			 * For OUT direction driver must set IOC and ISP
			 * only for last TRB in each TDs.
			 */
			if (enqd_len + trb_buff_len >= full_len || !pep->dir)
				control |= TRB_IOC | TRB_ISP;

			/*
			 * Don't give the first TRB to the hardware (by toggling
			 * the cycle bit) until we've finished creating all the
			 * other TRBs.
			 */
			if (first_trb) {
				first_trb = false;
				if (pep->ring.pcs == 0)
					control |= TRB_CYCLE;
			} else {
				control |= pep->ring.pcs;
			}

			if (enqd_len + trb_buff_len < full_len)
				control |= TRB_CHAIN;

			length = TRB_LEN(trb_buff_len) |
				 TRB_BURST(pep->pdev->burst_opt[trb_buff_len]);

			trb = pep->ring.trbs + pep->ring.enqueue;
			trb->buffer = cpu_to_le32(TRB_BUFFER(trb_dma));
			trb->length = cpu_to_le32(length);
			trb->control = cpu_to_le32(control);

			trb_dma += trb_buff_len;
			sent_len = trb_buff_len;

			if (sg && sent_len >= block_length) {
				/* New sg entry */
				--sg_iter;
				sent_len -= block_length;
				if (sg_iter != 0) {
					sg = sg_next(sg);
					trb_dma = sg_dma_address(sg);
					block_length = sg_dma_len(sg);
				}
			}

			remaining_packet_size -= trb_buff_len;
			block_length -= sent_len;
			preq->end_trb = pep->ring.enqueue;

			cdns2_ep_inc_enq(&pep->ring);
		}
	}
}

static void cdns2_ep_tx_bulk(struct cdns2_endpoint *pep,
			     struct cdns2_request *preq,
			     int trbs_per_td)
{
	struct scatterlist *sg = NULL;
	struct cdns2_ring *ring;
	struct cdns2_trb *trb;
	dma_addr_t trb_dma;
	int sg_iter = 0;
	u32 control;
	u32 length;

	if (preq->request.num_sgs) {
		sg = preq->request.sg;
		trb_dma = sg_dma_address(sg);
		length = sg_dma_len(sg);
	} else {
		trb_dma = preq->request.dma;
		length = preq->request.length;
	}

	ring = &pep->ring;

	for (sg_iter = 0; sg_iter < trbs_per_td; sg_iter++) {
		control = TRB_TYPE(TRB_NORMAL) | ring->pcs | TRB_ISP;
		trb = pep->ring.trbs + ring->enqueue;

		if (pep->dir && sg_iter == trbs_per_td - 1) {
			preq->end_trb = ring->enqueue;
			control = ring->pcs | TRB_TYPE(TRB_LINK) | TRB_CHAIN
				  | TRB_IOC;
			cdns2_ep_inc_enq(&pep->ring);

			if (ring->enqueue == 0)
				control |= TRB_TOGGLE;

			/* Point to next bad TRB. */
			trb->buffer = cpu_to_le32(pep->ring.dma +
						  (ring->enqueue * TRB_SIZE));
			trb->length = 0;
			trb->control = cpu_to_le32(control);
			break;
		}

		/*
		 * Don't give the first TRB to the hardware (by toggling
		 * the cycle bit) until we've finished creating all the
		 * other TRBs.
		 */
		if (sg_iter == 0)
			control = control ^ TRB_CYCLE;

		/* For last TRB in TD. */
		if (sg_iter == (trbs_per_td - (pep->dir ? 2 : 1)))
			control |= TRB_IOC;
		else
			control |= TRB_CHAIN;

		trb->buffer = cpu_to_le32(trb_dma);
		trb->length = cpu_to_le32(TRB_BURST(pep->trb_burst_size) |
					   TRB_LEN(length));
		trb->control = cpu_to_le32(control);

		if (sg && sg_iter < (trbs_per_td - 1)) {
			sg = sg_next(sg);
			trb_dma = sg_dma_address(sg);
			length = sg_dma_len(sg);
		}

		preq->end_trb = ring->enqueue;
		cdns2_ep_inc_enq(&pep->ring);
	}
}

static void cdns2_set_drdy(struct cdns2_device *pdev,
			   struct cdns2_endpoint *pep)
{
	trace_cdns2_ring(pep);

	/*
	 * Memory barrier - Cycle Bit must be set before doorbell.
	 */
	dma_wmb();

	/* Clearing TRBERR and DESCMIS before setting DRDY. */
	writel(DMA_EP_STS_TRBERR | DMA_EP_STS_DESCMIS,
	       &pdev->adma_regs->ep_sts);
	writel(DMA_EP_CMD_DRDY, &pdev->adma_regs->ep_cmd);

	if (readl(&pdev->adma_regs->ep_sts) & DMA_EP_STS_TRBERR) {
		writel(DMA_EP_STS_TRBERR, &pdev->adma_regs->ep_sts);
		writel(DMA_EP_CMD_DRDY, &pdev->adma_regs->ep_cmd);
	}

	trace_cdns2_doorbell_epx(pep, readl(&pdev->adma_regs->ep_traddr));
}

static int cdns2_prepare_first_isoc_transfer(struct cdns2_device *pdev,
					     struct cdns2_endpoint *pep)
{
	struct cdns2_trb *trb;
	u32 buffer;
	u8 hw_ccs;

	if ((readl(&pdev->adma_regs->ep_cmd) & DMA_EP_CMD_DRDY))
		return -EBUSY;

	if (!pep->dir) {
		set_reg_bit_32(&pdev->adma_regs->ep_cfg, DMA_EP_CFG_ENABLE);
		writel(pep->ring.dma + pep->ring.dequeue,
		       &pdev->adma_regs->ep_traddr);
		return 0;
	}

	/*
	 * The first packet after doorbell can be corrupted so,
	 * driver prepares 0 length packet as first packet.
	 */
	buffer = pep->ring.dma + pep->ring.dequeue * TRB_SIZE;
	hw_ccs = !!DMA_EP_STS_CCS(readl(&pdev->adma_regs->ep_sts));

	trb = &pep->ring.trbs[TRBS_PER_SEGMENT];
	trb->length = 0;
	trb->buffer = cpu_to_le32(TRB_BUFFER(buffer));
	trb->control = cpu_to_le32((hw_ccs ? TRB_CYCLE : 0) | TRB_TYPE(TRB_NORMAL));

	/*
	 * LINK TRB is used to force updating cycle bit in controller and
	 * move to correct place in transfer ring.
	 */
	trb++;
	trb->length = 0;
	trb->buffer = cpu_to_le32(TRB_BUFFER(buffer));
	trb->control = cpu_to_le32((hw_ccs ? TRB_CYCLE : 0) |
				    TRB_TYPE(TRB_LINK) | TRB_CHAIN);

	if (hw_ccs !=  pep->ring.ccs)
		trb->control |= cpu_to_le32(TRB_TOGGLE);

	set_reg_bit_32(&pdev->adma_regs->ep_cfg, DMA_EP_CFG_ENABLE);
	writel(pep->ring.dma + (TRBS_PER_SEGMENT * TRB_SIZE),
	       &pdev->adma_regs->ep_traddr);

	return 0;
}

/* Prepare and start transfer on no-default endpoint. */
static int cdns2_ep_run_transfer(struct cdns2_endpoint *pep,
				 struct cdns2_request *preq)
{
	struct cdns2_device *pdev = pep->pdev;
	struct cdns2_ring *ring;
	u32 togle_pcs = 1;
	int num_trbs;
	int ret;

	cdns2_select_ep(pdev, pep->endpoint.address);

	if (preq->request.sg)
		num_trbs = cdns2_count_sg_trbs(pep, &preq->request);
	else
		num_trbs = cdns2_count_trbs(pep, preq->request.dma,
					    preq->request.length);

	ret = cdns2_prepare_ring(pdev, pep, num_trbs);
	if (ret)
		return ret;

	ring = &pep->ring;
	preq->start_trb = ring->enqueue;
	preq->trb = ring->trbs + ring->enqueue;

	if (usb_endpoint_xfer_isoc(pep->endpoint.desc)) {
		cdns2_ep_tx_isoc(pep, preq, num_trbs);
	} else {
		togle_pcs = cdns2_wa1_update_guard(pep, ring->trbs + ring->enqueue);
		cdns2_ep_tx_bulk(pep, preq, num_trbs);
	}

	preq->num_of_trb = num_trbs;

	/*
	 * Memory barrier - cycle bit must be set as the last operation.
	 */
	dma_wmb();

	/* Give the TD to the consumer. */
	if (togle_pcs)
		preq->trb->control = preq->trb->control ^ cpu_to_le32(1);

	cdns2_wa1_tray_restore_cycle_bit(pdev, pep);
	cdns2_dbg_request_trbs(pep, preq);

	if (!pep->wa1_set && !(pep->ep_state & EP_STALLED) && !pep->skip) {
		if (pep->type == USB_ENDPOINT_XFER_ISOC) {
			ret = cdns2_prepare_first_isoc_transfer(pdev, pep);
			if (ret)
				return 0;
		}

		cdns2_set_drdy(pdev, pep);
	}

	return 0;
}

/* Prepare and start transfer for all not started requests. */
static int cdns2_start_all_request(struct cdns2_device *pdev,
				   struct cdns2_endpoint *pep)
{
	struct cdns2_request *preq;
	int ret;

	while (!list_empty(&pep->deferred_list)) {
		preq = cdns2_next_preq(&pep->deferred_list);

		ret = cdns2_ep_run_transfer(pep, preq);
		if (ret)
			return ret;

		list_move_tail(&preq->list, &pep->pending_list);
	}

	pep->ep_state &= ~EP_RING_FULL;

	return 0;
}

/*
 * Check whether trb has been handled by DMA.
 *
 * Endpoint must be selected before invoking this function.
 *
 * Returns false if request has not been handled by DMA, else returns true.
 *
 * SR - start ring
 * ER - end ring
 * DQ = ring->dequeue - dequeue position
 * EQ = ring->enqueue - enqueue position
 * ST = preq->start_trb - index of first TRB in transfer ring
 * ET = preq->end_trb - index of last TRB in transfer ring
 * CI = current_index - index of processed TRB by DMA.
 *
 * As first step, we check if the TRB between the ST and ET.
 * Then, we check if cycle bit for index pep->dequeue
 * is correct.
 *
 * some rules:
 * 1. ring->dequeue never equals to current_index.
 * 2  ring->enqueue never exceed ring->dequeue
 * 3. exception: ring->enqueue == ring->dequeue
 *    and ring->free_trbs is zero.
 *    This case indicate that TR is full.
 *
 * At below two cases, the request have been handled.
 * Case 1 - ring->dequeue < current_index
 *      SR ... EQ ... DQ ... CI ... ER
 *      SR ... DQ ... CI ... EQ ... ER
 *
 * Case 2 - ring->dequeue > current_index
 * This situation takes place when CI go through the LINK TRB at the end of
 * transfer ring.
 *      SR ... CI ... EQ ... DQ ... ER
 */
static bool cdns2_trb_handled(struct cdns2_endpoint *pep,
			      struct cdns2_request *preq)
{
	struct cdns2_device *pdev = pep->pdev;
	struct cdns2_ring *ring;
	struct cdns2_trb *trb;
	int current_index = 0;
	int handled = 0;
	int doorbell;

	ring = &pep->ring;
	current_index = cdns2_get_dma_pos(pdev, pep);
	doorbell = !!(readl(&pdev->adma_regs->ep_cmd) & DMA_EP_CMD_DRDY);

	/*
	 * Only ISO transfer can use 2 entries outside the standard
	 * Transfer Ring. First of them is used as zero length packet and the
	 * second as LINK TRB.
	 */
	if (current_index >= TRBS_PER_SEGMENT)
		goto finish;

	/* Current trb doesn't belong to this request. */
	if (preq->start_trb < preq->end_trb) {
		if (ring->dequeue > preq->end_trb)
			goto finish;

		if (ring->dequeue < preq->start_trb)
			goto finish;
	}

	if (preq->start_trb > preq->end_trb && ring->dequeue > preq->end_trb &&
	    ring->dequeue < preq->start_trb)
		goto finish;

	if (preq->start_trb == preq->end_trb && ring->dequeue != preq->end_trb)
		goto finish;

	trb = &ring->trbs[ring->dequeue];

	if ((le32_to_cpu(trb->control) & TRB_CYCLE) != ring->ccs)
		goto finish;

	if (doorbell == 1 && current_index == ring->dequeue)
		goto finish;

	/* The corner case for TRBS_PER_SEGMENT equal 2). */
	if (TRBS_PER_SEGMENT == 2 && pep->type != USB_ENDPOINT_XFER_ISOC) {
		handled = 1;
		goto finish;
	}

	if (ring->enqueue == ring->dequeue &&
	    ring->free_trbs == 0) {
		handled = 1;
	} else if (ring->dequeue < current_index) {
		if ((current_index == (TRBS_PER_SEGMENT - 1)) &&
		    !ring->dequeue)
			goto finish;

		handled = 1;
	} else if (ring->dequeue  > current_index) {
		handled = 1;
	}

finish:
	trace_cdns2_request_handled(preq, current_index, handled);

	return handled;
}

static void cdns2_skip_isoc_td(struct cdns2_device *pdev,
			       struct cdns2_endpoint *pep,
			       struct cdns2_request *preq)
{
	struct cdns2_trb *trb;
	int i;

	trb = pep->ring.trbs + pep->ring.dequeue;

	for (i = preq->finished_trb ; i < preq->num_of_trb; i++) {
		preq->finished_trb++;
		trace_cdns2_complete_trb(pep, trb);
		cdns2_ep_inc_deq(&pep->ring);
		trb = cdns2_next_trb(pep, trb);
	}

	cdns2_gadget_giveback(pep, preq, 0);
	cdns2_prepare_first_isoc_transfer(pdev, pep);
	pep->skip = false;
	cdns2_set_drdy(pdev, pep);
}

static void cdns2_transfer_completed(struct cdns2_device *pdev,
				     struct cdns2_endpoint *pep)
{
	struct cdns2_request *preq = NULL;
	bool request_handled = false;
	struct cdns2_trb *trb;

	while (!list_empty(&pep->pending_list)) {
		preq = cdns2_next_preq(&pep->pending_list);
		trb = pep->ring.trbs + pep->ring.dequeue;

		/*
		 * The TRB was changed as link TRB, and the request
		 * was handled at ep_dequeue.
		 */
		while (TRB_FIELD_TO_TYPE(le32_to_cpu(trb->control)) == TRB_LINK &&
		       le32_to_cpu(trb->length)) {
			trace_cdns2_complete_trb(pep, trb);
			cdns2_ep_inc_deq(&pep->ring);
			trb = pep->ring.trbs + pep->ring.dequeue;
		}

		/*
		 * Re-select endpoint. It could be changed by other CPU
		 * during handling usb_gadget_giveback_request.
		 */
		cdns2_select_ep(pdev, pep->endpoint.address);

		while (cdns2_trb_handled(pep, preq)) {
			preq->finished_trb++;

			if (preq->finished_trb >= preq->num_of_trb)
				request_handled = true;

			trb = pep->ring.trbs + pep->ring.dequeue;
			trace_cdns2_complete_trb(pep, trb);

			if (pep->dir && pep->type == USB_ENDPOINT_XFER_ISOC)
				/*
				 * For ISOC IN controller doens't update the
				 * trb->length.
				 */
				preq->request.actual = preq->request.length;
			else
				preq->request.actual +=
					TRB_LEN(le32_to_cpu(trb->length));

			cdns2_ep_inc_deq(&pep->ring);
		}

		if (request_handled) {
			cdns2_gadget_giveback(pep, preq, 0);
			request_handled = false;
		} else {
			goto prepare_next_td;
		}

		if (pep->type != USB_ENDPOINT_XFER_ISOC &&
		    TRBS_PER_SEGMENT == 2)
			break;
	}

prepare_next_td:
	if (pep->skip && preq)
		cdns2_skip_isoc_td(pdev, pep, preq);

	if (!(pep->ep_state & EP_STALLED) &&
	    !(pep->ep_state & EP_STALL_PENDING))
		cdns2_start_all_request(pdev, pep);
}

static void cdns2_wakeup(struct cdns2_device *pdev)
{
	if (!pdev->may_wakeup)
		return;

	/* Start driving resume signaling to indicate remote wakeup. */
	set_reg_bit_8(&pdev->usb_regs->usbcs, USBCS_SIGRSUME);
}

static void cdns2_rearm_transfer(struct cdns2_endpoint *pep, u8 rearm)
{
	struct cdns2_device *pdev = pep->pdev;

	cdns2_wa1_restore_cycle_bit(pep);

	if (rearm) {
		trace_cdns2_ring(pep);

		/* Cycle Bit must be updated before arming DMA. */
		dma_wmb();

		writel(DMA_EP_CMD_DRDY, &pdev->adma_regs->ep_cmd);

		cdns2_wakeup(pdev);

		trace_cdns2_doorbell_epx(pep,
					 readl(&pdev->adma_regs->ep_traddr));
	}
}

static void cdns2_handle_epx_interrupt(struct cdns2_endpoint *pep)
{
	struct cdns2_device *pdev = pep->pdev;
	u8 isoerror = 0;
	u32 ep_sts_reg;
	u32 val;

	cdns2_select_ep(pdev, pep->endpoint.address);

	trace_cdns2_epx_irq(pdev, pep);

	ep_sts_reg = readl(&pdev->adma_regs->ep_sts);
	writel(ep_sts_reg, &pdev->adma_regs->ep_sts);

	if (pep->type == USB_ENDPOINT_XFER_ISOC) {
		u8 mult;
		u8 cs;

		mult = USB_EP_MAXP_MULT(pep->endpoint.desc->wMaxPacketSize);
		cs = pep->dir ? readb(&pdev->epx_regs->ep[pep->num - 1].txcs) :
				readb(&pdev->epx_regs->ep[pep->num - 1].rxcs);
		if (mult > 0)
			isoerror = EPX_CS_ERR(cs);
	}

	/*
	 * Sometimes ISO Error for mult=1 or mult=2 is not propagated on time
	 * from USB module to DMA module. To protect against this driver
	 * checks also the txcs/rxcs registers.
	 */
	if ((ep_sts_reg & DMA_EP_STS_ISOERR) || isoerror) {
		clear_reg_bit_32(&pdev->adma_regs->ep_cfg, DMA_EP_CFG_ENABLE);

		/* Wait for DBUSY cleared. */
		readl_poll_timeout_atomic(&pdev->adma_regs->ep_sts, val,
					  !(val & DMA_EP_STS_DBUSY), 1, 125);

		writel(DMA_EP_CMD_DFLUSH, &pep->pdev->adma_regs->ep_cmd);

		/* Wait for DFLUSH cleared. */
		readl_poll_timeout_atomic(&pep->pdev->adma_regs->ep_cmd, val,
					  !(val & DMA_EP_CMD_DFLUSH), 1, 10);

		pep->skip = true;
	}

	if (ep_sts_reg & DMA_EP_STS_TRBERR || pep->skip) {
		if (pep->ep_state & EP_STALL_PENDING &&
		    !(ep_sts_reg & DMA_EP_STS_DESCMIS))
			cdns2_ep_stall_flush(pep);

		/*
		 * For isochronous transfer driver completes request on
		 * IOC or on TRBERR. IOC appears only when device receive
		 * OUT data packet. If host disable stream or lost some packet
		 * then the only way to finish all queued transfer is to do it
		 * on TRBERR event.
		 */
		if (pep->type == USB_ENDPOINT_XFER_ISOC && !pep->wa1_set) {
			if (!pep->dir)
				clear_reg_bit_32(&pdev->adma_regs->ep_cfg,
						 DMA_EP_CFG_ENABLE);

			cdns2_transfer_completed(pdev, pep);
			if (pep->ep_state & EP_DEFERRED_DRDY) {
				pep->ep_state &= ~EP_DEFERRED_DRDY;
				cdns2_set_drdy(pdev, pep);
			}

			return;
		}

		cdns2_transfer_completed(pdev, pep);

		if (!(pep->ep_state & EP_STALLED) &&
		    !(pep->ep_state & EP_STALL_PENDING)) {
			if (pep->ep_state & EP_DEFERRED_DRDY) {
				pep->ep_state &= ~EP_DEFERRED_DRDY;
				cdns2_start_all_request(pdev, pep);
			} else {
				cdns2_rearm_transfer(pep, pep->wa1_set);
			}
		}

		return;
	}

	if ((ep_sts_reg & DMA_EP_STS_IOC) || (ep_sts_reg & DMA_EP_STS_ISP))
		cdns2_transfer_completed(pdev, pep);
}

static void cdns2_disconnect_gadget(struct cdns2_device *pdev)
{
	if (pdev->gadget_driver && pdev->gadget_driver->disconnect)
		pdev->gadget_driver->disconnect(&pdev->gadget);
}

static irqreturn_t cdns2_usb_irq_handler(int irq, void *data)
{
	struct cdns2_device *pdev = data;
	unsigned long reg_ep_ists;
	u8 reg_usb_irq_m;
	u8 reg_ext_irq_m;
	u8 reg_usb_irq;
	u8 reg_ext_irq;

	if (pdev->in_lpm)
		return IRQ_NONE;

	reg_usb_irq_m = readb(&pdev->interrupt_regs->usbien);
	reg_ext_irq_m = readb(&pdev->interrupt_regs->extien);

	/* Mask all sources of interrupt. */
	writeb(0, &pdev->interrupt_regs->usbien);
	writeb(0, &pdev->interrupt_regs->extien);
	writel(0, &pdev->adma_regs->ep_ien);

	/* Clear interrupt sources. */
	writel(0, &pdev->adma_regs->ep_sts);
	writeb(0, &pdev->interrupt_regs->usbirq);
	writeb(0, &pdev->interrupt_regs->extirq);

	reg_ep_ists = readl(&pdev->adma_regs->ep_ists);
	reg_usb_irq = readb(&pdev->interrupt_regs->usbirq);
	reg_ext_irq = readb(&pdev->interrupt_regs->extirq);

	if (reg_ep_ists || (reg_usb_irq & reg_usb_irq_m) ||
	    (reg_ext_irq & reg_ext_irq_m))
		return IRQ_WAKE_THREAD;

	writeb(USB_IEN_INIT, &pdev->interrupt_regs->usbien);
	writeb(EXTIRQ_WAKEUP, &pdev->interrupt_regs->extien);
	writel(~0, &pdev->adma_regs->ep_ien);

	return IRQ_NONE;
}

static irqreturn_t cdns2_thread_usb_irq_handler(struct cdns2_device *pdev)
{
	u8 usb_irq, ext_irq;
	int speed;
	int i;

	ext_irq = readb(&pdev->interrupt_regs->extirq) & EXTIRQ_WAKEUP;
	writeb(ext_irq, &pdev->interrupt_regs->extirq);

	usb_irq = readb(&pdev->interrupt_regs->usbirq) & USB_IEN_INIT;
	writeb(usb_irq, &pdev->interrupt_regs->usbirq);

	if (!ext_irq && !usb_irq)
		return IRQ_NONE;

	trace_cdns2_usb_irq(usb_irq, ext_irq);

	if (ext_irq & EXTIRQ_WAKEUP) {
		if (pdev->gadget_driver && pdev->gadget_driver->resume) {
			spin_unlock(&pdev->lock);
			pdev->gadget_driver->resume(&pdev->gadget);
			spin_lock(&pdev->lock);
		}
	}

	if (usb_irq & USBIRQ_LPM) {
		u8 reg = readb(&pdev->usb_regs->lpmctrl);

		/* LPM1 enter */
		if (!(reg & LPMCTRLLH_LPMNYET))
			writeb(0, &pdev->usb_regs->sleep_clkgate);
	}

	if (usb_irq & USBIRQ_SUSPEND) {
		if (pdev->gadget_driver && pdev->gadget_driver->suspend) {
			spin_unlock(&pdev->lock);
			pdev->gadget_driver->suspend(&pdev->gadget);
			spin_lock(&pdev->lock);
		}
	}

	if (usb_irq & USBIRQ_URESET) {
		if (pdev->gadget_driver) {
			pdev->dev_address = 0;

			spin_unlock(&pdev->lock);
			usb_gadget_udc_reset(&pdev->gadget,
					     pdev->gadget_driver);
			spin_lock(&pdev->lock);

			/*
			 * The USBIRQ_URESET is reported at the beginning of
			 * reset signal. 100ms is enough time to finish reset
			 * process. For high-speed reset procedure is completed
			 * when controller detect HS mode.
			 */
			for (i = 0; i < 100; i++) {
				mdelay(1);
				speed = cdns2_get_speed(pdev);
				if (speed == USB_SPEED_HIGH)
					break;
			}

			pdev->gadget.speed = speed;
			cdns2_enable_l1(pdev, 0);
			cdns2_ep0_config(pdev);
			pdev->may_wakeup = 0;
		}
	}

	if (usb_irq & USBIRQ_SUDAV) {
		pdev->ep0_stage = CDNS2_SETUP_STAGE;
		cdns2_handle_setup_packet(pdev);
	}

	return IRQ_HANDLED;
}

/* Deferred USB interrupt handler. */
static irqreturn_t cdns2_thread_irq_handler(int irq, void *data)
{
	struct cdns2_device *pdev = data;
	unsigned long  dma_ep_ists;
	unsigned long flags;
	unsigned int bit;

	local_bh_disable();
	spin_lock_irqsave(&pdev->lock, flags);

	cdns2_thread_usb_irq_handler(pdev);

	dma_ep_ists = readl(&pdev->adma_regs->ep_ists);
	if (!dma_ep_ists)
		goto unlock;

	trace_cdns2_dma_ep_ists(dma_ep_ists);

	/* Handle default endpoint OUT. */
	if (dma_ep_ists & DMA_EP_ISTS_EP_OUT0)
		cdns2_handle_ep0_interrupt(pdev, USB_DIR_OUT);

	/* Handle default endpoint IN. */
	if (dma_ep_ists & DMA_EP_ISTS_EP_IN0)
		cdns2_handle_ep0_interrupt(pdev, USB_DIR_IN);

	dma_ep_ists &= ~(DMA_EP_ISTS_EP_OUT0 | DMA_EP_ISTS_EP_IN0);

	for_each_set_bit(bit, &dma_ep_ists, sizeof(u32) * BITS_PER_BYTE) {
		u8 ep_idx = bit > 16 ? (bit - 16) * 2 : (bit * 2) - 1;

		/*
		 * Endpoints in pdev->eps[] are held in order:
		 * ep0, ep1out, ep1in, ep2out, ep2in... ep15out, ep15in.
		 * but in dma_ep_ists in order:
		 * ep0 ep1out ep2out ... ep15out ep0in ep1in .. ep15in
		 */
		cdns2_handle_epx_interrupt(&pdev->eps[ep_idx]);
	}

unlock:
	writel(~0, &pdev->adma_regs->ep_ien);
	writeb(USB_IEN_INIT, &pdev->interrupt_regs->usbien);
	writeb(EXTIRQ_WAKEUP, &pdev->interrupt_regs->extien);

	spin_unlock_irqrestore(&pdev->lock, flags);
	local_bh_enable();

	return IRQ_HANDLED;
}

/* Calculates and assigns onchip memory for endpoints. */
static void cdns2_eps_onchip_buffer_init(struct cdns2_device *pdev)
{
	struct cdns2_endpoint *pep;
	int min_buf_tx = 0;
	int min_buf_rx = 0;
	u16 tx_offset = 0;
	u16 rx_offset = 0;
	int free;
	int i;

	for (i = 0; i < CDNS2_ENDPOINTS_NUM; i++) {
		pep = &pdev->eps[i];

		if (!(pep->ep_state & EP_CLAIMED))
			continue;

		if (pep->dir)
			min_buf_tx += pep->buffering;
		else
			min_buf_rx += pep->buffering;
	}

	for (i = 0; i < CDNS2_ENDPOINTS_NUM; i++) {
		pep = &pdev->eps[i];

		if (!(pep->ep_state & EP_CLAIMED))
			continue;

		if (pep->dir) {
			free = pdev->onchip_tx_buf - min_buf_tx;

			if (free + pep->buffering >= 4)
				free = 4;
			else
				free = free + pep->buffering;

			min_buf_tx = min_buf_tx - pep->buffering + free;

			pep->buffering = free;

			writel(tx_offset,
			       &pdev->epx_regs->txstaddr[pep->num - 1]);
			pdev->epx_regs->txstaddr[pep->num - 1] = tx_offset;

			dev_dbg(pdev->dev, "%s onchip address %04x, buffering: %d\n",
				pep->name, tx_offset, pep->buffering);

			tx_offset += pep->buffering * 1024;
		} else {
			free = pdev->onchip_rx_buf - min_buf_rx;

			if (free + pep->buffering >= 4)
				free = 4;
			else
				free = free + pep->buffering;

			min_buf_rx = min_buf_rx - pep->buffering + free;

			pep->buffering = free;
			writel(rx_offset,
			       &pdev->epx_regs->rxstaddr[pep->num - 1]);

			dev_dbg(pdev->dev, "%s onchip address %04x, buffering: %d\n",
				pep->name, rx_offset, pep->buffering);

			rx_offset += pep->buffering * 1024;
		}
	}
}

/* Configure hardware endpoint. */
static int cdns2_ep_config(struct cdns2_endpoint *pep, bool enable)
{
	bool is_iso_ep = (pep->type == USB_ENDPOINT_XFER_ISOC);
	struct cdns2_device *pdev = pep->pdev;
	u32 max_packet_size;
	u8 dir = 0;
	u8 ep_cfg;
	u8 mult;
	u32 val;
	int ret;

	switch (pep->type) {
	case USB_ENDPOINT_XFER_INT:
		ep_cfg = EPX_CON_TYPE_INT;
		break;
	case USB_ENDPOINT_XFER_BULK:
		ep_cfg = EPX_CON_TYPE_BULK;
		break;
	default:
		mult = USB_EP_MAXP_MULT(pep->endpoint.desc->wMaxPacketSize);
		ep_cfg = mult << EPX_CON_ISOD_SHIFT;
		ep_cfg |= EPX_CON_TYPE_ISOC;

		if (pep->dir) {
			set_reg_bit_8(&pdev->epx_regs->isoautoarm, BIT(pep->num));
			set_reg_bit_8(&pdev->epx_regs->isoautodump, BIT(pep->num));
			set_reg_bit_8(&pdev->epx_regs->isodctrl, BIT(pep->num));
		}
	}

	switch (pdev->gadget.speed) {
	case USB_SPEED_FULL:
		max_packet_size = is_iso_ep ? 1023 : 64;
		break;
	case USB_SPEED_HIGH:
		max_packet_size = is_iso_ep ? 1024 : 512;
		break;
	default:
		/* All other speed are not supported. */
		return -EINVAL;
	}

	ep_cfg |= (EPX_CON_VAL | (pep->buffering - 1));

	if (pep->dir) {
		dir = FIFOCTRL_IO_TX;
		writew(max_packet_size, &pdev->epx_regs->txmaxpack[pep->num - 1]);
		writeb(ep_cfg, &pdev->epx_regs->ep[pep->num - 1].txcon);
	} else {
		writew(max_packet_size, &pdev->epx_regs->rxmaxpack[pep->num - 1]);
		writeb(ep_cfg, &pdev->epx_regs->ep[pep->num - 1].rxcon);
	}

	writeb(pep->num | dir | FIFOCTRL_FIFOAUTO,
	       &pdev->usb_regs->fifoctrl);
	writeb(pep->num | dir, &pdev->epx_regs->endprst);
	writeb(pep->num | ENDPRST_FIFORST | ENDPRST_TOGRST | dir,
	       &pdev->epx_regs->endprst);

	if (max_packet_size == 1024)
		pep->trb_burst_size = 128;
	else if (max_packet_size >= 512)
		pep->trb_burst_size = 64;
	else
		pep->trb_burst_size = 16;

	cdns2_select_ep(pdev, pep->num | pep->dir);
	writel(DMA_EP_CMD_EPRST | DMA_EP_CMD_DFLUSH, &pdev->adma_regs->ep_cmd);

	ret = readl_poll_timeout_atomic(&pdev->adma_regs->ep_cmd, val,
					!(val & (DMA_EP_CMD_DFLUSH |
					DMA_EP_CMD_EPRST)),
					1, 1000);

	if (ret)
		return ret;

	writel(DMA_EP_STS_TRBERR | DMA_EP_STS_ISOERR, &pdev->adma_regs->ep_sts_en);

	if (enable)
		writel(DMA_EP_CFG_ENABLE, &pdev->adma_regs->ep_cfg);

	trace_cdns2_epx_hw_cfg(pdev, pep);

	dev_dbg(pdev->dev, "Configure %s: with MPS: %08x, ep con: %02x\n",
		pep->name, max_packet_size, ep_cfg);

	return 0;
}

struct usb_request *cdns2_gadget_ep_alloc_request(struct usb_ep *ep,
						  gfp_t gfp_flags)
{
	struct cdns2_endpoint *pep = ep_to_cdns2_ep(ep);
	struct cdns2_request *preq;

	preq = kzalloc(sizeof(*preq), gfp_flags);
	if (!preq)
		return NULL;

	preq->pep = pep;

	trace_cdns2_alloc_request(preq);

	return &preq->request;
}

void cdns2_gadget_ep_free_request(struct usb_ep *ep,
				  struct usb_request *request)
{
	struct cdns2_request *preq = to_cdns2_request(request);

	trace_cdns2_free_request(preq);
	kfree(preq);
}

static int cdns2_gadget_ep_enable(struct usb_ep *ep,
				  const struct usb_endpoint_descriptor *desc)
{
	u32 reg = DMA_EP_STS_EN_TRBERREN;
	struct cdns2_endpoint *pep;
	struct cdns2_device *pdev;
	unsigned long flags;
	int enable = 1;
	int ret = 0;

	if (!ep || !desc || desc->bDescriptorType != USB_DT_ENDPOINT ||
	    !desc->wMaxPacketSize) {
		return -EINVAL;
	}

	pep = ep_to_cdns2_ep(ep);
	pdev = pep->pdev;

	if (dev_WARN_ONCE(pdev->dev, pep->ep_state & EP_ENABLED,
			  "%s is already enabled\n", pep->name))
		return 0;

	spin_lock_irqsave(&pdev->lock, flags);

	pep->type = usb_endpoint_type(desc);
	pep->interval = desc->bInterval ? BIT(desc->bInterval - 1) : 0;

	if (pdev->gadget.speed == USB_SPEED_FULL)
		if (pep->type == USB_ENDPOINT_XFER_INT)
			pep->interval = desc->bInterval;

	if (pep->interval > ISO_MAX_INTERVAL &&
	    pep->type == USB_ENDPOINT_XFER_ISOC) {
		dev_err(pdev->dev, "ISO period is limited to %d (current: %d)\n",
			ISO_MAX_INTERVAL, pep->interval);

		ret =  -EINVAL;
		goto exit;
	}

	/*
	 * During ISO OUT traffic DMA reads Transfer Ring for the EP which has
	 * never got doorbell.
	 * This issue was detected only on simulation, but to avoid this issue
	 * driver add protection against it. To fix it driver enable ISO OUT
	 * endpoint before setting DRBL. This special treatment of ISO OUT
	 * endpoints are recommended by controller specification.
	 */
	if (pep->type == USB_ENDPOINT_XFER_ISOC  && !pep->dir)
		enable = 0;

	ret = cdns2_alloc_tr_segment(pep);
	if (ret)
		goto exit;

	ret = cdns2_ep_config(pep, enable);
	if (ret) {
		cdns2_free_tr_segment(pep);
		ret =  -EINVAL;
		goto exit;
	}

	trace_cdns2_gadget_ep_enable(pep);

	pep->ep_state &= ~(EP_STALLED | EP_STALL_PENDING);
	pep->ep_state |= EP_ENABLED;
	pep->wa1_set = 0;
	pep->ring.enqueue = 0;
	pep->ring.dequeue = 0;
	reg = readl(&pdev->adma_regs->ep_sts);
	pep->ring.pcs = !!DMA_EP_STS_CCS(reg);
	pep->ring.ccs = !!DMA_EP_STS_CCS(reg);

	writel(pep->ring.dma, &pdev->adma_regs->ep_traddr);

	/* one TRB is reserved for link TRB used in DMULT mode*/
	pep->ring.free_trbs = TRBS_PER_SEGMENT - 1;

exit:
	spin_unlock_irqrestore(&pdev->lock, flags);

	return ret;
}

static int cdns2_gadget_ep_disable(struct usb_ep *ep)
{
	struct cdns2_endpoint *pep;
	struct cdns2_request *preq;
	struct cdns2_device *pdev;
	unsigned long flags;
	int val;

	if (!ep)
		return -EINVAL;

	pep = ep_to_cdns2_ep(ep);
	pdev = pep->pdev;

	if (dev_WARN_ONCE(pdev->dev, !(pep->ep_state & EP_ENABLED),
			  "%s is already disabled\n", pep->name))
		return 0;

	spin_lock_irqsave(&pdev->lock, flags);

	trace_cdns2_gadget_ep_disable(pep);

	cdns2_select_ep(pdev, ep->desc->bEndpointAddress);

	clear_reg_bit_32(&pdev->adma_regs->ep_cfg, DMA_EP_CFG_ENABLE);

	/*
	 * Driver needs some time before resetting endpoint.
	 * It need waits for clearing DBUSY bit or for timeout expired.
	 * 10us is enough time for controller to stop transfer.
	 */
	readl_poll_timeout_atomic(&pdev->adma_regs->ep_sts, val,
				  !(val & DMA_EP_STS_DBUSY), 1, 10);
	writel(DMA_EP_CMD_EPRST, &pdev->adma_regs->ep_cmd);

	readl_poll_timeout_atomic(&pdev->adma_regs->ep_cmd, val,
				  !(val & (DMA_EP_CMD_DFLUSH | DMA_EP_CMD_EPRST)),
				  1, 1000);

	while (!list_empty(&pep->pending_list)) {
		preq = cdns2_next_preq(&pep->pending_list);
		cdns2_gadget_giveback(pep, preq, -ESHUTDOWN);
	}

	while (!list_empty(&pep->deferred_list)) {
		preq = cdns2_next_preq(&pep->deferred_list);
		cdns2_gadget_giveback(pep, preq, -ESHUTDOWN);
	}

	ep->desc = NULL;
	pep->ep_state &= ~EP_ENABLED;

	spin_unlock_irqrestore(&pdev->lock, flags);

	return 0;
}

static int cdns2_ep_enqueue(struct cdns2_endpoint *pep,
			    struct cdns2_request *preq,
			    gfp_t gfp_flags)
{
	struct cdns2_device *pdev = pep->pdev;
	struct usb_request *request;
	int ret;

	request = &preq->request;
	request->actual = 0;
	request->status = -EINPROGRESS;

	ret = usb_gadget_map_request_by_dev(pdev->dev, request, pep->dir);
	if (ret) {
		trace_cdns2_request_enqueue_error(preq);
		return ret;
	}

	list_add_tail(&preq->list, &pep->deferred_list);
	trace_cdns2_request_enqueue(preq);

	if (!(pep->ep_state & EP_STALLED) && !(pep->ep_state & EP_STALL_PENDING))
		cdns2_start_all_request(pdev, pep);

	return 0;
}

static int cdns2_gadget_ep_queue(struct usb_ep *ep, struct usb_request *request,
				 gfp_t gfp_flags)
{
	struct usb_request *zlp_request;
	struct cdns2_request *preq;
	struct cdns2_endpoint *pep;
	struct cdns2_device *pdev;
	unsigned long flags;
	int ret;

	if (!request || !ep)
		return -EINVAL;

	pep = ep_to_cdns2_ep(ep);
	pdev = pep->pdev;

	if (!(pep->ep_state & EP_ENABLED)) {
		dev_err(pdev->dev, "%s: can't queue to disabled endpoint\n",
			pep->name);
		return -EINVAL;
	}

	spin_lock_irqsave(&pdev->lock, flags);

	preq =  to_cdns2_request(request);
	ret = cdns2_ep_enqueue(pep, preq, gfp_flags);

	if (ret == 0 && request->zero && request->length &&
	    (request->length % ep->maxpacket == 0)) {
		struct cdns2_request *preq;

		zlp_request = cdns2_gadget_ep_alloc_request(ep, GFP_ATOMIC);
		zlp_request->buf = pdev->zlp_buf;
		zlp_request->length = 0;

		preq = to_cdns2_request(zlp_request);
		ret = cdns2_ep_enqueue(pep, preq, gfp_flags);
	}

	spin_unlock_irqrestore(&pdev->lock, flags);
	return ret;
}

int cdns2_gadget_ep_dequeue(struct usb_ep *ep,
			    struct usb_request *request)
{
	struct cdns2_request *preq, *preq_temp, *cur_preq;
	struct cdns2_endpoint *pep;
	struct cdns2_trb *link_trb;
	u8 req_on_hw_ring = 0;
	unsigned long flags;
	u32 buffer;
	int val, i;

	if (!ep || !request || !ep->desc)
		return -EINVAL;

	pep = ep_to_cdns2_ep(ep);
	if (!pep->endpoint.desc) {
		dev_err(pep->pdev->dev, "%s: can't dequeue to disabled endpoint\n",
			pep->name);
		return -ESHUTDOWN;
	}

	/* Requests has been dequeued during disabling endpoint. */
	if (!(pep->ep_state & EP_ENABLED))
		return 0;

	spin_lock_irqsave(&pep->pdev->lock, flags);

	cur_preq = to_cdns2_request(request);
	trace_cdns2_request_dequeue(cur_preq);

	list_for_each_entry_safe(preq, preq_temp, &pep->pending_list, list) {
		if (cur_preq == preq) {
			req_on_hw_ring = 1;
			goto found;
		}
	}

	list_for_each_entry_safe(preq, preq_temp, &pep->deferred_list, list) {
		if (cur_preq == preq)
			goto found;
	}

	goto not_found;

found:
	link_trb = preq->trb;

	/* Update ring only if removed request is on pending_req_list list. */
	if (req_on_hw_ring && link_trb) {
		/* Stop DMA */
		writel(DMA_EP_CMD_DFLUSH, &pep->pdev->adma_regs->ep_cmd);

		/* Wait for DFLUSH cleared. */
		readl_poll_timeout_atomic(&pep->pdev->adma_regs->ep_cmd, val,
					  !(val & DMA_EP_CMD_DFLUSH), 1, 1000);

		buffer = cpu_to_le32(TRB_BUFFER(pep->ring.dma +
				    ((preq->end_trb + 1) * TRB_SIZE)));

		for (i = 0; i < preq->num_of_trb; i++) {
			link_trb->buffer = buffer;
			link_trb->control = cpu_to_le32((le32_to_cpu(link_trb->control)
					    & TRB_CYCLE) | TRB_CHAIN |
					    TRB_TYPE(TRB_LINK));

			trace_cdns2_queue_trb(pep, link_trb);
			link_trb = cdns2_next_trb(pep, link_trb);
		}

		if (pep->wa1_trb == preq->trb)
			cdns2_wa1_restore_cycle_bit(pep);
	}

	cdns2_gadget_giveback(pep, cur_preq, -ECONNRESET);

	preq = cdns2_next_preq(&pep->pending_list);
	if (preq)
		cdns2_rearm_transfer(pep, 1);

not_found:
	spin_unlock_irqrestore(&pep->pdev->lock, flags);
	return 0;
}

int cdns2_halt_endpoint(struct cdns2_device *pdev,
			struct cdns2_endpoint *pep,
			int value)
{
	u8 __iomem *conf;
	int dir = 0;

	if (!(pep->ep_state & EP_ENABLED))
		return -EPERM;

	if (pep->dir) {
		dir = ENDPRST_IO_TX;
		conf = &pdev->epx_regs->ep[pep->num - 1].txcon;
	} else {
		conf = &pdev->epx_regs->ep[pep->num - 1].rxcon;
	}

	if (!value) {
		struct cdns2_trb *trb = NULL;
		struct cdns2_request *preq;
		struct cdns2_trb trb_tmp;

		preq = cdns2_next_preq(&pep->pending_list);
		if (preq) {
			trb = preq->trb;
			if (trb) {
				trb_tmp = *trb;
				trb->control = trb->control ^ cpu_to_le32(TRB_CYCLE);
			}
		}

		trace_cdns2_ep_halt(pep, 0, 0);

		/* Resets Sequence Number */
		writeb(dir | pep->num, &pdev->epx_regs->endprst);
		writeb(dir | ENDPRST_TOGRST | pep->num,
		       &pdev->epx_regs->endprst);

		clear_reg_bit_8(conf, EPX_CON_STALL);

		pep->ep_state &= ~(EP_STALLED | EP_STALL_PENDING);

		if (preq) {
			if (trb)
				*trb = trb_tmp;

			cdns2_rearm_transfer(pep, 1);
		}

		cdns2_start_all_request(pdev, pep);
	} else {
		trace_cdns2_ep_halt(pep, 1, 0);
		set_reg_bit_8(conf, EPX_CON_STALL);
		writeb(dir | pep->num, &pdev->epx_regs->endprst);
		writeb(dir | ENDPRST_FIFORST | pep->num,
		       &pdev->epx_regs->endprst);
		pep->ep_state |= EP_STALLED;
	}

	return 0;
}

/* Sets/clears stall on selected endpoint. */
static int cdns2_gadget_ep_set_halt(struct usb_ep *ep, int value)
{
	struct cdns2_endpoint *pep = ep_to_cdns2_ep(ep);
	struct cdns2_device *pdev = pep->pdev;
	struct cdns2_request *preq;
	unsigned long flags = 0;
	int ret;

	spin_lock_irqsave(&pdev->lock, flags);

	preq = cdns2_next_preq(&pep->pending_list);
	if (value && preq) {
		trace_cdns2_ep_busy_try_halt_again(pep);
		ret = -EAGAIN;
		goto done;
	}

	if (!value)
		pep->ep_state &= ~EP_WEDGE;

	ret = cdns2_halt_endpoint(pdev, pep, value);

done:
	spin_unlock_irqrestore(&pdev->lock, flags);
	return ret;
}

static int cdns2_gadget_ep_set_wedge(struct usb_ep *ep)
{
	struct cdns2_endpoint *pep = ep_to_cdns2_ep(ep);

	cdns2_gadget_ep_set_halt(ep, 1);
	pep->ep_state |= EP_WEDGE;

	return 0;
}

static struct
cdns2_endpoint *cdns2_find_available_ep(struct cdns2_device *pdev,
					struct usb_endpoint_descriptor *desc)
{
	struct cdns2_endpoint *pep;
	struct usb_ep *ep;
	int ep_correct;

	list_for_each_entry(ep, &pdev->gadget.ep_list, ep_list) {
		unsigned long num;
		int ret;
		/* ep name pattern likes epXin or epXout. */
		char c[2] = {ep->name[2], '\0'};

		ret = kstrtoul(c, 10, &num);
		if (ret)
			return ERR_PTR(ret);
		pep = ep_to_cdns2_ep(ep);

		if (pep->num != num)
			continue;

		ep_correct = (pep->endpoint.caps.dir_in &&
			      usb_endpoint_dir_in(desc)) ||
			     (pep->endpoint.caps.dir_out &&
			      usb_endpoint_dir_out(desc));

		if (ep_correct && !(pep->ep_state & EP_CLAIMED))
			return pep;
	}

	return ERR_PTR(-ENOENT);
}

/*
 * Function used to recognize which endpoints will be used to optimize
 * on-chip memory usage.
 */
static struct
usb_ep *cdns2_gadget_match_ep(struct usb_gadget *gadget,
			      struct usb_endpoint_descriptor *desc,
			      struct usb_ss_ep_comp_descriptor *comp_desc)
{
	struct cdns2_device *pdev = gadget_to_cdns2_device(gadget);
	struct cdns2_endpoint *pep;
	unsigned long flags;

	pep = cdns2_find_available_ep(pdev, desc);
	if (IS_ERR(pep)) {
		dev_err(pdev->dev, "no available ep\n");
		return NULL;
	}

	spin_lock_irqsave(&pdev->lock, flags);

	if (usb_endpoint_type(desc) == USB_ENDPOINT_XFER_ISOC)
		pep->buffering = 4;
	else
		pep->buffering = 1;

	pep->ep_state |= EP_CLAIMED;
	spin_unlock_irqrestore(&pdev->lock, flags);

	return &pep->endpoint;
}

static const struct usb_ep_ops cdns2_gadget_ep_ops = {
	.enable = cdns2_gadget_ep_enable,
	.disable = cdns2_gadget_ep_disable,
	.alloc_request = cdns2_gadget_ep_alloc_request,
	.free_request = cdns2_gadget_ep_free_request,
	.queue = cdns2_gadget_ep_queue,
	.dequeue = cdns2_gadget_ep_dequeue,
	.set_halt = cdns2_gadget_ep_set_halt,
	.set_wedge = cdns2_gadget_ep_set_wedge,
};

static int cdns2_gadget_get_frame(struct usb_gadget *gadget)
{
	struct cdns2_device *pdev = gadget_to_cdns2_device(gadget);

	return readw(&pdev->usb_regs->frmnr);
}

static int cdns2_gadget_wakeup(struct usb_gadget *gadget)
{
	struct cdns2_device *pdev = gadget_to_cdns2_device(gadget);
	unsigned long flags;

	spin_lock_irqsave(&pdev->lock, flags);
	cdns2_wakeup(pdev);
	spin_unlock_irqrestore(&pdev->lock, flags);

	return 0;
}

static int cdns2_gadget_set_selfpowered(struct usb_gadget *gadget,
					int is_selfpowered)
{
	struct cdns2_device *pdev = gadget_to_cdns2_device(gadget);
	unsigned long flags;

	spin_lock_irqsave(&pdev->lock, flags);
	pdev->is_selfpowered = !!is_selfpowered;
	spin_unlock_irqrestore(&pdev->lock, flags);
	return 0;
}

/*  Disable interrupts and begin the controller halting process. */
static void cdns2_quiesce(struct cdns2_device *pdev)
{
	set_reg_bit_8(&pdev->usb_regs->usbcs, USBCS_DISCON);

	/* Disable interrupt. */
	writeb(0, &pdev->interrupt_regs->extien);
	writeb(0, &pdev->interrupt_regs->usbien);
	writew(0, &pdev->adma_regs->ep_ien);

	/* Clear interrupt line. */
	writeb(0x0, &pdev->interrupt_regs->usbirq);
}

static void cdns2_gadget_config(struct cdns2_device *pdev)
{
	cdns2_ep0_config(pdev);

	/* Enable DMA interrupts for all endpoints. */
	writel(~0x0, &pdev->adma_regs->ep_ien);
	cdns2_enable_l1(pdev, 0);
	writeb(USB_IEN_INIT, &pdev->interrupt_regs->usbien);
	writeb(EXTIRQ_WAKEUP, &pdev->interrupt_regs->extien);
	writel(DMA_CONF_DMULT, &pdev->adma_regs->conf);
}

static int cdns2_gadget_pullup(struct usb_gadget *gadget, int is_on)
{
	struct cdns2_device *pdev = gadget_to_cdns2_device(gadget);
	unsigned long flags;

	trace_cdns2_pullup(is_on);

	/*
	 * Disable events handling while controller is being
	 * enabled/disabled.
	 */
	disable_irq(pdev->irq);
	spin_lock_irqsave(&pdev->lock, flags);

	if (is_on) {
		cdns2_gadget_config(pdev);
		clear_reg_bit_8(&pdev->usb_regs->usbcs, USBCS_DISCON);
	} else {
		cdns2_quiesce(pdev);
	}

	spin_unlock_irqrestore(&pdev->lock, flags);
	enable_irq(pdev->irq);

	return 0;
}

static int cdns2_gadget_udc_start(struct usb_gadget *gadget,
				  struct usb_gadget_driver *driver)
{
	struct cdns2_device *pdev = gadget_to_cdns2_device(gadget);
	enum usb_device_speed max_speed = driver->max_speed;
	unsigned long flags;

	spin_lock_irqsave(&pdev->lock, flags);
	pdev->gadget_driver = driver;

	/* Limit speed if necessary. */
	max_speed = min(driver->max_speed, gadget->max_speed);

	switch (max_speed) {
	case USB_SPEED_FULL:
		writeb(SPEEDCTRL_HSDISABLE, &pdev->usb_regs->speedctrl);
		break;
	case USB_SPEED_HIGH:
		writeb(0, &pdev->usb_regs->speedctrl);
		break;
	default:
		dev_err(pdev->dev, "invalid maximum_speed parameter %d\n",
			max_speed);
		fallthrough;
	case USB_SPEED_UNKNOWN:
		/* Default to highspeed. */
		max_speed = USB_SPEED_HIGH;
		break;
	}

	/* Reset all USB endpoints. */
	writeb(ENDPRST_IO_TX, &pdev->usb_regs->endprst);
	writeb(ENDPRST_FIFORST | ENDPRST_TOGRST | ENDPRST_IO_TX,
	       &pdev->usb_regs->endprst);
	writeb(ENDPRST_FIFORST | ENDPRST_TOGRST, &pdev->usb_regs->endprst);

	cdns2_eps_onchip_buffer_init(pdev);

	cdns2_gadget_config(pdev);
	spin_unlock_irqrestore(&pdev->lock, flags);

	return 0;
}

static int cdns2_gadget_udc_stop(struct usb_gadget *gadget)
{
	struct cdns2_device *pdev = gadget_to_cdns2_device(gadget);
	struct cdns2_endpoint *pep;
	u32 bEndpointAddress;
	struct usb_ep *ep;
	int val;

	pdev->gadget_driver = NULL;
	pdev->gadget.speed = USB_SPEED_UNKNOWN;

	list_for_each_entry(ep, &pdev->gadget.ep_list, ep_list) {
		pep = ep_to_cdns2_ep(ep);
		bEndpointAddress = pep->num | pep->dir;
		cdns2_select_ep(pdev, bEndpointAddress);
		writel(DMA_EP_CMD_EPRST, &pdev->adma_regs->ep_cmd);
		readl_poll_timeout_atomic(&pdev->adma_regs->ep_cmd, val,
					  !(val & DMA_EP_CMD_EPRST), 1, 100);
	}

	cdns2_quiesce(pdev);

	writeb(ENDPRST_IO_TX, &pdev->usb_regs->endprst);
	writeb(ENDPRST_FIFORST | ENDPRST_TOGRST | ENDPRST_IO_TX,
	       &pdev->epx_regs->endprst);
	writeb(ENDPRST_FIFORST | ENDPRST_TOGRST, &pdev->epx_regs->endprst);

	return 0;
}

static const struct usb_gadget_ops cdns2_gadget_ops = {
	.get_frame = cdns2_gadget_get_frame,
	.wakeup = cdns2_gadget_wakeup,
	.set_selfpowered = cdns2_gadget_set_selfpowered,
	.pullup = cdns2_gadget_pullup,
	.udc_start = cdns2_gadget_udc_start,
	.udc_stop = cdns2_gadget_udc_stop,
	.match_ep = cdns2_gadget_match_ep,
};

static void cdns2_free_all_eps(struct cdns2_device *pdev)
{
	int i;

	for (i = 0; i < CDNS2_ENDPOINTS_NUM; i++)
		cdns2_free_tr_segment(&pdev->eps[i]);
}

/* Initializes software endpoints of gadget. */
static int cdns2_init_eps(struct cdns2_device *pdev)
{
	struct cdns2_endpoint *pep;
	int i;

	for (i = 0; i < CDNS2_ENDPOINTS_NUM; i++) {
		bool direction = !(i & 1); /* Start from OUT endpoint. */
		u8 epnum = ((i + 1) >> 1);

		/*
		 * Endpoints are being held in pdev->eps[] in form:
		 * ep0, ep1out, ep1in ... ep15out, ep15in.
		 */
		if (!CDNS2_IF_EP_EXIST(pdev, epnum, direction))
			continue;

		pep = &pdev->eps[i];
		pep->pdev = pdev;
		pep->num = epnum;
		/* 0 for OUT, 1 for IN. */
		pep->dir = direction ? USB_DIR_IN : USB_DIR_OUT;
		pep->idx = i;

		/* Ep0in and ep0out are represented by pdev->eps[0]. */
		if (!epnum) {
			int ret;

			snprintf(pep->name, sizeof(pep->name), "ep%d%s",
				 epnum, "BiDir");

			cdns2_init_ep0(pdev, pep);

			ret = cdns2_alloc_tr_segment(pep);
			if (ret) {
				dev_err(pdev->dev, "Failed to init ep0\n");
				return ret;
			}
		} else {
			snprintf(pep->name, sizeof(pep->name), "ep%d%s",
				 epnum, !!direction ? "in" : "out");
			pep->endpoint.name = pep->name;

			usb_ep_set_maxpacket_limit(&pep->endpoint, 1024);
			pep->endpoint.ops = &cdns2_gadget_ep_ops;
			list_add_tail(&pep->endpoint.ep_list, &pdev->gadget.ep_list);

			pep->endpoint.caps.dir_in = direction;
			pep->endpoint.caps.dir_out = !direction;

			pep->endpoint.caps.type_iso = 1;
			pep->endpoint.caps.type_bulk = 1;
			pep->endpoint.caps.type_int = 1;
		}

		pep->endpoint.name = pep->name;
		pep->ep_state = 0;

		dev_dbg(pdev->dev, "Init %s, SupType: CTRL: %s, INT: %s, "
			"BULK: %s, ISOC %s, SupDir IN: %s, OUT: %s\n",
			pep->name,
			(pep->endpoint.caps.type_control) ? "yes" : "no",
			(pep->endpoint.caps.type_int) ? "yes" : "no",
			(pep->endpoint.caps.type_bulk) ? "yes" : "no",
			(pep->endpoint.caps.type_iso) ? "yes" : "no",
			(pep->endpoint.caps.dir_in) ? "yes" : "no",
			(pep->endpoint.caps.dir_out) ? "yes" : "no");

		INIT_LIST_HEAD(&pep->pending_list);
		INIT_LIST_HEAD(&pep->deferred_list);
	}

	return 0;
}

static int cdns2_gadget_start(struct cdns2_device *pdev)
{
	u32 max_speed;
	void *buf;
	int ret;

	pdev->usb_regs = pdev->regs;
	pdev->ep0_regs = pdev->regs;
	pdev->epx_regs = pdev->regs;
	pdev->interrupt_regs = pdev->regs;
	pdev->adma_regs = pdev->regs + CDNS2_ADMA_REGS_OFFSET;

	/* Reset controller. */
	writeb(CPUCTRL_SW_RST | CPUCTRL_UPCLK | CPUCTRL_WUEN,
	       &pdev->usb_regs->cpuctrl);
	usleep_range(5, 10);

	usb_initialize_gadget(pdev->dev, &pdev->gadget, NULL);

	device_property_read_u16(pdev->dev, "cdns,on-chip-tx-buff-size",
				 &pdev->onchip_tx_buf);
	device_property_read_u16(pdev->dev, "cdns,on-chip-rx-buff-size",
				 &pdev->onchip_rx_buf);
	device_property_read_u32(pdev->dev, "cdns,avail-endpoints",
				 &pdev->eps_supported);

	/*
	 * Driver assumes that each USBHS controller has at least
	 * one IN and one OUT non control endpoint.
	 */
	if (!pdev->onchip_tx_buf && !pdev->onchip_rx_buf) {
		ret = -EINVAL;
		dev_err(pdev->dev, "Invalid on-chip memory configuration\n");
		goto put_gadget;
	}

	if (!(pdev->eps_supported & ~0x00010001)) {
		ret = -EINVAL;
		dev_err(pdev->dev, "No hardware endpoints available\n");
		goto put_gadget;
	}

	max_speed = usb_get_maximum_speed(pdev->dev);

	switch (max_speed) {
	case USB_SPEED_FULL:
	case USB_SPEED_HIGH:
		break;
	default:
		dev_err(pdev->dev, "invalid maximum_speed parameter %d\n",
			max_speed);
		fallthrough;
	case USB_SPEED_UNKNOWN:
		max_speed = USB_SPEED_HIGH;
		break;
	}

	pdev->gadget.max_speed = max_speed;
	pdev->gadget.speed = USB_SPEED_UNKNOWN;
	pdev->gadget.ops = &cdns2_gadget_ops;
	pdev->gadget.name = "usbhs-gadget";
	pdev->gadget.quirk_avoids_skb_reserve = 1;
	pdev->gadget.irq = pdev->irq;

	spin_lock_init(&pdev->lock);
	INIT_WORK(&pdev->pending_status_wq, cdns2_pending_setup_status_handler);

	/* Initialize endpoint container. */
	INIT_LIST_HEAD(&pdev->gadget.ep_list);
	pdev->eps_dma_pool = dma_pool_create("cdns2_eps_dma_pool", pdev->dev,
					     TR_SEG_SIZE, 8, 0);
	if (!pdev->eps_dma_pool) {
		dev_err(pdev->dev, "Failed to create TRB dma pool\n");
		ret = -ENOMEM;
		goto put_gadget;
	}

	ret = cdns2_init_eps(pdev);
	if (ret) {
		dev_err(pdev->dev, "Failed to create endpoints\n");
		goto destroy_dma_pool;
	}

	pdev->gadget.sg_supported = 1;

	pdev->zlp_buf = kzalloc(CDNS2_EP_ZLP_BUF_SIZE, GFP_KERNEL);
	if (!pdev->zlp_buf) {
		ret = -ENOMEM;
		goto destroy_dma_pool;
	}

	/* Allocate memory for setup packet buffer. */
	buf = dma_alloc_coherent(pdev->dev, 8, &pdev->ep0_preq.request.dma,
				 GFP_DMA);
	pdev->ep0_preq.request.buf = buf;

	if (!pdev->ep0_preq.request.buf) {
		ret = -ENOMEM;
		goto free_zlp_buf;
	}

	/* Add USB gadget device. */
	ret = usb_add_gadget(&pdev->gadget);
	if (ret < 0) {
		dev_err(pdev->dev, "Failed to add gadget\n");
		goto free_ep0_buf;
	}

	return 0;

free_ep0_buf:
	dma_free_coherent(pdev->dev, 8, pdev->ep0_preq.request.buf,
			  pdev->ep0_preq.request.dma);
free_zlp_buf:
	kfree(pdev->zlp_buf);
destroy_dma_pool:
	dma_pool_destroy(pdev->eps_dma_pool);
put_gadget:
	usb_put_gadget(&pdev->gadget);

	return ret;
}

int cdns2_gadget_suspend(struct cdns2_device *pdev)
{
	unsigned long flags;

	cdns2_disconnect_gadget(pdev);

	spin_lock_irqsave(&pdev->lock, flags);
	pdev->gadget.speed = USB_SPEED_UNKNOWN;

	trace_cdns2_device_state("notattached");
	usb_gadget_set_state(&pdev->gadget, USB_STATE_NOTATTACHED);
	cdns2_enable_l1(pdev, 0);

	/* Disable interrupt for device. */
	writeb(0, &pdev->interrupt_regs->usbien);
	writel(0, &pdev->adma_regs->ep_ien);
	spin_unlock_irqrestore(&pdev->lock, flags);

	return 0;
}

int cdns2_gadget_resume(struct cdns2_device *pdev, bool hibernated)
{
	unsigned long flags;

	spin_lock_irqsave(&pdev->lock, flags);

	if (!pdev->gadget_driver) {
		spin_unlock_irqrestore(&pdev->lock, flags);
		return 0;
	}

	cdns2_gadget_config(pdev);

	if (hibernated)
		clear_reg_bit_8(&pdev->usb_regs->usbcs, USBCS_DISCON);

	spin_unlock_irqrestore(&pdev->lock, flags);

	return 0;
}

void cdns2_gadget_remove(struct cdns2_device *pdev)
{
	pm_runtime_mark_last_busy(pdev->dev);
	pm_runtime_put_autosuspend(pdev->dev);

	usb_del_gadget(&pdev->gadget);
	cdns2_free_all_eps(pdev);

	dma_pool_destroy(pdev->eps_dma_pool);
	kfree(pdev->zlp_buf);
	usb_put_gadget(&pdev->gadget);
}

int cdns2_gadget_init(struct cdns2_device *pdev)
{
	int ret;

	/* Ensure 32-bit DMA Mask. */
	ret = dma_set_mask_and_coherent(pdev->dev, DMA_BIT_MASK(32));
	if (ret) {
		dev_err(pdev->dev, "Failed to set dma mask: %d\n", ret);
		return ret;
	}

	pm_runtime_get_sync(pdev->dev);

	cdsn2_isoc_burst_opt(pdev);

	ret = cdns2_gadget_start(pdev);
	if (ret) {
		pm_runtime_put_sync(pdev->dev);
		return ret;
	}

	/*
	 * Because interrupt line can be shared with other components in
	 * driver it can't use IRQF_ONESHOT flag here.
	 */
	ret = devm_request_threaded_irq(pdev->dev, pdev->irq,
					cdns2_usb_irq_handler,
					cdns2_thread_irq_handler,
					IRQF_SHARED,
					dev_name(pdev->dev),
					pdev);
	if (ret)
		goto err0;

	return 0;

err0:
	cdns2_gadget_remove(pdev);

	return ret;
}

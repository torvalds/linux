/*
 * arch/arm/mach-tegra/dma.c
 *
 * System DMA driver for NVIDIA Tegra SoCs
 *
 * Copyright (c) 2008-2009, NVIDIA Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/err.h>
#include <linux/irq.h>
#include <linux/delay.h>
#include <mach/dma.h>
#include <mach/irqs.h>
#include <mach/iomap.h>
#include <mach/suspend.h>

#define APB_DMA_GEN				0x000
#define GEN_ENABLE				(1<<31)

#define APB_DMA_CNTRL				0x010

#define APB_DMA_IRQ_MASK			0x01c

#define APB_DMA_IRQ_MASK_SET			0x020

#define APB_DMA_CHAN_CSR			0x000
#define CSR_ENB					(1<<31)
#define CSR_IE_EOC				(1<<30)
#define CSR_HOLD				(1<<29)
#define CSR_DIR					(1<<28)
#define CSR_ONCE				(1<<27)
#define CSR_FLOW				(1<<21)
#define CSR_REQ_SEL_SHIFT			16
#define CSR_REQ_SEL_MASK			(0x1F<<CSR_REQ_SEL_SHIFT)
#define CSR_WCOUNT_SHIFT			2
#define CSR_WCOUNT_MASK				0xFFFC

#define APB_DMA_CHAN_STA				0x004
#define STA_BUSY				(1<<31)
#define STA_ISE_EOC				(1<<30)
#define STA_HALT				(1<<29)
#define STA_PING_PONG				(1<<28)
#define STA_COUNT_SHIFT				2
#define STA_COUNT_MASK				0xFFFC

#define APB_DMA_CHAN_AHB_PTR				0x010

#define APB_DMA_CHAN_AHB_SEQ				0x014
#define AHB_SEQ_INTR_ENB			(1<<31)
#define AHB_SEQ_BUS_WIDTH_SHIFT			28
#define AHB_SEQ_BUS_WIDTH_MASK			(0x7<<AHB_SEQ_BUS_WIDTH_SHIFT)
#define AHB_SEQ_BUS_WIDTH_8			(0<<AHB_SEQ_BUS_WIDTH_SHIFT)
#define AHB_SEQ_BUS_WIDTH_16			(1<<AHB_SEQ_BUS_WIDTH_SHIFT)
#define AHB_SEQ_BUS_WIDTH_32			(2<<AHB_SEQ_BUS_WIDTH_SHIFT)
#define AHB_SEQ_BUS_WIDTH_64			(3<<AHB_SEQ_BUS_WIDTH_SHIFT)
#define AHB_SEQ_BUS_WIDTH_128			(4<<AHB_SEQ_BUS_WIDTH_SHIFT)
#define AHB_SEQ_DATA_SWAP			(1<<27)
#define AHB_SEQ_BURST_MASK			(0x7<<24)
#define AHB_SEQ_BURST_1				(4<<24)
#define AHB_SEQ_BURST_4				(5<<24)
#define AHB_SEQ_BURST_8				(6<<24)
#define AHB_SEQ_DBL_BUF				(1<<19)
#define AHB_SEQ_WRAP_SHIFT			16
#define AHB_SEQ_WRAP_MASK			(0x7<<AHB_SEQ_WRAP_SHIFT)

#define APB_DMA_CHAN_APB_PTR				0x018

#define APB_DMA_CHAN_APB_SEQ				0x01c
#define APB_SEQ_BUS_WIDTH_SHIFT			28
#define APB_SEQ_BUS_WIDTH_MASK			(0x7<<APB_SEQ_BUS_WIDTH_SHIFT)
#define APB_SEQ_BUS_WIDTH_8			(0<<APB_SEQ_BUS_WIDTH_SHIFT)
#define APB_SEQ_BUS_WIDTH_16			(1<<APB_SEQ_BUS_WIDTH_SHIFT)
#define APB_SEQ_BUS_WIDTH_32			(2<<APB_SEQ_BUS_WIDTH_SHIFT)
#define APB_SEQ_BUS_WIDTH_64			(3<<APB_SEQ_BUS_WIDTH_SHIFT)
#define APB_SEQ_BUS_WIDTH_128			(4<<APB_SEQ_BUS_WIDTH_SHIFT)
#define APB_SEQ_DATA_SWAP			(1<<27)
#define APB_SEQ_WRAP_SHIFT			16
#define APB_SEQ_WRAP_MASK			(0x7<<APB_SEQ_WRAP_SHIFT)

#define TEGRA_SYSTEM_DMA_CH_NR			16
#define TEGRA_SYSTEM_DMA_AVP_CH_NUM		4
#define TEGRA_SYSTEM_DMA_CH_MIN			0
#define TEGRA_SYSTEM_DMA_CH_MAX	\
	(TEGRA_SYSTEM_DMA_CH_NR - TEGRA_SYSTEM_DMA_AVP_CH_NUM - 1)

const unsigned int ahb_addr_wrap_table[8] = {
	0, 32, 64, 128, 256, 512, 1024, 2048
};

const unsigned int apb_addr_wrap_table[8] = {0, 1, 2, 4, 8, 16, 32, 64};

const unsigned int bus_width_table[5] = {8, 16, 32, 64, 128};

#define TEGRA_DMA_NAME_SIZE 16
struct tegra_dma_channel {
	struct list_head	list;
	int			id;
	spinlock_t		lock;
	char			name[TEGRA_DMA_NAME_SIZE];
	void  __iomem		*addr;
	int			mode;
	int			irq;
	int			req_transfer_count;
};

#define  NV_DMA_MAX_CHANNELS  32

static DEFINE_MUTEX(tegra_dma_lock);
static DEFINE_SPINLOCK(enable_lock);

static DECLARE_BITMAP(channel_usage, NV_DMA_MAX_CHANNELS);
static struct tegra_dma_channel dma_channels[NV_DMA_MAX_CHANNELS];

static void tegra_dma_update_hw(struct tegra_dma_channel *ch,
	struct tegra_dma_req *req);
static void tegra_dma_update_hw_partial(struct tegra_dma_channel *ch,
	struct tegra_dma_req *req);
static void tegra_dma_stop(struct tegra_dma_channel *ch);

void tegra_dma_flush(struct tegra_dma_channel *ch)
{
}
EXPORT_SYMBOL(tegra_dma_flush);

void tegra_dma_dequeue(struct tegra_dma_channel *ch)
{
	struct tegra_dma_req *req;

	if (tegra_dma_is_empty(ch))
		return;

	req = list_entry(ch->list.next, typeof(*req), node);

	tegra_dma_dequeue_req(ch, req);
	return;
}

void tegra_dma_stop(struct tegra_dma_channel *ch)
{
	u32 csr;
	u32 status;

	csr = readl(ch->addr + APB_DMA_CHAN_CSR);
	csr &= ~CSR_IE_EOC;
	writel(csr, ch->addr + APB_DMA_CHAN_CSR);

	csr &= ~CSR_ENB;
	writel(csr, ch->addr + APB_DMA_CHAN_CSR);

	status = readl(ch->addr + APB_DMA_CHAN_STA);
	if (status & STA_ISE_EOC)
		writel(status, ch->addr + APB_DMA_CHAN_STA);
}

bool tegra_dma_is_stopped(struct tegra_dma_channel *ch)
{
	return !!(readl(ch->addr + APB_DMA_CHAN_STA) & CSR_ENB);
}

int tegra_dma_cancel(struct tegra_dma_channel *ch)
{
	unsigned long irq_flags;

	spin_lock_irqsave(&ch->lock, irq_flags);
	while (!list_empty(&ch->list))
		list_del(ch->list.next);

	tegra_dma_stop(ch);

	spin_unlock_irqrestore(&ch->lock, irq_flags);
	return 0;
}

/* should be called with the channel lock held */
static unsigned int dma_active_count(struct tegra_dma_channel *ch,
	struct tegra_dma_req *req, unsigned int status)
{
	unsigned int to_transfer;
	unsigned int req_transfer_count;

	unsigned int bytes_transferred;

	to_transfer = (status & STA_COUNT_MASK) >> STA_COUNT_SHIFT;
	req_transfer_count = ch->req_transfer_count;
	req_transfer_count += 1;
	to_transfer += 1;

	bytes_transferred = req_transfer_count;

	if (status & STA_BUSY)
		bytes_transferred -= to_transfer;

	/* In continuous transfer mode, DMA only tracks the count of the
	 * half DMA buffer. So, if the DMA already finished half the DMA
	 * then add the half buffer to the completed count.
	 */
	if (ch->mode & TEGRA_DMA_MODE_CONTINUOUS_DOUBLE)
		if (req->buffer_status == TEGRA_DMA_REQ_BUF_STATUS_HALF_FULL)
			bytes_transferred += req_transfer_count;

	if (status & STA_ISE_EOC)
		bytes_transferred += req_transfer_count;

	bytes_transferred *= 4;

	return bytes_transferred;
}

int tegra_dma_dequeue_req(struct tegra_dma_channel *ch,
	struct tegra_dma_req *_req)
{
	struct tegra_dma_req *req = NULL;
	int found = 0;
	unsigned int status;
	unsigned long irq_flags;
	int stop = 0;
	void __iomem *addr = IO_ADDRESS(TEGRA_APB_DMA_BASE);

	spin_lock_irqsave(&ch->lock, irq_flags);

	if (list_entry(ch->list.next, struct tegra_dma_req, node) == _req)
		stop = 1;

	list_for_each_entry(req, &ch->list, node) {
		if (req == _req) {
			list_del(&req->node);
			found = 1;
			break;
		}
	}
	if (!found) {
		spin_unlock_irqrestore(&ch->lock, irq_flags);
		return 0;
	}

	if (!stop)
		goto skip_status;

	/* STOP the DMA and get the transfer count.
	  * Getting the transfer count is tricky.
	 *  - Globally disable DMA on all channels
	 *  - Read the channel's status register to know the number of pending
	 *    bytes to be transfered.
	 *  - Stop the dma channel
	 *  - Globally re-enable DMA to resume other transfers
	 */
	spin_lock(&enable_lock);
	writel(0, addr + APB_DMA_GEN);
	udelay(20);
	status = readl(ch->addr + APB_DMA_CHAN_STA);
	tegra_dma_stop(ch);
	writel(GEN_ENABLE, addr + APB_DMA_GEN);
	spin_unlock(&enable_lock);

	req->bytes_transferred = dma_active_count(ch, req, status);

	if (!list_empty(&ch->list)) {
		/* if the list is not empty, queue the next request */
		struct tegra_dma_req *next_req;
		next_req = list_entry(ch->list.next,
			typeof(*next_req), node);
		tegra_dma_update_hw(ch, next_req);
	}
skip_status:
	req->status = -TEGRA_DMA_REQ_ERROR_ABORTED;

	spin_unlock_irqrestore(&ch->lock, irq_flags);

	/* Callback should be called without any lock */
	req->complete(req);
	return 0;
}
EXPORT_SYMBOL(tegra_dma_dequeue_req);

bool tegra_dma_is_empty(struct tegra_dma_channel *ch)
{
	unsigned long irq_flags;
	bool is_empty;

	spin_lock_irqsave(&ch->lock, irq_flags);
	if (list_empty(&ch->list))
		is_empty = true;
	else
		is_empty = false;
	spin_unlock_irqrestore(&ch->lock, irq_flags);
	return is_empty;
}
EXPORT_SYMBOL(tegra_dma_is_empty);

bool tegra_dma_is_req_inflight(struct tegra_dma_channel *ch,
	struct tegra_dma_req *_req)
{
	unsigned long irq_flags;
	struct tegra_dma_req *req;

	spin_lock_irqsave(&ch->lock, irq_flags);
	list_for_each_entry(req, &ch->list, node) {
		if (req == _req) {
			spin_unlock_irqrestore(&ch->lock, irq_flags);
			return true;
		}
	}
	spin_unlock_irqrestore(&ch->lock, irq_flags);
	return false;
}
EXPORT_SYMBOL(tegra_dma_is_req_inflight);

int tegra_dma_enqueue_req(struct tegra_dma_channel *ch,
	struct tegra_dma_req *req)
{
	unsigned long irq_flags;
	struct tegra_dma_req *_req;
	int start_dma = 0;

	if (req->size > TEGRA_DMA_MAX_TRANSFER_SIZE ||
		req->source_addr & 0x3 || req->dest_addr & 0x3) {
		pr_err("Invalid DMA request for channel %d\n", ch->id);
		return -EINVAL;
	}

	spin_lock_irqsave(&ch->lock, irq_flags);

	list_for_each_entry(_req, &ch->list, node) {
		if (req == _req) {
		    spin_unlock_irqrestore(&ch->lock, irq_flags);
		    return -EEXIST;
		}
	}

	req->bytes_transferred = 0;
	req->status = 0;
	/* STATUS_EMPTY just means the DMA hasn't processed the buf yet. */
	req->buffer_status = TEGRA_DMA_REQ_BUF_STATUS_EMPTY;
	if (list_empty(&ch->list))
		start_dma = 1;

	list_add_tail(&req->node, &ch->list);

	if (start_dma)
		tegra_dma_update_hw(ch, req);
	/* Check to see if this request needs to be pushed immediately.
	 * For continuous single-buffer DMA:
	 * The first buffer is always in-flight.  The 2nd buffer should
	 * also be in-flight.  The 3rd buffer becomes in-flight when the
	 * first is completed in the interrupt.
	 */
	else if (ch->mode & TEGRA_DMA_MODE_CONTINUOUS_SINGLE) {
		struct tegra_dma_req *first_req, *second_req;
		first_req = list_entry(ch->list.next,
					typeof(*first_req), node);
		second_req = list_entry(first_req->node.next,
					typeof(*second_req), node);
		if (second_req == req) {
			unsigned long status =
				readl(ch->addr + APB_DMA_CHAN_STA);
			if (!(status & STA_ISE_EOC))
				tegra_dma_update_hw_partial(ch, req);
			/* Handle the case where the IRQ fired while we're
			 * writing the interrupts.
			 */
			if (status & STA_ISE_EOC) {
				/* Interrupt fired, let the IRQ stop/restart
				 * the DMA with this buffer in a clean way.
				 */
				req->status = TEGRA_DMA_REQ_SUCCESS;
			}
		}
	}

	spin_unlock_irqrestore(&ch->lock, irq_flags);

	return 0;
}
EXPORT_SYMBOL(tegra_dma_enqueue_req);

struct tegra_dma_channel *tegra_dma_allocate_channel(int mode)
{
	int channel;
	struct tegra_dma_channel *ch = NULL;

	mutex_lock(&tegra_dma_lock);

	/* first channel is the shared channel */
	if (mode & TEGRA_DMA_SHARED) {
		channel = TEGRA_SYSTEM_DMA_CH_MIN;
	} else {
		channel = find_first_zero_bit(channel_usage,
			ARRAY_SIZE(dma_channels));
		if (channel >= ARRAY_SIZE(dma_channels)) {
			pr_err("%s: failed to allocate a DMA channel",
				__func__);
			goto out;
		}
	}
	__set_bit(channel, channel_usage);
	ch = &dma_channels[channel];
	ch->mode = mode;

out:
	mutex_unlock(&tegra_dma_lock);
	return ch;
}
EXPORT_SYMBOL(tegra_dma_allocate_channel);

void tegra_dma_free_channel(struct tegra_dma_channel *ch)
{
	if (ch->mode & TEGRA_DMA_SHARED)
		return;
	tegra_dma_cancel(ch);
	mutex_lock(&tegra_dma_lock);
	__clear_bit(ch->id, channel_usage);
	mutex_unlock(&tegra_dma_lock);
}
EXPORT_SYMBOL(tegra_dma_free_channel);

static void tegra_dma_update_hw_partial(struct tegra_dma_channel *ch,
	struct tegra_dma_req *req)
{
	u32 apb_ptr;
	u32 ahb_ptr;
	u32 csr;

	if (req->to_memory) {
		apb_ptr = req->source_addr;
		ahb_ptr = req->dest_addr;
	} else {
		apb_ptr = req->dest_addr;
		ahb_ptr = req->source_addr;
	}
	writel(apb_ptr, ch->addr + APB_DMA_CHAN_APB_PTR);
	writel(ahb_ptr, ch->addr + APB_DMA_CHAN_AHB_PTR);

	if (ch->mode & TEGRA_DMA_MODE_CONTINUOUS_DOUBLE)
		ch->req_transfer_count = (req->size >> 3) - 1;
	else
		ch->req_transfer_count = (req->size >> 2) - 1;
	csr = readl(ch->addr + APB_DMA_CHAN_CSR);
	csr &= ~CSR_WCOUNT_MASK;
	csr |= ch->req_transfer_count << CSR_WCOUNT_SHIFT;
	writel(csr, ch->addr + APB_DMA_CHAN_CSR);

	req->status = TEGRA_DMA_REQ_INFLIGHT;
	return;
}

static void tegra_dma_update_hw(struct tegra_dma_channel *ch,
	struct tegra_dma_req *req)
{
	int ahb_addr_wrap;
	int apb_addr_wrap;
	int ahb_bus_width;
	int apb_bus_width;
	int index;

	u32 ahb_seq;
	u32 apb_seq;
	u32 ahb_ptr;
	u32 apb_ptr;
	u32 csr;

	csr = CSR_IE_EOC | CSR_FLOW;
	ahb_seq = AHB_SEQ_INTR_ENB | AHB_SEQ_BURST_1;
	apb_seq = 0;

	csr |= req->req_sel << CSR_REQ_SEL_SHIFT;

	ch->req_transfer_count = (req->size >> 2) - 1;

	/* One shot mode is always single buffered.  Continuous mode could
	 * support either.
	 */
	if (ch->mode & TEGRA_DMA_MODE_ONESHOT) {
		csr |= CSR_ONCE;
	} else if (ch->mode & TEGRA_DMA_MODE_CONTINUOUS_DOUBLE) {
		ahb_seq |= AHB_SEQ_DBL_BUF;
		/* We want an interrupt halfway through, then on the
		 * completion.  The double buffer means 2 interrupts
		 * pass before the DMA HW latches a new AHB_PTR etc.
		 */
		ch->req_transfer_count = (req->size >> 3) - 1;
	}
	csr |= ch->req_transfer_count << CSR_WCOUNT_SHIFT;

	if (req->to_memory) {
		apb_ptr = req->source_addr;
		ahb_ptr = req->dest_addr;

		apb_addr_wrap = req->source_wrap;
		ahb_addr_wrap = req->dest_wrap;
		apb_bus_width = req->source_bus_width;
		ahb_bus_width = req->dest_bus_width;

	} else {
		csr |= CSR_DIR;
		apb_ptr = req->dest_addr;
		ahb_ptr = req->source_addr;

		apb_addr_wrap = req->dest_wrap;
		ahb_addr_wrap = req->source_wrap;
		apb_bus_width = req->dest_bus_width;
		ahb_bus_width = req->source_bus_width;
	}

	apb_addr_wrap >>= 2;
	ahb_addr_wrap >>= 2;

	/* set address wrap for APB size */
	index = 0;
	do  {
		if (apb_addr_wrap_table[index] == apb_addr_wrap)
			break;
		index++;
	} while (index < ARRAY_SIZE(apb_addr_wrap_table));
	BUG_ON(index == ARRAY_SIZE(apb_addr_wrap_table));
	apb_seq |= index << APB_SEQ_WRAP_SHIFT;

	/* set address wrap for AHB size */
	index = 0;
	do  {
		if (ahb_addr_wrap_table[index] == ahb_addr_wrap)
			break;
		index++;
	} while (index < ARRAY_SIZE(ahb_addr_wrap_table));
	BUG_ON(index == ARRAY_SIZE(ahb_addr_wrap_table));
	ahb_seq |= index << AHB_SEQ_WRAP_SHIFT;

	for (index = 0; index < ARRAY_SIZE(bus_width_table); index++) {
		if (bus_width_table[index] == ahb_bus_width)
			break;
	}
	BUG_ON(index == ARRAY_SIZE(bus_width_table));
	ahb_seq |= index << AHB_SEQ_BUS_WIDTH_SHIFT;

	for (index = 0; index < ARRAY_SIZE(bus_width_table); index++) {
		if (bus_width_table[index] == apb_bus_width)
			break;
	}
	BUG_ON(index == ARRAY_SIZE(bus_width_table));
	apb_seq |= index << APB_SEQ_BUS_WIDTH_SHIFT;

	writel(csr, ch->addr + APB_DMA_CHAN_CSR);
	writel(apb_seq, ch->addr + APB_DMA_CHAN_APB_SEQ);
	writel(apb_ptr, ch->addr + APB_DMA_CHAN_APB_PTR);
	writel(ahb_seq, ch->addr + APB_DMA_CHAN_AHB_SEQ);
	writel(ahb_ptr, ch->addr + APB_DMA_CHAN_AHB_PTR);

	csr |= CSR_ENB;
	writel(csr, ch->addr + APB_DMA_CHAN_CSR);

	req->status = TEGRA_DMA_REQ_INFLIGHT;
}

static void handle_oneshot_dma(struct tegra_dma_channel *ch)
{
	struct tegra_dma_req *req;
	unsigned long irq_flags;

	spin_lock_irqsave(&ch->lock, irq_flags);
	if (list_empty(&ch->list)) {
		spin_unlock_irqrestore(&ch->lock, irq_flags);
		return;
	}

	req = list_entry(ch->list.next, typeof(*req), node);
	if (req) {
		list_del(&req->node);
		req->bytes_transferred = req->size;
		req->status = TEGRA_DMA_REQ_SUCCESS;

		spin_unlock_irqrestore(&ch->lock, irq_flags);
		/* Callback should be called without any lock */
		pr_debug("%s: transferred %d bytes\n", __func__,
			req->bytes_transferred);
		req->complete(req);
		spin_lock_irqsave(&ch->lock, irq_flags);
	}

	if (!list_empty(&ch->list)) {
		req = list_entry(ch->list.next, typeof(*req), node);
		/* the complete function we just called may have enqueued
		   another req, in which case dma has already started */
		if (req->status != TEGRA_DMA_REQ_INFLIGHT)
			tegra_dma_update_hw(ch, req);
	}
	spin_unlock_irqrestore(&ch->lock, irq_flags);
}

static void handle_continuous_dbl_dma(struct tegra_dma_channel *ch)
{
	struct tegra_dma_req *req;
	struct tegra_dma_req *next_req;
	unsigned long irq_flags;

	spin_lock_irqsave(&ch->lock, irq_flags);
	if (list_empty(&ch->list)) {
		spin_unlock_irqrestore(&ch->lock, irq_flags);
		return;
	}

	req = list_entry(ch->list.next, typeof(*req), node);
	if (req) {
		if (req->buffer_status == TEGRA_DMA_REQ_BUF_STATUS_EMPTY) {
			bool is_dma_ping_complete;
			is_dma_ping_complete =
				!!(readl(ch->addr + APB_DMA_CHAN_STA) &
					STA_PING_PONG);
			if (req->to_memory)
				is_dma_ping_complete = !is_dma_ping_complete;
			/* Out of sync - Release current buffer */
			if (!is_dma_ping_complete) {
				req->buffer_status =
						TEGRA_DMA_REQ_BUF_STATUS_FULL;
				req->bytes_transferred = req->size;
				req->status = TEGRA_DMA_REQ_SUCCESS;
				tegra_dma_stop(ch);

				if (!list_is_last(&req->node, &ch->list)) {
					next_req = list_entry(req->node.next,
						typeof(*next_req), node);
					tegra_dma_update_hw(ch, next_req);
				}

				list_del(&req->node);

				/* DMA lock is NOT held when callbak is
				 * called. */
				spin_unlock_irqrestore(&ch->lock, irq_flags);
				req->complete(req);
				return;
			}
			/* Load the next request into the hardware, if
			 * available. */
			if (!list_is_last(&req->node, &ch->list)) {
				next_req = list_entry(req->node.next,
					typeof(*next_req), node);
				tegra_dma_update_hw_partial(ch, next_req);
			}
			req->buffer_status = TEGRA_DMA_REQ_BUF_STATUS_HALF_FULL;
			req->status = TEGRA_DMA_REQ_SUCCESS;
			/* DMA lock is NOT held when callback is called */
			spin_unlock_irqrestore(&ch->lock, irq_flags);
			if (likely(req->threshold))
				req->threshold(req);
			return;

		} else if (req->buffer_status ==
			TEGRA_DMA_REQ_BUF_STATUS_HALF_FULL) {
			/* Callback when the buffer is completely full (i.e on
			 * the second  interrupt */

			req->buffer_status = TEGRA_DMA_REQ_BUF_STATUS_FULL;
			req->bytes_transferred = req->size;
			req->status = TEGRA_DMA_REQ_SUCCESS;
			if (list_is_last(&req->node, &ch->list))
				tegra_dma_stop(ch);
			else {
				/* It may be possible that req came after
				 * half dma complete so it need to start
				 * immediately */
				next_req = list_entry(req->node.next,
						typeof(*next_req), node);
				if (next_req->status !=
						TEGRA_DMA_REQ_INFLIGHT) {
					tegra_dma_stop(ch);
					tegra_dma_update_hw(ch, next_req);
				}
			}

			list_del(&req->node);

			/* DMA lock is NOT held when callbak is called */
			spin_unlock_irqrestore(&ch->lock, irq_flags);
			req->complete(req);
			return;

		} else {
			tegra_dma_stop(ch);
			/* Dma should be stop much earlier */
			BUG();
		}
	}
	spin_unlock_irqrestore(&ch->lock, irq_flags);
}

static void handle_continuous_sngl_dma(struct tegra_dma_channel *ch)
{
	struct tegra_dma_req *req;
	struct tegra_dma_req *next_req;
	struct tegra_dma_req *next_next_req;
	unsigned long irq_flags;

	spin_lock_irqsave(&ch->lock, irq_flags);
	if (list_empty(&ch->list)) {
		tegra_dma_stop(ch);
		spin_unlock_irqrestore(&ch->lock, irq_flags);
		pr_err("%s: No requests in the list.\n", __func__);
		return;
	}
	req = list_entry(ch->list.next, typeof(*req), node);
	if (!req || (req->buffer_status == TEGRA_DMA_REQ_BUF_STATUS_FULL)) {
		tegra_dma_stop(ch);
		spin_unlock_irqrestore(&ch->lock, irq_flags);
		pr_err("%s: DMA complete irq without corresponding req\n",
				__func__);
		return;
	}

	/* Handle the case when buffer is completely full */
	req->bytes_transferred = req->size;
	req->buffer_status = TEGRA_DMA_REQ_BUF_STATUS_FULL;
	req->status = TEGRA_DMA_REQ_SUCCESS;
	if (list_is_last(&req->node, &ch->list)) {
		pr_debug("%s: stop\n", __func__);
		tegra_dma_stop(ch);
	} else {
		/* The next entry should have already been queued and is now
		 * in the middle of xfer.  We can then write the next->next one
		 * if it exists.
		 */
		next_req = list_entry(req->node.next, typeof(*next_req), node);
		if (next_req->status != TEGRA_DMA_REQ_INFLIGHT) {
			pr_debug("%s: interrupt during enqueue\n", __func__);
			tegra_dma_stop(ch);
			tegra_dma_update_hw(ch, next_req);
		} else if (!list_is_last(&next_req->node, &ch->list)) {
			next_next_req = list_entry(next_req->node.next,
						typeof(*next_next_req), node);
			tegra_dma_update_hw_partial(ch, next_next_req);
		}
	}
	list_del(&req->node);
	spin_unlock_irqrestore(&ch->lock, irq_flags);
	req->complete(req);
}

static irqreturn_t dma_isr(int irq, void *data)
{
	struct tegra_dma_channel *ch = data;
	unsigned long status;

	status = readl(ch->addr + APB_DMA_CHAN_STA);
	if (status & STA_ISE_EOC)
		writel(status, ch->addr + APB_DMA_CHAN_STA);
	else {
		pr_warning("Got a spurious ISR for DMA channel %d\n", ch->id);
		return IRQ_HANDLED;
	}

	if (ch->mode & TEGRA_DMA_MODE_ONESHOT)
		handle_oneshot_dma(ch);
	else if (ch->mode & TEGRA_DMA_MODE_CONTINUOUS_DOUBLE)
		handle_continuous_dbl_dma(ch);
	else if (ch->mode & TEGRA_DMA_MODE_CONTINUOUS_SINGLE)
		handle_continuous_sngl_dma(ch);
	else
		pr_err("Bad channel mode for DMA ISR to handle\n");
	return IRQ_HANDLED;
}

int __init tegra_dma_init(void)
{
	int ret = 0;
	int i;
	unsigned int irq;
	void __iomem *addr;

	addr = IO_ADDRESS(TEGRA_APB_DMA_BASE);
	writel(GEN_ENABLE, addr + APB_DMA_GEN);
	writel(0, addr + APB_DMA_CNTRL);
	writel(0xFFFFFFFFul >> (31 - TEGRA_SYSTEM_DMA_CH_MAX),
	       addr + APB_DMA_IRQ_MASK_SET);

	memset(channel_usage, 0, sizeof(channel_usage));
	memset(dma_channels, 0, sizeof(dma_channels));

	/* Reserve all the channels we are not supposed to touch */
	for (i = 0; i < TEGRA_SYSTEM_DMA_CH_MIN; i++)
		__set_bit(i, channel_usage);

	for (i = TEGRA_SYSTEM_DMA_CH_MIN; i <= TEGRA_SYSTEM_DMA_CH_MAX; i++) {
		struct tegra_dma_channel *ch = &dma_channels[i];

		__clear_bit(i, channel_usage);

		ch->id = i;
		snprintf(ch->name, TEGRA_DMA_NAME_SIZE, "dma_channel_%d", i);

		ch->addr = IO_ADDRESS(TEGRA_APB_DMA_CH0_BASE +
			TEGRA_APB_DMA_CH0_SIZE * i);

		spin_lock_init(&ch->lock);
		INIT_LIST_HEAD(&ch->list);

		irq = INT_APB_DMA_CH0 + i;
		ret = request_irq(irq, dma_isr, 0, dma_channels[i].name, ch);
		if (ret) {
			pr_err("Failed to register IRQ %d for DMA %d\n",
				irq, i);
			goto fail;
		}
		ch->irq = irq;
	}
	/* mark the shared channel allocated */
	__set_bit(TEGRA_SYSTEM_DMA_CH_MIN, channel_usage);

	for (i = TEGRA_SYSTEM_DMA_CH_MAX+1; i < NV_DMA_MAX_CHANNELS; i++)
		__set_bit(i, channel_usage);

	return ret;
fail:
	writel(0, addr + APB_DMA_GEN);
	for (i = TEGRA_SYSTEM_DMA_CH_MIN; i <= TEGRA_SYSTEM_DMA_CH_MAX; i++) {
		struct tegra_dma_channel *ch = &dma_channels[i];
		if (ch->irq)
			free_irq(ch->irq, ch);
	}
	return ret;
}

#ifdef CONFIG_PM
static u32 apb_dma[5*TEGRA_SYSTEM_DMA_CH_NR + 3];

void tegra_dma_suspend(void)
{
	void __iomem *addr = IO_ADDRESS(TEGRA_APB_DMA_BASE);
	u32 *ctx = apb_dma;
	int i;

	*ctx++ = readl(addr + APB_DMA_GEN);
	*ctx++ = readl(addr + APB_DMA_CNTRL);
	*ctx++ = readl(addr + APB_DMA_IRQ_MASK);

	for (i = 0; i < TEGRA_SYSTEM_DMA_CH_NR; i++) {
		addr = IO_ADDRESS(TEGRA_APB_DMA_CH0_BASE +
				  TEGRA_APB_DMA_CH0_SIZE * i);

		*ctx++ = readl(addr + APB_DMA_CHAN_CSR);
		*ctx++ = readl(addr + APB_DMA_CHAN_AHB_PTR);
		*ctx++ = readl(addr + APB_DMA_CHAN_AHB_SEQ);
		*ctx++ = readl(addr + APB_DMA_CHAN_APB_PTR);
		*ctx++ = readl(addr + APB_DMA_CHAN_APB_SEQ);
	}
}

void tegra_dma_resume(void)
{
	void __iomem *addr = IO_ADDRESS(TEGRA_APB_DMA_BASE);
	u32 *ctx = apb_dma;
	int i;

	writel(*ctx++, addr + APB_DMA_GEN);
	writel(*ctx++, addr + APB_DMA_CNTRL);
	writel(*ctx++, addr + APB_DMA_IRQ_MASK);

	for (i = 0; i < TEGRA_SYSTEM_DMA_CH_NR; i++) {
		addr = IO_ADDRESS(TEGRA_APB_DMA_CH0_BASE +
				  TEGRA_APB_DMA_CH0_SIZE * i);

		writel(*ctx++, addr + APB_DMA_CHAN_CSR);
		writel(*ctx++, addr + APB_DMA_CHAN_AHB_PTR);
		writel(*ctx++, addr + APB_DMA_CHAN_AHB_SEQ);
		writel(*ctx++, addr + APB_DMA_CHAN_APB_PTR);
		writel(*ctx++, addr + APB_DMA_CHAN_APB_SEQ);
	}
}

#endif

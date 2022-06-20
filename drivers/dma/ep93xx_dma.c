// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Driver for the Cirrus Logic EP93xx DMA Controller
 *
 * Copyright (C) 2011 Mika Westerberg
 *
 * DMA M2P implementation is based on the original
 * arch/arm/mach-ep93xx/dma-m2p.c which has following copyrights:
 *
 *   Copyright (C) 2006 Lennert Buytenhek <buytenh@wantstofly.org>
 *   Copyright (C) 2006 Applied Data Systems
 *   Copyright (C) 2009 Ryan Mallon <rmallon@gmail.com>
 *
 * This driver is based on dw_dmac and amba-pl08x drivers.
 */

#include <linux/clk.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/dmaengine.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include <linux/platform_data/dma-ep93xx.h>

#include "dmaengine.h"

/* M2P registers */
#define M2P_CONTROL			0x0000
#define M2P_CONTROL_STALLINT		BIT(0)
#define M2P_CONTROL_NFBINT		BIT(1)
#define M2P_CONTROL_CH_ERROR_INT	BIT(3)
#define M2P_CONTROL_ENABLE		BIT(4)
#define M2P_CONTROL_ICE			BIT(6)

#define M2P_INTERRUPT			0x0004
#define M2P_INTERRUPT_STALL		BIT(0)
#define M2P_INTERRUPT_NFB		BIT(1)
#define M2P_INTERRUPT_ERROR		BIT(3)

#define M2P_PPALLOC			0x0008
#define M2P_STATUS			0x000c

#define M2P_MAXCNT0			0x0020
#define M2P_BASE0			0x0024
#define M2P_MAXCNT1			0x0030
#define M2P_BASE1			0x0034

#define M2P_STATE_IDLE			0
#define M2P_STATE_STALL			1
#define M2P_STATE_ON			2
#define M2P_STATE_NEXT			3

/* M2M registers */
#define M2M_CONTROL			0x0000
#define M2M_CONTROL_DONEINT		BIT(2)
#define M2M_CONTROL_ENABLE		BIT(3)
#define M2M_CONTROL_START		BIT(4)
#define M2M_CONTROL_DAH			BIT(11)
#define M2M_CONTROL_SAH			BIT(12)
#define M2M_CONTROL_PW_SHIFT		9
#define M2M_CONTROL_PW_8		(0 << M2M_CONTROL_PW_SHIFT)
#define M2M_CONTROL_PW_16		(1 << M2M_CONTROL_PW_SHIFT)
#define M2M_CONTROL_PW_32		(2 << M2M_CONTROL_PW_SHIFT)
#define M2M_CONTROL_PW_MASK		(3 << M2M_CONTROL_PW_SHIFT)
#define M2M_CONTROL_TM_SHIFT		13
#define M2M_CONTROL_TM_TX		(1 << M2M_CONTROL_TM_SHIFT)
#define M2M_CONTROL_TM_RX		(2 << M2M_CONTROL_TM_SHIFT)
#define M2M_CONTROL_NFBINT		BIT(21)
#define M2M_CONTROL_RSS_SHIFT		22
#define M2M_CONTROL_RSS_SSPRX		(1 << M2M_CONTROL_RSS_SHIFT)
#define M2M_CONTROL_RSS_SSPTX		(2 << M2M_CONTROL_RSS_SHIFT)
#define M2M_CONTROL_RSS_IDE		(3 << M2M_CONTROL_RSS_SHIFT)
#define M2M_CONTROL_NO_HDSK		BIT(24)
#define M2M_CONTROL_PWSC_SHIFT		25

#define M2M_INTERRUPT			0x0004
#define M2M_INTERRUPT_MASK		6

#define M2M_STATUS			0x000c
#define M2M_STATUS_CTL_SHIFT		1
#define M2M_STATUS_CTL_IDLE		(0 << M2M_STATUS_CTL_SHIFT)
#define M2M_STATUS_CTL_STALL		(1 << M2M_STATUS_CTL_SHIFT)
#define M2M_STATUS_CTL_MEMRD		(2 << M2M_STATUS_CTL_SHIFT)
#define M2M_STATUS_CTL_MEMWR		(3 << M2M_STATUS_CTL_SHIFT)
#define M2M_STATUS_CTL_BWCWAIT		(4 << M2M_STATUS_CTL_SHIFT)
#define M2M_STATUS_CTL_MASK		(7 << M2M_STATUS_CTL_SHIFT)
#define M2M_STATUS_BUF_SHIFT		4
#define M2M_STATUS_BUF_NO		(0 << M2M_STATUS_BUF_SHIFT)
#define M2M_STATUS_BUF_ON		(1 << M2M_STATUS_BUF_SHIFT)
#define M2M_STATUS_BUF_NEXT		(2 << M2M_STATUS_BUF_SHIFT)
#define M2M_STATUS_BUF_MASK		(3 << M2M_STATUS_BUF_SHIFT)
#define M2M_STATUS_DONE			BIT(6)

#define M2M_BCR0			0x0010
#define M2M_BCR1			0x0014
#define M2M_SAR_BASE0			0x0018
#define M2M_SAR_BASE1			0x001c
#define M2M_DAR_BASE0			0x002c
#define M2M_DAR_BASE1			0x0030

#define DMA_MAX_CHAN_BYTES		0xffff
#define DMA_MAX_CHAN_DESCRIPTORS	32

struct ep93xx_dma_engine;
static int ep93xx_dma_slave_config_write(struct dma_chan *chan,
					 enum dma_transfer_direction dir,
					 struct dma_slave_config *config);

/**
 * struct ep93xx_dma_desc - EP93xx specific transaction descriptor
 * @src_addr: source address of the transaction
 * @dst_addr: destination address of the transaction
 * @size: size of the transaction (in bytes)
 * @complete: this descriptor is completed
 * @txd: dmaengine API descriptor
 * @tx_list: list of linked descriptors
 * @node: link used for putting this into a channel queue
 */
struct ep93xx_dma_desc {
	u32				src_addr;
	u32				dst_addr;
	size_t				size;
	bool				complete;
	struct dma_async_tx_descriptor	txd;
	struct list_head		tx_list;
	struct list_head		node;
};

/**
 * struct ep93xx_dma_chan - an EP93xx DMA M2P/M2M channel
 * @chan: dmaengine API channel
 * @edma: pointer to the engine device
 * @regs: memory mapped registers
 * @irq: interrupt number of the channel
 * @clk: clock used by this channel
 * @tasklet: channel specific tasklet used for callbacks
 * @lock: lock protecting the fields following
 * @flags: flags for the channel
 * @buffer: which buffer to use next (0/1)
 * @active: flattened chain of descriptors currently being processed
 * @queue: pending descriptors which are handled next
 * @free_list: list of free descriptors which can be used
 * @runtime_addr: physical address currently used as dest/src (M2M only). This
 *                is set via .device_config before slave operation is
 *                prepared
 * @runtime_ctrl: M2M runtime values for the control register.
 * @slave_config: slave configuration
 *
 * As EP93xx DMA controller doesn't support real chained DMA descriptors we
 * will have slightly different scheme here: @active points to a head of
 * flattened DMA descriptor chain.
 *
 * @queue holds pending transactions. These are linked through the first
 * descriptor in the chain. When a descriptor is moved to the @active queue,
 * the first and chained descriptors are flattened into a single list.
 *
 * @chan.private holds pointer to &struct ep93xx_dma_data which contains
 * necessary channel configuration information. For memcpy channels this must
 * be %NULL.
 */
struct ep93xx_dma_chan {
	struct dma_chan			chan;
	const struct ep93xx_dma_engine	*edma;
	void __iomem			*regs;
	int				irq;
	struct clk			*clk;
	struct tasklet_struct		tasklet;
	/* protects the fields following */
	spinlock_t			lock;
	unsigned long			flags;
/* Channel is configured for cyclic transfers */
#define EP93XX_DMA_IS_CYCLIC		0

	int				buffer;
	struct list_head		active;
	struct list_head		queue;
	struct list_head		free_list;
	u32				runtime_addr;
	u32				runtime_ctrl;
	struct dma_slave_config		slave_config;
};

/**
 * struct ep93xx_dma_engine - the EP93xx DMA engine instance
 * @dma_dev: holds the dmaengine device
 * @m2m: is this an M2M or M2P device
 * @hw_setup: method which sets the channel up for operation
 * @hw_synchronize: synchronizes DMA channel termination to current context
 * @hw_shutdown: shuts the channel down and flushes whatever is left
 * @hw_submit: pushes active descriptor(s) to the hardware
 * @hw_interrupt: handle the interrupt
 * @num_channels: number of channels for this instance
 * @channels: array of channels
 *
 * There is one instance of this struct for the M2P channels and one for the
 * M2M channels. hw_xxx() methods are used to perform operations which are
 * different on M2M and M2P channels. These methods are called with channel
 * lock held and interrupts disabled so they cannot sleep.
 */
struct ep93xx_dma_engine {
	struct dma_device	dma_dev;
	bool			m2m;
	int			(*hw_setup)(struct ep93xx_dma_chan *);
	void			(*hw_synchronize)(struct ep93xx_dma_chan *);
	void			(*hw_shutdown)(struct ep93xx_dma_chan *);
	void			(*hw_submit)(struct ep93xx_dma_chan *);
	int			(*hw_interrupt)(struct ep93xx_dma_chan *);
#define INTERRUPT_UNKNOWN	0
#define INTERRUPT_DONE		1
#define INTERRUPT_NEXT_BUFFER	2

	size_t			num_channels;
	struct ep93xx_dma_chan	channels[];
};

static inline struct device *chan2dev(struct ep93xx_dma_chan *edmac)
{
	return &edmac->chan.dev->device;
}

static struct ep93xx_dma_chan *to_ep93xx_dma_chan(struct dma_chan *chan)
{
	return container_of(chan, struct ep93xx_dma_chan, chan);
}

/**
 * ep93xx_dma_set_active - set new active descriptor chain
 * @edmac: channel
 * @desc: head of the new active descriptor chain
 *
 * Sets @desc to be the head of the new active descriptor chain. This is the
 * chain which is processed next. The active list must be empty before calling
 * this function.
 *
 * Called with @edmac->lock held and interrupts disabled.
 */
static void ep93xx_dma_set_active(struct ep93xx_dma_chan *edmac,
				  struct ep93xx_dma_desc *desc)
{
	BUG_ON(!list_empty(&edmac->active));

	list_add_tail(&desc->node, &edmac->active);

	/* Flatten the @desc->tx_list chain into @edmac->active list */
	while (!list_empty(&desc->tx_list)) {
		struct ep93xx_dma_desc *d = list_first_entry(&desc->tx_list,
			struct ep93xx_dma_desc, node);

		/*
		 * We copy the callback parameters from the first descriptor
		 * to all the chained descriptors. This way we can call the
		 * callback without having to find out the first descriptor in
		 * the chain. Useful for cyclic transfers.
		 */
		d->txd.callback = desc->txd.callback;
		d->txd.callback_param = desc->txd.callback_param;

		list_move_tail(&d->node, &edmac->active);
	}
}

/* Called with @edmac->lock held and interrupts disabled */
static struct ep93xx_dma_desc *
ep93xx_dma_get_active(struct ep93xx_dma_chan *edmac)
{
	return list_first_entry_or_null(&edmac->active,
					struct ep93xx_dma_desc, node);
}

/**
 * ep93xx_dma_advance_active - advances to the next active descriptor
 * @edmac: channel
 *
 * Function advances active descriptor to the next in the @edmac->active and
 * returns %true if we still have descriptors in the chain to process.
 * Otherwise returns %false.
 *
 * When the channel is in cyclic mode always returns %true.
 *
 * Called with @edmac->lock held and interrupts disabled.
 */
static bool ep93xx_dma_advance_active(struct ep93xx_dma_chan *edmac)
{
	struct ep93xx_dma_desc *desc;

	list_rotate_left(&edmac->active);

	if (test_bit(EP93XX_DMA_IS_CYCLIC, &edmac->flags))
		return true;

	desc = ep93xx_dma_get_active(edmac);
	if (!desc)
		return false;

	/*
	 * If txd.cookie is set it means that we are back in the first
	 * descriptor in the chain and hence done with it.
	 */
	return !desc->txd.cookie;
}

/*
 * M2P DMA implementation
 */

static void m2p_set_control(struct ep93xx_dma_chan *edmac, u32 control)
{
	writel(control, edmac->regs + M2P_CONTROL);
	/*
	 * EP93xx User's Guide states that we must perform a dummy read after
	 * write to the control register.
	 */
	readl(edmac->regs + M2P_CONTROL);
}

static int m2p_hw_setup(struct ep93xx_dma_chan *edmac)
{
	struct ep93xx_dma_data *data = edmac->chan.private;
	u32 control;

	writel(data->port & 0xf, edmac->regs + M2P_PPALLOC);

	control = M2P_CONTROL_CH_ERROR_INT | M2P_CONTROL_ICE
		| M2P_CONTROL_ENABLE;
	m2p_set_control(edmac, control);

	edmac->buffer = 0;

	return 0;
}

static inline u32 m2p_channel_state(struct ep93xx_dma_chan *edmac)
{
	return (readl(edmac->regs + M2P_STATUS) >> 4) & 0x3;
}

static void m2p_hw_synchronize(struct ep93xx_dma_chan *edmac)
{
	unsigned long flags;
	u32 control;

	spin_lock_irqsave(&edmac->lock, flags);
	control = readl(edmac->regs + M2P_CONTROL);
	control &= ~(M2P_CONTROL_STALLINT | M2P_CONTROL_NFBINT);
	m2p_set_control(edmac, control);
	spin_unlock_irqrestore(&edmac->lock, flags);

	while (m2p_channel_state(edmac) >= M2P_STATE_ON)
		schedule();
}

static void m2p_hw_shutdown(struct ep93xx_dma_chan *edmac)
{
	m2p_set_control(edmac, 0);

	while (m2p_channel_state(edmac) != M2P_STATE_IDLE)
		dev_warn(chan2dev(edmac), "M2P: Not yet IDLE\n");
}

static void m2p_fill_desc(struct ep93xx_dma_chan *edmac)
{
	struct ep93xx_dma_desc *desc;
	u32 bus_addr;

	desc = ep93xx_dma_get_active(edmac);
	if (!desc) {
		dev_warn(chan2dev(edmac), "M2P: empty descriptor list\n");
		return;
	}

	if (ep93xx_dma_chan_direction(&edmac->chan) == DMA_MEM_TO_DEV)
		bus_addr = desc->src_addr;
	else
		bus_addr = desc->dst_addr;

	if (edmac->buffer == 0) {
		writel(desc->size, edmac->regs + M2P_MAXCNT0);
		writel(bus_addr, edmac->regs + M2P_BASE0);
	} else {
		writel(desc->size, edmac->regs + M2P_MAXCNT1);
		writel(bus_addr, edmac->regs + M2P_BASE1);
	}

	edmac->buffer ^= 1;
}

static void m2p_hw_submit(struct ep93xx_dma_chan *edmac)
{
	u32 control = readl(edmac->regs + M2P_CONTROL);

	m2p_fill_desc(edmac);
	control |= M2P_CONTROL_STALLINT;

	if (ep93xx_dma_advance_active(edmac)) {
		m2p_fill_desc(edmac);
		control |= M2P_CONTROL_NFBINT;
	}

	m2p_set_control(edmac, control);
}

static int m2p_hw_interrupt(struct ep93xx_dma_chan *edmac)
{
	u32 irq_status = readl(edmac->regs + M2P_INTERRUPT);
	u32 control;

	if (irq_status & M2P_INTERRUPT_ERROR) {
		struct ep93xx_dma_desc *desc = ep93xx_dma_get_active(edmac);

		/* Clear the error interrupt */
		writel(1, edmac->regs + M2P_INTERRUPT);

		/*
		 * It seems that there is no easy way of reporting errors back
		 * to client so we just report the error here and continue as
		 * usual.
		 *
		 * Revisit this when there is a mechanism to report back the
		 * errors.
		 */
		dev_err(chan2dev(edmac),
			"DMA transfer failed! Details:\n"
			"\tcookie	: %d\n"
			"\tsrc_addr	: 0x%08x\n"
			"\tdst_addr	: 0x%08x\n"
			"\tsize		: %zu\n",
			desc->txd.cookie, desc->src_addr, desc->dst_addr,
			desc->size);
	}

	/*
	 * Even latest E2 silicon revision sometimes assert STALL interrupt
	 * instead of NFB. Therefore we treat them equally, basing on the
	 * amount of data we still have to transfer.
	 */
	if (!(irq_status & (M2P_INTERRUPT_STALL | M2P_INTERRUPT_NFB)))
		return INTERRUPT_UNKNOWN;

	if (ep93xx_dma_advance_active(edmac)) {
		m2p_fill_desc(edmac);
		return INTERRUPT_NEXT_BUFFER;
	}

	/* Disable interrupts */
	control = readl(edmac->regs + M2P_CONTROL);
	control &= ~(M2P_CONTROL_STALLINT | M2P_CONTROL_NFBINT);
	m2p_set_control(edmac, control);

	return INTERRUPT_DONE;
}

/*
 * M2M DMA implementation
 */

static int m2m_hw_setup(struct ep93xx_dma_chan *edmac)
{
	const struct ep93xx_dma_data *data = edmac->chan.private;
	u32 control = 0;

	if (!data) {
		/* This is memcpy channel, nothing to configure */
		writel(control, edmac->regs + M2M_CONTROL);
		return 0;
	}

	switch (data->port) {
	case EP93XX_DMA_SSP:
		/*
		 * This was found via experimenting - anything less than 5
		 * causes the channel to perform only a partial transfer which
		 * leads to problems since we don't get DONE interrupt then.
		 */
		control = (5 << M2M_CONTROL_PWSC_SHIFT);
		control |= M2M_CONTROL_NO_HDSK;

		if (data->direction == DMA_MEM_TO_DEV) {
			control |= M2M_CONTROL_DAH;
			control |= M2M_CONTROL_TM_TX;
			control |= M2M_CONTROL_RSS_SSPTX;
		} else {
			control |= M2M_CONTROL_SAH;
			control |= M2M_CONTROL_TM_RX;
			control |= M2M_CONTROL_RSS_SSPRX;
		}
		break;

	case EP93XX_DMA_IDE:
		/*
		 * This IDE part is totally untested. Values below are taken
		 * from the EP93xx Users's Guide and might not be correct.
		 */
		if (data->direction == DMA_MEM_TO_DEV) {
			/* Worst case from the UG */
			control = (3 << M2M_CONTROL_PWSC_SHIFT);
			control |= M2M_CONTROL_DAH;
			control |= M2M_CONTROL_TM_TX;
		} else {
			control = (2 << M2M_CONTROL_PWSC_SHIFT);
			control |= M2M_CONTROL_SAH;
			control |= M2M_CONTROL_TM_RX;
		}

		control |= M2M_CONTROL_NO_HDSK;
		control |= M2M_CONTROL_RSS_IDE;
		control |= M2M_CONTROL_PW_16;
		break;

	default:
		return -EINVAL;
	}

	writel(control, edmac->regs + M2M_CONTROL);
	return 0;
}

static void m2m_hw_shutdown(struct ep93xx_dma_chan *edmac)
{
	/* Just disable the channel */
	writel(0, edmac->regs + M2M_CONTROL);
}

static void m2m_fill_desc(struct ep93xx_dma_chan *edmac)
{
	struct ep93xx_dma_desc *desc;

	desc = ep93xx_dma_get_active(edmac);
	if (!desc) {
		dev_warn(chan2dev(edmac), "M2M: empty descriptor list\n");
		return;
	}

	if (edmac->buffer == 0) {
		writel(desc->src_addr, edmac->regs + M2M_SAR_BASE0);
		writel(desc->dst_addr, edmac->regs + M2M_DAR_BASE0);
		writel(desc->size, edmac->regs + M2M_BCR0);
	} else {
		writel(desc->src_addr, edmac->regs + M2M_SAR_BASE1);
		writel(desc->dst_addr, edmac->regs + M2M_DAR_BASE1);
		writel(desc->size, edmac->regs + M2M_BCR1);
	}

	edmac->buffer ^= 1;
}

static void m2m_hw_submit(struct ep93xx_dma_chan *edmac)
{
	struct ep93xx_dma_data *data = edmac->chan.private;
	u32 control = readl(edmac->regs + M2M_CONTROL);

	/*
	 * Since we allow clients to configure PW (peripheral width) we always
	 * clear PW bits here and then set them according what is given in
	 * the runtime configuration.
	 */
	control &= ~M2M_CONTROL_PW_MASK;
	control |= edmac->runtime_ctrl;

	m2m_fill_desc(edmac);
	control |= M2M_CONTROL_DONEINT;

	if (ep93xx_dma_advance_active(edmac)) {
		m2m_fill_desc(edmac);
		control |= M2M_CONTROL_NFBINT;
	}

	/*
	 * Now we can finally enable the channel. For M2M channel this must be
	 * done _after_ the BCRx registers are programmed.
	 */
	control |= M2M_CONTROL_ENABLE;
	writel(control, edmac->regs + M2M_CONTROL);

	if (!data) {
		/*
		 * For memcpy channels the software trigger must be asserted
		 * in order to start the memcpy operation.
		 */
		control |= M2M_CONTROL_START;
		writel(control, edmac->regs + M2M_CONTROL);
	}
}

/*
 * According to EP93xx User's Guide, we should receive DONE interrupt when all
 * M2M DMA controller transactions complete normally. This is not always the
 * case - sometimes EP93xx M2M DMA asserts DONE interrupt when the DMA channel
 * is still running (channel Buffer FSM in DMA_BUF_ON state, and channel
 * Control FSM in DMA_MEM_RD state, observed at least in IDE-DMA operation).
 * In effect, disabling the channel when only DONE bit is set could stop
 * currently running DMA transfer. To avoid this, we use Buffer FSM and
 * Control FSM to check current state of DMA channel.
 */
static int m2m_hw_interrupt(struct ep93xx_dma_chan *edmac)
{
	u32 status = readl(edmac->regs + M2M_STATUS);
	u32 ctl_fsm = status & M2M_STATUS_CTL_MASK;
	u32 buf_fsm = status & M2M_STATUS_BUF_MASK;
	bool done = status & M2M_STATUS_DONE;
	bool last_done;
	u32 control;
	struct ep93xx_dma_desc *desc;

	/* Accept only DONE and NFB interrupts */
	if (!(readl(edmac->regs + M2M_INTERRUPT) & M2M_INTERRUPT_MASK))
		return INTERRUPT_UNKNOWN;

	if (done) {
		/* Clear the DONE bit */
		writel(0, edmac->regs + M2M_INTERRUPT);
	}

	/*
	 * Check whether we are done with descriptors or not. This, together
	 * with DMA channel state, determines action to take in interrupt.
	 */
	desc = ep93xx_dma_get_active(edmac);
	last_done = !desc || desc->txd.cookie;

	/*
	 * Use M2M DMA Buffer FSM and Control FSM to check current state of
	 * DMA channel. Using DONE and NFB bits from channel status register
	 * or bits from channel interrupt register is not reliable.
	 */
	if (!last_done &&
	    (buf_fsm == M2M_STATUS_BUF_NO ||
	     buf_fsm == M2M_STATUS_BUF_ON)) {
		/*
		 * Two buffers are ready for update when Buffer FSM is in
		 * DMA_NO_BUF state. Only one buffer can be prepared without
		 * disabling the channel or polling the DONE bit.
		 * To simplify things, always prepare only one buffer.
		 */
		if (ep93xx_dma_advance_active(edmac)) {
			m2m_fill_desc(edmac);
			if (done && !edmac->chan.private) {
				/* Software trigger for memcpy channel */
				control = readl(edmac->regs + M2M_CONTROL);
				control |= M2M_CONTROL_START;
				writel(control, edmac->regs + M2M_CONTROL);
			}
			return INTERRUPT_NEXT_BUFFER;
		} else {
			last_done = true;
		}
	}

	/*
	 * Disable the channel only when Buffer FSM is in DMA_NO_BUF state
	 * and Control FSM is in DMA_STALL state.
	 */
	if (last_done &&
	    buf_fsm == M2M_STATUS_BUF_NO &&
	    ctl_fsm == M2M_STATUS_CTL_STALL) {
		/* Disable interrupts and the channel */
		control = readl(edmac->regs + M2M_CONTROL);
		control &= ~(M2M_CONTROL_DONEINT | M2M_CONTROL_NFBINT
			    | M2M_CONTROL_ENABLE);
		writel(control, edmac->regs + M2M_CONTROL);
		return INTERRUPT_DONE;
	}

	/*
	 * Nothing to do this time.
	 */
	return INTERRUPT_NEXT_BUFFER;
}

/*
 * DMA engine API implementation
 */

static struct ep93xx_dma_desc *
ep93xx_dma_desc_get(struct ep93xx_dma_chan *edmac)
{
	struct ep93xx_dma_desc *desc, *_desc;
	struct ep93xx_dma_desc *ret = NULL;
	unsigned long flags;

	spin_lock_irqsave(&edmac->lock, flags);
	list_for_each_entry_safe(desc, _desc, &edmac->free_list, node) {
		if (async_tx_test_ack(&desc->txd)) {
			list_del_init(&desc->node);

			/* Re-initialize the descriptor */
			desc->src_addr = 0;
			desc->dst_addr = 0;
			desc->size = 0;
			desc->complete = false;
			desc->txd.cookie = 0;
			desc->txd.callback = NULL;
			desc->txd.callback_param = NULL;

			ret = desc;
			break;
		}
	}
	spin_unlock_irqrestore(&edmac->lock, flags);
	return ret;
}

static void ep93xx_dma_desc_put(struct ep93xx_dma_chan *edmac,
				struct ep93xx_dma_desc *desc)
{
	if (desc) {
		unsigned long flags;

		spin_lock_irqsave(&edmac->lock, flags);
		list_splice_init(&desc->tx_list, &edmac->free_list);
		list_add(&desc->node, &edmac->free_list);
		spin_unlock_irqrestore(&edmac->lock, flags);
	}
}

/**
 * ep93xx_dma_advance_work - start processing the next pending transaction
 * @edmac: channel
 *
 * If we have pending transactions queued and we are currently idling, this
 * function takes the next queued transaction from the @edmac->queue and
 * pushes it to the hardware for execution.
 */
static void ep93xx_dma_advance_work(struct ep93xx_dma_chan *edmac)
{
	struct ep93xx_dma_desc *new;
	unsigned long flags;

	spin_lock_irqsave(&edmac->lock, flags);
	if (!list_empty(&edmac->active) || list_empty(&edmac->queue)) {
		spin_unlock_irqrestore(&edmac->lock, flags);
		return;
	}

	/* Take the next descriptor from the pending queue */
	new = list_first_entry(&edmac->queue, struct ep93xx_dma_desc, node);
	list_del_init(&new->node);

	ep93xx_dma_set_active(edmac, new);

	/* Push it to the hardware */
	edmac->edma->hw_submit(edmac);
	spin_unlock_irqrestore(&edmac->lock, flags);
}

static void ep93xx_dma_tasklet(struct tasklet_struct *t)
{
	struct ep93xx_dma_chan *edmac = from_tasklet(edmac, t, tasklet);
	struct ep93xx_dma_desc *desc, *d;
	struct dmaengine_desc_callback cb;
	LIST_HEAD(list);

	memset(&cb, 0, sizeof(cb));
	spin_lock_irq(&edmac->lock);
	/*
	 * If dma_terminate_all() was called before we get to run, the active
	 * list has become empty. If that happens we aren't supposed to do
	 * anything more than call ep93xx_dma_advance_work().
	 */
	desc = ep93xx_dma_get_active(edmac);
	if (desc) {
		if (desc->complete) {
			/* mark descriptor complete for non cyclic case only */
			if (!test_bit(EP93XX_DMA_IS_CYCLIC, &edmac->flags))
				dma_cookie_complete(&desc->txd);
			list_splice_init(&edmac->active, &list);
		}
		dmaengine_desc_get_callback(&desc->txd, &cb);
	}
	spin_unlock_irq(&edmac->lock);

	/* Pick up the next descriptor from the queue */
	ep93xx_dma_advance_work(edmac);

	/* Now we can release all the chained descriptors */
	list_for_each_entry_safe(desc, d, &list, node) {
		dma_descriptor_unmap(&desc->txd);
		ep93xx_dma_desc_put(edmac, desc);
	}

	dmaengine_desc_callback_invoke(&cb, NULL);
}

static irqreturn_t ep93xx_dma_interrupt(int irq, void *dev_id)
{
	struct ep93xx_dma_chan *edmac = dev_id;
	struct ep93xx_dma_desc *desc;
	irqreturn_t ret = IRQ_HANDLED;

	spin_lock(&edmac->lock);

	desc = ep93xx_dma_get_active(edmac);
	if (!desc) {
		dev_warn(chan2dev(edmac),
			 "got interrupt while active list is empty\n");
		spin_unlock(&edmac->lock);
		return IRQ_NONE;
	}

	switch (edmac->edma->hw_interrupt(edmac)) {
	case INTERRUPT_DONE:
		desc->complete = true;
		tasklet_schedule(&edmac->tasklet);
		break;

	case INTERRUPT_NEXT_BUFFER:
		if (test_bit(EP93XX_DMA_IS_CYCLIC, &edmac->flags))
			tasklet_schedule(&edmac->tasklet);
		break;

	default:
		dev_warn(chan2dev(edmac), "unknown interrupt!\n");
		ret = IRQ_NONE;
		break;
	}

	spin_unlock(&edmac->lock);
	return ret;
}

/**
 * ep93xx_dma_tx_submit - set the prepared descriptor(s) to be executed
 * @tx: descriptor to be executed
 *
 * Function will execute given descriptor on the hardware or if the hardware
 * is busy, queue the descriptor to be executed later on. Returns cookie which
 * can be used to poll the status of the descriptor.
 */
static dma_cookie_t ep93xx_dma_tx_submit(struct dma_async_tx_descriptor *tx)
{
	struct ep93xx_dma_chan *edmac = to_ep93xx_dma_chan(tx->chan);
	struct ep93xx_dma_desc *desc;
	dma_cookie_t cookie;
	unsigned long flags;

	spin_lock_irqsave(&edmac->lock, flags);
	cookie = dma_cookie_assign(tx);

	desc = container_of(tx, struct ep93xx_dma_desc, txd);

	/*
	 * If nothing is currently prosessed, we push this descriptor
	 * directly to the hardware. Otherwise we put the descriptor
	 * to the pending queue.
	 */
	if (list_empty(&edmac->active)) {
		ep93xx_dma_set_active(edmac, desc);
		edmac->edma->hw_submit(edmac);
	} else {
		list_add_tail(&desc->node, &edmac->queue);
	}

	spin_unlock_irqrestore(&edmac->lock, flags);
	return cookie;
}

/**
 * ep93xx_dma_alloc_chan_resources - allocate resources for the channel
 * @chan: channel to allocate resources
 *
 * Function allocates necessary resources for the given DMA channel and
 * returns number of allocated descriptors for the channel. Negative errno
 * is returned in case of failure.
 */
static int ep93xx_dma_alloc_chan_resources(struct dma_chan *chan)
{
	struct ep93xx_dma_chan *edmac = to_ep93xx_dma_chan(chan);
	struct ep93xx_dma_data *data = chan->private;
	const char *name = dma_chan_name(chan);
	int ret, i;

	/* Sanity check the channel parameters */
	if (!edmac->edma->m2m) {
		if (!data)
			return -EINVAL;
		if (data->port < EP93XX_DMA_I2S1 ||
		    data->port > EP93XX_DMA_IRDA)
			return -EINVAL;
		if (data->direction != ep93xx_dma_chan_direction(chan))
			return -EINVAL;
	} else {
		if (data) {
			switch (data->port) {
			case EP93XX_DMA_SSP:
			case EP93XX_DMA_IDE:
				if (!is_slave_direction(data->direction))
					return -EINVAL;
				break;
			default:
				return -EINVAL;
			}
		}
	}

	if (data && data->name)
		name = data->name;

	ret = clk_prepare_enable(edmac->clk);
	if (ret)
		return ret;

	ret = request_irq(edmac->irq, ep93xx_dma_interrupt, 0, name, edmac);
	if (ret)
		goto fail_clk_disable;

	spin_lock_irq(&edmac->lock);
	dma_cookie_init(&edmac->chan);
	ret = edmac->edma->hw_setup(edmac);
	spin_unlock_irq(&edmac->lock);

	if (ret)
		goto fail_free_irq;

	for (i = 0; i < DMA_MAX_CHAN_DESCRIPTORS; i++) {
		struct ep93xx_dma_desc *desc;

		desc = kzalloc(sizeof(*desc), GFP_KERNEL);
		if (!desc) {
			dev_warn(chan2dev(edmac), "not enough descriptors\n");
			break;
		}

		INIT_LIST_HEAD(&desc->tx_list);

		dma_async_tx_descriptor_init(&desc->txd, chan);
		desc->txd.flags = DMA_CTRL_ACK;
		desc->txd.tx_submit = ep93xx_dma_tx_submit;

		ep93xx_dma_desc_put(edmac, desc);
	}

	return i;

fail_free_irq:
	free_irq(edmac->irq, edmac);
fail_clk_disable:
	clk_disable_unprepare(edmac->clk);

	return ret;
}

/**
 * ep93xx_dma_free_chan_resources - release resources for the channel
 * @chan: channel
 *
 * Function releases all the resources allocated for the given channel.
 * The channel must be idle when this is called.
 */
static void ep93xx_dma_free_chan_resources(struct dma_chan *chan)
{
	struct ep93xx_dma_chan *edmac = to_ep93xx_dma_chan(chan);
	struct ep93xx_dma_desc *desc, *d;
	unsigned long flags;
	LIST_HEAD(list);

	BUG_ON(!list_empty(&edmac->active));
	BUG_ON(!list_empty(&edmac->queue));

	spin_lock_irqsave(&edmac->lock, flags);
	edmac->edma->hw_shutdown(edmac);
	edmac->runtime_addr = 0;
	edmac->runtime_ctrl = 0;
	edmac->buffer = 0;
	list_splice_init(&edmac->free_list, &list);
	spin_unlock_irqrestore(&edmac->lock, flags);

	list_for_each_entry_safe(desc, d, &list, node)
		kfree(desc);

	clk_disable_unprepare(edmac->clk);
	free_irq(edmac->irq, edmac);
}

/**
 * ep93xx_dma_prep_dma_memcpy - prepare a memcpy DMA operation
 * @chan: channel
 * @dest: destination bus address
 * @src: source bus address
 * @len: size of the transaction
 * @flags: flags for the descriptor
 *
 * Returns a valid DMA descriptor or %NULL in case of failure.
 */
static struct dma_async_tx_descriptor *
ep93xx_dma_prep_dma_memcpy(struct dma_chan *chan, dma_addr_t dest,
			   dma_addr_t src, size_t len, unsigned long flags)
{
	struct ep93xx_dma_chan *edmac = to_ep93xx_dma_chan(chan);
	struct ep93xx_dma_desc *desc, *first;
	size_t bytes, offset;

	first = NULL;
	for (offset = 0; offset < len; offset += bytes) {
		desc = ep93xx_dma_desc_get(edmac);
		if (!desc) {
			dev_warn(chan2dev(edmac), "couldn't get descriptor\n");
			goto fail;
		}

		bytes = min_t(size_t, len - offset, DMA_MAX_CHAN_BYTES);

		desc->src_addr = src + offset;
		desc->dst_addr = dest + offset;
		desc->size = bytes;

		if (!first)
			first = desc;
		else
			list_add_tail(&desc->node, &first->tx_list);
	}

	first->txd.cookie = -EBUSY;
	first->txd.flags = flags;

	return &first->txd;
fail:
	ep93xx_dma_desc_put(edmac, first);
	return NULL;
}

/**
 * ep93xx_dma_prep_slave_sg - prepare a slave DMA operation
 * @chan: channel
 * @sgl: list of buffers to transfer
 * @sg_len: number of entries in @sgl
 * @dir: direction of tha DMA transfer
 * @flags: flags for the descriptor
 * @context: operation context (ignored)
 *
 * Returns a valid DMA descriptor or %NULL in case of failure.
 */
static struct dma_async_tx_descriptor *
ep93xx_dma_prep_slave_sg(struct dma_chan *chan, struct scatterlist *sgl,
			 unsigned int sg_len, enum dma_transfer_direction dir,
			 unsigned long flags, void *context)
{
	struct ep93xx_dma_chan *edmac = to_ep93xx_dma_chan(chan);
	struct ep93xx_dma_desc *desc, *first;
	struct scatterlist *sg;
	int i;

	if (!edmac->edma->m2m && dir != ep93xx_dma_chan_direction(chan)) {
		dev_warn(chan2dev(edmac),
			 "channel was configured with different direction\n");
		return NULL;
	}

	if (test_bit(EP93XX_DMA_IS_CYCLIC, &edmac->flags)) {
		dev_warn(chan2dev(edmac),
			 "channel is already used for cyclic transfers\n");
		return NULL;
	}

	ep93xx_dma_slave_config_write(chan, dir, &edmac->slave_config);

	first = NULL;
	for_each_sg(sgl, sg, sg_len, i) {
		size_t len = sg_dma_len(sg);

		if (len > DMA_MAX_CHAN_BYTES) {
			dev_warn(chan2dev(edmac), "too big transfer size %zu\n",
				 len);
			goto fail;
		}

		desc = ep93xx_dma_desc_get(edmac);
		if (!desc) {
			dev_warn(chan2dev(edmac), "couldn't get descriptor\n");
			goto fail;
		}

		if (dir == DMA_MEM_TO_DEV) {
			desc->src_addr = sg_dma_address(sg);
			desc->dst_addr = edmac->runtime_addr;
		} else {
			desc->src_addr = edmac->runtime_addr;
			desc->dst_addr = sg_dma_address(sg);
		}
		desc->size = len;

		if (!first)
			first = desc;
		else
			list_add_tail(&desc->node, &first->tx_list);
	}

	first->txd.cookie = -EBUSY;
	first->txd.flags = flags;

	return &first->txd;

fail:
	ep93xx_dma_desc_put(edmac, first);
	return NULL;
}

/**
 * ep93xx_dma_prep_dma_cyclic - prepare a cyclic DMA operation
 * @chan: channel
 * @dma_addr: DMA mapped address of the buffer
 * @buf_len: length of the buffer (in bytes)
 * @period_len: length of a single period
 * @dir: direction of the operation
 * @flags: tx descriptor status flags
 *
 * Prepares a descriptor for cyclic DMA operation. This means that once the
 * descriptor is submitted, we will be submitting in a @period_len sized
 * buffers and calling callback once the period has been elapsed. Transfer
 * terminates only when client calls dmaengine_terminate_all() for this
 * channel.
 *
 * Returns a valid DMA descriptor or %NULL in case of failure.
 */
static struct dma_async_tx_descriptor *
ep93xx_dma_prep_dma_cyclic(struct dma_chan *chan, dma_addr_t dma_addr,
			   size_t buf_len, size_t period_len,
			   enum dma_transfer_direction dir, unsigned long flags)
{
	struct ep93xx_dma_chan *edmac = to_ep93xx_dma_chan(chan);
	struct ep93xx_dma_desc *desc, *first;
	size_t offset = 0;

	if (!edmac->edma->m2m && dir != ep93xx_dma_chan_direction(chan)) {
		dev_warn(chan2dev(edmac),
			 "channel was configured with different direction\n");
		return NULL;
	}

	if (test_and_set_bit(EP93XX_DMA_IS_CYCLIC, &edmac->flags)) {
		dev_warn(chan2dev(edmac),
			 "channel is already used for cyclic transfers\n");
		return NULL;
	}

	if (period_len > DMA_MAX_CHAN_BYTES) {
		dev_warn(chan2dev(edmac), "too big period length %zu\n",
			 period_len);
		return NULL;
	}

	ep93xx_dma_slave_config_write(chan, dir, &edmac->slave_config);

	/* Split the buffer into period size chunks */
	first = NULL;
	for (offset = 0; offset < buf_len; offset += period_len) {
		desc = ep93xx_dma_desc_get(edmac);
		if (!desc) {
			dev_warn(chan2dev(edmac), "couldn't get descriptor\n");
			goto fail;
		}

		if (dir == DMA_MEM_TO_DEV) {
			desc->src_addr = dma_addr + offset;
			desc->dst_addr = edmac->runtime_addr;
		} else {
			desc->src_addr = edmac->runtime_addr;
			desc->dst_addr = dma_addr + offset;
		}

		desc->size = period_len;

		if (!first)
			first = desc;
		else
			list_add_tail(&desc->node, &first->tx_list);
	}

	first->txd.cookie = -EBUSY;

	return &first->txd;

fail:
	ep93xx_dma_desc_put(edmac, first);
	return NULL;
}

/**
 * ep93xx_dma_synchronize - Synchronizes the termination of transfers to the
 * current context.
 * @chan: channel
 *
 * Synchronizes the DMA channel termination to the current context. When this
 * function returns it is guaranteed that all transfers for previously issued
 * descriptors have stopped and and it is safe to free the memory associated
 * with them. Furthermore it is guaranteed that all complete callback functions
 * for a previously submitted descriptor have finished running and it is safe to
 * free resources accessed from within the complete callbacks.
 */
static void ep93xx_dma_synchronize(struct dma_chan *chan)
{
	struct ep93xx_dma_chan *edmac = to_ep93xx_dma_chan(chan);

	if (edmac->edma->hw_synchronize)
		edmac->edma->hw_synchronize(edmac);
}

/**
 * ep93xx_dma_terminate_all - terminate all transactions
 * @chan: channel
 *
 * Stops all DMA transactions. All descriptors are put back to the
 * @edmac->free_list and callbacks are _not_ called.
 */
static int ep93xx_dma_terminate_all(struct dma_chan *chan)
{
	struct ep93xx_dma_chan *edmac = to_ep93xx_dma_chan(chan);
	struct ep93xx_dma_desc *desc, *_d;
	unsigned long flags;
	LIST_HEAD(list);

	spin_lock_irqsave(&edmac->lock, flags);
	/* First we disable and flush the DMA channel */
	edmac->edma->hw_shutdown(edmac);
	clear_bit(EP93XX_DMA_IS_CYCLIC, &edmac->flags);
	list_splice_init(&edmac->active, &list);
	list_splice_init(&edmac->queue, &list);
	/*
	 * We then re-enable the channel. This way we can continue submitting
	 * the descriptors by just calling ->hw_submit() again.
	 */
	edmac->edma->hw_setup(edmac);
	spin_unlock_irqrestore(&edmac->lock, flags);

	list_for_each_entry_safe(desc, _d, &list, node)
		ep93xx_dma_desc_put(edmac, desc);

	return 0;
}

static int ep93xx_dma_slave_config(struct dma_chan *chan,
				   struct dma_slave_config *config)
{
	struct ep93xx_dma_chan *edmac = to_ep93xx_dma_chan(chan);

	memcpy(&edmac->slave_config, config, sizeof(*config));

	return 0;
}

static int ep93xx_dma_slave_config_write(struct dma_chan *chan,
					 enum dma_transfer_direction dir,
					 struct dma_slave_config *config)
{
	struct ep93xx_dma_chan *edmac = to_ep93xx_dma_chan(chan);
	enum dma_slave_buswidth width;
	unsigned long flags;
	u32 addr, ctrl;

	if (!edmac->edma->m2m)
		return -EINVAL;

	switch (dir) {
	case DMA_DEV_TO_MEM:
		width = config->src_addr_width;
		addr = config->src_addr;
		break;

	case DMA_MEM_TO_DEV:
		width = config->dst_addr_width;
		addr = config->dst_addr;
		break;

	default:
		return -EINVAL;
	}

	switch (width) {
	case DMA_SLAVE_BUSWIDTH_1_BYTE:
		ctrl = 0;
		break;
	case DMA_SLAVE_BUSWIDTH_2_BYTES:
		ctrl = M2M_CONTROL_PW_16;
		break;
	case DMA_SLAVE_BUSWIDTH_4_BYTES:
		ctrl = M2M_CONTROL_PW_32;
		break;
	default:
		return -EINVAL;
	}

	spin_lock_irqsave(&edmac->lock, flags);
	edmac->runtime_addr = addr;
	edmac->runtime_ctrl = ctrl;
	spin_unlock_irqrestore(&edmac->lock, flags);

	return 0;
}

/**
 * ep93xx_dma_tx_status - check if a transaction is completed
 * @chan: channel
 * @cookie: transaction specific cookie
 * @state: state of the transaction is stored here if given
 *
 * This function can be used to query state of a given transaction.
 */
static enum dma_status ep93xx_dma_tx_status(struct dma_chan *chan,
					    dma_cookie_t cookie,
					    struct dma_tx_state *state)
{
	return dma_cookie_status(chan, cookie, state);
}

/**
 * ep93xx_dma_issue_pending - push pending transactions to the hardware
 * @chan: channel
 *
 * When this function is called, all pending transactions are pushed to the
 * hardware and executed.
 */
static void ep93xx_dma_issue_pending(struct dma_chan *chan)
{
	ep93xx_dma_advance_work(to_ep93xx_dma_chan(chan));
}

static int __init ep93xx_dma_probe(struct platform_device *pdev)
{
	struct ep93xx_dma_platform_data *pdata = dev_get_platdata(&pdev->dev);
	struct ep93xx_dma_engine *edma;
	struct dma_device *dma_dev;
	size_t edma_size;
	int ret, i;

	edma_size = pdata->num_channels * sizeof(struct ep93xx_dma_chan);
	edma = kzalloc(sizeof(*edma) + edma_size, GFP_KERNEL);
	if (!edma)
		return -ENOMEM;

	dma_dev = &edma->dma_dev;
	edma->m2m = platform_get_device_id(pdev)->driver_data;
	edma->num_channels = pdata->num_channels;

	INIT_LIST_HEAD(&dma_dev->channels);
	for (i = 0; i < pdata->num_channels; i++) {
		const struct ep93xx_dma_chan_data *cdata = &pdata->channels[i];
		struct ep93xx_dma_chan *edmac = &edma->channels[i];

		edmac->chan.device = dma_dev;
		edmac->regs = cdata->base;
		edmac->irq = cdata->irq;
		edmac->edma = edma;

		edmac->clk = clk_get(NULL, cdata->name);
		if (IS_ERR(edmac->clk)) {
			dev_warn(&pdev->dev, "failed to get clock for %s\n",
				 cdata->name);
			continue;
		}

		spin_lock_init(&edmac->lock);
		INIT_LIST_HEAD(&edmac->active);
		INIT_LIST_HEAD(&edmac->queue);
		INIT_LIST_HEAD(&edmac->free_list);
		tasklet_setup(&edmac->tasklet, ep93xx_dma_tasklet);

		list_add_tail(&edmac->chan.device_node,
			      &dma_dev->channels);
	}

	dma_cap_zero(dma_dev->cap_mask);
	dma_cap_set(DMA_SLAVE, dma_dev->cap_mask);
	dma_cap_set(DMA_CYCLIC, dma_dev->cap_mask);

	dma_dev->dev = &pdev->dev;
	dma_dev->device_alloc_chan_resources = ep93xx_dma_alloc_chan_resources;
	dma_dev->device_free_chan_resources = ep93xx_dma_free_chan_resources;
	dma_dev->device_prep_slave_sg = ep93xx_dma_prep_slave_sg;
	dma_dev->device_prep_dma_cyclic = ep93xx_dma_prep_dma_cyclic;
	dma_dev->device_config = ep93xx_dma_slave_config;
	dma_dev->device_synchronize = ep93xx_dma_synchronize;
	dma_dev->device_terminate_all = ep93xx_dma_terminate_all;
	dma_dev->device_issue_pending = ep93xx_dma_issue_pending;
	dma_dev->device_tx_status = ep93xx_dma_tx_status;

	dma_set_max_seg_size(dma_dev->dev, DMA_MAX_CHAN_BYTES);

	if (edma->m2m) {
		dma_cap_set(DMA_MEMCPY, dma_dev->cap_mask);
		dma_dev->device_prep_dma_memcpy = ep93xx_dma_prep_dma_memcpy;

		edma->hw_setup = m2m_hw_setup;
		edma->hw_shutdown = m2m_hw_shutdown;
		edma->hw_submit = m2m_hw_submit;
		edma->hw_interrupt = m2m_hw_interrupt;
	} else {
		dma_cap_set(DMA_PRIVATE, dma_dev->cap_mask);

		edma->hw_synchronize = m2p_hw_synchronize;
		edma->hw_setup = m2p_hw_setup;
		edma->hw_shutdown = m2p_hw_shutdown;
		edma->hw_submit = m2p_hw_submit;
		edma->hw_interrupt = m2p_hw_interrupt;
	}

	ret = dma_async_device_register(dma_dev);
	if (unlikely(ret)) {
		for (i = 0; i < edma->num_channels; i++) {
			struct ep93xx_dma_chan *edmac = &edma->channels[i];
			if (!IS_ERR_OR_NULL(edmac->clk))
				clk_put(edmac->clk);
		}
		kfree(edma);
	} else {
		dev_info(dma_dev->dev, "EP93xx M2%s DMA ready\n",
			 edma->m2m ? "M" : "P");
	}

	return ret;
}

static const struct platform_device_id ep93xx_dma_driver_ids[] = {
	{ "ep93xx-dma-m2p", 0 },
	{ "ep93xx-dma-m2m", 1 },
	{ },
};

static struct platform_driver ep93xx_dma_driver = {
	.driver		= {
		.name	= "ep93xx-dma",
	},
	.id_table	= ep93xx_dma_driver_ids,
};

static int __init ep93xx_dma_module_init(void)
{
	return platform_driver_probe(&ep93xx_dma_driver, ep93xx_dma_probe);
}
subsys_initcall(ep93xx_dma_module_init);

MODULE_AUTHOR("Mika Westerberg <mika.westerberg@iki.fi>");
MODULE_DESCRIPTION("EP93xx DMA driver");
MODULE_LICENSE("GPL");

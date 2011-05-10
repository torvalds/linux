/*
 * Copyright (C) 2005-2006 by Texas Instruments
 *
 * This file implements a DMA  interface using TI's CPPI DMA.
 * For now it's DaVinci-only, but CPPI isn't specific to DaVinci or USB.
 * The TUSB6020, using VLYNQ, has CPPI that looks much like DaVinci.
 */

#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/usb.h>

#include "musb_core.h"
#include "musb_debug.h"
#include "cppi_dma.h"


/* CPPI DMA status 7-mar-2006:
 *
 * - See musb_{host,gadget}.c for more info
 *
 * - Correct RX DMA generally forces the engine into irq-per-packet mode,
 *   which can easily saturate the CPU under non-mass-storage loads.
 *
 * NOTES 24-aug-2006 (2.6.18-rc4):
 *
 * - peripheral RXDMA wedged in a test with packets of length 512/512/1.
 *   evidently after the 1 byte packet was received and acked, the queue
 *   of BDs got garbaged so it wouldn't empty the fifo.  (rxcsr 0x2003,
 *   and RX DMA0: 4 left, 80000000 8feff880, 8feff860 8feff860; 8f321401
 *   004001ff 00000001 .. 8feff860)  Host was just getting NAKed on tx
 *   of its next (512 byte) packet.  IRQ issues?
 *
 * REVISIT:  the "transfer DMA" glue between CPPI and USB fifos will
 * evidently also directly update the RX and TX CSRs ... so audit all
 * host and peripheral side DMA code to avoid CSR access after DMA has
 * been started.
 */

/* REVISIT now we can avoid preallocating these descriptors; or
 * more simply, switch to a global freelist not per-channel ones.
 * Note: at full speed, 64 descriptors == 4K bulk data.
 */
#define NUM_TXCHAN_BD       64
#define NUM_RXCHAN_BD       64

static inline void cpu_drain_writebuffer(void)
{
	wmb();
#ifdef	CONFIG_CPU_ARM926T
	/* REVISIT this "should not be needed",
	 * but lack of it sure seemed to hurt ...
	 */
	asm("mcr p15, 0, r0, c7, c10, 4 @ drain write buffer\n");
#endif
}

static inline struct cppi_descriptor *cppi_bd_alloc(struct cppi_channel *c)
{
	struct cppi_descriptor	*bd = c->freelist;

	if (bd)
		c->freelist = bd->next;
	return bd;
}

static inline void
cppi_bd_free(struct cppi_channel *c, struct cppi_descriptor *bd)
{
	if (!bd)
		return;
	bd->next = c->freelist;
	c->freelist = bd;
}

/*
 *  Start DMA controller
 *
 *  Initialize the DMA controller as necessary.
 */

/* zero out entire rx state RAM entry for the channel */
static void cppi_reset_rx(struct cppi_rx_stateram __iomem *rx)
{
	musb_writel(&rx->rx_skipbytes, 0, 0);
	musb_writel(&rx->rx_head, 0, 0);
	musb_writel(&rx->rx_sop, 0, 0);
	musb_writel(&rx->rx_current, 0, 0);
	musb_writel(&rx->rx_buf_current, 0, 0);
	musb_writel(&rx->rx_len_len, 0, 0);
	musb_writel(&rx->rx_cnt_cnt, 0, 0);
}

/* zero out entire tx state RAM entry for the channel */
static void cppi_reset_tx(struct cppi_tx_stateram __iomem *tx, u32 ptr)
{
	musb_writel(&tx->tx_head, 0, 0);
	musb_writel(&tx->tx_buf, 0, 0);
	musb_writel(&tx->tx_current, 0, 0);
	musb_writel(&tx->tx_buf_current, 0, 0);
	musb_writel(&tx->tx_info, 0, 0);
	musb_writel(&tx->tx_rem_len, 0, 0);
	/* musb_writel(&tx->tx_dummy, 0, 0); */
	musb_writel(&tx->tx_complete, 0, ptr);
}

static void __init cppi_pool_init(struct cppi *cppi, struct cppi_channel *c)
{
	int	j;

	/* initialize channel fields */
	c->head = NULL;
	c->tail = NULL;
	c->last_processed = NULL;
	c->channel.status = MUSB_DMA_STATUS_UNKNOWN;
	c->controller = cppi;
	c->is_rndis = 0;
	c->freelist = NULL;

	/* build the BD Free list for the channel */
	for (j = 0; j < NUM_TXCHAN_BD + 1; j++) {
		struct cppi_descriptor	*bd;
		dma_addr_t		dma;

		bd = dma_pool_alloc(cppi->pool, GFP_KERNEL, &dma);
		bd->dma = dma;
		cppi_bd_free(c, bd);
	}
}

static int cppi_channel_abort(struct dma_channel *);

static void cppi_pool_free(struct cppi_channel *c)
{
	struct cppi		*cppi = c->controller;
	struct cppi_descriptor	*bd;

	(void) cppi_channel_abort(&c->channel);
	c->channel.status = MUSB_DMA_STATUS_UNKNOWN;
	c->controller = NULL;

	/* free all its bds */
	bd = c->last_processed;
	do {
		if (bd)
			dma_pool_free(cppi->pool, bd, bd->dma);
		bd = cppi_bd_alloc(c);
	} while (bd);
	c->last_processed = NULL;
}

static int __init cppi_controller_start(struct dma_controller *c)
{
	struct cppi	*controller;
	void __iomem	*tibase;
	int		i;

	controller = container_of(c, struct cppi, controller);

	/* do whatever is necessary to start controller */
	for (i = 0; i < ARRAY_SIZE(controller->tx); i++) {
		controller->tx[i].transmit = true;
		controller->tx[i].index = i;
	}
	for (i = 0; i < ARRAY_SIZE(controller->rx); i++) {
		controller->rx[i].transmit = false;
		controller->rx[i].index = i;
	}

	/* setup BD list on a per channel basis */
	for (i = 0; i < ARRAY_SIZE(controller->tx); i++)
		cppi_pool_init(controller, controller->tx + i);
	for (i = 0; i < ARRAY_SIZE(controller->rx); i++)
		cppi_pool_init(controller, controller->rx + i);

	tibase =  controller->tibase;
	INIT_LIST_HEAD(&controller->tx_complete);

	/* initialise tx/rx channel head pointers to zero */
	for (i = 0; i < ARRAY_SIZE(controller->tx); i++) {
		struct cppi_channel	*tx_ch = controller->tx + i;
		struct cppi_tx_stateram __iomem *tx;

		INIT_LIST_HEAD(&tx_ch->tx_complete);

		tx = tibase + DAVINCI_TXCPPI_STATERAM_OFFSET(i);
		tx_ch->state_ram = tx;
		cppi_reset_tx(tx, 0);
	}
	for (i = 0; i < ARRAY_SIZE(controller->rx); i++) {
		struct cppi_channel	*rx_ch = controller->rx + i;
		struct cppi_rx_stateram __iomem *rx;

		INIT_LIST_HEAD(&rx_ch->tx_complete);

		rx = tibase + DAVINCI_RXCPPI_STATERAM_OFFSET(i);
		rx_ch->state_ram = rx;
		cppi_reset_rx(rx);
	}

	/* enable individual cppi channels */
	musb_writel(tibase, DAVINCI_TXCPPI_INTENAB_REG,
			DAVINCI_DMA_ALL_CHANNELS_ENABLE);
	musb_writel(tibase, DAVINCI_RXCPPI_INTENAB_REG,
			DAVINCI_DMA_ALL_CHANNELS_ENABLE);

	/* enable tx/rx CPPI control */
	musb_writel(tibase, DAVINCI_TXCPPI_CTRL_REG, DAVINCI_DMA_CTRL_ENABLE);
	musb_writel(tibase, DAVINCI_RXCPPI_CTRL_REG, DAVINCI_DMA_CTRL_ENABLE);

	/* disable RNDIS mode, also host rx RNDIS autorequest */
	musb_writel(tibase, DAVINCI_RNDIS_REG, 0);
	musb_writel(tibase, DAVINCI_AUTOREQ_REG, 0);

	return 0;
}

/*
 *  Stop DMA controller
 *
 *  De-Init the DMA controller as necessary.
 */

static int cppi_controller_stop(struct dma_controller *c)
{
	struct cppi		*controller;
	void __iomem		*tibase;
	int			i;

	controller = container_of(c, struct cppi, controller);

	tibase = controller->tibase;
	/* DISABLE INDIVIDUAL CHANNEL Interrupts */
	musb_writel(tibase, DAVINCI_TXCPPI_INTCLR_REG,
			DAVINCI_DMA_ALL_CHANNELS_ENABLE);
	musb_writel(tibase, DAVINCI_RXCPPI_INTCLR_REG,
			DAVINCI_DMA_ALL_CHANNELS_ENABLE);

	DBG(1, "Tearing down RX and TX Channels\n");
	for (i = 0; i < ARRAY_SIZE(controller->tx); i++) {
		/* FIXME restructure of txdma to use bds like rxdma */
		controller->tx[i].last_processed = NULL;
		cppi_pool_free(controller->tx + i);
	}
	for (i = 0; i < ARRAY_SIZE(controller->rx); i++)
		cppi_pool_free(controller->rx + i);

	/* in Tx Case proper teardown is supported. We resort to disabling
	 * Tx/Rx CPPI after cleanup of Tx channels. Before TX teardown is
	 * complete TX CPPI cannot be disabled.
	 */
	/*disable tx/rx cppi */
	musb_writel(tibase, DAVINCI_TXCPPI_CTRL_REG, DAVINCI_DMA_CTRL_DISABLE);
	musb_writel(tibase, DAVINCI_RXCPPI_CTRL_REG, DAVINCI_DMA_CTRL_DISABLE);

	return 0;
}

/* While dma channel is allocated, we only want the core irqs active
 * for fault reports, otherwise we'd get irqs that we don't care about.
 * Except for TX irqs, where dma done != fifo empty and reusable ...
 *
 * NOTE: docs don't say either way, but irq masking **enables** irqs.
 *
 * REVISIT same issue applies to pure PIO usage too, and non-cppi dma...
 */
static inline void core_rxirq_disable(void __iomem *tibase, unsigned epnum)
{
	musb_writel(tibase, DAVINCI_USB_INT_MASK_CLR_REG, 1 << (epnum + 8));
}

static inline void core_rxirq_enable(void __iomem *tibase, unsigned epnum)
{
	musb_writel(tibase, DAVINCI_USB_INT_MASK_SET_REG, 1 << (epnum + 8));
}


/*
 * Allocate a CPPI Channel for DMA.  With CPPI, channels are bound to
 * each transfer direction of a non-control endpoint, so allocating
 * (and deallocating) is mostly a way to notice bad housekeeping on
 * the software side.  We assume the irqs are always active.
 */
static struct dma_channel *
cppi_channel_allocate(struct dma_controller *c,
		struct musb_hw_ep *ep, u8 transmit)
{
	struct cppi		*controller;
	u8			index;
	struct cppi_channel	*cppi_ch;
	void __iomem		*tibase;

	controller = container_of(c, struct cppi, controller);
	tibase = controller->tibase;

	/* ep0 doesn't use DMA; remember cppi indices are 0..N-1 */
	index = ep->epnum - 1;

	/* return the corresponding CPPI Channel Handle, and
	 * probably disable the non-CPPI irq until we need it.
	 */
	if (transmit) {
		if (index >= ARRAY_SIZE(controller->tx)) {
			DBG(1, "no %cX%d CPPI channel\n", 'T', index);
			return NULL;
		}
		cppi_ch = controller->tx + index;
	} else {
		if (index >= ARRAY_SIZE(controller->rx)) {
			DBG(1, "no %cX%d CPPI channel\n", 'R', index);
			return NULL;
		}
		cppi_ch = controller->rx + index;
		core_rxirq_disable(tibase, ep->epnum);
	}

	/* REVISIT make this an error later once the same driver code works
	 * with the other DMA engine too
	 */
	if (cppi_ch->hw_ep)
		DBG(1, "re-allocating DMA%d %cX channel %p\n",
				index, transmit ? 'T' : 'R', cppi_ch);
	cppi_ch->hw_ep = ep;
	cppi_ch->channel.status = MUSB_DMA_STATUS_FREE;
	cppi_ch->channel.max_len = 0x7fffffff;

	DBG(4, "Allocate CPPI%d %cX\n", index, transmit ? 'T' : 'R');
	return &cppi_ch->channel;
}

/* Release a CPPI Channel.  */
static void cppi_channel_release(struct dma_channel *channel)
{
	struct cppi_channel	*c;
	void __iomem		*tibase;

	/* REVISIT:  for paranoia, check state and abort if needed... */

	c = container_of(channel, struct cppi_channel, channel);
	tibase = c->controller->tibase;
	if (!c->hw_ep)
		DBG(1, "releasing idle DMA channel %p\n", c);
	else if (!c->transmit)
		core_rxirq_enable(tibase, c->index + 1);

	/* for now, leave its cppi IRQ enabled (we won't trigger it) */
	c->hw_ep = NULL;
	channel->status = MUSB_DMA_STATUS_UNKNOWN;
}

/* Context: controller irqlocked */
static void
cppi_dump_rx(int level, struct cppi_channel *c, const char *tag)
{
	void __iomem			*base = c->controller->mregs;
	struct cppi_rx_stateram __iomem	*rx = c->state_ram;

	musb_ep_select(base, c->index + 1);

	DBG(level, "RX DMA%d%s: %d left, csr %04x, "
			"%08x H%08x S%08x C%08x, "
			"B%08x L%08x %08x .. %08x"
			"\n",
		c->index, tag,
		musb_readl(c->controller->tibase,
			DAVINCI_RXCPPI_BUFCNT0_REG + 4 * c->index),
		musb_readw(c->hw_ep->regs, MUSB_RXCSR),

		musb_readl(&rx->rx_skipbytes, 0),
		musb_readl(&rx->rx_head, 0),
		musb_readl(&rx->rx_sop, 0),
		musb_readl(&rx->rx_current, 0),

		musb_readl(&rx->rx_buf_current, 0),
		musb_readl(&rx->rx_len_len, 0),
		musb_readl(&rx->rx_cnt_cnt, 0),
		musb_readl(&rx->rx_complete, 0)
		);
}

/* Context: controller irqlocked */
static void
cppi_dump_tx(int level, struct cppi_channel *c, const char *tag)
{
	void __iomem			*base = c->controller->mregs;
	struct cppi_tx_stateram __iomem	*tx = c->state_ram;

	musb_ep_select(base, c->index + 1);

	DBG(level, "TX DMA%d%s: csr %04x, "
			"H%08x S%08x C%08x %08x, "
			"F%08x L%08x .. %08x"
			"\n",
		c->index, tag,
		musb_readw(c->hw_ep->regs, MUSB_TXCSR),

		musb_readl(&tx->tx_head, 0),
		musb_readl(&tx->tx_buf, 0),
		musb_readl(&tx->tx_current, 0),
		musb_readl(&tx->tx_buf_current, 0),

		musb_readl(&tx->tx_info, 0),
		musb_readl(&tx->tx_rem_len, 0),
		/* dummy/unused word 6 */
		musb_readl(&tx->tx_complete, 0)
		);
}

/* Context: controller irqlocked */
static inline void
cppi_rndis_update(struct cppi_channel *c, int is_rx,
		void __iomem *tibase, int is_rndis)
{
	/* we may need to change the rndis flag for this cppi channel */
	if (c->is_rndis != is_rndis) {
		u32	value = musb_readl(tibase, DAVINCI_RNDIS_REG);
		u32	temp = 1 << (c->index);

		if (is_rx)
			temp <<= 16;
		if (is_rndis)
			value |= temp;
		else
			value &= ~temp;
		musb_writel(tibase, DAVINCI_RNDIS_REG, value);
		c->is_rndis = is_rndis;
	}
}

#ifdef CONFIG_USB_MUSB_DEBUG
static void cppi_dump_rxbd(const char *tag, struct cppi_descriptor *bd)
{
	pr_debug("RXBD/%s %08x: "
			"nxt %08x buf %08x off.blen %08x opt.plen %08x\n",
			tag, bd->dma,
			bd->hw_next, bd->hw_bufp, bd->hw_off_len,
			bd->hw_options);
}
#endif

static void cppi_dump_rxq(int level, const char *tag, struct cppi_channel *rx)
{
#ifdef CONFIG_USB_MUSB_DEBUG
	struct cppi_descriptor	*bd;

	if (!_dbg_level(level))
		return;
	cppi_dump_rx(level, rx, tag);
	if (rx->last_processed)
		cppi_dump_rxbd("last", rx->last_processed);
	for (bd = rx->head; bd; bd = bd->next)
		cppi_dump_rxbd("active", bd);
#endif
}


/* NOTE:  DaVinci autoreq is ignored except for host side "RNDIS" mode RX;
 * so we won't ever use it (see "CPPI RX Woes" below).
 */
static inline int cppi_autoreq_update(struct cppi_channel *rx,
		void __iomem *tibase, int onepacket, unsigned n_bds)
{
	u32	val;

#ifdef	RNDIS_RX_IS_USABLE
	u32	tmp;
	/* assert(is_host_active(musb)) */

	/* start from "AutoReq never" */
	tmp = musb_readl(tibase, DAVINCI_AUTOREQ_REG);
	val = tmp & ~((0x3) << (rx->index * 2));

	/* HCD arranged reqpkt for packet #1.  we arrange int
	 * for all but the last one, maybe in two segments.
	 */
	if (!onepacket) {
#if 0
		/* use two segments, autoreq "all" then the last "never" */
		val |= ((0x3) << (rx->index * 2));
		n_bds--;
#else
		/* one segment, autoreq "all-but-last" */
		val |= ((0x1) << (rx->index * 2));
#endif
	}

	if (val != tmp) {
		int n = 100;

		/* make sure that autoreq is updated before continuing */
		musb_writel(tibase, DAVINCI_AUTOREQ_REG, val);
		do {
			tmp = musb_readl(tibase, DAVINCI_AUTOREQ_REG);
			if (tmp == val)
				break;
			cpu_relax();
		} while (n-- > 0);
	}
#endif

	/* REQPKT is turned off after each segment */
	if (n_bds && rx->channel.actual_len) {
		void __iomem	*regs = rx->hw_ep->regs;

		val = musb_readw(regs, MUSB_RXCSR);
		if (!(val & MUSB_RXCSR_H_REQPKT)) {
			val |= MUSB_RXCSR_H_REQPKT | MUSB_RXCSR_H_WZC_BITS;
			musb_writew(regs, MUSB_RXCSR, val);
			/* flush writebufer */
			val = musb_readw(regs, MUSB_RXCSR);
		}
	}
	return n_bds;
}


/* Buffer enqueuing Logic:
 *
 *  - RX builds new queues each time, to help handle routine "early
 *    termination" cases (faults, including errors and short reads)
 *    more correctly.
 *
 *  - for now, TX reuses the same queue of BDs every time
 *
 * REVISIT long term, we want a normal dynamic model.
 * ... the goal will be to append to the
 * existing queue, processing completed "dma buffers" (segments) on the fly.
 *
 * Otherwise we force an IRQ latency between requests, which slows us a lot
 * (especially in "transparent" dma).  Unfortunately that model seems to be
 * inherent in the DMA model from the Mentor code, except in the rare case
 * of transfers big enough (~128+ KB) that we could append "middle" segments
 * in the TX paths.  (RX can't do this, see below.)
 *
 * That's true even in the CPPI- friendly iso case, where most urbs have
 * several small segments provided in a group and where the "packet at a time"
 * "transparent" DMA model is always correct, even on the RX side.
 */

/*
 * CPPI TX:
 * ========
 * TX is a lot more reasonable than RX; it doesn't need to run in
 * irq-per-packet mode very often.  RNDIS mode seems to behave too
 * (except how it handles the exactly-N-packets case).  Building a
 * txdma queue with multiple requests (urb or usb_request) looks
 * like it would work ... but fault handling would need much testing.
 *
 * The main issue with TX mode RNDIS relates to transfer lengths that
 * are an exact multiple of the packet length.  It appears that there's
 * a hiccup in that case (maybe the DMA completes before the ZLP gets
 * written?) boiling down to not being able to rely on CPPI writing any
 * terminating zero length packet before the next transfer is written.
 * So that's punted to PIO; better yet, gadget drivers can avoid it.
 *
 * Plus, there's allegedly an undocumented constraint that rndis transfer
 * length be a multiple of 64 bytes ... but the chip doesn't act that
 * way, and we really don't _want_ that behavior anyway.
 *
 * On TX, "transparent" mode works ... although experiments have shown
 * problems trying to use the SOP/EOP bits in different USB packets.
 *
 * REVISIT try to handle terminating zero length packets using CPPI
 * instead of doing it by PIO after an IRQ.  (Meanwhile, make Ethernet
 * links avoid that issue by forcing them to avoid zlps.)
 */
static void
cppi_next_tx_segment(struct musb *musb, struct cppi_channel *tx)
{
	unsigned		maxpacket = tx->maxpacket;
	dma_addr_t		addr = tx->buf_dma + tx->offset;
	size_t			length = tx->buf_len - tx->offset;
	struct cppi_descriptor	*bd;
	unsigned		n_bds;
	unsigned		i;
	struct cppi_tx_stateram	__iomem *tx_ram = tx->state_ram;
	int			rndis;

	/* TX can use the CPPI "rndis" mode, where we can probably fit this
	 * transfer in one BD and one IRQ.  The only time we would NOT want
	 * to use it is when hardware constraints prevent it, or if we'd
	 * trigger the "send a ZLP?" confusion.
	 */
	rndis = (maxpacket & 0x3f) == 0
		&& length > maxpacket
		&& length < 0xffff
		&& (length % maxpacket) != 0;

	if (rndis) {
		maxpacket = length;
		n_bds = 1;
	} else {
		n_bds = length / maxpacket;
		if (!length || (length % maxpacket))
			n_bds++;
		n_bds = min(n_bds, (unsigned) NUM_TXCHAN_BD);
		length = min(n_bds * maxpacket, length);
	}

	DBG(4, "TX DMA%d, pktSz %d %s bds %d dma 0x%llx len %u\n",
			tx->index,
			maxpacket,
			rndis ? "rndis" : "transparent",
			n_bds,
			(unsigned long long)addr, length);

	cppi_rndis_update(tx, 0, musb->ctrl_base, rndis);

	/* assuming here that channel_program is called during
	 * transfer initiation ... current code maintains state
	 * for one outstanding request only (no queues, not even
	 * the implicit ones of an iso urb).
	 */

	bd = tx->freelist;
	tx->head = bd;
	tx->last_processed = NULL;

	/* FIXME use BD pool like RX side does, and just queue
	 * the minimum number for this request.
	 */

	/* Prepare queue of BDs first, then hand it to hardware.
	 * All BDs except maybe the last should be of full packet
	 * size; for RNDIS there _is_ only that last packet.
	 */
	for (i = 0; i < n_bds; ) {
		if (++i < n_bds && bd->next)
			bd->hw_next = bd->next->dma;
		else
			bd->hw_next = 0;

		bd->hw_bufp = tx->buf_dma + tx->offset;

		/* FIXME set EOP only on the last packet,
		 * SOP only on the first ... avoid IRQs
		 */
		if ((tx->offset + maxpacket) <= tx->buf_len) {
			tx->offset += maxpacket;
			bd->hw_off_len = maxpacket;
			bd->hw_options = CPPI_SOP_SET | CPPI_EOP_SET
				| CPPI_OWN_SET | maxpacket;
		} else {
			/* only this one may be a partial USB Packet */
			u32		partial_len;

			partial_len = tx->buf_len - tx->offset;
			tx->offset = tx->buf_len;
			bd->hw_off_len = partial_len;

			bd->hw_options = CPPI_SOP_SET | CPPI_EOP_SET
				| CPPI_OWN_SET | partial_len;
			if (partial_len == 0)
				bd->hw_options |= CPPI_ZERO_SET;
		}

		DBG(5, "TXBD %p: nxt %08x buf %08x len %04x opt %08x\n",
				bd, bd->hw_next, bd->hw_bufp,
				bd->hw_off_len, bd->hw_options);

		/* update the last BD enqueued to the list */
		tx->tail = bd;
		bd = bd->next;
	}

	/* BDs live in DMA-coherent memory, but writes might be pending */
	cpu_drain_writebuffer();

	/* Write to the HeadPtr in state RAM to trigger */
	musb_writel(&tx_ram->tx_head, 0, (u32)tx->freelist->dma);

	cppi_dump_tx(5, tx, "/S");
}

/*
 * CPPI RX Woes:
 * =============
 * Consider a 1KB bulk RX buffer in two scenarios:  (a) it's fed two 300 byte
 * packets back-to-back, and (b) it's fed two 512 byte packets back-to-back.
 * (Full speed transfers have similar scenarios.)
 *
 * The correct behavior for Linux is that (a) fills the buffer with 300 bytes,
 * and the next packet goes into a buffer that's queued later; while (b) fills
 * the buffer with 1024 bytes.  How to do that with CPPI?
 *
 * - RX queues in "rndis" mode -- one single BD -- handle (a) correctly, but
 *   (b) loses **BADLY** because nothing (!) happens when that second packet
 *   fills the buffer, much less when a third one arrives.  (Which makes this
 *   not a "true" RNDIS mode.  In the RNDIS protocol short-packet termination
 *   is optional, and it's fine if peripherals -- not hosts! -- pad messages
 *   out to end-of-buffer.  Standard PCI host controller DMA descriptors
 *   implement that mode by default ... which is no accident.)
 *
 * - RX queues in "transparent" mode -- two BDs with 512 bytes each -- have
 *   converse problems:  (b) is handled right, but (a) loses badly.  CPPI RX
 *   ignores SOP/EOP markings and processes both of those BDs; so both packets
 *   are loaded into the buffer (with a 212 byte gap between them), and the next
 *   buffer queued will NOT get its 300 bytes of data. (It seems like SOP/EOP
 *   are intended as outputs for RX queues, not inputs...)
 *
 * - A variant of "transparent" mode -- one BD at a time -- is the only way to
 *   reliably make both cases work, with software handling both cases correctly
 *   and at the significant penalty of needing an IRQ per packet.  (The lack of
 *   I/O overlap can be slightly ameliorated by enabling double buffering.)
 *
 * So how to get rid of IRQ-per-packet?  The transparent multi-BD case could
 * be used in special cases like mass storage, which sets URB_SHORT_NOT_OK
 * (or maybe its peripheral side counterpart) to flag (a) scenarios as errors
 * with guaranteed driver level fault recovery and scrubbing out what's left
 * of that garbaged datastream.
 *
 * But there seems to be no way to identify the cases where CPPI RNDIS mode
 * is appropriate -- which do NOT include RNDIS host drivers, but do include
 * the CDC Ethernet driver! -- and the documentation is incomplete/wrong.
 * So we can't _ever_ use RX RNDIS mode ... except by using a heuristic
 * that applies best on the peripheral side (and which could fail rudely).
 *
 * Leaving only "transparent" mode; we avoid multi-bd modes in almost all
 * cases other than mass storage class.  Otherwise we're correct but slow,
 * since CPPI penalizes our need for a "true RNDIS" default mode.
 */


/* Heuristic, intended to kick in for ethernet/rndis peripheral ONLY
 *
 * IFF
 *  (a)	peripheral mode ... since rndis peripherals could pad their
 *	writes to hosts, causing i/o failure; or we'd have to cope with
 *	a largely unknowable variety of host side protocol variants
 *  (b)	and short reads are NOT errors ... since full reads would
 *	cause those same i/o failures
 *  (c)	and read length is
 *	- less than 64KB (max per cppi descriptor)
 *	- not a multiple of 4096 (g_zero default, full reads typical)
 *	- N (>1) packets long, ditto (full reads not EXPECTED)
 * THEN
 *   try rx rndis mode
 *
 * Cost of heuristic failing:  RXDMA wedges at the end of transfers that
 * fill out the whole buffer.  Buggy host side usb network drivers could
 * trigger that, but "in the field" such bugs seem to be all but unknown.
 *
 * So this module parameter lets the heuristic be disabled.  When using
 * gadgetfs, the heuristic will probably need to be disabled.
 */
static int cppi_rx_rndis = 1;

module_param(cppi_rx_rndis, bool, 0);
MODULE_PARM_DESC(cppi_rx_rndis, "enable/disable RX RNDIS heuristic");


/**
 * cppi_next_rx_segment - dma read for the next chunk of a buffer
 * @musb: the controller
 * @rx: dma channel
 * @onepacket: true unless caller treats short reads as errors, and
 *	performs fault recovery above usbcore.
 * Context: controller irqlocked
 *
 * See above notes about why we can't use multi-BD RX queues except in
 * rare cases (mass storage class), and can never use the hardware "rndis"
 * mode (since it's not a "true" RNDIS mode) with complete safety..
 *
 * It's ESSENTIAL that callers specify "onepacket" mode unless they kick in
 * code to recover from corrupted datastreams after each short transfer.
 */
static void
cppi_next_rx_segment(struct musb *musb, struct cppi_channel *rx, int onepacket)
{
	unsigned		maxpacket = rx->maxpacket;
	dma_addr_t		addr = rx->buf_dma + rx->offset;
	size_t			length = rx->buf_len - rx->offset;
	struct cppi_descriptor	*bd, *tail;
	unsigned		n_bds;
	unsigned		i;
	void __iomem		*tibase = musb->ctrl_base;
	int			is_rndis = 0;
	struct cppi_rx_stateram	__iomem *rx_ram = rx->state_ram;

	if (onepacket) {
		/* almost every USB driver, host or peripheral side */
		n_bds = 1;

		/* maybe apply the heuristic above */
		if (cppi_rx_rndis
				&& is_peripheral_active(musb)
				&& length > maxpacket
				&& (length & ~0xffff) == 0
				&& (length & 0x0fff) != 0
				&& (length & (maxpacket - 1)) == 0) {
			maxpacket = length;
			is_rndis = 1;
		}
	} else {
		/* virtually nothing except mass storage class */
		if (length > 0xffff) {
			n_bds = 0xffff / maxpacket;
			length = n_bds * maxpacket;
		} else {
			n_bds = length / maxpacket;
			if (length % maxpacket)
				n_bds++;
		}
		if (n_bds == 1)
			onepacket = 1;
		else
			n_bds = min(n_bds, (unsigned) NUM_RXCHAN_BD);
	}

	/* In host mode, autorequest logic can generate some IN tokens; it's
	 * tricky since we can't leave REQPKT set in RXCSR after the transfer
	 * finishes. So:  multipacket transfers involve two or more segments.
	 * And always at least two IRQs ... RNDIS mode is not an option.
	 */
	if (is_host_active(musb))
		n_bds = cppi_autoreq_update(rx, tibase, onepacket, n_bds);

	cppi_rndis_update(rx, 1, musb->ctrl_base, is_rndis);

	length = min(n_bds * maxpacket, length);

	DBG(4, "RX DMA%d seg, maxp %d %s bds %d (cnt %d) "
			"dma 0x%llx len %u %u/%u\n",
			rx->index, maxpacket,
			onepacket
				? (is_rndis ? "rndis" : "onepacket")
				: "multipacket",
			n_bds,
			musb_readl(tibase,
				DAVINCI_RXCPPI_BUFCNT0_REG + (rx->index * 4))
					& 0xffff,
			(unsigned long long)addr, length,
			rx->channel.actual_len, rx->buf_len);

	/* only queue one segment at a time, since the hardware prevents
	 * correct queue shutdown after unexpected short packets
	 */
	bd = cppi_bd_alloc(rx);
	rx->head = bd;

	/* Build BDs for all packets in this segment */
	for (i = 0, tail = NULL; bd && i < n_bds; i++, tail = bd) {
		u32	bd_len;

		if (i) {
			bd = cppi_bd_alloc(rx);
			if (!bd)
				break;
			tail->next = bd;
			tail->hw_next = bd->dma;
		}
		bd->hw_next = 0;

		/* all but the last packet will be maxpacket size */
		if (maxpacket < length)
			bd_len = maxpacket;
		else
			bd_len = length;

		bd->hw_bufp = addr;
		addr += bd_len;
		rx->offset += bd_len;

		bd->hw_off_len = (0 /*offset*/ << 16) + bd_len;
		bd->buflen = bd_len;

		bd->hw_options = CPPI_OWN_SET | (i == 0 ? length : 0);
		length -= bd_len;
	}

	/* we always expect at least one reusable BD! */
	if (!tail) {
		WARNING("rx dma%d -- no BDs? need %d\n", rx->index, n_bds);
		return;
	} else if (i < n_bds)
		WARNING("rx dma%d -- only %d of %d BDs\n", rx->index, i, n_bds);

	tail->next = NULL;
	tail->hw_next = 0;

	bd = rx->head;
	rx->tail = tail;

	/* short reads and other faults should terminate this entire
	 * dma segment.  we want one "dma packet" per dma segment, not
	 * one per USB packet, terminating the whole queue at once...
	 * NOTE that current hardware seems to ignore SOP and EOP.
	 */
	bd->hw_options |= CPPI_SOP_SET;
	tail->hw_options |= CPPI_EOP_SET;

#ifdef CONFIG_USB_MUSB_DEBUG
	if (_dbg_level(5)) {
		struct cppi_descriptor	*d;

		for (d = rx->head; d; d = d->next)
			cppi_dump_rxbd("S", d);
	}
#endif

	/* in case the preceding transfer left some state... */
	tail = rx->last_processed;
	if (tail) {
		tail->next = bd;
		tail->hw_next = bd->dma;
	}

	core_rxirq_enable(tibase, rx->index + 1);

	/* BDs live in DMA-coherent memory, but writes might be pending */
	cpu_drain_writebuffer();

	/* REVISIT specs say to write this AFTER the BUFCNT register
	 * below ... but that loses badly.
	 */
	musb_writel(&rx_ram->rx_head, 0, bd->dma);

	/* bufferCount must be at least 3, and zeroes on completion
	 * unless it underflows below zero, or stops at two, or keeps
	 * growing ... grr.
	 */
	i = musb_readl(tibase,
			DAVINCI_RXCPPI_BUFCNT0_REG + (rx->index * 4))
			& 0xffff;

	if (!i)
		musb_writel(tibase,
			DAVINCI_RXCPPI_BUFCNT0_REG + (rx->index * 4),
			n_bds + 2);
	else if (n_bds > (i - 3))
		musb_writel(tibase,
			DAVINCI_RXCPPI_BUFCNT0_REG + (rx->index * 4),
			n_bds - (i - 3));

	i = musb_readl(tibase,
			DAVINCI_RXCPPI_BUFCNT0_REG + (rx->index * 4))
			& 0xffff;
	if (i < (2 + n_bds)) {
		DBG(2, "bufcnt%d underrun - %d (for %d)\n",
					rx->index, i, n_bds);
		musb_writel(tibase,
			DAVINCI_RXCPPI_BUFCNT0_REG + (rx->index * 4),
			n_bds + 2);
	}

	cppi_dump_rx(4, rx, "/S");
}

/**
 * cppi_channel_program - program channel for data transfer
 * @ch: the channel
 * @maxpacket: max packet size
 * @mode: For RX, 1 unless the usb protocol driver promised to treat
 *	all short reads as errors and kick in high level fault recovery.
 *	For TX, ignored because of RNDIS mode races/glitches.
 * @dma_addr: dma address of buffer
 * @len: length of buffer
 * Context: controller irqlocked
 */
static int cppi_channel_program(struct dma_channel *ch,
		u16 maxpacket, u8 mode,
		dma_addr_t dma_addr, u32 len)
{
	struct cppi_channel	*cppi_ch;
	struct cppi		*controller;
	struct musb		*musb;

	cppi_ch = container_of(ch, struct cppi_channel, channel);
	controller = cppi_ch->controller;
	musb = controller->musb;

	switch (ch->status) {
	case MUSB_DMA_STATUS_BUS_ABORT:
	case MUSB_DMA_STATUS_CORE_ABORT:
		/* fault irq handler should have handled cleanup */
		WARNING("%cX DMA%d not cleaned up after abort!\n",
				cppi_ch->transmit ? 'T' : 'R',
				cppi_ch->index);
		/* WARN_ON(1); */
		break;
	case MUSB_DMA_STATUS_BUSY:
		WARNING("program active channel?  %cX DMA%d\n",
				cppi_ch->transmit ? 'T' : 'R',
				cppi_ch->index);
		/* WARN_ON(1); */
		break;
	case MUSB_DMA_STATUS_UNKNOWN:
		DBG(1, "%cX DMA%d not allocated!\n",
				cppi_ch->transmit ? 'T' : 'R',
				cppi_ch->index);
		/* FALLTHROUGH */
	case MUSB_DMA_STATUS_FREE:
		break;
	}

	ch->status = MUSB_DMA_STATUS_BUSY;

	/* set transfer parameters, then queue up its first segment */
	cppi_ch->buf_dma = dma_addr;
	cppi_ch->offset = 0;
	cppi_ch->maxpacket = maxpacket;
	cppi_ch->buf_len = len;
	cppi_ch->channel.actual_len = 0;

	/* TX channel? or RX? */
	if (cppi_ch->transmit)
		cppi_next_tx_segment(musb, cppi_ch);
	else
		cppi_next_rx_segment(musb, cppi_ch, mode);

	return true;
}

static bool cppi_rx_scan(struct cppi *cppi, unsigned ch)
{
	struct cppi_channel		*rx = &cppi->rx[ch];
	struct cppi_rx_stateram __iomem	*state = rx->state_ram;
	struct cppi_descriptor		*bd;
	struct cppi_descriptor		*last = rx->last_processed;
	bool				completed = false;
	bool				acked = false;
	int				i;
	dma_addr_t			safe2ack;
	void __iomem			*regs = rx->hw_ep->regs;

	cppi_dump_rx(6, rx, "/K");

	bd = last ? last->next : rx->head;
	if (!bd)
		return false;

	/* run through all completed BDs */
	for (i = 0, safe2ack = musb_readl(&state->rx_complete, 0);
			(safe2ack || completed) && bd && i < NUM_RXCHAN_BD;
			i++, bd = bd->next) {
		u16	len;

		/* catch latest BD writes from CPPI */
		rmb();
		if (!completed && (bd->hw_options & CPPI_OWN_SET))
			break;

		DBG(5, "C/RXBD %llx: nxt %08x buf %08x "
			"off.len %08x opt.len %08x (%d)\n",
			(unsigned long long)bd->dma, bd->hw_next, bd->hw_bufp,
			bd->hw_off_len, bd->hw_options,
			rx->channel.actual_len);

		/* actual packet received length */
		if ((bd->hw_options & CPPI_SOP_SET) && !completed)
			len = bd->hw_off_len & CPPI_RECV_PKTLEN_MASK;
		else
			len = 0;

		if (bd->hw_options & CPPI_EOQ_MASK)
			completed = true;

		if (!completed && len < bd->buflen) {
			/* NOTE:  when we get a short packet, RXCSR_H_REQPKT
			 * must have been cleared, and no more DMA packets may
			 * active be in the queue... TI docs didn't say, but
			 * CPPI ignores those BDs even though OWN is still set.
			 */
			completed = true;
			DBG(3, "rx short %d/%d (%d)\n",
					len, bd->buflen,
					rx->channel.actual_len);
		}

		/* If we got here, we expect to ack at least one BD; meanwhile
		 * CPPI may completing other BDs while we scan this list...
		 *
		 * RACE: we can notice OWN cleared before CPPI raises the
		 * matching irq by writing that BD as the completion pointer.
		 * In such cases, stop scanning and wait for the irq, avoiding
		 * lost acks and states where BD ownership is unclear.
		 */
		if (bd->dma == safe2ack) {
			musb_writel(&state->rx_complete, 0, safe2ack);
			safe2ack = musb_readl(&state->rx_complete, 0);
			acked = true;
			if (bd->dma == safe2ack)
				safe2ack = 0;
		}

		rx->channel.actual_len += len;

		cppi_bd_free(rx, last);
		last = bd;

		/* stop scanning on end-of-segment */
		if (bd->hw_next == 0)
			completed = true;
	}
	rx->last_processed = last;

	/* dma abort, lost ack, or ... */
	if (!acked && last) {
		int	csr;

		if (safe2ack == 0 || safe2ack == rx->last_processed->dma)
			musb_writel(&state->rx_complete, 0, safe2ack);
		if (safe2ack == 0) {
			cppi_bd_free(rx, last);
			rx->last_processed = NULL;

			/* if we land here on the host side, H_REQPKT will
			 * be clear and we need to restart the queue...
			 */
			WARN_ON(rx->head);
		}
		musb_ep_select(cppi->mregs, rx->index + 1);
		csr = musb_readw(regs, MUSB_RXCSR);
		if (csr & MUSB_RXCSR_DMAENAB) {
			DBG(4, "list%d %p/%p, last %llx%s, csr %04x\n",
				rx->index,
				rx->head, rx->tail,
				rx->last_processed
					? (unsigned long long)
						rx->last_processed->dma
					: 0,
				completed ? ", completed" : "",
				csr);
			cppi_dump_rxq(4, "/what?", rx);
		}
	}
	if (!completed) {
		int	csr;

		rx->head = bd;

		/* REVISIT seems like "autoreq all but EOP" doesn't...
		 * setting it here "should" be racey, but seems to work
		 */
		csr = musb_readw(rx->hw_ep->regs, MUSB_RXCSR);
		if (is_host_active(cppi->musb)
				&& bd
				&& !(csr & MUSB_RXCSR_H_REQPKT)) {
			csr |= MUSB_RXCSR_H_REQPKT;
			musb_writew(regs, MUSB_RXCSR,
					MUSB_RXCSR_H_WZC_BITS | csr);
			csr = musb_readw(rx->hw_ep->regs, MUSB_RXCSR);
		}
	} else {
		rx->head = NULL;
		rx->tail = NULL;
	}

	cppi_dump_rx(6, rx, completed ? "/completed" : "/cleaned");
	return completed;
}

irqreturn_t cppi_interrupt(int irq, void *dev_id)
{
	struct musb		*musb = dev_id;
	struct cppi		*cppi;
	void __iomem		*tibase;
	struct musb_hw_ep	*hw_ep = NULL;
	u32			rx, tx;
	int			i, index;
	unsigned long		uninitialized_var(flags);

	cppi = container_of(musb->dma_controller, struct cppi, controller);
	if (cppi->irq)
		spin_lock_irqsave(&musb->lock, flags);

	tibase = musb->ctrl_base;

	tx = musb_readl(tibase, DAVINCI_TXCPPI_MASKED_REG);
	rx = musb_readl(tibase, DAVINCI_RXCPPI_MASKED_REG);

	if (!tx && !rx) {
		if (cppi->irq)
			spin_unlock_irqrestore(&musb->lock, flags);
		return IRQ_NONE;
	}

	DBG(4, "CPPI IRQ Tx%x Rx%x\n", tx, rx);

	/* process TX channels */
	for (index = 0; tx; tx = tx >> 1, index++) {
		struct cppi_channel		*tx_ch;
		struct cppi_tx_stateram __iomem	*tx_ram;
		bool				completed = false;
		struct cppi_descriptor		*bd;

		if (!(tx & 1))
			continue;

		tx_ch = cppi->tx + index;
		tx_ram = tx_ch->state_ram;

		/* FIXME  need a cppi_tx_scan() routine, which
		 * can also be called from abort code
		 */

		cppi_dump_tx(5, tx_ch, "/E");

		bd = tx_ch->head;

		/*
		 * If Head is null then this could mean that a abort interrupt
		 * that needs to be acknowledged.
		 */
		if (NULL == bd) {
			DBG(1, "null BD\n");
			musb_writel(&tx_ram->tx_complete, 0, 0);
			continue;
		}

		/* run through all completed BDs */
		for (i = 0; !completed && bd && i < NUM_TXCHAN_BD;
				i++, bd = bd->next) {
			u16	len;

			/* catch latest BD writes from CPPI */
			rmb();
			if (bd->hw_options & CPPI_OWN_SET)
				break;

			DBG(5, "C/TXBD %p n %x b %x off %x opt %x\n",
					bd, bd->hw_next, bd->hw_bufp,
					bd->hw_off_len, bd->hw_options);

			len = bd->hw_off_len & CPPI_BUFFER_LEN_MASK;
			tx_ch->channel.actual_len += len;

			tx_ch->last_processed = bd;

			/* write completion register to acknowledge
			 * processing of completed BDs, and possibly
			 * release the IRQ; EOQ might not be set ...
			 *
			 * REVISIT use the same ack strategy as rx
			 *
			 * REVISIT have observed bit 18 set; huh??
			 */
			/* if ((bd->hw_options & CPPI_EOQ_MASK)) */
				musb_writel(&tx_ram->tx_complete, 0, bd->dma);

			/* stop scanning on end-of-segment */
			if (bd->hw_next == 0)
				completed = true;
		}

		/* on end of segment, maybe go to next one */
		if (completed) {
			/* cppi_dump_tx(4, tx_ch, "/complete"); */

			/* transfer more, or report completion */
			if (tx_ch->offset >= tx_ch->buf_len) {
				tx_ch->head = NULL;
				tx_ch->tail = NULL;
				tx_ch->channel.status = MUSB_DMA_STATUS_FREE;

				hw_ep = tx_ch->hw_ep;

				musb_dma_completion(musb, index + 1, 1);

			} else {
				/* Bigger transfer than we could fit in
				 * that first batch of descriptors...
				 */
				cppi_next_tx_segment(musb, tx_ch);
			}
		} else
			tx_ch->head = bd;
	}

	/* Start processing the RX block */
	for (index = 0; rx; rx = rx >> 1, index++) {

		if (rx & 1) {
			struct cppi_channel		*rx_ch;

			rx_ch = cppi->rx + index;

			/* let incomplete dma segments finish */
			if (!cppi_rx_scan(cppi, index))
				continue;

			/* start another dma segment if needed */
			if (rx_ch->channel.actual_len != rx_ch->buf_len
					&& rx_ch->channel.actual_len
						== rx_ch->offset) {
				cppi_next_rx_segment(musb, rx_ch, 1);
				continue;
			}

			/* all segments completed! */
			rx_ch->channel.status = MUSB_DMA_STATUS_FREE;

			hw_ep = rx_ch->hw_ep;

			core_rxirq_disable(tibase, index + 1);
			musb_dma_completion(musb, index + 1, 0);
		}
	}

	/* write to CPPI EOI register to re-enable interrupts */
	musb_writel(tibase, DAVINCI_CPPI_EOI_REG, 0);

	if (cppi->irq)
		spin_unlock_irqrestore(&musb->lock, flags);

	return IRQ_HANDLED;
}

/* Instantiate a software object representing a DMA controller. */
struct dma_controller *__init
dma_controller_create(struct musb *musb, void __iomem *mregs)
{
	struct cppi		*controller;
	struct device		*dev = musb->controller;
	struct platform_device	*pdev = to_platform_device(dev);
	int			irq = platform_get_irq_byname(pdev, "dma");

	controller = kzalloc(sizeof *controller, GFP_KERNEL);
	if (!controller)
		return NULL;

	controller->mregs = mregs;
	controller->tibase = mregs - DAVINCI_BASE_OFFSET;

	controller->musb = musb;
	controller->controller.start = cppi_controller_start;
	controller->controller.stop = cppi_controller_stop;
	controller->controller.channel_alloc = cppi_channel_allocate;
	controller->controller.channel_release = cppi_channel_release;
	controller->controller.channel_program = cppi_channel_program;
	controller->controller.channel_abort = cppi_channel_abort;

	/* NOTE: allocating from on-chip SRAM would give the least
	 * contention for memory access, if that ever matters here.
	 */

	/* setup BufferPool */
	controller->pool = dma_pool_create("cppi",
			controller->musb->controller,
			sizeof(struct cppi_descriptor),
			CPPI_DESCRIPTOR_ALIGN, 0);
	if (!controller->pool) {
		kfree(controller);
		return NULL;
	}

	if (irq > 0) {
		if (request_irq(irq, cppi_interrupt, 0, "cppi-dma", musb)) {
			dev_err(dev, "request_irq %d failed!\n", irq);
			dma_controller_destroy(&controller->controller);
			return NULL;
		}
		controller->irq = irq;
	}

	return &controller->controller;
}

/*
 *  Destroy a previously-instantiated DMA controller.
 */
void dma_controller_destroy(struct dma_controller *c)
{
	struct cppi	*cppi;

	cppi = container_of(c, struct cppi, controller);

	if (cppi->irq)
		free_irq(cppi->irq, cppi->musb);

	/* assert:  caller stopped the controller first */
	dma_pool_destroy(cppi->pool);

	kfree(cppi);
}

/*
 * Context: controller irqlocked, endpoint selected
 */
static int cppi_channel_abort(struct dma_channel *channel)
{
	struct cppi_channel	*cppi_ch;
	struct cppi		*controller;
	void __iomem		*mbase;
	void __iomem		*tibase;
	void __iomem		*regs;
	u32			value;
	struct cppi_descriptor	*queue;

	cppi_ch = container_of(channel, struct cppi_channel, channel);

	controller = cppi_ch->controller;

	switch (channel->status) {
	case MUSB_DMA_STATUS_BUS_ABORT:
	case MUSB_DMA_STATUS_CORE_ABORT:
		/* from RX or TX fault irq handler */
	case MUSB_DMA_STATUS_BUSY:
		/* the hardware needs shutting down */
		regs = cppi_ch->hw_ep->regs;
		break;
	case MUSB_DMA_STATUS_UNKNOWN:
	case MUSB_DMA_STATUS_FREE:
		return 0;
	default:
		return -EINVAL;
	}

	if (!cppi_ch->transmit && cppi_ch->head)
		cppi_dump_rxq(3, "/abort", cppi_ch);

	mbase = controller->mregs;
	tibase = controller->tibase;

	queue = cppi_ch->head;
	cppi_ch->head = NULL;
	cppi_ch->tail = NULL;

	/* REVISIT should rely on caller having done this,
	 * and caller should rely on us not changing it.
	 * peripheral code is safe ... check host too.
	 */
	musb_ep_select(mbase, cppi_ch->index + 1);

	if (cppi_ch->transmit) {
		struct cppi_tx_stateram __iomem *tx_ram;
		/* REVISIT put timeouts on these controller handshakes */

		cppi_dump_tx(6, cppi_ch, " (teardown)");

		/* teardown DMA engine then usb core */
		do {
			value = musb_readl(tibase, DAVINCI_TXCPPI_TEAR_REG);
		} while (!(value & CPPI_TEAR_READY));
		musb_writel(tibase, DAVINCI_TXCPPI_TEAR_REG, cppi_ch->index);

		tx_ram = cppi_ch->state_ram;
		do {
			value = musb_readl(&tx_ram->tx_complete, 0);
		} while (0xFFFFFFFC != value);

		/* FIXME clean up the transfer state ... here?
		 * the completion routine should get called with
		 * an appropriate status code.
		 */

		value = musb_readw(regs, MUSB_TXCSR);
		value &= ~MUSB_TXCSR_DMAENAB;
		value |= MUSB_TXCSR_FLUSHFIFO;
		musb_writew(regs, MUSB_TXCSR, value);
		musb_writew(regs, MUSB_TXCSR, value);

		/*
		 * 1. Write to completion Ptr value 0x1(bit 0 set)
		 *    (write back mode)
		 * 2. Wait for abort interrupt and then put the channel in
		 *    compare mode by writing 1 to the tx_complete register.
		 */
		cppi_reset_tx(tx_ram, 1);
		cppi_ch->head = NULL;
		musb_writel(&tx_ram->tx_complete, 0, 1);
		cppi_dump_tx(5, cppi_ch, " (done teardown)");

		/* REVISIT tx side _should_ clean up the same way
		 * as the RX side ... this does no cleanup at all!
		 */

	} else /* RX */ {
		u16			csr;

		/* NOTE: docs don't guarantee any of this works ...  we
		 * expect that if the usb core stops telling the cppi core
		 * to pull more data from it, then it'll be safe to flush
		 * current RX DMA state iff any pending fifo transfer is done.
		 */

		core_rxirq_disable(tibase, cppi_ch->index + 1);

		/* for host, ensure ReqPkt is never set again */
		if (is_host_active(cppi_ch->controller->musb)) {
			value = musb_readl(tibase, DAVINCI_AUTOREQ_REG);
			value &= ~((0x3) << (cppi_ch->index * 2));
			musb_writel(tibase, DAVINCI_AUTOREQ_REG, value);
		}

		csr = musb_readw(regs, MUSB_RXCSR);

		/* for host, clear (just) ReqPkt at end of current packet(s) */
		if (is_host_active(cppi_ch->controller->musb)) {
			csr |= MUSB_RXCSR_H_WZC_BITS;
			csr &= ~MUSB_RXCSR_H_REQPKT;
		} else
			csr |= MUSB_RXCSR_P_WZC_BITS;

		/* clear dma enable */
		csr &= ~(MUSB_RXCSR_DMAENAB);
		musb_writew(regs, MUSB_RXCSR, csr);
		csr = musb_readw(regs, MUSB_RXCSR);

		/* Quiesce: wait for current dma to finish (if not cleanup).
		 * We can't use bit zero of stateram->rx_sop, since that
		 * refers to an entire "DMA packet" not just emptying the
		 * current fifo.  Most segments need multiple usb packets.
		 */
		if (channel->status == MUSB_DMA_STATUS_BUSY)
			udelay(50);

		/* scan the current list, reporting any data that was
		 * transferred and acking any IRQ
		 */
		cppi_rx_scan(controller, cppi_ch->index);

		/* clobber the existing state once it's idle
		 *
		 * NOTE:  arguably, we should also wait for all the other
		 * RX channels to quiesce (how??) and then temporarily
		 * disable RXCPPI_CTRL_REG ... but it seems that we can
		 * rely on the controller restarting from state ram, with
		 * only RXCPPI_BUFCNT state being bogus.  BUFCNT will
		 * correct itself after the next DMA transfer though.
		 *
		 * REVISIT does using rndis mode change that?
		 */
		cppi_reset_rx(cppi_ch->state_ram);

		/* next DMA request _should_ load cppi head ptr */

		/* ... we don't "free" that list, only mutate it in place.  */
		cppi_dump_rx(5, cppi_ch, " (done abort)");

		/* clean up previously pending bds */
		cppi_bd_free(cppi_ch, cppi_ch->last_processed);
		cppi_ch->last_processed = NULL;

		while (queue) {
			struct cppi_descriptor	*tmp = queue->next;

			cppi_bd_free(cppi_ch, queue);
			queue = tmp;
		}
	}

	channel->status = MUSB_DMA_STATUS_FREE;
	cppi_ch->buf_dma = 0;
	cppi_ch->offset = 0;
	cppi_ch->buf_len = 0;
	cppi_ch->maxpacket = 0;
	return 0;
}

/* TBD Queries:
 *
 * Power Management ... probably turn off cppi during suspend, restart;
 * check state ram?  Clocking is presumably shared with usb core.
 */

// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for Audio DMA Controller (ADMAC) on t8103 (M1) and other Apple chips
 *
 * Copyright (C) The Asahi Linux Contributors
 */

#include <linux/bits.h>
#include <linux/bitfield.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of_dma.h>
#include <linux/reset.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>

#include "dmaengine.h"

#define NCHANNELS_MAX	64
#define IRQ_NOUTPUTS	4

/*
 * For allocation purposes we split the cache
 * memory into blocks of fixed size (given in bytes).
 */
#define SRAM_BLOCK	2048

#define RING_WRITE_SLOT		GENMASK(1, 0)
#define RING_READ_SLOT		GENMASK(5, 4)
#define RING_FULL		BIT(9)
#define RING_EMPTY		BIT(8)
#define RING_ERR		BIT(10)

#define STATUS_DESC_DONE	BIT(0)
#define STATUS_ERR		BIT(6)

#define FLAG_DESC_NOTIFY	BIT(16)

#define REG_TX_START		0x0000
#define REG_TX_STOP		0x0004
#define REG_RX_START		0x0008
#define REG_RX_STOP		0x000c
#define REG_IMPRINT		0x0090
#define REG_TX_SRAM_SIZE	0x0094
#define REG_RX_SRAM_SIZE	0x0098

#define REG_CHAN_CTL(ch)	(0x8000 + (ch) * 0x200)
#define REG_CHAN_CTL_RST_RINGS	BIT(0)

#define REG_DESC_RING(ch)	(0x8070 + (ch) * 0x200)
#define REG_REPORT_RING(ch)	(0x8074 + (ch) * 0x200)

#define REG_RESIDUE(ch)		(0x8064 + (ch) * 0x200)

#define REG_BUS_WIDTH(ch)	(0x8040 + (ch) * 0x200)

#define BUS_WIDTH_8BIT		0x00
#define BUS_WIDTH_16BIT		0x01
#define BUS_WIDTH_32BIT		0x02
#define BUS_WIDTH_FRAME_2_WORDS	0x10
#define BUS_WIDTH_FRAME_4_WORDS	0x20

#define REG_CHAN_SRAM_CARVEOUT(ch)	(0x8050 + (ch) * 0x200)
#define CHAN_SRAM_CARVEOUT_SIZE		GENMASK(31, 16)
#define CHAN_SRAM_CARVEOUT_BASE		GENMASK(15, 0)

#define REG_CHAN_FIFOCTL(ch)	(0x8054 + (ch) * 0x200)
#define CHAN_FIFOCTL_LIMIT	GENMASK(31, 16)
#define CHAN_FIFOCTL_THRESHOLD	GENMASK(15, 0)

#define REG_DESC_WRITE(ch)	(0x10000 + ((ch) / 2) * 0x4 + ((ch) & 1) * 0x4000)
#define REG_REPORT_READ(ch)	(0x10100 + ((ch) / 2) * 0x4 + ((ch) & 1) * 0x4000)

#define REG_TX_INTSTATE(idx)		(0x0030 + (idx) * 4)
#define REG_RX_INTSTATE(idx)		(0x0040 + (idx) * 4)
#define REG_CHAN_INTSTATUS(ch, idx)	(0x8010 + (ch) * 0x200 + (idx) * 4)
#define REG_CHAN_INTMASK(ch, idx)	(0x8020 + (ch) * 0x200 + (idx) * 4)

struct admac_data;
struct admac_tx;

struct admac_chan {
	unsigned int no;
	struct admac_data *host;
	struct dma_chan chan;
	struct tasklet_struct tasklet;

	u32 carveout;

	spinlock_t lock;
	struct admac_tx *current_tx;
	int nperiod_acks;

	/*
	 * We maintain a 'submitted' and 'issued' list mainly for interface
	 * correctness. Typical use of the driver (per channel) will be
	 * prepping, submitting and issuing a single cyclic transaction which
	 * will stay current until terminate_all is called.
	 */
	struct list_head submitted;
	struct list_head issued;

	struct list_head to_free;
};

struct admac_sram {
	u32 size;
	/*
	 * SRAM_CARVEOUT has 16-bit fields, so the SRAM cannot be larger than
	 * 64K and a 32-bit bitfield over 2K blocks covers it.
	 */
	u32 allocated;
};

struct admac_data {
	struct dma_device dma;
	struct device *dev;
	__iomem void *base;
	struct reset_control *rstc;

	struct mutex cache_alloc_lock;
	struct admac_sram txcache, rxcache;

	int irq;
	int irq_index;
	int nchannels;
	struct admac_chan channels[];
};

struct admac_tx {
	struct dma_async_tx_descriptor tx;
	bool cyclic;
	dma_addr_t buf_addr;
	dma_addr_t buf_end;
	size_t buf_len;
	size_t period_len;

	size_t submitted_pos;
	size_t reclaimed_pos;

	struct list_head node;
};

static int admac_alloc_sram_carveout(struct admac_data *ad,
				     enum dma_transfer_direction dir,
				     u32 *out)
{
	struct admac_sram *sram;
	int i, ret = 0, nblocks;

	if (dir == DMA_MEM_TO_DEV)
		sram = &ad->txcache;
	else
		sram = &ad->rxcache;

	mutex_lock(&ad->cache_alloc_lock);

	nblocks = sram->size / SRAM_BLOCK;
	for (i = 0; i < nblocks; i++)
		if (!(sram->allocated & BIT(i)))
			break;

	if (i < nblocks) {
		*out = FIELD_PREP(CHAN_SRAM_CARVEOUT_BASE, i * SRAM_BLOCK) |
			FIELD_PREP(CHAN_SRAM_CARVEOUT_SIZE, SRAM_BLOCK);
		sram->allocated |= BIT(i);
	} else {
		ret = -EBUSY;
	}

	mutex_unlock(&ad->cache_alloc_lock);

	return ret;
}

static void admac_free_sram_carveout(struct admac_data *ad,
				     enum dma_transfer_direction dir,
				     u32 carveout)
{
	struct admac_sram *sram;
	u32 base = FIELD_GET(CHAN_SRAM_CARVEOUT_BASE, carveout);
	int i;

	if (dir == DMA_MEM_TO_DEV)
		sram = &ad->txcache;
	else
		sram = &ad->rxcache;

	if (WARN_ON(base >= sram->size))
		return;

	mutex_lock(&ad->cache_alloc_lock);
	i = base / SRAM_BLOCK;
	sram->allocated &= ~BIT(i);
	mutex_unlock(&ad->cache_alloc_lock);
}

static void admac_modify(struct admac_data *ad, int reg, u32 mask, u32 val)
{
	void __iomem *addr = ad->base + reg;
	u32 curr = readl_relaxed(addr);

	writel_relaxed((curr & ~mask) | (val & mask), addr);
}

static struct admac_chan *to_admac_chan(struct dma_chan *chan)
{
	return container_of(chan, struct admac_chan, chan);
}

static struct admac_tx *to_admac_tx(struct dma_async_tx_descriptor *tx)
{
	return container_of(tx, struct admac_tx, tx);
}

static enum dma_transfer_direction admac_chan_direction(int channo)
{
	/* Channel directions are hardwired */
	return (channo & 1) ? DMA_DEV_TO_MEM : DMA_MEM_TO_DEV;
}

static dma_cookie_t admac_tx_submit(struct dma_async_tx_descriptor *tx)
{
	struct admac_tx *adtx = to_admac_tx(tx);
	struct admac_chan *adchan = to_admac_chan(tx->chan);
	unsigned long flags;
	dma_cookie_t cookie;

	spin_lock_irqsave(&adchan->lock, flags);
	cookie = dma_cookie_assign(tx);
	list_add_tail(&adtx->node, &adchan->submitted);
	spin_unlock_irqrestore(&adchan->lock, flags);

	return cookie;
}

static int admac_desc_free(struct dma_async_tx_descriptor *tx)
{
	kfree(to_admac_tx(tx));

	return 0;
}

static struct dma_async_tx_descriptor *admac_prep_dma_cyclic(
		struct dma_chan *chan, dma_addr_t buf_addr, size_t buf_len,
		size_t period_len, enum dma_transfer_direction direction,
		unsigned long flags)
{
	struct admac_chan *adchan = container_of(chan, struct admac_chan, chan);
	struct admac_tx *adtx;

	if (direction != admac_chan_direction(adchan->no))
		return NULL;

	adtx = kzalloc(sizeof(*adtx), GFP_NOWAIT);
	if (!adtx)
		return NULL;

	adtx->cyclic = true;

	adtx->buf_addr = buf_addr;
	adtx->buf_len = buf_len;
	adtx->buf_end = buf_addr + buf_len;
	adtx->period_len = period_len;

	adtx->submitted_pos = 0;
	adtx->reclaimed_pos = 0;

	dma_async_tx_descriptor_init(&adtx->tx, chan);
	adtx->tx.tx_submit = admac_tx_submit;
	adtx->tx.desc_free = admac_desc_free;

	return &adtx->tx;
}

/*
 * Write one hardware descriptor for a dmaengine cyclic transaction.
 */
static void admac_cyclic_write_one_desc(struct admac_data *ad, int channo,
					struct admac_tx *tx)
{
	dma_addr_t addr;

	addr = tx->buf_addr + (tx->submitted_pos % tx->buf_len);

	/* If happens means we have buggy code */
	WARN_ON_ONCE(addr + tx->period_len > tx->buf_end);

	dev_dbg(ad->dev, "ch%d descriptor: addr=0x%pad len=0x%zx flags=0x%lx\n",
		channo, &addr, tx->period_len, FLAG_DESC_NOTIFY);

	writel_relaxed(lower_32_bits(addr), ad->base + REG_DESC_WRITE(channo));
	writel_relaxed(upper_32_bits(addr), ad->base + REG_DESC_WRITE(channo));
	writel_relaxed(tx->period_len,      ad->base + REG_DESC_WRITE(channo));
	writel_relaxed(FLAG_DESC_NOTIFY,    ad->base + REG_DESC_WRITE(channo));

	tx->submitted_pos += tx->period_len;
	tx->submitted_pos %= 2 * tx->buf_len;
}

/*
 * Write all the hardware descriptors for a dmaengine cyclic
 * transaction there is space for.
 */
static void admac_cyclic_write_desc(struct admac_data *ad, int channo,
				    struct admac_tx *tx)
{
	int i;

	for (i = 0; i < 4; i++) {
		if (readl_relaxed(ad->base + REG_DESC_RING(channo)) & RING_FULL)
			break;
		admac_cyclic_write_one_desc(ad, channo, tx);
	}
}

static int admac_ring_noccupied_slots(int ringval)
{
	int wrslot = FIELD_GET(RING_WRITE_SLOT, ringval);
	int rdslot = FIELD_GET(RING_READ_SLOT, ringval);

	if (wrslot != rdslot) {
		return (wrslot + 4 - rdslot) % 4;
	} else {
		WARN_ON((ringval & (RING_FULL | RING_EMPTY)) == 0);

		if (ringval & RING_FULL)
			return 4;
		else
			return 0;
	}
}

/*
 * Read from hardware the residue of a cyclic dmaengine transaction.
 */
static u32 admac_cyclic_read_residue(struct admac_data *ad, int channo,
				     struct admac_tx *adtx)
{
	u32 ring1, ring2;
	u32 residue1, residue2;
	int nreports;
	size_t pos;

	ring1 =    readl_relaxed(ad->base + REG_REPORT_RING(channo));
	residue1 = readl_relaxed(ad->base + REG_RESIDUE(channo));
	ring2 =    readl_relaxed(ad->base + REG_REPORT_RING(channo));
	residue2 = readl_relaxed(ad->base + REG_RESIDUE(channo));

	if (residue2 > residue1) {
		/*
		 * Controller must have loaded next descriptor between
		 * the two residue reads
		 */
		nreports = admac_ring_noccupied_slots(ring1) + 1;
	} else {
		/* No descriptor load between the two reads, ring2 is safe to use */
		nreports = admac_ring_noccupied_slots(ring2);
	}

	pos = adtx->reclaimed_pos + adtx->period_len * (nreports + 1) - residue2;

	return adtx->buf_len - pos % adtx->buf_len;
}

static enum dma_status admac_tx_status(struct dma_chan *chan, dma_cookie_t cookie,
				       struct dma_tx_state *txstate)
{
	struct admac_chan *adchan = to_admac_chan(chan);
	struct admac_data *ad = adchan->host;
	struct admac_tx *adtx;

	enum dma_status ret;
	size_t residue;
	unsigned long flags;

	ret = dma_cookie_status(chan, cookie, txstate);
	if (ret == DMA_COMPLETE || !txstate)
		return ret;

	spin_lock_irqsave(&adchan->lock, flags);
	adtx = adchan->current_tx;

	if (adtx && adtx->tx.cookie == cookie) {
		ret = DMA_IN_PROGRESS;
		residue = admac_cyclic_read_residue(ad, adchan->no, adtx);
	} else {
		ret = DMA_IN_PROGRESS;
		residue = 0;
		list_for_each_entry(adtx, &adchan->issued, node) {
			if (adtx->tx.cookie == cookie) {
				residue = adtx->buf_len;
				break;
			}
		}
	}
	spin_unlock_irqrestore(&adchan->lock, flags);

	dma_set_residue(txstate, residue);
	return ret;
}

static void admac_start_chan(struct admac_chan *adchan)
{
	struct admac_data *ad = adchan->host;
	u32 startbit = 1 << (adchan->no / 2);

	writel_relaxed(STATUS_DESC_DONE | STATUS_ERR,
		       ad->base + REG_CHAN_INTSTATUS(adchan->no, ad->irq_index));
	writel_relaxed(STATUS_DESC_DONE | STATUS_ERR,
		       ad->base + REG_CHAN_INTMASK(adchan->no, ad->irq_index));

	switch (admac_chan_direction(adchan->no)) {
	case DMA_MEM_TO_DEV:
		writel_relaxed(startbit, ad->base + REG_TX_START);
		break;
	case DMA_DEV_TO_MEM:
		writel_relaxed(startbit, ad->base + REG_RX_START);
		break;
	default:
		break;
	}
	dev_dbg(adchan->host->dev, "ch%d start\n", adchan->no);
}

static void admac_stop_chan(struct admac_chan *adchan)
{
	struct admac_data *ad = adchan->host;
	u32 stopbit = 1 << (adchan->no / 2);

	switch (admac_chan_direction(adchan->no)) {
	case DMA_MEM_TO_DEV:
		writel_relaxed(stopbit, ad->base + REG_TX_STOP);
		break;
	case DMA_DEV_TO_MEM:
		writel_relaxed(stopbit, ad->base + REG_RX_STOP);
		break;
	default:
		break;
	}
	dev_dbg(adchan->host->dev, "ch%d stop\n", adchan->no);
}

static void admac_reset_rings(struct admac_chan *adchan)
{
	struct admac_data *ad = adchan->host;

	writel_relaxed(REG_CHAN_CTL_RST_RINGS,
		       ad->base + REG_CHAN_CTL(adchan->no));
	writel_relaxed(0, ad->base + REG_CHAN_CTL(adchan->no));
}

static void admac_start_current_tx(struct admac_chan *adchan)
{
	struct admac_data *ad = adchan->host;
	int ch = adchan->no;

	admac_reset_rings(adchan);
	writel_relaxed(0, ad->base + REG_CHAN_CTL(ch));

	admac_cyclic_write_one_desc(ad, ch, adchan->current_tx);
	admac_start_chan(adchan);
	admac_cyclic_write_desc(ad, ch, adchan->current_tx);
}

static void admac_issue_pending(struct dma_chan *chan)
{
	struct admac_chan *adchan = to_admac_chan(chan);
	struct admac_tx *tx;
	unsigned long flags;

	spin_lock_irqsave(&adchan->lock, flags);
	list_splice_tail_init(&adchan->submitted, &adchan->issued);
	if (!list_empty(&adchan->issued) && !adchan->current_tx) {
		tx = list_first_entry(&adchan->issued, struct admac_tx, node);
		list_del(&tx->node);

		adchan->current_tx = tx;
		adchan->nperiod_acks = 0;
		admac_start_current_tx(adchan);
	}
	spin_unlock_irqrestore(&adchan->lock, flags);
}

static int admac_pause(struct dma_chan *chan)
{
	struct admac_chan *adchan = to_admac_chan(chan);

	admac_stop_chan(adchan);

	return 0;
}

static int admac_resume(struct dma_chan *chan)
{
	struct admac_chan *adchan = to_admac_chan(chan);

	admac_start_chan(adchan);

	return 0;
}

static int admac_terminate_all(struct dma_chan *chan)
{
	struct admac_chan *adchan = to_admac_chan(chan);
	unsigned long flags;

	spin_lock_irqsave(&adchan->lock, flags);
	admac_stop_chan(adchan);
	admac_reset_rings(adchan);

	adchan->current_tx = NULL;
	/*
	 * Descriptors can only be freed after the tasklet
	 * has been killed (in admac_synchronize).
	 */
	list_splice_tail_init(&adchan->submitted, &adchan->to_free);
	list_splice_tail_init(&adchan->issued, &adchan->to_free);
	spin_unlock_irqrestore(&adchan->lock, flags);

	return 0;
}

static void admac_synchronize(struct dma_chan *chan)
{
	struct admac_chan *adchan = to_admac_chan(chan);
	struct admac_tx *adtx, *_adtx;
	unsigned long flags;
	LIST_HEAD(head);

	spin_lock_irqsave(&adchan->lock, flags);
	list_splice_tail_init(&adchan->to_free, &head);
	spin_unlock_irqrestore(&adchan->lock, flags);

	tasklet_kill(&adchan->tasklet);

	list_for_each_entry_safe(adtx, _adtx, &head, node) {
		list_del(&adtx->node);
		admac_desc_free(&adtx->tx);
	}
}

static int admac_alloc_chan_resources(struct dma_chan *chan)
{
	struct admac_chan *adchan = to_admac_chan(chan);
	struct admac_data *ad = adchan->host;
	int ret;

	dma_cookie_init(&adchan->chan);
	ret = admac_alloc_sram_carveout(ad, admac_chan_direction(adchan->no),
					&adchan->carveout);
	if (ret < 0)
		return ret;

	writel_relaxed(adchan->carveout,
		       ad->base + REG_CHAN_SRAM_CARVEOUT(adchan->no));
	return 0;
}

static void admac_free_chan_resources(struct dma_chan *chan)
{
	struct admac_chan *adchan = to_admac_chan(chan);

	admac_terminate_all(chan);
	admac_synchronize(chan);
	admac_free_sram_carveout(adchan->host, admac_chan_direction(adchan->no),
				 adchan->carveout);
}

static struct dma_chan *admac_dma_of_xlate(struct of_phandle_args *dma_spec,
					   struct of_dma *ofdma)
{
	struct admac_data *ad = (struct admac_data *) ofdma->of_dma_data;
	unsigned int index;

	if (dma_spec->args_count != 1)
		return NULL;

	index = dma_spec->args[0];

	if (index >= ad->nchannels) {
		dev_err(ad->dev, "channel index %u out of bounds\n", index);
		return NULL;
	}

	return dma_get_slave_channel(&ad->channels[index].chan);
}

static int admac_drain_reports(struct admac_data *ad, int channo)
{
	int count;

	for (count = 0; count < 4; count++) {
		u32 countval_hi, countval_lo, unk1, flags;

		if (readl_relaxed(ad->base + REG_REPORT_RING(channo)) & RING_EMPTY)
			break;

		countval_lo = readl_relaxed(ad->base + REG_REPORT_READ(channo));
		countval_hi = readl_relaxed(ad->base + REG_REPORT_READ(channo));
		unk1 =        readl_relaxed(ad->base + REG_REPORT_READ(channo));
		flags =       readl_relaxed(ad->base + REG_REPORT_READ(channo));

		dev_dbg(ad->dev, "ch%d report: countval=0x%llx unk1=0x%x flags=0x%x\n",
			channo, ((u64) countval_hi) << 32 | countval_lo, unk1, flags);
	}

	return count;
}

static void admac_handle_status_err(struct admac_data *ad, int channo)
{
	bool handled = false;

	if (readl_relaxed(ad->base + REG_DESC_RING(channo)) & RING_ERR) {
		writel_relaxed(RING_ERR, ad->base + REG_DESC_RING(channo));
		dev_err_ratelimited(ad->dev, "ch%d descriptor ring error\n", channo);
		handled = true;
	}

	if (readl_relaxed(ad->base + REG_REPORT_RING(channo)) & RING_ERR) {
		writel_relaxed(RING_ERR, ad->base + REG_REPORT_RING(channo));
		dev_err_ratelimited(ad->dev, "ch%d report ring error\n", channo);
		handled = true;
	}

	if (unlikely(!handled)) {
		dev_err(ad->dev, "ch%d unknown error, masking errors as cause of IRQs\n", channo);
		admac_modify(ad, REG_CHAN_INTMASK(channo, ad->irq_index),
			     STATUS_ERR, 0);
	}
}

static void admac_handle_status_desc_done(struct admac_data *ad, int channo)
{
	struct admac_chan *adchan = &ad->channels[channo];
	unsigned long flags;
	int nreports;

	writel_relaxed(STATUS_DESC_DONE,
		       ad->base + REG_CHAN_INTSTATUS(channo, ad->irq_index));

	spin_lock_irqsave(&adchan->lock, flags);
	nreports = admac_drain_reports(ad, channo);

	if (adchan->current_tx) {
		struct admac_tx *tx = adchan->current_tx;

		adchan->nperiod_acks += nreports;
		tx->reclaimed_pos += nreports * tx->period_len;
		tx->reclaimed_pos %= 2 * tx->buf_len;

		admac_cyclic_write_desc(ad, channo, tx);
		tasklet_schedule(&adchan->tasklet);
	}
	spin_unlock_irqrestore(&adchan->lock, flags);
}

static void admac_handle_chan_int(struct admac_data *ad, int no)
{
	u32 cause = readl_relaxed(ad->base + REG_CHAN_INTSTATUS(no, ad->irq_index));

	if (cause & STATUS_ERR)
		admac_handle_status_err(ad, no);

	if (cause & STATUS_DESC_DONE)
		admac_handle_status_desc_done(ad, no);
}

static irqreturn_t admac_interrupt(int irq, void *devid)
{
	struct admac_data *ad = devid;
	u32 rx_intstate, tx_intstate;
	int i;

	rx_intstate = readl_relaxed(ad->base + REG_RX_INTSTATE(ad->irq_index));
	tx_intstate = readl_relaxed(ad->base + REG_TX_INTSTATE(ad->irq_index));

	if (!tx_intstate && !rx_intstate)
		return IRQ_NONE;

	for (i = 0; i < ad->nchannels; i += 2) {
		if (tx_intstate & 1)
			admac_handle_chan_int(ad, i);
		tx_intstate >>= 1;
	}

	for (i = 1; i < ad->nchannels; i += 2) {
		if (rx_intstate & 1)
			admac_handle_chan_int(ad, i);
		rx_intstate >>= 1;
	}

	return IRQ_HANDLED;
}

static void admac_chan_tasklet(struct tasklet_struct *t)
{
	struct admac_chan *adchan = from_tasklet(adchan, t, tasklet);
	struct admac_tx *adtx;
	struct dmaengine_desc_callback cb;
	struct dmaengine_result tx_result;
	int nacks;

	spin_lock_irq(&adchan->lock);
	adtx = adchan->current_tx;
	nacks = adchan->nperiod_acks;
	adchan->nperiod_acks = 0;
	spin_unlock_irq(&adchan->lock);

	if (!adtx || !nacks)
		return;

	tx_result.result = DMA_TRANS_NOERROR;
	tx_result.residue = 0;

	dmaengine_desc_get_callback(&adtx->tx, &cb);
	while (nacks--)
		dmaengine_desc_callback_invoke(&cb, &tx_result);
}

static int admac_device_config(struct dma_chan *chan,
			       struct dma_slave_config *config)
{
	struct admac_chan *adchan = to_admac_chan(chan);
	struct admac_data *ad = adchan->host;
	bool is_tx = admac_chan_direction(adchan->no) == DMA_MEM_TO_DEV;
	int wordsize = 0;
	u32 bus_width = 0;

	switch (is_tx ? config->dst_addr_width : config->src_addr_width) {
	case DMA_SLAVE_BUSWIDTH_1_BYTE:
		wordsize = 1;
		bus_width |= BUS_WIDTH_8BIT;
		break;
	case DMA_SLAVE_BUSWIDTH_2_BYTES:
		wordsize = 2;
		bus_width |= BUS_WIDTH_16BIT;
		break;
	case DMA_SLAVE_BUSWIDTH_4_BYTES:
		wordsize = 4;
		bus_width |= BUS_WIDTH_32BIT;
		break;
	default:
		return -EINVAL;
	}

	/*
	 * We take port_window_size to be the number of words in a frame.
	 *
	 * The controller has some means of out-of-band signalling, to the peripheral,
	 * of words position in a frame. That's where the importance of this control
	 * comes from.
	 */
	switch (is_tx ? config->dst_port_window_size : config->src_port_window_size) {
	case 0 ... 1:
		break;
	case 2:
		bus_width |= BUS_WIDTH_FRAME_2_WORDS;
		break;
	case 4:
		bus_width |= BUS_WIDTH_FRAME_4_WORDS;
		break;
	default:
		return -EINVAL;
	}

	writel_relaxed(bus_width, ad->base + REG_BUS_WIDTH(adchan->no));

	/*
	 * By FIFOCTL_LIMIT we seem to set the maximal number of bytes allowed to be
	 * held in controller's per-channel FIFO. Transfers seem to be triggered
	 * around the time FIFO occupancy touches FIFOCTL_THRESHOLD.
	 *
	 * The numbers we set are more or less arbitrary.
	 */
	writel_relaxed(FIELD_PREP(CHAN_FIFOCTL_LIMIT, 0x30 * wordsize)
		       | FIELD_PREP(CHAN_FIFOCTL_THRESHOLD, 0x18 * wordsize),
		       ad->base + REG_CHAN_FIFOCTL(adchan->no));

	return 0;
}

static int admac_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct admac_data *ad;
	struct dma_device *dma;
	int nchannels;
	int err, irq, i;

	err = of_property_read_u32(np, "dma-channels", &nchannels);
	if (err || nchannels > NCHANNELS_MAX) {
		dev_err(&pdev->dev, "missing or invalid dma-channels property\n");
		return -EINVAL;
	}

	ad = devm_kzalloc(&pdev->dev, struct_size(ad, channels, nchannels), GFP_KERNEL);
	if (!ad)
		return -ENOMEM;

	platform_set_drvdata(pdev, ad);
	ad->dev = &pdev->dev;
	ad->nchannels = nchannels;
	mutex_init(&ad->cache_alloc_lock);

	/*
	 * The controller has 4 IRQ outputs. Try them all until
	 * we find one we can use.
	 */
	for (i = 0; i < IRQ_NOUTPUTS; i++) {
		irq = platform_get_irq_optional(pdev, i);
		if (irq >= 0) {
			ad->irq_index = i;
			break;
		}
	}

	if (irq < 0)
		return dev_err_probe(&pdev->dev, irq, "no usable interrupt\n");
	ad->irq = irq;

	ad->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(ad->base))
		return dev_err_probe(&pdev->dev, PTR_ERR(ad->base),
				     "unable to obtain MMIO resource\n");

	ad->rstc = devm_reset_control_get_optional_shared(&pdev->dev, NULL);
	if (IS_ERR(ad->rstc))
		return PTR_ERR(ad->rstc);

	dma = &ad->dma;

	dma_cap_set(DMA_PRIVATE, dma->cap_mask);
	dma_cap_set(DMA_CYCLIC, dma->cap_mask);

	dma->dev = &pdev->dev;
	dma->device_alloc_chan_resources = admac_alloc_chan_resources;
	dma->device_free_chan_resources = admac_free_chan_resources;
	dma->device_tx_status = admac_tx_status;
	dma->device_issue_pending = admac_issue_pending;
	dma->device_terminate_all = admac_terminate_all;
	dma->device_synchronize = admac_synchronize;
	dma->device_prep_dma_cyclic = admac_prep_dma_cyclic;
	dma->device_config = admac_device_config;
	dma->device_pause = admac_pause;
	dma->device_resume = admac_resume;

	dma->directions = BIT(DMA_MEM_TO_DEV) | BIT(DMA_DEV_TO_MEM);
	dma->residue_granularity = DMA_RESIDUE_GRANULARITY_BURST;
	dma->dst_addr_widths = BIT(DMA_SLAVE_BUSWIDTH_1_BYTE) |
			BIT(DMA_SLAVE_BUSWIDTH_2_BYTES) |
			BIT(DMA_SLAVE_BUSWIDTH_4_BYTES);

	INIT_LIST_HEAD(&dma->channels);
	for (i = 0; i < nchannels; i++) {
		struct admac_chan *adchan = &ad->channels[i];

		adchan->host = ad;
		adchan->no = i;
		adchan->chan.device = &ad->dma;
		spin_lock_init(&adchan->lock);
		INIT_LIST_HEAD(&adchan->submitted);
		INIT_LIST_HEAD(&adchan->issued);
		INIT_LIST_HEAD(&adchan->to_free);
		list_add_tail(&adchan->chan.device_node, &dma->channels);
		tasklet_setup(&adchan->tasklet, admac_chan_tasklet);
	}

	err = reset_control_reset(ad->rstc);
	if (err)
		return dev_err_probe(&pdev->dev, err,
				     "unable to trigger reset\n");

	err = request_irq(irq, admac_interrupt, 0, dev_name(&pdev->dev), ad);
	if (err) {
		dev_err_probe(&pdev->dev, err,
				"unable to register interrupt\n");
		goto free_reset;
	}

	err = dma_async_device_register(&ad->dma);
	if (err) {
		dev_err_probe(&pdev->dev, err, "failed to register DMA device\n");
		goto free_irq;
	}

	err = of_dma_controller_register(pdev->dev.of_node, admac_dma_of_xlate, ad);
	if (err) {
		dma_async_device_unregister(&ad->dma);
		dev_err_probe(&pdev->dev, err, "failed to register with OF\n");
		goto free_irq;
	}

	ad->txcache.size = readl_relaxed(ad->base + REG_TX_SRAM_SIZE);
	ad->rxcache.size = readl_relaxed(ad->base + REG_RX_SRAM_SIZE);

	dev_info(&pdev->dev, "Audio DMA Controller\n");
	dev_info(&pdev->dev, "imprint %x TX cache %u RX cache %u\n",
		 readl_relaxed(ad->base + REG_IMPRINT), ad->txcache.size, ad->rxcache.size);

	return 0;

free_irq:
	free_irq(ad->irq, ad);
free_reset:
	reset_control_rearm(ad->rstc);
	return err;
}

static int admac_remove(struct platform_device *pdev)
{
	struct admac_data *ad = platform_get_drvdata(pdev);

	of_dma_controller_free(pdev->dev.of_node);
	dma_async_device_unregister(&ad->dma);
	free_irq(ad->irq, ad);
	reset_control_rearm(ad->rstc);

	return 0;
}

static const struct of_device_id admac_of_match[] = {
	{ .compatible = "apple,admac", },
	{ }
};
MODULE_DEVICE_TABLE(of, admac_of_match);

static struct platform_driver apple_admac_driver = {
	.driver = {
		.name = "apple-admac",
		.of_match_table = admac_of_match,
	},
	.probe = admac_probe,
	.remove = admac_remove,
};
module_platform_driver(apple_admac_driver);

MODULE_AUTHOR("Martin Povišer <povik+lin@cutebit.org>");
MODULE_DESCRIPTION("Driver for Audio DMA Controller (ADMAC) on Apple SoCs");
MODULE_LICENSE("GPL");

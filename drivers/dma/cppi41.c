#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/of_dma.h>
#include <linux/of_irq.h>
#include <linux/dmapool.h>
#include <linux/interrupt.h>
#include <linux/of_address.h>
#include <linux/pm_runtime.h>
#include "dmaengine.h"

#define DESC_TYPE	27
#define DESC_TYPE_HOST	0x10
#define DESC_TYPE_TEARD	0x13

#define TD_DESC_IS_RX	(1 << 16)
#define TD_DESC_DMA_NUM	10

#define DESC_LENGTH_BITS_NUM	21

#define DESC_TYPE_USB	(5 << 26)
#define DESC_PD_COMPLETE	(1 << 31)

/* DMA engine */
#define DMA_TDFDQ	4
#define DMA_TXGCR(x)	(0x800 + (x) * 0x20)
#define DMA_RXGCR(x)	(0x808 + (x) * 0x20)
#define RXHPCRA0		4

#define GCR_CHAN_ENABLE		(1 << 31)
#define GCR_TEARDOWN		(1 << 30)
#define GCR_STARV_RETRY		(1 << 24)
#define GCR_DESC_TYPE_HOST	(1 << 14)

/* DMA scheduler */
#define DMA_SCHED_CTRL		0
#define DMA_SCHED_CTRL_EN	(1 << 31)
#define DMA_SCHED_WORD(x)	((x) * 4 + 0x800)

#define SCHED_ENTRY0_CHAN(x)	((x) << 0)
#define SCHED_ENTRY0_IS_RX	(1 << 7)

#define SCHED_ENTRY1_CHAN(x)	((x) << 8)
#define SCHED_ENTRY1_IS_RX	(1 << 15)

#define SCHED_ENTRY2_CHAN(x)	((x) << 16)
#define SCHED_ENTRY2_IS_RX	(1 << 23)

#define SCHED_ENTRY3_CHAN(x)	((x) << 24)
#define SCHED_ENTRY3_IS_RX	(1 << 31)

/* Queue manager */
/* 4 KiB of memory for descriptors, 2 for each endpoint */
#define ALLOC_DECS_NUM		128
#define DESCS_AREAS		1
#define TOTAL_DESCS_NUM		(ALLOC_DECS_NUM * DESCS_AREAS)
#define QMGR_SCRATCH_SIZE	(TOTAL_DESCS_NUM * 4)

#define QMGR_LRAM0_BASE		0x80
#define QMGR_LRAM_SIZE		0x84
#define QMGR_LRAM1_BASE		0x88
#define QMGR_MEMBASE(x)		(0x1000 + (x) * 0x10)
#define QMGR_MEMCTRL(x)		(0x1004 + (x) * 0x10)
#define QMGR_MEMCTRL_IDX_SH	16
#define QMGR_MEMCTRL_DESC_SH	8

#define QMGR_NUM_PEND	5
#define QMGR_PEND(x)	(0x90 + (x) * 4)

#define QMGR_PENDING_SLOT_Q(x)	(x / 32)
#define QMGR_PENDING_BIT_Q(x)	(x % 32)

#define QMGR_QUEUE_A(n)	(0x2000 + (n) * 0x10)
#define QMGR_QUEUE_B(n)	(0x2004 + (n) * 0x10)
#define QMGR_QUEUE_C(n)	(0x2008 + (n) * 0x10)
#define QMGR_QUEUE_D(n)	(0x200c + (n) * 0x10)

/* Glue layer specific */
/* USBSS  / USB AM335x */
#define USBSS_IRQ_STATUS	0x28
#define USBSS_IRQ_ENABLER	0x2c
#define USBSS_IRQ_CLEARR	0x30

#define USBSS_IRQ_PD_COMP	(1 <<  2)

struct cppi41_channel {
	struct dma_chan chan;
	struct dma_async_tx_descriptor txd;
	struct cppi41_dd *cdd;
	struct cppi41_desc *desc;
	dma_addr_t desc_phys;
	void __iomem *gcr_reg;
	int is_tx;
	u32 residue;

	unsigned int q_num;
	unsigned int q_comp_num;
	unsigned int port_num;

	unsigned td_retry;
	unsigned td_queued:1;
	unsigned td_seen:1;
	unsigned td_desc_seen:1;
};

struct cppi41_desc {
	u32 pd0;
	u32 pd1;
	u32 pd2;
	u32 pd3;
	u32 pd4;
	u32 pd5;
	u32 pd6;
	u32 pd7;
} __aligned(32);

struct chan_queues {
	u16 submit;
	u16 complete;
};

struct cppi41_dd {
	struct dma_device ddev;

	void *qmgr_scratch;
	dma_addr_t scratch_phys;

	struct cppi41_desc *cd;
	dma_addr_t descs_phys;
	u32 first_td_desc;
	struct cppi41_channel *chan_busy[ALLOC_DECS_NUM];

	void __iomem *usbss_mem;
	void __iomem *ctrl_mem;
	void __iomem *sched_mem;
	void __iomem *qmgr_mem;
	unsigned int irq;
	const struct chan_queues *queues_rx;
	const struct chan_queues *queues_tx;
	struct chan_queues td_queue;

	/* context for suspend/resume */
	unsigned int dma_tdfdq;
};

#define FIST_COMPLETION_QUEUE	93
static struct chan_queues usb_queues_tx[] = {
	/* USB0 ENDP 1 */
	[ 0] = { .submit = 32, .complete =  93},
	[ 1] = { .submit = 34, .complete =  94},
	[ 2] = { .submit = 36, .complete =  95},
	[ 3] = { .submit = 38, .complete =  96},
	[ 4] = { .submit = 40, .complete =  97},
	[ 5] = { .submit = 42, .complete =  98},
	[ 6] = { .submit = 44, .complete =  99},
	[ 7] = { .submit = 46, .complete = 100},
	[ 8] = { .submit = 48, .complete = 101},
	[ 9] = { .submit = 50, .complete = 102},
	[10] = { .submit = 52, .complete = 103},
	[11] = { .submit = 54, .complete = 104},
	[12] = { .submit = 56, .complete = 105},
	[13] = { .submit = 58, .complete = 106},
	[14] = { .submit = 60, .complete = 107},

	/* USB1 ENDP1 */
	[15] = { .submit = 62, .complete = 125},
	[16] = { .submit = 64, .complete = 126},
	[17] = { .submit = 66, .complete = 127},
	[18] = { .submit = 68, .complete = 128},
	[19] = { .submit = 70, .complete = 129},
	[20] = { .submit = 72, .complete = 130},
	[21] = { .submit = 74, .complete = 131},
	[22] = { .submit = 76, .complete = 132},
	[23] = { .submit = 78, .complete = 133},
	[24] = { .submit = 80, .complete = 134},
	[25] = { .submit = 82, .complete = 135},
	[26] = { .submit = 84, .complete = 136},
	[27] = { .submit = 86, .complete = 137},
	[28] = { .submit = 88, .complete = 138},
	[29] = { .submit = 90, .complete = 139},
};

static const struct chan_queues usb_queues_rx[] = {
	/* USB0 ENDP 1 */
	[ 0] = { .submit =  1, .complete = 109},
	[ 1] = { .submit =  2, .complete = 110},
	[ 2] = { .submit =  3, .complete = 111},
	[ 3] = { .submit =  4, .complete = 112},
	[ 4] = { .submit =  5, .complete = 113},
	[ 5] = { .submit =  6, .complete = 114},
	[ 6] = { .submit =  7, .complete = 115},
	[ 7] = { .submit =  8, .complete = 116},
	[ 8] = { .submit =  9, .complete = 117},
	[ 9] = { .submit = 10, .complete = 118},
	[10] = { .submit = 11, .complete = 119},
	[11] = { .submit = 12, .complete = 120},
	[12] = { .submit = 13, .complete = 121},
	[13] = { .submit = 14, .complete = 122},
	[14] = { .submit = 15, .complete = 123},

	/* USB1 ENDP 1 */
	[15] = { .submit = 16, .complete = 141},
	[16] = { .submit = 17, .complete = 142},
	[17] = { .submit = 18, .complete = 143},
	[18] = { .submit = 19, .complete = 144},
	[19] = { .submit = 20, .complete = 145},
	[20] = { .submit = 21, .complete = 146},
	[21] = { .submit = 22, .complete = 147},
	[22] = { .submit = 23, .complete = 148},
	[23] = { .submit = 24, .complete = 149},
	[24] = { .submit = 25, .complete = 150},
	[25] = { .submit = 26, .complete = 151},
	[26] = { .submit = 27, .complete = 152},
	[27] = { .submit = 28, .complete = 153},
	[28] = { .submit = 29, .complete = 154},
	[29] = { .submit = 30, .complete = 155},
};

struct cppi_glue_infos {
	irqreturn_t (*isr)(int irq, void *data);
	const struct chan_queues *queues_rx;
	const struct chan_queues *queues_tx;
	struct chan_queues td_queue;
};

static struct cppi41_channel *to_cpp41_chan(struct dma_chan *c)
{
	return container_of(c, struct cppi41_channel, chan);
}

static struct cppi41_channel *desc_to_chan(struct cppi41_dd *cdd, u32 desc)
{
	struct cppi41_channel *c;
	u32 descs_size;
	u32 desc_num;

	descs_size = sizeof(struct cppi41_desc) * ALLOC_DECS_NUM;

	if (!((desc >= cdd->descs_phys) &&
			(desc < (cdd->descs_phys + descs_size)))) {
		return NULL;
	}

	desc_num = (desc - cdd->descs_phys) / sizeof(struct cppi41_desc);
	BUG_ON(desc_num >= ALLOC_DECS_NUM);
	c = cdd->chan_busy[desc_num];
	cdd->chan_busy[desc_num] = NULL;
	return c;
}

static void cppi_writel(u32 val, void *__iomem *mem)
{
	__raw_writel(val, mem);
}

static u32 cppi_readl(void *__iomem *mem)
{
	return __raw_readl(mem);
}

static u32 pd_trans_len(u32 val)
{
	return val & ((1 << (DESC_LENGTH_BITS_NUM + 1)) - 1);
}

static u32 cppi41_pop_desc(struct cppi41_dd *cdd, unsigned queue_num)
{
	u32 desc;

	desc = cppi_readl(cdd->qmgr_mem + QMGR_QUEUE_D(queue_num));
	desc &= ~0x1f;
	return desc;
}

static irqreturn_t cppi41_irq(int irq, void *data)
{
	struct cppi41_dd *cdd = data;
	struct cppi41_channel *c;
	u32 status;
	int i;

	status = cppi_readl(cdd->usbss_mem + USBSS_IRQ_STATUS);
	if (!(status & USBSS_IRQ_PD_COMP))
		return IRQ_NONE;
	cppi_writel(status, cdd->usbss_mem + USBSS_IRQ_STATUS);

	for (i = QMGR_PENDING_SLOT_Q(FIST_COMPLETION_QUEUE); i < QMGR_NUM_PEND;
			i++) {
		u32 val;
		u32 q_num;

		val = cppi_readl(cdd->qmgr_mem + QMGR_PEND(i));
		if (i == QMGR_PENDING_SLOT_Q(FIST_COMPLETION_QUEUE) && val) {
			u32 mask;
			/* set corresponding bit for completetion Q 93 */
			mask = 1 << QMGR_PENDING_BIT_Q(FIST_COMPLETION_QUEUE);
			/* not set all bits for queues less than Q 93 */
			mask--;
			/* now invert and keep only Q 93+ set */
			val &= ~mask;
		}

		if (val)
			__iormb();

		while (val) {
			u32 desc;

			q_num = __fls(val);
			val &= ~(1 << q_num);
			q_num += 32 * i;
			desc = cppi41_pop_desc(cdd, q_num);
			c = desc_to_chan(cdd, desc);
			if (WARN_ON(!c)) {
				pr_err("%s() q %d desc %08x\n", __func__,
						q_num, desc);
				continue;
			}
			c->residue = pd_trans_len(c->desc->pd6) -
				pd_trans_len(c->desc->pd0);

			dma_cookie_complete(&c->txd);
			c->txd.callback(c->txd.callback_param);
		}
	}
	return IRQ_HANDLED;
}

static dma_cookie_t cppi41_tx_submit(struct dma_async_tx_descriptor *tx)
{
	dma_cookie_t cookie;

	cookie = dma_cookie_assign(tx);

	return cookie;
}

static int cppi41_dma_alloc_chan_resources(struct dma_chan *chan)
{
	struct cppi41_channel *c = to_cpp41_chan(chan);

	dma_cookie_init(chan);
	dma_async_tx_descriptor_init(&c->txd, chan);
	c->txd.tx_submit = cppi41_tx_submit;

	if (!c->is_tx)
		cppi_writel(c->q_num, c->gcr_reg + RXHPCRA0);

	return 0;
}

static void cppi41_dma_free_chan_resources(struct dma_chan *chan)
{
}

static enum dma_status cppi41_dma_tx_status(struct dma_chan *chan,
	dma_cookie_t cookie, struct dma_tx_state *txstate)
{
	struct cppi41_channel *c = to_cpp41_chan(chan);
	enum dma_status ret;

	/* lock */
	ret = dma_cookie_status(chan, cookie, txstate);
	if (txstate && ret == DMA_COMPLETE)
		txstate->residue = c->residue;
	/* unlock */

	return ret;
}

static void push_desc_queue(struct cppi41_channel *c)
{
	struct cppi41_dd *cdd = c->cdd;
	u32 desc_num;
	u32 desc_phys;
	u32 reg;

	desc_phys = lower_32_bits(c->desc_phys);
	desc_num = (desc_phys - cdd->descs_phys) / sizeof(struct cppi41_desc);
	WARN_ON(cdd->chan_busy[desc_num]);
	cdd->chan_busy[desc_num] = c;

	reg = (sizeof(struct cppi41_desc) - 24) / 4;
	reg |= desc_phys;
	cppi_writel(reg, cdd->qmgr_mem + QMGR_QUEUE_D(c->q_num));
}

static void cppi41_dma_issue_pending(struct dma_chan *chan)
{
	struct cppi41_channel *c = to_cpp41_chan(chan);
	u32 reg;

	c->residue = 0;

	reg = GCR_CHAN_ENABLE;
	if (!c->is_tx) {
		reg |= GCR_STARV_RETRY;
		reg |= GCR_DESC_TYPE_HOST;
		reg |= c->q_comp_num;
	}

	cppi_writel(reg, c->gcr_reg);

	/*
	 * We don't use writel() but __raw_writel() so we have to make sure
	 * that the DMA descriptor in coherent memory made to the main memory
	 * before starting the dma engine.
	 */
	__iowmb();
	push_desc_queue(c);
}

static u32 get_host_pd0(u32 length)
{
	u32 reg;

	reg = DESC_TYPE_HOST << DESC_TYPE;
	reg |= length;

	return reg;
}

static u32 get_host_pd1(struct cppi41_channel *c)
{
	u32 reg;

	reg = 0;

	return reg;
}

static u32 get_host_pd2(struct cppi41_channel *c)
{
	u32 reg;

	reg = DESC_TYPE_USB;
	reg |= c->q_comp_num;

	return reg;
}

static u32 get_host_pd3(u32 length)
{
	u32 reg;

	/* PD3 = packet size */
	reg = length;

	return reg;
}

static u32 get_host_pd6(u32 length)
{
	u32 reg;

	/* PD6 buffer size */
	reg = DESC_PD_COMPLETE;
	reg |= length;

	return reg;
}

static u32 get_host_pd4_or_7(u32 addr)
{
	u32 reg;

	reg = addr;

	return reg;
}

static u32 get_host_pd5(void)
{
	u32 reg;

	reg = 0;

	return reg;
}

static struct dma_async_tx_descriptor *cppi41_dma_prep_slave_sg(
	struct dma_chan *chan, struct scatterlist *sgl, unsigned sg_len,
	enum dma_transfer_direction dir, unsigned long tx_flags, void *context)
{
	struct cppi41_channel *c = to_cpp41_chan(chan);
	struct cppi41_desc *d;
	struct scatterlist *sg;
	unsigned int i;
	unsigned int num;

	num = 0;
	d = c->desc;
	for_each_sg(sgl, sg, sg_len, i) {
		u32 addr;
		u32 len;

		/* We need to use more than one desc once musb supports sg */
		BUG_ON(num > 0);
		addr = lower_32_bits(sg_dma_address(sg));
		len = sg_dma_len(sg);

		d->pd0 = get_host_pd0(len);
		d->pd1 = get_host_pd1(c);
		d->pd2 = get_host_pd2(c);
		d->pd3 = get_host_pd3(len);
		d->pd4 = get_host_pd4_or_7(addr);
		d->pd5 = get_host_pd5();
		d->pd6 = get_host_pd6(len);
		d->pd7 = get_host_pd4_or_7(addr);

		d++;
	}

	return &c->txd;
}

static int cpp41_cfg_chan(struct cppi41_channel *c,
		struct dma_slave_config *cfg)
{
	return 0;
}

static void cppi41_compute_td_desc(struct cppi41_desc *d)
{
	d->pd0 = DESC_TYPE_TEARD << DESC_TYPE;
}

static int cppi41_tear_down_chan(struct cppi41_channel *c)
{
	struct cppi41_dd *cdd = c->cdd;
	struct cppi41_desc *td;
	u32 reg;
	u32 desc_phys;
	u32 td_desc_phys;

	td = cdd->cd;
	td += cdd->first_td_desc;

	td_desc_phys = cdd->descs_phys;
	td_desc_phys += cdd->first_td_desc * sizeof(struct cppi41_desc);

	if (!c->td_queued) {
		cppi41_compute_td_desc(td);
		__iowmb();

		reg = (sizeof(struct cppi41_desc) - 24) / 4;
		reg |= td_desc_phys;
		cppi_writel(reg, cdd->qmgr_mem +
				QMGR_QUEUE_D(cdd->td_queue.submit));

		reg = GCR_CHAN_ENABLE;
		if (!c->is_tx) {
			reg |= GCR_STARV_RETRY;
			reg |= GCR_DESC_TYPE_HOST;
			reg |= c->q_comp_num;
		}
		reg |= GCR_TEARDOWN;
		cppi_writel(reg, c->gcr_reg);
		c->td_queued = 1;
		c->td_retry = 100;
	}

	if (!c->td_seen || !c->td_desc_seen) {

		desc_phys = cppi41_pop_desc(cdd, cdd->td_queue.complete);
		if (!desc_phys)
			desc_phys = cppi41_pop_desc(cdd, c->q_comp_num);

		if (desc_phys == c->desc_phys) {
			c->td_desc_seen = 1;

		} else if (desc_phys == td_desc_phys) {
			u32 pd0;

			__iormb();
			pd0 = td->pd0;
			WARN_ON((pd0 >> DESC_TYPE) != DESC_TYPE_TEARD);
			WARN_ON(!c->is_tx && !(pd0 & TD_DESC_IS_RX));
			WARN_ON((pd0 & 0x1f) != c->port_num);
			c->td_seen = 1;
		} else if (desc_phys) {
			WARN_ON_ONCE(1);
		}
	}
	c->td_retry--;
	/*
	 * If the TX descriptor / channel is in use, the caller needs to poke
	 * his TD bit multiple times. After that he hardware releases the
	 * transfer descriptor followed by TD descriptor. Waiting seems not to
	 * cause any difference.
	 * RX seems to be thrown out right away. However once the TearDown
	 * descriptor gets through we are done. If we have seens the transfer
	 * descriptor before the TD we fetch it from enqueue, it has to be
	 * there waiting for us.
	 */
	if (!c->td_seen && c->td_retry)
		return -EAGAIN;

	WARN_ON(!c->td_retry);
	if (!c->td_desc_seen) {
		desc_phys = cppi41_pop_desc(cdd, c->q_num);
		WARN_ON(!desc_phys);
	}

	c->td_queued = 0;
	c->td_seen = 0;
	c->td_desc_seen = 0;
	cppi_writel(0, c->gcr_reg);
	return 0;
}

static int cppi41_stop_chan(struct dma_chan *chan)
{
	struct cppi41_channel *c = to_cpp41_chan(chan);
	struct cppi41_dd *cdd = c->cdd;
	u32 desc_num;
	u32 desc_phys;
	int ret;

	desc_phys = lower_32_bits(c->desc_phys);
	desc_num = (desc_phys - cdd->descs_phys) / sizeof(struct cppi41_desc);
	if (!cdd->chan_busy[desc_num])
		return 0;

	ret = cppi41_tear_down_chan(c);
	if (ret)
		return ret;

	WARN_ON(!cdd->chan_busy[desc_num]);
	cdd->chan_busy[desc_num] = NULL;

	return 0;
}

static int cppi41_dma_control(struct dma_chan *chan, enum dma_ctrl_cmd cmd,
	unsigned long arg)
{
	struct cppi41_channel *c = to_cpp41_chan(chan);
	int ret;

	switch (cmd) {
	case DMA_SLAVE_CONFIG:
		ret = cpp41_cfg_chan(c, (struct dma_slave_config *) arg);
		break;

	case DMA_TERMINATE_ALL:
		ret = cppi41_stop_chan(chan);
		break;

	default:
		ret = -ENXIO;
		break;
	}
	return ret;
}

static void cleanup_chans(struct cppi41_dd *cdd)
{
	while (!list_empty(&cdd->ddev.channels)) {
		struct cppi41_channel *cchan;

		cchan = list_first_entry(&cdd->ddev.channels,
				struct cppi41_channel, chan.device_node);
		list_del(&cchan->chan.device_node);
		kfree(cchan);
	}
}

static int cppi41_add_chans(struct device *dev, struct cppi41_dd *cdd)
{
	struct cppi41_channel *cchan;
	int i;
	int ret;
	u32 n_chans;

	ret = of_property_read_u32(dev->of_node, "#dma-channels",
			&n_chans);
	if (ret)
		return ret;
	/*
	 * The channels can only be used as TX or as RX. So we add twice
	 * that much dma channels because USB can only do RX or TX.
	 */
	n_chans *= 2;

	for (i = 0; i < n_chans; i++) {
		cchan = kzalloc(sizeof(*cchan), GFP_KERNEL);
		if (!cchan)
			goto err;

		cchan->cdd = cdd;
		if (i & 1) {
			cchan->gcr_reg = cdd->ctrl_mem + DMA_TXGCR(i >> 1);
			cchan->is_tx = 1;
		} else {
			cchan->gcr_reg = cdd->ctrl_mem + DMA_RXGCR(i >> 1);
			cchan->is_tx = 0;
		}
		cchan->port_num = i >> 1;
		cchan->desc = &cdd->cd[i];
		cchan->desc_phys = cdd->descs_phys;
		cchan->desc_phys += i * sizeof(struct cppi41_desc);
		cchan->chan.device = &cdd->ddev;
		list_add_tail(&cchan->chan.device_node, &cdd->ddev.channels);
	}
	cdd->first_td_desc = n_chans;

	return 0;
err:
	cleanup_chans(cdd);
	return -ENOMEM;
}

static void purge_descs(struct device *dev, struct cppi41_dd *cdd)
{
	unsigned int mem_decs;
	int i;

	mem_decs = ALLOC_DECS_NUM * sizeof(struct cppi41_desc);

	for (i = 0; i < DESCS_AREAS; i++) {

		cppi_writel(0, cdd->qmgr_mem + QMGR_MEMBASE(i));
		cppi_writel(0, cdd->qmgr_mem + QMGR_MEMCTRL(i));

		dma_free_coherent(dev, mem_decs, cdd->cd,
				cdd->descs_phys);
	}
}

static void disable_sched(struct cppi41_dd *cdd)
{
	cppi_writel(0, cdd->sched_mem + DMA_SCHED_CTRL);
}

static void deinit_cppi41(struct device *dev, struct cppi41_dd *cdd)
{
	disable_sched(cdd);

	purge_descs(dev, cdd);

	cppi_writel(0, cdd->qmgr_mem + QMGR_LRAM0_BASE);
	cppi_writel(0, cdd->qmgr_mem + QMGR_LRAM0_BASE);
	dma_free_coherent(dev, QMGR_SCRATCH_SIZE, cdd->qmgr_scratch,
			cdd->scratch_phys);
}

static int init_descs(struct device *dev, struct cppi41_dd *cdd)
{
	unsigned int desc_size;
	unsigned int mem_decs;
	int i;
	u32 reg;
	u32 idx;

	BUILD_BUG_ON(sizeof(struct cppi41_desc) &
			(sizeof(struct cppi41_desc) - 1));
	BUILD_BUG_ON(sizeof(struct cppi41_desc) < 32);
	BUILD_BUG_ON(ALLOC_DECS_NUM < 32);

	desc_size = sizeof(struct cppi41_desc);
	mem_decs = ALLOC_DECS_NUM * desc_size;

	idx = 0;
	for (i = 0; i < DESCS_AREAS; i++) {

		reg = idx << QMGR_MEMCTRL_IDX_SH;
		reg |= (ilog2(desc_size) - 5) << QMGR_MEMCTRL_DESC_SH;
		reg |= ilog2(ALLOC_DECS_NUM) - 5;

		BUILD_BUG_ON(DESCS_AREAS != 1);
		cdd->cd = dma_alloc_coherent(dev, mem_decs,
				&cdd->descs_phys, GFP_KERNEL);
		if (!cdd->cd)
			return -ENOMEM;

		cppi_writel(cdd->descs_phys, cdd->qmgr_mem + QMGR_MEMBASE(i));
		cppi_writel(reg, cdd->qmgr_mem + QMGR_MEMCTRL(i));

		idx += ALLOC_DECS_NUM;
	}
	return 0;
}

static void init_sched(struct cppi41_dd *cdd)
{
	unsigned ch;
	unsigned word;
	u32 reg;

	word = 0;
	cppi_writel(0, cdd->sched_mem + DMA_SCHED_CTRL);
	for (ch = 0; ch < 15 * 2; ch += 2) {

		reg = SCHED_ENTRY0_CHAN(ch);
		reg |= SCHED_ENTRY1_CHAN(ch) | SCHED_ENTRY1_IS_RX;

		reg |= SCHED_ENTRY2_CHAN(ch + 1);
		reg |= SCHED_ENTRY3_CHAN(ch + 1) | SCHED_ENTRY3_IS_RX;
		cppi_writel(reg, cdd->sched_mem + DMA_SCHED_WORD(word));
		word++;
	}
	reg = 15 * 2 * 2 - 1;
	reg |= DMA_SCHED_CTRL_EN;
	cppi_writel(reg, cdd->sched_mem + DMA_SCHED_CTRL);
}

static int init_cppi41(struct device *dev, struct cppi41_dd *cdd)
{
	int ret;

	BUILD_BUG_ON(QMGR_SCRATCH_SIZE > ((1 << 14) - 1));
	cdd->qmgr_scratch = dma_alloc_coherent(dev, QMGR_SCRATCH_SIZE,
			&cdd->scratch_phys, GFP_KERNEL);
	if (!cdd->qmgr_scratch)
		return -ENOMEM;

	cppi_writel(cdd->scratch_phys, cdd->qmgr_mem + QMGR_LRAM0_BASE);
	cppi_writel(QMGR_SCRATCH_SIZE, cdd->qmgr_mem + QMGR_LRAM_SIZE);
	cppi_writel(0, cdd->qmgr_mem + QMGR_LRAM1_BASE);

	ret = init_descs(dev, cdd);
	if (ret)
		goto err_td;

	cppi_writel(cdd->td_queue.submit, cdd->ctrl_mem + DMA_TDFDQ);
	init_sched(cdd);
	return 0;
err_td:
	deinit_cppi41(dev, cdd);
	return ret;
}

static struct platform_driver cpp41_dma_driver;
/*
 * The param format is:
 * X Y
 * X: Port
 * Y: 0 = RX else TX
 */
#define INFO_PORT	0
#define INFO_IS_TX	1

static bool cpp41_dma_filter_fn(struct dma_chan *chan, void *param)
{
	struct cppi41_channel *cchan;
	struct cppi41_dd *cdd;
	const struct chan_queues *queues;
	u32 *num = param;

	if (chan->device->dev->driver != &cpp41_dma_driver.driver)
		return false;

	cchan = to_cpp41_chan(chan);

	if (cchan->port_num != num[INFO_PORT])
		return false;

	if (cchan->is_tx && !num[INFO_IS_TX])
		return false;
	cdd = cchan->cdd;
	if (cchan->is_tx)
		queues = cdd->queues_tx;
	else
		queues = cdd->queues_rx;

	BUILD_BUG_ON(ARRAY_SIZE(usb_queues_rx) != ARRAY_SIZE(usb_queues_tx));
	if (WARN_ON(cchan->port_num > ARRAY_SIZE(usb_queues_rx)))
		return false;

	cchan->q_num = queues[cchan->port_num].submit;
	cchan->q_comp_num = queues[cchan->port_num].complete;
	return true;
}

static struct of_dma_filter_info cpp41_dma_info = {
	.filter_fn = cpp41_dma_filter_fn,
};

static struct dma_chan *cppi41_dma_xlate(struct of_phandle_args *dma_spec,
		struct of_dma *ofdma)
{
	int count = dma_spec->args_count;
	struct of_dma_filter_info *info = ofdma->of_dma_data;

	if (!info || !info->filter_fn)
		return NULL;

	if (count != 2)
		return NULL;

	return dma_request_channel(info->dma_cap, info->filter_fn,
			&dma_spec->args[0]);
}

static const struct cppi_glue_infos usb_infos = {
	.isr = cppi41_irq,
	.queues_rx = usb_queues_rx,
	.queues_tx = usb_queues_tx,
	.td_queue = { .submit = 31, .complete = 0 },
};

static const struct of_device_id cppi41_dma_ids[] = {
	{ .compatible = "ti,am3359-cppi41", .data = &usb_infos},
	{},
};
MODULE_DEVICE_TABLE(of, cppi41_dma_ids);

static const struct cppi_glue_infos *get_glue_info(struct device *dev)
{
	const struct of_device_id *of_id;

	of_id = of_match_node(cppi41_dma_ids, dev->of_node);
	if (!of_id)
		return NULL;
	return of_id->data;
}

static int cppi41_dma_probe(struct platform_device *pdev)
{
	struct cppi41_dd *cdd;
	struct device *dev = &pdev->dev;
	const struct cppi_glue_infos *glue_info;
	int irq;
	int ret;

	glue_info = get_glue_info(dev);
	if (!glue_info)
		return -EINVAL;

	cdd = kzalloc(sizeof(*cdd), GFP_KERNEL);
	if (!cdd)
		return -ENOMEM;

	dma_cap_set(DMA_SLAVE, cdd->ddev.cap_mask);
	cdd->ddev.device_alloc_chan_resources = cppi41_dma_alloc_chan_resources;
	cdd->ddev.device_free_chan_resources = cppi41_dma_free_chan_resources;
	cdd->ddev.device_tx_status = cppi41_dma_tx_status;
	cdd->ddev.device_issue_pending = cppi41_dma_issue_pending;
	cdd->ddev.device_prep_slave_sg = cppi41_dma_prep_slave_sg;
	cdd->ddev.device_control = cppi41_dma_control;
	cdd->ddev.dev = dev;
	INIT_LIST_HEAD(&cdd->ddev.channels);
	cpp41_dma_info.dma_cap = cdd->ddev.cap_mask;

	cdd->usbss_mem = of_iomap(dev->of_node, 0);
	cdd->ctrl_mem = of_iomap(dev->of_node, 1);
	cdd->sched_mem = of_iomap(dev->of_node, 2);
	cdd->qmgr_mem = of_iomap(dev->of_node, 3);

	if (!cdd->usbss_mem || !cdd->ctrl_mem || !cdd->sched_mem ||
			!cdd->qmgr_mem) {
		ret = -ENXIO;
		goto err_remap;
	}

	pm_runtime_enable(dev);
	ret = pm_runtime_get_sync(dev);
	if (ret < 0)
		goto err_get_sync;

	cdd->queues_rx = glue_info->queues_rx;
	cdd->queues_tx = glue_info->queues_tx;
	cdd->td_queue = glue_info->td_queue;

	ret = init_cppi41(dev, cdd);
	if (ret)
		goto err_init_cppi;

	ret = cppi41_add_chans(dev, cdd);
	if (ret)
		goto err_chans;

	irq = irq_of_parse_and_map(dev->of_node, 0);
	if (!irq) {
		ret = -EINVAL;
		goto err_irq;
	}

	cppi_writel(USBSS_IRQ_PD_COMP, cdd->usbss_mem + USBSS_IRQ_ENABLER);

	ret = request_irq(irq, glue_info->isr, IRQF_SHARED,
			dev_name(dev), cdd);
	if (ret)
		goto err_irq;
	cdd->irq = irq;

	ret = dma_async_device_register(&cdd->ddev);
	if (ret)
		goto err_dma_reg;

	ret = of_dma_controller_register(dev->of_node,
			cppi41_dma_xlate, &cpp41_dma_info);
	if (ret)
		goto err_of;

	platform_set_drvdata(pdev, cdd);
	return 0;
err_of:
	dma_async_device_unregister(&cdd->ddev);
err_dma_reg:
	free_irq(irq, cdd);
err_irq:
	cppi_writel(0, cdd->usbss_mem + USBSS_IRQ_CLEARR);
	cleanup_chans(cdd);
err_chans:
	deinit_cppi41(dev, cdd);
err_init_cppi:
	pm_runtime_put(dev);
err_get_sync:
	pm_runtime_disable(dev);
	iounmap(cdd->usbss_mem);
	iounmap(cdd->ctrl_mem);
	iounmap(cdd->sched_mem);
	iounmap(cdd->qmgr_mem);
err_remap:
	kfree(cdd);
	return ret;
}

static int cppi41_dma_remove(struct platform_device *pdev)
{
	struct cppi41_dd *cdd = platform_get_drvdata(pdev);

	of_dma_controller_free(pdev->dev.of_node);
	dma_async_device_unregister(&cdd->ddev);

	cppi_writel(0, cdd->usbss_mem + USBSS_IRQ_CLEARR);
	free_irq(cdd->irq, cdd);
	cleanup_chans(cdd);
	deinit_cppi41(&pdev->dev, cdd);
	iounmap(cdd->usbss_mem);
	iounmap(cdd->ctrl_mem);
	iounmap(cdd->sched_mem);
	iounmap(cdd->qmgr_mem);
	pm_runtime_put(&pdev->dev);
	pm_runtime_disable(&pdev->dev);
	kfree(cdd);
	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int cppi41_suspend(struct device *dev)
{
	struct cppi41_dd *cdd = dev_get_drvdata(dev);

	cdd->dma_tdfdq = cppi_readl(cdd->ctrl_mem + DMA_TDFDQ);
	cppi_writel(0, cdd->usbss_mem + USBSS_IRQ_CLEARR);
	disable_sched(cdd);

	return 0;
}

static int cppi41_resume(struct device *dev)
{
	struct cppi41_dd *cdd = dev_get_drvdata(dev);
	struct cppi41_channel *c;
	int i;

	for (i = 0; i < DESCS_AREAS; i++)
		cppi_writel(cdd->descs_phys, cdd->qmgr_mem + QMGR_MEMBASE(i));

	list_for_each_entry(c, &cdd->ddev.channels, chan.device_node)
		if (!c->is_tx)
			cppi_writel(c->q_num, c->gcr_reg + RXHPCRA0);

	init_sched(cdd);

	cppi_writel(cdd->dma_tdfdq, cdd->ctrl_mem + DMA_TDFDQ);
	cppi_writel(cdd->scratch_phys, cdd->qmgr_mem + QMGR_LRAM0_BASE);
	cppi_writel(QMGR_SCRATCH_SIZE, cdd->qmgr_mem + QMGR_LRAM_SIZE);
	cppi_writel(0, cdd->qmgr_mem + QMGR_LRAM1_BASE);

	cppi_writel(USBSS_IRQ_PD_COMP, cdd->usbss_mem + USBSS_IRQ_ENABLER);

	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(cppi41_pm_ops, cppi41_suspend, cppi41_resume);

static struct platform_driver cpp41_dma_driver = {
	.probe  = cppi41_dma_probe,
	.remove = cppi41_dma_remove,
	.driver = {
		.name = "cppi41-dma-engine",
		.owner = THIS_MODULE,
		.pm = &cppi41_pm_ops,
		.of_match_table = of_match_ptr(cppi41_dma_ids),
	},
};

module_platform_driver(cpp41_dma_driver);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Sebastian Andrzej Siewior <bigeasy@linutronix.de>");

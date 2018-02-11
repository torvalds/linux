/*
 * Texas Instruments CPDMA Driver
 *
 * Copyright (C) 2010 Texas Instruments
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/dma-mapping.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/genalloc.h>
#include "davinci_cpdma.h"

/* DMA Registers */
#define CPDMA_TXIDVER		0x00
#define CPDMA_TXCONTROL		0x04
#define CPDMA_TXTEARDOWN	0x08
#define CPDMA_RXIDVER		0x10
#define CPDMA_RXCONTROL		0x14
#define CPDMA_SOFTRESET		0x1c
#define CPDMA_RXTEARDOWN	0x18
#define CPDMA_TX_PRI0_RATE	0x30
#define CPDMA_TXINTSTATRAW	0x80
#define CPDMA_TXINTSTATMASKED	0x84
#define CPDMA_TXINTMASKSET	0x88
#define CPDMA_TXINTMASKCLEAR	0x8c
#define CPDMA_MACINVECTOR	0x90
#define CPDMA_MACEOIVECTOR	0x94
#define CPDMA_RXINTSTATRAW	0xa0
#define CPDMA_RXINTSTATMASKED	0xa4
#define CPDMA_RXINTMASKSET	0xa8
#define CPDMA_RXINTMASKCLEAR	0xac
#define CPDMA_DMAINTSTATRAW	0xb0
#define CPDMA_DMAINTSTATMASKED	0xb4
#define CPDMA_DMAINTMASKSET	0xb8
#define CPDMA_DMAINTMASKCLEAR	0xbc
#define CPDMA_DMAINT_HOSTERR	BIT(1)

/* the following exist only if has_ext_regs is set */
#define CPDMA_DMACONTROL	0x20
#define CPDMA_DMASTATUS		0x24
#define CPDMA_RXBUFFOFS		0x28
#define CPDMA_EM_CONTROL	0x2c

/* Descriptor mode bits */
#define CPDMA_DESC_SOP		BIT(31)
#define CPDMA_DESC_EOP		BIT(30)
#define CPDMA_DESC_OWNER	BIT(29)
#define CPDMA_DESC_EOQ		BIT(28)
#define CPDMA_DESC_TD_COMPLETE	BIT(27)
#define CPDMA_DESC_PASS_CRC	BIT(26)
#define CPDMA_DESC_TO_PORT_EN	BIT(20)
#define CPDMA_TO_PORT_SHIFT	16
#define CPDMA_DESC_PORT_MASK	(BIT(18) | BIT(17) | BIT(16))
#define CPDMA_DESC_CRC_LEN	4

#define CPDMA_TEARDOWN_VALUE	0xfffffffc

#define CPDMA_MAX_RLIM_CNT	16384

struct cpdma_desc {
	/* hardware fields */
	u32			hw_next;
	u32			hw_buffer;
	u32			hw_len;
	u32			hw_mode;
	/* software fields */
	void			*sw_token;
	u32			sw_buffer;
	u32			sw_len;
};

struct cpdma_desc_pool {
	phys_addr_t		phys;
	dma_addr_t		hw_addr;
	void __iomem		*iomap;		/* ioremap map */
	void			*cpumap;	/* dma_alloc map */
	int			desc_size, mem_size;
	int			num_desc;
	struct device		*dev;
	struct gen_pool		*gen_pool;
};

enum cpdma_state {
	CPDMA_STATE_IDLE,
	CPDMA_STATE_ACTIVE,
	CPDMA_STATE_TEARDOWN,
};

struct cpdma_ctlr {
	enum cpdma_state	state;
	struct cpdma_params	params;
	struct device		*dev;
	struct cpdma_desc_pool	*pool;
	spinlock_t		lock;
	struct cpdma_chan	*channels[2 * CPDMA_MAX_CHANNELS];
	int chan_num;
	int			num_rx_desc; /* RX descriptors number */
	int			num_tx_desc; /* TX descriptors number */
};

struct cpdma_chan {
	struct cpdma_desc __iomem	*head, *tail;
	void __iomem			*hdp, *cp, *rxfree;
	enum cpdma_state		state;
	struct cpdma_ctlr		*ctlr;
	int				chan_num;
	spinlock_t			lock;
	int				count;
	u32				desc_num;
	u32				mask;
	cpdma_handler_fn		handler;
	enum dma_data_direction		dir;
	struct cpdma_chan_stats		stats;
	/* offsets into dmaregs */
	int	int_set, int_clear, td;
	int				weight;
	u32				rate_factor;
	u32				rate;
};

struct cpdma_control_info {
	u32		reg;
	u32		shift, mask;
	int		access;
#define ACCESS_RO	BIT(0)
#define ACCESS_WO	BIT(1)
#define ACCESS_RW	(ACCESS_RO | ACCESS_WO)
};

static struct cpdma_control_info controls[] = {
	[CPDMA_TX_RLIM]		  = {CPDMA_DMACONTROL,	8,  0xffff, ACCESS_RW},
	[CPDMA_CMD_IDLE]	  = {CPDMA_DMACONTROL,	3,  1,      ACCESS_WO},
	[CPDMA_COPY_ERROR_FRAMES] = {CPDMA_DMACONTROL,	4,  1,      ACCESS_RW},
	[CPDMA_RX_OFF_LEN_UPDATE] = {CPDMA_DMACONTROL,	2,  1,      ACCESS_RW},
	[CPDMA_RX_OWNERSHIP_FLIP] = {CPDMA_DMACONTROL,	1,  1,      ACCESS_RW},
	[CPDMA_TX_PRIO_FIXED]	  = {CPDMA_DMACONTROL,	0,  1,      ACCESS_RW},
	[CPDMA_STAT_IDLE]	  = {CPDMA_DMASTATUS,	31, 1,      ACCESS_RO},
	[CPDMA_STAT_TX_ERR_CODE]  = {CPDMA_DMASTATUS,	20, 0xf,    ACCESS_RW},
	[CPDMA_STAT_TX_ERR_CHAN]  = {CPDMA_DMASTATUS,	16, 0x7,    ACCESS_RW},
	[CPDMA_STAT_RX_ERR_CODE]  = {CPDMA_DMASTATUS,	12, 0xf,    ACCESS_RW},
	[CPDMA_STAT_RX_ERR_CHAN]  = {CPDMA_DMASTATUS,	8,  0x7,    ACCESS_RW},
	[CPDMA_RX_BUFFER_OFFSET]  = {CPDMA_RXBUFFOFS,	0,  0xffff, ACCESS_RW},
};

#define tx_chan_num(chan)	(chan)
#define rx_chan_num(chan)	((chan) + CPDMA_MAX_CHANNELS)
#define is_rx_chan(chan)	((chan)->chan_num >= CPDMA_MAX_CHANNELS)
#define is_tx_chan(chan)	(!is_rx_chan(chan))
#define __chan_linear(chan_num)	((chan_num) & (CPDMA_MAX_CHANNELS - 1))
#define chan_linear(chan)	__chan_linear((chan)->chan_num)

/* The following make access to common cpdma_ctlr params more readable */
#define dmaregs		params.dmaregs
#define num_chan	params.num_chan

/* various accessors */
#define dma_reg_read(ctlr, ofs)		readl((ctlr)->dmaregs + (ofs))
#define chan_read(chan, fld)		readl((chan)->fld)
#define desc_read(desc, fld)		readl(&(desc)->fld)
#define dma_reg_write(ctlr, ofs, v)	writel(v, (ctlr)->dmaregs + (ofs))
#define chan_write(chan, fld, v)	writel(v, (chan)->fld)
#define desc_write(desc, fld, v)	writel((u32)(v), &(desc)->fld)

#define cpdma_desc_to_port(chan, mode, directed)			\
	do {								\
		if (!is_rx_chan(chan) && ((directed == 1) ||		\
					  (directed == 2)))		\
			mode |= (CPDMA_DESC_TO_PORT_EN |		\
				 (directed << CPDMA_TO_PORT_SHIFT));	\
	} while (0)

static void cpdma_desc_pool_destroy(struct cpdma_ctlr *ctlr)
{
	struct cpdma_desc_pool *pool = ctlr->pool;

	if (!pool)
		return;

	WARN(gen_pool_size(pool->gen_pool) != gen_pool_avail(pool->gen_pool),
	     "cpdma_desc_pool size %d != avail %d",
	     gen_pool_size(pool->gen_pool),
	     gen_pool_avail(pool->gen_pool));
	if (pool->cpumap)
		dma_free_coherent(ctlr->dev, pool->mem_size, pool->cpumap,
				  pool->phys);
}

/*
 * Utility constructs for a cpdma descriptor pool.  Some devices (e.g. davinci
 * emac) have dedicated on-chip memory for these descriptors.  Some other
 * devices (e.g. cpsw switches) use plain old memory.  Descriptor pools
 * abstract out these details
 */
int cpdma_desc_pool_create(struct cpdma_ctlr *ctlr)
{
	struct cpdma_params *cpdma_params = &ctlr->params;
	struct cpdma_desc_pool *pool;
	int ret = -ENOMEM;

	pool = devm_kzalloc(ctlr->dev, sizeof(*pool), GFP_KERNEL);
	if (!pool)
		goto gen_pool_create_fail;
	ctlr->pool = pool;

	pool->mem_size	= cpdma_params->desc_mem_size;
	pool->desc_size	= ALIGN(sizeof(struct cpdma_desc),
				cpdma_params->desc_align);
	pool->num_desc	= pool->mem_size / pool->desc_size;

	if (cpdma_params->descs_pool_size) {
		/* recalculate memory size required cpdma descriptor pool
		 * basing on number of descriptors specified by user and
		 * if memory size > CPPI internal RAM size (desc_mem_size)
		 * then switch to use DDR
		 */
		pool->num_desc = cpdma_params->descs_pool_size;
		pool->mem_size = pool->desc_size * pool->num_desc;
		if (pool->mem_size > cpdma_params->desc_mem_size)
			cpdma_params->desc_mem_phys = 0;
	}

	pool->gen_pool = devm_gen_pool_create(ctlr->dev, ilog2(pool->desc_size),
					      -1, "cpdma");
	if (IS_ERR(pool->gen_pool)) {
		ret = PTR_ERR(pool->gen_pool);
		dev_err(ctlr->dev, "pool create failed %d\n", ret);
		goto gen_pool_create_fail;
	}

	if (cpdma_params->desc_mem_phys) {
		pool->phys  = cpdma_params->desc_mem_phys;
		pool->iomap = devm_ioremap(ctlr->dev, pool->phys,
					   pool->mem_size);
		pool->hw_addr = cpdma_params->desc_hw_addr;
	} else {
		pool->cpumap = dma_alloc_coherent(ctlr->dev,  pool->mem_size,
						  &pool->hw_addr, GFP_KERNEL);
		pool->iomap = (void __iomem __force *)pool->cpumap;
		pool->phys = pool->hw_addr; /* assumes no IOMMU, don't use this value */
	}

	if (!pool->iomap)
		goto gen_pool_create_fail;

	ret = gen_pool_add_virt(pool->gen_pool, (unsigned long)pool->iomap,
				pool->phys, pool->mem_size, -1);
	if (ret < 0) {
		dev_err(ctlr->dev, "pool add failed %d\n", ret);
		goto gen_pool_add_virt_fail;
	}

	return 0;

gen_pool_add_virt_fail:
	cpdma_desc_pool_destroy(ctlr);
gen_pool_create_fail:
	ctlr->pool = NULL;
	return ret;
}

static inline dma_addr_t desc_phys(struct cpdma_desc_pool *pool,
		  struct cpdma_desc __iomem *desc)
{
	if (!desc)
		return 0;
	return pool->hw_addr + (__force long)desc - (__force long)pool->iomap;
}

static inline struct cpdma_desc __iomem *
desc_from_phys(struct cpdma_desc_pool *pool, dma_addr_t dma)
{
	return dma ? pool->iomap + dma - pool->hw_addr : NULL;
}

static struct cpdma_desc __iomem *
cpdma_desc_alloc(struct cpdma_desc_pool *pool)
{
	return (struct cpdma_desc __iomem *)
		gen_pool_alloc(pool->gen_pool, pool->desc_size);
}

static void cpdma_desc_free(struct cpdma_desc_pool *pool,
			    struct cpdma_desc __iomem *desc, int num_desc)
{
	gen_pool_free(pool->gen_pool, (unsigned long)desc, pool->desc_size);
}

static int _cpdma_control_set(struct cpdma_ctlr *ctlr, int control, int value)
{
	struct cpdma_control_info *info = &controls[control];
	u32 val;

	if (!ctlr->params.has_ext_regs)
		return -ENOTSUPP;

	if (ctlr->state != CPDMA_STATE_ACTIVE)
		return -EINVAL;

	if (control < 0 || control >= ARRAY_SIZE(controls))
		return -ENOENT;

	if ((info->access & ACCESS_WO) != ACCESS_WO)
		return -EPERM;

	val  = dma_reg_read(ctlr, info->reg);
	val &= ~(info->mask << info->shift);
	val |= (value & info->mask) << info->shift;
	dma_reg_write(ctlr, info->reg, val);

	return 0;
}

static int _cpdma_control_get(struct cpdma_ctlr *ctlr, int control)
{
	struct cpdma_control_info *info = &controls[control];
	int ret;

	if (!ctlr->params.has_ext_regs)
		return -ENOTSUPP;

	if (ctlr->state != CPDMA_STATE_ACTIVE)
		return -EINVAL;

	if (control < 0 || control >= ARRAY_SIZE(controls))
		return -ENOENT;

	if ((info->access & ACCESS_RO) != ACCESS_RO)
		return -EPERM;

	ret = (dma_reg_read(ctlr, info->reg) >> info->shift) & info->mask;
	return ret;
}

/* cpdma_chan_set_chan_shaper - set shaper for a channel
 * Has to be called under ctlr lock
 */
static int cpdma_chan_set_chan_shaper(struct cpdma_chan *chan)
{
	struct cpdma_ctlr *ctlr = chan->ctlr;
	u32 rate_reg;
	u32 rmask;
	int ret;

	if (!chan->rate)
		return 0;

	rate_reg = CPDMA_TX_PRI0_RATE + 4 * chan->chan_num;
	dma_reg_write(ctlr, rate_reg, chan->rate_factor);

	rmask = _cpdma_control_get(ctlr, CPDMA_TX_RLIM);
	rmask |= chan->mask;

	ret = _cpdma_control_set(ctlr, CPDMA_TX_RLIM, rmask);
	return ret;
}

static int cpdma_chan_on(struct cpdma_chan *chan)
{
	struct cpdma_ctlr *ctlr = chan->ctlr;
	struct cpdma_desc_pool	*pool = ctlr->pool;
	unsigned long flags;

	spin_lock_irqsave(&chan->lock, flags);
	if (chan->state != CPDMA_STATE_IDLE) {
		spin_unlock_irqrestore(&chan->lock, flags);
		return -EBUSY;
	}
	if (ctlr->state != CPDMA_STATE_ACTIVE) {
		spin_unlock_irqrestore(&chan->lock, flags);
		return -EINVAL;
	}
	dma_reg_write(ctlr, chan->int_set, chan->mask);
	chan->state = CPDMA_STATE_ACTIVE;
	if (chan->head) {
		chan_write(chan, hdp, desc_phys(pool, chan->head));
		if (chan->rxfree)
			chan_write(chan, rxfree, chan->count);
	}

	spin_unlock_irqrestore(&chan->lock, flags);
	return 0;
}

/* cpdma_chan_fit_rate - set rate for a channel and check if it's possible.
 * rmask - mask of rate limited channels
 * Returns min rate in Kb/s
 */
static int cpdma_chan_fit_rate(struct cpdma_chan *ch, u32 rate,
			       u32 *rmask, int *prio_mode)
{
	struct cpdma_ctlr *ctlr = ch->ctlr;
	struct cpdma_chan *chan;
	u32 old_rate = ch->rate;
	u32 new_rmask = 0;
	int rlim = 1;
	int i;

	*prio_mode = 0;
	for (i = tx_chan_num(0); i < tx_chan_num(CPDMA_MAX_CHANNELS); i++) {
		chan = ctlr->channels[i];
		if (!chan) {
			rlim = 0;
			continue;
		}

		if (chan == ch)
			chan->rate = rate;

		if (chan->rate) {
			if (rlim) {
				new_rmask |= chan->mask;
			} else {
				ch->rate = old_rate;
				dev_err(ctlr->dev, "Prev channel of %dch is not rate limited\n",
					chan->chan_num);
				return -EINVAL;
			}
		} else {
			*prio_mode = 1;
			rlim = 0;
		}
	}

	*rmask = new_rmask;
	return 0;
}

static u32 cpdma_chan_set_factors(struct cpdma_ctlr *ctlr,
				  struct cpdma_chan *ch)
{
	u32 delta = UINT_MAX, prev_delta = UINT_MAX, best_delta = UINT_MAX;
	u32 best_send_cnt = 0, best_idle_cnt = 0;
	u32 new_rate, best_rate = 0, rate_reg;
	u64 send_cnt, idle_cnt;
	u32 min_send_cnt, freq;
	u64 divident, divisor;

	if (!ch->rate) {
		ch->rate_factor = 0;
		goto set_factor;
	}

	freq = ctlr->params.bus_freq_mhz * 1000 * 32;
	if (!freq) {
		dev_err(ctlr->dev, "The bus frequency is not set\n");
		return -EINVAL;
	}

	min_send_cnt = freq - ch->rate;
	send_cnt = DIV_ROUND_UP(min_send_cnt, ch->rate);
	while (send_cnt <= CPDMA_MAX_RLIM_CNT) {
		divident = ch->rate * send_cnt;
		divisor = min_send_cnt;
		idle_cnt = DIV_ROUND_CLOSEST_ULL(divident, divisor);

		divident = freq * idle_cnt;
		divisor = idle_cnt + send_cnt;
		new_rate = DIV_ROUND_CLOSEST_ULL(divident, divisor);

		delta = new_rate >= ch->rate ? new_rate - ch->rate : delta;
		if (delta < best_delta) {
			best_delta = delta;
			best_send_cnt = send_cnt;
			best_idle_cnt = idle_cnt;
			best_rate = new_rate;

			if (!delta)
				break;
		}

		if (prev_delta >= delta) {
			prev_delta = delta;
			send_cnt++;
			continue;
		}

		idle_cnt++;
		divident = freq * idle_cnt;
		send_cnt = DIV_ROUND_CLOSEST_ULL(divident, ch->rate);
		send_cnt -= idle_cnt;
		prev_delta = UINT_MAX;
	}

	ch->rate = best_rate;
	ch->rate_factor = best_send_cnt | (best_idle_cnt << 16);

set_factor:
	rate_reg = CPDMA_TX_PRI0_RATE + 4 * ch->chan_num;
	dma_reg_write(ctlr, rate_reg, ch->rate_factor);
	return 0;
}

struct cpdma_ctlr *cpdma_ctlr_create(struct cpdma_params *params)
{
	struct cpdma_ctlr *ctlr;

	ctlr = devm_kzalloc(params->dev, sizeof(*ctlr), GFP_KERNEL);
	if (!ctlr)
		return NULL;

	ctlr->state = CPDMA_STATE_IDLE;
	ctlr->params = *params;
	ctlr->dev = params->dev;
	ctlr->chan_num = 0;
	spin_lock_init(&ctlr->lock);

	if (cpdma_desc_pool_create(ctlr))
		return NULL;
	/* split pool equally between RX/TX by default */
	ctlr->num_tx_desc = ctlr->pool->num_desc / 2;
	ctlr->num_rx_desc = ctlr->pool->num_desc - ctlr->num_tx_desc;

	if (WARN_ON(ctlr->num_chan > CPDMA_MAX_CHANNELS))
		ctlr->num_chan = CPDMA_MAX_CHANNELS;
	return ctlr;
}
EXPORT_SYMBOL_GPL(cpdma_ctlr_create);

int cpdma_ctlr_start(struct cpdma_ctlr *ctlr)
{
	struct cpdma_chan *chan;
	unsigned long flags;
	int i, prio_mode;

	spin_lock_irqsave(&ctlr->lock, flags);
	if (ctlr->state != CPDMA_STATE_IDLE) {
		spin_unlock_irqrestore(&ctlr->lock, flags);
		return -EBUSY;
	}

	if (ctlr->params.has_soft_reset) {
		unsigned timeout = 10 * 100;

		dma_reg_write(ctlr, CPDMA_SOFTRESET, 1);
		while (timeout) {
			if (dma_reg_read(ctlr, CPDMA_SOFTRESET) == 0)
				break;
			udelay(10);
			timeout--;
		}
		WARN_ON(!timeout);
	}

	for (i = 0; i < ctlr->num_chan; i++) {
		writel(0, ctlr->params.txhdp + 4 * i);
		writel(0, ctlr->params.rxhdp + 4 * i);
		writel(0, ctlr->params.txcp + 4 * i);
		writel(0, ctlr->params.rxcp + 4 * i);
	}

	dma_reg_write(ctlr, CPDMA_RXINTMASKCLEAR, 0xffffffff);
	dma_reg_write(ctlr, CPDMA_TXINTMASKCLEAR, 0xffffffff);

	dma_reg_write(ctlr, CPDMA_TXCONTROL, 1);
	dma_reg_write(ctlr, CPDMA_RXCONTROL, 1);

	ctlr->state = CPDMA_STATE_ACTIVE;

	prio_mode = 0;
	for (i = 0; i < ARRAY_SIZE(ctlr->channels); i++) {
		chan = ctlr->channels[i];
		if (chan) {
			cpdma_chan_set_chan_shaper(chan);
			cpdma_chan_on(chan);

			/* off prio mode if all tx channels are rate limited */
			if (is_tx_chan(chan) && !chan->rate)
				prio_mode = 1;
		}
	}

	_cpdma_control_set(ctlr, CPDMA_TX_PRIO_FIXED, prio_mode);
	_cpdma_control_set(ctlr, CPDMA_RX_BUFFER_OFFSET, 0);

	spin_unlock_irqrestore(&ctlr->lock, flags);
	return 0;
}
EXPORT_SYMBOL_GPL(cpdma_ctlr_start);

int cpdma_ctlr_stop(struct cpdma_ctlr *ctlr)
{
	unsigned long flags;
	int i;

	spin_lock_irqsave(&ctlr->lock, flags);
	if (ctlr->state != CPDMA_STATE_ACTIVE) {
		spin_unlock_irqrestore(&ctlr->lock, flags);
		return -EINVAL;
	}

	ctlr->state = CPDMA_STATE_TEARDOWN;
	spin_unlock_irqrestore(&ctlr->lock, flags);

	for (i = 0; i < ARRAY_SIZE(ctlr->channels); i++) {
		if (ctlr->channels[i])
			cpdma_chan_stop(ctlr->channels[i]);
	}

	spin_lock_irqsave(&ctlr->lock, flags);
	dma_reg_write(ctlr, CPDMA_RXINTMASKCLEAR, 0xffffffff);
	dma_reg_write(ctlr, CPDMA_TXINTMASKCLEAR, 0xffffffff);

	dma_reg_write(ctlr, CPDMA_TXCONTROL, 0);
	dma_reg_write(ctlr, CPDMA_RXCONTROL, 0);

	ctlr->state = CPDMA_STATE_IDLE;

	spin_unlock_irqrestore(&ctlr->lock, flags);
	return 0;
}
EXPORT_SYMBOL_GPL(cpdma_ctlr_stop);

int cpdma_ctlr_destroy(struct cpdma_ctlr *ctlr)
{
	int ret = 0, i;

	if (!ctlr)
		return -EINVAL;

	if (ctlr->state != CPDMA_STATE_IDLE)
		cpdma_ctlr_stop(ctlr);

	for (i = 0; i < ARRAY_SIZE(ctlr->channels); i++)
		cpdma_chan_destroy(ctlr->channels[i]);

	cpdma_desc_pool_destroy(ctlr);
	return ret;
}
EXPORT_SYMBOL_GPL(cpdma_ctlr_destroy);

int cpdma_ctlr_int_ctrl(struct cpdma_ctlr *ctlr, bool enable)
{
	unsigned long flags;
	int i;

	spin_lock_irqsave(&ctlr->lock, flags);
	if (ctlr->state != CPDMA_STATE_ACTIVE) {
		spin_unlock_irqrestore(&ctlr->lock, flags);
		return -EINVAL;
	}

	for (i = 0; i < ARRAY_SIZE(ctlr->channels); i++) {
		if (ctlr->channels[i])
			cpdma_chan_int_ctrl(ctlr->channels[i], enable);
	}

	spin_unlock_irqrestore(&ctlr->lock, flags);
	return 0;
}
EXPORT_SYMBOL_GPL(cpdma_ctlr_int_ctrl);

void cpdma_ctlr_eoi(struct cpdma_ctlr *ctlr, u32 value)
{
	dma_reg_write(ctlr, CPDMA_MACEOIVECTOR, value);
}
EXPORT_SYMBOL_GPL(cpdma_ctlr_eoi);

u32 cpdma_ctrl_rxchs_state(struct cpdma_ctlr *ctlr)
{
	return dma_reg_read(ctlr, CPDMA_RXINTSTATMASKED);
}
EXPORT_SYMBOL_GPL(cpdma_ctrl_rxchs_state);

u32 cpdma_ctrl_txchs_state(struct cpdma_ctlr *ctlr)
{
	return dma_reg_read(ctlr, CPDMA_TXINTSTATMASKED);
}
EXPORT_SYMBOL_GPL(cpdma_ctrl_txchs_state);

static void cpdma_chan_set_descs(struct cpdma_ctlr *ctlr,
				 int rx, int desc_num,
				 int per_ch_desc)
{
	struct cpdma_chan *chan, *most_chan = NULL;
	int desc_cnt = desc_num;
	int most_dnum = 0;
	int min, max, i;

	if (!desc_num)
		return;

	if (rx) {
		min = rx_chan_num(0);
		max = rx_chan_num(CPDMA_MAX_CHANNELS);
	} else {
		min = tx_chan_num(0);
		max = tx_chan_num(CPDMA_MAX_CHANNELS);
	}

	for (i = min; i < max; i++) {
		chan = ctlr->channels[i];
		if (!chan)
			continue;

		if (chan->weight)
			chan->desc_num = (chan->weight * desc_num) / 100;
		else
			chan->desc_num = per_ch_desc;

		desc_cnt -= chan->desc_num;

		if (most_dnum < chan->desc_num) {
			most_dnum = chan->desc_num;
			most_chan = chan;
		}
	}
	/* use remains */
	if (most_chan)
		most_chan->desc_num += desc_cnt;
}

/**
 * cpdma_chan_split_pool - Splits ctrl pool between all channels.
 * Has to be called under ctlr lock
 */
int cpdma_chan_split_pool(struct cpdma_ctlr *ctlr)
{
	int tx_per_ch_desc = 0, rx_per_ch_desc = 0;
	int free_rx_num = 0, free_tx_num = 0;
	int rx_weight = 0, tx_weight = 0;
	int tx_desc_num, rx_desc_num;
	struct cpdma_chan *chan;
	int i;

	if (!ctlr->chan_num)
		return 0;

	for (i = 0; i < ARRAY_SIZE(ctlr->channels); i++) {
		chan = ctlr->channels[i];
		if (!chan)
			continue;

		if (is_rx_chan(chan)) {
			if (!chan->weight)
				free_rx_num++;
			rx_weight += chan->weight;
		} else {
			if (!chan->weight)
				free_tx_num++;
			tx_weight += chan->weight;
		}
	}

	if (rx_weight > 100 || tx_weight > 100)
		return -EINVAL;

	tx_desc_num = ctlr->num_tx_desc;
	rx_desc_num = ctlr->num_rx_desc;

	if (free_tx_num) {
		tx_per_ch_desc = tx_desc_num - (tx_weight * tx_desc_num) / 100;
		tx_per_ch_desc /= free_tx_num;
	}
	if (free_rx_num) {
		rx_per_ch_desc = rx_desc_num - (rx_weight * rx_desc_num) / 100;
		rx_per_ch_desc /= free_rx_num;
	}

	cpdma_chan_set_descs(ctlr, 0, tx_desc_num, tx_per_ch_desc);
	cpdma_chan_set_descs(ctlr, 1, rx_desc_num, rx_per_ch_desc);

	return 0;
}
EXPORT_SYMBOL_GPL(cpdma_chan_split_pool);


/* cpdma_chan_set_weight - set weight of a channel in percentage.
 * Tx and Rx channels have separate weights. That is 100% for RX
 * and 100% for Tx. The weight is used to split cpdma resources
 * in correct proportion required by the channels, including number
 * of descriptors. The channel rate is not enough to know the
 * weight of a channel as the maximum rate of an interface is needed.
 * If weight = 0, then channel uses rest of descriptors leaved by
 * weighted channels.
 */
int cpdma_chan_set_weight(struct cpdma_chan *ch, int weight)
{
	struct cpdma_ctlr *ctlr = ch->ctlr;
	unsigned long flags, ch_flags;
	int ret;

	spin_lock_irqsave(&ctlr->lock, flags);
	spin_lock_irqsave(&ch->lock, ch_flags);
	if (ch->weight == weight) {
		spin_unlock_irqrestore(&ch->lock, ch_flags);
		spin_unlock_irqrestore(&ctlr->lock, flags);
		return 0;
	}
	ch->weight = weight;
	spin_unlock_irqrestore(&ch->lock, ch_flags);

	/* re-split pool using new channel weight */
	ret = cpdma_chan_split_pool(ctlr);
	spin_unlock_irqrestore(&ctlr->lock, flags);
	return ret;
}
EXPORT_SYMBOL_GPL(cpdma_chan_set_weight);

/* cpdma_chan_get_min_rate - get minimum allowed rate for channel
 * Should be called before cpdma_chan_set_rate.
 * Returns min rate in Kb/s
 */
u32 cpdma_chan_get_min_rate(struct cpdma_ctlr *ctlr)
{
	unsigned int divident, divisor;

	divident = ctlr->params.bus_freq_mhz * 32 * 1000;
	divisor = 1 + CPDMA_MAX_RLIM_CNT;

	return DIV_ROUND_UP(divident, divisor);
}
EXPORT_SYMBOL_GPL(cpdma_chan_get_min_rate);

/* cpdma_chan_set_rate - limits bandwidth for transmit channel.
 * The bandwidth * limited channels have to be in order beginning from lowest.
 * ch - transmit channel the bandwidth is configured for
 * rate - bandwidth in Kb/s, if 0 - then off shaper
 */
int cpdma_chan_set_rate(struct cpdma_chan *ch, u32 rate)
{
	unsigned long flags, ch_flags;
	struct cpdma_ctlr *ctlr;
	int ret, prio_mode;
	u32 rmask;

	if (!ch || !is_tx_chan(ch))
		return -EINVAL;

	if (ch->rate == rate)
		return rate;

	ctlr = ch->ctlr;
	spin_lock_irqsave(&ctlr->lock, flags);
	spin_lock_irqsave(&ch->lock, ch_flags);

	ret = cpdma_chan_fit_rate(ch, rate, &rmask, &prio_mode);
	if (ret)
		goto err;

	ret = cpdma_chan_set_factors(ctlr, ch);
	if (ret)
		goto err;

	spin_unlock_irqrestore(&ch->lock, ch_flags);

	/* on shapers */
	_cpdma_control_set(ctlr, CPDMA_TX_RLIM, rmask);
	_cpdma_control_set(ctlr, CPDMA_TX_PRIO_FIXED, prio_mode);
	spin_unlock_irqrestore(&ctlr->lock, flags);
	return ret;

err:
	spin_unlock_irqrestore(&ch->lock, ch_flags);
	spin_unlock_irqrestore(&ctlr->lock, flags);
	return ret;
}
EXPORT_SYMBOL_GPL(cpdma_chan_set_rate);

u32 cpdma_chan_get_rate(struct cpdma_chan *ch)
{
	unsigned long flags;
	u32 rate;

	spin_lock_irqsave(&ch->lock, flags);
	rate = ch->rate;
	spin_unlock_irqrestore(&ch->lock, flags);

	return rate;
}
EXPORT_SYMBOL_GPL(cpdma_chan_get_rate);

struct cpdma_chan *cpdma_chan_create(struct cpdma_ctlr *ctlr, int chan_num,
				     cpdma_handler_fn handler, int rx_type)
{
	int offset = chan_num * 4;
	struct cpdma_chan *chan;
	unsigned long flags;

	chan_num = rx_type ? rx_chan_num(chan_num) : tx_chan_num(chan_num);

	if (__chan_linear(chan_num) >= ctlr->num_chan)
		return NULL;

	chan = devm_kzalloc(ctlr->dev, sizeof(*chan), GFP_KERNEL);
	if (!chan)
		return ERR_PTR(-ENOMEM);

	spin_lock_irqsave(&ctlr->lock, flags);
	if (ctlr->channels[chan_num]) {
		spin_unlock_irqrestore(&ctlr->lock, flags);
		devm_kfree(ctlr->dev, chan);
		return ERR_PTR(-EBUSY);
	}

	chan->ctlr	= ctlr;
	chan->state	= CPDMA_STATE_IDLE;
	chan->chan_num	= chan_num;
	chan->handler	= handler;
	chan->rate	= 0;
	chan->weight	= 0;

	if (is_rx_chan(chan)) {
		chan->hdp	= ctlr->params.rxhdp + offset;
		chan->cp	= ctlr->params.rxcp + offset;
		chan->rxfree	= ctlr->params.rxfree + offset;
		chan->int_set	= CPDMA_RXINTMASKSET;
		chan->int_clear	= CPDMA_RXINTMASKCLEAR;
		chan->td	= CPDMA_RXTEARDOWN;
		chan->dir	= DMA_FROM_DEVICE;
	} else {
		chan->hdp	= ctlr->params.txhdp + offset;
		chan->cp	= ctlr->params.txcp + offset;
		chan->int_set	= CPDMA_TXINTMASKSET;
		chan->int_clear	= CPDMA_TXINTMASKCLEAR;
		chan->td	= CPDMA_TXTEARDOWN;
		chan->dir	= DMA_TO_DEVICE;
	}
	chan->mask = BIT(chan_linear(chan));

	spin_lock_init(&chan->lock);

	ctlr->channels[chan_num] = chan;
	ctlr->chan_num++;

	cpdma_chan_split_pool(ctlr);

	spin_unlock_irqrestore(&ctlr->lock, flags);
	return chan;
}
EXPORT_SYMBOL_GPL(cpdma_chan_create);

int cpdma_chan_get_rx_buf_num(struct cpdma_chan *chan)
{
	unsigned long flags;
	int desc_num;

	spin_lock_irqsave(&chan->lock, flags);
	desc_num = chan->desc_num;
	spin_unlock_irqrestore(&chan->lock, flags);

	return desc_num;
}
EXPORT_SYMBOL_GPL(cpdma_chan_get_rx_buf_num);

int cpdma_chan_destroy(struct cpdma_chan *chan)
{
	struct cpdma_ctlr *ctlr;
	unsigned long flags;

	if (!chan)
		return -EINVAL;
	ctlr = chan->ctlr;

	spin_lock_irqsave(&ctlr->lock, flags);
	if (chan->state != CPDMA_STATE_IDLE)
		cpdma_chan_stop(chan);
	ctlr->channels[chan->chan_num] = NULL;
	ctlr->chan_num--;
	devm_kfree(ctlr->dev, chan);
	cpdma_chan_split_pool(ctlr);

	spin_unlock_irqrestore(&ctlr->lock, flags);
	return 0;
}
EXPORT_SYMBOL_GPL(cpdma_chan_destroy);

int cpdma_chan_get_stats(struct cpdma_chan *chan,
			 struct cpdma_chan_stats *stats)
{
	unsigned long flags;
	if (!chan)
		return -EINVAL;
	spin_lock_irqsave(&chan->lock, flags);
	memcpy(stats, &chan->stats, sizeof(*stats));
	spin_unlock_irqrestore(&chan->lock, flags);
	return 0;
}
EXPORT_SYMBOL_GPL(cpdma_chan_get_stats);

static void __cpdma_chan_submit(struct cpdma_chan *chan,
				struct cpdma_desc __iomem *desc)
{
	struct cpdma_ctlr		*ctlr = chan->ctlr;
	struct cpdma_desc __iomem	*prev = chan->tail;
	struct cpdma_desc_pool		*pool = ctlr->pool;
	dma_addr_t			desc_dma;
	u32				mode;

	desc_dma = desc_phys(pool, desc);

	/* simple case - idle channel */
	if (!chan->head) {
		chan->stats.head_enqueue++;
		chan->head = desc;
		chan->tail = desc;
		if (chan->state == CPDMA_STATE_ACTIVE)
			chan_write(chan, hdp, desc_dma);
		return;
	}

	/* first chain the descriptor at the tail of the list */
	desc_write(prev, hw_next, desc_dma);
	chan->tail = desc;
	chan->stats.tail_enqueue++;

	/* next check if EOQ has been triggered already */
	mode = desc_read(prev, hw_mode);
	if (((mode & (CPDMA_DESC_EOQ | CPDMA_DESC_OWNER)) == CPDMA_DESC_EOQ) &&
	    (chan->state == CPDMA_STATE_ACTIVE)) {
		desc_write(prev, hw_mode, mode & ~CPDMA_DESC_EOQ);
		chan_write(chan, hdp, desc_dma);
		chan->stats.misqueued++;
	}
}

int cpdma_chan_submit(struct cpdma_chan *chan, void *token, void *data,
		      int len, int directed)
{
	struct cpdma_ctlr		*ctlr = chan->ctlr;
	struct cpdma_desc __iomem	*desc;
	dma_addr_t			buffer;
	unsigned long			flags;
	u32				mode;
	int				ret = 0;

	spin_lock_irqsave(&chan->lock, flags);

	if (chan->state == CPDMA_STATE_TEARDOWN) {
		ret = -EINVAL;
		goto unlock_ret;
	}

	if (chan->count >= chan->desc_num)	{
		chan->stats.desc_alloc_fail++;
		ret = -ENOMEM;
		goto unlock_ret;
	}

	desc = cpdma_desc_alloc(ctlr->pool);
	if (!desc) {
		chan->stats.desc_alloc_fail++;
		ret = -ENOMEM;
		goto unlock_ret;
	}

	if (len < ctlr->params.min_packet_size) {
		len = ctlr->params.min_packet_size;
		chan->stats.runt_transmit_buff++;
	}

	buffer = dma_map_single(ctlr->dev, data, len, chan->dir);
	ret = dma_mapping_error(ctlr->dev, buffer);
	if (ret) {
		cpdma_desc_free(ctlr->pool, desc, 1);
		ret = -EINVAL;
		goto unlock_ret;
	}

	mode = CPDMA_DESC_OWNER | CPDMA_DESC_SOP | CPDMA_DESC_EOP;
	cpdma_desc_to_port(chan, mode, directed);

	/* Relaxed IO accessors can be used here as there is read barrier
	 * at the end of write sequence.
	 */
	writel_relaxed(0, &desc->hw_next);
	writel_relaxed(buffer, &desc->hw_buffer);
	writel_relaxed(len, &desc->hw_len);
	writel_relaxed(mode | len, &desc->hw_mode);
	writel_relaxed(token, &desc->sw_token);
	writel_relaxed(buffer, &desc->sw_buffer);
	writel_relaxed(len, &desc->sw_len);
	desc_read(desc, sw_len);

	__cpdma_chan_submit(chan, desc);

	if (chan->state == CPDMA_STATE_ACTIVE && chan->rxfree)
		chan_write(chan, rxfree, 1);

	chan->count++;

unlock_ret:
	spin_unlock_irqrestore(&chan->lock, flags);
	return ret;
}
EXPORT_SYMBOL_GPL(cpdma_chan_submit);

bool cpdma_check_free_tx_desc(struct cpdma_chan *chan)
{
	struct cpdma_ctlr	*ctlr = chan->ctlr;
	struct cpdma_desc_pool	*pool = ctlr->pool;
	bool			free_tx_desc;
	unsigned long		flags;

	spin_lock_irqsave(&chan->lock, flags);
	free_tx_desc = (chan->count < chan->desc_num) &&
			 gen_pool_avail(pool->gen_pool);
	spin_unlock_irqrestore(&chan->lock, flags);
	return free_tx_desc;
}
EXPORT_SYMBOL_GPL(cpdma_check_free_tx_desc);

static void __cpdma_chan_free(struct cpdma_chan *chan,
			      struct cpdma_desc __iomem *desc,
			      int outlen, int status)
{
	struct cpdma_ctlr		*ctlr = chan->ctlr;
	struct cpdma_desc_pool		*pool = ctlr->pool;
	dma_addr_t			buff_dma;
	int				origlen;
	void				*token;

	token      = (void *)desc_read(desc, sw_token);
	buff_dma   = desc_read(desc, sw_buffer);
	origlen    = desc_read(desc, sw_len);

	dma_unmap_single(ctlr->dev, buff_dma, origlen, chan->dir);
	cpdma_desc_free(pool, desc, 1);
	(*chan->handler)(token, outlen, status);
}

static int __cpdma_chan_process(struct cpdma_chan *chan)
{
	struct cpdma_ctlr		*ctlr = chan->ctlr;
	struct cpdma_desc __iomem	*desc;
	int				status, outlen;
	int				cb_status = 0;
	struct cpdma_desc_pool		*pool = ctlr->pool;
	dma_addr_t			desc_dma;
	unsigned long			flags;

	spin_lock_irqsave(&chan->lock, flags);

	desc = chan->head;
	if (!desc) {
		chan->stats.empty_dequeue++;
		status = -ENOENT;
		goto unlock_ret;
	}
	desc_dma = desc_phys(pool, desc);

	status	= desc_read(desc, hw_mode);
	outlen	= status & 0x7ff;
	if (status & CPDMA_DESC_OWNER) {
		chan->stats.busy_dequeue++;
		status = -EBUSY;
		goto unlock_ret;
	}

	if (status & CPDMA_DESC_PASS_CRC)
		outlen -= CPDMA_DESC_CRC_LEN;

	status	= status & (CPDMA_DESC_EOQ | CPDMA_DESC_TD_COMPLETE |
			    CPDMA_DESC_PORT_MASK);

	chan->head = desc_from_phys(pool, desc_read(desc, hw_next));
	chan_write(chan, cp, desc_dma);
	chan->count--;
	chan->stats.good_dequeue++;

	if ((status & CPDMA_DESC_EOQ) && chan->head) {
		chan->stats.requeue++;
		chan_write(chan, hdp, desc_phys(pool, chan->head));
	}

	spin_unlock_irqrestore(&chan->lock, flags);
	if (unlikely(status & CPDMA_DESC_TD_COMPLETE))
		cb_status = -ENOSYS;
	else
		cb_status = status;

	__cpdma_chan_free(chan, desc, outlen, cb_status);
	return status;

unlock_ret:
	spin_unlock_irqrestore(&chan->lock, flags);
	return status;
}

int cpdma_chan_process(struct cpdma_chan *chan, int quota)
{
	int used = 0, ret = 0;

	if (chan->state != CPDMA_STATE_ACTIVE)
		return -EINVAL;

	while (used < quota) {
		ret = __cpdma_chan_process(chan);
		if (ret < 0)
			break;
		used++;
	}
	return used;
}
EXPORT_SYMBOL_GPL(cpdma_chan_process);

int cpdma_chan_start(struct cpdma_chan *chan)
{
	struct cpdma_ctlr *ctlr = chan->ctlr;
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&ctlr->lock, flags);
	ret = cpdma_chan_set_chan_shaper(chan);
	spin_unlock_irqrestore(&ctlr->lock, flags);
	if (ret)
		return ret;

	ret = cpdma_chan_on(chan);
	if (ret)
		return ret;

	return 0;
}
EXPORT_SYMBOL_GPL(cpdma_chan_start);

int cpdma_chan_stop(struct cpdma_chan *chan)
{
	struct cpdma_ctlr	*ctlr = chan->ctlr;
	struct cpdma_desc_pool	*pool = ctlr->pool;
	unsigned long		flags;
	int			ret;
	unsigned		timeout;

	spin_lock_irqsave(&chan->lock, flags);
	if (chan->state == CPDMA_STATE_TEARDOWN) {
		spin_unlock_irqrestore(&chan->lock, flags);
		return -EINVAL;
	}

	chan->state = CPDMA_STATE_TEARDOWN;
	dma_reg_write(ctlr, chan->int_clear, chan->mask);

	/* trigger teardown */
	dma_reg_write(ctlr, chan->td, chan_linear(chan));

	/* wait for teardown complete */
	timeout = 100 * 100; /* 100 ms */
	while (timeout) {
		u32 cp = chan_read(chan, cp);
		if ((cp & CPDMA_TEARDOWN_VALUE) == CPDMA_TEARDOWN_VALUE)
			break;
		udelay(10);
		timeout--;
	}
	WARN_ON(!timeout);
	chan_write(chan, cp, CPDMA_TEARDOWN_VALUE);

	/* handle completed packets */
	spin_unlock_irqrestore(&chan->lock, flags);
	do {
		ret = __cpdma_chan_process(chan);
		if (ret < 0)
			break;
	} while ((ret & CPDMA_DESC_TD_COMPLETE) == 0);
	spin_lock_irqsave(&chan->lock, flags);

	/* remaining packets haven't been tx/rx'ed, clean them up */
	while (chan->head) {
		struct cpdma_desc __iomem *desc = chan->head;
		dma_addr_t next_dma;

		next_dma = desc_read(desc, hw_next);
		chan->head = desc_from_phys(pool, next_dma);
		chan->count--;
		chan->stats.teardown_dequeue++;

		/* issue callback without locks held */
		spin_unlock_irqrestore(&chan->lock, flags);
		__cpdma_chan_free(chan, desc, 0, -ENOSYS);
		spin_lock_irqsave(&chan->lock, flags);
	}

	chan->state = CPDMA_STATE_IDLE;
	spin_unlock_irqrestore(&chan->lock, flags);
	return 0;
}
EXPORT_SYMBOL_GPL(cpdma_chan_stop);

int cpdma_chan_int_ctrl(struct cpdma_chan *chan, bool enable)
{
	unsigned long flags;

	spin_lock_irqsave(&chan->lock, flags);
	if (chan->state != CPDMA_STATE_ACTIVE) {
		spin_unlock_irqrestore(&chan->lock, flags);
		return -EINVAL;
	}

	dma_reg_write(chan->ctlr, enable ? chan->int_set : chan->int_clear,
		      chan->mask);
	spin_unlock_irqrestore(&chan->lock, flags);

	return 0;
}

int cpdma_control_get(struct cpdma_ctlr *ctlr, int control)
{
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&ctlr->lock, flags);
	ret = _cpdma_control_get(ctlr, control);
	spin_unlock_irqrestore(&ctlr->lock, flags);

	return ret;
}

int cpdma_control_set(struct cpdma_ctlr *ctlr, int control, int value)
{
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&ctlr->lock, flags);
	ret = _cpdma_control_set(ctlr, control, value);
	spin_unlock_irqrestore(&ctlr->lock, flags);

	return ret;
}
EXPORT_SYMBOL_GPL(cpdma_control_set);

int cpdma_get_num_rx_descs(struct cpdma_ctlr *ctlr)
{
	return ctlr->num_rx_desc;
}
EXPORT_SYMBOL_GPL(cpdma_get_num_rx_descs);

int cpdma_get_num_tx_descs(struct cpdma_ctlr *ctlr)
{
	return ctlr->num_tx_desc;
}
EXPORT_SYMBOL_GPL(cpdma_get_num_tx_descs);

void cpdma_set_num_rx_descs(struct cpdma_ctlr *ctlr, int num_rx_desc)
{
	ctlr->num_rx_desc = num_rx_desc;
	ctlr->num_tx_desc = ctlr->pool->num_desc - ctlr->num_rx_desc;
}
EXPORT_SYMBOL_GPL(cpdma_set_num_rx_descs);

MODULE_LICENSE("GPL");

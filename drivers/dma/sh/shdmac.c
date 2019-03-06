// SPDX-License-Identifier: GPL-2.0+
/*
 * Renesas SuperH DMA Engine support
 *
 * base is drivers/dma/flsdma.c
 *
 * Copyright (C) 2011-2012 Guennadi Liakhovetski <g.liakhovetski@gmx.de>
 * Copyright (C) 2009 Nobuhiro Iwamatsu <iwamatsu.nobuhiro@renesas.com>
 * Copyright (C) 2009 Renesas Solutions, Inc. All rights reserved.
 * Copyright (C) 2007 Freescale Semiconductor, Inc. All rights reserved.
 *
 * - DMA of SuperH does not have Hardware DMA chain mode.
 * - MAX DMA size is 16MB.
 *
 */

#include <linux/delay.h>
#include <linux/dmaengine.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kdebug.h>
#include <linux/module.h>
#include <linux/notifier.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/rculist.h>
#include <linux/sh_dma.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

#include "../dmaengine.h"
#include "shdma.h"

/* DMA registers */
#define SAR	0x00	/* Source Address Register */
#define DAR	0x04	/* Destination Address Register */
#define TCR	0x08	/* Transfer Count Register */
#define CHCR	0x0C	/* Channel Control Register */
#define DMAOR	0x40	/* DMA Operation Register */

#define TEND	0x18 /* USB-DMAC */

#define SH_DMAE_DRV_NAME "sh-dma-engine"

/* Default MEMCPY transfer size = 2^2 = 4 bytes */
#define LOG2_DEFAULT_XFER_SIZE	2
#define SH_DMA_SLAVE_NUMBER 256
#define SH_DMA_TCR_MAX (16 * 1024 * 1024 - 1)

/*
 * Used for write-side mutual exclusion for the global device list,
 * read-side synchronization by way of RCU, and per-controller data.
 */
static DEFINE_SPINLOCK(sh_dmae_lock);
static LIST_HEAD(sh_dmae_devices);

/*
 * Different DMAC implementations provide different ways to clear DMA channels:
 * (1) none - no CHCLR registers are available
 * (2) one CHCLR register per channel - 0 has to be written to it to clear
 *     channel buffers
 * (3) one CHCLR per several channels - 1 has to be written to the bit,
 *     corresponding to the specific channel to reset it
 */
static void channel_clear(struct sh_dmae_chan *sh_dc)
{
	struct sh_dmae_device *shdev = to_sh_dev(sh_dc);
	const struct sh_dmae_channel *chan_pdata = shdev->pdata->channel +
		sh_dc->shdma_chan.id;
	u32 val = shdev->pdata->chclr_bitwise ? 1 << chan_pdata->chclr_bit : 0;

	__raw_writel(val, shdev->chan_reg + chan_pdata->chclr_offset);
}

static void sh_dmae_writel(struct sh_dmae_chan *sh_dc, u32 data, u32 reg)
{
	__raw_writel(data, sh_dc->base + reg);
}

static u32 sh_dmae_readl(struct sh_dmae_chan *sh_dc, u32 reg)
{
	return __raw_readl(sh_dc->base + reg);
}

static u16 dmaor_read(struct sh_dmae_device *shdev)
{
	void __iomem *addr = shdev->chan_reg + DMAOR;

	if (shdev->pdata->dmaor_is_32bit)
		return __raw_readl(addr);
	else
		return __raw_readw(addr);
}

static void dmaor_write(struct sh_dmae_device *shdev, u16 data)
{
	void __iomem *addr = shdev->chan_reg + DMAOR;

	if (shdev->pdata->dmaor_is_32bit)
		__raw_writel(data, addr);
	else
		__raw_writew(data, addr);
}

static void chcr_write(struct sh_dmae_chan *sh_dc, u32 data)
{
	struct sh_dmae_device *shdev = to_sh_dev(sh_dc);

	__raw_writel(data, sh_dc->base + shdev->chcr_offset);
}

static u32 chcr_read(struct sh_dmae_chan *sh_dc)
{
	struct sh_dmae_device *shdev = to_sh_dev(sh_dc);

	return __raw_readl(sh_dc->base + shdev->chcr_offset);
}

/*
 * Reset DMA controller
 *
 * SH7780 has two DMAOR register
 */
static void sh_dmae_ctl_stop(struct sh_dmae_device *shdev)
{
	unsigned short dmaor;
	unsigned long flags;

	spin_lock_irqsave(&sh_dmae_lock, flags);

	dmaor = dmaor_read(shdev);
	dmaor_write(shdev, dmaor & ~(DMAOR_NMIF | DMAOR_AE | DMAOR_DME));

	spin_unlock_irqrestore(&sh_dmae_lock, flags);
}

static int sh_dmae_rst(struct sh_dmae_device *shdev)
{
	unsigned short dmaor;
	unsigned long flags;

	spin_lock_irqsave(&sh_dmae_lock, flags);

	dmaor = dmaor_read(shdev) & ~(DMAOR_NMIF | DMAOR_AE | DMAOR_DME);

	if (shdev->pdata->chclr_present) {
		int i;
		for (i = 0; i < shdev->pdata->channel_num; i++) {
			struct sh_dmae_chan *sh_chan = shdev->chan[i];
			if (sh_chan)
				channel_clear(sh_chan);
		}
	}

	dmaor_write(shdev, dmaor | shdev->pdata->dmaor_init);

	dmaor = dmaor_read(shdev);

	spin_unlock_irqrestore(&sh_dmae_lock, flags);

	if (dmaor & (DMAOR_AE | DMAOR_NMIF)) {
		dev_warn(shdev->shdma_dev.dma_dev.dev, "Can't initialize DMAOR.\n");
		return -EIO;
	}
	if (shdev->pdata->dmaor_init & ~dmaor)
		dev_warn(shdev->shdma_dev.dma_dev.dev,
			 "DMAOR=0x%x hasn't latched the initial value 0x%x.\n",
			 dmaor, shdev->pdata->dmaor_init);
	return 0;
}

static bool dmae_is_busy(struct sh_dmae_chan *sh_chan)
{
	u32 chcr = chcr_read(sh_chan);

	if ((chcr & (CHCR_DE | CHCR_TE)) == CHCR_DE)
		return true; /* working */

	return false; /* waiting */
}

static unsigned int calc_xmit_shift(struct sh_dmae_chan *sh_chan, u32 chcr)
{
	struct sh_dmae_device *shdev = to_sh_dev(sh_chan);
	const struct sh_dmae_pdata *pdata = shdev->pdata;
	int cnt = ((chcr & pdata->ts_low_mask) >> pdata->ts_low_shift) |
		((chcr & pdata->ts_high_mask) >> pdata->ts_high_shift);

	if (cnt >= pdata->ts_shift_num)
		cnt = 0;

	return pdata->ts_shift[cnt];
}

static u32 log2size_to_chcr(struct sh_dmae_chan *sh_chan, int l2size)
{
	struct sh_dmae_device *shdev = to_sh_dev(sh_chan);
	const struct sh_dmae_pdata *pdata = shdev->pdata;
	int i;

	for (i = 0; i < pdata->ts_shift_num; i++)
		if (pdata->ts_shift[i] == l2size)
			break;

	if (i == pdata->ts_shift_num)
		i = 0;

	return ((i << pdata->ts_low_shift) & pdata->ts_low_mask) |
		((i << pdata->ts_high_shift) & pdata->ts_high_mask);
}

static void dmae_set_reg(struct sh_dmae_chan *sh_chan, struct sh_dmae_regs *hw)
{
	sh_dmae_writel(sh_chan, hw->sar, SAR);
	sh_dmae_writel(sh_chan, hw->dar, DAR);
	sh_dmae_writel(sh_chan, hw->tcr >> sh_chan->xmit_shift, TCR);
}

static void dmae_start(struct sh_dmae_chan *sh_chan)
{
	struct sh_dmae_device *shdev = to_sh_dev(sh_chan);
	u32 chcr = chcr_read(sh_chan);

	if (shdev->pdata->needs_tend_set)
		sh_dmae_writel(sh_chan, 0xFFFFFFFF, TEND);

	chcr |= CHCR_DE | shdev->chcr_ie_bit;
	chcr_write(sh_chan, chcr & ~CHCR_TE);
}

static void dmae_init(struct sh_dmae_chan *sh_chan)
{
	/*
	 * Default configuration for dual address memory-memory transfer.
	 */
	u32 chcr = DM_INC | SM_INC | RS_AUTO | log2size_to_chcr(sh_chan,
						   LOG2_DEFAULT_XFER_SIZE);
	sh_chan->xmit_shift = calc_xmit_shift(sh_chan, chcr);
	chcr_write(sh_chan, chcr);
}

static int dmae_set_chcr(struct sh_dmae_chan *sh_chan, u32 val)
{
	/* If DMA is active, cannot set CHCR. TODO: remove this superfluous check */
	if (dmae_is_busy(sh_chan))
		return -EBUSY;

	sh_chan->xmit_shift = calc_xmit_shift(sh_chan, val);
	chcr_write(sh_chan, val);

	return 0;
}

static int dmae_set_dmars(struct sh_dmae_chan *sh_chan, u16 val)
{
	struct sh_dmae_device *shdev = to_sh_dev(sh_chan);
	const struct sh_dmae_pdata *pdata = shdev->pdata;
	const struct sh_dmae_channel *chan_pdata = &pdata->channel[sh_chan->shdma_chan.id];
	void __iomem *addr = shdev->dmars;
	unsigned int shift = chan_pdata->dmars_bit;

	if (dmae_is_busy(sh_chan))
		return -EBUSY;

	if (pdata->no_dmars)
		return 0;

	/* in the case of a missing DMARS resource use first memory window */
	if (!addr)
		addr = shdev->chan_reg;
	addr += chan_pdata->dmars;

	__raw_writew((__raw_readw(addr) & (0xff00 >> shift)) | (val << shift),
		     addr);

	return 0;
}

static void sh_dmae_start_xfer(struct shdma_chan *schan,
			       struct shdma_desc *sdesc)
{
	struct sh_dmae_chan *sh_chan = container_of(schan, struct sh_dmae_chan,
						    shdma_chan);
	struct sh_dmae_desc *sh_desc = container_of(sdesc,
					struct sh_dmae_desc, shdma_desc);
	dev_dbg(sh_chan->shdma_chan.dev, "Queue #%d to %d: %u@%x -> %x\n",
		sdesc->async_tx.cookie, sh_chan->shdma_chan.id,
		sh_desc->hw.tcr, sh_desc->hw.sar, sh_desc->hw.dar);
	/* Get the ld start address from ld_queue */
	dmae_set_reg(sh_chan, &sh_desc->hw);
	dmae_start(sh_chan);
}

static bool sh_dmae_channel_busy(struct shdma_chan *schan)
{
	struct sh_dmae_chan *sh_chan = container_of(schan, struct sh_dmae_chan,
						    shdma_chan);
	return dmae_is_busy(sh_chan);
}

static void sh_dmae_setup_xfer(struct shdma_chan *schan,
			       int slave_id)
{
	struct sh_dmae_chan *sh_chan = container_of(schan, struct sh_dmae_chan,
						    shdma_chan);

	if (slave_id >= 0) {
		const struct sh_dmae_slave_config *cfg =
			sh_chan->config;

		dmae_set_dmars(sh_chan, cfg->mid_rid);
		dmae_set_chcr(sh_chan, cfg->chcr);
	} else {
		dmae_init(sh_chan);
	}
}

/*
 * Find a slave channel configuration from the contoller list by either a slave
 * ID in the non-DT case, or by a MID/RID value in the DT case
 */
static const struct sh_dmae_slave_config *dmae_find_slave(
	struct sh_dmae_chan *sh_chan, int match)
{
	struct sh_dmae_device *shdev = to_sh_dev(sh_chan);
	const struct sh_dmae_pdata *pdata = shdev->pdata;
	const struct sh_dmae_slave_config *cfg;
	int i;

	if (!sh_chan->shdma_chan.dev->of_node) {
		if (match >= SH_DMA_SLAVE_NUMBER)
			return NULL;

		for (i = 0, cfg = pdata->slave; i < pdata->slave_num; i++, cfg++)
			if (cfg->slave_id == match)
				return cfg;
	} else {
		for (i = 0, cfg = pdata->slave; i < pdata->slave_num; i++, cfg++)
			if (cfg->mid_rid == match) {
				sh_chan->shdma_chan.slave_id = i;
				return cfg;
			}
	}

	return NULL;
}

static int sh_dmae_set_slave(struct shdma_chan *schan,
			     int slave_id, dma_addr_t slave_addr, bool try)
{
	struct sh_dmae_chan *sh_chan = container_of(schan, struct sh_dmae_chan,
						    shdma_chan);
	const struct sh_dmae_slave_config *cfg = dmae_find_slave(sh_chan, slave_id);
	if (!cfg)
		return -ENXIO;

	if (!try) {
		sh_chan->config = cfg;
		sh_chan->slave_addr = slave_addr ? : cfg->addr;
	}

	return 0;
}

static void dmae_halt(struct sh_dmae_chan *sh_chan)
{
	struct sh_dmae_device *shdev = to_sh_dev(sh_chan);
	u32 chcr = chcr_read(sh_chan);

	chcr &= ~(CHCR_DE | CHCR_TE | shdev->chcr_ie_bit);
	chcr_write(sh_chan, chcr);
}

static int sh_dmae_desc_setup(struct shdma_chan *schan,
			      struct shdma_desc *sdesc,
			      dma_addr_t src, dma_addr_t dst, size_t *len)
{
	struct sh_dmae_desc *sh_desc = container_of(sdesc,
					struct sh_dmae_desc, shdma_desc);

	if (*len > schan->max_xfer_len)
		*len = schan->max_xfer_len;

	sh_desc->hw.sar = src;
	sh_desc->hw.dar = dst;
	sh_desc->hw.tcr = *len;

	return 0;
}

static void sh_dmae_halt(struct shdma_chan *schan)
{
	struct sh_dmae_chan *sh_chan = container_of(schan, struct sh_dmae_chan,
						    shdma_chan);
	dmae_halt(sh_chan);
}

static bool sh_dmae_chan_irq(struct shdma_chan *schan, int irq)
{
	struct sh_dmae_chan *sh_chan = container_of(schan, struct sh_dmae_chan,
						    shdma_chan);

	if (!(chcr_read(sh_chan) & CHCR_TE))
		return false;

	/* DMA stop */
	dmae_halt(sh_chan);

	return true;
}

static size_t sh_dmae_get_partial(struct shdma_chan *schan,
				  struct shdma_desc *sdesc)
{
	struct sh_dmae_chan *sh_chan = container_of(schan, struct sh_dmae_chan,
						    shdma_chan);
	struct sh_dmae_desc *sh_desc = container_of(sdesc,
					struct sh_dmae_desc, shdma_desc);
	return sh_desc->hw.tcr -
		(sh_dmae_readl(sh_chan, TCR) << sh_chan->xmit_shift);
}

/* Called from error IRQ or NMI */
static bool sh_dmae_reset(struct sh_dmae_device *shdev)
{
	bool ret;

	/* halt the dma controller */
	sh_dmae_ctl_stop(shdev);

	/* We cannot detect, which channel caused the error, have to reset all */
	ret = shdma_reset(&shdev->shdma_dev);

	sh_dmae_rst(shdev);

	return ret;
}

static irqreturn_t sh_dmae_err(int irq, void *data)
{
	struct sh_dmae_device *shdev = data;

	if (!(dmaor_read(shdev) & DMAOR_AE))
		return IRQ_NONE;

	sh_dmae_reset(shdev);
	return IRQ_HANDLED;
}

static bool sh_dmae_desc_completed(struct shdma_chan *schan,
				   struct shdma_desc *sdesc)
{
	struct sh_dmae_chan *sh_chan = container_of(schan,
					struct sh_dmae_chan, shdma_chan);
	struct sh_dmae_desc *sh_desc = container_of(sdesc,
					struct sh_dmae_desc, shdma_desc);
	u32 sar_buf = sh_dmae_readl(sh_chan, SAR);
	u32 dar_buf = sh_dmae_readl(sh_chan, DAR);

	return	(sdesc->direction == DMA_DEV_TO_MEM &&
		 (sh_desc->hw.dar + sh_desc->hw.tcr) == dar_buf) ||
		(sdesc->direction != DMA_DEV_TO_MEM &&
		 (sh_desc->hw.sar + sh_desc->hw.tcr) == sar_buf);
}

static bool sh_dmae_nmi_notify(struct sh_dmae_device *shdev)
{
	/* Fast path out if NMIF is not asserted for this controller */
	if ((dmaor_read(shdev) & DMAOR_NMIF) == 0)
		return false;

	return sh_dmae_reset(shdev);
}

static int sh_dmae_nmi_handler(struct notifier_block *self,
			       unsigned long cmd, void *data)
{
	struct sh_dmae_device *shdev;
	int ret = NOTIFY_DONE;
	bool triggered;

	/*
	 * Only concern ourselves with NMI events.
	 *
	 * Normally we would check the die chain value, but as this needs
	 * to be architecture independent, check for NMI context instead.
	 */
	if (!in_nmi())
		return NOTIFY_DONE;

	rcu_read_lock();
	list_for_each_entry_rcu(shdev, &sh_dmae_devices, node) {
		/*
		 * Only stop if one of the controllers has NMIF asserted,
		 * we do not want to interfere with regular address error
		 * handling or NMI events that don't concern the DMACs.
		 */
		triggered = sh_dmae_nmi_notify(shdev);
		if (triggered == true)
			ret = NOTIFY_OK;
	}
	rcu_read_unlock();

	return ret;
}

static struct notifier_block sh_dmae_nmi_notifier __read_mostly = {
	.notifier_call	= sh_dmae_nmi_handler,

	/* Run before NMI debug handler and KGDB */
	.priority	= 1,
};

static int sh_dmae_chan_probe(struct sh_dmae_device *shdev, int id,
					int irq, unsigned long flags)
{
	const struct sh_dmae_channel *chan_pdata = &shdev->pdata->channel[id];
	struct shdma_dev *sdev = &shdev->shdma_dev;
	struct platform_device *pdev = to_platform_device(sdev->dma_dev.dev);
	struct sh_dmae_chan *sh_chan;
	struct shdma_chan *schan;
	int err;

	sh_chan = devm_kzalloc(sdev->dma_dev.dev, sizeof(struct sh_dmae_chan),
			       GFP_KERNEL);
	if (!sh_chan)
		return -ENOMEM;

	schan = &sh_chan->shdma_chan;
	schan->max_xfer_len = SH_DMA_TCR_MAX + 1;

	shdma_chan_probe(sdev, schan, id);

	sh_chan->base = shdev->chan_reg + chan_pdata->offset;

	/* set up channel irq */
	if (pdev->id >= 0)
		snprintf(sh_chan->dev_id, sizeof(sh_chan->dev_id),
			 "sh-dmae%d.%d", pdev->id, id);
	else
		snprintf(sh_chan->dev_id, sizeof(sh_chan->dev_id),
			 "sh-dma%d", id);

	err = shdma_request_irq(schan, irq, flags, sh_chan->dev_id);
	if (err) {
		dev_err(sdev->dma_dev.dev,
			"DMA channel %d request_irq error %d\n",
			id, err);
		goto err_no_irq;
	}

	shdev->chan[id] = sh_chan;
	return 0;

err_no_irq:
	/* remove from dmaengine device node */
	shdma_chan_remove(schan);
	return err;
}

static void sh_dmae_chan_remove(struct sh_dmae_device *shdev)
{
	struct shdma_chan *schan;
	int i;

	shdma_for_each_chan(schan, &shdev->shdma_dev, i) {
		BUG_ON(!schan);

		shdma_chan_remove(schan);
	}
}

#ifdef CONFIG_PM
static int sh_dmae_runtime_suspend(struct device *dev)
{
	struct sh_dmae_device *shdev = dev_get_drvdata(dev);

	sh_dmae_ctl_stop(shdev);
	return 0;
}

static int sh_dmae_runtime_resume(struct device *dev)
{
	struct sh_dmae_device *shdev = dev_get_drvdata(dev);

	return sh_dmae_rst(shdev);
}
#endif

#ifdef CONFIG_PM_SLEEP
static int sh_dmae_suspend(struct device *dev)
{
	struct sh_dmae_device *shdev = dev_get_drvdata(dev);

	sh_dmae_ctl_stop(shdev);
	return 0;
}

static int sh_dmae_resume(struct device *dev)
{
	struct sh_dmae_device *shdev = dev_get_drvdata(dev);
	int i, ret;

	ret = sh_dmae_rst(shdev);
	if (ret < 0)
		dev_err(dev, "Failed to reset!\n");

	for (i = 0; i < shdev->pdata->channel_num; i++) {
		struct sh_dmae_chan *sh_chan = shdev->chan[i];

		if (!sh_chan->shdma_chan.desc_num)
			continue;

		if (sh_chan->shdma_chan.slave_id >= 0) {
			const struct sh_dmae_slave_config *cfg = sh_chan->config;
			dmae_set_dmars(sh_chan, cfg->mid_rid);
			dmae_set_chcr(sh_chan, cfg->chcr);
		} else {
			dmae_init(sh_chan);
		}
	}

	return 0;
}
#endif

static const struct dev_pm_ops sh_dmae_pm = {
	SET_SYSTEM_SLEEP_PM_OPS(sh_dmae_suspend, sh_dmae_resume)
	SET_RUNTIME_PM_OPS(sh_dmae_runtime_suspend, sh_dmae_runtime_resume,
			   NULL)
};

static dma_addr_t sh_dmae_slave_addr(struct shdma_chan *schan)
{
	struct sh_dmae_chan *sh_chan = container_of(schan,
					struct sh_dmae_chan, shdma_chan);

	/*
	 * Implicit BUG_ON(!sh_chan->config)
	 * This is an exclusive slave DMA operation, may only be called after a
	 * successful slave configuration.
	 */
	return sh_chan->slave_addr;
}

static struct shdma_desc *sh_dmae_embedded_desc(void *buf, int i)
{
	return &((struct sh_dmae_desc *)buf)[i].shdma_desc;
}

static const struct shdma_ops sh_dmae_shdma_ops = {
	.desc_completed = sh_dmae_desc_completed,
	.halt_channel = sh_dmae_halt,
	.channel_busy = sh_dmae_channel_busy,
	.slave_addr = sh_dmae_slave_addr,
	.desc_setup = sh_dmae_desc_setup,
	.set_slave = sh_dmae_set_slave,
	.setup_xfer = sh_dmae_setup_xfer,
	.start_xfer = sh_dmae_start_xfer,
	.embedded_desc = sh_dmae_embedded_desc,
	.chan_irq = sh_dmae_chan_irq,
	.get_partial = sh_dmae_get_partial,
};

static int sh_dmae_probe(struct platform_device *pdev)
{
	const enum dma_slave_buswidth widths =
		DMA_SLAVE_BUSWIDTH_1_BYTE   | DMA_SLAVE_BUSWIDTH_2_BYTES |
		DMA_SLAVE_BUSWIDTH_4_BYTES  | DMA_SLAVE_BUSWIDTH_8_BYTES |
		DMA_SLAVE_BUSWIDTH_16_BYTES | DMA_SLAVE_BUSWIDTH_32_BYTES;
	const struct sh_dmae_pdata *pdata;
	unsigned long chan_flag[SH_DMAE_MAX_CHANNELS] = {};
	int chan_irq[SH_DMAE_MAX_CHANNELS];
	unsigned long irqflags = 0;
	int err, errirq, i, irq_cnt = 0, irqres = 0, irq_cap = 0;
	struct sh_dmae_device *shdev;
	struct dma_device *dma_dev;
	struct resource *chan, *dmars, *errirq_res, *chanirq_res;

	if (pdev->dev.of_node)
		pdata = of_device_get_match_data(&pdev->dev);
	else
		pdata = dev_get_platdata(&pdev->dev);

	/* get platform data */
	if (!pdata || !pdata->channel_num)
		return -ENODEV;

	chan = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	/* DMARS area is optional */
	dmars = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	/*
	 * IRQ resources:
	 * 1. there always must be at least one IRQ IO-resource. On SH4 it is
	 *    the error IRQ, in which case it is the only IRQ in this resource:
	 *    start == end. If it is the only IRQ resource, all channels also
	 *    use the same IRQ.
	 * 2. DMA channel IRQ resources can be specified one per resource or in
	 *    ranges (start != end)
	 * 3. iff all events (channels and, optionally, error) on this
	 *    controller use the same IRQ, only one IRQ resource can be
	 *    specified, otherwise there must be one IRQ per channel, even if
	 *    some of them are equal
	 * 4. if all IRQs on this controller are equal or if some specific IRQs
	 *    specify IORESOURCE_IRQ_SHAREABLE in their resources, they will be
	 *    requested with the IRQF_SHARED flag
	 */
	errirq_res = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (!chan || !errirq_res)
		return -ENODEV;

	shdev = devm_kzalloc(&pdev->dev, sizeof(struct sh_dmae_device),
			     GFP_KERNEL);
	if (!shdev)
		return -ENOMEM;

	dma_dev = &shdev->shdma_dev.dma_dev;

	shdev->chan_reg = devm_ioremap_resource(&pdev->dev, chan);
	if (IS_ERR(shdev->chan_reg))
		return PTR_ERR(shdev->chan_reg);
	if (dmars) {
		shdev->dmars = devm_ioremap_resource(&pdev->dev, dmars);
		if (IS_ERR(shdev->dmars))
			return PTR_ERR(shdev->dmars);
	}

	dma_dev->src_addr_widths = widths;
	dma_dev->dst_addr_widths = widths;
	dma_dev->directions = BIT(DMA_MEM_TO_DEV) | BIT(DMA_DEV_TO_MEM);
	dma_dev->residue_granularity = DMA_RESIDUE_GRANULARITY_DESCRIPTOR;

	if (!pdata->slave_only)
		dma_cap_set(DMA_MEMCPY, dma_dev->cap_mask);
	if (pdata->slave && pdata->slave_num)
		dma_cap_set(DMA_SLAVE, dma_dev->cap_mask);

	/* Default transfer size of 32 bytes requires 32-byte alignment */
	dma_dev->copy_align = LOG2_DEFAULT_XFER_SIZE;

	shdev->shdma_dev.ops = &sh_dmae_shdma_ops;
	shdev->shdma_dev.desc_size = sizeof(struct sh_dmae_desc);
	err = shdma_init(&pdev->dev, &shdev->shdma_dev,
			      pdata->channel_num);
	if (err < 0)
		goto eshdma;

	/* platform data */
	shdev->pdata = pdata;

	if (pdata->chcr_offset)
		shdev->chcr_offset = pdata->chcr_offset;
	else
		shdev->chcr_offset = CHCR;

	if (pdata->chcr_ie_bit)
		shdev->chcr_ie_bit = pdata->chcr_ie_bit;
	else
		shdev->chcr_ie_bit = CHCR_IE;

	platform_set_drvdata(pdev, shdev);

	pm_runtime_enable(&pdev->dev);
	err = pm_runtime_get_sync(&pdev->dev);
	if (err < 0)
		dev_err(&pdev->dev, "%s(): GET = %d\n", __func__, err);

	spin_lock_irq(&sh_dmae_lock);
	list_add_tail_rcu(&shdev->node, &sh_dmae_devices);
	spin_unlock_irq(&sh_dmae_lock);

	/* reset dma controller - only needed as a test */
	err = sh_dmae_rst(shdev);
	if (err)
		goto rst_err;

	if (IS_ENABLED(CONFIG_CPU_SH4) || IS_ENABLED(CONFIG_ARCH_RENESAS)) {
		chanirq_res = platform_get_resource(pdev, IORESOURCE_IRQ, 1);

		if (!chanirq_res)
			chanirq_res = errirq_res;
		else
			irqres++;

		if (chanirq_res == errirq_res ||
		    (errirq_res->flags & IORESOURCE_BITS) == IORESOURCE_IRQ_SHAREABLE)
			irqflags = IRQF_SHARED;

		errirq = errirq_res->start;

		err = devm_request_irq(&pdev->dev, errirq, sh_dmae_err,
				       irqflags, "DMAC Address Error", shdev);
		if (err) {
			dev_err(&pdev->dev,
				"DMA failed requesting irq #%d, error %d\n",
				errirq, err);
			goto eirq_err;
		}
	} else {
		chanirq_res = errirq_res;
	}

	if (chanirq_res->start == chanirq_res->end &&
	    !platform_get_resource(pdev, IORESOURCE_IRQ, 1)) {
		/* Special case - all multiplexed */
		for (; irq_cnt < pdata->channel_num; irq_cnt++) {
			if (irq_cnt < SH_DMAE_MAX_CHANNELS) {
				chan_irq[irq_cnt] = chanirq_res->start;
				chan_flag[irq_cnt] = IRQF_SHARED;
			} else {
				irq_cap = 1;
				break;
			}
		}
	} else {
		do {
			for (i = chanirq_res->start; i <= chanirq_res->end; i++) {
				if (irq_cnt >= SH_DMAE_MAX_CHANNELS) {
					irq_cap = 1;
					break;
				}

				if ((errirq_res->flags & IORESOURCE_BITS) ==
				    IORESOURCE_IRQ_SHAREABLE)
					chan_flag[irq_cnt] = IRQF_SHARED;
				else
					chan_flag[irq_cnt] = 0;
				dev_dbg(&pdev->dev,
					"Found IRQ %d for channel %d\n",
					i, irq_cnt);
				chan_irq[irq_cnt++] = i;
			}

			if (irq_cnt >= SH_DMAE_MAX_CHANNELS)
				break;

			chanirq_res = platform_get_resource(pdev,
						IORESOURCE_IRQ, ++irqres);
		} while (irq_cnt < pdata->channel_num && chanirq_res);
	}

	/* Create DMA Channel */
	for (i = 0; i < irq_cnt; i++) {
		err = sh_dmae_chan_probe(shdev, i, chan_irq[i], chan_flag[i]);
		if (err)
			goto chan_probe_err;
	}

	if (irq_cap)
		dev_notice(&pdev->dev, "Attempting to register %d DMA "
			   "channels when a maximum of %d are supported.\n",
			   pdata->channel_num, SH_DMAE_MAX_CHANNELS);

	pm_runtime_put(&pdev->dev);

	err = dma_async_device_register(&shdev->shdma_dev.dma_dev);
	if (err < 0)
		goto edmadevreg;

	return err;

edmadevreg:
	pm_runtime_get(&pdev->dev);

chan_probe_err:
	sh_dmae_chan_remove(shdev);

eirq_err:
rst_err:
	spin_lock_irq(&sh_dmae_lock);
	list_del_rcu(&shdev->node);
	spin_unlock_irq(&sh_dmae_lock);

	pm_runtime_put(&pdev->dev);
	pm_runtime_disable(&pdev->dev);

	shdma_cleanup(&shdev->shdma_dev);
eshdma:
	synchronize_rcu();

	return err;
}

static int sh_dmae_remove(struct platform_device *pdev)
{
	struct sh_dmae_device *shdev = platform_get_drvdata(pdev);
	struct dma_device *dma_dev = &shdev->shdma_dev.dma_dev;

	dma_async_device_unregister(dma_dev);

	spin_lock_irq(&sh_dmae_lock);
	list_del_rcu(&shdev->node);
	spin_unlock_irq(&sh_dmae_lock);

	pm_runtime_disable(&pdev->dev);

	sh_dmae_chan_remove(shdev);
	shdma_cleanup(&shdev->shdma_dev);

	synchronize_rcu();

	return 0;
}

static struct platform_driver sh_dmae_driver = {
	.driver		= {
		.pm	= &sh_dmae_pm,
		.name	= SH_DMAE_DRV_NAME,
	},
	.remove		= sh_dmae_remove,
};

static int __init sh_dmae_init(void)
{
	/* Wire up NMI handling */
	int err = register_die_notifier(&sh_dmae_nmi_notifier);
	if (err)
		return err;

	return platform_driver_probe(&sh_dmae_driver, sh_dmae_probe);
}
module_init(sh_dmae_init);

static void __exit sh_dmae_exit(void)
{
	platform_driver_unregister(&sh_dmae_driver);

	unregister_die_notifier(&sh_dmae_nmi_notifier);
}
module_exit(sh_dmae_exit);

MODULE_AUTHOR("Nobuhiro Iwamatsu <iwamatsu.nobuhiro@renesas.com>");
MODULE_DESCRIPTION("Renesas SH DMA Engine driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:" SH_DMAE_DRV_NAME);

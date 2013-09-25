/*
 * Copyright (C) 2011-2013 Renesas Electronics Corporation
 * Copyright (C) 2013 Cogent Embedded, Inc.
 *
 * This file is based on the drivers/dma/sh/shdma.c
 *
 * Renesas SuperH DMA Engine support
 *
 * This is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * - DMA of SuperH does not have Hardware DMA chain mode.
 * - max DMA size is 16MB.
 *
 */

#include <linux/dmaengine.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/platform_data/dma-rcar-hpbdma.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/shdma-base.h>
#include <linux/slab.h>

/* DMA channel registers */
#define HPB_DMAE_DSAR0	0x00
#define HPB_DMAE_DDAR0	0x04
#define HPB_DMAE_DTCR0	0x08
#define HPB_DMAE_DSAR1	0x0C
#define HPB_DMAE_DDAR1	0x10
#define HPB_DMAE_DTCR1	0x14
#define HPB_DMAE_DSASR	0x18
#define HPB_DMAE_DDASR	0x1C
#define HPB_DMAE_DTCSR	0x20
#define HPB_DMAE_DPTR	0x24
#define HPB_DMAE_DCR	0x28
#define HPB_DMAE_DCMDR	0x2C
#define HPB_DMAE_DSTPR	0x30
#define HPB_DMAE_DSTSR	0x34
#define HPB_DMAE_DDBGR	0x38
#define HPB_DMAE_DDBGR2	0x3C
#define HPB_DMAE_CHAN(n)	(0x40 * (n))

/* DMA command register (DCMDR) bits */
#define HPB_DMAE_DCMDR_BDOUT	BIT(7)
#define HPB_DMAE_DCMDR_DQSPD	BIT(6)
#define HPB_DMAE_DCMDR_DQSPC	BIT(5)
#define HPB_DMAE_DCMDR_DMSPD	BIT(4)
#define HPB_DMAE_DCMDR_DMSPC	BIT(3)
#define HPB_DMAE_DCMDR_DQEND	BIT(2)
#define HPB_DMAE_DCMDR_DNXT	BIT(1)
#define HPB_DMAE_DCMDR_DMEN	BIT(0)

/* DMA forced stop register (DSTPR) bits */
#define HPB_DMAE_DSTPR_DMSTP	BIT(0)

/* DMA status register (DSTSR) bits */
#define HPB_DMAE_DSTSR_DMSTS	BIT(0)

/* DMA common registers */
#define HPB_DMAE_DTIMR		0x00
#define HPB_DMAE_DINTSR0		0x0C
#define HPB_DMAE_DINTSR1		0x10
#define HPB_DMAE_DINTCR0		0x14
#define HPB_DMAE_DINTCR1		0x18
#define HPB_DMAE_DINTMR0		0x1C
#define HPB_DMAE_DINTMR1		0x20
#define HPB_DMAE_DACTSR0		0x24
#define HPB_DMAE_DACTSR1		0x28
#define HPB_DMAE_HSRSTR(n)	(0x40 + (n) * 4)
#define HPB_DMAE_HPB_DMASPR(n)	(0x140 + (n) * 4)
#define HPB_DMAE_HPB_DMLVLR0	0x160
#define HPB_DMAE_HPB_DMLVLR1	0x164
#define HPB_DMAE_HPB_DMSHPT0	0x168
#define HPB_DMAE_HPB_DMSHPT1	0x16C

#define HPB_DMA_SLAVE_NUMBER 256
#define HPB_DMA_TCR_MAX 0x01000000	/* 16 MiB */

struct hpb_dmae_chan {
	struct shdma_chan shdma_chan;
	int xfer_mode;			/* DMA transfer mode */
#define XFER_SINGLE	1
#define XFER_DOUBLE	2
	unsigned plane_idx;		/* current DMA information set */
	bool first_desc;		/* first/next transfer */
	int xmit_shift;			/* log_2(bytes_per_xfer) */
	void __iomem *base;
	const struct hpb_dmae_slave_config *cfg;
	char dev_id[16];		/* unique name per DMAC of channel */
};

struct hpb_dmae_device {
	struct shdma_dev shdma_dev;
	spinlock_t reg_lock;		/* comm_reg operation lock */
	struct hpb_dmae_pdata *pdata;
	void __iomem *chan_reg;
	void __iomem *comm_reg;
	void __iomem *reset_reg;
	void __iomem *mode_reg;
};

struct hpb_dmae_regs {
	u32 sar; /* SAR / source address */
	u32 dar; /* DAR / destination address */
	u32 tcr; /* TCR / transfer count */
};

struct hpb_desc {
	struct shdma_desc shdma_desc;
	struct hpb_dmae_regs hw;
	unsigned plane_idx;
};

#define to_chan(schan) container_of(schan, struct hpb_dmae_chan, shdma_chan)
#define to_desc(sdesc) container_of(sdesc, struct hpb_desc, shdma_desc)
#define to_dev(sc) container_of(sc->shdma_chan.dma_chan.device, \
				struct hpb_dmae_device, shdma_dev.dma_dev)

static void ch_reg_write(struct hpb_dmae_chan *hpb_dc, u32 data, u32 reg)
{
	iowrite32(data, hpb_dc->base + reg);
}

static u32 ch_reg_read(struct hpb_dmae_chan *hpb_dc, u32 reg)
{
	return ioread32(hpb_dc->base + reg);
}

static void dcmdr_write(struct hpb_dmae_device *hpbdev, u32 data)
{
	iowrite32(data, hpbdev->chan_reg + HPB_DMAE_DCMDR);
}

static void hsrstr_write(struct hpb_dmae_device *hpbdev, u32 ch)
{
	iowrite32(0x1, hpbdev->comm_reg + HPB_DMAE_HSRSTR(ch));
}

static u32 dintsr_read(struct hpb_dmae_device *hpbdev, u32 ch)
{
	u32 v;

	if (ch < 32)
		v = ioread32(hpbdev->comm_reg + HPB_DMAE_DINTSR0) >> ch;
	else
		v = ioread32(hpbdev->comm_reg + HPB_DMAE_DINTSR1) >> (ch - 32);
	return v & 0x1;
}

static void dintcr_write(struct hpb_dmae_device *hpbdev, u32 ch)
{
	if (ch < 32)
		iowrite32((0x1 << ch), hpbdev->comm_reg + HPB_DMAE_DINTCR0);
	else
		iowrite32((0x1 << (ch - 32)),
			  hpbdev->comm_reg + HPB_DMAE_DINTCR1);
}

static void asyncmdr_write(struct hpb_dmae_device *hpbdev, u32 data)
{
	iowrite32(data, hpbdev->mode_reg);
}

static u32 asyncmdr_read(struct hpb_dmae_device *hpbdev)
{
	return ioread32(hpbdev->mode_reg);
}

static void hpb_dmae_enable_int(struct hpb_dmae_device *hpbdev, u32 ch)
{
	u32 intreg;

	spin_lock_irq(&hpbdev->reg_lock);
	if (ch < 32) {
		intreg = ioread32(hpbdev->comm_reg + HPB_DMAE_DINTMR0);
		iowrite32(BIT(ch) | intreg,
			  hpbdev->comm_reg + HPB_DMAE_DINTMR0);
	} else {
		intreg = ioread32(hpbdev->comm_reg + HPB_DMAE_DINTMR1);
		iowrite32(BIT(ch - 32) | intreg,
			  hpbdev->comm_reg + HPB_DMAE_DINTMR1);
	}
	spin_unlock_irq(&hpbdev->reg_lock);
}

static void hpb_dmae_async_reset(struct hpb_dmae_device *hpbdev, u32 data)
{
	u32 rstr;
	int timeout = 10000;	/* 100 ms */

	spin_lock(&hpbdev->reg_lock);
	rstr = ioread32(hpbdev->reset_reg);
	rstr |= data;
	iowrite32(rstr, hpbdev->reset_reg);
	do {
		rstr = ioread32(hpbdev->reset_reg);
		if ((rstr & data) == data)
			break;
		udelay(10);
	} while (timeout--);

	if (timeout < 0)
		dev_err(hpbdev->shdma_dev.dma_dev.dev,
			"%s timeout\n", __func__);

	rstr &= ~data;
	iowrite32(rstr, hpbdev->reset_reg);
	spin_unlock(&hpbdev->reg_lock);
}

static void hpb_dmae_set_async_mode(struct hpb_dmae_device *hpbdev,
				    u32 mask, u32 data)
{
	u32 mode;

	spin_lock_irq(&hpbdev->reg_lock);
	mode = asyncmdr_read(hpbdev);
	mode &= ~mask;
	mode |= data;
	asyncmdr_write(hpbdev, mode);
	spin_unlock_irq(&hpbdev->reg_lock);
}

static void hpb_dmae_ctl_stop(struct hpb_dmae_device *hpbdev)
{
	dcmdr_write(hpbdev, HPB_DMAE_DCMDR_DQSPD);
}

static void hpb_dmae_reset(struct hpb_dmae_device *hpbdev)
{
	u32 ch;

	for (ch = 0; ch < hpbdev->pdata->num_hw_channels; ch++)
		hsrstr_write(hpbdev, ch);
}

static unsigned int calc_xmit_shift(struct hpb_dmae_chan *hpb_chan)
{
	struct hpb_dmae_device *hpbdev = to_dev(hpb_chan);
	struct hpb_dmae_pdata *pdata = hpbdev->pdata;
	int width = ch_reg_read(hpb_chan, HPB_DMAE_DCR);
	int i;

	switch (width & (HPB_DMAE_DCR_SPDS_MASK | HPB_DMAE_DCR_DPDS_MASK)) {
	case HPB_DMAE_DCR_SPDS_8BIT | HPB_DMAE_DCR_DPDS_8BIT:
	default:
		i = XMIT_SZ_8BIT;
		break;
	case HPB_DMAE_DCR_SPDS_16BIT | HPB_DMAE_DCR_DPDS_16BIT:
		i = XMIT_SZ_16BIT;
		break;
	case HPB_DMAE_DCR_SPDS_32BIT | HPB_DMAE_DCR_DPDS_32BIT:
		i = XMIT_SZ_32BIT;
		break;
	}
	return pdata->ts_shift[i];
}

static void hpb_dmae_set_reg(struct hpb_dmae_chan *hpb_chan,
			     struct hpb_dmae_regs *hw, unsigned plane)
{
	ch_reg_write(hpb_chan, hw->sar,
		     plane ? HPB_DMAE_DSAR1 : HPB_DMAE_DSAR0);
	ch_reg_write(hpb_chan, hw->dar,
		     plane ? HPB_DMAE_DDAR1 : HPB_DMAE_DDAR0);
	ch_reg_write(hpb_chan, hw->tcr >> hpb_chan->xmit_shift,
		     plane ? HPB_DMAE_DTCR1 : HPB_DMAE_DTCR0);
}

static void hpb_dmae_start(struct hpb_dmae_chan *hpb_chan, bool next)
{
	ch_reg_write(hpb_chan, (next ? HPB_DMAE_DCMDR_DNXT : 0) |
		     HPB_DMAE_DCMDR_DMEN, HPB_DMAE_DCMDR);
}

static void hpb_dmae_halt(struct shdma_chan *schan)
{
	struct hpb_dmae_chan *chan = to_chan(schan);

	ch_reg_write(chan, HPB_DMAE_DCMDR_DQEND, HPB_DMAE_DCMDR);
	ch_reg_write(chan, HPB_DMAE_DSTPR_DMSTP, HPB_DMAE_DSTPR);
}

static const struct hpb_dmae_slave_config *
hpb_dmae_find_slave(struct hpb_dmae_chan *hpb_chan, int slave_id)
{
	struct hpb_dmae_device *hpbdev = to_dev(hpb_chan);
	struct hpb_dmae_pdata *pdata = hpbdev->pdata;
	int i;

	if (slave_id >= HPB_DMA_SLAVE_NUMBER)
		return NULL;

	for (i = 0; i < pdata->num_slaves; i++)
		if (pdata->slaves[i].id == slave_id)
			return pdata->slaves + i;

	return NULL;
}

static void hpb_dmae_start_xfer(struct shdma_chan *schan,
				struct shdma_desc *sdesc)
{
	struct hpb_dmae_chan *chan = to_chan(schan);
	struct hpb_dmae_device *hpbdev = to_dev(chan);
	struct hpb_desc *desc = to_desc(sdesc);

	if (chan->cfg->flags & HPB_DMAE_SET_ASYNC_RESET)
		hpb_dmae_async_reset(hpbdev, chan->cfg->rstr);

	desc->plane_idx = chan->plane_idx;
	hpb_dmae_set_reg(chan, &desc->hw, chan->plane_idx);
	hpb_dmae_start(chan, !chan->first_desc);

	if (chan->xfer_mode == XFER_DOUBLE) {
		chan->plane_idx ^= 1;
		chan->first_desc = false;
	}
}

static bool hpb_dmae_desc_completed(struct shdma_chan *schan,
				    struct shdma_desc *sdesc)
{
	/*
	 * This is correct since we always have at most single
	 * outstanding DMA transfer per channel, and by the time
	 * we get completion interrupt the transfer is completed.
	 * This will change if we ever use alternating DMA
	 * information sets and submit two descriptors at once.
	 */
	return true;
}

static bool hpb_dmae_chan_irq(struct shdma_chan *schan, int irq)
{
	struct hpb_dmae_chan *chan = to_chan(schan);
	struct hpb_dmae_device *hpbdev = to_dev(chan);
	int ch = chan->cfg->dma_ch;

	/* Check Complete DMA Transfer */
	if (dintsr_read(hpbdev, ch)) {
		/* Clear Interrupt status */
		dintcr_write(hpbdev, ch);
		return true;
	}
	return false;
}

static int hpb_dmae_desc_setup(struct shdma_chan *schan,
			       struct shdma_desc *sdesc,
			       dma_addr_t src, dma_addr_t dst, size_t *len)
{
	struct hpb_desc *desc = to_desc(sdesc);

	if (*len > (size_t)HPB_DMA_TCR_MAX)
		*len = (size_t)HPB_DMA_TCR_MAX;

	desc->hw.sar = src;
	desc->hw.dar = dst;
	desc->hw.tcr = *len;

	return 0;
}

static size_t hpb_dmae_get_partial(struct shdma_chan *schan,
				   struct shdma_desc *sdesc)
{
	struct hpb_desc *desc = to_desc(sdesc);
	struct hpb_dmae_chan *chan = to_chan(schan);
	u32 tcr = ch_reg_read(chan, desc->plane_idx ?
			      HPB_DMAE_DTCR1 : HPB_DMAE_DTCR0);

	return (desc->hw.tcr - tcr) << chan->xmit_shift;
}

static bool hpb_dmae_channel_busy(struct shdma_chan *schan)
{
	struct hpb_dmae_chan *chan = to_chan(schan);
	u32 dstsr = ch_reg_read(chan, HPB_DMAE_DSTSR);

	return (dstsr & HPB_DMAE_DSTSR_DMSTS) == HPB_DMAE_DSTSR_DMSTS;
}

static int
hpb_dmae_alloc_chan_resources(struct hpb_dmae_chan *hpb_chan,
			      const struct hpb_dmae_slave_config *cfg)
{
	struct hpb_dmae_device *hpbdev = to_dev(hpb_chan);
	struct hpb_dmae_pdata *pdata = hpbdev->pdata;
	const struct hpb_dmae_channel *channel = pdata->channels;
	int slave_id = cfg->id;
	int i, err;

	for (i = 0; i < pdata->num_channels; i++, channel++) {
		if (channel->s_id == slave_id) {
			struct device *dev = hpb_chan->shdma_chan.dev;

			hpb_chan->base = hpbdev->chan_reg +
				HPB_DMAE_CHAN(cfg->dma_ch);

			dev_dbg(dev, "Detected Slave device\n");
			dev_dbg(dev, " -- slave_id       : 0x%x\n", slave_id);
			dev_dbg(dev, " -- cfg->dma_ch    : %d\n", cfg->dma_ch);
			dev_dbg(dev, " -- channel->ch_irq: %d\n",
				channel->ch_irq);
			break;
		}
	}

	err = shdma_request_irq(&hpb_chan->shdma_chan, channel->ch_irq,
				IRQF_SHARED, hpb_chan->dev_id);
	if (err) {
		dev_err(hpb_chan->shdma_chan.dev,
			"DMA channel request_irq %d failed with error %d\n",
			channel->ch_irq, err);
		return err;
	}

	hpb_chan->plane_idx = 0;
	hpb_chan->first_desc = true;

	if ((cfg->dcr & (HPB_DMAE_DCR_CT | HPB_DMAE_DCR_DIP)) == 0) {
		hpb_chan->xfer_mode = XFER_SINGLE;
	} else if ((cfg->dcr & (HPB_DMAE_DCR_CT | HPB_DMAE_DCR_DIP)) ==
		   (HPB_DMAE_DCR_CT | HPB_DMAE_DCR_DIP)) {
		hpb_chan->xfer_mode = XFER_DOUBLE;
	} else {
		dev_err(hpb_chan->shdma_chan.dev, "DCR setting error");
		return -EINVAL;
	}

	if (cfg->flags & HPB_DMAE_SET_ASYNC_MODE)
		hpb_dmae_set_async_mode(hpbdev, cfg->mdm, cfg->mdr);
	ch_reg_write(hpb_chan, cfg->dcr, HPB_DMAE_DCR);
	ch_reg_write(hpb_chan, cfg->port, HPB_DMAE_DPTR);
	hpb_chan->xmit_shift = calc_xmit_shift(hpb_chan);
	hpb_dmae_enable_int(hpbdev, cfg->dma_ch);

	return 0;
}

static int hpb_dmae_set_slave(struct shdma_chan *schan, int slave_id, bool try)
{
	struct hpb_dmae_chan *chan = to_chan(schan);
	const struct hpb_dmae_slave_config *sc =
		hpb_dmae_find_slave(chan, slave_id);

	if (!sc)
		return -ENODEV;
	if (try)
		return 0;
	chan->cfg = sc;
	return hpb_dmae_alloc_chan_resources(chan, sc);
}

static void hpb_dmae_setup_xfer(struct shdma_chan *schan, int slave_id)
{
}

static dma_addr_t hpb_dmae_slave_addr(struct shdma_chan *schan)
{
	struct hpb_dmae_chan *chan = to_chan(schan);

	return chan->cfg->addr;
}

static struct shdma_desc *hpb_dmae_embedded_desc(void *buf, int i)
{
	return &((struct hpb_desc *)buf)[i].shdma_desc;
}

static const struct shdma_ops hpb_dmae_ops = {
	.desc_completed = hpb_dmae_desc_completed,
	.halt_channel = hpb_dmae_halt,
	.channel_busy = hpb_dmae_channel_busy,
	.slave_addr = hpb_dmae_slave_addr,
	.desc_setup = hpb_dmae_desc_setup,
	.set_slave = hpb_dmae_set_slave,
	.setup_xfer = hpb_dmae_setup_xfer,
	.start_xfer = hpb_dmae_start_xfer,
	.embedded_desc = hpb_dmae_embedded_desc,
	.chan_irq = hpb_dmae_chan_irq,
	.get_partial = hpb_dmae_get_partial,
};

static int hpb_dmae_chan_probe(struct hpb_dmae_device *hpbdev, int id)
{
	struct shdma_dev *sdev = &hpbdev->shdma_dev;
	struct platform_device *pdev =
		to_platform_device(hpbdev->shdma_dev.dma_dev.dev);
	struct hpb_dmae_chan *new_hpb_chan;
	struct shdma_chan *schan;

	/* Alloc channel */
	new_hpb_chan = devm_kzalloc(&pdev->dev,
				    sizeof(struct hpb_dmae_chan), GFP_KERNEL);
	if (!new_hpb_chan) {
		dev_err(hpbdev->shdma_dev.dma_dev.dev,
			"No free memory for allocating DMA channels!\n");
		return -ENOMEM;
	}

	schan = &new_hpb_chan->shdma_chan;
	shdma_chan_probe(sdev, schan, id);

	if (pdev->id >= 0)
		snprintf(new_hpb_chan->dev_id, sizeof(new_hpb_chan->dev_id),
			 "hpb-dmae%d.%d", pdev->id, id);
	else
		snprintf(new_hpb_chan->dev_id, sizeof(new_hpb_chan->dev_id),
			 "hpb-dma.%d", id);

	return 0;
}

static int hpb_dmae_probe(struct platform_device *pdev)
{
	struct hpb_dmae_pdata *pdata = pdev->dev.platform_data;
	struct hpb_dmae_device *hpbdev;
	struct dma_device *dma_dev;
	struct resource *chan, *comm, *rest, *mode, *irq_res;
	int err, i;

	/* Get platform data */
	if (!pdata || !pdata->num_channels)
		return -ENODEV;

	chan = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	comm = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	rest = platform_get_resource(pdev, IORESOURCE_MEM, 2);
	mode = platform_get_resource(pdev, IORESOURCE_MEM, 3);

	irq_res = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (!irq_res)
		return -ENODEV;

	hpbdev = devm_kzalloc(&pdev->dev, sizeof(struct hpb_dmae_device),
			      GFP_KERNEL);
	if (!hpbdev) {
		dev_err(&pdev->dev, "Not enough memory\n");
		return -ENOMEM;
	}

	hpbdev->chan_reg = devm_ioremap_resource(&pdev->dev, chan);
	if (IS_ERR(hpbdev->chan_reg))
		return PTR_ERR(hpbdev->chan_reg);

	hpbdev->comm_reg = devm_ioremap_resource(&pdev->dev, comm);
	if (IS_ERR(hpbdev->comm_reg))
		return PTR_ERR(hpbdev->comm_reg);

	hpbdev->reset_reg = devm_ioremap_resource(&pdev->dev, rest);
	if (IS_ERR(hpbdev->reset_reg))
		return PTR_ERR(hpbdev->reset_reg);

	hpbdev->mode_reg = devm_ioremap_resource(&pdev->dev, mode);
	if (IS_ERR(hpbdev->mode_reg))
		return PTR_ERR(hpbdev->mode_reg);

	dma_dev = &hpbdev->shdma_dev.dma_dev;

	spin_lock_init(&hpbdev->reg_lock);

	/* Platform data */
	hpbdev->pdata = pdata;

	pm_runtime_enable(&pdev->dev);
	err = pm_runtime_get_sync(&pdev->dev);
	if (err < 0)
		dev_err(&pdev->dev, "%s(): GET = %d\n", __func__, err);

	/* Reset DMA controller */
	hpb_dmae_reset(hpbdev);

	pm_runtime_put(&pdev->dev);

	dma_cap_set(DMA_MEMCPY, dma_dev->cap_mask);
	dma_cap_set(DMA_SLAVE, dma_dev->cap_mask);

	hpbdev->shdma_dev.ops = &hpb_dmae_ops;
	hpbdev->shdma_dev.desc_size = sizeof(struct hpb_desc);
	err = shdma_init(&pdev->dev, &hpbdev->shdma_dev, pdata->num_channels);
	if (err < 0)
		goto error;

	/* Create DMA channels */
	for (i = 0; i < pdata->num_channels; i++)
		hpb_dmae_chan_probe(hpbdev, i);

	platform_set_drvdata(pdev, hpbdev);
	err = dma_async_device_register(dma_dev);
	if (!err)
		return 0;

	shdma_cleanup(&hpbdev->shdma_dev);
error:
	pm_runtime_disable(&pdev->dev);
	return err;
}

static void hpb_dmae_chan_remove(struct hpb_dmae_device *hpbdev)
{
	struct dma_device *dma_dev = &hpbdev->shdma_dev.dma_dev;
	struct shdma_chan *schan;
	int i;

	shdma_for_each_chan(schan, &hpbdev->shdma_dev, i) {
		BUG_ON(!schan);

		shdma_chan_remove(schan);
	}
	dma_dev->chancnt = 0;
}

static int hpb_dmae_remove(struct platform_device *pdev)
{
	struct hpb_dmae_device *hpbdev = platform_get_drvdata(pdev);

	dma_async_device_unregister(&hpbdev->shdma_dev.dma_dev);

	pm_runtime_disable(&pdev->dev);

	hpb_dmae_chan_remove(hpbdev);

	return 0;
}

static void hpb_dmae_shutdown(struct platform_device *pdev)
{
	struct hpb_dmae_device *hpbdev = platform_get_drvdata(pdev);
	hpb_dmae_ctl_stop(hpbdev);
}

static struct platform_driver hpb_dmae_driver = {
	.probe		= hpb_dmae_probe,
	.remove		= hpb_dmae_remove,
	.shutdown	= hpb_dmae_shutdown,
	.driver = {
		.owner	= THIS_MODULE,
		.name	= "hpb-dma-engine",
	},
};
module_platform_driver(hpb_dmae_driver);

MODULE_AUTHOR("Max Filippov <max.filippov@cogentembedded.com>");
MODULE_DESCRIPTION("Renesas HPB DMA Engine driver");
MODULE_LICENSE("GPL");

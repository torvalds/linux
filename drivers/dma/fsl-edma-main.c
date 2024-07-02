// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * drivers/dma/fsl-edma.c
 *
 * Copyright 2013-2014 Freescale Semiconductor, Inc.
 *
 * Driver for the Freescale eDMA engine with flexible channel multiplexing
 * capability for DMA request sources. The eDMA block can be found on some
 * Vybrid and Layerscape SoCs.
 */

#include <dt-bindings/dma/fsl-edma.h>
#include <linux/bitfield.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/clk.h>
#include <linux/of.h>
#include <linux/of_dma.h>
#include <linux/dma-mapping.h>
#include <linux/pm_runtime.h>
#include <linux/pm_domain.h>
#include <linux/property.h>

#include "fsl-edma-common.h"

static void fsl_edma_synchronize(struct dma_chan *chan)
{
	struct fsl_edma_chan *fsl_chan = to_fsl_edma_chan(chan);

	vchan_synchronize(&fsl_chan->vchan);
}

static irqreturn_t fsl_edma_tx_handler(int irq, void *dev_id)
{
	struct fsl_edma_engine *fsl_edma = dev_id;
	unsigned int intr, ch;
	struct edma_regs *regs = &fsl_edma->regs;

	intr = edma_readl(fsl_edma, regs->intl);
	if (!intr)
		return IRQ_NONE;

	for (ch = 0; ch < fsl_edma->n_chans; ch++) {
		if (intr & (0x1 << ch)) {
			edma_writeb(fsl_edma, EDMA_CINT_CINT(ch), regs->cint);
			fsl_edma_tx_chan_handler(&fsl_edma->chans[ch]);
		}
	}
	return IRQ_HANDLED;
}

static irqreturn_t fsl_edma3_tx_handler(int irq, void *dev_id)
{
	struct fsl_edma_chan *fsl_chan = dev_id;
	unsigned int intr;

	intr = edma_readl_chreg(fsl_chan, ch_int);
	if (!intr)
		return IRQ_HANDLED;

	edma_writel_chreg(fsl_chan, 1, ch_int);

	fsl_edma_tx_chan_handler(fsl_chan);

	return IRQ_HANDLED;
}

static irqreturn_t fsl_edma_err_handler(int irq, void *dev_id)
{
	struct fsl_edma_engine *fsl_edma = dev_id;
	unsigned int err, ch;
	struct edma_regs *regs = &fsl_edma->regs;

	err = edma_readl(fsl_edma, regs->errl);
	if (!err)
		return IRQ_NONE;

	for (ch = 0; ch < fsl_edma->n_chans; ch++) {
		if (err & (0x1 << ch)) {
			fsl_edma_disable_request(&fsl_edma->chans[ch]);
			edma_writeb(fsl_edma, EDMA_CERR_CERR(ch), regs->cerr);
			fsl_edma_err_chan_handler(&fsl_edma->chans[ch]);
		}
	}
	return IRQ_HANDLED;
}

static irqreturn_t fsl_edma_irq_handler(int irq, void *dev_id)
{
	if (fsl_edma_tx_handler(irq, dev_id) == IRQ_HANDLED)
		return IRQ_HANDLED;

	return fsl_edma_err_handler(irq, dev_id);
}

static struct dma_chan *fsl_edma_xlate(struct of_phandle_args *dma_spec,
		struct of_dma *ofdma)
{
	struct fsl_edma_engine *fsl_edma = ofdma->of_dma_data;
	struct dma_chan *chan, *_chan;
	struct fsl_edma_chan *fsl_chan;
	u32 dmamux_nr = fsl_edma->drvdata->dmamuxs;
	unsigned long chans_per_mux = fsl_edma->n_chans / dmamux_nr;

	if (dma_spec->args_count != 2)
		return NULL;

	mutex_lock(&fsl_edma->fsl_edma_mutex);
	list_for_each_entry_safe(chan, _chan, &fsl_edma->dma_dev.channels, device_node) {
		if (chan->client_count)
			continue;
		if ((chan->chan_id / chans_per_mux) == dma_spec->args[0]) {
			chan = dma_get_slave_channel(chan);
			if (chan) {
				chan->device->privatecnt++;
				fsl_chan = to_fsl_edma_chan(chan);
				fsl_chan->slave_id = dma_spec->args[1];
				fsl_edma_chan_mux(fsl_chan, fsl_chan->slave_id,
						true);
				mutex_unlock(&fsl_edma->fsl_edma_mutex);
				return chan;
			}
		}
	}
	mutex_unlock(&fsl_edma->fsl_edma_mutex);
	return NULL;
}

static struct dma_chan *fsl_edma3_xlate(struct of_phandle_args *dma_spec,
					struct of_dma *ofdma)
{
	struct fsl_edma_engine *fsl_edma = ofdma->of_dma_data;
	struct dma_chan *chan, *_chan;
	struct fsl_edma_chan *fsl_chan;
	bool b_chmux;
	int i;

	if (dma_spec->args_count != 3)
		return NULL;

	b_chmux = !!(fsl_edma->drvdata->flags & FSL_EDMA_DRV_HAS_CHMUX);

	mutex_lock(&fsl_edma->fsl_edma_mutex);
	list_for_each_entry_safe(chan, _chan, &fsl_edma->dma_dev.channels,
					device_node) {

		if (chan->client_count)
			continue;

		fsl_chan = to_fsl_edma_chan(chan);
		i = fsl_chan - fsl_edma->chans;

		fsl_chan->priority = dma_spec->args[1];
		fsl_chan->is_rxchan = dma_spec->args[2] & FSL_EDMA_RX;
		fsl_chan->is_remote = dma_spec->args[2] & FSL_EDMA_REMOTE;
		fsl_chan->is_multi_fifo = dma_spec->args[2] & FSL_EDMA_MULTI_FIFO;

		if ((dma_spec->args[2] & FSL_EDMA_EVEN_CH) && (i & 0x1))
			continue;

		if ((dma_spec->args[2] & FSL_EDMA_ODD_CH) && !(i & 0x1))
			continue;

		if (!b_chmux && i == dma_spec->args[0]) {
			chan = dma_get_slave_channel(chan);
			chan->device->privatecnt++;
			mutex_unlock(&fsl_edma->fsl_edma_mutex);
			return chan;
		} else if (b_chmux && !fsl_chan->srcid) {
			/* if controller support channel mux, choose a free channel */
			chan = dma_get_slave_channel(chan);
			chan->device->privatecnt++;
			fsl_chan->srcid = dma_spec->args[0];
			mutex_unlock(&fsl_edma->fsl_edma_mutex);
			return chan;
		}
	}
	mutex_unlock(&fsl_edma->fsl_edma_mutex);
	return NULL;
}

static int
fsl_edma_irq_init(struct platform_device *pdev, struct fsl_edma_engine *fsl_edma)
{
	int ret;

	edma_writel(fsl_edma, ~0, fsl_edma->regs.intl);

	fsl_edma->txirq = platform_get_irq_byname(pdev, "edma-tx");
	if (fsl_edma->txirq < 0)
		return fsl_edma->txirq;

	fsl_edma->errirq = platform_get_irq_byname(pdev, "edma-err");
	if (fsl_edma->errirq < 0)
		return fsl_edma->errirq;

	if (fsl_edma->txirq == fsl_edma->errirq) {
		ret = devm_request_irq(&pdev->dev, fsl_edma->txirq,
				fsl_edma_irq_handler, 0, "eDMA", fsl_edma);
		if (ret) {
			dev_err(&pdev->dev, "Can't register eDMA IRQ.\n");
			return ret;
		}
	} else {
		ret = devm_request_irq(&pdev->dev, fsl_edma->txirq,
				fsl_edma_tx_handler, 0, "eDMA tx", fsl_edma);
		if (ret) {
			dev_err(&pdev->dev, "Can't register eDMA tx IRQ.\n");
			return ret;
		}

		ret = devm_request_irq(&pdev->dev, fsl_edma->errirq,
				fsl_edma_err_handler, 0, "eDMA err", fsl_edma);
		if (ret) {
			dev_err(&pdev->dev, "Can't register eDMA err IRQ.\n");
			return ret;
		}
	}

	return 0;
}

static int fsl_edma3_irq_init(struct platform_device *pdev, struct fsl_edma_engine *fsl_edma)
{
	int ret;
	int i;

	for (i = 0; i < fsl_edma->n_chans; i++) {

		struct fsl_edma_chan *fsl_chan = &fsl_edma->chans[i];

		if (fsl_edma->chan_masked & BIT(i))
			continue;

		/* request channel irq */
		fsl_chan->txirq = platform_get_irq(pdev, i);
		if (fsl_chan->txirq < 0)
			return  -EINVAL;

		ret = devm_request_irq(&pdev->dev, fsl_chan->txirq,
			fsl_edma3_tx_handler, IRQF_SHARED,
			fsl_chan->chan_name, fsl_chan);
		if (ret) {
			dev_err(&pdev->dev, "Can't register chan%d's IRQ.\n", i);
			return -EINVAL;
		}
	}

	return 0;
}

static int
fsl_edma2_irq_init(struct platform_device *pdev,
		   struct fsl_edma_engine *fsl_edma)
{
	int i, ret, irq;
	int count;

	edma_writel(fsl_edma, ~0, fsl_edma->regs.intl);

	count = platform_irq_count(pdev);
	dev_dbg(&pdev->dev, "%s Found %d interrupts\r\n", __func__, count);
	if (count <= 2) {
		dev_err(&pdev->dev, "Interrupts in DTS not correct.\n");
		return -EINVAL;
	}
	/*
	 * 16 channel independent interrupts + 1 error interrupt on i.mx7ulp.
	 * 2 channel share one interrupt, for example, ch0/ch16, ch1/ch17...
	 * For now, just simply request irq without IRQF_SHARED flag, since 16
	 * channels are enough on i.mx7ulp whose M4 domain own some peripherals.
	 */
	for (i = 0; i < count; i++) {
		irq = platform_get_irq(pdev, i);
		if (irq < 0)
			return -ENXIO;

		/* The last IRQ is for eDMA err */
		if (i == count - 1)
			ret = devm_request_irq(&pdev->dev, irq,
						fsl_edma_err_handler,
						0, "eDMA2-ERR", fsl_edma);
		else
			ret = devm_request_irq(&pdev->dev, irq,
						fsl_edma_tx_handler, 0,
						fsl_edma->chans[i].chan_name,
						fsl_edma);
		if (ret)
			return ret;
	}

	return 0;
}

static void fsl_edma_irq_exit(
		struct platform_device *pdev, struct fsl_edma_engine *fsl_edma)
{
	if (fsl_edma->txirq == fsl_edma->errirq) {
		devm_free_irq(&pdev->dev, fsl_edma->txirq, fsl_edma);
	} else {
		devm_free_irq(&pdev->dev, fsl_edma->txirq, fsl_edma);
		devm_free_irq(&pdev->dev, fsl_edma->errirq, fsl_edma);
	}
}

static void fsl_disable_clocks(struct fsl_edma_engine *fsl_edma, int nr_clocks)
{
	int i;

	for (i = 0; i < nr_clocks; i++)
		clk_disable_unprepare(fsl_edma->muxclk[i]);
}

static struct fsl_edma_drvdata vf610_data = {
	.dmamuxs = DMAMUX_NR,
	.flags = FSL_EDMA_DRV_WRAP_IO,
	.chreg_off = EDMA_TCD,
	.chreg_space_sz = sizeof(struct fsl_edma_hw_tcd),
	.setup_irq = fsl_edma_irq_init,
};

static struct fsl_edma_drvdata ls1028a_data = {
	.dmamuxs = DMAMUX_NR,
	.flags = FSL_EDMA_DRV_MUX_SWAP | FSL_EDMA_DRV_WRAP_IO,
	.chreg_off = EDMA_TCD,
	.chreg_space_sz = sizeof(struct fsl_edma_hw_tcd),
	.setup_irq = fsl_edma_irq_init,
};

static struct fsl_edma_drvdata imx7ulp_data = {
	.dmamuxs = 1,
	.chreg_off = EDMA_TCD,
	.chreg_space_sz = sizeof(struct fsl_edma_hw_tcd),
	.flags = FSL_EDMA_DRV_HAS_DMACLK | FSL_EDMA_DRV_CONFIG32,
	.setup_irq = fsl_edma2_irq_init,
};

static struct fsl_edma_drvdata imx8qm_data = {
	.flags = FSL_EDMA_DRV_HAS_PD | FSL_EDMA_DRV_EDMA3,
	.chreg_space_sz = 0x10000,
	.chreg_off = 0x10000,
	.setup_irq = fsl_edma3_irq_init,
};

static struct fsl_edma_drvdata imx8qm_audio_data = {
	.flags = FSL_EDMA_DRV_QUIRK_SWAPPED | FSL_EDMA_DRV_HAS_PD | FSL_EDMA_DRV_EDMA3,
	.chreg_space_sz = 0x10000,
	.chreg_off = 0x10000,
	.setup_irq = fsl_edma3_irq_init,
};

static struct fsl_edma_drvdata imx93_data3 = {
	.flags = FSL_EDMA_DRV_HAS_DMACLK | FSL_EDMA_DRV_EDMA3,
	.chreg_space_sz = 0x10000,
	.chreg_off = 0x10000,
	.setup_irq = fsl_edma3_irq_init,
};

static struct fsl_edma_drvdata imx93_data4 = {
	.flags = FSL_EDMA_DRV_HAS_CHMUX | FSL_EDMA_DRV_HAS_DMACLK | FSL_EDMA_DRV_EDMA4,
	.chreg_space_sz = 0x8000,
	.chreg_off = 0x10000,
	.mux_off = 0x10000 + offsetof(struct fsl_edma3_ch_reg, ch_mux),
	.mux_skip = 0x8000,
	.setup_irq = fsl_edma3_irq_init,
};

static struct fsl_edma_drvdata imx95_data5 = {
	.flags = FSL_EDMA_DRV_HAS_CHMUX | FSL_EDMA_DRV_HAS_DMACLK | FSL_EDMA_DRV_EDMA4 |
		 FSL_EDMA_DRV_TCD64,
	.chreg_space_sz = 0x8000,
	.chreg_off = 0x10000,
	.mux_off = 0x200,
	.mux_skip = sizeof(u32),
	.setup_irq = fsl_edma3_irq_init,
};

static const struct of_device_id fsl_edma_dt_ids[] = {
	{ .compatible = "fsl,vf610-edma", .data = &vf610_data},
	{ .compatible = "fsl,ls1028a-edma", .data = &ls1028a_data},
	{ .compatible = "fsl,imx7ulp-edma", .data = &imx7ulp_data},
	{ .compatible = "fsl,imx8qm-edma", .data = &imx8qm_data},
	{ .compatible = "fsl,imx8qm-adma", .data = &imx8qm_audio_data},
	{ .compatible = "fsl,imx93-edma3", .data = &imx93_data3},
	{ .compatible = "fsl,imx93-edma4", .data = &imx93_data4},
	{ .compatible = "fsl,imx95-edma5", .data = &imx95_data5},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, fsl_edma_dt_ids);

static int fsl_edma3_attach_pd(struct platform_device *pdev, struct fsl_edma_engine *fsl_edma)
{
	struct fsl_edma_chan *fsl_chan;
	struct device_link *link;
	struct device *pd_chan;
	struct device *dev;
	int i;

	dev = &pdev->dev;

	for (i = 0; i < fsl_edma->n_chans; i++) {
		if (fsl_edma->chan_masked & BIT(i))
			continue;

		fsl_chan = &fsl_edma->chans[i];

		pd_chan = dev_pm_domain_attach_by_id(dev, i);
		if (IS_ERR_OR_NULL(pd_chan)) {
			dev_err(dev, "Failed attach pd %d\n", i);
			return -EINVAL;
		}

		link = device_link_add(dev, pd_chan, DL_FLAG_STATELESS |
					     DL_FLAG_PM_RUNTIME |
					     DL_FLAG_RPM_ACTIVE);
		if (!link) {
			dev_err(dev, "Failed to add device_link to %d\n", i);
			return -EINVAL;
		}

		fsl_chan->pd_dev = pd_chan;

		pm_runtime_use_autosuspend(fsl_chan->pd_dev);
		pm_runtime_set_autosuspend_delay(fsl_chan->pd_dev, 200);
		pm_runtime_set_active(fsl_chan->pd_dev);
	}

	return 0;
}

static int fsl_edma_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct fsl_edma_engine *fsl_edma;
	const struct fsl_edma_drvdata *drvdata = NULL;
	u32 chan_mask[2] = {0, 0};
	struct edma_regs *regs;
	int chans;
	int ret, i;

	drvdata = device_get_match_data(&pdev->dev);
	if (!drvdata) {
		dev_err(&pdev->dev, "unable to find driver data\n");
		return -EINVAL;
	}

	ret = of_property_read_u32(np, "dma-channels", &chans);
	if (ret) {
		dev_err(&pdev->dev, "Can't get dma-channels.\n");
		return ret;
	}

	fsl_edma = devm_kzalloc(&pdev->dev, struct_size(fsl_edma, chans, chans),
				GFP_KERNEL);
	if (!fsl_edma)
		return -ENOMEM;

	fsl_edma->drvdata = drvdata;
	fsl_edma->n_chans = chans;
	mutex_init(&fsl_edma->fsl_edma_mutex);

	fsl_edma->membase = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(fsl_edma->membase))
		return PTR_ERR(fsl_edma->membase);

	if (!(drvdata->flags & FSL_EDMA_DRV_SPLIT_REG)) {
		fsl_edma_setup_regs(fsl_edma);
		regs = &fsl_edma->regs;
	}

	if (drvdata->flags & FSL_EDMA_DRV_HAS_DMACLK) {
		fsl_edma->dmaclk = devm_clk_get_enabled(&pdev->dev, "dma");
		if (IS_ERR(fsl_edma->dmaclk)) {
			dev_err(&pdev->dev, "Missing DMA block clock.\n");
			return PTR_ERR(fsl_edma->dmaclk);
		}
	}

	if (drvdata->flags & FSL_EDMA_DRV_HAS_CHCLK) {
		fsl_edma->chclk = devm_clk_get_enabled(&pdev->dev, "mp");
		if (IS_ERR(fsl_edma->chclk)) {
			dev_err(&pdev->dev, "Missing MP block clock.\n");
			return PTR_ERR(fsl_edma->chclk);
		}
	}

	ret = of_property_read_variable_u32_array(np, "dma-channel-mask", chan_mask, 1, 2);

	if (ret > 0) {
		fsl_edma->chan_masked = chan_mask[1];
		fsl_edma->chan_masked <<= 32;
		fsl_edma->chan_masked |= chan_mask[0];
	}

	for (i = 0; i < fsl_edma->drvdata->dmamuxs; i++) {
		char clkname[32];

		/* eDMAv3 mux register move to TCD area if ch_mux exist */
		if (drvdata->flags & FSL_EDMA_DRV_SPLIT_REG)
			break;

		fsl_edma->muxbase[i] = devm_platform_ioremap_resource(pdev,
								      1 + i);
		if (IS_ERR(fsl_edma->muxbase[i])) {
			/* on error: disable all previously enabled clks */
			fsl_disable_clocks(fsl_edma, i);
			return PTR_ERR(fsl_edma->muxbase[i]);
		}

		sprintf(clkname, "dmamux%d", i);
		fsl_edma->muxclk[i] = devm_clk_get_enabled(&pdev->dev, clkname);
		if (IS_ERR(fsl_edma->muxclk[i])) {
			dev_err(&pdev->dev, "Missing DMAMUX block clock.\n");
			/* on error: disable all previously enabled clks */
			return PTR_ERR(fsl_edma->muxclk[i]);
		}
	}

	fsl_edma->big_endian = of_property_read_bool(np, "big-endian");

	if (drvdata->flags & FSL_EDMA_DRV_HAS_PD) {
		ret = fsl_edma3_attach_pd(pdev, fsl_edma);
		if (ret)
			return ret;
	}

	if (drvdata->flags & FSL_EDMA_DRV_TCD64)
		dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64));

	INIT_LIST_HEAD(&fsl_edma->dma_dev.channels);
	for (i = 0; i < fsl_edma->n_chans; i++) {
		struct fsl_edma_chan *fsl_chan = &fsl_edma->chans[i];
		int len;

		if (fsl_edma->chan_masked & BIT(i))
			continue;

		snprintf(fsl_chan->chan_name, sizeof(fsl_chan->chan_name), "%s-CH%02d",
							   dev_name(&pdev->dev), i);

		fsl_chan->edma = fsl_edma;
		fsl_chan->pm_state = RUNNING;
		fsl_chan->slave_id = 0;
		fsl_chan->idle = true;
		fsl_chan->dma_dir = DMA_NONE;
		fsl_chan->vchan.desc_free = fsl_edma_free_desc;

		len = (drvdata->flags & FSL_EDMA_DRV_SPLIT_REG) ?
				offsetof(struct fsl_edma3_ch_reg, tcd) : 0;
		fsl_chan->tcd = fsl_edma->membase
				+ i * drvdata->chreg_space_sz + drvdata->chreg_off + len;
		fsl_chan->mux_addr = fsl_edma->membase + drvdata->mux_off + i * drvdata->mux_skip;

		fsl_chan->pdev = pdev;
		vchan_init(&fsl_chan->vchan, &fsl_edma->dma_dev);

		edma_write_tcdreg(fsl_chan, cpu_to_le32(0), csr);
		fsl_edma_chan_mux(fsl_chan, 0, false);
	}

	ret = fsl_edma->drvdata->setup_irq(pdev, fsl_edma);
	if (ret)
		return ret;

	dma_cap_set(DMA_PRIVATE, fsl_edma->dma_dev.cap_mask);
	dma_cap_set(DMA_SLAVE, fsl_edma->dma_dev.cap_mask);
	dma_cap_set(DMA_CYCLIC, fsl_edma->dma_dev.cap_mask);
	dma_cap_set(DMA_MEMCPY, fsl_edma->dma_dev.cap_mask);

	fsl_edma->dma_dev.dev = &pdev->dev;
	fsl_edma->dma_dev.device_alloc_chan_resources
		= fsl_edma_alloc_chan_resources;
	fsl_edma->dma_dev.device_free_chan_resources
		= fsl_edma_free_chan_resources;
	fsl_edma->dma_dev.device_tx_status = fsl_edma_tx_status;
	fsl_edma->dma_dev.device_prep_slave_sg = fsl_edma_prep_slave_sg;
	fsl_edma->dma_dev.device_prep_dma_cyclic = fsl_edma_prep_dma_cyclic;
	fsl_edma->dma_dev.device_prep_dma_memcpy = fsl_edma_prep_memcpy;
	fsl_edma->dma_dev.device_config = fsl_edma_slave_config;
	fsl_edma->dma_dev.device_pause = fsl_edma_pause;
	fsl_edma->dma_dev.device_resume = fsl_edma_resume;
	fsl_edma->dma_dev.device_terminate_all = fsl_edma_terminate_all;
	fsl_edma->dma_dev.device_synchronize = fsl_edma_synchronize;
	fsl_edma->dma_dev.device_issue_pending = fsl_edma_issue_pending;

	fsl_edma->dma_dev.src_addr_widths = FSL_EDMA_BUSWIDTHS;
	fsl_edma->dma_dev.dst_addr_widths = FSL_EDMA_BUSWIDTHS;

	if (drvdata->flags & FSL_EDMA_DRV_BUS_8BYTE) {
		fsl_edma->dma_dev.src_addr_widths |= BIT(DMA_SLAVE_BUSWIDTH_8_BYTES);
		fsl_edma->dma_dev.dst_addr_widths |= BIT(DMA_SLAVE_BUSWIDTH_8_BYTES);
	}

	fsl_edma->dma_dev.directions = BIT(DMA_DEV_TO_MEM) | BIT(DMA_MEM_TO_DEV);
	if (drvdata->flags & FSL_EDMA_DRV_DEV_TO_DEV)
		fsl_edma->dma_dev.directions |= BIT(DMA_DEV_TO_DEV);

	fsl_edma->dma_dev.copy_align = drvdata->flags & FSL_EDMA_DRV_ALIGN_64BYTE ?
					DMAENGINE_ALIGN_64_BYTES :
					DMAENGINE_ALIGN_32_BYTES;

	/* Per worst case 'nbytes = 1' take CITER as the max_seg_size */
	dma_set_max_seg_size(fsl_edma->dma_dev.dev,
			     FIELD_GET(EDMA_TCD_ITER_MASK, EDMA_TCD_ITER_MASK));

	fsl_edma->dma_dev.residue_granularity = DMA_RESIDUE_GRANULARITY_SEGMENT;

	platform_set_drvdata(pdev, fsl_edma);

	ret = dma_async_device_register(&fsl_edma->dma_dev);
	if (ret) {
		dev_err(&pdev->dev,
			"Can't register Freescale eDMA engine. (%d)\n", ret);
		return ret;
	}

	ret = of_dma_controller_register(np,
			drvdata->flags & FSL_EDMA_DRV_SPLIT_REG ? fsl_edma3_xlate : fsl_edma_xlate,
			fsl_edma);
	if (ret) {
		dev_err(&pdev->dev,
			"Can't register Freescale eDMA of_dma. (%d)\n", ret);
		dma_async_device_unregister(&fsl_edma->dma_dev);
		return ret;
	}

	/* enable round robin arbitration */
	if (!(drvdata->flags & FSL_EDMA_DRV_SPLIT_REG))
		edma_writel(fsl_edma, EDMA_CR_ERGA | EDMA_CR_ERCA, regs->cr);

	return 0;
}

static void fsl_edma_remove(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct fsl_edma_engine *fsl_edma = platform_get_drvdata(pdev);

	fsl_edma_irq_exit(pdev, fsl_edma);
	fsl_edma_cleanup_vchan(&fsl_edma->dma_dev);
	of_dma_controller_free(np);
	dma_async_device_unregister(&fsl_edma->dma_dev);
	fsl_disable_clocks(fsl_edma, fsl_edma->drvdata->dmamuxs);
}

static int fsl_edma_suspend_late(struct device *dev)
{
	struct fsl_edma_engine *fsl_edma = dev_get_drvdata(dev);
	struct fsl_edma_chan *fsl_chan;
	unsigned long flags;
	int i;

	for (i = 0; i < fsl_edma->n_chans; i++) {
		fsl_chan = &fsl_edma->chans[i];
		if (fsl_edma->chan_masked & BIT(i))
			continue;
		spin_lock_irqsave(&fsl_chan->vchan.lock, flags);
		/* Make sure chan is idle or will force disable. */
		if (unlikely(!fsl_chan->idle)) {
			dev_warn(dev, "WARN: There is non-idle channel.");
			fsl_edma_disable_request(fsl_chan);
			fsl_edma_chan_mux(fsl_chan, 0, false);
		}

		fsl_chan->pm_state = SUSPENDED;
		spin_unlock_irqrestore(&fsl_chan->vchan.lock, flags);
	}

	return 0;
}

static int fsl_edma_resume_early(struct device *dev)
{
	struct fsl_edma_engine *fsl_edma = dev_get_drvdata(dev);
	struct fsl_edma_chan *fsl_chan;
	struct edma_regs *regs = &fsl_edma->regs;
	int i;

	for (i = 0; i < fsl_edma->n_chans; i++) {
		fsl_chan = &fsl_edma->chans[i];
		if (fsl_edma->chan_masked & BIT(i))
			continue;
		fsl_chan->pm_state = RUNNING;
		edma_write_tcdreg(fsl_chan, 0, csr);
		if (fsl_chan->slave_id != 0)
			fsl_edma_chan_mux(fsl_chan, fsl_chan->slave_id, true);
	}

	if (!(fsl_edma->drvdata->flags & FSL_EDMA_DRV_SPLIT_REG))
		edma_writel(fsl_edma, EDMA_CR_ERGA | EDMA_CR_ERCA, regs->cr);

	return 0;
}

/*
 * eDMA provides the service to others, so it should be suspend late
 * and resume early. When eDMA suspend, all of the clients should stop
 * the DMA data transmission and let the channel idle.
 */
static const struct dev_pm_ops fsl_edma_pm_ops = {
	.suspend_late   = fsl_edma_suspend_late,
	.resume_early   = fsl_edma_resume_early,
};

static struct platform_driver fsl_edma_driver = {
	.driver		= {
		.name	= "fsl-edma",
		.of_match_table = fsl_edma_dt_ids,
		.pm     = &fsl_edma_pm_ops,
	},
	.probe          = fsl_edma_probe,
	.remove_new	= fsl_edma_remove,
};

static int __init fsl_edma_init(void)
{
	return platform_driver_register(&fsl_edma_driver);
}
subsys_initcall(fsl_edma_init);

static void __exit fsl_edma_exit(void)
{
	platform_driver_unregister(&fsl_edma_driver);
}
module_exit(fsl_edma_exit);

MODULE_ALIAS("platform:fsl-edma");
MODULE_DESCRIPTION("Freescale eDMA engine driver");
MODULE_LICENSE("GPL v2");

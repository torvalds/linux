// SPDX-License-Identifier: GPL-2.0
/*
 * sl3516-ce-core.c - hardware cryptographic offloader for Storlink SL3516 SoC
 *
 * Copyright (C) 2021 Corentin Labbe <clabbe@baylibre.com>
 *
 * Core file which registers crypto algorithms supported by the CryptoEngine
 */

#include <crypto/engine.h>
#include <crypto/internal/rng.h>
#include <crypto/internal/skcipher.h>
#include <linux/clk.h>
#include <linux/debugfs.h>
#include <linux/dev_printk.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>

#include "sl3516-ce.h"

static int sl3516_ce_desc_init(struct sl3516_ce_dev *ce)
{
	const size_t sz = sizeof(struct descriptor) * MAXDESC;
	int i;

	ce->tx = dma_alloc_coherent(ce->dev, sz, &ce->dtx, GFP_KERNEL);
	if (!ce->tx)
		return -ENOMEM;
	ce->rx = dma_alloc_coherent(ce->dev, sz, &ce->drx, GFP_KERNEL);
	if (!ce->rx)
		goto err_rx;

	for (i = 0; i < MAXDESC; i++) {
		ce->tx[i].frame_ctrl.bits.own = CE_CPU;
		ce->tx[i].next_desc.next_descriptor = ce->dtx + (i + 1) * sizeof(struct descriptor);
	}
	ce->tx[MAXDESC - 1].next_desc.next_descriptor = ce->dtx;

	for (i = 0; i < MAXDESC; i++) {
		ce->rx[i].frame_ctrl.bits.own = CE_CPU;
		ce->rx[i].next_desc.next_descriptor = ce->drx + (i + 1) * sizeof(struct descriptor);
	}
	ce->rx[MAXDESC - 1].next_desc.next_descriptor = ce->drx;

	ce->pctrl = dma_alloc_coherent(ce->dev, sizeof(struct pkt_control_ecb),
				       &ce->dctrl, GFP_KERNEL);
	if (!ce->pctrl)
		goto err_pctrl;

	return 0;
err_pctrl:
	dma_free_coherent(ce->dev, sz, ce->rx, ce->drx);
err_rx:
	dma_free_coherent(ce->dev, sz, ce->tx, ce->dtx);
	return -ENOMEM;
}

static void sl3516_ce_free_descs(struct sl3516_ce_dev *ce)
{
	const size_t sz = sizeof(struct descriptor) * MAXDESC;

	dma_free_coherent(ce->dev, sz, ce->tx, ce->dtx);
	dma_free_coherent(ce->dev, sz, ce->rx, ce->drx);
	dma_free_coherent(ce->dev, sizeof(struct pkt_control_ecb), ce->pctrl,
			  ce->dctrl);
}

static void start_dma_tx(struct sl3516_ce_dev *ce)
{
	u32 v;

	v = TXDMA_CTRL_START | TXDMA_CTRL_CHAIN_MODE | TXDMA_CTRL_CONTINUE | \
		TXDMA_CTRL_INT_FAIL | TXDMA_CTRL_INT_PERR | TXDMA_CTRL_BURST_UNK;

	writel(v, ce->base + IPSEC_TXDMA_CTRL);
}

static void start_dma_rx(struct sl3516_ce_dev *ce)
{
	u32 v;

	v = RXDMA_CTRL_START | RXDMA_CTRL_CHAIN_MODE | RXDMA_CTRL_CONTINUE | \
		RXDMA_CTRL_BURST_UNK | RXDMA_CTRL_INT_FINISH | \
		RXDMA_CTRL_INT_FAIL | RXDMA_CTRL_INT_PERR | \
		RXDMA_CTRL_INT_EOD | RXDMA_CTRL_INT_EOF;

	writel(v, ce->base + IPSEC_RXDMA_CTRL);
}

static struct descriptor *get_desc_tx(struct sl3516_ce_dev *ce)
{
	struct descriptor *dd;

	dd = &ce->tx[ce->ctx];
	ce->ctx++;
	if (ce->ctx >= MAXDESC)
		ce->ctx = 0;
	return dd;
}

static struct descriptor *get_desc_rx(struct sl3516_ce_dev *ce)
{
	struct descriptor *rdd;

	rdd = &ce->rx[ce->crx];
	ce->crx++;
	if (ce->crx >= MAXDESC)
		ce->crx = 0;
	return rdd;
}

int sl3516_ce_run_task(struct sl3516_ce_dev *ce, struct sl3516_ce_cipher_req_ctx *rctx,
		       const char *name)
{
	struct descriptor *dd, *rdd = NULL;
	u32 v;
	int i, err = 0;

	ce->stat_req++;

	reinit_completion(&ce->complete);
	ce->status = 0;

	for (i = 0; i < rctx->nr_sgd; i++) {
		dev_dbg(ce->dev, "%s handle DST SG %d/%d len=%d\n", __func__,
			i, rctx->nr_sgd, rctx->t_dst[i].len);
		rdd = get_desc_rx(ce);
		rdd->buf_adr = rctx->t_dst[i].addr;
		rdd->frame_ctrl.bits.buffer_size = rctx->t_dst[i].len;
		rdd->frame_ctrl.bits.own = CE_DMA;
	}
	rdd->next_desc.bits.eofie = 1;

	for (i = 0; i < rctx->nr_sgs; i++) {
		dev_dbg(ce->dev, "%s handle SRC SG %d/%d len=%d\n", __func__,
			i, rctx->nr_sgs, rctx->t_src[i].len);
		rctx->h->algorithm_len = rctx->t_src[i].len;

		dd = get_desc_tx(ce);
		dd->frame_ctrl.raw = 0;
		dd->flag_status.raw = 0;
		dd->frame_ctrl.bits.buffer_size = rctx->pctrllen;
		dd->buf_adr = ce->dctrl;
		dd->flag_status.tx_flag.tqflag = rctx->tqflag;
		dd->next_desc.bits.eofie = 0;
		dd->next_desc.bits.dec = 0;
		dd->next_desc.bits.sof_eof = DESC_FIRST | DESC_LAST;
		dd->frame_ctrl.bits.own = CE_DMA;

		dd = get_desc_tx(ce);
		dd->frame_ctrl.raw = 0;
		dd->flag_status.raw = 0;
		dd->frame_ctrl.bits.buffer_size = rctx->t_src[i].len;
		dd->buf_adr = rctx->t_src[i].addr;
		dd->flag_status.tx_flag.tqflag = 0;
		dd->next_desc.bits.eofie = 0;
		dd->next_desc.bits.dec = 0;
		dd->next_desc.bits.sof_eof = DESC_FIRST | DESC_LAST;
		dd->frame_ctrl.bits.own = CE_DMA;
		start_dma_tx(ce);
		start_dma_rx(ce);
	}
	wait_for_completion_interruptible_timeout(&ce->complete,
						  msecs_to_jiffies(5000));
	if (ce->status == 0) {
		dev_err(ce->dev, "DMA timeout for %s\n", name);
		err = -EFAULT;
	}
	v = readl(ce->base + IPSEC_STATUS_REG);
	if (v & 0xFFF) {
		dev_err(ce->dev, "IPSEC_STATUS_REG %x\n", v);
		err = -EFAULT;
	}

	return err;
}

static irqreturn_t ce_irq_handler(int irq, void *data)
{
	struct sl3516_ce_dev *ce = (struct sl3516_ce_dev *)data;
	u32 v;

	ce->stat_irq++;

	v = readl(ce->base + IPSEC_DMA_STATUS);
	writel(v, ce->base + IPSEC_DMA_STATUS);

	if (v & DMA_STATUS_TS_DERR)
		dev_err(ce->dev, "AHB bus Error While Tx !!!\n");
	if (v & DMA_STATUS_TS_PERR)
		dev_err(ce->dev, "Tx Descriptor Protocol Error !!!\n");
	if (v & DMA_STATUS_RS_DERR)
		dev_err(ce->dev, "AHB bus Error While Rx !!!\n");
	if (v & DMA_STATUS_RS_PERR)
		dev_err(ce->dev, "Rx Descriptor Protocol Error !!!\n");

	if (v & DMA_STATUS_TS_EOFI)
		ce->stat_irq_tx++;
	if (v & DMA_STATUS_RS_EOFI) {
		ce->status = 1;
		complete(&ce->complete);
		ce->stat_irq_rx++;
		return IRQ_HANDLED;
	}

	return IRQ_HANDLED;
}

static struct sl3516_ce_alg_template ce_algs[] = {
{
	.type = CRYPTO_ALG_TYPE_SKCIPHER,
	.mode = ECB_AES,
	.alg.skcipher.base = {
		.base = {
			.cra_name = "ecb(aes)",
			.cra_driver_name = "ecb-aes-sl3516",
			.cra_priority = 400,
			.cra_blocksize = AES_BLOCK_SIZE,
			.cra_flags = CRYPTO_ALG_TYPE_SKCIPHER |
				CRYPTO_ALG_ASYNC | CRYPTO_ALG_NEED_FALLBACK,
			.cra_ctxsize = sizeof(struct sl3516_ce_cipher_tfm_ctx),
			.cra_module = THIS_MODULE,
			.cra_alignmask = 0xf,
			.cra_init = sl3516_ce_cipher_init,
			.cra_exit = sl3516_ce_cipher_exit,
		},
		.min_keysize	= AES_MIN_KEY_SIZE,
		.max_keysize	= AES_MAX_KEY_SIZE,
		.setkey		= sl3516_ce_aes_setkey,
		.encrypt	= sl3516_ce_skencrypt,
		.decrypt	= sl3516_ce_skdecrypt,
	},
	.alg.skcipher.op = {
		.do_one_request = sl3516_ce_handle_cipher_request,
	},
},
};

static int sl3516_ce_debugfs_show(struct seq_file *seq, void *v)
{
	struct sl3516_ce_dev *ce = seq->private;
	unsigned int i;

	seq_printf(seq, "HWRNG %lu %lu\n",
		   ce->hwrng_stat_req, ce->hwrng_stat_bytes);
	seq_printf(seq, "IRQ %lu\n", ce->stat_irq);
	seq_printf(seq, "IRQ TX %lu\n", ce->stat_irq_tx);
	seq_printf(seq, "IRQ RX %lu\n", ce->stat_irq_rx);
	seq_printf(seq, "nreq %lu\n", ce->stat_req);
	seq_printf(seq, "fallback SG count TX %lu\n", ce->fallback_sg_count_tx);
	seq_printf(seq, "fallback SG count RX %lu\n", ce->fallback_sg_count_rx);
	seq_printf(seq, "fallback modulo16 %lu\n", ce->fallback_mod16);
	seq_printf(seq, "fallback align16 %lu\n", ce->fallback_align16);
	seq_printf(seq, "fallback not same len %lu\n", ce->fallback_not_same_len);

	for (i = 0; i < ARRAY_SIZE(ce_algs); i++) {
		if (!ce_algs[i].ce)
			continue;
		switch (ce_algs[i].type) {
		case CRYPTO_ALG_TYPE_SKCIPHER:
			seq_printf(seq, "%s %s reqs=%lu fallback=%lu\n",
				   ce_algs[i].alg.skcipher.base.base.cra_driver_name,
				   ce_algs[i].alg.skcipher.base.base.cra_name,
				   ce_algs[i].stat_req, ce_algs[i].stat_fb);
			break;
		}
	}
	return 0;
}

DEFINE_SHOW_ATTRIBUTE(sl3516_ce_debugfs);

static int sl3516_ce_register_algs(struct sl3516_ce_dev *ce)
{
	int err;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(ce_algs); i++) {
		ce_algs[i].ce = ce;
		switch (ce_algs[i].type) {
		case CRYPTO_ALG_TYPE_SKCIPHER:
			dev_info(ce->dev, "DEBUG: Register %s\n",
				 ce_algs[i].alg.skcipher.base.base.cra_name);
			err = crypto_engine_register_skcipher(&ce_algs[i].alg.skcipher);
			if (err) {
				dev_err(ce->dev, "Fail to register %s\n",
					ce_algs[i].alg.skcipher.base.base.cra_name);
				ce_algs[i].ce = NULL;
				return err;
			}
			break;
		default:
			ce_algs[i].ce = NULL;
			dev_err(ce->dev, "ERROR: tried to register an unknown algo\n");
		}
	}
	return 0;
}

static void sl3516_ce_unregister_algs(struct sl3516_ce_dev *ce)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(ce_algs); i++) {
		if (!ce_algs[i].ce)
			continue;
		switch (ce_algs[i].type) {
		case CRYPTO_ALG_TYPE_SKCIPHER:
			dev_info(ce->dev, "Unregister %d %s\n", i,
				 ce_algs[i].alg.skcipher.base.base.cra_name);
			crypto_engine_unregister_skcipher(&ce_algs[i].alg.skcipher);
			break;
		}
	}
}

static void sl3516_ce_start(struct sl3516_ce_dev *ce)
{
	ce->ctx = 0;
	ce->crx = 0;
	writel(ce->dtx, ce->base + IPSEC_TXDMA_CURR_DESC);
	writel(ce->drx, ce->base + IPSEC_RXDMA_CURR_DESC);
	writel(0, ce->base + IPSEC_DMA_STATUS);
}

/*
 * Power management strategy: The device is suspended unless a TFM exists for
 * one of the algorithms proposed by this driver.
 */
static int sl3516_ce_pm_suspend(struct device *dev)
{
	struct sl3516_ce_dev *ce = dev_get_drvdata(dev);

	reset_control_assert(ce->reset);
	clk_disable_unprepare(ce->clks);
	return 0;
}

static int sl3516_ce_pm_resume(struct device *dev)
{
	struct sl3516_ce_dev *ce = dev_get_drvdata(dev);
	int err;

	err = clk_prepare_enable(ce->clks);
	if (err) {
		dev_err(ce->dev, "Cannot prepare_enable\n");
		goto error;
	}
	err = reset_control_deassert(ce->reset);
	if (err) {
		dev_err(ce->dev, "Cannot deassert reset control\n");
		goto error;
	}

	sl3516_ce_start(ce);

	return 0;
error:
	sl3516_ce_pm_suspend(dev);
	return err;
}

static const struct dev_pm_ops sl3516_ce_pm_ops = {
	SET_RUNTIME_PM_OPS(sl3516_ce_pm_suspend, sl3516_ce_pm_resume, NULL)
};

static int sl3516_ce_pm_init(struct sl3516_ce_dev *ce)
{
	int err;

	pm_runtime_use_autosuspend(ce->dev);
	pm_runtime_set_autosuspend_delay(ce->dev, 2000);

	err = pm_runtime_set_suspended(ce->dev);
	if (err)
		return err;
	pm_runtime_enable(ce->dev);
	return err;
}

static void sl3516_ce_pm_exit(struct sl3516_ce_dev *ce)
{
	pm_runtime_disable(ce->dev);
}

static int sl3516_ce_probe(struct platform_device *pdev)
{
	struct sl3516_ce_dev *ce;
	int err, irq;
	u32 v;

	ce = devm_kzalloc(&pdev->dev, sizeof(*ce), GFP_KERNEL);
	if (!ce)
		return -ENOMEM;

	ce->dev = &pdev->dev;
	platform_set_drvdata(pdev, ce);

	ce->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(ce->base))
		return PTR_ERR(ce->base);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	err = devm_request_irq(&pdev->dev, irq, ce_irq_handler, 0, "crypto", ce);
	if (err) {
		dev_err(ce->dev, "Cannot request Crypto Engine IRQ (err=%d)\n", err);
		return err;
	}

	ce->reset = devm_reset_control_get(&pdev->dev, NULL);
	if (IS_ERR(ce->reset))
		return dev_err_probe(&pdev->dev, PTR_ERR(ce->reset),
				     "No reset control found\n");
	ce->clks = devm_clk_get(ce->dev, NULL);
	if (IS_ERR(ce->clks)) {
		err = PTR_ERR(ce->clks);
		dev_err(ce->dev, "Cannot get clock err=%d\n", err);
		return err;
	}

	err = sl3516_ce_desc_init(ce);
	if (err)
		return err;

	err = sl3516_ce_pm_init(ce);
	if (err)
		goto error_pm;

	init_completion(&ce->complete);

	ce->engine = crypto_engine_alloc_init(ce->dev, true);
	if (!ce->engine) {
		dev_err(ce->dev, "Cannot allocate engine\n");
		err = -ENOMEM;
		goto error_engine;
	}

	err = crypto_engine_start(ce->engine);
	if (err) {
		dev_err(ce->dev, "Cannot start engine\n");
		goto error_engine;
	}

	err = sl3516_ce_register_algs(ce);
	if (err)
		goto error_alg;

	err = sl3516_ce_rng_register(ce);
	if (err)
		goto error_rng;

	err = pm_runtime_resume_and_get(ce->dev);
	if (err < 0)
		goto error_pmuse;

	v = readl(ce->base + IPSEC_ID);
	dev_info(ce->dev, "SL3516 dev %lx rev %lx\n",
		 v & GENMASK(31, 4),
		 v & GENMASK(3, 0));
	v = readl(ce->base + IPSEC_DMA_DEVICE_ID);
	dev_info(ce->dev, "SL3516 DMA dev %lx rev %lx\n",
		 v & GENMASK(15, 4),
		 v & GENMASK(3, 0));

	pm_runtime_put_sync(ce->dev);

	if (IS_ENABLED(CONFIG_CRYPTO_DEV_SL3516_DEBUG)) {
		struct dentry *dbgfs_dir __maybe_unused;
		struct dentry *dbgfs_stats __maybe_unused;

		/* Ignore error of debugfs */
		dbgfs_dir = debugfs_create_dir("sl3516", NULL);
		dbgfs_stats = debugfs_create_file("stats", 0444,
						  dbgfs_dir, ce,
						  &sl3516_ce_debugfs_fops);
#ifdef CONFIG_CRYPTO_DEV_SL3516_DEBUG
		ce->dbgfs_dir = dbgfs_dir;
		ce->dbgfs_stats = dbgfs_stats;
#endif
	}

	return 0;
error_pmuse:
	sl3516_ce_rng_unregister(ce);
error_rng:
	sl3516_ce_unregister_algs(ce);
error_alg:
	crypto_engine_exit(ce->engine);
error_engine:
	sl3516_ce_pm_exit(ce);
error_pm:
	sl3516_ce_free_descs(ce);
	return err;
}

static void sl3516_ce_remove(struct platform_device *pdev)
{
	struct sl3516_ce_dev *ce = platform_get_drvdata(pdev);

	sl3516_ce_rng_unregister(ce);
	sl3516_ce_unregister_algs(ce);
	crypto_engine_exit(ce->engine);
	sl3516_ce_pm_exit(ce);
	sl3516_ce_free_descs(ce);

#ifdef CONFIG_CRYPTO_DEV_SL3516_DEBUG
	debugfs_remove_recursive(ce->dbgfs_dir);
#endif
}

static const struct of_device_id sl3516_ce_crypto_of_match_table[] = {
	{ .compatible = "cortina,sl3516-crypto"},
	{}
};
MODULE_DEVICE_TABLE(of, sl3516_ce_crypto_of_match_table);

static struct platform_driver sl3516_ce_driver = {
	.probe		 = sl3516_ce_probe,
	.remove_new	 = sl3516_ce_remove,
	.driver		 = {
		.name		= "sl3516-crypto",
		.pm		= &sl3516_ce_pm_ops,
		.of_match_table	= sl3516_ce_crypto_of_match_table,
	},
};

module_platform_driver(sl3516_ce_driver);

MODULE_DESCRIPTION("SL3516 cryptographic offloader");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Corentin Labbe <clabbe@baylibre.com>");

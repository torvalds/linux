// SPDX-License-Identifier: GPL-2.0
/*
 * amlgoic-core.c - hardware cryptographic offloader for Amlogic GXL SoC
 *
 * Copyright (C) 2018-2019 Corentin Labbe <clabbe@baylibre.com>
 *
 * Core file which registers crypto algorithms supported by the hardware.
 */

#include <crypto/engine.h>
#include <crypto/internal/skcipher.h>
#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#include "amlogic-gxl.h"

static irqreturn_t meson_irq_handler(int irq, void *data)
{
	struct meson_dev *mc = (struct meson_dev *)data;
	int flow;
	u32 p;

	for (flow = 0; flow < MAXFLOW; flow++) {
		if (mc->irqs[flow] == irq) {
			p = readl(mc->base + ((0x04 + flow) << 2));
			if (p) {
				writel_relaxed(0xF, mc->base + ((0x4 + flow) << 2));
				mc->chanlist[flow].status = 1;
				complete(&mc->chanlist[flow].complete);
				return IRQ_HANDLED;
			}
			dev_err(mc->dev, "%s %d Got irq for flow %d but ctrl is empty\n", __func__, irq, flow);
		}
	}

	dev_err(mc->dev, "%s %d from unknown irq\n", __func__, irq);
	return IRQ_HANDLED;
}

static struct meson_alg_template mc_algs[] = {
{
	.type = CRYPTO_ALG_TYPE_SKCIPHER,
	.blockmode = MESON_OPMODE_CBC,
	.alg.skcipher.base = {
		.base = {
			.cra_name = "cbc(aes)",
			.cra_driver_name = "cbc-aes-gxl",
			.cra_priority = 400,
			.cra_blocksize = AES_BLOCK_SIZE,
			.cra_flags = CRYPTO_ALG_TYPE_SKCIPHER |
				CRYPTO_ALG_ASYNC | CRYPTO_ALG_ALLOCATES_MEMORY |
				CRYPTO_ALG_NEED_FALLBACK,
			.cra_ctxsize = sizeof(struct meson_cipher_tfm_ctx),
			.cra_module = THIS_MODULE,
			.cra_alignmask = 0xf,
			.cra_init = meson_cipher_init,
			.cra_exit = meson_cipher_exit,
		},
		.min_keysize	= AES_MIN_KEY_SIZE,
		.max_keysize	= AES_MAX_KEY_SIZE,
		.ivsize		= AES_BLOCK_SIZE,
		.setkey		= meson_aes_setkey,
		.encrypt	= meson_skencrypt,
		.decrypt	= meson_skdecrypt,
	},
	.alg.skcipher.op = {
		.do_one_request = meson_handle_cipher_request,
	},
},
{
	.type = CRYPTO_ALG_TYPE_SKCIPHER,
	.blockmode = MESON_OPMODE_ECB,
	.alg.skcipher.base = {
		.base = {
			.cra_name = "ecb(aes)",
			.cra_driver_name = "ecb-aes-gxl",
			.cra_priority = 400,
			.cra_blocksize = AES_BLOCK_SIZE,
			.cra_flags = CRYPTO_ALG_TYPE_SKCIPHER |
				CRYPTO_ALG_ASYNC | CRYPTO_ALG_ALLOCATES_MEMORY |
				CRYPTO_ALG_NEED_FALLBACK,
			.cra_ctxsize = sizeof(struct meson_cipher_tfm_ctx),
			.cra_module = THIS_MODULE,
			.cra_alignmask = 0xf,
			.cra_init = meson_cipher_init,
			.cra_exit = meson_cipher_exit,
		},
		.min_keysize	= AES_MIN_KEY_SIZE,
		.max_keysize	= AES_MAX_KEY_SIZE,
		.setkey		= meson_aes_setkey,
		.encrypt	= meson_skencrypt,
		.decrypt	= meson_skdecrypt,
	},
	.alg.skcipher.op = {
		.do_one_request = meson_handle_cipher_request,
	},
},
};

static int meson_debugfs_show(struct seq_file *seq, void *v)
{
	struct meson_dev *mc __maybe_unused = seq->private;
	int i;

	for (i = 0; i < MAXFLOW; i++)
		seq_printf(seq, "Channel %d: nreq %lu\n", i,
#ifdef CONFIG_CRYPTO_DEV_AMLOGIC_GXL_DEBUG
			   mc->chanlist[i].stat_req);
#else
			   0ul);
#endif

	for (i = 0; i < ARRAY_SIZE(mc_algs); i++) {
		switch (mc_algs[i].type) {
		case CRYPTO_ALG_TYPE_SKCIPHER:
			seq_printf(seq, "%s %s %lu %lu\n",
				   mc_algs[i].alg.skcipher.base.base.cra_driver_name,
				   mc_algs[i].alg.skcipher.base.base.cra_name,
#ifdef CONFIG_CRYPTO_DEV_AMLOGIC_GXL_DEBUG
				   mc_algs[i].stat_req, mc_algs[i].stat_fb);
#else
				   0ul, 0ul);
#endif
			break;
		}
	}
	return 0;
}
DEFINE_SHOW_ATTRIBUTE(meson_debugfs);

static void meson_free_chanlist(struct meson_dev *mc, int i)
{
	while (i >= 0) {
		crypto_engine_exit(mc->chanlist[i].engine);
		if (mc->chanlist[i].tl)
			dma_free_coherent(mc->dev, sizeof(struct meson_desc) * MAXDESC,
					  mc->chanlist[i].tl,
					  mc->chanlist[i].t_phy);
		i--;
	}
}

/*
 * Allocate the channel list structure
 */
static int meson_allocate_chanlist(struct meson_dev *mc)
{
	int i, err;

	mc->chanlist = devm_kcalloc(mc->dev, MAXFLOW,
				    sizeof(struct meson_flow), GFP_KERNEL);
	if (!mc->chanlist)
		return -ENOMEM;

	for (i = 0; i < MAXFLOW; i++) {
		init_completion(&mc->chanlist[i].complete);

		mc->chanlist[i].engine = crypto_engine_alloc_init(mc->dev, true);
		if (!mc->chanlist[i].engine) {
			dev_err(mc->dev, "Cannot allocate engine\n");
			i--;
			err = -ENOMEM;
			goto error_engine;
		}
		err = crypto_engine_start(mc->chanlist[i].engine);
		if (err) {
			dev_err(mc->dev, "Cannot start engine\n");
			goto error_engine;
		}
		mc->chanlist[i].tl = dma_alloc_coherent(mc->dev,
							sizeof(struct meson_desc) * MAXDESC,
							&mc->chanlist[i].t_phy,
							GFP_KERNEL);
		if (!mc->chanlist[i].tl) {
			err = -ENOMEM;
			goto error_engine;
		}
	}
	return 0;
error_engine:
	meson_free_chanlist(mc, i);
	return err;
}

static int meson_register_algs(struct meson_dev *mc)
{
	int err, i;

	for (i = 0; i < ARRAY_SIZE(mc_algs); i++) {
		mc_algs[i].mc = mc;
		switch (mc_algs[i].type) {
		case CRYPTO_ALG_TYPE_SKCIPHER:
			err = crypto_engine_register_skcipher(&mc_algs[i].alg.skcipher);
			if (err) {
				dev_err(mc->dev, "Fail to register %s\n",
					mc_algs[i].alg.skcipher.base.base.cra_name);
				mc_algs[i].mc = NULL;
				return err;
			}
			break;
		}
	}

	return 0;
}

static void meson_unregister_algs(struct meson_dev *mc)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(mc_algs); i++) {
		if (!mc_algs[i].mc)
			continue;
		switch (mc_algs[i].type) {
		case CRYPTO_ALG_TYPE_SKCIPHER:
			crypto_engine_unregister_skcipher(&mc_algs[i].alg.skcipher);
			break;
		}
	}
}

static int meson_crypto_probe(struct platform_device *pdev)
{
	struct meson_dev *mc;
	int err, i;

	mc = devm_kzalloc(&pdev->dev, sizeof(*mc), GFP_KERNEL);
	if (!mc)
		return -ENOMEM;

	mc->dev = &pdev->dev;
	platform_set_drvdata(pdev, mc);

	mc->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(mc->base)) {
		err = PTR_ERR(mc->base);
		dev_err(&pdev->dev, "Cannot request MMIO err=%d\n", err);
		return err;
	}
	mc->busclk = devm_clk_get(&pdev->dev, "blkmv");
	if (IS_ERR(mc->busclk)) {
		err = PTR_ERR(mc->busclk);
		dev_err(&pdev->dev, "Cannot get core clock err=%d\n", err);
		return err;
	}

	for (i = 0; i < MAXFLOW; i++) {
		mc->irqs[i] = platform_get_irq(pdev, i);
		if (mc->irqs[i] < 0)
			return mc->irqs[i];

		err = devm_request_irq(&pdev->dev, mc->irqs[i], meson_irq_handler, 0,
				       "gxl-crypto", mc);
		if (err < 0) {
			dev_err(mc->dev, "Cannot request IRQ for flow %d\n", i);
			return err;
		}
	}

	err = clk_prepare_enable(mc->busclk);
	if (err != 0) {
		dev_err(&pdev->dev, "Cannot prepare_enable busclk\n");
		return err;
	}

	err = meson_allocate_chanlist(mc);
	if (err)
		goto error_flow;

	err = meson_register_algs(mc);
	if (err)
		goto error_alg;

	if (IS_ENABLED(CONFIG_CRYPTO_DEV_AMLOGIC_GXL_DEBUG)) {
		struct dentry *dbgfs_dir;

		dbgfs_dir = debugfs_create_dir("gxl-crypto", NULL);
		debugfs_create_file("stats", 0444, dbgfs_dir, mc, &meson_debugfs_fops);

#ifdef CONFIG_CRYPTO_DEV_AMLOGIC_GXL_DEBUG
		mc->dbgfs_dir = dbgfs_dir;
#endif
	}

	return 0;
error_alg:
	meson_unregister_algs(mc);
error_flow:
	meson_free_chanlist(mc, MAXFLOW - 1);
	clk_disable_unprepare(mc->busclk);
	return err;
}

static void meson_crypto_remove(struct platform_device *pdev)
{
	struct meson_dev *mc = platform_get_drvdata(pdev);

#ifdef CONFIG_CRYPTO_DEV_AMLOGIC_GXL_DEBUG
	debugfs_remove_recursive(mc->dbgfs_dir);
#endif

	meson_unregister_algs(mc);

	meson_free_chanlist(mc, MAXFLOW - 1);

	clk_disable_unprepare(mc->busclk);
}

static const struct of_device_id meson_crypto_of_match_table[] = {
	{ .compatible = "amlogic,gxl-crypto", },
	{}
};
MODULE_DEVICE_TABLE(of, meson_crypto_of_match_table);

static struct platform_driver meson_crypto_driver = {
	.probe		 = meson_crypto_probe,
	.remove_new	 = meson_crypto_remove,
	.driver		 = {
		.name		   = "gxl-crypto",
		.of_match_table	= meson_crypto_of_match_table,
	},
};

module_platform_driver(meson_crypto_driver);

MODULE_DESCRIPTION("Amlogic GXL cryptographic offloader");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Corentin Labbe <clabbe@baylibre.com>");

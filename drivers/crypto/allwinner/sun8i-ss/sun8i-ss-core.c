// SPDX-License-Identifier: GPL-2.0
/*
 * sun8i-ss-core.c - hardware cryptographic offloader for
 * Allwinner A80/A83T SoC
 *
 * Copyright (C) 2015-2019 Corentin Labbe <clabbe.montjoie@gmail.com>
 *
 * Core file which registers crypto algorithms supported by the SecuritySystem
 *
 * You could find a link for the datasheet in Documentation/arch/arm/sunxi.rst
 */

#include <crypto/engine.h>
#include <crypto/internal/rng.h>
#include <crypto/internal/skcipher.h>
#include <linux/clk.h>
#include <linux/delay.h>
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

#include "sun8i-ss.h"

static const struct ss_variant ss_a80_variant = {
	.alg_cipher = { SS_ALG_AES, SS_ALG_DES, SS_ALG_3DES,
	},
	.alg_hash = { SS_ID_NOTSUPP, SS_ID_NOTSUPP, SS_ID_NOTSUPP, SS_ID_NOTSUPP,
	},
	.op_mode = { SS_OP_ECB, SS_OP_CBC,
	},
	.ss_clks = {
		{ "bus", 0, 300 * 1000 * 1000 },
		{ "mod", 0, 300 * 1000 * 1000 },
	}
};

static const struct ss_variant ss_a83t_variant = {
	.alg_cipher = { SS_ALG_AES, SS_ALG_DES, SS_ALG_3DES,
	},
	.alg_hash = { SS_ALG_MD5, SS_ALG_SHA1, SS_ALG_SHA224, SS_ALG_SHA256,
	},
	.op_mode = { SS_OP_ECB, SS_OP_CBC,
	},
	.ss_clks = {
		{ "bus", 0, 300 * 1000 * 1000 },
		{ "mod", 0, 300 * 1000 * 1000 },
	}
};

/*
 * sun8i_ss_get_engine_number() get the next channel slot
 * This is a simple round-robin way of getting the next channel
 */
int sun8i_ss_get_engine_number(struct sun8i_ss_dev *ss)
{
	return atomic_inc_return(&ss->flow) % MAXFLOW;
}

int sun8i_ss_run_task(struct sun8i_ss_dev *ss, struct sun8i_cipher_req_ctx *rctx,
		      const char *name)
{
	int flow = rctx->flow;
	unsigned int ivlen = rctx->ivlen;
	u32 v = SS_START;
	int i;

#ifdef CONFIG_CRYPTO_DEV_SUN8I_SS_DEBUG
	ss->flows[flow].stat_req++;
#endif

	/* choose between stream0/stream1 */
	if (flow)
		v |= SS_FLOW1;
	else
		v |= SS_FLOW0;

	v |= rctx->op_mode;
	v |= rctx->method;

	if (rctx->op_dir)
		v |= SS_DECRYPTION;

	switch (rctx->keylen) {
	case 128 / 8:
		v |= SS_AES_128BITS << 7;
		break;
	case 192 / 8:
		v |= SS_AES_192BITS << 7;
		break;
	case 256 / 8:
		v |= SS_AES_256BITS << 7;
		break;
	}

	for (i = 0; i < MAX_SG; i++) {
		if (!rctx->t_dst[i].addr)
			break;

		mutex_lock(&ss->mlock);
		writel(rctx->p_key, ss->base + SS_KEY_ADR_REG);

		if (ivlen) {
			if (rctx->op_dir == SS_ENCRYPTION) {
				if (i == 0)
					writel(rctx->p_iv[0], ss->base + SS_IV_ADR_REG);
				else
					writel(rctx->t_dst[i - 1].addr + rctx->t_dst[i - 1].len * 4 - ivlen, ss->base + SS_IV_ADR_REG);
			} else {
				writel(rctx->p_iv[i], ss->base + SS_IV_ADR_REG);
			}
		}

		dev_dbg(ss->dev,
			"Processing SG %d on flow %d %s ctl=%x %d to %d method=%x opmode=%x opdir=%x srclen=%d\n",
			i, flow, name, v,
			rctx->t_src[i].len, rctx->t_dst[i].len,
			rctx->method, rctx->op_mode,
			rctx->op_dir, rctx->t_src[i].len);

		writel(rctx->t_src[i].addr, ss->base + SS_SRC_ADR_REG);
		writel(rctx->t_dst[i].addr, ss->base + SS_DST_ADR_REG);
		writel(rctx->t_src[i].len, ss->base + SS_LEN_ADR_REG);

		reinit_completion(&ss->flows[flow].complete);
		ss->flows[flow].status = 0;
		wmb();

		writel(v, ss->base + SS_CTL_REG);
		mutex_unlock(&ss->mlock);
		wait_for_completion_interruptible_timeout(&ss->flows[flow].complete,
							  msecs_to_jiffies(2000));
		if (ss->flows[flow].status == 0) {
			dev_err(ss->dev, "DMA timeout for %s\n", name);
			return -EFAULT;
		}
	}

	return 0;
}

static irqreturn_t ss_irq_handler(int irq, void *data)
{
	struct sun8i_ss_dev *ss = (struct sun8i_ss_dev *)data;
	int flow = 0;
	u32 p;

	p = readl(ss->base + SS_INT_STA_REG);
	for (flow = 0; flow < MAXFLOW; flow++) {
		if (p & (BIT(flow))) {
			writel(BIT(flow), ss->base + SS_INT_STA_REG);
			ss->flows[flow].status = 1;
			complete(&ss->flows[flow].complete);
		}
	}

	return IRQ_HANDLED;
}

static struct sun8i_ss_alg_template ss_algs[] = {
{
	.type = CRYPTO_ALG_TYPE_SKCIPHER,
	.ss_algo_id = SS_ID_CIPHER_AES,
	.ss_blockmode = SS_ID_OP_CBC,
	.alg.skcipher.base = {
		.base = {
			.cra_name = "cbc(aes)",
			.cra_driver_name = "cbc-aes-sun8i-ss",
			.cra_priority = 400,
			.cra_blocksize = AES_BLOCK_SIZE,
			.cra_flags = CRYPTO_ALG_TYPE_SKCIPHER |
				CRYPTO_ALG_ASYNC | CRYPTO_ALG_ALLOCATES_MEMORY |
				CRYPTO_ALG_NEED_FALLBACK,
			.cra_ctxsize = sizeof(struct sun8i_cipher_tfm_ctx),
			.cra_module = THIS_MODULE,
			.cra_alignmask = 0xf,
			.cra_init = sun8i_ss_cipher_init,
			.cra_exit = sun8i_ss_cipher_exit,
		},
		.min_keysize	= AES_MIN_KEY_SIZE,
		.max_keysize	= AES_MAX_KEY_SIZE,
		.ivsize		= AES_BLOCK_SIZE,
		.setkey		= sun8i_ss_aes_setkey,
		.encrypt	= sun8i_ss_skencrypt,
		.decrypt	= sun8i_ss_skdecrypt,
	},
	.alg.skcipher.op = {
		.do_one_request = sun8i_ss_handle_cipher_request,
	},
},
{
	.type = CRYPTO_ALG_TYPE_SKCIPHER,
	.ss_algo_id = SS_ID_CIPHER_AES,
	.ss_blockmode = SS_ID_OP_ECB,
	.alg.skcipher.base = {
		.base = {
			.cra_name = "ecb(aes)",
			.cra_driver_name = "ecb-aes-sun8i-ss",
			.cra_priority = 400,
			.cra_blocksize = AES_BLOCK_SIZE,
			.cra_flags = CRYPTO_ALG_TYPE_SKCIPHER |
				CRYPTO_ALG_ASYNC | CRYPTO_ALG_ALLOCATES_MEMORY |
				CRYPTO_ALG_NEED_FALLBACK,
			.cra_ctxsize = sizeof(struct sun8i_cipher_tfm_ctx),
			.cra_module = THIS_MODULE,
			.cra_alignmask = 0xf,
			.cra_init = sun8i_ss_cipher_init,
			.cra_exit = sun8i_ss_cipher_exit,
		},
		.min_keysize	= AES_MIN_KEY_SIZE,
		.max_keysize	= AES_MAX_KEY_SIZE,
		.setkey		= sun8i_ss_aes_setkey,
		.encrypt	= sun8i_ss_skencrypt,
		.decrypt	= sun8i_ss_skdecrypt,
	},
	.alg.skcipher.op = {
		.do_one_request = sun8i_ss_handle_cipher_request,
	},
},
{
	.type = CRYPTO_ALG_TYPE_SKCIPHER,
	.ss_algo_id = SS_ID_CIPHER_DES3,
	.ss_blockmode = SS_ID_OP_CBC,
	.alg.skcipher.base = {
		.base = {
			.cra_name = "cbc(des3_ede)",
			.cra_driver_name = "cbc-des3-sun8i-ss",
			.cra_priority = 400,
			.cra_blocksize = DES3_EDE_BLOCK_SIZE,
			.cra_flags = CRYPTO_ALG_TYPE_SKCIPHER |
				CRYPTO_ALG_ASYNC | CRYPTO_ALG_ALLOCATES_MEMORY |
				CRYPTO_ALG_NEED_FALLBACK,
			.cra_ctxsize = sizeof(struct sun8i_cipher_tfm_ctx),
			.cra_module = THIS_MODULE,
			.cra_alignmask = 0xf,
			.cra_init = sun8i_ss_cipher_init,
			.cra_exit = sun8i_ss_cipher_exit,
		},
		.min_keysize	= DES3_EDE_KEY_SIZE,
		.max_keysize	= DES3_EDE_KEY_SIZE,
		.ivsize		= DES3_EDE_BLOCK_SIZE,
		.setkey		= sun8i_ss_des3_setkey,
		.encrypt	= sun8i_ss_skencrypt,
		.decrypt	= sun8i_ss_skdecrypt,
	},
	.alg.skcipher.op = {
		.do_one_request = sun8i_ss_handle_cipher_request,
	},
},
{
	.type = CRYPTO_ALG_TYPE_SKCIPHER,
	.ss_algo_id = SS_ID_CIPHER_DES3,
	.ss_blockmode = SS_ID_OP_ECB,
	.alg.skcipher.base = {
		.base = {
			.cra_name = "ecb(des3_ede)",
			.cra_driver_name = "ecb-des3-sun8i-ss",
			.cra_priority = 400,
			.cra_blocksize = DES3_EDE_BLOCK_SIZE,
			.cra_flags = CRYPTO_ALG_TYPE_SKCIPHER |
				CRYPTO_ALG_ASYNC | CRYPTO_ALG_ALLOCATES_MEMORY |
				CRYPTO_ALG_NEED_FALLBACK,
			.cra_ctxsize = sizeof(struct sun8i_cipher_tfm_ctx),
			.cra_module = THIS_MODULE,
			.cra_alignmask = 0xf,
			.cra_init = sun8i_ss_cipher_init,
			.cra_exit = sun8i_ss_cipher_exit,
		},
		.min_keysize	= DES3_EDE_KEY_SIZE,
		.max_keysize	= DES3_EDE_KEY_SIZE,
		.setkey		= sun8i_ss_des3_setkey,
		.encrypt	= sun8i_ss_skencrypt,
		.decrypt	= sun8i_ss_skdecrypt,
	},
	.alg.skcipher.op = {
		.do_one_request = sun8i_ss_handle_cipher_request,
	},
},
#ifdef CONFIG_CRYPTO_DEV_SUN8I_SS_PRNG
{
	.type = CRYPTO_ALG_TYPE_RNG,
	.alg.rng = {
		.base = {
			.cra_name		= "stdrng",
			.cra_driver_name	= "sun8i-ss-prng",
			.cra_priority		= 300,
			.cra_ctxsize = sizeof(struct sun8i_ss_rng_tfm_ctx),
			.cra_module		= THIS_MODULE,
			.cra_init		= sun8i_ss_prng_init,
			.cra_exit		= sun8i_ss_prng_exit,
		},
		.generate               = sun8i_ss_prng_generate,
		.seed                   = sun8i_ss_prng_seed,
		.seedsize               = PRNG_SEED_SIZE,
	}
},
#endif
#ifdef CONFIG_CRYPTO_DEV_SUN8I_SS_HASH
{	.type = CRYPTO_ALG_TYPE_AHASH,
	.ss_algo_id = SS_ID_HASH_MD5,
	.alg.hash.base = {
		.init = sun8i_ss_hash_init,
		.update = sun8i_ss_hash_update,
		.final = sun8i_ss_hash_final,
		.finup = sun8i_ss_hash_finup,
		.digest = sun8i_ss_hash_digest,
		.export = sun8i_ss_hash_export,
		.import = sun8i_ss_hash_import,
		.init_tfm = sun8i_ss_hash_init_tfm,
		.exit_tfm = sun8i_ss_hash_exit_tfm,
		.halg = {
			.digestsize = MD5_DIGEST_SIZE,
			.statesize = sizeof(struct md5_state),
			.base = {
				.cra_name = "md5",
				.cra_driver_name = "md5-sun8i-ss",
				.cra_priority = 300,
				.cra_flags = CRYPTO_ALG_TYPE_AHASH |
					CRYPTO_ALG_ASYNC |
					CRYPTO_ALG_NEED_FALLBACK,
				.cra_blocksize = MD5_HMAC_BLOCK_SIZE,
				.cra_ctxsize = sizeof(struct sun8i_ss_hash_tfm_ctx),
				.cra_module = THIS_MODULE,
			}
		}
	},
	.alg.hash.op = {
		.do_one_request = sun8i_ss_hash_run,
	},
},
{	.type = CRYPTO_ALG_TYPE_AHASH,
	.ss_algo_id = SS_ID_HASH_SHA1,
	.alg.hash.base = {
		.init = sun8i_ss_hash_init,
		.update = sun8i_ss_hash_update,
		.final = sun8i_ss_hash_final,
		.finup = sun8i_ss_hash_finup,
		.digest = sun8i_ss_hash_digest,
		.export = sun8i_ss_hash_export,
		.import = sun8i_ss_hash_import,
		.init_tfm = sun8i_ss_hash_init_tfm,
		.exit_tfm = sun8i_ss_hash_exit_tfm,
		.halg = {
			.digestsize = SHA1_DIGEST_SIZE,
			.statesize = sizeof(struct sha1_state),
			.base = {
				.cra_name = "sha1",
				.cra_driver_name = "sha1-sun8i-ss",
				.cra_priority = 300,
				.cra_flags = CRYPTO_ALG_TYPE_AHASH |
					CRYPTO_ALG_ASYNC |
					CRYPTO_ALG_NEED_FALLBACK,
				.cra_blocksize = SHA1_BLOCK_SIZE,
				.cra_ctxsize = sizeof(struct sun8i_ss_hash_tfm_ctx),
				.cra_module = THIS_MODULE,
			}
		}
	},
	.alg.hash.op = {
		.do_one_request = sun8i_ss_hash_run,
	},
},
{	.type = CRYPTO_ALG_TYPE_AHASH,
	.ss_algo_id = SS_ID_HASH_SHA224,
	.alg.hash.base = {
		.init = sun8i_ss_hash_init,
		.update = sun8i_ss_hash_update,
		.final = sun8i_ss_hash_final,
		.finup = sun8i_ss_hash_finup,
		.digest = sun8i_ss_hash_digest,
		.export = sun8i_ss_hash_export,
		.import = sun8i_ss_hash_import,
		.init_tfm = sun8i_ss_hash_init_tfm,
		.exit_tfm = sun8i_ss_hash_exit_tfm,
		.halg = {
			.digestsize = SHA224_DIGEST_SIZE,
			.statesize = sizeof(struct sha256_state),
			.base = {
				.cra_name = "sha224",
				.cra_driver_name = "sha224-sun8i-ss",
				.cra_priority = 300,
				.cra_flags = CRYPTO_ALG_TYPE_AHASH |
					CRYPTO_ALG_ASYNC |
					CRYPTO_ALG_NEED_FALLBACK,
				.cra_blocksize = SHA224_BLOCK_SIZE,
				.cra_ctxsize = sizeof(struct sun8i_ss_hash_tfm_ctx),
				.cra_module = THIS_MODULE,
			}
		}
	},
	.alg.hash.op = {
		.do_one_request = sun8i_ss_hash_run,
	},
},
{	.type = CRYPTO_ALG_TYPE_AHASH,
	.ss_algo_id = SS_ID_HASH_SHA256,
	.alg.hash.base = {
		.init = sun8i_ss_hash_init,
		.update = sun8i_ss_hash_update,
		.final = sun8i_ss_hash_final,
		.finup = sun8i_ss_hash_finup,
		.digest = sun8i_ss_hash_digest,
		.export = sun8i_ss_hash_export,
		.import = sun8i_ss_hash_import,
		.init_tfm = sun8i_ss_hash_init_tfm,
		.exit_tfm = sun8i_ss_hash_exit_tfm,
		.halg = {
			.digestsize = SHA256_DIGEST_SIZE,
			.statesize = sizeof(struct sha256_state),
			.base = {
				.cra_name = "sha256",
				.cra_driver_name = "sha256-sun8i-ss",
				.cra_priority = 300,
				.cra_flags = CRYPTO_ALG_TYPE_AHASH |
					CRYPTO_ALG_ASYNC |
					CRYPTO_ALG_NEED_FALLBACK,
				.cra_blocksize = SHA256_BLOCK_SIZE,
				.cra_ctxsize = sizeof(struct sun8i_ss_hash_tfm_ctx),
				.cra_module = THIS_MODULE,
			}
		}
	},
	.alg.hash.op = {
		.do_one_request = sun8i_ss_hash_run,
	},
},
{	.type = CRYPTO_ALG_TYPE_AHASH,
	.ss_algo_id = SS_ID_HASH_SHA1,
	.alg.hash.base = {
		.init = sun8i_ss_hash_init,
		.update = sun8i_ss_hash_update,
		.final = sun8i_ss_hash_final,
		.finup = sun8i_ss_hash_finup,
		.digest = sun8i_ss_hash_digest,
		.export = sun8i_ss_hash_export,
		.import = sun8i_ss_hash_import,
		.init_tfm = sun8i_ss_hash_init_tfm,
		.exit_tfm = sun8i_ss_hash_exit_tfm,
		.setkey = sun8i_ss_hmac_setkey,
		.halg = {
			.digestsize = SHA1_DIGEST_SIZE,
			.statesize = sizeof(struct sha1_state),
			.base = {
				.cra_name = "hmac(sha1)",
				.cra_driver_name = "hmac-sha1-sun8i-ss",
				.cra_priority = 300,
				.cra_flags = CRYPTO_ALG_TYPE_AHASH |
					CRYPTO_ALG_ASYNC |
					CRYPTO_ALG_NEED_FALLBACK,
				.cra_blocksize = SHA1_BLOCK_SIZE,
				.cra_ctxsize = sizeof(struct sun8i_ss_hash_tfm_ctx),
				.cra_module = THIS_MODULE,
			}
		}
	},
	.alg.hash.op = {
		.do_one_request = sun8i_ss_hash_run,
	},
},
#endif
};

static int sun8i_ss_debugfs_show(struct seq_file *seq, void *v)
{
	struct sun8i_ss_dev *ss __maybe_unused = seq->private;
	unsigned int i;

	for (i = 0; i < MAXFLOW; i++)
		seq_printf(seq, "Channel %d: nreq %lu\n", i,
#ifdef CONFIG_CRYPTO_DEV_SUN8I_SS_DEBUG
			   ss->flows[i].stat_req);
#else
			   0ul);
#endif

	for (i = 0; i < ARRAY_SIZE(ss_algs); i++) {
		if (!ss_algs[i].ss)
			continue;
		switch (ss_algs[i].type) {
		case CRYPTO_ALG_TYPE_SKCIPHER:
			seq_printf(seq, "%s %s reqs=%lu fallback=%lu\n",
				   ss_algs[i].alg.skcipher.base.base.cra_driver_name,
				   ss_algs[i].alg.skcipher.base.base.cra_name,
				   ss_algs[i].stat_req, ss_algs[i].stat_fb);

			seq_printf(seq, "\tLast fallback is: %s\n",
				   ss_algs[i].fbname);
			seq_printf(seq, "\tFallback due to length: %lu\n",
				   ss_algs[i].stat_fb_len);
			seq_printf(seq, "\tFallback due to SG length: %lu\n",
				   ss_algs[i].stat_fb_sglen);
			seq_printf(seq, "\tFallback due to alignment: %lu\n",
				   ss_algs[i].stat_fb_align);
			seq_printf(seq, "\tFallback due to SG numbers: %lu\n",
				   ss_algs[i].stat_fb_sgnum);
			break;
		case CRYPTO_ALG_TYPE_RNG:
			seq_printf(seq, "%s %s reqs=%lu tsize=%lu\n",
				   ss_algs[i].alg.rng.base.cra_driver_name,
				   ss_algs[i].alg.rng.base.cra_name,
				   ss_algs[i].stat_req, ss_algs[i].stat_bytes);
			break;
		case CRYPTO_ALG_TYPE_AHASH:
			seq_printf(seq, "%s %s reqs=%lu fallback=%lu\n",
				   ss_algs[i].alg.hash.base.halg.base.cra_driver_name,
				   ss_algs[i].alg.hash.base.halg.base.cra_name,
				   ss_algs[i].stat_req, ss_algs[i].stat_fb);
			seq_printf(seq, "\tLast fallback is: %s\n",
				   ss_algs[i].fbname);
			seq_printf(seq, "\tFallback due to length: %lu\n",
				   ss_algs[i].stat_fb_len);
			seq_printf(seq, "\tFallback due to SG length: %lu\n",
				   ss_algs[i].stat_fb_sglen);
			seq_printf(seq, "\tFallback due to alignment: %lu\n",
				   ss_algs[i].stat_fb_align);
			seq_printf(seq, "\tFallback due to SG numbers: %lu\n",
				   ss_algs[i].stat_fb_sgnum);
			break;
		}
	}
	return 0;
}

DEFINE_SHOW_ATTRIBUTE(sun8i_ss_debugfs);

static void sun8i_ss_free_flows(struct sun8i_ss_dev *ss, int i)
{
	while (i >= 0) {
		crypto_engine_exit(ss->flows[i].engine);
		i--;
	}
}

/*
 * Allocate the flow list structure
 */
static int allocate_flows(struct sun8i_ss_dev *ss)
{
	int i, j, err;

	ss->flows = devm_kcalloc(ss->dev, MAXFLOW, sizeof(struct sun8i_ss_flow),
				 GFP_KERNEL);
	if (!ss->flows)
		return -ENOMEM;

	for (i = 0; i < MAXFLOW; i++) {
		init_completion(&ss->flows[i].complete);

		ss->flows[i].biv = devm_kmalloc(ss->dev, AES_BLOCK_SIZE,
						GFP_KERNEL);
		if (!ss->flows[i].biv) {
			err = -ENOMEM;
			goto error_engine;
		}

		for (j = 0; j < MAX_SG; j++) {
			ss->flows[i].iv[j] = devm_kmalloc(ss->dev, AES_BLOCK_SIZE,
							  GFP_KERNEL);
			if (!ss->flows[i].iv[j]) {
				err = -ENOMEM;
				goto error_engine;
			}
		}

		/* the padding could be up to two block. */
		ss->flows[i].pad = devm_kmalloc(ss->dev, MAX_PAD_SIZE,
						GFP_KERNEL);
		if (!ss->flows[i].pad) {
			err = -ENOMEM;
			goto error_engine;
		}
		ss->flows[i].result =
			devm_kmalloc(ss->dev, max(SHA256_DIGEST_SIZE,
						  dma_get_cache_alignment()),
				     GFP_KERNEL);
		if (!ss->flows[i].result) {
			err = -ENOMEM;
			goto error_engine;
		}

		ss->flows[i].engine = crypto_engine_alloc_init(ss->dev, true);
		if (!ss->flows[i].engine) {
			dev_err(ss->dev, "Cannot allocate engine\n");
			i--;
			err = -ENOMEM;
			goto error_engine;
		}
		err = crypto_engine_start(ss->flows[i].engine);
		if (err) {
			dev_err(ss->dev, "Cannot start engine\n");
			goto error_engine;
		}
	}
	return 0;
error_engine:
	sun8i_ss_free_flows(ss, i);
	return err;
}

/*
 * Power management strategy: The device is suspended unless a TFM exists for
 * one of the algorithms proposed by this driver.
 */
static int sun8i_ss_pm_suspend(struct device *dev)
{
	struct sun8i_ss_dev *ss = dev_get_drvdata(dev);
	int i;

	reset_control_assert(ss->reset);
	for (i = 0; i < SS_MAX_CLOCKS; i++)
		clk_disable_unprepare(ss->ssclks[i]);
	return 0;
}

static int sun8i_ss_pm_resume(struct device *dev)
{
	struct sun8i_ss_dev *ss = dev_get_drvdata(dev);
	int err, i;

	for (i = 0; i < SS_MAX_CLOCKS; i++) {
		if (!ss->variant->ss_clks[i].name)
			continue;
		err = clk_prepare_enable(ss->ssclks[i]);
		if (err) {
			dev_err(ss->dev, "Cannot prepare_enable %s\n",
				ss->variant->ss_clks[i].name);
			goto error;
		}
	}
	err = reset_control_deassert(ss->reset);
	if (err) {
		dev_err(ss->dev, "Cannot deassert reset control\n");
		goto error;
	}
	/* enable interrupts for all flows */
	writel(BIT(0) | BIT(1), ss->base + SS_INT_CTL_REG);

	return 0;
error:
	sun8i_ss_pm_suspend(dev);
	return err;
}

static const struct dev_pm_ops sun8i_ss_pm_ops = {
	SET_RUNTIME_PM_OPS(sun8i_ss_pm_suspend, sun8i_ss_pm_resume, NULL)
};

static int sun8i_ss_pm_init(struct sun8i_ss_dev *ss)
{
	int err;

	pm_runtime_use_autosuspend(ss->dev);
	pm_runtime_set_autosuspend_delay(ss->dev, 2000);

	err = pm_runtime_set_suspended(ss->dev);
	if (err)
		return err;
	pm_runtime_enable(ss->dev);
	return err;
}

static void sun8i_ss_pm_exit(struct sun8i_ss_dev *ss)
{
	pm_runtime_disable(ss->dev);
}

static int sun8i_ss_register_algs(struct sun8i_ss_dev *ss)
{
	int ss_method, err, id;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(ss_algs); i++) {
		ss_algs[i].ss = ss;
		switch (ss_algs[i].type) {
		case CRYPTO_ALG_TYPE_SKCIPHER:
			id = ss_algs[i].ss_algo_id;
			ss_method = ss->variant->alg_cipher[id];
			if (ss_method == SS_ID_NOTSUPP) {
				dev_info(ss->dev,
					 "DEBUG: Algo of %s not supported\n",
					 ss_algs[i].alg.skcipher.base.base.cra_name);
				ss_algs[i].ss = NULL;
				break;
			}
			id = ss_algs[i].ss_blockmode;
			ss_method = ss->variant->op_mode[id];
			if (ss_method == SS_ID_NOTSUPP) {
				dev_info(ss->dev, "DEBUG: Blockmode of %s not supported\n",
					 ss_algs[i].alg.skcipher.base.base.cra_name);
				ss_algs[i].ss = NULL;
				break;
			}
			dev_info(ss->dev, "DEBUG: Register %s\n",
				 ss_algs[i].alg.skcipher.base.base.cra_name);
			err = crypto_engine_register_skcipher(&ss_algs[i].alg.skcipher);
			if (err) {
				dev_err(ss->dev, "Fail to register %s\n",
					ss_algs[i].alg.skcipher.base.base.cra_name);
				ss_algs[i].ss = NULL;
				return err;
			}
			break;
		case CRYPTO_ALG_TYPE_RNG:
			err = crypto_register_rng(&ss_algs[i].alg.rng);
			if (err) {
				dev_err(ss->dev, "Fail to register %s\n",
					ss_algs[i].alg.rng.base.cra_name);
				ss_algs[i].ss = NULL;
			}
			break;
		case CRYPTO_ALG_TYPE_AHASH:
			id = ss_algs[i].ss_algo_id;
			ss_method = ss->variant->alg_hash[id];
			if (ss_method == SS_ID_NOTSUPP) {
				dev_info(ss->dev,
					"DEBUG: Algo of %s not supported\n",
					ss_algs[i].alg.hash.base.halg.base.cra_name);
				ss_algs[i].ss = NULL;
				break;
			}
			dev_info(ss->dev, "Register %s\n",
				 ss_algs[i].alg.hash.base.halg.base.cra_name);
			err = crypto_engine_register_ahash(&ss_algs[i].alg.hash);
			if (err) {
				dev_err(ss->dev, "ERROR: Fail to register %s\n",
					ss_algs[i].alg.hash.base.halg.base.cra_name);
				ss_algs[i].ss = NULL;
				return err;
			}
			break;
		default:
			ss_algs[i].ss = NULL;
			dev_err(ss->dev, "ERROR: tried to register an unknown algo\n");
		}
	}
	return 0;
}

static void sun8i_ss_unregister_algs(struct sun8i_ss_dev *ss)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(ss_algs); i++) {
		if (!ss_algs[i].ss)
			continue;
		switch (ss_algs[i].type) {
		case CRYPTO_ALG_TYPE_SKCIPHER:
			dev_info(ss->dev, "Unregister %d %s\n", i,
				 ss_algs[i].alg.skcipher.base.base.cra_name);
			crypto_engine_unregister_skcipher(&ss_algs[i].alg.skcipher);
			break;
		case CRYPTO_ALG_TYPE_RNG:
			dev_info(ss->dev, "Unregister %d %s\n", i,
				 ss_algs[i].alg.rng.base.cra_name);
			crypto_unregister_rng(&ss_algs[i].alg.rng);
			break;
		case CRYPTO_ALG_TYPE_AHASH:
			dev_info(ss->dev, "Unregister %d %s\n", i,
				 ss_algs[i].alg.hash.base.halg.base.cra_name);
			crypto_engine_unregister_ahash(&ss_algs[i].alg.hash);
			break;
		}
	}
}

static int sun8i_ss_get_clks(struct sun8i_ss_dev *ss)
{
	unsigned long cr;
	int err, i;

	for (i = 0; i < SS_MAX_CLOCKS; i++) {
		if (!ss->variant->ss_clks[i].name)
			continue;
		ss->ssclks[i] = devm_clk_get(ss->dev, ss->variant->ss_clks[i].name);
		if (IS_ERR(ss->ssclks[i])) {
			err = PTR_ERR(ss->ssclks[i]);
			dev_err(ss->dev, "Cannot get %s SS clock err=%d\n",
				ss->variant->ss_clks[i].name, err);
			return err;
		}
		cr = clk_get_rate(ss->ssclks[i]);
		if (!cr)
			return -EINVAL;
		if (ss->variant->ss_clks[i].freq > 0 &&
		    cr != ss->variant->ss_clks[i].freq) {
			dev_info(ss->dev, "Set %s clock to %lu (%lu Mhz) from %lu (%lu Mhz)\n",
				 ss->variant->ss_clks[i].name,
				 ss->variant->ss_clks[i].freq,
				 ss->variant->ss_clks[i].freq / 1000000,
				 cr, cr / 1000000);
			err = clk_set_rate(ss->ssclks[i], ss->variant->ss_clks[i].freq);
			if (err)
				dev_err(ss->dev, "Fail to set %s clk speed to %lu hz\n",
					ss->variant->ss_clks[i].name,
					ss->variant->ss_clks[i].freq);
		}
		if (ss->variant->ss_clks[i].max_freq > 0 &&
		    cr > ss->variant->ss_clks[i].max_freq)
			dev_warn(ss->dev, "Frequency for %s (%lu hz) is higher than datasheet's recommendation (%lu hz)",
				 ss->variant->ss_clks[i].name, cr,
				 ss->variant->ss_clks[i].max_freq);
	}
	return 0;
}

static int sun8i_ss_probe(struct platform_device *pdev)
{
	struct sun8i_ss_dev *ss;
	int err, irq;
	u32 v;

	ss = devm_kzalloc(&pdev->dev, sizeof(*ss), GFP_KERNEL);
	if (!ss)
		return -ENOMEM;

	ss->dev = &pdev->dev;
	platform_set_drvdata(pdev, ss);

	ss->variant = of_device_get_match_data(&pdev->dev);
	if (!ss->variant) {
		dev_err(&pdev->dev, "Missing Crypto Engine variant\n");
		return -EINVAL;
	}

	ss->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(ss->base))
		return PTR_ERR(ss->base);

	err = sun8i_ss_get_clks(ss);
	if (err)
		return err;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	ss->reset = devm_reset_control_get(&pdev->dev, NULL);
	if (IS_ERR(ss->reset))
		return dev_err_probe(&pdev->dev, PTR_ERR(ss->reset),
				     "No reset control found\n");

	mutex_init(&ss->mlock);

	err = allocate_flows(ss);
	if (err)
		return err;

	err = sun8i_ss_pm_init(ss);
	if (err)
		goto error_pm;

	err = devm_request_irq(&pdev->dev, irq, ss_irq_handler, 0, "sun8i-ss", ss);
	if (err) {
		dev_err(ss->dev, "Cannot request SecuritySystem IRQ (err=%d)\n", err);
		goto error_irq;
	}

	err = sun8i_ss_register_algs(ss);
	if (err)
		goto error_alg;

	err = pm_runtime_resume_and_get(ss->dev);
	if (err < 0)
		goto error_alg;

	v = readl(ss->base + SS_CTL_REG);
	v >>= SS_DIE_ID_SHIFT;
	v &= SS_DIE_ID_MASK;
	dev_info(&pdev->dev, "Security System Die ID %x\n", v);

	pm_runtime_put_sync(ss->dev);

	if (IS_ENABLED(CONFIG_CRYPTO_DEV_SUN8I_SS_DEBUG)) {
		struct dentry *dbgfs_dir __maybe_unused;
		struct dentry *dbgfs_stats __maybe_unused;

		/* Ignore error of debugfs */
		dbgfs_dir = debugfs_create_dir("sun8i-ss", NULL);
		dbgfs_stats = debugfs_create_file("stats", 0444,
						   dbgfs_dir, ss,
						   &sun8i_ss_debugfs_fops);

#ifdef CONFIG_CRYPTO_DEV_SUN8I_SS_DEBUG
		ss->dbgfs_dir = dbgfs_dir;
		ss->dbgfs_stats = dbgfs_stats;
#endif
	}

	return 0;
error_alg:
	sun8i_ss_unregister_algs(ss);
error_irq:
	sun8i_ss_pm_exit(ss);
error_pm:
	sun8i_ss_free_flows(ss, MAXFLOW - 1);
	return err;
}

static void sun8i_ss_remove(struct platform_device *pdev)
{
	struct sun8i_ss_dev *ss = platform_get_drvdata(pdev);

	sun8i_ss_unregister_algs(ss);

#ifdef CONFIG_CRYPTO_DEV_SUN8I_SS_DEBUG
	debugfs_remove_recursive(ss->dbgfs_dir);
#endif

	sun8i_ss_free_flows(ss, MAXFLOW - 1);

	sun8i_ss_pm_exit(ss);
}

static const struct of_device_id sun8i_ss_crypto_of_match_table[] = {
	{ .compatible = "allwinner,sun8i-a83t-crypto",
	  .data = &ss_a83t_variant },
	{ .compatible = "allwinner,sun9i-a80-crypto",
	  .data = &ss_a80_variant },
	{}
};
MODULE_DEVICE_TABLE(of, sun8i_ss_crypto_of_match_table);

static struct platform_driver sun8i_ss_driver = {
	.probe		 = sun8i_ss_probe,
	.remove		 = sun8i_ss_remove,
	.driver		 = {
		.name		= "sun8i-ss",
		.pm             = &sun8i_ss_pm_ops,
		.of_match_table	= sun8i_ss_crypto_of_match_table,
	},
};

module_platform_driver(sun8i_ss_driver);

MODULE_DESCRIPTION("Allwinner SecuritySystem cryptographic offloader");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Corentin Labbe <clabbe.montjoie@gmail.com>");

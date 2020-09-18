// SPDX-License-Identifier: GPL-2.0
/*
 * sun8i-ce-core.c - hardware cryptographic offloader for
 * Allwinner H3/A64/H5/H2+/H6/R40 SoC
 *
 * Copyright (C) 2015-2019 Corentin Labbe <clabbe.montjoie@gmail.com>
 *
 * Core file which registers crypto algorithms supported by the CryptoEngine.
 *
 * You could find a link for the datasheet in Documentation/arm/sunxi.rst
 */
#include <linux/clk.h>
#include <linux/crypto.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>
#include <crypto/internal/rng.h>
#include <crypto/internal/skcipher.h>

#include "sun8i-ce.h"

/*
 * mod clock is lower on H3 than other SoC due to some DMA timeout occurring
 * with high value.
 * If you want to tune mod clock, loading driver and passing selftest is
 * insufficient, you need to test with some LUKS test (mount and write to it)
 */
static const struct ce_variant ce_h3_variant = {
	.alg_cipher = { CE_ALG_AES, CE_ALG_DES, CE_ALG_3DES,
	},
	.alg_hash = { CE_ALG_MD5, CE_ALG_SHA1, CE_ALG_SHA224, CE_ALG_SHA256,
		CE_ALG_SHA384, CE_ALG_SHA512
	},
	.op_mode = { CE_OP_ECB, CE_OP_CBC
	},
	.ce_clks = {
		{ "bus", 0, 200000000 },
		{ "mod", 50000000, 0 },
		},
	.esr = ESR_H3,
	.prng = CE_ALG_PRNG,
};

static const struct ce_variant ce_h5_variant = {
	.alg_cipher = { CE_ALG_AES, CE_ALG_DES, CE_ALG_3DES,
	},
	.alg_hash = { CE_ALG_MD5, CE_ALG_SHA1, CE_ALG_SHA224, CE_ALG_SHA256,
		CE_ID_NOTSUPP, CE_ID_NOTSUPP
	},
	.op_mode = { CE_OP_ECB, CE_OP_CBC
	},
	.ce_clks = {
		{ "bus", 0, 200000000 },
		{ "mod", 300000000, 0 },
		},
	.esr = ESR_H5,
	.prng = CE_ALG_PRNG,
};

static const struct ce_variant ce_h6_variant = {
	.alg_cipher = { CE_ALG_AES, CE_ALG_DES, CE_ALG_3DES,
	},
	.alg_hash = { CE_ALG_MD5, CE_ALG_SHA1, CE_ALG_SHA224, CE_ALG_SHA256,
		CE_ALG_SHA384, CE_ALG_SHA512
	},
	.op_mode = { CE_OP_ECB, CE_OP_CBC
	},
	.cipher_t_dlen_in_bytes = true,
	.hash_t_dlen_in_bits = true,
	.prng_t_dlen_in_bytes = true,
	.ce_clks = {
		{ "bus", 0, 200000000 },
		{ "mod", 300000000, 0 },
		{ "ram", 0, 400000000 },
		},
	.esr = ESR_H6,
	.prng = CE_ALG_PRNG_V2,
};

static const struct ce_variant ce_a64_variant = {
	.alg_cipher = { CE_ALG_AES, CE_ALG_DES, CE_ALG_3DES,
	},
	.alg_hash = { CE_ALG_MD5, CE_ALG_SHA1, CE_ALG_SHA224, CE_ALG_SHA256,
		CE_ID_NOTSUPP, CE_ID_NOTSUPP
	},
	.op_mode = { CE_OP_ECB, CE_OP_CBC
	},
	.ce_clks = {
		{ "bus", 0, 200000000 },
		{ "mod", 300000000, 0 },
		},
	.esr = ESR_A64,
	.prng = CE_ALG_PRNG,
};

static const struct ce_variant ce_r40_variant = {
	.alg_cipher = { CE_ALG_AES, CE_ALG_DES, CE_ALG_3DES,
	},
	.alg_hash = { CE_ALG_MD5, CE_ALG_SHA1, CE_ALG_SHA224, CE_ALG_SHA256,
		CE_ID_NOTSUPP, CE_ID_NOTSUPP
	},
	.op_mode = { CE_OP_ECB, CE_OP_CBC
	},
	.ce_clks = {
		{ "bus", 0, 200000000 },
		{ "mod", 300000000, 0 },
		},
	.esr = ESR_R40,
	.prng = CE_ALG_PRNG,
};

/*
 * sun8i_ce_get_engine_number() get the next channel slot
 * This is a simple round-robin way of getting the next channel
 * The flow 3 is reserve for xRNG operations
 */
int sun8i_ce_get_engine_number(struct sun8i_ce_dev *ce)
{
	return atomic_inc_return(&ce->flow) % (MAXFLOW - 1);
}

int sun8i_ce_run_task(struct sun8i_ce_dev *ce, int flow, const char *name)
{
	u32 v;
	int err = 0;
	struct ce_task *cet = ce->chanlist[flow].tl;

#ifdef CONFIG_CRYPTO_DEV_SUN8I_CE_DEBUG
	ce->chanlist[flow].stat_req++;
#endif

	mutex_lock(&ce->mlock);

	v = readl(ce->base + CE_ICR);
	v |= 1 << flow;
	writel(v, ce->base + CE_ICR);

	reinit_completion(&ce->chanlist[flow].complete);
	writel(ce->chanlist[flow].t_phy, ce->base + CE_TDQ);

	ce->chanlist[flow].status = 0;
	/* Be sure all data is written before enabling the task */
	wmb();

	/* Only H6 needs to write a part of t_common_ctl along with "1", but since it is ignored
	 * on older SoCs, we have no reason to complicate things.
	 */
	v = 1 | ((le32_to_cpu(ce->chanlist[flow].tl->t_common_ctl) & 0x7F) << 8);
	writel(v, ce->base + CE_TLR);
	mutex_unlock(&ce->mlock);

	wait_for_completion_interruptible_timeout(&ce->chanlist[flow].complete,
			msecs_to_jiffies(ce->chanlist[flow].timeout));

	if (ce->chanlist[flow].status == 0) {
		dev_err(ce->dev, "DMA timeout for %s (tm=%d) on flow %d\n", name,
			ce->chanlist[flow].timeout, flow);
		err = -EFAULT;
	}
	/* No need to lock for this read, the channel is locked so
	 * nothing could modify the error value for this channel
	 */
	v = readl(ce->base + CE_ESR);
	switch (ce->variant->esr) {
	case ESR_H3:
		/* Sadly, the error bit is not per flow */
		if (v) {
			dev_err(ce->dev, "CE ERROR: %x for flow %x\n", v, flow);
			err = -EFAULT;
			print_hex_dump(KERN_INFO, "TASK: ", DUMP_PREFIX_NONE, 16, 4,
				       cet, sizeof(struct ce_task), false);
		}
		if (v & CE_ERR_ALGO_NOTSUP)
			dev_err(ce->dev, "CE ERROR: algorithm not supported\n");
		if (v & CE_ERR_DATALEN)
			dev_err(ce->dev, "CE ERROR: data length error\n");
		if (v & CE_ERR_KEYSRAM)
			dev_err(ce->dev, "CE ERROR: keysram access error for AES\n");
		break;
	case ESR_A64:
	case ESR_H5:
	case ESR_R40:
		v >>= (flow * 4);
		v &= 0xF;
		if (v) {
			dev_err(ce->dev, "CE ERROR: %x for flow %x\n", v, flow);
			err = -EFAULT;
			print_hex_dump(KERN_INFO, "TASK: ", DUMP_PREFIX_NONE, 16, 4,
				       cet, sizeof(struct ce_task), false);
		}
		if (v & CE_ERR_ALGO_NOTSUP)
			dev_err(ce->dev, "CE ERROR: algorithm not supported\n");
		if (v & CE_ERR_DATALEN)
			dev_err(ce->dev, "CE ERROR: data length error\n");
		if (v & CE_ERR_KEYSRAM)
			dev_err(ce->dev, "CE ERROR: keysram access error for AES\n");
		break;
	case ESR_H6:
		v >>= (flow * 8);
		v &= 0xFF;
		if (v) {
			dev_err(ce->dev, "CE ERROR: %x for flow %x\n", v, flow);
			err = -EFAULT;
			print_hex_dump(KERN_INFO, "TASK: ", DUMP_PREFIX_NONE, 16, 4,
				       cet, sizeof(struct ce_task), false);
		}
		if (v & CE_ERR_ALGO_NOTSUP)
			dev_err(ce->dev, "CE ERROR: algorithm not supported\n");
		if (v & CE_ERR_DATALEN)
			dev_err(ce->dev, "CE ERROR: data length error\n");
		if (v & CE_ERR_KEYSRAM)
			dev_err(ce->dev, "CE ERROR: keysram access error for AES\n");
		if (v & CE_ERR_ADDR_INVALID)
			dev_err(ce->dev, "CE ERROR: address invalid\n");
		if (v & CE_ERR_KEYLADDER)
			dev_err(ce->dev, "CE ERROR: key ladder configuration error\n");
		break;
	}

	return err;
}

static irqreturn_t ce_irq_handler(int irq, void *data)
{
	struct sun8i_ce_dev *ce = (struct sun8i_ce_dev *)data;
	int flow = 0;
	u32 p;

	p = readl(ce->base + CE_ISR);
	for (flow = 0; flow < MAXFLOW; flow++) {
		if (p & (BIT(flow))) {
			writel(BIT(flow), ce->base + CE_ISR);
			ce->chanlist[flow].status = 1;
			complete(&ce->chanlist[flow].complete);
		}
	}

	return IRQ_HANDLED;
}

static struct sun8i_ce_alg_template ce_algs[] = {
{
	.type = CRYPTO_ALG_TYPE_SKCIPHER,
	.ce_algo_id = CE_ID_CIPHER_AES,
	.ce_blockmode = CE_ID_OP_CBC,
	.alg.skcipher = {
		.base = {
			.cra_name = "cbc(aes)",
			.cra_driver_name = "cbc-aes-sun8i-ce",
			.cra_priority = 400,
			.cra_blocksize = AES_BLOCK_SIZE,
			.cra_flags = CRYPTO_ALG_TYPE_SKCIPHER |
				CRYPTO_ALG_ASYNC | CRYPTO_ALG_ALLOCATES_MEMORY |
				CRYPTO_ALG_NEED_FALLBACK,
			.cra_ctxsize = sizeof(struct sun8i_cipher_tfm_ctx),
			.cra_module = THIS_MODULE,
			.cra_alignmask = 0xf,
			.cra_init = sun8i_ce_cipher_init,
			.cra_exit = sun8i_ce_cipher_exit,
		},
		.min_keysize	= AES_MIN_KEY_SIZE,
		.max_keysize	= AES_MAX_KEY_SIZE,
		.ivsize		= AES_BLOCK_SIZE,
		.setkey		= sun8i_ce_aes_setkey,
		.encrypt	= sun8i_ce_skencrypt,
		.decrypt	= sun8i_ce_skdecrypt,
	}
},
{
	.type = CRYPTO_ALG_TYPE_SKCIPHER,
	.ce_algo_id = CE_ID_CIPHER_AES,
	.ce_blockmode = CE_ID_OP_ECB,
	.alg.skcipher = {
		.base = {
			.cra_name = "ecb(aes)",
			.cra_driver_name = "ecb-aes-sun8i-ce",
			.cra_priority = 400,
			.cra_blocksize = AES_BLOCK_SIZE,
			.cra_flags = CRYPTO_ALG_TYPE_SKCIPHER |
				CRYPTO_ALG_ASYNC | CRYPTO_ALG_ALLOCATES_MEMORY |
				CRYPTO_ALG_NEED_FALLBACK,
			.cra_ctxsize = sizeof(struct sun8i_cipher_tfm_ctx),
			.cra_module = THIS_MODULE,
			.cra_alignmask = 0xf,
			.cra_init = sun8i_ce_cipher_init,
			.cra_exit = sun8i_ce_cipher_exit,
		},
		.min_keysize	= AES_MIN_KEY_SIZE,
		.max_keysize	= AES_MAX_KEY_SIZE,
		.setkey		= sun8i_ce_aes_setkey,
		.encrypt	= sun8i_ce_skencrypt,
		.decrypt	= sun8i_ce_skdecrypt,
	}
},
{
	.type = CRYPTO_ALG_TYPE_SKCIPHER,
	.ce_algo_id = CE_ID_CIPHER_DES3,
	.ce_blockmode = CE_ID_OP_CBC,
	.alg.skcipher = {
		.base = {
			.cra_name = "cbc(des3_ede)",
			.cra_driver_name = "cbc-des3-sun8i-ce",
			.cra_priority = 400,
			.cra_blocksize = DES3_EDE_BLOCK_SIZE,
			.cra_flags = CRYPTO_ALG_TYPE_SKCIPHER |
				CRYPTO_ALG_ASYNC | CRYPTO_ALG_ALLOCATES_MEMORY |
				CRYPTO_ALG_NEED_FALLBACK,
			.cra_ctxsize = sizeof(struct sun8i_cipher_tfm_ctx),
			.cra_module = THIS_MODULE,
			.cra_alignmask = 0xf,
			.cra_init = sun8i_ce_cipher_init,
			.cra_exit = sun8i_ce_cipher_exit,
		},
		.min_keysize	= DES3_EDE_KEY_SIZE,
		.max_keysize	= DES3_EDE_KEY_SIZE,
		.ivsize		= DES3_EDE_BLOCK_SIZE,
		.setkey		= sun8i_ce_des3_setkey,
		.encrypt	= sun8i_ce_skencrypt,
		.decrypt	= sun8i_ce_skdecrypt,
	}
},
{
	.type = CRYPTO_ALG_TYPE_SKCIPHER,
	.ce_algo_id = CE_ID_CIPHER_DES3,
	.ce_blockmode = CE_ID_OP_ECB,
	.alg.skcipher = {
		.base = {
			.cra_name = "ecb(des3_ede)",
			.cra_driver_name = "ecb-des3-sun8i-ce",
			.cra_priority = 400,
			.cra_blocksize = DES3_EDE_BLOCK_SIZE,
			.cra_flags = CRYPTO_ALG_TYPE_SKCIPHER |
				CRYPTO_ALG_ASYNC | CRYPTO_ALG_ALLOCATES_MEMORY |
				CRYPTO_ALG_NEED_FALLBACK,
			.cra_ctxsize = sizeof(struct sun8i_cipher_tfm_ctx),
			.cra_module = THIS_MODULE,
			.cra_alignmask = 0xf,
			.cra_init = sun8i_ce_cipher_init,
			.cra_exit = sun8i_ce_cipher_exit,
		},
		.min_keysize	= DES3_EDE_KEY_SIZE,
		.max_keysize	= DES3_EDE_KEY_SIZE,
		.setkey		= sun8i_ce_des3_setkey,
		.encrypt	= sun8i_ce_skencrypt,
		.decrypt	= sun8i_ce_skdecrypt,
	}
},
#ifdef CONFIG_CRYPTO_DEV_SUN8I_CE_HASH
{	.type = CRYPTO_ALG_TYPE_AHASH,
	.ce_algo_id = CE_ID_HASH_MD5,
	.alg.hash = {
		.init = sun8i_ce_hash_init,
		.update = sun8i_ce_hash_update,
		.final = sun8i_ce_hash_final,
		.finup = sun8i_ce_hash_finup,
		.digest = sun8i_ce_hash_digest,
		.export = sun8i_ce_hash_export,
		.import = sun8i_ce_hash_import,
		.halg = {
			.digestsize = MD5_DIGEST_SIZE,
			.statesize = sizeof(struct md5_state),
			.base = {
				.cra_name = "md5",
				.cra_driver_name = "md5-sun8i-ce",
				.cra_priority = 300,
				.cra_alignmask = 3,
				.cra_flags = CRYPTO_ALG_TYPE_AHASH |
					CRYPTO_ALG_ASYNC |
					CRYPTO_ALG_NEED_FALLBACK,
				.cra_blocksize = MD5_HMAC_BLOCK_SIZE,
				.cra_ctxsize = sizeof(struct sun8i_ce_hash_tfm_ctx),
				.cra_module = THIS_MODULE,
				.cra_init = sun8i_ce_hash_crainit,
				.cra_exit = sun8i_ce_hash_craexit,
			}
		}
	}
},
{	.type = CRYPTO_ALG_TYPE_AHASH,
	.ce_algo_id = CE_ID_HASH_SHA1,
	.alg.hash = {
		.init = sun8i_ce_hash_init,
		.update = sun8i_ce_hash_update,
		.final = sun8i_ce_hash_final,
		.finup = sun8i_ce_hash_finup,
		.digest = sun8i_ce_hash_digest,
		.export = sun8i_ce_hash_export,
		.import = sun8i_ce_hash_import,
		.halg = {
			.digestsize = SHA1_DIGEST_SIZE,
			.statesize = sizeof(struct sha1_state),
			.base = {
				.cra_name = "sha1",
				.cra_driver_name = "sha1-sun8i-ce",
				.cra_priority = 300,
				.cra_alignmask = 3,
				.cra_flags = CRYPTO_ALG_TYPE_AHASH |
					CRYPTO_ALG_ASYNC |
					CRYPTO_ALG_NEED_FALLBACK,
				.cra_blocksize = SHA1_BLOCK_SIZE,
				.cra_ctxsize = sizeof(struct sun8i_ce_hash_tfm_ctx),
				.cra_module = THIS_MODULE,
				.cra_init = sun8i_ce_hash_crainit,
				.cra_exit = sun8i_ce_hash_craexit,
			}
		}
	}
},
{	.type = CRYPTO_ALG_TYPE_AHASH,
	.ce_algo_id = CE_ID_HASH_SHA224,
	.alg.hash = {
		.init = sun8i_ce_hash_init,
		.update = sun8i_ce_hash_update,
		.final = sun8i_ce_hash_final,
		.finup = sun8i_ce_hash_finup,
		.digest = sun8i_ce_hash_digest,
		.export = sun8i_ce_hash_export,
		.import = sun8i_ce_hash_import,
		.halg = {
			.digestsize = SHA224_DIGEST_SIZE,
			.statesize = sizeof(struct sha256_state),
			.base = {
				.cra_name = "sha224",
				.cra_driver_name = "sha224-sun8i-ce",
				.cra_priority = 300,
				.cra_alignmask = 3,
				.cra_flags = CRYPTO_ALG_TYPE_AHASH |
					CRYPTO_ALG_ASYNC |
					CRYPTO_ALG_NEED_FALLBACK,
				.cra_blocksize = SHA224_BLOCK_SIZE,
				.cra_ctxsize = sizeof(struct sun8i_ce_hash_tfm_ctx),
				.cra_module = THIS_MODULE,
				.cra_init = sun8i_ce_hash_crainit,
				.cra_exit = sun8i_ce_hash_craexit,
			}
		}
	}
},
{	.type = CRYPTO_ALG_TYPE_AHASH,
	.ce_algo_id = CE_ID_HASH_SHA256,
	.alg.hash = {
		.init = sun8i_ce_hash_init,
		.update = sun8i_ce_hash_update,
		.final = sun8i_ce_hash_final,
		.finup = sun8i_ce_hash_finup,
		.digest = sun8i_ce_hash_digest,
		.export = sun8i_ce_hash_export,
		.import = sun8i_ce_hash_import,
		.halg = {
			.digestsize = SHA256_DIGEST_SIZE,
			.statesize = sizeof(struct sha256_state),
			.base = {
				.cra_name = "sha256",
				.cra_driver_name = "sha256-sun8i-ce",
				.cra_priority = 300,
				.cra_alignmask = 3,
				.cra_flags = CRYPTO_ALG_TYPE_AHASH |
					CRYPTO_ALG_ASYNC |
					CRYPTO_ALG_NEED_FALLBACK,
				.cra_blocksize = SHA256_BLOCK_SIZE,
				.cra_ctxsize = sizeof(struct sun8i_ce_hash_tfm_ctx),
				.cra_module = THIS_MODULE,
				.cra_init = sun8i_ce_hash_crainit,
				.cra_exit = sun8i_ce_hash_craexit,
			}
		}
	}
},
{	.type = CRYPTO_ALG_TYPE_AHASH,
	.ce_algo_id = CE_ID_HASH_SHA384,
	.alg.hash = {
		.init = sun8i_ce_hash_init,
		.update = sun8i_ce_hash_update,
		.final = sun8i_ce_hash_final,
		.finup = sun8i_ce_hash_finup,
		.digest = sun8i_ce_hash_digest,
		.export = sun8i_ce_hash_export,
		.import = sun8i_ce_hash_import,
		.halg = {
			.digestsize = SHA384_DIGEST_SIZE,
			.statesize = sizeof(struct sha512_state),
			.base = {
				.cra_name = "sha384",
				.cra_driver_name = "sha384-sun8i-ce",
				.cra_priority = 300,
				.cra_alignmask = 3,
				.cra_flags = CRYPTO_ALG_TYPE_AHASH |
					CRYPTO_ALG_ASYNC |
					CRYPTO_ALG_NEED_FALLBACK,
				.cra_blocksize = SHA384_BLOCK_SIZE,
				.cra_ctxsize = sizeof(struct sun8i_ce_hash_tfm_ctx),
				.cra_module = THIS_MODULE,
				.cra_init = sun8i_ce_hash_crainit,
				.cra_exit = sun8i_ce_hash_craexit,
			}
		}
	}
},
{	.type = CRYPTO_ALG_TYPE_AHASH,
	.ce_algo_id = CE_ID_HASH_SHA512,
	.alg.hash = {
		.init = sun8i_ce_hash_init,
		.update = sun8i_ce_hash_update,
		.final = sun8i_ce_hash_final,
		.finup = sun8i_ce_hash_finup,
		.digest = sun8i_ce_hash_digest,
		.export = sun8i_ce_hash_export,
		.import = sun8i_ce_hash_import,
		.halg = {
			.digestsize = SHA512_DIGEST_SIZE,
			.statesize = sizeof(struct sha512_state),
			.base = {
				.cra_name = "sha512",
				.cra_driver_name = "sha512-sun8i-ce",
				.cra_priority = 300,
				.cra_alignmask = 3,
				.cra_flags = CRYPTO_ALG_TYPE_AHASH |
					CRYPTO_ALG_ASYNC |
					CRYPTO_ALG_NEED_FALLBACK,
				.cra_blocksize = SHA512_BLOCK_SIZE,
				.cra_ctxsize = sizeof(struct sun8i_ce_hash_tfm_ctx),
				.cra_module = THIS_MODULE,
				.cra_init = sun8i_ce_hash_crainit,
				.cra_exit = sun8i_ce_hash_craexit,
			}
		}
	}
},
#endif
#ifdef CONFIG_CRYPTO_DEV_SUN8I_CE_PRNG
{
	.type = CRYPTO_ALG_TYPE_RNG,
	.alg.rng = {
		.base = {
			.cra_name		= "stdrng",
			.cra_driver_name	= "sun8i-ce-prng",
			.cra_priority		= 300,
			.cra_ctxsize		= sizeof(struct sun8i_ce_rng_tfm_ctx),
			.cra_module		= THIS_MODULE,
			.cra_init		= sun8i_ce_prng_init,
			.cra_exit		= sun8i_ce_prng_exit,
		},
		.generate               = sun8i_ce_prng_generate,
		.seed                   = sun8i_ce_prng_seed,
		.seedsize               = PRNG_SEED_SIZE,
	}
},
#endif
};

#ifdef CONFIG_CRYPTO_DEV_SUN8I_CE_DEBUG
static int sun8i_ce_debugfs_show(struct seq_file *seq, void *v)
{
	struct sun8i_ce_dev *ce = seq->private;
	int i;

	for (i = 0; i < MAXFLOW; i++)
		seq_printf(seq, "Channel %d: nreq %lu\n", i, ce->chanlist[i].stat_req);

	for (i = 0; i < ARRAY_SIZE(ce_algs); i++) {
		if (!ce_algs[i].ce)
			continue;
		switch (ce_algs[i].type) {
		case CRYPTO_ALG_TYPE_SKCIPHER:
			seq_printf(seq, "%s %s %lu %lu\n",
				   ce_algs[i].alg.skcipher.base.cra_driver_name,
				   ce_algs[i].alg.skcipher.base.cra_name,
				   ce_algs[i].stat_req, ce_algs[i].stat_fb);
			break;
		case CRYPTO_ALG_TYPE_AHASH:
			seq_printf(seq, "%s %s %lu %lu\n",
				   ce_algs[i].alg.hash.halg.base.cra_driver_name,
				   ce_algs[i].alg.hash.halg.base.cra_name,
				   ce_algs[i].stat_req, ce_algs[i].stat_fb);
			break;
		case CRYPTO_ALG_TYPE_RNG:
			seq_printf(seq, "%s %s %lu %lu\n",
				   ce_algs[i].alg.rng.base.cra_driver_name,
				   ce_algs[i].alg.rng.base.cra_name,
				   ce_algs[i].stat_req, ce_algs[i].stat_bytes);
			break;
		}
	}
	return 0;
}

DEFINE_SHOW_ATTRIBUTE(sun8i_ce_debugfs);
#endif

static void sun8i_ce_free_chanlist(struct sun8i_ce_dev *ce, int i)
{
	while (i >= 0) {
		crypto_engine_exit(ce->chanlist[i].engine);
		if (ce->chanlist[i].tl)
			dma_free_coherent(ce->dev, sizeof(struct ce_task),
					  ce->chanlist[i].tl,
					  ce->chanlist[i].t_phy);
		i--;
	}
}

/*
 * Allocate the channel list structure
 */
static int sun8i_ce_allocate_chanlist(struct sun8i_ce_dev *ce)
{
	int i, err;

	ce->chanlist = devm_kcalloc(ce->dev, MAXFLOW,
				    sizeof(struct sun8i_ce_flow), GFP_KERNEL);
	if (!ce->chanlist)
		return -ENOMEM;

	for (i = 0; i < MAXFLOW; i++) {
		init_completion(&ce->chanlist[i].complete);

		ce->chanlist[i].engine = crypto_engine_alloc_init(ce->dev, true);
		if (!ce->chanlist[i].engine) {
			dev_err(ce->dev, "Cannot allocate engine\n");
			i--;
			err = -ENOMEM;
			goto error_engine;
		}
		err = crypto_engine_start(ce->chanlist[i].engine);
		if (err) {
			dev_err(ce->dev, "Cannot start engine\n");
			goto error_engine;
		}
		ce->chanlist[i].tl = dma_alloc_coherent(ce->dev,
							sizeof(struct ce_task),
							&ce->chanlist[i].t_phy,
							GFP_KERNEL);
		if (!ce->chanlist[i].tl) {
			dev_err(ce->dev, "Cannot get DMA memory for task %d\n",
				i);
			err = -ENOMEM;
			goto error_engine;
		}
	}
	return 0;
error_engine:
	sun8i_ce_free_chanlist(ce, i);
	return err;
}

/*
 * Power management strategy: The device is suspended unless a TFM exists for
 * one of the algorithms proposed by this driver.
 */
static int sun8i_ce_pm_suspend(struct device *dev)
{
	struct sun8i_ce_dev *ce = dev_get_drvdata(dev);
	int i;

	reset_control_assert(ce->reset);
	for (i = 0; i < CE_MAX_CLOCKS; i++)
		clk_disable_unprepare(ce->ceclks[i]);
	return 0;
}

static int sun8i_ce_pm_resume(struct device *dev)
{
	struct sun8i_ce_dev *ce = dev_get_drvdata(dev);
	int err, i;

	for (i = 0; i < CE_MAX_CLOCKS; i++) {
		if (!ce->variant->ce_clks[i].name)
			continue;
		err = clk_prepare_enable(ce->ceclks[i]);
		if (err) {
			dev_err(ce->dev, "Cannot prepare_enable %s\n",
				ce->variant->ce_clks[i].name);
			goto error;
		}
	}
	err = reset_control_deassert(ce->reset);
	if (err) {
		dev_err(ce->dev, "Cannot deassert reset control\n");
		goto error;
	}
	return 0;
error:
	sun8i_ce_pm_suspend(dev);
	return err;
}

static const struct dev_pm_ops sun8i_ce_pm_ops = {
	SET_RUNTIME_PM_OPS(sun8i_ce_pm_suspend, sun8i_ce_pm_resume, NULL)
};

static int sun8i_ce_pm_init(struct sun8i_ce_dev *ce)
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

static void sun8i_ce_pm_exit(struct sun8i_ce_dev *ce)
{
	pm_runtime_disable(ce->dev);
}

static int sun8i_ce_get_clks(struct sun8i_ce_dev *ce)
{
	unsigned long cr;
	int err, i;

	for (i = 0; i < CE_MAX_CLOCKS; i++) {
		if (!ce->variant->ce_clks[i].name)
			continue;
		ce->ceclks[i] = devm_clk_get(ce->dev, ce->variant->ce_clks[i].name);
		if (IS_ERR(ce->ceclks[i])) {
			err = PTR_ERR(ce->ceclks[i]);
			dev_err(ce->dev, "Cannot get %s CE clock err=%d\n",
				ce->variant->ce_clks[i].name, err);
			return err;
		}
		cr = clk_get_rate(ce->ceclks[i]);
		if (!cr)
			return -EINVAL;
		if (ce->variant->ce_clks[i].freq > 0 &&
		    cr != ce->variant->ce_clks[i].freq) {
			dev_info(ce->dev, "Set %s clock to %lu (%lu Mhz) from %lu (%lu Mhz)\n",
				 ce->variant->ce_clks[i].name,
				 ce->variant->ce_clks[i].freq,
				 ce->variant->ce_clks[i].freq / 1000000,
				 cr, cr / 1000000);
			err = clk_set_rate(ce->ceclks[i], ce->variant->ce_clks[i].freq);
			if (err)
				dev_err(ce->dev, "Fail to set %s clk speed to %lu hz\n",
					ce->variant->ce_clks[i].name,
					ce->variant->ce_clks[i].freq);
		}
		if (ce->variant->ce_clks[i].max_freq > 0 &&
		    cr > ce->variant->ce_clks[i].max_freq)
			dev_warn(ce->dev, "Frequency for %s (%lu hz) is higher than datasheet's recommendation (%lu hz)",
				 ce->variant->ce_clks[i].name, cr,
				 ce->variant->ce_clks[i].max_freq);
	}
	return 0;
}

static int sun8i_ce_register_algs(struct sun8i_ce_dev *ce)
{
	int ce_method, err, id, i;

	for (i = 0; i < ARRAY_SIZE(ce_algs); i++) {
		ce_algs[i].ce = ce;
		switch (ce_algs[i].type) {
		case CRYPTO_ALG_TYPE_SKCIPHER:
			id = ce_algs[i].ce_algo_id;
			ce_method = ce->variant->alg_cipher[id];
			if (ce_method == CE_ID_NOTSUPP) {
				dev_dbg(ce->dev,
					"DEBUG: Algo of %s not supported\n",
					ce_algs[i].alg.skcipher.base.cra_name);
				ce_algs[i].ce = NULL;
				break;
			}
			id = ce_algs[i].ce_blockmode;
			ce_method = ce->variant->op_mode[id];
			if (ce_method == CE_ID_NOTSUPP) {
				dev_dbg(ce->dev, "DEBUG: Blockmode of %s not supported\n",
					ce_algs[i].alg.skcipher.base.cra_name);
				ce_algs[i].ce = NULL;
				break;
			}
			dev_info(ce->dev, "Register %s\n",
				 ce_algs[i].alg.skcipher.base.cra_name);
			err = crypto_register_skcipher(&ce_algs[i].alg.skcipher);
			if (err) {
				dev_err(ce->dev, "ERROR: Fail to register %s\n",
					ce_algs[i].alg.skcipher.base.cra_name);
				ce_algs[i].ce = NULL;
				return err;
			}
			break;
		case CRYPTO_ALG_TYPE_AHASH:
			id = ce_algs[i].ce_algo_id;
			ce_method = ce->variant->alg_hash[id];
			if (ce_method == CE_ID_NOTSUPP) {
				dev_info(ce->dev,
					 "DEBUG: Algo of %s not supported\n",
					 ce_algs[i].alg.hash.halg.base.cra_name);
				ce_algs[i].ce = NULL;
				break;
			}
			dev_info(ce->dev, "Register %s\n",
				 ce_algs[i].alg.hash.halg.base.cra_name);
			err = crypto_register_ahash(&ce_algs[i].alg.hash);
			if (err) {
				dev_err(ce->dev, "ERROR: Fail to register %s\n",
					ce_algs[i].alg.hash.halg.base.cra_name);
				ce_algs[i].ce = NULL;
				return err;
			}
			break;
		case CRYPTO_ALG_TYPE_RNG:
			if (ce->variant->prng == CE_ID_NOTSUPP) {
				dev_info(ce->dev,
					 "DEBUG: Algo of %s not supported\n",
					 ce_algs[i].alg.rng.base.cra_name);
				ce_algs[i].ce = NULL;
				break;
			}
			dev_info(ce->dev, "Register %s\n",
				 ce_algs[i].alg.rng.base.cra_name);
			err = crypto_register_rng(&ce_algs[i].alg.rng);
			if (err) {
				dev_err(ce->dev, "Fail to register %s\n",
					ce_algs[i].alg.rng.base.cra_name);
				ce_algs[i].ce = NULL;
			}
			break;
		default:
			ce_algs[i].ce = NULL;
			dev_err(ce->dev, "ERROR: tried to register an unknown algo\n");
		}
	}
	return 0;
}

static void sun8i_ce_unregister_algs(struct sun8i_ce_dev *ce)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(ce_algs); i++) {
		if (!ce_algs[i].ce)
			continue;
		switch (ce_algs[i].type) {
		case CRYPTO_ALG_TYPE_SKCIPHER:
			dev_info(ce->dev, "Unregister %d %s\n", i,
				 ce_algs[i].alg.skcipher.base.cra_name);
			crypto_unregister_skcipher(&ce_algs[i].alg.skcipher);
			break;
		case CRYPTO_ALG_TYPE_AHASH:
			dev_info(ce->dev, "Unregister %d %s\n", i,
				 ce_algs[i].alg.hash.halg.base.cra_name);
			crypto_unregister_ahash(&ce_algs[i].alg.hash);
			break;
		case CRYPTO_ALG_TYPE_RNG:
			dev_info(ce->dev, "Unregister %d %s\n", i,
				 ce_algs[i].alg.rng.base.cra_name);
			crypto_unregister_rng(&ce_algs[i].alg.rng);
			break;
		}
	}
}

static int sun8i_ce_probe(struct platform_device *pdev)
{
	struct sun8i_ce_dev *ce;
	int err, irq;
	u32 v;

	ce = devm_kzalloc(&pdev->dev, sizeof(*ce), GFP_KERNEL);
	if (!ce)
		return -ENOMEM;

	ce->dev = &pdev->dev;
	platform_set_drvdata(pdev, ce);

	ce->variant = of_device_get_match_data(&pdev->dev);
	if (!ce->variant) {
		dev_err(&pdev->dev, "Missing Crypto Engine variant\n");
		return -EINVAL;
	}

	ce->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(ce->base))
		return PTR_ERR(ce->base);

	err = sun8i_ce_get_clks(ce);
	if (err)
		return err;

	/* Get Non Secure IRQ */
	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	ce->reset = devm_reset_control_get(&pdev->dev, NULL);
	if (IS_ERR(ce->reset))
		return dev_err_probe(&pdev->dev, PTR_ERR(ce->reset),
				     "No reset control found\n");

	mutex_init(&ce->mlock);
	mutex_init(&ce->rnglock);

	err = sun8i_ce_allocate_chanlist(ce);
	if (err)
		return err;

	err = sun8i_ce_pm_init(ce);
	if (err)
		goto error_pm;

	err = devm_request_irq(&pdev->dev, irq, ce_irq_handler, 0,
			       "sun8i-ce-ns", ce);
	if (err) {
		dev_err(ce->dev, "Cannot request CryptoEngine Non-secure IRQ (err=%d)\n", err);
		goto error_irq;
	}

	err = sun8i_ce_register_algs(ce);
	if (err)
		goto error_alg;

	err = pm_runtime_get_sync(ce->dev);
	if (err < 0)
		goto error_alg;

	v = readl(ce->base + CE_CTR);
	v >>= CE_DIE_ID_SHIFT;
	v &= CE_DIE_ID_MASK;
	dev_info(&pdev->dev, "CryptoEngine Die ID %x\n", v);

	pm_runtime_put_sync(ce->dev);

#ifdef CONFIG_CRYPTO_DEV_SUN8I_CE_DEBUG
	/* Ignore error of debugfs */
	ce->dbgfs_dir = debugfs_create_dir("sun8i-ce", NULL);
	ce->dbgfs_stats = debugfs_create_file("stats", 0444,
					      ce->dbgfs_dir, ce,
					      &sun8i_ce_debugfs_fops);
#endif

	return 0;
error_alg:
	sun8i_ce_unregister_algs(ce);
error_irq:
	sun8i_ce_pm_exit(ce);
error_pm:
	sun8i_ce_free_chanlist(ce, MAXFLOW - 1);
	return err;
}

static int sun8i_ce_remove(struct platform_device *pdev)
{
	struct sun8i_ce_dev *ce = platform_get_drvdata(pdev);

	sun8i_ce_unregister_algs(ce);

#ifdef CONFIG_CRYPTO_DEV_SUN8I_CE_DEBUG
	debugfs_remove_recursive(ce->dbgfs_dir);
#endif

	sun8i_ce_free_chanlist(ce, MAXFLOW - 1);

	sun8i_ce_pm_exit(ce);
	return 0;
}

static const struct of_device_id sun8i_ce_crypto_of_match_table[] = {
	{ .compatible = "allwinner,sun8i-h3-crypto",
	  .data = &ce_h3_variant },
	{ .compatible = "allwinner,sun8i-r40-crypto",
	  .data = &ce_r40_variant },
	{ .compatible = "allwinner,sun50i-a64-crypto",
	  .data = &ce_a64_variant },
	{ .compatible = "allwinner,sun50i-h5-crypto",
	  .data = &ce_h5_variant },
	{ .compatible = "allwinner,sun50i-h6-crypto",
	  .data = &ce_h6_variant },
	{}
};
MODULE_DEVICE_TABLE(of, sun8i_ce_crypto_of_match_table);

static struct platform_driver sun8i_ce_driver = {
	.probe		 = sun8i_ce_probe,
	.remove		 = sun8i_ce_remove,
	.driver		 = {
		.name		= "sun8i-ce",
		.pm		= &sun8i_ce_pm_ops,
		.of_match_table	= sun8i_ce_crypto_of_match_table,
	},
};

module_platform_driver(sun8i_ce_driver);

MODULE_DESCRIPTION("Allwinner Crypto Engine cryptographic offloader");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Corentin Labbe <clabbe.montjoie@gmail.com>");

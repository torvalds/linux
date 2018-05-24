/*
 * sun4i-ss-core.c - hardware cryptographic accelerator for Allwinner A20 SoC
 *
 * Copyright (C) 2013-2015 Corentin LABBE <clabbe.montjoie@gmail.com>
 *
 * Core file which registers crypto algorithms supported by the SS.
 *
 * You could find a link for the datasheet in Documentation/arm/sunxi/README
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <linux/clk.h>
#include <linux/crypto.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <crypto/scatterwalk.h>
#include <linux/scatterlist.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/reset.h>

#include "sun4i-ss.h"

static struct sun4i_ss_alg_template ss_algs[] = {
{       .type = CRYPTO_ALG_TYPE_AHASH,
	.mode = SS_OP_MD5,
	.alg.hash = {
		.init = sun4i_hash_init,
		.update = sun4i_hash_update,
		.final = sun4i_hash_final,
		.finup = sun4i_hash_finup,
		.digest = sun4i_hash_digest,
		.export = sun4i_hash_export_md5,
		.import = sun4i_hash_import_md5,
		.halg = {
			.digestsize = MD5_DIGEST_SIZE,
			.statesize = sizeof(struct md5_state),
			.base = {
				.cra_name = "md5",
				.cra_driver_name = "md5-sun4i-ss",
				.cra_priority = 300,
				.cra_alignmask = 3,
				.cra_flags = CRYPTO_ALG_TYPE_AHASH,
				.cra_blocksize = MD5_HMAC_BLOCK_SIZE,
				.cra_ctxsize = sizeof(struct sun4i_req_ctx),
				.cra_module = THIS_MODULE,
				.cra_type = &crypto_ahash_type,
				.cra_init = sun4i_hash_crainit
			}
		}
	}
},
{       .type = CRYPTO_ALG_TYPE_AHASH,
	.mode = SS_OP_SHA1,
	.alg.hash = {
		.init = sun4i_hash_init,
		.update = sun4i_hash_update,
		.final = sun4i_hash_final,
		.finup = sun4i_hash_finup,
		.digest = sun4i_hash_digest,
		.export = sun4i_hash_export_sha1,
		.import = sun4i_hash_import_sha1,
		.halg = {
			.digestsize = SHA1_DIGEST_SIZE,
			.statesize = sizeof(struct sha1_state),
			.base = {
				.cra_name = "sha1",
				.cra_driver_name = "sha1-sun4i-ss",
				.cra_priority = 300,
				.cra_alignmask = 3,
				.cra_flags = CRYPTO_ALG_TYPE_AHASH,
				.cra_blocksize = SHA1_BLOCK_SIZE,
				.cra_ctxsize = sizeof(struct sun4i_req_ctx),
				.cra_module = THIS_MODULE,
				.cra_type = &crypto_ahash_type,
				.cra_init = sun4i_hash_crainit
			}
		}
	}
},
{       .type = CRYPTO_ALG_TYPE_SKCIPHER,
	.alg.crypto = {
		.setkey         = sun4i_ss_aes_setkey,
		.encrypt        = sun4i_ss_cbc_aes_encrypt,
		.decrypt        = sun4i_ss_cbc_aes_decrypt,
		.min_keysize	= AES_MIN_KEY_SIZE,
		.max_keysize	= AES_MAX_KEY_SIZE,
		.ivsize		= AES_BLOCK_SIZE,
		.base = {
			.cra_name = "cbc(aes)",
			.cra_driver_name = "cbc-aes-sun4i-ss",
			.cra_priority = 300,
			.cra_blocksize = AES_BLOCK_SIZE,
			.cra_flags = CRYPTO_ALG_TYPE_SKCIPHER |
				     CRYPTO_ALG_KERN_DRIVER_ONLY,
			.cra_ctxsize = sizeof(struct sun4i_tfm_ctx),
			.cra_module = THIS_MODULE,
			.cra_alignmask = 3,
			.cra_init = sun4i_ss_cipher_init,
		}
	}
},
{       .type = CRYPTO_ALG_TYPE_SKCIPHER,
	.alg.crypto = {
		.setkey         = sun4i_ss_aes_setkey,
		.encrypt        = sun4i_ss_ecb_aes_encrypt,
		.decrypt        = sun4i_ss_ecb_aes_decrypt,
		.min_keysize	= AES_MIN_KEY_SIZE,
		.max_keysize	= AES_MAX_KEY_SIZE,
		.ivsize		= AES_BLOCK_SIZE,
		.base = {
			.cra_name = "ecb(aes)",
			.cra_driver_name = "ecb-aes-sun4i-ss",
			.cra_priority = 300,
			.cra_blocksize = AES_BLOCK_SIZE,
			.cra_flags = CRYPTO_ALG_TYPE_SKCIPHER |
				     CRYPTO_ALG_KERN_DRIVER_ONLY,
			.cra_ctxsize = sizeof(struct sun4i_tfm_ctx),
			.cra_module = THIS_MODULE,
			.cra_alignmask = 3,
			.cra_init = sun4i_ss_cipher_init,
		}
	}
},
{       .type = CRYPTO_ALG_TYPE_SKCIPHER,
	.alg.crypto = {
		.setkey         = sun4i_ss_des_setkey,
		.encrypt        = sun4i_ss_cbc_des_encrypt,
		.decrypt        = sun4i_ss_cbc_des_decrypt,
		.min_keysize    = DES_KEY_SIZE,
		.max_keysize    = DES_KEY_SIZE,
		.ivsize         = DES_BLOCK_SIZE,
		.base = {
			.cra_name = "cbc(des)",
			.cra_driver_name = "cbc-des-sun4i-ss",
			.cra_priority = 300,
			.cra_blocksize = DES_BLOCK_SIZE,
			.cra_flags = CRYPTO_ALG_TYPE_SKCIPHER |
				     CRYPTO_ALG_KERN_DRIVER_ONLY,
			.cra_ctxsize = sizeof(struct sun4i_req_ctx),
			.cra_module = THIS_MODULE,
			.cra_alignmask = 3,
			.cra_init = sun4i_ss_cipher_init,
		}
	}
},
{       .type = CRYPTO_ALG_TYPE_SKCIPHER,
	.alg.crypto = {
		.setkey         = sun4i_ss_des_setkey,
		.encrypt        = sun4i_ss_ecb_des_encrypt,
		.decrypt        = sun4i_ss_ecb_des_decrypt,
		.min_keysize    = DES_KEY_SIZE,
		.max_keysize    = DES_KEY_SIZE,
		.base = {
			.cra_name = "ecb(des)",
			.cra_driver_name = "ecb-des-sun4i-ss",
			.cra_priority = 300,
			.cra_blocksize = DES_BLOCK_SIZE,
			.cra_flags = CRYPTO_ALG_TYPE_SKCIPHER |
				     CRYPTO_ALG_KERN_DRIVER_ONLY,
			.cra_ctxsize = sizeof(struct sun4i_req_ctx),
			.cra_module = THIS_MODULE,
			.cra_alignmask = 3,
			.cra_init = sun4i_ss_cipher_init,
		}
	}
},
{       .type = CRYPTO_ALG_TYPE_SKCIPHER,
	.alg.crypto = {
		.setkey         = sun4i_ss_des3_setkey,
		.encrypt        = sun4i_ss_cbc_des3_encrypt,
		.decrypt        = sun4i_ss_cbc_des3_decrypt,
		.min_keysize    = DES3_EDE_KEY_SIZE,
		.max_keysize    = DES3_EDE_KEY_SIZE,
		.ivsize         = DES3_EDE_BLOCK_SIZE,
		.base = {
			.cra_name = "cbc(des3_ede)",
			.cra_driver_name = "cbc-des3-sun4i-ss",
			.cra_priority = 300,
			.cra_blocksize = DES3_EDE_BLOCK_SIZE,
			.cra_flags = CRYPTO_ALG_TYPE_SKCIPHER |
				     CRYPTO_ALG_KERN_DRIVER_ONLY,
			.cra_ctxsize = sizeof(struct sun4i_req_ctx),
			.cra_module = THIS_MODULE,
			.cra_alignmask = 3,
			.cra_init = sun4i_ss_cipher_init,
		}
	}
},
{       .type = CRYPTO_ALG_TYPE_SKCIPHER,
	.alg.crypto = {
		.setkey         = sun4i_ss_des3_setkey,
		.encrypt        = sun4i_ss_ecb_des3_encrypt,
		.decrypt        = sun4i_ss_ecb_des3_decrypt,
		.min_keysize    = DES3_EDE_KEY_SIZE,
		.max_keysize    = DES3_EDE_KEY_SIZE,
		.ivsize         = DES3_EDE_BLOCK_SIZE,
		.base = {
			.cra_name = "ecb(des3_ede)",
			.cra_driver_name = "ecb-des3-sun4i-ss",
			.cra_priority = 300,
			.cra_blocksize = DES3_EDE_BLOCK_SIZE,
			.cra_flags = CRYPTO_ALG_TYPE_SKCIPHER,
			.cra_ctxsize = sizeof(struct sun4i_req_ctx),
			.cra_module = THIS_MODULE,
			.cra_alignmask = 3,
			.cra_init = sun4i_ss_cipher_init,
		}
	}
},
#ifdef CONFIG_CRYPTO_DEV_SUN4I_SS_PRNG
{
	.type = CRYPTO_ALG_TYPE_RNG,
	.alg.rng = {
		.base = {
			.cra_name		= "stdrng",
			.cra_driver_name	= "sun4i_ss_rng",
			.cra_priority		= 300,
			.cra_ctxsize		= 0,
			.cra_module		= THIS_MODULE,
		},
		.generate               = sun4i_ss_prng_generate,
		.seed                   = sun4i_ss_prng_seed,
		.seedsize               = SS_SEED_LEN / BITS_PER_BYTE,
	}
},
#endif
};

static int sun4i_ss_probe(struct platform_device *pdev)
{
	struct resource *res;
	u32 v;
	int err, i;
	unsigned long cr;
	const unsigned long cr_ahb = 24 * 1000 * 1000;
	const unsigned long cr_mod = 150 * 1000 * 1000;
	struct sun4i_ss_ctx *ss;

	if (!pdev->dev.of_node)
		return -ENODEV;

	ss = devm_kzalloc(&pdev->dev, sizeof(*ss), GFP_KERNEL);
	if (!ss)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	ss->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(ss->base)) {
		dev_err(&pdev->dev, "Cannot request MMIO\n");
		return PTR_ERR(ss->base);
	}

	ss->ssclk = devm_clk_get(&pdev->dev, "mod");
	if (IS_ERR(ss->ssclk)) {
		err = PTR_ERR(ss->ssclk);
		dev_err(&pdev->dev, "Cannot get SS clock err=%d\n", err);
		return err;
	}
	dev_dbg(&pdev->dev, "clock ss acquired\n");

	ss->busclk = devm_clk_get(&pdev->dev, "ahb");
	if (IS_ERR(ss->busclk)) {
		err = PTR_ERR(ss->busclk);
		dev_err(&pdev->dev, "Cannot get AHB SS clock err=%d\n", err);
		return err;
	}
	dev_dbg(&pdev->dev, "clock ahb_ss acquired\n");

	ss->reset = devm_reset_control_get_optional(&pdev->dev, "ahb");
	if (IS_ERR(ss->reset)) {
		if (PTR_ERR(ss->reset) == -EPROBE_DEFER)
			return PTR_ERR(ss->reset);
		dev_info(&pdev->dev, "no reset control found\n");
		ss->reset = NULL;
	}

	/* Enable both clocks */
	err = clk_prepare_enable(ss->busclk);
	if (err) {
		dev_err(&pdev->dev, "Cannot prepare_enable busclk\n");
		return err;
	}
	err = clk_prepare_enable(ss->ssclk);
	if (err) {
		dev_err(&pdev->dev, "Cannot prepare_enable ssclk\n");
		goto error_ssclk;
	}

	/*
	 * Check that clock have the correct rates given in the datasheet
	 * Try to set the clock to the maximum allowed
	 */
	err = clk_set_rate(ss->ssclk, cr_mod);
	if (err) {
		dev_err(&pdev->dev, "Cannot set clock rate to ssclk\n");
		goto error_clk;
	}

	/* Deassert reset if we have a reset control */
	if (ss->reset) {
		err = reset_control_deassert(ss->reset);
		if (err) {
			dev_err(&pdev->dev, "Cannot deassert reset control\n");
			goto error_clk;
		}
	}

	/*
	 * The only impact on clocks below requirement are bad performance,
	 * so do not print "errors"
	 * warn on Overclocked clocks
	 */
	cr = clk_get_rate(ss->busclk);
	if (cr >= cr_ahb)
		dev_dbg(&pdev->dev, "Clock bus %lu (%lu MHz) (must be >= %lu)\n",
			cr, cr / 1000000, cr_ahb);
	else
		dev_warn(&pdev->dev, "Clock bus %lu (%lu MHz) (must be >= %lu)\n",
			 cr, cr / 1000000, cr_ahb);

	cr = clk_get_rate(ss->ssclk);
	if (cr <= cr_mod)
		if (cr < cr_mod)
			dev_warn(&pdev->dev, "Clock ss %lu (%lu MHz) (must be <= %lu)\n",
				 cr, cr / 1000000, cr_mod);
		else
			dev_dbg(&pdev->dev, "Clock ss %lu (%lu MHz) (must be <= %lu)\n",
				cr, cr / 1000000, cr_mod);
	else
		dev_warn(&pdev->dev, "Clock ss is at %lu (%lu MHz) (must be <= %lu)\n",
			 cr, cr / 1000000, cr_mod);

	/*
	 * Datasheet named it "Die Bonding ID"
	 * I expect to be a sort of Security System Revision number.
	 * Since the A80 seems to have an other version of SS
	 * this info could be useful
	 */
	writel(SS_ENABLED, ss->base + SS_CTL);
	v = readl(ss->base + SS_CTL);
	v >>= 16;
	v &= 0x07;
	dev_info(&pdev->dev, "Die ID %d\n", v);
	writel(0, ss->base + SS_CTL);

	ss->dev = &pdev->dev;

	spin_lock_init(&ss->slock);

	for (i = 0; i < ARRAY_SIZE(ss_algs); i++) {
		ss_algs[i].ss = ss;
		switch (ss_algs[i].type) {
		case CRYPTO_ALG_TYPE_SKCIPHER:
			err = crypto_register_skcipher(&ss_algs[i].alg.crypto);
			if (err) {
				dev_err(ss->dev, "Fail to register %s\n",
					ss_algs[i].alg.crypto.base.cra_name);
				goto error_alg;
			}
			break;
		case CRYPTO_ALG_TYPE_AHASH:
			err = crypto_register_ahash(&ss_algs[i].alg.hash);
			if (err) {
				dev_err(ss->dev, "Fail to register %s\n",
					ss_algs[i].alg.hash.halg.base.cra_name);
				goto error_alg;
			}
			break;
		case CRYPTO_ALG_TYPE_RNG:
			err = crypto_register_rng(&ss_algs[i].alg.rng);
			if (err) {
				dev_err(ss->dev, "Fail to register %s\n",
					ss_algs[i].alg.rng.base.cra_name);
			}
			break;
		}
	}
	platform_set_drvdata(pdev, ss);
	return 0;
error_alg:
	i--;
	for (; i >= 0; i--) {
		switch (ss_algs[i].type) {
		case CRYPTO_ALG_TYPE_SKCIPHER:
			crypto_unregister_skcipher(&ss_algs[i].alg.crypto);
			break;
		case CRYPTO_ALG_TYPE_AHASH:
			crypto_unregister_ahash(&ss_algs[i].alg.hash);
			break;
		case CRYPTO_ALG_TYPE_RNG:
			crypto_unregister_rng(&ss_algs[i].alg.rng);
			break;
		}
	}
	if (ss->reset)
		reset_control_assert(ss->reset);
error_clk:
	clk_disable_unprepare(ss->ssclk);
error_ssclk:
	clk_disable_unprepare(ss->busclk);
	return err;
}

static int sun4i_ss_remove(struct platform_device *pdev)
{
	int i;
	struct sun4i_ss_ctx *ss = platform_get_drvdata(pdev);

	for (i = 0; i < ARRAY_SIZE(ss_algs); i++) {
		switch (ss_algs[i].type) {
		case CRYPTO_ALG_TYPE_SKCIPHER:
			crypto_unregister_skcipher(&ss_algs[i].alg.crypto);
			break;
		case CRYPTO_ALG_TYPE_AHASH:
			crypto_unregister_ahash(&ss_algs[i].alg.hash);
			break;
		case CRYPTO_ALG_TYPE_RNG:
			crypto_unregister_rng(&ss_algs[i].alg.rng);
			break;
		}
	}

	writel(0, ss->base + SS_CTL);
	if (ss->reset)
		reset_control_assert(ss->reset);
	clk_disable_unprepare(ss->busclk);
	clk_disable_unprepare(ss->ssclk);
	return 0;
}

static const struct of_device_id a20ss_crypto_of_match_table[] = {
	{ .compatible = "allwinner,sun4i-a10-crypto" },
	{}
};
MODULE_DEVICE_TABLE(of, a20ss_crypto_of_match_table);

static struct platform_driver sun4i_ss_driver = {
	.probe          = sun4i_ss_probe,
	.remove         = sun4i_ss_remove,
	.driver         = {
		.name           = "sun4i-ss",
		.of_match_table	= a20ss_crypto_of_match_table,
	},
};

module_platform_driver(sun4i_ss_driver);

MODULE_ALIAS("platform:sun4i-ss");
MODULE_DESCRIPTION("Allwinner Security System cryptographic accelerator");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Corentin LABBE <clabbe.montjoie@gmail.com>");

// SPDX-License-Identifier: GPL-2.0-only
/*
 * Routines supporting VMX instructions on the Power 8
 *
 * Copyright (C) 2015 International Business Machines Inc.
 *
 * Author: Marcelo Henrique Cerri <mhcerri@br.ibm.com>
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/types.h>
#include <linux/err.h>
#include <linux/cpufeature.h>
#include <linux/crypto.h>
#include <asm/cputable.h>
#include <crypto/internal/hash.h>
#include <crypto/internal/skcipher.h>

#include "aesp8-ppc.h"

static int __init p8_init(void)
{
	int ret;

	ret = crypto_register_shash(&p8_ghash_alg);
	if (ret)
		goto err;

	ret = crypto_register_alg(&p8_aes_alg);
	if (ret)
		goto err_unregister_ghash;

	ret = crypto_register_skcipher(&p8_aes_cbc_alg);
	if (ret)
		goto err_unregister_aes;

	ret = crypto_register_skcipher(&p8_aes_ctr_alg);
	if (ret)
		goto err_unregister_aes_cbc;

	ret = crypto_register_skcipher(&p8_aes_xts_alg);
	if (ret)
		goto err_unregister_aes_ctr;

	return 0;

err_unregister_aes_ctr:
	crypto_unregister_skcipher(&p8_aes_ctr_alg);
err_unregister_aes_cbc:
	crypto_unregister_skcipher(&p8_aes_cbc_alg);
err_unregister_aes:
	crypto_unregister_alg(&p8_aes_alg);
err_unregister_ghash:
	crypto_unregister_shash(&p8_ghash_alg);
err:
	return ret;
}

static void __exit p8_exit(void)
{
	crypto_unregister_skcipher(&p8_aes_xts_alg);
	crypto_unregister_skcipher(&p8_aes_ctr_alg);
	crypto_unregister_skcipher(&p8_aes_cbc_alg);
	crypto_unregister_alg(&p8_aes_alg);
	crypto_unregister_shash(&p8_ghash_alg);
}

module_cpu_feature_match(PPC_MODULE_FEATURE_VEC_CRYPTO, p8_init);
module_exit(p8_exit);

MODULE_AUTHOR("Marcelo Cerri<mhcerri@br.ibm.com>");
MODULE_DESCRIPTION("IBM VMX cryptographic acceleration instructions "
		   "support on Power 8");
MODULE_LICENSE("GPL");
MODULE_VERSION("1.0.0");
MODULE_IMPORT_NS("CRYPTO_INTERNAL");

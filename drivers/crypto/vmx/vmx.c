/**
 * Routines supporting VMX instructions on the Power 8
 *
 * Copyright (C) 2015 International Business Machines Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 only.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Author: Marcelo Henrique Cerri <mhcerri@br.ibm.com>
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/types.h>
#include <linux/err.h>
#include <linux/crypto.h>
#include <asm/cputable.h>
#include <crypto/internal/hash.h>

extern struct shash_alg p8_ghash_alg;
extern struct crypto_alg p8_aes_alg;
extern struct crypto_alg p8_aes_cbc_alg;
extern struct crypto_alg p8_aes_ctr_alg;
static struct crypto_alg *algs[] = {
	&p8_aes_alg,
	&p8_aes_cbc_alg,
	&p8_aes_ctr_alg,
	NULL,
};

int __init p8_init(void)
{
	int ret = 0;
	struct crypto_alg **alg_it;

	if (!(cur_cpu_spec->cpu_user_features2 & PPC_FEATURE2_VEC_CRYPTO))
		return -ENODEV;

	for (alg_it = algs; *alg_it; alg_it++) {
		ret = crypto_register_alg(*alg_it);
		printk(KERN_INFO "crypto_register_alg '%s' = %d\n",
		       (*alg_it)->cra_name, ret);
		if (ret) {
			for (alg_it--; alg_it >= algs; alg_it--)
				crypto_unregister_alg(*alg_it);
			break;
		}
	}
	if (ret)
		return ret;

	ret = crypto_register_shash(&p8_ghash_alg);
	if (ret) {
		for (alg_it = algs; *alg_it; alg_it++)
			crypto_unregister_alg(*alg_it);
	}
	return ret;
}

void __exit p8_exit(void)
{
	struct crypto_alg **alg_it;

	for (alg_it = algs; *alg_it; alg_it++) {
		printk(KERN_INFO "Removing '%s'\n", (*alg_it)->cra_name);
		crypto_unregister_alg(*alg_it);
	}
	crypto_unregister_shash(&p8_ghash_alg);
}

module_init(p8_init);
module_exit(p8_exit);

MODULE_AUTHOR("Marcelo Cerri<mhcerri@br.ibm.com>");
MODULE_DESCRIPTION("IBM VMX cryptographic acceleration instructions "
		   "support on Power 8");
MODULE_LICENSE("GPL");
MODULE_VERSION("1.0.0");

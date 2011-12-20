/*
 * Cryptographic API.
 *
 * Partial (de)compression operations.
 *
 * Copyright 2008 Sony Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.
 * If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/crypto.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/seq_file.h>
#include <linux/string.h>
#include <linux/cryptouser.h>
#include <net/netlink.h>

#include <crypto/compress.h>
#include <crypto/internal/compress.h>

#include "internal.h"


static int crypto_pcomp_init(struct crypto_tfm *tfm, u32 type, u32 mask)
{
	return 0;
}

static unsigned int crypto_pcomp_extsize(struct crypto_alg *alg)
{
	return alg->cra_ctxsize;
}

static int crypto_pcomp_init_tfm(struct crypto_tfm *tfm)
{
	return 0;
}

#ifdef CONFIG_NET
static int crypto_pcomp_report(struct sk_buff *skb, struct crypto_alg *alg)
{
	struct crypto_report_comp rpcomp;

	snprintf(rpcomp.type, CRYPTO_MAX_ALG_NAME, "%s", "pcomp");

	NLA_PUT(skb, CRYPTOCFGA_REPORT_COMPRESS,
		sizeof(struct crypto_report_comp), &rpcomp);

	return 0;

nla_put_failure:
	return -EMSGSIZE;
}
#else
static int crypto_pcomp_report(struct sk_buff *skb, struct crypto_alg *alg)
{
	return -ENOSYS;
}
#endif

static void crypto_pcomp_show(struct seq_file *m, struct crypto_alg *alg)
	__attribute__ ((unused));
static void crypto_pcomp_show(struct seq_file *m, struct crypto_alg *alg)
{
	seq_printf(m, "type         : pcomp\n");
}

static const struct crypto_type crypto_pcomp_type = {
	.extsize	= crypto_pcomp_extsize,
	.init		= crypto_pcomp_init,
	.init_tfm	= crypto_pcomp_init_tfm,
#ifdef CONFIG_PROC_FS
	.show		= crypto_pcomp_show,
#endif
	.report		= crypto_pcomp_report,
	.maskclear	= ~CRYPTO_ALG_TYPE_MASK,
	.maskset	= CRYPTO_ALG_TYPE_MASK,
	.type		= CRYPTO_ALG_TYPE_PCOMPRESS,
	.tfmsize	= offsetof(struct crypto_pcomp, base),
};

struct crypto_pcomp *crypto_alloc_pcomp(const char *alg_name, u32 type,
					u32 mask)
{
	return crypto_alloc_tfm(alg_name, &crypto_pcomp_type, type, mask);
}
EXPORT_SYMBOL_GPL(crypto_alloc_pcomp);

int crypto_register_pcomp(struct pcomp_alg *alg)
{
	struct crypto_alg *base = &alg->base;

	base->cra_type = &crypto_pcomp_type;
	base->cra_flags &= ~CRYPTO_ALG_TYPE_MASK;
	base->cra_flags |= CRYPTO_ALG_TYPE_PCOMPRESS;

	return crypto_register_alg(base);
}
EXPORT_SYMBOL_GPL(crypto_register_pcomp);

int crypto_unregister_pcomp(struct pcomp_alg *alg)
{
	return crypto_unregister_alg(&alg->base);
}
EXPORT_SYMBOL_GPL(crypto_unregister_pcomp);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Partial (de)compression type");
MODULE_AUTHOR("Sony Corporation");

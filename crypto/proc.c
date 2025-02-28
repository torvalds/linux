// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Scatterlist Cryptographic API.
 *
 * Procfs information.
 *
 * Copyright (c) 2002 James Morris <jmorris@intercode.com.au>
 * Copyright (c) 2005 Herbert Xu <herbert@gondor.apana.org.au>
 */

#include <linux/atomic.h>
#include <linux/init.h>
#include <linux/crypto.h>
#include <linux/fips.h>
#include <linux/module.h>	/* for module_name() */
#include <linux/rwsem.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include "internal.h"

static void *c_start(struct seq_file *m, loff_t *pos)
{
	down_read(&crypto_alg_sem);
	return seq_list_start(&crypto_alg_list, *pos);
}

static void *c_next(struct seq_file *m, void *p, loff_t *pos)
{
	return seq_list_next(p, &crypto_alg_list, pos);
}

static void c_stop(struct seq_file *m, void *p)
{
	up_read(&crypto_alg_sem);
}

static int c_show(struct seq_file *m, void *p)
{
	struct crypto_alg *alg = list_entry(p, struct crypto_alg, cra_list);

	seq_printf(m, "name         : %s\n", alg->cra_name);
	seq_printf(m, "driver       : %s\n", alg->cra_driver_name);
	seq_printf(m, "module       : %s\n", module_name(alg->cra_module));
	seq_printf(m, "priority     : %d\n", alg->cra_priority);
	seq_printf(m, "refcnt       : %u\n", refcount_read(&alg->cra_refcnt));
	seq_printf(m, "selftest     : %s\n",
		   (alg->cra_flags & CRYPTO_ALG_TESTED) ?
		   "passed" : "unknown");
	seq_printf(m, "internal     : %s\n",
		   str_yes_no(alg->cra_flags & CRYPTO_ALG_INTERNAL));
	if (fips_enabled)
		seq_printf(m, "fips         : %s\n",
			str_no_yes(alg->cra_flags & CRYPTO_ALG_FIPS_INTERNAL));

	if (alg->cra_flags & CRYPTO_ALG_LARVAL) {
		seq_printf(m, "type         : larval\n");
		seq_printf(m, "flags        : 0x%x\n", alg->cra_flags);
		goto out;
	}

	if (alg->cra_type && alg->cra_type->show) {
		alg->cra_type->show(m, alg);
		goto out;
	}

	switch (alg->cra_flags & CRYPTO_ALG_TYPE_MASK) {
	case CRYPTO_ALG_TYPE_CIPHER:
		seq_printf(m, "type         : cipher\n");
		seq_printf(m, "blocksize    : %u\n", alg->cra_blocksize);
		seq_printf(m, "min keysize  : %u\n",
					alg->cra_cipher.cia_min_keysize);
		seq_printf(m, "max keysize  : %u\n",
					alg->cra_cipher.cia_max_keysize);
		break;
	case CRYPTO_ALG_TYPE_COMPRESS:
		seq_printf(m, "type         : compression\n");
		break;
	default:
		seq_printf(m, "type         : unknown\n");
		break;
	}

out:
	seq_putc(m, '\n');
	return 0;
}

static const struct seq_operations crypto_seq_ops = {
	.start		= c_start,
	.next		= c_next,
	.stop		= c_stop,
	.show		= c_show
};

void __init crypto_init_proc(void)
{
	proc_create_seq("crypto", 0, NULL, &crypto_seq_ops);
}

void __exit crypto_exit_proc(void)
{
	remove_proc_entry("crypto", NULL);
}

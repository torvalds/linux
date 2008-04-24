/*
 * Scatterlist Cryptographic API.
 *
 * Procfs information.
 *
 * Copyright (c) 2002 James Morris <jmorris@intercode.com.au>
 * Copyright (c) 2005 Herbert Xu <herbert@gondor.apana.org.au>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option) 
 * any later version.
 *
 */

#include <asm/atomic.h>
#include <linux/init.h>
#include <linux/crypto.h>
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
	seq_printf(m, "refcnt       : %d\n", atomic_read(&alg->cra_refcnt));
	
	switch (alg->cra_flags & CRYPTO_ALG_TYPE_MASK) {
	case CRYPTO_ALG_TYPE_CIPHER:
		seq_printf(m, "type         : cipher\n");
		seq_printf(m, "blocksize    : %u\n", alg->cra_blocksize);
		seq_printf(m, "min keysize  : %u\n",
					alg->cra_cipher.cia_min_keysize);
		seq_printf(m, "max keysize  : %u\n",
					alg->cra_cipher.cia_max_keysize);
		break;
		
	case CRYPTO_ALG_TYPE_DIGEST:
		seq_printf(m, "type         : digest\n");
		seq_printf(m, "blocksize    : %u\n", alg->cra_blocksize);
		seq_printf(m, "digestsize   : %u\n",
		           alg->cra_digest.dia_digestsize);
		break;
	case CRYPTO_ALG_TYPE_COMPRESS:
		seq_printf(m, "type         : compression\n");
		break;
	default:
		if (alg->cra_type && alg->cra_type->show)
			alg->cra_type->show(m, alg);
		else
			seq_printf(m, "type         : unknown\n");
		break;
	}

	seq_putc(m, '\n');
	return 0;
}

static const struct seq_operations crypto_seq_ops = {
	.start		= c_start,
	.next		= c_next,
	.stop		= c_stop,
	.show		= c_show
};

static int crypto_info_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &crypto_seq_ops);
}
        
static const struct file_operations proc_crypto_ops = {
	.open		= crypto_info_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release
};

void __init crypto_init_proc(void)
{
	proc_create("crypto", 0, NULL, &proc_crypto_ops);
}

void __exit crypto_exit_proc(void)
{
	remove_proc_entry("crypto", NULL);
}

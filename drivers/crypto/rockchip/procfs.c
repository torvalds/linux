// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) Rockchip Electronics Co., Ltd. */
#include <linux/clk.h>
#include <linux/proc_fs.h>
#include <linux/sem.h>
#include <linux/seq_file.h>

#include "procfs.h"

#ifdef CONFIG_PROC_FS

static const char *alg_type2name[ALG_TYPE_MAX] = {
	[ALG_TYPE_HASH]   = "HASH",
	[ALG_TYPE_HMAC]   = "HMAC",
	[ALG_TYPE_CIPHER] = "CIPHER",
	[ALG_TYPE_ASYM]   = "ASYM",
	[ALG_TYPE_AEAD]   = "AEAD",
};

static void crypto_show_clock(struct seq_file *p, struct clk_bulk_data *clk_bulks, int clks_num)
{
	int i;

	seq_puts(p, "clock info:\n");

	for (i = 0; i < clks_num; i++)
		seq_printf(p, "\t%-10s %ld\n", clk_bulks[i].id, clk_get_rate(clk_bulks[i].clk));

	seq_puts(p, "\n");
}

static void crypto_show_stat(struct seq_file *p, struct rk_crypto_stat *stat)
{
	/* show statistic info */
	seq_puts(p, "Statistic info:\n");
	seq_printf(p, "\tbusy_cnt      : %llu\n", stat->busy_cnt);
	seq_printf(p, "\tequeue_cnt    : %llu\n", stat->equeue_cnt);
	seq_printf(p, "\tdequeue_cnt   : %llu\n", stat->dequeue_cnt);
	seq_printf(p, "\tdone_cnt      : %llu\n", stat->done_cnt);
	seq_printf(p, "\tcomplete_cnt  : %llu\n", stat->complete_cnt);
	seq_printf(p, "\tfake_cnt      : %llu\n", stat->fake_cnt);
	seq_printf(p, "\tirq_cnt       : %llu\n", stat->irq_cnt);
	seq_printf(p, "\ttimeout_cnt   : %llu\n", stat->timeout_cnt);
	seq_printf(p, "\terror_cnt     : %llu\n", stat->error_cnt);
	seq_printf(p, "\tlast_error    : %d\n",   stat->last_error);
	seq_puts(p, "\n");
}

static void crypto_show_queue_info(struct seq_file *p, struct rk_crypto_dev *rk_dev)
{
	bool busy;
	unsigned long flags;
	u32 qlen, max_qlen;

	spin_lock_irqsave(&rk_dev->lock, flags);

	qlen     = rk_dev->queue.qlen;
	max_qlen = rk_dev->queue.max_qlen;
	busy     = rk_dev->busy;

	spin_unlock_irqrestore(&rk_dev->lock, flags);

	seq_printf(p, "Crypto queue usage [%u/%u], ever_max = %llu, status: %s\n",
		   qlen, max_qlen, rk_dev->stat.ever_queue_max, busy ? "busy" : "idle");

	seq_puts(p, "\n");
}

static void crypto_show_valid_algo_single(struct seq_file *p, enum alg_type type,
					  struct rk_crypto_algt **algs, u32 algs_num)
{
	u32 i;
	struct rk_crypto_algt *tmp_algs;

	seq_printf(p, "\t%s:\n", alg_type2name[type]);

	for (i = 0; i < algs_num; i++, algs++) {
		tmp_algs = *algs;

		if (!(tmp_algs->valid_flag) || tmp_algs->type != type)
			continue;

		seq_printf(p, "\t\t%s\n", tmp_algs->name);
	}

	seq_puts(p, "\n");
}

static void crypto_show_valid_algos(struct seq_file *p, struct rk_crypto_soc_data *soc_data)
{
	u32 algs_num = 0;
	struct rk_crypto_algt **algs;

	seq_puts(p, "Valid algorithms:\n");

	algs = soc_data->hw_get_algts(&algs_num);
	if (!algs || algs_num == 0)
		return;

	crypto_show_valid_algo_single(p, ALG_TYPE_CIPHER, algs, algs_num);
	crypto_show_valid_algo_single(p, ALG_TYPE_AEAD,   algs, algs_num);
	crypto_show_valid_algo_single(p, ALG_TYPE_HASH,   algs, algs_num);
	crypto_show_valid_algo_single(p, ALG_TYPE_HMAC,   algs, algs_num);
	crypto_show_valid_algo_single(p, ALG_TYPE_ASYM,   algs, algs_num);
}

static int crypto_show_all(struct seq_file *p, void *v)
{
	struct rk_crypto_dev *rk_dev = p->private;
	struct rk_crypto_soc_data *soc_data = rk_dev->soc_data;
	struct rk_crypto_stat *stat = &rk_dev->stat;

	seq_printf(p, "Rockchip Crypto Version: %s\n\n",
		   soc_data->crypto_ver);

	seq_printf(p, "use_soft_aes192 : %s\n\n", soc_data->use_soft_aes192 ? "true" : "false");

	crypto_show_clock(p, rk_dev->clk_bulks, rk_dev->clks_num);

	crypto_show_valid_algos(p, soc_data);

	crypto_show_stat(p, stat);

	crypto_show_queue_info(p, rk_dev);

	return 0;
}

static int crypto_open(struct inode *inode, struct file *file)
{
	struct rk_crypto_dev *data = PDE_DATA(inode);

	return single_open(file, crypto_show_all, data);
}

static const struct proc_ops ops = {
	.proc_open    = crypto_open,
	.proc_read    = seq_read,
	.proc_lseek   = seq_lseek,
	.proc_release = single_release,
};

int rkcrypto_proc_init(struct rk_crypto_dev *rk_dev)
{
	rk_dev->procfs = proc_create_data(rk_dev->name, 0, NULL, &ops, rk_dev);
	if (!rk_dev->procfs)
		return -EINVAL;

	return 0;
}

void rkcrypto_proc_cleanup(struct rk_crypto_dev *rk_dev)
{
	if (rk_dev->procfs)
		remove_proc_entry(rk_dev->name, NULL);

	rk_dev->procfs = NULL;
}

#endif /* CONFIG_PROC_FS */

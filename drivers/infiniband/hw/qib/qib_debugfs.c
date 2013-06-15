#ifdef CONFIG_DEBUG_FS
/*
 * Copyright (c) 2013 Intel Corporation.  All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/kernel.h>
#include <linux/export.h>

#include "qib.h"
#include "qib_verbs.h"
#include "qib_debugfs.h"

static struct dentry *qib_dbg_root;

#define DEBUGFS_FILE(name) \
static const struct seq_operations _##name##_seq_ops = { \
	.start = _##name##_seq_start, \
	.next  = _##name##_seq_next, \
	.stop  = _##name##_seq_stop, \
	.show  = _##name##_seq_show \
}; \
static int _##name##_open(struct inode *inode, struct file *s) \
{ \
	struct seq_file *seq; \
	int ret; \
	ret =  seq_open(s, &_##name##_seq_ops); \
	if (ret) \
		return ret; \
	seq = s->private_data; \
	seq->private = inode->i_private; \
	return 0; \
} \
static const struct file_operations _##name##_file_ops = { \
	.owner   = THIS_MODULE, \
	.open    = _##name##_open, \
	.read    = seq_read, \
	.llseek  = seq_lseek, \
	.release = seq_release \
};

#define DEBUGFS_FILE_CREATE(name) \
do { \
	struct dentry *ent; \
	ent = debugfs_create_file(#name , 0400, ibd->qib_ibdev_dbg, \
		ibd, &_##name##_file_ops); \
	if (!ent) \
		pr_warn("create of " #name " failed\n"); \
} while (0)

static void *_opcode_stats_seq_start(struct seq_file *s, loff_t *pos)
{
	struct qib_opcode_stats_perctx *opstats;

	if (*pos >= ARRAY_SIZE(opstats->stats))
		return NULL;
	return pos;
}

static void *_opcode_stats_seq_next(struct seq_file *s, void *v, loff_t *pos)
{
	struct qib_opcode_stats_perctx *opstats;

	++*pos;
	if (*pos >= ARRAY_SIZE(opstats->stats))
		return NULL;
	return pos;
}


static void _opcode_stats_seq_stop(struct seq_file *s, void *v)
{
	/* nothing allocated */
}

static int _opcode_stats_seq_show(struct seq_file *s, void *v)
{
	loff_t *spos = v;
	loff_t i = *spos, j;
	u64 n_packets = 0, n_bytes = 0;
	struct qib_ibdev *ibd = (struct qib_ibdev *)s->private;
	struct qib_devdata *dd = dd_from_dev(ibd);

	for (j = 0; j < dd->first_user_ctxt; j++) {
		if (!dd->rcd[j])
			continue;
		n_packets += dd->rcd[j]->opstats->stats[i].n_packets;
		n_bytes += dd->rcd[j]->opstats->stats[i].n_bytes;
	}
	if (!n_packets && !n_bytes)
		return SEQ_SKIP;
	seq_printf(s, "%02llx %llu/%llu\n", i,
		(unsigned long long) n_packets,
		(unsigned long long) n_bytes);

	return 0;
}

DEBUGFS_FILE(opcode_stats)

static void *_ctx_stats_seq_start(struct seq_file *s, loff_t *pos)
{
	struct qib_ibdev *ibd = (struct qib_ibdev *)s->private;
	struct qib_devdata *dd = dd_from_dev(ibd);

	if (!*pos)
		return SEQ_START_TOKEN;
	if (*pos >= dd->first_user_ctxt)
		return NULL;
	return pos;
}

static void *_ctx_stats_seq_next(struct seq_file *s, void *v, loff_t *pos)
{
	struct qib_ibdev *ibd = (struct qib_ibdev *)s->private;
	struct qib_devdata *dd = dd_from_dev(ibd);

	if (v == SEQ_START_TOKEN)
		return pos;

	++*pos;
	if (*pos >= dd->first_user_ctxt)
		return NULL;
	return pos;
}

static void _ctx_stats_seq_stop(struct seq_file *s, void *v)
{
	/* nothing allocated */
}

static int _ctx_stats_seq_show(struct seq_file *s, void *v)
{
	loff_t *spos;
	loff_t i, j;
	u64 n_packets = 0;
	struct qib_ibdev *ibd = (struct qib_ibdev *)s->private;
	struct qib_devdata *dd = dd_from_dev(ibd);

	if (v == SEQ_START_TOKEN) {
		seq_puts(s, "Ctx:npkts\n");
		return 0;
	}

	spos = v;
	i = *spos;

	if (!dd->rcd[i])
		return SEQ_SKIP;

	for (j = 0; j < ARRAY_SIZE(dd->rcd[i]->opstats->stats); j++)
		n_packets += dd->rcd[i]->opstats->stats[j].n_packets;

	if (!n_packets)
		return SEQ_SKIP;

	seq_printf(s, "  %llu:%llu\n", i, n_packets);
	return 0;
}

DEBUGFS_FILE(ctx_stats)

void qib_dbg_ibdev_init(struct qib_ibdev *ibd)
{
	char name[10];

	snprintf(name, sizeof(name), "qib%d", dd_from_dev(ibd)->unit);
	ibd->qib_ibdev_dbg = debugfs_create_dir(name, qib_dbg_root);
	if (!ibd->qib_ibdev_dbg) {
		pr_warn("create of %s failed\n", name);
		return;
	}
	DEBUGFS_FILE_CREATE(opcode_stats);
	DEBUGFS_FILE_CREATE(ctx_stats);
	return;
}

void qib_dbg_ibdev_exit(struct qib_ibdev *ibd)
{
	if (!qib_dbg_root)
		goto out;
	debugfs_remove_recursive(ibd->qib_ibdev_dbg);
out:
	ibd->qib_ibdev_dbg = NULL;
}

void qib_dbg_init(void)
{
	qib_dbg_root = debugfs_create_dir(QIB_DRV_NAME, NULL);
	if (!qib_dbg_root)
		pr_warn("init of debugfs failed\n");
}

void qib_dbg_exit(void)
{
	debugfs_remove_recursive(qib_dbg_root);
	qib_dbg_root = NULL;
}

#endif


// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2017 - 2019 Pensando Systems, Inc */

#include <linux/pci.h>
#include <linux/netdevice.h>

#include "ionic.h"
#include "ionic_bus.h"
#include "ionic_lif.h"
#include "ionic_debugfs.h"

#ifdef CONFIG_DEBUG_FS

static struct dentry *ionic_dir;

void ionic_debugfs_create(void)
{
	ionic_dir = debugfs_create_dir(IONIC_DRV_NAME, NULL);
}

void ionic_debugfs_destroy(void)
{
	debugfs_remove_recursive(ionic_dir);
}

void ionic_debugfs_add_dev(struct ionic *ionic)
{
	ionic->dentry = debugfs_create_dir(ionic_bus_info(ionic), ionic_dir);
}

void ionic_debugfs_del_dev(struct ionic *ionic)
{
	debugfs_remove_recursive(ionic->dentry);
	ionic->dentry = NULL;
}

static int identity_show(struct seq_file *seq, void *v)
{
	struct ionic *ionic = seq->private;
	struct ionic_identity *ident;

	ident = &ionic->ident;

	seq_printf(seq, "nlifs:            %d\n", ident->dev.nlifs);
	seq_printf(seq, "nintrs:           %d\n", ident->dev.nintrs);
	seq_printf(seq, "ndbpgs_per_lif:   %d\n", ident->dev.ndbpgs_per_lif);
	seq_printf(seq, "intr_coal_mult:   %d\n", ident->dev.intr_coal_mult);
	seq_printf(seq, "intr_coal_div:    %d\n", ident->dev.intr_coal_div);

	seq_printf(seq, "max_ucast_filters:  %d\n", ident->lif.eth.max_ucast_filters);
	seq_printf(seq, "max_mcast_filters:  %d\n", ident->lif.eth.max_mcast_filters);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(identity);

void ionic_debugfs_add_ident(struct ionic *ionic)
{
	debugfs_create_file("identity", 0400, ionic->dentry,
			    ionic, &identity_fops);
}

void ionic_debugfs_add_sizes(struct ionic *ionic)
{
	debugfs_create_u32("nlifs", 0400, ionic->dentry,
			   (u32 *)&ionic->ident.dev.nlifs);
	debugfs_create_u32("nintrs", 0400, ionic->dentry, &ionic->nintrs);

	debugfs_create_u32("ntxqs_per_lif", 0400, ionic->dentry,
			   (u32 *)&ionic->ident.lif.eth.config.queue_count[IONIC_QTYPE_TXQ]);
	debugfs_create_u32("nrxqs_per_lif", 0400, ionic->dentry,
			   (u32 *)&ionic->ident.lif.eth.config.queue_count[IONIC_QTYPE_RXQ]);
}

static int q_tail_show(struct seq_file *seq, void *v)
{
	struct ionic_queue *q = seq->private;

	seq_printf(seq, "%d\n", q->tail->index);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(q_tail);

static int q_head_show(struct seq_file *seq, void *v)
{
	struct ionic_queue *q = seq->private;

	seq_printf(seq, "%d\n", q->head->index);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(q_head);

static int cq_tail_show(struct seq_file *seq, void *v)
{
	struct ionic_cq *cq = seq->private;

	seq_printf(seq, "%d\n", cq->tail->index);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(cq_tail);

static const struct debugfs_reg32 intr_ctrl_regs[] = {
	{ .name = "coal_init", .offset = 0, },
	{ .name = "mask", .offset = 4, },
	{ .name = "credits", .offset = 8, },
	{ .name = "mask_on_assert", .offset = 12, },
	{ .name = "coal_timer", .offset = 16, },
};

void ionic_debugfs_add_qcq(struct ionic_lif *lif, struct ionic_qcq *qcq)
{
	struct dentry *q_dentry, *cq_dentry, *intr_dentry, *stats_dentry;
	struct ionic_dev *idev = &lif->ionic->idev;
	struct debugfs_regset32 *intr_ctrl_regset;
	struct ionic_intr_info *intr = &qcq->intr;
	struct debugfs_blob_wrapper *desc_blob;
	struct device *dev = lif->ionic->dev;
	struct ionic_queue *q = &qcq->q;
	struct ionic_cq *cq = &qcq->cq;

	qcq->dentry = debugfs_create_dir(q->name, lif->dentry);

	debugfs_create_x32("total_size", 0400, qcq->dentry, &qcq->total_size);
	debugfs_create_x64("base_pa", 0400, qcq->dentry, &qcq->base_pa);

	q_dentry = debugfs_create_dir("q", qcq->dentry);

	debugfs_create_u32("index", 0400, q_dentry, &q->index);
	debugfs_create_x64("base_pa", 0400, q_dentry, &q->base_pa);
	if (qcq->flags & IONIC_QCQ_F_SG) {
		debugfs_create_x64("sg_base_pa", 0400, q_dentry,
				   &q->sg_base_pa);
		debugfs_create_u32("sg_desc_size", 0400, q_dentry,
				   &q->sg_desc_size);
	}
	debugfs_create_u32("num_descs", 0400, q_dentry, &q->num_descs);
	debugfs_create_u32("desc_size", 0400, q_dentry, &q->desc_size);
	debugfs_create_u32("pid", 0400, q_dentry, &q->pid);
	debugfs_create_u32("qid", 0400, q_dentry, &q->hw_index);
	debugfs_create_u32("qtype", 0400, q_dentry, &q->hw_type);
	debugfs_create_u64("drop", 0400, q_dentry, &q->drop);
	debugfs_create_u64("stop", 0400, q_dentry, &q->stop);
	debugfs_create_u64("wake", 0400, q_dentry, &q->wake);

	debugfs_create_file("tail", 0400, q_dentry, q, &q_tail_fops);
	debugfs_create_file("head", 0400, q_dentry, q, &q_head_fops);

	desc_blob = devm_kzalloc(dev, sizeof(*desc_blob), GFP_KERNEL);
	if (!desc_blob)
		return;
	desc_blob->data = q->base;
	desc_blob->size = (unsigned long)q->num_descs * q->desc_size;
	debugfs_create_blob("desc_blob", 0400, q_dentry, desc_blob);

	if (qcq->flags & IONIC_QCQ_F_SG) {
		desc_blob = devm_kzalloc(dev, sizeof(*desc_blob), GFP_KERNEL);
		if (!desc_blob)
			return;
		desc_blob->data = q->sg_base;
		desc_blob->size = (unsigned long)q->num_descs * q->sg_desc_size;
		debugfs_create_blob("sg_desc_blob", 0400, q_dentry,
				    desc_blob);
	}

	cq_dentry = debugfs_create_dir("cq", qcq->dentry);

	debugfs_create_x64("base_pa", 0400, cq_dentry, &cq->base_pa);
	debugfs_create_u32("num_descs", 0400, cq_dentry, &cq->num_descs);
	debugfs_create_u32("desc_size", 0400, cq_dentry, &cq->desc_size);
	debugfs_create_u8("done_color", 0400, cq_dentry,
			  (u8 *)&cq->done_color);

	debugfs_create_file("tail", 0400, cq_dentry, cq, &cq_tail_fops);

	desc_blob = devm_kzalloc(dev, sizeof(*desc_blob), GFP_KERNEL);
	if (!desc_blob)
		return;
	desc_blob->data = cq->base;
	desc_blob->size = (unsigned long)cq->num_descs * cq->desc_size;
	debugfs_create_blob("desc_blob", 0400, cq_dentry, desc_blob);

	if (qcq->flags & IONIC_QCQ_F_INTR) {
		intr_dentry = debugfs_create_dir("intr", qcq->dentry);

		debugfs_create_u32("index", 0400, intr_dentry,
				   &intr->index);
		debugfs_create_u32("vector", 0400, intr_dentry,
				   &intr->vector);

		intr_ctrl_regset = devm_kzalloc(dev, sizeof(*intr_ctrl_regset),
						GFP_KERNEL);
		if (!intr_ctrl_regset)
			return;
		intr_ctrl_regset->regs = intr_ctrl_regs;
		intr_ctrl_regset->nregs = ARRAY_SIZE(intr_ctrl_regs);
		intr_ctrl_regset->base = &idev->intr_ctrl[intr->index];

		debugfs_create_regset32("intr_ctrl", 0400, intr_dentry,
					intr_ctrl_regset);
	}

	if (qcq->flags & IONIC_QCQ_F_NOTIFYQ) {
		stats_dentry = debugfs_create_dir("notifyblock", qcq->dentry);

		debugfs_create_u64("eid", 0400, stats_dentry,
				   (u64 *)&lif->info->status.eid);
		debugfs_create_u16("link_status", 0400, stats_dentry,
				   (u16 *)&lif->info->status.link_status);
		debugfs_create_u32("link_speed", 0400, stats_dentry,
				   (u32 *)&lif->info->status.link_speed);
		debugfs_create_u16("link_down_count", 0400, stats_dentry,
				   (u16 *)&lif->info->status.link_down_count);
	}
}

static int netdev_show(struct seq_file *seq, void *v)
{
	struct net_device *netdev = seq->private;

	seq_printf(seq, "%s\n", netdev->name);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(netdev);

void ionic_debugfs_add_lif(struct ionic_lif *lif)
{
	struct dentry *lif_dentry;

	lif_dentry = debugfs_create_dir(lif->name, lif->ionic->dentry);
	if (IS_ERR_OR_NULL(lif_dentry))
		return;
	lif->dentry = lif_dentry;

	debugfs_create_file("netdev", 0400, lif->dentry,
			    lif->netdev, &netdev_fops);
}

void ionic_debugfs_del_lif(struct ionic_lif *lif)
{
	debugfs_remove_recursive(lif->dentry);
	lif->dentry = NULL;
}

void ionic_debugfs_del_qcq(struct ionic_qcq *qcq)
{
	debugfs_remove_recursive(qcq->dentry);
	qcq->dentry = NULL;
}

#endif

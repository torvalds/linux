// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2023 Advanced Micro Devices, Inc */

#include <linux/pci.h>

#include "core.h"

static struct dentry *pdsc_dir;

void pdsc_debugfs_create(void)
{
	pdsc_dir = debugfs_create_dir(PDS_CORE_DRV_NAME, NULL);
}

void pdsc_debugfs_destroy(void)
{
	debugfs_remove_recursive(pdsc_dir);
}

void pdsc_debugfs_add_dev(struct pdsc *pdsc)
{
	pdsc->dentry = debugfs_create_dir(pci_name(pdsc->pdev), pdsc_dir);

	debugfs_create_ulong("state", 0400, pdsc->dentry, &pdsc->state);
}

void pdsc_debugfs_del_dev(struct pdsc *pdsc)
{
	debugfs_remove_recursive(pdsc->dentry);
	pdsc->dentry = NULL;
}

static int identity_show(struct seq_file *seq, void *v)
{
	struct pds_core_dev_identity *ident;
	struct pdsc *pdsc = seq->private;
	int vt;

	ident = &pdsc->dev_ident;

	seq_printf(seq, "fw_heartbeat:     0x%x\n",
		   ioread32(&pdsc->info_regs->fw_heartbeat));

	seq_printf(seq, "nlifs:            %d\n",
		   le32_to_cpu(ident->nlifs));
	seq_printf(seq, "nintrs:           %d\n",
		   le32_to_cpu(ident->nintrs));
	seq_printf(seq, "ndbpgs_per_lif:   %d\n",
		   le32_to_cpu(ident->ndbpgs_per_lif));
	seq_printf(seq, "intr_coal_mult:   %d\n",
		   le32_to_cpu(ident->intr_coal_mult));
	seq_printf(seq, "intr_coal_div:    %d\n",
		   le32_to_cpu(ident->intr_coal_div));

	seq_puts(seq, "vif_types:        ");
	for (vt = 0; vt < PDS_DEV_TYPE_MAX; vt++)
		seq_printf(seq, "%d ",
			   le16_to_cpu(pdsc->dev_ident.vif_types[vt]));
	seq_puts(seq, "\n");

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(identity);

void pdsc_debugfs_add_ident(struct pdsc *pdsc)
{
	/* This file will already exist in the reset flow */
	if (debugfs_lookup("identity", pdsc->dentry))
		return;

	debugfs_create_file("identity", 0400, pdsc->dentry,
			    pdsc, &identity_fops);
}

static int viftype_show(struct seq_file *seq, void *v)
{
	struct pdsc *pdsc = seq->private;
	int vt;

	for (vt = 0; vt < PDS_DEV_TYPE_MAX; vt++) {
		if (!pdsc->viftype_status[vt].name)
			continue;

		seq_printf(seq, "%s\t%d supported %d enabled\n",
			   pdsc->viftype_status[vt].name,
			   pdsc->viftype_status[vt].supported,
			   pdsc->viftype_status[vt].enabled);
	}
	return 0;
}
DEFINE_SHOW_ATTRIBUTE(viftype);

void pdsc_debugfs_add_viftype(struct pdsc *pdsc)
{
	debugfs_create_file("viftypes", 0400, pdsc->dentry,
			    pdsc, &viftype_fops);
}

static const struct debugfs_reg32 intr_ctrl_regs[] = {
	{ .name = "coal_init", .offset = 0, },
	{ .name = "mask", .offset = 4, },
	{ .name = "credits", .offset = 8, },
	{ .name = "mask_on_assert", .offset = 12, },
	{ .name = "coal_timer", .offset = 16, },
};

void pdsc_debugfs_add_qcq(struct pdsc *pdsc, struct pdsc_qcq *qcq)
{
	struct dentry *qcq_dentry, *q_dentry, *cq_dentry, *intr_dentry;
	struct debugfs_regset32 *intr_ctrl_regset;
	struct pdsc_queue *q = &qcq->q;
	struct pdsc_cq *cq = &qcq->cq;

	qcq_dentry = debugfs_create_dir(q->name, pdsc->dentry);
	if (IS_ERR_OR_NULL(qcq_dentry))
		return;
	qcq->dentry = qcq_dentry;

	debugfs_create_x64("q_base_pa", 0400, qcq_dentry, &qcq->q_base_pa);
	debugfs_create_x32("q_size", 0400, qcq_dentry, &qcq->q_size);
	debugfs_create_x64("cq_base_pa", 0400, qcq_dentry, &qcq->cq_base_pa);
	debugfs_create_x32("cq_size", 0400, qcq_dentry, &qcq->cq_size);
	debugfs_create_x32("accum_work", 0400, qcq_dentry, &qcq->accum_work);

	q_dentry = debugfs_create_dir("q", qcq->dentry);
	if (IS_ERR_OR_NULL(q_dentry))
		return;

	debugfs_create_u32("index", 0400, q_dentry, &q->index);
	debugfs_create_u32("num_descs", 0400, q_dentry, &q->num_descs);
	debugfs_create_u32("desc_size", 0400, q_dentry, &q->desc_size);
	debugfs_create_u32("pid", 0400, q_dentry, &q->pid);

	debugfs_create_u16("tail", 0400, q_dentry, &q->tail_idx);
	debugfs_create_u16("head", 0400, q_dentry, &q->head_idx);

	cq_dentry = debugfs_create_dir("cq", qcq->dentry);
	if (IS_ERR_OR_NULL(cq_dentry))
		return;

	debugfs_create_x64("base_pa", 0400, cq_dentry, &cq->base_pa);
	debugfs_create_u32("num_descs", 0400, cq_dentry, &cq->num_descs);
	debugfs_create_u32("desc_size", 0400, cq_dentry, &cq->desc_size);
	debugfs_create_bool("done_color", 0400, cq_dentry, &cq->done_color);
	debugfs_create_u16("tail", 0400, cq_dentry, &cq->tail_idx);

	if (qcq->flags & PDS_CORE_QCQ_F_INTR) {
		struct pdsc_intr_info *intr = &pdsc->intr_info[qcq->intx];

		intr_dentry = debugfs_create_dir("intr", qcq->dentry);
		if (IS_ERR_OR_NULL(intr_dentry))
			return;

		debugfs_create_u32("index", 0400, intr_dentry, &intr->index);
		debugfs_create_u32("vector", 0400, intr_dentry, &intr->vector);

		intr_ctrl_regset = kzalloc(sizeof(*intr_ctrl_regset),
					   GFP_KERNEL);
		if (!intr_ctrl_regset)
			return;
		intr_ctrl_regset->regs = intr_ctrl_regs;
		intr_ctrl_regset->nregs = ARRAY_SIZE(intr_ctrl_regs);
		intr_ctrl_regset->base = &pdsc->intr_ctrl[intr->index];

		debugfs_create_regset32("intr_ctrl", 0400, intr_dentry,
					intr_ctrl_regset);
	}
};

void pdsc_debugfs_del_qcq(struct pdsc_qcq *qcq)
{
	debugfs_remove_recursive(qcq->dentry);
	qcq->dentry = NULL;
}

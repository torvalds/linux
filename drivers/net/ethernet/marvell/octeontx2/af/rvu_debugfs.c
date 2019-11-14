// SPDX-License-Identifier: GPL-2.0
/* Marvell OcteonTx2 RVU Admin Function driver
 *
 * Copyright (C) 2019 Marvell International Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifdef CONFIG_DEBUG_FS

#include <linux/fs.h>
#include <linux/debugfs.h>
#include <linux/module.h>
#include <linux/pci.h>

#include "rvu_struct.h"
#include "rvu_reg.h"
#include "rvu.h"

#define DEBUGFS_DIR_NAME "octeontx2"

#define rvu_dbg_NULL NULL
#define rvu_dbg_open_NULL NULL

#define RVU_DEBUG_SEQ_FOPS(name, read_op, write_op)	\
static int rvu_dbg_open_##name(struct inode *inode, struct file *file) \
{ \
	return single_open(file, rvu_dbg_##read_op, inode->i_private); \
} \
static const struct file_operations rvu_dbg_##name##_fops = { \
	.owner		= THIS_MODULE, \
	.open		= rvu_dbg_open_##name, \
	.read		= seq_read, \
	.write		= rvu_dbg_##write_op, \
	.llseek		= seq_lseek, \
	.release	= single_release, \
}

#define RVU_DEBUG_FOPS(name, read_op, write_op) \
static const struct file_operations rvu_dbg_##name##_fops = { \
	.owner = THIS_MODULE, \
	.open = simple_open, \
	.read = rvu_dbg_##read_op, \
	.write = rvu_dbg_##write_op \
}

/* Dumps current provisioning status of all RVU block LFs */
static ssize_t rvu_dbg_rsrc_attach_status(struct file *filp,
					  char __user *buffer,
					  size_t count, loff_t *ppos)
{
	int index, off = 0, flag = 0, go_back = 0, off_prev;
	struct rvu *rvu = filp->private_data;
	int lf, pf, vf, pcifunc;
	struct rvu_block block;
	int bytes_not_copied;
	int buf_size = 2048;
	char *buf;

	/* don't allow partial reads */
	if (*ppos != 0)
		return 0;

	buf = kzalloc(buf_size, GFP_KERNEL);
	if (!buf)
		return -ENOSPC;
	off +=	scnprintf(&buf[off], buf_size - 1 - off, "\npcifunc\t\t");
	for (index = 0; index < BLK_COUNT; index++)
		if (strlen(rvu->hw->block[index].name))
			off +=	scnprintf(&buf[off], buf_size - 1 - off,
					  "%*s\t", (index - 1) * 2,
					  rvu->hw->block[index].name);
	off += scnprintf(&buf[off], buf_size - 1 - off, "\n");
	for (pf = 0; pf < rvu->hw->total_pfs; pf++) {
		for (vf = 0; vf <= rvu->hw->total_vfs; vf++) {
			pcifunc = pf << 10 | vf;
			if (!pcifunc)
				continue;

			if (vf) {
				go_back = scnprintf(&buf[off],
						    buf_size - 1 - off,
						    "PF%d:VF%d\t\t", pf,
						    vf - 1);
			} else {
				go_back = scnprintf(&buf[off],
						    buf_size - 1 - off,
						    "PF%d\t\t", pf);
			}

			off += go_back;
			for (index = 0; index < BLKTYPE_MAX; index++) {
				block = rvu->hw->block[index];
				if (!strlen(block.name))
					continue;
				off_prev = off;
				for (lf = 0; lf < block.lf.max; lf++) {
					if (block.fn_map[lf] != pcifunc)
						continue;
					flag = 1;
					off += scnprintf(&buf[off], buf_size - 1
							- off, "%3d,", lf);
				}
				if (flag && off_prev != off)
					off--;
				else
					go_back++;
				off += scnprintf(&buf[off], buf_size - 1 - off,
						"\t");
			}
			if (!flag)
				off -= go_back;
			else
				flag = 0;
			off--;
			off +=	scnprintf(&buf[off], buf_size - 1 - off, "\n");
		}
	}

	bytes_not_copied = copy_to_user(buffer, buf, off);
	kfree(buf);

	if (bytes_not_copied)
		return -EFAULT;

	*ppos = off;
	return off;
}

RVU_DEBUG_FOPS(rsrc_status, rsrc_attach_status, NULL);

static bool rvu_dbg_is_valid_lf(struct rvu *rvu, int blktype, int lf,
				u16 *pcifunc)
{
	struct rvu_block *block;
	struct rvu_hwinfo *hw;
	int blkaddr;

	blkaddr = rvu_get_blkaddr(rvu, blktype, 0);
	if (blkaddr < 0) {
		dev_warn(rvu->dev, "Invalid blktype\n");
		return false;
	}

	hw = rvu->hw;
	block = &hw->block[blkaddr];

	if (lf < 0 || lf >= block->lf.max) {
		dev_warn(rvu->dev, "Invalid LF: valid range: 0-%d\n",
			 block->lf.max - 1);
		return false;
	}

	*pcifunc = block->fn_map[lf];
	if (!*pcifunc) {
		dev_warn(rvu->dev,
			 "This LF is not attached to any RVU PFFUNC\n");
		return false;
	}
	return true;
}

static void print_npa_qsize(struct seq_file *m, struct rvu_pfvf *pfvf)
{
	char *buf;

	buf = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (!buf)
		return;

	if (!pfvf->aura_ctx) {
		seq_puts(m, "Aura context is not initialized\n");
	} else {
		bitmap_print_to_pagebuf(false, buf, pfvf->aura_bmap,
					pfvf->aura_ctx->qsize);
		seq_printf(m, "Aura count : %d\n", pfvf->aura_ctx->qsize);
		seq_printf(m, "Aura context ena/dis bitmap : %s\n", buf);
	}

	if (!pfvf->pool_ctx) {
		seq_puts(m, "Pool context is not initialized\n");
	} else {
		bitmap_print_to_pagebuf(false, buf, pfvf->pool_bmap,
					pfvf->pool_ctx->qsize);
		seq_printf(m, "Pool count : %d\n", pfvf->pool_ctx->qsize);
		seq_printf(m, "Pool context ena/dis bitmap : %s\n", buf);
	}
	kfree(buf);
}

/* The 'qsize' entry dumps current Aura/Pool context Qsize
 * and each context's current enable/disable status in a bitmap.
 */
static int rvu_dbg_qsize_display(struct seq_file *filp, void *unsused,
				 int blktype)
{
	void (*print_qsize)(struct seq_file *filp,
			    struct rvu_pfvf *pfvf) = NULL;
	struct rvu_pfvf *pfvf;
	struct rvu *rvu;
	int qsize_id;
	u16 pcifunc;

	rvu = filp->private;
	switch (blktype) {
	case BLKTYPE_NPA:
		qsize_id = rvu->rvu_dbg.npa_qsize_id;
		print_qsize = print_npa_qsize;
		break;
	default:
		return -EINVAL;
	}

	if (!rvu_dbg_is_valid_lf(rvu, blktype, qsize_id, &pcifunc))
		return -EINVAL;

	pfvf = rvu_get_pfvf(rvu, pcifunc);
	print_qsize(filp, pfvf);

	return 0;
}

static ssize_t rvu_dbg_qsize_write(struct file *filp,
				   const char __user *buffer, size_t count,
				   loff_t *ppos, int blktype)
{
	char *blk_string = (blktype == BLKTYPE_NPA) ? "npa" : "nix";
	struct seq_file *seqfile = filp->private_data;
	char *cmd_buf, *cmd_buf_tmp, *subtoken;
	struct rvu *rvu = seqfile->private;
	u16 pcifunc;
	int ret, lf;

	cmd_buf = memdup_user(buffer, count);
	if (IS_ERR(cmd_buf))
		return -ENOMEM;

	cmd_buf[count] = '\0';

	cmd_buf_tmp = strchr(cmd_buf, '\n');
	if (cmd_buf_tmp) {
		*cmd_buf_tmp = '\0';
		count = cmd_buf_tmp - cmd_buf + 1;
	}

	cmd_buf_tmp = cmd_buf;
	subtoken = strsep(&cmd_buf, " ");
	ret = subtoken ? kstrtoint(subtoken, 10, &lf) : -EINVAL;
	if (cmd_buf)
		ret = -EINVAL;

	if (!strncmp(subtoken, "help", 4) || ret < 0) {
		dev_info(rvu->dev, "Use echo <%s-lf > qsize\n", blk_string);
		goto qsize_write_done;
	}

	if (!rvu_dbg_is_valid_lf(rvu, blktype, lf, &pcifunc)) {
		ret = -EINVAL;
		goto qsize_write_done;
	}
	if (blktype  == BLKTYPE_NPA)
		rvu->rvu_dbg.npa_qsize_id = lf;

qsize_write_done:
	kfree(cmd_buf_tmp);
	return ret ? ret : count;
}

static ssize_t rvu_dbg_npa_qsize_write(struct file *filp,
				       const char __user *buffer,
				       size_t count, loff_t *ppos)
{
	return rvu_dbg_qsize_write(filp, buffer, count, ppos,
					    BLKTYPE_NPA);
}

static int rvu_dbg_npa_qsize_display(struct seq_file *filp, void *unused)
{
	return rvu_dbg_qsize_display(filp, unused, BLKTYPE_NPA);
}

RVU_DEBUG_SEQ_FOPS(npa_qsize, npa_qsize_display, npa_qsize_write);

/* Dumps given NPA Aura's context */
static void print_npa_aura_ctx(struct seq_file *m, struct npa_aq_enq_rsp *rsp)
{
	struct npa_aura_s *aura = &rsp->aura;

	seq_printf(m, "W0: Pool addr\t\t%llx\n", aura->pool_addr);

	seq_printf(m, "W1: ena\t\t\t%d\nW1: pool caching\t%d\n",
		   aura->ena, aura->pool_caching);
	seq_printf(m, "W1: pool way mask\t%d\nW1: avg con\t\t%d\n",
		   aura->pool_way_mask, aura->avg_con);
	seq_printf(m, "W1: pool drop ena\t%d\nW1: aura drop ena\t%d\n",
		   aura->pool_drop_ena, aura->aura_drop_ena);
	seq_printf(m, "W1: bp_ena\t\t%d\nW1: aura drop\t\t%d\n",
		   aura->bp_ena, aura->aura_drop);
	seq_printf(m, "W1: aura shift\t\t%d\nW1: avg_level\t\t%d\n",
		   aura->shift, aura->avg_level);

	seq_printf(m, "W2: count\t\t%llu\nW2: nix0_bpid\t\t%d\nW2: nix1_bpid\t\t%d\n",
		   (u64)aura->count, aura->nix0_bpid, aura->nix1_bpid);

	seq_printf(m, "W3: limit\t\t%llu\nW3: bp\t\t\t%d\nW3: fc_ena\t\t%d\n",
		   (u64)aura->limit, aura->bp, aura->fc_ena);
	seq_printf(m, "W3: fc_up_crossing\t%d\nW3: fc_stype\t\t%d\n",
		   aura->fc_up_crossing, aura->fc_stype);
	seq_printf(m, "W3: fc_hyst_bits\t%d\n", aura->fc_hyst_bits);

	seq_printf(m, "W4: fc_addr\t\t%llx\n", aura->fc_addr);

	seq_printf(m, "W5: pool_drop\t\t%d\nW5: update_time\t\t%d\n",
		   aura->pool_drop, aura->update_time);
	seq_printf(m, "W5: err_int \t\t%d\nW5: err_int_ena\t\t%d\n",
		   aura->err_int, aura->err_int_ena);
	seq_printf(m, "W5: thresh_int\t\t%d\nW5: thresh_int_ena \t%d\n",
		   aura->thresh_int, aura->thresh_int_ena);
	seq_printf(m, "W5: thresh_up\t\t%d\nW5: thresh_qint_idx\t%d\n",
		   aura->thresh_up, aura->thresh_qint_idx);
	seq_printf(m, "W5: err_qint_idx \t%d\n", aura->err_qint_idx);

	seq_printf(m, "W6: thresh\t\t%llu\n", (u64)aura->thresh);
}

/* Dumps given NPA Pool's context */
static void print_npa_pool_ctx(struct seq_file *m, struct npa_aq_enq_rsp *rsp)
{
	struct npa_pool_s *pool = &rsp->pool;

	seq_printf(m, "W0: Stack base\t\t%llx\n", pool->stack_base);

	seq_printf(m, "W1: ena \t\t%d\nW1: nat_align \t\t%d\n",
		   pool->ena, pool->nat_align);
	seq_printf(m, "W1: stack_caching\t%d\nW1: stack_way_mask\t%d\n",
		   pool->stack_caching, pool->stack_way_mask);
	seq_printf(m, "W1: buf_offset\t\t%d\nW1: buf_size\t\t%d\n",
		   pool->buf_offset, pool->buf_size);

	seq_printf(m, "W2: stack_max_pages \t%d\nW2: stack_pages\t\t%d\n",
		   pool->stack_max_pages, pool->stack_pages);

	seq_printf(m, "W3: op_pc \t\t%llu\n", (u64)pool->op_pc);

	seq_printf(m, "W4: stack_offset\t%d\nW4: shift\t\t%d\nW4: avg_level\t\t%d\n",
		   pool->stack_offset, pool->shift, pool->avg_level);
	seq_printf(m, "W4: avg_con \t\t%d\nW4: fc_ena\t\t%d\nW4: fc_stype\t\t%d\n",
		   pool->avg_con, pool->fc_ena, pool->fc_stype);
	seq_printf(m, "W4: fc_hyst_bits\t%d\nW4: fc_up_crossing\t%d\n",
		   pool->fc_hyst_bits, pool->fc_up_crossing);
	seq_printf(m, "W4: update_time\t\t%d\n", pool->update_time);

	seq_printf(m, "W5: fc_addr\t\t%llx\n", pool->fc_addr);

	seq_printf(m, "W6: ptr_start\t\t%llx\n", pool->ptr_start);

	seq_printf(m, "W7: ptr_end\t\t%llx\n", pool->ptr_end);

	seq_printf(m, "W8: err_int\t\t%d\nW8: err_int_ena\t\t%d\n",
		   pool->err_int, pool->err_int_ena);
	seq_printf(m, "W8: thresh_int\t\t%d\n", pool->thresh_int);
	seq_printf(m, "W8: thresh_int_ena\t%d\nW8: thresh_up\t\t%d\n",
		   pool->thresh_int_ena, pool->thresh_up);
	seq_printf(m, "W8: thresh_qint_idx\t%d\nW8: err_qint_idx\t\t%d\n",
		   pool->thresh_qint_idx, pool->err_qint_idx);
}

/* Reads aura/pool's ctx from admin queue */
static int rvu_dbg_npa_ctx_display(struct seq_file *m, void *unused, int ctype)
{
	void (*print_npa_ctx)(struct seq_file *m, struct npa_aq_enq_rsp *rsp);
	struct npa_aq_enq_req aq_req;
	struct npa_aq_enq_rsp rsp;
	struct rvu_pfvf *pfvf;
	int aura, rc, max_id;
	int npalf, id, all;
	struct rvu *rvu;
	u16 pcifunc;

	rvu = m->private;

	switch (ctype) {
	case NPA_AQ_CTYPE_AURA:
		npalf = rvu->rvu_dbg.npa_aura_ctx.lf;
		id = rvu->rvu_dbg.npa_aura_ctx.id;
		all = rvu->rvu_dbg.npa_aura_ctx.all;
		break;

	case NPA_AQ_CTYPE_POOL:
		npalf = rvu->rvu_dbg.npa_pool_ctx.lf;
		id = rvu->rvu_dbg.npa_pool_ctx.id;
		all = rvu->rvu_dbg.npa_pool_ctx.all;
		break;
	default:
		return -EINVAL;
	}

	if (!rvu_dbg_is_valid_lf(rvu, BLKTYPE_NPA, npalf, &pcifunc))
		return -EINVAL;

	pfvf = rvu_get_pfvf(rvu, pcifunc);
	if (ctype == NPA_AQ_CTYPE_AURA && !pfvf->aura_ctx) {
		seq_puts(m, "Aura context is not initialized\n");
		return -EINVAL;
	} else if (ctype == NPA_AQ_CTYPE_POOL && !pfvf->pool_ctx) {
		seq_puts(m, "Pool context is not initialized\n");
		return -EINVAL;
	}

	memset(&aq_req, 0, sizeof(struct npa_aq_enq_req));
	aq_req.hdr.pcifunc = pcifunc;
	aq_req.ctype = ctype;
	aq_req.op = NPA_AQ_INSTOP_READ;
	if (ctype == NPA_AQ_CTYPE_AURA) {
		max_id = pfvf->aura_ctx->qsize;
		print_npa_ctx = print_npa_aura_ctx;
	} else {
		max_id = pfvf->pool_ctx->qsize;
		print_npa_ctx = print_npa_pool_ctx;
	}

	if (id < 0 || id >= max_id) {
		seq_printf(m, "Invalid %s, valid range is 0-%d\n",
			   (ctype == NPA_AQ_CTYPE_AURA) ? "aura" : "pool",
			max_id - 1);
		return -EINVAL;
	}

	if (all)
		id = 0;
	else
		max_id = id + 1;

	for (aura = id; aura < max_id; aura++) {
		aq_req.aura_id = aura;
		seq_printf(m, "======%s : %d=======\n",
			   (ctype == NPA_AQ_CTYPE_AURA) ? "AURA" : "POOL",
			aq_req.aura_id);
		rc = rvu_npa_aq_enq_inst(rvu, &aq_req, &rsp);
		if (rc) {
			seq_puts(m, "Failed to read context\n");
			return -EINVAL;
		}
		print_npa_ctx(m, &rsp);
	}
	return 0;
}

static int write_npa_ctx(struct rvu *rvu, bool all,
			 int npalf, int id, int ctype)
{
	struct rvu_pfvf *pfvf;
	int max_id = 0;
	u16 pcifunc;

	if (!rvu_dbg_is_valid_lf(rvu, BLKTYPE_NPA, npalf, &pcifunc))
		return -EINVAL;

	pfvf = rvu_get_pfvf(rvu, pcifunc);

	if (ctype == NPA_AQ_CTYPE_AURA) {
		if (!pfvf->aura_ctx) {
			dev_warn(rvu->dev, "Aura context is not initialized\n");
			return -EINVAL;
		}
		max_id = pfvf->aura_ctx->qsize;
	} else if (ctype == NPA_AQ_CTYPE_POOL) {
		if (!pfvf->pool_ctx) {
			dev_warn(rvu->dev, "Pool context is not initialized\n");
			return -EINVAL;
		}
		max_id = pfvf->pool_ctx->qsize;
	}

	if (id < 0 || id >= max_id) {
		dev_warn(rvu->dev, "Invalid %s, valid range is 0-%d\n",
			 (ctype == NPA_AQ_CTYPE_AURA) ? "aura" : "pool",
			max_id - 1);
		return -EINVAL;
	}

	switch (ctype) {
	case NPA_AQ_CTYPE_AURA:
		rvu->rvu_dbg.npa_aura_ctx.lf = npalf;
		rvu->rvu_dbg.npa_aura_ctx.id = id;
		rvu->rvu_dbg.npa_aura_ctx.all = all;
		break;

	case NPA_AQ_CTYPE_POOL:
		rvu->rvu_dbg.npa_pool_ctx.lf = npalf;
		rvu->rvu_dbg.npa_pool_ctx.id = id;
		rvu->rvu_dbg.npa_pool_ctx.all = all;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int parse_cmd_buffer_ctx(char *cmd_buf, size_t *count,
				const char __user *buffer, int *npalf,
				int *id, bool *all)
{
	int bytes_not_copied;
	char *cmd_buf_tmp;
	char *subtoken;
	int ret;

	bytes_not_copied = copy_from_user(cmd_buf, buffer, *count);
	if (bytes_not_copied)
		return -EFAULT;

	cmd_buf[*count] = '\0';
	cmd_buf_tmp = strchr(cmd_buf, '\n');

	if (cmd_buf_tmp) {
		*cmd_buf_tmp = '\0';
		*count = cmd_buf_tmp - cmd_buf + 1;
	}

	subtoken = strsep(&cmd_buf, " ");
	ret = subtoken ? kstrtoint(subtoken, 10, npalf) : -EINVAL;
	if (ret < 0)
		return ret;
	subtoken = strsep(&cmd_buf, " ");
	if (subtoken && strcmp(subtoken, "all") == 0) {
		*all = true;
	} else {
		ret = subtoken ? kstrtoint(subtoken, 10, id) : -EINVAL;
		if (ret < 0)
			return ret;
	}
	if (cmd_buf)
		return -EINVAL;
	return ret;
}

static ssize_t rvu_dbg_npa_ctx_write(struct file *filp,
				     const char __user *buffer,
				     size_t count, loff_t *ppos, int ctype)
{
	char *cmd_buf, *ctype_string = (ctype == NPA_AQ_CTYPE_AURA) ?
					"aura" : "pool";
	struct seq_file *seqfp = filp->private_data;
	struct rvu *rvu = seqfp->private;
	int npalf, id = 0, ret;
	bool all = false;

	if ((*ppos != 0) || !count)
		return -EINVAL;

	cmd_buf = kzalloc(count + 1, GFP_KERNEL);
	if (!cmd_buf)
		return count;
	ret = parse_cmd_buffer_ctx(cmd_buf, &count, buffer,
				   &npalf, &id, &all);
	if (ret < 0) {
		dev_info(rvu->dev,
			 "Usage: echo <npalf> [%s number/all] > %s_ctx\n",
			 ctype_string, ctype_string);
		goto done;
	} else {
		ret = write_npa_ctx(rvu, all, npalf, id, ctype);
	}
done:
	kfree(cmd_buf);
	return ret ? ret : count;
}

static ssize_t rvu_dbg_npa_aura_ctx_write(struct file *filp,
					  const char __user *buffer,
					  size_t count, loff_t *ppos)
{
	return rvu_dbg_npa_ctx_write(filp, buffer, count, ppos,
				     NPA_AQ_CTYPE_AURA);
}

static int rvu_dbg_npa_aura_ctx_display(struct seq_file *filp, void *unused)
{
	return rvu_dbg_npa_ctx_display(filp, unused, NPA_AQ_CTYPE_AURA);
}

RVU_DEBUG_SEQ_FOPS(npa_aura_ctx, npa_aura_ctx_display, npa_aura_ctx_write);

static ssize_t rvu_dbg_npa_pool_ctx_write(struct file *filp,
					  const char __user *buffer,
					  size_t count, loff_t *ppos)
{
	return rvu_dbg_npa_ctx_write(filp, buffer, count, ppos,
				     NPA_AQ_CTYPE_POOL);
}

static int rvu_dbg_npa_pool_ctx_display(struct seq_file *filp, void *unused)
{
	return rvu_dbg_npa_ctx_display(filp, unused, NPA_AQ_CTYPE_POOL);
}

RVU_DEBUG_SEQ_FOPS(npa_pool_ctx, npa_pool_ctx_display, npa_pool_ctx_write);

static void rvu_dbg_npa_init(struct rvu *rvu)
{
	const struct device *dev = &rvu->pdev->dev;
	struct dentry *pfile;

	rvu->rvu_dbg.npa = debugfs_create_dir("npa", rvu->rvu_dbg.root);
	if (!rvu->rvu_dbg.npa)
		return;

	pfile = debugfs_create_file("qsize", 0600, rvu->rvu_dbg.npa, rvu,
				    &rvu_dbg_npa_qsize_fops);
	if (!pfile)
		goto create_failed;

	pfile = debugfs_create_file("aura_ctx", 0600, rvu->rvu_dbg.npa, rvu,
				    &rvu_dbg_npa_aura_ctx_fops);
	if (!pfile)
		goto create_failed;

	pfile = debugfs_create_file("pool_ctx", 0600, rvu->rvu_dbg.npa, rvu,
				    &rvu_dbg_npa_pool_ctx_fops);
	if (!pfile)
		goto create_failed;

	return;

create_failed:
	dev_err(dev, "Failed to create debugfs dir/file for NPA\n");
	debugfs_remove_recursive(rvu->rvu_dbg.npa);
}

void rvu_dbg_init(struct rvu *rvu)
{
	struct device *dev = &rvu->pdev->dev;
	struct dentry *pfile;

	rvu->rvu_dbg.root = debugfs_create_dir(DEBUGFS_DIR_NAME, NULL);
	if (!rvu->rvu_dbg.root) {
		dev_err(rvu->dev, "%s failed\n", __func__);
		return;
	}
	pfile = debugfs_create_file("rsrc_alloc", 0444, rvu->rvu_dbg.root, rvu,
				    &rvu_dbg_rsrc_status_fops);
	if (!pfile)
		goto create_failed;

	rvu_dbg_npa_init(rvu);

	return;

create_failed:
	dev_err(dev, "Failed to create debugfs dir\n");
	debugfs_remove_recursive(rvu->rvu_dbg.root);
}

void rvu_dbg_exit(struct rvu *rvu)
{
	debugfs_remove_recursive(rvu->rvu_dbg.root);
}

#endif /* CONFIG_DEBUG_FS */

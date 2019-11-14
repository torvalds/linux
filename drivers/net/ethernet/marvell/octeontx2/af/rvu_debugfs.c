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

#define NDC_MAX_BANK(rvu, blk_addr) (rvu_read64(rvu, \
						blk_addr, NDC_AF_CONST) & 0xFF)

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

static void print_nix_qsize(struct seq_file *filp, struct rvu_pfvf *pfvf);

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

	case BLKTYPE_NIX:
		qsize_id = rvu->rvu_dbg.nix_qsize_id;
		print_qsize = print_nix_qsize;
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
	else
		rvu->rvu_dbg.nix_qsize_id = lf;

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

static void ndc_cache_stats(struct seq_file *s, int blk_addr,
			    int ctype, int transaction)
{
	u64 req, out_req, lat, cant_alloc;
	struct rvu *rvu = s->private;
	int port;

	for (port = 0; port < NDC_MAX_PORT; port++) {
		req = rvu_read64(rvu, blk_addr, NDC_AF_PORTX_RTX_RWX_REQ_PC
						(port, ctype, transaction));
		lat = rvu_read64(rvu, blk_addr, NDC_AF_PORTX_RTX_RWX_LAT_PC
						(port, ctype, transaction));
		out_req = rvu_read64(rvu, blk_addr,
				     NDC_AF_PORTX_RTX_RWX_OSTDN_PC
				     (port, ctype, transaction));
		cant_alloc = rvu_read64(rvu, blk_addr,
					NDC_AF_PORTX_RTX_CANT_ALLOC_PC
					(port, transaction));
		seq_printf(s, "\nPort:%d\n", port);
		seq_printf(s, "\tTotal Requests:\t\t%lld\n", req);
		seq_printf(s, "\tTotal Time Taken:\t%lld cycles\n", lat);
		seq_printf(s, "\tAvg Latency:\t\t%lld cycles\n", lat / req);
		seq_printf(s, "\tOutstanding Requests:\t%lld\n", out_req);
		seq_printf(s, "\tCant Alloc Requests:\t%lld\n", cant_alloc);
	}
}

static int ndc_blk_cache_stats(struct seq_file *s, int idx, int blk_addr)
{
	seq_puts(s, "\n***** CACHE mode read stats *****\n");
	ndc_cache_stats(s, blk_addr, CACHING, NDC_READ_TRANS);
	seq_puts(s, "\n***** CACHE mode write stats *****\n");
	ndc_cache_stats(s, blk_addr, CACHING, NDC_WRITE_TRANS);
	seq_puts(s, "\n***** BY-PASS mode read stats *****\n");
	ndc_cache_stats(s, blk_addr, BYPASS, NDC_READ_TRANS);
	seq_puts(s, "\n***** BY-PASS mode write stats *****\n");
	ndc_cache_stats(s, blk_addr, BYPASS, NDC_WRITE_TRANS);
	return 0;
}

static int rvu_dbg_npa_ndc_cache_display(struct seq_file *filp, void *unused)
{
	return ndc_blk_cache_stats(filp, NPA0_U, BLKADDR_NDC_NPA0);
}

RVU_DEBUG_SEQ_FOPS(npa_ndc_cache, npa_ndc_cache_display, NULL);

static int ndc_blk_hits_miss_stats(struct seq_file *s, int idx, int blk_addr)
{
	struct rvu *rvu = s->private;
	int bank, max_bank;

	max_bank = NDC_MAX_BANK(rvu, blk_addr);
	for (bank = 0; bank < max_bank; bank++) {
		seq_printf(s, "BANK:%d\n", bank);
		seq_printf(s, "\tHits:\t%lld\n",
			   (u64)rvu_read64(rvu, blk_addr,
			   NDC_AF_BANKX_HIT_PC(bank)));
		seq_printf(s, "\tMiss:\t%lld\n",
			   (u64)rvu_read64(rvu, blk_addr,
			    NDC_AF_BANKX_MISS_PC(bank)));
	}
	return 0;
}

static int rvu_dbg_nix_ndc_rx_cache_display(struct seq_file *filp, void *unused)
{
	return ndc_blk_cache_stats(filp, NIX0_RX,
				   BLKADDR_NDC_NIX0_RX);
}

RVU_DEBUG_SEQ_FOPS(nix_ndc_rx_cache, nix_ndc_rx_cache_display, NULL);

static int rvu_dbg_nix_ndc_tx_cache_display(struct seq_file *filp, void *unused)
{
	return ndc_blk_cache_stats(filp, NIX0_TX,
				   BLKADDR_NDC_NIX0_TX);
}

RVU_DEBUG_SEQ_FOPS(nix_ndc_tx_cache, nix_ndc_tx_cache_display, NULL);

static int rvu_dbg_npa_ndc_hits_miss_display(struct seq_file *filp,
					     void *unused)
{
	return ndc_blk_hits_miss_stats(filp, NPA0_U, BLKADDR_NDC_NPA0);
}

RVU_DEBUG_SEQ_FOPS(npa_ndc_hits_miss, npa_ndc_hits_miss_display, NULL);

static int rvu_dbg_nix_ndc_rx_hits_miss_display(struct seq_file *filp,
						void *unused)
{
	return ndc_blk_hits_miss_stats(filp,
				      NPA0_U, BLKADDR_NDC_NIX0_RX);
}

RVU_DEBUG_SEQ_FOPS(nix_ndc_rx_hits_miss, nix_ndc_rx_hits_miss_display, NULL);

static int rvu_dbg_nix_ndc_tx_hits_miss_display(struct seq_file *filp,
						void *unused)
{
	return ndc_blk_hits_miss_stats(filp,
				      NPA0_U, BLKADDR_NDC_NIX0_TX);
}

RVU_DEBUG_SEQ_FOPS(nix_ndc_tx_hits_miss, nix_ndc_tx_hits_miss_display, NULL);

/* Dumps given nix_sq's context */
static void print_nix_sq_ctx(struct seq_file *m, struct nix_aq_enq_rsp *rsp)
{
	struct nix_sq_ctx_s *sq_ctx = &rsp->sq;

	seq_printf(m, "W0: sqe_way_mask \t\t%d\nW0: cq \t\t\t\t%d\n",
		   sq_ctx->sqe_way_mask, sq_ctx->cq);
	seq_printf(m, "W0: sdp_mcast \t\t\t%d\nW0: substream \t\t\t0x%03x\n",
		   sq_ctx->sdp_mcast, sq_ctx->substream);
	seq_printf(m, "W0: qint_idx \t\t\t%d\nW0: ena \t\t\t%d\n\n",
		   sq_ctx->qint_idx, sq_ctx->ena);

	seq_printf(m, "W1: sqb_count \t\t\t%d\nW1: default_chan \t\t%d\n",
		   sq_ctx->sqb_count, sq_ctx->default_chan);
	seq_printf(m, "W1: smq_rr_quantum \t\t%d\nW1: sso_ena \t\t\t%d\n",
		   sq_ctx->smq_rr_quantum, sq_ctx->sso_ena);
	seq_printf(m, "W1: xoff \t\t\t%d\nW1: cq_ena \t\t\t%d\nW1: smq\t\t\t\t%d\n\n",
		   sq_ctx->xoff, sq_ctx->cq_ena, sq_ctx->smq);

	seq_printf(m, "W2: sqe_stype \t\t\t%d\nW2: sq_int_ena \t\t\t%d\n",
		   sq_ctx->sqe_stype, sq_ctx->sq_int_ena);
	seq_printf(m, "W2: sq_int \t\t\t%d\nW2: sqb_aura \t\t\t%d\n",
		   sq_ctx->sq_int, sq_ctx->sqb_aura);
	seq_printf(m, "W2: smq_rr_count \t\t%d\n\n", sq_ctx->smq_rr_count);

	seq_printf(m, "W3: smq_next_sq_vld\t\t%d\nW3: smq_pend\t\t\t%d\n",
		   sq_ctx->smq_next_sq_vld, sq_ctx->smq_pend);
	seq_printf(m, "W3: smenq_next_sqb_vld \t\t%d\nW3: head_offset\t\t\t%d\n",
		   sq_ctx->smenq_next_sqb_vld, sq_ctx->head_offset);
	seq_printf(m, "W3: smenq_offset\t\t%d\nW3: tail_offset\t\t\t%d\n",
		   sq_ctx->smenq_offset, sq_ctx->tail_offset);
	seq_printf(m, "W3: smq_lso_segnum \t\t%d\nW3: smq_next_sq\t\t\t%d\n",
		   sq_ctx->smq_lso_segnum, sq_ctx->smq_next_sq);
	seq_printf(m, "W3: mnq_dis \t\t\t%d\nW3: lmt_dis \t\t\t%d\n",
		   sq_ctx->mnq_dis, sq_ctx->lmt_dis);
	seq_printf(m, "W3: cq_limit\t\t\t%d\nW3: max_sqe_size\t\t%d\n\n",
		   sq_ctx->cq_limit, sq_ctx->max_sqe_size);

	seq_printf(m, "W4: next_sqb \t\t\t%llx\n\n", sq_ctx->next_sqb);
	seq_printf(m, "W5: tail_sqb \t\t\t%llx\n\n", sq_ctx->tail_sqb);
	seq_printf(m, "W6: smenq_sqb \t\t\t%llx\n\n", sq_ctx->smenq_sqb);
	seq_printf(m, "W7: smenq_next_sqb \t\t%llx\n\n",
		   sq_ctx->smenq_next_sqb);

	seq_printf(m, "W8: head_sqb\t\t\t%llx\n\n", sq_ctx->head_sqb);

	seq_printf(m, "W9: vfi_lso_vld\t\t\t%d\nW9: vfi_lso_vlan1_ins_ena\t%d\n",
		   sq_ctx->vfi_lso_vld, sq_ctx->vfi_lso_vlan1_ins_ena);
	seq_printf(m, "W9: vfi_lso_vlan0_ins_ena\t%d\nW9: vfi_lso_mps\t\t\t%d\n",
		   sq_ctx->vfi_lso_vlan0_ins_ena, sq_ctx->vfi_lso_mps);
	seq_printf(m, "W9: vfi_lso_sb\t\t\t%d\nW9: vfi_lso_sizem1\t\t%d\n",
		   sq_ctx->vfi_lso_sb, sq_ctx->vfi_lso_sizem1);
	seq_printf(m, "W9: vfi_lso_total\t\t%d\n\n", sq_ctx->vfi_lso_total);

	seq_printf(m, "W10: scm_lso_rem \t\t%llu\n\n",
		   (u64)sq_ctx->scm_lso_rem);
	seq_printf(m, "W11: octs \t\t\t%llu\n\n", (u64)sq_ctx->octs);
	seq_printf(m, "W12: pkts \t\t\t%llu\n\n", (u64)sq_ctx->pkts);
	seq_printf(m, "W14: dropped_octs \t\t%llu\n\n",
		   (u64)sq_ctx->dropped_octs);
	seq_printf(m, "W15: dropped_pkts \t\t%llu\n\n",
		   (u64)sq_ctx->dropped_pkts);
}

/* Dumps given nix_rq's context */
static void print_nix_rq_ctx(struct seq_file *m, struct nix_aq_enq_rsp *rsp)
{
	struct nix_rq_ctx_s *rq_ctx = &rsp->rq;

	seq_printf(m, "W0: wqe_aura \t\t\t%d\nW0: substream \t\t\t0x%03x\n",
		   rq_ctx->wqe_aura, rq_ctx->substream);
	seq_printf(m, "W0: cq \t\t\t\t%d\nW0: ena_wqwd \t\t\t%d\n",
		   rq_ctx->cq, rq_ctx->ena_wqwd);
	seq_printf(m, "W0: ipsech_ena \t\t\t%d\nW0: sso_ena \t\t\t%d\n",
		   rq_ctx->ipsech_ena, rq_ctx->sso_ena);
	seq_printf(m, "W0: ena \t\t\t%d\n\n", rq_ctx->ena);

	seq_printf(m, "W1: lpb_drop_ena \t\t%d\nW1: spb_drop_ena \t\t%d\n",
		   rq_ctx->lpb_drop_ena, rq_ctx->spb_drop_ena);
	seq_printf(m, "W1: xqe_drop_ena \t\t%d\nW1: wqe_caching \t\t%d\n",
		   rq_ctx->xqe_drop_ena, rq_ctx->wqe_caching);
	seq_printf(m, "W1: pb_caching \t\t\t%d\nW1: sso_tt \t\t\t%d\n",
		   rq_ctx->pb_caching, rq_ctx->sso_tt);
	seq_printf(m, "W1: sso_grp \t\t\t%d\nW1: lpb_aura \t\t\t%d\n",
		   rq_ctx->sso_grp, rq_ctx->lpb_aura);
	seq_printf(m, "W1: spb_aura \t\t\t%d\n\n", rq_ctx->spb_aura);

	seq_printf(m, "W2: xqe_hdr_split \t\t%d\nW2: xqe_imm_copy \t\t%d\n",
		   rq_ctx->xqe_hdr_split, rq_ctx->xqe_imm_copy);
	seq_printf(m, "W2: xqe_imm_size \t\t%d\nW2: later_skip \t\t\t%d\n",
		   rq_ctx->xqe_imm_size, rq_ctx->later_skip);
	seq_printf(m, "W2: first_skip \t\t\t%d\nW2: lpb_sizem1 \t\t\t%d\n",
		   rq_ctx->first_skip, rq_ctx->lpb_sizem1);
	seq_printf(m, "W2: spb_ena \t\t\t%d\nW2: wqe_skip \t\t\t%d\n",
		   rq_ctx->spb_ena, rq_ctx->wqe_skip);
	seq_printf(m, "W2: spb_sizem1 \t\t\t%d\n\n", rq_ctx->spb_sizem1);

	seq_printf(m, "W3: spb_pool_pass \t\t%d\nW3: spb_pool_drop \t\t%d\n",
		   rq_ctx->spb_pool_pass, rq_ctx->spb_pool_drop);
	seq_printf(m, "W3: spb_aura_pass \t\t%d\nW3: spb_aura_drop \t\t%d\n",
		   rq_ctx->spb_aura_pass, rq_ctx->spb_aura_drop);
	seq_printf(m, "W3: wqe_pool_pass \t\t%d\nW3: wqe_pool_drop \t\t%d\n",
		   rq_ctx->wqe_pool_pass, rq_ctx->wqe_pool_drop);
	seq_printf(m, "W3: xqe_pass \t\t\t%d\nW3: xqe_drop \t\t\t%d\n\n",
		   rq_ctx->xqe_pass, rq_ctx->xqe_drop);

	seq_printf(m, "W4: qint_idx \t\t\t%d\nW4: rq_int_ena \t\t\t%d\n",
		   rq_ctx->qint_idx, rq_ctx->rq_int_ena);
	seq_printf(m, "W4: rq_int \t\t\t%d\nW4: lpb_pool_pass \t\t%d\n",
		   rq_ctx->rq_int, rq_ctx->lpb_pool_pass);
	seq_printf(m, "W4: lpb_pool_drop \t\t%d\nW4: lpb_aura_pass \t\t%d\n",
		   rq_ctx->lpb_pool_drop, rq_ctx->lpb_aura_pass);
	seq_printf(m, "W4: lpb_aura_drop \t\t%d\n\n", rq_ctx->lpb_aura_drop);

	seq_printf(m, "W5: flow_tagw \t\t\t%d\nW5: bad_utag \t\t\t%d\n",
		   rq_ctx->flow_tagw, rq_ctx->bad_utag);
	seq_printf(m, "W5: good_utag \t\t\t%d\nW5: ltag \t\t\t%d\n\n",
		   rq_ctx->good_utag, rq_ctx->ltag);

	seq_printf(m, "W6: octs \t\t\t%llu\n\n", (u64)rq_ctx->octs);
	seq_printf(m, "W7: pkts \t\t\t%llu\n\n", (u64)rq_ctx->pkts);
	seq_printf(m, "W8: drop_octs \t\t\t%llu\n\n", (u64)rq_ctx->drop_octs);
	seq_printf(m, "W9: drop_pkts \t\t\t%llu\n\n", (u64)rq_ctx->drop_pkts);
	seq_printf(m, "W10: re_pkts \t\t\t%llu\n", (u64)rq_ctx->re_pkts);
}

/* Dumps given nix_cq's context */
static void print_nix_cq_ctx(struct seq_file *m, struct nix_aq_enq_rsp *rsp)
{
	struct nix_cq_ctx_s *cq_ctx = &rsp->cq;

	seq_printf(m, "W0: base \t\t\t%llx\n\n", cq_ctx->base);

	seq_printf(m, "W1: wrptr \t\t\t%llx\n", (u64)cq_ctx->wrptr);
	seq_printf(m, "W1: avg_con \t\t\t%d\nW1: cint_idx \t\t\t%d\n",
		   cq_ctx->avg_con, cq_ctx->cint_idx);
	seq_printf(m, "W1: cq_err \t\t\t%d\nW1: qint_idx \t\t\t%d\n",
		   cq_ctx->cq_err, cq_ctx->qint_idx);
	seq_printf(m, "W1: bpid \t\t\t%d\nW1: bp_ena \t\t\t%d\n\n",
		   cq_ctx->bpid, cq_ctx->bp_ena);

	seq_printf(m, "W2: update_time \t\t%d\nW2:avg_level \t\t\t%d\n",
		   cq_ctx->update_time, cq_ctx->avg_level);
	seq_printf(m, "W2: head \t\t\t%d\nW2:tail \t\t\t%d\n\n",
		   cq_ctx->head, cq_ctx->tail);

	seq_printf(m, "W3: cq_err_int_ena \t\t%d\nW3:cq_err_int \t\t\t%d\n",
		   cq_ctx->cq_err_int_ena, cq_ctx->cq_err_int);
	seq_printf(m, "W3: qsize \t\t\t%d\nW3:caching \t\t\t%d\n",
		   cq_ctx->qsize, cq_ctx->caching);
	seq_printf(m, "W3: substream \t\t\t0x%03x\nW3: ena \t\t\t%d\n",
		   cq_ctx->substream, cq_ctx->ena);
	seq_printf(m, "W3: drop_ena \t\t\t%d\nW3: drop \t\t\t%d\n",
		   cq_ctx->drop_ena, cq_ctx->drop);
	seq_printf(m, "W3: bp \t\t\t\t%d\n\n", cq_ctx->bp);
}

static int rvu_dbg_nix_queue_ctx_display(struct seq_file *filp,
					 void *unused, int ctype)
{
	void (*print_nix_ctx)(struct seq_file *filp,
			      struct nix_aq_enq_rsp *rsp) = NULL;
	struct rvu *rvu = filp->private;
	struct nix_aq_enq_req aq_req;
	struct nix_aq_enq_rsp rsp;
	char *ctype_string = NULL;
	int qidx, rc, max_id = 0;
	struct rvu_pfvf *pfvf;
	int nixlf, id, all;
	u16 pcifunc;

	switch (ctype) {
	case NIX_AQ_CTYPE_CQ:
		nixlf = rvu->rvu_dbg.nix_cq_ctx.lf;
		id = rvu->rvu_dbg.nix_cq_ctx.id;
		all = rvu->rvu_dbg.nix_cq_ctx.all;
		break;

	case NIX_AQ_CTYPE_SQ:
		nixlf = rvu->rvu_dbg.nix_sq_ctx.lf;
		id = rvu->rvu_dbg.nix_sq_ctx.id;
		all = rvu->rvu_dbg.nix_sq_ctx.all;
		break;

	case NIX_AQ_CTYPE_RQ:
		nixlf = rvu->rvu_dbg.nix_rq_ctx.lf;
		id = rvu->rvu_dbg.nix_rq_ctx.id;
		all = rvu->rvu_dbg.nix_rq_ctx.all;
		break;

	default:
		return -EINVAL;
	}

	if (!rvu_dbg_is_valid_lf(rvu, BLKTYPE_NIX, nixlf, &pcifunc))
		return -EINVAL;

	pfvf = rvu_get_pfvf(rvu, pcifunc);
	if (ctype == NIX_AQ_CTYPE_SQ && !pfvf->sq_ctx) {
		seq_puts(filp, "SQ context is not initialized\n");
		return -EINVAL;
	} else if (ctype == NIX_AQ_CTYPE_RQ && !pfvf->rq_ctx) {
		seq_puts(filp, "RQ context is not initialized\n");
		return -EINVAL;
	} else if (ctype == NIX_AQ_CTYPE_CQ && !pfvf->cq_ctx) {
		seq_puts(filp, "CQ context is not initialized\n");
		return -EINVAL;
	}

	if (ctype == NIX_AQ_CTYPE_SQ) {
		max_id = pfvf->sq_ctx->qsize;
		ctype_string = "sq";
		print_nix_ctx = print_nix_sq_ctx;
	} else if (ctype == NIX_AQ_CTYPE_RQ) {
		max_id = pfvf->rq_ctx->qsize;
		ctype_string = "rq";
		print_nix_ctx = print_nix_rq_ctx;
	} else if (ctype == NIX_AQ_CTYPE_CQ) {
		max_id = pfvf->cq_ctx->qsize;
		ctype_string = "cq";
		print_nix_ctx = print_nix_cq_ctx;
	}

	memset(&aq_req, 0, sizeof(struct nix_aq_enq_req));
	aq_req.hdr.pcifunc = pcifunc;
	aq_req.ctype = ctype;
	aq_req.op = NIX_AQ_INSTOP_READ;
	if (all)
		id = 0;
	else
		max_id = id + 1;
	for (qidx = id; qidx < max_id; qidx++) {
		aq_req.qidx = qidx;
		seq_printf(filp, "=====%s_ctx for nixlf:%d and qidx:%d is=====\n",
			   ctype_string, nixlf, aq_req.qidx);
		rc = rvu_mbox_handler_nix_aq_enq(rvu, &aq_req, &rsp);
		if (rc) {
			seq_puts(filp, "Failed to read the context\n");
			return -EINVAL;
		}
		print_nix_ctx(filp, &rsp);
	}
	return 0;
}

static int write_nix_queue_ctx(struct rvu *rvu, bool all, int nixlf,
			       int id, int ctype, char *ctype_string)
{
	struct rvu_pfvf *pfvf;
	int max_id = 0;
	u16 pcifunc;

	if (!rvu_dbg_is_valid_lf(rvu, BLKTYPE_NIX, nixlf, &pcifunc))
		return -EINVAL;

	pfvf = rvu_get_pfvf(rvu, pcifunc);

	if (ctype == NIX_AQ_CTYPE_SQ) {
		if (!pfvf->sq_ctx) {
			dev_warn(rvu->dev, "SQ context is not initialized\n");
			return -EINVAL;
		}
		max_id = pfvf->sq_ctx->qsize;
	} else if (ctype == NIX_AQ_CTYPE_RQ) {
		if (!pfvf->rq_ctx) {
			dev_warn(rvu->dev, "RQ context is not initialized\n");
			return -EINVAL;
		}
		max_id = pfvf->rq_ctx->qsize;
	} else if (ctype == NIX_AQ_CTYPE_CQ) {
		if (!pfvf->cq_ctx) {
			dev_warn(rvu->dev, "CQ context is not initialized\n");
			return -EINVAL;
		}
		max_id = pfvf->cq_ctx->qsize;
	}

	if (id < 0 || id >= max_id) {
		dev_warn(rvu->dev, "Invalid %s_ctx valid range 0-%d\n",
			 ctype_string, max_id - 1);
		return -EINVAL;
	}
	switch (ctype) {
	case NIX_AQ_CTYPE_CQ:
		rvu->rvu_dbg.nix_cq_ctx.lf = nixlf;
		rvu->rvu_dbg.nix_cq_ctx.id = id;
		rvu->rvu_dbg.nix_cq_ctx.all = all;
		break;

	case NIX_AQ_CTYPE_SQ:
		rvu->rvu_dbg.nix_sq_ctx.lf = nixlf;
		rvu->rvu_dbg.nix_sq_ctx.id = id;
		rvu->rvu_dbg.nix_sq_ctx.all = all;
		break;

	case NIX_AQ_CTYPE_RQ:
		rvu->rvu_dbg.nix_rq_ctx.lf = nixlf;
		rvu->rvu_dbg.nix_rq_ctx.id = id;
		rvu->rvu_dbg.nix_rq_ctx.all = all;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static ssize_t rvu_dbg_nix_queue_ctx_write(struct file *filp,
					   const char __user *buffer,
					   size_t count, loff_t *ppos,
					   int ctype)
{
	struct seq_file *m = filp->private_data;
	struct rvu *rvu = m->private;
	char *cmd_buf, *ctype_string;
	int nixlf, id = 0, ret;
	bool all = false;

	if ((*ppos != 0) || !count)
		return -EINVAL;

	switch (ctype) {
	case NIX_AQ_CTYPE_SQ:
		ctype_string = "sq";
		break;
	case NIX_AQ_CTYPE_RQ:
		ctype_string = "rq";
		break;
	case NIX_AQ_CTYPE_CQ:
		ctype_string = "cq";
		break;
	default:
		return -EINVAL;
	}

	cmd_buf = kzalloc(count + 1, GFP_KERNEL);

	if (!cmd_buf)
		return count;

	ret = parse_cmd_buffer_ctx(cmd_buf, &count, buffer,
				   &nixlf, &id, &all);
	if (ret < 0) {
		dev_info(rvu->dev,
			 "Usage: echo <nixlf> [%s number/all] > %s_ctx\n",
			 ctype_string, ctype_string);
		goto done;
	} else {
		ret = write_nix_queue_ctx(rvu, all, nixlf, id, ctype,
					  ctype_string);
	}
done:
	kfree(cmd_buf);
	return ret ? ret : count;
}

static ssize_t rvu_dbg_nix_sq_ctx_write(struct file *filp,
					const char __user *buffer,
					size_t count, loff_t *ppos)
{
	return rvu_dbg_nix_queue_ctx_write(filp, buffer, count, ppos,
					    NIX_AQ_CTYPE_SQ);
}

static int rvu_dbg_nix_sq_ctx_display(struct seq_file *filp, void *unused)
{
	return rvu_dbg_nix_queue_ctx_display(filp, unused, NIX_AQ_CTYPE_SQ);
}

RVU_DEBUG_SEQ_FOPS(nix_sq_ctx, nix_sq_ctx_display, nix_sq_ctx_write);

static ssize_t rvu_dbg_nix_rq_ctx_write(struct file *filp,
					const char __user *buffer,
					size_t count, loff_t *ppos)
{
	return rvu_dbg_nix_queue_ctx_write(filp, buffer, count, ppos,
					    NIX_AQ_CTYPE_RQ);
}

static int rvu_dbg_nix_rq_ctx_display(struct seq_file *filp, void  *unused)
{
	return rvu_dbg_nix_queue_ctx_display(filp, unused,  NIX_AQ_CTYPE_RQ);
}

RVU_DEBUG_SEQ_FOPS(nix_rq_ctx, nix_rq_ctx_display, nix_rq_ctx_write);

static ssize_t rvu_dbg_nix_cq_ctx_write(struct file *filp,
					const char __user *buffer,
					size_t count, loff_t *ppos)
{
	return rvu_dbg_nix_queue_ctx_write(filp, buffer, count, ppos,
					    NIX_AQ_CTYPE_CQ);
}

static int rvu_dbg_nix_cq_ctx_display(struct seq_file *filp, void *unused)
{
	return rvu_dbg_nix_queue_ctx_display(filp, unused, NIX_AQ_CTYPE_CQ);
}

RVU_DEBUG_SEQ_FOPS(nix_cq_ctx, nix_cq_ctx_display, nix_cq_ctx_write);

static void print_nix_qctx_qsize(struct seq_file *filp, int qsize,
				 unsigned long *bmap, char *qtype)
{
	char *buf;

	buf = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (!buf)
		return;

	bitmap_print_to_pagebuf(false, buf, bmap, qsize);
	seq_printf(filp, "%s context count : %d\n", qtype, qsize);
	seq_printf(filp, "%s context ena/dis bitmap : %s\n",
		   qtype, buf);
	kfree(buf);
}

static void print_nix_qsize(struct seq_file *filp, struct rvu_pfvf *pfvf)
{
	if (!pfvf->cq_ctx)
		seq_puts(filp, "cq context is not initialized\n");
	else
		print_nix_qctx_qsize(filp, pfvf->cq_ctx->qsize, pfvf->cq_bmap,
				     "cq");

	if (!pfvf->rq_ctx)
		seq_puts(filp, "rq context is not initialized\n");
	else
		print_nix_qctx_qsize(filp, pfvf->rq_ctx->qsize, pfvf->rq_bmap,
				     "rq");

	if (!pfvf->sq_ctx)
		seq_puts(filp, "sq context is not initialized\n");
	else
		print_nix_qctx_qsize(filp, pfvf->sq_ctx->qsize, pfvf->sq_bmap,
				     "sq");
}

static ssize_t rvu_dbg_nix_qsize_write(struct file *filp,
				       const char __user *buffer,
				       size_t count, loff_t *ppos)
{
	return rvu_dbg_qsize_write(filp, buffer, count, ppos,
				   BLKTYPE_NIX);
}

static int rvu_dbg_nix_qsize_display(struct seq_file *filp, void *unused)
{
	return rvu_dbg_qsize_display(filp, unused, BLKTYPE_NIX);
}

RVU_DEBUG_SEQ_FOPS(nix_qsize, nix_qsize_display, nix_qsize_write);

static void rvu_dbg_nix_init(struct rvu *rvu)
{
	const struct device *dev = &rvu->pdev->dev;
	struct dentry *pfile;

	rvu->rvu_dbg.nix = debugfs_create_dir("nix", rvu->rvu_dbg.root);
	if (!rvu->rvu_dbg.nix) {
		dev_err(rvu->dev, "create debugfs dir failed for nix\n");
		return;
	}

	pfile = debugfs_create_file("sq_ctx", 0600, rvu->rvu_dbg.nix, rvu,
				    &rvu_dbg_nix_sq_ctx_fops);
	if (!pfile)
		goto create_failed;

	pfile = debugfs_create_file("rq_ctx", 0600, rvu->rvu_dbg.nix, rvu,
				    &rvu_dbg_nix_rq_ctx_fops);
	if (!pfile)
		goto create_failed;

	pfile = debugfs_create_file("cq_ctx", 0600, rvu->rvu_dbg.nix, rvu,
				    &rvu_dbg_nix_cq_ctx_fops);
	if (!pfile)
		goto create_failed;

	pfile = debugfs_create_file("ndc_tx_cache", 0600, rvu->rvu_dbg.nix, rvu,
				    &rvu_dbg_nix_ndc_tx_cache_fops);
	if (!pfile)
		goto create_failed;

	pfile = debugfs_create_file("ndc_rx_cache", 0600, rvu->rvu_dbg.nix, rvu,
				    &rvu_dbg_nix_ndc_rx_cache_fops);
	if (!pfile)
		goto create_failed;

	pfile = debugfs_create_file("ndc_tx_hits_miss", 0600, rvu->rvu_dbg.nix,
				    rvu, &rvu_dbg_nix_ndc_tx_hits_miss_fops);
	if (!pfile)
		goto create_failed;

	pfile = debugfs_create_file("ndc_rx_hits_miss", 0600, rvu->rvu_dbg.nix,
				    rvu, &rvu_dbg_nix_ndc_rx_hits_miss_fops);
	if (!pfile)
		goto create_failed;

	pfile = debugfs_create_file("qsize", 0600, rvu->rvu_dbg.nix, rvu,
				    &rvu_dbg_nix_qsize_fops);
	if (!pfile)
		goto create_failed;

	return;
create_failed:
	dev_err(dev, "Failed to create debugfs dir/file for NIX\n");
	debugfs_remove_recursive(rvu->rvu_dbg.nix);
}

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

	pfile = debugfs_create_file("ndc_cache", 0600, rvu->rvu_dbg.npa, rvu,
				    &rvu_dbg_npa_ndc_cache_fops);
	if (!pfile)
		goto create_failed;

	pfile = debugfs_create_file("ndc_hits_miss", 0600, rvu->rvu_dbg.npa,
				    rvu, &rvu_dbg_npa_ndc_hits_miss_fops);
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
	rvu_dbg_nix_init(rvu);

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

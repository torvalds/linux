/*
 * Copyright (c) 2013, Mellanox Technologies inc.  All rights reserved.
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

#include <asm-generic/kmap_types.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/pci.h>
#include <linux/dma-mapping.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/random.h>
#include <linux/io-mapping.h>
#include <linux/mlx5/driver.h>
#include <linux/debugfs.h>

#include "mlx5_core.h"

enum {
	CMD_IF_REV = 5,
};

enum {
	CMD_MODE_POLLING,
	CMD_MODE_EVENTS
};

enum {
	NUM_LONG_LISTS	  = 2,
	NUM_MED_LISTS	  = 64,
	LONG_LIST_SIZE	  = (2ULL * 1024 * 1024 * 1024 / PAGE_SIZE) * 8 + 16 +
				MLX5_CMD_DATA_BLOCK_SIZE,
	MED_LIST_SIZE	  = 16 + MLX5_CMD_DATA_BLOCK_SIZE,
};

enum {
	MLX5_CMD_DELIVERY_STAT_OK			= 0x0,
	MLX5_CMD_DELIVERY_STAT_SIGNAT_ERR		= 0x1,
	MLX5_CMD_DELIVERY_STAT_TOK_ERR			= 0x2,
	MLX5_CMD_DELIVERY_STAT_BAD_BLK_NUM_ERR		= 0x3,
	MLX5_CMD_DELIVERY_STAT_OUT_PTR_ALIGN_ERR	= 0x4,
	MLX5_CMD_DELIVERY_STAT_IN_PTR_ALIGN_ERR		= 0x5,
	MLX5_CMD_DELIVERY_STAT_FW_ERR			= 0x6,
	MLX5_CMD_DELIVERY_STAT_IN_LENGTH_ERR		= 0x7,
	MLX5_CMD_DELIVERY_STAT_OUT_LENGTH_ERR		= 0x8,
	MLX5_CMD_DELIVERY_STAT_RES_FLD_NOT_CLR_ERR	= 0x9,
	MLX5_CMD_DELIVERY_STAT_CMD_DESCR_ERR		= 0x10,
};

enum {
	MLX5_CMD_STAT_OK			= 0x0,
	MLX5_CMD_STAT_INT_ERR			= 0x1,
	MLX5_CMD_STAT_BAD_OP_ERR		= 0x2,
	MLX5_CMD_STAT_BAD_PARAM_ERR		= 0x3,
	MLX5_CMD_STAT_BAD_SYS_STATE_ERR		= 0x4,
	MLX5_CMD_STAT_BAD_RES_ERR		= 0x5,
	MLX5_CMD_STAT_RES_BUSY			= 0x6,
	MLX5_CMD_STAT_LIM_ERR			= 0x8,
	MLX5_CMD_STAT_BAD_RES_STATE_ERR		= 0x9,
	MLX5_CMD_STAT_IX_ERR			= 0xa,
	MLX5_CMD_STAT_NO_RES_ERR		= 0xf,
	MLX5_CMD_STAT_BAD_INP_LEN_ERR		= 0x50,
	MLX5_CMD_STAT_BAD_OUTP_LEN_ERR		= 0x51,
	MLX5_CMD_STAT_BAD_QP_STATE_ERR		= 0x10,
	MLX5_CMD_STAT_BAD_PKT_ERR		= 0x30,
	MLX5_CMD_STAT_BAD_SIZE_OUTS_CQES_ERR	= 0x40,
};

static struct mlx5_cmd_work_ent *alloc_cmd(struct mlx5_cmd *cmd,
					   struct mlx5_cmd_msg *in,
					   struct mlx5_cmd_msg *out,
					   void *uout, int uout_size,
					   mlx5_cmd_cbk_t cbk,
					   void *context, int page_queue)
{
	gfp_t alloc_flags = cbk ? GFP_ATOMIC : GFP_KERNEL;
	struct mlx5_cmd_work_ent *ent;

	ent = kzalloc(sizeof(*ent), alloc_flags);
	if (!ent)
		return ERR_PTR(-ENOMEM);

	ent->in		= in;
	ent->out	= out;
	ent->uout	= uout;
	ent->uout_size	= uout_size;
	ent->callback	= cbk;
	ent->context	= context;
	ent->cmd	= cmd;
	ent->page_queue = page_queue;

	return ent;
}

static u8 alloc_token(struct mlx5_cmd *cmd)
{
	u8 token;

	spin_lock(&cmd->token_lock);
	token = cmd->token++ % 255 + 1;
	spin_unlock(&cmd->token_lock);

	return token;
}

static int alloc_ent(struct mlx5_cmd *cmd)
{
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&cmd->alloc_lock, flags);
	ret = find_first_bit(&cmd->bitmask, cmd->max_reg_cmds);
	if (ret < cmd->max_reg_cmds)
		clear_bit(ret, &cmd->bitmask);
	spin_unlock_irqrestore(&cmd->alloc_lock, flags);

	return ret < cmd->max_reg_cmds ? ret : -ENOMEM;
}

static void free_ent(struct mlx5_cmd *cmd, int idx)
{
	unsigned long flags;

	spin_lock_irqsave(&cmd->alloc_lock, flags);
	set_bit(idx, &cmd->bitmask);
	spin_unlock_irqrestore(&cmd->alloc_lock, flags);
}

static struct mlx5_cmd_layout *get_inst(struct mlx5_cmd *cmd, int idx)
{
	return cmd->cmd_buf + (idx << cmd->log_stride);
}

static u8 xor8_buf(void *buf, int len)
{
	u8 *ptr = buf;
	u8 sum = 0;
	int i;

	for (i = 0; i < len; i++)
		sum ^= ptr[i];

	return sum;
}

static int verify_block_sig(struct mlx5_cmd_prot_block *block)
{
	if (xor8_buf(block->rsvd0, sizeof(*block) - sizeof(block->data) - 1) != 0xff)
		return -EINVAL;

	if (xor8_buf(block, sizeof(*block)) != 0xff)
		return -EINVAL;

	return 0;
}

static void calc_block_sig(struct mlx5_cmd_prot_block *block, u8 token,
			   int csum)
{
	block->token = token;
	if (csum) {
		block->ctrl_sig = ~xor8_buf(block->rsvd0, sizeof(*block) -
					    sizeof(block->data) - 2);
		block->sig = ~xor8_buf(block, sizeof(*block) - 1);
	}
}

static void calc_chain_sig(struct mlx5_cmd_msg *msg, u8 token, int csum)
{
	struct mlx5_cmd_mailbox *next = msg->next;

	while (next) {
		calc_block_sig(next->buf, token, csum);
		next = next->next;
	}
}

static void set_signature(struct mlx5_cmd_work_ent *ent, int csum)
{
	ent->lay->sig = ~xor8_buf(ent->lay, sizeof(*ent->lay));
	calc_chain_sig(ent->in, ent->token, csum);
	calc_chain_sig(ent->out, ent->token, csum);
}

static void poll_timeout(struct mlx5_cmd_work_ent *ent)
{
	unsigned long poll_end = jiffies + msecs_to_jiffies(MLX5_CMD_TIMEOUT_MSEC + 1000);
	u8 own;

	do {
		own = ent->lay->status_own;
		if (!(own & CMD_OWNER_HW)) {
			ent->ret = 0;
			return;
		}
		usleep_range(5000, 10000);
	} while (time_before(jiffies, poll_end));

	ent->ret = -ETIMEDOUT;
}

static void free_cmd(struct mlx5_cmd_work_ent *ent)
{
	kfree(ent);
}


static int verify_signature(struct mlx5_cmd_work_ent *ent)
{
	struct mlx5_cmd_mailbox *next = ent->out->next;
	int err;
	u8 sig;

	sig = xor8_buf(ent->lay, sizeof(*ent->lay));
	if (sig != 0xff)
		return -EINVAL;

	while (next) {
		err = verify_block_sig(next->buf);
		if (err)
			return err;

		next = next->next;
	}

	return 0;
}

static void dump_buf(void *buf, int size, int data_only, int offset)
{
	__be32 *p = buf;
	int i;

	for (i = 0; i < size; i += 16) {
		pr_debug("%03x: %08x %08x %08x %08x\n", offset, be32_to_cpu(p[0]),
			 be32_to_cpu(p[1]), be32_to_cpu(p[2]),
			 be32_to_cpu(p[3]));
		p += 4;
		offset += 16;
	}
	if (!data_only)
		pr_debug("\n");
}

const char *mlx5_command_str(int command)
{
	switch (command) {
	case MLX5_CMD_OP_QUERY_HCA_CAP:
		return "QUERY_HCA_CAP";

	case MLX5_CMD_OP_SET_HCA_CAP:
		return "SET_HCA_CAP";

	case MLX5_CMD_OP_QUERY_ADAPTER:
		return "QUERY_ADAPTER";

	case MLX5_CMD_OP_INIT_HCA:
		return "INIT_HCA";

	case MLX5_CMD_OP_TEARDOWN_HCA:
		return "TEARDOWN_HCA";

	case MLX5_CMD_OP_ENABLE_HCA:
		return "MLX5_CMD_OP_ENABLE_HCA";

	case MLX5_CMD_OP_DISABLE_HCA:
		return "MLX5_CMD_OP_DISABLE_HCA";

	case MLX5_CMD_OP_QUERY_PAGES:
		return "QUERY_PAGES";

	case MLX5_CMD_OP_MANAGE_PAGES:
		return "MANAGE_PAGES";

	case MLX5_CMD_OP_CREATE_MKEY:
		return "CREATE_MKEY";

	case MLX5_CMD_OP_QUERY_MKEY:
		return "QUERY_MKEY";

	case MLX5_CMD_OP_DESTROY_MKEY:
		return "DESTROY_MKEY";

	case MLX5_CMD_OP_QUERY_SPECIAL_CONTEXTS:
		return "QUERY_SPECIAL_CONTEXTS";

	case MLX5_CMD_OP_CREATE_EQ:
		return "CREATE_EQ";

	case MLX5_CMD_OP_DESTROY_EQ:
		return "DESTROY_EQ";

	case MLX5_CMD_OP_QUERY_EQ:
		return "QUERY_EQ";

	case MLX5_CMD_OP_CREATE_CQ:
		return "CREATE_CQ";

	case MLX5_CMD_OP_DESTROY_CQ:
		return "DESTROY_CQ";

	case MLX5_CMD_OP_QUERY_CQ:
		return "QUERY_CQ";

	case MLX5_CMD_OP_MODIFY_CQ:
		return "MODIFY_CQ";

	case MLX5_CMD_OP_CREATE_QP:
		return "CREATE_QP";

	case MLX5_CMD_OP_DESTROY_QP:
		return "DESTROY_QP";

	case MLX5_CMD_OP_RST2INIT_QP:
		return "RST2INIT_QP";

	case MLX5_CMD_OP_INIT2RTR_QP:
		return "INIT2RTR_QP";

	case MLX5_CMD_OP_RTR2RTS_QP:
		return "RTR2RTS_QP";

	case MLX5_CMD_OP_RTS2RTS_QP:
		return "RTS2RTS_QP";

	case MLX5_CMD_OP_SQERR2RTS_QP:
		return "SQERR2RTS_QP";

	case MLX5_CMD_OP_2ERR_QP:
		return "2ERR_QP";

	case MLX5_CMD_OP_RTS2SQD_QP:
		return "RTS2SQD_QP";

	case MLX5_CMD_OP_SQD2RTS_QP:
		return "SQD2RTS_QP";

	case MLX5_CMD_OP_2RST_QP:
		return "2RST_QP";

	case MLX5_CMD_OP_QUERY_QP:
		return "QUERY_QP";

	case MLX5_CMD_OP_CONF_SQP:
		return "CONF_SQP";

	case MLX5_CMD_OP_MAD_IFC:
		return "MAD_IFC";

	case MLX5_CMD_OP_INIT2INIT_QP:
		return "INIT2INIT_QP";

	case MLX5_CMD_OP_SUSPEND_QP:
		return "SUSPEND_QP";

	case MLX5_CMD_OP_UNSUSPEND_QP:
		return "UNSUSPEND_QP";

	case MLX5_CMD_OP_SQD2SQD_QP:
		return "SQD2SQD_QP";

	case MLX5_CMD_OP_ALLOC_QP_COUNTER_SET:
		return "ALLOC_QP_COUNTER_SET";

	case MLX5_CMD_OP_DEALLOC_QP_COUNTER_SET:
		return "DEALLOC_QP_COUNTER_SET";

	case MLX5_CMD_OP_QUERY_QP_COUNTER_SET:
		return "QUERY_QP_COUNTER_SET";

	case MLX5_CMD_OP_CREATE_PSV:
		return "CREATE_PSV";

	case MLX5_CMD_OP_DESTROY_PSV:
		return "DESTROY_PSV";

	case MLX5_CMD_OP_QUERY_PSV:
		return "QUERY_PSV";

	case MLX5_CMD_OP_QUERY_SIG_RULE_TABLE:
		return "QUERY_SIG_RULE_TABLE";

	case MLX5_CMD_OP_QUERY_BLOCK_SIZE_TABLE:
		return "QUERY_BLOCK_SIZE_TABLE";

	case MLX5_CMD_OP_CREATE_SRQ:
		return "CREATE_SRQ";

	case MLX5_CMD_OP_DESTROY_SRQ:
		return "DESTROY_SRQ";

	case MLX5_CMD_OP_QUERY_SRQ:
		return "QUERY_SRQ";

	case MLX5_CMD_OP_ARM_RQ:
		return "ARM_RQ";

	case MLX5_CMD_OP_RESIZE_SRQ:
		return "RESIZE_SRQ";

	case MLX5_CMD_OP_ALLOC_PD:
		return "ALLOC_PD";

	case MLX5_CMD_OP_DEALLOC_PD:
		return "DEALLOC_PD";

	case MLX5_CMD_OP_ALLOC_UAR:
		return "ALLOC_UAR";

	case MLX5_CMD_OP_DEALLOC_UAR:
		return "DEALLOC_UAR";

	case MLX5_CMD_OP_ATTACH_TO_MCG:
		return "ATTACH_TO_MCG";

	case MLX5_CMD_OP_DETACH_FROM_MCG:
		return "DETACH_FROM_MCG";

	case MLX5_CMD_OP_ALLOC_XRCD:
		return "ALLOC_XRCD";

	case MLX5_CMD_OP_DEALLOC_XRCD:
		return "DEALLOC_XRCD";

	case MLX5_CMD_OP_ACCESS_REG:
		return "MLX5_CMD_OP_ACCESS_REG";

	default: return "unknown command opcode";
	}
}

static void dump_command(struct mlx5_core_dev *dev,
			 struct mlx5_cmd_work_ent *ent, int input)
{
	u16 op = be16_to_cpu(((struct mlx5_inbox_hdr *)(ent->lay->in))->opcode);
	struct mlx5_cmd_msg *msg = input ? ent->in : ent->out;
	struct mlx5_cmd_mailbox *next = msg->next;
	int data_only;
	int offset = 0;
	int dump_len;

	data_only = !!(mlx5_core_debug_mask & (1 << MLX5_CMD_DATA));

	if (data_only)
		mlx5_core_dbg_mask(dev, 1 << MLX5_CMD_DATA,
				   "dump command data %s(0x%x) %s\n",
				   mlx5_command_str(op), op,
				   input ? "INPUT" : "OUTPUT");
	else
		mlx5_core_dbg(dev, "dump command %s(0x%x) %s\n",
			      mlx5_command_str(op), op,
			      input ? "INPUT" : "OUTPUT");

	if (data_only) {
		if (input) {
			dump_buf(ent->lay->in, sizeof(ent->lay->in), 1, offset);
			offset += sizeof(ent->lay->in);
		} else {
			dump_buf(ent->lay->out, sizeof(ent->lay->out), 1, offset);
			offset += sizeof(ent->lay->out);
		}
	} else {
		dump_buf(ent->lay, sizeof(*ent->lay), 0, offset);
		offset += sizeof(*ent->lay);
	}

	while (next && offset < msg->len) {
		if (data_only) {
			dump_len = min_t(int, MLX5_CMD_DATA_BLOCK_SIZE, msg->len - offset);
			dump_buf(next->buf, dump_len, 1, offset);
			offset += MLX5_CMD_DATA_BLOCK_SIZE;
		} else {
			mlx5_core_dbg(dev, "command block:\n");
			dump_buf(next->buf, sizeof(struct mlx5_cmd_prot_block), 0, offset);
			offset += sizeof(struct mlx5_cmd_prot_block);
		}
		next = next->next;
	}

	if (data_only)
		pr_debug("\n");
}

static void cmd_work_handler(struct work_struct *work)
{
	struct mlx5_cmd_work_ent *ent = container_of(work, struct mlx5_cmd_work_ent, work);
	struct mlx5_cmd *cmd = ent->cmd;
	struct mlx5_core_dev *dev = container_of(cmd, struct mlx5_core_dev, cmd);
	struct mlx5_cmd_layout *lay;
	struct semaphore *sem;

	sem = ent->page_queue ? &cmd->pages_sem : &cmd->sem;
	down(sem);
	if (!ent->page_queue) {
		ent->idx = alloc_ent(cmd);
		if (ent->idx < 0) {
			mlx5_core_err(dev, "failed to allocate command entry\n");
			up(sem);
			return;
		}
	} else {
		ent->idx = cmd->max_reg_cmds;
	}

	ent->token = alloc_token(cmd);
	cmd->ent_arr[ent->idx] = ent;
	lay = get_inst(cmd, ent->idx);
	ent->lay = lay;
	memset(lay, 0, sizeof(*lay));
	memcpy(lay->in, ent->in->first.data, sizeof(lay->in));
	ent->op = be32_to_cpu(lay->in[0]) >> 16;
	if (ent->in->next)
		lay->in_ptr = cpu_to_be64(ent->in->next->dma);
	lay->inlen = cpu_to_be32(ent->in->len);
	if (ent->out->next)
		lay->out_ptr = cpu_to_be64(ent->out->next->dma);
	lay->outlen = cpu_to_be32(ent->out->len);
	lay->type = MLX5_PCI_CMD_XPORT;
	lay->token = ent->token;
	lay->status_own = CMD_OWNER_HW;
	set_signature(ent, !cmd->checksum_disabled);
	dump_command(dev, ent, 1);
	ent->ts1 = ktime_get_ns();

	/* ring doorbell after the descriptor is valid */
	wmb();
	iowrite32be(1 << ent->idx, &dev->iseg->cmd_dbell);
	mlx5_core_dbg(dev, "write 0x%x to command doorbell\n", 1 << ent->idx);
	mmiowb();
	if (cmd->mode == CMD_MODE_POLLING) {
		poll_timeout(ent);
		/* make sure we read the descriptor after ownership is SW */
		rmb();
		mlx5_cmd_comp_handler(dev, 1UL << ent->idx);
	}
}

static const char *deliv_status_to_str(u8 status)
{
	switch (status) {
	case MLX5_CMD_DELIVERY_STAT_OK:
		return "no errors";
	case MLX5_CMD_DELIVERY_STAT_SIGNAT_ERR:
		return "signature error";
	case MLX5_CMD_DELIVERY_STAT_TOK_ERR:
		return "token error";
	case MLX5_CMD_DELIVERY_STAT_BAD_BLK_NUM_ERR:
		return "bad block number";
	case MLX5_CMD_DELIVERY_STAT_OUT_PTR_ALIGN_ERR:
		return "output pointer not aligned to block size";
	case MLX5_CMD_DELIVERY_STAT_IN_PTR_ALIGN_ERR:
		return "input pointer not aligned to block size";
	case MLX5_CMD_DELIVERY_STAT_FW_ERR:
		return "firmware internal error";
	case MLX5_CMD_DELIVERY_STAT_IN_LENGTH_ERR:
		return "command input length error";
	case MLX5_CMD_DELIVERY_STAT_OUT_LENGTH_ERR:
		return "command ouput length error";
	case MLX5_CMD_DELIVERY_STAT_RES_FLD_NOT_CLR_ERR:
		return "reserved fields not cleared";
	case MLX5_CMD_DELIVERY_STAT_CMD_DESCR_ERR:
		return "bad command descriptor type";
	default:
		return "unknown status code";
	}
}

static u16 msg_to_opcode(struct mlx5_cmd_msg *in)
{
	struct mlx5_inbox_hdr *hdr = (struct mlx5_inbox_hdr *)(in->first.data);

	return be16_to_cpu(hdr->opcode);
}

static int wait_func(struct mlx5_core_dev *dev, struct mlx5_cmd_work_ent *ent)
{
	unsigned long timeout = msecs_to_jiffies(MLX5_CMD_TIMEOUT_MSEC);
	struct mlx5_cmd *cmd = &dev->cmd;
	int err;

	if (cmd->mode == CMD_MODE_POLLING) {
		wait_for_completion(&ent->done);
		err = ent->ret;
	} else {
		if (!wait_for_completion_timeout(&ent->done, timeout))
			err = -ETIMEDOUT;
		else
			err = 0;
	}
	if (err == -ETIMEDOUT) {
		mlx5_core_warn(dev, "%s(0x%x) timeout. Will cause a leak of a command resource\n",
			       mlx5_command_str(msg_to_opcode(ent->in)),
			       msg_to_opcode(ent->in));
	}
	mlx5_core_dbg(dev, "err %d, delivery status %s(%d)\n",
		      err, deliv_status_to_str(ent->status), ent->status);

	return err;
}

/*  Notes:
 *    1. Callback functions may not sleep
 *    2. page queue commands do not support asynchrous completion
 */
static int mlx5_cmd_invoke(struct mlx5_core_dev *dev, struct mlx5_cmd_msg *in,
			   struct mlx5_cmd_msg *out, void *uout, int uout_size,
			   mlx5_cmd_cbk_t callback,
			   void *context, int page_queue, u8 *status)
{
	struct mlx5_cmd *cmd = &dev->cmd;
	struct mlx5_cmd_work_ent *ent;
	struct mlx5_cmd_stats *stats;
	int err = 0;
	s64 ds;
	u16 op;

	if (callback && page_queue)
		return -EINVAL;

	ent = alloc_cmd(cmd, in, out, uout, uout_size, callback, context,
			page_queue);
	if (IS_ERR(ent))
		return PTR_ERR(ent);

	if (!callback)
		init_completion(&ent->done);

	INIT_WORK(&ent->work, cmd_work_handler);
	if (page_queue) {
		cmd_work_handler(&ent->work);
	} else if (!queue_work(cmd->wq, &ent->work)) {
		mlx5_core_warn(dev, "failed to queue work\n");
		err = -ENOMEM;
		goto out_free;
	}

	if (!callback) {
		err = wait_func(dev, ent);
		if (err == -ETIMEDOUT)
			goto out;

		ds = ent->ts2 - ent->ts1;
		op = be16_to_cpu(((struct mlx5_inbox_hdr *)in->first.data)->opcode);
		if (op < ARRAY_SIZE(cmd->stats)) {
			stats = &cmd->stats[op];
			spin_lock_irq(&stats->lock);
			stats->sum += ds;
			++stats->n;
			spin_unlock_irq(&stats->lock);
		}
		mlx5_core_dbg_mask(dev, 1 << MLX5_CMD_TIME,
				   "fw exec time for %s is %lld nsec\n",
				   mlx5_command_str(op), ds);
		*status = ent->status;
		free_cmd(ent);
	}

	return err;

out_free:
	free_cmd(ent);
out:
	return err;
}

static ssize_t dbg_write(struct file *filp, const char __user *buf,
			 size_t count, loff_t *pos)
{
	struct mlx5_core_dev *dev = filp->private_data;
	struct mlx5_cmd_debug *dbg = &dev->cmd.dbg;
	char lbuf[3];
	int err;

	if (!dbg->in_msg || !dbg->out_msg)
		return -ENOMEM;

	if (copy_from_user(lbuf, buf, sizeof(lbuf)))
		return -EFAULT;

	lbuf[sizeof(lbuf) - 1] = 0;

	if (strcmp(lbuf, "go"))
		return -EINVAL;

	err = mlx5_cmd_exec(dev, dbg->in_msg, dbg->inlen, dbg->out_msg, dbg->outlen);

	return err ? err : count;
}


static const struct file_operations fops = {
	.owner	= THIS_MODULE,
	.open	= simple_open,
	.write	= dbg_write,
};

static int mlx5_copy_to_msg(struct mlx5_cmd_msg *to, void *from, int size)
{
	struct mlx5_cmd_prot_block *block;
	struct mlx5_cmd_mailbox *next;
	int copy;

	if (!to || !from)
		return -ENOMEM;

	copy = min_t(int, size, sizeof(to->first.data));
	memcpy(to->first.data, from, copy);
	size -= copy;
	from += copy;

	next = to->next;
	while (size) {
		if (!next) {
			/* this is a BUG */
			return -ENOMEM;
		}

		copy = min_t(int, size, MLX5_CMD_DATA_BLOCK_SIZE);
		block = next->buf;
		memcpy(block->data, from, copy);
		from += copy;
		size -= copy;
		next = next->next;
	}

	return 0;
}

static int mlx5_copy_from_msg(void *to, struct mlx5_cmd_msg *from, int size)
{
	struct mlx5_cmd_prot_block *block;
	struct mlx5_cmd_mailbox *next;
	int copy;

	if (!to || !from)
		return -ENOMEM;

	copy = min_t(int, size, sizeof(from->first.data));
	memcpy(to, from->first.data, copy);
	size -= copy;
	to += copy;

	next = from->next;
	while (size) {
		if (!next) {
			/* this is a BUG */
			return -ENOMEM;
		}

		copy = min_t(int, size, MLX5_CMD_DATA_BLOCK_SIZE);
		block = next->buf;

		memcpy(to, block->data, copy);
		to += copy;
		size -= copy;
		next = next->next;
	}

	return 0;
}

static struct mlx5_cmd_mailbox *alloc_cmd_box(struct mlx5_core_dev *dev,
					      gfp_t flags)
{
	struct mlx5_cmd_mailbox *mailbox;

	mailbox = kmalloc(sizeof(*mailbox), flags);
	if (!mailbox)
		return ERR_PTR(-ENOMEM);

	mailbox->buf = pci_pool_alloc(dev->cmd.pool, flags,
				      &mailbox->dma);
	if (!mailbox->buf) {
		mlx5_core_dbg(dev, "failed allocation\n");
		kfree(mailbox);
		return ERR_PTR(-ENOMEM);
	}
	memset(mailbox->buf, 0, sizeof(struct mlx5_cmd_prot_block));
	mailbox->next = NULL;

	return mailbox;
}

static void free_cmd_box(struct mlx5_core_dev *dev,
			 struct mlx5_cmd_mailbox *mailbox)
{
	pci_pool_free(dev->cmd.pool, mailbox->buf, mailbox->dma);
	kfree(mailbox);
}

static struct mlx5_cmd_msg *mlx5_alloc_cmd_msg(struct mlx5_core_dev *dev,
					       gfp_t flags, int size)
{
	struct mlx5_cmd_mailbox *tmp, *head = NULL;
	struct mlx5_cmd_prot_block *block;
	struct mlx5_cmd_msg *msg;
	int blen;
	int err;
	int n;
	int i;

	msg = kzalloc(sizeof(*msg), flags);
	if (!msg)
		return ERR_PTR(-ENOMEM);

	blen = size - min_t(int, sizeof(msg->first.data), size);
	n = (blen + MLX5_CMD_DATA_BLOCK_SIZE - 1) / MLX5_CMD_DATA_BLOCK_SIZE;

	for (i = 0; i < n; i++) {
		tmp = alloc_cmd_box(dev, flags);
		if (IS_ERR(tmp)) {
			mlx5_core_warn(dev, "failed allocating block\n");
			err = PTR_ERR(tmp);
			goto err_alloc;
		}

		block = tmp->buf;
		tmp->next = head;
		block->next = cpu_to_be64(tmp->next ? tmp->next->dma : 0);
		block->block_num = cpu_to_be32(n - i - 1);
		head = tmp;
	}
	msg->next = head;
	msg->len = size;
	return msg;

err_alloc:
	while (head) {
		tmp = head->next;
		free_cmd_box(dev, head);
		head = tmp;
	}
	kfree(msg);

	return ERR_PTR(err);
}

static void mlx5_free_cmd_msg(struct mlx5_core_dev *dev,
				  struct mlx5_cmd_msg *msg)
{
	struct mlx5_cmd_mailbox *head = msg->next;
	struct mlx5_cmd_mailbox *next;

	while (head) {
		next = head->next;
		free_cmd_box(dev, head);
		head = next;
	}
	kfree(msg);
}

static ssize_t data_write(struct file *filp, const char __user *buf,
			  size_t count, loff_t *pos)
{
	struct mlx5_core_dev *dev = filp->private_data;
	struct mlx5_cmd_debug *dbg = &dev->cmd.dbg;
	void *ptr;
	int err;

	if (*pos != 0)
		return -EINVAL;

	kfree(dbg->in_msg);
	dbg->in_msg = NULL;
	dbg->inlen = 0;

	ptr = kzalloc(count, GFP_KERNEL);
	if (!ptr)
		return -ENOMEM;

	if (copy_from_user(ptr, buf, count)) {
		err = -EFAULT;
		goto out;
	}
	dbg->in_msg = ptr;
	dbg->inlen = count;

	*pos = count;

	return count;

out:
	kfree(ptr);
	return err;
}

static ssize_t data_read(struct file *filp, char __user *buf, size_t count,
			 loff_t *pos)
{
	struct mlx5_core_dev *dev = filp->private_data;
	struct mlx5_cmd_debug *dbg = &dev->cmd.dbg;
	int copy;

	if (*pos)
		return 0;

	if (!dbg->out_msg)
		return -ENOMEM;

	copy = min_t(int, count, dbg->outlen);
	if (copy_to_user(buf, dbg->out_msg, copy))
		return -EFAULT;

	*pos += copy;

	return copy;
}

static const struct file_operations dfops = {
	.owner	= THIS_MODULE,
	.open	= simple_open,
	.write	= data_write,
	.read	= data_read,
};

static ssize_t outlen_read(struct file *filp, char __user *buf, size_t count,
			   loff_t *pos)
{
	struct mlx5_core_dev *dev = filp->private_data;
	struct mlx5_cmd_debug *dbg = &dev->cmd.dbg;
	char outlen[8];
	int err;

	if (*pos)
		return 0;

	err = snprintf(outlen, sizeof(outlen), "%d", dbg->outlen);
	if (err < 0)
		return err;

	if (copy_to_user(buf, &outlen, err))
		return -EFAULT;

	*pos += err;

	return err;
}

static ssize_t outlen_write(struct file *filp, const char __user *buf,
			    size_t count, loff_t *pos)
{
	struct mlx5_core_dev *dev = filp->private_data;
	struct mlx5_cmd_debug *dbg = &dev->cmd.dbg;
	char outlen_str[8];
	int outlen;
	void *ptr;
	int err;

	if (*pos != 0 || count > 6)
		return -EINVAL;

	kfree(dbg->out_msg);
	dbg->out_msg = NULL;
	dbg->outlen = 0;

	if (copy_from_user(outlen_str, buf, count))
		return -EFAULT;

	outlen_str[7] = 0;

	err = sscanf(outlen_str, "%d", &outlen);
	if (err < 0)
		return err;

	ptr = kzalloc(outlen, GFP_KERNEL);
	if (!ptr)
		return -ENOMEM;

	dbg->out_msg = ptr;
	dbg->outlen = outlen;

	*pos = count;

	return count;
}

static const struct file_operations olfops = {
	.owner	= THIS_MODULE,
	.open	= simple_open,
	.write	= outlen_write,
	.read	= outlen_read,
};

static void set_wqname(struct mlx5_core_dev *dev)
{
	struct mlx5_cmd *cmd = &dev->cmd;

	snprintf(cmd->wq_name, sizeof(cmd->wq_name), "mlx5_cmd_%s",
		 dev_name(&dev->pdev->dev));
}

static void clean_debug_files(struct mlx5_core_dev *dev)
{
	struct mlx5_cmd_debug *dbg = &dev->cmd.dbg;

	if (!mlx5_debugfs_root)
		return;

	mlx5_cmdif_debugfs_cleanup(dev);
	debugfs_remove_recursive(dbg->dbg_root);
}

static int create_debugfs_files(struct mlx5_core_dev *dev)
{
	struct mlx5_cmd_debug *dbg = &dev->cmd.dbg;
	int err = -ENOMEM;

	if (!mlx5_debugfs_root)
		return 0;

	dbg->dbg_root = debugfs_create_dir("cmd", dev->priv.dbg_root);
	if (!dbg->dbg_root)
		return err;

	dbg->dbg_in = debugfs_create_file("in", 0400, dbg->dbg_root,
					  dev, &dfops);
	if (!dbg->dbg_in)
		goto err_dbg;

	dbg->dbg_out = debugfs_create_file("out", 0200, dbg->dbg_root,
					   dev, &dfops);
	if (!dbg->dbg_out)
		goto err_dbg;

	dbg->dbg_outlen = debugfs_create_file("out_len", 0600, dbg->dbg_root,
					      dev, &olfops);
	if (!dbg->dbg_outlen)
		goto err_dbg;

	dbg->dbg_status = debugfs_create_u8("status", 0600, dbg->dbg_root,
					    &dbg->status);
	if (!dbg->dbg_status)
		goto err_dbg;

	dbg->dbg_run = debugfs_create_file("run", 0200, dbg->dbg_root, dev, &fops);
	if (!dbg->dbg_run)
		goto err_dbg;

	mlx5_cmdif_debugfs_init(dev);

	return 0;

err_dbg:
	clean_debug_files(dev);
	return err;
}

void mlx5_cmd_use_events(struct mlx5_core_dev *dev)
{
	struct mlx5_cmd *cmd = &dev->cmd;
	int i;

	for (i = 0; i < cmd->max_reg_cmds; i++)
		down(&cmd->sem);

	down(&cmd->pages_sem);

	flush_workqueue(cmd->wq);

	cmd->mode = CMD_MODE_EVENTS;

	up(&cmd->pages_sem);
	for (i = 0; i < cmd->max_reg_cmds; i++)
		up(&cmd->sem);
}

void mlx5_cmd_use_polling(struct mlx5_core_dev *dev)
{
	struct mlx5_cmd *cmd = &dev->cmd;
	int i;

	for (i = 0; i < cmd->max_reg_cmds; i++)
		down(&cmd->sem);

	down(&cmd->pages_sem);

	flush_workqueue(cmd->wq);
	cmd->mode = CMD_MODE_POLLING;

	up(&cmd->pages_sem);
	for (i = 0; i < cmd->max_reg_cmds; i++)
		up(&cmd->sem);
}

static void free_msg(struct mlx5_core_dev *dev, struct mlx5_cmd_msg *msg)
{
	unsigned long flags;

	if (msg->cache) {
		spin_lock_irqsave(&msg->cache->lock, flags);
		list_add_tail(&msg->list, &msg->cache->head);
		spin_unlock_irqrestore(&msg->cache->lock, flags);
	} else {
		mlx5_free_cmd_msg(dev, msg);
	}
}

void mlx5_cmd_comp_handler(struct mlx5_core_dev *dev, unsigned long vector)
{
	struct mlx5_cmd *cmd = &dev->cmd;
	struct mlx5_cmd_work_ent *ent;
	mlx5_cmd_cbk_t callback;
	void *context;
	int err;
	int i;
	s64 ds;
	struct mlx5_cmd_stats *stats;
	unsigned long flags;

	for (i = 0; i < (1 << cmd->log_sz); i++) {
		if (test_bit(i, &vector)) {
			struct semaphore *sem;

			ent = cmd->ent_arr[i];
			if (ent->page_queue)
				sem = &cmd->pages_sem;
			else
				sem = &cmd->sem;
			ent->ts2 = ktime_get_ns();
			memcpy(ent->out->first.data, ent->lay->out, sizeof(ent->lay->out));
			dump_command(dev, ent, 0);
			if (!ent->ret) {
				if (!cmd->checksum_disabled)
					ent->ret = verify_signature(ent);
				else
					ent->ret = 0;
				ent->status = ent->lay->status_own >> 1;
				mlx5_core_dbg(dev, "command completed. ret 0x%x, delivery status %s(0x%x)\n",
					      ent->ret, deliv_status_to_str(ent->status), ent->status);
			}
			free_ent(cmd, ent->idx);
			if (ent->callback) {
				ds = ent->ts2 - ent->ts1;
				if (ent->op < ARRAY_SIZE(cmd->stats)) {
					stats = &cmd->stats[ent->op];
					spin_lock_irqsave(&stats->lock, flags);
					stats->sum += ds;
					++stats->n;
					spin_unlock_irqrestore(&stats->lock, flags);
				}

				callback = ent->callback;
				context = ent->context;
				err = ent->ret;
				if (!err)
					err = mlx5_copy_from_msg(ent->uout,
								 ent->out,
								 ent->uout_size);

				mlx5_free_cmd_msg(dev, ent->out);
				free_msg(dev, ent->in);

				free_cmd(ent);
				callback(err, context);
			} else {
				complete(&ent->done);
			}
			up(sem);
		}
	}
}
EXPORT_SYMBOL(mlx5_cmd_comp_handler);

static int status_to_err(u8 status)
{
	return status ? -1 : 0; /* TBD more meaningful codes */
}

static struct mlx5_cmd_msg *alloc_msg(struct mlx5_core_dev *dev, int in_size,
				      gfp_t gfp)
{
	struct mlx5_cmd_msg *msg = ERR_PTR(-ENOMEM);
	struct mlx5_cmd *cmd = &dev->cmd;
	struct cache_ent *ent = NULL;

	if (in_size > MED_LIST_SIZE && in_size <= LONG_LIST_SIZE)
		ent = &cmd->cache.large;
	else if (in_size > 16 && in_size <= MED_LIST_SIZE)
		ent = &cmd->cache.med;

	if (ent) {
		spin_lock_irq(&ent->lock);
		if (!list_empty(&ent->head)) {
			msg = list_entry(ent->head.next, typeof(*msg), list);
			/* For cached lists, we must explicitly state what is
			 * the real size
			 */
			msg->len = in_size;
			list_del(&msg->list);
		}
		spin_unlock_irq(&ent->lock);
	}

	if (IS_ERR(msg))
		msg = mlx5_alloc_cmd_msg(dev, gfp, in_size);

	return msg;
}

static int is_manage_pages(struct mlx5_inbox_hdr *in)
{
	return be16_to_cpu(in->opcode) == MLX5_CMD_OP_MANAGE_PAGES;
}

static int cmd_exec(struct mlx5_core_dev *dev, void *in, int in_size, void *out,
		    int out_size, mlx5_cmd_cbk_t callback, void *context)
{
	struct mlx5_cmd_msg *inb;
	struct mlx5_cmd_msg *outb;
	int pages_queue;
	gfp_t gfp;
	int err;
	u8 status = 0;

	pages_queue = is_manage_pages(in);
	gfp = callback ? GFP_ATOMIC : GFP_KERNEL;

	inb = alloc_msg(dev, in_size, gfp);
	if (IS_ERR(inb)) {
		err = PTR_ERR(inb);
		return err;
	}

	err = mlx5_copy_to_msg(inb, in, in_size);
	if (err) {
		mlx5_core_warn(dev, "err %d\n", err);
		goto out_in;
	}

	outb = mlx5_alloc_cmd_msg(dev, gfp, out_size);
	if (IS_ERR(outb)) {
		err = PTR_ERR(outb);
		goto out_in;
	}

	err = mlx5_cmd_invoke(dev, inb, outb, out, out_size, callback, context,
			      pages_queue, &status);
	if (err)
		goto out_out;

	mlx5_core_dbg(dev, "err %d, status %d\n", err, status);
	if (status) {
		err = status_to_err(status);
		goto out_out;
	}

	err = mlx5_copy_from_msg(out, outb, out_size);

out_out:
	if (!callback)
		mlx5_free_cmd_msg(dev, outb);

out_in:
	if (!callback)
		free_msg(dev, inb);
	return err;
}

int mlx5_cmd_exec(struct mlx5_core_dev *dev, void *in, int in_size, void *out,
		  int out_size)
{
	return cmd_exec(dev, in, in_size, out, out_size, NULL, NULL);
}
EXPORT_SYMBOL(mlx5_cmd_exec);

int mlx5_cmd_exec_cb(struct mlx5_core_dev *dev, void *in, int in_size,
		     void *out, int out_size, mlx5_cmd_cbk_t callback,
		     void *context)
{
	return cmd_exec(dev, in, in_size, out, out_size, callback, context);
}
EXPORT_SYMBOL(mlx5_cmd_exec_cb);

static void destroy_msg_cache(struct mlx5_core_dev *dev)
{
	struct mlx5_cmd *cmd = &dev->cmd;
	struct mlx5_cmd_msg *msg;
	struct mlx5_cmd_msg *n;

	list_for_each_entry_safe(msg, n, &cmd->cache.large.head, list) {
		list_del(&msg->list);
		mlx5_free_cmd_msg(dev, msg);
	}

	list_for_each_entry_safe(msg, n, &cmd->cache.med.head, list) {
		list_del(&msg->list);
		mlx5_free_cmd_msg(dev, msg);
	}
}

static int create_msg_cache(struct mlx5_core_dev *dev)
{
	struct mlx5_cmd *cmd = &dev->cmd;
	struct mlx5_cmd_msg *msg;
	int err;
	int i;

	spin_lock_init(&cmd->cache.large.lock);
	INIT_LIST_HEAD(&cmd->cache.large.head);
	spin_lock_init(&cmd->cache.med.lock);
	INIT_LIST_HEAD(&cmd->cache.med.head);

	for (i = 0; i < NUM_LONG_LISTS; i++) {
		msg = mlx5_alloc_cmd_msg(dev, GFP_KERNEL, LONG_LIST_SIZE);
		if (IS_ERR(msg)) {
			err = PTR_ERR(msg);
			goto ex_err;
		}
		msg->cache = &cmd->cache.large;
		list_add_tail(&msg->list, &cmd->cache.large.head);
	}

	for (i = 0; i < NUM_MED_LISTS; i++) {
		msg = mlx5_alloc_cmd_msg(dev, GFP_KERNEL, MED_LIST_SIZE);
		if (IS_ERR(msg)) {
			err = PTR_ERR(msg);
			goto ex_err;
		}
		msg->cache = &cmd->cache.med;
		list_add_tail(&msg->list, &cmd->cache.med.head);
	}

	return 0;

ex_err:
	destroy_msg_cache(dev);
	return err;
}

int mlx5_cmd_init(struct mlx5_core_dev *dev)
{
	int size = sizeof(struct mlx5_cmd_prot_block);
	int align = roundup_pow_of_two(size);
	struct mlx5_cmd *cmd = &dev->cmd;
	u32 cmd_h, cmd_l;
	u16 cmd_if_rev;
	int err;
	int i;

	cmd_if_rev = cmdif_rev(dev);
	if (cmd_if_rev != CMD_IF_REV) {
		dev_err(&dev->pdev->dev,
			"Driver cmdif rev(%d) differs from firmware's(%d)\n",
			CMD_IF_REV, cmd_if_rev);
		return -EINVAL;
	}

	cmd->pool = pci_pool_create("mlx5_cmd", dev->pdev, size, align, 0);
	if (!cmd->pool)
		return -ENOMEM;

	cmd->cmd_buf = (void *)__get_free_pages(GFP_ATOMIC, 0);
	if (!cmd->cmd_buf) {
		err = -ENOMEM;
		goto err_free_pool;
	}
	cmd->dma = dma_map_single(&dev->pdev->dev, cmd->cmd_buf, PAGE_SIZE,
				  DMA_BIDIRECTIONAL);
	if (dma_mapping_error(&dev->pdev->dev, cmd->dma)) {
		err = -ENOMEM;
		goto err_free;
	}

	cmd_l = ioread32be(&dev->iseg->cmdq_addr_l_sz) & 0xff;
	cmd->log_sz = cmd_l >> 4 & 0xf;
	cmd->log_stride = cmd_l & 0xf;
	if (1 << cmd->log_sz > MLX5_MAX_COMMANDS) {
		dev_err(&dev->pdev->dev, "firmware reports too many outstanding commands %d\n",
			1 << cmd->log_sz);
		err = -EINVAL;
		goto err_map;
	}

	if (cmd->log_sz + cmd->log_stride > PAGE_SHIFT) {
		dev_err(&dev->pdev->dev, "command queue size overflow\n");
		err = -EINVAL;
		goto err_map;
	}

	cmd->checksum_disabled = 1;
	cmd->max_reg_cmds = (1 << cmd->log_sz) - 1;
	cmd->bitmask = (1 << cmd->max_reg_cmds) - 1;

	cmd->cmdif_rev = ioread32be(&dev->iseg->cmdif_rev_fw_sub) >> 16;
	if (cmd->cmdif_rev > CMD_IF_REV) {
		dev_err(&dev->pdev->dev, "driver does not support command interface version. driver %d, firmware %d\n",
			CMD_IF_REV, cmd->cmdif_rev);
		err = -ENOTSUPP;
		goto err_map;
	}

	spin_lock_init(&cmd->alloc_lock);
	spin_lock_init(&cmd->token_lock);
	for (i = 0; i < ARRAY_SIZE(cmd->stats); i++)
		spin_lock_init(&cmd->stats[i].lock);

	sema_init(&cmd->sem, cmd->max_reg_cmds);
	sema_init(&cmd->pages_sem, 1);

	cmd_h = (u32)((u64)(cmd->dma) >> 32);
	cmd_l = (u32)(cmd->dma);
	if (cmd_l & 0xfff) {
		dev_err(&dev->pdev->dev, "invalid command queue address\n");
		err = -ENOMEM;
		goto err_map;
	}

	iowrite32be(cmd_h, &dev->iseg->cmdq_addr_h);
	iowrite32be(cmd_l, &dev->iseg->cmdq_addr_l_sz);

	/* Make sure firmware sees the complete address before we proceed */
	wmb();

	mlx5_core_dbg(dev, "descriptor at dma 0x%llx\n", (unsigned long long)(cmd->dma));

	cmd->mode = CMD_MODE_POLLING;

	err = create_msg_cache(dev);
	if (err) {
		dev_err(&dev->pdev->dev, "failed to create command cache\n");
		goto err_map;
	}

	set_wqname(dev);
	cmd->wq = create_singlethread_workqueue(cmd->wq_name);
	if (!cmd->wq) {
		dev_err(&dev->pdev->dev, "failed to create command workqueue\n");
		err = -ENOMEM;
		goto err_cache;
	}

	err = create_debugfs_files(dev);
	if (err) {
		err = -ENOMEM;
		goto err_wq;
	}

	return 0;

err_wq:
	destroy_workqueue(cmd->wq);

err_cache:
	destroy_msg_cache(dev);

err_map:
	dma_unmap_single(&dev->pdev->dev, cmd->dma, PAGE_SIZE,
			 DMA_BIDIRECTIONAL);
err_free:
	free_pages((unsigned long)cmd->cmd_buf, 0);

err_free_pool:
	pci_pool_destroy(cmd->pool);

	return err;
}
EXPORT_SYMBOL(mlx5_cmd_init);

void mlx5_cmd_cleanup(struct mlx5_core_dev *dev)
{
	struct mlx5_cmd *cmd = &dev->cmd;

	clean_debug_files(dev);
	destroy_workqueue(cmd->wq);
	destroy_msg_cache(dev);
	dma_unmap_single(&dev->pdev->dev, cmd->dma, PAGE_SIZE,
			 DMA_BIDIRECTIONAL);
	free_pages((unsigned long)cmd->cmd_buf, 0);
	pci_pool_destroy(cmd->pool);
}
EXPORT_SYMBOL(mlx5_cmd_cleanup);

static const char *cmd_status_str(u8 status)
{
	switch (status) {
	case MLX5_CMD_STAT_OK:
		return "OK";
	case MLX5_CMD_STAT_INT_ERR:
		return "internal error";
	case MLX5_CMD_STAT_BAD_OP_ERR:
		return "bad operation";
	case MLX5_CMD_STAT_BAD_PARAM_ERR:
		return "bad parameter";
	case MLX5_CMD_STAT_BAD_SYS_STATE_ERR:
		return "bad system state";
	case MLX5_CMD_STAT_BAD_RES_ERR:
		return "bad resource";
	case MLX5_CMD_STAT_RES_BUSY:
		return "resource busy";
	case MLX5_CMD_STAT_LIM_ERR:
		return "limits exceeded";
	case MLX5_CMD_STAT_BAD_RES_STATE_ERR:
		return "bad resource state";
	case MLX5_CMD_STAT_IX_ERR:
		return "bad index";
	case MLX5_CMD_STAT_NO_RES_ERR:
		return "no resources";
	case MLX5_CMD_STAT_BAD_INP_LEN_ERR:
		return "bad input length";
	case MLX5_CMD_STAT_BAD_OUTP_LEN_ERR:
		return "bad output length";
	case MLX5_CMD_STAT_BAD_QP_STATE_ERR:
		return "bad QP state";
	case MLX5_CMD_STAT_BAD_PKT_ERR:
		return "bad packet (discarded)";
	case MLX5_CMD_STAT_BAD_SIZE_OUTS_CQES_ERR:
		return "bad size too many outstanding CQEs";
	default:
		return "unknown status";
	}
}

int mlx5_cmd_status_to_err(struct mlx5_outbox_hdr *hdr)
{
	if (!hdr->status)
		return 0;

	pr_warn("command failed, status %s(0x%x), syndrome 0x%x\n",
		cmd_status_str(hdr->status), hdr->status,
		be32_to_cpu(hdr->syndrome));

	switch (hdr->status) {
	case MLX5_CMD_STAT_OK:				return 0;
	case MLX5_CMD_STAT_INT_ERR:			return -EIO;
	case MLX5_CMD_STAT_BAD_OP_ERR:			return -EINVAL;
	case MLX5_CMD_STAT_BAD_PARAM_ERR:		return -EINVAL;
	case MLX5_CMD_STAT_BAD_SYS_STATE_ERR:		return -EIO;
	case MLX5_CMD_STAT_BAD_RES_ERR:			return -EINVAL;
	case MLX5_CMD_STAT_RES_BUSY:			return -EBUSY;
	case MLX5_CMD_STAT_LIM_ERR:			return -ENOMEM;
	case MLX5_CMD_STAT_BAD_RES_STATE_ERR:		return -EINVAL;
	case MLX5_CMD_STAT_IX_ERR:			return -EINVAL;
	case MLX5_CMD_STAT_NO_RES_ERR:			return -EAGAIN;
	case MLX5_CMD_STAT_BAD_INP_LEN_ERR:		return -EIO;
	case MLX5_CMD_STAT_BAD_OUTP_LEN_ERR:		return -EIO;
	case MLX5_CMD_STAT_BAD_QP_STATE_ERR:		return -EINVAL;
	case MLX5_CMD_STAT_BAD_PKT_ERR:			return -EINVAL;
	case MLX5_CMD_STAT_BAD_SIZE_OUTS_CQES_ERR:	return -EINVAL;
	default:					return -EIO;
	}
}

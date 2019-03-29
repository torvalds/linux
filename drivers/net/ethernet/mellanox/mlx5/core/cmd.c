/*
 * Copyright (c) 2013-2016, Mellanox Technologies. All rights reserved.
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

#include <linux/highmem.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/pci.h>
#include <linux/dma-mapping.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/random.h>
#include <linux/io-mapping.h>
#include <linux/mlx5/driver.h>
#include <linux/mlx5/eq.h>
#include <linux/debugfs.h>

#include "mlx5_core.h"
#include "lib/eq.h"

enum {
	CMD_IF_REV = 5,
};

enum {
	CMD_MODE_POLLING,
	CMD_MODE_EVENTS
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
	cmd->token++;
	if (cmd->token == 0)
		cmd->token++;
	token = cmd->token;
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

static int mlx5_calc_cmd_blocks(struct mlx5_cmd_msg *msg)
{
	int size = msg->len;
	int blen = size - min_t(int, sizeof(msg->first.data), size);

	return DIV_ROUND_UP(blen, MLX5_CMD_DATA_BLOCK_SIZE);
}

static u8 xor8_buf(void *buf, size_t offset, int len)
{
	u8 *ptr = buf;
	u8 sum = 0;
	int i;
	int end = len + offset;

	for (i = offset; i < end; i++)
		sum ^= ptr[i];

	return sum;
}

static int verify_block_sig(struct mlx5_cmd_prot_block *block)
{
	size_t rsvd0_off = offsetof(struct mlx5_cmd_prot_block, rsvd0);
	int xor_len = sizeof(*block) - sizeof(block->data) - 1;

	if (xor8_buf(block, rsvd0_off, xor_len) != 0xff)
		return -EINVAL;

	if (xor8_buf(block, 0, sizeof(*block)) != 0xff)
		return -EINVAL;

	return 0;
}

static void calc_block_sig(struct mlx5_cmd_prot_block *block)
{
	int ctrl_xor_len = sizeof(*block) - sizeof(block->data) - 2;
	size_t rsvd0_off = offsetof(struct mlx5_cmd_prot_block, rsvd0);

	block->ctrl_sig = ~xor8_buf(block, rsvd0_off, ctrl_xor_len);
	block->sig = ~xor8_buf(block, 0, sizeof(*block) - 1);
}

static void calc_chain_sig(struct mlx5_cmd_msg *msg)
{
	struct mlx5_cmd_mailbox *next = msg->next;
	int n = mlx5_calc_cmd_blocks(msg);
	int i = 0;

	for (i = 0; i < n && next; i++)  {
		calc_block_sig(next->buf);
		next = next->next;
	}
}

static void set_signature(struct mlx5_cmd_work_ent *ent, int csum)
{
	ent->lay->sig = ~xor8_buf(ent->lay, 0,  sizeof(*ent->lay));
	if (csum) {
		calc_chain_sig(ent->in);
		calc_chain_sig(ent->out);
	}
}

static void poll_timeout(struct mlx5_cmd_work_ent *ent)
{
	unsigned long poll_end = jiffies + msecs_to_jiffies(MLX5_CMD_TIMEOUT_MSEC + 1000);
	u8 own;

	do {
		own = READ_ONCE(ent->lay->status_own);
		if (!(own & CMD_OWNER_HW)) {
			ent->ret = 0;
			return;
		}
		cond_resched();
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
	int n = mlx5_calc_cmd_blocks(ent->out);
	int err;
	u8 sig;
	int i = 0;

	sig = xor8_buf(ent->lay, 0, sizeof(*ent->lay));
	if (sig != 0xff)
		return -EINVAL;

	for (i = 0; i < n && next; i++) {
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

static int mlx5_internal_err_ret_value(struct mlx5_core_dev *dev, u16 op,
				       u32 *synd, u8 *status)
{
	*synd = 0;
	*status = 0;

	switch (op) {
	case MLX5_CMD_OP_TEARDOWN_HCA:
	case MLX5_CMD_OP_DISABLE_HCA:
	case MLX5_CMD_OP_MANAGE_PAGES:
	case MLX5_CMD_OP_DESTROY_MKEY:
	case MLX5_CMD_OP_DESTROY_EQ:
	case MLX5_CMD_OP_DESTROY_CQ:
	case MLX5_CMD_OP_DESTROY_QP:
	case MLX5_CMD_OP_DESTROY_PSV:
	case MLX5_CMD_OP_DESTROY_SRQ:
	case MLX5_CMD_OP_DESTROY_XRC_SRQ:
	case MLX5_CMD_OP_DESTROY_XRQ:
	case MLX5_CMD_OP_DESTROY_DCT:
	case MLX5_CMD_OP_DEALLOC_Q_COUNTER:
	case MLX5_CMD_OP_DESTROY_SCHEDULING_ELEMENT:
	case MLX5_CMD_OP_DESTROY_QOS_PARA_VPORT:
	case MLX5_CMD_OP_DEALLOC_PD:
	case MLX5_CMD_OP_DEALLOC_UAR:
	case MLX5_CMD_OP_DETACH_FROM_MCG:
	case MLX5_CMD_OP_DEALLOC_XRCD:
	case MLX5_CMD_OP_DEALLOC_TRANSPORT_DOMAIN:
	case MLX5_CMD_OP_DELETE_VXLAN_UDP_DPORT:
	case MLX5_CMD_OP_DELETE_L2_TABLE_ENTRY:
	case MLX5_CMD_OP_DESTROY_LAG:
	case MLX5_CMD_OP_DESTROY_VPORT_LAG:
	case MLX5_CMD_OP_DESTROY_TIR:
	case MLX5_CMD_OP_DESTROY_SQ:
	case MLX5_CMD_OP_DESTROY_RQ:
	case MLX5_CMD_OP_DESTROY_RMP:
	case MLX5_CMD_OP_DESTROY_TIS:
	case MLX5_CMD_OP_DESTROY_RQT:
	case MLX5_CMD_OP_DESTROY_FLOW_TABLE:
	case MLX5_CMD_OP_DESTROY_FLOW_GROUP:
	case MLX5_CMD_OP_DELETE_FLOW_TABLE_ENTRY:
	case MLX5_CMD_OP_DEALLOC_FLOW_COUNTER:
	case MLX5_CMD_OP_2ERR_QP:
	case MLX5_CMD_OP_2RST_QP:
	case MLX5_CMD_OP_MODIFY_NIC_VPORT_CONTEXT:
	case MLX5_CMD_OP_MODIFY_FLOW_TABLE:
	case MLX5_CMD_OP_SET_FLOW_TABLE_ENTRY:
	case MLX5_CMD_OP_SET_FLOW_TABLE_ROOT:
	case MLX5_CMD_OP_DEALLOC_PACKET_REFORMAT_CONTEXT:
	case MLX5_CMD_OP_DEALLOC_MODIFY_HEADER_CONTEXT:
	case MLX5_CMD_OP_FPGA_DESTROY_QP:
	case MLX5_CMD_OP_DESTROY_GENERAL_OBJECT:
	case MLX5_CMD_OP_DEALLOC_MEMIC:
	case MLX5_CMD_OP_PAGE_FAULT_RESUME:
	case MLX5_CMD_OP_QUERY_HOST_PARAMS:
		return MLX5_CMD_STAT_OK;

	case MLX5_CMD_OP_QUERY_HCA_CAP:
	case MLX5_CMD_OP_QUERY_ADAPTER:
	case MLX5_CMD_OP_INIT_HCA:
	case MLX5_CMD_OP_ENABLE_HCA:
	case MLX5_CMD_OP_QUERY_PAGES:
	case MLX5_CMD_OP_SET_HCA_CAP:
	case MLX5_CMD_OP_QUERY_ISSI:
	case MLX5_CMD_OP_SET_ISSI:
	case MLX5_CMD_OP_CREATE_MKEY:
	case MLX5_CMD_OP_QUERY_MKEY:
	case MLX5_CMD_OP_QUERY_SPECIAL_CONTEXTS:
	case MLX5_CMD_OP_CREATE_EQ:
	case MLX5_CMD_OP_QUERY_EQ:
	case MLX5_CMD_OP_GEN_EQE:
	case MLX5_CMD_OP_CREATE_CQ:
	case MLX5_CMD_OP_QUERY_CQ:
	case MLX5_CMD_OP_MODIFY_CQ:
	case MLX5_CMD_OP_CREATE_QP:
	case MLX5_CMD_OP_RST2INIT_QP:
	case MLX5_CMD_OP_INIT2RTR_QP:
	case MLX5_CMD_OP_RTR2RTS_QP:
	case MLX5_CMD_OP_RTS2RTS_QP:
	case MLX5_CMD_OP_SQERR2RTS_QP:
	case MLX5_CMD_OP_QUERY_QP:
	case MLX5_CMD_OP_SQD_RTS_QP:
	case MLX5_CMD_OP_INIT2INIT_QP:
	case MLX5_CMD_OP_CREATE_PSV:
	case MLX5_CMD_OP_CREATE_SRQ:
	case MLX5_CMD_OP_QUERY_SRQ:
	case MLX5_CMD_OP_ARM_RQ:
	case MLX5_CMD_OP_CREATE_XRC_SRQ:
	case MLX5_CMD_OP_QUERY_XRC_SRQ:
	case MLX5_CMD_OP_ARM_XRC_SRQ:
	case MLX5_CMD_OP_CREATE_XRQ:
	case MLX5_CMD_OP_QUERY_XRQ:
	case MLX5_CMD_OP_ARM_XRQ:
	case MLX5_CMD_OP_CREATE_DCT:
	case MLX5_CMD_OP_DRAIN_DCT:
	case MLX5_CMD_OP_QUERY_DCT:
	case MLX5_CMD_OP_ARM_DCT_FOR_KEY_VIOLATION:
	case MLX5_CMD_OP_QUERY_VPORT_STATE:
	case MLX5_CMD_OP_MODIFY_VPORT_STATE:
	case MLX5_CMD_OP_QUERY_ESW_VPORT_CONTEXT:
	case MLX5_CMD_OP_MODIFY_ESW_VPORT_CONTEXT:
	case MLX5_CMD_OP_QUERY_NIC_VPORT_CONTEXT:
	case MLX5_CMD_OP_QUERY_ROCE_ADDRESS:
	case MLX5_CMD_OP_SET_ROCE_ADDRESS:
	case MLX5_CMD_OP_QUERY_HCA_VPORT_CONTEXT:
	case MLX5_CMD_OP_MODIFY_HCA_VPORT_CONTEXT:
	case MLX5_CMD_OP_QUERY_HCA_VPORT_GID:
	case MLX5_CMD_OP_QUERY_HCA_VPORT_PKEY:
	case MLX5_CMD_OP_QUERY_VNIC_ENV:
	case MLX5_CMD_OP_QUERY_VPORT_COUNTER:
	case MLX5_CMD_OP_ALLOC_Q_COUNTER:
	case MLX5_CMD_OP_QUERY_Q_COUNTER:
	case MLX5_CMD_OP_SET_MONITOR_COUNTER:
	case MLX5_CMD_OP_ARM_MONITOR_COUNTER:
	case MLX5_CMD_OP_SET_PP_RATE_LIMIT:
	case MLX5_CMD_OP_QUERY_RATE_LIMIT:
	case MLX5_CMD_OP_CREATE_SCHEDULING_ELEMENT:
	case MLX5_CMD_OP_QUERY_SCHEDULING_ELEMENT:
	case MLX5_CMD_OP_MODIFY_SCHEDULING_ELEMENT:
	case MLX5_CMD_OP_CREATE_QOS_PARA_VPORT:
	case MLX5_CMD_OP_ALLOC_PD:
	case MLX5_CMD_OP_ALLOC_UAR:
	case MLX5_CMD_OP_CONFIG_INT_MODERATION:
	case MLX5_CMD_OP_ACCESS_REG:
	case MLX5_CMD_OP_ATTACH_TO_MCG:
	case MLX5_CMD_OP_GET_DROPPED_PACKET_LOG:
	case MLX5_CMD_OP_MAD_IFC:
	case MLX5_CMD_OP_QUERY_MAD_DEMUX:
	case MLX5_CMD_OP_SET_MAD_DEMUX:
	case MLX5_CMD_OP_NOP:
	case MLX5_CMD_OP_ALLOC_XRCD:
	case MLX5_CMD_OP_ALLOC_TRANSPORT_DOMAIN:
	case MLX5_CMD_OP_QUERY_CONG_STATUS:
	case MLX5_CMD_OP_MODIFY_CONG_STATUS:
	case MLX5_CMD_OP_QUERY_CONG_PARAMS:
	case MLX5_CMD_OP_MODIFY_CONG_PARAMS:
	case MLX5_CMD_OP_QUERY_CONG_STATISTICS:
	case MLX5_CMD_OP_ADD_VXLAN_UDP_DPORT:
	case MLX5_CMD_OP_SET_L2_TABLE_ENTRY:
	case MLX5_CMD_OP_QUERY_L2_TABLE_ENTRY:
	case MLX5_CMD_OP_CREATE_LAG:
	case MLX5_CMD_OP_MODIFY_LAG:
	case MLX5_CMD_OP_QUERY_LAG:
	case MLX5_CMD_OP_CREATE_VPORT_LAG:
	case MLX5_CMD_OP_CREATE_TIR:
	case MLX5_CMD_OP_MODIFY_TIR:
	case MLX5_CMD_OP_QUERY_TIR:
	case MLX5_CMD_OP_CREATE_SQ:
	case MLX5_CMD_OP_MODIFY_SQ:
	case MLX5_CMD_OP_QUERY_SQ:
	case MLX5_CMD_OP_CREATE_RQ:
	case MLX5_CMD_OP_MODIFY_RQ:
	case MLX5_CMD_OP_QUERY_RQ:
	case MLX5_CMD_OP_CREATE_RMP:
	case MLX5_CMD_OP_MODIFY_RMP:
	case MLX5_CMD_OP_QUERY_RMP:
	case MLX5_CMD_OP_CREATE_TIS:
	case MLX5_CMD_OP_MODIFY_TIS:
	case MLX5_CMD_OP_QUERY_TIS:
	case MLX5_CMD_OP_CREATE_RQT:
	case MLX5_CMD_OP_MODIFY_RQT:
	case MLX5_CMD_OP_QUERY_RQT:

	case MLX5_CMD_OP_CREATE_FLOW_TABLE:
	case MLX5_CMD_OP_QUERY_FLOW_TABLE:
	case MLX5_CMD_OP_CREATE_FLOW_GROUP:
	case MLX5_CMD_OP_QUERY_FLOW_GROUP:
	case MLX5_CMD_OP_QUERY_FLOW_TABLE_ENTRY:
	case MLX5_CMD_OP_ALLOC_FLOW_COUNTER:
	case MLX5_CMD_OP_QUERY_FLOW_COUNTER:
	case MLX5_CMD_OP_ALLOC_PACKET_REFORMAT_CONTEXT:
	case MLX5_CMD_OP_ALLOC_MODIFY_HEADER_CONTEXT:
	case MLX5_CMD_OP_FPGA_CREATE_QP:
	case MLX5_CMD_OP_FPGA_MODIFY_QP:
	case MLX5_CMD_OP_FPGA_QUERY_QP:
	case MLX5_CMD_OP_FPGA_QUERY_QP_COUNTERS:
	case MLX5_CMD_OP_CREATE_GENERAL_OBJECT:
	case MLX5_CMD_OP_MODIFY_GENERAL_OBJECT:
	case MLX5_CMD_OP_QUERY_GENERAL_OBJECT:
	case MLX5_CMD_OP_ALLOC_MEMIC:
		*status = MLX5_DRIVER_STATUS_ABORTED;
		*synd = MLX5_DRIVER_SYND;
		return -EIO;
	default:
		mlx5_core_err(dev, "Unknown FW command (%d)\n", op);
		return -EINVAL;
	}
}

const char *mlx5_command_str(int command)
{
#define MLX5_COMMAND_STR_CASE(__cmd) case MLX5_CMD_OP_ ## __cmd: return #__cmd

	switch (command) {
	MLX5_COMMAND_STR_CASE(QUERY_HCA_CAP);
	MLX5_COMMAND_STR_CASE(QUERY_ADAPTER);
	MLX5_COMMAND_STR_CASE(INIT_HCA);
	MLX5_COMMAND_STR_CASE(TEARDOWN_HCA);
	MLX5_COMMAND_STR_CASE(ENABLE_HCA);
	MLX5_COMMAND_STR_CASE(DISABLE_HCA);
	MLX5_COMMAND_STR_CASE(QUERY_PAGES);
	MLX5_COMMAND_STR_CASE(MANAGE_PAGES);
	MLX5_COMMAND_STR_CASE(SET_HCA_CAP);
	MLX5_COMMAND_STR_CASE(QUERY_ISSI);
	MLX5_COMMAND_STR_CASE(SET_ISSI);
	MLX5_COMMAND_STR_CASE(SET_DRIVER_VERSION);
	MLX5_COMMAND_STR_CASE(CREATE_MKEY);
	MLX5_COMMAND_STR_CASE(QUERY_MKEY);
	MLX5_COMMAND_STR_CASE(DESTROY_MKEY);
	MLX5_COMMAND_STR_CASE(QUERY_SPECIAL_CONTEXTS);
	MLX5_COMMAND_STR_CASE(PAGE_FAULT_RESUME);
	MLX5_COMMAND_STR_CASE(CREATE_EQ);
	MLX5_COMMAND_STR_CASE(DESTROY_EQ);
	MLX5_COMMAND_STR_CASE(QUERY_EQ);
	MLX5_COMMAND_STR_CASE(GEN_EQE);
	MLX5_COMMAND_STR_CASE(CREATE_CQ);
	MLX5_COMMAND_STR_CASE(DESTROY_CQ);
	MLX5_COMMAND_STR_CASE(QUERY_CQ);
	MLX5_COMMAND_STR_CASE(MODIFY_CQ);
	MLX5_COMMAND_STR_CASE(CREATE_QP);
	MLX5_COMMAND_STR_CASE(DESTROY_QP);
	MLX5_COMMAND_STR_CASE(RST2INIT_QP);
	MLX5_COMMAND_STR_CASE(INIT2RTR_QP);
	MLX5_COMMAND_STR_CASE(RTR2RTS_QP);
	MLX5_COMMAND_STR_CASE(RTS2RTS_QP);
	MLX5_COMMAND_STR_CASE(SQERR2RTS_QP);
	MLX5_COMMAND_STR_CASE(2ERR_QP);
	MLX5_COMMAND_STR_CASE(2RST_QP);
	MLX5_COMMAND_STR_CASE(QUERY_QP);
	MLX5_COMMAND_STR_CASE(SQD_RTS_QP);
	MLX5_COMMAND_STR_CASE(INIT2INIT_QP);
	MLX5_COMMAND_STR_CASE(CREATE_PSV);
	MLX5_COMMAND_STR_CASE(DESTROY_PSV);
	MLX5_COMMAND_STR_CASE(CREATE_SRQ);
	MLX5_COMMAND_STR_CASE(DESTROY_SRQ);
	MLX5_COMMAND_STR_CASE(QUERY_SRQ);
	MLX5_COMMAND_STR_CASE(ARM_RQ);
	MLX5_COMMAND_STR_CASE(CREATE_XRC_SRQ);
	MLX5_COMMAND_STR_CASE(DESTROY_XRC_SRQ);
	MLX5_COMMAND_STR_CASE(QUERY_XRC_SRQ);
	MLX5_COMMAND_STR_CASE(ARM_XRC_SRQ);
	MLX5_COMMAND_STR_CASE(CREATE_DCT);
	MLX5_COMMAND_STR_CASE(DESTROY_DCT);
	MLX5_COMMAND_STR_CASE(DRAIN_DCT);
	MLX5_COMMAND_STR_CASE(QUERY_DCT);
	MLX5_COMMAND_STR_CASE(ARM_DCT_FOR_KEY_VIOLATION);
	MLX5_COMMAND_STR_CASE(QUERY_VPORT_STATE);
	MLX5_COMMAND_STR_CASE(MODIFY_VPORT_STATE);
	MLX5_COMMAND_STR_CASE(QUERY_ESW_VPORT_CONTEXT);
	MLX5_COMMAND_STR_CASE(MODIFY_ESW_VPORT_CONTEXT);
	MLX5_COMMAND_STR_CASE(QUERY_NIC_VPORT_CONTEXT);
	MLX5_COMMAND_STR_CASE(MODIFY_NIC_VPORT_CONTEXT);
	MLX5_COMMAND_STR_CASE(QUERY_ROCE_ADDRESS);
	MLX5_COMMAND_STR_CASE(SET_ROCE_ADDRESS);
	MLX5_COMMAND_STR_CASE(QUERY_HCA_VPORT_CONTEXT);
	MLX5_COMMAND_STR_CASE(MODIFY_HCA_VPORT_CONTEXT);
	MLX5_COMMAND_STR_CASE(QUERY_HCA_VPORT_GID);
	MLX5_COMMAND_STR_CASE(QUERY_HCA_VPORT_PKEY);
	MLX5_COMMAND_STR_CASE(QUERY_VNIC_ENV);
	MLX5_COMMAND_STR_CASE(QUERY_VPORT_COUNTER);
	MLX5_COMMAND_STR_CASE(ALLOC_Q_COUNTER);
	MLX5_COMMAND_STR_CASE(DEALLOC_Q_COUNTER);
	MLX5_COMMAND_STR_CASE(QUERY_Q_COUNTER);
	MLX5_COMMAND_STR_CASE(SET_MONITOR_COUNTER);
	MLX5_COMMAND_STR_CASE(ARM_MONITOR_COUNTER);
	MLX5_COMMAND_STR_CASE(SET_PP_RATE_LIMIT);
	MLX5_COMMAND_STR_CASE(QUERY_RATE_LIMIT);
	MLX5_COMMAND_STR_CASE(CREATE_SCHEDULING_ELEMENT);
	MLX5_COMMAND_STR_CASE(DESTROY_SCHEDULING_ELEMENT);
	MLX5_COMMAND_STR_CASE(QUERY_SCHEDULING_ELEMENT);
	MLX5_COMMAND_STR_CASE(MODIFY_SCHEDULING_ELEMENT);
	MLX5_COMMAND_STR_CASE(CREATE_QOS_PARA_VPORT);
	MLX5_COMMAND_STR_CASE(DESTROY_QOS_PARA_VPORT);
	MLX5_COMMAND_STR_CASE(ALLOC_PD);
	MLX5_COMMAND_STR_CASE(DEALLOC_PD);
	MLX5_COMMAND_STR_CASE(ALLOC_UAR);
	MLX5_COMMAND_STR_CASE(DEALLOC_UAR);
	MLX5_COMMAND_STR_CASE(CONFIG_INT_MODERATION);
	MLX5_COMMAND_STR_CASE(ACCESS_REG);
	MLX5_COMMAND_STR_CASE(ATTACH_TO_MCG);
	MLX5_COMMAND_STR_CASE(DETACH_FROM_MCG);
	MLX5_COMMAND_STR_CASE(GET_DROPPED_PACKET_LOG);
	MLX5_COMMAND_STR_CASE(MAD_IFC);
	MLX5_COMMAND_STR_CASE(QUERY_MAD_DEMUX);
	MLX5_COMMAND_STR_CASE(SET_MAD_DEMUX);
	MLX5_COMMAND_STR_CASE(NOP);
	MLX5_COMMAND_STR_CASE(ALLOC_XRCD);
	MLX5_COMMAND_STR_CASE(DEALLOC_XRCD);
	MLX5_COMMAND_STR_CASE(ALLOC_TRANSPORT_DOMAIN);
	MLX5_COMMAND_STR_CASE(DEALLOC_TRANSPORT_DOMAIN);
	MLX5_COMMAND_STR_CASE(QUERY_CONG_STATUS);
	MLX5_COMMAND_STR_CASE(MODIFY_CONG_STATUS);
	MLX5_COMMAND_STR_CASE(QUERY_CONG_PARAMS);
	MLX5_COMMAND_STR_CASE(MODIFY_CONG_PARAMS);
	MLX5_COMMAND_STR_CASE(QUERY_CONG_STATISTICS);
	MLX5_COMMAND_STR_CASE(ADD_VXLAN_UDP_DPORT);
	MLX5_COMMAND_STR_CASE(DELETE_VXLAN_UDP_DPORT);
	MLX5_COMMAND_STR_CASE(SET_L2_TABLE_ENTRY);
	MLX5_COMMAND_STR_CASE(QUERY_L2_TABLE_ENTRY);
	MLX5_COMMAND_STR_CASE(DELETE_L2_TABLE_ENTRY);
	MLX5_COMMAND_STR_CASE(SET_WOL_ROL);
	MLX5_COMMAND_STR_CASE(QUERY_WOL_ROL);
	MLX5_COMMAND_STR_CASE(CREATE_LAG);
	MLX5_COMMAND_STR_CASE(MODIFY_LAG);
	MLX5_COMMAND_STR_CASE(QUERY_LAG);
	MLX5_COMMAND_STR_CASE(DESTROY_LAG);
	MLX5_COMMAND_STR_CASE(CREATE_VPORT_LAG);
	MLX5_COMMAND_STR_CASE(DESTROY_VPORT_LAG);
	MLX5_COMMAND_STR_CASE(CREATE_TIR);
	MLX5_COMMAND_STR_CASE(MODIFY_TIR);
	MLX5_COMMAND_STR_CASE(DESTROY_TIR);
	MLX5_COMMAND_STR_CASE(QUERY_TIR);
	MLX5_COMMAND_STR_CASE(CREATE_SQ);
	MLX5_COMMAND_STR_CASE(MODIFY_SQ);
	MLX5_COMMAND_STR_CASE(DESTROY_SQ);
	MLX5_COMMAND_STR_CASE(QUERY_SQ);
	MLX5_COMMAND_STR_CASE(CREATE_RQ);
	MLX5_COMMAND_STR_CASE(MODIFY_RQ);
	MLX5_COMMAND_STR_CASE(DESTROY_RQ);
	MLX5_COMMAND_STR_CASE(QUERY_RQ);
	MLX5_COMMAND_STR_CASE(CREATE_RMP);
	MLX5_COMMAND_STR_CASE(MODIFY_RMP);
	MLX5_COMMAND_STR_CASE(DESTROY_RMP);
	MLX5_COMMAND_STR_CASE(QUERY_RMP);
	MLX5_COMMAND_STR_CASE(CREATE_TIS);
	MLX5_COMMAND_STR_CASE(MODIFY_TIS);
	MLX5_COMMAND_STR_CASE(DESTROY_TIS);
	MLX5_COMMAND_STR_CASE(QUERY_TIS);
	MLX5_COMMAND_STR_CASE(CREATE_RQT);
	MLX5_COMMAND_STR_CASE(MODIFY_RQT);
	MLX5_COMMAND_STR_CASE(DESTROY_RQT);
	MLX5_COMMAND_STR_CASE(QUERY_RQT);
	MLX5_COMMAND_STR_CASE(SET_FLOW_TABLE_ROOT);
	MLX5_COMMAND_STR_CASE(CREATE_FLOW_TABLE);
	MLX5_COMMAND_STR_CASE(DESTROY_FLOW_TABLE);
	MLX5_COMMAND_STR_CASE(QUERY_FLOW_TABLE);
	MLX5_COMMAND_STR_CASE(CREATE_FLOW_GROUP);
	MLX5_COMMAND_STR_CASE(DESTROY_FLOW_GROUP);
	MLX5_COMMAND_STR_CASE(QUERY_FLOW_GROUP);
	MLX5_COMMAND_STR_CASE(SET_FLOW_TABLE_ENTRY);
	MLX5_COMMAND_STR_CASE(QUERY_FLOW_TABLE_ENTRY);
	MLX5_COMMAND_STR_CASE(DELETE_FLOW_TABLE_ENTRY);
	MLX5_COMMAND_STR_CASE(ALLOC_FLOW_COUNTER);
	MLX5_COMMAND_STR_CASE(DEALLOC_FLOW_COUNTER);
	MLX5_COMMAND_STR_CASE(QUERY_FLOW_COUNTER);
	MLX5_COMMAND_STR_CASE(MODIFY_FLOW_TABLE);
	MLX5_COMMAND_STR_CASE(ALLOC_PACKET_REFORMAT_CONTEXT);
	MLX5_COMMAND_STR_CASE(DEALLOC_PACKET_REFORMAT_CONTEXT);
	MLX5_COMMAND_STR_CASE(ALLOC_MODIFY_HEADER_CONTEXT);
	MLX5_COMMAND_STR_CASE(DEALLOC_MODIFY_HEADER_CONTEXT);
	MLX5_COMMAND_STR_CASE(FPGA_CREATE_QP);
	MLX5_COMMAND_STR_CASE(FPGA_MODIFY_QP);
	MLX5_COMMAND_STR_CASE(FPGA_QUERY_QP);
	MLX5_COMMAND_STR_CASE(FPGA_QUERY_QP_COUNTERS);
	MLX5_COMMAND_STR_CASE(FPGA_DESTROY_QP);
	MLX5_COMMAND_STR_CASE(CREATE_XRQ);
	MLX5_COMMAND_STR_CASE(DESTROY_XRQ);
	MLX5_COMMAND_STR_CASE(QUERY_XRQ);
	MLX5_COMMAND_STR_CASE(ARM_XRQ);
	MLX5_COMMAND_STR_CASE(CREATE_GENERAL_OBJECT);
	MLX5_COMMAND_STR_CASE(DESTROY_GENERAL_OBJECT);
	MLX5_COMMAND_STR_CASE(MODIFY_GENERAL_OBJECT);
	MLX5_COMMAND_STR_CASE(QUERY_GENERAL_OBJECT);
	MLX5_COMMAND_STR_CASE(QUERY_MODIFY_HEADER_CONTEXT);
	MLX5_COMMAND_STR_CASE(ALLOC_MEMIC);
	MLX5_COMMAND_STR_CASE(DEALLOC_MEMIC);
	MLX5_COMMAND_STR_CASE(QUERY_HOST_PARAMS);
	default: return "unknown command opcode";
	}
}

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

static int cmd_status_to_err(u8 status)
{
	switch (status) {
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

struct mlx5_ifc_mbox_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x40];
};

struct mlx5_ifc_mbox_in_bits {
	u8         opcode[0x10];
	u8         uid[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         reserved_at_40[0x40];
};

void mlx5_cmd_mbox_status(void *out, u8 *status, u32 *syndrome)
{
	*status = MLX5_GET(mbox_out, out, status);
	*syndrome = MLX5_GET(mbox_out, out, syndrome);
}

static int mlx5_cmd_check(struct mlx5_core_dev *dev, void *in, void *out)
{
	u32 syndrome;
	u8  status;
	u16 opcode;
	u16 op_mod;
	u16 uid;

	mlx5_cmd_mbox_status(out, &status, &syndrome);
	if (!status)
		return 0;

	opcode = MLX5_GET(mbox_in, in, opcode);
	op_mod = MLX5_GET(mbox_in, in, op_mod);
	uid    = MLX5_GET(mbox_in, in, uid);

	if (!uid && opcode != MLX5_CMD_OP_DESTROY_MKEY)
		mlx5_core_err_rl(dev,
			"%s(0x%x) op_mod(0x%x) failed, status %s(0x%x), syndrome (0x%x)\n",
			mlx5_command_str(opcode), opcode, op_mod,
			cmd_status_str(status), status, syndrome);
	else
		mlx5_core_dbg(dev,
		      "%s(0x%x) op_mod(0x%x) failed, status %s(0x%x), syndrome (0x%x)\n",
		      mlx5_command_str(opcode),
		      opcode, op_mod,
		      cmd_status_str(status),
		      status,
		      syndrome);

	return cmd_status_to_err(status);
}

static void dump_command(struct mlx5_core_dev *dev,
			 struct mlx5_cmd_work_ent *ent, int input)
{
	struct mlx5_cmd_msg *msg = input ? ent->in : ent->out;
	u16 op = MLX5_GET(mbox_in, ent->lay->in, opcode);
	struct mlx5_cmd_mailbox *next = msg->next;
	int n = mlx5_calc_cmd_blocks(msg);
	int data_only;
	u32 offset = 0;
	int dump_len;
	int i;

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

	for (i = 0; i < n && next; i++)  {
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

static u16 msg_to_opcode(struct mlx5_cmd_msg *in)
{
	return MLX5_GET(mbox_in, in->first.data, opcode);
}

static void mlx5_cmd_comp_handler(struct mlx5_core_dev *dev, u64 vec, bool forced);

static void cb_timeout_handler(struct work_struct *work)
{
	struct delayed_work *dwork = container_of(work, struct delayed_work,
						  work);
	struct mlx5_cmd_work_ent *ent = container_of(dwork,
						     struct mlx5_cmd_work_ent,
						     cb_timeout_work);
	struct mlx5_core_dev *dev = container_of(ent->cmd, struct mlx5_core_dev,
						 cmd);

	ent->ret = -ETIMEDOUT;
	mlx5_core_warn(dev, "%s(0x%x) timeout. Will cause a leak of a command resource\n",
		       mlx5_command_str(msg_to_opcode(ent->in)),
		       msg_to_opcode(ent->in));
	mlx5_cmd_comp_handler(dev, 1UL << ent->idx, true);
}

static void free_msg(struct mlx5_core_dev *dev, struct mlx5_cmd_msg *msg);
static void mlx5_free_cmd_msg(struct mlx5_core_dev *dev,
			      struct mlx5_cmd_msg *msg);

static void cmd_work_handler(struct work_struct *work)
{
	struct mlx5_cmd_work_ent *ent = container_of(work, struct mlx5_cmd_work_ent, work);
	struct mlx5_cmd *cmd = ent->cmd;
	struct mlx5_core_dev *dev = container_of(cmd, struct mlx5_core_dev, cmd);
	unsigned long cb_timeout = msecs_to_jiffies(MLX5_CMD_TIMEOUT_MSEC);
	struct mlx5_cmd_layout *lay;
	struct semaphore *sem;
	unsigned long flags;
	bool poll_cmd = ent->polling;
	int alloc_ret;
	int cmd_mode;

	sem = ent->page_queue ? &cmd->pages_sem : &cmd->sem;
	down(sem);
	if (!ent->page_queue) {
		alloc_ret = alloc_ent(cmd);
		if (alloc_ret < 0) {
			mlx5_core_err(dev, "failed to allocate command entry\n");
			if (ent->callback) {
				ent->callback(-EAGAIN, ent->context);
				mlx5_free_cmd_msg(dev, ent->out);
				free_msg(dev, ent->in);
				free_cmd(ent);
			} else {
				ent->ret = -EAGAIN;
				complete(&ent->done);
			}
			up(sem);
			return;
		}
		ent->idx = alloc_ret;
	} else {
		ent->idx = cmd->max_reg_cmds;
		spin_lock_irqsave(&cmd->alloc_lock, flags);
		clear_bit(ent->idx, &cmd->bitmask);
		spin_unlock_irqrestore(&cmd->alloc_lock, flags);
	}

	cmd->ent_arr[ent->idx] = ent;
	set_bit(MLX5_CMD_ENT_STATE_PENDING_COMP, &ent->state);
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
	cmd_mode = cmd->mode;

	if (ent->callback)
		schedule_delayed_work(&ent->cb_timeout_work, cb_timeout);

	/* Skip sending command to fw if internal error */
	if (pci_channel_offline(dev->pdev) ||
	    dev->state == MLX5_DEVICE_STATE_INTERNAL_ERROR) {
		u8 status = 0;
		u32 drv_synd;

		ent->ret = mlx5_internal_err_ret_value(dev, msg_to_opcode(ent->in), &drv_synd, &status);
		MLX5_SET(mbox_out, ent->out, status, status);
		MLX5_SET(mbox_out, ent->out, syndrome, drv_synd);

		mlx5_cmd_comp_handler(dev, 1UL << ent->idx, true);
		return;
	}

	/* ring doorbell after the descriptor is valid */
	mlx5_core_dbg(dev, "writing 0x%x to command doorbell\n", 1 << ent->idx);
	wmb();
	iowrite32be(1 << ent->idx, &dev->iseg->cmd_dbell);
	mmiowb();
	/* if not in polling don't use ent after this point */
	if (cmd_mode == CMD_MODE_POLLING || poll_cmd) {
		poll_timeout(ent);
		/* make sure we read the descriptor after ownership is SW */
		rmb();
		mlx5_cmd_comp_handler(dev, 1UL << ent->idx, (ent->ret == -ETIMEDOUT));
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
		return "command output length error";
	case MLX5_CMD_DELIVERY_STAT_RES_FLD_NOT_CLR_ERR:
		return "reserved fields not cleared";
	case MLX5_CMD_DELIVERY_STAT_CMD_DESCR_ERR:
		return "bad command descriptor type";
	default:
		return "unknown status code";
	}
}

static int wait_func(struct mlx5_core_dev *dev, struct mlx5_cmd_work_ent *ent)
{
	unsigned long timeout = msecs_to_jiffies(MLX5_CMD_TIMEOUT_MSEC);
	struct mlx5_cmd *cmd = &dev->cmd;
	int err;

	if (cmd->mode == CMD_MODE_POLLING || ent->polling) {
		wait_for_completion(&ent->done);
	} else if (!wait_for_completion_timeout(&ent->done, timeout)) {
		ent->ret = -ETIMEDOUT;
		mlx5_cmd_comp_handler(dev, 1UL << ent->idx, true);
	}

	err = ent->ret;

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
			   void *context, int page_queue, u8 *status,
			   u8 token, bool force_polling)
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

	ent->token = token;
	ent->polling = force_polling;

	if (!callback)
		init_completion(&ent->done);

	INIT_DELAYED_WORK(&ent->cb_timeout_work, cb_timeout_handler);
	INIT_WORK(&ent->work, cmd_work_handler);
	if (page_queue) {
		cmd_work_handler(&ent->work);
	} else if (!queue_work(cmd->wq, &ent->work)) {
		mlx5_core_warn(dev, "failed to queue work\n");
		err = -ENOMEM;
		goto out_free;
	}

	if (callback)
		goto out;

	err = wait_func(dev, ent);
	if (err == -ETIMEDOUT)
		goto out;

	ds = ent->ts2 - ent->ts1;
	op = MLX5_GET(mbox_in, in->first.data, opcode);
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

	if (count < sizeof(lbuf) - 1)
		return -EINVAL;

	if (copy_from_user(lbuf, buf, sizeof(lbuf) - 1))
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

static int mlx5_copy_to_msg(struct mlx5_cmd_msg *to, void *from, int size,
			    u8 token)
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
		block->token = token;
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

	mailbox->buf = dma_pool_zalloc(dev->cmd.pool, flags,
				       &mailbox->dma);
	if (!mailbox->buf) {
		mlx5_core_dbg(dev, "failed allocation\n");
		kfree(mailbox);
		return ERR_PTR(-ENOMEM);
	}
	mailbox->next = NULL;

	return mailbox;
}

static void free_cmd_box(struct mlx5_core_dev *dev,
			 struct mlx5_cmd_mailbox *mailbox)
{
	dma_pool_free(dev->cmd.pool, mailbox->buf, mailbox->dma);
	kfree(mailbox);
}

static struct mlx5_cmd_msg *mlx5_alloc_cmd_msg(struct mlx5_core_dev *dev,
					       gfp_t flags, int size,
					       u8 token)
{
	struct mlx5_cmd_mailbox *tmp, *head = NULL;
	struct mlx5_cmd_prot_block *block;
	struct mlx5_cmd_msg *msg;
	int err;
	int n;
	int i;

	msg = kzalloc(sizeof(*msg), flags);
	if (!msg)
		return ERR_PTR(-ENOMEM);

	msg->len = size;
	n = mlx5_calc_cmd_blocks(msg);

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
		block->token = token;
		head = tmp;
	}
	msg->next = head;
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

	if (*pos != 0)
		return -EINVAL;

	kfree(dbg->in_msg);
	dbg->in_msg = NULL;
	dbg->inlen = 0;
	ptr = memdup_user(buf, count);
	if (IS_ERR(ptr))
		return PTR_ERR(ptr);
	dbg->in_msg = ptr;
	dbg->inlen = count;

	*pos = count;

	return count;
}

static ssize_t data_read(struct file *filp, char __user *buf, size_t count,
			 loff_t *pos)
{
	struct mlx5_core_dev *dev = filp->private_data;
	struct mlx5_cmd_debug *dbg = &dev->cmd.dbg;

	if (!dbg->out_msg)
		return -ENOMEM;

	return simple_read_from_buffer(buf, count, pos, dbg->out_msg,
				       dbg->outlen);
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

	err = snprintf(outlen, sizeof(outlen), "%d", dbg->outlen);
	if (err < 0)
		return err;

	return simple_read_from_buffer(buf, count, pos, outlen, err);
}

static ssize_t outlen_write(struct file *filp, const char __user *buf,
			    size_t count, loff_t *pos)
{
	struct mlx5_core_dev *dev = filp->private_data;
	struct mlx5_cmd_debug *dbg = &dev->cmd.dbg;
	char outlen_str[8] = {0};
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
		 dev->priv.name);
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

static void mlx5_cmd_change_mod(struct mlx5_core_dev *dev, int mode)
{
	struct mlx5_cmd *cmd = &dev->cmd;
	int i;

	for (i = 0; i < cmd->max_reg_cmds; i++)
		down(&cmd->sem);
	down(&cmd->pages_sem);

	cmd->mode = mode;

	up(&cmd->pages_sem);
	for (i = 0; i < cmd->max_reg_cmds; i++)
		up(&cmd->sem);
}

static int cmd_comp_notifier(struct notifier_block *nb,
			     unsigned long type, void *data)
{
	struct mlx5_core_dev *dev;
	struct mlx5_cmd *cmd;
	struct mlx5_eqe *eqe;

	cmd = mlx5_nb_cof(nb, struct mlx5_cmd, nb);
	dev = container_of(cmd, struct mlx5_core_dev, cmd);
	eqe = data;

	mlx5_cmd_comp_handler(dev, be32_to_cpu(eqe->data.cmd.vector), false);

	return NOTIFY_OK;
}
void mlx5_cmd_use_events(struct mlx5_core_dev *dev)
{
	MLX5_NB_INIT(&dev->cmd.nb, cmd_comp_notifier, CMD);
	mlx5_eq_notifier_register(dev, &dev->cmd.nb);
	mlx5_cmd_change_mod(dev, CMD_MODE_EVENTS);
}

void mlx5_cmd_use_polling(struct mlx5_core_dev *dev)
{
	mlx5_cmd_change_mod(dev, CMD_MODE_POLLING);
	mlx5_eq_notifier_unregister(dev, &dev->cmd.nb);
}

static void free_msg(struct mlx5_core_dev *dev, struct mlx5_cmd_msg *msg)
{
	unsigned long flags;

	if (msg->parent) {
		spin_lock_irqsave(&msg->parent->lock, flags);
		list_add_tail(&msg->list, &msg->parent->head);
		spin_unlock_irqrestore(&msg->parent->lock, flags);
	} else {
		mlx5_free_cmd_msg(dev, msg);
	}
}

static void mlx5_cmd_comp_handler(struct mlx5_core_dev *dev, u64 vec, bool forced)
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
	unsigned long vector;

	/* there can be at most 32 command queues */
	vector = vec & 0xffffffff;
	for (i = 0; i < (1 << cmd->log_sz); i++) {
		if (test_bit(i, &vector)) {
			struct semaphore *sem;

			ent = cmd->ent_arr[i];

			/* if we already completed the command, ignore it */
			if (!test_and_clear_bit(MLX5_CMD_ENT_STATE_PENDING_COMP,
						&ent->state)) {
				/* only real completion can free the cmd slot */
				if (!forced) {
					mlx5_core_err(dev, "Command completion arrived after timeout (entry idx = %d).\n",
						      ent->idx);
					free_ent(cmd, ent->idx);
					free_cmd(ent);
				}
				continue;
			}

			if (ent->callback)
				cancel_delayed_work(&ent->cb_timeout_work);
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
				if (vec & MLX5_TRIGGERED_CMD_COMP)
					ent->status = MLX5_DRIVER_STATUS_ABORTED;
				else
					ent->status = ent->lay->status_own >> 1;

				mlx5_core_dbg(dev, "command completed. ret 0x%x, delivery status %s(0x%x)\n",
					      ent->ret, deliv_status_to_str(ent->status), ent->status);
			}

			/* only real completion will free the entry slot */
			if (!forced)
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
				if (!err) {
					err = mlx5_copy_from_msg(ent->uout,
								 ent->out,
								 ent->uout_size);

					err = err ? err : mlx5_cmd_check(dev,
									ent->in->first.data,
									ent->uout);
				}

				mlx5_free_cmd_msg(dev, ent->out);
				free_msg(dev, ent->in);

				err = err ? err : ent->status;
				if (!forced)
					free_cmd(ent);
				callback(err, context);
			} else {
				complete(&ent->done);
			}
			up(sem);
		}
	}
}

void mlx5_cmd_trigger_completions(struct mlx5_core_dev *dev)
{
	unsigned long flags;
	u64 vector;

	/* wait for pending handlers to complete */
	mlx5_eq_synchronize_cmd_irq(dev);
	spin_lock_irqsave(&dev->cmd.alloc_lock, flags);
	vector = ~dev->cmd.bitmask & ((1ul << (1 << dev->cmd.log_sz)) - 1);
	if (!vector)
		goto no_trig;

	vector |= MLX5_TRIGGERED_CMD_COMP;
	spin_unlock_irqrestore(&dev->cmd.alloc_lock, flags);

	mlx5_core_dbg(dev, "vector 0x%llx\n", vector);
	mlx5_cmd_comp_handler(dev, vector, true);
	return;

no_trig:
	spin_unlock_irqrestore(&dev->cmd.alloc_lock, flags);
}

static int status_to_err(u8 status)
{
	return status ? -1 : 0; /* TBD more meaningful codes */
}

static struct mlx5_cmd_msg *alloc_msg(struct mlx5_core_dev *dev, int in_size,
				      gfp_t gfp)
{
	struct mlx5_cmd_msg *msg = ERR_PTR(-ENOMEM);
	struct cmd_msg_cache *ch = NULL;
	struct mlx5_cmd *cmd = &dev->cmd;
	int i;

	if (in_size <= 16)
		goto cache_miss;

	for (i = 0; i < MLX5_NUM_COMMAND_CACHES; i++) {
		ch = &cmd->cache[i];
		if (in_size > ch->max_inbox_size)
			continue;
		spin_lock_irq(&ch->lock);
		if (list_empty(&ch->head)) {
			spin_unlock_irq(&ch->lock);
			continue;
		}
		msg = list_entry(ch->head.next, typeof(*msg), list);
		/* For cached lists, we must explicitly state what is
		 * the real size
		 */
		msg->len = in_size;
		list_del(&msg->list);
		spin_unlock_irq(&ch->lock);
		break;
	}

	if (!IS_ERR(msg))
		return msg;

cache_miss:
	msg = mlx5_alloc_cmd_msg(dev, gfp, in_size, 0);
	return msg;
}

static int is_manage_pages(void *in)
{
	return MLX5_GET(mbox_in, in, opcode) == MLX5_CMD_OP_MANAGE_PAGES;
}

static int cmd_exec(struct mlx5_core_dev *dev, void *in, int in_size, void *out,
		    int out_size, mlx5_cmd_cbk_t callback, void *context,
		    bool force_polling)
{
	struct mlx5_cmd_msg *inb;
	struct mlx5_cmd_msg *outb;
	int pages_queue;
	gfp_t gfp;
	int err;
	u8 status = 0;
	u32 drv_synd;
	u8 token;

	if (pci_channel_offline(dev->pdev) ||
	    dev->state == MLX5_DEVICE_STATE_INTERNAL_ERROR) {
		u16 opcode = MLX5_GET(mbox_in, in, opcode);

		err = mlx5_internal_err_ret_value(dev, opcode, &drv_synd, &status);
		MLX5_SET(mbox_out, out, status, status);
		MLX5_SET(mbox_out, out, syndrome, drv_synd);
		return err;
	}

	pages_queue = is_manage_pages(in);
	gfp = callback ? GFP_ATOMIC : GFP_KERNEL;

	inb = alloc_msg(dev, in_size, gfp);
	if (IS_ERR(inb)) {
		err = PTR_ERR(inb);
		return err;
	}

	token = alloc_token(&dev->cmd);

	err = mlx5_copy_to_msg(inb, in, in_size, token);
	if (err) {
		mlx5_core_warn(dev, "err %d\n", err);
		goto out_in;
	}

	outb = mlx5_alloc_cmd_msg(dev, gfp, out_size, token);
	if (IS_ERR(outb)) {
		err = PTR_ERR(outb);
		goto out_in;
	}

	err = mlx5_cmd_invoke(dev, inb, outb, out, out_size, callback, context,
			      pages_queue, &status, token, force_polling);
	if (err)
		goto out_out;

	mlx5_core_dbg(dev, "err %d, status %d\n", err, status);
	if (status) {
		err = status_to_err(status);
		goto out_out;
	}

	if (!callback)
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
	int err;

	err = cmd_exec(dev, in, in_size, out, out_size, NULL, NULL, false);
	return err ? : mlx5_cmd_check(dev, in, out);
}
EXPORT_SYMBOL(mlx5_cmd_exec);

void mlx5_cmd_init_async_ctx(struct mlx5_core_dev *dev,
			     struct mlx5_async_ctx *ctx)
{
	ctx->dev = dev;
	/* Starts at 1 to avoid doing wake_up if we are not cleaning up */
	atomic_set(&ctx->num_inflight, 1);
	init_waitqueue_head(&ctx->wait);
}
EXPORT_SYMBOL(mlx5_cmd_init_async_ctx);

/**
 * mlx5_cmd_cleanup_async_ctx - Clean up an async_ctx
 * @ctx: The ctx to clean
 *
 * Upon return all callbacks given to mlx5_cmd_exec_cb() have been called. The
 * caller must ensure that mlx5_cmd_exec_cb() is not called during or after
 * the call mlx5_cleanup_async_ctx().
 */
void mlx5_cmd_cleanup_async_ctx(struct mlx5_async_ctx *ctx)
{
	atomic_dec(&ctx->num_inflight);
	wait_event(ctx->wait, atomic_read(&ctx->num_inflight) == 0);
}
EXPORT_SYMBOL(mlx5_cmd_cleanup_async_ctx);

static void mlx5_cmd_exec_cb_handler(int status, void *_work)
{
	struct mlx5_async_work *work = _work;
	struct mlx5_async_ctx *ctx = work->ctx;

	work->user_callback(status, work);
	if (atomic_dec_and_test(&ctx->num_inflight))
		wake_up(&ctx->wait);
}

int mlx5_cmd_exec_cb(struct mlx5_async_ctx *ctx, void *in, int in_size,
		     void *out, int out_size, mlx5_async_cbk_t callback,
		     struct mlx5_async_work *work)
{
	int ret;

	work->ctx = ctx;
	work->user_callback = callback;
	if (WARN_ON(!atomic_inc_not_zero(&ctx->num_inflight)))
		return -EIO;
	ret = cmd_exec(ctx->dev, in, in_size, out, out_size,
		       mlx5_cmd_exec_cb_handler, work, false);
	if (ret && atomic_dec_and_test(&ctx->num_inflight))
		wake_up(&ctx->wait);

	return ret;
}
EXPORT_SYMBOL(mlx5_cmd_exec_cb);

int mlx5_cmd_exec_polling(struct mlx5_core_dev *dev, void *in, int in_size,
			  void *out, int out_size)
{
	int err;

	err = cmd_exec(dev, in, in_size, out, out_size, NULL, NULL, true);

	return err ? : mlx5_cmd_check(dev, in, out);
}
EXPORT_SYMBOL(mlx5_cmd_exec_polling);

static void destroy_msg_cache(struct mlx5_core_dev *dev)
{
	struct cmd_msg_cache *ch;
	struct mlx5_cmd_msg *msg;
	struct mlx5_cmd_msg *n;
	int i;

	for (i = 0; i < MLX5_NUM_COMMAND_CACHES; i++) {
		ch = &dev->cmd.cache[i];
		list_for_each_entry_safe(msg, n, &ch->head, list) {
			list_del(&msg->list);
			mlx5_free_cmd_msg(dev, msg);
		}
	}
}

static unsigned cmd_cache_num_ent[MLX5_NUM_COMMAND_CACHES] = {
	512, 32, 16, 8, 2
};

static unsigned cmd_cache_ent_size[MLX5_NUM_COMMAND_CACHES] = {
	16 + MLX5_CMD_DATA_BLOCK_SIZE,
	16 + MLX5_CMD_DATA_BLOCK_SIZE * 2,
	16 + MLX5_CMD_DATA_BLOCK_SIZE * 16,
	16 + MLX5_CMD_DATA_BLOCK_SIZE * 256,
	16 + MLX5_CMD_DATA_BLOCK_SIZE * 512,
};

static void create_msg_cache(struct mlx5_core_dev *dev)
{
	struct mlx5_cmd *cmd = &dev->cmd;
	struct cmd_msg_cache *ch;
	struct mlx5_cmd_msg *msg;
	int i;
	int k;

	/* Initialize and fill the caches with initial entries */
	for (k = 0; k < MLX5_NUM_COMMAND_CACHES; k++) {
		ch = &cmd->cache[k];
		spin_lock_init(&ch->lock);
		INIT_LIST_HEAD(&ch->head);
		ch->num_ent = cmd_cache_num_ent[k];
		ch->max_inbox_size = cmd_cache_ent_size[k];
		for (i = 0; i < ch->num_ent; i++) {
			msg = mlx5_alloc_cmd_msg(dev, GFP_KERNEL | __GFP_NOWARN,
						 ch->max_inbox_size, 0);
			if (IS_ERR(msg))
				break;
			msg->parent = ch;
			list_add_tail(&msg->list, &ch->head);
		}
	}
}

static int alloc_cmd_page(struct mlx5_core_dev *dev, struct mlx5_cmd *cmd)
{
	struct device *ddev = &dev->pdev->dev;

	cmd->cmd_alloc_buf = dma_alloc_coherent(ddev, MLX5_ADAPTER_PAGE_SIZE,
						&cmd->alloc_dma, GFP_KERNEL);
	if (!cmd->cmd_alloc_buf)
		return -ENOMEM;

	/* make sure it is aligned to 4K */
	if (!((uintptr_t)cmd->cmd_alloc_buf & (MLX5_ADAPTER_PAGE_SIZE - 1))) {
		cmd->cmd_buf = cmd->cmd_alloc_buf;
		cmd->dma = cmd->alloc_dma;
		cmd->alloc_size = MLX5_ADAPTER_PAGE_SIZE;
		return 0;
	}

	dma_free_coherent(ddev, MLX5_ADAPTER_PAGE_SIZE, cmd->cmd_alloc_buf,
			  cmd->alloc_dma);
	cmd->cmd_alloc_buf = dma_alloc_coherent(ddev,
						2 * MLX5_ADAPTER_PAGE_SIZE - 1,
						&cmd->alloc_dma, GFP_KERNEL);
	if (!cmd->cmd_alloc_buf)
		return -ENOMEM;

	cmd->cmd_buf = PTR_ALIGN(cmd->cmd_alloc_buf, MLX5_ADAPTER_PAGE_SIZE);
	cmd->dma = ALIGN(cmd->alloc_dma, MLX5_ADAPTER_PAGE_SIZE);
	cmd->alloc_size = 2 * MLX5_ADAPTER_PAGE_SIZE - 1;
	return 0;
}

static void free_cmd_page(struct mlx5_core_dev *dev, struct mlx5_cmd *cmd)
{
	struct device *ddev = &dev->pdev->dev;

	dma_free_coherent(ddev, cmd->alloc_size, cmd->cmd_alloc_buf,
			  cmd->alloc_dma);
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

	memset(cmd, 0, sizeof(*cmd));
	cmd_if_rev = cmdif_rev(dev);
	if (cmd_if_rev != CMD_IF_REV) {
		mlx5_core_err(dev,
			      "Driver cmdif rev(%d) differs from firmware's(%d)\n",
			      CMD_IF_REV, cmd_if_rev);
		return -EINVAL;
	}

	cmd->pool = dma_pool_create("mlx5_cmd", &dev->pdev->dev, size, align,
				    0);
	if (!cmd->pool)
		return -ENOMEM;

	err = alloc_cmd_page(dev, cmd);
	if (err)
		goto err_free_pool;

	cmd_l = ioread32be(&dev->iseg->cmdq_addr_l_sz) & 0xff;
	cmd->log_sz = cmd_l >> 4 & 0xf;
	cmd->log_stride = cmd_l & 0xf;
	if (1 << cmd->log_sz > MLX5_MAX_COMMANDS) {
		mlx5_core_err(dev, "firmware reports too many outstanding commands %d\n",
			      1 << cmd->log_sz);
		err = -EINVAL;
		goto err_free_page;
	}

	if (cmd->log_sz + cmd->log_stride > MLX5_ADAPTER_PAGE_SHIFT) {
		mlx5_core_err(dev, "command queue size overflow\n");
		err = -EINVAL;
		goto err_free_page;
	}

	cmd->checksum_disabled = 1;
	cmd->max_reg_cmds = (1 << cmd->log_sz) - 1;
	cmd->bitmask = (1UL << cmd->max_reg_cmds) - 1;

	cmd->cmdif_rev = ioread32be(&dev->iseg->cmdif_rev_fw_sub) >> 16;
	if (cmd->cmdif_rev > CMD_IF_REV) {
		mlx5_core_err(dev, "driver does not support command interface version. driver %d, firmware %d\n",
			      CMD_IF_REV, cmd->cmdif_rev);
		err = -EOPNOTSUPP;
		goto err_free_page;
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
		mlx5_core_err(dev, "invalid command queue address\n");
		err = -ENOMEM;
		goto err_free_page;
	}

	iowrite32be(cmd_h, &dev->iseg->cmdq_addr_h);
	iowrite32be(cmd_l, &dev->iseg->cmdq_addr_l_sz);

	/* Make sure firmware sees the complete address before we proceed */
	wmb();

	mlx5_core_dbg(dev, "descriptor at dma 0x%llx\n", (unsigned long long)(cmd->dma));

	cmd->mode = CMD_MODE_POLLING;

	create_msg_cache(dev);

	set_wqname(dev);
	cmd->wq = create_singlethread_workqueue(cmd->wq_name);
	if (!cmd->wq) {
		mlx5_core_err(dev, "failed to create command workqueue\n");
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

err_free_page:
	free_cmd_page(dev, cmd);

err_free_pool:
	dma_pool_destroy(cmd->pool);

	return err;
}
EXPORT_SYMBOL(mlx5_cmd_init);

void mlx5_cmd_cleanup(struct mlx5_core_dev *dev)
{
	struct mlx5_cmd *cmd = &dev->cmd;

	clean_debug_files(dev);
	destroy_workqueue(cmd->wq);
	destroy_msg_cache(dev);
	free_cmd_page(dev, cmd);
	dma_pool_destroy(cmd->pool);
}
EXPORT_SYMBOL(mlx5_cmd_cleanup);

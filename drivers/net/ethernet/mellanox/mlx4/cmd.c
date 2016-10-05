/*
 * Copyright (c) 2004, 2005 Topspin Communications.  All rights reserved.
 * Copyright (c) 2005, 2006, 2007, 2008 Mellanox Technologies. All rights reserved.
 * Copyright (c) 2005, 2006, 2007 Cisco Systems, Inc.  All rights reserved.
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

#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/export.h>
#include <linux/pci.h>
#include <linux/errno.h>

#include <linux/mlx4/cmd.h>
#include <linux/mlx4/device.h>
#include <linux/semaphore.h>
#include <rdma/ib_smi.h>
#include <linux/delay.h>

#include <asm/io.h>

#include "mlx4.h"
#include "fw.h"
#include "fw_qos.h"
#include "mlx4_stats.h"

#define CMD_POLL_TOKEN 0xffff
#define INBOX_MASK	0xffffffffffffff00ULL

#define CMD_CHAN_VER 1
#define CMD_CHAN_IF_REV 1

enum {
	/* command completed successfully: */
	CMD_STAT_OK		= 0x00,
	/* Internal error (such as a bus error) occurred while processing command: */
	CMD_STAT_INTERNAL_ERR	= 0x01,
	/* Operation/command not supported or opcode modifier not supported: */
	CMD_STAT_BAD_OP		= 0x02,
	/* Parameter not supported or parameter out of range: */
	CMD_STAT_BAD_PARAM	= 0x03,
	/* System not enabled or bad system state: */
	CMD_STAT_BAD_SYS_STATE	= 0x04,
	/* Attempt to access reserved or unallocaterd resource: */
	CMD_STAT_BAD_RESOURCE	= 0x05,
	/* Requested resource is currently executing a command, or is otherwise busy: */
	CMD_STAT_RESOURCE_BUSY	= 0x06,
	/* Required capability exceeds device limits: */
	CMD_STAT_EXCEED_LIM	= 0x08,
	/* Resource is not in the appropriate state or ownership: */
	CMD_STAT_BAD_RES_STATE	= 0x09,
	/* Index out of range: */
	CMD_STAT_BAD_INDEX	= 0x0a,
	/* FW image corrupted: */
	CMD_STAT_BAD_NVMEM	= 0x0b,
	/* Error in ICM mapping (e.g. not enough auxiliary ICM pages to execute command): */
	CMD_STAT_ICM_ERROR	= 0x0c,
	/* Attempt to modify a QP/EE which is not in the presumed state: */
	CMD_STAT_BAD_QP_STATE   = 0x10,
	/* Bad segment parameters (Address/Size): */
	CMD_STAT_BAD_SEG_PARAM	= 0x20,
	/* Memory Region has Memory Windows bound to: */
	CMD_STAT_REG_BOUND	= 0x21,
	/* HCA local attached memory not present: */
	CMD_STAT_LAM_NOT_PRE	= 0x22,
	/* Bad management packet (silently discarded): */
	CMD_STAT_BAD_PKT	= 0x30,
	/* More outstanding CQEs in CQ than new CQ size: */
	CMD_STAT_BAD_SIZE	= 0x40,
	/* Multi Function device support required: */
	CMD_STAT_MULTI_FUNC_REQ	= 0x50,
};

enum {
	HCR_IN_PARAM_OFFSET	= 0x00,
	HCR_IN_MODIFIER_OFFSET	= 0x08,
	HCR_OUT_PARAM_OFFSET	= 0x0c,
	HCR_TOKEN_OFFSET	= 0x14,
	HCR_STATUS_OFFSET	= 0x18,

	HCR_OPMOD_SHIFT		= 12,
	HCR_T_BIT		= 21,
	HCR_E_BIT		= 22,
	HCR_GO_BIT		= 23
};

enum {
	GO_BIT_TIMEOUT_MSECS	= 10000
};

enum mlx4_vlan_transition {
	MLX4_VLAN_TRANSITION_VST_VST = 0,
	MLX4_VLAN_TRANSITION_VST_VGT = 1,
	MLX4_VLAN_TRANSITION_VGT_VST = 2,
	MLX4_VLAN_TRANSITION_VGT_VGT = 3,
};


struct mlx4_cmd_context {
	struct completion	done;
	int			result;
	int			next;
	u64			out_param;
	u16			token;
	u8			fw_status;
};

static int mlx4_master_process_vhcr(struct mlx4_dev *dev, int slave,
				    struct mlx4_vhcr_cmd *in_vhcr);

static int mlx4_status_to_errno(u8 status)
{
	static const int trans_table[] = {
		[CMD_STAT_INTERNAL_ERR]	  = -EIO,
		[CMD_STAT_BAD_OP]	  = -EPERM,
		[CMD_STAT_BAD_PARAM]	  = -EINVAL,
		[CMD_STAT_BAD_SYS_STATE]  = -ENXIO,
		[CMD_STAT_BAD_RESOURCE]	  = -EBADF,
		[CMD_STAT_RESOURCE_BUSY]  = -EBUSY,
		[CMD_STAT_EXCEED_LIM]	  = -ENOMEM,
		[CMD_STAT_BAD_RES_STATE]  = -EBADF,
		[CMD_STAT_BAD_INDEX]	  = -EBADF,
		[CMD_STAT_BAD_NVMEM]	  = -EFAULT,
		[CMD_STAT_ICM_ERROR]	  = -ENFILE,
		[CMD_STAT_BAD_QP_STATE]   = -EINVAL,
		[CMD_STAT_BAD_SEG_PARAM]  = -EFAULT,
		[CMD_STAT_REG_BOUND]	  = -EBUSY,
		[CMD_STAT_LAM_NOT_PRE]	  = -EAGAIN,
		[CMD_STAT_BAD_PKT]	  = -EINVAL,
		[CMD_STAT_BAD_SIZE]	  = -ENOMEM,
		[CMD_STAT_MULTI_FUNC_REQ] = -EACCES,
	};

	if (status >= ARRAY_SIZE(trans_table) ||
	    (status != CMD_STAT_OK && trans_table[status] == 0))
		return -EIO;

	return trans_table[status];
}

static u8 mlx4_errno_to_status(int errno)
{
	switch (errno) {
	case -EPERM:
		return CMD_STAT_BAD_OP;
	case -EINVAL:
		return CMD_STAT_BAD_PARAM;
	case -ENXIO:
		return CMD_STAT_BAD_SYS_STATE;
	case -EBUSY:
		return CMD_STAT_RESOURCE_BUSY;
	case -ENOMEM:
		return CMD_STAT_EXCEED_LIM;
	case -ENFILE:
		return CMD_STAT_ICM_ERROR;
	default:
		return CMD_STAT_INTERNAL_ERR;
	}
}

static int mlx4_internal_err_ret_value(struct mlx4_dev *dev, u16 op,
				       u8 op_modifier)
{
	switch (op) {
	case MLX4_CMD_UNMAP_ICM:
	case MLX4_CMD_UNMAP_ICM_AUX:
	case MLX4_CMD_UNMAP_FA:
	case MLX4_CMD_2RST_QP:
	case MLX4_CMD_HW2SW_EQ:
	case MLX4_CMD_HW2SW_CQ:
	case MLX4_CMD_HW2SW_SRQ:
	case MLX4_CMD_HW2SW_MPT:
	case MLX4_CMD_CLOSE_HCA:
	case MLX4_QP_FLOW_STEERING_DETACH:
	case MLX4_CMD_FREE_RES:
	case MLX4_CMD_CLOSE_PORT:
		return CMD_STAT_OK;

	case MLX4_CMD_QP_ATTACH:
		/* On Detach case return success */
		if (op_modifier == 0)
			return CMD_STAT_OK;
		return mlx4_status_to_errno(CMD_STAT_INTERNAL_ERR);

	default:
		return mlx4_status_to_errno(CMD_STAT_INTERNAL_ERR);
	}
}

static int mlx4_closing_cmd_fatal_error(u16 op, u8 fw_status)
{
	/* Any error during the closing commands below is considered fatal */
	if (op == MLX4_CMD_CLOSE_HCA ||
	    op == MLX4_CMD_HW2SW_EQ ||
	    op == MLX4_CMD_HW2SW_CQ ||
	    op == MLX4_CMD_2RST_QP ||
	    op == MLX4_CMD_HW2SW_SRQ ||
	    op == MLX4_CMD_SYNC_TPT ||
	    op == MLX4_CMD_UNMAP_ICM ||
	    op == MLX4_CMD_UNMAP_ICM_AUX ||
	    op == MLX4_CMD_UNMAP_FA)
		return 1;
	/* Error on MLX4_CMD_HW2SW_MPT is fatal except when fw status equals
	  * CMD_STAT_REG_BOUND.
	  * This status indicates that memory region has memory windows bound to it
	  * which may result from invalid user space usage and is not fatal.
	  */
	if (op == MLX4_CMD_HW2SW_MPT && fw_status != CMD_STAT_REG_BOUND)
		return 1;
	return 0;
}

static int mlx4_cmd_reset_flow(struct mlx4_dev *dev, u16 op, u8 op_modifier,
			       int err)
{
	/* Only if reset flow is really active return code is based on
	  * command, otherwise current error code is returned.
	  */
	if (mlx4_internal_err_reset) {
		mlx4_enter_error_state(dev->persist);
		err = mlx4_internal_err_ret_value(dev, op, op_modifier);
	}

	return err;
}

static int comm_pending(struct mlx4_dev *dev)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	u32 status = readl(&priv->mfunc.comm->slave_read);

	return (swab32(status) >> 31) != priv->cmd.comm_toggle;
}

static int mlx4_comm_cmd_post(struct mlx4_dev *dev, u8 cmd, u16 param)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	u32 val;

	/* To avoid writing to unknown addresses after the device state was
	 * changed to internal error and the function was rest,
	 * check the INTERNAL_ERROR flag which is updated under
	 * device_state_mutex lock.
	 */
	mutex_lock(&dev->persist->device_state_mutex);

	if (dev->persist->state & MLX4_DEVICE_STATE_INTERNAL_ERROR) {
		mutex_unlock(&dev->persist->device_state_mutex);
		return -EIO;
	}

	priv->cmd.comm_toggle ^= 1;
	val = param | (cmd << 16) | (priv->cmd.comm_toggle << 31);
	__raw_writel((__force u32) cpu_to_be32(val),
		     &priv->mfunc.comm->slave_write);
	mmiowb();
	mutex_unlock(&dev->persist->device_state_mutex);
	return 0;
}

static int mlx4_comm_cmd_poll(struct mlx4_dev *dev, u8 cmd, u16 param,
		       unsigned long timeout)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	unsigned long end;
	int err = 0;
	int ret_from_pending = 0;

	/* First, verify that the master reports correct status */
	if (comm_pending(dev)) {
		mlx4_warn(dev, "Communication channel is not idle - my toggle is %d (cmd:0x%x)\n",
			  priv->cmd.comm_toggle, cmd);
		return -EAGAIN;
	}

	/* Write command */
	down(&priv->cmd.poll_sem);
	if (mlx4_comm_cmd_post(dev, cmd, param)) {
		/* Only in case the device state is INTERNAL_ERROR,
		 * mlx4_comm_cmd_post returns with an error
		 */
		err = mlx4_status_to_errno(CMD_STAT_INTERNAL_ERR);
		goto out;
	}

	end = msecs_to_jiffies(timeout) + jiffies;
	while (comm_pending(dev) && time_before(jiffies, end))
		cond_resched();
	ret_from_pending = comm_pending(dev);
	if (ret_from_pending) {
		/* check if the slave is trying to boot in the middle of
		 * FLR process. The only non-zero result in the RESET command
		 * is MLX4_DELAY_RESET_SLAVE*/
		if ((MLX4_COMM_CMD_RESET == cmd)) {
			err = MLX4_DELAY_RESET_SLAVE;
			goto out;
		} else {
			mlx4_warn(dev, "Communication channel command 0x%x timed out\n",
				  cmd);
			err = mlx4_status_to_errno(CMD_STAT_INTERNAL_ERR);
		}
	}

	if (err)
		mlx4_enter_error_state(dev->persist);
out:
	up(&priv->cmd.poll_sem);
	return err;
}

static int mlx4_comm_cmd_wait(struct mlx4_dev *dev, u8 vhcr_cmd,
			      u16 param, u16 op, unsigned long timeout)
{
	struct mlx4_cmd *cmd = &mlx4_priv(dev)->cmd;
	struct mlx4_cmd_context *context;
	unsigned long end;
	int err = 0;

	down(&cmd->event_sem);

	spin_lock(&cmd->context_lock);
	BUG_ON(cmd->free_head < 0);
	context = &cmd->context[cmd->free_head];
	context->token += cmd->token_mask + 1;
	cmd->free_head = context->next;
	spin_unlock(&cmd->context_lock);

	reinit_completion(&context->done);

	if (mlx4_comm_cmd_post(dev, vhcr_cmd, param)) {
		/* Only in case the device state is INTERNAL_ERROR,
		 * mlx4_comm_cmd_post returns with an error
		 */
		err = mlx4_status_to_errno(CMD_STAT_INTERNAL_ERR);
		goto out;
	}

	if (!wait_for_completion_timeout(&context->done,
					 msecs_to_jiffies(timeout))) {
		mlx4_warn(dev, "communication channel command 0x%x (op=0x%x) timed out\n",
			  vhcr_cmd, op);
		goto out_reset;
	}

	err = context->result;
	if (err && context->fw_status != CMD_STAT_MULTI_FUNC_REQ) {
		mlx4_err(dev, "command 0x%x failed: fw status = 0x%x\n",
			 vhcr_cmd, context->fw_status);
		if (mlx4_closing_cmd_fatal_error(op, context->fw_status))
			goto out_reset;
	}

	/* wait for comm channel ready
	 * this is necessary for prevention the race
	 * when switching between event to polling mode
	 * Skipping this section in case the device is in FATAL_ERROR state,
	 * In this state, no commands are sent via the comm channel until
	 * the device has returned from reset.
	 */
	if (!(dev->persist->state & MLX4_DEVICE_STATE_INTERNAL_ERROR)) {
		end = msecs_to_jiffies(timeout) + jiffies;
		while (comm_pending(dev) && time_before(jiffies, end))
			cond_resched();
	}
	goto out;

out_reset:
	err = mlx4_status_to_errno(CMD_STAT_INTERNAL_ERR);
	mlx4_enter_error_state(dev->persist);
out:
	spin_lock(&cmd->context_lock);
	context->next = cmd->free_head;
	cmd->free_head = context - cmd->context;
	spin_unlock(&cmd->context_lock);

	up(&cmd->event_sem);
	return err;
}

int mlx4_comm_cmd(struct mlx4_dev *dev, u8 cmd, u16 param,
		  u16 op, unsigned long timeout)
{
	if (dev->persist->state & MLX4_DEVICE_STATE_INTERNAL_ERROR)
		return mlx4_status_to_errno(CMD_STAT_INTERNAL_ERR);

	if (mlx4_priv(dev)->cmd.use_events)
		return mlx4_comm_cmd_wait(dev, cmd, param, op, timeout);
	return mlx4_comm_cmd_poll(dev, cmd, param, timeout);
}

static int cmd_pending(struct mlx4_dev *dev)
{
	u32 status;

	if (pci_channel_offline(dev->persist->pdev))
		return -EIO;

	status = readl(mlx4_priv(dev)->cmd.hcr + HCR_STATUS_OFFSET);

	return (status & swab32(1 << HCR_GO_BIT)) ||
		(mlx4_priv(dev)->cmd.toggle ==
		 !!(status & swab32(1 << HCR_T_BIT)));
}

static int mlx4_cmd_post(struct mlx4_dev *dev, u64 in_param, u64 out_param,
			 u32 in_modifier, u8 op_modifier, u16 op, u16 token,
			 int event)
{
	struct mlx4_cmd *cmd = &mlx4_priv(dev)->cmd;
	u32 __iomem *hcr = cmd->hcr;
	int ret = -EIO;
	unsigned long end;

	mutex_lock(&dev->persist->device_state_mutex);
	/* To avoid writing to unknown addresses after the device state was
	  * changed to internal error and the chip was reset,
	  * check the INTERNAL_ERROR flag which is updated under
	  * device_state_mutex lock.
	  */
	if (pci_channel_offline(dev->persist->pdev) ||
	    (dev->persist->state & MLX4_DEVICE_STATE_INTERNAL_ERROR)) {
		/*
		 * Device is going through error recovery
		 * and cannot accept commands.
		 */
		goto out;
	}

	end = jiffies;
	if (event)
		end += msecs_to_jiffies(GO_BIT_TIMEOUT_MSECS);

	while (cmd_pending(dev)) {
		if (pci_channel_offline(dev->persist->pdev)) {
			/*
			 * Device is going through error recovery
			 * and cannot accept commands.
			 */
			goto out;
		}

		if (time_after_eq(jiffies, end)) {
			mlx4_err(dev, "%s:cmd_pending failed\n", __func__);
			goto out;
		}
		cond_resched();
	}

	/*
	 * We use writel (instead of something like memcpy_toio)
	 * because writes of less than 32 bits to the HCR don't work
	 * (and some architectures such as ia64 implement memcpy_toio
	 * in terms of writeb).
	 */
	__raw_writel((__force u32) cpu_to_be32(in_param >> 32),		  hcr + 0);
	__raw_writel((__force u32) cpu_to_be32(in_param & 0xfffffffful),  hcr + 1);
	__raw_writel((__force u32) cpu_to_be32(in_modifier),		  hcr + 2);
	__raw_writel((__force u32) cpu_to_be32(out_param >> 32),	  hcr + 3);
	__raw_writel((__force u32) cpu_to_be32(out_param & 0xfffffffful), hcr + 4);
	__raw_writel((__force u32) cpu_to_be32(token << 16),		  hcr + 5);

	/* __raw_writel may not order writes. */
	wmb();

	__raw_writel((__force u32) cpu_to_be32((1 << HCR_GO_BIT)		|
					       (cmd->toggle << HCR_T_BIT)	|
					       (event ? (1 << HCR_E_BIT) : 0)	|
					       (op_modifier << HCR_OPMOD_SHIFT) |
					       op), hcr + 6);

	/*
	 * Make sure that our HCR writes don't get mixed in with
	 * writes from another CPU starting a FW command.
	 */
	mmiowb();

	cmd->toggle = cmd->toggle ^ 1;

	ret = 0;

out:
	if (ret)
		mlx4_warn(dev, "Could not post command 0x%x: ret=%d, in_param=0x%llx, in_mod=0x%x, op_mod=0x%x\n",
			  op, ret, in_param, in_modifier, op_modifier);
	mutex_unlock(&dev->persist->device_state_mutex);

	return ret;
}

static int mlx4_slave_cmd(struct mlx4_dev *dev, u64 in_param, u64 *out_param,
			  int out_is_imm, u32 in_modifier, u8 op_modifier,
			  u16 op, unsigned long timeout)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	struct mlx4_vhcr_cmd *vhcr = priv->mfunc.vhcr;
	int ret;

	mutex_lock(&priv->cmd.slave_cmd_mutex);

	vhcr->in_param = cpu_to_be64(in_param);
	vhcr->out_param = out_param ? cpu_to_be64(*out_param) : 0;
	vhcr->in_modifier = cpu_to_be32(in_modifier);
	vhcr->opcode = cpu_to_be16((((u16) op_modifier) << 12) | (op & 0xfff));
	vhcr->token = cpu_to_be16(CMD_POLL_TOKEN);
	vhcr->status = 0;
	vhcr->flags = !!(priv->cmd.use_events) << 6;

	if (mlx4_is_master(dev)) {
		ret = mlx4_master_process_vhcr(dev, dev->caps.function, vhcr);
		if (!ret) {
			if (out_is_imm) {
				if (out_param)
					*out_param =
						be64_to_cpu(vhcr->out_param);
				else {
					mlx4_err(dev, "response expected while output mailbox is NULL for command 0x%x\n",
						 op);
					vhcr->status = CMD_STAT_BAD_PARAM;
				}
			}
			ret = mlx4_status_to_errno(vhcr->status);
		}
		if (ret &&
		    dev->persist->state & MLX4_DEVICE_STATE_INTERNAL_ERROR)
			ret = mlx4_internal_err_ret_value(dev, op, op_modifier);
	} else {
		ret = mlx4_comm_cmd(dev, MLX4_COMM_CMD_VHCR_POST, 0, op,
				    MLX4_COMM_TIME + timeout);
		if (!ret) {
			if (out_is_imm) {
				if (out_param)
					*out_param =
						be64_to_cpu(vhcr->out_param);
				else {
					mlx4_err(dev, "response expected while output mailbox is NULL for command 0x%x\n",
						 op);
					vhcr->status = CMD_STAT_BAD_PARAM;
				}
			}
			ret = mlx4_status_to_errno(vhcr->status);
		} else {
			if (dev->persist->state &
			    MLX4_DEVICE_STATE_INTERNAL_ERROR)
				ret = mlx4_internal_err_ret_value(dev, op,
								  op_modifier);
			else
				mlx4_err(dev, "failed execution of VHCR_POST command opcode 0x%x\n", op);
		}
	}

	mutex_unlock(&priv->cmd.slave_cmd_mutex);
	return ret;
}

static int mlx4_cmd_poll(struct mlx4_dev *dev, u64 in_param, u64 *out_param,
			 int out_is_imm, u32 in_modifier, u8 op_modifier,
			 u16 op, unsigned long timeout)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	void __iomem *hcr = priv->cmd.hcr;
	int err = 0;
	unsigned long end;
	u32 stat;

	down(&priv->cmd.poll_sem);

	if (dev->persist->state & MLX4_DEVICE_STATE_INTERNAL_ERROR) {
		/*
		 * Device is going through error recovery
		 * and cannot accept commands.
		 */
		err = mlx4_internal_err_ret_value(dev, op, op_modifier);
		goto out;
	}

	if (out_is_imm && !out_param) {
		mlx4_err(dev, "response expected while output mailbox is NULL for command 0x%x\n",
			 op);
		err = -EINVAL;
		goto out;
	}

	err = mlx4_cmd_post(dev, in_param, out_param ? *out_param : 0,
			    in_modifier, op_modifier, op, CMD_POLL_TOKEN, 0);
	if (err)
		goto out_reset;

	end = msecs_to_jiffies(timeout) + jiffies;
	while (cmd_pending(dev) && time_before(jiffies, end)) {
		if (pci_channel_offline(dev->persist->pdev)) {
			/*
			 * Device is going through error recovery
			 * and cannot accept commands.
			 */
			err = -EIO;
			goto out_reset;
		}

		if (dev->persist->state & MLX4_DEVICE_STATE_INTERNAL_ERROR) {
			err = mlx4_internal_err_ret_value(dev, op, op_modifier);
			goto out;
		}

		cond_resched();
	}

	if (cmd_pending(dev)) {
		mlx4_warn(dev, "command 0x%x timed out (go bit not cleared)\n",
			  op);
		err = -EIO;
		goto out_reset;
	}

	if (out_is_imm)
		*out_param =
			(u64) be32_to_cpu((__force __be32)
					  __raw_readl(hcr + HCR_OUT_PARAM_OFFSET)) << 32 |
			(u64) be32_to_cpu((__force __be32)
					  __raw_readl(hcr + HCR_OUT_PARAM_OFFSET + 4));
	stat = be32_to_cpu((__force __be32)
			   __raw_readl(hcr + HCR_STATUS_OFFSET)) >> 24;
	err = mlx4_status_to_errno(stat);
	if (err) {
		mlx4_err(dev, "command 0x%x failed: fw status = 0x%x\n",
			 op, stat);
		if (mlx4_closing_cmd_fatal_error(op, stat))
			goto out_reset;
		goto out;
	}

out_reset:
	if (err)
		err = mlx4_cmd_reset_flow(dev, op, op_modifier, err);
out:
	up(&priv->cmd.poll_sem);
	return err;
}

void mlx4_cmd_event(struct mlx4_dev *dev, u16 token, u8 status, u64 out_param)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	struct mlx4_cmd_context *context =
		&priv->cmd.context[token & priv->cmd.token_mask];

	/* previously timed out command completing at long last */
	if (token != context->token)
		return;

	context->fw_status = status;
	context->result    = mlx4_status_to_errno(status);
	context->out_param = out_param;

	complete(&context->done);
}

static int mlx4_cmd_wait(struct mlx4_dev *dev, u64 in_param, u64 *out_param,
			 int out_is_imm, u32 in_modifier, u8 op_modifier,
			 u16 op, unsigned long timeout)
{
	struct mlx4_cmd *cmd = &mlx4_priv(dev)->cmd;
	struct mlx4_cmd_context *context;
	long ret_wait;
	int err = 0;

	down(&cmd->event_sem);

	spin_lock(&cmd->context_lock);
	BUG_ON(cmd->free_head < 0);
	context = &cmd->context[cmd->free_head];
	context->token += cmd->token_mask + 1;
	cmd->free_head = context->next;
	spin_unlock(&cmd->context_lock);

	if (out_is_imm && !out_param) {
		mlx4_err(dev, "response expected while output mailbox is NULL for command 0x%x\n",
			 op);
		err = -EINVAL;
		goto out;
	}

	reinit_completion(&context->done);

	err = mlx4_cmd_post(dev, in_param, out_param ? *out_param : 0,
			    in_modifier, op_modifier, op, context->token, 1);
	if (err)
		goto out_reset;

	if (op == MLX4_CMD_SENSE_PORT) {
		ret_wait =
			wait_for_completion_interruptible_timeout(&context->done,
								  msecs_to_jiffies(timeout));
		if (ret_wait < 0) {
			context->fw_status = 0;
			context->out_param = 0;
			context->result = 0;
		}
	} else {
		ret_wait = (long)wait_for_completion_timeout(&context->done,
							     msecs_to_jiffies(timeout));
	}
	if (!ret_wait) {
		mlx4_warn(dev, "command 0x%x timed out (go bit not cleared)\n",
			  op);
		if (op == MLX4_CMD_NOP) {
			err = -EBUSY;
			goto out;
		} else {
			err = -EIO;
			goto out_reset;
		}
	}

	err = context->result;
	if (err) {
		/* Since we do not want to have this error message always
		 * displayed at driver start when there are ConnectX2 HCAs
		 * on the host, we deprecate the error message for this
		 * specific command/input_mod/opcode_mod/fw-status to be debug.
		 */
		if (op == MLX4_CMD_SET_PORT &&
		    (in_modifier == 1 || in_modifier == 2) &&
		    op_modifier == MLX4_SET_PORT_IB_OPCODE &&
		    context->fw_status == CMD_STAT_BAD_SIZE)
			mlx4_dbg(dev, "command 0x%x failed: fw status = 0x%x\n",
				 op, context->fw_status);
		else
			mlx4_err(dev, "command 0x%x failed: fw status = 0x%x\n",
				 op, context->fw_status);
		if (dev->persist->state & MLX4_DEVICE_STATE_INTERNAL_ERROR)
			err = mlx4_internal_err_ret_value(dev, op, op_modifier);
		else if (mlx4_closing_cmd_fatal_error(op, context->fw_status))
			goto out_reset;

		goto out;
	}

	if (out_is_imm)
		*out_param = context->out_param;

out_reset:
	if (err)
		err = mlx4_cmd_reset_flow(dev, op, op_modifier, err);
out:
	spin_lock(&cmd->context_lock);
	context->next = cmd->free_head;
	cmd->free_head = context - cmd->context;
	spin_unlock(&cmd->context_lock);

	up(&cmd->event_sem);
	return err;
}

int __mlx4_cmd(struct mlx4_dev *dev, u64 in_param, u64 *out_param,
	       int out_is_imm, u32 in_modifier, u8 op_modifier,
	       u16 op, unsigned long timeout, int native)
{
	if (pci_channel_offline(dev->persist->pdev))
		return mlx4_cmd_reset_flow(dev, op, op_modifier, -EIO);

	if (!mlx4_is_mfunc(dev) || (native && mlx4_is_master(dev))) {
		int ret;

		if (dev->persist->state & MLX4_DEVICE_STATE_INTERNAL_ERROR)
			return mlx4_internal_err_ret_value(dev, op,
							  op_modifier);
		down_read(&mlx4_priv(dev)->cmd.switch_sem);
		if (mlx4_priv(dev)->cmd.use_events)
			ret = mlx4_cmd_wait(dev, in_param, out_param,
					    out_is_imm, in_modifier,
					    op_modifier, op, timeout);
		else
			ret = mlx4_cmd_poll(dev, in_param, out_param,
					    out_is_imm, in_modifier,
					    op_modifier, op, timeout);

		up_read(&mlx4_priv(dev)->cmd.switch_sem);
		return ret;
	}
	return mlx4_slave_cmd(dev, in_param, out_param, out_is_imm,
			      in_modifier, op_modifier, op, timeout);
}
EXPORT_SYMBOL_GPL(__mlx4_cmd);


int mlx4_ARM_COMM_CHANNEL(struct mlx4_dev *dev)
{
	return mlx4_cmd(dev, 0, 0, 0, MLX4_CMD_ARM_COMM_CHANNEL,
			MLX4_CMD_TIME_CLASS_B, MLX4_CMD_NATIVE);
}

static int mlx4_ACCESS_MEM(struct mlx4_dev *dev, u64 master_addr,
			   int slave, u64 slave_addr,
			   int size, int is_read)
{
	u64 in_param;
	u64 out_param;

	if ((slave_addr & 0xfff) | (master_addr & 0xfff) |
	    (slave & ~0x7f) | (size & 0xff)) {
		mlx4_err(dev, "Bad access mem params - slave_addr:0x%llx master_addr:0x%llx slave_id:%d size:%d\n",
			 slave_addr, master_addr, slave, size);
		return -EINVAL;
	}

	if (is_read) {
		in_param = (u64) slave | slave_addr;
		out_param = (u64) dev->caps.function | master_addr;
	} else {
		in_param = (u64) dev->caps.function | master_addr;
		out_param = (u64) slave | slave_addr;
	}

	return mlx4_cmd_imm(dev, in_param, &out_param, size, 0,
			    MLX4_CMD_ACCESS_MEM,
			    MLX4_CMD_TIME_CLASS_A, MLX4_CMD_NATIVE);
}

static int query_pkey_block(struct mlx4_dev *dev, u8 port, u16 index, u16 *pkey,
			       struct mlx4_cmd_mailbox *inbox,
			       struct mlx4_cmd_mailbox *outbox)
{
	struct ib_smp *in_mad = (struct ib_smp *)(inbox->buf);
	struct ib_smp *out_mad = (struct ib_smp *)(outbox->buf);
	int err;
	int i;

	if (index & 0x1f)
		return -EINVAL;

	in_mad->attr_mod = cpu_to_be32(index / 32);

	err = mlx4_cmd_box(dev, inbox->dma, outbox->dma, port, 3,
			   MLX4_CMD_MAD_IFC, MLX4_CMD_TIME_CLASS_C,
			   MLX4_CMD_NATIVE);
	if (err)
		return err;

	for (i = 0; i < 32; ++i)
		pkey[i] = be16_to_cpu(((__be16 *) out_mad->data)[i]);

	return err;
}

static int get_full_pkey_table(struct mlx4_dev *dev, u8 port, u16 *table,
			       struct mlx4_cmd_mailbox *inbox,
			       struct mlx4_cmd_mailbox *outbox)
{
	int i;
	int err;

	for (i = 0; i < dev->caps.pkey_table_len[port]; i += 32) {
		err = query_pkey_block(dev, port, i, table + i, inbox, outbox);
		if (err)
			return err;
	}

	return 0;
}
#define PORT_CAPABILITY_LOCATION_IN_SMP 20
#define PORT_STATE_OFFSET 32

static enum ib_port_state vf_port_state(struct mlx4_dev *dev, int port, int vf)
{
	if (mlx4_get_slave_port_state(dev, vf, port) == SLAVE_PORT_UP)
		return IB_PORT_ACTIVE;
	else
		return IB_PORT_DOWN;
}

static int mlx4_MAD_IFC_wrapper(struct mlx4_dev *dev, int slave,
				struct mlx4_vhcr *vhcr,
				struct mlx4_cmd_mailbox *inbox,
				struct mlx4_cmd_mailbox *outbox,
				struct mlx4_cmd_info *cmd)
{
	struct ib_smp *smp = inbox->buf;
	u32 index;
	u8 port, slave_port;
	u8 opcode_modifier;
	u16 *table;
	int err;
	int vidx, pidx;
	int network_view;
	struct mlx4_priv *priv = mlx4_priv(dev);
	struct ib_smp *outsmp = outbox->buf;
	__be16 *outtab = (__be16 *)(outsmp->data);
	__be32 slave_cap_mask;
	__be64 slave_node_guid;

	slave_port = vhcr->in_modifier;
	port = mlx4_slave_convert_port(dev, slave, slave_port);

	/* network-view bit is for driver use only, and should not be passed to FW */
	opcode_modifier = vhcr->op_modifier & ~0x8; /* clear netw view bit */
	network_view = !!(vhcr->op_modifier & 0x8);

	if (smp->base_version == 1 &&
	    smp->mgmt_class == IB_MGMT_CLASS_SUBN_LID_ROUTED &&
	    smp->class_version == 1) {
		/* host view is paravirtualized */
		if (!network_view && smp->method == IB_MGMT_METHOD_GET) {
			if (smp->attr_id == IB_SMP_ATTR_PKEY_TABLE) {
				index = be32_to_cpu(smp->attr_mod);
				if (port < 1 || port > dev->caps.num_ports)
					return -EINVAL;
				table = kcalloc((dev->caps.pkey_table_len[port] / 32) + 1,
						sizeof(*table) * 32, GFP_KERNEL);

				if (!table)
					return -ENOMEM;
				/* need to get the full pkey table because the paravirtualized
				 * pkeys may be scattered among several pkey blocks.
				 */
				err = get_full_pkey_table(dev, port, table, inbox, outbox);
				if (!err) {
					for (vidx = index * 32; vidx < (index + 1) * 32; ++vidx) {
						pidx = priv->virt2phys_pkey[slave][port - 1][vidx];
						outtab[vidx % 32] = cpu_to_be16(table[pidx]);
					}
				}
				kfree(table);
				return err;
			}
			if (smp->attr_id == IB_SMP_ATTR_PORT_INFO) {
				/*get the slave specific caps:*/
				/*do the command */
				smp->attr_mod = cpu_to_be32(port);
				err = mlx4_cmd_box(dev, inbox->dma, outbox->dma,
					    port, opcode_modifier,
					    vhcr->op, MLX4_CMD_TIME_CLASS_C, MLX4_CMD_NATIVE);
				/* modify the response for slaves */
				if (!err && slave != mlx4_master_func_num(dev)) {
					u8 *state = outsmp->data + PORT_STATE_OFFSET;

					*state = (*state & 0xf0) | vf_port_state(dev, port, slave);
					slave_cap_mask = priv->mfunc.master.slave_state[slave].ib_cap_mask[port];
					memcpy(outsmp->data + PORT_CAPABILITY_LOCATION_IN_SMP, &slave_cap_mask, 4);
				}
				return err;
			}
			if (smp->attr_id == IB_SMP_ATTR_GUID_INFO) {
				__be64 guid = mlx4_get_admin_guid(dev, slave,
								  port);

				/* set the PF admin guid to the FW/HW burned
				 * GUID, if it wasn't yet set
				 */
				if (slave == 0 && guid == 0) {
					smp->attr_mod = 0;
					err = mlx4_cmd_box(dev,
							   inbox->dma,
							   outbox->dma,
							   vhcr->in_modifier,
							   opcode_modifier,
							   vhcr->op,
							   MLX4_CMD_TIME_CLASS_C,
							   MLX4_CMD_NATIVE);
					if (err)
						return err;
					mlx4_set_admin_guid(dev,
							    *(__be64 *)outsmp->
							    data, slave, port);
				} else {
					memcpy(outsmp->data, &guid, 8);
				}

				/* clean all other gids */
				memset(outsmp->data + 8, 0, 56);
				return 0;
			}
			if (smp->attr_id == IB_SMP_ATTR_NODE_INFO) {
				err = mlx4_cmd_box(dev, inbox->dma, outbox->dma,
					     port, opcode_modifier,
					     vhcr->op, MLX4_CMD_TIME_CLASS_C, MLX4_CMD_NATIVE);
				if (!err) {
					slave_node_guid =  mlx4_get_slave_node_guid(dev, slave);
					memcpy(outsmp->data + 12, &slave_node_guid, 8);
				}
				return err;
			}
		}
	}

	/* Non-privileged VFs are only allowed "host" view LID-routed 'Get' MADs.
	 * These are the MADs used by ib verbs (such as ib_query_gids).
	 */
	if (slave != mlx4_master_func_num(dev) &&
	    !mlx4_vf_smi_enabled(dev, slave, port)) {
		if (!(smp->mgmt_class == IB_MGMT_CLASS_SUBN_LID_ROUTED &&
		      smp->method == IB_MGMT_METHOD_GET) || network_view) {
			mlx4_err(dev, "Unprivileged slave %d is trying to execute a Subnet MGMT MAD, class 0x%x, method 0x%x, view=%s for attr 0x%x. Rejecting\n",
				 slave, smp->mgmt_class, smp->method,
				 network_view ? "Network" : "Host",
				 be16_to_cpu(smp->attr_id));
			return -EPERM;
		}
	}

	return mlx4_cmd_box(dev, inbox->dma, outbox->dma,
				    vhcr->in_modifier, opcode_modifier,
				    vhcr->op, MLX4_CMD_TIME_CLASS_C, MLX4_CMD_NATIVE);
}

static int mlx4_CMD_EPERM_wrapper(struct mlx4_dev *dev, int slave,
		     struct mlx4_vhcr *vhcr,
		     struct mlx4_cmd_mailbox *inbox,
		     struct mlx4_cmd_mailbox *outbox,
		     struct mlx4_cmd_info *cmd)
{
	return -EPERM;
}

int mlx4_DMA_wrapper(struct mlx4_dev *dev, int slave,
		     struct mlx4_vhcr *vhcr,
		     struct mlx4_cmd_mailbox *inbox,
		     struct mlx4_cmd_mailbox *outbox,
		     struct mlx4_cmd_info *cmd)
{
	u64 in_param;
	u64 out_param;
	int err;

	in_param = cmd->has_inbox ? (u64) inbox->dma : vhcr->in_param;
	out_param = cmd->has_outbox ? (u64) outbox->dma : vhcr->out_param;
	if (cmd->encode_slave_id) {
		in_param &= 0xffffffffffffff00ll;
		in_param |= slave;
	}

	err = __mlx4_cmd(dev, in_param, &out_param, cmd->out_is_imm,
			 vhcr->in_modifier, vhcr->op_modifier, vhcr->op,
			 MLX4_CMD_TIME_CLASS_A, MLX4_CMD_NATIVE);

	if (cmd->out_is_imm)
		vhcr->out_param = out_param;

	return err;
}

static struct mlx4_cmd_info cmd_info[] = {
	{
		.opcode = MLX4_CMD_QUERY_FW,
		.has_inbox = false,
		.has_outbox = true,
		.out_is_imm = false,
		.encode_slave_id = false,
		.verify = NULL,
		.wrapper = mlx4_QUERY_FW_wrapper
	},
	{
		.opcode = MLX4_CMD_QUERY_HCA,
		.has_inbox = false,
		.has_outbox = true,
		.out_is_imm = false,
		.encode_slave_id = false,
		.verify = NULL,
		.wrapper = NULL
	},
	{
		.opcode = MLX4_CMD_QUERY_DEV_CAP,
		.has_inbox = false,
		.has_outbox = true,
		.out_is_imm = false,
		.encode_slave_id = false,
		.verify = NULL,
		.wrapper = mlx4_QUERY_DEV_CAP_wrapper
	},
	{
		.opcode = MLX4_CMD_QUERY_FUNC_CAP,
		.has_inbox = false,
		.has_outbox = true,
		.out_is_imm = false,
		.encode_slave_id = false,
		.verify = NULL,
		.wrapper = mlx4_QUERY_FUNC_CAP_wrapper
	},
	{
		.opcode = MLX4_CMD_QUERY_ADAPTER,
		.has_inbox = false,
		.has_outbox = true,
		.out_is_imm = false,
		.encode_slave_id = false,
		.verify = NULL,
		.wrapper = NULL
	},
	{
		.opcode = MLX4_CMD_INIT_PORT,
		.has_inbox = false,
		.has_outbox = false,
		.out_is_imm = false,
		.encode_slave_id = false,
		.verify = NULL,
		.wrapper = mlx4_INIT_PORT_wrapper
	},
	{
		.opcode = MLX4_CMD_CLOSE_PORT,
		.has_inbox = false,
		.has_outbox = false,
		.out_is_imm  = false,
		.encode_slave_id = false,
		.verify = NULL,
		.wrapper = mlx4_CLOSE_PORT_wrapper
	},
	{
		.opcode = MLX4_CMD_QUERY_PORT,
		.has_inbox = false,
		.has_outbox = true,
		.out_is_imm = false,
		.encode_slave_id = false,
		.verify = NULL,
		.wrapper = mlx4_QUERY_PORT_wrapper
	},
	{
		.opcode = MLX4_CMD_SET_PORT,
		.has_inbox = true,
		.has_outbox = false,
		.out_is_imm = false,
		.encode_slave_id = false,
		.verify = NULL,
		.wrapper = mlx4_SET_PORT_wrapper
	},
	{
		.opcode = MLX4_CMD_MAP_EQ,
		.has_inbox = false,
		.has_outbox = false,
		.out_is_imm = false,
		.encode_slave_id = false,
		.verify = NULL,
		.wrapper = mlx4_MAP_EQ_wrapper
	},
	{
		.opcode = MLX4_CMD_SW2HW_EQ,
		.has_inbox = true,
		.has_outbox = false,
		.out_is_imm = false,
		.encode_slave_id = true,
		.verify = NULL,
		.wrapper = mlx4_SW2HW_EQ_wrapper
	},
	{
		.opcode = MLX4_CMD_HW_HEALTH_CHECK,
		.has_inbox = false,
		.has_outbox = false,
		.out_is_imm = false,
		.encode_slave_id = false,
		.verify = NULL,
		.wrapper = NULL
	},
	{
		.opcode = MLX4_CMD_NOP,
		.has_inbox = false,
		.has_outbox = false,
		.out_is_imm = false,
		.encode_slave_id = false,
		.verify = NULL,
		.wrapper = NULL
	},
	{
		.opcode = MLX4_CMD_CONFIG_DEV,
		.has_inbox = false,
		.has_outbox = true,
		.out_is_imm = false,
		.encode_slave_id = false,
		.verify = NULL,
		.wrapper = mlx4_CONFIG_DEV_wrapper
	},
	{
		.opcode = MLX4_CMD_ALLOC_RES,
		.has_inbox = false,
		.has_outbox = false,
		.out_is_imm = true,
		.encode_slave_id = false,
		.verify = NULL,
		.wrapper = mlx4_ALLOC_RES_wrapper
	},
	{
		.opcode = MLX4_CMD_FREE_RES,
		.has_inbox = false,
		.has_outbox = false,
		.out_is_imm = false,
		.encode_slave_id = false,
		.verify = NULL,
		.wrapper = mlx4_FREE_RES_wrapper
	},
	{
		.opcode = MLX4_CMD_SW2HW_MPT,
		.has_inbox = true,
		.has_outbox = false,
		.out_is_imm = false,
		.encode_slave_id = true,
		.verify = NULL,
		.wrapper = mlx4_SW2HW_MPT_wrapper
	},
	{
		.opcode = MLX4_CMD_QUERY_MPT,
		.has_inbox = false,
		.has_outbox = true,
		.out_is_imm = false,
		.encode_slave_id = false,
		.verify = NULL,
		.wrapper = mlx4_QUERY_MPT_wrapper
	},
	{
		.opcode = MLX4_CMD_HW2SW_MPT,
		.has_inbox = false,
		.has_outbox = false,
		.out_is_imm = false,
		.encode_slave_id = false,
		.verify = NULL,
		.wrapper = mlx4_HW2SW_MPT_wrapper
	},
	{
		.opcode = MLX4_CMD_READ_MTT,
		.has_inbox = false,
		.has_outbox = true,
		.out_is_imm = false,
		.encode_slave_id = false,
		.verify = NULL,
		.wrapper = NULL
	},
	{
		.opcode = MLX4_CMD_WRITE_MTT,
		.has_inbox = true,
		.has_outbox = false,
		.out_is_imm = false,
		.encode_slave_id = false,
		.verify = NULL,
		.wrapper = mlx4_WRITE_MTT_wrapper
	},
	{
		.opcode = MLX4_CMD_SYNC_TPT,
		.has_inbox = true,
		.has_outbox = false,
		.out_is_imm = false,
		.encode_slave_id = false,
		.verify = NULL,
		.wrapper = NULL
	},
	{
		.opcode = MLX4_CMD_HW2SW_EQ,
		.has_inbox = false,
		.has_outbox = false,
		.out_is_imm = false,
		.encode_slave_id = true,
		.verify = NULL,
		.wrapper = mlx4_HW2SW_EQ_wrapper
	},
	{
		.opcode = MLX4_CMD_QUERY_EQ,
		.has_inbox = false,
		.has_outbox = true,
		.out_is_imm = false,
		.encode_slave_id = true,
		.verify = NULL,
		.wrapper = mlx4_QUERY_EQ_wrapper
	},
	{
		.opcode = MLX4_CMD_SW2HW_CQ,
		.has_inbox = true,
		.has_outbox = false,
		.out_is_imm = false,
		.encode_slave_id = true,
		.verify = NULL,
		.wrapper = mlx4_SW2HW_CQ_wrapper
	},
	{
		.opcode = MLX4_CMD_HW2SW_CQ,
		.has_inbox = false,
		.has_outbox = false,
		.out_is_imm = false,
		.encode_slave_id = false,
		.verify = NULL,
		.wrapper = mlx4_HW2SW_CQ_wrapper
	},
	{
		.opcode = MLX4_CMD_QUERY_CQ,
		.has_inbox = false,
		.has_outbox = true,
		.out_is_imm = false,
		.encode_slave_id = false,
		.verify = NULL,
		.wrapper = mlx4_QUERY_CQ_wrapper
	},
	{
		.opcode = MLX4_CMD_MODIFY_CQ,
		.has_inbox = true,
		.has_outbox = false,
		.out_is_imm = true,
		.encode_slave_id = false,
		.verify = NULL,
		.wrapper = mlx4_MODIFY_CQ_wrapper
	},
	{
		.opcode = MLX4_CMD_SW2HW_SRQ,
		.has_inbox = true,
		.has_outbox = false,
		.out_is_imm = false,
		.encode_slave_id = true,
		.verify = NULL,
		.wrapper = mlx4_SW2HW_SRQ_wrapper
	},
	{
		.opcode = MLX4_CMD_HW2SW_SRQ,
		.has_inbox = false,
		.has_outbox = false,
		.out_is_imm = false,
		.encode_slave_id = false,
		.verify = NULL,
		.wrapper = mlx4_HW2SW_SRQ_wrapper
	},
	{
		.opcode = MLX4_CMD_QUERY_SRQ,
		.has_inbox = false,
		.has_outbox = true,
		.out_is_imm = false,
		.encode_slave_id = false,
		.verify = NULL,
		.wrapper = mlx4_QUERY_SRQ_wrapper
	},
	{
		.opcode = MLX4_CMD_ARM_SRQ,
		.has_inbox = false,
		.has_outbox = false,
		.out_is_imm = false,
		.encode_slave_id = false,
		.verify = NULL,
		.wrapper = mlx4_ARM_SRQ_wrapper
	},
	{
		.opcode = MLX4_CMD_RST2INIT_QP,
		.has_inbox = true,
		.has_outbox = false,
		.out_is_imm = false,
		.encode_slave_id = true,
		.verify = NULL,
		.wrapper = mlx4_RST2INIT_QP_wrapper
	},
	{
		.opcode = MLX4_CMD_INIT2INIT_QP,
		.has_inbox = true,
		.has_outbox = false,
		.out_is_imm = false,
		.encode_slave_id = false,
		.verify = NULL,
		.wrapper = mlx4_INIT2INIT_QP_wrapper
	},
	{
		.opcode = MLX4_CMD_INIT2RTR_QP,
		.has_inbox = true,
		.has_outbox = false,
		.out_is_imm = false,
		.encode_slave_id = false,
		.verify = NULL,
		.wrapper = mlx4_INIT2RTR_QP_wrapper
	},
	{
		.opcode = MLX4_CMD_RTR2RTS_QP,
		.has_inbox = true,
		.has_outbox = false,
		.out_is_imm = false,
		.encode_slave_id = false,
		.verify = NULL,
		.wrapper = mlx4_RTR2RTS_QP_wrapper
	},
	{
		.opcode = MLX4_CMD_RTS2RTS_QP,
		.has_inbox = true,
		.has_outbox = false,
		.out_is_imm = false,
		.encode_slave_id = false,
		.verify = NULL,
		.wrapper = mlx4_RTS2RTS_QP_wrapper
	},
	{
		.opcode = MLX4_CMD_SQERR2RTS_QP,
		.has_inbox = true,
		.has_outbox = false,
		.out_is_imm = false,
		.encode_slave_id = false,
		.verify = NULL,
		.wrapper = mlx4_SQERR2RTS_QP_wrapper
	},
	{
		.opcode = MLX4_CMD_2ERR_QP,
		.has_inbox = false,
		.has_outbox = false,
		.out_is_imm = false,
		.encode_slave_id = false,
		.verify = NULL,
		.wrapper = mlx4_GEN_QP_wrapper
	},
	{
		.opcode = MLX4_CMD_RTS2SQD_QP,
		.has_inbox = false,
		.has_outbox = false,
		.out_is_imm = false,
		.encode_slave_id = false,
		.verify = NULL,
		.wrapper = mlx4_GEN_QP_wrapper
	},
	{
		.opcode = MLX4_CMD_SQD2SQD_QP,
		.has_inbox = true,
		.has_outbox = false,
		.out_is_imm = false,
		.encode_slave_id = false,
		.verify = NULL,
		.wrapper = mlx4_SQD2SQD_QP_wrapper
	},
	{
		.opcode = MLX4_CMD_SQD2RTS_QP,
		.has_inbox = true,
		.has_outbox = false,
		.out_is_imm = false,
		.encode_slave_id = false,
		.verify = NULL,
		.wrapper = mlx4_SQD2RTS_QP_wrapper
	},
	{
		.opcode = MLX4_CMD_2RST_QP,
		.has_inbox = false,
		.has_outbox = false,
		.out_is_imm = false,
		.encode_slave_id = false,
		.verify = NULL,
		.wrapper = mlx4_2RST_QP_wrapper
	},
	{
		.opcode = MLX4_CMD_QUERY_QP,
		.has_inbox = false,
		.has_outbox = true,
		.out_is_imm = false,
		.encode_slave_id = false,
		.verify = NULL,
		.wrapper = mlx4_GEN_QP_wrapper
	},
	{
		.opcode = MLX4_CMD_SUSPEND_QP,
		.has_inbox = false,
		.has_outbox = false,
		.out_is_imm = false,
		.encode_slave_id = false,
		.verify = NULL,
		.wrapper = mlx4_GEN_QP_wrapper
	},
	{
		.opcode = MLX4_CMD_UNSUSPEND_QP,
		.has_inbox = false,
		.has_outbox = false,
		.out_is_imm = false,
		.encode_slave_id = false,
		.verify = NULL,
		.wrapper = mlx4_GEN_QP_wrapper
	},
	{
		.opcode = MLX4_CMD_UPDATE_QP,
		.has_inbox = true,
		.has_outbox = false,
		.out_is_imm = false,
		.encode_slave_id = false,
		.verify = NULL,
		.wrapper = mlx4_UPDATE_QP_wrapper
	},
	{
		.opcode = MLX4_CMD_GET_OP_REQ,
		.has_inbox = false,
		.has_outbox = false,
		.out_is_imm = false,
		.encode_slave_id = false,
		.verify = NULL,
		.wrapper = mlx4_CMD_EPERM_wrapper,
	},
	{
		.opcode = MLX4_CMD_ALLOCATE_VPP,
		.has_inbox = false,
		.has_outbox = true,
		.out_is_imm = false,
		.encode_slave_id = false,
		.verify = NULL,
		.wrapper = mlx4_CMD_EPERM_wrapper,
	},
	{
		.opcode = MLX4_CMD_SET_VPORT_QOS,
		.has_inbox = false,
		.has_outbox = true,
		.out_is_imm = false,
		.encode_slave_id = false,
		.verify = NULL,
		.wrapper = mlx4_CMD_EPERM_wrapper,
	},
	{
		.opcode = MLX4_CMD_CONF_SPECIAL_QP,
		.has_inbox = false,
		.has_outbox = false,
		.out_is_imm = false,
		.encode_slave_id = false,
		.verify = NULL, /* XXX verify: only demux can do this */
		.wrapper = NULL
	},
	{
		.opcode = MLX4_CMD_MAD_IFC,
		.has_inbox = true,
		.has_outbox = true,
		.out_is_imm = false,
		.encode_slave_id = false,
		.verify = NULL,
		.wrapper = mlx4_MAD_IFC_wrapper
	},
	{
		.opcode = MLX4_CMD_MAD_DEMUX,
		.has_inbox = false,
		.has_outbox = false,
		.out_is_imm = false,
		.encode_slave_id = false,
		.verify = NULL,
		.wrapper = mlx4_CMD_EPERM_wrapper
	},
	{
		.opcode = MLX4_CMD_QUERY_IF_STAT,
		.has_inbox = false,
		.has_outbox = true,
		.out_is_imm = false,
		.encode_slave_id = false,
		.verify = NULL,
		.wrapper = mlx4_QUERY_IF_STAT_wrapper
	},
	{
		.opcode = MLX4_CMD_ACCESS_REG,
		.has_inbox = true,
		.has_outbox = true,
		.out_is_imm = false,
		.encode_slave_id = false,
		.verify = NULL,
		.wrapper = mlx4_ACCESS_REG_wrapper,
	},
	{
		.opcode = MLX4_CMD_CONGESTION_CTRL_OPCODE,
		.has_inbox = false,
		.has_outbox = false,
		.out_is_imm = false,
		.encode_slave_id = false,
		.verify = NULL,
		.wrapper = mlx4_CMD_EPERM_wrapper,
	},
	/* Native multicast commands are not available for guests */
	{
		.opcode = MLX4_CMD_QP_ATTACH,
		.has_inbox = true,
		.has_outbox = false,
		.out_is_imm = false,
		.encode_slave_id = false,
		.verify = NULL,
		.wrapper = mlx4_QP_ATTACH_wrapper
	},
	{
		.opcode = MLX4_CMD_PROMISC,
		.has_inbox = false,
		.has_outbox = false,
		.out_is_imm = false,
		.encode_slave_id = false,
		.verify = NULL,
		.wrapper = mlx4_PROMISC_wrapper
	},
	/* Ethernet specific commands */
	{
		.opcode = MLX4_CMD_SET_VLAN_FLTR,
		.has_inbox = true,
		.has_outbox = false,
		.out_is_imm = false,
		.encode_slave_id = false,
		.verify = NULL,
		.wrapper = mlx4_SET_VLAN_FLTR_wrapper
	},
	{
		.opcode = MLX4_CMD_SET_MCAST_FLTR,
		.has_inbox = false,
		.has_outbox = false,
		.out_is_imm = false,
		.encode_slave_id = false,
		.verify = NULL,
		.wrapper = mlx4_SET_MCAST_FLTR_wrapper
	},
	{
		.opcode = MLX4_CMD_DUMP_ETH_STATS,
		.has_inbox = false,
		.has_outbox = true,
		.out_is_imm = false,
		.encode_slave_id = false,
		.verify = NULL,
		.wrapper = mlx4_DUMP_ETH_STATS_wrapper
	},
	{
		.opcode = MLX4_CMD_INFORM_FLR_DONE,
		.has_inbox = false,
		.has_outbox = false,
		.out_is_imm = false,
		.encode_slave_id = false,
		.verify = NULL,
		.wrapper = NULL
	},
	/* flow steering commands */
	{
		.opcode = MLX4_QP_FLOW_STEERING_ATTACH,
		.has_inbox = true,
		.has_outbox = false,
		.out_is_imm = true,
		.encode_slave_id = false,
		.verify = NULL,
		.wrapper = mlx4_QP_FLOW_STEERING_ATTACH_wrapper
	},
	{
		.opcode = MLX4_QP_FLOW_STEERING_DETACH,
		.has_inbox = false,
		.has_outbox = false,
		.out_is_imm = false,
		.encode_slave_id = false,
		.verify = NULL,
		.wrapper = mlx4_QP_FLOW_STEERING_DETACH_wrapper
	},
	{
		.opcode = MLX4_FLOW_STEERING_IB_UC_QP_RANGE,
		.has_inbox = false,
		.has_outbox = false,
		.out_is_imm = false,
		.encode_slave_id = false,
		.verify = NULL,
		.wrapper = mlx4_CMD_EPERM_wrapper
	},
	{
		.opcode = MLX4_CMD_VIRT_PORT_MAP,
		.has_inbox = false,
		.has_outbox = false,
		.out_is_imm = false,
		.encode_slave_id = false,
		.verify = NULL,
		.wrapper = mlx4_CMD_EPERM_wrapper
	},
};

static int mlx4_master_process_vhcr(struct mlx4_dev *dev, int slave,
				    struct mlx4_vhcr_cmd *in_vhcr)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	struct mlx4_cmd_info *cmd = NULL;
	struct mlx4_vhcr_cmd *vhcr_cmd = in_vhcr ? in_vhcr : priv->mfunc.vhcr;
	struct mlx4_vhcr *vhcr;
	struct mlx4_cmd_mailbox *inbox = NULL;
	struct mlx4_cmd_mailbox *outbox = NULL;
	u64 in_param;
	u64 out_param;
	int ret = 0;
	int i;
	int err = 0;

	/* Create sw representation of Virtual HCR */
	vhcr = kzalloc(sizeof(struct mlx4_vhcr), GFP_KERNEL);
	if (!vhcr)
		return -ENOMEM;

	/* DMA in the vHCR */
	if (!in_vhcr) {
		ret = mlx4_ACCESS_MEM(dev, priv->mfunc.vhcr_dma, slave,
				      priv->mfunc.master.slave_state[slave].vhcr_dma,
				      ALIGN(sizeof(struct mlx4_vhcr_cmd),
					    MLX4_ACCESS_MEM_ALIGN), 1);
		if (ret) {
			if (!(dev->persist->state &
			    MLX4_DEVICE_STATE_INTERNAL_ERROR))
				mlx4_err(dev, "%s: Failed reading vhcr ret: 0x%x\n",
					 __func__, ret);
			kfree(vhcr);
			return ret;
		}
	}

	/* Fill SW VHCR fields */
	vhcr->in_param = be64_to_cpu(vhcr_cmd->in_param);
	vhcr->out_param = be64_to_cpu(vhcr_cmd->out_param);
	vhcr->in_modifier = be32_to_cpu(vhcr_cmd->in_modifier);
	vhcr->token = be16_to_cpu(vhcr_cmd->token);
	vhcr->op = be16_to_cpu(vhcr_cmd->opcode) & 0xfff;
	vhcr->op_modifier = (u8) (be16_to_cpu(vhcr_cmd->opcode) >> 12);
	vhcr->e_bit = vhcr_cmd->flags & (1 << 6);

	/* Lookup command */
	for (i = 0; i < ARRAY_SIZE(cmd_info); ++i) {
		if (vhcr->op == cmd_info[i].opcode) {
			cmd = &cmd_info[i];
			break;
		}
	}
	if (!cmd) {
		mlx4_err(dev, "Unknown command:0x%x accepted from slave:%d\n",
			 vhcr->op, slave);
		vhcr_cmd->status = CMD_STAT_BAD_PARAM;
		goto out_status;
	}

	/* Read inbox */
	if (cmd->has_inbox) {
		vhcr->in_param &= INBOX_MASK;
		inbox = mlx4_alloc_cmd_mailbox(dev);
		if (IS_ERR(inbox)) {
			vhcr_cmd->status = CMD_STAT_BAD_SIZE;
			inbox = NULL;
			goto out_status;
		}

		ret = mlx4_ACCESS_MEM(dev, inbox->dma, slave,
				      vhcr->in_param,
				      MLX4_MAILBOX_SIZE, 1);
		if (ret) {
			if (!(dev->persist->state &
			    MLX4_DEVICE_STATE_INTERNAL_ERROR))
				mlx4_err(dev, "%s: Failed reading inbox (cmd:0x%x)\n",
					 __func__, cmd->opcode);
			vhcr_cmd->status = CMD_STAT_INTERNAL_ERR;
			goto out_status;
		}
	}

	/* Apply permission and bound checks if applicable */
	if (cmd->verify && cmd->verify(dev, slave, vhcr, inbox)) {
		mlx4_warn(dev, "Command:0x%x from slave: %d failed protection checks for resource_id:%d\n",
			  vhcr->op, slave, vhcr->in_modifier);
		vhcr_cmd->status = CMD_STAT_BAD_OP;
		goto out_status;
	}

	/* Allocate outbox */
	if (cmd->has_outbox) {
		outbox = mlx4_alloc_cmd_mailbox(dev);
		if (IS_ERR(outbox)) {
			vhcr_cmd->status = CMD_STAT_BAD_SIZE;
			outbox = NULL;
			goto out_status;
		}
	}

	/* Execute the command! */
	if (cmd->wrapper) {
		err = cmd->wrapper(dev, slave, vhcr, inbox, outbox,
				   cmd);
		if (cmd->out_is_imm)
			vhcr_cmd->out_param = cpu_to_be64(vhcr->out_param);
	} else {
		in_param = cmd->has_inbox ? (u64) inbox->dma :
			vhcr->in_param;
		out_param = cmd->has_outbox ? (u64) outbox->dma :
			vhcr->out_param;
		err = __mlx4_cmd(dev, in_param, &out_param,
				 cmd->out_is_imm, vhcr->in_modifier,
				 vhcr->op_modifier, vhcr->op,
				 MLX4_CMD_TIME_CLASS_A,
				 MLX4_CMD_NATIVE);

		if (cmd->out_is_imm) {
			vhcr->out_param = out_param;
			vhcr_cmd->out_param = cpu_to_be64(vhcr->out_param);
		}
	}

	if (err) {
		if (!(dev->persist->state & MLX4_DEVICE_STATE_INTERNAL_ERROR))
			mlx4_warn(dev, "vhcr command:0x%x slave:%d failed with error:%d, status %d\n",
				  vhcr->op, slave, vhcr->errno, err);
		vhcr_cmd->status = mlx4_errno_to_status(err);
		goto out_status;
	}


	/* Write outbox if command completed successfully */
	if (cmd->has_outbox && !vhcr_cmd->status) {
		ret = mlx4_ACCESS_MEM(dev, outbox->dma, slave,
				      vhcr->out_param,
				      MLX4_MAILBOX_SIZE, MLX4_CMD_WRAPPED);
		if (ret) {
			/* If we failed to write back the outbox after the
			 *command was successfully executed, we must fail this
			 * slave, as it is now in undefined state */
			if (!(dev->persist->state &
			    MLX4_DEVICE_STATE_INTERNAL_ERROR))
				mlx4_err(dev, "%s:Failed writing outbox\n", __func__);
			goto out;
		}
	}

out_status:
	/* DMA back vhcr result */
	if (!in_vhcr) {
		ret = mlx4_ACCESS_MEM(dev, priv->mfunc.vhcr_dma, slave,
				      priv->mfunc.master.slave_state[slave].vhcr_dma,
				      ALIGN(sizeof(struct mlx4_vhcr),
					    MLX4_ACCESS_MEM_ALIGN),
				      MLX4_CMD_WRAPPED);
		if (ret)
			mlx4_err(dev, "%s:Failed writing vhcr result\n",
				 __func__);
		else if (vhcr->e_bit &&
			 mlx4_GEN_EQE(dev, slave, &priv->mfunc.master.cmd_eqe))
				mlx4_warn(dev, "Failed to generate command completion eqe for slave %d\n",
					  slave);
	}

out:
	kfree(vhcr);
	mlx4_free_cmd_mailbox(dev, inbox);
	mlx4_free_cmd_mailbox(dev, outbox);
	return ret;
}

static int mlx4_master_immediate_activate_vlan_qos(struct mlx4_priv *priv,
					    int slave, int port)
{
	struct mlx4_vport_oper_state *vp_oper;
	struct mlx4_vport_state *vp_admin;
	struct mlx4_vf_immed_vlan_work *work;
	struct mlx4_dev *dev = &(priv->dev);
	int err;
	int admin_vlan_ix = NO_INDX;

	vp_oper = &priv->mfunc.master.vf_oper[slave].vport[port];
	vp_admin = &priv->mfunc.master.vf_admin[slave].vport[port];

	if (vp_oper->state.default_vlan == vp_admin->default_vlan &&
	    vp_oper->state.default_qos == vp_admin->default_qos &&
	    vp_oper->state.vlan_proto == vp_admin->vlan_proto &&
	    vp_oper->state.link_state == vp_admin->link_state &&
	    vp_oper->state.qos_vport == vp_admin->qos_vport)
		return 0;

	if (!(priv->mfunc.master.slave_state[slave].active &&
	      dev->caps.flags2 & MLX4_DEV_CAP_FLAG2_UPDATE_QP)) {
		/* even if the UPDATE_QP command isn't supported, we still want
		 * to set this VF link according to the admin directive
		 */
		vp_oper->state.link_state = vp_admin->link_state;
		return -1;
	}

	mlx4_dbg(dev, "updating immediately admin params slave %d port %d\n",
		 slave, port);
	mlx4_dbg(dev, "vlan %d QoS %d link down %d\n",
		 vp_admin->default_vlan, vp_admin->default_qos,
		 vp_admin->link_state);

	work = kzalloc(sizeof(*work), GFP_KERNEL);
	if (!work)
		return -ENOMEM;

	if (vp_oper->state.default_vlan != vp_admin->default_vlan) {
		if (MLX4_VGT != vp_admin->default_vlan) {
			err = __mlx4_register_vlan(&priv->dev, port,
						   vp_admin->default_vlan,
						   &admin_vlan_ix);
			if (err) {
				kfree(work);
				mlx4_warn(&priv->dev,
					  "No vlan resources slave %d, port %d\n",
					  slave, port);
				return err;
			}
		} else {
			admin_vlan_ix = NO_INDX;
		}
		work->flags |= MLX4_VF_IMMED_VLAN_FLAG_VLAN;
		mlx4_dbg(&priv->dev,
			 "alloc vlan %d idx  %d slave %d port %d\n",
			 (int)(vp_admin->default_vlan),
			 admin_vlan_ix, slave, port);
	}

	/* save original vlan ix and vlan id */
	work->orig_vlan_id = vp_oper->state.default_vlan;
	work->orig_vlan_ix = vp_oper->vlan_idx;

	/* handle new qos */
	if (vp_oper->state.default_qos != vp_admin->default_qos)
		work->flags |= MLX4_VF_IMMED_VLAN_FLAG_QOS;

	if (work->flags & MLX4_VF_IMMED_VLAN_FLAG_VLAN)
		vp_oper->vlan_idx = admin_vlan_ix;

	vp_oper->state.default_vlan = vp_admin->default_vlan;
	vp_oper->state.default_qos = vp_admin->default_qos;
	vp_oper->state.vlan_proto = vp_admin->vlan_proto;
	vp_oper->state.link_state = vp_admin->link_state;
	vp_oper->state.qos_vport = vp_admin->qos_vport;

	if (vp_admin->link_state == IFLA_VF_LINK_STATE_DISABLE)
		work->flags |= MLX4_VF_IMMED_VLAN_FLAG_LINK_DISABLE;

	/* iterate over QPs owned by this slave, using UPDATE_QP */
	work->port = port;
	work->slave = slave;
	work->qos = vp_oper->state.default_qos;
	work->qos_vport = vp_oper->state.qos_vport;
	work->vlan_id = vp_oper->state.default_vlan;
	work->vlan_ix = vp_oper->vlan_idx;
	work->vlan_proto = vp_oper->state.vlan_proto;
	work->priv = priv;
	INIT_WORK(&work->work, mlx4_vf_immed_vlan_work_handler);
	queue_work(priv->mfunc.master.comm_wq, &work->work);

	return 0;
}

static void mlx4_set_default_port_qos(struct mlx4_dev *dev, int port)
{
	struct mlx4_qos_manager *port_qos_ctl;
	struct mlx4_priv *priv = mlx4_priv(dev);

	port_qos_ctl = &priv->mfunc.master.qos_ctl[port];
	bitmap_zero(port_qos_ctl->priority_bm, MLX4_NUM_UP);

	/* Enable only default prio at PF init routine */
	set_bit(MLX4_DEFAULT_QOS_PRIO, port_qos_ctl->priority_bm);
}

static void mlx4_allocate_port_vpps(struct mlx4_dev *dev, int port)
{
	int i;
	int err;
	int num_vfs;
	u16 availible_vpp;
	u8 vpp_param[MLX4_NUM_UP];
	struct mlx4_qos_manager *port_qos;
	struct mlx4_priv *priv = mlx4_priv(dev);

	err = mlx4_ALLOCATE_VPP_get(dev, port, &availible_vpp, vpp_param);
	if (err) {
		mlx4_info(dev, "Failed query availible VPPs\n");
		return;
	}

	port_qos = &priv->mfunc.master.qos_ctl[port];
	num_vfs = (availible_vpp /
		   bitmap_weight(port_qos->priority_bm, MLX4_NUM_UP));

	for (i = 0; i < MLX4_NUM_UP; i++) {
		if (test_bit(i, port_qos->priority_bm))
			vpp_param[i] = num_vfs;
	}

	err = mlx4_ALLOCATE_VPP_set(dev, port, vpp_param);
	if (err) {
		mlx4_info(dev, "Failed allocating VPPs\n");
		return;
	}

	/* Query actual allocated VPP, just to make sure */
	err = mlx4_ALLOCATE_VPP_get(dev, port, &availible_vpp, vpp_param);
	if (err) {
		mlx4_info(dev, "Failed query availible VPPs\n");
		return;
	}

	port_qos->num_of_qos_vfs = num_vfs;
	mlx4_dbg(dev, "Port %d Availible VPPs %d\n", port, availible_vpp);

	for (i = 0; i < MLX4_NUM_UP; i++)
		mlx4_dbg(dev, "Port %d UP %d Allocated %d VPPs\n", port, i,
			 vpp_param[i]);
}

static int mlx4_master_activate_admin_state(struct mlx4_priv *priv, int slave)
{
	int port, err;
	struct mlx4_vport_state *vp_admin;
	struct mlx4_vport_oper_state *vp_oper;
	struct mlx4_slave_state *slave_state =
		&priv->mfunc.master.slave_state[slave];
	struct mlx4_active_ports actv_ports = mlx4_get_active_ports(
			&priv->dev, slave);
	int min_port = find_first_bit(actv_ports.ports,
				      priv->dev.caps.num_ports) + 1;
	int max_port = min_port - 1 +
		bitmap_weight(actv_ports.ports, priv->dev.caps.num_ports);

	for (port = min_port; port <= max_port; port++) {
		if (!test_bit(port - 1, actv_ports.ports))
			continue;
		priv->mfunc.master.vf_oper[slave].smi_enabled[port] =
			priv->mfunc.master.vf_admin[slave].enable_smi[port];
		vp_oper = &priv->mfunc.master.vf_oper[slave].vport[port];
		vp_admin = &priv->mfunc.master.vf_admin[slave].vport[port];
		if (vp_admin->vlan_proto != htons(ETH_P_8021AD) ||
		    slave_state->vst_qinq_supported) {
			vp_oper->state.vlan_proto   = vp_admin->vlan_proto;
			vp_oper->state.default_vlan = vp_admin->default_vlan;
			vp_oper->state.default_qos  = vp_admin->default_qos;
		}
		vp_oper->state.link_state = vp_admin->link_state;
		vp_oper->state.mac        = vp_admin->mac;
		vp_oper->state.spoofchk   = vp_admin->spoofchk;
		vp_oper->state.tx_rate    = vp_admin->tx_rate;
		vp_oper->state.qos_vport  = vp_admin->qos_vport;
		vp_oper->state.guid       = vp_admin->guid;

		if (MLX4_VGT != vp_admin->default_vlan) {
			err = __mlx4_register_vlan(&priv->dev, port,
						   vp_admin->default_vlan, &(vp_oper->vlan_idx));
			if (err) {
				vp_oper->vlan_idx = NO_INDX;
				vp_oper->state.default_vlan = MLX4_VGT;
				vp_oper->state.vlan_proto = htons(ETH_P_8021Q);
				mlx4_warn(&priv->dev,
					  "No vlan resources slave %d, port %d\n",
					  slave, port);
				return err;
			}
			mlx4_dbg(&priv->dev, "alloc vlan %d idx  %d slave %d port %d\n",
				 (int)(vp_oper->state.default_vlan),
				 vp_oper->vlan_idx, slave, port);
		}
		if (vp_admin->spoofchk) {
			vp_oper->mac_idx = __mlx4_register_mac(&priv->dev,
							       port,
							       vp_admin->mac);
			if (0 > vp_oper->mac_idx) {
				err = vp_oper->mac_idx;
				vp_oper->mac_idx = NO_INDX;
				mlx4_warn(&priv->dev,
					  "No mac resources slave %d, port %d\n",
					  slave, port);
				return err;
			}
			mlx4_dbg(&priv->dev, "alloc mac %llx idx  %d slave %d port %d\n",
				 vp_oper->state.mac, vp_oper->mac_idx, slave, port);
		}
	}
	return 0;
}

static void mlx4_master_deactivate_admin_state(struct mlx4_priv *priv, int slave)
{
	int port;
	struct mlx4_vport_oper_state *vp_oper;
	struct mlx4_active_ports actv_ports = mlx4_get_active_ports(
			&priv->dev, slave);
	int min_port = find_first_bit(actv_ports.ports,
				      priv->dev.caps.num_ports) + 1;
	int max_port = min_port - 1 +
		bitmap_weight(actv_ports.ports, priv->dev.caps.num_ports);


	for (port = min_port; port <= max_port; port++) {
		if (!test_bit(port - 1, actv_ports.ports))
			continue;
		priv->mfunc.master.vf_oper[slave].smi_enabled[port] =
			MLX4_VF_SMI_DISABLED;
		vp_oper = &priv->mfunc.master.vf_oper[slave].vport[port];
		if (NO_INDX != vp_oper->vlan_idx) {
			__mlx4_unregister_vlan(&priv->dev,
					       port, vp_oper->state.default_vlan);
			vp_oper->vlan_idx = NO_INDX;
		}
		if (NO_INDX != vp_oper->mac_idx) {
			__mlx4_unregister_mac(&priv->dev, port, vp_oper->state.mac);
			vp_oper->mac_idx = NO_INDX;
		}
	}
	return;
}

static void mlx4_master_do_cmd(struct mlx4_dev *dev, int slave, u8 cmd,
			       u16 param, u8 toggle)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	struct mlx4_slave_state *slave_state = priv->mfunc.master.slave_state;
	u32 reply;
	u8 is_going_down = 0;
	int i;
	unsigned long flags;

	slave_state[slave].comm_toggle ^= 1;
	reply = (u32) slave_state[slave].comm_toggle << 31;
	if (toggle != slave_state[slave].comm_toggle) {
		mlx4_warn(dev, "Incorrect toggle %d from slave %d. *** MASTER STATE COMPROMISED ***\n",
			  toggle, slave);
		goto reset_slave;
	}
	if (cmd == MLX4_COMM_CMD_RESET) {
		mlx4_warn(dev, "Received reset from slave:%d\n", slave);
		slave_state[slave].active = false;
		slave_state[slave].old_vlan_api = false;
		slave_state[slave].vst_qinq_supported = false;
		mlx4_master_deactivate_admin_state(priv, slave);
		for (i = 0; i < MLX4_EVENT_TYPES_NUM; ++i) {
				slave_state[slave].event_eq[i].eqn = -1;
				slave_state[slave].event_eq[i].token = 0;
		}
		/*check if we are in the middle of FLR process,
		if so return "retry" status to the slave*/
		if (MLX4_COMM_CMD_FLR == slave_state[slave].last_cmd)
			goto inform_slave_state;

		mlx4_dispatch_event(dev, MLX4_DEV_EVENT_SLAVE_SHUTDOWN, slave);

		/* write the version in the event field */
		reply |= mlx4_comm_get_version();

		goto reset_slave;
	}
	/*command from slave in the middle of FLR*/
	if (cmd != MLX4_COMM_CMD_RESET &&
	    MLX4_COMM_CMD_FLR == slave_state[slave].last_cmd) {
		mlx4_warn(dev, "slave:%d is Trying to run cmd(0x%x) in the middle of FLR\n",
			  slave, cmd);
		return;
	}

	switch (cmd) {
	case MLX4_COMM_CMD_VHCR0:
		if (slave_state[slave].last_cmd != MLX4_COMM_CMD_RESET)
			goto reset_slave;
		slave_state[slave].vhcr_dma = ((u64) param) << 48;
		priv->mfunc.master.slave_state[slave].cookie = 0;
		break;
	case MLX4_COMM_CMD_VHCR1:
		if (slave_state[slave].last_cmd != MLX4_COMM_CMD_VHCR0)
			goto reset_slave;
		slave_state[slave].vhcr_dma |= ((u64) param) << 32;
		break;
	case MLX4_COMM_CMD_VHCR2:
		if (slave_state[slave].last_cmd != MLX4_COMM_CMD_VHCR1)
			goto reset_slave;
		slave_state[slave].vhcr_dma |= ((u64) param) << 16;
		break;
	case MLX4_COMM_CMD_VHCR_EN:
		if (slave_state[slave].last_cmd != MLX4_COMM_CMD_VHCR2)
			goto reset_slave;
		slave_state[slave].vhcr_dma |= param;
		if (mlx4_master_activate_admin_state(priv, slave))
				goto reset_slave;
		slave_state[slave].active = true;
		mlx4_dispatch_event(dev, MLX4_DEV_EVENT_SLAVE_INIT, slave);
		break;
	case MLX4_COMM_CMD_VHCR_POST:
		if ((slave_state[slave].last_cmd != MLX4_COMM_CMD_VHCR_EN) &&
		    (slave_state[slave].last_cmd != MLX4_COMM_CMD_VHCR_POST)) {
			mlx4_warn(dev, "slave:%d is out of sync, cmd=0x%x, last command=0x%x, reset is needed\n",
				  slave, cmd, slave_state[slave].last_cmd);
			goto reset_slave;
		}

		mutex_lock(&priv->cmd.slave_cmd_mutex);
		if (mlx4_master_process_vhcr(dev, slave, NULL)) {
			mlx4_err(dev, "Failed processing vhcr for slave:%d, resetting slave\n",
				 slave);
			mutex_unlock(&priv->cmd.slave_cmd_mutex);
			goto reset_slave;
		}
		mutex_unlock(&priv->cmd.slave_cmd_mutex);
		break;
	default:
		mlx4_warn(dev, "Bad comm cmd:%d from slave:%d\n", cmd, slave);
		goto reset_slave;
	}
	spin_lock_irqsave(&priv->mfunc.master.slave_state_lock, flags);
	if (!slave_state[slave].is_slave_going_down)
		slave_state[slave].last_cmd = cmd;
	else
		is_going_down = 1;
	spin_unlock_irqrestore(&priv->mfunc.master.slave_state_lock, flags);
	if (is_going_down) {
		mlx4_warn(dev, "Slave is going down aborting command(%d) executing from slave:%d\n",
			  cmd, slave);
		return;
	}
	__raw_writel((__force u32) cpu_to_be32(reply),
		     &priv->mfunc.comm[slave].slave_read);
	mmiowb();

	return;

reset_slave:
	/* cleanup any slave resources */
	if (dev->persist->interface_state & MLX4_INTERFACE_STATE_UP)
		mlx4_delete_all_resources_for_slave(dev, slave);

	if (cmd != MLX4_COMM_CMD_RESET) {
		mlx4_warn(dev, "Turn on internal error to force reset, slave=%d, cmd=0x%x\n",
			  slave, cmd);
		/* Turn on internal error letting slave reset itself immeditaly,
		 * otherwise it might take till timeout on command is passed
		 */
		reply |= ((u32)COMM_CHAN_EVENT_INTERNAL_ERR);
	}

	spin_lock_irqsave(&priv->mfunc.master.slave_state_lock, flags);
	if (!slave_state[slave].is_slave_going_down)
		slave_state[slave].last_cmd = MLX4_COMM_CMD_RESET;
	spin_unlock_irqrestore(&priv->mfunc.master.slave_state_lock, flags);
	/*with slave in the middle of flr, no need to clean resources again.*/
inform_slave_state:
	memset(&slave_state[slave].event_eq, 0,
	       sizeof(struct mlx4_slave_event_eq_info));
	__raw_writel((__force u32) cpu_to_be32(reply),
		     &priv->mfunc.comm[slave].slave_read);
	wmb();
}

/* master command processing */
void mlx4_master_comm_channel(struct work_struct *work)
{
	struct mlx4_mfunc_master_ctx *master =
		container_of(work,
			     struct mlx4_mfunc_master_ctx,
			     comm_work);
	struct mlx4_mfunc *mfunc =
		container_of(master, struct mlx4_mfunc, master);
	struct mlx4_priv *priv =
		container_of(mfunc, struct mlx4_priv, mfunc);
	struct mlx4_dev *dev = &priv->dev;
	__be32 *bit_vec;
	u32 comm_cmd;
	u32 vec;
	int i, j, slave;
	int toggle;
	int served = 0;
	int reported = 0;
	u32 slt;

	bit_vec = master->comm_arm_bit_vector;
	for (i = 0; i < COMM_CHANNEL_BIT_ARRAY_SIZE; i++) {
		vec = be32_to_cpu(bit_vec[i]);
		for (j = 0; j < 32; j++) {
			if (!(vec & (1 << j)))
				continue;
			++reported;
			slave = (i * 32) + j;
			comm_cmd = swab32(readl(
					  &mfunc->comm[slave].slave_write));
			slt = swab32(readl(&mfunc->comm[slave].slave_read))
				     >> 31;
			toggle = comm_cmd >> 31;
			if (toggle != slt) {
				if (master->slave_state[slave].comm_toggle
				    != slt) {
					pr_info("slave %d out of sync. read toggle %d, state toggle %d. Resynching.\n",
						slave, slt,
						master->slave_state[slave].comm_toggle);
					master->slave_state[slave].comm_toggle =
						slt;
				}
				mlx4_master_do_cmd(dev, slave,
						   comm_cmd >> 16 & 0xff,
						   comm_cmd & 0xffff, toggle);
				++served;
			}
		}
	}

	if (reported && reported != served)
		mlx4_warn(dev, "Got command event with bitmask from %d slaves but %d were served\n",
			  reported, served);

	if (mlx4_ARM_COMM_CHANNEL(dev))
		mlx4_warn(dev, "Failed to arm comm channel events\n");
}

static int sync_toggles(struct mlx4_dev *dev)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	u32 wr_toggle;
	u32 rd_toggle;
	unsigned long end;

	wr_toggle = swab32(readl(&priv->mfunc.comm->slave_write));
	if (wr_toggle == 0xffffffff)
		end = jiffies + msecs_to_jiffies(30000);
	else
		end = jiffies + msecs_to_jiffies(5000);

	while (time_before(jiffies, end)) {
		rd_toggle = swab32(readl(&priv->mfunc.comm->slave_read));
		if (wr_toggle == 0xffffffff || rd_toggle == 0xffffffff) {
			/* PCI might be offline */
			msleep(100);
			wr_toggle = swab32(readl(&priv->mfunc.comm->
					   slave_write));
			continue;
		}

		if (rd_toggle >> 31 == wr_toggle >> 31) {
			priv->cmd.comm_toggle = rd_toggle >> 31;
			return 0;
		}

		cond_resched();
	}

	/*
	 * we could reach here if for example the previous VM using this
	 * function misbehaved and left the channel with unsynced state. We
	 * should fix this here and give this VM a chance to use a properly
	 * synced channel
	 */
	mlx4_warn(dev, "recovering from previously mis-behaved VM\n");
	__raw_writel((__force u32) 0, &priv->mfunc.comm->slave_read);
	__raw_writel((__force u32) 0, &priv->mfunc.comm->slave_write);
	priv->cmd.comm_toggle = 0;

	return 0;
}

int mlx4_multi_func_init(struct mlx4_dev *dev)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	struct mlx4_slave_state *s_state;
	int i, j, err, port;

	if (mlx4_is_master(dev))
		priv->mfunc.comm =
		ioremap(pci_resource_start(dev->persist->pdev,
					   priv->fw.comm_bar) +
			priv->fw.comm_base, MLX4_COMM_PAGESIZE);
	else
		priv->mfunc.comm =
		ioremap(pci_resource_start(dev->persist->pdev, 2) +
			MLX4_SLAVE_COMM_BASE, MLX4_COMM_PAGESIZE);
	if (!priv->mfunc.comm) {
		mlx4_err(dev, "Couldn't map communication vector\n");
		goto err_vhcr;
	}

	if (mlx4_is_master(dev)) {
		struct mlx4_vf_oper_state *vf_oper;
		struct mlx4_vf_admin_state *vf_admin;

		priv->mfunc.master.slave_state =
			kzalloc(dev->num_slaves *
				sizeof(struct mlx4_slave_state), GFP_KERNEL);
		if (!priv->mfunc.master.slave_state)
			goto err_comm;

		priv->mfunc.master.vf_admin =
			kzalloc(dev->num_slaves *
				sizeof(struct mlx4_vf_admin_state), GFP_KERNEL);
		if (!priv->mfunc.master.vf_admin)
			goto err_comm_admin;

		priv->mfunc.master.vf_oper =
			kzalloc(dev->num_slaves *
				sizeof(struct mlx4_vf_oper_state), GFP_KERNEL);
		if (!priv->mfunc.master.vf_oper)
			goto err_comm_oper;

		for (i = 0; i < dev->num_slaves; ++i) {
			vf_admin = &priv->mfunc.master.vf_admin[i];
			vf_oper = &priv->mfunc.master.vf_oper[i];
			s_state = &priv->mfunc.master.slave_state[i];
			s_state->last_cmd = MLX4_COMM_CMD_RESET;
			s_state->vst_qinq_supported = false;
			mutex_init(&priv->mfunc.master.gen_eqe_mutex[i]);
			for (j = 0; j < MLX4_EVENT_TYPES_NUM; ++j)
				s_state->event_eq[j].eqn = -1;
			__raw_writel((__force u32) 0,
				     &priv->mfunc.comm[i].slave_write);
			__raw_writel((__force u32) 0,
				     &priv->mfunc.comm[i].slave_read);
			mmiowb();
			for (port = 1; port <= MLX4_MAX_PORTS; port++) {
				struct mlx4_vport_state *admin_vport;
				struct mlx4_vport_state *oper_vport;

				s_state->vlan_filter[port] =
					kzalloc(sizeof(struct mlx4_vlan_fltr),
						GFP_KERNEL);
				if (!s_state->vlan_filter[port]) {
					if (--port)
						kfree(s_state->vlan_filter[port]);
					goto err_slaves;
				}

				admin_vport = &vf_admin->vport[port];
				oper_vport = &vf_oper->vport[port].state;
				INIT_LIST_HEAD(&s_state->mcast_filters[port]);
				admin_vport->default_vlan = MLX4_VGT;
				oper_vport->default_vlan = MLX4_VGT;
				admin_vport->qos_vport =
						MLX4_VPP_DEFAULT_VPORT;
				oper_vport->qos_vport = MLX4_VPP_DEFAULT_VPORT;
				admin_vport->vlan_proto = htons(ETH_P_8021Q);
				oper_vport->vlan_proto = htons(ETH_P_8021Q);
				vf_oper->vport[port].vlan_idx = NO_INDX;
				vf_oper->vport[port].mac_idx = NO_INDX;
				mlx4_set_random_admin_guid(dev, i, port);
			}
			spin_lock_init(&s_state->lock);
		}

		if (dev->caps.flags2 & MLX4_DEV_CAP_FLAG2_QOS_VPP) {
			for (port = 1; port <= dev->caps.num_ports; port++) {
				if (mlx4_is_eth(dev, port)) {
					mlx4_set_default_port_qos(dev, port);
					mlx4_allocate_port_vpps(dev, port);
				}
			}
		}

		memset(&priv->mfunc.master.cmd_eqe, 0, sizeof(struct mlx4_eqe));
		priv->mfunc.master.cmd_eqe.type = MLX4_EVENT_TYPE_CMD;
		INIT_WORK(&priv->mfunc.master.comm_work,
			  mlx4_master_comm_channel);
		INIT_WORK(&priv->mfunc.master.slave_event_work,
			  mlx4_gen_slave_eqe);
		INIT_WORK(&priv->mfunc.master.slave_flr_event_work,
			  mlx4_master_handle_slave_flr);
		spin_lock_init(&priv->mfunc.master.slave_state_lock);
		spin_lock_init(&priv->mfunc.master.slave_eq.event_lock);
		priv->mfunc.master.comm_wq =
			create_singlethread_workqueue("mlx4_comm");
		if (!priv->mfunc.master.comm_wq)
			goto err_slaves;

		if (mlx4_init_resource_tracker(dev))
			goto err_thread;

	} else {
		err = sync_toggles(dev);
		if (err) {
			mlx4_err(dev, "Couldn't sync toggles\n");
			goto err_comm;
		}
	}
	return 0;

err_thread:
	flush_workqueue(priv->mfunc.master.comm_wq);
	destroy_workqueue(priv->mfunc.master.comm_wq);
err_slaves:
	while (i--) {
		for (port = 1; port <= MLX4_MAX_PORTS; port++)
			kfree(priv->mfunc.master.slave_state[i].vlan_filter[port]);
	}
	kfree(priv->mfunc.master.vf_oper);
err_comm_oper:
	kfree(priv->mfunc.master.vf_admin);
err_comm_admin:
	kfree(priv->mfunc.master.slave_state);
err_comm:
	iounmap(priv->mfunc.comm);
err_vhcr:
	dma_free_coherent(&dev->persist->pdev->dev, PAGE_SIZE,
			  priv->mfunc.vhcr,
			  priv->mfunc.vhcr_dma);
	priv->mfunc.vhcr = NULL;
	return -ENOMEM;
}

int mlx4_cmd_init(struct mlx4_dev *dev)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	int flags = 0;

	if (!priv->cmd.initialized) {
		init_rwsem(&priv->cmd.switch_sem);
		mutex_init(&priv->cmd.slave_cmd_mutex);
		sema_init(&priv->cmd.poll_sem, 1);
		priv->cmd.use_events = 0;
		priv->cmd.toggle     = 1;
		priv->cmd.initialized = 1;
		flags |= MLX4_CMD_CLEANUP_STRUCT;
	}

	if (!mlx4_is_slave(dev) && !priv->cmd.hcr) {
		priv->cmd.hcr = ioremap(pci_resource_start(dev->persist->pdev,
					0) + MLX4_HCR_BASE, MLX4_HCR_SIZE);
		if (!priv->cmd.hcr) {
			mlx4_err(dev, "Couldn't map command register\n");
			goto err;
		}
		flags |= MLX4_CMD_CLEANUP_HCR;
	}

	if (mlx4_is_mfunc(dev) && !priv->mfunc.vhcr) {
		priv->mfunc.vhcr = dma_alloc_coherent(&dev->persist->pdev->dev,
						      PAGE_SIZE,
						      &priv->mfunc.vhcr_dma,
						      GFP_KERNEL);
		if (!priv->mfunc.vhcr)
			goto err;

		flags |= MLX4_CMD_CLEANUP_VHCR;
	}

	if (!priv->cmd.pool) {
		priv->cmd.pool = pci_pool_create("mlx4_cmd",
						 dev->persist->pdev,
						 MLX4_MAILBOX_SIZE,
						 MLX4_MAILBOX_SIZE, 0);
		if (!priv->cmd.pool)
			goto err;

		flags |= MLX4_CMD_CLEANUP_POOL;
	}

	return 0;

err:
	mlx4_cmd_cleanup(dev, flags);
	return -ENOMEM;
}

void mlx4_report_internal_err_comm_event(struct mlx4_dev *dev)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	int slave;
	u32 slave_read;

	/* Report an internal error event to all
	 * communication channels.
	 */
	for (slave = 0; slave < dev->num_slaves; slave++) {
		slave_read = swab32(readl(&priv->mfunc.comm[slave].slave_read));
		slave_read |= (u32)COMM_CHAN_EVENT_INTERNAL_ERR;
		__raw_writel((__force u32)cpu_to_be32(slave_read),
			     &priv->mfunc.comm[slave].slave_read);
		/* Make sure that our comm channel write doesn't
		 * get mixed in with writes from another CPU.
		 */
		mmiowb();
	}
}

void mlx4_multi_func_cleanup(struct mlx4_dev *dev)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	int i, port;

	if (mlx4_is_master(dev)) {
		flush_workqueue(priv->mfunc.master.comm_wq);
		destroy_workqueue(priv->mfunc.master.comm_wq);
		for (i = 0; i < dev->num_slaves; i++) {
			for (port = 1; port <= MLX4_MAX_PORTS; port++)
				kfree(priv->mfunc.master.slave_state[i].vlan_filter[port]);
		}
		kfree(priv->mfunc.master.slave_state);
		kfree(priv->mfunc.master.vf_admin);
		kfree(priv->mfunc.master.vf_oper);
		dev->num_slaves = 0;
	}

	iounmap(priv->mfunc.comm);
}

void mlx4_cmd_cleanup(struct mlx4_dev *dev, int cleanup_mask)
{
	struct mlx4_priv *priv = mlx4_priv(dev);

	if (priv->cmd.pool && (cleanup_mask & MLX4_CMD_CLEANUP_POOL)) {
		pci_pool_destroy(priv->cmd.pool);
		priv->cmd.pool = NULL;
	}

	if (!mlx4_is_slave(dev) && priv->cmd.hcr &&
	    (cleanup_mask & MLX4_CMD_CLEANUP_HCR)) {
		iounmap(priv->cmd.hcr);
		priv->cmd.hcr = NULL;
	}
	if (mlx4_is_mfunc(dev) && priv->mfunc.vhcr &&
	    (cleanup_mask & MLX4_CMD_CLEANUP_VHCR)) {
		dma_free_coherent(&dev->persist->pdev->dev, PAGE_SIZE,
				  priv->mfunc.vhcr, priv->mfunc.vhcr_dma);
		priv->mfunc.vhcr = NULL;
	}
	if (priv->cmd.initialized && (cleanup_mask & MLX4_CMD_CLEANUP_STRUCT))
		priv->cmd.initialized = 0;
}

/*
 * Switch to using events to issue FW commands (can only be called
 * after event queue for command events has been initialized).
 */
int mlx4_cmd_use_events(struct mlx4_dev *dev)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	int i;
	int err = 0;

	priv->cmd.context = kmalloc(priv->cmd.max_cmds *
				   sizeof (struct mlx4_cmd_context),
				   GFP_KERNEL);
	if (!priv->cmd.context)
		return -ENOMEM;

	down_write(&priv->cmd.switch_sem);
	for (i = 0; i < priv->cmd.max_cmds; ++i) {
		priv->cmd.context[i].token = i;
		priv->cmd.context[i].next  = i + 1;
		/* To support fatal error flow, initialize all
		 * cmd contexts to allow simulating completions
		 * with complete() at any time.
		 */
		init_completion(&priv->cmd.context[i].done);
	}

	priv->cmd.context[priv->cmd.max_cmds - 1].next = -1;
	priv->cmd.free_head = 0;

	sema_init(&priv->cmd.event_sem, priv->cmd.max_cmds);

	for (priv->cmd.token_mask = 1;
	     priv->cmd.token_mask < priv->cmd.max_cmds;
	     priv->cmd.token_mask <<= 1)
		; /* nothing */
	--priv->cmd.token_mask;

	down(&priv->cmd.poll_sem);
	priv->cmd.use_events = 1;
	up_write(&priv->cmd.switch_sem);

	return err;
}

/*
 * Switch back to polling (used when shutting down the device)
 */
void mlx4_cmd_use_polling(struct mlx4_dev *dev)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	int i;

	down_write(&priv->cmd.switch_sem);
	priv->cmd.use_events = 0;

	for (i = 0; i < priv->cmd.max_cmds; ++i)
		down(&priv->cmd.event_sem);

	kfree(priv->cmd.context);

	up(&priv->cmd.poll_sem);
	up_write(&priv->cmd.switch_sem);
}

struct mlx4_cmd_mailbox *mlx4_alloc_cmd_mailbox(struct mlx4_dev *dev)
{
	struct mlx4_cmd_mailbox *mailbox;

	mailbox = kmalloc(sizeof *mailbox, GFP_KERNEL);
	if (!mailbox)
		return ERR_PTR(-ENOMEM);

	mailbox->buf = pci_pool_alloc(mlx4_priv(dev)->cmd.pool, GFP_KERNEL,
				      &mailbox->dma);
	if (!mailbox->buf) {
		kfree(mailbox);
		return ERR_PTR(-ENOMEM);
	}

	memset(mailbox->buf, 0, MLX4_MAILBOX_SIZE);

	return mailbox;
}
EXPORT_SYMBOL_GPL(mlx4_alloc_cmd_mailbox);

void mlx4_free_cmd_mailbox(struct mlx4_dev *dev,
			   struct mlx4_cmd_mailbox *mailbox)
{
	if (!mailbox)
		return;

	pci_pool_free(mlx4_priv(dev)->cmd.pool, mailbox->buf, mailbox->dma);
	kfree(mailbox);
}
EXPORT_SYMBOL_GPL(mlx4_free_cmd_mailbox);

u32 mlx4_comm_get_version(void)
{
	 return ((u32) CMD_CHAN_IF_REV << 8) | (u32) CMD_CHAN_VER;
}

static int mlx4_get_slave_indx(struct mlx4_dev *dev, int vf)
{
	if ((vf < 0) || (vf >= dev->persist->num_vfs)) {
		mlx4_err(dev, "Bad vf number:%d (number of activated vf: %d)\n",
			 vf, dev->persist->num_vfs);
		return -EINVAL;
	}

	return vf+1;
}

int mlx4_get_vf_indx(struct mlx4_dev *dev, int slave)
{
	if (slave < 1 || slave > dev->persist->num_vfs) {
		mlx4_err(dev,
			 "Bad slave number:%d (number of activated slaves: %lu)\n",
			 slave, dev->num_slaves);
		return -EINVAL;
	}
	return slave - 1;
}

void mlx4_cmd_wake_completions(struct mlx4_dev *dev)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	struct mlx4_cmd_context *context;
	int i;

	spin_lock(&priv->cmd.context_lock);
	if (priv->cmd.context) {
		for (i = 0; i < priv->cmd.max_cmds; ++i) {
			context = &priv->cmd.context[i];
			context->fw_status = CMD_STAT_INTERNAL_ERR;
			context->result    =
				mlx4_status_to_errno(CMD_STAT_INTERNAL_ERR);
			complete(&context->done);
		}
	}
	spin_unlock(&priv->cmd.context_lock);
}

struct mlx4_active_ports mlx4_get_active_ports(struct mlx4_dev *dev, int slave)
{
	struct mlx4_active_ports actv_ports;
	int vf;

	bitmap_zero(actv_ports.ports, MLX4_MAX_PORTS);

	if (slave == 0) {
		bitmap_fill(actv_ports.ports, dev->caps.num_ports);
		return actv_ports;
	}

	vf = mlx4_get_vf_indx(dev, slave);
	if (vf < 0)
		return actv_ports;

	bitmap_set(actv_ports.ports, dev->dev_vfs[vf].min_port - 1,
		   min((int)dev->dev_vfs[mlx4_get_vf_indx(dev, slave)].n_ports,
		   dev->caps.num_ports));

	return actv_ports;
}
EXPORT_SYMBOL_GPL(mlx4_get_active_ports);

int mlx4_slave_convert_port(struct mlx4_dev *dev, int slave, int port)
{
	unsigned n;
	struct mlx4_active_ports actv_ports = mlx4_get_active_ports(dev, slave);
	unsigned m = bitmap_weight(actv_ports.ports, dev->caps.num_ports);

	if (port <= 0 || port > m)
		return -EINVAL;

	n = find_first_bit(actv_ports.ports, dev->caps.num_ports);
	if (port <= n)
		port = n + 1;

	return port;
}
EXPORT_SYMBOL_GPL(mlx4_slave_convert_port);

int mlx4_phys_to_slave_port(struct mlx4_dev *dev, int slave, int port)
{
	struct mlx4_active_ports actv_ports = mlx4_get_active_ports(dev, slave);
	if (test_bit(port - 1, actv_ports.ports))
		return port -
			find_first_bit(actv_ports.ports, dev->caps.num_ports);

	return -1;
}
EXPORT_SYMBOL_GPL(mlx4_phys_to_slave_port);

struct mlx4_slaves_pport mlx4_phys_to_slaves_pport(struct mlx4_dev *dev,
						   int port)
{
	unsigned i;
	struct mlx4_slaves_pport slaves_pport;

	bitmap_zero(slaves_pport.slaves, MLX4_MFUNC_MAX);

	if (port <= 0 || port > dev->caps.num_ports)
		return slaves_pport;

	for (i = 0; i < dev->persist->num_vfs + 1; i++) {
		struct mlx4_active_ports actv_ports =
			mlx4_get_active_ports(dev, i);
		if (test_bit(port - 1, actv_ports.ports))
			set_bit(i, slaves_pport.slaves);
	}

	return slaves_pport;
}
EXPORT_SYMBOL_GPL(mlx4_phys_to_slaves_pport);

struct mlx4_slaves_pport mlx4_phys_to_slaves_pport_actv(
		struct mlx4_dev *dev,
		const struct mlx4_active_ports *crit_ports)
{
	unsigned i;
	struct mlx4_slaves_pport slaves_pport;

	bitmap_zero(slaves_pport.slaves, MLX4_MFUNC_MAX);

	for (i = 0; i < dev->persist->num_vfs + 1; i++) {
		struct mlx4_active_ports actv_ports =
			mlx4_get_active_ports(dev, i);
		if (bitmap_equal(crit_ports->ports, actv_ports.ports,
				 dev->caps.num_ports))
			set_bit(i, slaves_pport.slaves);
	}

	return slaves_pport;
}
EXPORT_SYMBOL_GPL(mlx4_phys_to_slaves_pport_actv);

static int mlx4_slaves_closest_port(struct mlx4_dev *dev, int slave, int port)
{
	struct mlx4_active_ports actv_ports = mlx4_get_active_ports(dev, slave);
	int min_port = find_first_bit(actv_ports.ports, dev->caps.num_ports)
			+ 1;
	int max_port = min_port +
		bitmap_weight(actv_ports.ports, dev->caps.num_ports);

	if (port < min_port)
		port = min_port;
	else if (port >= max_port)
		port = max_port - 1;

	return port;
}

static int mlx4_set_vport_qos(struct mlx4_priv *priv, int slave, int port,
			      int max_tx_rate)
{
	int i;
	int err;
	struct mlx4_qos_manager *port_qos;
	struct mlx4_dev *dev = &priv->dev;
	struct mlx4_vport_qos_param vpp_qos[MLX4_NUM_UP];

	port_qos = &priv->mfunc.master.qos_ctl[port];
	memset(vpp_qos, 0, sizeof(struct mlx4_vport_qos_param) * MLX4_NUM_UP);

	if (slave > port_qos->num_of_qos_vfs) {
		mlx4_info(dev, "No availible VPP resources for this VF\n");
		return -EINVAL;
	}

	/* Query for default QoS values from Vport 0 is needed */
	err = mlx4_SET_VPORT_QOS_get(dev, port, 0, vpp_qos);
	if (err) {
		mlx4_info(dev, "Failed to query Vport 0 QoS values\n");
		return err;
	}

	for (i = 0; i < MLX4_NUM_UP; i++) {
		if (test_bit(i, port_qos->priority_bm) && max_tx_rate) {
			vpp_qos[i].max_avg_bw = max_tx_rate;
			vpp_qos[i].enable = 1;
		} else {
			/* if user supplied tx_rate == 0, meaning no rate limit
			 * configuration is required. so we are leaving the
			 * value of max_avg_bw as queried from Vport 0.
			 */
			vpp_qos[i].enable = 0;
		}
	}

	err = mlx4_SET_VPORT_QOS_set(dev, port, slave, vpp_qos);
	if (err) {
		mlx4_info(dev, "Failed to set Vport %d QoS values\n", slave);
		return err;
	}

	return 0;
}

static bool mlx4_is_vf_vst_and_prio_qos(struct mlx4_dev *dev, int port,
					struct mlx4_vport_state *vf_admin)
{
	struct mlx4_qos_manager *info;
	struct mlx4_priv *priv = mlx4_priv(dev);

	if (!mlx4_is_master(dev) ||
	    !(dev->caps.flags2 & MLX4_DEV_CAP_FLAG2_QOS_VPP))
		return false;

	info = &priv->mfunc.master.qos_ctl[port];

	if (vf_admin->default_vlan != MLX4_VGT &&
	    test_bit(vf_admin->default_qos, info->priority_bm))
		return true;

	return false;
}

static bool mlx4_valid_vf_state_change(struct mlx4_dev *dev, int port,
				       struct mlx4_vport_state *vf_admin,
				       int vlan, int qos)
{
	struct mlx4_vport_state dummy_admin = {0};

	if (!mlx4_is_vf_vst_and_prio_qos(dev, port, vf_admin) ||
	    !vf_admin->tx_rate)
		return true;

	dummy_admin.default_qos = qos;
	dummy_admin.default_vlan = vlan;

	/* VF wants to move to other VST state which is valid with current
	 * rate limit. Either differnt default vlan in VST or other
	 * supported QoS priority. Otherwise we don't allow this change when
	 * the TX rate is still configured.
	 */
	if (mlx4_is_vf_vst_and_prio_qos(dev, port, &dummy_admin))
		return true;

	mlx4_info(dev, "Cannot change VF state to %s while rate is set\n",
		  (vlan == MLX4_VGT) ? "VGT" : "VST");

	if (vlan != MLX4_VGT)
		mlx4_info(dev, "VST priority %d not supported for QoS\n", qos);

	mlx4_info(dev, "Please set rate to 0 prior to this VF state change\n");

	return false;
}

int mlx4_set_vf_mac(struct mlx4_dev *dev, int port, int vf, u64 mac)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	struct mlx4_vport_state *s_info;
	int slave;

	if (!mlx4_is_master(dev))
		return -EPROTONOSUPPORT;

	slave = mlx4_get_slave_indx(dev, vf);
	if (slave < 0)
		return -EINVAL;

	port = mlx4_slaves_closest_port(dev, slave, port);
	s_info = &priv->mfunc.master.vf_admin[slave].vport[port];
	s_info->mac = mac;
	mlx4_info(dev, "default mac on vf %d port %d to %llX will take effect only after vf restart\n",
		  vf, port, s_info->mac);
	return 0;
}
EXPORT_SYMBOL_GPL(mlx4_set_vf_mac);


int mlx4_set_vf_vlan(struct mlx4_dev *dev, int port, int vf, u16 vlan, u8 qos,
		     __be16 proto)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	struct mlx4_vport_state *vf_admin;
	struct mlx4_slave_state *slave_state;
	struct mlx4_vport_oper_state *vf_oper;
	int slave;

	if ((!mlx4_is_master(dev)) ||
	    !(dev->caps.flags2 & MLX4_DEV_CAP_FLAG2_VLAN_CONTROL))
		return -EPROTONOSUPPORT;

	if ((vlan > 4095) || (qos > 7))
		return -EINVAL;

	if (proto == htons(ETH_P_8021AD) &&
	    !(dev->caps.flags2 & MLX4_DEV_CAP_FLAG2_SVLAN_BY_QP))
		return -EPROTONOSUPPORT;

	if (proto != htons(ETH_P_8021Q) &&
	    proto != htons(ETH_P_8021AD))
		return -EINVAL;

	if ((proto == htons(ETH_P_8021AD)) &&
	    ((vlan == 0) || (vlan == MLX4_VGT)))
		return -EINVAL;

	slave = mlx4_get_slave_indx(dev, vf);
	if (slave < 0)
		return -EINVAL;

	slave_state = &priv->mfunc.master.slave_state[slave];
	if ((proto == htons(ETH_P_8021AD)) && (slave_state->active) &&
	    (!slave_state->vst_qinq_supported)) {
		mlx4_err(dev, "vf %d does not support VST QinQ mode\n", vf);
		return -EPROTONOSUPPORT;
	}
	port = mlx4_slaves_closest_port(dev, slave, port);
	vf_admin = &priv->mfunc.master.vf_admin[slave].vport[port];
	vf_oper = &priv->mfunc.master.vf_oper[slave].vport[port];

	if (!mlx4_valid_vf_state_change(dev, port, vf_admin, vlan, qos))
		return -EPERM;

	if ((0 == vlan) && (0 == qos))
		vf_admin->default_vlan = MLX4_VGT;
	else
		vf_admin->default_vlan = vlan;
	vf_admin->default_qos = qos;
	vf_admin->vlan_proto = proto;

	/* If rate was configured prior to VST, we saved the configured rate
	 * in vf_admin->rate and now, if priority supported we enforce the QoS
	 */
	if (mlx4_is_vf_vst_and_prio_qos(dev, port, vf_admin) &&
	    vf_admin->tx_rate)
		vf_admin->qos_vport = slave;

	/* Try to activate new vf state without restart,
	 * this option is not supported while moving to VST QinQ mode.
	 */
	if ((proto == htons(ETH_P_8021AD) &&
	     vf_oper->state.vlan_proto != proto) ||
	    mlx4_master_immediate_activate_vlan_qos(priv, slave, port))
		mlx4_info(dev,
			  "updating vf %d port %d config will take effect on next VF restart\n",
			  vf, port);
	return 0;
}
EXPORT_SYMBOL_GPL(mlx4_set_vf_vlan);

int mlx4_set_vf_rate(struct mlx4_dev *dev, int port, int vf, int min_tx_rate,
		     int max_tx_rate)
{
	int err;
	int slave;
	struct mlx4_vport_state *vf_admin;
	struct mlx4_priv *priv = mlx4_priv(dev);

	if (!mlx4_is_master(dev) ||
	    !(dev->caps.flags2 & MLX4_DEV_CAP_FLAG2_QOS_VPP))
		return -EPROTONOSUPPORT;

	if (min_tx_rate) {
		mlx4_info(dev, "Minimum BW share not supported\n");
		return -EPROTONOSUPPORT;
	}

	slave = mlx4_get_slave_indx(dev, vf);
	if (slave < 0)
		return -EINVAL;

	port = mlx4_slaves_closest_port(dev, slave, port);
	vf_admin = &priv->mfunc.master.vf_admin[slave].vport[port];

	err = mlx4_set_vport_qos(priv, slave, port, max_tx_rate);
	if (err) {
		mlx4_info(dev, "vf %d failed to set rate %d\n", vf,
			  max_tx_rate);
		return err;
	}

	vf_admin->tx_rate = max_tx_rate;
	/* if VF is not in supported mode (VST with supported prio),
	 * we do not change vport configuration for its QPs, but save
	 * the rate, so it will be enforced when it moves to supported
	 * mode next time.
	 */
	if (!mlx4_is_vf_vst_and_prio_qos(dev, port, vf_admin)) {
		mlx4_info(dev,
			  "rate set for VF %d when not in valid state\n", vf);

		if (vf_admin->default_vlan != MLX4_VGT)
			mlx4_info(dev, "VST priority not supported by QoS\n");
		else
			mlx4_info(dev, "VF in VGT mode (needed VST)\n");

		mlx4_info(dev,
			  "rate %d take affect when VF moves to valid state\n",
			  max_tx_rate);
		return 0;
	}

	/* If user sets rate 0 assigning default vport for its QPs */
	vf_admin->qos_vport = max_tx_rate ? slave : MLX4_VPP_DEFAULT_VPORT;

	if (priv->mfunc.master.slave_state[slave].active &&
	    dev->caps.flags2 & MLX4_DEV_CAP_FLAG2_UPDATE_QP)
		mlx4_master_immediate_activate_vlan_qos(priv, slave, port);

	return 0;
}
EXPORT_SYMBOL_GPL(mlx4_set_vf_rate);

 /* mlx4_get_slave_default_vlan -
 * return true if VST ( default vlan)
 * if VST, will return vlan & qos (if not NULL)
 */
bool mlx4_get_slave_default_vlan(struct mlx4_dev *dev, int port, int slave,
				 u16 *vlan, u8 *qos)
{
	struct mlx4_vport_oper_state *vp_oper;
	struct mlx4_priv *priv;

	priv = mlx4_priv(dev);
	port = mlx4_slaves_closest_port(dev, slave, port);
	vp_oper = &priv->mfunc.master.vf_oper[slave].vport[port];

	if (MLX4_VGT != vp_oper->state.default_vlan) {
		if (vlan)
			*vlan = vp_oper->state.default_vlan;
		if (qos)
			*qos = vp_oper->state.default_qos;
		return true;
	}
	return false;
}
EXPORT_SYMBOL_GPL(mlx4_get_slave_default_vlan);

int mlx4_set_vf_spoofchk(struct mlx4_dev *dev, int port, int vf, bool setting)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	struct mlx4_vport_state *s_info;
	int slave;

	if ((!mlx4_is_master(dev)) ||
	    !(dev->caps.flags2 & MLX4_DEV_CAP_FLAG2_FSM))
		return -EPROTONOSUPPORT;

	slave = mlx4_get_slave_indx(dev, vf);
	if (slave < 0)
		return -EINVAL;

	port = mlx4_slaves_closest_port(dev, slave, port);
	s_info = &priv->mfunc.master.vf_admin[slave].vport[port];
	s_info->spoofchk = setting;

	return 0;
}
EXPORT_SYMBOL_GPL(mlx4_set_vf_spoofchk);

int mlx4_get_vf_config(struct mlx4_dev *dev, int port, int vf, struct ifla_vf_info *ivf)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	struct mlx4_vport_state *s_info;
	int slave;

	if (!mlx4_is_master(dev))
		return -EPROTONOSUPPORT;

	slave = mlx4_get_slave_indx(dev, vf);
	if (slave < 0)
		return -EINVAL;

	s_info = &priv->mfunc.master.vf_admin[slave].vport[port];
	ivf->vf = vf;

	/* need to convert it to a func */
	ivf->mac[0] = ((s_info->mac >> (5*8)) & 0xff);
	ivf->mac[1] = ((s_info->mac >> (4*8)) & 0xff);
	ivf->mac[2] = ((s_info->mac >> (3*8)) & 0xff);
	ivf->mac[3] = ((s_info->mac >> (2*8)) & 0xff);
	ivf->mac[4] = ((s_info->mac >> (1*8)) & 0xff);
	ivf->mac[5] = ((s_info->mac)  & 0xff);

	ivf->vlan		= s_info->default_vlan;
	ivf->qos		= s_info->default_qos;
	ivf->vlan_proto		= s_info->vlan_proto;

	if (mlx4_is_vf_vst_and_prio_qos(dev, port, s_info))
		ivf->max_tx_rate = s_info->tx_rate;
	else
		ivf->max_tx_rate = 0;

	ivf->min_tx_rate	= 0;
	ivf->spoofchk		= s_info->spoofchk;
	ivf->linkstate		= s_info->link_state;

	return 0;
}
EXPORT_SYMBOL_GPL(mlx4_get_vf_config);

int mlx4_set_vf_link_state(struct mlx4_dev *dev, int port, int vf, int link_state)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	struct mlx4_vport_state *s_info;
	int slave;
	u8 link_stat_event;

	slave = mlx4_get_slave_indx(dev, vf);
	if (slave < 0)
		return -EINVAL;

	port = mlx4_slaves_closest_port(dev, slave, port);
	switch (link_state) {
	case IFLA_VF_LINK_STATE_AUTO:
		/* get current link state */
		if (!priv->sense.do_sense_port[port])
			link_stat_event = MLX4_PORT_CHANGE_SUBTYPE_ACTIVE;
		else
			link_stat_event = MLX4_PORT_CHANGE_SUBTYPE_DOWN;
	    break;

	case IFLA_VF_LINK_STATE_ENABLE:
		link_stat_event = MLX4_PORT_CHANGE_SUBTYPE_ACTIVE;
	    break;

	case IFLA_VF_LINK_STATE_DISABLE:
		link_stat_event = MLX4_PORT_CHANGE_SUBTYPE_DOWN;
	    break;

	default:
		mlx4_warn(dev, "unknown value for link_state %02x on slave %d port %d\n",
			  link_state, slave, port);
		return -EINVAL;
	};
	s_info = &priv->mfunc.master.vf_admin[slave].vport[port];
	s_info->link_state = link_state;

	/* send event */
	mlx4_gen_port_state_change_eqe(dev, slave, port, link_stat_event);

	if (mlx4_master_immediate_activate_vlan_qos(priv, slave, port))
		mlx4_dbg(dev,
			 "updating vf %d port %d no link state HW enforcment\n",
			 vf, port);
	return 0;
}
EXPORT_SYMBOL_GPL(mlx4_set_vf_link_state);

int mlx4_get_counter_stats(struct mlx4_dev *dev, int counter_index,
			   struct mlx4_counter *counter_stats, int reset)
{
	struct mlx4_cmd_mailbox *mailbox = NULL;
	struct mlx4_counter *tmp_counter;
	int err;
	u32 if_stat_in_mod;

	if (!counter_stats)
		return -EINVAL;

	if (counter_index == MLX4_SINK_COUNTER_INDEX(dev))
		return 0;

	mailbox = mlx4_alloc_cmd_mailbox(dev);
	if (IS_ERR(mailbox))
		return PTR_ERR(mailbox);

	memset(mailbox->buf, 0, sizeof(struct mlx4_counter));
	if_stat_in_mod = counter_index;
	if (reset)
		if_stat_in_mod |= MLX4_QUERY_IF_STAT_RESET;
	err = mlx4_cmd_box(dev, 0, mailbox->dma,
			   if_stat_in_mod, 0,
			   MLX4_CMD_QUERY_IF_STAT,
			   MLX4_CMD_TIME_CLASS_C,
			   MLX4_CMD_NATIVE);
	if (err) {
		mlx4_dbg(dev, "%s: failed to read statistics for counter index %d\n",
			 __func__, counter_index);
		goto if_stat_out;
	}
	tmp_counter = (struct mlx4_counter *)mailbox->buf;
	counter_stats->counter_mode = tmp_counter->counter_mode;
	if (counter_stats->counter_mode == 0) {
		counter_stats->rx_frames =
			cpu_to_be64(be64_to_cpu(counter_stats->rx_frames) +
				    be64_to_cpu(tmp_counter->rx_frames));
		counter_stats->tx_frames =
			cpu_to_be64(be64_to_cpu(counter_stats->tx_frames) +
				    be64_to_cpu(tmp_counter->tx_frames));
		counter_stats->rx_bytes =
			cpu_to_be64(be64_to_cpu(counter_stats->rx_bytes) +
				    be64_to_cpu(tmp_counter->rx_bytes));
		counter_stats->tx_bytes =
			cpu_to_be64(be64_to_cpu(counter_stats->tx_bytes) +
				    be64_to_cpu(tmp_counter->tx_bytes));
	}

if_stat_out:
	mlx4_free_cmd_mailbox(dev, mailbox);

	return err;
}
EXPORT_SYMBOL_GPL(mlx4_get_counter_stats);

int mlx4_get_vf_stats(struct mlx4_dev *dev, int port, int vf_idx,
		      struct ifla_vf_stats *vf_stats)
{
	struct mlx4_counter tmp_vf_stats;
	int slave;
	int err = 0;

	if (!vf_stats)
		return -EINVAL;

	if (!mlx4_is_master(dev))
		return -EPROTONOSUPPORT;

	slave = mlx4_get_slave_indx(dev, vf_idx);
	if (slave < 0)
		return -EINVAL;

	port = mlx4_slaves_closest_port(dev, slave, port);
	err = mlx4_calc_vf_counters(dev, slave, port, &tmp_vf_stats);
	if (!err && tmp_vf_stats.counter_mode == 0) {
		vf_stats->rx_packets = be64_to_cpu(tmp_vf_stats.rx_frames);
		vf_stats->tx_packets = be64_to_cpu(tmp_vf_stats.tx_frames);
		vf_stats->rx_bytes = be64_to_cpu(tmp_vf_stats.rx_bytes);
		vf_stats->tx_bytes = be64_to_cpu(tmp_vf_stats.tx_bytes);
	}

	return err;
}
EXPORT_SYMBOL_GPL(mlx4_get_vf_stats);

int mlx4_vf_smi_enabled(struct mlx4_dev *dev, int slave, int port)
{
	struct mlx4_priv *priv = mlx4_priv(dev);

	if (slave < 1 || slave >= dev->num_slaves ||
	    port < 1 || port > MLX4_MAX_PORTS)
		return 0;

	return priv->mfunc.master.vf_oper[slave].smi_enabled[port] ==
		MLX4_VF_SMI_ENABLED;
}
EXPORT_SYMBOL_GPL(mlx4_vf_smi_enabled);

int mlx4_vf_get_enable_smi_admin(struct mlx4_dev *dev, int slave, int port)
{
	struct mlx4_priv *priv = mlx4_priv(dev);

	if (slave == mlx4_master_func_num(dev))
		return 1;

	if (slave < 1 || slave >= dev->num_slaves ||
	    port < 1 || port > MLX4_MAX_PORTS)
		return 0;

	return priv->mfunc.master.vf_admin[slave].enable_smi[port] ==
		MLX4_VF_SMI_ENABLED;
}
EXPORT_SYMBOL_GPL(mlx4_vf_get_enable_smi_admin);

int mlx4_vf_set_enable_smi_admin(struct mlx4_dev *dev, int slave, int port,
				 int enabled)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	struct mlx4_active_ports actv_ports = mlx4_get_active_ports(
			&priv->dev, slave);
	int min_port = find_first_bit(actv_ports.ports,
				      priv->dev.caps.num_ports) + 1;
	int max_port = min_port - 1 +
		bitmap_weight(actv_ports.ports, priv->dev.caps.num_ports);

	if (slave == mlx4_master_func_num(dev))
		return 0;

	if (slave < 1 || slave >= dev->num_slaves ||
	    port < 1 || port > MLX4_MAX_PORTS ||
	    enabled < 0 || enabled > 1)
		return -EINVAL;

	if (min_port == max_port && dev->caps.num_ports > 1) {
		mlx4_info(dev, "SMI access disallowed for single ported VFs\n");
		return -EPROTONOSUPPORT;
	}

	priv->mfunc.master.vf_admin[slave].enable_smi[port] = enabled;
	return 0;
}
EXPORT_SYMBOL_GPL(mlx4_vf_set_enable_smi_admin);

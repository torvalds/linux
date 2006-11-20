/* hw_ops.c - query/set operations on active SPU context.
 *
 * Copyright (C) IBM 2005
 * Author: Mark Nutter <mnutter@us.ibm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/module.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/poll.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/stddef.h>
#include <linux/unistd.h>

#include <asm/io.h>
#include <asm/spu.h>
#include <asm/spu_priv1.h>
#include <asm/spu_csa.h>
#include <asm/mmu_context.h>
#include "spufs.h"

static int spu_hw_mbox_read(struct spu_context *ctx, u32 * data)
{
	struct spu *spu = ctx->spu;
	struct spu_problem __iomem *prob = spu->problem;
	u32 mbox_stat;
	int ret = 0;

	spin_lock_irq(&spu->register_lock);
	mbox_stat = in_be32(&prob->mb_stat_R);
	if (mbox_stat & 0x0000ff) {
		*data = in_be32(&prob->pu_mb_R);
		ret = 4;
	}
	spin_unlock_irq(&spu->register_lock);
	return ret;
}

static u32 spu_hw_mbox_stat_read(struct spu_context *ctx)
{
	return in_be32(&ctx->spu->problem->mb_stat_R);
}

static unsigned int spu_hw_mbox_stat_poll(struct spu_context *ctx,
					  unsigned int events)
{
	struct spu *spu = ctx->spu;
	int ret = 0;
	u32 stat;

	spin_lock_irq(&spu->register_lock);
	stat = in_be32(&spu->problem->mb_stat_R);

	/* if the requested event is there, return the poll
	   mask, otherwise enable the interrupt to get notified,
	   but first mark any pending interrupts as done so
	   we don't get woken up unnecessarily */

	if (events & (POLLIN | POLLRDNORM)) {
		if (stat & 0xff0000)
			ret |= POLLIN | POLLRDNORM;
		else {
			spu_int_stat_clear(spu, 2, 0x1);
			spu_int_mask_or(spu, 2, 0x1);
		}
	}
	if (events & (POLLOUT | POLLWRNORM)) {
		if (stat & 0x00ff00)
			ret = POLLOUT | POLLWRNORM;
		else {
			spu_int_stat_clear(spu, 2, 0x10);
			spu_int_mask_or(spu, 2, 0x10);
		}
	}
	spin_unlock_irq(&spu->register_lock);
	return ret;
}

static int spu_hw_ibox_read(struct spu_context *ctx, u32 * data)
{
	struct spu *spu = ctx->spu;
	struct spu_problem __iomem *prob = spu->problem;
	struct spu_priv2 __iomem *priv2 = spu->priv2;
	int ret;

	spin_lock_irq(&spu->register_lock);
	if (in_be32(&prob->mb_stat_R) & 0xff0000) {
		/* read the first available word */
		*data = in_be64(&priv2->puint_mb_R);
		ret = 4;
	} else {
		/* make sure we get woken up by the interrupt */
		spu_int_mask_or(spu, 2, 0x1);
		ret = 0;
	}
	spin_unlock_irq(&spu->register_lock);
	return ret;
}

static int spu_hw_wbox_write(struct spu_context *ctx, u32 data)
{
	struct spu *spu = ctx->spu;
	struct spu_problem __iomem *prob = spu->problem;
	int ret;

	spin_lock_irq(&spu->register_lock);
	if (in_be32(&prob->mb_stat_R) & 0x00ff00) {
		/* we have space to write wbox_data to */
		out_be32(&prob->spu_mb_W, data);
		ret = 4;
	} else {
		/* make sure we get woken up by the interrupt when space
		   becomes available */
		spu_int_mask_or(spu, 2, 0x10);
		ret = 0;
	}
	spin_unlock_irq(&spu->register_lock);
	return ret;
}

static void spu_hw_signal1_write(struct spu_context *ctx, u32 data)
{
	out_be32(&ctx->spu->problem->signal_notify1, data);
}

static void spu_hw_signal2_write(struct spu_context *ctx, u32 data)
{
	out_be32(&ctx->spu->problem->signal_notify2, data);
}

static void spu_hw_signal1_type_set(struct spu_context *ctx, u64 val)
{
	struct spu *spu = ctx->spu;
	struct spu_priv2 __iomem *priv2 = spu->priv2;
	u64 tmp;

	spin_lock_irq(&spu->register_lock);
	tmp = in_be64(&priv2->spu_cfg_RW);
	if (val)
		tmp |= 1;
	else
		tmp &= ~1;
	out_be64(&priv2->spu_cfg_RW, tmp);
	spin_unlock_irq(&spu->register_lock);
}

static u64 spu_hw_signal1_type_get(struct spu_context *ctx)
{
	return ((in_be64(&ctx->spu->priv2->spu_cfg_RW) & 1) != 0);
}

static void spu_hw_signal2_type_set(struct spu_context *ctx, u64 val)
{
	struct spu *spu = ctx->spu;
	struct spu_priv2 __iomem *priv2 = spu->priv2;
	u64 tmp;

	spin_lock_irq(&spu->register_lock);
	tmp = in_be64(&priv2->spu_cfg_RW);
	if (val)
		tmp |= 2;
	else
		tmp &= ~2;
	out_be64(&priv2->spu_cfg_RW, tmp);
	spin_unlock_irq(&spu->register_lock);
}

static u64 spu_hw_signal2_type_get(struct spu_context *ctx)
{
	return ((in_be64(&ctx->spu->priv2->spu_cfg_RW) & 2) != 0);
}

static u32 spu_hw_npc_read(struct spu_context *ctx)
{
	return in_be32(&ctx->spu->problem->spu_npc_RW);
}

static void spu_hw_npc_write(struct spu_context *ctx, u32 val)
{
	out_be32(&ctx->spu->problem->spu_npc_RW, val);
}

static u32 spu_hw_status_read(struct spu_context *ctx)
{
	return in_be32(&ctx->spu->problem->spu_status_R);
}

static char *spu_hw_get_ls(struct spu_context *ctx)
{
	return ctx->spu->local_store;
}

static u32 spu_hw_runcntl_read(struct spu_context *ctx)
{
	return in_be32(&ctx->spu->problem->spu_runcntl_RW);
}

static void spu_hw_runcntl_write(struct spu_context *ctx, u32 val)
{
	spin_lock_irq(&ctx->spu->register_lock);
	if (val & SPU_RUNCNTL_ISOLATE)
		out_be64(&ctx->spu->priv2->spu_privcntl_RW, 4LL);
	out_be32(&ctx->spu->problem->spu_runcntl_RW, val);
	spin_unlock_irq(&ctx->spu->register_lock);
}

static void spu_hw_master_start(struct spu_context *ctx)
{
	struct spu *spu = ctx->spu;
	u64 sr1;

	spin_lock_irq(&spu->register_lock);
	sr1 = spu_mfc_sr1_get(spu) | MFC_STATE1_MASTER_RUN_CONTROL_MASK;
	spu_mfc_sr1_set(spu, sr1);
	spin_unlock_irq(&spu->register_lock);
}

static void spu_hw_master_stop(struct spu_context *ctx)
{
	struct spu *spu = ctx->spu;
	u64 sr1;

	spin_lock_irq(&spu->register_lock);
	sr1 = spu_mfc_sr1_get(spu) & ~MFC_STATE1_MASTER_RUN_CONTROL_MASK;
	spu_mfc_sr1_set(spu, sr1);
	spin_unlock_irq(&spu->register_lock);
}

static int spu_hw_set_mfc_query(struct spu_context * ctx, u32 mask, u32 mode)
{
	struct spu_problem __iomem *prob = ctx->spu->problem;
	int ret;

	spin_lock_irq(&ctx->spu->register_lock);
	ret = -EAGAIN;
	if (in_be32(&prob->dma_querytype_RW))
		goto out;
	ret = 0;
	out_be32(&prob->dma_querymask_RW, mask);
	out_be32(&prob->dma_querytype_RW, mode);
out:
	spin_unlock_irq(&ctx->spu->register_lock);
	return ret;
}

static u32 spu_hw_read_mfc_tagstatus(struct spu_context * ctx)
{
	return in_be32(&ctx->spu->problem->dma_tagstatus_R);
}

static u32 spu_hw_get_mfc_free_elements(struct spu_context *ctx)
{
	return in_be32(&ctx->spu->problem->dma_qstatus_R);
}

static int spu_hw_send_mfc_command(struct spu_context *ctx,
					struct mfc_dma_command *cmd)
{
	u32 status;
	struct spu_problem __iomem *prob = ctx->spu->problem;

	spin_lock_irq(&ctx->spu->register_lock);
	out_be32(&prob->mfc_lsa_W, cmd->lsa);
	out_be64(&prob->mfc_ea_W, cmd->ea);
	out_be32(&prob->mfc_union_W.by32.mfc_size_tag32,
				cmd->size << 16 | cmd->tag);
	out_be32(&prob->mfc_union_W.by32.mfc_class_cmd32,
				cmd->class << 16 | cmd->cmd);
	status = in_be32(&prob->mfc_union_W.by32.mfc_class_cmd32);
	spin_unlock_irq(&ctx->spu->register_lock);

	switch (status & 0xffff) {
	case 0:
		return 0;
	case 2:
		return -EAGAIN;
	default:
		return -EINVAL;
	}
}

struct spu_context_ops spu_hw_ops = {
	.mbox_read = spu_hw_mbox_read,
	.mbox_stat_read = spu_hw_mbox_stat_read,
	.mbox_stat_poll = spu_hw_mbox_stat_poll,
	.ibox_read = spu_hw_ibox_read,
	.wbox_write = spu_hw_wbox_write,
	.signal1_write = spu_hw_signal1_write,
	.signal2_write = spu_hw_signal2_write,
	.signal1_type_set = spu_hw_signal1_type_set,
	.signal1_type_get = spu_hw_signal1_type_get,
	.signal2_type_set = spu_hw_signal2_type_set,
	.signal2_type_get = spu_hw_signal2_type_get,
	.npc_read = spu_hw_npc_read,
	.npc_write = spu_hw_npc_write,
	.status_read = spu_hw_status_read,
	.get_ls = spu_hw_get_ls,
	.runcntl_read = spu_hw_runcntl_read,
	.runcntl_write = spu_hw_runcntl_write,
	.master_start = spu_hw_master_start,
	.master_stop = spu_hw_master_stop,
	.set_mfc_query = spu_hw_set_mfc_query,
	.read_mfc_tagstatus = spu_hw_read_mfc_tagstatus,
	.get_mfc_free_elements = spu_hw_get_mfc_free_elements,
	.send_mfc_command = spu_hw_send_mfc_command,
};

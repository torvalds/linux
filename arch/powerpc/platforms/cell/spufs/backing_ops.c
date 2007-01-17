/* backing_ops.c - query/set operations on saved SPU context.
 *
 * Copyright (C) IBM 2005
 * Author: Mark Nutter <mnutter@us.ibm.com>
 *
 * These register operations allow SPUFS to operate on saved
 * SPU contexts rather than hardware.
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
#include <linux/vmalloc.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/stddef.h>
#include <linux/unistd.h>
#include <linux/poll.h>

#include <asm/io.h>
#include <asm/spu.h>
#include <asm/spu_csa.h>
#include <asm/spu_info.h>
#include <asm/mmu_context.h>
#include "spufs.h"

/*
 * Reads/writes to various problem and priv2 registers require
 * state changes, i.e.  generate SPU events, modify channel
 * counts, etc.
 */

static void gen_spu_event(struct spu_context *ctx, u32 event)
{
	u64 ch0_cnt;
	u64 ch0_data;
	u64 ch1_data;

	ch0_cnt = ctx->csa.spu_chnlcnt_RW[0];
	ch0_data = ctx->csa.spu_chnldata_RW[0];
	ch1_data = ctx->csa.spu_chnldata_RW[1];
	ctx->csa.spu_chnldata_RW[0] |= event;
	if ((ch0_cnt == 0) && !(ch0_data & event) && (ch1_data & event)) {
		ctx->csa.spu_chnlcnt_RW[0] = 1;
	}
}

static int spu_backing_mbox_read(struct spu_context *ctx, u32 * data)
{
	u32 mbox_stat;
	int ret = 0;

	spin_lock(&ctx->csa.register_lock);
	mbox_stat = ctx->csa.prob.mb_stat_R;
	if (mbox_stat & 0x0000ff) {
		/* Read the first available word.
		 * Implementation note: the depth
		 * of pu_mb_R is currently 1.
		 */
		*data = ctx->csa.prob.pu_mb_R;
		ctx->csa.prob.mb_stat_R &= ~(0x0000ff);
		ctx->csa.spu_chnlcnt_RW[28] = 1;
		gen_spu_event(ctx, MFC_PU_MAILBOX_AVAILABLE_EVENT);
		ret = 4;
	}
	spin_unlock(&ctx->csa.register_lock);
	return ret;
}

static u32 spu_backing_mbox_stat_read(struct spu_context *ctx)
{
	return ctx->csa.prob.mb_stat_R;
}

static unsigned int spu_backing_mbox_stat_poll(struct spu_context *ctx,
					  unsigned int events)
{
	int ret;
	u32 stat;

	ret = 0;
	spin_lock_irq(&ctx->csa.register_lock);
	stat = ctx->csa.prob.mb_stat_R;

	/* if the requested event is there, return the poll
	   mask, otherwise enable the interrupt to get notified,
	   but first mark any pending interrupts as done so
	   we don't get woken up unnecessarily */

	if (events & (POLLIN | POLLRDNORM)) {
		if (stat & 0xff0000)
			ret |= POLLIN | POLLRDNORM;
		else {
			ctx->csa.priv1.int_stat_class0_RW &= ~0x1;
			ctx->csa.priv1.int_mask_class2_RW |= 0x1;
		}
	}
	if (events & (POLLOUT | POLLWRNORM)) {
		if (stat & 0x00ff00)
			ret = POLLOUT | POLLWRNORM;
		else {
			ctx->csa.priv1.int_stat_class0_RW &= ~0x10;
			ctx->csa.priv1.int_mask_class2_RW |= 0x10;
		}
	}
	spin_unlock_irq(&ctx->csa.register_lock);
	return ret;
}

static int spu_backing_ibox_read(struct spu_context *ctx, u32 * data)
{
	int ret;

	spin_lock(&ctx->csa.register_lock);
	if (ctx->csa.prob.mb_stat_R & 0xff0000) {
		/* Read the first available word.
		 * Implementation note: the depth
		 * of puint_mb_R is currently 1.
		 */
		*data = ctx->csa.priv2.puint_mb_R;
		ctx->csa.prob.mb_stat_R &= ~(0xff0000);
		ctx->csa.spu_chnlcnt_RW[30] = 1;
		gen_spu_event(ctx, MFC_PU_INT_MAILBOX_AVAILABLE_EVENT);
		ret = 4;
	} else {
		/* make sure we get woken up by the interrupt */
		ctx->csa.priv1.int_mask_class2_RW |= 0x1UL;
		ret = 0;
	}
	spin_unlock(&ctx->csa.register_lock);
	return ret;
}

static int spu_backing_wbox_write(struct spu_context *ctx, u32 data)
{
	int ret;

	spin_lock(&ctx->csa.register_lock);
	if ((ctx->csa.prob.mb_stat_R) & 0x00ff00) {
		int slot = ctx->csa.spu_chnlcnt_RW[29];
		int avail = (ctx->csa.prob.mb_stat_R & 0x00ff00) >> 8;

		/* We have space to write wbox_data.
		 * Implementation note: the depth
		 * of spu_mb_W is currently 4.
		 */
		BUG_ON(avail != (4 - slot));
		ctx->csa.spu_mailbox_data[slot] = data;
		ctx->csa.spu_chnlcnt_RW[29] = ++slot;
		ctx->csa.prob.mb_stat_R = (((4 - slot) & 0xff) << 8);
		gen_spu_event(ctx, MFC_SPU_MAILBOX_WRITTEN_EVENT);
		ret = 4;
	} else {
		/* make sure we get woken up by the interrupt when space
		   becomes available */
		ctx->csa.priv1.int_mask_class2_RW |= 0x10;
		ret = 0;
	}
	spin_unlock(&ctx->csa.register_lock);
	return ret;
}

static u32 spu_backing_signal1_read(struct spu_context *ctx)
{
	return ctx->csa.spu_chnldata_RW[3];
}

static void spu_backing_signal1_write(struct spu_context *ctx, u32 data)
{
	spin_lock(&ctx->csa.register_lock);
	if (ctx->csa.priv2.spu_cfg_RW & 0x1)
		ctx->csa.spu_chnldata_RW[3] |= data;
	else
		ctx->csa.spu_chnldata_RW[3] = data;
	ctx->csa.spu_chnlcnt_RW[3] = 1;
	gen_spu_event(ctx, MFC_SIGNAL_1_EVENT);
	spin_unlock(&ctx->csa.register_lock);
}

static u32 spu_backing_signal2_read(struct spu_context *ctx)
{
	return ctx->csa.spu_chnldata_RW[4];
}

static void spu_backing_signal2_write(struct spu_context *ctx, u32 data)
{
	spin_lock(&ctx->csa.register_lock);
	if (ctx->csa.priv2.spu_cfg_RW & 0x2)
		ctx->csa.spu_chnldata_RW[4] |= data;
	else
		ctx->csa.spu_chnldata_RW[4] = data;
	ctx->csa.spu_chnlcnt_RW[4] = 1;
	gen_spu_event(ctx, MFC_SIGNAL_2_EVENT);
	spin_unlock(&ctx->csa.register_lock);
}

static void spu_backing_signal1_type_set(struct spu_context *ctx, u64 val)
{
	u64 tmp;

	spin_lock(&ctx->csa.register_lock);
	tmp = ctx->csa.priv2.spu_cfg_RW;
	if (val)
		tmp |= 1;
	else
		tmp &= ~1;
	ctx->csa.priv2.spu_cfg_RW = tmp;
	spin_unlock(&ctx->csa.register_lock);
}

static u64 spu_backing_signal1_type_get(struct spu_context *ctx)
{
	return ((ctx->csa.priv2.spu_cfg_RW & 1) != 0);
}

static void spu_backing_signal2_type_set(struct spu_context *ctx, u64 val)
{
	u64 tmp;

	spin_lock(&ctx->csa.register_lock);
	tmp = ctx->csa.priv2.spu_cfg_RW;
	if (val)
		tmp |= 2;
	else
		tmp &= ~2;
	ctx->csa.priv2.spu_cfg_RW = tmp;
	spin_unlock(&ctx->csa.register_lock);
}

static u64 spu_backing_signal2_type_get(struct spu_context *ctx)
{
	return ((ctx->csa.priv2.spu_cfg_RW & 2) != 0);
}

static u32 spu_backing_npc_read(struct spu_context *ctx)
{
	return ctx->csa.prob.spu_npc_RW;
}

static void spu_backing_npc_write(struct spu_context *ctx, u32 val)
{
	ctx->csa.prob.spu_npc_RW = val;
}

static u32 spu_backing_status_read(struct spu_context *ctx)
{
	return ctx->csa.prob.spu_status_R;
}

static char *spu_backing_get_ls(struct spu_context *ctx)
{
	return ctx->csa.lscsa->ls;
}

static u32 spu_backing_runcntl_read(struct spu_context *ctx)
{
	return ctx->csa.prob.spu_runcntl_RW;
}

static void spu_backing_runcntl_write(struct spu_context *ctx, u32 val)
{
	spin_lock(&ctx->csa.register_lock);
	ctx->csa.prob.spu_runcntl_RW = val;
	if (val & SPU_RUNCNTL_RUNNABLE) {
		ctx->csa.prob.spu_status_R |= SPU_STATUS_RUNNING;
	} else {
		ctx->csa.prob.spu_status_R &= ~SPU_STATUS_RUNNING;
	}
	spin_unlock(&ctx->csa.register_lock);
}

static void spu_backing_master_start(struct spu_context *ctx)
{
	struct spu_state *csa = &ctx->csa;
	u64 sr1;

	spin_lock(&csa->register_lock);
	sr1 = csa->priv1.mfc_sr1_RW | MFC_STATE1_MASTER_RUN_CONTROL_MASK;
	csa->priv1.mfc_sr1_RW = sr1;
	spin_unlock(&csa->register_lock);
}

static void spu_backing_master_stop(struct spu_context *ctx)
{
	struct spu_state *csa = &ctx->csa;
	u64 sr1;

	spin_lock(&csa->register_lock);
	sr1 = csa->priv1.mfc_sr1_RW & ~MFC_STATE1_MASTER_RUN_CONTROL_MASK;
	csa->priv1.mfc_sr1_RW = sr1;
	spin_unlock(&csa->register_lock);
}

static int spu_backing_set_mfc_query(struct spu_context * ctx, u32 mask,
					u32 mode)
{
	struct spu_problem_collapsed *prob = &ctx->csa.prob;
	int ret;

	spin_lock(&ctx->csa.register_lock);
	ret = -EAGAIN;
	if (prob->dma_querytype_RW)
		goto out;
	ret = 0;
	/* FIXME: what are the side-effects of this? */
	prob->dma_querymask_RW = mask;
	prob->dma_querytype_RW = mode;
out:
	spin_unlock(&ctx->csa.register_lock);

	return ret;
}

static u32 spu_backing_read_mfc_tagstatus(struct spu_context * ctx)
{
	return ctx->csa.prob.dma_tagstatus_R;
}

static u32 spu_backing_get_mfc_free_elements(struct spu_context *ctx)
{
	return ctx->csa.prob.dma_qstatus_R;
}

static int spu_backing_send_mfc_command(struct spu_context *ctx,
					struct mfc_dma_command *cmd)
{
	int ret;

	spin_lock(&ctx->csa.register_lock);
	ret = -EAGAIN;
	/* FIXME: set up priv2->puq */
	spin_unlock(&ctx->csa.register_lock);

	return ret;
}

struct spu_context_ops spu_backing_ops = {
	.mbox_read = spu_backing_mbox_read,
	.mbox_stat_read = spu_backing_mbox_stat_read,
	.mbox_stat_poll = spu_backing_mbox_stat_poll,
	.ibox_read = spu_backing_ibox_read,
	.wbox_write = spu_backing_wbox_write,
	.signal1_read = spu_backing_signal1_read,
	.signal1_write = spu_backing_signal1_write,
	.signal2_read = spu_backing_signal2_read,
	.signal2_write = spu_backing_signal2_write,
	.signal1_type_set = spu_backing_signal1_type_set,
	.signal1_type_get = spu_backing_signal1_type_get,
	.signal2_type_set = spu_backing_signal2_type_set,
	.signal2_type_get = spu_backing_signal2_type_get,
	.npc_read = spu_backing_npc_read,
	.npc_write = spu_backing_npc_write,
	.status_read = spu_backing_status_read,
	.get_ls = spu_backing_get_ls,
	.runcntl_read = spu_backing_runcntl_read,
	.runcntl_write = spu_backing_runcntl_write,
	.master_start = spu_backing_master_start,
	.master_stop = spu_backing_master_stop,
	.set_mfc_query = spu_backing_set_mfc_query,
	.read_mfc_tagstatus = spu_backing_read_mfc_tagstatus,
	.get_mfc_free_elements = spu_backing_get_mfc_free_elements,
	.send_mfc_command = spu_backing_send_mfc_command,
};

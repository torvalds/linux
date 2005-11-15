/*
 * spu_switch.c
 *
 * (C) Copyright IBM Corp. 2005
 *
 * Author: Mark Nutter <mnutter@us.ibm.com>
 *
 * Host-side part of SPU context switch sequence outlined in
 * Synergistic Processor Element, Book IV.
 *
 * A fully premptive switch of an SPE is very expensive in terms
 * of time and system resources.  SPE Book IV indicates that SPE
 * allocation should follow a "serially reusable device" model,
 * in which the SPE is assigned a task until it completes.  When
 * this is not possible, this sequence may be used to premptively
 * save, and then later (optionally) restore the context of a
 * program executing on an SPE.
 *
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

#include <linux/config.h>
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

#include <asm/io.h>
#include <asm/spu.h>
#include <asm/spu_csa.h>
#include <asm/mmu_context.h>

#include "spu_save_dump.h"
#include "spu_restore_dump.h"

/**
 * spu_save - SPU context save, with locking.
 * @prev: pointer to SPU context save area, to be saved.
 * @spu: pointer to SPU iomem structure.
 *
 * Acquire locks, perform the save operation then return.
 */
int spu_save(struct spu_state *prev, struct spu *spu)
{
	/* XXX missing */

	return 0;
}

/**
 * spu_restore - SPU context restore, with harvest and locking.
 * @new: pointer to SPU context save area, to be restored.
 * @spu: pointer to SPU iomem structure.
 *
 * Perform harvest + restore, as we may not be coming
 * from a previous succesful save operation, and the
 * hardware state is unknown.
 */
int spu_restore(struct spu_state *new, struct spu *spu)
{
	/* XXX missing */

	return 0;
}

/**
 * spu_switch - SPU context switch (save + restore).
 * @prev: pointer to SPU context save area, to be saved.
 * @new: pointer to SPU context save area, to be restored.
 * @spu: pointer to SPU iomem structure.
 *
 * Perform save, then restore.  Only harvest if the
 * save fails, as cleanup is otherwise not needed.
 */
int spu_switch(struct spu_state *prev, struct spu_state *new, struct spu *spu)
{
	/* XXX missing */

	return 0;
}

static void init_prob(struct spu_state *csa)
{
	csa->spu_chnlcnt_RW[9] = 1;
	csa->spu_chnlcnt_RW[21] = 16;
	csa->spu_chnlcnt_RW[23] = 1;
	csa->spu_chnlcnt_RW[28] = 1;
	csa->spu_chnlcnt_RW[30] = 1;
	csa->prob.spu_runcntl_RW = SPU_RUNCNTL_STOP;
}

static void init_priv1(struct spu_state *csa)
{
	/* Enable decode, relocate, tlbie response, master runcntl. */
	csa->priv1.mfc_sr1_RW = MFC_STATE1_LOCAL_STORAGE_DECODE_MASK |
	    MFC_STATE1_MASTER_RUN_CONTROL_MASK |
	    MFC_STATE1_PROBLEM_STATE_MASK |
	    MFC_STATE1_RELOCATE_MASK | MFC_STATE1_BUS_TLBIE_MASK;

	/* Set storage description.  */
	csa->priv1.mfc_sdr_RW = mfspr(SPRN_SDR1);

	/* Enable OS-specific set of interrupts. */
	csa->priv1.int_mask_class0_RW = CLASS0_ENABLE_DMA_ALIGNMENT_INTR |
	    CLASS0_ENABLE_INVALID_DMA_COMMAND_INTR |
	    CLASS0_ENABLE_SPU_ERROR_INTR;
	csa->priv1.int_mask_class1_RW = CLASS1_ENABLE_SEGMENT_FAULT_INTR |
	    CLASS1_ENABLE_STORAGE_FAULT_INTR;
	csa->priv1.int_mask_class2_RW = CLASS2_ENABLE_MAILBOX_INTR |
	    CLASS2_ENABLE_SPU_STOP_INTR | CLASS2_ENABLE_SPU_HALT_INTR;
}

static void init_priv2(struct spu_state *csa)
{
	csa->priv2.spu_lslr_RW = LS_ADDR_MASK;
	csa->priv2.mfc_control_RW = MFC_CNTL_RESUME_DMA_QUEUE |
	    MFC_CNTL_NORMAL_DMA_QUEUE_OPERATION |
	    MFC_CNTL_DMA_QUEUES_EMPTY_MASK;
}

/**
 * spu_alloc_csa - allocate and initialize an SPU context save area.
 *
 * Allocate and initialize the contents of an SPU context save area.
 * This includes enabling address translation, interrupt masks, etc.,
 * as appropriate for the given OS environment.
 *
 * Note that storage for the 'lscsa' is allocated separately,
 * as it is by far the largest of the context save regions,
 * and may need to be pinned or otherwise specially aligned.
 */
void spu_init_csa(struct spu_state *csa)
{
	struct spu_lscsa *lscsa;

	if (!csa)
		return;
	memset(csa, 0, sizeof(struct spu_state));

	lscsa = vmalloc(sizeof(struct spu_lscsa));
	if (!lscsa)
		return;

	memset(lscsa, 0, sizeof(struct spu_lscsa));
	csa->lscsa = lscsa;

	init_prob(csa);
	init_priv1(csa);
	init_priv2(csa);
}

void spu_fini_csa(struct spu_state *csa)
{
	vfree(csa->lscsa);
}

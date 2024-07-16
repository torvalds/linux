// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Low-level SPU handling
 *
 * (C) Copyright IBM Deutschland Entwicklung GmbH 2005
 *
 * Author: Arnd Bergmann <arndb@de.ibm.com>
 */
#include <linux/sched/signal.h>
#include <linux/mm.h>

#include <asm/spu.h>
#include <asm/spu_csa.h>

#include "spufs.h"

/**
 * Handle an SPE event, depending on context SPU_CREATE_EVENTS_ENABLED flag.
 *
 * If the context was created with events, we just set the return event.
 * Otherwise, send an appropriate signal to the process.
 */
static void spufs_handle_event(struct spu_context *ctx,
				unsigned long ea, int type)
{
	if (ctx->flags & SPU_CREATE_EVENTS_ENABLED) {
		ctx->event_return |= type;
		wake_up_all(&ctx->stop_wq);
		return;
	}

	switch (type) {
	case SPE_EVENT_INVALID_DMA:
		force_sig_fault(SIGBUS, BUS_OBJERR, NULL);
		break;
	case SPE_EVENT_SPE_DATA_STORAGE:
		ctx->ops->restart_dma(ctx);
		force_sig_fault(SIGSEGV, SEGV_ACCERR, (void __user *)ea);
		break;
	case SPE_EVENT_DMA_ALIGNMENT:
		/* DAR isn't set for an alignment fault :( */
		force_sig_fault(SIGBUS, BUS_ADRALN, NULL);
		break;
	case SPE_EVENT_SPE_ERROR:
		force_sig_fault(
			SIGILL, ILL_ILLOPC,
			(void __user *)(unsigned long)
			ctx->ops->npc_read(ctx) - 4);
		break;
	}
}

int spufs_handle_class0(struct spu_context *ctx)
{
	unsigned long stat = ctx->csa.class_0_pending & CLASS0_INTR_MASK;

	if (likely(!stat))
		return 0;

	if (stat & CLASS0_DMA_ALIGNMENT_INTR)
		spufs_handle_event(ctx, ctx->csa.class_0_dar,
			SPE_EVENT_DMA_ALIGNMENT);

	if (stat & CLASS0_INVALID_DMA_COMMAND_INTR)
		spufs_handle_event(ctx, ctx->csa.class_0_dar,
			SPE_EVENT_INVALID_DMA);

	if (stat & CLASS0_SPU_ERROR_INTR)
		spufs_handle_event(ctx, ctx->csa.class_0_dar,
			SPE_EVENT_SPE_ERROR);

	ctx->csa.class_0_pending = 0;

	return -EIO;
}

/*
 * bottom half handler for page faults, we can't do this from
 * interrupt context, since we might need to sleep.
 * we also need to give up the mutex so we can get scheduled
 * out while waiting for the backing store.
 *
 * TODO: try calling hash_page from the interrupt handler first
 *       in order to speed up the easy case.
 */
int spufs_handle_class1(struct spu_context *ctx)
{
	u64 ea, dsisr, access;
	unsigned long flags;
	vm_fault_t flt = 0;
	int ret;

	/*
	 * dar and dsisr get passed from the registers
	 * to the spu_context, to this function, but not
	 * back to the spu if it gets scheduled again.
	 *
	 * if we don't handle the fault for a saved context
	 * in time, we can still expect to get the same fault
	 * the immediately after the context restore.
	 */
	ea = ctx->csa.class_1_dar;
	dsisr = ctx->csa.class_1_dsisr;

	if (!(dsisr & (MFC_DSISR_PTE_NOT_FOUND | MFC_DSISR_ACCESS_DENIED)))
		return 0;

	spuctx_switch_state(ctx, SPU_UTIL_IOWAIT);

	pr_debug("ctx %p: ea %016llx, dsisr %016llx state %d\n", ctx, ea,
		dsisr, ctx->state);

	ctx->stats.hash_flt++;
	if (ctx->state == SPU_STATE_RUNNABLE)
		ctx->spu->stats.hash_flt++;

	/* we must not hold the lock when entering copro_handle_mm_fault */
	spu_release(ctx);

	access = (_PAGE_PRESENT | _PAGE_READ);
	access |= (dsisr & MFC_DSISR_ACCESS_PUT) ? _PAGE_WRITE : 0UL;
	local_irq_save(flags);
	ret = hash_page(ea, access, 0x300, dsisr);
	local_irq_restore(flags);

	/* hashing failed, so try the actual fault handler */
	if (ret)
		ret = copro_handle_mm_fault(current->mm, ea, dsisr, &flt);

	/*
	 * This is nasty: we need the state_mutex for all the bookkeeping even
	 * if the syscall was interrupted by a signal. ewww.
	 */
	mutex_lock(&ctx->state_mutex);

	/*
	 * Clear dsisr under ctxt lock after handling the fault, so that
	 * time slicing will not preempt the context while the page fault
	 * handler is running. Context switch code removes mappings.
	 */
	ctx->csa.class_1_dar = ctx->csa.class_1_dsisr = 0;

	/*
	 * If we handled the fault successfully and are in runnable
	 * state, restart the DMA.
	 * In case of unhandled error report the problem to user space.
	 */
	if (!ret) {
		if (flt & VM_FAULT_MAJOR)
			ctx->stats.maj_flt++;
		else
			ctx->stats.min_flt++;
		if (ctx->state == SPU_STATE_RUNNABLE) {
			if (flt & VM_FAULT_MAJOR)
				ctx->spu->stats.maj_flt++;
			else
				ctx->spu->stats.min_flt++;
		}

		if (ctx->spu)
			ctx->ops->restart_dma(ctx);
	} else
		spufs_handle_event(ctx, ea, SPE_EVENT_SPE_DATA_STORAGE);

	spuctx_switch_state(ctx, SPU_UTIL_SYSTEM);
	return ret;
}

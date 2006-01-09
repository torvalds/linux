#include <linux/wait.h>
#include <linux/ptrace.h>

#include <asm/spu.h>

#include "spufs.h"

/* interrupt-level stop callback function. */
void spufs_stop_callback(struct spu *spu)
{
	struct spu_context *ctx = spu->ctx;

	wake_up_all(&ctx->stop_wq);
}

static inline int spu_stopped(struct spu_context *ctx, u32 * stat)
{
	struct spu *spu;
	u64 pte_fault;

	*stat = ctx->ops->status_read(ctx);
	if (ctx->state != SPU_STATE_RUNNABLE)
		return 1;
	spu = ctx->spu;
	pte_fault = spu->dsisr &
	    (MFC_DSISR_PTE_NOT_FOUND | MFC_DSISR_ACCESS_DENIED);
	return (!(*stat & 0x1) || pte_fault || spu->class_0_pending) ? 1 : 0;
}

static inline int spu_run_init(struct spu_context *ctx, u32 * npc,
			       u32 * status)
{
	int ret;

	if ((ret = spu_acquire_runnable(ctx)) != 0)
		return ret;
	ctx->ops->npc_write(ctx, *npc);
	ctx->ops->runcntl_write(ctx, SPU_RUNCNTL_RUNNABLE);
	return 0;
}

static inline int spu_run_fini(struct spu_context *ctx, u32 * npc,
			       u32 * status)
{
	int ret = 0;

	*status = ctx->ops->status_read(ctx);
	*npc = ctx->ops->npc_read(ctx);
	spu_release(ctx);

	if (signal_pending(current))
		ret = -ERESTARTSYS;
	if (unlikely(current->ptrace & PT_PTRACED)) {
		if ((*status & SPU_STATUS_STOPPED_BY_STOP)
		    && (*status >> SPU_STOP_STATUS_SHIFT) == 0x3fff) {
			force_sig(SIGTRAP, current);
			ret = -ERESTARTSYS;
		}
	}
	return ret;
}

static inline int spu_reacquire_runnable(struct spu_context *ctx, u32 *npc,
				         u32 *status)
{
	int ret;

	if ((ret = spu_run_fini(ctx, npc, status)) != 0)
		return ret;
	if (*status & (SPU_STATUS_STOPPED_BY_STOP |
		       SPU_STATUS_STOPPED_BY_HALT)) {
		return *status;
	}
	if ((ret = spu_run_init(ctx, npc, status)) != 0)
		return ret;
	return 0;
}

static inline int spu_process_events(struct spu_context *ctx)
{
	struct spu *spu = ctx->spu;
	u64 pte_fault = MFC_DSISR_PTE_NOT_FOUND | MFC_DSISR_ACCESS_DENIED;
	int ret = 0;

	if (spu->dsisr & pte_fault)
		ret = spu_irq_class_1_bottom(spu);
	if (spu->class_0_pending)
		ret = spu_irq_class_0_bottom(spu);
	if (!ret && signal_pending(current))
		ret = -ERESTARTSYS;
	return ret;
}

long spufs_run_spu(struct file *file, struct spu_context *ctx,
		   u32 * npc, u32 * status)
{
	int ret;

	if (down_interruptible(&ctx->run_sema))
		return -ERESTARTSYS;

	ret = spu_run_init(ctx, npc, status);
	if (ret)
		goto out;

	do {
		ret = spufs_wait(ctx->stop_wq, spu_stopped(ctx, status));
		if (unlikely(ret))
			break;
		if (unlikely(ctx->state != SPU_STATE_RUNNABLE)) {
			ret = spu_reacquire_runnable(ctx, npc, status);
			if (ret)
				goto out;
			continue;
		}
		ret = spu_process_events(ctx);

	} while (!ret && !(*status & (SPU_STATUS_STOPPED_BY_STOP |
				      SPU_STATUS_STOPPED_BY_HALT)));

	ctx->ops->runcntl_stop(ctx);
	ret = spu_run_fini(ctx, npc, status);
	if (!ret)
		ret = *status;
	spu_yield(ctx);

out:
	up(&ctx->run_sema);
	return ret;
}


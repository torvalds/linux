#define DEBUG

#include <linux/wait.h>
#include <linux/ptrace.h>

#include <asm/spu.h>
#include <asm/unistd.h>

#include "spufs.h"

/* interrupt-level stop callback function. */
void spufs_stop_callback(struct spu *spu)
{
	struct spu_context *ctx = spu->ctx;

	wake_up_all(&ctx->stop_wq);
}

void spufs_dma_callback(struct spu *spu, int type)
{
	struct spu_context *ctx = spu->ctx;

	if (ctx->flags & SPU_CREATE_EVENTS_ENABLED) {
		ctx->event_return |= type;
		wake_up_all(&ctx->stop_wq);
	} else {
		switch (type) {
		case SPE_EVENT_DMA_ALIGNMENT:
		case SPE_EVENT_SPE_DATA_STORAGE:
		case SPE_EVENT_INVALID_DMA:
			force_sig(SIGBUS, /* info, */ current);
			break;
		case SPE_EVENT_SPE_ERROR:
			force_sig(SIGILL, /* info */ current);
			break;
		}
	}
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

static inline int spu_run_init(struct spu_context *ctx, u32 * npc)
{
	int ret;
	unsigned long runcntl = SPU_RUNCNTL_RUNNABLE;

	if ((ret = spu_acquire_runnable(ctx)) != 0)
		return ret;

	/* if we're in isolated mode, we would have started the SPU
	 * earlier, so don't do it again now. */
	if (!(ctx->flags & SPU_CREATE_ISOLATE)) {
		ctx->ops->npc_write(ctx, *npc);
		ctx->ops->runcntl_write(ctx, runcntl);
	}
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
	if ((ret = spu_run_init(ctx, npc)) != 0)
		return ret;
	return 0;
}

/*
 * SPU syscall restarting is tricky because we violate the basic
 * assumption that the signal handler is running on the interrupted
 * thread. Here instead, the handler runs on PowerPC user space code,
 * while the syscall was called from the SPU.
 * This means we can only do a very rough approximation of POSIX
 * signal semantics.
 */
int spu_handle_restartsys(struct spu_context *ctx, long *spu_ret,
			  unsigned int *npc)
{
	int ret;

	switch (*spu_ret) {
	case -ERESTARTSYS:
	case -ERESTARTNOINTR:
		/*
		 * Enter the regular syscall restarting for
		 * sys_spu_run, then restart the SPU syscall
		 * callback.
		 */
		*npc -= 8;
		ret = -ERESTARTSYS;
		break;
	case -ERESTARTNOHAND:
	case -ERESTART_RESTARTBLOCK:
		/*
		 * Restart block is too hard for now, just return -EINTR
		 * to the SPU.
		 * ERESTARTNOHAND comes from sys_pause, we also return
		 * -EINTR from there.
		 * Assume that we need to be restarted ourselves though.
		 */
		*spu_ret = -EINTR;
		ret = -ERESTARTSYS;
		break;
	default:
		printk(KERN_WARNING "%s: unexpected return code %ld\n",
			__FUNCTION__, *spu_ret);
		ret = 0;
	}
	return ret;
}

int spu_process_callback(struct spu_context *ctx)
{
	struct spu_syscall_block s;
	u32 ls_pointer, npc;
	char *ls;
	long spu_ret;
	int ret;

	/* get syscall block from local store */
	npc = ctx->ops->npc_read(ctx);
	ls = ctx->ops->get_ls(ctx);
	ls_pointer = *(u32*)(ls + npc);
	if (ls_pointer > (LS_SIZE - sizeof(s)))
		return -EFAULT;
	memcpy(&s, ls + ls_pointer, sizeof (s));

	/* do actual syscall without pinning the spu */
	ret = 0;
	spu_ret = -ENOSYS;
	npc += 4;

	if (s.nr_ret < __NR_syscalls) {
		spu_release(ctx);
		/* do actual system call from here */
		spu_ret = spu_sys_callback(&s);
		if (spu_ret <= -ERESTARTSYS) {
			ret = spu_handle_restartsys(ctx, &spu_ret, &npc);
		}
		spu_acquire(ctx);
		if (ret == -ERESTARTSYS)
			return ret;
	}

	/* write result, jump over indirect pointer */
	memcpy(ls + ls_pointer, &spu_ret, sizeof (spu_ret));
	ctx->ops->npc_write(ctx, npc);
	ctx->ops->runcntl_write(ctx, SPU_RUNCNTL_RUNNABLE);
	return ret;
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
		   u32 *npc, u32 *event)
{
	int ret;
	u32 status;

	if (down_interruptible(&ctx->run_sema))
		return -ERESTARTSYS;

	ctx->ops->master_start(ctx);
	ctx->event_return = 0;
	ret = spu_run_init(ctx, npc);
	if (ret)
		goto out;

	do {
		ret = spufs_wait(ctx->stop_wq, spu_stopped(ctx, &status));
		if (unlikely(ret))
			break;
		if ((status & SPU_STATUS_STOPPED_BY_STOP) &&
		    (status >> SPU_STOP_STATUS_SHIFT == 0x2104)) {
			ret = spu_process_callback(ctx);
			if (ret)
				break;
			status &= ~SPU_STATUS_STOPPED_BY_STOP;
		}
		if (unlikely(ctx->state != SPU_STATE_RUNNABLE)) {
			ret = spu_reacquire_runnable(ctx, npc, &status);
			if (ret)
				goto out2;
			continue;
		}
		ret = spu_process_events(ctx);

	} while (!ret && !(status & (SPU_STATUS_STOPPED_BY_STOP |
				      SPU_STATUS_STOPPED_BY_HALT)));

	ctx->ops->master_stop(ctx);
	ret = spu_run_fini(ctx, npc, &status);
	spu_yield(ctx);

out2:
	if ((ret == 0) ||
	    ((ret == -ERESTARTSYS) &&
	     ((status & SPU_STATUS_STOPPED_BY_HALT) ||
	      ((status & SPU_STATUS_STOPPED_BY_STOP) &&
	       (status >> SPU_STOP_STATUS_SHIFT != 0x2104)))))
		ret = status;

	if (unlikely(current->ptrace & PT_PTRACED)) {
		if ((status & SPU_STATUS_STOPPED_BY_STOP)
		    && (status >> SPU_STOP_STATUS_SHIFT) == 0x3fff) {
			force_sig(SIGTRAP, current);
			ret = -ERESTARTSYS;
		}
	}

out:
	*event = ctx->event_return;
	up(&ctx->run_sema);
	return ret;
}


#define DEBUG

#include <linux/wait.h>
#include <linux/ptrace.h>

#include <asm/spu.h>
#include <asm/spu_priv1.h>
#include <asm/io.h>
#include <asm/unistd.h>

#include "spufs.h"

/* interrupt-level stop callback function. */
void spufs_stop_callback(struct spu *spu)
{
	struct spu_context *ctx = spu->ctx;

	/*
	 * It should be impossible to preempt a context while an exception
	 * is being processed, since the context switch code is specially
	 * coded to deal with interrupts ... But, just in case, sanity check
	 * the context pointer.  It is OK to return doing nothing since
	 * the exception will be regenerated when the context is resumed.
	 */
	if (ctx) {
		/* Copy exception arguments into module specific structure */
		ctx->csa.class_0_pending = spu->class_0_pending;
		ctx->csa.dsisr = spu->dsisr;
		ctx->csa.dar = spu->dar;

		/* ensure that the exception status has hit memory before a
		 * thread waiting on the context's stop queue is woken */
		smp_wmb();

		wake_up_all(&ctx->stop_wq);
	}

	/* Clear callback arguments from spu structure */
	spu->class_0_pending = 0;
	spu->dsisr = 0;
	spu->dar = 0;
}

int spu_stopped(struct spu_context *ctx, u32 *stat)
{
	u64 dsisr;
	u32 stopped;

	*stat = ctx->ops->status_read(ctx);

	if (test_bit(SPU_SCHED_NOTIFY_ACTIVE, &ctx->sched_flags))
		return 1;

	stopped = SPU_STATUS_INVALID_INSTR | SPU_STATUS_SINGLE_STEP |
		SPU_STATUS_STOPPED_BY_HALT | SPU_STATUS_STOPPED_BY_STOP;
	if (*stat & stopped)
		return 1;

	dsisr = ctx->csa.dsisr;
	if (dsisr & (MFC_DSISR_PTE_NOT_FOUND | MFC_DSISR_ACCESS_DENIED))
		return 1;

	if (ctx->csa.class_0_pending)
		return 1;

	return 0;
}

static int spu_setup_isolated(struct spu_context *ctx)
{
	int ret;
	u64 __iomem *mfc_cntl;
	u64 sr1;
	u32 status;
	unsigned long timeout;
	const u32 status_loading = SPU_STATUS_RUNNING
		| SPU_STATUS_ISOLATED_STATE | SPU_STATUS_ISOLATED_LOAD_STATUS;

	ret = -ENODEV;
	if (!isolated_loader)
		goto out;

	/*
	 * We need to exclude userspace access to the context.
	 *
	 * To protect against memory access we invalidate all ptes
	 * and make sure the pagefault handlers block on the mutex.
	 */
	spu_unmap_mappings(ctx);

	mfc_cntl = &ctx->spu->priv2->mfc_control_RW;

	/* purge the MFC DMA queue to ensure no spurious accesses before we
	 * enter kernel mode */
	timeout = jiffies + HZ;
	out_be64(mfc_cntl, MFC_CNTL_PURGE_DMA_REQUEST);
	while ((in_be64(mfc_cntl) & MFC_CNTL_PURGE_DMA_STATUS_MASK)
			!= MFC_CNTL_PURGE_DMA_COMPLETE) {
		if (time_after(jiffies, timeout)) {
			printk(KERN_ERR "%s: timeout flushing MFC DMA queue\n",
					__FUNCTION__);
			ret = -EIO;
			goto out;
		}
		cond_resched();
	}

	/* put the SPE in kernel mode to allow access to the loader */
	sr1 = spu_mfc_sr1_get(ctx->spu);
	sr1 &= ~MFC_STATE1_PROBLEM_STATE_MASK;
	spu_mfc_sr1_set(ctx->spu, sr1);

	/* start the loader */
	ctx->ops->signal1_write(ctx, (unsigned long)isolated_loader >> 32);
	ctx->ops->signal2_write(ctx,
			(unsigned long)isolated_loader & 0xffffffff);

	ctx->ops->runcntl_write(ctx,
			SPU_RUNCNTL_RUNNABLE | SPU_RUNCNTL_ISOLATE);

	ret = 0;
	timeout = jiffies + HZ;
	while (((status = ctx->ops->status_read(ctx)) & status_loading) ==
				status_loading) {
		if (time_after(jiffies, timeout)) {
			printk(KERN_ERR "%s: timeout waiting for loader\n",
					__FUNCTION__);
			ret = -EIO;
			goto out_drop_priv;
		}
		cond_resched();
	}

	if (!(status & SPU_STATUS_RUNNING)) {
		/* If isolated LOAD has failed: run SPU, we will get a stop-and
		 * signal later. */
		pr_debug("%s: isolated LOAD failed\n", __FUNCTION__);
		ctx->ops->runcntl_write(ctx, SPU_RUNCNTL_RUNNABLE);
		ret = -EACCES;
		goto out_drop_priv;
	}

	if (!(status & SPU_STATUS_ISOLATED_STATE)) {
		/* This isn't allowed by the CBEA, but check anyway */
		pr_debug("%s: SPU fell out of isolated mode?\n", __FUNCTION__);
		ctx->ops->runcntl_write(ctx, SPU_RUNCNTL_STOP);
		ret = -EINVAL;
		goto out_drop_priv;
	}

out_drop_priv:
	/* Finished accessing the loader. Drop kernel mode */
	sr1 |= MFC_STATE1_PROBLEM_STATE_MASK;
	spu_mfc_sr1_set(ctx->spu, sr1);

out:
	return ret;
}

static int spu_run_init(struct spu_context *ctx, u32 *npc)
{
	unsigned long runcntl = SPU_RUNCNTL_RUNNABLE;
	int ret;

	spuctx_switch_state(ctx, SPU_UTIL_SYSTEM);

	/*
	 * NOSCHED is synchronous scheduling with respect to the caller.
	 * The caller waits for the context to be loaded.
	 */
	if (ctx->flags & SPU_CREATE_NOSCHED) {
		if (ctx->state == SPU_STATE_SAVED) {
			ret = spu_activate(ctx, 0);
			if (ret)
				return ret;
		}
	}

	/*
	 * Apply special setup as required.
	 */
	if (ctx->flags & SPU_CREATE_ISOLATE) {
		if (!(ctx->ops->status_read(ctx) & SPU_STATUS_ISOLATED_STATE)) {
			ret = spu_setup_isolated(ctx);
			if (ret)
				return ret;
		}

		/*
		 * If userspace has set the runcntrl register (eg, to
		 * issue an isolated exit), we need to re-set it here
		 */
		runcntl = ctx->ops->runcntl_read(ctx) &
			(SPU_RUNCNTL_RUNNABLE | SPU_RUNCNTL_ISOLATE);
		if (runcntl == 0)
			runcntl = SPU_RUNCNTL_RUNNABLE;
	}

	if (ctx->flags & SPU_CREATE_NOSCHED) {
		spuctx_switch_state(ctx, SPU_UTIL_USER);
		ctx->ops->runcntl_write(ctx, runcntl);
	} else {
		unsigned long privcntl;

		if (test_thread_flag(TIF_SINGLESTEP))
			privcntl = SPU_PRIVCNTL_MODE_SINGLE_STEP;
		else
			privcntl = SPU_PRIVCNTL_MODE_NORMAL;

		ctx->ops->npc_write(ctx, *npc);
		ctx->ops->privcntl_write(ctx, privcntl);
		ctx->ops->runcntl_write(ctx, runcntl);

		if (ctx->state == SPU_STATE_SAVED) {
			ret = spu_activate(ctx, 0);
			if (ret)
				return ret;
		} else {
			spuctx_switch_state(ctx, SPU_UTIL_USER);
		}
	}

	return 0;
}

static int spu_run_fini(struct spu_context *ctx, u32 *npc,
			       u32 *status)
{
	int ret = 0;

	spu_del_from_rq(ctx);

	*status = ctx->ops->status_read(ctx);
	*npc = ctx->ops->npc_read(ctx);

	spuctx_switch_state(ctx, SPU_UTIL_IDLE_LOADED);
	spu_release(ctx);

	if (signal_pending(current))
		ret = -ERESTARTSYS;

	return ret;
}

/*
 * SPU syscall restarting is tricky because we violate the basic
 * assumption that the signal handler is running on the interrupted
 * thread. Here instead, the handler runs on PowerPC user space code,
 * while the syscall was called from the SPU.
 * This means we can only do a very rough approximation of POSIX
 * signal semantics.
 */
static int spu_handle_restartsys(struct spu_context *ctx, long *spu_ret,
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

static int spu_process_callback(struct spu_context *ctx)
{
	struct spu_syscall_block s;
	u32 ls_pointer, npc;
	void __iomem *ls;
	long spu_ret;
	int ret, ret2;

	/* get syscall block from local store */
	npc = ctx->ops->npc_read(ctx) & ~3;
	ls = (void __iomem *)ctx->ops->get_ls(ctx);
	ls_pointer = in_be32(ls + npc);
	if (ls_pointer > (LS_SIZE - sizeof(s)))
		return -EFAULT;
	memcpy_fromio(&s, ls + ls_pointer, sizeof(s));

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
		ret2 = spu_acquire(ctx);
		if (ret == -ERESTARTSYS)
			return ret;
		if (ret2)
			return -EINTR;
	}

	/* write result, jump over indirect pointer */
	memcpy_toio(ls + ls_pointer, &spu_ret, sizeof(spu_ret));
	ctx->ops->npc_write(ctx, npc);
	ctx->ops->runcntl_write(ctx, SPU_RUNCNTL_RUNNABLE);
	return ret;
}

long spufs_run_spu(struct spu_context *ctx, u32 *npc, u32 *event)
{
	int ret;
	struct spu *spu;
	u32 status;

	if (mutex_lock_interruptible(&ctx->run_mutex))
		return -ERESTARTSYS;

	spu_enable_spu(ctx);
	ctx->event_return = 0;

	ret = spu_acquire(ctx);
	if (ret)
		goto out_unlock;

	spu_update_sched_info(ctx);

	ret = spu_run_init(ctx, npc);
	if (ret) {
		spu_release(ctx);
		goto out;
	}

	do {
		ret = spufs_wait(ctx->stop_wq, spu_stopped(ctx, &status));
		if (unlikely(ret))
			break;
		spu = ctx->spu;
		if (unlikely(test_and_clear_bit(SPU_SCHED_NOTIFY_ACTIVE,
						&ctx->sched_flags))) {
			if (!(status & SPU_STATUS_STOPPED_BY_STOP)) {
				spu_switch_notify(spu, ctx);
				continue;
			}
		}

		spuctx_switch_state(ctx, SPU_UTIL_SYSTEM);

		if ((status & SPU_STATUS_STOPPED_BY_STOP) &&
		    (status >> SPU_STOP_STATUS_SHIFT == 0x2104)) {
			ret = spu_process_callback(ctx);
			if (ret)
				break;
			status &= ~SPU_STATUS_STOPPED_BY_STOP;
		}
		ret = spufs_handle_class1(ctx);
		if (ret)
			break;

		ret = spufs_handle_class0(ctx);
		if (ret)
			break;

		if (signal_pending(current))
			ret = -ERESTARTSYS;
	} while (!ret && !(status & (SPU_STATUS_STOPPED_BY_STOP |
				      SPU_STATUS_STOPPED_BY_HALT |
				       SPU_STATUS_SINGLE_STEP)));

	if ((status & SPU_STATUS_STOPPED_BY_STOP) &&
	    (((status >> SPU_STOP_STATUS_SHIFT) & 0x3f00) == 0x2100) &&
	    (ctx->state == SPU_STATE_RUNNABLE))
		ctx->stats.libassist++;


	spu_disable_spu(ctx);
	ret = spu_run_fini(ctx, npc, &status);
	spu_yield(ctx);

	if ((ret == 0) ||
	    ((ret == -ERESTARTSYS) &&
	     ((status & SPU_STATUS_STOPPED_BY_HALT) ||
	      (status & SPU_STATUS_SINGLE_STEP) ||
	      ((status & SPU_STATUS_STOPPED_BY_STOP) &&
	       (status >> SPU_STOP_STATUS_SHIFT != 0x2104)))))
		ret = status;

	/* Note: we don't need to force_sig SIGTRAP on single-step
	 * since we have TIF_SINGLESTEP set, thus the kernel will do
	 * it upon return from the syscall anyawy
	 */
	if (unlikely(status & SPU_STATUS_SINGLE_STEP))
		ret = -ERESTARTSYS;

	else if (unlikely((status & SPU_STATUS_STOPPED_BY_STOP)
	    && (status >> SPU_STOP_STATUS_SHIFT) == 0x3fff)) {
		force_sig(SIGTRAP, current);
		ret = -ERESTARTSYS;
	}

out:
	*event = ctx->event_return;
out_unlock:
	mutex_unlock(&ctx->run_mutex);
	return ret;
}

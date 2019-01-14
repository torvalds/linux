/*
 * Copyright © 2014 Broadcom
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/sched/signal.h>
#include <linux/dma-fence-array.h>

#include "uapi/drm/vc4_drm.h"
#include "vc4_drv.h"
#include "vc4_regs.h"
#include "vc4_trace.h"

static void
vc4_queue_hangcheck(struct drm_device *dev)
{
	struct vc4_dev *vc4 = to_vc4_dev(dev);

	mod_timer(&vc4->hangcheck.timer,
		  round_jiffies_up(jiffies + msecs_to_jiffies(100)));
}

struct vc4_hang_state {
	struct drm_vc4_get_hang_state user_state;

	u32 bo_count;
	struct drm_gem_object **bo;
};

static void
vc4_free_hang_state(struct drm_device *dev, struct vc4_hang_state *state)
{
	unsigned int i;

	for (i = 0; i < state->user_state.bo_count; i++)
		drm_gem_object_put_unlocked(state->bo[i]);

	kfree(state);
}

int
vc4_get_hang_state_ioctl(struct drm_device *dev, void *data,
			 struct drm_file *file_priv)
{
	struct drm_vc4_get_hang_state *get_state = data;
	struct drm_vc4_get_hang_state_bo *bo_state;
	struct vc4_hang_state *kernel_state;
	struct drm_vc4_get_hang_state *state;
	struct vc4_dev *vc4 = to_vc4_dev(dev);
	unsigned long irqflags;
	u32 i;
	int ret = 0;

	spin_lock_irqsave(&vc4->job_lock, irqflags);
	kernel_state = vc4->hang_state;
	if (!kernel_state) {
		spin_unlock_irqrestore(&vc4->job_lock, irqflags);
		return -ENOENT;
	}
	state = &kernel_state->user_state;

	/* If the user's array isn't big enough, just return the
	 * required array size.
	 */
	if (get_state->bo_count < state->bo_count) {
		get_state->bo_count = state->bo_count;
		spin_unlock_irqrestore(&vc4->job_lock, irqflags);
		return 0;
	}

	vc4->hang_state = NULL;
	spin_unlock_irqrestore(&vc4->job_lock, irqflags);

	/* Save the user's BO pointer, so we don't stomp it with the memcpy. */
	state->bo = get_state->bo;
	memcpy(get_state, state, sizeof(*state));

	bo_state = kcalloc(state->bo_count, sizeof(*bo_state), GFP_KERNEL);
	if (!bo_state) {
		ret = -ENOMEM;
		goto err_free;
	}

	for (i = 0; i < state->bo_count; i++) {
		struct vc4_bo *vc4_bo = to_vc4_bo(kernel_state->bo[i]);
		u32 handle;

		ret = drm_gem_handle_create(file_priv, kernel_state->bo[i],
					    &handle);

		if (ret) {
			state->bo_count = i;
			goto err_delete_handle;
		}
		bo_state[i].handle = handle;
		bo_state[i].paddr = vc4_bo->base.paddr;
		bo_state[i].size = vc4_bo->base.base.size;
	}

	if (copy_to_user(u64_to_user_ptr(get_state->bo),
			 bo_state,
			 state->bo_count * sizeof(*bo_state)))
		ret = -EFAULT;

err_delete_handle:
	if (ret) {
		for (i = 0; i < state->bo_count; i++)
			drm_gem_handle_delete(file_priv, bo_state[i].handle);
	}

err_free:
	vc4_free_hang_state(dev, kernel_state);
	kfree(bo_state);

	return ret;
}

static void
vc4_save_hang_state(struct drm_device *dev)
{
	struct vc4_dev *vc4 = to_vc4_dev(dev);
	struct drm_vc4_get_hang_state *state;
	struct vc4_hang_state *kernel_state;
	struct vc4_exec_info *exec[2];
	struct vc4_bo *bo;
	unsigned long irqflags;
	unsigned int i, j, k, unref_list_count;

	kernel_state = kcalloc(1, sizeof(*kernel_state), GFP_KERNEL);
	if (!kernel_state)
		return;

	state = &kernel_state->user_state;

	spin_lock_irqsave(&vc4->job_lock, irqflags);
	exec[0] = vc4_first_bin_job(vc4);
	exec[1] = vc4_first_render_job(vc4);
	if (!exec[0] && !exec[1]) {
		spin_unlock_irqrestore(&vc4->job_lock, irqflags);
		return;
	}

	/* Get the bos from both binner and renderer into hang state. */
	state->bo_count = 0;
	for (i = 0; i < 2; i++) {
		if (!exec[i])
			continue;

		unref_list_count = 0;
		list_for_each_entry(bo, &exec[i]->unref_list, unref_head)
			unref_list_count++;
		state->bo_count += exec[i]->bo_count + unref_list_count;
	}

	kernel_state->bo = kcalloc(state->bo_count,
				   sizeof(*kernel_state->bo), GFP_ATOMIC);

	if (!kernel_state->bo) {
		spin_unlock_irqrestore(&vc4->job_lock, irqflags);
		return;
	}

	k = 0;
	for (i = 0; i < 2; i++) {
		if (!exec[i])
			continue;

		for (j = 0; j < exec[i]->bo_count; j++) {
			bo = to_vc4_bo(&exec[i]->bo[j]->base);

			/* Retain BOs just in case they were marked purgeable.
			 * This prevents the BO from being purged before
			 * someone had a chance to dump the hang state.
			 */
			WARN_ON(!refcount_read(&bo->usecnt));
			refcount_inc(&bo->usecnt);
			drm_gem_object_get(&exec[i]->bo[j]->base);
			kernel_state->bo[k++] = &exec[i]->bo[j]->base;
		}

		list_for_each_entry(bo, &exec[i]->unref_list, unref_head) {
			/* No need to retain BOs coming from the ->unref_list
			 * because they are naturally unpurgeable.
			 */
			drm_gem_object_get(&bo->base.base);
			kernel_state->bo[k++] = &bo->base.base;
		}
	}

	WARN_ON_ONCE(k != state->bo_count);

	if (exec[0])
		state->start_bin = exec[0]->ct0ca;
	if (exec[1])
		state->start_render = exec[1]->ct1ca;

	spin_unlock_irqrestore(&vc4->job_lock, irqflags);

	state->ct0ca = V3D_READ(V3D_CTNCA(0));
	state->ct0ea = V3D_READ(V3D_CTNEA(0));

	state->ct1ca = V3D_READ(V3D_CTNCA(1));
	state->ct1ea = V3D_READ(V3D_CTNEA(1));

	state->ct0cs = V3D_READ(V3D_CTNCS(0));
	state->ct1cs = V3D_READ(V3D_CTNCS(1));

	state->ct0ra0 = V3D_READ(V3D_CT00RA0);
	state->ct1ra0 = V3D_READ(V3D_CT01RA0);

	state->bpca = V3D_READ(V3D_BPCA);
	state->bpcs = V3D_READ(V3D_BPCS);
	state->bpoa = V3D_READ(V3D_BPOA);
	state->bpos = V3D_READ(V3D_BPOS);

	state->vpmbase = V3D_READ(V3D_VPMBASE);

	state->dbge = V3D_READ(V3D_DBGE);
	state->fdbgo = V3D_READ(V3D_FDBGO);
	state->fdbgb = V3D_READ(V3D_FDBGB);
	state->fdbgr = V3D_READ(V3D_FDBGR);
	state->fdbgs = V3D_READ(V3D_FDBGS);
	state->errstat = V3D_READ(V3D_ERRSTAT);

	/* We need to turn purgeable BOs into unpurgeable ones so that
	 * userspace has a chance to dump the hang state before the kernel
	 * decides to purge those BOs.
	 * Note that BO consistency at dump time cannot be guaranteed. For
	 * example, if the owner of these BOs decides to re-use them or mark
	 * them purgeable again there's nothing we can do to prevent it.
	 */
	for (i = 0; i < kernel_state->user_state.bo_count; i++) {
		struct vc4_bo *bo = to_vc4_bo(kernel_state->bo[i]);

		if (bo->madv == __VC4_MADV_NOTSUPP)
			continue;

		mutex_lock(&bo->madv_lock);
		if (!WARN_ON(bo->madv == __VC4_MADV_PURGED))
			bo->madv = VC4_MADV_WILLNEED;
		refcount_dec(&bo->usecnt);
		mutex_unlock(&bo->madv_lock);
	}

	spin_lock_irqsave(&vc4->job_lock, irqflags);
	if (vc4->hang_state) {
		spin_unlock_irqrestore(&vc4->job_lock, irqflags);
		vc4_free_hang_state(dev, kernel_state);
	} else {
		vc4->hang_state = kernel_state;
		spin_unlock_irqrestore(&vc4->job_lock, irqflags);
	}
}

static void
vc4_reset(struct drm_device *dev)
{
	struct vc4_dev *vc4 = to_vc4_dev(dev);

	DRM_INFO("Resetting GPU.\n");

	mutex_lock(&vc4->power_lock);
	if (vc4->power_refcount) {
		/* Power the device off and back on the by dropping the
		 * reference on runtime PM.
		 */
		pm_runtime_put_sync_suspend(&vc4->v3d->pdev->dev);
		pm_runtime_get_sync(&vc4->v3d->pdev->dev);
	}
	mutex_unlock(&vc4->power_lock);

	vc4_irq_reset(dev);

	/* Rearm the hangcheck -- another job might have been waiting
	 * for our hung one to get kicked off, and vc4_irq_reset()
	 * would have started it.
	 */
	vc4_queue_hangcheck(dev);
}

static void
vc4_reset_work(struct work_struct *work)
{
	struct vc4_dev *vc4 =
		container_of(work, struct vc4_dev, hangcheck.reset_work);

	vc4_save_hang_state(vc4->dev);

	vc4_reset(vc4->dev);
}

static void
vc4_hangcheck_elapsed(struct timer_list *t)
{
	struct vc4_dev *vc4 = from_timer(vc4, t, hangcheck.timer);
	struct drm_device *dev = vc4->dev;
	uint32_t ct0ca, ct1ca;
	unsigned long irqflags;
	struct vc4_exec_info *bin_exec, *render_exec;

	spin_lock_irqsave(&vc4->job_lock, irqflags);

	bin_exec = vc4_first_bin_job(vc4);
	render_exec = vc4_first_render_job(vc4);

	/* If idle, we can stop watching for hangs. */
	if (!bin_exec && !render_exec) {
		spin_unlock_irqrestore(&vc4->job_lock, irqflags);
		return;
	}

	ct0ca = V3D_READ(V3D_CTNCA(0));
	ct1ca = V3D_READ(V3D_CTNCA(1));

	/* If we've made any progress in execution, rearm the timer
	 * and wait.
	 */
	if ((bin_exec && ct0ca != bin_exec->last_ct0ca) ||
	    (render_exec && ct1ca != render_exec->last_ct1ca)) {
		if (bin_exec)
			bin_exec->last_ct0ca = ct0ca;
		if (render_exec)
			render_exec->last_ct1ca = ct1ca;
		spin_unlock_irqrestore(&vc4->job_lock, irqflags);
		vc4_queue_hangcheck(dev);
		return;
	}

	spin_unlock_irqrestore(&vc4->job_lock, irqflags);

	/* We've gone too long with no progress, reset.  This has to
	 * be done from a work struct, since resetting can sleep and
	 * this timer hook isn't allowed to.
	 */
	schedule_work(&vc4->hangcheck.reset_work);
}

static void
submit_cl(struct drm_device *dev, uint32_t thread, uint32_t start, uint32_t end)
{
	struct vc4_dev *vc4 = to_vc4_dev(dev);

	/* Set the current and end address of the control list.
	 * Writing the end register is what starts the job.
	 */
	V3D_WRITE(V3D_CTNCA(thread), start);
	V3D_WRITE(V3D_CTNEA(thread), end);
}

int
vc4_wait_for_seqno(struct drm_device *dev, uint64_t seqno, uint64_t timeout_ns,
		   bool interruptible)
{
	struct vc4_dev *vc4 = to_vc4_dev(dev);
	int ret = 0;
	unsigned long timeout_expire;
	DEFINE_WAIT(wait);

	if (vc4->finished_seqno >= seqno)
		return 0;

	if (timeout_ns == 0)
		return -ETIME;

	timeout_expire = jiffies + nsecs_to_jiffies(timeout_ns);

	trace_vc4_wait_for_seqno_begin(dev, seqno, timeout_ns);
	for (;;) {
		prepare_to_wait(&vc4->job_wait_queue, &wait,
				interruptible ? TASK_INTERRUPTIBLE :
				TASK_UNINTERRUPTIBLE);

		if (interruptible && signal_pending(current)) {
			ret = -ERESTARTSYS;
			break;
		}

		if (vc4->finished_seqno >= seqno)
			break;

		if (timeout_ns != ~0ull) {
			if (time_after_eq(jiffies, timeout_expire)) {
				ret = -ETIME;
				break;
			}
			schedule_timeout(timeout_expire - jiffies);
		} else {
			schedule();
		}
	}

	finish_wait(&vc4->job_wait_queue, &wait);
	trace_vc4_wait_for_seqno_end(dev, seqno);

	return ret;
}

static void
vc4_flush_caches(struct drm_device *dev)
{
	struct vc4_dev *vc4 = to_vc4_dev(dev);

	/* Flush the GPU L2 caches.  These caches sit on top of system
	 * L3 (the 128kb or so shared with the CPU), and are
	 * non-allocating in the L3.
	 */
	V3D_WRITE(V3D_L2CACTL,
		  V3D_L2CACTL_L2CCLR);

	V3D_WRITE(V3D_SLCACTL,
		  VC4_SET_FIELD(0xf, V3D_SLCACTL_T1CC) |
		  VC4_SET_FIELD(0xf, V3D_SLCACTL_T0CC) |
		  VC4_SET_FIELD(0xf, V3D_SLCACTL_UCC) |
		  VC4_SET_FIELD(0xf, V3D_SLCACTL_ICC));
}

static void
vc4_flush_texture_caches(struct drm_device *dev)
{
	struct vc4_dev *vc4 = to_vc4_dev(dev);

	V3D_WRITE(V3D_L2CACTL,
		  V3D_L2CACTL_L2CCLR);

	V3D_WRITE(V3D_SLCACTL,
		  VC4_SET_FIELD(0xf, V3D_SLCACTL_T1CC) |
		  VC4_SET_FIELD(0xf, V3D_SLCACTL_T0CC));
}

/* Sets the registers for the next job to be actually be executed in
 * the hardware.
 *
 * The job_lock should be held during this.
 */
void
vc4_submit_next_bin_job(struct drm_device *dev)
{
	struct vc4_dev *vc4 = to_vc4_dev(dev);
	struct vc4_exec_info *exec;

again:
	exec = vc4_first_bin_job(vc4);
	if (!exec)
		return;

	vc4_flush_caches(dev);

	/* Only start the perfmon if it was not already started by a previous
	 * job.
	 */
	if (exec->perfmon && vc4->active_perfmon != exec->perfmon)
		vc4_perfmon_start(vc4, exec->perfmon);

	/* Either put the job in the binner if it uses the binner, or
	 * immediately move it to the to-be-rendered queue.
	 */
	if (exec->ct0ca != exec->ct0ea) {
		submit_cl(dev, 0, exec->ct0ca, exec->ct0ea);
	} else {
		struct vc4_exec_info *next;

		vc4_move_job_to_render(dev, exec);
		next = vc4_first_bin_job(vc4);

		/* We can't start the next bin job if the previous job had a
		 * different perfmon instance attached to it. The same goes
		 * if one of them had a perfmon attached to it and the other
		 * one doesn't.
		 */
		if (next && next->perfmon == exec->perfmon)
			goto again;
	}
}

void
vc4_submit_next_render_job(struct drm_device *dev)
{
	struct vc4_dev *vc4 = to_vc4_dev(dev);
	struct vc4_exec_info *exec = vc4_first_render_job(vc4);

	if (!exec)
		return;

	/* A previous RCL may have written to one of our textures, and
	 * our full cache flush at bin time may have occurred before
	 * that RCL completed.  Flush the texture cache now, but not
	 * the instructions or uniforms (since we don't write those
	 * from an RCL).
	 */
	vc4_flush_texture_caches(dev);

	submit_cl(dev, 1, exec->ct1ca, exec->ct1ea);
}

void
vc4_move_job_to_render(struct drm_device *dev, struct vc4_exec_info *exec)
{
	struct vc4_dev *vc4 = to_vc4_dev(dev);
	bool was_empty = list_empty(&vc4->render_job_list);

	list_move_tail(&exec->head, &vc4->render_job_list);
	if (was_empty)
		vc4_submit_next_render_job(dev);
}

static void
vc4_update_bo_seqnos(struct vc4_exec_info *exec, uint64_t seqno)
{
	struct vc4_bo *bo;
	unsigned i;

	for (i = 0; i < exec->bo_count; i++) {
		bo = to_vc4_bo(&exec->bo[i]->base);
		bo->seqno = seqno;

		reservation_object_add_shared_fence(bo->resv, exec->fence);
	}

	list_for_each_entry(bo, &exec->unref_list, unref_head) {
		bo->seqno = seqno;
	}

	for (i = 0; i < exec->rcl_write_bo_count; i++) {
		bo = to_vc4_bo(&exec->rcl_write_bo[i]->base);
		bo->write_seqno = seqno;

		reservation_object_add_excl_fence(bo->resv, exec->fence);
	}
}

static void
vc4_unlock_bo_reservations(struct drm_device *dev,
			   struct vc4_exec_info *exec,
			   struct ww_acquire_ctx *acquire_ctx)
{
	int i;

	for (i = 0; i < exec->bo_count; i++) {
		struct vc4_bo *bo = to_vc4_bo(&exec->bo[i]->base);

		ww_mutex_unlock(&bo->resv->lock);
	}

	ww_acquire_fini(acquire_ctx);
}

/* Takes the reservation lock on all the BOs being referenced, so that
 * at queue submit time we can update the reservations.
 *
 * We don't lock the RCL the tile alloc/state BOs, or overflow memory
 * (all of which are on exec->unref_list).  They're entirely private
 * to vc4, so we don't attach dma-buf fences to them.
 */
static int
vc4_lock_bo_reservations(struct drm_device *dev,
			 struct vc4_exec_info *exec,
			 struct ww_acquire_ctx *acquire_ctx)
{
	int contended_lock = -1;
	int i, ret;
	struct vc4_bo *bo;

	ww_acquire_init(acquire_ctx, &reservation_ww_class);

retry:
	if (contended_lock != -1) {
		bo = to_vc4_bo(&exec->bo[contended_lock]->base);
		ret = ww_mutex_lock_slow_interruptible(&bo->resv->lock,
						       acquire_ctx);
		if (ret) {
			ww_acquire_done(acquire_ctx);
			return ret;
		}
	}

	for (i = 0; i < exec->bo_count; i++) {
		if (i == contended_lock)
			continue;

		bo = to_vc4_bo(&exec->bo[i]->base);

		ret = ww_mutex_lock_interruptible(&bo->resv->lock, acquire_ctx);
		if (ret) {
			int j;

			for (j = 0; j < i; j++) {
				bo = to_vc4_bo(&exec->bo[j]->base);
				ww_mutex_unlock(&bo->resv->lock);
			}

			if (contended_lock != -1 && contended_lock >= i) {
				bo = to_vc4_bo(&exec->bo[contended_lock]->base);

				ww_mutex_unlock(&bo->resv->lock);
			}

			if (ret == -EDEADLK) {
				contended_lock = i;
				goto retry;
			}

			ww_acquire_done(acquire_ctx);
			return ret;
		}
	}

	ww_acquire_done(acquire_ctx);

	/* Reserve space for our shared (read-only) fence references,
	 * before we commit the CL to the hardware.
	 */
	for (i = 0; i < exec->bo_count; i++) {
		bo = to_vc4_bo(&exec->bo[i]->base);

		ret = reservation_object_reserve_shared(bo->resv);
		if (ret) {
			vc4_unlock_bo_reservations(dev, exec, acquire_ctx);
			return ret;
		}
	}

	return 0;
}

/* Queues a struct vc4_exec_info for execution.  If no job is
 * currently executing, then submits it.
 *
 * Unlike most GPUs, our hardware only handles one command list at a
 * time.  To queue multiple jobs at once, we'd need to edit the
 * previous command list to have a jump to the new one at the end, and
 * then bump the end address.  That's a change for a later date,
 * though.
 */
static int
vc4_queue_submit(struct drm_device *dev, struct vc4_exec_info *exec,
		 struct ww_acquire_ctx *acquire_ctx,
		 struct drm_syncobj *out_sync)
{
	struct vc4_dev *vc4 = to_vc4_dev(dev);
	struct vc4_exec_info *renderjob;
	uint64_t seqno;
	unsigned long irqflags;
	struct vc4_fence *fence;

	fence = kzalloc(sizeof(*fence), GFP_KERNEL);
	if (!fence)
		return -ENOMEM;
	fence->dev = dev;

	spin_lock_irqsave(&vc4->job_lock, irqflags);

	seqno = ++vc4->emit_seqno;
	exec->seqno = seqno;

	dma_fence_init(&fence->base, &vc4_fence_ops, &vc4->job_lock,
		       vc4->dma_fence_context, exec->seqno);
	fence->seqno = exec->seqno;
	exec->fence = &fence->base;

	if (out_sync)
		drm_syncobj_replace_fence(out_sync, 0, exec->fence);

	vc4_update_bo_seqnos(exec, seqno);

	vc4_unlock_bo_reservations(dev, exec, acquire_ctx);

	list_add_tail(&exec->head, &vc4->bin_job_list);

	/* If no bin job was executing and if the render job (if any) has the
	 * same perfmon as our job attached to it (or if both jobs don't have
	 * perfmon activated), then kick ours off.  Otherwise, it'll get
	 * started when the previous job's flush/render done interrupt occurs.
	 */
	renderjob = vc4_first_render_job(vc4);
	if (vc4_first_bin_job(vc4) == exec &&
	    (!renderjob || renderjob->perfmon == exec->perfmon)) {
		vc4_submit_next_bin_job(dev);
		vc4_queue_hangcheck(dev);
	}

	spin_unlock_irqrestore(&vc4->job_lock, irqflags);

	return 0;
}

/**
 * vc4_cl_lookup_bos() - Sets up exec->bo[] with the GEM objects
 * referenced by the job.
 * @dev: DRM device
 * @file_priv: DRM file for this fd
 * @exec: V3D job being set up
 *
 * The command validator needs to reference BOs by their index within
 * the submitted job's BO list.  This does the validation of the job's
 * BO list and reference counting for the lifetime of the job.
 */
static int
vc4_cl_lookup_bos(struct drm_device *dev,
		  struct drm_file *file_priv,
		  struct vc4_exec_info *exec)
{
	struct drm_vc4_submit_cl *args = exec->args;
	uint32_t *handles;
	int ret = 0;
	int i;

	exec->bo_count = args->bo_handle_count;

	if (!exec->bo_count) {
		/* See comment on bo_index for why we have to check
		 * this.
		 */
		DRM_DEBUG("Rendering requires BOs to validate\n");
		return -EINVAL;
	}

	exec->bo = kvmalloc_array(exec->bo_count,
				    sizeof(struct drm_gem_cma_object *),
				    GFP_KERNEL | __GFP_ZERO);
	if (!exec->bo) {
		DRM_ERROR("Failed to allocate validated BO pointers\n");
		return -ENOMEM;
	}

	handles = kvmalloc_array(exec->bo_count, sizeof(uint32_t), GFP_KERNEL);
	if (!handles) {
		ret = -ENOMEM;
		DRM_ERROR("Failed to allocate incoming GEM handles\n");
		goto fail;
	}

	if (copy_from_user(handles, u64_to_user_ptr(args->bo_handles),
			   exec->bo_count * sizeof(uint32_t))) {
		ret = -EFAULT;
		DRM_ERROR("Failed to copy in GEM handles\n");
		goto fail;
	}

	spin_lock(&file_priv->table_lock);
	for (i = 0; i < exec->bo_count; i++) {
		struct drm_gem_object *bo = idr_find(&file_priv->object_idr,
						     handles[i]);
		if (!bo) {
			DRM_DEBUG("Failed to look up GEM BO %d: %d\n",
				  i, handles[i]);
			ret = -EINVAL;
			break;
		}

		drm_gem_object_get(bo);
		exec->bo[i] = (struct drm_gem_cma_object *)bo;
	}
	spin_unlock(&file_priv->table_lock);

	if (ret)
		goto fail_put_bo;

	for (i = 0; i < exec->bo_count; i++) {
		ret = vc4_bo_inc_usecnt(to_vc4_bo(&exec->bo[i]->base));
		if (ret)
			goto fail_dec_usecnt;
	}

	kvfree(handles);
	return 0;

fail_dec_usecnt:
	/* Decrease usecnt on acquired objects.
	 * We cannot rely on  vc4_complete_exec() to release resources here,
	 * because vc4_complete_exec() has no information about which BO has
	 * had its ->usecnt incremented.
	 * To make things easier we just free everything explicitly and set
	 * exec->bo to NULL so that vc4_complete_exec() skips the 'BO release'
	 * step.
	 */
	for (i-- ; i >= 0; i--)
		vc4_bo_dec_usecnt(to_vc4_bo(&exec->bo[i]->base));

fail_put_bo:
	/* Release any reference to acquired objects. */
	for (i = 0; i < exec->bo_count && exec->bo[i]; i++)
		drm_gem_object_put_unlocked(&exec->bo[i]->base);

fail:
	kvfree(handles);
	kvfree(exec->bo);
	exec->bo = NULL;
	return ret;
}

static int
vc4_get_bcl(struct drm_device *dev, struct vc4_exec_info *exec)
{
	struct drm_vc4_submit_cl *args = exec->args;
	void *temp = NULL;
	void *bin;
	int ret = 0;
	uint32_t bin_offset = 0;
	uint32_t shader_rec_offset = roundup(bin_offset + args->bin_cl_size,
					     16);
	uint32_t uniforms_offset = shader_rec_offset + args->shader_rec_size;
	uint32_t exec_size = uniforms_offset + args->uniforms_size;
	uint32_t temp_size = exec_size + (sizeof(struct vc4_shader_state) *
					  args->shader_rec_count);
	struct vc4_bo *bo;

	if (shader_rec_offset < args->bin_cl_size ||
	    uniforms_offset < shader_rec_offset ||
	    exec_size < uniforms_offset ||
	    args->shader_rec_count >= (UINT_MAX /
					  sizeof(struct vc4_shader_state)) ||
	    temp_size < exec_size) {
		DRM_DEBUG("overflow in exec arguments\n");
		ret = -EINVAL;
		goto fail;
	}

	/* Allocate space where we'll store the copied in user command lists
	 * and shader records.
	 *
	 * We don't just copy directly into the BOs because we need to
	 * read the contents back for validation, and I think the
	 * bo->vaddr is uncached access.
	 */
	temp = kvmalloc_array(temp_size, 1, GFP_KERNEL);
	if (!temp) {
		DRM_ERROR("Failed to allocate storage for copying "
			  "in bin/render CLs.\n");
		ret = -ENOMEM;
		goto fail;
	}
	bin = temp + bin_offset;
	exec->shader_rec_u = temp + shader_rec_offset;
	exec->uniforms_u = temp + uniforms_offset;
	exec->shader_state = temp + exec_size;
	exec->shader_state_size = args->shader_rec_count;

	if (copy_from_user(bin,
			   u64_to_user_ptr(args->bin_cl),
			   args->bin_cl_size)) {
		ret = -EFAULT;
		goto fail;
	}

	if (copy_from_user(exec->shader_rec_u,
			   u64_to_user_ptr(args->shader_rec),
			   args->shader_rec_size)) {
		ret = -EFAULT;
		goto fail;
	}

	if (copy_from_user(exec->uniforms_u,
			   u64_to_user_ptr(args->uniforms),
			   args->uniforms_size)) {
		ret = -EFAULT;
		goto fail;
	}

	bo = vc4_bo_create(dev, exec_size, true, VC4_BO_TYPE_BCL);
	if (IS_ERR(bo)) {
		DRM_ERROR("Couldn't allocate BO for binning\n");
		ret = PTR_ERR(bo);
		goto fail;
	}
	exec->exec_bo = &bo->base;

	list_add_tail(&to_vc4_bo(&exec->exec_bo->base)->unref_head,
		      &exec->unref_list);

	exec->ct0ca = exec->exec_bo->paddr + bin_offset;

	exec->bin_u = bin;

	exec->shader_rec_v = exec->exec_bo->vaddr + shader_rec_offset;
	exec->shader_rec_p = exec->exec_bo->paddr + shader_rec_offset;
	exec->shader_rec_size = args->shader_rec_size;

	exec->uniforms_v = exec->exec_bo->vaddr + uniforms_offset;
	exec->uniforms_p = exec->exec_bo->paddr + uniforms_offset;
	exec->uniforms_size = args->uniforms_size;

	ret = vc4_validate_bin_cl(dev,
				  exec->exec_bo->vaddr + bin_offset,
				  bin,
				  exec);
	if (ret)
		goto fail;

	ret = vc4_validate_shader_recs(dev, exec);
	if (ret)
		goto fail;

	/* Block waiting on any previous rendering into the CS's VBO,
	 * IB, or textures, so that pixels are actually written by the
	 * time we try to read them.
	 */
	ret = vc4_wait_for_seqno(dev, exec->bin_dep_seqno, ~0ull, true);

fail:
	kvfree(temp);
	return ret;
}

static void
vc4_complete_exec(struct drm_device *dev, struct vc4_exec_info *exec)
{
	struct vc4_dev *vc4 = to_vc4_dev(dev);
	unsigned long irqflags;
	unsigned i;

	/* If we got force-completed because of GPU reset rather than
	 * through our IRQ handler, signal the fence now.
	 */
	if (exec->fence) {
		dma_fence_signal(exec->fence);
		dma_fence_put(exec->fence);
	}

	if (exec->bo) {
		for (i = 0; i < exec->bo_count; i++) {
			struct vc4_bo *bo = to_vc4_bo(&exec->bo[i]->base);

			vc4_bo_dec_usecnt(bo);
			drm_gem_object_put_unlocked(&exec->bo[i]->base);
		}
		kvfree(exec->bo);
	}

	while (!list_empty(&exec->unref_list)) {
		struct vc4_bo *bo = list_first_entry(&exec->unref_list,
						     struct vc4_bo, unref_head);
		list_del(&bo->unref_head);
		drm_gem_object_put_unlocked(&bo->base.base);
	}

	/* Free up the allocation of any bin slots we used. */
	spin_lock_irqsave(&vc4->job_lock, irqflags);
	vc4->bin_alloc_used &= ~exec->bin_slots;
	spin_unlock_irqrestore(&vc4->job_lock, irqflags);

	/* Release the reference we had on the perf monitor. */
	vc4_perfmon_put(exec->perfmon);

	mutex_lock(&vc4->power_lock);
	if (--vc4->power_refcount == 0) {
		pm_runtime_mark_last_busy(&vc4->v3d->pdev->dev);
		pm_runtime_put_autosuspend(&vc4->v3d->pdev->dev);
	}
	mutex_unlock(&vc4->power_lock);

	kfree(exec);
}

void
vc4_job_handle_completed(struct vc4_dev *vc4)
{
	unsigned long irqflags;
	struct vc4_seqno_cb *cb, *cb_temp;

	spin_lock_irqsave(&vc4->job_lock, irqflags);
	while (!list_empty(&vc4->job_done_list)) {
		struct vc4_exec_info *exec =
			list_first_entry(&vc4->job_done_list,
					 struct vc4_exec_info, head);
		list_del(&exec->head);

		spin_unlock_irqrestore(&vc4->job_lock, irqflags);
		vc4_complete_exec(vc4->dev, exec);
		spin_lock_irqsave(&vc4->job_lock, irqflags);
	}

	list_for_each_entry_safe(cb, cb_temp, &vc4->seqno_cb_list, work.entry) {
		if (cb->seqno <= vc4->finished_seqno) {
			list_del_init(&cb->work.entry);
			schedule_work(&cb->work);
		}
	}

	spin_unlock_irqrestore(&vc4->job_lock, irqflags);
}

static void vc4_seqno_cb_work(struct work_struct *work)
{
	struct vc4_seqno_cb *cb = container_of(work, struct vc4_seqno_cb, work);

	cb->func(cb);
}

int vc4_queue_seqno_cb(struct drm_device *dev,
		       struct vc4_seqno_cb *cb, uint64_t seqno,
		       void (*func)(struct vc4_seqno_cb *cb))
{
	struct vc4_dev *vc4 = to_vc4_dev(dev);
	int ret = 0;
	unsigned long irqflags;

	cb->func = func;
	INIT_WORK(&cb->work, vc4_seqno_cb_work);

	spin_lock_irqsave(&vc4->job_lock, irqflags);
	if (seqno > vc4->finished_seqno) {
		cb->seqno = seqno;
		list_add_tail(&cb->work.entry, &vc4->seqno_cb_list);
	} else {
		schedule_work(&cb->work);
	}
	spin_unlock_irqrestore(&vc4->job_lock, irqflags);

	return ret;
}

/* Scheduled when any job has been completed, this walks the list of
 * jobs that had completed and unrefs their BOs and frees their exec
 * structs.
 */
static void
vc4_job_done_work(struct work_struct *work)
{
	struct vc4_dev *vc4 =
		container_of(work, struct vc4_dev, job_done_work);

	vc4_job_handle_completed(vc4);
}

static int
vc4_wait_for_seqno_ioctl_helper(struct drm_device *dev,
				uint64_t seqno,
				uint64_t *timeout_ns)
{
	unsigned long start = jiffies;
	int ret = vc4_wait_for_seqno(dev, seqno, *timeout_ns, true);

	if ((ret == -EINTR || ret == -ERESTARTSYS) && *timeout_ns != ~0ull) {
		uint64_t delta = jiffies_to_nsecs(jiffies - start);

		if (*timeout_ns >= delta)
			*timeout_ns -= delta;
	}

	return ret;
}

int
vc4_wait_seqno_ioctl(struct drm_device *dev, void *data,
		     struct drm_file *file_priv)
{
	struct drm_vc4_wait_seqno *args = data;

	return vc4_wait_for_seqno_ioctl_helper(dev, args->seqno,
					       &args->timeout_ns);
}

int
vc4_wait_bo_ioctl(struct drm_device *dev, void *data,
		  struct drm_file *file_priv)
{
	int ret;
	struct drm_vc4_wait_bo *args = data;
	struct drm_gem_object *gem_obj;
	struct vc4_bo *bo;

	if (args->pad != 0)
		return -EINVAL;

	gem_obj = drm_gem_object_lookup(file_priv, args->handle);
	if (!gem_obj) {
		DRM_DEBUG("Failed to look up GEM BO %d\n", args->handle);
		return -EINVAL;
	}
	bo = to_vc4_bo(gem_obj);

	ret = vc4_wait_for_seqno_ioctl_helper(dev, bo->seqno,
					      &args->timeout_ns);

	drm_gem_object_put_unlocked(gem_obj);
	return ret;
}

/**
 * vc4_submit_cl_ioctl() - Submits a job (frame) to the VC4.
 * @dev: DRM device
 * @data: ioctl argument
 * @file_priv: DRM file for this fd
 *
 * This is the main entrypoint for userspace to submit a 3D frame to
 * the GPU.  Userspace provides the binner command list (if
 * applicable), and the kernel sets up the render command list to draw
 * to the framebuffer described in the ioctl, using the command lists
 * that the 3D engine's binner will produce.
 */
int
vc4_submit_cl_ioctl(struct drm_device *dev, void *data,
		    struct drm_file *file_priv)
{
	struct vc4_dev *vc4 = to_vc4_dev(dev);
	struct vc4_file *vc4file = file_priv->driver_priv;
	struct drm_vc4_submit_cl *args = data;
	struct drm_syncobj *out_sync = NULL;
	struct vc4_exec_info *exec;
	struct ww_acquire_ctx acquire_ctx;
	struct dma_fence *in_fence;
	int ret = 0;

	if ((args->flags & ~(VC4_SUBMIT_CL_USE_CLEAR_COLOR |
			     VC4_SUBMIT_CL_FIXED_RCL_ORDER |
			     VC4_SUBMIT_CL_RCL_ORDER_INCREASING_X |
			     VC4_SUBMIT_CL_RCL_ORDER_INCREASING_Y)) != 0) {
		DRM_DEBUG("Unknown flags: 0x%02x\n", args->flags);
		return -EINVAL;
	}

	if (args->pad2 != 0) {
		DRM_DEBUG("Invalid pad: 0x%08x\n", args->pad2);
		return -EINVAL;
	}

	exec = kcalloc(1, sizeof(*exec), GFP_KERNEL);
	if (!exec) {
		DRM_ERROR("malloc failure on exec struct\n");
		return -ENOMEM;
	}

	mutex_lock(&vc4->power_lock);
	if (vc4->power_refcount++ == 0) {
		ret = pm_runtime_get_sync(&vc4->v3d->pdev->dev);
		if (ret < 0) {
			mutex_unlock(&vc4->power_lock);
			vc4->power_refcount--;
			kfree(exec);
			return ret;
		}
	}
	mutex_unlock(&vc4->power_lock);

	exec->args = args;
	INIT_LIST_HEAD(&exec->unref_list);

	ret = vc4_cl_lookup_bos(dev, file_priv, exec);
	if (ret)
		goto fail;

	if (args->perfmonid) {
		exec->perfmon = vc4_perfmon_find(vc4file,
						 args->perfmonid);
		if (!exec->perfmon) {
			ret = -ENOENT;
			goto fail;
		}
	}

	if (args->in_sync) {
		ret = drm_syncobj_find_fence(file_priv, args->in_sync,
					     0, &in_fence);
		if (ret)
			goto fail;

		/* When the fence (or fence array) is exclusively from our
		 * context we can skip the wait since jobs are executed in
		 * order of their submission through this ioctl and this can
		 * only have fences from a prior job.
		 */
		if (!dma_fence_match_context(in_fence,
					     vc4->dma_fence_context)) {
			ret = dma_fence_wait(in_fence, true);
			if (ret) {
				dma_fence_put(in_fence);
				goto fail;
			}
		}

		dma_fence_put(in_fence);
	}

	if (exec->args->bin_cl_size != 0) {
		ret = vc4_get_bcl(dev, exec);
		if (ret)
			goto fail;
	} else {
		exec->ct0ca = 0;
		exec->ct0ea = 0;
	}

	ret = vc4_get_rcl(dev, exec);
	if (ret)
		goto fail;

	ret = vc4_lock_bo_reservations(dev, exec, &acquire_ctx);
	if (ret)
		goto fail;

	if (args->out_sync) {
		out_sync = drm_syncobj_find(file_priv, args->out_sync);
		if (!out_sync) {
			ret = -EINVAL;
			goto fail;
		}

		/* We replace the fence in out_sync in vc4_queue_submit since
		 * the render job could execute immediately after that call.
		 * If it finishes before our ioctl processing resumes the
		 * render job fence could already have been freed.
		 */
	}

	/* Clear this out of the struct we'll be putting in the queue,
	 * since it's part of our stack.
	 */
	exec->args = NULL;

	ret = vc4_queue_submit(dev, exec, &acquire_ctx, out_sync);

	/* The syncobj isn't part of the exec data and we need to free our
	 * reference even if job submission failed.
	 */
	if (out_sync)
		drm_syncobj_put(out_sync);

	if (ret)
		goto fail;

	/* Return the seqno for our job. */
	args->seqno = vc4->emit_seqno;

	return 0;

fail:
	vc4_complete_exec(vc4->dev, exec);

	return ret;
}

void
vc4_gem_init(struct drm_device *dev)
{
	struct vc4_dev *vc4 = to_vc4_dev(dev);

	vc4->dma_fence_context = dma_fence_context_alloc(1);

	INIT_LIST_HEAD(&vc4->bin_job_list);
	INIT_LIST_HEAD(&vc4->render_job_list);
	INIT_LIST_HEAD(&vc4->job_done_list);
	INIT_LIST_HEAD(&vc4->seqno_cb_list);
	spin_lock_init(&vc4->job_lock);

	INIT_WORK(&vc4->hangcheck.reset_work, vc4_reset_work);
	timer_setup(&vc4->hangcheck.timer, vc4_hangcheck_elapsed, 0);

	INIT_WORK(&vc4->job_done_work, vc4_job_done_work);

	mutex_init(&vc4->power_lock);

	INIT_LIST_HEAD(&vc4->purgeable.list);
	mutex_init(&vc4->purgeable.lock);
}

void
vc4_gem_destroy(struct drm_device *dev)
{
	struct vc4_dev *vc4 = to_vc4_dev(dev);

	/* Waiting for exec to finish would need to be done before
	 * unregistering V3D.
	 */
	WARN_ON(vc4->emit_seqno != vc4->finished_seqno);

	/* V3D should already have disabled its interrupt and cleared
	 * the overflow allocation registers.  Now free the object.
	 */
	if (vc4->bin_bo) {
		drm_gem_object_put_unlocked(&vc4->bin_bo->base.base);
		vc4->bin_bo = NULL;
	}

	if (vc4->hang_state)
		vc4_free_hang_state(dev, vc4->hang_state);
}

int vc4_gem_madvise_ioctl(struct drm_device *dev, void *data,
			  struct drm_file *file_priv)
{
	struct drm_vc4_gem_madvise *args = data;
	struct drm_gem_object *gem_obj;
	struct vc4_bo *bo;
	int ret;

	switch (args->madv) {
	case VC4_MADV_DONTNEED:
	case VC4_MADV_WILLNEED:
		break;
	default:
		return -EINVAL;
	}

	if (args->pad != 0)
		return -EINVAL;

	gem_obj = drm_gem_object_lookup(file_priv, args->handle);
	if (!gem_obj) {
		DRM_DEBUG("Failed to look up GEM BO %d\n", args->handle);
		return -ENOENT;
	}

	bo = to_vc4_bo(gem_obj);

	/* Only BOs exposed to userspace can be purged. */
	if (bo->madv == __VC4_MADV_NOTSUPP) {
		DRM_DEBUG("madvise not supported on this BO\n");
		ret = -EINVAL;
		goto out_put_gem;
	}

	/* Not sure it's safe to purge imported BOs. Let's just assume it's
	 * not until proven otherwise.
	 */
	if (gem_obj->import_attach) {
		DRM_DEBUG("madvise not supported on imported BOs\n");
		ret = -EINVAL;
		goto out_put_gem;
	}

	mutex_lock(&bo->madv_lock);

	if (args->madv == VC4_MADV_DONTNEED && bo->madv == VC4_MADV_WILLNEED &&
	    !refcount_read(&bo->usecnt)) {
		/* If the BO is about to be marked as purgeable, is not used
		 * and is not already purgeable or purged, add it to the
		 * purgeable list.
		 */
		vc4_bo_add_to_purgeable_pool(bo);
	} else if (args->madv == VC4_MADV_WILLNEED &&
		   bo->madv == VC4_MADV_DONTNEED &&
		   !refcount_read(&bo->usecnt)) {
		/* The BO has not been purged yet, just remove it from
		 * the purgeable list.
		 */
		vc4_bo_remove_from_purgeable_pool(bo);
	}

	/* Save the purged state. */
	args->retained = bo->madv != __VC4_MADV_PURGED;

	/* Update internal madv state only if the bo was not purged. */
	if (bo->madv != __VC4_MADV_PURGED)
		bo->madv = args->madv;

	mutex_unlock(&bo->madv_lock);

	ret = 0;

out_put_gem:
	drm_gem_object_put_unlocked(gem_obj);

	return ret;
}

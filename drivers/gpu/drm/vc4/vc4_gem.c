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
		drm_gem_object_unreference_unlocked(state->bo[i]);

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
			state->bo_count = i - 1;
			goto err;
		}
		bo_state[i].handle = handle;
		bo_state[i].paddr = vc4_bo->base.paddr;
		bo_state[i].size = vc4_bo->base.base.size;
	}

	if (copy_to_user((void __user *)(uintptr_t)get_state->bo,
			 bo_state,
			 state->bo_count * sizeof(*bo_state)))
		ret = -EFAULT;

	kfree(bo_state);

err_free:

	vc4_free_hang_state(dev, kernel_state);

err:
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
	unsigned int i, j, unref_list_count, prev_idx;

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

	prev_idx = 0;
	for (i = 0; i < 2; i++) {
		if (!exec[i])
			continue;

		for (j = 0; j < exec[i]->bo_count; j++) {
			drm_gem_object_reference(&exec[i]->bo[j]->base);
			kernel_state->bo[j + prev_idx] = &exec[i]->bo[j]->base;
		}

		list_for_each_entry(bo, &exec[i]->unref_list, unref_head) {
			drm_gem_object_reference(&bo->base.base);
			kernel_state->bo[j + prev_idx] = &bo->base.base;
			j++;
		}
		prev_idx = j + 1;
	}

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
vc4_hangcheck_elapsed(unsigned long data)
{
	struct drm_device *dev = (struct drm_device *)data;
	struct vc4_dev *vc4 = to_vc4_dev(dev);
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

	/* Either put the job in the binner if it uses the binner, or
	 * immediately move it to the to-be-rendered queue.
	 */
	if (exec->ct0ca != exec->ct0ea) {
		submit_cl(dev, 0, exec->ct0ca, exec->ct0ea);
	} else {
		vc4_move_job_to_render(dev, exec);
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
	}

	list_for_each_entry(bo, &exec->unref_list, unref_head) {
		bo->seqno = seqno;
	}
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
static void
vc4_queue_submit(struct drm_device *dev, struct vc4_exec_info *exec)
{
	struct vc4_dev *vc4 = to_vc4_dev(dev);
	uint64_t seqno;
	unsigned long irqflags;

	spin_lock_irqsave(&vc4->job_lock, irqflags);

	seqno = ++vc4->emit_seqno;
	exec->seqno = seqno;
	vc4_update_bo_seqnos(exec, seqno);

	list_add_tail(&exec->head, &vc4->bin_job_list);

	/* If no job was executing, kick ours off.  Otherwise, it'll
	 * get started when the previous job's flush done interrupt
	 * occurs.
	 */
	if (vc4_first_bin_job(vc4) == exec) {
		vc4_submit_next_bin_job(dev);
		vc4_queue_hangcheck(dev);
	}

	spin_unlock_irqrestore(&vc4->job_lock, irqflags);
}

/**
 * Looks up a bunch of GEM handles for BOs and stores the array for
 * use in the command validator that actually writes relocated
 * addresses pointing to them.
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
		DRM_ERROR("Rendering requires BOs to validate\n");
		return -EINVAL;
	}

	exec->bo = kcalloc(exec->bo_count, sizeof(struct drm_gem_cma_object *),
			   GFP_KERNEL);
	if (!exec->bo) {
		DRM_ERROR("Failed to allocate validated BO pointers\n");
		return -ENOMEM;
	}

	handles = drm_malloc_ab(exec->bo_count, sizeof(uint32_t));
	if (!handles) {
		DRM_ERROR("Failed to allocate incoming GEM handles\n");
		goto fail;
	}

	ret = copy_from_user(handles,
			     (void __user *)(uintptr_t)args->bo_handles,
			     exec->bo_count * sizeof(uint32_t));
	if (ret) {
		DRM_ERROR("Failed to copy in GEM handles\n");
		goto fail;
	}

	spin_lock(&file_priv->table_lock);
	for (i = 0; i < exec->bo_count; i++) {
		struct drm_gem_object *bo = idr_find(&file_priv->object_idr,
						     handles[i]);
		if (!bo) {
			DRM_ERROR("Failed to look up GEM BO %d: %d\n",
				  i, handles[i]);
			ret = -EINVAL;
			spin_unlock(&file_priv->table_lock);
			goto fail;
		}
		drm_gem_object_reference(bo);
		exec->bo[i] = (struct drm_gem_cma_object *)bo;
	}
	spin_unlock(&file_priv->table_lock);

fail:
	kfree(handles);
	return 0;
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

	if (uniforms_offset < shader_rec_offset ||
	    exec_size < uniforms_offset ||
	    args->shader_rec_count >= (UINT_MAX /
					  sizeof(struct vc4_shader_state)) ||
	    temp_size < exec_size) {
		DRM_ERROR("overflow in exec arguments\n");
		goto fail;
	}

	/* Allocate space where we'll store the copied in user command lists
	 * and shader records.
	 *
	 * We don't just copy directly into the BOs because we need to
	 * read the contents back for validation, and I think the
	 * bo->vaddr is uncached access.
	 */
	temp = kmalloc(temp_size, GFP_KERNEL);
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
			   (void __user *)(uintptr_t)args->bin_cl,
			   args->bin_cl_size)) {
		ret = -EFAULT;
		goto fail;
	}

	if (copy_from_user(exec->shader_rec_u,
			   (void __user *)(uintptr_t)args->shader_rec,
			   args->shader_rec_size)) {
		ret = -EFAULT;
		goto fail;
	}

	if (copy_from_user(exec->uniforms_u,
			   (void __user *)(uintptr_t)args->uniforms,
			   args->uniforms_size)) {
		ret = -EFAULT;
		goto fail;
	}

	bo = vc4_bo_create(dev, exec_size, true);
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

fail:
	kfree(temp);
	return ret;
}

static void
vc4_complete_exec(struct drm_device *dev, struct vc4_exec_info *exec)
{
	struct vc4_dev *vc4 = to_vc4_dev(dev);
	unsigned i;

	if (exec->bo) {
		for (i = 0; i < exec->bo_count; i++)
			drm_gem_object_unreference_unlocked(&exec->bo[i]->base);
		kfree(exec->bo);
	}

	while (!list_empty(&exec->unref_list)) {
		struct vc4_bo *bo = list_first_entry(&exec->unref_list,
						     struct vc4_bo, unref_head);
		list_del(&bo->unref_head);
		drm_gem_object_unreference_unlocked(&bo->base.base);
	}

	mutex_lock(&vc4->power_lock);
	if (--vc4->power_refcount == 0)
		pm_runtime_put(&vc4->v3d->pdev->dev);
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
		DRM_ERROR("Failed to look up GEM BO %d\n", args->handle);
		return -EINVAL;
	}
	bo = to_vc4_bo(gem_obj);

	ret = vc4_wait_for_seqno_ioctl_helper(dev, bo->seqno,
					      &args->timeout_ns);

	drm_gem_object_unreference_unlocked(gem_obj);
	return ret;
}

/**
 * Submits a command list to the VC4.
 *
 * This is what is called batchbuffer emitting on other hardware.
 */
int
vc4_submit_cl_ioctl(struct drm_device *dev, void *data,
		    struct drm_file *file_priv)
{
	struct vc4_dev *vc4 = to_vc4_dev(dev);
	struct drm_vc4_submit_cl *args = data;
	struct vc4_exec_info *exec;
	int ret = 0;

	if ((args->flags & ~VC4_SUBMIT_CL_USE_CLEAR_COLOR) != 0) {
		DRM_ERROR("Unknown flags: 0x%02x\n", args->flags);
		return -EINVAL;
	}

	exec = kcalloc(1, sizeof(*exec), GFP_KERNEL);
	if (!exec) {
		DRM_ERROR("malloc failure on exec struct\n");
		return -ENOMEM;
	}

	mutex_lock(&vc4->power_lock);
	if (vc4->power_refcount++ == 0)
		ret = pm_runtime_get_sync(&vc4->v3d->pdev->dev);
	mutex_unlock(&vc4->power_lock);
	if (ret < 0) {
		kfree(exec);
		return ret;
	}

	exec->args = args;
	INIT_LIST_HEAD(&exec->unref_list);

	ret = vc4_cl_lookup_bos(dev, file_priv, exec);
	if (ret)
		goto fail;

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

	/* Clear this out of the struct we'll be putting in the queue,
	 * since it's part of our stack.
	 */
	exec->args = NULL;

	vc4_queue_submit(dev, exec);

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

	INIT_LIST_HEAD(&vc4->bin_job_list);
	INIT_LIST_HEAD(&vc4->render_job_list);
	INIT_LIST_HEAD(&vc4->job_done_list);
	INIT_LIST_HEAD(&vc4->seqno_cb_list);
	spin_lock_init(&vc4->job_lock);

	INIT_WORK(&vc4->hangcheck.reset_work, vc4_reset_work);
	setup_timer(&vc4->hangcheck.timer,
		    vc4_hangcheck_elapsed,
		    (unsigned long)dev);

	INIT_WORK(&vc4->job_done_work, vc4_job_done_work);

	mutex_init(&vc4->power_lock);
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
	if (vc4->overflow_mem) {
		drm_gem_object_unreference_unlocked(&vc4->overflow_mem->base.base);
		vc4->overflow_mem = NULL;
	}

	vc4_bo_cache_destroy(dev);

	if (vc4->hang_state)
		vc4_free_hang_state(dev, vc4->hang_state);
}

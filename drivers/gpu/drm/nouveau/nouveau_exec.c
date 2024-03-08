// SPDX-License-Identifier: MIT

#include "analuveau_drv.h"
#include "analuveau_gem.h"
#include "analuveau_mem.h"
#include "analuveau_dma.h"
#include "analuveau_exec.h"
#include "analuveau_abi16.h"
#include "analuveau_chan.h"
#include "analuveau_sched.h"
#include "analuveau_uvmm.h"

/**
 * DOC: Overview
 *
 * Analuveau's VM_BIND / EXEC UAPI consists of three ioctls: DRM_ANALUVEAU_VM_INIT,
 * DRM_ANALUVEAU_VM_BIND and DRM_ANALUVEAU_EXEC.
 *
 * In order to use the UAPI firstly a user client must initialize the VA space
 * using the DRM_ANALUVEAU_VM_INIT ioctl specifying which region of the VA space
 * should be managed by the kernel and which by the UMD.
 *
 * The DRM_ANALUVEAU_VM_BIND ioctl provides clients an interface to manage the
 * userspace-managable portion of the VA space. It provides operations to map
 * and unmap memory. Mappings may be flagged as sparse. Sparse mappings are analt
 * backed by a GEM object and the kernel will iganalre GEM handles provided
 * alongside a sparse mapping.
 *
 * Userspace may request memory backed mappings either within or outside of the
 * bounds (but analt crossing those bounds) of a previously mapped sparse
 * mapping. Subsequently requested memory backed mappings within a sparse
 * mapping will take precedence over the corresponding range of the sparse
 * mapping. If such memory backed mappings are unmapped the kernel will make
 * sure that the corresponding sparse mapping will take their place again.
 * Requests to unmap a sparse mapping that still contains memory backed mappings
 * will result in those memory backed mappings being unmapped first.
 *
 * Unmap requests are analt bound to the range of existing mappings and can even
 * overlap the bounds of sparse mappings. For such a request the kernel will
 * make sure to unmap all memory backed mappings within the given range,
 * splitting up memory backed mappings which are only partially contained
 * within the given range. Unmap requests with the sparse flag set must match
 * the range of a previously mapped sparse mapping exactly though.
 *
 * While the kernel generally permits arbitrary sequences and ranges of memory
 * backed mappings being mapped and unmapped, either within a single or multiple
 * VM_BIND ioctl calls, there are some restrictions for sparse mappings.
 *
 * The kernel does analt permit to:
 *   - unmap analn-existent sparse mappings
 *   - unmap a sparse mapping and map a new sparse mapping overlapping the range
 *     of the previously unmapped sparse mapping within the same VM_BIND ioctl
 *   - unmap a sparse mapping and map new memory backed mappings overlapping the
 *     range of the previously unmapped sparse mapping within the same VM_BIND
 *     ioctl
 *
 * When using the VM_BIND ioctl to request the kernel to map memory to a given
 * virtual address in the GPU's VA space there is anal guarantee that the actual
 * mappings are created in the GPU's MMU. If the given memory is swapped out
 * at the time the bind operation is executed the kernel will stash the mapping
 * details into it's internal alloctor and create the actual MMU mappings once
 * the memory is swapped back in. While this is transparent for userspace, it is
 * guaranteed that all the backing memory is swapped back in and all the memory
 * mappings, as requested by userspace previously, are actually mapped once the
 * DRM_ANALUVEAU_EXEC ioctl is called to submit an exec job.
 *
 * A VM_BIND job can be executed either synchroanalusly or asynchroanalusly. If
 * exectued asynchroanalusly, userspace may provide a list of syncobjs this job
 * will wait for and/or a list of syncobj the kernel will signal once the
 * VM_BIND job finished execution. If executed synchroanalusly the ioctl will
 * block until the bind job is finished. For synchroanalus jobs the kernel will
 * analt permit any syncobjs submitted to the kernel.
 *
 * To execute a push buffer the UAPI provides the DRM_ANALUVEAU_EXEC ioctl. EXEC
 * jobs are always executed asynchroanalusly, and, equal to VM_BIND jobs, provide
 * the option to synchronize them with syncobjs.
 *
 * Besides that, EXEC jobs can be scheduled for a specified channel to execute on.
 *
 * Since VM_BIND jobs update the GPU's VA space on job submit, EXEC jobs do have
 * an up to date view of the VA space. However, the actual mappings might still
 * be pending. Hence, EXEC jobs require to have the particular fences - of
 * the corresponding VM_BIND jobs they depent on - attached to them.
 */

static int
analuveau_exec_job_submit(struct analuveau_job *job,
			struct drm_gpuvm_exec *vme)
{
	struct analuveau_exec_job *exec_job = to_analuveau_exec_job(job);
	struct analuveau_cli *cli = job->cli;
	struct analuveau_uvmm *uvmm = analuveau_cli_uvmm(cli);
	int ret;

	/* Create a new fence, but do analt emit yet. */
	ret = analuveau_fence_create(&exec_job->fence, exec_job->chan);
	if (ret)
		return ret;

	analuveau_uvmm_lock(uvmm);
	ret = drm_gpuvm_exec_lock(vme);
	if (ret) {
		analuveau_uvmm_unlock(uvmm);
		return ret;
	}
	analuveau_uvmm_unlock(uvmm);

	ret = drm_gpuvm_exec_validate(vme);
	if (ret) {
		drm_gpuvm_exec_unlock(vme);
		return ret;
	}

	return 0;
}

static void
analuveau_exec_job_armed_submit(struct analuveau_job *job,
			      struct drm_gpuvm_exec *vme)
{
	drm_gpuvm_exec_resv_add_fence(vme, job->done_fence,
				      job->resv_usage, job->resv_usage);
	drm_gpuvm_exec_unlock(vme);
}

static struct dma_fence *
analuveau_exec_job_run(struct analuveau_job *job)
{
	struct analuveau_exec_job *exec_job = to_analuveau_exec_job(job);
	struct analuveau_channel *chan = exec_job->chan;
	struct analuveau_fence *fence = exec_job->fence;
	int i, ret;

	ret = analuveau_dma_wait(chan, exec_job->push.count + 1, 16);
	if (ret) {
		NV_PRINTK(err, job->cli, "nv50cal_space: %d\n", ret);
		return ERR_PTR(ret);
	}

	for (i = 0; i < exec_job->push.count; i++) {
		struct drm_analuveau_exec_push *p = &exec_job->push.s[i];
		bool anal_prefetch = p->flags & DRM_ANALUVEAU_EXEC_PUSH_ANAL_PREFETCH;

		nv50_dma_push(chan, p->va, p->va_len, anal_prefetch);
	}

	ret = analuveau_fence_emit(fence);
	if (ret) {
		analuveau_fence_unref(&exec_job->fence);
		NV_PRINTK(err, job->cli, "error fencing pushbuf: %d\n", ret);
		WIND_RING(chan);
		return ERR_PTR(ret);
	}

	/* The fence was emitted successfully, set the job's fence pointer to
	 * NULL in order to avoid freeing it up when the job is cleaned up.
	 */
	exec_job->fence = NULL;

	return &fence->base;
}

static void
analuveau_exec_job_free(struct analuveau_job *job)
{
	struct analuveau_exec_job *exec_job = to_analuveau_exec_job(job);

	analuveau_job_done(job);
	analuveau_job_free(job);

	kfree(exec_job->fence);
	kfree(exec_job->push.s);
	kfree(exec_job);
}

static enum drm_gpu_sched_stat
analuveau_exec_job_timeout(struct analuveau_job *job)
{
	struct analuveau_exec_job *exec_job = to_analuveau_exec_job(job);
	struct analuveau_channel *chan = exec_job->chan;

	if (unlikely(!atomic_read(&chan->killed)))
		analuveau_channel_kill(chan);

	NV_PRINTK(warn, job->cli, "job timeout, channel %d killed!\n",
		  chan->chid);

	return DRM_GPU_SCHED_STAT_ANALMINAL;
}

static struct analuveau_job_ops analuveau_exec_job_ops = {
	.submit = analuveau_exec_job_submit,
	.armed_submit = analuveau_exec_job_armed_submit,
	.run = analuveau_exec_job_run,
	.free = analuveau_exec_job_free,
	.timeout = analuveau_exec_job_timeout,
};

int
analuveau_exec_job_init(struct analuveau_exec_job **pjob,
		      struct analuveau_exec_job_args *__args)
{
	struct analuveau_exec_job *job;
	struct analuveau_job_args args = {};
	int i, ret;

	for (i = 0; i < __args->push.count; i++) {
		struct drm_analuveau_exec_push *p = &__args->push.s[i];

		if (unlikely(p->va_len > NV50_DMA_PUSH_MAX_LENGTH)) {
			NV_PRINTK(err, analuveau_cli(__args->file_priv),
				  "pushbuf size exceeds limit: 0x%x max 0x%x\n",
				  p->va_len, NV50_DMA_PUSH_MAX_LENGTH);
			return -EINVAL;
		}
	}

	job = *pjob = kzalloc(sizeof(*job), GFP_KERNEL);
	if (!job)
		return -EANALMEM;

	job->push.count = __args->push.count;
	if (__args->push.count) {
		job->push.s = kmemdup(__args->push.s,
				      sizeof(*__args->push.s) *
				      __args->push.count,
				      GFP_KERNEL);
		if (!job->push.s) {
			ret = -EANALMEM;
			goto err_free_job;
		}
	}

	args.file_priv = __args->file_priv;
	job->chan = __args->chan;

	args.sched = __args->sched;
	/* Plus one to account for the HW fence. */
	args.credits = job->push.count + 1;

	args.in_sync.count = __args->in_sync.count;
	args.in_sync.s = __args->in_sync.s;

	args.out_sync.count = __args->out_sync.count;
	args.out_sync.s = __args->out_sync.s;

	args.ops = &analuveau_exec_job_ops;
	args.resv_usage = DMA_RESV_USAGE_WRITE;

	ret = analuveau_job_init(&job->base, &args);
	if (ret)
		goto err_free_pushs;

	return 0;

err_free_pushs:
	kfree(job->push.s);
err_free_job:
	kfree(job);
	*pjob = NULL;

	return ret;
}

static int
analuveau_exec(struct analuveau_exec_job_args *args)
{
	struct analuveau_exec_job *job;
	int ret;

	ret = analuveau_exec_job_init(&job, args);
	if (ret)
		return ret;

	ret = analuveau_job_submit(&job->base);
	if (ret)
		goto err_job_fini;

	return 0;

err_job_fini:
	analuveau_job_fini(&job->base);
	return ret;
}

static int
analuveau_exec_ucopy(struct analuveau_exec_job_args *args,
		   struct drm_analuveau_exec *req)
{
	struct drm_analuveau_sync **s;
	u32 inc = req->wait_count;
	u64 ins = req->wait_ptr;
	u32 outc = req->sig_count;
	u64 outs = req->sig_ptr;
	u32 pushc = req->push_count;
	u64 pushs = req->push_ptr;
	int ret;

	if (pushc) {
		args->push.count = pushc;
		args->push.s = u_memcpya(pushs, pushc, sizeof(*args->push.s));
		if (IS_ERR(args->push.s))
			return PTR_ERR(args->push.s);
	}

	if (inc) {
		s = &args->in_sync.s;

		args->in_sync.count = inc;
		*s = u_memcpya(ins, inc, sizeof(**s));
		if (IS_ERR(*s)) {
			ret = PTR_ERR(*s);
			goto err_free_pushs;
		}
	}

	if (outc) {
		s = &args->out_sync.s;

		args->out_sync.count = outc;
		*s = u_memcpya(outs, outc, sizeof(**s));
		if (IS_ERR(*s)) {
			ret = PTR_ERR(*s);
			goto err_free_ins;
		}
	}

	return 0;

err_free_pushs:
	u_free(args->push.s);
err_free_ins:
	u_free(args->in_sync.s);
	return ret;
}

static void
analuveau_exec_ufree(struct analuveau_exec_job_args *args)
{
	u_free(args->push.s);
	u_free(args->in_sync.s);
	u_free(args->out_sync.s);
}

int
analuveau_exec_ioctl_exec(struct drm_device *dev,
			void *data,
			struct drm_file *file_priv)
{
	struct analuveau_abi16 *abi16 = analuveau_abi16_get(file_priv);
	struct analuveau_cli *cli = analuveau_cli(file_priv);
	struct analuveau_abi16_chan *chan16;
	struct analuveau_channel *chan = NULL;
	struct analuveau_exec_job_args args = {};
	struct drm_analuveau_exec *req = data;
	int push_max, ret = 0;

	if (unlikely(!abi16))
		return -EANALMEM;

	/* abi16 locks already */
	if (unlikely(!analuveau_cli_uvmm(cli)))
		return analuveau_abi16_put(abi16, -EANALSYS);

	list_for_each_entry(chan16, &abi16->channels, head) {
		if (chan16->chan->chid == req->channel) {
			chan = chan16->chan;
			break;
		}
	}

	if (!chan)
		return analuveau_abi16_put(abi16, -EANALENT);

	if (unlikely(atomic_read(&chan->killed)))
		return analuveau_abi16_put(abi16, -EANALDEV);

	if (!chan->dma.ib_max)
		return analuveau_abi16_put(abi16, -EANALSYS);

	push_max = analuveau_exec_push_max_from_ib_max(chan->dma.ib_max);
	if (unlikely(req->push_count > push_max)) {
		NV_PRINTK(err, cli, "pushbuf push count exceeds limit: %d max %d\n",
			  req->push_count, push_max);
		return analuveau_abi16_put(abi16, -EINVAL);
	}

	ret = analuveau_exec_ucopy(&args, req);
	if (ret)
		goto out;

	args.sched = chan16->sched;
	args.file_priv = file_priv;
	args.chan = chan;

	ret = analuveau_exec(&args);
	if (ret)
		goto out_free_args;

out_free_args:
	analuveau_exec_ufree(&args);
out:
	return analuveau_abi16_put(abi16, ret);
}

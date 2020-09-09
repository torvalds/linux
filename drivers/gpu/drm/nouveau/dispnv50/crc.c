// SPDX-License-Identifier: MIT
#include <linux/string.h>
#include <drm/drm_crtc.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_vblank.h>
#include <drm/drm_vblank_work.h>

#include <nvif/class.h>
#include <nvif/cl0002.h>
#include <nvif/timer.h>

#include <nvhw/class/cl907d.h>

#include "nouveau_drv.h"
#include "core.h"
#include "head.h"
#include "wndw.h"
#include "handles.h"
#include "crc.h"

static const char * const nv50_crc_sources[] = {
	[NV50_CRC_SOURCE_NONE] = "none",
	[NV50_CRC_SOURCE_AUTO] = "auto",
	[NV50_CRC_SOURCE_RG] = "rg",
	[NV50_CRC_SOURCE_OUTP_ACTIVE] = "outp-active",
	[NV50_CRC_SOURCE_OUTP_COMPLETE] = "outp-complete",
	[NV50_CRC_SOURCE_OUTP_INACTIVE] = "outp-inactive",
};

static int nv50_crc_parse_source(const char *buf, enum nv50_crc_source *s)
{
	int i;

	if (!buf) {
		*s = NV50_CRC_SOURCE_NONE;
		return 0;
	}

	i = match_string(nv50_crc_sources, ARRAY_SIZE(nv50_crc_sources), buf);
	if (i < 0)
		return i;

	*s = i;
	return 0;
}

int
nv50_crc_verify_source(struct drm_crtc *crtc, const char *source_name,
		       size_t *values_cnt)
{
	struct nouveau_drm *drm = nouveau_drm(crtc->dev);
	enum nv50_crc_source source;

	if (nv50_crc_parse_source(source_name, &source) < 0) {
		NV_DEBUG(drm, "unknown source %s\n", source_name);
		return -EINVAL;
	}

	*values_cnt = 1;
	return 0;
}

const char *const *nv50_crc_get_sources(struct drm_crtc *crtc, size_t *count)
{
	*count = ARRAY_SIZE(nv50_crc_sources);
	return nv50_crc_sources;
}

static void
nv50_crc_program_ctx(struct nv50_head *head,
		     struct nv50_crc_notifier_ctx *ctx)
{
	struct nv50_disp *disp = nv50_disp(head->base.base.dev);
	struct nv50_core *core = disp->core;
	u32 interlock[NV50_DISP_INTERLOCK__SIZE] = { 0 };

	core->func->crc->set_ctx(head, ctx);
	core->func->update(core, interlock, false);
}

static void nv50_crc_ctx_flip_work(struct kthread_work *base)
{
	struct drm_vblank_work *work = to_drm_vblank_work(base);
	struct nv50_crc *crc = container_of(work, struct nv50_crc, flip_work);
	struct nv50_head *head = container_of(crc, struct nv50_head, crc);
	struct drm_crtc *crtc = &head->base.base;
	struct nv50_disp *disp = nv50_disp(crtc->dev);
	u8 new_idx = crc->ctx_idx ^ 1;

	/*
	 * We don't want to accidentally wait for longer then the vblank, so
	 * try again for the next vblank if we don't grab the lock
	 */
	if (!mutex_trylock(&disp->mutex)) {
		DRM_DEV_DEBUG_KMS(crtc->dev->dev,
				  "Lock contended, delaying CRC ctx flip for head-%d\n",
				  head->base.index);
		drm_vblank_work_schedule(work,
					 drm_crtc_vblank_count(crtc) + 1,
					 true);
		return;
	}

	DRM_DEV_DEBUG_KMS(crtc->dev->dev,
			  "Flipping notifier ctx for head %d (%d -> %d)\n",
			  drm_crtc_index(crtc), crc->ctx_idx, new_idx);

	nv50_crc_program_ctx(head, NULL);
	nv50_crc_program_ctx(head, &crc->ctx[new_idx]);
	mutex_unlock(&disp->mutex);

	spin_lock_irq(&crc->lock);
	crc->ctx_changed = true;
	spin_unlock_irq(&crc->lock);
}

static inline void nv50_crc_reset_ctx(struct nv50_crc_notifier_ctx *ctx)
{
	memset_io(ctx->mem.object.map.ptr, 0, ctx->mem.object.map.size);
}

static void
nv50_crc_get_entries(struct nv50_head *head,
		     const struct nv50_crc_func *func,
		     enum nv50_crc_source source)
{
	struct drm_crtc *crtc = &head->base.base;
	struct nv50_crc *crc = &head->crc;
	u32 output_crc;

	while (crc->entry_idx < func->num_entries) {
		/*
		 * While Nvidia's documentation says CRCs are written on each
		 * subsequent vblank after being enabled, in practice they
		 * aren't written immediately.
		 */
		output_crc = func->get_entry(head, &crc->ctx[crc->ctx_idx],
					     source, crc->entry_idx);
		if (!output_crc)
			return;

		drm_crtc_add_crc_entry(crtc, true, crc->frame, &output_crc);
		crc->frame++;
		crc->entry_idx++;
	}
}

void nv50_crc_handle_vblank(struct nv50_head *head)
{
	struct drm_crtc *crtc = &head->base.base;
	struct nv50_crc *crc = &head->crc;
	const struct nv50_crc_func *func =
		nv50_disp(head->base.base.dev)->core->func->crc;
	struct nv50_crc_notifier_ctx *ctx;
	bool need_reschedule = false;

	if (!func)
		return;

	/*
	 * We don't lose events if we aren't able to report CRCs until the
	 * next vblank, so only report CRCs if the locks we need aren't
	 * contended to prevent missing an actual vblank event
	 */
	if (!spin_trylock(&crc->lock))
		return;

	if (!crc->src)
		goto out;

	ctx = &crc->ctx[crc->ctx_idx];
	if (crc->ctx_changed && func->ctx_finished(head, ctx)) {
		nv50_crc_get_entries(head, func, crc->src);

		crc->ctx_idx ^= 1;
		crc->entry_idx = 0;
		crc->ctx_changed = false;

		/*
		 * Unfortunately when notifier contexts are changed during CRC
		 * capture, we will inevitably lose the CRC entry for the
		 * frame where the hardware actually latched onto the first
		 * UPDATE. According to Nvidia's hardware engineers, there's
		 * no workaround for this.
		 *
		 * Now, we could try to be smart here and calculate the number
		 * of missed CRCs based on audit timestamps, but those were
		 * removed starting with volta. Since we always flush our
		 * updates back-to-back without waiting, we'll just be
		 * optimistic and assume we always miss exactly one frame.
		 */
		DRM_DEV_DEBUG_KMS(head->base.base.dev->dev,
				  "Notifier ctx flip for head-%d finished, lost CRC for frame %llu\n",
				  head->base.index, crc->frame);
		crc->frame++;

		nv50_crc_reset_ctx(ctx);
		need_reschedule = true;
	}

	nv50_crc_get_entries(head, func, crc->src);

	if (need_reschedule)
		drm_vblank_work_schedule(&crc->flip_work,
					 drm_crtc_vblank_count(crtc)
					 + crc->flip_threshold
					 - crc->entry_idx,
					 true);

out:
	spin_unlock(&crc->lock);
}

static void nv50_crc_wait_ctx_finished(struct nv50_head *head,
				       const struct nv50_crc_func *func,
				       struct nv50_crc_notifier_ctx *ctx)
{
	struct drm_device *dev = head->base.base.dev;
	struct nouveau_drm *drm = nouveau_drm(dev);
	s64 ret;

	ret = nvif_msec(&drm->client.device, 50,
			if (func->ctx_finished(head, ctx)) break;);
	if (ret == -ETIMEDOUT)
		NV_ERROR(drm,
			 "CRC notifier ctx for head %d not finished after 50ms\n",
			 head->base.index);
	else if (ret)
		NV_ATOMIC(drm,
			  "CRC notifier ctx for head-%d finished after %lldns\n",
			  head->base.index, ret);
}

void nv50_crc_atomic_stop_reporting(struct drm_atomic_state *state)
{
	struct drm_crtc_state *crtc_state;
	struct drm_crtc *crtc;
	int i;

	for_each_new_crtc_in_state(state, crtc, crtc_state, i) {
		struct nv50_head *head = nv50_head(crtc);
		struct nv50_head_atom *asyh = nv50_head_atom(crtc_state);
		struct nv50_crc *crc = &head->crc;

		if (!asyh->clr.crc)
			continue;

		spin_lock_irq(&crc->lock);
		crc->src = NV50_CRC_SOURCE_NONE;
		spin_unlock_irq(&crc->lock);

		drm_crtc_vblank_put(crtc);
		drm_vblank_work_cancel_sync(&crc->flip_work);

		NV_ATOMIC(nouveau_drm(crtc->dev),
			  "CRC reporting on vblank for head-%d disabled\n",
			  head->base.index);

		/* CRC generation is still enabled in hw, we'll just report
		 * any remaining CRC entries ourselves after it gets disabled
		 * in hardware
		 */
	}
}

void nv50_crc_atomic_init_notifier_contexts(struct drm_atomic_state *state)
{
	struct drm_crtc_state *new_crtc_state;
	struct drm_crtc *crtc;
	int i;

	for_each_new_crtc_in_state(state, crtc, new_crtc_state, i) {
		struct nv50_head *head = nv50_head(crtc);
		struct nv50_head_atom *asyh = nv50_head_atom(new_crtc_state);
		struct nv50_crc *crc = &head->crc;
		int i;

		if (!asyh->set.crc)
			continue;

		crc->entry_idx = 0;
		crc->ctx_changed = false;
		for (i = 0; i < ARRAY_SIZE(crc->ctx); i++)
			nv50_crc_reset_ctx(&crc->ctx[i]);
	}
}

void nv50_crc_atomic_release_notifier_contexts(struct drm_atomic_state *state)
{
	const struct nv50_crc_func *func =
		nv50_disp(state->dev)->core->func->crc;
	struct drm_crtc_state *new_crtc_state;
	struct drm_crtc *crtc;
	int i;

	for_each_new_crtc_in_state(state, crtc, new_crtc_state, i) {
		struct nv50_head *head = nv50_head(crtc);
		struct nv50_head_atom *asyh = nv50_head_atom(new_crtc_state);
		struct nv50_crc *crc = &head->crc;
		struct nv50_crc_notifier_ctx *ctx = &crc->ctx[crc->ctx_idx];

		if (!asyh->clr.crc)
			continue;

		if (crc->ctx_changed) {
			nv50_crc_wait_ctx_finished(head, func, ctx);
			ctx = &crc->ctx[crc->ctx_idx ^ 1];
		}
		nv50_crc_wait_ctx_finished(head, func, ctx);
	}
}

void nv50_crc_atomic_start_reporting(struct drm_atomic_state *state)
{
	struct drm_crtc_state *crtc_state;
	struct drm_crtc *crtc;
	int i;

	for_each_new_crtc_in_state(state, crtc, crtc_state, i) {
		struct nv50_head *head = nv50_head(crtc);
		struct nv50_head_atom *asyh = nv50_head_atom(crtc_state);
		struct nv50_crc *crc = &head->crc;
		u64 vbl_count;

		if (!asyh->set.crc)
			continue;

		drm_crtc_vblank_get(crtc);

		spin_lock_irq(&crc->lock);
		vbl_count = drm_crtc_vblank_count(crtc);
		crc->frame = vbl_count;
		crc->src = asyh->crc.src;
		drm_vblank_work_schedule(&crc->flip_work,
					 vbl_count + crc->flip_threshold,
					 true);
		spin_unlock_irq(&crc->lock);

		NV_ATOMIC(nouveau_drm(crtc->dev),
			  "CRC reporting on vblank for head-%d enabled\n",
			  head->base.index);
	}
}

int nv50_crc_atomic_check_head(struct nv50_head *head,
			       struct nv50_head_atom *asyh,
			       struct nv50_head_atom *armh)
{
	struct nv50_atom *atom = nv50_atom(asyh->state.state);
	struct drm_device *dev = head->base.base.dev;
	struct nv50_disp *disp = nv50_disp(dev);
	bool changed = armh->crc.src != asyh->crc.src;

	if (!armh->crc.src && !asyh->crc.src) {
		asyh->set.crc = false;
		asyh->clr.crc = false;
		return 0;
	}

	/* While we don't care about entry tags, Volta+ hw always needs the
	 * controlling wndw channel programmed to a wndw that's owned by our
	 * head
	 */
	if (asyh->crc.src && disp->disp->object.oclass >= GV100_DISP &&
	    !(BIT(asyh->crc.wndw) & asyh->wndw.owned)) {
		if (!asyh->wndw.owned) {
			/* TODO: once we support flexible channel ownership,
			 * we should write some code here to handle attempting
			 * to "steal" a plane: e.g. take a plane that is
			 * currently not-visible and owned by another head,
			 * and reassign it to this head. If we fail to do so,
			 * we shuld reject the mode outright as CRC capture
			 * then becomes impossible.
			 */
			NV_ATOMIC(nouveau_drm(dev),
				  "No available wndws for CRC readback\n");
			return -EINVAL;
		}
		asyh->crc.wndw = ffs(asyh->wndw.owned) - 1;
	}

	if (drm_atomic_crtc_needs_modeset(&asyh->state) || changed ||
	    armh->crc.wndw != asyh->crc.wndw) {
		asyh->clr.crc = armh->crc.src && armh->state.active;
		asyh->set.crc = asyh->crc.src && asyh->state.active;
		if (changed)
			asyh->set.or |= armh->or.crc_raster !=
					asyh->or.crc_raster;

		if (asyh->clr.crc && asyh->set.crc)
			atom->flush_disable = true;
	} else {
		asyh->set.crc = false;
		asyh->clr.crc = false;
	}

	return 0;
}

void nv50_crc_atomic_check_outp(struct nv50_atom *atom)
{
	struct drm_crtc *crtc;
	struct drm_crtc_state *old_crtc_state, *new_crtc_state;
	int i;

	if (atom->flush_disable)
		return;

	for_each_oldnew_crtc_in_state(&atom->state, crtc, old_crtc_state,
				      new_crtc_state, i) {
		struct nv50_head_atom *armh = nv50_head_atom(old_crtc_state);
		struct nv50_head_atom *asyh = nv50_head_atom(new_crtc_state);
		struct nv50_outp_atom *outp_atom;
		struct nouveau_encoder *outp =
			nv50_real_outp(nv50_head_atom_get_encoder(armh));
		struct drm_encoder *encoder = &outp->base.base;

		if (!asyh->clr.crc)
			continue;

		/*
		 * Re-programming ORs can't be done in the same flush as
		 * disabling CRCs
		 */
		list_for_each_entry(outp_atom, &atom->outp, head) {
			if (outp_atom->encoder == encoder) {
				if (outp_atom->set.mask) {
					atom->flush_disable = true;
					return;
				} else {
					break;
				}
			}
		}
	}
}

static enum nv50_crc_source_type
nv50_crc_source_type(struct nouveau_encoder *outp,
		     enum nv50_crc_source source)
{
	struct dcb_output *dcbe = outp->dcb;

	switch (source) {
	case NV50_CRC_SOURCE_NONE: return NV50_CRC_SOURCE_TYPE_NONE;
	case NV50_CRC_SOURCE_RG:   return NV50_CRC_SOURCE_TYPE_RG;
	default:		   break;
	}

	if (dcbe->location != DCB_LOC_ON_CHIP)
		return NV50_CRC_SOURCE_TYPE_PIOR;

	switch (dcbe->type) {
	case DCB_OUTPUT_DP:	return NV50_CRC_SOURCE_TYPE_SF;
	case DCB_OUTPUT_ANALOG:	return NV50_CRC_SOURCE_TYPE_DAC;
	default:		return NV50_CRC_SOURCE_TYPE_SOR;
	}
}

void nv50_crc_atomic_set(struct nv50_head *head,
			 struct nv50_head_atom *asyh)
{
	struct drm_crtc *crtc = &head->base.base;
	struct drm_device *dev = crtc->dev;
	struct nv50_crc *crc = &head->crc;
	const struct nv50_crc_func *func = nv50_disp(dev)->core->func->crc;
	struct nouveau_encoder *outp =
		nv50_real_outp(nv50_head_atom_get_encoder(asyh));

	func->set_src(head, outp->or,
		      nv50_crc_source_type(outp, asyh->crc.src),
		      &crc->ctx[crc->ctx_idx], asyh->crc.wndw);
}

void nv50_crc_atomic_clr(struct nv50_head *head)
{
	const struct nv50_crc_func *func =
		nv50_disp(head->base.base.dev)->core->func->crc;

	func->set_src(head, 0, NV50_CRC_SOURCE_TYPE_NONE, NULL, 0);
}

static inline int
nv50_crc_raster_type(enum nv50_crc_source source)
{
	switch (source) {
	case NV50_CRC_SOURCE_NONE:
	case NV50_CRC_SOURCE_AUTO:
	case NV50_CRC_SOURCE_RG:
	case NV50_CRC_SOURCE_OUTP_ACTIVE:
		return NV907D_HEAD_SET_CONTROL_OUTPUT_RESOURCE_CRC_MODE_ACTIVE_RASTER;
	case NV50_CRC_SOURCE_OUTP_COMPLETE:
		return NV907D_HEAD_SET_CONTROL_OUTPUT_RESOURCE_CRC_MODE_COMPLETE_RASTER;
	case NV50_CRC_SOURCE_OUTP_INACTIVE:
		return NV907D_HEAD_SET_CONTROL_OUTPUT_RESOURCE_CRC_MODE_NON_ACTIVE_RASTER;
	}

	return 0;
}

/* We handle mapping the memory for CRC notifiers ourselves, since each
 * notifier needs it's own handle
 */
static inline int
nv50_crc_ctx_init(struct nv50_head *head, struct nvif_mmu *mmu,
		  struct nv50_crc_notifier_ctx *ctx, size_t len, int idx)
{
	struct nv50_core *core = nv50_disp(head->base.base.dev)->core;
	int ret;

	ret = nvif_mem_ctor_map(mmu, "kmsCrcNtfy", NVIF_MEM_VRAM, len, &ctx->mem);
	if (ret)
		return ret;

	ret = nvif_object_ctor(&core->chan.base.user, "kmsCrcNtfyCtxDma",
			       NV50_DISP_HANDLE_CRC_CTX(head, idx),
			       NV_DMA_IN_MEMORY,
			       &(struct nv_dma_v0) {
					.target = NV_DMA_V0_TARGET_VRAM,
					.access = NV_DMA_V0_ACCESS_RDWR,
					.start = ctx->mem.addr,
					.limit =  ctx->mem.addr
						+ ctx->mem.size - 1,
			       }, sizeof(struct nv_dma_v0),
			       &ctx->ntfy);
	if (ret)
		goto fail_fini;

	return 0;

fail_fini:
	nvif_mem_dtor(&ctx->mem);
	return ret;
}

static inline void
nv50_crc_ctx_fini(struct nv50_crc_notifier_ctx *ctx)
{
	nvif_object_dtor(&ctx->ntfy);
	nvif_mem_dtor(&ctx->mem);
}

int nv50_crc_set_source(struct drm_crtc *crtc, const char *source_str)
{
	struct drm_device *dev = crtc->dev;
	struct drm_atomic_state *state;
	struct drm_modeset_acquire_ctx ctx;
	struct nv50_head *head = nv50_head(crtc);
	struct nv50_crc *crc = &head->crc;
	const struct nv50_crc_func *func = nv50_disp(dev)->core->func->crc;
	struct nvif_mmu *mmu = &nouveau_drm(dev)->client.mmu;
	struct nv50_head_atom *asyh;
	struct drm_crtc_state *crtc_state;
	enum nv50_crc_source source;
	int ret = 0, ctx_flags = 0, i;

	ret = nv50_crc_parse_source(source_str, &source);
	if (ret)
		return ret;

	/*
	 * Since we don't want the user to accidentally interrupt us as we're
	 * disabling CRCs
	 */
	if (source)
		ctx_flags |= DRM_MODESET_ACQUIRE_INTERRUPTIBLE;
	drm_modeset_acquire_init(&ctx, ctx_flags);

	state = drm_atomic_state_alloc(dev);
	if (!state) {
		ret = -ENOMEM;
		goto out_acquire_fini;
	}
	state->acquire_ctx = &ctx;

	if (source) {
		for (i = 0; i < ARRAY_SIZE(head->crc.ctx); i++) {
			ret = nv50_crc_ctx_init(head, mmu, &crc->ctx[i],
						func->notifier_len, i);
			if (ret)
				goto out_ctx_fini;
		}
	}

retry:
	crtc_state = drm_atomic_get_crtc_state(state, &head->base.base);
	if (IS_ERR(crtc_state)) {
		ret = PTR_ERR(crtc_state);
		if (ret == -EDEADLK)
			goto deadlock;
		else if (ret)
			goto out_drop_locks;
	}
	asyh = nv50_head_atom(crtc_state);
	asyh->crc.src = source;
	asyh->or.crc_raster = nv50_crc_raster_type(source);

	ret = drm_atomic_commit(state);
	if (ret == -EDEADLK)
		goto deadlock;
	else if (ret)
		goto out_drop_locks;

	if (!source) {
		/*
		 * If the user specified a custom flip threshold through
		 * debugfs, reset it
		 */
		crc->flip_threshold = func->flip_threshold;
	}

out_drop_locks:
	drm_modeset_drop_locks(&ctx);
out_ctx_fini:
	if (!source || ret) {
		for (i = 0; i < ARRAY_SIZE(crc->ctx); i++)
			nv50_crc_ctx_fini(&crc->ctx[i]);
	}
	drm_atomic_state_put(state);
out_acquire_fini:
	drm_modeset_acquire_fini(&ctx);
	return ret;

deadlock:
	drm_atomic_state_clear(state);
	drm_modeset_backoff(&ctx);
	goto retry;
}

static int
nv50_crc_debugfs_flip_threshold_get(struct seq_file *m, void *data)
{
	struct nv50_head *head = m->private;
	struct drm_crtc *crtc = &head->base.base;
	struct nv50_crc *crc = &head->crc;
	int ret;

	ret = drm_modeset_lock_single_interruptible(&crtc->mutex);
	if (ret)
		return ret;

	seq_printf(m, "%d\n", crc->flip_threshold);

	drm_modeset_unlock(&crtc->mutex);
	return ret;
}

static int
nv50_crc_debugfs_flip_threshold_open(struct inode *inode, struct file *file)
{
	return single_open(file, nv50_crc_debugfs_flip_threshold_get,
			   inode->i_private);
}

static ssize_t
nv50_crc_debugfs_flip_threshold_set(struct file *file,
				    const char __user *ubuf, size_t len,
				    loff_t *offp)
{
	struct seq_file *m = file->private_data;
	struct nv50_head *head = m->private;
	struct nv50_head_atom *armh;
	struct drm_crtc *crtc = &head->base.base;
	struct nouveau_drm *drm = nouveau_drm(crtc->dev);
	struct nv50_crc *crc = &head->crc;
	const struct nv50_crc_func *func =
		nv50_disp(crtc->dev)->core->func->crc;
	int value, ret;

	ret = kstrtoint_from_user(ubuf, len, 10, &value);
	if (ret)
		return ret;

	if (value > func->flip_threshold)
		return -EINVAL;
	else if (value == -1)
		value = func->flip_threshold;
	else if (value < -1)
		return -EINVAL;

	ret = drm_modeset_lock_single_interruptible(&crtc->mutex);
	if (ret)
		return ret;

	armh = nv50_head_atom(crtc->state);
	if (armh->crc.src) {
		ret = -EBUSY;
		goto out;
	}

	NV_DEBUG(drm,
		 "Changing CRC flip threshold for next capture on head-%d to %d\n",
		 head->base.index, value);
	crc->flip_threshold = value;
	ret = len;

out:
	drm_modeset_unlock(&crtc->mutex);
	return ret;
}

static const struct file_operations nv50_crc_flip_threshold_fops = {
	.owner = THIS_MODULE,
	.open = nv50_crc_debugfs_flip_threshold_open,
	.read = seq_read,
	.write = nv50_crc_debugfs_flip_threshold_set,
};

int nv50_head_crc_late_register(struct nv50_head *head)
{
	struct drm_crtc *crtc = &head->base.base;
	const struct nv50_crc_func *func =
		nv50_disp(crtc->dev)->core->func->crc;
	struct dentry *root;

	if (!func || !crtc->debugfs_entry)
		return 0;

	root = debugfs_create_dir("nv_crc", crtc->debugfs_entry);
	debugfs_create_file("flip_threshold", 0644, root, head,
			    &nv50_crc_flip_threshold_fops);

	return 0;
}

static inline void
nv50_crc_init_head(struct nv50_disp *disp, const struct nv50_crc_func *func,
		   struct nv50_head *head)
{
	struct nv50_crc *crc = &head->crc;

	crc->flip_threshold = func->flip_threshold;
	spin_lock_init(&crc->lock);
	drm_vblank_work_init(&crc->flip_work, &head->base.base,
			     nv50_crc_ctx_flip_work);
}

void nv50_crc_init(struct drm_device *dev)
{
	struct nv50_disp *disp = nv50_disp(dev);
	struct drm_crtc *crtc;
	const struct nv50_crc_func *func = disp->core->func->crc;

	if (!func)
		return;

	drm_for_each_crtc(crtc, dev)
		nv50_crc_init_head(disp, func, nv50_head(crtc));
}

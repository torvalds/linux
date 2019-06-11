/*
 * Copyright © 2014-2017 Intel Corporation
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
 *
 */

#include "intel_guc.h"
#include "intel_guc_ads.h"
#include "intel_guc_submission.h"
#include "i915_drv.h"

static void gen8_guc_raise_irq(struct intel_guc *guc)
{
	struct drm_i915_private *dev_priv = guc_to_i915(guc);

	I915_WRITE(GUC_SEND_INTERRUPT, GUC_SEND_TRIGGER);
}

static void gen11_guc_raise_irq(struct intel_guc *guc)
{
	struct drm_i915_private *dev_priv = guc_to_i915(guc);

	I915_WRITE(GEN11_GUC_HOST_INTERRUPT, 0);
}

static inline i915_reg_t guc_send_reg(struct intel_guc *guc, u32 i)
{
	GEM_BUG_ON(!guc->send_regs.base);
	GEM_BUG_ON(!guc->send_regs.count);
	GEM_BUG_ON(i >= guc->send_regs.count);

	return _MMIO(guc->send_regs.base + 4 * i);
}

void intel_guc_init_send_regs(struct intel_guc *guc)
{
	struct drm_i915_private *dev_priv = guc_to_i915(guc);
	enum forcewake_domains fw_domains = 0;
	unsigned int i;

	if (INTEL_GEN(dev_priv) >= 11) {
		guc->send_regs.base =
				i915_mmio_reg_offset(GEN11_SOFT_SCRATCH(0));
		guc->send_regs.count = GEN11_SOFT_SCRATCH_COUNT;
	} else {
		guc->send_regs.base = i915_mmio_reg_offset(SOFT_SCRATCH(0));
		guc->send_regs.count = GUC_MAX_MMIO_MSG_LEN;
		BUILD_BUG_ON(GUC_MAX_MMIO_MSG_LEN > SOFT_SCRATCH_COUNT);
	}

	for (i = 0; i < guc->send_regs.count; i++) {
		fw_domains |= intel_uncore_forcewake_for_reg(&dev_priv->uncore,
					guc_send_reg(guc, i),
					FW_REG_READ | FW_REG_WRITE);
	}
	guc->send_regs.fw_domains = fw_domains;
}

void intel_guc_init_early(struct intel_guc *guc)
{
	struct drm_i915_private *i915 = guc_to_i915(guc);

	intel_guc_fw_init_early(guc);
	intel_guc_ct_init_early(&guc->ct);
	intel_guc_log_init_early(&guc->log);

	mutex_init(&guc->send_mutex);
	spin_lock_init(&guc->irq_lock);
	guc->send = intel_guc_send_nop;
	guc->handler = intel_guc_to_host_event_handler_nop;
	if (INTEL_GEN(i915) >= 11) {
		guc->notify = gen11_guc_raise_irq;
		guc->interrupts.reset = gen11_reset_guc_interrupts;
		guc->interrupts.enable = gen11_enable_guc_interrupts;
		guc->interrupts.disable = gen11_disable_guc_interrupts;
	} else {
		guc->notify = gen8_guc_raise_irq;
		guc->interrupts.reset = gen9_reset_guc_interrupts;
		guc->interrupts.enable = gen9_enable_guc_interrupts;
		guc->interrupts.disable = gen9_disable_guc_interrupts;
	}
}

static int guc_init_wq(struct intel_guc *guc)
{
	struct drm_i915_private *dev_priv = guc_to_i915(guc);

	/*
	 * GuC log buffer flush work item has to do register access to
	 * send the ack to GuC and this work item, if not synced before
	 * suspend, can potentially get executed after the GFX device is
	 * suspended.
	 * By marking the WQ as freezable, we don't have to bother about
	 * flushing of this work item from the suspend hooks, the pending
	 * work item if any will be either executed before the suspend
	 * or scheduled later on resume. This way the handling of work
	 * item can be kept same between system suspend & rpm suspend.
	 */
	guc->log.relay.flush_wq =
		alloc_ordered_workqueue("i915-guc_log",
					WQ_HIGHPRI | WQ_FREEZABLE);
	if (!guc->log.relay.flush_wq) {
		DRM_ERROR("Couldn't allocate workqueue for GuC log\n");
		return -ENOMEM;
	}

	/*
	 * Even though both sending GuC action, and adding a new workitem to
	 * GuC workqueue are serialized (each with its own locking), since
	 * we're using mutliple engines, it's possible that we're going to
	 * issue a preempt request with two (or more - each for different
	 * engine) workitems in GuC queue. In this situation, GuC may submit
	 * all of them, which will make us very confused.
	 * Our preemption contexts may even already be complete - before we
	 * even had the chance to sent the preempt action to GuC!. Rather
	 * than introducing yet another lock, we can just use ordered workqueue
	 * to make sure we're always sending a single preemption request with a
	 * single workitem.
	 */
	if (HAS_LOGICAL_RING_PREEMPTION(dev_priv) &&
	    USES_GUC_SUBMISSION(dev_priv)) {
		guc->preempt_wq = alloc_ordered_workqueue("i915-guc_preempt",
							  WQ_HIGHPRI);
		if (!guc->preempt_wq) {
			destroy_workqueue(guc->log.relay.flush_wq);
			DRM_ERROR("Couldn't allocate workqueue for GuC "
				  "preemption\n");
			return -ENOMEM;
		}
	}

	return 0;
}

static void guc_fini_wq(struct intel_guc *guc)
{
	struct workqueue_struct *wq;

	wq = fetch_and_zero(&guc->preempt_wq);
	if (wq)
		destroy_workqueue(wq);

	wq = fetch_and_zero(&guc->log.relay.flush_wq);
	if (wq)
		destroy_workqueue(wq);
}

int intel_guc_init_misc(struct intel_guc *guc)
{
	struct drm_i915_private *i915 = guc_to_i915(guc);
	int ret;

	ret = guc_init_wq(guc);
	if (ret)
		return ret;

	intel_uc_fw_fetch(i915, &guc->fw);

	return 0;
}

void intel_guc_fini_misc(struct intel_guc *guc)
{
	intel_uc_fw_cleanup_fetch(&guc->fw);
	guc_fini_wq(guc);
}

static int guc_shared_data_create(struct intel_guc *guc)
{
	struct i915_vma *vma;
	void *vaddr;

	vma = intel_guc_allocate_vma(guc, PAGE_SIZE);
	if (IS_ERR(vma))
		return PTR_ERR(vma);

	vaddr = i915_gem_object_pin_map(vma->obj, I915_MAP_WB);
	if (IS_ERR(vaddr)) {
		i915_vma_unpin_and_release(&vma, 0);
		return PTR_ERR(vaddr);
	}

	guc->shared_data = vma;
	guc->shared_data_vaddr = vaddr;

	return 0;
}

static void guc_shared_data_destroy(struct intel_guc *guc)
{
	i915_vma_unpin_and_release(&guc->shared_data, I915_VMA_RELEASE_MAP);
}

int intel_guc_init(struct intel_guc *guc)
{
	struct drm_i915_private *dev_priv = guc_to_i915(guc);
	int ret;

	ret = intel_uc_fw_init(&guc->fw);
	if (ret)
		goto err_fetch;

	ret = guc_shared_data_create(guc);
	if (ret)
		goto err_fw;
	GEM_BUG_ON(!guc->shared_data);

	ret = intel_guc_log_create(&guc->log);
	if (ret)
		goto err_shared;

	ret = intel_guc_ads_create(guc);
	if (ret)
		goto err_log;
	GEM_BUG_ON(!guc->ads_vma);

	ret = intel_guc_ct_init(&guc->ct);
	if (ret)
		goto err_ads;

	/* We need to notify the guc whenever we change the GGTT */
	i915_ggtt_enable_guc(dev_priv);

	return 0;

err_ads:
	intel_guc_ads_destroy(guc);
err_log:
	intel_guc_log_destroy(&guc->log);
err_shared:
	guc_shared_data_destroy(guc);
err_fw:
	intel_uc_fw_fini(&guc->fw);
err_fetch:
	intel_uc_fw_cleanup_fetch(&guc->fw);
	return ret;
}

void intel_guc_fini(struct intel_guc *guc)
{
	struct drm_i915_private *dev_priv = guc_to_i915(guc);

	i915_ggtt_disable_guc(dev_priv);

	intel_guc_ct_fini(&guc->ct);

	intel_guc_ads_destroy(guc);
	intel_guc_log_destroy(&guc->log);
	guc_shared_data_destroy(guc);
	intel_uc_fw_fini(&guc->fw);
	intel_uc_fw_cleanup_fetch(&guc->fw);
}

static u32 guc_ctl_debug_flags(struct intel_guc *guc)
{
	u32 level = intel_guc_log_get_level(&guc->log);
	u32 flags = 0;

	if (!GUC_LOG_LEVEL_IS_VERBOSE(level))
		flags |= GUC_LOG_DISABLED;
	else
		flags |= GUC_LOG_LEVEL_TO_VERBOSITY(level) <<
			 GUC_LOG_VERBOSITY_SHIFT;

	return flags;
}

static u32 guc_ctl_feature_flags(struct intel_guc *guc)
{
	u32 flags = 0;

	if (!USES_GUC_SUBMISSION(guc_to_i915(guc)))
		flags |= GUC_CTL_DISABLE_SCHEDULER;

	return flags;
}

static u32 guc_ctl_ctxinfo_flags(struct intel_guc *guc)
{
	u32 flags = 0;

	if (USES_GUC_SUBMISSION(guc_to_i915(guc))) {
		u32 ctxnum, base;

		base = intel_guc_ggtt_offset(guc, guc->stage_desc_pool);
		ctxnum = GUC_MAX_STAGE_DESCRIPTORS / 16;

		base >>= PAGE_SHIFT;
		flags |= (base << GUC_CTL_BASE_ADDR_SHIFT) |
			(ctxnum << GUC_CTL_CTXNUM_IN16_SHIFT);
	}
	return flags;
}

static u32 guc_ctl_log_params_flags(struct intel_guc *guc)
{
	u32 offset = intel_guc_ggtt_offset(guc, guc->log.vma) >> PAGE_SHIFT;
	u32 flags;

	#if (((CRASH_BUFFER_SIZE) % SZ_1M) == 0)
	#define UNIT SZ_1M
	#define FLAG GUC_LOG_ALLOC_IN_MEGABYTE
	#else
	#define UNIT SZ_4K
	#define FLAG 0
	#endif

	BUILD_BUG_ON(!CRASH_BUFFER_SIZE);
	BUILD_BUG_ON(!IS_ALIGNED(CRASH_BUFFER_SIZE, UNIT));
	BUILD_BUG_ON(!DPC_BUFFER_SIZE);
	BUILD_BUG_ON(!IS_ALIGNED(DPC_BUFFER_SIZE, UNIT));
	BUILD_BUG_ON(!ISR_BUFFER_SIZE);
	BUILD_BUG_ON(!IS_ALIGNED(ISR_BUFFER_SIZE, UNIT));

	BUILD_BUG_ON((CRASH_BUFFER_SIZE / UNIT - 1) >
			(GUC_LOG_CRASH_MASK >> GUC_LOG_CRASH_SHIFT));
	BUILD_BUG_ON((DPC_BUFFER_SIZE / UNIT - 1) >
			(GUC_LOG_DPC_MASK >> GUC_LOG_DPC_SHIFT));
	BUILD_BUG_ON((ISR_BUFFER_SIZE / UNIT - 1) >
			(GUC_LOG_ISR_MASK >> GUC_LOG_ISR_SHIFT));

	flags = GUC_LOG_VALID |
		GUC_LOG_NOTIFY_ON_HALF_FULL |
		FLAG |
		((CRASH_BUFFER_SIZE / UNIT - 1) << GUC_LOG_CRASH_SHIFT) |
		((DPC_BUFFER_SIZE / UNIT - 1) << GUC_LOG_DPC_SHIFT) |
		((ISR_BUFFER_SIZE / UNIT - 1) << GUC_LOG_ISR_SHIFT) |
		(offset << GUC_LOG_BUF_ADDR_SHIFT);

	#undef UNIT
	#undef FLAG

	return flags;
}

static u32 guc_ctl_ads_flags(struct intel_guc *guc)
{
	u32 ads = intel_guc_ggtt_offset(guc, guc->ads_vma) >> PAGE_SHIFT;
	u32 flags = ads << GUC_ADS_ADDR_SHIFT;

	return flags;
}

/*
 * Initialise the GuC parameter block before starting the firmware
 * transfer. These parameters are read by the firmware on startup
 * and cannot be changed thereafter.
 */
void intel_guc_init_params(struct intel_guc *guc)
{
	struct drm_i915_private *dev_priv = guc_to_i915(guc);
	u32 params[GUC_CTL_MAX_DWORDS];
	int i;

	memset(params, 0, sizeof(params));

	params[GUC_CTL_CTXINFO] = guc_ctl_ctxinfo_flags(guc);
	params[GUC_CTL_LOG_PARAMS] = guc_ctl_log_params_flags(guc);
	params[GUC_CTL_FEATURE] = guc_ctl_feature_flags(guc);
	params[GUC_CTL_DEBUG] = guc_ctl_debug_flags(guc);
	params[GUC_CTL_ADS] = guc_ctl_ads_flags(guc);

	for (i = 0; i < GUC_CTL_MAX_DWORDS; i++)
		DRM_DEBUG_DRIVER("param[%2d] = %#x\n", i, params[i]);

	/*
	 * All SOFT_SCRATCH registers are in FORCEWAKE_BLITTER domain and
	 * they are power context saved so it's ok to release forcewake
	 * when we are done here and take it again at xfer time.
	 */
	intel_uncore_forcewake_get(&dev_priv->uncore, FORCEWAKE_BLITTER);

	I915_WRITE(SOFT_SCRATCH(0), 0);

	for (i = 0; i < GUC_CTL_MAX_DWORDS; i++)
		I915_WRITE(SOFT_SCRATCH(1 + i), params[i]);

	intel_uncore_forcewake_put(&dev_priv->uncore, FORCEWAKE_BLITTER);
}

int intel_guc_send_nop(struct intel_guc *guc, const u32 *action, u32 len,
		       u32 *response_buf, u32 response_buf_size)
{
	WARN(1, "Unexpected send: action=%#x\n", *action);
	return -ENODEV;
}

void intel_guc_to_host_event_handler_nop(struct intel_guc *guc)
{
	WARN(1, "Unexpected event: no suitable handler\n");
}

/*
 * This function implements the MMIO based host to GuC interface.
 */
int intel_guc_send_mmio(struct intel_guc *guc, const u32 *action, u32 len,
			u32 *response_buf, u32 response_buf_size)
{
	struct drm_i915_private *dev_priv = guc_to_i915(guc);
	struct intel_uncore *uncore = &dev_priv->uncore;
	u32 status;
	int i;
	int ret;

	GEM_BUG_ON(!len);
	GEM_BUG_ON(len > guc->send_regs.count);

	/* We expect only action code */
	GEM_BUG_ON(*action & ~INTEL_GUC_MSG_CODE_MASK);

	/* If CT is available, we expect to use MMIO only during init/fini */
	GEM_BUG_ON(*action != INTEL_GUC_ACTION_REGISTER_COMMAND_TRANSPORT_BUFFER &&
		   *action != INTEL_GUC_ACTION_DEREGISTER_COMMAND_TRANSPORT_BUFFER);

	mutex_lock(&guc->send_mutex);
	intel_uncore_forcewake_get(uncore, guc->send_regs.fw_domains);

	for (i = 0; i < len; i++)
		intel_uncore_write(uncore, guc_send_reg(guc, i), action[i]);

	intel_uncore_posting_read(uncore, guc_send_reg(guc, i - 1));

	intel_guc_notify(guc);

	/*
	 * No GuC command should ever take longer than 10ms.
	 * Fast commands should still complete in 10us.
	 */
	ret = __intel_wait_for_register_fw(uncore,
					   guc_send_reg(guc, 0),
					   INTEL_GUC_MSG_TYPE_MASK,
					   INTEL_GUC_MSG_TYPE_RESPONSE <<
					   INTEL_GUC_MSG_TYPE_SHIFT,
					   10, 10, &status);
	/* If GuC explicitly returned an error, convert it to -EIO */
	if (!ret && !INTEL_GUC_MSG_IS_RESPONSE_SUCCESS(status))
		ret = -EIO;

	if (ret) {
		DRM_ERROR("MMIO: GuC action %#x failed with error %d %#x\n",
			  action[0], ret, status);
		goto out;
	}

	if (response_buf) {
		int count = min(response_buf_size, guc->send_regs.count - 1);

		for (i = 0; i < count; i++)
			response_buf[i] = I915_READ(guc_send_reg(guc, i + 1));
	}

	/* Use data from the GuC response as our return value */
	ret = INTEL_GUC_MSG_TO_DATA(status);

out:
	intel_uncore_forcewake_put(uncore, guc->send_regs.fw_domains);
	mutex_unlock(&guc->send_mutex);

	return ret;
}

int intel_guc_to_host_process_recv_msg(struct intel_guc *guc,
				       const u32 *payload, u32 len)
{
	u32 msg;

	if (unlikely(!len))
		return -EPROTO;

	/* Make sure to handle only enabled messages */
	msg = payload[0] & guc->msg_enabled_mask;

	if (msg & (INTEL_GUC_RECV_MSG_FLUSH_LOG_BUFFER |
		   INTEL_GUC_RECV_MSG_CRASH_DUMP_POSTED))
		intel_guc_log_handle_flush_event(&guc->log);

	return 0;
}

int intel_guc_sample_forcewake(struct intel_guc *guc)
{
	struct drm_i915_private *dev_priv = guc_to_i915(guc);
	u32 action[2];

	action[0] = INTEL_GUC_ACTION_SAMPLE_FORCEWAKE;
	/* WaRsDisableCoarsePowerGating:skl,cnl */
	if (!HAS_RC6(dev_priv) || NEEDS_WaRsDisableCoarsePowerGating(dev_priv))
		action[1] = 0;
	else
		/* bit 0 and 1 are for Render and Media domain separately */
		action[1] = GUC_FORCEWAKE_RENDER | GUC_FORCEWAKE_MEDIA;

	return intel_guc_send(guc, action, ARRAY_SIZE(action));
}

/**
 * intel_guc_auth_huc() - Send action to GuC to authenticate HuC ucode
 * @guc: intel_guc structure
 * @rsa_offset: rsa offset w.r.t ggtt base of huc vma
 *
 * Triggers a HuC firmware authentication request to the GuC via intel_guc_send
 * INTEL_GUC_ACTION_AUTHENTICATE_HUC interface. This function is invoked by
 * intel_huc_auth().
 *
 * Return:	non-zero code on error
 */
int intel_guc_auth_huc(struct intel_guc *guc, u32 rsa_offset)
{
	u32 action[] = {
		INTEL_GUC_ACTION_AUTHENTICATE_HUC,
		rsa_offset
	};

	return intel_guc_send(guc, action, ARRAY_SIZE(action));
}

/**
 * intel_guc_suspend() - notify GuC entering suspend state
 * @guc:	the guc
 */
int intel_guc_suspend(struct intel_guc *guc)
{
	struct drm_i915_private *dev_priv = guc_to_i915(guc);
	int ret;
	u32 status;
	u32 action[] = {
		INTEL_GUC_ACTION_ENTER_S_STATE,
		GUC_POWER_D1, /* any value greater than GUC_POWER_D0 */
	};

	/*
	 * The ENTER_S_STATE action queues the save/restore operation in GuC FW
	 * and then returns, so waiting on the H2G is not enough to guarantee
	 * GuC is done. When all the processing is done, GuC writes
	 * INTEL_GUC_SLEEP_STATE_SUCCESS to scratch register 14, so we can poll
	 * on that. Note that GuC does not ensure that the value in the register
	 * is different from INTEL_GUC_SLEEP_STATE_SUCCESS while the action is
	 * in progress so we need to take care of that ourselves as well.
	 */

	I915_WRITE(SOFT_SCRATCH(14), INTEL_GUC_SLEEP_STATE_INVALID_MASK);

	ret = intel_guc_send(guc, action, ARRAY_SIZE(action));
	if (ret)
		return ret;

	ret = __intel_wait_for_register(&dev_priv->uncore, SOFT_SCRATCH(14),
					INTEL_GUC_SLEEP_STATE_INVALID_MASK,
					0, 0, 10, &status);
	if (ret)
		return ret;

	if (status != INTEL_GUC_SLEEP_STATE_SUCCESS) {
		DRM_ERROR("GuC failed to change sleep state. "
			  "action=0x%x, err=%u\n",
			  action[0], status);
		return -EIO;
	}

	return 0;
}

/**
 * intel_guc_reset_engine() - ask GuC to reset an engine
 * @guc:	intel_guc structure
 * @engine:	engine to be reset
 */
int intel_guc_reset_engine(struct intel_guc *guc,
			   struct intel_engine_cs *engine)
{
	u32 data[7];

	GEM_BUG_ON(!guc->execbuf_client);

	data[0] = INTEL_GUC_ACTION_REQUEST_ENGINE_RESET;
	data[1] = engine->guc_id;
	data[2] = 0;
	data[3] = 0;
	data[4] = 0;
	data[5] = guc->execbuf_client->stage_id;
	data[6] = intel_guc_ggtt_offset(guc, guc->shared_data);

	return intel_guc_send(guc, data, ARRAY_SIZE(data));
}

/**
 * intel_guc_resume() - notify GuC resuming from suspend state
 * @guc:	the guc
 */
int intel_guc_resume(struct intel_guc *guc)
{
	u32 action[] = {
		INTEL_GUC_ACTION_EXIT_S_STATE,
		GUC_POWER_D0,
	};

	return intel_guc_send(guc, action, ARRAY_SIZE(action));
}

/**
 * DOC: GuC Address Space
 *
 * The layout of GuC address space is shown below:
 *
 * ::
 *
 *     +===========> +====================+ <== FFFF_FFFF
 *     ^             |      Reserved      |
 *     |             +====================+ <== GUC_GGTT_TOP
 *     |             |                    |
 *     |             |        DRAM        |
 *    GuC            |                    |
 *  Address    +===> +====================+ <== GuC ggtt_pin_bias
 *   Space     ^     |                    |
 *     |       |     |                    |
 *     |      GuC    |        GuC         |
 *     |     WOPCM   |       WOPCM        |
 *     |      Size   |                    |
 *     |       |     |                    |
 *     v       v     |                    |
 *     +=======+===> +====================+ <== 0000_0000
 *
 * The lower part of GuC Address Space [0, ggtt_pin_bias) is mapped to GuC WOPCM
 * while upper part of GuC Address Space [ggtt_pin_bias, GUC_GGTT_TOP) is mapped
 * to DRAM. The value of the GuC ggtt_pin_bias is the GuC WOPCM size.
 */

/**
 * intel_guc_allocate_vma() - Allocate a GGTT VMA for GuC usage
 * @guc:	the guc
 * @size:	size of area to allocate (both virtual space and memory)
 *
 * This is a wrapper to create an object for use with the GuC. In order to
 * use it inside the GuC, an object needs to be pinned lifetime, so we allocate
 * both some backing storage and a range inside the Global GTT. We must pin
 * it in the GGTT somewhere other than than [0, GUC ggtt_pin_bias) because that
 * range is reserved inside GuC.
 *
 * Return:	A i915_vma if successful, otherwise an ERR_PTR.
 */
struct i915_vma *intel_guc_allocate_vma(struct intel_guc *guc, u32 size)
{
	struct drm_i915_private *dev_priv = guc_to_i915(guc);
	struct drm_i915_gem_object *obj;
	struct i915_vma *vma;
	u64 flags;
	int ret;

	obj = i915_gem_object_create_shmem(dev_priv, size);
	if (IS_ERR(obj))
		return ERR_CAST(obj);

	vma = i915_vma_instance(obj, &dev_priv->ggtt.vm, NULL);
	if (IS_ERR(vma))
		goto err;

	flags = PIN_GLOBAL | PIN_OFFSET_BIAS | i915_ggtt_pin_bias(vma);
	ret = i915_vma_pin(vma, 0, 0, flags);
	if (ret) {
		vma = ERR_PTR(ret);
		goto err;
	}

	return vma;

err:
	i915_gem_object_put(obj);
	return vma;
}

int intel_guc_reserve_ggtt_top(struct intel_guc *guc)
{
	struct drm_i915_private *i915 = guc_to_i915(guc);
	struct i915_ggtt *ggtt = &i915->ggtt;
	u64 size;
	int ret;

	size = ggtt->vm.total - GUC_GGTT_TOP;

	ret = i915_gem_gtt_reserve(&ggtt->vm, &ggtt->uc_fw, size,
				   GUC_GGTT_TOP, I915_COLOR_UNEVICTABLE,
				   PIN_NOEVICT);
	if (ret)
		DRM_DEBUG_DRIVER("GuC: failed to reserve top of ggtt\n");

	return ret;
}

void intel_guc_release_ggtt_top(struct intel_guc *guc)
{
	struct drm_i915_private *i915 = guc_to_i915(guc);
	struct i915_ggtt *ggtt = &i915->ggtt;

	if (drm_mm_node_allocated(&ggtt->uc_fw))
		drm_mm_remove_node(&ggtt->uc_fw);
}

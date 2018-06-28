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

	guc->send_regs.base = i915_mmio_reg_offset(SOFT_SCRATCH(0));
	guc->send_regs.count = SOFT_SCRATCH_COUNT - 1;

	for (i = 0; i < guc->send_regs.count; i++) {
		fw_domains |= intel_uncore_forcewake_for_reg(dev_priv,
					guc_send_reg(guc, i),
					FW_REG_READ | FW_REG_WRITE);
	}
	guc->send_regs.fw_domains = fw_domains;
}

void intel_guc_init_early(struct intel_guc *guc)
{
	intel_guc_fw_init_early(guc);
	intel_guc_ct_init_early(&guc->ct);
	intel_guc_log_init_early(&guc->log);

	mutex_init(&guc->send_mutex);
	spin_lock_init(&guc->irq_lock);
	guc->send = intel_guc_send_nop;
	guc->handler = intel_guc_to_host_event_handler_nop;
	guc->notify = gen8_guc_raise_irq;
}

int intel_guc_init_wq(struct intel_guc *guc)
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

void intel_guc_fini_wq(struct intel_guc *guc)
{
	struct drm_i915_private *dev_priv = guc_to_i915(guc);

	if (HAS_LOGICAL_RING_PREEMPTION(dev_priv) &&
	    USES_GUC_SUBMISSION(dev_priv))
		destroy_workqueue(guc->preempt_wq);

	destroy_workqueue(guc->log.relay.flush_wq);
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
		i915_vma_unpin_and_release(&vma);
		return PTR_ERR(vaddr);
	}

	guc->shared_data = vma;
	guc->shared_data_vaddr = vaddr;

	return 0;
}

static void guc_shared_data_destroy(struct intel_guc *guc)
{
	i915_gem_object_unpin_map(guc->shared_data->obj);
	i915_vma_unpin_and_release(&guc->shared_data);
}

int intel_guc_init(struct intel_guc *guc)
{
	struct drm_i915_private *dev_priv = guc_to_i915(guc);
	int ret;

	ret = guc_shared_data_create(guc);
	if (ret)
		return ret;
	GEM_BUG_ON(!guc->shared_data);

	ret = intel_guc_log_create(&guc->log);
	if (ret)
		goto err_shared;

	ret = intel_guc_ads_create(guc);
	if (ret)
		goto err_log;
	GEM_BUG_ON(!guc->ads_vma);

	/* We need to notify the guc whenever we change the GGTT */
	i915_ggtt_enable_guc(dev_priv);

	return 0;

err_log:
	intel_guc_log_destroy(&guc->log);
err_shared:
	guc_shared_data_destroy(guc);
	return ret;
}

void intel_guc_fini(struct intel_guc *guc)
{
	struct drm_i915_private *dev_priv = guc_to_i915(guc);

	i915_ggtt_disable_guc(dev_priv);
	intel_guc_ads_destroy(guc);
	intel_guc_log_destroy(&guc->log);
	guc_shared_data_destroy(guc);
}

static u32 get_log_control_flags(void)
{
	u32 level = i915_modparams.guc_log_level;
	u32 flags = 0;

	GEM_BUG_ON(level < 0);

	if (!GUC_LOG_LEVEL_IS_ENABLED(level))
		flags |= GUC_LOG_DEFAULT_DISABLED;

	if (!GUC_LOG_LEVEL_IS_VERBOSE(level))
		flags |= GUC_LOG_DISABLED;
	else
		flags |= GUC_LOG_LEVEL_TO_VERBOSITY(level) <<
			 GUC_LOG_VERBOSITY_SHIFT;

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

	/*
	 * GuC ARAT increment is 10 ns. GuC default scheduler quantum is one
	 * second. This ARAR is calculated by:
	 * Scheduler-Quantum-in-ns / ARAT-increment-in-ns = 1000000000 / 10
	 */
	params[GUC_CTL_ARAT_HIGH] = 0;
	params[GUC_CTL_ARAT_LOW] = 100000000;

	params[GUC_CTL_WA] |= GUC_CTL_WA_UK_BY_DRIVER;

	params[GUC_CTL_FEATURE] |= GUC_CTL_DISABLE_SCHEDULER |
			GUC_CTL_VCS2_ENABLED;

	params[GUC_CTL_LOG_PARAMS] = guc->log.flags;

	params[GUC_CTL_DEBUG] = get_log_control_flags();

	/* If GuC submission is enabled, set up additional parameters here */
	if (USES_GUC_SUBMISSION(dev_priv)) {
		u32 ads = intel_guc_ggtt_offset(guc,
						guc->ads_vma) >> PAGE_SHIFT;
		u32 pgs = intel_guc_ggtt_offset(guc, guc->stage_desc_pool);
		u32 ctx_in_16 = GUC_MAX_STAGE_DESCRIPTORS / 16;

		params[GUC_CTL_DEBUG] |= ads << GUC_ADS_ADDR_SHIFT;
		params[GUC_CTL_DEBUG] |= GUC_ADS_ENABLED;

		pgs >>= PAGE_SHIFT;
		params[GUC_CTL_CTXINFO] = (pgs << GUC_CTL_BASE_ADDR_SHIFT) |
			(ctx_in_16 << GUC_CTL_CTXNUM_IN16_SHIFT);

		params[GUC_CTL_FEATURE] |= GUC_CTL_KERNEL_SUBMISSIONS;

		/* Unmask this bit to enable the GuC's internal scheduler */
		params[GUC_CTL_FEATURE] &= ~GUC_CTL_DISABLE_SCHEDULER;
	}

	/*
	 * All SOFT_SCRATCH registers are in FORCEWAKE_BLITTER domain and
	 * they are power context saved so it's ok to release forcewake
	 * when we are done here and take it again at xfer time.
	 */
	intel_uncore_forcewake_get(dev_priv, FORCEWAKE_BLITTER);

	I915_WRITE(SOFT_SCRATCH(0), 0);

	for (i = 0; i < GUC_CTL_MAX_DWORDS; i++)
		I915_WRITE(SOFT_SCRATCH(1 + i), params[i]);

	intel_uncore_forcewake_put(dev_priv, FORCEWAKE_BLITTER);
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
	u32 status;
	int i;
	int ret;

	GEM_BUG_ON(!len);
	GEM_BUG_ON(len > guc->send_regs.count);

	/* We expect only action code */
	GEM_BUG_ON(*action & ~INTEL_GUC_MSG_CODE_MASK);

	/* If CT is available, we expect to use MMIO only during init/fini */
	GEM_BUG_ON(HAS_GUC_CT(dev_priv) &&
		*action != INTEL_GUC_ACTION_REGISTER_COMMAND_TRANSPORT_BUFFER &&
		*action != INTEL_GUC_ACTION_DEREGISTER_COMMAND_TRANSPORT_BUFFER);

	mutex_lock(&guc->send_mutex);
	intel_uncore_forcewake_get(dev_priv, guc->send_regs.fw_domains);

	for (i = 0; i < len; i++)
		I915_WRITE(guc_send_reg(guc, i), action[i]);

	POSTING_READ(guc_send_reg(guc, i - 1));

	intel_guc_notify(guc);

	/*
	 * No GuC command should ever take longer than 10ms.
	 * Fast commands should still complete in 10us.
	 */
	ret = __intel_wait_for_register_fw(dev_priv,
					   guc_send_reg(guc, 0),
					   INTEL_GUC_MSG_TYPE_MASK,
					   INTEL_GUC_MSG_TYPE_RESPONSE <<
					   INTEL_GUC_MSG_TYPE_SHIFT,
					   10, 10, &status);
	/* If GuC explicitly returned an error, convert it to -EIO */
	if (!ret && !INTEL_GUC_MSG_IS_RESPONSE_SUCCESS(status))
		ret = -EIO;

	if (ret) {
		DRM_DEBUG_DRIVER("INTEL_GUC_SEND: Action 0x%X failed;"
				 " ret=%d status=0x%08X response=0x%08X\n",
				 action[0], ret, status,
				 I915_READ(SOFT_SCRATCH(15)));
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
	intel_uncore_forcewake_put(dev_priv, guc->send_regs.fw_domains);
	mutex_unlock(&guc->send_mutex);

	return ret;
}

void intel_guc_to_host_event_handler_mmio(struct intel_guc *guc)
{
	struct drm_i915_private *dev_priv = guc_to_i915(guc);
	u32 msg, val;

	/*
	 * Sample the log buffer flush related bits & clear them out now
	 * itself from the message identity register to minimize the
	 * probability of losing a flush interrupt, when there are back
	 * to back flush interrupts.
	 * There can be a new flush interrupt, for different log buffer
	 * type (like for ISR), whilst Host is handling one (for DPC).
	 * Since same bit is used in message register for ISR & DPC, it
	 * could happen that GuC sets the bit for 2nd interrupt but Host
	 * clears out the bit on handling the 1st interrupt.
	 */
	spin_lock(&guc->irq_lock);
	val = I915_READ(SOFT_SCRATCH(15));
	msg = val & guc->msg_enabled_mask;
	I915_WRITE(SOFT_SCRATCH(15), val & ~msg);
	spin_unlock(&guc->irq_lock);

	intel_guc_to_host_process_recv_msg(guc, msg);
}

void intel_guc_to_host_process_recv_msg(struct intel_guc *guc, u32 msg)
{
	/* Make sure to handle only enabled messages */
	msg &= guc->msg_enabled_mask;

	if (msg & (INTEL_GUC_RECV_MSG_FLUSH_LOG_BUFFER |
		   INTEL_GUC_RECV_MSG_CRASH_DUMP_POSTED))
		intel_guc_log_handle_flush_event(&guc->log);
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
	u32 data[] = {
		INTEL_GUC_ACTION_ENTER_S_STATE,
		GUC_POWER_D1, /* any value greater than GUC_POWER_D0 */
		intel_guc_ggtt_offset(guc, guc->shared_data)
	};

	return intel_guc_send(guc, data, ARRAY_SIZE(data));
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
	u32 data[] = {
		INTEL_GUC_ACTION_EXIT_S_STATE,
		GUC_POWER_D0,
		intel_guc_ggtt_offset(guc, guc->shared_data)
	};

	return intel_guc_send(guc, data, ARRAY_SIZE(data));
}

/**
 * DOC: GuC Address Space
 *
 * The layout of GuC address space is shown below:
 *
 * ::
 *
 *     +==============> +====================+ <== GUC_GGTT_TOP
 *     ^                |                    |
 *     |                |                    |
 *     |                |        DRAM        |
 *     |                |       Memory       |
 *     |                |                    |
 *    GuC               |                    |
 *  Address  +========> +====================+ <== WOPCM Top
 *   Space   ^          |   HW contexts RSVD |
 *     |     |          |        WOPCM       |
 *     |     |     +==> +--------------------+ <== GuC WOPCM Top
 *     |    GuC    ^    |                    |
 *     |    GGTT   |    |                    |
 *     |    Pin   GuC   |        GuC         |
 *     |    Bias WOPCM  |       WOPCM        |
 *     |     |    Size  |                    |
 *     |     |     |    |                    |
 *     v     v     v    |                    |
 *     +=====+=====+==> +====================+ <== GuC WOPCM Base
 *                      |   Non-GuC WOPCM    |
 *                      |   (HuC/Reserved)   |
 *                      +====================+ <== WOPCM Base
 *
 * The lower part of GuC Address Space [0, ggtt_pin_bias) is mapped to WOPCM
 * while upper part of GuC Address Space [ggtt_pin_bias, GUC_GGTT_TOP) is mapped
 * to DRAM. The value of the GuC ggtt_pin_bias is determined by WOPCM size and
 * actual GuC WOPCM size.
 */

/**
 * intel_guc_init_ggtt_pin_bias() - Initialize the GuC ggtt_pin_bias value.
 * @guc: intel_guc structure.
 *
 * This function will calculate and initialize the ggtt_pin_bias value based on
 * overall WOPCM size and GuC WOPCM size.
 */
void intel_guc_init_ggtt_pin_bias(struct intel_guc *guc)
{
	struct drm_i915_private *i915 = guc_to_i915(guc);

	GEM_BUG_ON(!i915->wopcm.size);
	GEM_BUG_ON(i915->wopcm.size < i915->wopcm.guc.base);

	guc->ggtt_pin_bias = i915->wopcm.size - i915->wopcm.guc.base;
}

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
	int ret;

	obj = i915_gem_object_create(dev_priv, size);
	if (IS_ERR(obj))
		return ERR_CAST(obj);

	vma = i915_vma_instance(obj, &dev_priv->ggtt.base, NULL);
	if (IS_ERR(vma))
		goto err;

	ret = i915_vma_pin(vma, 0, PAGE_SIZE,
			   PIN_GLOBAL | PIN_OFFSET_BIAS | guc->ggtt_pin_bias);
	if (ret) {
		vma = ERR_PTR(ret);
		goto err;
	}

	return vma;

err:
	i915_gem_object_put(obj);
	return vma;
}

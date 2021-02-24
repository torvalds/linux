// SPDX-License-Identifier: MIT
/*
 * Copyright © 2014-2019 Intel Corporation
 */

#include "gt/intel_gt.h"
#include "gt/intel_gt_irq.h"
#include "gt/intel_gt_pm_irq.h"
#include "intel_guc.h"
#include "intel_guc_ads.h"
#include "intel_guc_submission.h"
#include "i915_drv.h"

/**
 * DOC: GuC
 *
 * The GuC is a microcontroller inside the GT HW, introduced in gen9. The GuC is
 * designed to offload some of the functionality usually performed by the host
 * driver; currently the main operations it can take care of are:
 *
 * - Authentication of the HuC, which is required to fully enable HuC usage.
 * - Low latency graphics context scheduling (a.k.a. GuC submission).
 * - GT Power management.
 *
 * The enable_guc module parameter can be used to select which of those
 * operations to enable within GuC. Note that not all the operations are
 * supported on all gen9+ platforms.
 *
 * Enabling the GuC is not mandatory and therefore the firmware is only loaded
 * if at least one of the operations is selected. However, not loading the GuC
 * might result in the loss of some features that do require the GuC (currently
 * just the HuC, but more are expected to land in the future).
 */

void intel_guc_notify(struct intel_guc *guc)
{
	struct intel_gt *gt = guc_to_gt(guc);

	/*
	 * On Gen11+, the value written to the register is passes as a payload
	 * to the FW. However, the FW currently treats all values the same way
	 * (H2G interrupt), so we can just write the value that the HW expects
	 * on older gens.
	 */
	intel_uncore_write(gt->uncore, guc->notify_reg, GUC_SEND_TRIGGER);
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
	struct intel_gt *gt = guc_to_gt(guc);
	enum forcewake_domains fw_domains = 0;
	unsigned int i;

	if (INTEL_GEN(gt->i915) >= 11) {
		guc->send_regs.base =
				i915_mmio_reg_offset(GEN11_SOFT_SCRATCH(0));
		guc->send_regs.count = GEN11_SOFT_SCRATCH_COUNT;
	} else {
		guc->send_regs.base = i915_mmio_reg_offset(SOFT_SCRATCH(0));
		guc->send_regs.count = GUC_MAX_MMIO_MSG_LEN;
		BUILD_BUG_ON(GUC_MAX_MMIO_MSG_LEN > SOFT_SCRATCH_COUNT);
	}

	for (i = 0; i < guc->send_regs.count; i++) {
		fw_domains |= intel_uncore_forcewake_for_reg(gt->uncore,
					guc_send_reg(guc, i),
					FW_REG_READ | FW_REG_WRITE);
	}
	guc->send_regs.fw_domains = fw_domains;
}

static void gen9_reset_guc_interrupts(struct intel_guc *guc)
{
	struct intel_gt *gt = guc_to_gt(guc);

	assert_rpm_wakelock_held(&gt->i915->runtime_pm);

	spin_lock_irq(&gt->irq_lock);
	gen6_gt_pm_reset_iir(gt, gt->pm_guc_events);
	spin_unlock_irq(&gt->irq_lock);
}

static void gen9_enable_guc_interrupts(struct intel_guc *guc)
{
	struct intel_gt *gt = guc_to_gt(guc);

	assert_rpm_wakelock_held(&gt->i915->runtime_pm);

	spin_lock_irq(&gt->irq_lock);
	if (!guc->interrupts.enabled) {
		WARN_ON_ONCE(intel_uncore_read(gt->uncore, GEN8_GT_IIR(2)) &
			     gt->pm_guc_events);
		guc->interrupts.enabled = true;
		gen6_gt_pm_enable_irq(gt, gt->pm_guc_events);
	}
	spin_unlock_irq(&gt->irq_lock);
}

static void gen9_disable_guc_interrupts(struct intel_guc *guc)
{
	struct intel_gt *gt = guc_to_gt(guc);

	assert_rpm_wakelock_held(&gt->i915->runtime_pm);

	spin_lock_irq(&gt->irq_lock);
	guc->interrupts.enabled = false;

	gen6_gt_pm_disable_irq(gt, gt->pm_guc_events);

	spin_unlock_irq(&gt->irq_lock);
	intel_synchronize_irq(gt->i915);

	gen9_reset_guc_interrupts(guc);
}

static void gen11_reset_guc_interrupts(struct intel_guc *guc)
{
	struct intel_gt *gt = guc_to_gt(guc);

	spin_lock_irq(&gt->irq_lock);
	gen11_gt_reset_one_iir(gt, 0, GEN11_GUC);
	spin_unlock_irq(&gt->irq_lock);
}

static void gen11_enable_guc_interrupts(struct intel_guc *guc)
{
	struct intel_gt *gt = guc_to_gt(guc);

	spin_lock_irq(&gt->irq_lock);
	if (!guc->interrupts.enabled) {
		u32 events = REG_FIELD_PREP(ENGINE1_MASK, GUC_INTR_GUC2HOST);

		WARN_ON_ONCE(gen11_gt_reset_one_iir(gt, 0, GEN11_GUC));
		intel_uncore_write(gt->uncore,
				   GEN11_GUC_SG_INTR_ENABLE, events);
		intel_uncore_write(gt->uncore,
				   GEN11_GUC_SG_INTR_MASK, ~events);
		guc->interrupts.enabled = true;
	}
	spin_unlock_irq(&gt->irq_lock);
}

static void gen11_disable_guc_interrupts(struct intel_guc *guc)
{
	struct intel_gt *gt = guc_to_gt(guc);

	spin_lock_irq(&gt->irq_lock);
	guc->interrupts.enabled = false;

	intel_uncore_write(gt->uncore, GEN11_GUC_SG_INTR_MASK, ~0);
	intel_uncore_write(gt->uncore, GEN11_GUC_SG_INTR_ENABLE, 0);

	spin_unlock_irq(&gt->irq_lock);
	intel_synchronize_irq(gt->i915);

	gen11_reset_guc_interrupts(guc);
}

void intel_guc_init_early(struct intel_guc *guc)
{
	struct drm_i915_private *i915 = guc_to_gt(guc)->i915;

	intel_uc_fw_init_early(&guc->fw, INTEL_UC_FW_TYPE_GUC);
	intel_guc_ct_init_early(&guc->ct);
	intel_guc_log_init_early(&guc->log);
	intel_guc_submission_init_early(guc);

	mutex_init(&guc->send_mutex);
	spin_lock_init(&guc->irq_lock);
	if (INTEL_GEN(i915) >= 11) {
		guc->notify_reg = GEN11_GUC_HOST_INTERRUPT;
		guc->interrupts.reset = gen11_reset_guc_interrupts;
		guc->interrupts.enable = gen11_enable_guc_interrupts;
		guc->interrupts.disable = gen11_disable_guc_interrupts;
	} else {
		guc->notify_reg = GUC_SEND_INTERRUPT;
		guc->interrupts.reset = gen9_reset_guc_interrupts;
		guc->interrupts.enable = gen9_enable_guc_interrupts;
		guc->interrupts.disable = gen9_disable_guc_interrupts;
	}
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

	if (!intel_guc_submission_is_used(guc))
		flags |= GUC_CTL_DISABLE_SCHEDULER;

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
static void guc_init_params(struct intel_guc *guc)
{
	u32 *params = guc->params;
	int i;

	BUILD_BUG_ON(sizeof(guc->params) != GUC_CTL_MAX_DWORDS * sizeof(u32));

	params[GUC_CTL_LOG_PARAMS] = guc_ctl_log_params_flags(guc);
	params[GUC_CTL_FEATURE] = guc_ctl_feature_flags(guc);
	params[GUC_CTL_DEBUG] = guc_ctl_debug_flags(guc);
	params[GUC_CTL_ADS] = guc_ctl_ads_flags(guc);

	for (i = 0; i < GUC_CTL_MAX_DWORDS; i++)
		DRM_DEBUG_DRIVER("param[%2d] = %#x\n", i, params[i]);
}

/*
 * Initialise the GuC parameter block before starting the firmware
 * transfer. These parameters are read by the firmware on startup
 * and cannot be changed thereafter.
 */
void intel_guc_write_params(struct intel_guc *guc)
{
	struct intel_uncore *uncore = guc_to_gt(guc)->uncore;
	int i;

	/*
	 * All SOFT_SCRATCH registers are in FORCEWAKE_GT domain and
	 * they are power context saved so it's ok to release forcewake
	 * when we are done here and take it again at xfer time.
	 */
	intel_uncore_forcewake_get(uncore, FORCEWAKE_GT);

	intel_uncore_write(uncore, SOFT_SCRATCH(0), 0);

	for (i = 0; i < GUC_CTL_MAX_DWORDS; i++)
		intel_uncore_write(uncore, SOFT_SCRATCH(1 + i), guc->params[i]);

	intel_uncore_forcewake_put(uncore, FORCEWAKE_GT);
}

int intel_guc_init(struct intel_guc *guc)
{
	struct intel_gt *gt = guc_to_gt(guc);
	int ret;

	ret = intel_uc_fw_init(&guc->fw);
	if (ret)
		goto out;

	ret = intel_guc_log_create(&guc->log);
	if (ret)
		goto err_fw;

	ret = intel_guc_ads_create(guc);
	if (ret)
		goto err_log;
	GEM_BUG_ON(!guc->ads_vma);

	ret = intel_guc_ct_init(&guc->ct);
	if (ret)
		goto err_ads;

	if (intel_guc_submission_is_used(guc)) {
		/*
		 * This is stuff we need to have available at fw load time
		 * if we are planning to enable submission later
		 */
		ret = intel_guc_submission_init(guc);
		if (ret)
			goto err_ct;
	}

	/* now that everything is perma-pinned, initialize the parameters */
	guc_init_params(guc);

	/* We need to notify the guc whenever we change the GGTT */
	i915_ggtt_enable_guc(gt->ggtt);

	intel_uc_fw_change_status(&guc->fw, INTEL_UC_FIRMWARE_LOADABLE);

	return 0;

err_ct:
	intel_guc_ct_fini(&guc->ct);
err_ads:
	intel_guc_ads_destroy(guc);
err_log:
	intel_guc_log_destroy(&guc->log);
err_fw:
	intel_uc_fw_fini(&guc->fw);
out:
	i915_probe_error(gt->i915, "failed with %d\n", ret);
	return ret;
}

void intel_guc_fini(struct intel_guc *guc)
{
	struct intel_gt *gt = guc_to_gt(guc);

	if (!intel_uc_fw_is_loadable(&guc->fw))
		return;

	i915_ggtt_disable_guc(gt->ggtt);

	if (intel_guc_submission_is_used(guc))
		intel_guc_submission_fini(guc);

	intel_guc_ct_fini(&guc->ct);

	intel_guc_ads_destroy(guc);
	intel_guc_log_destroy(&guc->log);
	intel_uc_fw_fini(&guc->fw);
}

/*
 * This function implements the MMIO based host to GuC interface.
 */
int intel_guc_send_mmio(struct intel_guc *guc, const u32 *action, u32 len,
			u32 *response_buf, u32 response_buf_size)
{
	struct intel_uncore *uncore = guc_to_gt(guc)->uncore;
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
			response_buf[i] = intel_uncore_read(uncore,
							    guc_send_reg(guc, i + 1));
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
	struct drm_i915_private *dev_priv = guc_to_gt(guc)->i915;
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
	struct intel_uncore *uncore = guc_to_gt(guc)->uncore;
	int ret;
	u32 status;
	u32 action[] = {
		INTEL_GUC_ACTION_ENTER_S_STATE,
		GUC_POWER_D1, /* any value greater than GUC_POWER_D0 */
	};

	/*
	 * If GuC communication is enabled but submission is not supported,
	 * we do not need to suspend the GuC.
	 */
	if (!intel_guc_submission_is_used(guc) || !intel_guc_is_ready(guc))
		return 0;

	/*
	 * The ENTER_S_STATE action queues the save/restore operation in GuC FW
	 * and then returns, so waiting on the H2G is not enough to guarantee
	 * GuC is done. When all the processing is done, GuC writes
	 * INTEL_GUC_SLEEP_STATE_SUCCESS to scratch register 14, so we can poll
	 * on that. Note that GuC does not ensure that the value in the register
	 * is different from INTEL_GUC_SLEEP_STATE_SUCCESS while the action is
	 * in progress so we need to take care of that ourselves as well.
	 */

	intel_uncore_write(uncore, SOFT_SCRATCH(14),
			   INTEL_GUC_SLEEP_STATE_INVALID_MASK);

	ret = intel_guc_send(guc, action, ARRAY_SIZE(action));
	if (ret)
		return ret;

	ret = __intel_wait_for_register(uncore, SOFT_SCRATCH(14),
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
	/* XXX: to be implemented with submission interface rework */

	return -ENODEV;
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

	/*
	 * If GuC communication is enabled but submission is not supported,
	 * we do not need to resume the GuC but we do need to enable the
	 * GuC communication on resume (above).
	 */
	if (!intel_guc_submission_is_used(guc) || !intel_guc_is_ready(guc))
		return 0;

	return intel_guc_send(guc, action, ARRAY_SIZE(action));
}

/**
 * DOC: GuC Memory Management
 *
 * GuC can't allocate any memory for its own usage, so all the allocations must
 * be handled by the host driver. GuC accesses the memory via the GGTT, with the
 * exception of the top and bottom parts of the 4GB address space, which are
 * instead re-mapped by the GuC HW to memory location of the FW itself (WOPCM)
 * or other parts of the HW. The driver must take care not to place objects that
 * the GuC is going to access in these reserved ranges. The layout of the GuC
 * address space is shown below:
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
	struct intel_gt *gt = guc_to_gt(guc);
	struct drm_i915_gem_object *obj;
	struct i915_vma *vma;
	u64 flags;
	int ret;

	obj = i915_gem_object_create_shmem(gt->i915, size);
	if (IS_ERR(obj))
		return ERR_CAST(obj);

	vma = i915_vma_instance(obj, &gt->ggtt->vm, NULL);
	if (IS_ERR(vma))
		goto err;

	flags = PIN_OFFSET_BIAS | i915_ggtt_pin_bias(vma);
	ret = i915_ggtt_pin(vma, NULL, 0, flags);
	if (ret) {
		vma = ERR_PTR(ret);
		goto err;
	}

	return i915_vma_make_unshrinkable(vma);

err:
	i915_gem_object_put(obj);
	return vma;
}

/**
 * intel_guc_allocate_and_map_vma() - Allocate and map VMA for GuC usage
 * @guc:	the guc
 * @size:	size of area to allocate (both virtual space and memory)
 * @out_vma:	return variable for the allocated vma pointer
 * @out_vaddr:	return variable for the obj mapping
 *
 * This wrapper calls intel_guc_allocate_vma() and then maps the allocated
 * object with I915_MAP_WB.
 *
 * Return:	0 if successful, a negative errno code otherwise.
 */
int intel_guc_allocate_and_map_vma(struct intel_guc *guc, u32 size,
				   struct i915_vma **out_vma, void **out_vaddr)
{
	struct i915_vma *vma;
	void *vaddr;

	vma = intel_guc_allocate_vma(guc, size);
	if (IS_ERR(vma))
		return PTR_ERR(vma);

	vaddr = i915_gem_object_pin_map(vma->obj, I915_MAP_WB);
	if (IS_ERR(vaddr)) {
		i915_vma_unpin_and_release(&vma, 0);
		return PTR_ERR(vaddr);
	}

	*out_vma = vma;
	*out_vaddr = vaddr;

	return 0;
}

/**
 * intel_guc_load_status - dump information about GuC load status
 * @guc: the GuC
 * @p: the &drm_printer
 *
 * Pretty printer for GuC load status.
 */
void intel_guc_load_status(struct intel_guc *guc, struct drm_printer *p)
{
	struct intel_gt *gt = guc_to_gt(guc);
	struct intel_uncore *uncore = gt->uncore;
	intel_wakeref_t wakeref;

	if (!intel_guc_is_supported(guc)) {
		drm_printf(p, "GuC not supported\n");
		return;
	}

	if (!intel_guc_is_wanted(guc)) {
		drm_printf(p, "GuC disabled\n");
		return;
	}

	intel_uc_fw_dump(&guc->fw, p);

	with_intel_runtime_pm(uncore->rpm, wakeref) {
		u32 status = intel_uncore_read(uncore, GUC_STATUS);
		u32 i;

		drm_printf(p, "\nGuC status 0x%08x:\n", status);
		drm_printf(p, "\tBootrom status = 0x%x\n",
			   (status & GS_BOOTROM_MASK) >> GS_BOOTROM_SHIFT);
		drm_printf(p, "\tuKernel status = 0x%x\n",
			   (status & GS_UKERNEL_MASK) >> GS_UKERNEL_SHIFT);
		drm_printf(p, "\tMIA Core status = 0x%x\n",
			   (status & GS_MIA_MASK) >> GS_MIA_SHIFT);
		drm_puts(p, "\nScratch registers:\n");
		for (i = 0; i < 16; i++) {
			drm_printf(p, "\t%2d: \t0x%x\n",
				   i, intel_uncore_read(uncore, SOFT_SCRATCH(i)));
		}
	}
}

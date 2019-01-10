/*
 *
 * (C) COPYRIGHT 2011-2015 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
 *
 * A copy of the licence is included with the program, and can also be obtained
 * from Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 *
 */





/*
 * Base kernel instrumentation APIs.
 */

#include <mali_kbase.h>
#include <mali_midg_regmap.h>

void kbase_instr_hwcnt_suspend(struct kbase_device *kbdev)
{
	struct kbase_context *kctx;

	KBASE_DEBUG_ASSERT(kbdev);
	KBASE_DEBUG_ASSERT(!kbdev->hwcnt.suspended_kctx);

	kctx = kbdev->hwcnt.kctx;
	kbdev->hwcnt.suspended_kctx = kctx;

	/* Relevant state was saved into hwcnt.suspended_state when enabling the
	 * counters */

	if (kctx) {
		KBASE_DEBUG_ASSERT(kctx->jctx.sched_info.ctx.flags &
						KBASE_CTX_FLAG_PRIVILEGED);
		kbase_instr_hwcnt_disable(kctx);
	}
}

void kbase_instr_hwcnt_resume(struct kbase_device *kbdev)
{
	struct kbase_context *kctx;

	KBASE_DEBUG_ASSERT(kbdev);

	kctx = kbdev->hwcnt.suspended_kctx;
	kbdev->hwcnt.suspended_kctx = NULL;

	if (kctx) {
		int err;

		err = kbase_instr_hwcnt_enable_internal(kbdev, kctx,
						&kbdev->hwcnt.suspended_state);
		WARN(err, "Failed to restore instrumented hardware counters on resume\n");
	}
}

int kbase_instr_hwcnt_enable(struct kbase_context *kctx,
		struct kbase_uk_hwcnt_setup *setup)
{
	struct kbase_device *kbdev;
	int err;

	kbdev = kctx->kbdev;

	/* Mark the context as active so the GPU is kept turned on */
	/* A suspend won't happen here, because we're in a syscall from a
	 * userspace thread. */
	kbase_pm_context_active(kbdev);

	/* Schedule the context in */
	kbasep_js_schedule_privileged_ctx(kbdev, kctx);
	err = kbase_instr_hwcnt_enable_internal(kbdev, kctx, setup);
	if (err) {
		/* Release the context. This had its own Power Manager Active
		 * reference */
		kbasep_js_release_privileged_ctx(kbdev, kctx);

		/* Also release our Power Manager Active reference */
		kbase_pm_context_idle(kbdev);
	}

	return err;
}
KBASE_EXPORT_SYMBOL(kbase_instr_hwcnt_enable);

int kbase_instr_hwcnt_disable(struct kbase_context *kctx)
{
	int err = -EINVAL;
	struct kbase_device *kbdev = kctx->kbdev;

	err = kbase_instr_hwcnt_disable_internal(kctx);
	if (err)
		goto out;

	/* Release the context. This had its own Power Manager Active reference
	 */
	kbasep_js_release_privileged_ctx(kbdev, kctx);

	/* Also release our Power Manager Active reference */
	kbase_pm_context_idle(kbdev);

	dev_dbg(kbdev->dev, "HW counters dumping disabled for context %p",
									kctx);
out:
	return err;
}
KBASE_EXPORT_SYMBOL(kbase_instr_hwcnt_disable);

int kbase_instr_hwcnt_dump(struct kbase_context *kctx)
{
	int err;

	err = kbase_instr_hwcnt_request_dump(kctx);
	if (err)
		return err;

	err = kbase_instr_hwcnt_wait_for_dump(kctx);
	return err;
}
KBASE_EXPORT_SYMBOL(kbase_instr_hwcnt_dump);


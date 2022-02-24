// SPDX-License-Identifier: MIT
/*
 * Copyright © 2013-2021 Intel Corporation
 */

#include "i915_drv.h"
#include "intel_pcode.h"

static int gen6_check_mailbox_status(u32 mbox)
{
	switch (mbox & GEN6_PCODE_ERROR_MASK) {
	case GEN6_PCODE_SUCCESS:
		return 0;
	case GEN6_PCODE_UNIMPLEMENTED_CMD:
		return -ENODEV;
	case GEN6_PCODE_ILLEGAL_CMD:
		return -ENXIO;
	case GEN6_PCODE_MIN_FREQ_TABLE_GT_RATIO_OUT_OF_RANGE:
	case GEN7_PCODE_MIN_FREQ_TABLE_GT_RATIO_OUT_OF_RANGE:
		return -EOVERFLOW;
	case GEN6_PCODE_TIMEOUT:
		return -ETIMEDOUT;
	default:
		MISSING_CASE(mbox & GEN6_PCODE_ERROR_MASK);
		return 0;
	}
}

static int gen7_check_mailbox_status(u32 mbox)
{
	switch (mbox & GEN6_PCODE_ERROR_MASK) {
	case GEN6_PCODE_SUCCESS:
		return 0;
	case GEN6_PCODE_ILLEGAL_CMD:
		return -ENXIO;
	case GEN7_PCODE_TIMEOUT:
		return -ETIMEDOUT;
	case GEN7_PCODE_ILLEGAL_DATA:
		return -EINVAL;
	case GEN11_PCODE_ILLEGAL_SUBCOMMAND:
		return -ENXIO;
	case GEN11_PCODE_LOCKED:
		return -EBUSY;
	case GEN11_PCODE_REJECTED:
		return -EACCES;
	case GEN7_PCODE_MIN_FREQ_TABLE_GT_RATIO_OUT_OF_RANGE:
		return -EOVERFLOW;
	default:
		MISSING_CASE(mbox & GEN6_PCODE_ERROR_MASK);
		return 0;
	}
}

static int __sandybridge_pcode_rw(struct drm_i915_private *i915,
				  u32 mbox, u32 *val, u32 *val1,
				  int fast_timeout_us,
				  int slow_timeout_ms,
				  bool is_read)
{
	struct intel_uncore *uncore = &i915->uncore;

	lockdep_assert_held(&i915->sb_lock);

	/*
	 * GEN6_PCODE_* are outside of the forcewake domain, we can use
	 * intel_uncore_read/write_fw variants to reduce the amount of work
	 * required when reading/writing.
	 */

	if (intel_uncore_read_fw(uncore, GEN6_PCODE_MAILBOX) & GEN6_PCODE_READY)
		return -EAGAIN;

	intel_uncore_write_fw(uncore, GEN6_PCODE_DATA, *val);
	intel_uncore_write_fw(uncore, GEN6_PCODE_DATA1, val1 ? *val1 : 0);
	intel_uncore_write_fw(uncore,
			      GEN6_PCODE_MAILBOX, GEN6_PCODE_READY | mbox);

	if (__intel_wait_for_register_fw(uncore,
					 GEN6_PCODE_MAILBOX,
					 GEN6_PCODE_READY, 0,
					 fast_timeout_us,
					 slow_timeout_ms,
					 &mbox))
		return -ETIMEDOUT;

	if (is_read)
		*val = intel_uncore_read_fw(uncore, GEN6_PCODE_DATA);
	if (is_read && val1)
		*val1 = intel_uncore_read_fw(uncore, GEN6_PCODE_DATA1);

	if (GRAPHICS_VER(i915) > 6)
		return gen7_check_mailbox_status(mbox);
	else
		return gen6_check_mailbox_status(mbox);
}

int sandybridge_pcode_read(struct drm_i915_private *i915, u32 mbox,
			   u32 *val, u32 *val1)
{
	int err;

	mutex_lock(&i915->sb_lock);
	err = __sandybridge_pcode_rw(i915, mbox, val, val1,
				     500, 20,
				     true);
	mutex_unlock(&i915->sb_lock);

	if (err) {
		drm_dbg(&i915->drm,
			"warning: pcode (read from mbox %x) mailbox access failed for %ps: %d\n",
			mbox, __builtin_return_address(0), err);
	}

	return err;
}

int sandybridge_pcode_write_timeout(struct drm_i915_private *i915,
				    u32 mbox, u32 val,
				    int fast_timeout_us,
				    int slow_timeout_ms)
{
	int err;

	mutex_lock(&i915->sb_lock);
	err = __sandybridge_pcode_rw(i915, mbox, &val, NULL,
				     fast_timeout_us, slow_timeout_ms,
				     false);
	mutex_unlock(&i915->sb_lock);

	if (err) {
		drm_dbg(&i915->drm,
			"warning: pcode (write of 0x%08x to mbox %x) mailbox access failed for %ps: %d\n",
			val, mbox, __builtin_return_address(0), err);
	}

	return err;
}

static bool skl_pcode_try_request(struct drm_i915_private *i915, u32 mbox,
				  u32 request, u32 reply_mask, u32 reply,
				  u32 *status)
{
	*status = __sandybridge_pcode_rw(i915, mbox, &request, NULL,
					 500, 0,
					 true);

	return *status || ((request & reply_mask) == reply);
}

/**
 * skl_pcode_request - send PCODE request until acknowledgment
 * @i915: device private
 * @mbox: PCODE mailbox ID the request is targeted for
 * @request: request ID
 * @reply_mask: mask used to check for request acknowledgment
 * @reply: value used to check for request acknowledgment
 * @timeout_base_ms: timeout for polling with preemption enabled
 *
 * Keep resending the @request to @mbox until PCODE acknowledges it, PCODE
 * reports an error or an overall timeout of @timeout_base_ms+50 ms expires.
 * The request is acknowledged once the PCODE reply dword equals @reply after
 * applying @reply_mask. Polling is first attempted with preemption enabled
 * for @timeout_base_ms and if this times out for another 50 ms with
 * preemption disabled.
 *
 * Returns 0 on success, %-ETIMEDOUT in case of a timeout, <0 in case of some
 * other error as reported by PCODE.
 */
int skl_pcode_request(struct drm_i915_private *i915, u32 mbox, u32 request,
		      u32 reply_mask, u32 reply, int timeout_base_ms)
{
	u32 status;
	int ret;

	mutex_lock(&i915->sb_lock);

#define COND \
	skl_pcode_try_request(i915, mbox, request, reply_mask, reply, &status)

	/*
	 * Prime the PCODE by doing a request first. Normally it guarantees
	 * that a subsequent request, at most @timeout_base_ms later, succeeds.
	 * _wait_for() doesn't guarantee when its passed condition is evaluated
	 * first, so send the first request explicitly.
	 */
	if (COND) {
		ret = 0;
		goto out;
	}
	ret = _wait_for(COND, timeout_base_ms * 1000, 10, 10);
	if (!ret)
		goto out;

	/*
	 * The above can time out if the number of requests was low (2 in the
	 * worst case) _and_ PCODE was busy for some reason even after a
	 * (queued) request and @timeout_base_ms delay. As a workaround retry
	 * the poll with preemption disabled to maximize the number of
	 * requests. Increase the timeout from @timeout_base_ms to 50ms to
	 * account for interrupts that could reduce the number of these
	 * requests, and for any quirks of the PCODE firmware that delays
	 * the request completion.
	 */
	drm_dbg_kms(&i915->drm,
		    "PCODE timeout, retrying with preemption disabled\n");
	drm_WARN_ON_ONCE(&i915->drm, timeout_base_ms > 3);
	preempt_disable();
	ret = wait_for_atomic(COND, 50);
	preempt_enable();

out:
	mutex_unlock(&i915->sb_lock);
	return ret ? ret : status;
#undef COND
}

int intel_pcode_init(struct drm_i915_private *i915)
{
	int ret = 0;

	if (!IS_DGFX(i915))
		return ret;

	ret = skl_pcode_request(i915, DG1_PCODE_STATUS,
				DG1_UNCORE_GET_INIT_STATUS,
				DG1_UNCORE_INIT_STATUS_COMPLETE,
				DG1_UNCORE_INIT_STATUS_COMPLETE, 180000);

	drm_dbg(&i915->drm, "PCODE init status %d\n", ret);

	if (ret)
		drm_err(&i915->drm, "Pcode did not report uncore initialization completion!\n");

	return ret;
}

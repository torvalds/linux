/*
 * Copyright © 2013 Intel Corporation
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

#include <asm/iosf_mbi.h>

#include "i915_drv.h"
#include "intel_sideband.h"

/*
 * IOSF sideband, see VLV2_SidebandMsg_HAS.docx and
 * VLV_VLV2_PUNIT_HAS_0.8.docx
 */

/* Standard MMIO read, non-posted */
#define SB_MRD_NP	0x00
/* Standard MMIO write, non-posted */
#define SB_MWR_NP	0x01
/* Private register read, double-word addressing, non-posted */
#define SB_CRRDDA_NP	0x06
/* Private register write, double-word addressing, non-posted */
#define SB_CRWRDA_NP	0x07

static void ping(void *info)
{
}

static void __vlv_punit_get(struct drm_i915_private *i915)
{
	iosf_mbi_punit_acquire();

	/*
	 * Prevent the cpu from sleeping while we use this sideband, otherwise
	 * the punit may cause a machine hang. The issue appears to be isolated
	 * with changing the power state of the CPU package while changing
	 * the power state via the punit, and we have only observed it
	 * reliably on 4-core Baytail systems suggesting the issue is in the
	 * power delivery mechanism and likely to be be board/function
	 * specific. Hence we presume the workaround needs only be applied
	 * to the Valleyview P-unit and not all sideband communications.
	 */
	if (IS_VALLEYVIEW(i915)) {
		cpu_latency_qos_update_request(&i915->sb_qos, 0);
		on_each_cpu(ping, NULL, 1);
	}
}

static void __vlv_punit_put(struct drm_i915_private *i915)
{
	if (IS_VALLEYVIEW(i915))
		cpu_latency_qos_update_request(&i915->sb_qos,
					       PM_QOS_DEFAULT_VALUE);

	iosf_mbi_punit_release();
}

void vlv_iosf_sb_get(struct drm_i915_private *i915, unsigned long ports)
{
	if (ports & BIT(VLV_IOSF_SB_PUNIT))
		__vlv_punit_get(i915);

	mutex_lock(&i915->sb_lock);
}

void vlv_iosf_sb_put(struct drm_i915_private *i915, unsigned long ports)
{
	mutex_unlock(&i915->sb_lock);

	if (ports & BIT(VLV_IOSF_SB_PUNIT))
		__vlv_punit_put(i915);
}

static int vlv_sideband_rw(struct drm_i915_private *i915,
			   u32 devfn, u32 port, u32 opcode,
			   u32 addr, u32 *val)
{
	struct intel_uncore *uncore = &i915->uncore;
	const bool is_read = (opcode == SB_MRD_NP || opcode == SB_CRRDDA_NP);
	int err;

	lockdep_assert_held(&i915->sb_lock);
	if (port == IOSF_PORT_PUNIT)
		iosf_mbi_assert_punit_acquired();

	/* Flush the previous comms, just in case it failed last time. */
	if (intel_wait_for_register(uncore,
				    VLV_IOSF_DOORBELL_REQ, IOSF_SB_BUSY, 0,
				    5)) {
		drm_dbg(&i915->drm, "IOSF sideband idle wait (%s) timed out\n",
			is_read ? "read" : "write");
		return -EAGAIN;
	}

	preempt_disable();

	intel_uncore_write_fw(uncore, VLV_IOSF_ADDR, addr);
	intel_uncore_write_fw(uncore, VLV_IOSF_DATA, is_read ? 0 : *val);
	intel_uncore_write_fw(uncore, VLV_IOSF_DOORBELL_REQ,
			      (devfn << IOSF_DEVFN_SHIFT) |
			      (opcode << IOSF_OPCODE_SHIFT) |
			      (port << IOSF_PORT_SHIFT) |
			      (0xf << IOSF_BYTE_ENABLES_SHIFT) |
			      (0 << IOSF_BAR_SHIFT) |
			      IOSF_SB_BUSY);

	if (__intel_wait_for_register_fw(uncore,
					 VLV_IOSF_DOORBELL_REQ, IOSF_SB_BUSY, 0,
					 10000, 0, NULL) == 0) {
		if (is_read)
			*val = intel_uncore_read_fw(uncore, VLV_IOSF_DATA);
		err = 0;
	} else {
		drm_dbg(&i915->drm, "IOSF sideband finish wait (%s) timed out\n",
			is_read ? "read" : "write");
		err = -ETIMEDOUT;
	}

	preempt_enable();

	return err;
}

u32 vlv_punit_read(struct drm_i915_private *i915, u32 addr)
{
	u32 val = 0;

	vlv_sideband_rw(i915, PCI_DEVFN(0, 0), IOSF_PORT_PUNIT,
			SB_CRRDDA_NP, addr, &val);

	return val;
}

int vlv_punit_write(struct drm_i915_private *i915, u32 addr, u32 val)
{
	return vlv_sideband_rw(i915, PCI_DEVFN(0, 0), IOSF_PORT_PUNIT,
			       SB_CRWRDA_NP, addr, &val);
}

u32 vlv_bunit_read(struct drm_i915_private *i915, u32 reg)
{
	u32 val = 0;

	vlv_sideband_rw(i915, PCI_DEVFN(0, 0), IOSF_PORT_BUNIT,
			SB_CRRDDA_NP, reg, &val);

	return val;
}

void vlv_bunit_write(struct drm_i915_private *i915, u32 reg, u32 val)
{
	vlv_sideband_rw(i915, PCI_DEVFN(0, 0), IOSF_PORT_BUNIT,
			SB_CRWRDA_NP, reg, &val);
}

u32 vlv_nc_read(struct drm_i915_private *i915, u8 addr)
{
	u32 val = 0;

	vlv_sideband_rw(i915, PCI_DEVFN(0, 0), IOSF_PORT_NC,
			SB_CRRDDA_NP, addr, &val);

	return val;
}

u32 vlv_iosf_sb_read(struct drm_i915_private *i915, u8 port, u32 reg)
{
	u32 val = 0;

	vlv_sideband_rw(i915, PCI_DEVFN(0, 0), port,
			SB_CRRDDA_NP, reg, &val);

	return val;
}

void vlv_iosf_sb_write(struct drm_i915_private *i915,
		       u8 port, u32 reg, u32 val)
{
	vlv_sideband_rw(i915, PCI_DEVFN(0, 0), port,
			SB_CRWRDA_NP, reg, &val);
}

u32 vlv_cck_read(struct drm_i915_private *i915, u32 reg)
{
	u32 val = 0;

	vlv_sideband_rw(i915, PCI_DEVFN(0, 0), IOSF_PORT_CCK,
			SB_CRRDDA_NP, reg, &val);

	return val;
}

void vlv_cck_write(struct drm_i915_private *i915, u32 reg, u32 val)
{
	vlv_sideband_rw(i915, PCI_DEVFN(0, 0), IOSF_PORT_CCK,
			SB_CRWRDA_NP, reg, &val);
}

u32 vlv_ccu_read(struct drm_i915_private *i915, u32 reg)
{
	u32 val = 0;

	vlv_sideband_rw(i915, PCI_DEVFN(0, 0), IOSF_PORT_CCU,
			SB_CRRDDA_NP, reg, &val);

	return val;
}

void vlv_ccu_write(struct drm_i915_private *i915, u32 reg, u32 val)
{
	vlv_sideband_rw(i915, PCI_DEVFN(0, 0), IOSF_PORT_CCU,
			SB_CRWRDA_NP, reg, &val);
}

static u32 vlv_dpio_phy_iosf_port(struct drm_i915_private *i915, enum dpio_phy phy)
{
	/*
	 * IOSF_PORT_DPIO: VLV x2 PHY (DP/HDMI B and C), CHV x1 PHY (DP/HDMI D)
	 * IOSF_PORT_DPIO_2: CHV x2 PHY (DP/HDMI B and C)
	 */
	if (IS_CHERRYVIEW(i915))
		return phy == DPIO_PHY0 ? IOSF_PORT_DPIO_2 : IOSF_PORT_DPIO;
	else
		return IOSF_PORT_DPIO;
}

u32 vlv_dpio_read(struct drm_i915_private *i915, enum pipe pipe, int reg)
{
	u32 port = vlv_dpio_phy_iosf_port(i915, DPIO_PHY(pipe));
	u32 val = 0;

	vlv_sideband_rw(i915, DPIO_DEVFN, port, SB_MRD_NP, reg, &val);

	/*
	 * FIXME: There might be some registers where all 1's is a valid value,
	 * so ideally we should check the register offset instead...
	 */
	drm_WARN(&i915->drm, val == 0xffffffff,
		 "DPIO read pipe %c reg 0x%x == 0x%x\n",
		 pipe_name(pipe), reg, val);

	return val;
}

void vlv_dpio_write(struct drm_i915_private *i915,
		    enum pipe pipe, int reg, u32 val)
{
	u32 port = vlv_dpio_phy_iosf_port(i915, DPIO_PHY(pipe));

	vlv_sideband_rw(i915, DPIO_DEVFN, port, SB_MWR_NP, reg, &val);
}

u32 vlv_flisdsi_read(struct drm_i915_private *i915, u32 reg)
{
	u32 val = 0;

	vlv_sideband_rw(i915, DPIO_DEVFN, IOSF_PORT_FLISDSI, SB_CRRDDA_NP,
			reg, &val);
	return val;
}

void vlv_flisdsi_write(struct drm_i915_private *i915, u32 reg, u32 val)
{
	vlv_sideband_rw(i915, DPIO_DEVFN, IOSF_PORT_FLISDSI, SB_CRWRDA_NP,
			reg, &val);
}

/* SBI access */
static int intel_sbi_rw(struct drm_i915_private *i915, u16 reg,
			enum intel_sbi_destination destination,
			u32 *val, bool is_read)
{
	struct intel_uncore *uncore = &i915->uncore;
	u32 cmd;

	lockdep_assert_held(&i915->sb_lock);

	if (intel_wait_for_register_fw(uncore,
				       SBI_CTL_STAT, SBI_BUSY, 0,
				       100)) {
		drm_err(&i915->drm,
			"timeout waiting for SBI to become ready\n");
		return -EBUSY;
	}

	intel_uncore_write_fw(uncore, SBI_ADDR, (u32)reg << 16);
	intel_uncore_write_fw(uncore, SBI_DATA, is_read ? 0 : *val);

	if (destination == SBI_ICLK)
		cmd = SBI_CTL_DEST_ICLK | SBI_CTL_OP_CRRD;
	else
		cmd = SBI_CTL_DEST_MPHY | SBI_CTL_OP_IORD;
	if (!is_read)
		cmd |= BIT(8);
	intel_uncore_write_fw(uncore, SBI_CTL_STAT, cmd | SBI_BUSY);

	if (__intel_wait_for_register_fw(uncore,
					 SBI_CTL_STAT, SBI_BUSY, 0,
					 100, 100, &cmd)) {
		drm_err(&i915->drm,
			"timeout waiting for SBI to complete read\n");
		return -ETIMEDOUT;
	}

	if (cmd & SBI_RESPONSE_FAIL) {
		drm_err(&i915->drm, "error during SBI read of reg %x\n", reg);
		return -ENXIO;
	}

	if (is_read)
		*val = intel_uncore_read_fw(uncore, SBI_DATA);

	return 0;
}

u32 intel_sbi_read(struct drm_i915_private *i915, u16 reg,
		   enum intel_sbi_destination destination)
{
	u32 result = 0;

	intel_sbi_rw(i915, reg, destination, &result, true);

	return result;
}

void intel_sbi_write(struct drm_i915_private *i915, u16 reg, u32 value,
		     enum intel_sbi_destination destination)
{
	intel_sbi_rw(i915, reg, destination, &value, false);
}

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
	 * GEN6_PCODE_* are outside of the forcewake domain, we can
	 * use te fw I915_READ variants to reduce the amount of work
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

	if (INTEL_GEN(i915) > 6)
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

void intel_pcode_init(struct drm_i915_private *i915)
{
	int ret;

	if (!IS_DGFX(i915))
		return;

	ret = skl_pcode_request(i915, DG1_PCODE_STATUS,
				DG1_UNCORE_GET_INIT_STATUS,
				DG1_UNCORE_INIT_STATUS_COMPLETE,
				DG1_UNCORE_INIT_STATUS_COMPLETE, 50);
	if (ret)
		drm_err(&i915->drm, "Pcode did not report uncore initialization completion!\n");
}

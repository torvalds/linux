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

#include "intel_sideband.h"

#include "i915_drv.h"
#include "intel_drv.h"

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
		pm_qos_update_request(&i915->sb_qos, 0);
		on_each_cpu(ping, NULL, 1);
	}
}

static void __vlv_punit_put(struct drm_i915_private *i915)
{
	if (IS_VALLEYVIEW(i915))
		pm_qos_update_request(&i915->sb_qos, PM_QOS_DEFAULT_VALUE);

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
		DRM_DEBUG_DRIVER("IOSF sideband idle wait (%s) timed out\n",
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
		DRM_DEBUG_DRIVER("IOSF sideband finish wait (%s) timed out\n",
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

u32 vlv_dpio_read(struct drm_i915_private *i915, enum pipe pipe, int reg)
{
	int port = i915->dpio_phy_iosf_port[DPIO_PHY(pipe)];
	u32 val = 0;

	vlv_sideband_rw(i915, DPIO_DEVFN, port, SB_MRD_NP, reg, &val);

	/*
	 * FIXME: There might be some registers where all 1's is a valid value,
	 * so ideally we should check the register offset instead...
	 */
	WARN(val == 0xffffffff, "DPIO read pipe %c reg 0x%x == 0x%x\n",
	     pipe_name(pipe), reg, val);

	return val;
}

void vlv_dpio_write(struct drm_i915_private *i915,
		    enum pipe pipe, int reg, u32 val)
{
	int port = i915->dpio_phy_iosf_port[DPIO_PHY(pipe)];

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
		DRM_ERROR("timeout waiting for SBI to become ready\n");
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
		DRM_ERROR("timeout waiting for SBI to complete read\n");
		return -ETIMEDOUT;
	}

	if (cmd & SBI_RESPONSE_FAIL) {
		DRM_ERROR("error during SBI read of reg %x\n", reg);
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

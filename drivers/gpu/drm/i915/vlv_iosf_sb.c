// SPDX-License-Identifier: MIT
/*
 * Copyright © 2013-2021 Intel Corporation
 */

#include "i915_drv.h"
#include "i915_iosf_mbi.h"
#include "i915_reg.h"
#include "vlv_iosf_sb.h"

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
	 * power delivery mechanism and likely to be board/function
	 * specific. Hence we presume the workaround needs only be applied
	 * to the Valleyview P-unit and not all sideband communications.
	 */
	if (IS_VALLEYVIEW(i915)) {
		cpu_latency_qos_update_request(&i915->vlv_iosf_sb.qos, 0);
		on_each_cpu(ping, NULL, 1);
	}
}

static void __vlv_punit_put(struct drm_i915_private *i915)
{
	if (IS_VALLEYVIEW(i915))
		cpu_latency_qos_update_request(&i915->vlv_iosf_sb.qos,
					       PM_QOS_DEFAULT_VALUE);

	iosf_mbi_punit_release();
}

void vlv_iosf_sb_get(struct drm_device *drm, unsigned long unit_mask)
{
	struct drm_i915_private *i915 = to_i915(drm);

	if (unit_mask & BIT(VLV_IOSF_SB_PUNIT))
		__vlv_punit_get(i915);

	mutex_lock(&i915->vlv_iosf_sb.lock);

	i915->vlv_iosf_sb.locked_unit_mask |= unit_mask;
}

void vlv_iosf_sb_put(struct drm_device *drm, unsigned long unit_mask)
{
	struct drm_i915_private *i915 = to_i915(drm);

	i915->vlv_iosf_sb.locked_unit_mask &= ~unit_mask;

	drm_WARN_ON(drm, i915->vlv_iosf_sb.locked_unit_mask);

	mutex_unlock(&i915->vlv_iosf_sb.lock);

	if (unit_mask & BIT(VLV_IOSF_SB_PUNIT))
		__vlv_punit_put(i915);
}

static int vlv_sideband_rw(struct drm_i915_private *i915,
			   u32 devfn, u32 port, u32 opcode,
			   u32 addr, u32 *val)
{
	struct intel_uncore *uncore = &i915->uncore;
	const bool is_read = (opcode == SB_MRD_NP || opcode == SB_CRRDDA_NP);
	int err;

	lockdep_assert_held(&i915->vlv_iosf_sb.lock);
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

static u32 unit_to_devfn(enum vlv_iosf_sb_unit unit)
{
	if (unit == VLV_IOSF_SB_DPIO || unit == VLV_IOSF_SB_DPIO_2 ||
	    unit == VLV_IOSF_SB_FLISDSI)
		return DPIO_DEVFN;
	else
		return PCI_DEVFN(0, 0);
}

static u32 unit_to_port(enum vlv_iosf_sb_unit unit)
{
	switch (unit) {
	case VLV_IOSF_SB_BUNIT:
		return IOSF_PORT_BUNIT;
	case VLV_IOSF_SB_CCK:
		return IOSF_PORT_CCK;
	case VLV_IOSF_SB_CCU:
		return IOSF_PORT_CCU;
	case VLV_IOSF_SB_DPIO:
		return IOSF_PORT_DPIO;
	case VLV_IOSF_SB_DPIO_2:
		return IOSF_PORT_DPIO_2;
	case VLV_IOSF_SB_FLISDSI:
		return IOSF_PORT_FLISDSI;
	case VLV_IOSF_SB_GPIO:
		return 0; /* FIXME: unused */
	case VLV_IOSF_SB_NC:
		return IOSF_PORT_NC;
	case VLV_IOSF_SB_PUNIT:
		return IOSF_PORT_PUNIT;
	default:
		return 0;
	}
}

static u32 unit_to_opcode(enum vlv_iosf_sb_unit unit, bool write)
{
	if (unit == VLV_IOSF_SB_DPIO || unit == VLV_IOSF_SB_DPIO_2)
		return write ? SB_MWR_NP : SB_MRD_NP;
	else
		return write ? SB_CRWRDA_NP : SB_CRRDDA_NP;
}

u32 vlv_iosf_sb_read(struct drm_device *drm, enum vlv_iosf_sb_unit unit, u32 addr)
{
	struct drm_i915_private *i915 = to_i915(drm);
	u32 devfn, port, opcode, val = 0;

	devfn = unit_to_devfn(unit);
	port = unit_to_port(unit);
	opcode = unit_to_opcode(unit, false);

	if (drm_WARN_ONCE(&i915->drm, !port, "invalid unit %d\n", unit))
		return 0;

	drm_WARN_ON(&i915->drm, !(i915->vlv_iosf_sb.locked_unit_mask & BIT(unit)));

	vlv_sideband_rw(i915, devfn, port, opcode, addr, &val);

	return val;
}

int vlv_iosf_sb_write(struct drm_device *drm, enum vlv_iosf_sb_unit unit, u32 addr, u32 val)
{
	struct drm_i915_private *i915 = to_i915(drm);
	u32 devfn, port, opcode;

	devfn = unit_to_devfn(unit);
	port = unit_to_port(unit);
	opcode = unit_to_opcode(unit, true);

	if (drm_WARN_ONCE(&i915->drm, !port, "invalid unit %d\n", unit))
		return -EINVAL;

	drm_WARN_ON(&i915->drm, !(i915->vlv_iosf_sb.locked_unit_mask & BIT(unit)));

	return vlv_sideband_rw(i915, devfn, port, opcode, addr, &val);
}

void vlv_iosf_sb_init(struct drm_i915_private *i915)
{
	if (IS_VALLEYVIEW(i915) || IS_CHERRYVIEW(i915))
		mutex_init(&i915->vlv_iosf_sb.lock);

	if (IS_VALLEYVIEW(i915))
		cpu_latency_qos_add_request(&i915->vlv_iosf_sb.qos, PM_QOS_DEFAULT_VALUE);
}

void vlv_iosf_sb_fini(struct drm_i915_private *i915)
{
	if (IS_VALLEYVIEW(i915))
		cpu_latency_qos_remove_request(&i915->vlv_iosf_sb.qos);

	if (IS_VALLEYVIEW(i915) || IS_CHERRYVIEW(i915))
		mutex_destroy(&i915->vlv_iosf_sb.lock);
}

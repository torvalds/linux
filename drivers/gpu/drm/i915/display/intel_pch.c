// SPDX-License-Identifier: MIT
/*
 * Copyright 2025 Intel Corporation.
 */

#include <drm/drm_print.h>

#include "intel_de.h"
#include "intel_display.h"
#include "intel_display_regs.h"
#include "intel_display_core.h"
#include "intel_display_utils.h"
#include "intel_pch.h"

#define INTEL_PCH_DEVICE_ID_MASK		0xff80
#define INTEL_PCH_IBX_DEVICE_ID_TYPE		0x3b00
#define INTEL_PCH_CPT_DEVICE_ID_TYPE		0x1c00
#define INTEL_PCH_PPT_DEVICE_ID_TYPE		0x1e00
#define INTEL_PCH_LPT_DEVICE_ID_TYPE		0x8c00
#define INTEL_PCH_LPT_LP_DEVICE_ID_TYPE		0x9c00
#define INTEL_PCH_WPT_DEVICE_ID_TYPE		0x8c80
#define INTEL_PCH_WPT_LP_DEVICE_ID_TYPE		0x9c80
#define INTEL_PCH_SPT_DEVICE_ID_TYPE		0xA100
#define INTEL_PCH_SPT_LP_DEVICE_ID_TYPE		0x9D00
#define INTEL_PCH_KBP_DEVICE_ID_TYPE		0xA280
#define INTEL_PCH_CNP_DEVICE_ID_TYPE		0xA300
#define INTEL_PCH_CNP_LP_DEVICE_ID_TYPE		0x9D80
#define INTEL_PCH_CMP_DEVICE_ID_TYPE		0x0280
#define INTEL_PCH_CMP2_DEVICE_ID_TYPE		0x0680
#define INTEL_PCH_CMP_V_DEVICE_ID_TYPE		0xA380
#define INTEL_PCH_ICP_DEVICE_ID_TYPE		0x3480
#define INTEL_PCH_ICP2_DEVICE_ID_TYPE		0x3880
#define INTEL_PCH_MCC_DEVICE_ID_TYPE		0x4B00
#define INTEL_PCH_TGP_DEVICE_ID_TYPE		0xA080
#define INTEL_PCH_TGP2_DEVICE_ID_TYPE		0x4380
#define INTEL_PCH_JSP_DEVICE_ID_TYPE		0x4D80
#define INTEL_PCH_ADP_DEVICE_ID_TYPE		0x7A80
#define INTEL_PCH_ADP2_DEVICE_ID_TYPE		0x5180
#define INTEL_PCH_ADP3_DEVICE_ID_TYPE		0x7A00
#define INTEL_PCH_ADP4_DEVICE_ID_TYPE		0x5480
#define INTEL_PCH_P2X_DEVICE_ID_TYPE		0x7100
#define INTEL_PCH_P3X_DEVICE_ID_TYPE		0x7000
#define INTEL_PCH_QEMU_DEVICE_ID_TYPE		0x2900 /* qemu q35 has 2918 */

/*
 * Check for platforms where the south display is on the same PCI device or SoC
 * die as the north display. The PCH (if it even exists) is not involved in
 * display. Return a fake PCH type for south display handling on these
 * platforms, without actually detecting the PCH, and PCH_NONE otherwise.
 */
static enum intel_pch intel_pch_fake_for_south_display(struct intel_display *display)
{
	enum intel_pch pch_type = PCH_NONE;

	if (DISPLAY_VER(display) >= 20)
		pch_type = PCH_LNL;
	else if (display->platform.battlemage || display->platform.meteorlake)
		pch_type = PCH_MTL;
	else if (display->platform.dg2)
		pch_type = PCH_DG2;
	else if (display->platform.dg1)
		pch_type = PCH_DG1;

	return pch_type;
}

/* Map PCH device id to PCH type, or PCH_NONE if unknown. */
static enum intel_pch
intel_pch_type(const struct intel_display *display, unsigned short id)
{
	switch (id) {
	case INTEL_PCH_IBX_DEVICE_ID_TYPE:
		drm_dbg_kms(display->drm, "Found Ibex Peak PCH\n");
		drm_WARN_ON(display->drm, DISPLAY_VER(display) != 5);
		return PCH_IBX;
	case INTEL_PCH_CPT_DEVICE_ID_TYPE:
		drm_dbg_kms(display->drm, "Found CougarPoint PCH\n");
		drm_WARN_ON(display->drm,
			    DISPLAY_VER(display) != 6 &&
			    !display->platform.ivybridge);
		return PCH_CPT;
	case INTEL_PCH_PPT_DEVICE_ID_TYPE:
		drm_dbg_kms(display->drm, "Found PantherPoint PCH\n");
		drm_WARN_ON(display->drm,
			    DISPLAY_VER(display) != 6 &&
			    !display->platform.ivybridge);
		/* PPT is CPT compatible */
		return PCH_CPT;
	case INTEL_PCH_LPT_DEVICE_ID_TYPE:
		drm_dbg_kms(display->drm, "Found LynxPoint PCH\n");
		drm_WARN_ON(display->drm,
			    !display->platform.haswell &&
			    !display->platform.broadwell);
		drm_WARN_ON(display->drm,
			    display->platform.haswell_ult ||
			    display->platform.broadwell_ult);
		return PCH_LPT_H;
	case INTEL_PCH_LPT_LP_DEVICE_ID_TYPE:
		drm_dbg_kms(display->drm, "Found LynxPoint LP PCH\n");
		drm_WARN_ON(display->drm,
			    !display->platform.haswell &&
			    !display->platform.broadwell);
		drm_WARN_ON(display->drm,
			    !display->platform.haswell_ult &&
			    !display->platform.broadwell_ult);
		return PCH_LPT_LP;
	case INTEL_PCH_WPT_DEVICE_ID_TYPE:
		drm_dbg_kms(display->drm, "Found WildcatPoint PCH\n");
		drm_WARN_ON(display->drm,
			    !display->platform.haswell &&
			    !display->platform.broadwell);
		drm_WARN_ON(display->drm,
			    display->platform.haswell_ult ||
			    display->platform.broadwell_ult);
		/* WPT is LPT compatible */
		return PCH_LPT_H;
	case INTEL_PCH_WPT_LP_DEVICE_ID_TYPE:
		drm_dbg_kms(display->drm, "Found WildcatPoint LP PCH\n");
		drm_WARN_ON(display->drm,
			    !display->platform.haswell &&
			    !display->platform.broadwell);
		drm_WARN_ON(display->drm,
			    !display->platform.haswell_ult &&
			    !display->platform.broadwell_ult);
		/* WPT is LPT compatible */
		return PCH_LPT_LP;
	case INTEL_PCH_SPT_DEVICE_ID_TYPE:
		drm_dbg_kms(display->drm, "Found SunrisePoint PCH\n");
		drm_WARN_ON(display->drm,
			    !display->platform.skylake &&
			    !display->platform.kabylake &&
			    !display->platform.coffeelake);
		return PCH_SPT;
	case INTEL_PCH_SPT_LP_DEVICE_ID_TYPE:
		drm_dbg_kms(display->drm, "Found SunrisePoint LP PCH\n");
		drm_WARN_ON(display->drm,
			    !display->platform.skylake &&
			    !display->platform.kabylake &&
			    !display->platform.coffeelake &&
			    !display->platform.cometlake);
		return PCH_SPT;
	case INTEL_PCH_KBP_DEVICE_ID_TYPE:
		drm_dbg_kms(display->drm, "Found Kaby Lake PCH (KBP)\n");
		drm_WARN_ON(display->drm,
			    !display->platform.skylake &&
			    !display->platform.kabylake &&
			    !display->platform.coffeelake &&
			    !display->platform.cometlake);
		/* KBP is SPT compatible */
		return PCH_SPT;
	case INTEL_PCH_CNP_DEVICE_ID_TYPE:
		drm_dbg_kms(display->drm, "Found Cannon Lake PCH (CNP)\n");
		drm_WARN_ON(display->drm,
			    !display->platform.coffeelake &&
			    !display->platform.cometlake);
		return PCH_CNP;
	case INTEL_PCH_CNP_LP_DEVICE_ID_TYPE:
		drm_dbg_kms(display->drm,
			    "Found Cannon Lake LP PCH (CNP-LP)\n");
		drm_WARN_ON(display->drm,
			    !display->platform.coffeelake &&
			    !display->platform.cometlake);
		return PCH_CNP;
	case INTEL_PCH_CMP_DEVICE_ID_TYPE:
	case INTEL_PCH_CMP2_DEVICE_ID_TYPE:
		drm_dbg_kms(display->drm, "Found Comet Lake PCH (CMP)\n");
		drm_WARN_ON(display->drm,
			    !display->platform.coffeelake &&
			    !display->platform.cometlake &&
			    !display->platform.rocketlake);
		/* CMP is CNP compatible */
		return PCH_CNP;
	case INTEL_PCH_CMP_V_DEVICE_ID_TYPE:
		drm_dbg_kms(display->drm, "Found Comet Lake V PCH (CMP-V)\n");
		drm_WARN_ON(display->drm,
			    !display->platform.coffeelake &&
			    !display->platform.cometlake);
		/* CMP-V is based on KBP, which is SPT compatible */
		return PCH_SPT;
	case INTEL_PCH_ICP_DEVICE_ID_TYPE:
	case INTEL_PCH_ICP2_DEVICE_ID_TYPE:
		drm_dbg_kms(display->drm, "Found Ice Lake PCH\n");
		drm_WARN_ON(display->drm, !display->platform.icelake);
		return PCH_ICP;
	case INTEL_PCH_MCC_DEVICE_ID_TYPE:
		drm_dbg_kms(display->drm, "Found Mule Creek Canyon PCH\n");
		drm_WARN_ON(display->drm, !(display->platform.jasperlake ||
					    display->platform.elkhartlake));
		/* MCC is TGP compatible */
		return PCH_TGP;
	case INTEL_PCH_TGP_DEVICE_ID_TYPE:
	case INTEL_PCH_TGP2_DEVICE_ID_TYPE:
		drm_dbg_kms(display->drm, "Found Tiger Lake LP PCH\n");
		drm_WARN_ON(display->drm, !display->platform.tigerlake &&
			    !display->platform.rocketlake &&
			    !display->platform.skylake &&
			    !display->platform.kabylake &&
			    !display->platform.coffeelake &&
			    !display->platform.cometlake);
		return PCH_TGP;
	case INTEL_PCH_JSP_DEVICE_ID_TYPE:
		drm_dbg_kms(display->drm, "Found Jasper Lake PCH\n");
		drm_WARN_ON(display->drm, !(display->platform.jasperlake ||
					    display->platform.elkhartlake));
		/* JSP is ICP compatible */
		return PCH_ICP;
	case INTEL_PCH_ADP_DEVICE_ID_TYPE:
	case INTEL_PCH_ADP2_DEVICE_ID_TYPE:
	case INTEL_PCH_ADP3_DEVICE_ID_TYPE:
	case INTEL_PCH_ADP4_DEVICE_ID_TYPE:
		drm_dbg_kms(display->drm, "Found Alder Lake PCH\n");
		drm_WARN_ON(display->drm, !display->platform.alderlake_s &&
			    !display->platform.alderlake_p);
		return PCH_ADP;
	default:
		return PCH_NONE;
	}
}

static void intel_pch_ibx_init_clock_gating(struct intel_display *display)
{
	/*
	 * On Ibex Peak and Cougar Point, we need to disable clock
	 * gating for the panel power sequencer or it will fail to
	 * start up when no ports are active.
	 */
	intel_de_write(display, SOUTH_DSPCLK_GATE_D,
		       PCH_DPLSUNIT_CLOCK_GATE_DISABLE);
}

static void intel_pch_cpt_init_clock_gating(struct intel_display *display)
{
	enum pipe pipe;
	u32 val;

	/*
	 * On Ibex Peak and Cougar Point, we need to disable clock
	 * gating for the panel power sequencer or it will fail to
	 * start up when no ports are active.
	 */
	intel_de_write(display, SOUTH_DSPCLK_GATE_D,
		       PCH_DPLSUNIT_CLOCK_GATE_DISABLE |
		       PCH_DPLUNIT_CLOCK_GATE_DISABLE |
		       PCH_CPUNIT_CLOCK_GATE_DISABLE);
	intel_de_rmw(display, SOUTH_CHICKEN2, 0, DPLS_EDP_PPS_FIX_DIS);

	/* The below fixes the weird display corruption, a few pixels shifted
	 * downward, on (only) LVDS of some HP laptops with IVY.
	 */
	for_each_pipe(display, pipe) {
		val = intel_de_read(display, TRANS_CHICKEN2(pipe));
		val |= TRANS_CHICKEN2_TIMING_OVERRIDE;
		val &= ~TRANS_CHICKEN2_FDI_POLARITY_REVERSED;
		if (display->vbt.fdi_rx_polarity_inverted)
			val |= TRANS_CHICKEN2_FDI_POLARITY_REVERSED;
		val &= ~TRANS_CHICKEN2_DISABLE_DEEP_COLOR_COUNTER;
		val &= ~TRANS_CHICKEN2_DISABLE_DEEP_COLOR_MODESWITCH;
		intel_de_write(display, TRANS_CHICKEN2(pipe), val);
	}

	/* WADP0ClockGatingDisable */
	for_each_pipe(display, pipe)
		intel_de_write(display, TRANS_CHICKEN1(pipe),
			       TRANS_CHICKEN1_DP0UNIT_GC_DISABLE);
}

static void intel_pch_lpt_init_clock_gating(struct intel_display *display)
{
	/*
	 * TODO: this bit should only be enabled when really needed, then
	 * disabled when not needed anymore in order to save power.
	 */
	if (HAS_PCH_LPT_LP(display))
		intel_de_rmw(display, SOUTH_DSPCLK_GATE_D, 0,
			     PCH_LP_PARTITION_LEVEL_DISABLE);

	/* WADPOClockGatingDisable:hsw */
	intel_de_rmw(display, TRANS_CHICKEN1(PIPE_A), 0,
		     TRANS_CHICKEN1_DP0UNIT_GC_DISABLE);
}

static void intel_pch_cnp_init_clock_gating(struct intel_display *display)
{
	/* Display WA #1181 WaSouthDisplayDisablePWMCGEGating: cnp */
	intel_de_rmw(display, SOUTH_DSPCLK_GATE_D, 0,
		     CNP_PWM_CGE_GATING_DISABLE);
}

void intel_pch_init_clock_gating(struct intel_display *display)
{
	switch (INTEL_PCH_TYPE(display)) {
	case PCH_IBX:
		intel_pch_ibx_init_clock_gating(display);
		break;
	case PCH_CPT:
		intel_pch_cpt_init_clock_gating(display);
		break;
	case PCH_LPT_H:
	case PCH_LPT_LP:
		intel_pch_lpt_init_clock_gating(display);
		break;
	case PCH_CNP:
		intel_pch_cnp_init_clock_gating(display);
		break;
	default:
		break;
	}
}

static bool intel_is_virt_pch(unsigned short id,
			      unsigned short svendor, unsigned short sdevice)
{
	return (id == INTEL_PCH_P2X_DEVICE_ID_TYPE ||
		id == INTEL_PCH_P3X_DEVICE_ID_TYPE ||
		(id == INTEL_PCH_QEMU_DEVICE_ID_TYPE &&
		 svendor == PCI_SUBVENDOR_ID_REDHAT_QUMRANET &&
		 sdevice == PCI_SUBDEVICE_ID_QEMU));
}

static void
intel_virt_detect_pch(const struct intel_display *display,
		      unsigned short *pch_id, enum intel_pch *pch_type)
{
	unsigned short id = 0;

	/*
	 * In a virtualized passthrough environment we can be in a
	 * setup where the ISA bridge is not able to be passed through.
	 * In this case, a south bridge can be emulated and we have to
	 * make an educated guess as to which PCH is really there.
	 */

	if (display->platform.alderlake_s || display->platform.alderlake_p)
		id = INTEL_PCH_ADP_DEVICE_ID_TYPE;
	else if (display->platform.tigerlake || display->platform.rocketlake)
		id = INTEL_PCH_TGP_DEVICE_ID_TYPE;
	else if (display->platform.jasperlake || display->platform.elkhartlake)
		id = INTEL_PCH_MCC_DEVICE_ID_TYPE;
	else if (display->platform.icelake)
		id = INTEL_PCH_ICP_DEVICE_ID_TYPE;
	else if (display->platform.coffeelake ||
		 display->platform.cometlake)
		id = INTEL_PCH_CNP_DEVICE_ID_TYPE;
	else if (display->platform.kabylake || display->platform.skylake)
		id = INTEL_PCH_SPT_DEVICE_ID_TYPE;
	else if (display->platform.haswell_ult ||
		 display->platform.broadwell_ult)
		id = INTEL_PCH_LPT_LP_DEVICE_ID_TYPE;
	else if (display->platform.haswell || display->platform.broadwell)
		id = INTEL_PCH_LPT_DEVICE_ID_TYPE;
	else if (DISPLAY_VER(display) == 6 || display->platform.ivybridge)
		id = INTEL_PCH_CPT_DEVICE_ID_TYPE;
	else if (DISPLAY_VER(display) == 5)
		id = INTEL_PCH_IBX_DEVICE_ID_TYPE;

	if (id)
		drm_dbg_kms(display->drm, "Assuming PCH ID %04x\n", id);
	else
		drm_dbg_kms(display->drm, "Assuming no PCH\n");

	*pch_type = intel_pch_type(display, id);

	/* Sanity check virtual PCH id */
	if (drm_WARN_ON(display->drm,
			id && *pch_type == PCH_NONE))
		id = 0;

	*pch_id = id;
}

void intel_pch_detect(struct intel_display *display)
{
	struct pci_dev *pch = NULL;
	unsigned short id;
	enum intel_pch pch_type;

	pch_type = intel_pch_fake_for_south_display(display);
	if (pch_type != PCH_NONE) {
		display->pch_type = pch_type;
		drm_dbg_kms(display->drm,
			    "PCH not involved in display, using fake PCH type %d for south display\n",
			    pch_type);
		return;
	}

	/*
	 * The reason to probe ISA bridge instead of Dev31:Fun0 is to
	 * make graphics device passthrough work easy for VMM, that only
	 * need to expose ISA bridge to let driver know the real hardware
	 * underneath. This is a requirement from virtualization team.
	 *
	 * In some virtualized environments (e.g. XEN), there is irrelevant
	 * ISA bridge in the system. To work reliably, we should scan through
	 * all the ISA bridge devices and check for the first match, instead
	 * of only checking the first one.
	 */
	while ((pch = pci_get_class(PCI_CLASS_BRIDGE_ISA << 8, pch))) {
		if (pch->vendor != PCI_VENDOR_ID_INTEL)
			continue;

		id = pch->device & INTEL_PCH_DEVICE_ID_MASK;

		pch_type = intel_pch_type(display, id);
		if (pch_type != PCH_NONE) {
			display->pch_type = pch_type;
			break;
		} else if (intel_is_virt_pch(id, pch->subsystem_vendor,
					     pch->subsystem_device)) {
			intel_virt_detect_pch(display, &id, &pch_type);
			display->pch_type = pch_type;
			break;
		}
	}

	/*
	 * Use PCH_NOP (PCH but no South Display) for PCH platforms without
	 * display.
	 */
	if (pch && !HAS_DISPLAY(display)) {
		drm_dbg_kms(display->drm,
			    "Display disabled, reverting to NOP PCH\n");
		display->pch_type = PCH_NOP;
	} else if (!pch) {
		if (intel_display_run_as_guest(display) && HAS_DISPLAY(display)) {
			intel_virt_detect_pch(display, &id, &pch_type);
			display->pch_type = pch_type;
		} else {
			drm_dbg_kms(display->drm, "No PCH found.\n");
		}
	}

	pci_dev_put(pch);
}

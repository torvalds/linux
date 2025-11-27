/*
 * Copyright © 2014 Intel Corporation
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

#include <linux/debugfs.h>
#include <linux/firmware.h>
#include <drm/drm_vblank.h>

#include <drm/drm_file.h>
#include <drm/drm_print.h>

#include "i915_reg.h"
#include "i915_utils.h"
#include "intel_crtc.h"
#include "intel_de.h"
#include "intel_display_power_well.h"
#include "intel_display_regs.h"
#include "intel_display_rpm.h"
#include "intel_display_types.h"
#include "intel_dmc.h"
#include "intel_dmc_regs.h"
#include "intel_flipq.h"
#include "intel_step.h"

/**
 * DOC: DMC Firmware Support
 *
 * From gen9 onwards we have newly added DMC (Display microcontroller) in display
 * engine to save and restore the state of display engine when it enter into
 * low-power state and comes back to normal.
 */

#define INTEL_DMC_FIRMWARE_URL "https://git.kernel.org/pub/scm/linux/kernel/git/firmware/linux-firmware.git"

enum intel_dmc_id {
	DMC_FW_MAIN = 0,
	DMC_FW_PIPEA,
	DMC_FW_PIPEB,
	DMC_FW_PIPEC,
	DMC_FW_PIPED,
	DMC_FW_MAX
};

struct intel_dmc {
	struct intel_display *display;
	struct work_struct work;
	const char *fw_path;
	u32 max_fw_size; /* bytes */
	u32 version;
	struct {
		u32 dc5_start;
		u32 count;
	} dc6_allowed;
	struct dmc_fw_info {
		u32 mmio_count;
		i915_reg_t mmioaddr[20];
		u32 mmiodata[20];
		u32 dmc_offset;
		u32 start_mmioaddr;
		u32 dmc_fw_size; /*dwords */
		u32 *payload;
		bool present;
	} dmc_info[DMC_FW_MAX];
};

/* Note: This may be NULL. */
static struct intel_dmc *display_to_dmc(struct intel_display *display)
{
	return display->dmc.dmc;
}

static const char *dmc_firmware_param(struct intel_display *display)
{
	const char *p = display->params.dmc_firmware_path;

	return p && *p ? p : NULL;
}

static bool dmc_firmware_param_disabled(struct intel_display *display)
{
	const char *p = dmc_firmware_param(display);

	/* Magic path to indicate disabled */
	return p && !strcmp(p, "/dev/null");
}

#define DMC_VERSION(major, minor)	((major) << 16 | (minor))
#define DMC_VERSION_MAJOR(version)	((version) >> 16)
#define DMC_VERSION_MINOR(version)	((version) & 0xffff)

#define DMC_PATH(platform) \
	"i915/" __stringify(platform) "_dmc.bin"

/*
 * New DMC additions should not use this. This is used solely to remain
 * compatible with systems that have not yet updated DMC blobs to use
 * unversioned file names.
 */
#define DMC_LEGACY_PATH(platform, major, minor) \
	"i915/"					\
	__stringify(platform) "_dmc_ver"	\
	__stringify(major) "_"			\
	__stringify(minor) ".bin"

#define XE2LPD_DMC_MAX_FW_SIZE		0x8000
#define XELPDP_DMC_MAX_FW_SIZE		0x7000
#define DISPLAY_VER13_DMC_MAX_FW_SIZE	0x20000
#define DISPLAY_VER12_DMC_MAX_FW_SIZE	ICL_DMC_MAX_FW_SIZE

#define XE3LPD_3002_DMC_PATH		DMC_PATH(xe3lpd_3002)
MODULE_FIRMWARE(XE3LPD_3002_DMC_PATH);

#define XE3LPD_DMC_PATH			DMC_PATH(xe3lpd)
MODULE_FIRMWARE(XE3LPD_DMC_PATH);

#define XE2LPD_DMC_PATH			DMC_PATH(xe2lpd)
MODULE_FIRMWARE(XE2LPD_DMC_PATH);

#define BMG_DMC_PATH			DMC_PATH(bmg)
MODULE_FIRMWARE(BMG_DMC_PATH);

#define MTL_DMC_PATH			DMC_PATH(mtl)
MODULE_FIRMWARE(MTL_DMC_PATH);

#define DG2_DMC_PATH			DMC_LEGACY_PATH(dg2, 2, 08)
MODULE_FIRMWARE(DG2_DMC_PATH);

#define ADLP_DMC_PATH			DMC_PATH(adlp)
#define ADLP_DMC_FALLBACK_PATH		DMC_LEGACY_PATH(adlp, 2, 16)
MODULE_FIRMWARE(ADLP_DMC_PATH);
MODULE_FIRMWARE(ADLP_DMC_FALLBACK_PATH);

#define ADLS_DMC_PATH			DMC_LEGACY_PATH(adls, 2, 01)
MODULE_FIRMWARE(ADLS_DMC_PATH);

#define DG1_DMC_PATH			DMC_LEGACY_PATH(dg1, 2, 02)
MODULE_FIRMWARE(DG1_DMC_PATH);

#define RKL_DMC_PATH			DMC_LEGACY_PATH(rkl, 2, 03)
MODULE_FIRMWARE(RKL_DMC_PATH);

#define TGL_DMC_PATH			DMC_LEGACY_PATH(tgl, 2, 12)
MODULE_FIRMWARE(TGL_DMC_PATH);

#define ICL_DMC_PATH			DMC_LEGACY_PATH(icl, 1, 09)
#define ICL_DMC_MAX_FW_SIZE		0x6000
MODULE_FIRMWARE(ICL_DMC_PATH);

#define GLK_DMC_PATH			DMC_LEGACY_PATH(glk, 1, 04)
#define GLK_DMC_MAX_FW_SIZE		0x4000
MODULE_FIRMWARE(GLK_DMC_PATH);

#define KBL_DMC_PATH			DMC_LEGACY_PATH(kbl, 1, 04)
#define KBL_DMC_MAX_FW_SIZE		BXT_DMC_MAX_FW_SIZE
MODULE_FIRMWARE(KBL_DMC_PATH);

#define SKL_DMC_PATH			DMC_LEGACY_PATH(skl, 1, 27)
#define SKL_DMC_MAX_FW_SIZE		BXT_DMC_MAX_FW_SIZE
MODULE_FIRMWARE(SKL_DMC_PATH);

#define BXT_DMC_PATH			DMC_LEGACY_PATH(bxt, 1, 07)
#define BXT_DMC_MAX_FW_SIZE		0x3000
MODULE_FIRMWARE(BXT_DMC_PATH);

static const char *dmc_firmware_default(struct intel_display *display, u32 *size)
{
	const char *fw_path = NULL;
	u32 max_fw_size = 0;
	if (DISPLAY_VERx100(display) == 3002) {
		fw_path = XE3LPD_3002_DMC_PATH;
		max_fw_size = XE2LPD_DMC_MAX_FW_SIZE;
	} else if (DISPLAY_VERx100(display) == 3000) {
		fw_path = XE3LPD_DMC_PATH;
		max_fw_size = XE2LPD_DMC_MAX_FW_SIZE;
	} else if (DISPLAY_VERx100(display) == 2000) {
		fw_path = XE2LPD_DMC_PATH;
		max_fw_size = XE2LPD_DMC_MAX_FW_SIZE;
	} else if (DISPLAY_VERx100(display) == 1401) {
		fw_path = BMG_DMC_PATH;
		max_fw_size = XELPDP_DMC_MAX_FW_SIZE;
	} else if (DISPLAY_VERx100(display) == 1400) {
		fw_path = MTL_DMC_PATH;
		max_fw_size = XELPDP_DMC_MAX_FW_SIZE;
	} else if (display->platform.dg2) {
		fw_path = DG2_DMC_PATH;
		max_fw_size = DISPLAY_VER13_DMC_MAX_FW_SIZE;
	} else if (display->platform.alderlake_p) {
		fw_path = ADLP_DMC_PATH;
		max_fw_size = DISPLAY_VER13_DMC_MAX_FW_SIZE;
	} else if (display->platform.alderlake_s) {
		fw_path = ADLS_DMC_PATH;
		max_fw_size = DISPLAY_VER12_DMC_MAX_FW_SIZE;
	} else if (display->platform.dg1) {
		fw_path = DG1_DMC_PATH;
		max_fw_size = DISPLAY_VER12_DMC_MAX_FW_SIZE;
	} else if (display->platform.rocketlake) {
		fw_path = RKL_DMC_PATH;
		max_fw_size = DISPLAY_VER12_DMC_MAX_FW_SIZE;
	} else if (display->platform.tigerlake) {
		fw_path = TGL_DMC_PATH;
		max_fw_size = DISPLAY_VER12_DMC_MAX_FW_SIZE;
	} else if (DISPLAY_VER(display) == 11) {
		fw_path = ICL_DMC_PATH;
		max_fw_size = ICL_DMC_MAX_FW_SIZE;
	} else if (display->platform.geminilake) {
		fw_path = GLK_DMC_PATH;
		max_fw_size = GLK_DMC_MAX_FW_SIZE;
	} else if (display->platform.kabylake ||
		   display->platform.coffeelake ||
		   display->platform.cometlake) {
		fw_path = KBL_DMC_PATH;
		max_fw_size = KBL_DMC_MAX_FW_SIZE;
	} else if (display->platform.skylake) {
		fw_path = SKL_DMC_PATH;
		max_fw_size = SKL_DMC_MAX_FW_SIZE;
	} else if (display->platform.broxton) {
		fw_path = BXT_DMC_PATH;
		max_fw_size = BXT_DMC_MAX_FW_SIZE;
	}

	*size = max_fw_size;

	return fw_path;
}

#define DMC_DEFAULT_FW_OFFSET		0xFFFFFFFF
#define PACKAGE_MAX_FW_INFO_ENTRIES	20
#define PACKAGE_V2_MAX_FW_INFO_ENTRIES	32
#define DMC_V1_MAX_MMIO_COUNT		8
#define DMC_V3_MAX_MMIO_COUNT		20
#define DMC_V1_MMIO_START_RANGE		0x80000

#define PIPE_TO_DMC_ID(pipe)		 (DMC_FW_PIPEA + ((pipe) - PIPE_A))

struct intel_css_header {
	/* 0x09 for DMC */
	u32 module_type;

	/* Includes the DMC specific header in dwords */
	u32 header_len;

	/* always value would be 0x10000 */
	u32 header_ver;

	/* Not used */
	u32 module_id;

	/* Not used */
	u32 module_vendor;

	/* in YYYYMMDD format */
	u32 date;

	/* Size in dwords (CSS_Headerlen + PackageHeaderLen + dmc FWsLen)/4 */
	u32 size;

	/* Not used */
	u32 key_size;

	/* Not used */
	u32 modulus_size;

	/* Not used */
	u32 exponent_size;

	/* Not used */
	u32 reserved1[12];

	/* Major Minor */
	u32 version;

	/* Not used */
	u32 reserved2[8];

	/* Not used */
	u32 kernel_header_info;
} __packed;

struct intel_fw_info {
	u8 reserved1;

	/* reserved on package_header version 1, must be 0 on version 2 */
	u8 dmc_id;

	/* Stepping (A, B, C, ..., *). * is a wildcard */
	char stepping;

	/* Sub-stepping (0, 1, ..., *). * is a wildcard */
	char substepping;

	u32 offset;
	u32 reserved2;
} __packed;

struct intel_package_header {
	/* DMC container header length in dwords */
	u8 header_len;

	/* 0x01, 0x02 */
	u8 header_ver;

	u8 reserved[10];

	/* Number of valid entries in the FWInfo array below */
	u32 num_entries;
} __packed;

struct intel_dmc_header_base {
	/* always value would be 0x40403E3E */
	u32 signature;

	/* DMC binary header length */
	u8 header_len;

	/* 0x01 */
	u8 header_ver;

	/* Reserved */
	u16 dmcc_ver;

	/* Major, Minor */
	u32 project;

	/* Firmware program size (excluding header) in dwords */
	u32 fw_size;

	/* Major Minor version */
	u32 fw_version;
} __packed;

struct intel_dmc_header_v1 {
	struct intel_dmc_header_base base;

	/* Number of valid MMIO cycles present. */
	u32 mmio_count;

	/* MMIO address */
	u32 mmioaddr[DMC_V1_MAX_MMIO_COUNT];

	/* MMIO data */
	u32 mmiodata[DMC_V1_MAX_MMIO_COUNT];

	/* FW filename  */
	char dfile[32];

	u32 reserved1[2];
} __packed;

struct intel_dmc_header_v3 {
	struct intel_dmc_header_base base;

	/* DMC RAM start MMIO address */
	u32 start_mmioaddr;

	u32 reserved[9];

	/* FW filename */
	char dfile[32];

	/* Number of valid MMIO cycles present. */
	u32 mmio_count;

	/* MMIO address */
	u32 mmioaddr[DMC_V3_MAX_MMIO_COUNT];

	/* MMIO data */
	u32 mmiodata[DMC_V3_MAX_MMIO_COUNT];
} __packed;

struct stepping_info {
	char stepping;
	char substepping;
};

#define for_each_dmc_id(__dmc_id) \
	for ((__dmc_id) = DMC_FW_MAIN; (__dmc_id) < DMC_FW_MAX; (__dmc_id)++)

static bool is_valid_dmc_id(enum intel_dmc_id dmc_id)
{
	return dmc_id >= DMC_FW_MAIN && dmc_id < DMC_FW_MAX;
}

static bool has_dmc_id_fw(struct intel_display *display, enum intel_dmc_id dmc_id)
{
	struct intel_dmc *dmc = display_to_dmc(display);

	return dmc && dmc->dmc_info[dmc_id].payload;
}

bool intel_dmc_has_payload(struct intel_display *display)
{
	return has_dmc_id_fw(display, DMC_FW_MAIN);
}

static const struct stepping_info *
intel_get_stepping_info(struct intel_display *display,
			struct stepping_info *si)
{
	const char *step_name = intel_step_name(INTEL_DISPLAY_STEP(display));

	si->stepping = step_name[0];
	si->substepping = step_name[1];
	return si;
}

static void gen9_set_dc_state_debugmask(struct intel_display *display)
{
	/* The below bit doesn't need to be cleared ever afterwards */
	intel_de_rmw(display, DC_STATE_DEBUG, 0,
		     DC_STATE_DEBUG_MASK_CORES | DC_STATE_DEBUG_MASK_MEMORY_UP);
	intel_de_posting_read(display, DC_STATE_DEBUG);
}

static void disable_event_handler(struct intel_display *display,
				  i915_reg_t ctl_reg, i915_reg_t htp_reg)
{
	intel_de_write(display, ctl_reg,
		       REG_FIELD_PREP(DMC_EVT_CTL_TYPE_MASK,
				      DMC_EVT_CTL_TYPE_EDGE_0_1) |
		       REG_FIELD_PREP(DMC_EVT_CTL_EVENT_ID_MASK,
				      DMC_EVENT_FALSE));
	intel_de_write(display, htp_reg, 0);
}

static void disable_all_event_handlers(struct intel_display *display,
				       enum intel_dmc_id dmc_id)
{
	int handler;

	/* TODO: disable the event handlers on pre-GEN12 platforms as well */
	if (DISPLAY_VER(display) < 12)
		return;

	if (!has_dmc_id_fw(display, dmc_id))
		return;

	for (handler = 0; handler < DMC_EVENT_HANDLER_COUNT_GEN12; handler++)
		disable_event_handler(display,
				      DMC_EVT_CTL(display, dmc_id, handler),
				      DMC_EVT_HTP(display, dmc_id, handler));
}

static void adlp_pipedmc_clock_gating_wa(struct intel_display *display, bool enable)
{
	enum pipe pipe;

	/*
	 * Wa_16015201720:adl-p,dg2
	 * The WA requires clock gating to be disabled all the time
	 * for pipe A and B.
	 * For pipe C and D clock gating needs to be disabled only
	 * during initializing the firmware.
	 */
	if (enable)
		for (pipe = PIPE_A; pipe <= PIPE_D; pipe++)
			intel_de_rmw(display, CLKGATE_DIS_PSL_EXT(pipe),
				     0, PIPEDMC_GATING_DIS);
	else
		for (pipe = PIPE_C; pipe <= PIPE_D; pipe++)
			intel_de_rmw(display, CLKGATE_DIS_PSL_EXT(pipe),
				     PIPEDMC_GATING_DIS, 0);
}

static void mtl_pipedmc_clock_gating_wa(struct intel_display *display)
{
	/*
	 * Wa_16015201720
	 * The WA requires clock gating to be disabled all the time
	 * for pipe A and B.
	 */
	intel_de_rmw(display, GEN9_CLKGATE_DIS_0, 0,
		     MTL_PIPEDMC_GATING_DIS(PIPE_A) |
		     MTL_PIPEDMC_GATING_DIS(PIPE_B));
}

static void pipedmc_clock_gating_wa(struct intel_display *display, bool enable)
{
	if (display->platform.meteorlake && enable)
		mtl_pipedmc_clock_gating_wa(display);
	else if (DISPLAY_VER(display) == 13)
		adlp_pipedmc_clock_gating_wa(display, enable);
}

static u32 pipedmc_interrupt_mask(struct intel_display *display)
{
	/*
	 * FIXME PIPEDMC_ERROR not enabled for now due to LNL pipe B
	 * triggering it during the first DC state transition. Figure
	 * out what is going on...
	 */
	return PIPEDMC_FLIPQ_PROG_DONE |
		PIPEDMC_GTT_FAULT |
		PIPEDMC_ATS_FAULT;
}

static u32 dmc_evt_ctl_disable(void)
{
	return REG_FIELD_PREP(DMC_EVT_CTL_TYPE_MASK,
			      DMC_EVT_CTL_TYPE_EDGE_0_1) |
		REG_FIELD_PREP(DMC_EVT_CTL_EVENT_ID_MASK,
			       DMC_EVENT_FALSE);
}

static bool is_dmc_evt_ctl_reg(struct intel_display *display,
			       enum intel_dmc_id dmc_id, i915_reg_t reg)
{
	u32 offset = i915_mmio_reg_offset(reg);
	u32 start = i915_mmio_reg_offset(DMC_EVT_CTL(display, dmc_id, 0));
	u32 end = i915_mmio_reg_offset(DMC_EVT_CTL(display, dmc_id, DMC_EVENT_HANDLER_COUNT_GEN12));

	return offset >= start && offset < end;
}

static bool is_dmc_evt_htp_reg(struct intel_display *display,
			       enum intel_dmc_id dmc_id, i915_reg_t reg)
{
	u32 offset = i915_mmio_reg_offset(reg);
	u32 start = i915_mmio_reg_offset(DMC_EVT_HTP(display, dmc_id, 0));
	u32 end = i915_mmio_reg_offset(DMC_EVT_HTP(display, dmc_id, DMC_EVENT_HANDLER_COUNT_GEN12));

	return offset >= start && offset < end;
}

static bool is_event_handler(struct intel_display *display,
			     enum intel_dmc_id dmc_id,
			     unsigned int event_id,
			     i915_reg_t reg, u32 data)
{
	return is_dmc_evt_ctl_reg(display, dmc_id, reg) &&
		REG_FIELD_GET(DMC_EVT_CTL_EVENT_ID_MASK, data) == event_id;
}

static bool fixup_dmc_evt(struct intel_display *display,
			  enum intel_dmc_id dmc_id,
			  i915_reg_t reg_ctl, u32 *data_ctl,
			  i915_reg_t reg_htp, u32 *data_htp)
{
	if (!is_dmc_evt_ctl_reg(display, dmc_id, reg_ctl))
		return false;

	if (!is_dmc_evt_htp_reg(display, dmc_id, reg_htp))
		return false;

	/* make sure reg_ctl and reg_htp are for the same event */
	if (i915_mmio_reg_offset(reg_ctl) - i915_mmio_reg_offset(DMC_EVT_CTL(display, dmc_id, 0)) !=
	    i915_mmio_reg_offset(reg_htp) - i915_mmio_reg_offset(DMC_EVT_HTP(display, dmc_id, 0)))
		return false;

	/*
	 * On ADL-S the HRR event handler is not restored after DC6.
	 * Clear it to zero from the beginning to avoid mismatches later.
	 */
	if (display->platform.alderlake_s && dmc_id == DMC_FW_MAIN &&
	    is_event_handler(display, dmc_id, MAINDMC_EVENT_VBLANK_A, reg_ctl, *data_ctl)) {
		*data_ctl = 0;
		*data_htp = 0;
		return true;
	}

	return false;
}

static bool disable_dmc_evt(struct intel_display *display,
			    enum intel_dmc_id dmc_id,
			    i915_reg_t reg, u32 data)
{
	if (!is_dmc_evt_ctl_reg(display, dmc_id, reg))
		return false;

	/* keep all pipe DMC events disabled by default */
	if (dmc_id != DMC_FW_MAIN)
		return true;

	/* also disable the flip queue event on the main DMC on TGL */
	if (display->platform.tigerlake &&
	    is_event_handler(display, dmc_id, MAINDMC_EVENT_CLK_MSEC, reg, data))
		return true;

	/* also disable the HRR event on the main DMC on TGL/ADLS */
	if ((display->platform.tigerlake || display->platform.alderlake_s) &&
	    is_event_handler(display, dmc_id, MAINDMC_EVENT_VBLANK_A, reg, data))
		return true;

	return false;
}

static u32 dmc_mmiodata(struct intel_display *display,
			struct intel_dmc *dmc,
			enum intel_dmc_id dmc_id, int i)
{
	if (disable_dmc_evt(display, dmc_id,
			    dmc->dmc_info[dmc_id].mmioaddr[i],
			    dmc->dmc_info[dmc_id].mmiodata[i]))
		return dmc_evt_ctl_disable();
	else
		return dmc->dmc_info[dmc_id].mmiodata[i];
}

static void dmc_load_mmio(struct intel_display *display, enum intel_dmc_id dmc_id)
{
	struct intel_dmc *dmc = display_to_dmc(display);
	int i;

	for (i = 0; i < dmc->dmc_info[dmc_id].mmio_count; i++) {
		intel_de_write(display, dmc->dmc_info[dmc_id].mmioaddr[i],
			       dmc_mmiodata(display, dmc, dmc_id, i));
	}
}

static void dmc_load_program(struct intel_display *display, enum intel_dmc_id dmc_id)
{
	struct intel_dmc *dmc = display_to_dmc(display);
	int i;

	disable_all_event_handlers(display, dmc_id);

	preempt_disable();

	for (i = 0; i < dmc->dmc_info[dmc_id].dmc_fw_size; i++) {
		intel_de_write_fw(display,
				  DMC_PROGRAM(dmc->dmc_info[dmc_id].start_mmioaddr, i),
				  dmc->dmc_info[dmc_id].payload[i]);
	}

	preempt_enable();

	dmc_load_mmio(display, dmc_id);
}

static void assert_dmc_loaded(struct intel_display *display,
			      enum intel_dmc_id dmc_id)
{
	struct intel_dmc *dmc = display_to_dmc(display);
	u32 expected, found;
	int i;

	if (!is_valid_dmc_id(dmc_id) || !has_dmc_id_fw(display, dmc_id))
		return;

	found = intel_de_read(display, DMC_PROGRAM(dmc->dmc_info[dmc_id].start_mmioaddr, 0));
	expected = dmc->dmc_info[dmc_id].payload[0];

	drm_WARN(display->drm, found != expected,
		 "DMC %d program storage start incorrect (expected 0x%x, current 0x%x)\n",
		 dmc_id, expected, found);

	for (i = 0; i < dmc->dmc_info[dmc_id].mmio_count; i++) {
		i915_reg_t reg = dmc->dmc_info[dmc_id].mmioaddr[i];

		found = intel_de_read(display, reg);
		expected = dmc_mmiodata(display, dmc, dmc_id, i);

		/* once set DMC_EVT_CTL_ENABLE can't be cleared :/ */
		if (is_dmc_evt_ctl_reg(display, dmc_id, reg)) {
			found &= ~DMC_EVT_CTL_ENABLE;
			expected &= ~DMC_EVT_CTL_ENABLE;
		}

		drm_WARN(display->drm, found != expected,
			 "DMC %d mmio[%d]/0x%x incorrect (expected 0x%x, current 0x%x)\n",
			 dmc_id, i, i915_mmio_reg_offset(reg), expected, found);
	}
}

void assert_main_dmc_loaded(struct intel_display *display)
{
	assert_dmc_loaded(display, DMC_FW_MAIN);
}

static bool need_pipedmc_load_program(struct intel_display *display)
{
	/* On TGL/derivatives pipe DMC state is lost when PG1 is disabled */
	return DISPLAY_VER(display) == 12;
}

static bool need_pipedmc_load_mmio(struct intel_display *display, enum pipe pipe)
{
	/*
	 * PTL:
	 * - pipe A/B DMC doesn't need save/restore
	 * - pipe C/D DMC is in PG0, needs manual save/restore
	 */
	if (DISPLAY_VER(display) == 30)
		return pipe >= PIPE_C;

	/*
	 * FIXME LNL unclear, main DMC firmware has the pipe DMC A/B PG0
	 * save/restore, but so far unable to see the loss of pipe DMC state
	 * in action. Are we just failing to turn off PG0 due to some other
	 * SoC level stuff?
	 */
	if (DISPLAY_VER(display) == 20)
		return false;

	/*
	 * FIXME BMG untested, main DMC firmware has the
	 * pipe DMC A/B PG0 save/restore...
	 */
	if (display->platform.battlemage)
		return false;

	/*
	 * DG2:
	 * - Pipe DMCs presumably in PG0?
	 * - No DC6, and even DC9 doesn't seem to result
	 *   in loss of DMC state for whatever reason
	 */
	if (display->platform.dg2)
		return false;

	/*
	 * ADL/MTL:
	 * - pipe A/B DMC is in PG0, saved/restored by the main DMC
	 * - pipe C/D DMC is in PG0, needs manual save/restore
	 */
	if (IS_DISPLAY_VER(display, 13, 14))
		return pipe >= PIPE_C;

	return false;
}

static bool can_enable_pipedmc(const struct intel_crtc_state *crtc_state)
{
	struct intel_display *display = to_intel_display(crtc_state);

	/*
	 * On TGL/derivatives pipe DMC state is lost when PG1 is disabled.
	 * Do not even enable the pipe DMC when that can happen outside
	 * of driver control (PSR+DC5/6).
	 */
	if (DISPLAY_VER(display) == 12 && crtc_state->has_psr)
		return false;

	return true;
}

void intel_dmc_enable_pipe(const struct intel_crtc_state *crtc_state)
{
	struct intel_display *display = to_intel_display(crtc_state);
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	enum pipe pipe = crtc->pipe;
	enum intel_dmc_id dmc_id = PIPE_TO_DMC_ID(pipe);

	if (!is_valid_dmc_id(dmc_id) || !has_dmc_id_fw(display, dmc_id))
		return;

	if (!can_enable_pipedmc(crtc_state)) {
		intel_dmc_disable_pipe(crtc_state);
		return;
	}

	if (need_pipedmc_load_program(display))
		dmc_load_program(display, dmc_id);
	else if (need_pipedmc_load_mmio(display, pipe))
		dmc_load_mmio(display, dmc_id);

	assert_dmc_loaded(display, dmc_id);

	if (DISPLAY_VER(display) >= 20) {
		intel_flipq_reset(display, pipe);

		intel_de_write(display, PIPEDMC_INTERRUPT(pipe), pipedmc_interrupt_mask(display));
		intel_de_write(display, PIPEDMC_INTERRUPT_MASK(pipe), ~pipedmc_interrupt_mask(display));
	}

	if (DISPLAY_VER(display) >= 14)
		intel_de_rmw(display, MTL_PIPEDMC_CONTROL, 0, PIPEDMC_ENABLE_MTL(pipe));
	else
		intel_de_rmw(display, PIPEDMC_CONTROL(pipe), 0, PIPEDMC_ENABLE);
}

void intel_dmc_disable_pipe(const struct intel_crtc_state *crtc_state)
{
	struct intel_display *display = to_intel_display(crtc_state);
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	enum pipe pipe = crtc->pipe;
	enum intel_dmc_id dmc_id = PIPE_TO_DMC_ID(pipe);

	if (!is_valid_dmc_id(dmc_id) || !has_dmc_id_fw(display, dmc_id))
		return;

	if (DISPLAY_VER(display) >= 14)
		intel_de_rmw(display, MTL_PIPEDMC_CONTROL, PIPEDMC_ENABLE_MTL(pipe), 0);
	else
		intel_de_rmw(display, PIPEDMC_CONTROL(pipe), PIPEDMC_ENABLE, 0);

	if (DISPLAY_VER(display) >= 20) {
		intel_de_write(display, PIPEDMC_INTERRUPT_MASK(pipe), ~0);
		intel_de_write(display, PIPEDMC_INTERRUPT(pipe), pipedmc_interrupt_mask(display));

		intel_flipq_reset(display, pipe);
	}
}

static void dmc_configure_event(struct intel_display *display,
				enum intel_dmc_id dmc_id,
				unsigned int event_id,
				bool enable)
{
	struct intel_dmc *dmc = display_to_dmc(display);
	int num_handlers = 0;
	int i;

	for (i = 0; i < dmc->dmc_info[dmc_id].mmio_count; i++) {
		i915_reg_t reg = dmc->dmc_info[dmc_id].mmioaddr[i];
		u32 data = dmc->dmc_info[dmc_id].mmiodata[i];

		if (!is_event_handler(display, dmc_id, event_id, reg, data))
			continue;

		intel_de_write(display, reg, enable ? data : dmc_evt_ctl_disable());
		num_handlers++;
	}

	drm_WARN_ONCE(display->drm, num_handlers != 1,
		      "DMC %d has %d handlers for event 0x%x\n",
		      dmc_id, num_handlers, event_id);
}

/**
 * intel_dmc_block_pkgc() - block PKG C-state
 * @display: display instance
 * @pipe: pipe which register use to block
 * @block: block/unblock
 *
 * This interface is target for Wa_16025596647 usage. I.e. to set/clear
 * PIPEDMC_BLOCK_PKGC_SW_BLOCK_PKGC_ALWAYS bit in PIPEDMC_BLOCK_PKGC_SW register.
 */
void intel_dmc_block_pkgc(struct intel_display *display, enum pipe pipe,
			  bool block)
{
	intel_de_rmw(display, PIPEDMC_BLOCK_PKGC_SW(pipe),
		     PIPEDMC_BLOCK_PKGC_SW_BLOCK_PKGC_ALWAYS, block ?
		     PIPEDMC_BLOCK_PKGC_SW_BLOCK_PKGC_ALWAYS : 0);
}

/**
 * intel_dmc_start_pkgc_exit_at_start_of_undelayed_vblank() - start of PKG
 * C-state exit
 * @display: display instance
 * @pipe: pipe which register use to block
 * @enable: enable/disable
 *
 * This interface is target for Wa_16025596647 usage. I.e. start the package C
 * exit at the start of the undelayed vblank
 */
void intel_dmc_start_pkgc_exit_at_start_of_undelayed_vblank(struct intel_display *display,
							    enum pipe pipe, bool enable)
{
	enum intel_dmc_id dmc_id = PIPE_TO_DMC_ID(pipe);

	dmc_configure_event(display, dmc_id, PIPEDMC_EVENT_VBLANK, enable);
}

/**
 * intel_dmc_load_program() - write the firmware from memory to register.
 * @display: display instance
 *
 * DMC firmware is read from a .bin file and kept in internal memory one time.
 * Everytime display comes back from low power state this function is called to
 * copy the firmware from internal memory to registers.
 */
void intel_dmc_load_program(struct intel_display *display)
{
	struct i915_power_domains *power_domains = &display->power.domains;
	enum intel_dmc_id dmc_id;

	if (!intel_dmc_has_payload(display))
		return;

	assert_display_rpm_held(display);

	pipedmc_clock_gating_wa(display, true);

	for_each_dmc_id(dmc_id) {
		dmc_load_program(display, dmc_id);
		assert_dmc_loaded(display, dmc_id);
	}

	if (DISPLAY_VER(display) >= 20)
		intel_de_write(display, DMC_FQ_W2_PTS_CFG_SEL,
			       PIPE_D_DMC_W2_PTS_CONFIG_SELECT(PIPE_D) |
			       PIPE_C_DMC_W2_PTS_CONFIG_SELECT(PIPE_C) |
			       PIPE_B_DMC_W2_PTS_CONFIG_SELECT(PIPE_B) |
			       PIPE_A_DMC_W2_PTS_CONFIG_SELECT(PIPE_A));

	power_domains->dc_state = 0;

	gen9_set_dc_state_debugmask(display);

	pipedmc_clock_gating_wa(display, false);
}

/**
 * intel_dmc_disable_program() - disable the firmware
 * @display: display instance
 *
 * Disable all event handlers in the firmware, making sure the firmware is
 * inactive after the display is uninitialized.
 */
void intel_dmc_disable_program(struct intel_display *display)
{
	enum intel_dmc_id dmc_id;

	if (!intel_dmc_has_payload(display))
		return;

	pipedmc_clock_gating_wa(display, true);

	for_each_dmc_id(dmc_id)
		disable_all_event_handlers(display, dmc_id);

	pipedmc_clock_gating_wa(display, false);
}

static bool fw_info_matches_stepping(const struct intel_fw_info *fw_info,
				     const struct stepping_info *si)
{
	if ((fw_info->substepping == '*' && si->stepping == fw_info->stepping) ||
	    (si->stepping == fw_info->stepping && si->substepping == fw_info->substepping) ||
	    /*
	     * If we don't find a more specific one from above two checks, we
	     * then check for the generic one to be sure to work even with
	     * "broken firmware"
	     */
	    (si->stepping == '*' && si->substepping == fw_info->substepping) ||
	    (fw_info->stepping == '*' && fw_info->substepping == '*'))
		return true;

	return false;
}

/*
 * Search fw_info table for dmc_offset to find firmware binary: num_entries is
 * already sanitized.
 */
static void dmc_set_fw_offset(struct intel_dmc *dmc,
			      const struct intel_fw_info *fw_info,
			      unsigned int num_entries,
			      const struct stepping_info *si,
			      u8 package_ver)
{
	struct intel_display *display = dmc->display;
	enum intel_dmc_id dmc_id;
	unsigned int i;

	for (i = 0; i < num_entries; i++) {
		dmc_id = package_ver <= 1 ? DMC_FW_MAIN : fw_info[i].dmc_id;

		if (!is_valid_dmc_id(dmc_id)) {
			drm_dbg(display->drm, "Unsupported firmware id: %u\n", dmc_id);
			continue;
		}

		/* More specific versions come first, so we don't even have to
		 * check for the stepping since we already found a previous FW
		 * for this id.
		 */
		if (dmc->dmc_info[dmc_id].present)
			continue;

		if (fw_info_matches_stepping(&fw_info[i], si)) {
			dmc->dmc_info[dmc_id].present = true;
			dmc->dmc_info[dmc_id].dmc_offset = fw_info[i].offset;
		}
	}
}

static bool dmc_mmio_addr_sanity_check(struct intel_dmc *dmc,
				       const u32 *mmioaddr, u32 mmio_count,
				       int header_ver, enum intel_dmc_id dmc_id)
{
	struct intel_display *display = dmc->display;
	u32 start_range, end_range;
	int i;

	if (header_ver == 1) {
		start_range = DMC_MMIO_START_RANGE;
		end_range = DMC_MMIO_END_RANGE;
	} else if (dmc_id == DMC_FW_MAIN) {
		start_range = TGL_MAIN_MMIO_START;
		end_range = TGL_MAIN_MMIO_END;
	} else if (DISPLAY_VER(display) >= 13) {
		start_range = ADLP_PIPE_MMIO_START;
		end_range = ADLP_PIPE_MMIO_END;
	} else if (DISPLAY_VER(display) >= 12) {
		start_range = TGL_PIPE_MMIO_START(dmc_id);
		end_range = TGL_PIPE_MMIO_END(dmc_id);
	} else {
		drm_warn(display->drm, "Unknown mmio range for sanity check");
		return false;
	}

	for (i = 0; i < mmio_count; i++) {
		if (mmioaddr[i] < start_range || mmioaddr[i] > end_range)
			return false;
	}

	return true;
}

static u32 parse_dmc_fw_header(struct intel_dmc *dmc,
			       const struct intel_dmc_header_base *dmc_header,
			       size_t rem_size, enum intel_dmc_id dmc_id)
{
	struct intel_display *display = dmc->display;
	struct dmc_fw_info *dmc_info = &dmc->dmc_info[dmc_id];
	unsigned int header_len_bytes, dmc_header_size, payload_size, i;
	const u32 *mmioaddr, *mmiodata;
	u32 mmio_count, mmio_count_max, start_mmioaddr;
	u8 *payload;

	BUILD_BUG_ON(ARRAY_SIZE(dmc_info->mmioaddr) < DMC_V3_MAX_MMIO_COUNT ||
		     ARRAY_SIZE(dmc_info->mmioaddr) < DMC_V1_MAX_MMIO_COUNT);

	/*
	 * Check if we can access common fields, we will checkc again below
	 * after we have read the version
	 */
	if (rem_size < sizeof(struct intel_dmc_header_base))
		goto error_truncated;

	/* Cope with small differences between v1 and v3 */
	if (dmc_header->header_ver == 3) {
		const struct intel_dmc_header_v3 *v3 =
			(const struct intel_dmc_header_v3 *)dmc_header;

		if (rem_size < sizeof(struct intel_dmc_header_v3))
			goto error_truncated;

		mmioaddr = v3->mmioaddr;
		mmiodata = v3->mmiodata;
		mmio_count = v3->mmio_count;
		mmio_count_max = DMC_V3_MAX_MMIO_COUNT;
		/* header_len is in dwords */
		header_len_bytes = dmc_header->header_len * 4;
		start_mmioaddr = v3->start_mmioaddr;
		dmc_header_size = sizeof(*v3);
	} else if (dmc_header->header_ver == 1) {
		const struct intel_dmc_header_v1 *v1 =
			(const struct intel_dmc_header_v1 *)dmc_header;

		if (rem_size < sizeof(struct intel_dmc_header_v1))
			goto error_truncated;

		mmioaddr = v1->mmioaddr;
		mmiodata = v1->mmiodata;
		mmio_count = v1->mmio_count;
		mmio_count_max = DMC_V1_MAX_MMIO_COUNT;
		header_len_bytes = dmc_header->header_len;
		start_mmioaddr = DMC_V1_MMIO_START_RANGE;
		dmc_header_size = sizeof(*v1);
	} else {
		drm_err(display->drm, "Unknown DMC fw header version: %u\n",
			dmc_header->header_ver);
		return 0;
	}

	if (header_len_bytes != dmc_header_size) {
		drm_err(display->drm, "DMC firmware has wrong dmc header length "
			"(%u bytes)\n", header_len_bytes);
		return 0;
	}

	/* Cache the dmc header info. */
	if (mmio_count > mmio_count_max) {
		drm_err(display->drm, "DMC firmware has wrong mmio count %u\n", mmio_count);
		return 0;
	}

	if (!dmc_mmio_addr_sanity_check(dmc, mmioaddr, mmio_count,
					dmc_header->header_ver, dmc_id)) {
		drm_err(display->drm, "DMC firmware has Wrong MMIO Addresses\n");
		return 0;
	}

	drm_dbg_kms(display->drm, "DMC %d:\n", dmc_id);
	for (i = 0; i < mmio_count; i++) {
		dmc_info->mmioaddr[i] = _MMIO(mmioaddr[i]);
		dmc_info->mmiodata[i] = mmiodata[i];
	}

	for (i = 0; i < mmio_count - 1; i++) {
		u32 orig_mmiodata[2] = {
			dmc_info->mmiodata[i],
			dmc_info->mmiodata[i+1],
		};

		if (!fixup_dmc_evt(display, dmc_id,
				   dmc_info->mmioaddr[i], &dmc_info->mmiodata[i],
				   dmc_info->mmioaddr[i+1], &dmc_info->mmiodata[i+1]))
			continue;

		drm_dbg_kms(display->drm,
			    " mmio[%d]: 0x%x = 0x%x->0x%x (EVT_CTL)\n",
			    i, i915_mmio_reg_offset(dmc_info->mmioaddr[i]),
			    orig_mmiodata[0], dmc_info->mmiodata[i]);
		drm_dbg_kms(display->drm,
			    " mmio[%d]: 0x%x = 0x%x->0x%x (EVT_HTP)\n",
			    i+1, i915_mmio_reg_offset(dmc_info->mmioaddr[i+1]),
			    orig_mmiodata[1], dmc_info->mmiodata[i+1]);
	}

	for (i = 0; i < mmio_count; i++) {
		drm_dbg_kms(display->drm, " mmio[%d]: 0x%x = 0x%x%s%s\n",
			    i, i915_mmio_reg_offset(dmc_info->mmioaddr[i]), dmc_info->mmiodata[i],
			    is_dmc_evt_ctl_reg(display, dmc_id, dmc_info->mmioaddr[i]) ? " (EVT_CTL)" :
			    is_dmc_evt_htp_reg(display, dmc_id, dmc_info->mmioaddr[i]) ? " (EVT_HTP)" : "",
			    disable_dmc_evt(display, dmc_id, dmc_info->mmioaddr[i],
					    dmc_info->mmiodata[i]) ? " (disabling)" : "");
	}
	dmc_info->mmio_count = mmio_count;
	dmc_info->start_mmioaddr = start_mmioaddr;

	rem_size -= header_len_bytes;

	/* fw_size is in dwords, so multiplied by 4 to convert into bytes. */
	payload_size = dmc_header->fw_size * 4;
	if (rem_size < payload_size)
		goto error_truncated;

	if (payload_size > dmc->max_fw_size) {
		drm_err(display->drm, "DMC FW too big (%u bytes)\n", payload_size);
		return 0;
	}
	dmc_info->dmc_fw_size = dmc_header->fw_size;

	dmc_info->payload = kmalloc(payload_size, GFP_KERNEL);
	if (!dmc_info->payload)
		return 0;

	payload = (u8 *)(dmc_header) + header_len_bytes;
	memcpy(dmc_info->payload, payload, payload_size);

	return header_len_bytes + payload_size;

error_truncated:
	drm_err(display->drm, "Truncated DMC firmware, refusing.\n");
	return 0;
}

static u32
parse_dmc_fw_package(struct intel_dmc *dmc,
		     const struct intel_package_header *package_header,
		     const struct stepping_info *si,
		     size_t rem_size)
{
	struct intel_display *display = dmc->display;
	u32 package_size = sizeof(struct intel_package_header);
	u32 num_entries, max_entries;
	const struct intel_fw_info *fw_info;

	if (rem_size < package_size)
		goto error_truncated;

	if (package_header->header_ver == 1) {
		max_entries = PACKAGE_MAX_FW_INFO_ENTRIES;
	} else if (package_header->header_ver == 2) {
		max_entries = PACKAGE_V2_MAX_FW_INFO_ENTRIES;
	} else {
		drm_err(display->drm, "DMC firmware has unknown header version %u\n",
			package_header->header_ver);
		return 0;
	}

	/*
	 * We should always have space for max_entries,
	 * even if not all are used
	 */
	package_size += max_entries * sizeof(struct intel_fw_info);
	if (rem_size < package_size)
		goto error_truncated;

	if (package_header->header_len * 4 != package_size) {
		drm_err(display->drm, "DMC firmware has wrong package header length "
			"(%u bytes)\n", package_size);
		return 0;
	}

	num_entries = package_header->num_entries;
	if (WARN_ON(package_header->num_entries > max_entries))
		num_entries = max_entries;

	fw_info = (const struct intel_fw_info *)
		((u8 *)package_header + sizeof(*package_header));
	dmc_set_fw_offset(dmc, fw_info, num_entries, si,
			  package_header->header_ver);

	/* dmc_offset is in dwords */
	return package_size;

error_truncated:
	drm_err(display->drm, "Truncated DMC firmware, refusing.\n");
	return 0;
}

/* Return number of bytes parsed or 0 on error */
static u32 parse_dmc_fw_css(struct intel_dmc *dmc,
			    struct intel_css_header *css_header,
			    size_t rem_size)
{
	struct intel_display *display = dmc->display;

	if (rem_size < sizeof(struct intel_css_header)) {
		drm_err(display->drm, "Truncated DMC firmware, refusing.\n");
		return 0;
	}

	if (sizeof(struct intel_css_header) !=
	    (css_header->header_len * 4)) {
		drm_err(display->drm, "DMC firmware has wrong CSS header length "
			"(%u bytes)\n",
			(css_header->header_len * 4));
		return 0;
	}

	dmc->version = css_header->version;

	return sizeof(struct intel_css_header);
}

static int parse_dmc_fw(struct intel_dmc *dmc, const struct firmware *fw)
{
	struct intel_display *display = dmc->display;
	struct intel_css_header *css_header;
	struct intel_package_header *package_header;
	struct intel_dmc_header_base *dmc_header;
	struct stepping_info display_info = { '*', '*'};
	const struct stepping_info *si = intel_get_stepping_info(display, &display_info);
	enum intel_dmc_id dmc_id;
	u32 readcount = 0;
	u32 r, offset;

	if (!fw)
		return -EINVAL;

	/* Extract CSS Header information */
	css_header = (struct intel_css_header *)fw->data;
	r = parse_dmc_fw_css(dmc, css_header, fw->size);
	if (!r)
		return -EINVAL;

	readcount += r;

	/* Extract Package Header information */
	package_header = (struct intel_package_header *)&fw->data[readcount];
	r = parse_dmc_fw_package(dmc, package_header, si, fw->size - readcount);
	if (!r)
		return -EINVAL;

	readcount += r;

	for_each_dmc_id(dmc_id) {
		if (!dmc->dmc_info[dmc_id].present)
			continue;

		offset = readcount + dmc->dmc_info[dmc_id].dmc_offset * 4;
		if (offset > fw->size) {
			drm_err(display->drm, "Reading beyond the fw_size\n");
			continue;
		}

		dmc_header = (struct intel_dmc_header_base *)&fw->data[offset];
		parse_dmc_fw_header(dmc, dmc_header, fw->size - offset, dmc_id);
	}

	if (!intel_dmc_has_payload(display)) {
		drm_err(display->drm, "DMC firmware main program not found\n");
		return -ENOENT;
	}

	return 0;
}

static void intel_dmc_runtime_pm_get(struct intel_display *display)
{
	drm_WARN_ON(display->drm, display->dmc.wakeref);
	display->dmc.wakeref = intel_display_power_get(display, POWER_DOMAIN_INIT);
}

static void intel_dmc_runtime_pm_put(struct intel_display *display)
{
	intel_wakeref_t wakeref __maybe_unused =
		fetch_and_zero(&display->dmc.wakeref);

	intel_display_power_put(display, POWER_DOMAIN_INIT, wakeref);
}

static const char *dmc_fallback_path(struct intel_display *display)
{
	if (display->platform.alderlake_p)
		return ADLP_DMC_FALLBACK_PATH;

	return NULL;
}

static void dmc_load_work_fn(struct work_struct *work)
{
	struct intel_dmc *dmc = container_of(work, typeof(*dmc), work);
	struct intel_display *display = dmc->display;
	const struct firmware *fw = NULL;
	const char *fallback_path;
	int err;

	err = request_firmware(&fw, dmc->fw_path, display->drm->dev);

	if (err == -ENOENT && !dmc_firmware_param(display)) {
		fallback_path = dmc_fallback_path(display);
		if (fallback_path) {
			drm_dbg_kms(display->drm, "%s not found, falling back to %s\n",
				    dmc->fw_path, fallback_path);
			err = request_firmware(&fw, fallback_path, display->drm->dev);
			if (err == 0)
				dmc->fw_path = fallback_path;
		}
	}

	if (err) {
		drm_notice(display->drm,
			   "Failed to load DMC firmware %s (%pe). Disabling runtime power management.\n",
			   dmc->fw_path, ERR_PTR(err));
		drm_notice(display->drm, "DMC firmware homepage: %s",
			   INTEL_DMC_FIRMWARE_URL);
		return;
	}

	err = parse_dmc_fw(dmc, fw);
	if (err) {
		drm_notice(display->drm,
			   "Failed to parse DMC firmware %s (%pe). Disabling runtime power management.\n",
			   dmc->fw_path, ERR_PTR(err));
		goto out;
	}

	intel_dmc_load_program(display);
	intel_dmc_runtime_pm_put(display);

	drm_info(display->drm, "Finished loading DMC firmware %s (v%u.%u)\n",
		 dmc->fw_path, DMC_VERSION_MAJOR(dmc->version),
		 DMC_VERSION_MINOR(dmc->version));

out:
	release_firmware(fw);
}

/**
 * intel_dmc_init() - initialize the firmware loading.
 * @display: display instance
 *
 * This function is called at the time of loading the display driver to read
 * firmware from a .bin file and copied into a internal memory.
 */
void intel_dmc_init(struct intel_display *display)
{
	struct intel_dmc *dmc;

	if (!HAS_DMC(display))
		return;

	/*
	 * Obtain a runtime pm reference, until DMC is loaded, to avoid entering
	 * runtime-suspend.
	 *
	 * On error, we return with the rpm wakeref held to prevent runtime
	 * suspend as runtime suspend *requires* a working DMC for whatever
	 * reason.
	 */
	intel_dmc_runtime_pm_get(display);

	dmc = kzalloc(sizeof(*dmc), GFP_KERNEL);
	if (!dmc)
		return;

	dmc->display = display;

	INIT_WORK(&dmc->work, dmc_load_work_fn);

	dmc->fw_path = dmc_firmware_default(display, &dmc->max_fw_size);

	if (dmc_firmware_param_disabled(display)) {
		drm_info(display->drm, "Disabling DMC firmware and runtime PM\n");
		goto out;
	}

	if (dmc_firmware_param(display))
		dmc->fw_path = dmc_firmware_param(display);

	if (!dmc->fw_path) {
		drm_dbg_kms(display->drm,
			    "No known DMC firmware for platform, disabling runtime PM\n");
		goto out;
	}

	display->dmc.dmc = dmc;

	drm_dbg_kms(display->drm, "Loading %s\n", dmc->fw_path);
	queue_work(display->wq.unordered, &dmc->work);

	return;

out:
	kfree(dmc);
}

/**
 * intel_dmc_suspend() - prepare DMC firmware before system suspend
 * @display: display instance
 *
 * Prepare the DMC firmware before entering system suspend. This includes
 * flushing pending work items and releasing any resources acquired during
 * init.
 */
void intel_dmc_suspend(struct intel_display *display)
{
	struct intel_dmc *dmc = display_to_dmc(display);

	if (!HAS_DMC(display))
		return;

	if (dmc)
		flush_work(&dmc->work);

	/* Drop the reference held in case DMC isn't loaded. */
	if (!intel_dmc_has_payload(display))
		intel_dmc_runtime_pm_put(display);
}

void intel_dmc_wait_fw_load(struct intel_display *display)
{
	struct intel_dmc *dmc = display_to_dmc(display);

	if (!HAS_DMC(display))
		return;

	if (dmc)
		flush_work(&dmc->work);
}

/**
 * intel_dmc_resume() - init DMC firmware during system resume
 * @display: display instance
 *
 * Reinitialize the DMC firmware during system resume, reacquiring any
 * resources released in intel_dmc_suspend().
 */
void intel_dmc_resume(struct intel_display *display)
{
	if (!HAS_DMC(display))
		return;

	/*
	 * Reacquire the reference to keep RPM disabled in case DMC isn't
	 * loaded.
	 */
	if (!intel_dmc_has_payload(display))
		intel_dmc_runtime_pm_get(display);
}

/**
 * intel_dmc_fini() - unload the DMC firmware.
 * @display: display instance
 *
 * Firmmware unloading includes freeing the internal memory and reset the
 * firmware loading status.
 */
void intel_dmc_fini(struct intel_display *display)
{
	struct intel_dmc *dmc = display_to_dmc(display);
	enum intel_dmc_id dmc_id;

	if (!HAS_DMC(display))
		return;

	intel_dmc_suspend(display);
	drm_WARN_ON(display->drm, display->dmc.wakeref);

	if (dmc) {
		for_each_dmc_id(dmc_id)
			kfree(dmc->dmc_info[dmc_id].payload);

		kfree(dmc);
		display->dmc.dmc = NULL;
	}
}

struct intel_dmc_snapshot {
	bool initialized;
	bool loaded;
	u32 version;
};

struct intel_dmc_snapshot *intel_dmc_snapshot_capture(struct intel_display *display)
{
	struct intel_dmc *dmc = display_to_dmc(display);
	struct intel_dmc_snapshot *snapshot;

	if (!HAS_DMC(display))
		return NULL;

	snapshot = kzalloc(sizeof(*snapshot), GFP_ATOMIC);
	if (!snapshot)
		return NULL;

	snapshot->initialized = dmc;
	snapshot->loaded = intel_dmc_has_payload(display);
	if (dmc)
		snapshot->version = dmc->version;

	return snapshot;
}

void intel_dmc_snapshot_print(const struct intel_dmc_snapshot *snapshot, struct drm_printer *p)
{
	if (!snapshot)
		return;

	drm_printf(p, "DMC initialized: %s\n", str_yes_no(snapshot->initialized));
	drm_printf(p, "DMC loaded: %s\n", str_yes_no(snapshot->loaded));
	if (snapshot->initialized)
		drm_printf(p, "DMC fw version: %d.%d\n",
			   DMC_VERSION_MAJOR(snapshot->version),
			   DMC_VERSION_MINOR(snapshot->version));
}

void intel_dmc_update_dc6_allowed_count(struct intel_display *display,
					bool start_tracking)
{
	struct intel_dmc *dmc = display_to_dmc(display);
	u32 dc5_cur_count;

	if (DISPLAY_VER(dmc->display) < 14)
		return;

	dc5_cur_count = intel_de_read(dmc->display, DG1_DMC_DEBUG_DC5_COUNT);

	if (!start_tracking)
		dmc->dc6_allowed.count += dc5_cur_count - dmc->dc6_allowed.dc5_start;

	dmc->dc6_allowed.dc5_start = dc5_cur_count;
}

static bool intel_dmc_get_dc6_allowed_count(struct intel_display *display, u32 *count)
{
	struct i915_power_domains *power_domains = &display->power.domains;
	struct intel_dmc *dmc = display_to_dmc(display);
	bool dc6_enabled;

	if (DISPLAY_VER(display) < 14)
		return false;

	mutex_lock(&power_domains->lock);
	dc6_enabled = intel_de_read(display, DC_STATE_EN) &
		      DC_STATE_EN_UPTO_DC6;
	if (dc6_enabled)
		intel_dmc_update_dc6_allowed_count(display, false);

	*count = dmc->dc6_allowed.count;
	mutex_unlock(&power_domains->lock);

	return true;
}

static int intel_dmc_debugfs_status_show(struct seq_file *m, void *unused)
{
	struct intel_display *display = m->private;
	struct intel_dmc *dmc = display_to_dmc(display);
	struct ref_tracker *wakeref;
	i915_reg_t dc5_reg, dc6_reg = INVALID_MMIO_REG;
	u32 dc6_allowed_count;

	if (!HAS_DMC(display))
		return -ENODEV;

	wakeref = intel_display_rpm_get(display);

	seq_printf(m, "DMC initialized: %s\n", str_yes_no(dmc));
	seq_printf(m, "fw loaded: %s\n",
		   str_yes_no(intel_dmc_has_payload(display)));
	seq_printf(m, "path: %s\n", dmc ? dmc->fw_path : "N/A");
	seq_printf(m, "Pipe A fw needed: %s\n",
		   str_yes_no(DISPLAY_VER(display) >= 12));
	seq_printf(m, "Pipe A fw loaded: %s\n",
		   str_yes_no(has_dmc_id_fw(display, DMC_FW_PIPEA)));
	seq_printf(m, "Pipe B fw needed: %s\n",
		   str_yes_no(display->platform.alderlake_p ||
			      DISPLAY_VER(display) >= 14));
	seq_printf(m, "Pipe B fw loaded: %s\n",
		   str_yes_no(has_dmc_id_fw(display, DMC_FW_PIPEB)));

	if (!intel_dmc_has_payload(display))
		goto out;

	seq_printf(m, "version: %d.%d\n", DMC_VERSION_MAJOR(dmc->version),
		   DMC_VERSION_MINOR(dmc->version));

	if (DISPLAY_VER(display) >= 12) {
		i915_reg_t dc3co_reg;

		if (display->platform.dgfx || DISPLAY_VER(display) >= 14) {
			dc3co_reg = DG1_DMC_DEBUG3;
			dc5_reg = DG1_DMC_DEBUG_DC5_COUNT;
		} else {
			dc3co_reg = TGL_DMC_DEBUG3;
			dc5_reg = TGL_DMC_DEBUG_DC5_COUNT;
			dc6_reg = TGL_DMC_DEBUG_DC6_COUNT;
		}

		seq_printf(m, "DC3CO count: %d\n",
			   intel_de_read(display, dc3co_reg));
	} else {
		dc5_reg = display->platform.broxton ? BXT_DMC_DC3_DC5_COUNT :
			SKL_DMC_DC3_DC5_COUNT;
		if (!display->platform.geminilake && !display->platform.broxton)
			dc6_reg = SKL_DMC_DC5_DC6_COUNT;
	}

	seq_printf(m, "DC3 -> DC5 count: %d\n", intel_de_read(display, dc5_reg));

	if (intel_dmc_get_dc6_allowed_count(display, &dc6_allowed_count))
		seq_printf(m, "DC5 -> DC6 allowed count: %d\n",
			   dc6_allowed_count);
	else if (i915_mmio_reg_valid(dc6_reg))
		seq_printf(m, "DC5 -> DC6 count: %d\n",
			   intel_de_read(display, dc6_reg));

	seq_printf(m, "program base: 0x%08x\n",
		   intel_de_read(display, DMC_PROGRAM(dmc->dmc_info[DMC_FW_MAIN].start_mmioaddr, 0)));

out:
	seq_printf(m, "ssp base: 0x%08x\n",
		   intel_de_read(display, DMC_SSP_BASE));
	seq_printf(m, "htp: 0x%08x\n", intel_de_read(display, DMC_HTP_SKL));

	intel_display_rpm_put(display, wakeref);

	return 0;
}

DEFINE_SHOW_ATTRIBUTE(intel_dmc_debugfs_status);

void intel_dmc_debugfs_register(struct intel_display *display)
{
	debugfs_create_file("i915_dmc_info", 0444, display->drm->debugfs_root,
			    display, &intel_dmc_debugfs_status_fops);
}

void intel_pipedmc_irq_handler(struct intel_display *display, enum pipe pipe)
{
	struct intel_crtc *crtc = intel_crtc_for_pipe(display, pipe);
	u32 tmp = 0, int_vector;

	if (DISPLAY_VER(display) >= 20) {
		tmp = intel_de_read(display, PIPEDMC_INTERRUPT(pipe));
		intel_de_write(display, PIPEDMC_INTERRUPT(pipe), tmp);

		if (tmp & PIPEDMC_FLIPQ_PROG_DONE) {
			spin_lock(&display->drm->event_lock);

			if (crtc->flipq_event) {
				/*
				 * Update vblank counter/timestamp in case it
				 * hasn't been done yet for this frame.
				 */
				drm_crtc_accurate_vblank_count(&crtc->base);

				drm_crtc_send_vblank_event(&crtc->base, crtc->flipq_event);
				crtc->flipq_event = NULL;
			}

			spin_unlock(&display->drm->event_lock);
		}

		if (tmp & PIPEDMC_ATS_FAULT)
			drm_err_ratelimited(display->drm, "[CRTC:%d:%s] PIPEDMC ATS fault\n",
					    crtc->base.base.id, crtc->base.name);
		if (tmp & PIPEDMC_GTT_FAULT)
			drm_err_ratelimited(display->drm, "[CRTC:%d:%s] PIPEDMC GTT fault\n",
					    crtc->base.base.id, crtc->base.name);
		if (tmp & PIPEDMC_ERROR)
			drm_err(display->drm, "[CRTC:%d:%s]] PIPEDMC error\n",
				crtc->base.base.id, crtc->base.name);
	}

	int_vector = intel_de_read(display, PIPEDMC_STATUS(pipe)) & PIPEDMC_INT_VECTOR_MASK;
	if (tmp == 0 && int_vector != 0)
		drm_err(display->drm, "[CRTC:%d:%s]] PIPEDMC interrupt vector 0x%x\n",
			crtc->base.base.id, crtc->base.name, tmp);
}

void intel_pipedmc_enable_event(struct intel_crtc *crtc,
				enum pipedmc_event_id event)
{
	struct intel_display *display = to_intel_display(crtc);
	enum intel_dmc_id dmc_id = PIPE_TO_DMC_ID(crtc->pipe);

	dmc_configure_event(display, dmc_id, event, true);
}

void intel_pipedmc_disable_event(struct intel_crtc *crtc,
				 enum pipedmc_event_id event)
{
	struct intel_display *display = to_intel_display(crtc);
	enum intel_dmc_id dmc_id = PIPE_TO_DMC_ID(crtc->pipe);

	dmc_configure_event(display, dmc_id, event, false);
}

u32 intel_pipedmc_start_mmioaddr(struct intel_crtc *crtc)
{
	struct intel_display *display = to_intel_display(crtc);
	struct intel_dmc *dmc = display_to_dmc(display);
	enum intel_dmc_id dmc_id = PIPE_TO_DMC_ID(crtc->pipe);

	return dmc ? dmc->dmc_info[dmc_id].start_mmioaddr : 0;
}

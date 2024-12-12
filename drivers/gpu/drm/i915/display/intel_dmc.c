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

#include "i915_drv.h"
#include "i915_reg.h"
#include "intel_de.h"
#include "intel_dmc.h"
#include "intel_dmc_regs.h"
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
	struct drm_i915_private *i915;
	struct work_struct work;
	const char *fw_path;
	u32 max_fw_size; /* bytes */
	u32 version;
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
static struct intel_dmc *i915_to_dmc(struct drm_i915_private *i915)
{
	return i915->display.dmc.dmc;
}

static const char *dmc_firmware_param(struct drm_i915_private *i915)
{
	const char *p = i915->display.params.dmc_firmware_path;

	return p && *p ? p : NULL;
}

static bool dmc_firmware_param_disabled(struct drm_i915_private *i915)
{
	const char *p = dmc_firmware_param(i915);

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

static const char *dmc_firmware_default(struct drm_i915_private *i915, u32 *size)
{
	const char *fw_path = NULL;
	u32 max_fw_size = 0;

	if (DISPLAY_VER_FULL(i915) == IP_VER(20, 0)) {
		fw_path = XE2LPD_DMC_PATH;
		max_fw_size = XE2LPD_DMC_MAX_FW_SIZE;
	} else if (DISPLAY_VER_FULL(i915) == IP_VER(14, 1)) {
		fw_path = BMG_DMC_PATH;
		max_fw_size = XELPDP_DMC_MAX_FW_SIZE;
	} else if (DISPLAY_VER_FULL(i915) == IP_VER(14, 0)) {
		fw_path = MTL_DMC_PATH;
		max_fw_size = XELPDP_DMC_MAX_FW_SIZE;
	} else if (IS_DG2(i915)) {
		fw_path = DG2_DMC_PATH;
		max_fw_size = DISPLAY_VER13_DMC_MAX_FW_SIZE;
	} else if (IS_ALDERLAKE_P(i915)) {
		fw_path = ADLP_DMC_PATH;
		max_fw_size = DISPLAY_VER13_DMC_MAX_FW_SIZE;
	} else if (IS_ALDERLAKE_S(i915)) {
		fw_path = ADLS_DMC_PATH;
		max_fw_size = DISPLAY_VER12_DMC_MAX_FW_SIZE;
	} else if (IS_DG1(i915)) {
		fw_path = DG1_DMC_PATH;
		max_fw_size = DISPLAY_VER12_DMC_MAX_FW_SIZE;
	} else if (IS_ROCKETLAKE(i915)) {
		fw_path = RKL_DMC_PATH;
		max_fw_size = DISPLAY_VER12_DMC_MAX_FW_SIZE;
	} else if (IS_TIGERLAKE(i915)) {
		fw_path = TGL_DMC_PATH;
		max_fw_size = DISPLAY_VER12_DMC_MAX_FW_SIZE;
	} else if (DISPLAY_VER(i915) == 11) {
		fw_path = ICL_DMC_PATH;
		max_fw_size = ICL_DMC_MAX_FW_SIZE;
	} else if (IS_GEMINILAKE(i915)) {
		fw_path = GLK_DMC_PATH;
		max_fw_size = GLK_DMC_MAX_FW_SIZE;
	} else if (IS_KABYLAKE(i915) ||
		   IS_COFFEELAKE(i915) ||
		   IS_COMETLAKE(i915)) {
		fw_path = KBL_DMC_PATH;
		max_fw_size = KBL_DMC_MAX_FW_SIZE;
	} else if (IS_SKYLAKE(i915)) {
		fw_path = SKL_DMC_PATH;
		max_fw_size = SKL_DMC_MAX_FW_SIZE;
	} else if (IS_BROXTON(i915)) {
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

static bool has_dmc_id_fw(struct drm_i915_private *i915, enum intel_dmc_id dmc_id)
{
	struct intel_dmc *dmc = i915_to_dmc(i915);

	return dmc && dmc->dmc_info[dmc_id].payload;
}

bool intel_dmc_has_payload(struct drm_i915_private *i915)
{
	return has_dmc_id_fw(i915, DMC_FW_MAIN);
}

static const struct stepping_info *
intel_get_stepping_info(struct drm_i915_private *i915,
			struct stepping_info *si)
{
	const char *step_name = intel_step_name(INTEL_DISPLAY_STEP(i915));

	si->stepping = step_name[0];
	si->substepping = step_name[1];
	return si;
}

static void gen9_set_dc_state_debugmask(struct drm_i915_private *i915)
{
	/* The below bit doesn't need to be cleared ever afterwards */
	intel_de_rmw(i915, DC_STATE_DEBUG, 0,
		     DC_STATE_DEBUG_MASK_CORES | DC_STATE_DEBUG_MASK_MEMORY_UP);
	intel_de_posting_read(i915, DC_STATE_DEBUG);
}

static void disable_event_handler(struct drm_i915_private *i915,
				  i915_reg_t ctl_reg, i915_reg_t htp_reg)
{
	intel_de_write(i915, ctl_reg,
		       REG_FIELD_PREP(DMC_EVT_CTL_TYPE_MASK,
				      DMC_EVT_CTL_TYPE_EDGE_0_1) |
		       REG_FIELD_PREP(DMC_EVT_CTL_EVENT_ID_MASK,
				      DMC_EVT_CTL_EVENT_ID_FALSE));
	intel_de_write(i915, htp_reg, 0);
}

static void disable_all_event_handlers(struct drm_i915_private *i915)
{
	enum intel_dmc_id dmc_id;

	/* TODO: disable the event handlers on pre-GEN12 platforms as well */
	if (DISPLAY_VER(i915) < 12)
		return;

	for_each_dmc_id(dmc_id) {
		int handler;

		if (!has_dmc_id_fw(i915, dmc_id))
			continue;

		for (handler = 0; handler < DMC_EVENT_HANDLER_COUNT_GEN12; handler++)
			disable_event_handler(i915,
					      DMC_EVT_CTL(i915, dmc_id, handler),
					      DMC_EVT_HTP(i915, dmc_id, handler));
	}
}

static void adlp_pipedmc_clock_gating_wa(struct drm_i915_private *i915, bool enable)
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
			intel_de_rmw(i915, CLKGATE_DIS_PSL_EXT(pipe),
				     0, PIPEDMC_GATING_DIS);
	else
		for (pipe = PIPE_C; pipe <= PIPE_D; pipe++)
			intel_de_rmw(i915, CLKGATE_DIS_PSL_EXT(pipe),
				     PIPEDMC_GATING_DIS, 0);
}

static void mtl_pipedmc_clock_gating_wa(struct drm_i915_private *i915)
{
	/*
	 * Wa_16015201720
	 * The WA requires clock gating to be disabled all the time
	 * for pipe A and B.
	 */
	intel_de_rmw(i915, GEN9_CLKGATE_DIS_0, 0,
		     MTL_PIPEDMC_GATING_DIS_A | MTL_PIPEDMC_GATING_DIS_B);
}

static void pipedmc_clock_gating_wa(struct drm_i915_private *i915, bool enable)
{
	if (DISPLAY_VER(i915) >= 14 && enable)
		mtl_pipedmc_clock_gating_wa(i915);
	else if (DISPLAY_VER(i915) == 13)
		adlp_pipedmc_clock_gating_wa(i915, enable);
}

void intel_dmc_enable_pipe(struct drm_i915_private *i915, enum pipe pipe)
{
	enum intel_dmc_id dmc_id = PIPE_TO_DMC_ID(pipe);

	if (!is_valid_dmc_id(dmc_id) || !has_dmc_id_fw(i915, dmc_id))
		return;

	if (DISPLAY_VER(i915) >= 14)
		intel_de_rmw(i915, MTL_PIPEDMC_CONTROL, 0, PIPEDMC_ENABLE_MTL(pipe));
	else
		intel_de_rmw(i915, PIPEDMC_CONTROL(pipe), 0, PIPEDMC_ENABLE);
}

void intel_dmc_disable_pipe(struct drm_i915_private *i915, enum pipe pipe)
{
	enum intel_dmc_id dmc_id = PIPE_TO_DMC_ID(pipe);

	if (!is_valid_dmc_id(dmc_id) || !has_dmc_id_fw(i915, dmc_id))
		return;

	if (DISPLAY_VER(i915) >= 14)
		intel_de_rmw(i915, MTL_PIPEDMC_CONTROL, PIPEDMC_ENABLE_MTL(pipe), 0);
	else
		intel_de_rmw(i915, PIPEDMC_CONTROL(pipe), PIPEDMC_ENABLE, 0);
}

static bool is_dmc_evt_ctl_reg(struct drm_i915_private *i915,
			       enum intel_dmc_id dmc_id, i915_reg_t reg)
{
	u32 offset = i915_mmio_reg_offset(reg);
	u32 start = i915_mmio_reg_offset(DMC_EVT_CTL(i915, dmc_id, 0));
	u32 end = i915_mmio_reg_offset(DMC_EVT_CTL(i915, dmc_id, DMC_EVENT_HANDLER_COUNT_GEN12));

	return offset >= start && offset < end;
}

static bool is_dmc_evt_htp_reg(struct drm_i915_private *i915,
			       enum intel_dmc_id dmc_id, i915_reg_t reg)
{
	u32 offset = i915_mmio_reg_offset(reg);
	u32 start = i915_mmio_reg_offset(DMC_EVT_HTP(i915, dmc_id, 0));
	u32 end = i915_mmio_reg_offset(DMC_EVT_HTP(i915, dmc_id, DMC_EVENT_HANDLER_COUNT_GEN12));

	return offset >= start && offset < end;
}

static bool disable_dmc_evt(struct drm_i915_private *i915,
			    enum intel_dmc_id dmc_id,
			    i915_reg_t reg, u32 data)
{
	if (!is_dmc_evt_ctl_reg(i915, dmc_id, reg))
		return false;

	/* keep all pipe DMC events disabled by default */
	if (dmc_id != DMC_FW_MAIN)
		return true;

	/* also disable the flip queue event on the main DMC on TGL */
	if (IS_TIGERLAKE(i915) &&
	    REG_FIELD_GET(DMC_EVT_CTL_EVENT_ID_MASK, data) == DMC_EVT_CTL_EVENT_ID_CLK_MSEC)
		return true;

	/* also disable the HRR event on the main DMC on TGL/ADLS */
	if ((IS_TIGERLAKE(i915) || IS_ALDERLAKE_S(i915)) &&
	    REG_FIELD_GET(DMC_EVT_CTL_EVENT_ID_MASK, data) == DMC_EVT_CTL_EVENT_ID_VBLANK_A)
		return true;

	return false;
}

static u32 dmc_mmiodata(struct drm_i915_private *i915,
			struct intel_dmc *dmc,
			enum intel_dmc_id dmc_id, int i)
{
	if (disable_dmc_evt(i915, dmc_id,
			    dmc->dmc_info[dmc_id].mmioaddr[i],
			    dmc->dmc_info[dmc_id].mmiodata[i]))
		return REG_FIELD_PREP(DMC_EVT_CTL_TYPE_MASK,
				      DMC_EVT_CTL_TYPE_EDGE_0_1) |
			REG_FIELD_PREP(DMC_EVT_CTL_EVENT_ID_MASK,
				       DMC_EVT_CTL_EVENT_ID_FALSE);
	else
		return dmc->dmc_info[dmc_id].mmiodata[i];
}

/**
 * intel_dmc_load_program() - write the firmware from memory to register.
 * @i915: i915 drm device.
 *
 * DMC firmware is read from a .bin file and kept in internal memory one time.
 * Everytime display comes back from low power state this function is called to
 * copy the firmware from internal memory to registers.
 */
void intel_dmc_load_program(struct drm_i915_private *i915)
{
	struct i915_power_domains *power_domains = &i915->display.power.domains;
	struct intel_dmc *dmc = i915_to_dmc(i915);
	enum intel_dmc_id dmc_id;
	u32 i;

	if (!intel_dmc_has_payload(i915))
		return;

	pipedmc_clock_gating_wa(i915, true);

	disable_all_event_handlers(i915);

	assert_rpm_wakelock_held(&i915->runtime_pm);

	preempt_disable();

	for_each_dmc_id(dmc_id) {
		for (i = 0; i < dmc->dmc_info[dmc_id].dmc_fw_size; i++) {
			intel_de_write_fw(i915,
					  DMC_PROGRAM(dmc->dmc_info[dmc_id].start_mmioaddr, i),
					  dmc->dmc_info[dmc_id].payload[i]);
		}
	}

	preempt_enable();

	for_each_dmc_id(dmc_id) {
		for (i = 0; i < dmc->dmc_info[dmc_id].mmio_count; i++) {
			intel_de_write(i915, dmc->dmc_info[dmc_id].mmioaddr[i],
				       dmc_mmiodata(i915, dmc, dmc_id, i));
		}
	}

	power_domains->dc_state = 0;

	gen9_set_dc_state_debugmask(i915);

	pipedmc_clock_gating_wa(i915, false);
}

/**
 * intel_dmc_disable_program() - disable the firmware
 * @i915: i915 drm device
 *
 * Disable all event handlers in the firmware, making sure the firmware is
 * inactive after the display is uninitialized.
 */
void intel_dmc_disable_program(struct drm_i915_private *i915)
{
	if (!intel_dmc_has_payload(i915))
		return;

	pipedmc_clock_gating_wa(i915, true);
	disable_all_event_handlers(i915);
	pipedmc_clock_gating_wa(i915, false);

	intel_dmc_wl_disable(&i915->display);
}

void assert_dmc_loaded(struct drm_i915_private *i915)
{
	struct intel_dmc *dmc = i915_to_dmc(i915);

	drm_WARN_ONCE(&i915->drm, !dmc, "DMC not initialized\n");
	drm_WARN_ONCE(&i915->drm, dmc &&
		      !intel_de_read(i915, DMC_PROGRAM(dmc->dmc_info[DMC_FW_MAIN].start_mmioaddr, 0)),
		      "DMC program storage start is NULL\n");
	drm_WARN_ONCE(&i915->drm, !intel_de_read(i915, DMC_SSP_BASE),
		      "DMC SSP Base Not fine\n");
	drm_WARN_ONCE(&i915->drm, !intel_de_read(i915, DMC_HTP_SKL),
		      "DMC HTP Not fine\n");
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
	struct drm_i915_private *i915 = dmc->i915;
	enum intel_dmc_id dmc_id;
	unsigned int i;

	for (i = 0; i < num_entries; i++) {
		dmc_id = package_ver <= 1 ? DMC_FW_MAIN : fw_info[i].dmc_id;

		if (!is_valid_dmc_id(dmc_id)) {
			drm_dbg(&i915->drm, "Unsupported firmware id: %u\n", dmc_id);
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
	struct drm_i915_private *i915 = dmc->i915;
	u32 start_range, end_range;
	int i;

	if (header_ver == 1) {
		start_range = DMC_MMIO_START_RANGE;
		end_range = DMC_MMIO_END_RANGE;
	} else if (dmc_id == DMC_FW_MAIN) {
		start_range = TGL_MAIN_MMIO_START;
		end_range = TGL_MAIN_MMIO_END;
	} else if (DISPLAY_VER(i915) >= 13) {
		start_range = ADLP_PIPE_MMIO_START;
		end_range = ADLP_PIPE_MMIO_END;
	} else if (DISPLAY_VER(i915) >= 12) {
		start_range = TGL_PIPE_MMIO_START(dmc_id);
		end_range = TGL_PIPE_MMIO_END(dmc_id);
	} else {
		drm_warn(&i915->drm, "Unknown mmio range for sanity check");
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
	struct drm_i915_private *i915 = dmc->i915;
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
		drm_err(&i915->drm, "Unknown DMC fw header version: %u\n",
			dmc_header->header_ver);
		return 0;
	}

	if (header_len_bytes != dmc_header_size) {
		drm_err(&i915->drm, "DMC firmware has wrong dmc header length "
			"(%u bytes)\n", header_len_bytes);
		return 0;
	}

	/* Cache the dmc header info. */
	if (mmio_count > mmio_count_max) {
		drm_err(&i915->drm, "DMC firmware has wrong mmio count %u\n", mmio_count);
		return 0;
	}

	if (!dmc_mmio_addr_sanity_check(dmc, mmioaddr, mmio_count,
					dmc_header->header_ver, dmc_id)) {
		drm_err(&i915->drm, "DMC firmware has Wrong MMIO Addresses\n");
		return 0;
	}

	drm_dbg_kms(&i915->drm, "DMC %d:\n", dmc_id);
	for (i = 0; i < mmio_count; i++) {
		dmc_info->mmioaddr[i] = _MMIO(mmioaddr[i]);
		dmc_info->mmiodata[i] = mmiodata[i];

		drm_dbg_kms(&i915->drm, " mmio[%d]: 0x%x = 0x%x%s%s\n",
			    i, mmioaddr[i], mmiodata[i],
			    is_dmc_evt_ctl_reg(i915, dmc_id, dmc_info->mmioaddr[i]) ? " (EVT_CTL)" :
			    is_dmc_evt_htp_reg(i915, dmc_id, dmc_info->mmioaddr[i]) ? " (EVT_HTP)" : "",
			    disable_dmc_evt(i915, dmc_id, dmc_info->mmioaddr[i],
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
		drm_err(&i915->drm, "DMC FW too big (%u bytes)\n", payload_size);
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
	drm_err(&i915->drm, "Truncated DMC firmware, refusing.\n");
	return 0;
}

static u32
parse_dmc_fw_package(struct intel_dmc *dmc,
		     const struct intel_package_header *package_header,
		     const struct stepping_info *si,
		     size_t rem_size)
{
	struct drm_i915_private *i915 = dmc->i915;
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
		drm_err(&i915->drm, "DMC firmware has unknown header version %u\n",
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
		drm_err(&i915->drm, "DMC firmware has wrong package header length "
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
	drm_err(&i915->drm, "Truncated DMC firmware, refusing.\n");
	return 0;
}

/* Return number of bytes parsed or 0 on error */
static u32 parse_dmc_fw_css(struct intel_dmc *dmc,
			    struct intel_css_header *css_header,
			    size_t rem_size)
{
	struct drm_i915_private *i915 = dmc->i915;

	if (rem_size < sizeof(struct intel_css_header)) {
		drm_err(&i915->drm, "Truncated DMC firmware, refusing.\n");
		return 0;
	}

	if (sizeof(struct intel_css_header) !=
	    (css_header->header_len * 4)) {
		drm_err(&i915->drm, "DMC firmware has wrong CSS header length "
			"(%u bytes)\n",
			(css_header->header_len * 4));
		return 0;
	}

	dmc->version = css_header->version;

	return sizeof(struct intel_css_header);
}

static int parse_dmc_fw(struct intel_dmc *dmc, const struct firmware *fw)
{
	struct drm_i915_private *i915 = dmc->i915;
	struct intel_css_header *css_header;
	struct intel_package_header *package_header;
	struct intel_dmc_header_base *dmc_header;
	struct stepping_info display_info = { '*', '*'};
	const struct stepping_info *si = intel_get_stepping_info(i915, &display_info);
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
			drm_err(&i915->drm, "Reading beyond the fw_size\n");
			continue;
		}

		dmc_header = (struct intel_dmc_header_base *)&fw->data[offset];
		parse_dmc_fw_header(dmc, dmc_header, fw->size - offset, dmc_id);
	}

	if (!intel_dmc_has_payload(i915)) {
		drm_err(&i915->drm, "DMC firmware main program not found\n");
		return -ENOENT;
	}

	return 0;
}

static void intel_dmc_runtime_pm_get(struct drm_i915_private *i915)
{
	drm_WARN_ON(&i915->drm, i915->display.dmc.wakeref);
	i915->display.dmc.wakeref = intel_display_power_get(i915, POWER_DOMAIN_INIT);
}

static void intel_dmc_runtime_pm_put(struct drm_i915_private *i915)
{
	intel_wakeref_t wakeref __maybe_unused =
		fetch_and_zero(&i915->display.dmc.wakeref);

	intel_display_power_put(i915, POWER_DOMAIN_INIT, wakeref);
}

static const char *dmc_fallback_path(struct drm_i915_private *i915)
{
	if (IS_ALDERLAKE_P(i915))
		return ADLP_DMC_FALLBACK_PATH;

	return NULL;
}

static void dmc_load_work_fn(struct work_struct *work)
{
	struct intel_dmc *dmc = container_of(work, typeof(*dmc), work);
	struct drm_i915_private *i915 = dmc->i915;
	const struct firmware *fw = NULL;
	const char *fallback_path;
	int err;

	err = request_firmware(&fw, dmc->fw_path, i915->drm.dev);

	if (err == -ENOENT && !dmc_firmware_param(i915)) {
		fallback_path = dmc_fallback_path(i915);
		if (fallback_path) {
			drm_dbg_kms(&i915->drm, "%s not found, falling back to %s\n",
				    dmc->fw_path, fallback_path);
			err = request_firmware(&fw, fallback_path, i915->drm.dev);
			if (err == 0)
				dmc->fw_path = fallback_path;
		}
	}

	if (err) {
		drm_notice(&i915->drm,
			   "Failed to load DMC firmware %s (%pe). Disabling runtime power management.\n",
			   dmc->fw_path, ERR_PTR(err));
		drm_notice(&i915->drm, "DMC firmware homepage: %s",
			   INTEL_DMC_FIRMWARE_URL);
		return;
	}

	err = parse_dmc_fw(dmc, fw);
	if (err) {
		drm_notice(&i915->drm,
			   "Failed to parse DMC firmware %s (%pe). Disabling runtime power management.\n",
			   dmc->fw_path, ERR_PTR(err));
		goto out;
	}

	intel_dmc_load_program(i915);
	intel_dmc_runtime_pm_put(i915);

	drm_info(&i915->drm, "Finished loading DMC firmware %s (v%u.%u)\n",
		 dmc->fw_path, DMC_VERSION_MAJOR(dmc->version),
		 DMC_VERSION_MINOR(dmc->version));

out:
	release_firmware(fw);
}

/**
 * intel_dmc_init() - initialize the firmware loading.
 * @i915: i915 drm device.
 *
 * This function is called at the time of loading the display driver to read
 * firmware from a .bin file and copied into a internal memory.
 */
void intel_dmc_init(struct drm_i915_private *i915)
{
	struct intel_dmc *dmc;

	if (!HAS_DMC(i915))
		return;

	/*
	 * Obtain a runtime pm reference, until DMC is loaded, to avoid entering
	 * runtime-suspend.
	 *
	 * On error, we return with the rpm wakeref held to prevent runtime
	 * suspend as runtime suspend *requires* a working DMC for whatever
	 * reason.
	 */
	intel_dmc_runtime_pm_get(i915);

	dmc = kzalloc(sizeof(*dmc), GFP_KERNEL);
	if (!dmc)
		return;

	dmc->i915 = i915;

	INIT_WORK(&dmc->work, dmc_load_work_fn);

	dmc->fw_path = dmc_firmware_default(i915, &dmc->max_fw_size);

	if (dmc_firmware_param_disabled(i915)) {
		drm_info(&i915->drm, "Disabling DMC firmware and runtime PM\n");
		goto out;
	}

	if (dmc_firmware_param(i915))
		dmc->fw_path = dmc_firmware_param(i915);

	if (!dmc->fw_path) {
		drm_dbg_kms(&i915->drm,
			    "No known DMC firmware for platform, disabling runtime PM\n");
		goto out;
	}

	i915->display.dmc.dmc = dmc;

	drm_dbg_kms(&i915->drm, "Loading %s\n", dmc->fw_path);
	queue_work(i915->unordered_wq, &dmc->work);

	return;

out:
	kfree(dmc);
}

/**
 * intel_dmc_suspend() - prepare DMC firmware before system suspend
 * @i915: i915 drm device
 *
 * Prepare the DMC firmware before entering system suspend. This includes
 * flushing pending work items and releasing any resources acquired during
 * init.
 */
void intel_dmc_suspend(struct drm_i915_private *i915)
{
	struct intel_dmc *dmc = i915_to_dmc(i915);

	if (!HAS_DMC(i915))
		return;

	if (dmc)
		flush_work(&dmc->work);

	intel_dmc_wl_disable(&i915->display);

	/* Drop the reference held in case DMC isn't loaded. */
	if (!intel_dmc_has_payload(i915))
		intel_dmc_runtime_pm_put(i915);
}

/**
 * intel_dmc_resume() - init DMC firmware during system resume
 * @i915: i915 drm device
 *
 * Reinitialize the DMC firmware during system resume, reacquiring any
 * resources released in intel_dmc_suspend().
 */
void intel_dmc_resume(struct drm_i915_private *i915)
{
	if (!HAS_DMC(i915))
		return;

	/*
	 * Reacquire the reference to keep RPM disabled in case DMC isn't
	 * loaded.
	 */
	if (!intel_dmc_has_payload(i915))
		intel_dmc_runtime_pm_get(i915);
}

/**
 * intel_dmc_fini() - unload the DMC firmware.
 * @i915: i915 drm device.
 *
 * Firmmware unloading includes freeing the internal memory and reset the
 * firmware loading status.
 */
void intel_dmc_fini(struct drm_i915_private *i915)
{
	struct intel_dmc *dmc = i915_to_dmc(i915);
	enum intel_dmc_id dmc_id;

	if (!HAS_DMC(i915))
		return;

	intel_dmc_suspend(i915);
	drm_WARN_ON(&i915->drm, i915->display.dmc.wakeref);

	if (dmc) {
		for_each_dmc_id(dmc_id)
			kfree(dmc->dmc_info[dmc_id].payload);

		kfree(dmc);
		i915->display.dmc.dmc = NULL;
	}
}

void intel_dmc_print_error_state(struct drm_printer *p,
				 struct drm_i915_private *i915)
{
	struct intel_dmc *dmc = i915_to_dmc(i915);

	if (!HAS_DMC(i915))
		return;

	drm_printf(p, "DMC initialized: %s\n", str_yes_no(dmc));
	drm_printf(p, "DMC loaded: %s\n",
		   str_yes_no(intel_dmc_has_payload(i915)));
	if (dmc)
		drm_printf(p, "DMC fw version: %d.%d\n",
			   DMC_VERSION_MAJOR(dmc->version),
			   DMC_VERSION_MINOR(dmc->version));
}

static int intel_dmc_debugfs_status_show(struct seq_file *m, void *unused)
{
	struct drm_i915_private *i915 = m->private;
	struct intel_dmc *dmc = i915_to_dmc(i915);
	intel_wakeref_t wakeref;
	i915_reg_t dc5_reg, dc6_reg = INVALID_MMIO_REG;

	if (!HAS_DMC(i915))
		return -ENODEV;

	wakeref = intel_runtime_pm_get(&i915->runtime_pm);

	seq_printf(m, "DMC initialized: %s\n", str_yes_no(dmc));
	seq_printf(m, "fw loaded: %s\n",
		   str_yes_no(intel_dmc_has_payload(i915)));
	seq_printf(m, "path: %s\n", dmc ? dmc->fw_path : "N/A");
	seq_printf(m, "Pipe A fw needed: %s\n",
		   str_yes_no(DISPLAY_VER(i915) >= 12));
	seq_printf(m, "Pipe A fw loaded: %s\n",
		   str_yes_no(has_dmc_id_fw(i915, DMC_FW_PIPEA)));
	seq_printf(m, "Pipe B fw needed: %s\n",
		   str_yes_no(IS_ALDERLAKE_P(i915) ||
			      DISPLAY_VER(i915) >= 14));
	seq_printf(m, "Pipe B fw loaded: %s\n",
		   str_yes_no(has_dmc_id_fw(i915, DMC_FW_PIPEB)));

	if (!intel_dmc_has_payload(i915))
		goto out;

	seq_printf(m, "version: %d.%d\n", DMC_VERSION_MAJOR(dmc->version),
		   DMC_VERSION_MINOR(dmc->version));

	if (DISPLAY_VER(i915) >= 12) {
		i915_reg_t dc3co_reg;

		if (IS_DGFX(i915) || DISPLAY_VER(i915) >= 14) {
			dc3co_reg = DG1_DMC_DEBUG3;
			dc5_reg = DG1_DMC_DEBUG_DC5_COUNT;
		} else {
			dc3co_reg = TGL_DMC_DEBUG3;
			dc5_reg = TGL_DMC_DEBUG_DC5_COUNT;
			dc6_reg = TGL_DMC_DEBUG_DC6_COUNT;
		}

		seq_printf(m, "DC3CO count: %d\n",
			   intel_de_read(i915, dc3co_reg));
	} else {
		dc5_reg = IS_BROXTON(i915) ? BXT_DMC_DC3_DC5_COUNT :
			SKL_DMC_DC3_DC5_COUNT;
		if (!IS_GEMINILAKE(i915) && !IS_BROXTON(i915))
			dc6_reg = SKL_DMC_DC5_DC6_COUNT;
	}

	seq_printf(m, "DC3 -> DC5 count: %d\n", intel_de_read(i915, dc5_reg));
	if (i915_mmio_reg_valid(dc6_reg))
		seq_printf(m, "DC5 -> DC6 count: %d\n",
			   intel_de_read(i915, dc6_reg));

	seq_printf(m, "program base: 0x%08x\n",
		   intel_de_read(i915, DMC_PROGRAM(dmc->dmc_info[DMC_FW_MAIN].start_mmioaddr, 0)));

out:
	seq_printf(m, "ssp base: 0x%08x\n",
		   intel_de_read(i915, DMC_SSP_BASE));
	seq_printf(m, "htp: 0x%08x\n", intel_de_read(i915, DMC_HTP_SKL));

	intel_runtime_pm_put(&i915->runtime_pm, wakeref);

	return 0;
}

DEFINE_SHOW_ATTRIBUTE(intel_dmc_debugfs_status);

void intel_dmc_debugfs_register(struct drm_i915_private *i915)
{
	struct drm_minor *minor = i915->drm.primary;

	debugfs_create_file("i915_dmc_info", 0444, minor->debugfs_root,
			    i915, &intel_dmc_debugfs_status_fops);
}

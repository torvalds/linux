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

#include <linux/firmware.h>

#include "i915_drv.h"
#include "i915_reg.h"
#include "intel_de.h"
#include "intel_dmc.h"

/**
 * DOC: DMC Firmware Support
 *
 * From gen9 onwards we have newly added DMC (Display microcontroller) in display
 * engine to save and restore the state of display engine when it enter into
 * low-power state and comes back to normal.
 */

#define DMC_PATH(platform, major, minor) \
	"i915/"				 \
	__stringify(platform) "_dmc_ver" \
	__stringify(major) "_"		 \
	__stringify(minor) ".bin"

#define GEN12_DMC_MAX_FW_SIZE		ICL_DMC_MAX_FW_SIZE

#define ADLP_DMC_PATH			DMC_PATH(adlp, 2, 10)
#define ADLP_DMC_VERSION_REQUIRED	DMC_VERSION(2, 10)
MODULE_FIRMWARE(ADLP_DMC_PATH);

#define ADLS_DMC_PATH			DMC_PATH(adls, 2, 01)
#define ADLS_DMC_VERSION_REQUIRED	DMC_VERSION(2, 1)
MODULE_FIRMWARE(ADLS_DMC_PATH);

#define DG1_DMC_PATH			DMC_PATH(dg1, 2, 02)
#define DG1_DMC_VERSION_REQUIRED	DMC_VERSION(2, 2)
MODULE_FIRMWARE(DG1_DMC_PATH);

#define RKL_DMC_PATH			DMC_PATH(rkl, 2, 02)
#define RKL_DMC_VERSION_REQUIRED	DMC_VERSION(2, 2)
MODULE_FIRMWARE(RKL_DMC_PATH);

#define TGL_DMC_PATH			DMC_PATH(tgl, 2, 08)
#define TGL_DMC_VERSION_REQUIRED	DMC_VERSION(2, 8)
MODULE_FIRMWARE(TGL_DMC_PATH);

#define ICL_DMC_PATH			DMC_PATH(icl, 1, 09)
#define ICL_DMC_VERSION_REQUIRED	DMC_VERSION(1, 9)
#define ICL_DMC_MAX_FW_SIZE		0x6000
MODULE_FIRMWARE(ICL_DMC_PATH);

#define CNL_DMC_PATH			DMC_PATH(cnl, 1, 07)
#define CNL_DMC_VERSION_REQUIRED	DMC_VERSION(1, 7)
#define CNL_DMC_MAX_FW_SIZE		GLK_DMC_MAX_FW_SIZE
MODULE_FIRMWARE(CNL_DMC_PATH);

#define GLK_DMC_PATH			DMC_PATH(glk, 1, 04)
#define GLK_DMC_VERSION_REQUIRED	DMC_VERSION(1, 4)
#define GLK_DMC_MAX_FW_SIZE		0x4000
MODULE_FIRMWARE(GLK_DMC_PATH);

#define KBL_DMC_PATH			DMC_PATH(kbl, 1, 04)
#define KBL_DMC_VERSION_REQUIRED	DMC_VERSION(1, 4)
#define KBL_DMC_MAX_FW_SIZE		BXT_DMC_MAX_FW_SIZE
MODULE_FIRMWARE(KBL_DMC_PATH);

#define SKL_DMC_PATH			DMC_PATH(skl, 1, 27)
#define SKL_DMC_VERSION_REQUIRED	DMC_VERSION(1, 27)
#define SKL_DMC_MAX_FW_SIZE		BXT_DMC_MAX_FW_SIZE
MODULE_FIRMWARE(SKL_DMC_PATH);

#define BXT_DMC_PATH			DMC_PATH(bxt, 1, 07)
#define BXT_DMC_VERSION_REQUIRED	DMC_VERSION(1, 7)
#define BXT_DMC_MAX_FW_SIZE		0x3000
MODULE_FIRMWARE(BXT_DMC_PATH);

#define DMC_DEFAULT_FW_OFFSET		0xFFFFFFFF
#define PACKAGE_MAX_FW_INFO_ENTRIES	20
#define PACKAGE_V2_MAX_FW_INFO_ENTRIES	32
#define DMC_V1_MAX_MMIO_COUNT		8
#define DMC_V3_MAX_MMIO_COUNT		20
#define DMC_V1_MMIO_START_RANGE		0x80000

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

bool intel_dmc_has_payload(struct drm_i915_private *i915)
{
	return i915->dmc.dmc_info[DMC_FW_MAIN].payload;
}

static const struct stepping_info skl_stepping_info[] = {
	{'A', '0'}, {'B', '0'}, {'C', '0'},
	{'D', '0'}, {'E', '0'}, {'F', '0'},
	{'G', '0'}, {'H', '0'}, {'I', '0'},
	{'J', '0'}, {'K', '0'}
};

static const struct stepping_info bxt_stepping_info[] = {
	{'A', '0'}, {'A', '1'}, {'A', '2'},
	{'B', '0'}, {'B', '1'}, {'B', '2'}
};

static const struct stepping_info icl_stepping_info[] = {
	{'A', '0'}, {'A', '1'}, {'A', '2'},
	{'B', '0'}, {'B', '2'},
	{'C', '0'}
};

static const struct stepping_info no_stepping_info = { '*', '*' };

static const struct stepping_info *
intel_get_stepping_info(struct drm_i915_private *dev_priv)
{
	const struct stepping_info *si;
	unsigned int size;

	if (IS_ICELAKE(dev_priv)) {
		size = ARRAY_SIZE(icl_stepping_info);
		si = icl_stepping_info;
	} else if (IS_SKYLAKE(dev_priv)) {
		size = ARRAY_SIZE(skl_stepping_info);
		si = skl_stepping_info;
	} else if (IS_BROXTON(dev_priv)) {
		size = ARRAY_SIZE(bxt_stepping_info);
		si = bxt_stepping_info;
	} else {
		size = 0;
		si = NULL;
	}

	if (INTEL_REVID(dev_priv) < size)
		return si + INTEL_REVID(dev_priv);

	return &no_stepping_info;
}

static void gen9_set_dc_state_debugmask(struct drm_i915_private *dev_priv)
{
	u32 val, mask;

	mask = DC_STATE_DEBUG_MASK_MEMORY_UP;

	if (IS_GEMINILAKE(dev_priv) || IS_BROXTON(dev_priv))
		mask |= DC_STATE_DEBUG_MASK_CORES;

	/* The below bit doesn't need to be cleared ever afterwards */
	val = intel_de_read(dev_priv, DC_STATE_DEBUG);
	if ((val & mask) != mask) {
		val |= mask;
		intel_de_write(dev_priv, DC_STATE_DEBUG, val);
		intel_de_posting_read(dev_priv, DC_STATE_DEBUG);
	}
}

/**
 * intel_dmc_load_program() - write the firmware from memory to register.
 * @dev_priv: i915 drm device.
 *
 * DMC firmware is read from a .bin file and kept in internal memory one time.
 * Everytime display comes back from low power state this function is called to
 * copy the firmware from internal memory to registers.
 */
void intel_dmc_load_program(struct drm_i915_private *dev_priv)
{
	struct intel_dmc *dmc = &dev_priv->dmc;
	u32 id, i;

	if (!HAS_DMC(dev_priv)) {
		drm_err(&dev_priv->drm,
			"No DMC support available for this platform\n");
		return;
	}

	if (!dev_priv->dmc.dmc_info[DMC_FW_MAIN].payload) {
		drm_err(&dev_priv->drm,
			"Tried to program CSR with empty payload\n");
		return;
	}

	assert_rpm_wakelock_held(&dev_priv->runtime_pm);

	preempt_disable();

	for (id = 0; id < DMC_FW_MAX; id++) {
		for (i = 0; i < dmc->dmc_info[id].dmc_fw_size; i++) {
			intel_uncore_write_fw(&dev_priv->uncore,
					      DMC_PROGRAM(dmc->dmc_info[id].start_mmioaddr, i),
					      dmc->dmc_info[id].payload[i]);
		}
	}

	preempt_enable();

	for (id = 0; id < DMC_FW_MAX; id++) {
		for (i = 0; i < dmc->dmc_info[id].mmio_count; i++) {
			intel_de_write(dev_priv, dmc->dmc_info[id].mmioaddr[i],
				       dmc->dmc_info[id].mmiodata[i]);
		}
	}

	dev_priv->dmc.dc_state = 0;

	gen9_set_dc_state_debugmask(dev_priv);
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
	unsigned int i, id;

	struct drm_i915_private *i915 = container_of(dmc, typeof(*i915), dmc);

	for (i = 0; i < num_entries; i++) {
		id = package_ver <= 1 ? DMC_FW_MAIN : fw_info[i].dmc_id;

		if (id >= DMC_FW_MAX) {
			drm_dbg(&i915->drm, "Unsupported firmware id: %u\n", id);
			continue;
		}

		/* More specific versions come first, so we don't even have to
		 * check for the stepping since we already found a previous FW
		 * for this id.
		 */
		if (dmc->dmc_info[id].present)
			continue;

		if (fw_info_matches_stepping(&fw_info[i], si)) {
			dmc->dmc_info[id].present = true;
			dmc->dmc_info[id].dmc_offset = fw_info[i].offset;
		}
	}
}

static u32 parse_dmc_fw_header(struct intel_dmc *dmc,
			       const struct intel_dmc_header_base *dmc_header,
			       size_t rem_size, u8 dmc_id)
{
	struct drm_i915_private *i915 = container_of(dmc, typeof(*i915), dmc);
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

	for (i = 0; i < mmio_count; i++) {
		dmc_info->mmioaddr[i] = _MMIO(mmioaddr[i]);
		dmc_info->mmiodata[i] = mmiodata[i];
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
	struct drm_i915_private *i915 = container_of(dmc, typeof(*i915), dmc);
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
	struct drm_i915_private *i915 = container_of(dmc, typeof(*i915), dmc);

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

	if (dmc->required_version &&
	    css_header->version != dmc->required_version) {
		drm_info(&i915->drm, "Refusing to load DMC firmware v%u.%u,"
			 " please use v%u.%u\n",
			 DMC_VERSION_MAJOR(css_header->version),
			 DMC_VERSION_MINOR(css_header->version),
			 DMC_VERSION_MAJOR(dmc->required_version),
			 DMC_VERSION_MINOR(dmc->required_version));
		return 0;
	}

	dmc->version = css_header->version;

	return sizeof(struct intel_css_header);
}

static void parse_dmc_fw(struct drm_i915_private *dev_priv,
			 const struct firmware *fw)
{
	struct intel_css_header *css_header;
	struct intel_package_header *package_header;
	struct intel_dmc_header_base *dmc_header;
	struct intel_dmc *dmc = &dev_priv->dmc;
	const struct stepping_info *si = intel_get_stepping_info(dev_priv);
	u32 readcount = 0;
	u32 r, offset;
	int id;

	if (!fw)
		return;

	/* Extract CSS Header information */
	css_header = (struct intel_css_header *)fw->data;
	r = parse_dmc_fw_css(dmc, css_header, fw->size);
	if (!r)
		return;

	readcount += r;

	/* Extract Package Header information */
	package_header = (struct intel_package_header *)&fw->data[readcount];
	r = parse_dmc_fw_package(dmc, package_header, si, fw->size - readcount);
	if (!r)
		return;

	readcount += r;

	for (id = 0; id < DMC_FW_MAX; id++) {
		if (!dev_priv->dmc.dmc_info[id].present)
			continue;

		offset = readcount + dmc->dmc_info[id].dmc_offset * 4;
		if (fw->size - offset < 0) {
			drm_err(&dev_priv->drm, "Reading beyond the fw_size\n");
			continue;
		}

		dmc_header = (struct intel_dmc_header_base *)&fw->data[offset];
		parse_dmc_fw_header(dmc, dmc_header, fw->size - offset, id);
	}
}

static void intel_dmc_runtime_pm_get(struct drm_i915_private *dev_priv)
{
	drm_WARN_ON(&dev_priv->drm, dev_priv->dmc.wakeref);
	dev_priv->dmc.wakeref =
		intel_display_power_get(dev_priv, POWER_DOMAIN_INIT);
}

static void intel_dmc_runtime_pm_put(struct drm_i915_private *dev_priv)
{
	intel_wakeref_t wakeref __maybe_unused =
		fetch_and_zero(&dev_priv->dmc.wakeref);

	intel_display_power_put(dev_priv, POWER_DOMAIN_INIT, wakeref);
}

static void dmc_load_work_fn(struct work_struct *work)
{
	struct drm_i915_private *dev_priv;
	struct intel_dmc *dmc;
	const struct firmware *fw = NULL;

	dev_priv = container_of(work, typeof(*dev_priv), dmc.work);
	dmc = &dev_priv->dmc;

	request_firmware(&fw, dev_priv->dmc.fw_path, dev_priv->drm.dev);
	parse_dmc_fw(dev_priv, fw);

	if (intel_dmc_has_payload(dev_priv)) {
		intel_dmc_load_program(dev_priv);
		intel_dmc_runtime_pm_put(dev_priv);

		drm_info(&dev_priv->drm,
			 "Finished loading DMC firmware %s (v%u.%u)\n",
			 dev_priv->dmc.fw_path, DMC_VERSION_MAJOR(dmc->version),
			 DMC_VERSION_MINOR(dmc->version));
	} else {
		drm_notice(&dev_priv->drm,
			   "Failed to load DMC firmware %s."
			   " Disabling runtime power management.\n",
			   dmc->fw_path);
		drm_notice(&dev_priv->drm, "DMC firmware homepage: %s",
			   INTEL_UC_FIRMWARE_URL);
	}

	release_firmware(fw);
}

/**
 * intel_dmc_ucode_init() - initialize the firmware loading.
 * @dev_priv: i915 drm device.
 *
 * This function is called at the time of loading the display driver to read
 * firmware from a .bin file and copied into a internal memory.
 */
void intel_dmc_ucode_init(struct drm_i915_private *dev_priv)
{
	struct intel_dmc *dmc = &dev_priv->dmc;

	INIT_WORK(&dev_priv->dmc.work, dmc_load_work_fn);

	if (!HAS_DMC(dev_priv))
		return;

	/*
	 * Obtain a runtime pm reference, until DMC is loaded, to avoid entering
	 * runtime-suspend.
	 *
	 * On error, we return with the rpm wakeref held to prevent runtime
	 * suspend as runtime suspend *requires* a working DMC for whatever
	 * reason.
	 */
	intel_dmc_runtime_pm_get(dev_priv);

	if (IS_ALDERLAKE_P(dev_priv)) {
		dmc->fw_path = ADLP_DMC_PATH;
		dmc->required_version = ADLP_DMC_VERSION_REQUIRED;
		dmc->max_fw_size = GEN12_DMC_MAX_FW_SIZE;
	} else if (IS_ALDERLAKE_S(dev_priv)) {
		dmc->fw_path = ADLS_DMC_PATH;
		dmc->required_version = ADLS_DMC_VERSION_REQUIRED;
		dmc->max_fw_size = GEN12_DMC_MAX_FW_SIZE;
	} else if (IS_DG1(dev_priv)) {
		dmc->fw_path = DG1_DMC_PATH;
		dmc->required_version = DG1_DMC_VERSION_REQUIRED;
		dmc->max_fw_size = GEN12_DMC_MAX_FW_SIZE;
	} else if (IS_ROCKETLAKE(dev_priv)) {
		dmc->fw_path = RKL_DMC_PATH;
		dmc->required_version = RKL_DMC_VERSION_REQUIRED;
		dmc->max_fw_size = GEN12_DMC_MAX_FW_SIZE;
	} else if (DISPLAY_VER(dev_priv) >= 12) {
		dmc->fw_path = TGL_DMC_PATH;
		dmc->required_version = TGL_DMC_VERSION_REQUIRED;
		dmc->max_fw_size = GEN12_DMC_MAX_FW_SIZE;
	} else if (DISPLAY_VER(dev_priv) == 11) {
		dmc->fw_path = ICL_DMC_PATH;
		dmc->required_version = ICL_DMC_VERSION_REQUIRED;
		dmc->max_fw_size = ICL_DMC_MAX_FW_SIZE;
	} else if (IS_CANNONLAKE(dev_priv)) {
		dmc->fw_path = CNL_DMC_PATH;
		dmc->required_version = CNL_DMC_VERSION_REQUIRED;
		dmc->max_fw_size = CNL_DMC_MAX_FW_SIZE;
	} else if (IS_GEMINILAKE(dev_priv)) {
		dmc->fw_path = GLK_DMC_PATH;
		dmc->required_version = GLK_DMC_VERSION_REQUIRED;
		dmc->max_fw_size = GLK_DMC_MAX_FW_SIZE;
	} else if (IS_KABYLAKE(dev_priv) ||
		   IS_COFFEELAKE(dev_priv) ||
		   IS_COMETLAKE(dev_priv)) {
		dmc->fw_path = KBL_DMC_PATH;
		dmc->required_version = KBL_DMC_VERSION_REQUIRED;
		dmc->max_fw_size = KBL_DMC_MAX_FW_SIZE;
	} else if (IS_SKYLAKE(dev_priv)) {
		dmc->fw_path = SKL_DMC_PATH;
		dmc->required_version = SKL_DMC_VERSION_REQUIRED;
		dmc->max_fw_size = SKL_DMC_MAX_FW_SIZE;
	} else if (IS_BROXTON(dev_priv)) {
		dmc->fw_path = BXT_DMC_PATH;
		dmc->required_version = BXT_DMC_VERSION_REQUIRED;
		dmc->max_fw_size = BXT_DMC_MAX_FW_SIZE;
	}

	if (dev_priv->params.dmc_firmware_path) {
		if (strlen(dev_priv->params.dmc_firmware_path) == 0) {
			dmc->fw_path = NULL;
			drm_info(&dev_priv->drm,
				 "Disabling DMC firmware and runtime PM\n");
			return;
		}

		dmc->fw_path = dev_priv->params.dmc_firmware_path;
		/* Bypass version check for firmware override. */
		dmc->required_version = 0;
	}

	if (!dmc->fw_path) {
		drm_dbg_kms(&dev_priv->drm,
			    "No known DMC firmware for platform, disabling runtime PM\n");
		return;
	}

	drm_dbg_kms(&dev_priv->drm, "Loading %s\n", dmc->fw_path);
	schedule_work(&dev_priv->dmc.work);
}

/**
 * intel_dmc_ucode_suspend() - prepare DMC firmware before system suspend
 * @dev_priv: i915 drm device
 *
 * Prepare the DMC firmware before entering system suspend. This includes
 * flushing pending work items and releasing any resources acquired during
 * init.
 */
void intel_dmc_ucode_suspend(struct drm_i915_private *dev_priv)
{
	if (!HAS_DMC(dev_priv))
		return;

	flush_work(&dev_priv->dmc.work);

	/* Drop the reference held in case DMC isn't loaded. */
	if (!intel_dmc_has_payload(dev_priv))
		intel_dmc_runtime_pm_put(dev_priv);
}

/**
 * intel_dmc_ucode_resume() - init DMC firmware during system resume
 * @dev_priv: i915 drm device
 *
 * Reinitialize the DMC firmware during system resume, reacquiring any
 * resources released in intel_dmc_ucode_suspend().
 */
void intel_dmc_ucode_resume(struct drm_i915_private *dev_priv)
{
	if (!HAS_DMC(dev_priv))
		return;

	/*
	 * Reacquire the reference to keep RPM disabled in case DMC isn't
	 * loaded.
	 */
	if (!intel_dmc_has_payload(dev_priv))
		intel_dmc_runtime_pm_get(dev_priv);
}

/**
 * intel_dmc_ucode_fini() - unload the DMC firmware.
 * @dev_priv: i915 drm device.
 *
 * Firmmware unloading includes freeing the internal memory and reset the
 * firmware loading status.
 */
void intel_dmc_ucode_fini(struct drm_i915_private *dev_priv)
{
	if (!HAS_DMC(dev_priv))
		return;

	intel_dmc_ucode_suspend(dev_priv);
	drm_WARN_ON(&dev_priv->drm, dev_priv->dmc.wakeref);

	kfree(dev_priv->dmc.dmc_info[DMC_FW_MAIN].payload);
}

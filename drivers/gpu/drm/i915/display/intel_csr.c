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
#include "intel_csr.h"
#include "intel_de.h"

/**
 * DOC: csr support for dmc
 *
 * Display Context Save and Restore (CSR) firmware support added from gen9
 * onwards to drive newly added DMC (Display microcontroller) in display
 * engine to save and restore the state of display engine when it enter into
 * low-power state and comes back to normal.
 */

#define GEN12_CSR_MAX_FW_SIZE		ICL_CSR_MAX_FW_SIZE

#define TGL_CSR_PATH			"i915/tgl_dmc_ver2_04.bin"
#define TGL_CSR_VERSION_REQUIRED	CSR_VERSION(2, 4)
#define TGL_CSR_MAX_FW_SIZE		0x6000
MODULE_FIRMWARE(TGL_CSR_PATH);

#define ICL_CSR_PATH			"i915/icl_dmc_ver1_09.bin"
#define ICL_CSR_VERSION_REQUIRED	CSR_VERSION(1, 9)
#define ICL_CSR_MAX_FW_SIZE		0x6000
MODULE_FIRMWARE(ICL_CSR_PATH);

#define CNL_CSR_PATH			"i915/cnl_dmc_ver1_07.bin"
#define CNL_CSR_VERSION_REQUIRED	CSR_VERSION(1, 7)
#define CNL_CSR_MAX_FW_SIZE		GLK_CSR_MAX_FW_SIZE
MODULE_FIRMWARE(CNL_CSR_PATH);

#define GLK_CSR_PATH			"i915/glk_dmc_ver1_04.bin"
#define GLK_CSR_VERSION_REQUIRED	CSR_VERSION(1, 4)
#define GLK_CSR_MAX_FW_SIZE		0x4000
MODULE_FIRMWARE(GLK_CSR_PATH);

#define KBL_CSR_PATH			"i915/kbl_dmc_ver1_04.bin"
#define KBL_CSR_VERSION_REQUIRED	CSR_VERSION(1, 4)
#define KBL_CSR_MAX_FW_SIZE		BXT_CSR_MAX_FW_SIZE
MODULE_FIRMWARE(KBL_CSR_PATH);

#define SKL_CSR_PATH			"i915/skl_dmc_ver1_27.bin"
#define SKL_CSR_VERSION_REQUIRED	CSR_VERSION(1, 27)
#define SKL_CSR_MAX_FW_SIZE		BXT_CSR_MAX_FW_SIZE
MODULE_FIRMWARE(SKL_CSR_PATH);

#define BXT_CSR_PATH			"i915/bxt_dmc_ver1_07.bin"
#define BXT_CSR_VERSION_REQUIRED	CSR_VERSION(1, 7)
#define BXT_CSR_MAX_FW_SIZE		0x3000
MODULE_FIRMWARE(BXT_CSR_PATH);

#define CSR_DEFAULT_FW_OFFSET		0xFFFFFFFF
#define PACKAGE_MAX_FW_INFO_ENTRIES	20
#define PACKAGE_V2_MAX_FW_INFO_ENTRIES	32
#define DMC_V1_MAX_MMIO_COUNT		8
#define DMC_V3_MAX_MMIO_COUNT		20

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

	if (IS_GEN9_LP(dev_priv))
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
 * intel_csr_load_program() - write the firmware from memory to register.
 * @dev_priv: i915 drm device.
 *
 * CSR firmware is read from a .bin file and kept in internal memory one time.
 * Everytime display comes back from low power state this function is called to
 * copy the firmware from internal memory to registers.
 */
void intel_csr_load_program(struct drm_i915_private *dev_priv)
{
	u32 *payload = dev_priv->csr.dmc_payload;
	u32 i, fw_size;

	if (!HAS_CSR(dev_priv)) {
		drm_err(&dev_priv->drm,
			"No CSR support available for this platform\n");
		return;
	}

	if (!dev_priv->csr.dmc_payload) {
		drm_err(&dev_priv->drm,
			"Tried to program CSR with empty payload\n");
		return;
	}

	fw_size = dev_priv->csr.dmc_fw_size;
	assert_rpm_wakelock_held(&dev_priv->runtime_pm);

	preempt_disable();

	for (i = 0; i < fw_size; i++)
		intel_uncore_write_fw(&dev_priv->uncore, CSR_PROGRAM(i),
				      payload[i]);

	preempt_enable();

	for (i = 0; i < dev_priv->csr.mmio_count; i++) {
		intel_de_write(dev_priv, dev_priv->csr.mmioaddr[i],
			       dev_priv->csr.mmiodata[i]);
	}

	dev_priv->csr.dc_state = 0;

	gen9_set_dc_state_debugmask(dev_priv);
}

/*
 * Search fw_info table for dmc_offset to find firmware binary: num_entries is
 * already sanitized.
 */
static u32 find_dmc_fw_offset(const struct intel_fw_info *fw_info,
			      unsigned int num_entries,
			      const struct stepping_info *si,
			      u8 package_ver)
{
	u32 dmc_offset = CSR_DEFAULT_FW_OFFSET;
	unsigned int i;

	for (i = 0; i < num_entries; i++) {
		if (package_ver > 1 && fw_info[i].dmc_id != 0)
			continue;

		if (fw_info[i].substepping == '*' &&
		    si->stepping == fw_info[i].stepping) {
			dmc_offset = fw_info[i].offset;
			break;
		}

		if (si->stepping == fw_info[i].stepping &&
		    si->substepping == fw_info[i].substepping) {
			dmc_offset = fw_info[i].offset;
			break;
		}

		if (fw_info[i].stepping == '*' &&
		    fw_info[i].substepping == '*') {
			/*
			 * In theory we should stop the search as generic
			 * entries should always come after the more specific
			 * ones, but let's continue to make sure to work even
			 * with "broken" firmwares. If we don't find a more
			 * specific one, then we use this entry
			 */
			dmc_offset = fw_info[i].offset;
		}
	}

	return dmc_offset;
}

static u32 parse_csr_fw_dmc(struct intel_csr *csr,
			    const struct intel_dmc_header_base *dmc_header,
			    size_t rem_size)
{
	unsigned int header_len_bytes, dmc_header_size, payload_size, i;
	const u32 *mmioaddr, *mmiodata;
	u32 mmio_count, mmio_count_max;
	u8 *payload;

	BUILD_BUG_ON(ARRAY_SIZE(csr->mmioaddr) < DMC_V3_MAX_MMIO_COUNT ||
		     ARRAY_SIZE(csr->mmioaddr) < DMC_V1_MAX_MMIO_COUNT);

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
		dmc_header_size = sizeof(*v1);
	} else {
		DRM_ERROR("Unknown DMC fw header version: %u\n",
			  dmc_header->header_ver);
		return 0;
	}

	if (header_len_bytes != dmc_header_size) {
		DRM_ERROR("DMC firmware has wrong dmc header length "
			  "(%u bytes)\n", header_len_bytes);
		return 0;
	}

	/* Cache the dmc header info. */
	if (mmio_count > mmio_count_max) {
		DRM_ERROR("DMC firmware has wrong mmio count %u\n", mmio_count);
		return 0;
	}

	for (i = 0; i < mmio_count; i++) {
		if (mmioaddr[i] < CSR_MMIO_START_RANGE ||
		    mmioaddr[i] > CSR_MMIO_END_RANGE) {
			DRM_ERROR("DMC firmware has wrong mmio address 0x%x\n",
				  mmioaddr[i]);
			return 0;
		}
		csr->mmioaddr[i] = _MMIO(mmioaddr[i]);
		csr->mmiodata[i] = mmiodata[i];
	}
	csr->mmio_count = mmio_count;

	rem_size -= header_len_bytes;

	/* fw_size is in dwords, so multiplied by 4 to convert into bytes. */
	payload_size = dmc_header->fw_size * 4;
	if (rem_size < payload_size)
		goto error_truncated;

	if (payload_size > csr->max_fw_size) {
		DRM_ERROR("DMC FW too big (%u bytes)\n", payload_size);
		return 0;
	}
	csr->dmc_fw_size = dmc_header->fw_size;

	csr->dmc_payload = kmalloc(payload_size, GFP_KERNEL);
	if (!csr->dmc_payload) {
		DRM_ERROR("Memory allocation failed for dmc payload\n");
		return 0;
	}

	payload = (u8 *)(dmc_header) + header_len_bytes;
	memcpy(csr->dmc_payload, payload, payload_size);

	return header_len_bytes + payload_size;

error_truncated:
	DRM_ERROR("Truncated DMC firmware, refusing.\n");
	return 0;
}

static u32
parse_csr_fw_package(struct intel_csr *csr,
		     const struct intel_package_header *package_header,
		     const struct stepping_info *si,
		     size_t rem_size)
{
	u32 package_size = sizeof(struct intel_package_header);
	u32 num_entries, max_entries, dmc_offset;
	const struct intel_fw_info *fw_info;

	if (rem_size < package_size)
		goto error_truncated;

	if (package_header->header_ver == 1) {
		max_entries = PACKAGE_MAX_FW_INFO_ENTRIES;
	} else if (package_header->header_ver == 2) {
		max_entries = PACKAGE_V2_MAX_FW_INFO_ENTRIES;
	} else {
		DRM_ERROR("DMC firmware has unknown header version %u\n",
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
		DRM_ERROR("DMC firmware has wrong package header length "
			  "(%u bytes)\n", package_size);
		return 0;
	}

	num_entries = package_header->num_entries;
	if (WARN_ON(package_header->num_entries > max_entries))
		num_entries = max_entries;

	fw_info = (const struct intel_fw_info *)
		((u8 *)package_header + sizeof(*package_header));
	dmc_offset = find_dmc_fw_offset(fw_info, num_entries, si,
					package_header->header_ver);
	if (dmc_offset == CSR_DEFAULT_FW_OFFSET) {
		DRM_ERROR("DMC firmware not supported for %c stepping\n",
			  si->stepping);
		return 0;
	}

	/* dmc_offset is in dwords */
	return package_size + dmc_offset * 4;

error_truncated:
	DRM_ERROR("Truncated DMC firmware, refusing.\n");
	return 0;
}

/* Return number of bytes parsed or 0 on error */
static u32 parse_csr_fw_css(struct intel_csr *csr,
			    struct intel_css_header *css_header,
			    size_t rem_size)
{
	if (rem_size < sizeof(struct intel_css_header)) {
		DRM_ERROR("Truncated DMC firmware, refusing.\n");
		return 0;
	}

	if (sizeof(struct intel_css_header) !=
	    (css_header->header_len * 4)) {
		DRM_ERROR("DMC firmware has wrong CSS header length "
			  "(%u bytes)\n",
			  (css_header->header_len * 4));
		return 0;
	}

	if (csr->required_version &&
	    css_header->version != csr->required_version) {
		DRM_INFO("Refusing to load DMC firmware v%u.%u,"
			 " please use v%u.%u\n",
			 CSR_VERSION_MAJOR(css_header->version),
			 CSR_VERSION_MINOR(css_header->version),
			 CSR_VERSION_MAJOR(csr->required_version),
			 CSR_VERSION_MINOR(csr->required_version));
		return 0;
	}

	csr->version = css_header->version;

	return sizeof(struct intel_css_header);
}

static void parse_csr_fw(struct drm_i915_private *dev_priv,
			 const struct firmware *fw)
{
	struct intel_css_header *css_header;
	struct intel_package_header *package_header;
	struct intel_dmc_header_base *dmc_header;
	struct intel_csr *csr = &dev_priv->csr;
	const struct stepping_info *si = intel_get_stepping_info(dev_priv);
	u32 readcount = 0;
	u32 r;

	if (!fw)
		return;

	/* Extract CSS Header information */
	css_header = (struct intel_css_header *)fw->data;
	r = parse_csr_fw_css(csr, css_header, fw->size);
	if (!r)
		return;

	readcount += r;

	/* Extract Package Header information */
	package_header = (struct intel_package_header *)&fw->data[readcount];
	r = parse_csr_fw_package(csr, package_header, si, fw->size - readcount);
	if (!r)
		return;

	readcount += r;

	/* Extract dmc_header information */
	dmc_header = (struct intel_dmc_header_base *)&fw->data[readcount];
	parse_csr_fw_dmc(csr, dmc_header, fw->size - readcount);
}

static void intel_csr_runtime_pm_get(struct drm_i915_private *dev_priv)
{
	drm_WARN_ON(&dev_priv->drm, dev_priv->csr.wakeref);
	dev_priv->csr.wakeref =
		intel_display_power_get(dev_priv, POWER_DOMAIN_INIT);
}

static void intel_csr_runtime_pm_put(struct drm_i915_private *dev_priv)
{
	intel_wakeref_t wakeref __maybe_unused =
		fetch_and_zero(&dev_priv->csr.wakeref);

	intel_display_power_put(dev_priv, POWER_DOMAIN_INIT, wakeref);
}

static void csr_load_work_fn(struct work_struct *work)
{
	struct drm_i915_private *dev_priv;
	struct intel_csr *csr;
	const struct firmware *fw = NULL;

	dev_priv = container_of(work, typeof(*dev_priv), csr.work);
	csr = &dev_priv->csr;

	request_firmware(&fw, dev_priv->csr.fw_path, &dev_priv->drm.pdev->dev);
	parse_csr_fw(dev_priv, fw);

	if (dev_priv->csr.dmc_payload) {
		intel_csr_load_program(dev_priv);
		intel_csr_runtime_pm_put(dev_priv);

		drm_info(&dev_priv->drm,
			 "Finished loading DMC firmware %s (v%u.%u)\n",
			 dev_priv->csr.fw_path, CSR_VERSION_MAJOR(csr->version),
			 CSR_VERSION_MINOR(csr->version));
	} else {
		drm_notice(&dev_priv->drm,
			   "Failed to load DMC firmware %s."
			   " Disabling runtime power management.\n",
			   csr->fw_path);
		drm_notice(&dev_priv->drm, "DMC firmware homepage: %s",
			   INTEL_UC_FIRMWARE_URL);
	}

	release_firmware(fw);
}

/**
 * intel_csr_ucode_init() - initialize the firmware loading.
 * @dev_priv: i915 drm device.
 *
 * This function is called at the time of loading the display driver to read
 * firmware from a .bin file and copied into a internal memory.
 */
void intel_csr_ucode_init(struct drm_i915_private *dev_priv)
{
	struct intel_csr *csr = &dev_priv->csr;

	INIT_WORK(&dev_priv->csr.work, csr_load_work_fn);

	if (!HAS_CSR(dev_priv))
		return;

	/*
	 * Obtain a runtime pm reference, until CSR is loaded, to avoid entering
	 * runtime-suspend.
	 *
	 * On error, we return with the rpm wakeref held to prevent runtime
	 * suspend as runtime suspend *requires* a working CSR for whatever
	 * reason.
	 */
	intel_csr_runtime_pm_get(dev_priv);

	if (INTEL_GEN(dev_priv) >= 12) {
		csr->fw_path = TGL_CSR_PATH;
		csr->required_version = TGL_CSR_VERSION_REQUIRED;
		/* Allow to load fw via parameter using the last known size */
		csr->max_fw_size = GEN12_CSR_MAX_FW_SIZE;
	} else if (IS_GEN(dev_priv, 11)) {
		csr->fw_path = ICL_CSR_PATH;
		csr->required_version = ICL_CSR_VERSION_REQUIRED;
		csr->max_fw_size = ICL_CSR_MAX_FW_SIZE;
	} else if (IS_CANNONLAKE(dev_priv)) {
		csr->fw_path = CNL_CSR_PATH;
		csr->required_version = CNL_CSR_VERSION_REQUIRED;
		csr->max_fw_size = CNL_CSR_MAX_FW_SIZE;
	} else if (IS_GEMINILAKE(dev_priv)) {
		csr->fw_path = GLK_CSR_PATH;
		csr->required_version = GLK_CSR_VERSION_REQUIRED;
		csr->max_fw_size = GLK_CSR_MAX_FW_SIZE;
	} else if (IS_KABYLAKE(dev_priv) || IS_COFFEELAKE(dev_priv)) {
		csr->fw_path = KBL_CSR_PATH;
		csr->required_version = KBL_CSR_VERSION_REQUIRED;
		csr->max_fw_size = KBL_CSR_MAX_FW_SIZE;
	} else if (IS_SKYLAKE(dev_priv)) {
		csr->fw_path = SKL_CSR_PATH;
		csr->required_version = SKL_CSR_VERSION_REQUIRED;
		csr->max_fw_size = SKL_CSR_MAX_FW_SIZE;
	} else if (IS_BROXTON(dev_priv)) {
		csr->fw_path = BXT_CSR_PATH;
		csr->required_version = BXT_CSR_VERSION_REQUIRED;
		csr->max_fw_size = BXT_CSR_MAX_FW_SIZE;
	}

	if (i915_modparams.dmc_firmware_path) {
		if (strlen(i915_modparams.dmc_firmware_path) == 0) {
			csr->fw_path = NULL;
			drm_info(&dev_priv->drm,
				 "Disabling CSR firmware and runtime PM\n");
			return;
		}

		csr->fw_path = i915_modparams.dmc_firmware_path;
		/* Bypass version check for firmware override. */
		csr->required_version = 0;
	}

	if (csr->fw_path == NULL) {
		drm_dbg_kms(&dev_priv->drm,
			    "No known CSR firmware for platform, disabling runtime PM\n");
		return;
	}

	drm_dbg_kms(&dev_priv->drm, "Loading %s\n", csr->fw_path);
	schedule_work(&dev_priv->csr.work);
}

/**
 * intel_csr_ucode_suspend() - prepare CSR firmware before system suspend
 * @dev_priv: i915 drm device
 *
 * Prepare the DMC firmware before entering system suspend. This includes
 * flushing pending work items and releasing any resources acquired during
 * init.
 */
void intel_csr_ucode_suspend(struct drm_i915_private *dev_priv)
{
	if (!HAS_CSR(dev_priv))
		return;

	flush_work(&dev_priv->csr.work);

	/* Drop the reference held in case DMC isn't loaded. */
	if (!dev_priv->csr.dmc_payload)
		intel_csr_runtime_pm_put(dev_priv);
}

/**
 * intel_csr_ucode_resume() - init CSR firmware during system resume
 * @dev_priv: i915 drm device
 *
 * Reinitialize the DMC firmware during system resume, reacquiring any
 * resources released in intel_csr_ucode_suspend().
 */
void intel_csr_ucode_resume(struct drm_i915_private *dev_priv)
{
	if (!HAS_CSR(dev_priv))
		return;

	/*
	 * Reacquire the reference to keep RPM disabled in case DMC isn't
	 * loaded.
	 */
	if (!dev_priv->csr.dmc_payload)
		intel_csr_runtime_pm_get(dev_priv);
}

/**
 * intel_csr_ucode_fini() - unload the CSR firmware.
 * @dev_priv: i915 drm device.
 *
 * Firmmware unloading includes freeing the internal memory and reset the
 * firmware loading status.
 */
void intel_csr_ucode_fini(struct drm_i915_private *dev_priv)
{
	if (!HAS_CSR(dev_priv))
		return;

	intel_csr_ucode_suspend(dev_priv);
	drm_WARN_ON(&dev_priv->drm, dev_priv->csr.wakeref);

	kfree(dev_priv->csr.dmc_payload);
}

/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2014-2018 Intel Corporation
 */

#include "intel_huc_fw.h"
#include "i915_drv.h"

/**
 * DOC: HuC Firmware
 *
 * Motivation:
 * GEN9 introduces a new dedicated firmware for usage in media HEVC (High
 * Efficiency Video Coding) operations. Userspace can use the firmware
 * capabilities by adding HuC specific commands to batch buffers.
 *
 * Implementation:
 * The same firmware loader is used as the GuC. However, the actual
 * loading to HW is deferred until GEM initialization is done.
 *
 * Note that HuC firmware loading must be done before GuC loading.
 */

#define BXT_HUC_FW_MAJOR 01
#define BXT_HUC_FW_MINOR 8
#define BXT_BLD_NUM 2893

#define SKL_HUC_FW_MAJOR 01
#define SKL_HUC_FW_MINOR 07
#define SKL_BLD_NUM 1398

#define KBL_HUC_FW_MAJOR 02
#define KBL_HUC_FW_MINOR 00
#define KBL_BLD_NUM 1810

#define HUC_FW_PATH(platform, major, minor, bld_num) \
	"i915/" __stringify(platform) "_huc_ver" __stringify(major) "_" \
	__stringify(minor) "_" __stringify(bld_num) ".bin"

#define I915_SKL_HUC_UCODE HUC_FW_PATH(skl, SKL_HUC_FW_MAJOR, \
	SKL_HUC_FW_MINOR, SKL_BLD_NUM)
MODULE_FIRMWARE(I915_SKL_HUC_UCODE);

#define I915_BXT_HUC_UCODE HUC_FW_PATH(bxt, BXT_HUC_FW_MAJOR, \
	BXT_HUC_FW_MINOR, BXT_BLD_NUM)
MODULE_FIRMWARE(I915_BXT_HUC_UCODE);

#define I915_KBL_HUC_UCODE HUC_FW_PATH(kbl, KBL_HUC_FW_MAJOR, \
	KBL_HUC_FW_MINOR, KBL_BLD_NUM)
MODULE_FIRMWARE(I915_KBL_HUC_UCODE);

static void huc_fw_select(struct intel_uc_fw *huc_fw)
{
	struct intel_huc *huc = container_of(huc_fw, struct intel_huc, fw);
	struct drm_i915_private *dev_priv = huc_to_i915(huc);

	GEM_BUG_ON(huc_fw->type != INTEL_UC_FW_TYPE_HUC);

	if (!HAS_HUC(dev_priv))
		return;

	if (i915_modparams.huc_firmware_path) {
		huc_fw->path = i915_modparams.huc_firmware_path;
		huc_fw->major_ver_wanted = 0;
		huc_fw->minor_ver_wanted = 0;
	} else if (IS_SKYLAKE(dev_priv)) {
		huc_fw->path = I915_SKL_HUC_UCODE;
		huc_fw->major_ver_wanted = SKL_HUC_FW_MAJOR;
		huc_fw->minor_ver_wanted = SKL_HUC_FW_MINOR;
	} else if (IS_BROXTON(dev_priv)) {
		huc_fw->path = I915_BXT_HUC_UCODE;
		huc_fw->major_ver_wanted = BXT_HUC_FW_MAJOR;
		huc_fw->minor_ver_wanted = BXT_HUC_FW_MINOR;
	} else if (IS_KABYLAKE(dev_priv) || IS_COFFEELAKE(dev_priv)) {
		huc_fw->path = I915_KBL_HUC_UCODE;
		huc_fw->major_ver_wanted = KBL_HUC_FW_MAJOR;
		huc_fw->minor_ver_wanted = KBL_HUC_FW_MINOR;
	}
}

/**
 * intel_huc_fw_init_early() - initializes HuC firmware struct
 * @huc: intel_huc struct
 *
 * On platforms with HuC selects firmware for uploading
 */
void intel_huc_fw_init_early(struct intel_huc *huc)
{
	struct intel_uc_fw *huc_fw = &huc->fw;

	intel_uc_fw_init_early(huc_fw, INTEL_UC_FW_TYPE_HUC);
	huc_fw_select(huc_fw);
}

static void huc_xfer_rsa(struct intel_huc *huc)
{
	struct intel_uc_fw *fw = &huc->fw;
	struct sg_table *pages = fw->obj->mm.pages;

	/*
	 * HuC firmware image is outside GuC accessible range.
	 * Copy the RSA signature out of the image into
	 * the perma-pinned region set aside for it
	 */
	sg_pcopy_to_buffer(pages->sgl, pages->nents,
			   huc->rsa_data_vaddr, fw->rsa_size,
			   fw->rsa_offset);
}

static int huc_xfer_ucode(struct intel_huc *huc)
{
	struct intel_uc_fw *huc_fw = &huc->fw;
	struct drm_i915_private *dev_priv = huc_to_i915(huc);
	struct intel_uncore *uncore = &dev_priv->uncore;
	unsigned long offset = 0;
	u32 size;
	int ret;

	GEM_BUG_ON(huc_fw->type != INTEL_UC_FW_TYPE_HUC);

	intel_uncore_forcewake_get(uncore, FORCEWAKE_ALL);

	/* Set the source address for the uCode */
	offset = intel_uc_fw_ggtt_offset(huc_fw) +
		 huc_fw->header_offset;
	intel_uncore_write(uncore, DMA_ADDR_0_LOW,
			   lower_32_bits(offset));
	intel_uncore_write(uncore, DMA_ADDR_0_HIGH,
			   upper_32_bits(offset) & 0xFFFF);

	/*
	 * Hardware doesn't look at destination address for HuC. Set it to 0,
	 * but still program the correct address space.
	 */
	intel_uncore_write(uncore, DMA_ADDR_1_LOW, 0);
	intel_uncore_write(uncore, DMA_ADDR_1_HIGH, DMA_ADDRESS_SPACE_WOPCM);

	size = huc_fw->header_size + huc_fw->ucode_size;
	intel_uncore_write(uncore, DMA_COPY_SIZE, size);

	/* Start the DMA */
	intel_uncore_write(uncore, DMA_CTRL,
			   _MASKED_BIT_ENABLE(HUC_UKERNEL | START_DMA));

	/* Wait for DMA to finish */
	ret = intel_wait_for_register_fw(uncore, DMA_CTRL, START_DMA, 0, 100);

	DRM_DEBUG_DRIVER("HuC DMA transfer wait over with ret %d\n", ret);

	/* Disable the bits once DMA is over */
	intel_uncore_write(uncore, DMA_CTRL, _MASKED_BIT_DISABLE(HUC_UKERNEL));

	intel_uncore_forcewake_put(uncore, FORCEWAKE_ALL);

	return ret;
}

/**
 * huc_fw_xfer() - DMA's the firmware
 * @huc_fw: the firmware descriptor
 *
 * Transfer the firmware image to RAM for execution by the microcontroller.
 *
 * Return: 0 on success, non-zero on failure
 */
static int huc_fw_xfer(struct intel_uc_fw *huc_fw)
{
	struct intel_huc *huc = container_of(huc_fw, struct intel_huc, fw);

	huc_xfer_rsa(huc);

	return huc_xfer_ucode(huc);
}

/**
 * intel_huc_fw_upload() - load HuC uCode to device
 * @huc: intel_huc structure
 *
 * Called from intel_uc_init_hw() during driver load, resume from sleep and
 * after a GPU reset. Note that HuC must be loaded before GuC.
 *
 * The firmware image should have already been fetched into memory, so only
 * check that fetch succeeded, and then transfer the image to the h/w.
 *
 * Return:	non-zero code on error
 */
int intel_huc_fw_upload(struct intel_huc *huc)
{
	return intel_uc_fw_upload(&huc->fw, huc_fw_xfer);
}

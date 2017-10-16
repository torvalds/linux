/*
 * Copyright © 2016-2017 Intel Corporation
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

#include <linux/types.h>

#include "intel_huc.h"
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
#define BXT_HUC_FW_MINOR 07
#define BXT_BLD_NUM 1398

#define SKL_HUC_FW_MAJOR 01
#define SKL_HUC_FW_MINOR 07
#define SKL_BLD_NUM 1398

#define KBL_HUC_FW_MAJOR 02
#define KBL_HUC_FW_MINOR 00
#define KBL_BLD_NUM 1810

#define GLK_HUC_FW_MAJOR 02
#define GLK_HUC_FW_MINOR 00
#define GLK_BLD_NUM 1748

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

#define I915_GLK_HUC_UCODE HUC_FW_PATH(glk, GLK_HUC_FW_MAJOR, \
	GLK_HUC_FW_MINOR, GLK_BLD_NUM)

/**
 * intel_huc_select_fw() - selects HuC firmware for loading
 * @huc:	intel_huc struct
 */
void intel_huc_select_fw(struct intel_huc *huc)
{
	struct drm_i915_private *dev_priv = huc_to_i915(huc);

	intel_uc_fw_init(&huc->fw, INTEL_UC_FW_TYPE_HUC);

	if (i915_modparams.huc_firmware_path) {
		huc->fw.path = i915_modparams.huc_firmware_path;
		huc->fw.major_ver_wanted = 0;
		huc->fw.minor_ver_wanted = 0;
	} else if (IS_SKYLAKE(dev_priv)) {
		huc->fw.path = I915_SKL_HUC_UCODE;
		huc->fw.major_ver_wanted = SKL_HUC_FW_MAJOR;
		huc->fw.minor_ver_wanted = SKL_HUC_FW_MINOR;
	} else if (IS_BROXTON(dev_priv)) {
		huc->fw.path = I915_BXT_HUC_UCODE;
		huc->fw.major_ver_wanted = BXT_HUC_FW_MAJOR;
		huc->fw.minor_ver_wanted = BXT_HUC_FW_MINOR;
	} else if (IS_KABYLAKE(dev_priv) || IS_COFFEELAKE(dev_priv)) {
		huc->fw.path = I915_KBL_HUC_UCODE;
		huc->fw.major_ver_wanted = KBL_HUC_FW_MAJOR;
		huc->fw.minor_ver_wanted = KBL_HUC_FW_MINOR;
	} else if (IS_GEMINILAKE(dev_priv)) {
		huc->fw.path = I915_GLK_HUC_UCODE;
		huc->fw.major_ver_wanted = GLK_HUC_FW_MAJOR;
		huc->fw.minor_ver_wanted = GLK_HUC_FW_MINOR;
	} else {
		DRM_ERROR("No HuC firmware known for platform with HuC!\n");
		return;
	}
}

/**
 * huc_ucode_xfer() - DMA's the firmware
 * @dev_priv: the drm_i915_private device
 *
 * Transfer the firmware image to RAM for execution by the microcontroller.
 *
 * Return: 0 on success, non-zero on failure
 */
static int huc_ucode_xfer(struct intel_uc_fw *huc_fw, struct i915_vma *vma)
{
	struct intel_huc *huc = container_of(huc_fw, struct intel_huc, fw);
	struct drm_i915_private *dev_priv = huc_to_i915(huc);
	unsigned long offset = 0;
	u32 size;
	int ret;

	GEM_BUG_ON(huc_fw->type != INTEL_UC_FW_TYPE_HUC);

	intel_uncore_forcewake_get(dev_priv, FORCEWAKE_ALL);

	/* Set the source address for the uCode */
	offset = guc_ggtt_offset(vma) + huc_fw->header_offset;
	I915_WRITE(DMA_ADDR_0_LOW, lower_32_bits(offset));
	I915_WRITE(DMA_ADDR_0_HIGH, upper_32_bits(offset) & 0xFFFF);

	/* Hardware doesn't look at destination address for HuC. Set it to 0,
	 * but still program the correct address space.
	 */
	I915_WRITE(DMA_ADDR_1_LOW, 0);
	I915_WRITE(DMA_ADDR_1_HIGH, DMA_ADDRESS_SPACE_WOPCM);

	size = huc_fw->header_size + huc_fw->ucode_size;
	I915_WRITE(DMA_COPY_SIZE, size);

	/* Start the DMA */
	I915_WRITE(DMA_CTRL, _MASKED_BIT_ENABLE(HUC_UKERNEL | START_DMA));

	/* Wait for DMA to finish */
	ret = wait_for((I915_READ(DMA_CTRL) & START_DMA) == 0, 100);

	DRM_DEBUG_DRIVER("HuC DMA transfer wait over with ret %d\n", ret);

	/* Disable the bits once DMA is over */
	I915_WRITE(DMA_CTRL, _MASKED_BIT_DISABLE(HUC_UKERNEL));

	intel_uncore_forcewake_put(dev_priv, FORCEWAKE_ALL);

	return ret;
}

/**
 * intel_huc_init_hw() - load HuC uCode to device
 * @huc: intel_huc structure
 *
 * Called from guc_setup() during driver loading and also after a GPU reset.
 * Be note that HuC loading must be done before GuC loading.
 *
 * The firmware image should have already been fetched into memory by the
 * earlier call to intel_huc_init(), so here we need only check that
 * is succeeded, and then transfer the image to the h/w.
 *
 */
void intel_huc_init_hw(struct intel_huc *huc)
{
	intel_uc_fw_upload(&huc->fw, huc_ucode_xfer);
}

/**
 * intel_huc_auth() - Authenticate HuC uCode
 * @huc: intel_huc structure
 *
 * Called after HuC and GuC firmware loading during intel_uc_init_hw().
 *
 * This function pins HuC firmware image object into GGTT.
 * Then it invokes GuC action to authenticate passing the offset to RSA
 * signature through intel_guc_auth_huc(). It then waits for 50ms for
 * firmware verification ACK and unpins the object.
 */
void intel_huc_auth(struct intel_huc *huc)
{
	struct drm_i915_private *i915 = huc_to_i915(huc);
	struct intel_guc *guc = &i915->guc;
	struct i915_vma *vma;
	int ret;

	if (huc->fw.load_status != INTEL_UC_FIRMWARE_SUCCESS)
		return;

	vma = i915_gem_object_ggtt_pin(huc->fw.obj, NULL, 0, 0,
				PIN_OFFSET_BIAS | GUC_WOPCM_TOP);
	if (IS_ERR(vma)) {
		DRM_ERROR("failed to pin huc fw object %d\n",
				(int)PTR_ERR(vma));
		return;
	}

	ret = intel_guc_auth_huc(guc,
				 guc_ggtt_offset(vma) + huc->fw.rsa_offset);
	if (ret) {
		DRM_ERROR("HuC: GuC did not ack Auth request %d\n", ret);
		goto out;
	}

	/* Check authentication status, it should be done by now */
	ret = intel_wait_for_register(i915,
				      HUC_STATUS2,
				      HUC_FW_VERIFIED,
				      HUC_FW_VERIFIED,
				      50);
	if (ret) {
		DRM_ERROR("HuC: Authentication failed %d\n", ret);
		goto out;
	}

out:
	i915_vma_unpin(vma);
}

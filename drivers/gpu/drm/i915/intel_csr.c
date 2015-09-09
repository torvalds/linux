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

/**
 * DOC: csr support for dmc
 *
 * Display Context Save and Restore (CSR) firmware support added from gen9
 * onwards to drive newly added DMC (Display microcontroller) in display
 * engine to save and restore the state of display engine when it enter into
 * low-power state and comes back to normal.
 *
 * Firmware loading status will be one of the below states: FW_UNINITIALIZED,
 * FW_LOADED, FW_FAILED.
 *
 * Once the firmware is written into the registers status will be moved from
 * FW_UNINITIALIZED to FW_LOADED and for any erroneous condition status will
 * be moved to FW_FAILED.
 */

#define I915_CSR_SKL "i915/skl_dmc_ver1.bin"

MODULE_FIRMWARE(I915_CSR_SKL);

/*
* SKL CSR registers for DC5 and DC6
*/
#define CSR_PROGRAM_BASE		0x80000
#define CSR_SSP_BASE_ADDR_GEN9		0x00002FC0
#define CSR_HTP_ADDR_SKL		0x00500034
#define CSR_SSP_BASE			0x8F074
#define CSR_HTP_SKL			0x8F004
#define CSR_LAST_WRITE			0x8F034
#define CSR_LAST_WRITE_VALUE		0xc003b400
/* MMIO address range for CSR program (0x80000 - 0x82FFF) */
#define CSR_MAX_FW_SIZE			0x2FFF
#define CSR_DEFAULT_FW_OFFSET		0xFFFFFFFF
#define CSR_MMIO_START_RANGE	0x80000
#define CSR_MMIO_END_RANGE		0x8FFFF

struct intel_css_header {
	/* 0x09 for DMC */
	uint32_t module_type;

	/* Includes the DMC specific header in dwords */
	uint32_t header_len;

	/* always value would be 0x10000 */
	uint32_t header_ver;

	/* Not used */
	uint32_t module_id;

	/* Not used */
	uint32_t module_vendor;

	/* in YYYYMMDD format */
	uint32_t date;

	/* Size in dwords (CSS_Headerlen + PackageHeaderLen + dmc FWsLen)/4 */
	uint32_t size;

	/* Not used */
	uint32_t key_size;

	/* Not used */
	uint32_t modulus_size;

	/* Not used */
	uint32_t exponent_size;

	/* Not used */
	uint32_t reserved1[12];

	/* Major Minor */
	uint32_t version;

	/* Not used */
	uint32_t reserved2[8];

	/* Not used */
	uint32_t kernel_header_info;
} __packed;

struct intel_fw_info {
	uint16_t reserved1;

	/* Stepping (A, B, C, ..., *). * is a wildcard */
	char stepping;

	/* Sub-stepping (0, 1, ..., *). * is a wildcard */
	char substepping;

	uint32_t offset;
	uint32_t reserved2;
} __packed;

struct intel_package_header {
	/* DMC container header length in dwords */
	unsigned char header_len;

	/* always value would be 0x01 */
	unsigned char header_ver;

	unsigned char reserved[10];

	/* Number of valid entries in the FWInfo array below */
	uint32_t num_entries;

	struct intel_fw_info fw_info[20];
} __packed;

struct intel_dmc_header {
	/* always value would be 0x40403E3E */
	uint32_t signature;

	/* DMC binary header length */
	unsigned char header_len;

	/* 0x01 */
	unsigned char header_ver;

	/* Reserved */
	uint16_t dmcc_ver;

	/* Major, Minor */
	uint32_t	project;

	/* Firmware program size (excluding header) in dwords */
	uint32_t	fw_size;

	/* Major Minor version */
	uint32_t fw_version;

	/* Number of valid MMIO cycles present. */
	uint32_t mmio_count;

	/* MMIO address */
	uint32_t mmioaddr[8];

	/* MMIO data */
	uint32_t mmiodata[8];

	/* FW filename  */
	unsigned char dfile[32];

	uint32_t reserved1[2];
} __packed;

struct stepping_info {
	char stepping;
	char substepping;
};

static const struct stepping_info skl_stepping_info[] = {
		{'A', '0'}, {'B', '0'}, {'C', '0'},
		{'D', '0'}, {'E', '0'}, {'F', '0'},
		{'G', '0'}, {'H', '0'}, {'I', '0'}
};

static char intel_get_stepping(struct drm_device *dev)
{
	if (IS_SKYLAKE(dev) && (dev->pdev->revision <
			ARRAY_SIZE(skl_stepping_info)))
		return skl_stepping_info[dev->pdev->revision].stepping;
	else
		return -ENODATA;
}

static char intel_get_substepping(struct drm_device *dev)
{
	if (IS_SKYLAKE(dev) && (dev->pdev->revision <
			ARRAY_SIZE(skl_stepping_info)))
		return skl_stepping_info[dev->pdev->revision].substepping;
	else
		return -ENODATA;
}

/**
 * intel_csr_load_status_get() - to get firmware loading status.
 * @dev_priv: i915 device.
 *
 * This function helps to get the firmware loading status.
 *
 * Return: Firmware loading status.
 */
enum csr_state intel_csr_load_status_get(struct drm_i915_private *dev_priv)
{
	enum csr_state state;

	mutex_lock(&dev_priv->csr_lock);
	state = dev_priv->csr.state;
	mutex_unlock(&dev_priv->csr_lock);

	return state;
}

/**
 * intel_csr_load_status_set() - help to set firmware loading status.
 * @dev_priv: i915 device.
 * @state: enumeration of firmware loading status.
 *
 * Set the firmware loading status.
 */
void intel_csr_load_status_set(struct drm_i915_private *dev_priv,
			enum csr_state state)
{
	mutex_lock(&dev_priv->csr_lock);
	dev_priv->csr.state = state;
	mutex_unlock(&dev_priv->csr_lock);
}

/**
 * intel_csr_load_program() - write the firmware from memory to register.
 * @dev: drm device.
 *
 * CSR firmware is read from a .bin file and kept in internal memory one time.
 * Everytime display comes back from low power state this function is called to
 * copy the firmware from internal memory to registers.
 */
void intel_csr_load_program(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	__be32 *payload = dev_priv->csr.dmc_payload;
	uint32_t i, fw_size;

	if (!IS_GEN9(dev)) {
		DRM_ERROR("No CSR support available for this platform\n");
		return;
	}

	mutex_lock(&dev_priv->csr_lock);
	fw_size = dev_priv->csr.dmc_fw_size;
	for (i = 0; i < fw_size; i++)
		I915_WRITE(CSR_PROGRAM_BASE + i * 4,
			(u32 __force)payload[i]);

	for (i = 0; i < dev_priv->csr.mmio_count; i++) {
		I915_WRITE(dev_priv->csr.mmioaddr[i],
			dev_priv->csr.mmiodata[i]);
	}

	dev_priv->csr.state = FW_LOADED;
	mutex_unlock(&dev_priv->csr_lock);
}

static void finish_csr_load(const struct firmware *fw, void *context)
{
	struct drm_i915_private *dev_priv = context;
	struct drm_device *dev = dev_priv->dev;
	struct intel_css_header *css_header;
	struct intel_package_header *package_header;
	struct intel_dmc_header *dmc_header;
	struct intel_csr *csr = &dev_priv->csr;
	char stepping = intel_get_stepping(dev);
	char substepping = intel_get_substepping(dev);
	uint32_t dmc_offset = CSR_DEFAULT_FW_OFFSET, readcount = 0, nbytes;
	uint32_t i;
	__be32 *dmc_payload;
	bool fw_loaded = false;

	if (!fw) {
		i915_firmware_load_error_print(csr->fw_path, 0);
		goto out;
	}

	if ((stepping == -ENODATA) || (substepping == -ENODATA)) {
		DRM_ERROR("Unknown stepping info, firmware loading failed\n");
		goto out;
	}

	/* Extract CSS Header information*/
	css_header = (struct intel_css_header *)fw->data;
	if (sizeof(struct intel_css_header) !=
		(css_header->header_len * 4)) {
		DRM_ERROR("Firmware has wrong CSS header length %u bytes\n",
			(css_header->header_len * 4));
		goto out;
	}
	readcount += sizeof(struct intel_css_header);

	/* Extract Package Header information*/
	package_header = (struct intel_package_header *)
					&fw->data[readcount];
	if (sizeof(struct intel_package_header) !=
		(package_header->header_len * 4)) {
		DRM_ERROR("Firmware has wrong package header length %u bytes\n",
			(package_header->header_len * 4));
		goto out;
	}
	readcount += sizeof(struct intel_package_header);

	/* Search for dmc_offset to find firware binary. */
	for (i = 0; i < package_header->num_entries; i++) {
		if (package_header->fw_info[i].substepping == '*' &&
			stepping == package_header->fw_info[i].stepping) {
			dmc_offset = package_header->fw_info[i].offset;
			break;
		} else if (stepping == package_header->fw_info[i].stepping &&
			substepping == package_header->fw_info[i].substepping) {
			dmc_offset = package_header->fw_info[i].offset;
			break;
		} else if (package_header->fw_info[i].stepping == '*' &&
			package_header->fw_info[i].substepping == '*')
			dmc_offset = package_header->fw_info[i].offset;
	}
	if (dmc_offset == CSR_DEFAULT_FW_OFFSET) {
		DRM_ERROR("Firmware not supported for %c stepping\n", stepping);
		goto out;
	}
	readcount += dmc_offset;

	/* Extract dmc_header information. */
	dmc_header = (struct intel_dmc_header *)&fw->data[readcount];
	if (sizeof(struct intel_dmc_header) != (dmc_header->header_len)) {
		DRM_ERROR("Firmware has wrong dmc header length %u bytes\n",
				(dmc_header->header_len));
		goto out;
	}
	readcount += sizeof(struct intel_dmc_header);

	/* Cache the dmc header info. */
	if (dmc_header->mmio_count > ARRAY_SIZE(csr->mmioaddr)) {
		DRM_ERROR("Firmware has wrong mmio count %u\n",
						dmc_header->mmio_count);
		goto out;
	}
	csr->mmio_count = dmc_header->mmio_count;
	for (i = 0; i < dmc_header->mmio_count; i++) {
		if (dmc_header->mmioaddr[i] < CSR_MMIO_START_RANGE ||
			dmc_header->mmioaddr[i] > CSR_MMIO_END_RANGE) {
			DRM_ERROR(" Firmware has wrong mmio address 0x%x\n",
						dmc_header->mmioaddr[i]);
			goto out;
		}
		csr->mmioaddr[i] = dmc_header->mmioaddr[i];
		csr->mmiodata[i] = dmc_header->mmiodata[i];
	}

	/* fw_size is in dwords, so multiplied by 4 to convert into bytes. */
	nbytes = dmc_header->fw_size * 4;
	if (nbytes > CSR_MAX_FW_SIZE) {
		DRM_ERROR("CSR firmware too big (%u) bytes\n", nbytes);
		goto out;
	}
	csr->dmc_fw_size = dmc_header->fw_size;

	csr->dmc_payload = kmalloc(nbytes, GFP_KERNEL);
	if (!csr->dmc_payload) {
		DRM_ERROR("Memory allocation failed for dmc payload\n");
		goto out;
	}

	dmc_payload = csr->dmc_payload;
	for (i = 0; i < dmc_header->fw_size; i++) {
		uint32_t *tmp = (u32 *)&fw->data[readcount + i * 4];
		/*
		 * The firmware payload is an array of 32 bit words stored in
		 * little-endian format in the firmware image and programmed
		 * as 32 bit big-endian format to memory.
		 */
		dmc_payload[i] = cpu_to_be32(*tmp);
	}

	/* load csr program during system boot, as needed for DC states */
	intel_csr_load_program(dev);
	fw_loaded = true;

out:
	if (fw_loaded)
		intel_runtime_pm_put(dev_priv);
	else
		intel_csr_load_status_set(dev_priv, FW_FAILED);

	release_firmware(fw);
}

/**
 * intel_csr_ucode_init() - initialize the firmware loading.
 * @dev: drm device.
 *
 * This function is called at the time of loading the display driver to read
 * firmware from a .bin file and copied into a internal memory.
 */
void intel_csr_ucode_init(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_csr *csr = &dev_priv->csr;
	int ret;

	if (!HAS_CSR(dev))
		return;

	if (IS_SKYLAKE(dev))
		csr->fw_path = I915_CSR_SKL;
	else {
		DRM_ERROR("Unexpected: no known CSR firmware for platform\n");
		intel_csr_load_status_set(dev_priv, FW_FAILED);
		return;
	}

	/*
	 * Obtain a runtime pm reference, until CSR is loaded,
	 * to avoid entering runtime-suspend.
	 */
	intel_runtime_pm_get(dev_priv);

	/* CSR supported for platform, load firmware */
	ret = request_firmware_nowait(THIS_MODULE, true, csr->fw_path,
				&dev_priv->dev->pdev->dev,
				GFP_KERNEL, dev_priv,
				finish_csr_load);
	if (ret) {
		i915_firmware_load_error_print(csr->fw_path, ret);
		intel_csr_load_status_set(dev_priv, FW_FAILED);
	}
}

/**
 * intel_csr_ucode_fini() - unload the CSR firmware.
 * @dev: drm device.
 *
 * Firmmware unloading includes freeing the internal momory and reset the
 * firmware loading status.
 */
void intel_csr_ucode_fini(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	if (!HAS_CSR(dev))
		return;

	intel_csr_load_status_set(dev_priv, FW_FAILED);
	kfree(dev_priv->csr.dmc_payload);
}

void assert_csr_loaded(struct drm_i915_private *dev_priv)
{
	WARN((intel_csr_load_status_get(dev_priv) != FW_LOADED), "CSR is not loaded.\n");
	WARN(!I915_READ(CSR_PROGRAM_BASE),
				"CSR program storage start is NULL\n");
	WARN(!I915_READ(CSR_SSP_BASE), "CSR SSP Base Not fine\n");
	WARN(!I915_READ(CSR_HTP_SKL), "CSR HTP Not fine\n");
}

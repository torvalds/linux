/*
 * Copyright 2019 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 */
#include <linux/pci.h>

#include "amdgpu.h"
#include "amdgpu_i2c.h"
#include "smu_v11_0_i2c.h"
#include "atom.h"
#include "amdgpu_fru_eeprom.h"
#include "amdgpu_eeprom.h"

#define FRU_EEPROM_MADDR        0x60000

static bool is_fru_eeprom_supported(struct amdgpu_device *adev)
{
	/* Only server cards have the FRU EEPROM
	 * TODO: See if we can figure this out dynamically instead of
	 * having to parse VBIOS versions.
	 */
	struct atom_context *atom_ctx = adev->mode_info.atom_context;

	/* The i2c access is blocked on VF
	 * TODO: Need other way to get the info
	 */
	if (amdgpu_sriov_vf(adev))
		return false;

	/* VBIOS is of the format ###-DXXXYYYY-##. For SKU identification,
	 * we can use just the "DXXX" portion. If there were more models, we
	 * could convert the 3 characters to a hex integer and use a switch
	 * for ease/speed/readability. For now, 2 string comparisons are
	 * reasonable and not too expensive
	 */
	switch (adev->asic_type) {
	case CHIP_VEGA20:
		/* D161 and D163 are the VG20 server SKUs */
		if (strnstr(atom_ctx->vbios_version, "D161",
			    sizeof(atom_ctx->vbios_version)) ||
		    strnstr(atom_ctx->vbios_version, "D163",
			    sizeof(atom_ctx->vbios_version)))
			return true;
		else
			return false;
	case CHIP_ALDEBARAN:
		/* All Aldebaran SKUs have the FRU */
		return true;
	case CHIP_SIENNA_CICHLID:
		if (strnstr(atom_ctx->vbios_version, "D603",
			    sizeof(atom_ctx->vbios_version)))
			return true;
		else
			return false;
	default:
		return false;
	}
}

static int amdgpu_fru_read_eeprom(struct amdgpu_device *adev, uint32_t addrptr,
				  unsigned char *buf, size_t buf_size)
{
	int ret;
	u8 size;

	ret = amdgpu_eeprom_read(adev->pm.fru_eeprom_i2c_bus, addrptr, buf, 1);
	if (ret < 1) {
		DRM_WARN("FRU: Failed to get size field");
		return ret;
	}

	/* The size returned by the i2c requires subtraction of 0xC0 since the
	 * size apparently always reports as 0xC0+actual size.
	 */
	size = buf[0] & 0x3F;
	size = min_t(size_t, size, buf_size);

	ret = amdgpu_eeprom_read(adev->pm.fru_eeprom_i2c_bus, addrptr + 1,
				 buf, size);
	if (ret < 1) {
		DRM_WARN("FRU: Failed to get data field");
		return ret;
	}

	return size;
}

int amdgpu_fru_get_product_info(struct amdgpu_device *adev)
{
	unsigned char buf[AMDGPU_PRODUCT_NAME_LEN];
	u32 addrptr;
	int size, len;

	if (!is_fru_eeprom_supported(adev))
		return 0;

	/* If algo exists, it means that the i2c_adapter's initialized */
	if (!adev->pm.fru_eeprom_i2c_bus || !adev->pm.fru_eeprom_i2c_bus->algo) {
		DRM_WARN("Cannot access FRU, EEPROM accessor not initialized");
		return -ENODEV;
	}

	/* There's a lot of repetition here. This is due to the FRU having
	 * variable-length fields. To get the information, we have to find the
	 * size of each field, and then keep reading along and reading along
	 * until we get all of the data that we want. We use addrptr to track
	 * the address as we go
	 */

	/* The first fields are all of size 1-byte, from 0-7 are offsets that
	 * contain information that isn't useful to us.
	 * Bytes 8-a are all 1-byte and refer to the size of the entire struct,
	 * and the language field, so just start from 0xb, manufacturer size
	 */
	addrptr = FRU_EEPROM_MADDR + 0xb;
	size = amdgpu_fru_read_eeprom(adev, addrptr, buf, sizeof(buf));
	if (size < 1) {
		DRM_ERROR("Failed to read FRU Manufacturer, ret:%d", size);
		return -EINVAL;
	}

	/* Increment the addrptr by the size of the field, and 1 due to the
	 * size field being 1 byte. This pattern continues below.
	 */
	addrptr += size + 1;
	size = amdgpu_fru_read_eeprom(adev, addrptr, buf, sizeof(buf));
	if (size < 1) {
		DRM_ERROR("Failed to read FRU product name, ret:%d", size);
		return -EINVAL;
	}

	len = size;
	if (len >= AMDGPU_PRODUCT_NAME_LEN) {
		DRM_WARN("FRU Product Name is larger than %d characters. This is likely a mistake",
				AMDGPU_PRODUCT_NAME_LEN);
		len = AMDGPU_PRODUCT_NAME_LEN - 1;
	}
	memcpy(adev->product_name, buf, len);
	adev->product_name[len] = '\0';

	addrptr += size + 1;
	size = amdgpu_fru_read_eeprom(adev, addrptr, buf, sizeof(buf));
	if (size < 1) {
		DRM_ERROR("Failed to read FRU product number, ret:%d", size);
		return -EINVAL;
	}

	len = size;
	/* Product number should only be 16 characters. Any more,
	 * and something could be wrong. Cap it at 16 to be safe
	 */
	if (len >= sizeof(adev->product_number)) {
		DRM_WARN("FRU Product Number is larger than 16 characters. This is likely a mistake");
		len = sizeof(adev->product_number) - 1;
	}
	memcpy(adev->product_number, buf, len);
	adev->product_number[len] = '\0';

	addrptr += size + 1;
	size = amdgpu_fru_read_eeprom(adev, addrptr, buf, sizeof(buf));

	if (size < 1) {
		DRM_ERROR("Failed to read FRU product version, ret:%d", size);
		return -EINVAL;
	}

	addrptr += size + 1;
	size = amdgpu_fru_read_eeprom(adev, addrptr, buf, sizeof(buf));

	if (size < 1) {
		DRM_ERROR("Failed to read FRU serial number, ret:%d", size);
		return -EINVAL;
	}

	len = size;
	/* Serial number should only be 16 characters. Any more,
	 * and something could be wrong. Cap it at 16 to be safe
	 */
	if (len >= sizeof(adev->serial)) {
		DRM_WARN("FRU Serial Number is larger than 16 characters. This is likely a mistake");
		len = sizeof(adev->serial) - 1;
	}
	memcpy(adev->serial, buf, len);
	adev->serial[len] = '\0';

	return 0;
}

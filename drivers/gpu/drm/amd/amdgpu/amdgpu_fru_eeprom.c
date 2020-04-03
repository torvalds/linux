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

#define I2C_PRODUCT_INFO_ADDR		0xAC
#define I2C_PRODUCT_INFO_ADDR_SIZE	0x2
#define I2C_PRODUCT_INFO_OFFSET		0xC0

bool is_fru_eeprom_supported(struct amdgpu_device *adev)
{
	/* TODO: Gaming SKUs don't have the FRU EEPROM.
	 * Use this hack to address hangs on modprobe on gaming SKUs
	 * until a proper solution can be implemented by only supporting
	 * it on Arcturus, and the explicit chip IDs for VG20 Server cards
	 */
	if ((adev->asic_type == CHIP_ARCTURUS) ||
	    (adev->asic_type == CHIP_VEGA20 && adev->pdev->device == 0x66a0) ||
	    (adev->asic_type == CHIP_VEGA20 && adev->pdev->device == 0x66a1) ||
	    (adev->asic_type == CHIP_VEGA20 && adev->pdev->device == 0x66a4))
		return true;
	return false;
}

int amdgpu_fru_read_eeprom(struct amdgpu_device *adev, uint32_t addrptr,
			   unsigned char *buff)
{
	int ret, size;
	struct i2c_msg msg = {
			.addr   = I2C_PRODUCT_INFO_ADDR,
			.flags  = I2C_M_RD,
			.buf    = buff,
	};
	buff[0] = 0;
	buff[1] = addrptr;
	msg.len = I2C_PRODUCT_INFO_ADDR_SIZE + 1;
	ret = i2c_transfer(&adev->pm.smu_i2c, &msg, 1);

	if (ret < 1) {
		DRM_WARN("FRU: Failed to get size field");
		return ret;
	}

	/* The size returned by the i2c requires subtraction of 0xC0 since the
	 * size apparently always reports as 0xC0+actual size.
	 */
	size = buff[2] - I2C_PRODUCT_INFO_OFFSET;
	/* Add 1 since address field was 1 byte */
	buff[1] = addrptr + 1;

	msg.len = I2C_PRODUCT_INFO_ADDR_SIZE + size;
	ret = i2c_transfer(&adev->pm.smu_i2c, &msg, 1);

	if (ret < 1) {
		DRM_WARN("FRU: Failed to get data field");
		return ret;
	}

	return size;
}

int amdgpu_fru_get_product_info(struct amdgpu_device *adev)
{
	unsigned char buff[34];
	int addrptr = 0, size = 0;

	if (!is_fru_eeprom_supported(adev))
		return 0;

	/* If algo exists, it means that the i2c_adapter's initialized */
	if (!adev->pm.smu_i2c.algo) {
		DRM_WARN("Cannot access FRU, EEPROM accessor not initialized");
		return 0;
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
	addrptr = 0xb;
	size = amdgpu_fru_read_eeprom(adev, addrptr, buff);
	if (size < 1) {
		DRM_ERROR("Failed to read FRU Manufacturer, ret:%d", size);
		return size;
	}

	/* Increment the addrptr by the size of the field, and 1 due to the
	 * size field being 1 byte. This pattern continues below.
	 */
	addrptr += size + 1;
	size = amdgpu_fru_read_eeprom(adev, addrptr, buff);
	if (size < 1) {
		DRM_ERROR("Failed to read FRU product name, ret:%d", size);
		return size;
	}

	/* Product name should only be 32 characters. Any more,
	 * and something could be wrong. Cap it at 32 to be safe
	 */
	if (size > 32) {
		DRM_WARN("FRU Product Number is larger than 32 characters. This is likely a mistake");
		size = 32;
	}
	/* Start at 2 due to buff using fields 0 and 1 for the address */
	memcpy(adev->product_name, &buff[2], size);
	adev->product_name[size] = '\0';

	addrptr += size + 1;
	size = amdgpu_fru_read_eeprom(adev, addrptr, buff);
	if (size < 1) {
		DRM_ERROR("Failed to read FRU product number, ret:%d", size);
		return size;
	}

	/* Product number should only be 16 characters. Any more,
	 * and something could be wrong. Cap it at 16 to be safe
	 */
	if (size > 16) {
		DRM_WARN("FRU Product Number is larger than 16 characters. This is likely a mistake");
		size = 16;
	}
	memcpy(adev->product_number, &buff[2], size);
	adev->product_number[size] = '\0';

	addrptr += size + 1;
	size = amdgpu_fru_read_eeprom(adev, addrptr, buff);

	if (size < 1) {
		DRM_ERROR("Failed to read FRU product version, ret:%d", size);
		return size;
	}

	addrptr += size + 1;
	size = amdgpu_fru_read_eeprom(adev, addrptr, buff);

	if (size < 1) {
		DRM_ERROR("Failed to read FRU serial number, ret:%d", size);
		return size;
	}

	/* Serial number should only be 16 characters. Any more,
	 * and something could be wrong. Cap it at 16 to be safe
	 */
	if (size > 16) {
		DRM_WARN("FRU Serial Number is larger than 16 characters. This is likely a mistake");
		size = 16;
	}
	memcpy(adev->serial, &buff[2], size);
	adev->serial[size] = '\0';

	return 0;
}

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

#define FRU_EEPROM_MADDR_6      0x60000
#define FRU_EEPROM_MADDR_8      0x80000

static bool is_fru_eeprom_supported(struct amdgpu_device *adev, u32 *fru_addr)
{
	/* Only server cards have the FRU EEPROM
	 * TODO: See if we can figure this out dynamically instead of
	 * having to parse VBIOS versions.
	 */
	struct atom_context *atom_ctx = adev->mode_info.atom_context;

	/* The i2c access is blocked on VF
	 * TODO: Need other way to get the info
	 * Also, FRU not valid for APU devices.
	 */
	if (amdgpu_sriov_vf(adev) || (adev->flags & AMD_IS_APU))
		return false;

	/* The default I2C EEPROM address of the FRU.
	 */
	if (fru_addr)
		*fru_addr = FRU_EEPROM_MADDR_8;

	/* VBIOS is of the format ###-DXXXYYYY-##. For SKU identification,
	 * we can use just the "DXXX" portion. If there were more models, we
	 * could convert the 3 characters to a hex integer and use a switch
	 * for ease/speed/readability. For now, 2 string comparisons are
	 * reasonable and not too expensive
	 */
	switch (amdgpu_ip_version(adev, MP1_HWIP, 0)) {
	case IP_VERSION(11, 0, 2):
		switch (adev->asic_type) {
		case CHIP_VEGA20:
			/* D161 and D163 are the VG20 server SKUs */
			if (strnstr(atom_ctx->vbios_pn, "D161",
				    sizeof(atom_ctx->vbios_pn)) ||
			    strnstr(atom_ctx->vbios_pn, "D163",
				    sizeof(atom_ctx->vbios_pn))) {
				if (fru_addr)
					*fru_addr = FRU_EEPROM_MADDR_6;
				return true;
			} else {
				return false;
			}
		case CHIP_ARCTURUS:
		default:
			return false;
		}
	case IP_VERSION(11, 0, 7):
		if (strnstr(atom_ctx->vbios_pn, "D603",
			    sizeof(atom_ctx->vbios_pn))) {
			if (strnstr(atom_ctx->vbios_pn, "D603GLXE",
				    sizeof(atom_ctx->vbios_pn))) {
				return false;
			}

			if (fru_addr)
				*fru_addr = FRU_EEPROM_MADDR_6;
			return true;

		} else {
			return false;
		}
	case IP_VERSION(13, 0, 2):
		/* All Aldebaran SKUs have an FRU */
		if (!strnstr(atom_ctx->vbios_pn, "D673",
			     sizeof(atom_ctx->vbios_pn)))
			if (fru_addr)
				*fru_addr = FRU_EEPROM_MADDR_6;
		return true;
	case IP_VERSION(13, 0, 6):
	case IP_VERSION(13, 0, 14):
			if (fru_addr)
				*fru_addr = FRU_EEPROM_MADDR_8;
			return true;
	default:
		return false;
	}
}

int amdgpu_fru_get_product_info(struct amdgpu_device *adev)
{
	struct amdgpu_fru_info *fru_info;
	unsigned char buf[8], *pia;
	u32 addr, fru_addr;
	int size, len;
	u8 csum;

	if (!is_fru_eeprom_supported(adev, &fru_addr))
		return 0;

	if (!adev->fru_info) {
		adev->fru_info = kzalloc(sizeof(*adev->fru_info), GFP_KERNEL);
		if (!adev->fru_info)
			return -ENOMEM;
	}

	fru_info = adev->fru_info;
	/* For Arcturus-and-later, default value of serial_number is unique_id
	 * so convert it to a 16-digit HEX string for convenience and
	 * backwards-compatibility.
	 */
	sprintf(fru_info->serial, "%llx", adev->unique_id);

	/* If algo exists, it means that the i2c_adapter's initialized */
	if (!adev->pm.fru_eeprom_i2c_bus || !adev->pm.fru_eeprom_i2c_bus->algo) {
		DRM_WARN("Cannot access FRU, EEPROM accessor not initialized");
		return -ENODEV;
	}

	/* Read the IPMI Common header */
	len = amdgpu_eeprom_read(adev->pm.fru_eeprom_i2c_bus, fru_addr, buf,
				 sizeof(buf));
	if (len != 8) {
		DRM_ERROR("Couldn't read the IPMI Common Header: %d", len);
		return len < 0 ? len : -EIO;
	}

	if (buf[0] != 1) {
		DRM_ERROR("Bad IPMI Common Header version: 0x%02x", buf[0]);
		return -EIO;
	}

	for (csum = 0; len > 0; len--)
		csum += buf[len - 1];
	if (csum) {
		DRM_ERROR("Bad IPMI Common Header checksum: 0x%02x", csum);
		return -EIO;
	}

	/* Get the offset to the Product Info Area (PIA). */
	addr = buf[4] * 8;
	if (!addr)
		return 0;

	/* Get the absolute address to the PIA. */
	addr += fru_addr;

	/* Read the header of the PIA. */
	len = amdgpu_eeprom_read(adev->pm.fru_eeprom_i2c_bus, addr, buf, 3);
	if (len != 3) {
		DRM_ERROR("Couldn't read the Product Info Area header: %d", len);
		return len < 0 ? len : -EIO;
	}

	if (buf[0] != 1) {
		DRM_ERROR("Bad IPMI Product Info Area version: 0x%02x", buf[0]);
		return -EIO;
	}

	size = buf[1] * 8;
	pia = kzalloc(size, GFP_KERNEL);
	if (!pia)
		return -ENOMEM;

	/* Read the whole PIA. */
	len = amdgpu_eeprom_read(adev->pm.fru_eeprom_i2c_bus, addr, pia, size);
	if (len != size) {
		kfree(pia);
		DRM_ERROR("Couldn't read the Product Info Area: %d", len);
		return len < 0 ? len : -EIO;
	}

	for (csum = 0; size > 0; size--)
		csum += pia[size - 1];
	if (csum) {
		DRM_ERROR("Bad Product Info Area checksum: 0x%02x", csum);
		kfree(pia);
		return -EIO;
	}

	/* Now extract useful information from the PIA.
	 *
	 * Read Manufacturer Name field whose length is [3].
	 */
	addr = 3;
	if (addr + 1 >= len)
		goto Out;
	memcpy(fru_info->manufacturer_name, pia + addr + 1,
	       min_t(size_t, sizeof(fru_info->manufacturer_name),
		     pia[addr] & 0x3F));
	fru_info->manufacturer_name[sizeof(fru_info->manufacturer_name) - 1] =
		'\0';

	/* Read Product Name field. */
	addr += 1 + (pia[addr] & 0x3F);
	if (addr + 1 >= len)
		goto Out;
	memcpy(fru_info->product_name, pia + addr + 1,
	       min_t(size_t, sizeof(fru_info->product_name), pia[addr] & 0x3F));
	fru_info->product_name[sizeof(fru_info->product_name) - 1] = '\0';

	/* Go to the Product Part/Model Number field. */
	addr += 1 + (pia[addr] & 0x3F);
	if (addr + 1 >= len)
		goto Out;
	memcpy(fru_info->product_number, pia + addr + 1,
	       min_t(size_t, sizeof(fru_info->product_number),
		     pia[addr] & 0x3F));
	fru_info->product_number[sizeof(fru_info->product_number) - 1] = '\0';

	/* Go to the Product Version field. */
	addr += 1 + (pia[addr] & 0x3F);

	/* Go to the Product Serial Number field. */
	addr += 1 + (pia[addr] & 0x3F);
	if (addr + 1 >= len)
		goto Out;
	memcpy(fru_info->serial, pia + addr + 1,
	       min_t(size_t, sizeof(fru_info->serial), pia[addr] & 0x3F));
	fru_info->serial[sizeof(fru_info->serial) - 1] = '\0';

	/* Asset Tag field */
	addr += 1 + (pia[addr] & 0x3F);

	/* FRU File Id field. This could be 'null'. */
	addr += 1 + (pia[addr] & 0x3F);
	if ((addr + 1 >= len) || !(pia[addr] & 0x3F))
		goto Out;
	memcpy(fru_info->fru_id, pia + addr + 1,
	       min_t(size_t, sizeof(fru_info->fru_id), pia[addr] & 0x3F));
	fru_info->fru_id[sizeof(fru_info->fru_id) - 1] = '\0';

Out:
	kfree(pia);
	return 0;
}

/**
 * DOC: product_name
 *
 * The amdgpu driver provides a sysfs API for reporting the product name
 * for the device
 * The file product_name is used for this and returns the product name
 * as returned from the FRU.
 * NOTE: This is only available for certain server cards
 */

static ssize_t amdgpu_fru_product_name_show(struct device *dev,
					    struct device_attribute *attr,
					    char *buf)
{
	struct drm_device *ddev = dev_get_drvdata(dev);
	struct amdgpu_device *adev = drm_to_adev(ddev);

	return sysfs_emit(buf, "%s\n", adev->fru_info->product_name);
}

static DEVICE_ATTR(product_name, 0444, amdgpu_fru_product_name_show, NULL);

/**
 * DOC: product_number
 *
 * The amdgpu driver provides a sysfs API for reporting the part number
 * for the device
 * The file product_number is used for this and returns the part number
 * as returned from the FRU.
 * NOTE: This is only available for certain server cards
 */

static ssize_t amdgpu_fru_product_number_show(struct device *dev,
					      struct device_attribute *attr,
					      char *buf)
{
	struct drm_device *ddev = dev_get_drvdata(dev);
	struct amdgpu_device *adev = drm_to_adev(ddev);

	return sysfs_emit(buf, "%s\n", adev->fru_info->product_number);
}

static DEVICE_ATTR(product_number, 0444, amdgpu_fru_product_number_show, NULL);

/**
 * DOC: serial_number
 *
 * The amdgpu driver provides a sysfs API for reporting the serial number
 * for the device
 * The file serial_number is used for this and returns the serial number
 * as returned from the FRU.
 * NOTE: This is only available for certain server cards
 */

static ssize_t amdgpu_fru_serial_number_show(struct device *dev,
					     struct device_attribute *attr,
					     char *buf)
{
	struct drm_device *ddev = dev_get_drvdata(dev);
	struct amdgpu_device *adev = drm_to_adev(ddev);

	return sysfs_emit(buf, "%s\n", adev->fru_info->serial);
}

static DEVICE_ATTR(serial_number, 0444, amdgpu_fru_serial_number_show, NULL);

/**
 * DOC: fru_id
 *
 * The amdgpu driver provides a sysfs API for reporting FRU File Id
 * for the device.
 * The file fru_id is used for this and returns the File Id value
 * as returned from the FRU.
 * NOTE: This is only available for certain server cards
 */

static ssize_t amdgpu_fru_id_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct drm_device *ddev = dev_get_drvdata(dev);
	struct amdgpu_device *adev = drm_to_adev(ddev);

	return sysfs_emit(buf, "%s\n", adev->fru_info->fru_id);
}

static DEVICE_ATTR(fru_id, 0444, amdgpu_fru_id_show, NULL);

/**
 * DOC: manufacturer
 *
 * The amdgpu driver provides a sysfs API for reporting manufacturer name from
 * FRU information.
 * The file manufacturer returns the value as returned from the FRU.
 * NOTE: This is only available for certain server cards
 */

static ssize_t amdgpu_fru_manufacturer_name_show(struct device *dev,
						 struct device_attribute *attr,
						 char *buf)
{
	struct drm_device *ddev = dev_get_drvdata(dev);
	struct amdgpu_device *adev = drm_to_adev(ddev);

	return sysfs_emit(buf, "%s\n", adev->fru_info->manufacturer_name);
}

static DEVICE_ATTR(manufacturer, 0444, amdgpu_fru_manufacturer_name_show, NULL);

static const struct attribute *amdgpu_fru_attributes[] = {
	&dev_attr_product_name.attr,
	&dev_attr_product_number.attr,
	&dev_attr_serial_number.attr,
	&dev_attr_fru_id.attr,
	&dev_attr_manufacturer.attr,
	NULL
};

int amdgpu_fru_sysfs_init(struct amdgpu_device *adev)
{
	if (!is_fru_eeprom_supported(adev, NULL) || !adev->fru_info)
		return 0;

	return sysfs_create_files(&adev->dev->kobj, amdgpu_fru_attributes);
}

void amdgpu_fru_sysfs_fini(struct amdgpu_device *adev)
{
	if (!adev->fru_info)
		return;

	sysfs_remove_files(&adev->dev->kobj, amdgpu_fru_attributes);
}

/*
 * Copyright 2008 Advanced Micro Devices, Inc.
 * Copyright 2008 Red Hat Inc.
 * Copyright 2009 Jerome Glisse.
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
 * Authors: Dave Airlie
 *          Alex Deucher
 *          Jerome Glisse
 */
#include <drm/drmP.h>
#include "amdgpu.h"
#include "atom.h"

#include <linux/slab.h>
#include <linux/acpi.h>
/*
 * BIOS.
 */

#define AMD_VBIOS_SIGNATURE " 761295520"
#define AMD_VBIOS_SIGNATURE_OFFSET 0x30
#define AMD_VBIOS_SIGNATURE_SIZE sizeof(AMD_VBIOS_SIGNATURE)
#define AMD_VBIOS_SIGNATURE_END (AMD_VBIOS_SIGNATURE_OFFSET + AMD_VBIOS_SIGNATURE_SIZE)
#define AMD_IS_VALID_VBIOS(p) ((p)[0] == 0x55 && (p)[1] == 0xAA)
#define AMD_VBIOS_LENGTH(p) ((p)[2] << 9)

/* Check if current bios is an ATOM BIOS.
 * Return true if it is ATOM BIOS. Otherwise, return false.
 */
static bool check_atom_bios(uint8_t *bios, size_t size)
{
	uint16_t tmp, bios_header_start;

	if (!bios || size < 0x49) {
		DRM_INFO("vbios mem is null or mem size is wrong\n");
		return false;
	}

	if (!AMD_IS_VALID_VBIOS(bios)) {
		DRM_INFO("BIOS signature incorrect %x %x\n", bios[0], bios[1]);
		return false;
	}

	tmp = bios[0x18] | (bios[0x19] << 8);
	if (bios[tmp + 0x14] != 0x0) {
		DRM_INFO("Not an x86 BIOS ROM\n");
		return false;
	}

	bios_header_start = bios[0x48] | (bios[0x49] << 8);
	if (!bios_header_start) {
		DRM_INFO("Can't locate bios header\n");
		return false;
	}

	tmp = bios_header_start + 4;
	if (size < tmp) {
		DRM_INFO("BIOS header is broken\n");
		return false;
	}

	if (!memcmp(bios + tmp, "ATOM", 4) ||
	    !memcmp(bios + tmp, "MOTA", 4)) {
		DRM_DEBUG("ATOMBIOS detected\n");
		return true;
	}

	return false;
}

/* If you boot an IGP board with a discrete card as the primary,
 * the IGP rom is not accessible via the rom bar as the IGP rom is
 * part of the system bios.  On boot, the system bios puts a
 * copy of the igp rom at the start of vram if a discrete card is
 * present.
 */
static bool igp_read_bios_from_vram(struct amdgpu_device *adev)
{
	uint8_t __iomem *bios;
	resource_size_t vram_base;
	resource_size_t size = 256 * 1024; /* ??? */

	if (!(adev->flags & AMD_IS_APU))
		if (amdgpu_need_post(adev))
			return false;

	adev->bios = NULL;
	vram_base = pci_resource_start(adev->pdev, 0);
	bios = ioremap_wc(vram_base, size);
	if (!bios) {
		return false;
	}

	adev->bios = kmalloc(size, GFP_KERNEL);
	if (!adev->bios) {
		iounmap(bios);
		return false;
	}
	adev->bios_size = size;
	memcpy_fromio(adev->bios, bios, size);
	iounmap(bios);

	if (!check_atom_bios(adev->bios, size)) {
		kfree(adev->bios);
		return false;
	}

	return true;
}

bool amdgpu_read_bios(struct amdgpu_device *adev)
{
	uint8_t __iomem *bios;
	size_t size;

	adev->bios = NULL;
	/* XXX: some cards may return 0 for rom size? ddx has a workaround */
	bios = pci_map_rom(adev->pdev, &size);
	if (!bios) {
		return false;
	}

	adev->bios = kzalloc(size, GFP_KERNEL);
	if (adev->bios == NULL) {
		pci_unmap_rom(adev->pdev, bios);
		return false;
	}
	adev->bios_size = size;
	memcpy_fromio(adev->bios, bios, size);
	pci_unmap_rom(adev->pdev, bios);

	if (!check_atom_bios(adev->bios, size)) {
		kfree(adev->bios);
		return false;
	}

	return true;
}

static bool amdgpu_read_bios_from_rom(struct amdgpu_device *adev)
{
	u8 header[AMD_VBIOS_SIGNATURE_END+1] = {0};
	int len;

	if (!adev->asic_funcs->read_bios_from_rom)
		return false;

	/* validate VBIOS signature */
	if (amdgpu_asic_read_bios_from_rom(adev, &header[0], sizeof(header)) == false)
		return false;
	header[AMD_VBIOS_SIGNATURE_END] = 0;

	if ((!AMD_IS_VALID_VBIOS(header)) ||
	    0 != memcmp((char *)&header[AMD_VBIOS_SIGNATURE_OFFSET],
			AMD_VBIOS_SIGNATURE,
			strlen(AMD_VBIOS_SIGNATURE)))
		return false;

	/* valid vbios, go on */
	len = AMD_VBIOS_LENGTH(header);
	len = ALIGN(len, 4);
	adev->bios = kmalloc(len, GFP_KERNEL);
	if (!adev->bios) {
		DRM_ERROR("no memory to allocate for BIOS\n");
		return false;
	}
	adev->bios_size = len;

	/* read complete BIOS */
	amdgpu_asic_read_bios_from_rom(adev, adev->bios, len);

	if (!check_atom_bios(adev->bios, len)) {
		kfree(adev->bios);
		return false;
	}

	return true;
}

static bool amdgpu_read_platform_bios(struct amdgpu_device *adev)
{
	uint8_t __iomem *bios;
	size_t size;

	adev->bios = NULL;

	bios = pci_platform_rom(adev->pdev, &size);
	if (!bios) {
		return false;
	}

	adev->bios = kzalloc(size, GFP_KERNEL);
	if (adev->bios == NULL)
		return false;

	memcpy_fromio(adev->bios, bios, size);

	if (!check_atom_bios(adev->bios, size)) {
		kfree(adev->bios);
		return false;
	}

	adev->bios_size = size;

	return true;
}

#ifdef CONFIG_ACPI
/* ATRM is used to get the BIOS on the discrete cards in
 * dual-gpu systems.
 */
/* retrieve the ROM in 4k blocks */
#define ATRM_BIOS_PAGE 4096
/**
 * amdgpu_atrm_call - fetch a chunk of the vbios
 *
 * @atrm_handle: acpi ATRM handle
 * @bios: vbios image pointer
 * @offset: offset of vbios image data to fetch
 * @len: length of vbios image data to fetch
 *
 * Executes ATRM to fetch a chunk of the discrete
 * vbios image on PX systems (all asics).
 * Returns the length of the buffer fetched.
 */
static int amdgpu_atrm_call(acpi_handle atrm_handle, uint8_t *bios,
			    int offset, int len)
{
	acpi_status status;
	union acpi_object atrm_arg_elements[2], *obj;
	struct acpi_object_list atrm_arg;
	struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL};

	atrm_arg.count = 2;
	atrm_arg.pointer = &atrm_arg_elements[0];

	atrm_arg_elements[0].type = ACPI_TYPE_INTEGER;
	atrm_arg_elements[0].integer.value = offset;

	atrm_arg_elements[1].type = ACPI_TYPE_INTEGER;
	atrm_arg_elements[1].integer.value = len;

	status = acpi_evaluate_object(atrm_handle, NULL, &atrm_arg, &buffer);
	if (ACPI_FAILURE(status)) {
		printk("failed to evaluate ATRM got %s\n", acpi_format_exception(status));
		return -ENODEV;
	}

	obj = (union acpi_object *)buffer.pointer;
	memcpy(bios+offset, obj->buffer.pointer, obj->buffer.length);
	len = obj->buffer.length;
	kfree(buffer.pointer);
	return len;
}

static bool amdgpu_atrm_get_bios(struct amdgpu_device *adev)
{
	int ret;
	int size = 256 * 1024;
	int i;
	struct pci_dev *pdev = NULL;
	acpi_handle dhandle, atrm_handle;
	acpi_status status;
	bool found = false;

	/* ATRM is for the discrete card only */
	if (adev->flags & AMD_IS_APU)
		return false;

	while ((pdev = pci_get_class(PCI_CLASS_DISPLAY_VGA << 8, pdev)) != NULL) {
		dhandle = ACPI_HANDLE(&pdev->dev);
		if (!dhandle)
			continue;

		status = acpi_get_handle(dhandle, "ATRM", &atrm_handle);
		if (!ACPI_FAILURE(status)) {
			found = true;
			break;
		}
	}

	if (!found) {
		while ((pdev = pci_get_class(PCI_CLASS_DISPLAY_OTHER << 8, pdev)) != NULL) {
			dhandle = ACPI_HANDLE(&pdev->dev);
			if (!dhandle)
				continue;

			status = acpi_get_handle(dhandle, "ATRM", &atrm_handle);
			if (!ACPI_FAILURE(status)) {
				found = true;
				break;
			}
		}
	}

	if (!found)
		return false;

	adev->bios = kmalloc(size, GFP_KERNEL);
	if (!adev->bios) {
		DRM_ERROR("Unable to allocate bios\n");
		return false;
	}

	for (i = 0; i < size / ATRM_BIOS_PAGE; i++) {
		ret = amdgpu_atrm_call(atrm_handle,
				       adev->bios,
				       (i * ATRM_BIOS_PAGE),
				       ATRM_BIOS_PAGE);
		if (ret < ATRM_BIOS_PAGE)
			break;
	}

	if (!check_atom_bios(adev->bios, size)) {
		kfree(adev->bios);
		return false;
	}
	adev->bios_size = size;
	return true;
}
#else
static inline bool amdgpu_atrm_get_bios(struct amdgpu_device *adev)
{
	return false;
}
#endif

static bool amdgpu_read_disabled_bios(struct amdgpu_device *adev)
{
	if (adev->flags & AMD_IS_APU)
		return igp_read_bios_from_vram(adev);
	else
		return amdgpu_asic_read_disabled_bios(adev);
}

#ifdef CONFIG_ACPI
static bool amdgpu_acpi_vfct_bios(struct amdgpu_device *adev)
{
	struct acpi_table_header *hdr;
	acpi_size tbl_size;
	UEFI_ACPI_VFCT *vfct;
	unsigned offset;

	if (!ACPI_SUCCESS(acpi_get_table("VFCT", 1, &hdr)))
		return false;
	tbl_size = hdr->length;
	if (tbl_size < sizeof(UEFI_ACPI_VFCT)) {
		DRM_ERROR("ACPI VFCT table present but broken (too short #1)\n");
		return false;
	}

	vfct = (UEFI_ACPI_VFCT *)hdr;
	offset = vfct->VBIOSImageOffset;

	while (offset < tbl_size) {
		GOP_VBIOS_CONTENT *vbios = (GOP_VBIOS_CONTENT *)((char *)hdr + offset);
		VFCT_IMAGE_HEADER *vhdr = &vbios->VbiosHeader;

		offset += sizeof(VFCT_IMAGE_HEADER);
		if (offset > tbl_size) {
			DRM_ERROR("ACPI VFCT image header truncated\n");
			return false;
		}

		offset += vhdr->ImageLength;
		if (offset > tbl_size) {
			DRM_ERROR("ACPI VFCT image truncated\n");
			return false;
		}

		if (vhdr->ImageLength &&
		    vhdr->PCIBus == adev->pdev->bus->number &&
		    vhdr->PCIDevice == PCI_SLOT(adev->pdev->devfn) &&
		    vhdr->PCIFunction == PCI_FUNC(adev->pdev->devfn) &&
		    vhdr->VendorID == adev->pdev->vendor &&
		    vhdr->DeviceID == adev->pdev->device) {
			adev->bios = kmemdup(&vbios->VbiosContent,
					     vhdr->ImageLength,
					     GFP_KERNEL);

			if (!check_atom_bios(adev->bios, vhdr->ImageLength)) {
				kfree(adev->bios);
				return false;
			}
			adev->bios_size = vhdr->ImageLength;
			return true;
		}
	}

	DRM_ERROR("ACPI VFCT table present but broken (too short #2)\n");
	return false;
}
#else
static inline bool amdgpu_acpi_vfct_bios(struct amdgpu_device *adev)
{
	return false;
}
#endif

bool amdgpu_get_bios(struct amdgpu_device *adev)
{
	if (amdgpu_atrm_get_bios(adev))
		goto success;

	if (amdgpu_acpi_vfct_bios(adev))
		goto success;

	if (igp_read_bios_from_vram(adev))
		goto success;

	if (amdgpu_read_bios(adev))
		goto success;

	if (amdgpu_read_bios_from_rom(adev))
		goto success;

	if (amdgpu_read_disabled_bios(adev))
		goto success;

	if (amdgpu_read_platform_bios(adev))
		goto success;

	DRM_ERROR("Unable to locate a BIOS ROM\n");
	return false;

success:
	adev->is_atom_fw = (adev->asic_type >= CHIP_VEGA10) ? true : false;
	return true;
}

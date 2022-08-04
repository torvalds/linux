/*
 * Copyright 2020 Advanced Micro Devices, Inc.
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

#include <linux/debugfs.h>
#include <linux/firmware.h>
#include <linux/dma-mapping.h>

#include "amdgpu.h"
#include "amdgpu_fw_attestation.h"
#include "amdgpu_psp.h"
#include "amdgpu_ucode.h"
#include "soc15_common.h"

#define FW_ATTESTATION_DB_COOKIE        0x143b6a37
#define FW_ATTESTATION_RECORD_VALID  	1
#define FW_ATTESTATION_MAX_SIZE		4096

typedef struct FW_ATT_DB_HEADER
{
	uint32_t AttDbVersion;           /* version of the fwar feature */
	uint32_t AttDbCookie;            /* cookie as an extra check for corrupt data */
} FW_ATT_DB_HEADER;

typedef struct FW_ATT_RECORD
{
	uint16_t AttFwIdV1;              /* Legacy FW Type field */
	uint16_t AttFwIdV2;              /* V2 FW ID field */
	uint32_t AttFWVersion;           /* FW Version */
	uint16_t AttFWActiveFunctionID;  /* The VF ID (only in VF Attestation Table) */
	uint16_t AttSource;              /* FW source indicator */
	uint16_t RecordValid;            /* Indicates whether the record is a valid entry */
	uint8_t  AttFwTaId;              /* Ta ID (only in TA Attestation Table) */
	uint8_t  Reserved;
} FW_ATT_RECORD;

static ssize_t amdgpu_fw_attestation_debugfs_read(struct file *f,
						  char __user *buf,
						  size_t size,
						  loff_t *pos)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)file_inode(f)->i_private;
	uint64_t records_addr = 0;
	uint64_t vram_pos = 0;
	FW_ATT_DB_HEADER fw_att_hdr = {0};
	FW_ATT_RECORD fw_att_record = {0};

	if (size < sizeof(FW_ATT_RECORD)) {
		DRM_WARN("FW attestation input buffer not enough memory");
		return -EINVAL;
	}

	if ((*pos + sizeof(FW_ATT_DB_HEADER)) >= FW_ATTESTATION_MAX_SIZE) {
		DRM_WARN("FW attestation out of bounds");
		return 0;
	}

	if (psp_get_fw_attestation_records_addr(&adev->psp, &records_addr)) {
		DRM_WARN("Failed to get FW attestation record address");
		return -EINVAL;
	}

	vram_pos =  records_addr - adev->gmc.vram_start;

	if (*pos == 0) {
		amdgpu_device_vram_access(adev,
					  vram_pos,
					  (uint32_t*)&fw_att_hdr,
					  sizeof(FW_ATT_DB_HEADER),
					  false);

		if (fw_att_hdr.AttDbCookie != FW_ATTESTATION_DB_COOKIE) {
			DRM_WARN("Invalid FW attestation cookie");
			return -EINVAL;
		}

		DRM_INFO("FW attestation version = 0x%X", fw_att_hdr.AttDbVersion);
	}

	amdgpu_device_vram_access(adev,
				  vram_pos + sizeof(FW_ATT_DB_HEADER) + *pos,
				  (uint32_t*)&fw_att_record,
				  sizeof(FW_ATT_RECORD),
				  false);

	if (fw_att_record.RecordValid != FW_ATTESTATION_RECORD_VALID)
		return 0;

	if (copy_to_user(buf, (void*)&fw_att_record, sizeof(FW_ATT_RECORD)))
		return -EINVAL;

	*pos += sizeof(FW_ATT_RECORD);

	return sizeof(FW_ATT_RECORD);
}

static const struct file_operations amdgpu_fw_attestation_debugfs_ops = {
	.owner = THIS_MODULE,
	.read = amdgpu_fw_attestation_debugfs_read,
	.write = NULL,
	.llseek = default_llseek
};

static int amdgpu_is_fw_attestation_supported(struct amdgpu_device *adev)
{
	if (adev->asic_type >= CHIP_SIENNA_CICHLID)
		return 1;

	return 0;
}

void amdgpu_fw_attestation_debugfs_init(struct amdgpu_device *adev)
{
	if (!amdgpu_is_fw_attestation_supported(adev))
		return;

	debugfs_create_file("amdgpu_fw_attestation",
			    S_IRUSR,
			    adev_to_drm(adev)->primary->debugfs_root,
			    adev,
			    &amdgpu_fw_attestation_debugfs_ops);
}

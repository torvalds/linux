/*
 * Copyright 2016 Advanced Micro Devices, Inc.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 */

#include <linux/firmware.h>
#include <linux/module.h>
#include <linux/dmi.h>
#include <linux/pci.h>
#include <linux/debugfs.h>
#include <drm/drm_drv.h>

#include "amdgpu.h"
#include "amdgpu_pm.h"
#include "amdgpu_vcn.h"
#include "soc15d.h"

/* Firmware Names */
#define FIRMWARE_RAVEN		"amdgpu/raven_vcn.bin"
#define FIRMWARE_PICASSO	"amdgpu/picasso_vcn.bin"
#define FIRMWARE_RAVEN2		"amdgpu/raven2_vcn.bin"
#define FIRMWARE_ARCTURUS	"amdgpu/arcturus_vcn.bin"
#define FIRMWARE_RENOIR		"amdgpu/renoir_vcn.bin"
#define FIRMWARE_GREEN_SARDINE	"amdgpu/green_sardine_vcn.bin"
#define FIRMWARE_NAVI10		"amdgpu/navi10_vcn.bin"
#define FIRMWARE_NAVI14		"amdgpu/navi14_vcn.bin"
#define FIRMWARE_NAVI12		"amdgpu/navi12_vcn.bin"
#define FIRMWARE_SIENNA_CICHLID	"amdgpu/sienna_cichlid_vcn.bin"
#define FIRMWARE_NAVY_FLOUNDER	"amdgpu/navy_flounder_vcn.bin"
#define FIRMWARE_VANGOGH	"amdgpu/vangogh_vcn.bin"
#define FIRMWARE_DIMGREY_CAVEFISH	"amdgpu/dimgrey_cavefish_vcn.bin"
#define FIRMWARE_ALDEBARAN	"amdgpu/aldebaran_vcn.bin"
#define FIRMWARE_BEIGE_GOBY	"amdgpu/beige_goby_vcn.bin"
#define FIRMWARE_YELLOW_CARP	"amdgpu/yellow_carp_vcn.bin"
#define FIRMWARE_VCN_3_1_2	"amdgpu/vcn_3_1_2.bin"
#define FIRMWARE_VCN4_0_0	"amdgpu/vcn_4_0_0.bin"
#define FIRMWARE_VCN4_0_2	"amdgpu/vcn_4_0_2.bin"
#define FIRMWARE_VCN4_0_4      "amdgpu/vcn_4_0_4.bin"

MODULE_FIRMWARE(FIRMWARE_RAVEN);
MODULE_FIRMWARE(FIRMWARE_PICASSO);
MODULE_FIRMWARE(FIRMWARE_RAVEN2);
MODULE_FIRMWARE(FIRMWARE_ARCTURUS);
MODULE_FIRMWARE(FIRMWARE_RENOIR);
MODULE_FIRMWARE(FIRMWARE_GREEN_SARDINE);
MODULE_FIRMWARE(FIRMWARE_ALDEBARAN);
MODULE_FIRMWARE(FIRMWARE_NAVI10);
MODULE_FIRMWARE(FIRMWARE_NAVI14);
MODULE_FIRMWARE(FIRMWARE_NAVI12);
MODULE_FIRMWARE(FIRMWARE_SIENNA_CICHLID);
MODULE_FIRMWARE(FIRMWARE_NAVY_FLOUNDER);
MODULE_FIRMWARE(FIRMWARE_VANGOGH);
MODULE_FIRMWARE(FIRMWARE_DIMGREY_CAVEFISH);
MODULE_FIRMWARE(FIRMWARE_BEIGE_GOBY);
MODULE_FIRMWARE(FIRMWARE_YELLOW_CARP);
MODULE_FIRMWARE(FIRMWARE_VCN_3_1_2);
MODULE_FIRMWARE(FIRMWARE_VCN4_0_0);
MODULE_FIRMWARE(FIRMWARE_VCN4_0_2);
MODULE_FIRMWARE(FIRMWARE_VCN4_0_4);

static void amdgpu_vcn_idle_work_handler(struct work_struct *work);

int amdgpu_vcn_sw_init(struct amdgpu_device *adev)
{
	unsigned long bo_size;
	const char *fw_name;
	const char *bios_ver;
	const struct common_firmware_header *hdr;
	unsigned char fw_check;
	unsigned int fw_shared_size, log_offset;
	int i, r;

	INIT_DELAYED_WORK(&adev->vcn.idle_work, amdgpu_vcn_idle_work_handler);
	mutex_init(&adev->vcn.vcn_pg_lock);
	mutex_init(&adev->vcn.vcn1_jpeg1_workaround);
	atomic_set(&adev->vcn.total_submission_cnt, 0);
	for (i = 0; i < adev->vcn.num_vcn_inst; i++)
		atomic_set(&adev->vcn.inst[i].dpg_enc_submission_cnt, 0);

	switch (adev->ip_versions[UVD_HWIP][0]) {
	case IP_VERSION(1, 0, 0):
	case IP_VERSION(1, 0, 1):
		if (adev->apu_flags & AMD_APU_IS_RAVEN2)
			fw_name = FIRMWARE_RAVEN2;
		else if (adev->apu_flags & AMD_APU_IS_PICASSO)
			fw_name = FIRMWARE_PICASSO;
		else
			fw_name = FIRMWARE_RAVEN;
		break;
	case IP_VERSION(2, 5, 0):
		fw_name = FIRMWARE_ARCTURUS;
		if ((adev->firmware.load_type == AMDGPU_FW_LOAD_PSP) &&
		    (adev->pg_flags & AMD_PG_SUPPORT_VCN_DPG))
			adev->vcn.indirect_sram = true;
		break;
	case IP_VERSION(2, 2, 0):
		if (adev->apu_flags & AMD_APU_IS_RENOIR)
			fw_name = FIRMWARE_RENOIR;
		else
			fw_name = FIRMWARE_GREEN_SARDINE;

		if ((adev->firmware.load_type == AMDGPU_FW_LOAD_PSP) &&
		    (adev->pg_flags & AMD_PG_SUPPORT_VCN_DPG))
			adev->vcn.indirect_sram = true;
		break;
	case IP_VERSION(2, 6, 0):
		fw_name = FIRMWARE_ALDEBARAN;
		if ((adev->firmware.load_type == AMDGPU_FW_LOAD_PSP) &&
		    (adev->pg_flags & AMD_PG_SUPPORT_VCN_DPG))
			adev->vcn.indirect_sram = true;
		break;
	case IP_VERSION(2, 0, 0):
		fw_name = FIRMWARE_NAVI10;
		if ((adev->firmware.load_type == AMDGPU_FW_LOAD_PSP) &&
		    (adev->pg_flags & AMD_PG_SUPPORT_VCN_DPG))
			adev->vcn.indirect_sram = true;
		break;
	case IP_VERSION(2, 0, 2):
		if (adev->asic_type == CHIP_NAVI12)
			fw_name = FIRMWARE_NAVI12;
		else
			fw_name = FIRMWARE_NAVI14;
		if ((adev->firmware.load_type == AMDGPU_FW_LOAD_PSP) &&
		    (adev->pg_flags & AMD_PG_SUPPORT_VCN_DPG))
			adev->vcn.indirect_sram = true;
		break;
	case IP_VERSION(3, 0, 0):
	case IP_VERSION(3, 0, 64):
	case IP_VERSION(3, 0, 192):
		if (adev->ip_versions[GC_HWIP][0] == IP_VERSION(10, 3, 0))
			fw_name = FIRMWARE_SIENNA_CICHLID;
		else
			fw_name = FIRMWARE_NAVY_FLOUNDER;
		if ((adev->firmware.load_type == AMDGPU_FW_LOAD_PSP) &&
		    (adev->pg_flags & AMD_PG_SUPPORT_VCN_DPG))
			adev->vcn.indirect_sram = true;
		break;
	case IP_VERSION(3, 0, 2):
		fw_name = FIRMWARE_VANGOGH;
		if ((adev->firmware.load_type == AMDGPU_FW_LOAD_PSP) &&
		    (adev->pg_flags & AMD_PG_SUPPORT_VCN_DPG))
			adev->vcn.indirect_sram = true;
		/*
		 * Some Steam Deck's BIOS versions are incompatible with the
		 * indirect SRAM mode, leading to amdgpu being unable to get
		 * properly probed (and even potentially crashing the kernel).
		 * Hence, check for these versions here - notice this is
		 * restricted to Vangogh (Deck's APU).
		 */
		bios_ver = dmi_get_system_info(DMI_BIOS_VERSION);

		if (bios_ver && (!strncmp("F7A0113", bios_ver, 7) ||
		     !strncmp("F7A0114", bios_ver, 7))) {
			adev->vcn.indirect_sram = false;
			dev_info(adev->dev,
				"Steam Deck quirk: indirect SRAM disabled on BIOS %s\n", bios_ver);
		}
		break;
	case IP_VERSION(3, 0, 16):
		fw_name = FIRMWARE_DIMGREY_CAVEFISH;
		if ((adev->firmware.load_type == AMDGPU_FW_LOAD_PSP) &&
		    (adev->pg_flags & AMD_PG_SUPPORT_VCN_DPG))
			adev->vcn.indirect_sram = true;
		break;
	case IP_VERSION(3, 0, 33):
		fw_name = FIRMWARE_BEIGE_GOBY;
		if ((adev->firmware.load_type == AMDGPU_FW_LOAD_PSP) &&
		    (adev->pg_flags & AMD_PG_SUPPORT_VCN_DPG))
			adev->vcn.indirect_sram = true;
		break;
	case IP_VERSION(3, 1, 1):
		fw_name = FIRMWARE_YELLOW_CARP;
		if ((adev->firmware.load_type == AMDGPU_FW_LOAD_PSP) &&
		    (adev->pg_flags & AMD_PG_SUPPORT_VCN_DPG))
			adev->vcn.indirect_sram = true;
		break;
	case IP_VERSION(3, 1, 2):
		fw_name = FIRMWARE_VCN_3_1_2;
		if ((adev->firmware.load_type == AMDGPU_FW_LOAD_PSP) &&
		    (adev->pg_flags & AMD_PG_SUPPORT_VCN_DPG))
			adev->vcn.indirect_sram = true;
		break;
	case IP_VERSION(4, 0, 0):
		fw_name = FIRMWARE_VCN4_0_0;
		if ((adev->firmware.load_type == AMDGPU_FW_LOAD_PSP) &&
			(adev->pg_flags & AMD_PG_SUPPORT_VCN_DPG))
			adev->vcn.indirect_sram = true;
		break;
	case IP_VERSION(4, 0, 2):
		fw_name = FIRMWARE_VCN4_0_2;
		if ((adev->firmware.load_type == AMDGPU_FW_LOAD_PSP) &&
			(adev->pg_flags & AMD_PG_SUPPORT_VCN_DPG))
			adev->vcn.indirect_sram = true;
		break;
	case IP_VERSION(4, 0, 4):
		fw_name = FIRMWARE_VCN4_0_4;
		if ((adev->firmware.load_type == AMDGPU_FW_LOAD_PSP) &&
			(adev->pg_flags & AMD_PG_SUPPORT_VCN_DPG))
			adev->vcn.indirect_sram = true;
		break;
	default:
		return -EINVAL;
	}

	r = request_firmware(&adev->vcn.fw, fw_name, adev->dev);
	if (r) {
		dev_err(adev->dev, "amdgpu_vcn: Can't load firmware \"%s\"\n",
			fw_name);
		return r;
	}

	r = amdgpu_ucode_validate(adev->vcn.fw);
	if (r) {
		dev_err(adev->dev, "amdgpu_vcn: Can't validate firmware \"%s\"\n",
			fw_name);
		release_firmware(adev->vcn.fw);
		adev->vcn.fw = NULL;
		return r;
	}

	/* from vcn4 and above, only unified queue is used */
	adev->vcn.using_unified_queue =
		adev->ip_versions[UVD_HWIP][0] >= IP_VERSION(4, 0, 0);

	hdr = (const struct common_firmware_header *)adev->vcn.fw->data;
	adev->vcn.fw_version = le32_to_cpu(hdr->ucode_version);

	/* Bit 20-23, it is encode major and non-zero for new naming convention.
	 * This field is part of version minor and DRM_DISABLED_FLAG in old naming
	 * convention. Since the l:wq!atest version minor is 0x5B and DRM_DISABLED_FLAG
	 * is zero in old naming convention, this field is always zero so far.
	 * These four bits are used to tell which naming convention is present.
	 */
	fw_check = (le32_to_cpu(hdr->ucode_version) >> 20) & 0xf;
	if (fw_check) {
		unsigned int dec_ver, enc_major, enc_minor, vep, fw_rev;

		fw_rev = le32_to_cpu(hdr->ucode_version) & 0xfff;
		enc_minor = (le32_to_cpu(hdr->ucode_version) >> 12) & 0xff;
		enc_major = fw_check;
		dec_ver = (le32_to_cpu(hdr->ucode_version) >> 24) & 0xf;
		vep = (le32_to_cpu(hdr->ucode_version) >> 28) & 0xf;
		DRM_INFO("Found VCN firmware Version ENC: %u.%u DEC: %u VEP: %u Revision: %u\n",
			enc_major, enc_minor, dec_ver, vep, fw_rev);
	} else {
		unsigned int version_major, version_minor, family_id;

		family_id = le32_to_cpu(hdr->ucode_version) & 0xff;
		version_major = (le32_to_cpu(hdr->ucode_version) >> 24) & 0xff;
		version_minor = (le32_to_cpu(hdr->ucode_version) >> 8) & 0xff;
		DRM_INFO("Found VCN firmware Version: %u.%u Family ID: %u\n",
			version_major, version_minor, family_id);
	}

	bo_size = AMDGPU_VCN_STACK_SIZE + AMDGPU_VCN_CONTEXT_SIZE;
	if (adev->firmware.load_type != AMDGPU_FW_LOAD_PSP)
		bo_size += AMDGPU_GPU_PAGE_ALIGN(le32_to_cpu(hdr->ucode_size_bytes) + 8);

	if (adev->ip_versions[UVD_HWIP][0] >= IP_VERSION(4, 0, 0)){
		fw_shared_size = AMDGPU_GPU_PAGE_ALIGN(sizeof(struct amdgpu_vcn4_fw_shared));
		log_offset = offsetof(struct amdgpu_vcn4_fw_shared, fw_log);
	} else {
		fw_shared_size = AMDGPU_GPU_PAGE_ALIGN(sizeof(struct amdgpu_fw_shared));
		log_offset = offsetof(struct amdgpu_fw_shared, fw_log);
	}

	bo_size += fw_shared_size;

	if (amdgpu_vcnfw_log)
		bo_size += AMDGPU_VCNFW_LOG_SIZE;

	for (i = 0; i < adev->vcn.num_vcn_inst; i++) {
		if (adev->vcn.harvest_config & (1 << i))
			continue;

		r = amdgpu_bo_create_kernel(adev, bo_size, PAGE_SIZE,
						AMDGPU_GEM_DOMAIN_VRAM, &adev->vcn.inst[i].vcpu_bo,
						&adev->vcn.inst[i].gpu_addr, &adev->vcn.inst[i].cpu_addr);
		if (r) {
			dev_err(adev->dev, "(%d) failed to allocate vcn bo\n", r);
			return r;
		}

		adev->vcn.inst[i].fw_shared.cpu_addr = adev->vcn.inst[i].cpu_addr +
				bo_size - fw_shared_size;
		adev->vcn.inst[i].fw_shared.gpu_addr = adev->vcn.inst[i].gpu_addr +
				bo_size - fw_shared_size;

		adev->vcn.inst[i].fw_shared.mem_size = fw_shared_size;

		if (amdgpu_vcnfw_log) {
			adev->vcn.inst[i].fw_shared.cpu_addr -= AMDGPU_VCNFW_LOG_SIZE;
			adev->vcn.inst[i].fw_shared.gpu_addr -= AMDGPU_VCNFW_LOG_SIZE;
			adev->vcn.inst[i].fw_shared.log_offset = log_offset;
		}

		if (adev->vcn.indirect_sram) {
			r = amdgpu_bo_create_kernel(adev, 64 * 2 * 4, PAGE_SIZE,
					AMDGPU_GEM_DOMAIN_VRAM, &adev->vcn.inst[i].dpg_sram_bo,
					&adev->vcn.inst[i].dpg_sram_gpu_addr, &adev->vcn.inst[i].dpg_sram_cpu_addr);
			if (r) {
				dev_err(adev->dev, "VCN %d (%d) failed to allocate DPG bo\n", i, r);
				return r;
			}
		}
	}

	return 0;
}

int amdgpu_vcn_sw_fini(struct amdgpu_device *adev)
{
	int i, j;

	for (j = 0; j < adev->vcn.num_vcn_inst; ++j) {
		if (adev->vcn.harvest_config & (1 << j))
			continue;

		if (adev->vcn.indirect_sram) {
			amdgpu_bo_free_kernel(&adev->vcn.inst[j].dpg_sram_bo,
						  &adev->vcn.inst[j].dpg_sram_gpu_addr,
						  (void **)&adev->vcn.inst[j].dpg_sram_cpu_addr);
		}
		kvfree(adev->vcn.inst[j].saved_bo);

		amdgpu_bo_free_kernel(&adev->vcn.inst[j].vcpu_bo,
					  &adev->vcn.inst[j].gpu_addr,
					  (void **)&adev->vcn.inst[j].cpu_addr);

		amdgpu_ring_fini(&adev->vcn.inst[j].ring_dec);

		for (i = 0; i < adev->vcn.num_enc_rings; ++i)
			amdgpu_ring_fini(&adev->vcn.inst[j].ring_enc[i]);
	}

	release_firmware(adev->vcn.fw);
	mutex_destroy(&adev->vcn.vcn1_jpeg1_workaround);
	mutex_destroy(&adev->vcn.vcn_pg_lock);

	return 0;
}

bool amdgpu_vcn_is_disabled_vcn(struct amdgpu_device *adev, enum vcn_ring_type type, uint32_t vcn_instance)
{
	bool ret = false;
	int vcn_config = adev->vcn.vcn_config[vcn_instance];

	if ((type == VCN_ENCODE_RING) && (vcn_config & VCN_BLOCK_ENCODE_DISABLE_MASK)) {
		ret = true;
	} else if ((type == VCN_DECODE_RING) && (vcn_config & VCN_BLOCK_DECODE_DISABLE_MASK)) {
		ret = true;
	} else if ((type == VCN_UNIFIED_RING) && (vcn_config & VCN_BLOCK_QUEUE_DISABLE_MASK)) {
		ret = true;
	}

	return ret;
}

int amdgpu_vcn_suspend(struct amdgpu_device *adev)
{
	unsigned size;
	void *ptr;
	int i, idx;

	bool in_ras_intr = amdgpu_ras_intr_triggered();

	cancel_delayed_work_sync(&adev->vcn.idle_work);

	/* err_event_athub will corrupt VCPU buffer, so we need to
	 * restore fw data and clear buffer in amdgpu_vcn_resume() */
	if (in_ras_intr)
		return 0;

	for (i = 0; i < adev->vcn.num_vcn_inst; ++i) {
		if (adev->vcn.harvest_config & (1 << i))
			continue;
		if (adev->vcn.inst[i].vcpu_bo == NULL)
			return 0;

		size = amdgpu_bo_size(adev->vcn.inst[i].vcpu_bo);
		ptr = adev->vcn.inst[i].cpu_addr;

		adev->vcn.inst[i].saved_bo = kvmalloc(size, GFP_KERNEL);
		if (!adev->vcn.inst[i].saved_bo)
			return -ENOMEM;

		if (drm_dev_enter(adev_to_drm(adev), &idx)) {
			memcpy_fromio(adev->vcn.inst[i].saved_bo, ptr, size);
			drm_dev_exit(idx);
		}
	}
	return 0;
}

int amdgpu_vcn_resume(struct amdgpu_device *adev)
{
	unsigned size;
	void *ptr;
	int i, idx;

	for (i = 0; i < adev->vcn.num_vcn_inst; ++i) {
		if (adev->vcn.harvest_config & (1 << i))
			continue;
		if (adev->vcn.inst[i].vcpu_bo == NULL)
			return -EINVAL;

		size = amdgpu_bo_size(adev->vcn.inst[i].vcpu_bo);
		ptr = adev->vcn.inst[i].cpu_addr;

		if (adev->vcn.inst[i].saved_bo != NULL) {
			if (drm_dev_enter(adev_to_drm(adev), &idx)) {
				memcpy_toio(ptr, adev->vcn.inst[i].saved_bo, size);
				drm_dev_exit(idx);
			}
			kvfree(adev->vcn.inst[i].saved_bo);
			adev->vcn.inst[i].saved_bo = NULL;
		} else {
			const struct common_firmware_header *hdr;
			unsigned offset;

			hdr = (const struct common_firmware_header *)adev->vcn.fw->data;
			if (adev->firmware.load_type != AMDGPU_FW_LOAD_PSP) {
				offset = le32_to_cpu(hdr->ucode_array_offset_bytes);
				if (drm_dev_enter(adev_to_drm(adev), &idx)) {
					memcpy_toio(adev->vcn.inst[i].cpu_addr, adev->vcn.fw->data + offset,
						    le32_to_cpu(hdr->ucode_size_bytes));
					drm_dev_exit(idx);
				}
				size -= le32_to_cpu(hdr->ucode_size_bytes);
				ptr += le32_to_cpu(hdr->ucode_size_bytes);
			}
			memset_io(ptr, 0, size);
		}
	}
	return 0;
}

static void amdgpu_vcn_idle_work_handler(struct work_struct *work)
{
	struct amdgpu_device *adev =
		container_of(work, struct amdgpu_device, vcn.idle_work.work);
	unsigned int fences = 0, fence[AMDGPU_MAX_VCN_INSTANCES] = {0};
	unsigned int i, j;
	int r = 0;

	for (j = 0; j < adev->vcn.num_vcn_inst; ++j) {
		if (adev->vcn.harvest_config & (1 << j))
			continue;

		for (i = 0; i < adev->vcn.num_enc_rings; ++i) {
			fence[j] += amdgpu_fence_count_emitted(&adev->vcn.inst[j].ring_enc[i]);
		}

		/* Only set DPG pause for VCN3 or below, VCN4 and above will be handled by FW */
		if (adev->pg_flags & AMD_PG_SUPPORT_VCN_DPG &&
		    !adev->vcn.using_unified_queue) {
			struct dpg_pause_state new_state;

			if (fence[j] ||
				unlikely(atomic_read(&adev->vcn.inst[j].dpg_enc_submission_cnt)))
				new_state.fw_based = VCN_DPG_STATE__PAUSE;
			else
				new_state.fw_based = VCN_DPG_STATE__UNPAUSE;

			adev->vcn.pause_dpg_mode(adev, j, &new_state);
		}

		fence[j] += amdgpu_fence_count_emitted(&adev->vcn.inst[j].ring_dec);
		fences += fence[j];
	}

	if (!fences && !atomic_read(&adev->vcn.total_submission_cnt)) {
		amdgpu_device_ip_set_powergating_state(adev, AMD_IP_BLOCK_TYPE_VCN,
		       AMD_PG_STATE_GATE);
		r = amdgpu_dpm_switch_power_profile(adev, PP_SMC_POWER_PROFILE_VIDEO,
				false);
		if (r)
			dev_warn(adev->dev, "(%d) failed to disable video power profile mode\n", r);
	} else {
		schedule_delayed_work(&adev->vcn.idle_work, VCN_IDLE_TIMEOUT);
	}
}

void amdgpu_vcn_ring_begin_use(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;
	int r = 0;

	atomic_inc(&adev->vcn.total_submission_cnt);

	if (!cancel_delayed_work_sync(&adev->vcn.idle_work)) {
		r = amdgpu_dpm_switch_power_profile(adev, PP_SMC_POWER_PROFILE_VIDEO,
				true);
		if (r)
			dev_warn(adev->dev, "(%d) failed to switch to video power profile mode\n", r);
	}

	mutex_lock(&adev->vcn.vcn_pg_lock);
	amdgpu_device_ip_set_powergating_state(adev, AMD_IP_BLOCK_TYPE_VCN,
	       AMD_PG_STATE_UNGATE);

	/* Only set DPG pause for VCN3 or below, VCN4 and above will be handled by FW */
	if (adev->pg_flags & AMD_PG_SUPPORT_VCN_DPG &&
	    !adev->vcn.using_unified_queue) {
		struct dpg_pause_state new_state;

		if (ring->funcs->type == AMDGPU_RING_TYPE_VCN_ENC) {
			atomic_inc(&adev->vcn.inst[ring->me].dpg_enc_submission_cnt);
			new_state.fw_based = VCN_DPG_STATE__PAUSE;
		} else {
			unsigned int fences = 0;
			unsigned int i;

			for (i = 0; i < adev->vcn.num_enc_rings; ++i)
				fences += amdgpu_fence_count_emitted(&adev->vcn.inst[ring->me].ring_enc[i]);

			if (fences || atomic_read(&adev->vcn.inst[ring->me].dpg_enc_submission_cnt))
				new_state.fw_based = VCN_DPG_STATE__PAUSE;
			else
				new_state.fw_based = VCN_DPG_STATE__UNPAUSE;
		}

		adev->vcn.pause_dpg_mode(adev, ring->me, &new_state);
	}
	mutex_unlock(&adev->vcn.vcn_pg_lock);
}

void amdgpu_vcn_ring_end_use(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;

	/* Only set DPG pause for VCN3 or below, VCN4 and above will be handled by FW */
	if (ring->adev->pg_flags & AMD_PG_SUPPORT_VCN_DPG &&
	    ring->funcs->type == AMDGPU_RING_TYPE_VCN_ENC &&
	    !adev->vcn.using_unified_queue)
		atomic_dec(&ring->adev->vcn.inst[ring->me].dpg_enc_submission_cnt);

	atomic_dec(&ring->adev->vcn.total_submission_cnt);

	schedule_delayed_work(&ring->adev->vcn.idle_work, VCN_IDLE_TIMEOUT);
}

int amdgpu_vcn_dec_ring_test_ring(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;
	uint32_t tmp = 0;
	unsigned i;
	int r;

	/* VCN in SRIOV does not support direct register read/write */
	if (amdgpu_sriov_vf(adev))
		return 0;

	WREG32(adev->vcn.inst[ring->me].external.scratch9, 0xCAFEDEAD);
	r = amdgpu_ring_alloc(ring, 3);
	if (r)
		return r;
	amdgpu_ring_write(ring, PACKET0(adev->vcn.internal.scratch9, 0));
	amdgpu_ring_write(ring, 0xDEADBEEF);
	amdgpu_ring_commit(ring);
	for (i = 0; i < adev->usec_timeout; i++) {
		tmp = RREG32(adev->vcn.inst[ring->me].external.scratch9);
		if (tmp == 0xDEADBEEF)
			break;
		udelay(1);
	}

	if (i >= adev->usec_timeout)
		r = -ETIMEDOUT;

	return r;
}

int amdgpu_vcn_dec_sw_ring_test_ring(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;
	uint32_t rptr;
	unsigned int i;
	int r;

	if (amdgpu_sriov_vf(adev))
		return 0;

	r = amdgpu_ring_alloc(ring, 16);
	if (r)
		return r;

	rptr = amdgpu_ring_get_rptr(ring);

	amdgpu_ring_write(ring, VCN_DEC_SW_CMD_END);
	amdgpu_ring_commit(ring);

	for (i = 0; i < adev->usec_timeout; i++) {
		if (amdgpu_ring_get_rptr(ring) != rptr)
			break;
		udelay(1);
	}

	if (i >= adev->usec_timeout)
		r = -ETIMEDOUT;

	return r;
}

static int amdgpu_vcn_dec_send_msg(struct amdgpu_ring *ring,
				   struct amdgpu_ib *ib_msg,
				   struct dma_fence **fence)
{
	struct amdgpu_device *adev = ring->adev;
	struct dma_fence *f = NULL;
	struct amdgpu_job *job;
	struct amdgpu_ib *ib;
	uint64_t addr = AMDGPU_GPU_PAGE_ALIGN(ib_msg->gpu_addr);
	int i, r;

	r = amdgpu_job_alloc_with_ib(adev, 64,
					AMDGPU_IB_POOL_DIRECT, &job);
	if (r)
		goto err;

	ib = &job->ibs[0];
	ib->ptr[0] = PACKET0(adev->vcn.internal.data0, 0);
	ib->ptr[1] = addr;
	ib->ptr[2] = PACKET0(adev->vcn.internal.data1, 0);
	ib->ptr[3] = addr >> 32;
	ib->ptr[4] = PACKET0(adev->vcn.internal.cmd, 0);
	ib->ptr[5] = 0;
	for (i = 6; i < 16; i += 2) {
		ib->ptr[i] = PACKET0(adev->vcn.internal.nop, 0);
		ib->ptr[i+1] = 0;
	}
	ib->length_dw = 16;

	r = amdgpu_job_submit_direct(job, ring, &f);
	if (r)
		goto err_free;

	amdgpu_ib_free(adev, ib_msg, f);

	if (fence)
		*fence = dma_fence_get(f);
	dma_fence_put(f);

	return 0;

err_free:
	amdgpu_job_free(job);
err:
	amdgpu_ib_free(adev, ib_msg, f);
	return r;
}

static int amdgpu_vcn_dec_get_create_msg(struct amdgpu_ring *ring, uint32_t handle,
		struct amdgpu_ib *ib)
{
	struct amdgpu_device *adev = ring->adev;
	uint32_t *msg;
	int r, i;

	memset(ib, 0, sizeof(*ib));
	r = amdgpu_ib_get(adev, NULL, AMDGPU_GPU_PAGE_SIZE * 2,
			AMDGPU_IB_POOL_DIRECT,
			ib);
	if (r)
		return r;

	msg = (uint32_t *)AMDGPU_GPU_PAGE_ALIGN((unsigned long)ib->ptr);
	msg[0] = cpu_to_le32(0x00000028);
	msg[1] = cpu_to_le32(0x00000038);
	msg[2] = cpu_to_le32(0x00000001);
	msg[3] = cpu_to_le32(0x00000000);
	msg[4] = cpu_to_le32(handle);
	msg[5] = cpu_to_le32(0x00000000);
	msg[6] = cpu_to_le32(0x00000001);
	msg[7] = cpu_to_le32(0x00000028);
	msg[8] = cpu_to_le32(0x00000010);
	msg[9] = cpu_to_le32(0x00000000);
	msg[10] = cpu_to_le32(0x00000007);
	msg[11] = cpu_to_le32(0x00000000);
	msg[12] = cpu_to_le32(0x00000780);
	msg[13] = cpu_to_le32(0x00000440);
	for (i = 14; i < 1024; ++i)
		msg[i] = cpu_to_le32(0x0);

	return 0;
}

static int amdgpu_vcn_dec_get_destroy_msg(struct amdgpu_ring *ring, uint32_t handle,
					  struct amdgpu_ib *ib)
{
	struct amdgpu_device *adev = ring->adev;
	uint32_t *msg;
	int r, i;

	memset(ib, 0, sizeof(*ib));
	r = amdgpu_ib_get(adev, NULL, AMDGPU_GPU_PAGE_SIZE * 2,
			AMDGPU_IB_POOL_DIRECT,
			ib);
	if (r)
		return r;

	msg = (uint32_t *)AMDGPU_GPU_PAGE_ALIGN((unsigned long)ib->ptr);
	msg[0] = cpu_to_le32(0x00000028);
	msg[1] = cpu_to_le32(0x00000018);
	msg[2] = cpu_to_le32(0x00000000);
	msg[3] = cpu_to_le32(0x00000002);
	msg[4] = cpu_to_le32(handle);
	msg[5] = cpu_to_le32(0x00000000);
	for (i = 6; i < 1024; ++i)
		msg[i] = cpu_to_le32(0x0);

	return 0;
}

int amdgpu_vcn_dec_ring_test_ib(struct amdgpu_ring *ring, long timeout)
{
	struct dma_fence *fence = NULL;
	struct amdgpu_ib ib;
	long r;

	r = amdgpu_vcn_dec_get_create_msg(ring, 1, &ib);
	if (r)
		goto error;

	r = amdgpu_vcn_dec_send_msg(ring, &ib, NULL);
	if (r)
		goto error;
	r = amdgpu_vcn_dec_get_destroy_msg(ring, 1, &ib);
	if (r)
		goto error;

	r = amdgpu_vcn_dec_send_msg(ring, &ib, &fence);
	if (r)
		goto error;

	r = dma_fence_wait_timeout(fence, false, timeout);
	if (r == 0)
		r = -ETIMEDOUT;
	else if (r > 0)
		r = 0;

	dma_fence_put(fence);
error:
	return r;
}

static uint32_t *amdgpu_vcn_unified_ring_ib_header(struct amdgpu_ib *ib,
						uint32_t ib_pack_in_dw, bool enc)
{
	uint32_t *ib_checksum;

	ib->ptr[ib->length_dw++] = 0x00000010; /* single queue checksum */
	ib->ptr[ib->length_dw++] = 0x30000002;
	ib_checksum = &ib->ptr[ib->length_dw++];
	ib->ptr[ib->length_dw++] = ib_pack_in_dw;

	ib->ptr[ib->length_dw++] = 0x00000010; /* engine info */
	ib->ptr[ib->length_dw++] = 0x30000001;
	ib->ptr[ib->length_dw++] = enc ? 0x2 : 0x3;
	ib->ptr[ib->length_dw++] = ib_pack_in_dw * sizeof(uint32_t);

	return ib_checksum;
}

static void amdgpu_vcn_unified_ring_ib_checksum(uint32_t **ib_checksum,
						uint32_t ib_pack_in_dw)
{
	uint32_t i;
	uint32_t checksum = 0;

	for (i = 0; i < ib_pack_in_dw; i++)
		checksum += *(*ib_checksum + 2 + i);

	**ib_checksum = checksum;
}

static int amdgpu_vcn_dec_sw_send_msg(struct amdgpu_ring *ring,
				      struct amdgpu_ib *ib_msg,
				      struct dma_fence **fence)
{
	struct amdgpu_vcn_decode_buffer *decode_buffer = NULL;
	unsigned int ib_size_dw = 64;
	struct amdgpu_device *adev = ring->adev;
	struct dma_fence *f = NULL;
	struct amdgpu_job *job;
	struct amdgpu_ib *ib;
	uint64_t addr = AMDGPU_GPU_PAGE_ALIGN(ib_msg->gpu_addr);
	uint32_t *ib_checksum;
	uint32_t ib_pack_in_dw;
	int i, r;

	if (adev->vcn.using_unified_queue)
		ib_size_dw += 8;

	r = amdgpu_job_alloc_with_ib(adev, ib_size_dw * 4,
				AMDGPU_IB_POOL_DIRECT, &job);
	if (r)
		goto err;

	ib = &job->ibs[0];
	ib->length_dw = 0;

	/* single queue headers */
	if (adev->vcn.using_unified_queue) {
		ib_pack_in_dw = sizeof(struct amdgpu_vcn_decode_buffer) / sizeof(uint32_t)
						+ 4 + 2; /* engine info + decoding ib in dw */
		ib_checksum = amdgpu_vcn_unified_ring_ib_header(ib, ib_pack_in_dw, false);
	}

	ib->ptr[ib->length_dw++] = sizeof(struct amdgpu_vcn_decode_buffer) + 8;
	ib->ptr[ib->length_dw++] = cpu_to_le32(AMDGPU_VCN_IB_FLAG_DECODE_BUFFER);
	decode_buffer = (struct amdgpu_vcn_decode_buffer *)&(ib->ptr[ib->length_dw]);
	ib->length_dw += sizeof(struct amdgpu_vcn_decode_buffer) / 4;
	memset(decode_buffer, 0, sizeof(struct amdgpu_vcn_decode_buffer));

	decode_buffer->valid_buf_flag |= cpu_to_le32(AMDGPU_VCN_CMD_FLAG_MSG_BUFFER);
	decode_buffer->msg_buffer_address_hi = cpu_to_le32(addr >> 32);
	decode_buffer->msg_buffer_address_lo = cpu_to_le32(addr);

	for (i = ib->length_dw; i < ib_size_dw; ++i)
		ib->ptr[i] = 0x0;

	if (adev->vcn.using_unified_queue)
		amdgpu_vcn_unified_ring_ib_checksum(&ib_checksum, ib_pack_in_dw);

	r = amdgpu_job_submit_direct(job, ring, &f);
	if (r)
		goto err_free;

	amdgpu_ib_free(adev, ib_msg, f);

	if (fence)
		*fence = dma_fence_get(f);
	dma_fence_put(f);

	return 0;

err_free:
	amdgpu_job_free(job);
err:
	amdgpu_ib_free(adev, ib_msg, f);
	return r;
}

int amdgpu_vcn_dec_sw_ring_test_ib(struct amdgpu_ring *ring, long timeout)
{
	struct dma_fence *fence = NULL;
	struct amdgpu_ib ib;
	long r;

	r = amdgpu_vcn_dec_get_create_msg(ring, 1, &ib);
	if (r)
		goto error;

	r = amdgpu_vcn_dec_sw_send_msg(ring, &ib, NULL);
	if (r)
		goto error;
	r = amdgpu_vcn_dec_get_destroy_msg(ring, 1, &ib);
	if (r)
		goto error;

	r = amdgpu_vcn_dec_sw_send_msg(ring, &ib, &fence);
	if (r)
		goto error;

	r = dma_fence_wait_timeout(fence, false, timeout);
	if (r == 0)
		r = -ETIMEDOUT;
	else if (r > 0)
		r = 0;

	dma_fence_put(fence);
error:
	return r;
}

int amdgpu_vcn_enc_ring_test_ring(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;
	uint32_t rptr;
	unsigned i;
	int r;

	if (amdgpu_sriov_vf(adev))
		return 0;

	r = amdgpu_ring_alloc(ring, 16);
	if (r)
		return r;

	rptr = amdgpu_ring_get_rptr(ring);

	amdgpu_ring_write(ring, VCN_ENC_CMD_END);
	amdgpu_ring_commit(ring);

	for (i = 0; i < adev->usec_timeout; i++) {
		if (amdgpu_ring_get_rptr(ring) != rptr)
			break;
		udelay(1);
	}

	if (i >= adev->usec_timeout)
		r = -ETIMEDOUT;

	return r;
}

static int amdgpu_vcn_enc_get_create_msg(struct amdgpu_ring *ring, uint32_t handle,
					 struct amdgpu_ib *ib_msg,
					 struct dma_fence **fence)
{
	unsigned int ib_size_dw = 16;
	struct amdgpu_device *adev = ring->adev;
	struct amdgpu_job *job;
	struct amdgpu_ib *ib;
	struct dma_fence *f = NULL;
	uint32_t *ib_checksum = NULL;
	uint64_t addr;
	int i, r;

	if (adev->vcn.using_unified_queue)
		ib_size_dw += 8;

	r = amdgpu_job_alloc_with_ib(ring->adev, ib_size_dw * 4,
					AMDGPU_IB_POOL_DIRECT, &job);
	if (r)
		return r;

	ib = &job->ibs[0];
	addr = AMDGPU_GPU_PAGE_ALIGN(ib_msg->gpu_addr);

	ib->length_dw = 0;

	if (adev->vcn.using_unified_queue)
		ib_checksum = amdgpu_vcn_unified_ring_ib_header(ib, 0x11, true);

	ib->ptr[ib->length_dw++] = 0x00000018;
	ib->ptr[ib->length_dw++] = 0x00000001; /* session info */
	ib->ptr[ib->length_dw++] = handle;
	ib->ptr[ib->length_dw++] = upper_32_bits(addr);
	ib->ptr[ib->length_dw++] = addr;
	ib->ptr[ib->length_dw++] = 0x0000000b;

	ib->ptr[ib->length_dw++] = 0x00000014;
	ib->ptr[ib->length_dw++] = 0x00000002; /* task info */
	ib->ptr[ib->length_dw++] = 0x0000001c;
	ib->ptr[ib->length_dw++] = 0x00000000;
	ib->ptr[ib->length_dw++] = 0x00000000;

	ib->ptr[ib->length_dw++] = 0x00000008;
	ib->ptr[ib->length_dw++] = 0x08000001; /* op initialize */

	for (i = ib->length_dw; i < ib_size_dw; ++i)
		ib->ptr[i] = 0x0;

	if (adev->vcn.using_unified_queue)
		amdgpu_vcn_unified_ring_ib_checksum(&ib_checksum, 0x11);

	r = amdgpu_job_submit_direct(job, ring, &f);
	if (r)
		goto err;

	if (fence)
		*fence = dma_fence_get(f);
	dma_fence_put(f);

	return 0;

err:
	amdgpu_job_free(job);
	return r;
}

static int amdgpu_vcn_enc_get_destroy_msg(struct amdgpu_ring *ring, uint32_t handle,
					  struct amdgpu_ib *ib_msg,
					  struct dma_fence **fence)
{
	unsigned int ib_size_dw = 16;
	struct amdgpu_device *adev = ring->adev;
	struct amdgpu_job *job;
	struct amdgpu_ib *ib;
	struct dma_fence *f = NULL;
	uint32_t *ib_checksum = NULL;
	uint64_t addr;
	int i, r;

	if (adev->vcn.using_unified_queue)
		ib_size_dw += 8;

	r = amdgpu_job_alloc_with_ib(ring->adev, ib_size_dw * 4,
					AMDGPU_IB_POOL_DIRECT, &job);
	if (r)
		return r;

	ib = &job->ibs[0];
	addr = AMDGPU_GPU_PAGE_ALIGN(ib_msg->gpu_addr);

	ib->length_dw = 0;

	if (adev->vcn.using_unified_queue)
		ib_checksum = amdgpu_vcn_unified_ring_ib_header(ib, 0x11, true);

	ib->ptr[ib->length_dw++] = 0x00000018;
	ib->ptr[ib->length_dw++] = 0x00000001;
	ib->ptr[ib->length_dw++] = handle;
	ib->ptr[ib->length_dw++] = upper_32_bits(addr);
	ib->ptr[ib->length_dw++] = addr;
	ib->ptr[ib->length_dw++] = 0x0000000b;

	ib->ptr[ib->length_dw++] = 0x00000014;
	ib->ptr[ib->length_dw++] = 0x00000002;
	ib->ptr[ib->length_dw++] = 0x0000001c;
	ib->ptr[ib->length_dw++] = 0x00000000;
	ib->ptr[ib->length_dw++] = 0x00000000;

	ib->ptr[ib->length_dw++] = 0x00000008;
	ib->ptr[ib->length_dw++] = 0x08000002; /* op close session */

	for (i = ib->length_dw; i < ib_size_dw; ++i)
		ib->ptr[i] = 0x0;

	if (adev->vcn.using_unified_queue)
		amdgpu_vcn_unified_ring_ib_checksum(&ib_checksum, 0x11);

	r = amdgpu_job_submit_direct(job, ring, &f);
	if (r)
		goto err;

	if (fence)
		*fence = dma_fence_get(f);
	dma_fence_put(f);

	return 0;

err:
	amdgpu_job_free(job);
	return r;
}

int amdgpu_vcn_enc_ring_test_ib(struct amdgpu_ring *ring, long timeout)
{
	struct amdgpu_device *adev = ring->adev;
	struct dma_fence *fence = NULL;
	struct amdgpu_ib ib;
	long r;

	memset(&ib, 0, sizeof(ib));
	r = amdgpu_ib_get(adev, NULL, (128 << 10) + AMDGPU_GPU_PAGE_SIZE,
			AMDGPU_IB_POOL_DIRECT,
			&ib);
	if (r)
		return r;

	r = amdgpu_vcn_enc_get_create_msg(ring, 1, &ib, NULL);
	if (r)
		goto error;

	r = amdgpu_vcn_enc_get_destroy_msg(ring, 1, &ib, &fence);
	if (r)
		goto error;

	r = dma_fence_wait_timeout(fence, false, timeout);
	if (r == 0)
		r = -ETIMEDOUT;
	else if (r > 0)
		r = 0;

error:
	amdgpu_ib_free(adev, &ib, fence);
	dma_fence_put(fence);

	return r;
}

int amdgpu_vcn_unified_ring_test_ib(struct amdgpu_ring *ring, long timeout)
{
	long r;

	r = amdgpu_vcn_enc_ring_test_ib(ring, timeout);
	if (r)
		goto error;

	r =  amdgpu_vcn_dec_sw_ring_test_ib(ring, timeout);

error:
	return r;
}

enum amdgpu_ring_priority_level amdgpu_vcn_get_enc_ring_prio(int ring)
{
	switch(ring) {
	case 0:
		return AMDGPU_RING_PRIO_0;
	case 1:
		return AMDGPU_RING_PRIO_1;
	case 2:
		return AMDGPU_RING_PRIO_2;
	default:
		return AMDGPU_RING_PRIO_0;
	}
}

void amdgpu_vcn_setup_ucode(struct amdgpu_device *adev)
{
	int i;
	unsigned int idx;

	if (adev->firmware.load_type == AMDGPU_FW_LOAD_PSP) {
		const struct common_firmware_header *hdr;
		hdr = (const struct common_firmware_header *)adev->vcn.fw->data;

		for (i = 0; i < adev->vcn.num_vcn_inst; i++) {
			if (adev->vcn.harvest_config & (1 << i))
				continue;
			/* currently only support 2 FW instances */
			if (i >= 2) {
				dev_info(adev->dev, "More then 2 VCN FW instances!\n");
				break;
			}
			idx = AMDGPU_UCODE_ID_VCN + i;
			adev->firmware.ucode[idx].ucode_id = idx;
			adev->firmware.ucode[idx].fw = adev->vcn.fw;
			adev->firmware.fw_size +=
				ALIGN(le32_to_cpu(hdr->ucode_size_bytes), PAGE_SIZE);
		}
		dev_info(adev->dev, "Will use PSP to load VCN firmware\n");
	}
}

/*
 * debugfs for mapping vcn firmware log buffer.
 */
#if defined(CONFIG_DEBUG_FS)
static ssize_t amdgpu_debugfs_vcn_fwlog_read(struct file *f, char __user *buf,
                                             size_t size, loff_t *pos)
{
	struct amdgpu_vcn_inst *vcn;
	void *log_buf;
	volatile struct amdgpu_vcn_fwlog *plog;
	unsigned int read_pos, write_pos, available, i, read_bytes = 0;
	unsigned int read_num[2] = {0};

	vcn = file_inode(f)->i_private;
	if (!vcn)
		return -ENODEV;

	if (!vcn->fw_shared.cpu_addr || !amdgpu_vcnfw_log)
		return -EFAULT;

	log_buf = vcn->fw_shared.cpu_addr + vcn->fw_shared.mem_size;

	plog = (volatile struct amdgpu_vcn_fwlog *)log_buf;
	read_pos = plog->rptr;
	write_pos = plog->wptr;

	if (read_pos > AMDGPU_VCNFW_LOG_SIZE || write_pos > AMDGPU_VCNFW_LOG_SIZE)
		return -EFAULT;

	if (!size || (read_pos == write_pos))
		return 0;

	if (write_pos > read_pos) {
		available = write_pos - read_pos;
		read_num[0] = min(size, (size_t)available);
	} else {
		read_num[0] = AMDGPU_VCNFW_LOG_SIZE - read_pos;
		available = read_num[0] + write_pos - plog->header_size;
		if (size > available)
			read_num[1] = write_pos - plog->header_size;
		else if (size > read_num[0])
			read_num[1] = size - read_num[0];
		else
			read_num[0] = size;
	}

	for (i = 0; i < 2; i++) {
		if (read_num[i]) {
			if (read_pos == AMDGPU_VCNFW_LOG_SIZE)
				read_pos = plog->header_size;
			if (read_num[i] == copy_to_user((buf + read_bytes),
			                                (log_buf + read_pos), read_num[i]))
				return -EFAULT;

			read_bytes += read_num[i];
			read_pos += read_num[i];
		}
	}

	plog->rptr = read_pos;
	*pos += read_bytes;
	return read_bytes;
}

static const struct file_operations amdgpu_debugfs_vcnfwlog_fops = {
	.owner = THIS_MODULE,
	.read = amdgpu_debugfs_vcn_fwlog_read,
	.llseek = default_llseek
};
#endif

void amdgpu_debugfs_vcn_fwlog_init(struct amdgpu_device *adev, uint8_t i,
                                   struct amdgpu_vcn_inst *vcn)
{
#if defined(CONFIG_DEBUG_FS)
	struct drm_minor *minor = adev_to_drm(adev)->primary;
	struct dentry *root = minor->debugfs_root;
	char name[32];

	sprintf(name, "amdgpu_vcn_%d_fwlog", i);
	debugfs_create_file_size(name, S_IFREG | S_IRUGO, root, vcn,
				 &amdgpu_debugfs_vcnfwlog_fops,
				 AMDGPU_VCNFW_LOG_SIZE);
#endif
}

void amdgpu_vcn_fwlog_init(struct amdgpu_vcn_inst *vcn)
{
#if defined(CONFIG_DEBUG_FS)
	volatile uint32_t *flag = vcn->fw_shared.cpu_addr;
	void *fw_log_cpu_addr = vcn->fw_shared.cpu_addr + vcn->fw_shared.mem_size;
	uint64_t fw_log_gpu_addr = vcn->fw_shared.gpu_addr + vcn->fw_shared.mem_size;
	volatile struct amdgpu_vcn_fwlog *log_buf = fw_log_cpu_addr;
	volatile struct amdgpu_fw_shared_fw_logging *fw_log = vcn->fw_shared.cpu_addr
                                                         + vcn->fw_shared.log_offset;
	*flag |= cpu_to_le32(AMDGPU_VCN_FW_LOGGING_FLAG);
	fw_log->is_enabled = 1;
	fw_log->addr_lo = cpu_to_le32(fw_log_gpu_addr & 0xFFFFFFFF);
	fw_log->addr_hi = cpu_to_le32(fw_log_gpu_addr >> 32);
	fw_log->size = cpu_to_le32(AMDGPU_VCNFW_LOG_SIZE);

	log_buf->header_size = sizeof(struct amdgpu_vcn_fwlog);
	log_buf->buffer_size = AMDGPU_VCNFW_LOG_SIZE;
	log_buf->rptr = log_buf->header_size;
	log_buf->wptr = log_buf->header_size;
	log_buf->wrapped = 0;
#endif
}

int amdgpu_vcn_process_poison_irq(struct amdgpu_device *adev,
				struct amdgpu_irq_src *source,
				struct amdgpu_iv_entry *entry)
{
	struct ras_common_if *ras_if = adev->vcn.ras_if;
	struct ras_dispatch_if ih_data = {
		.entry = entry,
	};

	if (!ras_if)
		return 0;

	ih_data.head = *ras_if;
	amdgpu_ras_interrupt_dispatch(adev, &ih_data);

	return 0;
}

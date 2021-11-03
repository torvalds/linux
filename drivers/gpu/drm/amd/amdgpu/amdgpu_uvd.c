/*
 * Copyright 2011 Advanced Micro Devices, Inc.
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
/*
 * Authors:
 *    Christian König <deathsimple@vodafone.de>
 */

#include <linux/firmware.h>
#include <linux/module.h>

#include <drm/drm.h>
#include <drm/drm_drv.h>

#include "amdgpu.h"
#include "amdgpu_pm.h"
#include "amdgpu_uvd.h"
#include "cikd.h"
#include "uvd/uvd_4_2_d.h"

#include "amdgpu_ras.h"

/* 1 second timeout */
#define UVD_IDLE_TIMEOUT	msecs_to_jiffies(1000)

/* Firmware versions for VI */
#define FW_1_65_10	((1 << 24) | (65 << 16) | (10 << 8))
#define FW_1_87_11	((1 << 24) | (87 << 16) | (11 << 8))
#define FW_1_87_12	((1 << 24) | (87 << 16) | (12 << 8))
#define FW_1_37_15	((1 << 24) | (37 << 16) | (15 << 8))

/* Polaris10/11 firmware version */
#define FW_1_66_16	((1 << 24) | (66 << 16) | (16 << 8))

/* Firmware Names */
#ifdef CONFIG_DRM_AMDGPU_SI
#define FIRMWARE_TAHITI		"amdgpu/tahiti_uvd.bin"
#define FIRMWARE_VERDE		"amdgpu/verde_uvd.bin"
#define FIRMWARE_PITCAIRN	"amdgpu/pitcairn_uvd.bin"
#define FIRMWARE_OLAND		"amdgpu/oland_uvd.bin"
#endif
#ifdef CONFIG_DRM_AMDGPU_CIK
#define FIRMWARE_BONAIRE	"amdgpu/bonaire_uvd.bin"
#define FIRMWARE_KABINI	"amdgpu/kabini_uvd.bin"
#define FIRMWARE_KAVERI	"amdgpu/kaveri_uvd.bin"
#define FIRMWARE_HAWAII	"amdgpu/hawaii_uvd.bin"
#define FIRMWARE_MULLINS	"amdgpu/mullins_uvd.bin"
#endif
#define FIRMWARE_TONGA		"amdgpu/tonga_uvd.bin"
#define FIRMWARE_CARRIZO	"amdgpu/carrizo_uvd.bin"
#define FIRMWARE_FIJI		"amdgpu/fiji_uvd.bin"
#define FIRMWARE_STONEY		"amdgpu/stoney_uvd.bin"
#define FIRMWARE_POLARIS10	"amdgpu/polaris10_uvd.bin"
#define FIRMWARE_POLARIS11	"amdgpu/polaris11_uvd.bin"
#define FIRMWARE_POLARIS12	"amdgpu/polaris12_uvd.bin"
#define FIRMWARE_VEGAM		"amdgpu/vegam_uvd.bin"

#define FIRMWARE_VEGA10		"amdgpu/vega10_uvd.bin"
#define FIRMWARE_VEGA12		"amdgpu/vega12_uvd.bin"
#define FIRMWARE_VEGA20		"amdgpu/vega20_uvd.bin"

/* These are common relative offsets for all asics, from uvd_7_0_offset.h,  */
#define UVD_GPCOM_VCPU_CMD		0x03c3
#define UVD_GPCOM_VCPU_DATA0	0x03c4
#define UVD_GPCOM_VCPU_DATA1	0x03c5
#define UVD_NO_OP				0x03ff
#define UVD_BASE_SI				0x3800

/*
 * amdgpu_uvd_cs_ctx - Command submission parser context
 *
 * Used for emulating virtual memory support on UVD 4.2.
 */
struct amdgpu_uvd_cs_ctx {
	struct amdgpu_cs_parser *parser;
	unsigned reg, count;
	unsigned data0, data1;
	unsigned idx;
	unsigned ib_idx;

	/* does the IB has a msg command */
	bool has_msg_cmd;

	/* minimum buffer sizes */
	unsigned *buf_sizes;
};

#ifdef CONFIG_DRM_AMDGPU_SI
MODULE_FIRMWARE(FIRMWARE_TAHITI);
MODULE_FIRMWARE(FIRMWARE_VERDE);
MODULE_FIRMWARE(FIRMWARE_PITCAIRN);
MODULE_FIRMWARE(FIRMWARE_OLAND);
#endif
#ifdef CONFIG_DRM_AMDGPU_CIK
MODULE_FIRMWARE(FIRMWARE_BONAIRE);
MODULE_FIRMWARE(FIRMWARE_KABINI);
MODULE_FIRMWARE(FIRMWARE_KAVERI);
MODULE_FIRMWARE(FIRMWARE_HAWAII);
MODULE_FIRMWARE(FIRMWARE_MULLINS);
#endif
MODULE_FIRMWARE(FIRMWARE_TONGA);
MODULE_FIRMWARE(FIRMWARE_CARRIZO);
MODULE_FIRMWARE(FIRMWARE_FIJI);
MODULE_FIRMWARE(FIRMWARE_STONEY);
MODULE_FIRMWARE(FIRMWARE_POLARIS10);
MODULE_FIRMWARE(FIRMWARE_POLARIS11);
MODULE_FIRMWARE(FIRMWARE_POLARIS12);
MODULE_FIRMWARE(FIRMWARE_VEGAM);

MODULE_FIRMWARE(FIRMWARE_VEGA10);
MODULE_FIRMWARE(FIRMWARE_VEGA12);
MODULE_FIRMWARE(FIRMWARE_VEGA20);

static void amdgpu_uvd_idle_work_handler(struct work_struct *work);
static void amdgpu_uvd_force_into_uvd_segment(struct amdgpu_bo *abo);

static int amdgpu_uvd_create_msg_bo_helper(struct amdgpu_device *adev,
					   uint32_t size,
					   struct amdgpu_bo **bo_ptr)
{
	struct ttm_operation_ctx ctx = { true, false };
	struct amdgpu_bo *bo = NULL;
	void *addr;
	int r;

	r = amdgpu_bo_create_reserved(adev, size, PAGE_SIZE,
				      AMDGPU_GEM_DOMAIN_GTT,
				      &bo, NULL, &addr);
	if (r)
		return r;

	if (adev->uvd.address_64_bit)
		goto succ;

	amdgpu_bo_kunmap(bo);
	amdgpu_bo_unpin(bo);
	amdgpu_bo_placement_from_domain(bo, AMDGPU_GEM_DOMAIN_VRAM);
	amdgpu_uvd_force_into_uvd_segment(bo);
	r = ttm_bo_validate(&bo->tbo, &bo->placement, &ctx);
	if (r)
		goto err;
	r = amdgpu_bo_pin(bo, AMDGPU_GEM_DOMAIN_VRAM);
	if (r)
		goto err_pin;
	r = amdgpu_bo_kmap(bo, &addr);
	if (r)
		goto err_kmap;
succ:
	amdgpu_bo_unreserve(bo);
	*bo_ptr = bo;
	return 0;
err_kmap:
	amdgpu_bo_unpin(bo);
err_pin:
err:
	amdgpu_bo_unreserve(bo);
	amdgpu_bo_unref(&bo);
	return r;
}

int amdgpu_uvd_sw_init(struct amdgpu_device *adev)
{
	unsigned long bo_size;
	const char *fw_name;
	const struct common_firmware_header *hdr;
	unsigned family_id;
	int i, j, r;

	INIT_DELAYED_WORK(&adev->uvd.idle_work, amdgpu_uvd_idle_work_handler);

	switch (adev->asic_type) {
#ifdef CONFIG_DRM_AMDGPU_SI
	case CHIP_TAHITI:
		fw_name = FIRMWARE_TAHITI;
		break;
	case CHIP_VERDE:
		fw_name = FIRMWARE_VERDE;
		break;
	case CHIP_PITCAIRN:
		fw_name = FIRMWARE_PITCAIRN;
		break;
	case CHIP_OLAND:
		fw_name = FIRMWARE_OLAND;
		break;
#endif
#ifdef CONFIG_DRM_AMDGPU_CIK
	case CHIP_BONAIRE:
		fw_name = FIRMWARE_BONAIRE;
		break;
	case CHIP_KABINI:
		fw_name = FIRMWARE_KABINI;
		break;
	case CHIP_KAVERI:
		fw_name = FIRMWARE_KAVERI;
		break;
	case CHIP_HAWAII:
		fw_name = FIRMWARE_HAWAII;
		break;
	case CHIP_MULLINS:
		fw_name = FIRMWARE_MULLINS;
		break;
#endif
	case CHIP_TONGA:
		fw_name = FIRMWARE_TONGA;
		break;
	case CHIP_FIJI:
		fw_name = FIRMWARE_FIJI;
		break;
	case CHIP_CARRIZO:
		fw_name = FIRMWARE_CARRIZO;
		break;
	case CHIP_STONEY:
		fw_name = FIRMWARE_STONEY;
		break;
	case CHIP_POLARIS10:
		fw_name = FIRMWARE_POLARIS10;
		break;
	case CHIP_POLARIS11:
		fw_name = FIRMWARE_POLARIS11;
		break;
	case CHIP_POLARIS12:
		fw_name = FIRMWARE_POLARIS12;
		break;
	case CHIP_VEGA10:
		fw_name = FIRMWARE_VEGA10;
		break;
	case CHIP_VEGA12:
		fw_name = FIRMWARE_VEGA12;
		break;
	case CHIP_VEGAM:
		fw_name = FIRMWARE_VEGAM;
		break;
	case CHIP_VEGA20:
		fw_name = FIRMWARE_VEGA20;
		break;
	default:
		return -EINVAL;
	}

	r = request_firmware(&adev->uvd.fw, fw_name, adev->dev);
	if (r) {
		dev_err(adev->dev, "amdgpu_uvd: Can't load firmware \"%s\"\n",
			fw_name);
		return r;
	}

	r = amdgpu_ucode_validate(adev->uvd.fw);
	if (r) {
		dev_err(adev->dev, "amdgpu_uvd: Can't validate firmware \"%s\"\n",
			fw_name);
		release_firmware(adev->uvd.fw);
		adev->uvd.fw = NULL;
		return r;
	}

	/* Set the default UVD handles that the firmware can handle */
	adev->uvd.max_handles = AMDGPU_DEFAULT_UVD_HANDLES;

	hdr = (const struct common_firmware_header *)adev->uvd.fw->data;
	family_id = le32_to_cpu(hdr->ucode_version) & 0xff;

	if (adev->asic_type < CHIP_VEGA20) {
		unsigned version_major, version_minor;

		version_major = (le32_to_cpu(hdr->ucode_version) >> 24) & 0xff;
		version_minor = (le32_to_cpu(hdr->ucode_version) >> 8) & 0xff;
		DRM_INFO("Found UVD firmware Version: %u.%u Family ID: %u\n",
			version_major, version_minor, family_id);

		/*
		 * Limit the number of UVD handles depending on microcode major
		 * and minor versions. The firmware version which has 40 UVD
		 * instances support is 1.80. So all subsequent versions should
		 * also have the same support.
		 */
		if ((version_major > 0x01) ||
		    ((version_major == 0x01) && (version_minor >= 0x50)))
			adev->uvd.max_handles = AMDGPU_MAX_UVD_HANDLES;

		adev->uvd.fw_version = ((version_major << 24) | (version_minor << 16) |
					(family_id << 8));

		if ((adev->asic_type == CHIP_POLARIS10 ||
		     adev->asic_type == CHIP_POLARIS11) &&
		    (adev->uvd.fw_version < FW_1_66_16))
			DRM_ERROR("POLARIS10/11 UVD firmware version %u.%u is too old.\n",
				  version_major, version_minor);
	} else {
		unsigned int enc_major, enc_minor, dec_minor;

		dec_minor = (le32_to_cpu(hdr->ucode_version) >> 8) & 0xff;
		enc_minor = (le32_to_cpu(hdr->ucode_version) >> 24) & 0x3f;
		enc_major = (le32_to_cpu(hdr->ucode_version) >> 30) & 0x3;
		DRM_INFO("Found UVD firmware ENC: %u.%u DEC: .%u Family ID: %u\n",
			enc_major, enc_minor, dec_minor, family_id);

		adev->uvd.max_handles = AMDGPU_MAX_UVD_HANDLES;

		adev->uvd.fw_version = le32_to_cpu(hdr->ucode_version);
	}

	bo_size = AMDGPU_UVD_STACK_SIZE + AMDGPU_UVD_HEAP_SIZE
		  +  AMDGPU_UVD_SESSION_SIZE * adev->uvd.max_handles;
	if (adev->firmware.load_type != AMDGPU_FW_LOAD_PSP)
		bo_size += AMDGPU_GPU_PAGE_ALIGN(le32_to_cpu(hdr->ucode_size_bytes) + 8);

	for (j = 0; j < adev->uvd.num_uvd_inst; j++) {
		if (adev->uvd.harvest_config & (1 << j))
			continue;
		r = amdgpu_bo_create_kernel(adev, bo_size, PAGE_SIZE,
					    AMDGPU_GEM_DOMAIN_VRAM, &adev->uvd.inst[j].vcpu_bo,
					    &adev->uvd.inst[j].gpu_addr, &adev->uvd.inst[j].cpu_addr);
		if (r) {
			dev_err(adev->dev, "(%d) failed to allocate UVD bo\n", r);
			return r;
		}
	}

	for (i = 0; i < adev->uvd.max_handles; ++i) {
		atomic_set(&adev->uvd.handles[i], 0);
		adev->uvd.filp[i] = NULL;
	}

	/* from uvd v5.0 HW addressing capacity increased to 64 bits */
	if (!amdgpu_device_ip_block_version_cmp(adev, AMD_IP_BLOCK_TYPE_UVD, 5, 0))
		adev->uvd.address_64_bit = true;

	r = amdgpu_uvd_create_msg_bo_helper(adev, 128 << 10, &adev->uvd.ib_bo);
	if (r)
		return r;

	switch (adev->asic_type) {
	case CHIP_TONGA:
		adev->uvd.use_ctx_buf = adev->uvd.fw_version >= FW_1_65_10;
		break;
	case CHIP_CARRIZO:
		adev->uvd.use_ctx_buf = adev->uvd.fw_version >= FW_1_87_11;
		break;
	case CHIP_FIJI:
		adev->uvd.use_ctx_buf = adev->uvd.fw_version >= FW_1_87_12;
		break;
	case CHIP_STONEY:
		adev->uvd.use_ctx_buf = adev->uvd.fw_version >= FW_1_37_15;
		break;
	default:
		adev->uvd.use_ctx_buf = adev->asic_type >= CHIP_POLARIS10;
	}

	return 0;
}

int amdgpu_uvd_sw_fini(struct amdgpu_device *adev)
{
	void *addr = amdgpu_bo_kptr(adev->uvd.ib_bo);
	int i, j;

	drm_sched_entity_destroy(&adev->uvd.entity);

	for (j = 0; j < adev->uvd.num_uvd_inst; ++j) {
		if (adev->uvd.harvest_config & (1 << j))
			continue;
		kvfree(adev->uvd.inst[j].saved_bo);

		amdgpu_bo_free_kernel(&adev->uvd.inst[j].vcpu_bo,
				      &adev->uvd.inst[j].gpu_addr,
				      (void **)&adev->uvd.inst[j].cpu_addr);

		amdgpu_ring_fini(&adev->uvd.inst[j].ring);

		for (i = 0; i < AMDGPU_MAX_UVD_ENC_RINGS; ++i)
			amdgpu_ring_fini(&adev->uvd.inst[j].ring_enc[i]);
	}
	amdgpu_bo_free_kernel(&adev->uvd.ib_bo, NULL, &addr);
	release_firmware(adev->uvd.fw);

	return 0;
}

/**
 * amdgpu_uvd_entity_init - init entity
 *
 * @adev: amdgpu_device pointer
 *
 */
int amdgpu_uvd_entity_init(struct amdgpu_device *adev)
{
	struct amdgpu_ring *ring;
	struct drm_gpu_scheduler *sched;
	int r;

	ring = &adev->uvd.inst[0].ring;
	sched = &ring->sched;
	r = drm_sched_entity_init(&adev->uvd.entity, DRM_SCHED_PRIORITY_NORMAL,
				  &sched, 1, NULL);
	if (r) {
		DRM_ERROR("Failed setting up UVD kernel entity.\n");
		return r;
	}

	return 0;
}

int amdgpu_uvd_suspend(struct amdgpu_device *adev)
{
	unsigned size;
	void *ptr;
	int i, j, idx;
	bool in_ras_intr = amdgpu_ras_intr_triggered();

	cancel_delayed_work_sync(&adev->uvd.idle_work);

	/* only valid for physical mode */
	if (adev->asic_type < CHIP_POLARIS10) {
		for (i = 0; i < adev->uvd.max_handles; ++i)
			if (atomic_read(&adev->uvd.handles[i]))
				break;

		if (i == adev->uvd.max_handles)
			return 0;
	}

	for (j = 0; j < adev->uvd.num_uvd_inst; ++j) {
		if (adev->uvd.harvest_config & (1 << j))
			continue;
		if (adev->uvd.inst[j].vcpu_bo == NULL)
			continue;

		size = amdgpu_bo_size(adev->uvd.inst[j].vcpu_bo);
		ptr = adev->uvd.inst[j].cpu_addr;

		adev->uvd.inst[j].saved_bo = kvmalloc(size, GFP_KERNEL);
		if (!adev->uvd.inst[j].saved_bo)
			return -ENOMEM;

		if (drm_dev_enter(adev_to_drm(adev), &idx)) {
			/* re-write 0 since err_event_athub will corrupt VCPU buffer */
			if (in_ras_intr)
				memset(adev->uvd.inst[j].saved_bo, 0, size);
			else
				memcpy_fromio(adev->uvd.inst[j].saved_bo, ptr, size);

			drm_dev_exit(idx);
		}
	}

	if (in_ras_intr)
		DRM_WARN("UVD VCPU state may lost due to RAS ERREVENT_ATHUB_INTERRUPT\n");

	return 0;
}

int amdgpu_uvd_resume(struct amdgpu_device *adev)
{
	unsigned size;
	void *ptr;
	int i, idx;

	for (i = 0; i < adev->uvd.num_uvd_inst; i++) {
		if (adev->uvd.harvest_config & (1 << i))
			continue;
		if (adev->uvd.inst[i].vcpu_bo == NULL)
			return -EINVAL;

		size = amdgpu_bo_size(adev->uvd.inst[i].vcpu_bo);
		ptr = adev->uvd.inst[i].cpu_addr;

		if (adev->uvd.inst[i].saved_bo != NULL) {
			if (drm_dev_enter(adev_to_drm(adev), &idx)) {
				memcpy_toio(ptr, adev->uvd.inst[i].saved_bo, size);
				drm_dev_exit(idx);
			}
			kvfree(adev->uvd.inst[i].saved_bo);
			adev->uvd.inst[i].saved_bo = NULL;
		} else {
			const struct common_firmware_header *hdr;
			unsigned offset;

			hdr = (const struct common_firmware_header *)adev->uvd.fw->data;
			if (adev->firmware.load_type != AMDGPU_FW_LOAD_PSP) {
				offset = le32_to_cpu(hdr->ucode_array_offset_bytes);
				if (drm_dev_enter(adev_to_drm(adev), &idx)) {
					memcpy_toio(adev->uvd.inst[i].cpu_addr, adev->uvd.fw->data + offset,
						    le32_to_cpu(hdr->ucode_size_bytes));
					drm_dev_exit(idx);
				}
				size -= le32_to_cpu(hdr->ucode_size_bytes);
				ptr += le32_to_cpu(hdr->ucode_size_bytes);
			}
			memset_io(ptr, 0, size);
			/* to restore uvd fence seq */
			amdgpu_fence_driver_force_completion(&adev->uvd.inst[i].ring);
		}
	}
	return 0;
}

void amdgpu_uvd_free_handles(struct amdgpu_device *adev, struct drm_file *filp)
{
	struct amdgpu_ring *ring = &adev->uvd.inst[0].ring;
	int i, r;

	for (i = 0; i < adev->uvd.max_handles; ++i) {
		uint32_t handle = atomic_read(&adev->uvd.handles[i]);

		if (handle != 0 && adev->uvd.filp[i] == filp) {
			struct dma_fence *fence;

			r = amdgpu_uvd_get_destroy_msg(ring, handle, false,
						       &fence);
			if (r) {
				DRM_ERROR("Error destroying UVD %d!\n", r);
				continue;
			}

			dma_fence_wait(fence, false);
			dma_fence_put(fence);

			adev->uvd.filp[i] = NULL;
			atomic_set(&adev->uvd.handles[i], 0);
		}
	}
}

static void amdgpu_uvd_force_into_uvd_segment(struct amdgpu_bo *abo)
{
	int i;
	for (i = 0; i < abo->placement.num_placement; ++i) {
		abo->placements[i].fpfn = 0 >> PAGE_SHIFT;
		abo->placements[i].lpfn = (256 * 1024 * 1024) >> PAGE_SHIFT;
	}
}

static u64 amdgpu_uvd_get_addr_from_ctx(struct amdgpu_uvd_cs_ctx *ctx)
{
	uint32_t lo, hi;
	uint64_t addr;

	lo = amdgpu_get_ib_value(ctx->parser, ctx->ib_idx, ctx->data0);
	hi = amdgpu_get_ib_value(ctx->parser, ctx->ib_idx, ctx->data1);
	addr = ((uint64_t)lo) | (((uint64_t)hi) << 32);

	return addr;
}

/**
 * amdgpu_uvd_cs_pass1 - first parsing round
 *
 * @ctx: UVD parser context
 *
 * Make sure UVD message and feedback buffers are in VRAM and
 * nobody is violating an 256MB boundary.
 */
static int amdgpu_uvd_cs_pass1(struct amdgpu_uvd_cs_ctx *ctx)
{
	struct ttm_operation_ctx tctx = { false, false };
	struct amdgpu_bo_va_mapping *mapping;
	struct amdgpu_bo *bo;
	uint32_t cmd;
	uint64_t addr = amdgpu_uvd_get_addr_from_ctx(ctx);
	int r = 0;

	r = amdgpu_cs_find_mapping(ctx->parser, addr, &bo, &mapping);
	if (r) {
		DRM_ERROR("Can't find BO for addr 0x%08Lx\n", addr);
		return r;
	}

	if (!ctx->parser->adev->uvd.address_64_bit) {
		/* check if it's a message or feedback command */
		cmd = amdgpu_get_ib_value(ctx->parser, ctx->ib_idx, ctx->idx) >> 1;
		if (cmd == 0x0 || cmd == 0x3) {
			/* yes, force it into VRAM */
			uint32_t domain = AMDGPU_GEM_DOMAIN_VRAM;
			amdgpu_bo_placement_from_domain(bo, domain);
		}
		amdgpu_uvd_force_into_uvd_segment(bo);

		r = ttm_bo_validate(&bo->tbo, &bo->placement, &tctx);
	}

	return r;
}

/**
 * amdgpu_uvd_cs_msg_decode - handle UVD decode message
 *
 * @adev: amdgpu_device pointer
 * @msg: pointer to message structure
 * @buf_sizes: placeholder to put the different buffer lengths
 *
 * Peek into the decode message and calculate the necessary buffer sizes.
 */
static int amdgpu_uvd_cs_msg_decode(struct amdgpu_device *adev, uint32_t *msg,
	unsigned buf_sizes[])
{
	unsigned stream_type = msg[4];
	unsigned width = msg[6];
	unsigned height = msg[7];
	unsigned dpb_size = msg[9];
	unsigned pitch = msg[28];
	unsigned level = msg[57];

	unsigned width_in_mb = width / 16;
	unsigned height_in_mb = ALIGN(height / 16, 2);
	unsigned fs_in_mb = width_in_mb * height_in_mb;

	unsigned image_size, tmp, min_dpb_size, num_dpb_buffer;
	unsigned min_ctx_size = ~0;

	image_size = width * height;
	image_size += image_size / 2;
	image_size = ALIGN(image_size, 1024);

	switch (stream_type) {
	case 0: /* H264 */
		switch(level) {
		case 30:
			num_dpb_buffer = 8100 / fs_in_mb;
			break;
		case 31:
			num_dpb_buffer = 18000 / fs_in_mb;
			break;
		case 32:
			num_dpb_buffer = 20480 / fs_in_mb;
			break;
		case 41:
			num_dpb_buffer = 32768 / fs_in_mb;
			break;
		case 42:
			num_dpb_buffer = 34816 / fs_in_mb;
			break;
		case 50:
			num_dpb_buffer = 110400 / fs_in_mb;
			break;
		case 51:
			num_dpb_buffer = 184320 / fs_in_mb;
			break;
		default:
			num_dpb_buffer = 184320 / fs_in_mb;
			break;
		}
		num_dpb_buffer++;
		if (num_dpb_buffer > 17)
			num_dpb_buffer = 17;

		/* reference picture buffer */
		min_dpb_size = image_size * num_dpb_buffer;

		/* macroblock context buffer */
		min_dpb_size += width_in_mb * height_in_mb * num_dpb_buffer * 192;

		/* IT surface buffer */
		min_dpb_size += width_in_mb * height_in_mb * 32;
		break;

	case 1: /* VC1 */

		/* reference picture buffer */
		min_dpb_size = image_size * 3;

		/* CONTEXT_BUFFER */
		min_dpb_size += width_in_mb * height_in_mb * 128;

		/* IT surface buffer */
		min_dpb_size += width_in_mb * 64;

		/* DB surface buffer */
		min_dpb_size += width_in_mb * 128;

		/* BP */
		tmp = max(width_in_mb, height_in_mb);
		min_dpb_size += ALIGN(tmp * 7 * 16, 64);
		break;

	case 3: /* MPEG2 */

		/* reference picture buffer */
		min_dpb_size = image_size * 3;
		break;

	case 4: /* MPEG4 */

		/* reference picture buffer */
		min_dpb_size = image_size * 3;

		/* CM */
		min_dpb_size += width_in_mb * height_in_mb * 64;

		/* IT surface buffer */
		min_dpb_size += ALIGN(width_in_mb * height_in_mb * 32, 64);
		break;

	case 7: /* H264 Perf */
		switch(level) {
		case 30:
			num_dpb_buffer = 8100 / fs_in_mb;
			break;
		case 31:
			num_dpb_buffer = 18000 / fs_in_mb;
			break;
		case 32:
			num_dpb_buffer = 20480 / fs_in_mb;
			break;
		case 41:
			num_dpb_buffer = 32768 / fs_in_mb;
			break;
		case 42:
			num_dpb_buffer = 34816 / fs_in_mb;
			break;
		case 50:
			num_dpb_buffer = 110400 / fs_in_mb;
			break;
		case 51:
			num_dpb_buffer = 184320 / fs_in_mb;
			break;
		default:
			num_dpb_buffer = 184320 / fs_in_mb;
			break;
		}
		num_dpb_buffer++;
		if (num_dpb_buffer > 17)
			num_dpb_buffer = 17;

		/* reference picture buffer */
		min_dpb_size = image_size * num_dpb_buffer;

		if (!adev->uvd.use_ctx_buf){
			/* macroblock context buffer */
			min_dpb_size +=
				width_in_mb * height_in_mb * num_dpb_buffer * 192;

			/* IT surface buffer */
			min_dpb_size += width_in_mb * height_in_mb * 32;
		} else {
			/* macroblock context buffer */
			min_ctx_size =
				width_in_mb * height_in_mb * num_dpb_buffer * 192;
		}
		break;

	case 8: /* MJPEG */
		min_dpb_size = 0;
		break;

	case 16: /* H265 */
		image_size = (ALIGN(width, 16) * ALIGN(height, 16) * 3) / 2;
		image_size = ALIGN(image_size, 256);

		num_dpb_buffer = (le32_to_cpu(msg[59]) & 0xff) + 2;
		min_dpb_size = image_size * num_dpb_buffer;
		min_ctx_size = ((width + 255) / 16) * ((height + 255) / 16)
					   * 16 * num_dpb_buffer + 52 * 1024;
		break;

	default:
		DRM_ERROR("UVD codec not handled %d!\n", stream_type);
		return -EINVAL;
	}

	if (width > pitch) {
		DRM_ERROR("Invalid UVD decoding target pitch!\n");
		return -EINVAL;
	}

	if (dpb_size < min_dpb_size) {
		DRM_ERROR("Invalid dpb_size in UVD message (%d / %d)!\n",
			  dpb_size, min_dpb_size);
		return -EINVAL;
	}

	buf_sizes[0x1] = dpb_size;
	buf_sizes[0x2] = image_size;
	buf_sizes[0x4] = min_ctx_size;
	/* store image width to adjust nb memory pstate */
	adev->uvd.decode_image_width = width;
	return 0;
}

/**
 * amdgpu_uvd_cs_msg - handle UVD message
 *
 * @ctx: UVD parser context
 * @bo: buffer object containing the message
 * @offset: offset into the buffer object
 *
 * Peek into the UVD message and extract the session id.
 * Make sure that we don't open up to many sessions.
 */
static int amdgpu_uvd_cs_msg(struct amdgpu_uvd_cs_ctx *ctx,
			     struct amdgpu_bo *bo, unsigned offset)
{
	struct amdgpu_device *adev = ctx->parser->adev;
	int32_t *msg, msg_type, handle;
	void *ptr;
	long r;
	int i;

	if (offset & 0x3F) {
		DRM_ERROR("UVD messages must be 64 byte aligned!\n");
		return -EINVAL;
	}

	r = amdgpu_bo_kmap(bo, &ptr);
	if (r) {
		DRM_ERROR("Failed mapping the UVD) message (%ld)!\n", r);
		return r;
	}

	msg = ptr + offset;

	msg_type = msg[1];
	handle = msg[2];

	if (handle == 0) {
		DRM_ERROR("Invalid UVD handle!\n");
		return -EINVAL;
	}

	switch (msg_type) {
	case 0:
		/* it's a create msg, calc image size (width * height) */
		amdgpu_bo_kunmap(bo);

		/* try to alloc a new handle */
		for (i = 0; i < adev->uvd.max_handles; ++i) {
			if (atomic_read(&adev->uvd.handles[i]) == handle) {
				DRM_ERROR(")Handle 0x%x already in use!\n",
					  handle);
				return -EINVAL;
			}

			if (!atomic_cmpxchg(&adev->uvd.handles[i], 0, handle)) {
				adev->uvd.filp[i] = ctx->parser->filp;
				return 0;
			}
		}

		DRM_ERROR("No more free UVD handles!\n");
		return -ENOSPC;

	case 1:
		/* it's a decode msg, calc buffer sizes */
		r = amdgpu_uvd_cs_msg_decode(adev, msg, ctx->buf_sizes);
		amdgpu_bo_kunmap(bo);
		if (r)
			return r;

		/* validate the handle */
		for (i = 0; i < adev->uvd.max_handles; ++i) {
			if (atomic_read(&adev->uvd.handles[i]) == handle) {
				if (adev->uvd.filp[i] != ctx->parser->filp) {
					DRM_ERROR("UVD handle collision detected!\n");
					return -EINVAL;
				}
				return 0;
			}
		}

		DRM_ERROR("Invalid UVD handle 0x%x!\n", handle);
		return -ENOENT;

	case 2:
		/* it's a destroy msg, free the handle */
		for (i = 0; i < adev->uvd.max_handles; ++i)
			atomic_cmpxchg(&adev->uvd.handles[i], handle, 0);
		amdgpu_bo_kunmap(bo);
		return 0;

	default:
		DRM_ERROR("Illegal UVD message type (%d)!\n", msg_type);
	}

	return -EINVAL;
}

/**
 * amdgpu_uvd_cs_pass2 - second parsing round
 *
 * @ctx: UVD parser context
 *
 * Patch buffer addresses, make sure buffer sizes are correct.
 */
static int amdgpu_uvd_cs_pass2(struct amdgpu_uvd_cs_ctx *ctx)
{
	struct amdgpu_bo_va_mapping *mapping;
	struct amdgpu_bo *bo;
	uint32_t cmd;
	uint64_t start, end;
	uint64_t addr = amdgpu_uvd_get_addr_from_ctx(ctx);
	int r;

	r = amdgpu_cs_find_mapping(ctx->parser, addr, &bo, &mapping);
	if (r) {
		DRM_ERROR("Can't find BO for addr 0x%08Lx\n", addr);
		return r;
	}

	start = amdgpu_bo_gpu_offset(bo);

	end = (mapping->last + 1 - mapping->start);
	end = end * AMDGPU_GPU_PAGE_SIZE + start;

	addr -= mapping->start * AMDGPU_GPU_PAGE_SIZE;
	start += addr;

	amdgpu_set_ib_value(ctx->parser, ctx->ib_idx, ctx->data0,
			    lower_32_bits(start));
	amdgpu_set_ib_value(ctx->parser, ctx->ib_idx, ctx->data1,
			    upper_32_bits(start));

	cmd = amdgpu_get_ib_value(ctx->parser, ctx->ib_idx, ctx->idx) >> 1;
	if (cmd < 0x4) {
		if ((end - start) < ctx->buf_sizes[cmd]) {
			DRM_ERROR("buffer (%d) to small (%d / %d)!\n", cmd,
				  (unsigned)(end - start),
				  ctx->buf_sizes[cmd]);
			return -EINVAL;
		}

	} else if (cmd == 0x206) {
		if ((end - start) < ctx->buf_sizes[4]) {
			DRM_ERROR("buffer (%d) to small (%d / %d)!\n", cmd,
					  (unsigned)(end - start),
					  ctx->buf_sizes[4]);
			return -EINVAL;
		}
	} else if ((cmd != 0x100) && (cmd != 0x204)) {
		DRM_ERROR("invalid UVD command %X!\n", cmd);
		return -EINVAL;
	}

	if (!ctx->parser->adev->uvd.address_64_bit) {
		if ((start >> 28) != ((end - 1) >> 28)) {
			DRM_ERROR("reloc %LX-%LX crossing 256MB boundary!\n",
				  start, end);
			return -EINVAL;
		}

		if ((cmd == 0 || cmd == 0x3) &&
		    (start >> 28) != (ctx->parser->adev->uvd.inst->gpu_addr >> 28)) {
			DRM_ERROR("msg/fb buffer %LX-%LX out of 256MB segment!\n",
				  start, end);
			return -EINVAL;
		}
	}

	if (cmd == 0) {
		ctx->has_msg_cmd = true;
		r = amdgpu_uvd_cs_msg(ctx, bo, addr);
		if (r)
			return r;
	} else if (!ctx->has_msg_cmd) {
		DRM_ERROR("Message needed before other commands are send!\n");
		return -EINVAL;
	}

	return 0;
}

/**
 * amdgpu_uvd_cs_reg - parse register writes
 *
 * @ctx: UVD parser context
 * @cb: callback function
 *
 * Parse the register writes, call cb on each complete command.
 */
static int amdgpu_uvd_cs_reg(struct amdgpu_uvd_cs_ctx *ctx,
			     int (*cb)(struct amdgpu_uvd_cs_ctx *ctx))
{
	struct amdgpu_ib *ib = &ctx->parser->job->ibs[ctx->ib_idx];
	int i, r;

	ctx->idx++;
	for (i = 0; i <= ctx->count; ++i) {
		unsigned reg = ctx->reg + i;

		if (ctx->idx >= ib->length_dw) {
			DRM_ERROR("Register command after end of CS!\n");
			return -EINVAL;
		}

		switch (reg) {
		case mmUVD_GPCOM_VCPU_DATA0:
			ctx->data0 = ctx->idx;
			break;
		case mmUVD_GPCOM_VCPU_DATA1:
			ctx->data1 = ctx->idx;
			break;
		case mmUVD_GPCOM_VCPU_CMD:
			r = cb(ctx);
			if (r)
				return r;
			break;
		case mmUVD_ENGINE_CNTL:
		case mmUVD_NO_OP:
			break;
		default:
			DRM_ERROR("Invalid reg 0x%X!\n", reg);
			return -EINVAL;
		}
		ctx->idx++;
	}
	return 0;
}

/**
 * amdgpu_uvd_cs_packets - parse UVD packets
 *
 * @ctx: UVD parser context
 * @cb: callback function
 *
 * Parse the command stream packets.
 */
static int amdgpu_uvd_cs_packets(struct amdgpu_uvd_cs_ctx *ctx,
				 int (*cb)(struct amdgpu_uvd_cs_ctx *ctx))
{
	struct amdgpu_ib *ib = &ctx->parser->job->ibs[ctx->ib_idx];
	int r;

	for (ctx->idx = 0 ; ctx->idx < ib->length_dw; ) {
		uint32_t cmd = amdgpu_get_ib_value(ctx->parser, ctx->ib_idx, ctx->idx);
		unsigned type = CP_PACKET_GET_TYPE(cmd);
		switch (type) {
		case PACKET_TYPE0:
			ctx->reg = CP_PACKET0_GET_REG(cmd);
			ctx->count = CP_PACKET_GET_COUNT(cmd);
			r = amdgpu_uvd_cs_reg(ctx, cb);
			if (r)
				return r;
			break;
		case PACKET_TYPE2:
			++ctx->idx;
			break;
		default:
			DRM_ERROR("Unknown packet type %d !\n", type);
			return -EINVAL;
		}
	}
	return 0;
}

/**
 * amdgpu_uvd_ring_parse_cs - UVD command submission parser
 *
 * @parser: Command submission parser context
 * @ib_idx: Which indirect buffer to use
 *
 * Parse the command stream, patch in addresses as necessary.
 */
int amdgpu_uvd_ring_parse_cs(struct amdgpu_cs_parser *parser, uint32_t ib_idx)
{
	struct amdgpu_uvd_cs_ctx ctx = {};
	unsigned buf_sizes[] = {
		[0x00000000]	=	2048,
		[0x00000001]	=	0xFFFFFFFF,
		[0x00000002]	=	0xFFFFFFFF,
		[0x00000003]	=	2048,
		[0x00000004]	=	0xFFFFFFFF,
	};
	struct amdgpu_ib *ib = &parser->job->ibs[ib_idx];
	int r;

	parser->job->vm = NULL;
	ib->gpu_addr = amdgpu_sa_bo_gpu_addr(ib->sa_bo);

	if (ib->length_dw % 16) {
		DRM_ERROR("UVD IB length (%d) not 16 dwords aligned!\n",
			  ib->length_dw);
		return -EINVAL;
	}

	ctx.parser = parser;
	ctx.buf_sizes = buf_sizes;
	ctx.ib_idx = ib_idx;

	/* first round only required on chips without UVD 64 bit address support */
	if (!parser->adev->uvd.address_64_bit) {
		/* first round, make sure the buffers are actually in the UVD segment */
		r = amdgpu_uvd_cs_packets(&ctx, amdgpu_uvd_cs_pass1);
		if (r)
			return r;
	}

	/* second round, patch buffer addresses into the command stream */
	r = amdgpu_uvd_cs_packets(&ctx, amdgpu_uvd_cs_pass2);
	if (r)
		return r;

	if (!ctx.has_msg_cmd) {
		DRM_ERROR("UVD-IBs need a msg command!\n");
		return -EINVAL;
	}

	return 0;
}

static int amdgpu_uvd_send_msg(struct amdgpu_ring *ring, struct amdgpu_bo *bo,
			       bool direct, struct dma_fence **fence)
{
	struct amdgpu_device *adev = ring->adev;
	struct dma_fence *f = NULL;
	struct amdgpu_job *job;
	struct amdgpu_ib *ib;
	uint32_t data[4];
	uint64_t addr;
	long r;
	int i;
	unsigned offset_idx = 0;
	unsigned offset[3] = { UVD_BASE_SI, 0, 0 };

	r = amdgpu_job_alloc_with_ib(adev, 64, direct ? AMDGPU_IB_POOL_DIRECT :
				     AMDGPU_IB_POOL_DELAYED, &job);
	if (r)
		return r;

	if (adev->asic_type >= CHIP_VEGA10) {
		offset_idx = 1 + ring->me;
		offset[1] = adev->reg_offset[UVD_HWIP][0][1];
		offset[2] = adev->reg_offset[UVD_HWIP][1][1];
	}

	data[0] = PACKET0(offset[offset_idx] + UVD_GPCOM_VCPU_DATA0, 0);
	data[1] = PACKET0(offset[offset_idx] + UVD_GPCOM_VCPU_DATA1, 0);
	data[2] = PACKET0(offset[offset_idx] + UVD_GPCOM_VCPU_CMD, 0);
	data[3] = PACKET0(offset[offset_idx] + UVD_NO_OP, 0);

	ib = &job->ibs[0];
	addr = amdgpu_bo_gpu_offset(bo);
	ib->ptr[0] = data[0];
	ib->ptr[1] = addr;
	ib->ptr[2] = data[1];
	ib->ptr[3] = addr >> 32;
	ib->ptr[4] = data[2];
	ib->ptr[5] = 0;
	for (i = 6; i < 16; i += 2) {
		ib->ptr[i] = data[3];
		ib->ptr[i+1] = 0;
	}
	ib->length_dw = 16;

	if (direct) {
		r = dma_resv_wait_timeout(bo->tbo.base.resv, true, false,
					  msecs_to_jiffies(10));
		if (r == 0)
			r = -ETIMEDOUT;
		if (r < 0)
			goto err_free;

		r = amdgpu_job_submit_direct(job, ring, &f);
		if (r)
			goto err_free;
	} else {
		r = amdgpu_sync_resv(adev, &job->sync, bo->tbo.base.resv,
				     AMDGPU_SYNC_ALWAYS,
				     AMDGPU_FENCE_OWNER_UNDEFINED);
		if (r)
			goto err_free;

		r = amdgpu_job_submit(job, &adev->uvd.entity,
				      AMDGPU_FENCE_OWNER_UNDEFINED, &f);
		if (r)
			goto err_free;
	}

	amdgpu_bo_reserve(bo, true);
	amdgpu_bo_fence(bo, f, false);
	amdgpu_bo_unreserve(bo);

	if (fence)
		*fence = dma_fence_get(f);
	dma_fence_put(f);

	return 0;

err_free:
	amdgpu_job_free(job);
	return r;
}

/* multiple fence commands without any stream commands in between can
   crash the vcpu so just try to emmit a dummy create/destroy msg to
   avoid this */
int amdgpu_uvd_get_create_msg(struct amdgpu_ring *ring, uint32_t handle,
			      struct dma_fence **fence)
{
	struct amdgpu_device *adev = ring->adev;
	struct amdgpu_bo *bo = adev->uvd.ib_bo;
	uint32_t *msg;
	int i;

	msg = amdgpu_bo_kptr(bo);
	/* stitch together an UVD create msg */
	msg[0] = cpu_to_le32(0x00000de4);
	msg[1] = cpu_to_le32(0x00000000);
	msg[2] = cpu_to_le32(handle);
	msg[3] = cpu_to_le32(0x00000000);
	msg[4] = cpu_to_le32(0x00000000);
	msg[5] = cpu_to_le32(0x00000000);
	msg[6] = cpu_to_le32(0x00000000);
	msg[7] = cpu_to_le32(0x00000780);
	msg[8] = cpu_to_le32(0x00000440);
	msg[9] = cpu_to_le32(0x00000000);
	msg[10] = cpu_to_le32(0x01b37000);
	for (i = 11; i < 1024; ++i)
		msg[i] = cpu_to_le32(0x0);

	return amdgpu_uvd_send_msg(ring, bo, true, fence);

}

int amdgpu_uvd_get_destroy_msg(struct amdgpu_ring *ring, uint32_t handle,
			       bool direct, struct dma_fence **fence)
{
	struct amdgpu_device *adev = ring->adev;
	struct amdgpu_bo *bo = NULL;
	uint32_t *msg;
	int r, i;

	if (direct) {
		bo = adev->uvd.ib_bo;
	} else {
		r = amdgpu_uvd_create_msg_bo_helper(adev, 4096, &bo);
		if (r)
			return r;
	}

	msg = amdgpu_bo_kptr(bo);
	/* stitch together an UVD destroy msg */
	msg[0] = cpu_to_le32(0x00000de4);
	msg[1] = cpu_to_le32(0x00000002);
	msg[2] = cpu_to_le32(handle);
	msg[3] = cpu_to_le32(0x00000000);
	for (i = 4; i < 1024; ++i)
		msg[i] = cpu_to_le32(0x0);

	r = amdgpu_uvd_send_msg(ring, bo, direct, fence);

	if (!direct)
		amdgpu_bo_free_kernel(&bo, NULL, (void **)&msg);

	return r;
}

static void amdgpu_uvd_idle_work_handler(struct work_struct *work)
{
	struct amdgpu_device *adev =
		container_of(work, struct amdgpu_device, uvd.idle_work.work);
	unsigned fences = 0, i, j;

	for (i = 0; i < adev->uvd.num_uvd_inst; ++i) {
		if (adev->uvd.harvest_config & (1 << i))
			continue;
		fences += amdgpu_fence_count_emitted(&adev->uvd.inst[i].ring);
		for (j = 0; j < adev->uvd.num_enc_rings; ++j) {
			fences += amdgpu_fence_count_emitted(&adev->uvd.inst[i].ring_enc[j]);
		}
	}

	if (fences == 0) {
		if (adev->pm.dpm_enabled) {
			amdgpu_dpm_enable_uvd(adev, false);
		} else {
			amdgpu_asic_set_uvd_clocks(adev, 0, 0);
			/* shutdown the UVD block */
			amdgpu_device_ip_set_powergating_state(adev, AMD_IP_BLOCK_TYPE_UVD,
							       AMD_PG_STATE_GATE);
			amdgpu_device_ip_set_clockgating_state(adev, AMD_IP_BLOCK_TYPE_UVD,
							       AMD_CG_STATE_GATE);
		}
	} else {
		schedule_delayed_work(&adev->uvd.idle_work, UVD_IDLE_TIMEOUT);
	}
}

void amdgpu_uvd_ring_begin_use(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;
	bool set_clocks;

	if (amdgpu_sriov_vf(adev))
		return;

	set_clocks = !cancel_delayed_work_sync(&adev->uvd.idle_work);
	if (set_clocks) {
		if (adev->pm.dpm_enabled) {
			amdgpu_dpm_enable_uvd(adev, true);
		} else {
			amdgpu_asic_set_uvd_clocks(adev, 53300, 40000);
			amdgpu_device_ip_set_clockgating_state(adev, AMD_IP_BLOCK_TYPE_UVD,
							       AMD_CG_STATE_UNGATE);
			amdgpu_device_ip_set_powergating_state(adev, AMD_IP_BLOCK_TYPE_UVD,
							       AMD_PG_STATE_UNGATE);
		}
	}
}

void amdgpu_uvd_ring_end_use(struct amdgpu_ring *ring)
{
	if (!amdgpu_sriov_vf(ring->adev))
		schedule_delayed_work(&ring->adev->uvd.idle_work, UVD_IDLE_TIMEOUT);
}

/**
 * amdgpu_uvd_ring_test_ib - test ib execution
 *
 * @ring: amdgpu_ring pointer
 * @timeout: timeout value in jiffies, or MAX_SCHEDULE_TIMEOUT
 *
 * Test if we can successfully execute an IB
 */
int amdgpu_uvd_ring_test_ib(struct amdgpu_ring *ring, long timeout)
{
	struct dma_fence *fence;
	long r;

	r = amdgpu_uvd_get_create_msg(ring, 1, &fence);
	if (r)
		goto error;

	r = dma_fence_wait_timeout(fence, false, timeout);
	dma_fence_put(fence);
	if (r == 0)
		r = -ETIMEDOUT;
	if (r < 0)
		goto error;

	r = amdgpu_uvd_get_destroy_msg(ring, 1, true, &fence);
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

/**
 * amdgpu_uvd_used_handles - returns used UVD handles
 *
 * @adev: amdgpu_device pointer
 *
 * Returns the number of UVD handles in use
 */
uint32_t amdgpu_uvd_used_handles(struct amdgpu_device *adev)
{
	unsigned i;
	uint32_t used_handles = 0;

	for (i = 0; i < adev->uvd.max_handles; ++i) {
		/*
		 * Handles can be freed in any order, and not
		 * necessarily linear. So we need to count
		 * all non-zero handles.
		 */
		if (atomic_read(&adev->uvd.handles[i]))
			used_handles++;
	}

	return used_handles;
}

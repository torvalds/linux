/*
 * Copyright 2013 Advanced Micro Devices, Inc.
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
 * Authors: Christian König <christian.koenig@amd.com>
 */

#include <linux/firmware.h>
#include <linux/module.h>
#include <drm/drmP.h>
#include <drm/drm.h>

#include "radeon.h"
#include "radeon_asic.h"
#include "sid.h"

/* 1 second timeout */
#define VCE_IDLE_TIMEOUT_MS	1000

/* Firmware Names */
#define FIRMWARE_TAHITI	"radeon/TAHITI_vce.bin"
#define FIRMWARE_BONAIRE	"radeon/BONAIRE_vce.bin"

MODULE_FIRMWARE(FIRMWARE_TAHITI);
MODULE_FIRMWARE(FIRMWARE_BONAIRE);

static void radeon_vce_idle_work_handler(struct work_struct *work);

/**
 * radeon_vce_init - allocate memory, load vce firmware
 *
 * @rdev: radeon_device pointer
 *
 * First step to get VCE online, allocate memory and load the firmware
 */
int radeon_vce_init(struct radeon_device *rdev)
{
	static const char *fw_version = "[ATI LIB=VCEFW,";
	static const char *fb_version = "[ATI LIB=VCEFWSTATS,";
	unsigned long size;
	const char *fw_name, *c;
	uint8_t start, mid, end;
	int i, r;

	INIT_DELAYED_WORK(&rdev->vce.idle_work, radeon_vce_idle_work_handler);

	switch (rdev->family) {
	case CHIP_TAHITI:
	case CHIP_PITCAIRN:
	case CHIP_VERDE:
	case CHIP_OLAND:
	case CHIP_ARUBA:
		fw_name = FIRMWARE_TAHITI;
		break;

	case CHIP_BONAIRE:
	case CHIP_KAVERI:
	case CHIP_KABINI:
	case CHIP_HAWAII:
	case CHIP_MULLINS:
		fw_name = FIRMWARE_BONAIRE;
		break;

	default:
		return -EINVAL;
	}

	r = request_firmware(&rdev->vce_fw, fw_name, rdev->dev);
	if (r) {
		dev_err(rdev->dev, "radeon_vce: Can't load firmware \"%s\"\n",
			fw_name);
		return r;
	}

	/* search for firmware version */

	size = rdev->vce_fw->size - strlen(fw_version) - 9;
	c = rdev->vce_fw->data;
	for (;size > 0; --size, ++c)
		if (strncmp(c, fw_version, strlen(fw_version)) == 0)
			break;

	if (size == 0)
		return -EINVAL;

	c += strlen(fw_version);
	if (sscanf(c, "%2hhd.%2hhd.%2hhd]", &start, &mid, &end) != 3)
		return -EINVAL;

	/* search for feedback version */

	size = rdev->vce_fw->size - strlen(fb_version) - 3;
	c = rdev->vce_fw->data;
	for (;size > 0; --size, ++c)
		if (strncmp(c, fb_version, strlen(fb_version)) == 0)
			break;

	if (size == 0)
		return -EINVAL;

	c += strlen(fb_version);
	if (sscanf(c, "%2u]", &rdev->vce.fb_version) != 1)
		return -EINVAL;

	DRM_INFO("Found VCE firmware/feedback version %hhd.%hhd.%hhd / %d!\n",
		 start, mid, end, rdev->vce.fb_version);

	rdev->vce.fw_version = (start << 24) | (mid << 16) | (end << 8);

	/* we can only work with this fw version for now */
	if ((rdev->vce.fw_version != ((40 << 24) | (2 << 16) | (2 << 8))) &&
	    (rdev->vce.fw_version != ((50 << 24) | (0 << 16) | (1 << 8))) &&
	    (rdev->vce.fw_version != ((50 << 24) | (1 << 16) | (2 << 8))))
		return -EINVAL;

	/* allocate firmware, stack and heap BO */

	if (rdev->family < CHIP_BONAIRE)
		size = vce_v1_0_bo_size(rdev);
	else
		size = vce_v2_0_bo_size(rdev);
	r = radeon_bo_create(rdev, size, PAGE_SIZE, true,
			     RADEON_GEM_DOMAIN_VRAM, 0, NULL, NULL,
			     &rdev->vce.vcpu_bo);
	if (r) {
		dev_err(rdev->dev, "(%d) failed to allocate VCE bo\n", r);
		return r;
	}

	r = radeon_bo_reserve(rdev->vce.vcpu_bo, false);
	if (r) {
		radeon_bo_unref(&rdev->vce.vcpu_bo);
		dev_err(rdev->dev, "(%d) failed to reserve VCE bo\n", r);
		return r;
	}

	r = radeon_bo_pin(rdev->vce.vcpu_bo, RADEON_GEM_DOMAIN_VRAM,
			  &rdev->vce.gpu_addr);
	radeon_bo_unreserve(rdev->vce.vcpu_bo);
	if (r) {
		radeon_bo_unref(&rdev->vce.vcpu_bo);
		dev_err(rdev->dev, "(%d) VCE bo pin failed\n", r);
		return r;
	}

	for (i = 0; i < RADEON_MAX_VCE_HANDLES; ++i) {
		atomic_set(&rdev->vce.handles[i], 0);
		rdev->vce.filp[i] = NULL;
        }

	return 0;
}

/**
 * radeon_vce_fini - free memory
 *
 * @rdev: radeon_device pointer
 *
 * Last step on VCE teardown, free firmware memory
 */
void radeon_vce_fini(struct radeon_device *rdev)
{
	if (rdev->vce.vcpu_bo == NULL)
		return;

	radeon_bo_unref(&rdev->vce.vcpu_bo);

	release_firmware(rdev->vce_fw);
}

/**
 * radeon_vce_suspend - unpin VCE fw memory
 *
 * @rdev: radeon_device pointer
 *
 */
int radeon_vce_suspend(struct radeon_device *rdev)
{
	int i;

	if (rdev->vce.vcpu_bo == NULL)
		return 0;

	for (i = 0; i < RADEON_MAX_VCE_HANDLES; ++i)
		if (atomic_read(&rdev->vce.handles[i]))
			break;

	if (i == RADEON_MAX_VCE_HANDLES)
		return 0;

	/* TODO: suspending running encoding sessions isn't supported */
	return -EINVAL;
}

/**
 * radeon_vce_resume - pin VCE fw memory
 *
 * @rdev: radeon_device pointer
 *
 */
int radeon_vce_resume(struct radeon_device *rdev)
{
	void *cpu_addr;
	int r;

	if (rdev->vce.vcpu_bo == NULL)
		return -EINVAL;

	r = radeon_bo_reserve(rdev->vce.vcpu_bo, false);
	if (r) {
		dev_err(rdev->dev, "(%d) failed to reserve VCE bo\n", r);
		return r;
	}

	r = radeon_bo_kmap(rdev->vce.vcpu_bo, &cpu_addr);
	if (r) {
		radeon_bo_unreserve(rdev->vce.vcpu_bo);
		dev_err(rdev->dev, "(%d) VCE map failed\n", r);
		return r;
	}

	memset(cpu_addr, 0, radeon_bo_size(rdev->vce.vcpu_bo));
	if (rdev->family < CHIP_BONAIRE)
		r = vce_v1_0_load_fw(rdev, cpu_addr);
	else
		memcpy(cpu_addr, rdev->vce_fw->data, rdev->vce_fw->size);

	radeon_bo_kunmap(rdev->vce.vcpu_bo);

	radeon_bo_unreserve(rdev->vce.vcpu_bo);

	return r;
}

/**
 * radeon_vce_idle_work_handler - power off VCE
 *
 * @work: pointer to work structure
 *
 * power of VCE when it's not used any more
 */
static void radeon_vce_idle_work_handler(struct work_struct *work)
{
	struct radeon_device *rdev =
		container_of(work, struct radeon_device, vce.idle_work.work);

	if ((radeon_fence_count_emitted(rdev, TN_RING_TYPE_VCE1_INDEX) == 0) &&
	    (radeon_fence_count_emitted(rdev, TN_RING_TYPE_VCE2_INDEX) == 0)) {
		if ((rdev->pm.pm_method == PM_METHOD_DPM) && rdev->pm.dpm_enabled) {
			radeon_dpm_enable_vce(rdev, false);
		} else {
			radeon_set_vce_clocks(rdev, 0, 0);
		}
	} else {
		schedule_delayed_work(&rdev->vce.idle_work,
				      msecs_to_jiffies(VCE_IDLE_TIMEOUT_MS));
	}
}

/**
 * radeon_vce_note_usage - power up VCE
 *
 * @rdev: radeon_device pointer
 *
 * Make sure VCE is powerd up when we want to use it
 */
void radeon_vce_note_usage(struct radeon_device *rdev)
{
	bool streams_changed = false;
	bool set_clocks = !cancel_delayed_work_sync(&rdev->vce.idle_work);
	set_clocks &= schedule_delayed_work(&rdev->vce.idle_work,
					    msecs_to_jiffies(VCE_IDLE_TIMEOUT_MS));

	if ((rdev->pm.pm_method == PM_METHOD_DPM) && rdev->pm.dpm_enabled) {
		/* XXX figure out if the streams changed */
		streams_changed = false;
	}

	if (set_clocks || streams_changed) {
		if ((rdev->pm.pm_method == PM_METHOD_DPM) && rdev->pm.dpm_enabled) {
			radeon_dpm_enable_vce(rdev, true);
		} else {
			radeon_set_vce_clocks(rdev, 53300, 40000);
		}
	}
}

/**
 * radeon_vce_free_handles - free still open VCE handles
 *
 * @rdev: radeon_device pointer
 * @filp: drm file pointer
 *
 * Close all VCE handles still open by this file pointer
 */
void radeon_vce_free_handles(struct radeon_device *rdev, struct drm_file *filp)
{
	int i, r;
	for (i = 0; i < RADEON_MAX_VCE_HANDLES; ++i) {
		uint32_t handle = atomic_read(&rdev->vce.handles[i]);
		if (!handle || rdev->vce.filp[i] != filp)
			continue;

		radeon_vce_note_usage(rdev);

		r = radeon_vce_get_destroy_msg(rdev, TN_RING_TYPE_VCE1_INDEX,
					       handle, NULL);
		if (r)
			DRM_ERROR("Error destroying VCE handle (%d)!\n", r);

		rdev->vce.filp[i] = NULL;
		atomic_set(&rdev->vce.handles[i], 0);
	}
}

/**
 * radeon_vce_get_create_msg - generate a VCE create msg
 *
 * @rdev: radeon_device pointer
 * @ring: ring we should submit the msg to
 * @handle: VCE session handle to use
 * @fence: optional fence to return
 *
 * Open up a stream for HW test
 */
int radeon_vce_get_create_msg(struct radeon_device *rdev, int ring,
			      uint32_t handle, struct radeon_fence **fence)
{
	const unsigned ib_size_dw = 1024;
	struct radeon_ib ib;
	uint64_t dummy;
	int i, r;

	r = radeon_ib_get(rdev, ring, &ib, NULL, ib_size_dw * 4);
	if (r) {
		DRM_ERROR("radeon: failed to get ib (%d).\n", r);
		return r;
	}

	dummy = ib.gpu_addr + 1024;

	/* stitch together an VCE create msg */
	ib.length_dw = 0;
	ib.ptr[ib.length_dw++] = cpu_to_le32(0x0000000c); /* len */
	ib.ptr[ib.length_dw++] = cpu_to_le32(0x00000001); /* session cmd */
	ib.ptr[ib.length_dw++] = cpu_to_le32(handle);

	ib.ptr[ib.length_dw++] = cpu_to_le32(0x00000030); /* len */
	ib.ptr[ib.length_dw++] = cpu_to_le32(0x01000001); /* create cmd */
	ib.ptr[ib.length_dw++] = cpu_to_le32(0x00000000);
	ib.ptr[ib.length_dw++] = cpu_to_le32(0x00000042);
	ib.ptr[ib.length_dw++] = cpu_to_le32(0x0000000a);
	ib.ptr[ib.length_dw++] = cpu_to_le32(0x00000001);
	ib.ptr[ib.length_dw++] = cpu_to_le32(0x00000080);
	ib.ptr[ib.length_dw++] = cpu_to_le32(0x00000060);
	ib.ptr[ib.length_dw++] = cpu_to_le32(0x00000100);
	ib.ptr[ib.length_dw++] = cpu_to_le32(0x00000100);
	ib.ptr[ib.length_dw++] = cpu_to_le32(0x0000000c);
	ib.ptr[ib.length_dw++] = cpu_to_le32(0x00000000);

	ib.ptr[ib.length_dw++] = cpu_to_le32(0x00000014); /* len */
	ib.ptr[ib.length_dw++] = cpu_to_le32(0x05000005); /* feedback buffer */
	ib.ptr[ib.length_dw++] = cpu_to_le32(upper_32_bits(dummy));
	ib.ptr[ib.length_dw++] = cpu_to_le32(dummy);
	ib.ptr[ib.length_dw++] = cpu_to_le32(0x00000001);

	for (i = ib.length_dw; i < ib_size_dw; ++i)
		ib.ptr[i] = cpu_to_le32(0x0);

	r = radeon_ib_schedule(rdev, &ib, NULL, false);
	if (r) {
	        DRM_ERROR("radeon: failed to schedule ib (%d).\n", r);
	}

	if (fence)
		*fence = radeon_fence_ref(ib.fence);

	radeon_ib_free(rdev, &ib);

	return r;
}

/**
 * radeon_vce_get_destroy_msg - generate a VCE destroy msg
 *
 * @rdev: radeon_device pointer
 * @ring: ring we should submit the msg to
 * @handle: VCE session handle to use
 * @fence: optional fence to return
 *
 * Close up a stream for HW test or if userspace failed to do so
 */
int radeon_vce_get_destroy_msg(struct radeon_device *rdev, int ring,
			       uint32_t handle, struct radeon_fence **fence)
{
	const unsigned ib_size_dw = 1024;
	struct radeon_ib ib;
	uint64_t dummy;
	int i, r;

	r = radeon_ib_get(rdev, ring, &ib, NULL, ib_size_dw * 4);
	if (r) {
		DRM_ERROR("radeon: failed to get ib (%d).\n", r);
		return r;
	}

	dummy = ib.gpu_addr + 1024;

	/* stitch together an VCE destroy msg */
	ib.length_dw = 0;
	ib.ptr[ib.length_dw++] = cpu_to_le32(0x0000000c); /* len */
	ib.ptr[ib.length_dw++] = cpu_to_le32(0x00000001); /* session cmd */
	ib.ptr[ib.length_dw++] = cpu_to_le32(handle);

	ib.ptr[ib.length_dw++] = cpu_to_le32(0x00000014); /* len */
	ib.ptr[ib.length_dw++] = cpu_to_le32(0x05000005); /* feedback buffer */
	ib.ptr[ib.length_dw++] = cpu_to_le32(upper_32_bits(dummy));
	ib.ptr[ib.length_dw++] = cpu_to_le32(dummy);
	ib.ptr[ib.length_dw++] = cpu_to_le32(0x00000001);

	ib.ptr[ib.length_dw++] = cpu_to_le32(0x00000008); /* len */
	ib.ptr[ib.length_dw++] = cpu_to_le32(0x02000001); /* destroy cmd */

	for (i = ib.length_dw; i < ib_size_dw; ++i)
		ib.ptr[i] = cpu_to_le32(0x0);

	r = radeon_ib_schedule(rdev, &ib, NULL, false);
	if (r) {
	        DRM_ERROR("radeon: failed to schedule ib (%d).\n", r);
	}

	if (fence)
		*fence = radeon_fence_ref(ib.fence);

	radeon_ib_free(rdev, &ib);

	return r;
}

/**
 * radeon_vce_cs_reloc - command submission relocation
 *
 * @p: parser context
 * @lo: address of lower dword
 * @hi: address of higher dword
 * @size: size of checker for relocation buffer
 *
 * Patch relocation inside command stream with real buffer address
 */
int radeon_vce_cs_reloc(struct radeon_cs_parser *p, int lo, int hi,
			unsigned size)
{
	struct radeon_cs_chunk *relocs_chunk;
	struct radeon_bo_list *reloc;
	uint64_t start, end, offset;
	unsigned idx;

	relocs_chunk = p->chunk_relocs;
	offset = radeon_get_ib_value(p, lo);
	idx = radeon_get_ib_value(p, hi);

	if (idx >= relocs_chunk->length_dw) {
		DRM_ERROR("Relocs at %d after relocations chunk end %d !\n",
			  idx, relocs_chunk->length_dw);
		return -EINVAL;
	}

	reloc = &p->relocs[(idx / 4)];
	start = reloc->gpu_offset;
	end = start + radeon_bo_size(reloc->robj);
	start += offset;

	p->ib.ptr[lo] = start & 0xFFFFFFFF;
	p->ib.ptr[hi] = start >> 32;

	if (end <= start) {
		DRM_ERROR("invalid reloc offset %llX!\n", offset);
		return -EINVAL;
	}
	if ((end - start) < size) {
		DRM_ERROR("buffer to small (%d / %d)!\n",
			(unsigned)(end - start), size);
		return -EINVAL;
	}

	return 0;
}

/**
 * radeon_vce_validate_handle - validate stream handle
 *
 * @p: parser context
 * @handle: handle to validate
 * @allocated: allocated a new handle?
 *
 * Validates the handle and return the found session index or -EINVAL
 * we we don't have another free session index.
 */
static int radeon_vce_validate_handle(struct radeon_cs_parser *p,
				      uint32_t handle, bool *allocated)
{
	unsigned i;

	*allocated = false;

	/* validate the handle */
	for (i = 0; i < RADEON_MAX_VCE_HANDLES; ++i) {
		if (atomic_read(&p->rdev->vce.handles[i]) == handle) {
			if (p->rdev->vce.filp[i] != p->filp) {
				DRM_ERROR("VCE handle collision detected!\n");
				return -EINVAL;
			}
			return i;
		}
	}

	/* handle not found try to alloc a new one */
	for (i = 0; i < RADEON_MAX_VCE_HANDLES; ++i) {
		if (!atomic_cmpxchg(&p->rdev->vce.handles[i], 0, handle)) {
			p->rdev->vce.filp[i] = p->filp;
			p->rdev->vce.img_size[i] = 0;
			*allocated = true;
			return i;
		}
	}

	DRM_ERROR("No more free VCE handles!\n");
	return -EINVAL;
}

/**
 * radeon_vce_cs_parse - parse and validate the command stream
 *
 * @p: parser context
 *
 */
int radeon_vce_cs_parse(struct radeon_cs_parser *p)
{
	int session_idx = -1;
	bool destroyed = false, created = false, allocated = false;
	uint32_t tmp, handle = 0;
	uint32_t *size = &tmp;
	int i, r = 0;

	while (p->idx < p->chunk_ib->length_dw) {
		uint32_t len = radeon_get_ib_value(p, p->idx);
		uint32_t cmd = radeon_get_ib_value(p, p->idx + 1);

		if ((len < 8) || (len & 3)) {
			DRM_ERROR("invalid VCE command length (%d)!\n", len);
			r = -EINVAL;
			goto out;
		}

		if (destroyed) {
			DRM_ERROR("No other command allowed after destroy!\n");
			r = -EINVAL;
			goto out;
		}

		switch (cmd) {
		case 0x00000001: // session
			handle = radeon_get_ib_value(p, p->idx + 2);
			session_idx = radeon_vce_validate_handle(p, handle,
								 &allocated);
			if (session_idx < 0)
				return session_idx;
			size = &p->rdev->vce.img_size[session_idx];
			break;

		case 0x00000002: // task info
			break;

		case 0x01000001: // create
			created = true;
			if (!allocated) {
				DRM_ERROR("Handle already in use!\n");
				r = -EINVAL;
				goto out;
			}

			*size = radeon_get_ib_value(p, p->idx + 8) *
				radeon_get_ib_value(p, p->idx + 10) *
				8 * 3 / 2;
			break;

		case 0x04000001: // config extension
		case 0x04000002: // pic control
		case 0x04000005: // rate control
		case 0x04000007: // motion estimation
		case 0x04000008: // rdo
		case 0x04000009: // vui
			break;

		case 0x03000001: // encode
			r = radeon_vce_cs_reloc(p, p->idx + 10, p->idx + 9,
						*size);
			if (r)
				goto out;

			r = radeon_vce_cs_reloc(p, p->idx + 12, p->idx + 11,
						*size / 3);
			if (r)
				goto out;
			break;

		case 0x02000001: // destroy
			destroyed = true;
			break;

		case 0x05000001: // context buffer
			r = radeon_vce_cs_reloc(p, p->idx + 3, p->idx + 2,
						*size * 2);
			if (r)
				goto out;
			break;

		case 0x05000004: // video bitstream buffer
			tmp = radeon_get_ib_value(p, p->idx + 4);
			r = radeon_vce_cs_reloc(p, p->idx + 3, p->idx + 2,
						tmp);
			if (r)
				goto out;
			break;

		case 0x05000005: // feedback buffer
			r = radeon_vce_cs_reloc(p, p->idx + 3, p->idx + 2,
						4096);
			if (r)
				goto out;
			break;

		default:
			DRM_ERROR("invalid VCE command (0x%x)!\n", cmd);
			r = -EINVAL;
			goto out;
		}

		if (session_idx == -1) {
			DRM_ERROR("no session command at start of IB\n");
			r = -EINVAL;
			goto out;
		}

		p->idx += len / 4;
	}

	if (allocated && !created) {
		DRM_ERROR("New session without create command!\n");
		r = -ENOENT;
	}

out:
	if ((!r && destroyed) || (r && allocated)) {
		/*
		 * IB contains a destroy msg or we have allocated an
		 * handle and got an error, anyway free the handle
		 */
		for (i = 0; i < RADEON_MAX_VCE_HANDLES; ++i)
			atomic_cmpxchg(&p->rdev->vce.handles[i], handle, 0);
	}

	return r;
}

/**
 * radeon_vce_semaphore_emit - emit a semaphore command
 *
 * @rdev: radeon_device pointer
 * @ring: engine to use
 * @semaphore: address of semaphore
 * @emit_wait: true=emit wait, false=emit signal
 *
 */
bool radeon_vce_semaphore_emit(struct radeon_device *rdev,
			       struct radeon_ring *ring,
			       struct radeon_semaphore *semaphore,
			       bool emit_wait)
{
	uint64_t addr = semaphore->gpu_addr;

	radeon_ring_write(ring, cpu_to_le32(VCE_CMD_SEMAPHORE));
	radeon_ring_write(ring, cpu_to_le32((addr >> 3) & 0x000FFFFF));
	radeon_ring_write(ring, cpu_to_le32((addr >> 23) & 0x000FFFFF));
	radeon_ring_write(ring, cpu_to_le32(0x01003000 | (emit_wait ? 1 : 0)));
	if (!emit_wait)
		radeon_ring_write(ring, cpu_to_le32(VCE_CMD_END));

	return true;
}

/**
 * radeon_vce_ib_execute - execute indirect buffer
 *
 * @rdev: radeon_device pointer
 * @ib: the IB to execute
 *
 */
void radeon_vce_ib_execute(struct radeon_device *rdev, struct radeon_ib *ib)
{
	struct radeon_ring *ring = &rdev->ring[ib->ring];
	radeon_ring_write(ring, cpu_to_le32(VCE_CMD_IB));
	radeon_ring_write(ring, cpu_to_le32(ib->gpu_addr));
	radeon_ring_write(ring, cpu_to_le32(upper_32_bits(ib->gpu_addr)));
	radeon_ring_write(ring, cpu_to_le32(ib->length_dw));
}

/**
 * radeon_vce_fence_emit - add a fence command to the ring
 *
 * @rdev: radeon_device pointer
 * @fence: the fence
 *
 */
void radeon_vce_fence_emit(struct radeon_device *rdev,
			   struct radeon_fence *fence)
{
	struct radeon_ring *ring = &rdev->ring[fence->ring];
	uint64_t addr = rdev->fence_drv[fence->ring].gpu_addr;

	radeon_ring_write(ring, cpu_to_le32(VCE_CMD_FENCE));
	radeon_ring_write(ring, cpu_to_le32(addr));
	radeon_ring_write(ring, cpu_to_le32(upper_32_bits(addr)));
	radeon_ring_write(ring, cpu_to_le32(fence->seq));
	radeon_ring_write(ring, cpu_to_le32(VCE_CMD_TRAP));
	radeon_ring_write(ring, cpu_to_le32(VCE_CMD_END));
}

/**
 * radeon_vce_ring_test - test if VCE ring is working
 *
 * @rdev: radeon_device pointer
 * @ring: the engine to test on
 *
 */
int radeon_vce_ring_test(struct radeon_device *rdev, struct radeon_ring *ring)
{
	uint32_t rptr = vce_v1_0_get_rptr(rdev, ring);
	unsigned i;
	int r;

	r = radeon_ring_lock(rdev, ring, 16);
	if (r) {
		DRM_ERROR("radeon: vce failed to lock ring %d (%d).\n",
			  ring->idx, r);
		return r;
	}
	radeon_ring_write(ring, cpu_to_le32(VCE_CMD_END));
	radeon_ring_unlock_commit(rdev, ring, false);

	for (i = 0; i < rdev->usec_timeout; i++) {
	        if (vce_v1_0_get_rptr(rdev, ring) != rptr)
	                break;
	        DRM_UDELAY(1);
	}

	if (i < rdev->usec_timeout) {
	        DRM_INFO("ring test on %d succeeded in %d usecs\n",
	                 ring->idx, i);
	} else {
	        DRM_ERROR("radeon: ring %d test failed\n",
	                  ring->idx);
	        r = -ETIMEDOUT;
	}

	return r;
}

/**
 * radeon_vce_ib_test - test if VCE IBs are working
 *
 * @rdev: radeon_device pointer
 * @ring: the engine to test on
 *
 */
int radeon_vce_ib_test(struct radeon_device *rdev, struct radeon_ring *ring)
{
	struct radeon_fence *fence = NULL;
	int r;

	r = radeon_vce_get_create_msg(rdev, ring->idx, 1, NULL);
	if (r) {
		DRM_ERROR("radeon: failed to get create msg (%d).\n", r);
		goto error;
	}

	r = radeon_vce_get_destroy_msg(rdev, ring->idx, 1, &fence);
	if (r) {
		DRM_ERROR("radeon: failed to get destroy ib (%d).\n", r);
		goto error;
	}

	r = radeon_fence_wait(fence, false);
	if (r) {
		DRM_ERROR("radeon: fence wait failed (%d).\n", r);
	} else {
	        DRM_INFO("ib test on ring %d succeeded\n", ring->idx);
	}
error:
	radeon_fence_unref(&fence);
	return r;
}

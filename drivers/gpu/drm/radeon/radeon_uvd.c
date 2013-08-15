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
#include <drm/drmP.h>
#include <drm/drm.h>

#include "radeon.h"
#include "r600d.h"

/* 1 second timeout */
#define UVD_IDLE_TIMEOUT_MS	1000

/* Firmware Names */
#define FIRMWARE_RV710		"radeon/RV710_uvd.bin"
#define FIRMWARE_CYPRESS	"radeon/CYPRESS_uvd.bin"
#define FIRMWARE_SUMO		"radeon/SUMO_uvd.bin"
#define FIRMWARE_TAHITI		"radeon/TAHITI_uvd.bin"
#define FIRMWARE_BONAIRE	"radeon/BONAIRE_uvd.bin"

MODULE_FIRMWARE(FIRMWARE_RV710);
MODULE_FIRMWARE(FIRMWARE_CYPRESS);
MODULE_FIRMWARE(FIRMWARE_SUMO);
MODULE_FIRMWARE(FIRMWARE_TAHITI);
MODULE_FIRMWARE(FIRMWARE_BONAIRE);

static void radeon_uvd_idle_work_handler(struct work_struct *work);

int radeon_uvd_init(struct radeon_device *rdev)
{
	unsigned long bo_size;
	const char *fw_name;
	int i, r;

	INIT_DELAYED_WORK(&rdev->uvd.idle_work, radeon_uvd_idle_work_handler);

	switch (rdev->family) {
	case CHIP_RV710:
	case CHIP_RV730:
	case CHIP_RV740:
		fw_name = FIRMWARE_RV710;
		break;

	case CHIP_CYPRESS:
	case CHIP_HEMLOCK:
	case CHIP_JUNIPER:
	case CHIP_REDWOOD:
	case CHIP_CEDAR:
		fw_name = FIRMWARE_CYPRESS;
		break;

	case CHIP_SUMO:
	case CHIP_SUMO2:
	case CHIP_PALM:
	case CHIP_CAYMAN:
	case CHIP_BARTS:
	case CHIP_TURKS:
	case CHIP_CAICOS:
		fw_name = FIRMWARE_SUMO;
		break;

	case CHIP_TAHITI:
	case CHIP_VERDE:
	case CHIP_PITCAIRN:
	case CHIP_ARUBA:
		fw_name = FIRMWARE_TAHITI;
		break;

	case CHIP_BONAIRE:
	case CHIP_KABINI:
	case CHIP_KAVERI:
		fw_name = FIRMWARE_BONAIRE;
		break;

	default:
		return -EINVAL;
	}

	r = request_firmware(&rdev->uvd_fw, fw_name, rdev->dev);
	if (r) {
		dev_err(rdev->dev, "radeon_uvd: Can't load firmware \"%s\"\n",
			fw_name);
		return r;
	}

	bo_size = RADEON_GPU_PAGE_ALIGN(rdev->uvd_fw->size + 8) +
		  RADEON_UVD_STACK_SIZE + RADEON_UVD_HEAP_SIZE;
	r = radeon_bo_create(rdev, bo_size, PAGE_SIZE, true,
			     RADEON_GEM_DOMAIN_VRAM, NULL, &rdev->uvd.vcpu_bo);
	if (r) {
		dev_err(rdev->dev, "(%d) failed to allocate UVD bo\n", r);
		return r;
	}

	r = radeon_bo_reserve(rdev->uvd.vcpu_bo, false);
	if (r) {
		radeon_bo_unref(&rdev->uvd.vcpu_bo);
		dev_err(rdev->dev, "(%d) failed to reserve UVD bo\n", r);
		return r;
	}

	r = radeon_bo_pin(rdev->uvd.vcpu_bo, RADEON_GEM_DOMAIN_VRAM,
			  &rdev->uvd.gpu_addr);
	if (r) {
		radeon_bo_unreserve(rdev->uvd.vcpu_bo);
		radeon_bo_unref(&rdev->uvd.vcpu_bo);
		dev_err(rdev->dev, "(%d) UVD bo pin failed\n", r);
		return r;
	}

	r = radeon_bo_kmap(rdev->uvd.vcpu_bo, &rdev->uvd.cpu_addr);
	if (r) {
		dev_err(rdev->dev, "(%d) UVD map failed\n", r);
		return r;
	}

	radeon_bo_unreserve(rdev->uvd.vcpu_bo);

	for (i = 0; i < RADEON_MAX_UVD_HANDLES; ++i) {
		atomic_set(&rdev->uvd.handles[i], 0);
		rdev->uvd.filp[i] = NULL;
	}

	return 0;
}

void radeon_uvd_fini(struct radeon_device *rdev)
{
	int r;

	if (rdev->uvd.vcpu_bo == NULL)
		return;

	r = radeon_bo_reserve(rdev->uvd.vcpu_bo, false);
	if (!r) {
		radeon_bo_kunmap(rdev->uvd.vcpu_bo);
		radeon_bo_unpin(rdev->uvd.vcpu_bo);
		radeon_bo_unreserve(rdev->uvd.vcpu_bo);
	}

	radeon_bo_unref(&rdev->uvd.vcpu_bo);

	release_firmware(rdev->uvd_fw);
}

int radeon_uvd_suspend(struct radeon_device *rdev)
{
	unsigned size;
	void *ptr;
	int i;

	if (rdev->uvd.vcpu_bo == NULL)
		return 0;

	for (i = 0; i < RADEON_MAX_UVD_HANDLES; ++i)
		if (atomic_read(&rdev->uvd.handles[i]))
			break;

	if (i == RADEON_MAX_UVD_HANDLES)
		return 0;

	size = radeon_bo_size(rdev->uvd.vcpu_bo);
	size -= rdev->uvd_fw->size;

	ptr = rdev->uvd.cpu_addr;
	ptr += rdev->uvd_fw->size;

	rdev->uvd.saved_bo = kmalloc(size, GFP_KERNEL);
	memcpy(rdev->uvd.saved_bo, ptr, size);

	return 0;
}

int radeon_uvd_resume(struct radeon_device *rdev)
{
	unsigned size;
	void *ptr;

	if (rdev->uvd.vcpu_bo == NULL)
		return -EINVAL;

	memcpy(rdev->uvd.cpu_addr, rdev->uvd_fw->data, rdev->uvd_fw->size);

	size = radeon_bo_size(rdev->uvd.vcpu_bo);
	size -= rdev->uvd_fw->size;

	ptr = rdev->uvd.cpu_addr;
	ptr += rdev->uvd_fw->size;

	if (rdev->uvd.saved_bo != NULL) {
		memcpy(ptr, rdev->uvd.saved_bo, size);
		kfree(rdev->uvd.saved_bo);
		rdev->uvd.saved_bo = NULL;
	} else
		memset(ptr, 0, size);

	return 0;
}

void radeon_uvd_force_into_uvd_segment(struct radeon_bo *rbo)
{
	rbo->placement.fpfn = 0 >> PAGE_SHIFT;
	rbo->placement.lpfn = (256 * 1024 * 1024) >> PAGE_SHIFT;
}

void radeon_uvd_free_handles(struct radeon_device *rdev, struct drm_file *filp)
{
	int i, r;
	for (i = 0; i < RADEON_MAX_UVD_HANDLES; ++i) {
		uint32_t handle = atomic_read(&rdev->uvd.handles[i]);
		if (handle != 0 && rdev->uvd.filp[i] == filp) {
			struct radeon_fence *fence;

			r = radeon_uvd_get_destroy_msg(rdev,
				R600_RING_TYPE_UVD_INDEX, handle, &fence);
			if (r) {
				DRM_ERROR("Error destroying UVD (%d)!\n", r);
				continue;
			}

			radeon_fence_wait(fence, false);
			radeon_fence_unref(&fence);

			rdev->uvd.filp[i] = NULL;
			atomic_set(&rdev->uvd.handles[i], 0);
		}
	}
}

static int radeon_uvd_cs_msg_decode(uint32_t *msg, unsigned buf_sizes[])
{
	unsigned stream_type = msg[4];
	unsigned width = msg[6];
	unsigned height = msg[7];
	unsigned dpb_size = msg[9];
	unsigned pitch = msg[28];

	unsigned width_in_mb = width / 16;
	unsigned height_in_mb = ALIGN(height / 16, 2);

	unsigned image_size, tmp, min_dpb_size;

	image_size = width * height;
	image_size += image_size / 2;
	image_size = ALIGN(image_size, 1024);

	switch (stream_type) {
	case 0: /* H264 */

		/* reference picture buffer */
		min_dpb_size = image_size * 17;

		/* macroblock context buffer */
		min_dpb_size += width_in_mb * height_in_mb * 17 * 192;

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
	return 0;
}

static int radeon_uvd_cs_msg(struct radeon_cs_parser *p, struct radeon_bo *bo,
			     unsigned offset, unsigned buf_sizes[])
{
	int32_t *msg, msg_type, handle;
	void *ptr;

	int i, r;

	if (offset & 0x3F) {
		DRM_ERROR("UVD messages must be 64 byte aligned!\n");
		return -EINVAL;
	}

	r = radeon_bo_kmap(bo, &ptr);
	if (r) {
		DRM_ERROR("Failed mapping the UVD message (%d)!\n", r);
		return r;
	}

	msg = ptr + offset;

	msg_type = msg[1];
	handle = msg[2];

	if (handle == 0) {
		DRM_ERROR("Invalid UVD handle!\n");
		return -EINVAL;
	}

	if (msg_type == 1) {
		/* it's a decode msg, calc buffer sizes */
		r = radeon_uvd_cs_msg_decode(msg, buf_sizes);
		radeon_bo_kunmap(bo);
		if (r)
			return r;

	} else if (msg_type == 2) {
		/* it's a destroy msg, free the handle */
		for (i = 0; i < RADEON_MAX_UVD_HANDLES; ++i)
			atomic_cmpxchg(&p->rdev->uvd.handles[i], handle, 0);
		radeon_bo_kunmap(bo);
		return 0;
	} else {
		radeon_bo_kunmap(bo);

		if (msg_type != 0) {
			DRM_ERROR("Illegal UVD message type (%d)!\n", msg_type);
			return -EINVAL;
		}

		/* it's a create msg, no special handling needed */
	}

	/* create or decode, validate the handle */
	for (i = 0; i < RADEON_MAX_UVD_HANDLES; ++i) {
		if (atomic_read(&p->rdev->uvd.handles[i]) == handle)
			return 0;
	}

	/* handle not found try to alloc a new one */
	for (i = 0; i < RADEON_MAX_UVD_HANDLES; ++i) {
		if (!atomic_cmpxchg(&p->rdev->uvd.handles[i], 0, handle)) {
			p->rdev->uvd.filp[i] = p->filp;
			return 0;
		}
	}

	DRM_ERROR("No more free UVD handles!\n");
	return -EINVAL;
}

static int radeon_uvd_cs_reloc(struct radeon_cs_parser *p,
			       int data0, int data1,
			       unsigned buf_sizes[], bool *has_msg_cmd)
{
	struct radeon_cs_chunk *relocs_chunk;
	struct radeon_cs_reloc *reloc;
	unsigned idx, cmd, offset;
	uint64_t start, end;
	int r;

	relocs_chunk = &p->chunks[p->chunk_relocs_idx];
	offset = radeon_get_ib_value(p, data0);
	idx = radeon_get_ib_value(p, data1);
	if (idx >= relocs_chunk->length_dw) {
		DRM_ERROR("Relocs at %d after relocations chunk end %d !\n",
			  idx, relocs_chunk->length_dw);
		return -EINVAL;
	}

	reloc = p->relocs_ptr[(idx / 4)];
	start = reloc->lobj.gpu_offset;
	end = start + radeon_bo_size(reloc->robj);
	start += offset;

	p->ib.ptr[data0] = start & 0xFFFFFFFF;
	p->ib.ptr[data1] = start >> 32;

	cmd = radeon_get_ib_value(p, p->idx) >> 1;

	if (cmd < 0x4) {
		if ((end - start) < buf_sizes[cmd]) {
			DRM_ERROR("buffer (%d) to small (%d / %d)!\n", cmd,
				  (unsigned)(end - start), buf_sizes[cmd]);
			return -EINVAL;
		}

	} else if (cmd != 0x100) {
		DRM_ERROR("invalid UVD command %X!\n", cmd);
		return -EINVAL;
	}

	if ((start >> 28) != (end >> 28)) {
		DRM_ERROR("reloc %LX-%LX crossing 256MB boundary!\n",
			  start, end);
		return -EINVAL;
	}

	/* TODO: is this still necessary on NI+ ? */
	if ((cmd == 0 || cmd == 0x3) &&
	    (start >> 28) != (p->rdev->uvd.gpu_addr >> 28)) {
		DRM_ERROR("msg/fb buffer %LX-%LX out of 256MB segment!\n",
			  start, end);
		return -EINVAL;
	}

	if (cmd == 0) {
		if (*has_msg_cmd) {
			DRM_ERROR("More than one message in a UVD-IB!\n");
			return -EINVAL;
		}
		*has_msg_cmd = true;
		r = radeon_uvd_cs_msg(p, reloc->robj, offset, buf_sizes);
		if (r)
			return r;
	} else if (!*has_msg_cmd) {
		DRM_ERROR("Message needed before other commands are send!\n");
		return -EINVAL;
	}

	return 0;
}

static int radeon_uvd_cs_reg(struct radeon_cs_parser *p,
			     struct radeon_cs_packet *pkt,
			     int *data0, int *data1,
			     unsigned buf_sizes[],
			     bool *has_msg_cmd)
{
	int i, r;

	p->idx++;
	for (i = 0; i <= pkt->count; ++i) {
		switch (pkt->reg + i*4) {
		case UVD_GPCOM_VCPU_DATA0:
			*data0 = p->idx;
			break;
		case UVD_GPCOM_VCPU_DATA1:
			*data1 = p->idx;
			break;
		case UVD_GPCOM_VCPU_CMD:
			r = radeon_uvd_cs_reloc(p, *data0, *data1,
						buf_sizes, has_msg_cmd);
			if (r)
				return r;
			break;
		case UVD_ENGINE_CNTL:
			break;
		default:
			DRM_ERROR("Invalid reg 0x%X!\n",
				  pkt->reg + i*4);
			return -EINVAL;
		}
		p->idx++;
	}
	return 0;
}

int radeon_uvd_cs_parse(struct radeon_cs_parser *p)
{
	struct radeon_cs_packet pkt;
	int r, data0 = 0, data1 = 0;

	/* does the IB has a msg command */
	bool has_msg_cmd = false;

	/* minimum buffer sizes */
	unsigned buf_sizes[] = {
		[0x00000000]	=	2048,
		[0x00000001]	=	32 * 1024 * 1024,
		[0x00000002]	=	2048 * 1152 * 3,
		[0x00000003]	=	2048,
	};

	if (p->chunks[p->chunk_ib_idx].length_dw % 16) {
		DRM_ERROR("UVD IB length (%d) not 16 dwords aligned!\n",
			  p->chunks[p->chunk_ib_idx].length_dw);
		return -EINVAL;
	}

	if (p->chunk_relocs_idx == -1) {
		DRM_ERROR("No relocation chunk !\n");
		return -EINVAL;
	}


	do {
		r = radeon_cs_packet_parse(p, &pkt, p->idx);
		if (r)
			return r;
		switch (pkt.type) {
		case RADEON_PACKET_TYPE0:
			r = radeon_uvd_cs_reg(p, &pkt, &data0, &data1,
					      buf_sizes, &has_msg_cmd);
			if (r)
				return r;
			break;
		case RADEON_PACKET_TYPE2:
			p->idx += pkt.count + 2;
			break;
		default:
			DRM_ERROR("Unknown packet type %d !\n", pkt.type);
			return -EINVAL;
		}
	} while (p->idx < p->chunks[p->chunk_ib_idx].length_dw);

	if (!has_msg_cmd) {
		DRM_ERROR("UVD-IBs need a msg command!\n");
		return -EINVAL;
	}

	return 0;
}

static int radeon_uvd_send_msg(struct radeon_device *rdev,
			       int ring, struct radeon_bo *bo,
			       struct radeon_fence **fence)
{
	struct ttm_validate_buffer tv;
	struct ww_acquire_ctx ticket;
	struct list_head head;
	struct radeon_ib ib;
	uint64_t addr;
	int i, r;

	memset(&tv, 0, sizeof(tv));
	tv.bo = &bo->tbo;

	INIT_LIST_HEAD(&head);
	list_add(&tv.head, &head);

	r = ttm_eu_reserve_buffers(&ticket, &head);
	if (r)
		return r;

	radeon_ttm_placement_from_domain(bo, RADEON_GEM_DOMAIN_VRAM);
	radeon_uvd_force_into_uvd_segment(bo);

	r = ttm_bo_validate(&bo->tbo, &bo->placement, true, false);
	if (r) 
		goto err;

	r = radeon_ib_get(rdev, ring, &ib, NULL, 16);
	if (r)
		goto err;

	addr = radeon_bo_gpu_offset(bo);
	ib.ptr[0] = PACKET0(UVD_GPCOM_VCPU_DATA0, 0);
	ib.ptr[1] = addr;
	ib.ptr[2] = PACKET0(UVD_GPCOM_VCPU_DATA1, 0);
	ib.ptr[3] = addr >> 32;
	ib.ptr[4] = PACKET0(UVD_GPCOM_VCPU_CMD, 0);
	ib.ptr[5] = 0;
	for (i = 6; i < 16; ++i)
		ib.ptr[i] = PACKET2(0);
	ib.length_dw = 16;

	r = radeon_ib_schedule(rdev, &ib, NULL);
	if (r)
		goto err;
	ttm_eu_fence_buffer_objects(&ticket, &head, ib.fence);

	if (fence)
		*fence = radeon_fence_ref(ib.fence);

	radeon_ib_free(rdev, &ib);
	radeon_bo_unref(&bo);
	return 0;

err:
	ttm_eu_backoff_reservation(&ticket, &head);
	return r;
}

/* multiple fence commands without any stream commands in between can
   crash the vcpu so just try to emmit a dummy create/destroy msg to
   avoid this */
int radeon_uvd_get_create_msg(struct radeon_device *rdev, int ring,
			      uint32_t handle, struct radeon_fence **fence)
{
	struct radeon_bo *bo;
	uint32_t *msg;
	int r, i;

	r = radeon_bo_create(rdev, 1024, PAGE_SIZE, true,
			     RADEON_GEM_DOMAIN_VRAM, NULL, &bo);
	if (r)
		return r;

	r = radeon_bo_reserve(bo, false);
	if (r) {
		radeon_bo_unref(&bo);
		return r;
	}

	r = radeon_bo_kmap(bo, (void **)&msg);
	if (r) {
		radeon_bo_unreserve(bo);
		radeon_bo_unref(&bo);
		return r;
	}

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

	radeon_bo_kunmap(bo);
	radeon_bo_unreserve(bo);

	return radeon_uvd_send_msg(rdev, ring, bo, fence);
}

int radeon_uvd_get_destroy_msg(struct radeon_device *rdev, int ring,
			       uint32_t handle, struct radeon_fence **fence)
{
	struct radeon_bo *bo;
	uint32_t *msg;
	int r, i;

	r = radeon_bo_create(rdev, 1024, PAGE_SIZE, true,
			     RADEON_GEM_DOMAIN_VRAM, NULL, &bo);
	if (r)
		return r;

	r = radeon_bo_reserve(bo, false);
	if (r) {
		radeon_bo_unref(&bo);
		return r;
	}

	r = radeon_bo_kmap(bo, (void **)&msg);
	if (r) {
		radeon_bo_unreserve(bo);
		radeon_bo_unref(&bo);
		return r;
	}

	/* stitch together an UVD destroy msg */
	msg[0] = cpu_to_le32(0x00000de4);
	msg[1] = cpu_to_le32(0x00000002);
	msg[2] = cpu_to_le32(handle);
	msg[3] = cpu_to_le32(0x00000000);
	for (i = 4; i < 1024; ++i)
		msg[i] = cpu_to_le32(0x0);

	radeon_bo_kunmap(bo);
	radeon_bo_unreserve(bo);

	return radeon_uvd_send_msg(rdev, ring, bo, fence);
}

static void radeon_uvd_idle_work_handler(struct work_struct *work)
{
	struct radeon_device *rdev =
		container_of(work, struct radeon_device, uvd.idle_work.work);

	if (radeon_fence_count_emitted(rdev, R600_RING_TYPE_UVD_INDEX) == 0) {
		if ((rdev->pm.pm_method == PM_METHOD_DPM) && rdev->pm.dpm_enabled) {
			mutex_lock(&rdev->pm.mutex);
			rdev->pm.dpm.uvd_active = false;
			mutex_unlock(&rdev->pm.mutex);
			radeon_pm_compute_clocks(rdev);
		} else {
			radeon_set_uvd_clocks(rdev, 0, 0);
		}
	} else {
		schedule_delayed_work(&rdev->uvd.idle_work,
				      msecs_to_jiffies(UVD_IDLE_TIMEOUT_MS));
	}
}

void radeon_uvd_note_usage(struct radeon_device *rdev)
{
	bool set_clocks = !cancel_delayed_work_sync(&rdev->uvd.idle_work);
	set_clocks &= schedule_delayed_work(&rdev->uvd.idle_work,
					    msecs_to_jiffies(UVD_IDLE_TIMEOUT_MS));
	if (set_clocks) {
		if ((rdev->pm.pm_method == PM_METHOD_DPM) && rdev->pm.dpm_enabled) {
			/* XXX pick SD/HD/MVC */
			radeon_dpm_enable_power_state(rdev, POWER_STATE_TYPE_INTERNAL_UVD);
		} else {
			radeon_set_uvd_clocks(rdev, 53300, 40000);
		}
	}
}

static unsigned radeon_uvd_calc_upll_post_div(unsigned vco_freq,
					      unsigned target_freq,
					      unsigned pd_min,
					      unsigned pd_even)
{
	unsigned post_div = vco_freq / target_freq;

	/* adjust to post divider minimum value */
	if (post_div < pd_min)
		post_div = pd_min;

	/* we alway need a frequency less than or equal the target */
	if ((vco_freq / post_div) > target_freq)
		post_div += 1;

	/* post dividers above a certain value must be even */
	if (post_div > pd_even && post_div % 2)
		post_div += 1;

	return post_div;
}

/**
 * radeon_uvd_calc_upll_dividers - calc UPLL clock dividers
 *
 * @rdev: radeon_device pointer
 * @vclk: wanted VCLK
 * @dclk: wanted DCLK
 * @vco_min: minimum VCO frequency
 * @vco_max: maximum VCO frequency
 * @fb_factor: factor to multiply vco freq with
 * @fb_mask: limit and bitmask for feedback divider
 * @pd_min: post divider minimum
 * @pd_max: post divider maximum
 * @pd_even: post divider must be even above this value
 * @optimal_fb_div: resulting feedback divider
 * @optimal_vclk_div: resulting vclk post divider
 * @optimal_dclk_div: resulting dclk post divider
 *
 * Calculate dividers for UVDs UPLL (R6xx-SI, except APUs).
 * Returns zero on success -EINVAL on error.
 */
int radeon_uvd_calc_upll_dividers(struct radeon_device *rdev,
				  unsigned vclk, unsigned dclk,
				  unsigned vco_min, unsigned vco_max,
				  unsigned fb_factor, unsigned fb_mask,
				  unsigned pd_min, unsigned pd_max,
				  unsigned pd_even,
				  unsigned *optimal_fb_div,
				  unsigned *optimal_vclk_div,
				  unsigned *optimal_dclk_div)
{
	unsigned vco_freq, ref_freq = rdev->clock.spll.reference_freq;

	/* start off with something large */
	unsigned optimal_score = ~0;

	/* loop through vco from low to high */
	vco_min = max(max(vco_min, vclk), dclk);
	for (vco_freq = vco_min; vco_freq <= vco_max; vco_freq += 100) {

		uint64_t fb_div = (uint64_t)vco_freq * fb_factor;
		unsigned vclk_div, dclk_div, score;

		do_div(fb_div, ref_freq);

		/* fb div out of range ? */
		if (fb_div > fb_mask)
			break; /* it can oly get worse */

		fb_div &= fb_mask;

		/* calc vclk divider with current vco freq */
		vclk_div = radeon_uvd_calc_upll_post_div(vco_freq, vclk,
							 pd_min, pd_even);
		if (vclk_div > pd_max)
			break; /* vco is too big, it has to stop */

		/* calc dclk divider with current vco freq */
		dclk_div = radeon_uvd_calc_upll_post_div(vco_freq, dclk,
							 pd_min, pd_even);
		if (vclk_div > pd_max)
			break; /* vco is too big, it has to stop */

		/* calc score with current vco freq */
		score = vclk - (vco_freq / vclk_div) + dclk - (vco_freq / dclk_div);

		/* determine if this vco setting is better than current optimal settings */
		if (score < optimal_score) {
			*optimal_fb_div = fb_div;
			*optimal_vclk_div = vclk_div;
			*optimal_dclk_div = dclk_div;
			optimal_score = score;
			if (optimal_score == 0)
				break; /* it can't get better than this */
		}
	}

	/* did we found a valid setup ? */
	if (optimal_score == ~0)
		return -EINVAL;

	return 0;
}

int radeon_uvd_send_upll_ctlreq(struct radeon_device *rdev,
				unsigned cg_upll_func_cntl)
{
	unsigned i;

	/* make sure UPLL_CTLREQ is deasserted */
	WREG32_P(cg_upll_func_cntl, 0, ~UPLL_CTLREQ_MASK);

	mdelay(10);

	/* assert UPLL_CTLREQ */
	WREG32_P(cg_upll_func_cntl, UPLL_CTLREQ_MASK, ~UPLL_CTLREQ_MASK);

	/* wait for CTLACK and CTLACK2 to get asserted */
	for (i = 0; i < 100; ++i) {
		uint32_t mask = UPLL_CTLACK_MASK | UPLL_CTLACK2_MASK;
		if ((RREG32(cg_upll_func_cntl) & mask) == mask)
			break;
		mdelay(10);
	}

	/* deassert UPLL_CTLREQ */
	WREG32_P(cg_upll_func_cntl, 0, ~UPLL_CTLREQ_MASK);

	if (i == 100) {
		DRM_ERROR("Timeout setting UVD clocks!\n");
		return -ETIMEDOUT;
	}

	return 0;
}

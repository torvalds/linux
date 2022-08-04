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

#include "radeon.h"
#include "radeon_ucode.h"
#include "r600d.h"

/* 1 second timeout */
#define UVD_IDLE_TIMEOUT_MS	1000

/* Firmware Names */
#define FIRMWARE_R600		"radeon/R600_uvd.bin"
#define FIRMWARE_RS780		"radeon/RS780_uvd.bin"
#define FIRMWARE_RV770		"radeon/RV770_uvd.bin"
#define FIRMWARE_RV710		"radeon/RV710_uvd.bin"
#define FIRMWARE_CYPRESS	"radeon/CYPRESS_uvd.bin"
#define FIRMWARE_SUMO		"radeon/SUMO_uvd.bin"
#define FIRMWARE_TAHITI		"radeon/TAHITI_uvd.bin"
#define FIRMWARE_BONAIRE_LEGACY	"radeon/BONAIRE_uvd.bin"
#define FIRMWARE_BONAIRE	"radeon/bonaire_uvd.bin"

MODULE_FIRMWARE(FIRMWARE_R600);
MODULE_FIRMWARE(FIRMWARE_RS780);
MODULE_FIRMWARE(FIRMWARE_RV770);
MODULE_FIRMWARE(FIRMWARE_RV710);
MODULE_FIRMWARE(FIRMWARE_CYPRESS);
MODULE_FIRMWARE(FIRMWARE_SUMO);
MODULE_FIRMWARE(FIRMWARE_TAHITI);
MODULE_FIRMWARE(FIRMWARE_BONAIRE_LEGACY);
MODULE_FIRMWARE(FIRMWARE_BONAIRE);

static void radeon_uvd_idle_work_handler(struct work_struct *work);

int radeon_uvd_init(struct radeon_device *rdev)
{
	unsigned long bo_size;
	const char *fw_name = NULL, *legacy_fw_name = NULL;
	int i, r;

	INIT_DELAYED_WORK(&rdev->uvd.idle_work, radeon_uvd_idle_work_handler);

	switch (rdev->family) {
	case CHIP_RV610:
	case CHIP_RV630:
	case CHIP_RV670:
	case CHIP_RV620:
	case CHIP_RV635:
		legacy_fw_name = FIRMWARE_R600;
		break;

	case CHIP_RS780:
	case CHIP_RS880:
		legacy_fw_name = FIRMWARE_RS780;
		break;

	case CHIP_RV770:
		legacy_fw_name = FIRMWARE_RV770;
		break;

	case CHIP_RV710:
	case CHIP_RV730:
	case CHIP_RV740:
		legacy_fw_name = FIRMWARE_RV710;
		break;

	case CHIP_CYPRESS:
	case CHIP_HEMLOCK:
	case CHIP_JUNIPER:
	case CHIP_REDWOOD:
	case CHIP_CEDAR:
		legacy_fw_name = FIRMWARE_CYPRESS;
		break;

	case CHIP_SUMO:
	case CHIP_SUMO2:
	case CHIP_PALM:
	case CHIP_CAYMAN:
	case CHIP_BARTS:
	case CHIP_TURKS:
	case CHIP_CAICOS:
		legacy_fw_name = FIRMWARE_SUMO;
		break;

	case CHIP_TAHITI:
	case CHIP_VERDE:
	case CHIP_PITCAIRN:
	case CHIP_ARUBA:
	case CHIP_OLAND:
		legacy_fw_name = FIRMWARE_TAHITI;
		break;

	case CHIP_BONAIRE:
	case CHIP_KABINI:
	case CHIP_KAVERI:
	case CHIP_HAWAII:
	case CHIP_MULLINS:
		legacy_fw_name = FIRMWARE_BONAIRE_LEGACY;
		fw_name = FIRMWARE_BONAIRE;
		break;

	default:
		return -EINVAL;
	}

	rdev->uvd.fw_header_present = false;
	rdev->uvd.max_handles = RADEON_DEFAULT_UVD_HANDLES;
	if (fw_name) {
		/* Let's try to load the newer firmware first */
		r = request_firmware(&rdev->uvd_fw, fw_name, rdev->dev);
		if (r) {
			dev_err(rdev->dev, "radeon_uvd: Can't load firmware \"%s\"\n",
				fw_name);
		} else {
			struct common_firmware_header *hdr = (void *)rdev->uvd_fw->data;
			unsigned version_major, version_minor, family_id;

			r = radeon_ucode_validate(rdev->uvd_fw);
			if (r)
				return r;

			rdev->uvd.fw_header_present = true;

			family_id = le32_to_cpu(hdr->ucode_version) & 0xff;
			version_major = (le32_to_cpu(hdr->ucode_version) >> 24) & 0xff;
			version_minor = (le32_to_cpu(hdr->ucode_version) >> 8) & 0xff;
			DRM_INFO("Found UVD firmware Version: %u.%u Family ID: %u\n",
				 version_major, version_minor, family_id);

			/*
			 * Limit the number of UVD handles depending on
			 * microcode major and minor versions.
			 */
			if ((version_major >= 0x01) && (version_minor >= 0x37))
				rdev->uvd.max_handles = RADEON_MAX_UVD_HANDLES;
		}
	}

	/*
	 * In case there is only legacy firmware, or we encounter an error
	 * while loading the new firmware, we fall back to loading the legacy
	 * firmware now.
	 */
	if (!fw_name || r) {
		r = request_firmware(&rdev->uvd_fw, legacy_fw_name, rdev->dev);
		if (r) {
			dev_err(rdev->dev, "radeon_uvd: Can't load firmware \"%s\"\n",
				legacy_fw_name);
			return r;
		}
	}

	bo_size = RADEON_GPU_PAGE_ALIGN(rdev->uvd_fw->size + 8) +
		  RADEON_UVD_STACK_SIZE + RADEON_UVD_HEAP_SIZE +
		  RADEON_UVD_SESSION_SIZE * rdev->uvd.max_handles;
	r = radeon_bo_create(rdev, bo_size, PAGE_SIZE, true,
			     RADEON_GEM_DOMAIN_VRAM, 0, NULL,
			     NULL, &rdev->uvd.vcpu_bo);
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

	for (i = 0; i < rdev->uvd.max_handles; ++i) {
		atomic_set(&rdev->uvd.handles[i], 0);
		rdev->uvd.filp[i] = NULL;
		rdev->uvd.img_size[i] = 0;
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

	radeon_ring_fini(rdev, &rdev->ring[R600_RING_TYPE_UVD_INDEX]);

	release_firmware(rdev->uvd_fw);
}

int radeon_uvd_suspend(struct radeon_device *rdev)
{
	int i, r;

	if (rdev->uvd.vcpu_bo == NULL)
		return 0;

	for (i = 0; i < rdev->uvd.max_handles; ++i) {
		uint32_t handle = atomic_read(&rdev->uvd.handles[i]);
		if (handle != 0) {
			struct radeon_fence *fence;

			radeon_uvd_note_usage(rdev);

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

	memset(ptr, 0, size);

	return 0;
}

void radeon_uvd_force_into_uvd_segment(struct radeon_bo *rbo,
				       uint32_t allowed_domains)
{
	int i;

	for (i = 0; i < rbo->placement.num_placement; ++i) {
		rbo->placements[i].fpfn = 0 >> PAGE_SHIFT;
		rbo->placements[i].lpfn = (256 * 1024 * 1024) >> PAGE_SHIFT;
	}

	/* If it must be in VRAM it must be in the first segment as well */
	if (allowed_domains == RADEON_GEM_DOMAIN_VRAM)
		return;

	/* abort if we already have more than one placement */
	if (rbo->placement.num_placement > 1)
		return;

	/* add another 256MB segment */
	rbo->placements[1] = rbo->placements[0];
	rbo->placements[1].fpfn += (256 * 1024 * 1024) >> PAGE_SHIFT;
	rbo->placements[1].lpfn += (256 * 1024 * 1024) >> PAGE_SHIFT;
	rbo->placement.num_placement++;
	rbo->placement.num_busy_placement++;
}

void radeon_uvd_free_handles(struct radeon_device *rdev, struct drm_file *filp)
{
	int i, r;
	for (i = 0; i < rdev->uvd.max_handles; ++i) {
		uint32_t handle = atomic_read(&rdev->uvd.handles[i]);
		if (handle != 0 && rdev->uvd.filp[i] == filp) {
			struct radeon_fence *fence;

			radeon_uvd_note_usage(rdev);

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

static int radeon_uvd_validate_codec(struct radeon_cs_parser *p,
				     unsigned stream_type)
{
	switch (stream_type) {
	case 0: /* H264 */
	case 1: /* VC1 */
		/* always supported */
		return 0;

	case 3: /* MPEG2 */
	case 4: /* MPEG4 */
		/* only since UVD 3 */
		if (p->rdev->family >= CHIP_PALM)
			return 0;

		fallthrough;
	default:
		DRM_ERROR("UVD codec not supported by hardware %d!\n",
			  stream_type);
		return -EINVAL;
	}
}

static int radeon_uvd_cs_msg(struct radeon_cs_parser *p, struct radeon_bo *bo,
			     unsigned offset, unsigned buf_sizes[])
{
	int32_t *msg, msg_type, handle;
	unsigned img_size = 0;
	struct dma_fence *f;
	void *ptr;

	int i, r;

	if (offset & 0x3F) {
		DRM_ERROR("UVD messages must be 64 byte aligned!\n");
		return -EINVAL;
	}

	f = dma_resv_get_excl(bo->tbo.base.resv);
	if (f) {
		r = radeon_fence_wait((struct radeon_fence *)f, false);
		if (r) {
			DRM_ERROR("Failed waiting for UVD message (%d)!\n", r);
			return r;
		}
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

	switch (msg_type) {
	case 0:
		/* it's a create msg, calc image size (width * height) */
		img_size = msg[7] * msg[8];

		r = radeon_uvd_validate_codec(p, msg[4]);
		radeon_bo_kunmap(bo);
		if (r)
			return r;

		/* try to alloc a new handle */
		for (i = 0; i < p->rdev->uvd.max_handles; ++i) {
			if (atomic_read(&p->rdev->uvd.handles[i]) == handle) {
				DRM_ERROR("Handle 0x%x already in use!\n", handle);
				return -EINVAL;
			}

			if (!atomic_cmpxchg(&p->rdev->uvd.handles[i], 0, handle)) {
				p->rdev->uvd.filp[i] = p->filp;
				p->rdev->uvd.img_size[i] = img_size;
				return 0;
			}
		}

		DRM_ERROR("No more free UVD handles!\n");
		return -EINVAL;

	case 1:
		/* it's a decode msg, validate codec and calc buffer sizes */
		r = radeon_uvd_validate_codec(p, msg[4]);
		if (!r)
			r = radeon_uvd_cs_msg_decode(msg, buf_sizes);
		radeon_bo_kunmap(bo);
		if (r)
			return r;

		/* validate the handle */
		for (i = 0; i < p->rdev->uvd.max_handles; ++i) {
			if (atomic_read(&p->rdev->uvd.handles[i]) == handle) {
				if (p->rdev->uvd.filp[i] != p->filp) {
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
		for (i = 0; i < p->rdev->uvd.max_handles; ++i)
			atomic_cmpxchg(&p->rdev->uvd.handles[i], handle, 0);
		radeon_bo_kunmap(bo);
		return 0;

	default:

		DRM_ERROR("Illegal UVD message type (%d)!\n", msg_type);
		return -EINVAL;
	}

	BUG();
	return -EINVAL;
}

static int radeon_uvd_cs_reloc(struct radeon_cs_parser *p,
			       int data0, int data1,
			       unsigned buf_sizes[], bool *has_msg_cmd)
{
	struct radeon_cs_chunk *relocs_chunk;
	struct radeon_bo_list *reloc;
	unsigned idx, cmd, offset;
	uint64_t start, end;
	int r;

	relocs_chunk = p->chunk_relocs;
	offset = radeon_get_ib_value(p, data0);
	idx = radeon_get_ib_value(p, data1);
	if (idx >= relocs_chunk->length_dw) {
		DRM_ERROR("Relocs at %d after relocations chunk end %d !\n",
			  idx, relocs_chunk->length_dw);
		return -EINVAL;
	}

	reloc = &p->relocs[(idx / 4)];
	start = reloc->gpu_offset;
	end = start + radeon_bo_size(reloc->robj);
	start += offset;

	p->ib.ptr[data0] = start & 0xFFFFFFFF;
	p->ib.ptr[data1] = start >> 32;

	cmd = radeon_get_ib_value(p, p->idx) >> 1;

	if (cmd < 0x4) {
		if (end <= start) {
			DRM_ERROR("invalid reloc offset %X!\n", offset);
			return -EINVAL;
		}
		if ((end - start) < buf_sizes[cmd]) {
			DRM_ERROR("buffer (%d) to small (%d / %d)!\n", cmd,
				  (unsigned)(end - start), buf_sizes[cmd]);
			return -EINVAL;
		}

	} else if (cmd != 0x100) {
		DRM_ERROR("invalid UVD command %X!\n", cmd);
		return -EINVAL;
	}

	if ((start >> 28) != ((end - 1) >> 28)) {
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
		case UVD_NO_OP:
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

	if (p->chunk_ib->length_dw % 16) {
		DRM_ERROR("UVD IB length (%d) not 16 dwords aligned!\n",
			  p->chunk_ib->length_dw);
		return -EINVAL;
	}

	if (p->chunk_relocs == NULL) {
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
	} while (p->idx < p->chunk_ib->length_dw);

	if (!has_msg_cmd) {
		DRM_ERROR("UVD-IBs need a msg command!\n");
		return -EINVAL;
	}

	return 0;
}

static int radeon_uvd_send_msg(struct radeon_device *rdev,
			       int ring, uint64_t addr,
			       struct radeon_fence **fence)
{
	struct radeon_ib ib;
	int i, r;

	r = radeon_ib_get(rdev, ring, &ib, NULL, 64);
	if (r)
		return r;

	ib.ptr[0] = PACKET0(UVD_GPCOM_VCPU_DATA0, 0);
	ib.ptr[1] = addr;
	ib.ptr[2] = PACKET0(UVD_GPCOM_VCPU_DATA1, 0);
	ib.ptr[3] = addr >> 32;
	ib.ptr[4] = PACKET0(UVD_GPCOM_VCPU_CMD, 0);
	ib.ptr[5] = 0;
	for (i = 6; i < 16; i += 2) {
		ib.ptr[i] = PACKET0(UVD_NO_OP, 0);
		ib.ptr[i+1] = 0;
	}
	ib.length_dw = 16;

	r = radeon_ib_schedule(rdev, &ib, NULL, false);

	if (fence)
		*fence = radeon_fence_ref(ib.fence);

	radeon_ib_free(rdev, &ib);
	return r;
}

/*
 * multiple fence commands without any stream commands in between can
 * crash the vcpu so just try to emmit a dummy create/destroy msg to
 * avoid this
 */
int radeon_uvd_get_create_msg(struct radeon_device *rdev, int ring,
			      uint32_t handle, struct radeon_fence **fence)
{
	/* we use the last page of the vcpu bo for the UVD message */
	uint64_t offs = radeon_bo_size(rdev->uvd.vcpu_bo) -
		RADEON_GPU_PAGE_SIZE;

	uint32_t *msg = rdev->uvd.cpu_addr + offs;
	uint64_t addr = rdev->uvd.gpu_addr + offs;

	int r, i;

	r = radeon_bo_reserve(rdev->uvd.vcpu_bo, true);
	if (r)
		return r;

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

	r = radeon_uvd_send_msg(rdev, ring, addr, fence);
	radeon_bo_unreserve(rdev->uvd.vcpu_bo);
	return r;
}

int radeon_uvd_get_destroy_msg(struct radeon_device *rdev, int ring,
			       uint32_t handle, struct radeon_fence **fence)
{
	/* we use the last page of the vcpu bo for the UVD message */
	uint64_t offs = radeon_bo_size(rdev->uvd.vcpu_bo) -
		RADEON_GPU_PAGE_SIZE;

	uint32_t *msg = rdev->uvd.cpu_addr + offs;
	uint64_t addr = rdev->uvd.gpu_addr + offs;

	int r, i;

	r = radeon_bo_reserve(rdev->uvd.vcpu_bo, true);
	if (r)
		return r;

	/* stitch together an UVD destroy msg */
	msg[0] = cpu_to_le32(0x00000de4);
	msg[1] = cpu_to_le32(0x00000002);
	msg[2] = cpu_to_le32(handle);
	msg[3] = cpu_to_le32(0x00000000);
	for (i = 4; i < 1024; ++i)
		msg[i] = cpu_to_le32(0x0);

	r = radeon_uvd_send_msg(rdev, ring, addr, fence);
	radeon_bo_unreserve(rdev->uvd.vcpu_bo);
	return r;
}

/**
 * radeon_uvd_count_handles - count number of open streams
 *
 * @rdev: radeon_device pointer
 * @sd: number of SD streams
 * @hd: number of HD streams
 *
 * Count the number of open SD/HD streams as a hint for power mangement
 */
static void radeon_uvd_count_handles(struct radeon_device *rdev,
				     unsigned *sd, unsigned *hd)
{
	unsigned i;

	*sd = 0;
	*hd = 0;

	for (i = 0; i < rdev->uvd.max_handles; ++i) {
		if (!atomic_read(&rdev->uvd.handles[i]))
			continue;

		if (rdev->uvd.img_size[i] >= 720*576)
			++(*hd);
		else
			++(*sd);
	}
}

static void radeon_uvd_idle_work_handler(struct work_struct *work)
{
	struct radeon_device *rdev =
		container_of(work, struct radeon_device, uvd.idle_work.work);

	if (radeon_fence_count_emitted(rdev, R600_RING_TYPE_UVD_INDEX) == 0) {
		if ((rdev->pm.pm_method == PM_METHOD_DPM) && rdev->pm.dpm_enabled) {
			radeon_uvd_count_handles(rdev, &rdev->pm.dpm.sd,
						 &rdev->pm.dpm.hd);
			radeon_dpm_enable_uvd(rdev, false);
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
	bool streams_changed = false;
	bool set_clocks = !cancel_delayed_work_sync(&rdev->uvd.idle_work);
	set_clocks &= schedule_delayed_work(&rdev->uvd.idle_work,
					    msecs_to_jiffies(UVD_IDLE_TIMEOUT_MS));

	if ((rdev->pm.pm_method == PM_METHOD_DPM) && rdev->pm.dpm_enabled) {
		unsigned hd = 0, sd = 0;
		radeon_uvd_count_handles(rdev, &sd, &hd);
		if ((rdev->pm.dpm.sd != sd) ||
		    (rdev->pm.dpm.hd != hd)) {
			rdev->pm.dpm.sd = sd;
			rdev->pm.dpm.hd = hd;
			/* disable this for now */
			/*streams_changed = true;*/
		}
	}

	if (set_clocks || streams_changed) {
		if ((rdev->pm.pm_method == PM_METHOD_DPM) && rdev->pm.dpm_enabled) {
			radeon_dpm_enable_uvd(rdev, true);
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
		if (dclk_div > pd_max)
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

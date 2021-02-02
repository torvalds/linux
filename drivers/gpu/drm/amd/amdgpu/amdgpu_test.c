// SPDX-License-Identifier: GPL-2.0 OR MIT
/*
 * Copyright 2009 VMware, Inc.
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
 * Authors: Michel Dänzer
 */

#include <drm/amdgpu_drm.h>
#include "amdgpu.h"
#include "amdgpu_uvd.h"
#include "amdgpu_vce.h"

/* Test BO GTT->VRAM and VRAM->GTT GPU copies across the whole GTT aperture */
static void amdgpu_do_test_moves(struct amdgpu_device *adev)
{
	struct amdgpu_ring *ring = adev->mman.buffer_funcs_ring;
	struct amdgpu_bo *vram_obj = NULL;
	struct amdgpu_bo **gtt_obj = NULL;
	struct amdgpu_bo_param bp;
	uint64_t gart_addr, vram_addr;
	unsigned n, size;
	int i, r;

	size = 1024 * 1024;

	/* Number of tests =
	 * (Total GTT - gart_pin_size - (2 transfer windows for buffer moves)) / test size
	 */
	n = adev->gmc.gart_size - atomic64_read(&adev->gart_pin_size);
	n -= AMDGPU_GTT_MAX_TRANSFER_SIZE * AMDGPU_GTT_NUM_TRANSFER_WINDOWS *
		AMDGPU_GPU_PAGE_SIZE;
	n /= size;

	gtt_obj = kcalloc(n, sizeof(*gtt_obj), GFP_KERNEL);
	if (!gtt_obj) {
		DRM_ERROR("Failed to allocate %d pointers\n", n);
		r = 1;
		goto out_cleanup;
	}
	memset(&bp, 0, sizeof(bp));
	bp.size = size;
	bp.byte_align = PAGE_SIZE;
	bp.domain = AMDGPU_GEM_DOMAIN_VRAM;
	bp.flags = 0;
	bp.type = ttm_bo_type_kernel;
	bp.resv = NULL;

	r = amdgpu_bo_create(adev, &bp, &vram_obj);
	if (r) {
		DRM_ERROR("Failed to create VRAM object\n");
		goto out_cleanup;
	}
	r = amdgpu_bo_reserve(vram_obj, false);
	if (unlikely(r != 0))
		goto out_unref;
	r = amdgpu_bo_pin(vram_obj, AMDGPU_GEM_DOMAIN_VRAM);
	if (r) {
		DRM_ERROR("Failed to pin VRAM object\n");
		goto out_unres;
	}
	vram_addr = amdgpu_bo_gpu_offset(vram_obj);
	for (i = 0; i < n; i++) {
		void *gtt_map, *vram_map;
		void **gart_start, **gart_end;
		void **vram_start, **vram_end;
		struct dma_fence *fence = NULL;

		bp.domain = AMDGPU_GEM_DOMAIN_GTT;
		r = amdgpu_bo_create(adev, &bp, gtt_obj + i);
		if (r) {
			DRM_ERROR("Failed to create GTT object %d\n", i);
			goto out_lclean;
		}

		r = amdgpu_bo_reserve(gtt_obj[i], false);
		if (unlikely(r != 0))
			goto out_lclean_unref;
		r = amdgpu_bo_pin(gtt_obj[i], AMDGPU_GEM_DOMAIN_GTT);
		if (r) {
			DRM_ERROR("Failed to pin GTT object %d\n", i);
			goto out_lclean_unres;
		}
		r = amdgpu_ttm_alloc_gart(&gtt_obj[i]->tbo);
		if (r) {
			DRM_ERROR("%p bind failed\n", gtt_obj[i]);
			goto out_lclean_unpin;
		}
		gart_addr = amdgpu_bo_gpu_offset(gtt_obj[i]);

		r = amdgpu_bo_kmap(gtt_obj[i], &gtt_map);
		if (r) {
			DRM_ERROR("Failed to map GTT object %d\n", i);
			goto out_lclean_unpin;
		}

		for (gart_start = gtt_map, gart_end = gtt_map + size;
		     gart_start < gart_end;
		     gart_start++)
			*gart_start = gart_start;

		amdgpu_bo_kunmap(gtt_obj[i]);

		r = amdgpu_copy_buffer(ring, gart_addr, vram_addr,
				       size, NULL, &fence, false, false, false);

		if (r) {
			DRM_ERROR("Failed GTT->VRAM copy %d\n", i);
			goto out_lclean_unpin;
		}

		r = dma_fence_wait(fence, false);
		if (r) {
			DRM_ERROR("Failed to wait for GTT->VRAM fence %d\n", i);
			goto out_lclean_unpin;
		}

		dma_fence_put(fence);
		fence = NULL;

		r = amdgpu_bo_kmap(vram_obj, &vram_map);
		if (r) {
			DRM_ERROR("Failed to map VRAM object after copy %d\n", i);
			goto out_lclean_unpin;
		}

		for (gart_start = gtt_map, gart_end = gtt_map + size,
		     vram_start = vram_map, vram_end = vram_map + size;
		     vram_start < vram_end;
		     gart_start++, vram_start++) {
			if (*vram_start != gart_start) {
				DRM_ERROR("Incorrect GTT->VRAM copy %d: Got 0x%p, "
					  "expected 0x%p (GTT/VRAM offset "
					  "0x%16llx/0x%16llx)\n",
					  i, *vram_start, gart_start,
					  (unsigned long long)
					  (gart_addr - adev->gmc.gart_start +
					   (void *)gart_start - gtt_map),
					  (unsigned long long)
					  (vram_addr - adev->gmc.vram_start +
					   (void *)gart_start - gtt_map));
				amdgpu_bo_kunmap(vram_obj);
				goto out_lclean_unpin;
			}
			*vram_start = vram_start;
		}

		amdgpu_bo_kunmap(vram_obj);

		r = amdgpu_copy_buffer(ring, vram_addr, gart_addr,
				       size, NULL, &fence, false, false, false);

		if (r) {
			DRM_ERROR("Failed VRAM->GTT copy %d\n", i);
			goto out_lclean_unpin;
		}

		r = dma_fence_wait(fence, false);
		if (r) {
			DRM_ERROR("Failed to wait for VRAM->GTT fence %d\n", i);
			goto out_lclean_unpin;
		}

		dma_fence_put(fence);
		fence = NULL;

		r = amdgpu_bo_kmap(gtt_obj[i], &gtt_map);
		if (r) {
			DRM_ERROR("Failed to map GTT object after copy %d\n", i);
			goto out_lclean_unpin;
		}

		for (gart_start = gtt_map, gart_end = gtt_map + size,
		     vram_start = vram_map, vram_end = vram_map + size;
		     gart_start < gart_end;
		     gart_start++, vram_start++) {
			if (*gart_start != vram_start) {
				DRM_ERROR("Incorrect VRAM->GTT copy %d: Got 0x%p, "
					  "expected 0x%p (VRAM/GTT offset "
					  "0x%16llx/0x%16llx)\n",
					  i, *gart_start, vram_start,
					  (unsigned long long)
					  (vram_addr - adev->gmc.vram_start +
					   (void *)vram_start - vram_map),
					  (unsigned long long)
					  (gart_addr - adev->gmc.gart_start +
					   (void *)vram_start - vram_map));
				amdgpu_bo_kunmap(gtt_obj[i]);
				goto out_lclean_unpin;
			}
		}

		amdgpu_bo_kunmap(gtt_obj[i]);

		DRM_INFO("Tested GTT->VRAM and VRAM->GTT copy for GTT offset 0x%llx\n",
			 gart_addr - adev->gmc.gart_start);
		continue;

out_lclean_unpin:
		amdgpu_bo_unpin(gtt_obj[i]);
out_lclean_unres:
		amdgpu_bo_unreserve(gtt_obj[i]);
out_lclean_unref:
		amdgpu_bo_unref(&gtt_obj[i]);
out_lclean:
		for (--i; i >= 0; --i) {
			amdgpu_bo_unpin(gtt_obj[i]);
			amdgpu_bo_unreserve(gtt_obj[i]);
			amdgpu_bo_unref(&gtt_obj[i]);
		}
		if (fence)
			dma_fence_put(fence);
		break;
	}

	amdgpu_bo_unpin(vram_obj);
out_unres:
	amdgpu_bo_unreserve(vram_obj);
out_unref:
	amdgpu_bo_unref(&vram_obj);
out_cleanup:
	kfree(gtt_obj);
	if (r) {
		pr_warn("Error while testing BO move\n");
	}
}

void amdgpu_test_moves(struct amdgpu_device *adev)
{
	if (adev->mman.buffer_funcs)
		amdgpu_do_test_moves(adev);
}

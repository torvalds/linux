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
#include <drm/drmP.h>
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
	uint64_t gtt_addr, vram_addr;
	unsigned n, size;
	int i, r;

	size = 1024 * 1024;

	/* Number of tests =
	 * (Total GTT - IB pool - writeback page - ring buffers) / test size
	 */
	n = adev->mc.gtt_size - AMDGPU_IB_POOL_SIZE*64*1024;
	for (i = 0; i < AMDGPU_MAX_RINGS; ++i)
		if (adev->rings[i])
			n -= adev->rings[i]->ring_size;
	if (adev->wb.wb_obj)
		n -= AMDGPU_GPU_PAGE_SIZE;
	if (adev->irq.ih.ring_obj)
		n -= adev->irq.ih.ring_size;
	n /= size;

	gtt_obj = kzalloc(n * sizeof(*gtt_obj), GFP_KERNEL);
	if (!gtt_obj) {
		DRM_ERROR("Failed to allocate %d pointers\n", n);
		r = 1;
		goto out_cleanup;
	}

	r = amdgpu_bo_create(adev, size, PAGE_SIZE, true,
			     AMDGPU_GEM_DOMAIN_VRAM, 0,
			     NULL, NULL, &vram_obj);
	if (r) {
		DRM_ERROR("Failed to create VRAM object\n");
		goto out_cleanup;
	}
	r = amdgpu_bo_reserve(vram_obj, false);
	if (unlikely(r != 0))
		goto out_unref;
	r = amdgpu_bo_pin(vram_obj, AMDGPU_GEM_DOMAIN_VRAM, &vram_addr);
	if (r) {
		DRM_ERROR("Failed to pin VRAM object\n");
		goto out_unres;
	}
	for (i = 0; i < n; i++) {
		void *gtt_map, *vram_map;
		void **gtt_start, **gtt_end;
		void **vram_start, **vram_end;
		struct dma_fence *fence = NULL;

		r = amdgpu_bo_create(adev, size, PAGE_SIZE, true,
				     AMDGPU_GEM_DOMAIN_GTT, 0, NULL,
				     NULL, gtt_obj + i);
		if (r) {
			DRM_ERROR("Failed to create GTT object %d\n", i);
			goto out_lclean;
		}

		r = amdgpu_bo_reserve(gtt_obj[i], false);
		if (unlikely(r != 0))
			goto out_lclean_unref;
		r = amdgpu_bo_pin(gtt_obj[i], AMDGPU_GEM_DOMAIN_GTT, &gtt_addr);
		if (r) {
			DRM_ERROR("Failed to pin GTT object %d\n", i);
			goto out_lclean_unres;
		}

		r = amdgpu_bo_kmap(gtt_obj[i], &gtt_map);
		if (r) {
			DRM_ERROR("Failed to map GTT object %d\n", i);
			goto out_lclean_unpin;
		}

		for (gtt_start = gtt_map, gtt_end = gtt_map + size;
		     gtt_start < gtt_end;
		     gtt_start++)
			*gtt_start = gtt_start;

		amdgpu_bo_kunmap(gtt_obj[i]);

		r = amdgpu_copy_buffer(ring, gtt_addr, vram_addr,
				       size, NULL, &fence, false);

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

		r = amdgpu_bo_kmap(vram_obj, &vram_map);
		if (r) {
			DRM_ERROR("Failed to map VRAM object after copy %d\n", i);
			goto out_lclean_unpin;
		}

		for (gtt_start = gtt_map, gtt_end = gtt_map + size,
		     vram_start = vram_map, vram_end = vram_map + size;
		     vram_start < vram_end;
		     gtt_start++, vram_start++) {
			if (*vram_start != gtt_start) {
				DRM_ERROR("Incorrect GTT->VRAM copy %d: Got 0x%p, "
					  "expected 0x%p (GTT/VRAM offset "
					  "0x%16llx/0x%16llx)\n",
					  i, *vram_start, gtt_start,
					  (unsigned long long)
					  (gtt_addr - adev->mc.gtt_start +
					   (void*)gtt_start - gtt_map),
					  (unsigned long long)
					  (vram_addr - adev->mc.vram_start +
					   (void*)gtt_start - gtt_map));
				amdgpu_bo_kunmap(vram_obj);
				goto out_lclean_unpin;
			}
			*vram_start = vram_start;
		}

		amdgpu_bo_kunmap(vram_obj);

		r = amdgpu_copy_buffer(ring, vram_addr, gtt_addr,
				       size, NULL, &fence, false);

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

		r = amdgpu_bo_kmap(gtt_obj[i], &gtt_map);
		if (r) {
			DRM_ERROR("Failed to map GTT object after copy %d\n", i);
			goto out_lclean_unpin;
		}

		for (gtt_start = gtt_map, gtt_end = gtt_map + size,
		     vram_start = vram_map, vram_end = vram_map + size;
		     gtt_start < gtt_end;
		     gtt_start++, vram_start++) {
			if (*gtt_start != vram_start) {
				DRM_ERROR("Incorrect VRAM->GTT copy %d: Got 0x%p, "
					  "expected 0x%p (VRAM/GTT offset "
					  "0x%16llx/0x%16llx)\n",
					  i, *gtt_start, vram_start,
					  (unsigned long long)
					  (vram_addr - adev->mc.vram_start +
					   (void*)vram_start - vram_map),
					  (unsigned long long)
					  (gtt_addr - adev->mc.gtt_start +
					   (void*)vram_start - vram_map));
				amdgpu_bo_kunmap(gtt_obj[i]);
				goto out_lclean_unpin;
			}
		}

		amdgpu_bo_kunmap(gtt_obj[i]);

		DRM_INFO("Tested GTT->VRAM and VRAM->GTT copy for GTT offset 0x%llx\n",
			 gtt_addr - adev->mc.gtt_start);
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
		printk(KERN_WARNING "Error while testing BO move.\n");
	}
}

void amdgpu_test_moves(struct amdgpu_device *adev)
{
	if (adev->mman.buffer_funcs)
		amdgpu_do_test_moves(adev);
}

void amdgpu_test_ring_sync(struct amdgpu_device *adev,
			   struct amdgpu_ring *ringA,
			   struct amdgpu_ring *ringB)
{
}

static void amdgpu_test_ring_sync2(struct amdgpu_device *adev,
			    struct amdgpu_ring *ringA,
			    struct amdgpu_ring *ringB,
			    struct amdgpu_ring *ringC)
{
}

static bool amdgpu_test_sync_possible(struct amdgpu_ring *ringA,
				      struct amdgpu_ring *ringB)
{
	if (ringA == &ringA->adev->vce.ring[0] &&
	    ringB == &ringB->adev->vce.ring[1])
		return false;

	return true;
}

void amdgpu_test_syncing(struct amdgpu_device *adev)
{
	int i, j, k;

	for (i = 1; i < AMDGPU_MAX_RINGS; ++i) {
		struct amdgpu_ring *ringA = adev->rings[i];
		if (!ringA || !ringA->ready)
			continue;

		for (j = 0; j < i; ++j) {
			struct amdgpu_ring *ringB = adev->rings[j];
			if (!ringB || !ringB->ready)
				continue;

			if (!amdgpu_test_sync_possible(ringA, ringB))
				continue;

			DRM_INFO("Testing syncing between rings %d and %d...\n", i, j);
			amdgpu_test_ring_sync(adev, ringA, ringB);

			DRM_INFO("Testing syncing between rings %d and %d...\n", j, i);
			amdgpu_test_ring_sync(adev, ringB, ringA);

			for (k = 0; k < j; ++k) {
				struct amdgpu_ring *ringC = adev->rings[k];
				if (!ringC || !ringC->ready)
					continue;

				if (!amdgpu_test_sync_possible(ringA, ringC))
					continue;

				if (!amdgpu_test_sync_possible(ringB, ringC))
					continue;

				DRM_INFO("Testing syncing between rings %d, %d and %d...\n", i, j, k);
				amdgpu_test_ring_sync2(adev, ringA, ringB, ringC);

				DRM_INFO("Testing syncing between rings %d, %d and %d...\n", i, k, j);
				amdgpu_test_ring_sync2(adev, ringA, ringC, ringB);

				DRM_INFO("Testing syncing between rings %d, %d and %d...\n", j, i, k);
				amdgpu_test_ring_sync2(adev, ringB, ringA, ringC);

				DRM_INFO("Testing syncing between rings %d, %d and %d...\n", j, k, i);
				amdgpu_test_ring_sync2(adev, ringB, ringC, ringA);

				DRM_INFO("Testing syncing between rings %d, %d and %d...\n", k, i, j);
				amdgpu_test_ring_sync2(adev, ringC, ringA, ringB);

				DRM_INFO("Testing syncing between rings %d, %d and %d...\n", k, j, i);
				amdgpu_test_ring_sync2(adev, ringC, ringB, ringA);
			}
		}
	}
}

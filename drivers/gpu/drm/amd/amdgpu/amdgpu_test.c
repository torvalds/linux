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
		struct fence *fence = NULL;

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
				       size, NULL, &fence);

		if (r) {
			DRM_ERROR("Failed GTT->VRAM copy %d\n", i);
			goto out_lclean_unpin;
		}

		r = fence_wait(fence, false);
		if (r) {
			DRM_ERROR("Failed to wait for GTT->VRAM fence %d\n", i);
			goto out_lclean_unpin;
		}

		fence_put(fence);

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
				       size, NULL, &fence);

		if (r) {
			DRM_ERROR("Failed VRAM->GTT copy %d\n", i);
			goto out_lclean_unpin;
		}

		r = fence_wait(fence, false);
		if (r) {
			DRM_ERROR("Failed to wait for VRAM->GTT fence %d\n", i);
			goto out_lclean_unpin;
		}

		fence_put(fence);

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
			fence_put(fence);
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

static int amdgpu_test_create_and_emit_fence(struct amdgpu_device *adev,
					     struct amdgpu_ring *ring,
					     struct fence **fence)
{
	uint32_t handle = ring->idx ^ 0xdeafbeef;
	int r;

	if (ring == &adev->uvd.ring) {
		r = amdgpu_uvd_get_create_msg(ring, handle, NULL);
		if (r) {
			DRM_ERROR("Failed to get dummy create msg\n");
			return r;
		}

		r = amdgpu_uvd_get_destroy_msg(ring, handle, fence);
		if (r) {
			DRM_ERROR("Failed to get dummy destroy msg\n");
			return r;
		}

	} else if (ring == &adev->vce.ring[0] ||
		   ring == &adev->vce.ring[1]) {
		r = amdgpu_vce_get_create_msg(ring, handle, NULL);
		if (r) {
			DRM_ERROR("Failed to get dummy create msg\n");
			return r;
		}

		r = amdgpu_vce_get_destroy_msg(ring, handle, fence);
		if (r) {
			DRM_ERROR("Failed to get dummy destroy msg\n");
			return r;
		}
	} else {
		struct amdgpu_fence *a_fence = NULL;
		r = amdgpu_ring_lock(ring, 64);
		if (r) {
			DRM_ERROR("Failed to lock ring A %d\n", ring->idx);
			return r;
		}
		amdgpu_fence_emit(ring, AMDGPU_FENCE_OWNER_UNDEFINED, &a_fence);
		amdgpu_ring_unlock_commit(ring);
		*fence = &a_fence->base;
	}
	return 0;
}

void amdgpu_test_ring_sync(struct amdgpu_device *adev,
			   struct amdgpu_ring *ringA,
			   struct amdgpu_ring *ringB)
{
	struct fence *fence1 = NULL, *fence2 = NULL;
	struct amdgpu_semaphore *semaphore = NULL;
	int r;

	r = amdgpu_semaphore_create(adev, &semaphore);
	if (r) {
		DRM_ERROR("Failed to create semaphore\n");
		goto out_cleanup;
	}

	r = amdgpu_ring_lock(ringA, 64);
	if (r) {
		DRM_ERROR("Failed to lock ring A %d\n", ringA->idx);
		goto out_cleanup;
	}
	amdgpu_semaphore_emit_wait(ringA, semaphore);
	amdgpu_ring_unlock_commit(ringA);

	r = amdgpu_test_create_and_emit_fence(adev, ringA, &fence1);
	if (r)
		goto out_cleanup;

	r = amdgpu_ring_lock(ringA, 64);
	if (r) {
		DRM_ERROR("Failed to lock ring A %d\n", ringA->idx);
		goto out_cleanup;
	}
	amdgpu_semaphore_emit_wait(ringA, semaphore);
	amdgpu_ring_unlock_commit(ringA);

	r = amdgpu_test_create_and_emit_fence(adev, ringA, &fence2);
	if (r)
		goto out_cleanup;

	mdelay(1000);

	if (fence_is_signaled(fence1)) {
		DRM_ERROR("Fence 1 signaled without waiting for semaphore.\n");
		goto out_cleanup;
	}

	r = amdgpu_ring_lock(ringB, 64);
	if (r) {
		DRM_ERROR("Failed to lock ring B %p\n", ringB);
		goto out_cleanup;
	}
	amdgpu_semaphore_emit_signal(ringB, semaphore);
	amdgpu_ring_unlock_commit(ringB);

	r = fence_wait(fence1, false);
	if (r) {
		DRM_ERROR("Failed to wait for sync fence 1\n");
		goto out_cleanup;
	}

	mdelay(1000);

	if (fence_is_signaled(fence2)) {
		DRM_ERROR("Fence 2 signaled without waiting for semaphore.\n");
		goto out_cleanup;
	}

	r = amdgpu_ring_lock(ringB, 64);
	if (r) {
		DRM_ERROR("Failed to lock ring B %p\n", ringB);
		goto out_cleanup;
	}
	amdgpu_semaphore_emit_signal(ringB, semaphore);
	amdgpu_ring_unlock_commit(ringB);

	r = fence_wait(fence2, false);
	if (r) {
		DRM_ERROR("Failed to wait for sync fence 1\n");
		goto out_cleanup;
	}

out_cleanup:
	amdgpu_semaphore_free(adev, &semaphore, NULL);

	if (fence1)
		fence_put(fence1);

	if (fence2)
		fence_put(fence2);

	if (r)
		printk(KERN_WARNING "Error while testing ring sync (%d).\n", r);
}

static void amdgpu_test_ring_sync2(struct amdgpu_device *adev,
			    struct amdgpu_ring *ringA,
			    struct amdgpu_ring *ringB,
			    struct amdgpu_ring *ringC)
{
	struct fence *fenceA = NULL, *fenceB = NULL;
	struct amdgpu_semaphore *semaphore = NULL;
	bool sigA, sigB;
	int i, r;

	r = amdgpu_semaphore_create(adev, &semaphore);
	if (r) {
		DRM_ERROR("Failed to create semaphore\n");
		goto out_cleanup;
	}

	r = amdgpu_ring_lock(ringA, 64);
	if (r) {
		DRM_ERROR("Failed to lock ring A %d\n", ringA->idx);
		goto out_cleanup;
	}
	amdgpu_semaphore_emit_wait(ringA, semaphore);
	amdgpu_ring_unlock_commit(ringA);

	r = amdgpu_test_create_and_emit_fence(adev, ringA, &fenceA);
	if (r)
		goto out_cleanup;

	r = amdgpu_ring_lock(ringB, 64);
	if (r) {
		DRM_ERROR("Failed to lock ring B %d\n", ringB->idx);
		goto out_cleanup;
	}
	amdgpu_semaphore_emit_wait(ringB, semaphore);
	amdgpu_ring_unlock_commit(ringB);
	r = amdgpu_test_create_and_emit_fence(adev, ringB, &fenceB);
	if (r)
		goto out_cleanup;

	mdelay(1000);

	if (fence_is_signaled(fenceA)) {
		DRM_ERROR("Fence A signaled without waiting for semaphore.\n");
		goto out_cleanup;
	}
	if (fence_is_signaled(fenceB)) {
		DRM_ERROR("Fence B signaled without waiting for semaphore.\n");
		goto out_cleanup;
	}

	r = amdgpu_ring_lock(ringC, 64);
	if (r) {
		DRM_ERROR("Failed to lock ring B %p\n", ringC);
		goto out_cleanup;
	}
	amdgpu_semaphore_emit_signal(ringC, semaphore);
	amdgpu_ring_unlock_commit(ringC);

	for (i = 0; i < 30; ++i) {
		mdelay(100);
		sigA = fence_is_signaled(fenceA);
		sigB = fence_is_signaled(fenceB);
		if (sigA || sigB)
			break;
	}

	if (!sigA && !sigB) {
		DRM_ERROR("Neither fence A nor B has been signaled\n");
		goto out_cleanup;
	} else if (sigA && sigB) {
		DRM_ERROR("Both fence A and B has been signaled\n");
		goto out_cleanup;
	}

	DRM_INFO("Fence %c was first signaled\n", sigA ? 'A' : 'B');

	r = amdgpu_ring_lock(ringC, 64);
	if (r) {
		DRM_ERROR("Failed to lock ring B %p\n", ringC);
		goto out_cleanup;
	}
	amdgpu_semaphore_emit_signal(ringC, semaphore);
	amdgpu_ring_unlock_commit(ringC);

	mdelay(1000);

	r = fence_wait(fenceA, false);
	if (r) {
		DRM_ERROR("Failed to wait for sync fence A\n");
		goto out_cleanup;
	}
	r = fence_wait(fenceB, false);
	if (r) {
		DRM_ERROR("Failed to wait for sync fence B\n");
		goto out_cleanup;
	}

out_cleanup:
	amdgpu_semaphore_free(adev, &semaphore, NULL);

	if (fenceA)
		fence_put(fenceA);

	if (fenceB)
		fence_put(fenceB);

	if (r)
		printk(KERN_WARNING "Error while testing ring sync (%d).\n", r);
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

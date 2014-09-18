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
#include <drm/radeon_drm.h>
#include "radeon_reg.h"
#include "radeon.h"

#define RADEON_TEST_COPY_BLIT 1
#define RADEON_TEST_COPY_DMA  0


/* Test BO GTT->VRAM and VRAM->GTT GPU copies across the whole GTT aperture */
static void radeon_do_test_moves(struct radeon_device *rdev, int flag)
{
	struct radeon_bo *vram_obj = NULL;
	struct radeon_bo **gtt_obj = NULL;
	uint64_t gtt_addr, vram_addr;
	unsigned n, size;
	int i, r, ring;

	switch (flag) {
	case RADEON_TEST_COPY_DMA:
		ring = radeon_copy_dma_ring_index(rdev);
		break;
	case RADEON_TEST_COPY_BLIT:
		ring = radeon_copy_blit_ring_index(rdev);
		break;
	default:
		DRM_ERROR("Unknown copy method\n");
		return;
	}

	size = 1024 * 1024;

	/* Number of tests =
	 * (Total GTT - IB pool - writeback page - ring buffers) / test size
	 */
	n = rdev->mc.gtt_size - rdev->gart_pin_size;
	n /= size;

	gtt_obj = kzalloc(n * sizeof(*gtt_obj), GFP_KERNEL);
	if (!gtt_obj) {
		DRM_ERROR("Failed to allocate %d pointers\n", n);
		r = 1;
		goto out_cleanup;
	}

	r = radeon_bo_create(rdev, size, PAGE_SIZE, true, RADEON_GEM_DOMAIN_VRAM,
			     0, NULL, &vram_obj);
	if (r) {
		DRM_ERROR("Failed to create VRAM object\n");
		goto out_cleanup;
	}
	r = radeon_bo_reserve(vram_obj, false);
	if (unlikely(r != 0))
		goto out_unref;
	r = radeon_bo_pin(vram_obj, RADEON_GEM_DOMAIN_VRAM, &vram_addr);
	if (r) {
		DRM_ERROR("Failed to pin VRAM object\n");
		goto out_unres;
	}
	for (i = 0; i < n; i++) {
		void *gtt_map, *vram_map;
		void **gtt_start, **gtt_end;
		void **vram_start, **vram_end;
		struct radeon_fence *fence = NULL;

		r = radeon_bo_create(rdev, size, PAGE_SIZE, true,
				     RADEON_GEM_DOMAIN_GTT, 0, NULL, gtt_obj + i);
		if (r) {
			DRM_ERROR("Failed to create GTT object %d\n", i);
			goto out_lclean;
		}

		r = radeon_bo_reserve(gtt_obj[i], false);
		if (unlikely(r != 0))
			goto out_lclean_unref;
		r = radeon_bo_pin(gtt_obj[i], RADEON_GEM_DOMAIN_GTT, &gtt_addr);
		if (r) {
			DRM_ERROR("Failed to pin GTT object %d\n", i);
			goto out_lclean_unres;
		}

		r = radeon_bo_kmap(gtt_obj[i], &gtt_map);
		if (r) {
			DRM_ERROR("Failed to map GTT object %d\n", i);
			goto out_lclean_unpin;
		}

		for (gtt_start = gtt_map, gtt_end = gtt_map + size;
		     gtt_start < gtt_end;
		     gtt_start++)
			*gtt_start = gtt_start;

		radeon_bo_kunmap(gtt_obj[i]);

		if (ring == R600_RING_TYPE_DMA_INDEX)
			r = radeon_copy_dma(rdev, gtt_addr, vram_addr, size / RADEON_GPU_PAGE_SIZE, &fence);
		else
			r = radeon_copy_blit(rdev, gtt_addr, vram_addr, size / RADEON_GPU_PAGE_SIZE, &fence);
		if (r) {
			DRM_ERROR("Failed GTT->VRAM copy %d\n", i);
			goto out_lclean_unpin;
		}

		r = radeon_fence_wait(fence, false);
		if (r) {
			DRM_ERROR("Failed to wait for GTT->VRAM fence %d\n", i);
			goto out_lclean_unpin;
		}

		radeon_fence_unref(&fence);

		r = radeon_bo_kmap(vram_obj, &vram_map);
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
					  (gtt_addr - rdev->mc.gtt_start +
					   (void*)gtt_start - gtt_map),
					  (unsigned long long)
					  (vram_addr - rdev->mc.vram_start +
					   (void*)gtt_start - gtt_map));
				radeon_bo_kunmap(vram_obj);
				goto out_lclean_unpin;
			}
			*vram_start = vram_start;
		}

		radeon_bo_kunmap(vram_obj);

		if (ring == R600_RING_TYPE_DMA_INDEX)
			r = radeon_copy_dma(rdev, vram_addr, gtt_addr, size / RADEON_GPU_PAGE_SIZE, &fence);
		else
			r = radeon_copy_blit(rdev, vram_addr, gtt_addr, size / RADEON_GPU_PAGE_SIZE, &fence);
		if (r) {
			DRM_ERROR("Failed VRAM->GTT copy %d\n", i);
			goto out_lclean_unpin;
		}

		r = radeon_fence_wait(fence, false);
		if (r) {
			DRM_ERROR("Failed to wait for VRAM->GTT fence %d\n", i);
			goto out_lclean_unpin;
		}

		radeon_fence_unref(&fence);

		r = radeon_bo_kmap(gtt_obj[i], &gtt_map);
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
					  (vram_addr - rdev->mc.vram_start +
					   (void*)vram_start - vram_map),
					  (unsigned long long)
					  (gtt_addr - rdev->mc.gtt_start +
					   (void*)vram_start - vram_map));
				radeon_bo_kunmap(gtt_obj[i]);
				goto out_lclean_unpin;
			}
		}

		radeon_bo_kunmap(gtt_obj[i]);

		DRM_INFO("Tested GTT->VRAM and VRAM->GTT copy for GTT offset 0x%llx\n",
			 gtt_addr - rdev->mc.gtt_start);
		continue;

out_lclean_unpin:
		radeon_bo_unpin(gtt_obj[i]);
out_lclean_unres:
		radeon_bo_unreserve(gtt_obj[i]);
out_lclean_unref:
		radeon_bo_unref(&gtt_obj[i]);
out_lclean:
		for (--i; i >= 0; --i) {
			radeon_bo_unpin(gtt_obj[i]);
			radeon_bo_unreserve(gtt_obj[i]);
			radeon_bo_unref(&gtt_obj[i]);
		}
		if (fence)
			radeon_fence_unref(&fence);
		break;
	}

	radeon_bo_unpin(vram_obj);
out_unres:
	radeon_bo_unreserve(vram_obj);
out_unref:
	radeon_bo_unref(&vram_obj);
out_cleanup:
	kfree(gtt_obj);
	if (r) {
		printk(KERN_WARNING "Error while testing BO move.\n");
	}
}

void radeon_test_moves(struct radeon_device *rdev)
{
	if (rdev->asic->copy.dma)
		radeon_do_test_moves(rdev, RADEON_TEST_COPY_DMA);
	if (rdev->asic->copy.blit)
		radeon_do_test_moves(rdev, RADEON_TEST_COPY_BLIT);
}

static int radeon_test_create_and_emit_fence(struct radeon_device *rdev,
					     struct radeon_ring *ring,
					     struct radeon_fence **fence)
{
	uint32_t handle = ring->idx ^ 0xdeafbeef;
	int r;

	if (ring->idx == R600_RING_TYPE_UVD_INDEX) {
		r = radeon_uvd_get_create_msg(rdev, ring->idx, handle, NULL);
		if (r) {
			DRM_ERROR("Failed to get dummy create msg\n");
			return r;
		}

		r = radeon_uvd_get_destroy_msg(rdev, ring->idx, handle, fence);
		if (r) {
			DRM_ERROR("Failed to get dummy destroy msg\n");
			return r;
		}

	} else if (ring->idx == TN_RING_TYPE_VCE1_INDEX ||
		   ring->idx == TN_RING_TYPE_VCE2_INDEX) {
		r = radeon_vce_get_create_msg(rdev, ring->idx, handle, NULL);
		if (r) {
			DRM_ERROR("Failed to get dummy create msg\n");
			return r;
		}

		r = radeon_vce_get_destroy_msg(rdev, ring->idx, handle, fence);
		if (r) {
			DRM_ERROR("Failed to get dummy destroy msg\n");
			return r;
		}

	} else {
		r = radeon_ring_lock(rdev, ring, 64);
		if (r) {
			DRM_ERROR("Failed to lock ring A %d\n", ring->idx);
			return r;
		}
		radeon_fence_emit(rdev, fence, ring->idx);
		radeon_ring_unlock_commit(rdev, ring, false);
	}
	return 0;
}

void radeon_test_ring_sync(struct radeon_device *rdev,
			   struct radeon_ring *ringA,
			   struct radeon_ring *ringB)
{
	struct radeon_fence *fence1 = NULL, *fence2 = NULL;
	struct radeon_semaphore *semaphore = NULL;
	int r;

	r = radeon_semaphore_create(rdev, &semaphore);
	if (r) {
		DRM_ERROR("Failed to create semaphore\n");
		goto out_cleanup;
	}

	r = radeon_ring_lock(rdev, ringA, 64);
	if (r) {
		DRM_ERROR("Failed to lock ring A %d\n", ringA->idx);
		goto out_cleanup;
	}
	radeon_semaphore_emit_wait(rdev, ringA->idx, semaphore);
	radeon_ring_unlock_commit(rdev, ringA, false);

	r = radeon_test_create_and_emit_fence(rdev, ringA, &fence1);
	if (r)
		goto out_cleanup;

	r = radeon_ring_lock(rdev, ringA, 64);
	if (r) {
		DRM_ERROR("Failed to lock ring A %d\n", ringA->idx);
		goto out_cleanup;
	}
	radeon_semaphore_emit_wait(rdev, ringA->idx, semaphore);
	radeon_ring_unlock_commit(rdev, ringA, false);

	r = radeon_test_create_and_emit_fence(rdev, ringA, &fence2);
	if (r)
		goto out_cleanup;

	mdelay(1000);

	if (radeon_fence_signaled(fence1)) {
		DRM_ERROR("Fence 1 signaled without waiting for semaphore.\n");
		goto out_cleanup;
	}

	r = radeon_ring_lock(rdev, ringB, 64);
	if (r) {
		DRM_ERROR("Failed to lock ring B %p\n", ringB);
		goto out_cleanup;
	}
	radeon_semaphore_emit_signal(rdev, ringB->idx, semaphore);
	radeon_ring_unlock_commit(rdev, ringB, false);

	r = radeon_fence_wait(fence1, false);
	if (r) {
		DRM_ERROR("Failed to wait for sync fence 1\n");
		goto out_cleanup;
	}

	mdelay(1000);

	if (radeon_fence_signaled(fence2)) {
		DRM_ERROR("Fence 2 signaled without waiting for semaphore.\n");
		goto out_cleanup;
	}

	r = radeon_ring_lock(rdev, ringB, 64);
	if (r) {
		DRM_ERROR("Failed to lock ring B %p\n", ringB);
		goto out_cleanup;
	}
	radeon_semaphore_emit_signal(rdev, ringB->idx, semaphore);
	radeon_ring_unlock_commit(rdev, ringB, false);

	r = radeon_fence_wait(fence2, false);
	if (r) {
		DRM_ERROR("Failed to wait for sync fence 1\n");
		goto out_cleanup;
	}

out_cleanup:
	radeon_semaphore_free(rdev, &semaphore, NULL);

	if (fence1)
		radeon_fence_unref(&fence1);

	if (fence2)
		radeon_fence_unref(&fence2);

	if (r)
		printk(KERN_WARNING "Error while testing ring sync (%d).\n", r);
}

static void radeon_test_ring_sync2(struct radeon_device *rdev,
			    struct radeon_ring *ringA,
			    struct radeon_ring *ringB,
			    struct radeon_ring *ringC)
{
	struct radeon_fence *fenceA = NULL, *fenceB = NULL;
	struct radeon_semaphore *semaphore = NULL;
	bool sigA, sigB;
	int i, r;

	r = radeon_semaphore_create(rdev, &semaphore);
	if (r) {
		DRM_ERROR("Failed to create semaphore\n");
		goto out_cleanup;
	}

	r = radeon_ring_lock(rdev, ringA, 64);
	if (r) {
		DRM_ERROR("Failed to lock ring A %d\n", ringA->idx);
		goto out_cleanup;
	}
	radeon_semaphore_emit_wait(rdev, ringA->idx, semaphore);
	radeon_ring_unlock_commit(rdev, ringA, false);

	r = radeon_test_create_and_emit_fence(rdev, ringA, &fenceA);
	if (r)
		goto out_cleanup;

	r = radeon_ring_lock(rdev, ringB, 64);
	if (r) {
		DRM_ERROR("Failed to lock ring B %d\n", ringB->idx);
		goto out_cleanup;
	}
	radeon_semaphore_emit_wait(rdev, ringB->idx, semaphore);
	radeon_ring_unlock_commit(rdev, ringB, false);
	r = radeon_test_create_and_emit_fence(rdev, ringB, &fenceB);
	if (r)
		goto out_cleanup;

	mdelay(1000);

	if (radeon_fence_signaled(fenceA)) {
		DRM_ERROR("Fence A signaled without waiting for semaphore.\n");
		goto out_cleanup;
	}
	if (radeon_fence_signaled(fenceB)) {
		DRM_ERROR("Fence B signaled without waiting for semaphore.\n");
		goto out_cleanup;
	}

	r = radeon_ring_lock(rdev, ringC, 64);
	if (r) {
		DRM_ERROR("Failed to lock ring B %p\n", ringC);
		goto out_cleanup;
	}
	radeon_semaphore_emit_signal(rdev, ringC->idx, semaphore);
	radeon_ring_unlock_commit(rdev, ringC, false);

	for (i = 0; i < 30; ++i) {
		mdelay(100);
		sigA = radeon_fence_signaled(fenceA);
		sigB = radeon_fence_signaled(fenceB);
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

	r = radeon_ring_lock(rdev, ringC, 64);
	if (r) {
		DRM_ERROR("Failed to lock ring B %p\n", ringC);
		goto out_cleanup;
	}
	radeon_semaphore_emit_signal(rdev, ringC->idx, semaphore);
	radeon_ring_unlock_commit(rdev, ringC, false);

	mdelay(1000);

	r = radeon_fence_wait(fenceA, false);
	if (r) {
		DRM_ERROR("Failed to wait for sync fence A\n");
		goto out_cleanup;
	}
	r = radeon_fence_wait(fenceB, false);
	if (r) {
		DRM_ERROR("Failed to wait for sync fence B\n");
		goto out_cleanup;
	}

out_cleanup:
	radeon_semaphore_free(rdev, &semaphore, NULL);

	if (fenceA)
		radeon_fence_unref(&fenceA);

	if (fenceB)
		radeon_fence_unref(&fenceB);

	if (r)
		printk(KERN_WARNING "Error while testing ring sync (%d).\n", r);
}

static bool radeon_test_sync_possible(struct radeon_ring *ringA,
				      struct radeon_ring *ringB)
{
	if (ringA->idx == TN_RING_TYPE_VCE2_INDEX &&
	    ringB->idx == TN_RING_TYPE_VCE1_INDEX)
		return false;

	return true;
}

void radeon_test_syncing(struct radeon_device *rdev)
{
	int i, j, k;

	for (i = 1; i < RADEON_NUM_RINGS; ++i) {
		struct radeon_ring *ringA = &rdev->ring[i];
		if (!ringA->ready)
			continue;

		for (j = 0; j < i; ++j) {
			struct radeon_ring *ringB = &rdev->ring[j];
			if (!ringB->ready)
				continue;

			if (!radeon_test_sync_possible(ringA, ringB))
				continue;

			DRM_INFO("Testing syncing between rings %d and %d...\n", i, j);
			radeon_test_ring_sync(rdev, ringA, ringB);

			DRM_INFO("Testing syncing between rings %d and %d...\n", j, i);
			radeon_test_ring_sync(rdev, ringB, ringA);

			for (k = 0; k < j; ++k) {
				struct radeon_ring *ringC = &rdev->ring[k];
				if (!ringC->ready)
					continue;

				if (!radeon_test_sync_possible(ringA, ringC))
					continue;

				if (!radeon_test_sync_possible(ringB, ringC))
					continue;

				DRM_INFO("Testing syncing between rings %d, %d and %d...\n", i, j, k);
				radeon_test_ring_sync2(rdev, ringA, ringB, ringC);

				DRM_INFO("Testing syncing between rings %d, %d and %d...\n", i, k, j);
				radeon_test_ring_sync2(rdev, ringA, ringC, ringB);

				DRM_INFO("Testing syncing between rings %d, %d and %d...\n", j, i, k);
				radeon_test_ring_sync2(rdev, ringB, ringA, ringC);

				DRM_INFO("Testing syncing between rings %d, %d and %d...\n", j, k, i);
				radeon_test_ring_sync2(rdev, ringB, ringC, ringA);

				DRM_INFO("Testing syncing between rings %d, %d and %d...\n", k, i, j);
				radeon_test_ring_sync2(rdev, ringC, ringA, ringB);

				DRM_INFO("Testing syncing between rings %d, %d and %d...\n", k, j, i);
				radeon_test_ring_sync2(rdev, ringC, ringB, ringA);
			}
		}
	}
}

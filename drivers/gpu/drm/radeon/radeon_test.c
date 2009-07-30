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


/* Test BO GTT->VRAM and VRAM->GTT GPU copies across the whole GTT aperture */
void radeon_test_moves(struct radeon_device *rdev)
{
	struct radeon_object *vram_obj = NULL;
	struct radeon_object **gtt_obj = NULL;
	struct radeon_fence *fence = NULL;
	uint64_t gtt_addr, vram_addr;
	unsigned i, n, size;
	int r;

	size = 1024 * 1024;

	/* Number of tests =
	 * (Total GTT - IB pool - writeback page - ring buffer) / test size
	 */
	n = (rdev->mc.gtt_size - RADEON_IB_POOL_SIZE*64*1024 - 4096 -
	     rdev->cp.ring_size) / size;

	gtt_obj = kzalloc(n * sizeof(*gtt_obj), GFP_KERNEL);
	if (!gtt_obj) {
		DRM_ERROR("Failed to allocate %d pointers\n", n);
		r = 1;
		goto out_cleanup;
	}

	r = radeon_object_create(rdev, NULL, size, true, RADEON_GEM_DOMAIN_VRAM,
				 false, &vram_obj);
	if (r) {
		DRM_ERROR("Failed to create VRAM object\n");
		goto out_cleanup;
	}

	r = radeon_object_pin(vram_obj, RADEON_GEM_DOMAIN_VRAM, &vram_addr);
	if (r) {
		DRM_ERROR("Failed to pin VRAM object\n");
		goto out_cleanup;
	}

	for (i = 0; i < n; i++) {
		void *gtt_map, *vram_map;
		void **gtt_start, **gtt_end;
		void **vram_start, **vram_end;

		r = radeon_object_create(rdev, NULL, size, true,
					 RADEON_GEM_DOMAIN_GTT, false, gtt_obj + i);
		if (r) {
			DRM_ERROR("Failed to create GTT object %d\n", i);
			goto out_cleanup;
		}

		r = radeon_object_pin(gtt_obj[i], RADEON_GEM_DOMAIN_GTT, &gtt_addr);
		if (r) {
			DRM_ERROR("Failed to pin GTT object %d\n", i);
			goto out_cleanup;
		}

		r = radeon_object_kmap(gtt_obj[i], &gtt_map);
		if (r) {
			DRM_ERROR("Failed to map GTT object %d\n", i);
			goto out_cleanup;
		}

		for (gtt_start = gtt_map, gtt_end = gtt_map + size;
		     gtt_start < gtt_end;
		     gtt_start++)
			*gtt_start = gtt_start;

		radeon_object_kunmap(gtt_obj[i]);

		r = radeon_fence_create(rdev, &fence);
		if (r) {
			DRM_ERROR("Failed to create GTT->VRAM fence %d\n", i);
			goto out_cleanup;
		}

		r = radeon_copy(rdev, gtt_addr, vram_addr, size / 4096, fence);
		if (r) {
			DRM_ERROR("Failed GTT->VRAM copy %d\n", i);
			goto out_cleanup;
		}

		r = radeon_fence_wait(fence, false);
		if (r) {
			DRM_ERROR("Failed to wait for GTT->VRAM fence %d\n", i);
			goto out_cleanup;
		}

		radeon_fence_unref(&fence);

		r = radeon_object_kmap(vram_obj, &vram_map);
		if (r) {
			DRM_ERROR("Failed to map VRAM object after copy %d\n", i);
			goto out_cleanup;
		}

		for (gtt_start = gtt_map, gtt_end = gtt_map + size,
		     vram_start = vram_map, vram_end = vram_map + size;
		     vram_start < vram_end;
		     gtt_start++, vram_start++) {
			if (*vram_start != gtt_start) {
				DRM_ERROR("Incorrect GTT->VRAM copy %d: Got 0x%p, "
					  "expected 0x%p (GTT map 0x%p-0x%p)\n",
					  i, *vram_start, gtt_start, gtt_map,
					  gtt_end);
				radeon_object_kunmap(vram_obj);
				goto out_cleanup;
			}
			*vram_start = vram_start;
		}

		radeon_object_kunmap(vram_obj);

		r = radeon_fence_create(rdev, &fence);
		if (r) {
			DRM_ERROR("Failed to create VRAM->GTT fence %d\n", i);
			goto out_cleanup;
		}

		r = radeon_copy(rdev, vram_addr, gtt_addr, size / 4096, fence);
		if (r) {
			DRM_ERROR("Failed VRAM->GTT copy %d\n", i);
			goto out_cleanup;
		}

		r = radeon_fence_wait(fence, false);
		if (r) {
			DRM_ERROR("Failed to wait for VRAM->GTT fence %d\n", i);
			goto out_cleanup;
		}

		radeon_fence_unref(&fence);

		r = radeon_object_kmap(gtt_obj[i], &gtt_map);
		if (r) {
			DRM_ERROR("Failed to map GTT object after copy %d\n", i);
			goto out_cleanup;
		}

		for (gtt_start = gtt_map, gtt_end = gtt_map + size,
		     vram_start = vram_map, vram_end = vram_map + size;
		     gtt_start < gtt_end;
		     gtt_start++, vram_start++) {
			if (*gtt_start != vram_start) {
				DRM_ERROR("Incorrect VRAM->GTT copy %d: Got 0x%p, "
					  "expected 0x%p (VRAM map 0x%p-0x%p)\n",
					  i, *gtt_start, vram_start, vram_map,
					  vram_end);
				radeon_object_kunmap(gtt_obj[i]);
				goto out_cleanup;
			}
		}

		radeon_object_kunmap(gtt_obj[i]);

		DRM_INFO("Tested GTT->VRAM and VRAM->GTT copy for GTT offset 0x%llx\n",
			 gtt_addr - rdev->mc.gtt_location);
	}

out_cleanup:
	if (vram_obj) {
		radeon_object_unpin(vram_obj);
		radeon_object_unref(&vram_obj);
	}
	if (gtt_obj) {
		for (i = 0; i < n; i++) {
			if (gtt_obj[i]) {
				radeon_object_unpin(gtt_obj[i]);
				radeon_object_unref(&gtt_obj[i]);
			}
		}
		kfree(gtt_obj);
	}
	if (fence) {
		radeon_fence_unref(&fence);
	}
	if (r) {
		printk(KERN_WARNING "Error while testing BO move.\n");
	}
}


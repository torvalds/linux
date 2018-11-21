/*
 * Copyright 2009 Jerome Glisse.
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
 * Authors: Jerome Glisse
 */
#include <drm/drmP.h>
#include <drm/amdgpu_drm.h>
#include "amdgpu.h"

#define AMDGPU_BENCHMARK_ITERATIONS 1024
#define AMDGPU_BENCHMARK_COMMON_MODES_N 17

static int amdgpu_benchmark_do_move(struct amdgpu_device *adev, unsigned size,
				    uint64_t saddr, uint64_t daddr, int n)
{
	unsigned long start_jiffies;
	unsigned long end_jiffies;
	struct dma_fence *fence = NULL;
	int i, r;

	start_jiffies = jiffies;
	for (i = 0; i < n; i++) {
		struct amdgpu_ring *ring = adev->mman.buffer_funcs_ring;
		r = amdgpu_copy_buffer(ring, saddr, daddr, size, NULL, &fence,
				       false, false);
		if (r)
			goto exit_do_move;
		r = dma_fence_wait(fence, false);
		if (r)
			goto exit_do_move;
		dma_fence_put(fence);
	}
	end_jiffies = jiffies;
	r = jiffies_to_msecs(end_jiffies - start_jiffies);

exit_do_move:
	if (fence)
		dma_fence_put(fence);
	return r;
}


static void amdgpu_benchmark_log_results(int n, unsigned size,
					 unsigned int time,
					 unsigned sdomain, unsigned ddomain,
					 char *kind)
{
	unsigned int throughput = (n * (size >> 10)) / time;
	DRM_INFO("amdgpu: %s %u bo moves of %u kB from"
		 " %d to %d in %u ms, throughput: %u Mb/s or %u MB/s\n",
		 kind, n, size >> 10, sdomain, ddomain, time,
		 throughput * 8, throughput);
}

static void amdgpu_benchmark_move(struct amdgpu_device *adev, unsigned size,
				  unsigned sdomain, unsigned ddomain)
{
	struct amdgpu_bo *dobj = NULL;
	struct amdgpu_bo *sobj = NULL;
	struct amdgpu_bo_param bp;
	uint64_t saddr, daddr;
	int r, n;
	int time;

	memset(&bp, 0, sizeof(bp));
	bp.size = size;
	bp.byte_align = PAGE_SIZE;
	bp.domain = sdomain;
	bp.flags = 0;
	bp.type = ttm_bo_type_kernel;
	bp.resv = NULL;
	n = AMDGPU_BENCHMARK_ITERATIONS;
	r = amdgpu_bo_create(adev, &bp, &sobj);
	if (r) {
		goto out_cleanup;
	}
	r = amdgpu_bo_reserve(sobj, false);
	if (unlikely(r != 0))
		goto out_cleanup;
	r = amdgpu_bo_pin(sobj, sdomain, &saddr);
	amdgpu_bo_unreserve(sobj);
	if (r) {
		goto out_cleanup;
	}
	bp.domain = ddomain;
	r = amdgpu_bo_create(adev, &bp, &dobj);
	if (r) {
		goto out_cleanup;
	}
	r = amdgpu_bo_reserve(dobj, false);
	if (unlikely(r != 0))
		goto out_cleanup;
	r = amdgpu_bo_pin(dobj, ddomain, &daddr);
	amdgpu_bo_unreserve(dobj);
	if (r) {
		goto out_cleanup;
	}

	if (adev->mman.buffer_funcs) {
		time = amdgpu_benchmark_do_move(adev, size, saddr, daddr, n);
		if (time < 0)
			goto out_cleanup;
		if (time > 0)
			amdgpu_benchmark_log_results(n, size, time,
						     sdomain, ddomain, "dma");
	}

out_cleanup:
	/* Check error value now. The value can be overwritten when clean up.*/
	if (r) {
		DRM_ERROR("Error while benchmarking BO move.\n");
	}

	if (sobj) {
		r = amdgpu_bo_reserve(sobj, true);
		if (likely(r == 0)) {
			amdgpu_bo_unpin(sobj);
			amdgpu_bo_unreserve(sobj);
		}
		amdgpu_bo_unref(&sobj);
	}
	if (dobj) {
		r = amdgpu_bo_reserve(dobj, true);
		if (likely(r == 0)) {
			amdgpu_bo_unpin(dobj);
			amdgpu_bo_unreserve(dobj);
		}
		amdgpu_bo_unref(&dobj);
	}
}

void amdgpu_benchmark(struct amdgpu_device *adev, int test_number)
{
	int i;
	static const int common_modes[AMDGPU_BENCHMARK_COMMON_MODES_N] = {
		640 * 480 * 4,
		720 * 480 * 4,
		800 * 600 * 4,
		848 * 480 * 4,
		1024 * 768 * 4,
		1152 * 768 * 4,
		1280 * 720 * 4,
		1280 * 800 * 4,
		1280 * 854 * 4,
		1280 * 960 * 4,
		1280 * 1024 * 4,
		1440 * 900 * 4,
		1400 * 1050 * 4,
		1680 * 1050 * 4,
		1600 * 1200 * 4,
		1920 * 1080 * 4,
		1920 * 1200 * 4
	};

	switch (test_number) {
	case 1:
		/* simple test, VRAM to GTT and GTT to VRAM */
		amdgpu_benchmark_move(adev, 1024*1024, AMDGPU_GEM_DOMAIN_GTT,
				      AMDGPU_GEM_DOMAIN_VRAM);
		amdgpu_benchmark_move(adev, 1024*1024, AMDGPU_GEM_DOMAIN_VRAM,
				      AMDGPU_GEM_DOMAIN_GTT);
		break;
	case 2:
		/* simple test, VRAM to VRAM */
		amdgpu_benchmark_move(adev, 1024*1024, AMDGPU_GEM_DOMAIN_VRAM,
				      AMDGPU_GEM_DOMAIN_VRAM);
		break;
	case 3:
		/* GTT to VRAM, buffer size sweep, powers of 2 */
		for (i = 1; i <= 16384; i <<= 1)
			amdgpu_benchmark_move(adev, i * AMDGPU_GPU_PAGE_SIZE,
					      AMDGPU_GEM_DOMAIN_GTT,
					      AMDGPU_GEM_DOMAIN_VRAM);
		break;
	case 4:
		/* VRAM to GTT, buffer size sweep, powers of 2 */
		for (i = 1; i <= 16384; i <<= 1)
			amdgpu_benchmark_move(adev, i * AMDGPU_GPU_PAGE_SIZE,
					      AMDGPU_GEM_DOMAIN_VRAM,
					      AMDGPU_GEM_DOMAIN_GTT);
		break;
	case 5:
		/* VRAM to VRAM, buffer size sweep, powers of 2 */
		for (i = 1; i <= 16384; i <<= 1)
			amdgpu_benchmark_move(adev, i * AMDGPU_GPU_PAGE_SIZE,
					      AMDGPU_GEM_DOMAIN_VRAM,
					      AMDGPU_GEM_DOMAIN_VRAM);
		break;
	case 6:
		/* GTT to VRAM, buffer size sweep, common modes */
		for (i = 0; i < AMDGPU_BENCHMARK_COMMON_MODES_N; i++)
			amdgpu_benchmark_move(adev, common_modes[i],
					      AMDGPU_GEM_DOMAIN_GTT,
					      AMDGPU_GEM_DOMAIN_VRAM);
		break;
	case 7:
		/* VRAM to GTT, buffer size sweep, common modes */
		for (i = 0; i < AMDGPU_BENCHMARK_COMMON_MODES_N; i++)
			amdgpu_benchmark_move(adev, common_modes[i],
					      AMDGPU_GEM_DOMAIN_VRAM,
					      AMDGPU_GEM_DOMAIN_GTT);
		break;
	case 8:
		/* VRAM to VRAM, buffer size sweep, common modes */
		for (i = 0; i < AMDGPU_BENCHMARK_COMMON_MODES_N; i++)
			amdgpu_benchmark_move(adev, common_modes[i],
					      AMDGPU_GEM_DOMAIN_VRAM,
					      AMDGPU_GEM_DOMAIN_VRAM);
		break;

	default:
		DRM_ERROR("Unknown benchmark\n");
	}
}

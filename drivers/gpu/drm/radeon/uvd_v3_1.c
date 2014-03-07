/*
 * Copyright 2013 Advanced Micro Devices, Inc.
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
 * Authors: Christian König <christian.koenig@amd.com>
 */

#include <drm/drmP.h>
#include "radeon.h"
#include "radeon_asic.h"
#include "nid.h"

/**
 * uvd_v3_1_semaphore_emit - emit semaphore command
 *
 * @rdev: radeon_device pointer
 * @ring: radeon_ring pointer
 * @semaphore: semaphore to emit commands for
 * @emit_wait: true if we should emit a wait command
 *
 * Emit a semaphore command (either wait or signal) to the UVD ring.
 */
bool uvd_v3_1_semaphore_emit(struct radeon_device *rdev,
			     struct radeon_ring *ring,
			     struct radeon_semaphore *semaphore,
			     bool emit_wait)
{
	uint64_t addr = semaphore->gpu_addr;

	radeon_ring_write(ring, PACKET0(UVD_SEMA_ADDR_LOW, 0));
	radeon_ring_write(ring, (addr >> 3) & 0x000FFFFF);

	radeon_ring_write(ring, PACKET0(UVD_SEMA_ADDR_HIGH, 0));
	radeon_ring_write(ring, (addr >> 23) & 0x000FFFFF);

	radeon_ring_write(ring, PACKET0(UVD_SEMA_CMD, 0));
	radeon_ring_write(ring, 0x80 | (emit_wait ? 1 : 0));

	return true;
}

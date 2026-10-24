/*
 * Copyright 2018 Advanced Micro Devices, Inc.
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
 */

#ifndef __AMDGPU_SDMA_H__
#define __AMDGPU_SDMA_H__

#include "amdgpu_sdma_types.h"
#include "amdgpu.h"
#include "amdgpu_ring.h"

#include <linux/types.h>

struct amdgpu_device;
struct amdgpu_iv_entry;
struct amdgpu_irq_src;
struct amdgpu_ring;
struct ras_common_if;

#define NUM_SDMA(x) hweight32(x)

int __printf(4, 5)
amdgpu_sdma_ring_init(struct amdgpu_device *adev, struct amdgpu_ring *ring,
		      unsigned int instance, const char *fmt, ...);

int amdgpu_sdma_reset_engine(struct amdgpu_device *adev, uint32_t instance_id);

int amdgpu_sdma_reset_queue_legacy(struct amdgpu_ring *ring,
				   unsigned int vmid,
				   struct amdgpu_fence *timedout_fence);

#define amdgpu_emit_copy_buffer(adev, ib, s, d, b, t) (adev)->mman.buffer_funcs->emit_copy_buffer((ib),  (s), (d), (b), (t))
#define amdgpu_emit_fill_buffer(adev, ib, s, d, b) (adev)->mman.buffer_funcs->emit_fill_buffer((ib), (s), (d), (b))

static inline struct amdgpu_sdma_instance *
amdgpu_sdma_get_instance_from_ring(struct amdgpu_ring *ring)
{
	return &ring->adev->sdma.instance[ring->me];
}

static inline uint64_t
amdgpu_sdma_get_csa_mc_addr(struct amdgpu_ring *ring, unsigned vmid)
{
	if (vmid == 0)
		return 0;
	else
		return amdgpu_sdma_get_instance_from_ring(ring)->csa_addr;
}

int amdgpu_sdma_ras_late_init(struct amdgpu_device *adev,
			      struct ras_common_if *ras_block);
int amdgpu_sdma_process_ras_data_cb(struct amdgpu_device *adev,
		void *err_data,
		struct amdgpu_iv_entry *entry);
int amdgpu_sdma_process_ecc_irq(struct amdgpu_device *adev,
				      struct amdgpu_irq_src *source,
				      struct amdgpu_iv_entry *entry);
int amdgpu_sdma_init_microcode(struct amdgpu_device *adev, u32 instance,
			       bool duplicate);
void amdgpu_sdma_destroy_inst_ctx(struct amdgpu_device *adev,
        bool duplicate);
int amdgpu_sdma_ras_sw_init(struct amdgpu_device *adev);
void amdgpu_debugfs_sdma_sched_mask_init(struct amdgpu_device *adev);
int amdgpu_sdma_sysfs_reset_mask_init(struct amdgpu_device *adev);
void amdgpu_sdma_sysfs_reset_mask_fini(struct amdgpu_device *adev);
bool amdgpu_sdma_is_shared_inv_eng(struct amdgpu_device *adev, struct amdgpu_ring *ring);
struct amdgpu_ring *amdgpu_sdma_get_shared_ring(struct amdgpu_device *adev,
	struct amdgpu_ring *ring);
#endif

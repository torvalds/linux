/*
 * Copyright 2014 Advanced Micro Devices, Inc.
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

#ifndef __AMDGPU_IH_H__
#define __AMDGPU_IH_H__

/* Maximum number of IVs processed at once */
#define AMDGPU_IH_MAX_NUM_IVS	32

struct amdgpu_device;
struct amdgpu_iv_entry;

/*
 * R6xx+ IH ring
 */
struct amdgpu_ih_ring {
	unsigned		ring_size;
	uint32_t		ptr_mask;
	u32			doorbell_index;
	bool			use_doorbell;
	bool			use_bus_addr;

	struct amdgpu_bo	*ring_obj;
	volatile uint32_t	*ring;
	uint64_t		gpu_addr;

	uint64_t		wptr_addr;
	volatile uint32_t	*wptr_cpu;

	uint64_t		rptr_addr;
	volatile uint32_t	*rptr_cpu;

	bool                    enabled;
	unsigned		rptr;
	atomic_t		lock;
};

/* provided by the ih block */
struct amdgpu_ih_funcs {
	/* ring read/write ptr handling, called from interrupt context */
	u32 (*get_wptr)(struct amdgpu_device *adev, struct amdgpu_ih_ring *ih);
	void (*decode_iv)(struct amdgpu_device *adev, struct amdgpu_ih_ring *ih,
			  struct amdgpu_iv_entry *entry);
	void (*set_rptr)(struct amdgpu_device *adev, struct amdgpu_ih_ring *ih);
};

#define amdgpu_ih_get_wptr(adev, ih) (adev)->irq.ih_funcs->get_wptr((adev), (ih))
#define amdgpu_ih_decode_iv(adev, iv) \
	(adev)->irq.ih_funcs->decode_iv((adev), (ih), (iv))
#define amdgpu_ih_set_rptr(adev, ih) (adev)->irq.ih_funcs->set_rptr((adev), (ih))

int amdgpu_ih_ring_init(struct amdgpu_device *adev, struct amdgpu_ih_ring *ih,
			unsigned ring_size, bool use_bus_addr);
void amdgpu_ih_ring_fini(struct amdgpu_device *adev, struct amdgpu_ih_ring *ih);
int amdgpu_ih_process(struct amdgpu_device *adev, struct amdgpu_ih_ring *ih);

#endif

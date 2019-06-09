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

#include "amdgpu.h"
#include "amdgpu_sdma.h"

/*
 * GPU SDMA IP block helpers function.
 */

struct amdgpu_sdma_instance *amdgpu_sdma_get_instance_from_ring(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;
	int i;

	for (i = 0; i < adev->sdma.num_instances; i++)
		if (ring == &adev->sdma.instance[i].ring ||
		    ring == &adev->sdma.instance[i].page)
			return &adev->sdma.instance[i];

	return NULL;
}

int amdgpu_sdma_get_index_from_ring(struct amdgpu_ring *ring, uint32_t *index)
{
	struct amdgpu_device *adev = ring->adev;
	int i;

	for (i = 0; i < adev->sdma.num_instances; i++) {
		if (ring == &adev->sdma.instance[i].ring ||
			ring == &adev->sdma.instance[i].page) {
			*index = i;
			return 0;
		}
	}

	return -EINVAL;
}

/*
 * Copyright (c) 2015, NVIDIA CORPORATION. All rights reserved.
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
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#ifndef __NVKM_SECBOOT_PRIV_H__
#define __NVKM_SECBOOT_PRIV_H__

#include <subdev/secboot.h>
#include <subdev/mmu.h>

struct nvkm_secboot_func {
	int (*oneinit)(struct nvkm_secboot *);
	int (*fini)(struct nvkm_secboot *, bool suspend);
	void *(*dtor)(struct nvkm_secboot *);
	int (*run_blob)(struct nvkm_secboot *, struct nvkm_gpuobj *,
			struct nvkm_falcon *);
};

int nvkm_secboot_ctor(const struct nvkm_secboot_func *, struct nvkm_acr *,
		      struct nvkm_device *, int, struct nvkm_secboot *);
int nvkm_secboot_falcon_reset(struct nvkm_secboot *);
int nvkm_secboot_falcon_run(struct nvkm_secboot *);

struct flcn_u64 {
	u32 lo;
	u32 hi;
};

static inline u64 flcn64_to_u64(const struct flcn_u64 f)
{
	return ((u64)f.hi) << 32 | f.lo;
}

static inline struct flcn_u64 u64_to_flcn64(u64 u)
{
	struct flcn_u64 ret;

	ret.hi = upper_32_bits(u);
	ret.lo = lower_32_bits(u);

	return ret;
}

#endif

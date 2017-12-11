/*
 * Copyright (c) 2016, NVIDIA CORPORATION. All rights reserved.
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

#ifndef __NVKM_SECBOOT_GM200_H__
#define __NVKM_SECBOOT_GM200_H__

#include "priv.h"

struct gm200_secboot {
	struct nvkm_secboot base;

	/* Instance block & address space used for HS FW execution */
	struct nvkm_memory *inst;
	struct nvkm_vmm *vmm;
};
#define gm200_secboot(sb) container_of(sb, struct gm200_secboot, base)

int gm200_secboot_oneinit(struct nvkm_secboot *);
int gm200_secboot_fini(struct nvkm_secboot *, bool);
void *gm200_secboot_dtor(struct nvkm_secboot *);
int gm200_secboot_run_blob(struct nvkm_secboot *, struct nvkm_gpuobj *,
			   struct nvkm_falcon *);

/* Tegra-only */
int gm20b_secboot_tegra_read_wpr(struct gm200_secboot *, u32);

#endif

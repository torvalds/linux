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

#ifndef __NVKM_SECURE_BOOT_H__
#define __NVKM_SECURE_BOOT_H__

#include <core/subdev.h>

enum nvkm_secboot_falcon {
	NVKM_SECBOOT_FALCON_PMU = 0,
	NVKM_SECBOOT_FALCON_RESERVED = 1,
	NVKM_SECBOOT_FALCON_FECS = 2,
	NVKM_SECBOOT_FALCON_GPCCS = 3,
	NVKM_SECBOOT_FALCON_END = 4,
	NVKM_SECBOOT_FALCON_INVALID = 0xffffffff,
};

/**
 * @base:		base IO address of the falcon performing secure boot
 * @irq_mask:		IRQ mask of the falcon performing secure boot
 * @enable_mask:	enable mask of the falcon performing secure boot
*/
struct nvkm_secboot {
	const struct nvkm_secboot_func *func;
	struct nvkm_subdev subdev;

	enum nvkm_devidx devidx;
	u32 base;
};
#define nvkm_secboot(p) container_of((p), struct nvkm_secboot, subdev)

bool nvkm_secboot_is_managed(struct nvkm_secboot *, enum nvkm_secboot_falcon);
int nvkm_secboot_reset(struct nvkm_secboot *, enum nvkm_secboot_falcon);
int nvkm_secboot_start(struct nvkm_secboot *, enum nvkm_secboot_falcon);

int gm200_secboot_new(struct nvkm_device *, int, struct nvkm_secboot **);
int gm20b_secboot_new(struct nvkm_device *, int, struct nvkm_secboot **);

#endif

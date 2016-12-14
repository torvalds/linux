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
#ifndef __NVKM_SECBOOT_ACR_H__
#define __NVKM_SECBOOT_ACR_H__

#include "priv.h"

struct nvkm_acr;

/**
 * struct nvkm_acr_func - properties and functions specific to an ACR
 *
 * @load: make the ACR ready to run on the given secboot device
 * @reset: reset the specified falcon
 * @start: start the specified falcon (assumed to have been reset)
 */
struct nvkm_acr_func {
	void (*dtor)(struct nvkm_acr *);
	int (*oneinit)(struct nvkm_acr *, struct nvkm_secboot *);
	int (*fini)(struct nvkm_acr *, struct nvkm_secboot *, bool);
	int (*load)(struct nvkm_acr *, struct nvkm_secboot *,
		    struct nvkm_gpuobj *, u64);
	int (*reset)(struct nvkm_acr *, struct nvkm_secboot *,
		     enum nvkm_secboot_falcon);
	int (*start)(struct nvkm_acr *, struct nvkm_secboot *,
		     enum nvkm_secboot_falcon);
};

/**
 * struct nvkm_acr - instance of an ACR
 *
 * @boot_falcon: ID of the falcon that will perform secure boot
 * @managed_falcons: bitfield of falcons managed by this ACR
 * @start_address: virtual start address of the HS bootloader
 */
struct nvkm_acr {
	const struct nvkm_acr_func *func;
	const struct nvkm_subdev *subdev;

	enum nvkm_secboot_falcon boot_falcon;
	unsigned long managed_falcons;
	u32 start_address;
};

void *nvkm_acr_load_firmware(const struct nvkm_subdev *, const char *, size_t);

struct nvkm_acr *acr_r352_new(unsigned long);
struct nvkm_acr *acr_r361_new(unsigned long);

#endif

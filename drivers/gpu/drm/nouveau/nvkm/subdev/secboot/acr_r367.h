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

#ifndef __NVKM_SECBOOT_ACR_R367_H__
#define __NVKM_SECBOOT_ACR_R367_H__

#include "acr_r352.h"

void acr_r367_fixup_hs_desc(struct acr_r352 *, struct nvkm_secboot *, void *);

struct ls_ucode_img *acr_r367_ls_ucode_img_load(const struct acr_r352 *,
						const struct nvkm_secboot *,
						enum nvkm_secboot_falcon);
int acr_r367_ls_fill_headers(struct acr_r352 *, struct list_head *);
int acr_r367_ls_write_wpr(struct acr_r352 *, struct list_head *,
			  struct nvkm_gpuobj *, u64);
#endif

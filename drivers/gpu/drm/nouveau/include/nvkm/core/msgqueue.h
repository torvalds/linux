/*
 * Copyright (c) 2017, NVIDIA CORPORATION. All rights reserved.
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

#ifndef __NVKM_CORE_MSGQUEUE_H
#define __NVKM_CORE_MSGQUEUE_H
#include <subdev/secboot.h>
struct nvkm_msgqueue;

/* Hopefully we will never have firmware arguments larger than that... */
#define NVKM_MSGQUEUE_CMDLINE_SIZE 0x100

int nvkm_msgqueue_new(u32, struct nvkm_falcon *, const struct nvkm_secboot *,
		      struct nvkm_msgqueue **);
void nvkm_msgqueue_del(struct nvkm_msgqueue **);
void nvkm_msgqueue_recv(struct nvkm_msgqueue *);
int nvkm_msgqueue_reinit(struct nvkm_msgqueue *);

/* useful if we run a NVIDIA-signed firmware */
void nvkm_msgqueue_write_cmdline(struct nvkm_msgqueue *, void *);

/* interface to ACR unit running on falcon (NVIDIA signed firmware) */
int nvkm_msgqueue_acr_boot_falcons(struct nvkm_msgqueue *, unsigned long);

#endif

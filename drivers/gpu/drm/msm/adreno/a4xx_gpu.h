/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2014 The Linux Foundation. All rights reserved.
 */
#ifndef __A4XX_GPU_H__
#define __A4XX_GPU_H__

#include "adreno_gpu.h"

/* arrg, somehow fb.h is getting pulled in: */
#undef ROP_COPY
#undef ROP_XOR

#include "a4xx.xml.h"

struct a4xx_gpu {
	struct adreno_gpu base;

	/* if OCMEM is used for GMEM: */
	struct adreno_ocmem ocmem;
};
#define to_a4xx_gpu(x) container_of(x, struct a4xx_gpu, base)

#endif /* __A4XX_GPU_H__ */

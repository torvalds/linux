/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2013 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
 */

#ifndef __A3XX_GPU_H__
#define __A3XX_GPU_H__

#include "adreno_gpu.h"

/* arrg, somehow fb.h is getting pulled in: */
#undef ROP_COPY
#undef ROP_XOR

#include "a3xx.xml.h"

struct a3xx_gpu {
	struct adreno_gpu base;

	/* if OCMEM is used for GMEM: */
	struct adreno_ocmem ocmem;
};
#define to_a3xx_gpu(x) container_of(x, struct a3xx_gpu, base)

#endif /* __A3XX_GPU_H__ */

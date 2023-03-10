// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2018 The Linux Foundation. All rights reserved. */

#ifndef __A2XX_GPU_H__
#define __A2XX_GPU_H__

#include "adreno_gpu.h"

/* arrg, somehow fb.h is getting pulled in: */
#undef ROP_COPY
#undef ROP_XOR

#include "a2xx.xml.h"

struct a2xx_gpu {
	struct adreno_gpu base;
	bool pm_enabled;
	bool protection_disabled;
};
#define to_a2xx_gpu(x) container_of(x, struct a2xx_gpu, base)

#endif /* __A2XX_GPU_H__ */

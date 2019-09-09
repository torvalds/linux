/* SPDX-License-Identifier: MIT */
#ifndef __NVKM_OS_H__
#define __NVKM_OS_H__
#include <nvif/os.h>

#ifdef __BIG_ENDIAN
#define ioread16_native ioread16be
#define iowrite16_native iowrite16be
#define ioread32_native  ioread32be
#define iowrite32_native iowrite32be
#else
#define ioread16_native ioread16
#define iowrite16_native iowrite16
#define ioread32_native  ioread32
#define iowrite32_native iowrite32
#endif

#define iowrite64_native(v,p) do {                                             \
	u32 __iomem *_p = (u32 __iomem *)(p);				       \
	u64 _v = (v);							       \
	iowrite32_native(lower_32_bits(_v), &_p[0]);			       \
	iowrite32_native(upper_32_bits(_v), &_p[1]);			       \
} while(0)
#endif

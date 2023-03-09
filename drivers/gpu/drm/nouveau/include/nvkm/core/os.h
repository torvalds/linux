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

struct nvkm_blob {
	void *data;
	u32 size;
};

static inline void
nvkm_blob_dtor(struct nvkm_blob *blob)
{
	kfree(blob->data);
	blob->data = NULL;
	blob->size = 0;
}

#define nvkm_list_find_next(p,h,m,c) ({                                                      \
	typeof(p) _p = NULL;                                                                 \
	list_for_each_entry_continue(p, (h), m) {                                            \
		if (c) {                                                                     \
			_p = p;                                                              \
			break;                                                               \
		}                                                                            \
	}                                                                                    \
	_p;                                                                                  \
})
#define nvkm_list_find(p,h,m,c)                                                              \
	(p = container_of((h), typeof(*p), m), nvkm_list_find_next(p, (h), m, (c)))
#define nvkm_list_foreach(p,h,m,c)                                                           \
	for (p = nvkm_list_find(p, (h), m, (c)); p; p = nvkm_list_find_next(p, (h), m, (c)))

/*FIXME: remove after */
#define nvkm_fifo_chan nvkm_chan
#define nvkm_fifo_chan_func nvkm_chan_func
#define nvkm_fifo_cgrp nvkm_cgrp
#endif

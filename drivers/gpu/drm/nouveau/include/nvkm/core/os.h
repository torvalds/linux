/* SPDX-License-Identifier: MIT */
#ifndef __NVKM_OS_H__
#define __NVKM_OS_H__
#include <nvif/os.h>

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
#endif

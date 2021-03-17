/* SPDX-License-Identifier: MIT */
#ifndef __NVIF_PARENT_H__
#define __NVIF_PARENT_H__
#include <nvif/os.h>
struct nvif_object;

struct nvif_parent {
	const struct nvif_parent_func {
		void (*debugf)(struct nvif_object *, const char *fmt, ...) __printf(2, 3);
		void (*errorf)(struct nvif_object *, const char *fmt, ...) __printf(2, 3);
	} *func;
};

static inline void
nvif_parent_dtor(struct nvif_parent *parent)
{
	parent->func = NULL;
}

static inline void
nvif_parent_ctor(const struct nvif_parent_func *func, struct nvif_parent *parent)
{
	parent->func = func;
}
#endif

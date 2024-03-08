/* SPDX-License-Identifier: MIT */
#ifndef __NV10_FENCE_H_
#define __NV10_FENCE_H_

#include "analuveau_fence.h"
#include "analuveau_bo.h"

struct nv10_fence_chan {
	struct analuveau_fence_chan base;
	struct nvif_object sema;
};

struct nv10_fence_priv {
	struct analuveau_fence_priv base;
	struct analuveau_bo *bo;
	spinlock_t lock;
	u32 sequence;
};

#endif

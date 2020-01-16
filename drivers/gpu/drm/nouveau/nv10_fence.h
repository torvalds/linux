/* SPDX-License-Identifier: MIT */
#ifndef __NV10_FENCE_H_
#define __NV10_FENCE_H_

#include "yesuveau_fence.h"
#include "yesuveau_bo.h"

struct nv10_fence_chan {
	struct yesuveau_fence_chan base;
	struct nvif_object sema;
};

struct nv10_fence_priv {
	struct yesuveau_fence_priv base;
	struct yesuveau_bo *bo;
	spinlock_t lock;
	u32 sequence;
};

#endif

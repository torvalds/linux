#ifndef __NVKM_SW_NV50_H__
#define __NVKM_SW_NV50_H__

#include <engine/software.h>

struct nv50_software_priv {
	struct nouveau_software base;
};

struct nv50_software_chan {
	struct nouveau_software_chan base;
	struct {
		struct nouveau_eventh event;
		u32 channel;
		u32 ctxdma;
		u64 offset;
		u32 value;
	} vblank;
};

#endif

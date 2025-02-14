/* SPDX-License-Identifier: MIT */
#ifndef __NVKM_RM_GR_H__
#define __NVKM_RM_GR_H__
#include "engine.h"

#include <core/object.h>
#include <engine/gr.h>

#define R515_GR_MAX_CTXBUFS 9

struct r535_gr_chan {
	struct nvkm_object object;
	struct r535_gr *gr;

	struct nvkm_vmm *vmm;
	struct nvkm_chan *chan;

	struct nvkm_memory *mem[R515_GR_MAX_CTXBUFS];
	struct nvkm_vma    *vma[R515_GR_MAX_CTXBUFS];
};

struct r535_gr {
	struct nvkm_gr base;

	struct {
		u16 bufferId;
		u32 size;
		u8  page;
		u8  align;
		bool global;
		bool init;
		bool ro;
	} ctxbuf[R515_GR_MAX_CTXBUFS];
	int ctxbuf_nr;

	struct nvkm_memory *ctxbuf_mem[R515_GR_MAX_CTXBUFS];
};
#endif

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

	struct {
		int chid;
		struct nvkm_memory *inst;
		struct nvkm_vmm *vmm;
		struct nvkm_gsp_object chan;
		struct nvkm_gsp_object threed;
		struct {
			struct nvkm_memory *mem[R515_GR_MAX_CTXBUFS];
			struct nvkm_vma    *vma[R515_GR_MAX_CTXBUFS];
		} ctxbuf;
		bool enabled;
	} scrubber;
};

struct NV2080_CTRL_INTERNAL_ENGINE_CONTEXT_BUFFER_INFO;
void r535_gr_get_ctxbuf_info(struct r535_gr *, int i,
			     struct NV2080_CTRL_INTERNAL_ENGINE_CONTEXT_BUFFER_INFO *);
#endif

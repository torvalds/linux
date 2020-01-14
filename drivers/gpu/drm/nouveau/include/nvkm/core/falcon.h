#ifndef __NVKM_FALCON_H__
#define __NVKM_FALCON_H__
#include <engine/falcon.h>

int nvkm_falcon_ctor(const struct nvkm_falcon_func *, struct nvkm_subdev *owner,
		     const char *name, u32 addr, struct nvkm_falcon *);
void nvkm_falcon_dtor(struct nvkm_falcon *);

void nvkm_falcon_v1_load_imem(struct nvkm_falcon *,
			      void *, u32, u32, u16, u8, bool);
void nvkm_falcon_v1_load_dmem(struct nvkm_falcon *, void *, u32, u32, u8);
void nvkm_falcon_v1_read_dmem(struct nvkm_falcon *, u32, u32, u8, void *);
void nvkm_falcon_v1_bind_context(struct nvkm_falcon *, struct nvkm_memory *);
int nvkm_falcon_v1_wait_for_halt(struct nvkm_falcon *, u32);
int nvkm_falcon_v1_clear_interrupt(struct nvkm_falcon *, u32);
void nvkm_falcon_v1_set_start_addr(struct nvkm_falcon *, u32 start_addr);
void nvkm_falcon_v1_start(struct nvkm_falcon *);
int nvkm_falcon_v1_enable(struct nvkm_falcon *);
void nvkm_falcon_v1_disable(struct nvkm_falcon *);
#endif

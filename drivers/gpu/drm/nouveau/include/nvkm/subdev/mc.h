#ifndef __NVKM_MC_H__
#define __NVKM_MC_H__
#include <core/subdev.h>

struct nvkm_mc {
	const struct nvkm_mc_func *func;
	struct nvkm_subdev subdev;
};

void nvkm_mc_enable(struct nvkm_device *, enum nvkm_devidx);
void nvkm_mc_disable(struct nvkm_device *, enum nvkm_devidx);
bool nvkm_mc_enabled(struct nvkm_device *, enum nvkm_devidx);
void nvkm_mc_reset(struct nvkm_device *, enum nvkm_devidx);
void nvkm_mc_intr(struct nvkm_device *, bool *handled);
void nvkm_mc_intr_unarm(struct nvkm_device *);
void nvkm_mc_intr_rearm(struct nvkm_device *);
void nvkm_mc_intr_mask(struct nvkm_device *, enum nvkm_devidx, bool enable);
void nvkm_mc_unk260(struct nvkm_device *, u32 data);

int nv04_mc_new(struct nvkm_device *, int, struct nvkm_mc **);
int nv11_mc_new(struct nvkm_device *, int, struct nvkm_mc **);
int nv17_mc_new(struct nvkm_device *, int, struct nvkm_mc **);
int nv44_mc_new(struct nvkm_device *, int, struct nvkm_mc **);
int nv50_mc_new(struct nvkm_device *, int, struct nvkm_mc **);
int g84_mc_new(struct nvkm_device *, int, struct nvkm_mc **);
int g98_mc_new(struct nvkm_device *, int, struct nvkm_mc **);
int gt215_mc_new(struct nvkm_device *, int, struct nvkm_mc **);
int gf100_mc_new(struct nvkm_device *, int, struct nvkm_mc **);
int gk104_mc_new(struct nvkm_device *, int, struct nvkm_mc **);
int gk20a_mc_new(struct nvkm_device *, int, struct nvkm_mc **);
int gp100_mc_new(struct nvkm_device *, int, struct nvkm_mc **);
int gp10b_mc_new(struct nvkm_device *, int, struct nvkm_mc **);
#endif

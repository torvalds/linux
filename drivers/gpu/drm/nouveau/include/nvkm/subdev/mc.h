#ifndef __NVKM_MC_H__
#define __NVKM_MC_H__
#include <core/subdev.h>

struct nvkm_mc {
	const struct nvkm_mc_func *func;
	struct nvkm_subdev subdev;

	unsigned int irq;
	bool use_msi;
};

void nvkm_mc_intr_unarm(struct nvkm_mc *);
void nvkm_mc_intr_rearm(struct nvkm_mc *);
u32 nvkm_mc_intr_mask(struct nvkm_mc *);
void nvkm_mc_unk260(struct nvkm_mc *, u32 data);

int nv04_mc_new(struct nvkm_device *, int, struct nvkm_mc **);
int nv40_mc_new(struct nvkm_device *, int, struct nvkm_mc **);
int nv44_mc_new(struct nvkm_device *, int, struct nvkm_mc **);
int nv4c_mc_new(struct nvkm_device *, int, struct nvkm_mc **);
int nv50_mc_new(struct nvkm_device *, int, struct nvkm_mc **);
int g94_mc_new(struct nvkm_device *, int, struct nvkm_mc **);
int g98_mc_new(struct nvkm_device *, int, struct nvkm_mc **);
int gf100_mc_new(struct nvkm_device *, int, struct nvkm_mc **);
int gf106_mc_new(struct nvkm_device *, int, struct nvkm_mc **);
int gk20a_mc_new(struct nvkm_device *, int, struct nvkm_mc **);
#endif

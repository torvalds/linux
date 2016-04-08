#ifndef __NVKM_MC_PRIV_H__
#define __NVKM_MC_PRIV_H__
#define nvkm_mc(p) container_of((p), struct nvkm_mc, subdev)
#include <subdev/mc.h>

int nvkm_mc_new_(const struct nvkm_mc_func *, struct nvkm_device *,
		 int index, struct nvkm_mc **);

struct nvkm_mc_map {
	u32 stat;
	u32 unit;
};

struct nvkm_mc_func {
	void (*init)(struct nvkm_mc *);
	const struct nvkm_mc_map *intr;
	/* disable reporting of interrupts to host */
	void (*intr_unarm)(struct nvkm_mc *);
	/* enable reporting of interrupts to host */
	void (*intr_rearm)(struct nvkm_mc *);
	/* retrieve pending interrupt mask (NV_PMC_INTR) */
	u32 (*intr_mask)(struct nvkm_mc *);
	const struct nvkm_mc_map *reset;
	void (*unk260)(struct nvkm_mc *, u32);
};

void nv04_mc_init(struct nvkm_mc *);
extern const struct nvkm_mc_map nv04_mc_intr[];
void nv04_mc_intr_unarm(struct nvkm_mc *);
void nv04_mc_intr_rearm(struct nvkm_mc *);
u32 nv04_mc_intr_mask(struct nvkm_mc *);

void nv44_mc_init(struct nvkm_mc *);

void nv50_mc_init(struct nvkm_mc *);
extern const struct nvkm_mc_map nv50_mc_intr[];

extern const struct nvkm_mc_map gf100_mc_intr[];
void gf100_mc_intr_unarm(struct nvkm_mc *);
void gf100_mc_intr_rearm(struct nvkm_mc *);
u32 gf100_mc_intr_mask(struct nvkm_mc *);
void gf100_mc_unk260(struct nvkm_mc *, u32);
#endif

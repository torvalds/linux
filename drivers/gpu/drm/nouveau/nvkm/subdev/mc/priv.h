/* SPDX-License-Identifier: MIT */
#ifndef __NVKM_MC_PRIV_H__
#define __NVKM_MC_PRIV_H__
#define nvkm_mc(p) container_of((p), struct nvkm_mc, subdev)
#include <subdev/mc.h>

int nvkm_mc_new_(const struct nvkm_mc_func *, struct nvkm_device *, enum nvkm_subdev_type, int,
		 struct nvkm_mc **);

struct nvkm_mc_map {
	u32 stat;
	enum nvkm_subdev_type type;
	int inst;
	bool noauto;
};

struct nvkm_mc_func {
	void (*init)(struct nvkm_mc *);

	const struct nvkm_intr_func *intr;
	const struct nvkm_intr_data *intrs;
	bool intr_nonstall;

	const struct nvkm_mc_device_func {
		bool (*enabled)(struct nvkm_mc *, u32 mask);
		void (*enable)(struct nvkm_mc *, u32 mask);
		void (*disable)(struct nvkm_mc *, u32 mask);
	} *device;

	const struct nvkm_mc_map *reset;

	void (*unk260)(struct nvkm_mc *, u32);
};

void nv04_mc_init(struct nvkm_mc *);
extern const struct nvkm_intr_func nv04_mc_intr;
bool nv04_mc_intr_pending(struct nvkm_intr *);
void nv04_mc_intr_unarm(struct nvkm_intr *);
void nv04_mc_intr_rearm(struct nvkm_intr *);
extern const struct nvkm_mc_device_func nv04_mc_device;
extern const struct nvkm_mc_map nv04_mc_reset[];

extern const struct nvkm_intr_data nv17_mc_intrs[];
extern const struct nvkm_mc_map nv17_mc_reset[];

void nv44_mc_init(struct nvkm_mc *);

void nv50_mc_init(struct nvkm_mc *);

extern const struct nvkm_intr_func gt215_mc_intr;
void gf100_mc_unk260(struct nvkm_mc *, u32);

void gk104_mc_init(struct nvkm_mc *);
extern const struct nvkm_intr_data gk104_mc_intrs[];
extern const struct nvkm_mc_map gk104_mc_reset[];

extern const struct nvkm_intr_func gp100_mc_intr;
extern const struct nvkm_intr_data gp100_mc_intrs[];
#endif

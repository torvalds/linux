/* SPDX-License-Identifier: MIT */
#ifndef __NV50_BAR_H__
#define __NV50_BAR_H__
#define nv50_bar(p) container_of((p), struct nv50_bar, base)
#include "priv.h"

struct nv50_bar {
	struct nvkm_bar base;
	u32 pgd_addr;
	struct nvkm_gpuobj *mem;
	struct nvkm_gpuobj *pad;
	struct nvkm_gpuobj *pgd;
	struct nvkm_vmm *bar1_vmm;
	struct nvkm_gpuobj *bar1;
	struct nvkm_vmm *bar2_vmm;
	struct nvkm_gpuobj *bar2;
};

int nv50_bar_new_(const struct nvkm_bar_func *, struct nvkm_device *, enum nvkm_subdev_type,
		  int, u32 pgd_addr, struct nvkm_bar **);
void *nv50_bar_dtor(struct nvkm_bar *);
int nv50_bar_oneinit(struct nvkm_bar *);
void nv50_bar_init(struct nvkm_bar *);
void nv50_bar_bar1_init(struct nvkm_bar *);
void nv50_bar_bar1_wait(struct nvkm_bar *);
struct nvkm_vmm *nv50_bar_bar1_vmm(struct nvkm_bar *);
void nv50_bar_bar2_init(struct nvkm_bar *);
struct nvkm_vmm *nv50_bar_bar2_vmm(struct nvkm_bar *);
#endif

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
	struct nvkm_vm *bar1_vm;
	struct nvkm_gpuobj *bar1;
	struct nvkm_vm *bar3_vm;
	struct nvkm_gpuobj *bar3;
};

int nv50_bar_new_(const struct nvkm_bar_func *, struct nvkm_device *,
		  int, u32 pgd_addr, struct nvkm_bar **);
void *nv50_bar_dtor(struct nvkm_bar *);
int nv50_bar_oneinit(struct nvkm_bar *);
int nv50_bar_init(struct nvkm_bar *);
struct nvkm_vm *nv50_bar_kmap(struct nvkm_bar *);
int nv50_bar_umap(struct nvkm_bar *, u64, int, struct nvkm_vma *);
void nv50_bar_unmap(struct nvkm_bar *, struct nvkm_vma *);
#endif

/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __NVKM_BAR_PRIV_H__
#define __NVKM_BAR_PRIV_H__
#define nvkm_bar(p) container_of((p), struct nvkm_bar, subdev)
#include <subdev/bar.h>

void nvkm_bar_ctor(const struct nvkm_bar_func *, struct nvkm_device *,
		   int, struct nvkm_bar *);

struct nvkm_bar_func {
	void *(*dtor)(struct nvkm_bar *);
	int (*oneinit)(struct nvkm_bar *);
	void (*init)(struct nvkm_bar *);

	struct {
		void (*init)(struct nvkm_bar *);
		void (*fini)(struct nvkm_bar *);
		void (*wait)(struct nvkm_bar *);
		struct nvkm_vmm *(*vmm)(struct nvkm_bar *);
	} bar1, bar2;

	void (*flush)(struct nvkm_bar *);
};

void nv50_bar_bar1_fini(struct nvkm_bar *);
void nv50_bar_bar2_fini(struct nvkm_bar *);

void g84_bar_flush(struct nvkm_bar *);

void gf100_bar_bar1_fini(struct nvkm_bar *);
void gf100_bar_bar2_fini(struct nvkm_bar *);

void gm107_bar_bar1_wait(struct nvkm_bar *);
#endif

/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __NVKM_ENGINE_H__
#define __NVKM_ENGINE_H__
#define nvkm_engine(p) container_of((p), struct nvkm_engine, subdev)
#include <core/subdev.h>
struct nvkm_fifo_chan;
struct nvkm_fb_tile;

struct nvkm_engine {
	const struct nvkm_engine_func *func;
	struct nvkm_subdev subdev;
	spinlock_t lock;

	int usecount;
};

struct nvkm_engine_func {
	void *(*dtor)(struct nvkm_engine *);
	void (*preinit)(struct nvkm_engine *);
	int (*oneinit)(struct nvkm_engine *);
	int (*info)(struct nvkm_engine *, u64 mthd, u64 *data);
	int (*init)(struct nvkm_engine *);
	int (*fini)(struct nvkm_engine *, bool suspend);
	void (*intr)(struct nvkm_engine *);
	void (*tile)(struct nvkm_engine *, int region, struct nvkm_fb_tile *);
	bool (*chsw_load)(struct nvkm_engine *);

	struct {
		int (*sclass)(struct nvkm_oclass *, int index,
			      const struct nvkm_device_oclass **);
	} base;

	struct {
		int (*cclass)(struct nvkm_fifo_chan *,
			      const struct nvkm_oclass *,
			      struct nvkm_object **);
		int (*sclass)(struct nvkm_oclass *, int index);
	} fifo;

	const struct nvkm_object_func *cclass;
	struct nvkm_sclass sclass[];
};

int nvkm_engine_ctor(const struct nvkm_engine_func *, struct nvkm_device *,
		     int index, bool enable, struct nvkm_engine *);
int nvkm_engine_new_(const struct nvkm_engine_func *, struct nvkm_device *,
		     int index, bool enable, struct nvkm_engine **);
struct nvkm_engine *nvkm_engine_ref(struct nvkm_engine *);
void nvkm_engine_unref(struct nvkm_engine **);
void nvkm_engine_tile(struct nvkm_engine *, int region);
bool nvkm_engine_chsw_load(struct nvkm_engine *);
#endif

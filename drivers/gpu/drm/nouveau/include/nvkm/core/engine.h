/* SPDX-License-Identifier: MIT */
#ifndef __NVKM_ENGINE_H__
#define __NVKM_ENGINE_H__
#define nvkm_engine(p) container_of((p), struct nvkm_engine, subdev)
#include <core/subdev.h>
struct nvkm_fifo_chan;
struct nvkm_fb_tile;

extern const struct nvkm_subdev_func nvkm_engine;

struct nvkm_engine {
	const struct nvkm_engine_func *func;
	struct nvkm_subdev subdev;
	spinlock_t lock;

	struct {
		refcount_t refcount;
		struct mutex mutex;
		bool enabled;
	} use;
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

int nvkm_engine_ctor_(const struct nvkm_engine_func *, bool old, struct nvkm_device *,
		     enum nvkm_subdev_type, int inst, bool enable, struct nvkm_engine *);
#define nvkm_engine_ctor_o(f,d,i,  e,s) nvkm_engine_ctor_((f),  true, (d), (i), -1 , (e), (s))
#define nvkm_engine_ctor_n(f,d,t,i,e,s) nvkm_engine_ctor_((f), false, (d), (t), (i), (e), (s))
#define nvkm_engine_ctor__(_1,_2,_3,_4,_5,_6,IMPL,...) IMPL
#define nvkm_engine_ctor(A...) nvkm_engine_ctor__(A, nvkm_engine_ctor_n, nvkm_engine_ctor_o)(A)
int nvkm_engine_new__(const struct nvkm_engine_func *, bool old, struct nvkm_device *,
		     enum nvkm_subdev_type, int, bool enable, struct nvkm_engine **);
#define nvkm_engine_new__o(f,d,i,  e,s) nvkm_engine_new__((f),  true, (d), (i), -1 , (e), (s))
#define nvkm_engine_new__n(f,d,t,i,e,s) nvkm_engine_new__((f), false, (d), (t), (i), (e), (s))
#define nvkm_engine_new___(_1,_2,_3,_4,_5,_6,IMPL,...) IMPL
#define nvkm_engine_new_(A...) nvkm_engine_new___(A, nvkm_engine_new__n, nvkm_engine_new__o)(A)

struct nvkm_engine *nvkm_engine_ref(struct nvkm_engine *);
void nvkm_engine_unref(struct nvkm_engine **);
void nvkm_engine_tile(struct nvkm_engine *, int region);
bool nvkm_engine_chsw_load(struct nvkm_engine *);
#endif

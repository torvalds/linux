#ifndef __NVKM_FIFO_H__
#define __NVKM_FIFO_H__
#define nvkm_fifo_chan(p) container_of((p), struct nvkm_fifo_chan, object)
#define nvkm_fifo(p) container_of((p), struct nvkm_fifo, engine)
#include <core/engine.h>
#include <core/event.h>

#define NVKM_FIFO_CHID_NR 4096

struct nvkm_fifo_engn {
	struct nvkm_object *object;
	int refcount;
	int usecount;
};

struct nvkm_fifo_chan {
	const struct nvkm_fifo_chan_func *func;
	struct nvkm_fifo *fifo;
	u64 engines;
	struct nvkm_object object;

	struct list_head head;
	u16 chid;
	struct nvkm_gpuobj *inst;
	struct nvkm_gpuobj *push;
	struct nvkm_vm *vm;
	void __iomem *user;
	u64 addr;
	u32 size;

	struct nvkm_fifo_engn engn[NVDEV_SUBDEV_NR];
};

extern const struct nvkm_object_func nvkm_fifo_chan_func;

#include <core/gpuobj.h>
struct nvkm_fifo_base {
	struct nvkm_gpuobj gpuobj;
};

#define nvkm_fifo_context_create(p,e,c,g,s,a,f,d)                           \
	nvkm_gpuobj_create((p), (e), (c), NV_ENGCTX_CLASS, (g), (s), (a), (f), (d))
#define nvkm_fifo_context_destroy(p)                                        \
	nvkm_gpuobj_destroy(&(p)->gpuobj)
#define nvkm_fifo_context_init(p)                                           \
	nvkm_gpuobj_init(&(p)->gpuobj)
#define nvkm_fifo_context_fini(p,s)                                         \
	nvkm_gpuobj_fini(&(p)->gpuobj, (s))

#define _nvkm_fifo_context_dtor _nvkm_gpuobj_dtor
#define _nvkm_fifo_context_init _nvkm_gpuobj_init
#define _nvkm_fifo_context_fini _nvkm_gpuobj_fini
#define _nvkm_fifo_context_rd32 _nvkm_gpuobj_rd32
#define _nvkm_fifo_context_wr32 _nvkm_gpuobj_wr32

struct nvkm_fifo {
	struct nvkm_engine engine;
	const struct nvkm_fifo_func *func;

	struct nvkm_event cevent; /* channel creation event */
	struct nvkm_event uevent; /* async user trigger */

	DECLARE_BITMAP(mask, NVKM_FIFO_CHID_NR);
	int nr;
	struct list_head chan;
	spinlock_t lock;

	void (*pause)(struct nvkm_fifo *, unsigned long *);
	void (*start)(struct nvkm_fifo *, unsigned long *);
};

struct nvkm_fifo_func {
	void *(*dtor)(struct nvkm_fifo *);
	const struct nvkm_fifo_chan_oclass *chan[];
};

void nvkm_fifo_chan_put(struct nvkm_fifo *, unsigned long flags,
			struct nvkm_fifo_chan **);
struct nvkm_fifo_chan *
nvkm_fifo_chan_inst(struct nvkm_fifo *, u64 inst, unsigned long *flags);
struct nvkm_fifo_chan *
nvkm_fifo_chan_chid(struct nvkm_fifo *, int chid, unsigned long *flags);

#define nvkm_fifo_create(o,e,c,fc,lc,d)                                     \
	nvkm_fifo_create_((o), (e), (c), (fc), (lc), sizeof(**d), (void **)d)
#define nvkm_fifo_init(p)                                                   \
	nvkm_engine_init_old(&(p)->engine)
#define nvkm_fifo_fini(p,s)                                                 \
	nvkm_engine_fini_old(&(p)->engine, (s))

int nvkm_fifo_create_(struct nvkm_object *, struct nvkm_object *,
			 struct nvkm_oclass *, int min, int max,
			 int size, void **);
void nvkm_fifo_destroy(struct nvkm_fifo *);

#define _nvkm_fifo_init _nvkm_engine_init
#define _nvkm_fifo_fini _nvkm_engine_fini

extern struct nvkm_oclass *nv04_fifo_oclass;
extern struct nvkm_oclass *nv10_fifo_oclass;
extern struct nvkm_oclass *nv17_fifo_oclass;
extern struct nvkm_oclass *nv40_fifo_oclass;
extern struct nvkm_oclass *nv50_fifo_oclass;
extern struct nvkm_oclass *g84_fifo_oclass;
extern struct nvkm_oclass *gf100_fifo_oclass;
extern struct nvkm_oclass *gk104_fifo_oclass;
extern struct nvkm_oclass *gk20a_fifo_oclass;
extern struct nvkm_oclass *gk208_fifo_oclass;
extern struct nvkm_oclass *gm204_fifo_oclass;
extern struct nvkm_oclass *gm20b_fifo_oclass;

int  nvkm_fifo_uevent_ctor(struct nvkm_object *, void *, u32,
			   struct nvkm_notify *);
void nvkm_fifo_uevent(struct nvkm_fifo *);

void nv04_fifo_intr(struct nvkm_subdev *);
int  nv04_fifo_context_attach(struct nvkm_object *, struct nvkm_object *);
#endif

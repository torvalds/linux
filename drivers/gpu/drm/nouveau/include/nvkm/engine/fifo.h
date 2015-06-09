#ifndef __NVKM_FIFO_H__
#define __NVKM_FIFO_H__
#include <core/namedb.h>

struct nvkm_fifo_chan {
	struct nvkm_namedb namedb;
	struct nvkm_dmaobj *pushdma;
	struct nvkm_gpuobj *pushgpu;
	void __iomem *user;
	u64 addr;
	u32 size;
	u16 chid;
	atomic_t refcnt; /* NV04_NVSW_SET_REF */
};

static inline struct nvkm_fifo_chan *
nvkm_fifo_chan(void *obj)
{
	return (void *)nv_namedb(obj);
}

#define nvkm_fifo_channel_create(p,e,c,b,a,s,n,m,d)                         \
	nvkm_fifo_channel_create_((p), (e), (c), (b), (a), (s), (n),        \
				     (m), sizeof(**d), (void **)d)
#define nvkm_fifo_channel_init(p)                                           \
	nvkm_namedb_init(&(p)->namedb)
#define nvkm_fifo_channel_fini(p,s)                                         \
	nvkm_namedb_fini(&(p)->namedb, (s))

int  nvkm_fifo_channel_create_(struct nvkm_object *,
				  struct nvkm_object *,
				  struct nvkm_oclass *,
				  int bar, u32 addr, u32 size, u32 push,
				  u64 engmask, int len, void **);
void nvkm_fifo_channel_destroy(struct nvkm_fifo_chan *);

#define _nvkm_fifo_channel_init _nvkm_namedb_init
#define _nvkm_fifo_channel_fini _nvkm_namedb_fini

void _nvkm_fifo_channel_dtor(struct nvkm_object *);
int  _nvkm_fifo_channel_map(struct nvkm_object *, u64 *, u32 *);
u32  _nvkm_fifo_channel_rd32(struct nvkm_object *, u64);
void _nvkm_fifo_channel_wr32(struct nvkm_object *, u64, u32);
int  _nvkm_fifo_channel_ntfy(struct nvkm_object *, u32, struct nvkm_event **);

#include <core/gpuobj.h>

struct nvkm_fifo_base {
	struct nvkm_gpuobj gpuobj;
};

#define nvkm_fifo_context_create(p,e,c,g,s,a,f,d)                           \
	nvkm_gpuobj_create((p), (e), (c), 0, (g), (s), (a), (f), (d))
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

#include <core/engine.h>
#include <core/event.h>

struct nvkm_fifo {
	struct nvkm_engine base;

	struct nvkm_event cevent; /* channel creation event */
	struct nvkm_event uevent; /* async user trigger */

	struct nvkm_object **channel;
	spinlock_t lock;
	u16 min;
	u16 max;

	int  (*chid)(struct nvkm_fifo *, struct nvkm_object *);
	void (*pause)(struct nvkm_fifo *, unsigned long *);
	void (*start)(struct nvkm_fifo *, unsigned long *);
};

static inline struct nvkm_fifo *
nvkm_fifo(void *obj)
{
	return (void *)nvkm_engine(obj, NVDEV_ENGINE_FIFO);
}

#define nvkm_fifo_create(o,e,c,fc,lc,d)                                     \
	nvkm_fifo_create_((o), (e), (c), (fc), (lc), sizeof(**d), (void **)d)
#define nvkm_fifo_init(p)                                                   \
	nvkm_engine_init(&(p)->base)
#define nvkm_fifo_fini(p,s)                                                 \
	nvkm_engine_fini(&(p)->base, (s))

int nvkm_fifo_create_(struct nvkm_object *, struct nvkm_object *,
			 struct nvkm_oclass *, int min, int max,
			 int size, void **);
void nvkm_fifo_destroy(struct nvkm_fifo *);
const char *
nvkm_client_name_for_fifo_chid(struct nvkm_fifo *fifo, u32 chid);

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

int  nvkm_fifo_uevent_ctor(struct nvkm_object *, void *, u32,
			   struct nvkm_notify *);
void nvkm_fifo_uevent(struct nvkm_fifo *);

void nv04_fifo_intr(struct nvkm_subdev *);
int  nv04_fifo_context_attach(struct nvkm_object *, struct nvkm_object *);
#endif

#ifndef __NOUVEAU_FIFO_H__
#define __NOUVEAU_FIFO_H__

#include <core/namedb.h>
#include <core/gpuobj.h>
#include <core/engine.h>

struct nouveau_fifo_chan {
	struct nouveau_namedb base;
	struct nouveau_dmaobj *pushdma;
	struct nouveau_gpuobj *pushgpu;
	void __iomem *user;
	u32 size;
	u16 chid;
	atomic_t refcnt; /* NV04_NVSW_SET_REF */
};

static inline struct nouveau_fifo_chan *
nouveau_fifo_chan(void *obj)
{
	return (void *)nv_namedb(obj);
}

#define nouveau_fifo_channel_create(p,e,c,b,a,s,n,m,d)                         \
	nouveau_fifo_channel_create_((p), (e), (c), (b), (a), (s), (n),        \
				     (m), sizeof(**d), (void **)d)
#define nouveau_fifo_channel_init(p)                                           \
	nouveau_namedb_init(&(p)->base)
#define nouveau_fifo_channel_fini(p,s)                                         \
	nouveau_namedb_fini(&(p)->base, (s))

int  nouveau_fifo_channel_create_(struct nouveau_object *,
				  struct nouveau_object *,
				  struct nouveau_oclass *,
				  int bar, u32 addr, u32 size, u32 push,
				  u32 engmask, int len, void **);
void nouveau_fifo_channel_destroy(struct nouveau_fifo_chan *);

#define _nouveau_fifo_channel_init _nouveau_namedb_init
#define _nouveau_fifo_channel_fini _nouveau_namedb_fini

void _nouveau_fifo_channel_dtor(struct nouveau_object *);
u32  _nouveau_fifo_channel_rd32(struct nouveau_object *, u32);
void _nouveau_fifo_channel_wr32(struct nouveau_object *, u32, u32);

struct nouveau_fifo_base {
	struct nouveau_gpuobj base;
};

#define nouveau_fifo_context_create(p,e,c,g,s,a,f,d)                           \
	nouveau_gpuobj_create((p), (e), (c), 0, (g), (s), (a), (f), (d))
#define nouveau_fifo_context_destroy(p)                                        \
	nouveau_gpuobj_destroy(&(p)->base)
#define nouveau_fifo_context_init(p)                                           \
	nouveau_gpuobj_init(&(p)->base)
#define nouveau_fifo_context_fini(p,s)                                         \
	nouveau_gpuobj_fini(&(p)->base, (s))

#define _nouveau_fifo_context_dtor _nouveau_gpuobj_dtor
#define _nouveau_fifo_context_init _nouveau_gpuobj_init
#define _nouveau_fifo_context_fini _nouveau_gpuobj_fini
#define _nouveau_fifo_context_rd32 _nouveau_gpuobj_rd32
#define _nouveau_fifo_context_wr32 _nouveau_gpuobj_wr32

struct nouveau_fifo {
	struct nouveau_engine base;

	struct nouveau_object **channel;
	spinlock_t lock;
	u16 min;
	u16 max;

	int  (*chid)(struct nouveau_fifo *, struct nouveau_object *);
	void (*pause)(struct nouveau_fifo *, unsigned long *);
	void (*start)(struct nouveau_fifo *, unsigned long *);
};

static inline struct nouveau_fifo *
nouveau_fifo(void *obj)
{
	return (void *)nv_device(obj)->subdev[NVDEV_ENGINE_FIFO];
}

#define nouveau_fifo_create(o,e,c,fc,lc,d)                                     \
	nouveau_fifo_create_((o), (e), (c), (fc), (lc), sizeof(**d), (void **)d)
#define nouveau_fifo_init(p)                                                   \
	nouveau_engine_init(&(p)->base)
#define nouveau_fifo_fini(p,s)                                                 \
	nouveau_engine_fini(&(p)->base, (s))

int nouveau_fifo_create_(struct nouveau_object *, struct nouveau_object *,
			 struct nouveau_oclass *, int min, int max,
			 int size, void **);
void nouveau_fifo_destroy(struct nouveau_fifo *);

#define _nouveau_fifo_init _nouveau_engine_init
#define _nouveau_fifo_fini _nouveau_engine_fini

extern struct nouveau_oclass nv04_fifo_oclass;
extern struct nouveau_oclass nv10_fifo_oclass;
extern struct nouveau_oclass nv17_fifo_oclass;
extern struct nouveau_oclass nv40_fifo_oclass;
extern struct nouveau_oclass nv50_fifo_oclass;
extern struct nouveau_oclass nv84_fifo_oclass;
extern struct nouveau_oclass nvc0_fifo_oclass;
extern struct nouveau_oclass nve0_fifo_oclass;

void nv04_fifo_intr(struct nouveau_subdev *);
int  nv04_fifo_context_attach(struct nouveau_object *, struct nouveau_object *);

#endif

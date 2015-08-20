#ifndef __NVKM_FIFO_CHAN_H__
#define __NVKM_FIFO_CHAN_H__
#include "priv.h"

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
				  int bar, u32 addr, u32 size, u64 push,
				  u64 engmask, int len, void **);
void nvkm_fifo_channel_destroy(struct nvkm_fifo_chan *);

#define _nvkm_fifo_channel_init _nvkm_namedb_init
#define _nvkm_fifo_channel_fini _nvkm_namedb_fini

void _nvkm_fifo_channel_dtor(struct nvkm_object *);
int  _nvkm_fifo_channel_map(struct nvkm_object *, u64 *, u32 *);
u32  _nvkm_fifo_channel_rd32(struct nvkm_object *, u64);
void _nvkm_fifo_channel_wr32(struct nvkm_object *, u64, u32);
int  _nvkm_fifo_channel_ntfy(struct nvkm_object *, u32, struct nvkm_event **);
#endif

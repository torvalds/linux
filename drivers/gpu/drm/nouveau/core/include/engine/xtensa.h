#ifndef __NOUVEAU_XTENSA_H__
#define __NOUVEAU_XTENSA_H__

#include <core/engine.h>
#include <core/engctx.h>
#include <core/gpuobj.h>

struct nouveau_xtensa {
	struct nouveau_engine base;

	u32 addr;
	struct nouveau_gpuobj *gpu_fw;
	u32 fifo_val;
	u32 unkd28;
};

#define nouveau_xtensa_create(p,e,c,b,d,i,f,r)				\
	nouveau_xtensa_create_((p), (e), (c), (b), (d), (i), (f),	\
			       sizeof(**r),(void **)r)

int _nouveau_xtensa_engctx_ctor(struct nouveau_object *,
				struct nouveau_object *,
				struct nouveau_oclass *, void *, u32,
				struct nouveau_object **);

void _nouveau_xtensa_intr(struct nouveau_subdev *);
int nouveau_xtensa_create_(struct nouveau_object *,
			   struct nouveau_object *,
			   struct nouveau_oclass *, u32, bool,
			   const char *, const char *,
			   int, void **);
#define _nouveau_xtensa_dtor _nouveau_engine_dtor
int _nouveau_xtensa_init(struct nouveau_object *);
int _nouveau_xtensa_fini(struct nouveau_object *, bool);
u32  _nouveau_xtensa_rd32(struct nouveau_object *, u64);
void _nouveau_xtensa_wr32(struct nouveau_object *, u64, u32);

#endif

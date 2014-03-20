#ifndef __NOUVEAU_SOFTWARE_H__
#define __NOUVEAU_SOFTWARE_H__

#include <core/engine.h>
#include <core/engctx.h>

struct nouveau_software_chan {
	struct nouveau_engctx base;

	int (*flip)(void *);
	void *flip_data;
};

#define nouveau_software_context_create(p,e,c,d)                               \
	nouveau_engctx_create((p), (e), (c), (p), 0, 0, 0, (d))
#define nouveau_software_context_destroy(d)                                    \
	nouveau_engctx_destroy(&(d)->base)
#define nouveau_software_context_init(d)                                       \
	nouveau_engctx_init(&(d)->base)
#define nouveau_software_context_fini(d,s)                                     \
	nouveau_engctx_fini(&(d)->base, (s))

#define _nouveau_software_context_dtor _nouveau_engctx_dtor
#define _nouveau_software_context_init _nouveau_engctx_init
#define _nouveau_software_context_fini _nouveau_engctx_fini

struct nouveau_software {
	struct nouveau_engine base;
};

#define nouveau_software_create(p,e,c,d)                                       \
	nouveau_engine_create((p), (e), (c), true, "SW", "software", (d))
#define nouveau_software_destroy(d)                                            \
	nouveau_engine_destroy(&(d)->base)
#define nouveau_software_init(d)                                               \
	nouveau_engine_init(&(d)->base)
#define nouveau_software_fini(d,s)                                             \
	nouveau_engine_fini(&(d)->base, (s))

#define _nouveau_software_dtor _nouveau_engine_dtor
#define _nouveau_software_init _nouveau_engine_init
#define _nouveau_software_fini _nouveau_engine_fini

extern struct nouveau_oclass *nv04_software_oclass;
extern struct nouveau_oclass *nv10_software_oclass;
extern struct nouveau_oclass *nv50_software_oclass;
extern struct nouveau_oclass *nvc0_software_oclass;

void nv04_software_intr(struct nouveau_subdev *);

#endif

#ifndef __NVKM_DISP_PRIV_H__
#define __NVKM_DISP_PRIV_H__

#include <subdev/bios.h>
#include <subdev/bios/dcb.h>
#include <subdev/bios/conn.h>

#include <engine/disp.h>

struct nouveau_disp_impl {
	struct nouveau_oclass base;
	struct nouveau_oclass **outp;
	struct nouveau_oclass **conn;
};

#define nouveau_disp_create(p,e,c,h,i,x,d)                                     \
	nouveau_disp_create_((p), (e), (c), (h), (i), (x),                     \
			     sizeof(**d), (void **)d)
#define nouveau_disp_destroy(d) ({                                             \
	struct nouveau_disp *disp = (d);                                       \
	_nouveau_disp_dtor(nv_object(disp));                                   \
})
#define nouveau_disp_init(d) ({                                                \
	struct nouveau_disp *disp = (d);                                       \
	_nouveau_disp_init(nv_object(disp));                                   \
})
#define nouveau_disp_fini(d,s) ({                                              \
	struct nouveau_disp *disp = (d);                                       \
	_nouveau_disp_fini(nv_object(disp), (s));                              \
})

int  nouveau_disp_create_(struct nouveau_object *, struct nouveau_object *,
			  struct nouveau_oclass *, int heads,
			  const char *, const char *, int, void **);
void _nouveau_disp_dtor(struct nouveau_object *);
int  _nouveau_disp_init(struct nouveau_object *);
int  _nouveau_disp_fini(struct nouveau_object *, bool);

extern struct nouveau_oclass *nvkm_output_oclass;
extern struct nouveau_oclass *nvkm_connector_oclass;

#endif

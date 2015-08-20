#ifndef __NVKM_DISP_PRIV_H__
#define __NVKM_DISP_PRIV_H__
#include <engine/disp.h>
#include "outp.h"
#include "outpdp.h"

struct nvkm_disp_func_outp {
	int (* crt)(struct nvkm_disp *, int index, struct dcb_output *,
		    struct nvkm_output **);
	int (*  tv)(struct nvkm_disp *, int index, struct dcb_output *,
		    struct nvkm_output **);
	int (*tmds)(struct nvkm_disp *, int index, struct dcb_output *,
		    struct nvkm_output **);
	int (*lvds)(struct nvkm_disp *, int index, struct dcb_output *,
		    struct nvkm_output **);
	int (*  dp)(struct nvkm_disp *, int index, struct dcb_output *,
		    struct nvkm_output **);
};

struct nvkm_disp_impl {
	struct nvkm_oclass base;
	struct {
		const struct nvkm_disp_func_outp internal;
		const struct nvkm_disp_func_outp external;
	} outp;
	const struct nvkm_event_func *vblank;
};

#define nvkm_disp_create(p,e,c,h,i,x,d)                                     \
	nvkm_disp_create_((p), (e), (c), (h), (i), (x),                     \
			     sizeof(**d), (void **)d)
#define nvkm_disp_destroy(d) ({                                             \
	struct nvkm_disp *disp = (d);                                       \
	_nvkm_disp_dtor(nv_object(disp));                                   \
})
#define nvkm_disp_init(d) ({                                                \
	struct nvkm_disp *disp = (d);                                       \
	_nvkm_disp_init(nv_object(disp));                                   \
})
#define nvkm_disp_fini(d,s) ({                                              \
	struct nvkm_disp *disp = (d);                                       \
	_nvkm_disp_fini(nv_object(disp), (s));                              \
})

int  nvkm_disp_create_(struct nvkm_object *, struct nvkm_object *,
			  struct nvkm_oclass *, int heads,
			  const char *, const char *, int, void **);
void _nvkm_disp_dtor(struct nvkm_object *);
int  _nvkm_disp_init(struct nvkm_object *);
int  _nvkm_disp_fini(struct nvkm_object *, bool);

extern struct nvkm_oclass *nvkm_output_oclass;
extern struct nvkm_oclass *nvkm_connector_oclass;

int  nvkm_disp_vblank_ctor(struct nvkm_object *, void *data, u32 size,
			   struct nvkm_notify *);
void nvkm_disp_vblank(struct nvkm_disp *, int head);
int  nvkm_disp_ntfy(struct nvkm_object *, u32, struct nvkm_event **);

extern struct nvkm_oclass nv04_disp_sclass[];
#endif

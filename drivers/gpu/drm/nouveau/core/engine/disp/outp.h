#ifndef __NVKM_DISP_OUTP_H__
#define __NVKM_DISP_OUTP_H__

#include "priv.h"

struct nvkm_output {
	struct nouveau_object base;
	struct list_head head;

	struct dcb_output info;
	int index;
	int or;

	struct nouveau_i2c_port *port;
	struct nouveau_i2c_port *edid;

	struct nvkm_connector *conn;
};

#define nvkm_output_create(p,e,c,b,i,d)                                        \
	nvkm_output_create_((p), (e), (c), (b), (i), sizeof(**d), (void **)d)
#define nvkm_output_destroy(d) ({                                              \
	struct nvkm_output *_outp = (d);                                       \
	_nvkm_output_dtor(nv_object(_outp));                                   \
})
#define nvkm_output_init(d) ({                                                 \
	struct nvkm_output *_outp = (d);                                       \
	_nvkm_output_init(nv_object(_outp));                                   \
})
#define nvkm_output_fini(d,s) ({                                               \
	struct nvkm_output *_outp = (d);                                       \
	_nvkm_output_fini(nv_object(_outp), (s));                              \
})

int nvkm_output_create_(struct nouveau_object *, struct nouveau_object *,
			struct nouveau_oclass *, struct dcb_output *,
			int, int, void **);

int  _nvkm_output_ctor(struct nouveau_object *, struct nouveau_object *,
		       struct nouveau_oclass *, void *, u32,
		       struct nouveau_object **);
void _nvkm_output_dtor(struct nouveau_object *);
int  _nvkm_output_init(struct nouveau_object *);
int  _nvkm_output_fini(struct nouveau_object *, bool);

struct nvkm_output_impl {
	struct nouveau_oclass base;
};

#ifndef MSG
#define MSG(l,f,a...) do {                                                     \
	struct nvkm_output *_outp = (void *)outp;                              \
	nv_##l(nv_object(outp)->engine, "%02x:%04x:%04x: "f, _outp->index,     \
	       _outp->info.hasht, _outp->info.hashm, ##a);                     \
} while(0)
#define DBG(f,a...) MSG(debug, f, ##a)
#define ERR(f,a...) MSG(error, f, ##a)
#endif

#endif

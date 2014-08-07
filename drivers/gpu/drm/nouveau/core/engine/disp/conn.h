#ifndef __NVKM_DISP_CONN_H__
#define __NVKM_DISP_CONN_H__

#include "priv.h"

struct nvkm_connector {
	struct nouveau_object base;
	struct list_head head;

	struct nvbios_connE info;
	int index;

	struct {
		struct nouveau_eventh *event;
		struct work_struct work;
	} hpd;
};

#define nvkm_connector_create(p,e,c,b,i,d)                                     \
	nvkm_connector_create_((p), (e), (c), (b), (i), sizeof(**d), (void **)d)
#define nvkm_connector_destroy(d) ({                                           \
	struct nvkm_connector *disp = (d);                                     \
	_nvkm_connector_dtor(nv_object(disp));                                 \
})
#define nvkm_connector_init(d) ({                                              \
	struct nvkm_connector *disp = (d);                                     \
	_nvkm_connector_init(nv_object(disp));                                 \
})
#define nvkm_connector_fini(d,s) ({                                            \
	struct nvkm_connector *disp = (d);                                     \
	_nvkm_connector_fini(nv_object(disp), (s));                            \
})

int nvkm_connector_create_(struct nouveau_object *, struct nouveau_object *,
			   struct nouveau_oclass *, struct nvbios_connE *,
			   int, int, void **);

int  _nvkm_connector_ctor(struct nouveau_object *, struct nouveau_object *,
			  struct nouveau_oclass *, void *, u32,
			  struct nouveau_object **);
void _nvkm_connector_dtor(struct nouveau_object *);
int  _nvkm_connector_init(struct nouveau_object *);
int  _nvkm_connector_fini(struct nouveau_object *, bool);

struct nvkm_connector_impl {
	struct nouveau_oclass base;
};

#ifndef MSG
#define MSG(l,f,a...) do {                                                     \
	struct nvkm_connector *_conn = (void *)conn;                           \
	nv_##l(nv_object(conn)->engine, "%02x:%02x%02x: "f, _conn->index,      \
	       _conn->info.location, _conn->info.type, ##a);                   \
} while(0)
#define DBG(f,a...) MSG(debug, f, ##a)
#define ERR(f,a...) MSG(error, f, ##a)
#endif

#endif

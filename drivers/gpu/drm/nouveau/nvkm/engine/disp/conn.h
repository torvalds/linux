#ifndef __NVKM_DISP_CONN_H__
#define __NVKM_DISP_CONN_H__
#include <core/object.h>
#include <core/notify.h>

#include <subdev/bios.h>
#include <subdev/bios/conn.h>

struct nvkm_connector {
	struct nvkm_object base;
	struct list_head head;

	struct nvbios_connE info;
	int index;

	struct nvkm_notify hpd;
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

int nvkm_connector_create_(struct nvkm_object *, struct nvkm_object *,
			   struct nvkm_oclass *, struct nvbios_connE *,
			   int, int, void **);

int  _nvkm_connector_ctor(struct nvkm_object *, struct nvkm_object *,
			  struct nvkm_oclass *, void *, u32,
			  struct nvkm_object **);
void _nvkm_connector_dtor(struct nvkm_object *);
int  _nvkm_connector_init(struct nvkm_object *);
int  _nvkm_connector_fini(struct nvkm_object *, bool);

struct nvkm_connector_impl {
	struct nvkm_oclass base;
};

#ifndef MSG
#define MSG(l,f,a...) do {                                                     \
	struct nvkm_connector *_conn = (void *)conn;                           \
	nv_##l(_conn, "%02x:%02x%02x: "f, _conn->index,                        \
	       _conn->info.location, _conn->info.type, ##a);                   \
} while(0)
#define DBG(f,a...) MSG(debug, f, ##a)
#define ERR(f,a...) MSG(error, f, ##a)
#endif
#endif

#ifndef __NVKM_DISP_CONN_H__
#define __NVKM_DISP_CONN_H__
#include <engine/disp.h>

#include <core/notify.h>
#include <subdev/bios.h>
#include <subdev/bios/conn.h>

struct nvkm_conn {
	struct nvkm_disp *disp;
	int index;
	struct nvbios_connE info;

	struct nvkm_notify hpd;

	struct list_head head;
};

int nvkm_conn_new(struct nvkm_disp *, int index, struct nvbios_connE *,
		  struct nvkm_conn **);
void nvkm_conn_del(struct nvkm_conn **);
void nvkm_conn_init(struct nvkm_conn *);
void nvkm_conn_fini(struct nvkm_conn *);

#define CONN_MSG(c,l,f,a...) do {                                              \
	struct nvkm_conn *_conn = (c);                                    \
	nvkm_##l(&_conn->disp->engine.subdev, "conn %02x:%02x%02x: "f"\n",     \
		 _conn->index, _conn->info.location, _conn->info.type, ##a);   \
} while(0)
#define CONN_ERR(c,f,a...) CONN_MSG((c), error, f, ##a)
#define CONN_DBG(c,f,a...) CONN_MSG((c), debug, f, ##a)
#define CONN_TRACE(c,f,a...) CONN_MSG((c), trace, f, ##a)
#endif

#ifndef __NVKM_DISP_OUTP_H__
#define __NVKM_DISP_OUTP_H__
#include <engine/disp.h>

#include <subdev/bios.h>
#include <subdev/bios/dcb.h>

struct nvkm_outp {
	const struct nvkm_outp_func *func;
	struct nvkm_disp *disp;
	int index;
	struct dcb_output info;

	struct nvkm_i2c_bus *i2c;
	int or;

	struct list_head head;
	struct nvkm_conn *conn;

	/* Assembly state. */
#define NVKM_OUTP_PRIV 1
#define NVKM_OUTP_USER 2
	u8 acquired:2;
	struct nvkm_ior *ior;
};

int nvkm_outp_ctor(const struct nvkm_outp_func *, struct nvkm_disp *,
		   int index, struct dcb_output *, struct nvkm_outp *);
int nvkm_outp_new(struct nvkm_disp *, int index, struct dcb_output *,
		  struct nvkm_outp **);
void nvkm_outp_del(struct nvkm_outp **);
void nvkm_outp_init(struct nvkm_outp *);
void nvkm_outp_fini(struct nvkm_outp *);
int nvkm_outp_acquire(struct nvkm_outp *, u8 user);
void nvkm_outp_release(struct nvkm_outp *, u8 user);
void nvkm_outp_route(struct nvkm_disp *);

struct nvkm_outp_func {
	void *(*dtor)(struct nvkm_outp *);
	void (*init)(struct nvkm_outp *);
	void (*fini)(struct nvkm_outp *);
	int (*acquire)(struct nvkm_outp *);
	void (*release)(struct nvkm_outp *, struct nvkm_ior *);
};

#define OUTP_MSG(o,l,f,a...) do {                                              \
	struct nvkm_outp *_outp = (o);                                         \
	nvkm_##l(&_outp->disp->engine.subdev, "outp %02x:%04x:%04x: "f"\n",    \
		 _outp->index, _outp->info.hasht, _outp->info.hashm, ##a);     \
} while(0)
#define OUTP_ERR(o,f,a...) OUTP_MSG((o), error, f, ##a)
#define OUTP_DBG(o,f,a...) OUTP_MSG((o), debug, f, ##a)
#define OUTP_TRACE(o,f,a...) OUTP_MSG((o), trace, f, ##a)
#endif

#ifndef __NVKM_I2C_PAD_H__
#define __NVKM_I2C_PAD_H__
#include "priv.h"

struct nvkm_i2c_pad {
	struct nvkm_object base;
	int index;
	struct nvkm_i2c_port *port;
	struct nvkm_i2c_port *next;
};

static inline struct nvkm_i2c_pad *
nvkm_i2c_pad(struct nvkm_i2c_port *port)
{
	struct nvkm_object *pad = nv_object(port);
	while (!nv_iclass(pad->parent, NV_SUBDEV_CLASS))
		pad = pad->parent;
	return (void *)pad;
}

#define nvkm_i2c_pad_create(p,e,o,i,d)                                         \
	nvkm_i2c_pad_create_((p), (e), (o), (i), sizeof(**d), (void **)d)
#define nvkm_i2c_pad_destroy(p) ({                                             \
	struct nvkm_i2c_pad *_p = (p);                                         \
	_nvkm_i2c_pad_dtor(nv_object(_p));                                     \
})
#define nvkm_i2c_pad_init(p) ({                                                \
	struct nvkm_i2c_pad *_p = (p);                                         \
	_nvkm_i2c_pad_init(nv_object(_p));                                     \
})
#define nvkm_i2c_pad_fini(p,s) ({                                              \
	struct nvkm_i2c_pad *_p = (p);                                         \
	_nvkm_i2c_pad_fini(nv_object(_p), (s));                                \
})

int nvkm_i2c_pad_create_(struct nvkm_object *, struct nvkm_object *,
			 struct nvkm_oclass *, int index, int, void **);

int _nvkm_i2c_pad_ctor(struct nvkm_object *, struct nvkm_object *,
		       struct nvkm_oclass *, void *, u32,
		       struct nvkm_object **);
#define _nvkm_i2c_pad_dtor nvkm_object_destroy
int _nvkm_i2c_pad_init(struct nvkm_object *);
int _nvkm_i2c_pad_fini(struct nvkm_object *, bool);

#ifndef MSG
#define MSG(l,f,a...) do {                                                     \
	struct nvkm_i2c_pad *_pad = (void *)pad;                               \
	nv_##l(_pad, "PAD:%c:%02x: "f,                                         \
	       _pad->index >= 0x100 ? 'X' : 'S',                               \
	       _pad->index >= 0x100 ? _pad->index - 0x100 : _pad->index, ##a); \
} while(0)
#define DBG(f,a...) MSG(debug, f, ##a)
#define ERR(f,a...) MSG(error, f, ##a)
#endif
#endif

#ifndef __NVKM_I2C_PAD_H__
#define __NVKM_I2C_PAD_H__

#include "priv.h"

struct nvkm_i2c_pad {
	struct nouveau_object base;
	int index;
	struct nouveau_i2c_port *port;
	struct nouveau_i2c_port *next;
};

static inline struct nvkm_i2c_pad *
nvkm_i2c_pad(struct nouveau_i2c_port *port)
{
	struct nouveau_object *pad = nv_object(port);
	while (pad->parent)
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

int nvkm_i2c_pad_create_(struct nouveau_object *, struct nouveau_object *,
			 struct nouveau_oclass *, int index, int, void **);

int _nvkm_i2c_pad_ctor(struct nouveau_object *, struct nouveau_object *,
		       struct nouveau_oclass *, void *, u32,
		       struct nouveau_object **);
#define _nvkm_i2c_pad_dtor nouveau_object_destroy
int _nvkm_i2c_pad_init(struct nouveau_object *);
int _nvkm_i2c_pad_fini(struct nouveau_object *, bool);

#ifndef MSG
#define MSG(l,f,a...) do {                                                     \
	struct nvkm_i2c_pad *_pad = (void *)pad;                               \
	nv_##l(nv_object(_pad)->engine, "PAD:%c:%02x: "f,                      \
	       _pad->index >= 0x100 ? 'X' : 'S',                               \
	       _pad->index >= 0x100 ? _pad->index - 0x100 : _pad->index, ##a); \
} while(0)
#define DBG(f,a...) MSG(debug, f, ##a)
#define ERR(f,a...) MSG(error, f, ##a)
#endif

#endif

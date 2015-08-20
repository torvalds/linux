#ifndef __NVKM_I2C_PRIV_H__
#define __NVKM_I2C_PRIV_H__
#include <subdev/i2c.h>

#define nvkm_i2c_create(p,e,o,d)                                            \
	nvkm_i2c_create_((p), (e), (o), sizeof(**d), (void **)d)
#define nvkm_i2c_destroy(p) ({                                              \
	struct nvkm_i2c *i2c = (p);                                         \
	_nvkm_i2c_dtor(nv_object(i2c));                                     \
})
#define nvkm_i2c_init(p) ({                                                 \
	struct nvkm_i2c *i2c = (p);                                         \
	_nvkm_i2c_init(nv_object(i2c));                                     \
})
#define nvkm_i2c_fini(p,s) ({                                               \
	struct nvkm_i2c *i2c = (p);                                         \
	_nvkm_i2c_fini(nv_object(i2c), (s));                                \
})

int nvkm_i2c_create_(struct nvkm_object *, struct nvkm_object *,
			struct nvkm_oclass *, int, void **);
int  _nvkm_i2c_ctor(struct nvkm_object *, struct nvkm_object *,
		       struct nvkm_oclass *, void *, u32,
		       struct nvkm_object **);
void _nvkm_i2c_dtor(struct nvkm_object *);
int  _nvkm_i2c_init(struct nvkm_object *);
int  _nvkm_i2c_fini(struct nvkm_object *, bool);

struct nvkm_i2c_impl {
	struct nvkm_oclass base;

	int (*pad_x_new)(struct nvkm_i2c *, int id, struct nvkm_i2c_pad **);
	int (*pad_s_new)(struct nvkm_i2c *, int id, struct nvkm_i2c_pad **);

	/* number of native dp aux channels present */
	int aux;

	/* read and ack pending interrupts, returning only data
	 * for ports that have not been masked off, while still
	 * performing the ack for anything that was pending.
	 */
	void (*aux_stat)(struct nvkm_i2c *, u32 *, u32 *, u32 *, u32 *);

	/* mask on/off interrupt types for a given set of auxch
	 */
	void (*aux_mask)(struct nvkm_i2c *, u32, u32, u32);
};

void g94_aux_stat(struct nvkm_i2c *, u32 *, u32 *, u32 *, u32 *);
void g94_aux_mask(struct nvkm_i2c *, u32, u32, u32);

void gk104_aux_stat(struct nvkm_i2c *, u32 *, u32 *, u32 *, u32 *);
void gk104_aux_mask(struct nvkm_i2c *, u32, u32, u32);
#endif

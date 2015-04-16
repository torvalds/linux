#ifndef __NVKM_I2C_PRIV_H__
#define __NVKM_I2C_PRIV_H__
#include <subdev/i2c.h>

extern struct nvkm_oclass nv04_i2c_pad_oclass;
extern struct nvkm_oclass g94_i2c_pad_oclass;
extern struct nvkm_oclass gm204_i2c_pad_oclass;

#define nvkm_i2c_port_create(p,e,o,i,a,f,d)                                 \
	nvkm_i2c_port_create_((p), (e), (o), (i), (a), (f),                 \
				 sizeof(**d), (void **)d)
#define nvkm_i2c_port_destroy(p) ({                                         \
	struct nvkm_i2c_port *port = (p);                                   \
	_nvkm_i2c_port_dtor(nv_object(i2c));                                \
})
#define nvkm_i2c_port_init(p)                                               \
	nvkm_object_init(&(p)->base)
#define nvkm_i2c_port_fini(p,s)                                             \
	nvkm_object_fini(&(p)->base, (s))

int nvkm_i2c_port_create_(struct nvkm_object *, struct nvkm_object *,
			     struct nvkm_oclass *, u8,
			     const struct i2c_algorithm *,
			     const struct nvkm_i2c_func *,
			     int, void **);
void _nvkm_i2c_port_dtor(struct nvkm_object *);
#define _nvkm_i2c_port_init nvkm_object_init
int  _nvkm_i2c_port_fini(struct nvkm_object *, bool);

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

extern struct nvkm_oclass nvkm_anx9805_sclass[];
extern struct nvkm_oclass gf110_i2c_sclass[];

extern const struct i2c_algorithm nvkm_i2c_bit_algo;
extern const struct i2c_algorithm nvkm_i2c_aux_algo;

struct nvkm_i2c_impl {
	struct nvkm_oclass base;

	/* supported i2c port classes */
	struct nvkm_oclass *sclass;
	struct nvkm_oclass *pad_x;
	struct nvkm_oclass *pad_s;

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

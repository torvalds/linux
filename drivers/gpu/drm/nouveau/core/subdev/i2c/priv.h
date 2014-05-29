#ifndef __NVKM_I2C_H__
#define __NVKM_I2C_H__

#include <subdev/i2c.h>

extern struct nouveau_oclass nv04_i2c_pad_oclass;
extern struct nouveau_oclass nv94_i2c_pad_oclass;

#define nouveau_i2c_port_create(p,e,o,i,a,f,d)                                 \
	nouveau_i2c_port_create_((p), (e), (o), (i), (a), (f),                 \
				 sizeof(**d), (void **)d)
#define nouveau_i2c_port_destroy(p) ({                                         \
	struct nouveau_i2c_port *port = (p);                                   \
	_nouveau_i2c_port_dtor(nv_object(i2c));                                \
})
#define nouveau_i2c_port_init(p)                                               \
	nouveau_object_init(&(p)->base)
#define nouveau_i2c_port_fini(p,s)                                             \
	nouveau_object_fini(&(p)->base, (s))

int nouveau_i2c_port_create_(struct nouveau_object *, struct nouveau_object *,
			     struct nouveau_oclass *, u8,
			     const struct i2c_algorithm *,
			     const struct nouveau_i2c_func *,
			     int, void **);
void _nouveau_i2c_port_dtor(struct nouveau_object *);
#define _nouveau_i2c_port_init nouveau_object_init
int  _nouveau_i2c_port_fini(struct nouveau_object *, bool);

#define nouveau_i2c_create(p,e,o,d)                                            \
	nouveau_i2c_create_((p), (e), (o), sizeof(**d), (void **)d)
#define nouveau_i2c_destroy(p) ({                                              \
	struct nouveau_i2c *i2c = (p);                                         \
	_nouveau_i2c_dtor(nv_object(i2c));                                     \
})
#define nouveau_i2c_init(p) ({                                                 \
	struct nouveau_i2c *i2c = (p);                                         \
	_nouveau_i2c_init(nv_object(i2c));                                     \
})
#define nouveau_i2c_fini(p,s) ({                                               \
	struct nouveau_i2c *i2c = (p);                                         \
	_nouveau_i2c_fini(nv_object(i2c), (s));                                \
})

int nouveau_i2c_create_(struct nouveau_object *, struct nouveau_object *,
			struct nouveau_oclass *, int, void **);
int  _nouveau_i2c_ctor(struct nouveau_object *, struct nouveau_object *,
		       struct nouveau_oclass *, void *, u32,
		       struct nouveau_object **);
void _nouveau_i2c_dtor(struct nouveau_object *);
int  _nouveau_i2c_init(struct nouveau_object *);
int  _nouveau_i2c_fini(struct nouveau_object *, bool);

extern struct nouveau_oclass nouveau_anx9805_sclass[];
extern struct nouveau_oclass nvd0_i2c_sclass[];

extern const struct i2c_algorithm nouveau_i2c_bit_algo;
extern const struct i2c_algorithm nouveau_i2c_aux_algo;

struct nouveau_i2c_impl {
	struct nouveau_oclass base;

	/* supported i2c port classes */
	struct nouveau_oclass *sclass;
	struct nouveau_oclass *pad_x;
	struct nouveau_oclass *pad_s;

	/* number of native dp aux channels present */
	int aux;

	/* read and ack pending interrupts, returning only data
	 * for ports that have not been masked off, while still
	 * performing the ack for anything that was pending.
	 */
	void (*aux_stat)(struct nouveau_i2c *, u32 *, u32 *, u32 *, u32 *);

	/* mask on/off interrupt types for a given set of auxch
	 */
	void (*aux_mask)(struct nouveau_i2c *, u32, u32, u32);
};

void nv94_aux_stat(struct nouveau_i2c *, u32 *, u32 *, u32 *, u32 *);
void nv94_aux_mask(struct nouveau_i2c *, u32, u32, u32);

#endif

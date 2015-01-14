#ifndef __NVKM_GPIO_PRIV_H__
#define __NVKM_GPIO_PRIV_H__
#include <subdev/gpio.h>

#define nvkm_gpio_create(p,e,o,d)                                           \
	nvkm_gpio_create_((p), (e), (o), sizeof(**d), (void **)d)
#define nvkm_gpio_destroy(p) ({                                             \
	struct nvkm_gpio *gpio = (p);                                       \
	_nvkm_gpio_dtor(nv_object(gpio));                                   \
})
#define nvkm_gpio_init(p) ({                                                \
	struct nvkm_gpio *gpio = (p);                                       \
	_nvkm_gpio_init(nv_object(gpio));                                   \
})
#define nvkm_gpio_fini(p,s) ({                                              \
	struct nvkm_gpio *gpio = (p);                                       \
	_nvkm_gpio_fini(nv_object(gpio), (s));                              \
})

int  nvkm_gpio_create_(struct nvkm_object *, struct nvkm_object *,
			  struct nvkm_oclass *, int, void **);
int  _nvkm_gpio_ctor(struct nvkm_object *, struct nvkm_object *,
			struct nvkm_oclass *, void *, u32,
			struct nvkm_object **);
void _nvkm_gpio_dtor(struct nvkm_object *);
int  _nvkm_gpio_init(struct nvkm_object *);
int  _nvkm_gpio_fini(struct nvkm_object *, bool);

struct nvkm_gpio_impl {
	struct nvkm_oclass base;
	int lines;

	/* read and ack pending interrupts, returning only data
	 * for lines that have not been masked off, while still
	 * performing the ack for anything that was pending.
	 */
	void (*intr_stat)(struct nvkm_gpio *, u32 *, u32 *);

	/* mask on/off interrupts for hi/lo transitions on a
	 * given set of gpio lines
	 */
	void (*intr_mask)(struct nvkm_gpio *, u32, u32, u32);

	/* configure gpio direction and output value */
	int  (*drive)(struct nvkm_gpio *, int line, int dir, int out);

	/* sense current state of given gpio line */
	int  (*sense)(struct nvkm_gpio *, int line);

	/*XXX*/
	void (*reset)(struct nvkm_gpio *, u8);
};

void nv50_gpio_reset(struct nvkm_gpio *, u8);
int  nv50_gpio_drive(struct nvkm_gpio *, int, int, int);
int  nv50_gpio_sense(struct nvkm_gpio *, int);

void g94_gpio_intr_stat(struct nvkm_gpio *, u32 *, u32 *);
void g94_gpio_intr_mask(struct nvkm_gpio *, u32, u32, u32);

void gf110_gpio_reset(struct nvkm_gpio *, u8);
int  gf110_gpio_drive(struct nvkm_gpio *, int, int, int);
int  gf110_gpio_sense(struct nvkm_gpio *, int);
#endif

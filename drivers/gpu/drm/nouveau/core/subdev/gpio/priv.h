#ifndef __NVKM_GPIO_H__
#define __NVKM_GPIO_H__

#include <subdev/gpio.h>

#define nouveau_gpio_create(p,e,o,d)                                           \
	nouveau_gpio_create_((p), (e), (o), sizeof(**d), (void **)d)
#define nouveau_gpio_destroy(p) ({                                             \
	struct nouveau_gpio *gpio = (p);                                       \
	_nouveau_gpio_dtor(nv_object(gpio));                                   \
})
#define nouveau_gpio_init(p) ({                                                \
	struct nouveau_gpio *gpio = (p);                                       \
	_nouveau_gpio_init(nv_object(gpio));                                   \
})
#define nouveau_gpio_fini(p,s) ({                                              \
	struct nouveau_gpio *gpio = (p);                                       \
	_nouveau_gpio_fini(nv_object(gpio), (s));                              \
})

int  nouveau_gpio_create_(struct nouveau_object *, struct nouveau_object *,
			  struct nouveau_oclass *, int, void **);
int  _nouveau_gpio_ctor(struct nouveau_object *, struct nouveau_object *,
			struct nouveau_oclass *, void *, u32,
			struct nouveau_object **);
void _nouveau_gpio_dtor(struct nouveau_object *);
int  _nouveau_gpio_init(struct nouveau_object *);
int  _nouveau_gpio_fini(struct nouveau_object *, bool);

struct nouveau_gpio_impl {
	struct nouveau_oclass base;
	int lines;

	/* read and ack pending interrupts, returning only data
	 * for lines that have not been masked off, while still
	 * performing the ack for anything that was pending.
	 */
	void (*intr_stat)(struct nouveau_gpio *, u32 *, u32 *);

	/* mask on/off interrupts for hi/lo transitions on a
	 * given set of gpio lines
	 */
	void (*intr_mask)(struct nouveau_gpio *, u32, u32, u32);

	/* configure gpio direction and output value */
	int  (*drive)(struct nouveau_gpio *, int line, int dir, int out);

	/* sense current state of given gpio line */
	int  (*sense)(struct nouveau_gpio *, int line);

	/*XXX*/
	void (*reset)(struct nouveau_gpio *, u8);
};

void nv50_gpio_reset(struct nouveau_gpio *, u8);
int  nv50_gpio_drive(struct nouveau_gpio *, int, int, int);
int  nv50_gpio_sense(struct nouveau_gpio *, int);

void nv94_gpio_intr_stat(struct nouveau_gpio *, u32 *, u32 *);
void nv94_gpio_intr_mask(struct nouveau_gpio *, u32, u32, u32);

void nvd0_gpio_reset(struct nouveau_gpio *, u8);
int  nvd0_gpio_drive(struct nouveau_gpio *, int, int, int);
int  nvd0_gpio_sense(struct nouveau_gpio *, int);


#endif

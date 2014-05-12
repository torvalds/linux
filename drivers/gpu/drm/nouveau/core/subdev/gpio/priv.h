#ifndef __NVKM_GPIO_H__
#define __NVKM_GPIO_H__

#include <subdev/gpio.h>

#define nouveau_gpio_create(p,e,o,d)                                           \
	nouveau_gpio_create_((p), (e), (o), sizeof(**d), (void **)d)
#define nouveau_gpio_destroy(p) ({                                             \
	struct nouveau_gpio *gpio = (p);                                       \
	_nouveau_gpio_dtor(nv_object(gpio));                                   \
})
#define nouveau_gpio_fini(p,s)                                                 \
	nouveau_subdev_fini(&(p)->base, (s))

int  nouveau_gpio_create_(struct nouveau_object *, struct nouveau_object *,
			  struct nouveau_oclass *, int, void **);
void _nouveau_gpio_dtor(struct nouveau_object *);
int  nouveau_gpio_init(struct nouveau_gpio *);

int  nv50_gpio_ctor(struct nouveau_object *, struct nouveau_object *,
		    struct nouveau_oclass *, void *, u32,
		    struct nouveau_object **);
void nv50_gpio_dtor(struct nouveau_object *);
int  nv50_gpio_init(struct nouveau_object *);
int  nv50_gpio_fini(struct nouveau_object *, bool);

void nvd0_gpio_reset(struct nouveau_gpio *, u8);
int  nvd0_gpio_drive(struct nouveau_gpio *, int, int, int);
int  nvd0_gpio_sense(struct nouveau_gpio *, int);

enum nvkm_gpio_event {
	NVKM_GPIO_HI = 1,
	NVKM_GPIO_LO = 2,
	NVKM_GPIO_TOGGLED = (NVKM_GPIO_HI | NVKM_GPIO_LO),
};

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
};

void nv92_gpio_intr_stat(struct nouveau_gpio *, u32 *, u32 *);
void nv92_gpio_intr_mask(struct nouveau_gpio *, u32, u32, u32);


#endif

#ifndef __NOUVEAU_GPIO_H__
#define __NOUVEAU_GPIO_H__

#include <core/subdev.h>
#include <core/device.h>
#include <core/event.h>

#include <subdev/bios.h>
#include <subdev/bios/gpio.h>

struct nouveau_gpio {
	struct nouveau_subdev base;

	struct nouveau_event *events;

	/* hardware interfaces */
	void (*reset)(struct nouveau_gpio *, u8 func);
	int  (*drive)(struct nouveau_gpio *, int line, int dir, int out);
	int  (*sense)(struct nouveau_gpio *, int line);

	/* software interfaces */
	int  (*find)(struct nouveau_gpio *, int idx, u8 tag, u8 line,
		     struct dcb_gpio_func *);
	int  (*set)(struct nouveau_gpio *, int idx, u8 tag, u8 line, int state);
	int  (*get)(struct nouveau_gpio *, int idx, u8 tag, u8 line);
};

static inline struct nouveau_gpio *
nouveau_gpio(void *obj)
{
	return (void *)nv_device(obj)->subdev[NVDEV_SUBDEV_GPIO];
}

#define nouveau_gpio_create(p,e,o,l,d)                                         \
	nouveau_gpio_create_((p), (e), (o), (l), sizeof(**d), (void **)d)
#define nouveau_gpio_destroy(p) ({                                             \
	struct nouveau_gpio *gpio = (p);                                       \
	_nouveau_gpio_dtor(nv_object(gpio));                                   \
})
#define nouveau_gpio_fini(p,s)                                                 \
	nouveau_subdev_fini(&(p)->base, (s))

int  nouveau_gpio_create_(struct nouveau_object *, struct nouveau_object *,
			  struct nouveau_oclass *, int, int, void **);
void _nouveau_gpio_dtor(struct nouveau_object *);
int  nouveau_gpio_init(struct nouveau_gpio *);

extern struct nouveau_oclass nv10_gpio_oclass;
extern struct nouveau_oclass nv50_gpio_oclass;
extern struct nouveau_oclass nvd0_gpio_oclass;
extern struct nouveau_oclass nve0_gpio_oclass;

#endif

#ifndef __NOUVEAU_GPIO_H__
#define __NOUVEAU_GPIO_H__

#include <core/subdev.h>
#include <core/device.h>
#include <core/event.h>

#include <subdev/bios.h>
#include <subdev/bios/gpio.h>

enum nvkm_gpio_event {
	NVKM_GPIO_HI = 1,
	NVKM_GPIO_LO = 2,
	NVKM_GPIO_TOGGLED = (NVKM_GPIO_HI | NVKM_GPIO_LO),
};

struct nouveau_gpio {
	struct nouveau_subdev base;

	struct nouveau_event *events;

	void (*reset)(struct nouveau_gpio *, u8 func);
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

extern struct nouveau_oclass *nv10_gpio_oclass;
extern struct nouveau_oclass *nv50_gpio_oclass;
extern struct nouveau_oclass *nv92_gpio_oclass;
extern struct nouveau_oclass *nvd0_gpio_oclass;
extern struct nouveau_oclass *nve0_gpio_oclass;

#endif

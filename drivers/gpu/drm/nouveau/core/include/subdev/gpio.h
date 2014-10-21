#ifndef __NOUVEAU_GPIO_H__
#define __NOUVEAU_GPIO_H__

#include <core/subdev.h>
#include <core/device.h>
#include <core/event.h>

#include <subdev/bios.h>
#include <subdev/bios/gpio.h>

struct nvkm_gpio_ntfy_req {
#define NVKM_GPIO_HI                                                       0x01
#define NVKM_GPIO_LO                                                       0x02
#define NVKM_GPIO_TOGGLED                                                  0x03
	u8 mask;
	u8 line;
};

struct nvkm_gpio_ntfy_rep {
	u8 mask;
};

struct nouveau_gpio {
	struct nouveau_subdev base;

	struct nvkm_event event;

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
extern struct nouveau_oclass *nv94_gpio_oclass;
extern struct nouveau_oclass *nvd0_gpio_oclass;
extern struct nouveau_oclass *nve0_gpio_oclass;

#endif

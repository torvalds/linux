#ifndef __NVKM_GPIO_H__
#define __NVKM_GPIO_H__
#include <core/subdev.h>
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

struct nvkm_gpio {
	struct nvkm_subdev base;

	struct nvkm_event event;

	void (*reset)(struct nvkm_gpio *, u8 func);
	int  (*find)(struct nvkm_gpio *, int idx, u8 tag, u8 line,
		     struct dcb_gpio_func *);
	int  (*set)(struct nvkm_gpio *, int idx, u8 tag, u8 line, int state);
	int  (*get)(struct nvkm_gpio *, int idx, u8 tag, u8 line);
};

static inline struct nvkm_gpio *
nvkm_gpio(void *obj)
{
	return (void *)nvkm_subdev(obj, NVDEV_SUBDEV_GPIO);
}

extern struct nvkm_oclass *nv10_gpio_oclass;
extern struct nvkm_oclass *nv50_gpio_oclass;
extern struct nvkm_oclass *g94_gpio_oclass;
extern struct nvkm_oclass *gf110_gpio_oclass;
extern struct nvkm_oclass *gk104_gpio_oclass;
#endif

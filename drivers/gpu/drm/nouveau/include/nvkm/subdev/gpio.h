/* SPDX-License-Identifier: MIT */
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
	const struct nvkm_gpio_func *func;
	struct nvkm_subdev subdev;

	struct nvkm_event event;
};

void nvkm_gpio_reset(struct nvkm_gpio *, u8 func);
int nvkm_gpio_find(struct nvkm_gpio *, int idx, u8 tag, u8 line,
		   struct dcb_gpio_func *);
int nvkm_gpio_set(struct nvkm_gpio *, int idx, u8 tag, u8 line, int state);
int nvkm_gpio_get(struct nvkm_gpio *, int idx, u8 tag, u8 line);

int nv10_gpio_new(struct nvkm_device *, enum nvkm_subdev_type, int inst, struct nvkm_gpio **);
int nv50_gpio_new(struct nvkm_device *, enum nvkm_subdev_type, int inst, struct nvkm_gpio **);
int g94_gpio_new(struct nvkm_device *, enum nvkm_subdev_type, int inst, struct nvkm_gpio **);
int gf119_gpio_new(struct nvkm_device *, enum nvkm_subdev_type, int inst, struct nvkm_gpio **);
int gk104_gpio_new(struct nvkm_device *, enum nvkm_subdev_type, int inst, struct nvkm_gpio **);
int ga102_gpio_new(struct nvkm_device *, enum nvkm_subdev_type, int inst, struct nvkm_gpio **);
#endif

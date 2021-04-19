/* SPDX-License-Identifier: MIT */
#ifndef __NVKM_GPIO_PRIV_H__
#define __NVKM_GPIO_PRIV_H__
#define nvkm_gpio(p) container_of((p), struct nvkm_gpio, subdev)
#include <subdev/gpio.h>

struct nvkm_gpio_func {
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

int nvkm_gpio_new_(const struct nvkm_gpio_func *, struct nvkm_device *, enum nvkm_subdev_type, int,
		   struct nvkm_gpio **);

void nv50_gpio_reset(struct nvkm_gpio *, u8);
int  nv50_gpio_drive(struct nvkm_gpio *, int, int, int);
int  nv50_gpio_sense(struct nvkm_gpio *, int);

void g94_gpio_intr_stat(struct nvkm_gpio *, u32 *, u32 *);
void g94_gpio_intr_mask(struct nvkm_gpio *, u32, u32, u32);

void gf119_gpio_reset(struct nvkm_gpio *, u8);
int  gf119_gpio_drive(struct nvkm_gpio *, int, int, int);
int  gf119_gpio_sense(struct nvkm_gpio *, int);
#endif
